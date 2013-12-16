/*
 * Copyright (C) 2012 Texas Instruments, Inc..
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/*
 * NOTE: this is a transitional file to help with DT adaptation.
 * This file will be removed when DSS supports DT.
 */

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "soc.h"
#include "dss-common.h"
#include "mux.h"

#define HDMI_GPIO_CT_CP_HPD 60 /* HPD mode enable/disable */
#define HDMI_GPIO_LS_OE 41 /* Level shifter for HDMI */
#define HDMI_GPIO_HPD  63 /* Hotplug detect */

#define PANDA_DVI_TFP410_POWER_DOWN_GPIO	0

/* DVI Connector */
static struct connector_dvi_platform_data omap4_panda_dvi_connector_pdata = {
	.name                   = "dvi",
	.source                 = "tfp410.0",
	.i2c_bus_num            = 2,
};

static struct platform_device omap4_panda_dvi_connector_device = {
	.name                   = "connector-dvi",
	.id                     = 0,
	.dev.platform_data      = &omap4_panda_dvi_connector_pdata,
};

/* TFP410 DPI-to-DVI chip */
static struct encoder_tfp410_platform_data omap4_panda_tfp410_pdata = {
	.name                   = "tfp410.0",
	.source                 = "dpi.0",
	.data_lines             = 24,
	.power_down_gpio        = PANDA_DVI_TFP410_POWER_DOWN_GPIO,
};

static struct platform_device omap4_panda_tfp410_device = {
	.name                   = "tfp410",
	.id                     = 0,
	.dev.platform_data      = &omap4_panda_tfp410_pdata,
};

/* HDMI Connector */
static struct connector_hdmi_platform_data omap4_panda_hdmi_connector_pdata = {
	.name                   = "hdmi",
	.source                 = "tpd12s015.0",
};

static struct platform_device omap4_panda_hdmi_connector_device = {
	.name                   = "connector-hdmi",
	.id                     = 0,
	.dev.platform_data      = &omap4_panda_hdmi_connector_pdata,
};

/* TPD12S015 HDMI ESD protection & level shifter chip */
static struct encoder_tpd12s015_platform_data omap4_panda_tpd_pdata = {
	.name                   = "tpd12s015.0",
	.source                 = "hdmi.0",

	.ct_cp_hpd_gpio = HDMI_GPIO_CT_CP_HPD,
	.ls_oe_gpio = HDMI_GPIO_LS_OE,
	.hpd_gpio = HDMI_GPIO_HPD,
};

static struct platform_device omap4_panda_tpd_device = {
	.name                   = "tpd12s015",
	.id                     = 0,
	.dev.platform_data      = &omap4_panda_tpd_pdata,
};

static struct omap_dss_board_info omap4_panda_dss_data = {
	.default_display_name = "dvi",
};

void __init omap4_panda_display_init_of(void)
{
	omap_display_init(&omap4_panda_dss_data);

	platform_device_register(&omap4_panda_tfp410_device);
	platform_device_register(&omap4_panda_dvi_connector_device);

	platform_device_register(&omap4_panda_tpd_device);
	platform_device_register(&omap4_panda_hdmi_connector_device);
}


/* OMAP4 Blaze display data */

#define DISPLAY_SEL_GPIO	59	/* LCD2/PicoDLP switch */
#define DLP_POWER_ON_GPIO	40

static struct panel_dsicm_platform_data dsi1_panel = {
	.name		= "lcd",
	.source		= "dsi.0",
	.reset_gpio	= 102,
	.use_ext_te	= false,
	.ext_te_gpio	= 101,
	.pin_config = {
		.num_pins	= 6,
		.pins		= { 0, 1, 2, 3, 4, 5 },
	},
};

static struct platform_device sdp4430_lcd_device = {
	.name                   = "panel-dsi-cm",
	.id                     = 0,
	.dev.platform_data	= &dsi1_panel,
};

static struct panel_dsicm_platform_data dsi2_panel = {
	.name		= "lcd2",
	.source		= "dsi.1",
	.reset_gpio	= 104,
	.use_ext_te	= false,
	.ext_te_gpio	= 103,
	.pin_config = {
		.num_pins	= 6,
		.pins		= { 0, 1, 2, 3, 4, 5 },
	},
};

