/**
 * @file me8200_di_reg.h
 *
 * @brief ME-8200 digital input subdevice register definitions.
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

#ifndef _ME8200_DI_REG_H_
#define _ME8200_DI_REG_H_

#ifdef __KERNEL__

// Common registry for whole family.
#define ME8200_DI_PORT_0_REG					0x3	// R
#define ME8200_DI_PORT_1_REG					0x4	// R

#define ME8200_DI_MASK_0_REG					0x5	// R/W
#define ME8200_DI_MASK_1_REG					0x6	// R/W

#define ME8200_DI_COMPARE_0_REG					0xA	// R/W
#define ME8200_DI_COMPARE_1_REG					0xB	// R/W

#define ME8200_DI_IRQ_CTRL_REG					0xC	// R/W

#ifndef ME8200_IRQ_MODE_REG
# define ME8200_IRQ_MODE_REG					0xD	// R/W
#endif

// This registry are for all versions
#define ME8200_DI_CHANGE_0_REG					0xE	// R
#define ME8200_DI_CHANGE_1_REG					0xF	// R

#define ME8200_DI_IRQ_CTRL_BIT_CLEAR			0x4
#define ME8200_DI_IRQ_CTRL_BIT_ENABLE			0x8

// This registry are for firmware versions 7 and later
#define ME8200_DI_EXTEND_CHANGE_0_LOW_REG		0x10	// R
#define ME8200_DI_EXTEND_CHANGE_0_HIGH_REG		0x11	// R
#define ME8200_DI_EXTEND_CHANGE_1_LOW_REG		0x12	// R
#define ME8200_DI_EXTEND_CHANGE_1_HIGH_REG		0x13	// R

#ifndef ME8200_FIRMWARE_VERSION_REG
# define ME8200_FIRMWARE_VERSION_REG			0x14	// R
#endif

// Bit definitions
#define ME8200_DI_IRQ_CTRL_MASK_EDGE			0x3
#define ME8200_DI_IRQ_CTRL_MASK_EDGE_RISING		0x0
#define ME8200_DI_IRQ_CTRL_MASK_EDGE_FALLING	0x1
#define ME8200_DI_IRQ_CTRL_MASK_EDGE_ANY		0x3

// Others
#define ME8200_DI_IRQ_CTRL_SHIFT				4

#endif
#endif
