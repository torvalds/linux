/*************************************************************************/ /*!
@File
@Title          Linux module setup
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

#if (!defined(LDM_PLATFORM) && !defined(LDM_PCI)) || \
	(defined(LDM_PLATFORM) && defined(LDM_PCI))
	#error "LDM_PLATFORM or LDM_PCI must be defined"
#endif

#if defined(SUPPORT_DRM)
#define	PVR_MOD_STATIC
#else
#define	PVR_MOD_STATIC	static
#endif

#if defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
#define PVR_USE_PRE_REGISTERED_PLATFORM_DEV
#endif

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>


#if defined(SUPPORT_DRM_AUTH_IMPORT)
#include <linux/list.h>
#endif

#if defined(SUPPORT_DRM)
#include <drm/drmP.h>
#endif

#if defined(LDM_PLATFORM)
#include <linux/platform_device.h>
#endif

#if defined(LDM_PCI)
#include <linux/pci.h>
#endif

#include <linux/device.h>

#include "img_defs.h"
#include "kerneldisplay.h"
#include "mm.h"
#include "allocmem.h"
#include "mmap.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "connection_server.h"
#include "handle.h"
#include "pvr_debugfs.h"
#include "pvrmodule.h"
#include "private_data.h"
#include "driverlock.h"
#include "linkage.h"
#include "power.h"
#include "env_connection.h"
#include "sysinfo.h"
#include "pvrsrv.h"
#include "process_stats.h"

#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING) || defined(SUPPORT_DRM)
#include "syscommon.h"
#endif

#if defined(SUPPORT_DRM)
#include "pvr_drm.h"
#endif
#if defined(SUPPORT_AUTH)
#include "osauth.h"
#endif

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
#include "pvr_sync.h"
#endif

#if defined(SUPPORT_GPUTRACE_EVENTS)
#include "pvr_gputrace.h"
#endif

#if defined(SUPPORT_KERNEL_HWPERF) || defined(SUPPORT_SHARED_SLC)
#include "rgxapi_km.h"
#endif
#include "pvrversion.h"  //add by zxl

#if defined(CONFIG_OF)
#include <linux/of.h>
#include <linux/of_device.h>
#endif

/*
 * DRVNAME is the name we use to register our driver.
 * DEVNAME is the name we use to register actual device nodes.
 */
#define	DRVNAME		PVR_LDM_DRIVER_REGISTRATION_NAME
#define DEVNAME		PVRSRV_MODNAME

#if defined(SUPPORT_DRM)
#define PRIVATE_DATA(pFile) ((pFile)->driver_priv)
#else
#define PRIVATE_DATA(pFile) ((pFile)->private_data)
#endif

/*
 * This is all module configuration stuff required by the linux kernel.
 */
MODULE_SUPPORTED_DEVICE(DEVNAME);

#if defined(PVRSRV_NEED_PVR_DPF)
#include <linux/moduleparam.h>
extern IMG_UINT32 gPVRDebugLevel;
module_param(gPVRDebugLevel, uint, 0644);
MODULE_PARM_DESC(gPVRDebugLevel, "Sets the level of debug output (default 0x7)");
#endif /* defined(PVRSRV_NEED_PVR_DPF) */

/*
 * Newer kernels no longer support __devinitdata, __devinit, __devexit, or
 * __devexit_p.
 */
#if !defined(__devinitdata)
#define __devinitdata
#endif
#if !defined(__devinit)
#define __devinit
#endif
#if !defined(__devexit)
#define __devexit
#endif
#if !defined(__devexit_p)
#define __devexit_p(x) (&(x))
#endif

#if defined(SUPPORT_DISPLAY_CLASS)
/* Display class interface */
EXPORT_SYMBOL(DCRegisterDevice);
EXPORT_SYMBOL(DCUnregisterDevice);
EXPORT_SYMBOL(DCDisplayConfigurationRetired);
EXPORT_SYMBOL(DCImportBufferAcquire);
EXPORT_SYMBOL(DCImportBufferRelease);
#endif

