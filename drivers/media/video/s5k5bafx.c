/*
 * Driver for S5K5BAFX from Samsung Electronics
 *
 * 1/6" 2Mp CMOS Image Sensor SoC with an Embedded Image Processor
 *
 * Copyright (C) 2010, DongSeong Lim<dongseong.lim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/i2c.h>
#include <linux/init.h>
#include <media/v4l2-device.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#ifdef CONFIG_VIDEO_SAMSUNG_V4L2
#include <linux/videodev2_exynos_camera.h>
#endif
#include <media/s5k5bafx_platform.h>

#include "s5k5bafx.h"

#ifdef S5K5BAFX_USLEEP
#include <linux/hrtimer.h>
#endif

#define S5K5BAFX_BURST_MODE

#include <linux/slab.h>
#ifdef CONFIG_LOAD_FILE
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

struct test {
	u8 data;
	struct test *nextBuf;
};
static struct test *testBuf;
static s32 large_file;

#define TEST_INIT	\
{			\
	.data = 0;	\
	.nextBuf = NULL;	\
}
#endif

#define CHECK_ERR(x)	if (unlikely((x) < 0)) { \
				cam_err("i2c failed, err %d\n", x); \
				return x; \
			}

#define NELEMS(array) (sizeof(array) / sizeof(array[0]))

extern struct class *camera_class;

#ifdef S5K5BAFX_USLEEP
/*
 * Use msleep() if the sleep time is over 1000 us.
*/
static void s5k5bafx_usleep(u32 usecs)
{
	ktime_t expires;
	u64 add_time = (u64)usecs * 1000;

	if (unlikely(!usecs))
		return;

	expires = ktime_add_ns(ktime_get(), add_time);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_hrtimeout(&expires, HRTIMER_MODE_ABS);
}
#endif

static inline int s5k5bafx_read(struct i2c_client *client,
	u16 subaddr, u16 *data)
{
	u8 buf[2];
	int err = 0;
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = 0,
		.len = 2,
		.buf = buf,
	};

	*(u16 *)buf = cpu_to_be16(subaddr);

	/* printk("\n\n\n%X %X\n\n\n", buf[0], buf[1]);*/

	err = i2c_transfer(client->adapter, &msg, 1);
	if (unlikely(err < 0))
		cam_err("ERR: %d register read fail\n", __LINE__);

	msg.flags = I2C_M_RD;

	err = i2c_transfer(client->adapter, &msg, 1);
	if (unlikely(err < 0))
		cam_err("ERR: %d register read fail\n", __LINE__);

	/*printk("\n\n\n%X %X\n\n\n", buf[0], buf[1]);*/
	*data = ((buf[0] << 8) | buf[1]);

	return err;
}

/*
 * s5k6aafx sensor i2c write routine
 * <start>--<Device address><2Byte Subaddr><2Byte Value>--<stop>
 */
#ifdef CONFIG_LOAD_FILE
static int loadFile(void)
{
	struct file *fp = NULL;
	struct test *nextBuf = NULL;

	u8 *nBuf = NULL;
	size_t file_size = 0, max_size = 0, testBuf_size = 0;
	ssize_t nread = 0;
	s32 check = 0, starCheck = 0;
	s32 tmp_large_file = 0;
	s32 i = 0;
	int ret = 0;
	loff_t pos;

	mm_segment_t fs = get_fs();
	set_fs(get_ds());

	BUG_ON(testBuf);

	fp = filp_open("/mnt/sdcard/external_sd/s5k5bafx.h", O_RDONLY, 0);
	if (IS_ERR(fp)) {
		cam_err("file open error\n");
		return PTR_ERR(fp);
	}

	file_size = (size_t) fp->f_path.dentry->d_inode->i_size;
	max_size = file_size;

	cam_dbg("file_size = %d\n", file_size);

	nBuf = kmalloc(file_size, GFP_ATOMIC);
	if (nBuf == NULL) {
		cam_dbg("Fail to 1st get memory\n");
		nBuf = vmalloc(file_size);
		if (nBuf == NULL) {
			cam_err("ERR: nBuf Out of Memory\n");
			ret = -ENOMEM;
			goto error_out;
		}
		tmp_large_file = 1;
	}

	testBuf_size = sizeof(struct test) * file_size;
	if (tmp_large_file) {
		testBuf = (struct test *)vmalloc(testBuf_size);
		large_file = 1;
	} else {
		testBuf = kmalloc(testBuf_size, GFP_ATOMIC);
		if (testBuf == NULL) {
			cam_dbg("Fail to get mem(%d bytes)\n", testBuf_size);
			testBuf = (struct test *)vmalloc(testBuf_size);
			large_file = 1;
		}
	}
	if (testBuf == NULL) {
		cam_err("ERR: Out of Memory\n");
		ret = -ENOMEM;
		goto error_out;
	}

	pos = 0;
	memset(nBuf, 0, file_size);
	memset(testBuf, 0, file_size * sizeof(struct test));

	nread = vfs_read(fp, (char __user *)nBuf, file_size, &pos);
	if (nread != file_size) {
		cam_err("failed to read file ret = %d\n", nread);
		ret = -1;
		goto error_out;
	}

	set_fs(fs);

	i = max_size;

	printk("i = %d\n", i);

	while (i) {
		testBuf[max_size - i].data = *nBuf;
		if (i != 1) {
			testBuf[max_size - i].nextBuf = &testBuf[max_size - i + 1];
		} else {
			testBuf[max_size - i].nextBuf = NULL;
			break;
		}
		i--;
		nBuf++;
	}

	i = max_size;
	nextBuf = &testBuf[0];

#if 1
	while (i - 1) {
		if (!check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data
								== '/') {
						check = 1;/* when find '//' */
						i--;
					} else if (testBuf[max_size-i].nextBuf->data == '*') {
						starCheck = 1;/* when find '/ *' */
						i--;
					}
				} else
					break;
			}
			if (!check && !starCheck) {
				/* ignore '\t' */
				if (testBuf[max_size - i].data != '\t') {
					nextBuf->nextBuf = &testBuf[max_size-i];
					nextBuf = &testBuf[max_size - i];
				}
			}
		} else if (check && !starCheck) {
			if (testBuf[max_size - i].data == '/') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data == '*') {
						starCheck = 1; /* when find '/ *' */
						check = 0;
						i--;
					}
				} else
					break;
			}

			 /* when find '\n' */
			if (testBuf[max_size - i].data == '\n' && check) {
				check = 0;
				nextBuf->nextBuf = &testBuf[max_size - i];
				nextBuf = &testBuf[max_size - i];
			}

		} else if (!check && starCheck) {
			if (testBuf[max_size - i].data == '*') {
				if (testBuf[max_size-i].nextBuf != NULL) {
					if (testBuf[max_size-i].nextBuf->data == '/') {
						starCheck = 0; /* when find '* /' */
						i--;
					}
				} else
					break;
			}
		}

		i--;

		if (i < 2) {
			nextBuf = NULL;
			break;
		}

		if (testBuf[max_size - i].nextBuf == NULL) {
			nextBuf = NULL;
			break;
		}
	}
