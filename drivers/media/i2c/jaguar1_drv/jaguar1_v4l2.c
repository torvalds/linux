// SPDX-License-Identifier: GPL-2.0
/*
 * jaguar1 driver
 * V0.0X01.0X00 first version.
 * V0.0X01.0X01 fix kernel5.10 compile error.
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
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-fwnode.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mfd/syscon.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <linux/rk-preisp.h>

#include "jaguar1_common.h"
#include "jaguar1_video.h"
#include "jaguar1_coax_protocol.h"
#include "jaguar1_motion.h"
#include "jaguar1_ioctl.h"
#include "jaguar1_video_eq.h"
#include "jaguar1_mipi.h"
#include "jaguar1_drv.h"
#include "jaguar1_v4l2.h"

#define DRIVER_VERSION				KERNEL_VERSION(0, 0x01, 0x1)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN			V4L2_CID_GAIN
#endif

#define JAGUAR1_XVCLK_FREQ			24000000
#define JAGUAR1_LINK_FREQ			320000000
#define JAGUAR1_LANES			4
#define JAGUAR1_BITS_PER_SAMPLE		8

/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define JAGUAR1_PIXEL_RATE \
	(JAGUAR1_LINK_FREQ * 2 / JAGUAR1_BITS_PER_SAMPLE * JAGUAR1_LANES)

#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP		"rockchip,camera_sleep"

#define OF_CAMERA_MODULE_REGULATORS		"rockchip,regulator-names"
#define OF_CAMERA_MODULE_REGULATOR_VOLTAGES	"rockchip,regulator-voltages"
#define RK_CAMERA_MODULE_DEFAULT_RECT		"rockchip,default_rect"

#define JAGUAR1_DEFAULT_WIDTH		1920
#define JAGUAR1_DEFAULT_HEIGHT		1080

#define JAGUAR1_NAME				"jaguar1"

/* #define FORCE_720P */

struct jaguar1_gpio {
	int pltfrm_gpio;
	const char *label;
	enum of_gpio_flags active_low;
};

struct jaguar1_regulator {
	struct regulator *regulator;
	u32 min_uV;
	u32 max_uV;
};

struct jaguar1_regulators {
	u32 cnt;
	struct jaguar1_regulator *regulator;
};

struct jaguar1_pixfmt {
	u32 code;
};

struct jaguar1_framesize {
	u16 width;
	u16 height;
	struct v4l2_fract max_fps;
	enum NC_VIVO_CH_FORMATDEF fmt_idx;
};

struct jaguar1_default_rect {
	unsigned int width;
	unsigned int height;
};

struct jaguar1 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*rst_gpio;
	struct gpio_desc	*rst2_gpio;
	struct gpio_desc	*pd_gpio;
	struct gpio_desc	*pd2_gpio;
	struct gpio_desc	*pwd_gpio;
	struct gpio_desc	*pwd2_gpio;

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad[PAD_MAX];
	struct v4l2_ctrl_handler ctrl_handler;
	struct mutex		mutex;
	bool			power_on;
	struct jaguar1_regulators regulators;

	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;

	struct v4l2_mbus_framefmt format;
	const struct jaguar1_framesize *frame_size;
	int streaming;
	struct jaguar1_default_rect defrect;
};

#define to_jaguar1(sd) container_of(sd, struct jaguar1, subdev)

static const struct jaguar1_framesize jaguar1_framesizes[] = {
#if defined CONFIG_VIDEO_ROCKCHIP_USBACM_CONTROL
	{
		.width		= 2560,
		.height		= 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.fmt_idx	= AHD20_720P_25P,
	}
#else
	{
		.width		= 1280,
		.height		= 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.fmt_idx	= AHD20_720P_25P_EX_Btype,
	},
	{
		.width		= 1920,
		.height		= 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.fmt_idx	= AHD20_1080P_25P,
	},
	{
		.width		= 2560,
		.height		= 1440,
		.max_fps = {
			.numerator = 10000,
			.denominator = 250000,
		},
		.fmt_idx	= AHD20_720P_25P,
	}
#endif
};

static const struct jaguar1_pixfmt jaguar1_formats[] = {
	{
		.code = MEDIA_BUS_FMT_UYVY8_2X8
	},
};

