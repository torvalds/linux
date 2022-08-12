/*************************************************************************/ /*!
@File
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
*/ /***************************************************************************/

/* See handle.h for a description of the handle API. */

/*
 * The implementation supports movable handle structures, allowing the address
 * of a handle structure to change without having to fix up pointers in
 * any of the handle structures. For example, the linked list mechanism
 * used to link subhandles together uses handle array indices rather than
 * pointers to the structures themselves.
 */

#if defined(__linux__)
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

#include "img_defs.h"
#include "handle.h"
#include "handle_impl.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "osfunc.h"
#include "lock.h"
#include "connection_server.h"

#define	HANDLE_HASH_TAB_INIT_SIZE 32
#define HANDLE_PROC_HANDLE_HASH_INIT_SIZE 10

#define	TEST_FLAG(v, f) BITMASK_HAS(v, f)
#define	TEST_ALLOC_FLAG(psHandleData, f) BITMASK_HAS((psHandleData)->eFlag, f)


/* Linked list structure. Used for both the list head and list items */
typedef struct _HANDLE_LIST_
{
	IMG_HANDLE hPrev;
	IMG_HANDLE hNext;
	IMG_HANDLE hParent;
} HANDLE_LIST;

typedef struct _HANDLE_DATA_
{
	/* The handle that represents this structure */
	IMG_HANDLE hHandle;

	/* Handle type */
	PVRSRV_HANDLE_TYPE eType;

	/* Flags specified when the handle was allocated */
	PVRSRV_HANDLE_ALLOC_FLAG eFlag;

	/* Pointer to the data that the handle represents */
	void *pvData;

	/*
	 * Callback specified at handle allocation time to
	 * release/destroy/free the data represented by the
	 * handle when it's reference count reaches 0. This
	 * should always be NULL for subhandles.
	 */
	PFN_HANDLE_RELEASE pfnReleaseData;

	/* List head for subhandles of this handle */
	HANDLE_LIST sChildren;

	/* List entry for sibling subhandles */
	HANDLE_LIST sSiblings;

	/* Reference count of lookups made. It helps track which resources are in
	 * use in concurrent bridge calls. */
	IMG_INT32 iLookupCount;
	/* State of a handle. If the handle was already destroyed this is false.
	 * If this is false and iLookupCount is 0 the pfnReleaseData callback is
	 * called on the handle. */
	IMG_BOOL bCanLookup;

#if defined(PVRSRV_DEBUG_HANDLE_LOCK)
	/* Store the handle base used for this handle, so we
	 * can later access the handle base lock (or check if
	 * it has been already acquired)
	 */
	PVRSRV_HANDLE_BASE *psBase;
#endif

} HANDLE_DATA;

struct _HANDLE_BASE_
{
	/* Pointer to a handle implementations base structure */
	HANDLE_IMPL_BASE *psImplBase;

	/*
	 * Pointer to handle hash table.
	 * The hash table is used to do reverse lookups, converting data
	 * pointers to handles.
	 */
	HASH_TABLE *psHashTab;

	/* Type specific (connection/global/process) Lock handle */
	POS_LOCK hLock;

	/* Can be connection, process, global */
	PVRSRV_HANDLE_BASE_TYPE eType;
};

/*
 * The key for the handle hash table is an array of three elements, the
 * pointer to the resource, the resource type and the parent handle (or
 * NULL if there is no parent). The eHandKey enumeration gives the
 * array indices of the elements making up the key.
 */
enum eHandKey
{
	HAND_KEY_DATA = 0,
	HAND_KEY_TYPE,
	HAND_KEY_PARENT,
	HAND_KEY_LEN		/* Must be last item in list */
};

/* HAND_KEY is the type of the hash table key */
typedef uintptr_t HAND_KEY[HAND_KEY_LEN];

typedef struct FREE_HANDLE_DATA_TAG
{
	PVRSRV_HANDLE_BASE *psBase;
	PVRSRV_HANDLE_TYPE eHandleFreeType;
	/* timing data (ns) to release bridge lock upon the deadline */
	IMG_UINT64 ui64TimeStart;
	IMG_UINT64 ui64MaxBridgeTime;
} FREE_HANDLE_DATA;

typedef struct FREE_KERNEL_HANDLE_DATA_TAG
{
	PVRSRV_HANDLE_BASE *psBase;
	HANDLE_DATA *psProcessHandleData;
	IMG_HANDLE hKernelHandle;
} FREE_KERNEL_HANDLE_DATA;

/* Stores a pointer to the function table of the handle back-end in use */
static HANDLE_IMPL_FUNCTAB const *gpsHandleFuncs;

static POS_LOCK gKernelHandleLock;
static IMG_BOOL gbLockInitialised = IMG_FALSE;
/* Pointer to process handle base currently being freed */
static PVRSRV_HANDLE_BASE *g_psProcessHandleBaseBeingFreed;
/* Lock for the process handle base table */
static POS_LOCK g_hProcessHandleBaseLock;
/* Hash table with process handle bases */
static HASH_TABLE *g_psProcessHandleBaseTable;

void LockHandle(PVRSRV_HANDLE_BASE *psBase)
{
	OSLockAcquire(psBase->hLock);
}

void UnlockHandle(PVRSRV_HANDLE_BASE *psBase)
{
	OSLockRelease(psBase->hLock);
}

/*
 * Kernel handle base structure. This is used for handles that are not
 * allocated on behalf of a particular process.
 */
PVRSRV_HANDLE_BASE *gpsKernelHandleBase = NULL;

/* Increase the lookup reference count on the given handle.
 * The handle lock must already be acquired.
 * Returns: the reference count after the increment
 */
static inline IMG_UINT32 HandleGet(HANDLE_DATA *psHandleData)
{
#if defined(PVRSRV_DEBUG_HANDLE_LOCK)
	if (!OSLockIsLocked(psHandleData->psBase->hLock))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle lock is not locked", __func__));
		OSDumpStack();
	}
#endif

#ifdef DEBUG_REFCNT
	PVR_DPF((PVR_DBG_ERROR, "%s: bCanLookup = %u, iLookupCount %d -> %d",
	        __func__, psHandleData->bCanLookup, psHandleData->iLookupCount,
	        psHandleData->iLookupCount + 1));
#endif /* DEBUG_REFCNT */

	PVR_ASSERT(psHandleData->bCanLookup);

	return ++psHandleData->iLookupCount;
}

/* Decrease the lookup reference count on the given handle.
 * The handle lock must already be acquired.
 * Returns: the reference count after the decrement
 */
static inline IMG_UINT32 HandlePut(HANDLE_DATA *psHandleData)
{
#if defined(PVRSRV_DEBUG_HANDLE_LOCK)
	if (!OSLockIsLocked(psHandleData->psBase->hLock))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle lock is not locked", __func__));
		OSDumpStack();
	}
#endif

#ifdef DEBUG_REFCNT
	PVR_DPF((PVR_DBG_ERROR, "%s: bCanLookup = %u, iLookupCount %d -> %d",
	        __func__, psHandleData->bCanLookup, psHandleData->iLookupCount,
	        psHandleData->iLookupCount - 1));
#endif /* DEBUG_REFCNT */

	/* psHandleData->bCanLookup can be false at this point */
	PVR_ASSERT(psHandleData->iLookupCount > 0);

	return --psHandleData->iLookupCount;
}

static inline IMG_BOOL IsRetryError(PVRSRV_ERROR eError)
{
	return eError == PVRSRV_ERROR_RETRY || eError == PVRSRV_ERROR_KERNEL_CCB_FULL;
}

#if defined(PVRSRV_NEED_PVR_DPF)
static const IMG_CHAR *HandleTypeToString(PVRSRV_HANDLE_TYPE eType)
{
	#define HANDLETYPE(x) \
			case PVRSRV_HANDLE_TYPE_##x: \
				return #x;
	switch (eType)
	{
		#include "handle_types.h"
		#undef HANDLETYPE

		default:
			return "INVALID";
	}
}

static const IMG_CHAR *HandleBaseTypeToString(PVRSRV_HANDLE_BASE_TYPE eType)
{
	#define HANDLEBASETYPE(x) \
			case PVRSRV_HANDLE_BASE_TYPE_##x: \
				return #x;
	switch (eType)
	{
		HANDLEBASETYPE(CONNECTION);
		HANDLEBASETYPE(PROCESS);
		HANDLEBASETYPE(GLOBAL);
		#undef HANDLEBASETYPE

		default:
			return "INVALID";
	}
}
#endif

static PVRSRV_ERROR HandleUnrefAndMaybeMarkForFree(PVRSRV_HANDLE_BASE *psBase,
                                                   HANDLE_DATA *psHandleData,
                                                   IMG_HANDLE hHandle,
                                                   PVRSRV_HANDLE_TYPE eType);

static PVRSRV_ERROR HandleFreePrivData(PVRSRV_HANDLE_BASE *psBase,
                                       HANDLE_DATA *psHandleData,
                                       IMG_HANDLE hHandle,
                                       PVRSRV_HANDLE_TYPE eType);

static PVRSRV_ERROR HandleFreeDestroy(PVRSRV_HANDLE_BASE *psBase,
                                      HANDLE_DATA *psHandleData,
                                      IMG_HANDLE hHandle,
                                      PVRSRV_HANDLE_TYPE eType);

