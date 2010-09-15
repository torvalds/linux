/*
 * saa7110 - Philips SAA7110(A) video decoder driver
 *
 * Copyright (C) 1998 Pauline Middelink <middelin@polyware.nl>
 *
 * Copyright (C) 1999 Wolfgang Scherr <scherr@net4you.net>
 * Copyright (C) 2000 Serguei Miridonov <mirsev@cicese.mx>
 *    - some corrections for Pinnacle Systems Inc. DC10plus card.
 *
 * Changes by Ronald Bultje <rbultje@ronald.bitfreak.net>
 *    - moved over to linux>=2.4.x i2c protocol (1/1/2003)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

MODULE_DESCRIPTION("Philips SAA7110 video decoder driver");
MODULE_AUTHOR("Pauline Middelink");
MODULE_LICENSE("GPL");


static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");

#define SAA7110_MAX_INPUT	9	/* 6 CVBS, 3 SVHS */
#define SAA7110_MAX_OUTPUT	1	/* 1 YUV */

#define SAA7110_NR_REG		0x35

struct saa7110 {
	struct v4l2_subdev sd;
	u8 reg[SAA7110_NR_REG];

	v4l2_std_id norm;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;

	wait_queue_head_t wq;
};

static inline struct saa7110 *to_saa7110(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa7110, sd);
}

/* ----------------------------------------------------------------------- */
/* I2C support functions						   */
/* ----------------------------------------------------------------------- */

static int saa7110_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct saa7110 *decoder = to_saa7110(sd);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int saa7110_write_block(struct v4l2_subdev *sd, const u8 *data, unsigned int len)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct saa7110 *decoder = to_saa7110(sd);
	int ret = -1;
	u8 reg = *data;		/* first register to write to */

	/* Sanity check */
	if (reg + (len - 1) > SAA7110_NR_REG)
		return ret;

	/* the saa7110 has an autoincrement function, use it if
	 * the adapter understands raw I2C */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		ret = i2c_master_send(client, data, len);

		/* Cache the written data */
		memcpy(decoder->reg + reg, data + 1, len - 1);
	} else {
		for (++data, --len; len; len--) {
			ret = saa7110_write(sd, reg++, *data++);
			if (ret < 0)
				break;
		}
	}

	return ret;
}

static inline int saa7110_read(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte(client);
}

/* ----------------------------------------------------------------------- */
/* SAA7110 functions							   */
/* ----------------------------------------------------------------------- */

#define FRESP_06H_COMPST 0x03	/*0x13*/
#define FRESP_06H_SVIDEO 0x83	/*0xC0*/


static int saa7110_selmux(struct v4l2_subdev *sd, int chan)
{
	static const unsigned char modes[9][8] = {
		/* mode 0 */
		{FRESP_06H_COMPST, 0xD9, 0x17, 0x40, 0x03,
			      0x44, 0x75, 0x16},
		/* mode 1 */
		{FRESP_06H_COMPST, 0xD8, 0x17, 0x40, 0x03,
			      0x44, 0x75, 0x16},
		/* mode 2 */
		{FRESP_06H_COMPST, 0xBA, 0x07, 0x91, 0x03,
			      0x60, 0xB5, 0x05},
		/* mode 3 */
		{FRESP_06H_COMPST, 0xB8, 0x07, 0x91, 0x03,
			      0x60, 0xB5, 0x05},
		/* mode 4 */
		{FRESP_06H_COMPST, 0x7C, 0x07, 0xD2, 0x83,
			      0x60, 0xB5, 0x03},
		/* mode 5 */
		{FRESP_06H_COMPST, 0x78, 0x07, 0xD2, 0x83,
			      0x60, 0xB5, 0x03},
		/* mode 6 */
		{FRESP_06H_SVIDEO, 0x59, 0x17, 0x42, 0xA3,
			      0x44, 0x75, 0x12},
		/* mode 7 */
		{FRESP_06H_SVIDEO, 0x9A, 0x17, 0xB1, 0x13,
			      0x60, 0xB5, 0x14},
		/* mode 8 */
		{FRESP_06H_SVIDEO, 0x3C, 0x27, 0xC1, 0x23,
			      0x44, 0x75, 0x21}
	};
	struct saa7110 *decoder = to_saa7110(sd);
	const unsigned char *ptr = modes[chan];

	saa7110_write(sd, 0x06, ptr[0]);	/* Luminance control    */
	saa7110_write(sd, 0x20, ptr[1]);	/* Analog Control #1    */
	saa7110_write(sd, 0x21, ptr[2]);	/* Analog Control #2    */
	saa7110_write(sd, 0x22, ptr[3]);	/* Mixer Control #1     */
	saa7110_write(sd, 0x2C, ptr[4]);	/* Mixer Control #2     */
	saa7110_write(sd, 0x30, ptr[5]);	/* ADCs gain control    */
	saa7110_write(sd, 0x31, ptr[6]);	/* Mixer Control #3     */
	saa7110_write(sd, 0x21, ptr[7]);	/* Analog Control #2    */
	decoder->input = chan;

	return 0;
}

