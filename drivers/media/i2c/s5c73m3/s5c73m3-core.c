/*
 * Samsung LSI S5C73M3 8M pixel camera driver
 *
 * Copyright (C) 2012, Samsung Electronics, Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/videodev2.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>
#include <media/s5c73m3.h>
#include <media/v4l2-of.h>

#include "s5c73m3.h"

int s5c73m3_dbg;
module_param_named(debug, s5c73m3_dbg, int, 0644);

static int boot_from_rom = 1;
module_param(boot_from_rom, int, 0644);

static int update_fw;
module_param(update_fw, int, 0644);

#define S5C73M3_EMBEDDED_DATA_MAXLEN	SZ_4K
#define S5C73M3_MIPI_DATA_LANES		4
#define S5C73M3_CLK_NAME		"cis_extclk"

static const char * const s5c73m3_supply_names[S5C73M3_MAX_SUPPLIES] = {
	"vdd-int",	/* Digital Core supply (1.2V), CAM_ISP_CORE_1.2V */
	"vdda",		/* Analog Core supply (1.2V), CAM_SENSOR_CORE_1.2V */
	"vdd-reg",	/* Regulator input supply (2.8V), CAM_SENSOR_A2.8V */
	"vddio-host",	/* Digital Host I/O power supply (1.8V...2.8V),
			   CAM_ISP_SENSOR_1.8V */
	"vddio-cis",	/* Digital CIS I/O power (1.2V...1.8V),
			   CAM_ISP_MIPI_1.2V */
	"vdd-af",	/* Lens, CAM_AF_2.8V */
};

static const struct s5c73m3_frame_size s5c73m3_isp_resolutions[] = {
	{ 320,	240,	COMM_CHG_MODE_YUV_320_240 },
	{ 352,	288,	COMM_CHG_MODE_YUV_352_288 },
	{ 640,	480,	COMM_CHG_MODE_YUV_640_480 },
	{ 880,	720,	COMM_CHG_MODE_YUV_880_720 },
	{ 960,	720,	COMM_CHG_MODE_YUV_960_720 },
	{ 1008,	672,	COMM_CHG_MODE_YUV_1008_672 },
	{ 1184,	666,	COMM_CHG_MODE_YUV_1184_666 },
	{ 1280,	720,	COMM_CHG_MODE_YUV_1280_720 },
	{ 1536,	864,	COMM_CHG_MODE_YUV_1536_864 },
	{ 1600,	1200,	COMM_CHG_MODE_YUV_1600_1200 },
	{ 1632,	1224,	COMM_CHG_MODE_YUV_1632_1224 },
	{ 1920,	1080,	COMM_CHG_MODE_YUV_1920_1080 },
	{ 1920,	1440,	COMM_CHG_MODE_YUV_1920_1440 },
	{ 2304,	1296,	COMM_CHG_MODE_YUV_2304_1296 },
	{ 3264,	2448,	COMM_CHG_MODE_YUV_3264_2448 },
};

static const struct s5c73m3_frame_size s5c73m3_jpeg_resolutions[] = {
	{ 640,	480,	COMM_CHG_MODE_JPEG_640_480 },
	{ 800,	450,	COMM_CHG_MODE_JPEG_800_450 },
	{ 800,	600,	COMM_CHG_MODE_JPEG_800_600 },
	{ 1024,	768,	COMM_CHG_MODE_JPEG_1024_768 },
	{ 1280,	720,	COMM_CHG_MODE_JPEG_1280_720 },
	{ 1280,	960,	COMM_CHG_MODE_JPEG_1280_960 },
	{ 1600,	900,	COMM_CHG_MODE_JPEG_1600_900 },
	{ 1600,	1200,	COMM_CHG_MODE_JPEG_1600_1200 },
	{ 2048,	1152,	COMM_CHG_MODE_JPEG_2048_1152 },
	{ 2048,	1536,	COMM_CHG_MODE_JPEG_2048_1536 },
	{ 2560,	1440,	COMM_CHG_MODE_JPEG_2560_1440 },
	{ 2560,	1920,	COMM_CHG_MODE_JPEG_2560_1920 },
	{ 3264,	1836,	COMM_CHG_MODE_JPEG_3264_1836 },
	{ 3264,	2176,	COMM_CHG_MODE_JPEG_3264_2176 },
	{ 3264,	2448,	COMM_CHG_MODE_JPEG_3264_2448 },
};

static const struct s5c73m3_frame_size * const s5c73m3_resolutions[] = {
	[RES_ISP] = s5c73m3_isp_resolutions,
	[RES_JPEG] = s5c73m3_jpeg_resolutions
};

static const int s5c73m3_resolutions_len[] = {
	[RES_ISP] = ARRAY_SIZE(s5c73m3_isp_resolutions),
	[RES_JPEG] = ARRAY_SIZE(s5c73m3_jpeg_resolutions)
};

static const struct s5c73m3_interval s5c73m3_intervals[] = {
	{ COMM_FRAME_RATE_FIXED_7FPS, {142857, 1000000}, {3264, 2448} },
	{ COMM_FRAME_RATE_FIXED_15FPS, {66667, 1000000}, {3264, 2448} },
	{ COMM_FRAME_RATE_FIXED_20FPS, {50000, 1000000}, {2304, 1296} },
	{ COMM_FRAME_RATE_FIXED_30FPS, {33333, 1000000}, {2304, 1296} },
};

#define S5C73M3_DEFAULT_FRAME_INTERVAL 3 /* 30 fps */

static void s5c73m3_fill_mbus_fmt(struct v4l2_mbus_framefmt *mf,
				  const struct s5c73m3_frame_size *fs,
				  u32 code)
{
	mf->width = fs->width;
	mf->height = fs->height;
	mf->code = code;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field = V4L2_FIELD_NONE;
}

static int s5c73m3_i2c_write(struct i2c_client *client, u16 addr, u16 data)
{
	u8 buf[4] = { addr >> 8, addr & 0xff, data >> 8, data & 0xff };

	int ret = i2c_master_send(client, buf, sizeof(buf));

	v4l_dbg(4, s5c73m3_dbg, client, "%s: addr 0x%04x, data 0x%04x\n",
		 __func__, addr, data);

	if (ret == 4)
		return 0;

	return ret < 0 ? ret : -EREMOTEIO;
}

static int s5c73m3_i2c_read(struct i2c_client *client, u16 addr, u16 *data)
{
	int ret;
	u8 rbuf[2], wbuf[2] = { addr >> 8, addr & 0xff };
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(wbuf),
			.buf = wbuf
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = sizeof(rbuf),
			.buf = rbuf
		}
	};
	/*
	 * Issue repeated START after writing 2 address bytes and
	 * just one STOP only after reading the data bytes.
	 */
	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret == 2) {
		*data = be16_to_cpup((__be16 *)rbuf);
		v4l2_dbg(4, s5c73m3_dbg, client,
			 "%s: addr: 0x%04x, data: 0x%04x\n",
			 __func__, addr, *data);
		return 0;
	}

	v4l2_err(client, "I2C read failed: addr: %04x, (%d)\n", addr, ret);

	return ret >= 0 ? -EREMOTEIO : ret;
}

