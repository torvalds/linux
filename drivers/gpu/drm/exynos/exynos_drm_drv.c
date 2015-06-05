/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pm_runtime.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>

#include <linux/component.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_plane.h"
#include "exynos_drm_vidi.h"
#include "exynos_drm_dmabuf.h"
#include "exynos_drm_g2d.h"
#include "exynos_drm_ipp.h"
#include "exynos_drm_iommu.h"

#define DRIVER_NAME	"exynos"
#define DRIVER_DESC	"Samsung SoC DRM"
#define DRIVER_DATE	"20110530"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

static struct platform_device *exynos_drm_pdev;

static DEFINE_MUTEX(drm_component_lock);
static LIST_HEAD(drm_component_list);

struct component_dev {
	struct list_head list;
	struct device *crtc_dev;
	struct device *conn_dev;
	enum exynos_drm_output_type out_type;
	unsigned int dev_type_flag;
};

static int exynos_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct exynos_drm_private *private;
	int ret;

	private = kzalloc(sizeof(struct exynos_drm_private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	dev_set_drvdata(dev->dev, dev);
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
		goto err_free_private;
	}

	drm_mode_config_init(dev);

	exynos_drm_mode_config_init(dev);

	/* setup possible_clones. */
	exynos_drm_encoder_setup(dev);

	platform_set_drvdata(dev->platformdev, dev);

	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev->dev, dev);
	if (ret)
		goto err_mode_config_cleanup;

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	/* Probe non kms sub drivers and virtual display driver. */
	ret = exynos_drm_device_subdrv_probe(dev);
	if (ret)
		goto err_cleanup_vblank;

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	dev->irq_enabled = true;

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	dev->vblank_disable_allowed = true;

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(dev);

	/* force connectors detection */
	drm_helper_hpd_irq_event(dev);

	return 0;

err_cleanup_vblank:
	drm_vblank_cleanup(dev);
err_unbind_all:
	component_unbind_all(dev->dev, dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(dev);
	drm_release_iommu_mapping(dev);
err_free_private:
	kfree(private);

	return ret;
}

static int exynos_drm_unload(struct drm_device *dev)
{
	exynos_drm_device_subdrv_remove(dev);

	exynos_drm_fbdev_fini(dev);
	drm_kms_helper_poll_fini(dev);

	drm_vblank_cleanup(dev);
	component_unbind_all(dev->dev, dev);
	drm_mode_config_cleanup(dev);
	drm_release_iommu_mapping(dev);

	kfree(dev->dev_private);
	dev->dev_private = NULL;

	return 0;
}

static int exynos_drm_suspend(struct drm_device *dev, pm_message_t state)
{
	struct drm_connector *connector;

	drm_modeset_lock_all(dev);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		int old_dpms = connector->dpms;

		if (connector->funcs->dpms)
			connector->funcs->dpms(connector, DRM_MODE_DPMS_OFF);

		/* Set the old mode back to the connector for resume */
		connector->dpms = old_dpms;
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

static int exynos_drm_resume(struct drm_device *dev)
{
	struct drm_connector *connector;

	drm_modeset_lock_all(dev);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->funcs->dpms) {
			int dpms = connector->dpms;

			connector->dpms = DRM_MODE_DPMS_OFF;
			connector->funcs->dpms(connector, dpms);
		}
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

static int exynos_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv;
	int ret;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;

	ret = exynos_drm_subdrv_open(dev, file);
	if (ret)
		goto err_file_priv_free;

	return ret;

err_file_priv_free:
	kfree(file_priv);
	file->driver_priv = NULL;
	return ret;
}

static void exynos_drm_preclose(struct drm_device *dev,
					struct drm_file *file)
{
	exynos_drm_subdrv_close(dev, file);
}

