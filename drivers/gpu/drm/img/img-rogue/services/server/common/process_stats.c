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

#include "img_defs.h"
#include "img_types.h"
#include "pvr_debug.h"
#include "lock.h"
#include "allocmem.h"
#include "osfunc.h"
#include "process_stats.h"
#include "ri_server.h"
#include "hash.h"
#include "connection_server.h"
#include "pvrsrv.h"
#include "proc_stats.h"
#include "pvr_ricommon.h"
#include "di_server.h"
#include "dllist.h"
#if defined(__linux__)
#include "trace_events.h"
#endif

/* Enabled OS Statistics entries: DEBUGFS on Linux, undefined for other OSs */
#if defined(__linux__) && ( \
	defined(PVRSRV_ENABLE_PERPID_STATS) || \
	defined(PVRSRV_ENABLE_CACHEOP_STATS) || \
	defined(PVRSRV_ENABLE_MEMORY_STATS) || \
	defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) )
#define ENABLE_DEBUGFS_PIDS
#endif

/* Enable GPU memory accounting tracepoint */
#if defined(__linux__) && ( \
	defined(CONFIG_TRACE_GPU_MEM) || defined(PVRSRV_ENABLE_GPU_MEM_TRACEPOINT) )
#define ENABLE_GPU_MEM_TRACEPOINT
#endif

/*
 * Maximum history of process statistics that will be kept.
 */
#define MAX_DEAD_LIST_PROCESSES  (10)

/*
 * Definition of all the strings used to format process based statistics.
 */

#if defined(PVRSRV_ENABLE_PERPID_STATS)
/* Array of Process stat type defined using the X-Macro */
#define X(stat_type, stat_str) stat_str,
static const IMG_CHAR *const pszProcessStatType[PVRSRV_PROCESS_STAT_TYPE_COUNT] = { PVRSRV_PROCESS_STAT_KEY };
static const IMG_CHAR *const pszDeviceStatType[PVRSRV_DEVICE_STAT_TYPE_COUNT] = { PVRSRV_DEVICE_STAT_KEY };
#undef X
#endif

/* Array of Driver stat type defined using the X-Macro */
#define X(stat_type, stat_str) stat_str,
static const IMG_CHAR *const pszDriverStatType[PVRSRV_DRIVER_STAT_TYPE_COUNT] = { PVRSRV_DRIVER_STAT_KEY };
#undef X

/* structure used in hash table to track statistic entries */
typedef struct {
	size_t	   uiSizeInBytes;
	IMG_PID	   uiPid;
} _PVR_STATS_TRACKING_HASH_ENTRY;

/* Function used internally to decrement tracked per-process statistic entries */
static void _StatsDecrMemTrackedStat(_PVR_STATS_TRACKING_HASH_ENTRY *psTrackingHashEntry,
                                     PVRSRV_MEM_ALLOC_TYPE eAllocType);

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
int RawProcessStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData);
#endif
int GlobalStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData);

/* Note: all of the accesses to the global stats should be protected
 * by the gsGlobalStats.hGlobalStatsLock lock. This means all of the
 * invocations of macros *_GLOBAL_STAT_VALUE. */

/* Macros for fetching stat values */
#define GET_STAT_VALUE(ptr,var) (ptr)->i64StatValue[(var)]
#define GET_GLOBAL_STAT_VALUE(idx) gsGlobalStats.ui64StatValue[idx]

#define GET_GPUMEM_GLOBAL_STAT_VALUE() \
	GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA) + \
	GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA) + \
	GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA) + \
	GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA) + \
	GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT)

#define GET_GPUMEM_PERPID_STAT_VALUE(ptr) \
	GET_STAT_VALUE((ptr), PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA) + \
	GET_STAT_VALUE((ptr), PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA) + \
	GET_STAT_VALUE((ptr), PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES) + \
	GET_STAT_VALUE((ptr), PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES) + \
	GET_STAT_VALUE((ptr), PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT)
/*
 * Macros for updating stat values.
 */
#define UPDATE_MAX_VALUE(a,b)					do { if ((b) > (a)) {(a) = (b);} } while (0)
#define INCREASE_STAT_VALUE(ptr,var,val)		do { (ptr)->i64StatValue[(var)] += (IMG_INT64)(val); if ((ptr)->i64StatValue[(var)] > (ptr)->i64StatValue[(var##_MAX)]) {(ptr)->i64StatValue[(var##_MAX)] = (ptr)->i64StatValue[(var)];} } while (0)
#define INCREASE_GLOBAL_STAT_VALUE(var,idx,val)		do { (var).ui64StatValue[(idx)] += (IMG_UINT64)(val); if ((var).ui64StatValue[(idx)] > (var).ui64StatValue[(idx##_MAX)]) {(var).ui64StatValue[(idx##_MAX)] = (var).ui64StatValue[(idx)];} } while (0)
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
/* Allow stats to go negative */
#define DECREASE_STAT_VALUE(ptr,var,val)		do { (ptr)->i64StatValue[(var)] -= (val); } while (0)
#define DECREASE_GLOBAL_STAT_VALUE(var,idx,val)		do { (var).ui64StatValue[(idx)] -= (val); } while (0)
#else
#define DECREASE_STAT_VALUE(ptr,var,val)		do { if ((ptr)->i64StatValue[(var)] >= (val)) { (ptr)->i64StatValue[(var)] -= (IMG_INT64)(val); } else { (ptr)->i64StatValue[(var)] = 0; } } while (0)
#define DECREASE_GLOBAL_STAT_VALUE(var,idx,val)		do { if ((var).ui64StatValue[(idx)] >= (val)) { (var).ui64StatValue[(idx)] -= (IMG_UINT64)(val); } else { (var).ui64StatValue[(idx)] = 0; } } while (0)
#endif
#define MAX_CACHEOP_STAT 16
#define INCREMENT_CACHEOP_STAT_IDX_WRAP(x) ((x+1) >= MAX_CACHEOP_STAT ? 0 : (x+1))
#define DECREMENT_CACHEOP_STAT_IDX_WRAP(x) ((x-1) < 0 ? (MAX_CACHEOP_STAT-1) : (x-1))

/*
 * Track the search of one process when PVRSRV_DEBUG_LINUX_MEMORY_STATS
 * is enabled.
 */
typedef enum _PVRSRV_PROC_SEARCH_STATE_
{
	PVRSRV_PROC_NOTFOUND,
	PVRSRV_PROC_FOUND,
	PVRSRV_PROC_RESURRECTED,
} PVRSRV_PROC_SEARCH_STATE;

/*
 * Structures for holding statistics...
 */
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
typedef struct _PVRSRV_MEM_ALLOC_REC_
{
	PVRSRV_MEM_ALLOC_TYPE           eAllocType;
	void*                           pvCpuVAddr;
	IMG_CPU_PHYADDR	                sCpuPAddr;
	size_t                          uiBytes;
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
	void*                           pvAllocdFromFile;
	IMG_UINT32                      ui32AllocdFromLine;
#endif
} PVRSRV_MEM_ALLOC_REC;

typedef struct PVRSRV_MEM_ALLOC_PRINT_DATA_TAG
{
	OSDI_IMPL_ENTRY *psEntry;
	IMG_PID	        pid;
	IMG_UINT32      ui32NumEntries;
} PVRSRV_MEM_ALLOC_PRINT_DATA;
#endif

typedef struct _PVRSRV_PROCESS_STATS_ {

	/* Linked list pointers */
	DLLIST_NODE                    sNode;

	/* Create per process lock that need to be held
	 * to edit of its members */
	POS_LOCK                       hLock;

	/* OS level process ID */
	IMG_PID	                       pid;
	IMG_UINT32                     ui32RefCount;

	/* Process memory stats */
	IMG_INT64                      i64StatValue[PVRSRV_PROCESS_STAT_TYPE_COUNT];
	IMG_UINT32                     ui32StatAllocFlags;

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
	struct _CACHEOP_STRUCT_ {
		PVRSRV_CACHE_OP        uiCacheOp;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
		IMG_DEV_VIRTADDR       sDevVAddr;
		IMG_DEV_PHYADDR        sDevPAddr;
#endif
		IMG_DEVMEM_SIZE_T      uiOffset;
		IMG_DEVMEM_SIZE_T      uiSize;
		IMG_UINT64             ui64ExecuteTime;
		IMG_BOOL               bUserModeFlush;
		IMG_BOOL               bIsFence;
		IMG_PID                ownerPid;
	}                              asCacheOp[MAX_CACHEOP_STAT];
	IMG_INT32                      uiCacheOpWriteIndex;
#endif

	/* Other statistics structures */
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	HASH_TABLE* psMemoryRecords;
#endif
	/* Device stats */
	IMG_UINT32                     ui32DevCount;
	IMG_INT32                      ai32DevStats[][PVRSRV_DEVICE_STAT_TYPE_COUNT];
} PVRSRV_PROCESS_STATS;

#if defined(ENABLE_DEBUGFS_PIDS)

typedef struct _PVRSRV_OS_STAT_ENTRY_
{
	DI_GROUP *psStatsDIGroup;
	DI_ENTRY *psProcessStatsDIEntry;
	DI_ENTRY *psMemStatsDIEntry;
	DI_ENTRY *psRIMemStatsDIEntry;
	DI_ENTRY *psCacheOpStatsDIEntry;
} PVRSRV_OS_STAT_ENTRY;

static PVRSRV_OS_STAT_ENTRY gsLiveStatEntries;
static PVRSRV_OS_STAT_ENTRY gsRetiredStatEntries;

int GenericStatsPrintElementsLive(OSDI_IMPL_ENTRY *psEntry, void *pvData);
int GenericStatsPrintElementsRetired(OSDI_IMPL_ENTRY *psEntry, void *pvData);

/*
 * Functions for printing the information stored...
 */
#if defined(PVRSRV_ENABLE_PERPID_STATS)
void ProcessStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                               PVRSRV_PROCESS_STATS *psProcessStats);
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
void MemStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                           PVRSRV_PROCESS_STATS *psProcessStats);
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
void RIMemStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                             PVRSRV_PROCESS_STATS *psProcessStats);
#endif

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
void CacheOpStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                               PVRSRV_PROCESS_STATS *psProcessStats);
#endif

typedef void (PVRSRV_STATS_PRINT_ELEMENTS)(OSDI_IMPL_ENTRY *psEntry,
                                           PVRSRV_PROCESS_STATS *psProcessStats);

typedef enum
{
	PVRSRV_STAT_TYPE_PROCESS,
	PVRSRV_STAT_TYPE_MEMORY,
	PVRSRV_STAT_TYPE_RIMEMORY,
	PVRSRV_STAT_TYPE_CACHEOP,
	PVRSRV_STAT_TYPE_LAST
} PVRSRV_STAT_TYPE;

#define SEPARATOR_STR_LEN 166

typedef struct _PVRSRV_STAT_PV_DATA_ {

	PVRSRV_STAT_TYPE eStatType;
	PVRSRV_STATS_PRINT_ELEMENTS* pfnStatsPrintElements;
	IMG_CHAR szLiveStatsHeaderStr[SEPARATOR_STR_LEN + 1];
	IMG_CHAR szRetiredStatsHeaderStr[SEPARATOR_STR_LEN + 1];

} PVRSRV_STAT_PV_DATA;

static PVRSRV_STAT_PV_DATA g_StatPvDataArr[] = {
						{ PVRSRV_STAT_TYPE_PROCESS,  NULL, " Process"               , " Process"               },
						{ PVRSRV_STAT_TYPE_MEMORY,   NULL, " Memory Allocation"     , " Memory Allocation"     },
						{ PVRSRV_STAT_TYPE_RIMEMORY, NULL, " Resource Allocation"   , " Resource Allocation"   },
						{ PVRSRV_STAT_TYPE_CACHEOP,  NULL, " Cache Maintenance Ops" , " Cache Maintenance Ops" }
					      };

