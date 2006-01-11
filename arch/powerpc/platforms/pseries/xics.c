/*
 * arch/powerpc/platforms/pseries/xics.c
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/gfp.h>
#include <linux/radix-tree.h>
#include <linux/cpu.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/rtas.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/i8259.h>

#include "xics.h"

static unsigned int xics_startup(unsigned int irq);
static void xics_enable_irq(unsigned int irq);
static void xics_disable_irq(unsigned int irq);
static void xics_mask_and_ack_irq(unsigned int irq);
static void xics_end_irq(unsigned int irq);
static void xics_set_affinity(unsigned int irq_nr, cpumask_t cpumask);

static struct hw_interrupt_type xics_pic = {
	.typename = " XICS     ",
	.startup = xics_startup,
	.enable = xics_enable_irq,
	.disable = xics_disable_irq,
	.ack = xics_mask_and_ack_irq,
	.end = xics_end_irq,
	.set_affinity = xics_set_affinity
};

/* This is used to map real irq numbers to virtual */
static struct radix_tree_root irq_map = RADIX_TREE_INIT(GFP_ATOMIC);

#define XICS_IPI		2
#define XICS_IRQ_SPURIOUS	0

/* Want a priority other than 0.  Various HW issues require this. */
#define	DEFAULT_PRIORITY	5

/*
 * Mark IPIs as higher priority so we can take them inside interrupts that
 * arent marked SA_INTERRUPT
 */
#define IPI_PRIORITY		4

struct xics_ipl {
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
};

static struct xics_ipl __iomem *xics_per_cpu[NR_CPUS];

static int xics_irq_8259_cascade = 0;
static int xics_irq_8259_cascade_real = 0;
static unsigned int default_server = 0xFF;
static unsigned int default_distrib_server = 0;
static unsigned int interrupt_server_size = 8;

/*
 * XICS only has a single IPI, so encode the messages per CPU
 */
struct xics_ipi_struct xics_ipi_message[NR_CPUS] __cacheline_aligned;

/* RTAS service tokens */
static int ibm_get_xive;
static int ibm_set_xive;
static int ibm_int_on;
static int ibm_int_off;

typedef struct {
	int (*xirr_info_get)(int cpu);
	void (*xirr_info_set)(int cpu, int val);
	void (*cppr_info)(int cpu, u8 val);
	void (*qirr_info)(int cpu, u8 val);
} xics_ops;


/* SMP */

static int pSeries_xirr_info_get(int n_cpu)
{
	return in_be32(&xics_per_cpu[n_cpu]->xirr.word);
}

static void pSeries_xirr_info_set(int n_cpu, int value)
{
	out_be32(&xics_per_cpu[n_cpu]->xirr.word, value);
}

static void pSeries_cppr_info(int n_cpu, u8 value)
{
	out_8(&xics_per_cpu[n_cpu]->xirr.bytes[0], value);
}

static void pSeries_qirr_info(int n_cpu, u8 value)
{
	out_8(&xics_per_cpu[n_cpu]->qirr.bytes[0], value);
}

static xics_ops pSeries_ops = {
	pSeries_xirr_info_get,
	pSeries_xirr_info_set,
	pSeries_cppr_info,
	pSeries_qirr_info
};

static xics_ops *ops = &pSeries_ops;


/* LPAR */

static inline long plpar_eoi(unsigned long xirr)
{
	return plpar_hcall_norets(H_EOI, xirr);
}

static inline long plpar_cppr(unsigned long cppr)
{
	return plpar_hcall_norets(H_CPPR, cppr);
}

static inline long plpar_ipi(unsigned long servernum, unsigned long mfrr)
{
	return plpar_hcall_norets(H_IPI, servernum, mfrr);
}

static inline long plpar_xirr(unsigned long *xirr_ret)
{
	unsigned long dummy;
	return plpar_hcall(H_XIRR, 0, 0, 0, 0, xirr_ret, &dummy, &dummy);
}

static int pSeriesLP_xirr_info_get(int n_cpu)
{
	unsigned long lpar_rc;
	unsigned long return_value;

	lpar_rc = plpar_xirr(&return_value);
	if (lpar_rc != H_Success)
		panic(" bad return code xirr - rc = %lx \n", lpar_rc);
	return (int)return_value;
}

