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
#include "lists.h"
#include "process_stats.h"
#include "ri_server.h"
#include "hash.h"
#include "connection_server.h"
#include "pvrsrv.h"
#include "proc_stats.h"
#include "htbuffer.h"
#include "pvr_ricommon.h"
#include "di_server.h"
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
const IMG_CHAR *const pszProcessStatType[PVRSRV_PROCESS_STAT_TYPE_COUNT] = { PVRSRV_PROCESS_STAT_KEY };
#undef X
#endif

/* Array of Driver stat type defined using the X-Macro */
#define X(stat_type, stat_str) stat_str,
const IMG_CHAR *const pszDriverStatType[PVRSRV_DRIVER_STAT_TYPE_COUNT] = { PVRSRV_DRIVER_STAT_KEY };
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
int PowerStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData);
int GlobalStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData);

/* Note: all of the accesses to the global stats should be protected
 * by the gsGlobalStats.hGlobalStatsLock lock. This means all of the
 * invocations of macros *_GLOBAL_STAT_VALUE. */

/* Macros for fetching stat values */
#define GET_STAT_VALUE(ptr,var) (ptr)->i32StatValue[(var)]
#define GET_GLOBAL_STAT_VALUE(idx) gsGlobalStats.ui32StatValue[idx]

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
#define INCREASE_STAT_VALUE(ptr,var,val)		do { (ptr)->i32StatValue[(var)] += (val); if ((ptr)->i32StatValue[(var)] > (ptr)->i32StatValue[(var##_MAX)]) {(ptr)->i32StatValue[(var##_MAX)] = (ptr)->i32StatValue[(var)];} } while (0)
#define INCREASE_GLOBAL_STAT_VALUE(var,idx,val)		do { (var).ui32StatValue[(idx)] += (val); if ((var).ui32StatValue[(idx)] > (var).ui32StatValue[(idx##_MAX)]) {(var).ui32StatValue[(idx##_MAX)] = (var).ui32StatValue[(idx)];} } while (0)
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
/* Allow stats to go negative */
#define DECREASE_STAT_VALUE(ptr,var,val)		do { (ptr)->i32StatValue[(var)] -= (val); } while (0)
#define DECREASE_GLOBAL_STAT_VALUE(var,idx,val)		do { (var).ui32StatValue[(idx)] -= (val); } while (0)
#else
#define DECREASE_STAT_VALUE(ptr,var,val)		do { if ((ptr)->i32StatValue[(var)] >= (val)) { (ptr)->i32StatValue[(var)] -= (val); } else { (ptr)->i32StatValue[(var)] = 0; } } while (0)
#define DECREASE_GLOBAL_STAT_VALUE(var,idx,val)		do { if ((var).ui32StatValue[(idx)] >= (val)) { (var).ui32StatValue[(idx)] -= (val); } else { (var).ui32StatValue[(idx)] = 0; } } while (0)
#endif
#define MAX_CACHEOP_STAT 16
#define INCREMENT_CACHEOP_STAT_IDX_WRAP(x) ((x+1) >= MAX_CACHEOP_STAT ? 0 : (x+1))
#define DECREMENT_CACHEOP_STAT_IDX_WRAP(x) ((x-1) < 0 ? (MAX_CACHEOP_STAT-1) : (x-1))

/*
 * Structures for holding statistics...
 */
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
typedef struct _PVRSRV_MEM_ALLOC_REC_
{
	PVRSRV_MEM_ALLOC_TYPE           eAllocType;
	IMG_UINT64                      ui64Key;
	void*                           pvCpuVAddr;
	IMG_CPU_PHYADDR	                sCpuPAddr;
	size_t                          uiBytes;
	void*                           pvPrivateData;
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
	void*                           pvAllocdFromFile;
	IMG_UINT32                      ui32AllocdFromLine;
#endif
	IMG_PID	                        pid;
	struct _PVRSRV_MEM_ALLOC_REC_*  psNext;
	struct _PVRSRV_MEM_ALLOC_REC_** ppsThis;
} PVRSRV_MEM_ALLOC_REC;
#endif