/* Physmem interface (required by LMA DC drivers) */
EXPORT_SYMBOL(PhysHeapAcquire);
EXPORT_SYMBOL(PhysHeapRelease);
EXPORT_SYMBOL(PhysHeapGetType);
EXPORT_SYMBOL(PhysHeapGetAddress);
EXPORT_SYMBOL(PhysHeapGetSize);
EXPORT_SYMBOL(PhysHeapCpuPAddrToDevPAddr);

/* System interface (required by DC drivers) */
#if defined(SUPPORT_SYSTEM_INTERRUPT_HANDLING) && !defined(SUPPORT_DRM)
EXPORT_SYMBOL(SysInstallDeviceLISR);
EXPORT_SYMBOL(SysUninstallDeviceLISR);
#endif

EXPORT_SYMBOL(PVRSRVCheckStatus);
EXPORT_SYMBOL(PVRSRVGetErrorStringKM);

#if defined(SUPPORT_KERNEL_HWPERF)
EXPORT_SYMBOL(RGXHWPerfConnect);
EXPORT_SYMBOL(RGXHWPerfDisconnect);
EXPORT_SYMBOL(RGXHWPerfControl);
EXPORT_SYMBOL(RGXHWPerfConfigureAndEnableCounters);
EXPORT_SYMBOL(RGXHWPerfDisableCounters);
EXPORT_SYMBOL(RGXHWPerfAcquireData);
EXPORT_SYMBOL(RGXHWPerfReleaseData);
#endif

#if defined(SUPPORT_SHARED_SLC)
EXPORT_SYMBOL(RGXInitSLC);
#endif

#if !defined(SUPPORT_DRM)
/*
 * Device class used for /sys entries (and udev device node creation)
 */
static struct class *psPvrClass;

/*
 * This is the major number we use for all nodes in /dev.
 */
static int AssignedMajorNumber;

static const struct of_device_id rockchip_gpu_dt_ids[] = {
    { .compatible = "arm,rogue-G6110", },
	{ .compatible = "arm,rk3368-gpu", },
	{},
};

/*
 * These are the operations that will be associated with the device node
 * we create.
 *
 * With gcc -W, specifying only the non-null members produces "missing
 * initializer" warnings.
*/
static int PVRSRVOpen(struct inode* pInode, struct file* pFile);
static int PVRSRVRelease(struct inode* pInode, struct file* pFile);

static struct file_operations pvrsrv_fops =
{
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= PVRSRV_BridgeDispatchKM,
#if defined(CONFIG_COMPAT)
	.compat_ioctl	= PVRSRV_BridgeCompatDispatchKM,
#endif
	.open		= PVRSRVOpen,
	.release	= PVRSRVRelease,
	.mmap		= MMapPMR,
};
#endif	/* !defined(SUPPORT_DRM) */

struct mutex gPVRSRVLock;

#if defined(SUPPORT_DRM_AUTH_IMPORT)
static LIST_HEAD(sDRMAuthListHead);
#endif

#if defined(LDM_PLATFORM)
#define	LDM_DEV	struct platform_device
#define	LDM_DRV	struct platform_driver
#define TO_LDM_DEV(d) to_platform_device(d)
#endif /*LDM_PLATFORM */

#if defined(LDM_PCI)
#define	LDM_DEV	struct pci_dev
#define	LDM_DRV	struct pci_driver
#define TO_LDM_DEV(d) to_pci_device(d)
#endif /* LDM_PCI */

#if defined(LDM_PLATFORM)
static int PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device);
#endif

#if defined(LDM_PCI)
static void PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device, const struct pci_device_id *id);
#endif

static void PVRSRVDriverShutdown(LDM_DEV *device);
static int PVRSRVDriverSuspend(struct device *device);
static int PVRSRVDriverResume(struct device *device);

#if defined(LDM_PCI)
/* This structure is used by the Linux module code */
struct pci_device_id powervr_id_table[] __devinitdata = {
	{PCI_DEVICE(SYS_RGX_DEV_VENDOR_ID, SYS_RGX_DEV_DEVICE_ID)},
#if defined (SYS_RGX_DEV1_DEVICE_ID)
	{PCI_DEVICE(SYS_RGX_DEV_VENDOR_ID, SYS_RGX_DEV1_DEVICE_ID)},
#endif
	{0}
};
MODULE_DEVICE_TABLE(pci, powervr_id_table);
#endif	/*defined(LDM_PCI) */ 

