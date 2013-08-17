/*************************************************************************/ /*!
@Title          Resource Handle Manager
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provide resource handle management
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

#if defined(PVR_SECURE_HANDLES)
/* See handle.h for a description of the handle API. */

/*
 * There is no locking here.  It is assumed the code is used in a single
 * threaded environment.  In particular, it is assumed that the code will
 * never be called from an interrupt handler.
 *
 * The implmentation supports movable handle structures, allowing the address
 * of a handle structure to change without having to fix up pointers in
 * any of the handle structures.  For example, the linked list mechanism
 * used to link subhandles together uses handle array indices rather than
 * pointers to the structures themselves.
 */

#include <stddef.h>

#include "services_headers.h"
#include "handle.h"

#ifdef	DEBUG
#define	HANDLE_BLOCK_SHIFT	2
#else
#define	HANDLE_BLOCK_SHIFT	8
#endif

#define	DIVIDE_BY_BLOCK_SIZE(i)		(((IMG_UINT32)(i)) >> HANDLE_BLOCK_SHIFT)
#define	MULTIPLY_BY_BLOCK_SIZE(i)	(((IMG_UINT32)(i)) << HANDLE_BLOCK_SHIFT)

#define HANDLE_BLOCK_SIZE       MULTIPLY_BY_BLOCK_SIZE(1)
#define	HANDLE_SUB_BLOCK_MASK	(HANDLE_BLOCK_SIZE - 1)
#define	HANDLE_BLOCK_MASK	(~(HANDLE_SUB_BLOCK_MASK))

#define	HANDLE_HASH_TAB_INIT_SIZE	32

#define	INDEX_IS_VALID(psBase, i) ((i) < (psBase)->ui32TotalHandCount)

/* Valid handles are never NULL, but handle array indices are based from 0 */
#define	INDEX_TO_HANDLE(i) ((IMG_HANDLE)((IMG_UINTPTR_T)(i) + 1))
#define	HANDLE_TO_INDEX(h) ((IMG_UINT32)(IMG_UINTPTR_T)(h) - 1)


#define	INDEX_TO_BLOCK_INDEX(i)		DIVIDE_BY_BLOCK_SIZE(i)
#define BLOCK_INDEX_TO_INDEX(i)		MULTIPLY_BY_BLOCK_SIZE(i)
#define INDEX_TO_SUB_BLOCK_INDEX(i)	((i) & HANDLE_SUB_BLOCK_MASK)

#define INDEX_TO_INDEX_STRUCT_PTR(psArray, i) (&((psArray)[INDEX_TO_BLOCK_INDEX(i)]))
#define	BASE_AND_INDEX_TO_INDEX_STRUCT_PTR(psBase, i) INDEX_TO_INDEX_STRUCT_PTR((psBase)->psHandleArray, i)

#define	INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, i) (BASE_AND_INDEX_TO_INDEX_STRUCT_PTR(psBase, i)->ui32FreeHandBlockCount)

#define INDEX_TO_HANDLE_STRUCT_PTR(psBase, i) (BASE_AND_INDEX_TO_INDEX_STRUCT_PTR(psBase, i)->psHandle + INDEX_TO_SUB_BLOCK_INDEX(i))

#define	HANDLE_TO_HANDLE_STRUCT_PTR(psBase, h) (INDEX_TO_HANDLE_STRUCT_PTR(psBase, HANDLE_TO_INDEX(h)))

#define	HANDLE_PTR_TO_INDEX(psHandle) ((psHandle)->ui32Index)
#define	HANDLE_PTR_TO_HANDLE(psHandle) INDEX_TO_HANDLE(HANDLE_PTR_TO_INDEX(psHandle))

#define	ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(a) (HANDLE_BLOCK_MASK & (a))
#define	ROUND_UP_TO_MULTIPLE_OF_BLOCK_SIZE(a) ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE((a) + HANDLE_BLOCK_SIZE - 1)

#define	DEFAULT_MAX_HANDLE		0x7fffffffu
#define	DEFAULT_MAX_INDEX_PLUS_ONE	ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(DEFAULT_MAX_HANDLE)

#define	HANDLES_BATCHED(psBase) ((psBase)->ui32HandBatchSize != 0)

#define HANDLE_ARRAY_SIZE(handleCount) DIVIDE_BY_BLOCK_SIZE(ROUND_UP_TO_MULTIPLE_OF_BLOCK_SIZE(handleCount))

#define	SET_FLAG(v, f) ((IMG_VOID)((v) |= (f)))
#define	CLEAR_FLAG(v, f) ((IMG_VOID)((v) &= ~(f)))
#define	TEST_FLAG(v, f) ((IMG_BOOL)(((v) & (f)) != 0))

#define	TEST_ALLOC_FLAG(psHandle, f) TEST_FLAG((psHandle)->eFlag, f)

#define	SET_INTERNAL_FLAG(psHandle, f) SET_FLAG((psHandle)->eInternalFlag, f)
#define	CLEAR_INTERNAL_FLAG(psHandle, f) CLEAR_FLAG((psHandle)->eInternalFlag, f)
#define	TEST_INTERNAL_FLAG(psHandle, f) TEST_FLAG((psHandle)->eInternalFlag, f)

#define	BATCHED_HANDLE(psHandle) TEST_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED)

#define	SET_BATCHED_HANDLE(psHandle) SET_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED)

#define	SET_UNBATCHED_HANDLE(psHandle) CLEAR_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED)

#define	BATCHED_HANDLE_PARTIALLY_FREE(psHandle) TEST_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED_PARTIALLY_FREE)

#define SET_BATCHED_HANDLE_PARTIALLY_FREE(psHandle) SET_INTERNAL_FLAG(psHandle, INTERNAL_HANDLE_FLAG_BATCHED_PARTIALLY_FREE)

#define	HANDLE_STRUCT_IS_FREE(psHandle) ((psHandle)->eType == PVRSRV_HANDLE_TYPE_NONE && (psHandle)->eInternalFlag == INTERNAL_HANDLE_FLAG_NONE)

#ifdef	MIN
#undef MIN
#endif

#define	MIN(x, y) (((x) < (y)) ? (x) : (y))

/*
 * Linked list structure.  Used for both the list head and list items.
 * Array indices, rather than pointers, are used to point to the next and
 * previous items on the list.
 */
struct sHandleList
{
	IMG_UINT32 ui32Prev;
	IMG_UINT32 ui32Next;
	IMG_HANDLE hParent;
};

enum ePVRSRVInternalHandleFlag
{
	INTERNAL_HANDLE_FLAG_NONE = 0x00,
	INTERNAL_HANDLE_FLAG_BATCHED = 0x01,
	INTERNAL_HANDLE_FLAG_BATCHED_PARTIALLY_FREE = 0x02,
};

/* Handle structure */
struct sHandle
{
	/* Handle type */
	PVRSRV_HANDLE_TYPE eType;

	/* Pointer to the data that the handle represents */
	IMG_VOID *pvData;

	/*
	 * When handles are on the free list, the value of the "next index
	 * plus one field" has the following meaning:
	 * zero - next handle is the one that follows this one,
	 * nonzero - the index of the next handle is the value minus one.
	 * This scheme means handle space can be initialised to all zeros.
	 *
	 * When this field is used to link together handles on a list
	 * other than the free list, zero indicates the end of the
	 * list, with nonzero the same as above.
	 */
	IMG_UINT32 ui32NextIndexPlusOne;

	/* Internal flags */
	enum ePVRSRVInternalHandleFlag eInternalFlag;

	/* Flags specified when the handle was allocated */
	PVRSRV_HANDLE_ALLOC_FLAG eFlag;

	/* Index of this handle in the handle array */
	IMG_UINT32 ui32Index;

	/* List head for subhandles of this handle */
	struct sHandleList sChildren;

	/* List entry for sibling subhandles */
	struct sHandleList sSiblings;
};

/* Handle array index structure.
 * The handle array is an array of index structures, reallocated as the number of
 * handles increases.
 * NOTE: There is one index structure per block of handles.
 */
struct sHandleIndex
{
	/* Pointer to first handle structure in the block */
	struct sHandle *psHandle;

	/* Block allocation cookie returned from OSAllocMem for the block of handles */
	IMG_HANDLE hBlockAlloc;

	/* Number of free handles in block */
	IMG_UINT32 ui32FreeHandBlockCount;
};

struct _PVRSRV_HANDLE_BASE_
{
	/*  Handle returned from OSAllocMem for handle base allocation */
	IMG_HANDLE hBaseBlockAlloc;

	/* Handle returned from OSAllocMem for handle array allocation */
	IMG_HANDLE hArrayBlockAlloc;

	/* Pointer to array of pointers to handle structures */
	struct sHandleIndex *psHandleArray;

	/*
	 * Pointer to handle hash table.
	 * The hash table is used to do reverse lookups, converting data
	 * pointers to handles.
	 */
	HASH_TABLE *psHashTab;

	/* Number of free handles */
	IMG_UINT32 ui32FreeHandCount;

	/*
	 * If purging is not enabled, this is the array index of first free
	 * handle.
	 * If purging is enabled, this is the index to start searching for
	 * a free handle from.  In this case it is usually zero, unless
	 * the handle array size has been increased due to lack of
	 * handles.
	 */
	IMG_UINT32 ui32FirstFreeIndex;

