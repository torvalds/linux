/*************************************************************************/ /*!
@Title          Parameter Buffer management functions
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

#include <stddef.h>

#include "services_headers.h"
#include "sgx_bridge_km.h"
#include "sgxapi_km.h"
#include "sgxinfo.h"
#include "sgxinfokm.h"
#include "pvr_bridge_km.h"
#include "pdump_km.h"
#include "sgxutils.h"

#if !defined(__linux__) && !defined(__QNXNTO__)
#pragma message("FIXME: Review use of OS_PAGEABLE vs OS_NON_PAGEABLE")
#endif

#include "lists.h"

static IMPLEMENT_LIST_INSERT(PVRSRV_STUB_PBDESC)
static IMPLEMENT_LIST_REMOVE(PVRSRV_STUB_PBDESC)

static PRESMAN_ITEM psResItemCreateSharedPB = IMG_NULL;
static PVRSRV_PER_PROCESS_DATA *psPerProcCreateSharedPB = IMG_NULL;

static PVRSRV_ERROR SGXCleanupSharedPBDescCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param, IMG_BOOL bDummy);
static PVRSRV_ERROR SGXCleanupSharedPBDescCreateLockCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param, IMG_BOOL bDummy);

/* override level pointer indirection */
/* PRQA S 5102 12 */
IMG_EXPORT PVRSRV_ERROR
SGXFindSharedPBDescKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
					  IMG_HANDLE 				hDevCookie,
					  IMG_BOOL 				bLockOnFailure,
					  IMG_UINT32 				ui32TotalPBSize,
					  IMG_HANDLE 				*phSharedPBDesc,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsSharedPBDescKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsHWPBDescKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsBlockKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	**ppsHWBlockKernelMemInfo,
					  PVRSRV_KERNEL_MEM_INFO 	***pppsSharedPBDescSubKernelMemInfos,
					  IMG_UINT32				*ui32SharedPBDescSubKernelMemInfosCount)
{
	PVRSRV_STUB_PBDESC *psStubPBDesc;
	PVRSRV_KERNEL_MEM_INFO **ppsSharedPBDescSubKernelMemInfos=IMG_NULL;
	PVRSRV_SGXDEV_INFO *psSGXDevInfo;
	PVRSRV_ERROR eError;

	psSGXDevInfo = ((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	psStubPBDesc = psSGXDevInfo->psStubPBDescListKM;
	if (psStubPBDesc != IMG_NULL)
	{
		IMG_UINT32 i;
		PRESMAN_ITEM psResItem;

		if(psStubPBDesc->ui32TotalPBSize != ui32TotalPBSize)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"SGXFindSharedPBDescKM: Shared PB requested with different size (0x%x) from existing shared PB (0x%x) - requested size ignored",
					ui32TotalPBSize, psStubPBDesc->ui32TotalPBSize));
		}

		if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
					  sizeof(PVRSRV_KERNEL_MEM_INFO *)
						* psStubPBDesc->ui32SubKernelMemInfosCount,
					  (IMG_VOID **)&ppsSharedPBDescSubKernelMemInfos,
					  IMG_NULL,
					  "Array of Kernel Memory Info") != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXFindSharedPBDescKM: OSAllocMem failed"));

			eError = PVRSRV_ERROR_OUT_OF_MEMORY;
			goto ExitNotFound;
		}

		psResItem = ResManRegisterRes(psPerProc->hResManContext,
									  RESMAN_TYPE_SHARED_PB_DESC,
									  psStubPBDesc,
									  0,
									  &SGXCleanupSharedPBDescCallback);

		if (psResItem == IMG_NULL)
		{
			OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					  sizeof(PVRSRV_KERNEL_MEM_INFO *) * psStubPBDesc->ui32SubKernelMemInfosCount,
					  ppsSharedPBDescSubKernelMemInfos,
					  0);
			/*not nulling pointer, out of scope*/

			PVR_DPF((PVR_DBG_ERROR, "SGXFindSharedPBDescKM: ResManRegisterRes failed"));

			eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
			goto ExitNotFound;
		}

		*ppsSharedPBDescKernelMemInfo = psStubPBDesc->psSharedPBDescKernelMemInfo;
		*ppsHWPBDescKernelMemInfo = psStubPBDesc->psHWPBDescKernelMemInfo;
		*ppsBlockKernelMemInfo = psStubPBDesc->psBlockKernelMemInfo;
		*ppsHWBlockKernelMemInfo = psStubPBDesc->psHWBlockKernelMemInfo;

		*ui32SharedPBDescSubKernelMemInfosCount =
			psStubPBDesc->ui32SubKernelMemInfosCount;

		*pppsSharedPBDescSubKernelMemInfos = ppsSharedPBDescSubKernelMemInfos;

		for(i=0; i<psStubPBDesc->ui32SubKernelMemInfosCount; i++)
		{
			ppsSharedPBDescSubKernelMemInfos[i] =
				psStubPBDesc->ppsSubKernelMemInfos[i];
		}

		psStubPBDesc->ui32RefCount++;
		*phSharedPBDesc = (IMG_HANDLE)psResItem;
		return PVRSRV_OK;
	}

	eError = PVRSRV_OK;
	if (bLockOnFailure)
	{
		if (psResItemCreateSharedPB == IMG_NULL)
		{
			psResItemCreateSharedPB = ResManRegisterRes(psPerProc->hResManContext,
				  RESMAN_TYPE_SHARED_PB_DESC_CREATE_LOCK,
				  psPerProc,
				  0,
				  &SGXCleanupSharedPBDescCreateLockCallback);

			if (psResItemCreateSharedPB == IMG_NULL)
			{
				PVR_DPF((PVR_DBG_ERROR, "SGXFindSharedPBDescKM: ResManRegisterRes failed"));

				eError = PVRSRV_ERROR_UNABLE_TO_REGISTER_RESOURCE;
				goto ExitNotFound;
			}
			PVR_ASSERT(psPerProcCreateSharedPB == IMG_NULL);
			psPerProcCreateSharedPB = psPerProc;
		}
		else
		{
			 eError = PVRSRV_ERROR_PROCESSING_BLOCKED;
		}
	}
