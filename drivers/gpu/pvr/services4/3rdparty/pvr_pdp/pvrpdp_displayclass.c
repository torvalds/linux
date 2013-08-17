/*************************************************************************/ /*!
@Title          PVRPDP common driver functions
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

/**************************************************************************
 The 3rd party driver is a specification of an API to integrate the IMG POWERVR
 Services driver with 3rd Party display hardware.  It is NOT a specification for
 a display controller driver, rather a specification to extend the API for a
 pre-existing driver for the display hardware.
 
 The 3rd party driver interface provides IMG POWERVR client drivers (e.g. PVR2D)
 with an API abstraction of the system's underlying display hardware, allowing
 the client drivers to indirectly control the display hardware and access its
 associated memory.
 
 Functions of the API include
 - query primary surface attributes (width, height, stride, pixel format, CPU
	 physical and virtual address)
 - swap/flip chain creation and subsequent query of surface attributes
 - asynchronous display surface flipping, taking account of asynchronous read
 (flip) and write (render) operations to the display surface
 
 Note: having queried surface attributes the client drivers are able to map the
 display memory to any IMG POWERVR Services device by calling
 PVRSRVMapDeviceClassMemory with the display surface handle.
 
 This code is intended to be an example of how a pre-existing display driver may
 be extended to support the 3rd Party Display interface to POWERVR Services
 - IMG is not providing a display driver implementation.
 **************************************************************************/

#if defined(__linux__)
#include <linux/string.h>
#else
#include <string.h>
#endif

/* IMG services headers */
#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "pvrpdp.h"

#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
#include "vesagtf.h"
#endif


/*
	Kernel services is a kernel module and must be loaded first.
	The display controller driver is also a kernel module and must be loaded after the pvr services module.
	The display controller driver should be able to retrieve the
	address of the services PVRGetDisplayClassJTable from (the already loaded)
	kernel services module.
*/

#define DISPLAY_DEVICE_NAME "PVRPDP"

/* the number of command types to register with PowerVR Services (1, flip) */
#define PVRPDP_COMMAND_COUNT		1

/* top level 'hook ptr' */
static PVRPDP_DEVINFO *gpsDevInfo = 0;

/* PowerVR services callback table ptr */
static PFN_DC_GET_PVRJTABLE pfnGetPVRJTable = 0;

/* local function prototypes */
static PDP_ERROR InitPDP(PVRPDP_DEVINFO *psDevInfo);
static void DeInitPDP(PVRPDP_DEVINFO    *psDevInfo);
#if !defined(PDP_DEVICE_POWER)
PVRSRV_ERROR PDPPrePower (IMG_HANDLE				hDevHandle,
						  PVRSRV_DEV_POWER_STATE	eNewPowerState,
						  PVRSRV_DEV_POWER_STATE	eCurrentPowerState);

PVRSRV_ERROR PDPPostPower (IMG_HANDLE				hDevHandle,
						   PVRSRV_DEV_POWER_STATE	eNewPowerState,
						   PVRSRV_DEV_POWER_STATE	eCurrentPowerState);
#endif


/************************************************************
	PDP utility functions:
************************************************************/

/* Advance an index into the Vsync flip array */
static void AdvanceFlipIndex(PVRPDP_DEVINFO *psDevInfo,
							 unsigned long  *pulIndex)
{
	unsigned long	ulMaxFlipIndex;
	
	ulMaxFlipIndex = psDevInfo->psSwapChain->ulBufferCount - 1;
	if (ulMaxFlipIndex >= PVRPDP_MAX_BACKBUFFERS)
	{
		ulMaxFlipIndex = PVRPDP_MAX_BACKBUFFERS-1;
	}
	
	(*pulIndex)++;
	
	if (*pulIndex > ulMaxFlipIndex )
	{
		*pulIndex = 0;
	}
}

/* flip to buffer function */
static PDP_ERROR Flip (PVRPDP_DEVINFO   *psDevInfo,
					   IMG_DEV_VIRTADDR *psDevVAddr)
{
	/* check parameters */
	if(!psDevInfo || !psDevVAddr || psDevVAddr->uiAddr == 0)
	{
		return (PDP_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo->sDisplayDevVAddr = *psDevVAddr;
	WriteReg(psDevInfo,
			 PVRPDP_STR1ADDRCTRL,
			 PVRPDP_STR1ADDRCTRL_STREAMENABLE
			 | (psDevInfo->sDisplayDevVAddr.uiAddr >> PVRPDP_STR1ADDRCTRL_ADDR_ALIGNSHIFT));
	
	return (PDP_OK);
}

#if defined(SYS_USING_INTERRUPTS)
/* function to enable Vsync interrupts */
static void EnableVSyncInterrupt(PVRPDP_DEVINFO *psDevInfo)
{
	/* Enable Vsync ISR */
	unsigned long ulInterruptEnable = ReadReg(psDevInfo, PVRPDP_INTENABLE);
	
	ulInterruptEnable |= (1UL << PVRPDP_INTE_VBLNK1_SHIFT);
	WriteReg(psDevInfo, PVRPDP_INTENABLE, ulInterruptEnable);
}

/* function to disable Vsync interrupts */
static IMG_VOID DisableVSyncInterrupt(PVRPDP_DEVINFO *psDevInfo)
{
	/* Disable Vsync ISR */
	unsigned long ulInterruptEnable = ReadReg(psDevInfo, PVRPDP_INTENABLE);
	
	ulInterruptEnable &= ~(1UL << PVRPDP_INTE_VBLNK1_SHIFT);
	WriteReg(psDevInfo, PVRPDP_INTENABLE, ulInterruptEnable);
}
#endif /* SYS_USING_INTERRUPTS */

/* reset internal flip queue function */
static PDP_ERROR ResetVSyncFlipItems(PVRPDP_DEVINFO* psDevInfo)
{
	unsigned long i;
	
	psDevInfo->ulInsertIndex = 0;
	psDevInfo->ulRemoveIndex = 0;
	
	for(i=0; i < PVRPDP_MAX_BACKBUFFERS; i++)
	{
		psDevInfo->asVSyncFlips[i].bValid = PDP_FALSE;
		psDevInfo->asVSyncFlips[i].bFlipped = PDP_FALSE;
		psDevInfo->asVSyncFlips[i].bCmdCompleted = PDP_FALSE;
	}
	
	return (PDP_OK);
}

/*
	function to flush all items out of the VSYNC queue.
	Apply pfFlipAction on each flip item.
*/
static void FlushInternalVSyncQueue(PVRPDP_DEVINFO *psDevInfo)
{
	PVRPDP_VSYNC_FLIP_ITEM*  psFlipItem;

#if defined(SYS_USING_INTERRUPTS)
	/* Disable interrupts while we remove the internal vsync flip queue */
	DisableVSyncInterrupt(psDevInfo);
#endif
	
	/* Need to flush any flips now pending in Internal queue */
	psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
	
	while(psFlipItem->bValid)
	{
		if(psFlipItem->bFlipped == PDP_FALSE)
		{
			/* flip to new surface - flip latches on next interrupt */
			Flip (psDevInfo, psFlipItem->psDevVAddr);
		}
		
		/* command complete handler - allows dependencies for outstanding flips to be updated -
		   doesn't matter that vsync interrupts have been disabled.
		*/
		if(psFlipItem->bCmdCompleted == PDP_FALSE)
		{
			/*
				2nd arg == IMG_FALSE - don't schedule the MISR as we're
				just emptying the internal VsyncQueue
			*/
			psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psFlipItem->hCmdComplete, IMG_FALSE);
		}
		
		/* advance remove index */
		AdvanceFlipIndex(psDevInfo, &psDevInfo->ulRemoveIndex);
		
		/* clear item state */
		psFlipItem->bFlipped = PDP_FALSE;
		psFlipItem->bCmdCompleted = PDP_FALSE;
		psFlipItem->bValid = PDP_FALSE;
		
		/* update to next flip item */
		psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
	}
	
	psDevInfo->ulInsertIndex = 0;
	psDevInfo->ulRemoveIndex = 0;

#if defined(SYS_USING_INTERRUPTS)
	/* Enable interrupts */
	EnableVSyncInterrupt(psDevInfo);
#endif
}

/************************************************************
	PDP functions called from services via the 3rd Party
	display class interface:
************************************************************/

/* Open device function, called from services */
static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32DeviceID,
								 IMG_HANDLE *phDevice,
								 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	UNREFERENCED_PARAMETER(ui32DeviceID);
	
	/* store the system surface sync data */
	gpsDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;
	
	/* return handle to the devinfo */
	*phDevice = (IMG_HANDLE)gpsDevInfo;

#if defined (ENABLE_DISPLAY_MODE_TRACKING) && defined (SUPPORT_DYNAMIC_GTF_TIMING)
	if (Shadow_Desktop_Resolution(gpsDevInfo) != PDP_OK)
	{
		return (PVRSRV_ERROR_NOT_SUPPORTED);
	}
#endif
	return (PVRSRV_OK);
}