static const unsigned char initseq[1 + SAA7110_NR_REG] = {
	0, 0x4C, 0x3C, 0x0D, 0xEF, 0xBD, 0xF2, 0x03, 0x00,
	/* 0x08 */ 0xF8, 0xF8, 0x60, 0x60, 0x00, 0x86, 0x18, 0x90,
	/* 0x10 */ 0x00, 0x59, 0x40, 0x46, 0x42, 0x1A, 0xFF, 0xDA,
	/* 0x18 */ 0xF2, 0x8B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 0x20 */ 0xD9, 0x16, 0x40, 0x41, 0x80, 0x41, 0x80, 0x4F,
	/* 0x28 */ 0xFE, 0x01, 0xCF, 0x0F, 0x03, 0x01, 0x03, 0x0C,
	/* 0x30 */ 0x44, 0x71, 0x02, 0x8C, 0x02
};

static v4l2_std_id determine_norm(struct v4l2_subdev *sd)
{
	DEFINE_WAIT(wait);
	struct saa7110 *decoder = to_saa7110(sd);
	int status;

	/* mode changed, start automatic detection */
	saa7110_write_block(sd, initseq, sizeof(initseq));
	saa7110_selmux(sd, decoder->input);
	prepare_to_wait(&decoder->wq, &wait, TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(250));
	finish_wait(&decoder->wq, &wait);
	status = saa7110_read(sd);
	if (status & 0x40) {
		v4l2_dbg(1, debug, sd, "status=0x%02x (no signal)\n", status);
		return decoder->norm;	/* no change*/
	}
	if ((status & 3) == 0) {
		saa7110_write(sd, 0x06, 0x83);
		if (status & 0x20) {
			v4l2_dbg(1, debug, sd, "status=0x%02x (NTSC/no color)\n", status);
			/*saa7110_write(sd,0x2E,0x81);*/
			return V4L2_STD_NTSC;
		}
		v4l2_dbg(1, debug, sd, "status=0x%02x (PAL/no color)\n", status);
		/*saa7110_write(sd,0x2E,0x9A);*/
		return V4L2_STD_PAL;
	}
	/*saa7110_write(sd,0x06,0x03);*/
	if (status & 0x20) {	/* 60Hz */
		v4l2_dbg(1, debug, sd, "status=0x%02x (NTSC)\n", status);
		saa7110_write(sd, 0x0D, 0x86);
		saa7110_write(sd, 0x0F, 0x50);
		saa7110_write(sd, 0x11, 0x2C);
		/*saa7110_write(sd,0x2E,0x81);*/
		return V4L2_STD_NTSC;
	}

	/* 50Hz -> PAL/SECAM */
	saa7110_write(sd, 0x0D, 0x86);
	saa7110_write(sd, 0x0F, 0x10);
	saa7110_write(sd, 0x11, 0x59);
	/*saa7110_write(sd,0x2E,0x9A);*/

	prepare_to_wait(&decoder->wq, &wait, TASK_UNINTERRUPTIBLE);
	schedule_timeout(msecs_to_jiffies(250));
	finish_wait(&decoder->wq, &wait);

	status = saa7110_read(sd);
	if ((status & 0x03) == 0x01) {
		v4l2_dbg(1, debug, sd, "status=0x%02x (SECAM)\n", status);
		saa7110_write(sd, 0x0D, 0x87);
		return V4L2_STD_SECAM;
	}
	v4l2_dbg(1, debug, sd, "status=0x%02x (PAL)\n", status);
	return V4L2_STD_PAL;
}

