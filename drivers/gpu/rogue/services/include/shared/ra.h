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

#ifndef _RA_H_
#define _RA_H_


#include "img_types.h"
#include "pvrsrv_error.h"

/** Resource arena.
 *  struct _RA_ARENA_ deliberately opaque
 */
typedef struct _RA_ARENA_ RA_ARENA;			//PRQA S 3313

/*
 * Per-Arena handle - this is private data for the caller of the RA.
 * The RA knows nothing about this data.  It is given it upon
 * RA_Create, and promises to pass it to calls to the ImportAlloc and
 * ImportFree callbacks
 */
typedef IMG_HANDLE RA_PERARENA_HANDLE;
/*
 * Per-Import handle - this is private data for the caller of the RA.
 * The RA knows nothing about this data.  It is given it on a
 * per-import basis, either the "initial" import at RA_Create time, or
 * further imports via the ImportAlloc callback.  It sends it back via
 * the ImportFree callback, and also provides it in answer to any
 * RA_Alloc request to signify from which "import" the allocation came
 */
typedef IMG_HANDLE RA_PERISPAN_HANDLE;

typedef IMG_UINT64 RA_BASE_T;
typedef IMG_UINT32 RA_LOG2QUANTUM_T;
typedef IMG_UINT64 RA_LENGTH_T;

#define RA_BASE_FMTSPEC "0x%010llx"
#define RA_ALIGN_FMTSPEC "0x%llx"
#define RA_LENGTH_FMTSPEC "0x%llx"

/* Lock classes: describes the level of nesting between different arenas. */
#define RA_LOCKCLASS_0 0
#define RA_LOCKCLASS_1 1
#define RA_LOCKCLASS_2 2

/*
 * Flags in an "import" must much the flags for an allocation
 */
typedef IMG_UINT32 RA_FLAGS_T;

struct _RA_SEGMENT_DETAILS_
{
	RA_LENGTH_T      uiSize;
	IMG_CPU_PHYADDR sCpuPhyAddr;
	IMG_HANDLE      hSegment;
};
typedef struct _RA_SEGMENT_DETAILS_ RA_SEGMENT_DETAILS;

/**
 *  @Function   RA_Create
 *
 *  @Description
 *
 *  To create a resource arena.
 *
 *  @Input name - the name of the arena for diagnostic purposes.
 *  @Input uQuantum - the arena allocation quantum.
 *  @Input ui32LockClass - the lock class level this arena uses.
 *  @Input alloc - a resource allocation callback or 0.
 *  @Input free - a resource de-allocation callback or 0.
 *  @Input per_arena_handle - user private handle passed to alloc and free or 0.
 *  @Return pointer to arena, or IMG_NULL.
 */
RA_ARENA *
RA_Create (IMG_CHAR *name,
           /* subsequent imports: */
           RA_LOG2QUANTUM_T uLog2Quantum,
		   IMG_UINT32 ui32LockClass,
           IMG_BOOL (*imp_alloc)(RA_PERARENA_HANDLE _h,
                                 RA_LENGTH_T uSize,
                                 RA_FLAGS_T uFlags,
                                 RA_BASE_T *pBase,
                                 RA_LENGTH_T *pActualSize,
                                 RA_PERISPAN_HANDLE *phPriv),
           IMG_VOID (*imp_free) (RA_PERARENA_HANDLE,
                                 RA_BASE_T,
                                 RA_PERISPAN_HANDLE),
           RA_PERARENA_HANDLE per_arena_handle);

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
IMG_VOID
RA_Delete (RA_ARENA *pArena);

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
 *  @Input hPriv - handle associated to the span (reserved to user uses)
 *  @Return IMG_TRUE - success, IMG_FALSE - failure
 */
IMG_BOOL
RA_Add (RA_ARENA *pArena,
		RA_BASE_T base,
		RA_LENGTH_T uSize,
		RA_FLAGS_T uFlags,
		RA_PERISPAN_HANDLE hPriv);

/**
 *  @Function   RA_Alloc
 *
 *  @Description
 *
 *  To allocate resource from an arena.
 *
 *  @Input  pArena - the arena
 *  @Input  uRequestSize - the size of resource segment requested.
 *  @Output pActualSize - the actual_size of resource segment allocated,
 *          typcially rounded up by quantum.
 *  @Input  uFlags - flags influencing allocation policy.
 *  @Input  uAlignment - the alignment constraint required for the
 *          allocated segment, use 0 if alignment not required.
 *  @Output pBase - allocated base resource
 *  @Output phPriv - the user reference associated with allocated
 *          resource span.
 *  @Return IMG_TRUE - success, IMG_FALSE - failure
 */
IMG_BOOL
RA_Alloc (RA_ARENA *pArena, 
          RA_LENGTH_T uSize,
          RA_FLAGS_T uFlags,
          RA_LENGTH_T uAlignment,
          RA_BASE_T *pBase,
          RA_LENGTH_T *pActualSize,
          RA_PERISPAN_HANDLE *phPriv);

/**
 *  @Function   RA_Free
 *
 *  @Description    To free a resource segment.
 *  
 *  @Input  pArena - the arena the segment was originally allocated from.
 *  @Input  base - the base of the resource span to free.
 *	@Input	bFreeBackingStore - Should backing store memory be freed?
 *
 *  @Return None
 */
IMG_VOID 
RA_Free (RA_ARENA *pArena, RA_BASE_T base);

#endif

