/*
 * Driver for Samsung S5K4ECGX 1/4" 5Mp CMOS Image Sensor SoC
 * with an Embedded Image Signal Processor.
 *
 * Copyright (C) 2012, Linaro, Sangwook Lee <sangwook.lee@linaro.org>
 * Copyright (C) 2012, Insignal Co,. Ltd, Homin Lee <suapapa@insignal.co.kr>
 *
 * Based on s5k6aa and noon010pc30 driver
 * Copyright (C) 2011, Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#include <media/media-entity.h>
#include <media/i2c/s5k4ecgx.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

static int debug;
module_param(debug, int, 0644);

#define S5K4ECGX_DRIVER_NAME		"s5k4ecgx"
#define S5K4ECGX_FIRMWARE		"s5k4ecgx.bin"

/* Firmware revision information */
#define REG_FW_REVISION			0x700001a6
#define REG_FW_VERSION			0x700001a4
#define S5K4ECGX_REVISION_1_1		0x11
#define S5K4ECGX_FW_VERSION		0x4ec0

/* General purpose parameters */
#define REG_USER_BRIGHTNESS		0x7000022c
#define REG_USER_CONTRAST		0x7000022e
#define REG_USER_SATURATION		0x70000230

#define REG_G_ENABLE_PREV		0x7000023e
#define REG_G_ENABLE_PREV_CHG		0x70000240
#define REG_G_NEW_CFG_SYNC		0x7000024a
#define REG_G_PREV_IN_WIDTH		0x70000250
#define REG_G_PREV_IN_HEIGHT		0x70000252
#define REG_G_PREV_IN_XOFFS		0x70000254
#define REG_G_PREV_IN_YOFFS		0x70000256
#define REG_G_CAP_IN_WIDTH		0x70000258
#define REG_G_CAP_IN_HEIGHT		0x7000025a
#define REG_G_CAP_IN_XOFFS		0x7000025c
#define REG_G_CAP_IN_YOFFS		0x7000025e
#define REG_G_INPUTS_CHANGE_REQ		0x70000262
#define REG_G_ACTIVE_PREV_CFG		0x70000266
#define REG_G_PREV_CFG_CHG		0x70000268
#define REG_G_PREV_OPEN_AFTER_CH	0x7000026a

/* Preview context register sets. n = 0...4. */
#define PREG(n, x)			((n) * 0x30 + (x))
#define REG_P_OUT_WIDTH(n)		PREG(n, 0x700002a6)
#define REG_P_OUT_HEIGHT(n)		PREG(n, 0x700002a8)
#define REG_P_FMT(n)			PREG(n, 0x700002aa)
#define REG_P_PVI_MASK(n)		PREG(n, 0x700002b4)
#define REG_P_FR_TIME_TYPE(n)		PREG(n, 0x700002be)
#define  FR_TIME_DYNAMIC		0
#define  FR_TIME_FIXED			1
#define  FR_TIME_FIXED_ACCURATE		2
#define REG_P_FR_TIME_Q_TYPE(n)		PREG(n, 0x700002c0)
#define  FR_TIME_Q_DYNAMIC		0
#define  FR_TIME_Q_BEST_FRRATE		1
#define  FR_TIME_Q_BEST_QUALITY		2

/* Frame period in 0.1 ms units */
#define REG_P_MAX_FR_TIME(n)		PREG(n, 0x700002c2)
#define REG_P_MIN_FR_TIME(n)		PREG(n, 0x700002c4)
#define  US_TO_FR_TIME(__t)		((__t) / 100)
#define REG_P_PREV_MIRROR(n)		PREG(n, 0x700002d0)
#define REG_P_CAP_MIRROR(n)		PREG(n, 0x700002d2)

#define REG_G_PREVZOOM_IN_WIDTH		0x70000494
#define REG_G_PREVZOOM_IN_HEIGHT	0x70000496
#define REG_G_PREVZOOM_IN_XOFFS		0x70000498
#define REG_G_PREVZOOM_IN_YOFFS		0x7000049a
#define REG_G_CAPZOOM_IN_WIDTH		0x7000049c
#define REG_G_CAPZOOM_IN_HEIGHT		0x7000049e
#define REG_G_CAPZOOM_IN_XOFFS		0x700004a0
#define REG_G_CAPZOOM_IN_YOFFS		0x700004a2

