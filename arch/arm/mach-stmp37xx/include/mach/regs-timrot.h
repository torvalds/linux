/*
 * include/asm-arm/arch-stmp3xxx/regstimer.h
 *
 * Copyright (c) 2008 SigmaTel Inc
 * Copyright (c) 2008 Embedded Alley Solutions, Inc
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ARCH_ARM_REGS_TIMROT_H
#define __ARCH_ARM_REGS_TIMROT_H

#include <mach/stmp3xxx_regs.h>

#define REGS_TIMROT_BASE (REGS_BASE + 0x00068000)

HW_REGISTER(HW_TIMROT_ROTCTRL, REGS_TIMROT_BASE, 0)
#define BM_TIMROT_ROTCTRL_SFTRST      0x80000000
#define BM_TIMROT_ROTCTRL_CLKGATE      0x40000000

HW_REGISTER_INDEXED(HW_TIMROT_TIMCTRLn, REGS_TIMROT_BASE, 0x20, 0x20)
#define BM_TIMROT_TIMCTRLn_SELECT      0x0000000F
#define BF_TIMROT_TIMCTRLn_SELECT(v)   (((v) << 0) & BM_TIMROT_TIMCTRLn_SELECT)
#define BM_TIMROT_TIMCTRLn_PRESCALE      0x00000030
#define BF_TIMROT_TIMCTRLn_PRESCALE(v)   \
	(((v) << 4) & BM_TIMROT_TIMCTRLn_PRESCALE)
#define BM_TIMROT_TIMCTRLn_RELOAD      0x00000040
#define BF_TIMROT_TIMCTRLn_RELOAD(v)   (((v) << 6) & BM_TIMROT_TIMCTRLn_RELOAD)
#define BM_TIMROT_TIMCTRLn_UPDATE      0x00000080
#define BF_TIMROT_TIMCTRLn_UPDATE(v)   (((v) << 7) & BM_TIMROT_TIMCTRLn_UPDATE)
#define BM_TIMROT_TIMCTRLn_POLARITY      0x00000100
#define BF_TIMROT_TIMCTRLn_POLARITY(v)   \
	(((v) << 8) & BM_TIMROT_TIMCTRLn_POLARITY)
#define BM_TIMROT_TIMCTRLn_IRQ_EN      0x00004000
#define BF_TIMROT_TIMCTRLn_IRQ_EN(v)   \
	(((v) << 14) & BM_TIMROT_TIMCTRLn_IRQ_EN)
#define BM_TIMROT_TIMCTRLn_IRQ      0x00008000
#define BF_TIMROT_TIMCTRLn_IRQ(v)   (((v) << 15) & BM_TIMROT_TIMCTRLn_IRQ)
HW_REGISTER_0_INDEXED(HW_TIMROT_TIMCOUNTn, REGS_TIMROT_BASE, 0x30, 0x20)

#endif /* __ARCH_ARM_REGSTIMER_H */
