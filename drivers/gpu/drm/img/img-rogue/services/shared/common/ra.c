/*************************************************************************/ /*!
@File
@Title          Resource Allocator
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

@Description
 Implements generic resource allocation. The resource allocator was originally
 intended to manage address spaces. In practice the resource allocator is
 generic and can manage arbitrary sets of integers.

 Resources are allocated from arenas. Arenas can be created with an initial
 span of resources. Further resources spans can be added to arenas. A
 callback mechanism allows an arena to request further resource spans on
 demand.

 Each arena maintains an ordered list of resource segments each described by a
 boundary tag. Each boundary tag describes a segment of resources which are
 either 'free', available for allocation, or 'busy' currently allocated.
 Adjacent 'free' segments are always coalesced to avoid fragmentation.

 For allocation, all 'free' segments are kept on lists of 'free' segments in
 a table index by pvr_log2(segment size) i.e., each table index n holds 'free'
 segments in the size range 2^n -> 2^(n+1) - 1.

 Allocation policy is based on an *almost* good fit strategy.

 Allocated segments are inserted into a self-scaling hash table which maps
 the base resource of the span to the relevant boundary tag. This allows the
 code to get back to the boundary tag without exporting explicit boundary tag
 references through the API.

 Each arena has an associated quantum size, all allocations from the arena are
 made in multiples of the basic quantum.

 On resource exhaustion in an arena, a callback if provided will be used to
 request further resources. Resource spans allocated by the callback mechanism
 will be returned when freed (through one of the two callbacks).
*/ /**************************************************************************/

/* Issues:
 * - flags, flags are passed into the resource allocator but are not currently used.
 * - determination, of import size, is currently braindead.
 * - debug code should be moved out to own module and #ifdef'd
 */

#include "img_types.h"
#include "img_defs.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "dllist.h"
#include "uniq_key_splay_tree.h"

#include "hash.h"
#include "ra.h"
#include "pvrsrv_memallocflags.h"

#include "osfunc.h"
#include "allocmem.h"
#include "lock.h"
#include "pvr_intrinsics.h"

/* The initial, and minimum size of the live address -> boundary tag structure
 * hash table. The value 64 is a fairly arbitrary choice. The hash table
 * resizes on demand so the value chosen is not critical.
 */
#define MINIMUM_HASH_SIZE (64)

/* #define RA_VALIDATE */

#if defined(__KLOCWORK__)
	/* Make sure Klocwork analyses all the code (including the debug one) */
	#if !defined(RA_VALIDATE)
		#define RA_VALIDATE
	#endif
#endif

#if !defined(PVRSRV_NEED_PVR_ASSERT) || !defined(RA_VALIDATE)
/* Disable the asserts unless explicitly told otherwise.
 * They slow the driver too much for other people
 */

#undef PVR_ASSERT
/* Use a macro that really do not do anything when compiling in release
 * mode!
 */
#define PVR_ASSERT(x)
#endif

/* boundary tags, used to describe a resource segment */
struct _BT_
{
	enum bt_type
	{
		btt_free,		/* free resource segment */
		btt_live		/* allocated resource segment */
	} type;

	unsigned int is_leftmost;
	unsigned int is_rightmost;
	unsigned int free_import;

	/* The base resource and extent of this segment */
	RA_BASE_T base;
	RA_LENGTH_T uSize;

	/* doubly linked ordered list of all segments within the arena */
	struct _BT_ *pNextSegment;
	struct _BT_ *pPrevSegment;

	/* doubly linked un-ordered list of free segments with the same flags. */
	struct _BT_ *next_free;
	struct _BT_ *prev_free;

	/* A user reference associated with this span, user references are
	 * currently only provided in the callback mechanism
	 */
	IMG_HANDLE hPriv;

	/* Flags to match on this span */
	RA_FLAGS_T uFlags;

};
typedef struct _BT_ BT;


/* resource allocation arena */
struct _RA_ARENA_
{
	/* arena name for diagnostics output */
	IMG_CHAR name[RA_MAX_NAME_LENGTH];

	/* Spans / Imports within this arena are at least quantum sized
	 * and are a multiple of the uQuantum. This also has the effect of
	 * aligning these Spans to the uQuantum.
	 */
	RA_LENGTH_T uQuantum;

	/* import interface, if provided */
	PFN_RA_ALLOC pImportAlloc;

	PFN_RA_FREE pImportFree;

	/* Arbitrary handle provided by arena owner to be passed into the
	 * import alloc and free hooks
	 */
	void *pImportHandle;

	IMG_PSPLAY_TREE per_flags_buckets;

	/* resource segment list */
	BT *pHeadSegment;

	/* segment address to boundary tag hash table */
	HASH_TABLE *pSegmentHash;

	/* Lock for this arena */
	POS_LOCK hLock;

	/* Policies that govern the resource area */
	RA_POLICY_T ui32PolicyFlags;

	/* LockClass of this arena. This is used within lockdep to decide if a
	 * recursive call sequence with the same lock class is allowed or not.
	 */
	IMG_UINT32 ui32LockClass;

	/* Total Size of the Arena */
	IMG_UINT64	ui64TotalArenaSize;

	/* Size available for allocation in the arena */
	IMG_UINT64	ui64FreeArenaSize;

};

struct _RA_ARENA_ITERATOR_
{
	RA_ARENA *pArena;
	BT *pCurrent;
	IMG_BOOL bIncludeFreeSegments;
};

static PVRSRV_ERROR _RA_FreeMultiUnlocked(RA_ARENA *pArena,
                                   RA_BASE_ARRAY_T aBaseArray,
                                   RA_BASE_ARRAY_SIZE_T uiBaseArraySize);
static PVRSRV_ERROR
_RA_FreeMultiUnlockedSparse(RA_ARENA *pArena,
                             RA_BASE_ARRAY_T aBaseArray,
                             RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                             RA_LENGTH_T uiChunkSize,
                             IMG_UINT32 *puiFreeIndices,
                             IMG_UINT32 *puiFreeCount);

/*************************************************************************/ /*!
@Function       _RequestAllocFail
@Description    Default callback allocator used if no callback is specified,
                always fails to allocate further resources to the arena.
@Input          _h - callback handle
@Input          _uSize - requested allocation size
@Input          _uflags - allocation flags
@Input          _uBaseAlignment - Alignment for the returned allocated base
@Input          _pBase - receives allocated base
@Output         _pActualSize - actual allocation size
@Input          _pRef - user reference
@Return         PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL, this function always fails
                to allocate.
*/ /**************************************************************************/
static PVRSRV_ERROR
_RequestAllocFail(RA_PERARENA_HANDLE _h,
                  RA_LENGTH_T _uSize,
                  RA_FLAGS_T _uFlags,
                  RA_LENGTH_T _uBaseAlignment,
                  const IMG_CHAR *_pszAnnotation,
                  RA_BASE_T *_pBase,
                  RA_LENGTH_T *_pActualSize,
                  RA_PERISPAN_HANDLE *_phPriv)
{
	PVR_UNREFERENCED_PARAMETER(_h);
	PVR_UNREFERENCED_PARAMETER(_uSize);
	PVR_UNREFERENCED_PARAMETER(_pActualSize);
	PVR_UNREFERENCED_PARAMETER(_phPriv);
	PVR_UNREFERENCED_PARAMETER(_uFlags);
	PVR_UNREFERENCED_PARAMETER(_uBaseAlignment);
	PVR_UNREFERENCED_PARAMETER(_pBase);
	PVR_UNREFERENCED_PARAMETER(_pszAnnotation);

	return PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL;
}


#if defined(PVR_CTZLL)
	/* Make sure to trigger an error if someone change the buckets or the bHasEltsMapping size
	   the bHasEltsMapping is used to quickly determine the smallest bucket containing elements.
	   therefore it must have at least as many bits has the buckets array have buckets. The RA
	   implementation actually uses one more bit. */
	static_assert(ARRAY_SIZE(((IMG_PSPLAY_TREE)0)->buckets)
				  < 8 * sizeof(((IMG_PSPLAY_TREE) 0)->bHasEltsMapping),
				  "Too many buckets for bHasEltsMapping bitmap");
#endif


/*************************************************************************/ /*!
@Function       pvr_log2
@Description    Computes the floor of the log base 2 of a unsigned integer
@Input          n       Unsigned integer
@Return         Floor(Log2(n))
*/ /**************************************************************************/
#if defined(PVR_CLZLL)
/* make sure to trigger a problem if someone changes the RA_LENGTH_T type
   indeed the __builtin_clzll is for unsigned long long variables.

   if someone changes RA_LENGTH to unsigned long, then use __builtin_clzl
   if it changes to unsigned int, use __builtin_clz

   if it changes for something bigger than unsigned long long,
   then revert the pvr_log2 to the classic implementation */
static_assert(sizeof(RA_LENGTH_T) == sizeof(unsigned long long),
			  "RA log routines not tuned for sizeof(RA_LENGTH_T)");

static inline IMG_UINT32 pvr_log2(RA_LENGTH_T n)
{
	PVR_ASSERT(n != 0); /* Log2 is not defined on 0 */

	return (8 * sizeof(RA_LENGTH_T)) - 1 - PVR_CLZLL(n);
}
#else
static IMG_UINT32
pvr_log2(RA_LENGTH_T n)
{
	IMG_UINT32 l = 0;

	PVR_ASSERT(n != 0); /* Log2 is not defined on 0 */

	n >>= 1;
	while (n > 0)
	{
		n >>= 1;
		l++;
	}
	return l;
}
#endif

static INLINE void _FreeTableLimitBoundsCheck(IMG_UINT32 *uiIndex)
{
	if (*uiIndex >= FREE_TABLE_LIMIT)
	{
		PVR_DPF((PVR_DBG_ERROR, "Index exceeds FREE_TABLE_LIMIT (1TB), "
		                        "Clamping Index to FREE_TABLE_LIMIT"));
		*uiIndex = FREE_TABLE_LIMIT - 1;
	}
}


#if defined(RA_VALIDATE)
/*************************************************************************/ /*!
@Function       _IsInSegmentList
@Description    Tests if a BT is in the segment list.
@Input          pArena           The arena.
@Input          pBT              The boundary tag to look for.
@Return         IMG_FALSE  BT was not in the arena's segment list.
                IMG_TRUE   BT was in the arena's segment list.
*/ /**************************************************************************/
static IMG_BOOL
_IsInSegmentList(RA_ARENA *pArena, BT *pBT)
{
	BT* pBTScan;

	PVR_ASSERT(pArena != NULL);
	PVR_ASSERT(pBT != NULL);

	/* Walk the segment list until we see the BT pointer... */
	pBTScan = pArena->pHeadSegment;
	while (pBTScan != NULL  &&  pBTScan != pBT)
	{
		pBTScan = pBTScan->pNextSegment;
	}

	/* Test if we found it and then return */
	return (pBTScan == pBT);
}

/*************************************************************************/ /*!
@Function       _IsInFreeList
@Description    Tests if a BT is in the free list.
@Input          pArena           The arena.
@Input          pBT              The boundary tag to look for.
@Return         IMG_FALSE  BT was not in the arena's free list.
                IMG_TRUE   BT was in the arena's free list.
*/ /**************************************************************************/
static IMG_BOOL
_IsInFreeList(RA_ARENA *pArena, BT *pBT)
{
	BT* pBTScan;
	IMG_UINT32 uIndex;

	PVR_ASSERT(pArena != NULL);
	PVR_ASSERT(pBT != NULL);

	/* Look for the free list that holds BTs of this size... */
	uIndex = pvr_log2(pBT->uSize);
	PVR_ASSERT(uIndex < FREE_TABLE_LIMIT);

	pArena->per_flags_buckets = PVRSRVSplay(pBT->uFlags, pArena->per_flags_buckets);
	if ((pArena->per_flags_buckets == NULL) || (pArena->per_flags_buckets->flags != pBT->uFlags))
	{
		return 0;
	}
	else
	{
		pBTScan = pArena->per_flags_buckets->buckets[uIndex];
		while (pBTScan != NULL  &&  pBTScan != pBT)
		{
			pBTScan = pBTScan->next_free;
		}

		/* Test if we found it and then return */
		return (pBTScan == pBT);
	}
}

/* is_arena_valid should only be used in debug mode.
 * It checks that some properties an arena must have are verified
 */
static int is_arena_valid(struct _RA_ARENA_ *arena)
{
	struct _BT_ *chunk;
#if defined(PVR_CTZLL)
	unsigned int i;
#endif

	for (chunk = arena->pHeadSegment; chunk != NULL; chunk = chunk->pNextSegment)
	{
		/* if next segment is NULL, then it must be a rightmost */
		PVR_ASSERT((chunk->pNextSegment != NULL) || (chunk->is_rightmost));
		/* if prev segment is NULL, then it must be a leftmost */
		PVR_ASSERT((chunk->pPrevSegment != NULL) || (chunk->is_leftmost));

		if (chunk->type == btt_free)
		{
			/* checks the correctness of the type field */
			PVR_ASSERT(_IsInFreeList(arena, chunk));

			/* check that there can't be two consecutive free chunks.
			   Indeed, instead of having two consecutive free chunks,
			   there should be only one that span the size of the two. */
			PVR_ASSERT((chunk->is_leftmost) || (chunk->pPrevSegment->type != btt_free));
			PVR_ASSERT((chunk->is_rightmost) || (chunk->pNextSegment->type != btt_free));
		}
		else
		{
			/* checks the correctness of the type field */
			PVR_ASSERT(!_IsInFreeList(arena, chunk));
		}

		PVR_ASSERT((chunk->is_leftmost) || (chunk->pPrevSegment->base + chunk->pPrevSegment->uSize == chunk->base));
		PVR_ASSERT((chunk->is_rightmost) || (chunk->base + chunk->uSize == chunk->pNextSegment->base));

		/* all segments of the same imports must have the same flags ... */
		PVR_ASSERT((chunk->is_rightmost) || (chunk->uFlags == chunk->pNextSegment->uFlags));
		/* ... and the same import handle */
		PVR_ASSERT((chunk->is_rightmost) || (chunk->hPriv == chunk->pNextSegment->hPriv));


		/* if a free chunk spans a whole import, then it must be an 'not to free import'.
		   Otherwise it should have been freed. */
		PVR_ASSERT((!chunk->is_leftmost) || (!chunk->is_rightmost) || (chunk->type == btt_live) || (!chunk->free_import));
	}

#if defined(PVR_CTZLL)
	if (arena->per_flags_buckets != NULL)
	{
		for (i = 0; i < FREE_TABLE_LIMIT; ++i)
		{
			/* verify that the bHasEltsMapping is correct for this flags bucket */
			PVR_ASSERT(
				((arena->per_flags_buckets->buckets[i] == NULL) &&
				 (((arena->per_flags_buckets->bHasEltsMapping & ((IMG_ELTS_MAPPINGS) 1 << i)) == 0)))
				||
				((arena->per_flags_buckets->buckets[i] != NULL) &&
				 (((arena->per_flags_buckets->bHasEltsMapping & ((IMG_ELTS_MAPPINGS) 1 << i)) != 0)))
				);
		}
	}
#endif

	/* if arena was not valid, an earlier assert should have triggered */
	return 1;
}
#endif

