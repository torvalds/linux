/*************************************************************************/ /*!
@File
@Title          Process based statistics
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Manages a collection of statistics based around a process
                and referenced via OS agnostic methods.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <stddef.h>

#include "img_defs.h"
#include "img_types.h"
#include "pvr_debug.h"
#include "lock.h"
#include "allocmem.h"
#include "osfunc.h"
#include "lists.h"
#include "process_stats.h"
#include "ri_server.h"
#include "hash.h"

#define DBGTIMEDIFF(T0, T1)  ((IMG_UINT64) ( (T0) <= (T1) ? (T1) - (T0): IMG_UINT64_MAX - (T0) + (T1) ))
#define MEAN_TIME(A, B)     ( ((3*(A))/4) + ((1 * (B))/4) )


/*
 *  Maximum history of process statistics that will be kept.
 */
#define MAX_DEAD_LIST_PROCESSES  (10)

void *pvOSPowerStatsEntryData=NULL;


/*
 * Definition of all process based statistics and the strings used to
 * format them.
 */
typedef enum
{
    /* Stats that are per process... */
    PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS,
    PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS,

    PVRSRV_PROCESS_STAT_TYPE_RC_OOMS,
    PVRSRV_PROCESS_STAT_TYPE_RC_PRS,
    PVRSRV_PROCESS_STAT_TYPE_RC_GROWS,
    PVRSRV_PROCESS_STAT_TYPE_RC_PUSH_GROWS,
    PVRSRV_PROCESS_STAT_TYPE_RC_TA_STORES,
    PVRSRV_PROCESS_STAT_TYPE_RC_3D_STORES,
    PVRSRV_PROCESS_STAT_TYPE_RC_SH_STORES,
    PVRSRV_PROCESS_STAT_TYPE_RC_CDM_STORES,
    PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_APP,
    PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_FW,
    PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_APP,
    PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_FW,
    PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT,
    PVRSRV_PROCESS_STAT_TYPE_FREELIST_MAX_PAGES,
    PVRSRV_PROCESS_STAT_TYPE_KMALLOC,
    PVRSRV_PROCESS_STAT_TYPE_MAX_KMALLOC,
    PVRSRV_PROCESS_STAT_TYPE_VMALLOC,
    PVRSRV_PROCESS_STAT_TYPE_MAX_VMALLOC,
    PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA,
    PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_PAGES_PT_UMA,
    PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA,
    PVRSRV_PROCESS_STAT_TYPE_MAX_VMAP_PT_UMA,
    PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA,
    PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_PAGES_PT_LMA,
    PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA,
    PVRSRV_PROCESS_STAT_TYPE_MAX_IOREMAP_PT_LMA,
    PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES,
    PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_LMA_PAGES,
    PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES,
    PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_UMA_PAGES,
    PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES,
    PVRSRV_PROCESS_STAT_TYPE_MAX_MAP_UMA_LMA_PAGES,

    //zxl: count total data
	PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC,
	PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_ALLOC,
	PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP,
	PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_MAP,

	/* Must be the last enum...*/
	PVRSRV_PROCESS_STAT_TYPE_COUNT
} PVRSRV_PROCESS_STAT_TYPE;


typedef enum
{
    PVRSRV_POWER_TIMING_STAT_FORCED_POWER_TRANSITION=0,
    PVRSRV_POWER_TIMING_STAT_PRE_DEVICE,
    PVRSRV_POWER_TIMING_STAT_PRE_SYSTEM,
    PVRSRV_POWER_TIMING_STAT_POST_DEVICE,
    PVRSRV_POWER_TIMING_STAT_POST_SYSTEM,
    PVRSRV_POWER_TIMING_STAT_NEWLINE1,
    PVRSRV_POWER_TIMING_STAT_NOT_FORCED_POWER_TRANSITION,
    PVRSRV_POWER_TIMING_STAT_NON_PRE_DEVICE,
    PVRSRV_POWER_TIMING_STAT_NON_PRE_SYSTEM,
    PVRSRV_POWER_TIMING_STAT_NON_POST_DEVICE,
    PVRSRV_POWER_TIMING_STAT_NON_POST_SYSTEM,
    PVRSRV_POWER_TIMING_STAT_NEWLINE2,
    PVRSRV_POWER_TIMING_STAT_FW_BOOTUP_TIME,
    PVRSRV_POWER_TIMING_STAT_HOST_ACK
} PVR_SRV_OTHER_STAT_TYPE;


static IMG_CHAR*  pszProcessStatFmt[PVRSRV_PROCESS_STAT_TYPE_COUNT] = {
	"Connections                       %10d\n", /* PVRSRV_STAT_TYPE_CONNECTIONS */
	"ConnectionsMax                    %10d\n", /* PVRSRV_STAT_TYPE_MAXCONNECTIONS */

    "RenderContextOutOfMemoryEvents    %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_OOMS */
    "RenderContextPartialRenders       %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_PRS */
    "RenderContextGrows                %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_GROWS */
    "RenderContextPushGrows            %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_PUSH_GROWS */
    "RenderContextTAStores             %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_TA_STORES */
    "RenderContext3DStores             %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_3D_STORES */
    "RenderContextSHStores             %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_SH_STORES */
    "RenderContextCDMStores            %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_RC_CDM_STORES */
    "ZSBufferRequestsByApp             %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_APP */
    "ZSBufferRequestsByFirmware        %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_FW */
    "FreeListGrowRequestsByApp         %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_APP */
    "FreeListGrowRequestsByFirmware    %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_FW */
    "FreeListInitialPages              %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT */
    "FreeListMaxPages                  %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_FREELIST_MAX_PAGES */
    "MemoryUsageKMalloc                %10d\n", /* PVRSRV_STAT_TYPE_KMALLOC */
    "MemoryUsageKMallocMax             %10d\n", /* PVRSRV_STAT_TYPE_MAX_KMALLOC */
    "MemoryUsageVMalloc                %10d\n", /* PVRSRV_STAT_TYPE_VMALLOC */
    "MemoryUsageVMallocMax             %10d\n", /* PVRSRV_STAT_TYPE_MAX_VMALLOC */
    "MemoryUsageAllocPTMemoryUMA       %10d\n", /* PVRSRV_STAT_TYPE_ALLOC_PAGES_PT_UMA */
    "MemoryUsageAllocPTMemoryUMAMax    %10d\n", /* PVRSRV_STAT_TYPE_MAX_ALLOC_PAGES_PT_UMA */
    "MemoryUsageVMapPTUMA              %10d\n", /* PVRSRV_STAT_TYPE_VMAP_PT_UMA */
    "MemoryUsageVMapPTUMAMax           %10d\n", /* PVRSRV_STAT_TYPE_MAX_VMAP_PT_UMA */
    "MemoryUsageAllocPTMemoryLMA       %10d\n", /* PVRSRV_STAT_TYPE_ALLOC_PAGES_PT_LMA */
    "MemoryUsageAllocPTMemoryLMAMax    %10d\n", /* PVRSRV_STAT_TYPE_MAX_ALLOC_PAGES_PT_LMA */
    "MemoryUsageIORemapPTLMA           %10d\n", /* PVRSRV_STAT_TYPE_IOREMAP_PT_LMA */
    "MemoryUsageIORemapPTLMAMax        %10d\n", /* PVRSRV_STAT_TYPE_MAX_IOREMAP_PT_LMA */
    "MemoryUsageAllocGPUMemLMA         %10d\n", /* PVRSRV_STAT_TYPE_ALLOC_LMA_PAGES */
    "MemoryUsageAllocGPUMemLMAMax      %10d\n", /* PVRSRV_STAT_TYPE_MAX_ALLOC_LMA_PAGES */
    "MemoryUsageAllocGPUMemUMA         %10d\n", /* PVRSRV_STAT_TYPE_ALLOC_UMA_PAGES */
    "MemoryUsageAllocGPUMemUMAMax      %10d\n", /* PVRSRV_STAT_TYPE_MAX_ALLOC_UMA_PAGES */
    "MemoryUsageMappedGPUMemUMA/LMA    %10d\n", /* PVRSRV_STAT_TYPE_MAP_UMA_LMA_PAGES */
    "MemoryUsageMappedGPUMemUMA/LMAMax %10d\n", /* PVRSRV_STAT_TYPE_MAX_MAP_UMA_LMA_PAGES */

    //zxl: count total data
    "MemoryUsageTotalAlloc             %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC */
    "MemoryUsageTotalAllocMax          %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_ALLOC */
    "MemoryUsageTotalMap               %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP */
    "MemoryUsageTotalMapMax            %10d\n", /* PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_MAP */
};


/*
 *  Macro for updating maximum stat values.
 */
#define UPDATE_MAX_VALUE(a,b)  do { if ((b) > (a)) {(a) = (b);} } while(0)

/*
 * Structures for holding statistics...
 */
typedef enum
{
	PVRSRV_STAT_STRUCTURE_PROCESS = 1,
	PVRSRV_STAT_STRUCTURE_RENDER_CONTEXT = 2,
	PVRSRV_STAT_STRUCTURE_MEMORY = 3,
	PVRSRV_STAT_STRUCTURE_RIMEMORY = 4,
	PVRSRV_STAT_STRUCTURE_POWER=5
} PVRSRV_STAT_STRUCTURE_TYPE;

#define MAX_PROC_NAME_LENGTH   (32)

typedef struct _PVRSRV_PROCESS_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE        eStructureType;

	/* Linked list pointers */
	struct _PVRSRV_PROCESS_STATS_*    psNext;
	struct _PVRSRV_PROCESS_STATS_*    psPrev;

	/* OS level process ID */
	IMG_PID                           pid;
	IMG_UINT32                        ui32RefCount;
	IMG_UINT32                        ui32MemRefCount;

	/* Folder name used to store the statistic */
	IMG_CHAR				          szFolderName[MAX_PROC_NAME_LENGTH];

	/* OS specific data */
	IMG_PVOID                         pvOSPidFolderData;
	IMG_PVOID                         pvOSPidEntryData;

	/* Stats... */
	IMG_INT32                         i32StatValue[PVRSRV_PROCESS_STAT_TYPE_COUNT];

	/* Other statistics structures */
	struct _PVRSRV_RENDER_STATS_*     psRenderLiveList;
	struct _PVRSRV_RENDER_STATS_*     psRenderDeadList;

	struct _PVRSRV_MEMORY_STATS_*     psMemoryStats;
	struct _PVRSRV_RI_MEMORY_STATS_*  psRIMemoryStats;

} PVRSRV_PROCESS_STATS;