#define GET_STAT_ENTRY_ID(STAT_TYPE) &g_StatPvDataArr[(STAT_TYPE)]

/* Generic header strings */
static const IMG_CHAR g_szLiveHeaderStr[]    = " Statistics for LIVE Processes ";
static const IMG_CHAR g_szRetiredHeaderStr[] = " Statistics for RETIRED Processes ";

/* Separator string used for separating stats for different PIDs */
static IMG_CHAR g_szSeparatorStr[SEPARATOR_STR_LEN + 1] = "";

static inline void
_prepareStatsHeaderString(IMG_CHAR *pszStatsSpecificStr, const IMG_CHAR* pszGenericHeaderStr)
{
	IMG_UINT32 ui32NumSeparators;
	IMG_CHAR szStatsHeaderFooterStr[75];

	/* Prepare text content of the header in a local string */
	OSStringLCopy(szStatsHeaderFooterStr, pszStatsSpecificStr, ARRAY_SIZE(szStatsHeaderFooterStr));
	OSStringLCat(szStatsHeaderFooterStr, pszGenericHeaderStr, ARRAY_SIZE(szStatsHeaderFooterStr));

	/* Write all '-' characters to the header string */
	memset(pszStatsSpecificStr, '-', SEPARATOR_STR_LEN);
	pszStatsSpecificStr[SEPARATOR_STR_LEN] = '\0';

	/* Find the spot for text content in the header string */
	ui32NumSeparators = (SEPARATOR_STR_LEN - OSStringLength(szStatsHeaderFooterStr)) >> 1;

	/* Finally write the text content */
	OSSNPrintf(pszStatsSpecificStr + ui32NumSeparators,
		   OSStringLength(szStatsHeaderFooterStr),
		   "%s", szStatsHeaderFooterStr);

	/* Overwrite the '\0' character added by OSSNPrintf() */
	if (OSStringLength(szStatsHeaderFooterStr) > 0)
	{
		pszStatsSpecificStr[ui32NumSeparators + OSStringLength(szStatsHeaderFooterStr) - 1] = ' ';
	}
}

static inline void
_prepareSeparatorStrings(void)
{
	IMG_UINT32 i;

	/* Prepare header strings for each stat type */
	for (i = 0; i < PVRSRV_STAT_TYPE_LAST; ++i)
	{
		_prepareStatsHeaderString(g_StatPvDataArr[i].szLiveStatsHeaderStr, g_szLiveHeaderStr);
		_prepareStatsHeaderString(g_StatPvDataArr[i].szRetiredStatsHeaderStr, g_szRetiredHeaderStr);
	}

	/* Prepare separator string to separate stats for different PIDs */
	memset(g_szSeparatorStr, '-', SEPARATOR_STR_LEN);
	g_szSeparatorStr[SEPARATOR_STR_LEN] = '\0';
}

static inline void
_prepareStatsPrivateData(void)
{
#if defined(PVRSRV_ENABLE_PERPID_STATS)
	g_StatPvDataArr[PVRSRV_STAT_TYPE_PROCESS].pfnStatsPrintElements = ProcessStatsPrintElements;
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	g_StatPvDataArr[PVRSRV_STAT_TYPE_MEMORY].pfnStatsPrintElements = MemStatsPrintElements;
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	g_StatPvDataArr[PVRSRV_STAT_TYPE_RIMEMORY].pfnStatsPrintElements = RIMemStatsPrintElements;
#endif

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
	g_StatPvDataArr[PVRSRV_STAT_TYPE_CACHEOP].pfnStatsPrintElements = CacheOpStatsPrintElements;
#endif

	_prepareSeparatorStrings();
}

#endif

/*
 * Global Boolean to flag when the statistics are ready to monitor
 * memory allocations.
 */
static IMG_BOOL bProcessStatsInitialised = IMG_FALSE;

/*
 * Linked lists for process stats. Live stats are for processes which are still running
 * and the dead list holds those that have exited.
 */
static DLLIST_NODE gsLiveList;
static DLLIST_NODE gsDeadList;

static POS_LOCK g_psLinkedListLock;
/* Lockdep feature in the kernel cannot differentiate between different instances of same lock type.
 * This allows it to group all such instances of the same lock type under one class
 * The consequence of this is that, if lock acquisition is nested on different instances, it generates
 * a false warning message about the possible occurrence of deadlock due to recursive lock acquisition.
 * Hence we create the following sub classes to explicitly appraise Lockdep of such safe lock nesting */
#define PROCESS_LOCK_SUBCLASS_CURRENT	1
#if defined(ENABLE_DEBUGFS_PIDS)
/*
 * Pointer to OS folder to hold PID folders.
 */
static DI_GROUP *psProcStatsDIGroup;
#endif
#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
static DI_ENTRY *psProcStatsDIEntry;
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
/* Global driver PID stats registration handle */
static IMG_HANDLE g_hDriverProcessStats;
#endif

/* Global driver-data folders */
typedef struct _GLOBAL_STATS_
{
	IMG_UINT64 ui64StatValue[PVRSRV_DRIVER_STAT_TYPE_COUNT];
	POS_LOCK   hGlobalStatsLock;
} GLOBAL_STATS;

static DI_ENTRY *psGlobalMemDIEntry;
static GLOBAL_STATS gsGlobalStats;

#define HASH_INITIAL_SIZE 5
/* A hash table used to store the size of any vmalloc'd allocation
 * against its address (not needed for kmallocs as we can use ksize()) */
static HASH_TABLE* gpsSizeTrackingHashTable;
static POS_LOCK	 gpsSizeTrackingHashTableLock;

static PVRSRV_ERROR _RegisterProcess(IMG_HANDLE *phProcessStats, IMG_PID ownerPid);

static void _DestroyProcessStat(PVRSRV_PROCESS_STATS* psProcessStats);

static void _DecreaseProcStatValue(PVRSRV_MEM_ALLOC_TYPE eAllocType,
                                   PVRSRV_PROCESS_STATS* psProcessStats,
                                   IMG_UINT64 uiBytes);

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
	DLLIST_NODE *psNode, *psNext;

	dllist_foreach_node(&gsLiveList, psNode, psNext)
	{
		PVRSRV_PROCESS_STATS* psProcessStats;
		psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);

		if (psProcessStats->pid == pid)
		{
			return psProcessStats;
		}
	}
	return NULL;
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
	DLLIST_NODE *psNode, *psNext;

	dllist_foreach_node(&gsDeadList, psNode, psNext)
	{
		PVRSRV_PROCESS_STATS* psProcessStats;
		psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);

		if (psProcessStats->pid == pid)
		{
			return psProcessStats;
		}
	}
	return NULL;
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
	PVRSRV_PROCESS_STATS* psProcessStats = _FindProcessStatsInLiveList(pid);

	if (psProcessStats == NULL)
	{
		psProcessStats = _FindProcessStatsInDeadList(pid);
	}

	return psProcessStats;
} /* _FindProcessStats */

/*************************************************************************/ /*!
@Function       _CompressMemoryUsage
@Description    Reduces memory usage by deleting old statistics data.
                This function requires that the list lock is not held!
*/ /**************************************************************************/
static void
_CompressMemoryUsage(void)
{
	PVRSRV_PROCESS_STATS* psProcessStatsToBeFreed;
	IMG_INT32 i32ItemsRemaining;
	DLLIST_NODE *psNode, *psNext;
	DLLIST_NODE sToBeFreedHead;

	/*
	 * We hold the lock whilst checking the list, but we'll release it
	 * before freeing memory (as that will require the lock too)!
	 */
	OSLockAcquire(g_psLinkedListLock);

	/* Check that the dead list is not bigger than the max size... */
	psProcessStatsToBeFreed = NULL;
	i32ItemsRemaining      = MAX_DEAD_LIST_PROCESSES;

	dllist_init(&sToBeFreedHead);

	dllist_foreach_node(&gsDeadList, psNode, psNext)
	{
		i32ItemsRemaining--;
		if (i32ItemsRemaining < 0)
		{
			/* This is the last allowed process, cut the linked list here! */
			dllist_remove_node(psNode);
			dllist_add_to_tail(&sToBeFreedHead, psNode);
		}
	}

	OSLockRelease(g_psLinkedListLock);

	dllist_foreach_node(&sToBeFreedHead, psNode, psNext)
	{
		psProcessStatsToBeFreed = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
		_DestroyProcessStat(psProcessStatsToBeFreed);
	}
} /* _CompressMemoryUsage */

/* These functions move the process stats from the live to the dead list.
 * _MoveProcessToDeadList moves the entry in the global lists and
 * it needs to be protected by g_psLinkedListLock.
 * _MoveProcessToDeadList performs the OS calls and it
 * shouldn't be used under g_psLinkedListLock because this could generate a
 * lockdep warning. */
static void
_MoveProcessToDeadList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Take the element out of the live list and append to the dead list... */
	PVR_ASSERT(psProcessStats != NULL);
	dllist_remove_node(&psProcessStats->sNode);
	dllist_add_to_head(&gsDeadList, &psProcessStats->sNode);
} /* _MoveProcessToDeadList */

/* These functions move the process stats from the dead to the live list.
 * _MoveProcessToLiveList moves the entry in the global lists and
 * it needs to be protected by g_psLinkedListLock.
 * _MoveProcessToLiveList performs the OS calls and it
 * shouldn't be used under g_psLinkedListLock because this could generate a
 * lockdep warning. */
static void
_MoveProcessToLiveList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Take the element out of the live list and append to the dead list... */
	PVR_ASSERT(psProcessStats != NULL);
	dllist_remove_node(&psProcessStats->sNode);
	dllist_add_to_head(&gsLiveList, &psProcessStats->sNode);
} /* _MoveProcessToLiveList */

static PVRSRV_ERROR
_AllocateProcessStats(PVRSRV_PROCESS_STATS **ppsProcessStats, IMG_PID ownerPid)
{
	PVRSRV_ERROR eError;
	PVRSRV_PROCESS_STATS *psProcessStats;
	PVRSRV_DATA	*psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_UINT32 ui32DevCount = 0;

	if (psPVRSRVData != NULL)
	{
		ui32DevCount = psPVRSRVData->ui32RegisteredDevices;
	}

	psProcessStats = OSAllocZMemNoStats(sizeof(PVRSRV_PROCESS_STATS) +
	                                    ui32DevCount * PVRSRV_DEVICE_STAT_TYPE_COUNT * sizeof(IMG_INT32));
	PVR_RETURN_IF_NOMEM(psProcessStats);

	psProcessStats->pid             = ownerPid;
	psProcessStats->ui32RefCount    = 1;
	psProcessStats->ui32DevCount    = ui32DevCount;
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	psProcessStats->psMemoryRecords = HASH_Create(HASH_INITIAL_SIZE);
	PVR_GOTO_IF_NOMEM(psProcessStats->psMemoryRecords, eError, free_process_stats);
#endif

	eError = OSLockCreateNoStats(&psProcessStats->hLock);
	PVR_GOTO_IF_ERROR(eError, destroy_mem_recs);

	*ppsProcessStats = psProcessStats;
	return PVRSRV_OK;

destroy_mem_recs:
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	HASH_Delete(psProcessStats->psMemoryRecords);
free_process_stats:
#endif
	OSFreeMemNoStats(psProcessStats);
	return PVRSRV_ERROR_OUT_OF_MEMORY;
}

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
static PVRSRV_ERROR _FreeMemStatsEntry(uintptr_t k, uintptr_t v, void* pvPriv)
{
	PVRSRV_MEM_ALLOC_REC *psRecord = (PVRSRV_MEM_ALLOC_REC *)(uintptr_t)v;

	PVR_UNREFERENCED_PARAMETER(pvPriv);

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
	PVR_DPF((PVR_DBG_WARNING, "Mem Stats Record not freed: 0x%" IMG_UINT64_FMTSPECx " %p, size="IMG_SIZE_FMTSPEC", %s:%d",
			 (IMG_UINT64)(k), psRecord, psRecord->uiBytes,
			 (IMG_CHAR*)psRecord->pvAllocdFromFile, psRecord->ui32AllocdFromLine));
#else
	PVR_UNREFERENCED_PARAMETER(k);
#endif
	OSFreeMemNoStats(psRecord);

	return PVRSRV_OK;
}
#endif

