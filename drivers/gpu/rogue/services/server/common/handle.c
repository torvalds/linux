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

#ifdef	PVR_SECURE_HANDLES
/* See handle.h for a description of the handle API. */

/*
 * The implmentation supports movable handle structures, allowing the address
 * of a handle structure to change without having to fix up pointers in
 * any of the handle structures.  For example, the linked list mechanism
 * used to link subhandles together uses handle array indices rather than
 * pointers to the structures themselves.
 */

#include <stddef.h>

#include "handle.h"
#include "handle_impl.h"
#include "allocmem.h"
#include "pvr_debug.h"

#define	HANDLE_HASH_TAB_INIT_SIZE		32

#define	SET_FLAG(v, f)				((IMG_VOID)((v) |= (f)))
#define	CLEAR_FLAG(v, f)			((IMG_VOID)((v) &= (IMG_UINT)~(f)))
#define	TEST_FLAG(v, f)				((IMG_BOOL)(((v) & (f)) != 0))

#define	TEST_ALLOC_FLAG(psHandleData, f)	TEST_FLAG((psHandleData)->eFlag, f)


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
	IMG_VOID *pvData;

	/* List head for subhandles of this handle */
	HANDLE_LIST sChildren;

	/* List entry for sibling subhandles */
	HANDLE_LIST sSiblings;

	/* Reference count, always 1 unless handle is shared */
	IMG_UINT32 ui32Refs;
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
};

/*
 * The key for the handle hash table is an array of three elements, the
 * pointer to the resource, the resource type and the parent handle (or 
 * IMG_NULL if there is no parent). The eHandKey enumeration gives the 
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
typedef IMG_UINTPTR_T HAND_KEY[HAND_KEY_LEN];

/* Stores a pointer to the function table of the handle back-end in use */
static HANDLE_IMPL_FUNCTAB const *gpsHandleFuncs = IMG_NULL;

/* 
 * Global lock added to avoid to call the handling functions
 * only in a single threaded context.
 */

static POS_LOCK gHandleLock;

static void LockHandle(void)
{
	OSLockAcquire(gHandleLock);
}

static void UnlockHandle(void)
{
	OSLockRelease(gHandleLock);
}

/*
 * Kernel handle base structure. This is used for handles that are not 
 * allocated on behalf of a particular process.
 */
PVRSRV_HANDLE_BASE *gpsKernelHandleBase = IMG_NULL;

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
						  (IMG_VOID **)&psHandleData);
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
		hParent - parent handle or IMG_NULL

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(HandleListInit)
#endif
static INLINE
IMG_VOID HandleListInit(IMG_HANDLE hHandle, HANDLE_LIST *psList, IMG_HANDLE hParent)
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
IMG_VOID InitParentList(HANDLE_DATA *psHandleData)
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
IMG_VOID InitChildEntry(HANDLE_DATA *psHandleData)
{
	HandleListInit(psHandleData->hHandle, &psHandleData->sSiblings, IMG_NULL);
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
		PVR_ASSERT(psHandleData->sSiblings.hParent == IMG_NULL);

		return IMG_TRUE;
	}
	else
	{
		PVR_ASSERT(psHandleData->sSiblings.hParent != IMG_NULL);
	}
	return IMG_FALSE;
}
#endif /*DEBUG*/

