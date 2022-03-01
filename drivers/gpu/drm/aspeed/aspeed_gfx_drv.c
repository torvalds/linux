// SPDX-License-Identifier: GPL-2.0+
// Copyright 2018 IBM Corporation

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_drv.h>

#include "aspeed_gfx.h"

/**
 * DOC: ASPEED GFX Driver
 *
 * This driver is for the ASPEED BMC SoC's 'GFX' display hardware, also called
 * the 'SOC Display Controller' in the datasheet. This driver runs on the ARM
 * based BMC systems, unlike the ast driver which runs on a host CPU and is for
 * a PCIe graphics device.
 *
 * The AST2500 supports a total of 3 output paths:
 *
 *   1. VGA output, the output target can choose either or both to the DAC
 *   or DVO interface.
 *
 *   2. Graphics CRT output, the output target can choose either or both to
 *   the DAC or DVO interface.
 *
 *   3. Video input from DVO, the video input can be used for video engine
 *   capture or DAC display output.
 *
 * Output options are selected in SCU2C.
 *
 * The "VGA mode" device is the PCI attached controller. The "Graphics CRT"
 * is the ARM's internal display controller.
 *
 * The driver only supports a simple configuration consisting of a 40MHz
 * pixel clock, fixed by hardware limitations, and the VGA output path.
 *
 * The driver was written with the 'AST2500 Software Programming Guide' v17,
 * which is available under NDA from ASPEED.
 */

struct aspeed_gfx_config {
	u32 dac_reg;		/* DAC register in SCU */
	u32 vga_scratch_reg;	/* VGA scratch register in SCU */
	u32 throd_val;		/* Default Threshold Seting */
	u32 scan_line_max;	/* Max memory size of one scan line */
};

static const struct aspeed_gfx_config ast2400_config = {
	.dac_reg = 0x2c,
	.vga_scratch_reg = 0x50,
	.throd_val = CRT_THROD_LOW(0x1e) | CRT_THROD_HIGH(0x12),
	.scan_line_max = 64,
};

static const struct aspeed_gfx_config ast2500_config = {
	.dac_reg = 0x2c,
	.vga_scratch_reg = 0x50,
	.throd_val = CRT_THROD_LOW(0x24) | CRT_THROD_HIGH(0x3c),
	.scan_line_max = 128,
};

static const struct of_device_id aspeed_gfx_match[] = {
	{ .compatible = "aspeed,ast2400-gfx", .data = &ast2400_config },
	{ .compatible = "aspeed,ast2500-gfx", .data = &ast2500_config },
	{ },
};
MODULE_DEVICE_TABLE(of, aspeed_gfx_match);

static const struct drm_mode_config_funcs aspeed_gfx_mode_config_funcs = {
	.fb_create		= drm_gem_fb_create,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
};

static int aspeed_gfx_setup_mode_config(struct drm_device *drm)
{
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = 800;
	drm->mode_config.max_height = 600;
	drm->mode_config.funcs = &aspeed_gfx_mode_config_funcs;

	return ret;
}

static irqreturn_t aspeed_gfx_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	u32 reg;

	reg = readl(priv->base + CRT_CTRL1);

	if (reg & CRT_CTRL_VERTICAL_INTR_STS) {
		drm_crtc_handle_vblank(&priv->pipe.crtc);
		writel(reg, priv->base + CRT_CTRL1);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int aspeed_gfx_load(struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	struct device_node *np = pdev->dev.of_node;
	const struct aspeed_gfx_config *config;
	const struct of_device_id *match;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(drm->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	match = of_match_device(aspeed_gfx_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	config = match->data;

	priv->dac_reg = config->dac_reg;
	priv->vga_scratch_reg = config->vga_scratch_reg;
	priv->throd_val = config->throd_val;
	priv->scan_line_max = config->scan_line_max;

	priv->scu = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(priv->scu)) {
		priv->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2500-scu");
		if (IS_ERR(priv->scu)) {
			dev_err(&pdev->dev, "failed to find SCU regmap\n");
			return PTR_ERR(priv->scu);
		}
	}

	ret = of_reserved_mem_device_init(drm->dev);
	if (ret) {
		dev_err(&pdev->dev,
			"failed to initialize reserved mem: %d\n", ret);
		return ret;
	}

	ret = dma_set_mask_and_coherent(drm->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", ret);
		return ret;
	}

	priv->rst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(priv->rst)) {
		dev_err(&pdev->dev,
			"missing or invalid reset controller device tree entry");
		return PTR_ERR(priv->rst);
	}
	reset_control_deassert(priv->rst);

	priv->clk = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev,
			"missing or invalid clk device tree entry");
		return PTR_ERR(priv->clk);
	}
	clk_prepare_enable(priv->clk);

	/* Sanitize control registers */
	writel(0, priv->base + CRT_CTRL1);
	writel(0, priv->base + CRT_CTRL2);

	ret = aspeed_gfx_setup_mode_config(drm);
	if (ret < 0)
		return ret;

	ret = drm_vblank_init(drm, 1);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to initialise vblank\n");
		return ret;
	}

	ret = aspeed_gfx_create_output(drm);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to create outputs\n");
		return ret;
	}

	ret = aspeed_gfx_create_pipe(drm);
	if (ret < 0) {
		dev_err(drm->dev, "Cannot setup simple display pipe\n");
		return ret;
	}

	ret = devm_request_irq(drm->dev, platform_get_irq(pdev, 0),
			       aspeed_gfx_irq_handler, 0, "aspeed gfx", drm);
	if (ret < 0) {
		dev_err(drm->dev, "Failed to install IRQ handler\n");
		return ret;
	}

	drm_mode_config_reset(drm);

	return 0;
}

