// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/phy/phy.h>
#include <linux/bitfield.h>

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_device.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

#include "meson_drv.h"
#include "meson_dw_mipi_dsi.h"
#include "meson_registers.h"
#include "meson_venc.h"

#define DRIVER_NAME "meson-dw-mipi-dsi"
#define DRIVER_DESC "Amlogic Meson MIPI-DSI DRM driver"

struct meson_dw_mipi_dsi {
	struct meson_drm *priv;
	struct device *dev;
	void __iomem *base;
	struct phy *phy;
	union phy_configure_opts phy_opts;
	struct dw_mipi_dsi *dmd;
	struct dw_mipi_dsi_plat_data pdata;
	struct mipi_dsi_device *dsi_device;
	const struct drm_display_mode *mode;
	struct clk *bit_clk;
	struct clk *px_clk;
	struct reset_control *top_rst;
};

#define encoder_to_meson_dw_mipi_dsi(x) \
	container_of(x, struct meson_dw_mipi_dsi, encoder)

static void meson_dw_mipi_dsi_hw_init(struct meson_dw_mipi_dsi *mipi_dsi)
{
	/* Software reset */
	writel_bits_relaxed(MIPI_DSI_TOP_SW_RESET_DWC | MIPI_DSI_TOP_SW_RESET_INTR |
			    MIPI_DSI_TOP_SW_RESET_DPI | MIPI_DSI_TOP_SW_RESET_TIMING,
			    MIPI_DSI_TOP_SW_RESET_DWC | MIPI_DSI_TOP_SW_RESET_INTR |
			    MIPI_DSI_TOP_SW_RESET_DPI | MIPI_DSI_TOP_SW_RESET_TIMING,
			    mipi_dsi->base + MIPI_DSI_TOP_SW_RESET);
	writel_bits_relaxed(MIPI_DSI_TOP_SW_RESET_DWC | MIPI_DSI_TOP_SW_RESET_INTR |
			    MIPI_DSI_TOP_SW_RESET_DPI | MIPI_DSI_TOP_SW_RESET_TIMING,
			    0, mipi_dsi->base + MIPI_DSI_TOP_SW_RESET);

	/* Enable clocks */
	writel_bits_relaxed(MIPI_DSI_TOP_CLK_SYSCLK_EN | MIPI_DSI_TOP_CLK_PIXCLK_EN,
			    MIPI_DSI_TOP_CLK_SYSCLK_EN | MIPI_DSI_TOP_CLK_PIXCLK_EN,
			    mipi_dsi->base + MIPI_DSI_TOP_CLK_CNTL);

	/* Take memory out of power down */
	writel_relaxed(0, mipi_dsi->base + MIPI_DSI_TOP_MEM_PD);
}

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;
	unsigned int dpi_data_format, venc_data_width;
	int ret;

	/* Set the bit clock rate to hs_clk_rate */
	ret = clk_set_rate(mipi_dsi->bit_clk,
			   mipi_dsi->phy_opts.mipi_dphy.hs_clk_rate);
	if (ret) {
		dev_err(mipi_dsi->dev, "Failed to set DSI Bit clock rate %lu (ret %d)\n",
			mipi_dsi->phy_opts.mipi_dphy.hs_clk_rate, ret);
		return ret;
	}

	/* Make sure the rate of the bit clock is not modified by someone else */
	ret = clk_rate_exclusive_get(mipi_dsi->bit_clk);
	if (ret) {
		dev_err(mipi_dsi->dev,
			"Failed to set the exclusivity on the bit clock rate (ret %d)\n", ret);
		return ret;
	}

	ret = clk_set_rate(mipi_dsi->px_clk, mipi_dsi->mode->clock * 1000);

	if (ret) {
		dev_err(mipi_dsi->dev, "Failed to set DSI Pixel clock rate %u (%d)\n",
			mipi_dsi->mode->clock * 1000, ret);
		return ret;
	}

	switch (mipi_dsi->dsi_device->format) {
	case MIPI_DSI_FMT_RGB888:
		dpi_data_format = DPI_COLOR_24BIT;
		venc_data_width = VENC_IN_COLOR_24B;
		break;
	case MIPI_DSI_FMT_RGB666:
		dpi_data_format = DPI_COLOR_18BIT_CFG_2;
		venc_data_width = VENC_IN_COLOR_18B;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
	case MIPI_DSI_FMT_RGB565:
		return -EINVAL;
	}

	/* Configure color format for DPI register */
	writel_relaxed(FIELD_PREP(MIPI_DSI_TOP_DPI_COLOR_MODE, dpi_data_format) |
		       FIELD_PREP(MIPI_DSI_TOP_IN_COLOR_MODE, venc_data_width) |
		       FIELD_PREP(MIPI_DSI_TOP_COMP2_SEL, 2) |
		       FIELD_PREP(MIPI_DSI_TOP_COMP1_SEL, 1) |
		       FIELD_PREP(MIPI_DSI_TOP_COMP0_SEL, 0),
			mipi_dsi->base + MIPI_DSI_TOP_CNTL);

	return phy_configure(mipi_dsi->phy, &mipi_dsi->phy_opts);
}

