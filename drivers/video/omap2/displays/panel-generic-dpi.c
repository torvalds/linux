/*
 * Generic DPI Panels support
 *
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Bryan Wu <bryan.wu@canonical.com>
 *
 * LCD panel driver for Sharp LQ043T1DG01
 *
 * Copyright (C) 2009 Texas Instruments Inc
 * Author: Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * LCD panel driver for Toppoly TDO35S
 *
 * Copyright (C) 2009 CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
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
#include <linux/slab.h>
#include <video/omapdss.h>

#include <video/omap-panel-generic-dpi.h>

struct panel_config {
	struct omap_video_timings timings;

	int acbi;	/* ac-bias pin transitions per interrupt */
	/* Unit: line clocks */
	int acb;	/* ac-bias pin frequency */

	enum omap_panel_config config;

	int power_on_delay;
	int power_off_delay;

	/*
	 * Used to match device to panel configuration
	 * when use generic panel driver
	 */
	const char *name;
};

/* Panel configurations */
static struct panel_config generic_dpi_panels[] = {
	/* Sharp LQ043T1DG01 */
	{
		{
			.x_res		= 480,
			.y_res		= 272,

			.pixel_clock	= 9000,

			.hsw		= 42,
			.hfp		= 3,
			.hbp		= 2,

			.vsw		= 11,
			.vfp		= 3,
			.vbp		= 2,
		},
		.acbi			= 0x0,
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IEO,
		.power_on_delay		= 50,
		.power_off_delay	= 100,
		.name			= "sharp_lq",
	},

	/* Sharp LS037V7DW01 */
	{
		{
			.x_res		= 480,
			.y_res		= 640,

			.pixel_clock	= 19200,

			.hsw		= 2,
			.hfp		= 1,
			.hbp		= 28,

			.vsw		= 1,
			.vfp		= 1,
			.vbp		= 1,
		},
		.acbi			= 0x0,
		.acb			= 0x28,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
						OMAP_DSS_LCD_IHS,
		.power_on_delay		= 50,
		.power_off_delay	= 100,
		.name			= "sharp_ls",
	},

	/* Toppoly TDO35S */
	{
		{
			.x_res		= 480,
			.y_res		= 640,

			.pixel_clock	= 26000,

			.hfp		= 104,
			.hsw		= 8,
			.hbp		= 8,

			.vfp		= 4,
			.vsw		= 2,
			.vbp		= 2,
		},
		.acbi			= 0x0,
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IPC |
					OMAP_DSS_LCD_ONOFF,
		.power_on_delay		= 0,
		.power_off_delay	= 0,
		.name			= "toppoly_tdo35s",
	},

	/* Samsung LTE430WQ-F0C */
	{
		{
			.x_res		= 480,
			.y_res		= 272,

			.pixel_clock	= 9200,

			.hfp		= 8,
			.hsw		= 41,
			.hbp		= 45 - 41,

			.vfp		= 4,
			.vsw		= 10,
			.vbp		= 12 - 10,
		},
		.acbi			= 0x0,
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
						OMAP_DSS_LCD_IHS,
		.power_on_delay		= 0,
		.power_off_delay	= 0,
		.name			= "samsung_lte430wq_f0c",
	},

	/* Seiko 70WVW1TZ3Z3 */
	{
		{
			.x_res		= 800,
			.y_res		= 480,

			.pixel_clock	= 33000,

			.hsw		= 128,
			.hfp		= 10,
			.hbp		= 10,

			.vsw		= 2,
			.vfp		= 4,
			.vbp		= 11,
		},
		.acbi			= 0x0,
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
						OMAP_DSS_LCD_IHS,
		.power_on_delay		= 0,
		.power_off_delay	= 0,
		.name			= "seiko_70wvw1tz3",
	},

	/* Powertip PH480272T */
	{
		{
			.x_res		= 480,
			.y_res		= 272,

			.pixel_clock	= 9000,

			.hsw		= 40,
			.hfp		= 2,
			.hbp		= 2,

			.vsw		= 10,
			.vfp		= 2,
			.vbp		= 2,
		},
		.acbi			= 0x0,
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					  OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IEO,
		.power_on_delay		= 0,
		.power_off_delay	= 0,
		.name			= "powertip_ph480272t",
	},

