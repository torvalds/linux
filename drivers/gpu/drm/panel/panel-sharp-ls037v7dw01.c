// SPDX-License-Identifier: GPL-2.0
/*
 * Sharp LS037V7DW01 LCD Panel Driver
 *
 * Copyright (C) 2019 Texas Instruments Incorporated
 *
 * Based on the omapdrm-specific panel-sharp-ls037v7dw01 driver
 *
 * Copyright (C) 2013 Texas Instruments Incorporated
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

struct ls037v7dw01_panel {
	struct drm_panel panel;
	struct platform_device *pdev;

	struct regulator *vdd;
	struct gpio_desc *resb_gpio;	/* low = reset active min 20 us */
	struct gpio_desc *ini_gpio;	/* high = power on */
	struct gpio_desc *mo_gpio;	/* low = 480x640, high = 240x320 */
	struct gpio_desc *lr_gpio;	/* high = conventional horizontal scanning */
	struct gpio_desc *ud_gpio;	/* high = conventional vertical scanning */
};

#define to_ls037v7dw01_device(p) \
	container_of(p, struct ls037v7dw01_panel, panel)

static int ls037v7dw01_disable(struct drm_panel *panel)
{
	struct ls037v7dw01_panel *lcd = to_ls037v7dw01_device(panel);

	gpiod_set_value_cansleep(lcd->ini_gpio, 0);
	gpiod_set_value_cansleep(lcd->resb_gpio, 0);

	/* Wait at least 5 vsyncs after disabling the LCD. */
	msleep(100);

	return 0;
}

static int ls037v7dw01_unprepare(struct drm_panel *panel)
{
	struct ls037v7dw01_panel *lcd = to_ls037v7dw01_device(panel);

	regulator_disable(lcd->vdd);
	return 0;
}

static int ls037v7dw01_prepare(struct drm_panel *panel)
{
	struct ls037v7dw01_panel *lcd = to_ls037v7dw01_device(panel);
	int ret;

	ret = regulator_enable(lcd->vdd);
	if (ret < 0)
		dev_err(&lcd->pdev->dev, "%s: failed to enable regulator\n",
			__func__);

	return ret;
}

static int ls037v7dw01_enable(struct drm_panel *panel)
{
	struct ls037v7dw01_panel *lcd = to_ls037v7dw01_device(panel);

	/* Wait couple of vsyncs before enabling the LCD. */
	msleep(50);

	gpiod_set_value_cansleep(lcd->resb_gpio, 1);
	gpiod_set_value_cansleep(lcd->ini_gpio, 1);

	return 0;
}

static const struct drm_display_mode ls037v7dw01_mode = {
	.clock = 19200,
	.hdisplay = 480,
	.hsync_start = 480 + 1,
	.hsync_end = 480 + 1 + 2,
	.htotal = 480 + 1 + 2 + 28,
	.vdisplay = 640,
	.vsync_start = 640 + 1,
	.vsync_end = 640 + 1 + 1,
	.vtotal = 640 + 1 + 1 + 1,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
	.width_mm = 56,
	.height_mm = 75,
};

static int ls037v7dw01_get_modes(struct drm_panel *panel,
				 struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &ls037v7dw01_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = ls037v7dw01_mode.width_mm;
	connector->display_info.height_mm = ls037v7dw01_mode.height_mm;
	/*
	 * FIXME: According to the datasheet pixel data is sampled on the
	 * rising edge of the clock, but the code running on the SDP3430
	 * indicates sampling on the negative edge. This should be tested on a
	 * real device.
	 */
	connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH
					  | DRM_BUS_FLAG_SYNC_SAMPLE_POSEDGE
					  | DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE;

	return 1;
}

static const struct drm_panel_funcs ls037v7dw01_funcs = {
	.disable = ls037v7dw01_disable,
	.unprepare = ls037v7dw01_unprepare,
	.prepare = ls037v7dw01_prepare,
	.enable = ls037v7dw01_enable,
	.get_modes = ls037v7dw01_get_modes,
};

static int ls037v7dw01_probe(struct platform_device *pdev)
{
	struct ls037v7dw01_panel *lcd;

	lcd = devm_kzalloc(&pdev->dev, sizeof(*lcd), GFP_KERNEL);
	if (!lcd)
		return -ENOMEM;

	platform_set_drvdata(pdev, lcd);
	lcd->pdev = pdev;

	lcd->vdd = devm_regulator_get(&pdev->dev, "envdd");
	if (IS_ERR(lcd->vdd))
		return dev_err_probe(&pdev->dev, PTR_ERR(lcd->vdd),
				     "failed to get regulator\n");

	lcd->ini_gpio = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->ini_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(lcd->ini_gpio),
				     "failed to get enable gpio\n");

	lcd->resb_gpio = devm_gpiod_get(&pdev->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(lcd->resb_gpio))
		return dev_err_probe(&pdev->dev, PTR_ERR(lcd->resb_gpio),
				     "failed to get reset gpio\n");

	lcd->mo_gpio = devm_gpiod_get_index(&pdev->dev, "mode", 0,
					    GPIOD_OUT_LOW);
	if (IS_ERR(lcd->mo_gpio)) {
		dev_err(&pdev->dev, "failed to get mode[0] gpio\n");
		return PTR_ERR(lcd->mo_gpio);
	}

	lcd->lr_gpio = devm_gpiod_get_index(&pdev->dev, "mode", 1,
					    GPIOD_OUT_LOW);
	if (IS_ERR(lcd->lr_gpio)) {
		dev_err(&pdev->dev, "failed to get mode[1] gpio\n");
		return PTR_ERR(lcd->lr_gpio);
	}

	lcd->ud_gpio = devm_gpiod_get_index(&pdev->dev, "mode", 2,
					    GPIOD_OUT_LOW);
	if (IS_ERR(lcd->ud_gpio)) {
		dev_err(&pdev->dev, "failed to get mode[2] gpio\n");
		return PTR_ERR(lcd->ud_gpio);
	}

	drm_panel_init(&lcd->panel, &pdev->dev, &ls037v7dw01_funcs,
		       DRM_MODE_CONNECTOR_DPI);

	drm_panel_add(&lcd->panel);

	return 0;
}

static void ls037v7dw01_remove(struct platform_device *pdev)
{
	struct ls037v7dw01_panel *lcd = platform_get_drvdata(pdev);

	drm_panel_remove(&lcd->panel);
	drm_panel_disable(&lcd->panel);
	drm_panel_unprepare(&lcd->panel);
}

static const struct of_device_id ls037v7dw01_of_match[] = {
	{ .compatible = "sharp,ls037v7dw01", },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, ls037v7dw01_of_match);

static struct platform_driver ls037v7dw01_driver = {
	.probe		= ls037v7dw01_probe,
	.remove		= ls037v7dw01_remove,
	.driver		= {
		.name = "panel-sharp-ls037v7dw01",
		.of_match_table = ls037v7dw01_of_match,
	},
};

module_platform_driver(ls037v7dw01_driver);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@ti.com>");
MODULE_DESCRIPTION("Sharp LS037V7DW01 Panel Driver");
MODULE_LICENSE("GPL");
