#ifndef __ASM_MACH_IRQS_H
#define __ASM_MACH_IRQS_H

#include <linux/sh_intc.h>

/* GIC */
#define gic_spi(nr)		((nr) + 32)
#define gic_iid(nr)		(nr) /* ICCIAR / interrupt ID */

/* INTCS */
#define INTCS_VECT_BASE		0x3400
#define INTCS_VECT(n, vect)	INTC_VECT((n), INTCS_VECT_BASE + (vect))
#define intcs_evt2irq(evt)	evt2irq(INTCS_VECT_BASE + (evt))

/* External IRQ pins */
#define IRQPIN_BASE		2000
#define irq_pin(nr)		((nr) + IRQPIN_BASE)

/* GPIO IRQ */
#define _GPIO_IRQ_BASE		2500
#define GPIO_IRQ_BASE(x)	(_GPIO_IRQ_BASE + (32 * x))
#define GPIO_IRQ(x, y)		(_GPIO_IRQ_BASE + (32 * x) + y)

#endif /* __ASM_MACH_IRQS_H */
