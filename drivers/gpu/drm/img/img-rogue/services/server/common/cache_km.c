/*************************************************************************/ /*!
@File           cache_km.c
@Title          CPU d-cache maintenance operations framework
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements server side code for CPU d-cache maintenance taking
                into account the idiosyncrasies of the various types of CPU
                d-cache instruction-set architecture (ISA) maintenance
                mechanisms.
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
#if defined(__linux__)
#include <linux/version.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#endif

#include "pmr.h"
#include "log2.h"
#include "device.h"
#include "pvrsrv.h"
#include "osfunc.h"
#include "cache_km.h"
#include "pvr_debug.h"
#include "lock_types.h"
#include "allocmem.h"
#include "process_stats.h"
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
#include "ri_server.h"
#endif
#include "devicemem.h"
#include "pvrsrv_apphint.h"
#include "pvrsrv_sync_server.h"
#include "km_apphint_defs.h"
#include "km_apphint_defs_common.h"
#include "oskm_apphint.h"
#include "di_server.h"

/* This header must always be included last */
#if defined(__linux__)
#include "kernel_compatibility.h"
#endif

/* Top-level file-local build definitions */
#if defined(PVRSRV_ENABLE_CACHEOP_STATS) && defined(__linux__)
#define CACHEOP_DEBUG
#define CACHEOP_STATS_ITEMS_MAX				32
#define INCR_WRAP(x)						((x+1) >= CACHEOP_STATS_ITEMS_MAX ? 0 : (x+1))
#define DECR_WRAP(x)						((x-1) < 0 ? (CACHEOP_STATS_ITEMS_MAX-1) : (x-1))
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
/* Refer to CacheOpStatsExecLogHeader() for header item names */
#define CACHEOP_RI_PRINTF_HEADER			"%-8s %-10s %-10s %-5s %-16s %-16s %-10s %-10s %-18s %-18s %-12s"
#define CACHEOP_RI_PRINTF					"%-8d %-10s %-10s %-5s 0x%-14llx 0x%-14llx 0x%-8llx 0x%-8llx %-18llu %-18llu 0x%-10x\n"
#else
#define CACHEOP_PRINTF_HEADER				"%-8s %-10s %-10s %-5s %-10s %-10s %-18s %-18s %-12s"
#define CACHEOP_PRINTF						"%-8d %-10s %-10s %-5s 0x%-8llx 0x%-8llx %-18llu %-18llu 0x%-10x\n"
#endif
#endif

//#define CACHEOP_NO_CACHE_LINE_ALIGNED_ROUNDING		/* Force OS page (not cache line) flush granularity */
#define CACHEOP_PVR_ASSERT(x)							/* Define as PVR_ASSERT(x), enable for swdev & testing */
#if defined(PVRSRV_SERVER_THREADS_INDEFINITE_SLEEP)
#define CACHEOP_THREAD_WAIT_TIMEOUT			0ULL		/* Wait indefinitely */
#else
#define CACHEOP_THREAD_WAIT_TIMEOUT			500000ULL	/* Wait 500ms between wait unless woken-up on demand */
#endif
#define CACHEOP_FENCE_WAIT_TIMEOUT			1000ULL		/* Wait 1ms between wait events unless woken-up */
#define CACHEOP_FENCE_RETRY_ABORT			1000ULL		/* Fence retries that aborts fence operation */
#define CACHEOP_SEQ_MIDPOINT (IMG_UINT32)	0x7FFFFFFF	/* Where seqNum(s) are rebase, compared at */
#define CACHEOP_ABORT_FENCE_ERROR_STRING	"detected stalled client, retrying cacheop fence"
#define CACHEOP_DEVMEM_OOR_ERROR_STRING		"cacheop device memory request is out of range"
#define CACHEOP_MAX_DEBUG_MESSAGE_LEN		160

typedef struct _CACHEOP_WORK_ITEM_
{
	PMR *psPMR;
	IMG_UINT32 ui32OpSeqNum;
	IMG_DEVMEM_SIZE_T uiSize;
	PVRSRV_CACHE_OP uiCacheOp;
	IMG_DEVMEM_OFFSET_T uiOffset;
	PVRSRV_TIMELINE iTimeline;
	SYNC_TIMELINE_OBJ sSWTimelineObj;
	PVRSRV_DEVICE_NODE *psDevNode;
#if defined(CACHEOP_DEBUG)
	IMG_UINT64 ui64EnqueuedTime;
	IMG_UINT64 ui64DequeuedTime;
	IMG_UINT64 ui64ExecuteTime;
	IMG_BOOL bDeferred;
	IMG_BOOL bKMReq;
	IMG_BOOL bUMF;
	IMG_PID pid;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
	RGXFWIF_DM eFenceOpType;
#endif
#endif
} CACHEOP_WORK_ITEM;

typedef struct _CACHEOP_STATS_EXEC_ITEM_
{
	IMG_PID pid;
	IMG_UINT32 ui32OpSeqNum;
	PVRSRV_CACHE_OP uiCacheOp;
	IMG_DEVMEM_SIZE_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT64 ui64EnqueuedTime;
	IMG_UINT64 ui64DequeuedTime;
	IMG_UINT64 ui64ExecuteTime;
	IMG_BOOL bIsFence;
	IMG_BOOL bKMReq;
	IMG_BOOL bUMF;
	IMG_BOOL bDeferred;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEV_PHYADDR sDevPAddr;
	RGXFWIF_DM eFenceOpType;
#endif
} CACHEOP_STATS_EXEC_ITEM;

typedef enum _CACHEOP_CONFIG_
{
	CACHEOP_CONFIG_DEFAULT = 0,
	/* cache flush mechanism types */
	CACHEOP_CONFIG_URBF    = 4,
	/* sw-emulated deferred flush mechanism */
	CACHEOP_CONFIG_KDF     = 8,
	/* pseudo configuration items */
	CACHEOP_CONFIG_LAST    = 16,
	CACHEOP_CONFIG_KLOG    = 16,
	CACHEOP_CONFIG_ALL     = 31
} CACHEOP_CONFIG;

typedef struct _CACHEOP_WORK_QUEUE_
{
/*
 * Init. state & primary device node framework
 * is anchored on.
 */
	IMG_BOOL bInit;
/*
  MMU page size/shift & d-cache line size
 */
	size_t uiPageSize;
	IMG_UINT32 uiLineSize;
	IMG_UINT32 uiLineShift;
	IMG_UINT32 uiPageShift;
	OS_CACHE_OP_ADDR_TYPE uiCacheOpAddrType;
/*
  CacheOp deferred queueing protocol
  + Implementation geared for performance, atomic counter based
	- Value Space is 0 -> 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> 7 -> 8 -> n.
	- Index Space is 0 -> 1 -> 2 -> 3 -> 0 -> 1 -> 2 -> 3 -> 0 -> m.
		- Index = Value modulo CACHEOP_INDICES_LOG2_SIZE.
  + Write counter never collides with read counter in index space
	- Unless at start of day when both are initialised to zero.
	- This means we sacrifice one entry when the queue is full.
	- Incremented by producer
		- Value space tracks total number of CacheOps queued.
		- Index space identifies CacheOp CCB queue index.
  + Read counter increments towards write counter in value space
	- Empty queue occurs when read equals write counter.
	- Wrap-round logic handled by consumer as/when needed.
	- Incremented by consumer
		- Value space tracks total # of CacheOps executed.
		- Index space identifies CacheOp CCB queue index.
  + Total queued size adjusted up/down during write/read activity
	- Counter might overflow but does not compromise framework.
 */
	ATOMIC_T hReadCounter;
	ATOMIC_T hWriteCounter;
/*
  CacheOp sequence numbers
  + hCommonSeqNum:
	- Common sequence, numbers every CacheOp operation in both UM/KM.
	- In KM
		- Every deferred CacheOp (on behalf of UM) gets a unique seqNum.
		- Last executed deferred CacheOp updates gsCwq.hCompletedSeqNum.
		- Under debug, all CacheOp gets a unique seqNum for tracking.
		- This includes all UM/KM synchronous non-deferred CacheOp(s)
	- In UM
		- CacheOp(s) discarding happens in both UM and KM space.
  + hCompletedSeqNum:
	- Tracks last executed KM/deferred RBF/Global<timeline> CacheOp(s)
 */
	ATOMIC_T hCommonSeqNum;
	ATOMIC_T hCompletedSeqNum;
/*
  CacheOp information page
  + psInfoPagePMR:
	- Single system-wide OS page that is multi-mapped in UM/KM.
	- Mapped into clients using read-only memory protection.
	- Mapped into server using read/write memory protection.
	- Contains information pertaining to cache framework.
  + pui32InfoPage:
	- Server linear address pointer to said information page.
	- Each info-page entry currently of sizeof(IMG_UINT32).
 */
	PMR *psInfoPagePMR;
	IMG_UINT32 *pui32InfoPage;
/*
  CacheOp deferred work-item queue
  + CACHEOP_INDICES_LOG2_SIZE
 */
#define CACHEOP_INDICES_LOG2_SIZE	(4)
#define CACHEOP_INDICES_MAX			(1 << CACHEOP_INDICES_LOG2_SIZE)
#define CACHEOP_INDICES_MASK		(CACHEOP_INDICES_MAX-1)
	CACHEOP_WORK_ITEM asWorkItems[CACHEOP_INDICES_MAX];
#if defined(CACHEOP_DEBUG)
/*
  CacheOp statistics
 */
	DI_ENTRY *psDIEntry;
	IMG_HANDLE hStatsExecLock;
	IMG_UINT32 ui32ServerASync;
	IMG_UINT32 ui32ServerSyncVA;
	IMG_UINT32 ui32ServerSync;
	IMG_UINT32 ui32ServerRBF;
	IMG_UINT32 ui32ServerDTL;
	IMG_UINT32 ui32ClientSync;
	IMG_UINT32 ui32ClientRBF;
	IMG_UINT32 ui32TotalFenceOps;
	IMG_UINT32 ui32TotalExecOps;
	IMG_UINT32 ui32AvgExecTime;
	IMG_UINT32 ui32AvgExecTimeRemainder;
	IMG_UINT32 ui32AvgFenceTime;
	IMG_UINT32 ui32AvgFenceTimeRemainder;
	IMG_INT32 i32StatsExecWriteIdx;
	CACHEOP_STATS_EXEC_ITEM asStatsExecuted[CACHEOP_STATS_ITEMS_MAX];
#endif
/*
  CacheOp (re)configuration
 */
	DI_ENTRY *psConfigTune;
	IMG_HANDLE hConfigLock;
/*
  CacheOp deferred worker thread
  + eConfig
	- Runtime configuration
  + hWorkerThread
	- CacheOp thread handler
  + hThreadWakeUpEvtObj
	- Event object to drive CacheOp worker thread sleep/wake-ups.
  + hClientWakeUpEvtObj
	- Event object to unblock stalled clients waiting on queue.
 */
	CACHEOP_CONFIG	eConfig;
	IMG_UINT32		ui32Config;
	IMG_HANDLE		hWorkerThread;
	IMG_HANDLE		hDeferredLock;
	IMG_HANDLE		hThreadWakeUpEvtObj;
	IMG_HANDLE		hClientWakeUpEvtObj;
	IMG_UINT32		ui32FenceWaitTimeUs;
	IMG_UINT32		ui32FenceRetryAbort;
	IMG_BOOL		bSupportsUMFlush;
} CACHEOP_WORK_QUEUE;

/* Top-level CacheOp framework object */
static CACHEOP_WORK_QUEUE gsCwq;

#define CacheOpConfigSupports(e) ((gsCwq.eConfig & (e)) ? IMG_TRUE : IMG_FALSE)

extern void do_invalid_range(unsigned long start, unsigned long len);




static INLINE IMG_UINT32 CacheOpIdxRead(ATOMIC_T *phCounter)
{
	IMG_UINT32 ui32Idx = OSAtomicRead(phCounter);
	return ui32Idx & CACHEOP_INDICES_MASK;
}

static INLINE IMG_UINT32 CacheOpIdxIncrement(ATOMIC_T *phCounter)
{
	IMG_UINT32 ui32Idx = OSAtomicIncrement(phCounter);
	return ui32Idx & CACHEOP_INDICES_MASK;
}

static INLINE IMG_UINT32 CacheOpIdxNext(ATOMIC_T *phCounter)
{
	IMG_UINT32 ui32Idx = OSAtomicRead(phCounter);
	return ++ui32Idx & CACHEOP_INDICES_MASK;
}

static INLINE IMG_UINT32 CacheOpIdxSpan(ATOMIC_T *phLhs, ATOMIC_T *phRhs)
{
	return OSAtomicRead(phLhs) - OSAtomicRead(phRhs);
}

/* Callback to dump info of cacheop thread in debug_dump */
static void CacheOpThreadDumpInfo(DUMPDEBUG_PRINTF_FUNC* pfnDumpDebugPrintf,
								void *pvDumpDebugFile)
{
	PVR_DUMPDEBUG_LOG("    Configuration: QSZ: %d, UKT: %d, KDFT: %d, "
			  "LINESIZE: %d, PGSIZE: %d, KDF: %s, "
			  "URBF: %s",
			  CACHEOP_INDICES_MAX,
			  gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD],
			  gsCwq.pui32InfoPage[CACHEOP_INFO_KMDFTHRESHLD],
			  gsCwq.pui32InfoPage[CACHEOP_INFO_LINESIZE],
			  gsCwq.pui32InfoPage[CACHEOP_INFO_PGSIZE],
			  gsCwq.eConfig & CACHEOP_CONFIG_KDF  ? "Yes" : "No",
			  gsCwq.eConfig & CACHEOP_CONFIG_URBF ? "Yes" : "No"
			  );
	PVR_DUMPDEBUG_LOG("    Pending deferred CacheOp entries : %u",
			  CacheOpIdxSpan(&gsCwq.hWriteCounter, &gsCwq.hReadCounter));
}

