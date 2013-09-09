/*************************************************************************/ /*!
@Title          Linux module setup
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2
* Copyright 2013 by S.LSI. Samsung Electronics Inc.

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
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))
#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif
#endif

#if defined(SUPPORT_DRI_DRM) && !defined(SUPPORT_DRI_DRM_PLUGIN)
#define	PVR_MOD_STATIC
#else
	/*
	 * For LDM drivers, define PVR_LDM_MODULE to indicate generic LDM
	 * support is required, besides indicating the exact support
	 * required (e.g. platform, or PCI device).
	 */
	#if defined(LDM_PLATFORM)
		#define	PVR_LDM_PLATFORM_MODULE
		#define PVR_LDM_DEVICE_CLASS
		#define	PVR_LDM_MODULE
	#else
		#if defined(LDM_PCI)
			#define PVR_LDM_DEVICE_CLASS
			#define PVR_LDM_PCI_MODULE
			#define	PVR_LDM_MODULE
		#else
			#if defined(SYS_SHARES_WITH_3PKM)
				#define PVR_LDM_DEVICE_CLASS
			#endif
		#endif
	#endif
#define	PVR_MOD_STATIC	static
#endif

#if defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
#if !defined(NO_HARDWARE)
#define PVR_USE_PRE_REGISTERED_PLATFORM_DEV
#endif
#endif

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/pm_domain.h>
#include <mach/regs-clock.h>

#if defined(SUPPORT_DRI_DRM)
#include <drm/drmP.h>
#if defined(PVR_SECURE_DRM_AUTH_EXPORT)
#include "env_perproc.h"
#endif
#endif

#if defined(PVR_LDM_PLATFORM_MODULE)
#include <linux/platform_device.h>
#endif /* PVR_LDM_PLATFORM_MODULE */

#if defined(PVR_LDM_PCI_MODULE)
#include <linux/pci.h>
#endif /* PVR_LDM_PCI_MODULE */

#if defined(PVR_LDM_DEVICE_CLASS)
#include <linux/device.h>
#endif /* PVR_LDM_DEVICE_CLASS */

#if defined(CONFIG_EXYNOS_THERMAL)
#include <mach/tmu.h>
#endif

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
#include <mach/devfreq.h>
#endif

#if defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL)
#include <asm/uaccess.h>
#endif

#if defined(PVR_LDM_MODULE) || defined(PVR_DRI_DRM_PLATFORM_DEV)
#include <asm/atomic.h>
#endif

#include "img_defs.h"
#include "services.h"
#include "kerneldisplay.h"
#include "kernelbuffer.h"
#include "syscommon.h"
#include "pvrmmap.h"
#include "mutils.h"
#include "mm.h"
#include "mmap.h"
#include "mutex.h"
#include "pvr_debug.h"
#include "srvkm.h"
#include "perproc.h"
#include "handle.h"
#include "pvr_bridge_km.h"
#include "proc.h"
#include "pvrmodule.h"
#include "private_data.h"
#include "lock.h"
#include "linkage.h"
#include "buffer_manager.h"
#include "secutils.h"
#if defined(SUPPORT_ANDROID_SYNC)
#include "pvr_sync.h"
#endif

#if defined(SUPPORT_DRI_DRM)
#include "pvr_drm.h"
#endif
/*
 * DRVNAME is the name we use to register our driver.
 * DEVNAME is the name we use to register actual device nodes.
 */
#if defined(PVR_LDM_MODULE)
/*S.LSI*/
#define	DRVNAME		PVRSRV_MODNAME
#endif
#define DEVNAME		PVRSRV_MODNAME

#define VOLTAGE_OFFSET_MARGIN 75000

#if defined(SUPPORT_DRI_DRM)
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

/*S.LSI*/
extern IMG_UINT32 gPVREnableVSync;
module_param(gPVREnableVSync, uint, 0644);
MODULE_PARM_DESC(gPVREnableVSync, "Sets the vsync waiting (default 1)");

extern IMG_UINT32 gPVRPanDisplaySignal;
module_param(gPVRPanDisplaySignal, uint, 0644);
MODULE_PARM_DESC(gPVRPanDisplaySignal, "Send Pan display signal to userspace through uevent (default 1)");

/*S.LSI*/
#endif /* defined(PVRSRV_NEED_PVR_DPF) */
#ifdef CONFIG_PM_RUNTIME
IMG_UINT32 gPVRSuspendNotifier;
#endif

#if defined(CONFIG_ION_OMAP)
#include <linux/ion.h>
#include <linux/omap_ion.h>
extern struct ion_device *omap_ion_device;
struct ion_client *gpsIONClient;
EXPORT_SYMBOL(gpsIONClient);
#endif /* defined(CONFIG_ION_OMAP) */

/* PRQA S 3207 2 */ /* ignore 'not used' warning */
EXPORT_SYMBOL(PVRGetDisplayClassJTable);
EXPORT_SYMBOL(PVRGetBufferClassJTable);