typedef struct _PVRSRV_PROCESS_STATS_ {

	/* Linked list pointers */
	struct _PVRSRV_PROCESS_STATS_* psNext;
	struct _PVRSRV_PROCESS_STATS_* psPrev;

	/* Create per process lock that need to be held
	 * to edit of its members */
	POS_LOCK                       hLock;

	/* OS level process ID */
	IMG_PID	                       pid;
	IMG_UINT32                     ui32RefCount;

	/* Stats... */
	IMG_INT32                      i32StatValue[PVRSRV_PROCESS_STAT_TYPE_COUNT];
	IMG_UINT32                     ui32StatAllocFlags;

#if defined(PVRSRV_ENABLE_CACHEOP_STATS)
	struct _CACHEOP_STRUCT_ {
		PVRSRV_CACHE_OP        uiCacheOp;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
		IMG_DEV_VIRTADDR       sDevVAddr;
		IMG_DEV_PHYADDR        sDevPAddr;
		RGXFWIF_DM             eFenceOpType;
#endif
		IMG_DEVMEM_SIZE_T      uiOffset;
		IMG_DEVMEM_SIZE_T      uiSize;
		IMG_UINT64             ui64ExecuteTime;
		IMG_BOOL               bUserModeFlush;
		IMG_UINT32             ui32OpSeqNum;
		IMG_BOOL               bIsFence;
		IMG_PID                ownerPid;
	}                              asCacheOp[MAX_CACHEOP_STAT];
	IMG_INT32                      uiCacheOpWriteIndex;
#endif

	/* Other statistics structures */
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	PVRSRV_MEM_ALLOC_REC*          psMemoryRecords;
#endif
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

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
static IMPLEMENT_LIST_INSERT(PVRSRV_MEM_ALLOC_REC)
static IMPLEMENT_LIST_REMOVE(PVRSRV_MEM_ALLOC_REC)
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
static PVRSRV_PROCESS_STATS *g_psLiveList;
static PVRSRV_PROCESS_STATS *g_psDeadList;

static POS_LOCK g_psLinkedListLock;
/* Lockdep feature in the kernel cannot differentiate between different instances of same lock type.
 * This allows it to group all such instances of the same lock type under one class
 * The consequence of this is that, if lock acquisition is nested on different instances, it generates
 * a false warning message about the possible occurrence of deadlock due to recursive lock acquisition.
 * Hence we create the following sub classes to explicitly appraise Lockdep of such safe lock nesting */
#define PROCESS_LOCK_SUBCLASS_CURRENT	1
#define PROCESS_LOCK_SUBCLASS_PREV		2
#define PROCESS_LOCK_SUBCLASS_NEXT		3
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
	IMG_UINT32 ui32StatValue[PVRSRV_DRIVER_STAT_TYPE_COUNT];
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

static void _AddProcessStatsToFrontOfDeadList(PVRSRV_PROCESS_STATS* psProcessStats);
static void _AddProcessStatsToFrontOfLiveList(PVRSRV_PROCESS_STATS* psProcessStats);
static void _RemoveProcessStatsFromList(PVRSRV_PROCESS_STATS* psProcessStats);

static void _DestroyProcessStat(PVRSRV_PROCESS_STATS* psProcessStats);

static void _DecreaseProcStatValue(PVRSRV_MEM_ALLOC_TYPE eAllocType,
                                   PVRSRV_PROCESS_STATS* psProcessStats,
                                   IMG_UINT32 uiBytes);
/*
 * Power statistics related definitions
 */

/* For the mean time, use an exponentially weighted moving average with a
 * 1/4 weighting for the new measurement.
 */
#define MEAN_TIME(A, B)     ( ((3*(A))/4) + ((1 * (B))/4) )

#define UPDATE_TIME(time, newtime) \
	((time) > 0 ? MEAN_TIME((time), (newtime)) : (newtime))

/* Enum to be used as input to GET_POWER_STAT_INDEX */
typedef enum
{
	DEVICE     = 0,
	SYSTEM     = 1,
	POST_POWER = 0,
	PRE_POWER  = 2,
	POWER_OFF  = 0,
	POWER_ON   = 4,
	NOT_FORCED = 0,
	FORCED     = 8,
} PVRSRV_POWER_STAT_TYPE;

/* Macro used to access one of the power timing statistics inside an array */
#define GET_POWER_STAT_INDEX(forced,powon,prepow,system) \
	((forced) + (powon) + (prepow) + (system))

/* For the power timing stats we need 16 variables to store all the
 * combinations of forced/not forced, power-on/power-off, pre-power/post-power
 * and device/system statistics
 */
#define NUM_POWER_STATS        (16)
static IMG_UINT32 aui32PowerTimingStats[NUM_POWER_STATS];

static DI_ENTRY *psPowerStatsDIEntry;

typedef struct _EXTRA_POWER_STATS_
{
	IMG_UINT64	ui64PreClockSpeedChangeDuration;
	IMG_UINT64	ui64BetweenPreEndingAndPostStartingDuration;
	IMG_UINT64	ui64PostClockSpeedChangeDuration;
} EXTRA_POWER_STATS;

#define NUM_EXTRA_POWER_STATS	10

static EXTRA_POWER_STATS asClockSpeedChanges[NUM_EXTRA_POWER_STATS];
static IMG_UINT32 ui32ClockSpeedIndexStart, ui32ClockSpeedIndexEnd;


#if defined(PVRSRV_ENABLE_PROCESS_STATS)
void InsertPowerTimeStatistic(IMG_UINT64 ui64SysStartTime, IMG_UINT64 ui64SysEndTime,
                              IMG_UINT64 ui64DevStartTime, IMG_UINT64 ui64DevEndTime,
                              IMG_BOOL bForced, IMG_BOOL bPowerOn, IMG_BOOL bPrePower)
{
	IMG_UINT32 *pui32Stat;
	IMG_UINT64 ui64DeviceDiff = ui64DevEndTime - ui64DevStartTime;
	IMG_UINT64 ui64SystemDiff = ui64SysEndTime - ui64SysStartTime;
	IMG_UINT32 ui32Index;

	if (bPrePower)
	{
		HTBLOGK(HTB_SF_MAIN_PRE_POWER, bPowerOn, ui64DeviceDiff, ui64SystemDiff);
	}
	else
	{
		HTBLOGK(HTB_SF_MAIN_POST_POWER, bPowerOn, ui64SystemDiff, ui64DeviceDiff);
	}

	ui32Index = GET_POWER_STAT_INDEX(bForced ? FORCED : NOT_FORCED,
	                                 bPowerOn ? POWER_ON : POWER_OFF,
	                                 bPrePower ? PRE_POWER : POST_POWER,
	                                 DEVICE);
	pui32Stat = &aui32PowerTimingStats[ui32Index];
	*pui32Stat = UPDATE_TIME(*pui32Stat, ui64DeviceDiff);

	ui32Index = GET_POWER_STAT_INDEX(bForced ? FORCED : NOT_FORCED,
	                                 bPowerOn ? POWER_ON : POWER_OFF,
	                                 bPrePower ? PRE_POWER : POST_POWER,
	                                 SYSTEM);
	pui32Stat = &aui32PowerTimingStats[ui32Index];
	*pui32Stat = UPDATE_TIME(*pui32Stat, ui64SystemDiff);
}

static IMG_UINT64 ui64PreClockSpeedChangeMark;

void InsertPowerTimeStatisticExtraPre(IMG_UINT64 ui64StartTimer, IMG_UINT64 ui64Stoptimer)
{
	asClockSpeedChanges[ui32ClockSpeedIndexEnd].ui64PreClockSpeedChangeDuration = ui64Stoptimer - ui64StartTimer;

	ui64PreClockSpeedChangeMark = OSClockus();
}

void InsertPowerTimeStatisticExtraPost(IMG_UINT64 ui64StartTimer, IMG_UINT64 ui64StopTimer)
{
	IMG_UINT64 ui64Duration = ui64StartTimer - ui64PreClockSpeedChangeMark;

	PVR_ASSERT(ui64PreClockSpeedChangeMark > 0);

	asClockSpeedChanges[ui32ClockSpeedIndexEnd].ui64BetweenPreEndingAndPostStartingDuration = ui64Duration;
	asClockSpeedChanges[ui32ClockSpeedIndexEnd].ui64PostClockSpeedChangeDuration = ui64StopTimer - ui64StartTimer;

	ui32ClockSpeedIndexEnd = (ui32ClockSpeedIndexEnd + 1) % NUM_EXTRA_POWER_STATS;

	if (ui32ClockSpeedIndexEnd == ui32ClockSpeedIndexStart)
	{
		ui32ClockSpeedIndexStart = (ui32ClockSpeedIndexStart + 1) % NUM_EXTRA_POWER_STATS;
	}

	ui64PreClockSpeedChangeMark = 0;
}
#endif

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
	PVRSRV_PROCESS_STATS* psProcessStats = g_psLiveList;

	while (psProcessStats != NULL)
	{
		if (psProcessStats->pid == pid)
		{
			return psProcessStats;
		}

		psProcessStats = psProcessStats->psNext;
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
	PVRSRV_PROCESS_STATS* psProcessStats = g_psDeadList;

	while (psProcessStats != NULL)
	{
		if (psProcessStats->pid == pid)
		{
			return psProcessStats;
		}

		psProcessStats = psProcessStats->psNext;
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
	PVRSRV_PROCESS_STATS* psProcessStats;
	PVRSRV_PROCESS_STATS* psProcessStatsToBeFreed;
	IMG_UINT32 ui32ItemsRemaining;

	/*
	 * We hold the lock whilst checking the list, but we'll release it
	 * before freeing memory (as that will require the lock too)!
	 */
	OSLockAcquire(g_psLinkedListLock);

	/* Check that the dead list is not bigger than the max size... */
	psProcessStats          = g_psDeadList;
	psProcessStatsToBeFreed = NULL;
	ui32ItemsRemaining      = MAX_DEAD_LIST_PROCESSES;

	while (psProcessStats != NULL  &&  ui32ItemsRemaining > 0)
	{
		ui32ItemsRemaining--;
		if (ui32ItemsRemaining == 0)
		{
			/* This is the last allowed process, cut the linked list here! */
			psProcessStatsToBeFreed = psProcessStats->psNext;
			psProcessStats->psNext  = NULL;
		}
		else
		{
			psProcessStats = psProcessStats->psNext;
		}
	}

	OSLockRelease(g_psLinkedListLock);

	/* Any processes stats remaining will need to be destroyed... */
	while (psProcessStatsToBeFreed != NULL)
	{
		PVRSRV_PROCESS_STATS* psNextProcessStats = psProcessStatsToBeFreed->psNext;

		psProcessStatsToBeFreed->psNext = NULL;
		_DestroyProcessStat(psProcessStatsToBeFreed);
		psProcessStatsToBeFreed = psNextProcessStats;
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
	_RemoveProcessStatsFromList(psProcessStats);
	_AddProcessStatsToFrontOfDeadList(psProcessStats);
} /* _MoveProcessToDeadList */

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
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
	_RemoveProcessStatsFromList(psProcessStats);
	_AddProcessStatsToFrontOfLiveList(psProcessStats);
} /* _MoveProcessToLiveList */
#endif

/*************************************************************************/ /*!
@Function       _AddProcessStatsToFrontOfLiveList
@Description    Add a statistic to the live list head.
@Input          psProcessStats  Process stats to add.
*/ /**************************************************************************/
static void
_AddProcessStatsToFrontOfLiveList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* This function should always be called under global list lock g_psLinkedListLock.
	 */
	PVR_ASSERT(psProcessStats != NULL);

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	if (g_psLiveList != NULL)
	{
		PVR_ASSERT(psProcessStats != g_psLiveList);
		OSLockAcquireNested(g_psLiveList->hLock, PROCESS_LOCK_SUBCLASS_PREV);
		g_psLiveList->psPrev = psProcessStats;
		OSLockRelease(g_psLiveList->hLock);
		psProcessStats->psNext = g_psLiveList;
	}

	g_psLiveList = psProcessStats;

	OSLockRelease(psProcessStats->hLock);
} /* _AddProcessStatsToFrontOfLiveList */

/*************************************************************************/ /*!
@Function       _AddProcessStatsToFrontOfDeadList
@Description    Add a statistic to the dead list head.
@Input          psProcessStats  Process stats to add.
*/ /**************************************************************************/
static void
_AddProcessStatsToFrontOfDeadList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != NULL);
	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	if (g_psDeadList != NULL)
	{
		PVR_ASSERT(psProcessStats != g_psDeadList);
		OSLockAcquireNested(g_psDeadList->hLock, PROCESS_LOCK_SUBCLASS_PREV);
		g_psDeadList->psPrev = psProcessStats;
		OSLockRelease(g_psDeadList->hLock);
		psProcessStats->psNext = g_psDeadList;
	}

	g_psDeadList = psProcessStats;

	OSLockRelease(psProcessStats->hLock);
} /* _AddProcessStatsToFrontOfDeadList */

