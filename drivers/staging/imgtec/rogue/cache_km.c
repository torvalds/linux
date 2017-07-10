/*************************************************************************/ /*!
@File           cache_km.c
@Title          CPU data cache management
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Implements server side code for CPU cache maintenance management.
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
#if defined(CONFIG_SW_SYNC)
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
#include <linux/sw_sync.h>
#else
#include <../drivers/staging/android/sw_sync.h>
#endif
#include <linux/file.h>
#include <linux/fs.h>
#endif
#include "pmr.h"
#include "device.h"
#include "pvrsrv.h"
#include "osfunc.h"
#include "cache_km.h"
#include "pvr_debug.h"
#include "lock_types.h"
#include "allocmem.h"
#include "process_stats.h"
#if defined(PVR_RI_DEBUG)
#include "ri_server.h"
#endif

/* Top-level file-local build definitions */
#if defined(DEBUG) && defined(LINUX)
	#define CACHEOP_DEBUG
#endif

/* Type of cache maintenance mechanism being used */
#if (CACHEFLUSH_KM_TYPE == CACHEFLUSH_KM_RANGEBASED_DEFERRED)
	#define SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED
	#define SUPPORT_RANGEBASED_CACHEFLUSH
#elif (CACHEFLUSH_KM_TYPE == CACHEFLUSH_KM_RANGEBASED)
	#define SUPPORT_RANGEBASED_CACHEFLUSH
#elif (CACHEFLUSH_KM_TYPE == CACHEFLUSH_KM_GLOBAL)
	/* Nothing to do here */
#else
	#error "Unknown CACHEFLUSH_KM_TYPE"
#endif

typedef struct _CACHEOP_WORK_ITEM_
{
	DLLIST_NODE sNode;
	PMR *psPMR;
	struct file *psTimeline;
	IMG_UINT32 ui32OpSeqNum;
	IMG_DEVMEM_SIZE_T uiSize;
	PVRSRV_CACHE_OP uiCacheOp;
	IMG_DEVMEM_OFFSET_T uiOffset;
	IMG_BOOL bSignalEventObject;
#if defined(CACHEOP_DEBUG)
	IMG_UINT64	ui64QueuedTime;
	IMG_UINT64	ui64ExecuteTime;
	IMG_BOOL bRBF;
	IMG_BOOL bUMF;
	IMG_PID pid;
#if defined(PVR_RI_DEBUG)
	RGXFWIF_DM eFenceOpType;
#endif
#endif
} CACHEOP_WORK_ITEM;

/* Copy of CPU page & dcache-line size */
static size_t guiOSPageSize;
static IMG_UINT32 guiCacheLineSize;

/* 
  System-wide CacheOp sequence numbers
  - ghCommonCacheOpSeqNum:
		This common sequence, numbers mostly CacheOp requests
		from UM/KM but might also number fence checks and
		completed CacheOps depending on SUPPORT_XXX configs.
  - ghCompletedCacheOpSeqNum:
		This tracks last CacheOp request that was executed
		in all SUPPORT_XXX configurations and is used for
		fence checks exclusively.
*/
static ATOMIC_T ghCommonCacheOpSeqNum;
static ATOMIC_T ghCompletedCacheOpSeqNum;

#if defined(CACHEOP_DEBUG)
#define CACHEOP_MAX_STATS_ITEMS 128
#define INCR_WRAP(x) ((x+1) >= CACHEOP_MAX_STATS_ITEMS ? 0 : (x+1))
#define DECR_WRAP(x) ((x-1) < 0 ? (CACHEOP_MAX_STATS_ITEMS-1) : (x-1))
#if defined(PVR_RI_DEBUG)
/* Refer to CacheOpStatExecLogHeader() for header item names */
#define CACHEOP_RI_PRINTF_HEADER "%-10s %-10s %-5s %-8s %-16s %-10s %-10s %-18s %-12s"
#define CACHEOP_RI_PRINTF_FENCE	 "%-10s %-10s %-5s %-8d %-16s %-10s %-10s %-18llu 0x%-10x\n"
#define CACHEOP_RI_PRINTF		 "%-10s %-10s %-5s %-8d 0x%-14llx 0x%-8llx 0x%-8llx %-18llu 0x%-10x\n"
#else
#define CACHEOP_PRINTF_HEADER	 "%-10s %-10s %-5s %-10s %-10s %-18s %-12s"
#define CACHEOP_PRINTF_FENCE	 "%-10s %-10s %-5s %-10s %-10s %-18llu 0x%-10x\n"
#define CACHEOP_PRINTF		 	 "%-10s %-10s %-5s 0x%-8llx 0x%-8llx %-18llu 0x%-10x\n"
#endif

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

#if defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED)
typedef struct _CACHEOP_STAT_STALL_ITEM_
{
	IMG_UINT32 ui32OpSeqNum;
	IMG_UINT32 ui32RetryCount;
	IMG_UINT64 ui64QueuedTime;
	IMG_UINT64 ui64ExecuteTime;
} CACHEOP_STAT_STALL_ITEM;

/* These are used in an atomic way so will never
   hold values outside of the valid range */
static IMG_INT32 gi32CacheOpStatStallWriteIdx;
static IMG_HANDLE ghCacheOpStatStallLock;
static void *pvCacheOpStatStallEntry;

static CACHEOP_STAT_STALL_ITEM gasCacheOpStatStalled[CACHEOP_MAX_STATS_ITEMS];

static INLINE void CacheOpStatStallLogHeader(IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
				"%-10s %-12s %-10s",
				"SeqNo",
				"Time (ns)",
				"RetryCount");
}

static INLINE void CacheOpStatStallLogWrite(IMG_UINT32 ui32FenceOpSeqNum,
											IMG_UINT64 ui64QueuedTime,
											IMG_UINT64 ui64ExecuteTime,
											IMG_UINT32 ui32RetryCount)
{
	IMG_INT32 i32WriteOffset = gi32CacheOpStatStallWriteIdx;
	gi32CacheOpStatStallWriteIdx = INCR_WRAP(gi32CacheOpStatStallWriteIdx);
	gasCacheOpStatStalled[i32WriteOffset].ui32RetryCount = ui32RetryCount;
	gasCacheOpStatStalled[i32WriteOffset].ui32OpSeqNum = ui32FenceOpSeqNum;
	gasCacheOpStatStalled[i32WriteOffset].ui64QueuedTime = ui64QueuedTime;
	gasCacheOpStatStalled[i32WriteOffset].ui64ExecuteTime = ui64ExecuteTime;
}

static void CacheOpStatStallLogRead(void *pvFilePtr, void *pvData,
							 OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	IMG_INT32 i32ReadOffset;
	IMG_INT32 i32WriteOffset;
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN]={0};

	PVR_UNREFERENCED_PARAMETER(pvData);

	CacheOpStatStallLogHeader(szBuffer);
	pfnOSStatsPrintf(pvFilePtr, "%s\n", szBuffer);

	OSLockAcquire(ghCacheOpStatStallLock);

	i32WriteOffset = gi32CacheOpStatStallWriteIdx;
	for (i32ReadOffset = DECR_WRAP(i32WriteOffset);
		 i32ReadOffset != i32WriteOffset; 
		 i32ReadOffset = DECR_WRAP(i32ReadOffset))
	{
		IMG_UINT64 ui64QueuedTime, ui64ExecuteTime;

		if (gasCacheOpStatStalled[i32ReadOffset].ui32OpSeqNum == 0)
		{
			break;
		}

		/* Convert from nano-seconds to micro-seconds */
		ui64ExecuteTime = gasCacheOpStatStalled[i32ReadOffset].ui64ExecuteTime;
		ui64QueuedTime = gasCacheOpStatStalled[i32ReadOffset].ui64QueuedTime;
		ui64ExecuteTime = DivBy10(DivBy10(DivBy10(ui64ExecuteTime)));
		ui64QueuedTime = DivBy10(DivBy10(DivBy10(ui64QueuedTime)));

		pfnOSStatsPrintf(pvFilePtr,
						"%-10x 0x%-10llx %-10x\n",
						gasCacheOpStatStalled[i32ReadOffset].ui32OpSeqNum,
						ui64QueuedTime < ui64ExecuteTime ?
								ui64ExecuteTime - ui64QueuedTime :
								ui64QueuedTime - ui64ExecuteTime,
						gasCacheOpStatStalled[i32ReadOffset].ui32RetryCount);
	}

	OSLockRelease(ghCacheOpStatStallLock);
}
#endif

typedef struct _CACHEOP_STAT_EXEC_ITEM_
{
	IMG_UINT32 ui32OpSeqNum;
	PVRSRV_CACHE_OP uiCacheOp;
	IMG_DEVMEM_SIZE_T uiOffset;
	IMG_DEVMEM_SIZE_T uiSize;
	IMG_UINT64 ui64QueuedTime;
	IMG_UINT64 ui64ExecuteTime;
	IMG_BOOL bHasTimeline;
	IMG_BOOL bIsFence;
	IMG_BOOL bRBF;
	IMG_BOOL bUMF;
#if defined(PVR_RI_DEBUG)
	IMG_DEV_VIRTADDR sDevVAddr;
	RGXFWIF_DM eFenceOpType;
	IMG_PID pid;
#endif
} CACHEOP_STAT_EXEC_ITEM;

/* These are used in an atomic way so will never
   hold values outside of the valid range */
static IMG_INT32 gi32CacheOpStatExecWriteIdx;
static IMG_HANDLE ghCacheOpStatExecLock;
static void *pvCacheOpStatExecEntry;

static CACHEOP_STAT_EXEC_ITEM gasCacheOpStatExecuted[CACHEOP_MAX_STATS_ITEMS];

static INLINE void CacheOpStatExecLogHeader(IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN])
{
	OSSNPrintf(szBuffer, PVR_MAX_DEBUG_MESSAGE_LEN,
#if defined(PVR_RI_DEBUG)
				CACHEOP_RI_PRINTF_HEADER,
#else
				CACHEOP_PRINTF_HEADER,
#endif
				"CacheOp",
				"Type",
				"Mode",
#if defined(PVR_RI_DEBUG)
				"Pid",
				"DevVAddr",
#endif
				"Offset",
				"Size",
				"Time (us)",
				"SeqNo");
}

