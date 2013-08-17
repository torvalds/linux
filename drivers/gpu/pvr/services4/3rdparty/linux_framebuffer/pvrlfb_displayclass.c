/*************************************************************************/ /*!
@Title          Linux frame buffer driver display class.
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

/*
 * Linux framebuffer 3rd party display driver.
 *
 * This is a very primitive driver at present.  It just provides the
 * framebuffer address; flipping isn't supported yet.
 *
 * It may be possible to implement flipping by using the display panning
 * facility within the Linux framebuffer driver, depending on which
 * particular driver is being used.  For example, panning within the Linux
 * vesafb driver uses the VESA video BIOS extension (VBE) to set the display
 * start address within the framebuffer.  Unfortunately, the VBE is not always
 * functional - as is the case on the system used to develop this
 * driver.
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <linux/string.h>
#if defined(PVRLFB_AUTO_UNBLANK)
#include <linux/notifier.h>
#include <linux/workqueue.h>
#endif	/* defined(PVRLFB_AUTO_UNBLANK) */

/* IMG services headers */
#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "pvrlfb.h"

#define	PVRLFB_MAX_NUM_DEVICES		FB_MAX
#if (PVRLFB_MAX_NUM_DEVICES > FB_MAX)
#error "PVRLFB_MAX_NUM_DEVICES must not be greater than FB_MAX"
#endif

static PVRLFB_DEVINFO *gapsDevInfo[PVRLFB_MAX_NUM_DEVICES];

/* top level 'hook ptr' */

static PFN_DC_GET_PVRJTABLE gpfnGetPVRJTable = NULL;

/* Returns DevInfo pointer for a given device */
static inline PVRLFB_DEVINFO *PVRLFBGetDevInfoPtr(unsigned uiFBDevID)
{
	WARN_ON(uiFBDevID >= PVRLFB_MAX_NUM_DEVICES);

	if (uiFBDevID >= PVRLFB_MAX_NUM_DEVICES)
	{
		return NULL;
	}

	return gapsDevInfo[uiFBDevID];
}

/* Sets the DevInfo pointer for a given device */
static inline void PVRLFBSetDevInfoPtr(unsigned uiFBDevID, PVRLFB_DEVINFO *psDevInfo)
{
	WARN_ON(uiFBDevID >= PVRLFB_MAX_NUM_DEVICES);

	if (uiFBDevID < PVRLFB_MAX_NUM_DEVICES)
	{
		gapsDevInfo[uiFBDevID] = psDevInfo;
	}
}

#if defined(PVRLFB_AUTO_UNBLANK)
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,10))
/* Linux Framebuffer event notification handler */
static int FrameBufferEvents(struct notifier_block *psNotif,
                             unsigned long event, void *data)
{
	PVRLFB_DEVINFO *psDevInfo;
	struct fb_event *psFBEvent = (struct fb_event *)data;
	struct fb_info *psFBInfo = psFBEvent->info;

	/* Only interested in blanking events */
	if (event != FB_EVENT_BLANK)
	{
		return 0;
	}

	/* Nothing to do if the screen is being unblanked */
	if (*(int *)psFBEvent->data == 0)
	{
		return 0;
	}

	psDevInfo = PVRLFBGetDevInfoPtr(psFBInfo->node);

	/* Schedule the work queue to unblank the screen */
	schedule_work(&psDevInfo->sLINWork);

	return 0;
}
#endif /* #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)) */

/* Work queue handler */
static void WorkHandler(
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
			void *pvData
#else
			struct work_struct *psWork
#endif
			)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	PVRLFB_DEVINFO *psDevInfo = (PVRLFB_DEVINFO *)pvData;
#else
	PVRLFB_DEVINFO *psDevInfo = container_of(psWork, PVRLFB_DEVINFO, sLINWork);
#endif

	/* Try and unblank display */
	PVRLFB_CONSOLE_LOCK();
	(void) fb_blank(psDevInfo->psLINFBInfo, 0);
	PVRLFB_CONSOLE_UNLOCK();

}
#endif	/* defined(PVRLFB_AUTO_UNBLANK) */

