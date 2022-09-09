// SPDX-License-Identifier: GPL-2.0-only
/*
 * et8ek8_driver.c
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 *          Tuukka Toivonen <tuukkat76@gmail.com>
 *          Pavel Machek <pavel@ucw.cz>
 *
 * Based on code from Toni Leinonen <toni.leinonen@offcode.fi>.
 *
 * This driver is based on the Micron MT9T012 camera imager driver
 * (C) Texas Instruments.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <linux/v4l2-mediabus.h>

#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

#include "et8ek8_reg.h"

#define ET8EK8_NAME		"et8ek8"
#define ET8EK8_PRIV_MEM_SIZE	128
#define ET8EK8_MAX_MSG		8

struct et8ek8_sensor {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	struct gpio_desc *reset;
	struct regulator *vana;
	struct clk *ext_clk;
	u32 xclk_freq;

	u16 version;

	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *pixel_rate;
	struct et8ek8_reglist *current_reglist;

	u8 priv_mem[ET8EK8_PRIV_MEM_SIZE];

	struct mutex power_lock;
	int power_count;
};

#define to_et8ek8_sensor(sd)	container_of(sd, struct et8ek8_sensor, subdev)

enum et8ek8_versions {
	ET8EK8_REV_1 = 0x0001,
	ET8EK8_REV_2,
};

/*
 * This table describes what should be written to the sensor register
 * for each gain value. The gain(index in the table) is in terms of
 * 0.1EV, i.e. 10 indexes in the table give 2 time more gain [0] in
 * the *analog gain, [1] in the digital gain
 *
 * Analog gain [dB] = 20*log10(regvalue/32); 0x20..0x100
 */
static struct et8ek8_gain {
	u16 analog;
	u16 digital;
} const et8ek8_gain_table[] = {
	{ 32,    0},  /* x1 */
	{ 34,    0},
	{ 37,    0},
	{ 39,    0},
	{ 42,    0},
	{ 45,    0},
	{ 49,    0},
	{ 52,    0},
	{ 56,    0},
	{ 60,    0},
	{ 64,    0},  /* x2 */
	{ 69,    0},
	{ 74,    0},
	{ 79,    0},
	{ 84,    0},
	{ 91,    0},
	{ 97,    0},
	{104,    0},
	{111,    0},
	{119,    0},
	{128,    0},  /* x4 */
	{137,    0},
	{147,    0},
	{158,    0},
	{169,    0},
	{181,    0},
	{194,    0},
	{208,    0},
	{223,    0},
	{239,    0},
	{256,    0},  /* x8 */
	{256,   73},
	{256,  152},
	{256,  236},
	{256,  327},
	{256,  424},
	{256,  528},
	{256,  639},
	{256,  758},
	{256,  886},
	{256, 1023},  /* x16 */
};

/* Register definitions */
#define REG_REVISION_NUMBER_L	0x1200
#define REG_REVISION_NUMBER_H	0x1201

#define PRIV_MEM_START_REG	0x0008
#define PRIV_MEM_WIN_SIZE	8

#define ET8EK8_I2C_DELAY	3	/* msec delay b/w accesses */

#define USE_CRC			1

/*
 * Register access helpers
 *
 * Read a 8/16/32-bit i2c register.  The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int et8ek8_i2c_read_reg(struct i2c_client *client, u16 data_length,
			       u16 reg, u32 *val)
{
	int r;
	struct i2c_msg msg;
	unsigned char data[4];

	if (!client->adapter)
		return -ENODEV;
	if (data_length != ET8EK8_REG_8BIT && data_length != ET8EK8_REG_16BIT)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 2;
	msg.buf = data;

	/* high byte goes out first */
	data[0] = (u8) (reg >> 8);
	data[1] = (u8) (reg & 0xff);
	r = i2c_transfer(client->adapter, &msg, 1);
	if (r < 0)
		goto err;

	msg.len = data_length;
	msg.flags = I2C_M_RD;
	r = i2c_transfer(client->adapter, &msg, 1);
	if (r < 0)
		goto err;

	*val = 0;
	/* high byte comes first */
	if (data_length == ET8EK8_REG_8BIT)
		*val = data[0];
	else
		*val = (data[1] << 8) + data[0];

	return 0;

err:
	dev_err(&client->dev, "read from offset 0x%x error %d\n", reg, r);

	return r;
}

static void et8ek8_i2c_create_msg(struct i2c_client *client, u16 len, u16 reg,
				  u32 val, struct i2c_msg *msg,
				  unsigned char *buf)
{
	msg->addr = client->addr;
	msg->flags = 0; /* Write */
	msg->len = 2 + len;
	msg->buf = buf;

	/* high byte goes out first */
	buf[0] = (u8) (reg >> 8);
	buf[1] = (u8) (reg & 0xff);

	switch (len) {
	case ET8EK8_REG_8BIT:
		buf[2] = (u8) (val) & 0xff;
		break;
	case ET8EK8_REG_16BIT:
		buf[2] = (u8) (val) & 0xff;
		buf[3] = (u8) (val >> 8) & 0xff;
		break;
	default:
		WARN_ONCE(1, ET8EK8_NAME ": %s: invalid message length.\n",
			  __func__);
	}
}

