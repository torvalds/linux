/*************************************************************************/ /*!
@Title          PVRPDP_EMULATOR linux structures and prototypes
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

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#include <linux/module.h>
#include <linux/pci.h>

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "pvrpdp_emulator.h"
#include "pvrmodule.h"

#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#endif

#define unref__ __attribute__ ((unused))

#define	MAKESTRING(x) # x

#if !defined(DISPLAY_CONTROLLER)
#define DISPLAY_CONTROLLER pvrpdp_emulator
#endif

#define	DRVNAME	MAKESTRING(DISPLAY_CONTROLLER)

#if !defined(SUPPORT_DRI_DRM)
MODULE_SUPPORTED_DEVICE(DRVNAME);
#endif

struct pci_dev *psPCIDev;

#if defined(PVRPDP_GET_BUFFER_DIMENSIONS)
static unsigned long width = PVRPDP_EMULATOR_WIDTH;
static unsigned long height = PVRPDP_EMULATOR_HEIGHT;
static unsigned long depth = 32;

module_param(width, ulong, S_IRUGO);
module_param(height, ulong, S_IRUGO);
module_param(depth, ulong, S_IRUGO);

IMG_BOOL GetBufferDimensions(IMG_UINT32 *pui32Width, IMG_UINT32 *pui32Height, PVRSRV_PIXEL_FORMAT *pePixelFormat, IMG_UINT32 *pui32Stride)
{
	if (width == 0 || height == 0 || depth == 0 ||
		depth != pvrpdp_roundup_bit_depth(depth))
	{
		printk(KERN_WARNING DRVNAME ": Illegal module parameters (width %lu, height %lu, depth %lu)\n", width, height, depth);
		return IMG_FALSE;
	}

	*pui32Width = (IMG_UINT32)width;
	*pui32Height = (IMG_UINT32)height;

	switch(depth)
	{
		case 32:
			*pePixelFormat = PVRSRV_PIXEL_FORMAT_ARGB8888;
			break;
		case 16:
			*pePixelFormat = PVRSRV_PIXEL_FORMAT_RGB565;
			break;
		default:
			printk(KERN_WARNING DRVNAME ": Display depth %lu not supported\n", depth);
			*pePixelFormat = PVRSRV_PIXEL_FORMAT_UNKNOWN;
			return IMG_FALSE;
	}
			
	*pui32Stride = pvrpdp_byte_stride(width, depth);

#if defined(DEBUG)
	printk(KERN_INFO DRVNAME " Width: %lu\n", (unsigned long)*pui32Width);
	printk(KERN_INFO DRVNAME " Height: %lu\n", (unsigned long)*pui32Height);
	printk(KERN_INFO DRVNAME " Depth: %lu bits\n", depth);
	printk(KERN_INFO DRVNAME " Stride: %lu bytes\n", (unsigned long)*pui32Stride);
#endif	/* defined(DEBUG) */

	return IMG_TRUE;
}
#endif	/* defined(DC_NOHW_GET_BUFFER_DIMENSIONS) */

#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device *dev)
{
	psPCIDev = dev->pdev;

	if (Init() != PVRSRV_OK)
	{
		return -ENODEV;
	}

	return 0;
}
#else
/*****************************************************************************
 Function Name:	PVRPDP_EMULATOR_Init
 Description  :	Insert the driver into the kernel.

				The device major number is allocated by the kernel dynamically
				if AssignedMajorNumber is zero on entry.  This means that the
				device node (nominally /dev/pvrpdp) may need to be re-made if
				the kernel varies the major number it assigns.  The number
				does seem to stay constant between runs, but I don't think
				this is guaranteed. The node is made as root on the shell
				with:

						mknod /dev/pvrpdp_emulator c ? 0

				where ? is the major number reported by the printk() - look
				at the boot log using `dmesg' to see this).

				__init places the function in a special memory section that
				the kernel frees once the function has been run.  Refer also
				to module_init() macro call below.

*****************************************************************************/
static IMG_INT __init PVRPDP_EMULATOR_Init(IMG_VOID)
{
	IMG_INT error;

	psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCI_EMULATOR, NULL);
	if (psPCIDev == NULL)
	{
		/* Couldn't find original device ID, so try alternate ID */
		psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCI_FPGA, NULL);
	}
	if (psPCIDev == NULL)
	{
		/* Look for the PCI Express based emulator board */
		psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCIE_EMULATOR, NULL);
	}

	if (psPCIDev == NULL)
	{
		printk(KERN_ERR DRVNAME ": PVRPDP_EMULATOR_Init:  pci_get_device failed\n");

		goto ExitError;
	}

	if ((error = pci_enable_device(psPCIDev)) != 0)
	{
		printk(KERN_ERR DRVNAME ": PVRPDP_EMULATOR_Init: pci_enable_device failed (%d)\n", error);
		goto ExitError;
	}

	if(Init() != PVRSRV_OK)
	{
		goto ExitDisable;
	}

	/*
	 * To prevent possible problems with system suspend/resume, we don't
	 * keep the device enabled, but rely on the fact that the SGX driver
	 * will have done a pci_enable_device.
	 */
	pci_disable_device(psPCIDev);
	psPCIDev = NULL;

	return 0;