#if defined(CACHEOP_DEBUG)
static INLINE void CacheOpStatsExecLogHeader(IMG_CHAR szBuffer[CACHEOP_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(szBuffer, CACHEOP_MAX_DEBUG_MESSAGE_LEN,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
				CACHEOP_RI_PRINTF_HEADER,
#else
				CACHEOP_PRINTF_HEADER,
#endif
				"Pid",
				"CacheOp",
				"  Type",
				"Mode",
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
				"DevVAddr",
				"DevPAddr",
#endif
				"Offset",
				"Size",
				"xTime (us)",
				"qTime (us)",
				"SeqNum");
}

static void CacheOpStatsExecLogWrite(CACHEOP_WORK_ITEM *psCacheOpWorkItem)
{
	IMG_UINT64 ui64ExecuteTime;
	IMG_UINT64 ui64EnqueuedTime;
	IMG_INT32 i32WriteOffset;

	if (!psCacheOpWorkItem->ui32OpSeqNum && !psCacheOpWorkItem->uiCacheOp)
	{
		/* This breaks the logic of read-out, so we do not queue items
		   with zero sequence number and no CacheOp */
		return;
	}
	else if (psCacheOpWorkItem->bKMReq && !CacheOpConfigSupports(CACHEOP_CONFIG_KLOG))
	{
		/* KM logs spams the history due to frequency, this removes it completely */
		return;
	}

	OSLockAcquire(gsCwq.hStatsExecLock);

	i32WriteOffset = gsCwq.i32StatsExecWriteIdx;
	gsCwq.asStatsExecuted[i32WriteOffset].pid = psCacheOpWorkItem->pid;
	gsCwq.i32StatsExecWriteIdx = INCR_WRAP(gsCwq.i32StatsExecWriteIdx);
	gsCwq.asStatsExecuted[i32WriteOffset].bUMF = psCacheOpWorkItem->bUMF;
	gsCwq.asStatsExecuted[i32WriteOffset].uiSize = psCacheOpWorkItem->uiSize;
	gsCwq.asStatsExecuted[i32WriteOffset].bKMReq = psCacheOpWorkItem->bKMReq;
	gsCwq.asStatsExecuted[i32WriteOffset].uiOffset	= psCacheOpWorkItem->uiOffset;
	gsCwq.asStatsExecuted[i32WriteOffset].uiCacheOp = psCacheOpWorkItem->uiCacheOp;
	gsCwq.asStatsExecuted[i32WriteOffset].bDeferred = psCacheOpWorkItem->bDeferred;
	gsCwq.asStatsExecuted[i32WriteOffset].ui32OpSeqNum	= psCacheOpWorkItem->ui32OpSeqNum;
	gsCwq.asStatsExecuted[i32WriteOffset].ui64ExecuteTime = psCacheOpWorkItem->ui64ExecuteTime;
	gsCwq.asStatsExecuted[i32WriteOffset].ui64EnqueuedTime = psCacheOpWorkItem->ui64EnqueuedTime;
	gsCwq.asStatsExecuted[i32WriteOffset].ui64DequeuedTime = psCacheOpWorkItem->ui64DequeuedTime;
	/* During early system initialisation, only non-fence & non-PMR CacheOps are processed */
	gsCwq.asStatsExecuted[i32WriteOffset].bIsFence = gsCwq.bInit && !psCacheOpWorkItem->psPMR;
	CACHEOP_PVR_ASSERT(gsCwq.asStatsExecuted[i32WriteOffset].pid);
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
	if (gsCwq.bInit && psCacheOpWorkItem->psPMR)
	{
		IMG_CPU_PHYADDR sDevPAddr;
		PVRSRV_ERROR eError, eLockError;
		IMG_BOOL bValid;

		/* Get more detailed information regarding the sub allocations that
		   PMR has from RI manager for process that requested the CacheOp */
		eError = RIDumpProcessListKM(psCacheOpWorkItem->psPMR,
									 gsCwq.asStatsExecuted[i32WriteOffset].pid,
									 gsCwq.asStatsExecuted[i32WriteOffset].uiOffset,
									 &gsCwq.asStatsExecuted[i32WriteOffset].sDevVAddr);
		PVR_GOTO_IF_ERROR(eError, e0);

		/* (Re)lock here as some PMR might have not been locked */
		eLockError = PMRLockSysPhysAddresses(psCacheOpWorkItem->psPMR);
		PVR_GOTO_IF_ERROR(eLockError, e0);

		eError = PMR_CpuPhysAddr(psCacheOpWorkItem->psPMR,
								 gsCwq.uiPageShift,
								 1,
								 gsCwq.asStatsExecuted[i32WriteOffset].uiOffset,
								 &sDevPAddr,
								 &bValid);

		eLockError = PMRUnlockSysPhysAddresses(psCacheOpWorkItem->psPMR);
		PVR_LOG_IF_ERROR(eLockError, "PMRUnlockSysPhysAddresses");

		PVR_GOTO_IF_ERROR(eError, e0);



		gsCwq.asStatsExecuted[i32WriteOffset].sDevPAddr.uiAddr = sDevPAddr.uiAddr;
	}

	if (gsCwq.asStatsExecuted[i32WriteOffset].bIsFence)
	{
		gsCwq.asStatsExecuted[i32WriteOffset].eFenceOpType = psCacheOpWorkItem->eFenceOpType;
	}
#endif

	{
		/* Convert timing from nanoseconds to microseconds */
		IMG_UINT64 ui64ExecuteTimeNs = gsCwq.asStatsExecuted[i32WriteOffset].ui64ExecuteTime;
		IMG_UINT64 ui64EnqueuedTimeNs = gsCwq.asStatsExecuted[i32WriteOffset].ui64EnqueuedTime;

		do_div(ui64ExecuteTimeNs, 1000);
		do_div(ui64EnqueuedTimeNs, 1000);

		ui64ExecuteTime = ui64ExecuteTimeNs;
		ui64EnqueuedTime = ui64EnqueuedTimeNs;
	}

	/* Coalesced deferred CacheOps do not contribute to statistics,
	   as both enqueue/execute time is identical for these CacheOps */
	if (!gsCwq.asStatsExecuted[i32WriteOffset].bIsFence)
	{
		/* Calculate the approximate cumulative moving average execution time.
		 * This calculation is based on standard equation:
		 *
		 * CMAnext = (new + count * CMAprev) / (count + 1)
		 *
		 * but in simplified form:
		 *
		 * CMAnext = CMAprev + (new - CMAprev) / (count + 1)
		 *
		 * this gets rid of multiplication and prevents overflow.
		 *
		 * Also to increase accuracy that we lose with integer division,
		 * we hold the moving remainder of the division and add it.
		 *
		 * CMAnext = CMAprev + (new - CMAprev + CMRprev) / (count + 1)
		 *
		 * Multiple tests proved it to be the best solution for approximating
		 * CMA using integers.
		 *
		 */

		IMG_UINT32 ui32Time = ui64ExecuteTime - ui64EnqueuedTime;
		IMG_INT32 i32Div = (IMG_INT32)ui32Time - (IMG_INT32)gsCwq.ui32AvgExecTime + (IMG_INT32)gsCwq.ui32AvgExecTimeRemainder;

		gsCwq.ui32AvgExecTime += i32Div / (IMG_INT32)(gsCwq.ui32TotalExecOps + 1);
		gsCwq.ui32AvgExecTimeRemainder = i32Div % (IMG_INT32)(gsCwq.ui32TotalExecOps + 1);

		gsCwq.ui32TotalExecOps++;
	}

	if (!gsCwq.asStatsExecuted[i32WriteOffset].bKMReq)
	{
		/* This operation queues only UM CacheOp in per-PID process statistics database */
		PVRSRVStatsUpdateCacheOpStats(gsCwq.asStatsExecuted[i32WriteOffset].uiCacheOp,
						gsCwq.asStatsExecuted[i32WriteOffset].ui32OpSeqNum,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
						gsCwq.asStatsExecuted[i32WriteOffset].sDevVAddr,
						gsCwq.asStatsExecuted[i32WriteOffset].sDevPAddr,
						gsCwq.asStatsExecuted[i32WriteOffset].eFenceOpType,
#endif
						gsCwq.asStatsExecuted[i32WriteOffset].uiOffset,
						gsCwq.asStatsExecuted[i32WriteOffset].uiSize,
						ui64ExecuteTime-ui64EnqueuedTime,
						gsCwq.asStatsExecuted[i32WriteOffset].bUMF,
						gsCwq.asStatsExecuted[i32WriteOffset].bIsFence,
						psCacheOpWorkItem->pid);
	}

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
e0:
#endif
	OSLockRelease(gsCwq.hStatsExecLock);
}

static int CacheOpStatsExecLogRead(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	IMG_CHAR *pszFlushype;
	IMG_CHAR *pszCacheOpType;
	IMG_CHAR *pszFlushSource;
	IMG_INT32 i32ReadOffset;
	IMG_INT32 i32WriteOffset;
	IMG_UINT64 ui64EnqueuedTime;
	IMG_UINT64 ui64DequeuedTime;
	IMG_UINT64 ui64ExecuteTime;
	IMG_CHAR szBuffer[CACHEOP_MAX_DEBUG_MESSAGE_LEN] = {0};
	PVR_UNREFERENCED_PARAMETER(pvData);

	OSLockAcquire(gsCwq.hStatsExecLock);

	DIPrintf(psEntry,
			"Primary CPU d-cache architecture: LSZ: 0x%d, URBF: %s\n",
			gsCwq.uiLineSize,
			gsCwq.bSupportsUMFlush ? "Yes" : "No"
		);

	DIPrintf(psEntry,
			"Configuration: QSZ: %d, UKT: %d, KDFT: %d, KDF: %s, URBF: %s\n",
			CACHEOP_INDICES_MAX,
			gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD],
			gsCwq.pui32InfoPage[CACHEOP_INFO_KMDFTHRESHLD],
			gsCwq.eConfig & CACHEOP_CONFIG_KDF  ? "Yes" : "No",
			gsCwq.eConfig & CACHEOP_CONFIG_URBF ? "Yes" : "No"
		);

	DIPrintf(psEntry,
			"Summary: OP[F][TL] (tot.avg): %d.%d/%d.%d/%d, [KM][UM][A]SYNC: %d.%d/%d/%d, RBF (um/km): %d/%d\n",
			gsCwq.ui32TotalExecOps, gsCwq.ui32AvgExecTime, gsCwq.ui32TotalFenceOps, gsCwq.ui32AvgFenceTime, gsCwq.ui32ServerDTL,
			gsCwq.ui32ServerSync, gsCwq.ui32ServerSyncVA, gsCwq.ui32ClientSync,	gsCwq.ui32ServerASync,
			gsCwq.ui32ClientRBF,   gsCwq.ui32ServerRBF
		);

	CacheOpStatsExecLogHeader(szBuffer);
	DIPrintf(psEntry, "%s\n", szBuffer);

	i32WriteOffset = gsCwq.i32StatsExecWriteIdx;
	for (i32ReadOffset = DECR_WRAP(i32WriteOffset);
		 i32ReadOffset != i32WriteOffset;
		 i32ReadOffset = DECR_WRAP(i32ReadOffset))
	{
		if (!gsCwq.asStatsExecuted[i32ReadOffset].ui32OpSeqNum &&
			!gsCwq.asStatsExecuted[i32ReadOffset].uiCacheOp)
		{
			break;
		}

		{
			/* Convert from nano-seconds to micro-seconds */
			IMG_UINT64 ui64ExecuteTimeNs = gsCwq.asStatsExecuted[i32ReadOffset].ui64ExecuteTime;
			IMG_UINT64 ui64EnqueuedTimeNs = gsCwq.asStatsExecuted[i32ReadOffset].ui64EnqueuedTime;
			IMG_UINT64 ui64DequeuedTimeNs = gsCwq.asStatsExecuted[i32ReadOffset].ui64DequeuedTime;

			do_div(ui64ExecuteTimeNs, 1000);
			do_div(ui64EnqueuedTimeNs, 1000);
			do_div(ui64DequeuedTimeNs, 1000);

			ui64ExecuteTime  = ui64ExecuteTimeNs;
			ui64EnqueuedTime = ui64EnqueuedTimeNs;
			ui64DequeuedTime = ui64DequeuedTimeNs;
		}

		if (gsCwq.asStatsExecuted[i32ReadOffset].bIsFence)
		{
			IMG_CHAR *pszMode = "";
			IMG_CHAR *pszFenceType = "";
			pszCacheOpType = "Fence";

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
			switch (gsCwq.asStatsExecuted[i32ReadOffset].eFenceOpType)
			{
				case RGXFWIF_DM_GP:
					pszFenceType = "  GP ";
					break;

				case RGXFWIF_DM_TDM:
					pszFenceType = "  TDM ";
					break;

				case RGXFWIF_DM_GEOM:
					pszFenceType = " GEOM";
					break;

				case RGXFWIF_DM_3D:
					pszFenceType = "  3D ";
					break;

				case RGXFWIF_DM_CDM:
					pszFenceType = "  CDM ";
					break;

				default:
					pszFenceType = "  DM? ";
					CACHEOP_PVR_ASSERT(0);
					break;
			}
#endif

			DIPrintf(psEntry,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							CACHEOP_RI_PRINTF,
#else
							CACHEOP_PRINTF,
#endif
							gsCwq.asStatsExecuted[i32ReadOffset].pid,
							pszCacheOpType,
							pszFenceType,
							pszMode,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							0ull,
							0ull,
#endif
							gsCwq.asStatsExecuted[i32ReadOffset].uiOffset,
							gsCwq.asStatsExecuted[i32ReadOffset].uiSize,
							ui64ExecuteTime - ui64EnqueuedTime,
							ui64DequeuedTime ? ui64DequeuedTime - ui64EnqueuedTime : 0, /* CacheOp might not have a valid DequeuedTime */
							gsCwq.asStatsExecuted[i32ReadOffset].ui32OpSeqNum);
		}
		else
		{
			IMG_DEVMEM_SIZE_T ui64NumOfPages;

			ui64NumOfPages = gsCwq.asStatsExecuted[i32ReadOffset].uiSize >> gsCwq.uiPageShift;
			if (ui64NumOfPages <= PMR_MAX_TRANSLATION_STACK_ALLOC)
			{
				pszFlushype = "RBF.Fast";
			}
			else
			{
				pszFlushype = "RBF.Slow";
			}

			if (gsCwq.asStatsExecuted[i32ReadOffset].bUMF)
			{
				pszFlushSource = " UM";
			}
			else
			{
				/*
				   - Request originates directly from a KM thread or in KM (KM<), or
				   - Request originates from a UM thread and is KM deferred (KM+), or
				*/
				pszFlushSource =
					gsCwq.asStatsExecuted[i32ReadOffset].bKMReq ? " KM<" :
					gsCwq.asStatsExecuted[i32ReadOffset].bDeferred && gsCwq.asStatsExecuted[i32ReadOffset].ui64ExecuteTime ? " KM+" :
					!gsCwq.asStatsExecuted[i32ReadOffset].ui64ExecuteTime ? " KM-" : " KM";
			}

			switch (gsCwq.asStatsExecuted[i32ReadOffset].uiCacheOp)
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
				case PVRSRV_CACHE_OP_TIMELINE:
					pszCacheOpType = "Timeline";
					pszFlushype = "      ";
					break;
				default:
					pszCacheOpType = "Unknown";
					gsCwq.asStatsExecuted[i32ReadOffset].ui32OpSeqNum =
							(IMG_UINT32) gsCwq.asStatsExecuted[i32ReadOffset].uiCacheOp;
					break;
			}

			DIPrintf(psEntry,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							CACHEOP_RI_PRINTF,
#else
							CACHEOP_PRINTF,
#endif
							gsCwq.asStatsExecuted[i32ReadOffset].pid,
							pszCacheOpType,
							pszFlushype,
							pszFlushSource,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
							gsCwq.asStatsExecuted[i32ReadOffset].sDevVAddr.uiAddr,
							gsCwq.asStatsExecuted[i32ReadOffset].sDevPAddr.uiAddr,
#endif
							gsCwq.asStatsExecuted[i32ReadOffset].uiOffset,
							gsCwq.asStatsExecuted[i32ReadOffset].uiSize,
							ui64ExecuteTime - ui64EnqueuedTime,
							ui64DequeuedTime ? ui64DequeuedTime - ui64EnqueuedTime : 0, /* CacheOp might not have a valid DequeuedTime */
							gsCwq.asStatsExecuted[i32ReadOffset].ui32OpSeqNum);
		}
	}

	OSLockRelease(gsCwq.hStatsExecLock);

	return 0;
}
#endif /* defined(CACHEOP_DEBUG) */