/* Close device function, called from services */
static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
	UNREFERENCED_PARAMETER(hDevice);
	
	/* nothing to do here */
	
	return (PVRSRV_OK);
}

/* Enumerate formats function, called from services */
static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE      hDevice,
								  IMG_UINT32      *pui32NumFormats,
								  DISPLAY_FORMAT  *psFormat)
{
	PVRPDP_DEVINFO	*psDevInfo;
	
	if(!hDevice || !pui32NumFormats)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	*pui32NumFormats = (IMG_UINT32)psDevInfo->ulNumFormats;
	
	if(psFormat)
	{
		unsigned long i;
		
		for(i=0; i<psDevInfo->ulNumFormats; i++)
		{
			psFormat[i] = psDevInfo->asDisplayFormatList[i];
		}
	}
	
	return (PVRSRV_OK);
}

/* Enumerate dims function, called from services */
static PVRSRV_ERROR EnumDCDims(IMG_HANDLE hDevice,
							   DISPLAY_FORMAT *psFormat,
							   IMG_UINT32 *pui32NumDims,
							   DISPLAY_DIMS *psDim)
{
	PVRPDP_DEVINFO	*psDevInfo;
	
	if(!hDevice || !psFormat || !pui32NumDims)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	*pui32NumDims = (IMG_UINT32)psDevInfo->ulNumDims;
	
	/* given psFormat return the available Dims */
//	if(psFormat);
	
	if(psDim)
	{
		unsigned long i;
		
		for(i=0; i<psDevInfo->ulNumDims; i++)
		{
			psDim[i] = psDevInfo->asDisplayDimList[i];
		}
	}
	
	return (PVRSRV_OK);
}

/* get the system buffer function, called from services */
static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	PVRPDP_DEVINFO	*psDevInfo;
	
	if(!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	*phBuffer = (IMG_HANDLE)&psDevInfo->sSystemBuffer;
	
	return (PVRSRV_OK);
}

/* get display info function, called from services */
static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	PVRPDP_DEVINFO	*psDevInfo;
	
	if(!hDevice || !psDCInfo)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	*psDCInfo = psDevInfo->sDisplayInfo;
	
	return (PVRSRV_OK);
}

/* get buffer address function, called from services */
static PVRSRV_ERROR GetDCBufferAddr(IMG_HANDLE        hDevice,
									IMG_HANDLE        hBuffer,
									IMG_SYS_PHYADDR   **ppsSysAddr,
									IMG_UINT32        *pui32ByteSize,
									IMG_VOID          **ppvCpuVAddr,
									IMG_HANDLE        *phOSMapInfo,
									IMG_BOOL          *pbIsContiguous,
									IMG_UINT32		  *pui32TilingStride)
{
	PVRPDP_DEVINFO	*psDevInfo;
	PVRPDP_BUFFER	*psBuffer;
	
	UNREFERENCED_PARAMETER(pui32TilingStride);
	
	if(!hDevice || !hBuffer || !ppsSysAddr || !pui32ByteSize)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	psBuffer = (PVRPDP_BUFFER*)hBuffer;
	
	*ppsSysAddr = &psBuffer->sSysAddr;
	*ppvCpuVAddr = psBuffer->sCPUVAddr;
	
	*pui32ByteSize = (IMG_UINT32)(psDevInfo->sFBInfo.ulHeight * psDevInfo->sFBInfo.ulByteStride);
	
	*phOSMapInfo = IMG_NULL;
	*pbIsContiguous = IMG_TRUE;
	
	return (PVRSRV_OK);
}

