/*************************************************************************/ /*!
@File
@Title		Resource Handle Manager - IDR Back-end
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description	Provide IDR based resource handle management back-end
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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/idr.h>

#include "handle_impl.h"
#include "allocmem.h"
#include "osfunc.h"
#include "pvr_debug.h"

#define ID_VALUE_MIN	1
#define ID_VALUE_MAX	INT_MAX

#define	ID_TO_HANDLE(i) ((IMG_HANDLE)(uintptr_t)(i))
#define	HANDLE_TO_ID(h) ((IMG_INT)(uintptr_t)(h))

struct _HANDLE_IMPL_BASE_
{
	struct idr sIdr;

	IMG_UINT32 ui32MaxHandleValue;

	IMG_UINT32 ui32TotalHandCount;
};

typedef struct _HANDLE_ITER_DATA_WRAPPER_
{
	PFN_HANDLE_ITER pfnHandleIter;
	void *pvHandleIterData;
} HANDLE_ITER_DATA_WRAPPER;


static int HandleIterFuncWrapper(int id, void *data, void *iter_data)
{
	HANDLE_ITER_DATA_WRAPPER *psIterData = (HANDLE_ITER_DATA_WRAPPER *)iter_data;

	PVR_UNREFERENCED_PARAMETER(data);

	return (int)psIterData->pfnHandleIter(ID_TO_HANDLE(id), psIterData->pvHandleIterData);
}

/*!
******************************************************************************

 @Function	AcquireHandle

 @Description	Acquire a new handle

 @Input		psBase - Pointer to handle base structure
		phHandle - Points to a handle pointer
		pvData - Pointer to resource to be associated with the handle

 @Output	phHandle - Points to a handle pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR AcquireHandle(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE *phHandle, 
				  void *pvData)
{
	int id;
	int result;

	PVR_ASSERT(psBase != NULL);
	PVR_ASSERT(phHandle != NULL);
	PVR_ASSERT(pvData != NULL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0))
	idr_preload(GFP_KERNEL);
	id = idr_alloc(&psBase->sIdr, pvData, ID_VALUE_MIN, psBase->ui32MaxHandleValue + 1, 0);
	idr_preload_end();

	result = id;
#else
	do
	{
		if (idr_pre_get(&psBase->sIdr, GFP_KERNEL) == 0)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		result = idr_get_new_above(&psBase->sIdr, pvData, ID_VALUE_MIN, &id);
	} while (result == -EAGAIN);

	if ((IMG_UINT32)id > psBase->ui32MaxHandleValue)
	{
		idr_remove(&psBase->sIdr, id);
		result = -ENOSPC;
	}
#endif

	if (result < 0)
	{
		if (result == -ENOSPC)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Limit of %u handles reached", 
				 __FUNCTION__, psBase->ui32MaxHandleValue));

			return PVRSRV_ERROR_UNABLE_TO_ADD_HANDLE;
		}

		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBase->ui32TotalHandCount++;

	*phHandle = ID_TO_HANDLE(id);

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	ReleaseHandle

 @Description	Release a handle that is no longer needed.

 @Input		psBase - Pointer to handle base structure
		hHandle - Handle to release
		ppvData - Points to a void data pointer

 @Output	ppvData - Points to a void data pointer

 @Return	PVRSRV_OK or PVRSRV_ERROR

******************************************************************************/
static PVRSRV_ERROR ReleaseHandle(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE hHandle, 
				  void **ppvData)
{
	int id = HANDLE_TO_ID(hHandle);
	void *pvData;

	PVR_ASSERT(psBase);

	/* Get the data associated with the handle. If we get back NULL then 
	   it's an invalid handle */

	pvData = idr_find(&psBase->sIdr, id);
	if (pvData)
	{
		idr_remove(&psBase->sIdr, id);
		psBase->ui32TotalHandCount--;
	}

	if (pvData == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Handle out of range (%u > %u)", 
			 __FUNCTION__, id, psBase->ui32TotalHandCount));
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}

	if (ppvData)
	{
		*ppvData = pvData;
	}

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	GetHandleData

 @Description	Get the data associated with the given handle

 @Input		psBase - Pointer to handle base structure
		hHandle - Handle from which data should be retrieved
                ppvData - Points to a void data pointer

 @Output	ppvData - Points to a void data pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR GetHandleData(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE hHandle, 
				  void **ppvData)
{
	int id = HANDLE_TO_ID(hHandle);
	void *pvData;

	PVR_ASSERT(psBase);
	PVR_ASSERT(ppvData);

	pvData = idr_find(&psBase->sIdr, id);
	if (pvData)
	{
		*ppvData = pvData;

		return PVRSRV_OK;
	}
	else
	{
		return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
	}
}