typedef struct _PVRSRV_RENDER_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE     eStructureType;

	/* Linked list pointers */
	struct _PVRSRV_RENDER_STATS_*  psNext;
	struct _PVRSRV_RENDER_STATS_*  psPrev;

	/* OS specific data */
	IMG_PVOID                      pvOSData;

	/* Stats... */
	IMG_INT32                      i32StatValue[4];
} PVRSRV_RENDER_STATS;

typedef struct _PVRSRV_MEM_ALLOC_REC_
{
    PVRSRV_MEM_ALLOC_TYPE  eAllocType;
    IMG_UINT64			ui64Key;
    IMG_VOID               *pvCpuVAddr;
    IMG_CPU_PHYADDR        sCpuPAddr;
	IMG_SIZE_T			   uiBytes;
    IMG_PVOID              pvPrivateData;

    struct _PVRSRV_MEM_ALLOC_REC_  *psNext;
	struct _PVRSRV_MEM_ALLOC_REC_  **ppsThis;
} PVRSRV_MEM_ALLOC_REC;

typedef struct _PVRSRV_MEMORY_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE  eStructureType;

	/* OS specific data */
	IMG_PVOID                   pvOSMemEntryData;

    /* Cached position in the linked list when obtaining all items... */
    IMG_UINT32                  ui32LastStatNumberRequested;
	PVRSRV_MEM_ALLOC_REC        *psLastStatMemoryRecordFound;

	/* Stats... */
	PVRSRV_MEM_ALLOC_REC        *psMemoryRecords;
} PVRSRV_MEMORY_STATS;

typedef struct _PVRSRV_RI_MEMORY_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE  eStructureType;

	/* OS level process ID */
	IMG_PID                   	pid;
	
	/* Handle used when querying the RI server... */
	IMG_HANDLE                 *pRIHandle;

	/* OS specific data */
	IMG_PVOID                   pvOSRIMemEntryData;
} PVRSRV_RI_MEMORY_STATS;

typedef struct _PVRSRV_GLOBAL_MEMORY_STATS_
{
	IMG_UINT32 ui32MemoryUsageKMalloc;
	IMG_UINT32 ui32MemoryUsageKMallocMax;
	IMG_UINT32 ui32MemoryUsageVMalloc;
	IMG_UINT32 ui32MemoryUsageVMallocMax;
	IMG_UINT32 ui32MemoryUsageAllocPTMemoryUMA;
	IMG_UINT32 ui32MemoryUsageAllocPTMemoryUMAMax;
	IMG_UINT32 ui32MemoryUsageVMapPTUMA;
	IMG_UINT32 ui32MemoryUsageVMapPTUMAMax;
	IMG_UINT32 ui32MemoryUsageAllocPTMemoryLMA;
	IMG_UINT32 ui32MemoryUsageAllocPTMemoryLMAMax;
	IMG_UINT32 ui32MemoryUsageIORemapPTLMA;
	IMG_UINT32 ui32MemoryUsageIORemapPTLMAMax;
	IMG_UINT32 ui32MemoryUsageAllocGPUMemLMA;
	IMG_UINT32 ui32MemoryUsageAllocGPUMemLMAMax;
	IMG_UINT32 ui32MemoryUsageAllocGPUMemUMA;
	IMG_UINT32 ui32MemoryUsageAllocGPUMemUMAMax;
	IMG_UINT32 ui32MemoryUsageMappedGPUMemUMA_LMA;
	IMG_UINT32 ui32MemoryUsageMappedGPUMemUMA_LMAMax;

	//zxl: count total data
	IMG_UINT32 ui32MemoryUsageTotalAlloc;
	IMG_UINT32 ui32MemoryUsageTotalAllocMax;
	IMG_UINT32 ui32MemoryUsageTotalMap;
    IMG_UINT32 ui32MemoryUsageTotalMapMax;
} PVRSRV_GLOBAL_MEMORY_STATS;

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
static IMPLEMENT_LIST_INSERT(PVRSRV_MEM_ALLOC_REC)
static IMPLEMENT_LIST_REMOVE(PVRSRV_MEM_ALLOC_REC)
#endif


/*
 *  Global Boolean to flag when the statistics are ready to monitor
 *  memory allocations.
 */
static  IMG_BOOL  bProcessStatsInitialised = IMG_FALSE;

/*
 * Linked lists for process stats. Live stats are for processes which are still running
 * and the dead list holds those that have exited.
 */
static PVRSRV_PROCESS_STATS*  psLiveList = IMG_NULL;
static PVRSRV_PROCESS_STATS*  psDeadList = IMG_NULL;

POS_LOCK  psLinkedListLock = IMG_NULL;


/*
 * Pointer to OS folder to hold PID folders.
 */
IMG_CHAR*  pszOSLivePidFolderName = "pid";
IMG_CHAR*  pszOSDeadPidFolderName = "pids_retired";
IMG_PVOID  pvOSLivePidFolder      = IMG_NULL;
IMG_PVOID  pvOSDeadPidFolder      = IMG_NULL;

/*Power Statistics List */

static IMG_UINT64 ui64TotalForcedEntries=0,ui64TotalNotForcedEntries=0;

static IMG_UINT64 ui64ForcedPreDevice=0, ui64ForcedPreSystem=0, ui64ForcedPostDevice=0, ui64ForcedPostSystem=0;
static IMG_UINT64 ui64NotForcedPreDevice=0, ui64NotForcedPreSystem=0, ui64NotForcedPostDevice=0, ui64NotForcedPostSystem=0;

/* global driver-data folders */
static IMG_PVOID  pvOSGlobalMemEntryRef = IMG_NULL;
static IMG_CHAR* const pszDriverStatFilename = "driver_stats";
static PVRSRV_GLOBAL_MEMORY_STATS gsGlobalStats;

#define HASH_INITIAL_SIZE 5
static HASH_TABLE* gpsTrackingTable;

static IMG_UINT32 _PVRSRVIncrMemStatRefCount(IMG_PVOID pvStatPtr);
static IMG_UINT32 _PVRSRVDecrMemStatRefCount(IMG_PVOID pvStatPtr);

static IMG_BOOL
_PVRSRVGetGlobalMemStat(IMG_PVOID pvStatPtr, 
						IMG_UINT32 ui32StatNumber, 
						IMG_INT32* pi32StatData, 
						IMG_CHAR** ppszStatFmtText);

IMG_VOID InsertPowerTimeStatistic(PVRSRV_POWER_ENTRY_TYPE bType,
		IMG_INT32 i32CurrentState, IMG_INT32 i32NextState,
        IMG_UINT64 ui64SysStartTime, IMG_UINT64 ui64SysEndTime,
		IMG_UINT64 ui64DevStartTime, IMG_UINT64 ui64DevEndTime,
		IMG_BOOL bForced)
{
    IMG_UINT64 ui64Device;
    IMG_UINT64 ui64System;

	if (i32CurrentState==i32NextState) return ;

    ui64Device=ui64DevEndTime-ui64DevStartTime;
    ui64System=ui64SysEndTime-ui64SysStartTime;

    if (bForced)
    {
        ui64TotalForcedEntries++;
        if (bType==PVRSRV_POWER_ENTRY_TYPE_POST)
        {
            ui64ForcedPostDevice+=ui64Device;
            ui64ForcedPostSystem+=ui64System;
        }
        else
        {
            ui64ForcedPreDevice+=ui64Device;
            ui64ForcedPreSystem+=ui64System;
        }
    }
    else
    {
        ui64TotalNotForcedEntries++;
        if (bType==PVRSRV_POWER_ENTRY_TYPE_POST)
        {
            ui64NotForcedPostDevice+=ui64Device;
            ui64NotForcedPostSystem+=ui64System;
        }
        else
        {
            ui64NotForcedPreDevice+=ui64Device;
            ui64NotForcedPreSystem+=ui64System;
        }
    }

	return;
}

/*************************************************************************/ /*!
@Function       _RemoveRenderStatsFromList
@Description    Detaches a process from either the live or dead list.
@Input          psProcessStats  Process to remove the stats from.
@Input          psRenderStats   Render stats to remove.
*/ /**************************************************************************/
static IMG_VOID
_RemoveRenderStatsFromList(PVRSRV_PROCESS_STATS* psProcessStats,
                           PVRSRV_RENDER_STATS* psRenderStats)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);
	PVR_ASSERT(psRenderStats != IMG_NULL);

	/* Remove the item from the linked lists... */
	if (psProcessStats->psRenderLiveList == psRenderStats)
	{
		psProcessStats->psRenderLiveList = psRenderStats->psNext;

		if (psProcessStats->psRenderLiveList != IMG_NULL)
		{
			psProcessStats->psRenderLiveList->psPrev = IMG_NULL;
		}
	}
	else if (psProcessStats->psRenderDeadList == psRenderStats)
	{
		psProcessStats->psRenderDeadList = psRenderStats->psNext;

		if (psProcessStats->psRenderDeadList != IMG_NULL)
		{
			psProcessStats->psRenderDeadList->psPrev = IMG_NULL;
		}
	}
	else
	{
		PVRSRV_RENDER_STATS*  psNext = psRenderStats->psNext;
		PVRSRV_RENDER_STATS*  psPrev = psRenderStats->psPrev;

		if (psRenderStats->psNext != IMG_NULL)
		{
			psRenderStats->psNext->psPrev = psPrev;
		}
		if (psRenderStats->psPrev != IMG_NULL)
		{
			psRenderStats->psPrev->psNext = psNext;
		}
	}

	/* Reset the pointers in this cell, as it is not attached to anything */
	psRenderStats->psNext = IMG_NULL;
	psRenderStats->psPrev = IMG_NULL;
} /* _RemoveRenderStatsFromList */


/*************************************************************************/ /*!
@Function       _DestoryRenderStat
@Description    Frees memory and resources held by a render statistic.
@Input          psRenderStats  Render stats to destroy.
*/ /**************************************************************************/
static IMG_VOID
_DestoryRenderStat(PVRSRV_RENDER_STATS* psRenderStats)
{
	PVR_ASSERT(psRenderStats != IMG_NULL);

	/* Free the memory... */
	OSFreeMem(psRenderStats);
} /* _DestoryRenderStat */


/*************************************************************************/ /*!
@Function       _FindProcessStatsInLiveList
@Description    Searches the Live Process List for a statistics structure that
                matches the PID given.
@Input          pid  Process to search for.
@Return         Pointer to stats structure for the process.
*/ /**************************************************************************/
static PVRSRV_PROCESS_STATS*
_FindProcessStatsInLiveList(IMG_PID pid)
{
	PVRSRV_PROCESS_STATS*  psProcessStats = psLiveList;

	while (psProcessStats != IMG_NULL)
	{
		if (psProcessStats->pid == pid)
		{
			return psProcessStats;
		}

		psProcessStats = psProcessStats->psNext;
	}

	return IMG_NULL;
} /* _FindProcessStatsInLiveList */


