/**
 * @file me8200_reg.h
 *
 * @brief ME-8200 register definitions.
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

#ifndef _ME8200_REG_H_
#define _ME8200_REG_H_

#ifdef __KERNEL__

#define ME8200_IRQ_MODE_REG				0xD	// R/W

#define ME8200_IRQ_MODE_MASK     			0x3

#define ME8200_IRQ_MODE_MASK_MASK			0x0
#define ME8200_IRQ_MODE_MASK_COMPARE			0x1

#define ME8200_IRQ_MODE_BIT_ENABLE_POWER		0x10
#define ME8200_IRQ_MODE_BIT_CLEAR_POWER			0x40

#define ME8200_IRQ_MODE_DI_SHIFT			2
#define ME8200_IRQ_MODE_POWER_SHIFT			1

#endif
#endif
