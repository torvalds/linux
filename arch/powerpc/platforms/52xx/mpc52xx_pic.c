/*
 *
 * Programmable Interrupt Controller functions for the Freescale MPC52xx.
 *
 * Copyright (C) 2006 bplan GmbH
 *
 * Based on the code from the 2.4 kernel by
 * Dale Farnsworth <dfarnsworth@mvista.com> and Kent Borg.
 *
 * Copyright (C) 2004 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 Montavista Software, Inc
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#undef DEBUG

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>
#include "mpc52xx_pic.h"

/*
 *
*/

/* MPC5200 device tree match tables */
static struct of_device_id mpc52xx_pic_ids[] __initdata = {
	{ .compatible = "fsl,mpc5200-pic", },
	{ .compatible = "mpc5200-pic", },
	{}
};
static struct of_device_id mpc52xx_sdma_ids[] __initdata = {
	{ .compatible = "fsl,mpc5200-bestcomm", },
	{ .compatible = "mpc5200-bestcomm", },
	{}
};

static struct mpc52xx_intr __iomem *intr;
static struct mpc52xx_sdma __iomem *sdma;
static struct irq_host *mpc52xx_irqhost = NULL;

static unsigned char mpc52xx_map_senses[4] = {
	IRQ_TYPE_LEVEL_HIGH,
	IRQ_TYPE_EDGE_RISING,
	IRQ_TYPE_EDGE_FALLING,
	IRQ_TYPE_LEVEL_LOW,
};

/*
 *
*/

static inline void io_be_setbit(u32 __iomem *addr, int bitno)
{
	out_be32(addr, in_be32(addr) | (1 << bitno));
}

static inline void io_be_clrbit(u32 __iomem *addr, int bitno)
{
	out_be32(addr, in_be32(addr) & ~(1 << bitno));
}

/*
 * IRQ[0-3] interrupt irq_chip
*/

static void mpc52xx_extirq_mask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_clrbit(&intr->ctrl, 11 - l2irq);
}

static void mpc52xx_extirq_unmask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_setbit(&intr->ctrl, 11 - l2irq);
}

static void mpc52xx_extirq_ack(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_setbit(&intr->ctrl, 27-l2irq);
}

static int mpc52xx_extirq_set_type(unsigned int virq, unsigned int flow_type)
{
	u32 ctrl_reg, type;
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d flow_type=%d\n", __func__, irq, l2irq, flow_type);

	switch (flow_type) {
	case IRQF_TRIGGER_HIGH:
		type = 0;
		break;
	case IRQF_TRIGGER_RISING:
		type = 1;
		break;
	case IRQF_TRIGGER_FALLING:
		type = 2;
		break;
	case IRQF_TRIGGER_LOW:
		type = 3;
		break;
	default:
		type = 0;
	}

	ctrl_reg = in_be32(&intr->ctrl);
	ctrl_reg &= ~(0x3 << (22 - (l2irq * 2)));
	ctrl_reg |= (type << (22 - (l2irq * 2)));
	out_be32(&intr->ctrl, ctrl_reg);

	return 0;
}

static struct irq_chip mpc52xx_extirq_irqchip = {
	.typename = " MPC52xx IRQ[0-3] ",
	.mask = mpc52xx_extirq_mask,
	.unmask = mpc52xx_extirq_unmask,
	.ack = mpc52xx_extirq_ack,
	.set_type = mpc52xx_extirq_set_type,
};

/*
 * Main interrupt irq_chip
*/

static void mpc52xx_main_mask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_setbit(&intr->main_mask, 16 - l2irq);
}

static void mpc52xx_main_unmask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_clrbit(&intr->main_mask, 16 - l2irq);
}

static struct irq_chip mpc52xx_main_irqchip = {
	.typename = "MPC52xx Main",
	.mask = mpc52xx_main_mask,
	.mask_ack = mpc52xx_main_mask,
	.unmask = mpc52xx_main_unmask,
};

/*
 * Peripherals interrupt irq_chip
*/

static void mpc52xx_periph_mask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_setbit(&intr->per_mask, 31 - l2irq);
}

static void mpc52xx_periph_unmask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_clrbit(&intr->per_mask, 31 - l2irq);
}

static struct irq_chip mpc52xx_periph_irqchip = {
	.typename = "MPC52xx Peripherals",
	.mask = mpc52xx_periph_mask,
	.mask_ack = mpc52xx_periph_mask,
	.unmask = mpc52xx_periph_unmask,
};

/*
 * SDMA interrupt irq_chip
*/

static void mpc52xx_sdma_mask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_setbit(&sdma->IntMask, l2irq);
}

