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
#include "connection_server.h"
#include "pvrsrv.h"

/*
 *  Maximum history of process statistics that will be kept.
 */
#define MAX_DEAD_LIST_PROCESSES  (10)

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
	PVRSRV_PROCESS_STAT_TYPE_KMALLOC_MAX,
	PVRSRV_PROCESS_STAT_TYPE_VMALLOC,
	PVRSRV_PROCESS_STAT_TYPE_VMALLOC_MAX,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA_MAX,
	PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA,
	PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA_MAX,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA_MAX,
	PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA,
	PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA_MAX,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES_MAX,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES,
	PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES_MAX,
	PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES,
	PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES_MAX,

	/* Must be the last enum...*/
	PVRSRV_PROCESS_STAT_TYPE_COUNT
} PVRSRV_PROCESS_STAT_TYPE;

static const IMG_CHAR *const pszProcessStatFmt[PVRSRV_PROCESS_STAT_TYPE_COUNT] = {
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
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
	"MemoryUsageKMalloc                %10d\n", /* PVRSRV_STAT_TYPE_KMALLOC */
	"MemoryUsageKMallocMax             %10d\n", /* PVRSRV_STAT_TYPE_MAX_KMALLOC */
	"MemoryUsageVMalloc                %10d\n", /* PVRSRV_STAT_TYPE_VMALLOC */
	"MemoryUsageVMallocMax             %10d\n", /* PVRSRV_STAT_TYPE_MAX_VMALLOC */
#else
	"","","","",                                /* Empty strings if these stats are not logged */
#endif
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
};

/* structure used in hash table to track statistic entries */
typedef struct{
	size_t	   uiSizeInBytes;
	IMG_PID	   uiPid;
}_PVR_STATS_TRACKING_HASH_ENTRY;

/* Function used internally to decrement tracked per-process statistic entries */
static void _StatsDecrMemTrackedStat(_PVR_STATS_TRACKING_HASH_ENTRY *psTrackingHashEntry,
                                    PVRSRV_MEM_ALLOC_TYPE eAllocType);

/*
 *  Functions for printing the information stored...
 */
void  ProcessStatsPrintElements(void *pvFile,
								void *pvStatPtr,
								OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
void RawProcessStatsPrintElements(void *pvFile,
                                  void *pvStatPtr,
                                  OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);
#endif

void  MemStatsPrintElements(void *pvFile,
							void *pvStatPtr,
							OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

void  RIMemStatsPrintElements(void *pvFile,
							  void *pvStatPtr,
							  OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

void  PowerStatsPrintElements(void *pvFile,
							  void *pvStatPtr,
							  OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

void  GlobalStatsPrintElements(void *pvFile,
							   void *pvStatPtr,
							   OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

void  CacheOpStatsPrintElements(void *pvFile,
							  void *pvStatPtr,
							  OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf);

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
static void StripBadChars( IMG_CHAR *psStr);
#endif

/*
 *  Macros for updating stat values.
 */
#define UPDATE_MAX_VALUE(a,b)					do { if ((b) > (a)) {(a) = (b);} } while(0)
#define INCREASE_STAT_VALUE(ptr,var,val)		do { (ptr)->i32StatValue[(var)] += (val); if ((ptr)->i32StatValue[(var)] > (ptr)->i32StatValue[(var##_MAX)]) {(ptr)->i32StatValue[(var##_MAX)] = (ptr)->i32StatValue[(var)];} } while(0)
#define INCREASE_GLOBAL_STAT_VALUE(var,val)		do { (var) += (val); if ((var) > (var##Max)) {(var##Max) = (var);} } while(0)
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
/* Allow stats to go negative */
#define DECREASE_STAT_VALUE(ptr,var,val)		do { (ptr)->i32StatValue[(var)] -= (val); } while(0)
#define DECREASE_GLOBAL_STAT_VALUE(var,val)		do { (var) -= (val); } while(0)
#else
#define DECREASE_STAT_VALUE(ptr,var,val)		do { if ((ptr)->i32StatValue[(var)] >= (val)) { (ptr)->i32StatValue[(var)] -= (val); } else { (ptr)->i32StatValue[(var)] = 0; } } while(0)
#define DECREASE_GLOBAL_STAT_VALUE(var,val)		do { if ((var) >= (val)) { (var) -= (val); } else { (var) = 0; } } while(0)
#endif
#define MAX_CACHEOP_STAT 16
#define INCREMENT_CACHEOP_STAT_IDX_WRAP(x) ((x+1) >= MAX_CACHEOP_STAT ? 0 : (x+1))
#define DECREMENT_CACHEOP_STAT_IDX_WRAP(x) ((x-1) < 0 ? (MAX_CACHEOP_STAT-1) : (x-1))

/*
 * Structures for holding statistics...
 */
typedef enum
{
	PVRSRV_STAT_STRUCTURE_PROCESS = 1,
	PVRSRV_STAT_STRUCTURE_RENDER_CONTEXT = 2,
	PVRSRV_STAT_STRUCTURE_MEMORY = 3,
	PVRSRV_STAT_STRUCTURE_RIMEMORY = 4,
	PVRSRV_STAT_STRUCTURE_CACHEOP = 5
} PVRSRV_STAT_STRUCTURE_TYPE;

#define MAX_PROC_NAME_LENGTH   (32)

typedef struct _PVRSRV_PROCESS_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE			eStructureType;

	/* Linked list pointers */
	struct _PVRSRV_PROCESS_STATS_*		psNext;
	struct _PVRSRV_PROCESS_STATS_*		psPrev;

	/* Create per process lock that need to be held
	 * to edit of its members */
	POS_LOCK							hLock;

	/* OS level process ID */
	IMG_PID								pid;
	IMG_UINT32							ui32RefCount;
	IMG_UINT32							ui32MemRefCount;

	/* Folder name used to store the statistic */
	IMG_CHAR							szFolderName[MAX_PROC_NAME_LENGTH];

	/* OS specific data */
	void								*pvOSPidFolderData;
	void								*pvOSPidEntryData;

	/* Stats... */
	IMG_INT32							i32StatValue[PVRSRV_PROCESS_STAT_TYPE_COUNT];
	IMG_UINT32							ui32StatAllocFlags;

#if defined(DEBUG)
	struct _CACHEOP_STRUCT_  {
		PVRSRV_CACHE_OP uiCacheOp;
#if defined(PVR_RI_DEBUG)
		IMG_DEV_VIRTADDR sDevVAddr;
		RGXFWIF_DM eFenceOpType;
#endif
		IMG_DEVMEM_SIZE_T uiOffset;
		IMG_DEVMEM_SIZE_T uiSize;
		IMG_UINT64 ui64ExecuteTime;
		IMG_BOOL bRangeBasedFlush;
		IMG_BOOL bUserModeFlush;
		IMG_UINT32 ui32OpSeqNum;
		IMG_BOOL bHasTimeline;
		IMG_BOOL bIsFence;
		IMG_PID ownerPid;
	} 									asCacheOp[MAX_CACHEOP_STAT];
	IMG_INT32 							uiCacheOpWriteIndex;
	struct _PVRSRV_CACHEOP_STATS_*		psCacheOpStats;
#endif

	/* Other statistics structures */
	struct _PVRSRV_MEMORY_STATS_*		psMemoryStats;
	struct _PVRSRV_RI_MEMORY_STATS_*	psRIMemoryStats;
} PVRSRV_PROCESS_STATS;

typedef struct _PVRSRV_MEM_ALLOC_REC_
{
    PVRSRV_MEM_ALLOC_TYPE  eAllocType;
    IMG_UINT64			ui64Key;
	void				*pvCpuVAddr;
	IMG_CPU_PHYADDR		sCpuPAddr;
	size_t				uiBytes;
	void				*pvPrivateData;
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) && defined(DEBUG)
	void				*pvAllocdFromFile;
	IMG_UINT32			ui32AllocdFromLine;
#endif
	IMG_PID				pid;
	struct _PVRSRV_MEM_ALLOC_REC_	*psNext;
	struct _PVRSRV_MEM_ALLOC_REC_	**ppsThis;
} PVRSRV_MEM_ALLOC_REC;

typedef struct _PVRSRV_MEMORY_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE  eStructureType;

	/* OS specific data */
	void						*pvOSMemEntryData;

	/* Stats... */
	PVRSRV_MEM_ALLOC_REC		*psMemoryRecords;
} PVRSRV_MEMORY_STATS;

typedef struct _PVRSRV_RI_MEMORY_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE  eStructureType;

	/* OS level process ID */
	IMG_PID						pid;

	/* OS specific data */
	void						*pvOSRIMemEntryData;
} PVRSRV_RI_MEMORY_STATS;

typedef struct _PVRSRV_CACHEOP_STATS_ {
	/* Structure type (must be first!) */
	PVRSRV_STAT_STRUCTURE_TYPE  eStructureType;

	/* OS specific data */
	void						*pvOSCacheOpEntryData;
} PVRSRV_CACHEOP_STATS;

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
static PVRSRV_PROCESS_STATS*  g_psLiveList = NULL;
static PVRSRV_PROCESS_STATS*  g_psDeadList = NULL;

static POS_LOCK g_psLinkedListLock = NULL;
/* Lockdep feature in the kernel cannot differentiate between different instances of same lock type.
 * This allows it to group all such instances of the same lock type under one class
 * The consequence of this is that, if lock acquisition is nested on different instances, it generates
 * a false warning message about the possible occurrence of deadlock due to recursive lock acquisition.
 * Hence we create the following sub classes to explicitly appraise Lockdep of such safe lock nesting */
#define PROCESS_LOCK_SUBCLASS_CURRENT	1
#define PROCESS_LOCK_SUBCLASS_PREV 		2
#define PROCESS_LOCK_SUBCLASS_NEXT 		3
/*
 * Pointer to OS folder to hold PID folders.
 */
static IMG_CHAR *pszOSLivePidFolderName = "pid";
static IMG_CHAR *pszOSDeadPidFolderName = "pids_retired";
static void *pvOSLivePidFolder = NULL;
static void *pvOSDeadPidFolder = NULL;
#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
static void *pvOSProcStats = NULL;
#endif

/* global driver-data folders */
typedef struct _GLOBAL_STATS_
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
	IMG_UINT32 ui32MemoryUsageAllocGPUMemUMAPool;
	IMG_UINT32 ui32MemoryUsageAllocGPUMemUMAPoolMax;
	IMG_UINT32 ui32MemoryUsageMappedGPUMemUMA_LMA;
	IMG_UINT32 ui32MemoryUsageMappedGPUMemUMA_LMAMax;
	POS_LOCK   hGlobalStatsLock;
} GLOBAL_STATS;