/*************************************************************************/ /*!
@Function       _FindProcessStatsInDeadList
@Description    Searches the Dead Process List for a statistics structure that
                matches the PID given.
@Input          pid  Process to search for.
@Return         Pointer to stats structure for the process.
*/ /**************************************************************************/
static PVRSRV_PROCESS_STATS*
_FindProcessStatsInDeadList(IMG_PID pid)
{
	PVRSRV_PROCESS_STATS*  psProcessStats = psDeadList;

	while (psProcessStats != IMG_NULL)
	{
		if (psProcessStats->pid == pid)
		{
			return psProcessStats;
		}

		psProcessStats = psProcessStats->psNext;
	}

	return IMG_NULL;
} /* _FindProcessStatsInDeadList */


/*************************************************************************/ /*!
@Function       _FindProcessStats
@Description    Searches the Live and Dead Process Lists for a statistics
                structure that matches the PID given.
@Input          pid  Process to search for.
@Return         Pointer to stats structure for the process.
*/ /**************************************************************************/
static PVRSRV_PROCESS_STATS*
_FindProcessStats(IMG_PID pid)
{
	PVRSRV_PROCESS_STATS*  psProcessStats = _FindProcessStatsInLiveList(pid);

	if (psProcessStats == IMG_NULL)
	{
		psProcessStats = _FindProcessStatsInDeadList(pid);
	}

	return psProcessStats;
} /* _FindProcessStats */


/*************************************************************************/ /*!
@Function       _AddProcessStatsToFrontOfLiveList
@Description    Add a statistic to the live list head.
@Input          psProcessStats  Process stats to add.
*/ /**************************************************************************/
static IMG_VOID
_AddProcessStatsToFrontOfLiveList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);

	if (psLiveList != IMG_NULL)
	{
		psLiveList->psPrev     = psProcessStats;
		psProcessStats->psNext = psLiveList;
	}

	psLiveList = psProcessStats;
} /* _AddProcessStatsToFrontOfLiveList */


/*************************************************************************/ /*!
@Function       _AddProcessStatsToFrontOfDeadList
@Description    Add a statistic to the dead list head.
@Input          psProcessStats  Process stats to add.
*/ /**************************************************************************/
static IMG_VOID
_AddProcessStatsToFrontOfDeadList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);

	if (psDeadList != IMG_NULL)
	{
		psDeadList->psPrev     = psProcessStats;
		psProcessStats->psNext = psDeadList;
	}

	psDeadList = psProcessStats;
} /* _AddProcessStatsToFrontOfDeadList */


/*************************************************************************/ /*!
@Function       _RemoveProcessStatsFromList
@Description    Detaches a process from either the live or dead list.
@Input          psProcessStats  Process stats to remove.
*/ /**************************************************************************/
static IMG_VOID
_RemoveProcessStatsFromList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);

	/* Remove the item from the linked lists... */
	if (psLiveList == psProcessStats)
	{
		psLiveList = psProcessStats->psNext;

		if (psLiveList != IMG_NULL)
		{
			psLiveList->psPrev = IMG_NULL;
		}
	}
	else if (psDeadList == psProcessStats)
	{
		psDeadList = psProcessStats->psNext;

		if (psDeadList != IMG_NULL)
		{
			psDeadList->psPrev = IMG_NULL;
		}
	}
	else
	{
		PVRSRV_PROCESS_STATS*  psNext = psProcessStats->psNext;
		PVRSRV_PROCESS_STATS*  psPrev = psProcessStats->psPrev;

		if (psProcessStats->psNext != IMG_NULL)
		{
			psProcessStats->psNext->psPrev = psPrev;
		}
		if (psProcessStats->psPrev != IMG_NULL)
		{
			psProcessStats->psPrev->psNext = psNext;
		}
	}

	/* Reset the pointers in this cell, as it is not attached to anything */
	psProcessStats->psNext = IMG_NULL;
	psProcessStats->psPrev = IMG_NULL;
} /* _RemoveProcessStatsFromList */


/*************************************************************************/ /*!
@Function       _CreateOSStatisticEntries
@Description    Create all OS entries for this statistic.
@Input          psProcessStats  Process stats to destroy.
@Input          pvOSPidFolder   Pointer to OS folder to place the entrys in.
*/ /**************************************************************************/
static IMG_VOID
_CreateOSStatisticEntries(PVRSRV_PROCESS_STATS* psProcessStats, 
                          IMG_PVOID pvOSPidFolder)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);

	psProcessStats->pvOSPidFolderData = OSCreateStatisticFolder(psProcessStats->szFolderName, pvOSPidFolder);
	psProcessStats->pvOSPidEntryData  = OSCreateStatisticEntry("process_stats",
	                                                           psProcessStats->pvOSPidFolderData,
	                                                           PVRSRVStatsObtainElement,
															   _PVRSRVIncrMemStatRefCount,
															   _PVRSRVDecrMemStatRefCount,
	                                                           (IMG_PVOID) psProcessStats);
	if (pvOSPowerStatsEntryData==NULL)
	{
		pvOSPowerStatsEntryData  = OSCreateStatisticEntry("power_timings_stats",
													NULL,
													PVRSRVPowerStatsObtainElement,
													IMG_NULL,
													IMG_NULL,
													(IMG_PVOID) psProcessStats);
	}

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	psProcessStats->psMemoryStats->pvOSMemEntryData = OSCreateStatisticEntry("mem_area",
	                                                           psProcessStats->pvOSPidFolderData,
	                                                           PVRSRVStatsObtainElement,
															   IMG_NULL,
															   IMG_NULL,
															   (IMG_PVOID) psProcessStats->psMemoryStats);
#endif

#if defined(PVR_RI_DEBUG)
	psProcessStats->psRIMemoryStats->pvOSRIMemEntryData = OSCreateStatisticEntry("ri_mem_area",
	                                                           psProcessStats->pvOSPidFolderData,
	                                                           PVRSRVStatsObtainElement,
															   IMG_NULL,
															   IMG_NULL,
															   (IMG_PVOID) psProcessStats->psRIMemoryStats);
#endif
} /* _CreateOSStatisticEntries */


/*************************************************************************/ /*!
@Function       _RemoveOSStatisticEntries
@Description    Removed all OS entries used by this statistic.
@Input          psProcessStats  Process stats to destroy.
*/ /**************************************************************************/
static IMG_VOID
_RemoveOSStatisticEntries(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);

#if defined(PVR_RI_DEBUG)
	PVR_ASSERT(psProcessStats->psRIMemoryStats->pvOSRIMemEntryData != IMG_NULL);
	OSRemoveStatisticEntry(psProcessStats->psRIMemoryStats->pvOSRIMemEntryData);
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVR_ASSERT(psProcessStats->psMemoryStats->pvOSMemEntryData != IMG_NULL);
	OSRemoveStatisticEntry(psProcessStats->psMemoryStats->pvOSMemEntryData);
#endif

	PVR_ASSERT(psProcessStats->pvOSPidEntryData != IMG_NULL);
	OSRemoveStatisticEntry(psProcessStats->pvOSPidEntryData);
	PVR_ASSERT(psProcessStats->pvOSPidFolderData != IMG_NULL);
	OSRemoveStatisticFolder(psProcessStats->pvOSPidFolderData);
} /* _RemoveOSStatisticEntries */


/*************************************************************************/ /*!
@Function       _DestoryProcessStat
@Description    Frees memory and resources held by a process statistic.
@Input          psProcessStats  Process stats to destroy.
*/ /**************************************************************************/
static IMG_VOID
_DestoryProcessStat(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);

	/* Free the live and dead render statistic lists... */
	while (psProcessStats->psRenderLiveList != IMG_NULL)
	{
		PVRSRV_RENDER_STATS*  psRenderStats = psProcessStats->psRenderLiveList;

		_RemoveRenderStatsFromList(psProcessStats, psRenderStats);
		_DestoryRenderStat(psRenderStats);
	}

	while (psProcessStats->psRenderDeadList != IMG_NULL)
	{
		PVRSRV_RENDER_STATS*  psRenderStats = psProcessStats->psRenderDeadList;

		_RemoveRenderStatsFromList(psProcessStats, psRenderStats);
		_DestoryRenderStat(psRenderStats);
	}

	/* Free the memory statistics... */
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	if (psProcessStats->psMemoryStats != IMG_NULL)
	{
		while (psProcessStats->psMemoryStats->psMemoryRecords)
		{
			List_PVRSRV_MEM_ALLOC_REC_Remove(psProcessStats->psMemoryStats->psMemoryRecords);
		}
		OSMemSet(psProcessStats->psMemoryStats, 0x6b, sizeof(PVRSRV_MEMORY_STATS));
		OSFreeMem(psProcessStats->psMemoryStats);
		psProcessStats->psMemoryStats = IMG_NULL;
	}
#endif

#if defined(PVR_RI_DEBUG)
	if (psProcessStats->psRIMemoryStats != IMG_NULL)
	{
		OSFreeMem(psProcessStats->psRIMemoryStats);
		psProcessStats->psRIMemoryStats = IMG_NULL;
	}
#endif

	/* Free the memory... */
	OSFreeMem(psProcessStats);
} /* _DestoryProcessStat */

static IMG_UINT32 _PVRSRVIncrMemStatRefCount(IMG_PVOID pvStatPtr)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) pvStatPtr;
	IMG_UINT32 ui32Res = 7777;

	switch (*peStructureType)
	{
		case PVRSRV_STAT_STRUCTURE_PROCESS:
		{
			/* Increment stat memory refCount */
			ui32Res = ++psProcessStats->ui32MemRefCount;
			break;
		}
		default:
		{
			break;
		}
	}
	return ui32Res;
}

static IMG_UINT32 _PVRSRVDecrMemStatRefCount(IMG_PVOID pvStatPtr)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) pvStatPtr;
	IMG_UINT32 ui32Res = 7777;

	switch (*peStructureType)
	{
		case PVRSRV_STAT_STRUCTURE_PROCESS:
		{
			/* Decrement stat memory refCount and free if now zero */
			ui32Res = --psProcessStats->ui32MemRefCount;
			if (ui32Res == 0)
			{
				_DestoryProcessStat(psProcessStats);
			}
			break;
		}
		default:
		{
			break;
		}
	}
	return ui32Res;
}