/*!
*******************************************************************************
 @Function      GetHandleData
 @Description   Get the handle data structure for a given handle
 @Input         psBase - pointer to handle base structure
                hHandle - handle from client
                eType - handle type or PVRSRV_HANDLE_TYPE_NONE if the handle
                        type is not to be checked.
 @Output        ppsHandleData - pointer to a pointer to the handle data struct
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(GetHandleData)
#endif
static INLINE
PVRSRV_ERROR GetHandleData(PVRSRV_HANDLE_BASE *psBase,
			   HANDLE_DATA **ppsHandleData,
			   IMG_HANDLE hHandle,
			   PVRSRV_HANDLE_TYPE eType)
{
	HANDLE_DATA *psHandleData;
	PVRSRV_ERROR eError;

	eError = gpsHandleFuncs->pfnGetHandleData(psBase->psImplBase,
						  hHandle,
						  (void **)&psHandleData);
	PVR_RETURN_IF_ERROR(eError);

	/*
	 * Unless PVRSRV_HANDLE_TYPE_NONE was passed in to this function,
	 * check handle is of the correct type.
	 */
	if (unlikely(eType != PVRSRV_HANDLE_TYPE_NONE && eType != psHandleData->eType))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "GetHandleData: Type mismatch. Lookup request: Handle %p, type: %s (%u) but stored handle is type %s (%u)",
			 hHandle,
			 HandleTypeToString(eType),
			 eType,
			 HandleTypeToString(psHandleData->eType),
			 psHandleData->eType));
		return PVRSRV_ERROR_HANDLE_TYPE_MISMATCH;
	}

	/* Return the handle structure */
	*ppsHandleData = psHandleData;

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      HandleListInit
 @Description   Initialise a linked list structure embedded in a handle
                structure.
 @Input         hHandle - handle containing the linked list structure
                psList - pointer to linked list structure
                hParent - parent handle or NULL
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListInit)
#endif
static INLINE
void HandleListInit(IMG_HANDLE hHandle, HANDLE_LIST *psList, IMG_HANDLE hParent)
{
	psList->hPrev = hHandle;
	psList->hNext = hHandle;
	psList->hParent = hParent;
}

/*!
*******************************************************************************
 @Function      InitParentList
 @Description   Initialise the children list head in a handle structure.
                The children are the subhandles of this handle.
 @Input         psHandleData - pointer to handle data structure
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(InitParentList)
#endif
static INLINE
void InitParentList(HANDLE_DATA *psHandleData)
{
	IMG_HANDLE hParent = psHandleData->hHandle;

	HandleListInit(hParent, &psHandleData->sChildren, hParent);
}

/*!
*******************************************************************************

 @Function      InitChildEntry
 @Description   Initialise the child list entry in a handle structure. The list
                entry is used to link together subhandles of a given handle.
 @Input         psHandleData - pointer to handle data structure
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(InitChildEntry)
#endif
static INLINE
void InitChildEntry(HANDLE_DATA *psHandleData)
{
	HandleListInit(psHandleData->hHandle, &psHandleData->sSiblings, NULL);
}

/*!
*******************************************************************************
 @Function      HandleListIsEmpty
 @Description   Determine whether a given linked list is empty.
 @Input         hHandle - handle containing the list head
                psList - pointer to the list head
 @Return        IMG_TRUE if the list is empty, IMG_FALSE if it isn't.
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListIsEmpty)
#endif
static INLINE
IMG_BOOL HandleListIsEmpty(IMG_HANDLE hHandle, HANDLE_LIST *psList) /* Instead of passing in the handle can we not just do (psList->hPrev == psList->hNext) ? IMG_TRUE : IMG_FALSE ??? */
{
	IMG_BOOL bIsEmpty;

	bIsEmpty = (IMG_BOOL)(psList->hNext == hHandle);

#ifdef DEBUG
	{
		IMG_BOOL bIsEmpty2;

		bIsEmpty2 = (IMG_BOOL)(psList->hPrev == hHandle);
		PVR_ASSERT(bIsEmpty == bIsEmpty2);
	}
#endif

	return bIsEmpty;
}

#ifdef DEBUG
/*!
*******************************************************************************
 @Function      NoChildren
 @Description   Determine whether a handle has any subhandles
 @Input         psHandleData - pointer to handle data structure
 @Return        IMG_TRUE if the handle has no subhandles, IMG_FALSE if it does.
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(NoChildren)
#endif
static INLINE
IMG_BOOL NoChildren(HANDLE_DATA *psHandleData)
{
	PVR_ASSERT(psHandleData->sChildren.hParent == psHandleData->hHandle);

	return HandleListIsEmpty(psHandleData->hHandle, &psHandleData->sChildren);
}

/*!
*******************************************************************************
 @Function      NoParent
 @Description   Determine whether a handle is a subhandle
 @Input         psHandleData - pointer to handle data structure
 @Return        IMG_TRUE if the handle is not a subhandle, IMG_FALSE if it is.
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(NoParent)
#endif
static INLINE
IMG_BOOL NoParent(HANDLE_DATA *psHandleData)
{
	if (HandleListIsEmpty(psHandleData->hHandle, &psHandleData->sSiblings))
	{
		PVR_ASSERT(psHandleData->sSiblings.hParent == NULL);

		return IMG_TRUE;
	}

	PVR_ASSERT(psHandleData->sSiblings.hParent != NULL);
	return IMG_FALSE;
}
#endif /*DEBUG*/

/*!
*******************************************************************************
 @Function      ParentHandle
 @Description   Determine the parent of a handle
 @Input         psHandleData - pointer to handle data structure
 @Return        Parent handle, or NULL if the handle is not a subhandle.
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(ParentHandle)
#endif
static INLINE
IMG_HANDLE ParentHandle(HANDLE_DATA *psHandleData)
{
	return psHandleData->sSiblings.hParent;
}

/*
 * GetHandleListFromHandleAndOffset is used to generate either a
 * pointer to the subhandle list head, or a pointer to the linked list
 * structure of an item on a subhandle list.
 * The list head is itself on the list, but is at a different offset
 * in the handle structure to the linked list structure for items on
 * the list. The two linked list structures are differentiated by
 * the third parameter, containing the parent handle. The parent field
 * in the list head structure references the handle structure that contains
 * it. For items on the list, the parent field in the linked list structure
 * references the parent handle, which will be different from the handle
 * containing the linked list structure.
 */
#ifdef INLINE_IS_PRAGMA
#pragma inline(GetHandleListFromHandleAndOffset)
#endif
static INLINE
HANDLE_LIST *GetHandleListFromHandleAndOffset(PVRSRV_HANDLE_BASE *psBase,
					      IMG_HANDLE hEntry,
					      IMG_HANDLE hParent,
					      size_t uiParentOffset,
					      size_t uiEntryOffset)
{
	HANDLE_DATA *psHandleData = NULL;

	PVR_ASSERT(psBase != NULL);

	if (GetHandleData(psBase, &psHandleData, hEntry,
	                  PVRSRV_HANDLE_TYPE_NONE) != PVRSRV_OK)
	{
		return NULL;
	}

	if (hEntry == hParent)
	{
		return (HANDLE_LIST *)IMG_OFFSET_ADDR(psHandleData, uiParentOffset);
	}
	else
	{
		return (HANDLE_LIST *)IMG_OFFSET_ADDR(psHandleData, uiEntryOffset);
	}
}

/*!
*******************************************************************************
 @Function      HandleListInsertBefore
 @Description   Insert a handle before a handle currently on the list.
 @Input         hEntry - handle to be inserted after
                psEntry - pointer to handle structure to be inserted after
                uiParentOffset - offset to list head struct in handle structure
                hNewEntry - handle to be inserted
                psNewEntry - pointer to handle structure of item to be inserted
                uiEntryOffset - offset of list item struct in handle structure
                hParent - parent handle of hNewEntry
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListInsertBefore)
#endif
static INLINE
PVRSRV_ERROR HandleListInsertBefore(PVRSRV_HANDLE_BASE *psBase,
				    IMG_HANDLE hEntry,
				    HANDLE_LIST *psEntry,
				    size_t uiParentOffset,
				    IMG_HANDLE hNewEntry,
				    HANDLE_LIST *psNewEntry,
				    size_t uiEntryOffset,
				    IMG_HANDLE hParent)
{
	HANDLE_LIST *psPrevEntry;

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psEntry != NULL, "psEntry");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psNewEntry != NULL, "psNewEntry");

	psPrevEntry = GetHandleListFromHandleAndOffset(psBase,
						       psEntry->hPrev,
						       hParent,
						       uiParentOffset,
						       uiEntryOffset);
	if (psPrevEntry == NULL)
	{
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}

	PVR_ASSERT(psNewEntry->hParent == NULL);
	PVR_ASSERT(hEntry == psPrevEntry->hNext);

#if defined(DEBUG)
	{
		HANDLE_LIST *psParentList;

		psParentList = GetHandleListFromHandleAndOffset(psBase,
								hParent,
								hParent,
								uiParentOffset,
								uiParentOffset);
		PVR_ASSERT(psParentList && psParentList->hParent == hParent);
	}
#endif /* defined(DEBUG) */

	psNewEntry->hPrev = psEntry->hPrev;
	psEntry->hPrev = hNewEntry;

	psNewEntry->hNext = hEntry;
	psPrevEntry->hNext = hNewEntry;

	psNewEntry->hParent = hParent;

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      AdoptChild
 @Description   Assign a subhandle to a handle
 @Input         psParentData - pointer to handle structure of parent handle
                psChildData - pointer to handle structure of child subhandle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(AdoptChild)
#endif
static INLINE
PVRSRV_ERROR AdoptChild(PVRSRV_HANDLE_BASE *psBase,
			HANDLE_DATA *psParentData,
			HANDLE_DATA *psChildData)
{
	IMG_HANDLE hParent = psParentData->sChildren.hParent;

	PVR_ASSERT(hParent == psParentData->hHandle);

	return HandleListInsertBefore(psBase,
				      hParent,
				      &psParentData->sChildren,
				      offsetof(HANDLE_DATA, sChildren),
				      psChildData->hHandle,
				      &psChildData->sSiblings,
				      offsetof(HANDLE_DATA, sSiblings),
				      hParent);
}