static void *pvOSGlobalMemEntryRef = NULL;
static IMG_CHAR* const pszDriverStatFilename = "driver_stats";
static GLOBAL_STATS gsGlobalStats;

#define HASH_INITIAL_SIZE 5
/* A hash table used to store the size of any vmalloc'd allocation
 * against its address (not needed for kmallocs as we can use ksize()) */
static HASH_TABLE* gpsSizeTrackingHashTable;
static POS_LOCK	 gpsSizeTrackingHashTableLock;

static void _AddProcessStatsToFrontOfDeadList(PVRSRV_PROCESS_STATS* psProcessStats);
static void _AddProcessStatsToFrontOfLiveList(PVRSRV_PROCESS_STATS* psProcessStats);
static IMG_UINT32 _PVRSRVIncrMemStatRefCount(void *pvStatPtr);
static IMG_UINT32 _PVRSRVDecrMemStatRefCount(void *pvStatPtr);
static void _DestroyProcessStat(PVRSRV_PROCESS_STATS* psProcessStats);
static void _RemoveProcessStatsFromList(PVRSRV_PROCESS_STATS* psProcessStats);
static void _RemoveOSStatisticEntries(PVRSRV_PROCESS_STATS* psProcessStats);
static void _CreateOSStatisticEntries(PVRSRV_PROCESS_STATS* psProcessStats, void *pvOSPidFolder);
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
	((time) > 0 ? MEAN_TIME((time),(newtime)) : (newtime))

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

static void *pvOSPowerStatsEntryData = NULL;

void InsertPowerTimeStatistic(IMG_UINT64 ui64SysStartTime, IMG_UINT64 ui64SysEndTime,
                              IMG_UINT64 ui64DevStartTime, IMG_UINT64 ui64DevEndTime,
                              IMG_BOOL bForced, IMG_BOOL bPowerOn, IMG_BOOL bPrePower)
{
	IMG_UINT32 *pui32Stat;
	IMG_UINT64 ui64DeviceDiff = ui64DevEndTime - ui64DevStartTime;
	IMG_UINT64 ui64SystemDiff = ui64SysEndTime - ui64SysStartTime;
	IMG_UINT32 ui32Index;

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

typedef struct _EXTRA_POWER_STATS_
{
	IMG_UINT64	ui64PreClockSpeedChangeDuration;
	IMG_UINT64	ui64BetweenPreEndingAndPostStartingDuration;
	IMG_UINT64	ui64PostClockSpeedChangeDuration;
} EXTRA_POWER_STATS;

#define NUM_EXTRA_POWER_STATS	10

static EXTRA_POWER_STATS asClockSpeedChanges[NUM_EXTRA_POWER_STATS];
static IMG_UINT32	ui32ClockSpeedIndexStart = 0, ui32ClockSpeedIndexEnd = 0;

static IMG_UINT64 ui64PreClockSpeedChangeMark = 0;

void InsertPowerTimeStatisticExtraPre(IMG_UINT64 ui64StartTimer, IMG_UINT64 ui64Stoptimer)
{
	asClockSpeedChanges[ui32ClockSpeedIndexEnd].ui64PreClockSpeedChangeDuration = ui64Stoptimer - ui64StartTimer;

	ui64PreClockSpeedChangeMark = OSClockus();

	return ;
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

	return;
}

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
	PVRSRV_PROCESS_STATS*  psProcessStats = g_psLiveList;

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
	PVRSRV_PROCESS_STATS*  psProcessStats = g_psDeadList;

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
	PVRSRV_PROCESS_STATS*  psProcessStats = _FindProcessStatsInLiveList(pid);

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
	PVRSRV_PROCESS_STATS*  psProcessStats;
	PVRSRV_PROCESS_STATS*  psProcessStatsToBeFreed;
	IMG_UINT32  ui32ItemsRemaining;

	/*
	 *  We hold the lock whilst checking the list, but we'll release it
	 *  before freeing memory (as that will require the lock too)!
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
		PVRSRV_PROCESS_STATS*  psNextProcessStats = psProcessStatsToBeFreed->psNext;

		psProcessStatsToBeFreed->psNext = NULL;
		_RemoveOSStatisticEntries(psProcessStatsToBeFreed);
		psProcessStatsToBeFreed = psNextProcessStats;
	}
} /* _CompressMemoryUsage */

/* These functions move the process stats from the live to the dead list.
 * _MoveProcessToDeadList moves the entry in the global lists and
 * it needs to be protected by g_psLinkedListLock.
 * _MoveProcessToDeadListDebugFS performs the OS calls and it
 * shouldn't be used under g_psLinkedListLock because this could generate a
 * lockdep warning. */
static void
_MoveProcessToDeadList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Take the element out of the live list and append to the dead list... */
	_RemoveProcessStatsFromList(psProcessStats);
	_AddProcessStatsToFrontOfDeadList(psProcessStats);
} /* _MoveProcessToDeadList */

static void
_MoveProcessToDeadListDebugFS(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Transfer the OS entries to the folder for dead processes... */
	_RemoveOSStatisticEntries(psProcessStats);
	_CreateOSStatisticEntries(psProcessStats, pvOSDeadPidFolder);
} /* _MoveProcessToDeadListDebugFS */

/* These functions move the process stats from the dead to the live list.
 * _MoveProcessToLiveList moves the entry in the global lists and
 * it needs to be protected by g_psLinkedListLock.
 * _MoveProcessToLiveListDebugFS performs the OS calls and it
 * shouldn't be used under g_psLinkedListLock because this could generate a
 * lockdep warning. */
static void
_MoveProcessToLiveList(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Take the element out of the live list and append to the dead list... */
	_RemoveProcessStatsFromList(psProcessStats);
	_AddProcessStatsToFrontOfLiveList(psProcessStats);
} /* _MoveProcessToLiveList */

static void
_MoveProcessToLiveListDebugFS(PVRSRV_PROCESS_STATS* psProcessStats)
{
	/* Transfer the OS entries to the folder for live processes... */
	_RemoveOSStatisticEntries(psProcessStats);
	_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);
} /* _MoveProcessToLiveListDebugFS */

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
		g_psLiveList->psPrev     = psProcessStats;
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
		g_psDeadList->psPrev     = psProcessStats;
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
		PVRSRV_PROCESS_STATS*  psNext = psProcessStats->psNext;
		PVRSRV_PROCESS_STATS*  psPrev = psProcessStats->psPrev;

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

/*************************************************************************/ /*!
@Function       _CreateOSStatisticEntries
@Description    Create all OS entries for this statistic.
@Input          psProcessStats  Process stats to destroy.
@Input          pvOSPidFolder   Pointer to OS folder to place the entries in.
*/ /**************************************************************************/
static void
_CreateOSStatisticEntries(PVRSRV_PROCESS_STATS* psProcessStats,
						  void *pvOSPidFolder)
{
	void								*pvOSPidFolderData;
	void								*pvOSPidEntryData;
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	void								*pvOSMemEntryData;
#endif
#if defined(PVR_RI_DEBUG)
	void								*pvOSRIMemEntryData;
#endif
#if defined(DEBUG)
	void								*pvOSCacheOpEntryData;
#endif

	PVR_ASSERT(psProcessStats != NULL);

	pvOSPidFolderData = OSCreateStatisticFolder(psProcessStats->szFolderName, pvOSPidFolder);
	pvOSPidEntryData  = OSCreateStatisticEntry("process_stats",
												pvOSPidFolderData,
												ProcessStatsPrintElements,
												_PVRSRVIncrMemStatRefCount,
												_PVRSRVDecrMemStatRefCount,
												(void *) psProcessStats);

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	pvOSMemEntryData = OSCreateStatisticEntry("mem_area",
											  pvOSPidFolderData,
											  MemStatsPrintElements,
											  NULL,
											  NULL,
											  (void *) psProcessStats->psMemoryStats);
#endif

#if defined(PVR_RI_DEBUG)
	pvOSRIMemEntryData = OSCreateStatisticEntry("ri_mem_area",
												 pvOSPidFolderData,
												 RIMemStatsPrintElements,
												 NULL,
												 NULL,
												 (void *) psProcessStats->psRIMemoryStats);
#endif

#if defined(DEBUG)
	pvOSCacheOpEntryData = OSCreateStatisticEntry("cache_ops_exec",
												 pvOSPidFolderData,
												 CacheOpStatsPrintElements,
												 NULL,
												 NULL,
												 (void *) psProcessStats);
#endif

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	psProcessStats->pvOSPidFolderData = pvOSPidFolderData;
	psProcessStats->pvOSPidEntryData  = pvOSPidEntryData;
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	psProcessStats->psMemoryStats->pvOSMemEntryData = pvOSMemEntryData;
#endif
#if defined(PVR_RI_DEBUG)
	psProcessStats->psRIMemoryStats->pvOSRIMemEntryData = pvOSRIMemEntryData;
#endif
#if defined(DEBUG)
	psProcessStats->psCacheOpStats->pvOSCacheOpEntryData = pvOSCacheOpEntryData;
#endif

	OSLockRelease(psProcessStats->hLock);
} /* _CreateOSStatisticEntries */