static INLINE void CacheOpStatsReset(void)
{
#if defined(CACHEOP_DEBUG)
	gsCwq.ui32TotalExecOps			= 0;
	gsCwq.ui32TotalFenceOps			= 0;
	gsCwq.ui32AvgExecTime			= 0;
	gsCwq.ui32AvgExecTimeRemainder	= 0;
	gsCwq.ui32AvgFenceTime			= 0;
	gsCwq.ui32AvgFenceTimeRemainder	= 0;
	gsCwq.ui32ClientRBF				= 0;
	gsCwq.ui32ClientSync			= 0;
	gsCwq.ui32ServerRBF				= 0;
	gsCwq.ui32ServerASync			= 0;
	gsCwq.ui32ServerSyncVA			= 0;
	gsCwq.ui32ServerSync			= 0;
	gsCwq.ui32ServerDTL				= 0;
	gsCwq.i32StatsExecWriteIdx		= 0;
	OSCachedMemSet(gsCwq.asStatsExecuted, 0, sizeof(gsCwq.asStatsExecuted));
#endif
}

static void CacheOpConfigUpdate(IMG_UINT32 ui32Config)
{
	OSLockAcquire(gsCwq.hConfigLock);

	/* Step 0, set the gsCwq.eConfig bits */
	if (!(ui32Config & (CACHEOP_CONFIG_LAST - 1)))
	{
		gsCwq.eConfig = CACHEOP_CONFIG_KDF;
		if (gsCwq.bSupportsUMFlush)
		{
			gsCwq.eConfig |= CACHEOP_CONFIG_URBF;
		}
	}
	else
	{
		if (ui32Config & CACHEOP_CONFIG_KDF)
		{
			gsCwq.eConfig |= CACHEOP_CONFIG_KDF;
		}
		else
		{
			gsCwq.eConfig &= ~CACHEOP_CONFIG_KDF;
		}

		if (gsCwq.bSupportsUMFlush && (ui32Config & CACHEOP_CONFIG_URBF))
		{
			gsCwq.eConfig |= CACHEOP_CONFIG_URBF;
		}
		else
		{
			gsCwq.eConfig &= ~CACHEOP_CONFIG_URBF;
		}
	}

	if (ui32Config & CACHEOP_CONFIG_KLOG)
	{
		/* Suppress logs from KM caller */
		gsCwq.eConfig |= CACHEOP_CONFIG_KLOG;
	}
	else
	{
		gsCwq.eConfig &= ~CACHEOP_CONFIG_KLOG;
	}

	/* Step 1, set gsCwq.ui32Config based on gsCwq.eConfig */
	ui32Config = 0;

	if (gsCwq.eConfig & CACHEOP_CONFIG_KDF)
	{
		ui32Config |= CACHEOP_CONFIG_KDF;
	}
	if (gsCwq.eConfig & CACHEOP_CONFIG_URBF)
	{
		ui32Config |= CACHEOP_CONFIG_URBF;
	}
	if (gsCwq.eConfig & CACHEOP_CONFIG_KLOG)
	{
		ui32Config |= CACHEOP_CONFIG_KLOG;
	}
	gsCwq.ui32Config = ui32Config;


	/* Step 3, in certain cases where a CacheOp/VA is provided, this threshold determines at what point
	   the optimisation due to the presence of said VA (i.e. us not having to remap the PMR pages in KM)
	   is clawed-back because of the overhead of maintaining such large request which might stalls the
	   user thread; so to hide this latency have these CacheOps executed on deferred CacheOp thread */
	gsCwq.pui32InfoPage[CACHEOP_INFO_KMDFTHRESHLD] = (IMG_UINT32)(PVR_DIRTY_BYTES_FLUSH_THRESHOLD >> 2);

	/* Step 4, if no UM support, all requests are done in KM so zero these forcing all client requests
	   to come down into the KM for maintenance */
	gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD] = 0;

	if (gsCwq.bSupportsUMFlush)
	{
		/* With URBF enabled we never go to the kernel */
		if (gsCwq.eConfig & CACHEOP_CONFIG_URBF)
		{
			gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD] = (IMG_UINT32)~0;
		}
	}

	/* Step 5, reset stats. */
	CacheOpStatsReset();

	OSLockRelease(gsCwq.hConfigLock);
}

static int CacheOpConfigRead(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	PVR_UNREFERENCED_PARAMETER(pvData);
	DIPrintf(psEntry,
			"KDF: %s, URBF: %s\n",
			gsCwq.eConfig & CACHEOP_CONFIG_KDF  ? "Yes" : "No",
			gsCwq.eConfig & CACHEOP_CONFIG_URBF ? "Yes" : "No"
		);
	return 0;
}

static INLINE PVRSRV_ERROR CacheOpConfigQuery(const PVRSRV_DEVICE_NODE *psDevNode,
											const void *psPrivate,
											IMG_UINT32 *pui32Value)
{
	IMG_UINT32 ui32ID = (IMG_UINT32)(uintptr_t) psPrivate;
	PVR_UNREFERENCED_PARAMETER(psDevNode);

	switch (ui32ID)
	{
		case APPHINT_ID_CacheOpConfig:
			*pui32Value = gsCwq.ui32Config;
			break;

		case APPHINT_ID_CacheOpUMKMThresholdSize:
			*pui32Value = gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD];
			break;

		default:
			break;
	}

	return PVRSRV_OK;
}

static INLINE PVRSRV_ERROR CacheOpConfigSet(const PVRSRV_DEVICE_NODE *psDevNode,
											const void *psPrivate,
											IMG_UINT32 ui32Value)
{
	IMG_UINT32 ui32ID = (IMG_UINT32)(uintptr_t) psPrivate;
	PVR_UNREFERENCED_PARAMETER(psDevNode);

	switch (ui32ID)
	{
		case APPHINT_ID_CacheOpConfig:
			CacheOpConfigUpdate(ui32Value & CACHEOP_CONFIG_ALL);
			break;


		case APPHINT_ID_CacheOpUMKMThresholdSize:
		{
			if (!ui32Value || !gsCwq.bSupportsUMFlush)
			{
				/* CPU ISA does not support UM flush, therefore every request goes down into
				   the KM, silently ignore request to adjust threshold */
				PVR_ASSERT(! gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD]);
				break;
			}
			else if (ui32Value < gsCwq.uiPageSize)
			{
				/* Silently round-up to OS page size */
				ui32Value = gsCwq.uiPageSize;
			}

			/* Align to OS page size */
			ui32Value &= ~(gsCwq.uiPageSize - 1);

			gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD] = ui32Value;

			break;
		}

		default:
			break;
	}

	return PVRSRV_OK;
}

static INLINE IMG_UINT32 CacheOpGetNextCommonSeqNum(void)
{
	IMG_UINT32 ui32SeqNum = OSAtomicIncrement(&gsCwq.hCommonSeqNum);
	if (! ui32SeqNum)
	{
		ui32SeqNum = OSAtomicIncrement(&gsCwq.hCommonSeqNum);
	}
	return ui32SeqNum;
}

static INLINE IMG_BOOL CacheOpFenceCheck(IMG_UINT32 ui32CompletedSeqNum,
										 IMG_UINT32 ui32FenceSeqNum)
{
	IMG_UINT32 ui32RebasedCompletedNum;
	IMG_UINT32 ui32RebasedFenceNum;
	IMG_UINT32 ui32Rebase;

	if (ui32FenceSeqNum == 0)
	{
		return IMG_TRUE;
	}

	/*
	   The problem statement is how to compare two values on
	   a numerical sequentially incrementing timeline in the
	   presence of wrap around arithmetic semantics using a
	   single ui32 counter & atomic (increment) operations.

	   The rationale for the solution here is to rebase the
	   incoming values to the sequence midpoint and perform
	   comparisons there; this allows us to handle overflow
	   or underflow wrap-round using only a single integer.

	   NOTE: Here we assume that the absolute value of the
	   difference between the two incoming values in _not_
	   greater than CACHEOP_SEQ_MIDPOINT. This assumption
	   holds as it implies that it is very _unlikely_ that 2
	   billion CacheOp requests could have been made between
	   a single client's CacheOp request & the corresponding
	   fence check. This code sequence is hopefully a _more_
	   hand optimised (branchless) version of this:

		   x = ui32CompletedOpSeqNum
		   y = ui32FenceOpSeqNum

		   if (|x - y| < CACHEOP_SEQ_MIDPOINT)
			   return (x - y) >= 0 ? true : false
		   else
			   return (y - x) >= 0 ? true : false
	 */
	ui32Rebase = CACHEOP_SEQ_MIDPOINT - ui32CompletedSeqNum;

	/* ui32Rebase could be either positive/negative, in
	   any case we still perform operation using unsigned
	   semantics as 2's complement notation always means
	   we end up with the correct result */
	ui32RebasedCompletedNum = ui32Rebase + ui32CompletedSeqNum;
	ui32RebasedFenceNum = ui32Rebase + ui32FenceSeqNum;

	return (ui32RebasedCompletedNum >= ui32RebasedFenceNum);
}

static INLINE PVRSRV_ERROR CacheOpTimelineBind(PVRSRV_DEVICE_NODE *psDevNode,
											   CACHEOP_WORK_ITEM *psCacheOpWorkItem,
											   PVRSRV_TIMELINE iTimeline)
{
	PVRSRV_ERROR eError;

	/* Always default the incoming CacheOp work-item to safe values */
	SyncClearTimelineObj(&psCacheOpWorkItem->sSWTimelineObj);
	psCacheOpWorkItem->iTimeline = PVRSRV_NO_TIMELINE;
	psCacheOpWorkItem->psDevNode = psDevNode;
	if (iTimeline == PVRSRV_NO_TIMELINE)
	{
		return PVRSRV_OK;
	}

	psCacheOpWorkItem->iTimeline = iTimeline;
	eError = SyncSWGetTimelineObj(iTimeline, &psCacheOpWorkItem->sSWTimelineObj);
	PVR_LOG_IF_ERROR(eError, "SyncSWGetTimelineObj");

	return eError;
}

static INLINE PVRSRV_ERROR CacheOpTimelineExec(CACHEOP_WORK_ITEM *psCacheOpWorkItem)
{
	PVRSRV_ERROR eError;

	if (psCacheOpWorkItem->iTimeline == PVRSRV_NO_TIMELINE)
	{
		return PVRSRV_OK;
	}
	CACHEOP_PVR_ASSERT(psCacheOpWorkItem->sSWTimelineObj.pvTlObj);

	eError = SyncSWTimelineAdvanceKM(psCacheOpWorkItem->psDevNode,
	                                 &psCacheOpWorkItem->sSWTimelineObj);
	(void) SyncSWTimelineReleaseKM(&psCacheOpWorkItem->sSWTimelineObj);

	return eError;
}

