/*
 * stmp37xx: PWM register definitions
 *
 * Copyright (c) 2008 Freescale Semiconductor
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#define REGS_PWM_BASE	(STMP3XXX_REGS_BASE + 0x64000)

#define HW_PWM_CTRL		0x0
#define BM_PWM_CTRL_PWM2_ENABLE	0x00000004
#define BM_PWM_CTRL_PWM2_ANA_CTRL_ENABLE	0x00000020

#define HW_PWM_ACTIVE0		(0x10 + 0 * 0x20)
#define HW_PWM_ACTIVE1		(0x10 + 1 * 0x20)
#define HW_PWM_ACTIVE2		(0x10 + 2 * 0x20)
#define HW_PWM_ACTIVE3		(0x10 + 3 * 0x20)

#define HW_PWM_ACTIVEn		0x10
#define BM_PWM_ACTIVEn_ACTIVE	0x0000FFFF
#define BP_PWM_ACTIVEn_ACTIVE	0
#define BM_PWM_ACTIVEn_INACTIVE	0xFFFF0000
#define BP_PWM_ACTIVEn_INACTIVE	16

#define HW_PWM_PERIOD0		(0x20 + 0 * 0x20)
#define HW_PWM_PERIOD1		(0x20 + 1 * 0x20)
#define HW_PWM_PERIOD2		(0x20 + 2 * 0x20)
#define HW_PWM_PERIOD3		(0x20 + 3 * 0x20)

#define HW_PWM_PERIODn		0x20
#define BM_PWM_PERIODn_PERIOD	0x0000FFFF
#define BP_PWM_PERIODn_PERIOD	0
#define BM_PWM_PERIODn_ACTIVE_STATE	0x00030000
#define BP_PWM_PERIODn_ACTIVE_STATE	16
#define BM_PWM_PERIODn_INACTIVE_STATE	0x000C0000
#define BP_PWM_PERIODn_INACTIVE_STATE	18
#define BM_PWM_PERIODn_CDIV	0x00700000
#define BP_PWM_PERIODn_CDIV	20