	/* Maximum handle index, plus one */
	IMG_UINT32 ui32MaxIndexPlusOne;

	/* Total number of handles, free and allocated */
	IMG_UINT32 ui32TotalHandCount;

	/*
	 * Index of the last free index, plus one. Not used if purging
	 * is enabled.
	 */
	IMG_UINT32 ui32LastFreeIndexPlusOne;

	/* Size of current handle batch, or zero if batching not enabled */
	IMG_UINT32 ui32HandBatchSize;

	/* Number of handles prior to start of current batch */
	IMG_UINT32 ui32TotalHandCountPreBatch;

	/* Index of first handle in batch, plus one */
	IMG_UINT32 ui32FirstBatchIndexPlusOne;

	/* Number of handle allocation failures in batch */
	IMG_UINT32 ui32BatchHandAllocFailures;

	/* Purging enabled.
	 * If purging is enabled, the size of the table can be reduced
	 * by removing free space at the end of the table.  To make
	 * purging more likely to succeed, handles are allocated as
	 * far to the front of the table as possible.  The first free
	 * handle is found by a linear search from the start of the table,
	 * and so no free handle list management is done.
	 */
	IMG_BOOL bPurgingEnabled;
};

/*
 * The key for the handle hash table is an array of three elements, the
 * pointer to the resource, the resource type, and the process ID.  The
 * eHandKey enumeration gives the array indices of the elements making
 * up the key.
 */
enum eHandKey {
	HAND_KEY_DATA = 0,
	HAND_KEY_TYPE,
	HAND_KEY_PARENT,
	HAND_KEY_LEN			/* Must be last item in list */
};

/*
 * Kernel handle base structure.  For handles that are not allocated on
 * behalf of a particular process
 */
PVRSRV_HANDLE_BASE *gpsKernelHandleBase = IMG_NULL;

/* HAND_KEY is the type of the hash table key */
typedef IMG_UINTPTR_T HAND_KEY[HAND_KEY_LEN];

/*!
******************************************************************************

 @Function	HandleListInit

 @Description	Initialise a linked list structure embedded in a handle
		structure.

 @Input		ui32Index - index of handle in the handle array
		psList - pointer to linked list structure
		hParent - parent handle, or IMG_NULL

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListInit)
#endif
static INLINE
IMG_VOID HandleListInit(IMG_UINT32 ui32Index, struct sHandleList *psList, IMG_HANDLE hParent)
{
	psList->ui32Next = ui32Index;
	psList->ui32Prev = ui32Index;
	psList->hParent = hParent;
}

/*!
******************************************************************************

 @Function	InitParentList

 @Description	Initialise the children list head in a handle structure.
		The children are the subhandles of this handle.

 @Input		psHandle - pointer to handle structure

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(InitParentList)
#endif
static INLINE
IMG_VOID InitParentList(struct sHandle *psHandle)
{
	IMG_UINT32 ui32Parent = HANDLE_PTR_TO_INDEX(psHandle);

	HandleListInit(ui32Parent, &psHandle->sChildren, INDEX_TO_HANDLE(ui32Parent));
}

/*!
******************************************************************************

 @Function	InitChildEntry

 @Description	Initialise the child list entry in a handle structure.
		The list entry is used to link together subhandles of
		a given handle.

 @Input		psHandle - pointer to handle structure

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(InitChildEntry)
#endif
static INLINE
IMG_VOID InitChildEntry(struct sHandle *psHandle)
{
	HandleListInit(HANDLE_PTR_TO_INDEX(psHandle), &psHandle->sSiblings, IMG_NULL);
}

/*!
******************************************************************************

 @Function	HandleListIsEmpty

 @Description	Determine whether a given linked list is empty.

 @Input		ui32Index - index of the handle containing the list head
		psList - pointer to the list head

 @Return	IMG_TRUE if the list is empty, IMG_FALSE if it isn't.

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListIsEmpty)
#endif
static INLINE
IMG_BOOL HandleListIsEmpty(IMG_UINT32 ui32Index, struct sHandleList *psList)
{
	IMG_BOOL bIsEmpty;

	bIsEmpty = (IMG_BOOL)(psList->ui32Next == ui32Index);

#ifdef	DEBUG
	{
		IMG_BOOL bIsEmpty2;

		bIsEmpty2 = (IMG_BOOL)(psList->ui32Prev == ui32Index);
		PVR_ASSERT(bIsEmpty == bIsEmpty2);
	}
#endif

	return bIsEmpty;
}

#ifdef DEBUG
/*!
******************************************************************************

 @Function	NoChildren

 @Description	Determine whether a handle has any subhandles

 @Input		psHandle - pointer to handle structure

 @Return	IMG_TRUE if the handle has no subhandles, IMG_FALSE if it does.

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(NoChildren)
#endif
static INLINE
IMG_BOOL NoChildren(struct sHandle *psHandle)
{
	PVR_ASSERT(psHandle->sChildren.hParent == HANDLE_PTR_TO_HANDLE(psHandle));

	return HandleListIsEmpty(HANDLE_PTR_TO_INDEX(psHandle), &psHandle->sChildren);
}

/*!
******************************************************************************

 @Function	NoParent

 @Description	Determine whether a handle is a subhandle

 @Input		psHandle - pointer to handle structure

 @Return	IMG_TRUE if the handle is not a subhandle, IMG_FALSE if it is.

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(NoParent)
#endif
static INLINE
IMG_BOOL NoParent(struct sHandle *psHandle)
{
	if (HandleListIsEmpty(HANDLE_PTR_TO_INDEX(psHandle), &psHandle->sSiblings))
	{
		PVR_ASSERT(psHandle->sSiblings.hParent == IMG_NULL);

		return IMG_TRUE;
	}
	else
	{
		PVR_ASSERT(psHandle->sSiblings.hParent != IMG_NULL);
	}
	return IMG_FALSE;
}
#endif /*DEBUG*/
/*!
******************************************************************************

 @Function	ParentHandle

 @Description	Determine the parent of a handle

 @Input		psHandle - pointer to handle structure

 @Return	Parent handle, or IMG_NULL if the handle is not a subhandle.

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(ParentHandle)
#endif
static INLINE
IMG_HANDLE ParentHandle(struct sHandle *psHandle)
{
	return psHandle->sSiblings.hParent;
}

/*
 * The LIST_PTR_FROM_INDEX_AND_OFFSET macro is used to generate either a
 * pointer to the subhandle list head, or a pointer to the linked list
 * structure of an item on a subhandle list.
 * The list head is itself on the list, but is at a different offset
 * in the handle structure to the linked list structure for items on
 * the list.  The two linked list structures are differentiated by
 * the third parameter, containing the parent index.  The parent field
 * in the list head structure references the handle structure that contains
 * it.  For items on the list, the parent field in the linked list structure
 * references the parent handle, which will be different from the handle
 * containing the linked list structure.
 */
#define	LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, i, p, po, eo) \
		((struct sHandleList *)((IMG_CHAR *)(INDEX_TO_HANDLE_STRUCT_PTR(psBase, i)) + (((i) == (p)) ? (po) : (eo))))

/*!
******************************************************************************

 @Function	HandleListInsertBefore

 @Description	Insert a handle before a handle currently on the list.

 @Input		ui32InsIndex - index of handle to be inserted after
		psIns - pointer to handle structure to be inserted after
		uiParentOffset - offset to list head struct in handle structure
		ui32EntryIndex - index of handle to be inserted
		psEntry - pointer to handle structure of item to be inserted
		uiEntryOffset - offset of list item struct in handle structure

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListInsertBefore)
#endif
static INLINE
IMG_VOID HandleListInsertBefore(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32InsIndex, struct sHandleList *psIns, IMG_SIZE_T uiParentOffset, IMG_UINT32 ui32EntryIndex, struct sHandleList *psEntry, IMG_SIZE_T uiEntryOffset, IMG_UINT32 ui32ParentIndex)
{
	/* PRQA S 3305 7 */ /*override stricter alignment warning */
	struct sHandleList *psPrevIns = LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, psIns->ui32Prev, ui32ParentIndex, uiParentOffset, uiEntryOffset);

	PVR_ASSERT(psEntry->hParent == IMG_NULL);
	PVR_ASSERT(ui32InsIndex == psPrevIns->ui32Next);
	PVR_ASSERT(LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, ui32ParentIndex, ui32ParentIndex, uiParentOffset, uiParentOffset)->hParent == INDEX_TO_HANDLE(ui32ParentIndex));

	psEntry->ui32Prev = psIns->ui32Prev;
	psIns->ui32Prev = ui32EntryIndex;
	psEntry->ui32Next = ui32InsIndex;
	psPrevIns->ui32Next = ui32EntryIndex;

	psEntry->hParent = INDEX_TO_HANDLE(ui32ParentIndex);
}

