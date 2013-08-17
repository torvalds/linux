/*************************************************************************/ /*!
@Title          PVRPDP_EMULATOR display structures and prototypes
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
#if defined(__linux__)
#include <linux/string.h>
#else
#include <string.h>
#endif

/* IMG services headers */
#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "pvrpdp_emulator.h"

#define DISPLAY_DEVICE_NAME "PVRPDP_EMULATOR"

#define PVRPDP_EMULATOR_COMMAND_COUNT		1

/* top level 'hook ptr' */
static IMG_VOID *gpvAnchor = IMG_NULL;
static PFN_DC_GET_PVRJTABLE pfnGetPVRJTable = IMG_NULL;

/* 	
	Kernel services is a kernel module and must be loaded first.  
	The display controller driver is also a kernel module and must be loaded after the pvr services module.
	The display controller driver should be able to retrieve the 
	address of the services PVRGetDisplayClassJTable from (the already loaded)
	kernel services module.
*/

/* returns anchor pointer */
static PVRPDP_EMULATOR_DEVINFO * GetAnchorPtr(IMG_VOID)
{
	return (PVRPDP_EMULATOR_DEVINFO *)gpvAnchor;
}

/* sets anchor pointer */
static IMG_VOID SetAnchorPtr(PVRPDP_EMULATOR_DEVINFO *psDevInfo)
{
	gpvAnchor = (IMG_VOID*)psDevInfo;
}


static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32DeviceID,
                                 IMG_HANDLE *phDevice,
                                 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	PVRPDP_EMULATOR_DEVINFO *psDevInfo;
    PVR_UNREFERENCED_PARAMETER(ui32DeviceID);
	
	psDevInfo = GetAnchorPtr();
	
	/* store the system surface sync data */
	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;
	
	/* return handle to the devinfo */
	*phDevice = (IMG_HANDLE)psDevInfo;
	
	return PVRSRV_OK;	
}



static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);
	
	return PVRSRV_OK;	
}


static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE hDevice,
									IMG_UINT32 *pui32NumFormats, 
									DISPLAY_FORMAT *psFormat)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	
	if(!hDevice || !pui32NumFormats)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;		
	}

	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;
	
	*pui32NumFormats = psDevInfo->ui32NumFormats;
	
	if(psFormat)
	{
		IMG_UINT32 i;
		
		for(i=0; i<psDevInfo->ui32NumFormats; i++)
		{
			psFormat[i] = psDevInfo->asDisplayFormatList[i];
		}
	}

	return PVRSRV_OK;	
}


static PVRSRV_ERROR EnumDCDims(IMG_HANDLE hDevice, 
							DISPLAY_FORMAT *psFormat, 
							IMG_UINT32 *pui32NumDims, 
							DISPLAY_DIMS *psDim)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	
	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;		
	}

	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;

	*pui32NumDims = psDevInfo->ui32NumDims;

	/* given psFormat return the available Dims */
	if(psFormat);
	
	if(psDim)
	{
		IMG_UINT32 i;

		for(i=0; i<psDevInfo->ui32NumDims; i++)
		{
			psDim[i] = psDevInfo->asDisplayDimList[i];
		}
	}
	
	return PVRSRV_OK;	
}


static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	
	if(!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;		
	}

	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;

	*phBuffer = (IMG_HANDLE)&psDevInfo->sSystemBuffer;

	return PVRSRV_OK;	
}


static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	
	if(!hDevice || !psDCInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;		
	}

	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;

	*psDCInfo = psDevInfo->sDisplayInfo;

	return PVRSRV_OK;	
}


