// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/sysdev/qe_lib/qe_ic.c
 *
 * Copyright (C) 2006 Freescale Semiconductor, Inc.  All rights reserved.
 *
 * Author: Li Yang <leoli@freescale.com>
 * Based on code from Shlomi Gridish <gridish@freescale.com>
 *
 * QUICC ENGINE Interrupt Controller
 */

#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <soc/fsl/qe/qe.h>

#define NR_QE_IC_INTS		64

/* QE IC registers offset */
#define QEIC_CICR		0x00
#define QEIC_CIVEC		0x04
#define QEIC_CIPXCC		0x10
#define QEIC_CIPYCC		0x14
#define QEIC_CIPWCC		0x18
#define QEIC_CIPZCC		0x1c
#define QEIC_CIMR		0x20
#define QEIC_CRIMR		0x24
#define QEIC_CIPRTA		0x30
#define QEIC_CIPRTB		0x34
#define QEIC_CHIVEC		0x60

struct qe_ic {
	/* Control registers offset */
	__be32 __iomem *regs;

	/* The remapper for this QEIC */
	struct irq_domain *irqhost;

	/* The "linux" controller struct */
	struct irq_chip hc_irq;

	/* VIRQ numbers of QE high/low irqs */
	int virq_high;
	int virq_low;
};

/*
 * QE interrupt controller internal structure
 */
struct qe_ic_info {
	/* Location of this source at the QIMR register */
	u32	mask;

	/* Mask register offset */
	u32	mask_reg;

	/*
	 * For grouped interrupts sources - the interrupt code as
	 * appears at the group priority register
	 */
	u8	pri_code;

	/* Group priority register offset */
	u32	pri_reg;
};

static DEFINE_RAW_SPINLOCK(qe_ic_lock);

static struct qe_ic_info qe_ic_info[] = {
	[1] = {
	       .mask = 0x00008000,
	       .mask_reg = QEIC_CIMR,
	       .pri_code = 0,
	       .pri_reg = QEIC_CIPWCC,
	       },
	[2] = {
	       .mask = 0x00004000,
	       .mask_reg = QEIC_CIMR,
	       .pri_code = 1,
	       .pri_reg = QEIC_CIPWCC,
	       },
	[3] = {
	       .mask = 0x00002000,
	       .mask_reg = QEIC_CIMR,
	       .pri_code = 2,
	       .pri_reg = QEIC_CIPWCC,
	       },
	[10] = {
		.mask = 0x00000040,
		.mask_reg = QEIC_CIMR,
		.pri_code = 1,
		.pri_reg = QEIC_CIPZCC,
		},
	[11] = {
		.mask = 0x00000020,
		.mask_reg = QEIC_CIMR,
		.pri_code = 2,
		.pri_reg = QEIC_CIPZCC,
		},
	[12] = {
		.mask = 0x00000010,
		.mask_reg = QEIC_CIMR,
		.pri_code = 3,
		.pri_reg = QEIC_CIPZCC,
		},
	[13] = {
		.mask = 0x00000008,
		.mask_reg = QEIC_CIMR,
		.pri_code = 4,
		.pri_reg = QEIC_CIPZCC,
		},
	[14] = {
		.mask = 0x00000004,
		.mask_reg = QEIC_CIMR,
		.pri_code = 5,
		.pri_reg = QEIC_CIPZCC,
		},
	[15] = {
		.mask = 0x00000002,
		.mask_reg = QEIC_CIMR,
		.pri_code = 6,
		.pri_reg = QEIC_CIPZCC,
		},
	[20] = {
		.mask = 0x10000000,
		.mask_reg = QEIC_CRIMR,
		.pri_code = 3,
		.pri_reg = QEIC_CIPRTA,
		},
	[25] = {
		.mask = 0x00800000,
		.mask_reg = QEIC_CRIMR,
		.pri_code = 0,
		.pri_reg = QEIC_CIPRTB,
		},
	[26] = {
		.mask = 0x00400000,
		.mask_reg = QEIC_CRIMR,
		.pri_code = 1,
		.pri_reg = QEIC_CIPRTB,
		},
	[27] = {
		.mask = 0x00200000,
		.mask_reg = QEIC_CRIMR,
		.pri_code = 2,
		.pri_reg = QEIC_CIPRTB,
		},
	[28] = {
		.mask = 0x00100000,
		.mask_reg = QEIC_CRIMR,
		.pri_code = 3,
		.pri_reg = QEIC_CIPRTB,
		},
	[32] = {
		.mask = 0x80000000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 0,
		.pri_reg = QEIC_CIPXCC,
		},
	[33] = {
		.mask = 0x40000000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 1,
		.pri_reg = QEIC_CIPXCC,
		},
	[34] = {
		.mask = 0x20000000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 2,
		.pri_reg = QEIC_CIPXCC,
		},
	[35] = {
		.mask = 0x10000000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 3,
		.pri_reg = QEIC_CIPXCC,
		},
	[36] = {
		.mask = 0x08000000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 4,
		.pri_reg = QEIC_CIPXCC,
		},
	[40] = {
		.mask = 0x00800000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 0,
		.pri_reg = QEIC_CIPYCC,
		},
	[41] = {
		.mask = 0x00400000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 1,
		.pri_reg = QEIC_CIPYCC,
		},
	[42] = {
		.mask = 0x00200000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 2,
		.pri_reg = QEIC_CIPYCC,
		},
	[43] = {
		.mask = 0x00100000,
		.mask_reg = QEIC_CIMR,
		.pri_code = 3,
		.pri_reg = QEIC_CIPYCC,
		},
};

