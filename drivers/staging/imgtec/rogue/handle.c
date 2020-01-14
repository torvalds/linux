/*************************************************************************/ /*!
@File
@Title		Resource Handle Manager
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Provide resource handle management
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
 * The implmentation supports movable handle structures, allowing the address
 * of a handle structure to change without having to fix up pointers in
 * any of the handle structures.  For example, the linked list mechanism
 * used to link subhandles together uses handle array indices rather than
 * pointers to the structures themselves.
 */

#include <stddef.h>

#include "osfunc.h"
#include "handle.h"
#include "handle_impl.h"
#include "allocmem.h"
#include "pvr_debug.h"
#include "connection_server.h"

#define	HANDLE_HASH_TAB_INIT_SIZE		32

#define	SET_FLAG(v, f)				((void)((v) |= (f)))
#define	CLEAR_FLAG(v, f)			((void)((v) &= (IMG_UINT)~(f)))
#define	TEST_FLAG(v, f)				((IMG_BOOL)(((v) & (f)) != 0))

#define	TEST_ALLOC_FLAG(psHandleData, f)	TEST_FLAG((psHandleData)->eFlag, f)

#if !defined(ARRAY_SIZE)
#define ARRAY_SIZE(a)				(sizeof(a) / sizeof((a)[0]))
#endif


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

	/* Reference count. The pfnReleaseData callback gets called when the
	 * reference count hits zero
	 */
	IMG_UINT32 ui32RefCount;
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

/* Stores a pointer to the function table of the handle back-end in use */
static HANDLE_IMPL_FUNCTAB const *gpsHandleFuncs = NULL;

/* 
 * Global lock added to avoid to call the handling functions
 * only in a single threaded context.
 */
static POS_LOCK gHandleLock;
static IMG_BOOL gbLockInitialised = IMG_FALSE;

void LockHandle(void)
{
	OSLockAcquire(gHandleLock);
}

void UnlockHandle(void)
{
	OSLockRelease(gHandleLock);
}

/*
 * Kernel handle base structure. This is used for handles that are not 
 * allocated on behalf of a particular process.
 */
PVRSRV_HANDLE_BASE *gpsKernelHandleBase = NULL;

/* Increase the reference count on the given handle.
 * The handle lock must already be acquired.
 * Returns: the reference count after the increment
 */
static inline IMG_UINT32 _HandleRef(HANDLE_DATA *psHandleData)
{
#if defined PVRSRV_DEBUG_HANDLE_LOCK
	if(!OSLockIsLocked(gHandleLock))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle lock is not locked", __func__));
		OSDumpStack();
	}
#endif
	psHandleData->ui32RefCount++;
	return psHandleData->ui32RefCount;
}

/* Decrease the reference count on the given handle.
 * The handle lock must already be acquired.
 * Returns: the reference count after the decrement
 */
static inline IMG_UINT32 _HandleUnref(HANDLE_DATA *psHandleData)
{
#if defined PVRSRV_DEBUG_HANDLE_LOCK
	if(!OSLockIsLocked(gHandleLock))
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle lock is not locked", __func__));
		OSDumpStack();
	}
#endif
	PVR_ASSERT(psHandleData->ui32RefCount > 0);
	psHandleData->ui32RefCount--;

	return psHandleData->ui32RefCount;
}