static int saa7110_g_input_status(struct v4l2_subdev *sd, u32 *pstatus)
{
	struct saa7110 *decoder = to_saa7110(sd);
	int res = V4L2_IN_ST_NO_SIGNAL;
	int status = saa7110_read(sd);

	v4l2_dbg(1, debug, sd, "status=0x%02x norm=%llx\n",
		       status, (unsigned long long)decoder->norm);
	if (!(status & 0x40))
		res = 0;
	if (!(status & 0x03))
		res |= V4L2_IN_ST_NO_COLOR;

	*pstatus = res;
	return 0;
}

static int saa7110_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	*(v4l2_std_id *)std = determine_norm(sd);
	return 0;
}

static int saa7110_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct saa7110 *decoder = to_saa7110(sd);

	if (decoder->norm != std) {
		decoder->norm = std;
		/*saa7110_write(sd, 0x06, 0x03);*/
		if (std & V4L2_STD_NTSC) {
			saa7110_write(sd, 0x0D, 0x86);
			saa7110_write(sd, 0x0F, 0x50);
			saa7110_write(sd, 0x11, 0x2C);
			/*saa7110_write(sd, 0x2E, 0x81);*/
			v4l2_dbg(1, debug, sd, "switched to NTSC\n");
		} else if (std & V4L2_STD_PAL) {
			saa7110_write(sd, 0x0D, 0x86);
			saa7110_write(sd, 0x0F, 0x10);
			saa7110_write(sd, 0x11, 0x59);
			/*saa7110_write(sd, 0x2E, 0x9A);*/
			v4l2_dbg(1, debug, sd, "switched to PAL\n");
		} else if (std & V4L2_STD_SECAM) {
			saa7110_write(sd, 0x0D, 0x87);
			saa7110_write(sd, 0x0F, 0x10);
			saa7110_write(sd, 0x11, 0x59);
			/*saa7110_write(sd, 0x2E, 0x9A);*/
			v4l2_dbg(1, debug, sd, "switched to SECAM\n");
		} else {
			return -EINVAL;
		}
	}
	return 0;
}

static int saa7110_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct saa7110 *decoder = to_saa7110(sd);

	if (input >= SAA7110_MAX_INPUT) {
		v4l2_dbg(1, debug, sd, "input=%d not available\n", input);
		return -EINVAL;
	}
	if (decoder->input != input) {
		saa7110_selmux(sd, input);
		v4l2_dbg(1, debug, sd, "switched to input=%d\n", input);
	}
	return 0;
}

static int saa7110_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct saa7110 *decoder = to_saa7110(sd);

	if (decoder->enable != enable) {
		decoder->enable = enable;
		saa7110_write(sd, 0x0E, enable ? 0x18 : 0x80);
		v4l2_dbg(1, debug, sd, "YUV %s\n", enable ? "on" : "off");
	}
	return 0;
}

static int saa7110_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		return v4l2_ctrl_query_fill(qc, 0, 255, 1, 128);
	case V4L2_CID_CONTRAST:
	case V4L2_CID_SATURATION:
		return v4l2_ctrl_query_fill(qc, 0, 127, 1, 64);
	case V4L2_CID_HUE:
		return v4l2_ctrl_query_fill(qc, -128, 127, 1, 0);
	default:
		return -EINVAL;
	}
	return 0;
}

