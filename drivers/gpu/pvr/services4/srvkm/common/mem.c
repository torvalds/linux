/*************************************************************************/ /*!
@Title          System memory functions
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System memory allocation APIs
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

#include "services_headers.h"
#include "pvr_bridge_km.h"


static PVRSRV_ERROR
FreeSharedSysMemCallBack(IMG_PVOID  pvParam,
						 IMG_UINT32 ui32Param,
						 IMG_BOOL   bDummy)
{
	PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo = pvParam;

	PVR_UNREFERENCED_PARAMETER(ui32Param);
	PVR_UNREFERENCED_PARAMETER(bDummy);

	OSFreePages(psKernelMemInfo->ui32Flags,
				psKernelMemInfo->uAllocSize,
				psKernelMemInfo->pvLinAddrKM,
				psKernelMemInfo->sMemBlk.hOSMemHandle);

	OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
			  sizeof(PVRSRV_KERNEL_MEM_INFO),
			  psKernelMemInfo,
			  IMG_NULL);
	/*not nulling pointer, copy on stack*/

	return PVRSRV_OK;
}


IMG_EXPORT PVRSRV_ERROR
PVRSRVAllocSharedSysMemoryKM(PVRSRV_PER_PROCESS_DATA	*psPerProc,
							 IMG_UINT32					ui32Flags,
							 IMG_SIZE_T 				uSize,
							 PVRSRV_KERNEL_MEM_INFO 	**ppsKernelMemInfo)
{
	PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

	if(OSAllocMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(PVRSRV_KERNEL_MEM_INFO),
				  (IMG_VOID **)&psKernelMemInfo, IMG_NULL,
				  "Kernel Memory Info") != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR,"PVRSRVAllocSharedSysMemoryKM: Failed to alloc memory for meminfo"));
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	OSMemSet(psKernelMemInfo, 0, sizeof(*psKernelMemInfo));

	ui32Flags &= ~PVRSRV_HAP_MAPTYPE_MASK;
	ui32Flags |= PVRSRV_HAP_MULTI_PROCESS;
	psKernelMemInfo->ui32Flags = ui32Flags;
	psKernelMemInfo->uAllocSize = uSize;

	if(OSAllocPages(psKernelMemInfo->ui32Flags,
					psKernelMemInfo->uAllocSize,
					(IMG_UINT32)HOST_PAGESIZE(),
					IMG_NULL,
					0,
					IMG_NULL,
					&psKernelMemInfo->pvLinAddrKM,
					&psKernelMemInfo->sMemBlk.hOSMemHandle)
		!= PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRSRVAllocSharedSysMemoryKM: Failed to alloc memory for block"));
		OSFreeMem(PVRSRV_OS_PAGEABLE_HEAP,
				  sizeof(PVRSRV_KERNEL_MEM_INFO),
				  psKernelMemInfo,
				  0);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	/* register with the resman */
	psKernelMemInfo->sMemBlk.hResItem =
				ResManRegisterRes(psPerProc->hResManContext,
								  RESMAN_TYPE_SHARED_MEM_INFO,
								  psKernelMemInfo,
								  0,
								  &FreeSharedSysMemCallBack);

	*ppsKernelMemInfo = psKernelMemInfo;

	return PVRSRV_OK; 
}


IMG_EXPORT PVRSRV_ERROR
PVRSRVFreeSharedSysMemoryKM(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVRSRV_ERROR eError;

	if(psKernelMemInfo->sMemBlk.hResItem)
	{
		eError = ResManFreeResByPtr(psKernelMemInfo->sMemBlk.hResItem, CLEANUP_WITH_POLL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeSharedSysMemoryKM: ResManFreeResByPtr failed %d", eError));
			PVR_DPF((PVR_DBG_ERROR, "ResManFreeResByPtr: hResItem 0x%x", (unsigned int)psKernelMemInfo->sMemBlk.hResItem));
		}
	}
	else
	{
		eError = FreeSharedSysMemCallBack(psKernelMemInfo, 0, CLEANUP_WITH_POLL);
		if(eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "PVRSRVFreeSharedSysMemoryKM: FreeSharedSysMemCallBack failed %d", eError));
			PVR_DPF((PVR_DBG_ERROR, "FreeSharedSysMemCallBack: psKernelMemInfo 0x%x", (unsigned int)psKernelMemInfo));
		}
	}

	return eError;
}


IMG_EXPORT PVRSRV_ERROR
PVRSRVDissociateMemFromResmanKM(PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if(!psKernelMemInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	if(psKernelMemInfo->sMemBlk.hResItem)
	{
		eError = ResManDissociateRes(psKernelMemInfo->sMemBlk.hResItem, IMG_NULL);

		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR,"PVRSRVDissociateMemFromResmanKM: ResManDissociateRes failed"));
			PVR_DBG_BREAK;
			return eError;
		}

		psKernelMemInfo->sMemBlk.hResItem = IMG_NULL;
	}

	return eError;
}

/******************************************************************************
 End of file (mem.c)
******************************************************************************/
