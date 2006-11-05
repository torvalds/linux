/*
 * Interrupt handling for INTC2-based IRQ.
 *
 * Copyright (C) 2001 David J. Mckay (david.mckay@st.com)
 * Copyright (C) 2005, 2006 Paul Mundt (lethal@linux-sh.org)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * These are the "new Hitachi style" interrupts, as present on the
 * Hitachi 7751, the STM ST40 STB1, SH7760, and SH7780.
 */
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <asm/system.h>

static void disable_intc2_irq(unsigned int irq)
{
	struct intc2_data *p = get_irq_chip_data(irq);
	ctrl_outl(1 << p->msk_shift,
		  INTC2_BASE + INTC2_INTMSK_OFFSET + p->msk_offset);
}

static void enable_intc2_irq(unsigned int irq)
{
	struct intc2_data *p = get_irq_chip_data(irq);
	ctrl_outl(1 << p->msk_shift,
		  INTC2_BASE + INTC2_INTMSKCLR_OFFSET + p->msk_offset);
}

static struct irq_chip intc2_irq_chip = {
	.name		= "INTC2",
	.mask		= disable_intc2_irq,
	.unmask		= enable_intc2_irq,
	.mask_ack	= disable_intc2_irq,
};

/*
 * Setup an INTC2 style interrupt.
 * NOTE: Unlike IPR interrupts, parameters are not shifted by this code,
 * allowing the use of the numbers straight out of the datasheet.
 * For example:
 *    PIO1 which is INTPRI00[19,16] and INTMSK00[13]
 * would be:               ^     ^             ^  ^
 *                         |     |             |  |
 *     { 84,		   0,   16,            0, 13 },
 *
 * in the intc2_data table.
 */
void make_intc2_irq(struct intc2_data *table, unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		unsigned long ipr, flags;
		struct intc2_data *p = table + i;

		disable_irq_nosync(p->irq);

		/* Set the priority level */
		local_irq_save(flags);

		ipr = ctrl_inl(INTC2_BASE + INTC2_INTPRI_OFFSET +
			       p->ipr_offset);
		ipr &= ~(0xf << p->ipr_shift);
		ipr |= p->priority << p->ipr_shift;
		ctrl_outl(ipr, INTC2_BASE + INTC2_INTPRI_OFFSET +
			  p->ipr_offset);

		local_irq_restore(flags);

		set_irq_chip_and_handler_name(p->irq, &intc2_irq_chip,
					      handle_level_irq, "level");
		set_irq_chip_data(p->irq, p);

		enable_intc2_irq(p->irq);
	}
}