/*!
******************************************************************************

 @Function	GetHandleData

 @Description	Get the handle data structure for a given handle

 @Input		psBase - pointer to handle base structure
		ppsHandleData - location to return pointer to handle data structure
		hHandle - handle from client
		eType - handle type or PVRSRV_HANDLE_TYPE_NONE if the
			handle type is not to be checked.

 @Output	ppsHandleData - points to a pointer to the handle data structure

 @Return	Error code or PVRSRV_OK

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
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	/*
	 * Unless PVRSRV_HANDLE_TYPE_NONE was passed in to this function,
	 * check handle is of the correct type.
	 */
	if (eType != PVRSRV_HANDLE_TYPE_NONE && eType != psHandleData->eType)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "GetHandleData: Handle type mismatch (%d != %d)",
			 eType, psHandleData->eType));
		return PVRSRV_ERROR_HANDLE_TYPE_MISMATCH;
	}

	/* Return the handle structure */
	*ppsHandleData = psHandleData;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	HandleListInit

 @Description	Initialise a linked list structure embedded in a handle
		structure.

 @Input		hHandle - handle containing the linked list structure
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
******************************************************************************

 @Function	InitParentList

 @Description	Initialise the children list head in a handle structure.
		The children are the subhandles of this handle.

 @Input		psHandleData - pointer to handle data structure

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
******************************************************************************

 @Function	InitChildEntry

 @Description	Initialise the child list entry in a handle structure.
		The list entry is used to link together subhandles of
		a given handle.

 @Input		psHandleData - pointer to handle data structure

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
******************************************************************************

 @Function	HandleListIsEmpty

 @Description	Determine whether a given linked list is empty.

 @Input		hHandle - handle containing the list head
		psList - pointer to the list head

 @Return	IMG_TRUE if the list is empty, IMG_FALSE if it isn't.

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListIsEmpty)
#endif
static INLINE
IMG_BOOL HandleListIsEmpty(IMG_HANDLE hHandle, HANDLE_LIST *psList) /* Instead of passing in the handle can we not just do (psList->hPrev == psList->hNext) ? IMG_TRUE : IMG_FALSE ??? */
{
	IMG_BOOL bIsEmpty;

	bIsEmpty = (IMG_BOOL)(psList->hNext == hHandle);

#ifdef	DEBUG
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
******************************************************************************

 @Function	NoChildren

 @Description	Determine whether a handle has any subhandles

 @Input		psHandleData - pointer to handle data structure

 @Return	IMG_TRUE if the handle has no subhandles, IMG_FALSE if it does.

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
******************************************************************************

 @Function	NoParent

 @Description	Determine whether a handle is a subhandle

 @Input		psHandleData - pointer to handle data structure

 @Return	IMG_TRUE if the handle is not a subhandle, IMG_FALSE if it is.

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
	else
	{
		PVR_ASSERT(psHandleData->sSiblings.hParent != NULL);
	}
	return IMG_FALSE;
}
#endif /*DEBUG*/