static PVRSRV_ERROR OpenDCDevice(IMG_UINT32 ui32PVRDevID,
                                 IMG_HANDLE *phDevice,
                                 PVRSRV_SYNC_DATA* psSystemBufferSyncData)
{
	PVRLFB_DEVINFO *psDevInfo;
	unsigned i;
#if defined(PVRLFB_AUTO_UNBLANK)
	int res;
#endif	/* defined(PVRLFB_AUTO_UNBLANK) */

	for (i = 0; i < PVRLFB_MAX_NUM_DEVICES; i++)
	{
		psDevInfo = PVRLFBGetDevInfoPtr(i);
		if (psDevInfo != NULL && psDevInfo->uiPVRDevID == ui32PVRDevID)
		{
			break;
		}
	}
	if (i == PVRLFB_MAX_NUM_DEVICES)
	{
		DEBUG_PRINTK((KERN_WARNING DRIVER_PREFIX
			": %s: PVR Device %u not found\n", __FUNCTION__, ui32PVRDevID));
		return PVRSRV_ERROR_INVALID_DEVICE;
	}

	/* store the system surface sync data */
	psDevInfo->sSystemBuffer.psSyncData = psSystemBufferSyncData;

	/* return handle to the devinfo */
	*phDevice = (IMG_HANDLE)psDevInfo;
	
#if defined(PVRLFB_AUTO_UNBLANK)
	/* Set up Linux Framebuffer event notification */
	memset(&psDevInfo->sLINNotifBlock, 0, sizeof(psDevInfo->sLINNotifBlock));
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,10))
	psDevInfo->sLINNotifBlock.notifier_call = FrameBufferEvents;
#endif

	res = fb_register_client(&psDevInfo->sLINNotifBlock);
	if (res != 0)
	{
		printk(KERN_INFO DRIVER_PREFIX
			": Device %u: Couldn't register for framebuffer events (error %d)\n",
			psDevInfo->uiFBDevID, res);

		return PVRSRV_ERROR_UNABLE_TO_REGISTER_EVENT;
	}
#endif	/* defined(PVRLFB_AUTO_UNBLANK) */

	/* Unblank display */
	PVRLFB_CONSOLE_LOCK();
	(void) fb_blank(psDevInfo->psLINFBInfo, 0);
	PVRLFB_CONSOLE_UNLOCK();

	return PVRSRV_OK;
}


static PVRSRV_ERROR CloseDCDevice(IMG_HANDLE hDevice)
{
#if defined(PVRLFB_AUTO_UNBLANK)
	PVRLFB_DEVINFO *psDevInfo = (PVRLFB_DEVINFO*)hDevice;

	/* Unregister for Framebuffer events */
	fb_unregister_client(&psDevInfo->sLINNotifBlock);

	/* Ensure we have nothing in the work queue */
	flush_scheduled_work();
#else	/* defined(PVRLFB_AUTO_UNBLANK) */
	UNREFERENCED_PARAMETER(hDevice);
#endif	/* defined(PVRLFB_AUTO_UNBLANK) */

	return PVRSRV_OK;
}


