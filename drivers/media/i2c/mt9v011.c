/*
 * mt9v011 -Micron 1/4-Inch VGA Digital Image Sensor
 *
 * Copyright (c) 2009 Mauro Carvalho Chehab
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/div64.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/i2c/mt9v011.h>

MODULE_DESCRIPTION("Micron mt9v011 sensor driver");
MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

#define R00_MT9V011_CHIP_VERSION	0x00
#define R01_MT9V011_ROWSTART		0x01
#define R02_MT9V011_COLSTART		0x02
#define R03_MT9V011_HEIGHT		0x03
#define R04_MT9V011_WIDTH		0x04
#define R05_MT9V011_HBLANK		0x05
#define R06_MT9V011_VBLANK		0x06
#define R07_MT9V011_OUT_CTRL		0x07
#define R09_MT9V011_SHUTTER_WIDTH	0x09
#define R0A_MT9V011_CLK_SPEED		0x0a
#define R0B_MT9V011_RESTART		0x0b
#define R0C_MT9V011_SHUTTER_DELAY	0x0c
#define R0D_MT9V011_RESET		0x0d
#define R1E_MT9V011_DIGITAL_ZOOM	0x1e
#define R20_MT9V011_READ_MODE		0x20
#define R2B_MT9V011_GREEN_1_GAIN	0x2b
#define R2C_MT9V011_BLUE_GAIN		0x2c
#define R2D_MT9V011_RED_GAIN		0x2d
#define R2E_MT9V011_GREEN_2_GAIN	0x2e
#define R35_MT9V011_GLOBAL_GAIN		0x35
#define RF1_MT9V011_CHIP_ENABLE		0xf1

#define MT9V011_VERSION			0x8232
#define MT9V011_REV_B_VERSION		0x8243

struct mt9v011 {
	struct v4l2_subdev sd;
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_pad pad;
#endif
	struct v4l2_ctrl_handler ctrls;
	unsigned width, height;
	unsigned xtal;
	unsigned hflip:1;
	unsigned vflip:1;

	u16 global_gain, exposure;
	s16 red_bal, blue_bal;
};

static inline struct mt9v011 *to_mt9v011(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9v011, sd);
}

static int mt9v011_read(struct v4l2_subdev *sd, unsigned char addr)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	__be16 buffer;
	int rc, val;

	rc = i2c_master_send(c, &addr, 1);
	if (rc != 1)
		v4l2_dbg(0, debug, sd,
			 "i2c i/o error: rc == %d (should be 1)\n", rc);

	msleep(10);

	rc = i2c_master_recv(c, (char *)&buffer, 2);
	if (rc != 2)
		v4l2_dbg(0, debug, sd,
			 "i2c i/o error: rc == %d (should be 2)\n", rc);

	val = be16_to_cpu(buffer);

	v4l2_dbg(2, debug, sd, "mt9v011: read 0x%02x = 0x%04x\n", addr, val);

	return val;
}

static void mt9v011_write(struct v4l2_subdev *sd, unsigned char addr,
				 u16 value)
{
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	unsigned char buffer[3];
	int rc;

	buffer[0] = addr;
	buffer[1] = value >> 8;
	buffer[2] = value & 0xff;

	v4l2_dbg(2, debug, sd,
		 "mt9v011: writing 0x%02x 0x%04x\n", buffer[0], value);
	rc = i2c_master_send(c, buffer, 3);
	if (rc != 3)
		v4l2_dbg(0, debug, sd,
			 "i2c i/o error: rc == %d (should be 3)\n", rc);
}


struct i2c_reg_value {
	unsigned char reg;
	u16           value;
};

/*
 * Values used at the original driver
 * Some values are marked as Reserved at the datasheet
 */
static const struct i2c_reg_value mt9v011_init_default[] = {
		{ R0D_MT9V011_RESET, 0x0001 },
		{ R0D_MT9V011_RESET, 0x0000 },

		{ R0C_MT9V011_SHUTTER_DELAY, 0x0000 },
		{ R09_MT9V011_SHUTTER_WIDTH, 0x1fc },

		{ R0A_MT9V011_CLK_SPEED, 0x0000 },
		{ R1E_MT9V011_DIGITAL_ZOOM,  0x0000 },

		{ R07_MT9V011_OUT_CTRL, 0x0002 },	/* chip enable */
};