static void dw_mipi_dsi_phy_power_on(void *priv_data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	if (phy_power_on(mipi_dsi->phy))
		dev_warn(mipi_dsi->dev, "Failed to power on PHY\n");
}

static void dw_mipi_dsi_phy_power_off(void *priv_data)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	if (phy_power_off(mipi_dsi->phy))
		dev_warn(mipi_dsi->dev, "Failed to power off PHY\n");

	/* Remove the exclusivity on the bit clock rate */
	clk_rate_exclusive_put(mipi_dsi->bit_clk);
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;
	int bpp;

	mipi_dsi->mode = mode;

	bpp = mipi_dsi_pixel_format_to_bpp(mipi_dsi->dsi_device->format);

	phy_mipi_dphy_get_default_config(mode->clock * 1000,
					 bpp, mipi_dsi->dsi_device->lanes,
					 &mipi_dsi->phy_opts.mipi_dphy);

	*lane_mbps = DIV_ROUND_UP(mipi_dsi->phy_opts.mipi_dphy.hs_clk_rate, USEC_PER_SEC);

	return 0;
}

static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
			   struct dw_mipi_dsi_dphy_timing *timing)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	switch (mipi_dsi->mode->hdisplay) {
	case 240:
	case 768:
	case 1920:
	case 2560:
		timing->clk_lp2hs = 23;
		timing->clk_hs2lp = 38;
		timing->data_lp2hs = 15;
		timing->data_hs2lp = 9;
		break;

	default:
		timing->clk_lp2hs = 37;
		timing->clk_hs2lp = 135;
		timing->data_lp2hs = 50;
		timing->data_hs2lp = 3;
	}

	return 0;
}

static int
dw_mipi_dsi_get_esc_clk_rate(void *priv_data, unsigned int *esc_clk_rate)
{
	*esc_clk_rate = 4; /* Mhz */

	return 0;
}

static const struct dw_mipi_dsi_phy_ops meson_dw_mipi_dsi_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_get_timing,
	.get_esc_clk_rate = dw_mipi_dsi_get_esc_clk_rate,
};

static int meson_dw_mipi_dsi_host_attach(void *priv_data,
					 struct mipi_dsi_device *device)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;
	int ret;

	mipi_dsi->dsi_device = device;

	switch (device->format) {
	case MIPI_DSI_FMT_RGB888:
		break;
	case MIPI_DSI_FMT_RGB666:
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
	case MIPI_DSI_FMT_RGB565:
		dev_err(mipi_dsi->dev, "invalid pixel format %d\n", device->format);
		return -EINVAL;
	}

	ret = phy_init(mipi_dsi->phy);
	if (ret)
		return ret;

	meson_dw_mipi_dsi_hw_init(mipi_dsi);

	return 0;
}

static int meson_dw_mipi_dsi_host_detach(void *priv_data,
					 struct mipi_dsi_device *device)
{
	struct meson_dw_mipi_dsi *mipi_dsi = priv_data;

