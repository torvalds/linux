/**
 * @file me0600_dio_reg.h
 *
 * @brief ME-630 digital input/output subdevice register definitions.
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

#ifndef _ME0600_DIO_REG_H_
#define _ME0600_DIO_REG_H_

#ifdef __KERNEL__

#define ME0600_DIO_CONFIG_REG		0x0007
#define ME0600_DIO_PORT_0_REG		0x0008
#define ME0600_DIO_PORT_1_REG		0x0009
#define ME0600_DIO_PORT_REG			ME0600_DIO_PORT_0_REG

#define ME0600_DIO_CONFIG_BIT_OUT_0	0x0001
#define ME0600_DIO_CONFIG_BIT_OUT_1	0x0004

#endif
#endif
