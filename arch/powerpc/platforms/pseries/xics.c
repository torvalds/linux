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

#undef DEBUG

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

#include <asm/firmware.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/rtas.h>
#include <asm/hvcall.h>
#include <asm/machdep.h>
#include <asm/i8259.h>

#include "xics.h"
#include "plpar_wrappers.h"

#define XICS_IPI		2
#define XICS_IRQ_SPURIOUS	0

/* Want a priority other than 0.  Various HW issues require this. */
#define	DEFAULT_PRIORITY	5

/*
 * Mark IPIs as higher priority so we can take them inside interrupts that
 * arent marked IRQF_DISABLED
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

static unsigned int default_server = 0xFF;
static unsigned int default_distrib_server = 0;
static unsigned int interrupt_server_size = 8;

static struct irq_host *xics_host;

/*
 * XICS only has a single IPI, so encode the messages per CPU
 */
struct xics_ipi_struct xics_ipi_message[NR_CPUS] __cacheline_aligned;

/* RTAS service tokens */
static int ibm_get_xive;
static int ibm_set_xive;
static int ibm_int_on;
static int ibm_int_off;


/* Direct HW low level accessors */


static inline unsigned int direct_xirr_info_get(void)
{
	int cpu = smp_processor_id();

	return in_be32(&xics_per_cpu[cpu]->xirr.word);
}

static inline void direct_xirr_info_set(int value)
{
	int cpu = smp_processor_id();

	out_be32(&xics_per_cpu[cpu]->xirr.word, value);
}

static inline void direct_cppr_info(u8 value)
{
	int cpu = smp_processor_id();

	out_8(&xics_per_cpu[cpu]->xirr.bytes[0], value);
}

static inline void direct_qirr_info(int n_cpu, u8 value)
{
	out_8(&xics_per_cpu[n_cpu]->qirr.bytes[0], value);
}


/* LPAR low level accessors */


static inline unsigned int lpar_xirr_info_get(void)
{
	unsigned long lpar_rc;
	unsigned long return_value;

	lpar_rc = plpar_xirr(&return_value);
	if (lpar_rc != H_SUCCESS)
		panic(" bad return code xirr - rc = %lx \n", lpar_rc);
	return (unsigned int)return_value;
}

static inline void lpar_xirr_info_set(int value)
{
	unsigned long lpar_rc;
	unsigned long val64 = value & 0xffffffff;

	lpar_rc = plpar_eoi(val64);
	if (lpar_rc != H_SUCCESS)
		panic("bad return code EOI - rc = %ld, value=%lx\n", lpar_rc,
		      val64);
}

static inline void lpar_cppr_info(u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_cppr(value);
	if (lpar_rc != H_SUCCESS)
		panic("bad return code cppr - rc = %lx\n", lpar_rc);
}

static inline void lpar_qirr_info(int n_cpu , u8 value)
{
	unsigned long lpar_rc;

	lpar_rc = plpar_ipi(get_hard_smp_processor_id(n_cpu), value);
	if (lpar_rc != H_SUCCESS)
		panic("bad return code qirr - rc = %lx\n", lpar_rc);
}


/* High level handlers and init code */


#ifdef CONFIG_SMP
static int get_irq_server(unsigned int virq, unsigned int strict_check)
{
	int server;
	/* For the moment only implement delivery to all cpus or one cpu */
	cpumask_t cpumask = irq_desc[virq].affinity;
	cpumask_t tmp = CPU_MASK_NONE;

	if (!distribute_irqs)
		return default_server;

	if (!cpus_equal(cpumask, CPU_MASK_ALL)) {
		cpus_and(tmp, cpu_online_map, cpumask);

		server = first_cpu(tmp);

		if (server < NR_CPUS)
			return get_hard_smp_processor_id(server);

		if (strict_check)
			return -1;
	}

	if (cpus_equal(cpu_online_map, cpu_present_map))
		return default_distrib_server;

	return default_server;
}
#else
static int get_irq_server(unsigned int virq, unsigned int strict_check)
{
	return default_server;
}
#endif


