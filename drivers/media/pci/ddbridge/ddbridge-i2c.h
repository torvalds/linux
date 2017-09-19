/*
 * ddbridge-i2c.c: Digital Devices bridge i2c driver
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DDBRIDGE_I2C_H__
#define __DDBRIDGE_I2C_H__

#include <linux/i2c.h>

#include "ddbridge.h"

/******************************************************************************/

void ddb_i2c_release(struct ddb *dev);
int ddb_i2c_init(struct ddb *dev);

/******************************************************************************/

static int __maybe_unused i2c_io(struct i2c_adapter *adapter, u8 adr,
				 u8 *wbuf, u32 wlen, u8 *rbuf, u32 rlen)
{
	struct i2c_msg msgs[2] = { { .addr = adr,  .flags = 0,
				     .buf  = wbuf, .len   = wlen },
				   { .addr = adr,  .flags = I2C_M_RD,
				     .buf  = rbuf, .len   = rlen } };

	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
}

static int __maybe_unused i2c_write(struct i2c_adapter *adap, u8 adr,
				    u8 *data, int len)
{
	struct i2c_msg msg = { .addr = adr, .flags = 0,
			       .buf = data, .len = len };

	return (i2c_transfer(adap, &msg, 1) == 1) ? 0 : -1;
}

static int __maybe_unused i2c_read(struct i2c_adapter *adapter, u8 adr, u8 *val)
{
	struct i2c_msg msgs[1] = { { .addr = adr, .flags = I2C_M_RD,
				     .buf  = val, .len   = 1 } };

	return (i2c_transfer(adapter, msgs, 1) == 1) ? 0 : -1;
}

static int __maybe_unused i2c_read_regs(struct i2c_adapter *adapter,
					u8 adr, u8 reg, u8 *val, u8 len)
{
	struct i2c_msg msgs[2] = { { .addr = adr,  .flags = 0,
				     .buf  = &reg, .len   = 1 },
				   { .addr = adr,  .flags = I2C_M_RD,
				     .buf  = val,  .len   = len } };

	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
}

static int __maybe_unused i2c_read_regs16(struct i2c_adapter *adapter,
					  u8 adr, u16 reg, u8 *val, u8 len)
{
	u8 msg[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg msgs[2] = { { .addr = adr, .flags = 0,
				     .buf  = msg, .len   = 2 },
				   { .addr = adr, .flags = I2C_M_RD,
				     .buf  = val, .len   = len } };

	return (i2c_transfer(adapter, msgs, 2) == 2) ? 0 : -1;
}

static int __maybe_unused i2c_write_reg16(struct i2c_adapter *adap,
					  u8 adr, u16 reg, u8 val)
{
	u8 msg[3] = { reg >> 8, reg & 0xff, val };

	return i2c_write(adap, adr, msg, 3);
}

static int __maybe_unused i2c_write_reg(struct i2c_adapter *adap,
					u8 adr, u8 reg, u8 val)
{
	u8 msg[2] = { reg, val };

	return i2c_write(adap, adr, msg, 2);
}

static int __maybe_unused i2c_read_reg16(struct i2c_adapter *adapter,
					 u8 adr, u16 reg, u8 *val)
{
	return i2c_read_regs16(adapter, adr, reg, val, 1);
}

static int __maybe_unused i2c_read_reg(struct i2c_adapter *adapter,
				       u8 adr, u8 reg, u8 *val)
{
	return i2c_read_regs(adapter, adr, reg, val, 1);
}

#endif /* __DDBRIDGE_I2C_H__ */
