// SPDX-License-Identifier: GPL-2.0
/*
 * tp2855 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */

#define DEBUG
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
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include <linux/platform_device.h>
#include <linux/input.h>

#define DRIVER_VERSION				KERNEL_VERSION(0, 0x01, 0x0)
#define TP2855_TEST_PATTERN 		0
#define TP2855_XVCLK_FREQ			27000000
#define TP2855_LINK_FREQ_297M		(297000000UL >> 1)
#define TP2855_LINK_FREQ_594M		(594000000UL >> 1)
#define TP2855_LANES				4
#define TP2855_BITS_PER_SAMPLE		8
#define TP2855_NAME					"tp2855"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP		"rockchip,camera_sleep"

enum{
	CH_1=0,
	CH_2=1,
	CH_3=2,
	CH_4=3,
	CH_ALL=4,
	MIPI_PAGE=8,
};

enum{
	STD_TVI, //TVI
	STD_HDA, //AHD
};

struct regval {
	u8 addr;
	u8 val;
};

struct tp2855_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 mipi_freq_idx;
	u32 bpp;
	const struct regval *global_reg_list;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
	u32 channel_reso[PAD_MAX];
};

struct tp2855 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*power_gpio;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct mutex		mutex;
	bool			power_on;
	struct tp2855_mode *cur_mode;

	u32			module_index;
	u32			cfg_num;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			lost_video_status;
	struct task_struct *detect_thread;

	int streaming;
};


#define to_tp2855(sd) container_of(sd, struct tp2855, subdev)

static __maybe_unused const struct regval common_setting_297M_720p_25fps_regs[] = {
	{0x40, 0x04},
	{0xf5, 0x00},
	{0x02, 0x42},
	{0x07, 0xc0},
	{0x0b, 0xc0},
	{0x0c, 0x13},
	{0x0d, 0x50},
	{0x15, 0x13},
	{0x16, 0x15},
	{0x17, 0x00},
	{0x18, 0x19},
	{0x19, 0xd0},
	{0x1a, 0x25},
	{0x1c, 0x07},
	{0x1d, 0xbc},
	{0x20, 0x30},
	{0x21, 0x84},
	{0x22, 0x36},
	{0x23, 0x3c},
#if TP2855_TEST_PATTERN
	{0x2a, 0x3c}, //vi test
#endif
	{0x2b, 0x60},
	{0x2c, 0x0a},
	{0x2d, 0x30},
	{0x2e, 0x70},
	{0x30, 0x48},
	{0x31, 0xbb},
	{0x32, 0x2e},
	{0x33, 0x90},
	{0x35, 0x25},
	{0x38, 0x00},
	{0x39, 0x18},

	{0x40, 0x08},
	{0x01, 0xf0},
	{0x02, 0x01},
	{0x08, 0x0f},
	{0x20, 0x44},
	{0x34, 0xe4},
	{0x14, 0x44},
	{0x15, 0x0d},
	{0x25, 0x04},
	{0x26, 0x03},
	{0x27, 0x09},
	{0x29, 0x02},
	{0x33, 0x07},
	{0x33, 0x00},
	{0x14, 0xc4},
	{0x14, 0x44},

	// {0x23, 0x02}, //vi test ok
	// {0x23, 0x00},
};

static __maybe_unused const struct regval common_setting_594M_1080p_25fps_regs[] = {
	{0x40, 0x04},
	{0xf5, 0x00},
	{0x02, 0x40},
	{0x07, 0xc0},
	{0x0b, 0xc0},
	{0x0c, 0x03},
	{0x0d, 0x50},
	{0x15, 0x03},
	{0x16, 0xd2},
	{0x17, 0x80},
	{0x18, 0x29},
	{0x19, 0x38},
	{0x1a, 0x47},
	{0x1c, 0x0a},
	{0x1d, 0x50},
	{0x20, 0x30},
	{0x21, 0x84},
	{0x22, 0x36},
	{0x23, 0x3c},
#if TP2855_TEST_PATTERN
	{0x2a, 0x3c}, //vi test
#endif
	{0x2b, 0x60},
	{0x2c, 0x0a},
	{0x2d, 0x30},
	{0x2e, 0x70},
	{0x30, 0x48},
	{0x31, 0xbb},
	{0x32, 0x2e},
	{0x33, 0x90},
	{0x35, 0x05},
	{0x38, 0x00},
	{0x39, 0x1C},

	{0x40, 0x08},
	{0x01, 0xf0},
	{0x02, 0x01},
	{0x08, 0x0f},
	{0x20, 0x44},
	{0x34, 0xe4},
	{0x15, 0x0C},
	{0x25, 0x08},
	{0x26, 0x06},
	{0x27, 0x11},
	{0x29, 0x0a},
	{0x33, 0x07},
	{0x33, 0x00},
	{0x14, 0x33},
	{0x14, 0xb3},
	{0x14, 0x33},
	// {0x23, 0x02}, //vi test ok
	// {0x23, 0x00},
};

