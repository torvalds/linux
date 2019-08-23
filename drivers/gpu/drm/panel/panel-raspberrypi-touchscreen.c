/*
 * Copyright © 2016-2017 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Portions of this file (derived from panel-simple.c) are:
 *
 * Copyright (C) 2013, NVIDIA Corporation.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * Raspberry Pi 7" touchscreen panel driver.
 *
 * The 7" touchscreen consists of a DPI LCD panel, a Toshiba
 * TC358762XBG DSI-DPI bridge, and an I2C-connected Atmel ATTINY88-MUR
 * controlling power management, the LCD PWM, and initial register
 * setup of the Tohsiba.
 *
 * This driver controls the TC358762 and ATTINY88, presenting a DSI
 * device with a drm_panel.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fb.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/pm.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#define RPI_DSI_DRIVER_NAME "rpi-ts-dsi"

/* I2C registers of the Atmel microcontroller. */
enum REG_ADDR {
	REG_ID = 0x80,
	REG_PORTA, /* BIT(2) for horizontal flip, BIT(3) for vertical flip */
	REG_PORTB,
	REG_PORTC,
	REG_PORTD,
	REG_POWERON,
	REG_PWM,
	REG_DDRA,
	REG_DDRB,
	REG_DDRC,
	REG_DDRD,
	REG_TEST,
	REG_WR_ADDRL,
	REG_WR_ADDRH,
	REG_READH,
	REG_READL,
	REG_WRITEH,
	REG_WRITEL,
	REG_ID2,
};

/* DSI D-PHY Layer Registers */
#define D0W_DPHYCONTTX		0x0004
#define CLW_DPHYCONTRX		0x0020
#define D0W_DPHYCONTRX		0x0024
#define D1W_DPHYCONTRX		0x0028
#define COM_DPHYCONTRX		0x0038
#define CLW_CNTRL		0x0040
#define D0W_CNTRL		0x0044
#define D1W_CNTRL		0x0048
#define DFTMODE_CNTRL		0x0054

/* DSI PPI Layer Registers */
#define PPI_STARTPPI		0x0104
#define PPI_BUSYPPI		0x0108
#define PPI_LINEINITCNT		0x0110
#define PPI_LPTXTIMECNT		0x0114
#define PPI_CLS_ATMR		0x0140
#define PPI_D0S_ATMR		0x0144
#define PPI_D1S_ATMR		0x0148
#define PPI_D0S_CLRSIPOCOUNT	0x0164
#define PPI_D1S_CLRSIPOCOUNT	0x0168
#define CLS_PRE			0x0180
#define D0S_PRE			0x0184
#define D1S_PRE			0x0188
#define CLS_PREP		0x01A0
#define D0S_PREP		0x01A4
#define D1S_PREP		0x01A8
#define CLS_ZERO		0x01C0
#define D0S_ZERO		0x01C4
#define D1S_ZERO		0x01C8
#define PPI_CLRFLG		0x01E0
#define PPI_CLRSIPO		0x01E4
#define HSTIMEOUT		0x01F0
#define HSTIMEOUTENABLE		0x01F4

/* DSI Protocol Layer Registers */
#define DSI_STARTDSI		0x0204
#define DSI_BUSYDSI		0x0208
#define DSI_LANEENABLE		0x0210
# define DSI_LANEENABLE_CLOCK		BIT(0)
# define DSI_LANEENABLE_D0		BIT(1)
# define DSI_LANEENABLE_D1		BIT(2)

#define DSI_LANESTATUS0		0x0214
#define DSI_LANESTATUS1		0x0218
#define DSI_INTSTATUS		0x0220
#define DSI_INTMASK		0x0224
#define DSI_INTCLR		0x0228
#define DSI_LPTXTO		0x0230
#define DSI_MODE		0x0260
#define DSI_PAYLOAD0		0x0268
#define DSI_PAYLOAD1		0x026C
#define DSI_SHORTPKTDAT		0x0270
#define DSI_SHORTPKTREQ		0x0274
#define DSI_BTASTA		0x0278
#define DSI_BTACLR		0x027C

/* DSI General Registers */
#define DSIERRCNT		0x0300
#define DSISIGMOD		0x0304

/* DSI Application Layer Registers */
#define APLCTRL			0x0400
#define APLSTAT			0x0404
#define APLERR			0x0408
#define PWRMOD			0x040C
#define RDPKTLN			0x0410
#define PXLFMT			0x0414
#define MEMWRCMD		0x0418

/* LCDC/DPI Host Registers */
#define LCDCTRL			0x0420
#define HSR			0x0424
#define HDISPR			0x0428
#define VSR			0x042C
#define VDISPR			0x0430
#define VFUEN			0x0434

/* DBI-B Host Registers */
#define DBIBCTRL		0x0440

/* SPI Master Registers */
#define SPICMR			0x0450
#define SPITCR			0x0454

/* System Controller Registers */
#define SYSSTAT			0x0460
#define SYSCTRL			0x0464
#define SYSPLL1			0x0468
#define SYSPLL2			0x046C
#define SYSPLL3			0x0470
#define SYSPMCTRL		0x047C