/*************************************************************************/ /*!
@Function       _RemoveOSStatisticEntries
@Description    Removed all OS entries used by this statistic.
@Input          psProcessStats  Process stats to destroy.
*/ /**************************************************************************/
static void
_RemoveOSStatisticEntries(PVRSRV_PROCESS_STATS* psProcessStats)
{
	PVR_ASSERT(psProcessStats != NULL);

#if defined(DEBUG)
	OSRemoveStatisticEntry(psProcessStats->psCacheOpStats->pvOSCacheOpEntryData);
#endif

#if defined(PVR_RI_DEBUG)
	OSRemoveStatisticEntry(psProcessStats->psRIMemoryStats->pvOSRIMemEntryData);
#endif

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	OSRemoveStatisticEntry(psProcessStats->psMemoryStats->pvOSMemEntryData);
#endif

	if( psProcessStats->pvOSPidEntryData != NULL)
	{
		OSRemoveStatisticEntry(psProcessStats->pvOSPidEntryData);
	}
	if( psProcessStats->pvOSPidFolderData != NULL)
	{
		OSRemoveStatisticFolder(&psProcessStats->pvOSPidFolderData);
	}

} /* _RemoveOSStatisticEntries */

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
	while (psProcessStats->psMemoryStats->psMemoryRecords)
	{
		List_PVRSRV_MEM_ALLOC_REC_Remove(psProcessStats->psMemoryStats->psMemoryRecords);
	}
	OSFreeMemNoStats(psProcessStats->psMemoryStats);
#endif
#if defined(PVR_RI_DEBUG)
	OSFreeMemNoStats(psProcessStats->psRIMemoryStats);
#endif
	OSLockRelease(psProcessStats->hLock);

	/*Destroy the lock */
	OSLockDestroyNoStats(psProcessStats->hLock);

	/* Free the memory... */
	OSFreeMemNoStats(psProcessStats);
} /* _DestroyProcessStat */

static IMG_UINT32 _PVRSRVIncrMemStatRefCount(void *pvStatPtr)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) pvStatPtr;
	IMG_UINT32 ui32Res = 0;

	switch (*peStructureType)
	{
		case PVRSRV_STAT_STRUCTURE_PROCESS:
		{
			OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
			ui32Res = ++psProcessStats->ui32MemRefCount;
			OSLockRelease(psProcessStats->hLock);
			break;
		}
		default:
		{
			/* _PVRSRVIncrMemStatRefCount was passed a pointer to an unrecognised struct */
			PVR_ASSERT(0);
			break;
		}
	}

	return ui32Res;
}

static IMG_UINT32 _PVRSRVDecrMemStatRefCount(void *pvStatPtr)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) pvStatPtr;
    IMG_UINT32 ui32Res = 0;

	switch (*peStructureType)
	{
		case PVRSRV_STAT_STRUCTURE_PROCESS:
		{
			OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
			/* Decrement stat memory refCount and free if now zero */
			ui32Res = --psProcessStats->ui32MemRefCount;
			OSLockRelease(psProcessStats->hLock);
			if (ui32Res == 0)
			{
				_DestroyProcessStat(psProcessStats);
			}
			break;
		}
		default:
		{
			/* _PVRSRVDecrMemStatRefCount was passed a pointer to an unrecognised struct */
			PVR_ASSERT(0);
			break;
		}
	}
	return ui32Res;
}

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
	error = OSLockCreate(&g_psLinkedListLock, LOCK_TYPE_NONE);
	if (error == PVRSRV_OK)
	{
		/* We also need a lock to protect the hash table used for size tracking.. */
		error = OSLockCreate(&gpsSizeTrackingHashTableLock, LOCK_TYPE_NONE);

		if (error != PVRSRV_OK)
		{
			goto e0;
		}

		/* We also need a lock to protect the GlobalStat counters */
		error = OSLockCreate(&gsGlobalStats.hGlobalStatsLock, LOCK_TYPE_NONE);
		if (error != PVRSRV_OK)
		{
			goto e1;
		}

		/* Create a pid folders for putting the PID files in... */
		pvOSLivePidFolder = OSCreateStatisticFolder(pszOSLivePidFolderName, NULL);
		pvOSDeadPidFolder = OSCreateStatisticFolder(pszOSDeadPidFolderName, NULL);
#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
		pvOSProcStats = OSCreateRawStatisticEntry("memtrack_stats", NULL,
		                                          RawProcessStatsPrintElements);
#endif

		/* Create power stats entry... */
		pvOSPowerStatsEntryData = OSCreateStatisticEntry("power_timing_stats",
														 NULL,
														 PowerStatsPrintElements,
														 NULL,
														 NULL,
														 NULL);

		pvOSGlobalMemEntryRef = OSCreateStatisticEntry(pszDriverStatFilename,
													   NULL,
													   GlobalStatsPrintElements,
													   NULL,
													   NULL,
													   NULL);

		/* Flag that we are ready to start monitoring memory allocations. */

		gpsSizeTrackingHashTable = HASH_Create(HASH_INITIAL_SIZE);

		OSCachedMemSet(asClockSpeedChanges, 0, sizeof(asClockSpeedChanges));

		bProcessStatsInitialised = IMG_TRUE;
	}
	return error;
e1:
	OSLockDestroy(gpsSizeTrackingHashTableLock);
	gpsSizeTrackingHashTableLock = NULL;
e0:
	OSLockDestroy(g_psLinkedListLock);
	g_psLinkedListLock = NULL;
	return error;

} /* PVRSRVStatsInitialise */

/*************************************************************************/ /*!
@Function       PVRSRVStatsDestroy
@Description    Method for destroying the statistics module data.
*/ /**************************************************************************/
void
PVRSRVStatsDestroy(void)
{
	PVR_ASSERT(bProcessStatsInitialised == IMG_TRUE);

	/* Stop monitoring memory allocations... */
	bProcessStatsInitialised = IMG_FALSE;

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
	if (pvOSProcStats)
	{
		OSRemoveRawStatisticEntry(pvOSProcStats);
		pvOSProcStats = NULL;
	}
#endif

	/* Destroy the power stats entry... */
	if (pvOSPowerStatsEntryData!=NULL)
	{
		OSRemoveStatisticEntry(pvOSPowerStatsEntryData);
		pvOSPowerStatsEntryData=NULL;
	}

	/* Destroy the global data entry */
	if (pvOSGlobalMemEntryRef!=NULL)
	{
		OSRemoveStatisticEntry(pvOSGlobalMemEntryRef);
		pvOSGlobalMemEntryRef=NULL;
	}
	
	/* Destroy the locks... */
	if (g_psLinkedListLock != NULL)
	{
		OSLockDestroy(g_psLinkedListLock);
		g_psLinkedListLock = NULL;
	}

	/* Free the live and dead lists... */
	while (g_psLiveList != NULL)
	{
		PVRSRV_PROCESS_STATS*  psProcessStats = g_psLiveList;

		_RemoveProcessStatsFromList(psProcessStats);
		_RemoveOSStatisticEntries(psProcessStats);
	}

	while (g_psDeadList != NULL)
	{
		PVRSRV_PROCESS_STATS*  psProcessStats = g_psDeadList;

		_RemoveProcessStatsFromList(psProcessStats);
		_RemoveOSStatisticEntries(psProcessStats);
	}

	/* Remove the OS folders used by the PID folders...
	 * OSRemoveStatisticFolder will NULL the pointers */
	OSRemoveStatisticFolder(&pvOSLivePidFolder);
	OSRemoveStatisticFolder(&pvOSDeadPidFolder);

	if (gpsSizeTrackingHashTable != NULL)
	{
		HASH_Delete(gpsSizeTrackingHashTable);
	}
	if (gpsSizeTrackingHashTableLock != NULL)
	{
		OSLockDestroy(gpsSizeTrackingHashTableLock);
		gpsSizeTrackingHashTableLock = NULL;
	}

	if(NULL != gsGlobalStats.hGlobalStatsLock)
	{
		OSLockDestroy(gsGlobalStats.hGlobalStatsLock);
		gsGlobalStats.hGlobalStatsLock = NULL;
	}

} /* PVRSRVStatsDestroy */

static void _decrease_global_stat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								  size_t uiBytes)
{
	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

	switch (eAllocType)
	{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageKMalloc, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageVMalloc, uiBytes);
			break;
#else
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			break;
#endif
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageVMapPTUMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageIORemapPTLMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:	
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_UMA_POOL_PAGES:
			DECREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAPool, uiBytes);
			break;

		default:
			PVR_ASSERT(0);
			break;
	}
	OSLockRelease(gsGlobalStats.hGlobalStatsLock);
}

static void _increase_global_stat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								  size_t uiBytes)
{
	OSLockAcquire(gsGlobalStats.hGlobalStatsLock);

	switch (eAllocType)
	{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageKMalloc, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageVMalloc, uiBytes);
			break;
#else
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			break;
#endif
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageVMapPTUMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageIORemapPTLMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:	
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA, uiBytes);
			break;

		case PVRSRV_MEM_ALLOC_TYPE_UMA_POOL_PAGES:
			INCREASE_GLOBAL_STAT_VALUE(gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAPool, uiBytes);
			break;

		default:
			PVR_ASSERT(0);
			break;
	}
	OSLockRelease(gsGlobalStats.hGlobalStatsLock);
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
	PVRSRV_PROCESS_STATS*	psProcessStats=NULL;
	PVRSRV_ERROR			eError;
	IMG_PID					currentPid = OSGetCurrentClientProcessIDKM();
	IMG_BOOL				bMoveProcess = IMG_FALSE;
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	IMG_CHAR				acFolderName[30];
	IMG_CHAR				*pszProcName = OSGetCurrentProcessName();

	strncpy(acFolderName, pszProcName, sizeof(acFolderName));
	StripBadChars(acFolderName);