static u16 calc_mt9v011_gain(s16 lineargain)
{

	u16 digitalgain = 0;
	u16 analogmult = 0;
	u16 analoginit = 0;

	if (lineargain < 0)
		lineargain = 0;

	/* recommended minimum */
	lineargain += 0x0020;

	if (lineargain > 2047)
		lineargain = 2047;

	if (lineargain > 1023) {
		digitalgain = 3;
		analogmult = 3;
		analoginit = lineargain / 16;
	} else if (lineargain > 511) {
		digitalgain = 1;
		analogmult = 3;
		analoginit = lineargain / 8;
	} else if (lineargain > 255) {
		analogmult = 3;
		analoginit = lineargain / 4;
	} else if (lineargain > 127) {
		analogmult = 1;
		analoginit = lineargain / 2;
	} else
		analoginit = lineargain;

	return analoginit + (analogmult << 7) + (digitalgain << 9);

}

static void set_balance(struct v4l2_subdev *sd)
{
	struct mt9v011 *core = to_mt9v011(sd);
	u16 green_gain, blue_gain, red_gain;
	u16 exposure;
	s16 bal;

	exposure = core->exposure;

	green_gain = calc_mt9v011_gain(core->global_gain);

	bal = core->global_gain;
	bal += (core->blue_bal * core->global_gain / (1 << 7));
	blue_gain = calc_mt9v011_gain(bal);

	bal = core->global_gain;
	bal += (core->red_bal * core->global_gain / (1 << 7));
	red_gain = calc_mt9v011_gain(bal);

	mt9v011_write(sd, R2B_MT9V011_GREEN_1_GAIN, green_gain);
	mt9v011_write(sd, R2E_MT9V011_GREEN_2_GAIN, green_gain);
	mt9v011_write(sd, R2C_MT9V011_BLUE_GAIN, blue_gain);
	mt9v011_write(sd, R2D_MT9V011_RED_GAIN, red_gain);
	mt9v011_write(sd, R09_MT9V011_SHUTTER_WIDTH, exposure);
}

static void calc_fps(struct v4l2_subdev *sd, u32 *numerator, u32 *denominator)
{
	struct mt9v011 *core = to_mt9v011(sd);
	unsigned height, width, hblank, vblank, speed;
	unsigned row_time, t_time;
	u64 frames_per_ms;
	unsigned tmp;

	height = mt9v011_read(sd, R03_MT9V011_HEIGHT);
	width = mt9v011_read(sd, R04_MT9V011_WIDTH);
	hblank = mt9v011_read(sd, R05_MT9V011_HBLANK);
	vblank = mt9v011_read(sd, R06_MT9V011_VBLANK);
	speed = mt9v011_read(sd, R0A_MT9V011_CLK_SPEED);

	row_time = (width + 113 + hblank) * (speed + 2);
	t_time = row_time * (height + vblank + 1);

	frames_per_ms = core->xtal * 1000l;
	do_div(frames_per_ms, t_time);
	tmp = frames_per_ms;

	v4l2_dbg(1, debug, sd, "Programmed to %u.%03u fps (%d pixel clcks)\n",
		tmp / 1000, tmp % 1000, t_time);

	if (numerator && denominator) {
		*numerator = 1000;
		*denominator = (u32)frames_per_ms;
	}
}

static u16 calc_speed(struct v4l2_subdev *sd, u32 numerator, u32 denominator)
{
	struct mt9v011 *core = to_mt9v011(sd);
	unsigned height, width, hblank, vblank;
	unsigned row_time, line_time;
	u64 t_time, speed;

	/* Avoid bogus calculus */
	if (!numerator || !denominator)
		return 0;

	height = mt9v011_read(sd, R03_MT9V011_HEIGHT);
	width = mt9v011_read(sd, R04_MT9V011_WIDTH);
	hblank = mt9v011_read(sd, R05_MT9V011_HBLANK);
	vblank = mt9v011_read(sd, R06_MT9V011_VBLANK);

	row_time = width + 113 + hblank;
	line_time = height + vblank + 1;

	t_time = core->xtal * ((u64)numerator);
	/* round to the closest value */
	t_time += denominator / 2;
	do_div(t_time, denominator);

	speed = t_time;
	do_div(speed, row_time * line_time);

	/* Avoid having a negative value for speed */
	if (speed < 2)
		speed = 0;
	else
		speed -= 2;

	/* Avoid speed overflow */
	if (speed > 15)
		return 15;

	return (u16)speed;
}

