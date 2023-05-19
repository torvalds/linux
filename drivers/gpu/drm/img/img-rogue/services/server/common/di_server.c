/*************************************************************************/ /*!
@File
@Title          Debug Info framework functions and types.
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

#include "di_server.h"
#include "osdi_impl.h"
#include "pvrsrv_error.h"
#include "dllist.h"
#include "lock.h"
#include "allocmem.h"
#include "osfunc.h"

#define ROOT_GROUP_NAME PVR_DRM_NAME

/*! Implementation object. */
typedef struct DI_IMPL
{
	const IMG_CHAR *pszName;       /*<! name of the implementation */
	OSDI_IMPL_CB sCb;              /*<! implementation callbacks */
	IMG_BOOL bInitialised;         /*<! set to IMG_TRUE after implementation
	                                    is initialised */

	DLLIST_NODE sListNode;         /*<! node element of the global list of all
	                                    implementations */
} DI_IMPL;

/*! Wrapper object for objects originating from derivative implementations.
 * This wraps both entries and groups native implementation objects. */
typedef struct DI_NATIVE_HANDLE
{
	void *pvHandle;                /*!< opaque handle to the native object */
	DI_IMPL *psDiImpl;             /*!< implementation pvHandle is associated
	                                    with */
	DLLIST_NODE sListNode;         /*!< node element of native handles list */
} DI_NATIVE_HANDLE;

/*! Debug Info entry object.
 *
 * Depending on the implementation this can be represented differently. For
 * example for the DebugFS this translates to a file.
 */
struct DI_ENTRY
{
	const IMG_CHAR *pszName;       /*!< name of the entry */
	void *pvPrivData;              /*! handle to entry's private data */
	DI_ENTRY_TYPE eType;           /*! entry type */
	DI_ITERATOR_CB sIterCb;        /*!< iterator interface for the entry */

	DLLIST_NODE sListNode;         /*!< node element of group's entry list */
	DLLIST_NODE sNativeHandleList; /*!< list of native handles belonging to this
	                                    entry */
};

/*! Debug Info group object.
 *
 * Depending on the implementation this can be represented differently. For
 * example for the DebugFS this translates to a directory.
 */
struct DI_GROUP
{
	IMG_CHAR *pszName;               /*!< name of the group */
	const struct DI_GROUP *psParent; /*!< parent groups */

	DLLIST_NODE sListNode;           /*!< node element of group's group list */
	DLLIST_NODE sGroupList;          /*!< list of groups (children) that belong
	                                      to this group */
	DLLIST_NODE sEntryList;          /*!< list of entries (children) that belong
	                                      to this group */
	DLLIST_NODE sNativeHandleList;   /*!< list of native handles belonging to
	                                      this group */
};

/* List of all registered implementations. */
static DECLARE_DLLIST(_g_sImpls);

/* Root group for the DI entries and groups. This group is used as a root
 * group for all other groups and entries if during creation a parent groups
 * is not given. */
static DI_GROUP *_g_psRootGroup;

/* Protects access to _g_sImpls and _g_psRootGroup */
static POS_LOCK _g_hLock;

PVRSRV_ERROR DIInit(void)
{
	PVRSRV_ERROR eError;

#if defined(__linux__) && defined(__KERNEL__)
	eError = OSLockCreateNoStats(&_g_hLock);
#else
	eError = OSLockCreate(&_g_hLock);
#endif
	PVR_LOG_GOTO_IF_ERROR(eError, "OSLockCreate", return_);

	_g_psRootGroup = OSAllocMemNoStats(sizeof(*_g_psRootGroup));
	PVR_LOG_GOTO_IF_NOMEM(_g_psRootGroup, eError, destroy_lock_);

	_g_psRootGroup->pszName = OSAllocMemNoStats(sizeof(ROOT_GROUP_NAME));
	PVR_LOG_GOTO_IF_NOMEM(_g_psRootGroup->pszName, eError, cleanup_name_);
	OSStringLCopy(_g_psRootGroup->pszName, ROOT_GROUP_NAME,
				  sizeof(ROOT_GROUP_NAME));

	dllist_init(&_g_psRootGroup->sListNode);
	dllist_init(&_g_psRootGroup->sGroupList);
	dllist_init(&_g_psRootGroup->sEntryList);
	dllist_init(&_g_psRootGroup->sNativeHandleList);

	return PVRSRV_OK;

cleanup_name_:
	OSFreeMemNoStats(_g_psRootGroup);
destroy_lock_:
#if defined(__linux__) && defined(__KERNEL__)
	OSLockDestroyNoStats(_g_hLock);
#else
	OSLockDestroy(_g_hLock);
#endif
return_:
	return eError;
}