static void aspeed_gfx_unload(struct drm_device *drm)
{
	drm_kms_helper_poll_fini(drm);
}

DEFINE_DRM_GEM_CMA_FOPS(fops);

static const struct drm_driver aspeed_gfx_driver = {
	.driver_features        = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	DRM_GEM_CMA_DRIVER_OPS,
	.fops = &fops,
	.name = "aspeed-gfx-drm",
	.desc = "ASPEED GFX DRM",
	.date = "20180319",
	.major = 1,
	.minor = 0,
};

static ssize_t dac_mux_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct aspeed_gfx *priv = dev_get_drvdata(dev);
	u32 val;
	int rc;

	rc = kstrtou32(buf, 0, &val);
	if (rc)
		return rc;

	if (val > 3)
		return -EINVAL;

	rc = regmap_update_bits(priv->scu, priv->dac_reg, 0x30000, val << 16);
	if (rc < 0)
		return 0;

	return count;
}

static ssize_t dac_mux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aspeed_gfx *priv = dev_get_drvdata(dev);
	u32 reg;
	int rc;

	rc = regmap_read(priv->scu, priv->dac_reg, &reg);
	if (rc)
		return rc;

	return sprintf(buf, "%u\n", (reg >> 16) & 0x3);
}
static DEVICE_ATTR_RW(dac_mux);

static ssize_t
vga_pw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct aspeed_gfx *priv = dev_get_drvdata(dev);
	u32 reg;
	int rc;

	rc = regmap_read(priv->scu, priv->vga_scratch_reg, &reg);
	if (rc)
		return rc;

	return sprintf(buf, "%u\n", reg);
}
static DEVICE_ATTR_RO(vga_pw);

static struct attribute *aspeed_sysfs_entries[] = {
	&dev_attr_vga_pw.attr,
	&dev_attr_dac_mux.attr,
	NULL,
};

static struct attribute_group aspeed_sysfs_attr_group = {
	.attrs = aspeed_sysfs_entries,
};

static int aspeed_gfx_probe(struct platform_device *pdev)
{
	struct aspeed_gfx *priv;
	int ret;

	priv = devm_drm_dev_alloc(&pdev->dev, &aspeed_gfx_driver,
				  struct aspeed_gfx, drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	ret = aspeed_gfx_load(&priv->drm);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);

	ret = sysfs_create_group(&pdev->dev.kobj, &aspeed_sysfs_attr_group);
	if (ret)
		return ret;

	ret = drm_dev_register(&priv->drm, 0);
	if (ret)
		goto err_unload;

	drm_fbdev_generic_setup(&priv->drm, 32);
	return 0;

err_unload:
	sysfs_remove_group(&pdev->dev.kobj, &aspeed_sysfs_attr_group);
	aspeed_gfx_unload(&priv->drm);

	return ret;
}

static int aspeed_gfx_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &aspeed_sysfs_attr_group);
	drm_dev_unregister(drm);
	aspeed_gfx_unload(drm);

	return 0;
}

static struct platform_driver aspeed_gfx_platform_driver = {
	.probe		= aspeed_gfx_probe,
	.remove		= aspeed_gfx_remove,
	.driver = {
		.name = "aspeed_gfx",
		.of_match_table = aspeed_gfx_match,
	},
};

module_platform_driver(aspeed_gfx_platform_driver);

MODULE_AUTHOR("Joel Stanley <joel@jms.id.au>");
MODULE_DESCRIPTION("ASPEED BMC DRM/KMS driver");
MODULE_LICENSE("GPL");
