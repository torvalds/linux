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
#define CACHEOP_RI_PRINTF_HEADER			"%-8s %-8s %-10s %-10s %-5s %-16s %-16s %-10s %-10s %-18s"
#define CACHEOP_RI_PRINTF					"%-8d %-8d %-10s %-10s %-5s 0x%-14llx 0x%-14llx 0x%-8llx 0x%-8llx %-18llu\n"
#else
#define CACHEOP_PRINTF_HEADER				"%-8s %-8s %-10s %-10s %-5s %-10s %-10s %-18s"
#define CACHEOP_PRINTF						"%-8d %-8d %-10s %-10s %-5s 0x%-8llx 0x%-8llx %-18llu\n"
#endif
#endif

//#define CACHEOP_NO_CACHE_LINE_ALIGNED_ROUNDING		/* Force OS page (not cache line) flush granularity */
#define CACHEOP_PVR_ASSERT(x)							/* Define as PVR_ASSERT(x), enable for swdev & testing */
#define CACHEOP_DEVMEM_OOR_ERROR_STRING		"cacheop device memory request is out of range"
#define CACHEOP_MAX_DEBUG_MESSAGE_LEN		160

typedef struct _CACHEOP_WORK_ITEM_
{
	PMR *psPMR;
	IMG_DEVMEM_SIZE_T uiSize;
	PVRSRV_CACHE_OP uiCacheOp;
	IMG_DEVMEM_OFFSET_T uiOffset;
	PVRSRV_TIMELINE iTimeline;
	SYNC_TIMELINE_OBJ sSWTimelineObj;
	PVRSRV_DEVICE_NODE *psDevNode;
#if defined(CACHEOP_DEBUG)
	IMG_UINT64 ui64StartTime;
	IMG_UINT64 ui64EndTime;
	IMG_BOOL bKMReq;
	IMG_PID pid;
#endif
} CACHEOP_WORK_ITEM;

typedef struct _CACHEOP_STATS_EXEC_ITEM_
{
	IMG_UINT32 ui32DeviceID;
	IMG_PID pid;
	PVRSRV_CACHE_OP uiCacheOp;
	IMG_DEVMEM_SIZE_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT64 ui64StartTime;
	IMG_UINT64 ui64EndTime;
	IMG_BOOL bKMReq;
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
	IMG_DEV_VIRTADDR sDevVAddr;
	IMG_DEV_PHYADDR sDevPAddr;
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
	PMR *psInfoPagePMR;
	IMG_UINT32 *pui32InfoPage;

#if defined(CACHEOP_DEBUG)
/*
  CacheOp statistics
 */
	DI_ENTRY *psDIEntry;
	IMG_HANDLE hStatsExecLock;

	IMG_UINT32 ui32ServerOps;
	IMG_UINT32 ui32ClientOps;
	IMG_UINT32 ui32TotalOps;
	IMG_UINT32 ui32ServerOpUsedUMVA;
	IMG_UINT32 ui32AvgExecTime;
	IMG_UINT32 ui32AvgExecTimeRemainder;

	IMG_INT32 i32StatsExecWriteIdx;
	CACHEOP_STATS_EXEC_ITEM asStatsExecuted[CACHEOP_STATS_ITEMS_MAX];
#endif

	DI_ENTRY *psConfigTune;
	IMG_HANDLE hConfigLock;
	CACHEOP_CONFIG	eConfig;
	IMG_UINT32		ui32Config;
	IMG_BOOL		bSupportsUMFlush;
} CACHEOP_WORK_QUEUE;

/* Top-level CacheOp framework object */
static CACHEOP_WORK_QUEUE gsCwq;

#define CacheOpConfigSupports(e) ((gsCwq.eConfig & (e)) ? IMG_TRUE : IMG_FALSE)