static int saa7110_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct saa7110 *decoder = to_saa7110(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ctrl->value = decoder->bright;
		break;
	case V4L2_CID_CONTRAST:
		ctrl->value = decoder->contrast;
		break;
	case V4L2_CID_SATURATION:
		ctrl->value = decoder->sat;
		break;
	case V4L2_CID_HUE:
		ctrl->value = decoder->hue;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int saa7110_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct saa7110 *decoder = to_saa7110(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (decoder->bright != ctrl->value) {
			decoder->bright = ctrl->value;
			saa7110_write(sd, 0x19, decoder->bright);
		}
		break;
	case V4L2_CID_CONTRAST:
		if (decoder->contrast != ctrl->value) {
			decoder->contrast = ctrl->value;
			saa7110_write(sd, 0x13, decoder->contrast);
		}
		break;
	case V4L2_CID_SATURATION:
		if (decoder->sat != ctrl->value) {
			decoder->sat = ctrl->value;
			saa7110_write(sd, 0x12, decoder->sat);
		}
		break;
	case V4L2_CID_HUE:
		if (decoder->hue != ctrl->value) {
			decoder->hue = ctrl->value;
			saa7110_write(sd, 0x07, decoder->hue);
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int saa7110_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SAA7110, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops saa7110_core_ops = {
	.g_chip_ident = saa7110_g_chip_ident,
	.g_ctrl = saa7110_g_ctrl,
	.s_ctrl = saa7110_s_ctrl,
	.queryctrl = saa7110_queryctrl,
	.s_std = saa7110_s_std,
};

static const struct v4l2_subdev_video_ops saa7110_video_ops = {
	.s_routing = saa7110_s_routing,
	.s_stream = saa7110_s_stream,
	.querystd = saa7110_querystd,
	.g_input_status = saa7110_g_input_status,
};

static const struct v4l2_subdev_ops saa7110_ops = {
	.core = &saa7110_core_ops,
	.video = &saa7110_video_ops,
};

/* ----------------------------------------------------------------------- */

static int saa7110_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct saa7110 *decoder;
	struct v4l2_subdev *sd;
	int rv;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -ENODEV;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	decoder = kzalloc(sizeof(struct saa7110), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;
	sd = &decoder->sd;
	v4l2_i2c_subdev_init(sd, client, &saa7110_ops);
	decoder->norm = V4L2_STD_PAL;
	decoder->input = 0;
	decoder->enable = 1;
	decoder->bright = 32768;
	decoder->contrast = 32768;
	decoder->hue = 32768;
	decoder->sat = 32768;
	init_waitqueue_head(&decoder->wq);

	rv = saa7110_write_block(sd, initseq, sizeof(initseq));
	if (rv < 0) {
		v4l2_dbg(1, debug, sd, "init status %d\n", rv);
	} else {
		int ver, status;
		saa7110_write(sd, 0x21, 0x10);
		saa7110_write(sd, 0x0e, 0x18);
		saa7110_write(sd, 0x0D, 0x04);
		ver = saa7110_read(sd);
		saa7110_write(sd, 0x0D, 0x06);
		/*mdelay(150);*/
		status = saa7110_read(sd);
		v4l2_dbg(1, debug, sd, "version %x, status=0x%02x\n",
			       ver, status);
		saa7110_write(sd, 0x0D, 0x86);
		saa7110_write(sd, 0x0F, 0x10);
		saa7110_write(sd, 0x11, 0x59);
		/*saa7110_write(sd, 0x2E, 0x9A);*/
	}

	/*saa7110_selmux(sd,0);*/
	/*determine_norm(sd);*/
	/* setup and implicit mode 0 select has been performed */

	return 0;
}

static int saa7110_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_saa7110(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id saa7110_id[] = {
	{ "saa7110", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa7110_id);

static struct i2c_driver saa7110_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "saa7110",
	},
	.probe		= saa7110_probe,
	.remove		= saa7110_remove,
	.id_table	= saa7110_id,
};

static __init int init_saa7110(void)
{
	return i2c_add_driver(&saa7110_driver);
}

static __exit void exit_saa7110(void)
{
	i2c_del_driver(&saa7110_driver);
}

module_init(init_saa7110);
module_exit(exit_saa7110);