static struct tp2855_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.global_reg_list = common_setting_594M_1080p_25fps_regs,
		.mipi_freq_idx = 0,
		.bpp = 8,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	},
	{
		.bus_fmt = MEDIA_BUS_FMT_UYVY8_2X8,
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.global_reg_list = common_setting_297M_720p_25fps_regs,
		.mipi_freq_idx = 1,
		.bpp = 8,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_2,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_3,
	}
};

static const s64 link_freq_items[] = {
	TP2855_LINK_FREQ_594M,
	TP2855_LINK_FREQ_297M,
};

/* sensor register write */
static int tp2855_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[2];
	int ret;

	buf[0] = reg & 0xFF;
	buf[1] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0) {
		usleep_range(300, 400);
		return 0;
	}

	dev_err(&client->dev,
		"tp2855 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int tp2855_write_array(struct i2c_client *client,
				  const struct regval *regs, int size)
{
	int i, ret = 0; 

	i = 0;
	while (i < size) {
		ret = tp2855_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

/* sensor register read */
static int tp2855_read_reg(struct i2c_client *client, u8 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[1];
	int ret;

	buf[0] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev, "tp2855 read reg(0x%x) failed !\n", reg);

	return ret;
}

static int tp2855_get_reso_dist(const struct tp2855_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct tp2855_mode *
tp2855_find_best_fit(struct tp2855 *tp2855,
                      struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < tp2855->cfg_num; i++) {
		dist = tp2855_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
			supported_modes[i].bus_fmt == framefmt->code) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int tp2855_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	const struct tp2855_mode *mode;
	u64 pixel_rate;

	mutex_lock(&tp2855->mutex);

	mode = tp2855_find_best_fit(tp2855, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	fmt->format.colorspace = V4L2_COLORSPACE_SRGB;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&tp2855->mutex);
		return -ENOTTY;
#endif
	} else {
		__v4l2_ctrl_s_ctrl(tp2855->link_freq, mode->mipi_freq_idx);
		pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * TP2855_LANES;
		__v4l2_ctrl_s_ctrl_int64(tp2855->pixel_rate, pixel_rate);
		dev_dbg(&tp2855->client->dev, "mipi_freq_idx %d\n", mode->mipi_freq_idx);
		dev_dbg(&tp2855->client->dev, "pixel_rate %lld\n", pixel_rate);
	}

	mutex_unlock(&tp2855->mutex);
	return 0;
}

static int tp2855_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	struct i2c_client *client = tp2855->client;
	const struct tp2855_mode *mode = tp2855->cur_mode;

	mutex_lock(&tp2855->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&tp2855->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && fmt->pad >= PAD0)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&tp2855->mutex);

	dev_dbg(&client->dev, "%s: %x %dx%d\n",
		__func__, fmt->format.code,
		fmt->format.width, fmt->format.height);

	return 0;
}


static int tp2855_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct tp2855 *tp2855 = to_tp2855(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = tp2855->cur_mode->bus_fmt;

	return 0;
}

static int tp2855_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	struct i2c_client *client = tp2855->client;

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= tp2855->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;
	return 0;
}

static int tp2855_g_mbus_config(struct v4l2_subdev *sd,
				 struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_4_LANE |
		     V4L2_MBUS_CSI2_CHANNELS;

	return 0;
}

static void tp2855_get_module_inf(struct tp2855 *tp2855,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, TP2855_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, tp2855->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, tp2855->len_name, sizeof(inf->base.lens));
}

