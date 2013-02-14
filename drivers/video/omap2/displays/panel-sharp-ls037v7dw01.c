/*
 * LCD panel driver for Sharp LS037V7DW01
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fb.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

static struct omap_video_timings sharp_ls_timings = {
	.x_res = 480,
	.y_res = 640,

	.pixel_clock	= 19200,

	.hsw		= 2,
	.hfp		= 1,
	.hbp		= 28,

	.vsw		= 1,
	.vfp		= 1,
	.vbp		= 1,

	.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
	.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
	.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
	.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
};

static inline struct panel_sharp_ls037v7dw01_data
*get_panel_data(const struct omap_dss_device *dssdev)
{
	return (struct panel_sharp_ls037v7dw01_data *) dssdev->data;
}

static int sharp_ls_panel_probe(struct omap_dss_device *dssdev)
{
	struct panel_sharp_ls037v7dw01_data *pd = get_panel_data(dssdev);
	int r;

	if (!pd)
		return -EINVAL;

	dssdev->panel.timings = sharp_ls_timings;

	if (gpio_is_valid(pd->mo_gpio)) {
		r = devm_gpio_request_one(dssdev->dev, pd->mo_gpio,
				GPIOF_OUT_INIT_LOW, "lcd MO");
		if (r)
			return r;
	}

	if (gpio_is_valid(pd->lr_gpio)) {
		r = devm_gpio_request_one(dssdev->dev, pd->lr_gpio,
				GPIOF_OUT_INIT_HIGH, "lcd LR");
		if (r)
			return r;
	}

	if (gpio_is_valid(pd->ud_gpio)) {
		r = devm_gpio_request_one(dssdev->dev, pd->ud_gpio,
				GPIOF_OUT_INIT_HIGH, "lcd UD");
		if (r)
			return r;
	}

	if (gpio_is_valid(pd->resb_gpio)) {
		r = devm_gpio_request_one(dssdev->dev, pd->resb_gpio,
				GPIOF_OUT_INIT_LOW, "lcd RESB");
		if (r)
			return r;
	}

	if (gpio_is_valid(pd->ini_gpio)) {
		r = devm_gpio_request_one(dssdev->dev, pd->ini_gpio,
				GPIOF_OUT_INIT_LOW, "lcd INI");
		if (r)
			return r;
	}

	return 0;
}

static void __exit sharp_ls_panel_remove(struct omap_dss_device *dssdev)
{
}

static int sharp_ls_power_on(struct omap_dss_device *dssdev)
{
	struct panel_sharp_ls037v7dw01_data *pd = get_panel_data(dssdev);
	int r = 0;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	omapdss_dpi_set_timings(dssdev, &dssdev->panel.timings);
	omapdss_dpi_set_data_lines(dssdev, dssdev->phy.dpi.data_lines);

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	if (gpio_is_valid(pd->resb_gpio))
		gpio_set_value_cansleep(pd->resb_gpio, 1);

	if (gpio_is_valid(pd->ini_gpio))
		gpio_set_value_cansleep(pd->ini_gpio, 1);

	return 0;
err0:
	return r;
}

static void sharp_ls_power_off(struct omap_dss_device *dssdev)
{
	struct panel_sharp_ls037v7dw01_data *pd = get_panel_data(dssdev);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	if (gpio_is_valid(pd->ini_gpio))
		gpio_set_value_cansleep(pd->ini_gpio, 0);

	if (gpio_is_valid(pd->resb_gpio))
		gpio_set_value_cansleep(pd->resb_gpio, 0);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);

	omapdss_dpi_display_disable(dssdev);
}

static int sharp_ls_panel_enable(struct omap_dss_device *dssdev)
{
	int r;
	r = sharp_ls_power_on(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
	return r;
}

static void sharp_ls_panel_disable(struct omap_dss_device *dssdev)
{
	sharp_ls_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static struct omap_dss_driver sharp_ls_driver = {
	.probe		= sharp_ls_panel_probe,
	.remove		= __exit_p(sharp_ls_panel_remove),

	.enable		= sharp_ls_panel_enable,
	.disable	= sharp_ls_panel_disable,

	.driver         = {
		.name   = "sharp_ls_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init sharp_ls_panel_drv_init(void)
{
	return omap_dss_register_driver(&sharp_ls_driver);
}

static void __exit sharp_ls_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&sharp_ls_driver);
}

module_init(sharp_ls_panel_drv_init);
module_exit(sharp_ls_panel_drv_exit);
MODULE_LICENSE("GPL");