/* n = 0...4 */
#define REG_USER_SHARPNESS(n)		(0x70000a28 + (n) * 0xb6)

/* Reduce sharpness range for user space API */
#define SHARPNESS_DIV			8208
#define TOK_TERM			0xffffffff

/*
 * FIXME: This is copied from s5k6aa, because of no information
 * in the S5K4ECGX datasheet.
 * H/W register Interface (0xd0000000 - 0xd0000fff)
 */
#define AHB_MSB_ADDR_PTR		0xfcfc
#define GEN_REG_OFFSH			0xd000
#define REG_CMDWR_ADDRH			0x0028
#define REG_CMDWR_ADDRL			0x002a
#define REG_CMDRD_ADDRH			0x002c
#define REG_CMDRD_ADDRL			0x002e
#define REG_CMDBUF0_ADDR		0x0f12

struct s5k4ecgx_frmsize {
	struct v4l2_frmsize_discrete size;
	/* Fixed sensor matrix crop rectangle */
	struct v4l2_rect input_window;
};

struct regval_list {
	u32 addr;
	u16 val;
};

/*
 * TODO: currently only preview is supported and snapshot (capture)
 * is not implemented yet
 */
static const struct s5k4ecgx_frmsize s5k4ecgx_prev_sizes[] = {
	{
		.size = { 176, 144 },
		.input_window = { 0x00, 0x00, 0x928, 0x780 },
	}, {
		.size = { 352, 288 },
		.input_window = { 0x00, 0x00, 0x928, 0x780 },
	}, {
		.size = { 640, 480 },
		.input_window = { 0x00, 0x00, 0xa00, 0x780 },
	}, {
		.size = { 720, 480 },
		.input_window = { 0x00, 0x00, 0xa00, 0x6a8 },
	}
};

#define S5K4ECGX_NUM_PREV ARRAY_SIZE(s5k4ecgx_prev_sizes)

struct s5k4ecgx_pixfmt {
	u32 code;
	u32 colorspace;
	/* REG_TC_PCFG_Format register value */
	u16 reg_p_format;
};

/* By default value, output from sensor will be YUV422 0-255 */
static const struct s5k4ecgx_pixfmt s5k4ecgx_formats[] = {
	{ MEDIA_BUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG, 5 },
};

static const char * const s5k4ecgx_supply_names[] = {
	/*
	 * Usually 2.8V is used for analog power (vdda)
	 * and digital IO (vddio, vdddcore)
	 */
	"vdda",
	"vddio",
	"vddcore",
	"vddreg", /* The internal s5k4ecgx regulator's supply (1.8V) */
};

#define S5K4ECGX_NUM_SUPPLIES ARRAY_SIZE(s5k4ecgx_supply_names)

enum s5k4ecgx_gpio_id {
	STBY,
	RST,
	GPIO_NUM,
};

struct s5k4ecgx {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler handler;

	struct s5k4ecgx_platform_data *pdata;
	const struct s5k4ecgx_pixfmt *curr_pixfmt;
	const struct s5k4ecgx_frmsize *curr_frmsize;
	struct mutex lock;
	u8 streaming;
	u8 set_params;

	struct regulator_bulk_data supplies[S5K4ECGX_NUM_SUPPLIES];
	struct s5k4ecgx_gpio gpio[GPIO_NUM];
};

static inline struct s5k4ecgx *to_s5k4ecgx(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k4ecgx, sd);
}

static int s5k4ecgx_i2c_read(struct i2c_client *client, u16 addr, u16 *val)
{
	u8 wbuf[2] = { addr >> 8, addr & 0xff };
	struct i2c_msg msg[2];
	u8 rbuf[2];
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = wbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = rbuf;

	ret = i2c_transfer(client->adapter, msg, 2);
	*val = be16_to_cpu(*((__be16 *)rbuf));

	v4l2_dbg(4, debug, client, "i2c_read: 0x%04X : 0x%04x\n", addr, *val);

	return ret == 2 ? 0 : ret;
}