#endif

	PVR_ASSERT(phProcessStats != NULL);

	/* Check the PID has not already moved to the dead list... */
	OSLockAcquire(g_psLinkedListLock);
	psProcessStats = _FindProcessStatsInDeadList(currentPid);
	if (psProcessStats != NULL)
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

		/* Check if we need to perform any OS operation */
		if (bMoveProcess)
		{
			/* Transfer the OS entries back to the folder for live processes... */
			_RemoveOSStatisticEntries(psProcessStats);
			_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);
		}

		return PVRSRV_OK;
	}
	OSLockRelease(g_psLinkedListLock);

	/* Allocate a new node structure and initialise it... */
	psProcessStats = OSAllocZMemNoStats(sizeof(PVRSRV_PROCESS_STATS));
	if (psProcessStats == NULL)
	{
		*phProcessStats = 0;
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psProcessStats->eStructureType  = PVRSRV_STAT_STRUCTURE_PROCESS;
	psProcessStats->pid             = currentPid;
	psProcessStats->ui32RefCount    = 1;
	psProcessStats->ui32MemRefCount = 1;

	psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]     = 1;
	psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS] = 1;

	eError = OSLockCreateNoStats(&psProcessStats->hLock ,LOCK_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		goto e0;
	}

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	psProcessStats->psMemoryStats = OSAllocZMemNoStats(sizeof(PVRSRV_MEMORY_STATS));
	if (psProcessStats->psMemoryStats == NULL)
	{
		OSLockDestroyNoStats(psProcessStats->hLock);
		goto e0;
	}

	psProcessStats->psMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_MEMORY;
#endif

#if defined(PVR_RI_DEBUG)
	psProcessStats->psRIMemoryStats = OSAllocZMemNoStats(sizeof(PVRSRV_RI_MEMORY_STATS));
	if (psProcessStats->psRIMemoryStats == NULL)
	{
		OSLockDestroyNoStats(psProcessStats->hLock);
		OSFreeMemNoStats(psProcessStats->psMemoryStats);
		goto e0;
	}
	psProcessStats->psRIMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_RIMEMORY;
	psProcessStats->psRIMemoryStats->pid            = currentPid;
#endif

#if defined(DEBUG)
	psProcessStats->psCacheOpStats = OSAllocZMemNoStats(sizeof(PVRSRV_CACHEOP_STATS));
	if (psProcessStats->psCacheOpStats == NULL)
	{
		OSLockDestroyNoStats(psProcessStats->hLock);
		OSFreeMemNoStats(psProcessStats->psMemoryStats);
		OSFreeMemNoStats(psProcessStats->psRIMemoryStats);
		goto e0;
	}
	psProcessStats->psCacheOpStats->eStructureType = PVRSRV_STAT_STRUCTURE_CACHEOP;
#endif

	/* Add it to the live list... */
	OSLockAcquire(g_psLinkedListLock);
	_AddProcessStatsToFrontOfLiveList(psProcessStats);
	OSLockRelease(g_psLinkedListLock);

	/* Create the process stat in the OS... */
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
	OSSNPrintf(psProcessStats->szFolderName, sizeof(psProcessStats->szFolderName),
			   "%d_%s", currentPid, acFolderName);
#else
	OSSNPrintf(psProcessStats->szFolderName, sizeof(psProcessStats->szFolderName),
			   "%d", currentPid);
#endif
	_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);

	/* Done */
	*phProcessStats = (IMG_HANDLE) psProcessStats;

	return PVRSRV_OK;

e0:
	OSFreeMemNoStats(psProcessStats);
	*phProcessStats = 0;
	return PVRSRV_ERROR_OUT_OF_MEMORY;
} /* PVRSRVStatsRegisterProcess */

/*************************************************************************/ /*!
@Function       PVRSRVStatsDeregisterProcess
@Input          hProcessStats  Handle to the process returned when registered.
@Description    Method for destroying the statistics module data.
*/ /**************************************************************************/
void
PVRSRVStatsDeregisterProcess(IMG_HANDLE hProcessStats)
{
	IMG_BOOL    bMoveProcess = IMG_FALSE;

	if (hProcessStats != 0)
	{
		PVRSRV_PROCESS_STATS*  psProcessStats = (PVRSRV_PROCESS_STATS*) hProcessStats;

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
				bMoveProcess = IMG_TRUE;
			}else
#endif
			{
				OSLockRelease(psProcessStats->hLock);
			}
		}
		OSLockRelease(g_psLinkedListLock);

		/* The OS calls need to be performed without g_psLinkedListLock */
		if (bMoveProcess == IMG_TRUE)
		{
			_MoveProcessToDeadListDebugFS(psProcessStats);
		}

		/* Check if the dead list needs to be reduced */
		_CompressMemoryUsage();
	}
} /* PVRSRVStatsDeregisterProcess */

void
PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
							 void *pvCpuVAddr,
							 IMG_CPU_PHYADDR sCpuPAddr,
							 size_t uiBytes,
							 void *pvPrivateData)
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) && defined(DEBUG)
{
	_PVRSRVStatsAddMemAllocRecord(eAllocType, pvCpuVAddr, sCpuPAddr, uiBytes, pvPrivateData, NULL, 0);
}
void
_PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
							  void *pvCpuVAddr,
							  IMG_CPU_PHYADDR sCpuPAddr,
							  size_t uiBytes,
							  void *pvPrivateData,
							  void *pvAllocFromFile, IMG_UINT32 ui32AllocFromLine)
#endif
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID				   currentPid = OSGetCurrentClientProcessIDKM();
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_MEM_ALLOC_REC*  psRecord   = NULL;
	PVRSRV_PROCESS_STATS*  psProcessStats;
	PVRSRV_MEMORY_STATS*   psMemoryStats;
	IMG_BOOL			   bResurrectProcess = IMG_FALSE;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	/*
	 *  To prevent a recursive loop, we make the memory allocations
	 *  for our memstat records via OSAllocMemNoStats(), which does not try to
	 *  create a memstat record entry..
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

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) && defined(DEBUG)
	psRecord->pvAllocdFromFile = pvAllocFromFile;
	psRecord->ui32AllocdFromLine = ui32AllocFromLine;
#endif

	_increase_global_stat(eAllocType, uiBytes);
	/* Lock while we find the correct process... */
	OSLockAcquire(g_psLinkedListLock);

	if (psPVRSRVData)
	{
		if ( (currentPid == psPVRSRVData->cleanupThreadPid) &&
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
				bResurrectProcess = IMG_TRUE;
			}
		}
	}
	else
	{
		psProcessStats = _FindProcessStatsInLiveList(currentPid);
		if (!psProcessStats)
		{
			psProcessStats = _FindProcessStatsInDeadList(currentPid);
			bResurrectProcess = IMG_TRUE;
		}
	}

	if (psProcessStats == NULL)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVRSRV_ERROR	eError;
		IMG_CHAR				acFolderName[30];
		IMG_CHAR				*pszProcName = OSGetCurrentProcessName();

		strncpy(acFolderName, pszProcName, sizeof(acFolderName));
		StripBadChars(acFolderName);

		psProcessStats = OSAllocZMemNoStats(sizeof(PVRSRV_PROCESS_STATS));
		if (psProcessStats == NULL)
		{
			OSLockRelease(g_psLinkedListLock);
			return;
		}

		psProcessStats->eStructureType  = PVRSRV_STAT_STRUCTURE_PROCESS;
		psProcessStats->pid             = currentPid;
		psProcessStats->ui32RefCount    = 1;
		psProcessStats->ui32MemRefCount = 1;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]     = 1;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS] = 1;

		eError = OSLockCreateNoStats(&psProcessStats->hLock ,LOCK_TYPE_NONE);
		if (eError != PVRSRV_OK)
		{
			goto e0;
		}

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
		psProcessStats->psMemoryStats = OSAllocZMemNoStats(sizeof(PVRSRV_MEMORY_STATS));
		if (psProcessStats->psMemoryStats == NULL)
		{
			OSLockRelease(g_psLinkedListLock);
			OSLockDestroyNoStats(psProcessStats->hLock);
			psProcessStats->hLock = NULL;
			goto e0;
		}

		psProcessStats->psMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_MEMORY;
#endif

#if defined(PVR_RI_DEBUG)
		psProcessStats->psRIMemoryStats = OSAllocZMemNoStats(sizeof(PVRSRV_RI_MEMORY_STATS));
		if (psProcessStats->psRIMemoryStats == NULL)
		{
			OSFreeMemNoStats(psProcessStats->psMemoryStats);
			OSLockDestroyNoStats(psProcessStats->hLock);
			psProcessStats->hLock = NULL;
			OSLockRelease(g_psLinkedListLock);
			goto e0;
		}

		psProcessStats->psRIMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_RIMEMORY;
		psProcessStats->psRIMemoryStats->pid            = currentPid;
#endif

#if defined(DEBUG)
		psProcessStats->psCacheOpStats = OSAllocZMemNoStats(sizeof(PVRSRV_CACHEOP_STATS));
		if (psProcessStats->psCacheOpStats == NULL)
		{
			OSFreeMemNoStats(psProcessStats->psRIMemoryStats);
			OSFreeMemNoStats(psProcessStats->psMemoryStats);
			OSLockDestroyNoStats(psProcessStats->hLock);
			OSLockRelease(g_psLinkedListLock);
			psProcessStats->hLock = NULL;
			goto e0;
		}

		psProcessStats->psCacheOpStats->eStructureType = PVRSRV_STAT_STRUCTURE_CACHEOP;