/* Destroys the whole tree of group and entries for a given group as a root. */
static void _DeInitGroupRecursively(DI_GROUP *psGroup)
{
	DLLIST_NODE *psThis, *psNext;

	dllist_foreach_node(&psGroup->sEntryList, psThis, psNext)
	{
		DI_ENTRY *psThisEntry = IMG_CONTAINER_OF(psThis, DI_ENTRY, sListNode);
		DIDestroyEntry(psThisEntry);
	}

	dllist_foreach_node(&psGroup->sGroupList, psThis, psNext)
	{
		DI_GROUP *psThisGroup = IMG_CONTAINER_OF(psThis, DI_GROUP, sListNode);

		_DeInitGroupRecursively(psThisGroup);
	}

	DIDestroyGroup(psGroup);
}

void DIDeInit(void)
{
	DLLIST_NODE *psThis, *psNext;

	OSLockAcquire(_g_hLock);

	if (!dllist_is_empty(&_g_psRootGroup->sGroupList) ||
	    !dllist_is_empty(&_g_psRootGroup->sEntryList))
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: entries or groups still exist during "
		        "de-initialisation process, destroying all", __func__));
	}

	_DeInitGroupRecursively(_g_psRootGroup);
	_g_psRootGroup = NULL;

	/* Remove all of the implementations. */
	dllist_foreach_node(&_g_sImpls, psThis, psNext)
	{
		DI_IMPL *psDiImpl = IMG_CONTAINER_OF(psThis, DI_IMPL, sListNode);

		if (psDiImpl->bInitialised)
		{
			psDiImpl->sCb.pfnDeInit();
			psDiImpl->bInitialised = IMG_FALSE;
		}

		dllist_remove_node(&psDiImpl->sListNode);
		OSFreeMem(psDiImpl);
	}

	OSLockRelease(_g_hLock);

	/* all resources freed so free the lock itself too */

#if defined(__linux__) && defined(__KERNEL__)
	OSLockDestroyNoStats(_g_hLock);
#else
	OSLockDestroy(_g_hLock);
#endif
}

static IMG_BOOL _ValidateIteratorCb(const DI_ITERATOR_CB *psIterCb,
                                    DI_ENTRY_TYPE eType)
{
	IMG_UINT32 uiFlags = 0;

	if (psIterCb == NULL)
	{
		return IMG_FALSE;
	}

	if (eType == DI_ENTRY_TYPE_GENERIC)
	{
		uiFlags |= psIterCb->pfnShow != NULL ? BIT(0) : 0;
		uiFlags |= psIterCb->pfnStart != NULL ? BIT(1) : 0;
		uiFlags |= psIterCb->pfnStop != NULL ? BIT(2) : 0;
		uiFlags |= psIterCb->pfnNext != NULL ? BIT(3) : 0;

		/* either only pfnShow or all callbacks need to be set */
		if (uiFlags != BIT(0) && !BITMASK_HAS(uiFlags, 0x0f))
		{
			return IMG_FALSE;
		}
	}
	else if (eType == DI_ENTRY_TYPE_RANDOM_ACCESS)
	{
		uiFlags |= psIterCb->pfnRead != NULL ? BIT(0) : 0;
		uiFlags |= psIterCb->pfnSeek != NULL ? BIT(1) : 0;

		/* either only pfnRead or all callbacks need to be set */
		if (uiFlags != BIT(0) && !BITMASK_HAS(uiFlags, 0x03))
		{
			return IMG_FALSE;
		}
	}
	else
	{
		return IMG_FALSE;
	}

	return IMG_TRUE;
}