/*!
*******************************************************************************
 @Function      HandleListRemove
 @Description   Remove a handle from a list
 @Input         hEntry - handle to be removed
                psEntry - pointer to handle structure of item to be removed
                uiEntryOffset - offset of list item struct in handle structure
                uiParentOffset - offset to list head struct in handle structure
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListRemove)
#endif
static INLINE
PVRSRV_ERROR HandleListRemove(PVRSRV_HANDLE_BASE *psBase,
			      IMG_HANDLE hEntry,
			      HANDLE_LIST *psEntry,
			      size_t uiEntryOffset,
			      size_t uiParentOffset)
{
	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psEntry != NULL, "psEntry");

	if (!HandleListIsEmpty(hEntry, psEntry))
	{
		HANDLE_LIST *psPrev;
		HANDLE_LIST *psNext;

		psPrev = GetHandleListFromHandleAndOffset(psBase,
							  psEntry->hPrev,
							  psEntry->hParent,
							  uiParentOffset,
							  uiEntryOffset);
		if (psPrev == NULL)
		{
			return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
		}

		psNext = GetHandleListFromHandleAndOffset(psBase,
							  psEntry->hNext,
							  psEntry->hParent,
							  uiParentOffset,
							  uiEntryOffset);
		if (psNext == NULL)
		{
			return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
		}

		/*
		 * The list head is on the list, and we don't want to
		 * remove it.
		 */
		PVR_ASSERT(psEntry->hParent != NULL);

		psPrev->hNext = psEntry->hNext;
		psNext->hPrev = psEntry->hPrev;

		HandleListInit(hEntry, psEntry, NULL);
	}

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      UnlinkFromParent
 @Description   Remove a subhandle from its parents list
 @Input         psHandleData - pointer to handle data structure of child
                               subhandle.
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(UnlinkFromParent)
#endif
static INLINE
PVRSRV_ERROR UnlinkFromParent(PVRSRV_HANDLE_BASE *psBase,
			      HANDLE_DATA *psHandleData)
{
	return HandleListRemove(psBase,
				psHandleData->hHandle,
				&psHandleData->sSiblings,
				offsetof(HANDLE_DATA, sSiblings),
				offsetof(HANDLE_DATA, sChildren));
}

/*!
*******************************************************************************
 @Function      HandleListIterate
 @Description   Iterate over the items in a list
 @Input         psHead - pointer to list head
                uiParentOffset - offset to list head struct in handle structure
                uiEntryOffset - offset of list item struct in handle structure
                pfnIterFunc - function to be called for each handle in the list
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListIterate)
#endif
static INLINE
PVRSRV_ERROR HandleListIterate(PVRSRV_HANDLE_BASE *psBase,
			       HANDLE_LIST *psHead,
			       size_t uiParentOffset,
			       size_t uiEntryOffset,
			       PVRSRV_ERROR (*pfnIterFunc)(PVRSRV_HANDLE_BASE *, IMG_HANDLE))
{
	IMG_HANDLE hHandle = psHead->hNext;
	IMG_HANDLE hParent = psHead->hParent;
	IMG_HANDLE hNext;

	PVR_ASSERT(psHead->hParent != NULL);

	/*
	 * Follow the next chain from the list head until we reach
	 * the list head again, which signifies the end of the list.
	 */
	while (hHandle != hParent)
	{
		HANDLE_LIST *psEntry;
		PVRSRV_ERROR eError;

		psEntry = GetHandleListFromHandleAndOffset(psBase,
							   hHandle,
							   hParent,
							   uiParentOffset,
							   uiEntryOffset);
		if (psEntry == NULL)
		{
			return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
		}

		PVR_ASSERT(psEntry->hParent == psHead->hParent);

		/*
		 * Get the next index now, in case the list item is
		 * modified by the iteration function.
		 */
		hNext = psEntry->hNext;

		eError = (*pfnIterFunc)(psBase, hHandle);
		PVR_RETURN_IF_ERROR(eError);

		hHandle = hNext;
	}

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      IterateOverChildren
 @Description   Iterate over the subhandles of a parent handle
 @Input         psParentData - pointer to parent handle structure
                pfnIterFunc - function to be called for each subhandle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(IterateOverChildren)
#endif
static INLINE
PVRSRV_ERROR IterateOverChildren(PVRSRV_HANDLE_BASE *psBase,
				 HANDLE_DATA *psParentData,
				 PVRSRV_ERROR (*pfnIterFunc)(PVRSRV_HANDLE_BASE *, IMG_HANDLE))
{
	 return HandleListIterate(psBase,
				  &psParentData->sChildren,
				  offsetof(HANDLE_DATA, sChildren),
				  offsetof(HANDLE_DATA, sSiblings),
				  pfnIterFunc);
}

/*!
*******************************************************************************
 @Function      ParentIfPrivate
 @Description   Return the parent handle if the handle was allocated with
                PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE, else return NULL.
 @Input         psHandleData - pointer to handle data structure
 @Return        Parent handle or NULL
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(ParentIfPrivate)
#endif
static INLINE
IMG_HANDLE ParentIfPrivate(HANDLE_DATA *psHandleData)
{
	return TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ?
			ParentHandle(psHandleData) : NULL;
}

/*!
*******************************************************************************
 @Function      InitKey
 @Description   Initialise a hash table key for the current process
 @Input         aKey - pointer to key
                psBase - pointer to handle base structure
                pvData - pointer to the resource the handle represents
                eType - type of resource
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(InitKey)
#endif
static INLINE
void InitKey(HAND_KEY aKey,
	     PVRSRV_HANDLE_BASE *psBase,
	     void *pvData,
	     PVRSRV_HANDLE_TYPE eType,
	     IMG_HANDLE hParent)
{
	PVR_UNREFERENCED_PARAMETER(psBase);

	aKey[HAND_KEY_DATA] = (uintptr_t)pvData;
	aKey[HAND_KEY_TYPE] = (uintptr_t)eType;
	aKey[HAND_KEY_PARENT] = (uintptr_t)hParent;
}

/*!
*******************************************************************************
 @Function      FindHandle
 @Description   Find handle corresponding to a resource pointer
 @Input         psBase - pointer to handle base structure
                pvData - pointer to resource to be associated with the handle
                eType - the type of resource
 @Return        The handle, or NULL if not found
******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(FindHandle)
#endif
static INLINE
IMG_HANDLE FindHandle(PVRSRV_HANDLE_BASE *psBase,
		      void *pvData,
		      PVRSRV_HANDLE_TYPE eType,
		      IMG_HANDLE hParent)
{
	HAND_KEY aKey;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);

	InitKey(aKey, psBase, pvData, eType, hParent);

	return (IMG_HANDLE) HASH_Retrieve_Extended(psBase->psHashTab, aKey);
}

/*!
*******************************************************************************
 @Function      AllocHandle
 @Description   Allocate a new handle
 @Input         phHandle - location for new handle
                pvData - pointer to resource to be associated with the handle
                eType - the type of resource
                hParent - parent handle or NULL
                pfnReleaseData - Function to release resource at handle release
                                 time
 @Output        phHandle - points to new handle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
static PVRSRV_ERROR AllocHandle(PVRSRV_HANDLE_BASE *psBase,
				IMG_HANDLE *phHandle,
				void *pvData,
				PVRSRV_HANDLE_TYPE eType,
				PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				IMG_HANDLE hParent,
				PFN_HANDLE_RELEASE pfnReleaseData)
{
	HANDLE_DATA *psNewHandleData;
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(psBase != NULL && psBase->psHashTab != NULL);
	PVR_ASSERT(gpsHandleFuncs);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* Handle must not already exist */
		PVR_ASSERT(FindHandle(psBase, pvData, eType, hParent) == NULL);
	}

	psNewHandleData = OSAllocZMem(sizeof(*psNewHandleData));
	PVR_LOG_RETURN_IF_NOMEM(psNewHandleData, "OSAllocZMem");

	eError = gpsHandleFuncs->pfnAcquireHandle(psBase->psImplBase, &hHandle,
	                                          psNewHandleData);
	PVR_LOG_GOTO_IF_ERROR(eError, "pfnAcquireHandle",
	                  ErrorFreeHandleData);

	/*
	 * If a data pointer can be associated with multiple handles, we
	 * don't put the handle in the hash table, as the data pointer
	 * may not map to a unique handle
	 */
	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		HAND_KEY aKey;

		/* Initialise hash key */
		InitKey(aKey, psBase, pvData, eType, hParent);

		/* Put the new handle in the hash table */
		eError = HASH_Insert_Extended(psBase->psHashTab, aKey, (uintptr_t)hHandle) ?
		        PVRSRV_OK : PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;
		PVR_LOG_GOTO_IF_FALSE(eError == PVRSRV_OK, "couldn't add handle to hash table",
		                  ErrorReleaseHandle);
	}

	psNewHandleData->hHandle = hHandle;
	psNewHandleData->eType = eType;
	psNewHandleData->eFlag = eFlag;
	psNewHandleData->pvData = pvData;
	psNewHandleData->pfnReleaseData = pfnReleaseData;
	psNewHandleData->iLookupCount = 0;
	psNewHandleData->bCanLookup = IMG_TRUE;

#ifdef DEBUG_REFCNT
	PVR_DPF((PVR_DBG_ERROR, "%s: bCanLookup = true", __func__));
#endif /* DEBUG_REFCNT */

	InitParentList(psNewHandleData);
#if defined(DEBUG)
	PVR_ASSERT(NoChildren(psNewHandleData));