int s5c73m3_write(struct s5c73m3 *state, u32 addr, u16 data)
{
	struct i2c_client *client = state->i2c_client;
	int ret;

	if ((addr ^ state->i2c_write_address) & 0xffff0000) {
		ret = s5c73m3_i2c_write(client, REG_CMDWR_ADDRH, addr >> 16);
		if (ret < 0) {
			state->i2c_write_address = 0;
			return ret;
		}
	}

	if ((addr ^ state->i2c_write_address) & 0xffff) {
		ret = s5c73m3_i2c_write(client, REG_CMDWR_ADDRL, addr & 0xffff);
		if (ret < 0) {
			state->i2c_write_address = 0;
			return ret;
		}
	}

	state->i2c_write_address = addr;

	ret = s5c73m3_i2c_write(client, REG_CMDBUF_ADDR, data);
	if (ret < 0)
		return ret;

	state->i2c_write_address += 2;

	return ret;
}

int s5c73m3_read(struct s5c73m3 *state, u32 addr, u16 *data)
{
	struct i2c_client *client = state->i2c_client;
	int ret;

	if ((addr ^ state->i2c_read_address) & 0xffff0000) {
		ret = s5c73m3_i2c_write(client, REG_CMDRD_ADDRH, addr >> 16);
		if (ret < 0) {
			state->i2c_read_address = 0;
			return ret;
		}
	}

	if ((addr ^ state->i2c_read_address) & 0xffff) {
		ret = s5c73m3_i2c_write(client, REG_CMDRD_ADDRL, addr & 0xffff);
		if (ret < 0) {
			state->i2c_read_address = 0;
			return ret;
		}
	}

	state->i2c_read_address = addr;

	ret = s5c73m3_i2c_read(client, REG_CMDBUF_ADDR, data);
	if (ret < 0)
		return ret;

	state->i2c_read_address += 2;

	return ret;
}

static int s5c73m3_check_status(struct s5c73m3 *state, unsigned int value)
{
	unsigned long start = jiffies;
	unsigned long end = start + msecs_to_jiffies(2000);
	int ret = 0;
	u16 status;
	int count = 0;

	while (time_is_after_jiffies(end)) {
		ret = s5c73m3_read(state, REG_STATUS, &status);
		if (ret < 0 || status == value)
			break;
		usleep_range(500, 1000);
		++count;
	}

	if (count > 0)
		v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd,
			 "status check took %dms\n",
			 jiffies_to_msecs(jiffies - start));

	if (ret == 0 && status != value) {
		u16 i2c_status = 0;
		u16 i2c_seq_status = 0;

		s5c73m3_read(state, REG_I2C_STATUS, &i2c_status);
		s5c73m3_read(state, REG_I2C_SEQ_STATUS, &i2c_seq_status);

		v4l2_err(&state->sensor_sd,
			 "wrong status %#x, expected: %#x, i2c_status: %#x/%#x\n",
			 status, value, i2c_status, i2c_seq_status);

		return -ETIMEDOUT;
	}

	return ret;
}

int s5c73m3_isp_command(struct s5c73m3 *state, u16 command, u16 data)
{
	int ret;

	ret = s5c73m3_check_status(state, REG_STATUS_ISP_COMMAND_COMPLETED);
	if (ret < 0)
		return ret;

	ret = s5c73m3_write(state, 0x00095000, command);
	if (ret < 0)
		return ret;

	ret = s5c73m3_write(state, 0x00095002, data);
	if (ret < 0)
		return ret;

	return s5c73m3_write(state, REG_STATUS, 0x0001);
}

static int s5c73m3_isp_comm_result(struct s5c73m3 *state, u16 command,
				   u16 *data)
{
	return s5c73m3_read(state, COMM_RESULT_OFFSET + command, data);
}

static int s5c73m3_set_af_softlanding(struct s5c73m3 *state)
{
	unsigned long start = jiffies;
	u16 af_softlanding;
	int count = 0;
	int ret;
	const char *msg;

	ret = s5c73m3_isp_command(state, COMM_AF_SOFTLANDING,
					COMM_AF_SOFTLANDING_ON);
	if (ret < 0) {
		v4l2_info(&state->sensor_sd, "AF soft-landing failed\n");
		return ret;
	}

	for (;;) {
		ret = s5c73m3_isp_comm_result(state, COMM_AF_SOFTLANDING,
							&af_softlanding);
		if (ret < 0) {
			msg = "failed";
			break;
		}
		if (af_softlanding == COMM_AF_SOFTLANDING_RES_COMPLETE) {
			msg = "succeeded";
			break;
		}
		if (++count > 100) {
			ret = -ETIME;
			msg = "timed out";
			break;
		}
		msleep(25);
	}

	v4l2_info(&state->sensor_sd, "AF soft-landing %s after %dms\n",
		  msg, jiffies_to_msecs(jiffies - start));

	return ret;
}

static int s5c73m3_load_fw(struct v4l2_subdev *sd)
{
	struct s5c73m3 *state = sensor_sd_to_s5c73m3(sd);
	struct i2c_client *client = state->i2c_client;
	const struct firmware *fw;
	int ret;
	char fw_name[20];

	snprintf(fw_name, sizeof(fw_name), "SlimISP_%.2s.bin",
							state->fw_file_version);
	ret = request_firmware(&fw, fw_name, &client->dev);
	if (ret < 0) {
		v4l2_err(sd, "Firmware request failed (%s)\n", fw_name);
		return -EINVAL;
	}

	v4l2_info(sd, "Loading firmware (%s, %zu B)\n", fw_name, fw->size);

	ret = s5c73m3_spi_write(state, fw->data, fw->size, 64);

	if (ret >= 0)
		state->isp_ready = 1;
	else
		v4l2_err(sd, "SPI write failed\n");

	release_firmware(fw);

	return ret;
}

static int s5c73m3_set_frame_size(struct s5c73m3 *state)
{
	const struct s5c73m3_frame_size *prev_size =
					state->sensor_pix_size[RES_ISP];
	const struct s5c73m3_frame_size *cap_size =
					state->sensor_pix_size[RES_JPEG];
	unsigned int chg_mode;

	v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd,
		 "Preview size: %dx%d, reg_val: 0x%x\n",
		 prev_size->width, prev_size->height, prev_size->reg_val);

	chg_mode = prev_size->reg_val | COMM_CHG_MODE_NEW;

	if (state->mbus_code == S5C73M3_JPEG_FMT) {
		v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd,
			 "Capture size: %dx%d, reg_val: 0x%x\n",
			 cap_size->width, cap_size->height, cap_size->reg_val);
		chg_mode |= cap_size->reg_val;
	}

	return s5c73m3_isp_command(state, COMM_CHG_MODE, chg_mode);
}