	/* Innolux AT070TN83 */
	{
		{
			.x_res		= 800,
			.y_res		= 480,

			.pixel_clock	= 40000,

			.hsw		= 48,
			.hfp		= 1,
			.hbp		= 1,

			.vsw		= 3,
			.vfp		= 12,
			.vbp		= 25,
		},
		.acbi			= 0x0,
		.acb			= 0x28,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					  OMAP_DSS_LCD_IHS,
		.power_on_delay		= 0,
		.power_off_delay	= 0,
		.name			= "innolux_at070tn83",
	},

	/* NEC NL2432DR22-11B */
	{
		{
			.x_res		= 240,
			.y_res		= 320,

			.pixel_clock	= 5400,

			.hsw		= 3,
			.hfp		= 3,
			.hbp		= 39,

			.vsw		= 1,
			.vfp		= 2,
			.vbp		= 7,
		},
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
						OMAP_DSS_LCD_IHS,
		.name			= "nec_nl2432dr22-11b",
	},

	/* Unknown panel used in OMAP H4 */
	{
		{
			.x_res		= 240,
			.y_res		= 320,

			.pixel_clock	= 6250,

			.hsw		= 15,
			.hfp		= 15,
			.hbp		= 60,

			.vsw		= 1,
			.vfp		= 1,
			.vbp		= 1,
		},
		.config			= OMAP_DSS_LCD_TFT,

		.name			= "h4",
	},

	/* Unknown panel used in Samsung OMAP2 Apollon */
	{
		{
			.x_res		= 480,
			.y_res		= 272,

			.pixel_clock	= 6250,

			.hsw		= 41,
			.hfp		= 2,
			.hbp		= 2,

			.vsw		= 10,
			.vfp		= 2,
			.vbp		= 2,
		},
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
						OMAP_DSS_LCD_IHS,

		.name			= "apollon",
	},
	/* FocalTech ETM070003DH6 */
	{
		{
			.x_res		= 800,
			.y_res		= 480,

			.pixel_clock	= 28000,

			.hsw		= 48,
			.hfp		= 40,
			.hbp		= 40,

			.vsw		= 3,
			.vfp		= 13,
			.vbp		= 29,
		},
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					  OMAP_DSS_LCD_IHS,
		.name			= "focaltech_etm070003dh6",
	},

	/* Microtips Technologies - UMSH-8173MD */
	{
		{
			.x_res		= 800,
			.y_res		= 480,

			.pixel_clock	= 34560,

			.hsw		= 13,
			.hfp		= 101,
			.hbp		= 101,

			.vsw		= 23,
			.vfp		= 1,
			.vbp		= 1,
		},
		.acbi			= 0x0,
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					  OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IPC,
		.power_on_delay		= 0,
		.power_off_delay	= 0,
		.name			= "microtips_umsh_8173md",
	},

	/* OrtusTech COM43H4M10XTC */
	{
		{
			.x_res		= 480,
			.y_res		= 272,

			.pixel_clock	= 8000,

			.hsw		= 41,
			.hfp		= 8,
			.hbp		= 4,

			.vsw		= 10,
			.vfp		= 4,
			.vbp		= 2,
		},
		.config			= OMAP_DSS_LCD_TFT,

		.name			= "ortustech_com43h4m10xtc",
	},

	/* Innolux AT080TN52 */
	{
		{
			.x_res = 800,
			.y_res = 600,

			.pixel_clock	= 41142,

			.hsw		= 20,
			.hfp		= 210,
			.hbp		= 46,

			.vsw		= 10,
			.vfp		= 12,
			.vbp		= 23,
		},
		.acb			= 0x0,
		.config			= OMAP_DSS_LCD_TFT | OMAP_DSS_LCD_IVS |
					  OMAP_DSS_LCD_IHS | OMAP_DSS_LCD_IEO,

		.name			= "innolux_at080tn52",
	},
};

struct panel_drv_data {

	struct omap_dss_device *dssdev;

	struct panel_config *panel_config;
};