#if defined(CACHEOP_DEBUG)
static INLINE void CacheOpStatsExecLogHeader(IMG_CHAR szBuffer[CACHEOP_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(szBuffer, CACHEOP_MAX_DEBUG_MESSAGE_LEN,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
				CACHEOP_RI_PRINTF_HEADER,
#else
				CACHEOP_PRINTF_HEADER,
#endif
				"DevID",
				"Pid",
				"CacheOp",
				"Type",
				"Origin",
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
				"DevVAddr",
				"DevPAddr",
#endif
				"Offset",
				"Size",
				"xTime (us)");
}

static void CacheOpStatsExecLogWrite(CACHEOP_WORK_ITEM *psCacheOpWorkItem)
{
	IMG_INT32 i32WriteOffset;
	IMG_UINT32 ui32ExecTime;
	printk("log write\n");
	if (!psCacheOpWorkItem->uiCacheOp)
	{
		return;
	}
	else if (psCacheOpWorkItem->bKMReq && !CacheOpConfigSupports(CACHEOP_CONFIG_KLOG))
	{
		/* KM logs spams the history due to frequency, this removes it completely */
		return;
	}

	OSLockAcquire(gsCwq.hStatsExecLock);

	i32WriteOffset = gsCwq.i32StatsExecWriteIdx;
	gsCwq.i32StatsExecWriteIdx = INCR_WRAP(gsCwq.i32StatsExecWriteIdx);
	gsCwq.asStatsExecuted[i32WriteOffset].ui32DeviceID = psCacheOpWorkItem->psDevNode ? psCacheOpWorkItem->psDevNode->sDevId.ui32InternalID : -1;
	gsCwq.asStatsExecuted[i32WriteOffset].pid = psCacheOpWorkItem->pid;
	gsCwq.asStatsExecuted[i32WriteOffset].uiSize = psCacheOpWorkItem->uiSize;
	gsCwq.asStatsExecuted[i32WriteOffset].bKMReq = psCacheOpWorkItem->bKMReq;
	gsCwq.asStatsExecuted[i32WriteOffset].uiOffset	= psCacheOpWorkItem->uiOffset;
	gsCwq.asStatsExecuted[i32WriteOffset].uiCacheOp = psCacheOpWorkItem->uiCacheOp;
	gsCwq.asStatsExecuted[i32WriteOffset].ui64StartTime = psCacheOpWorkItem->ui64StartTime;
	gsCwq.asStatsExecuted[i32WriteOffset].ui64EndTime = psCacheOpWorkItem->ui64EndTime;

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
#endif

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

	ui32ExecTime =
		gsCwq.asStatsExecuted[i32WriteOffset].ui64EndTime -
		gsCwq.asStatsExecuted[i32WriteOffset].ui64StartTime;

	{

	IMG_INT32 i32Div =
		(IMG_INT32) ui32ExecTime -
		(IMG_INT32) gsCwq.ui32AvgExecTime +
		(IMG_INT32) gsCwq.ui32AvgExecTimeRemainder;

	gsCwq.ui32AvgExecTime += i32Div / (IMG_INT32)(gsCwq.ui32TotalOps + 1);
	gsCwq.ui32AvgExecTimeRemainder = i32Div % (IMG_INT32)(gsCwq.ui32TotalOps + 1);

	gsCwq.ui32TotalOps++;

	}

	if (!gsCwq.asStatsExecuted[i32WriteOffset].bKMReq)
	{
		/* This operation queues only UM CacheOp in per-PID process statistics database */
		PVRSRVStatsUpdateCacheOpStats(
						gsCwq.asStatsExecuted[i32WriteOffset].uiCacheOp,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
						gsCwq.asStatsExecuted[i32WriteOffset].sDevVAddr,
						gsCwq.asStatsExecuted[i32WriteOffset].sDevPAddr,
#endif
						gsCwq.asStatsExecuted[i32WriteOffset].uiOffset,
						gsCwq.asStatsExecuted[i32WriteOffset].uiSize,
						ui32ExecTime,
						!gsCwq.asStatsExecuted[i32WriteOffset].bKMReq,
						psCacheOpWorkItem->pid);
	}

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
e0:
#endif
	OSLockRelease(gsCwq.hStatsExecLock);
}

static int CacheOpStatsExecLogRead(OSDI_IMPL_ENTRY *psEntry, void *pvData)
{
	IMG_CHAR *pszFlushType;
	IMG_CHAR *pszCacheOpType;
	IMG_CHAR *pszFlushSource;
	IMG_INT32 i32ReadOffset;
	IMG_INT32 i32WriteOffset;

	IMG_CHAR szBuffer[CACHEOP_MAX_DEBUG_MESSAGE_LEN] = {0};
	PVR_UNREFERENCED_PARAMETER(pvData);

	OSLockAcquire(gsCwq.hStatsExecLock);

	DIPrintf(psEntry,
			"Primary CPU d-cache architecture: LSZ: 0x%x, URBF: %s\n",
			gsCwq.uiLineSize,
			gsCwq.bSupportsUMFlush ? "Yes" : "No");

	DIPrintf(psEntry,
			"Configuration: UKT: %d, URBF: %s\n",
			gsCwq.pui32InfoPage[CACHEOP_INFO_UMKMTHRESHLD],
			gsCwq.eConfig & CACHEOP_CONFIG_URBF ? "Yes" : "No");

	DIPrintf(psEntry,
			"Summary: Total Ops [%d] - Server(using UMVA)/Client [%d(%d)/%d]. Avg execution time [%d]\n",
			gsCwq.ui32TotalOps, gsCwq.ui32ServerOps, gsCwq.ui32ServerOpUsedUMVA, gsCwq.ui32ClientOps, gsCwq.ui32AvgExecTime);


	CacheOpStatsExecLogHeader(szBuffer);
	DIPrintf(psEntry, "%s\n", szBuffer);

	i32WriteOffset = gsCwq.i32StatsExecWriteIdx;
	for (i32ReadOffset = DECR_WRAP(i32WriteOffset);
		 i32ReadOffset != i32WriteOffset;
		 i32ReadOffset = DECR_WRAP(i32ReadOffset))
	{
		IMG_UINT64 ui64ExecTime =
			gsCwq.asStatsExecuted[i32ReadOffset].ui64EndTime -
			gsCwq.asStatsExecuted[i32ReadOffset].ui64StartTime;

		IMG_DEVMEM_SIZE_T ui64NumOfPages =
			gsCwq.asStatsExecuted[i32ReadOffset].uiSize >> gsCwq.uiPageShift;


		if (!gsCwq.asStatsExecuted[i32ReadOffset].uiCacheOp)
		{
			break;
		}
		if (ui64NumOfPages <= PMR_MAX_TRANSLATION_STACK_ALLOC)
		{
			pszFlushType = "RBF.Fast";
		}
		else
		{
			pszFlushType = "RBF.Slow";
		}

		pszFlushSource = gsCwq.asStatsExecuted[i32ReadOffset].bKMReq ? " KM" : " UM";

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
				pszFlushType = "      ";
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
						gsCwq.asStatsExecuted[i32ReadOffset].ui32DeviceID,
						gsCwq.asStatsExecuted[i32ReadOffset].pid,
						pszCacheOpType,
						pszFlushType,
						pszFlushSource,
#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO) && defined(DEBUG)
						gsCwq.asStatsExecuted[i32ReadOffset].sDevVAddr.uiAddr,
						gsCwq.asStatsExecuted[i32ReadOffset].sDevPAddr.uiAddr,
#endif
						gsCwq.asStatsExecuted[i32ReadOffset].uiOffset,
						gsCwq.asStatsExecuted[i32ReadOffset].uiSize,
						ui64ExecTime);

	}

	OSLockRelease(gsCwq.hStatsExecLock);

	return 0;
}
#endif /* defined(CACHEOP_DEBUG) */