#endif

#if 0 /* for print */
	printk("i = %d\n", i);
	nextBuf = &testBuf[0];
	while (1) {
		/* printk("sdfdsf\n"); */
		if (nextBuf->nextBuf == NULL)
			break;
		printk("%c", nextBuf->data);
		nextBuf = nextBuf->nextBuf;
	}
#endif

error_out:

	if (nBuf)
		tmp_large_file ? vfree(nBuf) : kfree(nBuf);
	if (fp)
		filp_close(fp, current->files);
	return ret;
}
#endif

static inline int s5k5bafx_write(struct i2c_client *client,
		u32 packet)
{
	u8 buf[4];
	int err = 0, retry_count = 5;

	struct i2c_msg msg = {
		.addr	= client->addr,
		.flags	= 0,
		.buf	= buf,
		.len	= 4,
	};

	if (!client->adapter) {
		cam_err("ERR - can't search i2c client adapter\n");
		return -EIO;
	}

	while (retry_count--) {
		*(u32 *)buf = cpu_to_be32(packet);
		err = i2c_transfer(client->adapter, &msg, 1);
		if (likely(err == 1))
			break;
		mdelay(10);
	}

	if (unlikely(err < 0)) {
		cam_err("ERR - 0x%08x write failed err=%d\n",
				(u32)packet, err);
		return err;
	}

	return (err != 1) ? -1 : 0;
}

#ifdef CONFIG_LOAD_FILE
static int s5k5bafx_write_regs_from_sd(struct v4l2_subdev *sd, u8 s_name[])
{

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct test *tempData = NULL;

	int ret = -EAGAIN;
	u32 temp;
	u32 delay = 0;
	u8 data[11];
	s32 searched = 0;
	size_t size = strlen(s_name);
	s32 i;

	cam_dbg("E size = %d, string = %s\n", size, s_name);
	tempData = &testBuf[0];
	while (!searched) {
		searched = 1;
		for (i = 0; i < size; i++) {
			if (tempData->data != s_name[i]) {
				searched = 0;
				break;
			}
			tempData = tempData->nextBuf;
		}
		tempData = tempData->nextBuf;
	}
	/* structure is get..*/

	while (1) {
		if (tempData->data == '{')
			break;
		else
			tempData = tempData->nextBuf;
	}

	while (1) {
		searched = 0;
		while (1) {
			if (tempData->data == 'x') {
				/* get 10 strings.*/
				data[0] = '0';
				for (i = 1; i < 11; i++) {
					data[i] = tempData->data;
					tempData = tempData->nextBuf;
				}
				/*cam_dbg("%s\n", data);*/
				temp = simple_strtoul(data, NULL, 16);
				break;
			} else if (tempData->data == '}') {
				searched = 1;
				break;
			} else
				tempData = tempData->nextBuf;

			if (tempData->nextBuf == NULL)
				return -1;
		}

		if (searched)
			break;

		if ((temp & S5K5BAFX_DELAY) == S5K5BAFX_DELAY) {
			delay = temp & 0xFFFF;
			cam_info("line(%d):delay(0x%x, %d)\n", __LINE__,
							delay, delay);
			msleep(delay);
			continue;
		}

		ret = s5k5bafx_write(client, temp);

		/* In error circumstances */
		/* Give second shot */
		if (unlikely(ret)) {
			dev_info(&client->dev,
					"s5k5bafx i2c retry one more time\n");
			ret = s5k5bafx_write(client, temp);

			/* Give it one more shot */
			if (unlikely(ret)) {
				dev_info(&client->dev,
						"s5k5bafx i2c retry twice\n");
				ret = s5k5bafx_write(client, temp);
			}
		}
	}

	return ret;
}
#endif

/*
* Read a register.
*/
static int s5k5bafx_read_reg(struct v4l2_subdev *sd,
		u16 page, u16 addr, u16 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u32 page_cmd = (0x002C << 16) | page;
	u32 addr_cmd = (0x002E << 16) | addr;
	int err = 0;

	cam_dbg("page_cmd=0x%X, addr_cmd=0x%X\n", page_cmd, addr_cmd);

	err = s5k5bafx_write(client, page_cmd);
	CHECK_ERR(err);
	err = s5k5bafx_write(client, addr_cmd);
	CHECK_ERR(err);
	err = s5k5bafx_read(client, 0x0F12, val);
	CHECK_ERR(err);

	return 0;
}

#ifdef S5K5BAFX_BURST_MODE
	static u16 addr, value;

	static int len;
	static u8 buf[SZ_2K] = {0,};
#else
	static u8 buf[4] = {0,};
#endif