#endif

	InitChildEntry(psNewHandleData);
#if defined(DEBUG)
	PVR_ASSERT(NoParent(psNewHandleData));
#endif

#if defined(PVRSRV_DEBUG_HANDLE_LOCK)
	psNewHandleData->psBase = psBase;
#endif

	/* Return the new handle to the client */
	*phHandle = psNewHandleData->hHandle;

	return PVRSRV_OK;

ErrorReleaseHandle:
	(void)gpsHandleFuncs->pfnReleaseHandle(psBase->psImplBase, hHandle, NULL);

ErrorFreeHandleData:
	OSFreeMem(psNewHandleData);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVAllocHandle
 @Description   Allocate a handle
 @Input         psBase - pointer to handle base structure
                pvData - pointer to resource to be associated with the handle
                eType - the type of resource
                pfnReleaseData - Function to release resource at handle release
                                 time
 @Output        phHandle - points to new handle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE *psBase,
			       IMG_HANDLE *phHandle,
			       void *pvData,
			       PVRSRV_HANDLE_TYPE eType,
			       PVRSRV_HANDLE_ALLOC_FLAG eFlag,
			       PFN_HANDLE_RELEASE pfnReleaseData)
{
	PVRSRV_ERROR eError;

	LockHandle(psBase);
	eError = PVRSRVAllocHandleUnlocked(psBase, phHandle, pvData, eType, eFlag, pfnReleaseData);
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVAllocHandleUnlocked
 @Description   Allocate a handle without acquiring/releasing the handle lock.
                The function assumes you hold the lock when called.
 @Input         phHandle - location for new handle
                pvData - pointer to resource to be associated with the handle
                eType - the type of resource
                pfnReleaseData - Function to release resource at handle release
                                 time
 @Output        phHandle - points to new handle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
			       IMG_HANDLE *phHandle,
			       void *pvData,
			       PVRSRV_HANDLE_TYPE eType,
			       PVRSRV_HANDLE_ALLOC_FLAG eFlag,
			       PFN_HANDLE_RELEASE pfnReleaseData)
{
	*phHandle = NULL;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");
	PVR_LOG_RETURN_IF_INVALID_PARAM(pfnReleaseData != NULL, "pfnReleaseData");

	return AllocHandle(psBase, phHandle, pvData, eType, eFlag, NULL, pfnReleaseData);
}

/*!
*******************************************************************************
 @Function      PVRSRVAllocSubHandle
 @Description   Allocate a subhandle
 @Input         pvData - pointer to resource to be associated with the subhandle
                eType - the type of resource
                hParent - parent handle
 @Output        phHandle - points to new subhandle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVAllocSubHandle(PVRSRV_HANDLE_BASE *psBase,
				  IMG_HANDLE *phHandle,
				  void *pvData,
				  PVRSRV_HANDLE_TYPE eType,
				  PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				  IMG_HANDLE hParent)
{
	PVRSRV_ERROR eError;

	LockHandle(psBase);
	eError = PVRSRVAllocSubHandleUnlocked(psBase, phHandle, pvData, eType, eFlag, hParent);
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVAllocSubHandleUnlocked
 @Description   Allocate a subhandle without acquiring/releasing the handle
                lock. The function assumes you hold the lock when called.
 @Input         pvData - pointer to resource to be associated with the subhandle
                eType - the type of resource
                hParent - parent handle
 @Output        phHandle - points to new subhandle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVAllocSubHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
				  IMG_HANDLE *phHandle,
				  void *pvData,
				  PVRSRV_HANDLE_TYPE eType,
				  PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				  IMG_HANDLE hParent)
{
	HANDLE_DATA *psPHandleData = NULL;
	HANDLE_DATA *psCHandleData = NULL;
	IMG_HANDLE hParentKey;
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	*phHandle = NULL;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_GOTO_IF_INVALID_PARAM(psBase, eError, Exit);

	hParentKey = TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ? hParent : NULL;

	/* Lookup the parent handle */
	eError = GetHandleData(psBase, &psPHandleData, hParent, PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_GOTO_IF_FALSE(eError == PVRSRV_OK, "failed to get parent handle structure",
	                  Exit);

	eError = AllocHandle(psBase, &hHandle, pvData, eType, eFlag, hParentKey, NULL);
	PVR_GOTO_IF_ERROR(eError, Exit);

	eError = GetHandleData(psBase, &psCHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
	/* If we were able to allocate the handle then there should be no reason why we
	 * can't also get it's handle structure. Otherwise something has gone badly wrong.
	 */
	PVR_ASSERT(eError == PVRSRV_OK);
	PVR_LOG_GOTO_IF_FALSE(eError == PVRSRV_OK, "Failed to get parent handle structure",
	                  ExitFreeHandle);

	/*
	 * Get the parent handle structure again, in case the handle
	 * structure has moved (depending on the implementation
	 * of AllocHandle).
	 */
	eError = GetHandleData(psBase, &psPHandleData, hParent, PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_GOTO_IF_FALSE(eError == PVRSRV_OK, "failed to get parent handle structure",
	                  ExitFreeHandle);

	eError = AdoptChild(psBase, psPHandleData, psCHandleData);
	PVR_LOG_GOTO_IF_FALSE(eError == PVRSRV_OK, "parent handle failed to adopt subhandle",
	                  ExitFreeHandle);

	*phHandle = hHandle;

	return PVRSRV_OK;

ExitFreeHandle:
	PVRSRVDestroyHandleUnlocked(psBase, hHandle, eType);
Exit:
	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVFindHandle
 @Description   Find handle corresponding to a resource pointer
 @Input         pvData - pointer to resource to be associated with the handle
                eType - the type of resource
 @Output        phHandle - points to returned handle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVFindHandle(PVRSRV_HANDLE_BASE *psBase,
			      IMG_HANDLE *phHandle,
			      void *pvData,
			      PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	LockHandle(psBase);
	eError = PVRSRVFindHandleUnlocked(psBase, phHandle, pvData, eType);
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVFindHandleUnlocked
 @Description   Find handle corresponding to a resource pointer without
                acquiring/releasing the handle lock. The function assumes you
                hold the lock when called.
 @Input         pvData - pointer to resource to be associated with the handle
                eType - the type of resource
 @Output        phHandle - points to the returned handle
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVFindHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
			      IMG_HANDLE *phHandle,
			      void *pvData,
			      PVRSRV_HANDLE_TYPE eType)
{
	IMG_HANDLE hHandle;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");

	/* See if there is a handle for this data pointer */
	hHandle = FindHandle(psBase, pvData, eType, NULL);
	if (hHandle == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Error finding handle. Type %u",
			 __func__,
			 eType));

		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	}

	*phHandle = hHandle;

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      PVRSRVLookupHandle
 @Description   Lookup the data pointer corresponding to a handle
 @Input         hHandle - handle from client
                eType - handle type
                bRef - If TRUE, a reference will be added on the handle if the
                       lookup is successful.
 @Output        ppvData - points to the return data pointer
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE *psBase,
				void **ppvData,
				IMG_HANDLE hHandle,
				PVRSRV_HANDLE_TYPE eType,
				IMG_BOOL bRef)
{
	PVRSRV_ERROR eError;

	LockHandle(psBase);
	eError = PVRSRVLookupHandleUnlocked(psBase, ppvData, hHandle, eType, bRef);
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVLookupHandleUnlocked
 @Description   Lookup the data pointer corresponding to a handle without
                acquiring/releasing the handle lock. The function assumes you
                hold the lock when called.
 @Input         hHandle - handle from client
                eType - handle type
                bRef - If TRUE, a reference will be added on the handle if the
                       lookup is successful.
 @Output        ppvData - points to the returned data pointer
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVLookupHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
				void **ppvData,
				IMG_HANDLE hHandle,
				PVRSRV_HANDLE_TYPE eType,
				IMG_BOOL bRef)
{
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (unlikely(eError != PVRSRV_OK))
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Error looking up handle (%s) for base %p of type %s. Handle %p, type %s",
			 __func__,
			 PVRSRVGetErrorString(eError),
			 psBase,
			 HandleBaseTypeToString(psBase->eType),
			 (void*) hHandle,
			 HandleTypeToString(eType)));
#if defined(DEBUG) || defined(PVRSRV_NEED_PVR_DPF)
		OSDumpStack();
#endif
		return eError;
	}

	/* If bCanLookup is false it means that a destroy operation was already
	 * called on this handle; therefore it can no longer be looked up. */
	if (!psHandleData->bCanLookup)
	{
		return PVRSRV_ERROR_HANDLE_NOT_ALLOCATED;
	}

	if (bRef)
	{
		HandleGet(psHandleData);
	}

	*ppvData = psHandleData->pvData;

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      PVRSRVLookupSubHandle
 @Description   Lookup the data pointer corresponding to a subhandle
 @Input         hHandle - handle from client
                eType - handle type
                hAncestor - ancestor handle
 @Output        ppvData - points to the returned data pointer
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVLookupSubHandle(PVRSRV_HANDLE_BASE *psBase,
				   void **ppvData,
				   IMG_HANDLE hHandle,
				   PVRSRV_HANDLE_TYPE eType,
				   IMG_HANDLE hAncestor)
{
	HANDLE_DATA *psPHandleData = NULL;
	HANDLE_DATA *psCHandleData = NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");

	LockHandle(psBase);

	eError = GetHandleData(psBase, &psCHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "%s: Error looking up subhandle (%s). Handle %p, type %u",
			 __func__,
			 PVRSRVGetErrorString(eError),
			 (void*) hHandle,
			 eType));
		OSDumpStack();
		goto ExitUnlock;
	}

	/* Look for hAncestor among the handle's ancestors */
	for (psPHandleData = psCHandleData; ParentHandle(psPHandleData) != hAncestor; )
	{
		eError = GetHandleData(psBase, &psPHandleData, ParentHandle(psPHandleData), PVRSRV_HANDLE_TYPE_NONE);
		if (eError != PVRSRV_OK)
		{
			PVR_LOG_ERROR(eError, "GetHandleData");
			eError = PVRSRV_ERROR_INVALID_SUBHANDLE;
			goto ExitUnlock;
		}
	}

	*ppvData = psCHandleData->pvData;

	eError = PVRSRV_OK;

