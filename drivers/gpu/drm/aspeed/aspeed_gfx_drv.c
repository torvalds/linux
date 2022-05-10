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
	u32 int_clear_reg;	/* Interrupt clear register */
	u32 vga_scratch_reg;	/* VGA scratch register in SCU */
	u32 throd_val;		/* Default Threshold Seting */
	u32 scan_line_max;	/* Max memory size of one scan line */
	u32 gfx_flags;		/* Flags for gfx chip caps */
	u32 pcie_int_reg;	/* pcie interrupt */
};

static const struct aspeed_gfx_config ast2400_config = {
	.dac_reg = 0x2c,
	.int_clear_reg = 0x60,
	.vga_scratch_reg = 0x50,
	.throd_val = CRT_THROD_LOW(0x1e) | CRT_THROD_HIGH(0x12),
	.scan_line_max = 64,
	.gfx_flags = CLK_G4,
	.pcie_int_reg = 0x18,
};

static const struct aspeed_gfx_config ast2500_config = {
	.dac_reg = 0x2c,
	.int_clear_reg = 0x60,
	.vga_scratch_reg = 0x50,
	.throd_val = CRT_THROD_LOW(0x24) | CRT_THROD_HIGH(0x3c),
	.scan_line_max = 128,
	.gfx_flags = 0,
	.pcie_int_reg = 0x18,
};

static const struct aspeed_gfx_config ast2600_config = {
	.dac_reg = 0xc0,
	.int_clear_reg = 0x68,
	.vga_scratch_reg = 0x50,
	.throd_val = CRT_THROD_LOW(0x50) | CRT_THROD_HIGH(0x70),
	.scan_line_max = 128,
	.gfx_flags = RESET_G6 | CLK_G6,
	.pcie_int_reg = 0x560,
};

static const struct of_device_id aspeed_gfx_match[] = {
	{ .compatible = "aspeed,ast2400-gfx", .data = &ast2400_config },
	{ .compatible = "aspeed,ast2500-gfx", .data = &ast2500_config },
	{ .compatible = "aspeed,ast2600-gfx", .data = &ast2600_config },
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
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	int ret;

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;

	switch (priv->flags & CLK_MASK) {
	case CLK_G6:
		drm->mode_config.max_width = 1024;
		drm->mode_config.max_height = 768;
		break;
	default:
		drm->mode_config.max_width = 800;
		drm->mode_config.max_height = 600;
		break;
	}

	drm->mode_config.funcs = &aspeed_gfx_mode_config_funcs;

	return ret;
}