static INLINE void CacheOpStatsReset(void)
{
#if defined(CACHEOP_DEBUG)
	gsCwq.ui32ServerOps = 0;
	gsCwq.ui32ClientOps = 0;
	gsCwq.ui32TotalOps = 0;
	gsCwq.ui32ServerOpUsedUMVA = 0;
	gsCwq.ui32AvgExecTime = 0;
	gsCwq.ui32AvgExecTimeRemainder = 0;

	gsCwq.i32StatsExecWriteIdx = 0;

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

	DIPrintf(psEntry, "URBF: %s\n",
		gsCwq.eConfig & CACHEOP_CONFIG_URBF ? "Yes" : "No");

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
			gsCwq.ui32ServerOpUsedUMVA += 1;
#endif
			return PVRSRV_OK;
		}
		else if (pvAddress)
		{
			/* Round down the incoming VA (if any) down to the nearest page aligned VA */
			pvAddress = (void*)((uintptr_t)pvAddress & ~((uintptr_t)gsCwq.uiPageSize-1));
#if defined(CACHEOP_DEBUG)
			gsCwq.ui32ServerOpUsedUMVA += 1;
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

static PVRSRV_ERROR CacheOpBatchExecTimeline(PVRSRV_DEVICE_NODE *psDevNode,
											 PVRSRV_TIMELINE iTimeline)
{
	PVRSRV_ERROR eError;
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {NULL};

	eError = CacheOpTimelineBind(psDevNode, &sCacheOpWorkItem, iTimeline);
	PVR_LOG_RETURN_IF_ERROR(eError, "CacheOpTimelineBind");

	eError = CacheOpTimelineExec(&sCacheOpWorkItem);
	PVR_LOG_IF_ERROR(eError, "CacheOpTimelineExec");

	return eError;
}

static PVRSRV_ERROR CacheOpBatchExecRangeBased(PVRSRV_DEVICE_NODE *psDevNode,
											PMR **ppsPMR,
											IMG_CPU_VIRTADDR *pvAddress,
											IMG_DEVMEM_OFFSET_T *puiOffset,
											IMG_DEVMEM_SIZE_T *puiSize,
											PVRSRV_CACHE_OP *puiCacheOp,
											IMG_UINT32 ui32NumCacheOps,
											PVRSRV_TIMELINE uiTimeline)
{
	IMG_UINT32 ui32Idx;
	IMG_BOOL bBatchHasTimeline;
	PVRSRV_ERROR eError = PVRSRV_OK;

#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
#endif

	/* Check if batch has an associated timeline update */
	bBatchHasTimeline = puiCacheOp[ui32NumCacheOps-1] & PVRSRV_CACHE_OP_TIMELINE;
	puiCacheOp[ui32NumCacheOps-1] &= ~(PVRSRV_CACHE_OP_TIMELINE);

	for (ui32Idx = 0; ui32Idx < ui32NumCacheOps; ui32Idx++)
	{
		/* Fail UM request, don't silently ignore */
		PVR_GOTO_IF_INVALID_PARAM(puiSize[ui32Idx], eError, e0);

#if defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.ui64StartTime = OSClockus64();
#endif

		eError = CacheOpPMRExec(ppsPMR[ui32Idx],
								pvAddress[ui32Idx],
								puiOffset[ui32Idx],
								puiSize[ui32Idx],
								puiCacheOp[ui32Idx],
								IMG_FALSE);
		PVR_LOG_GOTO_IF_ERROR(eError, "CacheOpExecPMR", e0);

#if defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.ui64EndTime = OSClockus64();

		sCacheOpWorkItem.psDevNode = psDevNode;
		sCacheOpWorkItem.psPMR = ppsPMR[ui32Idx];
		sCacheOpWorkItem.uiSize = puiSize[ui32Idx];
		sCacheOpWorkItem.uiOffset = puiOffset[ui32Idx];
		sCacheOpWorkItem.uiCacheOp = puiCacheOp[ui32Idx];
		CacheOpStatsExecLogWrite(&sCacheOpWorkItem);

		gsCwq.ui32ServerOps += 1;
#endif
	}

e0:
	if (bBatchHasTimeline)
	{
		eError = CacheOpBatchExecTimeline(psDevNode, uiTimeline);
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
	IMG_UINT64 ui64StartTime = OSClockus64();
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

		gsCwq.ui32ServerOps += 1;

		sCacheOpWorkItem.uiOffset = 0;
		sCacheOpWorkItem.bKMReq = IMG_TRUE;
		sCacheOpWorkItem.uiCacheOp = uiCacheOp;
		/* Use information page PMR for logging KM request */
		sCacheOpWorkItem.psPMR = gsCwq.psInfoPagePMR;
		sCacheOpWorkItem.psDevNode = psDevNode;
		sCacheOpWorkItem.ui64StartTime = ui64StartTime;
		sCacheOpWorkItem.ui64EndTime = OSClockus64();
		sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
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

	sCacheOpWorkItem.ui64StartTime = OSClockus64();
#endif

	eError = CacheOpPMRExec(psPMR,
							pvAddress,
							uiOffset,
							uiSize,
							uiCacheOp,
							IMG_FALSE);
	PVR_LOG_GOTO_IF_ERROR(eError, "CacheOpPMRExec", e0);

#if defined(CACHEOP_DEBUG)
	sCacheOpWorkItem.ui64EndTime = OSClockus64();

	sCacheOpWorkItem.psDevNode = PMR_DeviceNode(psPMR);
	sCacheOpWorkItem.psPMR = psPMR;
	sCacheOpWorkItem.uiSize = uiSize;
	sCacheOpWorkItem.uiOffset = uiOffset;
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
	CacheOpStatsExecLogWrite(&sCacheOpWorkItem);

	gsCwq.ui32ServerOps += 1;
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
						   IMG_UINT32 ui32OpTimeline)
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
		eError = CacheOpBatchExecTimeline(psDevNode, uiTimeline);
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
									   uiTimeline);
	}

	return eError;
}