static void pSeriesLP_xirr_info_set(int n_cpu, int value)
{
	unsigned long lpar_rc;
	unsigned long val64 = value & 0xffffffff;

	lpar_rc = plpar_eoi(val64);
	if (lpar_rc != H_Success)
		panic("bad return code EOI - rc = %ld, value=%lx\n", lpar_rc,
		      val64);
}

void pSeriesLP_cppr_info(int n_cpu, u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_cppr(value);
	if (lpar_rc != H_Success)
		panic("bad return code cppr - rc = %lx\n", lpar_rc);
}

static void pSeriesLP_qirr_info(int n_cpu , u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_ipi(get_hard_smp_processor_id(n_cpu), value);
	if (lpar_rc != H_Success)
		panic("bad return code qirr - rc = %lx\n", lpar_rc);
}

xics_ops pSeriesLP_ops = {
	pSeriesLP_xirr_info_get,
	pSeriesLP_xirr_info_set,
	pSeriesLP_cppr_info,
	pSeriesLP_qirr_info
};

static unsigned int xics_startup(unsigned int virq)
{
	unsigned int irq;

	irq = irq_offset_down(virq);
	if (radix_tree_insert(&irq_map, virt_irq_to_real(irq),
			      &virt_irq_to_real_map[irq]) == -ENOMEM)
		printk(KERN_CRIT "Out of memory creating real -> virtual"
		       " IRQ mapping for irq %u (real 0x%x)\n",
		       virq, virt_irq_to_real(irq));
	xics_enable_irq(virq);
	return 0;	/* return value is ignored */
}

static unsigned int real_irq_to_virt(unsigned int real_irq)
{
	unsigned int *ptr;

	ptr = radix_tree_lookup(&irq_map, real_irq);
	if (ptr == NULL)
		return NO_IRQ;
	return ptr - virt_irq_to_real_map;
}

#ifdef CONFIG_SMP
static int get_irq_server(unsigned int irq)
{
	unsigned int server;
	/* For the moment only implement delivery to all cpus or one cpu */
	cpumask_t cpumask = irq_affinity[irq];
	cpumask_t tmp = CPU_MASK_NONE;

	if (!distribute_irqs)
		return default_server;

	if (cpus_equal(cpumask, CPU_MASK_ALL)) {
		server = default_distrib_server;
	} else {
		cpus_and(tmp, cpu_online_map, cpumask);

		if (cpus_empty(tmp))
			server = default_distrib_server;
		else
			server = get_hard_smp_processor_id(first_cpu(tmp));
	}

	return server;

}
#else
static int get_irq_server(unsigned int irq)
{
	return default_server;
}
#endif

static void xics_enable_irq(unsigned int virq)
{
	unsigned int irq;
	int call_status;
	unsigned int server;

	irq = virt_irq_to_real(irq_offset_down(virq));
	if (irq == XICS_IPI)
		return;

	server = get_irq_server(virq);
	call_status = rtas_call(ibm_set_xive, 3, 1, NULL, irq, server,
				DEFAULT_PRIORITY);
	if (call_status != 0) {
		printk(KERN_ERR "xics_enable_irq: irq=%u: ibm_set_xive "
		       "returned %d\n", irq, call_status);
		printk("set_xive %x, server %x\n", ibm_set_xive, server);
		return;
	}

	/* Now unmask the interrupt (often a no-op) */
	call_status = rtas_call(ibm_int_on, 1, 1, NULL, irq);
	if (call_status != 0) {
		printk(KERN_ERR "xics_enable_irq: irq=%u: ibm_int_on "
		       "returned %d\n", irq, call_status);
		return;
	}
}

static void xics_disable_real_irq(unsigned int irq)
{
	int call_status;
	unsigned int server;

	if (irq == XICS_IPI)
		return;

	call_status = rtas_call(ibm_int_off, 1, 1, NULL, irq);
	if (call_status != 0) {
		printk(KERN_ERR "xics_disable_real_irq: irq=%u: "
		       "ibm_int_off returned %d\n", irq, call_status);
		return;
	}

	server = get_irq_server(irq);
	/* Have to set XIVE to 0xff to be able to remove a slot */
	call_status = rtas_call(ibm_set_xive, 3, 1, NULL, irq, server, 0xff);
	if (call_status != 0) {
		printk(KERN_ERR "xics_disable_irq: irq=%u: ibm_set_xive(0xff)"
		       " returned %d\n", irq, call_status);
		return;
	}
}

