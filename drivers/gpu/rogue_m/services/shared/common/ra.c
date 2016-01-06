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
 Implements generic resource allocation. The resource
 allocator was originally intended to manage address spaces.  In
 practice the resource allocator is generic and can manage arbitrary
 sets of integers.

 Resources are allocated from arenas. Arena's can be created with an
 initial span of resources. Further resources spans can be added to
 arenas. A call back mechanism allows an arena to request further
 resource spans on demand.

 Each arena maintains an ordered list of resource segments each
 described by a boundary tag. Each boundary tag describes a segment
 of resources which are either 'free', available for allocation, or
 'busy' currently allocated. Adjacent 'free' segments are always
 coallesced to avoid fragmentation.

 For allocation, all 'free' segments are kept on lists of 'free'
 segments in a table index by pvr_log2(segment size). ie Each table index
 n holds 'free' segments in the size range 2^n -> 2^(n+1) - 1.

 Allocation policy is based on an *almost* good fit strategy. 

 Allocated segments are inserted into a self scaling hash table which
 maps the base resource of the span to the relevant boundary
 tag. This allows the code to get back to the bounary tag without
 exporting explicit boundary tag references through the API.

 Each arena has an associated quantum size, all allocations from the
 arena are made in multiples of the basic quantum.

 On resource exhaustion in an arena, a callback if provided will be
 used to request further resources. Resouces spans allocated by the
 callback mechanism will be returned when freed (through one of the
 two callbacks).
*/ /**************************************************************************/

/* Issues:
 * - flags, flags are passed into the resource allocator but are not currently used.
 * - determination, of import size, is currently braindead.
 * - debug code should be moved out to own module and #ifdef'd
 */

#include "img_types.h"
#include "pvr_debug.h"
#include "pvrsrv_error.h"
#include "uniq_key_splay_tree.h"

#include "hash.h"
#include "ra.h"

#include "osfunc.h"
#include "allocmem.h"
#include "lock.h"

/* The initial, and minimum size of the live address -> boundary tag
   structure hash table. The value 64 is a fairly arbitrary
   choice. The hash table resizes on demand so the value choosen is
   not critical. */
#define MINIMUM_HASH_SIZE (64)


/* #define RA_VALIDATE */

#if defined(__KLOCWORK__)
  /* make sure Klocworks analyse all the code (including the debug one) */
  #if !defined(RA_VALIDATE)
    #define RA_VALIDATE
  #endif
#endif

#if (!defined(PVRSRV_NEED_PVR_ASSERT)) || (!defined(RA_VALIDATE))
  /* Disable the asserts unless explicitly told otherwise.  They slow the driver
     too much for other people */

  #undef PVR_ASSERT
  /* let's use a macro that really do not do anything when compiling in release
     mode! */
  #define PVR_ASSERT(x)
#endif

/* boundary tags, used to describe a resource segment */
struct _BT_
{
	enum bt_type
	{
		btt_free,				/* free resource segment */
		btt_live				/* allocated resource segment */
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
	struct _BT_ * next_free;
	struct _BT_ * prev_free;
	
	/* a user reference associated with this span, user references are
	 * currently only provided in the callback mechanism */
    IMG_HANDLE hPriv;

    /* Flags to match on this span */
    IMG_UINT32 uFlags;

};
typedef struct _BT_ BT;


/* resource allocation arena */
struct _RA_ARENA_
{
	/* arena name for diagnostics output */
	IMG_CHAR *name;

	/* allocations within this arena are quantum sized */
	RA_LENGTH_T uQuantum;

	/* import interface, if provided */
	IMG_BOOL (*pImportAlloc)(RA_PERARENA_HANDLE h,
							 RA_LENGTH_T uSize,
							 IMG_UINT32 uFlags,
							 RA_BASE_T *pBase,
							 RA_LENGTH_T *pActualSize,
                             RA_PERISPAN_HANDLE *phPriv);
	IMG_VOID (*pImportFree) (RA_PERARENA_HANDLE,
                             RA_BASE_T,
                             RA_PERISPAN_HANDLE hPriv);

	/* arbitrary handle provided by arena owner to be passed into the
	 * import alloc and free hooks */
	IMG_VOID *pImportHandle;

	IMG_PSPLAY_TREE per_flags_buckets;
	
	/* resource segment list */
	BT *pHeadSegment;

	/* segment address to boundary tag hash table */
	HASH_TABLE *pSegmentHash;

	/* Lock for this arena */
	POS_LOCK hLock;

	/* LockClass of this arena. This is used within lockdep to decide if a
	 * recursive call sequence with the same lock class is allowed or not. */
	IMG_UINT32 ui32LockClass;

