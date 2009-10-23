/*
 * vpx3220a, vpx3216b & vpx3214c video decoder driver version 0.0.1
 *
 * Copyright (C) 2001 Laurent Pinchart <lpinchart@freegates.be>
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
#include <linux/delay.h>
#include <linux/types.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-i2c-drv.h>

MODULE_DESCRIPTION("vpx3220a/vpx3216b/vpx3214c video decoder driver");
MODULE_AUTHOR("Laurent Pinchart");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0-1)");


#define VPX_TIMEOUT_COUNT  10

/* ----------------------------------------------------------------------- */

struct vpx3220 {
	struct v4l2_subdev sd;
	unsigned char reg[255];

	v4l2_std_id norm;
	int ident;
	int input;
	int enable;
	int bright;
	int contrast;
	int hue;
	int sat;
};

static inline struct vpx3220 *to_vpx3220(struct v4l2_subdev *sd)
{
	return container_of(sd, struct vpx3220, sd);
}

static char *inputs[] = { "internal", "composite", "svideo" };

/* ----------------------------------------------------------------------- */

static inline int vpx3220_write(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct vpx3220 *decoder = i2c_get_clientdata(client);

	decoder->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

static inline int vpx3220_read(struct v4l2_subdev *sd, u8 reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return i2c_smbus_read_byte_data(client, reg);
}

static int vpx3220_fp_status(struct v4l2_subdev *sd)
{
	unsigned char status;
	unsigned int i;

	for (i = 0; i < VPX_TIMEOUT_COUNT; i++) {
		status = vpx3220_read(sd, 0x29);

		if (!(status & 4))
			return 0;

		udelay(10);

		if (need_resched())
			cond_resched();
	}

	return -1;
}

static int vpx3220_fp_write(struct v4l2_subdev *sd, u8 fpaddr, u16 data)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	/* Write the 16-bit address to the FPWR register */
	if (i2c_smbus_write_word_data(client, 0x27, swab16(fpaddr)) == -1) {
		v4l2_dbg(1, debug, sd, "%s: failed\n", __func__);
		return -1;
	}

	if (vpx3220_fp_status(sd) < 0)
		return -1;

	/* Write the 16-bit data to the FPDAT register */
	if (i2c_smbus_write_word_data(client, 0x28, swab16(data)) == -1) {
		v4l2_dbg(1, debug, sd, "%s: failed\n", __func__);
		return -1;
	}

	return 0;
}

static u16 vpx3220_fp_read(struct v4l2_subdev *sd, u16 fpaddr)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	s16 data;

	/* Write the 16-bit address to the FPRD register */
	if (i2c_smbus_write_word_data(client, 0x26, swab16(fpaddr)) == -1) {
		v4l2_dbg(1, debug, sd, "%s: failed\n", __func__);
		return -1;
	}

	if (vpx3220_fp_status(sd) < 0)
		return -1;

	/* Read the 16-bit data from the FPDAT register */
	data = i2c_smbus_read_word_data(client, 0x28);
	if (data == -1) {
		v4l2_dbg(1, debug, sd, "%s: failed\n", __func__);
		return -1;
	}

	return swab16(data);
}

static int vpx3220_write_block(struct v4l2_subdev *sd, const u8 *data, unsigned int len)
{
	u8 reg;
	int ret = -1;

	while (len >= 2) {
		reg = *data++;
		ret = vpx3220_write(sd, reg, *data++);
		if (ret < 0)
			break;
		len -= 2;
	}

	return ret;
}

static int vpx3220_write_fp_block(struct v4l2_subdev *sd,
		const u16 *data, unsigned int len)
{
	u8 reg;
	int ret = 0;

	while (len > 1) {
		reg = *data++;
		ret |= vpx3220_fp_write(sd, reg, *data++);
		len -= 2;
	}

	return ret;
}

/* ---------------------------------------------------------------------- */

static const unsigned short init_ntsc[] = {
	0x1c, 0x00,		/* NTSC tint angle */
	0x88, 17,		/* Window 1 vertical */
	0x89, 240,		/* Vertical lines in */
	0x8a, 240,		/* Vertical lines out */
	0x8b, 000,		/* Horizontal begin */
	0x8c, 640,		/* Horizontal length */
	0x8d, 640,		/* Number of pixels */
	0x8f, 0xc00,		/* Disable window 2 */
	0xf0, 0x73,		/* 13.5 MHz transport, Forced
				 * mode, latch windows */
	0xf2, 0x13,		/* NTSC M, composite input */
	0xe7, 0x1e1,		/* Enable vertical standard
				 * locking @ 240 lines */
};