static void xics_disable_irq(unsigned int virq)
{
	unsigned int irq;

	irq = virt_irq_to_real(irq_offset_down(virq));
	xics_disable_real_irq(irq);
}

static void xics_end_irq(unsigned int irq)
{
	int cpu = smp_processor_id();

	iosync();
	ops->xirr_info_set(cpu, ((0xff << 24) |
				 (virt_irq_to_real(irq_offset_down(irq)))));

}

static void xics_mask_and_ack_irq(unsigned int irq)
{
	int cpu = smp_processor_id();

	if (irq < irq_offset_value()) {
		i8259_pic.ack(irq);
		iosync();
		ops->xirr_info_set(cpu, ((0xff<<24) |
					 xics_irq_8259_cascade_real));
		iosync();
	}
}

int xics_get_irq(struct pt_regs *regs)
{
	unsigned int cpu = smp_processor_id();
	unsigned int vec;
	int irq;

	vec = ops->xirr_info_get(cpu);
	/*  (vec >> 24) == old priority */
	vec &= 0x00ffffff;

	/* for sanity, this had better be < NR_IRQS - 16 */
	if (vec == xics_irq_8259_cascade_real) {
		irq = i8259_irq(regs);
		xics_end_irq(irq_offset_up(xics_irq_8259_cascade));
	} else if (vec == XICS_IRQ_SPURIOUS) {
		irq = -1;
	} else {
		irq = real_irq_to_virt(vec);
		if (irq == NO_IRQ)
			irq = real_irq_to_virt_slowpath(vec);
		if (irq == NO_IRQ) {
			printk(KERN_ERR "Interrupt %u (real) is invalid,"
			       " disabling it.\n", vec);
			xics_disable_real_irq(vec);
		} else
			irq = irq_offset_up(irq);
	}
	return irq;
}

#ifdef CONFIG_SMP

static irqreturn_t xics_ipi_action(int irq, void *dev_id, struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	ops->qirr_info(cpu, 0xff);

	WARN_ON(cpu_is_offline(cpu));

	while (xics_ipi_message[cpu].value) {
		if (test_and_clear_bit(PPC_MSG_CALL_FUNCTION,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_CALL_FUNCTION, regs);
		}
		if (test_and_clear_bit(PPC_MSG_RESCHEDULE,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_RESCHEDULE, regs);
		}
#if 0
		if (test_and_clear_bit(PPC_MSG_MIGRATE_TASK,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_MIGRATE_TASK, regs);
		}
#endif
#if defined(CONFIG_DEBUGGER) || defined(CONFIG_KEXEC)
		if (test_and_clear_bit(PPC_MSG_DEBUGGER_BREAK,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_DEBUGGER_BREAK, regs);
		}
#endif
	}
	return IRQ_HANDLED;
}

void xics_cause_IPI(int cpu)
{
	ops->qirr_info(cpu, IPI_PRIORITY);
}
#endif /* CONFIG_SMP */

void xics_setup_cpu(void)
{
	int cpu = smp_processor_id();

	ops->cppr_info(cpu, 0xff);
	iosync();

	/*
	 * Put the calling processor into the GIQ.  This is really only
	 * necessary from a secondary thread as the OF start-cpu interface
	 * performs this function for us on primary threads.
	 *
	 * XXX: undo of teardown on kexec needs this too, as may hotplug
	 */
	rtas_set_indicator(GLOBAL_INTERRUPT_QUEUE,
		(1UL << interrupt_server_size) - 1 - default_distrib_server, 1);
}

