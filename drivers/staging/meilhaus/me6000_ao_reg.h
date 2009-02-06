/**
 * @file me6000_ao_reg.h
 *
 * @brief ME-6000 analog output subdevice register definitions.
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

#ifndef _ME6000_AO_REG_H_
#define _ME6000_AO_REG_H_

#ifdef __KERNEL__

// AO
#define ME6000_AO_00_CTRL_REG					0x00	// R/W
#define ME6000_AO_00_STATUS_REG					0x04	// R/_
#define ME6000_AO_00_FIFO_REG					0x08	// _/W
#define ME6000_AO_00_SINGLE_REG					0x0C	// R/W
#define ME6000_AO_00_TIMER_REG					0x10	// _/W

#define ME6000_AO_01_CTRL_REG					0x18	// R/W
#define ME6000_AO_01_STATUS_REG					0x1C	// R/_
#define ME6000_AO_01_FIFO_REG					0x20	// _/W
#define ME6000_AO_01_SINGLE_REG					0x24	// R/W
#define ME6000_AO_01_TIMER_REG					0x28	// _/W

#define ME6000_AO_02_CTRL_REG					0x30	// R/W
#define ME6000_AO_02_STATUS_REG					0x34	// R/_
#define ME6000_AO_02_FIFO_REG					0x38	// _/W
#define ME6000_AO_02_SINGLE_REG					0x3C	// R/W
#define ME6000_AO_02_TIMER_REG					0x40	// _/W

#define ME6000_AO_03_CTRL_REG					0x48	// R/W
#define ME6000_AO_03_STATUS_REG					0x4C	// R/_
#define ME6000_AO_03_FIFO_REG					0x50	// _/W
#define ME6000_AO_03_SINGLE_REG					0x54	// R/W
#define ME6000_AO_03_TIMER_REG					0x58	// _/W

#define ME6000_AO_SINGLE_STATUS_REG				0xA4	// R/_
#define ME6000_AO_SINGLE_STATUS_OFFSET			4	//The first single subdevice => bit 0 in ME6000_AO_SINGLE_STATUS_REG.

#define ME6000_AO_04_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_04_SINGLE_REG					0x74	// _/W

#define ME6000_AO_05_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_05_SINGLE_REG					0x78	// _/W

#define ME6000_AO_06_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_06_SINGLE_REG					0x7C	// _/W

#define ME6000_AO_07_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_07_SINGLE_REG					0x80	// _/W

#define ME6000_AO_08_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_08_SINGLE_REG					0x84	// _/W

#define ME6000_AO_09_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_09_SINGLE_REG					0x88	// _/W

#define ME6000_AO_10_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_10_SINGLE_REG					0x8C	// _/W

#define ME6000_AO_11_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_11_SINGLE_REG					0x90	// _/W

#define ME6000_AO_12_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_12_SINGLE_REG					0x94	// _/W

#define ME6000_AO_13_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_13_SINGLE_REG					0x98	// _/W

#define ME6000_AO_14_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_14_SINGLE_REG					0x9C	// _/W

#define ME6000_AO_15_STATUS_REG					ME6000_AO_SINGLE_STATUS_REG
#define ME6000_AO_15_SINGLE_REG					0xA0	// _/W

//ME6000_AO_CTRL_REG
#define ME6000_AO_MODE_SINGLE					0x00
#define ME6000_AO_MODE_WRAPAROUND				0x01
#define ME6000_AO_MODE_CONTINUOUS				0x02
#define ME6000_AO_CTRL_MODE_MASK				(ME6000_AO_MODE_WRAPAROUND | ME6000_AO_MODE_CONTINUOUS)

#define ME6000_AO_CTRL_BIT_MODE_WRAPAROUND		0x001
#define ME6000_AO_CTRL_BIT_MODE_CONTINUOUS		0x002
#define ME6000_AO_CTRL_BIT_STOP					0x004
#define ME6000_AO_CTRL_BIT_ENABLE_FIFO			0x008
#define ME6000_AO_CTRL_BIT_ENABLE_EX_TRIG		0x010
#define ME6000_AO_CTRL_BIT_EX_TRIG_EDGE			0x020
#define ME6000_AO_CTRL_BIT_ENABLE_IRQ			0x040
#define ME6000_AO_CTRL_BIT_IMMEDIATE_STOP		0x080
#define ME6000_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH 	0x800

//ME6000_AO_STATUS_REG
#define ME6000_AO_STATUS_BIT_FSM				0x01
#define ME6000_AO_STATUS_BIT_FF					0x02
#define ME6000_AO_STATUS_BIT_HF					0x04
#define ME6000_AO_STATUS_BIT_EF					0x08

#define ME6000_AO_PRELOAD_REG					0xA8	// R/W    ///ME6000_AO_SYNC_REG <==> ME6000_AO_PRELOAD_REG
/*
#define ME6000_AO_SYNC_HOLD_0					0x00000001
#define ME6000_AO_SYNC_HOLD_1					0x00000002
#define ME6000_AO_SYNC_HOLD_2					0x00000004
#define ME6000_AO_SYNC_HOLD_3					0x00000008
#define ME6000_AO_SYNC_HOLD_4					0x00000010
#define ME6000_AO_SYNC_HOLD_5					0x00000020
#define ME6000_AO_SYNC_HOLD_6					0x00000040
#define ME6000_AO_SYNC_HOLD_7					0x00000080
#define ME6000_AO_SYNC_HOLD_8					0x00000100
#define ME6000_AO_SYNC_HOLD_9					0x00000200
#define ME6000_AO_SYNC_HOLD_10					0x00000400
#define ME6000_AO_SYNC_HOLD_11					0x00000800
#define ME6000_AO_SYNC_HOLD_12					0x00001000
#define ME6000_AO_SYNC_HOLD_13					0x00002000
#define ME6000_AO_SYNC_HOLD_14					0x00004000
#define ME6000_AO_SYNC_HOLD_15					0x00008000
*/
#define ME6000_AO_SYNC_HOLD						0x00000001
/*
#define ME6000_AO_SYNC_EXT_TRIG_0				0x00010000
#define ME6000_AO_SYNC_EXT_TRIG_1				0x00020000
#define ME6000_AO_SYNC_EXT_TRIG_2				0x00040000
#define ME6000_AO_SYNC_EXT_TRIG_3				0x00080000
#define ME6000_AO_SYNC_EXT_TRIG_4				0x00100000
#define ME6000_AO_SYNC_EXT_TRIG_5				0x00200000
#define ME6000_AO_SYNC_EXT_TRIG_6				0x00400000
#define ME6000_AO_SYNC_EXT_TRIG_7				0x00800000
#define ME6000_AO_SYNC_EXT_TRIG_8				0x01000000
#define ME6000_AO_SYNC_EXT_TRIG_9				0x02000000
#define ME6000_AO_SYNC_EXT_TRIG_10				0x04000000
#define ME6000_AO_SYNC_EXT_TRIG_11				0x08000000
#define ME6000_AO_SYNC_EXT_TRIG_12				0x10000000
#define ME6000_AO_SYNC_EXT_TRIG_13				0x20000000
#define ME6000_AO_SYNC_EXT_TRIG_14				0x40000000
#define ME6000_AO_SYNC_EXT_TRIG_15				0x80000000
*/
#define ME6000_AO_SYNC_EXT_TRIG					0x00010000

#define ME6000_AO_EXT_TRIG						0x80000000

// AO-IRQ
#define ME6000_AO_IRQ_STATUS_REG				0x60	// R/_
#define ME6000_AO_00_IRQ_RESET_REG				0x64	// R/_
#define ME6000_AO_01_IRQ_RESET_REG				0x68	// R/_
#define ME6000_AO_02_IRQ_RESET_REG				0x6C	// R/_
#define ME6000_AO_03_IRQ_RESET_REG				0x70	// R/_

#define ME6000_IRQ_STATUS_BIT_0					0x01
#define ME6000_IRQ_STATUS_BIT_1					0x02
#define ME6000_IRQ_STATUS_BIT_2					0x04
#define ME6000_IRQ_STATUS_BIT_3					0x08

#define ME6000_IRQ_STATUS_BIT_AO_HF				ME6000_IRQ_STATUS_BIT_0

//DUMY register
#define ME6000_AO_DUMY									0xFC
#endif
#endif
