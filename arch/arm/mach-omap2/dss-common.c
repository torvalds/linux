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

#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "soc.h"
#include "dss-common.h"
#include "mux.h"

#define HDMI_GPIO_CT_CP_HPD 60 /* HPD mode enable/disable */
#define HDMI_GPIO_LS_OE 41 /* Level shifter for HDMI */
#define HDMI_GPIO_HPD  63 /* Hotplug detect */

/* Display DVI */
#define PANDA_DVI_TFP410_POWER_DOWN_GPIO	0

/* Using generic display panel */
static struct tfp410_platform_data omap4_dvi_panel = {
	.i2c_bus_num		= 3,
	.power_down_gpio	= PANDA_DVI_TFP410_POWER_DOWN_GPIO,
};

static struct omap_dss_device omap4_panda_dvi_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "dvi",
	.driver_name		= "tfp410",
	.data			= &omap4_dvi_panel,
	.phy.dpi.data_lines	= 24,
	.reset_gpio		= PANDA_DVI_TFP410_POWER_DOWN_GPIO,
	.channel		= OMAP_DSS_CHANNEL_LCD2,
};

static struct omap_dss_hdmi_data omap4_panda_hdmi_data = {
	.ct_cp_hpd_gpio = HDMI_GPIO_CT_CP_HPD,
	.ls_oe_gpio = HDMI_GPIO_LS_OE,
	.hpd_gpio = HDMI_GPIO_HPD,
};

static struct omap_dss_device  omap4_panda_hdmi_device = {
	.name = "hdmi",
	.driver_name = "hdmi_panel",
	.type = OMAP_DISPLAY_TYPE_HDMI,
	.channel = OMAP_DSS_CHANNEL_DIGIT,
	.data = &omap4_panda_hdmi_data,
};

static struct omap_dss_device *omap4_panda_dss_devices[] = {
	&omap4_panda_dvi_device,
	&omap4_panda_hdmi_device,
};

static struct omap_dss_board_info omap4_panda_dss_data = {
	.num_devices	= ARRAY_SIZE(omap4_panda_dss_devices),
	.devices	= omap4_panda_dss_devices,
	.default_device	= &omap4_panda_dvi_device,
};