#if defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
static struct platform_device_id powervr_id_table[] __devinitdata = {
	{SYS_RGX_DEV_NAME, 0},
	{}
};
#endif

static struct dev_pm_ops powervr_dev_pm_ops = {
	.suspend	= PVRSRVDriverSuspend,
	.resume		= PVRSRVDriverResume,
};

static LDM_DRV powervr_driver = {
#if defined(LDM_PLATFORM)
	.driver = {
		.name	= DRVNAME,
		.pm	= &powervr_dev_pm_ops,
		.of_match_table = of_match_ptr(rockchip_gpu_dt_ids),
	},
#endif
#if defined(LDM_PCI)
	.name		= DRVNAME,
	.driver.pm	= &powervr_dev_pm_ops,
#endif
#if defined(LDM_PCI) || defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
	.id_table	= powervr_id_table,
#endif
	.probe		= PVRSRVDriverProbe,
#if defined(LDM_PLATFORM)
	.remove		= PVRSRVDriverRemove,
#endif
#if defined(LDM_PCI)
	.remove		= __devexit_p(PVRSRVDriverRemove),
#endif
	.shutdown	= PVRSRVDriverShutdown,
};

LDM_DEV *gpsPVRLDMDev;
EXPORT_SYMBOL(gpsPVRLDMDev);

#if defined(LDM_PLATFORM)
#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
/*static void PVRSRVDeviceRelease(struct device unref__ *pDevice)
{
}

static struct platform_device powervr_device = {
	.name			= DEVNAME,
	.id				= -1,
	.dev 			= {
		.release	= PVRSRVDeviceRelease
	}
};*/
//time:2012-09-08
//move platform_device_register from devices.c to sgx
static struct resource resources_sgx[] = {
    [0] = {
        .name  = "gpu_irq",
        .start     = IRQ_GPU,
        .end    = IRQ_GPU,
        .flags  = IORESOURCE_IRQ,
    },
    [1] = {
        .name   = "gpu_base",
        .start  = RK30_GPU_PHYS ,
        .end    = RK30_GPU_PHYS  + RK30_GPU_SIZE - 1,
        .flags  = IORESOURCE_MEM,
    },
};
static struct platform_device powervr_device = {
    .name             = DEVNAME,
    .id               = 0,
    .num_resources    = ARRAY_SIZE(resources_sgx),
    .resource         = resources_sgx,
};
#else
static struct platform_device_info powervr_device_info =
{
	.name			= DEVNAME,
	.id			= -1,
	.dma_mask		= DMA_BIT_MASK(32),
};
#endif	/* (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)) */
#endif	/* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
#endif	/* defined(LDM_PLATFORM) */

static IMG_BOOL bCalledSysInit = IMG_FALSE;
static IMG_BOOL	bDriverProbeSucceeded = IMG_FALSE;

/*!
******************************************************************************

 @Function		PVRSRVSystemInit

 @Description

 Wrapper for PVRSRVInit.

 @input pDevice - the device for which a probe is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRM)
int PVRSRVSystemInit(struct drm_device *pDevice)
#else
static int PVRSRVSystemInit(LDM_DEV *pDevice)
#endif
{
	PVR_TRACE(("PVRSRVSystemInit (pDevice=%p)", pDevice));

//	ssleep(30);

	/* PVRSRVInit is only designed to be called once */
	if (bCalledSysInit == IMG_FALSE)
	{
#if defined(SUPPORT_DRM)

#if defined(LDM_PLATFORM)
		gpsPVRLDMDev = pDevice->platformdev;
#elif defined(LDM_PCI)
		gpsPVRLDMDev = pDevice->pdev;
#else
#error Only platform and pci devices are supported
#endif

#else /* SUPPORT_DRM */
		gpsPVRLDMDev = pDevice;
#endif

		bCalledSysInit = IMG_TRUE;

		if (PVRSRVInit() != PVRSRV_OK)
		{
			return -ENODEV;
		}
	}

	return 0;
}

