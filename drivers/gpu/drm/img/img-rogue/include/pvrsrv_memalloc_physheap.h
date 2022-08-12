/*************************************************************************/ /*!
@File           pvrsrv_memalloc_physheap.h
@Title          Services Phys Heap types
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Used in creating and allocating from Physical Heaps.
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
#ifndef PVRSRV_MEMALLOC_PHYSHEAP_H
#define PVRSRV_MEMALLOC_PHYSHEAP_H

#include "img_defs.h"

/*
 * These IDs are replicated in the Device Memory allocation flags to allow
 * allocations to be made in terms of their locality/use to ensure the correct
 * physical heap is accessed for the given system/platform configuration.
 * A system Phys Heap Config is linked to one or more Phys Heaps. When a heap
 * is not present in the system configuration the allocation will fallback to
 * the default GPU_LOCAL physical heap which all systems must define.
 * See PVRSRV_MEMALLOCFLAGS_*_MAPPABLE_MASK.
 *
 * NOTE: Enum order important, table in physheap.c must change if order changed.
 */
typedef IMG_UINT32 PVRSRV_PHYS_HEAP;
/* Services client accessible heaps */
#define PVRSRV_PHYS_HEAP_DEFAULT      0U  /* default phys heap for device memory allocations */
#define PVRSRV_PHYS_HEAP_GPU_LOCAL    1U  /* used for buffers with more GPU access than CPU */
#define PVRSRV_PHYS_HEAP_CPU_LOCAL    2U  /* used for buffers with more CPU access than GPU */
#define PVRSRV_PHYS_HEAP_GPU_PRIVATE  3U  /* used for buffers that only required GPU read/write access, not visible to the CPU. */

#define HEAPSTR(x) #x
static inline const IMG_CHAR *PVRSRVGetClientPhysHeapName(PVRSRV_PHYS_HEAP ePhysHeapID)
{
	switch (ePhysHeapID)
	{
		case PVRSRV_PHYS_HEAP_DEFAULT:
			return HEAPSTR(PVRSRV_PHYS_HEAP_DEFAULT);
		case PVRSRV_PHYS_HEAP_GPU_LOCAL:
			return HEAPSTR(PVRSRV_PHYS_HEAP_GPU_LOCAL);
		case PVRSRV_PHYS_HEAP_CPU_LOCAL:
			return HEAPSTR(PVRSRV_PHYS_HEAP_CPU_LOCAL);
		case PVRSRV_PHYS_HEAP_GPU_PRIVATE:
			return HEAPSTR(PVRSRV_PHYS_HEAP_GPU_PRIVATE);
		default:
			return "Unknown Heap";
	}
}

/* Services internal heaps */
#define PVRSRV_PHYS_HEAP_FW_MAIN      4U  /* runtime data, e.g. CCBs, sync objects */
#define PVRSRV_PHYS_HEAP_EXTERNAL     5U  /* used by some PMR import/export factories where the physical memory heap is not managed by the pvrsrv driver */
#define PVRSRV_PHYS_HEAP_GPU_COHERENT 6U  /* used for a cache coherent region */
#define PVRSRV_PHYS_HEAP_GPU_SECURE   7U  /* used by security validation */
#define PVRSRV_PHYS_HEAP_FW_CONFIG    8U  /* subheap of FW_MAIN, configuration data for FW init */
#define PVRSRV_PHYS_HEAP_FW_CODE      9U  /* used by security validation or dedicated fw */
#define PVRSRV_PHYS_HEAP_FW_PRIV_DATA 10U /* internal FW data (like the stack, FW control data structures, etc.) */
#define PVRSRV_PHYS_HEAP_FW_PREMAP0   11U /* Host OS premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP1   12U /* Guest OS 1 premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP2   13U /* Guest OS 2 premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP3   14U /* Guest OS 3 premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP4   15U /* Guest OS 4 premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP5   16U /* Guest OS 5 premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP6   17U /* Guest OS 6 premap fw heap */
#define PVRSRV_PHYS_HEAP_FW_PREMAP7   18U /* Guest OS 7 premap fw heap */
#define PVRSRV_PHYS_HEAP_LAST         19U


static_assert(PVRSRV_PHYS_HEAP_LAST <= (0x1FU + 1U), "Ensure enum fits in memalloc flags bitfield.");

/*! Type conveys the class of physical heap to instantiate within Services
 * for the physical pool of memory. */
typedef enum _PHYS_HEAP_TYPE_
{
	PHYS_HEAP_TYPE_UNKNOWN = 0,     /*!< Not a valid value for any config */
	PHYS_HEAP_TYPE_UMA,             /*!< Heap represents OS managed physical memory heap
	                                     i.e. system RAM. Unified Memory Architecture
	                                     physmem_osmem PMR factory */
	PHYS_HEAP_TYPE_LMA,             /*!< Heap represents physical memory pool managed by
	                                     Services i.e. carve out from system RAM or local
	                                     card memory. Local Memory Architecture
	                                     physmem_lma PMR factory */
#if defined(__KERNEL__)
	PHYS_HEAP_TYPE_DMA,             /*!< Heap represents a physical memory pool managed by
	                                     Services, alias of LMA and is only used on
	                                     VZ non-native system configurations for
	                                     a heap used for PHYS_HEAP_USAGE_FW_MAIN tagged
	                                     buffers */
#if defined(SUPPORT_WRAP_EXTMEMOBJECT)
	PHYS_HEAP_TYPE_WRAP,            /*!< Heap used to group UM buffers given
	                                     to Services. Integrity OS port only. */
#endif
#endif
} PHYS_HEAP_TYPE;

/* Defines used when interpreting the ui32PhysHeapFlags in PHYS_HEAP_MEM_STATS
     0x000000000000dttt
     d = is this the default heap? (1=yes, 0=no)
   ttt = heap type (000 = PHYS_HEAP_TYPE_UNKNOWN,
                    001 = PHYS_HEAP_TYPE_UMA,
                    010 = PHYS_HEAP_TYPE_LMA,
                    011 = PHYS_HEAP_TYPE_DMA)
*/
#define PVRSRV_PHYS_HEAP_FLAGS_TYPE_MASK  (0x7U << 0)
#define PVRSRV_PHYS_HEAP_FLAGS_IS_DEFAULT (0x1U << 7)

typedef struct PHYS_HEAP_MEM_STATS_TAG
{
	IMG_UINT64	ui64TotalSize;
	IMG_UINT64	ui64FreeSize;
	IMG_UINT32	ui32PhysHeapFlags;
}PHYS_HEAP_MEM_STATS, *PHYS_HEAP_MEM_STATS_PTR;

typedef struct PHYS_HEAP_MEM_STATS_PKD_TAG
{
	IMG_UINT64	ui64TotalSize;
	IMG_UINT64	ui64FreeSize;
	IMG_UINT32	ui32PhysHeapFlags;
	IMG_UINT32	ui32Dummy;
}PHYS_HEAP_MEM_STATS_PKD, *PHYS_HEAP_MEM_STATS_PKD_PTR;

static inline const IMG_CHAR *PVRSRVGetClientPhysHeapTypeName(PHYS_HEAP_TYPE ePhysHeapType)
{
	switch (ePhysHeapType)
	{
		case PHYS_HEAP_TYPE_UMA:
			return HEAPSTR(PHYS_HEAP_TYPE_UMA);
		case PHYS_HEAP_TYPE_LMA:
			return HEAPSTR(PHYS_HEAP_TYPE_LMA);
		default:
			return "Unknown Heap Type";
	}
}
#undef HEAPSTR

#endif /* PVRSRV_MEMALLOC_PHYSHEAP_H */
