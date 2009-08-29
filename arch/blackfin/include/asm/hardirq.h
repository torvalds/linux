#ifndef __BFIN_HARDIRQ_H
#define __BFIN_HARDIRQ_H

#define __ARCH_IRQ_EXIT_IRQS_DISABLED	1

extern void ack_bad_irq(unsigned int irq);
#define ack_bad_irq ack_bad_irq

/* Define until common code gets sane defaults */
#define HARDIRQ_BITS 9

#include <asm-generic/hardirq.h>

#endif
