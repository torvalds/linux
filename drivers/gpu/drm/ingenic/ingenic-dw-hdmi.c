// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 * Copyright (C) 2019, 2020 Paul Boddie <paul@boddie.org.uk>
 *
 * Derived from dw_hdmi-imx.c with i.MX portions removed.
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>

static const struct dw_hdmi_mpll_config ingenic_mpll_cfg[] = {
	{ 45250000,  { { 0x01e0, 0x0000 }, { 0x21e1, 0x0000 }, { 0x41e2, 0x0000 } } },
	{ 92500000,  { { 0x0140, 0x0005 }, { 0x2141, 0x0005 }, { 0x4142, 0x0005 } } },
	{ 148500000, { { 0x00a0, 0x000a }, { 0x20a1, 0x000a }, { 0x40a2, 0x000a } } },
	{ 216000000, { { 0x00a0, 0x000a }, { 0x2001, 0x000f }, { 0x4002, 0x000f } } },
	{ ~0UL,      { { 0x0000, 0x0000 }, { 0x0000, 0x0000 }, { 0x0000, 0x0000 } } }
};

static const struct dw_hdmi_curr_ctrl ingenic_cur_ctr[] = {
	/*pixelclk     bpp8    bpp10   bpp12 */
	{ 54000000,  { 0x091c, 0x091c, 0x06dc } },
	{ 58400000,  { 0x091c, 0x06dc, 0x06dc } },
	{ 72000000,  { 0x06dc, 0x06dc, 0x091c } },
	{ 74250000,  { 0x06dc, 0x0b5c, 0x091c } },
	{ 118800000, { 0x091c, 0x091c, 0x06dc } },
	{ 216000000, { 0x06dc, 0x0b5c, 0x091c } },
	{ ~0UL,      { 0x0000, 0x0000, 0x0000 } },
};

/*
 * Resistance term 133Ohm Cfg
 * PREEMP config 0.00
 * TX/CK level 10
 */
static const struct dw_hdmi_phy_config ingenic_phy_config[] = {
	/*pixelclk   symbol   term   vlev */
	{ 216000000, 0x800d, 0x0005, 0x01ad},
	{ ~0UL,      0x0000, 0x0000, 0x0000}
};

static enum drm_mode_status
ingenic_dw_hdmi_mode_valid(struct dw_hdmi *hdmi, void *data,
			   const struct drm_display_info *info,
			   const struct drm_display_mode *mode)
{
	if (mode->clock < 13500)
		return MODE_CLOCK_LOW;
	/* FIXME: Hardware is capable of 270MHz, but setup data is missing. */
	if (mode->clock > 216000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static struct dw_hdmi_plat_data ingenic_dw_hdmi_plat_data = {
	.mpll_cfg   = ingenic_mpll_cfg,
	.cur_ctr    = ingenic_cur_ctr,
	.phy_config = ingenic_phy_config,
	.mode_valid = ingenic_dw_hdmi_mode_valid,
	.output_port	= 1,
};

static const struct of_device_id ingenic_dw_hdmi_dt_ids[] = {
	{ .compatible = "ingenic,jz4780-dw-hdmi" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, ingenic_dw_hdmi_dt_ids);

static void ingenic_dw_hdmi_cleanup(void *data)
{
	struct dw_hdmi *hdmi = (struct dw_hdmi *)data;

	dw_hdmi_remove(hdmi);
}

static int ingenic_dw_hdmi_probe(struct platform_device *pdev)
{
	struct dw_hdmi *hdmi;

	hdmi = dw_hdmi_probe(pdev, &ingenic_dw_hdmi_plat_data);
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);

	return devm_add_action_or_reset(&pdev->dev, ingenic_dw_hdmi_cleanup, hdmi);
}

static struct platform_driver ingenic_dw_hdmi_driver = {
	.probe  = ingenic_dw_hdmi_probe,
	.driver = {
		.name = "dw-hdmi-ingenic",
		.of_match_table = ingenic_dw_hdmi_dt_ids,
	},
};
module_platform_driver(ingenic_dw_hdmi_driver);

MODULE_DESCRIPTION("JZ4780 Specific DW-HDMI Driver Extension");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dw-hdmi-ingenic");
