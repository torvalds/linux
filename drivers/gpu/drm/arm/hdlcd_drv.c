/*
 * Copyright (C) 2013-2015 ARM Limited
 * Author: Liviu Dudau <Liviu.Dudau@arm.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 *  ARM HDLCD Driver
 */

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/console.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_module.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "hdlcd_drv.h"
#include "hdlcd_regs.h"

static irqreturn_t hdlcd_irq(int irq, void *arg)
{
	struct hdlcd_drm_private *hdlcd = arg;
	unsigned long irq_status;

	irq_status = hdlcd_read(hdlcd, HDLCD_REG_INT_STATUS);

#ifdef CONFIG_DEBUG_FS
	if (irq_status & HDLCD_INTERRUPT_UNDERRUN)
		atomic_inc(&hdlcd->buffer_underrun_count);

	if (irq_status & HDLCD_INTERRUPT_DMA_END)
		atomic_inc(&hdlcd->dma_end_count);

	if (irq_status & HDLCD_INTERRUPT_BUS_ERROR)
		atomic_inc(&hdlcd->bus_error_count);

	if (irq_status & HDLCD_INTERRUPT_VSYNC)
		atomic_inc(&hdlcd->vsync_count);

#endif
	if (irq_status & HDLCD_INTERRUPT_VSYNC)
		drm_crtc_handle_vblank(&hdlcd->crtc);

	/* acknowledge interrupt(s) */
	hdlcd_write(hdlcd, HDLCD_REG_INT_CLEAR, irq_status);

	return IRQ_HANDLED;
}

static int hdlcd_irq_install(struct hdlcd_drm_private *hdlcd)
{
	int ret;

	/* Ensure interrupts are disabled */
	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, 0);
	hdlcd_write(hdlcd, HDLCD_REG_INT_CLEAR, ~0);

	ret = request_irq(hdlcd->irq, hdlcd_irq, 0, "hdlcd", hdlcd);
	if (ret)
		return ret;

#ifdef CONFIG_DEBUG_FS
	/* enable debug interrupts */
	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, HDLCD_DEBUG_INT_MASK);
#endif

	return 0;
}

static void hdlcd_irq_uninstall(struct hdlcd_drm_private *hdlcd)
{
	/* disable all the interrupts that we might have enabled */
	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, 0);

	free_irq(hdlcd->irq, hdlcd);
}

static int hdlcd_load(struct drm_device *drm, unsigned long flags)
{
	struct hdlcd_drm_private *hdlcd = drm_to_hdlcd_priv(drm);
	struct platform_device *pdev = to_platform_device(drm->dev);
	u32 version;
	int ret;

	hdlcd->clk = devm_clk_get(drm->dev, "pxlclk");
	if (IS_ERR(hdlcd->clk))
		return PTR_ERR(hdlcd->clk);

#ifdef CONFIG_DEBUG_FS
	atomic_set(&hdlcd->buffer_underrun_count, 0);
	atomic_set(&hdlcd->bus_error_count, 0);
	atomic_set(&hdlcd->vsync_count, 0);
	atomic_set(&hdlcd->dma_end_count, 0);
#endif

	hdlcd->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hdlcd->mmio)) {
		DRM_ERROR("failed to map control registers area\n");
		ret = PTR_ERR(hdlcd->mmio);
		hdlcd->mmio = NULL;
		return ret;
	}

	version = hdlcd_read(hdlcd, HDLCD_REG_VERSION);
	if ((version & HDLCD_PRODUCT_MASK) != HDLCD_PRODUCT_ID) {
		DRM_ERROR("unknown product id: 0x%x\n", version);
		return -EINVAL;
	}
	DRM_INFO("found ARM HDLCD version r%dp%d\n",
		(version & HDLCD_VERSION_MAJOR_MASK) >> 8,
		version & HDLCD_VERSION_MINOR_MASK);

	/* Get the optional framebuffer memory resource */
	ret = of_reserved_mem_device_init(drm->dev);
	if (ret && ret != -ENODEV)
		return ret;

	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32));
	if (ret)
		goto setup_fail;

	ret = hdlcd_setup_crtc(drm);
	if (ret < 0) {
		DRM_ERROR("failed to create crtc\n");
		goto setup_fail;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		goto irq_fail;
	hdlcd->irq = ret;

	ret = hdlcd_irq_install(hdlcd);
	if (ret < 0) {
		DRM_ERROR("failed to install IRQ handler\n");
		goto irq_fail;
	}

	return 0;

irq_fail:
	drm_crtc_cleanup(&hdlcd->crtc);
setup_fail:
	of_reserved_mem_device_release(drm->dev);

	return ret;
}