/*!
******************************************************************************

 @Function	ParentHandle

 @Description	Determine the parent of a handle

 @Input		psHandleData - pointer to handle data structure

 @Return	Parent handle, or NULL if the handle is not a subhandle.

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
 * the list.  The two linked list structures are differentiated by
 * the third parameter, containing the parent handle.  The parent field
 * in the list head structure references the handle structure that contains
 * it.  For items on the list, the parent field in the linked list structure
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
	PVRSRV_ERROR eError;

	PVR_ASSERT(psBase != NULL);

	eError = GetHandleData(psBase, 
			       &psHandleData, 
			       hEntry, 
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		return NULL;
	}

	if (hEntry == hParent)
	{
		return (HANDLE_LIST *)((IMG_CHAR *)psHandleData + uiParentOffset);
	}
	else
	{
		return (HANDLE_LIST *)((IMG_CHAR *)psHandleData + uiEntryOffset);
	}
}

/*!
******************************************************************************

 @Function	HandleListInsertBefore

 @Description	Insert a handle before a handle currently on the list.

 @Input		hEntry - handle to be inserted after
		psEntry - pointer to handle structure to be inserted after
		uiParentOffset - offset to list head struct in handle structure
		hNewEntry - handle to be inserted
		psNewEntry - pointer to handle structure of item to be inserted
		uiEntryOffset - offset of list item struct in handle structure
		hParent - parent handle of hNewEntry

 @Return	Error code or PVRSRV_OK

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

	if (psBase == NULL || psEntry == NULL || psNewEntry == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

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
******************************************************************************

 @Function	AdoptChild

 @Description	Assign a subhandle to a handle

 @Input		psParentData - pointer to handle structure of parent handle
		psChildData - pointer to handle structure of child subhandle

 @Return	Error code or PVRSRV_OK

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
******************************************************************************

 @Function	HandleListRemove

 @Description	Remove a handle from a list

 @Input		hEntry - handle to be removed
		psEntry - pointer to handle structure of item to be removed
		uiEntryOffset - offset of list item struct in handle structure
		uiParentOffset - offset to list head struct in handle structure

 @Return	Error code or PVRSRV_OK

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
	if (psBase == NULL || psEntry == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

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
******************************************************************************

 @Function	UnlinkFromParent

 @Description	Remove a subhandle from its parents list

 @Input		psHandleData - pointer to handle data structure of child subhandle

 @Return	Error code or PVRSRV_OK

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
******************************************************************************

 @Function	HandleListIterate

 @Description	Iterate over the items in a list

 @Input		psHead - pointer to list head
		uiParentOffset - offset to list head struct in handle structure
		uiEntryOffset - offset of list item struct in handle structure
		pfnIterFunc - function to be called for each handle in the list

 @Return	Error code or PVRSRV_OK

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
		if (eError != PVRSRV_OK)
		{
			return eError;
		}

		hHandle = hNext;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	IterateOverChildren

 @Description	Iterate over the subhandles of a parent handle

 @Input		psParentData - pointer to parent handle structure
		pfnIterFunc - function to be called for each subhandle

 @Return	Error code or PVRSRV_OK

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
******************************************************************************

 @Function	ParentIfPrivate

 @Description	Return the parent handle if the handle was allocated
		with PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE, else return
		NULL

 @Input		psHandleData - pointer to handle data structure

 @Return	Parent handle, or NULL

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

static PVRSRV_ERROR FreeHandleWrapper(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE hHandle);

/*!
******************************************************************************

 @Function	FreeHandle

 @Description	Free a handle data structure.

 @Input		psBase - Pointer to handle base structure
		hHandle - Handle to be freed
		eType - Type of the handle to be freed
		ppvData - Location for data associated with the freed handle

 @Output 		ppvData - Points to data that was associated with the freed handle

 @Return	PVRSRV_OK or PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR FreeHandle(PVRSRV_HANDLE_BASE *psBase,
			       IMG_HANDLE hHandle,
			       PVRSRV_HANDLE_TYPE eType,
			       void **ppvData)
{
	HANDLE_DATA *psHandleData = NULL;
	HANDLE_DATA *psReleasedHandleData;
	PVRSRV_ERROR eError;

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	if(_HandleUnref(psHandleData) > 0)
	{
		/* this handle still has references so do not destroy it
		 * or the underlying object yet
		 */
		return PVRSRV_OK;
	}

	/* Call the release data callback for each reference on the handle */
	if (psHandleData->pfnReleaseData != NULL)
	{
		eError = psHandleData->pfnReleaseData(psHandleData->pvData);
		if (eError == PVRSRV_ERROR_RETRY)
		{
			PVR_DPF((PVR_DBG_MESSAGE,
				 "FreeHandle: "
				 "Got retry while calling release data callback for %p (type = %d)",
				 hHandle,
				 (IMG_UINT32)psHandleData->eType));

			/* the caller should retry, so retain a reference on the handle */
			_HandleRef(psHandleData);

			return eError;
		}
		else if (eError != PVRSRV_OK)
		{
			return eError;
		}
	}

	if (!TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		HAND_KEY aKey;
		IMG_HANDLE hRemovedHandle;

		InitKey(aKey, psBase, psHandleData->pvData, psHandleData->eType, ParentIfPrivate(psHandleData));

		hRemovedHandle = (IMG_HANDLE)HASH_Remove_Extended(psBase->psHashTab, aKey);

		PVR_ASSERT(hRemovedHandle != NULL);
		PVR_ASSERT(hRemovedHandle == psHandleData->hHandle);
		PVR_UNREFERENCED_PARAMETER(hRemovedHandle);
	}

	eError = UnlinkFromParent(psBase, psHandleData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "FreeHandle: Error whilst unlinking from parent handle (%s)", 
			 PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	/* Free children */
	eError = IterateOverChildren(psBase, psHandleData, FreeHandleWrapper);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "FreeHandle: Error whilst freeing subhandles (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		return eError;
	}

	eError = gpsHandleFuncs->pfnReleaseHandle(psBase->psImplBase,
						  psHandleData->hHandle,
						  (void **)&psReleasedHandleData);
	if (eError == PVRSRV_OK)
	{
		PVR_ASSERT(psReleasedHandleData == psHandleData);
	}

	if (ppvData)
	{
		*ppvData = psHandleData->pvData;
	}

	OSFreeMem(psHandleData);

	return eError;
}

static PVRSRV_ERROR FreeHandleWrapper(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE hHandle)
{
	return FreeHandle(psBase, hHandle, PVRSRV_HANDLE_TYPE_NONE, NULL);
}