static INLINE void CacheOpStatExecLogWrite(DLLIST_NODE *psNode)
{
	CACHEOP_WORK_ITEM *psCacheOpWorkItem;
	IMG_UINT64 ui64ExecuteTime;
	IMG_UINT64 ui64QueuedTime;
	IMG_INT32 i32WriteOffset;

	psCacheOpWorkItem = IMG_CONTAINER_OF(psNode, CACHEOP_WORK_ITEM, sNode);	
	if (psCacheOpWorkItem->ui32OpSeqNum == 0)
	{
		/* This breaks the logic of read-out, so we
		   do not queue items with zero sequence
		   number */
		return;
	}

	i32WriteOffset = gi32CacheOpStatExecWriteIdx;
	gi32CacheOpStatExecWriteIdx = INCR_WRAP(gi32CacheOpStatExecWriteIdx);

	gasCacheOpStatExecuted[i32WriteOffset].uiSize = psCacheOpWorkItem->uiSize;
	gasCacheOpStatExecuted[i32WriteOffset].uiOffset	= psCacheOpWorkItem->uiOffset;
	gasCacheOpStatExecuted[i32WriteOffset].uiCacheOp = psCacheOpWorkItem->uiCacheOp;
	gasCacheOpStatExecuted[i32WriteOffset].ui32OpSeqNum	= psCacheOpWorkItem->ui32OpSeqNum;
	gasCacheOpStatExecuted[i32WriteOffset].ui64QueuedTime = psCacheOpWorkItem->ui64QueuedTime;
	gasCacheOpStatExecuted[i32WriteOffset].ui64ExecuteTime = psCacheOpWorkItem->ui64ExecuteTime;
	gasCacheOpStatExecuted[i32WriteOffset].bHasTimeline	= psCacheOpWorkItem->psPMR == NULL;
	gasCacheOpStatExecuted[i32WriteOffset].bRBF = psCacheOpWorkItem->bRBF;
	gasCacheOpStatExecuted[i32WriteOffset].bUMF = psCacheOpWorkItem->bUMF;
	gasCacheOpStatExecuted[i32WriteOffset].bIsFence	 =
			psCacheOpWorkItem->psPMR == NULL && psCacheOpWorkItem->psTimeline == NULL;
#if defined(PVR_RI_DEBUG)
	gasCacheOpStatExecuted[i32WriteOffset].pid = psCacheOpWorkItem->pid;
	PVR_ASSERT(gasCacheOpStatExecuted[i32WriteOffset].pid);

	if (psCacheOpWorkItem->psPMR != NULL)
	{
		PVRSRV_ERROR eError;

		/* Get more detailed information regarding the sub allocations that
		   PMR has from RI manager for process that requested the CacheOp */
		eError = RIDumpProcessListKM(psCacheOpWorkItem->psPMR,
									 gasCacheOpStatExecuted[i32WriteOffset].pid,
									 gasCacheOpStatExecuted[i32WriteOffset].uiOffset,
									 &gasCacheOpStatExecuted[i32WriteOffset].sDevVAddr);
		if (eError != PVRSRV_OK)
		{
			return;
		}
	}

	if (gasCacheOpStatExecuted[i32WriteOffset].bIsFence)
	{
		gasCacheOpStatExecuted[i32WriteOffset].eFenceOpType = psCacheOpWorkItem->eFenceOpType;
	}
#endif

	ui64ExecuteTime = gasCacheOpStatExecuted[i32WriteOffset].ui64ExecuteTime;
	ui64QueuedTime = gasCacheOpStatExecuted[i32WriteOffset].ui64QueuedTime;

	/* This operation queues this CacheOp in per-PID process statistics database */
	PVRSRVStatsUpdateCacheOpStats(gasCacheOpStatExecuted[i32WriteOffset].uiCacheOp,
					gasCacheOpStatExecuted[i32WriteOffset].ui32OpSeqNum,
#if defined(PVR_RI_DEBUG)
					gasCacheOpStatExecuted[i32WriteOffset].sDevVAddr,
					gasCacheOpStatExecuted[i32WriteOffset].eFenceOpType,
#endif
					gasCacheOpStatExecuted[i32WriteOffset].uiOffset,
					gasCacheOpStatExecuted[i32WriteOffset].uiSize,
					ui64QueuedTime < ui64ExecuteTime ?
						ui64ExecuteTime - ui64QueuedTime:
						ui64QueuedTime - ui64ExecuteTime,
					gasCacheOpStatExecuted[i32WriteOffset].bRBF,
					gasCacheOpStatExecuted[i32WriteOffset].bUMF,
					gasCacheOpStatExecuted[i32WriteOffset].bIsFence,
					gasCacheOpStatExecuted[i32WriteOffset].bHasTimeline,
					psCacheOpWorkItem->pid);
}

static void CacheOpStatExecLogRead(void *pvFilePtr, void *pvData,
								OS_STATS_PRINTF_FUNC* pfnOSStatsPrintf)
{
	IMG_INT32 i32ReadOffset;
	IMG_INT32 i32WriteOffset;
	IMG_CHAR *pszCacheOpType;
	IMG_CHAR *pszFlushSource;
	IMG_CHAR *pszFlushype;
	IMG_UINT64 ui64QueuedTime, ui64ExecuteTime;
	IMG_CHAR szBuffer[PVR_MAX_DEBUG_MESSAGE_LEN]={0};

	PVR_UNREFERENCED_PARAMETER(pvData);

	CacheOpStatExecLogHeader(szBuffer);
	pfnOSStatsPrintf(pvFilePtr, "%s\n", szBuffer);

	OSLockAcquire(ghCacheOpStatExecLock);

	i32WriteOffset = gi32CacheOpStatExecWriteIdx;
	for (i32ReadOffset = DECR_WRAP(i32WriteOffset);
		 i32ReadOffset != i32WriteOffset;
		 i32ReadOffset = DECR_WRAP(i32ReadOffset))
	{
		if (gasCacheOpStatExecuted[i32ReadOffset].ui32OpSeqNum == 0)
		{
			break;
		}

		/* Convert from nano-seconds to micro-seconds */
		ui64ExecuteTime = gasCacheOpStatExecuted[i32ReadOffset].ui64ExecuteTime;
		ui64QueuedTime = gasCacheOpStatExecuted[i32ReadOffset].ui64QueuedTime;
		ui64ExecuteTime = DivBy10(DivBy10(DivBy10(ui64ExecuteTime)));
		ui64QueuedTime = DivBy10(DivBy10(DivBy10(ui64QueuedTime)));

		if (gasCacheOpStatExecuted[i32ReadOffset].bIsFence)
		{
			IMG_CHAR *pszFenceType = "";
			pszCacheOpType = "Fence";

#if defined(PVR_RI_DEBUG)
			switch (gasCacheOpStatExecuted[i32ReadOffset].eFenceOpType)
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
			pfnOSStatsPrintf(pvFilePtr,
#if defined(PVR_RI_DEBUG)
							CACHEOP_RI_PRINTF_FENCE,
#else
							CACHEOP_PRINTF_FENCE,
#endif
							pszCacheOpType,
							pszFenceType,
							"",
#if defined(PVR_RI_DEBUG)
							gasCacheOpStatExecuted[i32ReadOffset].pid,
							"",
#endif
							"",
							"",
							ui64QueuedTime < ui64ExecuteTime ?
										ui64ExecuteTime - ui64QueuedTime :
										ui64QueuedTime - ui64ExecuteTime,
							gasCacheOpStatExecuted[i32ReadOffset].ui32OpSeqNum);
		}
		else if (gasCacheOpStatExecuted[i32ReadOffset].bHasTimeline)
		{
			pfnOSStatsPrintf(pvFilePtr,
#if defined(PVR_RI_DEBUG)
							CACHEOP_RI_PRINTF_FENCE,
#else
							CACHEOP_PRINTF_FENCE,
#endif
							"Timeline",
							"",
							"",
#if defined(PVR_RI_DEBUG)
							gasCacheOpStatExecuted[i32ReadOffset].pid,
							"",
#endif
							"",
							"",
							ui64QueuedTime < ui64ExecuteTime ?
										ui64ExecuteTime - ui64QueuedTime :
										ui64QueuedTime - ui64ExecuteTime,
							gasCacheOpStatExecuted[i32ReadOffset].ui32OpSeqNum);
		}
		else
		{
			if (gasCacheOpStatExecuted[i32ReadOffset].bRBF)
			{
				IMG_DEVMEM_SIZE_T ui64NumOfPages;

				ui64NumOfPages = gasCacheOpStatExecuted[i32ReadOffset].uiSize >> OSGetPageShift();
				if (ui64NumOfPages <= PMR_MAX_TRANSLATION_STACK_ALLOC)
				{
					pszFlushype = "RBF.Fast";
				}
				else
				{
					pszFlushype = "RBF.Slow";
				}
			}
			else
			{
				pszFlushype = "GF";
			}

			if (gasCacheOpStatExecuted[i32ReadOffset].bUMF)
			{
				pszFlushSource = "UM";
			}
			else
			{
				pszFlushSource = "KM";
			}

			switch (gasCacheOpStatExecuted[i32ReadOffset].uiCacheOp)
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

			pfnOSStatsPrintf(pvFilePtr,
#if defined(PVR_RI_DEBUG)
							CACHEOP_RI_PRINTF,
#else
							CACHEOP_PRINTF,
#endif
							pszCacheOpType,
							pszFlushype,
							pszFlushSource,
#if defined(PVR_RI_DEBUG)
							gasCacheOpStatExecuted[i32ReadOffset].pid,
							gasCacheOpStatExecuted[i32ReadOffset].sDevVAddr.uiAddr,
#endif
							gasCacheOpStatExecuted[i32ReadOffset].uiOffset,
							gasCacheOpStatExecuted[i32ReadOffset].uiSize,
							ui64QueuedTime < ui64ExecuteTime ?
										ui64ExecuteTime - ui64QueuedTime :
										ui64QueuedTime - ui64ExecuteTime,
							gasCacheOpStatExecuted[i32ReadOffset].ui32OpSeqNum);
		}
	}

	OSLockRelease(ghCacheOpStatExecLock);
}

static PVRSRV_ERROR CacheOpStatExecLog(void *pvData)
{
	DLLIST_NODE *psListNode = (DLLIST_NODE *) pvData;
	DLLIST_NODE *psCurrentNode, *psNextNode;

	OSLockAcquire(ghCacheOpStatExecLock);

	CacheOpStatExecLogWrite(psListNode);
	dllist_foreach_node (psListNode, psCurrentNode, psNextNode)
	{
		CacheOpStatExecLogWrite(psCurrentNode);
	}

	OSLockRelease(ghCacheOpStatExecLock);

	return PVRSRV_OK;
}
#endif /* defined(CACHEOP_DEBUG) */

//#define CACHEOP_NO_CACHE_LINE_ALIGNED_ROUNDING
#define CACHEOP_SEQ_MIDPOINT (IMG_UINT32) 0x7FFFFFFF
#define CACHEOP_DPFL PVR_DBG_MESSAGE

/* Perform requested CacheOp on the CPU data cache for successive cache
   line worth of bytes up to page or in-page cache-line boundary */