/*
 * A buffered write method that puts the wanted register write
 * commands in smaller number of message lists and passes the lists to
 * the i2c framework
 */
static int et8ek8_i2c_buffered_write_regs(struct i2c_client *client,
					  const struct et8ek8_reg *wnext,
					  int cnt)
{
	struct i2c_msg msg[ET8EK8_MAX_MSG];
	unsigned char data[ET8EK8_MAX_MSG][6];
	int wcnt = 0;
	u16 reg, data_length;
	u32 val;
	int rval;

	/* Create new write messages for all writes */
	while (wcnt < cnt) {
		data_length = wnext->type;
		reg = wnext->reg;
		val = wnext->val;
		wnext++;

		et8ek8_i2c_create_msg(client, data_length, reg,
				    val, &msg[wcnt], &data[wcnt][0]);

		/* Update write count */
		wcnt++;

		if (wcnt < ET8EK8_MAX_MSG)
			continue;

		rval = i2c_transfer(client->adapter, msg, wcnt);
		if (rval < 0)
			return rval;

		cnt -= wcnt;
		wcnt = 0;
	}

	rval = i2c_transfer(client->adapter, msg, wcnt);

	return rval < 0 ? rval : 0;
}

/*
 * Write a list of registers to i2c device.
 *
 * The list of registers is terminated by ET8EK8_REG_TERM.
 * Returns zero if successful, or non-zero otherwise.
 */
static int et8ek8_i2c_write_regs(struct i2c_client *client,
				 const struct et8ek8_reg *regs)
{
	int r, cnt = 0;
	const struct et8ek8_reg *next;

	if (!client->adapter)
		return -ENODEV;

	if (!regs)
		return -EINVAL;

	/* Initialize list pointers to the start of the list */
	next = regs;

	do {
		/*
		 * We have to go through the list to figure out how
		 * many regular writes we have in a row
		 */
		while (next->type != ET8EK8_REG_TERM &&
		       next->type != ET8EK8_REG_DELAY) {
			/*
			 * Here we check that the actual length fields
			 * are valid
			 */
			if (WARN(next->type != ET8EK8_REG_8BIT &&
				 next->type != ET8EK8_REG_16BIT,
				 "Invalid type = %d", next->type)) {
				return -EINVAL;
			}
			/*
			 * Increment count of successive writes and
			 * read pointer
			 */
			cnt++;
			next++;
		}

		/* Now we start writing ... */
		r = et8ek8_i2c_buffered_write_regs(client, regs, cnt);

		/* ... and then check that everything was OK */
		if (r < 0) {
			dev_err(&client->dev, "i2c transfer error!\n");
			return r;
		}

		/*
		 * If we ran into a sleep statement when going through
		 * the list, this is where we snooze for the required time
		 */
		if (next->type == ET8EK8_REG_DELAY) {
			msleep(next->val);
			/*
			 * ZZZ ...
			 * Update list pointers and cnt and start over ...
			 */
			next++;
			regs = next;
			cnt = 0;
		}
	} while (next->type != ET8EK8_REG_TERM);

	return 0;
}

/*
 * Write to a 8/16-bit register.
 * Returns zero if successful, or non-zero otherwise.
 */
static int et8ek8_i2c_write_reg(struct i2c_client *client, u16 data_length,
				u16 reg, u32 val)
{
	int r;
	struct i2c_msg msg;
	unsigned char data[6];

	if (!client->adapter)
		return -ENODEV;
	if (data_length != ET8EK8_REG_8BIT && data_length != ET8EK8_REG_16BIT)
		return -EINVAL;

	et8ek8_i2c_create_msg(client, data_length, reg, val, &msg, data);

	r = i2c_transfer(client->adapter, &msg, 1);
	if (r < 0) {
		dev_err(&client->dev,
			"wrote 0x%x to offset 0x%x error %d\n", val, reg, r);
		return r;
	}

	return 0;
}

static struct et8ek8_reglist *et8ek8_reglist_find_type(
		struct et8ek8_meta_reglist *meta,
		u16 type)
{
	struct et8ek8_reglist **next = &meta->reglist[0].ptr;

	while (*next) {
		if ((*next)->type == type)
			return *next;

		next++;
	}

	return NULL;
}

static int et8ek8_i2c_reglist_find_write(struct i2c_client *client,
					 struct et8ek8_meta_reglist *meta,
					 u16 type)
{
	struct et8ek8_reglist *reglist;

	reglist = et8ek8_reglist_find_type(meta, type);
	if (!reglist)
		return -EINVAL;

	return et8ek8_i2c_write_regs(client, reglist->regs);
}

static struct et8ek8_reglist **et8ek8_reglist_first(
		struct et8ek8_meta_reglist *meta)
{
	return &meta->reglist[0].ptr;
}

static void et8ek8_reglist_to_mbus(const struct et8ek8_reglist *reglist,
				   struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = reglist->mode.window_width;
	fmt->height = reglist->mode.window_height;
	fmt->code = reglist->mode.bus_format;
}