/* program multiple registers */
static int s5k5bafx_write_regs(struct v4l2_subdev *sd,
		const u32 *packet, u32 num)
{
	struct s5k5bafx_state *state = to_state(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = -EAGAIN;
	u32 temp = 0;
	u16 delay = 0;
	int retry_count = 5;

	struct i2c_msg msg = {
		msg.addr = client->addr,
		msg.flags = 0,
		msg.len = 4,
		msg.buf = buf,
	};

	while (num--) {
		temp = *packet++;

		if ((temp & S5K5BAFX_DELAY) == S5K5BAFX_DELAY) {
			delay = temp & 0xFFFF;
			cam_dbg("line(%d):delay(0x%x):delay(%d)\n",
						__LINE__, delay, delay);
			msleep(delay);
			continue;
		}

#ifdef S5K5BAFX_BURST_MODE
		addr = temp >> 16;
		value = temp & 0xFFFF;

		switch (addr) {
		case 0x0F12:
			if (len == 0) {
				buf[len++] = addr >> 8;
				buf[len++] = addr & 0xFF;
			}
			buf[len++] = value >> 8;
			buf[len++] = value & 0xFF;

			if ((*packet >> 16) != addr) {
				msg.len = len;
				goto s5k5bafx_burst_write;
			}
			break;

		case 0xFFFF:
			break;

		default:
			msg.len = 4;
			*(u32 *)buf = cpu_to_be32(temp);
			goto s5k5bafx_burst_write;
		}

		continue;
#else
		*(u32 *)buf = cpu_to_be32(temp);
#endif

#ifdef S5K5BAFX_BURST_MODE
s5k5bafx_burst_write:
		len = 0;
#endif
		retry_count = 5;

		while (retry_count--) {
			ret = i2c_transfer(client->adapter, &msg, 1);
			if (likely(ret == 1))
				break;
			mdelay(10);
		}

		if (unlikely(ret < 0)) {
			cam_err("ERR - 0x%08x write failed err=%d\n", (u32)packet, ret);
			break;
		}

#ifdef S5K5BAFX_USLEEP
		if (unlikely(state->vt_mode))
			if (!(num%200))
				s5k5bafx_usleep(3);
#endif
	}

	if (unlikely(ret < 0)) {
		cam_err("fail to write registers!!\n");
		return -EIO;
	}

	return 0;
}

static int s5k5bafx_get_exif(struct v4l2_subdev *sd)
{
	struct s5k5bafx_state *state = to_state(sd);
	u16 iso_gain_table[] = {10, 18, 23, 28};
	u16 iso_table[] = {0, 50, 100, 200, 400};
	u16 gain = 0, val = 0;
	s32 index = 0;

	state->exif.shutter_speed = 0;
	state->exif.iso = 0;

	/* Get shutter speed */
	s5k5bafx_read_reg(sd, REG_PAGE_SHUTTER, REG_ADDR_SHUTTER, &val);
	state->exif.shutter_speed = 1000 / (val / 500);
	cam_dbg("val = %d\n", val);

	/* Get ISO */
	val = 0;
	s5k5bafx_read_reg(sd, REG_PAGE_ISO, REG_ADDR_ISO, &val);
	cam_dbg("val = %d\n", val);
	gain = val * 10 / 256;
	for (index = 0; index < NELEMS(iso_gain_table); index++) {
		if (gain < iso_gain_table[index])
			break;
	}
	state->exif.iso = iso_table[index];

	cam_dbg("gain=%d, Shutter speed=%d, ISO=%d\n",
		gain, state->exif.shutter_speed, state->exif.iso);
	return 0;
}

static int s5k5bafx_check_dataline(struct v4l2_subdev *sd, s32 val)
{
	int err = 0;

	cam_info("DTP %s\n", val ? "ON" : "OFF");

#ifdef CONFIG_LOAD_FILE
	if (val)
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_pattern_on");
	else
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_pattern_off");
#else
	if (val) {
		err = s5k5bafx_write_regs(sd, s5k5bafx_pattern_on,
			sizeof(s5k5bafx_pattern_on) / \
			sizeof(s5k5bafx_pattern_on[0]));
	} else {
		err = s5k5bafx_write_regs(sd, s5k5bafx_pattern_off,
			sizeof(s5k5bafx_pattern_off) / \
			sizeof(s5k5bafx_pattern_off[0]));
	}
#endif
	if (unlikely(err)) {
		cam_err("fail to DTP setting\n");
		return err;
	}

	return 0;
}

static int s5k5bafx_debug_sensor_status(struct v4l2_subdev *sd)
{
	u16 val = 0;
	int err = -EINVAL;

	/* Read Mon_DBG_Counters_2 */
	/*err = s5k5bafx_read_reg(sd, 0x7000, 0x0402, &val);
	CHECK_ERR(err);
	cam_info("counter = %d\n", val); */

	/* Read REG_TC_GP_EnableCaptureChanged. */
	err = s5k5bafx_read_reg(sd, 0x7000, 0x01F6, &val);
	CHECK_ERR(err);

	switch (val) {
	case 0:
		cam_info("In normal mode(0)\n");
		break;
	case 1:
		cam_info("In swiching to capture mode(1).....\n");
		break;
	default:
		cam_err("In Unknown mode(?)\n");
		break;
	}

	return 0;
}

static int s5k5bafx_check_sensor_status(struct v4l2_subdev *sd)
{
	/*struct i2c_client *client = v4l2_get_subdevdata(sd);*/
	u16 val_1 = 0, val_2 = 0;
	int err = -EINVAL;

	err = s5k5bafx_read_reg(sd, 0x7000, 0x0132, &val_1);
	CHECK_ERR(err);
	err = s5k5bafx_read_reg(sd, 0xD000, 0x1002, &val_2);
	CHECK_ERR(err);

	cam_dbg("read val1=0x%x, val2=0x%x\n", val_1, val_2);

	if ((val_1 != 0xAAAA) || (val_2 != 0))
		goto error_occur;

	cam_dbg("Sensor error is not detected\n");
	return 0;

error_occur:
	cam_err("ERR: Sensor error occurs\n\n");
	return -EIO;
}

static inline int s5k5bafx_check_esd(struct v4l2_subdev *sd)
{
	int err = -EINVAL;

	err = s5k5bafx_check_sensor_status(sd);
	if (err < 0) {
		cam_err("ERR: ESD Shock detected!\n");
		return err;
	}

	return 0;
}

static int s5k5bafx_set_preview_start(struct v4l2_subdev *sd)
{
	struct s5k5bafx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_info("set preview\n");

#ifdef CONFIG_LOAD_FILE
	err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_preview");
#else
	err = s5k5bafx_write_regs(sd, s5k5bafx_preview,
			ARRAY_SIZE(s5k5bafx_preview));
#endif
	if (state->check_dataline)
		err = s5k5bafx_check_dataline(sd, 1);
	if (unlikely(err)) {
		cam_err("fail to make preview\n");
		return err;
	}

	return 0;
}

static int s5k5bafx_set_preview_stop(struct v4l2_subdev *sd)
{
	int err = 0;
	cam_info("do nothing.\n");

	return err;
}

static int s5k5bafx_set_capture_start(struct v4l2_subdev *sd)
{
	int err = -EINVAL;

	/* set initial regster value */
#ifdef CONFIG_LOAD_FILE
	err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_capture");
#else
	err = s5k5bafx_write_regs(sd, s5k5bafx_capture,
		sizeof(s5k5bafx_capture) / sizeof(s5k5bafx_capture[0]));
#endif
	if (unlikely(err)) {
		cam_err("failed to make capture\n");
		return err;
	}

	s5k5bafx_get_exif(sd);

	return err;
}

static int s5k5bafx_set_sensor_mode(struct v4l2_subdev *sd,
					struct v4l2_control *ctrl)
{
	struct s5k5bafx_state *state = to_state(sd);

	if ((ctrl->value != SENSOR_CAMERA) &&
	(ctrl->value != SENSOR_MOVIE)) {
		cam_err("ERR: Not support.(%d)\n", ctrl->value);
		return -EINVAL;
	}

	/* We does not support movie mode when in VT. */
	if ((ctrl->value == SENSOR_MOVIE) && state->vt_mode) {
		state->sensor_mode = SENSOR_CAMERA;
		cam_warn("ERR: Not support movie\n");
	} else
		state->sensor_mode = ctrl->value;

	return 0;
}

static int s5k5bafx_g_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	cam_dbg("E\n");
	return 0;
}

