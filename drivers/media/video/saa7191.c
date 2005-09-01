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
#include <linux/sched.h>
#include <linux/slab.h>

#include <linux/videodev.h>
#include <linux/video_decoder.h>
#include <linux/i2c.h>

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
	struct i2c_client *client;

	/* the register values are stored here as the actual
	 * I2C-registers are write-only */
	u8 reg[25];

	int input;
	int norm;
};

static struct i2c_driver i2c_driver_saa7191;

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

static u8 saa7191_read_reg(struct i2c_client *client,
			   u8 reg)
{
	return ((struct saa7191 *)i2c_get_clientdata(client))->reg[reg];
}

static int saa7191_read_status(struct i2c_client *client,
			       u8 *value)
{
	int ret;

	ret = i2c_master_recv(client, value, 1);
	if (ret < 0) {
		printk(KERN_ERR "SAA7191: saa7191_read_status(): read failed\n");
		return ret;
	}

	return 0;
}


static int saa7191_write_reg(struct i2c_client *client, u8 reg,
			     u8 value)
{
	((struct saa7191 *)i2c_get_clientdata(client))->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* the first byte of data must be the first subaddress number (register) */
static int saa7191_write_block(struct i2c_client *client,
			       u8 length, u8 *data)
{
	int i;
	int ret;

	struct saa7191 *decoder = (struct saa7191 *)i2c_get_clientdata(client);
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

static int saa7191_set_input(struct i2c_client *client, int input)
{
	struct saa7191 *decoder = i2c_get_clientdata(client);
	u8 luma = saa7191_read_reg(client, SAA7191_REG_LUMA);
	u8 iock = saa7191_read_reg(client, SAA7191_REG_IOCK);
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

	err = saa7191_write_reg(client, SAA7191_REG_LUMA, luma);
	if (err)
		return -EIO;
	err = saa7191_write_reg(client, SAA7191_REG_IOCK, iock);
	if (err)
		return -EIO;

	decoder->input = input;

	return 0;
}

static int saa7191_set_norm(struct i2c_client *client, int norm)
{
	struct saa7191 *decoder = i2c_get_clientdata(client);
	u8 stdc = saa7191_read_reg(client, SAA7191_REG_STDC);
	u8 ctl3 = saa7191_read_reg(client, SAA7191_REG_CTL3);
	u8 chcv = saa7191_read_reg(client, SAA7191_REG_CHCV);
	int err;

	switch(norm) {
	case SAA7191_NORM_PAL:
		stdc &= ~SAA7191_STDC_SECS;
		ctl3 &= ~(SAA7191_CTL3_AUFD | SAA7191_CTL3_FSEL);
		chcv = SAA7191_CHCV_PAL;
		break;
	case SAA7191_NORM_NTSC:
		stdc &= ~SAA7191_STDC_SECS;
		ctl3 &= ~SAA7191_CTL3_AUFD;
		ctl3 |= SAA7191_CTL3_FSEL;
		chcv = SAA7191_CHCV_NTSC;
		break;
	case SAA7191_NORM_SECAM:
		stdc |= SAA7191_STDC_SECS;
		ctl3 &= ~(SAA7191_CTL3_AUFD | SAA7191_CTL3_FSEL);
		chcv = SAA7191_CHCV_PAL;
		break;
	default:
		return -EINVAL;
	}

	err = saa7191_write_reg(client, SAA7191_REG_CTL3, ctl3);
	if (err)
		return -EIO;
	err = saa7191_write_reg(client, SAA7191_REG_STDC, stdc);
	if (err)
		return -EIO;
	err = saa7191_write_reg(client, SAA7191_REG_CHCV, chcv);
	if (err)
		return -EIO;

	decoder->norm = norm;

	dprintk("ctl3: %02x stdc: %02x chcv: %02x\n", ctl3,
		stdc, chcv);
	dprintk("norm: %d\n", norm);

	return 0;
}

static int saa7191_wait_for_signal(struct i2c_client *client, u8 *status)
{
	int i = 0;

	dprintk("Checking for signal...\n");

	for (i = 0; i < SAA7191_SYNC_COUNT; i++) {
		if (saa7191_read_status(client, status))
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

static int saa7191_autodetect_norm_extended(struct i2c_client *client)
{
	u8 stdc = saa7191_read_reg(client, SAA7191_REG_STDC);
	u8 ctl3 = saa7191_read_reg(client, SAA7191_REG_CTL3);
	u8 status;
	int err = 0;

	dprintk("SAA7191 extended signal auto-detection...\n");

	stdc &= ~SAA7191_STDC_SECS;
	ctl3 &= ~(SAA7191_CTL3_FSEL);

	err = saa7191_write_reg(client, SAA7191_REG_STDC, stdc);
	if (err) {
		err = -EIO;
		goto out;
	}
	err = saa7191_write_reg(client, SAA7191_REG_CTL3, ctl3);
	if (err) {
		err = -EIO;
		goto out;
	}

	ctl3 |= SAA7191_CTL3_AUFD;
	err = saa7191_write_reg(client, SAA7191_REG_CTL3, ctl3);
	if (err) {
		err = -EIO;
		goto out;
	}

	msleep(SAA7191_SYNC_DELAY);

	err = saa7191_wait_for_signal(client, &status);
	if (err)
		goto out;

	if (status & SAA7191_STATUS_FIDT) {
		/* 60Hz signal -> NTSC */
		dprintk("60Hz signal: NTSC\n");
		return saa7191_set_norm(client, SAA7191_NORM_NTSC);
	}

	/* 50Hz signal */
	dprintk("50Hz signal: Trying PAL...\n");

	/* try PAL first */
	err = saa7191_set_norm(client, SAA7191_NORM_PAL);
	if (err)
		goto out;

	msleep(SAA7191_SYNC_DELAY);

	err = saa7191_wait_for_signal(client, &status);
	if (err)
		goto out;

	/* not 50Hz ? */
	if (status & SAA7191_STATUS_FIDT) {
		dprintk("No 50Hz signal\n");
		err = -EAGAIN;
		goto out;
	}

	if (status & SAA7191_STATUS_CODE) {
		dprintk("PAL\n");
		return 0;
	}

	dprintk("No color detected with PAL - Trying SECAM...\n");

	/* no color detected ? -> try SECAM */
	err = saa7191_set_norm(client,
			       SAA7191_NORM_SECAM);
	if (err)
		goto out;

	msleep(SAA7191_SYNC_DELAY);

	err = saa7191_wait_for_signal(client, &status);
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
		return 0;
	}

	dprintk("No color detected with SECAM - Going back to PAL.\n");

	/* still no color detected ?
	 * -> set norm back to PAL */
	err = saa7191_set_norm(client,
			       SAA7191_NORM_PAL);
	if (err)
		goto out;

out:
	ctl3 = saa7191_read_reg(client, SAA7191_REG_CTL3);
	if (ctl3 & SAA7191_CTL3_AUFD) {
		ctl3 &= ~(SAA7191_CTL3_AUFD);
		err = saa7191_write_reg(client, SAA7191_REG_CTL3, ctl3);
		if (err) {
			err = -EIO;
		}
	}

	return err;
}

static int saa7191_autodetect_norm(struct i2c_client *client)
{
	u8 status;

	dprintk("SAA7191 signal auto-detection...\n");

	dprintk("Reading status...\n");

	if (saa7191_read_status(client, &status))
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
		return saa7191_set_norm(client, SAA7191_NORM_NTSC);
	} else {
		/* 50hz signal -> PAL */
		dprintk("PAL\n");
		return saa7191_set_norm(client, SAA7191_NORM_PAL);
	}
}

static int saa7191_get_control(struct i2c_client *client,
			       struct saa7191_control *ctrl)
{
	u8 reg;
	int ret = 0;

	switch (ctrl->type) {
	case SAA7191_CONTROL_BANDPASS:
	case SAA7191_CONTROL_BANDPASS_WEIGHT:
	case SAA7191_CONTROL_CORING:
		reg = saa7191_read_reg(client, SAA7191_REG_LUMA);
		switch (ctrl->type) {
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
		reg = saa7191_read_reg(client, SAA7191_REG_GAIN);
		if (ctrl->type == SAA7191_CONTROL_FORCE_COLOUR)
			ctrl->value = ((s32)reg & SAA7191_GAIN_COLO) ? 1 : 0;
		else
			ctrl->value = ((s32)reg & SAA7191_GAIN_LFIS_MASK)
				>> SAA7191_GAIN_LFIS_SHIFT;
		break;
	case SAA7191_CONTROL_HUE:
		reg = saa7191_read_reg(client, SAA7191_REG_HUEC);
		if (reg < 0x80)
			reg += 0x80;
		else
			reg -= 0x80;
		ctrl->value = (s32)reg;
		break;
	case SAA7191_CONTROL_VTRC:
		reg = saa7191_read_reg(client, SAA7191_REG_STDC);
		ctrl->value = ((s32)reg & SAA7191_STDC_VTRC) ? 1 : 0;
		break;
	case SAA7191_CONTROL_LUMA_DELAY:
		reg = saa7191_read_reg(client, SAA7191_REG_CTL3);
		ctrl->value = ((s32)reg & SAA7191_CTL3_YDEL_MASK)
			>> SAA7191_CTL3_YDEL_SHIFT;
		if (ctrl->value >= 4)
			ctrl->value -= 8;
		break;
	case SAA7191_CONTROL_VNR:
		reg = saa7191_read_reg(client, SAA7191_REG_CTL4);
		ctrl->value = ((s32)reg & SAA7191_CTL4_VNOI_MASK)
			>> SAA7191_CTL4_VNOI_SHIFT;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int saa7191_set_control(struct i2c_client *client,
			       struct saa7191_control *ctrl)
{
	u8 reg;
	int ret = 0;

	switch (ctrl->type) {
	case SAA7191_CONTROL_BANDPASS:
	case SAA7191_CONTROL_BANDPASS_WEIGHT:
	case SAA7191_CONTROL_CORING:
		reg = saa7191_read_reg(client, SAA7191_REG_LUMA);
		switch (ctrl->type) {
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
		ret = saa7191_write_reg(client, SAA7191_REG_LUMA, reg);
		break;
	case SAA7191_CONTROL_FORCE_COLOUR:
	case SAA7191_CONTROL_CHROMA_GAIN:
		reg = saa7191_read_reg(client, SAA7191_REG_GAIN);
		if (ctrl->type == SAA7191_CONTROL_FORCE_COLOUR) {
			if (ctrl->value)
				reg |= SAA7191_GAIN_COLO;
			else
				reg &= ~SAA7191_GAIN_COLO;
		} else {
			reg &= ~SAA7191_GAIN_LFIS_MASK;
			reg |= (ctrl->value << SAA7191_GAIN_LFIS_SHIFT)
				& SAA7191_GAIN_LFIS_MASK;
		}
		ret = saa7191_write_reg(client, SAA7191_REG_GAIN, reg);
		break;
	case SAA7191_CONTROL_HUE:
		reg = ctrl->value & 0xff;
		if (reg < 0x80)
			reg += 0x80;
		else
			reg -= 0x80;
		ret = saa7191_write_reg(client, SAA7191_REG_HUEC, reg);
		break;
	case SAA7191_CONTROL_VTRC:
		reg = saa7191_read_reg(client, SAA7191_REG_STDC);
		if (ctrl->value)
			reg |= SAA7191_STDC_VTRC;
		else
			reg &= ~SAA7191_STDC_VTRC;
		ret = saa7191_write_reg(client, SAA7191_REG_STDC, reg);
		break;
	case SAA7191_CONTROL_LUMA_DELAY: {
		s32 value = ctrl->value;
		if (value < 0)
			value += 8;
		reg = saa7191_read_reg(client, SAA7191_REG_CTL3);
		reg &= ~SAA7191_CTL3_YDEL_MASK;
		reg |= (value << SAA7191_CTL3_YDEL_SHIFT)
			& SAA7191_CTL3_YDEL_MASK;
		ret = saa7191_write_reg(client, SAA7191_REG_CTL3, reg);
		break;
	}
	case SAA7191_CONTROL_VNR:
		reg = saa7191_read_reg(client, SAA7191_REG_CTL4);
		reg &= ~SAA7191_CTL4_VNOI_MASK;
		reg |= (ctrl->value << SAA7191_CTL4_VNOI_SHIFT)
			& SAA7191_CTL4_VNOI_MASK;
		ret = saa7191_write_reg(client, SAA7191_REG_CTL4, reg);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/* I2C-interface */

static int saa7191_attach(struct i2c_adapter *adap, int addr, int kind)
{
	int err = 0;
	struct saa7191 *decoder;
	struct i2c_client *client;

	printk(KERN_INFO "Philips SAA7191 driver version %s\n",
	       SAA7191_MODULE_VERSION);

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	decoder = kmalloc(sizeof(*decoder), GFP_KERNEL);
	if (!decoder) {
		err = -ENOMEM;
		goto out_free_client;
	}

	memset(client, 0, sizeof(struct i2c_client));
	memset(decoder, 0, sizeof(struct saa7191));

	client->addr = addr;
	client->adapter = adap;
	client->driver = &i2c_driver_saa7191;
	client->flags = 0;
	strcpy(client->name, "saa7191 client");
	i2c_set_clientdata(client, decoder);

	decoder->client = client;

	err = i2c_attach_client(client);
	if (err)
		goto out_free_decoder;

	err = saa7191_write_block(client, sizeof(initseq), (u8 *)initseq);
	if (err) {
		printk(KERN_ERR "SAA7191 initialization failed\n");
		goto out_detach_client;
	}

	printk(KERN_INFO "SAA7191 initialized\n");

	decoder->input = SAA7191_INPUT_COMPOSITE;
	decoder->norm = SAA7191_NORM_PAL;

	err = saa7191_autodetect_norm(client);
	if (err && (err != -EBUSY)) {
		printk(KERN_ERR "SAA7191: Signal auto-detection failed\n");
	}

	return 0;

out_detach_client:
	i2c_detach_client(client);
out_free_decoder:
	kfree(decoder);
out_free_client:
	kfree(client);
	return err;
}

static int saa7191_probe(struct i2c_adapter *adap)
{
	/* Always connected to VINO */
	if (adap->id == I2C_HW_SGI_VINO)
		return saa7191_attach(adap, SAA7191_ADDR, 0);
	/* Feel free to add probe here :-) */
	return -ENODEV;
}

static int saa7191_detach(struct i2c_client *client)
{
	struct saa7191 *decoder = i2c_get_clientdata(client);

	i2c_detach_client(client);
	kfree(decoder);
	kfree(client);
	return 0;
}

static int saa7191_command(struct i2c_client *client, unsigned int cmd,
			   void *arg)
{
	struct saa7191 *decoder = i2c_get_clientdata(client);

	switch (cmd) {
	case DECODER_GET_CAPABILITIES: {
		struct video_decoder_capability *cap = arg;

		cap->flags  = VIDEO_DECODER_PAL | VIDEO_DECODER_NTSC |
			      VIDEO_DECODER_SECAM | VIDEO_DECODER_AUTO;
		cap->inputs = (client->adapter->id == I2C_HW_SGI_VINO) ? 2 : 1;
		cap->outputs = 1;
		break;
	}
	case DECODER_GET_STATUS: {
		int *iarg = arg;
		u8 status;
		int res = 0;

		if (saa7191_read_status(client, &status)) {
			return -EIO;
		}
		if ((status & SAA7191_STATUS_HLCK) == 0)
			res |= DECODER_STATUS_GOOD;
		if (status & SAA7191_STATUS_CODE)
			res |= DECODER_STATUS_COLOR;
		switch (decoder->norm) {
		case SAA7191_NORM_NTSC:
			res |= DECODER_STATUS_NTSC;
			break;
		case SAA7191_NORM_PAL:
			res |= DECODER_STATUS_PAL;
			break;
		case SAA7191_NORM_SECAM:
			res |= DECODER_STATUS_SECAM;
			break;
		case SAA7191_NORM_AUTO:
		default:
			if (status & SAA7191_STATUS_FIDT)
				res |= DECODER_STATUS_NTSC;
			else
				res |= DECODER_STATUS_PAL;
			break;
		}
		*iarg = res;
		break;
	}
	case DECODER_SET_NORM: {
		int *iarg = arg;

		switch (*iarg) {
		case VIDEO_MODE_AUTO:
			return saa7191_autodetect_norm(client);
		case VIDEO_MODE_PAL:
			return saa7191_set_norm(client, SAA7191_NORM_PAL);
		case VIDEO_MODE_NTSC:
			return saa7191_set_norm(client, SAA7191_NORM_NTSC);
		case VIDEO_MODE_SECAM:
			return saa7191_set_norm(client, SAA7191_NORM_SECAM);
		default:
			return -EINVAL;
		}
		break;
	}
	case DECODER_SET_INPUT:	{
		int *iarg = arg;

		switch (client->adapter->id) {
		case I2C_HW_SGI_VINO:
			return saa7191_set_input(client, *iarg);
		default:
			if (*iarg != 0)
				return -EINVAL;
		}
		break;
	}
	case DECODER_SET_OUTPUT: {
		int *iarg = arg;

		/* not much choice of outputs */
		if (*iarg != 0)
			return -EINVAL;
		break;
	}
	case DECODER_ENABLE_OUTPUT: {
		/* Always enabled */
		break;
	}
	case DECODER_SET_PICTURE: {
		struct video_picture *pic = arg;
		unsigned val;
		int err;

		val = (pic->hue >> 8) - 0x80;

		err = saa7191_write_reg(client, SAA7191_REG_HUEC, val);
		if (err)
			return -EIO;

		break;
	}
	case DECODER_SAA7191_GET_STATUS: {
		struct saa7191_status *status = arg;
		u8 status_reg;

		if (saa7191_read_status(client, &status_reg))
			return -EIO;

		status->signal = ((status_reg & SAA7191_STATUS_HLCK) == 0)
			? 1 : 0;
		status->signal_60hz = (status_reg & SAA7191_STATUS_FIDT)
			? 1 : 0;
		status->color = (status_reg & SAA7191_STATUS_CODE) ? 1 : 0;

		status->input = decoder->input;
		status->norm = decoder->norm;

		break;
	}
	case DECODER_SAA7191_SET_NORM: {
		int *norm = arg;

		switch (*norm) {
		case SAA7191_NORM_AUTO:
			return saa7191_autodetect_norm(client);
		case SAA7191_NORM_AUTO_EXT:
			return saa7191_autodetect_norm_extended(client);
		default:
			return saa7191_set_norm(client, *norm);
		}
	}
	case DECODER_SAA7191_GET_CONTROL: {
		return saa7191_get_control(client, arg);
	}
	case DECODER_SAA7191_SET_CONTROL: {
		return saa7191_set_control(client, arg);
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static struct i2c_driver i2c_driver_saa7191 = {
	.owner		= THIS_MODULE,
	.name		= "saa7191",
	.id		= I2C_DRIVERID_SAA7191,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter = saa7191_probe,
	.detach_client	= saa7191_detach,
	.command	= saa7191_command
};

static int saa7191_init(void)
{
	return i2c_add_driver(&i2c_driver_saa7191);
}

static void saa7191_exit(void)
{
	i2c_del_driver(&i2c_driver_saa7191);
}

module_init(saa7191_init);
module_exit(saa7191_exit);