static INLINE void CacheOpExecRangeBased(PVRSRV_DEVICE_NODE *psDevNode,
										PVRSRV_CACHE_OP uiCacheOp,
										IMG_BYTE *pbCpuVirtAddr,
										IMG_CPU_PHYADDR sCpuPhyAddr,
										IMG_DEVMEM_OFFSET_T uiPgAlignedOffset,
										IMG_DEVMEM_OFFSET_T uiCLAlignedStartOffset,
										IMG_DEVMEM_OFFSET_T uiCLAlignedEndOffset)
{
	IMG_BYTE *pbCpuVirtAddrEnd;
	IMG_BYTE *pbCpuVirtAddrStart;
	IMG_CPU_PHYADDR sCpuPhyAddrEnd;
	IMG_CPU_PHYADDR sCpuPhyAddrStart;
	IMG_DEVMEM_SIZE_T uiRelFlushSize;
	IMG_DEVMEM_OFFSET_T uiRelFlushOffset;
	IMG_DEVMEM_SIZE_T uiNextPgAlignedOffset;

	/* These quantities allows us to perform cache operations
	   at cache-line granularity thereby ensuring we do not
	   perform more than is necessary */
	CACHEOP_PVR_ASSERT(uiPgAlignedOffset < uiCLAlignedEndOffset);
	uiRelFlushSize = (IMG_DEVMEM_SIZE_T)gsCwq.uiPageSize;
	uiRelFlushOffset = 0;

	if (uiCLAlignedStartOffset > uiPgAlignedOffset)
	{
		/* Zero unless initially starting at an in-page offset */
		uiRelFlushOffset = uiCLAlignedStartOffset - uiPgAlignedOffset;
		uiRelFlushSize -= uiRelFlushOffset;
	}

	/* uiRelFlushSize is gsCwq.uiPageSize unless current outstanding CacheOp
	   size is smaller. The 1st case handles in-page CacheOp range and
	   the 2nd case handles multiple-page CacheOp range with a last
	   CacheOp size that is less than gsCwq.uiPageSize */
	uiNextPgAlignedOffset = uiPgAlignedOffset + (IMG_DEVMEM_SIZE_T)gsCwq.uiPageSize;
	if (uiNextPgAlignedOffset < uiPgAlignedOffset)
	{
		/* uiNextPgAlignedOffset is greater than uiCLAlignedEndOffset
		   by implication of this wrap-round; this only happens when
		   uiPgAlignedOffset is the last page aligned offset */
		uiRelFlushSize = uiRelFlushOffset ?
				uiCLAlignedEndOffset - uiCLAlignedStartOffset :
				uiCLAlignedEndOffset - uiPgAlignedOffset;
	}
	else
	{
		if (uiNextPgAlignedOffset > uiCLAlignedEndOffset)
		{
			uiRelFlushSize = uiRelFlushOffset ?
					uiCLAlignedEndOffset - uiCLAlignedStartOffset :
					uiCLAlignedEndOffset - uiPgAlignedOffset;
		}
	}

	/* More efficient to request cache maintenance operation for full
	   relative range as opposed to multiple cache-aligned ranges */
	sCpuPhyAddrStart.uiAddr = sCpuPhyAddr.uiAddr + uiRelFlushOffset;
	sCpuPhyAddrEnd.uiAddr = sCpuPhyAddrStart.uiAddr + uiRelFlushSize;
	if (pbCpuVirtAddr)
	{
		pbCpuVirtAddrStart = pbCpuVirtAddr + uiRelFlushOffset;
		pbCpuVirtAddrEnd = pbCpuVirtAddrStart + uiRelFlushSize;
	}
	else
	{
		/* Some OS/Env layer support functions expect NULL(s) */
		pbCpuVirtAddrStart = NULL;
		pbCpuVirtAddrEnd = NULL;
	}

	/* Perform requested CacheOp on the CPU data cache for successive cache
	   line worth of bytes up to page or in-page cache-line boundary */
	switch (uiCacheOp)
	{
		case PVRSRV_CACHE_OP_CLEAN:
			OSCPUCacheCleanRangeKM(psDevNode, pbCpuVirtAddrStart, pbCpuVirtAddrEnd,
									sCpuPhyAddrStart, sCpuPhyAddrEnd);
			break;
		case PVRSRV_CACHE_OP_INVALIDATE:
			OSCPUCacheInvalidateRangeKM(psDevNode, pbCpuVirtAddrStart, pbCpuVirtAddrEnd,
									sCpuPhyAddrStart, sCpuPhyAddrEnd);
			break;
		case PVRSRV_CACHE_OP_FLUSH:
			OSCPUCacheFlushRangeKM(psDevNode, pbCpuVirtAddrStart, pbCpuVirtAddrEnd,
									sCpuPhyAddrStart, sCpuPhyAddrEnd);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,	"%s: Invalid cache operation type %d",
					__func__, uiCacheOp));
			break;
	}

#if defined(CACHEOP_DEBUG)
	/* Tracks the number of kernel-mode cacheline maintenance instructions */
	gsCwq.ui32ServerRBF += (uiRelFlushSize & ((IMG_DEVMEM_SIZE_T)~(gsCwq.uiLineSize - 1))) >> gsCwq.uiLineShift;
#endif
}

static INLINE void CacheOpExecRangeBasedVA(PVRSRV_DEVICE_NODE *psDevNode,
										 IMG_CPU_VIRTADDR pvAddress,
										 IMG_DEVMEM_SIZE_T uiSize,
										 PVRSRV_CACHE_OP uiCacheOp)
{
	IMG_CPU_PHYADDR sCpuPhyAddrUnused =
		{ IMG_CAST_TO_CPUPHYADDR_UINT(0xCAFEF00DDEADBEEFULL) };
	IMG_BYTE *pbEnd = (IMG_BYTE*)((uintptr_t)pvAddress + (uintptr_t)uiSize);
	IMG_BYTE *pbStart = (IMG_BYTE*)((uintptr_t)pvAddress & ~((uintptr_t)gsCwq.uiLineSize-1));

	/*
	  If the start/end address isn't aligned to cache line size, round it up to the
	  nearest multiple; this ensures that we flush all the cache lines affected by
	  unaligned start/end addresses.
	 */
	pbEnd = (IMG_BYTE *) PVR_ALIGN((uintptr_t)pbEnd, (uintptr_t)gsCwq.uiLineSize);
	switch (uiCacheOp)
	{
		case PVRSRV_CACHE_OP_CLEAN:
			OSCPUCacheCleanRangeKM(psDevNode, pbStart, pbEnd, sCpuPhyAddrUnused, sCpuPhyAddrUnused);
			break;
		case PVRSRV_CACHE_OP_INVALIDATE:
			OSCPUCacheInvalidateRangeKM(psDevNode, pbStart, pbEnd, sCpuPhyAddrUnused, sCpuPhyAddrUnused);
			break;
		case PVRSRV_CACHE_OP_FLUSH:
			OSCPUCacheFlushRangeKM(psDevNode, pbStart, pbEnd, sCpuPhyAddrUnused, sCpuPhyAddrUnused);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,	"%s: Invalid cache operation type %d",
					 __func__, uiCacheOp));
			break;
	}

#if defined(CACHEOP_DEBUG)
	/* Tracks the number of kernel-mode cacheline maintenance instructions */
	gsCwq.ui32ServerRBF += (uiSize & ((IMG_DEVMEM_SIZE_T)~(gsCwq.uiLineSize - 1))) >> gsCwq.uiLineShift;
#endif
}

static INLINE PVRSRV_ERROR CacheOpValidateUMVA(PMR *psPMR,
											   IMG_CPU_VIRTADDR pvAddress,
											   IMG_DEVMEM_OFFSET_T uiOffset,
											   IMG_DEVMEM_SIZE_T uiSize,
											   PVRSRV_CACHE_OP uiCacheOp,
											   void **ppvOutAddress)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
#if defined(__linux__) && !defined(CACHEFLUSH_NO_KMRBF_USING_UMVA)
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
#endif
	void __user *pvAddr;

	IMG_BOOL bReadOnlyInvalidate =
		(uiCacheOp == PVRSRV_CACHE_OP_INVALIDATE) &&
		!PVRSRV_CHECK_CPU_WRITEABLE(PMR_Flags(psPMR));

	if (!pvAddress || bReadOnlyInvalidate)
	{
		/* As pvAddress is optional, NULL is expected from UM/KM requests */
		/* Also don't allow invalidates for UMVA of read-only memory */
		pvAddr = NULL;
		goto e0;
	}



#if !defined(__linux__) || defined(CACHEFLUSH_NO_KMRBF_USING_UMVA)
	pvAddr = NULL;
#else
	/* Validate VA, assume most basic address limit access_ok() check */
	pvAddr = (void __user *)(uintptr_t)((uintptr_t)pvAddress + uiOffset);
	if (!access_ok(pvAddr, uiSize))
	{
		pvAddr = NULL;
		if (! mm)
		{
			/* Bad KM request, don't silently ignore */
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_CPU_ADDR, e0);
		}
	}
	else if (mm)
	{
		mmap_read_lock(mm);
		vma = find_vma(mm, (unsigned long)(uintptr_t)pvAddr);

		if (!vma ||
			vma->vm_start > (unsigned long)(uintptr_t)pvAddr ||
			vma->vm_end < (unsigned long)(uintptr_t)pvAddr + uiSize ||
			vma->vm_private_data != psPMR)
		{
			/*
			 * Request range is not fully mapped or is not matching the PMR
			 * Ignore request's VA.
			 */
			pvAddr = NULL;
		}
		mmap_read_unlock(mm);
	}
#endif

e0:
	*ppvOutAddress = (IMG_CPU_VIRTADDR __force) pvAddr;
	return eError;
}

static PVRSRV_ERROR CacheOpPMRExec (PMR *psPMR,
									IMG_CPU_VIRTADDR pvAddress,
									IMG_DEVMEM_OFFSET_T uiOffset,
									IMG_DEVMEM_SIZE_T uiSize,
									PVRSRV_CACHE_OP uiCacheOp,
									IMG_BOOL bIsRequestValidated)