/*!
******************************************************************************

 @Function		PVRSRVSystemDeInit

 @Description

 Wrapper for PVRSRVDeInit.

 @input none
 @Return nothing.

*****************************************************************************/
PVR_MOD_STATIC void PVRSRVSystemDeInit(void)
{
	PVR_TRACE(("PVRSRVSystemDeInit"));

	PVRSRVDeInit();

#if !defined(LDM_PLATFORM) || (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	gpsPVRLDMDev = IMG_NULL;
#endif
}

/*!
******************************************************************************

 @Function		PVRSRVDriverProbe

 @Description

 See whether a given device is really one we can drive.

 @input pDevice - the device for which a probe is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(LDM_PLATFORM)
static int PVRSRVDriverProbe(LDM_DEV *pDevice)
#endif
#if defined(LDM_PCI)
static int __devinit PVRSRVDriverProbe(LDM_DEV *pDevice, const struct pci_device_id *pID)
#endif
{
	int result = 0;

	PVR_TRACE(("PVRSRVDriverProbe (pDevice=%p)", pDevice));

#if defined(SUPPORT_DRM)
#if defined(LDM_PLATFORM)
	result = drm_platform_init(&sPVRDRMDriver, pDevice);
#endif
#if defined(LDM_PCI)
	result = drm_get_pci_dev(pDevice, pID, &sPVRDRMDriver);
#endif
#else	/* defined(SUPPORT_DRM) */
	result = PVRSRVSystemInit(pDevice);
#endif	/* defined(SUPPORT_DRM) */
	bDriverProbeSucceeded = (result == 0);
	return result;
}


/*!
******************************************************************************

 @Function		PVRSRVDriverRemove

 @Description

 This call is the opposite of the probe call; it is called when the device is
 being removed from the driver's control.

 @input pDevice - the device for which driver detachment is happening

 @Return 0, or no return value at all, depending on the device type.

*****************************************************************************/
#if defined (LDM_PLATFORM)
static int PVRSRVDriverRemove(LDM_DEV *pDevice)
#endif
#if defined(LDM_PCI)
static void __devexit PVRSRVDriverRemove(LDM_DEV *pDevice)
#endif
{
	PVR_TRACE(("PVRSRVDriverRemove (pDevice=%p)", pDevice));

#if defined(SUPPORT_DRM)
#if defined(LDM_PLATFORM)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))
	drm_platform_exit(&sPVRDRMDriver, pDevice);
#else
	drm_put_dev(platform_get_drvdata(pDevice));
#endif
#endif	/* defined(LDM_PLATFORM) */
#if defined(LDM_PCI)
	drm_put_dev(pci_get_drvdata(pDevice));
#endif
#else	/* defined(SUPPORT_DRM) */
	PVRSRVSystemDeInit();
#endif	/* defined(SUPPORT_DRM) */
#if defined(LDM_PLATFORM)
	return 0;
#endif
}

static struct mutex gsPMMutex;
static IMG_BOOL bDriverIsSuspended;
static IMG_BOOL bDriverIsShutdown;

/*!
******************************************************************************

 @Function		PVRSRVDriverShutdown

 @Description

 Suspend device operation for system shutdown.  This is called as part of the
 system halt/reboot process.  The driver is put into a quiescent state by 
 setting the power state to D3.

 @input pDevice - the device for which shutdown is requested

 @Return nothing

*****************************************************************************/
static void PVRSRVDriverShutdown(LDM_DEV *pDevice)
{
	PVR_TRACE(("PVRSRVDriverShutdown (pDevice=%p)", pDevice));

	mutex_lock(&gsPMMutex);

	if (!bDriverIsShutdown && !bDriverIsSuspended)
	{
		/*
		 * Take the bridge mutex, and never release it, to stop
		 * processes trying to use the driver after it has been
		 * shutdown.
		 */
		OSAcquireBridgeLock();

		(void) PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_OFF, IMG_TRUE);
	}

	bDriverIsShutdown = IMG_TRUE;

	/* The bridge mutex is held on exit */
	mutex_unlock(&gsPMMutex);
}