static INLINE void CacheOpCPURangeBased(PVRSRV_DEVICE_NODE *psDevNode,
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
	PVR_ASSERT(uiPgAlignedOffset < uiCLAlignedEndOffset);
	uiRelFlushSize = (IMG_DEVMEM_SIZE_T)guiOSPageSize;
	uiRelFlushOffset = 0;

	if (uiCLAlignedStartOffset > uiPgAlignedOffset)
	{
		/* Zero unless initially starting at an in-page offset */
		uiRelFlushOffset = uiCLAlignedStartOffset - uiPgAlignedOffset;
		uiRelFlushSize -= uiRelFlushOffset;
	}

	/* uiRelFlushSize is guiOSPageSize unless current outstanding CacheOp
	   size is smaller. The 1st case handles in-page CacheOp range and
	   the 2nd case handles multiple-page CacheOp range with a last
	   CacheOp size that is less than guiOSPageSize */
	uiNextPgAlignedOffset = uiPgAlignedOffset + (IMG_DEVMEM_SIZE_T)guiOSPageSize;
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
	pbCpuVirtAddrStart = pbCpuVirtAddr + uiRelFlushOffset;
	pbCpuVirtAddrEnd = pbCpuVirtAddrStart + uiRelFlushSize;
	sCpuPhyAddrStart.uiAddr = sCpuPhyAddr.uiAddr + uiRelFlushOffset;
	sCpuPhyAddrEnd.uiAddr = sCpuPhyAddrStart.uiAddr + uiRelFlushSize;

	switch (uiCacheOp)
	{
		case PVRSRV_CACHE_OP_CLEAN:
			OSCleanCPUCacheRangeKM(psDevNode, pbCpuVirtAddrStart, pbCpuVirtAddrEnd,
									sCpuPhyAddrStart, sCpuPhyAddrEnd);
			break;
		case PVRSRV_CACHE_OP_INVALIDATE:
			OSInvalidateCPUCacheRangeKM(psDevNode, pbCpuVirtAddrStart, pbCpuVirtAddrEnd,
									sCpuPhyAddrStart, sCpuPhyAddrEnd);
			break;
		case PVRSRV_CACHE_OP_FLUSH:
			OSFlushCPUCacheRangeKM(psDevNode, pbCpuVirtAddrStart, pbCpuVirtAddrEnd,
									sCpuPhyAddrStart, sCpuPhyAddrEnd);
			break;
		default:
			PVR_DPF((PVR_DBG_ERROR,	"%s: Invalid cache operation type %d",
					__FUNCTION__, uiCacheOp));
			PVR_ASSERT(0);
			break;
	}
}