static struct et8ek8_reglist *et8ek8_reglist_find_mode_fmt(
		struct et8ek8_meta_reglist *meta,
		struct v4l2_mbus_framefmt *fmt)
{
	struct et8ek8_reglist **list = et8ek8_reglist_first(meta);
	struct et8ek8_reglist *best_match = NULL;
	struct et8ek8_reglist *best_other = NULL;
	struct v4l2_mbus_framefmt format;
	unsigned int max_dist_match = (unsigned int)-1;
	unsigned int max_dist_other = (unsigned int)-1;

	/*
	 * Find the mode with the closest image size. The distance between
	 * image sizes is the size in pixels of the non-overlapping regions
	 * between the requested size and the frame-specified size.
	 *
	 * Store both the closest mode that matches the requested format, and
	 * the closest mode for all other formats. The best match is returned
	 * if found, otherwise the best mode with a non-matching format is
	 * returned.
	 */
	for (; *list; list++) {
		unsigned int dist;

		if ((*list)->type != ET8EK8_REGLIST_MODE)
			continue;

		et8ek8_reglist_to_mbus(*list, &format);

		dist = min(fmt->width, format.width)
		     * min(fmt->height, format.height);
		dist = format.width * format.height
		     + fmt->width * fmt->height - 2 * dist;


		if (fmt->code == format.code) {
			if (dist < max_dist_match || !best_match) {
				best_match = *list;
				max_dist_match = dist;
			}
		} else {
			if (dist < max_dist_other || !best_other) {
				best_other = *list;
				max_dist_other = dist;
			}
		}
	}

	return best_match ? best_match : best_other;
}

#define TIMEPERFRAME_AVG_FPS(t)						\
	(((t).denominator + ((t).numerator >> 1)) / (t).numerator)

static struct et8ek8_reglist *et8ek8_reglist_find_mode_ival(
		struct et8ek8_meta_reglist *meta,
		struct et8ek8_reglist *current_reglist,
		struct v4l2_fract *timeperframe)
{
	int fps = TIMEPERFRAME_AVG_FPS(*timeperframe);
	struct et8ek8_reglist **list = et8ek8_reglist_first(meta);
	struct et8ek8_mode *current_mode = &current_reglist->mode;

	for (; *list; list++) {
		struct et8ek8_mode *mode = &(*list)->mode;

		if ((*list)->type != ET8EK8_REGLIST_MODE)
			continue;

		if (mode->window_width != current_mode->window_width ||
		    mode->window_height != current_mode->window_height)
			continue;

		if (TIMEPERFRAME_AVG_FPS(mode->timeperframe) == fps)
			return *list;
	}

	return NULL;
}

static int et8ek8_reglist_cmp(const void *a, const void *b)
{
	const struct et8ek8_reglist **list1 = (const struct et8ek8_reglist **)a,
		**list2 = (const struct et8ek8_reglist **)b;

	/* Put real modes in the beginning. */
	if ((*list1)->type == ET8EK8_REGLIST_MODE &&
	    (*list2)->type != ET8EK8_REGLIST_MODE)
		return -1;
	if ((*list1)->type != ET8EK8_REGLIST_MODE &&
	    (*list2)->type == ET8EK8_REGLIST_MODE)
		return 1;

	/* Descending width. */
	if ((*list1)->mode.window_width > (*list2)->mode.window_width)
		return -1;
	if ((*list1)->mode.window_width < (*list2)->mode.window_width)
		return 1;

	if ((*list1)->mode.window_height > (*list2)->mode.window_height)
		return -1;
	if ((*list1)->mode.window_height < (*list2)->mode.window_height)
		return 1;

	return 0;
}

static int et8ek8_reglist_import(struct i2c_client *client,
				 struct et8ek8_meta_reglist *meta)
{
	int nlists = 0, i;

	dev_info(&client->dev, "meta_reglist version %s\n", meta->version);

	while (meta->reglist[nlists].ptr)
		nlists++;

	if (!nlists)
		return -EINVAL;

	sort(&meta->reglist[0].ptr, nlists, sizeof(meta->reglist[0].ptr),
	     et8ek8_reglist_cmp, NULL);

	i = nlists;
	nlists = 0;

	while (i--) {
		struct et8ek8_reglist *list;

		list = meta->reglist[nlists].ptr;

		dev_dbg(&client->dev,
		       "%s: type %d\tw %d\th %d\tfmt %x\tival %d/%d\tptr %p\n",
		       __func__,
		       list->type,
		       list->mode.window_width, list->mode.window_height,
		       list->mode.bus_format,
		       list->mode.timeperframe.numerator,
		       list->mode.timeperframe.denominator,
		       (void *)meta->reglist[nlists].ptr);

		nlists++;
	}

	return 0;
}

/* Called to change the V4L2 gain control value. This function
 * rounds and clamps the given value and updates the V4L2 control value.
 * If power is on, also updates the sensor analog and digital gains.
 * gain is in 0.1 EV (exposure value) units.
 */
