/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 * 
 * based on exynos_drm_drv.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include <drm/rockchip_drm.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_crtc.h"
#include "rockchip_drm_encoder.h"
#include "rockchip_drm_fbdev.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"
#include "rockchip_drm_plane.h"
#include "rockchip_drm_dmabuf.h"
#include "rockchip_drm_iommu.h"

#define DRIVER_NAME	"rockchip"
#define DRIVER_DESC	"rockchip Soc DRM"
#define DRIVER_DATE	"20140318"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

#define VBLANK_OFF_DELAY	50000

/* platform device pointer for eynos drm device. */
static struct platform_device *rockchip_drm_pdev;

static int rockchip_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct rockchip_drm_private *private;
	int ret;
	int nr;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	private = kzalloc(sizeof(struct rockchip_drm_private), GFP_KERNEL);
	if (!private) {
		DRM_ERROR("failed to allocate private\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&private->pageflip_event_list);
	dev->dev_private = (void *)private;

	/*
	 * create mapping to manage iommu table and set a pointer to iommu
	 * mapping structure to iommu_mapping of private data.
	 * also this iommu_mapping can be used to check if iommu is supported
	 * or not.
	 */
	ret = drm_create_iommu_mapping(dev);
	if (ret < 0) {
		DRM_ERROR("failed to create iommu mapping.\n");
		goto err_crtc;
	}

	drm_mode_config_init(dev);

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(dev);

	rockchip_drm_mode_config_init(dev);

	/*
	 * ROCKCHIP4 is enough to have two CRTCs and each crtc would be used
	 * without dependency of hardware.
	 */
	for (nr = 0; nr < MAX_CRTC; nr++) {
		ret = rockchip_drm_crtc_create(dev, nr);
		if (ret)
			goto err_release_iommu_mapping;
	}

	for (nr = 0; nr < MAX_PLANE; nr++) {
		struct drm_plane *plane;
		unsigned int possible_crtcs = (1 << MAX_CRTC) - 1;

		plane = rockchip_plane_init(dev, possible_crtcs, false);
		if (!plane)
			goto err_release_iommu_mapping;
	}

	ret = drm_vblank_init(dev, MAX_CRTC);
	if (ret)
		goto err_release_iommu_mapping;

	/*
	 * probe sub drivers such as display controller and hdmi driver,
	 * that were registered at probe() of platform driver
	 * to the sub driver and create encoder and connector for them.
	 */
	ret = rockchip_drm_device_register(dev);
	if (ret)
		goto err_vblank;

	/* setup possible_clones. */
	rockchip_drm_encoder_setup(dev);

	/*
	 * create and configure fb helper and also rockchip specific
	 * fbdev object.
	 */
	ret = rockchip_drm_fbdev_init(dev);
	if (ret) {
		DRM_ERROR("failed to initialize drm fbdev\n");
		goto err_drm_device;
	}

	drm_vblank_offdelay = VBLANK_OFF_DELAY;

	return 0;

err_drm_device:
	rockchip_drm_device_unregister(dev);
err_vblank:
	drm_vblank_cleanup(dev);
err_release_iommu_mapping:
	drm_release_iommu_mapping(dev);
err_crtc:
	drm_mode_config_cleanup(dev);
	kfree(private);

	return ret;
}

static int rockchip_drm_unload(struct drm_device *dev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	rockchip_drm_fbdev_fini(dev);
	rockchip_drm_device_unregister(dev);
	drm_vblank_cleanup(dev);
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);

	drm_release_iommu_mapping(dev);
	kfree(dev->dev_private);

	dev->dev_private = NULL;

	return 0;
}

static int rockchip_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_rockchip_file_private *file_priv;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;

	return rockchip_drm_subdrv_open(dev, file);
}