static PVRSRV_ERROR EnumDCFormats(IMG_HANDLE hDevice,
                                  IMG_UINT32 *pui32NumFormats,
                                  DISPLAY_FORMAT *psFormat)
{
	PVRLFB_DEVINFO	*psDevInfo;

	if (!hDevice || !pui32NumFormats)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (PVRLFB_DEVINFO*)hDevice;

	*pui32NumFormats = (IMG_UINT32)psDevInfo->ulNumFormats;

	if (psFormat)
	{
		unsigned long i;

		for (i=0; i<psDevInfo->ulNumFormats; i++)
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
	PVRLFB_DEVINFO	*psDevInfo;
	
	if (!hDevice || !psFormat || !pui32NumDims)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (PVRLFB_DEVINFO*)hDevice;

	*pui32NumDims = (IMG_UINT32)psDevInfo->ulNumDims;

	/* No need to look at psFormat; there is only one */
	if (psDim)
	{
		unsigned long i;

		for(i=0; i<psDevInfo->ulNumDims; i++)
		{
			psDim[i] = psDevInfo->asDisplayDimList[i];
		}
	}
	
	return PVRSRV_OK;
}


static PVRSRV_ERROR GetDCSystemBuffer(IMG_HANDLE hDevice, IMG_HANDLE *phBuffer)
{
	PVRLFB_DEVINFO	*psDevInfo;

	if (!hDevice || !phBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (PVRLFB_DEVINFO*)hDevice;

	*phBuffer = (IMG_HANDLE)&psDevInfo->sSystemBuffer;

	return PVRSRV_OK;
}


static PVRSRV_ERROR GetDCInfo(IMG_HANDLE hDevice, DISPLAY_INFO *psDCInfo)
{
	PVRLFB_DEVINFO	*psDevInfo;

	if (!hDevice || !psDCInfo)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}

	psDevInfo = (PVRLFB_DEVINFO*)hDevice;

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
	PVRLFB_DEVINFO *psDevInfo;
	PVRLFB_BUFFER  *psSystemBuffer;

	UNREFERENCED_PARAMETER(pui32TilingStride);

	if (!hDevice)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psDevInfo = (PVRLFB_DEVINFO*)hDevice;

	if (hBuffer != (IMG_HANDLE)&psDevInfo->sSystemBuffer)
	{
		return PVRSRV_ERROR_INVALID_PARAMS;
	}
	psSystemBuffer = (PVRLFB_BUFFER *)hBuffer;

	if (ppsSysAddr)
	{
		*ppsSysAddr = &psSystemBuffer->sSysAddr;
	}

	if (pui32ByteSize)
	{
		*pui32ByteSize = (IMG_UINT32)psSystemBuffer->ulBufferSize;
	}

	if (ppvCpuVAddr)
	{
		*ppvCpuVAddr = psSystemBuffer->sCPUVAddr;
	}

	if (phOSMapInfo)
	{
		*phOSMapInfo = (IMG_HANDLE)0;
	}

	if (pbIsContiguous)
	{
		*pbIsContiguous = IMG_TRUE;
	}

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
	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR DestroyDCSwapChain(IMG_HANDLE hDevice,
                                       IMG_HANDLE hSwapChain)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);

	return PVRSRV_ERROR_INVALID_PARAMS;
}


static PVRSRV_ERROR SetDCDstRect(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCSrcRect(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_RECT *psRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(psRect);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCDstColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR SetDCSrcColourKey(IMG_HANDLE hDevice,
                                      IMG_HANDLE hSwapChain,
                                      IMG_UINT32 ui32CKColour)
{

	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(ui32CKColour);

	return PVRSRV_ERROR_NOT_SUPPORTED;
}


static PVRSRV_ERROR GetDCBuffers(IMG_HANDLE hDevice,
                                 IMG_HANDLE hSwapChain,
                                 IMG_UINT32 *pui32BufferCount,
                                 IMG_HANDLE *phBuffer)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hSwapChain);
	UNREFERENCED_PARAMETER(pui32BufferCount);
	UNREFERENCED_PARAMETER(phBuffer);

	return PVRSRV_ERROR_INVALID_PARAMS;
}


static PVRSRV_ERROR SwapToDCBuffer(IMG_HANDLE hDevice,
                                   IMG_HANDLE hBuffer,
                                   IMG_UINT32 ui32SwapInterval,
                                   IMG_HANDLE hPrivateTag,
                                   IMG_UINT32 ui32ClipRectCount,
                                   IMG_RECT *psClipRect)
{
	UNREFERENCED_PARAMETER(hDevice);
	UNREFERENCED_PARAMETER(hBuffer);
	UNREFERENCED_PARAMETER(ui32SwapInterval);
	UNREFERENCED_PARAMETER(hPrivateTag);
	UNREFERENCED_PARAMETER(ui32ClipRectCount);
	UNREFERENCED_PARAMETER(psClipRect);

	return PVRSRV_ERROR_INVALID_PARAMS;
}


