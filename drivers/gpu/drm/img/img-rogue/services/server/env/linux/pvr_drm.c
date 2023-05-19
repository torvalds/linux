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
#include <linux/mutex.h>
#include <linux/pm_runtime.h>

#include "module_common.h"
#include "pvr_drm.h"
#include "pvr_drv.h"
#include "pvrversion.h"
#include "services_kernel_client.h"
#include "pvr_sync_ioctl_drm.h"

#include "kernel_compatibility.h"

#define PVR_DRM_DRIVER_NAME PVR_DRM_NAME
#define PVR_DRM_DRIVER_DESC "Imagination Technologies PVR DRM"
#define	PVR_DRM_DRIVER_DATE "20170530"

/*
 * Protects global PVRSRV_DATA on a multi device system. i.e. this is used to
 * protect the PVRSRVCommonDeviceXXXX() APIs in the Server common layer which
 * are not re-entrant for device creation and initialisation.
 */
static DEFINE_MUTEX(g_device_mutex);

/* Executed before sleep/suspend-to-RAM/S3. During this phase the content
 * of the video memory is preserved (copied to system RAM). This step is
 * necessary because the device can be powered off and the content of the
 * video memory lost.
 */
static int pvr_pm_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("device %p\n", dev);

	return PVRSRVDeviceSuspend(ddev);
}

/* Executed after the system is woken up from sleep/suspend-to-RAM/S3. This
 * phase restores the content of the video memory from the system RAM.
 */
static int pvr_pm_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("device %p\n", dev);

	return PVRSRVDeviceResume(ddev);
}


static int pvr_pm_runtime_suspend(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;

	sPVRSRVDeviceSuspend(priv->dev_node);
	return 0;
}

static int pvr_pm_runtime_resume(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);
	struct pvr_drm_private *priv = ddev->dev_private;

	sPVRSRVDeviceResume(priv->dev_node);
	return 0;
}


/* Executed before the hibernation image is created. This callback allows to
 * preserve the content of the video RAM into the system RAM which in turn
 * is then stored into a disk.
 */
static int pvr_pm_freeze(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("%s(): device %p\n", __func__, dev);

	return PVRSRVDeviceSuspend(ddev);
}

/* Executed after the hibernation image is created or if the creation of the
 * image has failed. This callback should undo whatever was done in
 * pvr_pm_freeze to allow the device to operate in the same way as before the
 * call to pvr_pm_freeze.
 */
static int pvr_pm_thaw(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("%s(): device %p\n", __func__, dev);

	return PVRSRVDeviceResume(ddev);
}

/* Executed after the hibernation image is created. This callback should not
 * preserve the content of the video memory since this was already done
 * in pvr_pm_freeze.
 *
 * Note: from the tests performed on a TestChip this callback is not executed
 *       and driver's pvr_shutdown() is executed instead.
 */
static int pvr_pm_poweroff(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("%s(): device %p\n", __func__, dev);

	PVRSRVDeviceShutdown(ddev);

	return 0;
}

/* Executed after the content of the system memory is restored from the
 * hibernation image. This callback restored video RAM from the system RAM
 * and performs any necessary device setup required for the device to operate
 * properly.
 */
static int pvr_pm_restore(struct device *dev)
{
	struct drm_device *ddev = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("%s(): device %p\n", __func__, dev);

	return PVRSRVDeviceResume(ddev);
}