static const s64 link_freq_menu_items[] = {
	JAGUAR1_LINK_FREQ
};
static int __jaguar1_power_on(struct jaguar1 *jaguar1)
{
	u32 i;
	int ret;
	struct jaguar1_regulator *regulator;
	struct device *dev = &jaguar1->client->dev;

	if (!IS_ERR_OR_NULL(jaguar1->pins_default)) {
		ret = pinctrl_select_state(jaguar1->pinctrl,
					   jaguar1->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins. ret=%d\n", ret);
	}

	ret = clk_prepare_enable(jaguar1->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (jaguar1->regulators.regulator) {
		for (i = 0; i < jaguar1->regulators.cnt; i++) {
			regulator = jaguar1->regulators.regulator + i;
			if (IS_ERR(regulator->regulator))
				continue;
			regulator_set_voltage(
				regulator->regulator,
				regulator->min_uV,
				regulator->max_uV);
			if (regulator_enable(regulator->regulator)) {
				dev_err(dev,
					"regulator_enable failed!\n");
				goto disable_clk;
			}
		}
	}
	usleep_range(3000, 5000);

	if (!IS_ERR(jaguar1->pwd_gpio)) {
		gpiod_direction_output(jaguar1->pwd_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(jaguar1->pwd2_gpio)) {
		gpiod_direction_output(jaguar1->pwd2_gpio, 1);
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(jaguar1->pd_gpio)) {
		gpiod_direction_output(jaguar1->pd_gpio, 1);
		usleep_range(1500, 2000);
	}

	if (!IS_ERR(jaguar1->pd2_gpio)) {
		gpiod_direction_output(jaguar1->pd2_gpio, 1);
		usleep_range(1500, 2000);
	}

	if (!IS_ERR(jaguar1->rst_gpio)) {
		gpiod_direction_output(jaguar1->rst_gpio, 0);
		usleep_range(1500, 2000);
		gpiod_direction_output(jaguar1->rst_gpio, 1);
	}

	if (!IS_ERR(jaguar1->rst2_gpio)) {
		gpiod_direction_output(jaguar1->rst2_gpio, 0);
		usleep_range(1500, 2000);
		gpiod_direction_output(jaguar1->rst2_gpio, 1);
	}

	return 0;

disable_clk:
	clk_disable_unprepare(jaguar1->xvclk);

	return ret;
}

static void __jaguar1_power_off(struct jaguar1 *jaguar1)
{
	u32 i;
	int ret;
	struct jaguar1_regulator *regulator;
	struct device *dev = &jaguar1->client->dev;

	if (!IS_ERR(jaguar1->pd_gpio))
		gpiod_direction_output(jaguar1->pd_gpio, 0);

	if (!IS_ERR(jaguar1->pd2_gpio))
		gpiod_direction_output(jaguar1->pd2_gpio, 0);

	clk_disable_unprepare(jaguar1->xvclk);

	if (!IS_ERR(jaguar1->rst_gpio))
		gpiod_direction_output(jaguar1->rst_gpio, 0);

	if (!IS_ERR(jaguar1->rst2_gpio))
		gpiod_direction_output(jaguar1->rst2_gpio, 0);

	if (!IS_ERR(jaguar1->pwd_gpio))
		gpiod_direction_output(jaguar1->pwd_gpio, 0);

	if (!IS_ERR(jaguar1->pwd2_gpio))
		gpiod_direction_output(jaguar1->pwd2_gpio, 0);

	if (!IS_ERR_OR_NULL(jaguar1->pins_sleep)) {
		ret = pinctrl_select_state(jaguar1->pinctrl,
					   jaguar1->pins_sleep);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}

	if (jaguar1->regulators.regulator) {
		for (i = 0; i < jaguar1->regulators.cnt; i++) {
			regulator = jaguar1->regulators.regulator + i;
			if (IS_ERR(regulator->regulator))
				continue;
			regulator_disable(regulator->regulator);
		}
	}
}

static int jaguar1_power(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jaguar1 *jaguar1 = to_jaguar1(sd);
	int ret = 0;

	dev_dbg(&client->dev, "%s: on %d\n", __func__, on);
	mutex_lock(&jaguar1->mutex);

	/* If the power state is not modified - no work to do. */
	if (jaguar1->power_on == !!on)
		goto exit;

	if (on) {
		ret = __jaguar1_power_on(jaguar1);
		if (ret < 0)
			goto exit;

		jaguar1->power_on = true;
	} else {
		__jaguar1_power_off(jaguar1);
		jaguar1->power_on = false;
	}

exit:
	mutex_unlock(&jaguar1->mutex);

	return ret;
}

static int jaguar1_initialize_controls(struct jaguar1 *jaguar1)
{
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	int ret;

	handler = &jaguar1->ctrl_handler;
	ret = v4l2_ctrl_handler_init(handler, 2);
	if (ret)
		return ret;
	handler->lock = &jaguar1->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, JAGUAR1_PIXEL_RATE, 1, JAGUAR1_PIXEL_RATE);

	if (handler->error) {
		ret = handler->error;
		dev_err(&jaguar1->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	jaguar1->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}
static void jaguar1_get_default_format(struct jaguar1 *jaguar1)
{
	const struct jaguar1_framesize *fsize = &jaguar1_framesizes[0];
	const struct jaguar1_framesize *match = NULL;
	int i = ARRAY_SIZE(jaguar1_framesizes);
	unsigned int min_err = UINT_MAX;
	struct v4l2_mbus_framefmt *format = &jaguar1->format;
	struct jaguar1_default_rect *rect =  &jaguar1->defrect;

	while (i--) {
		unsigned int err = abs(fsize->width - rect->width)
				+ abs(fsize->height - rect->height);
		if (err < min_err) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match)
		match = &jaguar1_framesizes[0];

	format->width = match->width;
	format->height = match->height;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = jaguar1_formats[0].code;
	jaguar1->frame_size = match;
	format->field = V4L2_FIELD_NONE;
}

static int jaguar1_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jaguar1 *jaguar1 = to_jaguar1(sd);
	video_init_all video_init;
	enum NC_VIVO_CH_FORMATDEF fmt_idx;
	int ch;

	dev_dbg(&client->dev, "%s: on %d\n", __func__, on);
	mutex_lock(&jaguar1->mutex);
	on = !!on;

	if (jaguar1->streaming == on)
		goto unlock;

	if (on) {
		jaguar1_set_mclk(JAGUAR1_MCLK_1242MHZ);
		fmt_idx = jaguar1->frame_size->fmt_idx;
		for (ch = 0; ch < 4; ch++) {
			video_init.ch_param[ch].ch = ch;
			video_init.ch_param[ch].format = fmt_idx;
			video_init.ch_param[ch].input = SINGLE_ENDED;
			video_init.ch_param[ch].interface = YUV_422;
		}
		jaguar1_start(&video_init);
	} else {
		jaguar1_stop();
	}

	jaguar1->streaming = on;

unlock:
	mutex_unlock(&jaguar1->mutex);

	return 0;
}

static int jaguar1_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(jaguar1_formats))
		return -EINVAL;

	code->code = jaguar1_formats[code->index].code;

	return 0;
}

static int jaguar1_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int i = ARRAY_SIZE(jaguar1_formats);

	dev_dbg(&client->dev, "%s:\n", __func__);

	if (fse->index >= ARRAY_SIZE(jaguar1_framesizes))
		return -EINVAL;

	while (--i)
		if (fse->code == jaguar1_formats[i].code)
			break;

	fse->code = jaguar1_formats[i].code;

	fse->min_width  = jaguar1_framesizes[fse->index].width;
	fse->max_width  = fse->min_width;
	fse->max_height = jaguar1_framesizes[fse->index].height;
	fse->min_height = fse->max_height;

	return 0;
}