static const unsigned short init_pal[] = {
	0x88, 23,		/* Window 1 vertical begin */
	0x89, 288,		/* Vertical lines in (16 lines
				 * skipped by the VFE) */
	0x8a, 288,		/* Vertical lines out (16 lines
				 * skipped by the VFE) */
	0x8b, 16,		/* Horizontal begin */
	0x8c, 768,		/* Horizontal length */
	0x8d, 784, 		/* Number of pixels
				 * Must be >= Horizontal begin + Horizontal length */
	0x8f, 0xc00,		/* Disable window 2 */
	0xf0, 0x77,		/* 13.5 MHz transport, Forced
				 * mode, latch windows */
	0xf2, 0x3d1,		/* PAL B,G,H,I, composite input */
	0xe7, 0x241,		/* PAL/SECAM set to 288 lines */
};

static const unsigned short init_secam[] = {
	0x88, 23,		/* Window 1 vertical begin */
	0x89, 288,		/* Vertical lines in (16 lines
				 * skipped by the VFE) */
	0x8a, 288,		/* Vertical lines out (16 lines
				 * skipped by the VFE) */
	0x8b, 16,		/* Horizontal begin */
	0x8c, 768,		/* Horizontal length */
	0x8d, 784,		/* Number of pixels
				 * Must be >= Horizontal begin + Horizontal length */
	0x8f, 0xc00,		/* Disable window 2 */
	0xf0, 0x77,		/* 13.5 MHz transport, Forced
				 * mode, latch windows */
	0xf2, 0x3d5,		/* SECAM, composite input */
	0xe7, 0x241,		/* PAL/SECAM set to 288 lines */
};

static const unsigned char init_common[] = {
	0xf2, 0x00,		/* Disable all outputs */
	0x33, 0x0d,		/* Luma : VIN2, Chroma : CIN
				 * (clamp off) */
	0xd8, 0xa8,		/* HREF/VREF active high, VREF
				 * pulse = 2, Odd/Even flag */
	0x20, 0x03,		/* IF compensation 0dB/oct */
	0xe0, 0xff,		/* Open up all comparators */
	0xe1, 0x00,
	0xe2, 0x7f,
	0xe3, 0x80,
	0xe4, 0x7f,
	0xe5, 0x80,
	0xe6, 0x00,		/* Brightness set to 0 */
	0xe7, 0xe0,		/* Contrast to 1.0, noise shaping
				 * 10 to 8 2-bit error diffusion */
	0xe8, 0xf8,		/* YUV422, CbCr binary offset,
				 * ... (p.32) */
	0xea, 0x18,		/* LLC2 connected, output FIFO
				 * reset with VACTintern */
	0xf0, 0x8a,		/* Half full level to 10, bus
				 * shuffler [7:0, 23:16, 15:8] */
	0xf1, 0x18,		/* Single clock, sync mode, no
				 * FE delay, no HLEN counter */
	0xf8, 0x12,		/* Port A, PIXCLK, HF# & FE#
				 * strength to 2 */
	0xf9, 0x24,		/* Port B, HREF, VREF, PREF &
				 * ALPHA strength to 4 */
};

static const unsigned short init_fp[] = {
	0x59, 0,
	0xa0, 2070,		/* ACC reference */
	0xa3, 0,
	0xa4, 0,
	0xa8, 30,
	0xb2, 768,
	0xbe, 27,
	0x58, 0,
	0x26, 0,
	0x4b, 0x298,		/* PLL gain */
};


static int vpx3220_init(struct v4l2_subdev *sd, u32 val)
{
	struct vpx3220 *decoder = to_vpx3220(sd);

	vpx3220_write_block(sd, init_common, sizeof(init_common));
	vpx3220_write_fp_block(sd, init_fp, sizeof(init_fp) >> 1);
	if (decoder->norm & V4L2_STD_NTSC)
		vpx3220_write_fp_block(sd, init_ntsc, sizeof(init_ntsc) >> 1);
	else if (decoder->norm & V4L2_STD_PAL)
		vpx3220_write_fp_block(sd, init_pal, sizeof(init_pal) >> 1);
	else if (decoder->norm & V4L2_STD_SECAM)
		vpx3220_write_fp_block(sd, init_secam, sizeof(init_secam) >> 1);
	else
		vpx3220_write_fp_block(sd, init_pal, sizeof(init_pal) >> 1);
	return 0;
}