#if defined(PVR_LDM_DEVICE_CLASS) && !defined(SUPPORT_DRI_DRM)
/*
 * Device class used for /sys entries (and udev device node creation)
 */
static struct class *psPvrClass;
#endif

#if !defined(SUPPORT_DRI_DRM)
/*
 * This is the major number we use for all nodes in /dev.
 */
static int AssignedMajorNumber;

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
	.owner=THIS_MODULE,
	.unlocked_ioctl = PVRSRV_BridgeDispatchKM,
	.open=PVRSRVOpen,
	.release=PVRSRVRelease,
	.mmap=PVRMMap,
};
#endif

PVRSRV_LINUX_MUTEX gPVRSRVLock;

/* PID of process being released */
IMG_UINT32 gui32ReleasePID;

#if defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL)
static IMG_UINT32 gPVRPowerLevel;
#endif

static int PVRSRVIONClientCreate(void)
{
#if defined(CONFIG_ION_OMAP)
	gpsIONClient = ion_client_create(omap_ion_device,
									 1 << OMAP_ION_HEAP_TYPE_TILER,
									 "pvr");
	if (IS_ERR_OR_NULL(gpsIONClient))
	{
		int err;

		PVR_DPF((PVR_DBG_ERROR, "PVRSRVDriverProbe: Couldn't create ion client"));

		err = PTR_ERR(gpsIONClient);
		gpsIONClient = IMG_NULL;

		return err;
	}
#endif /* defined(CONFIG_ION_OMAP) */

	return 0;
}

static void PVRSRVIONClientDestroy(void)
{
#if defined(CONFIG_ION_OMAP)
	if (gpsIONClient != NULL)
	{
		ion_client_destroy(gpsIONClient);
		gpsIONClient = IMG_NULL;
	}
#endif
}

#if defined(PVR_LDM_MODULE)

#if defined(PVR_LDM_PLATFORM_MODULE)
#define	LDM_DEV	struct platform_device
#define	LDM_DRV	struct platform_driver
#endif /*PVR_LDM_PLATFORM_MODULE */

#if defined(PVR_LDM_PCI_MODULE)
#define	LDM_DEV	struct pci_dev
#define	LDM_DRV	struct pci_driver
#endif /* PVR_LDM_PCI_MODULE */
/*
 * This is the driver interface we support.  
 */
#if defined(PVR_LDM_PLATFORM_MODULE)
static int PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device);
#endif
#if defined(PVR_LDM_PCI_MODULE)
static void PVRSRVDriverRemove(LDM_DEV *device);
static int PVRSRVDriverProbe(LDM_DEV *device, const struct pci_device_id *id);
#endif
static int PVRSRVDriverSuspend(struct device *device);
static void PVRSRVDriverShutdown(LDM_DEV *device);
static int PVRSRVDriverResume(struct device *device);

#if defined(PVR_LDM_PCI_MODULE)
/* This structure is used by the Linux module code */
struct pci_device_id powervr_id_table[] __devinitdata = {
	{PCI_DEVICE(SYS_SGX_DEV_VENDOR_ID, SYS_SGX_DEV_DEVICE_ID)},
#if defined (SYS_SGX_DEV1_DEVICE_ID)
	{PCI_DEVICE(SYS_SGX_DEV_VENDOR_ID, SYS_SGX_DEV1_DEVICE_ID)},
#endif
	{0}
};

MODULE_DEVICE_TABLE(pci, powervr_id_table);
#endif

#if defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
static struct platform_device_id powervr_id_table[] __devinitdata = {
	{SYS_SGX_DEV_NAME, 0},
	{}
};
#endif

static const struct dev_pm_ops g3d_ops = {
	.suspend = PVRSRVDriverSuspend,
	.resume = PVRSRVDriverResume,
};

static LDM_DRV powervr_driver = {
#if defined(PVR_LDM_PLATFORM_MODULE)
	.driver = {
		.name		= DRVNAME,
		.pm		= &g3d_ops,
	},
#endif
#if defined(PVR_LDM_PCI_MODULE)
	.name		= DRVNAME,
#endif
#if defined(PVR_LDM_PCI_MODULE) || defined(PVR_USE_PRE_REGISTERED_PLATFORM_DEV)
	.id_table = powervr_id_table,
#endif
	.probe		= PVRSRVDriverProbe,
#if defined(PVR_LDM_PLATFORM_MODULE)
	.remove		= PVRSRVDriverRemove,
#endif
#if defined(PVR_LDM_PCI_MODULE)
	.remove		= __devexit_p(PVRSRVDriverRemove),
#endif
	.shutdown	= PVRSRVDriverShutdown,
};

LDM_DEV *gpsPVRLDMDev;

/*S.LSI*/
static void PVRSRVDeviceRelease(struct device unref__ *pDevice)
{
}

struct platform_device powervr_device = {
	.name			= DEVNAME,
	.id				= -1,
	.dev 			= {
		.release	= PVRSRVDeviceRelease
	}
};