/*************************************************************************/ /*!
@Function       _CompressMemoryUsage
@Description    Reduces memory usage by deleting old statistics data.
                This function requires that the list lock is not held!
*/ /**************************************************************************/
static IMG_VOID
_CompressMemoryUsage(IMG_VOID)
{
	PVRSRV_PROCESS_STATS*  psProcessStats;
	PVRSRV_PROCESS_STATS*  psProcessStatsToBeFreed;
	IMG_UINT32  ui32ItemsRemaining;

	/*
	 *  We hold the lock whilst checking the list, but we'll release it
	 *  before freeing memory (as that will require the lock too)!
	 */
    OSLockAcquire(psLinkedListLock);

	/* Check that the dead list is not bigger than the max size... */
	psProcessStats          = psDeadList;
	psProcessStatsToBeFreed = IMG_NULL;
	ui32ItemsRemaining      = MAX_DEAD_LIST_PROCESSES;

	while (psProcessStats != IMG_NULL  &&  ui32ItemsRemaining > 0)
    {
		ui32ItemsRemaining--;
		if (ui32ItemsRemaining == 0)
		{
			/* This is the last allowed process, cut the linked list here! */
			psProcessStatsToBeFreed = psProcessStats->psNext;
			psProcessStats->psNext  = IMG_NULL;
		}
		else
		{
			psProcessStats = psProcessStats->psNext;
		}
	}

	OSLockRelease(psLinkedListLock);

	/* Any processes stats remaining will need to be destroyed... */
	while (psProcessStatsToBeFreed != IMG_NULL)
    {
		PVRSRV_PROCESS_STATS*  psNextProcessStats = psProcessStatsToBeFreed->psNext;

		psProcessStatsToBeFreed->psNext = IMG_NULL;
		_RemoveOSStatisticEntries(psProcessStatsToBeFreed);
		_PVRSRVDecrMemStatRefCount((void*)psProcessStatsToBeFreed);

		psProcessStatsToBeFreed = psNextProcessStats;
	}
} /* _CompressMemoryUsage */


/* These functions move the process stats from the living to the dead list.
 * _MoveProcessToDeadList moves the entry in the global lists and
 * it needs to be protected by psLinkedListLock.
 * _MoveProcessToDeadListDebugFS performs the OS calls and it
 * shouldn't be used under psLinkedListLock because this could generate a
 * lockdep warning. */
static IMG_VOID
_MoveProcessToDeadList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Take the element out of the live list and append to the dead list... */
	_RemoveProcessStatsFromList(psProcessStats);
	_AddProcessStatsToFrontOfDeadList(psProcessStats);
} /* _MoveProcessToDeadList */

static IMG_VOID
_MoveProcessToDeadListDebugFS(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Transfer the OS entries to the folder for dead processes... */
	_RemoveOSStatisticEntries(psProcessStats);
	_CreateOSStatisticEntries(psProcessStats, pvOSDeadPidFolder);
} /* _MoveProcessToDeadListDebugFS */


/*************************************************************************/ /*!
@Function       PVRSRVStatsInitialise
@Description    Entry point for initialising the statistics module.
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVStatsInitialise(IMG_VOID)
{
    PVRSRV_ERROR error;

    PVR_ASSERT(psLiveList == IMG_NULL);
    PVR_ASSERT(psDeadList == IMG_NULL);
    PVR_ASSERT(psLinkedListLock == IMG_NULL);
    PVR_ASSERT(gpsTrackingTable == IMG_NULL);
	PVR_ASSERT(bProcessStatsInitialised == IMG_FALSE);

	/* We need a lock to protect the linked lists... */
    error = OSLockCreate(&psLinkedListLock, LOCK_TYPE_NONE);
    
    /* Create a pid folders for putting the PID files in... */
    pvOSLivePidFolder = OSCreateStatisticFolder(pszOSLivePidFolderName, IMG_NULL);
    pvOSDeadPidFolder = OSCreateStatisticFolder(pszOSDeadPidFolderName, IMG_NULL);

	/* for global memory stat */
	pvOSGlobalMemEntryRef = OSCreateStatisticEntry(pszDriverStatFilename,
												   IMG_NULL,
												   _PVRSRVGetGlobalMemStat,
												   IMG_NULL,
												   IMG_NULL,
												   IMG_NULL);

	OSMemSet(&gsGlobalStats, 0, sizeof(gsGlobalStats));

	gpsTrackingTable = HASH_Create(HASH_INITIAL_SIZE);
												   
	/* Flag that we are ready to start monitoring memory allocations. */
	bProcessStatsInitialised = IMG_TRUE;
	
	return error;
} /* PVRSRVStatsInitialise */


/*************************************************************************/ /*!
@Function       PVRSRVStatsDestroy
@Description    Method for destroying the statistics module data.
*/ /**************************************************************************/
IMG_VOID
PVRSRVStatsDestroy(IMG_VOID)
{
	PVR_ASSERT(bProcessStatsInitialised == IMG_TRUE);

	if (pvOSPowerStatsEntryData!=NULL)
	{
		OSRemoveStatisticEntry(pvOSPowerStatsEntryData);
		pvOSPowerStatsEntryData=NULL;
	}

	if (pvOSGlobalMemEntryRef != NULL)
	{
		OSRemoveStatisticEntry(pvOSGlobalMemEntryRef);
		pvOSGlobalMemEntryRef = IMG_NULL;
	}
	
	/* Stop monitoring memory allocations... */
	bProcessStatsInitialised = IMG_FALSE;

	/* Destroy the lock... */
	if (psLinkedListLock != IMG_NULL)
	{
		OSLockDestroy(psLinkedListLock);
		psLinkedListLock = IMG_NULL;
	}

	/* Free the live and dead lists... */
	while (psLiveList != IMG_NULL)
    {
		PVRSRV_PROCESS_STATS*  psProcessStats = psLiveList;

		_RemoveProcessStatsFromList(psProcessStats);
		_RemoveOSStatisticEntries(psProcessStats);
	}
	while (psDeadList != IMG_NULL)
    {
		PVRSRV_PROCESS_STATS*  psProcessStats = psDeadList;

		_RemoveProcessStatsFromList(psProcessStats);

		_RemoveOSStatisticEntries(psProcessStats);
	}

	/* Remove the OS folders used by the PID folders... */
    OSRemoveStatisticFolder(pvOSLivePidFolder);
    pvOSLivePidFolder = IMG_NULL;
    OSRemoveStatisticFolder(pvOSDeadPidFolder);
    pvOSDeadPidFolder = IMG_NULL;

	if (gpsTrackingTable != IMG_NULL)
	{
		HASH_Delete(gpsTrackingTable);
	}

} /* PVRSRVStatsDestroy */


static void _decrease_global_stat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								  IMG_SIZE_T uiBytes)
{
	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			if (gsGlobalStats.ui32MemoryUsageKMalloc > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageKMalloc -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageKMalloc = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			if (gsGlobalStats.ui32MemoryUsageVMalloc > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageVMalloc -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageVMalloc = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			if (gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			if (gsGlobalStats.ui32MemoryUsageVMapPTUMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageVMapPTUMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageVMapPTUMA = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			if (gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			if (gsGlobalStats.ui32MemoryUsageIORemapPTLMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageIORemapPTLMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageIORemapPTLMA = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			if (gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			if (gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA = 0;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			if (gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA > uiBytes)
			{
				gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA -= uiBytes;
			}
			else
			{
				gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA = 0;
			}
			break;

		default:
			PVR_ASSERT(0);
			break;
	}
	//Count total data
    gsGlobalStats.ui32MemoryUsageTotalAlloc = gsGlobalStats.ui32MemoryUsageKMalloc + gsGlobalStats.ui32MemoryUsageVMalloc +\
        gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA + gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA +\
        gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA + gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA;

    gsGlobalStats.ui32MemoryUsageTotalMap = gsGlobalStats.ui32MemoryUsageVMapPTUMA + gsGlobalStats.ui32MemoryUsageIORemapPTLMA +\
        gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA;
}


static void _increase_global_stat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								  IMG_SIZE_T uiBytes)
{
	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			gsGlobalStats.ui32MemoryUsageKMalloc += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageKMallocMax < gsGlobalStats.ui32MemoryUsageKMalloc)
			{
				gsGlobalStats.ui32MemoryUsageKMallocMax = gsGlobalStats.ui32MemoryUsageKMalloc;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			gsGlobalStats.ui32MemoryUsageVMalloc += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageVMallocMax < gsGlobalStats.ui32MemoryUsageVMalloc)
			{
				gsGlobalStats.ui32MemoryUsageVMallocMax = gsGlobalStats.ui32MemoryUsageVMalloc;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMAMax < gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA)
			{
				gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMAMax = gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			gsGlobalStats.ui32MemoryUsageVMapPTUMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageVMapPTUMAMax < gsGlobalStats.ui32MemoryUsageVMapPTUMA)
			{
				gsGlobalStats.ui32MemoryUsageVMapPTUMAMax = gsGlobalStats.ui32MemoryUsageVMapPTUMA;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMAMax < gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA)
			{
				gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMAMax = gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			gsGlobalStats.ui32MemoryUsageIORemapPTLMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageIORemapPTLMAMax < gsGlobalStats.ui32MemoryUsageIORemapPTLMA)
			{
				gsGlobalStats.ui32MemoryUsageIORemapPTLMAMax = gsGlobalStats.ui32MemoryUsageIORemapPTLMA;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageAllocGPUMemLMAMax < gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA)
			{
				gsGlobalStats.ui32MemoryUsageAllocGPUMemLMAMax = gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAMax < gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA)
			{
				gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAMax = gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA;
			}
			break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA += uiBytes;
			if (gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMAMax < gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA)
			{
				gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMAMax = gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA;
			}
			break;

		default:
			PVR_ASSERT(0);
			break;
	}
	//Count total data
	gsGlobalStats.ui32MemoryUsageTotalAlloc = gsGlobalStats.ui32MemoryUsageKMalloc + gsGlobalStats.ui32MemoryUsageVMalloc +\
        gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA + gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA +\
        gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA + gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA;
    UPDATE_MAX_VALUE(gsGlobalStats.ui32MemoryUsageTotalAllocMax,gsGlobalStats.ui32MemoryUsageTotalAlloc);

    gsGlobalStats.ui32MemoryUsageTotalMap = gsGlobalStats.ui32MemoryUsageVMapPTUMA + gsGlobalStats.ui32MemoryUsageIORemapPTLMA +\
        gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA;
    UPDATE_MAX_VALUE(gsGlobalStats.ui32MemoryUsageTotalMapMax,gsGlobalStats.ui32MemoryUsageTotalMap);
}


/*************************************************************************/ /*!
@Function       PVRSRVStatsRegisterProcess
@Description    Register a process into the list statistics list.
@Output         phProcessStats  Handle to the process to be used to deregister.
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVStatsRegisterProcess(IMG_HANDLE* phProcessStats)
{
    PVRSRV_PROCESS_STATS*  psProcessStats;
    IMG_PID                currentPid = OSGetCurrentProcessIDKM();
    IMG_BOOL               bMoveProcess = IMG_FALSE;

    PVR_ASSERT(phProcessStats != IMG_NULL);

    /* Check the PID has not already moved to the dead list... */
	OSLockAcquire(psLinkedListLock);
	psProcessStats = _FindProcessStatsInDeadList(currentPid);
    if (psProcessStats != IMG_NULL)
    {
		/* Move it back onto the live list! */
		_RemoveProcessStatsFromList(psProcessStats);
		_AddProcessStatsToFrontOfLiveList(psProcessStats);
		
		/* we can perform the OS operation out of lock */
		bMoveProcess = IMG_TRUE;
	}
	else
	{
		/* Check the PID is not already registered in the live list... */
		psProcessStats = _FindProcessStatsInLiveList(currentPid);
	}

	/* If the PID is on the live list then just increment the ref count and return... */
    if (psProcessStats != IMG_NULL)
    {
		psProcessStats->ui32RefCount++;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS] = psProcessStats->ui32RefCount;
		UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS],
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]);
		OSLockRelease(psLinkedListLock);

		*phProcessStats = psProcessStats;

		/* Check if we need to perform any OS operation */
		if (bMoveProcess)
		{
			/* Transfer the OS entries back to the folder for live processes... */
			_RemoveOSStatisticEntries(psProcessStats);
			_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);
		}

		return PVRSRV_OK;
	}
	OSLockRelease(psLinkedListLock);

	/* Allocate a new node structure and initialise it... */
	psProcessStats = OSAllocMem(sizeof(PVRSRV_PROCESS_STATS));
	if (psProcessStats == IMG_NULL)
	{
		*phProcessStats = 0;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSMemSet(psProcessStats, 0, sizeof(PVRSRV_PROCESS_STATS));

	psProcessStats->eStructureType  = PVRSRV_STAT_STRUCTURE_PROCESS;
	psProcessStats->pid             = currentPid;
	psProcessStats->ui32RefCount    = 1;
	psProcessStats->ui32MemRefCount = 1;

	psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]     = 1;
	psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS] = 1;

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	psProcessStats->psMemoryStats = OSAllocMem(sizeof(PVRSRV_MEMORY_STATS));
	if (psProcessStats->psMemoryStats == IMG_NULL)
	{
		OSFreeMem(psProcessStats);
		*phProcessStats = 0;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSMemSet(psProcessStats->psMemoryStats, 0, sizeof(PVRSRV_MEMORY_STATS));
	psProcessStats->psMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_MEMORY;
#endif

#if defined(PVR_RI_DEBUG)
	psProcessStats->psRIMemoryStats = OSAllocMem(sizeof(PVRSRV_RI_MEMORY_STATS));
	if (psProcessStats->psRIMemoryStats == IMG_NULL)
	{
		OSFreeMem(psProcessStats->psMemoryStats);
		OSFreeMem(psProcessStats);
		*phProcessStats = 0;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSMemSet(psProcessStats->psRIMemoryStats, 0, sizeof(PVRSRV_RI_MEMORY_STATS));
	psProcessStats->psRIMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_RIMEMORY;
	psProcessStats->psRIMemoryStats->pid            = currentPid;
#endif

	/* Add it to the live list... */
    OSLockAcquire(psLinkedListLock);
	_AddProcessStatsToFrontOfLiveList(psProcessStats);
	OSLockRelease(psLinkedListLock);

	/* Create the process stat in the OS... */
	OSSNPrintf(psProcessStats->szFolderName, sizeof(psProcessStats->szFolderName),
	           "%d", currentPid);
	_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);

	/* Done */
	*phProcessStats = (IMG_HANDLE) psProcessStats;

	return PVRSRV_OK;
} /* PVRSRVStatsRegisterProcess */