/** Create swapchain function, called from services
 *
 *  @Sets up a swapchain with @p ui32BufferCount number of buffers in the flip chain.
 *  If USE_PRIMARY_SURFACE_IN_FLIP_CHAIN is #define'd, the first buffer is the primary
 *  (aka system) surface and (@p ui32BufferCount -1) back buffers are setup.
 *  If USE_PRIMARY_SURFACE_IN_FLIP_CHAIN is not #define'd, @p ui32BufferCount back buffers
 *  are setup.
 */
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
	PVRPDP_DEVINFO	*psDevInfo;
	PVRPDP_SWAPCHAIN *psSwapChain;
	PVRPDP_BUFFER *psBuffer;
	IMG_UINT32 i;
	IMG_UINT32 backbuffer_i;
	
	UNREFERENCED_PARAMETER(ui32OEMFlags);
	UNREFERENCED_PARAMETER(pui32SwapChainID);
	
	/* check parameters */
	if(!hDevice
	|| !psDstSurfAttrib
	|| !psSrcSurfAttrib
	|| !ppsSyncData
	|| !phSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	/* the pdp only supports a single swapchain */
	if(psDevInfo->psSwapChain)
	{
		return (PVRSRV_ERROR_FLIP_CHAIN_EXISTS);
	}
	
	/* check the buffer count */
#ifdef USE_PRIMARY_SURFACE_IN_FLIP_CHAIN
	if(ui32BufferCount > (PVRPDP_MAX_BACKBUFFERS+1))
#else
	if(ui32BufferCount > (PVRPDP_MAX_BACKBUFFERS))
#endif
	{
		return (PVRSRV_ERROR_TOOMANYBUFFERS);
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
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	if(psDstSurfAttrib->pixelformat != psSrcSurfAttrib->pixelformat
	|| psDstSurfAttrib->sDims.ui32ByteStride != psSrcSurfAttrib->sDims.ui32ByteStride
	|| psDstSurfAttrib->sDims.ui32Width != psSrcSurfAttrib->sDims.ui32Width
	|| psDstSurfAttrib->sDims.ui32Height != psSrcSurfAttrib->sDims.ui32Height)
	{
		/* DST doesn't match the SRC */
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	/* check flags if implementation requires them */
	UNREFERENCED_PARAMETER(ui32Flags);
	
	/* create a swapchain structure */
	psSwapChain = (PVRPDP_SWAPCHAIN*)AllocKernelMem(sizeof(PVRPDP_SWAPCHAIN));
	if(!psSwapChain)
	{
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	
	psBuffer = (PVRPDP_BUFFER*)AllocKernelMem(sizeof(PVRPDP_BUFFER) * ui32BufferCount);
	if(!psBuffer)
	{
		FreeKernelMem(psSwapChain);
		return (PVRSRV_ERROR_OUT_OF_MEMORY);
	}
	
	psSwapChain->ulBufferCount = (unsigned long)ui32BufferCount;
	psSwapChain->psBuffer = psBuffer;
	
	/* link the buffers */
	for(i=0; i<ui32BufferCount-1; i++)
	{
		psBuffer[i].psNext = &psBuffer[i+1];
	}
	/* and link last to first */
	psBuffer[i].psNext = &psBuffer[0];
	
	i=0;
#ifdef USE_PRIMARY_SURFACE_IN_FLIP_CHAIN
	/* The primary surface becomes psBuffer[0] in the swapchain buffer array. */
	psBuffer[0].psSyncData = ppsSyncData[0];
	psBuffer[0].sSysAddr = psDevInfo->sFBInfo.sSysAddr;
	psBuffer[0].sDevVAddr.uiAddr = psDevInfo->ulSysSurfaceOffset;
	psBuffer[0].sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
	psBuffer[0].hSwapChain = (PDP_HANDLE)psSwapChain;
	i++;
#endif
	
	/* populate the buffers */
	for(backbuffer_i = 0; i<ui32BufferCount; i++, backbuffer_i++)
	{
		/* configure the remaining swapchain buffers to use pre-allocated backbuffers */
		psBuffer[i].psSyncData = ppsSyncData[i];
		
		/* asBackBuffers[0] and asBackBuffers[1] become psBuffer[0] and psBuffer[1] respectively
		 * if using not using the primary surface in the flip chain, and psBuffer[1] and psBuffer[2]
		 * respectively if using primary surface.. */
		psBuffer[i].sSysAddr = psDevInfo->asBackBuffers[backbuffer_i].sSysAddr;
		psBuffer[i].sDevVAddr = psDevInfo->asBackBuffers[backbuffer_i].sDevVAddr;
		psBuffer[i].sCPUVAddr = psDevInfo->asBackBuffers[backbuffer_i].sCPUVAddr;
		psBuffer[i].hSwapChain = (PDP_HANDLE)psSwapChain;
	}
	
	/* mark swapchain's existence */
	psDevInfo->psSwapChain = psSwapChain;
	
	/* return swapchain handle */
	*phSwapChain = (IMG_HANDLE)psSwapChain;

	/* Only one swapchain - only one ID for query call */
	*pui32SwapChainID = 1;
	
	ResetVSyncFlipItems(psDevInfo);

#if defined(SYS_USING_INTERRUPTS)
	EnableVSyncInterrupt(psDevInfo);
#endif
	
	return (PVRSRV_OK);
}

/* destroy swapchain function, called from services */
static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
									   IMG_HANDLE hSwapChain)
{
	PVRPDP_DEVINFO   *psDevInfo;
	PVRPDP_SWAPCHAIN *psSwapChain;
	PDP_ERROR         eError;
	
	/* check parameters */
	if(!hDevice
	|| !hSwapChain)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	psSwapChain = (PVRPDP_SWAPCHAIN*)hSwapChain;
	
	/* Flush the vsync flip queue */
	FlushInternalVSyncQueue(psDevInfo);
	
	/* swap to primary */
	eError = Flip(psDevInfo, &psDevInfo->sSystemBuffer.sDevVAddr);
	if(eError != PDP_OK)
	{
		return (PVRSRV_ERROR_FLIP_FAILED);
	}
	
	/* free resources */
	FreeKernelMem(psSwapChain->psBuffer);
	FreeKernelMem(psSwapChain);
	
	/* mark swapchain as not existing */
	psDevInfo->psSwapChain = 0;
	
	ResetVSyncFlipItems(psDevInfo);

#if defined(SYS_USING_INTERRUPTS)
	DisableVSyncInterrupt(psDevInfo);
#endif
	
	return (PVRSRV_OK);
}

/* set DST rect function, called from services */
static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice,
								 IMG_HANDLE hSwapChain,
								 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);
	
	/* only full screen swapchains on this device */
	
	return (PVRSRV_ERROR_NOT_SUPPORTED);
}


/* set SRC rect function, called from services */
static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice,
								 IMG_HANDLE hSwapChain,
								 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);
	
	/* only full screen swapchains on this device */
	
	return (PVRSRV_ERROR_NOT_SUPPORTED);
}

/* set DST colourkey function, called from services */
static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
									  IMG_HANDLE hSwapChain,
									  IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);
	
	/* don't support DST CK on this device */
	
	return (PVRSRV_ERROR_NOT_SUPPORTED);
}

/* set SRC colourkey function, called from services */
static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
									  IMG_HANDLE hSwapChain,
									  IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);
	
	/* don't support SRC CK on this device */
	
	return (PVRSRV_ERROR_NOT_SUPPORTED);
}


/* get swapchain buffers function, called from services */
static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
								 IMG_HANDLE hSwapChain,
								 IMG_UINT32 *pui32BufferCount,
								 IMG_HANDLE *phBuffer)
{
	PVRPDP_SWAPCHAIN *psSwapChain;
	unsigned long     i;
	
	/* check parameters */
	if(!hDevice
	|| !hSwapChain
	|| !pui32BufferCount
	|| !phBuffer)
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	psSwapChain = (PVRPDP_SWAPCHAIN*)hSwapChain;
	
	/* return the buffer count */
	*pui32BufferCount = (IMG_UINT32)psSwapChain->ulBufferCount;
	
	/* return the buffers */
	for(i=0; i<psSwapChain->ulBufferCount; i++)
	{
		phBuffer[i] = (IMG_HANDLE)&psSwapChain->psBuffer[i];
	}
	
	return (PVRSRV_OK);
}