/*!
******************************************************************************

 @Function	AdoptChild

 @Description	Assign a subhandle to a handle

 @Input		psParent - pointer to handle structure of parent handle
		psChild - pointer to handle structure of child subhandle

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(AdoptChild)
#endif
static INLINE
IMG_VOID AdoptChild(PVRSRV_HANDLE_BASE *psBase, struct sHandle *psParent, struct sHandle *psChild)
{
	IMG_UINT32 ui32Parent = HANDLE_TO_INDEX(psParent->sChildren.hParent);

	PVR_ASSERT(ui32Parent == HANDLE_PTR_TO_INDEX(psParent));

	HandleListInsertBefore(psBase, ui32Parent, &psParent->sChildren, offsetof(struct sHandle, sChildren), HANDLE_PTR_TO_INDEX(psChild), &psChild->sSiblings, offsetof(struct sHandle, sSiblings), ui32Parent);

}

/*!
******************************************************************************

 @Function	HandleListRemove

 @Description	Remove a handle from a list

 @Input		ui32EntryIndex - index of handle to be removed
		psEntry - pointer to handle structure of item to be removed
		uiEntryOffset - offset of list item struct in handle structure
		uiParentOffset - offset to list head struct in handle structure

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListRemove)
#endif
static INLINE
IMG_VOID HandleListRemove(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32EntryIndex, struct sHandleList *psEntry, IMG_SIZE_T uiEntryOffset, IMG_SIZE_T uiParentOffset)
{
	if (!HandleListIsEmpty(ui32EntryIndex, psEntry))
	{
		/* PRQA S 3305 3 */ /*override stricter alignment warning */
		struct sHandleList *psPrev = LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, psEntry->ui32Prev, HANDLE_TO_INDEX(psEntry->hParent), uiParentOffset, uiEntryOffset);
		struct sHandleList *psNext = LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, psEntry->ui32Next, HANDLE_TO_INDEX(psEntry->hParent), uiParentOffset, uiEntryOffset);

		/*
		 * The list head is on the list, and we don't want to
		 * remove it.
		 */
		PVR_ASSERT(psEntry->hParent != IMG_NULL);

		psPrev->ui32Next = psEntry->ui32Next;
		psNext->ui32Prev = psEntry->ui32Prev;

		HandleListInit(ui32EntryIndex, psEntry, IMG_NULL);
	}
}

/*!
******************************************************************************

 @Function	UnlinkFromParent

 @Description	Remove a subhandle from its parents list

 @Input		psHandle - pointer to handle structure of child subhandle

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(UnlinkFromParent)
#endif
static INLINE
IMG_VOID UnlinkFromParent(PVRSRV_HANDLE_BASE *psBase, struct sHandle *psHandle)
{
	HandleListRemove(psBase, HANDLE_PTR_TO_INDEX(psHandle), &psHandle->sSiblings, offsetof(struct sHandle, sSiblings), offsetof(struct sHandle, sChildren));
}

/*!
******************************************************************************

 @Function	HandleListIterate

 @Description	Iterate over the items in a list

 @Input		psHead - pointer to list head
		uiParentOffset - offset to list head struct in handle structure
		uiEntryOffset - offset of list item struct in handle structure
		pfnIterFunc - function to be called for each handle in the list

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListIterate)
#endif
static INLINE
PVRSRV_ERROR HandleListIterate(PVRSRV_HANDLE_BASE *psBase, struct sHandleList *psHead, IMG_SIZE_T uiParentOffset, IMG_SIZE_T uiEntryOffset, PVRSRV_ERROR (*pfnIterFunc)(PVRSRV_HANDLE_BASE *, struct sHandle *))
{
	IMG_UINT32 ui32Index;
	IMG_UINT32 ui32Parent = HANDLE_TO_INDEX(psHead->hParent);

	PVR_ASSERT(psHead->hParent != IMG_NULL);

	/*
 	 * Follow the next chain from the list head until we reach
 	 * the list head again, which signifies the end of the list.
 	 */
	for(ui32Index = psHead->ui32Next; ui32Index != ui32Parent; )
	{
		struct sHandle *psHandle = INDEX_TO_HANDLE_STRUCT_PTR(psBase, ui32Index);
		/* PRQA S 3305 2 */ /*override stricter alignment warning */
		struct sHandleList *psEntry = LIST_PTR_FROM_INDEX_AND_OFFSET(psBase, ui32Index, ui32Parent, uiParentOffset, uiEntryOffset);
		PVRSRV_ERROR eError;

		PVR_ASSERT(psEntry->hParent == psHead->hParent);
		/*
		 * Get the next index now, in case the list item is
		 * modified by the iteration function.
		 */
		ui32Index = psEntry->ui32Next;

		eError = (*pfnIterFunc)(psBase, psHandle);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	IterateOverChildren

 @Description	Iterate over the subhandles of a parent handle

 @Input		psParent - pointer to parent handle structure
		pfnIterFunc - function to be called for each subhandle

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(IterateOverChildren)
#endif
static INLINE
PVRSRV_ERROR IterateOverChildren(PVRSRV_HANDLE_BASE *psBase, struct sHandle *psParent, PVRSRV_ERROR (*pfnIterFunc)(PVRSRV_HANDLE_BASE *, struct sHandle *))
{
	 return HandleListIterate(psBase, &psParent->sChildren, offsetof(struct sHandle, sChildren), offsetof(struct sHandle, sSiblings), pfnIterFunc);
}

/*!
******************************************************************************

 @Function	GetHandleStructure

 @Description	Get the handle structure for a given handle

 @Input		psBase - pointer to handle base structure
		ppsHandle - location to return pointer to handle structure
		hHandle - handle from client
		eType - handle type or PVRSRV_HANDLE_TYPE_NONE if the
			handle type is not to be checked.

 @Output	ppsHandle - points to a pointer to the handle structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(GetHandleStructure)
#endif
static INLINE
PVRSRV_ERROR GetHandleStructure(PVRSRV_HANDLE_BASE *psBase, struct sHandle **ppsHandle, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType)
{
	IMG_UINT32 ui32Index = HANDLE_TO_INDEX(hHandle);
	struct sHandle *psHandle;

	/* Check handle index is in range */
	if (!INDEX_IS_VALID(psBase, ui32Index))
	{
		PVR_DPF((PVR_DBG_ERROR, "GetHandleStructure: Handle index out of range (%u >= %u)", ui32Index, psBase->ui32TotalHandCount));
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}

	psHandle =  INDEX_TO_HANDLE_STRUCT_PTR(psBase, ui32Index);
	if (psHandle->eType == PVRSRV_HANDLE_TYPE_NONE)
	{
		PVR_DPF((PVR_DBG_ERROR, "GetHandleStructure: Handle not allocated (index: %u)", ui32Index));
		return PVRSRV_ERROR_HANDLE_NOT_ALLOCATED;
	}

	/*
	 * Unless PVRSRV_HANDLE_TYPE_NONE was passed in to this function,
	 * check handle is of the correct type.
	 */
	if (eType != PVRSRV_HANDLE_TYPE_NONE && eType != psHandle->eType)
	{
		PVR_DPF((PVR_DBG_ERROR, "GetHandleStructure: Handle type mismatch (%d != %d)", eType, psHandle->eType));
		return PVRSRV_ERROR_HANDLE_TYPE_MISMATCH;
	}

	/* Return the handle structure */
	*ppsHandle = psHandle;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	ParentIfPrivate

 @Description	Return the parent handle if the handle was allocated
		with PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE, else return
		IMG_NULL

 @Input		psHandle - pointer to handle

 @Return	Parent handle, or IMG_NULL

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(ParentIfPrivate)
#endif
static INLINE
IMG_HANDLE ParentIfPrivate(struct sHandle *psHandle)
{
	return TEST_ALLOC_FLAG(psHandle, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ?
			ParentHandle(psHandle) : IMG_NULL;
}

/*!
******************************************************************************

 @Function	InitKey

 @Description	Initialise a hash table key for the current process

 @Input		psBase - pointer to handle base structure
		aKey - pointer to key
		pvData - pointer to the resource the handle represents
		eType - type of resource

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(InitKey)
#endif
static INLINE
IMG_VOID InitKey(HAND_KEY aKey, PVRSRV_HANDLE_BASE *psBase, IMG_VOID *pvData, PVRSRV_HANDLE_TYPE eType, IMG_HANDLE hParent)
{
	PVR_UNREFERENCED_PARAMETER(psBase);

	aKey[HAND_KEY_DATA] = (IMG_UINTPTR_T)pvData;
	aKey[HAND_KEY_TYPE] = (IMG_UINTPTR_T)eType;
	aKey[HAND_KEY_PARENT] = (IMG_UINTPTR_T)hParent;
}

/*!
******************************************************************************

 @Function	ReallocHandleArray

 @Description	Reallocate the handle array

 @Input		psBase - handle base.
		phBlockAlloc - pointer to block allocation handle.
		ui32NewCount - new handle count
		ui32OldCount - old handle count

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static
PVRSRV_ERROR ReallocHandleArray(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32NewCount)
{
	struct sHandleIndex *psOldArray = psBase->psHandleArray;
	IMG_HANDLE hOldArrayBlockAlloc = psBase->hArrayBlockAlloc;
	IMG_UINT32 ui32OldCount = psBase->ui32TotalHandCount;
	struct sHandleIndex *psNewArray = IMG_NULL;
	IMG_HANDLE hNewArrayBlockAlloc = IMG_NULL;
	PVRSRV_ERROR eError;
	PVRSRV_ERROR eReturn = PVRSRV_OK;
	IMG_UINT32 ui32Index;

	if (ui32NewCount == ui32OldCount)
	{
		return PVRSRV_OK;
	}

	if (ui32NewCount != 0 && !psBase->bPurgingEnabled &&
		 ui32NewCount < ui32OldCount)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (((ui32OldCount % HANDLE_BLOCK_SIZE) != 0) ||
		((ui32NewCount % HANDLE_BLOCK_SIZE) != 0))
	{
		PVR_ASSERT((ui32OldCount % HANDLE_BLOCK_SIZE) == 0);
		PVR_ASSERT((ui32NewCount % HANDLE_BLOCK_SIZE) == 0);

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if (ui32NewCount != 0)
	{
		/* Allocate new handle array */
		eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
			HANDLE_ARRAY_SIZE(ui32NewCount) * sizeof(struct sHandleIndex),
			(IMG_VOID **)&psNewArray,
			&hNewArrayBlockAlloc,
			"Memory Area");
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Couldn't allocate new handle array (%d)", eError));
			eReturn = eError;
			goto error;
		}

		if (ui32OldCount != 0)
		{
			OSMemCopy(psNewArray, psOldArray, HANDLE_ARRAY_SIZE(MIN(ui32NewCount, ui32OldCount)) * sizeof(struct sHandleIndex));
		}
	}

	/*
	 * If the new handle array is smaller than the old one, free
	 * unused handle structures
	 */
	for(ui32Index = ui32NewCount; ui32Index < ui32OldCount; ui32Index += HANDLE_BLOCK_SIZE)
	{
		struct sHandleIndex *psIndex = INDEX_TO_INDEX_STRUCT_PTR(psOldArray, ui32Index);

		eError = OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				sizeof(struct sHandle) * HANDLE_BLOCK_SIZE,
				psIndex->psHandle,
				psIndex->hBlockAlloc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Couldn't free handle structures (%d)", eError));
		}
	}

	/*
	 * If the new handle array is bigger than the old one, allocate
	 * new handle structures
	 */
	for(ui32Index = ui32OldCount; ui32Index < ui32NewCount; ui32Index += HANDLE_BLOCK_SIZE)
	{
		/* PRQA S 0505 1 */ /* psNewArray is never NULL, see assert earlier */
		struct sHandleIndex *psIndex = INDEX_TO_INDEX_STRUCT_PTR(psNewArray, ui32Index);

		eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				sizeof(struct sHandle) * HANDLE_BLOCK_SIZE,
				(IMG_VOID **)&psIndex->psHandle,
				&psIndex->hBlockAlloc,
				"Memory Area");
		if (eError != PVRSRV_OK)
		{
			psIndex->psHandle = IMG_NULL;
			PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Couldn't allocate handle structures (%d)", eError));
			eReturn = eError;
		}
		else
		{
			IMG_UINT32 ui32SubIndex;

			psIndex->ui32FreeHandBlockCount = HANDLE_BLOCK_SIZE;

			for(ui32SubIndex = 0; ui32SubIndex < HANDLE_BLOCK_SIZE; ui32SubIndex++)
			{
				struct sHandle *psHandle = psIndex->psHandle + ui32SubIndex;


				psHandle->ui32Index = ui32SubIndex + ui32Index;
				psHandle->eType = PVRSRV_HANDLE_TYPE_NONE;
				psHandle->eInternalFlag = INTERNAL_HANDLE_FLAG_NONE;
				psHandle->ui32NextIndexPlusOne  = 0;
			}
		}
	}
	if (eReturn != PVRSRV_OK)
	{
		goto error;
	}

