/*************************************************************************/ /*!
@Title          PVRPDP linux driver functions
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

#include <linux/module.h>
#if defined(PVR_PDP_LINUX_FB)
#include <linux/fb.h>
#endif
#include <linux/pci.h>

#if defined(SUPPORT_DYNAMIC_GTF_TIMING)
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/delay.h>
#endif

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#endif

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "pvrpdp.h"
#include "pvrmodule.h"

#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#endif

#define unref__ __attribute__ ((unused))

#define	MAKESTRING(x) # x

#if !defined(DISPLAY_CONTROLLER)
#define DISPLAY_CONTROLLER pvrpdp
#endif

#define	DRVNAME	MAKESTRING(DISPLAY_CONTROLLER)

#if !defined(SUPPORT_DRI_DRM)
MODULE_SUPPORTED_DEVICE(DRVNAME);
#endif

static struct pci_dev *psPCIDev;
static PDP_BOOL bIsPCIE;

static PDP_BOOL bIsInInit = PDP_FALSE;

#if defined(SUPPORT_DRI_DRM)
int PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Init)(struct drm_device *dev)
{
	psPCIDev = dev->pdev;

	if (Init() != PDP_OK)
	{
		return -ENODEV;
	}

	return 0;
}
#else
/*****************************************************************************
 Function Name:	PVRPDP_Init
 Description  :	Insert the driver into the kernel.

				The device major number is allocated by the kernel dynamically
				if AssignedMajorNumber is zero on entry.  This means that the
				device node (nominally /dev/pvrpdp) may need to be re-made if
				the kernel varies the major number it assigns.  The number
				does seem to stay constant between runs, but I don't think
				this is guaranteed. The node is made as root on the shell
				with:

						mknod /dev/pvrpdp c ? 0

				where ? is the major number reported by the printk() - look
				at the boot log using `dmesg' to see this).

				__init places the function in a special memory section that
				the kernel frees once the function has been run.  Refer also
				to module_init() macro call below.

*****************************************************************************/
static int __init PVRPDP_Init(void)
{
	int error;

	bIsInInit = PDP_TRUE;
#if ATLAS_REV == 2
	psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCI_ATLAS2_FPGA, NULL);
	if (psPCIDev == NULL)
	{
		/* Couldn't find device ID. Try an alternate ID */
		psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCIE_ATLAS2_FPGA, NULL);
		if (psPCIDev != NULL)
		{
			bIsPCIE = PDP_TRUE;
		}
	}
#else	/* ATLAS_REV==2 */
	psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCI_EMULATOR, NULL);
	if (psPCIDev == NULL)
	{
		/* Couldn't find original device ID, so try alternate ID */
		psPCIDev = pci_get_device(PVRPDP_VENDOR_ID_POWERVR, PVRPDP_DEVICE_ID_PCI_FPGA, NULL);
	}
#endif	/* ATLAS_REV==2 */
	if (psPCIDev == NULL)
	{
		printk(KERN_ERR DRVNAME ": PVRPDP_Init:  pci_get_device failed\n");

		goto ExitError;
	}

	if ((error = pci_enable_device(psPCIDev)) != 0)
	{
		printk(KERN_ERR DRVNAME ": PVRPDP_Init: pci_enable_device failed (%d)", error);
		goto ExitError;
	}

	if(Init() != PDP_OK)
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

	bIsInInit = PDP_FALSE;
	return 0;

ExitDisable:
	pci_disable_device(psPCIDev);
ExitError:
	bIsInInit = PDP_FALSE;
	return -ENODEV;
} /*PVRPDP_Init*/
#endif	/* defined(SUPPORT_DRI_DRM) */