#ifdef CONFIG_PM_RUNTIME
static int pvr_suspend_notifier(struct notifier_block *nb, unsigned long event, void *dummy);
static int pvr_suspend_notifier(struct notifier_block *nb, unsigned long event, void *dummy)
{
	switch (event) {
	case PM_SUSPEND_PREPARE:
		gPVRSuspendNotifier = true;
#if defined(CONFIG_PVR_SGX_DVFS)
		sec_gpu_utilization_pause();
#endif
		PVR_LOG(("%s: %d", __func__, gPVRSuspendNotifier));
		break;
	case PM_POST_SUSPEND:
		gPVRSuspendNotifier = false;
#if defined(CONFIG_PVR_SGX_DVFS)
		sec_gpu_utilization_resume();
#endif
		PVR_LOG(("%s: %d", __func__, gPVRSuspendNotifier));
		break;
	default:
		break;
	}
	return 0;
}
static struct notifier_block pvr_notif_block = {
	.notifier_call = pvr_suspend_notifier
};
#endif

#if defined(CONFIG_EXYNOS_THERMAL)
extern int exynos_tmu_add_notifier(struct notifier_block *n);
static int exynos_g3d_tmu_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	int volt_offset = 0;
	int *on = v;

	if (event != TMU_COLD)
		return NOTIFY_OK;

	if (*on)
		volt_offset = VOLTAGE_OFFSET_MARGIN;

	SysSystemSetPowerMargin(volt_offset);
	PVR_LOG(("exynos_g3d_tmu_notifier: volt_offset:%d", volt_offset));
	return NOTIFY_OK;
}

static struct notifier_block exynos_tmu_nb = {
	.notifier_call = exynos_g3d_tmu_notifier,
};
#endif

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
extern IMG_BOOL bEnableMIFMornitering;
extern int exynos5_mif_register_notifier(struct notifier_block *n);
static int exynos_g3d_mif_notifier(struct notifier_block *notifier, unsigned long event, void *v)
{
	if (event == MIF_DEVFREQ_EN_MONITORING)	{
		PVR_LOG(("exynos_g3d_mif_notifier: MIF_DEVFREQ_EN_MONITORING"));
		bEnableMIFMornitering = true;
	} else if (event == MIF_DEVFREQ_DIS_MONITORING) {
		PVR_LOG(("exynos_g3d_mif_notifier: MIF_DEVFREQ_DIS_MONITORING"));
		bEnableMIFMornitering = false;
	}
	return NOTIFY_OK;
}

static struct notifier_block exynos_mif_nb = {
	.notifier_call = exynos_g3d_mif_notifier,
};
#endif
/*!
******************************************************************************

 @Function		PVRSRVDriverProbe

 @Description

 See whether a given device is really one we can drive.  The platform bus
 handler has already established that we should be able to service this device
 because of the name match.  We probably don't need to do anything else.

 @input pDevice - the device for which a probe is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(PVR_LDM_PLATFORM_MODULE)
static int PVRSRVDriverProbe(LDM_DEV *pDevice)
#endif
#if defined(PVR_LDM_PCI_MODULE)
static int __devinit PVRSRVDriverProbe(LDM_DEV *pDevice, const struct pci_device_id *id)
#endif
{
	SYS_DATA *psSysData;

	PVR_LOG(("PVRSRVDriverProbe(pDevice=%p)", pDevice));

#if 0   /* INTEGRATION_POINT */
	/* Some systems require device-specific system initialisation.
	 * E.g. this lets the OS track a device's dependencies on various
	 * system hardware.
	 *
	 * Note: some systems use this to enable HW that SysAcquireData
	 * will depend on, therefore it must be called first.
	 */
	if (PerDeviceSysInitialise((IMG_PVOID)pDevice) != PVRSRV_OK)
	{
		return -EINVAL;
	}
#endif	
	/* SysInitialise only designed to be called once.
	 */
	psSysData = SysAcquireDataNoCheck();
	if (psSysData == IMG_NULL)
	{
		gpsPVRLDMDev = pDevice;
		if (SysInitialise() != PVRSRV_OK)
		{
			return -ENODEV;
		}
	}
	gPVRSuspendNotifier = false;
	return PVRSRVIONClientCreate();
}


/*!
******************************************************************************

 @Function		PVRSRVDriverRemove

 @Description

 This call is the opposite of the probe call: it is called when the device is
 being removed from the driver's control.  See the file $KERNELDIR/drivers/
 base/bus.c:device_release_driver() for the call to this function.

 This is the correct place to clean up anything our driver did while it was
 asoociated with the device.

 @input pDevice - the device for which driver detachment is happening

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined (PVR_LDM_PLATFORM_MODULE)
static int PVRSRVDriverRemove(LDM_DEV *pDevice)
#endif
#if defined(PVR_LDM_PCI_MODULE)
static void __devexit PVRSRVDriverRemove(LDM_DEV *pDevice)
#endif
{
	SYS_DATA *psSysData;

	PVR_TRACE(("PVRSRVDriverRemove(pDevice=%p)", pDevice));

	PVRSRVIONClientDestroy();

	SysAcquireData(&psSysData);
	
#if defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL)
	if (gPVRPowerLevel != 0)
	{
		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D0) == PVRSRV_OK)
		{
			gPVRPowerLevel = 0;
		}
	}
#endif
	(void) SysDeinitialise(psSysData);

	gpsPVRLDMDev = IMG_NULL;

#if 0   /* INTEGRATION_POINT */
	/* See previous integration point for details. */
	if (PerDeviceSysDeInitialise((IMG_PVOID)pDevice) != PVRSRV_OK)
	{
		return -EINVAL;
	}
