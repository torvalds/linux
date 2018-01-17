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
#include <linux/list.h>
#include <linux/of_graph.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_of.h>

#include "hdlcd_drv.h"
#include "hdlcd_regs.h"

static int hdlcd_load(struct drm_device *drm, unsigned long flags)
{
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct resource *res;
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

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hdlcd->mmio = devm_ioremap_resource(drm->dev, res);
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

	ret = drm_irq_install(drm, platform_get_irq(pdev, 0));
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
	.output_poll_changed = drm_fb_helper_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static void hdlcd_setup_mode_config(struct drm_device *drm)
{
	drm_mode_config_init(drm);
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = HDLCD_MAX_XRES;
	drm->mode_config.max_height = HDLCD_MAX_YRES;
	drm->mode_config.funcs = &hdlcd_mode_config_funcs;
}

static irqreturn_t hdlcd_irq(int irq, void *arg)
{
	struct drm_device *drm = arg;
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
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

static void hdlcd_irq_preinstall(struct drm_device *drm)
{
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	/* Ensure interrupts are disabled */
	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, 0);
	hdlcd_write(hdlcd, HDLCD_REG_INT_CLEAR, ~0);
}

static int hdlcd_irq_postinstall(struct drm_device *drm)
{
#ifdef CONFIG_DEBUG_FS
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	unsigned long irq_mask = hdlcd_read(hdlcd, HDLCD_REG_INT_MASK);

	/* enable debug interrupts */
	irq_mask |= HDLCD_DEBUG_INT_MASK;

	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, irq_mask);
#endif
	return 0;
}

static void hdlcd_irq_uninstall(struct drm_device *drm)
{
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	/* disable all the interrupts that we might have enabled */
	unsigned long irq_mask = hdlcd_read(hdlcd, HDLCD_REG_INT_MASK);

#ifdef CONFIG_DEBUG_FS
	/* disable debug interrupts */
	irq_mask &= ~HDLCD_DEBUG_INT_MASK;
#endif

	/* disable vsync interrupts */
	irq_mask &= ~HDLCD_INTERRUPT_VSYNC;

	hdlcd_write(hdlcd, HDLCD_REG_INT_MASK, irq_mask);
}

#ifdef CONFIG_DEBUG_FS
static int hdlcd_show_underrun_count(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *drm = node->minor->dev;
	struct hdlcd_drm_private *hdlcd = drm->dev_private;

	seq_printf(m, "underrun : %d\n", atomic_read(&hdlcd->buffer_underrun_count));
	seq_printf(m, "dma_end  : %d\n", atomic_read(&hdlcd->dma_end_count));
	seq_printf(m, "bus_error: %d\n", atomic_read(&hdlcd->bus_error_count));
	seq_printf(m, "vsync    : %d\n", atomic_read(&hdlcd->vsync_count));
	return 0;
}

static int hdlcd_show_pxlclock(struct seq_file *m, void *arg)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *drm = node->minor->dev;
	struct hdlcd_drm_private *hdlcd = drm->dev_private;
	unsigned long clkrate = clk_get_rate(hdlcd->clk);
	unsigned long mode_clock = hdlcd->crtc.mode.crtc_clock * 1000;

	seq_printf(m, "hw  : %lu\n", clkrate);
	seq_printf(m, "mode: %lu\n", mode_clock);
	return 0;
}

static struct drm_info_list hdlcd_debugfs_list[] = {
	{ "interrupt_count", hdlcd_show_underrun_count, 0 },
	{ "clocks", hdlcd_show_pxlclock, 0 },
};

static int hdlcd_debugfs_init(struct drm_minor *minor)
{
	return drm_debugfs_create_files(hdlcd_debugfs_list,
		ARRAY_SIZE(hdlcd_debugfs_list),	minor->debugfs_root, minor);
}
#endif

DEFINE_DRM_GEM_CMA_FOPS(fops);

static struct drm_driver hdlcd_driver = {
	.driver_features = DRIVER_HAVE_IRQ | DRIVER_GEM |
			   DRIVER_MODESET | DRIVER_PRIME |
			   DRIVER_ATOMIC,
	.lastclose = drm_fb_helper_lastclose,
	.irq_handler = hdlcd_irq,
	.irq_preinstall = hdlcd_irq_preinstall,
	.irq_postinstall = hdlcd_irq_postinstall,
	.irq_uninstall = hdlcd_irq_uninstall,
	.gem_free_object_unlocked = drm_gem_cma_free_object,
	.gem_print_info = drm_gem_cma_print_info,
	.gem_vm_ops = &drm_gem_cma_vm_ops,
	.dumb_create = drm_gem_cma_dumb_create,
	.prime_handle_to_fd = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle = drm_gem_prime_fd_to_handle,
	.gem_prime_export = drm_gem_prime_export,
	.gem_prime_import = drm_gem_prime_import,
	.gem_prime_get_sg_table = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap = drm_gem_cma_prime_mmap,
#ifdef CONFIG_DEBUG_FS
	.debugfs_init = hdlcd_debugfs_init,
#endif
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

	hdlcd = devm_kzalloc(dev, sizeof(*hdlcd), GFP_KERNEL);
	if (!hdlcd)
		return -ENOMEM;

	drm = drm_dev_alloc(&hdlcd_driver, dev);
	if (IS_ERR(drm))
		return PTR_ERR(drm);

	drm->dev_private = hdlcd;
	dev_set_drvdata(dev, drm);

	hdlcd_setup_mode_config(drm);
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

	drm_mode_config_reset(drm);
	drm_kms_helper_poll_init(drm);

	ret = drm_fb_cma_fbdev_init(drm, 32, 0);
	if (ret)
		goto err_fbdev;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto err_register;

	return 0;

err_register:
	drm_fb_cma_fbdev_fini(drm);
err_fbdev:
	drm_kms_helper_poll_fini(drm);
err_vblank:
	pm_runtime_disable(drm->dev);
err_pm_active:
	component_unbind_all(dev, drm);
err_unload:
	of_node_put(hdlcd->crtc.port);
	hdlcd->crtc.port = NULL;
	drm_irq_uninstall(drm);
	of_reserved_mem_device_release(drm->dev);
err_free:
	drm_mode_config_cleanup(drm);
	dev_set_drvdata(dev, NULL);
	drm_dev_put(drm);

	return ret;
}

static void hdlcd_drm_unbind(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct hdlcd_drm_private *hdlcd = drm->dev_private;

	drm_dev_unregister(drm);
	drm_fb_cma_fbdev_fini(drm);
	drm_kms_helper_poll_fini(drm);
	component_unbind_all(dev, drm);
	of_node_put(hdlcd->crtc.port);
	hdlcd->crtc.port = NULL;
	pm_runtime_get_sync(drm->dev);
	drm_irq_uninstall(drm);
	pm_runtime_put_sync(drm->dev);
	pm_runtime_disable(drm->dev);
	of_reserved_mem_device_release(drm->dev);
	drm_atomic_helper_shutdown(drm);
	drm_mode_config_cleanup(drm);
	drm_dev_put(drm);
	drm->dev_private = NULL;
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

module_platform_driver(hdlcd_platform_driver);

MODULE_AUTHOR("Liviu Dudau");
MODULE_DESCRIPTION("ARM HDLCD DRM driver");
MODULE_LICENSE("GPL v2");
