/*
 * arch/ppc/syslib/ipic.c
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
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/ipic.h>
#include <asm/mpc83xx.h>

#include "ipic.h"

static struct ipic p_ipic;
static struct ipic * primary_ipic;

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
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 0,
		.prio_mask = 0,
	},
	[65] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 1,
		.prio_mask = 1,
	},
	[66] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 2,
		.prio_mask = 2,
	},
	[67] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_A,
		.force	= IPIC_SIFCR_L,
		.bit	= 3,
		.prio_mask = 3,
	},
	[68] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 4,
		.prio_mask = 0,
	},
	[69] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 5,
		.prio_mask = 1,
	},
	[70] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 6,
		.prio_mask = 2,
	},
	[71] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= IPIC_SMPRR_B,
		.force	= IPIC_SIFCR_L,
		.bit	= 7,
		.prio_mask = 3,
	},
	[72] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 8,
	},
	[73] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 9,
	},
	[74] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 10,
	},
	[75] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 11,
	},
	[76] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 12,
	},
	[77] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 13,
	},
	[78] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 14,
	},
	[79] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 15,
	},
	[80] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 16,
	},
	[84] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 20,
	},
	[85] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 21,
	},
	[90] = {
		.pend	= IPIC_SIPNR_H,
		.mask	= IPIC_SIMSR_L,
		.prio	= 0,
		.force	= IPIC_SIFCR_L,
		.bit	= 26,
	},
	[91] = {
		.pend	= IPIC_SIPNR_H,
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

static inline struct ipic * ipic_from_irq(unsigned int irq)
{
	return primary_ipic;
}

static void ipic_enable_irq(unsigned int irq)
{
	struct ipic *ipic = ipic_from_irq(irq);
	unsigned int src = irq - ipic->irq_offset;
	u32 temp;

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp |= (1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);
}

static void ipic_disable_irq(unsigned int irq)
{
	struct ipic *ipic = ipic_from_irq(irq);
	unsigned int src = irq - ipic->irq_offset;
	u32 temp;

	temp = ipic_read(ipic->regs, ipic_info[src].mask);
	temp &= ~(1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].mask, temp);
}

static void ipic_disable_irq_and_ack(unsigned int irq)
{
	struct ipic *ipic = ipic_from_irq(irq);
	unsigned int src = irq - ipic->irq_offset;
	u32 temp;

	ipic_disable_irq(irq);

	temp = ipic_read(ipic->regs, ipic_info[src].pend);
	temp |= (1 << (31 - ipic_info[src].bit));
	ipic_write(ipic->regs, ipic_info[src].pend, temp);
}

static void ipic_end_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		ipic_enable_irq(irq);
}

struct hw_interrupt_type ipic = {
	.typename = " IPIC  ",
	.enable = ipic_enable_irq,
	.disable = ipic_disable_irq,
	.ack = ipic_disable_irq_and_ack,
	.end = ipic_end_irq,
};

void __init ipic_init(phys_addr_t phys_addr,
		unsigned int flags,
		unsigned int irq_offset,
		unsigned char *senses,
		unsigned int senses_count)
{
	u32 i, temp = 0;

	primary_ipic = &p_ipic;
	primary_ipic->regs = ioremap(phys_addr, MPC83xx_IPIC_SIZE);

	primary_ipic->irq_offset = irq_offset;

	ipic_write(primary_ipic->regs, IPIC_SICNR, 0x0);

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

	ipic_write(primary_ipic->regs, IPIC_SICNR, temp);

	/* handle MCP route */
	temp = 0;
	if (flags & IPIC_DISABLE_MCP_OUT)
		temp = SERCR_MCPR;
	ipic_write(primary_ipic->regs, IPIC_SERCR, temp);

	/* handle routing of IRQ0 to MCP */
	temp = ipic_read(primary_ipic->regs, IPIC_SEMSR);

	if (flags & IPIC_IRQ0_MCP)
		temp |= SEMSR_SIRQ0;
	else
		temp &= ~SEMSR_SIRQ0;

	ipic_write(primary_ipic->regs, IPIC_SEMSR, temp);

	for (i = 0 ; i < NR_IPIC_INTS ; i++) {
		irq_desc[i+irq_offset].chip = &ipic;
		irq_desc[i+irq_offset].status = IRQ_LEVEL;
	}

	temp = 0;
	for (i = 0 ; i < senses_count ; i++) {
		if ((senses[i] & IRQ_SENSE_MASK) == IRQ_SENSE_EDGE) {
			temp |= 1 << (15 - i);
			if (i != 0)
				irq_desc[i + irq_offset + MPC83xx_IRQ_EXT1 - 1].status = 0;
			else
				irq_desc[irq_offset + MPC83xx_IRQ_EXT0].status = 0;
		}
	}
	ipic_write(primary_ipic->regs, IPIC_SECNR, temp);

	printk ("IPIC (%d IRQ sources, %d External IRQs) at %p\n", NR_IPIC_INTS,
			senses_count, primary_ipic->regs);
}