static int et8ek8_set_gain(struct et8ek8_sensor *sensor, s32 gain)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	struct et8ek8_gain new;
	int r;

	new = et8ek8_gain_table[gain];

	/* FIXME: optimise I2C writes! */
	r = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT,
				0x124a, new.analog >> 8);
	if (r)
		return r;
	r = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT,
				0x1249, new.analog & 0xff);
	if (r)
		return r;

	r = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT,
				0x124d, new.digital >> 8);
	if (r)
		return r;
	r = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT,
				0x124c, new.digital & 0xff);

	return r;
}

static int et8ek8_set_test_pattern(struct et8ek8_sensor *sensor, s32 mode)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);
	int cbh_mode, cbv_mode, tp_mode, din_sw, r1420, rval;

	/* Values for normal mode */
	cbh_mode = 0;
	cbv_mode = 0;
	tp_mode  = 0;
	din_sw   = 0x00;
	r1420    = 0xF0;

	if (mode) {
		/* Test pattern mode */
		if (mode < 5) {
			cbh_mode = 1;
			cbv_mode = 1;
			tp_mode  = mode + 3;
		} else {
			cbh_mode = 0;
			cbv_mode = 0;
			tp_mode  = mode - 4 + 3;
		}

		din_sw   = 0x01;
		r1420    = 0xE0;
	}

	rval = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x111B,
				    tp_mode << 4);
	if (rval)
		return rval;

	rval = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x1121,
				    cbh_mode << 7);
	if (rval)
		return rval;

	rval = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x1124,
				    cbv_mode << 7);
	if (rval)
		return rval;

	rval = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x112C, din_sw);
	if (rval)
		return rval;

	return et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x1420, r1420);
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

static int et8ek8_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct et8ek8_sensor *sensor =
		container_of(ctrl->handler, struct et8ek8_sensor, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_GAIN:
		return et8ek8_set_gain(sensor, ctrl->val);

	case V4L2_CID_EXPOSURE:
	{
		struct i2c_client *client =
			v4l2_get_subdevdata(&sensor->subdev);

		return et8ek8_i2c_write_reg(client, ET8EK8_REG_16BIT, 0x1243,
					    ctrl->val);
	}

	case V4L2_CID_TEST_PATTERN:
		return et8ek8_set_test_pattern(sensor, ctrl->val);

	case V4L2_CID_PIXEL_RATE:
		return 0;

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops et8ek8_ctrl_ops = {
	.s_ctrl = et8ek8_set_ctrl,
};

static const char * const et8ek8_test_pattern_menu[] = {
	"Normal",
	"Vertical colorbar",
	"Horizontal colorbar",
	"Scale",
	"Ramp",
	"Small vertical colorbar",
	"Small horizontal colorbar",
	"Small scale",
	"Small ramp",
};

static int et8ek8_init_controls(struct et8ek8_sensor *sensor)
{
	s32 max_rows;

	v4l2_ctrl_handler_init(&sensor->ctrl_handler, 4);

	/* V4L2_CID_GAIN */
	v4l2_ctrl_new_std(&sensor->ctrl_handler, &et8ek8_ctrl_ops,
			  V4L2_CID_GAIN, 0, ARRAY_SIZE(et8ek8_gain_table) - 1,
			  1, 0);

	max_rows = sensor->current_reglist->mode.max_exp;
	{
		u32 min = 1, max = max_rows;

		sensor->exposure =
			v4l2_ctrl_new_std(&sensor->ctrl_handler,
					  &et8ek8_ctrl_ops, V4L2_CID_EXPOSURE,
					  min, max, min, max);
	}

	/* V4L2_CID_PIXEL_RATE */
	sensor->pixel_rate =
		v4l2_ctrl_new_std(&sensor->ctrl_handler, &et8ek8_ctrl_ops,
		V4L2_CID_PIXEL_RATE, 1, INT_MAX, 1, 1);

	/* V4L2_CID_TEST_PATTERN */
	v4l2_ctrl_new_std_menu_items(&sensor->ctrl_handler,
				     &et8ek8_ctrl_ops, V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(et8ek8_test_pattern_menu) - 1,
				     0, 0, et8ek8_test_pattern_menu);

	if (sensor->ctrl_handler.error)
		return sensor->ctrl_handler.error;

	sensor->subdev.ctrl_handler = &sensor->ctrl_handler;

	return 0;
}

static void et8ek8_update_controls(struct et8ek8_sensor *sensor)
{
	struct v4l2_ctrl *ctrl;
	struct et8ek8_mode *mode = &sensor->current_reglist->mode;

	u32 min, max, pixel_rate;
	static const int S = 8;

	ctrl = sensor->exposure;

	min = 1;
	max = mode->max_exp;

	/*
	 * Calculate average pixel clock per line. Assume buffers can spread
	 * the data over horizontal blanking time. Rounding upwards.
	 * Formula taken from stock Nokia N900 kernel.
	 */
	pixel_rate = ((mode->pixel_clock + (1 << S) - 1) >> S) + mode->width;
	pixel_rate = mode->window_width * (pixel_rate - 1) / mode->width;

	__v4l2_ctrl_modify_range(ctrl, min, max, min, max);
	__v4l2_ctrl_s_ctrl_int64(sensor->pixel_rate, pixel_rate << S);
}

static int et8ek8_configure(struct et8ek8_sensor *sensor)
{
	struct v4l2_subdev *subdev = &sensor->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval;

	rval = et8ek8_i2c_write_regs(client, sensor->current_reglist->regs);
	if (rval)
		goto fail;

	/* Controls set while the power to the sensor is turned off are saved
	 * but not applied to the hardware. Now that we're about to start
	 * streaming apply all the current values to the hardware.
	 */
	rval = v4l2_ctrl_handler_setup(&sensor->ctrl_handler);
	if (rval)
		goto fail;

	return 0;

fail:
	dev_err(&client->dev, "sensor configuration failed\n");

	return rval;
}

static int et8ek8_stream_on(struct et8ek8_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	return et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x1252, 0xb0);
}

