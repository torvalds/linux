/*
 * stmp37xx: ICOLL register definitions
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
#ifndef _MACH_REGS_ICOLL
#define _MACH_REGS_ICOLL

#define REGS_ICOLL_BASE	(STMP3XXX_REGS_BASE + 0x0)

#define HW_ICOLL_VECTOR		0x0

#define HW_ICOLL_LEVELACK	0x10

#define HW_ICOLL_CTRL		0x20
#define BM_ICOLL_CTRL_CLKGATE	0x40000000
#define BM_ICOLL_CTRL_SFTRST	0x80000000

#define HW_ICOLL_STAT		0x30

#define HW_ICOLL_PRIORITY0	(0x60 + 0 * 0x10)
#define HW_ICOLL_PRIORITY1	(0x60 + 1 * 0x10)
#define HW_ICOLL_PRIORITY2	(0x60 + 2 * 0x10)
#define HW_ICOLL_PRIORITY3	(0x60 + 3 * 0x10)

#define HW_ICOLL_PRIORITYn	0x60

#endif
