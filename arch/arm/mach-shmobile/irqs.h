#ifndef __SHMOBILE_IRQS_H
#define __SHMOBILE_IRQS_H

#include "include/mach/irqs.h"

/* GIC */
#define gic_spi(nr)		((nr) + 32)
#define gic_iid(nr)		(nr) /* ICCIAR / interrupt ID */

/* GPIO IRQ */
#define _GPIO_IRQ_BASE		2500
#define GPIO_IRQ_BASE(x)	(_GPIO_IRQ_BASE + (32 * x))
#define GPIO_IRQ(x, y)		(_GPIO_IRQ_BASE + (32 * x) + y)

#endif /* __SHMOBILE_IRQS_H */