static int jaguar1_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				 struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->flags = V4L2_MBUS_CSI2_4_LANE |
		     V4L2_MBUS_CSI2_CHANNELS;

	return 0;
}

static int jaguar1_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct jaguar1 *jaguar1 = to_jaguar1(sd);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_get_try_format(sd, cfg, 0);
		mutex_lock(&jaguar1->mutex);
		fmt->format = *mf;
		mutex_unlock(&jaguar1->mutex);
		return 0;
#else
	return -ENOTTY;
#endif
	}

	mutex_lock(&jaguar1->mutex);
	fmt->format = jaguar1->format;
	mutex_unlock(&jaguar1->mutex);

	dev_dbg(&client->dev, "%s: %x %dx%d\n", __func__,
		jaguar1->format.code, jaguar1->format.width,
		jaguar1->format.height);

	return 0;
}

static void __jaguar1_try_frame_size(struct v4l2_mbus_framefmt *mf,
				    const struct jaguar1_framesize **size)
{
	const struct jaguar1_framesize *fsize = &jaguar1_framesizes[0];
	const struct jaguar1_framesize *match = NULL;
	int i = ARRAY_SIZE(jaguar1_framesizes);
	unsigned int min_err = UINT_MAX;

	while (i--) {
		unsigned int err = abs(fsize->width - mf->width)
				+ abs(fsize->height - mf->height);
		if (err < min_err) {
			min_err = err;
			match = fsize;
		}
		fsize++;
	}

	if (!match)
		match = &jaguar1_framesizes[0];

	mf->width  = match->width;
	mf->height = match->height;

	if (size)
		*size = match;
}

