/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include <drm/armada_drm.h>
#include "armada_ioctlP.h"

#ifdef CONFIG_DRM_ARMADA_TDA1998X
#include <drm/i2c/tda998x.h>
#include "armada_slave.h"

static struct tda998x_encoder_params params = {
	/* With 0x24, there is no translation between vp_out and int_vp
	FB	LCD out	Pins	VIP	Int Vp
	R:23:16	R:7:0	VPC7:0	7:0	7:0[R]
	G:15:8	G:15:8	VPB7:0	23:16	23:16[G]
	B:7:0	B:23:16	VPA7:0	15:8	15:8[B]
	*/
	.swap_a = 2,
	.swap_b = 3,
	.swap_c = 4,
	.swap_d = 5,
	.swap_e = 0,
	.swap_f = 1,
	.audio_cfg = BIT(2),
	.audio_frame[1] = 1,
	.audio_format = AFMT_SPDIF,
	.audio_sample_rate = 44100,
};

static const struct armada_drm_slave_config tda19988_config = {
	.i2c_adapter_id = 0,
	.crtcs = 1 << 0, /* Only LCD0 at the moment */
	.polled = DRM_CONNECTOR_POLL_CONNECT | DRM_CONNECTOR_POLL_DISCONNECT,
	.interlace_allowed = true,
	.info = {
		.type = "tda998x",
		.addr = 0x70,
		.platform_data = &params,
	},
};
#endif

static void armada_drm_unref_work(struct work_struct *work)
{
	struct armada_private *priv =
		container_of(work, struct armada_private, fb_unref_work);
	struct drm_framebuffer *fb;

	while (kfifo_get(&priv->fb_unref, &fb))
		drm_framebuffer_unreference(fb);
}

/* Must be called with dev->event_lock held */
void __armada_drm_queue_unref_work(struct drm_device *dev,
	struct drm_framebuffer *fb)
{
	struct armada_private *priv = dev->dev_private;

	/*
	 * Yes, we really must jump through these hoops just to store a
	 * _pointer_ to something into the kfifo.  This is utterly insane
	 * and idiotic, because it kfifo requires the _data_ pointed to by
	 * the pointer const, not the pointer itself.  Not only that, but
	 * you have to pass a pointer _to_ the pointer you want stored.
	 */
	const struct drm_framebuffer *silly_api_alert = fb;
	WARN_ON(!kfifo_put(&priv->fb_unref, &silly_api_alert));
	schedule_work(&priv->fb_unref_work);
}

void armada_drm_queue_unref_work(struct drm_device *dev,
	struct drm_framebuffer *fb)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	__armada_drm_queue_unref_work(dev, fb);
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static int armada_drm_load(struct drm_device *dev, unsigned long flags)
{
	const struct platform_device_id *id;
	struct armada_private *priv;
	struct resource *res[ARRAY_SIZE(priv->dcrtc)];
	struct resource *mem = NULL;
	int ret, n, i;

	memset(res, 0, sizeof(res));

	for (n = i = 0; ; n++) {
		struct resource *r = platform_get_resource(dev->platformdev,
							   IORESOURCE_MEM, n);
		if (!r)
			break;

		/* Resources above 64K are graphics memory */
		if (resource_size(r) > SZ_64K)
			mem = r;
		else if (i < ARRAY_SIZE(priv->dcrtc))
			res[i++] = r;
		else
			return -EINVAL;
	}

	if (!res[0] || !mem)
		return -ENXIO;

	if (!devm_request_mem_region(dev->dev, mem->start,
			resource_size(mem), "armada-drm"))
		return -EBUSY;

	priv = devm_kzalloc(dev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		DRM_ERROR("failed to allocate private\n");
		return -ENOMEM;
	}

	dev->dev_private = priv;

	/* Get the implementation specific driver data. */
	id = platform_get_device_id(dev->platformdev);
	if (!id)
		return -ENXIO;

	priv->variant = (struct armada_variant *)id->driver_data;

	ret = priv->variant->init(priv, dev->dev);
	if (ret)
		return ret;

	INIT_WORK(&priv->fb_unref_work, armada_drm_unref_work);
	INIT_KFIFO(priv->fb_unref);

	/* Mode setting support */
	drm_mode_config_init(dev);
	dev->mode_config.min_width = 320;
	dev->mode_config.min_height = 200;

	/*
	 * With vscale enabled, the maximum width is 1920 due to the
	 * 1920 by 3 lines RAM
	 */
	dev->mode_config.max_width = 1920;
	dev->mode_config.max_height = 2048;

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.funcs = &armada_drm_mode_config_funcs;
	drm_mm_init(&priv->linear, mem->start, resource_size(mem));

	/* Create all LCD controllers */
	for (n = 0; n < ARRAY_SIZE(priv->dcrtc); n++) {
		if (!res[n])
			break;

		ret = armada_drm_crtc_create(dev, n, res[n]);
		if (ret)
			goto err_kms;
	}

#ifdef CONFIG_DRM_ARMADA_TDA1998X
	ret = armada_drm_connector_slave_create(dev, &tda19988_config);
	if (ret)
		goto err_kms;
#endif

	ret = drm_vblank_init(dev, n);
	if (ret)
		goto err_kms;

	ret = drm_irq_install(dev);
	if (ret)
		goto err_kms;

	dev->vblank_disable_allowed = 1;

	ret = armada_fbdev_init(dev);
	if (ret)
		goto err_irq;

	drm_kms_helper_poll_init(dev);

	return 0;

 err_irq:
	drm_irq_uninstall(dev);
 err_kms:
	drm_mode_config_cleanup(dev);
	drm_mm_takedown(&priv->linear);
	flush_work(&priv->fb_unref_work);

	return ret;
}