static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE        hDevice,
									IMG_HANDLE        hBuffer, 
									IMG_SYS_PHYADDR   **ppsSysAddr,
									IMG_UINT32        *pui32ByteSize, 
									IMG_VOID          **ppvCpuVAddr,
									IMG_HANDLE        *phOSMapInfo,
									IMG_BOOL          *pbIsContiguous,
	                                IMG_UINT32		  *pui32TilingStride)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	PVRPDP_EMULATOR_BUFFER	*psBuffer;

	PVR_UNREFERENCED_PARAMETER(pui32TilingStride);

	if(!hDevice || !hBuffer || !ppsSysAddr || !pui32ByteSize)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;		
	}

	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;

	 psBuffer = (PVRPDP_EMULATOR_BUFFER*)hBuffer;

	*ppsSysAddr = &psBuffer->sSysAddr;
	*ppvCpuVAddr = psBuffer->sCPUVAddr;

	*pui32ByteSize = psDevInfo->sFBInfo.ui32Height * psDevInfo->sFBInfo.ui32ByteStride;

	*phOSMapInfo = IMG_NULL;
	*pbIsContiguous = IMG_TRUE;

	return PVRSRV_OK;
}


static PVRSRV_ERROR CreateDCSwapChain(IMG_HANDLE hDevice,
										IMG_UINT32 ui32Flags, 
										DISPLAY_SURF_ATTRIBUTES *psDstSurfAttrib, 
										DISPLAY_SURF_ATTRIBUTES *psSrcSurfAttrib,
										IMG_UINT32 ui32BufferCount, 
										PVRSRV_SYNC_DATA **ppsSyncData,
										IMG_UINT32 ui32OEMFlags,
										IMG_HANDLE *phSwapChain, 
										IMG_UINT32 *pui32SwapChainID)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	PVRPDP_EMULATOR_SWAPCHAIN *psSwapChain;
	PVRPDP_EMULATOR_BUFFER *psBuffer;
	IMG_UINT32 i;
			
	PVR_UNREFERENCED_PARAMETER(ui32OEMFlags);	
	PVR_UNREFERENCED_PARAMETER(pui32SwapChainID);
	
	/* check parameters */
	if(!hDevice 
	|| !psDstSurfAttrib 
	|| !psSrcSurfAttrib 
	|| !ppsSyncData 
	|| !phSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;	
	}
	
	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;
	
	/* the pdp only supports a single swapchain */
	if(psDevInfo->psSwapChain)
	{
		return PVRSRV_ERROR_FLIP_CHAIN_EXISTS;	
	}
	
	/* check the buffer count */
	if(ui32BufferCount > PVRPDP_EMULATOR_MAX_BACKBUFFERS)
	{
		return PVRSRV_ERROR_TOOMANYBUFFERS;	
	}
	
	/* 
		verify the DST/SRC attributes
		- SRC/DST must match the current display mode config
	*/
	if(psDstSurfAttrib->pixelformat != psDevInfo->sSysFormat.pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psDevInfo->sSysDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psDevInfo->sSysDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psDevInfo->sSysDims.ui32Height)
	{
		/* DST doesn't match the current mode */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}		

	if(psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psSrcSurfAttrib->sDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psSrcSurfAttrib->sDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psSrcSurfAttrib->sDims.ui32Height)
	{
		/* DST doesn't match the SRC */
		return PVRSRV_ERROR_INVALID_PARAMS;
	}		

	/* INTEGRATION_POINT: check the flags */
	PVR_UNREFERENCED_PARAMETER(ui32Flags);
	
	/* create a swapchain structure */
	psSwapChain = (PVRPDP_EMULATOR_SWAPCHAIN*)AllocKernelMem(sizeof(PVRPDP_EMULATOR_SWAPCHAIN));
	if(!psSwapChain)
	{
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psBuffer = (PVRPDP_EMULATOR_BUFFER*)AllocKernelMem(sizeof(PVRPDP_EMULATOR_BUFFER) * ui32BufferCount);
	if(!psBuffer)
	{
		FreeKernelMem(psSwapChain);
		return PVRSRV_ERROR_OUT_OF_MEMORY;
	}

	psSwapChain->ui32BufferCount = ui32BufferCount;
	psSwapChain->psBuffer = psBuffer;
	
	/* link the buffers */
	for(i=0; i<ui32BufferCount-1; i++)
	{
		psBuffer[i].psNext = &psBuffer[i+1];
	}
	/* and link last to first */
	psBuffer[i].psNext = &psBuffer[0];
	
	/* populate the buffers */
	for(i=0; i<ui32BufferCount; i++)
	{
		psBuffer[i].psSyncData = ppsSyncData[i];
		psBuffer[i].sSysAddr = psDevInfo->asBackBuffers[i].sSysAddr;
		psBuffer[i].sDevVAddr = psDevInfo->asBackBuffers[i].sDevVAddr;
		psBuffer[i].sCPUVAddr = psDevInfo->asBackBuffers[i].sCPUVAddr;
		psBuffer[i].hSwapChain = (IMG_HANDLE)psSwapChain;
	}

	/* mark swapchain's existence */
	psDevInfo->psSwapChain = psSwapChain;

	/* return swapchain handle */
	*phSwapChain = (IMG_HANDLE)psSwapChain;
	
	/* INTEGRATION_POINT: enable Vsync ISR */
#if defined(PDP_EMULATOR_USING_INTERRUPTS)
	EnableVSyncInterrupt(psDevInfo);
#endif

	return PVRSRV_OK;
}


