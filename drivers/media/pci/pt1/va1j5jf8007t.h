/*
 * ISDB-T driver for VA1J5JF8007/VA1J5JF8011
 *
 * Copyright (C) 2009 HIRANO Takahito <hiranotaka@zng.info>
 *
 * based on pt1dvr - http://pt1dvr.sourceforge.jp/
 *	by Tomoaki Ishikawa <tomy@users.sourceforge.jp>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef VA1J5JF8007T_H
#define VA1J5JF8007T_H

enum va1j5jf8007t_frequency {
	VA1J5JF8007T_20MHZ,
	VA1J5JF8007T_25MHZ,
};

struct va1j5jf8007t_config {
	u8 demod_address;
	enum va1j5jf8007t_frequency frequency;
};

struct i2c_adapter;

struct dvb_frontend *
va1j5jf8007t_attach(const struct va1j5jf8007t_config *config,
		    struct i2c_adapter *adap);

/* must be called after va1j5jf8007s_attach */
int va1j5jf8007t_prepare(struct dvb_frontend *fe);

#endif