void xics_init_IRQ(void)
{
	int i;
	unsigned long intr_size = 0;
	struct device_node *np;
	uint *ireg, ilen, indx = 0;
	unsigned long intr_base = 0;
	struct xics_interrupt_node {
		unsigned long addr;
		unsigned long size;
	} intnodes[NR_CPUS];

	ppc64_boot_msg(0x20, "XICS Init");

	ibm_get_xive = rtas_token("ibm,get-xive");
	ibm_set_xive = rtas_token("ibm,set-xive");
	ibm_int_on  = rtas_token("ibm,int-on");
	ibm_int_off = rtas_token("ibm,int-off");

	np = of_find_node_by_type(NULL, "PowerPC-External-Interrupt-Presentation");
	if (!np)
		panic("xics_init_IRQ: can't find interrupt presentation");

nextnode:
	ireg = (uint *)get_property(np, "ibm,interrupt-server-ranges", NULL);
	if (ireg) {
		/*
		 * set node starting index for this node
		 */
		indx = *ireg;
	}

	ireg = (uint *)get_property(np, "reg", &ilen);
	if (!ireg)
		panic("xics_init_IRQ: can't find interrupt reg property");

	while (ilen) {
		intnodes[indx].addr = (unsigned long)*ireg++ << 32;
		ilen -= sizeof(uint);
		intnodes[indx].addr |= *ireg++;
		ilen -= sizeof(uint);
		intnodes[indx].size = (unsigned long)*ireg++ << 32;
		ilen -= sizeof(uint);
		intnodes[indx].size |= *ireg++;
		ilen -= sizeof(uint);
		indx++;
		if (indx >= NR_CPUS) break;
	}

	np = of_find_node_by_type(np, "PowerPC-External-Interrupt-Presentation");
	if ((indx < NR_CPUS) && np) goto nextnode;

	/* Find the server numbers for the boot cpu. */
	for (np = of_find_node_by_type(NULL, "cpu");
	     np;
	     np = of_find_node_by_type(np, "cpu")) {
		ireg = (uint *)get_property(np, "reg", &ilen);
		if (ireg && ireg[0] == boot_cpuid_phys) {
			ireg = (uint *)get_property(np, "ibm,ppc-interrupt-gserver#s",
						    &ilen);
			i = ilen / sizeof(int);
			if (ireg && i > 0) {
				default_server = ireg[0];
				default_distrib_server = ireg[i-1]; /* take last element */
			}
			ireg = (uint *)get_property(np,
					"ibm,interrupt-server#-size", NULL);
			if (ireg)
				interrupt_server_size = *ireg;
			break;
		}
	}
	of_node_put(np);

	intr_base = intnodes[0].addr;
	intr_size = intnodes[0].size;

	np = of_find_node_by_type(NULL, "interrupt-controller");
	if (!np) {
		printk(KERN_WARNING "xics: no ISA interrupt controller\n");
		xics_irq_8259_cascade_real = -1;
		xics_irq_8259_cascade = -1;
	} else {
		ireg = (uint *) get_property(np, "interrupts", NULL);
		if (!ireg)
			panic("xics_init_IRQ: can't find ISA interrupts property");

		xics_irq_8259_cascade_real = *ireg;
		xics_irq_8259_cascade
			= virt_irq_create_mapping(xics_irq_8259_cascade_real);
		i8259_init(0, 0);
		of_node_put(np);
	}

	if (platform_is_lpar())
		ops = &pSeriesLP_ops;
	else {
#ifdef CONFIG_SMP
		for_each_cpu(i) {
			int hard_id;

			/* FIXME: Do this dynamically! --RR */
			if (!cpu_present(i))
				continue;

			hard_id = get_hard_smp_processor_id(i);
			xics_per_cpu[i] = ioremap(intnodes[hard_id].addr,
						  intnodes[hard_id].size);
		}
#else
		xics_per_cpu[0] = ioremap(intr_base, intr_size);
#endif /* CONFIG_SMP */
	}

	for (i = irq_offset_value(); i < NR_IRQS; ++i)
		get_irq_desc(i)->handler = &xics_pic;

	xics_setup_cpu();

	ppc64_boot_msg(0x21, "XICS Done");
}

/*
 * We cant do this in init_IRQ because we need the memory subsystem up for
 * request_irq()
 */
static int __init xics_setup_i8259(void)
{
	if (ppc64_interrupt_controller == IC_PPC_XIC &&
	    xics_irq_8259_cascade != -1) {
		if (request_irq(irq_offset_up(xics_irq_8259_cascade),
				no_action, 0, "8259 cascade", NULL))
			printk(KERN_ERR "xics_setup_i8259: couldn't get 8259 "
					"cascade\n");
	}
	return 0;
}
arch_initcall(xics_setup_i8259);

#ifdef CONFIG_SMP
void xics_request_IPIs(void)
{
	virt_irq_to_real_map[XICS_IPI] = XICS_IPI;

	/* IPIs are marked SA_INTERRUPT as they must run with irqs disabled */
	request_irq(irq_offset_up(XICS_IPI), xics_ipi_action, SA_INTERRUPT,
		    "IPI", NULL);
	get_irq_desc(irq_offset_up(XICS_IPI))->status |= IRQ_PER_CPU;
}
#endif