/*************************************************************************/ /*!
@Function       _DestroyProcessStat
@Description    Frees memory and resources held by a process statistic.
@Input          psProcessStats  Process stats to destroy.
*/ /**************************************************************************/
static void
_DestroyProcessStat(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != NULL);

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	/* Free the memory statistics... */
	HASH_Iterate(psProcessStats->psMemoryRecords, (HASH_pfnCallback)_FreeMemStatsEntry, NULL);
	HASH_Delete(psProcessStats->psMemoryRecords);
#endif
	OSLockRelease(psProcessStats->hLock);

	/*Destroy the lock */
	OSLockDestroyNoStats(psProcessStats->hLock);

	/* Free the memory... */
	OSFreeMemNoStats(psProcessStats);
} /* _DestroyProcessStat */

#if defined(ENABLE_DEBUGFS_PIDS)
static inline void
_createStatsFiles(PVRSRV_OS_STAT_ENTRY* psStatsEntries,
                  DI_PFN_SHOW pfnStatsShow)
{
	PVRSRV_ERROR eError;
	DI_ITERATOR_CB sIterator = {.pfnShow = pfnStatsShow};

#if defined(PVRSRV_ENABLE_PERPID_STATS)
	eError = DICreateEntry("process_stats", psStatsEntries->psStatsDIGroup,
	                       &sIterator,
	                       GET_STAT_ENTRY_ID(PVRSRV_STAT_TYPE_PROCESS),
	                       DI_ENTRY_TYPE_GENERIC,
	                       &psStatsEntries->psProcessStatsDIEntry);
	PVR_LOG_IF_ERROR(eError, "DICreateEntry (1)");
#endif

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
	eError = DICreateEntry("cache_ops_exec", psStatsEntries->psStatsDIGroup,
	                       &sIterator,
	                       GET_STAT_ENTRY_ID(PVRSRV_STAT_TYPE_CACHEOP),
	                       DI_ENTRY_TYPE_GENERIC,
	                       &psStatsEntries->psCacheOpStatsDIEntry);
	PVR_LOG_IF_ERROR(eError, "DICreateEntry (2)");
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	eError = DICreateEntry("mem_area", psStatsEntries->psStatsDIGroup,
	                       &sIterator,
	                       GET_STAT_ENTRY_ID(PVRSRV_STAT_TYPE_MEMORY),
	                       DI_ENTRY_TYPE_GENERIC,
	                       &psStatsEntries->psMemStatsDIEntry);
	PVR_LOG_IF_ERROR(eError, "DICreateEntry (3)");
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	eError = DICreateEntry("gpu_mem_area", psStatsEntries->psStatsDIGroup,
	                       &sIterator,
	                       GET_STAT_ENTRY_ID(PVRSRV_STAT_TYPE_RIMEMORY),
	                       DI_ENTRY_TYPE_GENERIC,
	                       &psStatsEntries->psRIMemStatsDIEntry);
	PVR_LOG_IF_ERROR(eError, "DICreateEntry (4)");
#endif
}

static inline void
_createStatisticsEntries(void)
{
	PVRSRV_ERROR eError;

	eError = DICreateGroup("proc_stats", NULL, &psProcStatsDIGroup);
	PVR_LOG_IF_ERROR(eError, "DICreateGroup (1)");
	eError = DICreateGroup("live_pids_stats", psProcStatsDIGroup,
                           &gsLiveStatEntries.psStatsDIGroup);
	PVR_LOG_IF_ERROR(eError, "DICreateGroup (2)");
	eError = DICreateGroup("retired_pids_stats", psProcStatsDIGroup,
                           &gsRetiredStatEntries.psStatsDIGroup);
	PVR_LOG_IF_ERROR(eError, "DICreateGroup (3)");

	_createStatsFiles(&gsLiveStatEntries, GenericStatsPrintElementsLive);
	_createStatsFiles(&gsRetiredStatEntries, GenericStatsPrintElementsRetired);

	_prepareStatsPrivateData();
}

static inline void
_removeStatsFiles(PVRSRV_OS_STAT_ENTRY* psStatsEntries)
{
#if defined(PVRSRV_ENABLE_PERPID_STATS)
	DIDestroyEntry(psStatsEntries->psProcessStatsDIEntry);
	psStatsEntries->psProcessStatsDIEntry = NULL;
#endif

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
	DIDestroyEntry(psStatsEntries->psCacheOpStatsDIEntry);
    psStatsEntries->psCacheOpStatsDIEntry = NULL;
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	DIDestroyEntry(psStatsEntries->psMemStatsDIEntry);
	psStatsEntries->psMemStatsDIEntry = NULL;
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	DIDestroyEntry(psStatsEntries->psRIMemStatsDIEntry);
	psStatsEntries->psRIMemStatsDIEntry = NULL;
#endif
}

static inline void
_removeStatisticsEntries(void)
{
	_removeStatsFiles(&gsLiveStatEntries);
	_removeStatsFiles(&gsRetiredStatEntries);

	DIDestroyGroup(gsLiveStatEntries.psStatsDIGroup);
	gsLiveStatEntries.psStatsDIGroup = NULL;
	DIDestroyGroup(gsRetiredStatEntries.psStatsDIGroup);
	gsRetiredStatEntries.psStatsDIGroup = NULL;
	DIDestroyGroup(psProcStatsDIGroup);
	psProcStatsDIGroup = NULL;
}
#endif

/*************************************************************************/ /*!
@Function       PVRSRVStatsInitialise
@Description    Entry point for initialising the statistics module.
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVStatsInitialise(void)
{
	PVRSRV_ERROR error;

	PVR_ASSERT(g_psLinkedListLock == NULL);
	PVR_ASSERT(gpsSizeTrackingHashTable == NULL);
	PVR_ASSERT(bProcessStatsInitialised == IMG_FALSE);

	/* We need a lock to protect the linked lists... */
#if defined(__linux__) && defined(__KERNEL__)
	error = OSLockCreateNoStats(&g_psLinkedListLock);
#else
	error = OSLockCreate(&g_psLinkedListLock);
#endif
	PVR_GOTO_IF_ERROR(error, return_);

	/* We also need a lock to protect the hash table used for size tracking. */
#if defined(__linux__) && defined(__KERNEL__)
	error = OSLockCreateNoStats(&gpsSizeTrackingHashTableLock);
#else
	error = OSLockCreate(&gpsSizeTrackingHashTableLock);
#endif
	PVR_GOTO_IF_ERROR(error, destroy_linked_list_lock_);

	/* We also need a lock to protect the GlobalStat counters */
#if defined(__linux__) && defined(__KERNEL__)
	error = OSLockCreateNoStats(&gsGlobalStats.hGlobalStatsLock);
#else
	error = OSLockCreate(&gsGlobalStats.hGlobalStatsLock);
#endif
	PVR_GOTO_IF_ERROR(error, destroy_hashtable_lock_);

	/* Flag that we are ready to start monitoring memory allocations. */

	gpsSizeTrackingHashTable = HASH_Create(HASH_INITIAL_SIZE);
	PVR_GOTO_IF_NOMEM(gpsSizeTrackingHashTable, error, destroy_stats_lock_);

	dllist_init(&gsLiveList);
	dllist_init(&gsDeadList);

	bProcessStatsInitialised = IMG_TRUE;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	/* Register our 'system' PID to hold driver-wide alloc stats */
	_RegisterProcess(&g_hDriverProcessStats, PVR_SYS_ALLOC_PID);
#endif

#if defined(ENABLE_DEBUGFS_PIDS)
	_createStatisticsEntries();
#endif

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
	{
		DI_ITERATOR_CB sIterator = {.pfnShow = RawProcessStatsPrintElements};
		error = DICreateEntry("memtrack_stats", NULL, &sIterator, NULL,
		                       DI_ENTRY_TYPE_GENERIC, &psProcStatsDIEntry);
		PVR_LOG_IF_ERROR(error, "DICreateEntry (1)");
	}
#endif

	{
		DI_ITERATOR_CB sIterator = {.pfnShow = GlobalStatsPrintElements};
		error = DICreateEntry("driver_stats", NULL, &sIterator, NULL,
		                      DI_ENTRY_TYPE_GENERIC, &psGlobalMemDIEntry);
		PVR_LOG_IF_ERROR(error, "DICreateEntry (3)");
	}

	return PVRSRV_OK;

destroy_stats_lock_:
#if defined(__linux__) && defined(__KERNEL__)
	OSLockDestroyNoStats(gsGlobalStats.hGlobalStatsLock);
#else
	OSLockDestroy(gsGlobalStats.hGlobalStatsLock);
#endif
	gsGlobalStats.hGlobalStatsLock = NULL;
destroy_hashtable_lock_:
#if defined(__linux__) && defined(__KERNEL__)
	OSLockDestroyNoStats(gpsSizeTrackingHashTableLock);
#else
	OSLockDestroy(gpsSizeTrackingHashTableLock);
#endif
	gpsSizeTrackingHashTableLock = NULL;
destroy_linked_list_lock_:
#if defined(__linux__) && defined(__KERNEL__)
	OSLockDestroyNoStats(g_psLinkedListLock);
#else
	OSLockDestroy(g_psLinkedListLock);
#endif
	g_psLinkedListLock = NULL;
return_:
	return error;

}

