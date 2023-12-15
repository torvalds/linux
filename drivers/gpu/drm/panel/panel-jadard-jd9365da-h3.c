// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Radxa Limited
 * Copyright (c) 2022 Edgeble AI Technologies Pvt. Ltd.
 *
 * Author:
 * - Jagan Teki <jagan@amarulasolutions.com>
 * - Stephen Chen <stephen@radxa.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <video/display_timing.h>
#include <video/videomode.h>


//accel sc7a20
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/common/st_sensors_i2c.h>
#include <linux/iio/common/st_sensors.h>
//#include "st_accel.h"

#define DSI_DRIVER_NAME "starfive-dri"

enum cmd_type {
	CMD_TYPE_DCS,
	CMD_TYPE_DELAY,
};

struct jadard_init_cmd {
	enum cmd_type type;
	const char *data;
	size_t len;
};

#define _INIT_CMD_DCS(...)					\
	{							\
		.type	= CMD_TYPE_DCS,				\
		.data	= (char[]){__VA_ARGS__},		\
		.len	= sizeof((char[]){__VA_ARGS__})		\
	}							\

#define _INIT_CMD_DELAY(...)					\
	{							\
		.type	= CMD_TYPE_DELAY,			\
		.data	= (char[]){__VA_ARGS__},		\
		.len	= sizeof((char[]){__VA_ARGS__})		\
	}							\

struct jadard_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	const struct jadard_init_cmd *init_cmds;
	u32 num_init_cmds;
	const struct display_timing *timings;
	unsigned int num_timings;
};

struct jadard {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct jadard_panel_desc *desc;
	struct i2c_client *client;

	struct device   *dev;

	struct regulator *vdd;
	struct regulator *vccio;
	struct gpio_desc *reset;
	struct gpio_desc *enable;
	bool enable_initialized;
	int choosemode;
};

static inline struct jadard *panel_to_jadard(struct drm_panel *panel)
{
	return container_of(panel, struct jadard, panel);
}

static int jadard_i2c_write(struct i2c_client *client, u8 reg, u8 val)
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

static int jadard_i2c_read(struct i2c_client *client, u8 reg, u8 *val)
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

static int jadard_enable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct jadard *jadard = panel_to_jadard(panel);
	const struct jadard_panel_desc *desc = jadard->desc;
	struct mipi_dsi_device *dsi = jadard->dsi;
	unsigned int i;
	int err;
	if (jadard->enable_initialized == true)
		return 0;

	if(jadard->choosemode == 0) {//8inch

		for (i = 0; i < desc->num_init_cmds; i++) {
			const struct jadard_init_cmd *cmd = &desc->init_cmds[i];

			switch (cmd->type) {
			case CMD_TYPE_DELAY:
				msleep(cmd->data[0]);
				err = 0;
				break;
			case CMD_TYPE_DCS:
				err = mipi_dsi_dcs_write(dsi, cmd->data[0],
							 cmd->len <= 1 ? NULL : &cmd->data[1],
							 cmd->len - 1);
				break;
			default:
				err = -EINVAL;
			}

			if (err < 0) {
				DRM_DEV_ERROR(dev, "failed to write CMD#0x%x\n", cmd->data[0]);
				return err;
			}

		}

		err = mipi_dsi_dcs_exit_sleep_mode(dsi);
		if (err < 0)
			DRM_DEV_ERROR(dev, "failed to exit sleep mode ret = %d\n", err);
		msleep(120);

		err =  mipi_dsi_dcs_set_display_on(dsi);
		if (err < 0)
			DRM_DEV_ERROR(dev, "failed to set display on ret = %d\n", err);
	}

	jadard->enable_initialized = true ;

	return 0;
}