/*************************************************************************/ /*!
@Function       _RemoveProcessStatsFromList
@Description    Detaches a process from either the live or dead list.
@Input          psProcessStats  Process stats to remove.
*/ /**************************************************************************/
static void
_RemoveProcessStatsFromList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != NULL);

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	/* Remove the item from the linked lists... */
	if (g_psLiveList == psProcessStats)
	{
		g_psLiveList = psProcessStats->psNext;

		if (g_psLiveList != NULL)
		{
			PVR_ASSERT(psProcessStats != g_psLiveList);
			OSLockAcquireNested(g_psLiveList->hLock, PROCESS_LOCK_SUBCLASS_PREV);
			g_psLiveList->psPrev = NULL;
			OSLockRelease(g_psLiveList->hLock);

		}
	}
	else if (g_psDeadList == psProcessStats)
	{
		g_psDeadList = psProcessStats->psNext;

		if (g_psDeadList != NULL)
		{
			PVR_ASSERT(psProcessStats != g_psDeadList);
			OSLockAcquireNested(g_psDeadList->hLock, PROCESS_LOCK_SUBCLASS_PREV);
			g_psDeadList->psPrev = NULL;
			OSLockRelease(g_psDeadList->hLock);
		}
	}
	else
	{
		PVRSRV_PROCESS_STATS* psNext = psProcessStats->psNext;
		PVRSRV_PROCESS_STATS* psPrev = psProcessStats->psPrev;

		if (psProcessStats->psNext != NULL)
		{
			PVR_ASSERT(psProcessStats != psNext);
			OSLockAcquireNested(psNext->hLock, PROCESS_LOCK_SUBCLASS_NEXT);
			psProcessStats->psNext->psPrev = psPrev;
			OSLockRelease(psNext->hLock);
		}
		if (psProcessStats->psPrev != NULL)
		{
			PVR_ASSERT(psProcessStats != psPrev);
			OSLockAcquireNested(psPrev->hLock, PROCESS_LOCK_SUBCLASS_PREV);
			psProcessStats->psPrev->psNext = psNext;
			OSLockRelease(psPrev->hLock);
		}
	}


	/* Reset the pointers in this cell, as it is not attached to anything */
	psProcessStats->psNext = NULL;
	psProcessStats->psPrev = NULL;

	OSLockRelease(psProcessStats->hLock);

} /* _RemoveProcessStatsFromList */