/* This function assumes the PMR is locked */
static PVRSRV_ERROR CacheOpRangeBased (PMR *psPMR,
									   IMG_DEVMEM_OFFSET_T uiOffset,
									   IMG_DEVMEM_SIZE_T uiSize,
									   PVRSRV_CACHE_OP uiCacheOp,
									   IMG_BOOL *bUsedGlobalFlush)
{
	IMG_HANDLE hPrivOut;
	IMG_BOOL bPMRIsSparse;
	IMG_UINT32 ui32PageIndex;
	IMG_UINT32 ui32NumOfPages;
	IMG_DEVMEM_SIZE_T uiOutSize;
	IMG_DEVMEM_SIZE_T uiPgAlignedSize;
	IMG_DEVMEM_OFFSET_T uiCLAlignedEndOffset;
	IMG_DEVMEM_OFFSET_T uiPgAlignedEndOffset;
	IMG_DEVMEM_OFFSET_T uiCLAlignedStartOffset;
	IMG_DEVMEM_OFFSET_T uiPgAlignedStartOffset;
	IMG_DEVMEM_OFFSET_T uiPgAlignedOffsetNext;
	PVRSRV_CACHE_OP_ADDR_TYPE uiCacheOpAddrType;
	IMG_BOOL abValid[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_CPU_PHYADDR asCpuPhyAddr[PMR_MAX_TRANSLATION_STACK_ALLOC];
	IMG_UINT32 OS_PAGE_SHIFT = (IMG_UINT32) OSGetPageShift();
	IMG_CPU_PHYADDR *psCpuPhyAddr = asCpuPhyAddr;
	IMG_BOOL bIsPMRDataRetrieved = IMG_FALSE;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BYTE *pbCpuVirtAddr = NULL;
	IMG_BOOL *pbValid = abValid;

	if (uiCacheOp == PVRSRV_CACHE_OP_NONE)
	{
		PVR_ASSERT(0);
		return PVRSRV_OK;
	}
	else
	{
		/* Carry out full dcache operation if size (in pages) qualifies */
		if (uiSize >= PVR_DIRTY_BYTES_FLUSH_THRESHOLD)
		{
			/* Flush, so we can skip subsequent invalidates */
			eError = OSCPUOperation(PVRSRV_CACHE_OP_FLUSH);
			if (eError == PVRSRV_OK)
			{
				*bUsedGlobalFlush = IMG_TRUE;
				return PVRSRV_OK;
			}
		}
	}

	/* Need this for kernel mapping */
	bPMRIsSparse = PMR_IsSparse(psPMR);

	/* Round the incoming offset down to the nearest cache-line / page aligned-address */
	uiCLAlignedEndOffset = uiOffset + uiSize;
	uiCLAlignedEndOffset = PVR_ALIGN(uiCLAlignedEndOffset, (IMG_DEVMEM_SIZE_T)guiCacheLineSize);
	uiCLAlignedStartOffset = (uiOffset & ~((IMG_DEVMEM_OFFSET_T)guiCacheLineSize-1));

	uiPgAlignedEndOffset = uiCLAlignedEndOffset;
	uiPgAlignedEndOffset = PVR_ALIGN(uiPgAlignedEndOffset, (IMG_DEVMEM_SIZE_T)guiOSPageSize);
	uiPgAlignedStartOffset = (uiOffset & ~((IMG_DEVMEM_OFFSET_T)guiOSPageSize-1));
	uiPgAlignedSize = uiPgAlignedEndOffset - uiPgAlignedStartOffset;

#if defined(CACHEOP_NO_CACHE_LINE_ALIGNED_ROUNDING)
	/* For internal debug if cache-line optimised
	   flushing is suspected of causing data corruption */
	uiCLAlignedStartOffset = uiPgAlignedStartOffset;
	uiCLAlignedEndOffset = uiPgAlignedEndOffset;
#endif

	/* Which type of address(es) do we need for this CacheOp */
	uiCacheOpAddrType = OSCPUCacheOpAddressType(uiCacheOp);

	/* Type of allocation backing the PMR data */
	ui32NumOfPages = uiPgAlignedSize >> OS_PAGE_SHIFT;
	if (ui32NumOfPages > PMR_MAX_TRANSLATION_STACK_ALLOC)
	{
		/* The pbValid array is allocated first as it is needed in
		   both physical/virtual cache maintenance methods */
		pbValid = OSAllocZMem(ui32NumOfPages * sizeof(IMG_BOOL));
		if (pbValid != NULL)
		{
			if (uiCacheOpAddrType != PVRSRV_CACHE_OP_ADDR_TYPE_VIRTUAL)
			{
				psCpuPhyAddr = OSAllocZMem(ui32NumOfPages * sizeof(IMG_CPU_PHYADDR));
				if (psCpuPhyAddr == NULL)
				{
					psCpuPhyAddr = asCpuPhyAddr;
					OSFreeMem(pbValid);
					pbValid = abValid;
				}
			}
		}
		else
		{
			pbValid = abValid;
		}
	}

	/* We always retrieve PMR data in bulk, up-front if number of pages is within
	   PMR_MAX_TRANSLATION_STACK_ALLOC limits else we check to ensure that a 
	   dynamic buffer has been allocated to satisfy requests outside limits */
	if (ui32NumOfPages <= PMR_MAX_TRANSLATION_STACK_ALLOC || pbValid != abValid)
	{
		if (uiCacheOpAddrType != PVRSRV_CACHE_OP_ADDR_TYPE_VIRTUAL)
		{
			/* Look-up PMR CpuPhyAddr once, if possible */
			eError = PMR_CpuPhysAddr(psPMR,
									 OS_PAGE_SHIFT,
									 ui32NumOfPages,
									 uiPgAlignedStartOffset,
									 psCpuPhyAddr,
									 pbValid);
			if (eError == PVRSRV_OK)
			{
				bIsPMRDataRetrieved = IMG_TRUE;
			}
		}
		else
		{
			/* Look-up PMR per-page validity once, if possible */
			eError = PMR_IsOffsetValid(psPMR,
									   OS_PAGE_SHIFT,
									   ui32NumOfPages,
									   uiPgAlignedStartOffset,
									   pbValid);
			bIsPMRDataRetrieved = eError == PVRSRV_OK ? IMG_TRUE : IMG_FALSE;
		}
	}

	/* For each device page, carry out the requested cache maintenance operation */
	for (uiPgAlignedOffsetNext = uiPgAlignedStartOffset, ui32PageIndex = 0;
		 uiPgAlignedOffsetNext < uiPgAlignedEndOffset;
		 uiPgAlignedOffsetNext += (IMG_DEVMEM_OFFSET_T) guiOSPageSize, ui32PageIndex += 1)
	{
		if (bIsPMRDataRetrieved == IMG_FALSE)
		{
			/* Never cross page boundary without looking up corresponding
			   PMR page physical address and/or page validity if these
			   were not looked-up, in bulk, up-front */	
			ui32PageIndex = 0;
			if (uiCacheOpAddrType != PVRSRV_CACHE_OP_ADDR_TYPE_VIRTUAL)
			{
				eError = PMR_CpuPhysAddr(psPMR,
										 OS_PAGE_SHIFT,
										 1,
										 uiPgAlignedOffsetNext,
										 psCpuPhyAddr,
										 pbValid);
				if (eError != PVRSRV_OK)
				{
					PVR_ASSERT(0);
					goto e0;
				}
			}
			else
			{
				eError = PMR_IsOffsetValid(psPMR,
										  OS_PAGE_SHIFT,
										  1,
										  uiPgAlignedOffsetNext,
										  pbValid);
				if (eError != PVRSRV_OK)
				{
					PVR_ASSERT(0);
					goto e0;
				}
			}
		}

		/* Skip invalid PMR pages (i.e. sparse) */
		if (pbValid[ui32PageIndex] == IMG_FALSE)
		{
			continue;
		}

		/* Skip virtual address acquire if CacheOp can be maintained
		   entirely using PMR physical addresses */
		if (uiCacheOpAddrType != PVRSRV_CACHE_OP_ADDR_TYPE_PHYSICAL)
		{
			if (bPMRIsSparse)
			{
				eError =
					PMRAcquireSparseKernelMappingData(psPMR,
													  uiPgAlignedOffsetNext,
													  guiOSPageSize,
													  (void **)&pbCpuVirtAddr,
													  (size_t*)&uiOutSize,
													  &hPrivOut);
				if (eError != PVRSRV_OK)
				{
					PVR_ASSERT(0);
					goto e0;
				}
			}
			else
			{
				eError =
					PMRAcquireKernelMappingData(psPMR,
												uiPgAlignedOffsetNext,
												guiOSPageSize,
												(void **)&pbCpuVirtAddr,
												(size_t*)&uiOutSize,
												&hPrivOut);
				if (eError != PVRSRV_OK)
				{
					PVR_ASSERT(0);
					goto e0;
				}
			}
		}

		/* Issue actual cache maintenance for PMR */
		CacheOpCPURangeBased(PMR_DeviceNode(psPMR),
							uiCacheOp,
							pbCpuVirtAddr,
							(uiCacheOpAddrType != PVRSRV_CACHE_OP_ADDR_TYPE_VIRTUAL) ?
								psCpuPhyAddr[ui32PageIndex] : psCpuPhyAddr[0],
							uiPgAlignedOffsetNext,
							uiCLAlignedStartOffset,
							uiCLAlignedEndOffset);

		/* Skip virtual address release if CacheOp can be maintained
		   entirely using PMR physical addresses */
		if (uiCacheOpAddrType != PVRSRV_CACHE_OP_ADDR_TYPE_PHYSICAL)
		{
			eError = PMRReleaseKernelMappingData(psPMR, hPrivOut);
			PVR_ASSERT(eError == PVRSRV_OK);
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

	return eError;
}

static INLINE IMG_BOOL CacheOpFenceCheck(IMG_UINT32 ui32UpdateSeqNum,
										 IMG_UINT32 ui32FenceSeqNum)
{
	IMG_UINT32 ui32RebasedUpdateNum;
	IMG_UINT32 ui32RebasedFenceNum;
	IMG_UINT32 ui32Rebase;

	if (ui32FenceSeqNum == 0)
	{
		return IMG_TRUE;
	}

	/*
	   The problem statement is how to compare two values
	   on a numerical sequentially incrementing timeline in
	   the presence of wrap around arithmetic semantics using
	   a single ui32 counter & atomic (increment) operations.

	   The rationale for the solution here is to rebase the
	   incoming values to the sequence midpoint and perform
	   comparisons there; this allows us to handle overflow
	   or underflow wrap-round using only a single integer.

	   NOTE: We assume that the absolute value of the 
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
	ui32Rebase = CACHEOP_SEQ_MIDPOINT - ui32UpdateSeqNum;

	/* ui32Rebase could be either positive/negative, in
	   any case we still perform operation using unsigned
	   semantics as 2's complement notation always means
	   we end up with the correct result */
	ui32RebasedUpdateNum = ui32Rebase + ui32UpdateSeqNum;
	ui32RebasedFenceNum = ui32Rebase + ui32FenceSeqNum;

	return (ui32RebasedUpdateNum >= ui32RebasedFenceNum);
}

#if defined(SUPPORT_RANGEBASED_CACHEFLUSH)
#if defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED)
/* Wait 8hrs when no deferred CacheOp is required;
   for fence checks, wait 10ms then retry */
#define CACHEOP_THREAD_WAIT_TIMEOUT 28800000000ULL
#define CACHEOP_FENCE_WAIT_TIMEOUT  10000ULL

typedef struct _CACHEOP_CLEANUP_WORK_ITEM_
{
	PVRSRV_CLEANUP_THREAD_WORK sCleanupWorkItem;
	DLLIST_NODE *psListNode;
} CACHEOP_CLEANUP_WORK_ITEM;

/* These are used to track pending CacheOps */
static IMG_DEVMEM_SIZE_T guiPendingDevmemSize;
static IMG_BOOL gbPendingTimeline;

static INLINE PVRSRV_ERROR CacheOpFree(void *pvData)
{
	CACHEOP_CLEANUP_WORK_ITEM *psCacheOpCleanupItem = pvData;
	DLLIST_NODE *psListNode = psCacheOpCleanupItem->psListNode;
	CACHEOP_WORK_ITEM *psCacheOpWorkItem;
	DLLIST_NODE *psNodeIter;

	while (! dllist_is_empty(psListNode))
	{
		psNodeIter = dllist_get_next_node(psListNode);
		dllist_remove_node(psNodeIter);

		psCacheOpWorkItem = IMG_CONTAINER_OF(psNodeIter, CACHEOP_WORK_ITEM, sNode);
		if (psCacheOpWorkItem->psPMR)
		{
			PMRUnlockSysPhysAddresses(psCacheOpWorkItem->psPMR);
		}

		OSFreeMem(psCacheOpWorkItem);
	}

	/* Finally free pseudo head node which is also a valid CacheOp work item */
	psCacheOpWorkItem = IMG_CONTAINER_OF(psListNode, CACHEOP_WORK_ITEM, sNode);
	if (psCacheOpWorkItem->psPMR)
	{
		PMRUnlockSysPhysAddresses(psCacheOpWorkItem->psPMR);
	}

	OSFreeMem(psCacheOpWorkItem);
	OSFreeMem(psCacheOpCleanupItem);

	return PVRSRV_OK;
}

static INLINE PVRSRV_ERROR CacheOpCleanup(DLLIST_NODE *psListNode)
{
	CACHEOP_CLEANUP_WORK_ITEM *psCacheOpCleanupItem;
	PVRSRV_ERROR eError = PVRSRV_OK;

	psCacheOpCleanupItem = OSAllocMem(sizeof(CACHEOP_CLEANUP_WORK_ITEM));
	if (! psCacheOpCleanupItem)
	{
		PVR_DPF((CACHEOP_DPFL,
				"%s: performing sync cleanup",
				__FUNCTION__));
		eError = CacheOpFree(psListNode);
	}
	else
	{
		psCacheOpCleanupItem->psListNode = psListNode;
		psCacheOpCleanupItem->sCleanupWorkItem.ui32RetryCount = 0;
		psCacheOpCleanupItem->sCleanupWorkItem.pfnFree = CacheOpFree;
		psCacheOpCleanupItem->sCleanupWorkItem.pvData = psCacheOpCleanupItem;
		psCacheOpCleanupItem->sCleanupWorkItem.bDependsOnHW = IMG_FALSE;
		PVRSRVCleanupThreadAddWork(&psCacheOpCleanupItem->sCleanupWorkItem);
	}

	return eError;
}

static INLINE PVRSRV_ERROR CacheOpEnqueue(PVRSRV_DATA *psPVRSRVData,
										CACHEOP_WORK_ITEM *psData,
										IMG_UINT32 *psSeqNum)
{
	OSLockAcquire(psPVRSRVData->hCacheOpThreadWorkListLock);

	/* Queue this CacheOp work item into the pending list, update queue size */
	dllist_add_to_tail(&psPVRSRVData->sCacheOpThreadWorkList, &psData->sNode);
	gbPendingTimeline = psData->psTimeline ? IMG_TRUE : gbPendingTimeline;
	guiPendingDevmemSize += psData->uiSize;

	/* Advance the system-wide CacheOp common sequence value */
	*psSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
	if (! *psSeqNum)
	{
		/* Zero is _not_ a valid sequence value, doing so 
		   simplifies subsequent fence checking when no
		   cache maintenance operation is outstanding as
		   in this case a fence value of zero is supplied */
		*psSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
	}
	psData->ui32OpSeqNum = *psSeqNum;

	OSLockRelease(psPVRSRVData->hCacheOpThreadWorkListLock);

	return PVRSRV_OK;
}

static INLINE DLLIST_NODE *CacheOpDequeue(PVRSRV_DATA *psPVRSRVData,
										  IMG_UINT64 *uiQueueDevmemSize,
										  IMG_BOOL   *bHasTimeline)
{
	DLLIST_NODE *psListNode = NULL;

	OSLockAcquire(psPVRSRVData->hCacheOpThreadWorkListLock);

	if (! dllist_is_empty(&psPVRSRVData->sCacheOpThreadWorkList))
	{
		/* Replace entire pending list with a (re)initialized list */
		psListNode = psPVRSRVData->sCacheOpThreadWorkList.psNextNode;
		dllist_remove_node(&psPVRSRVData->sCacheOpThreadWorkList);
		dllist_init(&psPVRSRVData->sCacheOpThreadWorkList);

		/* These capture information about this dequeued list */
		*uiQueueDevmemSize = (IMG_UINT64) guiPendingDevmemSize;
		guiPendingDevmemSize = (IMG_DEVMEM_SIZE_T) 0;
		*bHasTimeline = gbPendingTimeline;
		gbPendingTimeline = IMG_FALSE;
	}

	OSLockRelease(psPVRSRVData->hCacheOpThreadWorkListLock);

	return psListNode;
}

static PVRSRV_ERROR CacheOpExecGlobal(PVRSRV_DATA *psPVRSRVData,
										DLLIST_NODE *psListNode)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32CacheOpSeqNum;
	CACHEOP_WORK_ITEM *psCacheOpWorkItem;
	DLLIST_NODE *psCurrentNode, *psNextNode;
#if defined(CACHEOP_DEBUG)
	IMG_UINT64 uiTimeNow;
#endif

	eError = OSCPUOperation(PVRSRV_CACHE_OP_FLUSH);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

#if defined(CACHEOP_DEBUG)
	uiTimeNow = OSClockns64();
#endif

	/* The head node is a _valid_ CacheOp work item so process it first */
	psCacheOpWorkItem = IMG_CONTAINER_OF(psListNode, CACHEOP_WORK_ITEM, sNode);
#if defined(CACHEOP_DEBUG)
	psCacheOpWorkItem->ui64ExecuteTime = uiTimeNow;
	psCacheOpWorkItem->bRBF = IMG_FALSE;
	psCacheOpWorkItem->bUMF = IMG_FALSE;
#endif

	/* Process other queue CacheOp work items if present */
	dllist_foreach_node (psListNode, psCurrentNode, psNextNode)
	{
		psCacheOpWorkItem = IMG_CONTAINER_OF(psCurrentNode, CACHEOP_WORK_ITEM, sNode);
#if defined(CACHEOP_DEBUG)
		psCacheOpWorkItem->ui64ExecuteTime = uiTimeNow;
		psCacheOpWorkItem->bRBF = IMG_FALSE;
		psCacheOpWorkItem->bUMF = IMG_FALSE;
#endif
	}

	/* Last CacheOp item updates ghCompletedCacheOpSeqNum */
	ui32CacheOpSeqNum = psCacheOpWorkItem->ui32OpSeqNum;
	OSAtomicWrite(&ghCompletedCacheOpSeqNum, ui32CacheOpSeqNum);

	/* Signal any waiting threads blocked on CacheOp fence checks;
	   update completed sequence number to last queue work item */
	eError = OSEventObjectSignal(psPVRSRVData->hCacheOpUpdateEventObject);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

	return eError;
}

static PVRSRV_ERROR CacheOpExecRangeBased(PVRSRV_DATA *psPVRSRVData,
										  DLLIST_NODE *psListNode)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	CACHEOP_WORK_ITEM *psCacheOpWorkItem;
	DLLIST_NODE *psNodeIter = psListNode;
	IMG_BOOL bSkipRemainingCacheOps = IMG_FALSE;
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM *psPrevWorkItem = NULL;
#endif

	do
	{
		/* Lookup corresponding work item & perform cache maintenance operation if
		   it is a non-timeline work-item (i.e. pmr is null) else notify timeline */
		psCacheOpWorkItem = IMG_CONTAINER_OF(psNodeIter, CACHEOP_WORK_ITEM, sNode);
		if (psCacheOpWorkItem->psPMR != NULL)
		{
			if (bSkipRemainingCacheOps == IMG_FALSE)
			{
				eError = CacheOpRangeBased(psCacheOpWorkItem->psPMR,
										   psCacheOpWorkItem->uiOffset,
										   psCacheOpWorkItem->uiSize,
										   psCacheOpWorkItem->uiCacheOp,
										   &bSkipRemainingCacheOps);
				if (eError != PVRSRV_OK)
				{
					/* This _should_ not fail but if it does, not much
					   we can do about it; for now we log it but still
					   increment the completed CacheOp seq number */
					PVR_DPF((CACHEOP_DPFL, 
							 "CacheOp failed: PMR:%p Offset:%llx Size:%llx CacheOp:%d",
							 psCacheOpWorkItem->psPMR,
							 psCacheOpWorkItem->uiOffset,
							 psCacheOpWorkItem->uiSize,
							 psCacheOpWorkItem->uiCacheOp));
					PVR_ASSERT(0);
				}
			}

#if defined(CACHEOP_DEBUG)
			psCacheOpWorkItem->ui64ExecuteTime = bSkipRemainingCacheOps ?
				(psPrevWorkItem ? psPrevWorkItem->ui64ExecuteTime : OSClockns64()) : OSClockns64();
			psCacheOpWorkItem->bRBF = !bSkipRemainingCacheOps;
			psCacheOpWorkItem->bUMF = IMG_FALSE;
			psPrevWorkItem = psCacheOpWorkItem;
#endif

			/* Currently executed CacheOp item updates ghCompletedCacheOpSeqNum */
			OSAtomicWrite(&ghCompletedCacheOpSeqNum, psCacheOpWorkItem->ui32OpSeqNum);

			if (psCacheOpWorkItem->bSignalEventObject == IMG_TRUE)
			{
				/* It is possible that multiple CacheOp work items from two or more
				   threads might be present within processed queue so we have to
				   signal when these CacheOps are  processed to unblock waiting threads */
				eError = OSEventObjectSignal(psPVRSRVData->hCacheOpUpdateEventObject);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
			}
		}
		else
		{
			PVR_ASSERT(psCacheOpWorkItem->psTimeline != NULL);

			OSAtomicWrite(&ghCompletedCacheOpSeqNum, psCacheOpWorkItem->ui32OpSeqNum);

#if defined(CONFIG_SW_SYNC)
			sw_sync_timeline_inc(psCacheOpWorkItem->psTimeline->private_data, 1);
			fput(psCacheOpWorkItem->psTimeline);
#endif

#if defined(CACHEOP_DEBUG)
			psCacheOpWorkItem->ui64ExecuteTime = OSClockns64();
#endif
		}

		/* This terminates on NULL or 1 item queue */
		psNodeIter = dllist_get_next_node(psNodeIter);
	} while (psNodeIter && psNodeIter != psListNode);

	return eError;
}

static void CacheOpExecQueuedList(PVRSRV_DATA *psPVRSRVData)
{
	PVRSRV_ERROR eError;
	DLLIST_NODE *psListNode;
	IMG_BOOL bUseGlobalCachOp;
	IMG_BOOL bHasTimeline = IMG_FALSE;
	IMG_UINT64 ui64Size = (IMG_UINT64) 0;
	IMG_UINT64 ui64FlushThreshold = PVR_DIRTY_BYTES_FLUSH_THRESHOLD;

	/* Obtain the current queue of pending CacheOps, this also provides
	   information pertaining to the queue such as if one or more 
	   CacheOps in the queue is a timeline request and the total
	   CacheOp size */
	psListNode = CacheOpDequeue(psPVRSRVData, &ui64Size, &bHasTimeline);
	if (psListNode == NULL)
	{
		/* This should _not_ happen but if it does, wake-up waiting threads */
		eError = OSEventObjectSignal(psPVRSRVData->hCacheOpUpdateEventObject);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		return;
	}

	/* Perform a global cache operation if queue size (in pages)
	   qualifies and there is no work item in the queue which is a
	   timeline request */
	bUseGlobalCachOp = ui64Size >= ui64FlushThreshold;
	if (bUseGlobalCachOp == IMG_TRUE && bHasTimeline == IMG_FALSE)
	{
		eError = CacheOpExecGlobal(psPVRSRVData, psListNode);
		if (eError == PVRSRV_OK)
		{
			goto e0;
		}
	}

	/* Else use range-based cache maintenance per queue item */
	eError = CacheOpExecRangeBased(psPVRSRVData, psListNode);

e0:
#if defined(CACHEOP_DEBUG)
	eError = CacheOpStatExecLog(psListNode);
#endif

	/* Once done, defer CacheOp cleanup */
	eError = CacheOpCleanup(psListNode);
}

static void CacheOpThread(void *pvData)
{
	PVRSRV_DATA *psPVRSRVData = pvData;
	IMG_HANDLE  hOSEvent;
	PVRSRV_ERROR eError;

	PVR_DPF((CACHEOP_DPFL, "%s: thread starting...", __FUNCTION__));

	/* Store the process id (pid) of the CacheOp-up thread */
	psPVRSRVData->CacheOpThreadPid = OSGetCurrentProcessID();

	/* Open CacheOp thread event object, abort driver if event object open fails */
	eError = OSEventObjectOpen(psPVRSRVData->hCacheOpThreadEventObject, &hOSEvent);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* While driver is in good state and not being unloaded, perform pending cache maintenance */
	while ((psPVRSRVData->eServicesState == PVRSRV_SERVICES_STATE_OK) && (!psPVRSRVData->bUnload))
	{
		/* Wait here until when signalled for queued (pending) CacheOp work items */
		eError = OSEventObjectWaitTimeout(hOSEvent, CACHEOP_THREAD_WAIT_TIMEOUT);
		if (eError == PVRSRV_ERROR_TIMEOUT)
		{
			PVR_DPF((CACHEOP_DPFL, "%s: wait timeout", __FUNCTION__));
		}
		else if (eError == PVRSRV_OK)
		{
			PVR_DPF((CACHEOP_DPFL, "%s: wait OK, signal received", __FUNCTION__));
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: wait error %d", __FUNCTION__, eError));
		}

		CacheOpExecQueuedList(psPVRSRVData);
	}

	eError = OSEventObjectClose(hOSEvent);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");

	PVR_DPF((CACHEOP_DPFL, "%s: thread terminating...", __FUNCTION__));
}

static PVRSRV_ERROR CacheOpExecQueue (PMR **ppsPMR,
									  IMG_DEVMEM_OFFSET_T *puiOffset,
									  IMG_DEVMEM_SIZE_T *puiSize,
									  PVRSRV_CACHE_OP *puiCacheOp,
									  IMG_UINT32 ui32NumCacheOps,
									  IMG_UINT32 *pui32OpSeqNum)
{
	IMG_UINT32 ui32Idx;
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData->bUnload)
	{
		PVR_DPF((CACHEOP_DPFL, 
				"%s: driver unloading, performing CacheOp synchronously",
				__FUNCTION__));

		for (ui32Idx = 0; ui32Idx < ui32NumCacheOps; ui32Idx++)
		{
			(void)CacheOpExec(ppsPMR[ui32Idx],
							  puiOffset[ui32Idx],
							  puiSize[ui32Idx],
							  puiCacheOp[ui32Idx]);
		}

		/* No CacheOp fence dependencies */
		*pui32OpSeqNum = 0;
	}
	else
	{
		IMG_DEVMEM_SIZE_T uiLogicalSize;
		CACHEOP_WORK_ITEM *psCacheOpWorkItem = NULL;

		for (ui32Idx = 0; ui32Idx < ui32NumCacheOps; ui32Idx++)
		{
			/* As PVRSRV_CACHE_OP_INVALIDATE is used to transfer
			   device memory buffer ownership back to processor
			   we cannot defer it so must action it immediately */
			if (puiCacheOp[ui32Idx] & PVRSRV_CACHE_OP_INVALIDATE)
			{
				eError = CacheOpExec (ppsPMR[ui32Idx],
									  puiOffset[ui32Idx],
									  puiSize[ui32Idx],
									  puiCacheOp[ui32Idx]);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((CACHEOP_DPFL,
							"%s: PVRSRV_CACHE_OP_INVALIDATE failed (%u)",
							__FUNCTION__, eError));
				}

				/* Clear CacheOp fence dependencies if single entry; in a
				   multiple entry batch, preserve fence dependency update */
				*pui32OpSeqNum = (ui32Idx == 0) ? 0 : *pui32OpSeqNum;
				continue;
			}

			/* Ensure request is valid before deferring to CacheOp thread */
			eError = PMR_LogicalSize(ppsPMR[ui32Idx], &uiLogicalSize);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((CACHEOP_DPFL,
						"%s: PMR_LogicalSize failed (%u), cannot defer CacheOp",
						__FUNCTION__, eError));

				/* Signal the CacheOp thread to ensure queued items get processed */
				(void) OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

				return eError;
			}
			else if ((puiOffset[ui32Idx]+puiSize[ui32Idx]) > uiLogicalSize)
			{
				PVR_DPF((CACHEOP_DPFL,
						"%s: Invalid parameters, cannot defer CacheOp",
						__FUNCTION__));

				/* Signal the CacheOp thread to ensure queued items get processed */
				(void) OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

				return PVRSRV_ERROR_INVALID_PARAMS;;
			}

			/* For now use dynamic alloc, static CCB _might_ be faster */
			psCacheOpWorkItem = OSAllocMem(sizeof(CACHEOP_WORK_ITEM));
			if (psCacheOpWorkItem == NULL)
			{
				PVR_DPF((CACHEOP_DPFL, "%s: OSAllocMem failed (%u)",
						__FUNCTION__, eError));

				/* Signal the CacheOp thread to ensure whatever was enqueued thus
				   far (if any) gets processed even though we fail the request */
				eError = OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

				return PVRSRV_ERROR_OUT_OF_MEMORY;
			}

			/* For safety, take reference here in user context; to speed
			   up deferred cache management we drop reference as late as
			   possible (during cleanup) */
			eError = PMRLockSysPhysAddresses(ppsPMR[ui32Idx]);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((CACHEOP_DPFL, "%s: PMRLockSysPhysAddresses failed (%u)",
						__FUNCTION__, eError));

				OSFreeMem(psCacheOpWorkItem);
				psCacheOpWorkItem = NULL;

				/* Signal the CacheOp thread to ensure whatever was enqueued thus
				   far (if any) gets processed even though we fail the request */
				eError = OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
				PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");

				return eError;
			}

			/* Prepare & enqueue CacheOp work item */