static void exynos_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_pending_event *e, *et;
	unsigned long flags;

	if (!file->driver_priv)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	/* Release all events handled by page flip handler but not freed. */
	list_for_each_entry_safe(e, et, &file->event_list, link) {
		list_del(&e->link);
		e->destroy(e);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static void exynos_drm_lastclose(struct drm_device *dev)
{
	exynos_drm_fbdev_restore_mode(dev);
}

static const struct vm_operations_struct exynos_drm_gem_vm_ops = {
	.fault = exynos_drm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_ioctl_desc exynos_ioctls[] = {
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_CREATE, exynos_drm_gem_create_ioctl,
			DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_GET,
			exynos_drm_gem_get_ioctl, DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(EXYNOS_VIDI_CONNECTION,
			vidi_connection_ioctl, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_GET_VER,
			exynos_g2d_get_ver_ioctl, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_SET_CMDLIST,
			exynos_g2d_set_cmdlist_ioctl, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_EXEC,
			exynos_g2d_exec_ioctl, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_GET_PROPERTY,
			exynos_drm_ipp_get_property, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_SET_PROPERTY,
			exynos_drm_ipp_set_property, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_QUEUE_BUF,
			exynos_drm_ipp_queue_buf, DRM_UNLOCKED | DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_CMD_CTRL,
			exynos_drm_ipp_cmd_ctrl, DRM_UNLOCKED | DRM_AUTH),
};

static const struct file_operations exynos_drm_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= exynos_drm_gem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release	= drm_release,
};

static struct drm_driver exynos_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME,
	.load			= exynos_drm_load,
	.unload			= exynos_drm_unload,
	.suspend		= exynos_drm_suspend,
	.resume			= exynos_drm_resume,
	.open			= exynos_drm_open,
	.preclose		= exynos_drm_preclose,
	.lastclose		= exynos_drm_lastclose,
	.postclose		= exynos_drm_postclose,
	.set_busid		= drm_platform_set_busid,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= exynos_drm_crtc_enable_vblank,
	.disable_vblank		= exynos_drm_crtc_disable_vblank,
	.gem_free_object	= exynos_drm_gem_free_object,
	.gem_vm_ops		= &exynos_drm_gem_vm_ops,
	.dumb_create		= exynos_drm_gem_dumb_create,
	.dumb_map_offset	= exynos_drm_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= exynos_dmabuf_prime_export,
	.gem_prime_import	= exynos_dmabuf_prime_import,
	.ioctls			= exynos_ioctls,
	.num_ioctls		= ARRAY_SIZE(exynos_ioctls),
	.fops			= &exynos_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static int exynos_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	pm_message_t message;

	if (pm_runtime_suspended(dev) || !drm_dev)
		return 0;

	message.event = PM_EVENT_SUSPEND;
	return exynos_drm_suspend(drm_dev, message);
}

static int exynos_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	if (pm_runtime_suspended(dev) || !drm_dev)
		return 0;

	return exynos_drm_resume(drm_dev);
}
#endif

static const struct dev_pm_ops exynos_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_drm_sys_suspend, exynos_drm_sys_resume)
};

int exynos_drm_component_add(struct device *dev,
				enum exynos_drm_device_type dev_type,
				enum exynos_drm_output_type out_type)
{
	struct component_dev *cdev;

	if (dev_type != EXYNOS_DEVICE_TYPE_CRTC &&
			dev_type != EXYNOS_DEVICE_TYPE_CONNECTOR) {
		DRM_ERROR("invalid device type.\n");
		return -EINVAL;
	}

	mutex_lock(&drm_component_lock);

	/*
	 * Make sure to check if there is a component which has two device
	 * objects, for connector and for encoder/connector.
	 * It should make sure that crtc and encoder/connector drivers are
	 * ready before exynos drm core binds them.
	 */
	list_for_each_entry(cdev, &drm_component_list, list) {
		if (cdev->out_type == out_type) {
			/*
			 * If crtc and encoder/connector device objects are
			 * added already just return.
			 */
			if (cdev->dev_type_flag == (EXYNOS_DEVICE_TYPE_CRTC |
						EXYNOS_DEVICE_TYPE_CONNECTOR)) {
				mutex_unlock(&drm_component_lock);
				return 0;
			}

			if (dev_type == EXYNOS_DEVICE_TYPE_CRTC) {
				cdev->crtc_dev = dev;
				cdev->dev_type_flag |= dev_type;
			}

			if (dev_type == EXYNOS_DEVICE_TYPE_CONNECTOR) {
				cdev->conn_dev = dev;
				cdev->dev_type_flag |= dev_type;
			}

			mutex_unlock(&drm_component_lock);
			return 0;
		}
	}