static long tp2855_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		tp2855_get_module_inf(tp2855, (struct rkmodule_inf *)arg);
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long tp2855_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = tp2855_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
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
			ret = tp2855_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int detect_thread_function(void *data)
{
	struct tp2855 *tp2855 = (struct tp2855 *) data;
	struct i2c_client *client = tp2855->client;
	u8 detect_status = 0, reg26_val = 0;
	bool lost_video = false;
	tp2855->lost_video_status = true;
	while (!kthread_should_stop()) {
		if (tp2855->power_on) {
			tp2855_write_reg(client, 0x40, 0x04);
			tp2855_read_reg(client, 0x01, &detect_status);
			tp2855_read_reg(client, 0x26, &reg26_val);
			lost_video = (detect_status & 0x80) ? true : false;
			if (tp2855->lost_video_status != lost_video) {
				if (lost_video) {
					tp2855_write_reg(client, 0x26, (reg26_val & 0xfe));
				} else {
					tp2855_write_reg(client, 0x26, (reg26_val | 0x01));
				}
				tp2855->lost_video_status = lost_video;
				tp2855_read_reg(client, 0x26, &reg26_val);
				dev_err(&client->dev, "tp2855 detect video lost status %d reg26_val %x\n",
						lost_video, reg26_val);
			}
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(msecs_to_jiffies(200));
	}
	return 0;
}

static int __maybe_unused detect_thread_start(struct tp2855 *tp2855) {
	int ret = 0;
	struct i2c_client *client = tp2855->client;
	tp2855->detect_thread = kthread_create(detect_thread_function,
                                   tp2855, "tp2855_kthread");
	if (IS_ERR(tp2855->detect_thread)) {
		dev_err(&client->dev, "kthread_create tp2855_kthread failed\n");
		ret = PTR_ERR(tp2855->detect_thread);
		tp2855->detect_thread = NULL;
		return ret;
	}
	wake_up_process(tp2855->detect_thread);
	return ret;
}

static int __maybe_unused detect_thread_stop(struct tp2855 *tp2855) {
	if (tp2855->detect_thread)
        kthread_stop(tp2855->detect_thread);
	tp2855->detect_thread = NULL;
	return 0;
}

static int __tp2855_start_stream(struct tp2855 *tp2855)
{
	int ret;
	int array_size = 0;
	struct i2c_client *client = tp2855->client;

    if (tp2855->cur_mode->global_reg_list == common_setting_594M_1080p_25fps_regs) {
		array_size = ARRAY_SIZE(common_setting_594M_1080p_25fps_regs);
	} else if (tp2855->cur_mode->global_reg_list == common_setting_297M_720p_25fps_regs) {
		array_size = ARRAY_SIZE(common_setting_297M_720p_25fps_regs);
	} else {
		return -1;
	}

	ret = tp2855_write_array(tp2855->client,
		tp2855->cur_mode->global_reg_list, array_size);
	if (ret) {
		dev_err(&client->dev, "__tp2855_start_stream global_reg_list faild");
		return ret;
	}

	detect_thread_start(tp2855);

	return 0;
}

static int __tp2855_stop_stream(struct tp2855 *tp2855)
{
	struct i2c_client *client = tp2855->client;

	tp2855_write_reg(client, 0x40, 0x08);
	tp2855_write_reg(client, 0x23, 0x02);
	detect_thread_stop(tp2855);

	return 0;
}

static int tp2855_stream(struct v4l2_subdev *sd, int on)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	struct i2c_client *client = tp2855->client;

	dev_dbg(&client->dev, "s_stream: %d. %dx%d\n", on,
			tp2855->cur_mode->width,
			tp2855->cur_mode->height);

	mutex_lock(&tp2855->mutex);
	on = !!on;
	if (tp2855->streaming == on)
		goto unlock;

	if (on) {
		__tp2855_start_stream(tp2855);
	} else {
		__tp2855_stop_stream(tp2855);
	}

	tp2855->streaming = on;

unlock:
	mutex_unlock(&tp2855->mutex);

	return 0;
}

static int tp2855_power(struct v4l2_subdev *sd, int on)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	struct i2c_client *client = tp2855->client;
	int ret = 0;

	mutex_lock(&tp2855->mutex);

	/* If the power state is not modified - no work to do. */
	if (tp2855->power_on == !!on)
		goto exit;

	dev_dbg(&client->dev, "%s: on %d\n", __func__, on);

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto exit;
		}
		tp2855->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		tp2855->power_on = false;
	}

