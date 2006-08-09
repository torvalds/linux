/*
 * Cell Internal Interrupt Controller
 *
 * Copyright (C) 2006 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *                    IBM, Corp.
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
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>

#include "interrupt.h"
#include "cbe_regs.h"

struct iic {
	struct cbe_iic_thread_regs __iomem *regs;
	u8 target_id;
	u8 eoi_stack[16];
	int eoi_ptr;
	struct irq_host *host;
};

static DEFINE_PER_CPU(struct iic, iic);
#define IIC_NODE_COUNT	2
static struct irq_host *iic_hosts[IIC_NODE_COUNT];

/* Convert between "pending" bits and hw irq number */
static irq_hw_number_t iic_pending_to_hwnum(struct cbe_iic_pending_bits bits)
{
	unsigned char unit = bits.source & 0xf;

	if (bits.flags & CBE_IIC_IRQ_IPI)
		return IIC_IRQ_IPI0 | (bits.prio >> 4);
	else if (bits.class <= 3)
		return (bits.class << 4) | unit;
	else
		return IIC_IRQ_INVALID;
}

static void iic_mask(unsigned int irq)
{
}

static void iic_unmask(unsigned int irq)
{
}

static void iic_eoi(unsigned int irq)
{
	struct iic *iic = &__get_cpu_var(iic);
	out_be64(&iic->regs->prio, iic->eoi_stack[--iic->eoi_ptr]);
	BUG_ON(iic->eoi_ptr < 0);
}

static struct irq_chip iic_chip = {
	.typename = " CELL-IIC ",
	.mask = iic_mask,
	.unmask = iic_unmask,
	.eoi = iic_eoi,
};

/* Get an IRQ number from the pending state register of the IIC */
static unsigned int iic_get_irq(struct pt_regs *regs)
{
	struct cbe_iic_pending_bits pending;
	struct iic *iic;

	iic = &__get_cpu_var(iic);
	*(unsigned long *) &pending =
		in_be64((unsigned long __iomem *) &iic->regs->pending_destr);
	iic->eoi_stack[++iic->eoi_ptr] = pending.prio;
	BUG_ON(iic->eoi_ptr > 15);
	if (pending.flags & CBE_IIC_IRQ_VALID)
		return irq_linear_revmap(iic->host,
					 iic_pending_to_hwnum(pending));
	return NO_IRQ;
}

#ifdef CONFIG_SMP

/* Use the highest interrupt priorities for IPI */
static inline int iic_ipi_to_irq(int ipi)
{
	return IIC_IRQ_IPI0 + IIC_NUM_IPIS - 1 - ipi;
}

static inline int iic_irq_to_ipi(int irq)
{
	return IIC_NUM_IPIS - 1 - (irq - IIC_IRQ_IPI0);
}

void iic_setup_cpu(void)
{
	out_be64(&__get_cpu_var(iic).regs->prio, 0xff);
}

void iic_cause_IPI(int cpu, int mesg)
{
	out_be64(&per_cpu(iic, cpu).regs->generate, (IIC_NUM_IPIS - 1 - mesg) << 4);
}

u8 iic_get_target_id(int cpu)
{
	return per_cpu(iic, cpu).target_id;
}
EXPORT_SYMBOL_GPL(iic_get_target_id);

struct irq_host *iic_get_irq_host(int node)
{
	if (node < 0 || node >= IIC_NODE_COUNT)
		return NULL;
	return iic_hosts[node];
}
EXPORT_SYMBOL_GPL(iic_get_irq_host);


static irqreturn_t iic_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	int ipi = (int)(long)dev_id;

	smp_message_recv(ipi, regs);

	return IRQ_HANDLED;
}

static void iic_request_ipi(int ipi, const char *name)
{
	int node, virq;

	for (node = 0; node < IIC_NODE_COUNT; node++) {
		char *rname;
		if (iic_hosts[node] == NULL)
			continue;
		virq = irq_create_mapping(iic_hosts[node],
					  iic_ipi_to_irq(ipi));
		if (virq == NO_IRQ) {
			printk(KERN_ERR
			       "iic: failed to map IPI %s on node %d\n",
			       name, node);
			continue;
		}
		rname = kzalloc(strlen(name) + 16, GFP_KERNEL);
		if (rname)
			sprintf(rname, "%s node %d", name, node);
		else
			rname = (char *)name;
		if (request_irq(virq, iic_ipi_action, IRQF_DISABLED,
				rname, (void *)(long)ipi))
			printk(KERN_ERR
			       "iic: failed to request IPI %s on node %d\n",
			       name, node);
	}
}

