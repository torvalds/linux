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
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <soc/fsl/qe/qe_ic.h>

#include "qe_ic.h"

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

static inline u32 qe_ic_read(volatile __be32  __iomem * base, unsigned int reg)
{
	return in_be32(base + (reg >> 2));
}

static inline void qe_ic_write(volatile __be32  __iomem * base, unsigned int reg,
			       u32 value)
{
	out_be32(base + (reg >> 2), value);
}

static inline struct qe_ic *qe_ic_from_irq(unsigned int virq)
{
	return irq_get_chip_data(virq);
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

/* Return an interrupt vector or NO_IRQ if no interrupt is pending. */
unsigned int qe_ic_get_low_irq(struct qe_ic *qe_ic)
{
	int irq;

	BUG_ON(qe_ic == NULL);

	/* get the interrupt source vector. */
	irq = qe_ic_read(qe_ic->regs, QEIC_CIVEC) >> 26;

	if (irq == 0)
		return NO_IRQ;

	return irq_linear_revmap(qe_ic->irqhost, irq);
}

/* Return an interrupt vector or NO_IRQ if no interrupt is pending. */
unsigned int qe_ic_get_high_irq(struct qe_ic *qe_ic)
{
	int irq;

	BUG_ON(qe_ic == NULL);

	/* get the interrupt source vector. */
	irq = qe_ic_read(qe_ic->regs, QEIC_CHIVEC) >> 26;

	if (irq == 0)
		return NO_IRQ;

	return irq_linear_revmap(qe_ic->irqhost, irq);
}

void __init qe_ic_init(struct device_node *node, unsigned int flags,
		       void (*low_handler)(struct irq_desc *desc),
		       void (*high_handler)(struct irq_desc *desc))
{
	struct qe_ic *qe_ic;
	struct resource res;
	u32 temp = 0, ret, high_active = 0;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return;

	qe_ic = kzalloc(sizeof(*qe_ic), GFP_KERNEL);
	if (qe_ic == NULL)
		return;

	qe_ic->irqhost = irq_domain_add_linear(node, NR_QE_IC_INTS,
					       &qe_ic_host_ops, qe_ic);
	if (qe_ic->irqhost == NULL) {
		kfree(qe_ic);
		return;
	}

	qe_ic->regs = ioremap(res.start, resource_size(&res));

	qe_ic->hc_irq = qe_ic_irq_chip;

	qe_ic->virq_high = irq_of_parse_and_map(node, 0);
	qe_ic->virq_low = irq_of_parse_and_map(node, 1);

	if (qe_ic->virq_low == NO_IRQ) {
		printk(KERN_ERR "Failed to map QE_IC low IRQ\n");
		kfree(qe_ic);
		return;
	}

	/* default priority scheme is grouped. If spread mode is    */
	/* required, configure cicr accordingly.                    */
	if (flags & QE_IC_SPREADMODE_GRP_W)
		temp |= CICR_GWCC;
	if (flags & QE_IC_SPREADMODE_GRP_X)
		temp |= CICR_GXCC;
	if (flags & QE_IC_SPREADMODE_GRP_Y)
		temp |= CICR_GYCC;
	if (flags & QE_IC_SPREADMODE_GRP_Z)
		temp |= CICR_GZCC;
	if (flags & QE_IC_SPREADMODE_GRP_RISCA)
		temp |= CICR_GRTA;
	if (flags & QE_IC_SPREADMODE_GRP_RISCB)
		temp |= CICR_GRTB;

	/* choose destination signal for highest priority interrupt */
	if (flags & QE_IC_HIGH_SIGNAL) {
		temp |= (SIGNAL_HIGH << CICR_HPIT_SHIFT);
		high_active = 1;
	}

	qe_ic_write(qe_ic->regs, QEIC_CICR, temp);

	irq_set_handler_data(qe_ic->virq_low, qe_ic);
	irq_set_chained_handler(qe_ic->virq_low, low_handler);

	if (qe_ic->virq_high != NO_IRQ &&
			qe_ic->virq_high != qe_ic->virq_low) {
		irq_set_handler_data(qe_ic->virq_high, qe_ic);
		irq_set_chained_handler(qe_ic->virq_high, high_handler);
	}
}

void qe_ic_set_highest_priority(unsigned int virq, int high)
{
	struct qe_ic *qe_ic = qe_ic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	u32 temp = 0;

	temp = qe_ic_read(qe_ic->regs, QEIC_CICR);

	temp &= ~CICR_HP_MASK;
	temp |= src << CICR_HP_SHIFT;

	temp &= ~CICR_HPIT_MASK;
	temp |= (high ? SIGNAL_HIGH : SIGNAL_LOW) << CICR_HPIT_SHIFT;

	qe_ic_write(qe_ic->regs, QEIC_CICR, temp);
}

/* Set Priority level within its group, from 1 to 8 */
int qe_ic_set_priority(unsigned int virq, unsigned int priority)
{
	struct qe_ic *qe_ic = qe_ic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	u32 temp;

	if (priority > 8 || priority == 0)
		return -EINVAL;
	if (WARN_ONCE(src >= ARRAY_SIZE(qe_ic_info),
		      "%s: Invalid hw irq number for QEIC\n", __func__))
		return -EINVAL;
	if (qe_ic_info[src].pri_reg == 0)
		return -EINVAL;

	temp = qe_ic_read(qe_ic->regs, qe_ic_info[src].pri_reg);

	if (priority < 4) {
		temp &= ~(0x7 << (32 - priority * 3));
		temp |= qe_ic_info[src].pri_code << (32 - priority * 3);
	} else {
		temp &= ~(0x7 << (24 - priority * 3));
		temp |= qe_ic_info[src].pri_code << (24 - priority * 3);
	}

	qe_ic_write(qe_ic->regs, qe_ic_info[src].pri_reg, temp);

	return 0;
}

/* Set a QE priority to use high irq, only priority 1~2 can use high irq */
int qe_ic_set_high_priority(unsigned int virq, unsigned int priority, int high)
{
	struct qe_ic *qe_ic = qe_ic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	u32 temp, control_reg = QEIC_CICNR, shift = 0;

	if (priority > 2 || priority == 0)
		return -EINVAL;
	if (WARN_ONCE(src >= ARRAY_SIZE(qe_ic_info),
		      "%s: Invalid hw irq number for QEIC\n", __func__))
		return -EINVAL;

	switch (qe_ic_info[src].pri_reg) {
	case QEIC_CIPZCC:
		shift = CICNR_ZCC1T_SHIFT;
		break;
	case QEIC_CIPWCC:
		shift = CICNR_WCC1T_SHIFT;
		break;
	case QEIC_CIPYCC:
		shift = CICNR_YCC1T_SHIFT;
		break;
	case QEIC_CIPXCC:
		shift = CICNR_XCC1T_SHIFT;
		break;
	case QEIC_CIPRTA:
		shift = CRICR_RTA1T_SHIFT;
		control_reg = QEIC_CRICR;
		break;
	case QEIC_CIPRTB:
		shift = CRICR_RTB1T_SHIFT;
		control_reg = QEIC_CRICR;
		break;
	default:
		return -EINVAL;
	}

	shift += (2 - priority) * 2;
	temp = qe_ic_read(qe_ic->regs, control_reg);
	temp &= ~(SIGNAL_MASK << shift);
	temp |= (high ? SIGNAL_HIGH : SIGNAL_LOW) << shift;
	qe_ic_write(qe_ic->regs, control_reg, temp);

	return 0;
}

static struct bus_type qe_ic_subsys = {
	.name = "qe_ic",
	.dev_name = "qe_ic",
};

static struct device device_qe_ic = {
	.id = 0,
	.bus = &qe_ic_subsys,
};

static int __init init_qe_ic_sysfs(void)
{
	int rc;

	printk(KERN_DEBUG "Registering qe_ic with sysfs...\n");

	rc = subsys_system_register(&qe_ic_subsys, NULL);
	if (rc) {
		printk(KERN_ERR "Failed registering qe_ic sys class\n");
		return -ENODEV;
	}
	rc = device_register(&device_qe_ic);
	if (rc) {
		printk(KERN_ERR "Failed registering qe_ic sys device\n");
		return -ENODEV;
	}
	return 0;
}

subsys_initcall(init_qe_ic_sysfs);