/*************************************************************************/ /*!
@Function       _SegmentListInsertAfter
@Description    Insert a boundary tag into an arena segment list after a
                specified boundary tag.
@Input          pInsertionPoint  The insertion point.
@Input          pBT              The boundary tag to insert.
*/ /**************************************************************************/
static INLINE void
_SegmentListInsertAfter(BT *pInsertionPoint,
                        BT *pBT)
{
	PVR_ASSERT(pBT != NULL);
	PVR_ASSERT(pInsertionPoint != NULL);

	pBT->pNextSegment = pInsertionPoint->pNextSegment;
	pBT->pPrevSegment = pInsertionPoint;
	if (pInsertionPoint->pNextSegment != NULL)
	{
		pInsertionPoint->pNextSegment->pPrevSegment = pBT;
	}
	pInsertionPoint->pNextSegment = pBT;
}

/*************************************************************************/ /*!
@Function       _SegmentListInsert
@Description    Insert a boundary tag into an arena segment list
@Input          pArena    The arena.
@Input          pBT       The boundary tag to insert.
*/ /**************************************************************************/
static INLINE void
_SegmentListInsert(RA_ARENA *pArena, BT *pBT)
{
	PVR_ASSERT(!_IsInSegmentList(pArena, pBT));

	/* insert into the segment chain */
	pBT->pNextSegment = pArena->pHeadSegment;
	pArena->pHeadSegment = pBT;
	if (pBT->pNextSegment != NULL)
	{
		pBT->pNextSegment->pPrevSegment = pBT;
	}

	pBT->pPrevSegment = NULL;
}

/*************************************************************************/ /*!
@Function       _SegmentListRemove
@Description    Remove a boundary tag from an arena segment list.
@Input          pArena    The arena.
@Input          pBT       The boundary tag to remove.
*/ /**************************************************************************/
static void
_SegmentListRemove(RA_ARENA *pArena, BT *pBT)
{
	PVR_ASSERT(_IsInSegmentList(pArena, pBT));

	if (pBT->pPrevSegment == NULL)
		pArena->pHeadSegment = pBT->pNextSegment;
	else
		pBT->pPrevSegment->pNextSegment = pBT->pNextSegment;

	if (pBT->pNextSegment != NULL)
		pBT->pNextSegment->pPrevSegment = pBT->pPrevSegment;
}


/*************************************************************************/ /*!
@Function       _BuildBT
@Description    Construct a boundary tag for a free segment.
@Input          base     The base of the resource segment.
@Input          uSize    The extent of the resource segment.
@Input          uFlags   The flags to give to the boundary tag
@Return         Boundary tag or NULL
*/ /**************************************************************************/
static BT *
_BuildBT(RA_BASE_T base, RA_LENGTH_T uSize, RA_FLAGS_T uFlags)
{
	BT *pBT;

	pBT = OSAllocZMem(sizeof(BT));
	if (pBT == NULL)
	{
		return NULL;
	}

	pBT->is_leftmost = 1;
	pBT->is_rightmost = 1;
	/* pBT->free_import = 0; */
	pBT->type = btt_live;
	pBT->base = base;
	pBT->uSize = uSize;
	pBT->uFlags = uFlags;

	return pBT;
}


/*************************************************************************/ /*!
@Function       _SegmentSplit
@Description    Split a segment into two, maintain the arena segment list. The
                boundary tag should not be in the free table. Neither the
                original or the new neighbour boundary tag will be in the free
                table.
@Input          pBT       The boundary tag to split.
@Input          uSize     The required segment size of boundary tag after
                          splitting.
@Return         New neighbour boundary tag or NULL.
*/ /**************************************************************************/
static BT *
_SegmentSplit(BT *pBT, RA_LENGTH_T uSize)
{
	BT *pNeighbour;

	pNeighbour = _BuildBT(pBT->base + uSize, pBT->uSize - uSize, pBT->uFlags);
	if (pNeighbour == NULL)
	{
		return NULL;
	}

	_SegmentListInsertAfter(pBT, pNeighbour);

	pNeighbour->is_leftmost = 0;
	pNeighbour->is_rightmost = pBT->is_rightmost;
	pNeighbour->free_import = pBT->free_import;
	pBT->is_rightmost = 0;
	pNeighbour->hPriv = pBT->hPriv;
	pBT->uSize = uSize;
	pNeighbour->uFlags = pBT->uFlags;

	return pNeighbour;
}

/*************************************************************************/ /*!
@Function       _FreeListInsert
@Description    Insert a boundary tag into an arena free table.
@Input          pArena    The arena.
@Input          pBT       The boundary tag.
*/ /**************************************************************************/
static void
_FreeListInsert(RA_ARENA *pArena, BT *pBT)
{
	IMG_UINT32 uIndex;
	BT *pBTTemp = NULL;
	uIndex = pvr_log2(pBT->uSize);

	_FreeTableLimitBoundsCheck(&uIndex);

	PVR_ASSERT(!_IsInFreeList(pArena, pBT));

	pBT->type = btt_free;

	pArena->per_flags_buckets = PVRSRVSplay(pBT->uFlags, pArena->per_flags_buckets);
	/* the flags item in the splay tree must have been created before-hand by
	   _InsertResource */
	PVR_ASSERT(pArena->per_flags_buckets != NULL);

	/* Handle NULL values for RELEASE builds and/or disabled ASSERT DEBUG builds */
	if (unlikely(pArena->per_flags_buckets == NULL))
	{
		return;
	}

	/* Get the first node in the bucket */
	pBTTemp = pArena->per_flags_buckets->buckets[uIndex];

	if (unlikely((pArena->ui32PolicyFlags & RA_POLICY_ALLOC_NODE_SELECT_MASK) == RA_POLICY_ALLOC_OPTIMAL))
	{
		/* Add the node to the start if the bucket is empty */
		if (NULL == pBTTemp)
		{
			pArena->per_flags_buckets->buckets[uIndex] = pBT;
			pBT->next_free = NULL;
			pBT->prev_free = NULL;

		}
		else
		{
			BT *pBTPrev = NULL;
			/* Traverse the list and identify the appropriate
			 * place based on the size of the Boundary being inserted */
			while (pBTTemp && (pBTTemp->uSize < pBT->uSize))
			{
				pBTPrev = pBTTemp;
				pBTTemp = pBTTemp->next_free;
			}
			/* point the new node to the first higher size element */
			pBT->next_free = pBTTemp;
			pBT->prev_free = pBTPrev;

			if (pBTPrev)
			{
				/* Set the lower size element in the
				 * chain to point new node */
				pBTPrev->next_free = pBT;
			}
			else
			{
				/* Assign the new node to the start of the bucket
				 * if the bucket is empty */
				pArena->per_flags_buckets->buckets[uIndex] = pBT;
			}
			/* Make sure the higher size element in the chain points back
			 * to the new node to be introduced */
			if (pBTTemp)
			{
				pBTTemp->prev_free = pBT;
			}
		}
	}
	else
	{
		pBT->next_free =  pBTTemp;
		if (pBT->next_free != NULL)
		{
			pBT->next_free->prev_free = pBT;
		}
		pBT->prev_free = NULL;
		pArena->per_flags_buckets->buckets[uIndex] = pBT;
	}

#if defined(PVR_CTZLL)
	/* tells that bucket[index] now contains elements */
	pArena->per_flags_buckets->bHasEltsMapping |= ((IMG_ELTS_MAPPINGS) 1 << uIndex);
#endif

}

/*************************************************************************/ /*!
@Function       _FreeListRemove
@Description    Remove a boundary tag from an arena free table.
@Input          pArena    The arena.
@Input          pBT       The boundary tag.
*/ /**************************************************************************/
static void
_FreeListRemove(RA_ARENA *pArena, BT *pBT)
{
	IMG_UINT32 uIndex;
	uIndex = pvr_log2(pBT->uSize);

	_FreeTableLimitBoundsCheck(&uIndex);

	PVR_ASSERT(_IsInFreeList(pArena, pBT));

	if (pBT->next_free != NULL)
	{
		pBT->next_free->prev_free = pBT->prev_free;
	}

	if (pBT->prev_free != NULL)
	{
		pBT->prev_free->next_free = pBT->next_free;
	}
	else
	{
		pArena->per_flags_buckets = PVRSRVSplay(pBT->uFlags, pArena->per_flags_buckets);
		/* the flags item in the splay tree must have already been created
		   (otherwise how could there be a segment with these flags */
		PVR_ASSERT(pArena->per_flags_buckets != NULL);

		/* Handle unlikely NULL values for RELEASE or ASSERT-disabled builds */
		if (unlikely(pArena->per_flags_buckets == NULL))
		{
			pBT->type = btt_live;
			return;
		}

		pArena->per_flags_buckets->buckets[uIndex] = pBT->next_free;
#if defined(PVR_CTZLL)
		if (pArena->per_flags_buckets->buckets[uIndex] == NULL)
		{
			/* there is no more elements in this bucket. Update the mapping. */
			pArena->per_flags_buckets->bHasEltsMapping &= ~((IMG_ELTS_MAPPINGS) 1 << uIndex);
		}
#endif
	}

	PVR_ASSERT(!_IsInFreeList(pArena, pBT));
	pBT->type = btt_live;
}


/*************************************************************************/ /*!
@Function       _InsertResource
@Description    Add a free resource segment to an arena.
@Input          pArena    The arena.
@Input          base      The base of the resource segment.
@Input          uSize     The extent of the resource segment.
@Input          uFlags    The flags of the new resources.
@Return         New bucket pointer
                NULL on failure
*/ /**************************************************************************/
static BT *
_InsertResource(RA_ARENA *pArena, RA_BASE_T base, RA_LENGTH_T uSize,
                RA_FLAGS_T uFlags)
{
	BT *pBT;
	PVR_ASSERT(pArena!=NULL);

	pBT = _BuildBT(base, uSize, uFlags);

	if (pBT != NULL)
	{
		IMG_PSPLAY_TREE tmp = PVRSRVInsert(pBT->uFlags, pArena->per_flags_buckets);
		if (tmp == NULL)
		{
			OSFreeMem(pBT);
			return NULL;
		}

		pArena->per_flags_buckets = tmp;
		_SegmentListInsert(pArena, pBT);
		_FreeListInsert(pArena, pBT);
	}
	return pBT;
}

/*************************************************************************/ /*!
@Function       _InsertResourceSpan
@Description    Add a free resource span to an arena, marked for free_import.
@Input          pArena    The arena.
@Input          base      The base of the resource segment.
@Input          uSize     The extent of the resource segment.
@Return         The boundary tag representing the free resource segment,
                or NULL on failure.
*/ /**************************************************************************/
static INLINE BT *
_InsertResourceSpan(RA_ARENA *pArena,
                    RA_BASE_T base,
                    RA_LENGTH_T uSize,
                    RA_FLAGS_T uFlags)
{
	BT *pBT = _InsertResource(pArena, base, uSize, uFlags);
	if (pBT != NULL)
	{
		pBT->free_import = 1;
	}
	return pBT;
}


/*************************************************************************/ /*!
@Function       _RemoveResourceSpan
@Description    Frees a resource span from an arena, returning the imported
                span via the callback.
@Input          pArena     The arena.
@Input          pBT        The boundary tag to free.
@Return         IMG_FALSE failure - span was still in use
                IMG_TRUE  success - span was removed and returned
*/ /**************************************************************************/
static INLINE IMG_BOOL
_RemoveResourceSpan(RA_ARENA *pArena, BT *pBT)
{
	PVR_ASSERT(pArena!=NULL);
	PVR_ASSERT(pBT!=NULL);

	if (pBT->free_import &&
		pBT->is_leftmost &&
		pBT->is_rightmost)
	{
		_SegmentListRemove(pArena, pBT);
		pArena->pImportFree(pArena->pImportHandle, pBT->base, pBT->hPriv);
		OSFreeMem(pBT);

		return IMG_TRUE;
	}

	return IMG_FALSE;
}

/*************************************************************************/ /*!
@Function       _FreeBT
@Description    Free a boundary tag taking care of the segment list and the
                boundary tag free table.
@Input          pArena     The arena.
@Input          pBT        The boundary tag to free.
*/ /**************************************************************************/
static void
_FreeBT(RA_ARENA *pArena, BT *pBT)
{
	BT *pNeighbour;

	PVR_ASSERT(pArena!=NULL);
	PVR_ASSERT(pBT!=NULL);
	PVR_ASSERT(!_IsInFreeList(pArena, pBT));

	/* try and coalesce with left neighbour */
	pNeighbour = pBT->pPrevSegment;
	if ((!pBT->is_leftmost)	&& (pNeighbour->type == btt_free))
	{
		/* Verify list correctness */
		PVR_ASSERT(pNeighbour->base + pNeighbour->uSize == pBT->base);

		_FreeListRemove(pArena, pNeighbour);
		_SegmentListRemove(pArena, pNeighbour);
		pBT->base = pNeighbour->base;

		pBT->uSize += pNeighbour->uSize;
		pBT->is_leftmost = pNeighbour->is_leftmost;
		OSFreeMem(pNeighbour);
	}

	/* try to coalesce with right neighbour */
	pNeighbour = pBT->pNextSegment;
	if ((!pBT->is_rightmost) && (pNeighbour->type == btt_free))
	{
		/* Verify list correctness */
		PVR_ASSERT(pBT->base + pBT->uSize == pNeighbour->base);

		_FreeListRemove(pArena, pNeighbour);
		_SegmentListRemove(pArena, pNeighbour);
		pBT->uSize += pNeighbour->uSize;
		pBT->is_rightmost = pNeighbour->is_rightmost;
		OSFreeMem(pNeighbour);
	}

	if (_RemoveResourceSpan(pArena, pBT) == IMG_FALSE)
	{
		_FreeListInsert(pArena, pBT);
		PVR_ASSERT((!pBT->is_rightmost) || (!pBT->is_leftmost) || (!pBT->free_import));
	}

	PVR_ASSERT(is_arena_valid(pArena));
}


/*
  This function returns the first element in a bucket that can be split
  in a way that one of the sub-segments can meet the size and alignment
  criteria.

  The first_elt is the bucket to look into. Remember that a bucket is
  implemented as a pointer to the first element of the linked list.

  nb_max_try is used to limit the number of elements considered.
  This is used to only consider the first nb_max_try elements in the
  free-list. The special value ~0 is used to say unlimited i.e. consider
  all elements in the free list
 */
static INLINE
struct _BT_ *find_chunk_in_bucket(struct _BT_ * first_elt,
                                  RA_LENGTH_T uSize,
                                  RA_LENGTH_T uAlignment,
                                  unsigned int nb_max_try)
{
	struct _BT_ *walker;

	for (walker = first_elt; (walker != NULL) && (nb_max_try != 0); walker = walker->next_free)
	{
		const RA_BASE_T aligned_base = (uAlignment > 1) ?
			PVR_ALIGN(walker->base, uAlignment)
			: walker->base;

		if (walker->base + walker->uSize >= aligned_base + uSize)
		{
			return walker;
		}

		/* 0xFFFF...FFFF is used has nb_max_try = infinity. */
		if (nb_max_try != (unsigned int) ~0)
		{
			nb_max_try--;
		}
	}

	return NULL;
}

