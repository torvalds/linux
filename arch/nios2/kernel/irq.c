/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2008 Thomas Chou <thomas@wytron.com.tw>
 *
 * based on irq.c from m68k which is:
 *
 * Copyright (C) 2007 Greg Ungerer <gerg@snapgear.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/of.h>

asmlinkage void do_IRQ(int hwirq, struct pt_regs *regs)
{
	struct pt_regs *oldregs = set_irq_regs(regs);
	int irq;

	irq_enter();
	irq = irq_find_mapping(NULL, hwirq);
	generic_handle_irq(irq);
	irq_exit();

	set_irq_regs(oldregs);
}

static void chip_unmask(struct irq_data *d)
{
	unsigned ien;
	ien = RDCTL(CTL_IENABLE);
	ien |= (1 << d->hwirq);
	WRCTL(CTL_IENABLE, ien);
}

static void chip_mask(struct irq_data *d)
{
	unsigned ien;
	ien = RDCTL(CTL_IENABLE);
	ien &= ~(1 << d->hwirq);
	WRCTL(CTL_IENABLE, ien);
}

static struct irq_chip m_irq_chip = {
	.name		= "NIOS2-INTC",
	.irq_unmask	= chip_unmask,
	.irq_mask	= chip_mask,
};

static int irq_map(struct irq_domain *h, unsigned int virq,
				irq_hw_number_t hw_irq_num)
{
	irq_set_chip_and_handler(virq, &m_irq_chip, handle_level_irq);

	return 0;
}

static struct irq_domain_ops irq_ops = {
	.map	= irq_map,
	.xlate	= irq_domain_xlate_onecell,
};

void __init init_IRQ(void)
{
	struct irq_domain *domain;
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "ALTR,nios2-1.0");
	BUG_ON(!node);

	domain = irq_domain_add_linear(node, NIOS2_CPU_NR_IRQS, &irq_ops, NULL);
	BUG_ON(!domain);

	irq_set_default_host(domain);
	of_node_put(node);
}
