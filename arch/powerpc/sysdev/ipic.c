/*
 * arch/powerpc/sysdev/ipic.c
 *
 * IPIC routines implementations.
 *
 * Copyright 2005 Freescale Semiconductor, Inc.
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
#include <linux/syscore_ops.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/spinlock.h>
#include <linux/fsl_devices.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/ipic.h>

#include "ipic.h"

static struct ipic * primary_ipic;
static struct irq_chip ipic_level_irq_chip, ipic_edge_irq_chip;
static DEFINE_RAW_SPINLOCK(ipic_lock);

static struct ipic_info ipic_info[] = {
	[1] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 16,
		.prio_mask = 0,
	},
	[2] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 17,
		.prio_mask = 1,
	},
	[3] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 18,
		.prio_mask = 2,
	},
	[4] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 19,
		.prio_mask = 3,
	},
	[5] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 20,
		.prio_mask = 4,
	},
	[6] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 21,
		.prio_mask = 5,
	},
	[7] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 22,
		.prio_mask = 6,
	},
	[8] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_C,
		.force	= IPIC_SIFCR_H,
		.bit	= 23,
		.prio_mask = 7,
	},
	[9] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 24,
		.prio_mask = 0,
	},
	[10] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 25,
		.prio_mask = 1,
	},
	[11] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 26,
		.prio_mask = 2,
	},
	[12] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 27,
		.prio_mask = 3,
	},
	[13] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 28,
		.prio_mask = 4,
	},
	[14] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 29,
		.prio_mask = 5,
	},
	[15] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 30,
		.prio_mask = 6,
	},
	[16] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 31,
		.prio_mask = 7,
	},
	[17] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 1,
		.prio_mask = 5,
	},
	[18] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 2,
		.prio_mask = 6,
	},
	[19] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 3,
		.prio_mask = 7,
	},
	[20] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 4,
		.prio_mask = 4,
	},
	[21] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 5,
		.prio_mask = 5,
	},
	[22] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 6,
		.prio_mask = 6,
	},
	[23] = {
		.ack	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 7,
		.prio_mask = 7,
	},
	[32] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 0,
		.prio_mask = 0,
	},
	[33] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 1,
		.prio_mask = 1,
	},
	[34] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 2,
		.prio_mask = 2,
	},
	[35] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 3,
		.prio_mask = 3,
	},
	[36] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 4,
		.prio_mask = 4,
	},
	[37] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 5,
		.prio_mask = 5,
	},
	[38] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 6,
		.prio_mask = 6,
	},
	[39] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 7,
		.prio_mask = 7,
	},
	[40] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 8,
		.prio_mask = 0,
	},
	[41] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 9,
		.prio_mask = 1,
	},
	[42] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 10,
		.prio_mask = 2,
	},
	[43] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 11,
		.prio_mask = 3,
	},
	[44] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 12,
		.prio_mask = 4,
	},
	[45] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 13,
		.prio_mask = 5,
	},
	[46] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 14,
		.prio_mask = 6,
	},
	[47] = {
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_B,
		.force	= IPIC_SIFCR_H,
		.bit	= 15,
		.prio_mask = 7,
	},
	[48] = {
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 0,
		.prio_mask = 4,
	},
	[64] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 0,
		.prio_mask = 0,
	},
	[65] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 1,
		.prio_mask = 1,
	},
	[66] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 2,
		.prio_mask = 2,
	},
	[67] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 3,
		.prio_mask = 3,
	},
	[68] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 4,
		.prio_mask = 0,
	},
	[69] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 5,
		.prio_mask = 1,
	},
	[70] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 6,
		.prio_mask = 2,
	},
	[71] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 7,
		.prio_mask = 3,
	},
	[72] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 8,
	},
	[73] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 9,
	},
	[74] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 10,
	},
	[75] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 11,
	},
	[76] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 12,
	},
	[77] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 13,
	},
	[78] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 14,
	},
	[79] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 15,
	},
	[80] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 16,
	},
	[81] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 17,
	},
	[82] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 18,
	},
	[83] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 19,
	},
	[84] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 20,
	},
	[85] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 21,
	},
	[86] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 22,
	},
	[87] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 23,
	},
	[88] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 24,
	},
	[89] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 25,
	},
	[90] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 26,
	},
	[91] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 27,
	},
	[94] = {
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 30,
	},
};

static inline u32 ipic_read(volatile u32 __iomem *base, unsigned int reg)
{
	return in_be32(base + (reg >> 2));
}

static inline void ipic_write(volatile u32 __iomem *base, unsigned int reg, u32 value)
{
	out_be32(base + (reg >> 2), value);
}

static inline struct ipic * ipic_from_irq(unsigned int virq)
{
	return primary_ipic;
}

static void ipic_unmask_irq(struct irq_data *d)
{
	struct ipic *ipic = ipic_from_irq(d->irq);
	unsigned int src = irqd_to_hwirq(d);
	unsigned long flags;
	u32 temp;

	raw_spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp |= (1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);

	raw_spin_unlock_irqrestore(&ipic_lock, flags);
}

static void ipic_mask_irq(struct irq_data *d)
{
	struct ipic *ipic = ipic_from_irq(d->irq);
	unsigned int src = irqd_to_hwirq(d);
	unsigned long flags;
	u32 temp;

	raw_spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp &= ~(1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);

	/* mb() can't guarantee that masking is finished.  But it does finish
	 * for nearly all cases. */
	mb();

	raw_spin_unlock_irqrestore(&ipic_lock, flags);
}

