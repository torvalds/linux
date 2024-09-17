// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <asm/mach/irq.h>
#include <asm/exception.h>

#include "common.h"
#include "hardware.h"
#include "irq-common.h"

#define AVIC_INTCNTL		0x00	/* int control reg */
#define AVIC_NIMASK		0x04	/* int mask reg */
#define AVIC_INTENNUM		0x08	/* int enable number reg */
#define AVIC_INTDISNUM		0x0C	/* int disable number reg */
#define AVIC_INTENABLEH		0x10	/* int enable reg high */
#define AVIC_INTENABLEL		0x14	/* int enable reg low */
#define AVIC_INTTYPEH		0x18	/* int type reg high */
#define AVIC_INTTYPEL		0x1C	/* int type reg low */
#define AVIC_NIPRIORITY(x)	(0x20 + 4 * (7 - (x))) /* int priority */
#define AVIC_NIVECSR		0x40	/* norm int vector/status */
#define AVIC_FIVECSR		0x44	/* fast int vector/status */
#define AVIC_INTSRCH		0x48	/* int source reg high */
#define AVIC_INTSRCL		0x4C	/* int source reg low */
#define AVIC_INTFRCH		0x50	/* int force reg high */
#define AVIC_INTFRCL		0x54	/* int force reg low */
#define AVIC_NIPNDH		0x58	/* norm int pending high */
#define AVIC_NIPNDL		0x5C	/* norm int pending low */
#define AVIC_FIPNDH		0x60	/* fast int pending high */
#define AVIC_FIPNDL		0x64	/* fast int pending low */

#define AVIC_NUM_IRQS 64

/* low power interrupt mask registers */
#define MX25_CCM_LPIMR0	0x68
#define MX25_CCM_LPIMR1	0x6C

static void __iomem *avic_base;
static void __iomem *mx25_ccm_base;
static struct irq_domain *domain;

#ifdef CONFIG_FIQ
static int avic_set_irq_fiq(unsigned int hwirq, unsigned int type)
{
	unsigned int irqt;

	if (hwirq >= AVIC_NUM_IRQS)
		return -EINVAL;

	if (hwirq < AVIC_NUM_IRQS / 2) {
		irqt = imx_readl(avic_base + AVIC_INTTYPEL) & ~(1 << hwirq);
		imx_writel(irqt | (!!type << hwirq), avic_base + AVIC_INTTYPEL);
	} else {
		hwirq -= AVIC_NUM_IRQS / 2;
		irqt = imx_readl(avic_base + AVIC_INTTYPEH) & ~(1 << hwirq);
		imx_writel(irqt | (!!type << hwirq), avic_base + AVIC_INTTYPEH);
	}

	return 0;
}
#endif /* CONFIG_FIQ */


static struct mxc_extra_irq avic_extra_irq = {
#ifdef CONFIG_FIQ
	.set_irq_fiq = avic_set_irq_fiq,
#endif
};

#ifdef CONFIG_PM
static u32 avic_saved_mask_reg[2];

static void avic_irq_suspend(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = gc->chip_types;
	int idx = d->hwirq >> 5;

	avic_saved_mask_reg[idx] = imx_readl(avic_base + ct->regs.mask);
	imx_writel(gc->wake_active, avic_base + ct->regs.mask);

	if (mx25_ccm_base) {
		u8 offs = d->hwirq < AVIC_NUM_IRQS / 2 ?
			MX25_CCM_LPIMR0 : MX25_CCM_LPIMR1;
		/*
		 * The interrupts which are still enabled will be used as wakeup
		 * sources. Allow those interrupts in low-power mode.
		 * The LPIMR registers use 0 to allow an interrupt, the AVIC
		 * registers use 1.
		 */
		imx_writel(~gc->wake_active, mx25_ccm_base + offs);
	}
}

static void avic_irq_resume(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct irq_chip_type *ct = gc->chip_types;
	int idx = d->hwirq >> 5;

	imx_writel(avic_saved_mask_reg[idx], avic_base + ct->regs.mask);

	if (mx25_ccm_base) {
		u8 offs = d->hwirq < AVIC_NUM_IRQS / 2 ?
			MX25_CCM_LPIMR0 : MX25_CCM_LPIMR1;

		imx_writel(0xffffffff, mx25_ccm_base + offs);
	}
}