#endif

		OSLockRelease(g_psLinkedListLock);
		/* Add it to the live list... */
		_AddProcessStatsToFrontOfLiveList(psProcessStats);

		/* Create the process stat in the OS... */
		OSSNPrintf(psProcessStats->szFolderName, sizeof(psProcessStats->szFolderName),
				   "%d_%s", currentPid, acFolderName);

		_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);
#else  /* defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */
		OSLockRelease(g_psLinkedListLock);
#endif /* defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */
	}
	else
	{
		OSLockRelease(g_psLinkedListLock);
	}

	if (psProcessStats == NULL)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVR_DPF((PVR_DBG_ERROR, "%s UNABLE TO CREATE process_stats entry for pid %d [%s] (" IMG_SIZE_FMTSPEC " bytes)", __FUNCTION__, currentPid, OSGetCurrentProcessName(), uiBytes));
#endif
		if (psRecord != NULL)
		{
			OSFreeMemNoStats(psRecord);
		}
		return;
	}

	OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);

	psMemoryStats = psProcessStats->psMemoryStats;

	/* Insert the memory record... */
	if (psRecord != NULL)
	{
		List_PVRSRV_MEM_ALLOC_REC_Insert(&psMemoryStats->psMemoryRecords, psRecord);
	}

	/* Update the memory watermarks... */
	switch (eAllocType)
	{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
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
			psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
		}
		break;
#else
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		break;
#endif
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
	OSLockRelease(psProcessStats->hLock);
	if (bResurrectProcess)
	{
		/* Move process from dead list to live list */
		OSLockAcquire(g_psLinkedListLock);
		_MoveProcessToLiveList(psProcessStats);
		OSLockRelease(g_psLinkedListLock);
		_MoveProcessToLiveListDebugFS(psProcessStats);
	}
	return;

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
e0:
	OSFreeMemNoStats(psRecord);
	OSFreeMemNoStats(psProcessStats);
	return;
#endif
#endif
} /* PVRSRVStatsAddMemAllocRecord */

void
PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE eAllocType,
								IMG_UINT64 ui64Key)
{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
	IMG_PID				   currentPid	  = OSGetCurrentClientProcessIDKM();
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA*		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_PROCESS_STATS*  psProcessStats = NULL;
	PVRSRV_MEMORY_STATS*   psMemoryStats  = NULL;
	PVRSRV_MEM_ALLOC_REC*  psRecord		  = NULL;
	IMG_BOOL			   bFound	      = IMG_FALSE;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	/* Lock while we find the correct process and remove this record... */
	OSLockAcquire(g_psLinkedListLock);

	if (psPVRSRVData)
	{
		if ( (currentPid == psPVRSRVData->cleanupThreadPid) &&
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
		psMemoryStats = psProcessStats->psMemoryStats;
		psRecord      = psMemoryStats->psMemoryRecords;
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
		PVRSRV_PROCESS_STATS*  psProcessStatsAlreadyChecked = psProcessStats;

		/* Search all live lists first... */
		psProcessStats = g_psLiveList;
		while (psProcessStats != NULL)
		{
			if (psProcessStats != psProcessStatsAlreadyChecked)
			{
				psMemoryStats = psProcessStats->psMemoryStats;
				psRecord      = psMemoryStats->psMemoryRecords;
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
					psMemoryStats = psProcessStats->psMemoryStats;
					psRecord      = psMemoryStats->psMemoryRecords;
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
			_MoveProcessToDeadListDebugFS(psProcessStats);

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
									IMG_UINT64 uiCpuVAddr)
{
	IMG_BOOL bRes = IMG_FALSE;
	_PVR_STATS_TRACKING_HASH_ENTRY *psNewTrackingHashEntry = NULL;

	if (!bProcessStatsInitialised || (gpsSizeTrackingHashTable == NULL) )
	{
		return;
	}

	/* Alloc untracked memory for the new hash table entry */
	psNewTrackingHashEntry = (_PVR_STATS_TRACKING_HASH_ENTRY *)OSAllocMemNoStats(sizeof(*psNewTrackingHashEntry));
	if (psNewTrackingHashEntry)
	{
		/* Fill-in the size of the allocation and PID of the allocating process */
		psNewTrackingHashEntry->uiSizeInBytes = uiBytes;
		psNewTrackingHashEntry->uiPid = OSGetCurrentClientProcessIDKM();
		OSLockAcquire(gpsSizeTrackingHashTableLock);
		/* Insert address of the new struct into the hash table */
		bRes = HASH_Insert(gpsSizeTrackingHashTable, uiCpuVAddr, (uintptr_t)psNewTrackingHashEntry);
		OSLockRelease(gpsSizeTrackingHashTableLock);
	}

	if (psNewTrackingHashEntry)
	{
		if (bRes)
		{
			PVRSRVStatsIncrMemAllocStat(eAllocType, uiBytes);
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "*** %s : @ line %d HASH_Insert() failed!!", __FUNCTION__, __LINE__));
		}
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR, "*** %s : @ line %d Failed to alloc memory for psNewTrackingHashEntry!!", __FUNCTION__, __LINE__));
	}
}

void
PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE eAllocType,
									size_t uiBytes)
{
	IMG_PID				   currentPid = OSGetCurrentClientProcessIDKM();
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA* 		   psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_PROCESS_STATS*  psProcessStats = NULL;
	IMG_BOOL			   bResurrectProcess = IMG_FALSE;

	/* Don't do anything if we are not initialised or we are shutting down! */
	if (!bProcessStatsInitialised)
	{
		return;
	}

	_increase_global_stat(eAllocType, uiBytes);
	OSLockAcquire(g_psLinkedListLock);
	if (psPVRSRVData)
	{
		if ( (currentPid == psPVRSRVData->cleanupThreadPid) &&
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
				bResurrectProcess = IMG_TRUE;
			}
		}
	}
	else
	{
		psProcessStats = _FindProcessStatsInLiveList(currentPid);
		if (!psProcessStats)
		{
			psProcessStats = _FindProcessStatsInDeadList(currentPid);
			bResurrectProcess = IMG_TRUE;
		}
	}

	if (psProcessStats == NULL)
	{
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
		PVRSRV_ERROR eError;
		IMG_CHAR				acFolderName[30];
		IMG_CHAR				*pszProcName = OSGetCurrentProcessName();

		strncpy(acFolderName, pszProcName, sizeof(acFolderName));
		StripBadChars(acFolderName);

		if (bProcessStatsInitialised)
		{
			psProcessStats = OSAllocZMemNoStats(sizeof(PVRSRV_PROCESS_STATS));
			if (psProcessStats == NULL)
			{
				return;
			}

			psProcessStats->eStructureType  = PVRSRV_STAT_STRUCTURE_PROCESS;
			psProcessStats->pid             = currentPid;
			psProcessStats->ui32RefCount    = 1;
			psProcessStats->ui32MemRefCount = 1;
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_CONNECTIONS]     = 1;
			psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAX_CONNECTIONS] = 1;

			eError = OSLockCreateNoStats(&psProcessStats->hLock ,LOCK_TYPE_NONE);
			if (eError != PVRSRV_OK)
			{
				OSFreeMemNoStats(psProcessStats);
				return;
			}

#if defined(PVRSRV_ENABLE_MEMORY_STATS)
			psProcessStats->psMemoryStats = OSAllocZMemNoStats(sizeof(PVRSRV_MEMORY_STATS));
			if (psProcessStats->psMemoryStats == NULL)
			{
				OSLockDestroyNoStats(psProcessStats->hLock);
				OSFreeMemNoStats(psProcessStats);
				return;
			}
			psProcessStats->psMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_MEMORY;
#endif

#if defined(PVR_RI_DEBUG)
			psProcessStats->psRIMemoryStats = OSAllocZMemNoStats(sizeof(PVRSRV_RI_MEMORY_STATS));
			if (psProcessStats->psRIMemoryStats == NULL)
			{
				OSFreeMemNoStats(psProcessStats->psMemoryStats);
				OSLockDestroyNoStats(psProcessStats->hLock);
				OSFreeMemNoStats(psProcessStats);
				return;
			}
			psProcessStats->psRIMemoryStats->eStructureType = PVRSRV_STAT_STRUCTURE_RIMEMORY;
			psProcessStats->psRIMemoryStats->pid            = currentPid;
#endif

#if defined(PVR_RI_DEBUG)
			psProcessStats->psCacheOpStats = OSAllocZMemNoStats(sizeof(PVRSRV_CACHEOP_STATS));
			if (psProcessStats->psCacheOpStats == NULL)
			{
				OSFreeMemNoStats(psProcessStats->psMemoryStats);
				OSFreeMemNoStats(psProcessStats->psRIMemoryStats);
				OSLockDestroyNoStats(psProcessStats->hLock);
				OSFreeMemNoStats(psProcessStats);
				return;
			}
			psProcessStats->psCacheOpStats->eStructureType = PVRSRV_STAT_STRUCTURE_CACHEOP;
#endif

			/* Add it to the live list... */
			_AddProcessStatsToFrontOfLiveList(psProcessStats);

			/* Create the process stat in the OS... */
			OSSNPrintf(psProcessStats->szFolderName, sizeof(psProcessStats->szFolderName),
					"%d_%s", currentPid, acFolderName);

			_CreateOSStatisticEntries(psProcessStats, pvOSLivePidFolder);
		}
#else
		OSLockRelease(g_psLinkedListLock);
#endif /* defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) */

	}

	if (psProcessStats != NULL)
	{
		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
		/*Release the list lock as soon as we acquire the process lock,
		 * this ensures if the process is in deadlist the entry cannot be deleted or modified */
		OSLockRelease(g_psLinkedListLock);
		/* Update the memory watermarks... */
		switch (eAllocType)
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
			case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;
#else
			case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
			case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
			break;
#endif
			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, (IMG_UINT32)uiBytes);
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
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
			{
				INCREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, (IMG_UINT32)uiBytes);
				psProcessStats->ui32StatAllocFlags |= (IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
			break;

			case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
			{
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
		OSLockRelease(psProcessStats->hLock);

		if (bResurrectProcess)
		{
			/* Move process from dead list to live list */
			OSLockAcquire(g_psLinkedListLock);
			_MoveProcessToLiveList(psProcessStats);
			OSLockRelease(g_psLinkedListLock);
			_MoveProcessToLiveListDebugFS(psProcessStats);
		}
    }
}

