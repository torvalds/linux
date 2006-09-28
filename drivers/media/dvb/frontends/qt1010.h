/*
 *  qt1010.h - DVB-T Tuner support
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _QT1010_H_
#define _QT1010_H_

#define QT1010_W 0
#define QT1010_R 1
/* Not actual hw limits. */
#define QT1010_MIN_STEP 2000000
#define QT1010_MIN_FREQ 48000000

static int qt1010_set_params(struct dvb_frontend *fe, struct dvb_frontend_parameters *params)
{
	int i;
	int div, mod;
	struct {
		u8 read, reg, value;
	} rd[46] = {	{ QT1010_W, 0x01, 0x80 },
			{ QT1010_W, 0x02, 0x3f },
			{ QT1010_W, 0x05, 0xff }, /* c */
			{ QT1010_W, 0x06, 0x44 },
			{ QT1010_W, 0x07, 0xff }, /* c */
			{ QT1010_W, 0x08, 0x08 },
			{ QT1010_W, 0x09, 0xff }, /* c */
			{ QT1010_W, 0x0a, 0xff }, /* c */
			{ QT1010_W, 0x0b, 0xff }, /* c */
			{ QT1010_W, 0x0c, 0xe1 },
			{ QT1010_W, 0x1a, 0xff }, /* 10 c */
			{ QT1010_W, 0x1b, 0x00 },
			{ QT1010_W, 0x1c, 0x89 },
			{ QT1010_W, 0x11, 0xff }, /* c */
			{ QT1010_W, 0x12, 0x91 },
			{ QT1010_W, 0x22, 0xff }, /* c */
			{ QT1010_W, 0x1e, 0x00 },
			{ QT1010_W, 0x1e, 0xd0 },
			{ QT1010_R, 0x22, 0xff }, /* c read */
			{ QT1010_W, 0x1e, 0x00 },
			{ QT1010_R, 0x05, 0xff }, /* 20 c read */
			{ QT1010_R, 0x22, 0xff }, /* c read */
			{ QT1010_W, 0x23, 0xd0 },
			{ QT1010_W, 0x1e, 0x00 },
			{ QT1010_W, 0x1e, 0xe0 },
			{ QT1010_R, 0x23, 0xff }, /* c read */
			{ QT1010_W, 0x1e, 0x00 },
			{ QT1010_W, 0x24, 0xd0 },
			{ QT1010_W, 0x1e, 0x00 },
			{ QT1010_W, 0x1e, 0xf0 },
			{ QT1010_R, 0x24, 0xff }, /* 30 c read */
			{ QT1010_W, 0x1e, 0x00 },
			{ QT1010_W, 0x14, 0x7f },
			{ QT1010_W, 0x15, 0x7f },
			{ QT1010_W, 0x05, 0xff }, /* c */
			{ QT1010_W, 0x06, 0x00 },
			{ QT1010_W, 0x15, 0x1f },
			{ QT1010_W, 0x16, 0xff },
			{ QT1010_W, 0x18, 0xff },
			{ QT1010_W, 0x1f, 0xff }, /* c */
			{ QT1010_W, 0x20, 0xff }, /* 40 c */
			{ QT1010_W, 0x21, 0x53 },
			{ QT1010_W, 0x25, 0xbd },
			{ QT1010_W, 0x26, 0x15 },
			{ QT1010_W, 0x02, 0x00 },
			{ QT1010_W, 0x01, 0x00 },
			};
	struct i2c_msg msg;
	struct dvb_usb_device *d = fe->dvb->priv;
	unsigned long freq = params->frequency;

	if (freq % QT1010_MIN_STEP)
		printk("frequency not supported.\n");

	div = (freq - QT1010_MIN_FREQ) / QT1010_MIN_STEP;
	mod = (div + 16 - 9) % 16;

	/* 0x5 */
	if (div >= 377)
		rd[2].value = 0x74;
	else if (div >=  265)
		rd[2].value = 0x54;
	else if (div >=  121)
		rd[2].value = 0x34;
	else
		rd[2].value = 0x14;

	/* 0x7 */
	rd[4].value = (((freq - QT1010_MIN_FREQ) / 1000000) * 9975 + 12960000) / 320000;

	/* 09 */
	if (mod < 4)
		rd[6].value = 0x1d;
	else
		rd[6].value = 0x1c;

	/* 0a */
	if (mod < 2)
		rd[7].value = 0x09;
	else if (mod < 4)
		rd[7].value = 0x08;
	else if (mod < 6)
		rd[7].value = 0x0f;
	else if (mod < 8)
		rd[7].value = 0x0e;
	else if (mod < 10)
		rd[7].value = 0x0d;
	else if (mod < 12)
		rd[7].value = 0x0c;
	else if (mod < 14)
		rd[7].value = 0x0b;
	else
		rd[7].value = 0x0a;

	/* 0b */
	if (div & 1)
		rd[8].value = 0x45;
	else
		rd[8].value = 0x44;

	/* 1a */
	if (div & 1)
		rd[10].value = 0x78;
	else
		rd[10].value = 0xf8;

	/* 11 */
	if (div >= 265)
		rd[13].value = 0xf9;
	else if (div >=  121)
		rd[13].value = 0xfd;
	else
		rd[13].value = 0xf9;

	/* 22 */
	if (div < 201)
		rd[15].value = 0xd0;
	else if (div < 217)
		rd[15].value = 0xd3;
	else if (div < 233)
		rd[15].value = 0xd6;
	else if (div < 249)
		rd[15].value = 0xd9;
	else if (div < 265)
		rd[15].value = 0xda;
	else
		rd[15].value = 0xd0;

	/* 05 */
	if (div >= 377)
		rd[34].value = 0x70;
	else if (div >=  265)
		rd[34].value = 0x50;
	else if (div >=  121)
		rd[34].value = 0x30;
	else
		rd[34].value = 0x10;

	/* 1f */
	if (mod < 4)
		rd[39].value = 0x64;
	else if (mod < 6)
		rd[39].value = 0x66;
	else if (mod < 8)
		rd[39].value = 0x67;
	else if (mod < 12)
		rd[39].value = 0x68;
	else if (mod < 14)
		rd[39].value = 0x69;
	else
		rd[39].value = 0x6a;

	/* 20 */
	if (mod < 4)
		rd[40].value = 0x10;
	else if (mod < 6)
		rd[40].value = 0x11;
	else if (mod < 10)
		rd[40].value = 0x12;
	else if (mod < 12)
		rd[40].value = 0x13;
	else if (mod < 14)
		rd[40].value = 0x14;
	else
		rd[40].value = 0x15;

	for (i = 0; i < sizeof(rd) / sizeof(*rd); i++) {
		if (rd[i].read)
			continue;

		msg.flags = 0;
		msg.len = 2;
		msg.addr = d->adapter[0].pll_addr;
		msg.buf = &rd[i].reg;

		if (i2c_transfer(&d->i2c_adap, &msg, 1) != 1) {
			printk("tuner write failed\n");
			return -EIO;
		}
	}

	return 0;
}

static int qt1010_tuner_attach(struct dvb_usb_adapter *adap)
{
	adap->pll_addr = 0xc4;
	adap->pll_desc = NULL;
	adap->fe->ops.tuner_ops.set_params = qt1010_set_params;

	return 0;
}
#endif