static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice, 
										IMG_HANDLE hSwapChain)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	PVRPDP_EMULATOR_SWAPCHAIN *psSwapChain;

	/* check parameters */
	if(!hDevice 
	|| !hSwapChain)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;	
	}
	
	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;
	psSwapChain = (PVRPDP_EMULATOR_SWAPCHAIN*)hSwapChain;
	
	/* free resources */
	FreeKernelMem(psSwapChain->psBuffer);
	FreeKernelMem(psSwapChain);
	
	/* mark swapchain as not existing */
	psDevInfo->psSwapChain = IMG_NULL;
	
	return PVRSRV_OK;
}


static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice, 
								IMG_HANDLE hSwapChain, 
								IMG_RECT *psRect)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);	
	PVR_UNREFERENCED_PARAMETER(hSwapChain);	
	PVR_UNREFERENCED_PARAMETER(psRect);	
	
	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice, 
								IMG_HANDLE hSwapChain, 
								IMG_RECT *psRect)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);	
	PVR_UNREFERENCED_PARAMETER(hSwapChain);	
	PVR_UNREFERENCED_PARAMETER(psRect);	

	return PVRSRV_ERROR_NOT_SUPPORTED;	
}


static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
									IMG_HANDLE hSwapChain,
									IMG_UINT32 ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);	
	PVR_UNREFERENCED_PARAMETER(hSwapChain);	
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);	

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
									IMG_HANDLE hSwapChain,
									IMG_UINT32 ui32CKColour)
{
	PVR_UNREFERENCED_PARAMETER(hDevice);	
	PVR_UNREFERENCED_PARAMETER(hSwapChain);	
	PVR_UNREFERENCED_PARAMETER(ui32CKColour);	

	return PVRSRV_ERROR_NOT_SUPPORTED;	
}


static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice, 
									IMG_HANDLE hSwapChain, 
									IMG_UINT32 *pui32BufferCount, 
									IMG_HANDLE *phBuffer)
{
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;
	PVRPDP_EMULATOR_SWAPCHAIN *psSwapChain;
	IMG_UINT32 i;
	
	/* check parameters */
	if(!hDevice 
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;	
	}
	
	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;
	psSwapChain = (PVRPDP_EMULATOR_SWAPCHAIN*)hSwapChain;
	
	/* return the buffer count */
	*pui32BufferCount = psSwapChain->ui32BufferCount;
	
	/* return the buffers */
	for(i=0; i<psSwapChain->ui32BufferCount; i++)
	{
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->psBuffer[i];
	}
	
	return PVRSRV_OK;
}


