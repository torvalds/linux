// SPDX-License-Identifier: GPL-2.0-only
/*
 * LCD panel driver for Sharp LS037V7DW01
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>

#include "../dss/omapdss.h"

struct panel_drv_data {
	struct omap_dss_device dssdev;
	struct regulator *vcc;

	struct videomode vm;

	struct gpio_desc *resb_gpio;	/* low = reset active min 20 us */
	struct gpio_desc *ini_gpio;	/* high = power on */
	struct gpio_desc *mo_gpio;	/* low = 480x640, high = 240x320 */
	struct gpio_desc *lr_gpio;	/* high = conventional horizontal scanning */
	struct gpio_desc *ud_gpio;	/* high = conventional vertical scanning */
};

static const struct videomode sharp_ls_vm = {
	.hactive = 480,
	.vactive = 640,

	.pixelclock	= 19200000,

	.hsync_len	= 2,
	.hfront_porch	= 1,
	.hback_porch	= 28,

	.vsync_len	= 1,
	.vfront_porch	= 1,
	.vback_porch	= 1,

	.flags		= DISPLAY_FLAGS_HSYNC_LOW | DISPLAY_FLAGS_VSYNC_LOW,
};

#define to_panel_data(p) container_of(p, struct panel_drv_data, dssdev)

static int sharp_ls_connect(struct omap_dss_device *src,
			    struct omap_dss_device *dst)
{
	return 0;
}

static void sharp_ls_disconnect(struct omap_dss_device *src,
				struct omap_dss_device *dst)
{
}

static void sharp_ls_pre_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);
	int r;

	if (ddata->vcc) {
		r = regulator_enable(ddata->vcc);
		if (r)
			dev_err(dssdev->dev, "%s: failed to enable regulator\n",
				__func__);
	}
}

static void sharp_ls_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (ddata->resb_gpio)
		gpiod_set_value_cansleep(ddata->resb_gpio, 1);

	if (ddata->ini_gpio)
		gpiod_set_value_cansleep(ddata->ini_gpio, 1);
}

static void sharp_ls_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (ddata->ini_gpio)
		gpiod_set_value_cansleep(ddata->ini_gpio, 0);

	if (ddata->resb_gpio)
		gpiod_set_value_cansleep(ddata->resb_gpio, 0);

	/* wait at least 5 vsyncs after disabling the LCD */
	msleep(100);
}

static void sharp_ls_post_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	if (ddata->vcc)
		regulator_disable(ddata->vcc);
}

static int sharp_ls_get_modes(struct omap_dss_device *dssdev,
			      struct drm_connector *connector)
{
	struct panel_drv_data *ddata = to_panel_data(dssdev);

	return omapdss_display_get_modes(connector, &ddata->vm);
}

static const struct omap_dss_device_ops sharp_ls_ops = {
	.connect	= sharp_ls_connect,
	.disconnect	= sharp_ls_disconnect,

	.pre_enable	= sharp_ls_pre_enable,
	.enable		= sharp_ls_enable,
	.disable	= sharp_ls_disable,
	.post_disable	= sharp_ls_post_disable,

	.get_modes	= sharp_ls_get_modes,
};

static  int sharp_ls_get_gpio_of(struct device *dev, int index, int val,
	const char *desc, struct gpio_desc **gpiod)
{
	struct gpio_desc *gd;

	*gpiod = NULL;

	gd = devm_gpiod_get_index(dev, desc, index, GPIOD_OUT_LOW);
	if (IS_ERR(gd))
		return PTR_ERR(gd);

	*gpiod = gd;
	return 0;
}

static int sharp_ls_probe_of(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	int r;

	ddata->vcc = devm_regulator_get(&pdev->dev, "envdd");
	if (IS_ERR(ddata->vcc)) {
		dev_err(&pdev->dev, "failed to get regulator\n");
		return PTR_ERR(ddata->vcc);
	}

	/* lcd INI */
	r = sharp_ls_get_gpio_of(&pdev->dev, 0, 0, "enable", &ddata->ini_gpio);
	if (r)
		return r;

	/* lcd RESB */
	r = sharp_ls_get_gpio_of(&pdev->dev, 0, 0, "reset", &ddata->resb_gpio);
	if (r)
		return r;

	/* lcd MO */
	r = sharp_ls_get_gpio_of(&pdev->dev, 0, 0, "mode", &ddata->mo_gpio);
	if (r)
		return r;

	/* lcd LR */
	r = sharp_ls_get_gpio_of(&pdev->dev, 1, 1, "mode", &ddata->lr_gpio);
	if (r)
		return r;

	/* lcd UD */
	r = sharp_ls_get_gpio_of(&pdev->dev, 2, 1, "mode", &ddata->ud_gpio);
	if (r)
		return r;

	return 0;
}

static int sharp_ls_probe(struct platform_device *pdev)
{
	struct panel_drv_data *ddata;
	struct omap_dss_device *dssdev;
	int r;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (ddata == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, ddata);

	r = sharp_ls_probe_of(pdev);
	if (r)
		return r;

	ddata->vm = sharp_ls_vm;

	dssdev = &ddata->dssdev;
	dssdev->dev = &pdev->dev;
	dssdev->ops = &sharp_ls_ops;
	dssdev->type = OMAP_DISPLAY_TYPE_DPI;
	dssdev->display = true;
	dssdev->owner = THIS_MODULE;
	dssdev->of_ports = BIT(0);
	dssdev->ops_flags = OMAP_DSS_DEVICE_OP_MODES;

	/*
	 * Note: According to the panel documentation:
	 * DATA needs to be driven on the FALLING edge
	 */
	dssdev->bus_flags = DRM_BUS_FLAG_DE_HIGH
			  | DRM_BUS_FLAG_SYNC_DRIVE_NEGEDGE
			  | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;

	omapdss_display_init(dssdev);
	omapdss_device_register(dssdev);

	return 0;
}

static int __exit sharp_ls_remove(struct platform_device *pdev)
{
	struct panel_drv_data *ddata = platform_get_drvdata(pdev);
	struct omap_dss_device *dssdev = &ddata->dssdev;

	omapdss_device_unregister(dssdev);

	if (omapdss_device_is_enabled(dssdev)) {
		sharp_ls_disable(dssdev);
		sharp_ls_post_disable(dssdev);
	}

	return 0;
}

static const struct of_device_id sharp_ls_of_match[] = {
	{ .compatible = "omapdss,sharp,ls037v7dw01", },
	{},
};

MODULE_DEVICE_TABLE(of, sharp_ls_of_match);

static struct platform_driver sharp_ls_driver = {
	.probe = sharp_ls_probe,
	.remove = __exit_p(sharp_ls_remove),
	.driver = {
		.name = "panel-sharp-ls037v7dw01",
		.of_match_table = sharp_ls_of_match,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(sharp_ls_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Sharp LS037V7DW01 Panel Driver");
MODULE_LICENSE("GPL");