/*************************************************************************/ /*!
 *  @Function   _FreeMultiBaseArray
 *
 *  @Description   Given an array (Could be complete or partial reference)
 *                 free the region given as the array and size. This function
 *                 should be used only when it is known that multiple Real
 *                 bases will be freed from the array.
 *
 *  @Input  pArena - The RA Arena to free the bases on.
 *  @Input  aBaseArray - The Base array to free from
 *  @Input  uiBaseArraySize - The Size of the base array to free.
 *
 *  @Return PVRSRV_OK on Success, PVRSRV_ERROR code otherwise.
*/ /**************************************************************************/
static PVRSRV_ERROR
_FreeMultiBaseArray(RA_ARENA *pArena,
                    RA_BASE_ARRAY_T aBaseArray,
                    RA_BASE_ARRAY_SIZE_T uiBaseArraySize)
{
	IMG_UINT32 i;
	for (i = 0; i < uiBaseArraySize; i++)
	{
		if (RA_BASE_IS_REAL(aBaseArray[i]))
		{
			BT *pBT;
			pBT = (BT *) HASH_Remove_Extended(pArena->pSegmentHash, &aBaseArray[i]);

			if (pBT)
			{
				pArena->ui64FreeArenaSize += pBT->uSize;

				PVR_ASSERT(pBT->base == aBaseArray[i]);
				_FreeBT(pArena, pBT);
				aBaseArray[i] = INVALID_BASE_ADDR;
			}
			else
			{
				/* Did we attempt to remove a ghost page?
				 * Essentially the base was marked real but was actually a ghost.
				 */
				PVR_ASSERT(!"Attempt to free non-existing real base!");
				return PVRSRV_ERROR_INVALID_REQUEST;
			}
		}
#if defined(DEBUG)
		else
		{
			aBaseArray[i] = INVALID_BASE_ADDR;
		}
#endif
	}

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
 *  @Function   _FreeSingleBaseArray
 *
 *  @Description   Given an array (Could be complete or partial reference)
 *                 free the region given as the array and size. This function
 *                 should be used only when it is known that a single Real
 *                 base will be freed from the array. All Bases will be
 *                 sanitised after the real has been freed.
 *
 *  @Input  pArena - The RA Arena to free the bases on.
 *  @Input  aBaseArray - The Base array to free from, entry 0 should be a
 *                       Real base
 *  @Input  uiBaseArraySize - The Size of the base array to free.
 *
 *  @Return PVRSRV_OK on Success, PVRSRV_ERROR code otherwise.
*/ /**************************************************************************/
static PVRSRV_ERROR
_FreeSingleBaseArray(RA_ARENA *pArena,
                     RA_BASE_ARRAY_T aBaseArray,
                     RA_BASE_ARRAY_SIZE_T uiBaseArraySize)
{
	BT *pBT;
	PVR_ASSERT(RA_BASE_IS_REAL(aBaseArray[0]));

	pBT = (BT *) HASH_Remove_Extended(pArena->pSegmentHash, &aBaseArray[0]);

	if (pBT)
	{
		pArena->ui64FreeArenaSize += pBT->uSize;

		PVR_ASSERT(pBT->base == aBaseArray[0]);
		_FreeBT(pArena, pBT);
	}
	else
	{
		/* Did we attempt to remove a ghost page?
		 * Essentially the base was marked real but was actually ghost.
		 */
		PVR_ASSERT(!"Attempt to free non-existing real base!");
		return PVRSRV_ERROR_INVALID_REQUEST;
	}

	/* Set all entries to INVALID_BASE_ADDR */
	OSCachedMemSet(aBaseArray, 0xFF, uiBaseArraySize * sizeof(RA_BASE_T));

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
 *  @Function   _GenerateGhostBases
 *
 *  @Description   Given an array (Could be complete or partial reference)
 *                 generate Ghost bases for the allocation and size.
 *
 *  @Input  uiBase - The Real base to generate Ghost Bases from.
 *  @Input  uiBaseSize - The size of the Real Base
 *  @Input  uiChunkSize - The Base chunk size used to generate Ghost
 *                           bases on specific boundaries.
 *  @Input  aBaseArray - The array to add the Ghost bases to.
 *
 *  @Return array index of element past last Ghost base of given array.
*/ /**************************************************************************/
static IMG_UINT32
_GenerateGhostBases(RA_BASE_T uiRealBase,
                    RA_LENGTH_T uiBaseSize,
                    RA_LENGTH_T uiChunkSize,
                    RA_BASE_ARRAY_T aBaseArray)
{
	IMG_UINT32 ui32Index = 0;
	RA_LENGTH_T uiRemaining = uiBaseSize - uiChunkSize;
	RA_LENGTH_T uiCurrentBase = uiRealBase + uiChunkSize;
	aBaseArray[ui32Index] = uiRealBase;

	for (ui32Index = 1; uiRemaining != 0; ui32Index++)
	{
		aBaseArray[ui32Index] = RA_BASE_SET_GHOST_BIT(uiCurrentBase);
		uiCurrentBase += uiChunkSize;
		uiRemaining -= uiChunkSize;
	}

	return ui32Index;
}

/*************************************************************************/ /*!
 *  @Function   _FindRealBaseFromGhost
 *
 *  @Description   Given an array and an index into that array for the Ghost Base
 *                 find the Real Base hosting the Ghost base in the RA.
 *  @Input aBaseArray - The array the Ghost and Real base reside on.
 *  @Input ui32GhostBaseIndex - The index into the given array for the Ghost Base.
 *  @Output pRealBase - The Real Base hosting the Ghost base.
 *  @Output pui32RealBaseIndex -  The index of the Real Base found in the array.
 *
 *  @Return None.
*/ /**************************************************************************/
static void
_FindRealBaseFromGhost(RA_BASE_ARRAY_T aBaseArray,
                       IMG_UINT32 ui32GhostBaseIndex,
                       RA_BASE_T *pRealBase,
                       IMG_UINT32 *pui32RealBaseIndex)
{
	IMG_UINT32 ui32Index = ui32GhostBaseIndex;

	PVR_ASSERT(RA_BASE_IS_GHOST(aBaseArray[ui32GhostBaseIndex]));

	while (ui32Index != 0 &&
	       RA_BASE_IS_GHOST(aBaseArray[ui32Index]))
	{
		ui32Index--;
	}

	*pRealBase = aBaseArray[ui32Index];
	*pui32RealBaseIndex = ui32Index;
}

/*************************************************************************/ /*!
 *  @Function   _ConvertGhostBaseToReal
 *
 *  @Description   Convert the given Ghost Base to a Real Base in the
 *                 RA. This is mainly used in free paths so we can be
 *                 agile with memory regions.
 *  @Input pArena - The RA Arena to convert the base on.
 *  @Input aBaseArray - The Base array to convert the base on.
 *  @Input uiRealBase - The Base hosting the Ghost base to convert.
 *  @Input ui32RealBaseArrayIndex - The index in the array of the Real Base.
 *  @Input ui32GhostBaseArrayIndex - The index in the array of the Ghost Base.
 *  @Input uiChunkSize - The chunk size used to generate the Ghost bases on.
 *
 *  @Return PVRSRV_OK on Success, PVRSRV_ERROR code on Failure.
*/ /**************************************************************************/
static PVRSRV_ERROR
_ConvertGhostBaseToReal(RA_ARENA *pArena,
                        RA_BASE_ARRAY_T aBaseArray,
                        RA_BASE_T uiRealBase,
                        IMG_UINT32 ui32RealBaseArrayIndex,
                        IMG_UINT32 ui32GhostBaseArrayIndex,
                        RA_LENGTH_T uiChunkSize)
{
	BT *pOrigRealBT;
	BT *pNewRealBT;

	pOrigRealBT = (BT *) HASH_Retrieve_Extended(pArena->pSegmentHash, &uiRealBase);
	pNewRealBT = _SegmentSplit(pOrigRealBT,
	                           uiChunkSize *
	                            (ui32GhostBaseArrayIndex - ui32RealBaseArrayIndex));
	PVR_LOG_RETURN_IF_FALSE(pNewRealBT != NULL,
	                        "Unable to split BT, no memory available to allocate new BT",
	                        PVRSRV_ERROR_OUT_OF_MEMORY);

	if (!HASH_Insert_Extended(pArena->pSegmentHash, &pNewRealBT->base, (uintptr_t) pNewRealBT))
	{
		PVR_LOG_RETURN_IF_ERROR(PVRSRV_ERROR_UNABLE_TO_INSERT_HASH_VALUE, "HASH_Insert_Extended");
	}

	aBaseArray[ui32GhostBaseArrayIndex] = pNewRealBT->base;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
 *  @Function   _FreeGhostBasesFromReal
 *
 *  @Description   Given a ghost base and size, free the contiguous ghost bases from the
 *                 real base. This has the effect of shrinking the size of the real base.
 *                 If ghost pages remain after the free region, a new Real base will be
 *                 created to host them.
 *  @Input pArena - The RA Arena to free the Ghost Bases from.
 *  @Input aBaseArray - The array to remove bases from
 *  @Input uiBaseArraySize - The size of the Base array to free from.
 *  @Input uiChunkSize - The chunk size used to generate the Ghost Bases.
 *  @Input ui32GhostBaseIndex - The index into the array of the initial Ghost base to free
 *  @Input ui32FreeCount - The number of Ghost bases to free from the Real base.
 *
 *  @Return PVRSRV_OK on Success, PVRSRV_ERROR code on Failure.
*/ /**************************************************************************/
static PVRSRV_ERROR
_FreeGhostBasesFromReal(RA_ARENA *pArena,
                        RA_BASE_ARRAY_T aBaseArray,
                        RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                        RA_LENGTH_T uiChunkSize,
                        IMG_UINT32 ui32GhostBaseIndex,
                        IMG_UINT32 ui32FreeCount)
{
	PVRSRV_ERROR eError;
	RA_BASE_T uiRealBase;
	IMG_UINT32 ui32RealBaseIndex;
	IMG_UINT32 ui32FreeEndIndex;

	_FindRealBaseFromGhost(aBaseArray,
	                       ui32GhostBaseIndex,
	                       &uiRealBase,
	                       &ui32RealBaseIndex);

	/* Make the first Ghost Base to free, real. */
	eError = _ConvertGhostBaseToReal(pArena,
	                                 aBaseArray,
	                                 uiRealBase,
	                                 ui32RealBaseIndex,
	                                 ui32GhostBaseIndex,
	                                 uiChunkSize);
	PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");

	/* Calculate the Base after the last to free. */
	ui32FreeEndIndex = ui32GhostBaseIndex + ui32FreeCount;

	/*
	 * If the end of the free region is a Ghost base then we need to
	 * make it a real base so that we can free the intended middle region.
	 */
	if (ui32FreeEndIndex != uiBaseArraySize &&
	    RA_BASE_IS_GHOST(aBaseArray[ui32FreeEndIndex]))
	{
		eError = _ConvertGhostBaseToReal(pArena,
		                                 aBaseArray,
		                                 aBaseArray[ui32GhostBaseIndex],
		                                 ui32GhostBaseIndex,
		                                 ui32FreeEndIndex,
		                                 uiChunkSize);
		PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");
	}

	/* Free the region calculated */
	eError = _FreeSingleBaseArray(pArena,
	                              &aBaseArray[ui32GhostBaseIndex],
	                              ui32FreeCount);
	PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");

	return eError;
}

/*************************************************************************/ /*!
 *  @Function   _ConvertGhostBaseFreeReal
 *
 *  @Description   Used in the case that we want to keep some indices that are ghost pages
 *                 but the indices to free start with the real base. In this case we can
 *                 convert the keep point to a real base, then free the original real base
 *                 along with all ghost bases prior to the new real.
 *
 *  @Input pArena - The RA Arena to free the bases from.
 *  @Input aBaseArray - The Base array to free from.
 *  @Input uiChunkSize - The chunk size used to generate the Ghost bases.
 *  @Input uiGhostBaseIndex - The index into the array of the Ghost base to convert.
 *
 *  @Return PVRSRV_OK on Success, PVRSRV_ERROR code on Failure.
*/ /**************************************************************************/
static PVRSRV_ERROR
_ConvertGhostBaseFreeReal(RA_ARENA *pArena,
                          RA_BASE_ARRAY_T aBaseArray,
                          RA_LENGTH_T uiChunkSize,
                          IMG_UINT32 uiRealBaseIndex,
                          IMG_UINT32 uiGhostBaseIndex)
{
	PVRSRV_ERROR eError;
	RA_BASE_T uiRealBase = aBaseArray[uiRealBaseIndex];

	eError = _ConvertGhostBaseToReal(pArena,
	                                 aBaseArray,
	                                 uiRealBase,
	                                 uiRealBaseIndex,
	                                 uiGhostBaseIndex,
	                                 uiChunkSize);
	PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");

	eError = _FreeSingleBaseArray(pArena,
	                              &aBaseArray[uiRealBaseIndex],
	                              uiGhostBaseIndex - uiRealBaseIndex);
	PVR_LOG_RETURN_IF_ERROR(eError, "_FreeBaseArray");

	return eError;
}

/*************************************************************************/ /*!
 *  @Function   _FreeBaseArraySlice
 *
 *  @Description   Free Bases in an Array Slice.
 *                 This function assumes that the slice is within a single Real base alloc.
 *                 i.e the uiFreeStartIndex and uiFreeCount remain fully within a single real
 *                 base alloc and do not cross into another Real base region.
 *
 *  @Input pArena - The RA Arena to free bases from.
 *  @Input aBaseArray - The Base array to free from.
 *  @Input uiBaseArraySize - The size of the Base array to free from.
 *  @Input uiChunkSize - The base chunk size used to generate the Ghost bases.
 *  @Input uiFreeStartIndex - The index in the array to start freeing from
 *  @Input uiFreeCount - The number of bases to free.
 *
 *  @Return PVRSRV_OK on Success, PVRSRV_ERROR code on Failure.
*/ /**************************************************************************/
static PVRSRV_ERROR
_FreeBaseArraySlice(RA_ARENA *pArena,
                    RA_BASE_ARRAY_T aBaseArray,
                    RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                    RA_LENGTH_T uiChunkSize,
                    IMG_UINT32 uiFreeStartIndex,
                    IMG_UINT32 uiFreeCount)
{
	/*3 cases:
	 * Key: () = Region to Free
	 *      [R] = Newly Real
	 *      R = Real Base
	 *      G = Ghost Base
	 * 1. We free the whole Realbase (inc all Ghost bases)
	 *    e.g. (RGGGGG)
	 *    e.g. RGGG(R)RGG
	 * 2 .We free the Real base but not all the Ghost bases meaning the first
	 *    ghost base after the last freed will become a real base.
	 *    e.g. RGGGG(RGGGG)[R]GGG
	 *    e.g. (RGGGG)[R]GGGG
	 * 3. We free some ghost bases from the real base
	 *    e.g. RGGG(GGG)
	 *    e.g. RGGG(GGG)[R]GGG
	 *
	 * Invalid Scenarios:
	 * 1. RGG(GR)GGGRG
	 * 2. RGG(GRG)GGRG
	 * Higher levels should prevent these situations by ensuring that the free
	 * index and count always focus on a single real base.
	 * Scenario 1 & 2, correctly handled, would be a case 3. followed by a case 2.
	 */
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_FALSE(uiBaseArraySize >= uiFreeStartIndex &&
	                        uiBaseArraySize >= uiFreeStartIndex + (uiFreeCount - 1),
	                        "Free Index given out of array bounds",
	                        PVRSRV_ERROR_INVALID_PARAMS);

	/* Find which case we have */

	/* Case 1 or 2 */
	if (RA_BASE_IS_REAL(aBaseArray[uiFreeStartIndex]))
	{
		/* Case 1 */
		if (uiFreeStartIndex + uiFreeCount == uiBaseArraySize ||
		    RA_BASE_IS_REAL(aBaseArray[uiFreeStartIndex + uiFreeCount]) ||
		    RA_BASE_IS_INVALID(aBaseArray[uiFreeStartIndex + uiFreeCount]))
		{
			eError = _FreeSingleBaseArray(pArena,
			                              &aBaseArray[uiFreeStartIndex],
			                              uiFreeCount);
			PVR_LOG_RETURN_IF_ERROR(eError, "_FreeBaseArray");
		}
		/* Case 2*/
		else
		{
			eError = _ConvertGhostBaseFreeReal(pArena,
			                                   aBaseArray,
			                                   uiChunkSize,
			                                   uiFreeStartIndex,
			                                   uiFreeStartIndex + uiFreeCount);
			PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");
		}
	}
	/* Case 3 */
	else if (RA_BASE_IS_GHOST(aBaseArray[uiFreeStartIndex]))
	{
		eError = _FreeGhostBasesFromReal(pArena,
		                                 aBaseArray,
		                                 uiBaseArraySize,
		                                 uiChunkSize,
		                                 uiFreeStartIndex,
		                                 uiFreeCount);
		PVR_LOG_RETURN_IF_ERROR(eError, "_FreeGhostBasesFromReal");
	}
	/* Attempt to free an invalid base, this could be a duplicated
	 * value in the free sparse index array */
	else
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "Attempt to free already free base Index %u", uiFreeStartIndex));
		PVR_ASSERT(!"Attempted double free.")
		return PVRSRV_ERROR_RA_FREE_INVALID_CHUNK;
	}

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       _AllocAlignSplit
@Description    Given a valid BT, trim the start and end of the BT according
                to alignment and size requirements. Also add the resulting
                BT to the live hash table.
@Input          pArena       The arena.
@Input          pBT          The BT to trim and add to live hash table
@Input          uSize        The requested allocation size.
@Input          uAlignment   The alignment requirements of the allocation
                             Required uAlignment, or 0.
                             Must be a power of 2 if not 0
@Output         pBase        Allocated, corrected, resource base
                             (non-optional, must not be NULL)
@Output         phPriv       The user references associated with
                             the imported segment. (optional)
@Return         IMG_FALSE failure
                IMG_TRUE success
*/ /**************************************************************************/
static IMG_BOOL
_AllocAlignSplit(RA_ARENA *pArena,
                 BT *pBT,
                 RA_LENGTH_T uSize,
                 RA_LENGTH_T uAlignment,
                 RA_BASE_T *pBase,
                 RA_PERISPAN_HANDLE *phPriv)
{
	RA_BASE_T aligned_base;

	aligned_base = (uAlignment > 1) ? PVR_ALIGN(pBT->base, uAlignment) : pBT->base;

	_FreeListRemove(pArena, pBT);

	if ((pArena->ui32PolicyFlags & RA_POLICY_NO_SPLIT_MASK) == RA_POLICY_NO_SPLIT)
	{
		goto nosplit;
	}

	/* with uAlignment we might need to discard the front of this segment */
	if (aligned_base > pBT->base)
	{
		BT *pNeighbour;
		pNeighbour = _SegmentSplit(pBT, (RA_LENGTH_T)(aligned_base - pBT->base));
		/* partition the buffer, create a new boundary tag */
		if (pNeighbour == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Front split failed", __func__));
			/* Put pBT back in the list */
			_FreeListInsert(pArena, pBT);
			return IMG_FALSE;
		}

		_FreeListInsert(pArena, pBT);
		pBT = pNeighbour;
	}

	/* the segment might be too big, if so, discard the back of the segment */
	if (pBT->uSize > uSize)
	{
		BT *pNeighbour;
		pNeighbour = _SegmentSplit(pBT, uSize);
		/* partition the buffer, create a new boundary tag */
		if (pNeighbour == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Back split failed", __func__));
			/* Put pBT back in the list */
			_FreeListInsert(pArena, pBT);
			return IMG_FALSE;
		}

		_FreeListInsert(pArena, pNeighbour);
	}
nosplit:
	pBT->type = btt_live;

	if (!HASH_Insert_Extended(pArena->pSegmentHash, &aligned_base, (uintptr_t)pBT))
	{
		_FreeBT(pArena, pBT);
		return IMG_FALSE;
	}

	if (phPriv != NULL)
		*phPriv = pBT->hPriv;

	*pBase = aligned_base;

	return IMG_TRUE;
}

/*************************************************************************/ /*!
@Function       _AttemptAllocAligned
@Description    Attempt an allocation from an arena.
@Input          pArena       The arena.
@Input          uSize        The requested allocation size.
@Input          uFlags       Allocation flags
@Output         phPriv       The user references associated with
                             the imported segment. (optional)
@Input          uAlignment   Required uAlignment, or 0.
                             Must be a power of 2 if not 0
@Output         base         Allocated resource base (non-optional, must not
                             be NULL)
@Return         IMG_FALSE failure
                IMG_TRUE success
*/ /**************************************************************************/
static IMG_BOOL
_AttemptAllocAligned(RA_ARENA *pArena,
                     RA_LENGTH_T uSize,
                     RA_FLAGS_T uFlags,
                     RA_LENGTH_T uAlignment,
                     RA_BASE_T *base,
                     RA_PERISPAN_HANDLE *phPriv) /* this is the "per-import" private data */
{

	IMG_UINT32 index_low;
	IMG_UINT32 index_high;
	IMG_UINT32 i;
	struct _BT_ *pBT = NULL;

	PVR_ASSERT(pArena!=NULL);
	PVR_ASSERT(base != NULL);

	pArena->per_flags_buckets = PVRSRVSplay(uFlags, pArena->per_flags_buckets);
	if ((pArena->per_flags_buckets == NULL) || (pArena->per_flags_buckets->uiFlags != uFlags))
	{
		/* no chunks with these flags. */
		return IMG_FALSE;
	}

	index_low = pvr_log2(uSize);
	if (uAlignment)
	{
		index_high = pvr_log2(uSize + uAlignment - 1);
	}
	else
	{
		index_high = index_low;
	}

	_FreeTableLimitBoundsCheck(&index_high);
	_FreeTableLimitBoundsCheck(&index_low);

	PVR_ASSERT(index_low <= index_high);

	if (unlikely((pArena->ui32PolicyFlags & RA_POLICY_BUCKET_MASK) == RA_POLICY_BUCKET_BEST_FIT))
	{
		/* This policy ensures the selection of the first lowest size bucket that
		 * satisfies the request size is selected */
#if defined(PVR_CTZLL)
		i = PVR_CTZLL((~(((IMG_ELTS_MAPPINGS)1 << (index_low )) - 1)) & pArena->per_flags_buckets->bHasEltsMapping);
#else
		i = index_low;
#endif
		for ( ; (i < FREE_TABLE_LIMIT) && (pBT == NULL); ++i)
		{
			if (pArena->per_flags_buckets->buckets[i])
			{
				pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], uSize, uAlignment, (unsigned int) ~0);
			}
		}
	}
	else
	{
#if defined(PVR_CTZLL)
		i = PVR_CTZLL((~(((IMG_ELTS_MAPPINGS)1 << (index_high + 1)) - 1)) & pArena->per_flags_buckets->bHasEltsMapping);
#else
		for (i = index_high + 1; (i < FREE_TABLE_LIMIT) && (pArena->per_flags_buckets->buckets[i] == NULL); ++i)
		{
		}
#endif
		PVR_ASSERT(i <= FREE_TABLE_LIMIT);

		if (i != FREE_TABLE_LIMIT)
		{
			/* since we start at index_high + 1, we are guaranteed to exit */
			pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], uSize, uAlignment, 1);
		}
		else
		{
			for (i = index_high; (i != index_low - 1) && (pBT == NULL); --i)
			{
				pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], uSize, uAlignment, (unsigned int) ~0);
			}
		}
	}

	if (pBT == NULL)
	{
		return IMG_FALSE;
	}

	return _AllocAlignSplit(pArena, pBT, uSize, uAlignment, base, phPriv);
}

