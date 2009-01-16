/**
 * @file me4600_ao_reg.h
 *
 * @brief ME-4000 analog output subdevice register definitions.
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

#ifndef _ME4600_AO_REG_H_
#define _ME4600_AO_REG_H_

#ifdef __KERNEL__

#define ME4600_AO_00_CTRL_REG					0x00	// R/W
#define ME4600_AO_00_STATUS_REG					0x04	// R/_
#define ME4600_AO_00_FIFO_REG					0x08	// _/W
#define ME4600_AO_00_SINGLE_REG					0x0C	// R/W
#define ME4600_AO_00_TIMER_REG					0x10	// _/W

#define ME4600_AO_01_CTRL_REG					0x18	// R/W
#define ME4600_AO_01_STATUS_REG					0x1C	// R/_
#define ME4600_AO_01_FIFO_REG					0x20	// _/W
#define ME4600_AO_01_SINGLE_REG					0x24	// R/W
#define ME4600_AO_01_TIMER_REG					0x28	// _/W

#define ME4600_AO_02_CTRL_REG					0x30	// R/W
#define ME4600_AO_02_STATUS_REG					0x34	// R/_
#define ME4600_AO_02_FIFO_REG					0x38	// _/W
#define ME4600_AO_02_SINGLE_REG					0x3C	// R/W
#define ME4600_AO_02_TIMER_REG					0x40	// _/W

#define ME4600_AO_03_CTRL_REG					0x48	// R/W
#define ME4600_AO_03_STATUS_REG					0x4C	// R/_
#define ME4600_AO_03_FIFO_REG					0x50	// _/W
#define ME4600_AO_03_SINGLE_REG					0x54	// R/W
#define ME4600_AO_03_TIMER_REG					0x58	// _/W

#define ME4600_AO_DEMUX_ADJUST_REG				0xBC	// -/W
#define ME4600_AO_DEMUX_ADJUST_VALUE			0x4C

#ifdef BOSCH
# define ME4600_AO_BOSCH_REG					0xC4

# define ME4600_AO_LOADSETREG_XX				0xB4	// R/W

# define ME4600_AO_CTRL_BIT_MODE_0				0x001
# define ME4600_AO_CTRL_BIT_MODE_1				0x002
# define ME4600_AO_CTRL_MASK_MODE				0x003

#else //~BOSCH

#define ME4600_AO_SYNC_REG						0xB4	// R/W    ///ME4600_AO_SYNC_REG <==> ME4600_AO_PRELOAD_REG <==> ME4600_AO_LOADSETREG_XX

# define ME4600_AO_MODE_SINGLE					0x00000000
# define ME4600_AO_MODE_WRAPAROUND				0x00000001
# define ME4600_AO_MODE_CONTINUOUS				0x00000002
# define ME4600_AO_CTRL_MODE_MASK				(ME4600_AO_MODE_WRAPAROUND | ME4600_AO_MODE_CONTINUOUS)
#endif //BOSCH

#define ME4600_AO_CTRL_BIT_MODE_WRAPAROUND		ME4600_AO_MODE_WRAPAROUND
#define ME4600_AO_CTRL_BIT_MODE_CONTINOUS		ME4600_AO_MODE_CONTINUOUS
#define ME4600_AO_CTRL_BIT_STOP					0x00000004
#define ME4600_AO_CTRL_BIT_ENABLE_FIFO			0x00000008
#define ME4600_AO_CTRL_BIT_ENABLE_EX_TRIG		0x00000010
#define ME4600_AO_CTRL_BIT_EX_TRIG_EDGE			0x00000020
#define ME4600_AO_CTRL_BIT_IMMEDIATE_STOP		0x00000080
#define ME4600_AO_CTRL_BIT_ENABLE_DO			0x00000100
#define ME4600_AO_CTRL_BIT_ENABLE_IRQ			0x00000200
#define ME4600_AO_CTRL_BIT_RESET_IRQ			0x00000400
#define ME4600_AO_CTRL_BIT_EX_TRIG_EDGE_BOTH	0x00000800
/*
#define ME4600_AO_SYNC_HOLD_0					0x00000001
#define ME4600_AO_SYNC_HOLD_1					0x00000002
#define ME4600_AO_SYNC_HOLD_2					0x00000004
#define ME4600_AO_SYNC_HOLD_3					0x00000008
*/
#define ME4600_AO_SYNC_HOLD						0x00000001

/*
#define ME4600_AO_SYNC_EXT_TRIG_0				0x00010000
#define ME4600_AO_SYNC_EXT_TRIG_1				0x00020000
#define ME4600_AO_SYNC_EXT_TRIG_2				0x00040000
#define ME4600_AO_SYNC_EXT_TRIG_3				0x00080000
*/
#define ME4600_AO_SYNC_EXT_TRIG					0x00010000

#define ME4600_AO_EXT_TRIG						0x80000000

#define ME4600_AO_STATUS_BIT_FSM				0x00000001
#define ME4600_AO_STATUS_BIT_FF					0x00000002
#define ME4600_AO_STATUS_BIT_HF					0x00000004
#define ME4600_AO_STATUS_BIT_EF					0x00000008

#endif
#endif