static inline u32 qe_ic_read(__be32  __iomem *base, unsigned int reg)
{
	return ioread32be(base + (reg >> 2));
}

static inline void qe_ic_write(__be32  __iomem *base, unsigned int reg,
			       u32 value)
{
	iowrite32be(value, base + (reg >> 2));
}

static inline struct qe_ic *qe_ic_from_irq_data(struct irq_data *d)
{
	return irq_data_get_irq_chip_data(d);
}

static void qe_ic_unmask_irq(struct irq_data *d)
{
	struct qe_ic *qe_ic = qe_ic_from_irq_data(d);
	unsigned int src = irqd_to_hwirq(d);
	unsigned long flags;
	u32 temp;

	raw_spin_lock_irqsave(&qe_ic_lock, flags);

	temp = qe_ic_read(qe_ic->regs, qe_ic_info[src].mask_reg);
	qe_ic_write(qe_ic->regs, qe_ic_info[src].mask_reg,
		    temp | qe_ic_info[src].mask);

	raw_spin_unlock_irqrestore(&qe_ic_lock, flags);
}

static void qe_ic_mask_irq(struct irq_data *d)
{
	struct qe_ic *qe_ic = qe_ic_from_irq_data(d);
	unsigned int src = irqd_to_hwirq(d);
	unsigned long flags;
	u32 temp;

	raw_spin_lock_irqsave(&qe_ic_lock, flags);

	temp = qe_ic_read(qe_ic->regs, qe_ic_info[src].mask_reg);
	qe_ic_write(qe_ic->regs, qe_ic_info[src].mask_reg,
		    temp & ~qe_ic_info[src].mask);

	/* Flush the above write before enabling interrupts; otherwise,
	 * spurious interrupts will sometimes happen.  To be 100% sure
	 * that the write has reached the device before interrupts are
	 * enabled, the mask register would have to be read back; however,
	 * this is not required for correctness, only to avoid wasting
	 * time on a large number of spurious interrupts.  In testing,
	 * a sync reduced the observed spurious interrupts to zero.
	 */
	mb();

	raw_spin_unlock_irqrestore(&qe_ic_lock, flags);
}

static struct irq_chip qe_ic_irq_chip = {
	.name = "QEIC",
	.irq_unmask = qe_ic_unmask_irq,
	.irq_mask = qe_ic_mask_irq,
	.irq_mask_ack = qe_ic_mask_irq,
};

static int qe_ic_host_match(struct irq_domain *h, struct device_node *node,
			    enum irq_domain_bus_token bus_token)
{
	/* Exact match, unless qe_ic node is NULL */
	struct device_node *of_node = irq_domain_get_of_node(h);
	return of_node == NULL || of_node == node;
}

static int qe_ic_host_map(struct irq_domain *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	struct qe_ic *qe_ic = h->host_data;
	struct irq_chip *chip;

	if (hw >= ARRAY_SIZE(qe_ic_info)) {
		pr_err("%s: Invalid hw irq number for QEIC\n", __func__);
		return -EINVAL;
	}

	if (qe_ic_info[hw].mask == 0) {
		printk(KERN_ERR "Can't map reserved IRQ\n");
		return -EINVAL;
	}
	/* Default chip */
	chip = &qe_ic->hc_irq;

	irq_set_chip_data(virq, qe_ic);
	irq_set_status_flags(virq, IRQ_LEVEL);

	irq_set_chip_and_handler(virq, chip, handle_level_irq);

	return 0;
}