#if defined(CACHEOP_DEBUG)
			psCacheOpWorkItem->pid = OSGetCurrentClientProcessIDKM();
			psCacheOpWorkItem->ui64QueuedTime = OSClockns64();
#endif
			psCacheOpWorkItem->bSignalEventObject = IMG_FALSE;
			psCacheOpWorkItem->uiCacheOp = puiCacheOp[ui32Idx];
			psCacheOpWorkItem->uiOffset = puiOffset[ui32Idx];
			psCacheOpWorkItem->uiSize = puiSize[ui32Idx];
			psCacheOpWorkItem->psPMR = ppsPMR[ui32Idx];
			psCacheOpWorkItem->psTimeline = NULL;

			if (ui32Idx == (ui32NumCacheOps - 1))
			{
				/* The idea here is to track the last CacheOp in a
				   batch queue so that we only wake-up stalled threads
				   waiting on fence checks when such CacheOp has been
				   processed; this serves to reduce spurious thread
				   wake-up */
				psCacheOpWorkItem->bSignalEventObject = IMG_TRUE;
			}

			eError = CacheOpEnqueue(psPVRSRVData, psCacheOpWorkItem, pui32OpSeqNum);
			PVR_LOG_IF_ERROR(eError, "CacheOpEnqueue");
		}

		if (psCacheOpWorkItem != NULL)
		{
			/* Signal the CacheOp thread to ensure this item gets processed */
			eError = OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}
	}

	return eError;
}
#else /* defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED) */
static PVRSRV_ERROR CacheOpExecQueue(PMR **ppsPMR,
									IMG_DEVMEM_OFFSET_T *puiOffset,
									IMG_DEVMEM_SIZE_T *puiSize,
									PVRSRV_CACHE_OP *puiCacheOp,
									IMG_UINT32 ui32NumCacheOps,
									IMG_UINT32 *pui32OpSeqNum)
{
	IMG_UINT32 ui32Idx;
	PVRSRV_ERROR eError = PVRSRV_OK;

	for (ui32Idx = 0; ui32Idx < ui32NumCacheOps; ui32Idx++)
	{
		PVRSRV_ERROR eError2 = CacheOpExec(ppsPMR[ui32Idx],
										   puiOffset[ui32Idx],
										   puiSize[ui32Idx],
										   puiCacheOp[ui32Idx]);
		if (eError2 != PVRSRV_OK)
		{
			eError = eError2;
			PVR_DPF((CACHEOP_DPFL,
					"%s: CacheOpExec failed (%u)",
					__FUNCTION__, eError));
		}
	}

	/* For immediate RBF, common/completed are identical */
	*pui32OpSeqNum = OSAtomicRead(&ghCommonCacheOpSeqNum);
	OSAtomicWrite(&ghCompletedCacheOpSeqNum, *pui32OpSeqNum);

	return eError;
}
#endif

