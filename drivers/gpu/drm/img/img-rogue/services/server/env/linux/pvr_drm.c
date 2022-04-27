/*
 * @File
 * @Title       PowerVR DRM driver
 * @Codingstyle LinuxKernel
 * @Copyright   Copyright (c) Imagination Technologies Ltd. All Rights Reserved
 * @License     Dual MIT/GPLv2
 *
 * The contents of this file are subject to the MIT license as set out below.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Alternatively, the contents of this file may be used under the terms of
 * the GNU General Public License Version 2 ("GPL") in which case the provisions
 * of GPL are applicable instead of those above.
 *
 * If you wish to allow use of your version of this file only under the terms of
 * GPL, and not to allow others to use your version of this file under the terms
 * of the MIT license, indicate your decision by deleting the provisions above
 * and replace them with the notice and other provisions required by GPL as set
 * out in the file called "GPL-COPYING" included in this distribution. If you do
 * not delete the provisions above, a recipient may use your version of this file
 * under the terms of either the MIT license or GPL.
 *
 * This License is also included in this distribution in the file called
 * "MIT-COPYING".
 *
 * EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/version.h>

#include <drm/drm.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_drv.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>
#include <linux/dma-mapping.h>
#else
#include <drm/drmP.h> /* include before drm_crtc.h for kernels older than 3.9 */
#endif

#include <drm/drm_crtc.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

#include "module_common.h"
#include "pvr_drm.h"
#include "pvr_drv.h"
#include "pvrversion.h"
#include "services_kernel_client.h"

#include "kernel_compatibility.h"

#define PVR_DRM_DRIVER_NAME PVR_DRM_NAME
#define PVR_DRM_DRIVER_DESC "Imagination Technologies PVR DRM"
#define	PVR_DRM_DRIVER_DATE "20170530"


static int pvr_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;

	DRM_DEBUG_DRIVER("device %p\n", dev);

	return PVRSRVDeviceSuspend(priv->dev_node);
}

static int pvr_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;

	DRM_DEBUG_DRIVER("device %p\n", dev);

	return PVRSRVDeviceResume(priv->dev_node);
}

const struct dev_pm_ops pvr_pm_ops = {
	.suspend = pvr_pm_suspend,
	.resume = pvr_pm_resume,
};


#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0))
static
#endif
int pvr_drm_load(struct drm_device *ddev, unsigned long flags)
{
	struct pvr_drm_private *priv;
	enum PVRSRV_ERROR_TAG srv_err;
	int err, deviceId;

	DRM_DEBUG_DRIVER("device %p\n", ddev->dev);

	dev_set_drvdata(ddev->dev, ddev);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	/*
	 * Older kernels do not have render drm_minor member in drm_device,
	 * so we fallback to primary node for device identification
	 */
	deviceId = ddev->primary->index;
#else
	if (ddev->render)
		deviceId = ddev->render->index;
	else /* when render node is NULL, fallback to primary node */
		deviceId = ddev->primary->index;
#endif

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		err = -ENOMEM;
		goto err_exit;
	}
	ddev->dev_private = priv;

	if (!ddev->dev->dma_parms)
		ddev->dev->dma_parms = &priv->dma_parms;
	dma_set_max_seg_size(ddev->dev, DMA_BIT_MASK(32));

#if defined(SUPPORT_BUFFER_SYNC) || defined(SUPPORT_NATIVE_FENCE_SYNC)
	priv->fence_status_wq = create_freezable_workqueue("pvr_fce_status");
	if (!priv->fence_status_wq) {
		DRM_ERROR("failed to create fence status workqueue\n");
		err = -ENOMEM;
		goto err_unset_dma_parms;
	}
#endif

	srv_err = PVRSRVCommonDeviceCreate(ddev->dev, deviceId, &priv->dev_node);
	if (srv_err != PVRSRV_OK) {
		DRM_ERROR("failed to create device node for device %p (%s)\n",
			  ddev->dev, PVRSRVGetErrorString(srv_err));
		if (srv_err == PVRSRV_ERROR_PROBE_DEFER)
			err = -EPROBE_DEFER;
		else
			err = -ENODEV;
		goto err_workqueue_destroy;
	}

	err = PVRSRVDeviceInit(priv->dev_node);
	if (err) {
		DRM_ERROR("device %p initialisation failed (err=%d)\n",
			  ddev->dev, err);
		goto err_device_destroy;
	}

	drm_mode_config_init(ddev);