/* GPIO Registers */
#define GPIOC			0x0480
#define GPIOO			0x0484
#define GPIOI			0x0488

/* I2C Registers */
#define I2CCLKCTRL		0x0490

/* Chip/Rev Registers */
#define IDREG			0x04A0

/* Debug Registers */
#define WCMDQUEUE		0x0500
#define RCMDQUEUE		0x0504

struct rpi_touchscreen {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;
	struct i2c_client *i2c;
};

static const struct drm_display_mode rpi_touchscreen_modes[] = {
	{
		/* Modeline comes from the Raspberry Pi firmware, with HFP=1
		 * plugged in and clock re-computed from that.
		 */
		.clock = 25979400 / 1000,
		.hdisplay = 800,
		.hsync_start = 800 + 1,
		.hsync_end = 800 + 1 + 2,
		.htotal = 800 + 1 + 2 + 46,
		.vdisplay = 480,
		.vsync_start = 480 + 7,
		.vsync_end = 480 + 7 + 2,
		.vtotal = 480 + 7 + 2 + 21,
		.vrefresh = 60,
	},
};

static struct rpi_touchscreen *panel_to_ts(struct drm_panel *panel)
{
	return container_of(panel, struct rpi_touchscreen, base);
}

static int rpi_touchscreen_i2c_read(struct rpi_touchscreen *ts, u8 reg)
{
	return i2c_smbus_read_byte_data(ts->i2c, reg);
}

static void rpi_touchscreen_i2c_write(struct rpi_touchscreen *ts,
				      u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(ts->i2c, reg, val);
	if (ret)
		dev_err(&ts->dsi->dev, "I2C write failed: %d\n", ret);
}

static int rpi_touchscreen_write(struct rpi_touchscreen *ts, u16 reg, u32 val)
{
	u8 msg[] = {
		reg,
		reg >> 8,
		val,
		val >> 8,
		val >> 16,
		val >> 24,
	};

	mipi_dsi_generic_write(ts->dsi, msg, sizeof(msg));

	return 0;
}

static int rpi_touchscreen_disable(struct drm_panel *panel)
{
	struct rpi_touchscreen *ts = panel_to_ts(panel);

	rpi_touchscreen_i2c_write(ts, REG_PWM, 0);

	rpi_touchscreen_i2c_write(ts, REG_POWERON, 0);
	udelay(1);

	return 0;
}

static int rpi_touchscreen_noop(struct drm_panel *panel)
{
	return 0;
}

static int rpi_touchscreen_enable(struct drm_panel *panel)
{
	struct rpi_touchscreen *ts = panel_to_ts(panel);
	int i;

	rpi_touchscreen_i2c_write(ts, REG_POWERON, 1);
	/* Wait for nPWRDWN to go low to indicate poweron is done. */
	for (i = 0; i < 100; i++) {
		if (rpi_touchscreen_i2c_read(ts, REG_PORTB) & 1)
			break;
	}

	rpi_touchscreen_write(ts, DSI_LANEENABLE,
			      DSI_LANEENABLE_CLOCK |
			      DSI_LANEENABLE_D0);
	rpi_touchscreen_write(ts, PPI_D0S_CLRSIPOCOUNT, 0x05);
	rpi_touchscreen_write(ts, PPI_D1S_CLRSIPOCOUNT, 0x05);
	rpi_touchscreen_write(ts, PPI_D0S_ATMR, 0x00);
	rpi_touchscreen_write(ts, PPI_D1S_ATMR, 0x00);
	rpi_touchscreen_write(ts, PPI_LPTXTIMECNT, 0x03);

	rpi_touchscreen_write(ts, SPICMR, 0x00);
	rpi_touchscreen_write(ts, LCDCTRL, 0x00100150);
	rpi_touchscreen_write(ts, SYSCTRL, 0x040f);
	msleep(100);

	rpi_touchscreen_write(ts, PPI_STARTPPI, 0x01);
	rpi_touchscreen_write(ts, DSI_STARTDSI, 0x01);
	msleep(100);

	/* Turn on the backlight. */
	rpi_touchscreen_i2c_write(ts, REG_PWM, 255);

	/* Default to the same orientation as the closed source
	 * firmware used for the panel.  Runtime rotation
	 * configuration will be supported using VC4's plane
	 * orientation bits.
	 */
	rpi_touchscreen_i2c_write(ts, REG_PORTA, BIT(2));

	return 0;
}

static int rpi_touchscreen_get_modes(struct drm_panel *panel)
{
	struct drm_connector *connector = panel->connector;
	struct drm_device *drm = panel->drm;
	unsigned int i, num = 0;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	for (i = 0; i < ARRAY_SIZE(rpi_touchscreen_modes); i++) {
		const struct drm_display_mode *m = &rpi_touchscreen_modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(drm, m);
		if (!mode) {
			dev_err(drm->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay, m->vrefresh);
			continue;
		}

		mode->type |= DRM_MODE_TYPE_DRIVER;

		if (i == 0)
			mode->type |= DRM_MODE_TYPE_PREFERRED;

		drm_mode_set_name(mode);

		drm_mode_probed_add(connector, mode);
		num++;
	}

	connector->display_info.bpc = 8;
	connector->display_info.width_mm = 154;
	connector->display_info.height_mm = 86;
	drm_display_info_set_bus_formats(&connector->display_info,
					 &bus_format, 1);

	return num;
}