/*************************************************************************/ /*!
@Function       _AttemptAllocAlignedAssured
@Description    Attempt an allocation from an arena. If the arena allows
                non-contiguous allocations, the allocation is guaranteed
                given there is enough memory to satisfy the full allocation.
@Input          pArena              The arena.
@Input          uSize               The requested allocation size.
@Input          uLog2MinContigSize  The Log2 minimum contiguity of the bases returned.
@Input          uFlags              Allocation flags
@Input          uAlignment          Required uAlignment, or 0.
                                    Must be a power of 2 if not 0
@Input          aBaseArray          Array to allocate bases to.
@Input          bSparseAlloc        Is the allocation we are making sparse.
@Output         bPhysContig         Is the allocation we made physically contiguous
                                    or did we use the scoop logic
@Return         Success: PVRSRV_OK
                Fail:    PVRSRV_ERROR code.
*/ /**************************************************************************/
static PVRSRV_ERROR
_AttemptAllocAlignedAssured(RA_ARENA *pArena,
                            RA_LENGTH_T uSize,
                            IMG_UINT32 uLog2MinContigSize,
                            RA_FLAGS_T uFlags,
                            RA_LENGTH_T uAlignment,
                            RA_BASE_ARRAY_T aBaseArray,
                            IMG_BOOL bSparseAlloc,
                            IMG_BOOL *bPhysContig)
{
	IMG_UINT32 index_low;  /* log2 Lowest contiguity required */
	IMG_UINT32 index_high; /* log2 Size of full alloc */
	IMG_UINT32 i;
	struct _BT_ *pBT = NULL;
	RA_PERISPAN_HANDLE phPriv;
	RA_LENGTH_T uiRemaining = uSize;
	RA_BASE_T uiBase;
	IMG_UINT32 uiCurrentArrayIndex = 0;

	PVR_ASSERT(pArena != NULL);

	pArena->per_flags_buckets = PVRSRVSplay(uFlags, pArena->per_flags_buckets);
	if ((pArena->per_flags_buckets == NULL) || (pArena->per_flags_buckets->uiFlags != uFlags))
	{
		/* no chunks with these flags. */
		return PVRSRV_ERROR_RA_NO_RESOURCE_WITH_FLAGS;
	}

	if (pArena->ui64FreeArenaSize < uSize)
	{
		/* Not enough memory to accommodate kick back for a chance to import more */
		return PVRSRV_ERROR_RA_OUT_OF_RESOURCE;
	}

	if (uLog2MinContigSize && uAlignment)
	{
		index_low = uLog2MinContigSize;
		index_high = pvr_log2(uSize);
	}
	else if (uLog2MinContigSize)
	{
		index_low = uLog2MinContigSize;
		index_high = pvr_log2(uSize);
	}
	else if (uAlignment)
	{
		index_low = 0;
		index_high = pvr_log2(uSize + uAlignment - 1);
	}
	else
	{
		index_low = 0;
		index_high = pvr_log2(uSize);
	}

	PVR_ASSERT(index_low < FREE_TABLE_LIMIT);
	PVR_ASSERT(index_high < FREE_TABLE_LIMIT);
	PVR_ASSERT(index_low <= index_high);

	/* Start at index_high + 1 as then we can check all buckets larger than the desired alloc
	 * If we don't find one larger then we could still find one of requested size in index_high and
	 * shortcut the non-contiguous allocation path. We check index_high + 1 first as it is
	 * guaranteed to have a free region of the requested size if the bucket has entries. Whereas
	 * index_high is not guaranteed to have an allocation that meets the size requested due to it
	 * representing all free regions of size 2^bucket index to 2^bucket index +1. e.g we could have
	 * a request for 19*4k Pages which would be represented by bucket 16, bucket 16 represents free
	 * entries from 16*4k pages to 31*4k Pages in size, if this bucket only had free entries of
	 * 17*4k pages the search would fail, hence not guaranteed at index_high.
	 */
#if defined(PVR_CTZLL)
	i = PVR_CTZLL((~(((IMG_ELTS_MAPPINGS)1 << (index_high + 1)) - 1)) & pArena->per_flags_buckets->bHasEltsMapping);
#else
	for (i = index_high + 1; (i < FREE_TABLE_LIMIT) && (pArena->per_flags_buckets->buckets[i] == NULL); i++)
	{
	}
#endif

	PVR_ASSERT(i <= FREE_TABLE_LIMIT);

	if (i != FREE_TABLE_LIMIT)
	{
		pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], uSize, uAlignment, 1);
	}
	else
	{
		/* In this case we have searched all buckets index_high + 1 to FREE_TABLE_LIMIT and not found an
		 * available bucket with the required allocation size.
		 * Because we haven't found an allocation of the requested size in index_high + 1 there is still a chance
		 * that we can find an allocation of correct size in index_high, when index_high references the bucket
		 * containing the largest free chunks in the RA Arena. i.e All buckets > index_high == NULL.
		 * We do a final search in that bucket here before we attempt to scoop memory or return NULL.
		 */
		pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[index_high], uSize, uAlignment, 1);
	}

	/* We managed to find a contiguous allocation block of sufficient size */
	if (pBT != NULL)
	{
		IMG_BOOL bResult;
		bResult =  _AllocAlignSplit(pArena, pBT, uSize, uAlignment, &uiBase, &phPriv);
		if (bResult)
		{
			if (!bSparseAlloc)
			{
				aBaseArray[0] = uiBase;
			}
			else
			{
				_GenerateGhostBases(uiBase, uSize, 1ULL << uLog2MinContigSize, aBaseArray);
			}
		}
		else
		{
			return PVRSRV_ERROR_RA_ATTEMPT_ALLOC_ALIGNED_FAILED;
		}
		*bPhysContig = IMG_TRUE;

		return PVRSRV_OK;
	}

	/*
	 * If this arena doesn't have the non-contiguous allocation functionality enabled, then
	 * don't attempt to scoop for non physically contiguous allocations. Sparse allocations
	 * are still able to use the scoop functionality as they map in a chunk at a time in the
	 * worst case.
	 */
	if (unlikely((pArena->ui32PolicyFlags & RA_POLICY_ALLOC_ALLOW_NONCONTIG_MASK) == 0) &&
	    !bSparseAlloc)
	{
		return PVRSRV_ERROR_RA_ATTEMPT_ALLOC_ALIGNED_FAILED;
	}

	/* Attempt to Scoop memory from non-contiguous blocks */
	for (i = index_high; i >= index_low && uiRemaining != 0; i--)
	{
		/* While we have chunks of at least our contig size in the bucket to use */
		for (
		pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], 1ULL << uLog2MinContigSize, uAlignment,(unsigned int) ~0);
		pBT != NULL && uiRemaining != 0;
		pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], 1ULL << uLog2MinContigSize, uAlignment,(unsigned int) ~0))//~0 Try all elements in bucket
		{
			/* Grab largest chunk possible that is a multiple of our min contiguity size
			 * N.B: C always rounds towards 0 so this effectively floors for us */
			IMG_BOOL bResult;
			RA_BASE_T uiAlignedBase =
			(uAlignment > 1) ? PVR_ALIGN(pBT->base, uAlignment) : pBT->base;
			RA_LENGTH_T uiMaxSizeAvailable = (pBT->uSize - (uiAlignedBase - pBT->base));
			RA_LENGTH_T uiMaxMultipleOfContig = (uiMaxSizeAvailable >> uLog2MinContigSize) << uLog2MinContigSize;

			/*
			 * If the size of the BT is larger than the remaining memory to allocate
			 * then just allocate what we need. The rest will be trimmed and put back
			 * into the pool in _AllocAlignSplit
			 */
			if (uiMaxMultipleOfContig > uiRemaining)
			{
				uiMaxMultipleOfContig = uiRemaining;
			}

			bResult = _AllocAlignSplit(pArena, pBT, uiMaxMultipleOfContig, uAlignment, &uiBase, &phPriv);
			if (!bResult)
			{
				/* Something went wrong with splitting or adding to hash,
				 * We can try find another chunk, although this should
				 * never occur.
				 */
				PVR_ASSERT(!"_AllocAlignSplit issue.");
				continue;
			}

			uiRemaining -= uiMaxMultipleOfContig;

			uiCurrentArrayIndex += _GenerateGhostBases(uiBase,
			                                           uiMaxMultipleOfContig,
			                                           1ULL << uLog2MinContigSize,
			                                           &aBaseArray[uiCurrentArrayIndex]);
		}
	}

	/* If we didn't manage to scoop enough memory then we need to unwind the allocations we just made */
	if (uiRemaining != 0)
	{
		goto error_unwind;
	}
	*bPhysContig = IMG_FALSE;

	return PVRSRV_OK;