/*!
******************************************************************************

 @Function	FindHandle

 @Description	Find handle corresponding to a resource pointer

 @Input		psBase - pointer to handle base structure
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource

 @Return	the handle, or NULL if not found

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
******************************************************************************

 @Function	AllocHandle

 @Description	Allocate a new handle

 @Input		phHandle - location for new handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource
		hParent - parent handle or NULL
		pfnReleaseData - Function to release resource at handle release
		                 time

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

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
	if (psNewHandleData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "AllocHandle: Couldn't allocate handle data"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	eError = gpsHandleFuncs->pfnAcquireHandle(psBase->psImplBase, &hHandle, psNewHandleData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "AllocHandle: Failed to acquire a handle"));
		goto ErrorFreeHandleData;
	}

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
		if (!HASH_Insert_Extended(psBase->psHashTab, aKey, (uintptr_t)hHandle))
		{
			PVR_DPF((PVR_DBG_ERROR, "AllocHandle: Couldn't add handle to hash table"));
			eError = PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;
			goto ErrorReleaseHandle;
		}
	}

	psNewHandleData->hHandle = hHandle;
	psNewHandleData->eType = eType;
	psNewHandleData->eFlag = eFlag;
	psNewHandleData->pvData = pvData;
	psNewHandleData->pfnReleaseData = pfnReleaseData;
	psNewHandleData->ui32RefCount = 1;

	InitParentList(psNewHandleData);
#if defined(DEBUG)
	PVR_ASSERT(NoChildren(psNewHandleData));
#endif

	InitChildEntry(psNewHandleData);
#if defined(DEBUG)
	PVR_ASSERT(NoParent(psNewHandleData));
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
******************************************************************************

 @Function	PVRSRVAllocHandle

 @Description	Allocate a handle

 @Input		phHandle - location for new handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource
		pfnReleaseData - Function to release resource at handle release
		                 time

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE *psBase,
			       IMG_HANDLE *phHandle,
			       void *pvData,
			       PVRSRV_HANDLE_TYPE eType,
			       PVRSRV_HANDLE_ALLOC_FLAG eFlag,
			       PFN_HANDLE_RELEASE pfnReleaseData)
{
	PVRSRV_ERROR eError;

	LockHandle();
	eError = PVRSRVAllocHandleUnlocked(psBase, phHandle, pvData, eType, eFlag, pfnReleaseData);
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVAllocHandleUnlocked

 @Description	Allocate a handle without acquiring/releasing the handle
		lock. The function assumes you hold the lock when called.

 @Input		phHandle - location for new handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource
		pfnReleaseData - Function to release resource at handle release
		                 time

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
			       IMG_HANDLE *phHandle,
			       void *pvData,
			       PVRSRV_HANDLE_TYPE eType,
			       PVRSRV_HANDLE_ALLOC_FLAG eFlag,
			       PFN_HANDLE_RELEASE pfnReleaseData)
{
	PVRSRV_ERROR eError;

	*phHandle = NULL;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	if (pfnReleaseData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandle: Missing release function"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	eError = AllocHandle(psBase, phHandle, pvData, eType, eFlag, NULL, pfnReleaseData);

Exit:
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
PVRSRV_ERROR PVRSRVAllocSubHandle(PVRSRV_HANDLE_BASE *psBase,
				  IMG_HANDLE *phHandle,
				  void *pvData,
				  PVRSRV_HANDLE_TYPE eType,
				  PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				  IMG_HANDLE hParent)
{
	PVRSRV_ERROR eError;

	LockHandle();
	eError = PVRSRVAllocSubHandleUnlocked(psBase, phHandle, pvData, eType, eFlag, hParent);
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVAllocSubHandleUnlocked

 @Description	Allocate a subhandle without acquiring/releasing the
		handle lock. The function assumes you hold the lock when called.

 @Input		phHandle - location for new subhandle
		pvData - pointer to resource to be associated with the subhandle
		eType - the type of resource
		hParent - parent handle

 @Output	phHandle - points to new subhandle

 @Return	Error code or PVRSRV_OK

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

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	hParentKey = TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ? hParent : NULL;

	/* Lookup the parent handle */
	eError = GetHandleData(psBase, &psPHandleData, hParent, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Failed to get parent handle structure"));
		goto Exit;
	}

	eError = AllocHandle(psBase, &hHandle, pvData, eType, eFlag, hParentKey, NULL);
	if (eError != PVRSRV_OK)
	{
		goto Exit;
	}

	eError = GetHandleData(psBase, &psCHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Failed to get parent handle structure"));

		/* If we were able to allocate the handle then there should be no reason why we 
		   can't also get it's handle structure. Otherwise something has gone badly wrong. */
		PVR_ASSERT(eError == PVRSRV_OK);

		goto Exit;
	}

	/*
	 * Get the parent handle structure again, in case the handle
	 * structure has moved (depending on the implementation
	 * of AllocHandle).
	 */
	eError = GetHandleData(psBase, &psPHandleData, hParent, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Failed to get parent handle structure"));

		(void)FreeHandle(psBase, hHandle, eType, NULL);
		goto Exit;
	}

	eError = AdoptChild(psBase, psPHandleData, psCHandleData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Parent handle failed to adopt subhandle"));

		(void)FreeHandle(psBase, hHandle, eType, NULL);
		goto Exit;
	}

	*phHandle = hHandle;

	eError = PVRSRV_OK;

