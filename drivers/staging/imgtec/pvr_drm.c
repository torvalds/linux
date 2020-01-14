/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File
@Title          PowerVR DRM driver
@Codingstyle    LinuxKernel
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

#include <drm/drm.h>
#include <drm/drmP.h> /* include before drm_crtc.h for kernels older than 3.9 */
#include <drm/drm_crtc.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/version.h>

#include "module_common.h"
#include "pvr_drm.h"
#include "pvr_drv.h"
#include "pvrversion.h"
#include "services_kernel_client.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
#define DRIVER_RENDER 0
#define DRM_RENDER_ALLOW 0
#endif

#define PVR_DRM_DRIVER_NAME PVR_DRM_NAME
#define PVR_DRM_DRIVER_DESC "Imagination Technologies PVR DRM"
#define	PVR_DRM_DRIVER_DATE "20110701"


static int pvr_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("device %p\n", dev);

	return PVRSRVCommonDeviceSuspend(ddev->dev_private);
}

static int pvr_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("device %p\n", dev);

	return PVRSRVCommonDeviceResume(ddev->dev_private);
}

const struct dev_pm_ops pvr_pm_ops = {
	.suspend = pvr_pm_suspend,
	.resume = pvr_pm_resume,
};


int pvr_drm_load(struct drm_device *ddev, unsigned long flags)
{
	struct _PVRSRV_DEVICE_NODE_ *dev_node;
	enum PVRSRV_ERROR srv_err;
	int err;

	DRM_DEBUG_DRIVER("device %p\n", ddev->dev);

	/*
	 * The equivalent is done for PCI modesetting drivers by
	 * drm_get_pci_dev()
	 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	if (ddev->platformdev)
		platform_set_drvdata(ddev->platformdev, ddev);
#else
	dev_set_drvdata(ddev->dev, ddev);
#endif

	srv_err = PVRSRVDeviceCreate(ddev->dev, &dev_node);
	if (srv_err != PVRSRV_OK) {
		DRM_ERROR("failed to create device node for device %p (%s)\n",
			  ddev->dev, PVRSRVGetErrorStringKM(srv_err));
		if (srv_err == PVRSRV_ERROR_PROBE_DEFER)
			err = -EPROBE_DEFER;
		else
			err = -ENODEV;
		goto err_exit;
	}

	err = PVRSRVCommonDeviceInit(dev_node);
	if (err) {
		DRM_ERROR("device %p initialisation failed (err=%d)\n",
			  ddev->dev, err);
		goto err_device_destroy;
	}

	drm_mode_config_init(ddev);
	ddev->dev_private = dev_node;

	return 0;

err_device_destroy:
	PVRSRVDeviceDestroy(dev_node);
err_exit:
	return err;
}

void pvr_drm_unload(struct drm_device *ddev)
{
	DRM_DEBUG_DRIVER("device %p\n", ddev->dev);

	PVRSRVCommonDeviceDeinit(ddev->dev_private);

	PVRSRVDeviceDestroy(ddev->dev_private);
	ddev->dev_private = NULL;

	//return 0;
}

static int pvr_drm_open(struct drm_device *ddev, struct drm_file *dfile)
{
	int err;

	if (!try_module_get(THIS_MODULE)) {
		DRM_ERROR("failed to get module reference\n");
		return -ENOENT;
	}

	err = PVRSRVCommonDeviceOpen(ddev->dev_private, dfile);
	if (err)
		module_put(THIS_MODULE);

	return err;
}

static void pvr_drm_release(struct drm_device *ddev, struct drm_file *dfile)
{
	PVRSRVCommonDeviceRelease(ddev->dev_private, dfile);

	module_put(THIS_MODULE);
}

/*
 * The DRM global lock is taken for ioctls unless the DRM_UNLOCKED flag is set.
 * If you revise one of the driver specific ioctls, or add a new one, that has
 * DRM_UNLOCKED set then consider whether the gPVRSRVLock mutex needs to be
 * taken.
 */
static struct drm_ioctl_desc pvr_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(PVR_SRVKM_CMD, PVRSRV_BridgeDispatchKM, DRM_RENDER_ALLOW | DRM_UNLOCKED),
#if defined(PDUMP)
	DRM_IOCTL_DEF_DRV(PVR_DBGDRV_CMD, dbgdrv_ioctl, DRM_RENDER_ALLOW | DRM_AUTH | DRM_UNLOCKED),
#endif
};

#if defined(CONFIG_COMPAT)
#if defined(PDUMP)
static drm_ioctl_compat_t *pvr_drm_compat_ioctls[] = {
	[DRM_PVR_DBGDRV_CMD] = dbgdrv_ioctl_compat,
};
#endif

static long pvr_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(file, cmd, arg);

#if defined(PDUMP)
	if (nr < DRM_COMMAND_BASE + ARRAY_SIZE(pvr_drm_compat_ioctls)) {
		drm_ioctl_compat_t *pfnBridge;

		pfnBridge = pvr_drm_compat_ioctls[nr - DRM_COMMAND_BASE];
		if (pfnBridge)
			return pfnBridge(file, cmd, arg);
	}
#endif

	return drm_ioctl(file, cmd, arg);
}
#endif /* defined(CONFIG_COMPAT) */

int g_gpu_performance = -1;
static ssize_t  PVRSRV_Perf_Write(struct file *pfile, const char __user *ubuf,
				size_t count, loff_t *ploff)
{
	char kbuf[10];
	long enable = -1;

	if (count > sizeof(kbuf))
		count = sizeof(kbuf);

	if (copy_from_user(kbuf, ubuf, count)) {
		DRM_ERROR("%s: copy_to_user failed!\n", __func__);
		return -EFAULT;
	}

	kbuf[count - 1] = '\0';
	if (kstrtol(kbuf, 10, &enable) != 0) {
		DRM_ERROR("%s: kstrtol failed!\n", __func__);
		return -EFAULT;
	}

	if (g_gpu_performance != enable)
		g_gpu_performance = enable;

	return count;
}

static const struct file_operations pvr_drm_fops = {
	.owner			= THIS_MODULE,
	.open			= drm_open,
	.release		= drm_release,
	/*
	 * FIXME:
	 * Wrap this in a function that checks enough data has been
	 * supplied with the ioctl (e.g. _IOCDIR(nr) != _IOC_NONE &&
	 * _IOC_SIZE(nr) == size).
	 */
	.unlocked_ioctl		= drm_ioctl,
#if defined(CONFIG_COMPAT)
	.compat_ioctl		= pvr_compat_ioctl,
#endif
	.mmap			= PVRSRV_MMap,
	.poll			= drm_poll,
	.read			= drm_read,
	.write			= PVRSRV_Perf_Write,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	.fasync			= drm_fasync,
#endif
};

const struct drm_driver pvr_drm_generic_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_RENDER,

	.dev_priv_size		= 0,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0))
	.load			= NULL,
	.unload			= NULL,
#else
	.load			= pvr_drm_load,
	.unload			= pvr_drm_unload,
#endif
	.open			= pvr_drm_open,
	.postclose		= pvr_drm_release,

	.ioctls			= pvr_drm_ioctls,
	.num_ioctls		= ARRAY_SIZE(pvr_drm_ioctls),
	.fops			= &pvr_drm_fops,

	.name			= PVR_DRM_DRIVER_NAME,
	.desc			= PVR_DRM_DRIVER_DESC,
	.date			= PVR_DRM_DRIVER_DATE,
	.major			= PVRVERSION_MAJ,
	.minor			= PVRVERSION_MIN,
	.patchlevel		= PVRVERSION_BUILD,
};
