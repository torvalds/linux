/*
 * Copyright (C) 2004 Embedded Edge, LLC <dan@embeddededge.com>
 * 2.6 port by Matt Porter <mporter@kernel.crashing.org>
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

#ifndef I2C_AU1550_H
#define I2C_AU1550_H

struct i2c_au1550_data {
	u32	psc_base;
	int	xfer_timeout;
	int	ack_timeout;
};

int i2c_au1550_add_bus(struct i2c_adapter *);
int i2c_au1550_del_bus(struct i2c_adapter *);

#endif /* I2C_AU1550_H */