static void mpc52xx_sdma_unmask(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	io_be_clrbit(&sdma->IntMask, l2irq);
}

static void mpc52xx_sdma_ack(unsigned int virq)
{
	int irq;
	int l2irq;

	irq = irq_map[virq].hwirq;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	pr_debug("%s: irq=%x. l2=%d\n", __func__, irq, l2irq);

	out_be32(&sdma->IntPend, 1 << l2irq);
}

static struct irq_chip mpc52xx_sdma_irqchip = {
	.typename = "MPC52xx SDMA",
	.mask = mpc52xx_sdma_mask,
	.unmask = mpc52xx_sdma_unmask,
	.ack = mpc52xx_sdma_ack,
};

/*
 * irq_host
*/

static int mpc52xx_irqhost_xlate(struct irq_host *h, struct device_node *ct,
				 u32 * intspec, unsigned int intsize,
				 irq_hw_number_t * out_hwirq,
				 unsigned int *out_flags)
{
	int intrvect_l1;
	int intrvect_l2;
	int intrvect_type;
	int intrvect_linux;

	if (intsize != 3)
		return -1;

	intrvect_l1 = (int)intspec[0];
	intrvect_l2 = (int)intspec[1];
	intrvect_type = (int)intspec[2];

	intrvect_linux =
	    (intrvect_l1 << MPC52xx_IRQ_L1_OFFSET) & MPC52xx_IRQ_L1_MASK;
	intrvect_linux |=
	    (intrvect_l2 << MPC52xx_IRQ_L2_OFFSET) & MPC52xx_IRQ_L2_MASK;

	pr_debug("return %x, l1=%d, l2=%d\n", intrvect_linux, intrvect_l1,
		 intrvect_l2);

	*out_hwirq = intrvect_linux;
	*out_flags = mpc52xx_map_senses[intrvect_type];

	return 0;
}

/*
 * this function retrieves the correct IRQ type out
 * of the MPC regs
 * Only externals IRQs needs this
*/
static int mpc52xx_irqx_gettype(int irq)
{
	int type;
	u32 ctrl_reg;

	ctrl_reg = in_be32(&intr->ctrl);
	type = (ctrl_reg >> (22 - irq * 2)) & 0x3;

	return mpc52xx_map_senses[type];
}

static int mpc52xx_irqhost_map(struct irq_host *h, unsigned int virq,
			       irq_hw_number_t irq)
{
	int l1irq;
	int l2irq;
	struct irq_chip *good_irqchip;
	void *good_handle;
	int type;

	l1irq = (irq & MPC52xx_IRQ_L1_MASK) >> MPC52xx_IRQ_L1_OFFSET;
	l2irq = (irq & MPC52xx_IRQ_L2_MASK) >> MPC52xx_IRQ_L2_OFFSET;

	/*
	 * Most of ours IRQs will be level low
	 * Only external IRQs on some platform may be others
	 */
	type = IRQ_TYPE_LEVEL_LOW;

	switch (l1irq) {
	case MPC52xx_IRQ_L1_CRIT:
		pr_debug("%s: Critical. l2=%x\n", __func__, l2irq);

		BUG_ON(l2irq != 0);

		type = mpc52xx_irqx_gettype(l2irq);
		good_irqchip = &mpc52xx_extirq_irqchip;
		break;

	case MPC52xx_IRQ_L1_MAIN:
		pr_debug("%s: Main IRQ[1-3] l2=%x\n", __func__, l2irq);

		if ((l2irq >= 1) && (l2irq <= 3)) {
			type = mpc52xx_irqx_gettype(l2irq);
			good_irqchip = &mpc52xx_extirq_irqchip;
		} else {
			good_irqchip = &mpc52xx_main_irqchip;
		}
		break;

	case MPC52xx_IRQ_L1_PERP:
		pr_debug("%s: Peripherals. l2=%x\n", __func__, l2irq);
		good_irqchip = &mpc52xx_periph_irqchip;
		break;

	case MPC52xx_IRQ_L1_SDMA:
		pr_debug("%s: SDMA. l2=%x\n", __func__, l2irq);
		good_irqchip = &mpc52xx_sdma_irqchip;
		break;

	default:
		pr_debug("%s: Error, unknown L1 IRQ (0x%x)\n", __func__, l1irq);
		printk(KERN_ERR "Unknow IRQ!\n");
		return -EINVAL;
	}

	switch (type) {
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_RISING:
		good_handle = handle_edge_irq;
		break;
	default:
		good_handle = handle_level_irq;
	}

	set_irq_chip_and_handler(virq, good_irqchip, good_handle);

	pr_debug("%s: virq=%x, hw=%x. type=%x\n", __func__, virq,
		 (int)irq, type);

	return 0;
}