ExitUnlock:
	UnlockHandle(psBase);

	return eError;
}


/*!
*******************************************************************************
 @Function      PVRSRVReleaseHandle
 @Description   Release a handle that is no longer needed
 @Input         hHandle - handle from client
                eType - handle type
 @Return        Error code or PVRSRV_OK
******************************************************************************/
void PVRSRVReleaseHandle(PVRSRV_HANDLE_BASE *psBase,
                         IMG_HANDLE hHandle,
                         PVRSRV_HANDLE_TYPE eType)
{
	LockHandle(psBase);
	PVRSRVReleaseHandleUnlocked(psBase, hHandle, eType);
	UnlockHandle(psBase);
}


/*!
*******************************************************************************
 @Function      PVRSRVReleaseHandleUnlocked
 @Description   Release a handle that is no longer needed without
                acquiring/releasing the handle lock. The function assumes you
                hold the lock when called.
 @Input         hHandle - handle from client
                eType - handle type
******************************************************************************/
void PVRSRVReleaseHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
                                 IMG_HANDLE hHandle,
                                 PVRSRV_HANDLE_TYPE eType)
{
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(psBase != NULL);
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_VOID_IF_FALSE(psBase != NULL, "invalid psBase");

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (unlikely(eError != PVRSRV_OK))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Error (%s) looking up handle %p of type %s "
		        "for base %p of type %s.", __func__, PVRSRVGetErrorString(eError),
		        (void*) hHandle, HandleTypeToString(eType), psBase,
		        HandleBaseTypeToString(psBase->eType)));

		PVR_ASSERT(eError == PVRSRV_OK);

		return;
	}

	PVR_ASSERT(psHandleData->bCanLookup);
	PVR_ASSERT(psHandleData->iLookupCount > 0);

	/* If there are still outstanding lookups for this handle or the handle
	 * has not been destroyed yet, return early */
	HandlePut(psHandleData);
}

/*!
*******************************************************************************
 @Function      PVRSRVPurgeHandles
 @Description   Purge handles for a given handle base
 @Input         psBase - pointer to handle base structure
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVPurgeHandles(PVRSRV_HANDLE_BASE *psBase)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");

	LockHandle(psBase);
	eError = gpsHandleFuncs->pfnPurgeHandles(psBase->psImplBase);
	UnlockHandle(psBase);

	return eError;
}

static PVRSRV_ERROR HandleUnrefAndMaybeMarkForFreeWrapper(PVRSRV_HANDLE_BASE *psBase,
                                                          IMG_HANDLE hHandle)
{
	HANDLE_DATA *psHandleData;
	PVRSRV_ERROR eError = GetHandleData(psBase, &psHandleData, hHandle,
	                                    PVRSRV_HANDLE_TYPE_NONE);
	PVR_RETURN_IF_ERROR(eError);

	return HandleUnrefAndMaybeMarkForFree(psBase, psHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
}

static PVRSRV_ERROR HandleUnrefAndMaybeMarkForFree(PVRSRV_HANDLE_BASE *psBase,
                                                   HANDLE_DATA *psHandleData,
                                                   IMG_HANDLE hHandle,
                                                   PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	/* If bCanLookup is false it means that the destructor was called more than
	 * once on this handle. */
	if (!psHandleData->bCanLookup)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle %p of type %s already freed.",
		        __func__, psHandleData->hHandle,
		        HandleTypeToString(psHandleData->eType)));
		return PVRSRV_ERROR_HANDLE_NOT_FOUND;
	}

	if (psHandleData->iLookupCount > 0)
	{
		return PVRSRV_ERROR_OBJECT_STILL_REFERENCED;
	}

	/* Mark this handle as freed only if it's no longer referenced by any
	 * lookup. The user space should retry freeing this handle once there are
	 * no outstanding lookups. */
	psHandleData->bCanLookup = IMG_FALSE;

#ifdef DEBUG_REFCNT
	PVR_DPF((PVR_DBG_ERROR, "%s: bCanLookup = false, iLookupCount = %d", __func__,
	        psHandleData->iLookupCount));
#endif /* DEBUG_REFCNT */

	/* Prepare children for destruction */
	eError = IterateOverChildren(psBase, psHandleData,
	                             HandleUnrefAndMaybeMarkForFreeWrapper);
	PVR_LOG_RETURN_IF_ERROR(eError, "HandleUnrefAndMaybeMarkForFreeWrapper");

	return PVRSRV_OK;
}