static int jadard_disable(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct jadard *jadard = panel_to_jadard(panel);
	int ret;
	if(jadard->choosemode == 0) {//8inch
		ret = mipi_dsi_dcs_set_display_off(jadard->dsi);
		if (ret < 0)
			DRM_DEV_ERROR(dev, "failed to set display off: %d\n", ret);

		ret = mipi_dsi_dcs_enter_sleep_mode(jadard->dsi);
		if (ret < 0)
			DRM_DEV_ERROR(dev, "failed to enter sleep mode: %d\n", ret);

	}
	jadard->enable_initialized = false;

	return 0;
}

static int jadard_prepare(struct drm_panel *panel)
{
	struct device *dev = panel->dev;
	struct jadard *jadard = panel_to_jadard(panel);
	const struct jadard_panel_desc *desc = jadard->desc;
	struct mipi_dsi_device *dsi = jadard->dsi;
	unsigned int i;
	int err;

	if (jadard->enable_initialized == true)
		return 0;

	gpiod_direction_output(jadard->enable, 0);
	gpiod_set_value(jadard->enable, 1);
	mdelay(100);

	gpiod_direction_output(jadard->reset, 0);
	mdelay(100);
	gpiod_set_value(jadard->reset, 1);
	mdelay(100);
	gpiod_set_value(jadard->reset, 0);
	mdelay(100);
	gpiod_set_value(jadard->reset, 1);
	mdelay(150);

	return 0;
}

static int jadard_unprepare(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);

	gpiod_set_value(jadard->reset, 1);
	msleep(120);
#if 0
	regulator_disable(jadard->vdd);
	regulator_disable(jadard->vccio);
#endif
	return 0;
}

