/*
 * arch/powerpc/sysdev/qe_lib/qe_ic.c
 *
 * Copyright (C) 2006 Freescale Semicondutor, Inc.  All rights reserved.
 *
 * Author: Li Yang <leoli@freescale.com>
 * Based on code from Shlomi Gridish <gridish@freescale.com>
 *
 * QUICC ENGINE Interrupt Controller
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sysdev.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/spinlock.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/qe_ic.h>

#include "qe_ic.h"

static DEFINE_SPINLOCK(qe_ic_lock);

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
	return irq_to_desc(virq)->chip_data;
}

#define virq_to_hw(virq)	((unsigned int)irq_map[virq].hwirq)

static void qe_ic_unmask_irq(unsigned int virq)
{
	struct qe_ic *qe_ic = qe_ic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	unsigned long flags;
	u32 temp;

	spin_lock_irqsave(&qe_ic_lock, flags);

	temp = qe_ic_read(qe_ic->regs, qe_ic_info[src].mask_reg);
	qe_ic_write(qe_ic->regs, qe_ic_info[src].mask_reg,
		    temp | qe_ic_info[src].mask);

	spin_unlock_irqrestore(&qe_ic_lock, flags);
}

static void qe_ic_mask_irq(unsigned int virq)
{
	struct qe_ic *qe_ic = qe_ic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	unsigned long flags;
	u32 temp;

	spin_lock_irqsave(&qe_ic_lock, flags);

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

	spin_unlock_irqrestore(&qe_ic_lock, flags);
}

static struct irq_chip qe_ic_irq_chip = {
	.name = "QEIC",
	.unmask = qe_ic_unmask_irq,
	.mask = qe_ic_mask_irq,
	.mask_ack = qe_ic_mask_irq,
};

static int qe_ic_host_match(struct irq_host *h, struct device_node *node)
{
	/* Exact match, unless qe_ic node is NULL */
	return h->of_node == NULL || h->of_node == node;
}

static int qe_ic_host_map(struct irq_host *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	struct qe_ic *qe_ic = h->host_data;
	struct irq_chip *chip;

	if (qe_ic_info[hw].mask == 0) {
		printk(KERN_ERR "Can't map reserved IRQ\n");
		return -EINVAL;
	}
	/* Default chip */
	chip = &qe_ic->hc_irq;

	set_irq_chip_data(virq, qe_ic);
	irq_to_desc(virq)->status |= IRQ_LEVEL;

	set_irq_chip_and_handler(virq, chip, handle_level_irq);

	return 0;
}

static int qe_ic_host_xlate(struct irq_host *h, struct device_node *ct,
			    const u32 * intspec, unsigned int intsize,
			    irq_hw_number_t * out_hwirq,
			    unsigned int *out_flags)
{
	*out_hwirq = intspec[0];
	if (intsize > 1)
		*out_flags = intspec[1];
	else
		*out_flags = IRQ_TYPE_NONE;
	return 0;
}

static struct irq_host_ops qe_ic_host_ops = {
	.match = qe_ic_host_match,
	.map = qe_ic_host_map,
	.xlate = qe_ic_host_xlate,
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
		void (*low_handler)(unsigned int irq, struct irq_desc *desc),
		void (*high_handler)(unsigned int irq, struct irq_desc *desc))
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

	qe_ic->irqhost = irq_alloc_host(node, IRQ_HOST_MAP_LINEAR,
					NR_QE_IC_INTS, &qe_ic_host_ops, 0);
	if (qe_ic->irqhost == NULL) {
		kfree(qe_ic);
		return;
	}

	qe_ic->regs = ioremap(res.start, res.end - res.start + 1);

	qe_ic->irqhost->host_data = qe_ic;
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

	set_irq_data(qe_ic->virq_low, qe_ic);
	set_irq_chained_handler(qe_ic->virq_low, low_handler);

	if (qe_ic->virq_high != NO_IRQ &&
			qe_ic->virq_high != qe_ic->virq_low) {
		set_irq_data(qe_ic->virq_high, qe_ic);
		set_irq_chained_handler(qe_ic->virq_high, high_handler);
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
	if (src > 127)
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

static struct sysdev_class qe_ic_sysclass = {
	.name = "qe_ic",
};

static struct sys_device device_qe_ic = {
	.id = 0,
	.cls = &qe_ic_sysclass,
};

static int __init init_qe_ic_sysfs(void)
{
	int rc;

	printk(KERN_DEBUG "Registering qe_ic with sysfs...\n");

	rc = sysdev_class_register(&qe_ic_sysclass);
	if (rc) {
		printk(KERN_ERR "Failed registering qe_ic sys class\n");
		return -ENODEV;
	}
	rc = sysdev_register(&device_qe_ic);
	if (rc) {
		printk(KERN_ERR "Failed registering qe_ic sys device\n");
		return -ENODEV;
	}
	return 0;
}

subsys_initcall(init_qe_ic_sysfs);
