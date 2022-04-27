/*************************************************************************/ /*!
@File           rgxmulticore.c
@Title          Functions related to multicore devices
@Codingstyle    IMG
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Kernel mode workload estimation functionality.
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

#include "rgxdevice.h"
#include "rgxdefs_km.h"
#include "pdump_km.h"
#include "rgxmulticore.h"
#include "multicore_defs.h"
#include "allocmem.h"
#include "pvr_debug.h"

/*
 * check that register defines match our hardcoded definitions.
 * Rogue has these, volcanic does not.
 */
#if ((RGX_MULTICORE_CAPABILITY_FRAGMENT_EN != RGX_CR_MULTICORE_GPU_CAPABILITY_FRAGMENT_EN) || \
     (RGX_MULTICORE_CAPABILITY_GEOMETRY_EN != RGX_CR_MULTICORE_GPU_CAPABILITY_GEOMETRY_EN) || \
     (RGX_MULTICORE_CAPABILITY_COMPUTE_EN  != RGX_CR_MULTICORE_GPU_CAPABILITY_COMPUTE_EN) || \
     (RGX_MULTICORE_CAPABILITY_PRIMARY_EN  != RGX_CR_MULTICORE_GPU_CAPABILITY_PRIMARY_EN) || \
     (RGX_MULTICORE_ID_CLRMSK              != RGX_CR_MULTICORE_GPU_ID_CLRMSK))
#error "Rogue definitions for RGX_CR_MULTICORE_GPU register have changed"
#endif


static PVRSRV_ERROR RGXGetMultiCoreInfo(PVRSRV_DEVICE_NODE *psDeviceNode,
                                        IMG_UINT32 ui32CapsSize,
                                        IMG_UINT32 *pui32NumCores,
                                        IMG_UINT64 *pui64Caps);


/*
 * RGXInitMultiCoreInfo:
 * Return multicore information to clients.
 * Return not_supported on cores without multicore.
 */
static PVRSRV_ERROR RGXGetMultiCoreInfo(PVRSRV_DEVICE_NODE *psDeviceNode,
                                 IMG_UINT32 ui32CapsSize,
                                 IMG_UINT32 *pui32NumCores,
                                 IMG_UINT64 *pui64Caps)
{
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psDeviceNode->ui32MultiCoreNumCores == 0)
	{
		/* MULTICORE not supported on this device */
		eError = PVRSRV_ERROR_NOT_SUPPORTED;
	}
	else
	{
		*pui32NumCores = psDeviceNode->ui32MultiCoreNumCores;
		if (ui32CapsSize > 0)
		{
			if (ui32CapsSize < psDeviceNode->ui32MultiCoreNumCores)
			{
				PVR_DPF((PVR_DBG_ERROR, "Multicore caps buffer too small"));
				eError = PVRSRV_ERROR_BRIDGE_BUFFER_TOO_SMALL;
			}
			else
			{
				IMG_UINT32 i;

				for (i = 0; i < psDeviceNode->ui32MultiCoreNumCores; ++i)
				{
					pui64Caps[i] = psDeviceNode->pui64MultiCoreCapabilities[i];
				}
			}
		}
	}

	return eError;
}



/*
 * RGXInitMultiCoreInfo:
 * Read multicore HW registers and fill in data structure for clients.
 * Return not supported on cores without multicore.
 */
