/*
	STB6100 Silicon Tuner
	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	Copyright (C) ST Microelectronics

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

#ifndef __STB_6100_REG_H
#define __STB_6100_REG_H

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

#define STB6100_LD			0x00
#define STB6100_LD_LOCK			(1 << 0)

#define STB6100_VCO			0x01
#define STB6100_VCO_OSCH		(0x01 << 7)
#define STB6100_VCO_OSCH_SHIFT		7
#define STB6100_VCO_OCK			(0x03 << 5)
#define STB6100_VCO_OCK_SHIFT		5
#define STB6100_VCO_ODIV		(0x01 << 4)
#define STB6100_VCO_ODIV_SHIFT		4
#define STB6100_VCO_OSM			(0x0f << 0)

#define STB6100_NI			0x02
#define STB6100_NF_LSB			0x03

#define STB6100_K			0x04
#define STB6100_K_PSD2			(0x01 << 2)
#define STB6100_K_PSD2_SHIFT            2
#define STB6100_K_NF_MSB		(0x03 << 0)

#define STB6100_G			0x05
#define STB6100_G_G			(0x0f << 0)
#define STB6100_G_GCT			(0x07 << 5)

#define STB6100_F			0x06
#define STB6100_F_F			(0x1f << 0)

#define STB6100_DLB			0x07

#define STB6100_TEST1			0x08

#define STB6100_FCCK			0x09
#define STB6100_FCCK_FCCK		(0x01 << 6)

#define STB6100_LPEN			0x0a
#define STB6100_LPEN_LPEN		(0x01 << 4)
#define STB6100_LPEN_SYNP		(0x01 << 5)
#define STB6100_LPEN_OSCP		(0x01 << 6)
#define STB6100_LPEN_BEN		(0x01 << 7)

#define STB6100_TEST3			0x0b

#define STB6100_NUMREGS                 0x0c


#define INRANGE(val, x, y)		(((x <= val) && (val <= y)) ||		\
					 ((y <= val) && (val <= x)) ? 1 : 0)

#define CHKRANGE(val, x, y)		(((val >= x) && (val < y)) ? 1 : 0)

struct stb6100_config {
	u8	tuner_address;
	u32	refclock;
};

struct stb6100_state {
	struct i2c_adapter *i2c;

	const struct stb6100_config	*config;
	struct dvb_tuner_ops		ops;
	struct dvb_frontend		*frontend;

	u32 frequency;
	u32 srate;
	u32 bandwidth;
	u32 reference;
};

#if IS_REACHABLE(CONFIG_DVB_STB6100)

extern struct dvb_frontend *stb6100_attach(struct dvb_frontend *fe,
					   const struct stb6100_config *config,
					   struct i2c_adapter *i2c);

#else

static inline struct dvb_frontend *stb6100_attach(struct dvb_frontend *fe,
						  const struct stb6100_config *config,
						  struct i2c_adapter *i2c)
{
	printk(KERN_WARNING "%s: Driver disabled by Kconfig\n", __func__);
	return NULL;
}

#endif //CONFIG_DVB_STB6100

#endif
