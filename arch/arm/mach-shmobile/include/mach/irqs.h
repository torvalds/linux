#ifndef __ASM_MACH_IRQS_H
#define __ASM_MACH_IRQS_H

/* Stuck here until drivers/pinctl/sh-pfc gets rid of legacy code */

/* External IRQ pins */
#define IRQPIN_BASE		2000
#define irq_pin(nr)		((nr) + IRQPIN_BASE)

#endif /* __ASM_MACH_IRQS_H */