ExitNotFound:
	*phSharedPBDesc = IMG_NULL;

	return eError;
}


static PVRSRV_ERROR
SGXCleanupSharedPBDescKM(PVRSRV_STUB_PBDESC *psStubPBDescIn)
{
	/*PVRSRV_STUB_PBDESC **ppsStubPBDesc;*/
	IMG_UINT32 i;
	PVRSRV_DEVICE_NODE *psDeviceNode;

	psDeviceNode = (PVRSRV_DEVICE_NODE*)psStubPBDescIn->hDevCookie;

	psStubPBDescIn->ui32RefCount--;
	if (psStubPBDescIn->ui32RefCount == 0)
	{
		IMG_DEV_VIRTADDR sHWPBDescDevVAddr = psStubPBDescIn->sHWPBDescDevVAddr;
		List_PVRSRV_STUB_PBDESC_Remove(psStubPBDescIn);
		for(i=0 ; i<psStubPBDescIn->ui32SubKernelMemInfosCount; i++)
		{
			PVRSRVFreeDeviceMemKM(psStubPBDescIn->hDevCookie,
								  psStubPBDescIn->ppsSubKernelMemInfos[i]);
		}

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_KERNEL_MEM_INFO *) * psStubPBDescIn->ui32SubKernelMemInfosCount,
				  psStubPBDescIn->ppsSubKernelMemInfos,
				  0);
		psStubPBDescIn->ppsSubKernelMemInfos = IMG_NULL;

		PVRSRVFreeSharedSysMemoryKM(psStubPBDescIn->psBlockKernelMemInfo);

		PVRSRVFreeDeviceMemKM(psStubPBDescIn->hDevCookie, psStubPBDescIn->psHWBlockKernelMemInfo);

		PVRSRVFreeDeviceMemKM(psStubPBDescIn->hDevCookie, psStubPBDescIn->psHWPBDescKernelMemInfo);

		PVRSRVFreeSharedSysMemoryKM(psStubPBDescIn->psSharedPBDescKernelMemInfo);

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_STUB_PBDESC),
				  psStubPBDescIn,
				  0);
		/*not nulling pointer, copy on stack*/

		/* signal the microkernel to clear its sTAHWPBDesc and s3DHWPBDesc values in sTA3DCtl */
		SGXCleanupRequest(psDeviceNode,
						  &sHWPBDescDevVAddr,
						  PVRSRV_CLEANUPCMD_PB,
						  CLEANUP_WITH_POLL);
	}
	return PVRSRV_OK;
	/*return PVRSRV_ERROR_INVALID_PARAMS;*/
}

static PVRSRV_ERROR SGXCleanupSharedPBDescCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param, IMG_BOOL bDummy)
{
	PVRSRV_STUB_PBDESC *psStubPBDesc = (PVRSRV_STUB_PBDESC *)pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	return SGXCleanupSharedPBDescKM(psStubPBDesc);
}