/*!
******************************************************************************

 @Function		PVRSRVDriverSuspend

 @Description

 Suspend device operation.

 @input pDevice - the device for which resume is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
static int PVRSRVDriverSuspend(struct device *pDevice)
{
	int res = 0;

	PVR_TRACE(( "PVRSRVDriverSuspend (pDevice=%p)", pDevice));

	mutex_lock(&gsPMMutex);

	if (!bDriverIsSuspended && !bDriverIsShutdown)
	{
		OSAcquireBridgeLock();

		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_OFF, IMG_TRUE) == PVRSRV_OK)
		{
			/* The bridge mutex will be held until we resume */
			bDriverIsSuspended = IMG_TRUE;
		}
		else
		{
			OSReleaseBridgeLock();
			res = -EINVAL;
		}
	}

	mutex_unlock(&gsPMMutex);

	return res;
}


/*!
******************************************************************************

 @Function		PVRSRVDriverResume

 @Description

 Resume device operation.

 @input pDevice - the device for which resume is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
static int PVRSRVDriverResume(struct device *pDevice)
{
	int res = 0;

	PVR_TRACE(("PVRSRVDriverResume (pDevice=%p)", pDevice));

	mutex_lock(&gsPMMutex);

	if (bDriverIsSuspended && !bDriverIsShutdown)
	{
		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_ON, IMG_TRUE) == PVRSRV_OK)
		{
			bDriverIsSuspended = IMG_FALSE;
			OSReleaseBridgeLock();
		}
		else
		{
			/* The bridge mutex is not released on failure */
			res = -EINVAL;
		}
	}

	mutex_unlock(&gsPMMutex);

	return res;
}

/*!
******************************************************************************

 @Function		PVRSRVOpen

 @Description

 Open the PVR services node.

 @input pInode - the inode for the file being openeded.
 @input dev    - the DRM device corresponding to this driver.

 @input pFile - the file handle data for the actual file being opened

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRM)
int PVRSRVOpen(struct drm_device unref__ *dev, struct drm_file *pFile)
#else
static int PVRSRVOpen(struct inode unref__ * pInode, struct file *pFile)
#endif
{
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData;
	int iRet = -ENOMEM;
	PVRSRV_ERROR eError;

	if (!try_module_get(THIS_MODULE))
	{
		PVR_DPF((PVR_DBG_ERROR, "Failed to get module"));
		return iRet;
	}

	OSAcquireBridgeLock();

	psPrivateData = OSAllocMem(sizeof(PVRSRV_FILE_PRIVATE_DATA));

	if(psPrivateData == IMG_NULL)
		goto err_unlock;

	/*
		Here we pass the file pointer which will passed through to our
		OSConnectionPrivateDataInit function where we can save it so
		we can back reference the file structure from it's connection
	*/
	eError = PVRSRVConnectionConnect(&psPrivateData->pvConnectionData, (IMG_PVOID) pFile);
	if (eError != PVRSRV_OK)
	{
		OSFreeMem(psPrivateData);
		goto err_unlock;
	}

#if defined(PVR_SECURE_FD_EXPORT)
	psPrivateData->hKernelMemInfo = NULL;
#endif
#if defined(SUPPORT_DRM_AUTH_IMPORT)
	psPrivateData->uPID = OSGetCurrentProcessIDKM();
	list_add_tail(&psPrivateData->sDRMAuthListItem, &sDRMAuthListHead);
#endif
	PRIVATE_DATA(pFile) = psPrivateData;
	OSReleaseBridgeLock();
	return 0;

err_unlock:	
	OSReleaseBridgeLock();
	module_put(THIS_MODULE);
	return iRet;
}