#if defined(SUPPORT_DRI_DRM)
void PVR_DRM_MAKENAME(DISPLAY_CONTROLLER, _Cleanup)(struct drm_device unref__ *dev)
{
	if (Deinit() != PDP_OK)
	{
		printk(KERN_ERR DRVNAME ": %s: Can't deinit device\n", __FUNCTION__);
	}
}
#else
/*****************************************************************************
 Function Name:	PVRPDP_Cleanup
 Description  :	Remove the driver from the kernel.

				__exit places the function in a special memory section that
				the kernel frees once the function has been run.  Refer also
				to module_exit() macro call below.

*****************************************************************************/
static void __exit PVRPDP_Cleanup(void)
{
	if(Deinit() != PDP_OK)
	{
		printk(KERN_ERR DRVNAME ": PVRPDP_Cleanup: Can't deinit device\n");
	}

}
#endif	/* defined(SUPPORT_DRI_DRM) */


void *AllocKernelMem(unsigned long ulSize)
{
	return kmalloc(ulSize, GFP_KERNEL);
}

void FreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}


PDP_ERROR OpenPVRServices (PDP_HANDLE *phPVRServices)
{
	/* Nothing to do - we have already checked services module insertion */
	*phPVRServices = 0;
	return (PDP_OK);
}


PDP_ERROR ClosePVRServices (PDP_HANDLE unref__ hPVRServices)
{
	/* Nothing to do */
	return (PDP_OK);
}

PDP_ERROR GetLibFuncAddr (PDP_HANDLE unref__ hExtDrv, char *szFunctionName, PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		return (PDP_ERROR_INVALID_PARAMS);
	}

	/* Nothing to do - should be exported from pvrsrv.ko */
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return (PDP_OK);
}

void WriteReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset, unsigned long ulValue)
{
	void *pvRegAddr = (void *)((unsigned char *)psDevInfo->sFBInfo.pvRegs + ulOffset);

	/* printk("writing reg %x val %x\n",pvRegAddr,ui32Value); */
	writel(ulValue, pvRegAddr);
}

unsigned long ReadReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset)
{
	return readl((unsigned char *)psDevInfo->sFBInfo.pvRegs + ulOffset);
}

#if defined (SUPPORT_DYNAMIC_GTF_TIMING)
void WriteTCFReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset, unsigned long ulValue)
{
	void *pvRegAddr = (void *)((unsigned char *)psDevInfo->sFBInfo.pvTCFRegs + ulOffset);

	writel(ulValue, pvRegAddr);
}

unsigned long ReadTCFReg(PVRPDP_DEVINFO *psDevInfo, unsigned long ulOffset)
{
	return readl((unsigned char *)psDevInfo->sFBInfo.pvTCFRegs + ulOffset);
}

void PDPReleaseThreadQuanta(void)
{
	// We can't use schedule in module init as context is atomic.
	if(bIsInInit)
		udelay(1);
	else
		schedule();
}

unsigned long PDPClockus(void)
{
	unsigned long time, j = jiffies;

	time = j * (1000000 / HZ);

	return time;
}

void PDPWaitus(unsigned long ulTimeus)
{
	udelay(ulTimeus);
}
#endif /* #if defined (SUPPORT_DYNAMIC_GTF_TIMING) */

void *MapPhysAddr(IMG_SYS_PHYADDR sSysAddr, unsigned long ulSize)
{
	void *pvAddr = ioremap(sSysAddr.uiAddr, ulSize);
	/* printk("MapPhysAddr: %x for %x to %x\n",sSysAddr.uiAddr, ulSize, pvAddr); */
	return pvAddr;
}

void UnMapPhysAddr(void *pvAddr, unsigned long ulSize)
{
	iounmap(pvAddr);
}

