/*************************************************************************/ /*!
@Title          Linux frame buffer driver.
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

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/slab.h>

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#else
#include <linux/module.h>
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "pvrlfb.h"
#include "pvrmodule.h"

#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#endif

#if !defined(SUPPORT_DRI_DRM)
MODULE_SUPPORTED_DEVICE(DRVNAME);
#endif

#define unref__ __attribute__ ((unused))


/*****************************************************************************
 Function Name:	PVRLFB_Init
 Description  :	Insert the driver into the kernel.

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device unref__ *dev)
#else
static int __init PVRLFB_Init(void)
#endif
{
	if (PVRLFBInit() != PVRLFB_OK)
	{
		return -ENODEV;
	}

	return 0;
}

/*****************************************************************************
 Function Name:	PVRLFB_Cleanup
 Description  :	Remove the driver from the kernel.

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device unref__ *dev)
#else
static void __exit PVRLFB_Cleanup(void)
#endif
{
	if (PVRLFBDeInit() != PVRLFB_OK)
	{
		printk("%s: Can't deinit device\n", __FUNCTION__);
	}
}


void *PVRLFBAllocKernelMem(unsigned long ulSize)
{
	return kmalloc(ulSize, GFP_KERNEL);
}

void PVRLFBFreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}


PVRLFB_ERROR PVRLFBGetLibFuncAddr(char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if (strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		return PVRLFB_ERROR_INVALID_PARAMS;
	}

	/* Nothing to do - should be exported from pvrsrv.ko */
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return PVRLFB_OK;
}

#if !defined(SUPPORT_DRI_DRM)
/*
 These macro calls define the initialisation and removal functions of the
 driver.  Although they are prefixed `module_', they apply when compiling
 statically as well; in both cases they define the function the kernel will
 run to start/stop the driver.
*/
module_init(PVRLFB_Init);
module_exit(PVRLFB_Cleanup);
#endif
