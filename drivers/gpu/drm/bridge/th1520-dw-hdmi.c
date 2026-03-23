// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Icenowy Zheng <uwu@icenowy.me>
 *
 * Based on rcar_dw_hdmi.c, which is:
 *   Copyright (C) 2016 Renesas Electronics Corporation
 * Based on imx8mp-hdmi-tx.c, which is:
 *   Copyright (C) 2022 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_modes.h>

#define TH1520_HDMI_PHY_OPMODE_PLLCFG	0x06	/* Mode of operation and PLL dividers */
#define TH1520_HDMI_PHY_CKSYMTXCTRL	0x09	/* Clock Symbol and Transmitter Control Register */
#define TH1520_HDMI_PHY_VLEVCTRL	0x0e	/* Voltage Level Control Register */
#define TH1520_HDMI_PHY_PLLCURRGMPCTRL	0x10	/* PLL current and Gmp (conductance) */
#define TH1520_HDMI_PHY_PLLDIVCTRL	0x11	/* PLL dividers */
#define TH1520_HDMI_PHY_TXTERM		0x19	/* Transmission Termination Register */

struct th1520_hdmi_phy_params {
	unsigned long mpixelclock;
	u16 opmode_pllcfg;
	u16 pllcurrgmpctrl;
	u16 plldivctrl;
	u16 cksymtxctrl;
	u16 vlevctrl;
	u16 txterm;
};

static const struct th1520_hdmi_phy_params th1520_hdmi_phy_params[] = {
	{ 35500000,  0x0003, 0x0283, 0x0628, 0x8088, 0x01a0, 0x0007 },
	{ 44900000,  0x0003, 0x0285, 0x0228, 0x8088, 0x01a0, 0x0007 },
	{ 71000000,  0x0002, 0x1183, 0x0614, 0x8088, 0x01a0, 0x0007 },
	{ 90000000,  0x0002, 0x1142, 0x0214, 0x8088, 0x01a0, 0x0007 },
	{ 121750000, 0x0001, 0x20c0, 0x060a, 0x8088, 0x01a0, 0x0007 },
	{ 165000000, 0x0001, 0x2080, 0x020a, 0x8088, 0x01a0, 0x0007 },
	{ 198000000, 0x0000, 0x3040, 0x0605, 0x83c8, 0x0120, 0x0004 },
	{ 297000000, 0x0000, 0x3041, 0x0205, 0x81dc, 0x0200, 0x0005 },
	{ 371250000, 0x0640, 0x3041, 0x0205, 0x80f6, 0x0140, 0x0000 },
	{ 495000000, 0x0640, 0x3080, 0x0005, 0x80f6, 0x0140, 0x0000 },
	{ 594000000, 0x0640, 0x3080, 0x0005, 0x80fa, 0x01e0, 0x0004 },
};

struct th1520_hdmi {
	struct dw_hdmi_plat_data plat_data;
	struct dw_hdmi *dw_hdmi;
	struct clk *pixclk;
	struct reset_control *mainrst, *prst;
};

static enum drm_mode_status
th1520_hdmi_mode_valid(struct dw_hdmi *hdmi, void *data,
		       const struct drm_display_info *info,
		       const struct drm_display_mode *mode)
{
	/*
	 * The maximum supported clock frequency is 594 MHz, as shown in the PHY
	 * parameters table.
	 */
	if (mode->clock > 594000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static void th1520_hdmi_phy_set_params(struct dw_hdmi *hdmi,
				const struct th1520_hdmi_phy_params *params)
{
	dw_hdmi_phy_i2c_write(hdmi, params->opmode_pllcfg,
			      TH1520_HDMI_PHY_OPMODE_PLLCFG);
	dw_hdmi_phy_i2c_write(hdmi, params->pllcurrgmpctrl,
			      TH1520_HDMI_PHY_PLLCURRGMPCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params->plldivctrl,
			      TH1520_HDMI_PHY_PLLDIVCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params->vlevctrl,
			      TH1520_HDMI_PHY_VLEVCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params->cksymtxctrl,
			      TH1520_HDMI_PHY_CKSYMTXCTRL);
	dw_hdmi_phy_i2c_write(hdmi, params->txterm,
			      TH1520_HDMI_PHY_TXTERM);
}

static int th1520_hdmi_phy_configure(struct dw_hdmi *hdmi, void *data,
				     unsigned long mpixelclock)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(th1520_hdmi_phy_params); i++) {
		if (mpixelclock <= th1520_hdmi_phy_params[i].mpixelclock) {
			th1520_hdmi_phy_set_params(hdmi,
						   &th1520_hdmi_phy_params[i]);
			return 0;
		}
	}

	return -EINVAL;
}

static int th1520_dw_hdmi_probe(struct platform_device *pdev)
{
	struct th1520_hdmi *hdmi;
	struct dw_hdmi_plat_data *plat_data;
	struct device *dev = &pdev->dev;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	plat_data = &hdmi->plat_data;

	hdmi->pixclk = devm_clk_get_enabled(dev, "pix");
	if (IS_ERR(hdmi->pixclk))
		return dev_err_probe(dev, PTR_ERR(hdmi->pixclk),
				     "Unable to get pixel clock\n");

	hdmi->mainrst = devm_reset_control_get_exclusive_deasserted(dev, "main");
	if (IS_ERR(hdmi->mainrst))
		return dev_err_probe(dev, PTR_ERR(hdmi->mainrst),
				     "Unable to get main reset\n");

	hdmi->prst = devm_reset_control_get_exclusive_deasserted(dev, "apb");
	if (IS_ERR(hdmi->prst))
		return dev_err_probe(dev, PTR_ERR(hdmi->prst),
				     "Unable to get apb reset\n");

	plat_data->output_port = 1;
	plat_data->mode_valid = th1520_hdmi_mode_valid;
	plat_data->configure_phy = th1520_hdmi_phy_configure;
	plat_data->priv_data = hdmi;

	hdmi->dw_hdmi = dw_hdmi_probe(pdev, plat_data);
	if (IS_ERR(hdmi))
		return PTR_ERR(hdmi);

	platform_set_drvdata(pdev, hdmi);

	return 0;
}

static void th1520_dw_hdmi_remove(struct platform_device *pdev)
{
	struct dw_hdmi *hdmi = platform_get_drvdata(pdev);

	dw_hdmi_remove(hdmi);
}

static const struct of_device_id th1520_dw_hdmi_of_table[] = {
	{ .compatible = "thead,th1520-dw-hdmi" },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, th1520_dw_hdmi_of_table);

static struct platform_driver th1520_dw_hdmi_platform_driver = {
	.probe		= th1520_dw_hdmi_probe,
	.remove		= th1520_dw_hdmi_remove,
	.driver		= {
		.name	= "th1520-dw-hdmi",
		.of_match_table = th1520_dw_hdmi_of_table,
	},
};

module_platform_driver(th1520_dw_hdmi_platform_driver);

MODULE_AUTHOR("Icenowy Zheng <uwu@icenowy.me>");
MODULE_DESCRIPTION("T-Head TH1520 HDMI Encoder Driver");
MODULE_LICENSE("GPL");
