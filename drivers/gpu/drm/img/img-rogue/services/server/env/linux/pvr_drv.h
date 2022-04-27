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

#if !defined(__PVR_DRV_H__)
#define __PVR_DRV_H__

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <linux/device.h>
#else
#include <drm/drmP.h>
#endif

#include <linux/pm.h>

struct file;
struct _PVRSRV_DEVICE_NODE_;
struct workqueue_struct;
struct vm_area_struct;

/* This structure is used to store Linux specific per-device information. */
struct pvr_drm_private {
	struct _PVRSRV_DEVICE_NODE_ *dev_node;

	/*
	 * This is needed for devices that don't already have their own dma
	 * parameters structure, e.g. platform devices, and, if necessary, will
	 * be assigned to the 'struct device' during device initialisation. It
	 * should therefore never be accessed directly via this structure as
	 * this may not be the version of dma parameters in use.
	 */
	struct device_dma_parameters dma_parms;

#if defined(SUPPORT_BUFFER_SYNC) || defined(SUPPORT_NATIVE_FENCE_SYNC)
	struct workqueue_struct *fence_status_wq;
#endif

	/* PVR Sync debug notify handle */
	void *sync_debug_notify_handle;
};

extern const struct dev_pm_ops pvr_pm_ops;
extern const struct drm_driver pvr_drm_generic_driver;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
int pvr_drm_load(struct drm_device *ddev, unsigned long flags);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
int pvr_drm_unload(struct drm_device *ddev);
#else
void pvr_drm_unload(struct drm_device *ddev);
#endif
#endif

int PVRSRV_BridgeDispatchKM(struct drm_device *dev, void *arg,
			    struct drm_file *file);
int PVRSRV_MMap(struct file *file, struct vm_area_struct *ps_vma);

#endif /* !defined(__PVR_DRV_H__) */