static int s5c73m3_set_frame_rate(struct s5c73m3 *state)
{
	int ret;

	if (state->ctrls.stabilization->val)
		return 0;

	if (WARN_ON(state->fiv == NULL))
		return -EINVAL;

	ret = s5c73m3_isp_command(state, COMM_FRAME_RATE, state->fiv->fps_reg);
	if (!ret)
		state->apply_fiv = 0;

	return ret;
}

static int __s5c73m3_s_stream(struct s5c73m3 *state, struct v4l2_subdev *sd,
								int on)
{
	u16 mode;
	int ret;

	if (on && state->apply_fmt) {
		if (state->mbus_code == S5C73M3_JPEG_FMT)
			mode = COMM_IMG_OUTPUT_INTERLEAVED;
		else
			mode = COMM_IMG_OUTPUT_YUV;

		ret = s5c73m3_isp_command(state, COMM_IMG_OUTPUT, mode);
		if (!ret)
			ret = s5c73m3_set_frame_size(state);
		if (ret)
			return ret;
		state->apply_fmt = 0;
	}

	ret = s5c73m3_isp_command(state, COMM_SENSOR_STREAMING, !!on);
	if (ret)
		return ret;

	state->streaming = !!on;

	if (!on)
		return ret;

	if (state->apply_fiv) {
		ret = s5c73m3_set_frame_rate(state);
		if (ret < 0)
			v4l2_err(sd, "Error setting frame rate(%d)\n", ret);
	}

	return s5c73m3_check_status(state, REG_STATUS_ISP_COMMAND_COMPLETED);
}

static int s5c73m3_oif_s_stream(struct v4l2_subdev *sd, int on)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	int ret;

	mutex_lock(&state->lock);
	ret = __s5c73m3_s_stream(state, sd, on);
	mutex_unlock(&state->lock);

	return ret;
}

static int s5c73m3_system_status_wait(struct s5c73m3 *state, u32 value,
				      unsigned int delay, unsigned int steps)
{
	u16 reg = 0;

	while (steps-- > 0) {
		int ret = s5c73m3_read(state, 0x30100010, &reg);
		if (ret < 0)
			return ret;
		if (reg == value)
			return 0;
		usleep_range(delay, delay + 25);
	}
	return -ETIMEDOUT;
}

static int s5c73m3_read_fw_version(struct s5c73m3 *state)
{
	struct v4l2_subdev *sd = &state->sensor_sd;
	int i, ret;
	u16 data[2];
	int offset;

	offset = state->isp_ready ? 0x60 : 0;

	for (i = 0; i < S5C73M3_SENSOR_FW_LEN / 2; i++) {
		ret = s5c73m3_read(state, offset + i * 2, data);
		if (ret < 0)
			return ret;
		state->sensor_fw[i * 2] = (char)(*data & 0xff);
		state->sensor_fw[i * 2 + 1] = (char)(*data >> 8);
	}
	state->sensor_fw[S5C73M3_SENSOR_FW_LEN] = '\0';


	for (i = 0; i < S5C73M3_SENSOR_TYPE_LEN / 2; i++) {
		ret = s5c73m3_read(state, offset + 6 + i * 2, data);
		if (ret < 0)
			return ret;
		state->sensor_type[i * 2] = (char)(*data & 0xff);
		state->sensor_type[i * 2 + 1] = (char)(*data >> 8);
	}
	state->sensor_type[S5C73M3_SENSOR_TYPE_LEN] = '\0';

	ret = s5c73m3_read(state, offset + 0x14, data);
	if (ret >= 0) {
		ret = s5c73m3_read(state, offset + 0x16, data + 1);
		if (ret >= 0)
			state->fw_size = data[0] + (data[1] << 16);
	}

	v4l2_info(sd, "Sensor type: %s, FW version: %s\n",
		  state->sensor_type, state->sensor_fw);
	return ret;
}

static int s5c73m3_fw_update_from(struct s5c73m3 *state)
{
	struct v4l2_subdev *sd = &state->sensor_sd;
	u16 status = COMM_FW_UPDATE_NOT_READY;
	int ret;
	int count = 0;

	v4l2_warn(sd, "Updating F-ROM firmware.\n");
	do {
		if (status == COMM_FW_UPDATE_NOT_READY) {
			ret = s5c73m3_isp_command(state, COMM_FW_UPDATE, 0);
			if (ret < 0)
				return ret;
		}

		ret = s5c73m3_read(state, 0x00095906, &status);
		if (ret < 0)
			return ret;
		switch (status) {
		case COMM_FW_UPDATE_FAIL:
			v4l2_warn(sd, "Updating F-ROM firmware failed.\n");
			return -EIO;
		case COMM_FW_UPDATE_SUCCESS:
			v4l2_warn(sd, "Updating F-ROM firmware finished.\n");
			return 0;
		}
		++count;
		msleep(20);
	} while (count < 500);

	v4l2_warn(sd, "Updating F-ROM firmware timed-out.\n");
	return -ETIMEDOUT;
}

static int s5c73m3_spi_boot(struct s5c73m3 *state, bool load_fw)
{
	struct v4l2_subdev *sd = &state->sensor_sd;
	int ret;

	/* Run ARM MCU */
	ret = s5c73m3_write(state, 0x30000004, 0xffff);
	if (ret < 0)
		return ret;

	usleep_range(400, 500);

	/* Check booting status */
	ret = s5c73m3_system_status_wait(state, 0x0c, 100, 3);
	if (ret < 0) {
		v4l2_err(sd, "booting failed: %d\n", ret);
		return ret;
	}

	/* P,M,S and Boot Mode */
	ret = s5c73m3_write(state, 0x30100014, 0x2146);
	if (ret < 0)
		return ret;

	ret = s5c73m3_write(state, 0x30100010, 0x210c);
	if (ret < 0)
		return ret;

	usleep_range(200, 250);

	/* Check SPI status */
	ret = s5c73m3_system_status_wait(state, 0x210d, 100, 300);
	if (ret < 0)
		v4l2_err(sd, "SPI not ready: %d\n", ret);

	/* Firmware download over SPI */
	if (load_fw)
		s5c73m3_load_fw(sd);

	/* MCU reset */
	ret = s5c73m3_write(state, 0x30000004, 0xfffd);
	if (ret < 0)
		return ret;

	/* Remap */
	ret = s5c73m3_write(state, 0x301000a4, 0x0183);
	if (ret < 0)
		return ret;

	/* MCU restart */
	ret = s5c73m3_write(state, 0x30000004, 0xffff);
	if (ret < 0 || !load_fw)
		return ret;

	ret = s5c73m3_read_fw_version(state);
	if (ret < 0)
		return ret;

	if (load_fw && update_fw) {
		ret = s5c73m3_fw_update_from(state);
		update_fw = 0;
	}

	return ret;
}