static PVRSRV_ERROR HandleFreePrivDataWrapper(PVRSRV_HANDLE_BASE *psBase,
                                              IMG_HANDLE hHandle)
{
	HANDLE_DATA *psHandleData;
	PVRSRV_ERROR eError = GetHandleData(psBase, &psHandleData, hHandle,
	                                    PVRSRV_HANDLE_TYPE_NONE);
	PVR_RETURN_IF_ERROR(eError);

	return HandleFreePrivData(psBase, psHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
}

static PVRSRV_ERROR HandleFreePrivData(PVRSRV_HANDLE_BASE *psBase,
                                       HANDLE_DATA *psHandleData,
                                       IMG_HANDLE hHandle,
                                       PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	/* Call the release data callback for each reference on the handle */
	if (psHandleData->pfnReleaseData != NULL)
	{
		eError = psHandleData->pfnReleaseData(psHandleData->pvData);
		if (eError != PVRSRV_OK)
		{
			if (IsRetryError(eError))
			{
				PVR_DPF((PVR_DBG_MESSAGE, "%s: Got retry while calling release "
						"data callback for handle %p of type = %s", __func__,
						hHandle, HandleTypeToString(psHandleData->eType)));
			}
			else
			{
				PVR_LOG_ERROR(eError, "pfnReleaseData");
			}

			return eError;
		}

		/* we don't need this so make sure it's not called on
		 * the pvData for the second time
		 */
		psHandleData->pfnReleaseData = NULL;
	}

	/* Free children's data */
	eError = IterateOverChildren(psBase, psHandleData,
	                             HandleFreePrivDataWrapper);
	PVR_LOG_RETURN_IF_ERROR(eError, "IterateOverChildren->HandleFreePrivData");

	return PVRSRV_OK;
}

static PVRSRV_ERROR HandleFreeDestroyWrapper(PVRSRV_HANDLE_BASE *psBase,
                                             IMG_HANDLE hHandle)
{
	HANDLE_DATA *psHandleData;
	PVRSRV_ERROR eError = GetHandleData(psBase, &psHandleData, hHandle,
	                                    PVRSRV_HANDLE_TYPE_NONE);
	PVR_RETURN_IF_ERROR(eError);

	return HandleFreeDestroy(psBase, psHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
}

static PVRSRV_ERROR HandleFreeDestroy(PVRSRV_HANDLE_BASE *psBase,
                                      HANDLE_DATA *psHandleData,
                                      IMG_HANDLE hHandle,
                                      PVRSRV_HANDLE_TYPE eType)
{
	HANDLE_DATA *psReleasedHandleData;
	PVRSRV_ERROR eError;

	eError = UnlinkFromParent(psBase, psHandleData);
	PVR_LOG_RETURN_IF_ERROR(eError, "UnlinkFromParent");

	if (!TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		HAND_KEY aKey;
		IMG_HANDLE hRemovedHandle;

		InitKey(aKey, psBase, psHandleData->pvData, psHandleData->eType,
		        ParentIfPrivate(psHandleData));

		hRemovedHandle = (IMG_HANDLE) HASH_Remove_Extended(psBase->psHashTab,
		                                                   aKey);

		PVR_ASSERT(hRemovedHandle != NULL);
		PVR_ASSERT(hRemovedHandle == psHandleData->hHandle);
		PVR_UNREFERENCED_PARAMETER(hRemovedHandle);
	}

	/* Free children */
	eError = IterateOverChildren(psBase, psHandleData, HandleFreeDestroyWrapper);
	PVR_LOG_RETURN_IF_ERROR(eError, "IterateOverChildren->HandleFreeDestroy");

	eError = gpsHandleFuncs->pfnReleaseHandle(psBase->psImplBase,
	                                          psHandleData->hHandle,
	                                          (void **)&psReleasedHandleData);
	OSFreeMem(psHandleData);
	PVR_LOG_RETURN_IF_ERROR(eError, "pfnReleaseHandle");

	return PVRSRV_OK;
}

static PVRSRV_ERROR DestroyHandle(PVRSRV_HANDLE_BASE *psBase,
                                  IMG_HANDLE hHandle,
                                  PVRSRV_HANDLE_TYPE eType,
                                  IMG_BOOL bReleaseLock)
{
	PVRSRV_ERROR eError;
	HANDLE_DATA *psHandleData = NULL;

	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	PVR_RETURN_IF_ERROR(eError);

	eError = HandleUnrefAndMaybeMarkForFree(psBase, psHandleData, hHandle, eType);
	PVR_RETURN_IF_ERROR(eError);

	if (bReleaseLock)
	{
		UnlockHandle(psBase);
	}

	eError = HandleFreePrivData(psBase, psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		if (bReleaseLock)
		{
			LockHandle(psBase);
		}

		/* If the data could not be freed due to a temporary condition the
		 * handle must be kept alive so that the next destroy call can try again */
		if (IsRetryError(eError))
		{
			psHandleData->bCanLookup = IMG_TRUE;
		}

		return eError;
	}

	if (bReleaseLock)
	{
		LockHandle(psBase);
	}

	return HandleFreeDestroy(psBase, psHandleData, hHandle, eType);
}

/*!
*******************************************************************************
 @Function      PVRSRVDestroyHandle
 @Description   Destroys a handle that is no longer needed. Will
                acquiring the handle lock for duration of the call.
                Can return RETRY or KERNEL_CCB_FULL if resource could not be
                destroyed, caller should retry sometime later.
 @Input         psBase - pointer to handle base structure
                hHandle - handle from client
                eType - handle type
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVDestroyHandle(PVRSRV_HANDLE_BASE *psBase,
                                 IMG_HANDLE hHandle,
                                 PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	LockHandle(psBase);
	eError = DestroyHandle(psBase, hHandle, eType, IMG_FALSE);
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVDestroyHandleUnlocked
 @Description   Destroys a handle that is no longer needed without
                acquiring/releasing the handle lock. The function assumes you
                hold the lock when called.
                Can return RETRY or KERNEL_CCB_FULL if resource could not be
                destroyed, caller should retry sometime later.
 @Input         psBase - pointer to handle base structure
                hHandle - handle from client
                eType - handle type
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVDestroyHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
                                         IMG_HANDLE hHandle,
                                         PVRSRV_HANDLE_TYPE eType)
{
	return DestroyHandle(psBase, hHandle, eType, IMG_FALSE);
}

/*!
*******************************************************************************
 @Function      PVRSRVDestroyHandleStagedUnlocked
 @Description   Destroys a handle that is no longer needed without
                acquiring/releasing the handle lock. The function assumes you
                hold the lock when called. This function, unlike
                PVRSRVDestroyHandleUnlocked(), releases the handle lock while
                destroying handle private data. This is done to open the
                bridge for other bridge calls.
                Can return RETRY or KERNEL_CCB_FULL if resource could not be
                destroyed, caller should retry sometime later.
 @Input         psBase - pointer to handle base structure
                hHandle - handle from client
                eType - handle type
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVDestroyHandleStagedUnlocked(PVRSRV_HANDLE_BASE *psBase,
                                               IMG_HANDLE hHandle,
                                               PVRSRV_HANDLE_TYPE eType)
{
	return DestroyHandle(psBase, hHandle, eType, IMG_TRUE);
}

/*!
*******************************************************************************
 @Function      PVRSRVAllocHandleBase
 @Description   Allocate a handle base structure for a process
 @Input         eType - handle type
 @Output        ppsBase - points to handle base structure pointer
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandleBase(PVRSRV_HANDLE_BASE **ppsBase,
                                   PVRSRV_HANDLE_BASE_TYPE eType)
{
	PVRSRV_HANDLE_BASE *psBase;
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_FALSE(gpsHandleFuncs != NULL, "handle management not initialised",
	                  PVRSRV_ERROR_NOT_READY);
	PVR_LOG_RETURN_IF_INVALID_PARAM(ppsBase != NULL, "ppsBase");

	psBase = OSAllocZMem(sizeof(*psBase));
	PVR_LOG_RETURN_IF_NOMEM(psBase, "psBase");

	eError = OSLockCreate(&psBase->hLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", ErrorFreeHandleBase);

	psBase->eType = eType;

	LockHandle(psBase);

	eError = gpsHandleFuncs->pfnCreateHandleBase(&psBase->psImplBase);
	PVR_GOTO_IF_ERROR(eError, ErrorUnlock);

	psBase->psHashTab = HASH_Create_Extended(HANDLE_HASH_TAB_INIT_SIZE,
						 sizeof(HAND_KEY),
						 HASH_Func_Default,
						 HASH_Key_Comp_Default);
	PVR_LOG_GOTO_IF_FALSE(psBase->psHashTab != NULL, "couldn't create data pointer"
	                  " hash table", ErrorDestroyHandleBase);

	*ppsBase = psBase;

	UnlockHandle(psBase);

	return PVRSRV_OK;

ErrorDestroyHandleBase:
	(void)gpsHandleFuncs->pfnDestroyHandleBase(psBase->psImplBase);

ErrorUnlock:
	UnlockHandle(psBase);
	OSLockDestroy(psBase->hLock);

ErrorFreeHandleBase:
	OSFreeMem(psBase);

	return eError;
}

#if defined(DEBUG)
typedef struct _COUNT_HANDLE_DATA_
{
	PVRSRV_HANDLE_BASE *psBase;
	IMG_UINT32 uiHandleDataCount;
} COUNT_HANDLE_DATA;

/* Used to count the number of handles that have data associated with them */
static PVRSRV_ERROR CountHandleDataWrapper(IMG_HANDLE hHandle, void *pvData)
{
	COUNT_HANDLE_DATA *psData = (COUNT_HANDLE_DATA *)pvData;
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psData != NULL, "psData");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psData->psBase != NULL, "psData->psBase");

	eError = GetHandleData(psData->psBase,
			       &psHandleData,
			       hHandle,
			       PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_RETURN_IF_ERROR(eError, "GetHandleData");

	if (psHandleData != NULL)
	{
		psData->uiHandleDataCount++;
	}

	return PVRSRV_OK;
}

/* Print a handle in the handle base. Used with the iterator callback. */
static PVRSRV_ERROR ListHandlesInBase(IMG_HANDLE hHandle, void *pvData)
{
	PVRSRV_HANDLE_BASE *psBase = (PVRSRV_HANDLE_BASE*) pvData;
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psBase != NULL, "psBase");

	eError = GetHandleData(psBase,
			       &psHandleData,
			       hHandle,
			       PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_RETURN_IF_ERROR(eError, "GetHandleData");

	if (psHandleData != NULL)
	{
		PVR_DPF((PVR_DBG_WARNING,
		        "    Handle: %6u, CanLookup: %u, LookupCount: %3u, Type: %s (%u), pvData<%p>",
		       (IMG_UINT32) (uintptr_t) psHandleData->hHandle, psHandleData->bCanLookup,
		       psHandleData->iLookupCount, HandleTypeToString(psHandleData->eType),
		       psHandleData->eType, psHandleData->pvData));
	}

	return PVRSRV_OK;
}

#endif /* defined(DEBUG) */

static INLINE IMG_BOOL _CheckIfMaxTimeExpired(IMG_UINT64 ui64TimeStart, IMG_UINT64 ui64MaxBridgeTime)
{
	/* unsigned arithmetic is well defined so this will wrap around correctly */
	return (IMG_BOOL)((OSClockns64() - ui64TimeStart) >= ui64MaxBridgeTime);
}

static PVRSRV_ERROR FreeKernelHandlesWrapperIterKernel(IMG_HANDLE hHandle, void *pvData)
{
	FREE_KERNEL_HANDLE_DATA *psData = (FREE_KERNEL_HANDLE_DATA *)pvData;
	HANDLE_DATA *psKernelHandleData = NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	/* Get kernel handle data. */
	eError = GetHandleData(KERNEL_HANDLE_BASE,
			    &psKernelHandleData,
			    hHandle,
			    PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_RETURN_IF_ERROR(eError, "GetHandleData");

	if (psKernelHandleData->pvData == psData->psProcessHandleData->pvData)
	{
		/* This kernel handle belongs to our process handle. */
		psData->hKernelHandle = hHandle;
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR FreeKernelHandlesWrapperIterProcess(IMG_HANDLE hHandle, void *pvData)
{
	FREE_KERNEL_HANDLE_DATA *psData = (FREE_KERNEL_HANDLE_DATA *)pvData;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	/* Get process handle data. */
	eError = GetHandleData(psData->psBase,
			    &psData->psProcessHandleData,
			    hHandle,
			    PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_RETURN_IF_ERROR(eError, "GetHandleData");

	if (psData->psProcessHandleData->eFlag == PVRSRV_HANDLE_ALLOC_FLAG_MULTI
#if defined(SUPPORT_INSECURE_EXPORT)
		|| psData->psProcessHandleData->eType == PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_EXPORT
#endif
		)
	{
		/* Only multi alloc process handles might be in kernel handle base. */
		psData->hKernelHandle = NULL;
		/* Iterate over kernel handles. */
		eError = gpsHandleFuncs->pfnIterateOverHandles(KERNEL_HANDLE_BASE->psImplBase,
									&FreeKernelHandlesWrapperIterKernel,
									(void *)psData);
		PVR_LOG_RETURN_IF_FALSE(eError == PVRSRV_OK, "failed to iterate over kernel handles",
		                  eError);

		if (psData->hKernelHandle)
		{
			/* Release kernel handle which belongs to our process handle. */
			eError = gpsHandleFuncs->pfnReleaseHandle(KERNEL_HANDLE_BASE->psImplBase,
						psData->hKernelHandle,
						NULL);
			PVR_LOG_RETURN_IF_FALSE(eError == PVRSRV_OK, "couldn't release kernel handle",
			                  eError);
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR FreeHandleDataWrapper(IMG_HANDLE hHandle, void *pvData)
{
	FREE_HANDLE_DATA *psData = (FREE_HANDLE_DATA *)pvData;
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	PVR_LOG_RETURN_IF_INVALID_PARAM(psData != NULL, "psData");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psData->psBase != NULL, "psData->psBase");
	PVR_LOG_RETURN_IF_INVALID_PARAM(psData->eHandleFreeType != PVRSRV_HANDLE_TYPE_NONE,
	                          "psData->eHandleFreeType");

	eError = GetHandleData(psData->psBase,
			       &psHandleData,
			       hHandle,
			       PVRSRV_HANDLE_TYPE_NONE);
	PVR_LOG_RETURN_IF_ERROR(eError, "GetHandleData");

	if (psHandleData == NULL || psHandleData->eType != psData->eHandleFreeType)
	{
		return PVRSRV_OK;
	}

	PVR_ASSERT(psHandleData->bCanLookup && psHandleData->iLookupCount == 0);

	if (psHandleData->bCanLookup)
	{
		if (psHandleData->pfnReleaseData != NULL)
		{
			eError = psHandleData->pfnReleaseData(psHandleData->pvData);
			if (eError == PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_MESSAGE, "%s: Got retry while calling release "
				        "data callback for handle %p of type = %s", __func__,
				        hHandle, HandleTypeToString(psHandleData->eType)));

				return eError;
			}
			else if (eError != PVRSRV_OK)
			{
				return eError;
			}
		}

		psHandleData->bCanLookup = IMG_FALSE;
	}

	if (!TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		HAND_KEY aKey;
		IMG_HANDLE hRemovedHandle;

		InitKey(aKey,
			psData->psBase,
			psHandleData->pvData,
			psHandleData->eType,
			ParentIfPrivate(psHandleData));

		hRemovedHandle = (IMG_HANDLE)HASH_Remove_Extended(psData->psBase->psHashTab, aKey);

		PVR_ASSERT(hRemovedHandle != NULL);
		PVR_ASSERT(hRemovedHandle == psHandleData->hHandle);
		PVR_UNREFERENCED_PARAMETER(hRemovedHandle);
	}

	eError = gpsHandleFuncs->pfnSetHandleData(psData->psBase->psImplBase, hHandle, NULL);
	PVR_RETURN_IF_ERROR(eError);

	OSFreeMem(psHandleData);

	/* If we reach the end of the time slice release we can release the global
	 * lock, invoke the scheduler and reacquire the lock */
	if ((psData->ui64MaxBridgeTime != 0) && _CheckIfMaxTimeExpired(psData->ui64TimeStart, psData->ui64MaxBridgeTime))
	{
		PVR_DPF((PVR_DBG_MESSAGE,
			 "%s: Lock timeout (timeout: %" IMG_UINT64_FMTSPEC")",
			 __func__,
			 psData->ui64MaxBridgeTime));
		UnlockHandle(psData->psBase);
		/* Invoke the scheduler to check if other processes are waiting for the lock */
		OSReleaseThreadQuanta();
		LockHandle(psData->psBase);
		/* Set again lock timeout and reset the counter */
		psData->ui64TimeStart = OSClockns64();
		PVR_DPF((PVR_DBG_MESSAGE, "%s: Lock acquired again", __func__));
	}

	return PVRSRV_OK;
}

/* The Ordered Array of PVRSRV_HANDLE_TYPE Enum Entries.
 *
 *   Some handles must be destroyed prior to other handles,
 *   such relationships are established with respect to handle types.
 *   Therefore elements of this array have to maintain specific order,
 *   e.g. the PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET must be placed
 *   before PVRSRV_HANDLE_TYPE_RGX_FREELIST.
 *
 *   If ordering is incorrect driver may fail on the ground of cleanup
 *   routines. Unfortunately, we can mainly rely on the actual definition of
 *   the array, there is no explicit information about all relationships
 *   between handle types. These relationships do not necessarily come from
 *   bridge-specified handle attributes such as 'sub handle' and 'parent
 *   handle'. They may come from internal/private ref-counters contained by
 *   objects referenced by our kernel handles.
 *
 *   For example, at the bridge level, PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET
 *   and PVRSRV_HANDLE_TYPE_RGX_FREELIST have no explicit relationship, meaning
 *   none of them is a sub-handle for the other.
 *   However the freelist contains internal ref-count that is decremented by
 *   the destroy routine for KM_HW_RT_DATASET.
 *
 *   BE CAREFUL when adding/deleting/moving handle types.
 */
static const PVRSRV_HANDLE_TYPE g_aeOrderedFreeList[] =
{
	PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
	PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
	PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
	PVRSRV_HANDLE_TYPE_RGX_KM_HW_RT_DATASET,
	PVRSRV_HANDLE_TYPE_RGX_FREELIST,
	PVRSRV_HANDLE_TYPE_RGX_MEMORY_BLOCK,
	PVRSRV_HANDLE_TYPE_RGX_POPULATION,
	PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT,
#if defined(PVR_TESTING_UTILS) && defined(SUPPORT_VALIDATION)
	PVRSRV_HANDLE_TYPE_RGX_SERVER_GPUMAP_CONTEXT,
#endif
	PVRSRV_HANDLE_TYPE_RI_HANDLE,
	PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE,
	PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
	PVRSRV_HANDLE_TYPE_PVRSRV_TIMELINE_SERVER,
	PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_EXPORT,
	PVRSRV_HANDLE_TYPE_PVRSRV_FENCE_SERVER,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT,
	PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_PAGELIST,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_SECURE_EXPORT,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
	PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT,
	PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
	PVRSRV_HANDLE_TYPE_DC_PIN_HANDLE,
	PVRSRV_HANDLE_TYPE_DC_BUFFER,
	PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT,
	PVRSRV_HANDLE_TYPE_DC_DEVICE,
	PVRSRV_HANDLE_TYPE_PVR_TL_SD,
	PVRSRV_HANDLE_TYPE_DI_CONTEXT,
	PVRSRV_HANDLE_TYPE_MM_PLAT_CLEANUP
};

/*!
*******************************************************************************
 @Function      PVRSRVFreeKernelHandles
 @Description   Free kernel handles which belongs to process handles
 @Input         psBase - pointer to handle base structure
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVFreeKernelHandles(PVRSRV_HANDLE_BASE *psBase)
{
	FREE_KERNEL_HANDLE_DATA sHandleData = {NULL};
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	LockHandle(psBase);

	sHandleData.psBase = psBase;
	/* Iterate over process handles. */
	eError = gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
								&FreeKernelHandlesWrapperIterProcess,
								(void *)&sHandleData);
	PVR_LOG_GOTO_IF_ERROR(eError, "pfnIterateOverHandles", ExitUnlock);

	eError = PVRSRV_OK;

ExitUnlock:
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVRetrieveProcessHandleBase
 @Description   Returns a pointer to the process handle base for the current
                process. If the current process is the cleanup thread, then the
                process handle base for the process currently being cleaned up
                is returned
 @Return        Pointer to the process handle base, or NULL if not found.
******************************************************************************/
PVRSRV_HANDLE_BASE *PVRSRVRetrieveProcessHandleBase(void)
{
	PVRSRV_HANDLE_BASE *psHandleBase = NULL;
	PROCESS_HANDLE_BASE *psProcHandleBase = NULL;
	IMG_PID ui32PurgePid = PVRSRVGetPurgeConnectionPid();
	IMG_PID uiCleanupPid = PVRSRVCleanupThreadGetPid();
	uintptr_t uiCleanupTid = PVRSRVCleanupThreadGetTid();

	OSLockAcquire(g_hProcessHandleBaseLock);

	/* Check to see if we're being called from the cleanup thread... */
	if ((OSGetCurrentProcessID() == uiCleanupPid) &&
	    (OSGetCurrentThreadID() == uiCleanupTid) &&
	    (ui32PurgePid > 0))
	{
		/* Check to see if the cleanup thread has already removed the
		 * process handle base from the HASH table.
		 */
		psHandleBase = g_psProcessHandleBaseBeingFreed;
		/* psHandleBase shouldn't be null, as cleanup thread
		 * should be removing this from the HASH table before
		 * we get here, so assert if not.
		 */
		PVR_ASSERT(psHandleBase);
	}
	else
	{
		/* Not being called from the cleanup thread, so return the process
		 * handle base for the current process.
		 */
		psProcHandleBase = (PROCESS_HANDLE_BASE *)
		    HASH_Retrieve(g_psProcessHandleBaseTable, OSGetCurrentClientProcessIDKM());
	}

	OSLockRelease(g_hProcessHandleBaseLock);

	if (psHandleBase == NULL && psProcHandleBase != NULL)
	{
		psHandleBase = psProcHandleBase->psHandleBase;
	}
	return psHandleBase;
}

/*!
*******************************************************************************
 @Function      PVRSRVAcquireProcessHandleBase
 @Description   Increments reference count on a process handle base identified
                by uiPid and returns pointer to the base. If the handle base
                does not exist it will be allocated.
 @Inout         uiPid - PID of a process
 @Output        ppsBase - pointer to a handle base for the process identified by
                          uiPid
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVAcquireProcessHandleBase(IMG_PID uiPid, PROCESS_HANDLE_BASE **ppsBase)
{
	PROCESS_HANDLE_BASE *psBase;
	PVRSRV_ERROR eError;

	OSLockAcquire(g_hProcessHandleBaseLock);

	psBase = (PROCESS_HANDLE_BASE*) HASH_Retrieve(g_psProcessHandleBaseTable, uiPid);

	/* In case there is none we are going to allocate one */
	if (psBase == NULL)
	{
		IMG_BOOL bSuccess;

		psBase = OSAllocZMem(sizeof(*psBase));
		PVR_LOG_GOTO_IF_NOMEM(psBase, eError, ErrorUnlock);

		/* Allocate handle base for this process */
		eError = PVRSRVAllocHandleBase(&psBase->psHandleBase, PVRSRV_HANDLE_BASE_TYPE_PROCESS);
		PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVAllocHandleBase", ErrorFreeProcessHandleBase);

		/* Insert the handle base into the global hash table */
		bSuccess = HASH_Insert(g_psProcessHandleBaseTable, uiPid, (uintptr_t) psBase);
		PVR_LOG_GOTO_IF_FALSE(bSuccess, "HASH_Insert failed", ErrorFreeHandleBase);
	}

	OSAtomicIncrement(&psBase->iRefCount);

	OSLockRelease(g_hProcessHandleBaseLock);

	*ppsBase = psBase;

	return PVRSRV_OK;

ErrorFreeHandleBase:
	PVRSRVFreeHandleBase(psBase->psHandleBase, 0);
ErrorFreeProcessHandleBase:
	OSFreeMem(psBase);
ErrorUnlock:
	OSLockRelease(g_hProcessHandleBaseLock);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVReleaseProcessHandleBase
 @Description   Decrements reference count on a process handle base psBase
                for a process identified by uiPid. If the reference count
                reaches 0 the handle base will be freed..
 @Input         psBase - pointer to a process handle base
 @Inout         uiPid - PID of a process
 @Inout         ui64MaxBridgeTime - maximum time a handle destroy operation
                                    can hold the handle base lock (after that
                                    time a lock will be release and reacquired
                                    for another time slice)
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVReleaseProcessHandleBase(PROCESS_HANDLE_BASE *psBase, IMG_PID uiPid,
                                            IMG_UINT64 ui64MaxBridgeTime)
{
	PVRSRV_ERROR eError;
	IMG_INT iRefCount;
	uintptr_t uiHashValue;

	OSLockAcquire(g_hProcessHandleBaseLock);

	iRefCount = OSAtomicDecrement(&psBase->iRefCount);

	if (iRefCount != 0)
	{
		OSLockRelease(g_hProcessHandleBaseLock);
		return PVRSRV_OK;
	}

	/* in case the refcount becomes 0 we can remove the process handle base
	 * and all related objects */

	uiHashValue = HASH_Remove(g_psProcessHandleBaseTable, uiPid);
	OSLockRelease(g_hProcessHandleBaseLock);

	PVR_LOG_RETURN_IF_FALSE(uiHashValue != 0, "HASH_Remove failed",
	                        PVRSRV_ERROR_UNABLE_TO_REMOVE_HASH_VALUE);

	eError = PVRSRVFreeKernelHandles(psBase->psHandleBase);
	PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVFreeKernelHandles");

	eError = PVRSRVFreeHandleBase(psBase->psHandleBase, ui64MaxBridgeTime);
	PVR_LOG_RETURN_IF_ERROR(eError, "PVRSRVFreeHandleBase");

	OSFreeMem(psBase);

	return PVRSRV_OK;
}

/*!
*******************************************************************************
 @Function      PVRSRVFreeHandleBase
 @Description   Free a handle base structure
 @Input         psBase - pointer to handle base structure
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVFreeHandleBase(PVRSRV_HANDLE_BASE *psBase, IMG_UINT64 ui64MaxBridgeTime)
{
#if defined(DEBUG)
	COUNT_HANDLE_DATA sCountData = {NULL};
#endif
	FREE_HANDLE_DATA sHandleData = {NULL};
	IMG_UINT32 i;
	PVRSRV_ERROR eError;
	IMG_PID uiCleanupPid = PVRSRVCleanupThreadGetPid();
	uintptr_t uiCleanupTid = PVRSRVCleanupThreadGetTid();

	PVR_ASSERT(gpsHandleFuncs);

	LockHandle(psBase);

	/* If this is a process handle base being freed by the cleanup
	 * thread, store this in g_psProcessHandleBaseBeingFreed
	 */
	if ((OSGetCurrentProcessID() == uiCleanupPid) &&
	    (OSGetCurrentThreadID() == uiCleanupTid) &&
	    (psBase->eType == PVRSRV_HANDLE_BASE_TYPE_PROCESS))
	{
		g_psProcessHandleBaseBeingFreed = psBase;
	}

	sHandleData.psBase = psBase;
	sHandleData.ui64TimeStart = OSClockns64();
	sHandleData.ui64MaxBridgeTime = ui64MaxBridgeTime;


#if defined(DEBUG)

	sCountData.psBase = psBase;

	eError = gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
						       &CountHandleDataWrapper,
						       (void *)&sCountData);
	PVR_LOG_GOTO_IF_ERROR(eError, "pfnIterateOverHandles", ExitUnlock);

	if (sCountData.uiHandleDataCount != 0)
	{
		IMG_BOOL bList = (IMG_BOOL)(sCountData.uiHandleDataCount < HANDLE_DEBUG_LISTING_MAX_NUM);

		PVR_DPF((PVR_DBG_WARNING,
			 "%s: %u remaining handles in handle base 0x%p "
			 "(PVRSRV_HANDLE_BASE_TYPE %u).%s",
			 __func__,
			 sCountData.uiHandleDataCount,
			 psBase,
			 psBase->eType,
			 bList ? "": " Skipping details, too many items..."));

		if (bList)
		{
			PVR_DPF((PVR_DBG_WARNING, "-------- Listing Handles --------"));
			(void) gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
			                                             &ListHandlesInBase,
			                                             psBase);
			PVR_DPF((PVR_DBG_WARNING, "-------- Done Listing    --------"));
		}
	}

#endif /* defined(DEBUG) */

	/*
	 * As we're freeing handles based on type, make sure all
	 * handles have actually had their data freed to avoid
	 * resources being leaked
	 */
	for (i = 0; i < ARRAY_SIZE(g_aeOrderedFreeList); i++)
	{
		sHandleData.eHandleFreeType = g_aeOrderedFreeList[i];

		/* Make sure all handles have been freed before destroying the handle base */
		eError = gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
							       &FreeHandleDataWrapper,
							       (void *)&sHandleData);
		PVR_GOTO_IF_ERROR(eError, ExitUnlock);
	}


	if (psBase->psHashTab != NULL)
	{
		HASH_Delete(psBase->psHashTab);
	}

	eError = gpsHandleFuncs->pfnDestroyHandleBase(psBase->psImplBase);
	PVR_GOTO_IF_ERROR(eError, ExitUnlock);

	UnlockHandle(psBase);
	OSLockDestroy(psBase->hLock);
	OSFreeMem(psBase);

	return eError;

ExitUnlock:
	if ((OSGetCurrentProcessID() == uiCleanupPid) &&
		(OSGetCurrentThreadID() == uiCleanupTid))
	{
		g_psProcessHandleBaseBeingFreed = NULL;
	}
	UnlockHandle(psBase);

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVHandleInit
 @Description   Initialise handle management
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVHandleInit(void)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsKernelHandleBase == NULL);
	PVR_ASSERT(gpsHandleFuncs == NULL);
	PVR_ASSERT(g_hProcessHandleBaseLock == NULL);
	PVR_ASSERT(g_psProcessHandleBaseTable == NULL);
	PVR_ASSERT(!gbLockInitialised);

	eError = OSLockCreate(&gKernelHandleLock);
	PVR_LOG_RETURN_IF_ERROR(eError, "OSLockCreate:1");

	eError = OSLockCreate(&g_hProcessHandleBaseLock);
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate:2", ErrorHandleDeinit);

	gbLockInitialised = IMG_TRUE;

	eError = PVRSRVHandleGetFuncTable(&gpsHandleFuncs);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVHandleGetFuncTable", ErrorHandleDeinit);

	eError = PVRSRVAllocHandleBase(&gpsKernelHandleBase,
	                               PVRSRV_HANDLE_BASE_TYPE_GLOBAL);
	PVR_LOG_GOTO_IF_ERROR(eError, "PVRSRVAllocHandleBase", ErrorHandleDeinit);

	g_psProcessHandleBaseTable = HASH_Create(HANDLE_PROC_HANDLE_HASH_INIT_SIZE);
	PVR_LOG_GOTO_IF_NOMEM(g_psProcessHandleBaseTable, eError, ErrorHandleDeinit);

	eError = gpsHandleFuncs->pfnEnableHandlePurging(gpsKernelHandleBase->psImplBase);
	PVR_LOG_GOTO_IF_ERROR(eError, "pfnEnableHandlePurging", ErrorHandleDeinit);

	return PVRSRV_OK;