{
	IMG_HANDLE hPrivOut = NULL;
	IMG_BOOL bPMRIsSparse;
	IMG_UINT32 ui32PageIndex;
	IMG_UINT32 ui32NumOfPages;
	size_t uiOutSize;	/* Effectively unused */
	PVRSRV_DEVICE_NODE *psDevNode;
	IMG_DEVMEM_SIZE_T uiPgAlignedSize;
	IMG_DEVMEM_OFFSET_T uiPgAlignedOffset;
	IMG_DEVMEM_OFFSET_T uiCLAlignedEndOffset;
	IMG_DEVMEM_OFFSET_T uiPgAlignedEndOffset;
	IMG_DEVMEM_OFFSET_T uiCLAlignedStartOffset;
	IMG_DEVMEM_OFFSET_T uiPgAlignedStartOffset;
	IMG_BOOL abValid[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_CPU_PHYADDR asCpuPhyAddr[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_CPU_PHYADDR *psCpuPhyAddr = asCpuPhyAddr;
	IMG_BOOL bIsPMRInfoValid = IMG_FALSE;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BYTE *pbCpuVirtAddr = NULL;
	IMG_BOOL *pbValid = abValid;

	if (uiCacheOp == PVRSRV_CACHE_OP_NONE || uiCacheOp == PVRSRV_CACHE_OP_TIMELINE)
	{
		return PVRSRV_OK;
	}

	if (! bIsRequestValidated)
	{
		IMG_DEVMEM_SIZE_T uiLPhysicalSize;

		/* Need to validate parameters before proceeding */
		eError = PMR_PhysicalSize(psPMR, &uiLPhysicalSize);
		PVR_LOG_RETURN_IF_ERROR(eError, "uiLPhysicalSize");

		PVR_LOG_RETURN_IF_FALSE(((uiOffset+uiSize) <= uiLPhysicalSize), CACHEOP_DEVMEM_OOR_ERROR_STRING, PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE);

		eError = PMRLockSysPhysAddresses(psPMR);
		PVR_LOG_RETURN_IF_ERROR(eError, "PMRLockSysPhysAddresses");
	}

	/* Fast track the request if a CPU VA is provided and CPU ISA supports VA only maintenance */
	eError = CacheOpValidateUMVA(psPMR, pvAddress, uiOffset, uiSize, uiCacheOp, (void**)&pbCpuVirtAddr);
	if (eError == PVRSRV_OK)
	{
		pvAddress = pbCpuVirtAddr;

		if (pvAddress && gsCwq.uiCacheOpAddrType == OS_CACHE_OP_ADDR_TYPE_VIRTUAL)
		{
			CacheOpExecRangeBasedVA(PMR_DeviceNode(psPMR), pvAddress, uiSize, uiCacheOp);

			if (!bIsRequestValidated)
			{
				eError = PMRUnlockSysPhysAddresses(psPMR);
				PVR_LOG_IF_ERROR(eError, "PMRUnlockSysPhysAddresses");
			}
#if defined(CACHEOP_DEBUG)
			gsCwq.ui32ServerSyncVA += 1;
#endif
			return PVRSRV_OK;
		}
		else if (pvAddress)
		{
			/* Round down the incoming VA (if any) down to the nearest page aligned VA */
			pvAddress = (void*)((uintptr_t)pvAddress & ~((uintptr_t)gsCwq.uiPageSize-1));
#if defined(CACHEOP_DEBUG)
			gsCwq.ui32ServerSyncVA += 1;
#endif
		}
	}
	else
	{
		/*
		 * This validation pathway has been added to accommodate any/all requests that might
		 * cause the kernel to Oops; essentially, KM requests should prevalidate cache maint.
		 * parameters but if this fails then we would rather fail gracefully than cause the
		 * kernel to Oops so instead we log the fact that an invalid KM virtual address was
		 * supplied and what action was taken to mitigate against kernel Oops(ing) if any.
		 */
		CACHEOP_PVR_ASSERT(pbCpuVirtAddr == NULL);

		if (gsCwq.uiCacheOpAddrType == OS_CACHE_OP_ADDR_TYPE_PHYSICAL)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"%s: Invalid vaddress 0x%p in CPU d-cache maint. op, using paddress",
					__func__,
					pvAddress));

			/* We can still proceed as kernel/cpu uses CPU PA for d-cache maintenance */
			pvAddress = NULL;
		}
		else
		{
			/*
			 * The approach here is to attempt a reacquisition of the PMR kernel VA and see if
			 * said VA corresponds to the parameter VA, if so fail requested cache maint. op.
			 * cause this indicates some kind of internal, memory and/or meta-data corruption
			 * else we reissue the request using this (re)acquired alias PMR kernel VA.
			 */
			if (PMR_IsSparse(psPMR))
			{
				eError = PMRAcquireSparseKernelMappingData(psPMR,
														   0,
														   gsCwq.uiPageSize,
														   (void **)&pbCpuVirtAddr,
														   &uiOutSize,
														   &hPrivOut);
				PVR_LOG_GOTO_IF_ERROR(eError, "PMRAcquireSparseKernelMappingData", e0);
			}
			else
			{
				eError = PMRAcquireKernelMappingData(psPMR,
													 0,
													 gsCwq.uiPageSize,
													 (void **)&pbCpuVirtAddr,
													 &uiOutSize,
													 &hPrivOut);
				PVR_LOG_GOTO_IF_ERROR(eError, "PMRAcquireKernelMappingData", e0);
			}

			/* Here, we only compare these CPU virtual addresses at granularity of the OS page size */
			if ((uintptr_t)pbCpuVirtAddr == ((uintptr_t)pvAddress & ~((uintptr_t)gsCwq.uiPageSize-1)))
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: Invalid vaddress 0x%p in CPU d-cache maint. op, no alt. so failing request",
						__func__,
						pvAddress));

				eError = PMRReleaseKernelMappingData(psPMR, hPrivOut);
				PVR_LOG_GOTO_WITH_ERROR("PMRReleaseKernelMappingData", eError, PVRSRV_ERROR_INVALID_CPU_ADDR, e0);
			}
			else if (gsCwq.uiCacheOpAddrType == OS_CACHE_OP_ADDR_TYPE_VIRTUAL)
			{
				PVR_DPF((PVR_DBG_WARNING,
						"%s: Bad vaddress 0x%p in CPU d-cache maint. op, using reacquired vaddress 0x%p",
						__func__,
						pvAddress,
						pbCpuVirtAddr));

				/* Note that this might still fail if there is kernel memory/meta-data corruption;
				   there is not much we can do here but at the least we will be informed of this
				   before the kernel Oops(ing) */
				CacheOpExecRangeBasedVA(PMR_DeviceNode(psPMR), pbCpuVirtAddr, uiSize, uiCacheOp);

				eError = PMRReleaseKernelMappingData(psPMR, hPrivOut);
				PVR_LOG_IF_ERROR(eError, "PMRReleaseKernelMappingData");

				eError = PVRSRV_OK;
				goto e0;
			}
			else
			{
				/* At this junction, we have exhausted every possible work-around possible but we do
				   know that VA reacquisition returned another/alias page-aligned VA; so with this
				   future expectation of PMRAcquireKernelMappingData(), we proceed */
				PVR_DPF((PVR_DBG_WARNING,
						"%s: Bad vaddress %p in CPU d-cache maint. op, will use reacquired vaddress",
						__func__,
						pvAddress));

				eError = PMRReleaseKernelMappingData(psPMR, hPrivOut);
				PVR_LOG_IF_ERROR(eError, "PMRReleaseKernelMappingData");

				/* NULL this to force per-page reacquisition down-stream */
				pvAddress = NULL;
			}
		}
	}

	/* NULL clobbered var., OK to proceed */
	pbCpuVirtAddr = NULL;
	eError = PVRSRV_OK;

	/* Need this for kernel mapping */
	bPMRIsSparse = PMR_IsSparse(psPMR);
	psDevNode = PMR_DeviceNode(psPMR);

	/* Round the incoming offset down to the nearest cache-line / page aligned-address */
	uiCLAlignedEndOffset = uiOffset + uiSize;
	uiCLAlignedEndOffset = PVR_ALIGN(uiCLAlignedEndOffset, (IMG_DEVMEM_SIZE_T)gsCwq.uiLineSize);
	uiCLAlignedStartOffset = (uiOffset & ~((IMG_DEVMEM_OFFSET_T)gsCwq.uiLineSize-1));

	uiPgAlignedEndOffset = uiCLAlignedEndOffset;
	uiPgAlignedEndOffset = PVR_ALIGN(uiPgAlignedEndOffset, (IMG_DEVMEM_SIZE_T)gsCwq.uiPageSize);
	uiPgAlignedStartOffset = (uiOffset & ~((IMG_DEVMEM_OFFSET_T)gsCwq.uiPageSize-1));
	uiPgAlignedSize = uiPgAlignedEndOffset - uiPgAlignedStartOffset;

#if defined(CACHEOP_NO_CACHE_LINE_ALIGNED_ROUNDING)
	/* For internal debug if cache-line optimised
	   flushing is suspected of causing data corruption */
	uiCLAlignedStartOffset = uiPgAlignedStartOffset;
	uiCLAlignedEndOffset = uiPgAlignedEndOffset;
#endif

	/* Type of allocation backing the PMR data */
	ui32NumOfPages = uiPgAlignedSize >> gsCwq.uiPageShift;
	if (ui32NumOfPages > PMR_MAX_TRANSLATION_STACK_ALLOC)
	{
		/* The pbValid array is allocated first as it is needed in
		   both physical/virtual cache maintenance methods */
		pbValid = OSAllocZMem(ui32NumOfPages * sizeof(IMG_BOOL));
		if (! pbValid)
		{
			pbValid = abValid;
		}
		else if (gsCwq.uiCacheOpAddrType != OS_CACHE_OP_ADDR_TYPE_VIRTUAL)
		{
			psCpuPhyAddr = OSAllocZMem(ui32NumOfPages * sizeof(IMG_CPU_PHYADDR));
			if (! psCpuPhyAddr)
			{
				psCpuPhyAddr = asCpuPhyAddr;
				OSFreeMem(pbValid);
				pbValid = abValid;
			}
		}
	}

	/* We always retrieve PMR data in bulk, up-front if number of pages is within
	   PMR_MAX_TRANSLATION_STACK_ALLOC limits else we check to ensure that a
	   dynamic buffer has been allocated to satisfy requests outside limits */
	if (ui32NumOfPages <= PMR_MAX_TRANSLATION_STACK_ALLOC || pbValid != abValid)
	{
		if (gsCwq.uiCacheOpAddrType != OS_CACHE_OP_ADDR_TYPE_VIRTUAL)
		{
			/* Look-up PMR CpuPhyAddr once, if possible */
			eError = PMR_CpuPhysAddr(psPMR,
									 gsCwq.uiPageShift,
									 ui32NumOfPages,
									 uiPgAlignedStartOffset,
									 psCpuPhyAddr,
									 pbValid);
			if (eError == PVRSRV_OK)
			{
				bIsPMRInfoValid = IMG_TRUE;
			}
		}
		else
		{
			/* Look-up PMR per-page validity once, if possible */
			eError = PMR_IsOffsetValid(psPMR,
									   gsCwq.uiPageShift,
									   ui32NumOfPages,
									   uiPgAlignedStartOffset,
									   pbValid);
			bIsPMRInfoValid = (eError == PVRSRV_OK) ? IMG_TRUE : IMG_FALSE;
		}
	}

	/* For each (possibly non-contiguous) PMR page(s), carry out the requested cache maint. op. */
	for (uiPgAlignedOffset = uiPgAlignedStartOffset, ui32PageIndex = 0;
		 uiPgAlignedOffset < uiPgAlignedEndOffset;
		 uiPgAlignedOffset += (IMG_DEVMEM_OFFSET_T) gsCwq.uiPageSize, ui32PageIndex += 1)
	{

		if (! bIsPMRInfoValid)
		{
			/* Never cross page boundary without looking up corresponding PMR page physical
			   address and/or page validity if these were not looked-up, in bulk, up-front */
			ui32PageIndex = 0;
			if (gsCwq.uiCacheOpAddrType != OS_CACHE_OP_ADDR_TYPE_VIRTUAL)
			{
				eError = PMR_CpuPhysAddr(psPMR,
										 gsCwq.uiPageShift,
										 1,
										 uiPgAlignedOffset,
										 psCpuPhyAddr,
										 pbValid);
				PVR_LOG_GOTO_IF_ERROR(eError, "PMR_CpuPhysAddr", e0);
			}
			else
			{
				eError = PMR_IsOffsetValid(psPMR,
										  gsCwq.uiPageShift,
										  1,
										  uiPgAlignedOffset,
										  pbValid);
				PVR_LOG_GOTO_IF_ERROR(eError, "PMR_IsOffsetValid", e0);
			}
		}

		/* Skip invalid PMR pages (i.e. sparse) */
		if (pbValid[ui32PageIndex] == IMG_FALSE)
		{
			CACHEOP_PVR_ASSERT(bPMRIsSparse);
			continue;
		}

		if (pvAddress)
		{
			/* The caller has supplied either a KM/UM CpuVA, so use it unconditionally */
			pbCpuVirtAddr =
				(void *)(uintptr_t)((uintptr_t)pvAddress + (uintptr_t)(uiPgAlignedOffset-uiPgAlignedStartOffset));
		}
		/* Skip CpuVA acquire if CacheOp can be maintained entirely using CpuPA */
		else if (gsCwq.uiCacheOpAddrType != OS_CACHE_OP_ADDR_TYPE_PHYSICAL)
		{
			if (bPMRIsSparse)
			{
				eError =
					PMRAcquireSparseKernelMappingData(psPMR,
													  uiPgAlignedOffset,
													  gsCwq.uiPageSize,
													  (void **)&pbCpuVirtAddr,
													  &uiOutSize,
													  &hPrivOut);
				PVR_LOG_GOTO_IF_ERROR(eError, "PMRAcquireSparseKernelMappingData", e0);
			}
			else
			{
				eError =
					PMRAcquireKernelMappingData(psPMR,
												uiPgAlignedOffset,
												gsCwq.uiPageSize,
												(void **)&pbCpuVirtAddr,
												&uiOutSize,
												&hPrivOut);
				PVR_LOG_GOTO_IF_ERROR(eError, "PMRAcquireKernelMappingData", e0);
			}
		}

		/* Issue actual cache maintenance for PMR */
		CacheOpExecRangeBased(psDevNode,
							uiCacheOp,
							pbCpuVirtAddr,
							(gsCwq.uiCacheOpAddrType != OS_CACHE_OP_ADDR_TYPE_VIRTUAL) ?
								psCpuPhyAddr[ui32PageIndex] : psCpuPhyAddr[0],
							uiPgAlignedOffset,
							uiCLAlignedStartOffset,
							uiCLAlignedEndOffset);

		if (! pvAddress)
		{
			/* The caller has not supplied either a KM/UM CpuVA, release mapping */
			if (gsCwq.uiCacheOpAddrType != OS_CACHE_OP_ADDR_TYPE_PHYSICAL)
			{
				eError = PMRReleaseKernelMappingData(psPMR, hPrivOut);
				PVR_LOG_IF_ERROR(eError, "PMRReleaseKernelMappingData");
			}
		}
	}

    if(uiCacheOp == PVRSRV_CACHE_OP_INVALIDATE && uiSize >= 4096)
    {
        do_invalid_range(0x00000000, 0x200000);
    }

e0:
	if (psCpuPhyAddr != asCpuPhyAddr)
	{
		OSFreeMem(psCpuPhyAddr);
	}

	if (pbValid != abValid)
	{
		OSFreeMem(pbValid);
	}

	if (! bIsRequestValidated)
	{
		eError = PMRUnlockSysPhysAddresses(psPMR);
		PVR_LOG_IF_ERROR(eError, "PMRUnlockSysPhysAddresses");
	}

	return eError;
}

