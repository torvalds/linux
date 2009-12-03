/*
	Mantis VP-3030 driver

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
#include "mantis_vp3030.h"

struct zl10353_config mantis_vp3030_config = {
	.demod_address	= 0x0f,
};

#define MANTIS_MODEL_NAME	"VP-3030"
#define MANTIS_DEV_TYPE		"DVB-T"

struct mantis_hwconfig vp3030_mantis_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
	.ts_size	= MANTIS_TS_188,
};

int panasonic_en57h12d5_set_params(struct dvb_frontend *fe,
				   struct dvb_frontend_parameters *params)
{
	u8 buf[4];
	int rc;
	struct mantis_pci *mantis = fe->dvb->priv;

	struct i2c_msg tuner_msg = {
		.addr = 0x60,
		.flags = 0,
		.buf = buf,
		.len = sizeof (buf)
	};

	if ((params->frequency < 950000) || (params->frequency > 2150000))
		return -EINVAL;
	rc = i2c_transfer(&mantis->adapter, &tuner_msg, 1);
	if (rc != 1) {
		printk("%s: I2C Transfer returned [%d]\n", __func__, rc);
		return -EIO;
	}
	msleep_interruptible(1);
	printk("%s: Send params to tuner ok!!!\n", __func__);

	return 0;
}