error_unwind:
	_RA_FreeMultiUnlocked(pArena,
	                       aBaseArray,
	                       uiCurrentArrayIndex);
	return PVRSRV_ERROR_RA_ATTEMPT_ALLOC_ALIGNED_FAILED;
}

/*************************************************************************/ /*!
@Function       _AttemptImportSpanAlloc
@Description    Attempt to Import more memory and create a new span.
                Function attempts to import more memory from the callback
                provided at RA creation time, if successful the memory
                will form a new span in the RA.
@Input          pArena            The arena.
@Input          uRequestSize      The requested allocation size.
@Input          uImportMultiplier Import x-times more for future requests if
                                  we have to import new memory.
@Input          uImportFlags      Flags influencing allocation policy.
@Input          uAlignment        The alignment requirements of the allocation
                                  Required uAlignment, or 0.
                                  Must be a power of 2 if not 0
@Input          pszAnnotation     String to describe the allocation
@Output         pImportBase       Allocated import base
                                  (non-optional, must not be NULL)
@Output         pImportSize       Allocated import size
@Output         pImportBT         Allocated import BT
@Return         PVRSRV_OK - success
*/ /**************************************************************************/
static PVRSRV_ERROR
_AttemptImportSpanAlloc(RA_ARENA *pArena,
                        RA_LENGTH_T uRequestSize,
                        IMG_UINT8 uImportMultiplier,
                        RA_FLAGS_T uImportFlags,
                        RA_LENGTH_T uAlignment,
                        const IMG_CHAR *pszAnnotation,
                        RA_BASE_T *pImportBase,
                        RA_LENGTH_T *pImportSize,
                        BT **pImportBT)
{
	IMG_HANDLE hPriv;
	RA_FLAGS_T uFlags = (uImportFlags & PVRSRV_MEMALLOCFLAGS_RA_DIFFERENTIATION_MASK);
	BT *pBT;
	PVRSRV_ERROR eError;

	*pImportSize = uRequestSize;

	/* apply over-allocation multiplier after all alignment adjustments */
	*pImportSize *= uImportMultiplier;

	/* ensure that we import according to the quanta of this arena */
	*pImportSize = PVR_ALIGN(*pImportSize, pArena->uQuantum);

	eError = pArena->pImportAlloc(pArena->pImportHandle,
								  *pImportSize, uImportFlags,
								  uAlignment,
								  pszAnnotation,
								  pImportBase, pImportSize,
								  &hPriv);
	if (PVRSRV_OK != eError)
	{
		return eError;
	}

	/* If we successfully import more resource, create a span to
	 * represent it else free the resource we imported.
	 */
	pBT = _InsertResourceSpan(pArena, *pImportBase, *pImportSize, uFlags);
	if (pBT == NULL)
	{
		/* insufficient resources to insert the newly acquired span,
		   so free it back again */
		pArena->pImportFree(pArena->pImportHandle, *pImportBase, hPriv);

		PVR_DPF((PVR_DBG_MESSAGE, "%s: name='%s', "
		        "size=0x%llx failed!", __func__, pArena->name,
		        (unsigned long long)uRequestSize));
		/* RA_Dump (arena); */

		return PVRSRV_ERROR_RA_INSERT_RESOURCE_SPAN_FAILED;
	}

	pBT->hPriv = hPriv;
	*pImportBT = pBT;

	return eError;
}

IMG_INTERNAL RA_ARENA *
RA_Create(IMG_CHAR *name,
          RA_LOG2QUANTUM_T uLog2Quantum,
          IMG_UINT32 ui32LockClass,
          PFN_RA_ALLOC imp_alloc,
          PFN_RA_FREE imp_free,
          RA_PERARENA_HANDLE arena_handle,
          RA_POLICY_T ui32PolicyFlags)
{
	RA_ARENA *pArena;
	PVRSRV_ERROR eError;

	if (name == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid parameter 'name' (NULL not accepted)", __func__));
		return NULL;
	}

	PVR_DPF((PVR_DBG_MESSAGE, "%s: name='%s'", __func__, name));

	pArena = OSAllocMem(sizeof(*pArena));
	if (pArena == NULL)
	{
		goto arena_fail;
	}

	eError = OSLockCreate(&pArena->hLock);
	if (eError != PVRSRV_OK)
	{
		goto lock_fail;
	}

	pArena->pSegmentHash = HASH_Create_Extended(MINIMUM_HASH_SIZE, sizeof(RA_BASE_T), HASH_Func_Default, HASH_Key_Comp_Default);

	if (pArena->pSegmentHash==NULL)
	{
		goto hash_fail;
	}

	OSStringLCopy(pArena->name, name, RA_MAX_NAME_LENGTH);
	pArena->pImportAlloc = (imp_alloc!=NULL) ? imp_alloc : &_RequestAllocFail;
	pArena->pImportFree = imp_free;
	pArena->pImportHandle = arena_handle;
	pArena->pHeadSegment = NULL;
	pArena->uQuantum = 1ULL << uLog2Quantum;
	pArena->per_flags_buckets = NULL;
	pArena->ui32LockClass = ui32LockClass;
	pArena->ui32PolicyFlags = ui32PolicyFlags;
	pArena->ui64TotalArenaSize = 0;
	pArena->ui64FreeArenaSize = 0;

	PVR_ASSERT(is_arena_valid(pArena));
	return pArena;

hash_fail:
	OSLockDestroy(pArena->hLock);
lock_fail:
	OSFreeMem(pArena);
	/* not nulling pointer, out of scope */
arena_fail:
	return NULL;
}

static void _LogRegionCreation(const char *pszMemType,
                               IMG_UINT64 ui64CpuPA,
                               IMG_UINT64 ui64DevPA,
                               IMG_UINT64 ui64Size)
{
#if !defined(DEBUG)
	PVR_UNREFERENCED_PARAMETER(pszMemType);
	PVR_UNREFERENCED_PARAMETER(ui64CpuPA);
	PVR_UNREFERENCED_PARAMETER(ui64DevPA);
	PVR_UNREFERENCED_PARAMETER(ui64Size);
#else
	if ((ui64CpuPA != 0) && (ui64DevPA != 0) && (ui64CpuPA != ui64DevPA))
	{
		PVR_DPF((PVR_DBG_MESSAGE,
		        "Creating RA for \"%s\" memory"
		        " - Cpu PA 0x%016" IMG_UINT64_FMTSPECx "-0x%016" IMG_UINT64_FMTSPECx
		        " - Dev PA 0x%016" IMG_UINT64_FMTSPECx "-0x%016" IMG_UINT64_FMTSPECx,
		        pszMemType,
		        ui64CpuPA, ui64CpuPA + ui64Size,
		        ui64DevPA, ui64DevPA + ui64Size));
	}
	else
	{
		__maybe_unused IMG_UINT64 ui64PA =
			ui64CpuPA != 0 ? ui64CpuPA : ui64DevPA;
		__maybe_unused const IMG_CHAR *pszAddrType =
			ui64CpuPA == ui64DevPA ? "Cpu/Dev" : (ui64CpuPA != 0 ? "Cpu" : "Dev");

		PVR_DPF((PVR_DBG_MESSAGE,
		        "Creating RA for \"%s\" memory - %s PA 0x%016"
		        IMG_UINT64_FMTSPECx "-0x%016" IMG_UINT64_FMTSPECx,
		        pszMemType, pszAddrType,
		        ui64PA, ui64PA + ui64Size));
	}
#endif
}

IMG_INTERNAL RA_ARENA *
RA_Create_With_Span(IMG_CHAR *name,
                    RA_LOG2QUANTUM_T uLog2Quantum,
                    IMG_UINT64 ui64CpuBase,
                    IMG_UINT64 ui64SpanDevBase,
                    IMG_UINT64 ui64SpanSize,
                    RA_POLICY_T ui32PolicyFlags)
{
	RA_ARENA *psRA;
	IMG_BOOL bSuccess;

	psRA = RA_Create(name,
	                 uLog2Quantum,       /* Use OS page size, keeps things simple */
	                 RA_LOCKCLASS_0,     /* This arena doesn't use any other arenas. */
	                 NULL,               /* No Import */
	                 NULL,               /* No free import */
	                 NULL,               /* No import handle */
	                 ui32PolicyFlags);   /* No restriction on import splitting */
	PVR_LOG_GOTO_IF_FALSE(psRA != NULL, "RA_Create() failed", return_);

	bSuccess = RA_Add(psRA, (RA_BASE_T) ui64SpanDevBase, (RA_LENGTH_T) ui64SpanSize, 0, NULL);
	PVR_LOG_GOTO_IF_FALSE(bSuccess, "RA_Add() failed", cleanup_);

	_LogRegionCreation(name, ui64CpuBase, ui64SpanDevBase, ui64SpanSize);

	return psRA;

cleanup_:
	RA_Delete(psRA);
return_:
	return NULL;
}

IMG_INTERNAL void
RA_Delete(RA_ARENA *pArena)
{
	IMG_UINT32 uIndex;
	IMG_BOOL bWarn = IMG_TRUE;

	PVR_ASSERT(pArena != NULL);

	if (pArena == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid parameter - pArena", __func__));
		return;
	}

	PVR_ASSERT(is_arena_valid(pArena));

	PVR_DPF((PVR_DBG_MESSAGE,
			  "%s: name='%s'", __func__, pArena->name));

	while (pArena->pHeadSegment != NULL)
	{
		BT *pBT = pArena->pHeadSegment;

		if (pBT->type != btt_free)
		{
			if (bWarn)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Allocations still exist in the arena that is being destroyed", __func__));
				PVR_DPF((PVR_DBG_ERROR, "%s: Likely Cause: client drivers not freeing allocations before destroying devmem context", __func__));
				PVR_DPF((PVR_DBG_ERROR, "%s: base = 0x%llx size=0x%llx", __func__,
					  (unsigned long long)pBT->base, (unsigned long long)pBT->uSize));
				PVR_DPF((PVR_DBG_ERROR, "%s: This warning will be issued only once for the first allocation found!", __func__));
				bWarn = IMG_FALSE;
			}
		}
		else
		{
			_FreeListRemove(pArena, pBT);
		}

		_SegmentListRemove(pArena, pBT);
		OSFreeMem(pBT);
		/* not nulling original pointer, it has changed */
	}

	while (pArena->per_flags_buckets != NULL)
	{
		for (uIndex=0; uIndex<FREE_TABLE_LIMIT; uIndex++)
		{
			PVR_ASSERT(pArena->per_flags_buckets->buckets[uIndex] == NULL);
		}

		pArena->per_flags_buckets = PVRSRVDelete(pArena->per_flags_buckets->uiFlags, pArena->per_flags_buckets);
	}

	HASH_Delete(pArena->pSegmentHash);
	OSLockDestroy(pArena->hLock);
	OSFreeMem(pArena);
	/* not nulling pointer, copy on stack */
}

IMG_INTERNAL IMG_BOOL
RA_Add(RA_ARENA *pArena,
       RA_BASE_T base,
       RA_LENGTH_T uSize,
       RA_FLAGS_T uFlags,
       RA_PERISPAN_HANDLE hPriv)
{
	struct _BT_* bt;
	PVR_ASSERT(pArena != NULL);
	PVR_ASSERT(uSize != 0);

	if (pArena == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid parameter - pArena", __func__));
		return IMG_FALSE;
	}

	if (uSize == 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid size 0 added to arena %s", __func__, pArena->name));
		return IMG_FALSE;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));
	PVR_DPF((PVR_DBG_MESSAGE, "%s: name='%s', "
			 "base=0x%llx, size=0x%llx", __func__, pArena->name,
			 (unsigned long long)base, (unsigned long long)uSize));

	uSize = PVR_ALIGN(uSize, pArena->uQuantum);
	bt = _InsertResource(pArena, base, uSize, uFlags);
	if (bt != NULL)
	{
		bt->hPriv = hPriv;
	}

	PVR_ASSERT(is_arena_valid(pArena));

	pArena->ui64TotalArenaSize += uSize;
	pArena->ui64FreeArenaSize += uSize;
	OSLockRelease(pArena->hLock);

	return bt != NULL;
}