static PVRSRV_ERROR CacheOpQListExecRangeBased(void)
{
	IMG_UINT32 ui32NumOfEntries;
	PVRSRV_ERROR eError = PVRSRV_OK;
	CACHEOP_WORK_ITEM *psCacheOpWorkItem = NULL;

	/* Take a snapshot of the current count of deferred entries at this junction */
	ui32NumOfEntries = CacheOpIdxSpan(&gsCwq.hWriteCounter, &gsCwq.hReadCounter);
	if (! ui32NumOfEntries)
	{
		return PVRSRV_OK;
	}
#if defined(CACHEOP_DEBUG)
	CACHEOP_PVR_ASSERT(ui32NumOfEntries < CACHEOP_INDICES_MAX);
#endif

	while (ui32NumOfEntries)
	{
		if (! OSAtomicRead(&gsCwq.hReadCounter))
		{
			/* Normally, the read-counter will trail the write counter until the write
			   counter wraps-round to zero. Under this condition we (re)calculate as the
			   read-counter too is wrapping around at this point */
			ui32NumOfEntries = CacheOpIdxSpan(&gsCwq.hWriteCounter, &gsCwq.hReadCounter);
		}
#if defined(CACHEOP_DEBUG)
		/* Something's gone horribly wrong if these 2 counters are identical at this point */
		CACHEOP_PVR_ASSERT(OSAtomicRead(&gsCwq.hWriteCounter) != OSAtomicRead(&gsCwq.hReadCounter));
#endif

		/* Select the next pending deferred work-item for RBF cache maintenance */
		psCacheOpWorkItem = &gsCwq.asWorkItems[CacheOpIdxNext(&gsCwq.hReadCounter)];

#if defined(CACHEOP_DEBUG)
		/* The time waiting in the queue to be serviced */
		psCacheOpWorkItem->ui64DequeuedTime = OSClockns64();
#endif

		eError = CacheOpPMRExec(psCacheOpWorkItem->psPMR,
								NULL, /* No UM virtual address */
								psCacheOpWorkItem->uiOffset,
								psCacheOpWorkItem->uiSize,
								psCacheOpWorkItem->uiCacheOp,
								IMG_TRUE /* PMR is pre-validated */
								);
		if (eError != PVRSRV_OK)
		{
#if defined(CACHEOP_DEBUG)
#define PID_FMTSPEC " PID:%u"
#define CACHE_OP_WORK_PID psCacheOpWorkItem->pid
#else
#define PID_FMTSPEC "%s"
#define CACHE_OP_WORK_PID ""
#endif

			PVR_LOG(("Deferred CacheOpPMRExec failed:"
					 PID_FMTSPEC
					 " PMR:%p"
					 " Offset:%" IMG_UINT64_FMTSPECX
					 " Size:%" IMG_UINT64_FMTSPECX
					 " CacheOp:%d,"
					 " error: %d",
					CACHE_OP_WORK_PID,
					psCacheOpWorkItem->psPMR,
					psCacheOpWorkItem->uiOffset,
					psCacheOpWorkItem->uiSize,
					psCacheOpWorkItem->uiCacheOp,
					eError));

#undef PID_FMTSPEC
#undef CACHE_OP_WORK_PID
		}

#if defined(CACHEOP_DEBUG)
		psCacheOpWorkItem->ui64ExecuteTime = OSClockns64();
		CacheOpStatsExecLogWrite(psCacheOpWorkItem);
#endif

		/* The currently executed CacheOp item updates gsCwq.hCompletedSeqNum.
		   NOTE: This CacheOp item might be a discard item, if so its seqNum
		   still updates the gsCwq.hCompletedSeqNum */
		OSAtomicWrite(&gsCwq.hCompletedSeqNum, psCacheOpWorkItem->ui32OpSeqNum);

		/* If CacheOp is timeline(d), notify timeline waiters */
		eError = CacheOpTimelineExec(psCacheOpWorkItem);
		PVR_LOG_IF_ERROR(eError, "CacheOpTimelineExec");

		eError = PMRUnlockSysPhysAddresses(psCacheOpWorkItem->psPMR);
		PVR_LOG_IF_ERROR(eError, "PMRUnlockSysPhysAddresses");

		(void) CacheOpIdxIncrement(&gsCwq.hReadCounter);
		ui32NumOfEntries = ui32NumOfEntries - 1;
	}

	return eError;
}

static INLINE PVRSRV_ERROR CacheOpQListExec(void)
{
	PVRSRV_ERROR eError;

	eError = CacheOpQListExecRangeBased();
	PVR_LOG_IF_ERROR(eError, "CacheOpQListExecRangeBased");

	/* Signal any waiting threads blocked on CacheOp fence checks update
	   completed sequence number to last queue work item */
	eError = OSEventObjectSignal(gsCwq.hClientWakeUpEvtObj);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

	return eError;
}

static void CacheOpThread(void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR eError;

	/* Open CacheOp thread event object, abort driver if event object open fails */
	eError = OSEventObjectOpen(gsCwq.hThreadWakeUpEvtObj, &hOSEvent);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectOpen");

	/* While driver is in good state & loaded, perform pending cache maintenance */
	while ((psPVRSRVData->eServicesState == PVRSRV_SERVICES_STATE_OK) && gsCwq.bInit)
	{
		/* Sleep-wait here until when signalled for new queued CacheOp work items;
		   when woken-up, drain deferred queue completely before next event-wait */
		(void) OSEventObjectWaitKernel(hOSEvent, CACHEOP_THREAD_WAIT_TIMEOUT);
		while (CacheOpIdxSpan(&gsCwq.hWriteCounter, &gsCwq.hReadCounter))
		{
			eError = CacheOpQListExec();
			PVR_LOG_IF_ERROR(eError, "CacheOpQListExec");
		}
	}

	eError = CacheOpQListExec();
	PVR_LOG_IF_ERROR(eError, "CacheOpQListExec");

	eError = OSEventObjectClose(hOSEvent);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");
}

static PVRSRV_ERROR CacheOpBatchExecTimeline(PVRSRV_DEVICE_NODE *psDevNode,
											 PVRSRV_TIMELINE iTimeline,
											 IMG_UINT32 ui32CurrentFenceSeqNum,
											 IMG_UINT32 *pui32NextFenceSeqNum)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32NextIdx;
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {NULL};
	CACHEOP_WORK_ITEM *psCacheOpWorkItem = NULL;

	eError = CacheOpTimelineBind(psDevNode, &sCacheOpWorkItem, iTimeline);
	PVR_LOG_RETURN_IF_ERROR(eError, "CacheOpTimelineBind");

	OSLockAcquire(gsCwq.hDeferredLock);

	/*
	   Check if there is any deferred queueing space available and that nothing is
	   currently queued. This second check is required as Android where timelines
	   are used sets a timeline signalling deadline of 1000ms to signal timelines
	   else complains. So seeing we cannot be sure how long the CacheOp presently
	   in the queue would take we should not send this timeline down the queue as
	   well.
	 */
	ui32NextIdx = CacheOpIdxNext(&gsCwq.hWriteCounter);
	if (!CacheOpIdxSpan(&gsCwq.hWriteCounter, &gsCwq.hReadCounter) &&
		CacheOpIdxRead(&gsCwq.hReadCounter) != ui32NextIdx)
	{
		psCacheOpWorkItem = &gsCwq.asWorkItems[ui32NextIdx];

		psCacheOpWorkItem->sSWTimelineObj = sCacheOpWorkItem.sSWTimelineObj;
		psCacheOpWorkItem->iTimeline = sCacheOpWorkItem.iTimeline;
		psCacheOpWorkItem->psDevNode = sCacheOpWorkItem.psDevNode;
		psCacheOpWorkItem->ui32OpSeqNum = CacheOpGetNextCommonSeqNum();
		psCacheOpWorkItem->uiCacheOp = PVRSRV_CACHE_OP_TIMELINE;
		psCacheOpWorkItem->uiOffset = (IMG_DEVMEM_OFFSET_T)0;
		psCacheOpWorkItem->uiSize = (IMG_DEVMEM_SIZE_T)0;
		/* Defer timeline using information page PMR */
		psCacheOpWorkItem->psPMR = gsCwq.psInfoPagePMR;

		eError = PMRLockSysPhysAddresses(psCacheOpWorkItem->psPMR);
		PVR_LOG_GOTO_IF_ERROR(eError, "PMRLockSysPhysAddresses", e0);

#if defined(CACHEOP_DEBUG)
		psCacheOpWorkItem->pid = OSGetCurrentClientProcessIDKM();
		psCacheOpWorkItem->ui64EnqueuedTime = OSClockns64();
		gsCwq.ui32ServerASync += 1;
		gsCwq.ui32ServerDTL += 1;
#endif

		/* Mark index ready for cache maintenance */
		(void) CacheOpIdxIncrement(&gsCwq.hWriteCounter);

		eError = OSEventObjectSignal(gsCwq.hThreadWakeUpEvtObj);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
	}
	else
	{
		/* signal timeline.
		 * All ops with timelines and partial batches were executed synchronously. */
		eError = CacheOpTimelineExec(&sCacheOpWorkItem);
		PVR_LOG_IF_ERROR(eError, "CacheOpTimelineExec");
	}

e0:
	OSLockRelease(gsCwq.hDeferredLock);
	return eError;
}

static PVRSRV_ERROR CacheOpBatchExecRangeBased(PVRSRV_DEVICE_NODE *psDevNode,
											PMR **ppsPMR,
											IMG_CPU_VIRTADDR *pvAddress,
											IMG_DEVMEM_OFFSET_T *puiOffset,
											IMG_DEVMEM_SIZE_T *puiSize,
											PVRSRV_CACHE_OP *puiCacheOp,
											IMG_UINT32 ui32NumCacheOps,
											PVRSRV_TIMELINE uiTimeline,
											IMG_UINT32 uiCurrentFenceSeqNum,
											IMG_UINT32 *pui32NextFenceSeqNum)
{
	IMG_UINT32 ui32Idx;
	IMG_UINT32 ui32NextIdx;
	IMG_BOOL bBatchHasTimeline;
	IMG_BOOL bCacheOpConfigKDF;
	IMG_DEVMEM_SIZE_T uiLogicalSize;
	IMG_BOOL bBatchForceSynchronous = IMG_FALSE;
	PVRSRV_ERROR eError = PVRSRV_OK;
	CACHEOP_WORK_ITEM *psCacheOpWorkItem = NULL;
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};
	IMG_UINT32 ui32OpSeqNum = CacheOpGetNextCommonSeqNum();
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
#endif

	/* Check if batch has an associated timeline update */
	bBatchHasTimeline = puiCacheOp[ui32NumCacheOps-1] & PVRSRV_CACHE_OP_TIMELINE;
	puiCacheOp[ui32NumCacheOps-1] &= ~(PVRSRV_CACHE_OP_TIMELINE);

	/* Check if batch is forcing synchronous execution */
	bBatchForceSynchronous = puiCacheOp[ui32NumCacheOps-1] & PVRSRV_CACHE_OP_FORCE_SYNCHRONOUS;
	puiCacheOp[ui32NumCacheOps-1] &= ~(PVRSRV_CACHE_OP_FORCE_SYNCHRONOUS);

	/* Check if config. supports kernel deferring of cacheops */
	bCacheOpConfigKDF = CacheOpConfigSupports(CACHEOP_CONFIG_KDF);

	/*
	   Client expects the next fence seqNum to be zero unless the server has deferred
	   at least one CacheOp in the submitted queue in which case the server informs
	   the client of the last CacheOp seqNum deferred in this batch.
	*/
	for (*pui32NextFenceSeqNum = 0, ui32Idx = 0; ui32Idx < ui32NumCacheOps; ui32Idx++)
	{
		/* Fail UM request, don't silently ignore */
		PVR_GOTO_IF_INVALID_PARAM(puiSize[ui32Idx], eError, e0);

		if (bCacheOpConfigKDF)
		{
			/* Check if there is deferred queueing space available */
			ui32NextIdx = CacheOpIdxNext(&gsCwq.hWriteCounter);
			if (ui32NextIdx != CacheOpIdxRead(&gsCwq.hReadCounter))
			{
				psCacheOpWorkItem = &gsCwq.asWorkItems[ui32NextIdx];
			}
		}

		/*
		   Normally, we would like to defer client CacheOp(s) but we may not always be in a
		   position or is necessary to do so based on the following reasons:
		   0 - There is currently no queueing space left to enqueue this CacheOp, this might
		       imply the system is queueing more requests than can be consumed by the CacheOp
		       thread in time.
		   1 - Batch has timeline, action this now due to Android timeline signaling deadlines.
		   2 - Batch is forced synchronous. Necessary on Android for batches scheduled in the
		       middle of add operation. Those cannot have timelines that client plans to add
			   during actual batch execution and thus make synchronization on Android tricky.
		   3 - Configuration does not support deferring of cache maintenance operations so we
		       execute the batch synchronously/immediately.
		   4 - CacheOp has an INVALIDATE, as this is used to transfer device memory buffer
		       ownership back to the processor, we cannot defer it so action it immediately.
		   5 - CacheOp size too small (single OS page size) to warrant overhead of deferment,
		   6 - CacheOp size OK for deferment, but a client virtual address is supplied so we
		       might has well just take advantage of said VA & flush immediately in UM context.
		   7 - Prevent DoS attack if a malicious client queues something very large, say 1GiB.
		       Here we upper bound this threshold to PVR_DIRTY_BYTES_FLUSH_THRESHOLD.
		*/
		if (!psCacheOpWorkItem  ||
			bBatchHasTimeline   ||
			bBatchForceSynchronous ||
			!bCacheOpConfigKDF  ||
			puiCacheOp[ui32Idx] & PVRSRV_CACHE_OP_INVALIDATE ||
			(puiSize[ui32Idx] <= (IMG_DEVMEM_SIZE_T)gsCwq.uiPageSize) ||
			(pvAddress[ui32Idx] && puiSize[ui32Idx] < (IMG_DEVMEM_SIZE_T)gsCwq.pui32InfoPage[CACHEOP_INFO_KMDFTHRESHLD]) ||
			(puiSize[ui32Idx] >= (IMG_DEVMEM_SIZE_T)(gsCwq.pui32InfoPage[CACHEOP_INFO_KMDFTHRESHLD] << 2)))
		{
			/* When the CacheOp thread not keeping up, trash d-cache */
#if defined(CACHEOP_DEBUG)
			sCacheOpWorkItem.ui64EnqueuedTime = OSClockns64();
			gsCwq.ui32ServerSync += 1;
#endif
			psCacheOpWorkItem = NULL;

			eError = CacheOpPMRExec(ppsPMR[ui32Idx],
									pvAddress[ui32Idx],
									puiOffset[ui32Idx],
									puiSize[ui32Idx],
									puiCacheOp[ui32Idx],
									IMG_FALSE);
			PVR_LOG_GOTO_IF_ERROR(eError, "CacheOpExecPMR", e0);

#if defined(CACHEOP_DEBUG)
			sCacheOpWorkItem.ui64ExecuteTime = OSClockns64();
			sCacheOpWorkItem.ui32OpSeqNum = ui32OpSeqNum;
			sCacheOpWorkItem.psPMR = ppsPMR[ui32Idx];
			sCacheOpWorkItem.uiSize = puiSize[ui32Idx];
			sCacheOpWorkItem.uiOffset = puiOffset[ui32Idx];
			sCacheOpWorkItem.uiCacheOp = puiCacheOp[ui32Idx];
			CacheOpStatsExecLogWrite(&sCacheOpWorkItem);
#endif

			continue;
		}

		/* Need to validate request parameters here before enqueing */
		eError = PMR_LogicalSize(ppsPMR[ui32Idx], &uiLogicalSize);
		PVR_LOG_GOTO_IF_ERROR(eError, "PMR_LogicalSize", e0);
		eError = PVRSRV_ERROR_DEVICEMEM_OUT_OF_RANGE;
		PVR_LOG_GOTO_IF_FALSE(((puiOffset[ui32Idx]+puiSize[ui32Idx]) <= uiLogicalSize), CACHEOP_DEVMEM_OOR_ERROR_STRING, e0);
		eError = PVRSRV_OK;

		/* For safety, take reference here in user context */
		eError = PMRLockSysPhysAddresses(ppsPMR[ui32Idx]);
		PVR_LOG_GOTO_IF_ERROR(eError, "PMRLockSysPhysAddresses", e0);

		OSLockAcquire(gsCwq.hDeferredLock);
		/* Select next item off the queue to defer with */
		ui32NextIdx = CacheOpIdxNext(&gsCwq.hWriteCounter);
		if (ui32NextIdx != CacheOpIdxRead(&gsCwq.hReadCounter))
		{
			psCacheOpWorkItem = &gsCwq.asWorkItems[ui32NextIdx];
		}
		else
		{
			/* Retry, disable KDF for this batch */
			OSLockRelease(gsCwq.hDeferredLock);
			bCacheOpConfigKDF = IMG_FALSE;
			psCacheOpWorkItem = NULL;
			eError = PMRUnlockSysPhysAddresses(ppsPMR[ui32Idx]);
			PVR_LOG_GOTO_IF_ERROR(eError, "PMRUnlockSysPhysAddresses", e0);
			ui32Idx = ui32Idx - 1;
			continue;
		}

		psCacheOpWorkItem->psPMR = ppsPMR[ui32Idx];
		psCacheOpWorkItem->uiCacheOp = puiCacheOp[ui32Idx];
		psCacheOpWorkItem->uiOffset = puiOffset[ui32Idx];
		psCacheOpWorkItem->uiSize = puiSize[ui32Idx];
		
		/* Timeline need to be looked-up (i.e. bind) in the user context
		   before deferring into the CacheOp thread kernel context */
		eError = CacheOpTimelineBind(psDevNode, psCacheOpWorkItem, PVRSRV_NO_TIMELINE);
		PVR_LOG_GOTO_IF_ERROR(eError, "CacheOpTimelineBind", e1);

		/* Prepare & enqueue next deferred work item for CacheOp thread */
		psCacheOpWorkItem->ui32OpSeqNum = CacheOpGetNextCommonSeqNum();
		*pui32NextFenceSeqNum = psCacheOpWorkItem->ui32OpSeqNum;

#if defined(CACHEOP_DEBUG)
		psCacheOpWorkItem->ui64EnqueuedTime = OSClockns64();
		psCacheOpWorkItem->pid = sCacheOpWorkItem.pid;
		psCacheOpWorkItem->bDeferred = IMG_TRUE;
		psCacheOpWorkItem->bKMReq = IMG_FALSE;
		psCacheOpWorkItem->bUMF = IMG_FALSE;
		gsCwq.ui32ServerASync += 1;
#endif

		/* Increment deferred size & mark index ready for cache maintenance */
		(void) CacheOpIdxIncrement(&gsCwq.hWriteCounter);

		OSLockRelease(gsCwq.hDeferredLock);
		psCacheOpWorkItem = NULL;
	}

	/* Signal the CacheOp thread to ensure these items get processed */
	eError = OSEventObjectSignal(gsCwq.hThreadWakeUpEvtObj);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