static int jaguar1_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	int index = ARRAY_SIZE(jaguar1_formats);
	struct v4l2_mbus_framefmt *mf = &fmt->format;
	const struct jaguar1_framesize *size = NULL;
	struct jaguar1 *jaguar1 = to_jaguar1(sd);
	int ret = 0;

	__jaguar1_try_frame_size(mf, &size);

	while (--index >= 0)
		if (jaguar1_formats[index].code == mf->code)
			break;

	if (index < 0)
		return -EINVAL;

	mf->colorspace = V4L2_COLORSPACE_SRGB;
	mf->code = jaguar1_formats[index].code;
	mf->field = V4L2_FIELD_NONE;

	mutex_lock(&jaguar1->mutex);

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		mf = v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
		*mf = fmt->format;
#else
		return -ENOTTY;
#endif
	} else {
		if (jaguar1->streaming) {
			mutex_unlock(&jaguar1->mutex);
			return -EBUSY;
		}

		jaguar1->frame_size = size;
		jaguar1->format = fmt->format;
	}

	mutex_unlock(&jaguar1->mutex);
	return ret;
}

static int jaguar1_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct jaguar1 *jaguar1 = to_jaguar1(sd);
	const struct jaguar1_framesize *size = jaguar1->frame_size;

	mutex_lock(&jaguar1->mutex);
	fi->interval = size->max_fps;
	mutex_unlock(&jaguar1->mutex);

	return 0;
}

static void jaguar1_get_module_inf(struct jaguar1 *jaguar1,
				   struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, JAGUAR1_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, jaguar1->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, jaguar1->len_name, sizeof(inf->base.lens));
}

static long jaguar1_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct jaguar1 *jaguar1 = to_jaguar1(sd);
	long ret = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		jaguar1_get_module_inf(jaguar1, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		*(int *)arg = RKMODULE_START_STREAM_FRONT;
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long jaguar1_compat_ioctl32(struct v4l2_subdev *sd,
				   unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	int *seq;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = jaguar1_ioctl(sd, cmd, inf);
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
			ret = jaguar1_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_START_STREAM_SEQ:
		seq = kzalloc(sizeof(*seq), GFP_KERNEL);
		if (!seq) {
			ret = -ENOMEM;
			return ret;
		}

		ret = jaguar1_ioctl(sd, cmd, seq);
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

static int jaguar1_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jaguar1 *jaguar1 = to_jaguar1(sd);

	return __jaguar1_power_on(jaguar1);
}

static int jaguar1_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jaguar1 *jaguar1 = to_jaguar1(sd);

	__jaguar1_power_off(jaguar1);

	return 0;
}

static const struct dev_pm_ops jaguar1_pm_ops = {
	SET_RUNTIME_PM_OPS(jaguar1_runtime_suspend,
			   jaguar1_runtime_resume, NULL)
};

