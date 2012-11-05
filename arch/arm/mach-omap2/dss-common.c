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
#include <video/omapdss.h>
#include <video/omap-panel-tfp410.h>

#include <plat/cpu.h>

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
