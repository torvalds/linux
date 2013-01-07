/*
 *  saa7191.c - Philips SAA7191 video decoder driver
 *
 *  Copyright (C) 2003 Ladislav Michl <ladis@linux-mips.org>
 *  Copyright (C) 2004,2005 Mikael Nousiainen <tmnousia@cc.hut.fi>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

#include "saa7191.h"

#define SAA7191_MODULE_VERSION	"0.0.5"

MODULE_DESCRIPTION("Philips SAA7191 video decoder driver");
MODULE_VERSION(SAA7191_MODULE_VERSION);
MODULE_AUTHOR("Mikael Nousiainen <tmnousia@cc.hut.fi>");
MODULE_LICENSE("GPL");


// #define SAA7191_DEBUG

#ifdef SAA7191_DEBUG
#define dprintk(x...) printk("SAA7191: " x);
#else
#define dprintk(x...)
#endif

#define SAA7191_SYNC_COUNT	30
#define SAA7191_SYNC_DELAY	100	/* milliseconds */

struct saa7191 {
	struct v4l2_subdev sd;

	/* the register values are stored here as the actual
	 * I2C-registers are write-only */
	u8 reg[25];

	int input;
	v4l2_std_id norm;
};

static inline struct saa7191 *to_saa7191(struct v4l2_subdev *sd)
{
	return container_of(sd, struct saa7191, sd);
}

static const u8 initseq[] = {
	0,	/* Subaddress */

	0x50,	/* (0x50) SAA7191_REG_IDEL */

	/* 50 Hz signal timing */
	0x30,	/* (0x30) SAA7191_REG_HSYB */
	0x00,	/* (0x00) SAA7191_REG_HSYS */
	0xe8,	/* (0xe8) SAA7191_REG_HCLB */
	0xb6,	/* (0xb6) SAA7191_REG_HCLS */
	0xf4,	/* (0xf4) SAA7191_REG_HPHI */

	/* control */
	SAA7191_LUMA_APER_1,	/* (0x01) SAA7191_REG_LUMA - CVBS mode */
	0x00,	/* (0x00) SAA7191_REG_HUEC */
	0xf8,	/* (0xf8) SAA7191_REG_CKTQ */
	0xf8,	/* (0xf8) SAA7191_REG_CKTS */
	0x90,	/* (0x90) SAA7191_REG_PLSE */
	0x90,	/* (0x90) SAA7191_REG_SESE */
	0x00,	/* (0x00) SAA7191_REG_GAIN */
	SAA7191_STDC_NFEN | SAA7191_STDC_HRMV,	/* (0x0c) SAA7191_REG_STDC
						 * - not SECAM,
						 * slow time constant */
	SAA7191_IOCK_OEDC | SAA7191_IOCK_OEHS | SAA7191_IOCK_OEVS
	| SAA7191_IOCK_OEDY,	/* (0x78) SAA7191_REG_IOCK
				 * - chroma from CVBS, GPSW1 & 2 off */
	SAA7191_CTL3_AUFD | SAA7191_CTL3_SCEN | SAA7191_CTL3_OFTS
	| SAA7191_CTL3_YDEL0,	/* (0x99) SAA7191_REG_CTL3
				 * - automatic field detection */
	0x00,	/* (0x00) SAA7191_REG_CTL4 */
	0x2c,	/* (0x2c) SAA7191_REG_CHCV - PAL nominal value */
	0x00,	/* unused */
	0x00,	/* unused */

	/* 60 Hz signal timing */
	0x34,	/* (0x34) SAA7191_REG_HS6B */
	0x0a,	/* (0x0a) SAA7191_REG_HS6S */
	0xf4,	/* (0xf4) SAA7191_REG_HC6B */
	0xce,	/* (0xce) SAA7191_REG_HC6S */
	0xf4,	/* (0xf4) SAA7191_REG_HP6I */
};

/* SAA7191 register handling */

static u8 saa7191_read_reg(struct v4l2_subdev *sd, u8 reg)
{
	return to_saa7191(sd)->reg[reg];
}

static int saa7191_read_status(struct v4l2_subdev *sd, u8 *value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = i2c_master_recv(client, value, 1);
	if (ret < 0) {
		printk(KERN_ERR "SAA7191: saa7191_read_status(): read failed\n");
		return ret;
	}

	return 0;
}


