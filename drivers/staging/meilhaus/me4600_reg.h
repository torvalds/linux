/**
 * @file me4600_reg.h
 *
 * @brief ME-4000 register definitions.
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

#ifndef _ME4600_REG_H_
#define _ME4600_REG_H_

#ifdef __KERNEL__

#define ME4600_IRQ_STATUS_REG			0x9C	// R/_

#define ME4600_IRQ_STATUS_BIT_EX		0x01
#define ME4600_IRQ_STATUS_BIT_LE		0x02
#define ME4600_IRQ_STATUS_BIT_AI_HF		0x04
#define ME4600_IRQ_STATUS_BIT_AO_0_HF	0x08
#define ME4600_IRQ_STATUS_BIT_AO_1_HF	0x10
#define ME4600_IRQ_STATUS_BIT_AO_2_HF	0x20
#define ME4600_IRQ_STATUS_BIT_AO_3_HF	0x40
#define ME4600_IRQ_STATUS_BIT_SC		0x80

#define ME4600_IRQ_STATUS_BIT_AO_HF		ME4600_IRQ_STATUS_BIT_AO_0_HF

#endif
#endif
