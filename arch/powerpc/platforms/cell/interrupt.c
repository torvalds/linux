/*
 * Cell Internal Interrupt Controller
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
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/types.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/ptrace.h>

#include "interrupt.h"
#include "cbe_regs.h"

struct iic {
	struct cbe_iic_thread_regs __iomem *regs;
	u8 target_id;
};

static DEFINE_PER_CPU(struct iic, iic);

void iic_local_enable(void)
{
	struct iic *iic = &__get_cpu_var(iic);
	u64 tmp;

	/*
	 * There seems to be a bug that is present in DD2.x CPUs
	 * and still only partially fixed in DD3.1.
	 * This bug causes a value written to the priority register
	 * not to make it there, resulting in a system hang unless we
	 * write it again.
	 * Masking with 0xf0 is done because the Cell BE does not
	 * implement the lower four bits of the interrupt priority,
	 * they always read back as zeroes, although future CPUs
	 * might implement different bits.
	 */
	do {
		out_be64(&iic->regs->prio, 0xff);
		tmp = in_be64(&iic->regs->prio);
	} while ((tmp & 0xf0) != 0xf0);
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
	.typename = " CELL-IIC ",
	.startup = iic_startup,
	.enable = iic_enable,
	.disable = iic_disable,
	.end = iic_end,
};

static int iic_external_get_irq(struct cbe_iic_pending_bits pending)
{
	int irq;
	unsigned char node, unit;

	node = pending.source >> 4;
	unit = pending.source & 0xf;
	irq = -1;

	/*
	 * This mapping is specific to the Cell Broadband
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
			+ spider_get_irq(node)
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
	struct cbe_iic_pending_bits pending;

	iic = &__get_cpu_var(iic);
	*(unsigned long *) &pending = 
		in_be64((unsigned long __iomem *) &iic->regs->pending_destr);

	irq = -1;
	if (pending.flags & CBE_IIC_IRQ_VALID) {
		if (pending.flags & CBE_IIC_IRQ_IPI) {
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

/* hardcoded part to be compatible with older firmware */

static int setup_iic_hardcoded(void)
{
	struct device_node *np;
	int nodeid, cpu;
	unsigned long regs;
	struct iic *iic;

	for_each_cpu(cpu) {
		iic = &per_cpu(iic, cpu);
		nodeid = cpu/2;

		for (np = of_find_node_by_type(NULL, "cpu");
		     np;
		     np = of_find_node_by_type(np, "cpu")) {
			if (nodeid == *(int *)get_property(np, "node-id", NULL))
				break;
			}

		if (!np) {
			printk(KERN_WARNING "IIC: CPU %d not found\n", cpu);
			iic->regs = NULL;
			iic->target_id = 0xff;
			return -ENODEV;
			}

		regs = *(long *)get_property(np, "iic", NULL);

		/* hack until we have decided on the devtree info */
		regs += 0x400;
		if (cpu & 1)
			regs += 0x20;

		printk(KERN_INFO "IIC for CPU %d at %lx\n", cpu, regs);
		iic->regs = ioremap(regs, sizeof(struct cbe_iic_thread_regs));
		iic->target_id = (nodeid << 4) + ((cpu & 1) ? 0xf : 0xe);
	}

	return 0;
}

static int setup_iic(void)
{
	struct device_node *dn;
	unsigned long *regs;
	char *compatible;
 	unsigned *np, found = 0;
	struct iic *iic = NULL;

	for (dn = NULL; (dn = of_find_node_by_name(dn, "interrupt-controller"));) {
		compatible = (char *)get_property(dn, "compatible", NULL);

		if (!compatible) {
			printk(KERN_WARNING "no compatible property found !\n");
			continue;
		}

 		if (strstr(compatible, "IBM,CBEA-Internal-Interrupt-Controller"))
 			regs = (unsigned long *)get_property(dn,"reg", NULL);
 		else
			continue;

 		if (!regs)
 			printk(KERN_WARNING "IIC: no reg property\n");

 		np = (unsigned int *)get_property(dn, "ibm,interrupt-server-ranges", NULL);

 		if (!np) {
			printk(KERN_WARNING "IIC: CPU association not found\n");
			iic->regs = NULL;
			iic->target_id = 0xff;
			return -ENODEV;
		}

 		iic = &per_cpu(iic, np[0]);
 		iic->regs = ioremap(regs[0], sizeof(struct cbe_iic_thread_regs));
		iic->target_id = ((np[0] & 2) << 3) + ((np[0] & 1) ? 0xf : 0xe);
 		printk("IIC for CPU %d at %lx mapped to %p\n", np[0], regs[0], iic->regs);

 		iic = &per_cpu(iic, np[1]);
 		iic->regs = ioremap(regs[2], sizeof(struct cbe_iic_thread_regs));
		iic->target_id = ((np[1] & 2) << 3) + ((np[1] & 1) ? 0xf : 0xe);
 		printk("IIC for CPU %d at %lx mapped to %p\n", np[1], regs[2], iic->regs);

		found++;
  	}

	if (found)
		return 0;
	else
		return -ENODEV;
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

u8 iic_get_target_id(int cpu)
{
	return per_cpu(iic, cpu).target_id;
}
EXPORT_SYMBOL_GPL(iic_get_target_id);

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

	if (setup_iic() < 0)
		setup_iic_hardcoded();

	irq_offset = 0;
	for_each_possible_cpu(cpu) {
		iic = &per_cpu(iic, cpu);
		if (iic->regs)
			out_be64(&iic->regs->prio, 0xff);
	}
	iic_setup_spe_handlers();
}