static struct platform_device sdp4430_lcd2_device = {
	.name                   = "panel-dsi-cm",
	.id                     = 1,
	.dev.platform_data	= &dsi2_panel,
};

/* HDMI Connector */
static struct connector_hdmi_platform_data sdp4430_hdmi_connector_pdata = {
	.name                   = "hdmi",
	.source                 = "tpd12s015.0",
};

static struct platform_device sdp4430_hdmi_connector_device = {
	.name                   = "connector-hdmi",
	.id                     = 0,
	.dev.platform_data      = &sdp4430_hdmi_connector_pdata,
};

/* TPD12S015 HDMI ESD protection & level shifter chip */
static struct encoder_tpd12s015_platform_data sdp4430_tpd_pdata = {
	.name                   = "tpd12s015.0",
	.source                 = "hdmi.0",

	.ct_cp_hpd_gpio = HDMI_GPIO_CT_CP_HPD,
	.ls_oe_gpio = HDMI_GPIO_LS_OE,
	.hpd_gpio = HDMI_GPIO_HPD,
};

static struct platform_device sdp4430_tpd_device = {
	.name                   = "tpd12s015",
	.id                     = 0,
	.dev.platform_data      = &sdp4430_tpd_pdata,
};


static struct omap_dss_board_info sdp4430_dss_data = {
	.default_display_name = "lcd",
};

/*
 * we select LCD2 by default (instead of Pico DLP) by setting DISPLAY_SEL_GPIO.
 * Setting DLP_POWER_ON gpio enables the VDLP_2V5 VDLP_1V8 and VDLP_1V0 rails
 * used by picodlp on the 4430sdp platform. Keep this gpio disabled as LCD2 is
 * selected by default
 */
void __init omap_4430sdp_display_init_of(void)
{
	int r;

	r = gpio_request_one(DISPLAY_SEL_GPIO, GPIOF_OUT_INIT_HIGH,
			"display_sel");
	if (r)
		pr_err("%s: Could not get display_sel GPIO\n", __func__);

	r = gpio_request_one(DLP_POWER_ON_GPIO, GPIOF_OUT_INIT_LOW,
		"DLP POWER ON");
	if (r)
		pr_err("%s: Could not get DLP POWER ON GPIO\n", __func__);

	omap_display_init(&sdp4430_dss_data);

	platform_device_register(&sdp4430_lcd_device);
	platform_device_register(&sdp4430_lcd2_device);

	platform_device_register(&sdp4430_tpd_device);
	platform_device_register(&sdp4430_hdmi_connector_device);
}


/* OMAP3 IGEPv2 data */

#define IGEP2_DVI_TFP410_POWER_DOWN_GPIO	170

/* DVI Connector */
static struct connector_dvi_platform_data omap3_igep2_dvi_connector_pdata = {
	.name                   = "dvi",
	.source                 = "tfp410.0",
	.i2c_bus_num            = 2,
};

static struct platform_device omap3_igep2_dvi_connector_device = {
	.name                   = "connector-dvi",
	.id                     = 0,
	.dev.platform_data      = &omap3_igep2_dvi_connector_pdata,
};

/* TFP410 DPI-to-DVI chip */
static struct encoder_tfp410_platform_data omap3_igep2_tfp410_pdata = {
	.name                   = "tfp410.0",
	.source                 = "dpi.0",
	.data_lines             = 24,
	.power_down_gpio        = IGEP2_DVI_TFP410_POWER_DOWN_GPIO,
};

static struct platform_device omap3_igep2_tfp410_device = {
	.name                   = "tfp410",
	.id                     = 0,
	.dev.platform_data      = &omap3_igep2_tfp410_pdata,
};

static struct omap_dss_board_info igep2_dss_data = {
	.default_display_name = "dvi",
};

void __init omap3_igep2_display_init_of(void)
{
	omap_display_init(&igep2_dss_data);

	platform_device_register(&omap3_igep2_tfp410_device);
	platform_device_register(&omap3_igep2_dvi_connector_device);
}
