// SPDX-License-Identifier: GPL-2.0
/*
 * max96722 GMSL2/GMSL1 to CSI-2 Deserializer driver
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 *
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x00)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define MAX96722_LINK_FREQ_400MHZ	400000000UL
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define MAX96722_PIXEL_RATE		(MAX96722_LINK_FREQ_400MHZ * 2LL * 4LL / 24LL)
#define MAX96722_XVCLK_FREQ		24000000

#define CHIP_ID				0xA1
#define MAX96722_REG_CHIP_ID		0x0D

#define MAX96722_REG_CTRL_MODE		0x08a0
#define MAX96722_MODE_SW_STANDBY	0x4
#define MAX96722_MODE_STREAMING		0xa4

#define MAX96722_REMOTE_CTRL		0x0003
#define MAX96722_REMOTE_DISABLE		0xFF

#define REG_NULL			0xFFFF

#define MAX96722_LANES			4

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"

#define MAX96722_REG_VALUE_08BIT	1
#define MAX96722_REG_VALUE_16BIT	2
#define MAX96722_REG_VALUE_24BIT	3

#define MAX96722_NAME			"max96722"
#define MAX96722_MEDIA_BUS_FMT		MEDIA_BUS_FMT_RGB888_1X24

static const char * const max96722_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define MAX96722_NUM_SUPPLIES ARRAY_SIZE(max96722_supply_names)

struct regval {
	u16 i2c_addr;
	u16 addr;
	u8 val;
	u16 delay;
};

struct max96722_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	u32 link_freq_idx;
	u32 bpp;
	const struct regval *reg_list;
};

struct max96722 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*power_gpio;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[MAX96722_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	bool			hot_plug;
	u8			is_reset;
	const struct max96722_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_max96722(sd) container_of(sd, struct max96722, subdev)

static const struct regval max96722_mipi_init[] = {
	{0x6b, 0x0006, 0xF0, 0x00},
	// Disable MIPI output
	{0x6b, 0x040B, 0x00, 0x00},
	// RGB888 software override for all pipes since connected GMSL1 is under parallel mode
	{0x6b, 0x040B, 0xC0, 0x00},	//0b11000-000, bpp0=0x18
	{0x6b, 0x040E, 0xA4, 0x00},	//0b10-100100, DT0=0x24
	{0x6b, 0x040F, 0x04, 0x00},	//0b0000-0100, DT1=0x24
	{0x6b, 0x0411, 0x18, 0x00},	//0b000-11000, bpp1=0x18
	//Video pipe sel
	{0x6b, 0x00F0, 0x40, 0x00},	//LINKA-pipex=pipe0, LINKB-pipex=pipe1
	// Send RGB888, FS, and FE from Pipe 0 to Controller 1
	{0x6b, 0x090B, 0x07, 0x00}, // Enable 3 Mappings
	{0x6b, 0x092D, 0x15, 0x00},	//Map Data to Port A
	// For the following MSB 2 bits = VC, LSB 6 bits =DT
	{0x6b, 0x090D, 0x24, 0x00}, // SRC DT = RGB888
	{0x6b, 0x090E, 0x24, 0x00}, // DEST DT = RGB888
	{0x6b, 0x090F, 0x00, 0x00}, // SRC DT = Frame Start
	{0x6b, 0x0910, 0x00, 0x00}, // DEST DT = Frame Start
	{0x6b, 0x0911, 0x01, 0x00}, // SRC DT = Frame End
	{0x6b, 0x0912, 0x01, 0x00}, // DEST DT = Frame End
	//Send RGB888, FS, and FE from Pipe 1 to Controller 2
	{0x6b, 0x094B, 0x07, 0x00},
	{0x6b, 0x096D, 0xAA, 0x00}, // map to MIPI Controller 2
	// For the following MSB 2 bits = VC, LSB 6 bits =DT
	{0x6b, 0x094D, 0x24, 0x00},
	{0x6b, 0x094E, 0x24, 0x00}, // map to VC0
	{0x6b, 0x094F, 0x00, 0x00}, // frame start
	{0x6b, 0x0950, 0x00, 0x00},
	{0x6b, 0x0951, 0x01, 0x00},
	{0x6b, 0x0952, 0x01, 0x00},
	// MIPI PHY Setting
	// Set Des in 2x4 mode
	{0x6b, 0x08A0, 0x04, 0x00},
	// Set Lane Mapping for 4-lane port A
	{0x6b, 0x08A3, 0xE4, 0x00},
	{0x6b, 0x08A4, 0xE4, 0x00},
	// Set 4 lane D-PHY
	{0x6b, 0x090A, 0xC0, 0x00},
	{0x6b, 0x094A, 0xC0, 0x00},
	{0x6b, 0x098A, 0xC0, 0x00},
	{0x6b, 0x09CA, 0xC0, 0x00},
	// Turn on MIPI PHYs
	{0x6b, 0x08A2, 0xF0, 0x00},
	// Hold DPLL in reset (config_soft_rst_n = 0) before changing the rate
	{0x6b, 0x1C00, 0xF4, 0x00},
	{0x6b, 0x1D00, 0xF4, 0x00},
	{0x6b, 0x1E00, 0xF4, 0x00},
	{0x6b, 0x1F00, 0xF4, 0x00},
	// Set Data rate to be 800Mbps/lane for port A and enable software override
	{0x6b, 0x0415, 0xE8, 0x00},	//override pipe0/1
	{0x6b, 0x0418, 0x28, 0x00},
	{0x6b, 0x041B, 0x28, 0x00},
	{0x6b, 0x041E, 0x28, 0x00},
	// Release reset to DPLL (config_soft_rst_n = 1)
	{0x6b, 0x1C00, 0xF5, 0x00},
	{0x6b, 0x1D00, 0xF5, 0x00},
	{0x6b, 0x1E00, 0xF5, 0x00},
	{0x6b, 0x1F00, 0xF5, 0x00},
	{0x6b, 0x0003, 0xFF, 0x00},
	{0x6b, 0x0006, 0xF3, 0x0a},
	// {0x6b, 0x08A0, 0x84},
	{0x6b, REG_NULL, 0x00, 0x00},
};

static const struct max96722_mode supported_modes[] = {
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.reg_list = max96722_mipi_init,
		.link_freq_idx = 0,
	},
};

static const s64 link_freq_items[] = {
	MAX96722_LINK_FREQ_400MHZ,
};

/* Write registers up to 4 at a time */
static int max96722_write_reg(struct i2c_client *client, u16 reg,
			     u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	dev_dbg(&client->dev, "write reg(0x%x val:0x%x)!\n", reg, val);

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev, "%s: writing register 0x%x from 0x%x failed\n",
				__func__, reg, client->addr);
		return -EIO;
	}

	return 0;
}