static void xics_unmask_irq(unsigned int virq)
{
	unsigned int irq;
	int call_status;
	int server;

	pr_debug("xics: unmask virq %d\n", virq);

	irq = (unsigned int)irq_map[virq].hwirq;
	pr_debug(" -> map to hwirq 0x%x\n", irq);
	if (irq == XICS_IPI || irq == XICS_IRQ_SPURIOUS)
		return;

	server = get_irq_server(virq, 0);

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

static void xics_mask_real_irq(unsigned int irq)
{
	int call_status;

	if (irq == XICS_IPI)
		return;

	call_status = rtas_call(ibm_int_off, 1, 1, NULL, irq);
	if (call_status != 0) {
		printk(KERN_ERR "xics_disable_real_irq: irq=%u: "
		       "ibm_int_off returned %d\n", irq, call_status);
		return;
	}

	/* Have to set XIVE to 0xff to be able to remove a slot */
	call_status = rtas_call(ibm_set_xive, 3, 1, NULL, irq,
				default_server, 0xff);
	if (call_status != 0) {
		printk(KERN_ERR "xics_disable_irq: irq=%u: ibm_set_xive(0xff)"
		       " returned %d\n", irq, call_status);
		return;
	}
}

static void xics_mask_irq(unsigned int virq)
{
	unsigned int irq;

	pr_debug("xics: mask virq %d\n", virq);

	irq = (unsigned int)irq_map[virq].hwirq;
	if (irq == XICS_IPI || irq == XICS_IRQ_SPURIOUS)
		return;
	xics_mask_real_irq(irq);
}

static unsigned int xics_startup(unsigned int virq)
{
	unsigned int irq;

	/* force a reverse mapping of the interrupt so it gets in the cache */
	irq = (unsigned int)irq_map[virq].hwirq;
	irq_radix_revmap(xics_host, irq);

	/* unmask it */
	xics_unmask_irq(virq);
	return 0;
}

static void xics_eoi_direct(unsigned int virq)
{
	unsigned int irq = (unsigned int)irq_map[virq].hwirq;

	iosync();
	direct_xirr_info_set((0xff << 24) | irq);
}


static void xics_eoi_lpar(unsigned int virq)
{
	unsigned int irq = (unsigned int)irq_map[virq].hwirq;

	iosync();
	lpar_xirr_info_set((0xff << 24) | irq);
}

static inline unsigned int xics_remap_irq(unsigned int vec)
{
	unsigned int irq;

	vec &= 0x00ffffff;

	if (vec == XICS_IRQ_SPURIOUS)
		return NO_IRQ;
	irq = irq_radix_revmap(xics_host, vec);
	if (likely(irq != NO_IRQ))
		return irq;

	printk(KERN_ERR "Interrupt %u (real) is invalid,"
	       " disabling it.\n", vec);
	xics_mask_real_irq(vec);
	return NO_IRQ;
}

static unsigned int xics_get_irq_direct(void)
{
	return xics_remap_irq(direct_xirr_info_get());
}

static unsigned int xics_get_irq_lpar(void)
{
	return xics_remap_irq(lpar_xirr_info_get());
}

#ifdef CONFIG_SMP

static irqreturn_t xics_ipi_dispatch(int cpu)
{
	WARN_ON(cpu_is_offline(cpu));

	while (xics_ipi_message[cpu].value) {
		if (test_and_clear_bit(PPC_MSG_CALL_FUNCTION,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_CALL_FUNCTION);
		}
		if (test_and_clear_bit(PPC_MSG_RESCHEDULE,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_RESCHEDULE);
		}
#if 0
		if (test_and_clear_bit(PPC_MSG_MIGRATE_TASK,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_MIGRATE_TASK);
		}
#endif
#if defined(CONFIG_DEBUGGER) || defined(CONFIG_KEXEC)
		if (test_and_clear_bit(PPC_MSG_DEBUGGER_BREAK,
				       &xics_ipi_message[cpu].value)) {
			mb();
			smp_message_recv(PPC_MSG_DEBUGGER_BREAK);
		}
#endif
	}
	return IRQ_HANDLED;
}

static irqreturn_t xics_ipi_action_direct(int irq, void *dev_id)
{
	int cpu = smp_processor_id();

	direct_qirr_info(cpu, 0xff);

	return xics_ipi_dispatch(cpu);
}

static irqreturn_t xics_ipi_action_lpar(int irq, void *dev_id)
{
	int cpu = smp_processor_id();

	lpar_qirr_info(cpu, 0xff);

	return xics_ipi_dispatch(cpu);
}

void xics_cause_IPI(int cpu)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		lpar_qirr_info(cpu, IPI_PRIORITY);
	else
		direct_qirr_info(cpu, IPI_PRIORITY);
}