	/* If TRUE, imports will not be split up. Allocations will always get their
	 * own import
	 */
	IMG_BOOL bNoSplit;
};

/*************************************************************************/ /*!
@Function       _RequestAllocFail
@Description    Default callback allocator used if no callback is
                specified, always fails to allocate further resources to the
                arena.
@Input          _h - callback handle
@Input          _uSize - requested allocation size
@Output         _pActualSize - actual allocation size
@Input          _pRef - user reference
@Input          _uflags - allocation flags
@Input          _pBase - receives allocated base
@Return         IMG_FALSE, this function always fails to allocate.
*/ /**************************************************************************/
static IMG_BOOL
_RequestAllocFail (RA_PERARENA_HANDLE _h,
                   RA_LENGTH_T _uSize,
                   IMG_UINT32 _uFlags,
                   RA_BASE_T *_pBase,
                   RA_LENGTH_T *_pActualSize,
                   RA_PERISPAN_HANDLE *_phPriv)
{
	PVR_UNREFERENCED_PARAMETER (_h);
	PVR_UNREFERENCED_PARAMETER (_uSize);
	PVR_UNREFERENCED_PARAMETER (_pActualSize);
	PVR_UNREFERENCED_PARAMETER (_phPriv);
	PVR_UNREFERENCED_PARAMETER (_uFlags);
	PVR_UNREFERENCED_PARAMETER (_pBase);

	return IMG_FALSE;
}


#if defined (HAS_BUILTIN_CTZLL)
    /* make sure to trigger an error if someone change the buckets or the bHasEltsMapping size
       the bHasEltsMapping is used to quickly determine the smallest bucket containing elements.
       therefore it must have at least as many bits has the buckets array have buckets. The RA
       implementation actually uses one more bit. */
    BLD_ASSERT((sizeof(((IMG_PSPLAY_TREE) 0)->buckets) / sizeof(((IMG_PSPLAY_TREE) 0)->buckets[0]))
			   < 8 * sizeof(((IMG_PSPLAY_TREE) 0)->bHasEltsMapping), ra_c);
#endif 


/*************************************************************************/ /*!
@Function       pvr_log2
@Description    Computes the floor of the log base 2 of a unsigned integer
@Input          n       Unsigned integer
@Return         Floor(Log2(n))
*/ /**************************************************************************/
#if defined(__GNUC__)
/* make sure to trigger a problem if someone changes the RA_LENGTH_T type
   indeed the __builtin_clzll is for unsigned long long variables.

   if someone changes RA_LENGTH to unsigned long, then use __builtin_clzl
   if it changes to unsigned int, use __builtin_clz

   if it changes for something bigger than unsigned long long, 
   then revert the pvr_log2 to the classic implementation */
BLD_ASSERT(sizeof(RA_LENGTH_T) == sizeof(unsigned long long), ra_c);

static inline IMG_UINT32 pvr_log2(RA_LENGTH_T n)
{
	PVR_ASSERT( n != 0 ); /* Log2 is not defined on 0 */

	return (8 * sizeof(RA_LENGTH_T)) - 1 - __builtin_clzll(n);
}
#else
static IMG_UINT32
pvr_log2 (RA_LENGTH_T n)
{
	IMG_UINT32 l = 0;

	PVR_ASSERT( n != 0 ); /* Log2 is not defined on 0 */

	n>>=1;
	while (n>0)
	{
		n>>=1;
		l++;
	}
	return l;
}
#endif


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
_IsInSegmentList (RA_ARENA *pArena,
                  BT *pBT)
{
	BT*  pBTScan;

	PVR_ASSERT (pArena != IMG_NULL);
	PVR_ASSERT (pBT != IMG_NULL);

	/* Walk the segment list until we see the BT pointer... */
	pBTScan = pArena->pHeadSegment;
	while (pBTScan != IMG_NULL  &&  pBTScan != pBT)
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
_IsInFreeList (RA_ARENA *pArena,
               BT *pBT)
{
	BT*  pBTScan;
	IMG_UINT32  uIndex;

	PVR_ASSERT (pArena != IMG_NULL);
	PVR_ASSERT (pBT != IMG_NULL);

	/* Look for the free list that holds BTs of this size... */
	uIndex  = pvr_log2 (pBT->uSize);
	PVR_ASSERT (uIndex < FREE_TABLE_LIMIT);

	pArena->per_flags_buckets = PVRSRVSplay(pBT->uFlags, pArena->per_flags_buckets);
	if ((pArena->per_flags_buckets == NULL) || (pArena->per_flags_buckets->flags != pBT->uFlags))
	{
		return 0;
	}
	else
	{
		pBTScan = pArena->per_flags_buckets->buckets[uIndex];
		while (pBTScan != IMG_NULL  &&  pBTScan != pBT)
		{
			pBTScan = pBTScan->next_free;
		}

		/* Test if we found it and then return */
		return (pBTScan == pBT);
	}
}

/* is_arena_valid should only be used in debug mode.
   it checks that some properties an arena must have are verified */
static int is_arena_valid(struct _RA_ARENA_ * arena)
{
	struct _BT_ * chunk;
#if defined(HAS_BUILTIN_CTZLL)
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

#if defined(HAS_BUILTIN_CTZLL)
    if (arena->per_flags_buckets != NULL)
	{
		for (i = 0; i < FREE_TABLE_LIMIT; ++i)
		{
			/* verify that the bHasEltsMapping is correct for this flags bucket */
			PVR_ASSERT( 
				((arena->per_flags_buckets->buckets[i] == NULL) &&
				 (( (arena->per_flags_buckets->bHasEltsMapping & ((IMG_ELTS_MAPPINGS) 1 << i)) == 0)))
				||
				((arena->per_flags_buckets->buckets[i] != NULL) &&
				 ((  (arena->per_flags_buckets->bHasEltsMapping & ((IMG_ELTS_MAPPINGS) 1 << i)) != 0)))
				);		
		}
	}
#endif	

	/* if arena was not valid, one of the assert before should have triggered */
	return 1;
}
#endif
/*************************************************************************/ /*!
@Function       _SegmentListInsertAfter
@Description    Insert a boundary tag into an arena segment list after a
                specified boundary tag.
@Input          pInsertionPoint  The insertion point.
@Input          pBT              The boundary tag to insert.
@Return         PVRSRV_OK (doesn't fail)
*/ /**************************************************************************/
static INLINE PVRSRV_ERROR
_SegmentListInsertAfter (BT *pInsertionPoint,
						 BT *pBT)
{
	PVR_ASSERT (pBT != IMG_NULL);
	PVR_ASSERT (pInsertionPoint != IMG_NULL);

	pBT->pNextSegment = pInsertionPoint->pNextSegment;
	pBT->pPrevSegment = pInsertionPoint;
	if (pInsertionPoint->pNextSegment != IMG_NULL)
	{
		pInsertionPoint->pNextSegment->pPrevSegment = pBT;
	}
	pInsertionPoint->pNextSegment = pBT;

	return PVRSRV_OK;
}

/*************************************************************************/ /*!
@Function       _SegmentListInsert
@Description    Insert a boundary tag into an arena segment list
@Input          pArena    The arena.
@Input          pBT       The boundary tag to insert.
@Return         PVRSRV_OK (doesn't fail)
*/ /**************************************************************************/
static INLINE PVRSRV_ERROR
_SegmentListInsert (RA_ARENA *pArena, BT *pBT)
{
	PVRSRV_ERROR eError = PVRSRV_OK;
	PVR_ASSERT (!_IsInSegmentList(pArena, pBT));

	/* insert into the segment chain */
	pBT->pNextSegment = pArena->pHeadSegment;
	pArena->pHeadSegment = pBT;
	if (pBT->pNextSegment != NULL)
	{
		pBT->pNextSegment->pPrevSegment = pBT;
	}

	pBT->pPrevSegment = NULL;

	return eError;
}

/*************************************************************************/ /*!
@Function       _SegmentListRemove
@Description    Remove a boundary tag from an arena segment list.
@Input          pArena    The arena.
@Input          pBT       The boundary tag to remove.
*/ /**************************************************************************/
static IMG_VOID
_SegmentListRemove (RA_ARENA *pArena, BT *pBT)
{
	PVR_ASSERT (_IsInSegmentList(pArena, pBT));
	
	if (pBT->pPrevSegment == IMG_NULL)
		pArena->pHeadSegment = pBT->pNextSegment;
	else
		pBT->pPrevSegment->pNextSegment = pBT->pNextSegment;

	if (pBT->pNextSegment != IMG_NULL)
		pBT->pNextSegment->pPrevSegment = pBT->pPrevSegment;
}


/*************************************************************************/ /*!
@Function       _BuildBT
@Description    Construct a boundary tag for a free segment.
@Input          base     The base of the resource segment.
@Input          uSize    The extent of the resouce segment.
@Input          uFlags   The flags to give to the boundary tag
@Return         Boundary tag or NULL
*/ /**************************************************************************/
static BT *
_BuildBT (RA_BASE_T base,
          RA_LENGTH_T uSize,
          RA_FLAGS_T uFlags
          )
{
	BT *pBT;

	pBT = OSAllocMem(sizeof(BT));
    if (pBT == IMG_NULL)
	{
		return IMG_NULL;
	}

	OSCachedMemSet(pBT, 0, sizeof(BT));

	pBT->is_leftmost = 1;
	pBT->is_rightmost = 1;
	pBT->type = btt_live;
	pBT->base = base;
	pBT->uSize = uSize;
    pBT->uFlags = uFlags;
	pBT->free_import = 0;

	return pBT;
}


/*************************************************************************/ /*!
@Function       _SegmentSplit
@Description    Split a segment into two, maintain the arena segment list. The
                boundary tag should not be in the free table. Neither the
                original or the new neighbour bounary tag will be in the free
                table.
@Input          pBT       The boundary tag to split.
@Input          uSize     The required segment size of boundary tag after
                          splitting.
@Return         New neighbour boundary tag or NULL.
*/ /**************************************************************************/
static BT *
_SegmentSplit (BT *pBT, RA_LENGTH_T uSize)
{
	BT *pNeighbour;

	pNeighbour = _BuildBT(pBT->base + uSize, pBT->uSize - uSize, pBT->uFlags);
    if (pNeighbour == IMG_NULL)
    {
        return IMG_NULL;
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
static IMG_VOID
_FreeListInsert (RA_ARENA *pArena, BT *pBT)
{
	IMG_UINT32 uIndex;
	uIndex = pvr_log2 (pBT->uSize);

	PVR_ASSERT (uIndex < FREE_TABLE_LIMIT);
	PVR_ASSERT (!_IsInFreeList(pArena, pBT));

	pBT->type = btt_free;

	pArena->per_flags_buckets = PVRSRVSplay(pBT->uFlags, pArena->per_flags_buckets);
	/* the flags item in the splay tree must have been created before-hand by
	   _InsertResource */
	PVR_ASSERT(pArena->per_flags_buckets != NULL);
	PVR_ASSERT(pArena->per_flags_buckets->buckets != NULL);

	pBT->next_free = pArena->per_flags_buckets->buckets[uIndex];
	if (pBT->next_free != NULL)
	{
		pBT->next_free->prev_free = pBT;
	}
	pBT->prev_free = NULL;
	pArena->per_flags_buckets->buckets[uIndex] = pBT;

#if defined(HAS_BUILTIN_CTZLL)
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
static IMG_VOID
_FreeListRemove (RA_ARENA *pArena, BT *pBT)
{
	IMG_UINT32 uIndex;
	uIndex = pvr_log2 (pBT->uSize);

	PVR_ASSERT (uIndex < FREE_TABLE_LIMIT);
	PVR_ASSERT (_IsInFreeList(pArena, pBT));

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
		PVR_ASSERT(pArena->per_flags_buckets->buckets != NULL);

		pArena->per_flags_buckets->buckets[uIndex] = pBT->next_free;
#if defined(HAS_BUILTIN_CTZLL)
		if (pArena->per_flags_buckets->buckets[uIndex] == NULL)
		{
			/* there is no more elements in this bucket. Update the mapping. */
			pArena->per_flags_buckets->bHasEltsMapping &= ~((IMG_ELTS_MAPPINGS) 1 << uIndex);
		}
#endif
	}
	

	PVR_ASSERT (!_IsInFreeList(pArena, pBT));
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
                IMG_NULL on failure
*/ /**************************************************************************/
static BT *
_InsertResource (RA_ARENA *pArena,
                 RA_BASE_T base,
                 RA_LENGTH_T uSize,
                 RA_FLAGS_T uFlags
                 )
{
	BT *pBT;
	PVR_ASSERT (pArena!=IMG_NULL);

	pBT = _BuildBT (base, uSize, uFlags);

	if (pBT != IMG_NULL)
	{
		IMG_PSPLAY_TREE tmp = PVRSRVInsert(pBT->uFlags, pArena->per_flags_buckets);
		if (tmp == NULL)
		{
			OSFreeMem(pBT);
			return NULL;
		}
		
		pArena->per_flags_buckets = tmp;
		_SegmentListInsert (pArena, pBT);
		_FreeListInsert (pArena, pBT);
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
                or IMG_NULL on failure.
*/ /**************************************************************************/
static INLINE BT *
_InsertResourceSpan (RA_ARENA *pArena,
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
_RemoveResourceSpan (RA_ARENA *pArena, BT *pBT)
{
	PVR_ASSERT (pArena!=IMG_NULL);
	PVR_ASSERT (pBT!=IMG_NULL);

	if (pBT->free_import &&
		pBT->is_leftmost &&
		pBT->is_rightmost)
	{
		_SegmentListRemove (pArena, pBT);
		pArena->pImportFree (pArena->pImportHandle, pBT->base, pBT->hPriv);
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
static IMG_VOID
_FreeBT (RA_ARENA *pArena, BT *pBT)
{
	BT *pNeighbour;

	PVR_ASSERT (pArena!=IMG_NULL);
	PVR_ASSERT (pBT!=IMG_NULL);
	PVR_ASSERT (!_IsInFreeList(pArena, pBT));

	/* try and coalesce with left neighbour */
	pNeighbour = pBT->pPrevSegment;
	if ((!pBT->is_leftmost)	&& (pNeighbour->type == btt_free))
	{
		/* Sanity check. */
		PVR_ASSERT(pNeighbour->base + pNeighbour->uSize == pBT->base);

		_FreeListRemove (pArena, pNeighbour);
		_SegmentListRemove (pArena, pNeighbour);
		pBT->base = pNeighbour->base;

		pBT->uSize += pNeighbour->uSize;
		pBT->is_leftmost = pNeighbour->is_leftmost;
        OSFreeMem(pNeighbour);
	}

	/* try to coalesce with right neighbour */
	pNeighbour = pBT->pNextSegment;
	if ((!pBT->is_rightmost) && (pNeighbour->type == btt_free))
	{
		/* sanity check */
		PVR_ASSERT(pBT->base + pBT->uSize == pNeighbour->base);

		_FreeListRemove (pArena, pNeighbour);
		_SegmentListRemove (pArena, pNeighbour);
		pBT->uSize += pNeighbour->uSize;
		pBT->is_rightmost = pNeighbour->is_rightmost;
		OSFreeMem(pNeighbour);
	}

	if (_RemoveResourceSpan(pArena, pBT) == IMG_FALSE)
	{
		_FreeListInsert (pArena, pBT);
		PVR_ASSERT( (!pBT->is_rightmost) || (!pBT->is_leftmost) || (!pBT->free_import) );
	}
	
	PVR_ASSERT(is_arena_valid(pArena));
}


/*
  This function returns the first element in a bucket that can be split
  in a way that one of the subsegment can meet the size and alignment
  criteria.

  The first_elt is the bucket to look into. Remember that a bucket is
  implemented as a pointer to the first element of the linked list.

  nb_max_try is used to limit the number of elements considered.
  This is used to only consider the first nb_max_try elements in the
  free-list. The special value ~0 is used to say unlimited i.e. consider
  all elements in the free list 
 */
static INLINE
struct _BT_ * find_chunk_in_bucket(struct _BT_ * first_elt,
								   RA_LENGTH_T uSize,
								   RA_LENGTH_T uAlignment,
								   unsigned int nb_max_try)
{
	struct _BT_ * walker;

	for (walker = first_elt; (walker != NULL) && (nb_max_try != 0); walker = walker->next_free)
	{
		const RA_BASE_T aligned_base = (uAlignment > 1) ?
			(walker->base + uAlignment - 1) & ~(uAlignment - 1)
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
@Function       _AttemptAllocAligned
@Description    Attempt an allocation from an arena.
@Input          pArena       The arena.
@Input          uSize        The requested allocation size.
@Output         phPriv		 The user references associated with
                             the imported segment. (optional)
@Input          flags        Allocation flags
@Input          uAlignment   Required uAlignment, or 0. 
                             Must be a power of 2 if not 0
@Output         base         Allocated resource base (non optional, must not be NULL)
@Return         IMG_FALSE failure
                IMG_TRUE success
*/ /**************************************************************************/
static IMG_BOOL
_AttemptAllocAligned (RA_ARENA *pArena,
					  RA_LENGTH_T uSize,
					  IMG_UINT32 uFlags,
					  RA_LENGTH_T uAlignment,
					  RA_BASE_T *base,
                      RA_PERISPAN_HANDLE *phPriv) /* this is the "per-import" private data */
{

	IMG_UINT32 index_low;
	IMG_UINT32 index_high; 
	IMG_UINT32 i; 
	struct _BT_ * pBT = NULL;
	RA_BASE_T aligned_base;

	PVR_ASSERT (pArena!=IMG_NULL);
	PVR_ASSERT (base != NULL);

	pArena->per_flags_buckets = PVRSRVSplay(uFlags, pArena->per_flags_buckets);
	if ((pArena->per_flags_buckets == NULL) || (pArena->per_flags_buckets->ui32Flags != uFlags))
	{
		/* no chunks with these flags. */
		return IMG_FALSE;
	}

	index_low = pvr_log2(uSize);
	index_high = pvr_log2(uSize + uAlignment - 1);
	
	PVR_ASSERT(index_low < FREE_TABLE_LIMIT);
	PVR_ASSERT(index_high < FREE_TABLE_LIMIT);
	PVR_ASSERT(index_low <= index_high);

#if defined(HAS_BUILTIN_CTZLL)
	i = __builtin_ctzll((IMG_ELTS_MAPPINGS) (~((1 << (index_high + 1)) - 1)) & pArena->per_flags_buckets->bHasEltsMapping);
#else
 	for (i = index_high + 1; (i < FREE_TABLE_LIMIT) && (pArena->per_flags_buckets->buckets[i] == NULL); ++i)
	{
	}
#endif
	PVR_ASSERT(i <= FREE_TABLE_LIMIT);

	if (i != FREE_TABLE_LIMIT)
	{
		/* since we start at index_high + 1, we are guarantee to exit */
		pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], uSize, uAlignment, 1);
	}
	else
	{
		for (i = index_high; (i != index_low - 1) && (pBT == NULL); --i)
		{
			pBT = find_chunk_in_bucket(pArena->per_flags_buckets->buckets[i], uSize, uAlignment, (unsigned int) ~0);			
		}
	}

	if (pBT == NULL)
	{
		return IMG_FALSE;
	}

	aligned_base = (uAlignment > 1) ? (pBT->base + uAlignment - 1) & ~(uAlignment - 1) : pBT->base;

	_FreeListRemove (pArena, pBT);

	if(pArena->bNoSplit)
	{
		goto nosplit;
	}

	/* with uAlignment we might need to discard the front of this segment */
	if (aligned_base > pBT->base)
	{
		BT *pNeighbour;
		pNeighbour = _SegmentSplit (pBT, (RA_LENGTH_T)(aligned_base - pBT->base));
		/* partition the buffer, create a new boundary tag */
		if (pNeighbour == NULL)
		{
			PVR_DPF ((PVR_DBG_ERROR, "%s: Front split failed", __FUNCTION__));
			/* Put pBT back in the list */
			_FreeListInsert (pArena, pBT);
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
			PVR_DPF ((PVR_DBG_ERROR, "%s: Back split failed", __FUNCTION__));
			/* Put pBT back in the list */
			_FreeListInsert (pArena, pBT);
			return IMG_FALSE;
		}
	
		_FreeListInsert (pArena, pNeighbour);
	}
nosplit:
	pBT->type = btt_live;
	
	if (!HASH_Insert_Extended (pArena->pSegmentHash, &pBT->base, (IMG_UINTPTR_T)pBT))
	{
		_FreeBT (pArena, pBT);
		return IMG_FALSE;
	}
	
	if (phPriv != IMG_NULL)
		*phPriv = pBT->hPriv;
	
	*base = pBT->base;
	
	return IMG_TRUE;
}



/*************************************************************************/ /*!
@Function       RA_Create
@Description    To create a resource arena.
@Input          name          The name of the arena for diagnostic purposes.
@Input          base          The base of an initial resource span or 0.
@Input          uSize         The size of an initial resource span or 0.
@Input          uFlags        The flags of an initial resource span or 0.
@Input          ulog2Quantum  The arena allocation quantum.
@Input          imp_alloc     A resource allocation callback or 0.
@Input          imp_free      A resource de-allocation callback or 0.
@Input          pImportHandle Handle passed to alloc and free or 0.
@Input          bNoSplit      Disable splitting up imports.
@Return         arena handle, or IMG_NULL.
*/ /**************************************************************************/
IMG_INTERNAL RA_ARENA *
RA_Create (IMG_CHAR *name,
		   RA_LOG2QUANTUM_T uLog2Quantum,
		   IMG_UINT32 ui32LockClass,
		   IMG_BOOL (*imp_alloc)(RA_PERARENA_HANDLE h, 
                                 RA_LENGTH_T uSize,
                                 RA_FLAGS_T _flags, 
                                 /* returned data */
                                 RA_BASE_T *pBase,
                                 RA_LENGTH_T *pActualSize,
                                 RA_PERISPAN_HANDLE *phPriv),
		   IMG_VOID (*imp_free) (RA_PERARENA_HANDLE,
                                 RA_BASE_T,
                                 RA_PERISPAN_HANDLE),
		   RA_PERARENA_HANDLE arena_handle,
		   IMG_BOOL bNoSplit)
{
	RA_ARENA *pArena;
	PVRSRV_ERROR eError;

	if (name == NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR, "RA_Create: invalid parameter 'name' (NULL not accepted)"));
		return NULL;
	}
	
	PVR_DPF ((PVR_DBG_MESSAGE, "RA_Create: name='%s'", name));

	pArena = OSAllocMem(sizeof (*pArena));
    if (pArena == IMG_NULL)
	{
		goto arena_fail;
	}

	eError = OSLockCreate(&pArena->hLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		goto lock_fail;
	}

	pArena->pSegmentHash = HASH_Create_Extended(MINIMUM_HASH_SIZE, sizeof(RA_BASE_T), HASH_Func_Default, HASH_Key_Comp_Default);

	if (pArena->pSegmentHash==IMG_NULL)
	{
		goto hash_fail;
	}

	pArena->name = name;
	pArena->pImportAlloc = (imp_alloc!=IMG_NULL) ? imp_alloc : &_RequestAllocFail;
	pArena->pImportFree = imp_free;
	pArena->pImportHandle = arena_handle;
	pArena->pHeadSegment = IMG_NULL;
	pArena->uQuantum = (IMG_UINT64) (1 << uLog2Quantum);
	pArena->per_flags_buckets = NULL;
	pArena->ui32LockClass = ui32LockClass;
	pArena->bNoSplit = bNoSplit;

	PVR_ASSERT(is_arena_valid(pArena));
	return pArena;

hash_fail:
	OSLockDestroy(pArena->hLock);
lock_fail:
	OSFreeMem(pArena);
	/*not nulling pointer, out of scope*/
arena_fail:
	return IMG_NULL;
}

/*************************************************************************/ /*!
@Function       RA_Delete
@Description    To delete a resource arena. All resources allocated from
                the arena must be freed before deleting the arena.
@Input          pArena        The arena to delete.
*/ /**************************************************************************/
IMG_INTERNAL IMG_VOID
RA_Delete (RA_ARENA *pArena)
{
	IMG_UINT32 uIndex;

	PVR_ASSERT(pArena != IMG_NULL);

	if (pArena == IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR,"RA_Delete: invalid parameter - pArena"));
		return;
	}

	PVR_ASSERT(is_arena_valid(pArena));

	PVR_DPF ((PVR_DBG_MESSAGE,
			  "RA_Delete: name='%s'", pArena->name));

	while (pArena->pHeadSegment != IMG_NULL)
	{
		BT *pBT = pArena->pHeadSegment;

		if (pBT->type != btt_free)
		{
			PVR_DPF ((PVR_DBG_ERROR, "RA_Delete: allocations still exist in the arena that is being destroyed"));
			PVR_DPF ((PVR_DBG_ERROR, "Likely Cause: client drivers not freeing alocations before destroying devmemcontext"));
			PVR_DPF ((PVR_DBG_ERROR, "RA_Delete: base = 0x%llx size=0x%llx",
					  (unsigned long long)pBT->base, (unsigned long long)pBT->uSize));
		}
		else
		{
			_FreeListRemove(pArena, pBT);
		}

		_SegmentListRemove (pArena, pBT);
		OSFreeMem(pBT);
		/*not nulling original pointer, it has changed*/
	}

	while (pArena->per_flags_buckets != NULL)
	{
		for (uIndex=0; uIndex<FREE_TABLE_LIMIT; uIndex++)
		{
			PVR_ASSERT(pArena->per_flags_buckets->buckets[uIndex] == IMG_NULL);
		}

		pArena->per_flags_buckets = PVRSRVDelete(pArena->per_flags_buckets->ui32Flags, pArena->per_flags_buckets);
	}

	HASH_Delete (pArena->pSegmentHash);
	OSLockDestroy(pArena->hLock);
	OSFreeMem(pArena);
	/*not nulling pointer, copy on stack*/
}

/*************************************************************************/ /*!
@Function       RA_Add
@Description    To add a resource span to an arena. The span must not
                overlapp with any span previously added to the arena.
@Input          pArena     The arena to add a span into.
@Input          base       The base of the span.
@Input          uSize      The extent of the span.
@Input			uFlags	   the flags of the new import
@Input			hPriv	   a private handle associate to the span. (reserved for user)
@Return         IMG_TRUE - Success
                IMG_FALSE - failure
*/ /**************************************************************************/
IMG_INTERNAL IMG_BOOL
RA_Add (RA_ARENA *pArena,
		RA_BASE_T base,
		RA_LENGTH_T uSize,
		RA_FLAGS_T uFlags,
		RA_PERISPAN_HANDLE hPriv)
{
	struct _BT_* bt;
	PVR_ASSERT (pArena != IMG_NULL);
	PVR_ASSERT (uSize != 0);

	if (pArena == IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR,"RA_Add: invalid parameter - pArena"));
		return IMG_FALSE;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));
	PVR_DPF ((PVR_DBG_MESSAGE, "RA_Add: name='%s', "
              "base=0x%llx, size=0x%llx", pArena->name,
			  (unsigned long long)base, (unsigned long long)uSize));

	uSize = (uSize + pArena->uQuantum - 1) & ~(pArena->uQuantum - 1);
	bt = _InsertResource(pArena, base, uSize, uFlags);
	if (bt != NULL)
	{
		bt->hPriv = hPriv;
	}

	PVR_ASSERT(is_arena_valid(pArena));
	OSLockRelease(pArena->hLock);

	return bt != NULL;
}

/*************************************************************************/ /*!
@Function       RA_Alloc
@Description    To allocate resource from an arena.
@Input          pArena         The arena
@Input          uRequestSize   The size of resource segment requested.
@Output         pActualSize    The actual size of resource segment
                               allocated, typcially rounded up by quantum.
@Output         phPriv         The user reference associated with allocated resource span.
@Input          uFlags         Flags influencing allocation policy.
@Input          uAlignment     The uAlignment constraint required for the
                               allocated segment, use 0 if uAlignment not required, otherwise
                               must be a power of 2.
@Output         base           Allocated base resource
@Return         IMG_TRUE - success
                IMG_FALSE - failure
*/ /**************************************************************************/
IMG_INTERNAL IMG_BOOL
RA_Alloc (RA_ARENA *pArena,
		  RA_LENGTH_T uRequestSize,
		  RA_FLAGS_T uFlags,
		  RA_LENGTH_T uAlignment,
		  RA_BASE_T *base,
		  RA_LENGTH_T *pActualSize,
          RA_PERISPAN_HANDLE *phPriv)
{
	IMG_BOOL bResult;
	RA_LENGTH_T uSize = uRequestSize;

	PVR_ASSERT (pArena!=IMG_NULL);
	PVR_ASSERT (uSize > 0);

	if (pArena == IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR,"RA_Alloc: invalid parameter - pArena"));
		return IMG_FALSE;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));