static int s5k5bafx_enum_framesizes(struct v4l2_subdev *sd, \
					struct v4l2_frmsizeenum *fsize)
{
	struct s5k5bafx_state *state = to_state(sd);

	cam_dbg("E\n");

	/*
	 * Return the actual output settings programmed to the camera
	 */
	if (state->req_fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE) {
		fsize->discrete.width = state->capture_frmsizes.width;
		fsize->discrete.height = state->capture_frmsizes.height;
	} else {
		fsize->discrete.width = state->preview_frmsizes.width;
		fsize->discrete.height = state->preview_frmsizes.height;
	}

	cam_info("width - %d , height - %d\n",
		fsize->discrete.width, fsize->discrete.height);

	return 0;
}

#if (0) /* not used */
static int s5k5bafx_enum_fmt(struct v4l2_subdev *sd, struct v4l2_fmtdesc *fmtdesc)
{
	int err = 0;

	FUNC_ENTR();
	return err;
}

static int s5k5bafx_enum_frameintervals(struct v4l2_subdev *sd,
					struct v4l2_frmivalenum *fival)
{
	int err = 0;

	FUNC_ENTR();
	return err;
}
#endif

static int s5k5bafx_try_fmt(struct v4l2_subdev *sd, struct v4l2_format *fmt)
{
	int err = 0;

	cam_dbg("E\n");

	return err;
}

static int s5k5bafx_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *ffmt)
{
	struct s5k5bafx_state *state = to_state(sd);
	u32 *width = NULL, *height = NULL;

	cam_dbg("E\n");
	/*
	 * Just copying the requested format as of now.
	 * We need to check here what are the formats the camera support, and
	 * set the most appropriate one according to the request from FIMC
	 */

	state->req_fmt.width = ffmt->width;
	state->req_fmt.height = ffmt->height;
	state->req_fmt.priv = ffmt->field;

	switch (state->req_fmt.priv) {
	case V4L2_PIX_FMT_MODE_PREVIEW:
		cam_dbg("V4L2_PIX_FMT_MODE_PREVIEW\n");
		width = &state->preview_frmsizes.width;
		height = &state->preview_frmsizes.height;
		break;

	case V4L2_PIX_FMT_MODE_CAPTURE:
		cam_dbg("V4L2_PIX_FMT_MODE_CAPTURE\n");
		width = &state->capture_frmsizes.width;
		height = &state->capture_frmsizes.height;
		break;

	default:
		cam_err("ERR(EINVAL)\n");
		return -EINVAL;
	}

	if ((*width != state->req_fmt.width) ||
		(*height != state->req_fmt.height)) {
		cam_err("ERR: Invalid size. width= %d, height= %d\n",
			state->req_fmt.width, state->req_fmt.height);
	}

	return 0;
}

static int s5k5bafx_set_frame_rate(struct v4l2_subdev *sd, u32 fps)
{
	int err = 0;

	cam_info("frame rate %d\n\n", fps);

#ifdef CONFIG_LOAD_FILE
	switch (fps) {
	case 7:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_7fps");
		break;
	case 10:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_10fps");

		break;
	case 12:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_12fps");

		break;
	case 15:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_15fps");
		break;
	case 30:
		cam_err("frame rate is 30\n");
		break;
	default:
		cam_err("ERR: Invalid framerate\n");
		break;
	}
#else
	switch (fps) {
	case 7:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_7fps,
				sizeof(s5k5bafx_vt_7fps) / \
				sizeof(s5k5bafx_vt_7fps[0]));
		break;
	case 10:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_10fps,
				sizeof(s5k5bafx_vt_10fps) / \
				sizeof(s5k5bafx_vt_10fps[0]));

		break;
	case 12:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_12fps,
				sizeof(s5k5bafx_vt_12fps) / \
				sizeof(s5k5bafx_vt_12fps[0]));

		break;
	case 15:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_15fps,
				sizeof(s5k5bafx_vt_15fps) / \
				sizeof(s5k5bafx_vt_15fps[0]));
		break;
	case 30:
		cam_warn("frame rate is 30\n");
		break;
	default:
		cam_err("ERR: Invalid framerate\n");
		break;
	}
#endif

	if (unlikely(err < 0)) {
		cam_err("i2c_write for set framerate\n");
		return -EIO;
	}

	return err;
}

static int s5k5bafx_g_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	int err = 0;

	cam_dbg("E\n");

	return err;
}