static int vpx3220_status(struct v4l2_subdev *sd, u32 *pstatus, v4l2_std_id *pstd)
{
	int res = V4L2_IN_ST_NO_SIGNAL, status;
	v4l2_std_id std = 0;

	status = vpx3220_fp_read(sd, 0x0f3);

	v4l2_dbg(1, debug, sd, "status: 0x%04x\n", status);

	if (status < 0)
		return status;

	if ((status & 0x20) == 0) {
		res = 0;

		switch (status & 0x18) {
		case 0x00:
		case 0x10:
		case 0x14:
		case 0x18:
			std = V4L2_STD_PAL;
			break;

		case 0x08:
			std = V4L2_STD_SECAM;
			break;

		case 0x04:
		case 0x0c:
		case 0x1c:
			std = V4L2_STD_NTSC;
			break;
		}
	}
	if (pstd)
		*pstd = std;
	if (pstatus)
		*pstatus = status;
	return 0;
}

static int vpx3220_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	v4l2_dbg(1, debug, sd, "querystd\n");
	return vpx3220_status(sd, NULL, std);
}

static int vpx3220_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	v4l2_dbg(1, debug, sd, "g_input_status\n");
	return vpx3220_status(sd, status, NULL);
}

static int vpx3220_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct vpx3220 *decoder = to_vpx3220(sd);
	int temp_input;

	/* Here we back up the input selection because it gets
	   overwritten when we fill the registers with the
	   choosen video norm */
	temp_input = vpx3220_fp_read(sd, 0xf2);

	v4l2_dbg(1, debug, sd, "s_std %llx\n", (unsigned long long)std);
	if (std & V4L2_STD_NTSC) {
		vpx3220_write_fp_block(sd, init_ntsc, sizeof(init_ntsc) >> 1);
		v4l2_dbg(1, debug, sd, "norm switched to NTSC\n");
	} else if (std & V4L2_STD_PAL) {
		vpx3220_write_fp_block(sd, init_pal, sizeof(init_pal) >> 1);
		v4l2_dbg(1, debug, sd, "norm switched to PAL\n");
	} else if (std & V4L2_STD_SECAM) {
		vpx3220_write_fp_block(sd, init_secam, sizeof(init_secam) >> 1);
		v4l2_dbg(1, debug, sd, "norm switched to SECAM\n");
	} else {
		return -EINVAL;
	}

	decoder->norm = std;

	/* And here we set the backed up video input again */
	vpx3220_fp_write(sd, 0xf2, temp_input | 0x0010);
	udelay(10);
	return 0;
}

static int vpx3220_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	int data;

	/* RJ:   input = 0: ST8 (PCTV) input
		 input = 1: COMPOSITE  input
		 input = 2: SVHS       input  */

	const int input_vals[3][2] = {
		{0x0c, 0},
		{0x0d, 0},
		{0x0e, 1}
	};

	if (input > 2)
		return -EINVAL;

	v4l2_dbg(1, debug, sd, "input switched to %s\n", inputs[input]);

	vpx3220_write(sd, 0x33, input_vals[input][0]);

	data = vpx3220_fp_read(sd, 0xf2) & ~(0x0020);
	if (data < 0)
		return data;
	/* 0x0010 is required to latch the setting */
	vpx3220_fp_write(sd, 0xf2,
			data | (input_vals[input][1] << 5) | 0x0010);

	udelay(10);
	return 0;
}

static int vpx3220_s_stream(struct v4l2_subdev *sd, int enable)
{
	v4l2_dbg(1, debug, sd, "s_stream %s\n", enable ? "on" : "off");

	vpx3220_write(sd, 0xf2, (enable ? 0x1b : 0x00));
	return 0;
}

static int vpx3220_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_BRIGHTNESS:
		v4l2_ctrl_query_fill(qc, -128, 127, 1, 0);
		break;

	case V4L2_CID_CONTRAST:
		v4l2_ctrl_query_fill(qc, 0, 63, 1, 32);
		break;

	case V4L2_CID_SATURATION:
		v4l2_ctrl_query_fill(qc, 0, 4095, 1, 2048);
		break;

	case V4L2_CID_HUE:
		v4l2_ctrl_query_fill(qc, -512, 511, 1, 0);
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int vpx3220_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct vpx3220 *decoder = to_vpx3220(sd);

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