static void
_DecreaseProcStatValue(PVRSRV_MEM_ALLOC_TYPE eAllocType,
                       PVRSRV_PROCESS_STATS* psProcessStats,
                       IMG_UINT32 uiBytes)
{
	switch (eAllocType)
	{
	#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_KMALLOC, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_KMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMALLOC, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMALLOC] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMALLOC-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;
	#else
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
		break;
	#endif
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_VMAP_PT_UMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_IOREMAP_PT_LMA-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES:
		{
			DECREASE_STAT_VALUE(psProcessStats, PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES, (IMG_UINT32)uiBytes);
			if( psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES] == 0 )
			{
				psProcessStats->ui32StatAllocFlags &= ~(IMG_UINT32)(1 << (PVRSRV_PROCESS_STAT_TYPE_MAP_UMA_LMA_PAGES-PVRSRV_PROCESS_STAT_TYPE_KMALLOC));
			}
		}
		break;

		default:
		{
			PVR_ASSERT(0);
		}
		break;
	}

}

#if defined(PVRSRV_ENABLE_MEMTRACK_STATS_FILE)
void RawProcessStatsPrintElements(void *pvFile,
                                  void *pvStatPtr,
                                  OS_STATS_PRINTF_FUNC *pfnOSStatsPrintf)
{
	PVRSRV_PROCESS_STATS *psProcessStats;

	if (pfnOSStatsPrintf == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: pfnOSStatsPrintf not set", __func__));
		return;
	}

	pfnOSStatsPrintf(pvFile, "%s,%s,%s,%s,%s,%s\n",
	                 "PID",
	                 "MemoryUsageKMalloc",           // PVRSRV_PROCESS_STAT_TYPE_KMALLOC
	                 "MemoryUsageAllocPTMemoryUMA",  // PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA
	                 "MemoryUsageAllocPTMemoryLMA",  // PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA
	                 "MemoryUsageAllocGPUMemUMA",    // PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES
	                 "MemoryUsageAllocGPUMemLMA"     // PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES
	                 );

	OSLockAcquire(g_psLinkedListLock);

	psProcessStats = g_psLiveList;

	while (psProcessStats != NULL)
	{
		pfnOSStatsPrintf(pvFile, "%d,%d,%d,%d,%d,%d\n",
		                 psProcessStats->pid,
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_KMALLOC],
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_UMA],
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_PAGES_PT_LMA],
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_UMA_PAGES],
		                 psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_ALLOC_LMA_PAGES]
		                 );

		psProcessStats = psProcessStats->psNext;
	}

	OSLockRelease(g_psLinkedListLock);
} /* RawProcessStatsPrintElements */
#endif

void
PVRSRVStatsDecrMemKAllocStat(size_t uiBytes,
                             IMG_PID decrPID)
{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
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
	}

	OSLockRelease(g_psLinkedListLock);
#endif
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
		/* Decrement the memory stat... */
		_DecreaseProcStatValue(eAllocType,
		                       psProcessStats,
		                       psTrackingHashEntry->uiSizeInBytes);
	}

	OSLockRelease(g_psLinkedListLock);
}

void
PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE eAllocType,
									  IMG_UINT64 uiCpuVAddr)
{
	_PVR_STATS_TRACKING_HASH_ENTRY *psTrackingHashEntry = NULL;

	if (!bProcessStatsInitialised || (gpsSizeTrackingHashTable == NULL) )
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
							size_t uiBytes)
{
	IMG_PID				   currentPid = OSGetCurrentClientProcessIDKM();
	IMG_PID				   currentCleanupPid = PVRSRVGetPurgeConnectionPid();
	PVRSRV_DATA* 		   psPVRSRVData = PVRSRVGetPVRSRVData();
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
		if ( (currentPid == psPVRSRVData->cleanupThreadPid) &&
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
		/*Release the list lock as soon as we acquire the process lock,
		 * this ensures if the process is in deadlist the entry cannot be deleted or modified */
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
			_MoveProcessToDeadListDebugFS(psProcessStats);

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
PVRSRVStatsUpdateRenderContextStats(IMG_UINT32 ui32TotalNumPartialRenders,
									IMG_UINT32 ui32TotalNumOutOfMemory,
									IMG_UINT32 ui32NumTAStores,
									IMG_UINT32 ui32Num3DStores,
									IMG_UINT32 ui32NumSHStores,
									IMG_UINT32 ui32NumCDMStores,
									IMG_PID pidOwner)
{
	IMG_PID	pidCurrent = pidOwner;

	PVRSRV_PROCESS_STATS*  psProcessStats;

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
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_SH_STORES] += ui32NumSHStores;
		psProcessStats->i32StatValue[PVRSRV_PROCESS_STAT_TYPE_RC_CDM_STORES]+= ui32NumCDMStores;
		OSLockRelease(psProcessStats->hLock);
	}
	else
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVStatsUpdateRenderContextStats: Null process. Pid=%d", pidCurrent));
	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateRenderContextStats */

void
PVRSRVStatsUpdateZSBufferStats(IMG_UINT32 ui32NumReqByApp,
							   IMG_UINT32 ui32NumReqByFW,
							   IMG_PID owner)
{
	IMG_PID				   currentPid = (owner==0)?OSGetCurrentClientProcessIDKM():owner;
	PVRSRV_PROCESS_STATS*  psProcessStats;


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
	IMG_PID				   currentPid = (ownerPid!=0)?ownerPid:OSGetCurrentClientProcessIDKM();
	PVRSRV_PROCESS_STATS*  psProcessStats;

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
		/* Avoid signed / unsigned mismatch which is flagged by some compilers */
		IMG_INT32 a, b;

		OSLockAcquireNested(psProcessStats->hLock, PROCESS_LOCK_SUBCLASS_CURRENT);
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
		OSLockRelease(psProcessStats->hLock);

	}

	OSLockRelease(g_psLinkedListLock);
} /* PVRSRVStatsUpdateFreelistStats */

/*************************************************************************/ /*!
@Function       ProcessStatsPrintElements
@Description    Prints all elements for this process statistic record.
@Input          pvStatPtr         Pointer to statistics structure.
@Input          pfnOSStatsPrintf  Printf function to use for output.
*/ /**************************************************************************/
void
ProcessStatsPrintElements(void *pvFile,
						  void *pvStatPtr,
						  OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_PROCESS_STATS*	     psProcessStats  = (PVRSRV_PROCESS_STATS*) pvStatPtr;
	IMG_UINT32					 ui32StatNumber = 0;

	if (peStructureType == NULL  ||  *peStructureType != PVRSRV_STAT_STRUCTURE_PROCESS)
	{
		PVR_ASSERT(peStructureType != NULL  &&  *peStructureType == PVRSRV_STAT_STRUCTURE_PROCESS);
		return;
	}

	if (pfnOSStatsPrintf == NULL)
	{
		return;
	}

	/* Loop through all the values and print them... */
	while (ui32StatNumber < PVRSRV_PROCESS_STAT_TYPE_COUNT)
	{
		if (psProcessStats->ui32MemRefCount > 0)
		{
			pfnOSStatsPrintf(pvFile, pszProcessStatFmt[ui32StatNumber], psProcessStats->i32StatValue[ui32StatNumber]);
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Called with psProcessStats->ui32MemRefCount=%d", __FUNCTION__, psProcessStats->ui32MemRefCount));
		}
		ui32StatNumber++;
	}
} /* ProcessStatsPrintElements */

#if defined(DEBUG)
/* Divide a number by 10 using shifts only */
static INLINE IMG_UINT64 DivBy10(IMG_UINT64 uiNum) 
{
	IMG_UINT64 uiQuot;
	IMG_UINT64 uiRem;

	uiQuot = (uiNum >> 1) + (uiNum >> 2);
	uiQuot = uiQuot + (uiQuot >> 4);
	uiQuot = uiQuot + (uiQuot >> 8);
	uiQuot = uiQuot + (uiQuot >> 16);
	uiQuot = uiQuot >> 3;
	uiRem  = uiNum - (((uiQuot << 2) + uiQuot) << 1);

	return uiQuot + (uiRem > 9);
}