ErrorHandleDeinit:
	(void) PVRSRVHandleDeInit();

	return eError;
}

/*!
*******************************************************************************
 @Function      PVRSRVHandleDeInit
 @Description   De-initialise handle management
 @Return        Error code or PVRSRV_OK
******************************************************************************/
PVRSRV_ERROR PVRSRVHandleDeInit(void)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (gpsHandleFuncs != NULL)
	{
		if (gpsKernelHandleBase != NULL)
		{
			eError = PVRSRVFreeHandleBase(gpsKernelHandleBase, 0 /* do not release bridge lock */);
			if (eError == PVRSRV_OK)
			{
				gpsKernelHandleBase = NULL;
			}
			else
			{
				PVR_LOG_ERROR(eError, "PVRSRVFreeHandleBase");
			}
		}

		if (eError == PVRSRV_OK)
		{
			gpsHandleFuncs = NULL;
		}
	}
	else
	{
		/* If we don't have a handle function table we shouldn't have a handle base either */
		PVR_ASSERT(gpsKernelHandleBase == NULL);
	}

	if (g_psProcessHandleBaseTable != NULL)
	{
		HASH_Delete(g_psProcessHandleBaseTable);
		g_psProcessHandleBaseTable = NULL;
	}

	if (g_hProcessHandleBaseLock != NULL)
	{
		OSLockDestroy(g_hProcessHandleBaseLock);
		g_hProcessHandleBaseLock = NULL;
	}

	if (gKernelHandleLock != NULL)
	{
		OSLockDestroy(gKernelHandleLock);
		gbLockInitialised = IMG_FALSE;
	}

	return eError;
}