static int s5k4ecgx_i2c_write(struct i2c_client *client, u16 addr, u16 val)
{
	u8 buf[4] = { addr >> 8, addr & 0xff, val >> 8, val & 0xff };

	int ret = i2c_master_send(client, buf, 4);
	v4l2_dbg(4, debug, client, "i2c_write: 0x%04x : 0x%04x\n", addr, val);

	return ret == 4 ? 0 : ret;
}

static int s5k4ecgx_write(struct i2c_client *client, u32 addr, u16 val)
{
	u16 high = addr >> 16, low = addr & 0xffff;
	int ret;

	v4l2_dbg(3, debug, client, "write: 0x%08x : 0x%04x\n", addr, val);

	ret = s5k4ecgx_i2c_write(client, REG_CMDWR_ADDRH, high);
	if (!ret)
		ret = s5k4ecgx_i2c_write(client, REG_CMDWR_ADDRL, low);
	if (!ret)
		ret = s5k4ecgx_i2c_write(client, REG_CMDBUF0_ADDR, val);

	return ret;
}

static int s5k4ecgx_read(struct i2c_client *client, u32 addr, u16 *val)
{
	u16 high = addr >> 16, low =  addr & 0xffff;
	int ret;

	ret = s5k4ecgx_i2c_write(client, REG_CMDRD_ADDRH, high);
	if (!ret)
		ret = s5k4ecgx_i2c_write(client, REG_CMDRD_ADDRL, low);
	if (!ret)
		ret = s5k4ecgx_i2c_read(client, REG_CMDBUF0_ADDR, val);
	if (!ret)
		dev_err(&client->dev, "Failed to execute read command\n");

	return ret;
}

static int s5k4ecgx_read_fw_ver(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u16 hw_rev, fw_ver = 0;
	int ret;

	ret = s5k4ecgx_read(client, REG_FW_VERSION, &fw_ver);
	if (ret < 0 || fw_ver != S5K4ECGX_FW_VERSION) {
		v4l2_err(sd, "FW version check failed!\n");
		return -ENODEV;
	}

	ret = s5k4ecgx_read(client, REG_FW_REVISION, &hw_rev);
	if (ret < 0)
		return ret;

	v4l2_info(sd, "chip found FW ver: 0x%x, HW rev: 0x%x\n",
						fw_ver, hw_rev);
	return 0;
}

static int s5k4ecgx_set_ahb_address(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	/* Set APB peripherals start address */
	ret = s5k4ecgx_i2c_write(client, AHB_MSB_ADDR_PTR, GEN_REG_OFFSH);
	if (ret < 0)
		return ret;
	/*
	 * FIXME: This is copied from s5k6aa, because of no information
	 * in s5k4ecgx's datasheet.
	 * sw_reset is activated to put device into idle status
	 */
	ret = s5k4ecgx_i2c_write(client, 0x0010, 0x0001);
	if (ret < 0)
		return ret;

	ret = s5k4ecgx_i2c_write(client, 0x1030, 0x0000);
	if (ret < 0)
		return ret;
	/* Halt ARM CPU */
	return s5k4ecgx_i2c_write(client, 0x0014, 0x0001);
}

#define FW_CRC_SIZE	4
/* Register address, value are 4, 2 bytes */
#define FW_RECORD_SIZE	6
/*
 * The firmware has following format:
 * < total number of records (4 bytes + 2 bytes padding) N >,
 * < record 0 >, ..., < record N - 1 >, < CRC32-CCITT (4-bytes) >,
 * where "record" is a 4-byte register address followed by 2-byte
 * register value (little endian).
 * The firmware generator can be found in following git repository:
 * git://git.linaro.org/people/sangwook/fimc-v4l2-app.git
 */