static PVRSRV_ERROR _DumpAllVMallocEntries (uintptr_t k, uintptr_t v, void* pvPriv)
{
#if defined(PVRSRV_NEED_PVR_DPF) || defined(DOXYGEN)
	_PVR_STATS_TRACKING_HASH_ENTRY *psNewTrackingHashEntry = (_PVR_STATS_TRACKING_HASH_ENTRY *)(uintptr_t)v;
	IMG_UINT64 uiCpuVAddr = (IMG_UINT64)k;

	PVR_DPF((PVR_DBG_ERROR, "%s: " IMG_SIZE_FMTSPEC " bytes @ 0x%" IMG_UINT64_FMTSPECx " (PID %u)", __func__,
	         psNewTrackingHashEntry->uiSizeInBytes,
	         uiCpuVAddr,
	         psNewTrackingHashEntry->uiPid));

	PVR_UNREFERENCED_PARAMETER(pvPriv);
#endif
	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       PVRSRVStatsDestroy
@Description    Method for destroying the statistics module data.
*/ /**************************************************************************/
void
PVRSRVStatsDestroy(void)
{
	DLLIST_NODE *psNode, *psNext;

	PVR_ASSERT(bProcessStatsInitialised);

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
	if (psProcStatsDIEntry != NULL)
	{
		DIDestroyEntry(psProcStatsDIEntry);
		psProcStatsDIEntry = NULL;
	}
#endif

	/* Destroy the global data entry */
	if (psGlobalMemDIEntry!=NULL)
	{
		DIDestroyEntry(psGlobalMemDIEntry);
		psGlobalMemDIEntry = NULL;
	}

#if defined(ENABLE_DEBUGFS_PIDS)
	_removeStatisticsEntries();
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	/* Deregister our 'system' PID which holds driver-wide alloc stats */
	PVRSRVStatsDeregisterProcess(g_hDriverProcessStats);
#endif

	/* Stop monitoring memory allocations... */
	bProcessStatsInitialised = IMG_FALSE;

	/* Destroy the locks... */
	if (g_psLinkedListLock != NULL)
	{
#if defined(__linux__) && defined(__KERNEL__)
		OSLockDestroyNoStats(g_psLinkedListLock);
#else
		OSLockDestroy(g_psLinkedListLock);
#endif
		g_psLinkedListLock = NULL;
	}

	/* Free the live and dead lists... */
	dllist_foreach_node(&gsLiveList, psNode, psNext)
	{
		PVRSRV_PROCESS_STATS* psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
		dllist_remove_node(&psProcessStats->sNode);
		_DestroyProcessStat(psProcessStats);
	}

	dllist_foreach_node(&gsDeadList, psNode, psNext)
	{
		PVRSRV_PROCESS_STATS* psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
		dllist_remove_node(&psProcessStats->sNode);
		_DestroyProcessStat(psProcessStats);
	}

	if (gpsSizeTrackingHashTable != NULL)
	{
		/* Dump all remaining entries in HASH table (list any remaining vmallocs) */
		HASH_Iterate(gpsSizeTrackingHashTable, (HASH_pfnCallback)_DumpAllVMallocEntries, NULL);
		HASH_Delete(gpsSizeTrackingHashTable);
	}
	if (gpsSizeTrackingHashTableLock != NULL)
	{
#if defined(__linux__) && defined(__KERNEL__)
		OSLockDestroyNoStats(gpsSizeTrackingHashTableLock);
#else
		OSLockDestroy(gpsSizeTrackingHashTableLock);
#endif
		gpsSizeTrackingHashTableLock = NULL;
	}

	if (NULL != gsGlobalStats.hGlobalStatsLock)
	{
#if defined(__linux__) && defined(__KERNEL__)
		OSLockDestroyNoStats(gsGlobalStats.hGlobalStatsLock);
#else
		OSLockDestroy(gsGlobalStats.hGlobalStatsLock);
#endif
		gsGlobalStats.hGlobalStatsLock = NULL;
	}

}

static void _decrease_global_stat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								  size_t uiBytes)
{
#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	IMG_UINT64 ui64InitialSize;
#endif

	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	ui64InitialSize = GET_GPUMEM_GLOBAL_STAT_VALUE();
#endif

	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_KMALLOC, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_VMALLOC, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_VMAP_PT_UMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_IOREMAP_PT_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_MAPPED_GPUMEM_UMA_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_UMA_POOL_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_POOL, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT, uiBytes);
			break;

		default:
			PVR_ASSERT(0);
			break;
	}

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	{
		IMG_UINT64 ui64Size = GET_GPUMEM_GLOBAL_STAT_VALUE();
		if (ui64Size != ui64InitialSize)
		{
			TracepointUpdateGPUMemGlobal(0, ui64Size);
		}
	}
#endif

	OSLockRelease(gsGlobalStats.hGlobalStatsLock);
}

static void _increase_global_stat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								  size_t uiBytes)
{
#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	IMG_UINT64 ui64InitialSize;
#endif

	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	ui64InitialSize = GET_GPUMEM_GLOBAL_STAT_VALUE();
#endif

	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_KMALLOC, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_VMALLOC, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_VMAP_PT_UMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_IOREMAP_PT_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_MAPPED_GPUMEM_UMA_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_UMA_POOL_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA_POOL, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats, PVRSRV_DRIVER_STAT_TYPE_DMA_BUF_IMPORT, uiBytes);
			break;

		default:
			PVR_ASSERT(0);
			break;
	}

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	{
		IMG_UINT64 ui64Size = GET_GPUMEM_GLOBAL_STAT_VALUE();
		if (ui64Size != ui64InitialSize)
		{
			TracepointUpdateGPUMemGlobal(0, ui64Size);
		}
	}
#endif

	OSLockRelease(gsGlobalStats.hGlobalStatsLock);
}

static PVRSRV_ERROR
_RegisterProcess(IMG_HANDLE *phProcessStats, IMG_PID ownerPid)
{
	PVRSRV_PROCESS_STATS*	psProcessStats=NULL;
	PVRSRV_ERROR			eError;

	PVR_ASSERT(phProcessStats != NULL);

	PVR_DPF((PVR_DBG_MESSAGE, "%s: Register process PID %d [%s]",
			__func__, ownerPid, (ownerPid == PVR_SYS_ALLOC_PID)
			? "system" : OSGetCurrentClientProcessNameKM()));

	/* Check the PID has not already moved to the dead list... */
	OSLockAcquire(g_psLinkedListLock);
	psProcessStats = _FindProcessStatsInDeadList(ownerPid);
	if (psProcessStats != NULL)
	{
		/* Move it back onto the live list! */
		_MoveProcessToLiveList(psProcessStats);
	}
	else
	{
		/* Check the PID is not already registered in the live list... */
		psProcessStats = _FindProcessStatsInLiveList(ownerPid);
	}

	/* If the PID is on the live list then just increment the ref count and return... */
	if (psProcessStats != NULL)
	{
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

		psProcessStats->ui32RefCount++;

		OSLockRelease(psProcessStats->hLock);
		OSLockRelease(g_psLinkedListLock);

		*phProcessStats = psProcessStats;

		return PVRSRV_OK;
	}
	OSLockRelease(g_psLinkedListLock);

	/* Allocate a new node structure and initialise it... */
	eError = _AllocateProcessStats(&psProcessStats, ownerPid);
	PVR_GOTO_IF_ERROR(eError, e0);

	/* Add it to the live list... */
	OSLockAcquire(g_psLinkedListLock);
	dllist_add_to_head(&gsLiveList, &psProcessStats->sNode);
	OSLockRelease(g_psLinkedListLock);

	/* Done */
	*phProcessStats = (IMG_HANDLE) psProcessStats;

	return PVRSRV_OK;

e0:
	*phProcessStats = (IMG_HANDLE) NULL;
	return PVRSRV_ERROR_OUT_OF_MEMORY;
} /* _RegisterProcess */

/*************************************************************************/ /*!
@Function       PVRSRVStatsRegisterProcess
@Description    Register a process into the list statistics list.
@Output         phProcessStats  Handle to the process to be used to deregister.
@Return         Standard PVRSRV_ERROR error code.
*/ /**************************************************************************/
PVRSRV_ERROR
PVRSRVStatsRegisterProcess(IMG_HANDLE* phProcessStats)
{
	return _RegisterProcess(phProcessStats, OSGetCurrentClientProcessIDKM());
}

/*************************************************************************/ /*!
@Function       PVRSRVStatsDeregisterProcess
@Input          hProcessStats  Handle to the process returned when registered.
@Description    Method for destroying the statistics module data.
*/ /**************************************************************************/
void
PVRSRVStatsDeregisterProcess(IMG_HANDLE hProcessStats)
{
	PVR_DPF((PVR_DBG_MESSAGE, "%s: Deregister process entered PID %d [%s]",
			__func__, OSGetCurrentClientProcessIDKM(),
			OSGetCurrentProcessName()));

	if (hProcessStats != (IMG_HANDLE) NULL)
	{
		PVRSRV_PROCESS_STATS* psProcessStats = (PVRSRV_PROCESS_STATS*) hProcessStats;

		/* Lower the reference count, if zero then move it to the dead list */
		OSLockAcquire(g_psLinkedListLock);
		if (psProcessStats->ui32RefCount > 0)
		{
			OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
			psProcessStats->ui32RefCount--;

#if !defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
			if (psProcessStats->ui32RefCount == 0)
			{
				OSLockRelease(psProcessStats->hLock);
				_MoveProcessToDeadList(psProcessStats);
			}else
#endif
			{
				OSLockRelease(psProcessStats->hLock);
			}
		}
		OSLockRelease(g_psLinkedListLock);

		/* Check if the dead list needs to be reduced */
		_CompressMemoryUsage();
	}
} /* PVRSRVStatsDeregisterProcess */

PVRSRV_ERROR PVRSRVStatsDeviceConnect(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32 ui32DevID = psDeviceNode->sDevId.ui32InternalID;
	IMG_PID ownerPid = OSGetCurrentClientProcessIDKM();
	PVRSRV_PROCESS_STATS*	psProcessStats;

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStatsInLiveList(ownerPid);

	if (psProcessStats != NULL)
	{
		if (ui32DevID < psProcessStats->ui32DevCount)
		{
			psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_CONNECTIONS]++;
			UPDATE_MAX_VALUE(psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_MAX_CONNECTIONS],
							 psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_CONNECTIONS]);

		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Device index %d is greater than device count %d for PID %d.",
					 __func__, ui32DevID, psProcessStats->ui32DevCount, ownerPid));
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Process %d not found.",
				 __func__, ownerPid));
	}

	OSLockRelease(g_psLinkedListLock);

	return PVRSRV_OK;
}

void PVRSRVStatsDeviceDisconnect(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	IMG_UINT32 ui32DevID = psDeviceNode->sDevId.ui32InternalID;
	IMG_PID	currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA* psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_PID currentPid = OSGetCurrentClientProcessIDKM();
	PVRSRV_PROCESS_STATS*	psProcessStats;

	OSLockAcquire(g_psLinkedListLock);

	if (psPVRSRVData)
	{
		if ((currentPid == psPVRSRVData->cleanupThreadPid) &&
		    (currentCleanupPid != 0))
		{
			psProcessStats = _FindProcessStats(currentCleanupPid);
		}
		else
		{
			psProcessStats = _FindProcessStatsInLiveList(currentPid);
		}
	}
	else
	{
		psProcessStats = _FindProcessStatsInLiveList(currentPid);
	}

	if (psProcessStats != NULL)
	{
		if (ui32DevID < psProcessStats->ui32DevCount)
		{
			psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_CONNECTIONS]--;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Device index %d is greater than device count %d for PID %d.",
					 __func__, ui32DevID, psProcessStats->ui32DevCount, currentPid));
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Process %d not found.",
				 __func__, currentPid));
	}

	OSLockRelease(g_psLinkedListLock);
}

void
PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
							 void *pvCpuVAddr,
							 IMG_CPU_PHYADDR sCpuPAddr,
							 size_t uiBytes,
							 IMG_PID currentPid
							 DEBUG_MEMSTATS_PARAMS)
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_MEM_ALLOC_REC*  psRecord = NULL;
	PVRSRV_PROCESS_STATS*  psProcessStats;
	__maybe_unused PVRSRV_PROC_SEARCH_STATE eProcSearch = PVRSRV_PROC_FOUND;
#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	IMG_UINT64 ui64InitialSize;
#endif

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Called when process statistics module is not initialised",
				 __func__));
#endif
		return;
	}

	/*
	 * To prevent a recursive loop, we make the memory allocations for our
	 * memstat records via OSAllocMemNoStats(), which does not try to
	 * create a memstat record entry.
	 */

	/* Allocate the memory record... */
	psRecord = OSAllocZMemNoStats(sizeof(PVRSRV_MEM_ALLOC_REC));
	if (psRecord == NULL)
	{
		return;
	}

	psRecord->eAllocType       = eAllocType;
	psRecord->pvCpuVAddr       = pvCpuVAddr;
	psRecord->sCpuPAddr.uiAddr = sCpuPAddr.uiAddr;
	psRecord->uiBytes          = uiBytes;

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
	psRecord->pvAllocdFromFile = pvAllocFromFile;
	psRecord->ui32AllocdFromLine = ui32AllocFromLine;
