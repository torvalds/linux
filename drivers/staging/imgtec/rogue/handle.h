/**************************************************************************/ /*!
@File
@Title          Handle Manager API
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Provide handle management
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

#if !defined(__HANDLE_H__)
#define __HANDLE_H__

/*
 * Handle API
 * ----------
 * The handle API is intended to provide handles for kernel resources,
 * which can then be passed back to user space processes.
 *
 * The following functions comprise the API.  Each function takes a
 * pointer to a PVRSRV_HANDLE_BASE strcture, one of which is allocated
 * for each process, and stored in the per-process data area.  Use
 * KERNEL_HANDLE_BASE for handles not allocated for a particular process,
 * or for handles that need to be allocated before the PVRSRV_HANDLE_BASE
 * structure for the process is available.
 *
 * PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType,
 * 	PVRSRV_HANDLE_ALLOC_FLAG eFlag);
 *
 * Allocate a handle phHandle, for the resource of type eType pointed to by
 * pvData.
 *
 * For handles that have a definite lifetime, where the corresponding
 * resource is explicitly created and destroyed, eFlag should be zero.
 *
 * If a particular resource may be referenced multiple times by a
 * given process, setting eFlag to PVRSRV_HANDLE_ALLOC_FLAG_MULTI
 * will allow multiple handles to be allocated for the resource.
 * Such handles cannot be found with PVRSRVFindHandle.
 *
 * PVRSRV_ERROR PVRSRVAllocSubHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType,
 * 	PVRSRV_HANDLE_ALLOC_FLAG eFlag, IMG_HANDLE hParent);
 *
 * This function is similar to PVRSRVAllocHandle, except that the allocated
 * handles are associated with a parent handle, hParent, that has been
 * allocated previously.  Subhandles are automatically deallocated when their
 * parent handle is deallocated.
 * Subhandles can be treated as ordinary handles.  For example, they may
 * have subhandles of their own, and may be explicity deallocated using
 * PVRSRVReleaseHandle (see below).
 *
 * PVRSRV_ERROR PVRSRVFindHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType);
 *
 * Find the handle previously allocated for the resource pointed to by
 * pvData, of type eType.  Handles allocated with the flag
 * PVRSRV_HANDLE_ALLOC_FLAG_MULTI cannot be found using this
 * function.
 *
 * PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	void **ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType);
 *
 * Given a handle for a resource of type eType, return the pointer to the
 * resource.
 *
 * PVRSRV_ERROR PVRSRVLookuSubHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	void **ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType,
 * 	IMH_HANDLE hAncestor);
 *
 * Similar to PVRSRVLookupHandle, but checks the handle is a descendant
 * of hAncestor.
 *
 * PVRSRV_ERROR PVRSRVReleaseHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType);
 *
 * Deallocate a handle of given type.
 *
 * PVRSRV_ERROR PVRSRVGetParentHandle(PVRSRV_HANDLE_BASE *psBase,
 * 	void **phParent, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType);
 *
 * Return the parent of a handle in *phParent, or NULL if the handle has
 * no parent.
 */

#include "lock_types.h"
#include "img_types.h"
#include "hash.h"

typedef enum
{
	PVRSRV_HANDLE_TYPE_NONE = 0,
	PVRSRV_HANDLE_TYPE_SHARED_EVENT_OBJECT,
	PVRSRV_HANDLE_TYPE_EVENT_OBJECT_CONNECT,
	PVRSRV_HANDLE_TYPE_PMR_LOCAL_EXPORT_HANDLE,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_EXPORT,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_SECURE_EXPORT,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_CTX_EXPORT,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_HEAP,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_RESERVATION,
	PVRSRV_HANDLE_TYPE_DEVMEMINT_MAPPING,
	PVRSRV_HANDLE_TYPE_RGX_FW_MEMDESC,
	PVRSRV_HANDLE_TYPE_RGX_RTDATA_CLEANUP,
	PVRSRV_HANDLE_TYPE_RGX_FREELIST,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RPM_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_RPM_FREELIST,
	PVRSRV_HANDLE_TYPE_RGX_MEMORY_BLOCK,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RENDER_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_TQ_TDM_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_COMPUTE_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_RAY_CONTEXT,
	PVRSRV_HANDLE_TYPE_RGX_SERVER_KICKSYNC_CONTEXT,
	PVRSRV_HANDLE_TYPE_SYNC_PRIMITIVE_BLOCK,
	PVRSRV_HANDLE_TYPE_SERVER_SYNC_PRIMITIVE,
	PVRSRV_HANDLE_TYPE_SERVER_SYNC_EXPORT,
	PVRSRV_HANDLE_TYPE_SERVER_OP_COOKIE,
	PVRSRV_HANDLE_TYPE_SYNC_RECORD_HANDLE,
	PVRSRV_HANDLE_TYPE_RGX_FWIF_RENDERTARGET,
	PVRSRV_HANDLE_TYPE_RGX_FWIF_ZSBUFFER,
	PVRSRV_HANDLE_TYPE_RGX_POPULATION,
	PVRSRV_HANDLE_TYPE_DC_DEVICE,
	PVRSRV_HANDLE_TYPE_DC_DISPLAY_CONTEXT,
	PVRSRV_HANDLE_TYPE_DC_BUFFER,
	PVRSRV_HANDLE_TYPE_DC_PIN_HANDLE,
	PVRSRV_HANDLE_TYPE_DEVMEM_MEM_IMPORT,
	PVRSRV_HANDLE_TYPE_PHYSMEM_PMR_PAGELIST,
	PVRSRV_HANDLE_TYPE_PVR_TL_SD,
	PVRSRV_HANDLE_TYPE_RI_HANDLE,
	PVRSRV_HANDLE_TYPE_DEV_PRIV_DATA,
	PVRSRV_HANDLE_TYPE_MM_PLAT_CLEANUP,
	PVRSRV_HANDLE_TYPE_WORKEST_RETURN_DATA
} PVRSRV_HANDLE_TYPE;

