/**
 * @file me6000_dio_reg.h
 *
 * @brief ME-6000 digital input/output subdevice register definitions.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _ME6000_DIO_REG_H_
#define _ME6000_DIO_REG_H_

#ifdef __KERNEL__

#define ME6000_DIO_CTRL_REG				0x00	// R/W
#define ME6000_DIO_PORT_0_REG			0x01	// R/W
#define ME6000_DIO_PORT_1_REG			0x02	// R/W
#define ME6000_DIO_PORT_REG				ME6000_DIO_PORT_0_REG	// R/W

#define ME6000_DIO_CTRL_BIT_MODE_0		0x01
#define ME6000_DIO_CTRL_BIT_MODE_1		0x02
#define ME6000_DIO_CTRL_BIT_MODE_2		0x04
#define ME6000_DIO_CTRL_BIT_MODE_3		0x08

#endif
#endif