static PVRSRV_ERROR _CreateNativeEntry(DI_ENTRY *psEntry,
                                       const DI_NATIVE_HANDLE *psNativeParent)
{
	PVRSRV_ERROR eError;
	DI_IMPL *psImpl = psNativeParent->psDiImpl;

	DI_NATIVE_HANDLE *psNativeEntry = OSAllocMem(sizeof(*psNativeEntry));
	PVR_LOG_GOTO_IF_NOMEM(psNativeEntry, eError, return_);

	eError = psImpl->sCb.pfnCreateEntry(psEntry->pszName,
	                                    psEntry->eType,
	                                    &psEntry->sIterCb,
	                                    psEntry->pvPrivData,
	                                    psNativeParent->pvHandle,
	                                    &psNativeEntry->pvHandle);
	PVR_LOG_GOTO_IF_ERROR(eError, "psImpl->sCb.pfnCreateEntry", free_memory_);

	psNativeEntry->psDiImpl = psImpl;

	dllist_add_to_head(&psEntry->sNativeHandleList, &psNativeEntry->sListNode);

	return PVRSRV_OK;

free_memory_:
	OSFreeMem(psNativeEntry);
return_:
	return eError;
}

static void _DestroyNativeEntry(DI_NATIVE_HANDLE *psNativeEntry)
{
	dllist_remove_node(&psNativeEntry->sListNode);
	OSFreeMem(psNativeEntry);
}

PVRSRV_ERROR DICreateEntry(const IMG_CHAR *pszName,
                           DI_GROUP *psGroup,
                           const DI_ITERATOR_CB *psIterCb,
                           void *pvPriv,
                           DI_ENTRY_TYPE eType,
                           DI_ENTRY **ppsEntry)
{
	PVRSRV_ERROR eError;
	DLLIST_NODE *psThis, *psNext;
	DI_ENTRY *psEntry;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pszName != NULL, "pszName");
	PVR_LOG_RETURN_IF_INVALID_PARAM(_ValidateIteratorCb(psIterCb, eType),
	                                "psIterCb");
	PVR_LOG_RETURN_IF_INVALID_PARAM(ppsEntry != NULL, "psEntry");

	psEntry = OSAllocMem(sizeof(*psEntry));
	PVR_LOG_RETURN_IF_NOMEM(psEntry, "OSAllocMem");

	if (psGroup == NULL)
	{
		psGroup = _g_psRootGroup;
	}

	psEntry->pszName = pszName;
	psEntry->pvPrivData = pvPriv;
	psEntry->eType = eType;
	psEntry->sIterCb = *psIterCb;
	dllist_init(&psEntry->sNativeHandleList);

	OSLockAcquire(_g_hLock);

	dllist_add_to_tail(&psGroup->sEntryList, &psEntry->sListNode);

	/* Iterate over all of the native handles of parent group to create
	 * the entry for every registered implementation. */
	dllist_foreach_node(&psGroup->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNativeGroup =
		        IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE, sListNode);

		eError = _CreateNativeEntry(psEntry, psNativeGroup);
		PVR_GOTO_IF_ERROR(eError, cleanup_);
	}

	OSLockRelease(_g_hLock);

	*ppsEntry = psEntry;

	return PVRSRV_OK;

cleanup_:
	OSLockRelease(_g_hLock);

	/* Something went wrong so if there were any native entries created remove
	 * them from the list, free them and free the DI entry itself. */
	dllist_foreach_node(&psEntry->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNativeEntry =
		        IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE, sListNode);

		_DestroyNativeEntry(psNativeEntry);
	}

	OSFreeMem(psEntry);

	return eError;
}