Exit:
	return eError;
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
PVRSRV_ERROR PVRSRVFindHandle(PVRSRV_HANDLE_BASE *psBase,
			      IMG_HANDLE *phHandle,
			      void *pvData,
			      PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	LockHandle();
	eError = PVRSRVFindHandleUnlocked(psBase, phHandle, pvData, eType);
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVFindHandleUnlocked

 @Description	Find handle corresponding to a resource pointer without
		acquiring/releasing the handle lock. The function assumes you hold
		the lock when called.

 @Input		phHandle - location for returned handle
		pvData - pointer to resource to be associated with the handle
		eType - the type of resource

 @Output	phHandle - points to handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVFindHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
			      IMG_HANDLE *phHandle,
			      void *pvData,
			      PVRSRV_HANDLE_TYPE eType)
{
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFindHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	/* See if there is a handle for this data pointer */
	hHandle = FindHandle(psBase, pvData, eType, NULL);
	if (hHandle == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVFindHandle: Error finding handle. Type %u",
			 eType));

		eError = PVRSRV_ERROR_HANDLE_NOT_FOUND;
		goto Exit;
	}

	*phHandle = hHandle;

	eError = PVRSRV_OK;

Exit:
	return eError;

}

/*!
******************************************************************************

 @Function	PVRSRVLookupHandle

 @Description	Lookup the data pointer corresponding to a handle

 @Input		ppvData - location to return data pointer
		hHandle - handle from client
		eType - handle type
		bRef - If TRUE, a reference will be added on the handle if the
		       lookup is successful.

 @Output	ppvData - points to the data pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE *psBase,
				void **ppvData,
				IMG_HANDLE hHandle,
				PVRSRV_HANDLE_TYPE eType,
				IMG_BOOL bRef)
{
	PVRSRV_ERROR eError;

	LockHandle();
	eError = PVRSRVLookupHandleUnlocked(psBase, ppvData, hHandle, eType, bRef);
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVLookupHandleUnlocked

 @Description	Lookup the data pointer corresponding to a handle without
 		acquiring/releasing the handle lock. The function assumes you
		hold the lock when called.

 @Input		ppvData - location to return data pointer
		hHandle - handle from client
		eType - handle type
		bRef - If TRUE, a reference will be added on the handle if the
		       lookup is successful.

 @Output	ppvData - points to the data pointer

 @Return	Error code or PVRSRV_OK

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

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVLookupHandle: Error looking up handle (%s). Handle %p, type %u",
			 PVRSRVGetErrorStringKM(eError),
			 (void*) hHandle,
			 eType));
#if defined(DEBUG) || defined(PVRSRV_NEED_PVR_DPF)
		OSDumpStack();
#endif
		goto Exit;
	}

	if(bRef)
	{
		_HandleRef(psHandleData);
	}

	*ppvData = psHandleData->pvData;

	eError = PVRSRV_OK;

Exit:

	return eError;
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

	LockHandle();

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupSubHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ExitUnlock;
	}

	eError = GetHandleData(psBase, &psCHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVLookupSubHandle: Error looking up subhandle (%s). Handle %p, type %u",
			 PVRSRVGetErrorStringKM(eError),
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
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVLookupSubHandle: Subhandle doesn't belong to given ancestor"));
			eError = PVRSRV_ERROR_INVALID_SUBHANDLE;
			goto ExitUnlock;
		}
	}

	*ppvData = psCHandleData->pvData;

	eError = PVRSRV_OK;

ExitUnlock:
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVGetParentHandle

 @Description	Lookup the parent of a handle

 @Input		phParent - location for returning parent handle
		hHandle - handle for which the parent handle is required
		eType - handle type
		hParent - parent handle

 @Output	*phParent - parent handle, or NULL if there is no parent

 @Return	Error code or PVRSRV_OK.  Note that not having a parent is
		not regarded as an error.

******************************************************************************/
PVRSRV_ERROR PVRSRVGetParentHandle(PVRSRV_HANDLE_BASE *psBase,
				   IMG_HANDLE *phParent,
				   IMG_HANDLE hHandle,
				   PVRSRV_HANDLE_TYPE eType)
{
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetParentHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ExitUnlock;
	}

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVGetParentHandle: Error looking up subhandle (%s). Type %u",
			 PVRSRVGetErrorStringKM(eError),
			 eType));
		OSDumpStack();
		goto ExitUnlock;
	}

	*phParent = ParentHandle(psHandleData);

	eError = PVRSRV_OK;