static void ipic_ack_irq(struct irq_data *d)
{
	struct ipic *ipic = ipic_from_irq(d->irq);
	unsigned int src = irqd_to_hwirq(d);
	unsigned long flags;
	u32 temp;

	raw_spin_lock_irqsave(&ipic_lock, flags);

	temp = 1 << (31 - ipic_info[src].bit);
	ipic_write(ipic->regs, ipic_info[src].ack, temp);

	/* mb() can't guarantee that ack is finished.  But it does finish
	 * for nearly all cases. */
	mb();

	raw_spin_unlock_irqrestore(&ipic_lock, flags);
}

static void ipic_mask_irq_and_ack(struct irq_data *d)
{
	struct ipic *ipic = ipic_from_irq(d->irq);
	unsigned int src = irqd_to_hwirq(d);
	unsigned long flags;
	u32 temp;

	raw_spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp &= ~(1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);

	temp = 1 << (31 - ipic_info[src].bit);
	ipic_write(ipic->regs, ipic_info[src].ack, temp);

	/* mb() can't guarantee that ack is finished.  But it does finish
	 * for nearly all cases. */
	mb();

	raw_spin_unlock_irqrestore(&ipic_lock, flags);
}

static int ipic_set_irq_type(struct irq_data *d, unsigned int flow_type)
{
	struct ipic *ipic = ipic_from_irq(d->irq);
	unsigned int src = irqd_to_hwirq(d);
	unsigned int vold, vnew, edibit;

	if (flow_type == IRQ_TYPE_NONE)
		flow_type = IRQ_TYPE_LEVEL_LOW;

	/* ipic supports only low assertion and high-to-low change senses
	 */
	if (!(flow_type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING))) {
		printk(KERN_ERR "ipic: sense type 0x%x not supported\n",
			flow_type);
		return -EINVAL;
	}
	/* ipic supports only edge mode on external interrupts */
	if ((flow_type & IRQ_TYPE_EDGE_FALLING) && !ipic_info[src].ack) {
		printk(KERN_ERR "ipic: edge sense not supported on internal "
				"interrupts\n");
		return -EINVAL;

	}

	irqd_set_trigger_type(d, flow_type);
	if (flow_type & IRQ_TYPE_LEVEL_LOW)  {
		__irq_set_handler_locked(d->irq, handle_level_irq);
		d->chip = &ipic_level_irq_chip;
	} else {
		__irq_set_handler_locked(d->irq, handle_edge_irq);
		d->chip = &ipic_edge_irq_chip;
	}

	/* only EXT IRQ senses are programmable on ipic
	 * internal IRQ senses are LEVEL_LOW
	 */
	if (src == IPIC_IRQ_EXT0)
		edibit = 15;
	else
		if (src >= IPIC_IRQ_EXT1 && src <= IPIC_IRQ_EXT7)
			edibit = (14 - (src - IPIC_IRQ_EXT1));
		else
			return (flow_type & IRQ_TYPE_LEVEL_LOW) ? 0 : -EINVAL;

	vold = ipic_read(ipic->regs, IPIC_SECNR);
	if ((flow_type & IRQ_TYPE_SENSE_MASK) == IRQ_TYPE_EDGE_FALLING) {
		vnew = vold | (1 << edibit);
	} else {
		vnew = vold & ~(1 << edibit);
	}
	if (vold != vnew)
		ipic_write(ipic->regs, IPIC_SECNR, vnew);
	return IRQ_SET_MASK_OK_NOCOPY;
}

