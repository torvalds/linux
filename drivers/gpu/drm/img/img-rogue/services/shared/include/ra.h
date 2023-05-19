/*************************************************************************/ /*!
@File
@Title          Resource Allocator API
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

#ifndef RA_H
#define RA_H

#include "img_types.h"
#include "pvrsrv_error.h"

#define RA_MAX_NAME_LENGTH 20

/** Resource arena.
 *  struct _RA_ARENA_ deliberately opaque
 */
typedef struct _RA_ARENA_ RA_ARENA;			//PRQA S 3313

/** Resource arena's iterator.
 *  struct _RA_ARENA_ITERATOR_ deliberately opaque
 */
typedef struct _RA_ARENA_ITERATOR_ RA_ARENA_ITERATOR;

typedef struct _RA_ITERATOR_DATA_ {
	IMG_UINT64 uiAddr;
	IMG_UINT64 uiSize;
	IMG_BOOL bFree;
} RA_ITERATOR_DATA;

/** Resource arena usage statistics.
 *  struct _RA_USAGE_STATS
 */
typedef struct _RA_USAGE_STATS {
	IMG_UINT64	ui64TotalArenaSize;
	IMG_UINT64	ui64FreeArenaSize;
}RA_USAGE_STATS, *PRA_USAGE_STATS;

/*
 * Per-Arena handle - this is private data for the caller of the RA.
 * The RA knows nothing about this data. It is given it in RA_Create, and
 * promises to pass it to calls to the ImportAlloc and ImportFree callbacks
 */
typedef IMG_HANDLE RA_PERARENA_HANDLE;
/*
 * Per-Import handle - this is private data for the caller of the RA.
 * The RA knows nothing about this data. It is given it on a per-import basis,
 * basis, either the "initial" import at RA_Create time, or further imports
 * via the ImportAlloc callback. It sends it back via the ImportFree callback,
 * and also provides it in answer to any RA_Alloc request to signify from
 * which "import" the allocation came.
 */
typedef IMG_HANDLE RA_PERISPAN_HANDLE;

typedef IMG_UINT64 RA_BASE_T;
typedef IMG_UINT32 RA_LOG2QUANTUM_T;
typedef IMG_UINT64 RA_LENGTH_T;
typedef IMG_UINT32 RA_POLICY_T;

typedef struct _RA_BASE_MULTI_ RA_BASE_MULTI_T;

typedef IMG_UINT32 RA_BASE_ARRAY_SIZE_T;


/*
 * RA_BASE_ARRAY can represent a number of bases of which are packed,
 * that is, they can be one of two types, a Real Base or a Ghost base.
 * A Real Base is a base that has been created by the RA and is used to
 * represent an allocated region, it has an entry in the RA Hash table and
 * as such has a BT associated with it.
 * A Ghost base is a fabricated base address generated at chunk boundaries
 * given by the caller. These are used to divide a RealBase into
 * arbitrary regions that the caller requires e.g. 4k pages. Ghost bases don't
 * exist from the RA memory tracking point of view but they do exist and are treated
 * as base addresses from the PoV of the caller. This allows the RA to allocate in
 * largest possible lengths meaning fewer alloc calls whilst allowing the chunk
 * flexibility for callers. Ghost refers to the concept that they
 * don't exist in this RA internals context but do in the callers (LMA) context i.e.
 * they appear Real from another perspective but we the RA know they are a ghost of the
 * Real Base.
 * */
#if defined(__GNUC__) && GCC_VERSION_AT_LEAST(9, 0)
/* Use C99 dynamic arrays, older compilers do not support this. */
typedef RA_BASE_T RA_BASE_ARRAY_T[];
#else
/* Variable length array work around, will contain at least 1 element.
 * Causes errors on newer compilers, in which case use dynamic arrays (see above).
 */
#define RA_FLEX_ARRAY_ONE_OR_MORE_ELEMENTS 1U
typedef RA_BASE_T RA_BASE_ARRAY_T[RA_FLEX_ARRAY_ONE_OR_MORE_ELEMENTS];
#endif

/* Since 0x0 is a valid BaseAddr, we rely on max 64-bit value to be an invalid
 * page address.
 */
#define INVALID_BASE_ADDR (IMG_UINT64_MAX)
/* Used to check for duplicated alloc indices in sparse alloc path
 * prior to attempting allocations */