static int et8ek8_stream_off(struct et8ek8_sensor *sensor)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->subdev);

	return et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x1252, 0x30);
}

static int et8ek8_s_stream(struct v4l2_subdev *subdev, int streaming)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	int ret;

	if (!streaming)
		return et8ek8_stream_off(sensor);

	ret = et8ek8_configure(sensor);
	if (ret < 0)
		return ret;

	return et8ek8_stream_on(sensor);
}

/* --------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static int et8ek8_power_off(struct et8ek8_sensor *sensor)
{
	gpiod_set_value(sensor->reset, 0);
	udelay(1);

	clk_disable_unprepare(sensor->ext_clk);

	return regulator_disable(sensor->vana);
}

static int et8ek8_power_on(struct et8ek8_sensor *sensor)
{
	struct v4l2_subdev *subdev = &sensor->subdev;
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	unsigned int xclk_freq;
	int val, rval;

	rval = regulator_enable(sensor->vana);
	if (rval) {
		dev_err(&client->dev, "failed to enable vana regulator\n");
		return rval;
	}

	if (sensor->current_reglist)
		xclk_freq = sensor->current_reglist->mode.ext_clock;
	else
		xclk_freq = sensor->xclk_freq;

	rval = clk_set_rate(sensor->ext_clk, xclk_freq);
	if (rval < 0) {
		dev_err(&client->dev, "unable to set extclk clock freq to %u\n",
			xclk_freq);
		goto out;
	}
	rval = clk_prepare_enable(sensor->ext_clk);
	if (rval < 0) {
		dev_err(&client->dev, "failed to enable extclk\n");
		goto out;
	}

	if (rval)
		goto out;

	udelay(10); /* I wish this is a good value */

	gpiod_set_value(sensor->reset, 1);

	msleep(5000 * 1000 / xclk_freq + 1); /* Wait 5000 cycles */

	rval = et8ek8_i2c_reglist_find_write(client, &meta_reglist,
					     ET8EK8_REGLIST_POWERON);
	if (rval)
		goto out;

#ifdef USE_CRC
	rval = et8ek8_i2c_read_reg(client, ET8EK8_REG_8BIT, 0x1263, &val);
	if (rval)
		goto out;
#if USE_CRC /* TODO get crc setting from DT */
	val |= BIT(4);
#else
	val &= ~BIT(4);
#endif
	rval = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x1263, val);
	if (rval)
		goto out;
#endif

out:
	if (rval)
		et8ek8_power_off(sensor);

	return rval;
}

/* --------------------------------------------------------------------------
 * V4L2 subdev video operations
 */
#define MAX_FMTS 4
static int et8ek8_enum_mbus_code(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct et8ek8_reglist **list =
			et8ek8_reglist_first(&meta_reglist);
	u32 pixelformat[MAX_FMTS];
	int npixelformat = 0;

	if (code->index >= MAX_FMTS)
		return -EINVAL;

	for (; *list; list++) {
		struct et8ek8_mode *mode = &(*list)->mode;
		int i;

		if ((*list)->type != ET8EK8_REGLIST_MODE)
			continue;

		for (i = 0; i < npixelformat; i++) {
			if (pixelformat[i] == mode->bus_format)
				break;
		}
		if (i != npixelformat)
			continue;

		if (code->index == npixelformat) {
			code->code = mode->bus_format;
			return 0;
		}

		pixelformat[npixelformat] = mode->bus_format;
		npixelformat++;
	}

	return -EINVAL;
}

static int et8ek8_enum_frame_size(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	struct et8ek8_reglist **list =
			et8ek8_reglist_first(&meta_reglist);
	struct v4l2_mbus_framefmt format;
	int cmp_width = INT_MAX;
	int cmp_height = INT_MAX;
	int index = fse->index;

	for (; *list; list++) {
		if ((*list)->type != ET8EK8_REGLIST_MODE)
			continue;

		et8ek8_reglist_to_mbus(*list, &format);
		if (fse->code != format.code)
			continue;

		/* Assume that the modes are grouped by frame size. */
		if (format.width == cmp_width && format.height == cmp_height)
			continue;

		cmp_width = format.width;
		cmp_height = format.height;

		if (index-- == 0) {
			fse->min_width = format.width;
			fse->min_height = format.height;
			fse->max_width = format.width;
			fse->max_height = format.height;
			return 0;
		}
	}

	return -EINVAL;
}

