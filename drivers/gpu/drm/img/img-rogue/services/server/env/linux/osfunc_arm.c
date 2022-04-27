/*************************************************************************/ /*!
@File
@Title          arm specific OS functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Processor specific OS functions
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
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0))
 #include <asm/system.h>
#endif
#include <asm/cacheflush.h>

#include "pvrsrv_error.h"
#include "img_types.h"
#include "img_defs.h"
#include "osfunc.h"
#include "pvr_debug.h"


static inline size_t pvr_dmac_range_len(const void *pvStart, const void *pvEnd)
{
	return (size_t)((char *)pvEnd - (char *)pvStart);
}

void OSCPUCacheFlushRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	arm_dma_ops.sync_single_for_device(psDevNode->psDevConfig->pvOSDevice, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_TO_DEVICE);
	arm_dma_ops.sync_single_for_cpu(psDevNode->psDevConfig->pvOSDevice, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_FROM_DEVICE);
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
	/* Inner cache */
	dmac_flush_range(pvVirtStart, pvVirtEnd);

	/* Outer cache */
	outer_flush_range(sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr);
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
}

void OSCPUCacheCleanRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                            void *pvVirtStart,
                            void *pvVirtEnd,
                            IMG_CPU_PHYADDR sCPUPhysStart,
                            IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	arm_dma_ops.sync_single_for_device(psDevNode->psDevConfig->pvOSDevice, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_TO_DEVICE);
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
	/* Inner cache */
	dmac_map_area(pvVirtStart, pvr_dmac_range_len(pvVirtStart, pvVirtEnd), DMA_TO_DEVICE);

	/* Outer cache */
	outer_clean_range(sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr);
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
}

void OSCPUCacheInvalidateRangeKM(PVRSRV_DEVICE_NODE *psDevNode,
                                 void *pvVirtStart,
                                 void *pvVirtEnd,
                                 IMG_CPU_PHYADDR sCPUPhysStart,
                                 IMG_CPU_PHYADDR sCPUPhysEnd)
{
	PVR_UNREFERENCED_PARAMETER(psDevNode);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	arm_dma_ops.sync_single_for_cpu(psDevNode->psDevConfig->pvOSDevice, sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr - sCPUPhysStart.uiAddr, DMA_FROM_DEVICE);
#else	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
	/* Inner cache */
	dmac_map_area(pvVirtStart, pvr_dmac_range_len(pvVirtStart, pvVirtEnd), DMA_FROM_DEVICE);

	/* Outer cache */
	outer_inv_range(sCPUPhysStart.uiAddr, sCPUPhysEnd.uiAddr);
#endif	/* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)) */
}

OS_CACHE_OP_ADDR_TYPE OSCPUCacheOpAddressType(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0))
	return OS_CACHE_OP_ADDR_TYPE_PHYSICAL;
#else
	return OS_CACHE_OP_ADDR_TYPE_BOTH;
#endif
}

/* User Enable Register */
#define PMUSERENR_EN      0x00000001 /* enable user access to the counters */

static void per_cpu_perf_counter_user_access_en(void *data)
{
	PVR_UNREFERENCED_PARAMETER(data);
	/* Enable user-mode access to counters. */
	asm volatile("mcr p15, 0, %0, c9, c14, 0" :: "r"(PMUSERENR_EN));
}

void OSUserModeAccessToPerfCountersEn(void)
{
	on_each_cpu(per_cpu_perf_counter_user_access_en, NULL, 1);
}

IMG_BOOL OSIsWriteCombineUnalignedSafe(void)
{
	/*
	 * The kernel looks to have always used normal memory under ARM32.
	 * See osfunc_arm64.c implementation for more details.
	 */
	return IMG_TRUE;
}