void
PVRSRVStatsUpdateCacheOpStats(PVRSRV_CACHE_OP uiCacheOp,
							IMG_UINT32 ui32OpSeqNum,
#if defined(PVR_RI_DEBUG)
							IMG_DEV_VIRTADDR sDevVAddr,
							IMG_UINT32 eFenceOpType,
#endif
							IMG_DEVMEM_SIZE_T uiOffset,
							IMG_DEVMEM_SIZE_T uiSize,
							IMG_UINT64 ui64ExecuteTime,
							IMG_BOOL bRangeBasedFlush,
							IMG_BOOL bUserModeFlush,
							IMG_BOOL bHasTimeline,
							IMG_BOOL bIsFence,
							IMG_PID ownerPid)
{
	IMG_PID				   currentPid = (ownerPid!=0)?ownerPid:OSGetCurrentClientProcessIDKM();
	PVRSRV_PROCESS_STATS*  psProcessStats;

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
#if defined(PVR_RI_DEBUG)
		psProcessStats->asCacheOp[Idx].sDevVAddr = sDevVAddr;
		psProcessStats->asCacheOp[Idx].eFenceOpType = eFenceOpType;
#endif
		psProcessStats->asCacheOp[Idx].uiOffset = uiOffset;
		psProcessStats->asCacheOp[Idx].uiSize = uiSize;
		psProcessStats->asCacheOp[Idx].bRangeBasedFlush = bRangeBasedFlush;
		psProcessStats->asCacheOp[Idx].bUserModeFlush = bUserModeFlush;
		psProcessStats->asCacheOp[Idx].ui64ExecuteTime = ui64ExecuteTime;
		psProcessStats->asCacheOp[Idx].ui32OpSeqNum = ui32OpSeqNum;
		psProcessStats->asCacheOp[Idx].bHasTimeline = bHasTimeline;
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
CacheOpStatsPrintElements(void *pvFile,
						  void *pvStatPtr,
						  OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_PROCESS_STATS*		 psProcessStats  = (PVRSRV_PROCESS_STATS*) pvStatPtr;
	IMG_CHAR					 *pszCacheOpType, *pszFlushType, *pszFlushMode;
	IMG_INT32 					 i32WriteIdx, i32ReadIdx;

#if defined(PVR_RI_DEBUG)
	#define CACHEOP_RI_PRINTF_HEADER \
		"%-10s %-10s %-5s %-16s %-10s %-10s %-12s %-12s\n"
	#define CACHEOP_RI_PRINTF_FENCE	 \
		"%-10s %-10s %-5s %-16s %-10s %-10s %-12llu 0x%-10x\n"
	#define CACHEOP_RI_PRINTF		\
		"%-10s %-10s %-5s 0x%-14llx 0x%-8llx 0x%-8llx %-12llu 0x%-10x\n"
#else
	#define CACHEOP_PRINTF_HEADER	\
		"%-10s %-10s %-5s %-10s %-10s %-12s %-12s\n"
	#define CACHEOP_PRINTF_FENCE	 \
		"%-10s %-10s %-5s %-10s %-10s %-12llu 0x%-10x\n"
	#define CACHEOP_PRINTF		 	\
		"%-10s %-10s %-5s 0x%-8llx 0x%-8llx %-12llu 0x%-10x\n"
#endif

	if (peStructureType == NULL  ||
		*peStructureType != PVRSRV_STAT_STRUCTURE_PROCESS ||
		psProcessStats->psCacheOpStats->eStructureType != PVRSRV_STAT_STRUCTURE_CACHEOP)
	{
		PVR_ASSERT(peStructureType != NULL);
		PVR_ASSERT(*peStructureType == PVRSRV_STAT_STRUCTURE_PROCESS);
		PVR_ASSERT(psProcessStats->psCacheOpStats->eStructureType == PVRSRV_STAT_STRUCTURE_CACHEOP);
		return;
	}

	if (pfnOSStatsPrintf == NULL)
	{
		return;
	}

	/* File header info */
	pfnOSStatsPrintf(pvFile,
#if defined(PVR_RI_DEBUG)
					CACHEOP_RI_PRINTF_HEADER,
#else
					CACHEOP_PRINTF_HEADER,
#endif
					"CacheOp",
					"Type",
					"Mode",
#if defined(PVR_RI_DEBUG)
					"DevVAddr",
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

		/* Convert nano-seconds to micro-seconds */
		ui64ExecuteTime = psProcessStats->asCacheOp[i32ReadIdx].ui64ExecuteTime;
		ui64ExecuteTime = DivBy10(DivBy10(DivBy10(ui64ExecuteTime)));

		if (psProcessStats->asCacheOp[i32ReadIdx].bIsFence)
		{
			IMG_CHAR *pszFenceType = "";
			pszCacheOpType = "Fence";

#if defined(PVR_RI_DEBUG)
			switch (psProcessStats->asCacheOp[i32ReadIdx].eFenceOpType)
			{
				case RGXFWIF_DM_GP:
					pszFenceType = "GP";
					break;

				case RGXFWIF_DM_TDM:
					/* Also case RGXFWIF_DM_2D: */
					pszFenceType = "TDM/2D";
					break;
	
				case RGXFWIF_DM_TA:
					pszFenceType = "TA";
					break;

				case RGXFWIF_DM_3D:
					pszFenceType = "3D";
					break;

				case RGXFWIF_DM_CDM:
					pszFenceType = "CDM";
					break;

				case RGXFWIF_DM_RTU:
					pszFenceType = "RTU";
					break;
	
				case RGXFWIF_DM_SHG:
					pszFenceType = "SHG";
					break;

				default:
					PVR_ASSERT(0);
					break;
			}
#endif

			pfnOSStatsPrintf(pvFile,
#if defined(PVR_RI_DEBUG)
							CACHEOP_RI_PRINTF_FENCE,
#else
							CACHEOP_PRINTF_FENCE,
#endif
							pszCacheOpType,
							pszFenceType,
							"",
#if defined(PVR_RI_DEBUG)
							"",
#endif
							"",
							"",
							ui64ExecuteTime,
							psProcessStats->asCacheOp[i32ReadIdx].ui32OpSeqNum);
		}
		else if (psProcessStats->asCacheOp[i32ReadIdx].bHasTimeline)
		{
			pfnOSStatsPrintf(pvFile,
#if defined(PVR_RI_DEBUG)
							CACHEOP_RI_PRINTF_FENCE,
#else
							CACHEOP_PRINTF_FENCE,
#endif
							"Timeline",
							"",
							"",
#if defined(PVR_RI_DEBUG)
							"",
#endif
							"",
							"",
							ui64ExecuteTime,
							psProcessStats->asCacheOp[i32ReadIdx].ui32OpSeqNum);
		}
		else
		{
			if (psProcessStats->asCacheOp[i32ReadIdx].bRangeBasedFlush)
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
			}
			else
			{
				pszFlushType = "GF";
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

			pfnOSStatsPrintf(pvFile,
#if defined(PVR_RI_DEBUG)
							CACHEOP_RI_PRINTF,
#else
							CACHEOP_PRINTF,
#endif
							pszCacheOpType,
							pszFlushType,
							pszFlushMode,
#if defined(PVR_RI_DEBUG)
							psProcessStats->asCacheOp[i32ReadIdx].sDevVAddr.uiAddr,
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
MemStatsPrintElements(void *pvFile,
					  void *pvStatPtr,
					  OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	PVRSRV_STAT_STRUCTURE_TYPE*  peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_MEMORY_STATS*         psMemoryStats   = (PVRSRV_MEMORY_STATS*) pvStatPtr;
	IMG_UINT32	ui32VAddrFields = sizeof(void*)/sizeof(IMG_UINT32);
	IMG_UINT32	ui32PAddrFields = sizeof(IMG_CPU_PHYADDR)/sizeof(IMG_UINT32);
	PVRSRV_MEM_ALLOC_REC  *psRecord;
	IMG_UINT32  ui32ItemNumber;

	if (peStructureType == NULL  ||  *peStructureType != PVRSRV_STAT_STRUCTURE_MEMORY)
	{
		PVR_ASSERT(peStructureType != NULL  &&  *peStructureType == PVRSRV_STAT_STRUCTURE_MEMORY);
		return;
	}

	if (pfnOSStatsPrintf == NULL)
	{
		return;
	}

	/* Write the header... */
	pfnOSStatsPrintf(pvFile, "Type                VAddress");
	for (ui32ItemNumber = 1;  ui32ItemNumber < ui32VAddrFields;  ui32ItemNumber++)
	{
		pfnOSStatsPrintf(pvFile, "        ");
	}

	pfnOSStatsPrintf(pvFile, "  PAddress");
	for (ui32ItemNumber = 1;  ui32ItemNumber < ui32PAddrFields;  ui32ItemNumber++)
	{
        pfnOSStatsPrintf(pvFile, "        ");
	}

    pfnOSStatsPrintf(pvFile, "  Size(bytes)\n");

	/* The lock has to be held whilst moving through the memory list... */
	OSLockAcquire(g_psLinkedListLock);
	psRecord = psMemoryStats->psMemoryRecords;

	while (psRecord != NULL)
	{
		IMG_BOOL bPrintStat = IMG_TRUE;

		switch (psRecord->eAllocType)
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:      		pfnOSStatsPrintf(pvFile, "KMALLOC             "); break;
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:      		pfnOSStatsPrintf(pvFile, "VMALLOC             "); break;
#else
		case PVRSRV_MEM_ALLOC_TYPE_KMALLOC:
		case PVRSRV_MEM_ALLOC_TYPE_VMALLOC:
														bPrintStat = IMG_FALSE; break;
#endif
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_LMA:  pfnOSStatsPrintf(pvFile, "ALLOC_PAGES_PT_LMA  "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_PAGES_PT_UMA:  pfnOSStatsPrintf(pvFile, "ALLOC_PAGES_PT_UMA  "); break;
		case PVRSRV_MEM_ALLOC_TYPE_IOREMAP_PT_LMA:		pfnOSStatsPrintf(pvFile, "IOREMAP_PT_LMA      "); break;
		case PVRSRV_MEM_ALLOC_TYPE_VMAP_PT_UMA:			pfnOSStatsPrintf(pvFile, "VMAP_PT_UMA         "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_LMA_PAGES: 	pfnOSStatsPrintf(pvFile, "ALLOC_LMA_PAGES     "); break;
		case PVRSRV_MEM_ALLOC_TYPE_ALLOC_UMA_PAGES: 	pfnOSStatsPrintf(pvFile, "ALLOC_UMA_PAGES     "); break;
		case PVRSRV_MEM_ALLOC_TYPE_MAP_UMA_LMA_PAGES: 	pfnOSStatsPrintf(pvFile, "MAP_UMA_LMA_PAGES   "); break;
		default:										pfnOSStatsPrintf(pvFile, "INVALID             "); break;
		}

		if (bPrintStat)
		{
			for (ui32ItemNumber = 0;  ui32ItemNumber < ui32VAddrFields;  ui32ItemNumber++)
			{
				pfnOSStatsPrintf(pvFile, "%08x", *(((IMG_UINT32*) &psRecord->pvCpuVAddr) + ui32VAddrFields - ui32ItemNumber - 1));
			}
			pfnOSStatsPrintf(pvFile, "  ");

			for (ui32ItemNumber = 0;  ui32ItemNumber < ui32PAddrFields;  ui32ItemNumber++)
			{
				pfnOSStatsPrintf(pvFile, "%08x", *(((IMG_UINT32*) &psRecord->sCpuPAddr.uiAddr) + ui32PAddrFields - ui32ItemNumber - 1));
			}

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) && defined(DEBUG)
			pfnOSStatsPrintf(pvFile, "  %u", psRecord->uiBytes);

			pfnOSStatsPrintf(pvFile, "  %s", (IMG_CHAR*)psRecord->pvAllocdFromFile);

			pfnOSStatsPrintf(pvFile, "  %d\n", psRecord->ui32AllocdFromLine);
#else
			pfnOSStatsPrintf(pvFile, "  %u\n", psRecord->uiBytes);
#endif
		}
		/* Move to next record... */
		psRecord = psRecord->psNext;
	}

	OSLockRelease(g_psLinkedListLock);
} /* MemStatsPrintElements */
#endif

#if defined(PVR_RI_DEBUG)
/*************************************************************************/ /*!
@Function       RIMemStatsPrintElements
@Description    Prints all elements for the RI Memory record.
@Input          pvStatPtr         Pointer to statistics structure.
@Input          pfnOSStatsPrintf  Printf function to use for output.
*/ /**************************************************************************/
void
RIMemStatsPrintElements(void *pvFile,
						void *pvStatPtr,
						OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	PVRSRV_STAT_STRUCTURE_TYPE  *peStructureType = (PVRSRV_STAT_STRUCTURE_TYPE*) pvStatPtr;
	PVRSRV_RI_MEMORY_STATS		*psRIMemoryStats = (PVRSRV_RI_MEMORY_STATS*) pvStatPtr;
	IMG_CHAR					*pszStatFmtText  = NULL;
	IMG_HANDLE					*pRIHandle       = NULL;

	if (peStructureType == NULL  ||  *peStructureType != PVRSRV_STAT_STRUCTURE_RIMEMORY)
	{
		PVR_ASSERT(peStructureType != NULL  &&  *peStructureType == PVRSRV_STAT_STRUCTURE_RIMEMORY);
		return;
	}

	if (pfnOSStatsPrintf == NULL)
	{
		return;
	}

	/*
	 *  Loop through the RI system to get each line of text.
	 */
	while (RIGetListEntryKM(psRIMemoryStats->pid,
							&pRIHandle,
							&pszStatFmtText))
	{
		pfnOSStatsPrintf(pvFile, "%s", pszStatFmtText);
	}
} /* RIMemStatsPrintElements */
#endif

static IMG_UINT32	ui32FirmwareStartTimestamp=0;
static IMG_UINT64	ui64FirmwareIdleDuration=0;

void SetFirmwareStartTime(IMG_UINT32 ui32Time)
{
	ui32FirmwareStartTimestamp = UPDATE_TIME(ui32FirmwareStartTimestamp, ui32Time);
}

void SetFirmwareHandshakeIdleTime(IMG_UINT64 ui64Duration)
{
	ui64FirmwareIdleDuration = UPDATE_TIME(ui64FirmwareIdleDuration, ui64Duration);
}

static INLINE void PowerStatsPrintGroup(IMG_UINT32 *pui32Stats,
                                        void *pvFile,
                                        OS_STATS_PRINTF_FUNC *pfnPrintf,
                                        PVRSRV_POWER_STAT_TYPE eForced,
                                        PVRSRV_POWER_STAT_TYPE ePowerOn)
{
	IMG_UINT32 ui32Index;

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, PRE_POWER, DEVICE);
	pfnPrintf(pvFile, "  Pre-Device:  %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, PRE_POWER, SYSTEM);
	pfnPrintf(pvFile, "  Pre-System:  %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, POST_POWER, SYSTEM);
	pfnPrintf(pvFile, "  Post-System: %9u\n", pui32Stats[ui32Index]);

	ui32Index = GET_POWER_STAT_INDEX(eForced, ePowerOn, POST_POWER, DEVICE);
	pfnPrintf(pvFile, "  Post-Device: %9u\n", pui32Stats[ui32Index]);
}

void PowerStatsPrintElements(void *pvFile,
							 void *pvStatPtr,
							 OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	IMG_UINT32 *pui32Stats = &aui32PowerTimingStats[0];
	IMG_UINT32 ui32Idx;

	PVR_UNREFERENCED_PARAMETER(pvStatPtr);

	if (pfnOSStatsPrintf == NULL)
	{
		return;
	}

	pfnOSStatsPrintf(pvFile, "Forced Power-on Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, pvFile, pfnOSStatsPrintf, FORCED, POWER_ON);
	pfnOSStatsPrintf(pvFile, "\n");

	pfnOSStatsPrintf(pvFile, "Forced Power-off Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, pvFile, pfnOSStatsPrintf, FORCED, POWER_OFF);
	pfnOSStatsPrintf(pvFile, "\n");

	pfnOSStatsPrintf(pvFile, "Not Forced Power-on Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, pvFile, pfnOSStatsPrintf, NOT_FORCED, POWER_ON);
	pfnOSStatsPrintf(pvFile, "\n");

	pfnOSStatsPrintf(pvFile, "Not Forced Power-off Transition (nanoseconds):\n");
	PowerStatsPrintGroup(pui32Stats, pvFile, pfnOSStatsPrintf, NOT_FORCED, POWER_OFF);
	pfnOSStatsPrintf(pvFile, "\n");


	pfnOSStatsPrintf(pvFile, "FW bootup time (timer ticks): %u\n", ui32FirmwareStartTimestamp);
	pfnOSStatsPrintf(pvFile, "Host Acknowledge Time for FW Idle Signal (timer ticks): %u\n", (IMG_UINT32)(ui64FirmwareIdleDuration));
	pfnOSStatsPrintf(pvFile, "\n");

	pfnOSStatsPrintf(pvFile, "Last %d Clock Speed Change Timers (nanoseconds):\n", NUM_EXTRA_POWER_STATS);
	pfnOSStatsPrintf(pvFile, "Prepare DVFS\tDVFS Change\tPost DVFS\n");

	for (ui32Idx = ui32ClockSpeedIndexStart; ui32Idx !=ui32ClockSpeedIndexEnd; ui32Idx = (ui32Idx + 1) % NUM_EXTRA_POWER_STATS)
	{
		pfnOSStatsPrintf(pvFile, "%12llu\t%11llu\t%9llu\n",asClockSpeedChanges[ui32Idx].ui64PreClockSpeedChangeDuration,
						 asClockSpeedChanges[ui32Idx].ui64BetweenPreEndingAndPostStartingDuration,
						 asClockSpeedChanges[ui32Idx].ui64PostClockSpeedChangeDuration);
	}


} /* PowerStatsPrintElements */

void GlobalStatsPrintElements(void *pvFile,
							  void *pvStatPtr,
							  OS_STATS_PRINTF_FUNC* pfnOSGetStatsPrintf)
{
	PVR_UNREFERENCED_PARAMETER(pvStatPtr);

	if (pfnOSGetStatsPrintf != NULL)
	{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageKMalloc                %10d\n", gsGlobalStats.ui32MemoryUsageKMalloc);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageKMallocMax             %10d\n", gsGlobalStats.ui32MemoryUsageKMallocMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageVMalloc                %10d\n", gsGlobalStats.ui32MemoryUsageVMalloc);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageVMallocMax             %10d\n", gsGlobalStats.ui32MemoryUsageVMallocMax);
#endif
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocPTMemoryUMA       %10d\n", gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocPTMemoryUMAMax    %10d\n", gsGlobalStats.ui32MemoryUsageAllocPTMemoryUMAMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageVMapPTUMA              %10d\n", gsGlobalStats.ui32MemoryUsageVMapPTUMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageVMapPTUMAMax           %10d\n", gsGlobalStats.ui32MemoryUsageVMapPTUMAMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocPTMemoryLMA       %10d\n", gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocPTMemoryLMAMax    %10d\n", gsGlobalStats.ui32MemoryUsageAllocPTMemoryLMAMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageIORemapPTLMA           %10d\n", gsGlobalStats.ui32MemoryUsageIORemapPTLMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageIORemapPTLMAMax        %10d\n", gsGlobalStats.ui32MemoryUsageIORemapPTLMAMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocGPUMemLMA         %10d\n", gsGlobalStats.ui32MemoryUsageAllocGPUMemLMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocGPUMemLMAMax      %10d\n", gsGlobalStats.ui32MemoryUsageAllocGPUMemLMAMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocGPUMemUMA         %10d\n", gsGlobalStats.ui32MemoryUsageAllocGPUMemUMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocGPUMemUMAMax      %10d\n", gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocGPUMemUMAPool     %10d\n", gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAPool);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageAllocGPUMemUMAPoolMax  %10d\n", gsGlobalStats.ui32MemoryUsageAllocGPUMemUMAPoolMax);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageMappedGPUMemUMA/LMA    %10d\n", gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMA);
		pfnOSGetStatsPrintf(pvFile, "MemoryUsageMappedGPUMemUMA/LMAMax %10d\n", gsGlobalStats.ui32MemoryUsageMappedGPUMemUMA_LMAMax);
	}
}

#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS)
static void StripBadChars( IMG_CHAR *psStr)
{
	IMG_INT	cc;

	/* Remove any '/' chars that may be in the ProcName (kernel thread could contain these) */
	for (cc=0; cc<30; cc++)
	{
		if( *psStr == '/')
		{
			*psStr = '-';
		}
		psStr++;
	}
}
#endif