/* function to get the physical addresses of the device registers and memory */
PDP_ERROR OSGetDeviceAddresses(unsigned long *pulRegBaseAddr, unsigned long *pulMemBaseAddr, unsigned long *pulSysSurfaceOffset)
{
	if (psPCIDev == NULL)
	{
		return (PDP_ERROR_INVALID_DEVICE);
	}

#if ATLAS_REV == 2
	/*
	 * We don't do a pci_request_region for either PVRPDP_REG_PCI_BASENUM
	 * or PVRPDP_MEM_PCI_BASENUM, we assume the SGX driver has done this
	 * already.
	 */
	*pulRegBaseAddr = pci_resource_start(psPCIDev, PVRPDP_REG_PCI_BASENUM);
	*pulMemBaseAddr = pci_resource_start(psPCIDev, PVRPDP_MEM_PCI_BASENUM);
	*pulSysSurfaceOffset = bIsPCIE ? PVRPDP_PCIE_SYSSURFACE_OFFSET : PVRPDP_SYSSURFACE_OFFSET;
#else /* ATLAS_REV==2 */
	/*
	 * We don't do a pci_request_region for PVRPDP_PCI_BASENUM, 
	 * we assume the SGX driver has done this already.
	 */
	*pulRegBaseAddr = pci_resource_start(psPCIDev, PVRPDP_PCI_BASENUM);
	*pulMemBaseAddr = *pui32RegBaseAddr + PVRPDP_PCI_MEM_OFFSET;
	*pulSysSurfaceOffset = PVRPDP_SYSSURFACE_OFFSET;
#endif /* ATLAS_REV == 2 */

	return (PDP_OK);
}

#if defined(SYS_USING_INTERRUPTS) && defined(PDP_DEVICE_ISR)
/* OS ISR callback change function args/return type as required */
PDP_ERROR OSVSyncISR (PVRPDP_DEVINFO *psDevInfo)
{
	PDPVSyncISR(psDevInfo);

	return (PDP_OK);
}

PDP_ERROR InstallVsyncISR (PVRPDP_DEVINFO *psDevInfo)
{
	/*
		call OS routines to install ISR, specifying:
		IRQ/SHIRQ
		"PDP VSync ISR"
		OSVSyncISR
		psDevInfo
	*/
	UNREFERENCED_PARAMETER(psDevInfo);

	return (PDP_OK);
}


PDP_ERROR UninstallVsyncISR (PVRPDP_DEVINFO *psDevInfo)
{
	/* 
		call OS routines to uninstall ISR
	*/
	UNREFERENCED_PARAMETER(psDevInfo);

	return (PDP_OK);
}
#endif/* #if defined(SYS_USING_INTERRUPTS) && defined(PDP_DEVICE_ISR)*/

#if !defined(PVR_PDP_LINUX_FB)
PDP_ERROR OSRegisterDevice(PVRPDP_DEVINFO unref__ *psDevInfo)
{
	return (PDP_OK);
}

PDP_ERROR OSUnregisterDevice(PVRPDP_DEVINFO unref__ *psDevInfo)
{
	return (PDP_OK);
}
#else	/* !defined(PVR_PDP_LINUX_FB) */

#define	PVRPDP_PSEUDO_PALETTE_SIZE	256

static char *mode_option;

struct sPDPPar
{
	unsigned char *byBase;
	unsigned long ulPseudoPalette[PVRPDP_PSEUDO_PALETTE_SIZE];
};

