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
#include "process_stats.h"
#if defined(DEBUG) && defined(SUPPORT_VALIDATION)
#include "pvrsrv.h"
#endif
#include "osfunc.h"

/*
 * DEBUG_MEMSTATS_ALLOC_RECORD_VALUES needs to be different from
 * DEBUG_MEMSTATS_VALUES defined in process_stats.h.
 * The reason for this is that the file and line where the allocation happens
 * are tracked from the OSAllocMem params. If DEBUG_MEMSTATS_VALUES were to be
 * used, all OSAllocMem allocation statistics would point to allocmem.c, which
 * is not expected behaviour.
 */
#if defined(PVRSRV_DEBUG_LINUX_MEMORY_STATS_ON)
#define DEBUG_MEMSTATS_ALLOC_RECORD_VALUES ,pvAllocFromFile, ui32AllocFromLine
#else
#define DEBUG_MEMSTATS_ALLOC_RECORD_VALUES
#endif

/*
 * When memory statistics are disabled, memory records are used instead.
 * In order for these to work, the PID of the process that requested the
 * allocation needs to be stored at the end of the kmalloc'd memory, making
 * sure 4 extra bytes are allocated to fit the PID.
 *
 * There is no need for this extra allocation when memory statistics are
 * enabled, since all allocations are tracked in DebugFS mem_area files.
 */
#if defined(PVRSRV_ENABLE_PROCESS_STATS) && !defined(PVRSRV_ENABLE_MEMORY_STATS)
#define ALLOCMEM_MEMSTATS_PADDING sizeof(IMG_UINT32)
#else
#define ALLOCMEM_MEMSTATS_PADDING 0UL
#endif

/* How many times kmalloc can fail before the allocation threshold is reduced */
static const IMG_UINT32 g_ui32kmallocFailLimit = 10;
/* How many kmalloc failures happened since the last allocation threshold change */
static IMG_UINT32 g_ui32kmallocFailCount = 0;
/* Current kmalloc threshold value in bytes */
static IMG_UINT32 g_ui32kmallocThreshold = PVR_LINUX_KMALLOC_ALLOCATION_THRESHOLD;
/* Spinlock used so that the global variables above may not be modified by more than 1 thread at a time */
static DEFINE_SPINLOCK(kmalloc_lock);

#if defined(DEBUG) && defined(SUPPORT_VALIDATION)
static DEFINE_SPINLOCK(kmalloc_leak_lock);
static IMG_UINT32 g_ui32kmallocLeakCounter = 0;
#endif

static inline void OSTryDecreaseKmallocThreshold(void)
{
	unsigned long flags;
	spin_lock_irqsave(&kmalloc_lock, flags);

	g_ui32kmallocFailCount++;

	if (g_ui32kmallocFailCount >= g_ui32kmallocFailLimit)
	{
		g_ui32kmallocFailCount = 0;
		if (g_ui32kmallocThreshold > PAGE_SIZE)
		{
			g_ui32kmallocThreshold >>= 1;
			printk(KERN_INFO "Threshold is now set to %d\n", g_ui32kmallocThreshold);
		}
	}

	spin_unlock_irqrestore(&kmalloc_lock, flags);
}

static inline void OSResetKmallocFailCount(void)
{
	unsigned long flags;
	spin_lock_irqsave(&kmalloc_lock, flags);

	g_ui32kmallocFailCount = 0;

	spin_unlock_irqrestore(&kmalloc_lock, flags);
}

static inline void _pvr_vfree(const void* pvAddr)
{
#if defined(DEBUG)
	/* Size harder to come by for vmalloc and since vmalloc allocates
	 * a whole number of pages, poison the minimum size known to have
	 * been allocated.
	 */
	OSCachedMemSet((void*)pvAddr, PVRSRV_POISON_ON_ALLOC_VALUE,
	               PAGE_SIZE);
#endif
	vfree(pvAddr);
}

static inline void _pvr_kfree(const void* pvAddr)
{
#if defined(DEBUG)
	/* Poison whole memory block */
	OSCachedMemSet((void*)pvAddr, PVRSRV_POISON_ON_ALLOC_VALUE,
	               ksize(pvAddr));
#endif
	kfree(pvAddr);
}

