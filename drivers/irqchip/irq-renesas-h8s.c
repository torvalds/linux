// SPDX-License-Identifier: GPL-2.0
/*
 * H8S interrupt contoller driver
 *
 * Copyright 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <asm/io.h>

static void *intc_baseaddr;
#define IPRA ((unsigned long)intc_baseaddr)

static const unsigned char ipr_table[] = {
	0x03, 0x02, 0x01, 0x00, 0x13, 0x12, 0x11, 0x10, /* 16 - 23 */
	0x23, 0x22, 0x21, 0x20, 0x33, 0x32, 0x31, 0x30, /* 24 - 31 */
	0x43, 0x42, 0x41, 0x40, 0x53, 0x53, 0x52, 0x52, /* 32 - 39 */
	0x51, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51, 0x51, /* 40 - 47 */
	0x50, 0x50, 0x50, 0x50, 0x63, 0x63, 0x63, 0x63, /* 48 - 55 */
	0x62, 0x62, 0x62, 0x62, 0x62, 0x62, 0x62, 0x62, /* 56 - 63 */
	0x61, 0x61, 0x61, 0x61, 0x60, 0x60, 0x60, 0x60, /* 64 - 71 */
	0x73, 0x73, 0x73, 0x73, 0x72, 0x72, 0x72, 0x72, /* 72 - 79 */
	0x71, 0x71, 0x71, 0x71, 0x70, 0x83, 0x82, 0x81, /* 80 - 87 */
	0x80, 0x80, 0x80, 0x80, 0x93, 0x93, 0x93, 0x93, /* 88 - 95 */
	0x92, 0x92, 0x92, 0x92, 0x91, 0x91, 0x91, 0x91, /* 96 - 103 */
	0x90, 0x90, 0x90, 0x90, 0xa3, 0xa3, 0xa3, 0xa3, /* 104 - 111 */
	0xa2, 0xa2, 0xa2, 0xa2, 0xa1, 0xa1, 0xa1, 0xa1, /* 112 - 119 */
	0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, /* 120 - 127 */
};

static void h8s_disable_irq(struct irq_data *data)
{
	int pos;
	unsigned int addr;
	unsigned short pri;
	int irq = data->irq;

	addr = IPRA + ((ipr_table[irq - 16] & 0xf0) >> 3);
	pos = (ipr_table[irq - 16] & 0x0f) * 4;
	pri = ~(0x000f << pos);
	pri &= readw(addr);
	writew(pri, addr);
}

static void h8s_enable_irq(struct irq_data *data)
{
	int pos;
	unsigned int addr;
	unsigned short pri;
	int irq = data->irq;

	addr = IPRA + ((ipr_table[irq - 16] & 0xf0) >> 3);
	pos = (ipr_table[irq - 16] & 0x0f) * 4;
	pri = ~(0x000f << pos);
	pri &= readw(addr);
	pri |= 1 << pos;
	writew(pri, addr);
}

struct irq_chip h8s_irq_chip = {
	.name		= "H8S-INTC",
	.irq_enable	= h8s_enable_irq,
	.irq_disable	= h8s_disable_irq,
};

static __init int irq_map(struct irq_domain *h, unsigned int virq,
			  irq_hw_number_t hw_irq_num)
{
       irq_set_chip_and_handler(virq, &h8s_irq_chip, handle_simple_irq);

       return 0;
}

static const struct irq_domain_ops irq_ops = {
       .map    = irq_map,
       .xlate  = irq_domain_xlate_onecell,
};

static int __init h8s_intc_of_init(struct device_node *intc,
				   struct device_node *parent)
{
	struct irq_domain *domain;
	int n;

	intc_baseaddr = of_iomap(intc, 0);
	BUG_ON(!intc_baseaddr);

	/* All interrupt priority is 0 (disable) */
	/* IPRA to IPRK */
	for (n = 0; n <= 'k' - 'a'; n++)
		writew(0x0000, IPRA + (n * 2));

	domain = irq_domain_add_linear(intc, NR_IRQS, &irq_ops, NULL);
	BUG_ON(!domain);
	irq_set_default_host(domain);
	return 0;
}

IRQCHIP_DECLARE(h8s_intc, "renesas,h8s-intc", h8s_intc_of_init);