#ifdef	DEBUG_MAX_HANDLE_COUNT
	/* Force handle failure to test error exit code */
	if (ui32NewCount > DEBUG_MAX_HANDLE_COUNT)
	{
		PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Max handle count (%u) reached", DEBUG_MAX_HANDLE_COUNT));
		eReturn = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto error;
	}
#endif

	if (psOldArray != IMG_NULL)
	{
		/* Free old handle array */
		eError = OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			HANDLE_ARRAY_SIZE(ui32OldCount) * sizeof(struct sHandleIndex),
			psOldArray,
			hOldArrayBlockAlloc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Couldn't free old handle array (%d)", eError));
		}
	}

	psBase->psHandleArray = psNewArray;
	psBase->hArrayBlockAlloc = hNewArrayBlockAlloc;
	psBase->ui32TotalHandCount = ui32NewCount;

	if (ui32NewCount > ui32OldCount)
	{
		/* Check for wraparound */
		PVR_ASSERT(psBase->ui32FreeHandCount + (ui32NewCount - ui32OldCount) > psBase->ui32FreeHandCount);

		/* PRQA S 3382 1 */ /* ui32NewCount always > ui32OldCount */
		psBase->ui32FreeHandCount += (ui32NewCount - ui32OldCount);

		/*
		 * If purging is enabled, there is no free handle list
		 * management, but as an optimization, when allocating
		 * new handles, we use ui32FirstFreeIndex to point to
		 * the first handle in a newly allocated block.
		 */
		if (psBase->ui32FirstFreeIndex == 0)
		{
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == 0);

			psBase->ui32FirstFreeIndex = ui32OldCount;
		}
		else
		{
			if (!psBase->bPurgingEnabled)
			{
				PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne != 0);
				PVR_ASSERT(INDEX_TO_HANDLE_STRUCT_PTR(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne == 0);

				INDEX_TO_HANDLE_STRUCT_PTR(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne = ui32OldCount + 1;
			}
		}

		if (!psBase->bPurgingEnabled)
		{
			psBase->ui32LastFreeIndexPlusOne = ui32NewCount;
		}
	}
	else
	{
		PVR_ASSERT(ui32NewCount == 0 || psBase->bPurgingEnabled);
		PVR_ASSERT(ui32NewCount == 0 || psBase->ui32FirstFreeIndex <= ui32NewCount);
		PVR_ASSERT(psBase->ui32FreeHandCount - (ui32OldCount - ui32NewCount) < psBase->ui32FreeHandCount);

		/* PRQA S 3382 1 */ /* ui32OldCount always >= ui32NewCount */
		psBase->ui32FreeHandCount -= (ui32OldCount - ui32NewCount);

		if (ui32NewCount == 0)
		{
			psBase->ui32FirstFreeIndex = 0;
			psBase->ui32LastFreeIndexPlusOne = 0;
		}
	}

	PVR_ASSERT(psBase->ui32FirstFreeIndex <= psBase->ui32TotalHandCount);

	return PVRSRV_OK;

error:
	PVR_ASSERT(eReturn != PVRSRV_OK);

	if (psNewArray != IMG_NULL)
	{
		/* Free any new handle structures that were allocated */
		for(ui32Index = ui32OldCount; ui32Index < ui32NewCount; ui32Index += HANDLE_BLOCK_SIZE)
		{
			struct sHandleIndex *psIndex = INDEX_TO_INDEX_STRUCT_PTR(psNewArray, ui32Index);
			if (psIndex->psHandle != IMG_NULL)
			{
				eError = OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
						sizeof(struct sHandle) * HANDLE_BLOCK_SIZE,
						psIndex->psHandle,
						psIndex->hBlockAlloc);
				if (eError != PVRSRV_OK)
				{
					PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Couldn't free handle structures (%d)", eError));
				}
			}
		}

		/* Free new handle array */
		eError = OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			HANDLE_ARRAY_SIZE(ui32NewCount) * sizeof(struct sHandleIndex),
			psNewArray,
			hNewArrayBlockAlloc);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "ReallocHandleArray: Couldn't free new handle array (%d)", eError));
		}
	}

	return eReturn;
}