static void xics_set_affinity(unsigned int virq, cpumask_t cpumask)
{
	unsigned int irq;
	int status;
	int xics_status[2];
	unsigned long newmask;
	cpumask_t tmp = CPU_MASK_NONE;

	irq = virt_irq_to_real(irq_offset_down(virq));
	if (irq == XICS_IPI || irq == NO_IRQ)
		return;

	status = rtas_call(ibm_get_xive, 1, 3, xics_status, irq);

	if (status) {
		printk(KERN_ERR "xics_set_affinity: irq=%u ibm,get-xive "
		       "returns %d\n", irq, status);
		return;
	}

	/* For the moment only implement delivery to all cpus or one cpu */
	if (cpus_equal(cpumask, CPU_MASK_ALL)) {
		newmask = default_distrib_server;
	} else {
		cpus_and(tmp, cpu_online_map, cpumask);
		if (cpus_empty(tmp))
			return;
		newmask = get_hard_smp_processor_id(first_cpu(tmp));
	}

	status = rtas_call(ibm_set_xive, 3, 1, NULL,
				irq, newmask, xics_status[1]);

	if (status) {
		printk(KERN_ERR "xics_set_affinity: irq=%u ibm,set-xive "
		       "returns %d\n", irq, status);
		return;
	}
}

void xics_teardown_cpu(int secondary)
{
	int cpu = smp_processor_id();

	ops->cppr_info(cpu, 0x00);
	iosync();

	/*
	 * Some machines need to have at least one cpu in the GIQ,
	 * so leave the master cpu in the group.
	 */
	if (secondary) {
		/*
		 * we need to EOI the IPI if we got here from kexec down IPI
		 *
		 * probably need to check all the other interrupts too
		 * should we be flagging idle loop instead?
		 * or creating some task to be scheduled?
		 */
		ops->xirr_info_set(cpu, XICS_IPI);
		rtas_set_indicator(GLOBAL_INTERRUPT_QUEUE,
			(1UL << interrupt_server_size) - 1 -
			default_distrib_server, 0);
	}
}

#ifdef CONFIG_HOTPLUG_CPU

/* Interrupts are disabled. */
void xics_migrate_irqs_away(void)
{
	int status;
	unsigned int irq, virq, cpu = smp_processor_id();

	/* Reject any interrupt that was queued to us... */
	ops->cppr_info(cpu, 0);
	iosync();

	/* remove ourselves from the global interrupt queue */
	status = rtas_set_indicator(GLOBAL_INTERRUPT_QUEUE,
		(1UL << interrupt_server_size) - 1 - default_distrib_server, 0);
	WARN_ON(status < 0);

	/* Allow IPIs again... */
	ops->cppr_info(cpu, DEFAULT_PRIORITY);
	iosync();

	for_each_irq(virq) {
		irq_desc_t *desc;
		int xics_status[2];
		unsigned long flags;

		/* We cant set affinity on ISA interrupts */
		if (virq < irq_offset_value())
			continue;

		desc = get_irq_desc(virq);
		irq = virt_irq_to_real(irq_offset_down(virq));

		/* We need to get IPIs still. */
		if (irq == XICS_IPI || irq == NO_IRQ)
			continue;

		/* We only need to migrate enabled IRQS */
		if (desc == NULL || desc->handler == NULL
		    || desc->action == NULL
		    || desc->handler->set_affinity == NULL)
			continue;

		spin_lock_irqsave(&desc->lock, flags);

		status = rtas_call(ibm_get_xive, 1, 3, xics_status, irq);
		if (status) {
			printk(KERN_ERR "migrate_irqs_away: irq=%u "
					"ibm,get-xive returns %d\n",
					virq, status);
			goto unlock;
		}

		/*
		 * We only support delivery to all cpus or to one cpu.
		 * The irq has to be migrated only in the single cpu
		 * case.
		 */
		if (xics_status[0] != get_hard_smp_processor_id(cpu))
			goto unlock;

		printk(KERN_WARNING "IRQ %u affinity broken off cpu %u\n",
		       virq, cpu);

		/* Reset affinity to all cpus */
		desc->handler->set_affinity(virq, CPU_MASK_ALL);
		irq_affinity[virq] = CPU_MASK_ALL;
unlock:
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}
#endif
