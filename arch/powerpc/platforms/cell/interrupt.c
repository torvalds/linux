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
 *
 * TODO:
 * - Fix various assumptions related to HW CPU numbers vs. linux CPU numbers
 *   vs node numbers in the setup code
 * - Implement proper handling of maxcpus=1/2 (that is, routing of irqs from
 *   a non-active node to the active node)
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/kernel_stat.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/cell-regs.h>

#include "interrupt.h"

struct iic {
	struct cbe_iic_thread_regs __iomem *regs;
	u8 target_id;
	u8 eoi_stack[16];
	int eoi_ptr;
	struct device_node *node;
};

static DEFINE_PER_CPU(struct iic, iic);
#define IIC_NODE_COUNT	2
static struct irq_host *iic_host;

/* Convert between "pending" bits and hw irq number */
static irq_hw_number_t iic_pending_to_hwnum(struct cbe_iic_pending_bits bits)
{
	unsigned char unit = bits.source & 0xf;
	unsigned char node = bits.source >> 4;
	unsigned char class = bits.class & 3;

	/* Decode IPIs */
	if (bits.flags & CBE_IIC_IRQ_IPI)
		return IIC_IRQ_TYPE_IPI | (bits.prio >> 4);
	else
		return (node << IIC_IRQ_NODE_SHIFT) | (class << 4) | unit;
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


static void iic_ioexc_eoi(unsigned int irq)
{
}

static void iic_ioexc_cascade(unsigned int irq, struct irq_desc *desc)
{
	struct cbe_iic_regs __iomem *node_iic = (void __iomem *)desc->handler_data;
	unsigned int base = (irq & 0xffffff00) | IIC_IRQ_TYPE_IOEXC;
	unsigned long bits, ack;
	int cascade;

	for (;;) {
		bits = in_be64(&node_iic->iic_is);
		if (bits == 0)
			break;
		/* pre-ack edge interrupts */
		ack = bits & IIC_ISR_EDGE_MASK;
		if (ack)
			out_be64(&node_iic->iic_is, ack);
		/* handle them */
		for (cascade = 63; cascade >= 0; cascade--)
			if (bits & (0x8000000000000000UL >> cascade)) {
				unsigned int cirq =
					irq_linear_revmap(iic_host,
							  base | cascade);
				if (cirq != NO_IRQ)
					generic_handle_irq(cirq);
			}
		/* post-ack level interrupts */
		ack = bits & ~IIC_ISR_EDGE_MASK;
		if (ack)
			out_be64(&node_iic->iic_is, ack);
	}
	desc->chip->eoi(irq);
}


static struct irq_chip iic_ioexc_chip = {
	.typename = " CELL-IOEX",
	.mask = iic_mask,
	.unmask = iic_unmask,
	.eoi = iic_ioexc_eoi,
};

/* Get an IRQ number from the pending state register of the IIC */
static unsigned int iic_get_irq(void)
{
	struct cbe_iic_pending_bits pending;
	struct iic *iic;
	unsigned int virq;

	iic = &__get_cpu_var(iic);
	*(unsigned long *) &pending =
		in_be64((u64 __iomem *) &iic->regs->pending_destr);
	if (!(pending.flags & CBE_IIC_IRQ_VALID))
		return NO_IRQ;
	virq = irq_linear_revmap(iic_host, iic_pending_to_hwnum(pending));
	if (virq == NO_IRQ)
		return NO_IRQ;
	iic->eoi_stack[++iic->eoi_ptr] = pending.prio;
	BUG_ON(iic->eoi_ptr > 15);
	return virq;
}

void iic_setup_cpu(void)
{
	out_be64(&__get_cpu_var(iic).regs->prio, 0xff);
}

u8 iic_get_target_id(int cpu)
{
	return per_cpu(iic, cpu).target_id;
}

EXPORT_SYMBOL_GPL(iic_get_target_id);

#ifdef CONFIG_SMP

/* Use the highest interrupt priorities for IPI */
static inline int iic_ipi_to_irq(int ipi)
{
	return IIC_IRQ_TYPE_IPI + 0xf - ipi;
}

void iic_cause_IPI(int cpu, int mesg)
{
	out_be64(&per_cpu(iic, cpu).regs->generate, (0xf - mesg) << 4);
}

struct irq_host *iic_get_irq_host(int node)
{
	return iic_host;
}
EXPORT_SYMBOL_GPL(iic_get_irq_host);

static irqreturn_t iic_ipi_action(int irq, void *dev_id)
{
	int ipi = (int)(long)dev_id;

	smp_message_recv(ipi);

	return IRQ_HANDLED;
}
static void iic_request_ipi(int ipi, const char *name)
{
	int virq;

	virq = irq_create_mapping(iic_host, iic_ipi_to_irq(ipi));
	if (virq == NO_IRQ) {
		printk(KERN_ERR
		       "iic: failed to map IPI %s\n", name);
		return;
	}
	if (request_irq(virq, iic_ipi_action, IRQF_DISABLED, name,
			(void *)(long)ipi))
		printk(KERN_ERR
		       "iic: failed to request IPI %s\n", name);
}

void iic_request_IPIs(void)
{
	iic_request_ipi(PPC_MSG_CALL_FUNCTION, "IPI-call");
	iic_request_ipi(PPC_MSG_RESCHEDULE, "IPI-resched");
	iic_request_ipi(PPC_MSG_CALL_FUNC_SINGLE, "IPI-call-single");
#ifdef CONFIG_DEBUGGER
	iic_request_ipi(PPC_MSG_DEBUGGER_BREAK, "IPI-debug");
#endif /* CONFIG_DEBUGGER */
}

#endif /* CONFIG_SMP */


static int iic_host_match(struct irq_host *h, struct device_node *node)
{
	return of_device_is_compatible(node,
				    "IBM,CBEA-Internal-Interrupt-Controller");
}

extern int noirqdebug;

static void handle_iic_irq(unsigned int irq, struct irq_desc *desc)
{
	spin_lock(&desc->lock);

	desc->status &= ~(IRQ_REPLAY | IRQ_WAITING);

	/*
	 * If we're currently running this IRQ, or its disabled,
	 * we shouldn't process the IRQ. Mark it pending, handle
	 * the necessary masking and go out
	 */
	if (unlikely((desc->status & (IRQ_INPROGRESS | IRQ_DISABLED)) ||
		    !desc->action)) {
		desc->status |= IRQ_PENDING;
		goto out_eoi;
	}

	kstat_incr_irqs_this_cpu(irq, desc);

	/* Mark the IRQ currently in progress.*/
	desc->status |= IRQ_INPROGRESS;

	do {
		struct irqaction *action = desc->action;
		irqreturn_t action_ret;

		if (unlikely(!action))
			goto out_eoi;

		desc->status &= ~IRQ_PENDING;
		spin_unlock(&desc->lock);
		action_ret = handle_IRQ_event(irq, action);
		if (!noirqdebug)
			note_interrupt(irq, desc, action_ret);
		spin_lock(&desc->lock);

	} while ((desc->status & (IRQ_PENDING | IRQ_DISABLED)) == IRQ_PENDING);

	desc->status &= ~IRQ_INPROGRESS;
out_eoi:
	desc->chip->eoi(irq);
	spin_unlock(&desc->lock);
}

static int iic_host_map(struct irq_host *h, unsigned int virq,
			irq_hw_number_t hw)
{
	switch (hw & IIC_IRQ_TYPE_MASK) {
	case IIC_IRQ_TYPE_IPI:
		set_irq_chip_and_handler(virq, &iic_chip, handle_percpu_irq);
		break;
	case IIC_IRQ_TYPE_IOEXC:
		set_irq_chip_and_handler(virq, &iic_ioexc_chip,
					 handle_iic_irq);
		break;
	default:
		set_irq_chip_and_handler(virq, &iic_chip, handle_iic_irq);
	}
	return 0;
}

static int iic_host_xlate(struct irq_host *h, struct device_node *ct,
			   u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	unsigned int node, ext, unit, class;
	const u32 *val;

	if (!of_device_is_compatible(ct,
				     "IBM,CBEA-Internal-Interrupt-Controller"))
		return -ENODEV;
	if (intsize != 1)
		return -ENODEV;
	val = of_get_property(ct, "#interrupt-cells", NULL);
	if (val == NULL || *val != 1)
		return -ENODEV;

	node = intspec[0] >> 24;
	ext = (intspec[0] >> 16) & 0xff;
	class = (intspec[0] >> 8) & 0xff;
	unit = intspec[0] & 0xff;

	/* Check if node is in supported range */
	if (node > 1)
		return -EINVAL;

	/* Build up interrupt number, special case for IO exceptions */
	*out_hwirq = (node << IIC_IRQ_NODE_SHIFT);
	if (unit == IIC_UNIT_IIC && class == 1)
		*out_hwirq |= IIC_IRQ_TYPE_IOEXC | ext;
	else
		*out_hwirq |= IIC_IRQ_TYPE_NORMAL |
			(class << IIC_IRQ_CLASS_SHIFT) | unit;

	/* Dummy flags, ignored by iic code */
	*out_flags = IRQ_TYPE_EDGE_RISING;

	return 0;
}

static struct irq_host_ops iic_host_ops = {
	.match = iic_host_match,
	.map = iic_host_map,
	.xlate = iic_host_xlate,
};

static void __init init_one_iic(unsigned int hw_cpu, unsigned long addr,
				struct device_node *node)
{
	/* XXX FIXME: should locate the linux CPU number from the HW cpu
	 * number properly. We are lucky for now
	 */
	struct iic *iic = &per_cpu(iic, hw_cpu);

	iic->regs = ioremap(addr, sizeof(struct cbe_iic_thread_regs));
	BUG_ON(iic->regs == NULL);

	iic->target_id = ((hw_cpu & 2) << 3) | ((hw_cpu & 1) ? 0xf : 0xe);
	iic->eoi_stack[0] = 0xff;
	iic->node = of_node_get(node);
	out_be64(&iic->regs->prio, 0);

	printk(KERN_INFO "IIC for CPU %d target id 0x%x : %s\n",
	       hw_cpu, iic->target_id, node->full_name);
}

static int __init setup_iic(void)
{
	struct device_node *dn;
	struct resource r0, r1;
	unsigned int node, cascade, found = 0;
	struct cbe_iic_regs __iomem *node_iic;
	const u32 *np;

	for (dn = NULL;
	     (dn = of_find_node_by_name(dn,"interrupt-controller")) != NULL;) {
		if (!of_device_is_compatible(dn,
				     "IBM,CBEA-Internal-Interrupt-Controller"))
			continue;
		np = of_get_property(dn, "ibm,interrupt-server-ranges", NULL);
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
		found++;
		init_one_iic(np[0], r0.start, dn);
		init_one_iic(np[1], r1.start, dn);

		/* Setup cascade for IO exceptions. XXX cleanup tricks to get
		 * node vs CPU etc...
		 * Note that we configure the IIC_IRR here with a hard coded
		 * priority of 1. We might want to improve that later.
		 */
		node = np[0] >> 1;
		node_iic = cbe_get_cpu_iic_regs(np[0]);
		cascade = node << IIC_IRQ_NODE_SHIFT;
		cascade |= 1 << IIC_IRQ_CLASS_SHIFT;
		cascade |= IIC_UNIT_IIC;
		cascade = irq_create_mapping(iic_host, cascade);
		if (cascade == NO_IRQ)
			continue;
		/*
		 * irq_data is a generic pointer that gets passed back
		 * to us later, so the forced cast is fine.
		 */
		set_irq_data(cascade, (void __force *)node_iic);
		set_irq_chained_handler(cascade , iic_ioexc_cascade);
		out_be64(&node_iic->iic_ir,
			 (1 << 12)		/* priority */ |
			 (node << 4)		/* dest node */ |
			 IIC_UNIT_THREAD_0	/* route them to thread 0 */);
		/* Flush pending (make sure it triggers if there is
		 * anything pending
		 */
		out_be64(&node_iic->iic_is, 0xfffffffffffffffful);
	}

	if (found)
		return 0;
	else
		return -ENODEV;
}

void __init iic_init_IRQ(void)
{
	/* Setup an irq host data structure */
	iic_host = irq_alloc_host(NULL, IRQ_HOST_MAP_LINEAR, IIC_SOURCE_COUNT,
				  &iic_host_ops, IIC_IRQ_INVALID);
	BUG_ON(iic_host == NULL);
	irq_set_default_host(iic_host);

	/* Discover and initialize iics */
	if (setup_iic() < 0)
		panic("IIC: Failed to initialize !\n");

	/* Set master interrupt handling function */
	ppc_md.get_irq = iic_get_irq;

	/* Enable on current CPU */
	iic_setup_cpu();
}

void iic_set_interrupt_routing(int cpu, int thread, int priority)
{
	struct cbe_iic_regs __iomem *iic_regs = cbe_get_cpu_iic_regs(cpu);
	u64 iic_ir = 0;
	int node = cpu >> 1;

	/* Set which node and thread will handle the next interrupt */
	iic_ir |= CBE_IIC_IR_PRIO(priority) |
		  CBE_IIC_IR_DEST_NODE(node);
	if (thread == 0)
		iic_ir |= CBE_IIC_IR_DEST_UNIT(CBE_IIC_IR_PT_0);
	else
		iic_ir |= CBE_IIC_IR_DEST_UNIT(CBE_IIC_IR_PT_1);
	out_be64(&iic_regs->iic_ir, iic_ir);
}