static int s5k5bafx_s_parm(struct v4l2_subdev *sd, struct v4l2_streamparm *parms)
{
	int err = 0;
	u32 fps = 0;
	struct s5k5bafx_state *state = to_state(sd);

	if (!state->vt_mode)
		return 0;

	cam_dbg("E\n");

	fps = parms->parm.capture.timeperframe.denominator /
			parms->parm.capture.timeperframe.numerator;

	if (fps != state->set_fps) {
		if (fps < 0 && fps > 15) {
			cam_err("invalid frame rate %d\n", fps);
			fps = 15;
		}
		state->req_fps = fps;

		if (state->initialized) {
			err = s5k5bafx_set_frame_rate(sd, state->req_fps);
			if (err >= 0)
				state->set_fps = state->req_fps;
		}

	}

	return err;
}

#if (0) /* not used */
static int s5k5bafx_set_60hz_antibanding(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5bafx_state *state = to_state(sd);
	int err = -EINVAL;

	FUNC_ENTR();

	u32 s5k5bafx_antibanding60hz[] = {
		0xFCFCD000,
		0x00287000,
		/* Anti-Flicker //
		// End user init script */
		0x002A0400,
		0x0F12005F,	/* REG_TC_DBG_AutoAlgEnBits //Auto Anti-Flicker is enabled bit[5] = 1. */
		0x002A03DC,
		0x0F120002,	/* 02 REG_SF_USER_FlickerQuant //Set flicker quantization(0: no AFC, 1: 50Hz, 2: 60 Hz) */
		0x0F120001,
	};

	err = s5k5bafx_write_regs(sd, s5k5bafx_antibanding60hz,
				  sizeof(s5k5bafx_antibanding60hz) /
				  sizeof(s5k5bafx_antibanding60hz[0]));
	pr_info("%s: setting 60hz antibanding\n", __func__);
	if (unlikely(err)) {
		pr_info("%s: failed to set 60hz antibanding\n", __func__);
		return err;
	}

	return 0;
}
#endif

static int s5k5bafx_control_stream(struct v4l2_subdev *sd, stream_cmd_t cmd)
{
	int err = 0;

	switch (cmd) {
	case STREAM_START:
		cam_warn("WARN: do nothing\n");
		break;

	case STREAM_STOP:
		cam_dbg("stream stop!!!\n");
#ifdef CONFIG_LOAD_FILE
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_stream_stop");
#else
		err = s5k5bafx_write_regs(sd, s5k5bafx_stream_stop,
			sizeof(s5k5bafx_stream_stop) / \
			sizeof(s5k5bafx_stream_stop[0]));
#endif
		break;
	default:
		cam_err("ERR: Invalid cmd\n");
		break;
	}

	if (unlikely(err))
		cam_err("failed to stream start(stop)\n");

	return err;
}

static int s5k5bafx_check_device(struct v4l2_subdev *sd)
{
	struct s5k5bafx_state *state = to_state(sd);
	const u32 write_reg = 0x00287000;
	u16 read_value = 0;
	int err = -ENODEV;

	/* enter read mode */
	err = s5k5bafx_read_reg(sd, 0xD000, 0x1006, &read_value);
	if (unlikely(err < 0))
		return -ENODEV;

	if (likely(read_value == S5K5BAFX_CHIP_ID))
		cam_info("Sensor ChipID: 0x%04X\n", S5K5BAFX_CHIP_ID);
	else
		cam_info("Sensor ChipID: 0x%04X, unknown ChipID\n", read_value);

	err = s5k5bafx_read_reg(sd, 0xD000, 0x1008, &read_value);
	if (likely((u8)read_value == S5K5BAFX_CHIP_REV))
		cam_info("Sensor revision: 0x%02X\n", S5K5BAFX_CHIP_REV);
	else
		cam_info("Sensor revision: 0x%02X, unknown revision\n",
				(u8)read_value);

	/* restore write mode */
	err = s5k5bafx_write_regs(sd, &write_reg, 1);
	if (err < 0)
		return -ENODEV;

	return 0;
}


static int s5k5bafx_init(struct v4l2_subdev *sd, u32 val)
{
	/* struct i2c_client *client = v4l2_get_subdevdata(sd); */
	struct s5k5bafx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("E\n");

	s5k5bafx_check_device(sd);

	/* set initial regster value */
#ifdef CONFIG_LOAD_FILE
	if (state->sensor_mode == SENSOR_CAMERA) {
		if (!state->vt_mode) {
			cam_dbg("load camera common setting\n");
			err = s5k5bafx_write_regs_from_sd(sd,
					"s5k5bafx_common");
		} else {
			if (state->vt_mode == 1) {
				cam_info("load camera VT call setting\n");
				err = s5k5bafx_write_regs_from_sd(sd,
						"s5k5bafx_vt_common");
			} else {
				cam_info("load camera WIFI VT call setting\n");
				err = s5k5bafx_write_regs_from_sd(sd,
						"s5k5bafx_vt_wifi_common");
			}
		}
	} else {
		cam_info("load recording setting\n");

		if (ANTI_BANDING_50HZ == state->anti_banding) {
			err = s5k5bafx_write_regs_from_sd(sd,
				"s5k5bafx_recording_50Hz_common");
		} else {
			err = s5k5bafx_write_regs_from_sd(sd,
				"s5k5bafx_recording_60Hz_common");
		}
	}
#else
	if (state->sensor_mode == SENSOR_CAMERA) {
		if (!state->vt_mode) {
			cam_info("load camera common setting\n");
			err = s5k5bafx_write_regs(sd, s5k5bafx_common,
				ARRAY_SIZE(s5k5bafx_common));
		} else {
			if (state->vt_mode == 1) {
				cam_info("load camera VT call setting\n");
				err = s5k5bafx_write_regs(sd, s5k5bafx_vt_common,
					ARRAY_SIZE(s5k5bafx_vt_common));
			} else if (state->vt_mode == 3) {
				cam_info("load camera smart stay setting\n");
				err = s5k5bafx_write_regs(sd,
					s5k5bafx_recording_50Hz_common,
					ARRAY_SIZE(
					s5k5bafx_recording_50Hz_common));
			} else {
				cam_info("load camera WIFI VT call setting\n");
				err = s5k5bafx_write_regs(sd, s5k5bafx_vt_wifi_common,
					ARRAY_SIZE(s5k5bafx_vt_wifi_common));
			}
		}
	} else {
		cam_info("load recording setting\n");
		if (ANTI_BANDING_50HZ == state->anti_banding) {
			err = s5k5bafx_write_regs(sd, s5k5bafx_recording_50Hz_common,
				ARRAY_SIZE(s5k5bafx_recording_50Hz_common));
		} else {
			err = s5k5bafx_write_regs(sd, s5k5bafx_recording_60Hz_common,
				ARRAY_SIZE(s5k5bafx_recording_60Hz_common));
		}
	}
#endif
	if (unlikely(err)) {
		cam_err("failed to init\n");
		return err;
	}

	/* We stop stream-output from sensor when starting camera. */
	err = s5k5bafx_control_stream(sd, STREAM_STOP);
	if (unlikely(err < 0))
		return err;
	msleep(150);

	if (state->vt_mode && (state->req_fps != state->set_fps)) {
		err = s5k5bafx_set_frame_rate(sd, state->req_fps);
		if (unlikely(err < 0))
			return err;
		else
			state->set_fps = state->req_fps;
	}

	state->initialized = 1;

	return 0;
}