#endif /* CONFIG_SMP */

static void xics_set_cpu_priority(unsigned char cppr)
{
	if (firmware_has_feature(FW_FEATURE_LPAR))
		lpar_cppr_info(cppr);
	else
		direct_cppr_info(cppr);
	iosync();
}

static void xics_set_affinity(unsigned int virq, cpumask_t cpumask)
{
	unsigned int irq;
	int status;
	int xics_status[2];
	int irq_server;

	irq = (unsigned int)irq_map[virq].hwirq;
	if (irq == XICS_IPI || irq == XICS_IRQ_SPURIOUS)
		return;

	status = rtas_call(ibm_get_xive, 1, 3, xics_status, irq);

	if (status) {
		printk(KERN_ERR "xics_set_affinity: irq=%u ibm,get-xive "
		       "returns %d\n", irq, status);
		return;
	}

	/*
	 * For the moment only implement delivery to all cpus or one cpu.
	 * Get current irq_server for the given irq
	 */
	irq_server = get_irq_server(virq, 1);
	if (irq_server == -1) {
		char cpulist[128];
		cpumask_scnprintf(cpulist, sizeof(cpulist), cpumask);
		printk(KERN_WARNING "xics_set_affinity: No online cpus in "
				"the mask %s for irq %d\n", cpulist, virq);
		return;
	}

	status = rtas_call(ibm_set_xive, 3, 1, NULL,
				irq, irq_server, xics_status[1]);

	if (status) {
		printk(KERN_ERR "xics_set_affinity: irq=%u ibm,set-xive "
		       "returns %d\n", irq, status);
		return;
	}
}

void xics_setup_cpu(void)
{
	xics_set_cpu_priority(0xff);

	/*
	 * Put the calling processor into the GIQ.  This is really only
	 * necessary from a secondary thread as the OF start-cpu interface
	 * performs this function for us on primary threads.
	 *
	 * XXX: undo of teardown on kexec needs this too, as may hotplug
	 */
	rtas_set_indicator_fast(GLOBAL_INTERRUPT_QUEUE,
		(1UL << interrupt_server_size) - 1 - default_distrib_server, 1);
}


static struct irq_chip xics_pic_direct = {
	.typename = " XICS     ",
	.startup = xics_startup,
	.mask = xics_mask_irq,
	.unmask = xics_unmask_irq,
	.eoi = xics_eoi_direct,
	.set_affinity = xics_set_affinity
};


static struct irq_chip xics_pic_lpar = {
	.typename = " XICS     ",
	.startup = xics_startup,
	.mask = xics_mask_irq,
	.unmask = xics_unmask_irq,
	.eoi = xics_eoi_lpar,
	.set_affinity = xics_set_affinity
};


static int xics_host_match(struct irq_host *h, struct device_node *node)
{
	/* IBM machines have interrupt parents of various funky types for things
	 * like vdevices, events, etc... The trick we use here is to match
	 * everything here except the legacy 8259 which is compatible "chrp,iic"
	 */
	return !of_device_is_compatible(node, "chrp,iic");
}

static int xics_host_map_direct(struct irq_host *h, unsigned int virq,
				irq_hw_number_t hw)
{
	pr_debug("xics: map_direct virq %d, hwirq 0x%lx\n", virq, hw);

	get_irq_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &xics_pic_direct, handle_fasteoi_irq);
	return 0;
}