#endif

#if defined (PVR_LDM_PLATFORM_MODULE)
	return 0;
#endif
#if defined (PVR_LDM_PCI_MODULE)
	return;
#endif
}
#endif /* defined(PVR_LDM_MODULE) */


#if defined(PVR_LDM_MODULE) || defined(SUPPORT_DRI_DRM)
static PVRSRV_LINUX_MUTEX gsPMMutex;
static IMG_BOOL bDriverIsSuspended;
static IMG_BOOL bDriverIsShutdown;
#endif

#if defined(PVR_LDM_MODULE) || defined(PVR_DRI_DRM_PLATFORM_DEV)
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
#if defined(SUPPORT_DRI_DRM) && !defined(PVR_DRI_DRM_PLATFORM_DEV) && \
	!defined(SUPPORT_DRI_DRM_PLUGIN)
void PVRSRVDriverShutdown(struct drm_device *pDevice)
#else
PVR_MOD_STATIC void PVRSRVDriverShutdown(LDM_DEV *pDevice)
#endif
{
	PVR_TRACE(("PVRSRVDriverShutdown(pDevice=%p)", pDevice));

	LinuxLockMutex(&gsPMMutex);

	if (!bDriverIsShutdown && !bDriverIsSuspended)
	{
		/*
		 * Take the bridge mutex, and never release it, to stop
		 * processes trying to use the driver after it has been
		 * shutdown.
		 */
		LinuxLockMutex(&gPVRSRVLock);

		(void) PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D1);
	}

	bDriverIsShutdown = IMG_TRUE;

	/* The bridge mutex is held on exit */
	LinuxUnLockMutex(&gsPMMutex);
}

#endif /* defined(PVR_LDM_MODULE) || defined(PVR_DRI_DRM_PLATFORM_DEV) */


#if defined(PVR_LDM_MODULE) || defined(SUPPORT_DRI_DRM)
/*!
******************************************************************************

 @Function		PVRSRVDriverSuspend

 @Description

 For 2.6 kernels:
 Suspend device operation.  We always get three calls to this regardless of
 the state (D1-D3) chosen.  The order is SUSPEND_DISABLE, SUSPEND_SAVE_STATE
 then SUSPEND_POWER_DOWN.  We take action as soon as we get the disable call,
 the other states not being handled by us yet.

 For MontaVista 2.4 kernels:
 This call gets made once only when someone does something like

	# echo -e -n "suspend powerdown 0" >/sys.devices/legacy/pvrsrv0/power

 The 3rd, numeric parameter (0) in the above has no relevence and is not
 passed into us.  The state parameter is always zero and the level parameter
 is always SUSPEND_POWER_DOWN.  Vive la difference!

 @input pDevice - the device for which resume is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM) && !defined(PVR_DRI_DRM_PLATFORM_DEV) && \
	!defined(SUPPORT_DRI_DRM_PLUGIN)
#if defined(SUPPORT_DRM_MODESET)
int PVRSRVDriverSuspend(struct pci_dev *pDevice, pm_message_t state)
#else
int PVRSRVDriverSuspend(struct drm_device *pDevice, pm_message_t state)
#endif
#else
PVR_MOD_STATIC int PVRSRVDriverSuspend(struct device *pDevice)
#endif
{
	int res = 0;
#if !(defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL) && !defined(SUPPORT_DRI_DRM))
	PVR_TRACE(( "PVRSRVDriverSuspend(pDevice=%p)", pDevice));

	LinuxLockMutex(&gsPMMutex);

	if (!bDriverIsSuspended && !bDriverIsShutdown)
	{
		LinuxLockMutex(&gPVRSRVLock);

		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D3) == PVRSRV_OK)
		{
			/* The bridge mutex will be held until we resume */
			bDriverIsSuspended = IMG_TRUE;
		}
		else
		{
			LinuxUnLockMutex(&gPVRSRVLock);
			res = -EINVAL;
		}
	}

	LinuxUnLockMutex(&gsPMMutex);
#endif
	return res;
}


