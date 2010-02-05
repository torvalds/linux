#ifndef __ASM_MACH_IRQS_H
#define __ASM_MACH_IRQS_H

#define NR_IRQS         512
#define NR_IRQS_LEGACY  8

#define evt2irq(evt)		(((evt) >> 5) - 16)
#define irq2evt(irq)		(((irq) + 16) << 5)

#endif /* __ASM_MACH_IRQS_H */
