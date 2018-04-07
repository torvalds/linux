/*
 * Copyright 2011 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/xics.h>
#include <asm/kvm_ppc.h>
#include <asm/dbell.h>

struct icp_ipl {
	union {
		u32 word;
		u8 bytes[4];
	} xirr_poll;
	union {
		u32 word;
		u8 bytes[4];
	} xirr;
	u32 dummy;
	union {
		u32 word;
		u8 bytes[4];
	} qirr;
	u32 link_a;
	u32 link_b;
	u32 link_c;
};

static struct icp_ipl __iomem *icp_native_regs[NR_CPUS];

static inline unsigned int icp_native_get_xirr(void)
{
	int cpu = smp_processor_id();
	unsigned int xirr;

	/* Handled an interrupt latched by KVM */
	xirr = kvmppc_get_xics_latch();
	if (xirr)
		return xirr;

	return in_be32(&icp_native_regs[cpu]->xirr.word);
}

static inline void icp_native_set_xirr(unsigned int value)
{
	int cpu = smp_processor_id();

	out_be32(&icp_native_regs[cpu]->xirr.word, value);
}

static inline void icp_native_set_cppr(u8 value)
{
	int cpu = smp_processor_id();

	out_8(&icp_native_regs[cpu]->xirr.bytes[0], value);
}

static inline void icp_native_set_qirr(int n_cpu, u8 value)
{
	out_8(&icp_native_regs[n_cpu]->qirr.bytes[0], value);
}

static void icp_native_set_cpu_priority(unsigned char cppr)
{
	xics_set_base_cppr(cppr);
	icp_native_set_cppr(cppr);
	iosync();
}

void icp_native_eoi(struct irq_data *d)
{
	unsigned int hw_irq = (unsigned int)irqd_to_hwirq(d);

	iosync();
	icp_native_set_xirr((xics_pop_cppr() << 24) | hw_irq);
}

static void icp_native_teardown_cpu(void)
{
	int cpu = smp_processor_id();

	/* Clear any pending IPI */
	icp_native_set_qirr(cpu, 0xff);
}

static void icp_native_flush_ipi(void)
{
	/* We take the ipi irq but and never return so we
	 * need to EOI the IPI, but want to leave our priority 0
	 *
	 * should we check all the other interrupts too?
	 * should we be flagging idle loop instead?
	 * or creating some task to be scheduled?
	 */

	icp_native_set_xirr((0x00 << 24) | XICS_IPI);
}

static unsigned int icp_native_get_irq(void)
{
	unsigned int xirr = icp_native_get_xirr();
	unsigned int vec = xirr & 0x00ffffff;
	unsigned int irq;

	if (vec == XICS_IRQ_SPURIOUS)
		return 0;

	irq = irq_find_mapping(xics_host, vec);
	if (likely(irq)) {
		xics_push_cppr(vec);
		return irq;
	}

	/* We don't have a linux mapping, so have rtas mask it. */
	xics_mask_unknown_vec(vec);

	/* We might learn about it later, so EOI it */
	icp_native_set_xirr(xirr);

	return 0;
}

#ifdef CONFIG_SMP

static void icp_native_cause_ipi(int cpu)
{
	kvmppc_set_host_ipi(cpu, 1);
	icp_native_set_qirr(cpu, IPI_PRIORITY);
}

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
void icp_native_cause_ipi_rm(int cpu)
{
	/*
	 * Currently not used to send IPIs to another CPU
	 * on the same core. Only caller is KVM real mode.
	 * Need the physical address of the XICS to be
	 * previously saved in kvm_hstate in the paca.
	 */
	void __iomem *xics_phys;

	/*
	 * Just like the cause_ipi functions, it is required to
	 * include a full barrier before causing the IPI.
	 */
	xics_phys = paca_ptrs[cpu]->kvm_hstate.xics_phys;
	mb();
	__raw_rm_writeb(IPI_PRIORITY, xics_phys + XICS_MFRR);
}
#endif

/*
 * Called when an interrupt is received on an off-line CPU to
 * clear the interrupt, so that the CPU can go back to nap mode.
 */
void icp_native_flush_interrupt(void)
{
	unsigned int xirr = icp_native_get_xirr();
	unsigned int vec = xirr & 0x00ffffff;

	if (vec == XICS_IRQ_SPURIOUS)
		return;
	if (vec == XICS_IPI) {
		/* Clear pending IPI */
		int cpu = smp_processor_id();
		kvmppc_set_host_ipi(cpu, 0);
		icp_native_set_qirr(cpu, 0xff);
	} else {
		pr_err("XICS: hw interrupt 0x%x to offline cpu, disabling\n",
		       vec);
		xics_mask_unknown_vec(vec);
	}
	/* EOI the interrupt */
	icp_native_set_xirr(xirr);
}