/*!
******************************************************************************

 @Function		PVRSRVDriverResume

 @Description

 Resume device operation following a lull due to earlier suspension.  It is
 implicit we're returning to D0 (fully operational) state.  We always get three
 calls to this using level thus: RESUME_POWER_ON, RESUME_RESTORE_STATE then
 RESUME_ENABLE.  On 2.6 kernels We don't do anything until we get the enable
 call; on the MontaVista set-up we only ever get the RESUME_POWER_ON call.

 @input pDevice - the device for which resume is requested

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM) && !defined(PVR_DRI_DRM_PLATFORM_DEV) && \
	!defined(SUPPORT_DRI_DRM_PLUGIN)
#if defined(SUPPORT_DRM_MODESET)
int PVRSRVDriverResume(struct pci_dev *pDevice)
#else
int PVRSRVDriverResume(struct drm_device *pDevice)
#endif
#else
PVR_MOD_STATIC int PVRSRVDriverResume(struct device *pDevice)
#endif
{
	int res = 0;
#if !(defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL) && !defined(SUPPORT_DRI_DRM))
	PVR_TRACE(("PVRSRVDriverResume(pDevice=%p)", pDevice));

	LinuxLockMutex(&gsPMMutex);

	if (bDriverIsSuspended && !bDriverIsShutdown)
	{
		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D0) == PVRSRV_OK)
		{
			bDriverIsSuspended = IMG_FALSE;
			LinuxUnLockMutex(&gPVRSRVLock);
		}
		else
		{
			/* The bridge mutex is not released on failure */
			res = -EINVAL;
		}
	}

	LinuxUnLockMutex(&gsPMMutex);
#endif
	return res;
}
#endif /* defined(PVR_LDM_MODULE) || defined(SUPPORT_DRI_DRM) */


#if defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL) && !defined(SUPPORT_DRI_DRM)
/*
 * If PVR_LDM_PCI_MODULE is defined (and PVR_MANUAL_POWER_CONTROL is *NOT* defined),
 * the device can be suspended and resumed without suspending/resuming the
 * system, by writing values into the power/state sysfs file for the device.
 * To suspend:
 *	echo -n 2 > power/state
 * To Resume:
 *	echo -n 0 > power/state
 *
 * The problem with this approach is that the device is usually left
 * powered up; it is the responsibility of the bus driver to remove
 * the power.
 *
 * Defining PVR_MANUAL_POWER_CONTROL is intended to make it easier to
 * debug power management issues, especially when power is really removed
 * from the device.  It is easier to debug the driver if it is not being
 * suspended/resumed with the rest of the system.
 *
 * When PVR_MANUAL_POWER_CONTROL is defined, the following proc entry is
 * created:
 * 	/proc/pvr/power_control
 * The driver suspend/resume entry points defined below no longer suspend or
 * resume the device.  To suspend the device, type the following:
 * 	echo 2 > /proc/pvr/power_control
 * To resume the device, type:
 * 	echo 0 > /proc/pvr/power_control
 * 
 * The following example shows how to suspend/resume the device independently
 * of the rest of the system.
 * Suspend the device:
 * 	echo 2 > /proc/pvr/power_control
 * Suspend the system.  Then you should be able to suspend and resume
 * as normal.  To resume the device type the following:
 * 	echo 0 > /proc/pvr/power_control
 */

IMG_INT PVRProcSetPowerLevel(struct file *file, const IMG_CHAR *buffer, IMG_UINT32 count, IMG_VOID *data)
{
	IMG_CHAR data_buffer[2];
	IMG_UINT32 PVRPowerLevel;

	if (count != sizeof(data_buffer))
	{
		return -EINVAL;
	}
	else
	{
		if (copy_from_user(data_buffer, buffer, count))
			return -EINVAL;
		if (data_buffer[count - 1] != '\n')
			return -EINVAL;
		PVRPowerLevel = data_buffer[0] - '0';
		if (PVRPowerLevel != gPVRPowerLevel)
		{
			if (PVRPowerLevel != 0)
			{
				if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D3) != PVRSRV_OK)
				{
					return -EINVAL;
				}
			}
			else
			{
				if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D0) != PVRSRV_OK)
				{
					return -EINVAL;
				}
			}

			gPVRPowerLevel = PVRPowerLevel;
		}
	}
	return (count);
}

void ProcSeqShowPowerLevel(struct seq_file *sfile,void* el)	
{
	seq_printf(sfile, "%lu\n", gPVRPowerLevel);
}

#endif

