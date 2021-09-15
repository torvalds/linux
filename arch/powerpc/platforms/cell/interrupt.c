// SPDX-License-Identifier: GPL-2.0-or-later
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
 * TODO:
 * - Fix various assumptions related to HW CPU numbers vs. linux CPU numbers
 *   vs node numbers in the setup code
 * - Implement proper handling of maxcpus=1/2 (that is, routing of irqs from
 *   a non-active node to the active node)
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/kernel_stat.h>
#include <linux/pgtable.h>

#include <asm/io.h>
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

static DEFINE_PER_CPU(struct iic, cpu_iic);
#define IIC_NODE_COUNT	2
static struct irq_domain *iic_host;

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

static void iic_mask(struct irq_data *d)
{
}

static void iic_unmask(struct irq_data *d)
{
}

static void iic_eoi(struct irq_data *d)
{
	struct iic *iic = this_cpu_ptr(&cpu_iic);
	out_be64(&iic->regs->prio, iic->eoi_stack[--iic->eoi_ptr]);
	BUG_ON(iic->eoi_ptr < 0);
}

static struct irq_chip iic_chip = {
	.name = "CELL-IIC",
	.irq_mask = iic_mask,
	.irq_unmask = iic_unmask,
	.irq_eoi = iic_eoi,
};


static void iic_ioexc_eoi(struct irq_data *d)
{
}

static void iic_ioexc_cascade(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct cbe_iic_regs __iomem *node_iic =
		(void __iomem *)irq_desc_get_handler_data(desc);
	unsigned int irq = irq_desc_get_irq(desc);
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
			if (bits & (0x8000000000000000UL >> cascade))
				generic_handle_domain_irq(iic_host,
							  base | cascade);
		/* post-ack level interrupts */
		ack = bits & ~IIC_ISR_EDGE_MASK;
		if (ack)
			out_be64(&node_iic->iic_is, ack);
	}
	chip->irq_eoi(&desc->irq_data);
}


static struct irq_chip iic_ioexc_chip = {
	.name = "CELL-IOEX",
	.irq_mask = iic_mask,
	.irq_unmask = iic_unmask,
	.irq_eoi = iic_ioexc_eoi,
};

/* Get an IRQ number from the pending state register of the IIC */
static unsigned int iic_get_irq(void)
{
	struct cbe_iic_pending_bits pending;
	struct iic *iic;
	unsigned int virq;

	iic = this_cpu_ptr(&cpu_iic);
	*(unsigned long *) &pending =
		in_be64((u64 __iomem *) &iic->regs->pending_destr);
	if (!(pending.flags & CBE_IIC_IRQ_VALID))
		return 0;
	virq = irq_linear_revmap(iic_host, iic_pending_to_hwnum(pending));
	if (!virq)
		return 0;
	iic->eoi_stack[++iic->eoi_ptr] = pending.prio;
	BUG_ON(iic->eoi_ptr > 15);
	return virq;
}

void iic_setup_cpu(void)
{
	out_be64(&this_cpu_ptr(&cpu_iic)->regs->prio, 0xff);
}

u8 iic_get_target_id(int cpu)
{
	return per_cpu(cpu_iic, cpu).target_id;
}

EXPORT_SYMBOL_GPL(iic_get_target_id);

#ifdef CONFIG_SMP

/* Use the highest interrupt priorities for IPI */
static inline int iic_msg_to_irq(int msg)
{
	return IIC_IRQ_TYPE_IPI + 0xf - msg;
}

void iic_message_pass(int cpu, int msg)
{
	out_be64(&per_cpu(cpu_iic, cpu).regs->generate, (0xf - msg) << 4);
}

static void iic_request_ipi(int msg)
{
	int virq;

	virq = irq_create_mapping(iic_host, iic_msg_to_irq(msg));
	if (!virq) {
		printk(KERN_ERR
		       "iic: failed to map IPI %s\n", smp_ipi_name[msg]);
		return;
	}

	/*
	 * If smp_request_message_ipi encounters an error it will notify
	 * the error.  If a message is not needed it will return non-zero.
	 */
	if (smp_request_message_ipi(virq, msg))
		irq_dispose_mapping(virq);
}

void iic_request_IPIs(void)
{
	iic_request_ipi(PPC_MSG_CALL_FUNCTION);
	iic_request_ipi(PPC_MSG_RESCHEDULE);
	iic_request_ipi(PPC_MSG_TICK_BROADCAST);
	iic_request_ipi(PPC_MSG_NMI_IPI);
}

#endif /* CONFIG_SMP */


static int iic_host_match(struct irq_domain *h, struct device_node *node,
			  enum irq_domain_bus_token bus_token)
{
	return of_device_is_compatible(node,
				    "IBM,CBEA-Internal-Interrupt-Controller");
}

static int iic_host_map(struct irq_domain *h, unsigned int virq,
			irq_hw_number_t hw)
{
	switch (hw & IIC_IRQ_TYPE_MASK) {
	case IIC_IRQ_TYPE_IPI:
		irq_set_chip_and_handler(virq, &iic_chip, handle_percpu_irq);
		break;
	case IIC_IRQ_TYPE_IOEXC:
		irq_set_chip_and_handler(virq, &iic_ioexc_chip,
					 handle_edge_eoi_irq);
		break;
	default:
		irq_set_chip_and_handler(virq, &iic_chip, handle_edge_eoi_irq);
	}
	return 0;
}

static int iic_host_xlate(struct irq_domain *h, struct device_node *ct,
			   const u32 *intspec, unsigned int intsize,
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

static const struct irq_domain_ops iic_host_ops = {
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
	struct iic *iic = &per_cpu(cpu_iic, hw_cpu);

	iic->regs = ioremap(addr, sizeof(struct cbe_iic_thread_regs));
	BUG_ON(iic->regs == NULL);

	iic->target_id = ((hw_cpu & 2) << 3) | ((hw_cpu & 1) ? 0xf : 0xe);
	iic->eoi_stack[0] = 0xff;
	iic->node = of_node_get(node);
	out_be64(&iic->regs->prio, 0);

	printk(KERN_INFO "IIC for CPU %d target id 0x%x : %pOF\n",
	       hw_cpu, iic->target_id, node);
}

static int __init setup_iic(void)
{
	struct device_node *dn;
	struct resource r0, r1;
	unsigned int node, cascade, found = 0;
	struct cbe_iic_regs __iomem *node_iic;
	const u32 *np;

	for_each_node_by_name(dn, "interrupt-controller") {
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
		if (!cascade)
			continue;
		/*
		 * irq_data is a generic pointer that gets passed back
		 * to us later, so the forced cast is fine.
		 */
		irq_set_handler_data(cascade, (void __force *)node_iic);
		irq_set_chained_handler(cascade, iic_ioexc_cascade);
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
	iic_host = irq_domain_add_linear(NULL, IIC_SOURCE_COUNT, &iic_host_ops,
					 NULL);
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
