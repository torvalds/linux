/*************************************************************************/ /*!
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
#include "hash.h"
#include "osfunc.h"

/** Resource arena.
 *  struct _RA_ARENA_ deliberately opaque
 */
typedef struct _RA_ARENA_ RA_ARENA;			//PRQA S 3313
typedef struct _BM_MAPPING_ BM_MAPPING;



/** Enable support for arena statistics. */
#define RA_STATS 


/** Resource arena statistics. */
struct _RA_STATISTICS_
{
    /** total number of segments add to the arena */
    IMG_SIZE_T uSpanCount;

    /** number of current live segments within the arena */
    IMG_SIZE_T uLiveSegmentCount;

    /** number of current free segments within the arena */
    IMG_SIZE_T uFreeSegmentCount;

    /** total number of resource within the arena */
    IMG_SIZE_T uTotalResourceCount;
    
    /** number of free resource within the arena */
    IMG_SIZE_T uFreeResourceCount;

    /** total number of resources allocated from the arena */
    IMG_SIZE_T uCumulativeAllocs;

    /** total number of resources returned to the arena */
    IMG_SIZE_T uCumulativeFrees;

    /** total number of spans allocated by the callback mechanism */
    IMG_SIZE_T uImportCount;

    /** total number of spans deallocated by the callback mechanism */
    IMG_SIZE_T uExportCount;
};
typedef struct _RA_STATISTICS_ RA_STATISTICS;

struct _RA_SEGMENT_DETAILS_
{
	IMG_SIZE_T      uiSize;
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
 *  @Input base - the base of an initial resource span or 0.
 *  @Input uSize - the size of an initial resource span or 0.
 *  @Input pRef - the reference to return for the initial resource or 0.
 *  @Input uQuantum - the arena allocation quantum.
 *  @Input alloc - a resource allocation callback or 0.
 *  @Input free - a resource de-allocation callback or 0.
 *  @Input import_handle - handle passed to alloc and free or 0.
 *  @Return arena handle, or IMG_NULL.
 */
RA_ARENA *
RA_Create (IMG_CHAR *name,
           IMG_UINTPTR_T base,
           IMG_SIZE_T uSize,
           BM_MAPPING *psMapping,
           IMG_SIZE_T uQuantum, 
           IMG_BOOL (*imp_alloc)(IMG_VOID *_h,
                                IMG_SIZE_T uSize,
                                IMG_SIZE_T *pActualSize,
                                BM_MAPPING **ppsMapping,
                                IMG_UINT32 uFlags,
								IMG_PVOID pvPrivData,
								IMG_UINT32 ui32PrivDataLength,
                                IMG_UINTPTR_T *pBase),
           IMG_VOID (*imp_free) (IMG_VOID *,
                                IMG_UINTPTR_T,
                                BM_MAPPING *),
           IMG_VOID (*backingstore_free) (IMG_VOID *,
                                          IMG_SIZE_T,
                                          IMG_SIZE_T,
                                          IMG_HANDLE),
           IMG_VOID *import_handle);

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
 *  @Function   RA_TestDelete
 *
 *  @Description
 *
 *  To test whether it is safe to delete a resource arena. If any allocations
 *	have not been freed, the RA must not be deleted.
 *                  
 *  @Input  pArena - the arena to test.
 *  @Return IMG_BOOL - IMG_TRUE if is safe to go on and call RA_Delete.
 */
IMG_BOOL
RA_TestDelete (RA_ARENA *pArena);

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
 *  @Return IMG_TRUE - success, IMG_FALSE - failure
 */
IMG_BOOL
RA_Add (RA_ARENA *pArena, IMG_UINTPTR_T base, IMG_SIZE_T uSize);

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
 *  @Output ppsMapping - the user reference associated with allocated
 *          resource span.
 *  @Input  uFlags - flags influencing allocation policy.
 *  @Input  uAlignment - the alignment constraint required for the
 *          allocated segment, use 0 if alignment not required.
 *	@Input  uAlignmentOffset - the required alignment offset
 *  @Input  pvPrivData - private data passed to OS allocator
 *  @Input  ui32PrivData - length of private data
 *
 *  @Output pBase - allocated base resource
 *  @Return IMG_TRUE - success, IMG_FALSE - failure
 */
IMG_BOOL
RA_Alloc (RA_ARENA *pArena, 
          IMG_SIZE_T uSize,
          IMG_SIZE_T *pActualSize,
          BM_MAPPING **ppsMapping, 
          IMG_UINT32 uFlags,
          IMG_UINT32 uAlignment,
		  IMG_UINT32 uAlignmentOffset,
		  IMG_PVOID pvPrivData,
		  IMG_UINT32 ui32PrivDataLength,
          IMG_UINTPTR_T *pBase);

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
RA_Free (RA_ARENA *pArena, IMG_UINTPTR_T base, IMG_BOOL bFreeBackingStore);


#ifdef RA_STATS

#define CHECK_SPACE(total)					\
{											\
	if((total)<100) 							\
		return PVRSRV_ERROR_INVALID_PARAMS;	\
}

#define UPDATE_SPACE(str, count, total)		\
{											\
	if((count) == -1)					 		\
		return PVRSRV_ERROR_INVALID_PARAMS;	\
	else									\
	{										\
		(str) += (count);						\
		(total) -= (count);						\
	}										\
}


/**
 * @Function    RA_GetNextLiveSegment
 * 
 * @Description Returns details of the next live resource segments
 * 
 * @Input       pArena - the arena the segment was originally allocated from.
 * @Output      psSegDetails - rtn details of segments
 * 
 * @Return      IMG_TRUE if operation succeeded
 */
IMG_BOOL RA_GetNextLiveSegment(IMG_HANDLE hArena, RA_SEGMENT_DETAILS *psSegDetails);


/**
 *  @Function   RA_GetStats
 *
 *  @Description    gets stats on a given arena
 *  
 *  @Input  pArena - the arena the segment was originally allocated from.
 *  @Input  ppszStr - string to write stats to 
 *	@Input	pui32StrLen - length of string
 *
 *  @Return PVRSRV_ERROR
 */
PVRSRV_ERROR RA_GetStats(RA_ARENA *pArena,
							IMG_CHAR **ppszStr, 
							IMG_UINT32 *pui32StrLen);

PVRSRV_ERROR RA_GetStatsFreeMem(RA_ARENA *pArena,
								IMG_CHAR **ppszStr, 
								IMG_UINT32 *pui32StrLen);

#endif /* #ifdef RA_STATS */

#endif