/*!
******************************************************************************

 @Function	FreeHandleArray

 @Description	Frees the handle array.
		The memory containing the array of handle structure
		pointers is deallocated.

 @Input		psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR FreeHandleArray(PVRSRV_HANDLE_BASE *psBase)
{
	return ReallocHandleArray(psBase, 0);
}

/*!
******************************************************************************

 @Function	FreeHandle

 @Description	Free a handle structure.

 @Input		psBase - pointer to handle base structure
		psHandle - pointer to handle structure

 @Return	PVRSRV_OK or PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR FreeHandle(PVRSRV_HANDLE_BASE *psBase, struct sHandle *psHandle)
{
	HAND_KEY aKey;
	IMG_UINT32 ui32Index = HANDLE_PTR_TO_INDEX(psHandle);
	PVRSRV_ERROR eError;

	/*
	 * If a handle allocated in batch mode is freed whilst still
	 * in batch mode, the type is set to PVRSRV_HANDLE_TYPE_NONE further
	 * down, to indicate the handle will not be used, but not actually
	 * freed.  The Free is completed when this function is called a
	 * second time as part of the batch commit or release.
	 */

	InitKey(aKey, psBase, psHandle->pvData, psHandle->eType, ParentIfPrivate(psHandle));

	if (!TEST_ALLOC_FLAG(psHandle, PVRSRV_HANDLE_ALLOC_FLAG_MULTI) && !BATCHED_HANDLE_PARTIALLY_FREE(psHandle))
	{
		IMG_HANDLE hHandle;
		hHandle = (IMG_HANDLE) HASH_Remove_Extended(psBase->psHashTab, aKey);


		PVR_ASSERT(hHandle != IMG_NULL);
		PVR_ASSERT(hHandle == INDEX_TO_HANDLE(ui32Index));
		PVR_UNREFERENCED_PARAMETER(hHandle);
	}

	/* Unlink handle from parent */
	UnlinkFromParent(psBase, psHandle);

	/* Free children */
	eError = IterateOverChildren(psBase, psHandle, FreeHandle);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandle: Error whilst freeing subhandles (%d)", eError));
		return eError;
	}

	/*
	 * Clear the type here, so that a handle can no longer be looked
	 * up if it is only partially freed.
	 */
	psHandle->eType = PVRSRV_HANDLE_TYPE_NONE;

	if (BATCHED_HANDLE(psHandle) && !BATCHED_HANDLE_PARTIALLY_FREE(psHandle))
	{
		/* PRQA S 1474,4130 1 */ /* ignore warnings about enum types being modified */
        SET_BATCHED_HANDLE_PARTIALLY_FREE(psHandle);
		/*
		 * If the handle was allocated in batch mode, delay the free
		 * until the batch commit or release.
		 */
		return PVRSRV_OK;
	}

	/* No free list management if purging is enabled */
	if (!psBase->bPurgingEnabled)
	{
		if (psBase->ui32FreeHandCount == 0)
		{
			PVR_ASSERT(psBase->ui32FirstFreeIndex == 0);
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == 0);

			psBase->ui32FirstFreeIndex =  ui32Index;
		}
		else
		{
			/*
			 * Put the handle pointer on the end of the the free
			 * handle pointer linked list.
			 */
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne != 0);
			PVR_ASSERT(INDEX_TO_HANDLE_STRUCT_PTR(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne == 0);
			INDEX_TO_HANDLE_STRUCT_PTR(psBase, psBase->ui32LastFreeIndexPlusOne - 1)->ui32NextIndexPlusOne =  ui32Index + 1;
		}

		PVR_ASSERT(psHandle->ui32NextIndexPlusOne == 0);

		/* Update the end of the free handle linked list */
		psBase->ui32LastFreeIndexPlusOne = ui32Index + 1;
	}

	psBase->ui32FreeHandCount++;
	INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, ui32Index)++;

	PVR_ASSERT(INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, ui32Index)<= HANDLE_BLOCK_SIZE);

#ifdef DEBUG
	{
		IMG_UINT32 ui32BlockedIndex;
		IMG_UINT32 ui32FreeHandCount = 0;

		for (ui32BlockedIndex = 0; ui32BlockedIndex < psBase->ui32TotalHandCount; ui32BlockedIndex += HANDLE_BLOCK_SIZE)
		{
			ui32FreeHandCount += INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, ui32BlockedIndex);
		}

		PVR_ASSERT(ui32FreeHandCount == psBase->ui32FreeHandCount);
	}
#endif

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	FreeAllHandles

 @Description	Free all handles for a given handle base

 @Input		psBase - pointer to handle base structure

 @Return	PVRSRV_OK or PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR FreeAllHandles(PVRSRV_HANDLE_BASE *psBase)
{
	IMG_UINT32 i;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psBase->ui32FreeHandCount == psBase->ui32TotalHandCount)
	{
		return eError;
	}

	for (i = 0; i < psBase->ui32TotalHandCount; i++)
	{
		struct sHandle *psHandle;

		psHandle = INDEX_TO_HANDLE_STRUCT_PTR(psBase, i);

		if (psHandle->eType != PVRSRV_HANDLE_TYPE_NONE)
		{
			eError = FreeHandle(psBase, psHandle);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "FreeAllHandles: FreeHandle failed (%d)", eError));
				break;
			}

			/* Break out of loop if all the handles free */
			if (psBase->ui32FreeHandCount == psBase->ui32TotalHandCount)
			{
				break;
			}
		}
	}

	return eError;
}

/*!
******************************************************************************

 @Function	FreeHandleBase

 @Description	Free a handle base.

 @Input		psHandleBase - pointer to handle base

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR FreeHandleBase(PVRSRV_HANDLE_BASE *psBase)
{
	PVRSRV_ERROR eError;

	if (HANDLES_BATCHED(psBase))
	{
		PVR_DPF((PVR_DBG_WARNING, "FreeHandleBase: Uncommitted/Unreleased handle batch"));
		PVRSRVReleaseHandleBatch(psBase);
	}

	/* Free the handle array */
	eError = FreeAllHandles(psBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleBase: Couldn't free handles (%d)", eError));
		return eError;
	}

	/* Free the handle array */
	eError = FreeHandleArray(psBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleBase: Couldn't free handle array (%d)", eError));
		return eError;
	}

	if (psBase->psHashTab != IMG_NULL)
	{
		/* Free the hash table */
		HASH_Delete(psBase->psHashTab);
	}

	eError = OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
		sizeof(*psBase),
		psBase,
		psBase->hBaseBlockAlloc);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleBase: Couldn't free handle base (%d)", eError));
		return eError;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	FindHandle

 @Description	Find handle corresponding to a resource pointer

 @Input		psBase - pointer to handle base structure
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource

 @Return	the handle, or IMG_NULL if not found

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(FindHandle)
#endif
static INLINE
IMG_HANDLE FindHandle(PVRSRV_HANDLE_BASE *psBase, IMG_VOID *pvData, PVRSRV_HANDLE_TYPE eType, IMG_HANDLE hParent)
{
	HAND_KEY aKey;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	InitKey(aKey, psBase, pvData, eType, hParent);

	return (IMG_HANDLE) HASH_Retrieve_Extended(psBase->psHashTab, aKey);
}