/*************************************************************************/ /*!
@Function       PVRSRVStatsDeregisterProcess
@Input          hProcessStats  Handle to the process returned when registered.
@Description    Method for destroying the statistics module data.
*/ /**************************************************************************/
IMG_VOID
PVRSRVStatsDeregisterProcess(IMG_HANDLE hProcessStats)
{
	IMG_BOOL    bMoveProcess = IMG_FALSE;

	if (hProcessStats != 0)
	{
		PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) hProcessStats;

		/* Lower the reference count, if zero then move it to the dead list */
		OSLockAcquire(psLinkedListLock);
		if (psProcessStats->ui32RefCount > 0)
		{
			psProcessStats->ui32RefCount--;
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS] = psProcessStats->ui32RefCount;

			if (psProcessStats->ui32RefCount == 0)
			{
				_MoveProcessToDeadList(psProcessStats);
				bMoveProcess = IMG_TRUE;
			}
		}
		OSLockRelease(psLinkedListLock);

		/* The OS calls need to be performed without psLinkedListLock */
		if (bMoveProcess == IMG_TRUE)
		{
			_MoveProcessToDeadListDebugFS(psProcessStats);
		}

		/* Check if the dead list needs to be reduced */
		_CompressMemoryUsage();
	}
} /* PVRSRVStatsDeregisterProcess */


IMG_VOID
PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
                             IMG_VOID *pvCpuVAddr,
                             IMG_CPU_PHYADDR sCpuPAddr,
                             IMG_SIZE_T uiBytes,
                             IMG_PVOID pvPrivateData)
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID                currentPid = OSGetCurrentProcessIDKM();
    PVRSRV_MEM_ALLOC_REC*  psRecord   = IMG_NULL;
    PVRSRV_PROCESS_STATS*  psProcessStats;
    PVRSRV_MEMORY_STATS*   psMemoryStats;

    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	/*
	 *  To prevent a recursive loop, we make the memory allocations
	 *  for our memstat records via OSAllocMemstatMem(), which does not try to
	 *  create a memstat record entry..
	 */

    /* Allocate the memory record... */
	psRecord = OSAllocMemstatMem(sizeof(PVRSRV_MEM_ALLOC_REC));
	if (psRecord == IMG_NULL)
	{
		return;
	}

	OSMemSet(psRecord, 0, sizeof(PVRSRV_MEM_ALLOC_REC));
	psRecord->eAllocType       = eAllocType;
	psRecord->pvCpuVAddr       = pvCpuVAddr;
	psRecord->sCpuPAddr.uiAddr = sCpuPAddr.uiAddr;
	psRecord->uiBytes          = uiBytes;
	psRecord->pvPrivateData    = pvPrivateData;

	/* Lock while we find the correct process... */
	OSLockAcquire(psLinkedListLock);

    psProcessStats = _FindProcessStats(currentPid);
    if (psProcessStats == IMG_NULL)
    {
		OSLockRelease(psLinkedListLock);
		if (psRecord != IMG_NULL)
		{
			OSFreeMemstatMem(psRecord);
		}
		return;
	}
	psMemoryStats = psProcessStats->psMemoryStats;

	_increase_global_stat(eAllocType, uiBytes);
	
	/* Insert the memory record... */
	if (psRecord != IMG_NULL)
	{
		psMemoryStats->ui32LastStatNumberRequested = 0;
		psMemoryStats->psLastStatMemoryRecordFound = IMG_NULL;
		List_PVRSRV_MEM_ALLOC_REC_Insert(&psMemoryStats->psMemoryRecords, psRecord);
	}

	/* Update the memory watermarks... */
	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		{
			if (psRecord != IMG_NULL)
			{
				if (pvCpuVAddr == IMG_NULL)
				{
					return;
				}
				psRecord->ui64Key = (IMG_UINT64)(IMG_UINTPTR_T)pvCpuVAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_KMALLOC],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC]);
		}
		break;
		
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		{
			if (psRecord != IMG_NULL)
			{
				if (pvCpuVAddr == IMG_NULL)
				{
					return;
				}
				psRecord->ui64Key = (IMG_UINT64)(IMG_UINTPTR_T)pvCpuVAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_VMALLOC],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC]);


		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
		{
			if (psRecord != IMG_NULL)
			{
				if (pvCpuVAddr == IMG_NULL)
				{
					return;
				}
				psRecord->ui64Key = (IMG_UINT64)(IMG_UINTPTR_T)pvCpuVAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_PAGES_PT_UMA],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA]);
		}
		break;
		
		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
		{
			if (psRecord != IMG_NULL)
			{
				if (pvCpuVAddr == IMG_NULL)
				{
					return;
				}
				psRecord->ui64Key = (IMG_UINT64)(IMG_UINTPTR_T)pvCpuVAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_VMAP_PT_UMA],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA]);
		}
		break;
		
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
		{
			if (psRecord != IMG_NULL)
			{
				psRecord->ui64Key = sCpuPAddr.uiAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_PAGES_PT_LMA],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA]);
		}
		break;
		
		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
		{
			if (psRecord != IMG_NULL)
			{
				if (pvCpuVAddr == IMG_NULL)
				{
					return;
				}
				psRecord->ui64Key = (IMG_UINT64)(IMG_UINTPTR_T)pvCpuVAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_IOREMAP_PT_LMA],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA]);
		}
		break;
		
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
		{
			if (psRecord != IMG_NULL)
			{
				psRecord->ui64Key = sCpuPAddr.uiAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_LMA_PAGES],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES]);
		}
		break;
		
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
		{
			if (psRecord != IMG_NULL)
			{
				psRecord->ui64Key = sCpuPAddr.uiAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_UMA_PAGES],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES]);
		}
		break;
		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
		{
			if (psRecord != IMG_NULL)
			{
				if (pvCpuVAddr == IMG_NULL)
				{
					return;
				}
				psRecord->ui64Key = (IMG_UINT64)(IMG_UINTPTR_T)pvCpuVAddr;
			}
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] += uiBytes;
			UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_MAP_UMA_LMA_PAGES],
							 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES]);
		}
		break;
		default:
		{
			PVR_ASSERT(0);
		}
		break;
	}

//Count total data
psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC] += psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] + PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES];
UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_ALLOC],
				 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC]);

psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP] += psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA];
UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_MAP],
				 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP]);

	OSLockRelease(psLinkedListLock);
#else
PVR_UNREFERENCED_PARAMETER(eAllocType);
PVR_UNREFERENCED_PARAMETER(pvCpuVAddr);
PVR_UNREFERENCED_PARAMETER(sCpuPAddr);
PVR_UNREFERENCED_PARAMETER(uiBytes);
PVR_UNREFERENCED_PARAMETER(pvPrivateData);
#endif
} /* PVRSRVStatsAddMemAllocRecord */


