/*
 * R-Car Gen3 HDMI PHY
 *
 * Copyright (C) 2016 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/bridge/dw_hdmi.h>

#define RCAR_HDMI_PHY_OPMODE_PLLCFG	0x06	/* Mode of operation and PLL dividers */
#define RCAR_HDMI_PHY_PLLCURRGMPCTRL	0x10	/* PLL current and Gmp (conductance) */
#define RCAR_HDMI_PHY_PLLDIVCTRL	0x11	/* PLL dividers */

struct rcar_hdmi_phy_params {
	unsigned long mpixelclock;
	u16 opmode_div;	/* Mode of operation and PLL dividers */
	u16 curr_gmp;	/* PLL current and Gmp (conductance) */
	u16 div;	/* PLL dividers */
};

static const struct rcar_hdmi_phy_params rcar_hdmi_phy_params[] = {
	{ 35500000,  0x0003, 0x0344, 0x0328 },
	{ 44900000,  0x0003, 0x0285, 0x0128 },
	{ 71000000,  0x0002, 0x1184, 0x0314 },
	{ 90000000,  0x0002, 0x1144, 0x0114 },
	{ 140250000, 0x0001, 0x20c4, 0x030a },
	{ 182750000, 0x0001, 0x2084, 0x010a },
	{ 281250000, 0x0000, 0x0084, 0x0305 },
	{ 297000000, 0x0000, 0x0084, 0x0105 },
	{ ~0UL,      0x0000, 0x0000, 0x0000 },
};

static int rcar_hdmi_phy_configure(struct dw_hdmi *hdmi,
				   const struct dw_hdmi_plat_data *pdata,
				   unsigned long mpixelclock)
{
	const struct rcar_hdmi_phy_params *params = rcar_hdmi_phy_params;

	for (; params->mpixelclock != ~0UL; ++params) {
		if (mpixelclock <= params->mpixelclock)
			break;
	}

	if (params->mpixelclock == ~0UL)
		return -EINVAL;

	dw_hdmi_phy_i2c_write(hdmi, params->opmode_div,
			      RCAR_HDMI_PHY_OPMODE_PLLCFG);
	dw_hdmi_phy_i2c_write(hdmi, params->curr_gmp,
			      RCAR_HDMI_PHY_PLLCURRGMPCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params->div, RCAR_HDMI_PHY_PLLDIVCTRL);

	return 0;
}

static const struct dw_hdmi_plat_data rcar_dw_hdmi_plat_data = {
	.configure_phy	= rcar_hdmi_phy_configure,
};

static int rcar_dw_hdmi_probe(struct platform_device *pdev)
{
	return dw_hdmi_probe(pdev, &rcar_dw_hdmi_plat_data);
}

static int rcar_dw_hdmi_remove(struct platform_device *pdev)
{
	dw_hdmi_remove(pdev);

	return 0;
}

static const struct of_device_id rcar_dw_hdmi_of_table[] = {
	{ .compatible = "renesas,rcar-gen3-hdmi" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_dw_hdmi_of_table);

static struct platform_driver rcar_dw_hdmi_platform_driver = {
	.probe		= rcar_dw_hdmi_probe,
	.remove		= rcar_dw_hdmi_remove,
	.driver		= {
		.name	= "rcar-dw-hdmi",
		.of_match_table = rcar_dw_hdmi_of_table,
	},
};

module_platform_driver(rcar_dw_hdmi_platform_driver);

MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_DESCRIPTION("Renesas R-Car Gen3 HDMI Encoder Driver");
MODULE_LICENSE("GPL");
