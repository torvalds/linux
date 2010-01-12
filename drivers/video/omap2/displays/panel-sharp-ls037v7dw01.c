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
#include <linux/regulator/consumer.h>
#include <linux/err.h>

#include <plat/display.h>

struct sharp_data {
	/* XXX This regulator should actually be in SDP board file, not here,
	 * as it doesn't actually power the LCD, but something else that
	 * affects the output to LCD (I think. Somebody clarify). It doesn't do
	 * harm here, as SDP is the only board using this currently */
	struct regulator *vdvi_reg;
};

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
};

static int sharp_ls_panel_probe(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd;

	dssdev->panel.config = OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
		OMAP_DSS_LCD_IHS;
	dssdev->panel.acb = 0x28;
	dssdev->panel.timings = sharp_ls_timings;

	sd = kzalloc(sizeof(*sd), GFP_KERNEL);
	if (!sd)
		return -ENOMEM;

	dev_set_drvdata(&dssdev->dev, sd);

	sd->vdvi_reg = regulator_get(&dssdev->dev, "vdvi");
	if (IS_ERR(sd->vdvi_reg)) {
		kfree(sd);
		pr_err("failed to get VDVI regulator\n");
		return PTR_ERR(sd->vdvi_reg);
	}

	return 0;
}

static void sharp_ls_panel_remove(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd = dev_get_drvdata(&dssdev->dev);

	regulator_put(sd->vdvi_reg);

	kfree(sd);
}

static int sharp_ls_panel_enable(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd = dev_get_drvdata(&dssdev->dev);
	int r = 0;

	/* wait couple of vsyncs until enabling the LCD */
	msleep(50);

	regulator_enable(sd->vdvi_reg);

	if (dssdev->platform_enable)
		r = dssdev->platform_enable(dssdev);

	return r;
}

static void sharp_ls_panel_disable(struct omap_dss_device *dssdev)
{
	struct sharp_data *sd = dev_get_drvdata(&dssdev->dev);

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);

	regulator_disable(sd->vdvi_reg);

	/* wait at least 5 vsyncs after disabling the LCD */

	msleep(100);
}

static int sharp_ls_panel_suspend(struct omap_dss_device *dssdev)
{
	sharp_ls_panel_disable(dssdev);
	return 0;
}

static int sharp_ls_panel_resume(struct omap_dss_device *dssdev)
{
	return sharp_ls_panel_enable(dssdev);
}

static struct omap_dss_driver sharp_ls_driver = {
	.probe		= sharp_ls_panel_probe,
	.remove		= sharp_ls_panel_remove,

	.enable		= sharp_ls_panel_enable,
	.disable	= sharp_ls_panel_disable,
	.suspend	= sharp_ls_panel_suspend,
	.resume		= sharp_ls_panel_resume,

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