/*!
******************************************************************************

 @Function		PVRSRVRelease

 @Description

 Release access the PVR services node - called when a file is closed, whether
 at exit or using close(2) system call.

 @input pInode - the inode for the file being released
 @input pvPrivData - driver private data

 @input pFile - the file handle data for the actual file being released

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRM)
void PVRSRVRelease(void *pvPrivData)
#else
static int PVRSRVRelease(struct inode unref__ * pInode, struct file *pFile)
#endif
{
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData;

	OSAcquireBridgeLock();

#if defined(SUPPORT_DRM)
	psPrivateData = (PVRSRV_FILE_PRIVATE_DATA *)pvPrivData;
#else
	psPrivateData = PRIVATE_DATA(pFile);
#endif
	if (psPrivateData != IMG_NULL)
	{
#if defined(SUPPORT_DRM_AUTH_IMPORT)
		list_del(&psPrivateData->sDRMAuthListItem);
#endif
		PVRSRVConnectionDisconnect(psPrivateData->pvConnectionData);

		OSFreeMem(psPrivateData);

#if !defined(SUPPORT_DRM)
		PRIVATE_DATA(pFile) = IMG_NULL;
#endif
	}

	OSReleaseBridgeLock();
	module_put(THIS_MODULE);
#if defined(SUPPORT_DRM)
	return;
#else
	return 0;
#endif
}

#if defined(SUPPORT_DRM)
CONNECTION_DATA *LinuxConnectionFromFile(struct drm_file *pFile)
#else
CONNECTION_DATA *LinuxConnectionFromFile(struct file *pFile)
#endif
{
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData = PRIVATE_DATA(pFile);

	return (psPrivateData == IMG_NULL) ? IMG_NULL : psPrivateData->pvConnectionData;
}

struct file *LinuxFileFromEnvConnection(ENV_CONNECTION_DATA *psEnvConnection)
{
	PVR_ASSERT(psEnvConnection != NULL);
	
#if defined(SUPPORT_DRM)
	return psEnvConnection->psFile->filp;
#else
	return psEnvConnection->psFile;
#endif
}

#if defined(SUPPORT_DRM_AUTH_IMPORT)
static IMG_BOOL PVRDRMCheckAuthentication(struct drm_file *pFile, IMG_PID uPID)
{
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData;

	BUG_ON(!mutex_is_locked(&gPVRSRVLock));

	list_for_each_entry(psPrivateData, &sDRMAuthListHead, sDRMAuthListItem)
	{
		if (uPID == psPrivateData->uPID)
		{
			ENV_CONNECTION_DATA *psEnvConnection = PVRSRVConnectionPrivateData(psPrivateData->pvConnectionData);

			if (psEnvConnection != IMG_NULL && pFile->master == psEnvConnection->psFile->master)
			{
				if (psEnvConnection->psFile->authenticated)
				{
					return IMG_TRUE;
				}
			}
		}
	}

	return IMG_FALSE;
}

PVRSRV_ERROR OSCheckAuthentication(CONNECTION_DATA *psConnection, IMG_UINT32 ui32Level)
{
	ENV_CONNECTION_DATA *psEnvConnection;
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData;
	IMG_BOOL bAuthenticated = IMG_FALSE;

	if (ui32Level == 0)
	{
		return PVRSRV_OK;
	}

	psEnvConnection = PVRSRVConnectionPrivateData(psConnection);
	if (psEnvConnection == IMG_NULL)
	{
		return PVRSRV_ERROR_RESOURCE_UNAVAILABLE;
	}

	bAuthenticated |= psEnvConnection->bAuthenticated;
	bAuthenticated |= psEnvConnection->psFile->authenticated;
	if (bAuthenticated)
	{
		goto check_auth_exit;
	}

	psPrivateData = PRIVATE_DATA(psEnvConnection->psFile);

	/*
	 * If our connection was not authenticated, see if we have another
	 * one that is.
	 */
	bAuthenticated = PVRDRMCheckAuthentication(psEnvConnection->psFile, psPrivateData->uPID);

check_auth_exit:
	if (!bAuthenticated)
	{
		PVR_DPF((PVR_DBG_WARNING, "%s: PVR Services Connection not authenticated", __FUNCTION__));
		return PVRSRV_ERROR_NOT_AUTHENTICATED;
	}

	psEnvConnection->bAuthenticated = bAuthenticated;

	return PVRSRV_OK;
}
#endif /* defined(SUPPORT_DRM_AUTH_IMPORT) */

/*!
******************************************************************************

 @Function		PVRCore_Init

 @Description

 Insert the driver into the kernel.

 Readable and/or writable debugfs entries under /sys/kernel/debug/pvr are
 created with PVRDebugFSCreateEntry().  These can be read at runtime to get
 information about the device (eg. 'cat /sys/kernel/debug/pvr/nodes')

 __init places the function in a special memory section that the kernel frees
 once the function has been run.  Refer also to module_init() macro call below.

 @input none

 @Return none

*****************************************************************************/


PVRSRV_ERROR LinuxBridgeInit(void);
void LinuxBridgeDeInit(void);

