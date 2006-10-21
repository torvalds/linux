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
#include <linux/sysdev.h>
#include <linux/device.h>
#include <linux/bootmem.h>
#include <linux/spinlock.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/ipic.h>

#include "ipic.h"

static struct ipic * primary_ipic;
static DEFINE_SPINLOCK(ipic_lock);

static struct ipic_info ipic_info[] = {
	[9] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 24,
		.prio_mask = 0,
	},
	[10] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 25,
		.prio_mask = 1,
	},
	[11] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 26,
		.prio_mask = 2,
	},
	[14] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 29,
		.prio_mask = 5,
	},
	[15] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 30,
		.prio_mask = 6,
	},
	[16] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_D,
		.force	= IPIC_SIFCR_H,
		.bit	= 31,
		.prio_mask = 7,
	},
	[17] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 1,
		.prio_mask = 5,
	},
	[18] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 2,
		.prio_mask = 6,
	},
	[19] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 3,
		.prio_mask = 7,
	},
	[20] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 4,
		.prio_mask = 4,
	},
	[21] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 5,
		.prio_mask = 5,
	},
	[22] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 6,
		.prio_mask = 6,
	},
	[23] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SEFCR,
		.bit	= 7,
		.prio_mask = 7,
	},
	[32] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 0,
		.prio_mask = 0,
	},
	[33] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 1,
		.prio_mask = 1,
	},
	[34] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 2,
		.prio_mask = 2,
	},
	[35] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 3,
		.prio_mask = 3,
	},
	[36] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 4,
		.prio_mask = 4,
	},
	[37] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 5,
		.prio_mask = 5,
	},
	[38] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 6,
		.prio_mask = 6,
	},
	[39] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_H,
		.prio	= IPIC_SIPRR_A,
		.force	= IPIC_SIFCR_H,
		.bit	= 7,
		.prio_mask = 7,
	},
	[48] = {
		.pend	= IPIC_SEPNR,
		.mask	= IPIC_SEMSR,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SEFCR,
		.bit	= 0,
		.prio_mask = 4,
	},
	[64] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 0,
		.prio_mask = 0,
	},
	[65] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 1,
		.prio_mask = 1,
	},
	[66] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 2,
		.prio_mask = 2,
	},
	[67] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 3,
		.prio_mask = 3,
	},
	[68] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 4,
		.prio_mask = 0,
	},
	[69] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 5,
		.prio_mask = 1,
	},
	[70] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 6,
		.prio_mask = 2,
	},
	[71] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 7,
		.prio_mask = 3,
	},
	[72] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 8,
	},
	[73] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 9,
	},
	[74] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 10,
	},
	[75] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 11,
	},
	[76] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 12,
	},
	[77] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 13,
	},
	[78] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 14,
	},
	[79] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 15,
	},
	[80] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 16,
	},
	[84] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 20,
	},
	[85] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 21,
	},
	[90] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 26,
	},
	[91] = {
		.pend	= IPIC_SIPNR_L,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 27,
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

#define ipic_irq_to_hw(virq)	((unsigned int)irq_map[virq].hwirq)

static void ipic_unmask_irq(unsigned int virq)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = ipic_irq_to_hw(virq);
	unsigned long flags;
	u32 temp;

	spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp |= (1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);

	spin_unlock_irqrestore(&ipic_lock, flags);
}

static void ipic_mask_irq(unsigned int virq)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = ipic_irq_to_hw(virq);
	unsigned long flags;
	u32 temp;

	spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp &= ~(1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);

	spin_unlock_irqrestore(&ipic_lock, flags);
}

static void ipic_ack_irq(unsigned int virq)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = ipic_irq_to_hw(virq);
	unsigned long flags;
	u32 temp;

	spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].pend);
	temp |= (1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].pend, temp);

	spin_unlock_irqrestore(&ipic_lock, flags);
}

static void ipic_mask_irq_and_ack(unsigned int virq)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = ipic_irq_to_hw(virq);
	unsigned long flags;
	u32 temp;

	spin_lock_irqsave(&ipic_lock, flags);

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp &= ~(1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);

	temp = ipic_read(ipic->regs, ipic_info[src].pend);
	temp |= (1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].pend, temp);

	spin_unlock_irqrestore(&ipic_lock, flags);
}

static int ipic_set_irq_type(unsigned int virq, unsigned int flow_type)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = ipic_irq_to_hw(virq);
	struct irq_desc *desc = get_irq_desc(virq);
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

	desc->status &= ~(IRQ_TYPE_SENSE_MASK | IRQ_LEVEL);
	desc->status |= flow_type & IRQ_TYPE_SENSE_MASK;
	if (flow_type & IRQ_TYPE_LEVEL_LOW)  {
		desc->status |= IRQ_LEVEL;
		set_irq_handler(virq, handle_level_irq);
	} else {
		set_irq_handler(virq, handle_edge_irq);
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
	return 0;
}