#if 0
static int s5k5bafx_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	FUNC_ENTR();
	return 0;
}

static int s5k5bafx_querymenu(struct v4l2_subdev *sd, struct v4l2_querymenu *qm)
{
	FUNC_ENTR();
	return 0;
}
#endif

static int s5k5bafx_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct s5k5bafx_state *state = to_state(sd);
	/* struct i2c_client *client = v4l2_get_subdevdata(sd); */
	int err = 0;

	cam_info("stream mode = %d\n", enable);

	switch (enable) {
	case STREAM_MODE_CAM_OFF:
		if (state->sensor_mode == SENSOR_CAMERA) {
			if (state->check_dataline)
				err = s5k5bafx_check_dataline(sd, 0);
			else
				err = s5k5bafx_control_stream(sd, STREAM_STOP);
		}
		break;

	case STREAM_MODE_CAM_ON:
		/* The position of this code need to be adjusted later */
		if ((state->sensor_mode == SENSOR_CAMERA)
		&& (state->req_fmt.priv == V4L2_PIX_FMT_MODE_CAPTURE))
			err = s5k5bafx_set_capture_start(sd);
		else
			err = s5k5bafx_set_preview_start(sd);
		break;

	case STREAM_MODE_MOVIE_ON:
		cam_dbg("do nothing(movie on)!!\n");
		break;

	case STREAM_MODE_MOVIE_OFF:
		cam_dbg("do nothing(movie off)!!\n");
		break;

	default:
		cam_err("ERR: Invalid stream mode\n");
		break;
	}

	if (unlikely(err < 0)) {
		cam_err("ERR: faild\n");
		return err;
	}

	return 0;
}

static int s5k5bafx_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s5k5bafx_state *state = to_state(sd);
	int err = 0;

	cam_dbg("ctrl->id : %d\n", ctrl->id - V4L2_CID_PRIVATE_BASE);

	switch (ctrl->id) {
	case V4L2_CID_CAMERA_EXIF_TV:
		ctrl->value = state->exif.shutter_speed;
		break;
	case V4L2_CID_CAMERA_EXIF_ISO:
		ctrl->value = state->exif.iso;
		break;
	default:
		cam_err("no such control id %d\n",
				ctrl->id - V4L2_CID_PRIVATE_BASE);
		break;
	}

	return err;
}

static int s5k5bafx_set_brightness(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s5k5bafx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("E\n");

	if (state->check_dataline)
		return 0;

#ifdef CONFIG_LOAD_FILE
	switch (ctrl->value) {
	case EV_MINUS_4:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_m4");
		break;
	case EV_MINUS_3:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_m3");
		break;
	case EV_MINUS_2:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_m2");
		break;
	case EV_MINUS_1:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_m1");
		break;
	case EV_DEFAULT:
		err = s5k5bafx_write_regs_from_sd(sd,
			"s5k5bafx_bright_default");
		break;
	case EV_PLUS_1:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_p1");
		break;
	case EV_PLUS_2:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_p2");
		break;
	case EV_PLUS_3:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_p3");
		break;
	case EV_PLUS_4:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_bright_p4");
		break;
	default:
		cam_err("ERR: Invalid brightness(%d)\n", ctrl->value);
		return err;
		break;
	}
#else
	switch (ctrl->value) {
	case EV_MINUS_4:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_m4, \
			sizeof(s5k5bafx_bright_m4) / \
			sizeof(s5k5bafx_bright_m4[0]));
		break;
	case EV_MINUS_3:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_m3, \
			sizeof(s5k5bafx_bright_m3) / \
			sizeof(s5k5bafx_bright_m3[0]));

		break;
	case EV_MINUS_2:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_m2, \
			sizeof(s5k5bafx_bright_m2) / \
			sizeof(s5k5bafx_bright_m2[0]));
		break;
	case EV_MINUS_1:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_m1, \
			sizeof(s5k5bafx_bright_m1) / \
			sizeof(s5k5bafx_bright_m1[0]));
		break;
	case EV_DEFAULT:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_default, \
			sizeof(s5k5bafx_bright_default) / \
			sizeof(s5k5bafx_bright_default[0]));
		break;
	case EV_PLUS_1:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_p1, \
			sizeof(s5k5bafx_bright_p1) / \
			sizeof(s5k5bafx_bright_p1[0]));
		break;
	case EV_PLUS_2:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_p2, \
			sizeof(s5k5bafx_bright_p2) / \
			sizeof(s5k5bafx_bright_p2[0]));
		break;
	case EV_PLUS_3:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_p3, \
			sizeof(s5k5bafx_bright_p3) / \
			sizeof(s5k5bafx_bright_p3[0]));
		break;
	case EV_PLUS_4:
		err = s5k5bafx_write_regs(sd, s5k5bafx_bright_p4, \
			sizeof(s5k5bafx_bright_p4) / \
			sizeof(s5k5bafx_bright_p4[0]));
		break;
	default:
		cam_err("ERR: invalid brightness(%d)\n", ctrl->value);
		return err;
		break;
	}
