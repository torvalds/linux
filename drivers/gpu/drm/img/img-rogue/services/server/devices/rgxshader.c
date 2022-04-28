/*************************************************************************/ /*!
@File           rgxshader.c
@Title          TQ Shader Load
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Shader code and info are shared for all context on the device.
                If allocation doesn't already exist, read shader data from file
                and allocate PMR memory. PMR memory is not deallocated until
                device deinit.
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

#include "rgxshader.h"
#include "osfunc_common.h"
#include "rgxdevice.h"
#include "pdump_km.h"
#include "physmem.h"
#include "ri_server.h"
#include "pvr_ricommon.h"

static void
RGXShaderReadHeader(OS_FW_IMAGE *psShaderFW, RGX_SHADER_HEADER *psHeader)
{
	const void * pvData;

	pvData = OSFirmwareData(psShaderFW);

	OSDeviceMemCopy(psHeader, pvData, sizeof(RGX_SHADER_HEADER));
}

static size_t
RGXShaderCLIMemSize(OS_FW_IMAGE *psShaderFW)
{
	RGX_SHADER_HEADER sHeader;

	RGXShaderReadHeader(psShaderFW, &sHeader);

	return sHeader.ui32SizeClientMem;
}

static size_t
RGXShaderUSCMemSize(OS_FW_IMAGE *psShaderFW)
{
	RGX_SHADER_HEADER sHeader;

	RGXShaderReadHeader(psShaderFW, &sHeader);

	return sHeader.ui32SizeFragment;
}

static void *
RGXShaderCLIMem(OS_FW_IMAGE *psShaderFW)
{
	return (void*)OSFirmwareData(psShaderFW);
}

static void *
RGXShaderUSCMem(OS_FW_IMAGE *psShaderFW)
{
	IMG_PBYTE pui8Data;

	pui8Data = (IMG_PBYTE)OSFirmwareData(psShaderFW);

	pui8Data += RGXShaderCLIMemSize(psShaderFW);

	return (void*) pui8Data;
}

#define RGX_SHADER_FILENAME_MAX_SIZE   ((sizeof(RGX_SH_FILENAME)+	\
										 RGX_BVNC_STR_SIZE_MAX))

static void
_GetShaderFileName(PVRSRV_DEVICE_NODE * psDeviceNode,
				   IMG_CHAR           * pszShaderFilenameStr,
				   IMG_CHAR           * pszShaderpFilenameStr)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	OSSNPrintf(pszShaderFilenameStr, RGX_SHADER_FILENAME_MAX_SIZE,
			   "%s." RGX_BVNC_STR_FMTSPEC,
			   RGX_SH_FILENAME,
			   psDevInfo->sDevFeatureCfg.ui32B, psDevInfo->sDevFeatureCfg.ui32V,
			   psDevInfo->sDevFeatureCfg.ui32N, psDevInfo->sDevFeatureCfg.ui32C);

	OSSNPrintf(pszShaderpFilenameStr, RGX_SHADER_FILENAME_MAX_SIZE,
			   "%s." RGX_BVNC_STRP_FMTSPEC,
			   RGX_SH_FILENAME,
			   psDevInfo->sDevFeatureCfg.ui32B, psDevInfo->sDevFeatureCfg.ui32V,
			   psDevInfo->sDevFeatureCfg.ui32N, psDevInfo->sDevFeatureCfg.ui32C);
}

PVRSRV_ERROR
PVRSRVTQLoadShaders(PVRSRV_DEVICE_NODE * psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	OS_FW_IMAGE        *psShaderFW;
	RGX_SHADER_HEADER   sHeader;
	IMG_UINT32          ui32MappingTable = 0;
	IMG_UINT32          ui32NumPages;
	IMG_CHAR            aszShaderFilenameStr[RGX_SHADER_FILENAME_MAX_SIZE];
	IMG_CHAR            aszShaderpFilenameStr[RGX_SHADER_FILENAME_MAX_SIZE];
	const IMG_CHAR      *pszShaderFilenameStr = aszShaderFilenameStr;
	size_t              uiNumBytes;
	PVRSRV_ERROR        eError;

	_GetShaderFileName(psDeviceNode, aszShaderFilenameStr, aszShaderpFilenameStr);

	eError = OSLoadFirmware(psDeviceNode, aszShaderFilenameStr, NULL, &psShaderFW);

	if (eError != PVRSRV_OK)
	{
		eError = OSLoadFirmware(psDeviceNode, aszShaderpFilenameStr,
		                        NULL, &psShaderFW);
		if (eError != PVRSRV_OK)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to load shader binary file %s (%s)",
			         __func__,
			         aszShaderpFilenameStr,
			         PVRSRVGetErrorString(eError)));
			eError = PVRSRV_ERROR_UNABLE_TO_FIND_RESOURCE;
			goto failed_init;
		}

		pszShaderFilenameStr = aszShaderpFilenameStr;
	}

	PVR_LOG(("Shader binary image '%s' loaded", pszShaderFilenameStr));

	RGXShaderReadHeader(psShaderFW, &sHeader);

	ui32NumPages = (sHeader.ui32SizeFragment / RGX_BIF_PM_PHYSICAL_PAGE_SIZE) + 1;

	PDUMPCOMMENT("Allocate TDM USC PMR Block (Pages %08X)", ui32NumPages);

	eError = PhysmemNewRamBackedPMR(NULL,
									psDeviceNode,
									(IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE,
									(IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE,
									1,
									1,
									&ui32MappingTable,
									RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
									PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE
									| PVRSRV_MEMALLOCFLAG_GPU_READABLE
									| PVRSRV_MEMALLOCFLAG_GPU_CACHE_INCOHERENT
									| PVRSRV_MEMALLOCFLAG_VAL_SHARED_BUFFER,
									sizeof("tquscpmr"),
									"tquscpmr",
									PVR_SYS_ALLOC_PID,
									(PMR**)&psDevInfo->hTQUSCSharedMem,
									PDUMP_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from PhysmemNewRamBackedPMR (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto failed_firmware;
	}

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	eError = RIWritePMREntryWithOwnerKM(psDevInfo->hTQUSCSharedMem, PVR_SYS_ALLOC_PID);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RIWritePMREntryWithOwnerKM (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto failed_uscpmr;
	}
#endif

	eError = PMR_WriteBytes(psDevInfo->hTQUSCSharedMem, 0, RGXShaderUSCMem(psShaderFW), RGXShaderUSCMemSize(psShaderFW), &uiNumBytes);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from PMR_WriteBytes (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto failed_uscpmr;
	}

	ui32NumPages = (sHeader.ui32SizeClientMem / RGX_BIF_PM_PHYSICAL_PAGE_SIZE) + 1;

	PDUMPCOMMENT("Allocate TDM Client PMR Block (Pages %08X)", ui32NumPages);

	eError = PhysmemNewRamBackedPMR(NULL,
									psDeviceNode,
									(IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE,
									(IMG_DEVMEM_SIZE_T)ui32NumPages * RGX_BIF_PM_PHYSICAL_PAGE_SIZE,
									1,
									1,
									&ui32MappingTable,
									RGX_BIF_PM_PHYSICAL_PAGE_ALIGNSHIFT,
									PVRSRV_MEMALLOCFLAG_KERNEL_CPU_MAPPABLE
									| PVRSRV_MEMALLOCFLAG_CPU_READABLE
									| PVRSRV_MEMALLOCFLAG_CPU_CACHE_INCOHERENT
									| PVRSRV_MEMALLOCFLAG_VAL_SHARED_BUFFER,
									sizeof("tqclipmr"),
									"tqclipmr",
									PVR_SYS_ALLOC_PID,
									(PMR**)&psDevInfo->hTQCLISharedMem,
									PDUMP_NONE);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from PhysmemNewRamBackedPMR (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto failed_uscpmr;
	}

#if defined(PVRSRV_ENABLE_GPU_MEMORY_INFO)
	eError = RIWritePMREntryWithOwnerKM(psDevInfo->hTQCLISharedMem, PVR_SYS_ALLOC_PID);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from RIWritePMREntryWithOwnerKM (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto failed_clipmr;
	}
#endif

	eError = PMR_WriteBytes(psDevInfo->hTQCLISharedMem, 0, RGXShaderCLIMem(psShaderFW), RGXShaderCLIMemSize(psShaderFW), &uiNumBytes);
	if (eError != PVRSRV_OK)
	{
		PVR_LOG(("%s: Unexpected error from PMR_WriteBytes (%s)",
				 __func__,
				 PVRSRVGetErrorString(eError)));
		goto failed_clipmr;
	}

	OSUnloadFirmware(psShaderFW);

	PVR_ASSERT(psDevInfo->hTQUSCSharedMem != NULL);
	PVR_ASSERT(psDevInfo->hTQCLISharedMem != NULL);

	return PVRSRV_OK;

failed_clipmr:
	PMRUnrefPMR(psDevInfo->hTQCLISharedMem);
failed_uscpmr:
	PMRUnrefPMR(psDevInfo->hTQUSCSharedMem);
failed_firmware:
	OSUnloadFirmware(psShaderFW);
failed_init:
	return eError;
}

void
PVRSRVTQAcquireShaders(PVRSRV_DEVICE_NODE  * psDeviceNode,
					   PMR                ** ppsCLIPMRMem,
					   PMR                ** ppsUSCPMRMem)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;

	PVR_ASSERT(psDevInfo->hTQUSCSharedMem != NULL);
	PVR_ASSERT(psDevInfo->hTQCLISharedMem != NULL);

	*ppsUSCPMRMem = psDevInfo->hTQUSCSharedMem;
	*ppsCLIPMRMem = psDevInfo->hTQCLISharedMem;
}

PVRSRV_ERROR
PVRSRVTQUnloadShaders(PVRSRV_DEVICE_NODE * psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError;

	eError = PMRUnrefPMR(psDevInfo->hTQUSCSharedMem);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	eError = PMRUnrefPMR(psDevInfo->hTQCLISharedMem);
	if (eError != PVRSRV_OK)
	{
		return eError;
	}

	return PVRSRV_OK;
}