ExitUnlock:
	UnlockHandle();

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
PVRSRV_ERROR PVRSRVReleaseHandle(PVRSRV_HANDLE_BASE *psBase,
				 IMG_HANDLE hHandle,
				 PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	LockHandle();
	eError = PVRSRVReleaseHandleUnlocked(psBase, hHandle, eType);
	UnlockHandle();

	return eError;
}


/*!
******************************************************************************

 @Function	PVRSRVReleaseHandleUnlocked

 @Description	Release a handle that is no longer needed without
 		acquiring/releasing the handle lock. The function assumes you
		hold the lock when called.

 @Input 	hHandle - handle from client
		eType - handle type

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVReleaseHandleUnlocked(PVRSRV_HANDLE_BASE *psBase,
				 IMG_HANDLE hHandle,
				 PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto Exit;
	}

	eError = FreeHandle(psBase, hHandle, eType, NULL);

Exit:

	return eError;
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
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPurgeHandles: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ExitUnlock;
	}

	eError = gpsHandleFuncs->pfnPurgeHandles(psBase->psImplBase);

ExitUnlock:
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVAllocHandleBase

 @Description	Allocate a handle base structure for a process

 @Input 	ppsBase - pointer to handle base structure pointer

 @Output	ppsBase - points to handle base structure pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandleBase(PVRSRV_HANDLE_BASE **ppsBase,
                                   PVRSRV_HANDLE_BASE_TYPE eType)
{
	PVRSRV_HANDLE_BASE *psBase;
	PVRSRV_ERROR eError;

	if (gpsHandleFuncs == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Handle management not initialised"));
		return PVRSRV_ERROR_NOT_READY;
	}

	LockHandle();

	if (ppsBase == NULL)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto ErrorUnlock;
	}

	psBase = OSAllocZMem(sizeof(*psBase));
	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Couldn't allocate handle base"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto ErrorUnlock;
	}

	psBase->eType = eType;

	eError = gpsHandleFuncs->pfnCreateHandleBase(&psBase->psImplBase);
	if (eError != PVRSRV_OK)
	{
		goto ErrorFreeHandleBase;
	}

	psBase->psHashTab = HASH_Create_Extended(HANDLE_HASH_TAB_INIT_SIZE, 
						 sizeof(HAND_KEY), 
						 HASH_Func_Default, 
						 HASH_Key_Comp_Default);
	if (psBase->psHashTab == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Couldn't create data pointer hash table"));
		eError = PVRSRV_ERROR_UNABLE_TO_CREATE_HASH_TABLE;
		goto ErrorDestroyHandleBase;
	}

	*ppsBase = psBase;

	UnlockHandle();

	return PVRSRV_OK;

ErrorDestroyHandleBase:
	(void)gpsHandleFuncs->pfnDestroyHandleBase(psBase->psImplBase);

ErrorFreeHandleBase:
	OSFreeMem(psBase);

ErrorUnlock:
	UnlockHandle();

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

	if (psData == NULL ||
	    psData->psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "CountHandleDataWrapper: Missing free data"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = GetHandleData(psData->psBase, 
			       &psHandleData, 
			       hHandle, 
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "CountHandleDataWrapper: Couldn't get handle data for handle"));
		return eError;
	}

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

	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Missing base", __func__));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = GetHandleData(psBase,
			       &psHandleData,
			       hHandle,
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't get handle data for handle", __func__));
		return eError;
	}

	if (psHandleData != NULL)
	{
		PVR_DPF((PVR_DBG_WARNING, "    Handle: %6u, Type: %3u, Refs: %3u",
				(IMG_UINT32) (uintptr_t) psHandleData->hHandle,
				psHandleData->eType,
				psHandleData->ui32RefCount));

	}

	return PVRSRV_OK;
}



#endif /* defined(DEBUG) */

