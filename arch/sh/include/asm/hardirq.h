#ifndef __ASM_SH_HARDIRQ_H
#define __ASM_SH_HARDIRQ_H

extern void ack_bad_irq(unsigned int irq);
#define ack_bad_irq ack_bad_irq

#include <asm-generic/hardirq.h>

#endif /* __ASM_SH_HARDIRQ_H */
