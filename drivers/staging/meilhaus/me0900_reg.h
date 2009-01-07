/**
 * @file me0900_reg.h
 *
 * @brief ME-9x register definitions.
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

#ifndef _ME0900_REG_H_
#define _ME0900_REG_H_

#ifdef __KERNEL__

#define ME0900_PORT_A_REG          0x00
#define ME0900_PORT_B_REG          0x01
#define ME0900_PORT_C_REG          0x02
#define ME0900_CTRL_REG            0x03	// ( ,w)
#define ME0900_WRITE_ENABLE_REG    0x04	// (r,w)
#define ME0900_WRITE_DISABLE_REG   0x08	// (r,w)

#endif
#endif
