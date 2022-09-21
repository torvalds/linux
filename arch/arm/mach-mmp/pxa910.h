/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MACH_PXA910_H
#define __ASM_MACH_PXA910_H

extern void pxa910_timer_init(void);
extern void __init pxa910_init_irq(void);

#include <linux/i2c.h>
#include <linux/irqchip/mmp.h>

#endif /* __ASM_MACH_PXA910_H */
