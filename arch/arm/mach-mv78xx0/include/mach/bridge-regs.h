/*
 * arch/arm/mach-mv78xx0/include/mach/bridge-regs.h
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_BRIDGE_REGS_H
#define __ASM_ARCH_BRIDGE_REGS_H

#include <mach/mv78xx0.h>

#define CPU_CONTROL		(BRIDGE_VIRT_BASE | 0x0104)
#define L2_WRITETHROUGH		0x00020000

#define RSTOUTn_MASK		(BRIDGE_VIRT_BASE | 0x0108)
#define SOFT_RESET_OUT_EN	0x00000004

#define SYSTEM_SOFT_RESET	(BRIDGE_VIRT_BASE | 0x010c)
#define SOFT_RESET		0x00000001

#define BRIDGE_INT_TIMER1_CLR	(~0x0004)

#define IRQ_VIRT_BASE		(BRIDGE_VIRT_BASE | 0x0200)
#define IRQ_CAUSE_ERR_OFF	0x0000
#define IRQ_CAUSE_LOW_OFF	0x0004
#define IRQ_CAUSE_HIGH_OFF	0x0008
#define IRQ_MASK_ERR_OFF	0x000c
#define IRQ_MASK_LOW_OFF	0x0010
#define IRQ_MASK_HIGH_OFF	0x0014

#define TIMER_VIRT_BASE		(BRIDGE_VIRT_BASE | 0x0300)

#endif
