/*
    zr36120_i2c.c - Zoran 36120/36125 based framegrabbers

    Copyright (C) 1998-1999 Pauline Middelink <middelin@polyware.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/types.h>
#include <linux/delay.h>
#include <asm/io.h>

#include <linux/video_decoder.h>
#include <asm/uaccess.h>

#include "tuner.h"
#include "zr36120.h"

/* ----------------------------------------------------------------------- */
/* I2C functions							   */
/* ----------------------------------------------------------------------- */

/* software I2C functions */

#define I2C_DELAY   10

static void i2c_setlines(struct i2c_bus *bus,int ctrl,int data)
{
	struct zoran *ztv = (struct zoran*)bus->data;
	unsigned int b = 0;
	if (data) b |= ztv->card->swapi2c ? ZORAN_I2C_SCL : ZORAN_I2C_SDA;
	if (ctrl) b |= ztv->card->swapi2c ? ZORAN_I2C_SDA : ZORAN_I2C_SCL;
	zrwrite(b, ZORAN_I2C);
	udelay(I2C_DELAY);
}

static int i2c_getdataline(struct i2c_bus *bus)
{
	struct zoran *ztv = (struct zoran*)bus->data;
	if (ztv->card->swapi2c)
		return zrread(ZORAN_I2C) & ZORAN_I2C_SCL;
	return zrread(ZORAN_I2C) & ZORAN_I2C_SDA;
}

static
void attach_inform(struct i2c_bus *bus, int id)
{
	struct zoran *ztv = (struct zoran*)bus->data;
	struct video_decoder_capability dc;
	int rv;

	switch (id) {
	 case I2C_DRIVERID_VIDEODECODER:
		DEBUG(printk(CARD_INFO "decoder attached\n",CARD));

		/* fetch the capabilites of the decoder */
		rv = i2c_control_device(&ztv->i2c, I2C_DRIVERID_VIDEODECODER, DECODER_GET_CAPABILITIES, &dc);
		if (rv) {
			DEBUG(printk(CARD_DEBUG "decoder is not V4L aware!\n",CARD));
			break;
		}
		DEBUG(printk(CARD_DEBUG "capabilities %d %d %d\n",CARD,dc.flags,dc.inputs,dc.outputs));

		/* Test if the decoder can de VBI transfers */
		if (dc.flags & 16 /*VIDEO_DECODER_VBI*/)
			ztv->have_decoder = 2;
		else
			ztv->have_decoder = 1;
		break;
	 case I2C_DRIVERID_TUNER:
		ztv->have_tuner = 1;
		DEBUG(printk(CARD_INFO "tuner attached\n",CARD));
		if (ztv->tuner_type >= 0)
		{
			if (i2c_control_device(&ztv->i2c,I2C_DRIVERID_TUNER,TUNER_SET_TYPE,&ztv->tuner_type)<0)
			DEBUG(printk(CARD_INFO "attach_inform; tuner won't be set to type %d\n",CARD,ztv->tuner_type));
		}
		break;
	 default:
		DEBUG(printk(CARD_INFO "attach_inform; unknown device id=%d\n",CARD,id));
		break;
	}
}

static
void detach_inform(struct i2c_bus *bus, int id)
{
	struct zoran *ztv = (struct zoran*)bus->data;

	switch (id) {
	 case I2C_DRIVERID_VIDEODECODER:
		ztv->have_decoder = 0;
		DEBUG(printk(CARD_INFO "decoder detached\n",CARD));
		break;
	 case I2C_DRIVERID_TUNER:
		ztv->have_tuner = 0;
		DEBUG(printk(CARD_INFO "tuner detached\n",CARD));
		break;
	 default:
		DEBUG(printk(CARD_INFO "detach_inform; unknown device id=%d\n",CARD,id));
		break;
	}
}

struct i2c_bus zoran_i2c_bus_template =
{
	"ZR36120",
	I2C_BUSID_ZORAN,
	NULL,

	SPIN_LOCK_UNLOCKED,

	attach_inform,
	detach_inform,

	i2c_setlines,
	i2c_getdataline,
	NULL,
	NULL
};
