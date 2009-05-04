#ifndef __XTENSA_S6000_IRQ_H
#define __XTENSA_S6000_IRQ_H

#define NO_IRQ		(-1)

extern void variant_irq_enable(unsigned int irq);
extern void variant_irq_disable(unsigned int irq);

#endif /* __XTENSA_S6000_IRQ_H */