const struct dev_pm_ops pvr_pm_ops = {
	/* Sleep (suspend-to-RAM/S3) callbacks.
	 * This mode saves the content of the video RAM to the system RAM and
	 * powers off the device to reduce the power consumption. Because the
	 * video RAM can be powered off, it needs to be preserved beforehand.
	 */
	.suspend = pvr_pm_suspend,
	.resume = pvr_pm_resume,
        .runtime_suspend = pvr_pm_runtime_suspend,
        .runtime_resume = pvr_pm_runtime_resume,


	/* Hibernation (suspend-to-disk/S4) callbacks.
	 * This mode saves the content of the video RAM to the system RAM and then
	 * dumps the system RAM to disk (swap partition or swap file). The system
	 * then powers off. After power on the system RAM content is loaded from
	 * the disk and then video RAM is restored from the system RAM.
	 *
	 * The procedure is executed in following order
	 *
	 * - Suspend-to-disk is triggered
	 *   At this point the OS goes through the list of all registered devices and
	 *   calls provided callbacks.
	 * -- pvr_pm_freeze() is called
	 *         The GPU is powered of and submitting new work is blocked.
	 *         The content of the video RAM is saved to the system RAM, and
	 *         other actions required to suspend the device are performed.
	 * -- system RAM image is created and saved on the disk
	 *         The disk now contains a snapshot for the DDK Driver for the
	 *         moment when pvr_pm_freeze() was called.
	 * -- pvr_pm_thaw() is called
	 *         All actions taken in pvr_pm_freeze() are undone. The memory
	 *         allocated for the video RAM is freed and all actions necessary
	 *         to bring the device to operational state are taken.
	 *         This makes sure that regardless if image was created successfully
	 *         or not the device remains operational.
	 *
	 * - System is powered off
	 * -- pvr_shutdown() is called
	 *         No actions are required beside powering off the GPU.
	 *
	 * - System is powered up
	 * -- system RAM image is read from the disk
	 *         This restores the snapshot of the DDK driver along with the saved
	 *         video RAM buffer.
	 * -- pvr_pm_restore() is called
	 *         Video RAM is restored from the buffer located in the system RAM.
	 *         Actions to reset the device and bring it back to working state
	 *         are taken. Video RAM buffer is freed.
	 *         In summary the same procedure as in the case of pvr_pm_thaw() is
	 *         performed.
	 */
	.freeze = pvr_pm_freeze,
	.thaw = pvr_pm_thaw,
	.poweroff = pvr_pm_poweroff,
	.restore = pvr_pm_restore,
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

	mutex_lock(&g_device_mutex);

	srv_err = PVRSRVCommonDeviceCreate(ddev->dev, deviceId, &priv->dev_node);
	if (srv_err != PVRSRV_OK) {
		DRM_ERROR("failed to create device node for device %p (%s)\n",
			  ddev->dev, PVRSRVGetErrorString(srv_err));
		if (srv_err == PVRSRV_ERROR_PROBE_DEFER)
			err = -EPROBE_DEFER;
		else
			err = -ENODEV;
		goto err_unset_dma_parms;
	}

	err = PVRSRVDeviceInit(priv->dev_node);
	if (err) {
		DRM_ERROR("device %p initialisation failed (err=%d)\n",
			  ddev->dev, err);
		goto err_device_destroy;
	}

	drm_mode_config_init(ddev);

#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_PROBE)
	srv_err = PVRSRVCommonDeviceInitialise(priv->dev_node);
	if (srv_err != PVRSRV_OK) {
		err = -ENODEV;
		DRM_ERROR("device %p initialisation failed (err=%d)\n",
			  ddev->dev, err);
		goto err_device_deinit;
	}
#endif

	mutex_unlock(&g_device_mutex);

	return 0;

#if (PVRSRV_DEVICE_INIT_MODE == PVRSRV_LINUX_DEV_INIT_ON_PROBE)
err_device_deinit:
	drm_mode_config_cleanup(ddev);
	PVRSRVDeviceDeinit(priv->dev_node);
#endif
err_device_destroy:
	PVRSRVCommonDeviceDestroy(priv->dev_node);
err_unset_dma_parms:
	mutex_unlock(&g_device_mutex);
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

	pm_runtime_disable(ddev->dev);
	mutex_lock(&g_device_mutex);
	PVRSRVCommonDeviceDestroy(priv->dev_node);
	mutex_unlock(&g_device_mutex);

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
#if (PVRSRV_DEVICE_INIT_MODE != PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
	struct pvr_drm_private *priv = ddev->dev_private;
	int err;
#endif

	if (!try_module_get(THIS_MODULE)) {
		DRM_ERROR("failed to get module reference\n");
		return -ENOENT;
	}

#if (PVRSRV_DEVICE_INIT_MODE != PVRSRV_LINUX_DEV_INIT_ON_CONNECT)
	err = PVRSRVDeviceServicesOpen(priv->dev_node, dfile);
	if (err)
		module_put(THIS_MODULE);

	return err;
#else
	return 0;
#endif
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
	DRM_IOCTL_DEF_DRV(PVR_SRVKM_CMD, PVRSRV_BridgeDispatchKM,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_SRVKM_INIT, drm_pvr_srvkm_init,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
#if defined(SUPPORT_NATIVE_FENCE_SYNC) && !defined(USE_PVRSYNC_DEVNODE)
	DRM_IOCTL_DEF_DRV(PVR_SYNC_RENAME_CMD, pvr_sync_rename_ioctl,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_SYNC_FORCE_SW_ONLY_CMD, pvr_sync_force_sw_only_ioctl,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_SW_SYNC_CREATE_FENCE_CMD, pvr_sw_sync_create_fence_ioctl,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(PVR_SW_SYNC_INC_CMD, pvr_sw_sync_inc_ioctl,
			  DRM_RENDER_ALLOW | DRM_UNLOCKED),
#endif
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

const struct file_operations pvr_drm_fops = {
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