static void set_res(struct v4l2_subdev *sd)
{
	struct mt9v011 *core = to_mt9v011(sd);
	unsigned vstart, hstart;

	/*
	 * The mt9v011 doesn't have scaling. So, in order to select the desired
	 * resolution, we're cropping at the middle of the sensor.
	 * hblank and vblank should be adjusted, in order to warrant that
	 * we'll preserve the line timings for 30 fps, no matter what resolution
	 * is selected.
	 * NOTE: datasheet says that width (and height) should be filled with
	 * width-1. However, this doesn't work, since one pixel per line will
	 * be missing.
	 */

	hstart = 20 + (640 - core->width) / 2;
	mt9v011_write(sd, R02_MT9V011_COLSTART, hstart);
	mt9v011_write(sd, R04_MT9V011_WIDTH, core->width);
	mt9v011_write(sd, R05_MT9V011_HBLANK, 771 - core->width);

	vstart = 8 + (480 - core->height) / 2;
	mt9v011_write(sd, R01_MT9V011_ROWSTART, vstart);
	mt9v011_write(sd, R03_MT9V011_HEIGHT, core->height);
	mt9v011_write(sd, R06_MT9V011_VBLANK, 508 - core->height);

	calc_fps(sd, NULL, NULL);
};

static void set_read_mode(struct v4l2_subdev *sd)
{
	struct mt9v011 *core = to_mt9v011(sd);
	unsigned mode = 0x1000;

	if (core->hflip)
		mode |= 0x4000;

	if (core->vflip)
		mode |= 0x8000;

	mt9v011_write(sd, R20_MT9V011_READ_MODE, mode);
}

static int mt9v011_reset(struct v4l2_subdev *sd, u32 val)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt9v011_init_default); i++)
		mt9v011_write(sd, mt9v011_init_default[i].reg,
			       mt9v011_init_default[i].value);

	set_balance(sd);
	set_res(sd);
	set_read_mode(sd);

	return 0;
}

static int mt9v011_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->pad || code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG8_1X8;
	return 0;
}

static int mt9v011_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct mt9v011 *core = to_mt9v011(sd);

	if (format->pad || fmt->code != MEDIA_BUS_FMT_SGRBG8_1X8)
		return -EINVAL;

	v4l_bound_align_image(&fmt->width, 48, 639, 1,
			      &fmt->height, 32, 480, 1, 0);
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		core->width = fmt->width;
		core->height = fmt->height;

		set_res(sd);
	} else {
		cfg->try_fmt = *fmt;
	}

	return 0;
}

static int mt9v011_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(cp, 0, sizeof(struct v4l2_captureparm));
	cp->capability = V4L2_CAP_TIMEPERFRAME;
	calc_fps(sd,
		 &cp->timeperframe.numerator,
		 &cp->timeperframe.denominator);

	return 0;
}

static int mt9v011_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	struct v4l2_captureparm *cp = &parms->parm.capture;
	struct v4l2_fract *tpf = &cp->timeperframe;
	u16 speed;

	if (parms->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (cp->extendedmode != 0)
		return -EINVAL;

	speed = calc_speed(sd, tpf->numerator, tpf->denominator);

	mt9v011_write(sd, R0A_MT9V011_CLK_SPEED, speed);
	v4l2_dbg(1, debug, sd, "Setting speed to %d\n", speed);

	/* Recalculate and update fps info */
	calc_fps(sd, &tpf->numerator, &tpf->denominator);

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int mt9v011_g_register(struct v4l2_subdev *sd,
			      struct v4l2_dbg_register *reg)
{
	reg->val = mt9v011_read(sd, reg->reg & 0xff);
	reg->size = 2;

	return 0;
}

static int mt9v011_s_register(struct v4l2_subdev *sd,
			      const struct v4l2_dbg_register *reg)
{
	mt9v011_write(sd, reg->reg & 0xff, reg->val & 0xffff);

	return 0;
}
#endif

static int mt9v011_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9v011 *core =
		container_of(ctrl->handler, struct mt9v011, ctrls);
	struct v4l2_subdev *sd = &core->sd;

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		core->global_gain = ctrl->val;
		break;
	case V4L2_CID_EXPOSURE:
		core->exposure = ctrl->val;
		break;
	case V4L2_CID_RED_BALANCE:
		core->red_bal = ctrl->val;
		break;
	case V4L2_CID_BLUE_BALANCE:
		core->blue_bal = ctrl->val;
		break;
	case V4L2_CID_HFLIP:
		core->hflip = ctrl->val;
		set_read_mode(sd);
		return 0;
	case V4L2_CID_VFLIP:
		core->vflip = ctrl->val;
		set_read_mode(sd);
		return 0;
	default:
		return -EINVAL;
	}

	set_balance(sd);
	return 0;
}

static const struct v4l2_ctrl_ops mt9v011_ctrl_ops = {
	.s_ctrl = mt9v011_s_ctrl,
};