#else
#define avic_irq_suspend NULL
#define avic_irq_resume NULL
#endif

static __init void avic_init_gc(int idx, unsigned int irq_start)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;

	gc = irq_alloc_generic_chip("mxc-avic", 1, irq_start, avic_base,
				    handle_level_irq);
	gc->private = &avic_extra_irq;
	gc->wake_enabled = IRQ_MSK(32);

	ct = gc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_ack = irq_gc_mask_clr_bit;
	ct->chip.irq_set_wake = irq_gc_set_wake;
	ct->chip.irq_suspend = avic_irq_suspend;
	ct->chip.irq_resume = avic_irq_resume;
	ct->regs.mask = !idx ? AVIC_INTENABLEL : AVIC_INTENABLEH;
	ct->regs.ack = ct->regs.mask;

	irq_setup_generic_chip(gc, IRQ_MSK(32), 0, IRQ_NOREQUEST, 0);
}

static void __exception_irq_entry avic_handle_irq(struct pt_regs *regs)
{
	u32 nivector;

	do {
		nivector = imx_readl(avic_base + AVIC_NIVECSR) >> 16;
		if (nivector == 0xffff)
			break;

		generic_handle_domain_irq(domain, nivector);
	} while (1);
}

/*
 * This function initializes the AVIC hardware and disables all the
 * interrupts. It registers the interrupt enable and disable functions
 * to the kernel for each interrupt source.
 */
static void __init mxc_init_irq(void __iomem *irqbase)
{
	struct device_node *np;
	int irq_base;
	int i;

	avic_base = irqbase;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx25-ccm");
	mx25_ccm_base = of_iomap(np, 0);

	if (mx25_ccm_base) {
		/*
		 * By default, we mask all interrupts. We set the actual mask
		 * before we go into low-power mode.
		 */
		imx_writel(0xffffffff, mx25_ccm_base + MX25_CCM_LPIMR0);
		imx_writel(0xffffffff, mx25_ccm_base + MX25_CCM_LPIMR1);
	}

	/* put the AVIC into the reset value with
	 * all interrupts disabled
	 */
	imx_writel(0, avic_base + AVIC_INTCNTL);
	imx_writel(0x1f, avic_base + AVIC_NIMASK);

	/* disable all interrupts */
	imx_writel(0, avic_base + AVIC_INTENABLEH);
	imx_writel(0, avic_base + AVIC_INTENABLEL);

	/* all IRQ no FIQ */
	imx_writel(0, avic_base + AVIC_INTTYPEH);
	imx_writel(0, avic_base + AVIC_INTTYPEL);

	irq_base = irq_alloc_descs(-1, 0, AVIC_NUM_IRQS, numa_node_id());
	WARN_ON(irq_base < 0);

	np = of_find_compatible_node(NULL, NULL, "fsl,avic");
	domain = irq_domain_add_legacy(np, AVIC_NUM_IRQS, irq_base, 0,
				       &irq_domain_simple_ops, NULL);
	WARN_ON(!domain);

	for (i = 0; i < AVIC_NUM_IRQS / 32; i++, irq_base += 32)
		avic_init_gc(i, irq_base);

	/* Set default priority value (0) for all IRQ's */
	for (i = 0; i < 8; i++)
		imx_writel(0, avic_base + AVIC_NIPRIORITY(i));

	set_handle_irq(avic_handle_irq);

#ifdef CONFIG_FIQ
	/* Initialize FIQ */
	init_FIQ(FIQ_START);
#endif

	printk(KERN_INFO "MXC IRQ initialized\n");
}

static int __init imx_avic_init(struct device_node *node,
			       struct device_node *parent)
{
	void __iomem *avic_base;

	avic_base = of_iomap(node, 0);
	BUG_ON(!avic_base);
	mxc_init_irq(avic_base);
	return 0;
}

IRQCHIP_DECLARE(imx_avic, "fsl,avic", imx_avic_init);