static const struct drm_mode_config_funcs hdlcd_mode_config_funcs = {
	.fb_create = drm_gem_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int hdlcd_setup_mode_config(struct drm_device *drm)
{
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = HDLCD_MAX_XRES;
	drm->mode_config.max_height = HDLCD_MAX_YRES;
	drm->mode_config.funcs = &hdlcd_mode_config_funcs;

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int hdlcd_show_underrun_count(struct seq_file *m, void *arg)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *drm = entry->dev;
	struct hdlcd_drm_private *hdlcd = drm_to_hdlcd_priv(drm);

	seq_printf(m, "underrun : %d\n", atomic_read(&hdlcd->buffer_underrun_count));
	seq_printf(m, "dma_end  : %d\n", atomic_read(&hdlcd->dma_end_count));
	seq_printf(m, "bus_error: %d\n", atomic_read(&hdlcd->bus_error_count));
	seq_printf(m, "vsync    : %d\n", atomic_read(&hdlcd->vsync_count));
	return 0;
}

static int hdlcd_show_pxlclock(struct seq_file *m, void *arg)
{
	struct drm_debugfs_entry *entry = m->private;
	struct drm_device *drm = entry->dev;
	struct hdlcd_drm_private *hdlcd = drm_to_hdlcd_priv(drm);
	unsigned long clkrate = clk_get_rate(hdlcd->clk);
	unsigned long mode_clock = hdlcd->crtc.mode.crtc_clock * 1000;

	seq_printf(m, "hw  : %lu\n", clkrate);
	seq_printf(m, "mode: %lu\n", mode_clock);
	return 0;
}

static struct drm_debugfs_info hdlcd_debugfs_list[] = {
	{ "interrupt_count", hdlcd_show_underrun_count, 0 },
	{ "clocks", hdlcd_show_pxlclock, 0 },
};
#endif

DEFINE_DRM_GEM_DMA_FOPS(fops);

static const struct drm_driver hdlcd_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_DMA_DRIVER_OPS,
	.fops = &fops,
	.name = "hdlcd",
	.desc = "ARM HDLCD Controller DRM",
	.date = "20151021",
	.major = 1,
	.minor = 0,
};

