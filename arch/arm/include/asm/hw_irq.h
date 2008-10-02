/*
 * Nothing to see here yet
 */
#ifndef _ARCH_ARM_HW_IRQ_H
#define _ARCH_ARM_HW_IRQ_H

static inline void ack_bad_irq(int irq)
{
	extern unsigned long irq_err_count;
	irq_err_count++;
}

/*
 * Obsolete inline function for calling irq descriptor handlers.
 */
static inline void desc_handle_irq(unsigned int irq, struct irq_desc *desc)
{
	desc->handle_irq(irq, desc);
}

void set_irq_flags(unsigned int irq, unsigned int flags);

#define IRQF_VALID	(1 << 0)
#define IRQF_PROBE	(1 << 1)
#define IRQF_NOAUTOEN	(1 << 2)

#endif