IMG_INTERNAL PVRSRV_ERROR
RA_Alloc(RA_ARENA *pArena,
         RA_LENGTH_T uRequestSize,
         IMG_UINT8 uImportMultiplier,
         RA_FLAGS_T uImportFlags,
         RA_LENGTH_T uAlignment,
         const IMG_CHAR *pszAnnotation,
         RA_BASE_T *base,
         RA_LENGTH_T *pActualSize,
         RA_PERISPAN_HANDLE *phPriv)
{
	PVRSRV_ERROR eError;
	IMG_BOOL bResult;
	RA_LENGTH_T uSize = uRequestSize;
	RA_FLAGS_T uFlags = (uImportFlags & PVRSRV_MEMALLOCFLAGS_RA_DIFFERENTIATION_MASK);

	if (pArena == NULL || uImportMultiplier == 0 || uSize == 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: One of the necessary parameters is 0", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));

	if (pActualSize != NULL)
	{
		*pActualSize = uSize;
	}

	/* Must be a power of 2 or 0 */
	PVR_ASSERT((uAlignment == 0) || (uAlignment & (uAlignment - 1)) == 0);

	PVR_DPF((PVR_DBG_MESSAGE,
	        "%s: arena='%s', size=0x%llx(0x%llx), "
	        "alignment=0x%llx", __func__, pArena->name,
	        (unsigned long long)uSize, (unsigned long long)uRequestSize,
	        (unsigned long long)uAlignment));

	/* if allocation failed then we might have an import source which
	   can provide more resource, else we will have to fail the
	   allocation to the caller. */
	bResult = _AttemptAllocAligned(pArena, uSize, uFlags, uAlignment, base, phPriv);
	if (!bResult)
	{
		RA_BASE_T uImportBase;
		RA_LENGTH_T uImportSize;
		BT *pBT = NULL;

		eError = _AttemptImportSpanAlloc(pArena,
		                                 uSize,
		                                 uImportMultiplier,
		                                 uImportFlags,
		                                 uAlignment,
		                                 pszAnnotation,
		                                 &uImportBase,
		                                 &uImportSize,
		                                 &pBT);
		if (eError != PVRSRV_OK)
		{
			OSLockRelease(pArena->hLock);
			return eError;
		}

		bResult = _AttemptAllocAligned(pArena, uSize, uFlags, uAlignment, base, phPriv);
		if (!bResult)
		{
			PVR_DPF((PVR_DBG_ERROR,
			        "%s: name='%s' second alloc failed!",
			        __func__, pArena->name));

			/*
			  On failure of _AttemptAllocAligned() depending on the exact point
			  of failure, the imported segment may have been used and freed, or
			  left untouched. If the later, we need to return it.
			*/
			_FreeBT(pArena, pBT);

			OSLockRelease(pArena->hLock);
			return PVRSRV_ERROR_RA_ATTEMPT_ALLOC_ALIGNED_FAILED;
		}
		else
		{
			/* Check if the new allocation was in the span we just added... */
			if (*base < uImportBase  ||  *base > (uImportBase + uImportSize))
			{
				PVR_DPF((PVR_DBG_ERROR,
				        "%s: name='%s' alloc did not occur in the imported span!",
				        __func__, pArena->name));

				/*
				  Remove the imported span which should not be in use (if it is then
				  that is okay, but essentially no span should exist that is not used).
				*/
				_FreeBT(pArena, pBT);
			}
			else
			{
				pArena->ui64FreeArenaSize += uImportSize;
				pArena->ui64TotalArenaSize += uImportSize;
			}
		}
	}

	PVR_DPF((PVR_DBG_MESSAGE, "%s: name='%s', size=0x%llx, "
	        "*base=0x%llx = %d", __func__, pArena->name, (unsigned long long)uSize,
	        (unsigned long long)*base, bResult));

	PVR_ASSERT(is_arena_valid(pArena));

	pArena->ui64FreeArenaSize -= uSize;

	OSLockRelease(pArena->hLock);
	return PVRSRV_OK;
}

static PVRSRV_ERROR
_RA_AllocMultiUnlocked(RA_ARENA *pArena,
                       RA_LENGTH_T uRequestSize,
                       IMG_UINT32 uiLog2ChunkSize,
                       IMG_UINT8 uImportMultiplier,
                       RA_FLAGS_T uImportFlags,
                       const IMG_CHAR *pszAnnotation,
                       RA_BASE_ARRAY_T aBaseArray,
                       RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                       IMG_BOOL bSparseAlloc,
                       IMG_BOOL *bPhysContig)
{
	PVRSRV_ERROR eError;
	RA_LENGTH_T uSize = uRequestSize;
	RA_FLAGS_T uFlags = (uImportFlags & PVRSRV_MEMALLOCFLAGS_RA_DIFFERENTIATION_MASK);

	PVR_LOG_RETURN_IF_FALSE(pArena != NULL && uImportMultiplier != 0 && uSize != 0,
	                        "One of the necessary parameters is 0",
	                        PVRSRV_ERROR_INVALID_PARAMS);

	PVR_ASSERT((uRequestSize & ((1 << uiLog2ChunkSize) - 1)) == 0)
	PVR_LOG_RETURN_IF_FALSE((uRequestSize & ((1 << uiLog2ChunkSize) - 1)) == 0,
	                        "Require uiLog2ChunkSize pow 2 & multiple of uRequestSize",
	                        PVRSRV_ERROR_INVALID_PARAMS);

	/* Enforce these constraints so we can use those bits to handle Ghost bases. */
	PVR_LOG_RETURN_IF_FALSE(uiLog2ChunkSize >= RA_BASE_FLAGS_LOG2 &&
	                        uiLog2ChunkSize <= RA_BASE_CHUNK_LOG2_MAX,
		                    "Log2 chunk size must be 12-64",
		                    PVRSRV_ERROR_INVALID_PARAMS);

	/* Ensure Base Array is large enough for intended allocation */
	PVR_LOG_RETURN_IF_FALSE(uiBaseArraySize * (1 << uiLog2ChunkSize) >= uRequestSize,
		                    "Not enough array space to store alloc bases",
		                    PVRSRV_ERROR_INVALID_PARAMS);

	PVR_ASSERT(is_arena_valid(pArena));

	/* Must be a power of 2 */
	PVR_ASSERT((uAlignment & (uAlignment - 1)) == 0);

	PVR_DPF((PVR_DBG_MESSAGE,
	        "%s: arena='%s', size=0x%llx(0x%llx), "
	        "log2ChunkSize=0x%llx", __func__, pArena->name,
	        (unsigned long long)uSize, (unsigned long long)uRequestSize,
	        (unsigned long long)uiLog2ChunkSize));

	/* if allocation failed then we might have an import source which
	   can provide more resource, else we will have to fail the
	   allocation to the caller. */
	eError = _AttemptAllocAlignedAssured(pArena,
	                                      uSize,
	                                      uiLog2ChunkSize,
	                                      uFlags,
	                                      1ULL << uiLog2ChunkSize,
	                                      aBaseArray,
	                                      bSparseAlloc,
	                                      bPhysContig);
	if (eError)
	{
		RA_BASE_T uImportBase;
		RA_LENGTH_T uImportSize;
		BT *pBT;

		if (eError == PVRSRV_ERROR_RA_OUT_OF_RESOURCE)
		{
			PVR_DPF((PVR_DBG_MESSAGE,"RA out of resource, attempt to import more if possible:"
			                         " uSize:0x%llx"
			                         " uFlags:0x%llx",
			                         (unsigned long long) uSize,
			                         (unsigned long long) uFlags));
		}
		else if (eError == PVRSRV_ERROR_RA_NO_RESOURCE_WITH_FLAGS)
		{
			PVR_DPF((PVR_DBG_MESSAGE,"RA no resource for flags, attempt to import some if possible:"
			                         " uSize:0x%llx"
			                         " uFlags:0x%llx",
			                         (unsigned long long) uSize,
			                         (unsigned long long) uFlags));
		}
		else
		{
			PVR_DPF((PVR_DBG_MESSAGE,"RA Failed to Allocate, could be fragmented, attempt to import"
			                         " more resource if possible."));
		}

		eError = _AttemptImportSpanAlloc(pArena,
		                                 uSize,
		                                 uImportMultiplier,
		                                 uFlags,
		                                 1ULL << uiLog2ChunkSize,
		                                 pszAnnotation,
		                                 &uImportBase,
		                                 &uImportSize,
		                                 &pBT);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		pArena->ui64FreeArenaSize += uImportSize;
		pArena->ui64TotalArenaSize += uImportSize;

		eError = _AttemptAllocAlignedAssured(pArena,
		                                      uSize,
		                                      uiLog2ChunkSize,
		                                      uFlags,
		                                      1Ull << uiLog2ChunkSize,
		                                      aBaseArray,
		                                      bSparseAlloc,
		                                      bPhysContig);
		if (eError)
		{
			PVR_DPF((PVR_DBG_ERROR,
					 "%s: name='%s' second alloc failed!",
					 __func__, pArena->name));
			/*
			  On failure of _AttemptAllocAligned() depending on the exact point
			  of failure, the imported segment may have been used and freed, or
			  left untouched. If the later, we need to return it.
			*/
			_FreeBT(pArena, pBT);

			return PVRSRV_ERROR_RA_ATTEMPT_ALLOC_ALIGNED_FAILED;
		}
#if defined(DEBUG)
		/*
		 * This block of code checks to see if the extra memory we just imported was
		 * used for the second allocation. If we imported memory but did not use it,
		 * it indicates there is a bug in the allocation logic. We can still recover by
		 * freeing the imported span but we emit an error to signal that there is an
		 * issue.
		 * */
		else
		{
			IMG_UINT32 i;
			IMG_BOOL bBasesInNewSpan = IMG_FALSE;

			for (i = 0; i < uiBaseArraySize; i++)
			{
				RA_BASE_T uiBase = RA_BASE_STRIP_GHOST_BIT(aBaseArray[i]);

				/* If the base hasn't been allocated then skip it */
				if (aBaseArray[i] == INVALID_BASE_ADDR)
				{
					continue;
				}

				if (uiBase >= uImportBase &&
				    uiBase <= uImportBase + uImportSize)
				{
					bBasesInNewSpan = IMG_TRUE;
				}
			}

			if (!bBasesInNewSpan)
			{
				PVR_DPF((PVR_DBG_ERROR,
						"%s: name='%s' alloc did not occur in the imported span!",
						__func__, pArena->name));
				/*
				  Remove the imported span which should not be in use (if it is then
				  that is okay, but essentially no span should exist that is not used).
				*/
				_FreeBT(pArena, pBT);

				pArena->ui64FreeArenaSize -= uImportSize;
				pArena->ui64TotalArenaSize -= uImportSize;
			}
		}
#endif
	}

	PVR_ASSERT(is_arena_valid(pArena));

	pArena->ui64FreeArenaSize -= uSize;

	return PVRSRV_OK;
}

IMG_INTERNAL PVRSRV_ERROR
RA_AllocMulti(RA_ARENA *pArena,
              RA_LENGTH_T uRequestSize,
              IMG_UINT32 uiLog2ChunkSize,
              IMG_UINT8 uImportMultiplier,
              RA_FLAGS_T uImportFlags,
              const IMG_CHAR *pszAnnotation,
              RA_BASE_ARRAY_T aBaseArray,
              RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
              IMG_BOOL *bPhysContig)
{
	PVRSRV_ERROR eError;
	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	eError = _RA_AllocMultiUnlocked(pArena,
	                                 uRequestSize,
	                                 uiLog2ChunkSize,
	                                 uImportMultiplier,
	                                 uImportFlags,
	                                 pszAnnotation,
	                                 aBaseArray,
	                                 uiBaseArraySize,
	                                 IMG_FALSE, /* Sparse alloc */
	                                 bPhysContig);
	OSLockRelease(pArena->hLock);

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR
RA_AllocMultiSparse(RA_ARENA *pArena,
                     IMG_UINT32 uiLog2ChunkSize,
                     IMG_UINT8 uImportMultiplier,
                     RA_FLAGS_T uImportFlags,
                     const IMG_CHAR *pszAnnotation,
                     RA_BASE_ARRAY_T aBaseArray,
                     RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                     IMG_UINT32 *puiAllocIndices,
                     IMG_UINT32 uiAllocCount)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;
	IMG_BOOL bPhysContig;

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);

	/*
	 * In this case the arguments given show the allocation is
	 * sparse but has no specific indices, this indicates
	 * we want to populate the full aBaseArray
	 */
	if (puiAllocIndices == NULL)
	{
		RA_LENGTH_T uRequestSize = (RA_LENGTH_T) uiAllocCount << uiLog2ChunkSize;
		eError = _RA_AllocMultiUnlocked(pArena,
		                                 uRequestSize,
		                                 uiLog2ChunkSize,
		                                 uImportMultiplier,
		                                 uImportFlags,
		                                 pszAnnotation,
		                                 aBaseArray,
		                                 uiBaseArraySize,
		                                 IMG_TRUE, /* Sparse alloc */
		                                 &bPhysContig);
		PVR_LOG_IF_ERROR(eError, "RA_AllocMulti");
		OSLockRelease(pArena->hLock);
		return eError;
	}

	/*
	 * This case is optimised for single allocations as we can skip
	 * some of the iteration logic in the full allocation path.
	 */
	if (uiAllocCount == 1)
	{
		eError = _RA_AllocMultiUnlocked(pArena,
		                                 1ULL << uiLog2ChunkSize,
		                                 uiLog2ChunkSize,
		                                 uImportMultiplier,
		                                 uImportFlags,
		                                 pszAnnotation,
		                                 &aBaseArray[puiAllocIndices[0]],
		                                 1,
		                                 IMG_TRUE, /* Sparse alloc */
		                                 &bPhysContig);
		PVR_LOG_IF_ERROR(eError, "RA_AllocMulti");
		OSLockRelease(pArena->hLock);
		return eError;
	}

	/*
	 * By consolidating / grouping the indices given we can perform sparse allocations
	 * in blocks, this has the effect of reducing fragmentation and creating optimal free
	 * scenarios. Free can be performed in blocks rather than a chunk at a time, this reduces
	 * the amount of BT merging cycles we perform.
	 */
	for (i = 0; i < uiAllocCount;)
	{
		IMG_UINT32 j;
		IMG_UINT32 uiConsolidate = 1;

		for (j = i;
		     j + 1 != uiAllocCount &&
		     puiAllocIndices[j + 1] == puiAllocIndices[j] + 1;
		     j++)
		{
			uiConsolidate++;
		}

		eError = _RA_AllocMultiUnlocked(pArena,
		                                 (IMG_UINT64) uiConsolidate << uiLog2ChunkSize,
		                                 uiLog2ChunkSize,
		                                 uImportMultiplier,
		                                 uImportFlags,
		                                 pszAnnotation,
		                                 &aBaseArray[puiAllocIndices[i]],
		                                 uiConsolidate,
		                                 IMG_TRUE, /* Sparse alloc */
		                                 &bPhysContig);
		PVR_LOG_GOTO_IF_ERROR(eError, "RA_AllocMulti", unwind_alloc);
		i += uiConsolidate;
	}

	OSLockRelease(pArena->hLock);
	return PVRSRV_OK;

unwind_alloc:
	if (i != 0)
	{
		PVRSRV_ERROR eFreeError;
		eFreeError = _RA_FreeMultiUnlockedSparse(pArena,
		                                          aBaseArray,
		                                          uiBaseArraySize,
		                                          1ULL << uiLog2ChunkSize,
		                                          puiAllocIndices,
		                                          &i);
		PVR_LOG_IF_ERROR(eFreeError, "_RA_FreeMultiUnlockedSparse");
	}

	OSLockRelease(pArena->hLock);
	return eError;
}

/*************************************************************************/ /*!
@Function       RA_Find_BT_VARange
@Description    To find the boundary tag associated with the given device
                                  virtual address.
@Input          pArena            The arena
@input          base              Allocated base resource
@Input          uRequestSize      The size of resource segment requested.
@Input          uImportFlags            Flags influencing allocation policy.
@Return         Boundary Tag - success, NULL on failure
*/ /**************************************************************************/
static BT *RA_Find_BT_VARange(RA_ARENA *pArena,
                              RA_BASE_T base,
                              RA_LENGTH_T uRequestSize,
                              RA_FLAGS_T uImportFlags)
{
	IMG_PSPLAY_TREE psSplaynode;
	IMG_UINT32 uIndex;

	/* Find the splay node associated with these import flags */
	psSplaynode = PVRSRVFindNode(uImportFlags, pArena->per_flags_buckets);

	if (psSplaynode == NULL)
	{
		return NULL;
	}

	uIndex = pvr_log2(uRequestSize);

	/* Find the free Boundary Tag from the bucket that holds the requested range */
	while (uIndex < FREE_TABLE_LIMIT)
	{
		BT *pBT = psSplaynode->buckets[uIndex];

		while (pBT)
		{
			if ((pBT->base <= base) && ((pBT->base + pBT->uSize) >= (base + uRequestSize)))
			{
				if (pBT->type == btt_free)
				{
					return pBT;
				}
				else
				{
					PVR_ASSERT(pBT->type == btt_free);
				}
			}
			else{
				pBT = pBT->next_free;
			}
		}

#if defined(PVR_CTZLL)
		/* This could further be optimised to get the next valid bucket */
		while (!(psSplaynode->bHasEltsMapping & (1ULL << ++uIndex)));
#else
		uIndex++;
#endif
	}

	return NULL;
}