static PVRSRV_ERROR
_AllocateProcessStats(PVRSRV_PROCESS_STATS **ppsProcessStats, IMG_PID ownerPid)
{
	PVRSRV_ERROR eError;
	PVRSRV_PROCESS_STATS *psProcessStats;

	psProcessStats = OSAllocZMemNoStats(sizeof(PVRSRV_PROCESS_STATS));
	PVR_RETURN_IF_NOMEM(psProcessStats);

	psProcessStats->pid             = ownerPid;
	psProcessStats->ui32RefCount    = 1;

	psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]     = 1;
	psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS] = 1;

	eError = OSLockCreateNoStats(&psProcessStats->hLock);
	PVR_GOTO_IF_ERROR(eError, e0);

	*ppsProcessStats = psProcessStats;
	return PVRSRV_OK;

e0:
	OSFreeMemNoStats(psProcessStats);
	return PVRSRV_ERROR_OUT_OF_MEMORY;
}

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

	/* Free the memory statistics... */
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	while (psProcessStats->psMemoryRecords)
	{
		List_PVRSRV_MEM_ALLOC_REC_Remove(psProcessStats->psMemoryRecords);
	}
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

	PVR_ASSERT(g_psLiveList == NULL);
	PVR_ASSERT(g_psDeadList == NULL);
	PVR_ASSERT(g_psLinkedListLock == NULL);
	PVR_ASSERT(gpsSizeTrackingHashTable == NULL);
	PVR_ASSERT(bProcessStatsInitialised == IMG_FALSE);

	/* We need a lock to protect the linked lists... */
	error = OSLockCreate(&g_psLinkedListLock);
	PVR_GOTO_IF_ERROR(error, return_);

	/* We also need a lock to protect the hash table used for size tracking. */
	error = OSLockCreate(&gpsSizeTrackingHashTableLock);
	PVR_GOTO_IF_ERROR(error, detroy_linked_list_lock_);

	/* We also need a lock to protect the GlobalStat counters */
	error = OSLockCreate(&gsGlobalStats.hGlobalStatsLock);
	PVR_GOTO_IF_ERROR(error, destroy_hashtable_lock_);

	/* Flag that we are ready to start monitoring memory allocations. */

	gpsSizeTrackingHashTable = HASH_Create(HASH_INITIAL_SIZE);
	PVR_GOTO_IF_NOMEM(gpsSizeTrackingHashTable, error, destroy_stats_lock_);

	OSCachedMemSet(asClockSpeedChanges, 0, sizeof(asClockSpeedChanges));

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
		DI_ITERATOR_CB sIterator = {.pfnShow = PowerStatsPrintElements};
		/* Create power stats entry... */
		error = DICreateEntry("power_timing_stats", NULL, &sIterator, NULL,
		                      DI_ENTRY_TYPE_GENERIC, &psPowerStatsDIEntry);
		PVR_LOG_IF_ERROR(error, "DICreateEntry (2)");
	}

	{
		DI_ITERATOR_CB sIterator = {.pfnShow = GlobalStatsPrintElements};
		error = DICreateEntry("driver_stats", NULL, &sIterator, NULL,
		                      DI_ENTRY_TYPE_GENERIC, &psGlobalMemDIEntry);
		PVR_LOG_IF_ERROR(error, "DICreateEntry (3)");
	}

	return PVRSRV_OK;

destroy_stats_lock_:
	OSLockDestroy(gsGlobalStats.hGlobalStatsLock);
	gsGlobalStats.hGlobalStatsLock = NULL;
destroy_hashtable_lock_:
	OSLockDestroy(gpsSizeTrackingHashTableLock);
	gpsSizeTrackingHashTableLock = NULL;
detroy_linked_list_lock_:
	OSLockDestroy(g_psLinkedListLock);
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
	PVR_ASSERT(bProcessStatsInitialised);

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
	if (psProcStatsDIEntry != NULL)
	{
		DIDestroyEntry(psProcStatsDIEntry);
		psProcStatsDIEntry = NULL;
	}
#endif

	/* Destroy the power stats entry... */
	if (psPowerStatsDIEntry!=NULL)
	{
		DIDestroyEntry(psPowerStatsDIEntry);
		psPowerStatsDIEntry = NULL;
	}

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
		OSLockDestroy(g_psLinkedListLock);
		g_psLinkedListLock = NULL;
	}

	/* Free the live and dead lists... */
	while (g_psLiveList != NULL)
	{
		PVRSRV_PROCESS_STATS* psProcessStats = g_psLiveList;
		_RemoveProcessStatsFromList(psProcessStats);
		_DestroyProcessStat(psProcessStats);
	}

	while (g_psDeadList != NULL)
	{
		PVRSRV_PROCESS_STATS* psProcessStats = g_psDeadList;
		_RemoveProcessStatsFromList(psProcessStats);
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
		OSLockDestroy(gpsSizeTrackingHashTableLock);
		gpsSizeTrackingHashTableLock = NULL;
	}

	if (NULL != gsGlobalStats.hGlobalStatsLock)
	{
		OSLockDestroy(gsGlobalStats.hGlobalStatsLock);
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
		_RemoveProcessStatsFromList(psProcessStats);
		_AddProcessStatsToFrontOfLiveList(psProcessStats);
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
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS] = psProcessStats->ui32RefCount;
		UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS],
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]);
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
	_AddProcessStatsToFrontOfLiveList(psProcessStats);
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
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS] = psProcessStats->ui32RefCount;

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