static PVRSRV_ERROR SGXCleanupSharedPBDescCreateLockCallback(IMG_PVOID pvParam, IMG_UINT32 ui32Param, IMG_BOOL bDummy)
{
#ifdef DEBUG
	PVRSRV_PER_PROCESS_DATA *psPerProc = (PVRSRV_PER_PROCESS_DATA *)pvParam;
	PVR_ASSERT(psPerProc == psPerProcCreateSharedPB);
#else
	PVR_UNREFERENCED_PARAMETER(pvParam);
#endif

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	psPerProcCreateSharedPB = IMG_NULL;
	psResItemCreateSharedPB = IMG_NULL;

	return PVRSRV_OK;
}


IMG_EXPORT PVRSRV_ERROR
SGXUnrefSharedPBDescKM(IMG_HANDLE hSharedPBDesc)
{
	PVR_ASSERT(hSharedPBDesc != IMG_NULL);

	return ResManFreeResByPtr(hSharedPBDesc, CLEANUP_WITH_POLL);
}


IMG_EXPORT PVRSRV_ERROR
SGXAddSharedPBDescKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
					 IMG_HANDLE					hDevCookie,
					 PVRSRV_KERNEL_MEM_INFO		*psSharedPBDescKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psHWPBDescKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psBlockKernelMemInfo,
					 PVRSRV_KERNEL_MEM_INFO		*psHWBlockKernelMemInfo,
					 IMG_UINT32					ui32TotalPBSize,
					 IMG_HANDLE					*phSharedPBDesc,
					 PVRSRV_KERNEL_MEM_INFO		**ppsSharedPBDescSubKernelMemInfos,
					 IMG_UINT32					ui32SharedPBDescSubKernelMemInfosCount,
					 IMG_DEV_VIRTADDR			sHWPBDescDevVAddr)
{
	PVRSRV_STUB_PBDESC *psStubPBDesc=IMG_NULL;
	PVRSRV_ERROR eRet = PVRSRV_ERROR_INVALID_PERPROC;
	IMG_UINT32 i;
	PVRSRV_SGXDEV_INFO *psSGXDevInfo;
	PRESMAN_ITEM psResItem;

	/*
	 * The caller must have previously called SGXFindSharedPBDesc with
	 * bLockOnFailure set, and not managed to find a suitable shared PB.
	 */
	if (psPerProcCreateSharedPB != psPerProc)
	{
		goto NoAdd;
	}
	else
	{
		PVR_ASSERT(psResItemCreateSharedPB != IMG_NULL);

		ResManFreeResByPtr(psResItemCreateSharedPB, CLEANUP_WITH_POLL);

		PVR_ASSERT(psResItemCreateSharedPB == IMG_NULL);
		PVR_ASSERT(psPerProcCreateSharedPB == IMG_NULL);
	}

	psSGXDevInfo = (PVRSRV_SGXDEV_INFO *)((PVRSRV_DEVICE_NODE *)hDevCookie)->pvDevice;

	psStubPBDesc = psSGXDevInfo->psStubPBDescListKM;
	if (psStubPBDesc != IMG_NULL)
	{
		if(psStubPBDesc->ui32TotalPBSize != ui32TotalPBSize)
		{
			PVR_DPF((PVR_DBG_WARNING,
					"SGXAddSharedPBDescKM: Shared PB requested with different size (0x%x) from existing shared PB (0x%x) - requested size ignored",
					ui32TotalPBSize, psStubPBDesc->ui32TotalPBSize));

		}

		/*
		 * We make the caller think the add was successful,
		 * but return the existing shared PB desc rather than
		 * a new one.
		 */
		psResItem = ResManRegisterRes(psPerProc->hResManContext,
									  RESMAN_TYPE_SHARED_PB_DESC,
									  psStubPBDesc,
									  0,
									  &SGXCleanupSharedPBDescCallback);
		if (psResItem == IMG_NULL)
		{
			PVR_DPF((PVR_DBG_ERROR,
				"SGXAddSharedPBDescKM: "
				"Failed to register existing shared "
				"PBDesc with the resource manager"));
			goto NoAddKeepPB;
		}

		/*
		 * The caller will unreference the PB desc after
		 * a successful add, so up the reference count.
		 */
		psStubPBDesc->ui32RefCount++;

		*phSharedPBDesc = (IMG_HANDLE)psResItem;
		eRet = PVRSRV_OK;
		goto NoAddKeepPB;
	}

	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_STUB_PBDESC),
				  (IMG_VOID **)&psStubPBDesc,
				  0,
				  "Stub Parameter Buffer Description") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: Failed to alloc "
					"StubPBDesc"));
		eRet = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto NoAdd;
	}


	psStubPBDesc->ppsSubKernelMemInfos = IMG_NULL;

	if(OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_KERNEL_MEM_INFO *)
				  * ui32SharedPBDescSubKernelMemInfosCount,
				  (IMG_VOID **)&psStubPBDesc->ppsSubKernelMemInfos,
				  0,
				  "Array of Kernel Memory Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: "
				 "Failed to alloc "
				 "StubPBDesc->ppsSubKernelMemInfos"));
		eRet = PVRSRV_ERROR_OUT_OF_MEMORY;
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psSharedPBDescKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psHWPBDescKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psBlockKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}

	if(PVRSRVDissociateMemFromResmanKM(psHWBlockKernelMemInfo)
	   != PVRSRV_OK)
	{
		goto NoAdd;
	}

	psStubPBDesc->ui32RefCount = 1;
	psStubPBDesc->ui32TotalPBSize = ui32TotalPBSize;
	psStubPBDesc->psSharedPBDescKernelMemInfo = psSharedPBDescKernelMemInfo;
	psStubPBDesc->psHWPBDescKernelMemInfo = psHWPBDescKernelMemInfo;
	psStubPBDesc->psBlockKernelMemInfo = psBlockKernelMemInfo;
	psStubPBDesc->psHWBlockKernelMemInfo = psHWBlockKernelMemInfo;

	psStubPBDesc->ui32SubKernelMemInfosCount =
		ui32SharedPBDescSubKernelMemInfosCount;
	for(i=0; i<ui32SharedPBDescSubKernelMemInfosCount; i++)
	{
		psStubPBDesc->ppsSubKernelMemInfos[i] = ppsSharedPBDescSubKernelMemInfos[i];
		if(PVRSRVDissociateMemFromResmanKM(ppsSharedPBDescSubKernelMemInfos[i])
		   != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: "
					 "Failed to dissociate shared PBDesc "
					 "from process"));
			goto NoAdd;
		}
	}

	psStubPBDesc->sHWPBDescDevVAddr = sHWPBDescDevVAddr;

	psResItem = ResManRegisterRes(psPerProc->hResManContext,
								  RESMAN_TYPE_SHARED_PB_DESC,
								  psStubPBDesc,
								  0,
								  &SGXCleanupSharedPBDescCallback);
	if (psResItem == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "SGXAddSharedPBDescKM: "
					 "Failed to register shared PBDesc "
					 " with the resource manager"));
		goto NoAdd;
	}
	psStubPBDesc->hDevCookie = hDevCookie;

	/* Finally everything was prepared successfully so link the new
	 * PB in to place. */
	List_PVRSRV_STUB_PBDESC_Insert(&(psSGXDevInfo->psStubPBDescListKM),
									psStubPBDesc);

	*phSharedPBDesc = (IMG_HANDLE)psResItem;

	return PVRSRV_OK;