static int saa7191_write_reg(struct v4l2_subdev *sd, u8 reg, u8 value)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	to_saa7191(sd)->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* the first byte of data must be the first subaddress number (register) */
static int saa7191_write_block(struct v4l2_subdev *sd,
			       u8 length, const u8 *data)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct saa7191 *decoder = to_saa7191(sd);
	int i;
	int ret;

	for (i = 0; i < (length - 1); i++) {
		decoder->reg[data[0] + i] = data[i + 1];
	}

	ret = i2c_master_send(client, data, length);
	if (ret < 0) {
		printk(KERN_ERR "SAA7191: saa7191_write_block(): "
		       "write failed\n");
		return ret;
	}

	return 0;
}

/* Helper functions */

static int saa7191_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct saa7191 *decoder = to_saa7191(sd);
	u8 luma = saa7191_read_reg(sd, SAA7191_REG_LUMA);
	u8 iock = saa7191_read_reg(sd, SAA7191_REG_IOCK);
	int err;

	switch (input) {
	case SAA7191_INPUT_COMPOSITE: /* Set Composite input */
		iock &= ~(SAA7191_IOCK_CHRS | SAA7191_IOCK_GPSW1
			  | SAA7191_IOCK_GPSW2);
		/* Chrominance trap active */
		luma &= ~SAA7191_LUMA_BYPS;
		break;
	case SAA7191_INPUT_SVIDEO: /* Set S-Video input */
		iock |= SAA7191_IOCK_CHRS | SAA7191_IOCK_GPSW2;
		/* Chrominance trap bypassed */
		luma |= SAA7191_LUMA_BYPS;
		break;
	default:
		return -EINVAL;
	}

	err = saa7191_write_reg(sd, SAA7191_REG_LUMA, luma);
	if (err)
		return -EIO;
	err = saa7191_write_reg(sd, SAA7191_REG_IOCK, iock);
	if (err)
		return -EIO;

	decoder->input = input;

	return 0;
}

static int saa7191_s_std(struct v4l2_subdev *sd, v4l2_std_id norm)
{
	struct saa7191 *decoder = to_saa7191(sd);
	u8 stdc = saa7191_read_reg(sd, SAA7191_REG_STDC);
	u8 ctl3 = saa7191_read_reg(sd, SAA7191_REG_CTL3);
	u8 chcv = saa7191_read_reg(sd, SAA7191_REG_CHCV);
	int err;

	if (norm & V4L2_STD_PAL) {
		stdc &= ~SAA7191_STDC_SECS;
		ctl3 &= ~(SAA7191_CTL3_AUFD | SAA7191_CTL3_FSEL);
		chcv = SAA7191_CHCV_PAL;
	} else if (norm & V4L2_STD_NTSC) {
		stdc &= ~SAA7191_STDC_SECS;
		ctl3 &= ~SAA7191_CTL3_AUFD;
		ctl3 |= SAA7191_CTL3_FSEL;
		chcv = SAA7191_CHCV_NTSC;
	} else if (norm & V4L2_STD_SECAM) {
		stdc |= SAA7191_STDC_SECS;
		ctl3 &= ~(SAA7191_CTL3_AUFD | SAA7191_CTL3_FSEL);
		chcv = SAA7191_CHCV_PAL;
	} else {
		return -EINVAL;
	}

	err = saa7191_write_reg(sd, SAA7191_REG_CTL3, ctl3);
	if (err)
		return -EIO;
	err = saa7191_write_reg(sd, SAA7191_REG_STDC, stdc);
	if (err)
		return -EIO;
	err = saa7191_write_reg(sd, SAA7191_REG_CHCV, chcv);
	if (err)
		return -EIO;

	decoder->norm = norm;

	dprintk("ctl3: %02x stdc: %02x chcv: %02x\n", ctl3,
		stdc, chcv);
	dprintk("norm: %llx\n", norm);

	return 0;
}

static int saa7191_wait_for_signal(struct v4l2_subdev *sd, u8 *status)
{
	int i = 0;

	dprintk("Checking for signal...\n");

	for (i = 0; i < SAA7191_SYNC_COUNT; i++) {
		if (saa7191_read_status(sd, status))
			return -EIO;

		if (((*status) & SAA7191_STATUS_HLCK) == 0) {
			dprintk("Signal found\n");
			return 0;
		}

		msleep(SAA7191_SYNC_DELAY);
	}

	dprintk("No signal\n");

	return -EBUSY;
}