static int max96722_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++) {
		client->addr = regs[i].i2c_addr;
		ret = max96722_write_reg(client, regs[i].addr,
					MAX96722_REG_VALUE_08BIT,
					regs[i].val);
		msleep(regs[i].delay);
	}

	return ret;
}

/* Read registers up to 4 at a time */
static int max96722_read_reg(struct i2c_client *client, u16 reg,
			    unsigned int len, u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev, "%s: reading register 0x%x from 0x%x failed\n",
				__func__, reg, client->addr);
		return -EIO;
	}

	*val = be32_to_cpu(data_be);

	return 0;
}

static int max96722_get_reso_dist(const struct max96722_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct max96722_mode *
max96722_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = max96722_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int max96722_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct max96722 *max96722 = to_max96722(sd);
	const struct max96722_mode *mode;

	mutex_lock(&max96722->mutex);

	mode = max96722_find_best_fit(fmt);
	fmt->format.code = MAX96722_MEDIA_BUS_FMT;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&max96722->mutex);
		return -ENOTTY;
#endif
	} else {
		if (max96722->streaming) {
			mutex_unlock(&max96722->mutex);
			return -EBUSY;
		}
	}

	mutex_unlock(&max96722->mutex);

	return 0;
}

static int max96722_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct max96722 *max96722 = to_max96722(sd);
	const struct max96722_mode *mode = max96722->cur_mode;

	mutex_lock(&max96722->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&max96722->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MAX96722_MEDIA_BUS_FMT;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&max96722->mutex);

	return 0;
}

static int max96722_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MAX96722_MEDIA_BUS_FMT;

	return 0;
}