/*!
******************************************************************************

 @Function	SetHandleData

 @Description	Set the data associated with the given handle

 @Input		psBase - Pointer to handle base structure
		hHandle - Handle for which data should be changed
		pvData - Pointer to new data to be associated with the handle

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR SetHandleData(HANDLE_IMPL_BASE *psBase, 
				  IMG_HANDLE hHandle, 
				  void *pvData)
{
	int id = HANDLE_TO_ID(hHandle);
	void *pvOldData;

	PVR_ASSERT(psBase);

	pvOldData = idr_replace(&psBase->sIdr, pvData, id);
	if (IS_ERR(pvOldData))
	{
		if (PTR_ERR(pvOldData) == -ENOENT)
		{
			return PVRSRV_ERROR_HANDLE_NOT_ALLOCATED;
		}
		else
		{
			return PVRSRV_ERROR_HANDLE_INDEX_OUT_OF_RANGE;
		}
	}

	return PVRSRV_OK;
}

static PVRSRV_ERROR IterateOverHandles(HANDLE_IMPL_BASE *psBase, PFN_HANDLE_ITER pfnHandleIter, void *pvHandleIterData)
{
	HANDLE_ITER_DATA_WRAPPER sIterData;

	PVR_ASSERT(psBase);
	PVR_ASSERT(pfnHandleIter);

	sIterData.pfnHandleIter = pfnHandleIter;
	sIterData.pvHandleIterData = pvHandleIterData;

	return (PVRSRV_ERROR)idr_for_each(&psBase->sIdr, HandleIterFuncWrapper, &sIterData);
}

/*!
******************************************************************************

 @Function	EnableHandlePurging

 @Description	Enable purging for a given handle base

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR EnableHandlePurging(HANDLE_IMPL_BASE *psBase)
{
	PVR_UNREFERENCED_PARAMETER(psBase);
	PVR_ASSERT(psBase);

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	PurgeHandles

 @Description	Purge handles for a given handle base

 @Input 	psBase - Pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR PurgeHandles(HANDLE_IMPL_BASE *psBase)
{
	PVR_UNREFERENCED_PARAMETER(psBase);
	PVR_ASSERT(psBase);

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	CreateHandleBase

 @Description	Create a handle base structure

 @Input 	ppsBase - pointer to handle base structure pointer

 @Output	ppsBase - points to handle base structure pointer

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR CreateHandleBase(HANDLE_IMPL_BASE **ppsBase)
{
	HANDLE_IMPL_BASE *psBase;

	PVR_ASSERT(ppsBase);

	psBase = OSAllocZMem(sizeof(*psBase));
	if (psBase == NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Couldn't allocate generic handle base", __FUNCTION__));

		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	idr_init(&psBase->sIdr);

	psBase->ui32MaxHandleValue = ID_VALUE_MAX;
	psBase->ui32TotalHandCount = 0;

	*ppsBase = psBase;

	return PVRSRV_OK;
}

/*!
******************************************************************************

 @Function	DestroyHandleBase

 @Description	Destroy a handle base structure

 @Input 	psBase - pointer to handle base structure

 @Return	Error code or PVRSRV_OK

******************************************************************************/
static PVRSRV_ERROR DestroyHandleBase(HANDLE_IMPL_BASE *psBase)
{
	PVR_ASSERT(psBase);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0))
	idr_remove_all(&psBase->sIdr);
#endif

	/* Finally destroy the idr */
	idr_destroy(&psBase->sIdr);

	OSFreeMem(psBase);

	return PVRSRV_OK;
}


static const HANDLE_IMPL_FUNCTAB g_sHandleFuncTab = 
{
	.pfnAcquireHandle = AcquireHandle,
	.pfnReleaseHandle = ReleaseHandle,
	.pfnGetHandleData = GetHandleData,
	.pfnSetHandleData = SetHandleData,
	.pfnIterateOverHandles = IterateOverHandles,
	.pfnEnableHandlePurging = EnableHandlePurging,
	.pfnPurgeHandles = PurgeHandles,
	.pfnCreateHandleBase = CreateHandleBase,
	.pfnDestroyHandleBase = DestroyHandleBase
};

PVRSRV_ERROR PVRSRVHandleGetFuncTable(HANDLE_IMPL_FUNCTAB const **ppsFuncs)
{
	static IMG_BOOL bAcquired = IMG_FALSE;

	if (bAcquired)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: Function table already acquired", 
			 __FUNCTION__));
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	if (ppsFuncs == NULL)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	*ppsFuncs = &g_sHandleFuncTab;

	bAcquired = IMG_TRUE;

	return PVRSRV_OK;
}