#endif

	_increase_global_stat(eAllocType, uiBytes);
	/* Lock while we find the correct process... */
	OSLockAcquire(g_psLinkedListLock);

	if (psPVRSRVData)
	{
		if ((currentPid == psPVRSRVData->cleanupThreadPid) &&
		    (currentCleanupPid != 0))
		{
			psProcessStats = _FindProcessStats(currentCleanupPid);
		}
		else
		{
			psProcessStats = _FindProcessStatsInLiveList(currentPid);
			if (!psProcessStats)
			{
				psProcessStats = _FindProcessStatsInDeadList(currentPid);
				eProcSearch = PVRSRV_PROC_RESURRECTED;
			}
		}
	}
	else
	{
		psProcessStats = _FindProcessStatsInLiveList(currentPid);
		if (!psProcessStats)
		{
			psProcessStats = _FindProcessStatsInDeadList(currentPid);
			eProcSearch = PVRSRV_PROC_RESURRECTED;
		}
	}

	if (psProcessStats == NULL)
	{
		eProcSearch = PVRSRV_PROC_NOTFOUND;

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: Process stat increment called for 'unknown' process PID(%d)",
				 __func__, currentPid));

		if (_AllocateProcessStats(&psProcessStats, currentPid) != PVRSRV_OK)
		{
			OSLockRelease(g_psLinkedListLock);
			PVR_DPF((PVR_DBG_ERROR,
			        "%s UNABLE TO CREATE process_stats entry for pid %d [%s] (" IMG_SIZE_FMTSPEC " bytes)",
			        __func__, currentPid, OSGetCurrentProcessName(), uiBytes));
			goto free_record;
		}

		/* Add it to the live list... */
		dllist_add_to_head(&gsLiveList, &psProcessStats->sNode);

		OSLockRelease(g_psLinkedListLock);

#else  /* defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */
		OSLockRelease(g_psLinkedListLock);
		goto free_record;
#endif /* defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */
	}
	else
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		if (eProcSearch == PVRSRV_PROC_RESURRECTED)
		{
			PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: Process stat incremented on 'dead' process PID(%d)",
				 __func__, currentPid));
			/* Move process from dead list to live list */
			_MoveProcessToLiveList(psProcessStats);
		}
#endif
		OSLockRelease(g_psLinkedListLock);
	}

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	{
		IMG_UINT64 ui64Key;

		if (eAllocType == PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA ||
			eAllocType == PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES ||
			eAllocType == PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES)
		{
			ui64Key = psRecord->sCpuPAddr.uiAddr;
		}
		else
		{
			ui64Key = (IMG_UINT64)psRecord->pvCpuVAddr;
		}

		/* Insert the memory record... */
		if (!HASH_Insert(psProcessStats->psMemoryRecords, ui64Key, (uintptr_t)psRecord))
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s UNABLE TO CREATE mem stats record for pid %d [%s] (" IMG_SIZE_FMTSPEC " bytes)",
					 __func__, currentPid, OSGetCurrentProcessName(), uiBytes));
		}
	}
#endif

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	ui64InitialSize = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
#endif

	/* Update the memory watermarks... */
	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
		{
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		default:
		{
			PVR_ASSERT(0);
		}
		break;
	}

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	if (psProcessStats->pid != PVR_SYS_ALLOC_PID)
	{
		IMG_UINT64 ui64Size = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
		if (ui64Size != ui64InitialSize)
		{
			TracepointUpdateGPUMemPerProcess(0, psProcessStats->pid, ui64Size);
		}
	}
#endif

	OSLockRelease(psProcessStats->hLock);

	return;

free_record:
	_decrease_global_stat(eAllocType, uiBytes);
	if (psRecord != NULL)
	{
		OSFreeMemNoStats(psRecord);
	}
#endif /* defined(PVRSRV_ENABLE_MEMORY_STATS) */
} /* PVRSRVStatsAddMemAllocRecord */

void
PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								IMG_UINT64 ui64Key,
								IMG_PID currentPid)
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_PROCESS_STATS*  psProcessStats = NULL;
	PVRSRV_MEM_ALLOC_REC*  psRecord		  = NULL;
	IMG_BOOL			   bFound	      = IMG_FALSE;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Called when process statistics module is not initialised",
				 __func__));
#endif
		return;
	}

	/* Lock while we find the correct process and remove this record... */
	OSLockAcquire(g_psLinkedListLock);

	if (psPVRSRVData)
	{
		if ((currentPid == psPVRSRVData->cleanupThreadPid) &&
		    (currentCleanupPid != 0))
		{
			psProcessStats = _FindProcessStats(currentCleanupPid);
		}
		else
		{
			psProcessStats = _FindProcessStats(currentPid);
		}
	}
	else
	{
		psProcessStats = _FindProcessStats(currentPid);
	}
	if (psProcessStats != NULL)
	{
		psRecord = (PVRSRV_MEM_ALLOC_REC*)HASH_Remove(psProcessStats->psMemoryRecords, ui64Key);
		bFound = psRecord != NULL;
	}

	/* If not found, we need to do a full search in case it was allocated to a different PID... */
	if (!bFound)
	{
		PVRSRV_PROCESS_STATS* psProcessStatsAlreadyChecked = psProcessStats;
		DLLIST_NODE *psNode, *psNext;

		/* Search all live lists first... */
		dllist_foreach_node(&gsLiveList, psNode, psNext)
		{
			psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
			if (psProcessStats != psProcessStatsAlreadyChecked)
			{
				psRecord = (PVRSRV_MEM_ALLOC_REC*)HASH_Remove(psProcessStats->psMemoryRecords, ui64Key);
				bFound = psRecord != NULL;
			}

			if (bFound)
			{
				break;
			}
		}

		/* If not found, then search all dead lists next... */
		if (!bFound)
		{
			dllist_foreach_node(&gsDeadList, psNode, psNext)
			{
				psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
				if (psProcessStats != psProcessStatsAlreadyChecked)
				{
					psRecord = (PVRSRV_MEM_ALLOC_REC*)HASH_Remove(psProcessStats->psMemoryRecords, ui64Key);
					bFound = psRecord != NULL;
				}

				if (bFound)
				{
					break;
				}
			}
		}
	}

	/* Update the watermark and remove this record...*/
	if (bFound)
	{
		_decrease_global_stat(eAllocType, psRecord->uiBytes);

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

		_DecreaseProcStatValue(eAllocType,
		                       psProcessStats,
		                       psRecord->uiBytes);

		OSLockRelease(psProcessStats->hLock);
		OSLockRelease(g_psLinkedListLock);

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		/* If all stats are now zero, remove the entry for this thread */
		if (psProcessStats->ui32StatAllocFlags == 0)
		{
			OSLockAcquire(g_psLinkedListLock);
			_MoveProcessToDeadList(psProcessStats);
			OSLockRelease(g_psLinkedListLock);

			/* Check if the dead list needs to be reduced */
			_CompressMemoryUsage();
		}
#endif
		/*
		 * Free the record outside the lock so we don't deadlock and so we
		 * reduce the time the lock is held.
		 */
		OSFreeMemNoStats(psRecord);
	}
	else
	{
		OSLockRelease(g_psLinkedListLock);
	}

#else
PVR_UNREFERENCED_PARAMETER(eAllocType);
PVR_UNREFERENCED_PARAMETER(ui64Key);
#endif
} /* PVRSRVStatsRemoveMemAllocRecord */

void
PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE eAllocType,
									size_t uiBytes,
									IMG_UINT64 uiCpuVAddr,
									IMG_PID uiPid)
{
	IMG_BOOL bRes = IMG_FALSE;
	_PVR_STATS_TRACKING_HASH_ENTRY *psNewTrackingHashEntry = NULL;

	if (!bProcessStatsInitialised || (gpsSizeTrackingHashTable == NULL))
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Called when process statistics module is not initialised",
				 __func__));
#endif
		return;
	}

	/* Alloc untracked memory for the new hash table entry */
	psNewTrackingHashEntry = (_PVR_STATS_TRACKING_HASH_ENTRY *)OSAllocMemNoStats(sizeof(*psNewTrackingHashEntry));
	if (psNewTrackingHashEntry == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "*** %s : @ line %d Failed to alloc memory for psNewTrackingHashEntry!",
				 __func__, __LINE__));
		return;
	}

	/* Fill-in the size of the allocation and PID of the allocating process */
	psNewTrackingHashEntry->uiSizeInBytes = uiBytes;
	psNewTrackingHashEntry->uiPid = uiPid;
	OSLockAcquire(gpsSizeTrackingHashTableLock);
	/* Insert address of the new struct into the hash table */
	bRes = HASH_Insert(gpsSizeTrackingHashTable, uiCpuVAddr, (uintptr_t)psNewTrackingHashEntry);
	OSLockRelease(gpsSizeTrackingHashTableLock);
	if (bRes)
	{
		PVRSRVStatsIncrMemAllocStat(eAllocType, uiBytes, uiPid);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "*** %s : @ line %d HASH_Insert() failed!",
				 __func__, __LINE__));
		/* Free the memory allocated for psNewTrackingHashEntry, as we
		 * failed to insert it into the Hash table.
		 */
		OSFreeMemNoStats(psNewTrackingHashEntry);
	}
}

void
PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
                            size_t uiBytes,
                            IMG_PID currentPid)

{
	IMG_PID				  currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		  psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_PROCESS_STATS* psProcessStats = NULL;
	__maybe_unused PVRSRV_PROC_SEARCH_STATE eProcSearch = PVRSRV_PROC_FOUND;
#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	IMG_UINT64 ui64InitialSize;
#endif

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_WARNING,
				 "%s: Called when process statistics module is not initialised",
				 __func__));
#endif
		return;
	}

	_increase_global_stat(eAllocType, uiBytes);
	OSLockAcquire(g_psLinkedListLock);
	if (psPVRSRVData)
	{
		if ((currentPid == psPVRSRVData->cleanupThreadPid) &&
		    (currentCleanupPid != 0))
		{
			psProcessStats = _FindProcessStats(currentCleanupPid);
		}
		else
		{
			psProcessStats = _FindProcessStatsInLiveList(currentPid);
			if (!psProcessStats)
			{
				psProcessStats = _FindProcessStatsInDeadList(currentPid);
				eProcSearch = PVRSRV_PROC_RESURRECTED;
			}
		}
	}
	else
	{
		psProcessStats = _FindProcessStatsInLiveList(currentPid);
		if (!psProcessStats)
		{
			psProcessStats = _FindProcessStatsInDeadList(currentPid);
			eProcSearch = PVRSRV_PROC_RESURRECTED;
		}
	}

	if (psProcessStats == NULL)
	{
		eProcSearch = PVRSRV_PROC_NOTFOUND;

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_MESSAGE,
				 "%s: Process stat increment called for 'unknown' process PID(%d)",
				 __func__, currentPid));

		if (bProcessStatsInitialised)
		{
			if (_AllocateProcessStats(&psProcessStats, currentPid) != PVRSRV_OK)
			{
				OSLockRelease(g_psLinkedListLock);
				return;
			}
			/* Add it to the live list... */
			dllist_add_to_head(&gsLiveList, &psProcessStats->sNode);
		}
#else
		OSLockRelease(g_psLinkedListLock);
#endif /* defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */

	}

	if (psProcessStats != NULL)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		if (eProcSearch == PVRSRV_PROC_RESURRECTED)
		{
			PVR_DPF((PVR_DBG_MESSAGE,
					 "%s: Process stat incremented on 'dead' process PID(%d)",
					 __func__, currentPid));

			/* Move process from dead list to live list */
			_MoveProcessToLiveList(psProcessStats);
		}
#endif
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		/* Release the list lock as soon as we acquire the process lock,
		 * this ensures if the process is in deadlist the entry cannot be
		 * deleted or modified
		 */
		OSLockRelease(g_psLinkedListLock);

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
		ui64InitialSize = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
#endif

		/* Update the memory watermarks... */
		switch (eAllocType)
		{
			case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT, uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			default:
			{
				PVR_ASSERT(0);
			}
			break;
		}

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
		if (psProcessStats->pid != PVR_SYS_ALLOC_PID)
		{
			IMG_UINT64 ui64Size = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
			if (ui64Size != ui64InitialSize)
			{
				TracepointUpdateGPUMemPerProcess(0, psProcessStats->pid,
				                                 ui64Size);
			}
		}
#endif

		OSLockRelease(psProcessStats->hLock);
	}

}