static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice, 
									IMG_HANDLE hBuffer, 
									IMG_UINT32 ui32SwapInterval,
									IMG_HANDLE hPrivateTag,
									IMG_UINT32 ui32ClipRectCount,
									IMG_RECT *psClipRect)
{
	PVRPDP_EMULATOR_DEVINFO *psDevInfo;

	PVR_UNREFERENCED_PARAMETER(ui32SwapInterval);
	PVR_UNREFERENCED_PARAMETER(hPrivateTag);	
	PVR_UNREFERENCED_PARAMETER(psClipRect);
	
	if(!hDevice 
	|| !hBuffer
	|| (ui32ClipRectCount != 0))
	{
		return PVRSRV_ERROR_INVALID_PARAMS;	
	}
	
	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)hDevice;

	PVR_UNREFERENCED_PARAMETER(hBuffer);

	return PVRSRV_OK;	
}


static IMG_BOOL ProcessFlip(IMG_HANDLE	hCmdCookie, 
							IMG_UINT32	ui32DataSize,
							IMG_VOID	*pvData)
{
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	PVRPDP_EMULATOR_DEVINFO	*psDevInfo;

	PVR_UNREFERENCED_PARAMETER(ui32DataSize);
	PVR_UNREFERENCED_PARAMETER(pvData);

	/* validate data packet */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;
	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)	
	{
		return IMG_FALSE;
	}
	else if (psFlipCmd->ui32SwapInterval > 0)
	{
#if !defined(PDP_EMULATOR_USING_INTERRUPTS)
		return IMG_FALSE;
#endif /* PDP_EMULATOR_USING_INTERRUPTS */
	}
	
	/* call command complete Callback */
	psDevInfo = (PVRPDP_EMULATOR_DEVINFO*)psFlipCmd->hExtDevice;
	psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);

	return IMG_TRUE;
}

#if defined(EMULATOR_HAS_JDISPLAY)   

static IMG_VOID InitJDisplayRegisters(PVRPDP_EMULATOR_DEVINFO *psDevInfo)
{
	IMG_UINT32  uiPixFmt;


	WriteSOCReg(psDevInfo, JDISPLAY_TIM_VBPS_ADD, 0x0001);
	WriteSOCReg(psDevInfo, JDISPLAY_TIM_VAS_ADD,  0x0002);

	WriteSOCReg(psDevInfo, JDISPLAY_TIM_VFPS_ADD, psDevInfo->sFBInfo.ui32Height+2);
	WriteSOCReg(psDevInfo, JDISPLAY_TIM_VT_ADD,   psDevInfo->sFBInfo.ui32Height+3);

	WriteSOCReg(psDevInfo, JDISPLAY_TIM_HBPS_ADD, 0x0001);
	WriteSOCReg(psDevInfo, JDISPLAY_TIM_HAS_ADD,  0x0002);

	WriteSOCReg(psDevInfo, JDISPLAY_TIM_HFPS_ADD, psDevInfo->sFBInfo.ui32Width+2);
	WriteSOCReg(psDevInfo, JDISPLAY_TIM_HT_ADD,   psDevInfo->sFBInfo.ui32Width+3);
	WriteSOCReg(psDevInfo, JDISPLAY_FRAME_SIZE_ADD, (psDevInfo->sFBInfo.ui32Height<<12) + psDevInfo->sFBInfo.ui32Width);

	WriteSOCReg(psDevInfo, JDISPLAY_TIM_VOFF_ADD, 0x0002);
	WriteSOCReg(psDevInfo, JDISPLAY_FRAME_STRIDE_ADD, psDevInfo->sFBInfo.ui32ByteStride>>2);

	switch (psDevInfo->sFBInfo.ePixelFormat)
	{
		case PVRSRV_PIXEL_FORMAT_PAL8:
			uiPixFmt = 0;
			break;

		case PVRSRV_PIXEL_FORMAT_RGB565:
			uiPixFmt = 1;
			break;

		default:
			uiPixFmt = 5;
			break;
	}

	WriteSOCReg(psDevInfo, JDISPLAY_FBCTRL_ADD, uiPixFmt+0x80000000);

	WriteSOCReg(psDevInfo, JDISPLAY_SPGKICK_ADD, 0x80000000);
}
#endif /* defined(EMULATOR_HAS_JDISPLAY) */


