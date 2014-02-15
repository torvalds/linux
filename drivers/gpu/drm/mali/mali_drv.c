/*
 * Copyright (C) 2010, 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/vermagic.h>
#include <linux/version.h>
#include "drmP.h"
#include "mali_drm.h"
#include "mali_drv.h"

static struct platform_device *pdev;

static int mali_platform_drm_probe(struct platform_device *pdev)
{
	return 0;
}

static int mali_platform_drm_remove(struct platform_device *pdev)
{
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


static char mali_drm_device_name[] = "mali_drm";
static struct platform_driver platform_drm_driver =
{
	.probe = mali_platform_drm_probe,
	.remove = mali_platform_drm_remove,
	.suspend = mali_platform_drm_suspend,
	.resume = mali_platform_drm_resume,
	.driver = {
		.name = mali_drm_device_name,
		.owner = THIS_MODULE,
	},
};

#if 0
static const struct drm_device_id dock_device_ids[] =
{
	{"MALIDRM", 0},
	{"", 0},
};
#endif

static int mali_driver_load(struct drm_device *dev, unsigned long chipset)
{
	int ret;
	unsigned long base, size;
	drm_mali_private_t *dev_priv;
	printk(KERN_ERR "DRM: mali_driver_load start\n");

	dev_priv = drm_calloc(1, sizeof(drm_mali_private_t), DRM_MEM_DRIVER);

	if (dev_priv == NULL)
	{
		return -ENOMEM;
	}

	dev->dev_private = (void *)dev_priv;

	if (NULL == dev->platformdev)
	{
		dev->platformdev = platform_device_register_simple(mali_drm_device_name, 0, NULL, 0);
		pdev = dev->platformdev;
	}

#if 0
	base = drm_get_resource_start(dev, 1);
	size = drm_get_resource_len(dev, 1);
#endif
	ret = drm_sman_init(&dev_priv->sman, 2, 12, 8);

	if (ret)
	{
		drm_free(dev_priv, sizeof(dev_priv), DRM_MEM_DRIVER);
	}

	//if ( ret ) kfree( dev_priv );

	printk(KERN_ERR "DRM: mali_driver_load done\n");

	return ret;
}

static int mali_driver_unload(struct drm_device *dev)
{
	drm_mali_private_t *dev_priv = dev->dev_private;
	printk(KERN_ERR "DRM: mali_driver_unload start\n");

	drm_sman_takedown(&dev_priv->sman);
	drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);
	//kfree( dev_priv );
	printk(KERN_ERR "DRM: mali_driver_unload done\n");

	return 0;
}

static struct drm_driver driver =
{
	.driver_features = DRIVER_USE_PLATFORM_DEVICE,
	.load = mali_driver_load,
	.unload = mali_driver_unload,
	.context_dtor = NULL,
	.dma_quiescent = mali_idle,
	.reclaim_buffers = NULL,
	.reclaim_buffers_idlelocked = mali_reclaim_buffers_locked,
	.lastclose = mali_lastclose,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = mali_ioctls,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
		.ioctl = drm_ioctl,
#else
		.unlocked_ioctl = drm_ioctl,
#endif
		.mmap = drm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
	},
	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init mali_init(void)
{
	driver.num_ioctls = mali_max_ioctl;
	return drm_init(&driver);
}

static void __exit mali_exit(void)
{
	platform_device_unregister(pdev);
	drm_exit(&driver);
}

module_init(mali_init);
module_exit(mali_exit);

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_AUTHOR("ARM Ltd.");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
