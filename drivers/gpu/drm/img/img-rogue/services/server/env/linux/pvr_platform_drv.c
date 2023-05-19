/*
 * @File
 * @Title       PowerVR DRM platform driver
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 5, 0))
#include <drm/drm_drv.h>
#include <drm/drm_print.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/slab.h>
#else
#include <drm/drmP.h>
#endif

#include <linux/module.h>
#include <linux/platform_device.h>

#include "module_common.h"
#include "pvr_drv.h"
#include "pvrmodule.h"
#include "sysinfo.h"


/* This header must always be included last */
#include "kernel_compatibility.h"

MODULE_IMPORT_NS(DMA_BUF);

static struct drm_driver pvr_drm_platform_driver;

#if defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
/*
 * This is an arbitrary value. If it's changed then the 'num_devices' module
 * parameter description should also be updated to match.
 */
#define MAX_DEVICES 16

static unsigned int pvr_num_devices = 1;
static struct platform_device **pvr_devices;

#if defined(NO_HARDWARE)
static int pvr_num_devices_set(const char *val,
			       const struct kernel_param *param)
{
	int err;

	err = param_set_uint(val, param);
	if (err)
		return err;

	if (pvr_num_devices == 0 || pvr_num_devices > MAX_DEVICES)
		return -EINVAL;

	return 0;
}

static const struct kernel_param_ops pvr_num_devices_ops = {
	.set = pvr_num_devices_set,
	.get = param_get_uint,
};

module_param_cb(num_devices, &pvr_num_devices_ops, &pvr_num_devices, 0444);
MODULE_PARM_DESC(num_devices,
		 "Number of platform devices to register (default: 1 - max: 16)");
#endif /* defined(NO_HARDWARE) */
#endif /* defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */

#if 0
static int pvr_devices_register(void)
{
#if defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	struct platform_device_info pvr_dev_info = {
		.name = SYS_RGX_DEV_NAME,
		.id = -2,
#if defined(NO_HARDWARE)
		/* Not all cores have 40 bit physical support, but this
		 * will work unless > 32 bit address is returned on those cores.
		 * In the future this will be fixed more correctly.
		 */
		.dma_mask = DMA_BIT_MASK(40),
#else
		.dma_mask = DMA_BIT_MASK(32),
#endif
	};
	unsigned int i;

	BUG_ON(pvr_num_devices == 0 || pvr_num_devices > MAX_DEVICES);

	pvr_devices = kmalloc_array(pvr_num_devices, sizeof(*pvr_devices),
				    GFP_KERNEL);
	if (!pvr_devices)
		return -ENOMEM;

	for (i = 0; i < pvr_num_devices; i++) {
		pvr_devices[i] = platform_device_register_full(&pvr_dev_info);
		if (IS_ERR(pvr_devices[i])) {
			DRM_ERROR("unable to register device %u (err=%ld)\n",
				  i, PTR_ERR(pvr_devices[i]));
			pvr_devices[i] = NULL;
			return -ENODEV;
		}
	}
#endif /* defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */

	return 0;
}
#endif

static void pvr_devices_unregister(void)
{
#if defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED)
	unsigned int i;

	BUG_ON(!pvr_devices);

	for (i = 0; i < pvr_num_devices && pvr_devices[i]; i++)
		platform_device_unregister(pvr_devices[i]);

	kfree(pvr_devices);
	pvr_devices = NULL;
#endif /* defined(MODULE) && !defined(PVR_LDM_PLATFORM_PRE_REGISTERED) */
}

static int pvr_probe(struct platform_device *pdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	struct drm_device *ddev;
	int ret;

	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

	ddev = drm_dev_alloc(&pvr_drm_platform_driver, &pdev->dev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0))
	if (IS_ERR(ddev))
		return PTR_ERR(ddev);
#else
	if (!ddev)
		return -ENOMEM;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	/* Needed by drm_platform_set_busid */
	ddev->platformdev = pdev;