int ipic_set_priority(unsigned int irq, unsigned int priority)
{
	struct ipic *ipic = ipic_from_irq(irq);
	unsigned int src = irq - ipic->irq_offset;
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

void ipic_set_highest_priority(unsigned int irq)
{
	struct ipic *ipic = ipic_from_irq(irq);
	unsigned int src = irq - ipic->irq_offset;
	u32 temp;

	temp = ipic_read(ipic->regs, IPIC_SICFR);

	/* clear and set HPI */
	temp &= 0x7f000000;
	temp |= (src & 0x7f) << 24;

	ipic_write(ipic->regs, IPIC_SICFR, temp);
}

void ipic_set_default_priority(void)
{
	ipic_set_priority(MPC83xx_IRQ_TSEC1_TX, 0);
	ipic_set_priority(MPC83xx_IRQ_TSEC1_RX, 1);
	ipic_set_priority(MPC83xx_IRQ_TSEC1_ERROR, 2);
	ipic_set_priority(MPC83xx_IRQ_TSEC2_TX, 3);
	ipic_set_priority(MPC83xx_IRQ_TSEC2_RX, 4);
	ipic_set_priority(MPC83xx_IRQ_TSEC2_ERROR, 5);
	ipic_set_priority(MPC83xx_IRQ_USB2_DR, 6);
	ipic_set_priority(MPC83xx_IRQ_USB2_MPH, 7);

	ipic_set_priority(MPC83xx_IRQ_UART1, 0);
	ipic_set_priority(MPC83xx_IRQ_UART2, 1);
	ipic_set_priority(MPC83xx_IRQ_SEC2, 2);
	ipic_set_priority(MPC83xx_IRQ_IIC1, 5);
	ipic_set_priority(MPC83xx_IRQ_IIC2, 6);
	ipic_set_priority(MPC83xx_IRQ_SPI, 7);
	ipic_set_priority(MPC83xx_IRQ_RTC_SEC, 0);
	ipic_set_priority(MPC83xx_IRQ_PIT, 1);
	ipic_set_priority(MPC83xx_IRQ_PCI1, 2);
	ipic_set_priority(MPC83xx_IRQ_PCI2, 3);
	ipic_set_priority(MPC83xx_IRQ_EXT0, 4);
	ipic_set_priority(MPC83xx_IRQ_EXT1, 5);
	ipic_set_priority(MPC83xx_IRQ_EXT2, 6);
	ipic_set_priority(MPC83xx_IRQ_EXT3, 7);
	ipic_set_priority(MPC83xx_IRQ_RTC_ALR, 0);
	ipic_set_priority(MPC83xx_IRQ_MU, 1);
	ipic_set_priority(MPC83xx_IRQ_SBA, 2);
	ipic_set_priority(MPC83xx_IRQ_DMA, 3);
	ipic_set_priority(MPC83xx_IRQ_EXT4, 4);
	ipic_set_priority(MPC83xx_IRQ_EXT5, 5);
	ipic_set_priority(MPC83xx_IRQ_EXT6, 6);
	ipic_set_priority(MPC83xx_IRQ_EXT7, 7);
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

/* Return an interrupt vector or -1 if no interrupt is pending. */
int ipic_get_irq(void)
{
	int irq;

	irq = ipic_read(primary_ipic->regs, IPIC_SIVCR) & 0x7f;

	if (irq == 0)    /* 0 --> no irq is pending */
		irq = -1;

	return irq;
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
