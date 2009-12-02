/*
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __VIA_CORE_H__
#define __VIA_CORE_H__
/*
 * A description of each known serial I2C/GPIO port.
 */
enum via_port_type {
	VIA_PORT_NONE = 0,
	VIA_PORT_I2C,
	VIA_PORT_GPIO,
};

enum via_port_mode {
	VIA_MODE_OFF = 0,
	VIA_MODE_I2C,		/* Used as I2C port */
	VIA_MODE_GPIO,	/* Two GPIO ports */
};

enum viafb_i2c_adap {
	VIA_PORT_26 = 0,
	VIA_PORT_31,
	VIA_PORT_25,
	VIA_PORT_2C,
	VIA_PORT_3D,
};
#define VIAFB_NUM_PORTS 5

struct via_port_cfg {
	enum via_port_type	type;
	enum via_port_mode	mode;
	u_int16_t		io_port;
	u_int8_t		ioport_index;
};
#endif /* __VIA_CORE_H__ */