static void rockchip_drm_preclose(struct drm_device *dev,
					struct drm_file *file)
{
	struct rockchip_drm_private *private = dev->dev_private;
	struct drm_pending_vblank_event *e, *t;
	unsigned long flags;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	/* release events of current file */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_for_each_entry_safe(e, t, &private->pageflip_event_list,
			base.link) {
		if (e->base.file_priv == file) {
			list_del(&e->base.link);
			e->base.destroy(&e->base);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	rockchip_drm_subdrv_close(dev, file);
}

static void rockchip_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	if (!file->driver_priv)
		return;

	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static void rockchip_drm_lastclose(struct drm_device *dev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	rockchip_drm_fbdev_restore_mode(dev);
}

static const struct vm_operations_struct rockchip_drm_gem_vm_ops = {
	.fault = rockchip_drm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static struct drm_ioctl_desc rockchip_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_CREATE, rockchip_drm_gem_create_ioctl,
			DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MAP_OFFSET,
			rockchip_drm_gem_map_offset_ioctl, DRM_UNLOCKED |
			DRM_AUTH),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_MMAP,
			rockchip_drm_gem_mmap_ioctl, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(ROCKCHIP_GEM_GET,
			rockchip_drm_gem_get_ioctl, DRM_UNLOCKED),
};

static const struct file_operations rockchip_drm_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= rockchip_drm_gem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release	= drm_release,
};

static struct drm_driver rockchip_drm_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_MODESET |
					DRIVER_GEM | DRIVER_PRIME,
	.load			= rockchip_drm_load,
	.unload			= rockchip_drm_unload,
	.open			= rockchip_drm_open,
	.preclose		= rockchip_drm_preclose,
	.lastclose		= rockchip_drm_lastclose,
	.postclose		= rockchip_drm_postclose,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= rockchip_drm_crtc_enable_vblank,
	.disable_vblank		= rockchip_drm_crtc_disable_vblank,
//	.get_vblank_timestamp   = rockchip_get_crtc_vblank_timestamp,
	.gem_init_object	= rockchip_drm_gem_init_object,
	.gem_free_object	= rockchip_drm_gem_free_object,
	.gem_vm_ops		= &rockchip_drm_gem_vm_ops,
	.dumb_create		= rockchip_drm_gem_dumb_create,
	.dumb_map_offset	= rockchip_drm_gem_dumb_map_offset,
	.dumb_destroy		= rockchip_drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= rockchip_dmabuf_prime_export,
	.gem_prime_import	= rockchip_dmabuf_prime_import,
	.ioctls			= rockchip_ioctls,
	.fops			= &rockchip_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

static int rockchip_drm_platform_probe(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	rockchip_drm_driver.num_ioctls = DRM_ARRAY_SIZE(rockchip_ioctls);

	return drm_platform_init(&rockchip_drm_driver, pdev);
}

static int rockchip_drm_platform_remove(struct platform_device *pdev)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	drm_platform_exit(&rockchip_drm_driver, pdev);

	return 0;
}

static struct platform_driver rockchip_drm_platform_driver = {
	.probe		= rockchip_drm_platform_probe,
	.remove		= rockchip_drm_platform_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "rockchip-drm",
	},
};

static int __init rockchip_drm_init(void)
{
	int ret;

	DRM_DEBUG_DRIVER("%s\n", __FILE__);


#ifdef CONFIG_DRM_ROCKCHIP_PRIMARY
	ret = platform_driver_register(&primary_platform_driver);
	if (ret < 0)
		goto out_primary;
	platform_device_register_simple("primary-display", -1,
			NULL, 0);
#endif
#ifdef CONFIG_DRM_ROCKCHIP_HDMI
	ret = platform_driver_register(&extend_platform_driver);
	if (ret < 0)
		goto out_extend;
	platform_device_register_simple("extend-display", -1,
			NULL, 0);
#endif

	ret = platform_driver_register(&rockchip_drm_platform_driver);
	if (ret < 0)
		goto out_drm;


	rockchip_drm_pdev = platform_device_register_simple("rockchip-drm", -1,
				NULL, 0);
	if (IS_ERR(rockchip_drm_pdev)) {
		ret = PTR_ERR(rockchip_drm_pdev);
		goto out;
	}

	return 0;

out:
	platform_driver_unregister(&rockchip_drm_platform_driver);
out_drm:
#ifdef CONFIG_DRM_ROCKCHIP_PRIMARY
	platform_driver_unregister(&primary_platform_driver);
out_primary:
#endif
#ifdef CONFIG_DRM_ROCKCHIP_HDMI
	platform_driver_unregister(&extend_platform_driver);
out_extend:
#endif
	return ret;
}

static void __exit rockchip_drm_exit(void)
{
	DRM_DEBUG_DRIVER("%s\n", __FILE__);

	platform_device_unregister(rockchip_drm_pdev);

	platform_driver_unregister(&rockchip_drm_platform_driver);
}

module_init(rockchip_drm_init);
module_exit(rockchip_drm_exit);