static int s5k4ecgx_load_firmware(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	const struct firmware *fw;
	const u8 *ptr;
	int err, i, regs_num;
	u32 addr, crc, crc_file, addr_inc = 0;
	u16 val;

	err = request_firmware(&fw, S5K4ECGX_FIRMWARE, sd->v4l2_dev->dev);
	if (err) {
		v4l2_err(sd, "Failed to read firmware %s\n", S5K4ECGX_FIRMWARE);
		return err;
	}
	regs_num = get_unaligned_le32(fw->data);

	v4l2_dbg(3, debug, sd, "FW: %s size %zu register sets %d\n",
		 S5K4ECGX_FIRMWARE, fw->size, regs_num);

	regs_num++; /* Add header */
	if (fw->size != regs_num * FW_RECORD_SIZE + FW_CRC_SIZE) {
		err = -EINVAL;
		goto fw_out;
	}
	crc_file = get_unaligned_le32(fw->data + regs_num * FW_RECORD_SIZE);
	crc = crc32_le(~0, fw->data, regs_num * FW_RECORD_SIZE);
	if (crc != crc_file) {
		v4l2_err(sd, "FW: invalid crc (%#x:%#x)\n", crc, crc_file);
		err = -EINVAL;
		goto fw_out;
	}
	ptr = fw->data + FW_RECORD_SIZE;
	for (i = 1; i < regs_num; i++) {
		addr = get_unaligned_le32(ptr);
		ptr += sizeof(u32);
		val = get_unaligned_le16(ptr);
		ptr += sizeof(u16);
		if (addr - addr_inc != 2)
			err = s5k4ecgx_write(client, addr, val);
		else
			err = s5k4ecgx_i2c_write(client, REG_CMDBUF0_ADDR, val);
		if (err)
			break;
		addr_inc = addr;
	}
fw_out:
	release_firmware(fw);
	return err;
}

/* Set preview and capture input window */
static int s5k4ecgx_set_input_window(struct i2c_client *c,
				     const struct v4l2_rect *r)
{
	int ret;

	ret = s5k4ecgx_write(c, REG_G_PREV_IN_WIDTH, r->width);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_PREV_IN_HEIGHT, r->height);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_PREV_IN_XOFFS, r->left);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_PREV_IN_YOFFS, r->top);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAP_IN_WIDTH, r->width);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAP_IN_HEIGHT, r->height);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAP_IN_XOFFS, r->left);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAP_IN_YOFFS, r->top);

	return ret;
}

/* Set preview and capture zoom input window */
static int s5k4ecgx_set_zoom_window(struct i2c_client *c,
				    const struct v4l2_rect *r)
{
	int ret;

	ret = s5k4ecgx_write(c, REG_G_PREVZOOM_IN_WIDTH, r->width);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_PREVZOOM_IN_HEIGHT, r->height);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_PREVZOOM_IN_XOFFS, r->left);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_PREVZOOM_IN_YOFFS, r->top);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAPZOOM_IN_WIDTH, r->width);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAPZOOM_IN_HEIGHT, r->height);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAPZOOM_IN_XOFFS, r->left);
	if (!ret)
		ret = s5k4ecgx_write(c, REG_G_CAPZOOM_IN_YOFFS, r->top);

	return ret;
}

static int s5k4ecgx_set_output_framefmt(struct s5k4ecgx *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	int ret;

	ret = s5k4ecgx_write(client, REG_P_OUT_WIDTH(0),
			     priv->curr_frmsize->size.width);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_OUT_HEIGHT(0),
				     priv->curr_frmsize->size.height);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_FMT(0),
				     priv->curr_pixfmt->reg_p_format);
	return ret;
}

static int s5k4ecgx_init_sensor(struct v4l2_subdev *sd)
{
	int ret;

	ret = s5k4ecgx_set_ahb_address(sd);

	/* The delay is from manufacturer's settings */
	msleep(100);

	if (!ret)
		ret = s5k4ecgx_load_firmware(sd);
	if (ret)
		v4l2_err(sd, "Failed to write initial settings\n");

	return ret;
}

static int s5k4ecgx_gpio_set_value(struct s5k4ecgx *priv, int id, u32 val)
{
	if (!gpio_is_valid(priv->gpio[id].gpio))
		return 0;
	gpio_set_value(priv->gpio[id].gpio, val);

	return 1;
}