	mutex_unlock(&drm_component_lock);

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	if (dev_type == EXYNOS_DEVICE_TYPE_CRTC)
		cdev->crtc_dev = dev;
	if (dev_type == EXYNOS_DEVICE_TYPE_CONNECTOR)
		cdev->conn_dev = dev;

	cdev->out_type = out_type;
	cdev->dev_type_flag = dev_type;

	mutex_lock(&drm_component_lock);
	list_add_tail(&cdev->list, &drm_component_list);
	mutex_unlock(&drm_component_lock);

	return 0;
}

void exynos_drm_component_del(struct device *dev,
				enum exynos_drm_device_type dev_type)
{
	struct component_dev *cdev, *next;

	mutex_lock(&drm_component_lock);

	list_for_each_entry_safe(cdev, next, &drm_component_list, list) {
		if (dev_type == EXYNOS_DEVICE_TYPE_CRTC) {
			if (cdev->crtc_dev == dev) {
				cdev->crtc_dev = NULL;
				cdev->dev_type_flag &= ~dev_type;
			}
		}

		if (dev_type == EXYNOS_DEVICE_TYPE_CONNECTOR) {
			if (cdev->conn_dev == dev) {
				cdev->conn_dev = NULL;
				cdev->dev_type_flag &= ~dev_type;
			}
		}

		/*
		 * Release cdev object only in case that both of crtc and
		 * encoder/connector device objects are NULL.
		 */
		if (!cdev->crtc_dev && !cdev->conn_dev) {
			list_del(&cdev->list);
			kfree(cdev);
		}
	}

	mutex_unlock(&drm_component_lock);
}

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static struct component_match *exynos_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	struct component_dev *cdev;
	unsigned int attach_cnt = 0;

	mutex_lock(&drm_component_lock);

	/* Do not retry to probe if there is no any kms driver regitered. */
	if (list_empty(&drm_component_list)) {
		mutex_unlock(&drm_component_lock);
		return ERR_PTR(-ENODEV);
	}

	list_for_each_entry(cdev, &drm_component_list, list) {
		/*
		 * Add components to master only in case that crtc and
		 * encoder/connector device objects exist.
		 */
		if (!cdev->crtc_dev || !cdev->conn_dev)
			continue;

		attach_cnt++;

		mutex_unlock(&drm_component_lock);

		/*
		 * fimd and dpi modules have same device object so add
		 * only crtc device object in this case.
		 */
		if (cdev->crtc_dev == cdev->conn_dev) {
			component_match_add(dev, &match, compare_dev,
						cdev->crtc_dev);
			goto out_lock;
		}

		/*
		 * Do not chage below call order.
		 * crtc device first should be added to master because
		 * connector/encoder need pipe number of crtc when they
		 * are created.
		 */
		component_match_add(dev, &match, compare_dev, cdev->crtc_dev);
		component_match_add(dev, &match, compare_dev, cdev->conn_dev);

out_lock:
		mutex_lock(&drm_component_lock);
	}

	mutex_unlock(&drm_component_lock);

	return attach_cnt ? match : ERR_PTR(-EPROBE_DEFER);
}

static int exynos_drm_bind(struct device *dev)
{
	return drm_platform_init(&exynos_drm_driver, to_platform_device(dev));
}

static void exynos_drm_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops exynos_drm_ops = {
	.bind		= exynos_drm_bind,
	.unbind		= exynos_drm_unbind,
};

static struct platform_driver *const exynos_drm_kms_drivers[] = {
#ifdef CONFIG_DRM_EXYNOS_FIMD
	&fimd_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS7_DECON
	&decon_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_DP
	&dp_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_DSI
	&dsi_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_HDMI
	&mixer_driver,
	&hdmi_driver,
#endif
};

static struct platform_driver *const exynos_drm_non_kms_drivers[] = {
#ifdef CONFIG_DRM_EXYNOS_G2D
	&g2d_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_FIMC
	&fimc_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_ROTATOR
	&rotator_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_GSC
	&gsc_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_IPP
	&ipp_driver,
#endif
};