static struct irq_host_ops mpc52xx_irqhost_ops = {
	.xlate = mpc52xx_irqhost_xlate,
	.map = mpc52xx_irqhost_map,
};

/*
 * init (public)
*/

void __init mpc52xx_init_irq(void)
{
	u32 intr_ctrl;
	struct device_node *picnode;
	struct device_node *np;

	/* Remap the necessary zones */
	picnode = of_find_matching_node(NULL, mpc52xx_pic_ids);
	intr = of_iomap(picnode, 0);
	if (!intr)
		panic(__FILE__	": find_and_map failed on 'mpc5200-pic'. "
				"Check node !");

	np = of_find_matching_node(NULL, mpc52xx_sdma_ids);
	sdma = of_iomap(np, 0);
	of_node_put(np);
	if (!sdma)
		panic(__FILE__	": find_and_map failed on 'mpc5200-bestcomm'. "
				"Check node !");

	/* Disable all interrupt sources. */
	out_be32(&sdma->IntPend, 0xffffffff);	/* 1 means clear pending */
	out_be32(&sdma->IntMask, 0xffffffff);	/* 1 means disabled */
	out_be32(&intr->per_mask, 0x7ffffc00);	/* 1 means disabled */
	out_be32(&intr->main_mask, 0x00010fff);	/* 1 means disabled */
	intr_ctrl = in_be32(&intr->ctrl);
	intr_ctrl &= 0x00ff0000;	/* Keeps IRQ[0-3] config */
	intr_ctrl |=	0x0f000000 |	/* clear IRQ 0-3 */
			0x00001000 |	/* MEE master external enable */
			0x00000000 |	/* 0 means disable IRQ 0-3 */
			0x00000001;	/* CEb route critical normally */
	out_be32(&intr->ctrl, intr_ctrl);

	/* Zero a bunch of the priority settings. */
	out_be32(&intr->per_pri1, 0);
	out_be32(&intr->per_pri2, 0);
	out_be32(&intr->per_pri3, 0);
	out_be32(&intr->main_pri1, 0);
	out_be32(&intr->main_pri2, 0);

	/*
	 * As last step, add an irq host to translate the real
	 * hw irq information provided by the ofw to linux virq
	 */

	mpc52xx_irqhost = irq_alloc_host(picnode, IRQ_HOST_MAP_LINEAR,
	                                 MPC52xx_IRQ_HIGHTESTHWIRQ,
	                                 &mpc52xx_irqhost_ops, -1);

	if (!mpc52xx_irqhost)
		panic(__FILE__ ": Cannot allocate the IRQ host\n");

	printk(KERN_INFO "MPC52xx PIC is up and running!\n");
}

/*
 * get_irq (public)
*/
unsigned int mpc52xx_get_irq(void)
{
	u32 status;
	int irq = NO_IRQ_IGNORE;

	status = in_be32(&intr->enc_status);
	if (status & 0x00000400) {	/* critical */
		irq = (status >> 8) & 0x3;
		if (irq == 2)	/* high priority peripheral */
			goto peripheral;
		irq |=	(MPC52xx_IRQ_L1_CRIT << MPC52xx_IRQ_L1_OFFSET) &
			MPC52xx_IRQ_L1_MASK;
	} else if (status & 0x00200000) {	/* main */
		irq = (status >> 16) & 0x1f;
		if (irq == 4)	/* low priority peripheral */
			goto peripheral;
		irq |=	(MPC52xx_IRQ_L1_MAIN << MPC52xx_IRQ_L1_OFFSET) &
			MPC52xx_IRQ_L1_MASK;
	} else if (status & 0x20000000) {	/* peripheral */
	      peripheral:
		irq = (status >> 24) & 0x1f;
		if (irq == 0) {	/* bestcomm */
			status = in_be32(&sdma->IntPend);
			irq = ffs(status) - 1;
			irq |=	(MPC52xx_IRQ_L1_SDMA << MPC52xx_IRQ_L1_OFFSET) &
				MPC52xx_IRQ_L1_MASK;
		} else {
			irq |=	(MPC52xx_IRQ_L1_PERP << MPC52xx_IRQ_L1_OFFSET) &
				MPC52xx_IRQ_L1_MASK;
		}
	}

	pr_debug("%s: irq=%x. virq=%d\n", __func__, irq,
		 irq_linear_revmap(mpc52xx_irqhost, irq));

	return irq_linear_revmap(mpc52xx_irqhost, irq);
}
