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
 *	On-chip supporting modules for SH7709/SH7709A/SH7729/SH7300.
 *	Hitachi SolutionEngine external I/O:
 *		MS7709SE01, MS7709ASE01, and MS7750SE01
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>

static void disable_ipr_irq(unsigned int irq)
{
	struct ipr_data *p = get_irq_chip_data(irq);
	/* Set the priority in IPR to 0 */
	ctrl_outw(ctrl_inw(p->addr) & (0xffff ^ (0xf << p->shift)), p->addr);
}

static void enable_ipr_irq(unsigned int irq)
{
	struct ipr_data *p = get_irq_chip_data(irq);
	/* Set priority in IPR back to original value */
	ctrl_outw(ctrl_inw(p->addr) | (p->priority << p->shift), p->addr);
}

static struct irq_chip ipr_irq_chip = {
	.name		= "IPR",
	.mask		= disable_ipr_irq,
	.unmask		= enable_ipr_irq,
	.mask_ack	= disable_ipr_irq,
};

unsigned int map_ipridx_to_addr(int idx) __attribute__ ((weak));
unsigned int map_ipridx_to_addr(int idx)
{
	return 0;
}

void make_ipr_irq(struct ipr_data *table, unsigned int nr_irqs)
{
	int i;

	for (i = 0; i < nr_irqs; i++) {
		unsigned int irq = table[i].irq;

		if (!irq)
			irq = table[i].irq = i;

		/* could the IPR index be mapped, if not we ignore this */
		if (!table[i].addr) {
			table[i].addr = map_ipridx_to_addr(table[i].ipr_idx);
			if (!table[i].addr)
				continue;
		}

		disable_irq_nosync(irq);
		set_irq_chip_and_handler_name(irq, &ipr_irq_chip,
				      handle_level_irq, "level");
		set_irq_chip_data(irq, &table[i]);
		enable_ipr_irq(irq);
	}
}
EXPORT_SYMBOL(make_ipr_irq);

#if !defined(CONFIG_CPU_HAS_PINT_IRQ)
int ipr_irq_demux(int irq)
{
	return irq;
}
#endif
