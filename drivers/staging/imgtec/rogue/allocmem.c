/*************************************************************************/ /*!
@File
@Title          Host memory management implementation for Linux
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
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

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/string.h>

#include "img_defs.h"
#include "allocmem.h"
#include "pvr_debug.h"
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
#include "process_stats.h"
#endif
#include "osfunc.h"

#if defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#define ALLOCMEM_MEMSTATS_PADDING 0
#else
#define ALLOCMEM_MEMSTATS_PADDING sizeof(IMG_UINT32)
#endif

/* Ensure poison value is not divisible by 4.
 * Used to poison memory to trip up use after free in kernel-side code
 */
#define OS_MEM_POISON_VALUE (0x6b)

static inline void _pvr_vfree(const void* pvAddr)
{
#if defined(DEBUG)
			/* Size harder to come by for vmalloc and since vmalloc allocates
			 * a whole number of pages, poison the minimum size known to have
			 * been allocated.
			 */
			OSCachedMemSet((void*)pvAddr, OS_MEM_POISON_VALUE, PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD);
#endif
			vfree(pvAddr);
}

static inline void _pvr_kfree(const void* pvAddr)
{
#if defined(DEBUG)
			/* Poison whole memory block */
			OSCachedMemSet((void*)pvAddr, OS_MEM_POISON_VALUE, ksize(pvAddr));
#endif
			kfree(pvAddr);
}

#if !defined(PVRSRV_ENABLE_PROCESS_STATS)
IMG_INTERNAL void *OSAllocMem(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if (ui32Size > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vmalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		pvRet = kmalloc(ui32Size, GFP_KERNEL);
	}

	return pvRet;
}

IMG_INTERNAL void *OSAllocZMem(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if (ui32Size > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vzalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		pvRet = kzalloc(ui32Size, GFP_KERNEL);
	}

	return pvRet;
}

/*
 * The parentheses around OSFreeMem prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
IMG_INTERNAL void (OSFreeMem)(void *pvMem)
{
	if (pvMem != NULL)
	{
		if (!is_vmalloc_addr(pvMem))
		{
			_pvr_kfree(pvMem);
		}
		else
		{
			_pvr_vfree(pvMem);
		}
	}
}
#else
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS) && defined(DEBUG) && defined(PVRSRV_ENABLE_MEMORY_STATS)
IMG_INTERNAL void *_OSAllocMem(IMG_UINT32 ui32Size, void *pvAllocFromFile, IMG_UINT32 ui32AllocFromLine)
{
	void *pvRet = NULL;

	if (ui32Size > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vmalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		pvRet = kmalloc(ui32Size, GFP_KERNEL);
	}

	if (pvRet != NULL)
	{

		if (!is_vmalloc_addr(pvRet))
		{
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			_PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
										  pvRet,
										  sCpuPAddr,
										  ksize(pvRet),
										  NULL,
										  pvAllocFromFile,
										  ui32AllocFromLine);
		}
		else
		{
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			_PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
										  pvRet,
										  sCpuPAddr,
										  ((ui32Size + PAGE_SIZE -1) & ~(PAGE_SIZE-1)),
										  NULL,
										  pvAllocFromFile,
										  ui32AllocFromLine);
		}
	}
	return pvRet;
}

IMG_INTERNAL void *_OSAllocZMem(IMG_UINT32 ui32Size, void *pvAllocFromFile, IMG_UINT32 ui32AllocFromLine)
{
	void *pvRet = NULL;

	if (ui32Size > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vzalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		pvRet = kzalloc(ui32Size, GFP_KERNEL);
	}

	if (pvRet != NULL)
	{
		if (!is_vmalloc_addr(pvRet))
		{
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			_PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
										  pvRet,
										  sCpuPAddr,
										  ksize(pvRet),
										  NULL,
										  pvAllocFromFile,
										  ui32AllocFromLine);
		}
		else
		{
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			_PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
										  pvRet,
										  sCpuPAddr,
										  ((ui32Size + PAGE_SIZE -1) & ~(PAGE_SIZE-1)),
										  NULL,
										  pvAllocFromFile,
										  ui32AllocFromLine);
		}
	}
	return pvRet;
}
#else
IMG_INTERNAL void *OSAllocMem(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if ((ui32Size + ALLOCMEM_MEMSTATS_PADDING) > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vmalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		/* Allocate an additional 4 bytes to store the PID of the allocating process */
		pvRet = kmalloc(ui32Size + ALLOCMEM_MEMSTATS_PADDING, GFP_KERNEL);
	}

	if (pvRet != NULL)
	{

		if (!is_vmalloc_addr(pvRet))
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			{
				/* Store the PID in the final additional 4 bytes allocated */
				IMG_UINT32 *puiTemp = (IMG_UINT32*) (((IMG_BYTE*)pvRet) + (ksize(pvRet) - ALLOCMEM_MEMSTATS_PADDING));
				*puiTemp = OSGetCurrentProcessID();
			}
			PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC, ksize(pvRet));