static int et8ek8_enum_frame_ival(struct v4l2_subdev *subdev,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_interval_enum *fie)
{
	struct et8ek8_reglist **list =
			et8ek8_reglist_first(&meta_reglist);
	struct v4l2_mbus_framefmt format;
	int index = fie->index;

	for (; *list; list++) {
		struct et8ek8_mode *mode = &(*list)->mode;

		if ((*list)->type != ET8EK8_REGLIST_MODE)
			continue;

		et8ek8_reglist_to_mbus(*list, &format);
		if (fie->code != format.code)
			continue;

		if (fie->width != format.width || fie->height != format.height)
			continue;

		if (index-- == 0) {
			fie->interval = mode->timeperframe;
			return 0;
		}
	}

	return -EINVAL;
}

static struct v4l2_mbus_framefmt *
__et8ek8_get_pad_format(struct et8ek8_sensor *sensor,
			struct v4l2_subdev_state *sd_state,
			unsigned int pad, enum v4l2_subdev_format_whence which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&sensor->subdev, sd_state,
						  pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &sensor->format;
	default:
		return NULL;
	}
}

static int et8ek8_get_pad_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct v4l2_mbus_framefmt *format;

	format = __et8ek8_get_pad_format(sensor, sd_state, fmt->pad,
					 fmt->which);
	if (!format)
		return -EINVAL;

	fmt->format = *format;

	return 0;
}

static int et8ek8_set_pad_format(struct v4l2_subdev *subdev,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_format *fmt)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct v4l2_mbus_framefmt *format;
	struct et8ek8_reglist *reglist;

	format = __et8ek8_get_pad_format(sensor, sd_state, fmt->pad,
					 fmt->which);
	if (!format)
		return -EINVAL;

	reglist = et8ek8_reglist_find_mode_fmt(&meta_reglist, &fmt->format);
	et8ek8_reglist_to_mbus(reglist, &fmt->format);
	*format = fmt->format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		sensor->current_reglist = reglist;
		et8ek8_update_controls(sensor);
	}

	return 0;
}

static int et8ek8_get_frame_interval(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	memset(fi, 0, sizeof(*fi));
	fi->interval = sensor->current_reglist->mode.timeperframe;

	return 0;
}

static int et8ek8_set_frame_interval(struct v4l2_subdev *subdev,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct et8ek8_reglist *reglist;

	reglist = et8ek8_reglist_find_mode_ival(&meta_reglist,
						sensor->current_reglist,
						&fi->interval);

	if (!reglist)
		return -EINVAL;

	if (sensor->current_reglist->mode.ext_clock != reglist->mode.ext_clock)
		return -EINVAL;

	sensor->current_reglist = reglist;
	et8ek8_update_controls(sensor);

	return 0;
}

static int et8ek8_g_priv_mem(struct v4l2_subdev *subdev)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	unsigned int length = ET8EK8_PRIV_MEM_SIZE;
	unsigned int offset = 0;
	u8 *ptr  = sensor->priv_mem;
	int rval = 0;

	/* Read the EEPROM window-by-window, each window 8 bytes */
	do {
		u8 buffer[PRIV_MEM_WIN_SIZE];
		struct i2c_msg msg;
		int bytes, i;
		int ofs;

		/* Set the current window */
		rval = et8ek8_i2c_write_reg(client, ET8EK8_REG_8BIT, 0x0001,
					    0xe0 | (offset >> 3));
		if (rval < 0)
			return rval;

		/* Wait for status bit */
		for (i = 0; i < 1000; ++i) {
			u32 status;

			rval = et8ek8_i2c_read_reg(client, ET8EK8_REG_8BIT,
						   0x0003, &status);
			if (rval < 0)
				return rval;
			if (!(status & 0x08))
				break;
			usleep_range(1000, 2000);
		}

		if (i == 1000)
			return -EIO;

		/* Read window, 8 bytes at once, and copy to user space */
		ofs = offset & 0x07;	/* Offset within this window */
		bytes = length + ofs > 8 ? 8-ofs : length;
		msg.addr = client->addr;
		msg.flags = 0;
		msg.len = 2;
		msg.buf = buffer;
		ofs += PRIV_MEM_START_REG;
		buffer[0] = (u8)(ofs >> 8);
		buffer[1] = (u8)(ofs & 0xFF);

		rval = i2c_transfer(client->adapter, &msg, 1);
		if (rval < 0)
			return rval;

		mdelay(ET8EK8_I2C_DELAY);
		msg.addr = client->addr;
		msg.len = bytes;
		msg.flags = I2C_M_RD;
		msg.buf = buffer;
		memset(buffer, 0, sizeof(buffer));

		rval = i2c_transfer(client->adapter, &msg, 1);
		if (rval < 0)
			return rval;

		rval = 0;
		memcpy(ptr, buffer, bytes);

		length -= bytes;
		offset += bytes;
		ptr += bytes;
	} while (length > 0);

	return rval;
}