void DIDestroyEntry(DI_ENTRY *psEntry)
{
	DLLIST_NODE *psThis, *psNext;

	PVR_LOG_RETURN_VOID_IF_FALSE(psEntry != NULL,
	                             "psEntry invalid in DIDestroyEntry()");

	/* Iterate through all of the native entries of the DI entry, remove
	 * them from the list and then destroy them. After that, destroy the
	 * DI entry itself. */
	dllist_foreach_node(&psEntry->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNative = IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE,
		                                              sListNode);

		/* The implementation must ensure that entry is not removed if any
		 * operations are being executed on the entry. If this is the case
		 * the implementation should block until all of them are finished
		 * and prevent any further operations.
		 * This will guarantee proper synchronisation between the DI framework
		 * and underlying implementations and prevent destruction/access
		 * races. */
		psNative->psDiImpl->sCb.pfnDestroyEntry(psNative->pvHandle);
		dllist_remove_node(&psNative->sListNode);
		OSFreeMem(psNative);
	}

	dllist_remove_node(&psEntry->sListNode);

	OSFreeMem(psEntry);
}

static PVRSRV_ERROR _CreateNativeGroup(DI_GROUP *psGroup,
                                       const DI_NATIVE_HANDLE *psNativeParent,
                                       DI_NATIVE_HANDLE **ppsNativeGroup)
{
	PVRSRV_ERROR eError;
	DI_IMPL *psImpl = psNativeParent->psDiImpl;

	DI_NATIVE_HANDLE *psNativeGroup = OSAllocMem(sizeof(*psNativeGroup));
	PVR_LOG_GOTO_IF_NOMEM(psNativeGroup, eError, return_);

	eError = psImpl->sCb.pfnCreateGroup(psGroup->pszName,
	                                    psNativeParent->pvHandle,
	                                    &psNativeGroup->pvHandle);
	PVR_LOG_GOTO_IF_ERROR(eError, "psImpl->sCb.pfnCreateGroup", free_memory_);

	psNativeGroup->psDiImpl = psImpl;

	dllist_add_to_head(&psGroup->sNativeHandleList, &psNativeGroup->sListNode);

	*ppsNativeGroup = psNativeGroup;

	return PVRSRV_OK;

free_memory_:
	OSFreeMem(psNativeGroup);
return_:
	return eError;
}

static void _DestroyNativeGroup(DI_NATIVE_HANDLE *psNativeEntry)
{
	dllist_remove_node(&psNativeEntry->sListNode);
	OSFreeMem(psNativeEntry);
}

PVRSRV_ERROR DICreateGroup(const IMG_CHAR *pszName,
                           DI_GROUP *psParent,
                           DI_GROUP **ppsGroup)
{
	PVRSRV_ERROR eError;
	DLLIST_NODE *psThis, *psNext;
	DI_GROUP *psGroup;
	size_t uSize;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pszName != NULL, "pszName");
	PVR_LOG_RETURN_IF_INVALID_PARAM(ppsGroup != NULL, "ppsDiGroup");

	psGroup = OSAllocMem(sizeof(*psGroup));
	PVR_LOG_RETURN_IF_NOMEM(psGroup, "OSAllocMem");

	if (psParent == NULL)
	{
		psParent = _g_psRootGroup;
	}

	uSize = OSStringLength(pszName) + 1;
	psGroup->pszName = OSAllocMem(uSize * sizeof(*psGroup->pszName));
	PVR_LOG_GOTO_IF_NOMEM(psGroup->pszName, eError, cleanup_name_);
	OSStringLCopy(psGroup->pszName, pszName, uSize);

	psGroup->psParent = psParent;
	dllist_init(&psGroup->sGroupList);
	dllist_init(&psGroup->sEntryList);
	dllist_init(&psGroup->sNativeHandleList);

	OSLockAcquire(_g_hLock);

	dllist_add_to_tail(&psParent->sGroupList, &psGroup->sListNode);

	/* Iterate over all of the native handles of parent group to create
	 * the group for every registered implementation. */
	dllist_foreach_node(&psParent->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNativeGroup = NULL, *psNativeParent =
		        IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE, sListNode);

		eError = _CreateNativeGroup(psGroup, psNativeParent, &psNativeGroup);
		PVR_GOTO_IF_ERROR(eError, cleanup_);
	}

	OSLockRelease(_g_hLock);

	*ppsGroup = psGroup;

	return PVRSRV_OK;