/* level interrupts and edge interrupts have different ack operations */
static struct irq_chip ipic_level_irq_chip = {
	.name		= "IPIC",
	.irq_unmask	= ipic_unmask_irq,
	.irq_mask	= ipic_mask_irq,
	.irq_mask_ack	= ipic_mask_irq,
	.irq_set_type	= ipic_set_irq_type,
};

static struct irq_chip ipic_edge_irq_chip = {
	.name		= "IPIC",
	.irq_unmask	= ipic_unmask_irq,
	.irq_mask	= ipic_mask_irq,
	.irq_mask_ack	= ipic_mask_irq_and_ack,
	.irq_ack	= ipic_ack_irq,
	.irq_set_type	= ipic_set_irq_type,
};

static int ipic_host_match(struct irq_host *h, struct device_node *node)
{
	/* Exact match, unless ipic node is NULL */
	return h->of_node == NULL || h->of_node == node;
}

static int ipic_host_map(struct irq_host *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct ipic *ipic = h->host_data;

	irq_set_chip_data(virq, ipic);
	irq_set_chip_and_handler(virq, &ipic_level_irq_chip, handle_level_irq);

	/* Set default irq type */
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int ipic_host_xlate(struct irq_host *h, struct device_node *ct,
			   const u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	/* interrupt sense values coming from the device tree equal either
	 * LEVEL_LOW (low assertion) or EDGE_FALLING (high-to-low change)
	 */
	*out_hwirq = intspec[0];
	if (intsize > 1)
		*out_flags = intspec[1];
	else
		*out_flags = IRQ_TYPE_NONE;
	return 0;
}

static struct irq_host_ops ipic_host_ops = {
	.match	= ipic_host_match,
	.map	= ipic_host_map,
	.xlate	= ipic_host_xlate,
};

struct ipic * __init ipic_init(struct device_node *node, unsigned int flags)
{
	struct ipic	*ipic;
	struct resource res;
	u32 temp = 0, ret;

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return NULL;

	ipic = kzalloc(sizeof(*ipic), GFP_KERNEL);
	if (ipic == NULL)
		return NULL;

	ipic->irqhost = irq_alloc_host(node, IRQ_HOST_MAP_LINEAR,
				       NR_IPIC_INTS,
				       &ipic_host_ops, 0);
	if (ipic->irqhost == NULL) {
		kfree(ipic);
		return NULL;
	}

	ipic->regs = ioremap(res.start, resource_size(&res));

	ipic->irqhost->host_data = ipic;

	/* init hw */
	ipic_write(ipic->regs, IPIC_SICNR, 0x0);

	/* default priority scheme is grouped. If spread mode is required
	 * configure SICFR accordingly */
	if (flags & IPIC_SPREADMODE_GRP_A)
		temp |= SICFR_IPSA;
	if (flags & IPIC_SPREADMODE_GRP_B)
		temp |= SICFR_IPSB;
	if (flags & IPIC_SPREADMODE_GRP_C)
		temp |= SICFR_IPSC;
	if (flags & IPIC_SPREADMODE_GRP_D)
		temp |= SICFR_IPSD;
	if (flags & IPIC_SPREADMODE_MIX_A)
		temp |= SICFR_MPSA;
	if (flags & IPIC_SPREADMODE_MIX_B)
		temp |= SICFR_MPSB;

	ipic_write(ipic->regs, IPIC_SICFR, temp);

	/* handle MCP route */
	temp = 0;
	if (flags & IPIC_DISABLE_MCP_OUT)
		temp = SERCR_MCPR;
	ipic_write(ipic->regs, IPIC_SERCR, temp);

	/* handle routing of IRQ0 to MCP */
	temp = ipic_read(ipic->regs, IPIC_SEMSR);

	if (flags & IPIC_IRQ0_MCP)
		temp |= SEMSR_SIRQ0;
	else
		temp &= ~SEMSR_SIRQ0;

	ipic_write(ipic->regs, IPIC_SEMSR, temp);

	primary_ipic = ipic;
	irq_set_default_host(primary_ipic->irqhost);

	ipic_write(ipic->regs, IPIC_SIMSR_H, 0);
	ipic_write(ipic->regs, IPIC_SIMSR_L, 0);

	printk ("IPIC (%d IRQ sources) at %p\n", NR_IPIC_INTS,
			primary_ipic->regs);

	return ipic;
}

int ipic_set_priority(unsigned int virq, unsigned int priority)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	u32 temp;

	if (priority > 7)
		return -EINVAL;
	if (src > 127)
		return -EINVAL;
	if (ipic_info[src].prio == 0)
		return -EINVAL;

	temp = ipic_read(ipic->regs, ipic_info[src].prio);

	if (priority < 4) {
		temp &= ~(0x7 << (20 + (3 - priority) * 3));
		temp |= ipic_info[src].prio_mask << (20 + (3 - priority) * 3);
	} else {
		temp &= ~(0x7 << (4 + (7 - priority) * 3));
		temp |= ipic_info[src].prio_mask << (4 + (7 - priority) * 3);
	}

	ipic_write(ipic->regs, ipic_info[src].prio, temp);

	return 0;
}