typedef struct FREE_HANDLE_DATA_TAG
{
	PVRSRV_HANDLE_BASE *psBase;
	PVRSRV_HANDLE_TYPE eHandleFreeType;
	/* timing data (ns) to release bridge lock upon the deadline */
	IMG_UINT64 ui64TimeStart;
	IMG_UINT64 ui64MaxBridgeTime;
} FREE_HANDLE_DATA;

static INLINE IMG_BOOL _CheckIfMaxTimeExpired(IMG_UINT64 ui64TimeStart, IMG_UINT64 ui64MaxBridgeTime)
{
	IMG_UINT64 ui64Diff;
	IMG_UINT64 ui64Now = OSClockns64();

	if(ui64Now >= ui64TimeStart)
	{
		ui64Diff = ui64Now - ui64TimeStart;
	}
	else
	{
		/* time has wrapped around */
		ui64Diff = (0xFFFFFFFFFFFFFFFF - ui64TimeStart) + ui64Now;
	}

	return ui64Diff >= ui64MaxBridgeTime;
}

static PVRSRV_ERROR FreeHandleDataWrapper(IMG_HANDLE hHandle, void *pvData)
{
	FREE_HANDLE_DATA *psData = (FREE_HANDLE_DATA *)pvData;
	HANDLE_DATA *psHandleData = NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	if (psData == NULL ||
	    psData->psBase == NULL ||
	    psData->eHandleFreeType == PVRSRV_HANDLE_TYPE_NONE)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleDataWrapper: Missing free data"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = GetHandleData(psData->psBase, 
			       &psHandleData, 
			       hHandle, 
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleDataWrapper: Couldn't get handle data for handle"));
		return eError;
	}

	if (psHandleData == NULL || psHandleData->eType != psData->eHandleFreeType)
	{
		return PVRSRV_OK;
	}

	PVR_ASSERT(psHandleData->ui32RefCount > 0);

	while (psHandleData->ui32RefCount != 0)
	{
		if (psHandleData->pfnReleaseData != NULL)
		{
			eError = psHandleData->pfnReleaseData(psHandleData->pvData);
			if (eError == PVRSRV_ERROR_RETRY)
			{
				PVR_DPF((PVR_DBG_MESSAGE,
					 "FreeHandleDataWrapper: "
					 "Got retry while calling release data callback for %p (type = %d)",
					 hHandle,
					 (IMG_UINT32)psHandleData->eType));

				return eError;
			}
			else if (eError != PVRSRV_OK)
			{
				return eError;
			}
		}

		_HandleUnref(psHandleData);
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
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	OSFreeMem(psHandleData);

	/* If we reach the end of the time slice release we can release the global
	 * lock, invoke the scheduler and reacquire the lock */
	if((psData->ui64MaxBridgeTime != 0) && _CheckIfMaxTimeExpired(psData->ui64TimeStart, psData->ui64MaxBridgeTime))
	{
		PVR_DPF((PVR_DBG_MESSAGE, "FreeResourceByCriteria: Lock timeout (timeout: %llu)",
								            psData->ui64MaxBridgeTime));
		UnlockHandle();
		OSReleaseBridgeLock();
		/* Invoke the scheduler to check if other processes are waiting for the lock */
		OSReleaseThreadQuanta();
		OSAcquireBridgeLock();
		LockHandle();
		/* Set again lock timeout and reset the counter */
		psData->ui64TimeStart = OSClockns64();
		PVR_DPF((PVR_DBG_MESSAGE, "FreeResourceByCriteria: Lock acquired again"));
	}

	return PVRSRV_OK;
}