NoAdd:
	if(psStubPBDesc)
	{
		if(psStubPBDesc->ppsSubKernelMemInfos)
		{
			OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
					  sizeof(PVRSRV_KERNEL_MEM_INFO *) * ui32SharedPBDescSubKernelMemInfosCount,
					  psStubPBDesc->ppsSubKernelMemInfos,
					  0);
			psStubPBDesc->ppsSubKernelMemInfos = IMG_NULL;
		}
		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_STUB_PBDESC),
				  psStubPBDesc,
				  0);
		/*not nulling pointer, out of scope*/
	}

NoAddKeepPB:
	for (i = 0; i < ui32SharedPBDescSubKernelMemInfosCount; i++)
	{
		PVRSRVFreeDeviceMemKM(hDevCookie, ppsSharedPBDescSubKernelMemInfos[i]);
	}

	PVRSRVFreeSharedSysMemoryKM(psSharedPBDescKernelMemInfo);
	PVRSRVFreeDeviceMemKM(hDevCookie, psHWPBDescKernelMemInfo);

	PVRSRVFreeSharedSysMemoryKM(psBlockKernelMemInfo);
	PVRSRVFreeDeviceMemKM(hDevCookie, psHWBlockKernelMemInfo);

	return eRet;
}

/******************************************************************************
 End of file (pb.c)
******************************************************************************/