static const struct drm_panel_funcs rpi_touchscreen_funcs = {
	.disable = rpi_touchscreen_disable,
	.unprepare = rpi_touchscreen_noop,
	.prepare = rpi_touchscreen_noop,
	.enable = rpi_touchscreen_enable,
	.get_modes = rpi_touchscreen_get_modes,
};

static int rpi_touchscreen_probe(struct i2c_client *i2c,
				 const struct i2c_device_id *id)
{
	struct device *dev = &i2c->dev;
	struct rpi_touchscreen *ts;
	struct device_node *endpoint, *dsi_host_node;
	struct mipi_dsi_host *host;
	int ret, ver;
	struct mipi_dsi_device_info info = {
		.type = RPI_DSI_DRIVER_NAME,
		.channel = 0,
		.node = NULL,
	};

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ts);

	ts->i2c = i2c;

	ver = rpi_touchscreen_i2c_read(ts, REG_ID);
	if (ver < 0) {
		dev_err(dev, "Atmel I2C read failed: %d\n", ver);
		return -ENODEV;
	}

	switch (ver) {
	case 0xde: /* ver 1 */
	case 0xc3: /* ver 2 */
		break;
	default:
		dev_err(dev, "Unknown Atmel firmware revision: 0x%02x\n", ver);
		return -ENODEV;
	}

	/* Turn off at boot, so we can cleanly sequence powering on. */
	rpi_touchscreen_i2c_write(ts, REG_POWERON, 0);

	/* Look up the DSI host.  It needs to probe before we do. */
	endpoint = of_graph_get_next_endpoint(dev->of_node, NULL);
	if (!endpoint)
		return -ENODEV;

	dsi_host_node = of_graph_get_remote_port_parent(endpoint);
	if (!dsi_host_node)
		goto error;

	host = of_find_mipi_dsi_host_by_node(dsi_host_node);
	of_node_put(dsi_host_node);
	if (!host) {
		of_node_put(endpoint);
		return -EPROBE_DEFER;
	}

	info.node = of_graph_get_remote_port(endpoint);
	if (!info.node)
		goto error;

	of_node_put(endpoint);

	ts->dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(ts->dsi)) {
		dev_err(dev, "DSI device registration failed: %ld\n",
			PTR_ERR(ts->dsi));
		return PTR_ERR(ts->dsi);
	}

	drm_panel_init(&ts->base);
	ts->base.dev = dev;
	ts->base.funcs = &rpi_touchscreen_funcs;

	/* This appears last, as it's what will unblock the DSI host
	 * driver's component bind function.
	 */
	ret = drm_panel_add(&ts->base);
	if (ret)
		return ret;

	return 0;

error:
	of_node_put(endpoint);
	return -ENODEV;
}

static int rpi_touchscreen_remove(struct i2c_client *i2c)
{
	struct rpi_touchscreen *ts = i2c_get_clientdata(i2c);

	mipi_dsi_detach(ts->dsi);

	drm_panel_remove(&ts->base);

	mipi_dsi_device_unregister(ts->dsi);
	kfree(ts->dsi);

	return 0;
}

static int rpi_touchscreen_dsi_probe(struct mipi_dsi_device *dsi)
{
	int ret;

	dsi->mode_flags = (MIPI_DSI_MODE_VIDEO |
			   MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			   MIPI_DSI_MODE_LPM);
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 1;

	ret = mipi_dsi_attach(dsi);

	if (ret)
		dev_err(&dsi->dev, "failed to attach dsi to host: %d\n", ret);

	return ret;
}

static struct mipi_dsi_driver rpi_touchscreen_dsi_driver = {
	.driver.name = RPI_DSI_DRIVER_NAME,
	.probe = rpi_touchscreen_dsi_probe,
};

static const struct of_device_id rpi_touchscreen_of_ids[] = {
	{ .compatible = "raspberrypi,7inch-touchscreen-panel" },
	{ } /* sentinel */
};
MODULE_DEVICE_TABLE(of, rpi_touchscreen_of_ids);

static struct i2c_driver rpi_touchscreen_driver = {
	.driver = {
		.name = "rpi_touchscreen",
		.of_match_table = rpi_touchscreen_of_ids,
	},
	.probe = rpi_touchscreen_probe,
	.remove = rpi_touchscreen_remove,
};

static int __init rpi_touchscreen_init(void)
{
	mipi_dsi_driver_register(&rpi_touchscreen_dsi_driver);
	return i2c_add_driver(&rpi_touchscreen_driver);
}
module_init(rpi_touchscreen_init);

static void __exit rpi_touchscreen_exit(void)
{
	i2c_del_driver(&rpi_touchscreen_driver);
	mipi_dsi_driver_unregister(&rpi_touchscreen_dsi_driver);
}
module_exit(rpi_touchscreen_exit);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("Raspberry Pi 7-inch touchscreen driver");
MODULE_LICENSE("GPL v2");
