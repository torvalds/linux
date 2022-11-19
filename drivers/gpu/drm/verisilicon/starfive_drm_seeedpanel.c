// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 *
 * reference to seeed5inch.c
 */
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include "vs_drv.h"
#define RPI_DSI_DRIVER_NAME "starfive-dri-panel"

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
#define DSI_LANEENABLE_CLOCK		BIT(0)
#define DSI_LANEENABLE_D0		BIT(1)
#define DSI_LANEENABLE_D1		BIT(2)

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

struct seeed_panel_dev {
	struct i2c_client *client;
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct device   *dev;
	int irq;

};

static int seeed_panel_i2c_write(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg;
	buf[1] = val;
	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = 2;

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	return ret;
}

static int seeed_panel_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2];
	int ret;

	buf[0] = reg;
	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = buf;
	msg[0].len = 1;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = 1;
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0)
		return 0;

	return ret;
}

enum dsi_rgb_pattern_t {
	RGB_PAT_WHITE,
	RGB_PAT_BLACK,
	RGB_PAT_RED,
	RGB_PAT_GREEN,
	RGB_PAT_BLUE,
	RGB_PAT_HORIZ_COLORBAR,
	RGB_PAT_VERT_COLORBAR,
	RGB_PAT_NUM
};

static struct seeed_panel_dev *panel_to_seeed(struct drm_panel *panel)
{
	return container_of(panel, struct seeed_panel_dev, base);
}

static const struct drm_display_mode seeed_panel_modes[] = {
#ifdef PLL_1228M
	{
		.clock = 27306666 / 1000,
		.hdisplay = 800,
		.hsync_start = 800 + 93,
		.hsync_end = 800 + 93 + 5,
		.htotal = 800 + 93 + 5 + 5,
		.vdisplay = 480,
		.vsync_start = 480 + 14,
		.vsync_end = 480 + 14 + 5,
		.vtotal = 480 + 14 + 5 + 5,
	},
#endif
	{// pll 1188M
		.clock = 29700000 / 1000,
		.hdisplay = 800,
		.hsync_start = 800 + 90,
		.hsync_end = 800 + 90 + 5,
		.htotal = 800 + 90 + 5 + 5,
		.vdisplay = 480,
		.vsync_start = 480 + 60,
		.vsync_end = 480 + 60 + 5,
		.vtotal = 480 + 60 + 5 + 5,
	},
};

static int seeed_panel_disable(struct drm_panel *panel)
{
	struct seeed_panel_dev *sp = panel_to_seeed(panel);

	seeed_panel_i2c_write(sp->client, REG_PWM, 0);
	seeed_panel_i2c_write(sp->client, REG_POWERON, 0);
	udelay(1);

	return 0;
}

static int seeed_panel_noop(struct drm_panel *panel)
{
	return 0;
}

static int seeed_dsi_write(struct drm_panel *panel, u16 reg, u32 val)
{
	struct seeed_panel_dev *sp = panel_to_seeed(panel);

	u8 msg[] = {
		reg,
		reg >> 8,
		val,
		val >> 8,
		val >> 16,
		val >> 24,
	};
	mipi_dsi_generic_write(sp->dsi, msg, sizeof(msg));

	return 0;
}

static int seeed_panel_enable(struct drm_panel *panel)
{
	struct seeed_panel_dev *sp = panel_to_seeed(panel);
	int i;
	u8 reg_value = 0;

	seeed_panel_i2c_write(sp->client, REG_POWERON, 1);
	/* Wait for nPWRDWN to go low to indicate poweron is done. */
	for (i = 0; i < 100; i++) {
		seeed_panel_i2c_read(sp->client, REG_PORTB, &reg_value);
		if (reg_value & 1)
			break;
	}

	seeed_dsi_write(panel, DSI_LANEENABLE,
				DSI_LANEENABLE_CLOCK |
				DSI_LANEENABLE_D0);
	seeed_dsi_write(panel, PPI_D0S_CLRSIPOCOUNT, 0x05);
	seeed_dsi_write(panel, PPI_D1S_CLRSIPOCOUNT, 0x05);
	seeed_dsi_write(panel, PPI_D0S_ATMR, 0x00);
	seeed_dsi_write(panel, PPI_D1S_ATMR, 0x00);
	seeed_dsi_write(panel, PPI_LPTXTIMECNT, 0x03);
	seeed_dsi_write(panel, SPICMR, 0x00);
	seeed_dsi_write(panel, LCDCTRL, 0x00100150);
	seeed_dsi_write(panel, SYSCTRL, 0x040f);
	msleep(100);

	seeed_dsi_write(panel, PPI_STARTPPI, 0x01);
	seeed_dsi_write(panel, DSI_STARTDSI, 0x01);
	msleep(100);

	/* Turn on the backlight. */
	seeed_panel_i2c_write(sp->client, REG_PWM, 255);

	/* Default to the same orientation as the closed source
	 * firmware used for the panel.  Runtime rotation
	 * configuration will be supported using VC4's plane
	 * orientation bits.
	 */
	seeed_panel_i2c_write(sp->client, REG_PORTA, BIT(2));

	return 0;
}

