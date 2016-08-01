/*
 * External Interrupt Controller on Spider South Bridge
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * Author: Arnd Bergmann <arndb@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ioport.h>

#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/io.h>

#include "interrupt.h"

/* register layout taken from Spider spec, table 7.4-4 */
enum {
	TIR_DEN		= 0x004, /* Detection Enable Register */
	TIR_MSK		= 0x084, /* Mask Level Register */
	TIR_EDC		= 0x0c0, /* Edge Detection Clear Register */
	TIR_PNDA	= 0x100, /* Pending Register A */
	TIR_PNDB	= 0x104, /* Pending Register B */
	TIR_CS		= 0x144, /* Current Status Register */
	TIR_LCSA	= 0x150, /* Level Current Status Register A */
	TIR_LCSB	= 0x154, /* Level Current Status Register B */
	TIR_LCSC	= 0x158, /* Level Current Status Register C */
	TIR_LCSD	= 0x15c, /* Level Current Status Register D */
	TIR_CFGA	= 0x200, /* Setting Register A0 */
	TIR_CFGB	= 0x204, /* Setting Register B0 */
			/* 0x208 ... 0x3ff Setting Register An/Bn */
	TIR_PPNDA	= 0x400, /* Packet Pending Register A */
	TIR_PPNDB	= 0x404, /* Packet Pending Register B */
	TIR_PIERA	= 0x408, /* Packet Output Error Register A */
	TIR_PIERB	= 0x40c, /* Packet Output Error Register B */
	TIR_PIEN	= 0x444, /* Packet Output Enable Register */
	TIR_PIPND	= 0x454, /* Packet Output Pending Register */
	TIRDID		= 0x484, /* Spider Device ID Register */
	REISTIM		= 0x500, /* Reissue Command Timeout Time Setting */
	REISTIMEN	= 0x504, /* Reissue Command Timeout Setting */
	REISWAITEN	= 0x508, /* Reissue Wait Control*/
};

#define SPIDER_CHIP_COUNT	4
#define SPIDER_SRC_COUNT	64
#define SPIDER_IRQ_INVALID	63

struct spider_pic {
	struct irq_domain		*host;
	void __iomem		*regs;
	unsigned int		node_id;
};
static struct spider_pic spider_pics[SPIDER_CHIP_COUNT];

static struct spider_pic *spider_irq_data_to_pic(struct irq_data *d)
{
	return irq_data_get_irq_chip_data(d);
}

static void __iomem *spider_get_irq_config(struct spider_pic *pic,
					   unsigned int src)
{
	return pic->regs + TIR_CFGA + 8 * src;
}

static void spider_unmask_irq(struct irq_data *d)
{
	struct spider_pic *pic = spider_irq_data_to_pic(d);
	void __iomem *cfg = spider_get_irq_config(pic, irqd_to_hwirq(d));

	out_be32(cfg, in_be32(cfg) | 0x30000000u);
}

static void spider_mask_irq(struct irq_data *d)
{
	struct spider_pic *pic = spider_irq_data_to_pic(d);
	void __iomem *cfg = spider_get_irq_config(pic, irqd_to_hwirq(d));

	out_be32(cfg, in_be32(cfg) & ~0x30000000u);
}

static void spider_ack_irq(struct irq_data *d)
{
	struct spider_pic *pic = spider_irq_data_to_pic(d);
	unsigned int src = irqd_to_hwirq(d);

	/* Reset edge detection logic if necessary
	 */
	if (irqd_is_level_type(d))
		return;

	/* Only interrupts 47 to 50 can be set to edge */
	if (src < 47 || src > 50)
		return;

	/* Perform the clear of the edge logic */
	out_be32(pic->regs + TIR_EDC, 0x100 | (src & 0xf));
}

