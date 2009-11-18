/* linux/arch/arm/mach-s5pc100/include/mach/regs-irq.h
 *
 * Copyright 2009 Samsung Electronics Co.
 *	Byungho Min <bhmin@samsung.com>
 *
 * S5PC1XX - IRQ register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_IRQ_H
#define __ASM_ARCH_REGS_IRQ_H __FILE__

#include <mach/map.h>
#include <asm/hardware/vic.h>

/* interrupt controller */
#define S5PC1XX_VIC0REG(x)          		((x) + S5PC1XX_VA_VIC(0))
#define S5PC1XX_VIC1REG(x)          		((x) + S5PC1XX_VA_VIC(1))
#define S5PC1XX_VIC2REG(x)         		((x) + S5PC1XX_VA_VIC(2))

#endif /* __ASM_ARCH_REGS_IRQ_H */