IMG_INTERNAL PVRSRV_ERROR
RA_Alloc_Range(RA_ARENA *pArena,
               RA_LENGTH_T uRequestSize,
               RA_FLAGS_T uImportFlags,
               RA_LENGTH_T uAlignment,
               RA_BASE_T base,
               RA_LENGTH_T *pActualSize)
{
	RA_LENGTH_T uSize = uRequestSize;
	BT *pBT = NULL;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (pArena == NULL || uSize == 0)
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: One of the necessary parameters is 0", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));

	/* Align the requested size to the Arena Quantum */
	uSize = PVR_ALIGN(uSize, pArena->uQuantum);

	/* Must be a power of 2 or 0 */
	PVR_ASSERT((uAlignment == 0) || (uAlignment & (uAlignment - 1)) == 0);

	if (uAlignment > 1)
	{
		if (base != PVR_ALIGN(base, uAlignment))
		{
			PVR_GOTO_WITH_ERROR(eError, PVRSRV_ERROR_INVALID_PARAMS, unlock_);
		}
	}

	/* Find if the segment in the range exists and is free
	 * Check if the segment can be split
	 * Find the bucket that points to this segment
	 * Find the free segment is in the free list
	 * remove the free segment
	 * split the segment into three segments one prior free, alloc range,
	 *     free segment after the range.
	 * remove the allocated range segment from the free list
	 * hook up the prior and after segments back to free list
	 * For each free, find the bucket the segment should go to
	 */

	pBT = RA_Find_BT_VARange(pArena, base, uSize, uImportFlags);

	if (pBT == NULL)
	{
		PVR_GOTO_WITH_ERROR(eError,
		                    PVRSRV_ERROR_RA_REQUEST_VIRT_ADDR_FAIL,
		                    unlock_);
	}

	/* Remove the boundary tag from the free list */
	_FreeListRemove (pArena, pBT);

	/* if requested VA start in the middle of the BT, split the BT accordingly */
	if (base > pBT->base)
	{
		BT *pNeighbour;
		pNeighbour = _SegmentSplit (pBT, (RA_LENGTH_T)(base - pBT->base));
		/* partition the buffer, create a new boundary tag */
		if (pNeighbour == NULL)
		{
			/* Put pBT back in the list */
			_FreeListInsert (pArena, pBT);
			PVR_LOG_GOTO_WITH_ERROR("_SegmentSplit (1)", eError,
			                        PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL,
			                        unlock_);
		}

		/* Insert back the free BT to the free list */
		_FreeListInsert(pArena, pBT);
		pBT = pNeighbour;
	}

	/* the segment might be too big, if so, discard the back of the segment */
	if (pBT->uSize > uSize)
	{
		BT *pNeighbour;
		pNeighbour = _SegmentSplit(pBT, uSize);
		/* partition the buffer, create a new boundary tag */
		if (pNeighbour == NULL)
		{
			/* Put pBT back in the list */
			_FreeListInsert (pArena, pBT);
			PVR_LOG_GOTO_WITH_ERROR("_SegmentSplit (2)", eError,
			                        PVRSRV_ERROR_RA_REQUEST_ALLOC_FAIL,
			                        unlock_);
		}

		/* Insert back the free BT to the free list */
		_FreeListInsert (pArena, pNeighbour);
	}

	pBT->type = btt_live;

	if (!HASH_Insert_Extended (pArena->pSegmentHash, &base, (uintptr_t)pBT))
	{
		_FreeBT (pArena, pBT);
		PVR_GOTO_WITH_ERROR(eError,
		                    PVRSRV_ERROR_INSERT_HASH_TABLE_DATA_FAILED,
		                    unlock_);
	}

	if (pActualSize != NULL)
	{
		*pActualSize = uSize;
	}

	pArena->ui64FreeArenaSize -= uSize;

unlock_:
	OSLockRelease(pArena->hLock);

	return eError;
}

IMG_INTERNAL void
RA_Free(RA_ARENA *pArena, RA_BASE_T base)
{
	BT *pBT;

	PVR_ASSERT(pArena != NULL);

	if (pArena == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: invalid parameter - pArena", __func__));
		return;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));

	PVR_DPF((PVR_DBG_MESSAGE, "%s: name='%s', base=0x%llx", __func__, pArena->name,
	        (unsigned long long)base));

	pBT = (BT *) HASH_Remove_Extended(pArena->pSegmentHash, &base);
	PVR_ASSERT(pBT != NULL);

	if (pBT)
	{
		pArena->ui64FreeArenaSize += pBT->uSize;

		PVR_ASSERT(pBT->base == base);
		_FreeBT(pArena, pBT);
	}
	else
	{
		PVR_DPF((PVR_DBG_ERROR,
		        "%s: no resource span found for given base (0x%llX) in arena %s",
		         __func__, (unsigned long long) base, pArena->name));
	}

	PVR_ASSERT(is_arena_valid(pArena));
	OSLockRelease(pArena->hLock);
}

static PVRSRV_ERROR
_RA_FreeMultiUnlocked(RA_ARENA *pArena,
                       RA_BASE_ARRAY_T aBaseArray,
                       RA_BASE_ARRAY_SIZE_T uiBaseArraySize)
{
	PVRSRV_ERROR eError;

	/* Free the whole array */
	if (uiBaseArraySize == 1)
	{
		eError = _FreeSingleBaseArray(pArena, aBaseArray, uiBaseArraySize);
		PVR_LOG_IF_ERROR(eError, "_FreeSingleBaseArray");
	}
	else
	{
		eError = _FreeMultiBaseArray(pArena, aBaseArray, uiBaseArraySize);
		PVR_LOG_IF_ERROR(eError, "_FreeMultiBaseArray");
	}

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR
RA_FreeMulti(RA_ARENA *pArena,
              RA_BASE_ARRAY_T aBaseArray,
              RA_BASE_ARRAY_SIZE_T uiBaseArraySize)
{
	PVRSRV_ERROR eError;
	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	eError = _RA_FreeMultiUnlocked(pArena,
	                                aBaseArray,
	                                uiBaseArraySize);
	OSLockRelease(pArena->hLock);

	return eError;
}

static PVRSRV_ERROR
_RA_FreeMultiUnlockedSparse(RA_ARENA *pArena,
                             RA_BASE_ARRAY_T aBaseArray,
                             RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                             RA_LENGTH_T uiChunkSize,
                             IMG_UINT32 *puiFreeIndices,
                             IMG_UINT32 *puiFreeCount)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError;
	IMG_UINT32 uiFreeCount = *puiFreeCount;
	*puiFreeCount = 0;

	/* Handle case where we only have 1 base to free. */
	if (uiFreeCount == 1)
	{
		eError = _FreeBaseArraySlice(pArena,
		                             aBaseArray,
		                             uiBaseArraySize,
		                             uiChunkSize,
		                             puiFreeIndices[0],
		                             1);
		PVR_LOG_IF_ERROR(eError, "_FreeBaseArraySlice");
		if (eError == PVRSRV_OK)
		{
			*puiFreeCount = uiFreeCount;
		}
		return eError;
	}

	for (i = 0; i < uiFreeCount;)
	{
		IMG_UINT32 j;
		IMG_UINT32 uiConsolidate = 1;

		PVR_ASSERT(RA_BASE_IS_REAL(aBaseArray[i]));

		for (j = i;
		     puiFreeIndices[j + 1] == puiFreeIndices[j] + 1 &&
		     RA_BASE_IS_GHOST(aBaseArray[puiFreeIndices[j + 1]]);
		     j++)
		{
			uiConsolidate++;
		}

		eError = _FreeBaseArraySlice(pArena,
		                             aBaseArray,
		                             uiBaseArraySize,
		                             uiChunkSize,
		                             puiFreeIndices[i],
		                             uiConsolidate);
		PVR_LOG_RETURN_IF_ERROR(eError, "_FreeBaseArraySlice");

		i += uiConsolidate;
		*puiFreeCount += uiConsolidate;
	}

	return PVRSRV_OK;
}

IMG_INTERNAL PVRSRV_ERROR
RA_FreeMultiSparse(RA_ARENA *pArena,
                    RA_BASE_ARRAY_T aBaseArray,
                    RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                    IMG_UINT32 uiLog2ChunkSize,
                    IMG_UINT32 *puiFreeIndices,
                    IMG_UINT32 *puiFreeCount)
{
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_FALSE(puiFreeCount != NULL,
		                        "puiFreeCount Required",
		                        PVRSRV_ERROR_INVALID_PARAMS);

	/* Ensure Base Array is large enough for intended free */
	PVR_LOG_RETURN_IF_FALSE(uiBaseArraySize >= *puiFreeCount,
		                        "Attempt to free more bases than array holds",
		                        PVRSRV_ERROR_INVALID_PARAMS);

	PVR_LOG_RETURN_IF_FALSE(puiFreeIndices != NULL,
		                        "puiFreeIndices Required",
		                        PVRSRV_ERROR_INVALID_PARAMS);

	PVR_LOG_RETURN_IF_FALSE(uiLog2ChunkSize >= RA_BASE_FLAGS_LOG2 &&
	                        uiLog2ChunkSize <= RA_BASE_CHUNK_LOG2_MAX,
		                    "Log2 chunk size must be 12-64",
		                    PVRSRV_ERROR_INVALID_PARAMS);

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	eError = _RA_FreeMultiUnlockedSparse(pArena,
	                                      aBaseArray,
	                                      uiBaseArraySize,
	                                      1ULL << uiLog2ChunkSize,
	                                      puiFreeIndices,
	                                      puiFreeCount);
	OSLockRelease(pArena->hLock);

	return eError;
}

static PVRSRV_ERROR
_TrimBlockMakeReal(RA_ARENA *pArena,
                   RA_BASE_ARRAY_T aBaseArray,
                   RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                   IMG_UINT32 uiLog2ChunkSize,
                   IMG_UINT32 uiStartIndex,
                   IMG_UINT32 uiEndIndex)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	RA_BASE_T sRealBase;
	IMG_UINT32 uiRealBaseIndex;

	/* Note: Error return paths in this function do not require unwinding.
	 * Free logic is performed based upon indices and detection of Real base regions,
	 * performance wise it would be more costly to unwind the conversion here than to
	 * just free a smaller Real base region.
	 */

	/* Check Start index is real, if not make it real */
	if (RA_BASE_IS_GHOST(aBaseArray[uiStartIndex]))
	{
		_FindRealBaseFromGhost(aBaseArray,
		                       uiStartIndex,
		                       &sRealBase,
		                       &uiRealBaseIndex);

		eError = _ConvertGhostBaseToReal(pArena,
		                                 aBaseArray,
		                                 sRealBase,
		                                 uiRealBaseIndex,
		                                 uiStartIndex,
		                                 1ULL << uiLog2ChunkSize);
		PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");
	}

	/* Check end +1 is real or end of array , if ghost make real */
	if (uiEndIndex + 1 != uiBaseArraySize &&
	    RA_BASE_IS_GHOST(aBaseArray[uiEndIndex + 1]))
	{
		_FindRealBaseFromGhost(aBaseArray,
		                       uiEndIndex + 1,
		                       &sRealBase,
		                       &uiRealBaseIndex);

		eError = _ConvertGhostBaseToReal(pArena,
		                                 aBaseArray,
		                                 sRealBase,
		                                 uiRealBaseIndex,
		                                 uiEndIndex + 1,
		                                 1ULL << uiLog2ChunkSize);
		PVR_LOG_RETURN_IF_ERROR(eError, "_ConvertGhostBaseToReal");
	}

	return eError;
}

IMG_INTERNAL PVRSRV_ERROR
RA_SwapSparseMem(RA_ARENA *pArena,
                  RA_BASE_ARRAY_T aBaseArray,
                  RA_BASE_ARRAY_SIZE_T uiBaseArraySize,
                  IMG_UINT32 uiLog2ChunkSize,
                  IMG_UINT32 *puiXIndices,
                  IMG_UINT32 *puiYIndices,
                  IMG_UINT32 uiSwapCount)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 uiSwapped = 0;
	IMG_UINT32 uiStartIndex;
	/* Consolidation values counting the bases after the start index*/
	IMG_UINT32 uiXConsol;
	IMG_UINT32 uiYConsol;
	/* Consolidation limit, the smallest consecutive indices between the
	 * two inputs
	 */
	IMG_UINT32 uiConsolidateLimit;
	IMG_UINT32 uiTotalSwapCount;
	IMG_UINT32 i;

	/*
	 * The algorithm below aims to swap the desired indices whilst also
	 * maintaining a maximum contiguity of allocation blocks where possible.
	 * It does this by:
	 * Consolidating the contiguous indices of X and Y.
	 * Selecting the smallest of these consolidations as a range to swap in a block.
	 * Trim both block ranges using the indices range to ensure that Real bases are
	 * created to represent regions that have been split due to the indices.
	 * Perform the swap and update the swapped count ready for the next iteration.
	 * Note: Maintaining contiguity improves performance of free logic for sparse
	 * allocations because we can free in regions rather than chunks.
	 */
	while (uiSwapped != uiSwapCount)
	{
		IMG_UINT32 x, y;
		uiTotalSwapCount = 1;
		uiStartIndex = uiSwapped;
		uiXConsol = 0;
		uiYConsol = 0;

		/* Calculate contiguous indices at X */
		for (x = uiStartIndex;
		     x < uiSwapCount &&
		     puiXIndices[x] + 1 == puiXIndices[x + 1];
		     x++)
		{
			uiXConsol++;
		}

		/* Calculate contiguous indices at Y */
		for (y = uiStartIndex;
		     y < uiSwapCount &&
		     puiYIndices[y] + 1 == puiYIndices[y + 1];
		     y++)
		{
			uiYConsol++;
		}

		/* Find lowest consolidation value */
		uiConsolidateLimit = (uiXConsol < uiYConsol) ? uiXConsol : uiYConsol;

		/* Perform RealBase translation where required */
		eError = _TrimBlockMakeReal(pArena,
		                            aBaseArray,
		                            uiBaseArraySize,
		                            uiLog2ChunkSize,
		                            puiXIndices[uiStartIndex],
		                            puiXIndices[uiStartIndex + uiConsolidateLimit]);
		PVR_LOG_GOTO_IF_ERROR(eError, "_TrimBlockMakeReal", unwind);

		eError = _TrimBlockMakeReal(pArena,
		                            aBaseArray,
		                            uiBaseArraySize,
		                            uiLog2ChunkSize,
		                            puiYIndices[uiStartIndex],
		                            puiYIndices[uiStartIndex + uiConsolidateLimit]);
		PVR_LOG_GOTO_IF_ERROR(eError, "_TrimBlockMakeReal", unwind);

		uiTotalSwapCount += uiConsolidateLimit;
		uiSwapped += uiTotalSwapCount;
		i = uiStartIndex;

		do
		{
			SWAP(aBaseArray[puiXIndices[i]], aBaseArray[puiYIndices[i]]);
			uiTotalSwapCount--;
			i++;
		}
		while (uiTotalSwapCount != 0);
	}

	return PVRSRV_OK;

unwind:
	/* If we hit an error when Trimming we should revert the swapping
	 * that has already been performed.
	 */
	for (i = 0; i < uiSwapped; i++)
	{
		SWAP(aBaseArray[puiXIndices[i]], aBaseArray[puiYIndices[i]]);
	}

	return eError;
}

IMG_INTERNAL void
RA_Get_Usage_Stats(RA_ARENA *pArena, PRA_USAGE_STATS psRAStats)
{
	psRAStats->ui64TotalArenaSize = pArena->ui64TotalArenaSize;
	psRAStats->ui64FreeArenaSize = pArena->ui64FreeArenaSize;
}