/* swap to buffer function, called from services */
static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice,
								   IMG_HANDLE hBuffer,
								   IMG_UINT32 ui32SwapInterval,
								   IMG_HANDLE hPrivateTag,
								   IMG_UINT32 ui32ClipRectCount,
								   IMG_RECT *psClipRect)
{
	UNREFERENCED_PARAMETER(ui32SwapInterval);
	UNREFERENCED_PARAMETER(hPrivateTag);
	UNREFERENCED_PARAMETER(psClipRect);
	
	if(!hDevice
	|| !hBuffer
	|| (ui32ClipRectCount != 0))
	{
		return (PVRSRV_ERROR_INVALID_PARAMS);
	}
	
	/* nothing to do since services common code does the work in the general case */
	
	return (PVRSRV_OK);
}


/* set state function, called from services */
static IMG_VOID PDPSetState(IMG_HANDLE hDevice,
							IMG_UINT32 ui32State)
{
	PVRPDP_DEVINFO	*psDevInfo;
	
	psDevInfo = (PVRPDP_DEVINFO*)hDevice;
	
	if (ui32State == DC_STATE_FLUSH_COMMANDS)
	{
		if (psDevInfo->psSwapChain != 0)
		{
			FlushInternalVSyncQueue(psDevInfo);
		}
		
		psDevInfo->bFlushCommands = PDP_TRUE;
	}
	else if (ui32State == DC_STATE_NO_FLUSH_COMMANDS)
	{
		psDevInfo->bFlushCommands = PDP_FALSE;
	}
}

/************************************************************
	command processing and interrupt specific functions:
************************************************************/

#if defined(SYS_USING_INTERRUPTS)
/* Vsync ISR handler function */
IMG_BOOL PDPVSyncISR(IMG_VOID *pvDevInfo)
{
	IMG_BOOL         bStatus;
	unsigned long    ulInterruptStatus;
	PVRPDP_DEVINFO  *psDevInfo = (PVRPDP_DEVINFO*)pvDevInfo;
	
	PVRPDP_VSYNC_FLIP_ITEM *psFlipItem;
	
	/* Disable interrupt */
	DisableVSyncInterrupt(psDevInfo);
	
	/* Read interrupt status */
	ulInterruptStatus = ReadReg(psDevInfo, PVRPDP_INTSTATUS);
	
	ulInterruptStatus &= (1 << PVRPDP_INTS_VBLNK1_SHIFT);
	
	bStatus = (ulInterruptStatus != 0)? IMG_TRUE : IMG_FALSE;
	
	if (bStatus == IMG_TRUE)
	{
		/* Clear interrupts */
		WriteReg(psDevInfo, PVRPDP_INTCLEAR, ulInterruptStatus);
		
		/* Check if swapchain exists */
		if(!psDevInfo->psSwapChain)
		{
			/* Re-enable interrupts */
			EnableVSyncInterrupt(psDevInfo);
			return (IMG_FALSE);
		}
		
		psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
		
		while(psFlipItem->bValid)
		{
			/* have we already flipped BEFORE this interrupt */
			if(psFlipItem->bFlipped)
			{
				/* have we already 'Cmd Completed'? */
				if(!psFlipItem->bCmdCompleted)
				{
					IMG_BOOL bScheduleMISR;
					/* only schedule the MISR if the display vsync is on its own LISR */
#if defined(PDP_DEVICE_ISR)
					bScheduleMISR = IMG_TRUE;
#else
					bScheduleMISR = IMG_FALSE;
#endif
					/* command complete the flip */
					psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete((IMG_HANDLE)psFlipItem->hCmdComplete, bScheduleMISR);
					
					/* signal we've done the cmd complete */
					psFlipItem->bCmdCompleted = PDP_TRUE;
				}
				
				/* we've cmd completed so decrement the swap interval */
				psFlipItem->ulSwapInterval--;
				
				/* can we remove the flip item? */
				if(psFlipItem->ulSwapInterval == 0)
				{
					/* advance remove index */
					AdvanceFlipIndex(psDevInfo, &psDevInfo->ulRemoveIndex);
					
					/* clear item state */
					psFlipItem->bCmdCompleted = PDP_FALSE;
					psFlipItem->bFlipped = PDP_FALSE;
					
					/* only mark as invalid once item data is finished with */
					psFlipItem->bValid = PDP_FALSE;
				}
				else
				{
					/*	we're waiting for the last flip to finish displaying
					 *	so the remove index hasn't been updated to block any
					 *	new flips occuring. Nothing more to do on interrupt
					 */
					break;
				}
			}
			else
			{
				/* flip to new surface - flip latches on next interrupt */
				Flip (psDevInfo, psFlipItem->psDevVAddr);
				
				/* signal we've issued the flip to the HW */
				psFlipItem->bFlipped = PDP_TRUE;
				
				/* nothing more to do on interrupt */
				break;
			}
			
			/* update to next flip item */
			psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulRemoveIndex];
		}
	}
	
	/* Re-enable interrupts */
	EnableVSyncInterrupt(psDevInfo);
	
	return bStatus;
}

