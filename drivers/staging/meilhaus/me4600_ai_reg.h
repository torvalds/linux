/**
 * @file me4600_ai_reg.h
 *
 * @brief ME-4000 analog input subdevice register definitions.
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

#ifndef _ME4600_AI_REG_H_
#define _ME4600_AI_REG_H_

#ifdef __KERNEL__

#define ME4600_AI_CTRL_REG					0x74	// _/W
#define ME4600_AI_STATUS_REG				0x74	// R/_
#define ME4600_AI_CHANNEL_LIST_REG			0x78	// _/W
#define ME4600_AI_DATA_REG					0x7C	// R/_
#define ME4600_AI_CHAN_TIMER_REG			0x80	// _/W
#define ME4600_AI_CHAN_PRE_TIMER_REG		0x84	// _/W
#define ME4600_AI_SCAN_TIMER_LOW_REG		0x88	// _/W
#define ME4600_AI_SCAN_TIMER_HIGH_REG		0x8C	// _/W
#define ME4600_AI_SCAN_PRE_TIMER_LOW_REG	0x90	// _/W
#define ME4600_AI_SCAN_PRE_TIMER_HIGH_REG	0x94	// _/W
#define ME4600_AI_START_REG					0x98	// R/_

#define ME4600_AI_SAMPLE_COUNTER_REG		0xC0	// _/W

#define ME4600_AI_CTRL_BIT_MODE_0			0x00000001
#define ME4600_AI_CTRL_BIT_MODE_1			0x00000002
#define ME4600_AI_CTRL_BIT_MODE_2			0x00000004
#define ME4600_AI_CTRL_BIT_SAMPLE_HOLD		0x00000008
#define ME4600_AI_CTRL_BIT_IMMEDIATE_STOP	0x00000010
#define ME4600_AI_CTRL_BIT_STOP				0x00000020
#define ME4600_AI_CTRL_BIT_CHANNEL_FIFO		0x00000040
#define ME4600_AI_CTRL_BIT_DATA_FIFO		0x00000080
#define ME4600_AI_CTRL_BIT_FULLSCALE		0x00000100
#define ME4600_AI_CTRL_BIT_OFFSET			0x00000200
#define ME4600_AI_CTRL_BIT_EX_TRIG_ANALOG	0x00000400
#define ME4600_AI_CTRL_BIT_EX_TRIG			0x00000800
#define ME4600_AI_CTRL_BIT_EX_TRIG_FALLING	0x00001000
#define ME4600_AI_CTRL_BIT_EX_IRQ			0x00002000
#define ME4600_AI_CTRL_BIT_EX_IRQ_RESET		0x00004000
#define ME4600_AI_CTRL_BIT_LE_IRQ			0x00008000
#define ME4600_AI_CTRL_BIT_LE_IRQ_RESET		0x00010000
#define ME4600_AI_CTRL_BIT_HF_IRQ			0x00020000
#define ME4600_AI_CTRL_BIT_HF_IRQ_RESET		0x00040000
#define ME4600_AI_CTRL_BIT_SC_IRQ			0x00080000
#define ME4600_AI_CTRL_BIT_SC_IRQ_RESET		0x00100000
#define ME4600_AI_CTRL_BIT_SC_RELOAD		0x00200000
#define ME4600_AI_CTRL_BIT_EX_TRIG_BOTH		0x80000000

#define ME4600_AI_STATUS_BIT_EF_CHANNEL		0x00400000
#define ME4600_AI_STATUS_BIT_HF_CHANNEL		0x00800000
#define ME4600_AI_STATUS_BIT_FF_CHANNEL		0x01000000
#define ME4600_AI_STATUS_BIT_EF_DATA		0x02000000
#define ME4600_AI_STATUS_BIT_HF_DATA		0x04000000
#define ME4600_AI_STATUS_BIT_FF_DATA		0x08000000
#define ME4600_AI_STATUS_BIT_LE				0x10000000
#define ME4600_AI_STATUS_BIT_FSM			0x20000000

#define ME4600_AI_CTRL_RPCI_FIFO			0x40000000	//Always set to zero!

#define ME4600_AI_BASE_FREQUENCY			33E6

#define ME4600_AI_MIN_ACQ_TICKS				66LL
#define ME4600_AI_MAX_ACQ_TICKS				0xFFFFFFFFLL

#define ME4600_AI_MIN_SCAN_TICKS			66LL
#define ME4600_AI_MAX_SCAN_TICKS			0xFFFFFFFFFLL

#define ME4600_AI_MIN_CHAN_TICKS			66LL
#define ME4600_AI_MAX_CHAN_TICKS			0xFFFFFFFFLL

#define ME4600_AI_FIFO_COUNT				2048

#define ME4600_AI_LIST_COUNT				1024

#define ME4600_AI_LIST_INPUT_SINGLE_ENDED	0x000
#define ME4600_AI_LIST_INPUT_DIFFERENTIAL	0x020

#define ME4600_AI_LIST_RANGE_BIPOLAR_10		0x000
#define ME4600_AI_LIST_RANGE_BIPOLAR_2_5	0x040
#define ME4600_AI_LIST_RANGE_UNIPOLAR_10	0x080
#define ME4600_AI_LIST_RANGE_UNIPOLAR_2_5	0x0C0

#define ME4600_AI_LIST_LAST_ENTRY		0x100

#endif
#endif