/*!
******************************************************************************

 @Function		PVRSRVOpen

 @Description

 Release access the PVR services node - called when a file is closed, whether
 at exit or using close(2) system call.

 @input pInode - the inode for the file being openeded

 @input pFile - the file handle data for the actual file being opened

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM)
int PVRSRVOpen(struct drm_device unref__ *dev, struct drm_file *pFile)
#else
static int PVRSRVOpen(struct inode unref__ * pInode, struct file *pFile)
#endif
{
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData;
	IMG_HANDLE hBlockAlloc;
	int iRet = -ENOMEM;
	PVRSRV_ERROR eError;
	IMG_UINT32 ui32PID;
#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	PVRSRV_ENV_PER_PROCESS_DATA *psEnvPerProc;
#endif

	LinuxLockMutex(&gPVRSRVLock);

	ui32PID = OSGetCurrentProcessIDKM();

	if (PVRSRVProcessConnect(ui32PID, 0) != PVRSRV_OK)
		goto err_unlock;

#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	psEnvPerProc = PVRSRVPerProcessPrivateData(ui32PID);
	if (psEnvPerProc == IMG_NULL)
	{
		PVR_DPF((PVR_DBG_ERROR, "%s: No per-process private data", __FUNCTION__));
		goto err_unlock;
	}
#endif

	eError = OSAllocMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
						sizeof(PVRSRV_FILE_PRIVATE_DATA),
						(IMG_PVOID *)&psPrivateData,
						&hBlockAlloc,
						"File Private Data");

	if(eError != PVRSRV_OK)
		goto err_unlock;

	psPrivateData->hKernelMemInfo = NULL;
#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
	psPrivateData->psDRMFile = pFile;

	list_add_tail(&psPrivateData->sDRMAuthListItem, &psEnvPerProc->sDRMAuthListHead);
#endif
	psPrivateData->ui32OpenPID = ui32PID;
	psPrivateData->hBlockAlloc = hBlockAlloc;
	PRIVATE_DATA(pFile) = psPrivateData;
	iRet = 0;
err_unlock:	
	LinuxUnLockMutex(&gPVRSRVLock);
	return iRet;
}


/*!
******************************************************************************

 @Function		PVRSRVRelease

 @Description

 Release access the PVR services node - called when a file is closed, whether
 at exit or using close(2) system call.

 @input pInode - the inode for the file being released

 @input pFile - the file handle data for the actual file being released

 @Return 0 for success or <0 for an error.

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM)
void PVRSRVRelease(void *pvPrivData)
#else
static int PVRSRVRelease(struct inode unref__ * pInode, struct file *pFile)
#endif
{
	PVRSRV_FILE_PRIVATE_DATA *psPrivateData;
	int err = 0;

	LinuxLockMutex(&gPVRSRVLock);

#if defined(SUPPORT_DRI_DRM)
	psPrivateData = (PVRSRV_FILE_PRIVATE_DATA *)pvPrivData;
#else
	psPrivateData = PRIVATE_DATA(pFile);
#endif
	if (psPrivateData != IMG_NULL)
	{
#if defined(SUPPORT_DRI_DRM) && defined(PVR_SECURE_DRM_AUTH_EXPORT)
		list_del(&psPrivateData->sDRMAuthListItem);
#endif

		if(psPrivateData->hKernelMemInfo)
		{
			PVRSRV_KERNEL_MEM_INFO *psKernelMemInfo;

			/* Look up the meminfo we just exported */
			if(PVRSRVLookupHandle(KERNEL_HANDLE_BASE,
								  (IMG_PVOID *)&psKernelMemInfo,
								  psPrivateData->hKernelMemInfo,
								  PVRSRV_HANDLE_TYPE_MEM_INFO) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: Failed to look up export handle", __FUNCTION__));
				err = -EFAULT;
				goto err_unlock;
			}

			/* Tell the XProc about the export if required */
			if (psKernelMemInfo->sShareMemWorkaround.bInUse)
			{
				BM_XProcIndexRelease(psKernelMemInfo->sShareMemWorkaround.ui32ShareIndex);
			}

			/* This drops the psMemInfo refcount bumped on export */
			if(FreeMemCallBackCommon(psKernelMemInfo, 0,
									 PVRSRV_FREE_CALLBACK_ORIGIN_EXTERNAL) != PVRSRV_OK)
			{
				PVR_DPF((PVR_DBG_ERROR, "%s: FreeMemCallBackCommon failed", __FUNCTION__));
				err = -EFAULT;
				goto err_unlock;
			}
		}

		/* Usually this is the same as OSGetCurrentProcessIDKM(),
		 * but not necessarily (e.g. fork(), child closes last..)
		 */
		gui32ReleasePID = psPrivateData->ui32OpenPID;
		PVRSRVProcessDisconnect(psPrivateData->ui32OpenPID);
		gui32ReleasePID = 0;

		OSFreeMem(PVRSRV_OS_NON_PAGEABLE_HEAP,
				  sizeof(PVRSRV_FILE_PRIVATE_DATA),
				  psPrivateData, psPrivateData->hBlockAlloc);

#if !defined(SUPPORT_DRI_DRM)
		PRIVATE_DATA(pFile) = IMG_NULL; /*nulling shared pointer*/
#endif
	}

err_unlock:
	LinuxUnLockMutex(&gPVRSRVLock);
#if defined(SUPPORT_DRI_DRM)
	return;
#else
	return err;
#endif
}