static inline struct panel_generic_dpi_data
*get_panel_data(const struct omap_dss_device *dssdev)
{
	return (struct panel_generic_dpi_data *) dssdev->data;
}

static int generic_dpi_panel_power_on(struct omap_dss_device *dssdev)
{
	int r;
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	struct panel_drv_data *drv_data = dev_get_drvdata(&dssdev->dev);
	struct panel_config *panel_config = drv_data->panel_config;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	/* wait couple of vsyncs until enabling the LCD */
	if (panel_config->power_on_delay)
		msleep(panel_config->power_on_delay);

	if (panel_data->platform_enable) {
		r = panel_data->platform_enable(dssdev);
		if (r)
			goto err1;
	}

	return 0;
err1:
	omapdss_dpi_display_disable(dssdev);
err0:
	return r;
}

static void generic_dpi_panel_power_off(struct omap_dss_device *dssdev)
{
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	struct panel_drv_data *drv_data = dev_get_drvdata(&dssdev->dev);
	struct panel_config *panel_config = drv_data->panel_config;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	if (panel_data->platform_disable)
		panel_data->platform_disable(dssdev);

	/* wait couple of vsyncs after disabling the LCD */
	if (panel_config->power_off_delay)
		msleep(panel_config->power_off_delay);

	omapdss_dpi_display_disable(dssdev);
}

static int generic_dpi_panel_probe(struct omap_dss_device *dssdev)
{
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	struct panel_config *panel_config = NULL;
	struct panel_drv_data *drv_data = NULL;
	int i;

	dev_dbg(&dssdev->dev, "probe\n");

	if (!panel_data || !panel_data->name)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(generic_dpi_panels); i++) {
		if (strcmp(panel_data->name, generic_dpi_panels[i].name) == 0) {
			panel_config = &generic_dpi_panels[i];
			break;
		}
	}

	if (!panel_config)
		return -EINVAL;

	dssdev->panel.config = panel_config->config;
	dssdev->panel.timings = panel_config->timings;
	dssdev->panel.acb = panel_config->acb;
	dssdev->panel.acbi = panel_config->acbi;

	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->dssdev = dssdev;
	drv_data->panel_config = panel_config;

	dev_set_drvdata(&dssdev->dev, drv_data);

	return 0;
}

static void __exit generic_dpi_panel_remove(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *drv_data = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "remove\n");

	kfree(drv_data);

	dev_set_drvdata(&dssdev->dev, NULL);
}

static int generic_dpi_panel_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = generic_dpi_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void generic_dpi_panel_disable(struct omap_dss_device *dssdev)
{
	generic_dpi_panel_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}

static int generic_dpi_panel_suspend(struct omap_dss_device *dssdev)
{
	generic_dpi_panel_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;

	return 0;
}

static int generic_dpi_panel_resume(struct omap_dss_device *dssdev)
{
	int r = 0;

	r = generic_dpi_panel_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void generic_dpi_panel_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	dpi_set_timings(dssdev, timings);
}

static void generic_dpi_panel_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	*timings = dssdev->panel.timings;
}

static int generic_dpi_panel_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	return dpi_check_timings(dssdev, timings);
}

static struct omap_dss_driver dpi_driver = {
	.probe		= generic_dpi_panel_probe,
	.remove		= __exit_p(generic_dpi_panel_remove),

	.enable		= generic_dpi_panel_enable,
	.disable	= generic_dpi_panel_disable,
	.suspend	= generic_dpi_panel_suspend,
	.resume		= generic_dpi_panel_resume,

	.set_timings	= generic_dpi_panel_set_timings,
	.get_timings	= generic_dpi_panel_get_timings,
	.check_timings	= generic_dpi_panel_check_timings,

	.driver         = {
		.name   = "generic_dpi_panel",
		.owner  = THIS_MODULE,
	},
};

static int __init generic_dpi_panel_drv_init(void)
{
	return omap_dss_register_driver(&dpi_driver);
}

static void __exit generic_dpi_panel_drv_exit(void)
{
	omap_dss_unregister_driver(&dpi_driver);
}

module_init(generic_dpi_panel_drv_init);
module_exit(generic_dpi_panel_drv_exit);
MODULE_LICENSE("GPL");