typedef enum
{
	PVRSRV_HANDLE_BASE_TYPE_CONNECTION,
	PVRSRV_HANDLE_BASE_TYPE_PROCESS,
	PVRSRV_HANDLE_BASE_TYPE_GLOBAL
} PVRSRV_HANDLE_BASE_TYPE;


typedef enum
{
	/* No flags */
	PVRSRV_HANDLE_ALLOC_FLAG_NONE = 		0,
	/* Muliple handles can point at the given data pointer */
	PVRSRV_HANDLE_ALLOC_FLAG_MULTI = 		0x01,
	/* Subhandles are allocated in a private handle space */
	PVRSRV_HANDLE_ALLOC_FLAG_PRIVATE = 		0x02
} PVRSRV_HANDLE_ALLOC_FLAG;

typedef struct _HANDLE_BASE_ PVRSRV_HANDLE_BASE;

typedef struct _PROCESS_HANDLE_BASE_
{
	PVRSRV_HANDLE_BASE *psHandleBase;
	ATOMIC_T iRefCount;

} PROCESS_HANDLE_BASE;

extern PVRSRV_HANDLE_BASE *gpsKernelHandleBase;
#define	KERNEL_HANDLE_BASE (gpsKernelHandleBase)

#define HANDLE_DEBUG_LISTING_MAX_NUM 20

typedef PVRSRV_ERROR (*PFN_HANDLE_RELEASE)(void *pvData);

PVRSRV_ERROR PVRSRVAllocHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag, PFN_HANDLE_RELEASE pfnReleaseData);
PVRSRV_ERROR PVRSRVAllocHandleUnlocked(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag, PFN_HANDLE_RELEASE pfnReleaseData);

PVRSRV_ERROR PVRSRVAllocSubHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag, IMG_HANDLE hParent);
PVRSRV_ERROR PVRSRVAllocSubHandleUnlocked(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType, PVRSRV_HANDLE_ALLOC_FLAG eFlag, IMG_HANDLE hParent);

PVRSRV_ERROR PVRSRVFindHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType);
PVRSRV_ERROR PVRSRVFindHandleUnlocked(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phHandle, void *pvData, PVRSRV_HANDLE_TYPE eType);

PVRSRV_ERROR PVRSRVLookupHandle(PVRSRV_HANDLE_BASE *psBase, void **ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType, IMG_BOOL bRef);
PVRSRV_ERROR PVRSRVLookupHandleUnlocked(PVRSRV_HANDLE_BASE *psBase, void **ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType, IMG_BOOL bRef);

PVRSRV_ERROR PVRSRVLookupSubHandle(PVRSRV_HANDLE_BASE *psBase, void **ppvData, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType, IMG_HANDLE hAncestor);

PVRSRV_ERROR PVRSRVGetParentHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE *phParent, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType);

PVRSRV_ERROR PVRSRVReleaseHandle(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType);
PVRSRV_ERROR PVRSRVReleaseHandleUnlocked(PVRSRV_HANDLE_BASE *psBase, IMG_HANDLE hHandle, PVRSRV_HANDLE_TYPE eType);

PVRSRV_ERROR PVRSRVPurgeHandles(PVRSRV_HANDLE_BASE *psBase);

PVRSRV_ERROR PVRSRVAllocHandleBase(PVRSRV_HANDLE_BASE **ppsBase,
                                   PVRSRV_HANDLE_BASE_TYPE eType);

PVRSRV_ERROR PVRSRVFreeHandleBase(PVRSRV_HANDLE_BASE *psBase, IMG_UINT64 ui64MaxBridgeTime);

PVRSRV_ERROR PVRSRVHandleInit(void);

PVRSRV_ERROR PVRSRVHandleDeInit(void);

void LockHandle(void);
void UnlockHandle(void);


#endif /* !defined(__HANDLE_H__) */