static int xics_host_map_lpar(struct irq_host *h, unsigned int virq,
			      irq_hw_number_t hw)
{
	pr_debug("xics: map_direct virq %d, hwirq 0x%lx\n", virq, hw);

	get_irq_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &xics_pic_lpar, handle_fasteoi_irq);
	return 0;
}

static int xics_host_xlate(struct irq_host *h, struct device_node *ct,
			   u32 *intspec, unsigned int intsize,
			   irq_hw_number_t *out_hwirq, unsigned int *out_flags)

{
	/* Current xics implementation translates everything
	 * to level. It is not technically right for MSIs but this
	 * is irrelevant at this point. We might get smarter in the future
	 */
	*out_hwirq = intspec[0];
	*out_flags = IRQ_TYPE_LEVEL_LOW;

	return 0;
}

static struct irq_host_ops xics_host_direct_ops = {
	.match = xics_host_match,
	.map = xics_host_map_direct,
	.xlate = xics_host_xlate,
};

static struct irq_host_ops xics_host_lpar_ops = {
	.match = xics_host_match,
	.map = xics_host_map_lpar,
	.xlate = xics_host_xlate,
};

static void __init xics_init_host(void)
{
	struct irq_host_ops *ops;

	if (firmware_has_feature(FW_FEATURE_LPAR))
		ops = &xics_host_lpar_ops;
	else
		ops = &xics_host_direct_ops;
	xics_host = irq_alloc_host(NULL, IRQ_HOST_MAP_TREE, 0, ops,
				   XICS_IRQ_SPURIOUS);
	BUG_ON(xics_host == NULL);
	irq_set_default_host(xics_host);
}

static void __init xics_map_one_cpu(int hw_id, unsigned long addr,
				     unsigned long size)
{
#ifdef CONFIG_SMP
	int i;

	/* This may look gross but it's good enough for now, we don't quite
	 * have a hard -> linux processor id matching.
	 */
	for_each_possible_cpu(i) {
		if (!cpu_present(i))
			continue;
		if (hw_id == get_hard_smp_processor_id(i)) {
			xics_per_cpu[i] = ioremap(addr, size);
			return;
		}
	}
#else
	if (hw_id != 0)
		return;
	xics_per_cpu[0] = ioremap(addr, size);
#endif /* CONFIG_SMP */
}

static void __init xics_init_one_node(struct device_node *np,
				      unsigned int *indx)
{
	unsigned int ilen;
	const u32 *ireg;

	/* This code does the theorically broken assumption that the interrupt
	 * server numbers are the same as the hard CPU numbers.
	 * This happens to be the case so far but we are playing with fire...
	 * should be fixed one of these days. -BenH.
	 */
	ireg = of_get_property(np, "ibm,interrupt-server-ranges", NULL);

	/* Do that ever happen ? we'll know soon enough... but even good'old
	 * f80 does have that property ..
	 */
	WARN_ON(ireg == NULL);
	if (ireg) {
		/*
		 * set node starting index for this node
		 */
		*indx = *ireg;
	}
	ireg = of_get_property(np, "reg", &ilen);
	if (!ireg)
		panic("xics_init_IRQ: can't find interrupt reg property");

	while (ilen >= (4 * sizeof(u32))) {
		unsigned long addr, size;

		/* XXX Use proper OF parsing code here !!! */
		addr = (unsigned long)*ireg++ << 32;
		ilen -= sizeof(u32);
		addr |= *ireg++;
		ilen -= sizeof(u32);
		size = (unsigned long)*ireg++ << 32;
		ilen -= sizeof(u32);
		size |= *ireg++;
		ilen -= sizeof(u32);
		xics_map_one_cpu(*indx, addr, size);
		(*indx)++;
	}
}