	if (pActualSize != IMG_NULL)
	{
		*pActualSize = uSize;
	}

	/* Must be a power of 2 or 0 */
	PVR_ASSERT((uAlignment == 0) || (uAlignment & (uAlignment - 1)) == 0);

	PVR_DPF ((PVR_DBG_MESSAGE,
			  "RA_Alloc: arena='%s', size=0x%llx(0x%llx), "
              "alignment=0x%llx", pArena->name,
			  (unsigned long long)uSize, (unsigned long long)uRequestSize,
			  (unsigned long long)uAlignment));

	/* if allocation failed then we might have an import source which
	   can provide more resource, else we will have to fail the
	   allocation to the caller. */
	bResult = _AttemptAllocAligned (pArena, uSize, uFlags, uAlignment, base, phPriv);
	if (!bResult)
	{
        IMG_HANDLE hPriv;
		RA_BASE_T import_base;
		RA_LENGTH_T uImportSize = uSize;

		/*
			Ensure that we allocate sufficient space to meet the uAlignment
			constraint
		 */
		if (uAlignment > pArena->uQuantum)
		{
			uImportSize += (uAlignment - pArena->uQuantum);
		}

		/* ensure that we import according to the quanta of this arena */
		uImportSize = (uImportSize + pArena->uQuantum - 1) & ~(pArena->uQuantum - 1);

		bResult =
			pArena->pImportAlloc (pArena->pImportHandle, uImportSize, uFlags,
                                  &import_base, &uImportSize, &hPriv);
		if (bResult)
		{
			BT *pBT;
			pBT = _InsertResourceSpan (pArena, import_base, uImportSize, uFlags);
			/* successfully import more resource, create a span to
			   represent it and retry the allocation attempt */
			if (pBT == IMG_NULL)
			{
				/* insufficient resources to insert the newly acquired span,
				   so free it back again */
				pArena->pImportFree(pArena->pImportHandle, import_base, hPriv);

				PVR_DPF ((PVR_DBG_MESSAGE, "RA_Alloc: name='%s', "
                          "size=0x%llx failed!", pArena->name,
						  (unsigned long long)uSize));
				/* RA_Dump (arena); */
				OSLockRelease(pArena->hLock);
				return IMG_FALSE;
			}


            pBT->hPriv = hPriv;

			bResult = _AttemptAllocAligned(pArena, uSize, uFlags, uAlignment, base, phPriv);
			if (!bResult)
			{
				PVR_DPF ((PVR_DBG_ERROR,
						  "RA_Alloc: name='%s' second alloc failed!",
						  pArena->name));

				/*
				  On failure of _AttemptAllocAligned() depending on the exact point
				  of failure, the imported segment may have been used and freed, or
				  left untouched. If the later, we need to return it.
				*/
				_FreeBT(pArena, pBT);
			}
			else
			{
				/* Check if the new allocation was in the span we just added... */
				if (*base < import_base  ||  *base > (import_base + uImportSize))
				{
					PVR_DPF ((PVR_DBG_ERROR,
							  "RA_Alloc: name='%s' alloc did not occur in the imported span!",
							  pArena->name));

					/*
					  Remove the imported span which should not be in use (if it is then
					  that is okay, but essentially no span should exist that is not used).
					*/
					_FreeBT(pArena, pBT);
				}
			}
		}
	}