#endif

	if (unlikely(err < 0)) {
		cam_err("ERR: i2c_write for set brightness\n");
		return -EIO;
	}

	return 0;
}

static int s5k5bafx_set_blur(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct s5k5bafx_state *state = to_state(sd);
	int err = -EINVAL;

	cam_dbg("E\n");

	if (state->check_dataline)
		return 0;

#ifdef CONFIG_LOAD_FILE
	switch (ctrl->value) {
	case BLUR_LEVEL_0:
		err = s5k5bafx_write_regs_from_sd(sd,
				"s5k5bafx_vt_pretty_default");
		break;
	case BLUR_LEVEL_1:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_pretty_1");
		break;
	case BLUR_LEVEL_2:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_pretty_2");
		break;
	case BLUR_LEVEL_3:
	case BLUR_LEVEL_MAX:
		err = s5k5bafx_write_regs_from_sd(sd, "s5k5bafx_vt_pretty_3");
		break;
	default:
		cam_err("ERR: Invalid blur(%d)\n", ctrl->value);
		return err;
		break;
	}
#else
	switch (ctrl->value) {
	case BLUR_LEVEL_0:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_pretty_default, \
			sizeof(s5k5bafx_vt_pretty_default) / \
			sizeof(s5k5bafx_vt_pretty_default[0]));
		break;
	case BLUR_LEVEL_1:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_pretty_1, \
			sizeof(s5k5bafx_vt_pretty_1) / \
			sizeof(s5k5bafx_vt_pretty_1[0]));
		break;
	case BLUR_LEVEL_2:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_pretty_2, \
			sizeof(s5k5bafx_vt_pretty_2) / \
			sizeof(s5k5bafx_vt_pretty_2[0]));
		break;
	case BLUR_LEVEL_3:
	case BLUR_LEVEL_MAX:
		err = s5k5bafx_write_regs(sd, s5k5bafx_vt_pretty_3, \
			sizeof(s5k5bafx_vt_pretty_3) / \
			sizeof(s5k5bafx_vt_pretty_3[0]));
		break;
	default:
		cam_err("ERR: Invalid blur(%d)\n", ctrl->value);
		return err;
		break;
	}
#endif

	if (unlikely(err < 0)) {
		cam_err("ERR: i2c_write for set blur\n");
		return -EIO;
	}

	return 0;
}

static int s5k5bafx_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	/* struct i2c_client *client = v4l2_get_subdevdata(sd); */
	struct s5k5bafx_state *state = to_state(sd);
	int err = 0;

	cam_info("ctrl->id : %d, value=%d\n", ctrl->id - V4L2_CID_PRIVATE_BASE,
	ctrl->value);

	if ((ctrl->id != V4L2_CID_CAMERA_CHECK_DATALINE)
	&& (ctrl->id != V4L2_CID_CAMERA_SENSOR_MODE)
	&& ((ctrl->id != V4L2_CID_CAMERA_VT_MODE))
	&& (ctrl->id != V4L2_CID_CAMERA_ANTI_BANDING)
	&& (!state->initialized)) {
		cam_warn("camera isn't initialized\n");
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_CAM_PREVIEW_ONOFF:
		if (ctrl->value)
			err = s5k5bafx_set_preview_start(sd);
		else
			err = s5k5bafx_set_preview_stop(sd);
		cam_dbg("V4L2_CID_CAM_PREVIEW_ONOFF [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAM_CAPTURE:
		err = s5k5bafx_set_capture_start(sd);
		cam_dbg("V4L2_CID_CAM_CAPTURE\n");
		break;

	case V4L2_CID_CAMERA_BRIGHTNESS:
		err = s5k5bafx_set_brightness(sd, ctrl);
		cam_dbg("V4L2_CID_CAMERA_BRIGHTNESS [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_VGA_BLUR:
		err = s5k5bafx_set_blur(sd, ctrl);
		cam_dbg("V4L2_CID_CAMERA_VGA_BLUR [%d]\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_VT_MODE:
		state->vt_mode = ctrl->value;
		break;

	case V4L2_CID_CAMERA_ANTI_BANDING:
		state->anti_banding = ctrl->value;
		cam_dbg("V4L2_CID_CAMERA_ANTI_BANDING [%d],[%d]\n",
				state->anti_banding, ctrl->value);
		err = 0;
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE:
		state->check_dataline = ctrl->value;
		cam_dbg("check_dataline = %d\n", state->check_dataline);
		err = 0;
		break;

	case V4L2_CID_CAMERA_SENSOR_MODE:
		err = s5k5bafx_set_sensor_mode(sd, ctrl);
		cam_dbg("sensor_mode = %d\n", ctrl->value);
		break;

	case V4L2_CID_CAMERA_CHECK_DATALINE_STOP:
		cam_dbg("do nothing\n");
		/*err = s5k5bafx_check_dataline_stop(sd);*/
		break;

	case V4L2_CID_CAMERA_CHECK_ESD:
		err = s5k5bafx_check_esd(sd);
		break;

	case V4L2_CID_CAMERA_FRAME_RATE:
		cam_dbg("do nothing\n");
		break;

	case V4L2_CID_CAMERA_CHECK_SENSOR_STATUS:
		s5k5bafx_debug_sensor_status(sd);
		err = s5k5bafx_check_sensor_status(sd);
		break;

	default:
		cam_err("ERR(ENOIOCTLCMD)\n");
		/* no errors return.*/
		break;
	}

	cam_dbg("X\n");
	return err;
}

static const struct v4l2_subdev_core_ops s5k5bafx_core_ops = {
	.init = s5k5bafx_init,		/* initializing API */
#if 0
	.queryctrl = s5k5bafx_queryctrl,
	.querymenu = s5k5bafx_querymenu,
#endif
	.g_ctrl = s5k5bafx_g_ctrl,
	.s_ctrl = s5k5bafx_s_ctrl,
};

static const struct v4l2_subdev_video_ops s5k5bafx_video_ops = {
	/*.s_crystal_freq = s5k5bafx_s_crystal_freq,*/
	.s_mbus_fmt	= s5k5bafx_s_fmt,
	.s_stream = s5k5bafx_s_stream,
	.enum_framesizes = s5k5bafx_enum_framesizes,
	/*.enum_frameintervals = s5k5bafx_enum_frameintervals,*/
	/*.enum_fmt = s5k5bafx_enum_fmt,*/
	.g_parm	= s5k5bafx_g_parm,
	.s_parm	= s5k5bafx_s_parm,
};

static const struct v4l2_subdev_ops s5k5bafx_ops = {
	.core = &s5k5bafx_core_ops,
	.video = &s5k5bafx_video_ops,
};

ssize_t s5k5bafx_camera_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *cam_type;
	cam_info("%s\n", __func__);

	cam_type = "SLSI_S5K5BAFX";

	return sprintf(buf, "%s\n", cam_type);
}

static DEVICE_ATTR(front_camtype, S_IRUGO, s5k5bafx_camera_type_show, NULL);

ssize_t s5k5bafx_startup_time_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	char *cam_type;
	cam_info("%s\n", __func__);

	return sprintf(buf, "%d\n", SMARTSTAY_STARTUP_TIME);
}