PVRSRV_ERROR CacheOpLog (PMR *psPMR,
						 IMG_UINT64 puiAddress,
						 IMG_DEVMEM_OFFSET_T uiOffset,
						 IMG_DEVMEM_SIZE_T uiSize,
						 IMG_UINT64 ui64StartTime,
						 IMG_UINT64 ui64EndTime,
						 PVRSRV_CACHE_OP uiCacheOp)
{
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem = {0};
	PVR_UNREFERENCED_PARAMETER(puiAddress);

	sCacheOpWorkItem.psDevNode = PMR_DeviceNode(psPMR);
	sCacheOpWorkItem.psPMR = psPMR;
	sCacheOpWorkItem.uiSize = uiSize;
	sCacheOpWorkItem.uiOffset = uiOffset;
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();

	sCacheOpWorkItem.ui64StartTime = ui64StartTime;
	sCacheOpWorkItem.ui64EndTime = ui64EndTime;

	gsCwq.ui32ClientOps += 1;

	CacheOpStatsExecLogWrite(&sCacheOpWorkItem);
#else
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(uiCacheOp);
	PVR_UNREFERENCED_PARAMETER(puiAddress);
	PVR_UNREFERENCED_PARAMETER(ui64StartTime);
	PVR_UNREFERENCED_PARAMETER(ui64EndTime);
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpInit2 (void)
{
	PVRSRV_ERROR eError;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Apphint read/write is not concurrent, so lock protects against this */
	eError = OSLockCreate((POS_LOCK*)&gsCwq.hConfigLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", e0);


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
	gsCwq.bInit = IMG_FALSE;

	if (gsCwq.hConfigLock)
	{
		OSLockDestroy(gsCwq.hConfigLock);
		gsCwq.hConfigLock = NULL;
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
	PVRSRV_ERROR eError = PVRSRV_OK;

	gsCwq.uiPageSize = OSGetPageSize();
	gsCwq.uiPageShift = OSGetPageShift();
	gsCwq.uiLineSize = OSCPUCacheAttributeSize(OS_CPU_CACHE_ATTRIBUTE_LINE_SIZE);
	gsCwq.uiLineShift = ExactLog2(gsCwq.uiLineSize);
	PVR_LOG_RETURN_IF_FALSE((gsCwq.uiLineSize && gsCwq.uiPageSize && gsCwq.uiPageShift), "", PVRSRV_ERROR_INIT_FAILURE);
	gsCwq.uiCacheOpAddrType = OSCPUCacheOpAddressType();

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
		OSLockDestroy(gsCwq.hStatsExecLock);
		gsCwq.hStatsExecLock = NULL;
	}

	if (gsCwq.psDIEntry)
	{
		DIDestroyEntry(gsCwq.psDIEntry);
		gsCwq.psDIEntry = NULL;
	}
#endif
}