void __init omap4_panda_display_init(void)
{
	omap_display_init(&omap4_panda_dss_data);

	/*
	 * OMAP4460SDP/Blaze and OMAP4430 ES2.3 SDP/Blaze boards and
	 * later have external pull up on the HDMI I2C lines
	 */
	if (cpu_is_omap446x() || omap_rev() > OMAP4430_REV_ES2_2)
		omap_hdmi_init(OMAP_HDMI_SDA_SCL_EXTERNAL_PULLUP);
	else
		omap_hdmi_init(0);

	omap_mux_init_gpio(HDMI_GPIO_LS_OE, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(HDMI_GPIO_CT_CP_HPD, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(HDMI_GPIO_HPD, OMAP_PIN_INPUT_PULLDOWN);
}

void __init omap4_panda_display_init_of(void)
{
	omap_display_init(&omap4_panda_dss_data);
}


/* OMAP4 Blaze display data */

#define DISPLAY_SEL_GPIO	59	/* LCD2/PicoDLP switch */
#define DLP_POWER_ON_GPIO	40

static struct nokia_dsi_panel_data dsi1_panel = {
		.name		= "taal",
		.reset_gpio	= 102,
		.use_ext_te	= false,
		.ext_te_gpio	= 101,
		.esd_interval	= 0,
		.pin_config = {
			.num_pins	= 6,
			.pins		= { 0, 1, 2, 3, 4, 5 },
		},
};

static struct omap_dss_device sdp4430_lcd_device = {
	.name			= "lcd",
	.driver_name		= "taal",
	.type			= OMAP_DISPLAY_TYPE_DSI,
	.data			= &dsi1_panel,
	.phy.dsi		= {
		.module		= 0,
	},
	.channel		= OMAP_DSS_CHANNEL_LCD,
};

static struct nokia_dsi_panel_data dsi2_panel = {
		.name		= "taal",
		.reset_gpio	= 104,
		.use_ext_te	= false,
		.ext_te_gpio	= 103,
		.esd_interval	= 0,
		.pin_config = {
			.num_pins	= 6,
			.pins		= { 0, 1, 2, 3, 4, 5 },
		},
};

static struct omap_dss_device sdp4430_lcd2_device = {
	.name			= "lcd2",
	.driver_name		= "taal",
	.type			= OMAP_DISPLAY_TYPE_DSI,
	.data			= &dsi2_panel,
	.phy.dsi		= {

		.module		= 1,
	},
	.channel		= OMAP_DSS_CHANNEL_LCD2,
};

static struct omap_dss_hdmi_data sdp4430_hdmi_data = {
	.ct_cp_hpd_gpio = HDMI_GPIO_CT_CP_HPD,
	.ls_oe_gpio = HDMI_GPIO_LS_OE,
	.hpd_gpio = HDMI_GPIO_HPD,
};

static struct omap_dss_device sdp4430_hdmi_device = {
	.name = "hdmi",
	.driver_name = "hdmi_panel",
	.type = OMAP_DISPLAY_TYPE_HDMI,
	.channel = OMAP_DSS_CHANNEL_DIGIT,
	.data = &sdp4430_hdmi_data,
};

static struct picodlp_panel_data sdp4430_picodlp_pdata = {
	.picodlp_adapter_id	= 2,
	.emu_done_gpio		= 44,
	.pwrgood_gpio		= 45,
};

static void sdp4430_picodlp_init(void)
{
	int r;
	const struct gpio picodlp_gpios[] = {
		{DLP_POWER_ON_GPIO, GPIOF_OUT_INIT_LOW,
			"DLP POWER ON"},
		{sdp4430_picodlp_pdata.emu_done_gpio, GPIOF_IN,
			"DLP EMU DONE"},
		{sdp4430_picodlp_pdata.pwrgood_gpio, GPIOF_OUT_INIT_LOW,
			"DLP PWRGOOD"},
	};

	r = gpio_request_array(picodlp_gpios, ARRAY_SIZE(picodlp_gpios));
	if (r)
		pr_err("Cannot request PicoDLP GPIOs, error %d\n", r);
}

static int sdp4430_panel_enable_picodlp(struct omap_dss_device *dssdev)
{
	gpio_set_value(DISPLAY_SEL_GPIO, 0);
	gpio_set_value(DLP_POWER_ON_GPIO, 1);

	return 0;
}

static void sdp4430_panel_disable_picodlp(struct omap_dss_device *dssdev)
{
	gpio_set_value(DLP_POWER_ON_GPIO, 0);
	gpio_set_value(DISPLAY_SEL_GPIO, 1);
}

static struct omap_dss_device sdp4430_picodlp_device = {
	.name			= "picodlp",
	.driver_name		= "picodlp_panel",
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines	= 24,
	.channel		= OMAP_DSS_CHANNEL_LCD2,
	.platform_enable	= sdp4430_panel_enable_picodlp,
	.platform_disable	= sdp4430_panel_disable_picodlp,
	.data			= &sdp4430_picodlp_pdata,
};

static struct omap_dss_device *sdp4430_dss_devices[] = {
	&sdp4430_lcd_device,
	&sdp4430_lcd2_device,
	&sdp4430_hdmi_device,
	&sdp4430_picodlp_device,
};

static struct omap_dss_board_info sdp4430_dss_data = {
	.num_devices	= ARRAY_SIZE(sdp4430_dss_devices),
	.devices	= sdp4430_dss_devices,
	.default_device	= &sdp4430_lcd_device,
};

void __init omap_4430sdp_display_init(void)
{
	int r;

	/* Enable LCD2 by default (instead of Pico DLP) */
	r = gpio_request_one(DISPLAY_SEL_GPIO, GPIOF_OUT_INIT_HIGH,
			"display_sel");
	if (r)
		pr_err("%s: Could not get display_sel GPIO\n", __func__);

	sdp4430_picodlp_init();
	omap_display_init(&sdp4430_dss_data);
	/*
	 * OMAP4460SDP/Blaze and OMAP4430 ES2.3 SDP/Blaze boards and
	 * later have external pull up on the HDMI I2C lines
	 */
	if (cpu_is_omap446x() || omap_rev() > OMAP4430_REV_ES2_2)
		omap_hdmi_init(OMAP_HDMI_SDA_SCL_EXTERNAL_PULLUP);
	else
		omap_hdmi_init(0);

	omap_mux_init_gpio(HDMI_GPIO_LS_OE, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(HDMI_GPIO_CT_CP_HPD, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(HDMI_GPIO_HPD, OMAP_PIN_INPUT_PULLDOWN);
}

void __init omap_4430sdp_display_init_of(void)
{
	int r;

	/* Enable LCD2 by default (instead of Pico DLP) */
	r = gpio_request_one(DISPLAY_SEL_GPIO, GPIOF_OUT_INIT_HIGH,
			"display_sel");
	if (r)
		pr_err("%s: Could not get display_sel GPIO\n", __func__);

	sdp4430_picodlp_init();
	omap_display_init(&sdp4430_dss_data);
}