static int spider_set_irq_type(struct irq_data *d, unsigned int type)
{
	unsigned int sense = type & IRQ_TYPE_SENSE_MASK;
	struct spider_pic *pic = spider_irq_data_to_pic(d);
	unsigned int hw = irqd_to_hwirq(d);
	void __iomem *cfg = spider_get_irq_config(pic, hw);
	u32 old_mask;
	u32 ic;

	/* Note that only level high is supported for most interrupts */
	if (sense != IRQ_TYPE_NONE && sense != IRQ_TYPE_LEVEL_HIGH &&
	    (hw < 47 || hw > 50))
		return -EINVAL;

	/* Decode sense type */
	switch(sense) {
	case IRQ_TYPE_EDGE_RISING:
		ic = 0x3;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		ic = 0x2;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		ic = 0x0;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_NONE:
		ic = 0x1;
		break;
	default:
		return -EINVAL;
	}

	/* Configure the source. One gross hack that was there before and
	 * that I've kept around is the priority to the BE which I set to
	 * be the same as the interrupt source number. I don't know whether
	 * that's supposed to make any kind of sense however, we'll have to
	 * decide that, but for now, I'm not changing the behaviour.
	 */
	old_mask = in_be32(cfg) & 0x30000000u;
	out_be32(cfg, old_mask | (ic << 24) | (0x7 << 16) |
		 (pic->node_id << 4) | 0xe);
	out_be32(cfg + 4, (0x2 << 16) | (hw & 0xff));

	return 0;
}

static struct irq_chip spider_pic = {
	.name = "SPIDER",
	.irq_unmask = spider_unmask_irq,
	.irq_mask = spider_mask_irq,
	.irq_ack = spider_ack_irq,
	.irq_set_type = spider_set_irq_type,
};

static int spider_host_map(struct irq_domain *h, unsigned int virq,
			irq_hw_number_t hw)
{
	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &spider_pic, handle_level_irq);

	/* Set default irq type */
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int spider_host_xlate(struct irq_domain *h, struct device_node *ct,
			   const u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	/* Spider interrupts have 2 cells, first is the interrupt source,
	 * second, well, I don't know for sure yet ... We mask the top bits
	 * because old device-trees encode a node number in there
	 */
	*out_hwirq = intspec[0] & 0x3f;
	*out_flags = IRQ_TYPE_LEVEL_HIGH;
	return 0;
}

static const struct irq_domain_ops spider_host_ops = {
	.map = spider_host_map,
	.xlate = spider_host_xlate,
};

static void spider_irq_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct spider_pic *pic = irq_desc_get_handler_data(desc);
	unsigned int cs, virq;

	cs = in_be32(pic->regs + TIR_CS) >> 24;
	if (cs == SPIDER_IRQ_INVALID)
		virq = NO_IRQ;
	else
		virq = irq_linear_revmap(pic->host, cs);

	if (virq != NO_IRQ)
		generic_handle_irq(virq);

	chip->irq_eoi(&desc->irq_data);
}

/* For hooking up the cascade we have a problem. Our device-tree is
 * crap and we don't know on which BE iic interrupt we are hooked on at
 * least not the "standard" way. We can reconstitute it based on two
 * informations though: which BE node we are connected to and whether
 * we are connected to IOIF0 or IOIF1. Right now, we really only care
 * about the IBM cell blade and we know that its firmware gives us an
 * interrupt-map property which is pretty strange.
 */