void ipic_set_highest_priority(unsigned int virq)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = virq_to_hw(virq);
	u32 temp;

	temp = ipic_read(ipic->regs, IPIC_SICFR);

	/* clear and set HPI */
	temp &= 0x7f000000;
	temp |= (src & 0x7f) << 24;

	ipic_write(ipic->regs, IPIC_SICFR, temp);
}

void ipic_set_default_priority(void)
{
	ipic_write(primary_ipic->regs, IPIC_SIPRR_A, IPIC_PRIORITY_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SIPRR_B, IPIC_PRIORITY_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SIPRR_C, IPIC_PRIORITY_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SIPRR_D, IPIC_PRIORITY_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SMPRR_A, IPIC_PRIORITY_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SMPRR_B, IPIC_PRIORITY_DEFAULT);
}

void ipic_enable_mcp(enum ipic_mcp_irq mcp_irq)
{
	struct ipic *ipic = primary_ipic;
	u32 temp;

	temp = ipic_read(ipic->regs, IPIC_SERMR);
	temp |= (1 << (31 - mcp_irq));
	ipic_write(ipic->regs, IPIC_SERMR, temp);
}

void ipic_disable_mcp(enum ipic_mcp_irq mcp_irq)
{
	struct ipic *ipic = primary_ipic;
	u32 temp;

	temp = ipic_read(ipic->regs, IPIC_SERMR);
	temp &= (1 << (31 - mcp_irq));
	ipic_write(ipic->regs, IPIC_SERMR, temp);
}

u32 ipic_get_mcp_status(void)
{
	return ipic_read(primary_ipic->regs, IPIC_SERMR);
}

void ipic_clear_mcp_status(u32 mask)
{
	ipic_write(primary_ipic->regs, IPIC_SERMR, mask);
}

/* Return an interrupt vector or NO_IRQ if no interrupt is pending. */
unsigned int ipic_get_irq(void)
{
	int irq;

	BUG_ON(primary_ipic == NULL);

#define IPIC_SIVCR_VECTOR_MASK	0x7f
	irq = ipic_read(primary_ipic->regs, IPIC_SIVCR) & IPIC_SIVCR_VECTOR_MASK;

	if (irq == 0)    /* 0 --> no irq is pending */
		return NO_IRQ;

	return irq_linear_revmap(primary_ipic->irqhost, irq);
}