static int __init PVRCore_Init(void)
{
	int error;
#if defined(PVRSRV_ENABLE_PROCESS_STATS) || defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
	PVRSRV_ERROR eError;
#endif
#if !defined(SUPPORT_DRM)
	struct device *psDev;
#endif

	/*
	 * Must come before attempting to print anything via Services.
	 * For DRM, the initialisation will already have been done.
	 */
	PVRDPFInit();

	PVR_TRACE(("PVRCore_Init"));

	//zxl:print gpu version on boot time
	printk("PVR_K: sys.gpvr.version=%s\n",RKVERSION);

#if defined(SUPPORT_DRM)
#if defined(PDUMP)
	error = dbgdrv_init();
	if (error != 0)
	{
		return error;
	}
#endif
#endif

	mutex_init(&gsPMMutex);

	mutex_init(&gPVRSRVLock);

	error = PVRDebugFSInit();
	if (error != 0)
	{
		goto dbgdrv_cleanup;
	}

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	eError = PVRSRVStatsInitialise();
	if (eError != PVRSRV_OK)
	{
		error = -ENOMEM;

		goto debugfs_deinit;
	}
#endif

	if (PVROSFuncInit() != PVRSRV_OK)
	{
		error = -ENOMEM;
		goto init_failed;
	}

	LinuxBridgeInit();

	PVRMMapInit();

#if defined(LDM_PLATFORM)
	if ((error = platform_driver_register(&powervr_driver)) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register platform driver (%d)", error));

		goto init_failed;
	}

#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	error = platform_device_register(&powervr_device);
#else
	gpsPVRLDMDev = platform_device_register_full(&powervr_device_info);
	error = IS_ERR(gpsPVRLDMDev) ? PTR_ERR(gpsPVRLDMDev) : 0;
#endif
	if (error != 0)
	{
		gpsPVRLDMDev = NULL;
		platform_driver_unregister(&powervr_driver);

		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register platform device (%d)", error));

		goto init_failed;
	}
#endif	/* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
#endif	/* defined(LDM_PLATFORM) */ 

#if defined(LDM_PCI)
#if defined(SUPPORT_DRM)
	error = drm_pci_init(&sPVRDRMDriver, &powervr_driver);
#else
	error = pci_register_driver(&powervr_driver);
#endif
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register PCI driver (%d)", error));

		goto init_failed;
	}
#endif /* LDM_PCI */

	/* Check that the driver probe function was called */
	if (!bDriverProbeSucceeded)
	{
		PVR_TRACE(("PVRCore_Init: PVRSRVDriverProbe has not been called or did not succeed - check that hardware is detected"));
		goto init_failed;
	}

#if !defined(SUPPORT_DRM)
	AssignedMajorNumber = register_chrdev(0, DEVNAME, &pvrsrv_fops);

	if (AssignedMajorNumber <= 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to get major number"));

		error = -EBUSY;
		goto sys_deinit;
	}

	PVR_TRACE(("PVRCore_Init: major device %d", AssignedMajorNumber));

	/*
	 * This code facilitates automatic device node creation on platforms
	 * with udev (or similar).
	 */
	psPvrClass = class_create(THIS_MODULE, "pvr");

	if (IS_ERR(psPvrClass))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create class (%ld)", PTR_ERR(psPvrClass)));
		error = -EBUSY;
		goto unregister_device;
	}

	psDev = device_create(psPvrClass, NULL, MKDEV(AssignedMajorNumber, 0),
				  NULL, DEVNAME);
	if (IS_ERR(psDev))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create device (%ld)", PTR_ERR(psDev)));
		error = -EBUSY;
		goto destroy_class;
	}
#endif /* !defined(SUPPORT_DRM) */

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
	eError = pvr_sync_init();
	if (eError != PVRSRV_OK)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create sync (%d)", eError));
		error = -EBUSY;
		goto destroy_class;

	}
#endif

	error = PVRDebugCreateDebugFSEntries();
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRCore_Init: failed to create default debugfs entries (%d)", error));
	}

#if defined(SUPPORT_GPUTRACE_EVENTS)
	error = PVRGpuTraceInit();
	if (error != 0)
	{
		PVR_DPF((PVR_DBG_WARNING, "PVRCore_Init: failed to initialise PVR GPU Tracing (%d)", error));
	}