static int s5c73m3_set_timing_register_for_vdd(struct s5c73m3 *state)
{
	static const u32 regs[][2] = {
		{ 0x30100018, 0x0618 },
		{ 0x3010001c, 0x10c1 },
		{ 0x30100020, 0x249e }
	};
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		ret = s5c73m3_write(state, regs[i][0], regs[i][1]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void s5c73m3_set_fw_file_version(struct s5c73m3 *state)
{
	switch (state->sensor_fw[0]) {
	case 'G':
	case 'O':
		state->fw_file_version[0] = 'G';
		break;
	case 'S':
	case 'Z':
		state->fw_file_version[0] = 'Z';
		break;
	}

	switch (state->sensor_fw[1]) {
	case 'C'...'F':
		state->fw_file_version[1] = state->sensor_fw[1];
		break;
	}
}

static int s5c73m3_get_fw_version(struct s5c73m3 *state)
{
	struct v4l2_subdev *sd = &state->sensor_sd;
	int ret;

	/* Run ARM MCU */
	ret = s5c73m3_write(state, 0x30000004, 0xffff);
	if (ret < 0)
		return ret;
	usleep_range(400, 500);

	/* Check booting status */
	ret = s5c73m3_system_status_wait(state, 0x0c, 100, 3);
	if (ret < 0) {

		v4l2_err(sd, "%s: booting failed: %d\n", __func__, ret);
		return ret;
	}

	/* Change I/O Driver Current in order to read from F-ROM */
	ret = s5c73m3_write(state, 0x30100120, 0x0820);
	ret = s5c73m3_write(state, 0x30100124, 0x0820);

	/* Offset Setting */
	ret = s5c73m3_write(state, 0x00010418, 0x0008);

	/* P,M,S and Boot Mode */
	ret = s5c73m3_write(state, 0x30100014, 0x2146);
	if (ret < 0)
		return ret;
	ret = s5c73m3_write(state, 0x30100010, 0x230c);
	if (ret < 0)
		return ret;

	usleep_range(200, 250);

	/* Check SPI status */
	ret = s5c73m3_system_status_wait(state, 0x230e, 100, 300);
	if (ret < 0)
		v4l2_err(sd, "SPI not ready: %d\n", ret);

	/* ARM reset */
	ret = s5c73m3_write(state, 0x30000004, 0xfffd);
	if (ret < 0)
		return ret;

	/* Remap */
	ret = s5c73m3_write(state, 0x301000a4, 0x0183);
	if (ret < 0)
		return ret;

	s5c73m3_set_timing_register_for_vdd(state);

	ret = s5c73m3_read_fw_version(state);

	s5c73m3_set_fw_file_version(state);

	return ret;
}

static int s5c73m3_rom_boot(struct s5c73m3 *state, bool load_fw)
{
	static const u32 boot_regs[][2] = {
		{ 0x3100010c, 0x0044 },
		{ 0x31000108, 0x000d },
		{ 0x31000304, 0x0001 },
		{ 0x00010000, 0x5800 },
		{ 0x00010002, 0x0002 },
		{ 0x31000000, 0x0001 },
		{ 0x30100014, 0x1b85 },
		{ 0x30100010, 0x230c }
	};
	struct v4l2_subdev *sd = &state->sensor_sd;
	int i, ret;

	/* Run ARM MCU */
	ret = s5c73m3_write(state, 0x30000004, 0xffff);
	if (ret < 0)
		return ret;
	usleep_range(400, 450);

	/* Check booting status */
	ret = s5c73m3_system_status_wait(state, 0x0c, 100, 4);
	if (ret < 0) {
		v4l2_err(sd, "Booting failed: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(boot_regs); i++) {
		ret = s5c73m3_write(state, boot_regs[i][0], boot_regs[i][1]);
		if (ret < 0)
			return ret;
	}
	msleep(200);

	/* Check the binary read status */
	ret = s5c73m3_system_status_wait(state, 0x230e, 1000, 150);
	if (ret < 0) {
		v4l2_err(sd, "Binary read failed: %d\n", ret);
		return ret;
	}

	/* ARM reset */
	ret = s5c73m3_write(state, 0x30000004, 0xfffd);
	if (ret < 0)
		return ret;
	/* Remap */
	ret = s5c73m3_write(state, 0x301000a4, 0x0183);
	if (ret < 0)
		return ret;
	/* MCU re-start */
	ret = s5c73m3_write(state, 0x30000004, 0xffff);
	if (ret < 0)
		return ret;

	state->isp_ready = 1;

	return s5c73m3_read_fw_version(state);
}

static int s5c73m3_isp_init(struct s5c73m3 *state)
{
	int ret;

	state->i2c_read_address = 0;
	state->i2c_write_address = 0;

	ret = s5c73m3_i2c_write(state->i2c_client, AHB_MSB_ADDR_PTR, 0x3310);
	if (ret < 0)
		return ret;

	if (boot_from_rom)
		return s5c73m3_rom_boot(state, true);
	else
		return s5c73m3_spi_boot(state, true);
}

static const struct s5c73m3_frame_size *s5c73m3_find_frame_size(
					struct v4l2_mbus_framefmt *fmt,
					enum s5c73m3_resolution_types idx)
{
	const struct s5c73m3_frame_size *fs;
	const struct s5c73m3_frame_size *best_fs;
	int best_dist = INT_MAX;
	int i;

	fs = s5c73m3_resolutions[idx];
	best_fs = NULL;
	for (i = 0; i < s5c73m3_resolutions_len[idx]; ++i) {
		int dist = abs(fs->width - fmt->width) +
						abs(fs->height - fmt->height);
		if (dist < best_dist) {
			best_dist = dist;
			best_fs = fs;
		}
		++fs;
	}

	return best_fs;
}

static void s5c73m3_oif_try_format(struct s5c73m3 *state,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_format *fmt,
				   const struct s5c73m3_frame_size **fs)
{
	struct v4l2_subdev *sd = &state->sensor_sd;
	u32 code;

	switch (fmt->pad) {
	case OIF_ISP_PAD:
		*fs = s5c73m3_find_frame_size(&fmt->format, RES_ISP);
		code = S5C73M3_ISP_FMT;
		break;
	case OIF_JPEG_PAD:
		*fs = s5c73m3_find_frame_size(&fmt->format, RES_JPEG);
		code = S5C73M3_JPEG_FMT;
		break;
	case OIF_SOURCE_PAD:
	default:
		if (fmt->format.code == S5C73M3_JPEG_FMT)
			code = S5C73M3_JPEG_FMT;
		else
			code = S5C73M3_ISP_FMT;

		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			*fs = state->oif_pix_size[RES_ISP];
		else
			*fs = s5c73m3_find_frame_size(
						v4l2_subdev_get_try_format(sd, cfg,
							OIF_ISP_PAD),
						RES_ISP);
		break;
	}

	s5c73m3_fill_mbus_fmt(&fmt->format, *fs, code);
}

static void s5c73m3_try_format(struct s5c73m3 *state,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_format *fmt,
			      const struct s5c73m3_frame_size **fs)
{
	u32 code;

	if (fmt->pad == S5C73M3_ISP_PAD) {
		*fs = s5c73m3_find_frame_size(&fmt->format, RES_ISP);
		code = S5C73M3_ISP_FMT;
	} else {
		*fs = s5c73m3_find_frame_size(&fmt->format, RES_JPEG);
		code = S5C73M3_JPEG_FMT;
	}

	s5c73m3_fill_mbus_fmt(&fmt->format, *fs, code);
}

static int s5c73m3_oif_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);

	if (fi->pad != OIF_SOURCE_PAD)
		return -EINVAL;

	mutex_lock(&state->lock);
	fi->interval = state->fiv->interval;
	mutex_unlock(&state->lock);

	return 0;
}

static int __s5c73m3_set_frame_interval(struct s5c73m3 *state,
					struct v4l2_subdev_frame_interval *fi)
{
	const struct s5c73m3_frame_size *prev_size =
						state->sensor_pix_size[RES_ISP];
	const struct s5c73m3_interval *fiv = &s5c73m3_intervals[0];
	unsigned int ret, min_err = UINT_MAX;
	unsigned int i, fr_time;

	if (fi->interval.denominator == 0)
		return -EINVAL;

	fr_time = fi->interval.numerator * 1000 / fi->interval.denominator;

	for (i = 0; i < ARRAY_SIZE(s5c73m3_intervals); i++) {
		const struct s5c73m3_interval *iv = &s5c73m3_intervals[i];

		if (prev_size->width > iv->size.width ||
		    prev_size->height > iv->size.height)
			continue;

		ret = abs(iv->interval.numerator / 1000 - fr_time);
		if (ret < min_err) {
			fiv = iv;
			min_err = ret;
		}
	}
	state->fiv = fiv;

	v4l2_dbg(1, s5c73m3_dbg, &state->sensor_sd,
		 "Changed frame interval to %u us\n", fiv->interval.numerator);
	return 0;
}

static int s5c73m3_oif_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	int ret;

	if (fi->pad != OIF_SOURCE_PAD)
		return -EINVAL;

	v4l2_dbg(1, s5c73m3_dbg, sd, "Setting %d/%d frame interval\n",
		 fi->interval.numerator, fi->interval.denominator);

	mutex_lock(&state->lock);

	ret = __s5c73m3_set_frame_interval(state, fi);
	if (!ret) {
		if (state->streaming)
			ret = s5c73m3_set_frame_rate(state);
		else
			state->apply_fiv = 1;
	}
	mutex_unlock(&state->lock);
	return ret;
}