void iic_request_IPIs(void)
{
	iic_request_ipi(PPC_MSG_CALL_FUNCTION, "IPI-call");
	iic_request_ipi(PPC_MSG_RESCHEDULE, "IPI-resched");
#ifdef CONFIG_DEBUGGER
	iic_request_ipi(PPC_MSG_DEBUGGER_BREAK, "IPI-debug");
#endif /* CONFIG_DEBUGGER */
}

#endif /* CONFIG_SMP */


static int iic_host_match(struct irq_host *h, struct device_node *node)
{
	return h->host_data != NULL && node == h->host_data;
}

static int iic_host_map(struct irq_host *h, unsigned int virq,
			irq_hw_number_t hw)
{
	if (hw < IIC_IRQ_IPI0)
		set_irq_chip_and_handler(virq, &iic_chip, handle_fasteoi_irq);
	else
		set_irq_chip_and_handler(virq, &iic_chip, handle_percpu_irq);
	return 0;
}

static int iic_host_xlate(struct irq_host *h, struct device_node *ct,
			   u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	/* Currently, we don't translate anything. That needs to be fixed as
	 * we get better defined device-trees. iic interrupts have to be
	 * explicitely mapped by whoever needs them
	 */
	return -ENODEV;
}

static struct irq_host_ops iic_host_ops = {
	.match = iic_host_match,
	.map = iic_host_map,
	.xlate = iic_host_xlate,
};

static void __init init_one_iic(unsigned int hw_cpu, unsigned long addr,
				struct irq_host *host)
{
	/* XXX FIXME: should locate the linux CPU number from the HW cpu
	 * number properly. We are lucky for now
	 */
	struct iic *iic = &per_cpu(iic, hw_cpu);

	iic->regs = ioremap(addr, sizeof(struct cbe_iic_thread_regs));
	BUG_ON(iic->regs == NULL);

	iic->target_id = ((hw_cpu & 2) << 3) | ((hw_cpu & 1) ? 0xf : 0xe);
	iic->eoi_stack[0] = 0xff;
	iic->host = host;
	out_be64(&iic->regs->prio, 0);

	printk(KERN_INFO "IIC for CPU %d at %lx mapped to %p, target id 0x%x\n",
	       hw_cpu, addr, iic->regs, iic->target_id);
}

static int __init setup_iic(void)
{
	struct device_node *dn;
	struct resource r0, r1;
	struct irq_host *host;
	int found = 0;
	const u32 *np;

	for (dn = NULL;
	     (dn = of_find_node_by_name(dn,"interrupt-controller")) != NULL;) {
		if (!device_is_compatible(dn,
				     "IBM,CBEA-Internal-Interrupt-Controller"))
			continue;
		np = get_property(dn, "ibm,interrupt-server-ranges", NULL);
		if (np == NULL) {
			printk(KERN_WARNING "IIC: CPU association not found\n");
			of_node_put(dn);
			return -ENODEV;
		}
		if (of_address_to_resource(dn, 0, &r0) ||
		    of_address_to_resource(dn, 1, &r1)) {
			printk(KERN_WARNING "IIC: Can't resolve addresses\n");
			of_node_put(dn);
			return -ENODEV;
		}
		host = NULL;
		if (found < IIC_NODE_COUNT) {
			host = irq_alloc_host(IRQ_HOST_MAP_LINEAR,
					      IIC_SOURCE_COUNT,
					      &iic_host_ops,
					      IIC_IRQ_INVALID);
			iic_hosts[found] = host;
			BUG_ON(iic_hosts[found] == NULL);
			iic_hosts[found]->host_data = of_node_get(dn);
			found++;
		}
		init_one_iic(np[0], r0.start, host);
		init_one_iic(np[1], r1.start, host);
	}

	if (found)
		return 0;
	else
		return -ENODEV;
}

void __init iic_init_IRQ(void)
{
	/* Discover and initialize iics */
	if (setup_iic() < 0)
		panic("IIC: Failed to initialize !\n");

	/* Set master interrupt handling function */
	ppc_md.get_irq = iic_get_irq;

	/* Enable on current CPU */
	iic_setup_cpu();
}