IMG_INTERNAL IMG_CHAR *
RA_GetArenaName(RA_ARENA *pArena)
{
	return pArena->name;
}

/* #define _DBG(...) PVR_LOG((__VA_ARGS__)) */
#define _DBG(...)

IMG_INTERNAL RA_ARENA_ITERATOR *
RA_IteratorAcquire(RA_ARENA *pArena, IMG_BOOL bIncludeFreeSegments)
{
	RA_ARENA_ITERATOR *pIter = OSAllocMem(sizeof(*pIter));
	PVR_LOG_RETURN_IF_FALSE(pIter != NULL, "OSAllocMem", NULL);

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);

	pIter->pArena = pArena;
	pIter->bIncludeFreeSegments = bIncludeFreeSegments;

	RA_IteratorReset(pIter);

	return pIter;
}

IMG_INTERNAL void
RA_IteratorRelease(RA_ARENA_ITERATOR *pIter)
{
	PVR_ASSERT(pIter != NULL);

	if (pIter == NULL)
	{
		return;
	}

	OSLockRelease(pIter->pArena->hLock);

	OSFreeMem(pIter);
}

IMG_INTERNAL void
RA_IteratorReset(RA_ARENA_ITERATOR *pIter)
{
	BT *pNext;

	PVR_ASSERT(pIter != NULL);

	pNext = pIter->pArena->pHeadSegment;

	/* find next element if we're not including the free ones */
	if (!pIter->bIncludeFreeSegments)
	{
		while (pNext != NULL && pNext->type != btt_live)
		{
			_DBG("(%s()) skipping segment=%px, size=0x%" IMG_UINT64_FMTSPECx ", "
			     "type=%u", __func__, (void *) pNext->base, pNext->uSize,
			     pNext->type);
			pNext = pNext->pNextSegment;
		}
	}

	_DBG("(%s()) current segment=%px, size=0x%" IMG_UINT64_FMTSPECx ", "
	     "type=%u", __func__,
	     pNext != NULL ? (void *) pNext->base : NULL,
	     pNext != NULL ? pNext->uSize : 0,
	     pNext != NULL ? pNext->type : 0);

	/* if bIncludeFreeSegments then pNext here is either a valid pointer to
	 * "live" segment or NULL and if !bIncludeFreeSegments then it's either
	 * a valid pointer to any next segment or NULL */
	pIter->pCurrent = pNext;
}

IMG_INTERNAL IMG_BOOL
RA_IteratorNext(RA_ARENA_ITERATOR *pIter, RA_ITERATOR_DATA *pData)
{
	BT *pNext;

	PVR_ASSERT(pIter != NULL);

	if (pIter == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "pIter in %s() is NULL", __func__));
		return IMG_FALSE;
	}

	if (pIter->pCurrent == NULL)
	{
		return IMG_FALSE;
	}

	pNext = pIter->pCurrent;

	_DBG("(%s()) current segment=%px, size=0x%" IMG_UINT64_FMTSPECx ", "
	     "type=%u", __func__, (void *) pNext->base, pNext->uSize,
	     pNext->type);

	pData->uiAddr = pIter->pCurrent->base;
	pData->uiSize = pIter->pCurrent->uSize;
	pData->bFree = pIter->pCurrent->type == btt_free;

	/* combine contiguous segments */
	while ((pNext = pNext->pNextSegment) != NULL &&
	        pNext->type == pNext->pPrevSegment->type &&
	        pNext->type == btt_live &&
	        pNext->base == pData->uiAddr + pData->uiSize)
	{
		_DBG("(%s()) combining segment=%px, size=0x%" IMG_UINT64_FMTSPECx ", "
		     "type=%u", __func__, (void *) pNext->base, pNext->uSize,
		     pNext->type);
		pData->uiSize += pNext->uSize;
	}

	/* advance to next */
	if (!pIter->bIncludeFreeSegments)
	{
		while (pNext != NULL && pNext->type != btt_live)
		{
			_DBG("(%s()) skipping segment=%px, size=0x%" IMG_UINT64_FMTSPECx ", "
			     "type=%u", __func__, (void *) pNext->base, pNext->uSize,
			     pNext->type);
			pNext = pNext->pNextSegment;
		}
	}

	_DBG("(%s()) next segment=%px, size=0x%" IMG_UINT64_FMTSPECx ", "
	     "type=%u", __func__,
	     pNext != NULL ? (void *) pNext->base : NULL,
	     pNext != NULL ? pNext->uSize : 0,
	     pNext != NULL ? pNext->type : 0);

	/* if bIncludeFreeSegments then pNext here is either a valid pointer to
	 * "live" segment or NULL and if !bIncludeFreeSegments then it's either
	 * a valid pointer to any next segment or NULL */
	pIter->pCurrent = pNext;

	return IMG_TRUE;
}

IMG_INTERNAL PVRSRV_ERROR
RA_BlockDump(RA_ARENA *pArena, void (*pfnLogDump)(void*, IMG_CHAR*, ...), void *pPrivData)
{
	RA_ARENA_ITERATOR *pIter = NULL;
	RA_ITERATOR_DATA sIterData;
	const IMG_UINT32 uiLineWidth = 64;

	IMG_UINT32 **papRegionArray = NULL;
	IMG_UINT32 uiRegionCount = 0;

	const IMG_UINT32 uiChunkSize = 32; /* 32-bit chunks */
	const IMG_UINT32 uiChunkCount = (uiLineWidth / uiChunkSize) * 2; /* This should equal 2 or a multiple of 2 */
	const IMG_UINT32 uiRegionSize = uiChunkSize * uiChunkCount;

	IMG_UINT32 uiRecognisedQuantum = 0;

	IMG_UINT64 uiLastBase = 0;
	IMG_UINT64 uiLastSize = 0;

	IMG_UINT32 i;
	IMG_UINT32 uiRemainder;
	PVRSRV_ERROR eError = PVRSRV_OK;

	IMG_UINT64 uiLargestFreeSegmentSize = 0;
	IMG_UINT32 uiFragPercentage = 0;

	/* -- papRegionArray Structure --
	 *  papRegionArray Indexes
	 *  |         Chunk 0      Chunk 1      Chunk 2      Chunk 3
	 *  v     |------------|------------|------------|------------|
	 * [0] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 | -- |
	 * [1] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 |    |
	 * [2] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 |    |
	 * [3] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 |    | Regions
	 * [4] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 |    |
	 * [5] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 |    |
	 * [6] -> | 0000000000 | 0000000000 | 0000000000 | 0000000000 | -- |
	 * ...
	 */

	if (pArena == NULL || pfnLogDump == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	pIter = RA_IteratorAcquire(pArena, IMG_TRUE);
	PVR_LOG_RETURN_IF_NOMEM(pIter, "RA_IteratorAcquire");

	uiRecognisedQuantum = pArena->uQuantum > 0 ? pArena->uQuantum : 4096;

	while (RA_IteratorNext(pIter, &sIterData))
	{
		if (!sIterData.bFree && sIterData.uiAddr >= uiLastBase)
		{
			uiLastBase = sIterData.uiAddr;
			uiLastSize = sIterData.uiSize;
		}
	}

	uiRegionCount = OSDivide64(uiLastBase + uiLastSize, uiRecognisedQuantum,
	                           &uiRemainder);
	uiRegionCount = OSDivide64(uiRegionCount, uiRegionSize, &uiRemainder);
	if (uiRemainder != 0 || uiRegionCount == 0)
	{
		uiRegionCount += 1;
	}

	papRegionArray = OSAllocZMem(sizeof(IMG_UINT32*) * uiRegionCount);
	PVR_LOG_GOTO_IF_NOMEM(papRegionArray, eError, cleanup_array);

	RA_IteratorReset(pIter);

	while (RA_IteratorNext(pIter, &sIterData))
	{
		IMG_UINT64 uiDataDivRecQuant;

		IMG_UINT32 uiAddrRegionIdx = 0;
		IMG_UINT32 uiAddrRegionOffset = 0;
		IMG_UINT32 uiAddrChunkIdx = 0;
		IMG_UINT32 uiAddrChunkOffset = 0;
		IMG_UINT32 uiAddrChunkShift; /* The bit-shift needed to fill the chunk */

		IMG_UINT32 uiQuantisedSize;
		IMG_UINT32 uiQuantisedSizeMod;
		IMG_UINT32 uiAllocLastRegionIdx = 0; /* The last region that this alloc appears in */
		IMG_UINT32 uiAllocChunkSize = 0; /* The number of chunks this alloc spans */

		IMG_INT32 iBitSetCount = 0;
		IMG_INT32 iOverflowCheck = 0;
		IMG_INT32 iOverflow = 0;
		IMG_UINT32 uiRegionIdx = 0;
		IMG_UINT32 uiChunkIdx = 0;

		/* If the current data is for a free block, use it to track largest
		 * contiguous free segment size.
		 */
		if (sIterData.bFree && sIterData.uiSize > uiLargestFreeSegmentSize)
		{
			uiLargestFreeSegmentSize = sIterData.uiSize;
			continue;
		}

		uiDataDivRecQuant = OSDivide64(sIterData.uiAddr, uiRecognisedQuantum,
		                               &uiRemainder);
		uiAddrRegionIdx = OSDivide64(uiDataDivRecQuant, uiRegionSize,
		                             &uiAddrRegionOffset);
		uiQuantisedSize = OSDivide64(sIterData.uiSize, uiRecognisedQuantum,
		                             &uiQuantisedSizeMod);

		uiAddrChunkIdx = uiAddrRegionOffset / uiChunkSize;
		uiAddrChunkOffset = uiAddrRegionOffset % uiChunkSize;
		uiAddrChunkShift = uiChunkSize - uiAddrChunkOffset;
		uiRegionIdx = uiAddrRegionIdx;
		uiChunkIdx = uiAddrChunkIdx;

		if ((uiQuantisedSize == 0) || (uiQuantisedSizeMod != 0))
		{
			uiQuantisedSize += 1;
		}

		uiAllocLastRegionIdx = OSDivide64(uiDataDivRecQuant + uiQuantisedSize - 1,
		                                  uiRegionSize, &uiRemainder);
		uiAllocChunkSize = (uiAddrChunkOffset + uiQuantisedSize) / uiChunkSize;

		if ((uiAddrChunkOffset + uiQuantisedSize) % uiChunkSize > 0)
		{
			uiAllocChunkSize += 1;
		}

		iBitSetCount = uiQuantisedSize;
		iOverflowCheck = uiQuantisedSize - uiAddrChunkShift;

		if (iOverflowCheck > 0)
		{
			iOverflow = iOverflowCheck;
			iBitSetCount = uiQuantisedSize - iOverflow;
		}

		/**
		 * Allocate memory to represent the chunks for each region the allocation
		 * spans. If one was already allocated before don't do it again.
		 */
		for (i = 0; uiAddrRegionIdx + i <= uiAllocLastRegionIdx; i++)
		{
			if (papRegionArray[uiAddrRegionIdx + i] == NULL)
			{
				papRegionArray[uiAddrRegionIdx + i] = OSAllocZMem(sizeof(IMG_UINT32) * uiChunkCount);
				PVR_LOG_GOTO_IF_NOMEM(papRegionArray[uiAddrRegionIdx + i], eError, cleanup_regions);
			}
		}

		for (i = 0; i < uiAllocChunkSize; i++)
		{
			if (uiChunkIdx >= uiChunkCount)
			{
				uiRegionIdx++;
				uiChunkIdx = 0;
			}

			if ((IMG_UINT32)iBitSetCount != uiChunkSize)
			{
				IMG_UINT32 uiBitMask = 0;

				uiBitMask = (1U << iBitSetCount) - 1;
				uiBitMask <<= (uiAddrChunkShift - iBitSetCount);

				papRegionArray[uiRegionIdx][uiChunkIdx] |= uiBitMask;
			}
			else
			{
				papRegionArray[uiRegionIdx][uiChunkIdx] |= 0xFFFFFFFF;
			}

			uiChunkIdx++;
			iOverflow -= uiChunkSize;
			iBitSetCount = iOverflow >= 0 ? uiChunkSize : uiChunkSize + iOverflow;
			if (iOverflow < 0)
			{
				uiAddrChunkShift = 32;
			}
		}
	}
	if (pArena->ui64FreeArenaSize && uiLargestFreeSegmentSize)
	{
		/* N.B This can look strange in a dual RA when comparing to the dump visualisation
		 * as spans that are freed are not included in the segment list, regardless it is
		 * an accurate representation for the spans in the Arena.
		 */
		uiFragPercentage = OSDivide64(100 * pArena->ui64FreeArenaSize,
		                              pArena->ui64FreeArenaSize + uiLargestFreeSegmentSize,
		                              &uiRemainder);
	}

	pfnLogDump(pPrivData, "~~~ '%s' Resource Arena Block Dump", pArena->name);
	pfnLogDump(pPrivData, "    Block Size: %uB", uiRecognisedQuantum);
	pfnLogDump(pPrivData,
	           "    Span Memory Usage: %"IMG_UINT64_FMTSPEC"B"
	           "    Free Span Memory: %"IMG_UINT64_FMTSPEC"B"
	           "    Largest Free Region Size: %"IMG_UINT64_FMTSPEC"B"
	           "    Percent Fragmented %u%%",
	           pArena->ui64TotalArenaSize,
	           pArena->ui64FreeArenaSize,
	           uiLargestFreeSegmentSize,
	           uiFragPercentage);
	pfnLogDump(pPrivData,
	           "===============================================================================");

	for (i = 0; i < uiRegionCount; i++)
	{
		static IMG_BOOL bEmptyRegion = IMG_FALSE;
		if (papRegionArray[i] != NULL)
		{
			IMG_CHAR pszLine[65];
			IMG_UINT32 j;

			bEmptyRegion = IMG_FALSE;
			pszLine[64] = '\0';

			for (j = 0; j < uiChunkCount; j+=2)
			{
				IMG_UINT8 uiBit = 0;
				IMG_UINT32 k;
				IMG_UINT64 uiLineAddress =
				    (i * uiRegionSize + (j >> 1) * uiLineWidth) * uiRecognisedQuantum;

				/**
				 * Move through each of the 32 bits in the chunk and check their
				 * value. If it is 1 we set the corresponding character to '#',
				 * otherwise it is set to '.' representing empty space
				 */
				for (k = 1 << 31; k != 0; k >>= 1)
				{
					pszLine[uiBit] = papRegionArray[i][j] & k ? '#' : '.';
					pszLine[32 + uiBit] = papRegionArray[i][j+1] & k ? '#' : '.';
					uiBit++;
				}

				pfnLogDump(pPrivData,
				           "| 0x%08"IMG_UINT64_FMTSPECx" | %s",
				           uiLineAddress,
				           pszLine);
			}
			OSFreeMem(papRegionArray[i]);
		}
		else
		{
			/* We only print this once per gap of n regions */
			if (!bEmptyRegion)
			{
				pfnLogDump(pPrivData, "     ....");
				bEmptyRegion = IMG_TRUE;
			}
		}
	}

	RA_IteratorRelease(pIter);

	OSFreeMem(papRegionArray);
	return eError;

cleanup_regions:
	for (i = 0; i < uiRegionCount; i++)
	{
		if (papRegionArray[i] != NULL)
		{
			OSFreeMem(papRegionArray[i]);
		}
	}

cleanup_array:
	OSFreeMem(papRegionArray);
	RA_IteratorRelease(pIter);

	return eError;
}
