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
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/smp.h>

static inline struct intc2_desc *get_intc2_desc(unsigned int irq)
{
	struct irq_chip *chip = get_irq_chip(irq);
	return (void *)((char *)chip - offsetof(struct intc2_desc, chip));
}

static void disable_intc2_irq(unsigned int irq)
{
	struct intc2_data *p = get_irq_chip_data(irq);
	struct intc2_desc *d = get_intc2_desc(irq);

	ctrl_outl(1 << p->msk_shift, d->msk_base + p->msk_offset +
				     (hard_smp_processor_id() * 4));
}

static void enable_intc2_irq(unsigned int irq)
{
	struct intc2_data *p = get_irq_chip_data(irq);
	struct intc2_desc *d = get_intc2_desc(irq);

	ctrl_outl(1 << p->msk_shift, d->mskclr_base + p->msk_offset +
				     (hard_smp_processor_id() * 4));
}

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
void register_intc2_controller(struct intc2_desc *desc)
{
	int i;

	desc->chip.mask = disable_intc2_irq;
	desc->chip.unmask = enable_intc2_irq;
	desc->chip.mask_ack = disable_intc2_irq;

	for (i = 0; i < desc->nr_irqs; i++) {
		unsigned long ipr, flags;
		struct intc2_data *p = desc->intc2_data + i;

		disable_irq_nosync(p->irq);

		if (desc->prio_base) {
			/* Set the priority level */
			local_irq_save(flags);

			ipr = ctrl_inl(desc->prio_base + p->ipr_offset);
			ipr &= ~(0xf << p->ipr_shift);
			ipr |= p->priority << p->ipr_shift;
			ctrl_outl(ipr, desc->prio_base + p->ipr_offset);

			local_irq_restore(flags);
		}

		set_irq_chip_and_handler_name(p->irq, &desc->chip,
					      handle_level_irq, "level");
		set_irq_chip_data(p->irq, p);

		disable_intc2_irq(p->irq);
	}
}
