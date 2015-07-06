/*
 * H8/300H interrupt controller driver
 *
 * Copyright 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/io.h>

#include "irqchip.h"

static const char ipr_bit[] = {
	 7,  6,  5,  5,
	 4,  4,  4,  4,  3,  3,  3,  3,
	 2,  2,  2,  2,  1,  1,  1,  1,
	 0,  0,  0,  0, 15, 15, 15, 15,
	14, 14, 14, 14, 13, 13, 13, 13,
	-1, -1, -1, -1, 11, 11, 11, 11,
	10, 10, 10, 10,  9,  9,  9,  9,
};

static void *intc_baseaddr;

#define IPR ((unsigned long)intc_baseaddr + 6)

static void h8300h_disable_irq(struct irq_data *data)
{
	int bit;
	int irq = data->irq - 12;

	bit = ipr_bit[irq];
	if (bit >= 0) {
		if (bit < 8)
			ctrl_bclr(bit & 7, IPR);
		else
			ctrl_bclr(bit & 7, (IPR+1));
	}
}

static void h8300h_enable_irq(struct irq_data *data)
{
	int bit;
	int irq = data->irq - 12;

	bit = ipr_bit[irq];
	if (bit >= 0) {
		if (bit < 8)
			ctrl_bset(bit & 7, IPR);
		else
			ctrl_bset(bit & 7, (IPR+1));
	}
}

struct irq_chip h8300h_irq_chip = {
	.name		= "H8/300H-INTC",
	.irq_enable	= h8300h_enable_irq,
	.irq_disable	= h8300h_disable_irq,
};

static int irq_map(struct irq_domain *h, unsigned int virq,
		   irq_hw_number_t hw_irq_num)
{
       irq_set_chip_and_handler(virq, &h8300h_irq_chip, handle_simple_irq);

       return 0;
}

static struct irq_domain_ops irq_ops = {
       .map    = irq_map,
       .xlate  = irq_domain_xlate_onecell,
};

static int __init h8300h_intc_of_init(struct device_node *intc,
				      struct device_node *parent)
{
	struct irq_domain *domain;

	intc_baseaddr = of_iomap(intc, 0);
	BUG_ON(!intc_baseaddr);

	/* All interrupt priority low */
	ctrl_outb(0x00, IPR + 0);
	ctrl_outb(0x00, IPR + 1);

	domain = irq_domain_add_linear(intc, NR_IRQS, &irq_ops, NULL);
	BUG_ON(!domain);
	irq_set_default_host(domain);
	return 0;
}

IRQCHIP_DECLARE(h8300h_intc, "renesas,h8300h-intc", h8300h_intc_of_init);
