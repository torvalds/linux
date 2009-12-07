#ifndef _ALPHA_HARDIRQ_H
#define _ALPHA_HARDIRQ_H

void ack_bad_irq(unsigned int irq);
#define ack_bad_irq ack_bad_irq

#include <asm-generic/hardirq.h>

#endif /* _ALPHA_HARDIRQ_H */