static irqreturn_t aspeed_host_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	u32 reg;

	regmap_read(priv->scu, priv->pcie_int_reg, &reg);

	if (reg & STS_PERST_STATUS) {
		if (reg & PCIE_PERST_L_T_H) {
			dev_dbg(drm->dev, "pcie active.\n");
			/*Change the DP back to host*/
			if (priv->dp_support) {
				/*Change the DP back to host*/
				regmap_update_bits(priv->dp,
				DP_SOURCE, DP_CONTROL_FROM_SOC, 0);
				dev_dbg(drm->dev, "dp set at 0 int L_T_H.\n");
				regmap_update_bits(priv->scu,
				priv->dac_reg, DP_FROM_SOC, 0);
			}

			/*Change the CRT back to host*/
			regmap_update_bits(priv->scu, priv->dac_reg,
			CRT_FROM_SOC, 0);
		} else if (reg & PCIE_PERST_H_T_L) {
			dev_dbg(drm->dev, "pcie de-active.\n");
			/*Change the DP into host*/
			if (priv->dp_support) {
				/*Change the DP back to soc*/
				regmap_update_bits(priv->dp, DP_SOURCE,
				DP_CONTROL_FROM_SOC, DP_CONTROL_FROM_SOC);
				dev_dbg(drm->dev, "dp set at 11 int H_T_L.\n");
				regmap_update_bits(priv->scu, priv->dac_reg,
				DP_FROM_SOC, DP_FROM_SOC);
			}

			/*Change the CRT into soc*/
			regmap_update_bits(priv->scu, priv->dac_reg,
			CRT_FROM_SOC, CRT_FROM_SOC);
		}
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t aspeed_gfx_irq_handler(int irq, void *data)
{
	struct drm_device *drm = data;
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	u32 reg;

	reg = readl(priv->base + CRT_CTRL1);

	if (reg & CRT_CTRL_VERTICAL_INTR_STS) {
		drm_crtc_handle_vblank(&priv->pipe.crtc);
		writel(reg, priv->base + priv->int_clr_reg);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int aspeed_pcie_active_detect(struct drm_device *drm)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	u32 reg = 0;

	/* map pcie ep resource */
	priv->pcie_ep = syscon_regmap_lookup_by_compatible("aspeed,ast2500-pcie-ep");
	if (IS_ERR(priv->pcie_ep)) {
		priv->pcie_ep = syscon_regmap_lookup_by_compatible("aspeed,ast2600-pcie-ep");
		if (IS_ERR(priv->pcie_ep)) {
			dev_err(drm->dev, "failed to find pcie_ep regmap\n");
			return PTR_ERR(priv->pcie_ep);
		}
	}

	/* check pcie rst status */
	regmap_read(priv->pcie_ep, PCIE_LINK_REG, &reg);
	dev_dbg(drm->dev, "g6 drv link reg v %x\n", reg);

	/* host vga is on or not */
	if (reg & PCIE_LINK_STATUS)
		priv->pcie_active = 0x1;
	else
		priv->pcie_active = 0x0;

	return 0;
}

static int aspeed_adaptor_detect(struct drm_device *drm)
{
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);
	u32 reg = 0;

	switch (priv->flags & CLK_MASK) {
	case CLK_G6:
		/* check AST DP is executed or not*/
		regmap_read(priv->scu, SCU_DP_STATUS, &reg);
		if (((reg>>8) & DP_EXECUTE) == DP_EXECUTE) {
			priv->dp_support = 0x1;

			priv->dp = syscon_regmap_lookup_by_compatible(DP_CP_NAME);
			if (IS_ERR(priv->dp)) {
				dev_err(drm->dev, "failed to find DP regmap\n");
				return PTR_ERR(priv->dp);
			}

			priv->dpmcu = syscon_regmap_lookup_by_compatible(DP_MCU_CP_NAME);
			if (IS_ERR(priv->dpmcu)) {
				dev_err(drm->dev, "failed to find DP MCU regmap\n");
				return PTR_ERR(priv->dpmcu);
			}

			/* change the dp setting is coming from soc display */
			if (!priv->pcie_active) {
				regmap_update_bits(priv->dp, DP_SOURCE,
				DP_CONTROL_FROM_SOC, DP_CONTROL_FROM_SOC);
			}
		}
		break;
	default:
		priv->dp_support = 0x0;
		priv->dp = NULL;
		priv->dpmcu = NULL;
		break;
	}
	return 0;
}

static int aspeed_gfx_reset(struct drm_device *drm)
{
	struct platform_device *pdev = to_platform_device(drm->dev);
	struct aspeed_gfx *priv = to_aspeed_gfx(drm);

	switch (priv->flags & RESET_MASK) {
	case RESET_G6:
		priv->rst_crt = devm_reset_control_get(&pdev->dev, "crt");
		if (IS_ERR(priv->rst_crt)) {
			dev_err(&pdev->dev,
				"missing or invalid crt reset controller device tree entry");
			return PTR_ERR(priv->rst_crt);
		}
		reset_control_deassert(priv->rst_crt);

		priv->rst_engine = devm_reset_control_get(&pdev->dev, "engine");
		if (IS_ERR(priv->rst_engine)) {
			dev_err(&pdev->dev,
				"missing or invalid engine reset controller device tree entry");
			return PTR_ERR(priv->rst_engine);
		}
		reset_control_deassert(priv->rst_engine);
		break;

	default:
		priv->rst_crt = devm_reset_control_get_exclusive(&pdev->dev, NULL);
		if (IS_ERR(priv->rst_crt)) {
			dev_err(&pdev->dev,
				"missing or invalid reset controller device tree entry");
			return PTR_ERR(priv->rst_crt);
		}
		reset_control_deassert(priv->rst_crt);
		break;
	}

	return 0;
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
	priv->int_clr_reg = config->int_clear_reg;
	priv->vga_scratch_reg = config->vga_scratch_reg;
	priv->throd_val = config->throd_val;
	priv->scan_line_max = config->scan_line_max;
	priv->flags = config->gfx_flags;
	priv->pcie_int_reg = config->pcie_int_reg;

	/* add pcie detect after ast2400 */
	if (priv->flags != CLK_G4)
		priv->pcie_advance = 1;

	priv->scu = syscon_regmap_lookup_by_phandle(np, "syscon");
	if (IS_ERR(priv->scu)) {
		priv->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2400-scu");
		if (IS_ERR(priv->scu)) {
			priv->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2500-scu");
			if (IS_ERR(priv->scu)) {
				priv->scu = syscon_regmap_lookup_by_compatible("aspeed,ast2600-scu");
				if (IS_ERR(priv->scu)) {
					dev_err(&pdev->dev, "failed to find SCU regmap\n");
					return PTR_ERR(priv->scu);
				}
			}
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

	ret = aspeed_gfx_reset(drm);
	if (ret) {
		dev_err(&pdev->dev,
			"missing or invalid reset controller device tree entry");
		return ret;
	}

	priv->clk = devm_clk_get(drm->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev,
			"missing or invalid clk device tree entry");
		return PTR_ERR(priv->clk);
	}
	clk_prepare_enable(priv->clk);

	if (priv->pcie_advance) {
		ret = aspeed_pcie_active_detect(drm);
		if (ret) {
			dev_err(&pdev->dev,
				"missing or invalid pcie-ep controller device tree entry");
			return ret;
		}
	}

	ret = aspeed_adaptor_detect(drm);
	if (ret) {
		dev_err(&pdev->dev,
			"missing or invalid adaptor controller device tree entry");
		return ret;
	}

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

	/* install pcie reset detect */
	if (of_property_read_bool(np, "pcie-reset-detect") && priv->pcie_advance) {

		dev_dbg(drm->dev, "hook pcie reset.\n");

		/* Special watch the host power up / down */
		ret = devm_request_irq(drm->dev, platform_get_irq(pdev, 1),
					   aspeed_host_irq_handler,
					   IRQF_SHARED, "aspeed host active", drm);
		if (ret < 0) {
			dev_err(drm->dev, "Failed to install HOST active handler\n");
			return ret;
		}
		ret = devm_request_irq(drm->dev, platform_get_irq(pdev, 2),
					   aspeed_host_irq_handler,
					   IRQF_SHARED, "aspeed host deactive", drm);
		if (ret < 0) {
			dev_err(drm->dev, "Failed to install HOST de-active handler\n");
			return ret;
		}
	}

	drm_mode_config_reset(drm);

	return 0;
}

static void aspeed_gfx_unload(struct drm_device *drm)
{
	struct aspeed_gfx *priv = drm->dev_private;

	/* change the dp setting is coming from host side */
	if (priv->dp_support)
		regmap_update_bits(priv->dp, DP_SOURCE, DP_CONTROL_FROM_SOC, 0);

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