/*!
******************************************************************************

 @Function	InitPDPEmulator
 
 @Description specifies devices in the systems memory map
 
 @Input    psDevInfo - device data

 @Return   PVRSRV_ERROR  : 

******************************************************************************/
static PVRSRV_ERROR InitPDPEmulator(PVRPDP_EMULATOR_DEVINFO *psDevInfo)
{
	IMG_UINT32 ui32RegBaseAddr, ui32SOCBaseAddr, ui32MemBaseAddr, ui32MemSize;
	IMG_UINT32 i, *pui32Primary;

	OSGetDeviceAddresses(&ui32RegBaseAddr, &ui32SOCBaseAddr, &ui32MemBaseAddr, &ui32MemSize);

	/* Registers */
	psDevInfo->sFBInfo.sRegSysAddr.uiAddr = ui32RegBaseAddr;
	#if defined(EMULATE_ATLAS_3BAR) && (ATLAS_REV != 2)
	psDevInfo->sFBInfo.pvRegs    = MapPhysAddr(psDevInfo->sFBInfo.sRegSysAddr, PVRPDP_ATLAS_REG_SIZE);
	#else
	psDevInfo->sFBInfo.pvRegs    = MapPhysAddr(psDevInfo->sFBInfo.sRegSysAddr, PVRPDP_REG_SIZE);
	#endif
	psDevInfo->sFBInfo.sSOCSysAddr.uiAddr = ui32SOCBaseAddr;
	psDevInfo->sFBInfo.pvSOCRegs = MapPhysAddr(psDevInfo->sFBInfo.sSOCSysAddr, PVRPDP_EMULATOR_SOC_SIZE);

#if defined(PVRPDP_GET_BUFFER_DIMENSIONS)
	if (!GetBufferDimensions(&psDevInfo->sFBInfo.ui32Width, &psDevInfo->sFBInfo.ui32Height,
							&psDevInfo->sFBInfo.ePixelFormat, &psDevInfo->sFBInfo.ui32ByteStride))
	{
		return  PVRSRV_ERROR_INIT_FAILURE;
	}
#else	/* defined(DC_NOHW_GET_BUFFER_DIMENSIONS) */
	/*
	 * PDP setup : 640x480x32bpp 60Hz. Surface address = base+PVRPDP_EMULATOR_SYSSURFACE_OFFSET
	 * Note this will eventually be done in hardware on power-up
	 */
	psDevInfo->sFBInfo.ePixelFormat = PVRPDP_EMULATOR_PIXELFORMAT;
	psDevInfo->sFBInfo.ui32Width = PVRPDP_EMULATOR_WIDTH;
	psDevInfo->sFBInfo.ui32Height = PVRPDP_EMULATOR_HEIGHT;
	psDevInfo->sFBInfo.ui32ByteStride = PVRPDP_EMULATOR_STRIDE;
#endif	/* defined(DC_NOHW_GET_BUFFER_DIMENSIONS) */


	/* System Surface */
	if(ui32MemSize == 0)
	{
		/*
			If we couldn't auto-discover the memory size then resort to the old
			method of using pre-defined sizes.
		*/
		psDevInfo->sFBInfo.sSysAddr.uiAddr = ui32MemBaseAddr + PVRPDP_EMULATOR_SYSSURFACE_OFFSET;
	}
	else
	{
		psDevInfo->sFBInfo.sSysAddr.uiAddr = ui32MemBaseAddr + (ui32MemSize - PVRPDP_EMULATOR_SYSSURFACE_SIZE);
	}
	psDevInfo->sFBInfo.sCPUVAddr = MapPhysAddr(psDevInfo->sFBInfo.sSysAddr, psDevInfo->sFBInfo.ui32ByteStride * psDevInfo->sFBInfo.ui32Height);

	/* setup swapchain back buffers */
	psDevInfo->asBackBuffers[0].sSysAddr.uiAddr = psDevInfo->sFBInfo.sSysAddr.uiAddr;
	psDevInfo->asBackBuffers[0].sDevVAddr.uiAddr = PVRPDP_EMULATOR_SYSSURFACE_OFFSET;
	psDevInfo->asBackBuffers[0].sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
	psDevInfo->asBackBuffers[0].hSwapChain = IMG_NULL;
	psDevInfo->asBackBuffers[0].psSyncData = IMG_NULL;
	psDevInfo->asBackBuffers[0].psNext = IMG_NULL;

	for(i=1; i<PVRPDP_EMULATOR_MAX_BACKBUFFERS; i++)
	{
		psDevInfo->asBackBuffers[i].sSysAddr.uiAddr = psDevInfo->sFBInfo.sSysAddr.uiAddr 
													+ (i * psDevInfo->sFBInfo.ui32ByteStride * psDevInfo->sFBInfo.ui32Height);
		psDevInfo->asBackBuffers[i].sDevVAddr.uiAddr = PVRPDP_EMULATOR_SYSSURFACE_OFFSET 
													+ (i * psDevInfo->sFBInfo.ui32ByteStride * psDevInfo->sFBInfo.ui32Height);
		psDevInfo->asBackBuffers[i].sCPUVAddr = MapPhysAddr(psDevInfo->asBackBuffers[i].sSysAddr, psDevInfo->sFBInfo.ui32ByteStride * psDevInfo->sFBInfo.ui32Height);
		psDevInfo->asBackBuffers[i].hSwapChain = IMG_NULL;
		psDevInfo->asBackBuffers[i].psSyncData = IMG_NULL;
		psDevInfo->asBackBuffers[i].psNext = IMG_NULL;
	}
	
#if defined(EMULATOR_HAS_JDISPLAY)   
	InitJDisplayRegisters(psDevInfo);
#endif

	pui32Primary = (IMG_UINT32 *)psDevInfo->sFBInfo.sCPUVAddr;
	return PVRSRV_OK;
}