static int et8ek8_dev_init(struct v4l2_subdev *subdev)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval, rev_l, rev_h;

	rval = et8ek8_power_on(sensor);
	if (rval) {
		dev_err(&client->dev, "could not power on\n");
		return rval;
	}

	rval = et8ek8_i2c_read_reg(client, ET8EK8_REG_8BIT,
				   REG_REVISION_NUMBER_L, &rev_l);
	if (!rval)
		rval = et8ek8_i2c_read_reg(client, ET8EK8_REG_8BIT,
					   REG_REVISION_NUMBER_H, &rev_h);
	if (rval) {
		dev_err(&client->dev, "no et8ek8 sensor detected\n");
		goto out_poweroff;
	}

	sensor->version = (rev_h << 8) + rev_l;
	if (sensor->version != ET8EK8_REV_1 && sensor->version != ET8EK8_REV_2)
		dev_info(&client->dev,
			 "unknown version 0x%x detected, continuing anyway\n",
			 sensor->version);

	rval = et8ek8_reglist_import(client, &meta_reglist);
	if (rval) {
		dev_err(&client->dev,
			"invalid register list %s, import failed\n",
			ET8EK8_NAME);
		goto out_poweroff;
	}

	sensor->current_reglist = et8ek8_reglist_find_type(&meta_reglist,
							   ET8EK8_REGLIST_MODE);
	if (!sensor->current_reglist) {
		dev_err(&client->dev,
			"invalid register list %s, no mode found\n",
			ET8EK8_NAME);
		rval = -ENODEV;
		goto out_poweroff;
	}

	et8ek8_reglist_to_mbus(sensor->current_reglist, &sensor->format);

	rval = et8ek8_i2c_reglist_find_write(client, &meta_reglist,
					     ET8EK8_REGLIST_POWERON);
	if (rval) {
		dev_err(&client->dev,
			"invalid register list %s, no POWERON mode found\n",
			ET8EK8_NAME);
		goto out_poweroff;
	}
	rval = et8ek8_stream_on(sensor); /* Needed to be able to read EEPROM */
	if (rval)
		goto out_poweroff;
	rval = et8ek8_g_priv_mem(subdev);
	if (rval)
		dev_warn(&client->dev,
			"can not read OTP (EEPROM) memory from sensor\n");
	rval = et8ek8_stream_off(sensor);
	if (rval)
		goto out_poweroff;

	rval = et8ek8_power_off(sensor);
	if (rval)
		goto out_poweroff;

	return 0;

out_poweroff:
	et8ek8_power_off(sensor);

	return rval;
}

/* --------------------------------------------------------------------------
 * sysfs attributes
 */
static ssize_t
priv_mem_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

#if PAGE_SIZE < ET8EK8_PRIV_MEM_SIZE
#error PAGE_SIZE too small!
#endif

	memcpy(buf, sensor->priv_mem, ET8EK8_PRIV_MEM_SIZE);

	return ET8EK8_PRIV_MEM_SIZE;
}
static DEVICE_ATTR_RO(priv_mem);

/* --------------------------------------------------------------------------
 * V4L2 subdev core operations
 */

static int
et8ek8_registered(struct v4l2_subdev *subdev)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int rval;

	dev_dbg(&client->dev, "registered!");

	rval = device_create_file(&client->dev, &dev_attr_priv_mem);
	if (rval) {
		dev_err(&client->dev, "could not register sysfs entry\n");
		return rval;
	}

	rval = et8ek8_dev_init(subdev);
	if (rval)
		goto err_file;

	rval = et8ek8_init_controls(sensor);
	if (rval) {
		dev_err(&client->dev, "controls initialization failed\n");
		goto err_file;
	}

	__et8ek8_get_pad_format(sensor, NULL, 0, V4L2_SUBDEV_FORMAT_ACTIVE);

	return 0;

err_file:
	device_remove_file(&client->dev, &dev_attr_priv_mem);

	return rval;
}

static int __et8ek8_set_power(struct et8ek8_sensor *sensor, bool on)
{
	return on ? et8ek8_power_on(sensor) : et8ek8_power_off(sensor);
}

static int et8ek8_set_power(struct v4l2_subdev *subdev, int on)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);
	int ret = 0;

	mutex_lock(&sensor->power_lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (sensor->power_count == !on) {
		ret = __et8ek8_set_power(sensor, !!on);
		if (ret < 0)
			goto done;
	}

	/* Update the power count. */
	sensor->power_count += on ? 1 : -1;
	WARN_ON(sensor->power_count < 0);

done:
	mutex_unlock(&sensor->power_lock);

	return ret;
}

static int et8ek8_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(sd);
	struct v4l2_mbus_framefmt *format;
	struct et8ek8_reglist *reglist;

	reglist = et8ek8_reglist_find_type(&meta_reglist, ET8EK8_REGLIST_MODE);
	format = __et8ek8_get_pad_format(sensor, fh->state, 0,
					 V4L2_SUBDEV_FORMAT_TRY);
	et8ek8_reglist_to_mbus(reglist, format);

	return et8ek8_set_power(sd, true);
}