static int saa7191_querystd(struct v4l2_subdev *sd, v4l2_std_id *norm)
{
	struct saa7191 *decoder = to_saa7191(sd);
	u8 stdc = saa7191_read_reg(sd, SAA7191_REG_STDC);
	u8 ctl3 = saa7191_read_reg(sd, SAA7191_REG_CTL3);
	u8 status;
	v4l2_std_id old_norm = decoder->norm;
	int err = 0;

	dprintk("SAA7191 extended signal auto-detection...\n");

	*norm = V4L2_STD_NTSC | V4L2_STD_PAL | V4L2_STD_SECAM;
	stdc &= ~SAA7191_STDC_SECS;
	ctl3 &= ~(SAA7191_CTL3_FSEL);

	err = saa7191_write_reg(sd, SAA7191_REG_STDC, stdc);
	if (err) {
		err = -EIO;
		goto out;
	}
	err = saa7191_write_reg(sd, SAA7191_REG_CTL3, ctl3);
	if (err) {
		err = -EIO;
		goto out;
	}

	ctl3 |= SAA7191_CTL3_AUFD;
	err = saa7191_write_reg(sd, SAA7191_REG_CTL3, ctl3);
	if (err) {
		err = -EIO;
		goto out;
	}

	msleep(SAA7191_SYNC_DELAY);

	err = saa7191_wait_for_signal(sd, &status);
	if (err)
		goto out;

	if (status & SAA7191_STATUS_FIDT) {
		/* 60Hz signal -> NTSC */
		dprintk("60Hz signal: NTSC\n");
		*norm = V4L2_STD_NTSC;
		return 0;
	}

	/* 50Hz signal */
	dprintk("50Hz signal: Trying PAL...\n");

	/* try PAL first */
	err = saa7191_s_std(sd, V4L2_STD_PAL);
	if (err)
		goto out;

	msleep(SAA7191_SYNC_DELAY);

	err = saa7191_wait_for_signal(sd, &status);
	if (err)
		goto out;

	/* not 50Hz ? */
	if (status & SAA7191_STATUS_FIDT) {
		dprintk("No 50Hz signal\n");
		saa7191_s_std(sd, old_norm);
		return -EAGAIN;
	}

	if (status & SAA7191_STATUS_CODE) {
		dprintk("PAL\n");
		*norm = V4L2_STD_PAL;
		return saa7191_s_std(sd, old_norm);
	}

	dprintk("No color detected with PAL - Trying SECAM...\n");

	/* no color detected ? -> try SECAM */
	err = saa7191_s_std(sd, V4L2_STD_SECAM);
	if (err)
		goto out;

	msleep(SAA7191_SYNC_DELAY);

	err = saa7191_wait_for_signal(sd, &status);
	if (err)
		goto out;

	/* not 50Hz ? */
	if (status & SAA7191_STATUS_FIDT) {
		dprintk("No 50Hz signal\n");
		err = -EAGAIN;
		goto out;
	}

	if (status & SAA7191_STATUS_CODE) {
		/* Color detected -> SECAM */
		dprintk("SECAM\n");
		*norm = V4L2_STD_SECAM;
		return saa7191_s_std(sd, old_norm);
	}

	dprintk("No color detected with SECAM - Going back to PAL.\n");

out:
	return saa7191_s_std(sd, old_norm);
}

static int saa7191_autodetect_norm(struct v4l2_subdev *sd)
{
	u8 status;

	dprintk("SAA7191 signal auto-detection...\n");

	dprintk("Reading status...\n");

	if (saa7191_read_status(sd, &status))
		return -EIO;

	dprintk("Checking for signal...\n");

	/* no signal ? */
	if (status & SAA7191_STATUS_HLCK) {
		dprintk("No signal\n");
		return -EBUSY;
	}

	dprintk("Signal found\n");

	if (status & SAA7191_STATUS_FIDT) {
		/* 60hz signal -> NTSC */
		dprintk("NTSC\n");
		return saa7191_s_std(sd, V4L2_STD_NTSC);
	} else {
		/* 50hz signal -> PAL */
		dprintk("PAL\n");
		return saa7191_s_std(sd, V4L2_STD_PAL);
	}
}