exit:
	mutex_unlock(&tp2855->mutex);

	return ret;
}

static int __tp2855_power_on(struct tp2855 *tp2855)
{
	int ret;
	struct device *dev = &tp2855->client->dev;

	dev_dbg(dev, "%s\n", __func__);

	if (!IS_ERR_OR_NULL(tp2855->pins_default)) {
		ret = pinctrl_select_state(tp2855->pinctrl,
					   tp2855->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins. ret=%d\n", ret);
	}

	if (!IS_ERR(tp2855->power_gpio)) {
		gpiod_set_value_cansleep(tp2855->power_gpio, 1);
		usleep_range(25*1000, 30*1000);
	}

	usleep_range(1500, 2000);

	ret = clk_set_rate(tp2855->xvclk, TP2855_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate\n");
	if (clk_get_rate(tp2855->xvclk) != TP2855_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched\n");
	ret = clk_prepare_enable(tp2855->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		goto err_clk;
	}

	if (!IS_ERR(tp2855->reset_gpio)) {
		gpiod_set_value_cansleep(tp2855->reset_gpio, 1);
		usleep_range(10*1000, 20*1000);
		gpiod_set_value_cansleep(tp2855->reset_gpio, 0);
		usleep_range(10*1000, 20*1000);
	}

	usleep_range(10*1000, 20*1000);

	return 0;

err_clk:
	if (!IS_ERR(tp2855->reset_gpio))
		gpiod_set_value_cansleep(tp2855->reset_gpio, 0);

	if (!IS_ERR_OR_NULL(tp2855->pins_sleep))
		pinctrl_select_state(tp2855->pinctrl, tp2855->pins_sleep);

	return ret;
}

static void __tp2855_power_off(struct tp2855 *tp2855)
{
	int ret;
	struct device *dev = &tp2855->client->dev;

	dev_dbg(dev, "%s\n", __func__);

	if (!IS_ERR(tp2855->reset_gpio))
		gpiod_set_value_cansleep(tp2855->reset_gpio, 0);
	clk_disable_unprepare(tp2855->xvclk);

	if (!IS_ERR_OR_NULL(tp2855->pins_sleep)) {
		ret = pinctrl_select_state(tp2855->pinctrl,
					   tp2855->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}

	if (!IS_ERR(tp2855->power_gpio))
		gpiod_set_value_cansleep(tp2855->power_gpio, 0);
}

static int tp2855_initialize_controls(struct tp2855 *tp2855)
{
	const struct tp2855_mode *mode;
	struct v4l2_ctrl_handler *handler;
	u64 pixel_rate;
	int ret;

	handler = &tp2855->ctrl_handler;
	mode = tp2855->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &tp2855->mutex;

	tp2855->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
				V4L2_CID_LINK_FREQ,
				ARRAY_SIZE(link_freq_items) - 1, 0,
				link_freq_items);
	__v4l2_ctrl_s_ctrl(tp2855->link_freq, mode->mipi_freq_idx);

	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	pixel_rate = (u32)link_freq_items[mode->mipi_freq_idx] / mode->bpp * 2 * TP2855_LANES;
	tp2855->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
		V4L2_CID_PIXEL_RATE, 0, pixel_rate,
		1, pixel_rate);


	if (handler->error) {
		ret = handler->error;
		dev_err(&tp2855->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	tp2855->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int tp2855_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tp2855 *tp2855 = to_tp2855(sd);

	return __tp2855_power_on(tp2855);
}

static int tp2855_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tp2855 *tp2855 = to_tp2855(sd);

	__tp2855_power_off(tp2855);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int tp2855_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct tp2855 *tp2855 = to_tp2855(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct tp2855_mode *def_mode = &supported_modes[0];

	dev_dbg(&tp2855->client->dev, "%s\n", __func__);

	mutex_lock(&tp2855->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&tp2855->mutex);
	/* No crop or compose */

	return 0;
}
#endif

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops tp2855_internal_ops = {
	.open = tp2855_open,
};
#endif

static const struct v4l2_subdev_video_ops tp2855_video_ops = {
	.s_stream = tp2855_stream,
	.g_mbus_config = tp2855_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops tp2855_subdev_pad_ops = {
	.enum_mbus_code = tp2855_enum_mbus_code,
	.enum_frame_size = tp2855_enum_frame_sizes,
	.get_fmt = tp2855_get_fmt,
	.set_fmt = tp2855_set_fmt,
};

static const struct v4l2_subdev_core_ops tp2855_core_ops = {
	.s_power = tp2855_power,
	.ioctl = tp2855_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = tp2855_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops tp2855_subdev_ops = {
	.core = &tp2855_core_ops,
	.video = &tp2855_video_ops,
	.pad   = &tp2855_subdev_pad_ops,
};

static int check_chip_id(struct i2c_client *client){
	struct device *dev = &client->dev;
	unsigned char chip_id = 0xFF;
	tp2855_read_reg(client, 0x34, &chip_id);
	dev_err(dev, "chip_id : 0x%2x\n", chip_id);
	if (chip_id != 0x0) {
		return -1;
	}
	return 0;
}

static int tp2855_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct tp2855 *tp2855;
	struct v4l2_subdev *sd;
	__maybe_unused char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	tp2855 = devm_kzalloc(dev, sizeof(*tp2855), GFP_KERNEL);
	if (!tp2855)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &tp2855->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &tp2855->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &tp2855->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &tp2855->len_name);
	if (ret) {
		dev_err(dev, "could not get %s!\n", RKMODULE_CAMERA_LENS_NAME);
		return -EINVAL;
	}

	tp2855->client = client;
	tp2855->cur_mode = &supported_modes[0];

	tp2855->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(tp2855->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	tp2855->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(tp2855->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	tp2855->power_gpio = devm_gpiod_get(dev, "power", GPIOD_OUT_LOW);
	if (IS_ERR(tp2855->power_gpio))
		dev_warn(dev, "Failed to get power-gpios\n");

	tp2855->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(tp2855->pinctrl)) {
		tp2855->pins_default =
			pinctrl_lookup_state(tp2855->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(tp2855->pins_default))
			dev_info(dev, "could not get default pinstate\n");

		tp2855->pins_sleep =
			pinctrl_lookup_state(tp2855->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(tp2855->pins_sleep))
			dev_info(dev, "could not get sleep pinstate\n");
	} else {
		dev_info(dev, "no pinctrl\n");
	}

	mutex_init(&tp2855->mutex);

	sd = &tp2855->subdev;
	v4l2_i2c_subdev_init(sd, client, &tp2855_subdev_ops);
	ret = tp2855_initialize_controls(tp2855);
	if (ret) {
		dev_err(dev, "Failed to initialize controls tp2855\n");
		goto err_destroy_mutex;
	}

	ret = __tp2855_power_on(tp2855);
	if (ret) {
		dev_err(dev, "Failed to power on tp2855\n");
		goto err_free_handler;
	}

	ret = check_chip_id(client);
	if (ret) {
		dev_err(dev, "Failed to check senosr id\n");
		goto err_free_handler;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &tp2855_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	tp2855->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &tp2855->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(tp2855->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 tp2855->module_index, facing,
		 TP2855_NAME, dev_name(sd->dev));

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
	__tp2855_power_off(tp2855);
err_free_handler:
	v4l2_ctrl_handler_free(&tp2855->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&tp2855->mutex);

	return ret;
}

static int tp2855_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tp2855 *tp2855 = to_tp2855(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&tp2855->ctrl_handler);
	mutex_destroy(&tp2855->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__tp2855_power_off(tp2855);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct dev_pm_ops tp2855_pm_ops = {
	SET_RUNTIME_PM_OPS(tp2855_runtime_suspend,
			   tp2855_runtime_resume, NULL)
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id tp2855_of_match[] = {
	{ .compatible = "tp2855" },
	{},
};
MODULE_DEVICE_TABLE(of, tp2855_of_match);
#endif

static const struct i2c_device_id tp2855_match_id[] = {
	{ "tp2855", 0 },
	{ },
};

static struct i2c_driver tp2855_i2c_driver = {
	.driver = {
		.name = TP2855_NAME,
		.pm = &tp2855_pm_ops,
		.of_match_table = of_match_ptr(tp2855_of_match),
	},
	.probe		= &tp2855_probe,
	.remove		= &tp2855_remove,
	.id_table	= tp2855_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&tp2855_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&tp2855_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_AUTHOR("Vicent Chi <vicent.chi@rock-chips.com>");
MODULE_DESCRIPTION("tp2855 sensor driver");
MODULE_LICENSE("GPL v2");