static int et8ek8_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return et8ek8_set_power(sd, false);
}

static const struct v4l2_subdev_video_ops et8ek8_video_ops = {
	.s_stream = et8ek8_s_stream,
	.g_frame_interval = et8ek8_get_frame_interval,
	.s_frame_interval = et8ek8_set_frame_interval,
};

static const struct v4l2_subdev_core_ops et8ek8_core_ops = {
	.s_power = et8ek8_set_power,
};

static const struct v4l2_subdev_pad_ops et8ek8_pad_ops = {
	.enum_mbus_code = et8ek8_enum_mbus_code,
	.enum_frame_size = et8ek8_enum_frame_size,
	.enum_frame_interval = et8ek8_enum_frame_ival,
	.get_fmt = et8ek8_get_pad_format,
	.set_fmt = et8ek8_set_pad_format,
};

static const struct v4l2_subdev_ops et8ek8_ops = {
	.core = &et8ek8_core_ops,
	.video = &et8ek8_video_ops,
	.pad = &et8ek8_pad_ops,
};

static const struct v4l2_subdev_internal_ops et8ek8_internal_ops = {
	.registered = et8ek8_registered,
	.open = et8ek8_open,
	.close = et8ek8_close,
};

/* --------------------------------------------------------------------------
 * I2C driver
 */
static int __maybe_unused et8ek8_suspend(struct device *dev)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	if (!sensor->power_count)
		return 0;

	return __et8ek8_set_power(sensor, false);
}

static int __maybe_unused et8ek8_resume(struct device *dev)
{
	struct v4l2_subdev *subdev = dev_get_drvdata(dev);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	if (!sensor->power_count)
		return 0;

	return __et8ek8_set_power(sensor, true);
}

static int et8ek8_probe(struct i2c_client *client)
{
	struct et8ek8_sensor *sensor;
	struct device *dev = &client->dev;
	int ret;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset)) {
		dev_dbg(&client->dev, "could not request reset gpio\n");
		return PTR_ERR(sensor->reset);
	}

	sensor->vana = devm_regulator_get(dev, "vana");
	if (IS_ERR(sensor->vana)) {
		dev_err(&client->dev, "could not get regulator for vana\n");
		return PTR_ERR(sensor->vana);
	}

	sensor->ext_clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->ext_clk)) {
		dev_err(&client->dev, "could not get clock\n");
		return PTR_ERR(sensor->ext_clk);
	}

	ret = of_property_read_u32(dev->of_node, "clock-frequency",
				   &sensor->xclk_freq);
	if (ret) {
		dev_warn(dev, "can't get clock-frequency\n");
		return ret;
	}

	mutex_init(&sensor->power_lock);

	v4l2_i2c_subdev_init(&sensor->subdev, client, &et8ek8_ops);
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->subdev.internal_ops = &et8ek8_internal_ops;

	sensor->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sensor->subdev.entity, 1, &sensor->pad);
	if (ret < 0) {
		dev_err(&client->dev, "media entity init failed!\n");
		goto err_mutex;
	}

	ret = v4l2_async_register_subdev_sensor(&sensor->subdev);
	if (ret < 0)
		goto err_entity;

	dev_dbg(dev, "initialized!\n");

	return 0;

err_entity:
	media_entity_cleanup(&sensor->subdev.entity);
err_mutex:
	mutex_destroy(&sensor->power_lock);
	return ret;
}

static int __exit et8ek8_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct et8ek8_sensor *sensor = to_et8ek8_sensor(subdev);

	if (sensor->power_count) {
		WARN_ON(1);
		et8ek8_power_off(sensor);
		sensor->power_count = 0;
	}

	v4l2_device_unregister_subdev(&sensor->subdev);
	device_remove_file(&client->dev, &dev_attr_priv_mem);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	v4l2_async_unregister_subdev(&sensor->subdev);
	media_entity_cleanup(&sensor->subdev.entity);
	mutex_destroy(&sensor->power_lock);

	return 0;
}

static const struct of_device_id et8ek8_of_table[] = {
	{ .compatible = "toshiba,et8ek8" },
	{ },
};
MODULE_DEVICE_TABLE(of, et8ek8_of_table);

static const struct i2c_device_id et8ek8_id_table[] = {
	{ ET8EK8_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, et8ek8_id_table);

static const struct dev_pm_ops et8ek8_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(et8ek8_suspend, et8ek8_resume)
};

static struct i2c_driver et8ek8_i2c_driver = {
	.driver		= {
		.name	= ET8EK8_NAME,
		.pm	= &et8ek8_pm_ops,
		.of_match_table	= et8ek8_of_table,
	},
	.probe_new	= et8ek8_probe,
	.remove		= __exit_p(et8ek8_remove),
	.id_table	= et8ek8_id_table,
};

module_i2c_driver(et8ek8_i2c_driver);

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@iki.fi>, Pavel Machek <pavel@ucw.cz");
MODULE_DESCRIPTION("Toshiba ET8EK8 camera sensor driver");
MODULE_LICENSE("GPL");