	if (device == mipi_dsi->dsi_device)
		mipi_dsi->dsi_device = NULL;
	else
		return -EINVAL;

	return phy_exit(mipi_dsi->phy);
}

static const struct dw_mipi_dsi_host_ops meson_dw_mipi_dsi_host_ops = {
	.attach = meson_dw_mipi_dsi_host_attach,
	.detach = meson_dw_mipi_dsi_host_detach,
};

static int meson_dw_mipi_dsi_probe(struct platform_device *pdev)
{
	struct meson_dw_mipi_dsi *mipi_dsi;
	struct device *dev = &pdev->dev;

	mipi_dsi = devm_kzalloc(dev, sizeof(*mipi_dsi), GFP_KERNEL);
	if (!mipi_dsi)
		return -ENOMEM;

	mipi_dsi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mipi_dsi->base))
		return PTR_ERR(mipi_dsi->base);

	mipi_dsi->phy = devm_phy_get(dev, "dphy");
	if (IS_ERR(mipi_dsi->phy))
		return dev_err_probe(dev, PTR_ERR(mipi_dsi->phy),
				     "failed to get mipi dphy\n");

	mipi_dsi->bit_clk = devm_clk_get_enabled(dev, "bit");
	if (IS_ERR(mipi_dsi->bit_clk)) {
		int ret = PTR_ERR(mipi_dsi->bit_clk);

		/* TOFIX GP0 on some platforms fails to lock in early boot, defer probe */
		if (ret == -EIO)
			ret = -EPROBE_DEFER;

		return dev_err_probe(dev, ret, "Unable to get enabled bit_clk\n");
	}

	mipi_dsi->px_clk = devm_clk_get_enabled(dev, "px");
	if (IS_ERR(mipi_dsi->px_clk))
		return dev_err_probe(dev, PTR_ERR(mipi_dsi->px_clk),
				     "Unable to get enabled px_clk\n");

	/*
	 * We use a TOP reset signal because the APB reset signal
	 * is handled by the TOP control registers.
	 */
	mipi_dsi->top_rst = devm_reset_control_get_exclusive(dev, "top");
	if (IS_ERR(mipi_dsi->top_rst))
		return dev_err_probe(dev, PTR_ERR(mipi_dsi->top_rst),
				     "Unable to get reset control\n");

	reset_control_assert(mipi_dsi->top_rst);
	usleep_range(10, 20);
	reset_control_deassert(mipi_dsi->top_rst);

	/* MIPI DSI Controller */

	mipi_dsi->dev = dev;
	mipi_dsi->pdata.base = mipi_dsi->base;
	mipi_dsi->pdata.max_data_lanes = 4;
	mipi_dsi->pdata.phy_ops = &meson_dw_mipi_dsi_phy_ops;
	mipi_dsi->pdata.host_ops = &meson_dw_mipi_dsi_host_ops;
	mipi_dsi->pdata.priv_data = mipi_dsi;
	platform_set_drvdata(pdev, mipi_dsi);

	mipi_dsi->dmd = dw_mipi_dsi_probe(pdev, &mipi_dsi->pdata);
	if (IS_ERR(mipi_dsi->dmd))
		return dev_err_probe(dev, PTR_ERR(mipi_dsi->dmd),
				     "Failed to probe dw_mipi_dsi\n");

	return 0;
}

static int meson_dw_mipi_dsi_remove(struct platform_device *pdev)
{
	struct meson_dw_mipi_dsi *mipi_dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi_remove(mipi_dsi->dmd);

	return 0;
}

static const struct of_device_id meson_dw_mipi_dsi_of_table[] = {
	{ .compatible = "amlogic,meson-g12a-dw-mipi-dsi", },
	{ }
};
MODULE_DEVICE_TABLE(of, meson_dw_mipi_dsi_of_table);

static struct platform_driver meson_dw_mipi_dsi_platform_driver = {
	.probe		= meson_dw_mipi_dsi_probe,
	.remove		= meson_dw_mipi_dsi_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= meson_dw_mipi_dsi_of_table,
	},
};
module_platform_driver(meson_dw_mipi_dsi_platform_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
