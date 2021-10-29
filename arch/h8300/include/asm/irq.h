/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H8300_IRQ_H_
#define _H8300_IRQ_H_

#if defined(CONFIG_CPU_H8300H)
#define NR_IRQS 64
#define IRQ_CHIP h8300h_irq_chip
#define EXT_IRQ0 12
#define EXT_IRQS 6
#elif defined(CONFIG_CPU_H8S)
#define NR_IRQS 128
#define IRQ_CHIP h8s_irq_chip
#define EXT_IRQ0 16
#define EXT_IRQS 16
#endif

static inline int irq_canonicalize(int irq)
{
	return irq;
}

void h8300_init_ipr(void);
extern struct irq_chip h8300h_irq_chip;
extern struct irq_chip h8s_irq_chip;
#endif /* _H8300_IRQ_H_ */