/* cmd processing flip handler function, called from services */
static IMG_BOOL ProcessFlip(IMG_HANDLE  hCmdCookie,
							IMG_UINT32  ui32DataSize,
							IMG_VOID   *pvData)
{
	PDP_ERROR eError;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	PVRPDP_DEVINFO *psDevInfo;
	PVRPDP_BUFFER *psBuffer;
	PVRPDP_VSYNC_FLIP_ITEM* psFlipItem;
	
	/* check parameters */
	if(!hCmdCookie)
	{
		return IMG_FALSE;
	}
	
	/* validate data packet */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;
	
	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
	{
		return IMG_FALSE;
	}
	
	/* setup some useful pointers */
	psDevInfo = (PVRPDP_DEVINFO*)psFlipCmd->hExtDevice;
	psBuffer = (PVRPDP_BUFFER*)psFlipCmd->hExtBuffer; /* This is the buffer we are flipping to */
	
	if (psDevInfo->bFlushCommands)
	{
		/*
			PVR is flushing its queues so no need to flip.
			Also don't schedule the MISR as we're already in the MISR
		*/
		psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);
		return IMG_TRUE;
	}
	
	/*
		Support for vsync "unlocked" flipping - not real support as this is a latched display,
		we just complete immediately.
	*/
	if(psFlipCmd->ui32SwapInterval == 0)
	{
		/*
			The 'baseaddr' register can be updated outside the vertical blanking region.
			The 'baseaddr' update only takes effect on the vfetch event and the baseaddr register
			update is double-buffered. Hence page flipping is 'latched'.
		*/
		
		/* flip to new surface */
		eError = Flip(psDevInfo, &(psBuffer->sDevVAddr));
		
		if(eError != PDP_OK)
		{
			return IMG_FALSE;
		}
		
		/*
			call command complete Callback
			Also don't schedule the MISR as we're already in the MISR
		*/
		psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);
		
		return IMG_TRUE;
	}
	
	psFlipItem = &psDevInfo->asVSyncFlips[psDevInfo->ulInsertIndex];
	
	/* try to insert command into list */
	if(!psFlipItem->bValid)
	{
		if(psDevInfo->ulInsertIndex == psDevInfo->ulRemoveIndex)
		{
			/* flip to new surface */
			eError = Flip(psDevInfo, &(psBuffer->sDevVAddr));
			if(eError != PDP_OK)
			{
				return IMG_FALSE;
			}
			
			psFlipItem->bFlipped = PDP_TRUE;
		}
		else
		{
			psFlipItem->bFlipped = PDP_FALSE;
		}
		
		psFlipItem->hCmdComplete = hCmdCookie;
		psFlipItem->psDevVAddr = &psBuffer->sDevVAddr;
		psFlipItem->ulSwapInterval = (unsigned long)psFlipCmd->ui32SwapInterval;
		psFlipItem->bValid = PDP_TRUE;
		
		AdvanceFlipIndex(psDevInfo, &psDevInfo->ulInsertIndex);
		
		return IMG_TRUE;
	}
	
	return IMG_FALSE;
}

#else/* #if defined(SYS_USING_INTERRUPTS) */

/*
	cmd processing flip handler function, called from services
	Note: in the case of no interrupts just flip and complete
*/
static IMG_BOOL ProcessFlip(IMG_HANDLE	hCmdCookie,
							IMG_UINT32	ui32DataSize,
							IMG_VOID	*pvData)
{
	PDP_ERROR eError;
	DISPLAYCLASS_FLIP_COMMAND *psFlipCmd;
	PVRPDP_DEVINFO	*psDevInfo;
	PVRPDP_BUFFER	*psBuffer;
	
	/* check parameters */
	if(!hCmdCookie || !pvData)
	{
		return IMG_FALSE;
	}
	
	/* validate data packet */
	psFlipCmd = (DISPLAYCLASS_FLIP_COMMAND*)pvData;
	if (psFlipCmd == IMG_NULL || sizeof(DISPLAYCLASS_FLIP_COMMAND) != ui32DataSize)
	{
		return IMG_FALSE;
	}
	
	/* setup some useful pointers */
	psDevInfo = (PVRPDP_DEVINFO*)psFlipCmd->hExtDevice;
	psBuffer = (PVRPDP_BUFFER*)psFlipCmd->hExtBuffer;
	
	/* flip the display */
	eError = Flip(psDevInfo, &(psBuffer->sDevVAddr));
	if(eError != PDP_OK)
	{
		return IMG_FALSE;
	}
	
	/*
		call command complete Callback
		Also don't schedule the MISR as we're already in the MISR
	*/
	psDevInfo->sPVRJTable.pfnPVRSRVCmdComplete(hCmdCookie, IMG_FALSE);
	
	return IMG_TRUE;
}
#endif /* #if defined (SYS_USING_INTERRUPTS) */


/************************************************************
	init/de-init functions:
************************************************************/

/* the common PDP driver initialisation function */
PDP_ERROR Init(void)
{
	PVRPDP_DEVINFO		*psDevInfo;
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
	
	psDevInfo = gpsDevInfo;
	
	if (psDevInfo == 0)
	{
		PFN_CMD_PROC	pfnCmdProcList[PVRPDP_COMMAND_COUNT];
		IMG_UINT32		aui32SyncCountList[PVRPDP_COMMAND_COUNT][2];
		
		/* allocate device info. structure */
		psDevInfo = (PVRPDP_DEVINFO *)AllocKernelMem(sizeof(PVRPDP_DEVINFO));
		if(!psDevInfo)
		{
			return (PDP_ERROR_OUT_OF_MEMORY);/* failure */
		}
		
		/* update the static ptr */
		gpsDevInfo = psDevInfo;
		
		/* set ref count */
		psDevInfo->ulRefCount = 0;
		
		/* save private fbdev information structure in the dev. info. */
		if(InitPDP(psDevInfo) != PDP_OK)
		{
			return (PDP_ERROR_INIT_FAILURE);/* failure */
		}
		
		if(OpenPVRServices(&psDevInfo->hPVRServices) != PDP_OK)
		{
			return (PDP_ERROR_INIT_FAILURE);/* failure */
		}
		if(GetLibFuncAddr (psDevInfo->hPVRServices, "PVRGetDisplayClassJTable", &pfnGetPVRJTable) != PDP_OK)
		{
			return (PDP_ERROR_INIT_FAILURE);/* failure */
		}
		
		/* got the kernel services function table */
		if(!(*pfnGetPVRJTable)(&psDevInfo->sPVRJTable))
		{
			return (PDP_ERROR_INIT_FAILURE);/* failure */
		}
		
		/* Setup the devinfo */
		psDevInfo->bFlushCommands = PDP_FALSE;
		psDevInfo->psSwapChain = 0;
		psDevInfo->sDisplayInfo.ui32MinSwapInterval = 0UL;
		/* Maximum swap interval is an arbitary choice */
		psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 10UL;
		psDevInfo->sDisplayInfo.ui32MaxSwapChains = 1UL;
#ifdef USE_PRIMARY_SURFACE_IN_FLIP_CHAIN
		psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = PVRPDP_MAX_BACKBUFFERS + 1;
#else
		psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = PVRPDP_MAX_BACKBUFFERS;
#endif
		strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);
		
		psDevInfo->sDisplayInfo.ui32PhysicalWidthmm = PDP_DISPLAY_WIDTH_MM;
		psDevInfo->sDisplayInfo.ui32PhysicalHeightmm = PDP_DISPLAY_HEIGHT_MM;
		
		psDevInfo->ulNumFormats = 1UL;
		
		psDevInfo->asDisplayFormatList[0].pixelformat = psDevInfo->sFBInfo.ePixelFormat;
		psDevInfo->ulNumDims = 1UL;
		psDevInfo->asDisplayDimList[0].ui32Width      = (IMG_UINT32)psDevInfo->sFBInfo.ulWidth;
		psDevInfo->asDisplayDimList[0].ui32Height     = (IMG_UINT32)psDevInfo->sFBInfo.ulHeight;
		psDevInfo->asDisplayDimList[0].ui32ByteStride = (IMG_UINT32)psDevInfo->sFBInfo.ulByteStride;
		psDevInfo->sSysFormat = psDevInfo->asDisplayFormatList[0];
		psDevInfo->sSysDims.ui32Width      = psDevInfo->asDisplayDimList[0].ui32Width;
		psDevInfo->sSysDims.ui32Height     = psDevInfo->asDisplayDimList[0].ui32Height;
		psDevInfo->sSysDims.ui32ByteStride = psDevInfo->asDisplayDimList[0].ui32ByteStride;
		
		/* Setup system buffer */
		psDevInfo->sSystemBuffer.hSwapChain = 0;
		psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
		psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
		psDevInfo->sSystemBuffer.sDevVAddr.uiAddr = psDevInfo->ulSysSurfaceOffset;
		
		/* setup the DC Jtable so SRVKM can call into this driver */
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
		psDevInfo->sDCJTable.pfnSetDCState = PDPSetState;
		
		/* register device with services and retrieve device index */
		if(psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice (&psDevInfo->sDCJTable,
															&psDevInfo->uiDeviceID ) != PVRSRV_OK)
		{
			return (PDP_ERROR_DEVICE_REGISTER_FAILED);/* failure */
		}
		
		/* setup private command processing function table */
		pfnCmdProcList[DC_FLIP_COMMAND] = ProcessFlip;
		
		/* and associated sync count(s)	*/
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
																PVRPDP_COMMAND_COUNT) != PVRSRV_OK)
		{
			return (PDP_ERROR_CANT_REGISTER_CALLBACK);/* failure */
		}
		
		/*
			 - Install an ISR for the PDP Vsync interrupt
			 - Disable the ISR on installation
				- create/destroy swapchain enables/disables the ISR
		*/
		
		ResetVSyncFlipItems(psDevInfo);
		
		/*
			display hardware may have its own interrupt or can share
			with other devices (device vs. services ISRs)
			where PDP_DEVICE_ISR == device isr type
		*/