static int __s5k4ecgx_power_on(struct s5k4ecgx *priv)
{
	int ret;

	ret = regulator_bulk_enable(S5K4ECGX_NUM_SUPPLIES, priv->supplies);
	if (ret)
		return ret;
	usleep_range(30, 50);

	/* The polarity of STBY is controlled by TSP */
	if (s5k4ecgx_gpio_set_value(priv, STBY, priv->gpio[STBY].level))
		usleep_range(30, 50);

	if (s5k4ecgx_gpio_set_value(priv, RST, priv->gpio[RST].level))
		usleep_range(30, 50);

	return 0;
}

static int __s5k4ecgx_power_off(struct s5k4ecgx *priv)
{
	if (s5k4ecgx_gpio_set_value(priv, RST, !priv->gpio[RST].level))
		usleep_range(30, 50);

	if (s5k4ecgx_gpio_set_value(priv, STBY, !priv->gpio[STBY].level))
		usleep_range(30, 50);

	priv->streaming = 0;

	return regulator_bulk_disable(S5K4ECGX_NUM_SUPPLIES, priv->supplies);
}

/* Find nearest matching image pixel size. */
static int s5k4ecgx_try_frame_size(struct v4l2_mbus_framefmt *mf,
				  const struct s5k4ecgx_frmsize **size)
{
	unsigned int min_err = ~0;
	int i = ARRAY_SIZE(s5k4ecgx_prev_sizes);
	const struct s5k4ecgx_frmsize *fsize = &s5k4ecgx_prev_sizes[0],
		*match = NULL;

	while (i--) {
		int err = abs(fsize->size.width - mf->width)
				+ abs(fsize->size.height - mf->height);
		if (err < min_err) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}
	if (match) {
		mf->width  = match->size.width;
		mf->height = match->size.height;
		if (size)
			*size = match;
		return 0;
	}

	return -EINVAL;
}

static int s5k4ecgx_enum_mbus_code(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(s5k4ecgx_formats))
		return -EINVAL;
	code->code = s5k4ecgx_formats[code->index].code;

	return 0;
}

static int s5k4ecgx_get_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);
	struct v4l2_mbus_framefmt *mf;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (cfg) {
			mf = v4l2_subdev_get_try_format(sd, cfg, 0);
			fmt->format = *mf;
		}
		return 0;
	}

	mf = &fmt->format;

	mutex_lock(&priv->lock);
	mf->width = priv->curr_frmsize->size.width;
	mf->height = priv->curr_frmsize->size.height;
	mf->code = priv->curr_pixfmt->code;
	mf->colorspace = priv->curr_pixfmt->colorspace;
	mf->field = V4L2_FIELD_NONE;
	mutex_unlock(&priv->lock);

	return 0;
}

static const struct s5k4ecgx_pixfmt *s5k4ecgx_try_fmt(struct v4l2_subdev *sd,
					    struct v4l2_mbus_framefmt *mf)
{
	int i = ARRAY_SIZE(s5k4ecgx_formats);

	while (--i)
		if (mf->code == s5k4ecgx_formats[i].code)
			break;
	mf->code = s5k4ecgx_formats[i].code;

	return &s5k4ecgx_formats[i];
}

static int s5k4ecgx_set_fmt(struct v4l2_subdev *sd, struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);
	const struct s5k4ecgx_frmsize *fsize = NULL;
	const struct s5k4ecgx_pixfmt *pf;
	struct v4l2_mbus_framefmt *mf;
	int ret = 0;

	pf = s5k4ecgx_try_fmt(sd, &fmt->format);
	s5k4ecgx_try_frame_size(&fmt->format, &fsize);
	fmt->format.colorspace = V4L2_COLORSPACE_JPEG;
	fmt->format.field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		if (cfg) {
			mf = v4l2_subdev_get_try_format(sd, cfg, 0);
			*mf = fmt->format;
		}
		return 0;
	}

	mutex_lock(&priv->lock);
	if (!priv->streaming) {
		priv->curr_frmsize = fsize;
		priv->curr_pixfmt = pf;
		priv->set_params = 1;
	} else {
		ret = -EBUSY;
	}
	mutex_unlock(&priv->lock);

	return ret;
}