static int s5c73m3_oif_enum_frame_interval(struct v4l2_subdev *sd,
			      struct v4l2_subdev_pad_config *cfg,
			      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	const struct s5c73m3_interval *fi;
	int ret = 0;

	if (fie->pad != OIF_SOURCE_PAD)
		return -EINVAL;
	if (fie->index >= ARRAY_SIZE(s5c73m3_intervals))
		return -EINVAL;

	mutex_lock(&state->lock);
	fi = &s5c73m3_intervals[fie->index];
	if (fie->width > fi->size.width || fie->height > fi->size.height)
		ret = -EINVAL;
	else
		fie->interval = fi->interval;
	mutex_unlock(&state->lock);

	return ret;
}

static int s5c73m3_oif_get_pad_code(int pad, int index)
{
	if (pad == OIF_SOURCE_PAD) {
		if (index > 1)
			return -EINVAL;
		return (index == 0) ? S5C73M3_ISP_FMT : S5C73M3_JPEG_FMT;
	}

	if (index > 0)
		return -EINVAL;

	return (pad == OIF_ISP_PAD) ? S5C73M3_ISP_FMT : S5C73M3_JPEG_FMT;
}

static int s5c73m3_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct s5c73m3 *state = sensor_sd_to_s5c73m3(sd);
	const struct s5c73m3_frame_size *fs;
	u32 code;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		return 0;
	}

	mutex_lock(&state->lock);

	switch (fmt->pad) {
	case S5C73M3_ISP_PAD:
		code = S5C73M3_ISP_FMT;
		fs = state->sensor_pix_size[RES_ISP];
		break;
	case S5C73M3_JPEG_PAD:
		code = S5C73M3_JPEG_FMT;
		fs = state->sensor_pix_size[RES_JPEG];
		break;
	default:
		mutex_unlock(&state->lock);
		return -EINVAL;
	}
	s5c73m3_fill_mbus_fmt(&fmt->format, fs, code);

	mutex_unlock(&state->lock);
	return 0;
}

static int s5c73m3_oif_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	const struct s5c73m3_frame_size *fs;
	u32 code;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		return 0;
	}

	mutex_lock(&state->lock);

	switch (fmt->pad) {
	case OIF_ISP_PAD:
		code = S5C73M3_ISP_FMT;
		fs = state->oif_pix_size[RES_ISP];
		break;
	case OIF_JPEG_PAD:
		code = S5C73M3_JPEG_FMT;
		fs = state->oif_pix_size[RES_JPEG];
		break;
	case OIF_SOURCE_PAD:
		code = state->mbus_code;
		fs = state->oif_pix_size[RES_ISP];
		break;
	default:
		mutex_unlock(&state->lock);
		return -EINVAL;
	}
	s5c73m3_fill_mbus_fmt(&fmt->format, fs, code);

	mutex_unlock(&state->lock);
	return 0;
}

static int s5c73m3_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	const struct s5c73m3_frame_size *frame_size = NULL;
	struct s5c73m3 *state = sensor_sd_to_s5c73m3(sd);
	struct v4l2_mbus_framefmt *mf;
	int ret = 0;

	mutex_lock(&state->lock);

	s5c73m3_try_format(state, cfg, fmt, &frame_size);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
	} else {
		switch (fmt->pad) {
		case S5C73M3_ISP_PAD:
			state->sensor_pix_size[RES_ISP] = frame_size;
			break;
		case S5C73M3_JPEG_PAD:
			state->sensor_pix_size[RES_JPEG] = frame_size;
			break;
		default:
			ret = -EBUSY;
		}

		if (state->streaming)
			ret = -EBUSY;
		else
			state->apply_fmt = 1;
	}

	mutex_unlock(&state->lock);

	return ret;
}

static int s5c73m3_oif_set_fmt(struct v4l2_subdev *sd,
			 struct v4l2_subdev_pad_config *cfg,
			 struct v4l2_subdev_format *fmt)
{
	const struct s5c73m3_frame_size *frame_size = NULL;
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	struct v4l2_mbus_framefmt *mf;
	int ret = 0;

	mutex_lock(&state->lock);

	s5c73m3_oif_try_format(state, cfg, fmt, &frame_size);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
		if (fmt->pad == OIF_ISP_PAD) {
			mf = v4l2_subdev_get_try_format(sd, cfg, OIF_SOURCE_PAD);
			mf->width = fmt->format.width;
			mf->height = fmt->format.height;
		}
	} else {
		switch (fmt->pad) {
		case OIF_ISP_PAD:
			state->oif_pix_size[RES_ISP] = frame_size;
			break;
		case OIF_JPEG_PAD:
			state->oif_pix_size[RES_JPEG] = frame_size;
			break;
		case OIF_SOURCE_PAD:
			state->mbus_code = fmt->format.code;
			break;
		default:
			ret = -EBUSY;
		}

		if (state->streaming)
			ret = -EBUSY;
		else
			state->apply_fmt = 1;
	}

	mutex_unlock(&state->lock);

	return ret;
}