PVRSRV_ERROR CacheOpQueue (IMG_UINT32 ui32NumCacheOps,
						   PMR **ppsPMR,
						   IMG_DEVMEM_OFFSET_T *puiOffset,
						   IMG_DEVMEM_SIZE_T *puiSize,
						   PVRSRV_CACHE_OP *puiCacheOp,
						   IMG_UINT32 *pui32OpSeqNum)
{
	return CacheOpExecQueue(ppsPMR,
							puiOffset,
							puiSize,
							puiCacheOp,
							ui32NumCacheOps,
							pui32OpSeqNum);
}

PVRSRV_ERROR CacheOpFence (RGXFWIF_DM eFenceOpType,
						   IMG_UINT32 ui32FenceOpSeqNum)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_UINT32 ui32CompletedOpSeqNum;
	IMG_BOOL b1stCacheOpFenceCheckPass;
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem;
	IMG_UINT64 uiTimeNow = OSClockns64();
	IMG_UINT32 ui32RetryCount = 0;

	dllist_init(&sCacheOpWorkItem.sNode);

	/* No PMR/timeline for fence CacheOp */
	sCacheOpWorkItem.psPMR = NULL;
	sCacheOpWorkItem.psTimeline = NULL;
	sCacheOpWorkItem.ui64QueuedTime = uiTimeNow;
	sCacheOpWorkItem.ui32OpSeqNum = ui32FenceOpSeqNum;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
#if defined(PVR_RI_DEBUG)
	sCacheOpWorkItem.eFenceOpType = eFenceOpType;
#endif
#endif

	PVR_UNREFERENCED_PARAMETER(eFenceOpType);

	ui32CompletedOpSeqNum = OSAtomicRead(&ghCompletedCacheOpSeqNum);
	b1stCacheOpFenceCheckPass = CacheOpFenceCheck(ui32CompletedOpSeqNum, ui32FenceOpSeqNum);

#if defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED)
	/* If initial fence check fails, then wait-and-retry in loop */
	if (b1stCacheOpFenceCheckPass == IMG_FALSE)
	{
		IMG_HANDLE hOSEvent;
		PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

		/* Open CacheOp update event object, if event object open fails return error */
		eError = OSEventObjectOpen(psPVRSRVData->hCacheOpUpdateEventObject, &hOSEvent);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((CACHEOP_DPFL,
					"%s: failed to open update event object",
					__FUNCTION__));
			goto e0;
		}

		/* (Re)read completed cache op sequence number before wait */
		ui32CompletedOpSeqNum = OSAtomicRead(&ghCompletedCacheOpSeqNum);

		/* Check if the CacheOp dependencies for this thread are met */
		eError = CacheOpFenceCheck(ui32CompletedOpSeqNum, ui32FenceOpSeqNum) ?
				PVRSRV_OK : PVRSRV_ERROR_FAILED_DEPENDENCIES;

		while (eError != PVRSRV_OK)
		{
			/* Wait here until signalled that update has occurred by CacheOp thread */
			eError = OSEventObjectWaitTimeout(hOSEvent, CACHEOP_FENCE_WAIT_TIMEOUT);
			if (eError == PVRSRV_ERROR_TIMEOUT)
			{
				PVR_DPF((CACHEOP_DPFL, "%s: wait timeout", __FUNCTION__));
#if defined(CACHEOP_DEBUG)
				/* This is a more accurate notion of fence check retries */
				ui32RetryCount += 1;
#endif
			}
			else if (eError == PVRSRV_OK)
			{
				PVR_DPF((CACHEOP_DPFL, "%s: wait OK, signal received", __FUNCTION__));
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: wait error %d", __FUNCTION__, eError));
			}

			/* (Re)read latest completed CacheOp sequence number to fence */
			ui32CompletedOpSeqNum = OSAtomicRead(&ghCompletedCacheOpSeqNum);

			/* Check if the CacheOp dependencies for this thread are met */
			eError = CacheOpFenceCheck(ui32CompletedOpSeqNum, ui32FenceOpSeqNum) ?
								PVRSRV_OK : PVRSRV_ERROR_FAILED_DEPENDENCIES;
		}

#if defined(CACHEOP_DEBUG)
		uiTimeNow = OSClockns64();
#endif

		eError = OSEventObjectClose(hOSEvent);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectClose");
	}

e0:
#if defined(CACHEOP_DEBUG)
	if (b1stCacheOpFenceCheckPass == IMG_FALSE)
	{
		/* This log gives an indication of how badly deferred
		   cache maintenance is doing and provides data for
		   possible dynamic spawning of multiple CacheOpThreads;
		   currently not implemented in the framework but such
		   an extension would require a monitoring thread to
		   scan the gasCacheOpStatStalled table and spawn/kill a 
		   new CacheOpThread if certain conditions are met */
		CacheOpStatStallLogWrite(ui32FenceOpSeqNum,
								 sCacheOpWorkItem.ui64QueuedTime,
								 uiTimeNow,
								 ui32RetryCount);
	}
#endif
#else /* defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED) */
#if defined(CACHEOP_DEBUG)
	PVR_UNREFERENCED_PARAMETER(ui32RetryCount);
#endif
	/* Fence checks _cannot_ fail in immediate RBF */
	PVR_UNREFERENCED_PARAMETER(b1stCacheOpFenceCheckPass);
	PVR_ASSERT(b1stCacheOpFenceCheckPass == IMG_TRUE);
#endif

#if defined(CACHEOP_DEBUG)
	sCacheOpWorkItem.ui64ExecuteTime = uiTimeNow;
	sCacheOpWorkItem.uiCacheOp = PVRSRV_CACHE_OP_NONE;
	eError = CacheOpStatExecLog(&sCacheOpWorkItem.sNode);
#endif

	return eError;
}