static const struct v4l2_subdev_pad_ops s5k4ecgx_pad_ops = {
	.enum_mbus_code	= s5k4ecgx_enum_mbus_code,
	.get_fmt	= s5k4ecgx_get_fmt,
	.set_fmt	= s5k4ecgx_set_fmt,
};

/*
 * V4L2 subdev controls
 */
static int s5k4ecgx_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = &container_of(ctrl->handler, struct s5k4ecgx,
						handler)->sd;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);
	unsigned int i;
	int err = 0;

	v4l2_dbg(1, debug, sd, "ctrl: 0x%x, value: %d\n", ctrl->id, ctrl->val);

	mutex_lock(&priv->lock);
	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		err = s5k4ecgx_write(client, REG_USER_CONTRAST, ctrl->val);
		break;

	case V4L2_CID_SATURATION:
		err = s5k4ecgx_write(client, REG_USER_SATURATION, ctrl->val);
		break;

	case V4L2_CID_SHARPNESS:
		/* TODO: Revisit, is this setting for all presets ? */
		for (i = 0; i < 4 && !err; i++)
			err = s5k4ecgx_write(client, REG_USER_SHARPNESS(i),
					     ctrl->val * SHARPNESS_DIV);
		break;

	case V4L2_CID_BRIGHTNESS:
		err = s5k4ecgx_write(client, REG_USER_BRIGHTNESS, ctrl->val);
		break;
	}
	mutex_unlock(&priv->lock);
	if (err < 0)
		v4l2_err(sd, "Failed to write s_ctrl err %d\n", err);

	return err;
}

static const struct v4l2_ctrl_ops s5k4ecgx_ctrl_ops = {
	.s_ctrl = s5k4ecgx_s_ctrl,
};

/*
 * Reading s5k4ecgx version information
 */
static int s5k4ecgx_registered(struct v4l2_subdev *sd)
{
	int ret;
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);

	mutex_lock(&priv->lock);
	ret = __s5k4ecgx_power_on(priv);
	if (!ret) {
		ret = s5k4ecgx_read_fw_ver(sd);
		__s5k4ecgx_power_off(priv);
	}
	mutex_unlock(&priv->lock);

	return ret;
}

/*
 * V4L2 subdev internal operations
 */
static int s5k4ecgx_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *mf = v4l2_subdev_get_try_format(sd, fh->pad, 0);

	mf->width = s5k4ecgx_prev_sizes[0].size.width;
	mf->height = s5k4ecgx_prev_sizes[0].size.height;
	mf->code = s5k4ecgx_formats[0].code;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	mf->field = V4L2_FIELD_NONE;

	return 0;
}

static const struct v4l2_subdev_internal_ops s5k4ecgx_subdev_internal_ops = {
	.registered = s5k4ecgx_registered,
	.open = s5k4ecgx_open,
};

static int s5k4ecgx_s_power(struct v4l2_subdev *sd, int on)
{
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);
	int ret;

	v4l2_dbg(1, debug, sd, "Switching %s\n", on ? "on" : "off");

	if (on) {
		ret = __s5k4ecgx_power_on(priv);
		if (ret < 0)
			return ret;
		/* Time to stabilize sensor */
		msleep(100);
		ret = s5k4ecgx_init_sensor(sd);
		if (ret < 0)
			__s5k4ecgx_power_off(priv);
		else
			priv->set_params = 1;
	} else {
		ret = __s5k4ecgx_power_off(priv);
	}

	return ret;
}

static int s5k4ecgx_log_status(struct v4l2_subdev *sd)
{
	v4l2_ctrl_handler_log_status(sd->ctrl_handler, sd->name);

	return 0;
}

static const struct v4l2_subdev_core_ops s5k4ecgx_core_ops = {
	.s_power	= s5k4ecgx_s_power,
	.log_status	= s5k4ecgx_log_status,
};