static inline void _pvr_alloc_stats_add(void *pvAddr, IMG_UINT32 ui32Size DEBUG_MEMSTATS_PARAMS)
{
#if !defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVR_UNREFERENCED_PARAMETER(pvAddr);
#else
	if (!is_vmalloc_addr(pvAddr))
	{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
									  pvAddr,
									  sCpuPAddr,
									  ksize(pvAddr),
									  NULL,
									  OSGetCurrentClientProcessIDKM()
									  DEBUG_MEMSTATS_ALLOC_RECORD_VALUES);
#else
		{
			/* Store the PID in the final additional 4 bytes allocated */
			IMG_UINT32 *puiTemp = IMG_OFFSET_ADDR(pvAddr, ksize(pvAddr) - ALLOCMEM_MEMSTATS_PADDING);
			*puiTemp = OSGetCurrentClientProcessIDKM();
		}
		PVRSRVStatsIncrMemAllocStat(PVRSRV_MEM_ALLOC_TYPE_KMALLOC, ksize(pvAddr), OSGetCurrentClientProcessIDKM());
#endif /* defined(PVRSRV_ENABLE_MEMORY_STATS) */
	}
	else
	{
#if defined(PVRSRV_ENABLE_MEMORY_STATS)
		IMG_CPU_PHYADDR sCpuPAddr;
		sCpuPAddr.uiAddr = 0;

		PVRSRVStatsAddMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
									  pvAddr,
									  sCpuPAddr,
									  ((ui32Size + PAGE_SIZE-1) & ~(PAGE_SIZE-1)),
									  NULL,
									  OSGetCurrentClientProcessIDKM()
									  DEBUG_MEMSTATS_ALLOC_RECORD_VALUES);
#else
		PVRSRVStatsIncrMemAllocStatAndTrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
		                                    ((ui32Size + PAGE_SIZE-1) & ~(PAGE_SIZE-1)),
		                                    (IMG_UINT64)(uintptr_t) pvAddr,
		                                    OSGetCurrentClientProcessIDKM());
#endif /* defined(PVRSRV_ENABLE_MEMORY_STATS) */
	}
#endif /* !defined(PVRSRV_ENABLE_PROCESS_STATS) */
}

static inline void _pvr_alloc_stats_remove(void *pvAddr)
{
#if !defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVR_UNREFERENCED_PARAMETER(pvAddr);
#else
	if (!is_vmalloc_addr(pvAddr))
	{
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		{
			IMG_UINT32 *puiTemp = IMG_OFFSET_ADDR(pvAddr, ksize(pvAddr) - ALLOCMEM_MEMSTATS_PADDING);
			PVRSRVStatsDecrMemKAllocStat(ksize(pvAddr), *puiTemp);
		}
#else
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_KMALLOC,
		                                (IMG_UINT64)(uintptr_t) pvAddr,
		                                OSGetCurrentClientProcessIDKM());
#endif
	}
	else
	{
#if !defined(PVRSRV_ENABLE_MEMORY_STATS)
		PVRSRVStatsDecrMemAllocStatAndUntrack(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
		                                      (IMG_UINT64)(uintptr_t) pvAddr);
#else
		PVRSRVStatsRemoveMemAllocRecord(PVRSRV_MEM_ALLOC_TYPE_VMALLOC,
		                                (IMG_UINT64)(uintptr_t) pvAddr,
		                                OSGetCurrentClientProcessIDKM());
#endif
	}
#endif /* !defined(PVRSRV_ENABLE_PROCESS_STATS) */
}

void *(OSAllocMem)(IMG_UINT32 ui32Size DEBUG_MEMSTATS_PARAMS)
{
	void *pvRet = NULL;

	if ((ui32Size + ALLOCMEM_MEMSTATS_PADDING) <= g_ui32kmallocThreshold)
	{
		pvRet = kmalloc(ui32Size + ALLOCMEM_MEMSTATS_PADDING, GFP_KERNEL);
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
		pvRet = vmalloc(ui32Size);
	}

	if (pvRet != NULL)
	{
		_pvr_alloc_stats_add(pvRet, ui32Size DEBUG_MEMSTATS_ALLOC_RECORD_VALUES);
	}

	return pvRet;
}