#endif

	/*
	 * The load callback, called from drm_dev_register, is deprecated,
	 * because of potential race conditions. Calling the function here,
	 * before calling drm_dev_register, avoids those potential races.
	 */
	BUG_ON(pvr_drm_platform_driver.load != NULL);
	ret = pvr_drm_load(ddev, 0);
	if (ret)
		goto err_drm_dev_put;

	ret = drm_dev_register(ddev, 0);
	if (ret)
		goto err_drm_dev_unload;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0))
	DRM_INFO("Initialized %s %d.%d.%d %s on minor %d\n",
		pvr_drm_platform_driver.name,
		pvr_drm_platform_driver.major,
		pvr_drm_platform_driver.minor,
		pvr_drm_platform_driver.patchlevel,
		pvr_drm_platform_driver.date,
		ddev->primary->index);
#endif
	return 0;

err_drm_dev_unload:
	pvr_drm_unload(ddev);
err_drm_dev_put:
	drm_dev_put(ddev);
	return	ret;
#else
	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

	return drm_platform_init(&pvr_drm_platform_driver, pdev);
#endif
}

static int pvr_remove(struct platform_device *pdev)
{
	struct drm_device *ddev = platform_get_drvdata(pdev);

	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0))
	drm_dev_unregister(ddev);

	/* The unload callback, called from drm_dev_unregister, is
	 * deprecated. Call the unload function directly.
	 */
	BUG_ON(pvr_drm_platform_driver.unload != NULL);
	pvr_drm_unload(ddev);

	drm_dev_put(ddev);
#else
	drm_put_dev(ddev);
#endif
	return 0;
}

static void pvr_shutdown(struct platform_device *pdev)
{
	struct drm_device *ddev = platform_get_drvdata(pdev);

	DRM_DEBUG_DRIVER("device %p\n", &pdev->dev);

	PVRSRVDeviceShutdown(ddev);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
static const struct of_device_id pvr_of_ids[] = {
#if defined(SYS_RGX_OF_COMPATIBLE)
	{ .compatible = SYS_RGX_OF_COMPATIBLE, },
#endif
	{},
};

#if !defined(CHROMIUMOS_KERNEL) || !defined(MODULE)
MODULE_DEVICE_TABLE(of, pvr_of_ids);
#endif
#endif

static struct platform_device_id pvr_platform_ids[] = {
#if defined(SYS_RGX_DEV_NAME)
	{ SYS_RGX_DEV_NAME, 0 },
#endif
	{ }
};

#if !defined(CHROMIUMOS_KERNEL) || !defined(MODULE)
MODULE_DEVICE_TABLE(platform, pvr_platform_ids);
#endif

static struct platform_driver pvr_platform_driver = {
	.driver = {
		.name		= DRVNAME,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
		.of_match_table	= of_match_ptr(pvr_of_ids),
#endif
		.pm		= &pvr_pm_ops,
	},
	.id_table		= pvr_platform_ids,
	.probe			= pvr_probe,
	.remove			= pvr_remove,
	.shutdown		= pvr_shutdown,
};

static int __init pvr_init(void)
{
	int err;

	DRM_DEBUG_DRIVER("\n");

	pvr_drm_platform_driver = pvr_drm_generic_driver;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)) && \
	(LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
	pvr_drm_platform_driver.set_busid = drm_platform_set_busid;
#endif

	err = PVRSRVDriverInit();
	if (err)
		return err;

	err = platform_driver_register(&pvr_platform_driver);
	if (err)
		return err;

	return 0; // pvr_devices_register();
}

static void __exit pvr_exit(void)
{
	DRM_DEBUG_DRIVER("\n");

	pvr_devices_unregister();
	platform_driver_unregister(&pvr_platform_driver);
	PVRSRVDriverDeinit();

	DRM_DEBUG_DRIVER("done\n");
}

late_initcall(pvr_init);
module_exit(pvr_exit);