static int hdlcd_drm_bind(struct device *dev)
{
	struct drm_device *drm;
	struct hdlcd_drm_private *hdlcd;
	int ret;

	hdlcd = devm_drm_dev_alloc(dev, &hdlcd_driver, typeof(*hdlcd), base);
	if (IS_ERR(hdlcd))
		return PTR_ERR(hdlcd);

	drm = &hdlcd->base;

	dev_set_drvdata(dev, drm);

	ret = hdlcd_setup_mode_config(drm);
	if (ret)
		goto err_free;

	ret = hdlcd_load(drm, 0);
	if (ret)
		goto err_free;

	/* Set the CRTC's port so that the encoder component can find it */
	hdlcd->crtc.port = of_graph_get_port_by_id(dev->of_node, 0);

	ret = component_bind_all(dev, drm);
	if (ret) {
		DRM_ERROR("Failed to bind all components\n");
		goto err_unload;
	}

	ret = pm_runtime_set_active(dev);
	if (ret)
		goto err_pm_active;

	pm_runtime_enable(dev);

	ret = drm_vblank_init(drm, drm->mode_config.num_crtc);
	if (ret < 0) {
		DRM_ERROR("failed to initialise vblank\n");
		goto err_vblank;
	}

	/*
	 * If EFI left us running, take over from simple framebuffer
	 * drivers. Read HDLCD_REG_COMMAND to see if we are enabled.
	 */
	if (hdlcd_read(hdlcd, HDLCD_REG_COMMAND)) {
		hdlcd_write(hdlcd, HDLCD_REG_COMMAND, 0);
		drm_aperture_remove_framebuffers(false, &hdlcd_driver);
	}

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

#ifdef CONFIG_DEBUG_FS
	drm_debugfs_add_files(drm, hdlcd_debugfs_list, ARRAY_SIZE(hdlcd_debugfs_list));
#endif

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_register;

	drm_fbdev_dma_setup(drm, 32);

	return 0;

err_register:
	drm_kms_helper_poll_fini(drm);
err_vblank:
	pm_runtime_disable(drm->dev);
err_pm_active:
	drm_atomic_helper_shutdown(drm);
	component_unbind_all(dev, drm);
err_unload:
	of_node_put(hdlcd->crtc.port);
	hdlcd->crtc.port = NULL;
	hdlcd_irq_uninstall(hdlcd);
	of_reserved_mem_device_release(drm->dev);
err_free:
	dev_set_drvdata(dev, NULL);
	return ret;
}

static void hdlcd_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct hdlcd_drm_private *hdlcd = drm_to_hdlcd_priv(drm);

	drm_dev_unregister(drm);
	drm_kms_helper_poll_fini(drm);
	component_unbind_all(dev, drm);
	of_node_put(hdlcd->crtc.port);
	hdlcd->crtc.port = NULL;
	pm_runtime_get_sync(dev);
	drm_atomic_helper_shutdown(drm);
	hdlcd_irq_uninstall(hdlcd);
	pm_runtime_put(dev);
	if (pm_runtime_enabled(dev))
		pm_runtime_disable(dev);
	of_reserved_mem_device_release(dev);
	dev_set_drvdata(dev, NULL);
}

static const struct component_master_ops hdlcd_master_ops = {
	.bind		= hdlcd_drm_bind,
	.unbind		= hdlcd_drm_unbind,
};

static int compare_dev(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static int hdlcd_probe(struct platform_device *pdev)
{
	struct device_node *port;
	struct component_match *match = NULL;

	/* there is only one output port inside each device, find it */
	port = of_graph_get_remote_node(pdev->dev.of_node, 0, 0);
	if (!port)
		return -ENODEV;

	drm_of_component_match_add(&pdev->dev, &match, compare_dev, port);
	of_node_put(port);

	return component_master_add_with_match(&pdev->dev, &hdlcd_master_ops,
					       match);
}

static int hdlcd_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &hdlcd_master_ops);
	return 0;
}

static const struct of_device_id  hdlcd_of_match[] = {
	{ .compatible	= "arm,hdlcd" },
	{},
};
MODULE_DEVICE_TABLE(of, hdlcd_of_match);

static int __maybe_unused hdlcd_pm_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	return drm_mode_config_helper_suspend(drm);
}

static int __maybe_unused hdlcd_pm_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);

	drm_mode_config_helper_resume(drm);

	return 0;
}

static SIMPLE_DEV_PM_OPS(hdlcd_pm_ops, hdlcd_pm_suspend, hdlcd_pm_resume);

static struct platform_driver hdlcd_platform_driver = {
	.probe		= hdlcd_probe,
	.remove		= hdlcd_remove,
	.driver	= {
		.name = "hdlcd",
		.pm = &hdlcd_pm_ops,
		.of_match_table	= hdlcd_of_match,
	},
};

drm_module_platform_driver(hdlcd_platform_driver);

MODULE_AUTHOR("Liviu Dudau");
MODULE_DESCRIPTION("ARM HDLCD DRM driver");
MODULE_LICENSE("GPL v2");