cleanup_:
	OSLockRelease(_g_hLock);

	/* Something went wrong so if there were any native groups created remove
	 * them from the list, free them and free the DI group itself. */
	dllist_foreach_node(&psGroup->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNativeGroup =
		        IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE, sListNode);

		dllist_remove_node(&psNativeGroup->sListNode);
		OSFreeMem(psNativeGroup);
	}

	OSFreeMem(psGroup->pszName);
cleanup_name_:
	OSFreeMem(psGroup);

	return eError;
}

void DIDestroyGroup(DI_GROUP *psGroup)
{
	DLLIST_NODE *psThis, *psNext;

	PVR_LOG_RETURN_VOID_IF_FALSE(psGroup != NULL,
	                             "psGroup invalid in DIDestroyGroup()");

	/* Iterate through all of the native groups of the DI group, remove
	 * them from the list and then destroy them. After that destroy the
	 * DI group itself. */
	dllist_foreach_node(&psGroup->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNative = IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE,
		                                              sListNode);

		psNative->psDiImpl->sCb.pfnDestroyGroup(psNative->pvHandle);
		dllist_remove_node(&psNative->sListNode);
		OSFreeMem(psNative);
	}

	dllist_remove_node(&psGroup->sListNode);

	if (psGroup == _g_psRootGroup)
	{
		OSFreeMemNoStats(psGroup->pszName);
		OSFreeMemNoStats(psGroup);
	}
	else
	{
		OSFreeMem(psGroup->pszName);
		OSFreeMem(psGroup);
	}
}

void *DIGetPrivData(const OSDI_IMPL_ENTRY *psEntry)
{
	PVR_ASSERT(psEntry != NULL);

	return psEntry->pvPrivData;
}

void DIWrite(const OSDI_IMPL_ENTRY *psEntry, const void *pvData,
             IMG_UINT32 uiSize)
{
	PVR_ASSERT(psEntry != NULL);
	PVR_ASSERT(psEntry->psCb != NULL);
	PVR_ASSERT(psEntry->psCb->pfnWrite != NULL);
	PVR_ASSERT(psEntry->pvNative != NULL);

	psEntry->psCb->pfnWrite(psEntry->pvNative, pvData, uiSize);
}

void DIPrintf(const OSDI_IMPL_ENTRY *psEntry, const IMG_CHAR *pszFmt, ...)
{
	va_list args;

	PVR_ASSERT(psEntry != NULL);
	PVR_ASSERT(psEntry->psCb != NULL);
	PVR_ASSERT(psEntry->psCb->pfnVPrintf != NULL);
	PVR_ASSERT(psEntry->pvNative != NULL);

	va_start(args, pszFmt);
	psEntry->psCb->pfnVPrintf(psEntry->pvNative, pszFmt, args);
	va_end(args);
}

void DIVPrintf(const OSDI_IMPL_ENTRY *psEntry, const IMG_CHAR *pszFmt,
               va_list pArgs)
{
	PVR_ASSERT(psEntry != NULL);
	PVR_ASSERT(psEntry->psCb != NULL);
	PVR_ASSERT(psEntry->psCb->pfnVPrintf != NULL);
	PVR_ASSERT(psEntry->pvNative != NULL);

	psEntry->psCb->pfnVPrintf(psEntry->pvNative, pszFmt, pArgs);
}

void DIPuts(const OSDI_IMPL_ENTRY *psEntry, const IMG_CHAR *pszStr)
{
	PVR_ASSERT(psEntry != NULL);
	PVR_ASSERT(psEntry->psCb != NULL);
	PVR_ASSERT(psEntry->psCb->pfnPuts != NULL);
	PVR_ASSERT(psEntry->pvNative != NULL);

	psEntry->psCb->pfnPuts(psEntry->pvNative, pszStr);
}