#define RA_BASE_SPARSE_PREP_ALLOC_ADDR (IMG_UINT64_MAX - 1)
#define RA_BASE_FLAGS_MASK 0xFFF /* 12 Bits 4k alignment. */
#define RA_BASE_FLAGS_LOG2 12
#define RA_BASE_CHUNK_LOG2_MAX 64
#define RA_BASE_GHOST_BIT (1ULL << 0)
#define RA_BASE_STRIP_GHOST_BIT(uiBase) ((uiBase) & ~(RA_BASE_GHOST_BIT))
#define RA_BASE_SET_GHOST_BIT(uiBase)   ((uiBase) |= RA_BASE_GHOST_BIT)
#define RA_BASE_IS_GHOST(uiBase) (BITMASK_HAS((uiBase), RA_BASE_GHOST_BIT) && (uiBase) != INVALID_BASE_ADDR)
#define RA_BASE_IS_REAL(uiBase) (!BITMASK_HAS((uiBase), RA_BASE_GHOST_BIT))
#define RA_BASE_IS_SPARSE_PREP(uiBase) ((uiBase) == RA_BASE_SPARSE_PREP_ALLOC_ADDR)
#define RA_BASE_IS_INVALID(uiBase) ((uiBase) == INVALID_BASE_ADDR)

typedef struct _RA_MULTIBASE_ITERATOR_ RA_MULTIBASE_ITERATOR;

/* Lock classes: describes the level of nesting between different arenas. */
#define RA_LOCKCLASS_0 0
#define RA_LOCKCLASS_1 1
#define RA_LOCKCLASS_2 2

#define RA_NO_IMPORT_MULTIPLIER 1

/*
 * Allocation Policies that govern the resource areas.
 * */

/* --- Resource allocation policy definitions ---
* | 31.........5|.......4....|......3....|........2.............|1...................0|
* | Reserved    | Non-Contig | No split  | Area bucket selection| Alloc node selection|
*/

/*
 * Fast allocation policy allows to pick the first node
 * that satisfies the request.
 * It is the default policy for all arenas.
 *  */
#define RA_POLICY_ALLOC_FAST			(0U)
/*
 * Optimal allocation policy allows to pick the lowest size node
 * that satisfies the request. This picking policy helps in reducing the fragmentation.
 * This minimises the necessity to split the nodes more often as the optimal
 * ones are picked.
 * As a result any future higher size allocation requests are likely to succeed
 */
#define RA_POLICY_ALLOC_OPTIMAL		(1U)
#define RA_POLICY_ALLOC_NODE_SELECT_MASK			(3U)

/*
 * Bucket selection policies
 * */
/* Assured bucket policy makes sure the selected bucket is guaranteed
 * to satisfy the given request. Generally Nodes picked up from such a
 * bucket need to be further split. However picking node that belongs to this
 * bucket is likely to succeed and thus promises better response times */
#define RA_POLICY_BUCKET_ASSURED_FIT		(0U)
/*
 * Best fit bucket policy selects a bucket with free nodes that are likely
 * to satisfy the request and nodes that are close to the requested size.
 * Nodes picked up from this bucket may likely to satisfy the request but not
 * guaranteed. Failing to satisfy the request from this bucket mean further
 * higher size buckets are selected in the later iterations till the request
 * is satisfied.
 *
 * Hence response times may vary depending on availability of free nodes
 * that satisfy the request.
 * */
#define RA_POLICY_BUCKET_BEST_FIT		(4U)
#define RA_POLICY_BUCKET_MASK			(4U)

/* This flag ensures the imports will not be split up and Allocations will always get
 * their own import
 */
#define RA_POLICY_NO_SPLIT			(8U)
#define RA_POLICY_NO_SPLIT_MASK		(8U)

/* This flag is used in physmem_lma only. it is used to decide if we should
 * activate the non-contiguous allocation feature of RA MultiAlloc.
 * Requirements for activation are that the OS implements the
 * OSMapPageArrayToKernelVA function in osfunc which allows for mapping
 * physically sparse pages as a virtually contiguous range.
 * */
#define RA_POLICY_ALLOC_ALLOW_NONCONTIG      (16U)
#define RA_POLICY_ALLOC_ALLOW_NONCONTIG_MASK (16U)

/*
 * Default Arena Policy
 * */
#define RA_POLICY_DEFAULT			(RA_POLICY_ALLOC_FAST | RA_POLICY_BUCKET_ASSURED_FIT)

/*
 * Flags in an "import" must match the flags for an allocation
 */