/*******************************************************************************
 Function Name      : PVRPixelFormat
 Inputs             : var
 Outputs            : pPVRPixelFormat
 Returns            : Error
 Description        : Calculates the PVR equivalent of the pixel
			  format in the var structure
*******************************************************************************/
static PVRLFB_ERROR PVRPixelFormat(struct fb_var_screeninfo *var,
                                PVRSRV_PIXEL_FORMAT *pPVRPixelFormat)
{
	if (var->bits_per_pixel == 16)
	{
		if ((var->red.length == 5) &&
			(var->green.length == 6) && 
			(var->blue.length == 5) && 
			(var->red.offset == 11) &&
			(var->green.offset == 5) && 
			(var->blue.offset == 0) && 
			(var->red.msb_right == 0))
		{
			*pPVRPixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
			return PVRLFB_OK;
		}
		else if ((var->red.length == 4) &&
				 (var->green.length == 4) && 
				 (var->blue.length == 4) && 
				 (var->red.offset == 8) &&
				 (var->green.offset == 4) && 
				 (var->blue.offset == 0) && 
				 (var->red.msb_right == 0) &&
				 (((var->transp.length == 4) && ((var->transp.offset == 12))) || ((var->transp.length == 0) && (var->transp.offset == 0))))
		{
			*pPVRPixelFormat = PVRSRV_PIXEL_FORMAT_ARGB4444;
			return PVRLFB_OK;
		}
		else
		{
			return PVRLFB_ERROR_INVALID_PARAMS;
		}
	}
	else if ((var->bits_per_pixel == 32) &&
			 (var->red.length == 8) &&
			 (var->green.length == 8) && 
			 (var->blue.length == 8) && 
			 (var->red.offset == 16) &&
			 (var->green.offset == 8) && 
			 (var->blue.offset == 0) && 
			 (var->red.msb_right == 0) &&
			 (((var->transp.length == 8) && ((var->transp.offset == 24))) || ((var->transp.length == 0) && (var->transp.offset == 0))))
	{
		*pPVRPixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
		return PVRLFB_OK;
	}
	else
	{
		return PVRLFB_ERROR_INVALID_PARAMS;
	}
}
/*!
******************************************************************************

 @Function	InitDev
 
 @Description specifies devices in the systems memory map
 
 @Input    psSysData - sys data

 @Return   PVRLFB_ERROR  : 

******************************************************************************/
static PVRLFB_ERROR FBInitDev(PVRLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo;
	struct module *psLINFBOwner;
	PVRLFB_FBINFO *psPVRFBInfo = &psDevInfo->sFBInfo;
	PVRLFB_ERROR eError = PVRLFB_ERROR_GENERIC;
	unsigned long ulFBSize;
	unsigned uiFBDevID = psDevInfo->uiFBDevID;

	PVRLFB_CONSOLE_LOCK();

	psLINFBInfo = registered_fb[uiFBDevID];
	if (psLINFBInfo == NULL)
	{
		eError = PVRLFB_ERROR_INVALID_DEVICE;
		goto errRelSem;
	}

	ulFBSize = (psLINFBInfo->screen_size) != 0 ?
					psLINFBInfo->screen_size :
					psLINFBInfo->fix.smem_len;

	/*
	 * Try and filter out invalid FB info structures.
	 */
	if (ulFBSize == 0 || psLINFBInfo->fix.line_length == 0)
	{
		eError = PVRLFB_ERROR_INVALID_DEVICE;
		goto errRelSem;
	}

	psLINFBOwner = psLINFBInfo->fbops->owner;
	if (!try_module_get(psLINFBOwner))
	{
		printk(KERN_INFO DRIVER_PREFIX
			": Device %u: Couldn't get framebuffer module\n",
				uiFBDevID);

		goto errRelSem;
	}

	if (psLINFBInfo->fbops->fb_open != NULL)
	{
		int res;

		res = psLINFBInfo->fbops->fb_open(psLINFBInfo, 0);
		if (res != 0)
		{
			printk(KERN_INFO DRIVER_PREFIX
				": Device %u: Couldn't open framebuffer (error %d)\n",
					uiFBDevID, res);

			goto errModPut;
		}
	}

	psDevInfo->psLINFBInfo = psLINFBInfo;

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer physical address: 0x%lx\n",
			uiFBDevID,
			psLINFBInfo->fix.smem_start));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual address: 0x%lx\n",
			uiFBDevID, (unsigned long)psLINFBInfo->screen_base));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer screen size: %lu\n",
			uiFBDevID, ulFBSize));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual width: %u\n",
			uiFBDevID, psLINFBInfo->var.xres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer virtual height: %u\n",
			uiFBDevID, psLINFBInfo->var.yres_virtual));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer width: %u\n",
			uiFBDevID, psLINFBInfo->var.xres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer height: %u\n",
			uiFBDevID, psLINFBInfo->var.yres));
	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
			": Device %u: Framebuffer stride: %u\n",
			uiFBDevID, psLINFBInfo->fix.line_length));

	psPVRFBInfo->sSysAddr.uiAddr = psLINFBInfo->fix.smem_start;
	psPVRFBInfo->sCPUVAddr       = psLINFBInfo->screen_base;
	psPVRFBInfo->ulWidth         = psLINFBInfo->var.xres;
	psPVRFBInfo->ulHeight        = psLINFBInfo->var.yres;
	psPVRFBInfo->ulByteStride    = psLINFBInfo->fix.line_length;
	psPVRFBInfo->ulScreenSize    = ulFBSize;

	eError = PVRPixelFormat(&psLINFBInfo->var, &psPVRFBInfo->ePixelFormat);
	if (eError != PVRLFB_OK)
	{
		printk(KERN_INFO DRIVER_PREFIX
			": Device %u: Invalid pixel format\n", uiFBDevID);

		goto errModPut;
	}

	eError = PVRLFB_OK;
	goto errRelSem;