static void __init xics_setup_8259_cascade(void)
{
	struct device_node *np, *old, *found = NULL;
	int cascade, naddr;
	const u32 *addrp;
	unsigned long intack = 0;

	for_each_node_by_type(np, "interrupt-controller")
		if (of_device_is_compatible(np, "chrp,iic")) {
			found = np;
			break;
		}
	if (found == NULL) {
		printk(KERN_DEBUG "xics: no ISA interrupt controller\n");
		return;
	}
	cascade = irq_of_parse_and_map(found, 0);
	if (cascade == NO_IRQ) {
		printk(KERN_ERR "xics: failed to map cascade interrupt");
		return;
	}
	pr_debug("xics: cascade mapped to irq %d\n", cascade);

	for (old = of_node_get(found); old != NULL ; old = np) {
		np = of_get_parent(old);
		of_node_put(old);
		if (np == NULL)
			break;
		if (strcmp(np->name, "pci") != 0)
			continue;
		addrp = of_get_property(np, "8259-interrupt-acknowledge", NULL);
		if (addrp == NULL)
			continue;
		naddr = of_n_addr_cells(np);
		intack = addrp[naddr-1];
		if (naddr > 1)
			intack |= ((unsigned long)addrp[naddr-2]) << 32;
	}
	if (intack)
		printk(KERN_DEBUG "xics: PCI 8259 intack at 0x%016lx\n", intack);
	i8259_init(found, intack);
	of_node_put(found);
	set_irq_chained_handler(cascade, pseries_8259_cascade);
}

static struct device_node *cpuid_to_of_node(int cpu)
{
	struct device_node *np;
	u32 hcpuid = get_hard_smp_processor_id(cpu);

	for_each_node_by_type(np, "cpu") {
		int i, len;
		const u32 *intserv;

		intserv = of_get_property(np, "ibm,ppc-interrupt-server#s",
					&len);

		if (!intserv)
			intserv = of_get_property(np, "reg", &len);

		i = len / sizeof(u32);

		while (i--)
			if (intserv[i] == hcpuid)
				return np;
	}

	return NULL;
}

void __init xics_init_IRQ(void)
{
	int i, j;
	struct device_node *np;
	u32 ilen, indx = 0;
	const u32 *ireg, *isize;
	int found = 0;
	u32 hcpuid;

	ppc64_boot_msg(0x20, "XICS Init");

	ibm_get_xive = rtas_token("ibm,get-xive");
	ibm_set_xive = rtas_token("ibm,set-xive");
	ibm_int_on  = rtas_token("ibm,int-on");
	ibm_int_off = rtas_token("ibm,int-off");

	for_each_node_by_type(np, "PowerPC-External-Interrupt-Presentation") {
		found = 1;
		if (firmware_has_feature(FW_FEATURE_LPAR))
			break;
		xics_init_one_node(np, &indx);
	}
	if (found == 0)
		return;

	xics_init_host();

	/* Find the server numbers for the boot cpu. */
	np = cpuid_to_of_node(boot_cpuid);
	BUG_ON(!np);
	ireg = of_get_property(np, "ibm,ppc-interrupt-gserver#s", &ilen);
	if (!ireg)
		goto skip_gserver_check;
	i = ilen / sizeof(int);
	hcpuid = get_hard_smp_processor_id(boot_cpuid);

	/* Global interrupt distribution server is specified in the last
	 * entry of "ibm,ppc-interrupt-gserver#s" property. Get the last
	 * entry fom this property for current boot cpu id and use it as
	 * default distribution server
	 */
	for (j = 0; j < i; j += 2) {
		if (ireg[j] == hcpuid) {
			default_server = hcpuid;
			default_distrib_server = ireg[j+1];

			isize = of_get_property(np,
					"ibm,interrupt-server#-size", NULL);
			if (isize)
				interrupt_server_size = *isize;
		}
	}
skip_gserver_check:
	of_node_put(np);

	if (firmware_has_feature(FW_FEATURE_LPAR))
		ppc_md.get_irq = xics_get_irq_lpar;
	else
		ppc_md.get_irq = xics_get_irq_direct;

	xics_setup_cpu();

	xics_setup_8259_cascade();

	ppc64_boot_msg(0x21, "XICS Done");
}