static IMG_VOID DeInitPDPEmulator(PVRPDP_EMULATOR_DEVINFO *psDevInfo)
{
	{
		IMG_UINT32 i;
		UnMapPhysAddr(psDevInfo->sFBInfo.sCPUVAddr, psDevInfo->sFBInfo.ui32ByteStride * psDevInfo->sFBInfo.ui32Height);
		for(i=1; i<PVRPDP_EMULATOR_MAX_BACKBUFFERS; i++)
		{
			UnMapPhysAddr(psDevInfo->asBackBuffers[i].sCPUVAddr, psDevInfo->sFBInfo.ui32ByteStride * psDevInfo->sFBInfo.ui32Height);
		}		
	}
}

PVRSRV_ERROR Init(IMG_VOID)
{
	PVRPDP_EMULATOR_DEVINFO		*psDevInfo;
	/*
		- connect to services
		- register with services
		- allocate and setup private data structure
	*/

	
	/*
		in kernel driver, data structures must be anchored to something for subsequent retrieval
		this may be a single global pointer or TLS or something else - up to you
		call API to retrieve this ptr
	*/
	
	/* 
		get the anchor pointer 
	*/
	psDevInfo = GetAnchorPtr();
	
	if (psDevInfo == IMG_NULL)
	{
		PFN_CMD_PROC	 		pfnCmdProcList[PVRPDP_EMULATOR_COMMAND_COUNT];
		IMG_UINT32				aui32SyncCountList[PVRPDP_EMULATOR_COMMAND_COUNT][2];
		
		/* allocate device info. structure */
		psDevInfo = (PVRPDP_EMULATOR_DEVINFO *)AllocKernelMem(sizeof(PVRPDP_EMULATOR_DEVINFO));

		if(!psDevInfo)
		{
			return PVRSRV_ERROR_OUT_OF_MEMORY;/* failure */
		}

		/* set the top-level anchor */
		SetAnchorPtr((IMG_VOID*)psDevInfo);

		/* set ref count */
		psDevInfo->ui32RefCount = 0;

		/* save private fbdev information structure in the dev. info. */
		if(InitPDPEmulator(psDevInfo) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_INIT_FAILURE;/* failure */
		}

		if(OpenPVRServices(&psDevInfo->hPVRServices) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_INIT_FAILURE;/* failure */
		}
		if(GetLibFuncAddr (psDevInfo->hPVRServices, "PVRGetDisplayClassJTable", &pfnGetPVRJTable) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_INIT_FAILURE;/* failure */	
		}

		/* got the kernel services function table */
		if(!(*pfnGetPVRJTable)(&psDevInfo->sPVRJTable))
		{
			return PVRSRV_ERROR_INIT_FAILURE;/* failure */	
		}

		/*
			Setup the devinfo
		*/
		psDevInfo->psSwapChain = IMG_NULL;
		psDevInfo->sDisplayInfo.ui32MinSwapInterval = 0;
#if defined(PDP_EMULATOR_USING_INTERRUPTS)
		psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 1;
#else
		psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 0;
#endif
		psDevInfo->sDisplayInfo.ui32MaxSwapChains = 0;
		psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = 1;
		strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);
	
		psDevInfo->ui32NumFormats = 1;
			
		psDevInfo->asDisplayFormatList[0].pixelformat = psDevInfo->sFBInfo.ePixelFormat;
		psDevInfo->ui32NumDims = 1;
		psDevInfo->asDisplayDimList[0].ui32Width =  psDevInfo->sFBInfo.ui32Width;
		psDevInfo->asDisplayDimList[0].ui32Height =  psDevInfo->sFBInfo.ui32Height;
		psDevInfo->asDisplayDimList[0].ui32ByteStride =  psDevInfo->sFBInfo.ui32ByteStride;
		psDevInfo->sSysFormat = psDevInfo->asDisplayFormatList[0];
		psDevInfo->sSysDims.ui32Width = psDevInfo->asDisplayDimList[0].ui32Width;
		psDevInfo->sSysDims.ui32Height = psDevInfo->asDisplayDimList[0].ui32Height;
		psDevInfo->sSysDims.ui32ByteStride = psDevInfo->asDisplayDimList[0].ui32ByteStride;
		
		
		/* Setup system buffer */
		psDevInfo->sSystemBuffer.hSwapChain = IMG_NULL;
		psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
		psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;

		/*
			setup the DC Jtable so SRVKM can call into this driver
		*/
		psDevInfo->sDCJTable.ui32TableSize = sizeof(PVRSRV_DC_SRV2DISP_KMJTABLE);
		psDevInfo->sDCJTable.pfnOpenDCDevice = OpenDCDevice;
		psDevInfo->sDCJTable.pfnCloseDCDevice = CloseDCDevice;
		psDevInfo->sDCJTable.pfnEnumDCFormats = EnumDCFormats;
		psDevInfo->sDCJTable.pfnEnumDCDims = EnumDCDims;
		psDevInfo->sDCJTable.pfnGetDCSystemBuffer = GetDCSystemBuffer;
		psDevInfo->sDCJTable.pfnGetDCInfo = GetDCInfo;
		psDevInfo->sDCJTable.pfnGetBufferAddr = GetDCBufferAddr;
		psDevInfo->sDCJTable.pfnCreateDCSwapChain = CreateDCSwapChain;
		psDevInfo->sDCJTable.pfnDestroyDCSwapChain = DestroyDCSwapChain;
		psDevInfo->sDCJTable.pfnSetDCDstRect = SetDCDstRect;
		psDevInfo->sDCJTable.pfnSetDCSrcRect = SetDCSrcRect;
		psDevInfo->sDCJTable.pfnSetDCDstColourKey = SetDCDstColourKey;
		psDevInfo->sDCJTable.pfnSetDCSrcColourKey = SetDCSrcColourKey;
		psDevInfo->sDCJTable.pfnGetDCBuffers = GetDCBuffers;
		psDevInfo->sDCJTable.pfnSwapToDCBuffer = SwapToDCBuffer;
		psDevInfo->sDCJTable.pfnSetDCState = IMG_NULL;
		
		
		/* register device with services and retrieve device index */
		if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice (&psDevInfo->sDCJTable,
															&psDevInfo->uiDeviceID ) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_DEVICE_REGISTER_FAILED;/* failure */
		}

		/*
			setup private command processing function table
		*/
		pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;
		
		/*
			and associated sync count(s)
		*/
		aui32SyncCountList[DC_FLIP_COMMAND][0] = 0;/* no writes */
		aui32SyncCountList[DC_FLIP_COMMAND][1] = 2;/* 2 reads: To / From */
		
		/*
			register private command processing functions with
			the Command Queue Manager and setup the general
			command complete function in the devinfo
		*/
		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterCmdProcList (psDevInfo->uiDeviceID,
																&pfnCmdProcList[0], 
																aui32SyncCountList,
																PVRPDP_EMULATOR_COMMAND_COUNT) != PVRSRV_OK)
		{
			return PVRSRV_ERROR_CANT_REGISTER_CALLBACK;/* failure */
		}
		
	}	

	/* increment the ref count */
	psDevInfo->ui32RefCount++;

	/* return success */		
	return PVRSRV_OK;
}