#endif

	return 0;

#if !defined(SUPPORT_DRM)
destroy_class:
	class_destroy(psPvrClass);
unregister_device:
	unregister_chrdev((IMG_UINT)AssignedMajorNumber, DEVNAME);
sys_deinit:
#if defined(LDM_PCI)
#if defined(SUPPORT_DRM)
	drm_pci_exit(&sPVRDRMDriver, &powervr_driver);
#else
	pci_unregister_driver(&powervr_driver);
#endif
#endif

#if defined (LDM_PLATFORM)
#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	platform_device_unregister(&powervr_device);
#else
	PVR_ASSERT(gpsPVRLDMDev != NULL);
	platform_device_unregister(gpsPVRLDMDev);
#endif	/* (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)) */
#endif	/* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
	platform_driver_unregister(&powervr_driver);
#endif	/* defined (LDM_PLATFORM) */
#endif	/* !defined(SUPPORT_DRM) */

init_failed:
	PVRMMapCleanup();
	LinuxBridgeDeInit();
	PVROSFuncDeInit();
#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsDestroy();
debugfs_deinit:
#endif
	PVRDebugFSDeInit();
dbgdrv_cleanup:
#if defined(SUPPORT_DRM)
#if defined(PDUMP)
	dbgdrv_cleanup();
#endif
#endif
	return error;

} /*PVRCore_Init*/


/*!
*****************************************************************************

 @Function		PVRCore_Cleanup

 @Description	

 Remove the driver from the kernel.

 There's no way we can get out of being unloaded other than panicking; we
 just do everything and plough on regardless of error.

 __exit places the function in a special memory section that the kernel frees
 once the function has been run.  Refer also to module_exit() macro call below.

 @input none

 @Return none

*****************************************************************************/
static void __exit PVRCore_Cleanup(void)
{
	PVR_TRACE(("PVRCore_Cleanup"));

#if defined(SUPPORT_GPUTRACE_EVENTS)
	PVRGpuTraceDeInit();
#endif

	PVRDebugRemoveDebugFSEntries();

#if defined(SUPPORT_DRM_AUTH_IMPORT)
	BUG_ON(!list_empty(&sDRMAuthListHead));
#endif

#if defined(PVR_ANDROID_NATIVE_WINDOW_HAS_SYNC)
	pvr_sync_deinit();
#endif

#if !defined(SUPPORT_DRM)
	device_destroy(psPvrClass, MKDEV(AssignedMajorNumber, 0));
	class_destroy(psPvrClass);

	unregister_chrdev((IMG_UINT)AssignedMajorNumber, DEVNAME);
#endif

#if defined(LDM_PCI)
#if defined(SUPPORT_DRM)
	drm_pci_exit(&sPVRDRMDriver, &powervr_driver);
#else
	pci_unregister_driver(&powervr_driver);
#endif
#endif	/* defined(LDM_PCI) */

#if defined (LDM_PLATFORM)
#if defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))
	platform_device_unregister(&powervr_device);
#else
	PVR_ASSERT(gpsPVRLDMDev != NULL);
	platform_device_unregister(gpsPVRLDMDev);
#endif	/* (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)) */
#endif	/* defined(MODULE) && !defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV) */
	platform_driver_unregister(&powervr_driver);
#endif	/* defined (LDM_PLATFORM) */

	PVRMMapCleanup();

	LinuxBridgeDeInit();

	PVROSFuncDeInit();

#if defined(PVRSRV_ENABLE_PROCESS_STATS)
	PVRSRVStatsDestroy();
#endif
	PVRDebugFSDeInit();

#if defined(SUPPORT_DRM)
#if defined(PDUMP)
	dbgdrv_cleanup();
#endif
#endif
	PVR_TRACE(("PVRCore_Cleanup: unloading"));
}

/*
 * These macro calls define the initialisation and removal functions of the
 * driver.  Although they are prefixed `module_', they apply when compiling
 * statically as well; in both cases they define the function the kernel will
 * run to start/stop the driver.
*/
module_init(PVRCore_Init);
module_exit(PVRCore_Cleanup);