/*!
******************************************************************************

 @Function		PVRCore_Init

 @Description

 Insert the driver into the kernel.

 The device major number is allocated by the kernel dynamically.  This means
 that the device node (nominally /dev/pvrsrv) will need to be re-made at boot
 time if the number changes between subsequent loads of the module.  While the
 number often stays constant between loads this is not guaranteed.  The node
 is made as root on the shell with:

 		mknod /dev/pvrsrv c nnn 0

 where nnn is the major number found in /proc/devices for DEVNAME and also
 reported by the PVR_DPF() - look at the boot log using dmesg' to see this).

 Currently the auto-generated script /etc/init.d/rc.pvr handles creation of
 the device.  In other environments the device may be created either through
 devfs or sysfs.

 Readable proc-filesystem entries under /proc/pvr are created with
 CreateProcEntries().  These can be read at runtime to get information about
 the device (eg. 'cat /proc/pvr/vm')

 __init places the function in a special memory section that the kernel frees
 once the function has been run.  Refer also to module_init() macro call below.

 @input none

 @Return none

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM)
int PVRCore_Init(void)
#else
static int __init PVRCore_Init(void)
#endif
{
	int error;
#if !defined(PVR_LDM_MODULE)
	PVRSRV_ERROR eError;
#endif
#if !defined(SUPPORT_DRI_DRM) && defined(PVR_LDM_DEVICE_CLASS)
	struct device *psDev;
#endif

#if defined(SUPPORT_ANDROID_SYNC)
	PVRSyncDeviceInit();
#endif

#if !defined(SUPPORT_DRI_DRM)
	/*
	 * Must come before attempting to print anything via Services.
	 * For DRM, the initialisation will already have been done.
	 */
	PVRDPFInit();
#endif
	PVR_TRACE(("PVRCore_Init"));

	LinuxInitMutex(&gsPMMutex);
	LinuxInitMutex(&gPVRSRVLock);

	if (CreateProcEntries ())
	{
		error = -ENOMEM;
		return error;
	}

	if (PVROSFuncInit() != PVRSRV_OK)
	{
		error = -ENOMEM;
		goto init_failed;
	}

	PVRLinuxMUtilsInit();

	if(LinuxMMInit() != PVRSRV_OK)
	{
		error = -ENOMEM;
		goto init_failed;
	}

	LinuxBridgeInit();
	

	PVRMMapInit();

#ifdef CONFIG_PM_RUNTIME
	error = register_pm_notifier(&pvr_notif_block);
	if (error)
		return error;
#endif
#if defined(PVR_LDM_MODULE)
#if defined(PVR_LDM_PLATFORM_MODULE) || defined(SUPPORT_DRI_DRM_PLUGIN)
/*S.LSI*/
	if ((error = platform_driver_register(&powervr_driver)) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register platform driver (%d)", error));
#ifdef CONFIG_PM_RUNTIME
		unregister_pm_notifier(&pvr_notif_block);
#endif
		goto init_failed;
	}
#if defined(CONFIG_EXYNOS_THERMAL)
	exynos_tmu_add_notifier(&exynos_tmu_nb);
#endif
#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	exynos5_mif_register_notifier(&exynos_mif_nb);
#endif
#endif /* PVR_LDM_PLATFORM_MODULE */

#if defined(PVR_LDM_PCI_MODULE)
	if ((error = pci_register_driver(&powervr_driver)) != 0)
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to register PCI driver (%d)", error));

		goto init_failed;
	}
#endif /* PVR_LDM_PCI_MODULE */
#endif /* defined(PVR_LDM_MODULE) */

#if !defined(PVR_LDM_MODULE)
	/*
	 * Drivers using LDM, will call SysInitialise in the probe/attach code
	 */
	if ((eError = SysInitialise()) != PVRSRV_OK)
	{
		error = -ENODEV;
#if defined(TCF_REV) && (TCF_REV == 110)
		if(eError == PVRSRV_ERROR_NOT_SUPPORTED)
		{
			printk("\nAtlas wrapper (FPGA image) version mismatch");
			error = -ENODEV;
		}
#endif
		goto init_failed;
	}

	error = PVRSRVIONClientCreate();
	if (error != 0)
	{
		goto sys_deinit;
	}
#endif /* !defined(PVR_LDM_MODULE) */

#if !defined(SUPPORT_DRI_DRM)
/*S.LSI*/
	AssignedMajorNumber = 240;
	if (register_chrdev(AssignedMajorNumber, DEVNAME, &pvrsrv_fops))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to get major number"));

		error = -EBUSY;
		goto sys_deinit;
	}

	PVR_TRACE(("PVRCore_Init: major device %d", AssignedMajorNumber));

#if defined(PVR_LDM_DEVICE_CLASS)
	/*
	 * This code (using GPL symbols) facilitates automatic device
	 * node creation on platforms with udev (or similar).
	 */
	psPvrClass = class_create(THIS_MODULE, "pvr");

	if (IS_ERR(psPvrClass))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create class (%ld)", PTR_ERR(psPvrClass)));
		error = -EBUSY;
		goto unregister_device;
	}

	psDev = device_create(psPvrClass, NULL, MKDEV(AssignedMajorNumber, 0),
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,26))
				  NULL,
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,26)) */
				  DEVNAME);
	if (IS_ERR(psDev))
	{
		PVR_DPF((PVR_DBG_ERROR, "PVRCore_Init: unable to create device (%ld)", PTR_ERR(psDev)));
		error = -EBUSY;
		goto destroy_class;
	}