#if defined (SYS_USING_INTERRUPTS)
	#if defined(PDP_DEVICE_ISR)
		/* install a device specific ISR handler for PDP */
		if(InstallVsyncISR(psDevInfo) != PDP_OK)
		{
			return (PDP_ERROR_INIT_FAILURE);
		}
	#else
		/* register external isr to be called off back of the services system isr */
		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterSystemISRHandler(PDPVSyncISR,
																	psDevInfo,
																	0,
																	psDevInfo->uiDeviceID) != PVRSRV_OK)
		{
			return (PDP_ERROR_INIT_FAILURE);
		}
	#endif /* defined(PDP_DEVICE_ISR) */
#endif

#if defined(PDP_DEVICE_POWER)
		/*
			Note: In this case the Device should be registered
			with the OS power manager directly
		*/
#else
		/*
			Register PDP with services to get power events from services
			Note: generally this is not the recommended method for
			3rd party display drivers power management
		*/
		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterPowerDevice(psDevInfo->uiDeviceID,
															   PDPPrePower, PDPPostPower,
															   IMG_NULL, IMG_NULL,
															   psDevInfo,
															   PVRSRV_DEV_POWER_STATE_ON,
															   PVRSRV_DEV_POWER_STATE_ON) != PVRSRV_OK)
		{
			return (PDP_ERROR_INIT_FAILURE);
		}
#endif /* defined(PDP_DEVICE_POWER) */
	}
	
	/* increment the ref count */
	psDevInfo->ulRefCount++;
	
	/* return success */
	return (PDP_OK);
}

/* the common PDP driver de-initialisation function */
PDP_ERROR Deinit(void)
{
	PVRPDP_DEVINFO *psDevInfo;
	
	psDevInfo = gpsDevInfo;
	
	/* check DevInfo has been setup */
	if (psDevInfo == 0)
	{
		return (PDP_ERROR_GENERIC);/* failure */
	}
	
	/* decrement ref count */
	psDevInfo->ulRefCount--;
	
	if (psDevInfo->ulRefCount == 0)
	{
		PVRSRV_DC_DISP2SRV_KMJTABLE	*psJTable = &psDevInfo->sPVRJTable;

#if !defined(PDP_DEVICE_POWER)
		/* unregister with services power manager */
		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterPowerDevice(psDevInfo->uiDeviceID,
															   IMG_NULL, IMG_NULL,
															   IMG_NULL, IMG_NULL, IMG_NULL,
															   PVRSRV_DEV_POWER_STATE_ON,
															   PVRSRV_DEV_POWER_STATE_ON) != PVRSRV_OK)
		{
			return (PDP_ERROR_GENERIC);
		}
#endif /* defined(PDP_DEVICE_POWER) */
		
		/*
			display hardware may have its own interrupt or can share
			with other devices (device vs. services ISRs)
			where PDP_DEVICE_ISR == device isr type
		*/
#if defined (SYS_USING_INTERRUPTS)
	#if defined(PDP_DEVICE_ISR)
		/* uninstall device specific ISR handler for PDP */
		if(UninstallVsyncISR(psDevInfo) != PDP_OK)
		{
			return (PDP_ERROR_GENERIC);
		}
	#else
		/* remove registration with external system isr */
		if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterSystemISRHandler(IMG_NULL, IMG_NULL, 0,
																	psDevInfo->uiDeviceID) != PVRSRV_OK)
		{
			return (PDP_ERROR_GENERIC);
		}
	#endif /* defined(PDP_DEVICE_ISR) */
#endif
		/* remove cmd handler registration */
		if (psDevInfo->sPVRJTable.pfnPVRSRVRemoveCmdProcList (psDevInfo->uiDeviceID,
																PVRPDP_COMMAND_COUNT) != PVRSRV_OK)
		{
			return (PDP_ERROR_GENERIC);/* failure */
		}
		
		/* Remove display class device from kernel services device register */
		if (psJTable->pfnPVRSRVRemoveDCDevice(psDevInfo->uiDeviceID) != PVRSRV_OK)
		{
			return (PDP_ERROR_GENERIC);/* failure */
		}
		
		/* perform device specific de-initialisation */
		DeInitPDP(psDevInfo);
		
		/* close services connection */
		if (ClosePVRServices(psDevInfo->hPVRServices) != PDP_OK)
		{
			psDevInfo->hPVRServices = 0;
			return (PDP_ERROR_GENERIC);/* failure */
		}
		
		/* de-allocate data structure */
		FreeKernelMem(psDevInfo);
	}

#if defined (ENABLE_DISPLAY_MODE_TRACKING) && defined (SUPPORT_DYNAMIC_GTF_TIMING)
	CloseMiniport();
#endif
	/* clear the static ptr */
	gpsDevInfo = 0;
	
	/* return success */
	return (PDP_OK);
}

/************************************************************
	services controlled power callbacks:
************************************************************/