void
PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
							 void *pvCpuVAddr,
							 IMG_CPU_PHYADDR sCpuPAddr,
							 size_t uiBytes,
							 void *pvPrivateData,
							 IMG_PID currentPid
							 DEBUG_MEMSTATS_PARAMS)
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_MEM_ALLOC_REC*  psRecord = NULL;
	PVRSRV_PROCESS_STATS*  psProcessStats;
	enum { PVRSRV_PROC_NOTFOUND,
	       PVRSRV_PROC_FOUND,
	       PVRSRV_PROC_RESURRECTED
	     } eProcSearch = PVRSRV_PROC_FOUND;

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
	psRecord->pvPrivateData    = pvPrivateData;

	psRecord->pid = currentPid;

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
		PVR_DPF((PVR_DBG_WARNING,
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
		_AddProcessStatsToFrontOfLiveList(psProcessStats);

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
			PVR_DPF((PVR_DBG_WARNING,
				 "%s: Process stat incremented on 'dead' process PID(%d)",
				 __func__, currentPid));
			/* Move process from dead list to live list */
			_MoveProcessToLiveList(psProcessStats);
		}
#endif
		OSLockRelease(g_psLinkedListLock);
	}

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	/* Insert the memory record... */
	if (psRecord != NULL)
	{
		List_PVRSRV_MEM_ALLOC_REC_Insert(&psProcessStats->psMemoryRecords, psRecord);
	}