IMG_BOOL DIHasOverflowed(const OSDI_IMPL_ENTRY *psEntry)
{
	PVR_ASSERT(psEntry != NULL);
	PVR_ASSERT(psEntry->psCb != NULL);
	PVR_ASSERT(psEntry->psCb->pfnHasOverflowed != NULL);
	PVR_ASSERT(psEntry->pvNative != NULL);

	return psEntry->psCb->pfnHasOverflowed(psEntry->pvNative);
}

/* ---- OS implementation API ---------------------------------------------- */

static IMG_BOOL _ValidateImplCb(const OSDI_IMPL_CB *psImplCb)
{
	PVR_GOTO_IF_FALSE(psImplCb->pfnInit != NULL, failed_);
	PVR_GOTO_IF_FALSE(psImplCb->pfnDeInit != NULL, failed_);
	PVR_GOTO_IF_FALSE(psImplCb->pfnCreateGroup != NULL, failed_);
	PVR_GOTO_IF_FALSE(psImplCb->pfnDestroyGroup != NULL, failed_);
	PVR_GOTO_IF_FALSE(psImplCb->pfnCreateEntry != NULL, failed_);
	PVR_GOTO_IF_FALSE(psImplCb->pfnDestroyEntry != NULL, failed_);

	return IMG_TRUE;

failed_:
	return IMG_FALSE;
}

/* Walks the tree of groups and entries and create all of the native handles
 * for the given implementation for all of the already existing groups and
 * entries. */
static PVRSRV_ERROR _InitNativeHandlesRecursively(DI_IMPL *psImpl,
                                               DI_GROUP *psGroup,
                                               DI_NATIVE_HANDLE *psNativeParent)
{
	PVRSRV_ERROR eError;
	DLLIST_NODE *psThis, *psNext;
	DI_NATIVE_HANDLE *psNativeGroup;

	psNativeGroup = OSAllocMem(sizeof(*psNativeGroup));
	PVR_LOG_RETURN_IF_NOMEM(psNativeGroup, "OSAllocMem");

	eError = psImpl->sCb.pfnCreateGroup(psGroup->pszName,
	                           psNativeParent ? psNativeParent->pvHandle : NULL,
	                           &psNativeGroup->pvHandle);
	PVR_LOG_GOTO_IF_ERROR(eError, "psImpl->sCb.pfnCreateGroup", free_memory_);

	psNativeGroup->psDiImpl = psImpl;

	dllist_add_to_head(&psGroup->sNativeHandleList,
	                   &psNativeGroup->sListNode);

	dllist_foreach_node(&psGroup->sGroupList, psThis, psNext)
	{
		DI_GROUP *psThisGroup = IMG_CONTAINER_OF(psThis, DI_GROUP, sListNode);

		// and then walk the new group
		eError = _InitNativeHandlesRecursively(psImpl, psThisGroup,
		                                       psNativeGroup);
		PVR_LOG_RETURN_IF_ERROR(eError, "_InitNativeHandlesRecursively");
	}

	dllist_foreach_node(&psGroup->sEntryList, psThis, psNext)
	{
		DI_ENTRY *psThisEntry = IMG_CONTAINER_OF(psThis, DI_ENTRY, sListNode);

		eError = _CreateNativeEntry(psThisEntry, psNativeGroup);
		PVR_LOG_RETURN_IF_ERROR(eError, "_CreateNativeEntry");
	}

	return PVRSRV_OK;

free_memory_:
	OSFreeMem(psNativeGroup);

	return eError;
}

/* Walks the tree of groups and entries and destroys all of the native handles
 * for the given implementation. */
