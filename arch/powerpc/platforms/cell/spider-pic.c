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

static void __iomem *spider_pics[4];

static void __iomem *spider_get_pic(int irq)
{
	int node = irq / IIC_NODE_STRIDE;
	irq %= IIC_NODE_STRIDE;

	if (irq >= IIC_EXT_OFFSET &&
	    irq < IIC_EXT_OFFSET + IIC_NUM_EXT &&
	    spider_pics)
		return spider_pics[node];
	return NULL;
}

static int spider_get_nr(unsigned int irq)
{
	return (irq % IIC_NODE_STRIDE) - IIC_EXT_OFFSET;
}

static void __iomem *spider_get_irq_config(int irq)
{
	void __iomem *pic;
	pic = spider_get_pic(irq);
	return pic + TIR_CFGA + 8 * spider_get_nr(irq);
}

static void spider_unmask_irq(unsigned int irq)
{
	int nodeid = (irq / IIC_NODE_STRIDE) * 0x10;
	void __iomem *cfg = spider_get_irq_config(irq);
	irq = spider_get_nr(irq);

	/* FIXME: Most of that is configuration and has nothing to do with enabling/disable,
	 * besides, it's also partially bogus.
	 */
	out_be32(cfg, (in_be32(cfg) & ~0xf0)| 0x3107000eu | nodeid);
	out_be32(cfg + 4, in_be32(cfg + 4) | 0x00020000u | irq);
}

static void spider_mask_irq(unsigned int irq)
{
	void __iomem *cfg = spider_get_irq_config(irq);
	irq = spider_get_nr(irq);

	out_be32(cfg, in_be32(cfg) & ~0x30000000u);
}

static void spider_ack_irq(unsigned int irq)
{
	/* Should reset edge detection logic but we don't configure any edge interrupt
	 * at the moment.
	 */
}

static struct irq_chip spider_pic = {
	.typename = " SPIDER   ",
	.unmask = spider_unmask_irq,
	.mask = spider_mask_irq,
	.ack = spider_ack_irq,
};

static int spider_get_irq(int node)
{
	unsigned long cs;
	void __iomem *regs = spider_pics[node];

	cs = in_be32(regs + TIR_CS) >> 24;

	if (cs == 63)
		return -1;
	else
		return cs;
}

static void spider_irq_cascade(unsigned int irq, struct irq_desc *desc,
			       struct pt_regs *regs)
{
	int node = (int)(long)desc->handler_data;
	int cascade_irq;

	cascade_irq = spider_get_irq(node);
	generic_handle_irq(cascade_irq, regs);
	desc->chip->eoi(irq);
}

/* hardcoded part to be compatible with older firmware */

static void __init spider_init_one(int node, unsigned long addr)
{
	int n, irq;

	spider_pics[node] = ioremap(addr, 0x800);
	if (spider_pics[node] == NULL)
		panic("spider_pic: can't map registers !");

	printk(KERN_INFO "spider_pic: mapped for node %d, addr: 0x%lx mapped to %p\n",
	       node, addr, spider_pics[node]);

	for (n = 0; n < IIC_NUM_EXT; n++) {
		if (n == IIC_EXT_CASCADE)
			continue;
		irq = n + IIC_EXT_OFFSET + node * IIC_NODE_STRIDE;
		set_irq_chip_and_handler(irq, &spider_pic, handle_level_irq);
		get_irq_desc(irq)->status |= IRQ_LEVEL;
	}

	/* do not mask any interrupts because of level */
	out_be32(spider_pics[node] + TIR_MSK, 0x0);

	/* disable edge detection clear */
	/* out_be32(spider_pics[node] + TIR_EDC, 0x0); */

	/* enable interrupt packets to be output */
	out_be32(spider_pics[node] + TIR_PIEN,
		 in_be32(spider_pics[node] + TIR_PIEN) | 0x1);

	/* Hook up cascade */
	irq = IIC_EXT_CASCADE + node * IIC_NODE_STRIDE;
	set_irq_data(irq, (void *)(long)node);
	set_irq_chained_handler(irq, spider_irq_cascade);

	/* Enable the interrupt detection enable bit. Do this last! */
	out_be32(spider_pics[node] + TIR_DEN,
		 in_be32(spider_pics[node] + TIR_DEN) | 0x1);
}

void __init spider_init_IRQ(void)
{
	unsigned long *spider_reg;
	struct device_node *dn;
	char *compatible;
	int node = 0;

	/* XXX node numbers are totally bogus. We _hope_ we get the device nodes in the right
	 * order here but that's definitely not guaranteed, we need to get the node from the
	 * device tree instead. There is currently no proper property for it (but our whole
	 * device-tree is bogus anyway) so all we can do is pray or maybe test the address
	 * and deduce the node-id
	 */
	for (dn = NULL; (dn = of_find_node_by_name(dn, "interrupt-controller"));) {
		compatible = (char *)get_property(dn, "compatible", NULL);

		if (!compatible)
			continue;

 		if (strstr(compatible, "CBEA,platform-spider-pic"))
			spider_reg = (unsigned long *)get_property(dn, "reg", NULL);
		else if (strstr(compatible, "sti,platform-spider-pic") && (node < 2)) {
			static long hard_coded_pics[] = { 0x24000008000, 0x34000008000 };
			spider_reg = &hard_coded_pics[node];
		} else
			continue;

		if (spider_reg == NULL)
			printk(KERN_ERR "spider_pic: No address for node %d\n", node);

		spider_init_one(node, *spider_reg);
		node++;
	}
}