#if !defined(PDP_DEVICE_POWER)
/*
	Services power management pre-power-transition callback function
	Note: generally it is not recommended to use this function, instead
	the device should register with the OS power manager independently
*/
PVRSRV_ERROR PDPPrePower (IMG_HANDLE 		      hDevHandle,
						  PVRSRV_DEV_POWER_STATE  eNewPowerState,
						  PVRSRV_DEV_POWER_STATE  eCurrentPowerState)
{
	PVRPDP_DEVINFO	*psDevInfo = (PVRPDP_DEVINFO *)hDevHandle;
	
	if ((eNewPowerState != eCurrentPowerState) &&
		(eNewPowerState == PVRSRV_DEV_POWER_STATE_OFF))
	{
		if (psDevInfo->psSwapChain != 0)
		{
#if defined(SYS_USING_INTERRUPTS)
			DisableVSyncInterrupt(psDevInfo);
#endif
		}
	}
	
	return (PVRSRV_OK);
}

/*
	Services power management pre-power-transition callback function
	Note: generally it is not recommended to use this function, instead
	the device should register with the OS power manager independently
*/
PVRSRV_ERROR PDPPostPower (IMG_HANDLE 				hDevHandle,
						   PVRSRV_DEV_POWER_STATE	eNewPowerState,
						   PVRSRV_DEV_POWER_STATE	eCurrentPowerState)
{
	PVRPDP_DEVINFO	*psDevInfo = (PVRPDP_DEVINFO *)hDevHandle;
	
	if ((eNewPowerState != eCurrentPowerState) &&
		(eCurrentPowerState == PVRSRV_DEV_POWER_STATE_OFF))
	{
		InitPDPRegisters(psDevInfo);
		
		if (psDevInfo->psSwapChain != 0)
		{
#if defined(SYS_USING_INTERRUPTS)
			EnableVSyncInterrupt(psDevInfo);
#endif
		}
	}
	
	return (PVRSRV_OK);
}
#endif /* #if !defined(PDP_DEVICE_POWER) */

/************************************************************
	PDP Hardware specific functions:
************************************************************/

/* PDP register setup function: */
void InitPDPRegisters(PVRPDP_DEVINFO *psDevInfo)
{
	HVTIMEREGS    *psHVTRegs = &psDevInfo->sHVTRegs;
	unsigned long  ulHDataEnableStart;
	unsigned long  ulHDataEnableFinish;
	unsigned long  ulVDataEnableStart;
	unsigned long  ulVDataEnableFinish;
	unsigned long  ulStr1Surf;
		
	WriteReg(psDevInfo, PVRPDP_STR1ADDRCTRL, 0x00000000); /* turn off memory request */
	WriteReg(psDevInfo, PVRPDP_SYNCTRL, 0x0000022A);      /* disable sync gen */

#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
	/*************************************************
	 * PLL default setup is for 640 x 480 at 60Hz
	 * The default PLL clock is in flash memory which
	 * is not amended by SysSetPDP1Clk()
	 * The default clock will be loaded from flash on reset
	 ************************************************/
	SysSetPDP1Clk(psDevInfo, psHVTRegs->ulClockFreq);
#endif /* #if defined (SUPPORT_DYNAMIC_GTF_TIMING) */
	
	
	ulStr1Surf = (PVRPDP_STR1SURF_FORMAT_ARGB8888 << PVRPDP_STR1SURF_STRFORMAT_SHIFT) |
					((psDevInfo->sFBInfo.ulWidth - 1) << PVRPDP_STR1SURF_STRWIDTH_SHIFT) |
					((psDevInfo->sFBInfo.ulHeight - 1) << PVRPDP_STR1SURF_STRHEIGHT_SHIFT);
	
	WriteReg(psDevInfo, PVRPDP_STR1SURF, ulStr1Surf);
	WriteReg(psDevInfo, PVRPDP_STR1ADDRCTRL, PVRPDP_STR1ADDRCTRL_STREAMENABLE |
											(psDevInfo->sDisplayDevVAddr.uiAddr >> PVRPDP_STR1ADDRCTRL_ADDR_ALIGNSHIFT));
	
	/* stride, in bits 9:0 (in units of 16 bytes minus 1) */
	WriteReg(psDevInfo, PVRPDP_STR1POSN, (psDevInfo->sFBInfo.ulByteStride >> PVRPDP_STR1POSN_STRIDE_ALIGNSHIFT) - 1);
	
	if(ReadReg(psDevInfo, PVRPDP_STRCTRL) != 0x0000C010)
	{
		WriteReg(psDevInfo, PVRPDP_STRCTRL, 0x00001C10); /* buffer request threshold */
	}
	
	WriteReg(psDevInfo, PVRPDP_BORDCOL, 0x00005544); /* border colour */
	WriteReg(psDevInfo, PVRPDP_UPDCTRL, 0x00000000); /* update control */
	
	WriteReg(psDevInfo, PVRPDP_HSYNC1, psHVTRegs->ulHBackPorch<<16|psHVTRegs->ulHTotal); /* hsync timings */
	WriteReg(psDevInfo, PVRPDP_HSYNC2, psHVTRegs->ulHActiveStart<<16|psHVTRegs->ulHLeftBorder); /* more hsync timings */
	WriteReg(psDevInfo, PVRPDP_HSYNC3, psHVTRegs->ulHFrontPorch<<16|psHVTRegs->ulHRightBorder); /* more hsync timings */
	WriteReg(psDevInfo, PVRPDP_VSYNC1, psHVTRegs->ulVBackPorch<<16|psHVTRegs->ulVTotal); /* vsync timings */
	WriteReg(psDevInfo, PVRPDP_VSYNC2, psHVTRegs->ulVActiveStart<<16|psHVTRegs->ulVTopBorder); /* more vsync timings */
	WriteReg(psDevInfo, PVRPDP_VSYNC3, psHVTRegs->ulVFrontPorch<<16|psHVTRegs->ulVBottomBorder); /* more vsync timings */
	
	ulHDataEnableStart  = psHVTRegs->ulHActiveStart;
	ulHDataEnableFinish = psHVTRegs->ulHFrontPorch;
	
	ulVDataEnableStart  = psHVTRegs->ulVActiveStart;
	ulVDataEnableFinish = psHVTRegs->ulVFrontPorch;
	
	WriteReg(psDevInfo, PVRPDP_HDECTRL, ulHDataEnableStart<<16|ulHDataEnableFinish); /* horizontal data enable */
	WriteReg(psDevInfo, PVRPDP_VDECTRL, ulVDataEnableStart<<16|ulVDataEnableFinish); /* vertical data enable */
	
	WriteReg(psDevInfo, PVRPDP_VEVENT, 0x01F20003); /* vertical event start(27:16), vfetch start line 6 */
	WriteReg(psDevInfo, PVRPDP_SYNCTRL, 0x8000022A); /* Enable sync gen last and set up polarities of sync,blank */
}