static int s5c73m3_oif_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				  struct v4l2_mbus_frame_desc *fd)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	int i;

	if (pad != OIF_SOURCE_PAD || fd == NULL)
		return -EINVAL;

	mutex_lock(&state->lock);
	fd->num_entries = 2;
	for (i = 0; i < fd->num_entries; i++)
		fd->entry[i] = state->frame_desc.entry[i];
	mutex_unlock(&state->lock);

	return 0;
}

static int s5c73m3_oif_set_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				      struct v4l2_mbus_frame_desc *fd)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	struct v4l2_mbus_frame_desc *frame_desc = &state->frame_desc;
	int i;

	if (pad != OIF_SOURCE_PAD || fd == NULL)
		return -EINVAL;

	fd->entry[0].length = 10 * SZ_1M;
	fd->entry[1].length = max_t(u32, fd->entry[1].length,
				    S5C73M3_EMBEDDED_DATA_MAXLEN);
	fd->num_entries = 2;

	mutex_lock(&state->lock);
	for (i = 0; i < fd->num_entries; i++)
		frame_desc->entry[i] = fd->entry[i];
	mutex_unlock(&state->lock);

	return 0;
}

static int s5c73m3_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	static const int codes[] = {
			[S5C73M3_ISP_PAD] = S5C73M3_ISP_FMT,
			[S5C73M3_JPEG_PAD] = S5C73M3_JPEG_FMT};

	if (code->index > 0 || code->pad >= S5C73M3_NUM_PADS)
		return -EINVAL;

	code->code = codes[code->pad];

	return 0;
}

static int s5c73m3_oif_enum_mbus_code(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_mbus_code_enum *code)
{
	int ret;

	ret = s5c73m3_oif_get_pad_code(code->pad, code->index);
	if (ret < 0)
		return ret;

	code->code = ret;

	return 0;
}

static int s5c73m3_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int idx;

	if (fse->pad == S5C73M3_ISP_PAD) {
		if (fse->code != S5C73M3_ISP_FMT)
			return -EINVAL;
		idx = RES_ISP;
	} else{
		if (fse->code != S5C73M3_JPEG_FMT)
			return -EINVAL;
		idx = RES_JPEG;
	}

	if (fse->index >= s5c73m3_resolutions_len[idx])
		return -EINVAL;

	fse->min_width  = s5c73m3_resolutions[idx][fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = s5c73m3_resolutions[idx][fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int s5c73m3_oif_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	int idx;

	if (fse->pad == OIF_SOURCE_PAD) {
		if (fse->index > 0)
			return -EINVAL;

		switch (fse->code) {
		case S5C73M3_JPEG_FMT:
		case S5C73M3_ISP_FMT: {
			unsigned w, h;

			if (fse->which == V4L2_SUBDEV_FORMAT_TRY) {
				struct v4l2_mbus_framefmt *mf;

				mf = v4l2_subdev_get_try_format(sd, cfg,
								OIF_ISP_PAD);

				w = mf->width;
				h = mf->height;
			} else {
				const struct s5c73m3_frame_size *fs;

				fs = state->oif_pix_size[RES_ISP];
				w = fs->width;
				h = fs->height;
			}
			fse->max_width = fse->min_width = w;
			fse->max_height = fse->min_height = h;
			return 0;
		}
		default:
			return -EINVAL;
		}
	}

	if (fse->code != s5c73m3_oif_get_pad_code(fse->pad, 0))
		return -EINVAL;

	if (fse->pad == OIF_JPEG_PAD)
		idx = RES_JPEG;
	else
		idx = RES_ISP;

	if (fse->index >= s5c73m3_resolutions_len[idx])
		return -EINVAL;

	fse->min_width  = s5c73m3_resolutions[idx][fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = s5c73m3_resolutions[idx][fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}

static int s5c73m3_oif_log_status(struct v4l2_subdev *sd)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);

	v4l2_ctrl_handler_log_status(sd->ctrl_handler, sd->name);

	v4l2_info(sd, "power: %d, apply_fmt: %d\n", state->power,
							state->apply_fmt);

	return 0;
}

static int s5c73m3_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_get_try_format(sd, fh->pad, S5C73M3_ISP_PAD);
	s5c73m3_fill_mbus_fmt(mf, &s5c73m3_isp_resolutions[1],
						S5C73M3_ISP_FMT);

	mf = v4l2_subdev_get_try_format(sd, fh->pad, S5C73M3_JPEG_PAD);
	s5c73m3_fill_mbus_fmt(mf, &s5c73m3_jpeg_resolutions[1],
					S5C73M3_JPEG_FMT);

	return 0;
}

static int s5c73m3_oif_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *mf;

	mf = v4l2_subdev_get_try_format(sd, fh->pad, OIF_ISP_PAD);
	s5c73m3_fill_mbus_fmt(mf, &s5c73m3_isp_resolutions[1],
						S5C73M3_ISP_FMT);

	mf = v4l2_subdev_get_try_format(sd, fh->pad, OIF_JPEG_PAD);
	s5c73m3_fill_mbus_fmt(mf, &s5c73m3_jpeg_resolutions[1],
					S5C73M3_JPEG_FMT);

	mf = v4l2_subdev_get_try_format(sd, fh->pad, OIF_SOURCE_PAD);
	s5c73m3_fill_mbus_fmt(mf, &s5c73m3_isp_resolutions[1],
						S5C73M3_ISP_FMT);
	return 0;
}

static int s5c73m3_gpio_set_value(struct s5c73m3 *priv, int id, u32 val)
{
	if (!gpio_is_valid(priv->gpio[id].gpio))
		return 0;
	gpio_set_value(priv->gpio[id].gpio, !!val);
	return 1;
}

static int s5c73m3_gpio_assert(struct s5c73m3 *priv, int id)
{
	return s5c73m3_gpio_set_value(priv, id, priv->gpio[id].level);
}

static int s5c73m3_gpio_deassert(struct s5c73m3 *priv, int id)
{
	return s5c73m3_gpio_set_value(priv, id, !priv->gpio[id].level);
}

static int __s5c73m3_power_on(struct s5c73m3 *state)
{
	int i, ret;

	for (i = 0; i < S5C73M3_MAX_SUPPLIES; i++) {
		ret = regulator_enable(state->supplies[i].consumer);
		if (ret)
			goto err_reg_dis;
	}

	ret = clk_set_rate(state->clock, state->mclk_frequency);
	if (ret < 0)
		goto err_reg_dis;

	ret = clk_prepare_enable(state->clock);
	if (ret < 0)
		goto err_reg_dis;

	v4l2_dbg(1, s5c73m3_dbg, &state->oif_sd, "clock frequency: %ld\n",
					clk_get_rate(state->clock));

	s5c73m3_gpio_deassert(state, STBY);
	usleep_range(100, 200);

	s5c73m3_gpio_deassert(state, RST);
	usleep_range(50, 100);

	return 0;

err_reg_dis:
	for (--i; i >= 0; i--)
		regulator_disable(state->supplies[i].consumer);
	return ret;
}