PVRSRV_ERROR CacheOpSetTimeline (IMG_INT32 i32Timeline)
{
	PVRSRV_ERROR eError;

#if defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED)
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	CACHEOP_WORK_ITEM *psCacheOpWorkItem;
	IMG_UINT32 ui32OpSeqNum;

	if (i32Timeline < 0)
	{
		return PVRSRV_OK;
	}

	psCacheOpWorkItem = OSAllocMem(sizeof(CACHEOP_WORK_ITEM));
	if (psCacheOpWorkItem == NULL)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* Prepare & enqueue a timeline CacheOp work item */
	psCacheOpWorkItem->psPMR = NULL;
#if defined(CACHEOP_DEBUG)
	psCacheOpWorkItem->ui64QueuedTime = OSClockns64();
	psCacheOpWorkItem->pid = OSGetCurrentClientProcessIDKM();
#endif

#if defined(CONFIG_SW_SYNC)
	psCacheOpWorkItem->psTimeline = fget(i32Timeline);
	if (!psCacheOpWorkItem->psTimeline || 
		!psCacheOpWorkItem->psTimeline->private_data)
	{
		OSFreeMem(psCacheOpWorkItem);
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Enqueue timeline work-item, notifies timeline FD when executed */
	eError = CacheOpEnqueue(psPVRSRVData, psCacheOpWorkItem, &ui32OpSeqNum);
	PVR_LOG_IF_ERROR(eError, "CacheOpEnqueue");

	/* Signal the CacheOp thread to ensure this item gets processed */
	eError = OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
	PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
#else
	PVR_UNREFERENCED_PARAMETER(psPVRSRVData);
	PVR_UNREFERENCED_PARAMETER(ui32OpSeqNum);
	eError = PVRSRV_ERROR_NOT_SUPPORTED;
	PVR_ASSERT(0);
#endif
#else /* defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED) */
	struct file *psFile;

	if (i32Timeline < 0)
	{
		return PVRSRV_OK;
	}

#if defined(CONFIG_SW_SYNC)
	psFile = fget(i32Timeline);
	if (!psFile || !psFile->private_data)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	sw_sync_timeline_inc(psFile->private_data, 1);
	fput(psFile);

	eError = PVRSRV_OK;
#else
	PVR_UNREFERENCED_PARAMETER(psFile);
	eError = PVRSRV_ERROR_NOT_SUPPORTED;
	PVR_ASSERT(0);
#endif
#endif

	return eError;
}
#else /* defined(SUPPORT_RANGEBASED_CACHEFLUSH) */
PVRSRV_ERROR CacheOpQueue (IMG_UINT32 ui32NumCacheOps,
						   PMR **ppsPMR,
						   IMG_DEVMEM_OFFSET_T *puiOffset,
						   IMG_DEVMEM_SIZE_T *puiSize,
						   PVRSRV_CACHE_OP *puiCacheOp,
						   IMG_UINT32 *pui32OpSeqNum)
{
	IMG_UINT32 ui32Idx;
	PVRSRV_ERROR eError = PVRSRV_OK;
	IMG_BOOL bHasInvalidate = IMG_FALSE;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	PVRSRV_CACHE_OP uiCacheOp = PVRSRV_CACHE_OP_NONE;
#if	defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem;
	dllist_init(&sCacheOpWorkItem.sNode);

	sCacheOpWorkItem.psPMR = ppsPMR[0];
	sCacheOpWorkItem.bRBF = IMG_FALSE;
	sCacheOpWorkItem.bUMF = IMG_FALSE;
	sCacheOpWorkItem.psTimeline = NULL;
	sCacheOpWorkItem.uiOffset = puiOffset[0];
	sCacheOpWorkItem.ui64QueuedTime = (IMG_UINT64)0;
	sCacheOpWorkItem.ui64ExecuteTime = (IMG_UINT64)0;
	sCacheOpWorkItem.uiSize = (IMG_DEVMEM_OFFSET_T)0;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
#endif

	/* Coalesce all requests into a single superset request */
	for (ui32Idx = 0; ui32Idx < ui32NumCacheOps; ui32Idx++)
	{
		uiCacheOp = SetCacheOp(uiCacheOp, puiCacheOp[ui32Idx]);
		if (puiCacheOp[ui32Idx] & PVRSRV_CACHE_OP_INVALIDATE)
		{
			/* Cannot be deferred, action now */
			bHasInvalidate = IMG_TRUE;
#if	!defined(CACHEOP_DEBUG)
			break;
#endif
		}
#if	defined(CACHEOP_DEBUG)
		/* For debug, we _want_ to know how many items are in batch */
		sCacheOpWorkItem.uiSize += puiSize[ui32Idx];
		*pui32OpSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
		*pui32OpSeqNum = !*pui32OpSeqNum ?
			OSAtomicIncrement(&ghCommonCacheOpSeqNum) : *pui32OpSeqNum;
#endif
	}

#if	!defined(CACHEOP_DEBUG)
	/* For release, we don't care, so use per-batch sequencing */
	*pui32OpSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
	*pui32OpSeqNum = !*pui32OpSeqNum ?
			OSAtomicIncrement(&ghCommonCacheOpSeqNum) : *pui32OpSeqNum;
#endif

	if (bHasInvalidate == IMG_TRUE)
	{
		psPVRSRVData->uiCacheOp = PVRSRV_CACHE_OP_NONE;

#if	defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.ui64QueuedTime = OSClockns64();
#endif

		/* Perform global cache maintenance operation */
		eError = OSCPUOperation(PVRSRV_CACHE_OP_FLUSH);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: OSCPUOperation failed (%u)",
					__FUNCTION__, eError));
			goto e0;
		}

#if	defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.ui64ExecuteTime = OSClockns64();
#endif

		/* Having completed the invalidate, note sequence number */
		OSAtomicWrite(&ghCompletedCacheOpSeqNum, *pui32OpSeqNum);
	}
	else
	{
		/* NOTE: Possible race condition, CacheOp value set here using SetCacheOp()
		   might be over-written during read-modify-write sequence in CacheOpFence() */
		psPVRSRVData->uiCacheOp = SetCacheOp(psPVRSRVData->uiCacheOp, uiCacheOp);
	}

#if	defined(CACHEOP_DEBUG)
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.ui32OpSeqNum = *pui32OpSeqNum;
	eError = CacheOpStatExecLog(&sCacheOpWorkItem.sNode);
#endif

e0:
	return eError;
}

PVRSRV_ERROR CacheOpFence (RGXFWIF_DM eFenceOpType,
						   IMG_UINT32 ui32FenceOpSeqNum)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();
	IMG_BOOL b1stCacheOpFenceCheckPass;
	IMG_UINT32 ui32CacheOpSeqNum;
	PVRSRV_CACHE_OP uiCacheOp;
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem;
	sCacheOpWorkItem.ui64QueuedTime = (IMG_UINT64)0;
	sCacheOpWorkItem.ui64ExecuteTime = (IMG_UINT64)0;
	sCacheOpWorkItem.uiCacheOp = PVRSRV_CACHE_OP_NONE;
#endif

	ui32CacheOpSeqNum = OSAtomicRead(&ghCompletedCacheOpSeqNum);
	b1stCacheOpFenceCheckPass = CacheOpFenceCheck(ui32CacheOpSeqNum, ui32FenceOpSeqNum);

	/* Flush if there is pending CacheOp that affects this fence */
	if (b1stCacheOpFenceCheckPass == IMG_FALSE)
	{
		/* After global CacheOp, requests before this sequence are met */
		ui32CacheOpSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
		ui32CacheOpSeqNum = !ui32CacheOpSeqNum ?
				OSAtomicIncrement(&ghCommonCacheOpSeqNum) : ui32CacheOpSeqNum;

		uiCacheOp = psPVRSRVData->uiCacheOp;
		psPVRSRVData->uiCacheOp = PVRSRV_CACHE_OP_NONE;

#if	defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.ui64QueuedTime = OSClockns64();
#endif

		/* Perform global cache maintenance operation */
		eError = OSCPUOperation(uiCacheOp);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: OSCPUOperation failed (%u)",
					__FUNCTION__, eError));
			goto e0;
		}

#if	defined(CACHEOP_DEBUG)
		sCacheOpWorkItem.ui64ExecuteTime = OSClockns64();
		sCacheOpWorkItem.uiCacheOp = uiCacheOp;
#endif

		/* Having completed global CacheOp, note sequence number */
		OSAtomicWrite(&ghCompletedCacheOpSeqNum, ui32CacheOpSeqNum);
	}

#if defined(CACHEOP_DEBUG)
	dllist_init(&sCacheOpWorkItem.sNode);

	sCacheOpWorkItem.psPMR = NULL;
	sCacheOpWorkItem.psTimeline = NULL;
	sCacheOpWorkItem.ui32OpSeqNum = ui32FenceOpSeqNum;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
#if defined(PVR_RI_DEBUG)
	sCacheOpWorkItem.eFenceOpType = eFenceOpType;
#endif

	eError = CacheOpStatExecLog(&sCacheOpWorkItem.sNode);
#endif

e0:
	return eError;
}

PVRSRV_ERROR CacheOpSetTimeline (IMG_INT32 i32Timeline)
{
	PVRSRV_ERROR eError;
	struct file *psFile;
	PVRSRV_CACHE_OP uiCacheOp;
	PVRSRV_DATA *psPVRSRVData;

	if (i32Timeline < 0)
	{
		return PVRSRV_OK;
	}

#if defined(CONFIG_SW_SYNC)
	psFile = fget(i32Timeline);
	if (!psFile || !psFile->private_data)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psPVRSRVData = PVRSRVGetPVRSRVData();
	uiCacheOp = psPVRSRVData->uiCacheOp;
	psPVRSRVData->uiCacheOp = PVRSRV_CACHE_OP_NONE;

	/* Perform global cache maintenance operation */
	eError = OSCPUOperation(uiCacheOp);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: OSCPUOperation failed (%u)",
				__FUNCTION__, eError));
		goto e0;
	}

	sw_sync_timeline_inc(psFile->private_data, 1);
	fput(psFile);
e0:
#else
	PVR_UNREFERENCED_PARAMETER(psFile);
	PVR_UNREFERENCED_PARAMETER(uiCacheOp);
	PVR_UNREFERENCED_PARAMETER(psPVRSRVData);
	eError = PVRSRV_ERROR_NOT_SUPPORTED;
	PVR_ASSERT(0);
#endif

	return eError;
}
#endif /* defined(SUPPORT_RANGEBASED_CACHEFLUSH) */

PVRSRV_ERROR CacheOpExec (PMR *psPMR,
						  IMG_DEVMEM_OFFSET_T uiOffset,
						  IMG_DEVMEM_SIZE_T uiSize,
						  PVRSRV_CACHE_OP uiCacheOp)
{
	PVRSRV_ERROR eError;
	IMG_DEVMEM_SIZE_T uiLogicalSize;
	IMG_BOOL bUsedGlobalFlush = IMG_FALSE;
#if	defined(CACHEOP_DEBUG)
	/* This interface is always synchronous and not deferred;
	   during debug build, use work-item to capture debug logs */
	CACHEOP_WORK_ITEM sCacheOpWorkItem;
	dllist_init(&sCacheOpWorkItem.sNode);

	sCacheOpWorkItem.psPMR = psPMR;
	sCacheOpWorkItem.uiSize = uiSize;
	sCacheOpWorkItem.psTimeline = NULL;
	sCacheOpWorkItem.uiOffset = uiOffset;
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.ui64QueuedTime = OSClockns64();
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
	sCacheOpWorkItem.ui32OpSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
	sCacheOpWorkItem.ui32OpSeqNum = !sCacheOpWorkItem.ui32OpSeqNum ?
		OSAtomicIncrement(&ghCommonCacheOpSeqNum) : sCacheOpWorkItem.ui32OpSeqNum;
#endif

	eError = PMR_LogicalSize(psPMR, &uiLogicalSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((CACHEOP_DPFL,
				"%s: PMR_LogicalSize failed (%u)",
				__FUNCTION__, eError));
		goto e0;
	}
	else if ((uiOffset+uiSize) > uiLogicalSize)
	{
		PVR_DPF((CACHEOP_DPFL,
				"%s: Invalid parameters",
				__FUNCTION__));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto e0;
	}

	/* Perform range-based cache maintenance operation */
	eError = PMRLockSysPhysAddresses(psPMR);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((CACHEOP_DPFL,
				"%s: PMRLockSysPhysAddresses failed (%u)",
				__FUNCTION__, eError));
		goto e0;
	}

	eError = CacheOpRangeBased(psPMR, uiOffset, uiSize, uiCacheOp, &bUsedGlobalFlush);