void *(OSAllocZMem)(IMG_UINT32 ui32Size DEBUG_MEMSTATS_PARAMS)
{
	void *pvRet = NULL;

	if ((ui32Size + ALLOCMEM_MEMSTATS_PADDING) <= g_ui32kmallocThreshold)
	{
		pvRet = kzalloc(ui32Size + ALLOCMEM_MEMSTATS_PADDING, GFP_KERNEL);
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
		pvRet = vzalloc(ui32Size);
	}

	if (pvRet != NULL)
	{
		_pvr_alloc_stats_add(pvRet, ui32Size DEBUG_MEMSTATS_ALLOC_RECORD_VALUES);
	}

	return pvRet;
}

/*
 * The parentheses around OSFreeMem prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
void (OSFreeMem)(void *pvMem)
{
#if defined(DEBUG) && defined(SUPPORT_VALIDATION)
	unsigned long flags;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData)
	{
		IMG_UINT32 ui32kmallocLeakMax = psPVRSRVData->sMemLeakIntervals.ui32OSAlloc;

		spin_lock_irqsave(&kmalloc_leak_lock, flags);

		g_ui32kmallocLeakCounter++;
		if (ui32kmallocLeakMax && (g_ui32kmallocLeakCounter >= ui32kmallocLeakMax))
		{
			g_ui32kmallocLeakCounter = 0;
			spin_unlock_irqrestore(&kmalloc_leak_lock, flags);

			PVR_DPF((PVR_DBG_WARNING,
			         "%s: Skipped freeing of pointer 0x%p to trigger memory leak.",
			         __func__,
			         pvMem));
			return;
		}

		spin_unlock_irqrestore(&kmalloc_leak_lock, flags);
	}
#endif
	if (pvMem != NULL)
	{
		_pvr_alloc_stats_remove(pvMem);

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

void *OSAllocMemNoStats(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if (ui32Size <= g_ui32kmallocThreshold)
	{
		pvRet = kmalloc(ui32Size, GFP_KERNEL);
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
		pvRet = vmalloc(ui32Size);
	}

	return pvRet;
}

void *OSAllocZMemNoStats(IMG_UINT32 ui32Size)
{
	void *pvRet = NULL;

	if (ui32Size <= g_ui32kmallocThreshold)
	{
		pvRet = kzalloc(ui32Size, GFP_KERNEL);
		if (pvRet == NULL)
		{
			OSTryDecreaseKmallocThreshold();
		}
		else
		{
			OSResetKmallocFailCount();
		}
	}

	if (pvRet == NULL)
	{
		pvRet = vzalloc(ui32Size);
	}

	return pvRet;
}

/*
 * The parentheses around OSFreeMemNoStats prevent the macro in allocmem.h from
 * applying, as it would break the function's definition.
 */
void (OSFreeMemNoStats)(void *pvMem)
{
#if defined(DEBUG) && defined(SUPPORT_VALIDATION)
	unsigned long flags;
	PVRSRV_DATA *psPVRSRVData = PVRSRVGetPVRSRVData();

	if (psPVRSRVData)
	{
		IMG_UINT32 ui32kmallocLeakMax = psPVRSRVData->sMemLeakIntervals.ui32OSAlloc;

		spin_lock_irqsave(&kmalloc_leak_lock, flags);

		g_ui32kmallocLeakCounter++;
		if (ui32kmallocLeakMax && (g_ui32kmallocLeakCounter >= ui32kmallocLeakMax))
		{
			g_ui32kmallocLeakCounter = 0;
			spin_unlock_irqrestore(&kmalloc_leak_lock, flags);

			PVR_DPF((PVR_DBG_WARNING,
			         "%s: Skipped freeing of pointer 0x%p to trigger memory leak.",
			         __func__,
			         pvMem));
			return;
		}

		spin_unlock_irqrestore(&kmalloc_leak_lock, flags);
	}
#endif
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