PVRSRV_ERROR RGXInitMultiCoreInfo(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	PVRSRV_RGXDEV_INFO *psDevInfo = psDeviceNode->pvDevice;
	PVRSRV_ERROR eError = PVRSRV_OK;

	if (psDeviceNode->pfnGetMultiCoreInfo != NULL)
	{
		/* we only set this up once */
		return PVRSRV_OK;
	}

	/* defaults for non-multicore devices */
	psDeviceNode->ui32MultiCoreNumCores = 0;
	psDeviceNode->ui32MultiCorePrimaryId = (IMG_UINT32)(-1);
	psDeviceNode->pui64MultiCoreCapabilities = NULL;
	psDeviceNode->pfnGetMultiCoreInfo = NULL;

	if (RGX_IS_FEATURE_SUPPORTED(psDevInfo, GPU_MULTICORE_SUPPORT))
	{
		IMG_UINT32 ui32MulticoreRegBankOffset = (1 << RGX_GET_FEATURE_VALUE(psDevInfo, XPU_MAX_REGBANKS_ADDR_WIDTH));
		IMG_UINT32 ui32MulticoreGPUReg = RGX_CR_MULTICORE_GPU;
		IMG_UINT32 ui32NumCores;
		IMG_UINT32 i;

		ui32NumCores = OSReadHWReg32(psDevInfo->pvRegsBaseKM, RGX_CR_MULTICORE_SYSTEM);
#if !defined(NO_HARDWARE)
		/* check that the number of cores reported is in-bounds */
		if (ui32NumCores > (RGX_CR_MULTICORE_SYSTEM_MASKFULL >> RGX_CR_MULTICORE_SYSTEM_GPU_COUNT_SHIFT))
		{
			PVR_DPF((PVR_DBG_ERROR, "invalid return (%u) read from MULTICORE_SYSTEM", ui32NumCores));
			return PVRSRV_ERROR_DEVICE_REGISTER_FAILED;
		}
#else
		/* for nohw set to max so clients can allocate enough memory for all pdump runs on any config */
		ui32NumCores = RGX_MULTICORE_MAX_NOHW_CORES;
#endif
		PVR_DPF((PVR_DBG_MESSAGE, "Multicore system has %u cores", ui32NumCores));
		PDUMPCOMMENT("RGX Multicore has %d cores\n", ui32NumCores);

		/* allocate storage for capabilities */
		psDeviceNode->pui64MultiCoreCapabilities = OSAllocMem(ui32NumCores * sizeof(psDeviceNode->pui64MultiCoreCapabilities[0]));
		if (psDeviceNode->pui64MultiCoreCapabilities == NULL)
		{
			PVR_DPF((PVR_DBG_ERROR, "%s: Failed to alloc memory for Multicore info", __func__));
			return PVRSRV_ERROR_OUT_OF_MEMORY;
		}

		psDeviceNode->ui32MultiCoreNumCores = ui32NumCores;

		for (i = 0; i < ui32NumCores; ++i)
		{
	#if !defined(NO_HARDWARE)
			psDeviceNode->pui64MultiCoreCapabilities[i] =
							OSReadHWReg64(psDevInfo->pvRegsBaseKM, ui32MulticoreGPUReg) & RGX_CR_MULTICORE_GPU_MASKFULL;
	#else
			/* emulation for what we think caps are */
			psDeviceNode->pui64MultiCoreCapabilities[i] =
							   i | ((i == 0) ? (RGX_MULTICORE_CAPABILITY_PRIMARY_EN
											  | RGX_MULTICORE_CAPABILITY_GEOMETRY_EN) : 0)
							   | RGX_MULTICORE_CAPABILITY_COMPUTE_EN
							   | RGX_MULTICORE_CAPABILITY_FRAGMENT_EN;
	#endif
			PVR_DPF((PVR_DBG_MESSAGE, "Core %d has capabilities value 0x%x", i, (IMG_UINT32)psDeviceNode->pui64MultiCoreCapabilities[i] ));
			PDUMPCOMMENT("\tCore %d has caps 0x%08x\n", i, (IMG_UINT32)psDeviceNode->pui64MultiCoreCapabilities[i]);

			if (psDeviceNode->pui64MultiCoreCapabilities[i] & RGX_CR_MULTICORE_GPU_CAPABILITY_PRIMARY_EN)
			{
				psDeviceNode->ui32MultiCorePrimaryId = (psDeviceNode->pui64MultiCoreCapabilities[i]
														& ~RGX_CR_MULTICORE_GPU_ID_CLRMSK)
														>> RGX_CR_MULTICORE_GPU_ID_SHIFT;
			}

			ui32MulticoreGPUReg += ui32MulticoreRegBankOffset;
		}

		/* Register callback to return info about multicore setup to client bridge */
		psDeviceNode->pfnGetMultiCoreInfo = RGXGetMultiCoreInfo;
	}
	else
	{
		/* MULTICORE not supported on this device */
		eError = PVRSRV_ERROR_NOT_SUPPORTED;
	}

	return eError;
}


/*
 * RGXDeinitMultiCoreInfo:
 * Release resources and clear the MultiCore values in the DeviceNode.
 */
void RGXDeInitMultiCoreInfo(PVRSRV_DEVICE_NODE *psDeviceNode)
{
	if (psDeviceNode->pui64MultiCoreCapabilities != NULL)
	{
		OSFreeMem(psDeviceNode->pui64MultiCoreCapabilities);
		psDeviceNode->pui64MultiCoreCapabilities = NULL;
		psDeviceNode->ui32MultiCoreNumCores = 0;
		psDeviceNode->ui32MultiCorePrimaryId = (IMG_UINT32)(-1);
	}
	psDeviceNode->pfnGetMultiCoreInfo = NULL;
}
