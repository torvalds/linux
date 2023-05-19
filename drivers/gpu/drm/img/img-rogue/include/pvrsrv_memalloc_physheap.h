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
#define PHYS_HEAP_LIST                                                                                                                           \
	X(DEFAULT)       /* Client: default phys heap for device memory allocations */                                                               \
	X(GPU_LOCAL)     /* Client: used for buffers with more GPU access than CPU */                                                                \
	X(CPU_LOCAL)     /* Client: used for buffers with more CPU access than GPU */                                                                \
	X(GPU_PRIVATE)   /* Client: used for buffers that only required GPU read/write access, not visible to the CPU. */                            \
	X(FW_MAIN)       /* Internal: runtime data, e.g. CCBs, sync objects */                                                                       \
	X(EXTERNAL)      /* Internal: used by some PMR import/export factories where the physical memory heap is not managed by the pvrsrv driver */ \
	X(GPU_COHERENT)  /* Internal: used for a cache coherent region */                                                                            \
	X(GPU_SECURE)    /* Internal: used by security validation */                                                                                 \
	X(FW_CONFIG)     /* Internal: subheap of FW_MAIN, configuration data for FW init */                                                          \
	X(FW_CODE)       /* Internal: used by security validation or dedicated fw */                                                                 \
	X(FW_PRIV_DATA)  /* Internal: internal FW data (like the stack, FW control data structures, etc.) */                                         \
	X(FW_PREMAP_PT)  /* Internal: page tables for premapped firmware memory */                                                                   \
	X(FW_PREMAP0)    /* Internal: Host OS premap fw heap */                                                                                      \
	X(FW_PREMAP1)    /* Internal: Guest OS 1 premap fw heap */                                                                                   \
	X(FW_PREMAP2)    /* Internal: Guest OS 2 premap fw heap */                                                                                   \
	X(FW_PREMAP3)    /* Internal: Guest OS 3 premap fw heap */                                                                                   \
	X(FW_PREMAP4)    /* Internal: Guest OS 4 premap fw heap */                                                                                   \
	X(FW_PREMAP5)    /* Internal: Guest OS 5 premap fw heap */                                                                                   \
	X(FW_PREMAP6)    /* Internal: Guest OS 6 premap fw heap */                                                                                   \
	X(FW_PREMAP7)    /* Internal: Guest OS 7 premap fw heap */                                                                                   \
	X(WRAP)          /* External: Wrap memory */                                                                                                 \
	X(DISPLAY)       /* External: Display memory */                                                                                              \
	X(LAST)

typedef enum _PVRSRV_PHYS_HEAP_
{
#define X(_name) PVRSRV_PHYS_HEAP_ ## _name,
	PHYS_HEAP_LIST
#undef X

	PVRSRV_PHYS_HEAP_INVALID = 0x7FFFFFFF
} PVRSRV_PHYS_HEAP;

/* Defines the number of user mode physheaps. These physheaps are: DEFAULT, GPU_LOCAL,
 * CPU_LOCAL, GPU_PRIVATE, GPU_SECURE. */
#define MAX_USER_MODE_ALLOC_PHYS_HEAPS 5

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
	                                     a heap used for allocations tagged with
	                                     PVRSRV_PHYS_HEAP_FW_MAIN or
	                                     PVRSRV_PHYS_HEAP_FW_CONFIG */
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

/* Force PHYS_HEAP_MEM_STATS size to be a multiple of 8 bytes
 * (as type is a parameter in bridge calls)
 */
typedef struct PHYS_HEAP_MEM_STATS_TAG
{
	IMG_UINT64	ui64TotalSize;
	IMG_UINT64	ui64FreeSize;
	IMG_UINT32	ui32PhysHeapFlags;
	IMG_UINT32	ui32UnusedPadding;
}PHYS_HEAP_MEM_STATS, *PHYS_HEAP_MEM_STATS_PTR;

#if defined(PHYSHEAP_STRINGS)

static const char *const _pszPhysHeapStrings[] = {
#define X(_name) #_name,
	PHYS_HEAP_LIST
#undef X
};

/*************************************************************************/ /*!
@Function       PVRSRVGetClientPhysHeapTypeName
@Description    Returns the phys heap type as a string.

@Input          ePhysHeapType   The physheap type.

@Return         const IMG_CHAR pointer.
*/ /**************************************************************************/
static inline const IMG_CHAR *PVRSRVGetClientPhysHeapTypeName(PHYS_HEAP_TYPE ePhysHeapType)
{
#define HEAPSTR(x) #x
	switch (ePhysHeapType)
	{
		case PHYS_HEAP_TYPE_UMA:
			return HEAPSTR(PHYS_HEAP_TYPE_UMA);
		case PHYS_HEAP_TYPE_LMA:
			return HEAPSTR(PHYS_HEAP_TYPE_LMA);
		default:
			return "Unknown Heap Type";
	}
#undef HEAPSTR
}

/*************************************************************************/ /*!
@Function       PVRSRVGetPhysHeapName
@Description    Returns the name of a PhysHeap.

@Input          ePhysHeap   The enum value of the physheap.

@Return         const IMG_CHAR pointer.
*/ /**************************************************************************/
static inline const IMG_CHAR *PVRSRVGetPhysHeapName(PVRSRV_PHYS_HEAP ePhysHeap)
{
	if (ePhysHeap < 0 || ePhysHeap >= PVRSRV_PHYS_HEAP_LAST)
	{
		return "Undefined";
	}

	return _pszPhysHeapStrings[ePhysHeap];
}

/*************************************************************************/ /*!
@Function       PVRSRVGetClientPhysHeapName
@Description    Returns the name of a client PhysHeap.

@Input          ePhysHeap   The enum value of the physheap.

@Return         const IMG_CHAR pointer.
*/ /**************************************************************************/
static inline const IMG_CHAR *PVRSRVGetClientPhysHeapName(PVRSRV_PHYS_HEAP ePhysHeap)
{
    if (ePhysHeap > PVRSRV_PHYS_HEAP_GPU_PRIVATE)
    {
        return "Unknown Heap";
    }

    return PVRSRVGetPhysHeapName(ePhysHeap);
}
#endif /* PHYSHEAP_STRINGS */

#endif /* PVRSRV_MEMALLOC_PHYSHEAP_H */
