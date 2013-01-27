/*
 * arch/arm/mach-kirkwood/include/mach/bridge-regs.h
 *
 * Mbus-L to Mbus Bridge Registers
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_BRIDGE_REGS_H
#define __ASM_ARCH_BRIDGE_REGS_H

#include <mach/kirkwood.h>

#define CPU_CONFIG		(BRIDGE_VIRT_BASE + 0x0100)
#define CPU_CONFIG_ERROR_PROP	0x00000004

#define CPU_CONTROL		(BRIDGE_VIRT_BASE + 0x0104)
#define CPU_CONTROL_PHYS	(BRIDGE_PHYS_BASE + 0x0104)
#define CPU_RESET		0x00000002

#define RSTOUTn_MASK		(BRIDGE_VIRT_BASE + 0x0108)
#define WDT_RESET_OUT_EN	0x00000002
#define SOFT_RESET_OUT_EN	0x00000004

#define SYSTEM_SOFT_RESET	(BRIDGE_VIRT_BASE + 0x010c)
#define SOFT_RESET		0x00000001

#define BRIDGE_CAUSE		(BRIDGE_VIRT_BASE + 0x0110)
#define WDT_INT_REQ		0x0008

#define BRIDGE_INT_TIMER1_CLR	(~0x0004)

#define IRQ_VIRT_BASE		(BRIDGE_VIRT_BASE + 0x0200)
#define IRQ_CAUSE_LOW_OFF	0x0000
#define IRQ_MASK_LOW_OFF	0x0004
#define IRQ_CAUSE_HIGH_OFF	0x0010
#define IRQ_MASK_HIGH_OFF	0x0014

#define TIMER_VIRT_BASE		(BRIDGE_VIRT_BASE + 0x0300)
#define TIMER_PHYS_BASE		(BRIDGE_PHYS_BASE + 0x0300)

#define L2_CONFIG_REG		(BRIDGE_VIRT_BASE + 0x0128)
#define L2_WRITETHROUGH		0x00000010

#define CLOCK_GATING_CTRL	(BRIDGE_VIRT_BASE + 0x11c)
#define CGC_BIT_GE0		(0)
#define CGC_BIT_PEX0		(2)
#define CGC_BIT_USB0		(3)
#define CGC_BIT_SDIO		(4)
#define CGC_BIT_TSU		(5)
#define CGC_BIT_DUNIT		(6)
#define CGC_BIT_RUNIT		(7)
#define CGC_BIT_XOR0		(8)
#define CGC_BIT_AUDIO		(9)
#define CGC_BIT_SATA0		(14)
#define CGC_BIT_SATA1		(15)
#define CGC_BIT_XOR1		(16)
#define CGC_BIT_CRYPTO		(17)
#define CGC_BIT_PEX1		(18)
#define CGC_BIT_GE1		(19)
#define CGC_BIT_TDM		(20)
#define CGC_GE0			(1 << 0)
#define CGC_PEX0		(1 << 2)
#define CGC_USB0		(1 << 3)
#define CGC_SDIO		(1 << 4)
#define CGC_TSU			(1 << 5)
#define CGC_DUNIT		(1 << 6)
#define CGC_RUNIT		(1 << 7)
#define CGC_XOR0		(1 << 8)
#define CGC_AUDIO		(1 << 9)
#define CGC_POWERSAVE           (1 << 11)
#define CGC_SATA0		(1 << 14)
#define CGC_SATA1		(1 << 15)
#define CGC_XOR1		(1 << 16)
#define CGC_CRYPTO		(1 << 17)
#define CGC_PEX1		(1 << 18)
#define CGC_GE1			(1 << 19)
#define CGC_TDM			(1 << 20)
#define CGC_RESERVED		(0x6 << 21)

#endif