#ifdef CONFIG_SMP
void xics_request_IPIs(void)
{
	unsigned int ipi;
	int rc;

	ipi = irq_create_mapping(xics_host, XICS_IPI);
	BUG_ON(ipi == NO_IRQ);

	/*
	 * IPIs are marked IRQF_DISABLED as they must run with irqs
	 * disabled
	 */
	set_irq_handler(ipi, handle_percpu_irq);
	if (firmware_has_feature(FW_FEATURE_LPAR))
		rc = request_irq(ipi, xics_ipi_action_lpar, IRQF_DISABLED,
				"IPI", NULL);
	else
		rc = request_irq(ipi, xics_ipi_action_direct, IRQF_DISABLED,
				"IPI", NULL);
	BUG_ON(rc);
}
#endif /* CONFIG_SMP */

void xics_teardown_cpu(int secondary)
{
	int cpu = smp_processor_id();
	unsigned int ipi;
	struct irq_desc *desc;

	xics_set_cpu_priority(0);

	/*
	 * Clear IPI
	 */
	if (firmware_has_feature(FW_FEATURE_LPAR))
		lpar_qirr_info(cpu, 0xff);
	else
		direct_qirr_info(cpu, 0xff);

	/*
	 * we need to EOI the IPI if we got here from kexec down IPI
	 *
	 * probably need to check all the other interrupts too
	 * should we be flagging idle loop instead?
	 * or creating some task to be scheduled?
	 */

	ipi = irq_find_mapping(xics_host, XICS_IPI);
	if (ipi == XICS_IRQ_SPURIOUS)
		return;
	desc = get_irq_desc(ipi);
	if (desc->chip && desc->chip->eoi)
		desc->chip->eoi(ipi);

	/*
	 * Some machines need to have at least one cpu in the GIQ,
	 * so leave the master cpu in the group.
	 */
	if (secondary)
		rtas_set_indicator_fast(GLOBAL_INTERRUPT_QUEUE,
				   (1UL << interrupt_server_size) - 1 -
				   default_distrib_server, 0);
}

#ifdef CONFIG_HOTPLUG_CPU

/* Interrupts are disabled. */
void xics_migrate_irqs_away(void)
{
	int status;
	int cpu = smp_processor_id(), hw_cpu = hard_smp_processor_id();
	unsigned int irq, virq;

	/* Reject any interrupt that was queued to us... */
	xics_set_cpu_priority(0);

	/* remove ourselves from the global interrupt queue */
	status = rtas_set_indicator_fast(GLOBAL_INTERRUPT_QUEUE,
		(1UL << interrupt_server_size) - 1 - default_distrib_server, 0);
	WARN_ON(status < 0);

	/* Allow IPIs again... */
	xics_set_cpu_priority(DEFAULT_PRIORITY);

	for_each_irq(virq) {
		struct irq_desc *desc;
		int xics_status[2];
		unsigned long flags;

		/* We cant set affinity on ISA interrupts */
		if (virq < NUM_ISA_INTERRUPTS)
			continue;
		if (irq_map[virq].host != xics_host)
			continue;
		irq = (unsigned int)irq_map[virq].hwirq;
		/* We need to get IPIs still. */
		if (irq == XICS_IPI || irq == XICS_IRQ_SPURIOUS)
			continue;
		desc = get_irq_desc(virq);

		/* We only need to migrate enabled IRQS */
		if (desc == NULL || desc->chip == NULL
		    || desc->action == NULL
		    || desc->chip->set_affinity == NULL)
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
		if (xics_status[0] != hw_cpu)
			goto unlock;

		printk(KERN_WARNING "IRQ %u affinity broken off cpu %u\n",
		       virq, cpu);

		/* Reset affinity to all cpus */
		desc->chip->set_affinity(virq, CPU_MASK_ALL);
		irq_desc[irq].affinity = CPU_MASK_ALL;
unlock:
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}
#endif