#else
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
										 pvRet,
										 sCpuPAddr,
										 ksize(pvRet),
										 NULL);
#endif
#endif
		}
		else
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
											    ((ui32Size + PAGE_SIZE -1) & ~(PAGE_SIZE-1)),
											    (IMG_UINT64)(uintptr_t) pvRet);
#else
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
										 pvRet,
										 sCpuPAddr,
										 ((ui32Size + PAGE_SIZE -1) & ~(PAGE_SIZE-1)),
										 NULL);
#endif
#endif
		}
	}
	return pvRet;
}

IMG_INTERNAL void *OSAllocZMem(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if ((ui32Size + ALLOCMEM_MEMSTATS_PADDING) > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vzalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		/* Allocate an additional 4 bytes to store the PID of the allocating process */
		pvRet = kzalloc(ui32Size + ALLOCMEM_MEMSTATS_PADDING, GFP_KERNEL);
	}

	if (pvRet != NULL)
	{
		if (!is_vmalloc_addr(pvRet))
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			{
				/* Store the PID in the final additional 4 bytes allocated */
				IMG_UINT32 *puiTemp = (IMG_UINT32*) (((IMG_BYTE*)pvRet) + (ksize(pvRet) - ALLOCMEM_MEMSTATS_PADDING));
				*puiTemp = OSGetCurrentProcessID();
			}
			PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC, ksize(pvRet));
#else
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
								 pvRet,
								 sCpuPAddr,
								 ksize(pvRet),
								 NULL);
#endif
#endif
		}
		else
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
											    ((ui32Size + PAGE_SIZE -1) & ~(PAGE_SIZE-1)),
											    (IMG_UINT64)(uintptr_t) pvRet);
#else
			IMG_CPU_PHYADDR sCpuPAddr;
			sCpuPAddr.uiAddr = 0;

			PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
										 pvRet,
										 sCpuPAddr,
										 ((ui32Size + PAGE_SIZE -1) & ~(PAGE_SIZE-1)),
										 NULL);
#endif
#endif
		}
	}
	return pvRet;
}
#endif

/*
 * The parentheses around OSFreeMem prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
IMG_INTERNAL void (OSFreeMem)(void *pvMem)
{
	if (pvMem != NULL)
	{
		if (!is_vmalloc_addr(pvMem))
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			PVRSRVStatsDecrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC, ksize(pvMem));
#else
			PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
			                                (IMG_UINT64)(uintptr_t) pvMem);
#endif
#endif
			_pvr_kfree(pvMem);
		}
		else
		{
#if !defined(PVR_DISABLE_KMALLOC_MEMSTATS)
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
			PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
			                                      (IMG_UINT64)(uintptr_t) pvMem);
#else
			PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
			                                (IMG_UINT64)(uintptr_t) pvMem);
#endif
#endif
			_pvr_vfree(pvMem);
		}
	}
}
#endif


IMG_INTERNAL void *OSAllocMemNoStats(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if (ui32Size > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vmalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		pvRet = kmalloc(ui32Size, GFP_KERNEL);
	}

	return pvRet;
}

IMG_INTERNAL void *OSAllocZMemNoStats(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if (ui32Size > PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD)
	{
		pvRet = vzalloc(ui32Size);
	}
	if (pvRet == NULL)
	{
		pvRet = kzalloc(ui32Size, GFP_KERNEL);
	}

	return pvRet;
}

/*
 * The parentheses around OSFreeMemNoStats prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
IMG_INTERNAL void (OSFreeMemNoStats)(void *pvMem)
{
	if (pvMem != NULL)
	{
		if ( !is_vmalloc_addr(pvMem) )
		{
			_pvr_kfree(pvMem);
		}
		else
		{
			_pvr_vfree(pvMem);
		}
	}
}