#ifdef CONFIG_SUSPEND
static struct {
	u32 sicfr;
	u32 siprr[2];
	u32 simsr[2];
	u32 sicnr;
	u32 smprr[2];
	u32 semsr;
	u32 secnr;
	u32 sermr;
	u32 sercr;
} ipic_saved_state;

static int ipic_suspend(void)
{
	struct ipic *ipic = primary_ipic;

	ipic_saved_state.sicfr = ipic_read(ipic->regs, IPIC_SICFR);
	ipic_saved_state.siprr[0] = ipic_read(ipic->regs, IPIC_SIPRR_A);
	ipic_saved_state.siprr[1] = ipic_read(ipic->regs, IPIC_SIPRR_D);
	ipic_saved_state.simsr[0] = ipic_read(ipic->regs, IPIC_SIMSR_H);
	ipic_saved_state.simsr[1] = ipic_read(ipic->regs, IPIC_SIMSR_L);
	ipic_saved_state.sicnr = ipic_read(ipic->regs, IPIC_SICNR);
	ipic_saved_state.smprr[0] = ipic_read(ipic->regs, IPIC_SMPRR_A);
	ipic_saved_state.smprr[1] = ipic_read(ipic->regs, IPIC_SMPRR_B);
	ipic_saved_state.semsr = ipic_read(ipic->regs, IPIC_SEMSR);
	ipic_saved_state.secnr = ipic_read(ipic->regs, IPIC_SECNR);
	ipic_saved_state.sermr = ipic_read(ipic->regs, IPIC_SERMR);
	ipic_saved_state.sercr = ipic_read(ipic->regs, IPIC_SERCR);

	if (fsl_deep_sleep()) {
		/* In deep sleep, make sure there can be no
		 * pending interrupts, as this can cause
		 * problems on 831x.
		 */
		ipic_write(ipic->regs, IPIC_SIMSR_H, 0);
		ipic_write(ipic->regs, IPIC_SIMSR_L, 0);
		ipic_write(ipic->regs, IPIC_SEMSR, 0);
		ipic_write(ipic->regs, IPIC_SERMR, 0);
	}

	return 0;
}

static void ipic_resume(void)
{
	struct ipic *ipic = primary_ipic;

	ipic_write(ipic->regs, IPIC_SICFR, ipic_saved_state.sicfr);
	ipic_write(ipic->regs, IPIC_SIPRR_A, ipic_saved_state.siprr[0]);
	ipic_write(ipic->regs, IPIC_SIPRR_D, ipic_saved_state.siprr[1]);
	ipic_write(ipic->regs, IPIC_SIMSR_H, ipic_saved_state.simsr[0]);
	ipic_write(ipic->regs, IPIC_SIMSR_L, ipic_saved_state.simsr[1]);
	ipic_write(ipic->regs, IPIC_SICNR, ipic_saved_state.sicnr);
	ipic_write(ipic->regs, IPIC_SMPRR_A, ipic_saved_state.smprr[0]);
	ipic_write(ipic->regs, IPIC_SMPRR_B, ipic_saved_state.smprr[1]);
	ipic_write(ipic->regs, IPIC_SEMSR, ipic_saved_state.semsr);
	ipic_write(ipic->regs, IPIC_SECNR, ipic_saved_state.secnr);
	ipic_write(ipic->regs, IPIC_SERMR, ipic_saved_state.sermr);
	ipic_write(ipic->regs, IPIC_SERCR, ipic_saved_state.sercr);
}
#else
#define ipic_suspend NULL
#define ipic_resume NULL
#endif

static struct syscore_ops ipic_syscore_ops = {
	.suspend = ipic_suspend,
	.resume = ipic_resume,
};

static int __init init_ipic_syscore(void)
{
	if (!primary_ipic || !primary_ipic->regs)
		return -ENODEV;

	printk(KERN_DEBUG "Registering ipic system core operations\n");
	register_syscore_ops(&ipic_syscore_ops);

	return 0;
}

subsys_initcall(init_ipic_syscore);