/*!
******************************************************************************

 @Function	IncreaseHandleArraySize

 @Description	Allocate some more free handles

 @Input		psBase - pointer to handle base structure
		ui32Delta - number of new handles required

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR IncreaseHandleArraySize(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32Delta)
{
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32DeltaAdjusted = ROUND_UP_TO_MULTIPLE_OF_BLOCK_SIZE(ui32Delta);
	IMG_UINT32 ui32NewTotalHandCount = psBase->ui32TotalHandCount + ui32DeltaAdjusted;

	PVR_ASSERT(ui32Delta != 0);

	/*
	 * Check new count against max handle index, and check for wrap around.
	 */
	if (ui32NewTotalHandCount > psBase->ui32MaxIndexPlusOne || ui32NewTotalHandCount <= psBase->ui32TotalHandCount)
	{
		ui32NewTotalHandCount = psBase->ui32MaxIndexPlusOne;

		ui32DeltaAdjusted = ui32NewTotalHandCount - psBase->ui32TotalHandCount;

		if (ui32DeltaAdjusted < ui32Delta)
		{
			PVR_DPF((PVR_DBG_ERROR, "IncreaseHandleArraySize: Maximum handle limit reached (%d)", psBase->ui32MaxIndexPlusOne));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}
	}

	PVR_ASSERT(ui32DeltaAdjusted >= ui32Delta);

	/* Realloc handle pointer array */
	eError = ReallocHandleArray(psBase, ui32NewTotalHandCount);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "IncreaseHandleArraySize: ReallocHandleArray failed (%d)", eError));
		return eError;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	EnsureFreeHandles

 @Description	Ensure there are enough free handles

 @Input		psBase - pointer to handle base structure
		ui32Free - number of free handles required

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR EnsureFreeHandles(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32Free)
{
	PVRSRV_ERROR eError;

	if (ui32Free > psBase->ui32FreeHandCount)
	{
		IMG_UINT32 ui32FreeHandDelta = ui32Free - psBase->ui32FreeHandCount;
		eError = IncreaseHandleArraySize(psBase, ui32FreeHandDelta);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "EnsureFreeHandles: Couldn't allocate %u handles to ensure %u free handles (IncreaseHandleArraySize failed with error %d)", ui32FreeHandDelta, ui32Free, eError));

			return eError;
		}
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	AllocHandle

 @Description	Allocate a new handle

 @Input		phHandle - location for new handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource
		hParent - parent handle or IMG_NULL

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR AllocHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, IMG_VOID *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag, IMG_HANDLE hParent)
{
	IMG_UINT32 ui32NewIndex = DEFAULT_MAX_INDEX_PLUS_ONE;
	struct sHandle *psNewHandle = IMG_NULL;
	IMG_HANDLE hHandle;
	HAND_KEY aKey;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(psBase != IMG_NULL);
	PVR_ASSERT(psBase->psHashTab != IMG_NULL);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* Handle must not already exist */
		PVR_ASSERT(FindHandle(psBase, pvData, eType, hParent) == IMG_NULL);
	}

	if (psBase->ui32FreeHandCount == 0 && HANDLES_BATCHED(psBase))
	{
		 PVR_DPF((PVR_DBG_WARNING, "AllocHandle: Handle batch size (%u) was too small, allocating additional space", psBase->ui32HandBatchSize)); 
	}

	/* Ensure there is a free handle */
	eError = EnsureFreeHandles(psBase, 1);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "AllocHandle: EnsureFreeHandles failed (%d)", eError));
		return eError;
	}
	PVR_ASSERT(psBase->ui32FreeHandCount != 0);

	if (!psBase->bPurgingEnabled)
	{
		/* Array index of first free handle */
		ui32NewIndex = psBase->ui32FirstFreeIndex;

		/* Get handle array entry */
		psNewHandle = INDEX_TO_HANDLE_STRUCT_PTR(psBase, ui32NewIndex);
	}
	else
	{
		IMG_UINT32 ui32BlockedIndex;

		/*
		 * If purging is enabled, we always try to allocate handles
		 * at the front of the array, to increase the chances that
		 * the size of the handle array can be reduced by a purge.
		 * No linked list of free handles is kept; we search for
		 * free handles as required.
		 */

		/*
		 * ui32FirstFreeIndex should only be set when a new batch of
		 * handle structures is allocated, and should always be a
		 * multiple of the block size.
		 */
		PVR_ASSERT((psBase->ui32FirstFreeIndex % HANDLE_BLOCK_SIZE) == 0);

		for (ui32BlockedIndex = ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(psBase->ui32FirstFreeIndex); ui32BlockedIndex < psBase->ui32TotalHandCount; ui32BlockedIndex += HANDLE_BLOCK_SIZE)
		{
			struct sHandleIndex *psIndex = BASE_AND_INDEX_TO_INDEX_STRUCT_PTR(psBase, ui32BlockedIndex);

			if (psIndex->ui32FreeHandBlockCount == 0)
			{
				continue;
			}

			for (ui32NewIndex = ui32BlockedIndex; ui32NewIndex < ui32BlockedIndex + HANDLE_BLOCK_SIZE; ui32NewIndex++)
			{
				psNewHandle = INDEX_TO_HANDLE_STRUCT_PTR(psBase, ui32NewIndex);
				if (HANDLE_STRUCT_IS_FREE(psNewHandle))
				{
					break;
				}
			}
		}
		psBase->ui32FirstFreeIndex = 0;
		PVR_ASSERT(ui32NewIndex < psBase->ui32TotalHandCount);
	}
	PVR_ASSERT(psNewHandle != IMG_NULL);

	/* Handle to be returned to client */
	hHandle = INDEX_TO_HANDLE(ui32NewIndex);

	/*
	 * If a data pointer can be associated with multiple handles, we
	 * don't put the handle in the hash table, as the data pointer
	 * may not map to a unique handle
	 */
	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* Initialise hash key */
		InitKey(aKey, psBase, pvData, eType, hParent);

		/* Put the new handle in the hash table */
		if (!HASH_Insert_Extended(psBase->psHashTab, aKey, (IMG_UINTPTR_T)hHandle))
		{
			PVR_DPF((PVR_DBG_ERROR, "AllocHandle: Couldn't add handle to hash table"));

			return PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;
		}
	}

	psBase->ui32FreeHandCount--;

	PVR_ASSERT(INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, ui32NewIndex) <= HANDLE_BLOCK_SIZE);
	PVR_ASSERT(INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, ui32NewIndex) > 0);

	INDEX_TO_FREE_HAND_BLOCK_COUNT(psBase, ui32NewIndex)--;

	/* No free list management if purging is enabled */
	if (!psBase->bPurgingEnabled)
	{
		/* Check whether the last free handle has been allocated */
		if (psBase->ui32FreeHandCount == 0)
		{
			PVR_ASSERT(psBase->ui32FirstFreeIndex == ui32NewIndex);
			PVR_ASSERT(psBase->ui32LastFreeIndexPlusOne == (ui32NewIndex + 1));

			psBase->ui32LastFreeIndexPlusOne = 0;
			psBase->ui32FirstFreeIndex = 0;
		}
		else
		{
			/*
			 * Update the first free handle index.
			 * If the "next free index plus one" field in the new
			 * handle structure is zero, the next free index is
			 * the index of the new handle plus one.  This
			 * convention has been adopted to simplify the
			 * initialisation of freshly allocated handle
			 * space.
			 */
			psBase->ui32FirstFreeIndex = (psNewHandle->ui32NextIndexPlusOne == 0) ?
				ui32NewIndex + 1 :
				psNewHandle->ui32NextIndexPlusOne - 1;
		}
	}

	/* Initialise the newly allocated handle */
	PVR_ASSERT(psNewHandle->ui32Index == ui32NewIndex);

	/* PRQA S 0505 1 */ /* psNewHandle is never NULL, see assert earlier */
	psNewHandle->eType = eType;
	psNewHandle->pvData = pvData;
	psNewHandle->eInternalFlag = INTERNAL_HANDLE_FLAG_NONE;
	psNewHandle->eFlag = eFlag;

	InitParentList(psNewHandle);
#if defined(DEBUG)
	PVR_ASSERT(NoChildren(psNewHandle));
#endif

	InitChildEntry(psNewHandle);
#if defined(DEBUG)
	PVR_ASSERT(NoParent(psNewHandle));
#endif

	if (HANDLES_BATCHED(psBase))
	{
		/* Add handle to batch list */
		psNewHandle->ui32NextIndexPlusOne = psBase->ui32FirstBatchIndexPlusOne;

		psBase->ui32FirstBatchIndexPlusOne = ui32NewIndex + 1;

		/* PRQA S 1474 1 */ /* ignore warnings about enum types being modified */
		SET_BATCHED_HANDLE(psNewHandle);
	}
	else
	{
		psNewHandle->ui32NextIndexPlusOne = 0;
	}

	/* Return the new handle to the client */
	*phHandle = hHandle;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVAllocHandle

 @Description	Allocate a handle

 @Input		phHandle - location for new handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, IMG_VOID *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag)
{
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	*phHandle = IMG_NULL;

	if (HANDLES_BATCHED(psBase))
	{
		/*
		 * Increment the counter in case of failure.  It will be
		 * decremented on success.
		 */
		psBase->ui32BatchHandAllocFailures++;
	}

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* See if there is already a handle for this data pointer */
		hHandle = FindHandle(psBase, pvData, eType, IMG_NULL);
		if (hHandle != IMG_NULL)
		{
			struct sHandle *psHandle;

			eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandle: Lookup of existing handle failed"));
				return eError;
			}

			/*
			 * If the client is willing to share a handle, and the
			 * existing handle is marked as shareable, return the
			 * existing handle.
			 */
			if (TEST_FLAG(psHandle->eFlag & eFlag, PVRSRV_HANDLE_ALLOC_FLAG_SHARED))
			{
				*phHandle = hHandle;
				eError = PVRSRV_OK;
				goto exit_ok;
			}
			
			return PVRSRV_ERROR_HANDLE_NOT_SHAREABLE;
		}
	}

	eError = AllocHandle(psBase, phHandle, pvData, eType, eFlag, IMG_NULL);
	