IMG_VOID
PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								IMG_UINT64 ui64Key)
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID                currentPid     = OSGetCurrentProcessIDKM();
    PVRSRV_PROCESS_STATS*  psProcessStats = IMG_NULL;
	PVRSRV_MEMORY_STATS*   psMemoryStats  = IMG_NULL;
	PVRSRV_MEM_ALLOC_REC*  psRecord       = IMG_NULL;
    IMG_BOOL               bFound         = IMG_FALSE;

    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	/* Lock while we find the correct process and remove this record... */
	OSLockAcquire(psLinkedListLock);

	/* First look for the record in the current PID... */
    psProcessStats = _FindProcessStats(currentPid);
    if (psProcessStats != IMG_NULL)
    {
		psMemoryStats = psProcessStats->psMemoryStats;
		psRecord      = psMemoryStats->psMemoryRecords;
		while (psRecord != IMG_NULL)
		{
			if (psRecord->ui64Key == ui64Key  &&  psRecord->eAllocType == eAllocType)
			{
				bFound = IMG_TRUE;
				break;
			}

			psRecord = psRecord->psNext;
		}
	}

	/* If not found, we need to do a full search in case it was allocated to a different PID... */
	if (!bFound)
	{
		PVRSRV_PROCESS_STATS*  psProcessStatsAlreadyChecked = psProcessStats;

		/* Search all live lists first... */
		psProcessStats = psLiveList;
		while (psProcessStats != IMG_NULL)
		{
			if (psProcessStats != psProcessStatsAlreadyChecked)
			{
				psMemoryStats = psProcessStats->psMemoryStats;
				psRecord      = psMemoryStats->psMemoryRecords;
				while (psRecord != IMG_NULL)
				{
					if (psRecord->ui64Key == ui64Key  &&  psRecord->eAllocType == eAllocType)
					{
						bFound = IMG_TRUE;
						break;
					}

					psRecord = psRecord->psNext;
				}
			}
			
			if (bFound)
			{
				break;
			}

			psProcessStats = psProcessStats->psNext;
		}

		/* If not found, then search all dead lists next... */
		if (!bFound)
		{
			psProcessStats = psDeadList;
			while (psProcessStats != IMG_NULL)
			{
				if (psProcessStats != psProcessStatsAlreadyChecked)
				{
					psMemoryStats = psProcessStats->psMemoryStats;
					psRecord      = psMemoryStats->psMemoryRecords;
					while (psRecord != IMG_NULL)
					{
						if (psRecord->ui64Key == ui64Key  &&  psRecord->eAllocType == eAllocType)
						{
							bFound = IMG_TRUE;
							break;
						}

						psRecord = psRecord->psNext;
					}
				}
				
				if (bFound)
				{
					break;
				}

				psProcessStats = psProcessStats->psNext;
			}
		}
	}

	/* Update the watermark and remove this record...*/
	if (bFound)
	{
		_decrease_global_stat(eAllocType, psRecord->uiBytes);
		
		switch (eAllocType)
		{
			case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] = 0;
				}			
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] = 0;
				}

			}
			break;
					
			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] = 0;
				}
			}
			break;
					
			case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] = 0;
				}			
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] = 0;
				}		
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			{
				if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] >= psRecord->uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] -= psRecord->uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] = 0;
				}
			}
			break;

			default:
			{
				PVR_ASSERT(0);
			}
			break;
		}

		psMemoryStats->ui32LastStatNumberRequested = 0;
		psMemoryStats->psLastStatMemoryRecordFound = IMG_NULL;
		List_PVRSRV_MEM_ALLOC_REC_Remove(psRecord);
	}
//Count total data
psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC] = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] + PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES];

psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP] = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] +\
    psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA];

	OSLockRelease(psLinkedListLock);

	/*
	 * Free the record outside the lock so we don't deadlock and so we
	 * reduce the time the lock is held.
	 */
	if (psRecord != IMG_NULL)
	{
		OSFreeMemstatMem(psRecord);
	}
#else
PVR_UNREFERENCED_PARAMETER(eAllocType);
PVR_UNREFERENCED_PARAMETER(ui64Key);
#endif
} /* PVRSRVStatsRemoveMemAllocRecord */

IMG_VOID
PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE eAllocType,
        							IMG_SIZE_T uiBytes,
        							IMG_UINT64 uiCpuVAddr)
{

	if (!bProcessStatsInitialised || (gpsTrackingTable == IMG_NULL) )
	{
		return;
	}

	HASH_Insert(gpsTrackingTable, uiCpuVAddr, uiBytes);

	PVRSRVStatsIncrMemAllocStat(eAllocType, uiBytes);
}

IMG_VOID
PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
        							IMG_SIZE_T uiBytes)
{
	IMG_PID                currentPid = OSGetCurrentProcessIDKM();
    PVRSRV_PROCESS_STATS*  psProcessStats;

    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	_increase_global_stat(eAllocType, uiBytes);

    psProcessStats = _FindProcessStats(currentPid);
    if (psProcessStats != IMG_NULL)
    {
		/* Update the memory watermarks... */
		switch (eAllocType)
		{
			case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_KMALLOC],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_VMALLOC],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_PAGES_PT_UMA],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_VMAP_PT_UMA],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_PAGES_PT_LMA],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_IOREMAP_PT_LMA],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_LMA_PAGES],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_ALLOC_UMA_PAGES],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES]);
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			{
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] += uiBytes;
				UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_MAP_UMA_LMA_PAGES],
								 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES]);
			}
			break;

			default:
			{
				PVR_ASSERT(0);
			}
			break;
		}
        //Count total data
        psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC] = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] + PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES];
        UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_ALLOC],
                        psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC]);

        psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP] = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA];
        UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_TOTAL_MAP],
                        psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP]);
    }
}

IMG_VOID
PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE eAllocType,
        							IMG_UINT64 uiCpuVAddr)
{
	IMG_SIZE_T uiBytes;

	if (!bProcessStatsInitialised || (gpsTrackingTable == IMG_NULL) )
	{
		return;
	}

	uiBytes = HASH_Remove(gpsTrackingTable, uiCpuVAddr);

	PVRSRVStatsDecrMemAllocStat(eAllocType, uiBytes);
}

IMG_VOID
PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
        							IMG_SIZE_T uiBytes)
{
	IMG_PID                currentPid = OSGetCurrentProcessIDKM();
    PVRSRV_PROCESS_STATS*  psProcessStats;

    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	_decrease_global_stat(eAllocType, uiBytes);

    psProcessStats = _FindProcessStats(currentPid);
    if (psProcessStats != IMG_NULL)
    {
		/* Update the memory watermarks... */
		switch (eAllocType)
		{
			case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] = 0;
				}

			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] = 0;
				}
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			{
				if ((IMG_SIZE_T)psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] >= uiBytes)
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] -= uiBytes;
				}
				else
				{
					psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] = 0;
				}
			}
			break;

			default:
			{
				PVR_ASSERT(0);
			}
			break;
		}
		//Count total data
        psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_ALLOC] = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] + PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] + psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES];

        psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAP] = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] +\
            psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA];
	}
}


IMG_VOID
PVRSRVStatsUpdateRenderContextStats(IMG_UINT32 ui32TotalNumPartialRenders,
                                    IMG_UINT32 ui32TotalNumOutOfMemory,
                                    IMG_UINT32 ui32NumTAStores,
                                    IMG_UINT32 ui32Num3DStores,
                                    IMG_UINT32 ui32NumSHStores,
                                    IMG_UINT32 ui32NumCDMStores,
                                    IMG_PID pidOwner)
{
	//IMG_PID                currentPid = OSGetCurrentProcessIDKM();
	IMG_PID	pidCurrent=pidOwner;

    PVRSRV_PROCESS_STATS*  psProcessStats;

    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(psLinkedListLock);

    psProcessStats = _FindProcessStats(pidCurrent);
    if (psProcessStats != IMG_NULL)
    {
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_PRS]       += ui32TotalNumPartialRenders;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_OOMS]      += ui32TotalNumOutOfMemory;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_TA_STORES] += ui32NumTAStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_3D_STORES] += ui32Num3DStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_SH_STORES] += ui32NumSHStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_CDM_STORES]+= ui32NumCDMStores;
	}
    else
    {
    	PVR_DPF((PVR_DBG_WARNING, "PVRSRVStatsUpdateRenderContextStats: Null process. Pid=%d", pidCurrent));
    }

	OSLockRelease(psLinkedListLock);
} /* PVRSRVStatsUpdateRenderContextStats */


IMG_VOID
PVRSRVStatsUpdateZSBufferStats(IMG_UINT32 ui32NumReqByApp,
                               IMG_UINT32 ui32NumReqByFW,
                               IMG_PID owner)
{
	IMG_PID                currentPid = (owner==0)?OSGetCurrentProcessIDKM():owner;
    PVRSRV_PROCESS_STATS*  psProcessStats;


    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(psLinkedListLock);

    psProcessStats = _FindProcessStats(currentPid);
    if (psProcessStats != IMG_NULL)
    {
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_APP] += ui32NumReqByApp;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_FW]  += ui32NumReqByFW;
	}

	OSLockRelease(psLinkedListLock);
} /* PVRSRVStatsUpdateZSBufferStats */


IMG_VOID
PVRSRVStatsUpdateFreelistStats(IMG_UINT32 ui32NumGrowReqByApp,
                               IMG_UINT32 ui32NumGrowReqByFW,
                               IMG_UINT32 ui32InitFLPages,
                               IMG_UINT32 ui32NumHighPages,
                               IMG_PID ownerPid)
{
	IMG_PID                currentPid = (ownerPid!=0)?ownerPid:OSGetCurrentProcessIDKM();
    PVRSRV_PROCESS_STATS*  psProcessStats;

    /* Don't do anything if we are not initialised or we are shutting down! */
    if (!bProcessStatsInitialised)
    {
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(psLinkedListLock);

	psProcessStats = _FindProcessStats(currentPid);

    if (psProcessStats != IMG_NULL)
    {
		/* Avoid signed / unsigned mismatch which is flagged by some compilers */
		IMG_INT32 a, b;

		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_APP] += ui32NumGrowReqByApp;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_FW]  += ui32NumGrowReqByFW;
		
		a=psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT];
		b=(IMG_INT32)(ui32InitFLPages);
		UPDATE_MAX_VALUE(a, b);

		
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT]=a;
		ui32InitFLPages=(IMG_UINT32)b;

		a=psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_MAX_PAGES];
		b=(IMG_INT32)ui32NumHighPages;

		UPDATE_MAX_VALUE(a, b);
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT]=a;
		ui32InitFLPages=(IMG_UINT32)b;

	}

	OSLockRelease(psLinkedListLock);
} /* PVRSRVStatsUpdateFreelistStats */