/*****************************************************************************
 Function Name:	PVRPDPCheckVar
 Description  :	Validates a var passed in. 

*****************************************************************************/
static int
PVRPDPCheckVar(struct fb_var_screeninfo *psScreenInfo, struct fb_info *psFBInfo)
{
    PDP_BOOL bMode = PDP_FALSE;

    switch (psScreenInfo->xres)
    {
        case 640:
            if (psScreenInfo->yres == 480)
            {
                bMode=PDP_TRUE;
            }
            break;
    }

    if (!bMode)
    {
        printk(KERN_WARNING DRVNAME ": PVRPDPCheckVar: mode (%dx%d) not supported\n",
               psScreenInfo->xres, psScreenInfo->yres);
        return -EINVAL;
    }

    psScreenInfo->red.msb_right = 0;
    psScreenInfo->green.msb_right = 0;
    psScreenInfo->blue.msb_right = 0;

    switch (psScreenInfo->bits_per_pixel)
    {
        case 32:
            psScreenInfo->transp.offset = 24;
            psScreenInfo->transp.length = 8;
            psScreenInfo->red.offset = 16;
            psScreenInfo->red.length = 8;
            psScreenInfo->green.offset = 8;
            psScreenInfo->green.length = 8;
            psScreenInfo->blue.offset = 0;
            psScreenInfo->blue.length = 8;
            break;
        case 16:
            psScreenInfo->transp.offset = 0;
            psScreenInfo->transp.length = 0;
            psScreenInfo->red.offset = 11;
            psScreenInfo->red.length = 5;
            psScreenInfo->green.offset = 5;
            psScreenInfo->green.length = 6;
            psScreenInfo->blue.offset = 0;
            psScreenInfo->blue.length = 5;
            break;
        default:
            printk (KERN_WARNING DRVNAME ": PVRPDPCheckVar: no support for %dbpp\n",
                    psScreenInfo->bits_per_pixel);
    }

    psScreenInfo->yres_virtual= psFBInfo->fix.smem_len / psFBInfo->fix.line_length;

    if (psScreenInfo->xres_virtual != psScreenInfo->xres || psScreenInfo->yres_virtual < psScreenInfo->yres)
    {
        return -EINVAL;
    }

    /* We only support panning vertically for the sake of implementing
     * backbuffers. */
    if (psScreenInfo->xoffset != 0)
    {
        return -EINVAL;
    }

    if (psScreenInfo->yoffset + psScreenInfo->yres > psScreenInfo->yres_virtual)
    {
        return -EINVAL;
    }

    psScreenInfo->nonstd = 0;

    return 0;
}

/*****************************************************************************
 Function Name:	PVRPDPSetPar
 Description  :	Alters the hardware state. 

*****************************************************************************/
static int PVRPDPSetPar(struct fb_info *info)
{
    return 0;
}

/*****************************************************************************
 Function Name:	PVRPDPPanDisplay
 Description  :	Pans the display.

*****************************************************************************/
static int PVRPDPPanDisplay(struct fb_var_screeninfo *psScreenInfo,
                            struct fb_info *psFBInfo)
{
    struct sPDPPar *psPar = (struct sPDPPar *) psFBInfo->par;

    unsigned long ulBase = PVRPDP_SYSSURFACE_OFFSET;

    if (psScreenInfo->xoffset != 0)
    {
        return -EINVAL;
    }

    if (psScreenInfo->yoffset + psScreenInfo->yres > psScreenInfo->yres_virtual)
    {
        return -EINVAL;
    }

    ulBase += psScreenInfo->yoffset * psFBInfo->fix.line_length;

    writel(PVRPDP_STR1ADDRCTRL_STREAMENABLE
           |(ulBase >> PVRPDP_STR1ADDRCTRL_ADDR_ALIGNSHIFT),
             psPar->byBase + PVRPDP_STR1ADDRCTRL);

    return 0;
}

/*****************************************************************************
 Function Name:	PVRPDPSetColReg
 Description  :	Sets a color register.

*****************************************************************************/
static int PVRPDPSetColReg(unsigned int uiRegno,
                           unsigned int uiRed,
                           unsigned int uiGreen,
                           unsigned int uiBlue,
                           unsigned int uiTransp,
                           struct fb_info *psFBInfo)
{
    if (uiRegno >= psFBInfo->cmap.len || uiRegno >= PVRPDP_PSEUDO_PALETTE_SIZE)
    {
        return -EINVAL;
    }