static const struct v4l2_subdev_video_ops jaguar1_video_ops = {
	.s_stream = jaguar1_stream,
	.g_frame_interval = jaguar1_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops jaguar1_subdev_pad_ops = {
	.enum_mbus_code = jaguar1_enum_mbus_code,
	.enum_frame_size = jaguar1_enum_frame_sizes,
	.get_fmt = jaguar1_get_fmt,
	.set_fmt = jaguar1_set_fmt,
	.get_mbus_config = jaguar1_g_mbus_config,
};

static const struct v4l2_subdev_core_ops jaguar1_core_ops = {
	.s_power = jaguar1_power,
	.ioctl = jaguar1_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = jaguar1_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_ops jaguar1_subdev_ops = {
	.core = &jaguar1_core_ops,
	.video = &jaguar1_video_ops,
	.pad   = &jaguar1_subdev_pad_ops,
};

static int jaguar1_analyze_dts(struct jaguar1 *jaguar1)
{
	int ret;
	int elem_size, elem_index;
	const char *str = "";
	struct property *prop;
	struct jaguar1_regulator *regulator;
	struct device *dev = &jaguar1->client->dev;
	struct device_node *np = of_node_get(dev->of_node);

	jaguar1->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(jaguar1->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}
	ret = clk_set_rate(jaguar1->xvclk, JAGUAR1_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(jaguar1->xvclk) != JAGUAR1_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");

	jaguar1->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(jaguar1->pinctrl)) {
		jaguar1->pins_default =
			pinctrl_lookup_state(jaguar1->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(jaguar1->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		jaguar1->pins_sleep =
			pinctrl_lookup_state(jaguar1->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(jaguar1->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	elem_size = of_property_count_elems_of_size(
		np,
		OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
		sizeof(u32));
	prop = of_find_property(
		np,
		OF_CAMERA_MODULE_REGULATORS,
		NULL);
	if (elem_size > 0 && !IS_ERR_OR_NULL(prop)) {
		jaguar1->regulators.regulator =
			devm_kzalloc(&jaguar1->client->dev,
				     elem_size * sizeof(struct jaguar1_regulator),
				     GFP_KERNEL);
		if (!jaguar1->regulators.regulator)
			dev_err(dev, "could not malloc jaguar1_regulator\n");

		jaguar1->regulators.cnt = elem_size;

		str = NULL;
		elem_index = 0;
		regulator = jaguar1->regulators.regulator;
		if (regulator) {
			do {
				str = of_prop_next_string(prop, str);
				if (!str) {
					dev_err(dev, "%s is not match %s in dts\n",
						OF_CAMERA_MODULE_REGULATORS,
						OF_CAMERA_MODULE_REGULATOR_VOLTAGES);
					break;
				}
				regulator->regulator =
					devm_regulator_get_optional(dev, str);
				if (IS_ERR(regulator->regulator))
					dev_err(dev, "devm_regulator_get %s failed\n",
						str);
				of_property_read_u32_index(
					np,
					OF_CAMERA_MODULE_REGULATOR_VOLTAGES,
					elem_index++,
					&regulator->min_uV);
				regulator->max_uV = regulator->min_uV;
				regulator++;
			} while (--elem_size);
		}
	}
	if (of_property_read_u32_array(np,
		RK_CAMERA_MODULE_DEFAULT_RECT,
		(unsigned int *)&jaguar1->defrect, 2)) {
		jaguar1->defrect.width = JAGUAR1_DEFAULT_WIDTH;
		jaguar1->defrect.height = JAGUAR1_DEFAULT_HEIGHT;
		dev_warn(dev,
			"can not get module %s from dts, use default wxh(%dx%d)!\n",
			RK_CAMERA_MODULE_DEFAULT_RECT,
			JAGUAR1_DEFAULT_WIDTH, JAGUAR1_DEFAULT_HEIGHT);
	} else {
		dev_info(dev,
			"get module %s from dts, wxh(%dx%d)!\n",
			RK_CAMERA_MODULE_DEFAULT_RECT,
			jaguar1->defrect.width,
			jaguar1->defrect.height);
	}

	jaguar1->pd_gpio = devm_gpiod_get(dev, "pd", GPIOD_OUT_LOW);
	if (IS_ERR(jaguar1->pd_gpio))
		dev_warn(dev, "can not find pd-gpios, error %ld\n",
			 PTR_ERR(jaguar1->pd_gpio));

	jaguar1->pd2_gpio = devm_gpiod_get(dev, "pd2", GPIOD_OUT_LOW);
	if (IS_ERR(jaguar1->pd2_gpio))
		dev_warn(dev, "can not find pd2-gpios, error %ld\n",
			 PTR_ERR(jaguar1->pd2_gpio));

	jaguar1->rst_gpio = devm_gpiod_get(dev, "rst", GPIOD_OUT_LOW);
	if (IS_ERR(jaguar1->rst_gpio))
		dev_warn(dev, "can not find rst-gpios, error %ld\n",
			 PTR_ERR(jaguar1->rst_gpio));

	jaguar1->rst2_gpio = devm_gpiod_get(dev, "rst2", GPIOD_OUT_LOW);
	if (IS_ERR(jaguar1->rst2_gpio))
		dev_warn(dev, "can not find rst2-gpios, error %ld\n",
			 PTR_ERR(jaguar1->rst2_gpio));

	jaguar1->pwd_gpio = devm_gpiod_get(dev, "pwd", GPIOD_OUT_HIGH);
	if (IS_ERR(jaguar1->pwd_gpio))
		dev_warn(dev, "can not find pwd-gpios, error %ld\n",
			 PTR_ERR(jaguar1->pwd_gpio));

	jaguar1->pwd2_gpio = devm_gpiod_get(dev, "pwd2", GPIOD_OUT_HIGH);
	if (IS_ERR(jaguar1->pwd2_gpio))
		dev_warn(dev, "can not find pwd2-gpios, error %ld\n",
			 PTR_ERR(jaguar1->pwd2_gpio));

	return 0;
}

static int jaguar1_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct jaguar1 *jaguar1;
	struct v4l2_subdev *sd;
	__maybe_unused char facing[2];
	int ret, index;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	jaguar1 = devm_kzalloc(dev, sizeof(*jaguar1), GFP_KERNEL);
	if (!jaguar1)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &jaguar1->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &jaguar1->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &jaguar1->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &jaguar1->len_name);
	if (ret) {
		dev_err(dev, "could not get %s!\n", RKMODULE_CAMERA_LENS_NAME);
		return -EINVAL;
	}

	jaguar1->client = client;

	ret = jaguar1_analyze_dts(jaguar1);
	if (ret) {
		dev_err(dev, "Failed to analyze dts\n");
		return ret;
	}

	mutex_init(&jaguar1->mutex);
	jaguar1_get_default_format(jaguar1);

	sd = &jaguar1->subdev;
	v4l2_i2c_subdev_init(sd, client, &jaguar1_subdev_ops);
	ret = jaguar1_initialize_controls(jaguar1);

	__jaguar1_power_on(jaguar1);
	ret = jaguar1_init(i2c_adapter_id(client->adapter));
	if (ret) {
		dev_err(dev, "Failed to init jaguar1\n");
		__jaguar1_power_off(jaguar1);
		mutex_destroy(&jaguar1->mutex);

		return ret;
	}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_VIDEO_ROCKCHIP_USBACM_CONTROL)
	__jaguar1_power_off(jaguar1);
	mutex_destroy(&jaguar1->mutex);

	return 0;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
	for (index = 0; index < PAD_MAX; index++)
		jaguar1->pad[index].flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, PAD_MAX, jaguar1->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(jaguar1->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 jaguar1->module_index, facing,
		 JAGUAR1_NAME, dev_name(sd->dev));

	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	v4l2_info(sd, "%s found @ 0x%x (%s)\n", client->name,
			client->addr << 1, client->adapter->name);
	return 0;

err_power_off:
	__jaguar1_power_off(jaguar1);
err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&jaguar1->mutex);

	return ret;
}

static int jaguar1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct jaguar1 *jaguar1 = to_jaguar1(sd);

	jaguar1_exit();
	v4l2_ctrl_handler_free(&jaguar1->ctrl_handler);
	mutex_destroy(&jaguar1->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__jaguar1_power_off(jaguar1);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id jaguar1_of_match[] = {
	{ .compatible = "jaguar1-v4l2" },
	{},
};
MODULE_DEVICE_TABLE(of, jaguar1_of_match);
#endif

static const struct i2c_device_id jaguar1_match_id[] = {
	{ "jaguar1-v4l2", 0 },
	{ },
};

static struct i2c_driver jaguar1_i2c_driver = {
	.driver = {
		.name = JAGUAR1_NAME,
		.pm = &jaguar1_pm_ops,
		.of_match_table = of_match_ptr(jaguar1_of_match),
	},
	.probe		= &jaguar1_probe,
	.remove		= &jaguar1_remove,
	.id_table	= jaguar1_match_id,
};

int nvp6324_sensor_mod_init(void)
{
	return i2c_add_driver(&jaguar1_i2c_driver);
}

#ifndef CONFIG_VIDEO_REVERSE_IMAGE
device_initcall_sync(nvp6324_sensor_mod_init);
#endif

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&jaguar1_i2c_driver);
}

module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("jaguar1 sensor driver");
MODULE_LICENSE("GPL v2");