static PVRSRV_HANDLE_TYPE g_aeOrderedFreeList[] =
{
	PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
	PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
	PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
	PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP,
	PVRSRV_HANDLE_TYPE_RGX_FREELIST,
	PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_MEMORY_BLOCK,
	PVRSRV_HANDLE_TYPE_RGX_POPULATION,
	PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
	PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT,
	PVRSRV_HANDLE_TYPE_RI_HANDLE,
	PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE,
	PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE,
	PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
	PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT,
	PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
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
	PVRSRV_HANDLE_TYPE_MM_PLAT_CLEANUP
};

/*!
******************************************************************************

 @Function	PVRSRVFreeHandleBase

 @Description	Free a handle base structure

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVFreeHandleBase(PVRSRV_HANDLE_BASE *psBase, IMG_UINT64 ui64MaxBridgeTime)
{
#if defined(DEBUG)
	COUNT_HANDLE_DATA sCountData = { 0 };
#endif
	FREE_HANDLE_DATA sHandleData = { 0 };
	IMG_UINT32 i;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();

	sHandleData.psBase = psBase;
	sHandleData.ui64TimeStart = OSClockns64();
	sHandleData.ui64MaxBridgeTime = ui64MaxBridgeTime;


#if defined(DEBUG)

	sCountData.psBase = psBase;

	eError = gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
						       &CountHandleDataWrapper,
						       (void *)&sCountData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVFreeHandleBase: Failed to perform handle count (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto ExitUnlock;
	}

	if (sCountData.uiHandleDataCount != 0)
	{
		IMG_BOOL bList = sCountData.uiHandleDataCount < HANDLE_DEBUG_LISTING_MAX_NUM;

		PVR_DPF((PVR_DBG_WARNING,
			 "%s: %u remaining handles in handle base 0x%p "
			 "(PVRSRV_HANDLE_BASE_TYPE %u). %s",
			 __func__,
			 sCountData.uiHandleDataCount,
			 psBase,
			 psBase->eType,
			 bList ? "Check handle.h for a type reference":
					 "Skipping details, too many items..."));

		if (bList)
		{
			PVR_DPF((PVR_DBG_WARNING, "-------- Listing Handles --------"));
			eError = gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
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
		if (eError != PVRSRV_OK)
		{
			goto ExitUnlock;
		}
	}


	if (psBase->psHashTab != NULL)
	{
		HASH_Delete(psBase->psHashTab);
	}

	eError = gpsHandleFuncs->pfnDestroyHandleBase(psBase->psImplBase);
	if (eError != PVRSRV_OK)
	{
		goto ExitUnlock;
	}

	OSFreeMem(psBase);

	eError = PVRSRV_OK;

ExitUnlock:
	UnlockHandle();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVHandleInit

 @Description	Initialise handle management

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVHandleInit(void)
{
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsKernelHandleBase == NULL);
	PVR_ASSERT(gpsHandleFuncs == NULL);
	PVR_ASSERT(!gbLockInitialised);

	eError = OSLockCreate(&gHandleLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: Creation of handle global lock failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		return eError;
	}
	gbLockInitialised = IMG_TRUE;

	eError = PVRSRVHandleGetFuncTable(&gpsHandleFuncs);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: PVRSRVHandleGetFuncTable failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto ErrorHandleDeinit;
	}

	eError = PVRSRVAllocHandleBase(&gpsKernelHandleBase,
	                               PVRSRV_HANDLE_BASE_TYPE_GLOBAL);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: PVRSRVAllocHandleBase failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto ErrorHandleDeinit;
	}

	eError = gpsHandleFuncs->pfnEnableHandlePurging(gpsKernelHandleBase->psImplBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: PVRSRVEnableHandlePurging failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto ErrorHandleDeinit;
	}

	return PVRSRV_OK;

ErrorHandleDeinit:
	(void) PVRSRVHandleDeInit();

	return eError;
}

/*!
******************************************************************************

 @Function	PVRSRVHandleDeInit

 @Description	De-initialise handle management

 @Return	Error code or PVRSRV_OK

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
				PVR_DPF((PVR_DBG_ERROR,
					 "PVRSRVHandleDeInit: FreeHandleBase failed (%s)",
					 PVRSRVGetErrorStringKM(eError)));
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

	if (gbLockInitialised)
	{
		OSLockDestroy(gHandleLock);
		gbLockInitialised = IMG_FALSE;
	}

	return eError;
}