static const struct v4l2_subdev_core_ops mt9v011_core_ops = {
	.reset = mt9v011_reset,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = mt9v011_g_register,
	.s_register = mt9v011_s_register,
#endif
};

static const struct v4l2_subdev_video_ops mt9v011_video_ops = {
	.g_parm = mt9v011_g_parm,
	.s_parm = mt9v011_s_parm,
};

static const struct v4l2_subdev_pad_ops mt9v011_pad_ops = {
	.enum_mbus_code = mt9v011_enum_mbus_code,
	.set_fmt = mt9v011_set_fmt,
};

static const struct v4l2_subdev_ops mt9v011_ops = {
	.core  = &mt9v011_core_ops,
	.video = &mt9v011_video_ops,
	.pad   = &mt9v011_pad_ops,
};


/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/

static int mt9v011_probe(struct i2c_client *c,
			 const struct i2c_device_id *id)
{
	u16 version;
	struct mt9v011 *core;
	struct v4l2_subdev *sd;
#ifdef CONFIG_MEDIA_CONTROLLER
	int ret;
#endif

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(c->adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	core = devm_kzalloc(&c->dev, sizeof(struct mt9v011), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	sd = &core->sd;
	v4l2_i2c_subdev_init(sd, c, &mt9v011_ops);

#ifdef CONFIG_MEDIA_CONTROLLER
	core->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ret = media_entity_pads_init(&sd->entity, 1, &core->pad);
	if (ret < 0)
		return ret;
#endif

	/* Check if the sensor is really a MT9V011 */
	version = mt9v011_read(sd, R00_MT9V011_CHIP_VERSION);
	if ((version != MT9V011_VERSION) &&
	    (version != MT9V011_REV_B_VERSION)) {
		v4l2_info(sd, "*** unknown micron chip detected (0x%04x).\n",
			  version);
		return -EINVAL;
	}

	v4l2_ctrl_handler_init(&core->ctrls, 5);
	v4l2_ctrl_new_std(&core->ctrls, &mt9v011_ctrl_ops,
			  V4L2_CID_GAIN, 0, (1 << 12) - 1 - 0x20, 1, 0x20);
	v4l2_ctrl_new_std(&core->ctrls, &mt9v011_ctrl_ops,
			  V4L2_CID_EXPOSURE, 0, 2047, 1, 0x01fc);
	v4l2_ctrl_new_std(&core->ctrls, &mt9v011_ctrl_ops,
			  V4L2_CID_RED_BALANCE, -(1 << 9), (1 << 9) - 1, 1, 0);
	v4l2_ctrl_new_std(&core->ctrls, &mt9v011_ctrl_ops,
			  V4L2_CID_BLUE_BALANCE, -(1 << 9), (1 << 9) - 1, 1, 0);
	v4l2_ctrl_new_std(&core->ctrls, &mt9v011_ctrl_ops,
			  V4L2_CID_HFLIP, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&core->ctrls, &mt9v011_ctrl_ops,
			  V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (core->ctrls.error) {
		int ret = core->ctrls.error;

		v4l2_err(sd, "control initialization error %d\n", ret);
		v4l2_ctrl_handler_free(&core->ctrls);
		return ret;
	}
	core->sd.ctrl_handler = &core->ctrls;

	core->global_gain = 0x0024;
	core->exposure = 0x01fc;
	core->width  = 640;
	core->height = 480;
	core->xtal = 27000000;	/* Hz */

	if (c->dev.platform_data) {
		struct mt9v011_platform_data *pdata = c->dev.platform_data;

		core->xtal = pdata->xtal;
		v4l2_dbg(1, debug, sd, "xtal set to %d.%03d MHz\n",
			core->xtal / 1000000, (core->xtal / 1000) % 1000);
	}

	v4l_info(c, "chip found @ 0x%02x (%s - chip version 0x%04x)\n",
		 c->addr << 1, c->adapter->name, version);

	return 0;
}

static int mt9v011_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);
	struct mt9v011 *core = to_mt9v011(sd);

	v4l2_dbg(1, debug, sd,
		"mt9v011.c: removing mt9v011 adapter on address 0x%x\n",
		c->addr << 1);

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&core->ctrls);

	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id mt9v011_id[] = {
	{ "mt9v011", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mt9v011_id);

static struct i2c_driver mt9v011_driver = {
	.driver = {
		.name	= "mt9v011",
	},
	.probe		= mt9v011_probe,
	.remove		= mt9v011_remove,
	.id_table	= mt9v011_id,
};

module_i2c_driver(mt9v011_driver);