    switch (psFBInfo->var.bits_per_pixel)
    {
        case 16:
            ((IMG_UINT32 *) psFBInfo->pseudo_palette)[uiRegno] =
                ((uiRed & 0xf800)) | ((uiGreen & 0xfc00) >> 5) |
                ((uiBlue & 0xf800) >> 11);
            break;
        case 32:
            ((IMG_UINT32 *) psFBInfo->pseudo_palette)[uiRegno] =
            ((uiTransp & 0xff00) << 16) | ((uiRed & 0xff00) << 8) |
            ((uiGreen & 0xff00)) | ((uiBlue & 0xff00) >> 8);
            break;
        default:
            return 1;
    }

    return 0;
}
/*****************************************************************************
 Function Name:	PVRPDPBlank
 Description  :	Blanks the display.

*****************************************************************************/
static int PVRPDPBlank(int iBlanMode, struct fb_info *psFBInfo)
{
    return 0;
}

/*****************************************************************************
 Description  :	PVRPDP fops
*****************************************************************************/

static struct fb_ops sPVRPDPFbOps = {
    .owner          = THIS_MODULE,
    .fb_check_var   = PVRPDPCheckVar,
    .fb_set_par     = PVRPDPSetPar,
    .fb_setcolreg   = PVRPDPSetColReg,
    .fb_pan_display = PVRPDPPanDisplay,
    .fb_blank       = PVRPDPBlank,
    .fb_fillrect    = cfb_fillrect,
    .fb_copyarea    = cfb_copyarea,
    .fb_imageblit   = cfb_imageblit,
};

static struct fb_videomode sPDPDefaultMode = {
	.xres           = PVRPDP_WIDTH,
	.yres           = PVRPDP_HEIGHT,
	.pixclock       = 25000,
	.left_margin    = 88,
	.right_margin   = 40,
	.upper_margin   = 23,
	.lower_margin   = 1,
	.hsync_len      = 128,
	.vsync_len      = 4,
	.sync           = FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
	.vmode          = FB_VMODE_NONINTERLACED
};

/*****************************************************************************
 Function Name:	OSRegisterDevice
 Description  :	Registers the device 
*****************************************************************************/

