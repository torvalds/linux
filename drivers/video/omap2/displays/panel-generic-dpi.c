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
#include <linux/gpio.h>
#include <video/omapdss.h>

#include <video/omap-panel-data.h>

struct panel_config {
	struct omap_video_timings timings;

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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "h4",
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
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

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "innolux_at080tn52",
	},

	/* Mitsubishi AA084SB01 */
	{
		{
			.x_res		= 800,
			.y_res		= 600,
			.pixel_clock	= 40000,

			.hsw		= 1,
			.hfp		= 254,
			.hbp		= 1,

			.vsw		= 1,
			.vfp		= 26,
			.vbp		= 1,

			.vsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "mitsubishi_aa084sb01",
	},
	/* EDT ET0500G0DH6 */
	{
		{
			.x_res		= 800,
			.y_res		= 480,
			.pixel_clock	= 33260,

			.hsw		= 128,
			.hfp		= 216,
			.hbp		= 40,

			.vsw		= 2,
			.vfp		= 35,
			.vbp		= 10,

			.vsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_RISING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "edt_et0500g0dh6",
	},

	/* Prime-View PD050VL1 */
	{
		{
			.x_res		= 640,
			.y_res		= 480,

			.pixel_clock	= 25000,

			.hsw		= 96,
			.hfp		= 18,
			.hbp		= 46,

			.vsw		= 2,
			.vfp		= 10,
			.vbp		= 33,

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "primeview_pd050vl1",
	},

	/* Prime-View PM070WL4 */
	{
		{
			.x_res		= 800,
			.y_res		= 480,

			.pixel_clock	= 32000,

			.hsw		= 128,
			.hfp		= 42,
			.hbp		= 86,

			.vsw		= 2,
			.vfp		= 10,
			.vbp		= 33,

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "primeview_pm070wl4",
	},

	/* Prime-View PD104SLF */
	{
		{
			.x_res		= 800,
			.y_res		= 600,

			.pixel_clock	= 40000,

			.hsw		= 128,
			.hfp		= 42,
			.hbp		= 86,

			.vsw		= 4,
			.vfp		= 1,
			.vbp		= 23,

			.vsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.hsync_level	= OMAPDSS_SIG_ACTIVE_LOW,
			.data_pclk_edge	= OMAPDSS_DRIVE_SIG_FALLING_EDGE,
			.de_level	= OMAPDSS_SIG_ACTIVE_HIGH,
			.sync_pclk_edge	= OMAPDSS_DRIVE_SIG_OPPOSITE_EDGES,
		},
		.name			= "primeview_pd104slf",
	},
};

struct panel_drv_data {

	struct omap_dss_device *dssdev;

	struct panel_config *panel_config;

	struct mutex lock;
};

static inline struct panel_generic_dpi_data
*get_panel_data(const struct omap_dss_device *dssdev)
{
	return (struct panel_generic_dpi_data *) dssdev->data;
}

static int generic_dpi_panel_power_on(struct omap_dss_device *dssdev)
{
	int r, i;
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);
	struct panel_config *panel_config = drv_data->panel_config;

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	omapdss_dpi_set_timings(dssdev, &dssdev->panel.timings);
	omapdss_dpi_set_data_lines(dssdev, dssdev->phy.dpi.data_lines);

	r = omapdss_dpi_display_enable(dssdev);
	if (r)
		goto err0;

	/* wait couple of vsyncs until enabling the LCD */
	if (panel_config->power_on_delay)
		msleep(panel_config->power_on_delay);

	for (i = 0; i < panel_data->num_gpios; ++i) {
		gpio_set_value_cansleep(panel_data->gpios[i],
				panel_data->gpio_invert[i] ? 0 : 1);
	}

	return 0;

err0:
	return r;
}

static void generic_dpi_panel_power_off(struct omap_dss_device *dssdev)
{
	struct panel_generic_dpi_data *panel_data = get_panel_data(dssdev);
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);
	struct panel_config *panel_config = drv_data->panel_config;
	int i;

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	for (i = panel_data->num_gpios - 1; i >= 0; --i) {
		gpio_set_value_cansleep(panel_data->gpios[i],
				panel_data->gpio_invert[i] ? 1 : 0);
	}

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
	int i, r;

	dev_dbg(dssdev->dev, "probe\n");

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

	for (i = 0; i < panel_data->num_gpios; ++i) {
		r = devm_gpio_request_one(dssdev->dev, panel_data->gpios[i],
				panel_data->gpio_invert[i] ?
				GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
				"panel gpio");
		if (r)
			return r;
	}

	dssdev->panel.timings = panel_config->timings;

	drv_data = devm_kzalloc(dssdev->dev, sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->dssdev = dssdev;
	drv_data->panel_config = panel_config;

	mutex_init(&drv_data->lock);

	dev_set_drvdata(dssdev->dev, drv_data);

	return 0;
}

static void __exit generic_dpi_panel_remove(struct omap_dss_device *dssdev)
{
	dev_dbg(dssdev->dev, "remove\n");

	dev_set_drvdata(dssdev->dev, NULL);
}

static int generic_dpi_panel_enable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);
	int r;

	mutex_lock(&drv_data->lock);

	r = generic_dpi_panel_power_on(dssdev);
	if (r)
		goto err;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
err:
	mutex_unlock(&drv_data->lock);

	return r;
}

static void generic_dpi_panel_disable(struct omap_dss_device *dssdev)
{
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);

	mutex_lock(&drv_data->lock);

	generic_dpi_panel_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	mutex_unlock(&drv_data->lock);
}

static void generic_dpi_panel_set_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);

	mutex_lock(&drv_data->lock);

	omapdss_dpi_set_timings(dssdev, timings);

	dssdev->panel.timings = *timings;

	mutex_unlock(&drv_data->lock);
}

static void generic_dpi_panel_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);

	mutex_lock(&drv_data->lock);

	*timings = dssdev->panel.timings;

	mutex_unlock(&drv_data->lock);
}

static int generic_dpi_panel_check_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	struct panel_drv_data *drv_data = dev_get_drvdata(dssdev->dev);
	int r;

	mutex_lock(&drv_data->lock);

	r = dpi_check_timings(dssdev, timings);

	mutex_unlock(&drv_data->lock);

	return r;
}

static struct omap_dss_driver dpi_driver = {
	.probe		= generic_dpi_panel_probe,
	.remove		= __exit_p(generic_dpi_panel_remove),

	.enable		= generic_dpi_panel_enable,
	.disable	= generic_dpi_panel_disable,

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