ExitDisable:
	pci_disable_device(psPCIDev);
ExitError:
	return -ENODEV;
} /*PVRPDP_EMULATOR_Init*/
#endif	/* defined(SUPPORT_DRI_DRM) */

#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device unref__ *dev)
{
	if (Deinit() != PVRSRV_OK)
	{
		printk ("PVRPDP_EMULATOR_Cleanup: can't deinit device\n");
	}
}
#else
/*****************************************************************************
 Function Name:	PVRPDP_EMULATOR_Cleanup
 Description  :	Remove the driver from the kernel.

				__exit places the function in a special memory section that
				the kernel frees once the function has been run.  Refer also
				to module_exit() macro call below.

*****************************************************************************/
static IMG_VOID __exit PVRPDP_EMULATOR_Cleanup(IMG_VOID)
{    
	if(Deinit() != PVRSRV_OK)
	{
		printk ("PVRPDP_EMULATOR_Cleanup: can't deinit device\n");
	}
} /*PVRPDP_EMULATOR_Cleanup*/
#endif	/* defined(SUPPORT_DRI_DRM) */

IMG_VOID *AllocKernelMem(IMG_UINT32 ui32Size)
{
	return kmalloc(ui32Size, GFP_KERNEL);
}

IMG_VOID FreeKernelMem(IMG_VOID *pvMem)
{
	kfree(pvMem);
}


PVRSRV_ERROR OpenPVRServices (IMG_HANDLE *phPVRServices)
{
	/* Nothing to do - we have already checked services module insertion */
	*phPVRServices = 0;
	return PVRSRV_OK;
}


PVRSRV_ERROR ClosePVRServices (IMG_HANDLE unref__ hPVRServices)
{
	/* Nothing to do */
	return PVRSRV_OK;
}

PVRSRV_ERROR GetLibFuncAddr (IMG_HANDLE unref__ hExtDrv, IMG_CHAR *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
		return PVRSRV_ERROR_INVALID_PARAMS;

	/* Nothing to do - should be exported from pvrsrv.ko */
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return PVRSRV_OK;
}


IMG_VOID WriteSOCReg(PVRPDP_EMULATOR_DEVINFO *psDevInfo, IMG_UINT32 ui32Offset, IMG_UINT32 ui32Value)
{
	IMG_VOID *pvRegAddr = (IMG_VOID *)((IMG_UINT8 *)psDevInfo->sFBInfo.pvSOCRegs + ui32Offset);

	/* printk("writing reg %x val %x\n",pvRegAddr,ui32Value); */
	writel(ui32Value, pvRegAddr);
}


IMG_UINT32 ReadSOCReg(PVRPDP_EMULATOR_DEVINFO *psDevInfo, IMG_UINT32 ui32Offset)
{
	return readl((IMG_UINT8 *)psDevInfo->sFBInfo.pvSOCRegs + ui32Offset);
}

IMG_VOID *MapPhysAddr(IMG_SYS_PHYADDR sSysAddr, IMG_UINT32 ui32Size)
{
	IMG_VOID *pvAddr = ioremap(sSysAddr.uiAddr, ui32Size);
	return pvAddr;
}

IMG_VOID UnMapPhysAddr(IMG_VOID *pvAddr, IMG_UINT32 ui32Size)
{
	iounmap(pvAddr);
}


#if 0
IMG_UINT32 GetPCIResourceStart(IMG_UINT32 ui32Resource)
{
	IMG_UINT32 ui32Start = pci_resource_start(psPCIDev, ui32Resource);

#ifdef	DEBUG
	printk(KERN_INFO DRVNAME ": GetPCIResourceStart: Resource: %lu Start: %lx\n", ui32Resource, ui32Start);
#endif
	return ui32Start;
}
#endif


/* function to get the physical addresses of the device registers and memory */
PVRSRV_ERROR OSGetDeviceAddresses(IMG_UINT32 *pui32RegBaseAddr,
								  IMG_UINT32 *pui32SOCBaseAddr,
								  IMG_UINT32 *pui32MemBaseAddr,
								  IMG_UINT32 *pui32MemSize)
{
	if (psPCIDev == NULL)
	{
		return PVRSRV_ERROR_INVALID_DEVICE;
	}
#if 0
	/*
	 * We don't do a pci_request_region for PVRPDP_PCI_BASENUM, 
	 * we assume the SGX driver has done this already.
	 */
	*pui32MemBaseAddr = pci_resource_start(psPCIDev, PVRPDP_MEM_PCI_BASENUM);
	*pui32MemSize = 0; // INTEGRATION_POINT: implement auto-discovery of memory size.

	/*
	 * These are not used. If there are ever required, they need to be read from
	 * the other PCI BARs.
	 */
	*pui32RegBaseAddr = 0xDEADBEEF;
	*pui32SOCBaseAddr = 0xDEADBEEF;
#endif
	return PVRSRV_OK;
}

#if !defined(SUPPORT_DRI_DRM)
/*
 These macro calls define the initialisation and removal functions of the
 driver.  Although they are prefixed `module_', they apply when compiling
 statically as well; in both cases they define the function the kernel will
 run to start/stop the driver.
*/
module_init(PVRPDP_EMULATOR_Init);
module_exit(PVRPDP_EMULATOR_Cleanup);
#endif