static int exynos_drm_platform_probe(struct platform_device *pdev)
{
	struct component_match *match;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	exynos_drm_driver.num_ioctls = ARRAY_SIZE(exynos_ioctls);

	match = exynos_drm_match_add(&pdev->dev);
	if (IS_ERR(match)) {
		return PTR_ERR(match);
	}

	return component_master_add_with_match(&pdev->dev, &exynos_drm_ops,
					       match);
}

static int exynos_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &exynos_drm_ops);
	return 0;
}

static const char * const strings[] = {
	"samsung,exynos3",
	"samsung,exynos4",
	"samsung,exynos5",
	"samsung,exynos7",
};

static struct platform_driver exynos_drm_platform_driver = {
	.probe	= exynos_drm_platform_probe,
	.remove	= exynos_drm_platform_remove,
	.driver	= {
		.name	= "exynos-drm",
		.pm	= &exynos_drm_pm_ops,
	},
};

static int exynos_drm_init(void)
{
	bool is_exynos = false;
	int ret, i, j;

	/*
	 * Register device object only in case of Exynos SoC.
	 *
	 * Below codes resolves temporarily infinite loop issue incurred
	 * by Exynos drm driver when using multi-platform kernel.
	 * So these codes will be replaced with more generic way later.
	 */
	for (i = 0; i < ARRAY_SIZE(strings); i++) {
		if (of_machine_is_compatible(strings[i])) {
			is_exynos = true;
			break;
		}
	}

	if (!is_exynos)
		return -ENODEV;

	exynos_drm_pdev = platform_device_register_simple("exynos-drm", -1,
								NULL, 0);
	if (IS_ERR(exynos_drm_pdev))
		return PTR_ERR(exynos_drm_pdev);

	ret = exynos_drm_probe_vidi();
	if (ret < 0)
		goto err_unregister_pd;

	for (i = 0; i < ARRAY_SIZE(exynos_drm_kms_drivers); ++i) {
		ret = platform_driver_register(exynos_drm_kms_drivers[i]);
		if (ret < 0)
			goto err_unregister_kms_drivers;
	}

	for (j = 0; j < ARRAY_SIZE(exynos_drm_non_kms_drivers); ++j) {
		ret = platform_driver_register(exynos_drm_non_kms_drivers[j]);
		if (ret < 0)
			goto err_unregister_non_kms_drivers;
	}

#ifdef CONFIG_DRM_EXYNOS_IPP
	ret = exynos_platform_device_ipp_register();
	if (ret < 0)
		goto err_unregister_non_kms_drivers;
#endif

	ret = platform_driver_register(&exynos_drm_platform_driver);
	if (ret)
		goto err_unregister_resources;

	return 0;

err_unregister_resources:
#ifdef CONFIG_DRM_EXYNOS_IPP
	exynos_platform_device_ipp_unregister();
#endif

err_unregister_non_kms_drivers:
	while (--j >= 0)
		platform_driver_unregister(exynos_drm_non_kms_drivers[j]);

err_unregister_kms_drivers:
	while (--i >= 0)
		platform_driver_unregister(exynos_drm_kms_drivers[i]);

	exynos_drm_remove_vidi();

err_unregister_pd:
	platform_device_unregister(exynos_drm_pdev);

	return ret;
}

static void exynos_drm_exit(void)
{
	int i;

#ifdef CONFIG_DRM_EXYNOS_IPP
	exynos_platform_device_ipp_unregister();
#endif

	for (i = ARRAY_SIZE(exynos_drm_non_kms_drivers) - 1; i >= 0; --i)
		platform_driver_unregister(exynos_drm_non_kms_drivers[i]);

	for (i = ARRAY_SIZE(exynos_drm_kms_drivers) - 1; i >= 0; --i)
		platform_driver_unregister(exynos_drm_kms_drivers[i]);

	platform_driver_unregister(&exynos_drm_platform_driver);

	exynos_drm_remove_vidi();

	platform_device_unregister(exynos_drm_pdev);
}

module_init(exynos_drm_init);
module_exit(exynos_drm_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Seung-Woo Kim <sw0312.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Driver");
MODULE_LICENSE("GPL");