static unsigned int __init spider_find_cascade_and_node(struct spider_pic *pic)
{
	unsigned int virq;
	const u32 *imap, *tmp;
	int imaplen, intsize, unit;
	struct device_node *iic;
	struct device_node *of_node;

	of_node = irq_domain_get_of_node(pic->host);

	/* First, we check whether we have a real "interrupts" in the device
	 * tree in case the device-tree is ever fixed
	 */
	virq = irq_of_parse_and_map(of_node, 0);
	if (virq)
		return virq;

	/* Now do the horrible hacks */
	tmp = of_get_property(of_node, "#interrupt-cells", NULL);
	if (tmp == NULL)
		return NO_IRQ;
	intsize = *tmp;
	imap = of_get_property(of_node, "interrupt-map", &imaplen);
	if (imap == NULL || imaplen < (intsize + 1))
		return NO_IRQ;
	iic = of_find_node_by_phandle(imap[intsize]);
	if (iic == NULL)
		return NO_IRQ;
	imap += intsize + 1;
	tmp = of_get_property(iic, "#interrupt-cells", NULL);
	if (tmp == NULL) {
		of_node_put(iic);
		return NO_IRQ;
	}
	intsize = *tmp;
	/* Assume unit is last entry of interrupt specifier */
	unit = imap[intsize - 1];
	/* Ok, we have a unit, now let's try to get the node */
	tmp = of_get_property(iic, "ibm,interrupt-server-ranges", NULL);
	if (tmp == NULL) {
		of_node_put(iic);
		return NO_IRQ;
	}
	/* ugly as hell but works for now */
	pic->node_id = (*tmp) >> 1;
	of_node_put(iic);

	/* Ok, now let's get cracking. You may ask me why I just didn't match
	 * the iic host from the iic OF node, but that way I'm still compatible
	 * with really really old old firmwares for which we don't have a node
	 */
	/* Manufacture an IIC interrupt number of class 2 */
	virq = irq_create_mapping(NULL,
				  (pic->node_id << IIC_IRQ_NODE_SHIFT) |
				  (2 << IIC_IRQ_CLASS_SHIFT) |
				  unit);
	if (virq == NO_IRQ)
		printk(KERN_ERR "spider_pic: failed to map cascade !");
	return virq;
}


static void __init spider_init_one(struct device_node *of_node, int chip,
				   unsigned long addr)
{
	struct spider_pic *pic = &spider_pics[chip];
	int i, virq;

	/* Map registers */
	pic->regs = ioremap(addr, 0x1000);
	if (pic->regs == NULL)
		panic("spider_pic: can't map registers !");

	/* Allocate a host */
	pic->host = irq_domain_add_linear(of_node, SPIDER_SRC_COUNT,
					  &spider_host_ops, pic);
	if (pic->host == NULL)
		panic("spider_pic: can't allocate irq host !");

	/* Go through all sources and disable them */
	for (i = 0; i < SPIDER_SRC_COUNT; i++) {
		void __iomem *cfg = pic->regs + TIR_CFGA + 8 * i;
		out_be32(cfg, in_be32(cfg) & ~0x30000000u);
	}

	/* do not mask any interrupts because of level */
	out_be32(pic->regs + TIR_MSK, 0x0);

	/* enable interrupt packets to be output */
	out_be32(pic->regs + TIR_PIEN, in_be32(pic->regs + TIR_PIEN) | 0x1);

	/* Hook up the cascade interrupt to the iic and nodeid */
	virq = spider_find_cascade_and_node(pic);
	if (virq == NO_IRQ)
		return;
	irq_set_handler_data(virq, pic);
	irq_set_chained_handler(virq, spider_irq_cascade);

	printk(KERN_INFO "spider_pic: node %d, addr: 0x%lx %s\n",
	       pic->node_id, addr, of_node->full_name);

	/* Enable the interrupt detection enable bit. Do this last! */
	out_be32(pic->regs + TIR_DEN, in_be32(pic->regs + TIR_DEN) | 0x1);
}

void __init spider_init_IRQ(void)
{
	struct resource r;
	struct device_node *dn;
	int chip = 0;

	/* XXX node numbers are totally bogus. We _hope_ we get the device
	 * nodes in the right order here but that's definitely not guaranteed,
	 * we need to get the node from the device tree instead.
	 * There is currently no proper property for it (but our whole
	 * device-tree is bogus anyway) so all we can do is pray or maybe test
	 * the address and deduce the node-id
	 */
	for (dn = NULL;
	     (dn = of_find_node_by_name(dn, "interrupt-controller"));) {
		if (of_device_is_compatible(dn, "CBEA,platform-spider-pic")) {
			if (of_address_to_resource(dn, 0, &r)) {
				printk(KERN_WARNING "spider-pic: Failed\n");
				continue;
			}
		} else if (of_device_is_compatible(dn, "sti,platform-spider-pic")
			   && (chip < 2)) {
			static long hard_coded_pics[] =
				{ 0x24000008000ul, 0x34000008000ul};
			r.start = hard_coded_pics[chip];
		} else
			continue;
		spider_init_one(dn, chip++, r.start);
	}
}