/*************************************************************************/ /*!
@Function       _ObtainProcessStatistic
@Description    Returns a specific statistic number for a process.
@Input          psProcessStats  Process statistics to examine.
@Input          ui32StatNumber  Index of the statistic to return.
@Output         pi32StatData    Statistic value.
@Output         peStatistic     Statistic type being returned.
@Output         pszStatFmtText  Pointer to formatting string for the stat.
@Return         IMG_TRUE if a statistic is returned, IMG_FALSE if no statistic
                is available (and this also implies there are no more to get).
*/ /**************************************************************************/
static IMG_BOOL
_ObtainProcessStatistic(PVRSRV_PROCESS_STATS* psProcessStats,
                        IMG_UINT32 ui32StatNumber,
                        IMG_INT32* pi32StatData,
                        IMG_CHAR** ppszStatFmtText)
{
	PVR_ASSERT(psProcessStats != IMG_NULL);
	PVR_ASSERT(pi32StatData != IMG_NULL);

	if (ui32StatNumber < PVRSRV_PROCESS_STAT_TYPE_COUNT)
	{
		*pi32StatData = psProcessStats->i32StatValue[ui32StatNumber];
		
		if (ppszStatFmtText != IMG_NULL)
		{
			*ppszStatFmtText = pszProcessStatFmt[ui32StatNumber];
		}

		return IMG_TRUE;
	}

	return IMG_FALSE;
} /* _ObtainProcessStatistic */


#if defined(PVRSRV_ENABLE_MEMORY_STATS)
/*************************************************************************/ /*!
@Function       _ObtainMemoryStatistic
@Description    Returns a specific statistic number for a memory area.
@Input          psMemoryStats   Memory statistics to examine.
@Input          ui32StatNumber  Index of the statistic to return.
@Output         pi32StatData    Statistic value.
@Output         pszStatFmtText  Pointer to formatting string for the stat.
@Return         IMG_TRUE if a statistic is returned, IMG_FALSE if no statistic
                is available (and this also implies there are no more to get).
*/ /**************************************************************************/
static IMG_BOOL
_ObtainMemoryStatistic(PVRSRV_MEMORY_STATS* psMemoryStats,
                       IMG_UINT32 ui32StatNumber,
                       IMG_INT32* pi32StatData,
                       IMG_CHAR** ppszStatFmtText)
{
	PVRSRV_MEM_ALLOC_REC  *psRecord;
	IMG_BOOL              found = IMG_FALSE;
	IMG_UINT32            ui32ItemNumber, ui32VAddrFields, ui32PAddrFields;

	PVR_ASSERT(psMemoryStats != IMG_NULL);
	PVR_ASSERT(pi32StatData != IMG_NULL);

	/*
	 *  Displaying the record is achieved by passing back 32bit chunks of data and
	 *  appropriate formatting for it. Therefore the ui32StatNumber is divided by the
	 *  number of items to be displayed.
	 *
	 *  sCpuPAddr.uiAddr is assumed to be a multiple of 32bits.
	 */
	ui32VAddrFields = sizeof(IMG_VOID*)/sizeof(IMG_UINT32);
	ui32PAddrFields = sizeof(IMG_CPU_PHYADDR)/sizeof(IMG_UINT32);
	ui32ItemNumber  = ui32StatNumber % (2 + ui32VAddrFields + ui32PAddrFields);
	ui32StatNumber  = ui32StatNumber / (2 + ui32VAddrFields + ui32PAddrFields);
	
	/* Write the header... */
	if (ui32StatNumber == 0)
	{
		if (ui32ItemNumber == 0)
		{
			*pi32StatData    = 0;
			*ppszStatFmtText = "Type                ";
		}
		else if (ui32ItemNumber-1 < ui32VAddrFields)
		{
			*pi32StatData    = 0;
			*ppszStatFmtText = (ui32ItemNumber-1 == 0 ? "VAddress  " : "        ");
		}
		else if (ui32ItemNumber-1-ui32VAddrFields < ui32PAddrFields)
		{
			*pi32StatData    = 0;
			*ppszStatFmtText = (ui32ItemNumber-1-ui32VAddrFields == 0 ? "PAddress  " : "        ");
		}
		else if (ui32ItemNumber == ui32PAddrFields+2)
		{
			*pi32StatData    = 0;
			*ppszStatFmtText = "Size(bytes)\n";
		}
		
		return IMG_TRUE;
	}

	/* The lock has to be held whilst moving through the memory list... */
	OSLockAcquire(psLinkedListLock);
	
	/* Check if we have a cached position... */
	if (psMemoryStats->ui32LastStatNumberRequested == ui32StatNumber  &&
		psMemoryStats->psLastStatMemoryRecordFound != IMG_NULL)
	{
		psRecord = psMemoryStats->psLastStatMemoryRecordFound;
	}
	else if (psMemoryStats->ui32LastStatNumberRequested == ui32StatNumber-1  &&
	         psMemoryStats->psLastStatMemoryRecordFound != IMG_NULL)
	{
		psRecord = psMemoryStats->psLastStatMemoryRecordFound->psNext;
	}
	else
	{
		psRecord = psMemoryStats->psMemoryRecords;
		while (psRecord != IMG_NULL  &&  ui32StatNumber > 1)
		{
			psRecord = psRecord->psNext;
			ui32StatNumber--;
		}
	}

	psMemoryStats->ui32LastStatNumberRequested = ui32StatNumber;
	psMemoryStats->psLastStatMemoryRecordFound = psRecord;
	
	/* Was a record found? */
	if (psRecord != IMG_NULL)
	{
		if (ui32ItemNumber == 0)
		{
			*pi32StatData    = 0;
			switch (psRecord->eAllocType)
			{
				case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:      		*ppszStatFmtText = "KMALLOC             "; break;
				case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:      		*ppszStatFmtText = "VMALLOC             "; break;
				case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:  *ppszStatFmtText = "ALLOC_PAGES_PT_LMA  "; break;
				case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:  *ppszStatFmtText = "ALLOC_PAGES_PT_UMA  "; break;
				case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:      *ppszStatFmtText = "IOREMAP_PT_LMA      "; break;
				case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:         *ppszStatFmtText = "VMAP_PT_UMA         "; break;
				case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES: 	*ppszStatFmtText = "ALLOC_LMA_PAGES     "; break;
				case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES: *ppszStatFmtText = "ALLOC_UMA_PAGES    "; break;
				case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES: *ppszStatFmtText = "MAP_UMA_LMA_PAGES   "; break;
				default:                                 		*ppszStatFmtText = "INVALID             "; break;
			}
		}
		else if (ui32ItemNumber-1 < ui32VAddrFields)
		{
			*pi32StatData    = *(((IMG_UINT32*) &psRecord->pvCpuVAddr) + ui32VAddrFields - (ui32ItemNumber-1) - 1);
			*ppszStatFmtText = (ui32ItemNumber-1 == ui32VAddrFields-1 ? "%08x  " : "%08x");
		}
		else if (ui32ItemNumber-1-ui32VAddrFields < ui32PAddrFields)
		{
			*pi32StatData    = *(((IMG_UINT32*) &psRecord->sCpuPAddr.uiAddr) + ui32PAddrFields - (ui32ItemNumber-1-ui32VAddrFields) - 1);
			*ppszStatFmtText = (ui32ItemNumber-1-ui32VAddrFields == ui32PAddrFields-1 ? "%08x  " : "%08x");
		}
		else if (ui32ItemNumber == ui32PAddrFields+2)
		{
			*pi32StatData    = psRecord->uiBytes;
			*ppszStatFmtText = "%u\n";
		}

		found = IMG_TRUE;
	}

	OSLockRelease(psLinkedListLock);

	return found;
} /* _ObtainMemoryStatistic */
#endif


#if defined(PVR_RI_DEBUG)
/*************************************************************************/ /*!
@Function       _ObtainRIMemoryStatistic
@Description    Returns a specific RI MEMDESC entry.
@Input          psMemoryStats   Memory statistics to examine.
@Input          ui32StatNumber  Index of the statistic to return.
@Output         pi32StatData    Statistic value.
@Output         pszStatFmtText  Pointer to formatting string for the stat.
@Return         IMG_TRUE if a statistic is returned, IMG_FALSE if no statistic
                is available (and this also implies there are no more to get).
*/ /**************************************************************************/
static IMG_BOOL
_ObtainRIMemoryStatistic(PVRSRV_RI_MEMORY_STATS* psRIMemoryStats,
                         IMG_UINT32 ui32StatNumber,
                         IMG_INT32* pi32StatData,
                         IMG_CHAR** ppszStatFmtText)
{
	static IMG_UINT32 ui32LastStatNumber = 0;
	static IMG_BOOL bLastStatus = IMG_TRUE;

	PVR_ASSERT(psRIMemoryStats != IMG_NULL);

	PVR_UNREFERENCED_PARAMETER(pi32StatData);

	if (ui32StatNumber > 0 && ui32LastStatNumber == ui32StatNumber)
		return bLastStatus;
	ui32LastStatNumber = ui32StatNumber;

	/*
	 * If this request is for the first item then set the handle to null.
	 * If it is not the first request, then the handle should not be null
	 * unless this is the OS's way of double checking that the previous
	 * request, which returned IMG_FALSE was indeed the last request.
	 */
	if (ui32StatNumber == 0)
	{
		psRIMemoryStats->pRIHandle = IMG_NULL;
	}
	else if (psRIMemoryStats->pRIHandle == IMG_NULL)
	{
		ui32LastStatNumber = 0;
		return IMG_FALSE;
	}

	bLastStatus = RIGetListEntryKM(psRIMemoryStats->pid,
	                               &psRIMemoryStats->pRIHandle,
	                               ppszStatFmtText);
	return bLastStatus;
} /* _ObtainRIMemoryStatistic */
#endif


/*************************************************************************/ /*!
@Function       PVRSRVStatsObtainElement
@Description    Returns a specific statistic number for a process. Clients are
                expected to iterate through the statistics by requesting stat
                0,1,2,3,4,etc. and stopping when FALSE is returned.
@Input          pvStatPtr       Pointer to statistics structure.
@Input          ui32StatNumber  Index of the statistic to return.
@Output         pi32StatData    Statistic value.
@Output         pszStatFmtText  Pointer to formatting string for the stat.
@Return         IMG_TRUE if a statistic is returned, IMG_FALSE if no statistic
                is available (and this also implies there are no more to get).
*/ /**************************************************************************/
IMG_BOOL
PVRSRVStatsObtainElement(IMG_PVOID pvStatPtr, IMG_UINT32 ui32StatNumber,
                         IMG_INT32* pi32StatData, IMG_CHAR** ppszStatFmtText)
{
	IMG_BOOL bRes = IMG_FALSE;
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;

	if (peStructureType == IMG_NULL  ||  pi32StatData == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid param, peStructureType=<%p>, pvStatPtr=<%p>", __FUNCTION__, (void*)peStructureType, (void*)pvStatPtr));
		return IMG_FALSE;
	}

	if (*peStructureType == PVRSRV_STAT_STRUCTURE_PROCESS)
	{
		PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) pvStatPtr;

		bRes = _ObtainProcessStatistic(psProcessStats, ui32StatNumber,
									   pi32StatData, ppszStatFmtText);
		return bRes;
	}
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	else if (*peStructureType == PVRSRV_STAT_STRUCTURE_MEMORY)
	{
		PVRSRV_MEMORY_STATS*  psMemoryStats = (PVRSRV_MEMORY_STATS*) pvStatPtr;

		bRes = _ObtainMemoryStatistic(psMemoryStats, ui32StatNumber,
									  pi32StatData, ppszStatFmtText);
		return bRes;
	}
