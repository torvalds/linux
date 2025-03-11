// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2022 Pengutronix, Lucas Stach <kernel@pengutronix.de>
 */

#include <linux/clk.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <drm/bridge/dw_hdmi.h>
#include <drm/drm_modes.h>

struct imx8mp_hdmi {
	struct dw_hdmi_plat_data plat_data;
	struct dw_hdmi *dw_hdmi;
	struct clk *pixclk;
};

static enum drm_mode_status
imx8mp_hdmi_mode_valid(struct dw_hdmi *dw_hdmi, void *data,
		       const struct drm_display_info *info,
		       const struct drm_display_mode *mode)
{
	struct imx8mp_hdmi *hdmi = (struct imx8mp_hdmi *)data;
	long round_rate;

	if (mode->clock < 13500)
		return MODE_CLOCK_LOW;

	if (mode->clock > 297000)
		return MODE_CLOCK_HIGH;

	round_rate = clk_round_rate(hdmi->pixclk, mode->clock * 1000);
	/* imx8mp's pixel clock generator (fsl-samsung-hdmi) cannot generate
	 * all possible frequencies, so allow some tolerance to support more
	 * modes.
	 * Allow 0.5% difference allowed in various standards (VESA, CEA861)
	 * 0.5% = 5/1000 tolerance (mode->clock is 1/1000)
	 */
	if (abs(round_rate - mode->clock * 1000) > mode->clock * 5)
		return MODE_CLOCK_RANGE;

	/* We don't support double-clocked and Interlaced modes */
	if ((mode->flags & DRM_MODE_FLAG_DBLCLK) ||
	    (mode->flags & DRM_MODE_FLAG_INTERLACE))
		return MODE_BAD;

	return MODE_OK;
}

static int imx8mp_hdmi_phy_init(struct dw_hdmi *dw_hdmi, void *data,
				const struct drm_display_info *display,
				const struct drm_display_mode *mode)
{
	return 0;
}

static void imx8mp_hdmi_phy_disable(struct dw_hdmi *dw_hdmi, void *data)
{
}

static void im8mp_hdmi_phy_setup_hpd(struct dw_hdmi *hdmi, void *data)
{
	/*
	 * Just release PHY core from reset, all other power management is done
	 * by the PHY driver.
	 */
	dw_hdmi_phy_gen1_reset(hdmi);

	dw_hdmi_phy_setup_hpd(hdmi, data);
}

static const struct dw_hdmi_phy_ops imx8mp_hdmi_phy_ops = {
	.init		= imx8mp_hdmi_phy_init,
	.disable	= imx8mp_hdmi_phy_disable,
	.setup_hpd	= im8mp_hdmi_phy_setup_hpd,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
};

static int imx8mp_dw_hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_hdmi_plat_data *plat_data;
	struct imx8mp_hdmi *hdmi;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	plat_data = &hdmi->plat_data;

	hdmi->pixclk = devm_clk_get(dev, "pix");
	if (IS_ERR(hdmi->pixclk))
		return dev_err_probe(dev, PTR_ERR(hdmi->pixclk),
				     "Unable to get pixel clock\n");

	plat_data->mode_valid = imx8mp_hdmi_mode_valid;
	plat_data->phy_ops = &imx8mp_hdmi_phy_ops;
	plat_data->phy_name = "SAMSUNG HDMI TX PHY";
	plat_data->priv_data = hdmi;
	plat_data->phy_force_vendor = true;

	hdmi->dw_hdmi = dw_hdmi_probe(pdev, plat_data);
	if (IS_ERR(hdmi->dw_hdmi))
		return PTR_ERR(hdmi->dw_hdmi);

	platform_set_drvdata(pdev, hdmi);

	return 0;
}

static void imx8mp_dw_hdmi_remove(struct platform_device *pdev)
{
	struct imx8mp_hdmi *hdmi = platform_get_drvdata(pdev);

	dw_hdmi_remove(hdmi->dw_hdmi);
}

static int imx8mp_dw_hdmi_pm_suspend(struct device *dev)
{
	return 0;
}

static int imx8mp_dw_hdmi_pm_resume(struct device *dev)
{
	struct imx8mp_hdmi *hdmi = dev_get_drvdata(dev);

	dw_hdmi_resume(hdmi->dw_hdmi);

	return 0;
}

static const struct dev_pm_ops imx8mp_dw_hdmi_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(imx8mp_dw_hdmi_pm_suspend, imx8mp_dw_hdmi_pm_resume)
};

static const struct of_device_id imx8mp_dw_hdmi_of_table[] = {
	{ .compatible = "fsl,imx8mp-hdmi-tx" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx8mp_dw_hdmi_of_table);

static struct platform_driver imx8mp_dw_hdmi_platform_driver = {
	.probe		= imx8mp_dw_hdmi_probe,
	.remove		= imx8mp_dw_hdmi_remove,
	.driver		= {
		.name	= "imx8mp-dw-hdmi-tx",
		.of_match_table = imx8mp_dw_hdmi_of_table,
		.pm = pm_ptr(&imx8mp_dw_hdmi_pm_ops),
	},
};

module_platform_driver(imx8mp_dw_hdmi_platform_driver);

MODULE_DESCRIPTION("i.MX8MP HDMI encoder driver");
MODULE_LICENSE("GPL");