static int jadard_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct jadard *jadard = panel_to_jadard(panel);
	const struct drm_display_mode *desc_mode = &jadard->desc->mode;
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, desc_mode);
	if (!mode) {
		DRM_DEV_ERROR(&jadard->dsi->dev, "failed to add mode %ux%ux@%u\n",
			      desc_mode->hdisplay, desc_mode->vdisplay,
			      drm_mode_vrefresh(desc_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static int seiko_panel_get_timings(struct drm_panel *panel,
					unsigned int num_timings,
					struct display_timing *timings)
{
	struct jadard *jadard = panel_to_jadard(panel);
	unsigned int i;

	if (jadard->desc->num_timings < num_timings)
		num_timings = jadard->desc->num_timings;

	if (timings)
		for (i = 0; i < num_timings; i++)
			timings[i] = jadard->desc->timings[i];

	return jadard->desc->num_timings;
}

static const struct drm_panel_funcs jadard_funcs = {
	.disable = jadard_disable,
	.unprepare = jadard_unprepare,
	.prepare = jadard_prepare,
	.enable = jadard_enable,
	.get_modes = jadard_get_modes,
	.get_timings = seiko_panel_get_timings,
};

static const struct jadard_init_cmd cz101b4001_init_cmds[] = {
	_INIT_CMD_DCS(0x01),
	_INIT_CMD_DELAY(100),
	_INIT_CMD_DCS(0xE0, 0x00),
	_INIT_CMD_DCS(0xE1, 0x93),
	_INIT_CMD_DCS(0xE2, 0x65),
	_INIT_CMD_DCS(0xE3, 0xF8),
	_INIT_CMD_DCS(0x80, 0x03),
	_INIT_CMD_DCS(0xE0, 0x01),
	_INIT_CMD_DCS(0x00, 0x00),
	_INIT_CMD_DCS(0x01, 0x7E),
	_INIT_CMD_DCS(0x03, 0x00),
	_INIT_CMD_DCS(0x04, 0x65),
	_INIT_CMD_DCS(0x0C, 0x74),
	_INIT_CMD_DCS(0x17, 0x00),
	_INIT_CMD_DCS(0x18, 0xB7),
	_INIT_CMD_DCS(0x19, 0x00),
	_INIT_CMD_DCS(0x1A, 0x00),
	_INIT_CMD_DCS(0x1B, 0xB7),
	_INIT_CMD_DCS(0x1C, 0x00),
	_INIT_CMD_DCS(0x24, 0xFE),
	_INIT_CMD_DCS(0x37, 0x19),
	_INIT_CMD_DCS(0x38, 0x05),
	_INIT_CMD_DCS(0x39, 0x00),
	_INIT_CMD_DCS(0x3A, 0x01),
	_INIT_CMD_DCS(0x3B, 0x01),
	_INIT_CMD_DCS(0x3C, 0x70),
	_INIT_CMD_DCS(0x3D, 0xFF),
	_INIT_CMD_DCS(0x3E, 0xFF),
	_INIT_CMD_DCS(0x3F, 0xFF),
	_INIT_CMD_DCS(0x40, 0x06),
	_INIT_CMD_DCS(0x41, 0xA0),
	_INIT_CMD_DCS(0x43, 0x1E),
	_INIT_CMD_DCS(0x44, 0x0F),
	_INIT_CMD_DCS(0x45, 0x28),
	_INIT_CMD_DCS(0x4B, 0x04),
	_INIT_CMD_DCS(0x55, 0x02),
	_INIT_CMD_DCS(0x56, 0x01),
	_INIT_CMD_DCS(0x57, 0xA9),
	_INIT_CMD_DCS(0x58, 0x0A),
	_INIT_CMD_DCS(0x59, 0x0A),
	_INIT_CMD_DCS(0x5A, 0x37),
	_INIT_CMD_DCS(0x5B, 0x19),
	_INIT_CMD_DCS(0x5D, 0x78),
	_INIT_CMD_DCS(0x5E, 0x63),
	_INIT_CMD_DCS(0x5F, 0x54),
	_INIT_CMD_DCS(0x60, 0x49),
	_INIT_CMD_DCS(0x61, 0x45),
	_INIT_CMD_DCS(0x62, 0x38),
	_INIT_CMD_DCS(0x63, 0x3D),
	_INIT_CMD_DCS(0x64, 0x28),
	_INIT_CMD_DCS(0x65, 0x43),
	_INIT_CMD_DCS(0x66, 0x41),
	_INIT_CMD_DCS(0x67, 0x43),
	_INIT_CMD_DCS(0x68, 0x62),
	_INIT_CMD_DCS(0x69, 0x50),
	_INIT_CMD_DCS(0x6A, 0x57),
	_INIT_CMD_DCS(0x6B, 0x49),
	_INIT_CMD_DCS(0x6C, 0x44),
	_INIT_CMD_DCS(0x6D, 0x37),
	_INIT_CMD_DCS(0x6E, 0x23),
	_INIT_CMD_DCS(0x6F, 0x10),
	_INIT_CMD_DCS(0x70, 0x78),
	_INIT_CMD_DCS(0x71, 0x63),
	_INIT_CMD_DCS(0x72, 0x54),
	_INIT_CMD_DCS(0x73, 0x49),
	_INIT_CMD_DCS(0x74, 0x45),
	_INIT_CMD_DCS(0x75, 0x38),
	_INIT_CMD_DCS(0x76, 0x3D),
	_INIT_CMD_DCS(0x77, 0x28),
	_INIT_CMD_DCS(0x78, 0x43),
	_INIT_CMD_DCS(0x79, 0x41),
	_INIT_CMD_DCS(0x7A, 0x43),
	_INIT_CMD_DCS(0x7B, 0x62),
	_INIT_CMD_DCS(0x7C, 0x50),
	_INIT_CMD_DCS(0x7D, 0x57),
	_INIT_CMD_DCS(0x7E, 0x49),
	_INIT_CMD_DCS(0x7F, 0x44),
	_INIT_CMD_DCS(0x80, 0x37),
	_INIT_CMD_DCS(0x81, 0x23),
	_INIT_CMD_DCS(0x82, 0x10),
	_INIT_CMD_DCS(0xE0, 0x02),
	_INIT_CMD_DCS(0x00, 0x47),
	_INIT_CMD_DCS(0x01, 0x47),
	_INIT_CMD_DCS(0x02, 0x45),
	_INIT_CMD_DCS(0x03, 0x45),
	_INIT_CMD_DCS(0x04, 0x4B),
	_INIT_CMD_DCS(0x05, 0x4B),
	_INIT_CMD_DCS(0x06, 0x49),
	_INIT_CMD_DCS(0x07, 0x49),
	_INIT_CMD_DCS(0x08, 0x41),
	_INIT_CMD_DCS(0x09, 0x1F),
	_INIT_CMD_DCS(0x0A, 0x1F),
	_INIT_CMD_DCS(0x0B, 0x1F),
	_INIT_CMD_DCS(0x0C, 0x1F),
	_INIT_CMD_DCS(0x0D, 0x1F),
	_INIT_CMD_DCS(0x0E, 0x1F),
	_INIT_CMD_DCS(0x0F, 0x5F),
	_INIT_CMD_DCS(0x10, 0x5F),
	_INIT_CMD_DCS(0x11, 0x57),
	_INIT_CMD_DCS(0x12, 0x77),
	_INIT_CMD_DCS(0x13, 0x35),
	_INIT_CMD_DCS(0x14, 0x1F),
	_INIT_CMD_DCS(0x15, 0x1F),
	_INIT_CMD_DCS(0x16, 0x46),
	_INIT_CMD_DCS(0x17, 0x46),
	_INIT_CMD_DCS(0x18, 0x44),
	_INIT_CMD_DCS(0x19, 0x44),
	_INIT_CMD_DCS(0x1A, 0x4A),
	_INIT_CMD_DCS(0x1B, 0x4A),
	_INIT_CMD_DCS(0x1C, 0x48),
	_INIT_CMD_DCS(0x1D, 0x48),
	_INIT_CMD_DCS(0x1E, 0x40),
	_INIT_CMD_DCS(0x1F, 0x1F),
	_INIT_CMD_DCS(0x20, 0x1F),
	_INIT_CMD_DCS(0x21, 0x1F),
	_INIT_CMD_DCS(0x22, 0x1F),
	_INIT_CMD_DCS(0x23, 0x1F),
	_INIT_CMD_DCS(0x24, 0x1F),
	_INIT_CMD_DCS(0x25, 0x5F),
	_INIT_CMD_DCS(0x26, 0x5F),
	_INIT_CMD_DCS(0x27, 0x57),
	_INIT_CMD_DCS(0x28, 0x77),
	_INIT_CMD_DCS(0x29, 0x35),
	_INIT_CMD_DCS(0x2A, 0x1F),
	_INIT_CMD_DCS(0x2B, 0x1F),
	_INIT_CMD_DCS(0x58, 0x40),
	_INIT_CMD_DCS(0x59, 0x00),
	_INIT_CMD_DCS(0x5A, 0x00),
	_INIT_CMD_DCS(0x5B, 0x10),
	_INIT_CMD_DCS(0x5C, 0x06),
	_INIT_CMD_DCS(0x5D, 0x40),
	_INIT_CMD_DCS(0x5E, 0x01),
	_INIT_CMD_DCS(0x5F, 0x02),
	_INIT_CMD_DCS(0x60, 0x30),
	_INIT_CMD_DCS(0x61, 0x01),
	_INIT_CMD_DCS(0x62, 0x02),
	_INIT_CMD_DCS(0x63, 0x03),
	_INIT_CMD_DCS(0x64, 0x6B),
	_INIT_CMD_DCS(0x65, 0x05),
	_INIT_CMD_DCS(0x66, 0x0C),
	_INIT_CMD_DCS(0x67, 0x73),
	_INIT_CMD_DCS(0x68, 0x09),
	_INIT_CMD_DCS(0x69, 0x03),
	_INIT_CMD_DCS(0x6A, 0x56),
	_INIT_CMD_DCS(0x6B, 0x08),
	_INIT_CMD_DCS(0x6C, 0x00),
	_INIT_CMD_DCS(0x6D, 0x04),
	_INIT_CMD_DCS(0x6E, 0x04),
	_INIT_CMD_DCS(0x6F, 0x88),
	_INIT_CMD_DCS(0x70, 0x00),
	_INIT_CMD_DCS(0x71, 0x00),
	_INIT_CMD_DCS(0x72, 0x06),
	_INIT_CMD_DCS(0x73, 0x7B),
	_INIT_CMD_DCS(0x74, 0x00),
	_INIT_CMD_DCS(0x75, 0xF8),
	_INIT_CMD_DCS(0x76, 0x00),
	_INIT_CMD_DCS(0x77, 0xD5),
	_INIT_CMD_DCS(0x78, 0x2E),
	_INIT_CMD_DCS(0x79, 0x12),
	_INIT_CMD_DCS(0x7A, 0x03),
	_INIT_CMD_DCS(0x7B, 0x00),
	_INIT_CMD_DCS(0x7C, 0x00),
	_INIT_CMD_DCS(0x7D, 0x03),
	_INIT_CMD_DCS(0x7E, 0x7B),
	_INIT_CMD_DCS(0xE0, 0x04),
	_INIT_CMD_DCS(0x00, 0x0E),
	_INIT_CMD_DCS(0x02, 0xB3),
	_INIT_CMD_DCS(0x09, 0x60),
	_INIT_CMD_DCS(0x0E, 0x2A),
	_INIT_CMD_DCS(0x36, 0x59),
	_INIT_CMD_DCS(0xE0, 0x00),

	_INIT_CMD_DELAY(120),
};

static const struct display_timing jadard_timing[] = {
	{
	.pixelclock = { 79200000, 79200000, 79200000 },
	.hactive = { 800, 800, 800 },
	.hfront_porch = {  356, 356, 356 },
	.hback_porch = { 134, 134, 134 },
	.hsync_len = { 7, 7, 7 },
	.vactive = { 1280, 1280, 1280 },
	.vfront_porch = { 84, 84, 84 },
	.vback_porch = { 20, 20, 20 },
	.vsync_len = { 9, 9, 9 },
	.flags = DISPLAY_FLAGS_DE_LOW,
	},
	{
	 .pixelclock = { 148500000, 148500000, 148500000 },
	 .hactive = { 1200, 1200, 1200 },
	 .hfront_porch = {	246, 246, 246 },
	 .hback_porch = { 5, 5, 5 },
	 .hsync_len = { 5, 5, 5 },
	 .vactive = { 1920, 1920, 1920 },
	 .vfront_porch = { 84, 84, 84 },
	 .vback_porch = { 20, 20, 20 },
	 .vsync_len = { 16, 16, 16 },
	 .flags = DISPLAY_FLAGS_DE_LOW,
	},
	{}
};

static const struct jadard_panel_desc cz101b4001_desc[] = {
	{
	.mode = {
		.clock		= 79200,

		.hdisplay	= 800,
		.hsync_start	= 800 + 180,
		.hsync_end	= 800 + 180 + 15,
		.htotal		= 800 + 180 + 15 + 45,

		.vdisplay	= 1280,
		.vsync_start	= 1280 + 84,
		.vsync_end	= 1280 + 84 + 20,
		.vtotal		= 1280 + 84+ 20 + 7,

		.width_mm	= 62,
		.height_mm	= 110,
		.type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.init_cmds = cz101b4001_init_cmds,
	.num_init_cmds = ARRAY_SIZE(cz101b4001_init_cmds),
	.timings = &jadard_timing[0],
	.num_timings = 1,
	},
	{
	.mode = {
		 .clock 	 = 148500,

		 .hdisplay	 = 1200,
		 .hsync_start	 = 1200 + 246,
		 .hsync_end  = 1200 + 246 + 5,
		 .htotal	 = 1200 + 246 + 5 + 5,

		 .vdisplay	 = 1920,
		 .vsync_start	 = 1920 + 84,
		 .vsync_end  = 1920 + 84 + 20,
		 .vtotal	 = 1920 + 84+ 20 + 16,

		 .width_mm	 = 62,
		 .height_mm  = 110,
		 .type		 = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	 },
	 .lanes = 4,
	 .format = MIPI_DSI_FMT_RGB888,
	 .init_cmds = cz101b4001_init_cmds,//no
	 .num_init_cmds = ARRAY_SIZE(cz101b4001_init_cmds),//no
	 //.timings = &starfive_timing,
	 .timings = &jadard_timing[1],
	 .num_timings = 1,
	},
	{}
};

static int panel_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	u8 reg_value = 0;
	struct jadard *jd_panel;
	const struct jadard_panel_desc *desc;

	struct device_node *endpoint, *dsi_host_node;
	struct mipi_dsi_host *host;
	struct device *dev = &client->dev;

	const struct st_sensor_settings *settings;
	struct st_sensor_data *adata;
	struct iio_dev *indio_dev;
	int err; u8 mode = 1;int ret = 0;


	struct mipi_dsi_device_info info = {
		.type = DSI_DRIVER_NAME,
		.channel = 1, //0,
		.node = NULL,
	};

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev,
			 "I2C adapter doesn't support I2C_FUNC_SMBUS_BYTE\n");
		return -EIO;
	}

	jd_panel = devm_kzalloc(&client->dev, sizeof(struct jadard), GFP_KERNEL);
	if (!jd_panel )
		return -ENOMEM;

	desc = &cz101b4001_desc[0];//use 8inch parameter to pre config dsi and phy

	jd_panel ->client = client;
	i2c_set_clientdata(client, jd_panel);

	jd_panel->enable_initialized = false;

	jd_panel->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(jd_panel->reset)) {
		DRM_DEV_ERROR(dev, "failed to get our reset GPIO\n");
		return PTR_ERR(jd_panel->reset);
	}

	jd_panel->enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(jd_panel->enable)) {
		DRM_DEV_ERROR(dev, "failed to get our enable GPIO\n");
		return PTR_ERR(jd_panel->enable);
	}

	/*use i2c read to detect whether the panel has connected */
	ret = jadard_i2c_read(client, 0x00, &reg_value);
	if (ret < 0)
	{
		dev_info(dev, "no 4lane connect!!!!\n");
		return -ENODEV;
	}
	dev_info(dev, "==4lane panel!!! maybe 8inch==\n");
	jd_panel->choosemode = 0;
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

	drm_panel_init(&jd_panel->panel, dev, &jadard_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&jd_panel->panel);

	info.node = of_node_get(of_graph_get_remote_port(endpoint));
	if (!info.node)
		goto error;

	of_node_put(endpoint);
	jd_panel->desc = desc;

	jd_panel->dsi = mipi_dsi_device_register_full(host, &info);
	if (IS_ERR(jd_panel->dsi)) {
		dev_err(dev, "DSI device registration failed: %ld\n",
			PTR_ERR(jd_panel->dsi));
		return PTR_ERR(jd_panel->dsi);
	}

	mipi_dsi_set_drvdata(jd_panel->dsi, jd_panel);

	//radxa 10inch connect detect
	gpiod_direction_output(jd_panel->enable, 0);
	gpiod_set_value(jd_panel->enable, 1);
	mdelay(100);

	gpiod_direction_output(jd_panel->reset, 0);
	mdelay(100);
	gpiod_set_value(jd_panel->reset, 1);
	mdelay(100);
	gpiod_set_value(jd_panel->reset, 0);
	mdelay(100);
	gpiod_set_value(jd_panel->reset, 1);
	mdelay(150);

	jd_panel->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;
	if(jd_panel->dsi)
	{
		//use this command to detect the connected status
		err = mipi_dsi_dcs_get_power_mode(jd_panel->dsi, &mode);
		dev_info(dev,"dsi command return %d, mode %d\n", err, mode);
		if(err == -EIO){
			dev_info(dev, "raxda 10 inch detected\n");
			jd_panel->choosemode = 1;
			desc = &cz101b4001_desc[1];//choose 1200x1920 mode
			jd_panel->desc = desc;
			jd_panel->dsi->hs_rate = 980000000;//after this, dsi and phy will config again
		}else{
			dev_info(dev, "4lane is radxa 8inch\n");
			jd_panel->dsi->hs_rate = 490000000;
		}
	}
	jd_panel->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	//acceleroter SC7A20
	dev_info(dev, "probe sc7a20 begin\n");
	settings = st_accel_get_settings("sc7a20");
	if (!settings) {
		dev_err(&client->dev, "device name %s not recognized.\n",
			client->name);
		return -ENODEV;
	}

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*adata));
	if (!indio_dev)
		return -ENOMEM;

	adata = iio_priv(indio_dev);
	adata->sensor_settings = (struct st_sensor_settings *)settings;

	ret = st_sensors_i2c_configure(indio_dev, client);
	if (ret < 0)
		return ret;

	ret = st_sensors_power_enable(indio_dev);
	if (ret)
		return ret;
	st_accel_common_probe(indio_dev);
	dev_info(dev, "probe sc7a20 end\n");

	return 0;