static void
_DecreaseProcStatValue(PVRSRV_MEM_ALLOC_TYPE eAllocType,
                       PVRSRV_PROCESS_STATS* psProcessStats,
                       IMG_UINT64 uiBytes)
{
#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	IMG_UINT64 ui64InitialSize = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
#endif

	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT, uiBytes);
			if (psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT] == 0)
			{
					psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		default:
		{
			PVR_ASSERT(0);
		}
		break;
	}

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	if (psProcessStats->pid != PVR_SYS_ALLOC_PID)
	{
		IMG_UINT64 ui64Size = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
		if (ui64Size != ui64InitialSize)
		{
			TracepointUpdateGPUMemPerProcess(0, psProcessStats->pid, ui64Size);
		}
	}
#endif
}

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
int RawProcessStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_PROCESS_STATS *psProcessStats;
	DLLIST_NODE *psNode, *psNext;

	DIPrintf(psEntry,
	         "%s,%s,%s,%s,%s,%s,%s\n",
	         "PID",
	         "MemoryUsageKMalloc",           // PVRSRV_PROCESS_STAT_TYPE_KMALLOC
	         "MemoryUsageAllocPTMemoryUMA",  // PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA
	         "MemoryUsageAllocPTMemoryLMA",  // PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA
	         "MemoryUsageAllocGPUMemLMA",    // PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES
	         "MemoryUsageAllocGPUMemUMA",    // PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES
	         "MemoryUsageDmaBufImport");     // PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT

	OSLockAcquire(g_psLinkedListLock);

	dllist_foreach_node(&gsLiveList, psNode, psNext)
	{
		psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
		if (psProcessStats->pid != PVR_SYS_ALLOC_PID)
		{
			DIPrintf(psEntry,
			         "%d,%"IMG_INT64_FMTSPECd",%"IMG_INT64_FMTSPECd","
			         "%"IMG_INT64_FMTSPECd",%"IMG_INT64_FMTSPECd","
			         "%"IMG_INT64_FMTSPECd",%"IMG_INT64_FMTSPECd"\n",
			         psProcessStats->pid,
			         psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC],
			         psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA],
			         psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA],
			         psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES],
			         psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES],
			         psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT]);
		}
	}

	OSLockRelease(g_psLinkedListLock);

	return 0;
} /* RawProcessStatsPrintElements */
#endif

void
PVRSRVStatsDecrMemKAllocStat(size_t uiBytes,
                             IMG_PID decrPID)
{
	PVRSRV_PROCESS_STATS*  psProcessStats;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	_decrease_global_stat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC, uiBytes);

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(decrPID);

	if (psProcessStats != NULL)
	{
		/* Decrement the kmalloc memory stat... */
		DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, uiBytes);
		DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, uiBytes);
	}

	OSLockRelease(g_psLinkedListLock);
}

static void
_StatsDecrMemTrackedStat(_PVR_STATS_TRACKING_HASH_ENTRY *psTrackingHashEntry,
                        PVRSRV_MEM_ALLOC_TYPE eAllocType)
{
	PVRSRV_PROCESS_STATS*  psProcessStats;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	_decrease_global_stat(eAllocType, psTrackingHashEntry->uiSizeInBytes);

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(psTrackingHashEntry->uiPid);

	if (psProcessStats != NULL)
	{
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		/* Decrement the memory stat... */
		_DecreaseProcStatValue(eAllocType,
		                       psProcessStats,
		                       psTrackingHashEntry->uiSizeInBytes);
		OSLockRelease(psProcessStats->hLock);
	}

	OSLockRelease(g_psLinkedListLock);
}

void
PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE eAllocType,
									  IMG_UINT64 uiCpuVAddr)
{
	_PVR_STATS_TRACKING_HASH_ENTRY *psTrackingHashEntry = NULL;

	if (!bProcessStatsInitialised || (gpsSizeTrackingHashTable == NULL))
	{
		return;
	}

	OSLockAcquire(gpsSizeTrackingHashTableLock);
	psTrackingHashEntry = (_PVR_STATS_TRACKING_HASH_ENTRY *)HASH_Remove(gpsSizeTrackingHashTable, uiCpuVAddr);
	OSLockRelease(gpsSizeTrackingHashTableLock);
	if (psTrackingHashEntry)
	{
		_StatsDecrMemTrackedStat(psTrackingHashEntry, eAllocType);
		OSFreeMemNoStats(psTrackingHashEntry);
	}
}

void
PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
							size_t uiBytes,
							IMG_PID currentPid)
{
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_PROCESS_STATS*  psProcessStats = NULL;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	_decrease_global_stat(eAllocType, uiBytes);

	OSLockAcquire(g_psLinkedListLock);
	if (psPVRSRVData)
	{
		if ((currentPid == psPVRSRVData->cleanupThreadPid) &&
		    (currentCleanupPid != 0))
		{
			psProcessStats = _FindProcessStats(currentCleanupPid);
		}
		else
		{
			psProcessStats = _FindProcessStats(currentPid);
		}
	}
	else
	{
		psProcessStats = _FindProcessStats(currentPid);
	}


	if (psProcessStats != NULL)
	{
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		/* Release the list lock as soon as we acquire the process lock,
		 * this ensures if the process is in deadlist the entry cannot be
		 * deleted or modified
		 */
		OSLockRelease(g_psLinkedListLock);
		/* Update the memory watermarks... */
		_DecreaseProcStatValue(eAllocType,
		                       psProcessStats,
		                       uiBytes);
		OSLockRelease(psProcessStats->hLock);

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		/* If all stats are now zero, remove the entry for this thread */
		if (psProcessStats->ui32StatAllocFlags == 0)
		{
			OSLockAcquire(g_psLinkedListLock);
			_MoveProcessToDeadList(psProcessStats);
			OSLockRelease(g_psLinkedListLock);

			/* Check if the dead list needs to be reduced */
			_CompressMemoryUsage();
		}
#endif
	}else{
		OSLockRelease(g_psLinkedListLock);
	}
}

/* For now we do not want to expose the global stats API
 * so we wrap it into this specific function for pooled pages.
 * As soon as we need to modify the global stats directly somewhere else
 * we want to replace these functions with more general ones.
 */
void
PVRSRVStatsIncrMemAllocPoolStat(size_t uiBytes)
{
	_increase_global_stat(PVRSRV_MEM_ALLOC_TYPE_UMA_POOL_PAGES, uiBytes);
}

void
PVRSRVStatsDecrMemAllocPoolStat(size_t uiBytes)
{
	_decrease_global_stat(PVRSRV_MEM_ALLOC_TYPE_UMA_POOL_PAGES, uiBytes);
}

PVRSRV_ERROR
PVRSRVStatsUpdateOOMStat(CONNECTION_DATA *psConnection,
						  PVRSRV_DEVICE_NODE *psDeviceNode,
						  IMG_UINT32 ui32OOMStatType,
						  IMG_PID pidOwner)
{
	PVRSRV_DEVICE_STAT_TYPE eOOMStatType = (PVRSRV_DEVICE_STAT_TYPE) ui32OOMStatType;
	IMG_PID	pidCurrent = pidOwner;
	PVRSRV_PROCESS_STATS* psProcessStats;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return PVRSRV_ERROR_NOT_INITIALISED;
	}

	if (ui32OOMStatType >= PVRSRV_DEVICE_STAT_TYPE_COUNT)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(pidCurrent);
	if (psProcessStats != NULL)
	{
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->ai32DevStats[psDeviceNode->sDevId.ui32InternalID][eOOMStatType]++;
		OSLockRelease(psProcessStats->hLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVStatsUpdateOOMStat: Process not found for Pid=%d", pidCurrent));
	}

	OSLockRelease(g_psLinkedListLock);

	return PVRSRV_OK;
} /* PVRSRVStatsUpdateOOMStat */