/* PDP initialisation function */
static PDP_ERROR InitPDP(PVRPDP_DEVINFO *psDevInfo)
{
	unsigned long   ulRegBaseAddr, ulMemBaseAddr, ulSysSurfaceOffset;
	unsigned long   i, *pulPrimary;
	unsigned long   ulSurfaceSize;
	
	PDP_ERROR eError = OSGetDeviceAddresses(&ulRegBaseAddr, &ulMemBaseAddr, &ulSysSurfaceOffset);
	
	if(eError != PDP_OK)
	{
		return eError;
	}
	
	/*
	 * PDP setup : 640x480x32bpp 60Hz. Surface address = base+ulSysSurfaceOffset
	 * Note this will eventually be done in hardware on power-up
	 */
	psDevInfo->sFBInfo.ePixelFormat = PVRPDP_PIXELFORMAT;
	psDevInfo->sFBInfo.ulWidth      = PVRPDP_WIDTH;
	psDevInfo->sFBInfo.ulHeight     = PVRPDP_HEIGHT;
	psDevInfo->sFBInfo.ulByteStride = PVRPDP_STRIDE;
	psDevInfo->ulSysSurfaceOffset	= ulSysSurfaceOffset;
	
	psDevInfo->sDisplayDevVAddr.uiAddr = ulSysSurfaceOffset;
	
	ulSurfaceSize = PAGEALIGN(psDevInfo->sFBInfo.ulByteStride * psDevInfo->sFBInfo.ulHeight);
	
	/* Registers */
	psDevInfo->sFBInfo.sRegSysAddr.uiAddr = ulRegBaseAddr + PVRPDP_PCI_REG_OFFSET;
	psDevInfo->sFBInfo.pvRegs = MapPhysAddr(psDevInfo->sFBInfo.sRegSysAddr, PVRPDP_REG_SIZE);

#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
	{
		IMG_SYS_PHYADDR sRegTCFAddr;
		
		sRegTCFAddr.uiAddr = ulRegBaseAddr + TCF_PCI_REG_OFFSET;
		psDevInfo->sFBInfo.pvTCFRegs = MapPhysAddr(sRegTCFAddr, TCF_REG_SIZE);
	}
#endif /* #if defined (SUPPORT_DYNAMIC_GTF_TIMING) */
	
	/* System Surface */
	psDevInfo->sFBInfo.sSysAddr.uiAddr = ulMemBaseAddr + ulSysSurfaceOffset;


	psDevInfo->sFBInfo.sCPUVAddr = MapPhysAddr(psDevInfo->sFBInfo.sSysAddr, ulSurfaceSize);
	
	/* setup swapchain back buffers */
	for(i=0; i<PVRPDP_MAX_BACKBUFFERS; i++)
	{
		/* backbuffers allocated immediately after the system surface */
		psDevInfo->asBackBuffers[i].sSysAddr.uiAddr = psDevInfo->sFBInfo.sSysAddr.uiAddr
													+ ((1 + i) * ulSurfaceSize);
		psDevInfo->asBackBuffers[i].sDevVAddr.uiAddr = ulSysSurfaceOffset
													+ ((1 + i) * ulSurfaceSize);
		
		psDevInfo->asBackBuffers[i].sCPUVAddr = MapPhysAddr(psDevInfo->asBackBuffers[i].sSysAddr, ulSurfaceSize);
		psDevInfo->asBackBuffers[i].hSwapChain = 0;
		psDevInfo->asBackBuffers[i].psSyncData = 0;
		psDevInfo->asBackBuffers[i].psNext = 0;
	}



#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
	{
		DLMODE sDisplayMode;
		
		sDisplayMode.ulRefresh = (psDevInfo->sFBInfo.ulWidth > 1024) ? 60 : 75;
		sDisplayMode.ulXExt = psDevInfo->sFBInfo.ulWidth;
		sDisplayMode.ulYExt = psDevInfo->sFBInfo.ulHeight;
		
		GTFCalcFromRefresh(&sDisplayMode, &psDevInfo->sHVTRegs);
	}
#else
	psDevInfo->sHVTRegs.ulHTotal = 816;
	psDevInfo->sHVTRegs.ulHBackPorch = 106;
	psDevInfo->sHVTRegs.ulHActiveStart = 152;
	psDevInfo->sHVTRegs.ulHLeftBorder = 152;
	psDevInfo->sHVTRegs.ulHFrontPorch = 792;
	psDevInfo->sHVTRegs.ulHRightBorder = 792;
	psDevInfo->sHVTRegs.ulVTotal = 519;
	psDevInfo->sHVTRegs.ulVBackPorch = 3;
	psDevInfo->sHVTRegs.ulVActiveStart = 15;
	psDevInfo->sHVTRegs.ulVTopBorder = 15;
	psDevInfo->sHVTRegs.ulVFrontPorch = 498;
	psDevInfo->sHVTRegs.ulVBottomBorder = 498;
#endif /* #if defined (SUPPORT_DYNAMIC_GTF_TIMING) */
	
	InitPDPRegisters(psDevInfo);
	
	/* Initialise the primary display to a nice gradient. */
	ulSurfaceSize = psDevInfo->sFBInfo.ulWidth * psDevInfo->sFBInfo.ulHeight;
	pulPrimary = (unsigned long *)psDevInfo->sFBInfo.sCPUVAddr;
	for(i=0; i < ulSurfaceSize; i++)
	{
		unsigned long gray = (unsigned long)(0x5AUL * i / ulSurfaceSize);
		pulPrimary[i] =  0xFF000000UL + gray + (gray << 8) + (gray << 16);
	}
	
	OSRegisterDevice(psDevInfo);
	
	return (PDP_OK);
}

/* PDP de-initialisation function */
static void DeInitPDP(PVRPDP_DEVINFO *psDevInfo)
{
	unsigned long i;
	
	OSUnregisterDevice(psDevInfo);
	
	WriteReg(psDevInfo, PVRPDP_STR1ADDRCTRL, 0);		 /* turn off memory request */
	WriteReg(psDevInfo, PVRPDP_SYNCTRL, 0x0000022A);	/* disable sync gen */

#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
	UnMapPhysAddr(psDevInfo->sFBInfo.pvTCFRegs, TCF_REG_SIZE);
#endif
	UnMapPhysAddr(psDevInfo->sFBInfo.pvRegs, PVRPDP_REG_SIZE);

	UnMapPhysAddr(psDevInfo->sFBInfo.sCPUVAddr, psDevInfo->sFBInfo.ulByteStride * psDevInfo->sFBInfo.ulHeight);
	for(i=0; i<PVRPDP_MAX_BACKBUFFERS; i++)
	{
		UnMapPhysAddr(psDevInfo->asBackBuffers[i].sCPUVAddr, psDevInfo->sFBInfo.ulByteStride * psDevInfo->sFBInfo.ulHeight);
	}
}

/******************************************************************************
 End of file (pvrpdp-displayclass.c)
******************************************************************************/
