/*
	Mantis VP-2033 driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

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

#include "mantis_common.h"
#include "mantis_vp2033.h"

#define MANTIS_MODEL_NAME	"VP-2033"
#define MANTIS_DEV_TYPE		"DVB-C"

struct mantis_hwconfig vp2033_mantis_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
};

struct cu1216_config philips_cu1216_config = {
	.demod_address	= 0x18 >> 1,
	.pll_set	= philips_cu1216_tuner_set,
//	.fe_reset = mantis_fe_reset,
};

int philips_cu1216_tuner_set(struct dvb_frontend *fe,
			     struct dvb_frontend_parameters *params)
{
	struct mantis_pci *mantis = fe->dvb->priv;

	u8 buf[4];

	struct i2c_msg msg = {
		.addr	= 0xc0 >> 1,
		.flags	= 0,
		.buf	= buf,
		.len	= sizeof (buf)
	};

#define TUNER_MUL 62500

	u32 div = (params->frequency + 36125000 + TUNER_MUL / 2) / TUNER_MUL;

	buf[0] = (div >> 8) & 0x7f;
	buf[1] = div & 0xff;
	buf[2] = 0x86;
	buf[3] = (params->frequency < 150000000 ? 0xA1 :
		  params->frequency < 445000000 ? 0x92 : 0x34);

	if (i2c_transfer(&mantis->adapter, &msg, 1) < 0) {
		printk("%s tuner not ack!\n", __FUNCTION__);
		return -EIO;
	}
	msleep(100);

	return 0;
}