static int __s5c73m3_power_off(struct s5c73m3 *state)
{
	int i, ret;

	if (s5c73m3_gpio_assert(state, RST))
		usleep_range(10, 50);

	if (s5c73m3_gpio_assert(state, STBY))
		usleep_range(100, 200);

	clk_disable_unprepare(state->clock);

	state->streaming = 0;
	state->isp_ready = 0;

	for (i = S5C73M3_MAX_SUPPLIES - 1; i >= 0; i--) {
		ret = regulator_disable(state->supplies[i].consumer);
		if (ret)
			goto err;
	}

	return 0;
err:
	for (++i; i < S5C73M3_MAX_SUPPLIES; i++) {
		int r = regulator_enable(state->supplies[i].consumer);
		if (r < 0)
			v4l2_err(&state->oif_sd, "Failed to reenable %s: %d\n",
				 state->supplies[i].supply, r);
	}

	clk_prepare_enable(state->clock);
	return ret;
}

static int s5c73m3_oif_set_power(struct v4l2_subdev *sd, int on)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	int ret = 0;

	mutex_lock(&state->lock);

	if (on && !state->power) {
		ret = __s5c73m3_power_on(state);
		if (!ret)
			ret = s5c73m3_isp_init(state);
		if (!ret) {
			state->apply_fiv = 1;
			state->apply_fmt = 1;
		}
	} else if (state->power == !on) {
		ret = s5c73m3_set_af_softlanding(state);
		if (!ret)
			ret = __s5c73m3_power_off(state);
		else
			v4l2_err(sd, "Soft landing lens failed\n");
	}
	if (!ret)
		state->power += on ? 1 : -1;

	v4l2_dbg(1, s5c73m3_dbg, sd, "%s: power: %d\n",
		 __func__, state->power);

	mutex_unlock(&state->lock);
	return ret;
}

static int s5c73m3_oif_registered(struct v4l2_subdev *sd)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	int ret;

	ret = v4l2_device_register_subdev(sd->v4l2_dev, &state->sensor_sd);
	if (ret) {
		v4l2_err(sd->v4l2_dev, "Failed to register %s\n",
							state->oif_sd.name);
		return ret;
	}

	ret = media_entity_create_link(&state->sensor_sd.entity,
			S5C73M3_ISP_PAD, &state->oif_sd.entity, OIF_ISP_PAD,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);

	ret = media_entity_create_link(&state->sensor_sd.entity,
			S5C73M3_JPEG_PAD, &state->oif_sd.entity, OIF_JPEG_PAD,
			MEDIA_LNK_FL_IMMUTABLE | MEDIA_LNK_FL_ENABLED);

	return ret;
}

static void s5c73m3_oif_unregistered(struct v4l2_subdev *sd)
{
	struct s5c73m3 *state = oif_sd_to_s5c73m3(sd);
	v4l2_device_unregister_subdev(&state->sensor_sd);
}

static const struct v4l2_subdev_internal_ops s5c73m3_internal_ops = {
	.open		= s5c73m3_open,
};

static const struct v4l2_subdev_pad_ops s5c73m3_pad_ops = {
	.enum_mbus_code		= s5c73m3_enum_mbus_code,
	.enum_frame_size	= s5c73m3_enum_frame_size,
	.get_fmt		= s5c73m3_get_fmt,
	.set_fmt		= s5c73m3_set_fmt,
};

static const struct v4l2_subdev_ops s5c73m3_subdev_ops = {
	.pad	= &s5c73m3_pad_ops,
};

static const struct v4l2_subdev_internal_ops oif_internal_ops = {
	.registered	= s5c73m3_oif_registered,
	.unregistered	= s5c73m3_oif_unregistered,
	.open		= s5c73m3_oif_open,
};

static const struct v4l2_subdev_pad_ops s5c73m3_oif_pad_ops = {
	.enum_mbus_code		= s5c73m3_oif_enum_mbus_code,
	.enum_frame_size	= s5c73m3_oif_enum_frame_size,
	.enum_frame_interval	= s5c73m3_oif_enum_frame_interval,
	.get_fmt		= s5c73m3_oif_get_fmt,
	.set_fmt		= s5c73m3_oif_set_fmt,
	.get_frame_desc		= s5c73m3_oif_get_frame_desc,
	.set_frame_desc		= s5c73m3_oif_set_frame_desc,
};

static const struct v4l2_subdev_core_ops s5c73m3_oif_core_ops = {
	.s_power	= s5c73m3_oif_set_power,
	.log_status	= s5c73m3_oif_log_status,
};

static const struct v4l2_subdev_video_ops s5c73m3_oif_video_ops = {
	.s_stream		= s5c73m3_oif_s_stream,
	.g_frame_interval	= s5c73m3_oif_g_frame_interval,
	.s_frame_interval	= s5c73m3_oif_s_frame_interval,
};

static const struct v4l2_subdev_ops oif_subdev_ops = {
	.core	= &s5c73m3_oif_core_ops,
	.pad	= &s5c73m3_oif_pad_ops,
	.video	= &s5c73m3_oif_video_ops,
};

static int s5c73m3_configure_gpios(struct s5c73m3 *state)
{
	static const char * const gpio_names[] = {
		"S5C73M3_STBY", "S5C73M3_RST"
	};
	struct i2c_client *c = state->i2c_client;
	struct s5c73m3_gpio *g = state->gpio;
	int ret, i;

	for (i = 0; i < GPIO_NUM; ++i) {
		unsigned int flags = GPIOF_DIR_OUT;
		if (g[i].level)
			flags |= GPIOF_INIT_HIGH;
		ret = devm_gpio_request_one(&c->dev, g[i].gpio, flags,
					    gpio_names[i]);
		if (ret) {
			v4l2_err(c, "failed to request gpio %s\n",
				 gpio_names[i]);
			return ret;
		}
	}
	return 0;
}

static int s5c73m3_parse_gpios(struct s5c73m3 *state)
{
	static const char * const prop_names[] = {
		"standby-gpios", "xshutdown-gpios",
	};
	struct device *dev = &state->i2c_client->dev;
	struct device_node *node = dev->of_node;
	int ret, i;

	for (i = 0; i < GPIO_NUM; ++i) {
		enum of_gpio_flags of_flags;

		ret = of_get_named_gpio_flags(node, prop_names[i],
					      0, &of_flags);
		if (ret < 0) {
			dev_err(dev, "failed to parse %s DT property\n",
				prop_names[i]);
			return -EINVAL;
		}
		state->gpio[i].gpio = ret;
		state->gpio[i].level = !(of_flags & OF_GPIO_ACTIVE_LOW);
	}
	return 0;
}