e1:
	if (psCacheOpWorkItem)
	{
		eError = PMRUnlockSysPhysAddresses(psCacheOpWorkItem->psPMR);
		PVR_LOG_IF_ERROR(eError, "PMRUnlockSysPhysAddresses");
	}
	OSLockRelease(gsCwq.hDeferredLock);

e0:
	if (bBatchHasTimeline)
	{
		PVRSRV_ERROR eError2;
		eError2 = CacheOpBatchExecTimeline(psDevNode, uiTimeline,
										   uiCurrentFenceSeqNum, pui32NextFenceSeqNum);
		eError = (eError2 == PVRSRV_ERROR_RETRY) ? eError2 : eError;
	}

	return eError;
}


PVRSRV_ERROR CacheOpExec (PPVRSRV_DEVICE_NODE psDevNode,
						  void *pvVirtStart,
						  void *pvVirtEnd,
						  IMG_CPU_PHYADDR sCPUPhysStart,
						  IMG_CPU_PHYADDR sCPUPhysEnd,
						  PVRSRV_CACHE_OP uiCacheOp)
{
#if defined(CACHEOP_DEBUG)
	IMG_UINT64 ui64EnqueueTime = OSClockns64();
#endif

	switch (uiCacheOp)
	{
		case PVRSRV_CACHE_OP_CLEAN:
			OSCPUCacheCleanRangeKM(psDevNode, pvVirtStart, pvVirtEnd, sCPUPhysStart, sCPUPhysEnd);
			break;
		case PVRSRV_CACHE_OP_INVALIDATE:
			OSCPUCacheInvalidateRangeKM(psDevNode, pvVirtStart, pvVirtEnd, sCPUPhysStart, sCPUPhysEnd);
			break;
		case PVRSRV_CACHE_OP_FLUSH:
			OSCPUCacheFlushRangeKM(psDevNode, pvVirtStart, pvVirtEnd, sCPUPhysStart, sCPUPhysEnd);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,	"%s: Invalid cache operation type %d",
					 __func__, uiCacheOp));
			break;
	}

#if defined(CACHEOP_DEBUG)
	if (CacheOpConfigSupports(CACHEOP_CONFIG_KLOG))
	{
		CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};

		gsCwq.ui32ServerSync += 1;
		gsCwq.ui32ServerRBF +=
				((sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr) & ((IMG_DEVMEM_SIZE_T)~(gsCwq.uiLineSize - 1))) >> gsCwq.uiLineShift;

		sCacheOpWorkItem.uiOffset = 0;
		sCacheOpWorkItem.bKMReq = IMG_TRUE;
		sCacheOpWorkItem.uiCacheOp = uiCacheOp;
		/* Use information page PMR for logging KM request */
		sCacheOpWorkItem.psPMR = gsCwq.psInfoPagePMR;
		sCacheOpWorkItem.ui64EnqueuedTime = ui64EnqueueTime;
		sCacheOpWorkItem.ui64ExecuteTime = OSClockns64();
		sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
		sCacheOpWorkItem.ui32OpSeqNum = CacheOpGetNextCommonSeqNum();
		sCacheOpWorkItem.uiSize = (sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr);

		CacheOpStatsExecLogWrite(&sCacheOpWorkItem);
	}
#endif

	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpValExec(PMR *psPMR,
						    IMG_UINT64 uiAddress,
						    IMG_DEVMEM_OFFSET_T uiOffset,
						    IMG_DEVMEM_SIZE_T uiSize,
						    PVRSRV_CACHE_OP uiCacheOp)
{
	PVRSRV_ERROR eError;
	IMG_CPU_VIRTADDR pvAddress = (IMG_CPU_VIRTADDR)(uintptr_t)uiAddress;
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};
	gsCwq.ui32ServerSync += 1;
	sCacheOpWorkItem.psPMR = psPMR;
	sCacheOpWorkItem.uiSize = uiSize;
	sCacheOpWorkItem.uiOffset = uiOffset;
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
	sCacheOpWorkItem.ui32OpSeqNum = CacheOpGetNextCommonSeqNum();
	sCacheOpWorkItem.ui64EnqueuedTime = OSClockns64();
#endif

	eError = CacheOpPMRExec(psPMR,
							pvAddress,
							uiOffset,
							uiSize,
							uiCacheOp,
							IMG_FALSE);
	PVR_LOG_GOTO_IF_ERROR(eError, "CacheOpPMRExec", e0);

#if defined(CACHEOP_DEBUG)
	sCacheOpWorkItem.ui64ExecuteTime = OSClockns64();
	CacheOpStatsExecLogWrite(&sCacheOpWorkItem);
#endif

e0:
	return eError;
}

PVRSRV_ERROR CacheOpQueue (CONNECTION_DATA *psConnection,
						   PVRSRV_DEVICE_NODE *psDevNode,
						   IMG_UINT32 ui32NumCacheOps,
						   PMR **ppsPMR,
						   IMG_UINT64 *puiAddress,
						   IMG_DEVMEM_OFFSET_T *puiOffset,
						   IMG_DEVMEM_SIZE_T *puiSize,
						   PVRSRV_CACHE_OP *puiCacheOp,
						   IMG_UINT32 ui32OpTimeline,
						   IMG_UINT32 uiCurrentFenceSeqNum,
						   IMG_UINT32 *pui32NextFenceSeqNum)
{
	PVRSRV_ERROR eError;
	PVRSRV_TIMELINE uiTimeline = (PVRSRV_TIMELINE)ui32OpTimeline;
	IMG_CPU_VIRTADDR *pvAddress = (IMG_CPU_VIRTADDR*)(uintptr_t)puiAddress;

	PVR_UNREFERENCED_PARAMETER(psConnection);

	if (!gsCwq.bInit)
	{
		PVR_LOG(("CacheOp framework not initialised, failing request"));
		return PVRSRV_ERROR_NOT_INITIALISED;
	}
	else if (! ui32NumCacheOps)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	/* Ensure any single timeline CacheOp request is processed immediately */
	else if (ui32NumCacheOps == 1 && puiCacheOp[0] == PVRSRV_CACHE_OP_TIMELINE)
	{
		eError = CacheOpBatchExecTimeline(psDevNode, uiTimeline, uiCurrentFenceSeqNum, pui32NextFenceSeqNum);
	}
	/* This is the default entry for all client requests */
	else
	{
		if (!(gsCwq.eConfig & (CACHEOP_CONFIG_LAST-1)))
		{
			/* default the configuration before execution */
			CacheOpConfigUpdate(CACHEOP_CONFIG_DEFAULT);
		}

		eError =
			CacheOpBatchExecRangeBased(psDevNode,
									   ppsPMR,
									   pvAddress,
									   puiOffset,
									   puiSize,
									   puiCacheOp,
									   ui32NumCacheOps,
									   uiTimeline,
									   uiCurrentFenceSeqNum,
									   pui32NextFenceSeqNum);
	}

	return eError;
}

PVRSRV_ERROR CacheOpFence (RGXFWIF_DM eFenceOpType, IMG_UINT32 ui32FenceOpSeqNum)
{
	IMG_HANDLE hOSEvent;
	PVRSRV_ERROR eError2;
	IMG_UINT32 ui32RetryAbort;
	IMG_UINT32 ui32CompletedOpSeqNum;
	PVRSRV_ERROR eError = PVRSRV_OK;
#if defined(CACHEOP_DEBUG)
	IMG_UINT64 uiTimeNow;
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
	sCacheOpWorkItem.ui32OpSeqNum = ui32FenceOpSeqNum;
	sCacheOpWorkItem.ui64EnqueuedTime = OSClockns64();
	uiTimeNow = sCacheOpWorkItem.ui64EnqueuedTime;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
	sCacheOpWorkItem.eFenceOpType = eFenceOpType;
#endif
	sCacheOpWorkItem.uiSize = (uintptr_t) OSAtomicRead(&gsCwq.hCompletedSeqNum);
	sCacheOpWorkItem.uiOffset = 0;
#endif
	PVR_UNREFERENCED_PARAMETER(eFenceOpType);

	/* If initial fence check fails, then wait-and-retry in loop */
	ui32CompletedOpSeqNum = OSAtomicRead(&gsCwq.hCompletedSeqNum);
	if (CacheOpFenceCheck(ui32CompletedOpSeqNum, ui32FenceOpSeqNum))
	{
#if defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.uiSize = (uintptr_t) ui32CompletedOpSeqNum;
#endif
		goto e0;
	}

	/* Open CacheOp update event object, if event open fails return error */
	eError2 = OSEventObjectOpen(gsCwq.hClientWakeUpEvtObj, &hOSEvent);
	PVR_LOG_GOTO_IF_ERROR(eError2, "OSEventObjectOpen", e0);

	/* Linear (i.e. use exponential?) back-off, upper bounds user wait */
	for (ui32RetryAbort = gsCwq.ui32FenceRetryAbort; ;--ui32RetryAbort)
	{
		/* (Re)read completed CacheOp sequence number before waiting */
		ui32CompletedOpSeqNum = OSAtomicRead(&gsCwq.hCompletedSeqNum);
		if (CacheOpFenceCheck(ui32CompletedOpSeqNum, ui32FenceOpSeqNum))
		{
#if defined(CACHEOP_DEBUG)
			sCacheOpWorkItem.uiSize = (uintptr_t) ui32CompletedOpSeqNum;
#endif
			break;
		}

		(void) OSEventObjectWaitTimeout(hOSEvent, gsCwq.ui32FenceWaitTimeUs);

		if (! ui32RetryAbort)
		{
#if defined(CACHEOP_DEBUG)
			sCacheOpWorkItem.uiSize = (uintptr_t) OSAtomicRead(&gsCwq.hCompletedSeqNum);
			sCacheOpWorkItem.uiOffset = 0;
			uiTimeNow = OSClockns64();
#endif
			PVR_LOG(("CacheOpFence() event: "CACHEOP_ABORT_FENCE_ERROR_STRING));
			eError = PVRSRV_ERROR_RETRY;
			break;
		}
		else
		{
#if defined(CACHEOP_DEBUG)
			uiTimeNow = OSClockns64();
#endif
		}
	}

	eError2 = OSEventObjectClose(hOSEvent);
	PVR_LOG_IF_ERROR(eError2, "OSEventObjectOpen");

e0:
#if defined(CACHEOP_DEBUG)
	sCacheOpWorkItem.ui64ExecuteTime = uiTimeNow;
	if (ui32FenceOpSeqNum)
	{
		IMG_UINT64 ui64TimeTakenNs = sCacheOpWorkItem.ui64EnqueuedTime - sCacheOpWorkItem.ui64ExecuteTime;
		IMG_UINT32 ui32Time;
		IMG_INT32 i32Div;

		do_div(ui64TimeTakenNs, 1000);
		ui32Time = ui64TimeTakenNs;

		/* Only fences pending on CacheOps contribute towards statistics,
		 * Calculate the approximate cumulative moving average fence time.
		 * This calculation is based on standard equation:
		 *
		 * CMAnext = (new + count * CMAprev) / (count + 1)
		 *
		 * but in simplified form:
		 *
		 * CMAnext = CMAprev + (new - CMAprev) / (count + 1)
		 *
		 * this gets rid of multiplication and prevents overflow.
		 *
		 * Also to increase accuracy that we lose with integer division,
		 * we hold the moving remainder of the division and add it.
		 *
		 * CMAnext = CMAprev + (new - CMAprev + CMRprev) / (count + 1)
		 *
		 * Multiple tests proved it to be the best solution for approximating
		 * CMA using integers.
		 *
		 */

		i32Div = (IMG_INT32)ui32Time - (IMG_INT32)gsCwq.ui32AvgFenceTime + (IMG_INT32)gsCwq.ui32AvgFenceTimeRemainder;


		gsCwq.ui32AvgFenceTime += i32Div / (IMG_INT32)(gsCwq.ui32TotalFenceOps + 1);
		gsCwq.ui32AvgFenceTimeRemainder = i32Div % (IMG_INT32)(gsCwq.ui32TotalFenceOps + 1);


		gsCwq.ui32TotalFenceOps++;

	}
	CacheOpStatsExecLogWrite(&sCacheOpWorkItem);
#endif

	return eError;
}

