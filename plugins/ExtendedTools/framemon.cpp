/*
 * Process Hacker Extended Tools -
 *   Frame monitoring
 *
 * Copyright (C) 2021 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "exttools.h"
#include "framemon.h"

BOOLEAN EtFramesEnabled = FALSE;
PPH_HASHTABLE EtFramesHashTable = NULL;
PH_QUEUED_LOCK EtFramesHashTableLock = PH_QUEUED_LOCK_INIT;

static BOOLEAN NTAPI EtFramesEqualFunction(
    _In_ PVOID Entry1,
    _In_ PVOID Entry2
    )
{
    PET_FPS_COUNTER entry1 = (PET_FPS_COUNTER)Entry1;
    PET_FPS_COUNTER entry2 = (PET_FPS_COUNTER)Entry2;

    return entry1->ProcessId == entry2->ProcessId;
}

static ULONG NTAPI EtFramesHashFunction(
    _In_ PVOID Entry
    )
{
    PET_FPS_COUNTER entry = (PET_FPS_COUNTER)Entry;

    return HandleToUlong(entry->ProcessId) / 4;
}

static NTSTATUS NTAPI EtStartFpsTraceSession(
    _In_ PVOID ThreadParameter
    )
{
    StartFpsTraceSession();
    return STATUS_SUCCESS;
}

VOID EtFramesMonitorInitialization(
    VOID
    )
{
    EtFramesEnabled = !!PhGetIntegerSetting(const_cast<PWSTR>(SETTING_NAME_ENABLE_FPS_MONITOR));

    if (!EtFramesEnabled)
        return;

    EtFramesHashTable = PhCreateHashtable(
        sizeof(ET_FPS_COUNTER),
        EtFramesEqualFunction,
        EtFramesHashFunction,
        10
        );
}

VOID EtFramesMonitorUninitialization(
    VOID
    )
{
    StopFpsTraceSession();
}

VOID EtFramesMonitorStart(
    VOID
    )
{
    if (EtFramesEnabled)
    {
        PhQueueItemWorkQueue(PhGetGlobalWorkQueue(), EtStartFpsTraceSession, NULL);
    }
}

PET_FPS_COUNTER EtLookupProcessGpuFrameEntry(
    _In_ HANDLE ProcessId
    )
{
    ET_FPS_COUNTER lookupEntry;
    PET_FPS_COUNTER entry;

    lookupEntry.ProcessId = ProcessId;

    PhAcquireQueuedLockShared(&EtFramesHashTableLock);
    entry = reinterpret_cast<PET_FPS_COUNTER>(PhFindEntryHashtable(EtFramesHashTable, &lookupEntry));
    PhReleaseQueuedLockShared(&EtFramesHashTableLock);

    return entry;
}

VOID EtLockGpuFrameHashTable(
    VOID
    )
{
    PhAcquireQueuedLockExclusive(&EtFramesHashTableLock);
}

VOID EtUnlockGpuFrameHashTable(
    VOID
    )
{
#pragma warning(suppress: 26110)

    PhReleaseQueuedLockExclusive(&EtFramesHashTableLock);
}

VOID EtAddGpuFrameToHashTable(
    _In_ ULONG ProcessId,
    _In_ DOUBLE FrameMs,
    _In_ DOUBLE FramesPerSecond,
    _In_ DOUBLE DisplayLatency,
    _In_ DOUBLE DisplayFramesPerSecond,
    _In_ DOUBLE MsBetweenPresents,
    _In_ DOUBLE MsInPresentApi,
    _In_ DOUBLE MsUntilRenderComplete,
    _In_ DOUBLE MsUntilDisplayed
    )
{
    ET_FPS_COUNTER lookupEntry;
    PET_FPS_COUNTER entry;

    lookupEntry.ProcessId = UlongToHandle(ProcessId);
    entry = reinterpret_cast<PET_FPS_COUNTER>(PhFindEntryHashtable(EtFramesHashTable, &lookupEntry));

    if (entry)
    {
        if (entry->FrameMs < FrameMs)
            entry->FrameMs = FrameMs;
        if (entry->FramesPerSecond < FramesPerSecond)
            entry->FramesPerSecond = FramesPerSecond;
        if (entry->MsBetweenPresents < MsBetweenPresents)
            entry->MsBetweenPresents = MsBetweenPresents;
        if (entry->MsInPresentApi < MsInPresentApi)
            entry->MsInPresentApi = MsInPresentApi;
        if (entry->MsUntilRenderComplete < MsUntilRenderComplete)
            entry->MsUntilRenderComplete = MsUntilRenderComplete;
        if (entry->MsUntilDisplayed < MsUntilDisplayed)
            entry->MsUntilDisplayed = MsUntilDisplayed;
        if (entry->DisplayLatency < DisplayLatency)
            entry->DisplayLatency = DisplayLatency;
        //if (entry->DisplayFramesPerSecond < DisplayFramesPerSecond)
        //    entry->DisplayFramesPerSecond = DisplayFramesPerSecond;
    }
    else
    {
        lookupEntry.ProcessId = UlongToHandle(ProcessId);
        lookupEntry.FrameMs = FrameMs;
        lookupEntry.FramesPerSecond = FramesPerSecond;
        lookupEntry.MsBetweenPresents = MsBetweenPresents;
        lookupEntry.MsInPresentApi = MsInPresentApi;
        lookupEntry.MsUntilRenderComplete = MsUntilRenderComplete;
        lookupEntry.MsUntilDisplayed = MsUntilDisplayed;
        lookupEntry.DisplayLatency = DisplayLatency;
        //lookupEntry.DisplayFramesPerSecond = DisplayFramesPerSecond;

        PhAddEntryHashtable(EtFramesHashTable, &lookupEntry);
    }
}

VOID EtClearGpuFrameHashTable(
    VOID
    )
{ 
    static ULONG64 lastTickCount = 0;
    ULONG64 tickCount = NtGetTickCount64();

    if (lastTickCount == 0)
        lastTickCount = tickCount;

    // Reset hashtable once in a while.
    if (tickCount - lastTickCount >= 600 * 1000)
    {
        PhDereferenceObject(EtFramesHashTable);

        EtFramesHashTable = PhCreateHashtable(
            sizeof(ET_FPS_COUNTER),
            EtFramesEqualFunction,
            EtFramesHashFunction,
            10
            );

        lastTickCount = tickCount;
    }
    else
    {
        PhClearHashtable(EtFramesHashTable);
    }
}

VOID EtProcessFramesUpdateProcessBlock(
    _In_ struct _ET_PROCESS_BLOCK* ProcessBlock
    )
{
    PET_FPS_COUNTER entry;

    if (entry = EtLookupProcessGpuFrameEntry(ProcessBlock->ProcessItem->ProcessId))
    {
        ProcessBlock->FramesPerSecond = (FLOAT)entry->FramesPerSecond;
        ProcessBlock->FramesLatency = (FLOAT)entry->FrameMs;
        ProcessBlock->FramesMsBetweenPresents = (FLOAT)entry->MsBetweenPresents;
        ProcessBlock->FramesMsInPresentApi = (FLOAT)entry->MsInPresentApi;
        ProcessBlock->FramesMsUntilRenderComplete = (FLOAT)entry->MsUntilRenderComplete;
        ProcessBlock->FramesMsUntilDisplayed = (FLOAT)entry->MsUntilDisplayed;
        ProcessBlock->FramesDisplayLatency = (FLOAT)entry->DisplayLatency;
        //ProcessBlock->FramesDisplayFramesPerSecond = (FLOAT)entry->DisplayFramesPerSecond;

        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesPerSecondHistory, ProcessBlock->FramesPerSecond);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesLatencyHistory, ProcessBlock->FramesLatency);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsBetweenPresentsHistory, ProcessBlock->FramesMsBetweenPresents);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsInPresentApiHistory, ProcessBlock->FramesMsInPresentApi);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsUntilRenderCompleteHistory, ProcessBlock->FramesMsUntilRenderComplete);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsUntilDisplayedHistory, ProcessBlock->FramesMsUntilDisplayed);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesDisplayLatencyHistory, ProcessBlock->FramesDisplayLatency);
        //PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesDisplayFramesPerSecondHistory, ProcessBlock->FramesDisplayFramesPerSecond);
    }
    else
    {
        ProcessBlock->FramesPerSecond = 0;
        ProcessBlock->FramesLatency = 0;
        ProcessBlock->FramesMsBetweenPresents = 0;
        ProcessBlock->FramesMsInPresentApi = 0;
        ProcessBlock->FramesMsUntilRenderComplete = 0;
        ProcessBlock->FramesMsUntilDisplayed = 0;
        ProcessBlock->FramesDisplayLatency = 0;
        //ProcessBlock->FramesDisplayFramesPerSecond = 0;

        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesPerSecondHistory, 0);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesLatencyHistory, 0);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsBetweenPresentsHistory, 0);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsInPresentApiHistory, 0);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsUntilRenderCompleteHistory, 0);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesMsUntilDisplayedHistory, 0);
        PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesDisplayLatencyHistory, 0);
        //PhAddItemCircularBuffer_FLOAT(&ProcessBlock->FramesDisplayFramesPerSecondHistory, 0);
    }
}
