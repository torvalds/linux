/*
 * BPA Internal Interrupt Controller
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

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/percpu.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/ptrace.h>

#include "bpa_iic.h"

struct iic_pending_bits {
	u32 data;
	u8 flags;
	u8 class;
	u8 source;
	u8 prio;
};

enum iic_pending_flags {
	IIC_VALID = 0x80,
	IIC_IPI   = 0x40,
};

struct iic_regs {
	struct iic_pending_bits pending;
	struct iic_pending_bits pending_destr;
	u64 generate;
	u64 prio;
};

struct iic {
	struct iic_regs __iomem *regs;
};

static DEFINE_PER_CPU(struct iic, iic);

void iic_local_enable(void)
{
	out_be64(&__get_cpu_var(iic).regs->prio, 0xff);
}

void iic_local_disable(void)
{
	out_be64(&__get_cpu_var(iic).regs->prio, 0x0);
}

static unsigned int iic_startup(unsigned int irq)
{
	return 0;
}

static void iic_enable(unsigned int irq)
{
	iic_local_enable();
}

static void iic_disable(unsigned int irq)
{
}

static void iic_end(unsigned int irq)
{
	iic_local_enable();
}

static struct hw_interrupt_type iic_pic = {
	.typename = " BPA-IIC  ",
	.startup = iic_startup,
	.enable = iic_enable,
	.disable = iic_disable,
	.end = iic_end,
};

static int iic_external_get_irq(struct iic_pending_bits pending)
{
	int irq;
	unsigned char node, unit;

	node = pending.source >> 4;
	unit = pending.source & 0xf;
	irq = -1;

	/*
	 * This mapping is specific to the Broadband
	 * Engine. We might need to get the numbers
	 * from the device tree to support future CPUs.
	 */
	switch (unit) {
	case 0x00:
	case 0x0b:
		/*
		 * One of these units can be connected
		 * to an external interrupt controller.
		 */
		if (pending.prio > 0x3f ||
		    pending.class != 2)
			break;
		irq = IIC_EXT_OFFSET
			+ spider_get_irq(pending.prio + node * IIC_NODE_STRIDE)
			+ node * IIC_NODE_STRIDE;
		break;
	case 0x01 ... 0x04:
	case 0x07 ... 0x0a:
		/*
		 * These units are connected to the SPEs
		 */
		if (pending.class > 2)
			break;
		irq = IIC_SPE_OFFSET
			+ pending.class * IIC_CLASS_STRIDE
			+ node * IIC_NODE_STRIDE
			+ unit;
		break;
	}
	if (irq == -1)
		printk(KERN_WARNING "Unexpected interrupt class %02x, "
			"source %02x, prio %02x, cpu %02x\n", pending.class,
			pending.source, pending.prio, smp_processor_id());
	return irq;
}

/* Get an IRQ number from the pending state register of the IIC */
int iic_get_irq(struct pt_regs *regs)
{
	struct iic *iic;
	int irq;
	struct iic_pending_bits pending;

	iic = &__get_cpu_var(iic);
	*(unsigned long *) &pending = 
		in_be64((unsigned long __iomem *) &iic->regs->pending_destr);

	irq = -1;
	if (pending.flags & IIC_VALID) {
		if (pending.flags & IIC_IPI) {
			irq = IIC_IPI_OFFSET + (pending.prio >> 4);
/*
			if (irq > 0x80)
				printk(KERN_WARNING "Unexpected IPI prio %02x"
					"on CPU %02x\n", pending.prio,
							smp_processor_id());
*/
		} else {
			irq = iic_external_get_irq(pending);
		}
	}
	return irq;
}

static struct iic_regs __iomem *find_iic(int cpu)
{
	struct device_node *np;
	int nodeid = cpu / 2;
	unsigned long regs;
	struct iic_regs __iomem *iic_regs;

	for (np = of_find_node_by_type(NULL, "cpu");
	     np;
	     np = of_find_node_by_type(np, "cpu")) {
		if (nodeid == *(int *)get_property(np, "node-id", NULL))
			break;
	}

	if (!np) {
		printk(KERN_WARNING "IIC: CPU %d not found\n", cpu);
		iic_regs = NULL;
	} else {
		regs = *(long *)get_property(np, "iic", NULL);

		/* hack until we have decided on the devtree info */
		regs += 0x400;
		if (cpu & 1)
			regs += 0x20;

		printk(KERN_DEBUG "IIC for CPU %d at %lx\n", cpu, regs);
		iic_regs = __ioremap(regs, sizeof(struct iic_regs),
						 _PAGE_NO_CACHE);
	}
	return iic_regs;
}

#ifdef CONFIG_SMP

/* Use the highest interrupt priorities for IPI */
static inline int iic_ipi_to_irq(int ipi)
{
	return IIC_IPI_OFFSET + IIC_NUM_IPIS - 1 - ipi;
}

static inline int iic_irq_to_ipi(int irq)
{
	return IIC_NUM_IPIS - 1 - (irq - IIC_IPI_OFFSET);
}

void iic_setup_cpu(void)
{
	out_be64(&__get_cpu_var(iic).regs->prio, 0xff);
}

void iic_cause_IPI(int cpu, int mesg)
{
	out_be64(&per_cpu(iic, cpu).regs->generate, (IIC_NUM_IPIS - 1 - mesg) << 4);
}

static irqreturn_t iic_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	smp_message_recv(iic_irq_to_ipi(irq), regs);
	return IRQ_HANDLED;
}

static void iic_request_ipi(int ipi, const char *name)
{
	int irq;

	irq = iic_ipi_to_irq(ipi);
	/* IPIs are marked SA_INTERRUPT as they must run with irqs
	 * disabled */
	get_irq_desc(irq)->handler = &iic_pic;
	get_irq_desc(irq)->status |= IRQ_PER_CPU;
	request_irq(irq, iic_ipi_action, SA_INTERRUPT, name, NULL);
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

static void iic_setup_spe_handlers(void)
{
	int be, isrc;

	/* Assume two threads per BE are present */
	for (be=0; be < num_present_cpus() / 2; be++) {
		for (isrc = 0; isrc < IIC_CLASS_STRIDE * 3; isrc++) {
			int irq = IIC_NODE_STRIDE * be + IIC_SPE_OFFSET + isrc;
			get_irq_desc(irq)->handler = &iic_pic;
		}
	}
}

void iic_init_IRQ(void)
{
	int cpu, irq_offset;
	struct iic *iic;

	irq_offset = 0;
	for_each_cpu(cpu) {
		iic = &per_cpu(iic, cpu);
		iic->regs = find_iic(cpu);
		if (iic->regs)
			out_be64(&iic->regs->prio, 0xff);
	}
	iic_setup_spe_handlers();
}
