/*************************************************************************/ /*!
@File
@Title          arm specific OS functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    OS functions who's implementation are processor specific
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
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0))
 #include <asm/system.h>
#endif
#include <asm/cacheflush.h>

#include "pvrsrv_error.h"
#include "img_types.h"
#include "osfunc.h"
#include "pvr_debug.h"



#if defined(CONFIG_OUTER_CACHE)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0))

	/* Since 3.16 the outer_xxx() functions require irqs to be disabled and no
	 * other cache masters must operate on the outer cache. */
	static DEFINE_SPINLOCK(gsCacheFlushLock);

	#define OUTER_CLEAN_RANGE() { \
		unsigned long uiLockFlags; \
		\
		spin_lock_irqsave(&gsCacheFlushLock, uiLockFlags); \
		outer_clean_range(0, ULONG_MAX); \
		spin_unlock_irqrestore(&gsCacheFlushLock, uiLockFlags); \
	}

	#define OUTER_FLUSH_ALL() { \
		unsigned long uiLockFlags; \
		\
		spin_lock_irqsave(&gsCacheFlushLock, uiLockFlags); \
		outer_flush_all(); \
		spin_unlock_irqrestore(&gsCacheFlushLock, uiLockFlags); \
	}

#else /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)) */

	/* No need to disable IRQs for older kernels */
	#define OUTER_CLEAN_RANGE() outer_clean_range(0, ULONG_MAX)
	#define OUTER_FLUSH_ALL()   outer_flush_all()
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)) */

#else /* CONFIG_OUTER_CACHE */

	/* Don't do anything if we have no outer cache */
	#define OUTER_CLEAN_RANGE()
	#define OUTER_FLUSH_ALL()
#endif /* CONFIG_OUTER_CACHE */

static void per_cpu_cache_flush(void *arg)
{
	PVR_UNREFERENCED_PARAMETER(arg);
	flush_cache_all();
}

PVRSRV_ERROR OSCPUOperation(PVRSRV_CACHE_OP uiCacheOp)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	switch(uiCacheOp)
	{
		/* Fall-through */
		case PVRSRV_CACHE_OP_CLEAN:
			on_each_cpu(per_cpu_cache_flush, NULL, 1);
			OUTER_CLEAN_RANGE();
			break;

		case PVRSRV_CACHE_OP_INVALIDATE:
		case PVRSRV_CACHE_OP_FLUSH:
			on_each_cpu(per_cpu_cache_flush, NULL, 1);
			OUTER_FLUSH_ALL();
			break;

		case PVRSRV_CACHE_OP_NONE:
			break;

		default:
			PVR_DPF((PVR_DBG_ERROR,
					"%s: Global cache operation type %d is invalid",
					__FUNCTION__, uiCacheOp));
			eError = PVRSRV_ERROR_INVALID_PARAMS;
			PVR_ASSERT(0);
			break;
	}

	return eError;
}

static inline size_t pvr_dmac_range_len(const void *pvStart, const void *pvEnd)
{
	return (size_t)((char *)pvEnd - (char *)pvStart);
}

void OSFlushCPUCacheRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	arm_dma_ops.sync_single_for_device(NULL, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_TO_DEVICE);
	arm_dma_ops.sync_single_for_cpu(NULL, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_FROM_DEVICE);
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
	/* Inner cache */
	dmac_flush_range(pvVirtStart, pvVirtEnd);

	/* Outer cache */
	outer_flush_range(sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr);
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
}

void OSCleanCPUCacheRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	arm_dma_ops.sync_single_for_device(NULL, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_TO_DEVICE);
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
	/* Inner cache */
	dmac_map_area(pvVirtStart, pvr_dmac_range_len(pvVirtStart, pvVirtEnd), DMA_TO_DEVICE);

	/* Outer cache */
	outer_clean_range(sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr);
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
}

void OSInvalidateCPUCacheRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                                 void *pvVirtStart,
                                 void *pvVirtEnd,
                                 IMG_CPU_PHYADDR sCPUPhysStart,
                                 IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	arm_dma_ops.sync_single_for_cpu(NULL, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_FROM_DEVICE);
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
#if defined(PVR_LINUX_DONT_USE_RANGE_BASED_INVALIDATE)
	OSCleanCPUCacheRangeKM(psDevNode, pvVirtStart, pvVirtEnd, sCPUPhysStart, sCPUPhysEnd);
#else
	/* Inner cache */
	dmac_map_area(pvVirtStart, pvr_dmac_range_len(pvVirtStart, pvVirtEnd), DMA_FROM_DEVICE);

	/* Outer cache */
	outer_inv_range(sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr);
#endif
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
}

PVRSRV_CACHE_OP_ADDR_TYPE OSCPUCacheOpAddressType(PVRSRV_CACHE_OP uiCacheOp)
{
	PVR_UNREFERENCED_PARAMETER(uiCacheOp);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	return PVRSRV_CACHE_OP_ADDR_TYPE_PHYSICAL;
#else
	return PVRSRV_CACHE_OP_ADDR_TYPE_BOTH;
#endif
}

/* User Enable Register */
#define PMUSERENR_EN      0x00000001 /* enable user access to the counters */

static void per_cpu_perf_counter_user_access_en(void *data)
{
	PVR_UNREFERENCED_PARAMETER(data);
#if !defined(CONFIG_L4)
	/* Enable user-mode access to counters. */
	asm volatile("mcr p15, 0, %0, c9, c14, 0" :: "r"(PMUSERENR_EN));
#endif
}

void OSUserModeAccessToPerfCountersEn(void)
{
	on_each_cpu(per_cpu_perf_counter_user_access_en, NULL, 1);
}
