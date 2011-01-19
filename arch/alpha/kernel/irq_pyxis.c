/*
 *	linux/arch/alpha/kernel/irq_pyxis.c
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 * IRQ Code common to all PYXIS core logic chips.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/core_cia.h>

#include "proto.h"
#include "irq_impl.h"


/* Note mask bit is true for ENABLED irqs.  */
static unsigned long cached_irq_mask;

static inline void
pyxis_update_irq_hw(unsigned long mask)
{
	*(vulp)PYXIS_INT_MASK = mask;
	mb();
	*(vulp)PYXIS_INT_MASK;
}

static inline void
pyxis_enable_irq(unsigned int irq)
{
	pyxis_update_irq_hw(cached_irq_mask |= 1UL << (irq - 16));
}

static void
pyxis_disable_irq(unsigned int irq)
{
	pyxis_update_irq_hw(cached_irq_mask &= ~(1UL << (irq - 16)));
}

static void
pyxis_mask_and_ack_irq(unsigned int irq)
{
	unsigned long bit = 1UL << (irq - 16);
	unsigned long mask = cached_irq_mask &= ~bit;

	/* Disable the interrupt.  */
	*(vulp)PYXIS_INT_MASK = mask;
	wmb();
	/* Ack PYXIS PCI interrupt.  */
	*(vulp)PYXIS_INT_REQ = bit;
	mb();
	/* Re-read to force both writes.  */
	*(vulp)PYXIS_INT_MASK;
}

static struct irq_chip pyxis_irq_type = {
	.name		= "PYXIS",
	.mask_ack	= pyxis_mask_and_ack_irq,
	.mask		= pyxis_disable_irq,
	.unmask		= pyxis_enable_irq,
};

void 
pyxis_device_interrupt(unsigned long vector)
{
	unsigned long pld;
	unsigned int i;

	/* Read the interrupt summary register of PYXIS */
	pld = *(vulp)PYXIS_INT_REQ;
	pld &= cached_irq_mask;

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 7)
			isa_device_interrupt(vector);
		else
			handle_irq(16+i);
	}
}

void __init
init_pyxis_irqs(unsigned long ignore_mask)
{
	long i;

	*(vulp)PYXIS_INT_MASK = 0;		/* disable all */
	*(vulp)PYXIS_INT_REQ  = -1;		/* flush all */
	mb();

	/* Send -INTA pulses to clear any pending interrupts ...*/
	*(vuip) CIA_IACK_SC;

	for (i = 16; i < 48; ++i) {
		if ((ignore_mask >> i) & 1)
			continue;
		set_irq_chip_and_handler(i, &pyxis_irq_type, handle_level_irq);
		irq_to_desc(i)->status |= IRQ_LEVEL;
	}

	setup_irq(16+7, &isa_cascade_irqaction);
}