/*
 *	PDP Emulator DeInit
 *	deinitialises the display class device component of the FBDev
 */
PVRSRV_ERROR Deinit(IMG_VOID)
{
	PVRPDP_EMULATOR_DEVINFO *psDevInfo, *psDevFirst;

	psDevFirst = GetAnchorPtr();
	psDevInfo = psDevFirst;

	/* check DevInfo has been setup */
	if (psDevInfo == IMG_NULL)
	{
		return PVRSRV_ERROR_INVALID_DEVINFO;/* failure */
	}

	/* decrement ref count */
	psDevInfo->ui32RefCount--;

	if (psDevInfo->ui32RefCount == 0)
	{
		PVRSRV_ERROR eError;
		
		/* all references gone - de-init device information */
		PVRSRV_DC_DISP2SRV_KMJTABLE	*psJTable = &psDevInfo->sPVRJTable;

		eError = psDevInfo->sPVRJTable.pfnPVRSRVRemoveCmdProcList (psDevInfo->uiDeviceID,
																PVRPDP_EMULATOR_COMMAND_COUNT);
		if (eError != PVRSRV_OK)
		{
			return eError;/* failure */
		}

		/* Remove display class device from kernel services device register */
		eError = psJTable->pfnPVRSRVRemoveDCDevice(psDevInfo->uiDeviceID);
		if (eError != PVRSRV_OK)
		{
			return eError;/* failure */
		}
		
		DeInitPDPEmulator(psDevInfo);

		eError = ClosePVRServices(psDevInfo->hPVRServices);
		if (eError != PVRSRV_OK)
		{
			psDevInfo->hPVRServices = IMG_NULL;
			return eError;/* failure */
		}

		/* de-allocate data structure */
		FreeKernelMem(psDevInfo);
	}
	
	/* clear the top-level anchor */
	SetAnchorPtr(IMG_NULL);

	/* return success */
	return PVRSRV_OK;
}


/******************************************************************************
 End of file (pvrpdp-emulator-displayclass.c)
******************************************************************************/