static int max96722_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MAX96722_MEDIA_BUS_FMT)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int max96722_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct max96722 *max96722 = to_max96722(sd);
	const struct max96722_mode *mode = max96722->cur_mode;

	mutex_lock(&max96722->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&max96722->mutex);

	return 0;
}

static void max96722_get_module_inf(struct max96722 *max96722,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, MAX96722_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, max96722->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, max96722->len_name, sizeof(inf->base.lens));
}

static void max96722_get_vicap_rst_inf(struct max96722 *max96722,
				   struct rkmodule_vicap_reset_info *rst_info)
{
	struct i2c_client *client = max96722->client;

	rst_info->is_reset = max96722->hot_plug;
	max96722->hot_plug = false;
	rst_info->src = RKCIF_RESET_SRC_ERR_HOTPLUG;
	dev_info(&client->dev, "%s: rst_info->is_reset:%d.\n", __func__, rst_info->is_reset);
}

static void max96722_set_vicap_rst_inf(struct max96722 *max96722,
				   struct rkmodule_vicap_reset_info rst_info)
{
	max96722->is_reset = rst_info.is_reset;
}

static long max96722_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct max96722 *max96722 = to_max96722(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		max96722_get_module_inf(max96722, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = max96722_write_reg(max96722->client,
				 MAX96722_REG_CTRL_MODE,
				 MAX96722_REG_VALUE_08BIT,
				 MAX96722_MODE_STREAMING);
		else
			ret = max96722_write_reg(max96722->client,
				 MAX96722_REG_CTRL_MODE,
				 MAX96722_REG_VALUE_08BIT,
				 MAX96722_MODE_SW_STANDBY);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		max96722_get_vicap_rst_inf(max96722,
			(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		max96722_set_vicap_rst_inf(max96722,
			*(struct rkmodule_vicap_reset_info *)arg);
		break;
	case RKMODULE_GET_CSI_DSI_INFO:
		*(int *)arg = RKMODULE_CSI_INPUT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long max96722_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_vicap_reset_info *vicap_rst_inf;
	long ret = 0;
	int *seq;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = max96722_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, vicap_rst_inf);
		if (!ret) {
			ret = copy_to_user(up, vicap_rst_inf, sizeof(*vicap_rst_inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_SET_VICAP_RST_INFO:
		vicap_rst_inf = kzalloc(sizeof(*vicap_rst_inf), GFP_KERNEL);
		if (!vicap_rst_inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(vicap_rst_inf, up, sizeof(*vicap_rst_inf));
		if (!ret)
			ret = max96722_ioctl(sd, cmd, vicap_rst_inf);
		else
			ret = -EFAULT;
		kfree(vicap_rst_inf);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = max96722_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_GET_CSI_DSI_INFO:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = max96722_ioctl(sd, cmd, seq);
		if (!ret) {
			ret = copy_to_user(up, seq, sizeof(*seq));
			if (ret)
				ret = -EFAULT;
		}
		kfree(seq);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __max96722_start_stream(struct max96722 *max96722)
{
	int ret;

	ret = max96722_write_array(max96722->client, max96722->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&max96722->mutex);
	ret = v4l2_ctrl_handler_setup(&max96722->ctrl_handler);
	mutex_lock(&max96722->mutex);
	if (ret)
		return ret;

	return max96722_write_reg(max96722->client,
				 MAX96722_REG_CTRL_MODE,
				 MAX96722_REG_VALUE_08BIT,
				 MAX96722_MODE_STREAMING);
}

static int __max96722_stop_stream(struct max96722 *max96722)
{
	return max96722_write_reg(max96722->client,
				 MAX96722_REG_CTRL_MODE,
				 MAX96722_REG_VALUE_08BIT,
				 MAX96722_MODE_SW_STANDBY);
}

static int max96722_s_stream(struct v4l2_subdev *sd, int on)
{
	struct max96722 *max96722 = to_max96722(sd);
	struct i2c_client *client = max96722->client;
	int ret = 0;

	dev_info(&client->dev, "%s: on: %d, %dx%d@%d\n", __func__, on,
				max96722->cur_mode->width,
				max96722->cur_mode->height,
		DIV_ROUND_CLOSEST(max96722->cur_mode->max_fps.denominator,
				  max96722->cur_mode->max_fps.numerator));

	mutex_lock(&max96722->mutex);
	on = !!on;
	if (on == max96722->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __max96722_start_stream(max96722);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__max96722_stop_stream(max96722);
		pm_runtime_put(&client->dev);
	}

	max96722->streaming = on;

unlock_and_return:
	mutex_unlock(&max96722->mutex);

	return ret;
}

static int max96722_s_power(struct v4l2_subdev *sd, int on)
{
	struct max96722 *max96722 = to_max96722(sd);
	struct i2c_client *client = max96722->client;
	int ret = 0;

	mutex_lock(&max96722->mutex);

	/* If the power state is not modified - no work to do. */
	if (max96722->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		max96722->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		max96722->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&max96722->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 max96722_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, MAX96722_XVCLK_FREQ / 1000 / 1000);
}

static int __max96722_power_on(struct max96722 *max96722)
{
	int ret;
	u32 delay_us;
	struct device *dev = &max96722->client->dev;

	if (!IS_ERR(max96722->power_gpio))
		gpiod_set_value_cansleep(max96722->power_gpio, 1);

	usleep_range(1000, 2000);

	if (!IS_ERR_OR_NULL(max96722->pins_default)) {
		ret = pinctrl_select_state(max96722->pinctrl,
					   max96722->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (!IS_ERR(max96722->reset_gpio))
		gpiod_set_value_cansleep(max96722->reset_gpio, 0);

	ret = regulator_bulk_enable(MAX96722_NUM_SUPPLIES, max96722->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(max96722->reset_gpio))
		gpiod_set_value_cansleep(max96722->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(max96722->pwdn_gpio))
		gpiod_set_value_cansleep(max96722->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = max96722_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(max96722->xvclk);

	return ret;
}

static void __max96722_power_off(struct max96722 *max96722)
{
	int ret;
	struct device *dev = &max96722->client->dev;

	if (!IS_ERR(max96722->pwdn_gpio))
		gpiod_set_value_cansleep(max96722->pwdn_gpio, 0);
	clk_disable_unprepare(max96722->xvclk);
	if (!IS_ERR(max96722->reset_gpio))
		gpiod_set_value_cansleep(max96722->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(max96722->pins_sleep)) {
		ret = pinctrl_select_state(max96722->pinctrl,
					   max96722->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	if (!IS_ERR(max96722->power_gpio))
		gpiod_set_value_cansleep(max96722->power_gpio, 0);

	regulator_bulk_disable(MAX96722_NUM_SUPPLIES, max96722->supplies);
}

static int max96722_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722 *max96722 = to_max96722(sd);

	return __max96722_power_on(max96722);
}

static int max96722_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722 *max96722 = to_max96722(sd);

	__max96722_power_off(max96722);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int max96722_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct max96722 *max96722 = to_max96722(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct max96722_mode *def_mode = &supported_modes[0];

	mutex_lock(&max96722->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MAX96722_MEDIA_BUS_FMT;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&max96722->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int max96722_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MAX96722_MEDIA_BUS_FMT)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;

	return 0;
}

static int max96722_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *config)
{
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = V4L2_MBUS_CSI2_4_LANE |
			V4L2_MBUS_CSI2_CHANNEL_0 |
			V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	return 0;
}

static int max96722_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct max96722 *max96722 = to_max96722(sd);

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.width = max96722->cur_mode->width;
		sel->r.top = 0;
		sel->r.height = max96722->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static const struct dev_pm_ops max96722_pm_ops = {
	SET_RUNTIME_PM_OPS(max96722_runtime_suspend,
			   max96722_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops max96722_internal_ops = {
	.open = max96722_open,
};
#endif

static const struct v4l2_subdev_core_ops max96722_core_ops = {
	.s_power = max96722_s_power,
	.ioctl = max96722_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = max96722_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops max96722_video_ops = {
	.s_stream = max96722_s_stream,
	.g_frame_interval = max96722_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops max96722_pad_ops = {
	.enum_mbus_code = max96722_enum_mbus_code,
	.enum_frame_size = max96722_enum_frame_sizes,
	.enum_frame_interval = max96722_enum_frame_interval,
	.get_fmt = max96722_get_fmt,
	.set_fmt = max96722_set_fmt,
	.get_selection = max96722_get_selection,
	.get_mbus_config = max96722_g_mbus_config,
};

static const struct v4l2_subdev_ops max96722_subdev_ops = {
	.core	= &max96722_core_ops,
	.video	= &max96722_video_ops,
	.pad	= &max96722_pad_ops,
};

static int max96722_initialize_controls(struct max96722 *max96722)
{
	const struct max96722_mode *mode;
	struct v4l2_ctrl_handler *handler;
	int ret;

	handler = &max96722->ctrl_handler;

	mode = max96722->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &max96722->mutex;

	max96722->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
			V4L2_CID_LINK_FREQ,
			1, 0, link_freq_items);

	max96722->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
			V4L2_CID_PIXEL_RATE,
			0, MAX96722_PIXEL_RATE,
			1, MAX96722_PIXEL_RATE);

	__v4l2_ctrl_s_ctrl(max96722->link_freq,
			   mode->link_freq_idx);

	if (handler->error) {
		ret = handler->error;
		dev_err(&max96722->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	max96722->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int max96722_check_sensor_id(struct max96722 *max96722,
				   struct i2c_client *client)
{
	struct device *dev = &max96722->client->dev;
	u32 id = 0;
	int ret;

	ret = max96722_read_reg(client, MAX96722_REG_CHIP_ID,
			       MAX96722_REG_VALUE_08BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%02x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected %02x sensor\n", id);

	return 0;
}

static int max96722_configure_regulators(struct max96722 *max96722)
{
	unsigned int i;

	for (i = 0; i < MAX96722_NUM_SUPPLIES; i++)
		max96722->supplies[i].supply = max96722_supply_names[i];

	return devm_regulator_bulk_get(&max96722->client->dev,
					MAX96722_NUM_SUPPLIES,
					max96722->supplies);
}

static int max96722_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct max96722 *max96722;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	max96722 = devm_kzalloc(dev, sizeof(*max96722), GFP_KERNEL);
	if (!max96722)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &max96722->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &max96722->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &max96722->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &max96722->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	max96722->client = client;
	max96722->cur_mode = &supported_modes[0];

	max96722->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->power_gpio))
		dev_warn(dev, "Failed to get power-gpios, maybe no use\n");

	max96722->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	max96722->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(max96722->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = max96722_configure_regulators(max96722);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	max96722->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(max96722->pinctrl)) {
		max96722->pins_default =
			pinctrl_lookup_state(max96722->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(max96722->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		max96722->pins_sleep =
			pinctrl_lookup_state(max96722->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(max96722->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	}

	mutex_init(&max96722->mutex);

	sd = &max96722->subdev;
	v4l2_i2c_subdev_init(sd, client, &max96722_subdev_ops);
	ret = max96722_initialize_controls(max96722);
	if (ret)
		goto err_destroy_mutex;

	ret = __max96722_power_on(max96722);
	if (ret)
		goto err_free_handler;

	ret = max96722_write_reg(max96722->client,
				 MAX96722_REMOTE_CTRL,
				 MAX96722_REG_VALUE_08BIT,
				 MAX96722_REMOTE_DISABLE);
	if (ret) {
		dev_err(dev, "disable i2c remote control error\n");
		goto err_power_off;
	}

	ret = max96722_check_sensor_id(max96722, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &max96722_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	max96722->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &max96722->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(max96722->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 max96722->module_index, facing,
		 MAX96722_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__max96722_power_off(max96722);
err_free_handler:
	v4l2_ctrl_handler_free(&max96722->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&max96722->mutex);

	return ret;
}

static int max96722_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct max96722 *max96722 = to_max96722(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&max96722->ctrl_handler);
	mutex_destroy(&max96722->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__max96722_power_off(max96722);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id max96722_of_match[] = {
	{ .compatible = "maxim,max96722" },
	{},
};
MODULE_DEVICE_TABLE(of, max96722_of_match);
#endif

static const struct i2c_device_id max96722_match_id[] = {
	{ "maxim,max96722", 0 },
	{},
};

static struct i2c_driver max96722_i2c_driver = {
	.driver = {
		.name = MAX96722_NAME,
		.pm = &max96722_pm_ops,
		.of_match_table = of_match_ptr(max96722_of_match),
	},
	.probe		= &max96722_probe,
	.remove		= &max96722_remove,
	.id_table	= max96722_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&max96722_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&max96722_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("Maxim max96722 deserializer driver");
MODULE_LICENSE("GPL");