#if	defined(CACHEOP_DEBUG)
	sCacheOpWorkItem.bUMF = IMG_FALSE;
	sCacheOpWorkItem.bRBF = !bUsedGlobalFlush;
	sCacheOpWorkItem.ui64ExecuteTime = OSClockns64();
	eError = CacheOpStatExecLog(&sCacheOpWorkItem.sNode);
#endif

	PMRUnlockSysPhysAddresses(psPMR);
e0:
	return eError;
}

PVRSRV_ERROR CacheOpLog (PMR *psPMR,
						 IMG_DEVMEM_OFFSET_T uiOffset,
						 IMG_DEVMEM_SIZE_T uiSize,
						 IMG_UINT64 ui64QueuedTimeUs,
						 IMG_UINT64 ui64ExecuteTimeUs,
						 PVRSRV_CACHE_OP uiCacheOp)
{
#if defined(CACHEOP_DEBUG)
	CACHEOP_WORK_ITEM sCacheOpWorkItem;
	dllist_init(&sCacheOpWorkItem.sNode);

	sCacheOpWorkItem.psPMR = psPMR;
	sCacheOpWorkItem.uiSize = uiSize;
	sCacheOpWorkItem.psTimeline = NULL;
	sCacheOpWorkItem.uiOffset = uiOffset;
	sCacheOpWorkItem.uiCacheOp = uiCacheOp;
	sCacheOpWorkItem.pid = OSGetCurrentClientProcessIDKM();
	sCacheOpWorkItem.ui32OpSeqNum = OSAtomicIncrement(&ghCommonCacheOpSeqNum);
	sCacheOpWorkItem.ui32OpSeqNum = !sCacheOpWorkItem.ui32OpSeqNum ?
		OSAtomicIncrement(&ghCommonCacheOpSeqNum) : sCacheOpWorkItem.ui32OpSeqNum;

	/* All UM cache maintenance is range-based */
	sCacheOpWorkItem.ui64ExecuteTime = ui64ExecuteTimeUs;
	sCacheOpWorkItem.ui64QueuedTime = ui64QueuedTimeUs;
	sCacheOpWorkItem.bUMF = IMG_TRUE;
	sCacheOpWorkItem.bRBF = IMG_TRUE;

	CacheOpStatExecLogWrite(&sCacheOpWorkItem.sNode);
#else /* defined(CACHEOP_DEBUG) */
	PVR_UNREFERENCED_PARAMETER(psPMR);
	PVR_UNREFERENCED_PARAMETER(uiSize);
	PVR_UNREFERENCED_PARAMETER(uiOffset);
	PVR_UNREFERENCED_PARAMETER(ui64QueuedTimeUs);
	PVR_UNREFERENCED_PARAMETER(ui64ExecuteTimeUs);
	PVR_UNREFERENCED_PARAMETER(uiCacheOp);
#endif
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpGetLineSize (IMG_UINT32 *pui32L1DataCacheLineSize)
{
	*pui32L1DataCacheLineSize = guiCacheLineSize;
	PVR_ASSERT(guiCacheLineSize != 0);
	return PVRSRV_OK;
}

PVRSRV_ERROR CacheOpInit (void)
{
	PVRSRV_ERROR eError;
	PVRSRV_DATA *psPVRSRVData;

	/* DDK initialisation is anticipated to be performed on the boot
	   processor (little core in big/little systems) though this may
	   not always be the case. If so, the value cached here is the 
	   system wide safe (i.e. smallest) L1 d-cache line size value 
	   on platforms with mismatched d-cache line sizes */
	guiCacheLineSize = OSCPUCacheAttributeSize(PVR_DCACHE_LINE_SIZE);
	PVR_ASSERT(guiCacheLineSize != 0);

	guiOSPageSize = OSGetPageSize();
	PVR_ASSERT(guiOSPageSize != 0);

	OSAtomicWrite(&ghCommonCacheOpSeqNum, 0);
	OSAtomicWrite(&ghCompletedCacheOpSeqNum, 0);

#if defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED)
	psPVRSRVData = PVRSRVGetPVRSRVData();

	/* Create an event object for pending CacheOp work items */
	eError = OSEventObjectCreate("PVRSRV_CACHEOP_EVENTOBJECT", &psPVRSRVData->hCacheOpThreadEventObject);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Create an event object for updating pending fence checks on CacheOp */
	eError = OSEventObjectCreate("PVRSRV_CACHEOP_EVENTOBJECT", &psPVRSRVData->hCacheOpUpdateEventObject);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Create a lock to police list of pending CacheOp work items */
	eError = OSLockCreate((POS_LOCK*)&psPVRSRVData->hCacheOpThreadWorkListLock, LOCK_TYPE_PASSIVE);
	PVR_ASSERT(eError == PVRSRV_OK);

	/* Initialise pending CacheOp list & seq number */
	dllist_init(&psPVRSRVData->sCacheOpThreadWorkList);
	guiPendingDevmemSize = (IMG_DEVMEM_SIZE_T) 0;
	gbPendingTimeline = IMG_FALSE;

#if defined(CACHEOP_DEBUG)
	gi32CacheOpStatExecWriteIdx = 0;
	gi32CacheOpStatStallWriteIdx = 0;

	OSCachedMemSet(gasCacheOpStatExecuted, 0, sizeof(gasCacheOpStatExecuted));
	OSCachedMemSet(gasCacheOpStatStalled, 0, sizeof(gasCacheOpStatStalled));

	eError = OSLockCreate((POS_LOCK*)&ghCacheOpStatExecLock, LOCK_TYPE_PASSIVE);
	PVR_ASSERT(eError == PVRSRV_OK);

	eError = OSLockCreate((POS_LOCK*)&ghCacheOpStatStallLock, LOCK_TYPE_PASSIVE);
	PVR_ASSERT(eError == PVRSRV_OK);

	pvCacheOpStatExecEntry = OSCreateStatisticEntry("cache_ops_exec",
												NULL,
												CacheOpStatExecLogRead,
												NULL,
												NULL,
												NULL);
	PVR_ASSERT(pvCacheOpStatExecEntry != NULL);

	pvCacheOpStatStallEntry = OSCreateStatisticEntry("cache_ops_stall",
												NULL,
												CacheOpStatStallLogRead,
												NULL,
												NULL,
												NULL);
	PVR_ASSERT(pvCacheOpStatStallEntry != NULL);
#endif

	/* Create a thread which is used to do the deferred CacheOp */
	eError = OSThreadCreatePriority(&psPVRSRVData->hCacheOpThread,
							"pvr_cache_ops",
							CacheOpThread, 
							psPVRSRVData,
							OS_THREAD_HIGHEST_PRIORITY);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"CacheOpInit: failed to create CacheOp thread"));
		return CacheOpDeInit();
	}
#else /* defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED) */
	PVR_UNREFERENCED_PARAMETER(psPVRSRVData);
#if defined(CACHEOP_DEBUG)
	gi32CacheOpStatExecWriteIdx = 0;

	OSCachedMemSet(gasCacheOpStatExecuted, 0, sizeof(gasCacheOpStatExecuted));

	eError = OSLockCreate((POS_LOCK*)&ghCacheOpStatExecLock, LOCK_TYPE_PASSIVE);
	PVR_ASSERT(eError == PVRSRV_OK);

	pvCacheOpStatExecEntry = OSCreateStatisticEntry("cache_ops_exec",
												NULL,
												CacheOpStatExecLogRead,
												NULL,
												NULL,
												NULL);
	PVR_ASSERT(pvCacheOpStatExecEntry != NULL);
#endif
	eError = PVRSRV_OK;
#endif

	return eError;
}

PVRSRV_ERROR CacheOpDeInit (void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVRSRV_DATA *psPVRSRVData;

#if defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED)
	psPVRSRVData = PVRSRVGetPVRSRVData();

#if defined(CACHEOP_DEBUG)
	OSLockDestroy(ghCacheOpStatExecLock);
	OSRemoveStatisticEntry(pvCacheOpStatExecEntry);
	OSRemoveStatisticEntry(pvCacheOpStatStallEntry);
#endif

	if (psPVRSRVData->hCacheOpThread)
	{
		if (psPVRSRVData->hCacheOpThreadEventObject)
		{
			eError = OSEventObjectSignal(psPVRSRVData->hCacheOpThreadEventObject);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}

		if (psPVRSRVData->hCacheOpUpdateEventObject)
		{
			eError = OSEventObjectSignal(psPVRSRVData->hCacheOpUpdateEventObject);
			PVR_LOG_IF_ERROR(eError, "OSEventObjectSignal");
		}

		LOOP_UNTIL_TIMEOUT(OS_THREAD_DESTROY_TIMEOUT_US)
		{
			eError = OSThreadDestroy(psPVRSRVData->hCacheOpThread);
			if (PVRSRV_OK == eError)
			{
				psPVRSRVData->hCacheOpThread = NULL;
				break;
			}
			OSWaitus(OS_THREAD_DESTROY_TIMEOUT_US/OS_THREAD_DESTROY_RETRY_COUNT);
		} END_LOOP_UNTIL_TIMEOUT();
		PVR_LOG_IF_ERROR(eError, "OSThreadDestroy");
	}

	OSLockDestroy(psPVRSRVData->hCacheOpThreadWorkListLock);
	psPVRSRVData->hCacheOpThreadWorkListLock = NULL;

	if (psPVRSRVData->hCacheOpUpdateEventObject)
	{
		eError = OSEventObjectDestroy(psPVRSRVData->hCacheOpUpdateEventObject);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
		psPVRSRVData->hCacheOpUpdateEventObject = NULL;
	}

	if (psPVRSRVData->hCacheOpThreadEventObject)
	{
		eError = OSEventObjectDestroy(psPVRSRVData->hCacheOpThreadEventObject);
		PVR_LOG_IF_ERROR(eError, "OSEventObjectDestroy");
		psPVRSRVData->hCacheOpThreadEventObject = NULL;
	}

	eError = PVRSRV_OK;
#else /* defined(SUPPORT_RANGEBASED_CACHEFLUSH_DEFERRED) */
	PVR_UNREFERENCED_PARAMETER(psPVRSRVData);
#if defined(CACHEOP_DEBUG)
	OSLockDestroy(ghCacheOpStatExecLock);
	OSRemoveStatisticEntry(pvCacheOpStatExecEntry);
#endif
	eError = PVRSRV_OK;
#endif

	return eError;
}