static struct irq_chip ipic_irq_chip = {
	.typename	= " IPIC  ",
	.unmask		= ipic_unmask_irq,
	.mask		= ipic_mask_irq,
	.mask_ack	= ipic_mask_irq_and_ack,
	.ack		= ipic_ack_irq,
	.set_type	= ipic_set_irq_type,
};

static int ipic_host_match(struct irq_host *h, struct device_node *node)
{
	struct ipic *ipic = h->host_data;

	/* Exact match, unless ipic node is NULL */
	return ipic->of_node == NULL || ipic->of_node == node;
}

static int ipic_host_map(struct irq_host *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct ipic *ipic = h->host_data;
	struct irq_chip *chip;

	/* Default chip */
	chip = &ipic->hc_irq;

	set_irq_chip_data(virq, ipic);
	set_irq_chip_and_handler(virq, chip, handle_level_irq);

	/* Set default irq type */
	set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static int ipic_host_xlate(struct irq_host *h, struct device_node *ct,
			   u32 *intspec, unsigned int intsize,
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

void __init ipic_init(struct device_node *node,
		unsigned int flags)
{
	struct ipic	*ipic;
	struct resource res;
	u32 temp = 0, ret;

	ipic = alloc_bootmem(sizeof(struct ipic));
	if (ipic == NULL)
		return;

	memset(ipic, 0, sizeof(struct ipic));
	ipic->of_node = node ? of_node_get(node) : NULL;

	ipic->irqhost = irq_alloc_host(IRQ_HOST_MAP_LINEAR,
				       NR_IPIC_INTS,
				       &ipic_host_ops, 0);
	if (ipic->irqhost == NULL) {
		of_node_put(node);
		return;
	}

	ret = of_address_to_resource(node, 0, &res);
	if (ret)
		return;

	ipic->regs = ioremap(res.start, res.end - res.start + 1);

	ipic->irqhost->host_data = ipic;
	ipic->hc_irq = ipic_irq_chip;

	/* init hw */
	ipic_write(ipic->regs, IPIC_SICNR, 0x0);

	/* default priority scheme is grouped. If spread mode is required
	 * configure SICFR accordingly */
	if (flags & IPIC_SPREADMODE_GRP_A)
		temp |= SICFR_IPSA;
	if (flags & IPIC_SPREADMODE_GRP_D)
		temp |= SICFR_IPSD;
	if (flags & IPIC_SPREADMODE_MIX_A)
		temp |= SICFR_MPSA;
	if (flags & IPIC_SPREADMODE_MIX_B)
		temp |= SICFR_MPSB;

	ipic_write(ipic->regs, IPIC_SICNR, temp);

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

	printk ("IPIC (%d IRQ sources) at %p\n", NR_IPIC_INTS,
			primary_ipic->regs);
}

int ipic_set_priority(unsigned int virq, unsigned int priority)
{
	struct ipic *ipic = ipic_from_irq(virq);
	unsigned int src = ipic_irq_to_hw(virq);
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
	unsigned int src = ipic_irq_to_hw(virq);
	u32 temp;

	temp = ipic_read(ipic->regs, IPIC_SICFR);

	/* clear and set HPI */
	temp &= 0x7f000000;
	temp |= (src & 0x7f) << 24;

	ipic_write(ipic->regs, IPIC_SICFR, temp);
}

void ipic_set_default_priority(void)
{
	ipic_write(primary_ipic->regs, IPIC_SIPRR_A, IPIC_SIPRR_A_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SIPRR_D, IPIC_SIPRR_D_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SMPRR_A, IPIC_SMPRR_A_DEFAULT);
	ipic_write(primary_ipic->regs, IPIC_SMPRR_B, IPIC_SMPRR_B_DEFAULT);
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

static struct sysdev_class ipic_sysclass = {
	set_kset_name("ipic"),
};

static struct sys_device device_ipic = {
	.id		= 0,
	.cls		= &ipic_sysclass,
};

static int __init init_ipic_sysfs(void)
{
	int rc;

	if (!primary_ipic->regs)
		return -ENODEV;
	printk(KERN_DEBUG "Registering ipic with sysfs...\n");

	rc = sysdev_class_register(&ipic_sysclass);
	if (rc) {
		printk(KERN_ERR "Failed registering ipic sys class\n");
		return -ENODEV;
	}
	rc = sysdev_register(&device_ipic);
	if (rc) {
		printk(KERN_ERR "Failed registering ipic sys device\n");
		return -ENODEV;
	}
	return 0;
}

subsys_initcall(init_ipic_sysfs);