PVRSRV_ERROR CacheOpLog (PMR *psPMR,
						 IMG_UINT64 puiAddress,
						 IMG_DEVMEM_OFFSET_T uiOffset,
						 IMG_DEVMEM_SIZE_T uiSize,
						 IMG_UINT64 ui64EnqueuedTimeUs,
						 IMG_UINT64 ui64ExecuteTimeUs,
						 IMG_UINT32 ui32NumRBF,
						 PVRSRV_CACHE_OP uiCacheOp)
{
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};
	PVR_UNREFERENCED_PARAMETER(puiAddress);

	sCacheOpWorkItem.psPMR = psPMR;
	sCacheOpWorkItem.uiSize = uiSize;
	sCacheOpWorkItem.uiOffset = uiOffset;
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
	sCacheOpWorkItem.ui32OpSeqNum = CacheOpGetNextCommonSeqNum();

	sCacheOpWorkItem.ui64EnqueuedTime = ui64EnqueuedTimeUs;
	sCacheOpWorkItem.ui64ExecuteTime = ui64ExecuteTimeUs;
	sCacheOpWorkItem.bUMF = IMG_TRUE;
	gsCwq.ui32ClientRBF += ui32NumRBF;
	gsCwq.ui32ClientSync += 1;

	CacheOpStatsExecLogWrite(&sCacheOpWorkItem);
#else
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(uiCacheOp);
	PVR_UNREFERENCED_PARAMETER(ui32NumRBF);
	PVR_UNREFERENCED_PARAMETER(puiAddress);
	PVR_UNREFERENCED_PARAMETER(ui64ExecuteTimeUs);
	PVR_UNREFERENCED_PARAMETER(ui64EnqueuedTimeUs);
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpInit2 (void)
{
	void *pvAppHintState = NULL;
	IMG_UINT32 ui32AppHintDefault = PVRSRV_APPHINT_CACHEOPTHREADPRIORITY;
	IMG_UINT32 ui32AppHintCacheOpThreadPriority;

	PVRSRV_ERROR eError;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Create an event object for pending CacheOp work items */
	eError = OSEventObjectCreate("PVRSRV_CACHEOP_EVENTOBJECT", &gsCwq.hThreadWakeUpEvtObj);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSEventObjectCreate", e0);

	/* Create an event object for updating pending fence checks on CacheOp */
	eError = OSEventObjectCreate("PVRSRV_CACHEOP_EVENTOBJECT", &gsCwq.hClientWakeUpEvtObj);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSEventObjectCreate", e0);

	/* Appending work-items is not concurrent, lock protects against this */
	eError = OSLockCreate((POS_LOCK*)&gsCwq.hDeferredLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e0);

	/* Apphint read/write is not concurrent, so lock protects against this */
	eError = OSLockCreate((POS_LOCK*)&gsCwq.hConfigLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e0);

	gsCwq.ui32FenceWaitTimeUs = CACHEOP_FENCE_WAIT_TIMEOUT;
	gsCwq.ui32FenceRetryAbort = CACHEOP_FENCE_RETRY_ABORT;

#if defined(CACHEFLUSH_ISA_SUPPORTS_UM_FLUSH)
	gsCwq.bSupportsUMFlush = IMG_TRUE;
#else
	gsCwq.bSupportsUMFlush = IMG_FALSE;
#endif

	gsCwq.pui32InfoPage = psPVRSRVData->pui32InfoPage;
	gsCwq.psInfoPagePMR = psPVRSRVData->psInfoPagePMR;

	/* Normally, platforms should use their default configurations, put exceptions here */
#if defined(__i386__) || defined(__x86_64__)
#if !defined(TC_MEMORY_CONFIG)
	CacheOpConfigUpdate(CACHEOP_CONFIG_URBF | CACHEOP_CONFIG_KDF);
#else
	CacheOpConfigUpdate(CACHEOP_CONFIG_KDF);
#endif
#else /* defined(__x86__) */
	CacheOpConfigUpdate(CACHEOP_CONFIG_DEFAULT);
#endif

	/* Initialise the remaining occupants of the CacheOp information page */
	gsCwq.pui32InfoPage[CACHEOP_INFO_PGSIZE]   = (IMG_UINT32)gsCwq.uiPageSize;
	gsCwq.pui32InfoPage[CACHEOP_INFO_LINESIZE] = (IMG_UINT32)gsCwq.uiLineSize;

	/* Set before spawning thread */
	gsCwq.bInit = IMG_TRUE;

	OSCreateKMAppHintState(&pvAppHintState);
	OSGetKMAppHintUINT32(pvAppHintState, CacheOpThreadPriority,
		                     &ui32AppHintDefault, &ui32AppHintCacheOpThreadPriority);
	OSFreeKMAppHintState(pvAppHintState);

	/* Create a thread which is used to execute the deferred CacheOp(s),
	   these are CacheOp(s) executed by the server on behalf of clients
	   asynchronously. All clients synchronise with the server before
	   submitting any HW operation (i.e. device kicks) to ensure that
	   client device work-load memory is coherent */
	eError = OSThreadCreatePriority(&gsCwq.hWorkerThread,
									"pvr_cacheop",
									CacheOpThread,
									CacheOpThreadDumpInfo,
									IMG_TRUE,
									psPVRSRVData,
									ui32AppHintCacheOpThreadPriority);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSThreadCreatePriority", e0);
	{
		DI_ITERATOR_CB sIterator = {.pfnShow = CacheOpConfigRead};
		/* Writing the unsigned integer binary encoding of CACHEOP_CONFIG
		   into this file cycles through avail. configuration(s) */
		eError = DICreateEntry("cacheop_config", NULL, &sIterator, NULL,
		                       DI_ENTRY_TYPE_GENERIC, &gsCwq.psConfigTune);
		PVR_LOG_GOTO_IF_FALSE(gsCwq.psConfigTune, "DICreateEntry", e0);
	}

	/* Register the CacheOp framework (re)configuration handlers */
	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_CacheOpConfig,
										CacheOpConfigQuery,
										CacheOpConfigSet,
										APPHINT_OF_DRIVER_NO_DEVICE,
										(void *) APPHINT_ID_CacheOpConfig);

	PVRSRVAppHintRegisterHandlersUINT32(APPHINT_ID_CacheOpUMKMThresholdSize,
										CacheOpConfigQuery,
										CacheOpConfigSet,
										APPHINT_OF_DRIVER_NO_DEVICE,
										(void *) APPHINT_ID_CacheOpUMKMThresholdSize);

	return PVRSRV_OK;
e0:
	CacheOpDeInit2();
	return eError;
}

void CacheOpDeInit2 (void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	gsCwq.bInit = IMG_FALSE;

	if (gsCwq.hThreadWakeUpEvtObj)
	{
		eError = OSEventObjectSignal(gsCwq.hThreadWakeUpEvtObj);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
	}

	if (gsCwq.hClientWakeUpEvtObj)
	{
		eError = OSEventObjectSignal(gsCwq.hClientWakeUpEvtObj);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
	}

	if (gsCwq.hWorkerThread)
	{
		LOOP_UNTIL_TIMEOUT(OS_THREAD_DESTROY_TIMEOUT_US)
		{
			eError = OSThreadDestroy(gsCwq.hWorkerThread);
			if (PVRSRV_OK == eError)
			{
				gsCwq.hWorkerThread = NULL;
				break;
			}
			OSWaitus(OS_THREAD_DESTROY_TIMEOUT_US/OS_THREAD_DESTROY_RETRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
		PVR_LOG_IF_ERROR(eError, "OSThreadDestroy");
		gsCwq.hWorkerThread = NULL;
	}

	if (gsCwq.hClientWakeUpEvtObj)
	{
		eError = OSEventObjectDestroy(gsCwq.hClientWakeUpEvtObj);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
		gsCwq.hClientWakeUpEvtObj = NULL;
	}

	if (gsCwq.hThreadWakeUpEvtObj)
	{
		eError = OSEventObjectDestroy(gsCwq.hThreadWakeUpEvtObj);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
		gsCwq.hThreadWakeUpEvtObj = NULL;
	}

	if (gsCwq.hConfigLock)
	{
		eError = OSLockDestroy(gsCwq.hConfigLock);
		PVR_LOG_IF_ERROR(eError, "OSLockDestroy");
		gsCwq.hConfigLock = NULL;
	}

	if (gsCwq.hDeferredLock)
	{
		eError = OSLockDestroy(gsCwq.hDeferredLock);
		PVR_LOG_IF_ERROR(eError, "OSLockDestroy");
		gsCwq.hDeferredLock = NULL;
	}

	if (gsCwq.psConfigTune)
	{
		DIDestroyEntry(gsCwq.psConfigTune);
		gsCwq.psConfigTune = NULL;
	}

	gsCwq.pui32InfoPage = NULL;
	gsCwq.psInfoPagePMR = NULL;
}

PVRSRV_ERROR CacheOpInit (void)
{
	IMG_UINT32 idx;
	PVRSRV_ERROR eError = PVRSRV_OK;

	/* DDK initialisation is anticipated to be performed on the boot
	   processor (little core in big/little systems) though this may
	   not always be the case. If so, the value cached here is the
	   system wide safe (i.e. smallest) L1 d-cache line size value
	   on any/such platforms with mismatched d-cache line sizes */
	gsCwq.uiPageSize = OSGetPageSize();
	gsCwq.uiPageShift = OSGetPageShift();
	gsCwq.uiLineSize = OSCPUCacheAttributeSize(OS_CPU_CACHE_ATTRIBUTE_LINE_SIZE);
	gsCwq.uiLineShift = ExactLog2(gsCwq.uiLineSize);
	PVR_LOG_RETURN_IF_FALSE((gsCwq.uiLineSize && gsCwq.uiPageSize && gsCwq.uiPageShift), "", PVRSRV_ERROR_INIT_FAILURE);
	gsCwq.uiCacheOpAddrType = OSCPUCacheOpAddressType();

	/* More information regarding these atomic counters can be found
	   in the CACHEOP_WORK_QUEUE type definition at top of file */
	OSAtomicWrite(&gsCwq.hCompletedSeqNum, 0);
	OSAtomicWrite(&gsCwq.hCommonSeqNum, 0);
	OSAtomicWrite(&gsCwq.hWriteCounter, 0);
	OSAtomicWrite(&gsCwq.hReadCounter, 0);

	for (idx = 0; idx < CACHEOP_INDICES_MAX; idx++)
	{
		gsCwq.asWorkItems[idx].iTimeline = PVRSRV_NO_TIMELINE;
		gsCwq.asWorkItems[idx].psPMR = (void *)(uintptr_t)~0;
		gsCwq.asWorkItems[idx].ui32OpSeqNum = (IMG_UINT32)~0;
	}


#if defined(CACHEOP_DEBUG)
	/* debugfs file read-out is not concurrent, so lock protects against this */
	eError = OSLockCreate((POS_LOCK*)&gsCwq.hStatsExecLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e0);

	gsCwq.i32StatsExecWriteIdx = 0;
	OSCachedMemSet(gsCwq.asStatsExecuted, 0, sizeof(gsCwq.asStatsExecuted));

	{
		DI_ITERATOR_CB sIterator = {.pfnShow = CacheOpStatsExecLogRead};
		/* File captures the most recent subset of CacheOp(s) executed */
		eError = DICreateEntry("cacheop_history", NULL, &sIterator, NULL,
		                       DI_ENTRY_TYPE_GENERIC, &gsCwq.psDIEntry);
		PVR_LOG_GOTO_IF_ERROR(eError, "DICreateEntry", e0);
	}
e0:
#endif
	return eError;
}

void CacheOpDeInit (void)
{
#if defined(CACHEOP_DEBUG)
	if (gsCwq.hStatsExecLock)
	{
		(void) OSLockDestroy(gsCwq.hStatsExecLock);
		gsCwq.hStatsExecLock = NULL;
	}

	if (gsCwq.psDIEntry)
	{
		DIDestroyEntry(gsCwq.psDIEntry);
		gsCwq.psDIEntry = NULL;
	}
#endif
}