static DEVICE_ATTR(startup_time, S_IRUGO, s5k5bafx_startup_time_show, NULL);

static struct device *s5k5bafx_sysdev;

static int s5k5bafx_create_sysfs(void)
{
	cam_dbg("%s\n", __func__);

	s5k5bafx_sysdev = device_create(camera_class, NULL,
				MKDEV(CAM_MAJOR, 1), NULL, "front");
	if (IS_ERR(s5k5bafx_sysdev)) {
		cam_err("failed to create device s5k5bafx_dev!\n");
		return 0;
	}

	if (device_create_file(s5k5bafx_sysdev, &dev_attr_front_camtype) < 0) {
		cam_err("failed to create device file, %s\n",
				dev_attr_front_camtype.attr.name);
	}

	if (device_create_file(s5k5bafx_sysdev, &dev_attr_startup_time) < 0) {
		cam_err("failed to create device file, %s\n",
				dev_attr_startup_time.attr.name);
	}

	return 0;
}

static int s5k5bafx_remove_sysfs(void)
{
	device_remove_file(s5k5bafx_sysdev, &dev_attr_front_camtype);
	device_remove_file(s5k5bafx_sysdev, &dev_attr_startup_time);
	device_destroy(camera_class, s5k5bafx_sysdev->devt);
	s5k5bafx_sysdev = NULL;

	return 0;
}

/*
 * s5k5bafx_probe
 * Fetching platform data is being done with s_config subdev call.
 * In probe routine, we just register subdev device
 */
static int s5k5bafx_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct s5k5bafx_state *state = NULL;
	struct v4l2_subdev *sd = NULL;
	struct s5k5bafx_platform_data *pdata = NULL;
	cam_dbg("E\n");

	state = kzalloc(sizeof(struct s5k5bafx_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	sd = &state->sd;
	strcpy(sd->name, S5K5BAFX_DRIVER_NAME);

	state->initialized = 0;
	state->req_fps = state->set_fps = 8;
	state->sensor_mode = SENSOR_CAMERA;

	pdata = client->dev.platform_data;

	if (!pdata) {
		cam_err("no platform data\n");
		return -ENODEV;
	}

	/* Registering subdev */
	v4l2_i2c_subdev_init(sd, client, &s5k5bafx_ops);

	/*
	 * Assign default format and resolution
	 * Use configured default information in platform data
	 * or without them, use default information in driver
	 */
	if (!(pdata->default_width && pdata->default_height)) {
		state->preview_frmsizes.width = DEFAULT_PREVIEW_WIDTH;
		state->preview_frmsizes.height = DEFAULT_PREVIEW_HEIGHT;
	} else {
		state->preview_frmsizes.width = pdata->default_width;
		state->preview_frmsizes.height = pdata->default_height;
	}
	state->capture_frmsizes.width = DEFAULT_CAPTURE_WIDTH;
	state->capture_frmsizes.height = DEFAULT_CAPTURE_HEIGHT;

	cam_dbg("preview_width: %d , preview_height: %d, "
	"capture_width: %d, capture_height: %d",
	state->preview_frmsizes.width, state->preview_frmsizes.height,
	state->capture_frmsizes.width, state->capture_frmsizes.height);

	state->req_fmt.width = state->preview_frmsizes.width;
	state->req_fmt.height = state->preview_frmsizes.height;

	if (!pdata->pixelformat)
		state->req_fmt.pixelformat = DEFAULT_FMT;
	else
		state->req_fmt.pixelformat = pdata->pixelformat;

	cam_dbg("probed!!\n");

	return 0;
}

static int s5k5bafx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k5bafx_state *state = to_state(sd);

	cam_dbg("E\n");

	state->initialized = 0;

	v4l2_device_unregister_subdev(sd);
	kfree(to_state(sd));

#ifdef CONFIG_LOAD_FILE
	if (testBuf) {
		large_file ? vfree(testBuf) : kfree(testBuf);
		large_file = 0;
		testBuf = NULL;
	}
#endif

	return 0;
}

static const struct i2c_device_id s5k5bafx_id[] = {
	{ S5K5BAFX_DRIVER_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, s5k5bafx_id);

static struct i2c_driver s5k5bafx_i2c_driver = {
	.driver = {
		.name	= S5K5BAFX_DRIVER_NAME,
	},
	.probe		= s5k5bafx_probe,
	.remove		= s5k5bafx_remove,
	.id_table	= s5k5bafx_id,
};

static int __init s5k5bafx_mod_init(void)
{
	cam_dbg("E\n");
	s5k5bafx_create_sysfs();
	return i2c_add_driver(&s5k5bafx_i2c_driver);
}

static void __exit s5k5bafx_mod_exit(void)
{
	cam_dbg("E\n");
	s5k5bafx_remove_sysfs();
	i2c_del_driver(&s5k5bafx_i2c_driver);
}
module_init(s5k5bafx_mod_init);
module_exit(s5k5bafx_mod_exit);

MODULE_DESCRIPTION("S5K5BAFX ISP driver");
MODULE_AUTHOR("DongSeong Lim<dongseong.lim@samsung.com>");
MODULE_LICENSE("GPL");
