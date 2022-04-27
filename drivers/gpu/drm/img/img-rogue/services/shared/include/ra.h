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

/* Lock classes: describes the level of nesting between different arenas. */
#define RA_LOCKCLASS_0 0
#define RA_LOCKCLASS_1 1
#define RA_LOCKCLASS_2 2

#define RA_NO_IMPORT_MULTIPLIER 1

/*
 * Allocation Policies that govern the resource areas.
 * */

/* --- Resource allocation policy definitions ---
* | 31.........4|......3....|........2.............|1..................0|
* | Reserved    | No split  | Area bucket selection| Free node selection|
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
#define RA_POLICY_ALLOC_OPTIMAL_MASK			(3U)

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
@Input          IMG_CHAR           Annotation
@Input          RA_BASE_T          Allocation base
@Input          RA_LENGTH_T        Actual size
@Input          RA_PERISPAN_HANDLE Per import private data
@Return         PVRSRV_ERROR       PVRSRV_OK or error code
*/ /**************************************************************************/
typedef PVRSRV_ERROR (*PFN_RA_ALLOC)(RA_PERARENA_HANDLE,
									 RA_LENGTH_T,
									 RA_FLAGS_T,
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
 *  @Input ui32PlicyFlags - Policies that govern the arena.
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
          IMG_UINT32 ui32PolicyFlags);

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
 *  @Return pointer to arena, or NULL.
*/
RA_ARENA *
RA_Create_With_Span(IMG_CHAR *name,
                    RA_LOG2QUANTUM_T uLog2Quantum,
                    IMG_UINT64 ui64CpuBase,
                    IMG_UINT64 ui64SpanDevBase,
                    IMG_UINT64 ui64SpanSize);

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

#endif