static const struct irq_domain_ops qe_ic_host_ops = {
	.match = qe_ic_host_match,
	.map = qe_ic_host_map,
	.xlate = irq_domain_xlate_onetwocell,
};

/* Return an interrupt vector or 0 if no interrupt is pending. */
static unsigned int qe_ic_get_low_irq(struct qe_ic *qe_ic)
{
	int irq;

	BUG_ON(qe_ic == NULL);

	/* get the interrupt source vector. */
	irq = qe_ic_read(qe_ic->regs, QEIC_CIVEC) >> 26;

	if (irq == 0)
		return 0;

	return irq_find_mapping(qe_ic->irqhost, irq);
}

/* Return an interrupt vector or 0 if no interrupt is pending. */
static unsigned int qe_ic_get_high_irq(struct qe_ic *qe_ic)
{
	int irq;

	BUG_ON(qe_ic == NULL);

	/* get the interrupt source vector. */
	irq = qe_ic_read(qe_ic->regs, QEIC_CHIVEC) >> 26;

	if (irq == 0)
		return 0;

	return irq_find_mapping(qe_ic->irqhost, irq);
}

static void qe_ic_cascade_low(struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq = qe_ic_get_low_irq(qe_ic);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (cascade_irq != 0)
		generic_handle_irq(cascade_irq);

	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

static void qe_ic_cascade_high(struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq = qe_ic_get_high_irq(qe_ic);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	if (cascade_irq != 0)
		generic_handle_irq(cascade_irq);

	if (chip->irq_eoi)
		chip->irq_eoi(&desc->irq_data);
}

static void qe_ic_cascade_muxed_mpic(struct irq_desc *desc)
{
	struct qe_ic *qe_ic = irq_desc_get_handler_data(desc);
	unsigned int cascade_irq;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	cascade_irq = qe_ic_get_high_irq(qe_ic);
	if (cascade_irq == 0)
		cascade_irq = qe_ic_get_low_irq(qe_ic);

	if (cascade_irq != 0)
		generic_handle_irq(cascade_irq);

	chip->irq_eoi(&desc->irq_data);
}

static int qe_ic_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void (*low_handler)(struct irq_desc *desc);
	void (*high_handler)(struct irq_desc *desc);
	struct qe_ic *qe_ic;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(dev, "no memory resource defined\n");
		return -ENODEV;
	}

	qe_ic = devm_kzalloc(dev, sizeof(*qe_ic), GFP_KERNEL);
	if (qe_ic == NULL)
		return -ENOMEM;

	qe_ic->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (qe_ic->regs == NULL) {
		dev_err(dev, "failed to ioremap() registers\n");
		return -ENODEV;
	}

	qe_ic->hc_irq = qe_ic_irq_chip;

	qe_ic->virq_high = platform_get_irq(pdev, 0);
	qe_ic->virq_low = platform_get_irq(pdev, 1);

	if (qe_ic->virq_low <= 0)
		return -ENODEV;

	if (qe_ic->virq_high > 0 && qe_ic->virq_high != qe_ic->virq_low) {
		low_handler = qe_ic_cascade_low;
		high_handler = qe_ic_cascade_high;
	} else {
		low_handler = qe_ic_cascade_muxed_mpic;
		high_handler = NULL;
	}

	qe_ic->irqhost = irq_domain_create_linear(dev_fwnode(&pdev->dev), NR_QE_IC_INTS,
						  &qe_ic_host_ops, qe_ic);
	if (qe_ic->irqhost == NULL) {
		dev_err(dev, "failed to add irq domain\n");
		return -ENODEV;
	}

	qe_ic_write(qe_ic->regs, QEIC_CICR, 0);

	irq_set_chained_handler_and_data(qe_ic->virq_low, low_handler, qe_ic);

	if (high_handler)
		irq_set_chained_handler_and_data(qe_ic->virq_high,
						 high_handler, qe_ic);
	return 0;
}
static const struct of_device_id qe_ic_ids[] = {
	{ .compatible = "fsl,qe-ic"},
	{ .type = "qeic"},
	{},
};

static struct platform_driver qe_ic_driver =
{
	.driver	= {
		.name		= "qe-ic",
		.of_match_table	= qe_ic_ids,
	},
	.probe	= qe_ic_init,
};

static int __init qe_ic_of_init(void)
{
	platform_driver_register(&qe_ic_driver);
	return 0;
}
subsys_initcall(qe_ic_of_init);