void xics_wake_cpu(int cpu)
{
	icp_native_set_qirr(cpu, IPI_PRIORITY);
}
EXPORT_SYMBOL_GPL(xics_wake_cpu);

static irqreturn_t icp_native_ipi_action(int irq, void *dev_id)
{
	int cpu = smp_processor_id();

	kvmppc_set_host_ipi(cpu, 0);
	icp_native_set_qirr(cpu, 0xff);

	return smp_ipi_demux();
}

#endif /* CONFIG_SMP */

static int __init icp_native_map_one_cpu(int hw_id, unsigned long addr,
					 unsigned long size)
{
	char *rname;
	int i, cpu = -1;

	/* This may look gross but it's good enough for now, we don't quite
	 * have a hard -> linux processor id matching.
	 */
	for_each_possible_cpu(i) {
		if (!cpu_present(i))
			continue;
		if (hw_id == get_hard_smp_processor_id(i)) {
			cpu = i;
			break;
		}
	}

	/* Fail, skip that CPU. Don't print, it's normal, some XICS come up
	 * with way more entries in there than you have CPUs
	 */
	if (cpu == -1)
		return 0;

	rname = kasprintf(GFP_KERNEL, "CPU %d [0x%x] Interrupt Presentation",
			  cpu, hw_id);

	if (!request_mem_region(addr, size, rname)) {
		pr_warn("icp_native: Could not reserve ICP MMIO for CPU %d, interrupt server #0x%x\n",
			cpu, hw_id);
		return -EBUSY;
	}

	icp_native_regs[cpu] = ioremap(addr, size);
	kvmppc_set_xics_phys(cpu, addr);
	if (!icp_native_regs[cpu]) {
		pr_warn("icp_native: Failed ioremap for CPU %d, interrupt server #0x%x, addr %#lx\n",
			cpu, hw_id, addr);
		release_mem_region(addr, size);
		return -ENOMEM;
	}
	return 0;
}

static int __init icp_native_init_one_node(struct device_node *np,
					   unsigned int *indx)
{
	unsigned int ilen;
	const __be32 *ireg;
	int i;
	int reg_tuple_size;
	int num_servers = 0;

	/* This code does the theorically broken assumption that the interrupt
	 * server numbers are the same as the hard CPU numbers.
	 * This happens to be the case so far but we are playing with fire...
	 * should be fixed one of these days. -BenH.
	 */
	ireg = of_get_property(np, "ibm,interrupt-server-ranges", &ilen);

	/* Do that ever happen ? we'll know soon enough... but even good'old
	 * f80 does have that property ..
	 */
	WARN_ON((ireg == NULL) || (ilen != 2*sizeof(u32)));

	if (ireg) {
		*indx = of_read_number(ireg, 1);
		if (ilen >= 2*sizeof(u32))
			num_servers = of_read_number(ireg + 1, 1);
	}

	ireg = of_get_property(np, "reg", &ilen);
	if (!ireg) {
		pr_err("icp_native: Can't find interrupt reg property");
		return -1;
	}

	reg_tuple_size = (of_n_addr_cells(np) + of_n_size_cells(np)) * 4;
	if (((ilen % reg_tuple_size) != 0)
	    || (num_servers && (num_servers != (ilen / reg_tuple_size)))) {
		pr_err("icp_native: ICP reg len (%d) != num servers (%d)",
		       ilen / reg_tuple_size, num_servers);
		return -1;
	}

	for (i = 0; i < (ilen / reg_tuple_size); i++) {
		struct resource r;
		int err;

		err = of_address_to_resource(np, i, &r);
		if (err) {
			pr_err("icp_native: Could not translate ICP MMIO"
			       " for interrupt server 0x%x (%d)\n", *indx, err);
			return -1;
		}

		if (icp_native_map_one_cpu(*indx, r.start, resource_size(&r)))
			return -1;

		(*indx)++;
	}
	return 0;
}

static const struct icp_ops icp_native_ops = {
	.get_irq	= icp_native_get_irq,
	.eoi		= icp_native_eoi,
	.set_priority	= icp_native_set_cpu_priority,
	.teardown_cpu	= icp_native_teardown_cpu,
	.flush_ipi	= icp_native_flush_ipi,
#ifdef CONFIG_SMP
	.ipi_action	= icp_native_ipi_action,
	.cause_ipi	= icp_native_cause_ipi,
#endif
};

int __init icp_native_init(void)
{
	struct device_node *np;
	u32 indx = 0;
	int found = 0;

	for_each_compatible_node(np, NULL, "ibm,ppc-xicp")
		if (icp_native_init_one_node(np, &indx) == 0)
			found = 1;
	if (!found) {
		for_each_node_by_type(np,
			"PowerPC-External-Interrupt-Presentation") {
				if (icp_native_init_one_node(np, &indx) == 0)
					found = 1;
		}
	}

	if (found == 0)
		return -ENODEV;

	icp_ops = &icp_native_ops;

	return 0;
}
