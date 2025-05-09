/*
 * Copyright 2023 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/drm_drv.h>

#include "amdgpu_xcp_drv.h"

#define MAX_XCP_PLATFORM_DEVICE 64

struct xcp_device {
	struct drm_device drm;
	struct platform_device *pdev;
};

static const struct drm_driver amdgpu_xcp_driver = {
	.driver_features = DRIVER_GEM | DRIVER_RENDER,
	.name = "amdgpu_xcp_drv",
	.major = 1,
	.minor = 0,
};

static int8_t pdev_num;
static struct xcp_device *xcp_dev[MAX_XCP_PLATFORM_DEVICE];
static DEFINE_MUTEX(xcp_mutex);

int amdgpu_xcp_drm_dev_alloc(struct drm_device **ddev)
{
	struct platform_device *pdev;
	struct xcp_device *pxcp_dev;
	char dev_name[20];
	int ret, i;

	guard(mutex)(&xcp_mutex);

	if (pdev_num >= MAX_XCP_PLATFORM_DEVICE)
		return -ENODEV;

	for (i = 0; i < MAX_XCP_PLATFORM_DEVICE; i++) {
		if (!xcp_dev[i])
			break;
	}

	if (i >= MAX_XCP_PLATFORM_DEVICE)
		return -ENODEV;

	snprintf(dev_name, sizeof(dev_name), "amdgpu_xcp_%d", i);
	pdev = platform_device_register_simple(dev_name, -1, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	if (!devres_open_group(&pdev->dev, NULL, GFP_KERNEL)) {
		ret = -ENOMEM;
		goto out_unregister;
	}

	pxcp_dev = devm_drm_dev_alloc(&pdev->dev, &amdgpu_xcp_driver, struct xcp_device, drm);
	if (IS_ERR(pxcp_dev)) {
		ret = PTR_ERR(pxcp_dev);
		goto out_devres;
	}

	xcp_dev[i] = pxcp_dev;
	xcp_dev[i]->pdev = pdev;
	*ddev = &pxcp_dev->drm;
	pdev_num++;

	return 0;

out_devres:
	devres_release_group(&pdev->dev, NULL);
out_unregister:
	platform_device_unregister(pdev);

	return ret;
}
EXPORT_SYMBOL(amdgpu_xcp_drm_dev_alloc);

static void free_xcp_dev(int8_t index)
{
	if ((index < MAX_XCP_PLATFORM_DEVICE) && (xcp_dev[index])) {
		struct platform_device *pdev = xcp_dev[index]->pdev;

		devres_release_group(&pdev->dev, NULL);
		platform_device_unregister(pdev);

		xcp_dev[index] = NULL;
		pdev_num--;
	}
}

void amdgpu_xcp_drm_dev_free(struct drm_device *ddev)
{
	int8_t i;

	guard(mutex)(&xcp_mutex);

	for (i = 0; i < MAX_XCP_PLATFORM_DEVICE; i++) {
		if ((xcp_dev[i]) && (&xcp_dev[i]->drm == ddev)) {
			free_xcp_dev(i);
			break;
		}
	}
}
EXPORT_SYMBOL(amdgpu_xcp_drm_dev_free);

void amdgpu_xcp_drv_release(void)
{
	int8_t i;

	guard(mutex)(&xcp_mutex);

	for (i = 0; pdev_num && i < MAX_XCP_PLATFORM_DEVICE; i++) {
		free_xcp_dev(i);
	}
}
EXPORT_SYMBOL(amdgpu_xcp_drv_release);

static void __exit amdgpu_xcp_drv_exit(void)
{
	amdgpu_xcp_drv_release();
}

module_exit(amdgpu_xcp_drv_exit);

MODULE_AUTHOR("AMD linux driver team");
MODULE_DESCRIPTION("AMD XCP PLATFORM DEVICES");
MODULE_LICENSE("GPL and additional rights");