errModPut:
	module_put(psLINFBOwner);
errRelSem:
	PVRLFB_CONSOLE_UNLOCK();
	return eError;
}

static void FBDeInitDev(PVRLFB_DEVINFO *psDevInfo)
{
	struct fb_info *psLINFBInfo = psDevInfo->psLINFBInfo;
	struct module *psLINFBOwner;

	PVRLFB_CONSOLE_LOCK();

	psLINFBOwner = psLINFBInfo->fbops->owner;

	if (psLINFBInfo->fbops->fb_release != NULL) 
	{
		(void) psLINFBInfo->fbops->fb_release(psLINFBInfo, 0);
	}

	module_put(psLINFBOwner);

	PVRLFB_CONSOLE_UNLOCK();
}


static PVRLFB_DEVINFO *PVRLFBInitDev(unsigned uiFBDevID)
{
	PVRLFB_DEVINFO *psDevInfo;

	/* allocate device info. structure */
	psDevInfo = (PVRLFB_DEVINFO *)PVRLFBAllocKernelMem(sizeof(PVRLFB_DEVINFO));

	if(psDevInfo == NULL)
	{
		return NULL;
	}

	/* any fields not set will be zero */
	memset(psDevInfo, 0, sizeof(PVRLFB_DEVINFO));

	psDevInfo->uiFBDevID = uiFBDevID;

	/* Get the kernel services function table */
	if (!(*gpfnGetPVRJTable)(&psDevInfo->sPVRJTable))
	{
		goto errFreeDevInfo;
	}

#if defined(PVRLFB_AUTO_UNBLANK)
	/* Set up work queue handler */
	INIT_WORK(&psDevInfo->sLINWork, WorkHandler
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20))
	, (void *) psDevInfo
#endif
	);
#endif
	/* save private fbdev information structure in the dev. info. */
	if (FBInitDev(psDevInfo) != PVRLFB_OK)
	{
		goto errFreeDevInfo;
	}

	/*
		Setup the devinfo
	*/
	psDevInfo->sDisplayInfo.ui32MinSwapInterval = 0;
	psDevInfo->sDisplayInfo.ui32MaxSwapInterval = 0;
	psDevInfo->sDisplayInfo.ui32MaxSwapChains = 0;
	psDevInfo->sDisplayInfo.ui32MaxSwapChainBuffers = 0;
	strncpy(psDevInfo->sDisplayInfo.szDisplayName, DISPLAY_DEVICE_NAME, MAX_DISPLAY_NAME_SIZE);

	psDevInfo->ulNumFormats = 1;

	psDevInfo->asDisplayFormatList[0].pixelformat = psDevInfo->sFBInfo.ePixelFormat;
	psDevInfo->ulNumDims = 1;
	psDevInfo->asDisplayDimList[0].ui32Width      = (IMG_UINT32)psDevInfo->sFBInfo.ulWidth;
	psDevInfo->asDisplayDimList[0].ui32Height     = (IMG_UINT32)psDevInfo->sFBInfo.ulHeight;
	psDevInfo->asDisplayDimList[0].ui32ByteStride = (IMG_UINT32)psDevInfo->sFBInfo.ulByteStride;
	psDevInfo->sSysFormat = psDevInfo->asDisplayFormatList[0];
	
	/* Setup system buffer */
	psDevInfo->sSystemBuffer.hSwapChain = NULL;
	psDevInfo->sSystemBuffer.sSysAddr = psDevInfo->sFBInfo.sSysAddr;
	psDevInfo->sSystemBuffer.sCPUVAddr = psDevInfo->sFBInfo.sCPUVAddr;
	psDevInfo->sSystemBuffer.ulBufferSize = psDevInfo->sFBInfo.ulScreenSize;

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
	

	/* register device with services and retrieve device index */
	if (psDevInfo->sPVRJTable.pfnPVRSRVRegisterDCDevice(
		&psDevInfo->sDCJTable,
		&psDevInfo->uiPVRDevID) != PVRSRV_OK)
	{
		goto errFreeDevInfo;
	}

	DEBUG_PRINTK((KERN_INFO DRIVER_PREFIX
		": Device %u: PVR Device ID: %u\n",
		psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID));
	
	/* return success */
	return psDevInfo;