/*!
******************************************************************************

 @Function	ParentHandle

 @Description	Determine the parent of a handle

 @Input		psHandleData - pointer to handle data structure

 @Return	Parent handle, or IMG_NULL if the handle is not a subhandle.

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
					      IMG_SIZE_T uiParentOffset, 
					      IMG_SIZE_T uiEntryOffset)
{
	HANDLE_DATA *psHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(psBase != IMG_NULL);

	eError = GetHandleData(psBase, 
			       &psHandleData, 
			       hEntry, 
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		return IMG_NULL;
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
				    IMG_SIZE_T uiParentOffset,
				    IMG_HANDLE hNewEntry,
				    HANDLE_LIST *psNewEntry,
				    IMG_SIZE_T uiEntryOffset,
				    IMG_HANDLE hParent)
{
	HANDLE_LIST *psPrevEntry;

	if (psBase == IMG_NULL || psEntry == IMG_NULL || psNewEntry == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psPrevEntry = GetHandleListFromHandleAndOffset(psBase, 
						       psEntry->hPrev, 
						       hParent, 
						       uiParentOffset, 
						       uiEntryOffset);
	if (psPrevEntry == IMG_NULL)
	{
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}

	PVR_ASSERT(psNewEntry->hParent == IMG_NULL);
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
			      IMG_SIZE_T uiEntryOffset,
			      IMG_SIZE_T uiParentOffset)
{
	if (psBase == IMG_NULL || psEntry == IMG_NULL)
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
		if (psPrev == IMG_NULL)
		{
			return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
		}

		psNext = GetHandleListFromHandleAndOffset(psBase, 
							  psEntry->hNext, 
							  psEntry->hParent, 
							  uiParentOffset, 
							  uiEntryOffset);
		if (psNext == IMG_NULL)
		{
			return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
		}

		/*
		 * The list head is on the list, and we don't want to
		 * remove it.
		 */
		PVR_ASSERT(psEntry->hParent != IMG_NULL);

		psPrev->hNext = psEntry->hNext;
		psNext->hPrev = psEntry->hPrev;

		HandleListInit(hEntry, psEntry, IMG_NULL);
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
			       IMG_SIZE_T uiParentOffset,
			       IMG_SIZE_T uiEntryOffset,
			       PVRSRV_ERROR (*pfnIterFunc)(PVRSRV_HANDLE_BASE *, IMG_HANDLE))
{
	IMG_HANDLE hHandle = psHead->hNext;
	IMG_HANDLE hParent = psHead->hParent;
	IMG_HANDLE hNext;

	PVR_ASSERT(psHead->hParent != IMG_NULL);

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
		if (psEntry == IMG_NULL)
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
		IMG_NULL

 @Input		psHandleData - pointer to handle data structure

 @Return	Parent handle, or IMG_NULL

******************************************************************************/
#ifdef INLINE_IS_PRAGMA
#pragma inline(ParentIfPrivate)
#endif
static INLINE
IMG_HANDLE ParentIfPrivate(HANDLE_DATA *psHandleData)
{
	return TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ?
			ParentHandle(psHandleData) : IMG_NULL;
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
IMG_VOID InitKey(HAND_KEY aKey,
		 PVRSRV_HANDLE_BASE *psBase,
		 IMG_VOID *pvData,
		 PVRSRV_HANDLE_TYPE eType,
		 IMG_HANDLE hParent)
{
	PVR_UNREFERENCED_PARAMETER(psBase);

	aKey[HAND_KEY_DATA] = (IMG_UINTPTR_T)pvData;
	aKey[HAND_KEY_TYPE] = (IMG_UINTPTR_T)eType;
	aKey[HAND_KEY_PARENT] = (IMG_UINTPTR_T)hParent;
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
			       IMG_VOID **ppvData)
{
	HANDLE_DATA *psHandleData = IMG_NULL;
	HANDLE_DATA *psReleasedHandleData;
	PVRSRV_ERROR eError;

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	PVR_ASSERT(psHandleData->ui32Refs>0);

	psHandleData->ui32Refs--;
	if (psHandleData->ui32Refs > 0)
	{
		/* Reference count still positive, only possible for shared handles */
		PVR_ASSERT(TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_SHARED));
		return PVRSRV_OK;
	}
	/* else reference count zero, time to clean up */

	if (!TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		HAND_KEY aKey;
		IMG_HANDLE hRemovedHandle;

		InitKey(aKey, psBase, psHandleData->pvData, psHandleData->eType, ParentIfPrivate(psHandleData));

		hRemovedHandle = (IMG_HANDLE)HASH_Remove_Extended(psBase->psHashTab, aKey);

		PVR_ASSERT(hRemovedHandle != IMG_NULL);
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

	eError = gpsHandleFuncs->pfnReleaseHandle(psBase->psImplBase, psHandleData->hHandle, (IMG_VOID **)&psReleasedHandleData);
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
	return FreeHandle(psBase, hHandle, PVRSRV_HANDLE_TYPE_NONE, IMG_NULL);
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
IMG_HANDLE FindHandle(PVRSRV_HANDLE_BASE *psBase,
		      IMG_VOID *pvData,
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
		hParent - parent handle or IMG_NULL

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR AllocHandle(PVRSRV_HANDLE_BASE *psBase,
				IMG_HANDLE *phHandle,
				IMG_VOID *pvData,
				PVRSRV_HANDLE_TYPE eType,
				PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				IMG_HANDLE hParent)
{
	HANDLE_DATA *psNewHandleData;
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(psBase != IMG_NULL && psBase->psHashTab != IMG_NULL);
	PVR_ASSERT(gpsHandleFuncs);

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* Handle must not already exist */
		PVR_ASSERT(FindHandle(psBase, pvData, eType, hParent) == IMG_NULL);
	}

	psNewHandleData = OSAllocZMem(sizeof(*psNewHandleData));
	if (psNewHandleData == IMG_NULL)
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
		if (!HASH_Insert_Extended(psBase->psHashTab, aKey, (IMG_UINTPTR_T)hHandle))
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
	psNewHandleData->ui32Refs = 1;

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
	(IMG_VOID)gpsHandleFuncs->pfnReleaseHandle(psBase->psImplBase, hHandle, IMG_NULL);

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

 @Output	phHandle - points to new handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE *psBase,
			       IMG_HANDLE *phHandle,
			       IMG_VOID *pvData,
			       PVRSRV_HANDLE_TYPE eType,
			       PVRSRV_HANDLE_ALLOC_FLAG eFlag)
{
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	*phHandle = IMG_NULL;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exit_AllocHandle;
	}

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* See if there is already a handle for this data pointer */
		hHandle = FindHandle(psBase, pvData, eType, IMG_NULL);
		if (hHandle != IMG_NULL)
		{
			HANDLE_DATA *psHandleData = IMG_NULL;

			eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "PVRSRVAllocHandle: Lookup of existing handle failed (%s)",
					 PVRSRVGetErrorStringKM(eError)));
				goto exit_AllocHandle;
			}

			/*
			 * If the client is willing to share a handle, and the
			 * existing handle is marked as shareable, return the
			 * existing handle.
			 */
			if (TEST_FLAG(psHandleData->eFlag & eFlag, PVRSRV_HANDLE_ALLOC_FLAG_SHARED))
			{
				psHandleData->ui32Refs++;
				*phHandle = hHandle;
				eError = PVRSRV_OK;
				goto exit_AllocHandle;
			}
			eError = PVRSRV_ERROR_HANDLE_NOT_SHAREABLE;
			goto exit_AllocHandle;
		}
	}

	eError = AllocHandle(psBase, phHandle, pvData, eType, eFlag, IMG_NULL);

	exit_AllocHandle:
	UnlockHandle();

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
				  IMG_VOID *pvData,
				  PVRSRV_HANDLE_TYPE eType,
				  PVRSRV_HANDLE_ALLOC_FLAG eFlag,
				  IMG_HANDLE hParent)
{
	HANDLE_DATA *psPHandleData = IMG_NULL;
	HANDLE_DATA *psCHandleData = IMG_NULL;
	IMG_HANDLE hParentKey;
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	*phHandle = IMG_NULL;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();

	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	hParentKey = TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE) ? hParent : IMG_NULL;

	/* Lookup the parent handle */
	eError = GetHandleData(psBase, &psPHandleData, hParent, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Failed to get parent handle structure"));
		goto err;
	}

	if (!TEST_FLAG(eFlag, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		/* See if there is already a handle for this data pointer */
		hHandle = FindHandle(psBase, pvData, eType, hParentKey);
		if (hHandle != IMG_NULL)
		{
			eError = GetHandleData(psBase, &psCHandleData, hHandle, eType);
			if (eError != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Lookup of existing handle failed"));
				goto err;
			}

			PVR_ASSERT(hParentKey != IMG_NULL && ParentHandle(psCHandleData) == hParent);

			/*
			 * If the client is willing to share a handle, the
			 * existing handle is marked as shareable, and the
			 * existing handle has the same parent, return the
			 * existing handle.
			 */
			if (TEST_FLAG(psCHandleData->eFlag & eFlag, PVRSRV_HANDLE_ALLOC_FLAG_SHARED) && 
			    ParentHandle(psCHandleData) == hParent)
			{
				psCHandleData->ui32Refs++;
				*phHandle = hHandle;
				eError = PVRSRV_OK;
				goto err;
			}
			eError = PVRSRV_ERROR_HANDLE_NOT_SHAREABLE;
			goto err;
		}
	}

	eError = AllocHandle(psBase, &hHandle, pvData, eType, eFlag, hParentKey);
	if (eError != PVRSRV_OK)
	{
		goto err;
	}

	eError = GetHandleData(psBase, &psCHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Failed to get parent handle structure"));

		/* If we were able to allocate the handle then there should be no reason why we 
		   can't also get it's handle structure. Otherwise something has gone badly wrong. */
		PVR_ASSERT(eError == PVRSRV_OK);

		goto err;
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

		FreeHandle(psBase, hHandle, eType, IMG_NULL);
		goto err;
	}

	eError = AdoptChild(psBase, psPHandleData, psCHandleData);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSubHandle: Parent handle failed to adopt subhandle"));

		FreeHandle(psBase, hHandle, eType, IMG_NULL);
		goto err;
	}

	*phHandle = hHandle;

	eError = PVRSRV_OK;

	err:
	UnlockHandle();
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
			      IMG_VOID *pvData,
			      PVRSRV_HANDLE_TYPE eType)
{
	IMG_HANDLE hHandle;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVFindHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	/* See if there is a handle for this data pointer */
	hHandle = FindHandle(psBase, pvData, eType, IMG_NULL);
	if (hHandle == IMG_NULL)
	{
		eError = PVRSRV_ERROR_HANDLE_NOT_FOUND;
		goto err;
	}

	*phHandle = hHandle;

	eError = PVRSRV_OK;

	err:
	UnlockHandle();
	return eError;

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
PVRSRV_ERROR PVRSRVLookupHandleAnyType(PVRSRV_HANDLE_BASE *psBase,
				       IMG_PVOID *ppvData,
				       PVRSRV_HANDLE_TYPE *peType,
				       IMG_HANDLE hHandle)
{
	HANDLE_DATA *psHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupHandleAnyType: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	eError = GetHandleData(psBase, &psHandleData, hHandle, PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVLookupHandleAnyType: Error looking up handle (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		OSDumpStack();
		goto err;
	}

	*ppvData = psHandleData->pvData;
	*peType = psHandleData->eType;

	eError = PVRSRV_OK;

	err:
	UnlockHandle();
	return eError;
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
PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE *psBase,
				IMG_PVOID *ppvData,
				IMG_HANDLE hHandle,
				PVRSRV_HANDLE_TYPE eType)
{
	HANDLE_DATA *psHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVLookupHandle: Error looking up handle (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		OSDumpStack();
		goto err;
	}

	*ppvData = psHandleData->pvData;

	eError = PVRSRV_OK;

	err:
	UnlockHandle();
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
				   IMG_PVOID *ppvData,
				   IMG_HANDLE hHandle,
				   PVRSRV_HANDLE_TYPE eType,
				   IMG_HANDLE hAncestor)
{
	HANDLE_DATA *psPHandleData = IMG_NULL;
	HANDLE_DATA *psCHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupSubHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	eError = GetHandleData(psBase, &psCHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVLookupSubHandle: Error looking up subhandle (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		OSDumpStack();
		goto err;
	}

	/* Look for hAncestor among the handle's ancestors */
	for (psPHandleData = psCHandleData; ParentHandle(psPHandleData) != hAncestor; )
	{
		eError = GetHandleData(psBase, &psPHandleData, ParentHandle(psPHandleData), PVRSRV_HANDLE_TYPE_NONE);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVLookupSubHandle: Subhandle doesn't belong to given ancestor"));
			eError = PVRSRV_ERROR_INVALID_SUBHANDLE;
			goto err;
		}
	}

	*ppvData = psCHandleData->pvData;

	eError = PVRSRV_OK;

	err:
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

 @Output	*phParent - parent handle, or IMG_NULL if there is no parent

 @Return	Error code or PVRSRV_OK.  Note that not having a parent is
		not regarded as an error.

******************************************************************************/
PVRSRV_ERROR PVRSRVGetParentHandle(PVRSRV_HANDLE_BASE *psBase,
				   IMG_HANDLE *phParent,
				   IMG_HANDLE hHandle,
				   PVRSRV_HANDLE_TYPE eType)
{
	HANDLE_DATA *psHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVGetParentHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	eError = GetHandleData(psBase, &psHandleData, hHandle, eType);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVGetParentHandle: Error looking up subhandle (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		OSDumpStack();
		goto err;
	}

	*phParent = ParentHandle(psHandleData);

	eError = PVRSRV_OK;

	err:
	UnlockHandle();
	return eError;
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
PVRSRV_ERROR PVRSRVLookupAndReleaseHandle(PVRSRV_HANDLE_BASE *psBase,
					  IMG_PVOID *ppvData,
					  IMG_HANDLE hHandle,
					  PVRSRV_HANDLE_TYPE eType)
{
	PVRSRV_ERROR eError;

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVLookupAndReleaseHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exit_LookupAndReleaseHandle;
	}

	eError = FreeHandle(psBase, hHandle, eType, ppvData);

	exit_LookupAndReleaseHandle:
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

	/* PVRSRV_HANDLE_TYPE_NONE is reserved for internal use */
	PVR_ASSERT(eType != PVRSRV_HANDLE_TYPE_NONE);
	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVReleaseHandle: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exit_ReleaseHandle;
	}

	eError = FreeHandle(psBase, hHandle, eType, IMG_NULL);

exit_ReleaseHandle:
	UnlockHandle();
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
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVPurgeHandles: Missing handle base"));
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto exit_PVRSRVPurgeHandles;
	}

	eError = gpsHandleFuncs->pfnPurgeHandles(psBase->psImplBase);

	exit_PVRSRVPurgeHandles:
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
PVRSRV_ERROR PVRSRVAllocHandleBase(PVRSRV_HANDLE_BASE **ppsBase)
{
	PVRSRV_HANDLE_BASE *psBase;
	PVRSRV_ERROR eError;

	if (gpsHandleFuncs == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Handle management not initialised"));
		return PVRSRV_ERROR_NOT_READY;
	}

	LockHandle();
	if (ppsBase == IMG_NULL)
	{
		eError = PVRSRV_ERROR_INVALID_PARAMS;
		goto err;
	}

	psBase = OSAllocZMem(sizeof(*psBase));
	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Couldn't allocate handle base"));
		eError = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto err;
	}

	eError = gpsHandleFuncs->pfnCreateHandleBase(&psBase->psImplBase);
	if (eError != PVRSRV_OK)
	{
		goto ErrorFreeHandleBase;
	}

	psBase->psHashTab = HASH_Create_Extended(HANDLE_HASH_TAB_INIT_SIZE, 
						 sizeof(HAND_KEY), 
						 HASH_Func_Default, 
						 HASH_Key_Comp_Default);
	if (psBase->psHashTab == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocHandleBase: Couldn't create data pointer hash table"));
		eError = PVRSRV_ERROR_UNABLE_TO_CREATE_HASH_TABLE;
		goto ErrorDestroyHandleBase;
	}

	*ppsBase = psBase;

	UnlockHandle();
	return PVRSRV_OK;

ErrorDestroyHandleBase:
	(IMG_VOID)gpsHandleFuncs->pfnDestroyHandleBase(psBase->psImplBase);

ErrorFreeHandleBase:
	OSFreeMem(psBase);

err:
	UnlockHandle();
	return eError;
}

static PVRSRV_ERROR FreeHandleDataWrapper(IMG_HANDLE hHandle, IMG_VOID *pvData)
{
	PVRSRV_HANDLE_BASE *psBase = (PVRSRV_HANDLE_BASE *)pvData;
	HANDLE_DATA *psHandleData = IMG_NULL;
	PVRSRV_ERROR eError;

	if (psBase == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleDataWrapper: Handle base missing"));
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	eError = GetHandleData(psBase, 
			       &psHandleData, 
			       hHandle, 
			       PVRSRV_HANDLE_TYPE_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "FreeHandleDataWrapper: Couldn't get handle data for handle"));
		return eError;
	}

	if (!TEST_ALLOC_FLAG(psHandleData, PVRSRV_HANDLE_ALLOC_FLAG_MULTI))
	{
		HAND_KEY aKey;
		IMG_HANDLE hRemovedHandle;

		InitKey(aKey, psBase, psHandleData->pvData, psHandleData->eType, ParentIfPrivate(psHandleData));

		hRemovedHandle = (IMG_HANDLE)HASH_Remove_Extended(psBase->psHashTab, aKey);

		PVR_ASSERT(hRemovedHandle != IMG_NULL);
		PVR_ASSERT(hRemovedHandle == psHandleData->hHandle);
		PVR_UNREFERENCED_PARAMETER(hRemovedHandle);
	}

	OSFreeMem(psHandleData);

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

	PVR_ASSERT(gpsHandleFuncs);

	LockHandle();
	/* Make sure all handles have been freed before destroying the handle base */
	eError = gpsHandleFuncs->pfnIterateOverHandles(psBase->psImplBase,
						       &FreeHandleDataWrapper,
						       (IMG_VOID *)psBase);
	if (eError != PVRSRV_OK)
	{
		goto err;
	}

	if (psBase->psHashTab != IMG_NULL)
	{
		HASH_Delete(psBase->psHashTab);
	}

	eError = gpsHandleFuncs->pfnDestroyHandleBase(psBase->psImplBase);
	if (eError != PVRSRV_OK)
	{
		goto err;
	}

	OSFreeMem(psBase);

	eError = PVRSRV_OK;
err:
	UnlockHandle();
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
	PVR_ASSERT(gpsHandleFuncs == IMG_NULL);

	eError = OSLockCreate(&gHandleLock, LOCK_TYPE_PASSIVE);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: Creation of handle global lock failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto error;
	}

	eError = PVRSRVHandleGetFuncTable(&gpsHandleFuncs);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: PVRSRVHandleGetFuncTable failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto error;
	}

	eError = PVRSRVAllocHandleBase(&gpsKernelHandleBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: PVRSRVAllocHandleBase failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
		goto error;
	}

	eError = gpsHandleFuncs->pfnEnableHandlePurging(gpsKernelHandleBase->psImplBase);
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,
			 "PVRSRVHandleInit: PVRSRVEnableHandlePurging failed (%s)",
			 PVRSRVGetErrorStringKM(eError)));
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

	if (gpsHandleFuncs != IMG_NULL)
	{
		if (gpsKernelHandleBase != IMG_NULL)
		{
			eError = PVRSRVFreeHandleBase(gpsKernelHandleBase);
			if (eError == PVRSRV_OK)
			{
				gpsKernelHandleBase = IMG_NULL;
			}
			else
			{
				PVR_DPF((PVR_DBG_ERROR,
					 "PVRSRVHandleDeInit: FreeHandleBase failed (%s)",
					 PVRSRVGetErrorStringKM(eError)));
			}
		}

		OSLockDestroy(gHandleLock);

		if (eError == PVRSRV_OK)
		{
			gpsHandleFuncs = IMG_NULL;
		}
	}
	else
	{
		/* If we don't have a handle function table we shouldn't have a handle base either */
		PVR_ASSERT(gpsKernelHandleBase == IMG_NULL);
	}

	return eError;
}
#else
/* disable warning about empty module */
#ifdef	_WIN32
#pragma warning (disable:4206)
#endif
#endif	/* #ifdef PVR_SECURE_HANDLES */