PDP_ERROR OSRegisterDevice(PVRPDP_DEVINFO *psDevInfo)
{
    struct fb_info *psFBInfo;
    struct sPDPPar *psPar;

    psFBInfo = framebuffer_alloc(sizeof(struct sPDPPar), 0);

    if (!psFBInfo)
    {
        printk(KERN_ERR DRVNAME ": OSRegisterDevice: Unable to alloc frame buffer.\n");
        return (PDP_ERROR_OUT_OF_MEMORY);
    }

    psPar = psFBInfo->par;

    psFBInfo->fix.accel = FB_ACCEL_NONE;

    psFBInfo->fix.type = FB_TYPE_PACKED_PIXELS;
    psFBInfo->fix.type_aux = 0;
    psFBInfo->fix.visual = FB_VISUAL_TRUECOLOR;

    psFBInfo->fix.xpanstep = 0;
    psFBInfo->fix.ypanstep = 4;
    psFBInfo->fix.ywrapstep = 0;

    psFBInfo->fix.line_length = PVRPDP_STRIDE;

    psFBInfo->var.nonstd = 0;
    psFBInfo->var.height = -1;
    psFBInfo->var.width = -1;
    psFBInfo->var.vmode = FB_VMODE_NONINTERLACED;

    psFBInfo->var.transp.offset = 24;
    psFBInfo->var.transp.length = 8;
    psFBInfo->var.red.offset = 16;
    psFBInfo->var.red.length = 8;
    psFBInfo->var.green.offset = 8;
    psFBInfo->var.green.length = 8;
    psFBInfo->var.blue.offset = 0;
    psFBInfo->var.blue.length = 8;

    psFBInfo->fbops = &sPVRPDPFbOps;
    psFBInfo->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;

    psFBInfo->pseudo_palette = (u32 *) (psPar->ulPseudoPalette);

    strncpy(&psFBInfo->fix.id[0], DRVNAME, sizeof(psFBInfo->fix.id));

    psFBInfo->fix.mmio_start = (IMG_UINT32) psDevInfo->sFBInfo.sRegSysAddr.uiAddr;
    psFBInfo->fix.mmio_len = PVRPDP_REG_SIZE;
    psPar->byBase = (unsigned char *)(psDevInfo->sFBInfo.pvRegs);

    psFBInfo->fix.smem_start = (unsigned long) psDevInfo->sFBInfo.sSysAddr.uiAddr;
    psFBInfo->fix.smem_len =  PVRPDP_STRIDE * PVRPDP_HEIGHT * (PVRPDP_MAX_BACKBUFFERS + 1);
    psFBInfo->screen_base = (char __iomem *) MapPhysAddr(psDevInfo->sFBInfo.sSysAddr, psFBInfo->fix.smem_len);

    if (!psFBInfo->screen_base)
    {
        goto err_map_video;
    }

    psFBInfo->var.xres = PVRPDP_WIDTH;
    psFBInfo->var.yres = PVRPDP_HEIGHT;
    psFBInfo->var.xres_virtual = PVRPDP_WIDTH;
    psFBInfo->var.yres_virtual = (u32) (psFBInfo->fix.smem_len / psFBInfo->fix.line_length);
    psFBInfo->var.bits_per_pixel = 32;

   if (!fb_find_mode(&psFBInfo->var,
                     psFBInfo,
                     mode_option,
                     NULL, 0,
                     &sPDPDefaultMode, 32))
   {
        printk(KERN_ERR DRVNAME ": OSRegisterDevice: Unable to find usable video mode.\n");
        goto err_find_mode;
    }

    if (fb_alloc_cmap(&psFBInfo->cmap, 256, 0) < 0)
    {
        printk(KERN_ERR DRVNAME ": OSRegisterDevice: Unable to alloc cmap.\n");
        goto err_alloc_cmap;
    }

    if(register_framebuffer(psFBInfo) < 0)
    {
        printk(KERN_ERR DRVNAME ": OSRegisterDevice: Unable to register frame buffer.\n");
        goto err_reg_fb;
    }

    psDevInfo->hOSHandle = (IMG_HANDLE) psFBInfo;

    return (PDP_OK);

err_reg_fb:
    fb_dealloc_cmap(&psFBInfo->cmap);
err_alloc_cmap:
err_find_mode:
    UnMapPhysAddr(psFBInfo->screen_base, psFBInfo->fix.smem_len);
err_map_video:
    framebuffer_release(psFBInfo);

    return (PDP_ERROR_GENERIC);
}

/*****************************************************************************
 Function Name:	OSUnregisterDevice
 Description  :	Remove the device from the OS.
*****************************************************************************/
PDP_ERROR OSUnregisterDevice(PVRPDP_DEVINFO *psDevInfo)
{
	if (psDevInfo->hOSHandle)
	{
		struct fb_info *psFBInfo = (struct fb_info *) psDevInfo->hOSHandle;

		if (unregister_framebuffer(psFBInfo))
		{
			printk(KERN_ERR DRVNAME ": OSUnregisterDevice: Unable to release frame buffer.\n");
			return (PDP_ERROR_GENERIC);
		}

		fb_dealloc_cmap(&psFBInfo->cmap);

		UnMapPhysAddr(psFBInfo->screen_base, psFBInfo->fix.smem_len);

		framebuffer_release(psFBInfo);

		psDevInfo->hOSHandle = 0;
	}

	return (PDP_OK);
}
#endif	/* !defined(PVR_PDP_LINUX_FB) */

#if !defined(SUPPORT_DRI_DRM)
/*
	These macro calls define the initialisation and removal functions of the
	driver.  Although they are prefixed `module_', they apply when compiling
	statically as well; in both cases they define the function the kernel will
	run to start/stop the driver.
*/
module_init(PVRPDP_Init);
module_exit(PVRPDP_Cleanup);
#endif