typedef IMG_UINT64 RA_FLAGS_T;

/*************************************************************************/ /*!
@Function       Callback function PFN_RA_ALLOC
@Description    RA import allocate function
@Input          RA_PERARENA_HANDLE RA handle
@Input          RA_LENGTH_T        Request size
@Input          RA_FLAGS_T         RA flags
@Input          RA_LENGTH_T        Base Alignment
@Input          IMG_CHAR           Annotation
@Input          RA_BASE_T          Allocation base
@Input          RA_LENGTH_T        Actual size
@Input          RA_PERISPAN_HANDLE Per import private data
@Return         PVRSRV_ERROR       PVRSRV_OK or error code
*/ /**************************************************************************/
typedef PVRSRV_ERROR (*PFN_RA_ALLOC)(RA_PERARENA_HANDLE,
									 RA_LENGTH_T,
									 RA_FLAGS_T,
									 RA_LENGTH_T,
									 const IMG_CHAR*,
									 RA_BASE_T*,
									 RA_LENGTH_T*,
									 RA_PERISPAN_HANDLE*);

/*************************************************************************/ /*!
@Function       Callback function PFN_RA_FREE
@Description    RA free imported allocation
@Input          RA_PERARENA_HANDLE   RA handle
@Input          RA_BASE_T            Allocation base
@Output         RA_PERISPAN_HANDLE   Per import private data
*/ /**************************************************************************/
typedef void (*PFN_RA_FREE)(RA_PERARENA_HANDLE,
							RA_BASE_T,
							RA_PERISPAN_HANDLE);

/**
 *  @Function   RA_Create
 *
 *  @Description    To create a resource arena.
 *
 *  @Input name - the name of the arena for diagnostic purposes.
 *  @Input uLog2Quantum - the arena allocation quantum.
 *  @Input ui32LockClass - the lock class level this arena uses.
 *  @Input imp_alloc - a resource allocation callback or 0.
 *  @Input imp_free - a resource de-allocation callback or 0.
 *  @Input per_arena_handle - private handle passed to alloc and free or 0.
 *  @Input ui32PolicyFlags - Policies that govern the arena.
 *  @Return pointer to arena, or NULL.
 */
RA_ARENA *
RA_Create(IMG_CHAR *name,
          /* subsequent imports: */
          RA_LOG2QUANTUM_T uLog2Quantum,
          IMG_UINT32 ui32LockClass,
          PFN_RA_ALLOC imp_alloc,
          PFN_RA_FREE imp_free,
          RA_PERARENA_HANDLE per_arena_handle,
          RA_POLICY_T ui32PolicyFlags);

/**
 *  @Function   RA_Create_With_Span
 *
 *  @Description
 *
 *  Create a resource arena and initialises it, with a given resource span.
 *
 *  @Input name - String briefly describing the RA's purpose.
 *  @Input uLog2Quantum - the arena allocation quantum.
 *  @Input ui64CpuBase - CPU Physical Base Address of the RA.
 *  @Input ui64SpanDevBase - Device Physical Base Address of the RA.
 *  @Input ui64SpanSize - Size of the span to add to the created RA.
 *  @Input ui32PolicyFlags - Policies that govern the arena.
 *  @Return pointer to arena, or NULL.
*/
RA_ARENA *
RA_Create_With_Span(IMG_CHAR *name,
                    RA_LOG2QUANTUM_T uLog2Quantum,
                    IMG_UINT64 ui64CpuBase,
                    IMG_UINT64 ui64SpanDevBase,
                    IMG_UINT64 ui64SpanSize,
                    RA_POLICY_T ui32PolicyFlags);

/**
 *  @Function   RA_Delete
 *
 *  @Description
 *
 *  To delete a resource arena. All resources allocated from the arena
 *  must be freed before deleting the arena.
 *
 *  @Input  pArena - the arena to delete.
 *  @Return None
 */
void
RA_Delete(RA_ARENA *pArena);

/**
 *  @Function   RA_Add
 *
 *  @Description
 *
 *  To add a resource span to an arena. The span must not overlap with
 *  any span previously added to the arena.
 *
 *  @Input pArena - the arena to add a span into.
 *  @Input base - the base of the span.
 *  @Input uSize - the extent of the span.
 *  @Input hPriv - handle associated to the span (reserved for user uses)
 *  @Return IMG_TRUE - success, IMG_FALSE - failure
 */
