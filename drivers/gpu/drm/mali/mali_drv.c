/**
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_drv.c
 * Implementation of the Linux device driver entrypoints for Mali DRM
 */

#include <linux/module.h>
#include <linux/vermagic.h>
#include <drm/drmP.h>
#include "mali_drv.h"

static struct platform_device *dev0;
static struct platform_device *dev1;

void mali_drm_preclose(struct drm_device *dev, struct drm_file *file_priv)
{
}

void mali_drm_lastclose(struct drm_device *dev)
{
}

static int mali_drm_suspend(struct drm_device *dev, pm_message_t state)
{
	return 0;
}

static int mali_drm_resume(struct drm_device *dev)
{
	return 0;
}

static int mali_drm_load(struct drm_device *dev, unsigned long chipset)
{
	return 0;
}

static int mali_drm_unload(struct drm_device *dev)
{
	return 0;
}

static struct file_operations mali_fops = {
	 .owner = THIS_MODULE,
	 .open = drm_open,
	 .release = drm_release,
	 .unlocked_ioctl = drm_ioctl,
	 .mmap = drm_mmap,
	 .poll = drm_poll,
	 .fasync = drm_fasync,
};

static struct drm_driver driver =
{
	.driver_features = DRIVER_BUS_PLATFORM,
	.load = mali_drm_load,
	.unload = mali_drm_unload,
	.context_dtor = NULL,
	.reclaim_buffers = NULL,
	.reclaim_buffers_idlelocked = NULL,
	.preclose = mali_drm_preclose,
	.lastclose = mali_drm_lastclose,
	.suspend = mali_drm_suspend,
	.resume = mali_drm_resume,
	.ioctls = NULL,
	.fops = &mali_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static struct drm_driver driver1 =
{
	.driver_features = DRIVER_BUS_PLATFORM,
	.load = mali_drm_load,
	.unload = mali_drm_unload,
	.context_dtor = NULL,
	.reclaim_buffers = NULL,
	.reclaim_buffers_idlelocked = NULL,
	.preclose = mali_drm_preclose,
	.lastclose = mali_drm_lastclose,
	.suspend = mali_drm_suspend,
	.resume = mali_drm_resume,
	.ioctls = NULL,
	.fops = &mali_fops,
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

int mali_drm_init(struct platform_device *dev)
{
	printk(KERN_INFO "Mali DRM initialize, driver name: %s, version %d.%d\n", DRIVER_NAME, DRIVER_MAJOR, DRIVER_MINOR);
	if (dev == dev0) {
		driver.num_ioctls = 0;
		driver.kdriver.platform_device = dev;
		return drm_platform_init(&driver, dev);
	} else if (dev == dev1) {
		driver1.num_ioctls = 0;
		driver1.kdriver.platform_device = dev;
		return drm_platform_init(&driver1, dev);
	}
	return 0;
}

void mali_drm_exit(struct platform_device *dev)
{
	if (driver.kdriver.platform_device == dev) {
		drm_platform_exit(&driver, dev);
	} else if (driver1.kdriver.platform_device == dev) {
		drm_platform_exit(&driver1, dev);
	}
}

static int __devinit mali_platform_drm_probe(struct platform_device *dev)
{
	return mali_drm_init(dev);
}

static int mali_platform_drm_remove(struct platform_device *dev)
{
	mali_drm_exit(dev);

	return 0;
}

static int mali_platform_drm_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int mali_platform_drm_resume(struct platform_device *dev)
{
	return 0;
}


static struct platform_driver platform_drm_driver = {
	.probe = mali_platform_drm_probe,
	.remove = __devexit_p(mali_platform_drm_remove),
	.suspend = mali_platform_drm_suspend,
	.resume = mali_platform_drm_resume,
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
	},
};

static int __init mali_platform_drm_init(void)
{
	dev0 = platform_device_register_simple("mali_drm", 0, NULL, 0);
	dev1 = platform_device_register_simple("mali_drm", 1, NULL, 0);
	return platform_driver_register( &platform_drm_driver );
}

static void __exit mali_platform_drm_exit(void)
{
	platform_driver_unregister( &platform_drm_driver );
	platform_device_unregister(dev0);
	platform_device_unregister(dev1);
}

#ifdef MODULE
module_init(mali_platform_drm_init);
#else
late_initcall(mali_platform_drm_init);
#endif
module_exit(mali_platform_drm_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_ALIAS(DRIVER_ALIAS);
MODULE_INFO(vermagic, VERMAGIC_STRING);
