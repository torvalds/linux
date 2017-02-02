/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2014 Endless Mobile
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Written by:
 *     Jasper St. Pierre <jstpierre@mecheye.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/of_graph.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_rect.h>
#include <drm/drm_fb_helper.h>

#include "meson_drv.h"
#include "meson_plane.h"
#include "meson_crtc.h"
#include "meson_venc_cvbs.h"

#include "meson_vpp.h"
#include "meson_viu.h"
#include "meson_venc.h"
#include "meson_canvas.h"
#include "meson_registers.h"

#define DRIVER_NAME "meson"
#define DRIVER_DESC "Amlogic Meson DRM driver"

/*
 * Video Processing Unit
 *
 * VPU Handles the Global Video Processing, it includes management of the
 * clocks gates, blocks reset lines and power domains.
 *
 * What is missing :
 * - Full reset of entire video processing HW blocks
 * - Scaling and setup of the VPU clock
 * - Bus clock gates
 * - Powering up video processing HW blocks
 * - Powering Up HDMI controller and PHY
 */

static void meson_fb_output_poll_changed(struct drm_device *dev)
{
	struct meson_drm *priv = dev->dev_private;

	drm_fbdev_cma_hotplug_event(priv->fbdev);
}

static const struct drm_mode_config_funcs meson_mode_config_funcs = {
	.output_poll_changed = meson_fb_output_poll_changed,
	.atomic_check        = drm_atomic_helper_check,
	.atomic_commit       = drm_atomic_helper_commit,
	.fb_create           = drm_fb_cma_create,
};

static int meson_enable_vblank(struct drm_device *dev, unsigned int crtc)
{
	struct meson_drm *priv = dev->dev_private;

	meson_venc_enable_vsync(priv);

	return 0;
}

static void meson_disable_vblank(struct drm_device *dev, unsigned int crtc)
{
	struct meson_drm *priv = dev->dev_private;

	meson_venc_disable_vsync(priv);
}

static irqreturn_t meson_irq(int irq, void *arg)
{
	struct drm_device *dev = arg;
	struct meson_drm *priv = dev->dev_private;

	(void)readl_relaxed(priv->io_base + _REG(VENC_INTFLAG));

	meson_crtc_irq(priv);

	return IRQ_HANDLED;
}

static const struct file_operations fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver meson_driver = {
	.driver_features	= DRIVER_HAVE_IRQ | DRIVER_GEM |
				  DRIVER_MODESET | DRIVER_PRIME |
				  DRIVER_ATOMIC,

	/* Vblank */
	.enable_vblank		= meson_enable_vblank,
	.disable_vblank		= meson_disable_vblank,
	.get_vblank_counter	= drm_vblank_no_hw_counter,

	/* IRQ */
	.irq_handler		= meson_irq,

	/* PRIME Ops */
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,

	/* GEM Ops */
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,

	/* Misc */
	.fops			= &fops,
	.name			= DRIVER_NAME,
	.desc			= DRIVER_DESC,
	.date			= "20161109",
	.major			= 1,
	.minor			= 0,
};

static bool meson_vpu_has_available_connectors(struct device *dev)
{
	struct device_node *ep, *remote;

	/* Parses each endpoint and check if remote exists */
	for_each_endpoint_of_node(dev->of_node, ep) {
		/* If the endpoint node exists, consider it enabled */
		remote = of_graph_get_remote_port(ep);
		if (remote)
			return true;
	}

	return false;
}

static struct regmap_config meson_regmap_config = {
	.reg_bits       = 32,
	.val_bits       = 32,
	.reg_stride     = 4,
	.max_register   = 0x1000,
};

static int meson_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_drm *priv;
	struct drm_device *drm;
	struct resource *res;
	void __iomem *regs;
	int ret;

	/* Checks if an output connector is available */
	if (!meson_vpu_has_available_connectors(dev)) {
		dev_err(dev, "No output connector available\n");
		return -ENODEV;
	}

	drm = drm_dev_alloc(&meson_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto free_drm;
	}
	drm->dev_private = priv;
	priv->drm = drm;
	priv->dev = dev;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "vpu");
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	priv->io_base = regs;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hhi");
	/* Simply ioremap since it may be a shared register zone */
	regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!regs)
		return -EADDRNOTAVAIL;

	priv->hhi = devm_regmap_init_mmio(dev, regs,
					  &meson_regmap_config);
	if (IS_ERR(priv->hhi)) {
		dev_err(&pdev->dev, "Couldn't create the HHI regmap\n");
		return PTR_ERR(priv->hhi);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dmc");
	/* Simply ioremap since it may be a shared register zone */
	regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!regs)
		return -EADDRNOTAVAIL;

	priv->dmc = devm_regmap_init_mmio(dev, regs,
					  &meson_regmap_config);
	if (IS_ERR(priv->dmc)) {
		dev_err(&pdev->dev, "Couldn't create the DMC regmap\n");
		return PTR_ERR(priv->dmc);
	}

	priv->vsync_irq = platform_get_irq(pdev, 0);

	drm_vblank_init(drm, 1);
	drm_mode_config_init(drm);

	/* Encoder Initialization */

	ret = meson_venc_cvbs_create(priv);
	if (ret)
		goto free_drm;

	/* Hardware Initialization */

	meson_venc_init(priv);
	meson_vpp_init(priv);
	meson_viu_init(priv);

	ret = meson_plane_create(priv);
	if (ret)
		goto free_drm;

	ret = meson_crtc_create(priv);
	if (ret)
		goto free_drm;

	ret = drm_irq_install(drm, priv->vsync_irq);
	if (ret)
		goto free_drm;

	drm_mode_config_reset(drm);
	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;
	drm->mode_config.funcs = &meson_mode_config_funcs;

	priv->fbdev = drm_fbdev_cma_init(drm, 32,
					 drm->mode_config.num_connector);
	if (IS_ERR(priv->fbdev)) {
		ret = PTR_ERR(priv->fbdev);
		goto free_drm;
	}

	drm_kms_helper_poll_init(drm);

	platform_set_drvdata(pdev, priv);

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto free_drm;

	return 0;

free_drm:
	drm_dev_unref(drm);

	return ret;
}

static int meson_drv_remove(struct platform_device *pdev)
{
	struct drm_device *drm = dev_get_drvdata(&pdev->dev);
	struct meson_drm *priv = drm->dev_private;

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	drm_fbdev_cma_fini(priv->fbdev);
	drm_mode_config_cleanup(drm);
	drm_vblank_cleanup(drm);
	drm_dev_unref(drm);

	return 0;
}

static const struct of_device_id dt_match[] = {
	{ .compatible = "amlogic,meson-gxbb-vpu" },
	{ .compatible = "amlogic,meson-gxl-vpu" },
	{ .compatible = "amlogic,meson-gxm-vpu" },
	{}
};
MODULE_DEVICE_TABLE(of, dt_match);

static struct platform_driver meson_drm_platform_driver = {
	.probe      = meson_drv_probe,
	.remove     = meson_drv_remove,
	.driver     = {
		.name	= "meson-drm",
		.of_match_table = dt_match,
	},
};

module_platform_driver(meson_drm_platform_driver);

MODULE_AUTHOR("Jasper St. Pierre <jstpierre@mecheye.net>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