static int seeed_panel_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	unsigned int i, num = 0;
	static const u32 bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	for (i = 0; i < ARRAY_SIZE(seeed_panel_modes); i++) {
		const struct drm_display_mode *m = &seeed_panel_modes[i];
		struct drm_display_mode *mode;

		mode = drm_mode_duplicate(connector->dev, m);
		if (!mode) {
			dev_err(panel->dev, "failed to add mode %ux%u@%u\n",
				m->hdisplay, m->vdisplay,
				drm_mode_vrefresh(m));
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

static const struct drm_panel_funcs seeed_panel_funcs = {
	.disable = seeed_panel_disable,
	.unprepare = seeed_panel_noop,
	.prepare = seeed_panel_noop,
	.enable = seeed_panel_enable,
	.get_modes = seeed_panel_get_modes,
};

static int seeed_panel_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	u8 reg_value = 0;
	struct seeed_panel_dev *seeed_panel;
	struct device_node *endpoint, *dsi_host_node;
	struct mipi_dsi_host *host;
	struct device *dev = &client->dev;

	struct mipi_dsi_device_info info = {
		.type = RPI_DSI_DRIVER_NAME,
		.channel = 0, //0,
		.node = NULL,
	};
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev,
			 "I2C adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	seeed_panel = devm_kzalloc(&client->dev, sizeof(struct seeed_panel_dev), GFP_KERNEL);
	if (!seeed_panel)
		return -ENOMEM;

	seeed_panel->client = client;
	i2c_set_clientdata(client, seeed_panel);

	seeed_panel_i2c_read(client, REG_ID, &reg_value);
	switch (reg_value) {
	case 0xde: /* ver 1 */
	case 0xc3: /* ver 2 */
	break;

	default:
		dev_err(&client->dev, "Unknown Atmel firmware revision: 0x%02x\n", reg_value);
		return -ENODEV;
	}

	seeed_panel_i2c_write(client, REG_POWERON, 0);

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

	drm_panel_init(&seeed_panel->base, dev, &seeed_panel_funcs,
			DRM_MODE_CONNECTOR_DSI);

	/* This appears last, as it's what will unblock the DSI host
	 * driver's component bind function.
	 */
	drm_panel_add(&seeed_panel->base);

	info.node = of_node_get(of_graph_get_remote_port(endpoint));
	if (!info.node)
		goto error;

	of_node_put(endpoint);

	seeed_panel->dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(seeed_panel->dsi)) {
		dev_err(dev, "DSI device registration failed: %ld\n",
			PTR_ERR(seeed_panel->dsi));
		return PTR_ERR(seeed_panel->dsi);
	}

	return 0;
error:
	of_node_put(endpoint);
	return -ENODEV;

}

static int seeed_panel_remove(struct i2c_client *client)
{
	struct seeed_panel_dev *seeed_panel = i2c_get_clientdata(client);

	mipi_dsi_detach(seeed_panel->dsi);

	drm_panel_remove(&seeed_panel->base);

	mipi_dsi_device_unregister(seeed_panel->dsi);
	// kfree(seeed_panel->dsi);
	return 0;
}

static const struct i2c_device_id seeed_panel_id[] = {
	{ "seeed_panel", 0 },
	{ }
};

static const struct of_device_id seeed_panel_dt_ids[] = {
	{ .compatible = "seeed_panel", },
	{ /* sentinel */ }
};

static struct i2c_driver seeed_panel_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "seeed_panel",
		.of_match_table = seeed_panel_dt_ids,
	},
	.probe		= seeed_panel_probe,
	.remove		= seeed_panel_remove,
	.id_table	= seeed_panel_id,
};

static int seeed_dsi_probe(struct mipi_dsi_device *dsi)
{
	int ret;

	dsi->mode_flags = (MIPI_DSI_MODE_VIDEO |
				MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
				MIPI_DSI_MODE_LPM);
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 1;
	dsi->hs_rate = 750000000;

	ret = mipi_dsi_attach(dsi);
	if (ret)
		dev_err(&dsi->dev, "failed to attach dsi to host: %d\n", ret);

	return ret;
}

static struct mipi_dsi_driver seeed_dsi_driver = {
	.driver.name = RPI_DSI_DRIVER_NAME,
	.probe = seeed_dsi_probe,
};

static int __init init_seeed_panel(void)
{
	int err;

	mipi_dsi_driver_register(&seeed_dsi_driver);
	err = i2c_add_driver(&seeed_panel_driver);
	return err;
}
module_init(init_seeed_panel);

static void __exit exit_seeed_panel(void)
{
	i2c_del_driver(&seeed_panel_driver);
	mipi_dsi_driver_unregister(&seeed_dsi_driver);
}
module_exit(exit_seeed_panel);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("Raspberry Pi 7-inch touchscreen driver");
MODULE_LICENSE("GPL v2");