#endif
#if defined(PVR_RI_DEBUG)
	else if (*peStructureType == PVRSRV_STAT_STRUCTURE_RIMEMORY)
	{
		PVRSRV_RI_MEMORY_STATS*  psRIMemoryStats = (PVRSRV_RI_MEMORY_STATS*) pvStatPtr;

		bRes = _ObtainRIMemoryStatistic(psRIMemoryStats, ui32StatNumber,
									    pi32StatData, ppszStatFmtText);
		return bRes;
	}
#endif
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Unrecognised peStructureType<%p>=%d", __FUNCTION__, (void*)peStructureType, *peStructureType));
	}

	return IMG_FALSE;
} /* PVRSRVStatsObtainElement */

static IMG_UINT32	ui32FirmwareStartTimestamp=0;
static IMG_UINT64	ui64FirmwareIdleDuration=0;

/* Averaging each new value with the previous accumulated knowledge. There are many coefficients for that
 * (e.g.) 50 / 50 but i chose 75 / 25 meaning that previous knowledge affects the weighted average more
 * than any new knowledge. As time goes by though eventually the number converges to the most commonly used
 */

IMG_VOID SetFirmwareStartTime(IMG_UINT32 ui32Time)
{
    if (ui32FirmwareStartTimestamp > 0)
    {
        ui32FirmwareStartTimestamp=MEAN_TIME(ui32FirmwareStartTimestamp, ui32Time);
    }
    else
    {
        ui32FirmwareStartTimestamp = ui32Time;
    }
}

IMG_VOID SetFirmwareHandshakeIdleTime(IMG_UINT64 ui64Duration)
{
    if (ui64FirmwareIdleDuration > 0)
	{
        ui64FirmwareIdleDuration=MEAN_TIME(ui64FirmwareIdleDuration, ui64Duration);
	}
	else
	{
		ui64FirmwareIdleDuration = ui64Duration;
	}
}


IMG_BOOL PVRSRVPowerStatsObtainElement(IMG_PVOID pvStatPtr, IMG_UINT32 ui32StatNumber,
                         IMG_INT32* pi32StatData, IMG_CHAR** ppszStatFmtText)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;

    if (peStructureType == IMG_NULL  ||  pi32StatData == IMG_NULL)
	{
		/* Stat type not handled probably indicates bad pointers... */
		PVR_DPF((PVR_DBG_ERROR, "%s: Invalid param, peStructureType=<%p>, pvStatPtr=<%p>, ppszStatFmtText=<%p>, *ppszStatFmtText=<%p>", __FUNCTION__, (void*)peStructureType, (void*)pvStatPtr, (void*)ppszStatFmtText, (void*)*ppszStatFmtText));
		PVR_ASSERT(IMG_FALSE);
		return IMG_FALSE;
	}

    switch(ui32StatNumber)
    {
        case PVRSRV_POWER_TIMING_STAT_FORCED_POWER_TRANSITION:
        {
            (*ppszStatFmtText)="Forced Power Transition (nanoseconds):\n\n";
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_PRE_DEVICE:
        {
            (*ppszStatFmtText)="Pre-Device: %u\n";
			if (ui64TotalForcedEntries > 0)
			{
				(*pi32StatData)= ((IMG_UINT32)(ui64ForcedPreDevice)/(IMG_UINT32) ui64TotalForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_PRE_SYSTEM:
        {
            (*ppszStatFmtText)="Pre-System: %u\n";
			if (ui64TotalForcedEntries > 0)
			{
				(*pi32StatData)=((IMG_UINT32)(ui64ForcedPreSystem)/(IMG_UINT32) ui64TotalForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_POST_DEVICE:
        {
            (*ppszStatFmtText)="Post-Device: %u\n";
			if (ui64TotalForcedEntries > 0)
			{
				(*pi32StatData)=((IMG_UINT32)(ui64ForcedPostDevice)/(IMG_UINT32) ui64TotalForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_POST_SYSTEM:
        {
            (*ppszStatFmtText)="Post-System: %u\n";
			if (ui64TotalForcedEntries > 0)
			{
				(*pi32StatData)=((IMG_UINT32)(ui64ForcedPostSystem)/(IMG_UINT32) ui64TotalForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_NEWLINE1:
        case PVRSRV_POWER_TIMING_STAT_NEWLINE2:
        {
            (*ppszStatFmtText)="\n\n\n\n";
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_NOT_FORCED_POWER_TRANSITION:
        {
            (*ppszStatFmtText)="Not Forced Power Transition (nanoseconds):\n\n";
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_NON_PRE_DEVICE:
        {
            (*ppszStatFmtText)="Pre-Device: %u\n";
			if (ui64TotalNotForcedEntries > 0)
			{
				(*pi32StatData)=((IMG_UINT32)(ui64NotForcedPreDevice)/(IMG_UINT32) ui64TotalNotForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_NON_PRE_SYSTEM:
        {
            (*ppszStatFmtText)="Pre-System: %u\n";
			if (ui64TotalNotForcedEntries > 0)
			{
				(*pi32StatData)=((IMG_UINT32)(ui64NotForcedPreSystem)/(IMG_UINT32) ui64TotalNotForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_NON_POST_DEVICE:
        {
            (*ppszStatFmtText)="Post-Device: %u\n";
			if (ui64TotalNotForcedEntries > 0)
			{
				(*pi32StatData)= ((IMG_UINT32)(ui64NotForcedPostDevice)/(IMG_UINT32) ui64TotalNotForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_NON_POST_SYSTEM:
        {
            (*ppszStatFmtText)="Post-System: %u\n";
			if (ui64TotalNotForcedEntries > 0)
			{
				(*pi32StatData)=((IMG_UINT32)(ui64NotForcedPostSystem)/(IMG_UINT32) ui64TotalNotForcedEntries);
			}
			else
			{
				(*pi32StatData)= 0;
			}
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_FW_BOOTUP_TIME:
        {
            (*ppszStatFmtText)="FW bootup time (timer tics): %u\n";
            (*pi32StatData)=ui32FirmwareStartTimestamp;
            break;
        }
        case PVRSRV_POWER_TIMING_STAT_HOST_ACK:
        {
            (*ppszStatFmtText)="Host Acknowledge Time for FW Idle Signal (timer tics): %u\n";
            (*pi32StatData)=(IMG_UINT32)(ui64FirmwareIdleDuration);
            break;
        }
        default:
        {
            return IMG_FALSE;
        }
    }
    return IMG_TRUE;
}


static IMG_BOOL
_PVRSRVGetGlobalMemStat(IMG_PVOID pvStatPtr, 
						IMG_UINT32 ui32StatNumber, 
						IMG_INT32* pi32StatData, 
						IMG_CHAR** ppszStatFmtText)
{
	switch (ui32StatNumber)
	{
		case 0:	
			(*ppszStatFmtText) = "MemoryUsageKMalloc                %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageKMalloc;
			break;

		case 1:
			(*ppszStatFmtText) = "MemoryUsageKMallocMax             %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageKMallocMax;
			break;

		case 2:
			(*ppszStatFmtText) = "MemoryUsageVMalloc                %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageVMalloc;
			break;

		case 3:
			(*ppszStatFmtText) = "MemoryUsageVMallocMax             %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageVMallocMax;
			break;

		case 4:
			(*ppszStatFmtText) = "MemoryUsageAllocPTMemoryUMA       %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA;
			break;

		case 5:
			(*ppszStatFmtText) = "MemoryUsageAllocPTMemoryUMAMax    %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMAMax;
			break;

		case 6:
			(*ppszStatFmtText) = "MemoryUsageVMapPTUMA              %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageVMapPTUMA;
			break;

		case 7:
			(*ppszStatFmtText) = "MemoryUsageVMapPTUMAMax           %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageVMapPTUMAMax;
			break;

		case 8:
			(*ppszStatFmtText) = "MemoryUsageAllocPTMemoryLMA       %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA;
			break;

		case 9:
			(*ppszStatFmtText) = "MemoryUsageAllocPTMemoryLMAMax    %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMAMax;
			break;

		case 10:
			(*ppszStatFmtText) = "MemoryUsageIORemapPTLMA           %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageIORemapPTLMA;
			break;

		case 11:
			(*ppszStatFmtText) = "MemoryUsageIORemapPTLMAMax        %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageIORemapPTLMAMax;
			break;

		case 12:
			(*ppszStatFmtText) = "MemoryUsageAllocGPUMemLMA         %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA;
			break;

		case 13:
			(*ppszStatFmtText) = "MemoryUsageAllocGPUMemLMAMax      %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocGPUMemLMAMax;
			break;

		case 14:
			(*ppszStatFmtText) = "MemoryUsageAllocGPUMemUMA         %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA;
			break;

		case 15:
			(*ppszStatFmtText) = "MemoryUsageAllocGPUMemUMAMax      %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAMax;
			break;

		case 16:
			(*ppszStatFmtText) = "MemoryUsageMappedGPUMemUMA/LMA    %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA;
			break;

		case 17:
			(*ppszStatFmtText) = "MemoryUsageMappedGPUMemUMA/LMAMax %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMAMax;
			break;

        //zxl: count total data
        case 18:
			(*ppszStatFmtText) = "MemoryUsageTotalAlloc             %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageTotalAlloc;
			break;

        case 19:
			(*ppszStatFmtText) = "MemoryUsageTotalAllocMax          %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageTotalAllocMax;
			break;

        case 20:
			(*ppszStatFmtText) = "MemoryUsageTotalMap               %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageTotalMap;
			break;

        case 21:
			(*ppszStatFmtText) = "MemoryUsageTotalMapMax            %10u\n";
			(*pi32StatData) = (IMG_INT32) gsGlobalStats.ui32MemoryUsageTotalMapMax;
			break;

		default:
			return IMG_FALSE;
	}
	
	
	return IMG_TRUE;
}