IMG_BOOL
RA_Add(RA_ARENA *pArena,
       RA_BASE_T base,
       RA_LENGTH_T uSize,
       RA_FLAGS_T uFlags,
       RA_PERISPAN_HANDLE hPriv);

/**
 *  @Function   RA_Alloc
 *
 *  @Description    To allocate resource from an arena.
 *
 *  @Input  pArena - the arena
 *  @Input  uRequestSize - the size of resource segment requested.
 *  @Input  uImportMultiplier - Import x-times of the uRequestSize
 *          for future RA_Alloc calls.
 *          Use RA_NO_IMPORT_MULTIPLIER to import the exact size.
 *  @Input  uImportFlags - flags influencing allocation policy.
 *  @Input  uAlignment - the alignment constraint required for the
 *          allocated segment, use 0 if alignment not required.
 *  @Input  pszAnnotation - a string to describe the allocation
 *  @Output base - allocated base resource
 *  @Output pActualSize - the actual_size of resource segment allocated,
 *          typically rounded up by quantum.
 *  @Output phPriv - the user reference associated with allocated
 *          resource span.
 *  @Return PVRSRV_OK - success
 */
PVRSRV_ERROR
RA_Alloc(RA_ARENA *pArena,
         RA_LENGTH_T uRequestSize,
         IMG_UINT8 uImportMultiplier,
         RA_FLAGS_T uImportFlags,
         RA_LENGTH_T uAlignment,
         const IMG_CHAR *pszAnnotation,
         RA_BASE_T *base,
         RA_LENGTH_T *pActualSize,
         RA_PERISPAN_HANDLE *phPriv);

/*************************************************************************/ /*!
@Function       RA_AllocMulti
@Description    To allocate resource from an arena.
                This method of allocation can be used to guarantee that if there
                is enough space in the RA and the contiguity given is the
                greatest common divisor of the contiguities used on this RA
                the allocation can be made.
                Allocations with contiguity less than the current GCD
                (Greatest Common Divisor) abiding to pow2 are also guaranteed to
                succeed. See scenario 4.
                Allocations are not guaranteed but still reduce fragmentation
                using this method when multiple contiguities are used e.g.
                4k & 16k and the current allocation has a contiguity higher than
                the greatest common divisor used.
                Scenarios with Log 2 contiguity examples:
                1. All allocations have contiguity of 4k. Allocations can be
                guaranteed given enough RA space since the GCD is always used.
                2. Allocations of 4k and 16k contiguity have been previously
                made on this RA. A new allocation of 4k contiguity is guaranteed
                to succeed given enough RA space since the contiguity is the GCD.
                3. Allocations of 4k and 16k contiguity have been previously made
                on this RA. A new allocation of 16k contiguity is not guaranteed
                to succeed since it is not the GCD of all contiguities used.
                4. Contiguity 16k and 64k already exist, a 4k contiguity
                allocation would be guaranteed to succeed but would now be the
                new GCD. So further allocations would be required to match this
                GCD to guarantee success.
                This method does not suffer the same fragmentation pitfalls
                as RA_Alloc as it constructs the allocation size from many
                smaller constituent allocations, these are represented and returned
                in the given array. In addition, Ghost bases are generated in
                array entries conforming to the chunk size, this allows for
                representing chunks of any size that work as page addrs
                in upper levels.
                The aforementioned array must be at least of size
                uRequestsize / uiChunkSize, this ensures there is at least one
                array entry per chunk required.
                This function must have a uiChunkSize value of
                at least 4096, this is to ensure space for the base type encoding.
@Input          pArena            The arena
@Input          uRequestSize      The size of resource requested.
@Input          uiLog2ChunkSize   The log2 contiguity multiple of the bases i.e all
                                  Real bases must be a multiple in size of this
                                  size, also used to generate Ghost bases.
                                  Allocations will also be aligned to this value.
@Input          uImportMultiplier Import x-times more for future requests if
                                  we have to import new resource.
@Input          uImportFlags      Flags influencing allocation policy.
                                  required, otherwise must be a power of 2.
@Input          pszAnnotation     String to describe the allocation
@InOut          aBaseArray        Array of bases to populate.
@Input          uiBaseArraySize   Size of the array to populate.
@Output         bPhysContig       Are the allocations made in the RA physically
                                  contiguous.
@Return         PVRSRV_OK - success
*/ /**************************************************************************/
PVRSRV_ERROR
RA_AllocMulti(RA_ARENA *pArena,
               RA_LENGTH_T uRequestSize,
               IMG_UINT32 uiLog2ChunkSize,
               IMG_UINT8 uImportMultiplier,
               RA_FLAGS_T uImportFlags,
               const IMG_CHAR *pszAnnotation,
               RA_BASE_ARRAY_T aBaseArray,
               RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
               IMG_BOOL *bPhysContig);

