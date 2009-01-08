/**
 * @file me8100_reg.h
 *
 * @brief ME-8100 register definitions.
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

#ifndef _ME8100_REG_H_
#define _ME8100_REG_H_

#ifdef __KERNEL__

#define ME8100_CTRL_REG_A			0x00	//( ,w)
#define ME8100_CTRL_REG_B			0x0C	//( ,w)

#define ME8100_DIO_CTRL_BIT_SOURCE		0x10
#define ME8100_DIO_CTRL_BIT_INTB_1		0x20
#define ME8100_DIO_CTRL_BIT_INTB_0		0x40
#define ME8100_DIO_CTRL_BIT_ENABLE_DIO		0x80

#endif
#endif