static int __s5k4ecgx_s_params(struct s5k4ecgx *priv)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	const struct v4l2_rect *crop_rect = &priv->curr_frmsize->input_window;
	int ret;

	ret = s5k4ecgx_set_input_window(client, crop_rect);
	if (!ret)
		ret = s5k4ecgx_set_zoom_window(client, crop_rect);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_G_INPUTS_CHANGE_REQ, 1);
	if (!ret)
		ret = s5k4ecgx_write(client, 0x70000a1e, 0x28);
	if (!ret)
		ret = s5k4ecgx_write(client, 0x70000ad4, 0x3c);
	if (!ret)
		ret = s5k4ecgx_set_output_framefmt(priv);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_PVI_MASK(0), 0x52);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_FR_TIME_TYPE(0),
				     FR_TIME_DYNAMIC);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_FR_TIME_Q_TYPE(0),
				     FR_TIME_Q_BEST_FRRATE);
	if (!ret)
		ret = s5k4ecgx_write(client,  REG_P_MIN_FR_TIME(0),
				     US_TO_FR_TIME(33300));
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_MAX_FR_TIME(0),
				     US_TO_FR_TIME(66600));
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_PREV_MIRROR(0), 0);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_P_CAP_MIRROR(0), 0);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_G_ACTIVE_PREV_CFG, 0);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_G_PREV_OPEN_AFTER_CH, 1);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_G_NEW_CFG_SYNC, 1);
	if (!ret)
		ret = s5k4ecgx_write(client, REG_G_PREV_CFG_CHG, 1);

	return ret;
}

static int __s5k4ecgx_s_stream(struct s5k4ecgx *priv, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(&priv->sd);
	int ret;

	if (on && priv->set_params) {
		ret = __s5k4ecgx_s_params(priv);
		if (ret < 0)
			return ret;
		priv->set_params = 0;
	}
	/*
	 * This enables/disables preview stream only. Capture requests
	 * are not supported yet.
	 */
	ret = s5k4ecgx_write(client, REG_G_ENABLE_PREV, on);
	if (ret < 0)
		return ret;
	return s5k4ecgx_write(client, REG_G_ENABLE_PREV_CHG, 1);
}

static int s5k4ecgx_s_stream(struct v4l2_subdev *sd, int on)
{
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);
	int ret = 0;

	v4l2_dbg(1, debug, sd, "Turn streaming %s\n", on ? "on" : "off");

	mutex_lock(&priv->lock);

	if (priv->streaming == !on) {
		ret = __s5k4ecgx_s_stream(priv, on);
		if (!ret)
			priv->streaming = on & 1;
	}

	mutex_unlock(&priv->lock);
	return ret;
}

static const struct v4l2_subdev_video_ops s5k4ecgx_video_ops = {
	.s_stream = s5k4ecgx_s_stream,
};

static const struct v4l2_subdev_ops s5k4ecgx_ops = {
	.core = &s5k4ecgx_core_ops,
	.pad = &s5k4ecgx_pad_ops,
	.video = &s5k4ecgx_video_ops,
};

/*
 * GPIO setup
 */
static int s5k4ecgx_config_gpio(int nr, int val, const char *name)
{
	unsigned long flags = val ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW;
	int ret;

	if (!gpio_is_valid(nr))
		return 0;
	ret = gpio_request_one(nr, flags, name);
	if (!ret)
		gpio_export(nr, 0);

	return ret;
}

static void s5k4ecgx_free_gpios(struct s5k4ecgx *priv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->gpio); i++) {
		if (!gpio_is_valid(priv->gpio[i].gpio))
			continue;
		gpio_free(priv->gpio[i].gpio);
		priv->gpio[i].gpio = -EINVAL;
	}
}

static int s5k4ecgx_config_gpios(struct s5k4ecgx *priv,
				  const struct s5k4ecgx_platform_data *pdata)
{
	const struct s5k4ecgx_gpio *gpio = &pdata->gpio_stby;
	int ret;

	priv->gpio[STBY].gpio = -EINVAL;
	priv->gpio[RST].gpio  = -EINVAL;

	ret = s5k4ecgx_config_gpio(gpio->gpio, gpio->level, "S5K4ECGX_STBY");

	if (ret) {
		s5k4ecgx_free_gpios(priv);
		return ret;
	}
	priv->gpio[STBY] = *gpio;
	if (gpio_is_valid(gpio->gpio))
		gpio_set_value(gpio->gpio, 0);

	gpio = &pdata->gpio_reset;

	ret = s5k4ecgx_config_gpio(gpio->gpio, gpio->level, "S5K4ECGX_RST");
	if (ret) {
		s5k4ecgx_free_gpios(priv);
		return ret;
	}
	priv->gpio[RST] = *gpio;
	if (gpio_is_valid(gpio->gpio))
		gpio_set_value(gpio->gpio, 0);

	return 0;
}