/**
 * @Function   RA_AllocMultiSparse
 *
 * @Description    To Alloc resource from an RA arena at the specified indices.
 *                 This function follows the same conditions and functionality as
 *                 RA_AllocMulti although with the added aspect of specifying the
 *                 indices to allocate in the Base Array. This means we can still
 *                 attempt to maintain contiguity where possible with the aim of
 *                 reducing fragmentation and increasing occurrence of optimal free
 *                 scenarios.
 * @Input          pArena            The Arena
 * @Input          uiLog2ChunkSize   The log2 contiguity multiple of the bases i.e all
 *                                   Real bases must be a multiple in size of this
 *                                   size, also used to generate Ghost bases.
 *                                   Allocations will also be aligned to this value.
 * @Input          uImportMultiplier Import x-times more for future requests if
 *                                   we have to import new resource.
 * @Input          uImportFlags      Flags influencing allocation policy.
 *                                   required, otherwise must be a power of 2.
 * @Input          pszAnnotation     String to describe the allocation
 * @InOut          aBaseArray        Array of bases to populate.
 * @Input          uiBaseArraySize   Size of the array to populate.
 * @Input          puiAllocIndices   The indices into the array to alloc, if indices are NULL
 *                                   then we will allocate uiAllocCount chunks sequentially.
 * @InOut          uiAllocCount      The number of bases to alloc from the array.
 *
 *  @Return PVRSRV_OK - success
 */
PVRSRV_ERROR
RA_AllocMultiSparse(RA_ARENA *pArena,
                     IMG_UINT32 uiLog2ChunkSize,
                     IMG_UINT8 uImportMultiplier,
                     RA_FLAGS_T uImportFlags,
                     const IMG_CHAR *pszAnnotation,
                     RA_BASE_ARRAY_T aBaseArray,
                     RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                     IMG_UINT32 *puiAllocIndices,
                     IMG_UINT32 uiAllocCount);
/**
 *  @Function   RA_FreeMulti
 *
 *  @Description    To free a multi-base resource constructed using
 *                  a call to RA_AllocMulti.
 *
 *  @Input  pArena     - The arena the segment was originally allocated from.
 *  @Input  aBaseArray - The array to free bases from.
 *  @Input  uiBaseArraysize - Size of the array to free bases from.
 *
 *  @Return PVRSRV_OK - success
 */
PVRSRV_ERROR
RA_FreeMulti(RA_ARENA *pArena,
              RA_BASE_ARRAY_T aBaseArray,
              RA_BASE_ARRAY_SIZE_T uiBaseArraySize);

/**
 *  @Function   RA_FreeMultiSparse
 *
 *  @Description    To free part of a multi-base resource constructed using
 *                  a call to RA_AllocMulti.
 *
 *  @Input  pArena     - The arena the segment was originally allocated from.
 *  @Input  aBaseArray - The array to free bases from.
 *  @Input  uiBaseArraysize - Size of the array to free bases from.
 *  @Input  uiLog2ChunkSize - The log2 chunk size used to generate the Ghost bases.
 *  @Input  puiFreeIndices - The indices into the array to free.
 *  @InOut  puiFreeCount - The number of bases to free from the array, becomes the number
 *                         of bases actually free'd. The in value may differ from the out
 *                         value in cases of error when freeing. The out value can then be
 *                         used in upper levels to keep any mem tracking structures consistent
 *                         with what was actually freed before the error occurred.
 *
 *  @Return PVRSRV_OK - success
 */
PVRSRV_ERROR
RA_FreeMultiSparse(RA_ARENA *pArena,
                    RA_BASE_ARRAY_T aBaseArray,
                    RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                    IMG_UINT32 uiLog2ChunkSize,
                    IMG_UINT32 *puiFreeIndices,
                    IMG_UINT32 *puiFreeCount);

