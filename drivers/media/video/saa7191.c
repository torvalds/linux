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

#define SAA7191_MODULE_VERSION "0.0.3"

MODULE_DESCRIPTION("Philips SAA7191 video decoder driver");
MODULE_VERSION(SAA7191_MODULE_VERSION);
MODULE_AUTHOR("Mikael Nousiainen <tmnousia@cc.hut.fi>");
MODULE_LICENSE("GPL");

struct saa7191 {
	struct i2c_client *client;

	/* the register values are stored here as the actual
	 * I2C-registers are write-only */
	unsigned char reg[25];

	unsigned char norm;
	unsigned char input;
};

static struct i2c_driver i2c_driver_saa7191;

static const unsigned char initseq[] = {
	0,	/* Subaddress */
	0x50,	/* SAA7191_REG_IDEL */
	0x30,	/* SAA7191_REG_HSYB */
	0x00,	/* SAA7191_REG_HSYS */
	0xe8,	/* SAA7191_REG_HCLB */
	0xb6,	/* SAA7191_REG_HCLS */
	0xf4,	/* SAA7191_REG_HPHI */
	0x01,	/* SAA7191_REG_LUMA - chrominance trap active (CVBS) */
	0x00,	/* SAA7191_REG_HUEC */
	0xf8,	/* SAA7191_REG_CKTQ */
	0xf8,	/* SAA7191_REG_CKTS */
	0x90,	/* SAA7191_REG_PLSE */
	0x90,	/* SAA7191_REG_SESE */
	0x00,	/* SAA7191_REG_GAIN */
	0x0c,	/* SAA7191_REG_STDC - not SECAM, slow time constant */
	0x78,	/* SAA7191_REG_IOCK - chrominance from CVBS, GPSW1 & 2 off */
	0x99,	/* SAA7191_REG_CTL3 - automatic field detection */
	0x00,	/* SAA7191_REG_CTL4 */
	0x2c,	/* SAA7191_REG_CHCV */
	0x00,	/* unused */
	0x00,	/* unused */
	0x34,	/* SAA7191_REG_HS6B */
	0x0a,	/* SAA7191_REG_HS6S */
	0xf4,	/* SAA7191_REG_HC6B */
	0xce,	/* SAA7191_REG_HC6S */
	0xf4,	/* SAA7191_REG_HP6I */
};

/* SAA7191 register handling */

static unsigned char saa7191_read_reg(struct i2c_client *client,
				      unsigned char reg)
{
	return ((struct saa7191 *)i2c_get_clientdata(client))->reg[reg];
}

static int saa7191_read_status(struct i2c_client *client,
			       unsigned char *value)
{
	int ret;

	ret = i2c_master_recv(client, value, 1);
	if (ret < 0) {
		printk(KERN_ERR "SAA7191: saa7191_read_status(): read failed");
		return ret;
	}

	return 0;
}


static int saa7191_write_reg(struct i2c_client *client, unsigned char reg,
			     unsigned char value)
{

	((struct saa7191 *)i2c_get_clientdata(client))->reg[reg] = value;
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* the first byte of data must be the first subaddress number (register) */
static int saa7191_write_block(struct i2c_client *client,
			       unsigned char length, unsigned char *data)
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
		       "write failed");
		return ret;
	}

	return 0;
}

/* Helper functions */

static int saa7191_set_input(struct i2c_client *client, int input)
{
	unsigned char luma = saa7191_read_reg(client, SAA7191_REG_LUMA);
	unsigned char iock = saa7191_read_reg(client, SAA7191_REG_IOCK);
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

	return 0;
}

