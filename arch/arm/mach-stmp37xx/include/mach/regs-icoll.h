/*
 * Freescale STMP378X: clock registers definitions
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _INCLUDE_ASM_ARCH_REGS_ICOLL_H
#define _INCLUDE_ASM_ARCH_REGS_ICOLL_H


#include <mach/stmp3xxx_regs.h>

#define REGS_ICOLL_BASE (REGS_BASE + 0x00000000)

HW_REGISTER(HW_ICOLL_VECTOR, REGS_ICOLL_BASE, 0x00)
HW_REGISTER_WO(HW_ICOLL_LEVELACK, REGS_ICOLL_BASE, 0x10)
HW_REGISTER(HW_ICOLL_CTRL, REGS_ICOLL_BASE, 0x20)
#define BM_ICOLL_CTRL_CLKGATE	  0x40000000
#define BM_ICOLL_CTRL_SFTRST	   0x80000000
HW_REGISTER_RO(HW_ICOLL_STAT, REGS_ICOLL_BASE, 0x30)

HW_REGISTER_INDEXED(HW_ICOLL_PRIORITYn, REGS_ICOLL_BASE, 0x60, 0x10)

#endif /* _INCLUDE_ASM_ARCH_REGS_CLKCTRL_H */