void
PVRSRVStatsUpdateRenderContextStats(PVRSRV_DEVICE_NODE *psDeviceNode,
									IMG_UINT32 ui32TotalNumPartialRenders,
									IMG_UINT32 ui32TotalNumOutOfMemory,
									IMG_UINT32 ui32NumTAStores,
									IMG_UINT32 ui32Num3DStores,
									IMG_UINT32 ui32NumCDMStores,
									IMG_UINT32 ui32NumTDMStores,
									IMG_PID pidOwner)
{
	IMG_PID	pidCurrent = pidOwner;

	PVRSRV_PROCESS_STATS* psProcessStats;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(pidCurrent);
	if (psProcessStats != NULL)
	{
		IMG_UINT32 ui32DevID = psDeviceNode->sDevId.ui32InternalID;

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_RC_PRS]       += ui32TotalNumPartialRenders;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_RC_OOMS]      += ui32TotalNumOutOfMemory;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_RC_TA_STORES] += ui32NumTAStores;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_RC_3D_STORES] += ui32Num3DStores;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_RC_CDM_STORES]+= ui32NumCDMStores;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_RC_TDM_STORES]+= ui32NumTDMStores;
		OSLockRelease(psProcessStats->hLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVStatsUpdateRenderContextStats: Process not found for Pid=%d", pidCurrent));
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateRenderContextStats */

void
PVRSRVStatsUpdateZSBufferStats(PVRSRV_DEVICE_NODE *psDeviceNode,
							   IMG_UINT32 ui32NumReqByApp,
							   IMG_UINT32 ui32NumReqByFW,
							   IMG_PID owner)
{
	IMG_PID				  currentPid = (owner==0)?OSGetCurrentClientProcessIDKM():owner;
	PVRSRV_PROCESS_STATS* psProcessStats;


	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(currentPid);
	if (psProcessStats != NULL)
	{
		IMG_UINT32 ui32DevID = psDeviceNode->sDevId.ui32InternalID;

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_ZSBUFFER_REQS_BY_APP] += ui32NumReqByApp;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_ZSBUFFER_REQS_BY_FW] += ui32NumReqByFW;
		OSLockRelease(psProcessStats->hLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Process not found for Pid=%d", __func__, currentPid));
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateZSBufferStats */

void
PVRSRVStatsUpdateFreelistStats(PVRSRV_DEVICE_NODE *psDeviceNode,
							   IMG_UINT32 ui32NumGrowReqByApp,
							   IMG_UINT32 ui32NumGrowReqByFW,
							   IMG_UINT32 ui32InitFLPages,
							   IMG_UINT32 ui32NumHighPages,
							   IMG_PID ownerPid)
{
	IMG_PID				  currentPid = (ownerPid!=0)?ownerPid:OSGetCurrentClientProcessIDKM();
	PVRSRV_PROCESS_STATS* psProcessStats;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(currentPid);

	if (psProcessStats != NULL)
	{
		IMG_UINT32 ui32DevID = psDeviceNode->sDevId.ui32InternalID;

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_FREELIST_GROW_REQS_BY_APP] += ui32NumGrowReqByApp;
		psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_FREELIST_GROW_REQS_BY_FW] += ui32NumGrowReqByFW;

		UPDATE_MAX_VALUE(psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_FREELIST_PAGES_INIT],
						 (IMG_INT32) ui32InitFLPages);

		UPDATE_MAX_VALUE(psProcessStats->ai32DevStats[ui32DevID][PVRSRV_DEVICE_STAT_TYPE_FREELIST_MAX_PAGES],
						 (IMG_INT32) ui32NumHighPages);
		OSLockRelease(psProcessStats->hLock);

	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: Process not found for Pid=%d", __func__, currentPid));
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateFreelistStats */


#if defined(ENABLE_DEBUGFS_PIDS)

int
GenericStatsPrintElementsLive(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_STAT_PV_DATA *psStatType = DIGetPrivData(psEntry);
	PVRSRV_PROCESS_STATS* psProcessStats;
	DLLIST_NODE *psNode, *psNext;

	PVR_UNREFERENCED_PARAMETER(pvData);

	PVR_ASSERT(psStatType->pfnStatsPrintElements != NULL);

	DIPrintf(psEntry, "%s\n", psStatType->szLiveStatsHeaderStr);

	OSLockAcquire(g_psLinkedListLock);

	if (dllist_is_empty(&gsLiveList))
	{
		DIPrintf(psEntry, "No Stats to display\n%s\n", g_szSeparatorStr);
	}
	else
	{
		dllist_foreach_node(&gsLiveList, psNode, psNext)
		{
			psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
			psStatType->pfnStatsPrintElements(psEntry, psProcessStats);
			DIPrintf(psEntry, "%s\n", g_szSeparatorStr);
		}
	}
	OSLockRelease(g_psLinkedListLock);

	return 0;
}

int
GenericStatsPrintElementsRetired(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_STAT_PV_DATA *psStatType = DIGetPrivData(psEntry);
	PVRSRV_PROCESS_STATS* psProcessStats;
	DLLIST_NODE *psNode, *psNext;

	PVR_UNREFERENCED_PARAMETER(pvData);

	PVR_ASSERT(psStatType->pfnStatsPrintElements != NULL);

	DIPrintf(psEntry, "%s\n", psStatType->szRetiredStatsHeaderStr);

	OSLockAcquire(g_psLinkedListLock);

	if (dllist_is_empty(&gsDeadList))
	{
		DIPrintf(psEntry, "No Stats to display\n%s\n", g_szSeparatorStr);
	}
	else
	{
		dllist_foreach_node(&gsDeadList, psNode, psNext)
		{
			psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
			psStatType->pfnStatsPrintElements(psEntry, psProcessStats);
			DIPrintf(psEntry, "%s\n", g_szSeparatorStr);
		}
	}
	OSLockRelease(g_psLinkedListLock);

	return 0;
}

#if defined(PVRSRV_ENABLE_PERPID_STATS)
/*************************************************************************/ /*!
@Function       ProcessStatsPrintElements
@Description    Prints all elements for this process statistic record.
@Input          pvStatPtr         Pointer to statistics structure.
@Input          pfnOSStatsPrintf  Printf function to use for output.
*/ /**************************************************************************/
void
ProcessStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                          PVRSRV_PROCESS_STATS *psProcessStats)
{
	IMG_UINT32 ui32StatNumber;

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	DIPrintf(psEntry, "PID %u\n", psProcessStats->pid);

	/* Print device stats table PVRSRV_DEVICE_STAT_TYPE */
	if (psProcessStats->ui32DevCount > 0)
	{
		IMG_UINT32 i;

		for (ui32StatNumber = 0;
			 ui32StatNumber < ARRAY_SIZE(pszDeviceStatType);
			 ui32StatNumber++)
		{
			if (OSStringNCompare(pszDeviceStatType[ui32StatNumber], "", 1) != 0)
			{
				DIPrintf(psEntry, "%-34s",
						 pszDeviceStatType[ui32StatNumber]);

				for (i = 0; i < psProcessStats->ui32DevCount; i++)
				{
					if (i == 0)
					{
						DIPrintf(psEntry, "%10d",
								 psProcessStats->ai32DevStats[i][ui32StatNumber]);
					}
					else
					{
						DIPrintf(psEntry, ",%d",
								 psProcessStats->ai32DevStats[i][ui32StatNumber]);
					}
				}
			}

			DIPrintf(psEntry, "\n");
		}
	}

	/* Print process memory stats table PVRSRV_PROCESS_STAT_TYPE */
	for (ui32StatNumber = 0;
	     ui32StatNumber < ARRAY_SIZE(pszProcessStatType);
	     ui32StatNumber++)
	{
		if (OSStringNCompare(pszProcessStatType[ui32StatNumber], "", 1) != 0)
		{
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
			if ((ui32StatNumber == PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES) ||
			    (ui32StatNumber == PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES))
			{
				/* get the stat from RI */
				IMG_INT32 ui32Total = RITotalAllocProcessKM(psProcessStats->pid,
									    (ui32StatNumber == PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES)
									    ? PHYS_HEAP_TYPE_LMA : PHYS_HEAP_TYPE_UMA);

				DIPrintf(psEntry, "%-34s%10d %8dK\n",
						 pszProcessStatType[ui32StatNumber], ui32Total, ui32Total>>10);
			}
			else
#endif
			{
				if (ui32StatNumber >= PVRSRV_PROCESS_STAT_TYPE_KMALLOC &&
					ui32StatNumber <= PVRSRV_PROCESS_STAT_TYPE_TOTAL_MAX)
				{
					DIPrintf(psEntry, "%-34s%10"IMG_INT64_FMTSPECd" %8"IMG_INT64_FMTSPECd"K\n",
					         pszProcessStatType[ui32StatNumber],
					         psProcessStats->i64StatValue[ui32StatNumber],
					         psProcessStats->i64StatValue[ui32StatNumber] >> 10);
				}
				else
				{
					DIPrintf(psEntry, "%-34s%10"IMG_INT64_FMTSPECd"\n",
					         pszProcessStatType[ui32StatNumber],
					         psProcessStats->i64StatValue[ui32StatNumber]);
				}
			}
		}
	}

	OSLockRelease(psProcessStats->hLock);
} /* ProcessStatsPrintElements */
#endif

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
void
PVRSRVStatsUpdateCacheOpStats(PVRSRV_CACHE_OP uiCacheOp,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEV_PHYADDR sDevPAddr,
#endif
							IMG_DEVMEM_SIZE_T uiOffset,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_UINT64 ui64ExecuteTime,
							IMG_BOOL bUserModeFlush,
							IMG_PID ownerPid)
{
	IMG_PID				  currentPid = (ownerPid!=0)?ownerPid:OSGetCurrentClientProcessIDKM();
	PVRSRV_PROCESS_STATS* psProcessStats;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	/* Lock while we find the correct process and update the record... */
	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = _FindProcessStats(currentPid);

	if (psProcessStats != NULL)
	{
		IMG_INT32 Idx;

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

		/* Look-up next buffer write index */
		Idx = psProcessStats->uiCacheOpWriteIndex;
		psProcessStats->uiCacheOpWriteIndex = INCREMENT_CACHEOP_STAT_IDX_WRAP(Idx);

		/* Store all CacheOp meta-data */
		psProcessStats->asCacheOp[Idx].uiCacheOp = uiCacheOp;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
		psProcessStats->asCacheOp[Idx].sDevVAddr = sDevVAddr;
		psProcessStats->asCacheOp[Idx].sDevPAddr = sDevPAddr;
#endif
		psProcessStats->asCacheOp[Idx].uiOffset = uiOffset;
		psProcessStats->asCacheOp[Idx].uiSize = uiSize;
		psProcessStats->asCacheOp[Idx].bUserModeFlush = bUserModeFlush;
		psProcessStats->asCacheOp[Idx].ui64ExecuteTime = ui64ExecuteTime;

		OSLockRelease(psProcessStats->hLock);
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateCacheOpStats */

/*************************************************************************/ /*!
@Function       CacheOpStatsPrintElements
@Description    Prints all elements for this process statistic CacheOp record.
@Input          pvStatPtr         Pointer to statistics structure.
@Input          pfnOSStatsPrintf  Printf function to use for output.
*/ /**************************************************************************/
void
CacheOpStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                          PVRSRV_PROCESS_STATS *psProcessStats)
{
	IMG_CHAR  *pszCacheOpType, *pszFlushType, *pszFlushMode;
	IMG_INT32 i32WriteIdx, i32ReadIdx;

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
	#define CACHEOP_RI_PRINTF_HEADER \
		"%-10s %-10s %-5s %-16s %-16s %-10s %-10s %-12s\n"
	#define CACHEOP_RI_PRINTF		\
		"%-10s %-10s %-5s 0x%-14llx 0x%-14llx 0x%-8llx 0x%-8llx %-12llu\n"
#else
	#define CACHEOP_PRINTF_HEADER	\
		"%-10s %-10s %-5s %-10s %-10s %-12s\n"
	#define CACHEOP_PRINTF			\
		"%-10s %-10s %-5s 0x%-8llx 0x%-8llx %-12llu\n"
#endif

	DIPrintf(psEntry, "PID %u\n", psProcessStats->pid);

	/* File header info */
	DIPrintf(psEntry,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
					CACHEOP_RI_PRINTF_HEADER,
#else
					CACHEOP_PRINTF_HEADER,
#endif
					"CacheOp",
					"Type",
					"Mode",
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
					"DevVAddr",
					"DevPAddr",
#endif
					"Offset",
					"Size",
					"Time (us)");

	/* Take a snapshot of write index, read backwards in buffer
	   and wrap round at boundary */
	i32WriteIdx = psProcessStats->uiCacheOpWriteIndex;
	for (i32ReadIdx = DECREMENT_CACHEOP_STAT_IDX_WRAP(i32WriteIdx);
		 i32ReadIdx != i32WriteIdx;
		 i32ReadIdx = DECREMENT_CACHEOP_STAT_IDX_WRAP(i32ReadIdx))
	{
		IMG_UINT64 ui64ExecuteTime = psProcessStats->asCacheOp[i32ReadIdx].ui64ExecuteTime;
		IMG_DEVMEM_SIZE_T ui64NumOfPages = psProcessStats->asCacheOp[i32ReadIdx].uiSize >> OSGetPageShift();

		if (ui64NumOfPages <= PMR_MAX_TRANSLATION_STACK_ALLOC)
		{
			pszFlushType = "RBF.Fast";
		}
		else
		{
			pszFlushType = "RBF.Slow";
		}

		if (psProcessStats->asCacheOp[i32ReadIdx].bUserModeFlush)
		{
			pszFlushMode = "UM";
		}
		else
		{
			pszFlushMode = "KM";
		}

		switch (psProcessStats->asCacheOp[i32ReadIdx].uiCacheOp)
		{
			case PVRSRV_CACHE_OP_NONE:
				pszCacheOpType = "None";
				break;
			case PVRSRV_CACHE_OP_CLEAN:
				pszCacheOpType = "Clean";
				break;
			case PVRSRV_CACHE_OP_INVALIDATE:
				pszCacheOpType = "Invalidate";
				break;
			case PVRSRV_CACHE_OP_FLUSH:
				pszCacheOpType = "Flush";
				break;
			default:
				pszCacheOpType = "Unknown";
				break;
		}

		DIPrintf(psEntry,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							CACHEOP_RI_PRINTF,
#else
							CACHEOP_PRINTF,
#endif
							pszCacheOpType,
							pszFlushType,
							pszFlushMode,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							psProcessStats->asCacheOp[i32ReadIdx].sDevVAddr.uiAddr,
							psProcessStats->asCacheOp[i32ReadIdx].sDevPAddr.uiAddr,
#endif
							psProcessStats->asCacheOp[i32ReadIdx].uiOffset,
							psProcessStats->asCacheOp[i32ReadIdx].uiSize,
							ui64ExecuteTime);
		}

} /* CacheOpStatsPrintElements */
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
static PVRSRV_ERROR _PrintMemStatsEntry(uintptr_t k, uintptr_t v, void* pvPriv)
{
	IMG_UINT32	ui32VAddrFields = sizeof(void*)/sizeof(IMG_UINT32);
	IMG_UINT32	ui32PAddrFields = sizeof(IMG_CPU_PHYADDR)/sizeof(IMG_UINT32);
	IMG_UINT32 ui32ItemNumber;
	PVRSRV_MEM_ALLOC_REC *psRecord = (PVRSRV_MEM_ALLOC_REC *)(uintptr_t)v;
	PVRSRV_MEM_ALLOC_PRINT_DATA *psPrintData = (PVRSRV_MEM_ALLOC_PRINT_DATA *)pvPriv;
	OSDI_IMPL_ENTRY *psEntry = psPrintData->psEntry;

	if (psRecord != NULL)
	{
		IMG_BOOL bPrintStat = IMG_TRUE;

		DIPrintf(psEntry, "%-5d  ", psPrintData->pid);

		switch (psRecord->eAllocType)
		{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:				DIPrintf(psEntry, "KMALLOC             "); break;
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:				DIPrintf(psEntry, "VMALLOC             "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:	DIPrintf(psEntry, "ALLOC_PAGES_PT_LMA  "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:	DIPrintf(psEntry, "ALLOC_PAGES_PT_UMA  "); break;
		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:		DIPrintf(psEntry, "IOREMAP_PT_LMA      "); break;
		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:			DIPrintf(psEntry, "VMAP_PT_UMA         "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:		DIPrintf(psEntry, "ALLOC_LMA_PAGES     "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:		DIPrintf(psEntry, "ALLOC_UMA_PAGES     "); break;
		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:	DIPrintf(psEntry, "MAP_UMA_LMA_PAGES   "); break;
		case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:      DIPrintf(psEntry, "DMA_BUF_IMPORT      "); break;
		default:										DIPrintf(psEntry, "INVALID             "); break;
		}

		if (bPrintStat)
		{
			for (ui32ItemNumber = 0; ui32ItemNumber < ui32VAddrFields; ui32ItemNumber++)
			{
				DIPrintf(psEntry, "%08x", *(((IMG_UINT32*) &psRecord->pvCpuVAddr) + ui32VAddrFields - ui32ItemNumber - 1));
			}
			DIPrintf(psEntry, "  ");

			for (ui32ItemNumber = 0; ui32ItemNumber < ui32PAddrFields; ui32ItemNumber++)
			{
				DIPrintf(psEntry, "%08x", *(((IMG_UINT32*) &psRecord->sCpuPAddr.uiAddr) + ui32PAddrFields - ui32ItemNumber - 1));
			}

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
			DIPrintf(psEntry, "  " IMG_SIZE_FMTSPEC, psRecord->uiBytes);

			DIPrintf(psEntry, "  %s", (IMG_CHAR*) psRecord->pvAllocdFromFile);

			DIPrintf(psEntry, "  %d\n", psRecord->ui32AllocdFromLine);
#else
			DIPrintf(psEntry, "  " IMG_SIZE_FMTSPEC "\n", psRecord->uiBytes);
#endif
		}

		psPrintData->ui32NumEntries++;
	}

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       MemStatsPrintElements
@Description    Prints all elements for the memory statistic record.
@Input          pvStatPtr         Pointer to statistics structure.
@Input          pfnOSStatsPrintf  Printf function to use for output.
*/ /**************************************************************************/
void
MemStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                      PVRSRV_PROCESS_STATS *psProcessStats)
{
	IMG_UINT32	ui32VAddrFields = sizeof(void*)/sizeof(IMG_UINT32);
	IMG_UINT32	ui32PAddrFields = sizeof(IMG_CPU_PHYADDR)/sizeof(IMG_UINT32);
	IMG_UINT32 ui32ItemNumber;
	PVRSRV_MEM_ALLOC_PRINT_DATA sPrintData;

	sPrintData.psEntry = psEntry;
	sPrintData.pid = psProcessStats->pid;
	sPrintData.ui32NumEntries = 0;

	/* Write the header... */
	DIPrintf(psEntry, "PID    ");

	DIPrintf(psEntry, "Type                VAddress");
	for (ui32ItemNumber = 1;  ui32ItemNumber < ui32VAddrFields;  ui32ItemNumber++)
	{
		DIPrintf(psEntry, "        ");
	}

	DIPrintf(psEntry, "  PAddress");
	for (ui32ItemNumber = 1;  ui32ItemNumber < ui32PAddrFields;  ui32ItemNumber++)
	{
		DIPrintf(psEntry, "        ");
	}

	DIPrintf(psEntry, "  Size(bytes)\n");

	HASH_Iterate(psProcessStats->psMemoryRecords, (HASH_pfnCallback)_PrintMemStatsEntry, &sPrintData);

	if (sPrintData.ui32NumEntries == 0)
	{
		DIPrintf(psEntry, "%-5d\n", psProcessStats->pid);
	}
} /* MemStatsPrintElements */
#endif

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
/*************************************************************************/ /*!
@Function       RIMemStatsPrintElements
@Description    Prints all elements for the RI Memory record.
@Input          pvStatPtr         Pointer to statistics structure.
@Input          pfnOSStatsPrintf  Printf function to use for output.
*/ /**************************************************************************/
void RIMemStatsPrintElements(OSDI_IMPL_ENTRY *psEntry,
                             PVRSRV_PROCESS_STATS *psProcessStats)
{
	IMG_CHAR   *pszStatFmtText  = NULL;
	IMG_HANDLE *pRIHandle       = NULL;

	/* Acquire RI lock */
	RILockAcquireKM();

	/*
	 * Loop through the RI system to get each line of text.
	 */
	while (RIGetListEntryKM(psProcessStats->pid,
							&pRIHandle,
							&pszStatFmtText))
	{
		DIPrintf(psEntry, "%s", pszStatFmtText);
	}

	/* Release RI lock */
	RILockReleaseKM();

} /* RIMemStatsPrintElements */
#endif

#endif

int GlobalStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	IMG_UINT32 ui32StatNumber;
	PVR_UNREFERENCED_PARAMETER(pvData);

	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

	for (ui32StatNumber = 0;
	     ui32StatNumber < ARRAY_SIZE(pszDriverStatType);
	     ui32StatNumber++)
	{
		if (OSStringNCompare(pszDriverStatType[ui32StatNumber], "", 1) != 0)
		{
			DIPrintf(psEntry, "%-34s%12llu\n",
				    pszDriverStatType[ui32StatNumber],
				    GET_GLOBAL_STAT_VALUE(ui32StatNumber));
		}
	}

	OSLockRelease(gsGlobalStats.hGlobalStatsLock);

	return 0;
}

/*************************************************************************/ /*!
@Function       PVRSRVFindProcessMemStats
@Description    Using the provided PID find memory stats for that process.
                Memstats will be provided for live/connected processes only.
                Memstat values provided by this API relate only to the physical
                memory allocated by the process and does not relate to any of
                the mapped or imported memory.
@Input          pid                 Process to search for.
@Input          ArraySize           Size of the array where memstat
                                    records will be stored
@Input          bAllProcessStats    Flag to denote if stats for
                                    individual process are requested
                                    stats for all processes are
                                    requested
@Input          MemoryStats         Handle to the memory where memstats
                                    are stored.
@Output         Memory statistics records for the requested pid.
*/ /**************************************************************************/
PVRSRV_ERROR PVRSRVFindProcessMemStats(IMG_PID pid,
                                       IMG_UINT32 ui32ArrSize,
                                       IMG_BOOL bAllProcessStats,
                                       IMG_UINT64 *pui64MemoryStats)
{
	IMG_INT i;
	PVRSRV_PROCESS_STATS* psProcessStats;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pui64MemoryStats, "pui64MemoryStats");

	if (bAllProcessStats)
	{
		PVR_LOG_RETURN_IF_FALSE(ui32ArrSize == PVRSRV_DRIVER_STAT_TYPE_COUNT,
			"MemStats array size is incorrect",
			PVRSRV_ERROR_INVALID_PARAMS);

		OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

		for (i = 0; i < ui32ArrSize; i++)
		{
			pui64MemoryStats[i] = GET_GLOBAL_STAT_VALUE(i);
		}

		OSLockRelease(gsGlobalStats.hGlobalStatsLock);

		return PVRSRV_OK;
	}

	PVR_LOG_RETURN_IF_FALSE(ui32ArrSize == PVRSRV_PROCESS_STAT_TYPE_COUNT,
		"MemStats array size is incorrect",
		PVRSRV_ERROR_INVALID_PARAMS);

	OSLockAcquire(g_psLinkedListLock);

	/* Search for the given PID in the Live List */
	psProcessStats = _FindProcessStatsInLiveList(pid);

	if (psProcessStats == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "Process %d not found. This process may not be live anymore.", (IMG_INT)pid));
		OSLockRelease(g_psLinkedListLock);

		return PVRSRV_ERROR_PROCESS_NOT_FOUND;
	}

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
	for (i = 0; i < ui32ArrSize; i++)
	{
		pui64MemoryStats[i] = psProcessStats->i64StatValue[i];
	}
	OSLockRelease(psProcessStats->hLock);

	OSLockRelease(g_psLinkedListLock);

	return PVRSRV_OK;

} /* PVRSRVFindProcessMemStats */

/*************************************************************************/ /*!
@Function       PVRSRVGetProcessMemUsage
@Description    Calculate allocated kernel and graphics memory for all live or
                connected processes. Memstat values provided by this API relate
                only to the physical memory allocated by the process and does
                not relate to any of the mapped or imported memory.
@Output         pui64TotalMem                   Total memory usage for all live
                                                PIDs connected to the driver.
@Output         pui32NumberOfLivePids           Number of live pids currently
                                                connected to the server.
@Output         ppsPerProcessMemUsageData       Handle to an array of
                                                PVRSRV_PER_PROCESS_MEM_USAGE,
                                                number of elements defined by
                                                pui32NumberOfLivePids.
@Return         PVRSRV_OK                       Success
                PVRSRV_ERROR_PROCESS_NOT_FOUND  No live processes.
                PVRSRV_ERROR_OUT_OF_MEMORY      Failed to allocate memory for
                                                ppsPerProcessMemUsageData.
*/ /**************************************************************************/
PVRSRV_ERROR PVRSRVGetProcessMemUsage(IMG_UINT64 *pui64TotalMem,
									  IMG_UINT32 *pui32NumberOfLivePids,
									  PVRSRV_PER_PROCESS_MEM_USAGE **ppsPerProcessMemUsageData)
{
	IMG_UINT32 ui32NumberOfLivePids = 0;
	PVRSRV_ERROR eError = PVRSRV_ERROR_PROCESS_NOT_FOUND;
	PVRSRV_PER_PROCESS_MEM_USAGE* psPerProcessMemUsageData = NULL;
	DLLIST_NODE *psNode, *psNext;

	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

	*pui64TotalMem = GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_KMALLOC) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_VMALLOC) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA);

	OSLockRelease(gsGlobalStats.hGlobalStatsLock);

	OSLockAcquire(g_psLinkedListLock);

	dllist_foreach_node(&gsLiveList, psNode, psNext)
	{
		ui32NumberOfLivePids++;
	}

	if (ui32NumberOfLivePids > 0)
	{
		/* Use OSAllocZMemNoStats to prevent deadlock. */
		psPerProcessMemUsageData = OSAllocZMemNoStats(ui32NumberOfLivePids * sizeof(*psPerProcessMemUsageData));

		if (psPerProcessMemUsageData)
		{
			PVRSRV_PROCESS_STATS* psProcessStats = NULL;
			IMG_UINT32 ui32Counter = 0;

			dllist_foreach_node(&gsLiveList, psNode, psNext)
			{
				psProcessStats = IMG_CONTAINER_OF(psNode, PVRSRV_PROCESS_STATS, sNode);
				OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

				psPerProcessMemUsageData[ui32Counter].ui32Pid = (IMG_UINT32)psProcessStats->pid;

				psPerProcessMemUsageData[ui32Counter].ui64KernelMemUsage =
					psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] +
					psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC];

				psPerProcessMemUsageData[ui32Counter].ui64GraphicsMemUsage =
					psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] +
					psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] +
					psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] +
					psProcessStats->i64StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES];

				OSLockRelease(psProcessStats->hLock);
				ui32Counter++;
			}
			eError = PVRSRV_OK;
		}
		else
		{
			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}

	OSLockRelease(g_psLinkedListLock);
	*pui32NumberOfLivePids = ui32NumberOfLivePids;
	*ppsPerProcessMemUsageData = psPerProcessMemUsageData;

	return eError;

} /* PVRSRVGetProcessMemUsage */