errFreeDevInfo:
	PVRLFBFreeKernelMem(psDevInfo);
	return NULL;
}

PVRLFB_ERROR PVRLFBInit(void)
{
	unsigned i;
	unsigned uiDevicesFound = 0;

	if (PVRLFBGetLibFuncAddr("PVRGetDisplayClassJTable", &gpfnGetPVRJTable) != PVRLFB_OK)
	{
		return PVRLFB_ERROR_INIT_FAILURE;
	}

	/*
	 * We search for frame buffer devices backwards, as the last device
	 * registered with PVR Services will be the first device enumerated
	 * by PVR Services.
	 */
	for (i = PVRLFB_MAX_NUM_DEVICES; i-- != 0;)
	{
		PVRLFB_DEVINFO *psDevInfo = PVRLFBInitDev(i);

		if (psDevInfo != NULL)
		{
			/* Set the top-level anchor */
			PVRLFBSetDevInfoPtr(psDevInfo->uiFBDevID, psDevInfo);
			uiDevicesFound++;
		}
	}

	return (uiDevicesFound != 0) ? PVRLFB_OK : PVRLFB_ERROR_INIT_FAILURE;
}


/*
 *	PVRLFBDeInitDev
 *	Deinitialises the display class device component of the FBDev
 */
static PVRLFB_BOOL PVRLFBDeInitDev(PVRLFB_DEVINFO *psDevInfo)
{
	PVRSRV_ERROR eError;
	PVRSRV_DC_DISP2SRV_KMJTABLE	*psJTable = &psDevInfo->sPVRJTable;

	/* Remove display class device from kernel services device register */
	eError = psJTable->pfnPVRSRVRemoveDCDevice(psDevInfo->uiPVRDevID);
	if (eError != PVRSRV_OK)
	{
		printk(KERN_INFO DRIVER_PREFIX
			": Device %u: Couldn't unregister PVR device %u (error %d)\n",
			psDevInfo->uiFBDevID, psDevInfo->uiPVRDevID, eError);

		return PVRLFB_FALSE;
	}

	FBDeInitDev(psDevInfo);

	PVRLFBSetDevInfoPtr(psDevInfo->uiFBDevID, NULL);

	/* de-allocate data structure */
	PVRLFBFreeKernelMem(psDevInfo);
	
	return PVRLFB_TRUE;
}


/*
 *	PVRLFBDeInit
 *	Deinitialises all the display class device components
 */
PVRLFB_ERROR PVRLFBDeInit(void)
{
	unsigned i;
	PVRLFB_BOOL bError = PVRLFB_FALSE;

	for (i = 0; i < PVRLFB_MAX_NUM_DEVICES; i++)
	{
		PVRLFB_DEVINFO *psDevInfo = PVRLFBGetDevInfoPtr(i);

		if (psDevInfo != NULL)
		{
			bError |= !PVRLFBDeInitDev(psDevInfo);
		}
	}

	return (bError) ? PVRLFB_ERROR_INIT_FAILURE : PVRLFB_OK;
}

/******************************************************************************
 End of file (pvrlfb_displayclass.c)
******************************************************************************/