#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	ui64InitialSize = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
#endif

	/* Update the memory watermarks... */
	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		{
			if (psRecord != NULL)
			{
				if (pvCpuVAddr == NULL)
				{
					break;
				}
				psRecord->ui64Key = (IMG_UINT64)(uintptr_t)pvCpuVAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, (IMG_UINT32)uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		{
			if (psRecord != NULL)
			{
				if (pvCpuVAddr == NULL)
				{
					break;
				}
				psRecord->ui64Key = (IMG_UINT64)(uintptr_t)pvCpuVAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, (IMG_UINT32)uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
		{
			if (psRecord != NULL)
			{
				if (pvCpuVAddr == NULL)
				{
					break;
				}
				psRecord->ui64Key = (IMG_UINT64)(uintptr_t)pvCpuVAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, (IMG_UINT32)uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
		{
			if (psRecord != NULL)
			{
				if (pvCpuVAddr == NULL)
				{
					break;
				}
				psRecord->ui64Key = (IMG_UINT64)(uintptr_t)pvCpuVAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
		{
			if (psRecord != NULL)
			{
				psRecord->ui64Key = sCpuPAddr.uiAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, (IMG_UINT32)uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
		{
			if (psRecord != NULL)
			{
				if (pvCpuVAddr == NULL)
				{
					break;
				}
				psRecord->ui64Key = (IMG_UINT64)(uintptr_t)pvCpuVAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
		{
			if (psRecord != NULL)
			{
				psRecord->ui64Key = sCpuPAddr.uiAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, (IMG_UINT32)uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
		{
			if (psRecord != NULL)
			{
				psRecord->ui64Key = sCpuPAddr.uiAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, (IMG_UINT32)uiBytes);
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
		{
			if (psRecord != NULL)
			{
				if (pvCpuVAddr == NULL)
				{
					break;
				}
				psRecord->ui64Key = (IMG_UINT64)(uintptr_t)pvCpuVAddr;
			}
			INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, (IMG_UINT32)uiBytes);
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
		psRecord      = psProcessStats->psMemoryRecords;
		while (psRecord != NULL)
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
		PVRSRV_PROCESS_STATS* psProcessStatsAlreadyChecked = psProcessStats;

		/* Search all live lists first... */
		psProcessStats = g_psLiveList;
		while (psProcessStats != NULL)
		{
			if (psProcessStats != psProcessStatsAlreadyChecked)
			{
				psRecord      = psProcessStats->psMemoryRecords;
				while (psRecord != NULL)
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
			psProcessStats = g_psDeadList;
			while (psProcessStats != NULL)
			{
				if (psProcessStats != psProcessStatsAlreadyChecked)
				{
					psRecord      = psProcessStats->psMemoryRecords;
					while (psRecord != NULL)
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

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

		_DecreaseProcStatValue(eAllocType,
		                       psProcessStats,
		                       psRecord->uiBytes);

		List_PVRSRV_MEM_ALLOC_REC_Remove(psRecord);
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
	if (psNewTrackingHashEntry)
	{
		/* Fill-in the size of the allocation and PID of the allocating process */
		psNewTrackingHashEntry->uiSizeInBytes = uiBytes;
		psNewTrackingHashEntry->uiPid = uiPid;
		OSLockAcquire(gpsSizeTrackingHashTableLock);
		/* Insert address of the new struct into the hash table */
		bRes = HASH_Insert(gpsSizeTrackingHashTable, uiCpuVAddr, (uintptr_t)psNewTrackingHashEntry);
		OSLockRelease(gpsSizeTrackingHashTableLock);
	}

	if (psNewTrackingHashEntry)
	{
		if (bRes)
		{
			PVRSRVStatsIncrMemAllocStat(eAllocType, uiBytes, uiPid);
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "*** %s : @ line %d HASH_Insert() failed!",
					 __func__, __LINE__));
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,
				 "*** %s : @ line %d Failed to alloc memory for psNewTrackingHashEntry!",
				 __func__, __LINE__));
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
	enum { PVRSRV_PROC_NOTFOUND,
	       PVRSRV_PROC_FOUND,
	       PVRSRV_PROC_RESURRECTED
	     } eProcSearch = PVRSRV_PROC_FOUND;

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
		PVR_DPF((PVR_DBG_WARNING,
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
			_AddProcessStatsToFrontOfLiveList(psProcessStats);
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
			PVR_DPF((PVR_DBG_WARNING,
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
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, (IMG_UINT32)uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, (IMG_UINT32)uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, (IMG_UINT32)uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, (IMG_UINT32)uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, (IMG_UINT32)uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, (IMG_UINT32)uiBytes);
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT, (IMG_UINT32)uiBytes);
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
                       IMG_UINT32 uiBytes)
{
#if defined(ENABLE_GPU_MEM_TRACEPOINT)
	IMG_UINT64 ui64InitialSize = GET_GPUMEM_PERPID_STAT_VALUE(psProcessStats);
#endif

	switch (eAllocType)
	{
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, (IMG_UINT32)uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, (IMG_UINT32)uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, (IMG_UINT32)uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, (IMG_UINT32)uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, (IMG_UINT32)uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, (IMG_UINT32)uiBytes);
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_TOTAL, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] == 0)
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_DMA_BUF_IMPORT:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT, (IMG_UINT32)uiBytes);
			if (psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_DMA_BUF_IMPORT] == 0)
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

	DIPrintf(psEntry,
	         "%s,%s,%s,%s,%s,%s\n",
	         "PID",
	         "MemoryUsageKMalloc",           // PVRSRV_PROCESS_STAT_TYPE_KMALLOC
	         "MemoryUsageAllocPTMemoryUMA",  // PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA
	         "MemoryUsageAllocPTMemoryLMA",  // PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA
	         "MemoryUsageAllocGPUMemLMA",    // PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES
	         "MemoryUsageAllocGPUMemUMA");   // PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = g_psLiveList;

	while (psProcessStats != NULL)
	{
		if (psProcessStats->pid != PVR_SYS_ALLOC_PID)
		{
			DIPrintf(psEntry,
			         "%d,%d,%d,%d,%d,%d\n",
			         psProcessStats->pid,
			         psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC],
			         psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA],
			         psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA],
			         psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES],
			         psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES]);
		}

		psProcessStats = psProcessStats->psNext;
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

void
PVRSRVStatsUpdateOOMStats(IMG_UINT32 ui32OOMStatType,
			  IMG_PID pidOwner)
{
	PVRSRV_PROCESS_STAT_TYPE eOOMStatType = (PVRSRV_PROCESS_STAT_TYPE) ui32OOMStatType;
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
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->i32StatValue[eOOMStatType]++;
		OSLockRelease(psProcessStats->hLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVStatsUpdateOOMStats: Process not found for Pid=%d", pidCurrent));
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateOOMStats */

PVRSRV_ERROR
PVRSRVServerUpdateOOMStats(IMG_UINT32 ui32OOMStatType,
			   IMG_PID pidOwner)
{
	if (ui32OOMStatType >= PVRSRV_PROCESS_STAT_TYPE_COUNT)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVRSRVStatsUpdateOOMStats(ui32OOMStatType, pidOwner);

	return PVRSRV_OK;
}

void
PVRSRVStatsUpdateRenderContextStats(IMG_UINT32 ui32TotalNumPartialRenders,
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
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_PRS]       += ui32TotalNumPartialRenders;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_OOMS]      += ui32TotalNumOutOfMemory;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_TA_STORES] += ui32NumTAStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_3D_STORES] += ui32Num3DStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_CDM_STORES]+= ui32NumCDMStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_TDM_STORES]+= ui32NumTDMStores;
		OSLockRelease(psProcessStats->hLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVStatsUpdateRenderContextStats: Process not found for Pid=%d", pidCurrent));
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateRenderContextStats */

void
PVRSRVStatsUpdateZSBufferStats(IMG_UINT32 ui32NumReqByApp,
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
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_APP] += ui32NumReqByApp;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ZSBUFFER_REQS_BY_FW]  += ui32NumReqByFW;
		OSLockRelease(psProcessStats->hLock);
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateZSBufferStats */

void
PVRSRVStatsUpdateFreelistStats(IMG_UINT32 ui32NumGrowReqByApp,
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

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_APP] += ui32NumGrowReqByApp;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_GROW_REQS_BY_FW]  += ui32NumGrowReqByFW;

		UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_PAGES_INIT],
				(IMG_INT32) ui32InitFLPages);

		UPDATE_MAX_VALUE(psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_FREELIST_MAX_PAGES],
				(IMG_INT32) ui32NumHighPages);

		OSLockRelease(psProcessStats->hLock);

	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateFreelistStats */


#if defined(ENABLE_DEBUGFS_PIDS)

int
GenericStatsPrintElementsLive(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVRSRV_STAT_PV_DATA *psStatType = DIGetPrivData(psEntry);
	PVRSRV_PROCESS_STATS* psProcessStats;

	PVR_UNREFERENCED_PARAMETER(pvData);

	PVR_ASSERT(psStatType->pfnStatsPrintElements != NULL);

	DIPrintf(psEntry, "%s\n", psStatType->szLiveStatsHeaderStr);

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = g_psLiveList;

	if (psProcessStats == NULL)
	{
		DIPrintf(psEntry, "No Stats to display\n%s\n", g_szSeparatorStr);
	}
	else
	{
		while (psProcessStats != NULL)
		{
			psStatType->pfnStatsPrintElements(psEntry, psProcessStats);
			psProcessStats = psProcessStats->psNext;
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

	PVR_UNREFERENCED_PARAMETER(pvData);

	PVR_ASSERT(psStatType->pfnStatsPrintElements != NULL);

	DIPrintf(psEntry, "%s\n", psStatType->szRetiredStatsHeaderStr);

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = g_psDeadList;

	if (psProcessStats == NULL)
	{
		DIPrintf(psEntry, "No Stats to display\n%s\n", g_szSeparatorStr);
	}
	else
	{
		while (psProcessStats != NULL)
		{
			psStatType->pfnStatsPrintElements(psEntry, psProcessStats);
			psProcessStats = psProcessStats->psNext;
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

	/* Loop through all the values and print them... */
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
					DIPrintf(psEntry, "%-34s%10d %8dK\n",
							 pszProcessStatType[ui32StatNumber],
							 psProcessStats->i32StatValue[ui32StatNumber],
							 psProcessStats->i32StatValue[ui32StatNumber] >> 10);
				}
				else
				{
					DIPrintf(psEntry, "%-34s%10d\n",
							 pszProcessStatType[ui32StatNumber],
							 psProcessStats->i32StatValue[ui32StatNumber]);
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
							IMG_UINT32 ui32OpSeqNum,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_DEV_PHYADDR sDevPAddr,
							IMG_UINT32 eFenceOpType,
#endif
							IMG_DEVMEM_SIZE_T uiOffset,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_UINT64 ui64ExecuteTime,
							IMG_BOOL bUserModeFlush,
							IMG_BOOL bIsFence,
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
		psProcessStats->asCacheOp[Idx].eFenceOpType = eFenceOpType;
#endif
		psProcessStats->asCacheOp[Idx].uiOffset = uiOffset;
		psProcessStats->asCacheOp[Idx].uiSize = uiSize;
		psProcessStats->asCacheOp[Idx].bUserModeFlush = bUserModeFlush;
		psProcessStats->asCacheOp[Idx].ui64ExecuteTime = ui64ExecuteTime;
		psProcessStats->asCacheOp[Idx].ui32OpSeqNum = ui32OpSeqNum;
		psProcessStats->asCacheOp[Idx].bIsFence = bIsFence;

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
		"%-10s %-10s %-5s %-16s %-16s %-10s %-10s %-12s %-12s\n"
	#define CACHEOP_RI_PRINTF_FENCE	 \
		"%-10s %-10s %-5s %-16s %-16s %-10s %-10s %-12llu 0x%-10x\n"
	#define CACHEOP_RI_PRINTF		\
		"%-10s %-10s %-5s 0x%-14llx 0x%-14llx 0x%-8llx 0x%-8llx %-12llu 0x%-10x\n"
#else
	#define CACHEOP_PRINTF_HEADER	\
		"%-10s %-10s %-5s %-10s %-10s %-12s %-12s\n"
	#define CACHEOP_PRINTF_FENCE	 \
		"%-10s %-10s %-5s %-10s %-10s %-12llu 0x%-10x\n"
	#define CACHEOP_PRINTF			\
		"%-10s %-10s %-5s 0x%-8llx 0x%-8llx %-12llu 0x%-10x\n"
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
					"Time (us)",
					"SeqNo");

	/* Take a snapshot of write index, read backwards in buffer
	   and wrap round at boundary */
	i32WriteIdx = psProcessStats->uiCacheOpWriteIndex;
	for (i32ReadIdx = DECREMENT_CACHEOP_STAT_IDX_WRAP(i32WriteIdx);
		 i32ReadIdx != i32WriteIdx;
		 i32ReadIdx = DECREMENT_CACHEOP_STAT_IDX_WRAP(i32ReadIdx))
	{
		IMG_UINT64 ui64ExecuteTime;

		if (! psProcessStats->asCacheOp[i32ReadIdx].ui32OpSeqNum)
		{
			break;
		}

		ui64ExecuteTime = psProcessStats->asCacheOp[i32ReadIdx].ui64ExecuteTime;

		if (psProcessStats->asCacheOp[i32ReadIdx].bIsFence)
		{
			IMG_CHAR *pszFenceType = "";
			pszCacheOpType = "Fence";

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
			switch (psProcessStats->asCacheOp[i32ReadIdx].eFenceOpType)
			{
				case RGXFWIF_DM_GP:
					pszFenceType = "GP";
					break;

				case RGXFWIF_DM_TDM:
					/* Also case RGXFWIF_DM_2D: */
					pszFenceType = "TDM/2D";
					break;

				case RGXFWIF_DM_GEOM:
					pszFenceType = "GEOM";
					break;

				case RGXFWIF_DM_3D:
					pszFenceType = "3D";
					break;

				case RGXFWIF_DM_CDM:
					pszFenceType = "CDM";
					break;

				default:
					PVR_ASSERT(0);
					break;
			}
#endif

			DIPrintf(psEntry,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							CACHEOP_RI_PRINTF_FENCE,
#else
							CACHEOP_PRINTF_FENCE,
#endif
							pszCacheOpType,
							pszFenceType,
							"",
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							"",
							"",
#endif
							"",
							"",
							ui64ExecuteTime,
							psProcessStats->asCacheOp[i32ReadIdx].ui32OpSeqNum);
		}
		else
		{
			IMG_DEVMEM_SIZE_T ui64NumOfPages;

			ui64NumOfPages = psProcessStats->asCacheOp[i32ReadIdx].uiSize >> OSGetPageShift();
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
							ui64ExecuteTime,
							psProcessStats->asCacheOp[i32ReadIdx].ui32OpSeqNum);
		}
	}
} /* CacheOpStatsPrintElements */
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
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
	PVRSRV_MEM_ALLOC_REC *psRecord;
	IMG_UINT32 ui32ItemNumber;

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

	psRecord = psProcessStats->psMemoryRecords;
	if (psRecord == NULL)
	{
		DIPrintf(psEntry, "%-5d\n", psProcessStats->pid);
	}

	while (psRecord != NULL)
	{
		IMG_BOOL bPrintStat = IMG_TRUE;

		DIPrintf(psEntry, "%-5d  ", psProcessStats->pid);

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
		/* Move to next record... */
		psRecord = psRecord->psNext;
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

static IMG_UINT32	ui32FirmwareStartTimestamp;
static IMG_UINT64	ui64FirmwareIdleDuration;

void SetFirmwareStartTime(IMG_UINT32 ui32Time)
{
	ui32FirmwareStartTimestamp = UPDATE_TIME(ui32FirmwareStartTimestamp, ui32Time);
}

void SetFirmwareHandshakeIdleTime(IMG_UINT64 ui64Duration)
{
	ui64FirmwareIdleDuration = UPDATE_TIME(ui64FirmwareIdleDuration, ui64Duration);
}

static INLINE void PowerStatsPrintGroup(IMG_UINT32 *pui32Stats,
                                        OSDI_IMPL_ENTRY *psEntry,
                                        PVRSRV_POWER_STAT_TYPE eForced,
                                        PVRSRV_POWER_STAT_TYPE ePowerOn)
{
	IMG_UINT32 ui32Index;

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, PRE_POWER, DEVICE);
	DIPrintf(psEntry, "  Pre-Device:  %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, PRE_POWER, SYSTEM);
	DIPrintf(psEntry, "  Pre-System:  %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, POST_POWER, SYSTEM);
	DIPrintf(psEntry, "  Post-System: %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, POST_POWER, DEVICE);
	DIPrintf(psEntry, "  Post-Device: %9u\n", pui32Stats[ui32Index]);
}

int PowerStatsPrintElements(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	IMG_UINT32 *pui32Stats = &aui32PowerTimingStats[0];
	IMG_UINT32 ui32Idx;

	PVR_UNREFERENCED_PARAMETER(pvData);

	DIPrintf(psEntry, "Forced Power-on Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, psEntry, FORCED, POWER_ON);
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Forced Power-off Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, psEntry, FORCED, POWER_OFF);
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Not Forced Power-on Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, psEntry, NOT_FORCED, POWER_ON);
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Not Forced Power-off Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, psEntry, NOT_FORCED, POWER_OFF);
	DIPrintf(psEntry, "\n");


	DIPrintf(psEntry, "FW bootup time (timer ticks): %u\n", ui32FirmwareStartTimestamp);
	DIPrintf(psEntry, "Host Acknowledge Time for FW Idle Signal (timer ticks): %u\n", (IMG_UINT32)(ui64FirmwareIdleDuration));
	DIPrintf(psEntry, "\n");

	DIPrintf(psEntry, "Last %d Clock Speed Change Timers (nanoseconds):\n", NUM_EXTRA_POWER_STATS);
	DIPrintf(psEntry, "Prepare DVFS\tDVFS Change\tPost DVFS\n");

	for (ui32Idx = ui32ClockSpeedIndexStart; ui32Idx !=ui32ClockSpeedIndexEnd; ui32Idx = (ui32Idx + 1) % NUM_EXTRA_POWER_STATS)
	{
		DIPrintf(psEntry, "%12llu\t%11llu\t%9llu\n",asClockSpeedChanges[ui32Idx].ui64PreClockSpeedChangeDuration,
						 asClockSpeedChanges[ui32Idx].ui64BetweenPreEndingAndPostStartingDuration,
						 asClockSpeedChanges[ui32Idx].ui64PostClockSpeedChangeDuration);
	}

	return 0;
} /* PowerStatsPrintElements */

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
			DIPrintf(psEntry, "%-34s%10d\n",
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
PVRSRV_ERROR PVRSRVFindProcessMemStats(IMG_PID pid, IMG_UINT32 ui32ArrSize, IMG_BOOL bAllProcessStats, IMG_UINT32 *pui32MemoryStats)
{
	IMG_INT i;
	PVRSRV_PROCESS_STATS* psProcessStats;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pui32MemoryStats, "pui32MemoryStats");

	if (bAllProcessStats)
	{
		PVR_LOG_RETURN_IF_FALSE(ui32ArrSize == PVRSRV_DRIVER_STAT_TYPE_COUNT,
				  "MemStats array size is incorrect",
				  PVRSRV_ERROR_INVALID_PARAMS);

		OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

		for (i = 0; i < ui32ArrSize; i++)
		{
			pui32MemoryStats[i] = GET_GLOBAL_STAT_VALUE(i);
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
		pui32MemoryStats[i] = psProcessStats->i32StatValue[i];
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
@Output         pui32TotalMem                   Total memory usage for all live
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
PVRSRV_ERROR PVRSRVGetProcessMemUsage(IMG_UINT32 *pui32TotalMem,
									  IMG_UINT32 *pui32NumberOfLivePids,
									  PVRSRV_PER_PROCESS_MEM_USAGE **ppsPerProcessMemUsageData)
{
	IMG_UINT32 ui32Counter = 0;
	IMG_UINT32 ui32NumberOfLivePids = 0;
	PVRSRV_ERROR eError = PVRSRV_ERROR_PROCESS_NOT_FOUND;
	PVRSRV_PROCESS_STATS* psProcessStats = NULL;
	PVRSRV_PER_PROCESS_MEM_USAGE* psPerProcessMemUsageData = NULL;

	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

	*pui32TotalMem = GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_KMALLOC) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_VMALLOC) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_LMA) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_GPUMEM_UMA) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_UMA) +
		GET_GLOBAL_STAT_VALUE(PVRSRV_DRIVER_STAT_TYPE_ALLOC_PT_MEMORY_LMA);

	OSLockRelease(gsGlobalStats.hGlobalStatsLock);

	OSLockAcquire(g_psLinkedListLock);
	psProcessStats = g_psLiveList;

	while (psProcessStats != NULL)
	{
		psProcessStats = psProcessStats->psNext;
		ui32NumberOfLivePids++;
	}

	if (ui32NumberOfLivePids > 0)
	{
		/* Use OSAllocZMemNoStats to prevent deadlock. */
		psPerProcessMemUsageData = OSAllocZMemNoStats(ui32NumberOfLivePids * sizeof(*psPerProcessMemUsageData));

		if (psPerProcessMemUsageData)
		{
			psProcessStats = g_psLiveList;

			while (psProcessStats != NULL)
			{
				OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

				psPerProcessMemUsageData[ui32Counter].ui32Pid = (IMG_UINT32)psProcessStats->pid;

				psPerProcessMemUsageData[ui32Counter].ui32KernelMemUsage = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] +
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC];

				psPerProcessMemUsageData[ui32Counter].ui32GraphicsMemUsage = psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] +
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] +
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] +
				psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES];

				OSLockRelease(psProcessStats->hLock);
				psProcessStats = psProcessStats->psNext;
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