static void _DeInitNativeHandlesRecursively(DI_IMPL *psImpl, DI_GROUP *psGroup)
{
	DLLIST_NODE *psThis, *psNext;

	dllist_foreach_node(&psGroup->sEntryList, psThis, psNext)
	{
		DI_ENTRY *psThisEntry = IMG_CONTAINER_OF(psThis, DI_ENTRY, sListNode);

		// free all of the native entries that belong to this implementation
		dllist_foreach_node(&psThisEntry->sNativeHandleList, psThis, psNext)
		{
			DI_NATIVE_HANDLE *psNativeEntry =
			        IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE, sListNode);

			if (psNativeEntry->psDiImpl == psImpl)
			{
				_DestroyNativeEntry(psNativeEntry);
				// there can be only one entry on the list for a given
				// implementation
				break;
			}
		}
	}

	dllist_foreach_node(&psGroup->sGroupList, psThis, psNext)
	{
		DI_GROUP *psThisGroup = IMG_CONTAINER_OF(psThis, DI_GROUP, sListNode);

		// and then walk the new group
		_DeInitNativeHandlesRecursively(psImpl, psThisGroup);
	}

	// free all of the native entries that belong to this implementation
	dllist_foreach_node(&psGroup->sNativeHandleList, psThis, psNext)
	{
		DI_NATIVE_HANDLE *psNativeGroup =
		        IMG_CONTAINER_OF(psThis, DI_NATIVE_HANDLE, sListNode);

		if (psNativeGroup->psDiImpl == psImpl)
		{
			_DestroyNativeGroup(psNativeGroup);
			// there can be only one entry on the list for a given
			// implementation
			break;
		}
	}
}

static PVRSRV_ERROR _InitImpl(DI_IMPL *psImpl)
{
	PVRSRV_ERROR eError;
	// DI_NATIVE_HANDLE *psNativeGroup;

	eError = psImpl->sCb.pfnInit();
	PVR_LOG_GOTO_IF_ERROR(eError, "psImpl->pfnInit()", return_);

	/* if the implementation is being created after any groups or entries
	 * have been created we need to walk the current tree and create
	 * native groups and entries for all of the existing ones */
	eError = _InitNativeHandlesRecursively(psImpl, _g_psRootGroup, NULL);
	PVR_LOG_GOTO_IF_ERROR(eError, "_InitNativeHandlesRecursively",
	                      free_native_handles_and_deinit_);

	psImpl->bInitialised = IMG_TRUE;

	return PVRSRV_OK;

free_native_handles_and_deinit_:
	/* something went wrong so we need to walk the tree and remove all of the
	 * native entries and groups that we've created before we can destroy
	 * the implementation */
	_DeInitNativeHandlesRecursively(psImpl, _g_psRootGroup);
	psImpl->sCb.pfnDeInit();
return_:
	return eError;
}

PVRSRV_ERROR DIRegisterImplementation(const IMG_CHAR *pszName,
                                      const OSDI_IMPL_CB *psImplCb)
{
	DI_IMPL *psImpl;
	PVRSRV_ERROR eError;

	PVR_LOG_RETURN_IF_INVALID_PARAM(pszName != NULL, "pszName");
	PVR_LOG_RETURN_IF_INVALID_PARAM(_ValidateImplCb(psImplCb), "psImplCb");
	/* if root group does not exist it can mean 2 things:
	 * - DIInit() was not called so initialisation order is incorrect and needs
	 *   to be fixed
	 * - DIInit() failed but if that happens we should never make it here */
	PVR_ASSERT(_g_psRootGroup != NULL);

	psImpl = OSAllocMem(sizeof(*psImpl));
	PVR_LOG_RETURN_IF_NOMEM(psImpl, "OSAllocMem");

	psImpl->pszName = pszName;
	psImpl->sCb = *psImplCb;

	OSLockAcquire(_g_hLock);

	eError = _InitImpl(psImpl);
	if (eError != PVRSRV_OK)
	{
		/* implementation could not be initialised so remove it from the
		 * list, free the memory and forget about it */

		PVR_DPF((PVR_DBG_ERROR, "%s: could not initialise \"%s\" debug "
		        "info implementation, discarding", __func__,
		        psImpl->pszName));

		goto free_impl_;
	}

	psImpl->bInitialised = IMG_TRUE;

	dllist_add_to_tail(&_g_sImpls, &psImpl->sListNode);

	OSLockRelease(_g_hLock);

	return PVRSRV_OK;

free_impl_:
	OSLockRelease(_g_hLock);

	OSFreeMem(psImpl);

	return eError;
}