static int saa7191_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	u8 reg;
	int ret = 0;

	switch (ctrl->id) {
	case SAA7191_CONTROL_BANDPASS:
	case SAA7191_CONTROL_BANDPASS_WEIGHT:
	case SAA7191_CONTROL_CORING:
		reg = saa7191_read_reg(sd, SAA7191_REG_LUMA);
		switch (ctrl->id) {
		case SAA7191_CONTROL_BANDPASS:
			ctrl->value = ((s32)reg & SAA7191_LUMA_BPSS_MASK)
				>> SAA7191_LUMA_BPSS_SHIFT;
			break;
		case SAA7191_CONTROL_BANDPASS_WEIGHT:
			ctrl->value = ((s32)reg & SAA7191_LUMA_APER_MASK)
				>> SAA7191_LUMA_APER_SHIFT;
			break;
		case SAA7191_CONTROL_CORING:
			ctrl->value = ((s32)reg & SAA7191_LUMA_CORI_MASK)
				>> SAA7191_LUMA_CORI_SHIFT;
			break;
		}
		break;
	case SAA7191_CONTROL_FORCE_COLOUR:
	case SAA7191_CONTROL_CHROMA_GAIN:
		reg = saa7191_read_reg(sd, SAA7191_REG_GAIN);
		if (ctrl->id == SAA7191_CONTROL_FORCE_COLOUR)
			ctrl->value = ((s32)reg & SAA7191_GAIN_COLO) ? 1 : 0;
		else
			ctrl->value = ((s32)reg & SAA7191_GAIN_LFIS_MASK)
				>> SAA7191_GAIN_LFIS_SHIFT;
		break;
	case V4L2_CID_HUE:
		reg = saa7191_read_reg(sd, SAA7191_REG_HUEC);
		if (reg < 0x80)
			reg += 0x80;
		else
			reg -= 0x80;
		ctrl->value = (s32)reg;
		break;
	case SAA7191_CONTROL_VTRC:
		reg = saa7191_read_reg(sd, SAA7191_REG_STDC);
		ctrl->value = ((s32)reg & SAA7191_STDC_VTRC) ? 1 : 0;
		break;
	case SAA7191_CONTROL_LUMA_DELAY:
		reg = saa7191_read_reg(sd, SAA7191_REG_CTL3);
		ctrl->value = ((s32)reg & SAA7191_CTL3_YDEL_MASK)
			>> SAA7191_CTL3_YDEL_SHIFT;
		if (ctrl->value >= 4)
			ctrl->value -= 8;
		break;
	case SAA7191_CONTROL_VNR:
		reg = saa7191_read_reg(sd, SAA7191_REG_CTL4);
		ctrl->value = ((s32)reg & SAA7191_CTL4_VNOI_MASK)
			>> SAA7191_CTL4_VNOI_SHIFT;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int saa7191_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	u8 reg;
	int ret = 0;

	switch (ctrl->id) {
	case SAA7191_CONTROL_BANDPASS:
	case SAA7191_CONTROL_BANDPASS_WEIGHT:
	case SAA7191_CONTROL_CORING:
		reg = saa7191_read_reg(sd, SAA7191_REG_LUMA);
		switch (ctrl->id) {
		case SAA7191_CONTROL_BANDPASS:
			reg &= ~SAA7191_LUMA_BPSS_MASK;
			reg |= (ctrl->value << SAA7191_LUMA_BPSS_SHIFT)
				& SAA7191_LUMA_BPSS_MASK;
			break;
		case SAA7191_CONTROL_BANDPASS_WEIGHT:
			reg &= ~SAA7191_LUMA_APER_MASK;
			reg |= (ctrl->value << SAA7191_LUMA_APER_SHIFT)
				& SAA7191_LUMA_APER_MASK;
			break;
		case SAA7191_CONTROL_CORING:
			reg &= ~SAA7191_LUMA_CORI_MASK;
			reg |= (ctrl->value << SAA7191_LUMA_CORI_SHIFT)
				& SAA7191_LUMA_CORI_MASK;
			break;
		}
		ret = saa7191_write_reg(sd, SAA7191_REG_LUMA, reg);
		break;
	case SAA7191_CONTROL_FORCE_COLOUR:
	case SAA7191_CONTROL_CHROMA_GAIN:
		reg = saa7191_read_reg(sd, SAA7191_REG_GAIN);
		if (ctrl->id == SAA7191_CONTROL_FORCE_COLOUR) {
			if (ctrl->value)
				reg |= SAA7191_GAIN_COLO;
			else
				reg &= ~SAA7191_GAIN_COLO;
		} else {
			reg &= ~SAA7191_GAIN_LFIS_MASK;
			reg |= (ctrl->value << SAA7191_GAIN_LFIS_SHIFT)
				& SAA7191_GAIN_LFIS_MASK;
		}
		ret = saa7191_write_reg(sd, SAA7191_REG_GAIN, reg);
		break;
	case V4L2_CID_HUE:
		reg = ctrl->value & 0xff;
		if (reg < 0x80)
			reg += 0x80;
		else
			reg -= 0x80;
		ret = saa7191_write_reg(sd, SAA7191_REG_HUEC, reg);
		break;
	case SAA7191_CONTROL_VTRC:
		reg = saa7191_read_reg(sd, SAA7191_REG_STDC);
		if (ctrl->value)
			reg |= SAA7191_STDC_VTRC;
		else
			reg &= ~SAA7191_STDC_VTRC;
		ret = saa7191_write_reg(sd, SAA7191_REG_STDC, reg);
		break;
	case SAA7191_CONTROL_LUMA_DELAY: {
		s32 value = ctrl->value;
		if (value < 0)
			value += 8;
		reg = saa7191_read_reg(sd, SAA7191_REG_CTL3);
		reg &= ~SAA7191_CTL3_YDEL_MASK;
		reg |= (value << SAA7191_CTL3_YDEL_SHIFT)
			& SAA7191_CTL3_YDEL_MASK;
		ret = saa7191_write_reg(sd, SAA7191_REG_CTL3, reg);
		break;
	}
	case SAA7191_CONTROL_VNR:
		reg = saa7191_read_reg(sd, SAA7191_REG_CTL4);
		reg &= ~SAA7191_CTL4_VNOI_MASK;
		reg |= (ctrl->value << SAA7191_CTL4_VNOI_SHIFT)
			& SAA7191_CTL4_VNOI_MASK;
		ret = saa7191_write_reg(sd, SAA7191_REG_CTL4, reg);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/* I2C-interface */

static int saa7191_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	u8 status_reg;
	int res = V4L2_IN_ST_NO_SIGNAL;

	if (saa7191_read_status(sd, &status_reg))
		return -EIO;
	if ((status_reg & SAA7191_STATUS_HLCK) == 0)
		res = 0;
	if (!(status_reg & SAA7191_STATUS_CODE))
		res |= V4L2_IN_ST_NO_COLOR;
	*status = res;
	return 0;
}


static int saa7191_g_chip_ident(struct v4l2_subdev *sd,
		struct v4l2_dbg_chip_ident *chip)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return v4l2_chip_ident_i2c_client(client, chip, V4L2_IDENT_SAA7191, 0);
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_subdev_core_ops saa7191_core_ops = {
	.g_chip_ident = saa7191_g_chip_ident,
	.g_ctrl = saa7191_g_ctrl,
	.s_ctrl = saa7191_s_ctrl,
	.s_std = saa7191_s_std,
};

static const struct v4l2_subdev_video_ops saa7191_video_ops = {
	.s_routing = saa7191_s_routing,
	.querystd = saa7191_querystd,
	.g_input_status = saa7191_g_input_status,
};

static const struct v4l2_subdev_ops saa7191_ops = {
	.core = &saa7191_core_ops,
	.video = &saa7191_video_ops,
};

static int saa7191_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int err = 0;
	struct saa7191 *decoder;
	struct v4l2_subdev *sd;

	v4l_info(client, "chip found @ 0x%x (%s)\n",
			client->addr << 1, client->adapter->name);

	decoder = kzalloc(sizeof(*decoder), GFP_KERNEL);
	if (!decoder)
		return -ENOMEM;

	sd = &decoder->sd;
	v4l2_i2c_subdev_init(sd, client, &saa7191_ops);

	err = saa7191_write_block(sd, sizeof(initseq), initseq);
	if (err) {
		printk(KERN_ERR "SAA7191 initialization failed\n");
		kfree(decoder);
		return err;
	}

	printk(KERN_INFO "SAA7191 initialized\n");

	decoder->input = SAA7191_INPUT_COMPOSITE;
	decoder->norm = V4L2_STD_PAL;

	err = saa7191_autodetect_norm(sd);
	if (err && (err != -EBUSY))
		printk(KERN_ERR "SAA7191: Signal auto-detection failed\n");

	return 0;
}

static int saa7191_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_device_unregister_subdev(sd);
	kfree(to_saa7191(sd));
	return 0;
}

static const struct i2c_device_id saa7191_id[] = {
	{ "saa7191", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, saa7191_id);

static struct i2c_driver saa7191_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "saa7191",
	},
	.probe		= saa7191_probe,
	.remove		= saa7191_remove,
	.id_table	= saa7191_id,
};

module_i2c_driver(saa7191_driver);