/**
 *  @Function   RA_Alloc_Range
 *
 *  @Description
 *
 *  To allocate a resource at a specified base from an arena.
 *
 *  @Input  pArena - the arena
 *  @Input  uRequestSize - the size of resource segment requested.
 *  @Input  uImportFlags - flags influencing allocation policy.
 *  @Input  uAlignment - the alignment constraint required for the
 *          allocated segment, use 0 if alignment not required.
 *  @Input  base - allocated base resource
 *  @Output pActualSize - the actual_size of resource segment allocated,
 *          typically rounded up by quantum.
 *  @Return PVRSRV_OK - success
 */
PVRSRV_ERROR
RA_Alloc_Range(RA_ARENA *pArena,
		  RA_LENGTH_T uRequestSize,
		  RA_FLAGS_T uImportFlags,
		  RA_LENGTH_T uAlignment,
		  RA_BASE_T base,
		  RA_LENGTH_T *pActualSize);

/**
 *  @Function   RA_Free
 *
 *  @Description    To free a resource segment.
 *
 *  @Input  pArena - the arena the segment was originally allocated from.
 *  @Input  base - the base of the resource span to free.
 *
 *  @Return None
 */
void
RA_Free(RA_ARENA *pArena, RA_BASE_T base);

/**
 *  @Function   RA_SwapSparseMem
 *
 *  @Description    Swaps chunk sized allocations at X<->Y indices.
 *                  The function is most optimal when Indices are provided
 *                  in ascending order, this allows the internals to optimally
 *                  swap based on contiguity and reduces the amount of ghost to
 *                  real conversion performed. Note this function can also be used
 *                  to move pages, in this case, we effectively swap real allocations
 *                  with invalid marked bases.
 *  @Input  pArena     - The arena.
 *  @InOut  aBaseArray - The array to Swap bases in.
 *  @Input  uiBaseArraysize - Size of the array to Swap bases in.
 *  @Input  uiLog2ChunkSize - The log2 chunk size used to generate the Ghost bases
 *                            and size the Real chunks.
 *  @Input  puiXIndices - Set of X indices to swap with parallel indices in Y.
 *  @Input  puiYIndices - Set of Y indices to swap with parallel indices in X.
 *  @Input  uiSwapCount - Number of indices to swap.
 *
 *  @Return PVRSRV_OK - success
 */
PVRSRV_ERROR
RA_SwapSparseMem(RA_ARENA *pArena,
                  RA_BASE_ARRAY_T aBaseArray,
                  RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                  IMG_UINT32 uiLog2ChunkSize,
                  IMG_UINT32 *puiXIndices,
                  IMG_UINT32 *puiYIndices,
                  IMG_UINT32 uiSwapCount);

/**
 *  @Function   RA_Get_Usage_Stats
 *
 *  @Description    To collect the arena usage statistics.
 *
 *  @Input  pArena - the arena to acquire usage statistics from.
 *  @Input  psRAStats - the buffer to hold the usage statistics of the arena.
 *
 *  @Return None
 */
IMG_INTERNAL void
RA_Get_Usage_Stats(RA_ARENA *pArena, PRA_USAGE_STATS psRAStats);

/**
 *  @Function   RA_GetArenaName
 *
 *  @Description    To obtain the arena name.
 *
 *  @Input  pArena - the arena to acquire the name from.
 *
 *  @Return IMG_CHAR* Arena name.
 */
IMG_INTERNAL IMG_CHAR *
RA_GetArenaName(RA_ARENA *pArena);

IMG_INTERNAL RA_ARENA_ITERATOR *
RA_IteratorAcquire(RA_ARENA *pArena, IMG_BOOL bIncludeFreeSegments);

IMG_INTERNAL void
RA_IteratorReset(RA_ARENA_ITERATOR *pIter);

IMG_INTERNAL void
RA_IteratorRelease(RA_ARENA_ITERATOR *pIter);

IMG_INTERNAL IMG_BOOL
RA_IteratorNext(RA_ARENA_ITERATOR *pIter, RA_ITERATOR_DATA *pData);

/*************************************************************************/ /*!
@Function       RA_BlockDump
@Description    Debug dump of all memory allocations within the RA and the space
                between. A '#' represents a block of memory (the arena's quantum
                in size) that has been allocated whereas a '.' represents a free
                block.
@Input          pArena        The arena to dump.
@Input          pfnLogDump    The dumping method.
@Input          pPrivData     Data to be passed into the pfnLogDump method.
*/ /**************************************************************************/
IMG_INTERNAL PVRSRV_ERROR
RA_BlockDump(RA_ARENA *pArena,
             __printf(2, 3) void (*pfnLogDump)(void*, IMG_CHAR*, ...),
             void *pPrivData);

#endif