error:
	of_node_put(endpoint);
	return -ENODEV;
no_panel:
	mipi_dsi_device_unregister(jd_panel->dsi);
	//drm_panel_remove(&jd_panel->panel);
	//mipi_dsi_detach(jd_panel->dsi);

	return -ENODEV;


}

static int panel_remove(struct i2c_client *client)
{
	struct jadard *jd_panel = i2c_get_clientdata(client);

	mipi_dsi_detach(jd_panel->dsi);
	drm_panel_remove(&jd_panel->panel);
	mipi_dsi_device_unregister(jd_panel->dsi);
	return 0;
}

static const struct i2c_device_id panel_id[] = {
	{ "starfive_jadard", 0 },
	{ }
};

static const struct of_device_id panel_dt_ids[] = {
	{ .compatible = "starfive_jadard", .data = &cz101b4001_desc},
	{ /* sentinel */ }
};

static struct i2c_driver panel_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "starfive_jadard",
		.of_match_table = panel_dt_ids,
	},
	.probe		= panel_probe,
	.remove		= panel_remove,
	.id_table	= panel_id,
};

static int jadard_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jadard *jadard = mipi_dsi_get_drvdata(dsi);

	int ret;

	dsi->mode_flags = MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE ;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->lanes = 4;
	dsi->channel = 1;
	dsi->hs_rate = 490000000;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int jadard_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct jadard *jadard = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&jadard->panel);

	return 0;
}

static const struct of_device_id jadard_of_match[] = {
	{ .compatible = "starfive-dri-panel-1", .data = &cz101b4001_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jadard_of_match);

static struct mipi_dsi_driver jadard_mipi_driver = {
	.probe = jadard_dsi_probe,
	.remove = jadard_dsi_remove,
	.driver.name = DSI_DRIVER_NAME,
};
//module_mipi_dsi_driver(jadard_driver);


static int __init init_panel(void)
{
	int err;

	mipi_dsi_driver_register(&jadard_mipi_driver);
	err = i2c_add_driver(&panel_driver);

	return err;

}
module_init(init_panel);

static void __exit exit_panel(void)
{
	i2c_del_driver(&panel_driver);
	mipi_dsi_driver_unregister(&jadard_mipi_driver);
}
module_exit(exit_panel);


MODULE_AUTHOR("Jagan Teki <jagan@edgeble.ai>");
MODULE_AUTHOR("Stephen Chen <stephen@radxa.com>");
MODULE_DESCRIPTION("Jadard JD9365DA-H3 WUXGA DSI panel");
MODULE_LICENSE("GPL");

