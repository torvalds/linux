/*
 * Interrupt handling for IPR-based IRQ.
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2003  Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * Copyright (C) 2006  Paul Mundt
 *
 * Supported system:
 *	On-chip supporting modules (TMU, RTC, etc.).
 *	On-chip supporting modules for SH7709/SH7709A/SH7729.
 *	Hitachi SolutionEngine external I/O:
 *		MS7709SE01, MS7709ASE01, and MS7750SE01
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/topology.h>

static inline struct ipr_desc *get_ipr_desc(struct irq_data *data)
{
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	return container_of(chip, struct ipr_desc, chip);
}

static void disable_ipr_irq(struct irq_data *data)
{
	struct ipr_data *p = irq_data_get_irq_chip_data(data);
	unsigned long addr = get_ipr_desc(data)->ipr_offsets[p->ipr_idx];
	/* Set the priority in IPR to 0 */
	__raw_writew(__raw_readw(addr) & (0xffff ^ (0xf << p->shift)), addr);
	(void)__raw_readw(addr);	/* Read back to flush write posting */
}

static void enable_ipr_irq(struct irq_data *data)
{
	struct ipr_data *p = irq_data_get_irq_chip_data(data);
	unsigned long addr = get_ipr_desc(data)->ipr_offsets[p->ipr_idx];
	/* Set priority in IPR back to original value */
	__raw_writew(__raw_readw(addr) | (p->priority << p->shift), addr);
}

/*
 * The shift value is now the number of bits to shift, not the number of
 * bits/4. This is to make it easier to read the value directly from the
 * datasheets. The IPR address is calculated using the ipr_offset table.
 */
void register_ipr_controller(struct ipr_desc *desc)
{
	int i;

	desc->chip.irq_mask = disable_ipr_irq;
	desc->chip.irq_unmask = enable_ipr_irq;

	for (i = 0; i < desc->nr_irqs; i++) {
		struct ipr_data *p = desc->ipr_data + i;
		int res;

		BUG_ON(p->ipr_idx >= desc->nr_offsets);
		BUG_ON(!desc->ipr_offsets[p->ipr_idx]);

		res = irq_alloc_desc_at(p->irq, numa_node_id());
		if (unlikely(res != p->irq && res != -EEXIST)) {
			printk(KERN_INFO "can not get irq_desc for %d\n",
			       p->irq);
			continue;
		}

		disable_irq_nosync(p->irq);
		irq_set_chip_and_handler_name(p->irq, &desc->chip,
					      handle_level_irq, "level");
		irq_set_chip_data(p->irq, p);
		disable_ipr_irq(irq_get_irq_data(p->irq));
	}
}
EXPORT_SYMBOL(register_ipr_controller);