#endif /* defined(PVR_LDM_DEVICE_CLASS) */
#endif /* !defined(SUPPORT_DRI_DRM) */

	return 0;

#if !defined(SUPPORT_DRI_DRM)
#if defined(PVR_LDM_DEVICE_CLASS)
destroy_class:
	class_destroy(psPvrClass);
unregister_device:
	unregister_chrdev((IMG_UINT)AssignedMajorNumber, DEVNAME);
#endif
#endif
#if !defined(PVR_LDM_MODULE) || !defined(SUPPORT_DRI_DRM)
sys_deinit:
#endif
#if defined(PVR_LDM_MODULE)
#if defined(PVR_LDM_PCI_MODULE)
	pci_unregister_driver(&powervr_driver);
#endif

/*S.LSI*/
#if defined (PVR_LDM_PLATFORM_MODULE)
	platform_device_unregister(&powervr_device);
	platform_driver_unregister(&powervr_driver);
#endif
#ifdef CONFIG_PM_RUNTIME
	unregister_pm_notifier(&pvr_notif_block);
#endif
#else	/* defined(PVR_LDM_MODULE) */
	PVRSRVIONClientDestroy();

	/* LDM drivers call SysDeinitialise during PVRSRVDriverRemove */
	{
		SYS_DATA *psSysData;

		psSysData = SysAcquireDataNoCheck();
		if (psSysData != IMG_NULL)
		{
			(void) SysDeinitialise(psSysData);
		}
	}
#endif	/* defined(PVR_LDM_MODULE) */
init_failed:
	PVRMMapCleanup();
	LinuxMMCleanup();
	LinuxBridgeDeInit();
	PVROSFuncDeInit();
	RemoveProcEntries();

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

 Note that the for LDM on MontaVista kernels, the positioning of the driver
 de-registration is the opposite way around than would be suggested by the
 registration case or the 2,6 kernel case.  This is the correct way to do it
 and the kernel panics if you change it.  You have been warned.

 @input none

 @Return none

*****************************************************************************/
#if defined(SUPPORT_DRI_DRM)
void PVRCore_Cleanup(void)
#else
static void __exit PVRCore_Cleanup(void)
#endif
{
#if !defined(PVR_LDM_MODULE)
	SYS_DATA *psSysData;
#endif
	PVR_TRACE(("PVRCore_Cleanup"));

#if !defined(PVR_LDM_MODULE)
	SysAcquireData(&psSysData);
#endif

#if defined(SUPPORT_ANDROID_SYNC)
	PVRSyncDeviceDeInit();
#endif

#if !defined(SUPPORT_DRI_DRM)

#if defined(PVR_LDM_DEVICE_CLASS)
	device_destroy(psPvrClass, MKDEV(AssignedMajorNumber, 0));
	class_destroy(psPvrClass);
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22))
	if (
#endif	/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22)) */
		unregister_chrdev((IMG_UINT)AssignedMajorNumber, DEVNAME)
#if !(LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22))
								;
#else	/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22)) */
								)
	{
		PVR_DPF((PVR_DBG_ERROR," can't unregister device major %d", AssignedMajorNumber));
	}
#endif	/* (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,22)) */
#endif	/* !defined(SUPPORT_DRI_DRM) */

#if defined(PVR_LDM_MODULE)

#if defined(PVR_LDM_PCI_MODULE)
	pci_unregister_driver(&powervr_driver);
#endif

/*S.LSI*/
#if defined (PVR_LDM_PLATFORM_MODULE)
	platform_device_unregister(&powervr_device);
	platform_driver_unregister(&powervr_driver);
#endif

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
	exynos5_mif_unregister_notifier(&exynos_mif_nb);
#endif
#else /* defined(PVR_LDM_MODULE) */
#if defined(DEBUG) && defined(PVR_MANUAL_POWER_CONTROL)
	if (gPVRPowerLevel != 0)
	{
		if (PVRSRVSetPowerStateKM(PVRSRV_SYS_POWER_STATE_D0) == PVRSRV_OK)
		{
			gPVRPowerLevel = 0;
		}
	}
#endif
	PVRSRVIONClientDestroy();

	/* LDM drivers call SysDeinitialise during PVRSRVDriverRemove */
	(void) SysDeinitialise(psSysData);
#endif /* defined(PVR_LDM_MODULE) */

	PVRMMapCleanup();

	LinuxMMCleanup();

	LinuxBridgeDeInit();

	PVROSFuncDeInit();

	RemoveProcEntries();

	PVR_TRACE(("PVRCore_Cleanup: unloading"));
}

/*
 * These macro calls define the initialisation and removal functions of the
 * driver.  Although they are prefixed `module_', they apply when compiling
 * statically as well; in both cases they define the function the kernel will
 * run to start/stop the driver.
*/
#if !defined(SUPPORT_DRI_DRM)
module_init(PVRCore_Init);
module_exit(PVRCore_Cleanup);
#endif