static int s5k4ecgx_init_v4l2_ctrls(struct s5k4ecgx *priv)
{
	const struct v4l2_ctrl_ops *ops = &s5k4ecgx_ctrl_ops;
	struct v4l2_ctrl_handler *hdl = &priv->handler;
	int ret;

	ret = v4l2_ctrl_handler_init(hdl, 4);
	if (ret)
		return ret;

	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_BRIGHTNESS, -208, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_CONTRAST, -127, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SATURATION, -127, 127, 1, 0);

	/* Sharpness default is 24612, and then (24612/SHARPNESS_DIV) = 2 */
	v4l2_ctrl_new_std(hdl, ops, V4L2_CID_SHARPNESS, -32704/SHARPNESS_DIV,
			  24612/SHARPNESS_DIV, 1, 2);
	if (hdl->error) {
		ret = hdl->error;
		v4l2_ctrl_handler_free(hdl);
		return ret;
	}
	priv->sd.ctrl_handler = hdl;

	return 0;
};

static int s5k4ecgx_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct s5k4ecgx_platform_data *pdata = client->dev.platform_data;
	struct v4l2_subdev *sd;
	struct s5k4ecgx *priv;
	int ret, i;

	if (pdata == NULL) {
		dev_err(&client->dev, "platform data is missing!\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&client->dev, sizeof(struct s5k4ecgx), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->lock);
	priv->streaming = 0;

	sd = &priv->sd;
	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &s5k4ecgx_ops);
	/* Static name; NEVER use in new drivers! */
	strscpy(sd->name, S5K4ECGX_DRIVER_NAME, sizeof(sd->name));

	sd->internal_ops = &s5k4ecgx_subdev_internal_ops;
	/* Support v4l2 sub-device user space API */
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &priv->pad);
	if (ret)
		return ret;

	ret = s5k4ecgx_config_gpios(priv, pdata);
	if (ret) {
		dev_err(&client->dev, "Failed to set gpios\n");
		goto out_err1;
	}
	for (i = 0; i < S5K4ECGX_NUM_SUPPLIES; i++)
		priv->supplies[i].supply = s5k4ecgx_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev, S5K4ECGX_NUM_SUPPLIES,
				 priv->supplies);
	if (ret) {
		dev_err(&client->dev, "Failed to get regulators\n");
		goto out_err2;
	}
	ret = s5k4ecgx_init_v4l2_ctrls(priv);
	if (ret)
		goto out_err2;

	priv->curr_pixfmt = &s5k4ecgx_formats[0];
	priv->curr_frmsize = &s5k4ecgx_prev_sizes[0];

	return 0;

out_err2:
	s5k4ecgx_free_gpios(priv);
out_err1:
	media_entity_cleanup(&priv->sd.entity);

	return ret;
}

static int s5k4ecgx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k4ecgx *priv = to_s5k4ecgx(sd);

	mutex_destroy(&priv->lock);
	s5k4ecgx_free_gpios(priv);
	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&priv->handler);
	media_entity_cleanup(&sd->entity);

	return 0;
}

static const struct i2c_device_id s5k4ecgx_id[] = {
	{ S5K4ECGX_DRIVER_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, s5k4ecgx_id);

static struct i2c_driver v4l2_i2c_driver = {
	.driver = {
		.name = S5K4ECGX_DRIVER_NAME,
	},
	.probe = s5k4ecgx_probe,
	.remove = s5k4ecgx_remove,
	.id_table = s5k4ecgx_id,
};

module_i2c_driver(v4l2_i2c_driver);

MODULE_DESCRIPTION("Samsung S5K4ECGX 5MP SOC camera");
MODULE_AUTHOR("Sangwook Lee <sangwook.lee@linaro.org>");
MODULE_AUTHOR("Seok-Young Jang <quartz.jang@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(S5K4ECGX_FIRMWARE);