static int s5c73m3_get_platform_data(struct s5c73m3 *state)
{
	struct device *dev = &state->i2c_client->dev;
	const struct s5c73m3_platform_data *pdata = dev->platform_data;
	struct device_node *node = dev->of_node;
	struct device_node *node_ep;
	struct v4l2_of_endpoint ep;
	int ret;

	if (!node) {
		if (!pdata) {
			dev_err(dev, "Platform data not specified\n");
			return -EINVAL;
		}

		state->mclk_frequency = pdata->mclk_frequency;
		state->gpio[STBY] = pdata->gpio_stby;
		state->gpio[RST] = pdata->gpio_reset;
		return 0;
	}

	state->clock = devm_clk_get(dev, S5C73M3_CLK_NAME);
	if (IS_ERR(state->clock))
		return PTR_ERR(state->clock);

	if (of_property_read_u32(node, "clock-frequency",
				 &state->mclk_frequency)) {
		state->mclk_frequency = S5C73M3_DEFAULT_MCLK_FREQ;
		dev_info(dev, "using default %u Hz clock frequency\n",
					state->mclk_frequency);
	}

	ret = s5c73m3_parse_gpios(state);
	if (ret < 0)
		return -EINVAL;

	node_ep = of_graph_get_next_endpoint(node, NULL);
	if (!node_ep) {
		dev_warn(dev, "no endpoint defined for node: %s\n",
						node->full_name);
		return 0;
	}

	v4l2_of_parse_endpoint(node_ep, &ep);
	of_node_put(node_ep);

	if (ep.bus_type != V4L2_MBUS_CSI2) {
		dev_err(dev, "unsupported bus type\n");
		return -EINVAL;
	}
	/*
	 * Number of MIPI CSI-2 data lanes is currently not configurable,
	 * always a default value of 4 lanes is used.
	 */
	if (ep.bus.mipi_csi2.num_data_lanes != S5C73M3_MIPI_DATA_LANES)
		dev_info(dev, "falling back to 4 MIPI CSI-2 data lanes\n");

	return 0;
}

static int s5c73m3_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct v4l2_subdev *sd;
	struct v4l2_subdev *oif_sd;
	struct s5c73m3 *state;
	int ret, i;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->i2c_client = client;
	ret = s5c73m3_get_platform_data(state);
	if (ret < 0)
		return ret;

	mutex_init(&state->lock);
	sd = &state->sensor_sd;
	oif_sd = &state->oif_sd;

	v4l2_subdev_init(sd, &s5c73m3_subdev_ops);
	sd->owner = client->dev.driver->owner;
	v4l2_set_subdevdata(sd, state);
	strlcpy(sd->name, "S5C73M3", sizeof(sd->name));

	sd->internal_ops = &s5c73m3_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	state->sensor_pads[S5C73M3_JPEG_PAD].flags = MEDIA_PAD_FL_SOURCE;
	state->sensor_pads[S5C73M3_ISP_PAD].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;

	ret = media_entity_init(&sd->entity, S5C73M3_NUM_PADS,
							state->sensor_pads, 0);
	if (ret < 0)
		return ret;

	v4l2_i2c_subdev_init(oif_sd, client, &oif_subdev_ops);
	strcpy(oif_sd->name, "S5C73M3-OIF");

	oif_sd->internal_ops = &oif_internal_ops;
	oif_sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	state->oif_pads[OIF_ISP_PAD].flags = MEDIA_PAD_FL_SINK;
	state->oif_pads[OIF_JPEG_PAD].flags = MEDIA_PAD_FL_SINK;
	state->oif_pads[OIF_SOURCE_PAD].flags = MEDIA_PAD_FL_SOURCE;
	oif_sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;

	ret = media_entity_init(&oif_sd->entity, OIF_NUM_PADS,
							state->oif_pads, 0);
	if (ret < 0)
		return ret;

	ret = s5c73m3_configure_gpios(state);
	if (ret)
		goto out_err;

	for (i = 0; i < S5C73M3_MAX_SUPPLIES; i++)
		state->supplies[i].supply = s5c73m3_supply_names[i];

	ret = devm_regulator_bulk_get(dev, S5C73M3_MAX_SUPPLIES,
			       state->supplies);
	if (ret) {
		dev_err(dev, "failed to get regulators\n");
		goto out_err;
	}

	ret = s5c73m3_init_controls(state);
	if (ret)
		goto out_err;

	state->sensor_pix_size[RES_ISP] = &s5c73m3_isp_resolutions[1];
	state->sensor_pix_size[RES_JPEG] = &s5c73m3_jpeg_resolutions[1];
	state->oif_pix_size[RES_ISP] = state->sensor_pix_size[RES_ISP];
	state->oif_pix_size[RES_JPEG] = state->sensor_pix_size[RES_JPEG];

	state->mbus_code = S5C73M3_ISP_FMT;

	state->fiv = &s5c73m3_intervals[S5C73M3_DEFAULT_FRAME_INTERVAL];

	state->fw_file_version[0] = 'G';
	state->fw_file_version[1] = 'C';

	ret = s5c73m3_register_spi_driver(state);
	if (ret < 0)
		goto out_err;

	oif_sd->dev = dev;

	ret = __s5c73m3_power_on(state);
	if (ret < 0)
		goto out_err1;

	ret = s5c73m3_get_fw_version(state);
	__s5c73m3_power_off(state);

	if (ret < 0) {
		dev_err(dev, "Device detection failed: %d\n", ret);
		goto out_err1;
	}

	ret = v4l2_async_register_subdev(oif_sd);
	if (ret < 0)
		goto out_err1;

	v4l2_info(sd, "%s: completed successfully\n", __func__);
	return 0;

out_err1:
	s5c73m3_unregister_spi_driver(state);
out_err:
	media_entity_cleanup(&sd->entity);
	return ret;
}

static int s5c73m3_remove(struct i2c_client *client)
{
	struct v4l2_subdev *oif_sd = i2c_get_clientdata(client);
	struct s5c73m3 *state = oif_sd_to_s5c73m3(oif_sd);
	struct v4l2_subdev *sensor_sd = &state->sensor_sd;

	v4l2_async_unregister_subdev(oif_sd);

	v4l2_ctrl_handler_free(oif_sd->ctrl_handler);
	media_entity_cleanup(&oif_sd->entity);

	v4l2_device_unregister_subdev(sensor_sd);
	media_entity_cleanup(&sensor_sd->entity);

	s5c73m3_unregister_spi_driver(state);

	return 0;
}

static const struct i2c_device_id s5c73m3_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s5c73m3_id);

#ifdef CONFIG_OF
static const struct of_device_id s5c73m3_of_match[] = {
	{ .compatible = "samsung,s5c73m3" },
	{ }
};
MODULE_DEVICE_TABLE(of, s5c73m3_of_match);
#endif

static struct i2c_driver s5c73m3_i2c_driver = {
	.driver = {
		.of_match_table = of_match_ptr(s5c73m3_of_match),
		.name	= DRIVER_NAME,
	},
	.probe		= s5c73m3_probe,
	.remove		= s5c73m3_remove,
	.id_table	= s5c73m3_id,
};

module_i2c_driver(s5c73m3_i2c_driver);

MODULE_DESCRIPTION("Samsung S5C73M3 camera driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL");
