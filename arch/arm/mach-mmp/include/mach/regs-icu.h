/*
 * linux/arch/arm/mach-mmp/include/mach/regs-icu.h
 *
 *   Interrupt Control Unit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_MACH_ICU_H
#define __ASM_MACH_ICU_H

#include <mach/addr-map.h>

#define ICU_VIRT_BASE	(AXI_VIRT_BASE + 0x82000)
#define ICU_REG(x)	(ICU_VIRT_BASE + (x))

#define ICU_INT_CONF(n)		ICU_REG((n) << 2)
#define ICU_INT_CONF_AP_INT	(1 << 6)
#define ICU_INT_CONF_CP_INT	(1 << 5)
#define ICU_INT_CONF_IRQ	(1 << 4)
#define ICU_INT_CONF_MASK	(0xf)

#define ICU_AP_FIQ_SEL_INT_NUM	ICU_REG(0x108)	/* AP FIQ Selected Interrupt */
#define ICU_AP_IRQ_SEL_INT_NUM	ICU_REG(0x10C)	/* AP IRQ Selected Interrupt */
#define ICU_AP_GBL_IRQ_MSK	ICU_REG(0x114)	/* AP Global Interrupt Mask */
#define ICU_INT_STATUS_0	ICU_REG(0x128)	/* Interrupt Stuats 0 */
#define ICU_INT_STATUS_1	ICU_REG(0x12C)	/* Interrupt Status 1 */

#endif /* __ASM_MACH_ICU_H */