exit_ok:
	if (HANDLES_BATCHED(psBase) && (eError == PVRSRV_OK))
	{
		psBase->ui32BatchHandAllocFailures--;
	}

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVAllocSubHandle

 @Description	Allocate a subhandle

 @Input		phHandle - location for new subhandle
		pvData - pointer to resource to be associated with the subhandle
		eType - the type of resource
		hParent - parent handle

 @Output	phHandle - points to new subhandle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocSubHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, IMG_VOID *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag, IMG_HANDLE hParent)
{
	struct sHandle *psPHand;
	struct sHandle *psCHand;
	PVRSRV_ERROR eError;
	IMG_HANDLE hParentKey;
	IMG_HANDLE hHandle;

	*phHandle = IMG_NULL;

	if (HANDLES_BATCHED(psBase))
	{
		/*
		 * Increment the counter in case of failure.  It will be
		 * decremented on success.
		 */
		psBase->ui32BatchHandAllocFailures++;
	}

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	hParentKey = TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ?
			hParent : IMG_NULL;

	/* Lookup the parent handle */
	eError = GetHandleStructure(psBase, &psPHand, hParent, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* See if there is already a handle for this data pointer */
		hHandle = FindHandle(psBase, pvData, eType, hParentKey);
		if (hHandle != IMG_NULL)
		{
			struct sHandle *psCHandle;
			PVRSRV_ERROR eErr;

			eErr = GetHandleStructure(psBase, &psCHandle, hHandle, eType);
			if (eErr != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Lookup of existing handle failed"));
				return eErr;
			}

			PVR_ASSERT(hParentKey != IMG_NULL && ParentHandle(HANDLE_TO_HANDLE_STRUCT_PTR(psBase, hHandle)) == hParent);

			/*
			 * If the client is willing to share a handle, the
			 * existing handle is marked as shareable, and the
			 * existing handle has the same parent, return the
			 * existing handle.
			 */
			if (TEST_FLAG(psCHandle->eFlag & eFlag, PVRSRV_HANDLE_ALLOC_FLAG_SHARED) && ParentHandle(HANDLE_TO_HANDLE_STRUCT_PTR(psBase, hHandle)) == hParent)
			{
				*phHandle = hHandle;
				goto exit_ok;
			}
			return PVRSRV_ERROR_HANDLE_NOT_SHAREABLE;
		}
	}

	eError = AllocHandle(psBase, &hHandle, pvData, eType, eFlag, hParentKey);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/*
	 * Get the parent handle structure again, in case the handle
	 * structure has moved (depending on the implementation
	 * of AllocHandle).
	 */
	psPHand = HANDLE_TO_HANDLE_STRUCT_PTR(psBase, hParent);

	psCHand = HANDLE_TO_HANDLE_STRUCT_PTR(psBase, hHandle);

	AdoptChild(psBase, psPHand, psCHand);

	*phHandle = hHandle;

exit_ok:
	if (HANDLES_BATCHED(psBase))
	{
		psBase->ui32BatchHandAllocFailures--;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVFindHandle

 @Description	Find handle corresponding to a resource pointer

 @Input		phHandle - location for returned handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource

 @Output	phHandle - points to handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVFindHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, IMG_VOID *pvData, PVRSRV_HANDLE_TYPE eType)
{
	IMG_HANDLE hHandle;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	/* See if there is a handle for this data pointer */
	hHandle = (IMG_HANDLE) FindHandle(psBase, pvData, eType, IMG_NULL);
	if (hHandle == IMG_NULL)
	{
		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	}

	*phHandle = hHandle;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVLookupHandleAnyType

 @Description	Lookup the data pointer and type corresponding to a handle

 @Input		ppvData - location to return data pointer
		peType - location to return handle type
		hHandle - handle from client

 @Output	ppvData - points to the data pointer
		peType - points to handle type

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVLookupHandleAnyType(PVRSRV_HANDLE_BASE *psBase, IMG_PVOID *ppvData, PVRSRV_HANDLE_TYPE *peType, IMG_HANDLE hHandle)
{
	struct sHandle *psHandle;
	PVRSRV_ERROR eError;

	eError = GetHandleStructure(psBase, &psHandle, hHandle, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupHandleAnyType: Error looking up handle (%d)", eError));
		return eError;
	}

	*ppvData = psHandle->pvData;
	*peType = psHandle->eType;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVLookupHandle

 @Description	Lookup the data pointer corresponding to a handle

 @Input		ppvData - location to return data pointer
		hHandle - handle from client
		eType - handle type

 @Output	ppvData - points to the data pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE *psBase, IMG_PVOID *ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupHandle: Error looking up handle (%d)", eError));
		return eError;
	}

	*ppvData = psHandle->pvData;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVLookupSubHandle

 @Description	Lookup the data pointer corresponding to a subhandle

 @Input		ppvData - location to return data pointer
		hHandle - handle from client
		eType - handle type
		hAncestor - ancestor handle

 @Output	ppvData - points to the data pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVLookupSubHandle(PVRSRV_HANDLE_BASE *psBase, IMG_PVOID *ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType, IMG_HANDLE hAncestor)
{
	struct sHandle *psPHand;
	struct sHandle *psCHand;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psCHand, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupSubHandle: Error looking up subhandle (%d)", eError));
		return eError;
	}

	/* Look for hAncestor among the handle's ancestors */
	for (psPHand = psCHand; ParentHandle(psPHand) != hAncestor; )
	{
		eError = GetHandleStructure(psBase, &psPHand, ParentHandle(psPHand), PVRSRV_HANDLE_TYPE_NONE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupSubHandle: Subhandle doesn't belong to given ancestor"));
			return PVRSRV_ERROR_INVALID_SUBHANDLE;
		}
	}

	*ppvData = psCHand->pvData;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVGetParentHandle

 @Description	Lookup the parent of a handle

 @Input		phParent - location for returning parent handle
		hHandle - handle for which the parent handle is required
		eType - handle type
		hParent - parent handle

 @Output	*phParent - parent handle, or IMG_NULL if there is no parent

 @Return	Error code or PVRSRV_OK.  Note that not having a parent is
		not regarded as an error.

******************************************************************************/
PVRSRV_ERROR PVRSRVGetParentHandle(PVRSRV_HANDLE_BASE *psBase, IMG_PVOID *phParent, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetParentHandle: Error looking up subhandle (%d)", eError));
		return eError;
	}

	*phParent = ParentHandle(psHandle);

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVLookupAndReleaseHandle

 @Description	Lookup the data pointer corresponding to a handle

 @Input		ppvData - location to return data pointer
		hHandle - handle from client
		eType - handle type
		eFlag - lookup flags

 @Output	ppvData - points to the data pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVLookupAndReleaseHandle(PVRSRV_HANDLE_BASE *psBase, IMG_PVOID *ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupAndReleaseHandle: Error looking up handle (%d)", eError));
		return eError;
	}

	*ppvData = psHandle->pvData;

	eError = FreeHandle(psBase, psHandle);

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVReleaseHandle

 @Description	Release a handle that is no longer needed

 @Input 	hHandle - handle from client
		eType - handle type

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVReleaseHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType)
{
	struct sHandle *psHandle;
	PVRSRV_ERROR eError;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	eError = GetHandleStructure(psBase, &psHandle, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: Error looking up handle (%d)", eError));
		return eError;
	}

	eError = FreeHandle(psBase, psHandle);

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVNewHandleBatch

 @Description	Start a new handle batch

 @Input 	psBase		- handle base
 @Input 	ui32BatchSize	- handle batch size

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVNewHandleBatch(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32BatchSize)
{
	PVRSRV_ERROR eError;

	if (HANDLES_BATCHED(psBase))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVNewHandleBatch: There is a handle batch already in use (size %u)", psBase->ui32HandBatchSize));
		return  PVRSRV_ERROR_HANDLE_BATCH_IN_USE;
	}

	if (ui32BatchSize == 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVNewHandleBatch: Invalid batch size (%u)", ui32BatchSize));
		return  PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = EnsureFreeHandles(psBase, ui32BatchSize);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVNewHandleBatch: EnsureFreeHandles failed (error %d)", eError));
		return eError;
	}

	psBase->ui32HandBatchSize = ui32BatchSize;

	/* Record current number of handles */
	psBase->ui32TotalHandCountPreBatch = psBase->ui32TotalHandCount;

	PVR_ASSERT(psBase->ui32BatchHandAllocFailures == 0);

	PVR_ASSERT(psBase->ui32FirstBatchIndexPlusOne == 0);

	PVR_ASSERT(HANDLES_BATCHED(psBase));

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVHandleBatchCommitOrRelease

 @Description	Release a handle batch

 @Input 	psBase	- handle base
		bCommit	- commit handles

 @Return	none

******************************************************************************/
static PVRSRV_ERROR PVRSRVHandleBatchCommitOrRelease(PVRSRV_HANDLE_BASE *psBase, IMG_BOOL bCommit)
{

	IMG_UINT32 ui32IndexPlusOne;
	IMG_BOOL bCommitBatch = bCommit;

	if (!HANDLES_BATCHED(psBase))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVHandleBatchCommitOrRelease: There is no handle batch"));
		return PVRSRV_ERROR_INVALID_PARAMS;

	}

	if (psBase->ui32BatchHandAllocFailures != 0)
	{
		if (bCommit)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVHandleBatchCommitOrRelease: Attempting to commit batch with handle allocation failures."));
		}
		bCommitBatch = IMG_FALSE;
	}
	/*
	 * The whole point of batched handles is to avoid handle allocation
	 * failures.
	 */
	PVR_ASSERT(psBase->ui32BatchHandAllocFailures == 0 || !bCommit);

	ui32IndexPlusOne = psBase->ui32FirstBatchIndexPlusOne;
	while(ui32IndexPlusOne != 0)
	{
		struct sHandle *psHandle = INDEX_TO_HANDLE_STRUCT_PTR(psBase, ui32IndexPlusOne - 1);
		IMG_UINT32 ui32NextIndexPlusOne = psHandle->ui32NextIndexPlusOne;
		PVR_ASSERT(BATCHED_HANDLE(psHandle));

		psHandle->ui32NextIndexPlusOne = 0;

		if (!bCommitBatch || BATCHED_HANDLE_PARTIALLY_FREE(psHandle))
		{
			PVRSRV_ERROR eError;

			/*
			 * We need a complete free here.  If the handle
			 * is not partially free, set the handle as
			 * unbatched to avoid a partial free.
			 */
			if (!BATCHED_HANDLE_PARTIALLY_FREE(psHandle))
			{
				/* PRQA S 1474,4130 1 */ /* ignore warnings about enum types being modified */
				SET_UNBATCHED_HANDLE(psHandle); /* PRQA S 4130 */ /* mis-use of enums FIXME*/
			}

			eError = FreeHandle(psBase, psHandle);
			if (eError != PVRSRV_OK)
			{
				 PVR_DPF((PVR_DBG_ERROR, "PVRSRVHandleBatchCommitOrRelease: Error freeing handle (%d)", eError));
			}
			PVR_ASSERT(eError == PVRSRV_OK);
		}
		else
		{
			/* PRQA S 1474,4130 1 */ /* ignore warnings about enum types being modified */
			SET_UNBATCHED_HANDLE(psHandle);
		}

		ui32IndexPlusOne = ui32NextIndexPlusOne;
	}

#ifdef DEBUG
	if (psBase->ui32TotalHandCountPreBatch != psBase->ui32TotalHandCount)
	{
		IMG_UINT32 ui32Delta = psBase->ui32TotalHandCount - psBase->ui32TotalHandCountPreBatch;

		PVR_ASSERT(psBase->ui32TotalHandCount > psBase->ui32TotalHandCountPreBatch);

		PVR_DPF((PVR_DBG_WARNING, "PVRSRVHandleBatchCommitOrRelease: The batch size was too small.  Batch size was %u, but needs to be %u", psBase->ui32HandBatchSize,  psBase->ui32HandBatchSize + ui32Delta));

	}
#endif

	psBase->ui32HandBatchSize = 0;
	psBase->ui32FirstBatchIndexPlusOne = 0;
	psBase->ui32TotalHandCountPreBatch = 0;
	psBase->ui32BatchHandAllocFailures = 0;

	if (psBase->ui32BatchHandAllocFailures != 0 && bCommit)
	{
		PVR_ASSERT(!bCommitBatch);

		return PVRSRV_ERROR_HANDLE_BATCH_COMMIT_FAILURE;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVCommitHandleBatch

 @Description	Commit a handle batch

 @Input 	psBase	- handle base

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVCommitHandleBatch(PVRSRV_HANDLE_BASE *psBase)
{
	return PVRSRVHandleBatchCommitOrRelease(psBase, IMG_TRUE);
}

/*!
******************************************************************************

 @Function	PVRSRReleaseHandleBatch

 @Description	Release a handle batch

 @Input 	psBase	- handle base

 @Return	none

******************************************************************************/
IMG_VOID PVRSRVReleaseHandleBatch(PVRSRV_HANDLE_BASE *psBase)
{
	(IMG_VOID) PVRSRVHandleBatchCommitOrRelease(psBase, IMG_FALSE);
}

/*!
******************************************************************************

 @Function	PVRSRVSetMaxHandle

 @Description	Set maximum handle number for given handle base

 @Input 	psBase - pointer to handle base structure
		ui32MaxHandle - Maximum handle number

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVSetMaxHandle(PVRSRV_HANDLE_BASE *psBase, IMG_UINT32 ui32MaxHandle)
{
	IMG_UINT32 ui32MaxHandleRounded;

	if (HANDLES_BATCHED(psBase))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSetMaxHandle: Limit cannot be set whilst in batch mode"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* Validate the limit */
	if (ui32MaxHandle  == 0 || ui32MaxHandle > DEFAULT_MAX_HANDLE)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSetMaxHandle: Limit must be between %u and %u, inclusive", 0, DEFAULT_MAX_HANDLE));

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	/* The limit can only be set if no handles have been allocated */
	if (psBase->ui32TotalHandCount != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVSetMaxHandle: Limit cannot be set because handles have already been allocated"));

		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	ui32MaxHandleRounded = ROUND_DOWN_TO_MULTIPLE_OF_BLOCK_SIZE(ui32MaxHandle);

	/*
	 * Allow the maximum number of handles to be reduced, but never to
	 * zero.
	 */
	if (ui32MaxHandleRounded != 0 && ui32MaxHandleRounded < psBase->ui32MaxIndexPlusOne)
	{
		psBase->ui32MaxIndexPlusOne = ui32MaxHandleRounded;
	}

	PVR_ASSERT(psBase->ui32MaxIndexPlusOne != 0);
	PVR_ASSERT(psBase->ui32MaxIndexPlusOne <= DEFAULT_MAX_INDEX_PLUS_ONE);
	PVR_ASSERT((psBase->ui32MaxIndexPlusOne % HANDLE_BLOCK_SIZE) == 0);

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVGetMaxHandle

 @Description	Get maximum handle number for given handle base

 @Input 	psBase - pointer to handle base structure

 @Output 	Maximum handle number, or 0 if handle limits not
		supported.

 @Return	Error code or PVRSRV_OK

******************************************************************************/
IMG_UINT32 PVRSRVGetMaxHandle(PVRSRV_HANDLE_BASE *psBase)
{
	return psBase->ui32MaxIndexPlusOne;
}

/*!
******************************************************************************

 @Function	PVRSRVEnableHandlePurging

 @Description	Enable purging for a given handle base

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVEnableHandlePurging(PVRSRV_HANDLE_BASE *psBase)
{
	if (psBase->bPurgingEnabled)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRSRVEnableHandlePurging: Purging already enabled"));
		return PVRSRV_OK;
	}

	/* Purging can only be enabled if no handles have been allocated */
	if (psBase->ui32TotalHandCount != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVEnableHandlePurging: Handles have already been allocated"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psBase->bPurgingEnabled = IMG_TRUE;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVPurgeHandles

 @Description	Purge handles for a given handle base

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVPurgeHandles(PVRSRV_HANDLE_BASE *psBase)
{
	IMG_UINT32 ui32BlockIndex;
	IMG_UINT32 ui32NewHandCount;

	if (!psBase->bPurgingEnabled)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPurgeHandles: Purging not enabled for this handle base"));
		return PVRSRV_ERROR_NOT_SUPPORTED;
	}

	if (HANDLES_BATCHED(psBase))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPurgeHandles: Purging not allowed whilst in batch mode"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	PVR_ASSERT((psBase->ui32TotalHandCount % HANDLE_BLOCK_SIZE) == 0);

	for (ui32BlockIndex = INDEX_TO_BLOCK_INDEX(psBase->ui32TotalHandCount); ui32BlockIndex != 0; ui32BlockIndex--)
	{
		if (psBase->psHandleArray[ui32BlockIndex - 1].ui32FreeHandBlockCount != HANDLE_BLOCK_SIZE)
		{
			break;
		}
	}
	ui32NewHandCount = BLOCK_INDEX_TO_INDEX(ui32BlockIndex);

	/*
	 * Check for a suitable decrease in the handle count.
	 */
	if (ui32NewHandCount <= (psBase->ui32TotalHandCount/2))
	{
		PVRSRV_ERROR eError;

		// PVR_TRACE((" PVRSRVPurgeHandles: reducing number of handles from %u to %u", psBase->ui32TotalHandCount, ui32NewHandCount));

		eError = ReallocHandleArray(psBase, ui32NewHandCount);
		if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVAllocHandleBase

 @Description	Allocate a handle base structure for a process

 @Input 	ppsBase - pointer to handle base structure pointer

 @Output	ppsBase - points to handle base structure pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandleBase(PVRSRV_HANDLE_BASE **ppsBase)
{
	PVRSRV_HANDLE_BASE *psBase;
	IMG_HANDLE hBlockAlloc;
	PVRSRV_ERROR eError;

	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
		sizeof(*psBase),
		(IMG_PVOID *)&psBase,
		&hBlockAlloc,
		"Handle Base");
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Couldn't allocate handle base (%d)", eError));
		return eError;
	}
	OSMemSet(psBase, 0, sizeof(*psBase));

	/* Create hash table */
	psBase->psHashTab = HASH_Create_Extended(HANDLE_HASH_TAB_INIT_SIZE, sizeof(HAND_KEY), HASH_Func_Default, HASH_Key_Comp_Default);
	if (psBase->psHashTab == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Couldn't create data pointer hash table\n"));
		(IMG_VOID)PVRSRVFreeHandleBase(psBase);
		return PVRSRV_ERROR_UNABLE_TO_CREATE_HASH_TABLE;
	}

	psBase->hBaseBlockAlloc = hBlockAlloc;

	psBase->ui32MaxIndexPlusOne = DEFAULT_MAX_INDEX_PLUS_ONE;

	*ppsBase = psBase;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PVRSRVFreeHandleBase

 @Description	Free a handle base structure

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVFreeHandleBase(PVRSRV_HANDLE_BASE *psBase)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(psBase != gpsKernelHandleBase);

	eError = FreeHandleBase(psBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeHandleBase: FreeHandleBase failed (%d)", eError));
	}

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVHandleInit

 @Description	Initialise handle management

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVHandleInit(IMG_VOID)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsKernelHandleBase == IMG_NULL);

	eError = PVRSRVAllocHandleBase(&gpsKernelHandleBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVHandleInit: PVRSRVAllocHandleBase failed (%d)", eError));
		goto error;
	}

	eError = PVRSRVEnableHandlePurging(gpsKernelHandleBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVHandleInit: PVRSRVEnableHandlePurging failed (%d)", eError));
		goto error;
	}

	return PVRSRV_OK;
error:
	(IMG_VOID) PVRSRVHandleDeInit();
	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVHandleDeInit

 @Description	De-initialise handle management

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVHandleDeInit(IMG_VOID)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (gpsKernelHandleBase != IMG_NULL)
	{
		eError = FreeHandleBase(gpsKernelHandleBase);
		if (eError == PVRSRV_OK)
		{
			gpsKernelHandleBase = IMG_NULL;
		}
		else
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVHandleDeInit: FreeHandleBase failed (%d)", eError));
		}
	}

	return eError;
}
#else
/* disable warning about empty module */
#endif	/* #if defined(PVR_SECURE_HANDLES) */
/******************************************************************************
 End of file (handle.c)
******************************************************************************/