static int vpx3220_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct vpx3220 *decoder = to_vpx3220(sd);

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		if (decoder->bright != ctrl->value) {
			decoder->bright = ctrl->value;
			vpx3220_write(sd, 0xe6, decoder->bright);
		}
		break;
	case V4L2_CID_CONTRAST:
		if (decoder->contrast != ctrl->value) {
			/* Bit 7 and 8 is for noise shaping */
			decoder->contrast = ctrl->value;
			vpx3220_write(sd, 0xe7, decoder->contrast + 192);
		}
		break;
	case V4L2_CID_SATURATION:
		if (decoder->sat != ctrl->value) {
			decoder->sat = ctrl->value;
			vpx3220_fp_write(sd, 0xa0, decoder->sat);
		}
		break;
	case V4L2_CID_HUE:
		if (decoder->hue != ctrl->value) {
			decoder->hue = ctrl->value;
			vpx3220_fp_write(sd, 0x1c, decoder->hue);
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vpx3220_g_chip_ident(struct v4l2_subdev *sd, struct v4l2_dbg_chip_ident *chip)
{
	struct vpx3220 *decoder = to_vpx3220(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, decoder->ident, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops vpx3220_core_ops = {
	.g_chip_ident = vpx3220_g_chip_ident,
	.init = vpx3220_init,
	.g_ctrl = vpx3220_g_ctrl,
	.s_ctrl = vpx3220_s_ctrl,
	.queryctrl = vpx3220_queryctrl,
	.s_std = vpx3220_s_std,
};

static const struct v4l2_subdev_video_ops vpx3220_video_ops = {
	.s_routing = vpx3220_s_routing,
	.s_stream = vpx3220_s_stream,
	.querystd = vpx3220_querystd,
	.g_input_status = vpx3220_g_input_status,
};

static const struct v4l2_subdev_ops vpx3220_ops = {
	.core = &vpx3220_core_ops,
	.video = &vpx3220_video_ops,
};

/* -----------------------------------------------------------------------
 * Client management code
 */

static int vpx3220_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct vpx3220 *decoder;
	struct v4l2_subdev *sd;
	const char *name = NULL;
	u8 ver;
	u16 pn;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter,
		I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	decoder = kzalloc(sizeof(struct vpx3220), GFP_KERNEL);
	if (decoder == NULL)
		return -ENOMEM;
	sd = &decoder->sd;
	v4l2_i2c_subdev_init(sd, client, &vpx3220_ops);
	decoder->norm = V4L2_STD_PAL;
	decoder->input = 0;
	decoder->enable = 1;
	decoder->bright = 32768;
	decoder->contrast = 32768;
	decoder->hue = 32768;
	decoder->sat = 32768;

	ver = i2c_smbus_read_byte_data(client, 0x00);
	pn = (i2c_smbus_read_byte_data(client, 0x02) << 8) +
		i2c_smbus_read_byte_data(client, 0x01);
	decoder->ident = V4L2_IDENT_VPX3220A;
	if (ver == 0xec) {
		switch (pn) {
		case 0x4680:
			name = "vpx3220a";
			break;
		case 0x4260:
			name = "vpx3216b";
			decoder->ident = V4L2_IDENT_VPX3216B;
			break;
		case 0x4280:
			name = "vpx3214c";
			decoder->ident = V4L2_IDENT_VPX3214C;
			break;
		}
	}
	if (name)
		v4l2_info(sd, "%s found @ 0x%x (%s)\n", name,
			client->addr << 1, client->adapter->name);
	else
		v4l2_info(sd, "chip (%02x:%04x) found @ 0x%x (%s)\n",
			ver, pn, client->addr << 1, client->adapter->name);

	vpx3220_write_block(sd, init_common, sizeof(init_common));
	vpx3220_write_fp_block(sd, init_fp, sizeof(init_fp) >> 1);
	/* Default to PAL */
	vpx3220_write_fp_block(sd, init_pal, sizeof(init_pal) >> 1);
	return 0;
}

static int vpx3220_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_vpx3220(sd));
	return 0;
}

static const struct i2c_device_id vpx3220_id[] = {
	{ "vpx3220a", 0 },
	{ "vpx3216b", 0 },
	{ "vpx3214c", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, vpx3220_id);

static struct v4l2_i2c_driver_data v4l2_i2c_data = {
	.name = "vpx3220",
	.probe = vpx3220_probe,
	.remove = vpx3220_remove,
	.id_table = vpx3220_id,
};