static int saa7191_set_norm(struct i2c_client *client, int norm)
{
	struct saa7191 *decoder = i2c_get_clientdata(client);
	unsigned char stdc = saa7191_read_reg(client, SAA7191_REG_STDC);
	unsigned char ctl3 = saa7191_read_reg(client, SAA7191_REG_CTL3);
	unsigned char chcv = saa7191_read_reg(client, SAA7191_REG_CHCV);
	int err;

	switch(norm) {
	case SAA7191_NORM_AUTO: {
		unsigned char status;

		// does status depend on current norm ?
		if (saa7191_read_status(client, &status))
			return -EIO;

		stdc &= ~SAA7191_STDC_SECS;
		ctl3 &= ~SAA7191_CTL3_FSEL;
		ctl3 |= SAA7191_CTL3_AUFD;
		chcv = (status & SAA7191_STATUS_FIDT)
			       ? SAA7191_CHCV_NTSC : SAA7191_CHCV_PAL;
		break;
	}
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

	return 0;
}

static int saa7191_get_controls(struct i2c_client *client,
				struct saa7191_control *ctrl)
{
	unsigned char hue = saa7191_read_reg(client, SAA7191_REG_HUEC);
	unsigned char stdc = saa7191_read_reg(client, SAA7191_REG_STDC);

	if (hue < 0x80) {
		hue += 0x80;
	} else {
		hue -= 0x80;
	}
	ctrl->hue = hue;

	ctrl->vtrc = (stdc & SAA7191_STDC_VTRC)
		? SAA7191_VALUE_ENABLED : SAA7191_VALUE_DISABLED;

	return 0;
}

static int saa7191_set_controls(struct i2c_client *client,
				struct saa7191_control *ctrl)
{
	int err;

	if (ctrl->hue >= 0) {
		unsigned char hue = ctrl->hue & 0xff;
		if (hue < 0x80) {
			hue += 0x80;
		} else {
			hue -= 0x80;
		}
		err = saa7191_write_reg(client, SAA7191_REG_HUEC, hue);
		if (err)
			return -EIO;
	}
	if (ctrl->vtrc >= 0) {
		unsigned char stdc =
			saa7191_read_reg(client, SAA7191_REG_STDC);

		if (ctrl->vtrc) {
			stdc |= SAA7191_STDC_VTRC;
		} else {
			stdc &= ~SAA7191_STDC_VTRC;
		}

		err = saa7191_write_reg(client, SAA7191_REG_STDC, stdc);
		if (err)
			return -EIO;
	}

	return 0;
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

	decoder->input = SAA7191_INPUT_COMPOSITE;
	decoder->norm = SAA7191_NORM_AUTO;

	err = saa7191_write_block(client, sizeof(initseq),
				  (unsigned char *)initseq);
	if (err) {
		printk(KERN_ERR "SAA7191 initialization failed\n");
		goto out_detach_client;
	}

	printk(KERN_INFO "SAA7191 initialized\n");

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
		unsigned char status;
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
			return saa7191_set_norm(client, SAA7191_NORM_AUTO);
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
		unsigned char status_reg;

		if (saa7191_read_status(client, &status_reg))
			return -EIO;
		status->signal = ((status_reg & SAA7191_STATUS_HLCK) == 0)
			? SAA7191_VALUE_ENABLED : SAA7191_VALUE_DISABLED;
		status->ntsc = (status_reg & SAA7191_STATUS_FIDT)
			? SAA7191_VALUE_ENABLED : SAA7191_VALUE_DISABLED;
		status->color = (status_reg & SAA7191_STATUS_CODE)
			? SAA7191_VALUE_ENABLED : SAA7191_VALUE_DISABLED;

		status->input = decoder->input;
		status->norm = decoder->norm;
	}
	case DECODER_SAA7191_SET_NORM: {
		int *norm = arg;
		return saa7191_set_norm(client, *norm);
	}
	case DECODER_SAA7191_GET_CONTROLS: {
		struct saa7191_control *ctrl = arg;
		return saa7191_get_controls(client, ctrl);
	}
	case DECODER_SAA7191_SET_CONTROLS: {
		struct saa7191_control *ctrl = arg;
		return saa7191_set_controls(client, ctrl);
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static struct i2c_driver i2c_driver_saa7191 = {
	.owner		= THIS_MODULE,
	.name 		= "saa7191",
	.id 		= I2C_DRIVERID_SAA7191,
	.flags 		= I2C_DF_NOTIFY,
	.attach_adapter = saa7191_probe,
	.detach_client 	= saa7191_detach,
	.command 	= saa7191_command
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
