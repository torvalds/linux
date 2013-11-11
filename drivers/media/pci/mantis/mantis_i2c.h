/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#ifndef __MANTIS_I2C_H
#define __MANTIS_I2C_H

#define I2C_STOP		(1 <<  0)
#define I2C_READ		(1 <<  1)

extern int mantis_i2c_init(struct mantis_pci *mantis);
extern int mantis_i2c_exit(struct mantis_pci *mantis);

#endif /* __MANTIS_I2C_H */