	PVR_DPF ((PVR_DBG_MESSAGE, "RA_Alloc: name='%s', size=0x%llx, "
              "*base=0x%llx = %d",pArena->name, (unsigned long long)uSize,
			  (unsigned long long)*base, bResult));

	PVR_ASSERT(is_arena_valid(pArena));

	OSLockRelease(pArena->hLock);
	return bResult;
}




/*************************************************************************/ /*!
@Function       RA_Free
@Description    To free a resource segment.
@Input          pArena     The arena the segment was originally allocated from.
@Input          base       The base of the resource span to free.
*/ /**************************************************************************/
IMG_INTERNAL IMG_VOID
RA_Free (RA_ARENA *pArena, RA_BASE_T base)
{
	BT *pBT;

	PVR_ASSERT (pArena != IMG_NULL);

	if (pArena == IMG_NULL)
	{
		PVR_DPF ((PVR_DBG_ERROR,"RA_Free: invalid parameter - pArena"));
		return;
	}

	OSLockAcquireNested(pArena->hLock, pArena->ui32LockClass);
	PVR_ASSERT(is_arena_valid(pArena));

	PVR_DPF ((PVR_DBG_MESSAGE, "RA_Free: name='%s', base=0x%llx", pArena->name,
			  (unsigned long long)base));

	pBT = (BT *) HASH_Remove_Extended (pArena->pSegmentHash, &base);
	PVR_ASSERT (pBT != IMG_NULL);

	if (pBT)
	{
		PVR_ASSERT (pBT->base == base);
		_FreeBT (pArena, pBT);
	}

	PVR_ASSERT(is_arena_valid(pArena));
	OSLockRelease(pArena->hLock);
}
