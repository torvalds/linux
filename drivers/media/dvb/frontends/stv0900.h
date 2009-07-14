/*
 * stv0900.h
 *
 * Driver for ST STV0900 satellite demodulator IC.
 *
 * Copyright (C) ST Microelectronics.
 * Copyright (C) 2009 NetUP Inc.
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef STV0900_H
#define STV0900_H

#include <linux/dvb/frontend.h>
#include "dvb_frontend.h"

struct stv0900_reg {
	u16 addr;
	u8  val;
};

struct stv0900_config {
	u8 demod_address;
	u32 xtal;
	u8 clkmode;/* 0 for CLKI,  2 for XTALI */

	u8 diseqc_mode;

	u8 path1_mode;
	u8 path2_mode;
	struct stv0900_reg *ts_config_regs;
	u8 tun1_maddress;/* 0, 1, 2, 3 for 0xc0, 0xc2, 0xc4, 0xc6 */
	u8 tun2_maddress;
	u8 tun1_adc;/* 1 for stv6110, 2 for stb6100 */
	u8 tun2_adc;
};

#if defined(CONFIG_DVB_STV0900) || (defined(CONFIG_DVB_STV0900_MODULE) \
							&& defined(MODULE))
extern struct dvb_frontend *stv0900_attach(const struct stv0900_config *config,
					struct i2c_adapter *i2c, int demod);
#else
static inline struct dvb_frontend *stv0900_attach(const struct stv0900_config *config,
					struct i2c_adapter *i2c, int demod)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif

