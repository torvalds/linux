/*
 * arch/arm/mach-orion5x/include/mach/bridge-regs.h
 *
 * Orion CPU Bridge Registers
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_BRIDGE_REGS_H
#define __ASM_ARCH_BRIDGE_REGS_H

#include <mach/orion5x.h>

#define CPU_CONF		(ORION5X_BRIDGE_VIRT_BASE + 0x100)

#define CPU_CTRL		(ORION5X_BRIDGE_VIRT_BASE + 0x104)

#define RSTOUTn_MASK		(ORION5X_BRIDGE_VIRT_BASE + 0x108)
#define RSTOUTn_MASK_PHYS	(ORION5X_BRIDGE_PHYS_BASE + 0x108)

#define CPU_SOFT_RESET		(ORION5X_BRIDGE_VIRT_BASE + 0x10c)

#define BRIDGE_CAUSE		(ORION5X_BRIDGE_VIRT_BASE + 0x110)

#define POWER_MNG_CTRL_REG	(ORION5X_BRIDGE_VIRT_BASE + 0x11C)

#define BRIDGE_INT_TIMER1_CLR	(~0x0004)

#define MAIN_IRQ_CAUSE		(ORION5X_BRIDGE_VIRT_BASE + 0x200)

#define MAIN_IRQ_MASK		(ORION5X_BRIDGE_VIRT_BASE + 0x204)

#define TIMER_VIRT_BASE		(ORION5X_BRIDGE_VIRT_BASE + 0x300)
#define TIMER_PHYS_BASE		(ORION5X_BRIDGE_PHYS_BASE + 0x300)
#endif