static int armada_drm_unload(struct drm_device *dev)
{
	struct armada_private *priv = dev->dev_private;

	drm_kms_helper_poll_fini(dev);
	armada_fbdev_fini(dev);
	drm_irq_uninstall(dev);
	drm_mode_config_cleanup(dev);
	drm_mm_takedown(&priv->linear);
	flush_work(&priv->fb_unref_work);
	dev->dev_private = NULL;

	return 0;
}

void armada_drm_vbl_event_add(struct armada_crtc *dcrtc,
	struct armada_vbl_event *evt)
{
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	if (list_empty(&evt->node)) {
		list_add_tail(&evt->node, &dcrtc->vbl_list);

		drm_vblank_get(dcrtc->crtc.dev, dcrtc->num);
	}
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

void armada_drm_vbl_event_remove(struct armada_crtc *dcrtc,
	struct armada_vbl_event *evt)
{
	if (!list_empty(&evt->node)) {
		list_del_init(&evt->node);
		drm_vblank_put(dcrtc->crtc.dev, dcrtc->num);
	}
}

void armada_drm_vbl_event_remove_unlocked(struct armada_crtc *dcrtc,
	struct armada_vbl_event *evt)
{
	unsigned long flags;

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_vbl_event_remove(dcrtc, evt);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

/* These are called under the vbl_lock. */
static int armada_drm_enable_vblank(struct drm_device *dev, int crtc)
{
	struct armada_private *priv = dev->dev_private;
	armada_drm_crtc_enable_irq(priv->dcrtc[crtc], VSYNC_IRQ_ENA);
	return 0;
}

static void armada_drm_disable_vblank(struct drm_device *dev, int crtc)
{
	struct armada_private *priv = dev->dev_private;
	armada_drm_crtc_disable_irq(priv->dcrtc[crtc], VSYNC_IRQ_ENA);
}

static irqreturn_t armada_drm_irq_handler(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct armada_private *priv = dev->dev_private;
	struct armada_crtc *dcrtc = priv->dcrtc[0];
	uint32_t v, stat = readl_relaxed(dcrtc->base + LCD_SPU_IRQ_ISR);
	irqreturn_t handled = IRQ_NONE;

	/*
	 * This is rediculous - rather than writing bits to clear, we
	 * have to set the actual status register value.  This is racy.
	 */
	writel_relaxed(0, dcrtc->base + LCD_SPU_IRQ_ISR);

	/* Mask out those interrupts we haven't enabled */
	v = stat & dcrtc->irq_ena;

	if (v & (VSYNC_IRQ|GRA_FRAME_IRQ|DUMB_FRAMEDONE)) {
		armada_drm_crtc_irq(dcrtc, stat);
		handled = IRQ_HANDLED;
	}

	return handled;
}

static int armada_drm_irq_postinstall(struct drm_device *dev)
{
	struct armada_private *priv = dev->dev_private;
	struct armada_crtc *dcrtc = priv->dcrtc[0];

	spin_lock_irq(&dev->vbl_lock);
	writel_relaxed(dcrtc->irq_ena, dcrtc->base + LCD_SPU_IRQ_ENA);
	writel(0, dcrtc->base + LCD_SPU_IRQ_ISR);
	spin_unlock_irq(&dev->vbl_lock);

	return 0;
}

static void armada_drm_irq_uninstall(struct drm_device *dev)
{
	struct armada_private *priv = dev->dev_private;
	struct armada_crtc *dcrtc = priv->dcrtc[0];

	writel(0, dcrtc->base + LCD_SPU_IRQ_ENA);
}

static struct drm_ioctl_desc armada_ioctls[] = {
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_CREATE, armada_gem_create_ioctl,
		DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_MMAP, armada_gem_mmap_ioctl,
		DRM_UNLOCKED),
	DRM_IOCTL_DEF_DRV(ARMADA_GEM_PWRITE, armada_gem_pwrite_ioctl,
		DRM_UNLOCKED),
};

static const struct file_operations armada_drm_fops = {
	.owner			= THIS_MODULE,
	.llseek			= no_llseek,
	.read			= drm_read,
	.poll			= drm_poll,
	.unlocked_ioctl		= drm_ioctl,
	.mmap			= drm_gem_mmap,
	.open			= drm_open,
	.release		= drm_release,
};

static struct drm_driver armada_drm_driver = {
	.load			= armada_drm_load,
	.open			= NULL,
	.preclose		= NULL,
	.postclose		= NULL,
	.lastclose		= NULL,
	.unload			= armada_drm_unload,
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= armada_drm_enable_vblank,
	.disable_vblank		= armada_drm_disable_vblank,
	.irq_handler		= armada_drm_irq_handler,
	.irq_postinstall	= armada_drm_irq_postinstall,
	.irq_uninstall		= armada_drm_irq_uninstall,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init		= armada_drm_debugfs_init,
	.debugfs_cleanup	= armada_drm_debugfs_cleanup,
#endif
	.gem_free_object	= armada_gem_free_object,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= armada_gem_prime_export,
	.gem_prime_import	= armada_gem_prime_import,
	.dumb_create		= armada_gem_dumb_create,
	.dumb_map_offset	= armada_gem_dumb_map_offset,
	.dumb_destroy		= armada_gem_dumb_destroy,
	.gem_vm_ops		= &armada_gem_vm_ops,
	.major			= 1,
	.minor			= 0,
	.name			= "armada-drm",
	.desc			= "Armada SoC DRM",
	.date			= "20120730",
	.driver_features	= DRIVER_GEM | DRIVER_MODESET |
				  DRIVER_HAVE_IRQ | DRIVER_PRIME,
	.ioctls			= armada_ioctls,
	.fops			= &armada_drm_fops,
};

static int armada_drm_probe(struct platform_device *pdev)
{
	return drm_platform_init(&armada_drm_driver, pdev);
}

static int armada_drm_remove(struct platform_device *pdev)
{
	drm_platform_exit(&armada_drm_driver, pdev);
	return 0;
}

static const struct platform_device_id armada_drm_platform_ids[] = {
	{
		.name		= "armada-drm",
		.driver_data	= (unsigned long)&armada510_ops,
	}, {
		.name		= "armada-510-drm",
		.driver_data	= (unsigned long)&armada510_ops,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, armada_drm_platform_ids);

static struct platform_driver armada_drm_platform_driver = {
	.probe	= armada_drm_probe,
	.remove	= armada_drm_remove,
	.driver	= {
		.name	= "armada-drm",
		.owner	= THIS_MODULE,
	},
	.id_table = armada_drm_platform_ids,
};

static int __init armada_drm_init(void)
{
	armada_drm_driver.num_ioctls = DRM_ARRAY_SIZE(armada_ioctls);
	return platform_driver_register(&armada_drm_platform_driver);
}
module_init(armada_drm_init);

static void __exit armada_drm_exit(void)
{
	platform_driver_unregister(&armada_drm_platform_driver);
}
module_exit(armada_drm_exit);

MODULE_AUTHOR("Russell King <rmk+kernel@arm.linux.org.uk>");
MODULE_DESCRIPTION("Armada DRM Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:armada-drm");
