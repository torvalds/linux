/*
 * STMP POWER Register Definitions
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

#ifndef __ARCH_ARM___POWER_H
#define __ARCH_ARM___POWER_H  1

#include <mach/stmp3xxx_regs.h>

#define REGS_POWER_BASE (void __iomem *)(REGS_BASE + 0x44000)
#define REGS_POWER_BASE_PHYS (0x80044000)
#define REGS_POWER_SIZE 0x00002000
HW_REGISTER(HW_POWER_MINPWR, REGS_POWER_BASE, 0x00000020)
HW_REGISTER(HW_POWER_CHARGE, REGS_POWER_BASE, 0x00000030)
#endif /* __ARCH_ARM___POWER_H */