#if defined(SUPPORT_FWLOAD_ON_PROBE)
	srv_err = PVRSRVCommonDeviceInitialise(priv->dev_node);
	if (srv_err != PVRSRV_OK) {
		err = -ENODEV;
		DRM_ERROR("device %p initialisation failed (err=%d)\n",
			  ddev->dev, err);
		drm_mode_config_cleanup(ddev);
		PVRSRVDeviceDeinit(priv->dev_node);
		goto err_device_destroy;
	}
#endif

	return 0;

err_device_destroy:
	PVRSRVCommonDeviceDestroy(priv->dev_node);
err_workqueue_destroy:
#if defined(SUPPORT_BUFFER_SYNC) || defined(SUPPORT_NATIVE_FENCE_SYNC)
	destroy_workqueue(priv->fence_status_wq);
err_unset_dma_parms:
#endif
	if (ddev->dev->dma_parms == &priv->dma_parms)
		ddev->dev->dma_parms = NULL;
	kfree(priv);
err_exit:
	return err;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0))
static
#endif
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
int pvr_drm_unload(struct drm_device *ddev)
#else
void pvr_drm_unload(struct drm_device *ddev)
#endif
{
	struct pvr_drm_private *priv = ddev->dev_private;

	DRM_DEBUG_DRIVER("device %p\n", ddev->dev);

	drm_mode_config_cleanup(ddev);

	PVRSRVDeviceDeinit(priv->dev_node);

	PVRSRVCommonDeviceDestroy(priv->dev_node);

#if defined(SUPPORT_BUFFER_SYNC) || defined(SUPPORT_NATIVE_FENCE_SYNC)
	destroy_workqueue(priv->fence_status_wq);
#endif

	if (ddev->dev->dma_parms == &priv->dma_parms)
		ddev->dev->dma_parms = NULL;

	kfree(priv);
	ddev->dev_private = NULL;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	return 0;
#endif
}

static int pvr_drm_open(struct drm_device *ddev, struct drm_file *dfile)
{
	struct pvr_drm_private *priv = ddev->dev_private;
	int err;

	if (!try_module_get(THIS_MODULE)) {
		DRM_ERROR("failed to get module reference\n");
		return -ENOENT;
	}

	err = PVRSRVDeviceOpen(priv->dev_node, dfile);
	if (err)
		module_put(THIS_MODULE);

	return err;
}

static void pvr_drm_release(struct drm_device *ddev, struct drm_file *dfile)
{
	struct pvr_drm_private *priv = ddev->dev_private;

	PVRSRVDeviceRelease(priv->dev_node, dfile);

	module_put(THIS_MODULE);
}

/*
 * The DRM global lock is taken for ioctls unless the DRM_UNLOCKED flag is set.
 */
static struct drm_ioctl_desc pvr_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(PVR_SRVKM_CMD, PVRSRV_BridgeDispatchKM, DRM_RENDER_ALLOW | DRM_UNLOCKED)
};

#if defined(CONFIG_COMPAT)
static long pvr_compat_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	unsigned int nr = DRM_IOCTL_NR(cmd);

	if (nr < DRM_COMMAND_BASE)
		return drm_compat_ioctl(file, cmd, arg);

	return drm_ioctl(file, cmd, arg);
}
#endif /* defined(CONFIG_COMPAT) */

static const struct file_operations pvr_drm_fops = {
	.owner			= THIS_MODULE,
	.open			= drm_open,
	.release		= drm_release,
	.unlocked_ioctl		= drm_ioctl,
#if defined(CONFIG_COMPAT)
	.compat_ioctl		= pvr_compat_ioctl,
#endif
	.mmap			= PVRSRV_MMap,
	.poll			= drm_poll,
	.read			= drm_read,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 0))
	.fasync			= drm_fasync,
#endif
};

const struct drm_driver pvr_drm_generic_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_RENDER,

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
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
