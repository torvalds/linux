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
#include <linux/threads.h>
#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/debugfs.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/prom.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/irq.h>
#include <asm/errno.h>
#include <asm/rtas.h>
#include <asm/xics.h>
#include <asm/firmware.h>

/* Globals common to all ICP/ICS implementations */
const struct icp_ops	*icp_ops;

unsigned int xics_default_server		= 0xff;
unsigned int xics_default_distrib_server	= 0;
unsigned int xics_interrupt_server_size		= 8;

DEFINE_PER_CPU(struct xics_cppr, xics_cppr);

struct irq_domain *xics_host;

static LIST_HEAD(ics_list);

void xics_update_irq_servers(void)
{
	int i, j;
	struct device_node *np;
	u32 ilen;
	const u32 *ireg;
	u32 hcpuid;

	/* Find the server numbers for the boot cpu. */
	np = of_get_cpu_node(boot_cpuid, NULL);
	BUG_ON(!np);

	hcpuid = get_hard_smp_processor_id(boot_cpuid);
	xics_default_server = xics_default_distrib_server = hcpuid;

	pr_devel("xics: xics_default_server = 0x%x\n", xics_default_server);

	ireg = of_get_property(np, "ibm,ppc-interrupt-gserver#s", &ilen);
	if (!ireg) {
		of_node_put(np);
		return;
	}

	i = ilen / sizeof(int);

	/* Global interrupt distribution server is specified in the last
	 * entry of "ibm,ppc-interrupt-gserver#s" property. Get the last
	 * entry fom this property for current boot cpu id and use it as
	 * default distribution server
	 */
	for (j = 0; j < i; j += 2) {
		if (ireg[j] == hcpuid) {
			xics_default_distrib_server = ireg[j+1];
			break;
		}
	}
	pr_devel("xics: xics_default_distrib_server = 0x%x\n",
		 xics_default_distrib_server);
	of_node_put(np);
}

/* GIQ stuff, currently only supported on RTAS setups, will have
 * to be sorted properly for bare metal
 */
void xics_set_cpu_giq(unsigned int gserver, unsigned int join)
{
#ifdef CONFIG_PPC_RTAS
	int index;
	int status;

	if (!rtas_indicator_present(GLOBAL_INTERRUPT_QUEUE, NULL))
		return;

	index = (1UL << xics_interrupt_server_size) - 1 - gserver;

	status = rtas_set_indicator_fast(GLOBAL_INTERRUPT_QUEUE, index, join);

	WARN(status < 0, "set-indicator(%d, %d, %u) returned %d\n",
	     GLOBAL_INTERRUPT_QUEUE, index, join, status);
#endif
}

void xics_setup_cpu(void)
{
	icp_ops->set_priority(LOWEST_PRIORITY);

	xics_set_cpu_giq(xics_default_distrib_server, 1);
}

void xics_mask_unknown_vec(unsigned int vec)
{
	struct ics *ics;

	pr_err("Interrupt 0x%x (real) is invalid, disabling it.\n", vec);

	list_for_each_entry(ics, &ics_list, link)
		ics->mask_unknown(ics, vec);
}


#ifdef CONFIG_SMP

static void xics_request_ipi(void)
{
	unsigned int ipi;

	ipi = irq_create_mapping(xics_host, XICS_IPI);
	BUG_ON(ipi == NO_IRQ);

	/*
	 * IPIs are marked IRQF_PERCPU. The handler was set in map.
	 */
	BUG_ON(request_irq(ipi, icp_ops->ipi_action,
			   IRQF_PERCPU | IRQF_NO_THREAD, "IPI", NULL));
}

int __init xics_smp_probe(void)
{
	/* Setup cause_ipi callback  based on which ICP is used */
	smp_ops->cause_ipi = icp_ops->cause_ipi;

	/* Register all the IPIs */
	xics_request_ipi();

	return cpumask_weight(cpu_possible_mask);
}

#endif /* CONFIG_SMP */

void xics_teardown_cpu(void)
{
	struct xics_cppr *os_cppr = &__get_cpu_var(xics_cppr);

	/*
	 * we have to reset the cppr index to 0 because we're
	 * not going to return from the IPI
	 */
	os_cppr->index = 0;
	icp_ops->set_priority(0);
	icp_ops->teardown_cpu();
}

void xics_kexec_teardown_cpu(int secondary)
{
	xics_teardown_cpu();

	icp_ops->flush_ipi();

	/*
	 * Some machines need to have at least one cpu in the GIQ,
	 * so leave the master cpu in the group.
	 */
	if (secondary)
		xics_set_cpu_giq(xics_default_distrib_server, 0);
}


#ifdef CONFIG_HOTPLUG_CPU

/* Interrupts are disabled. */
void xics_migrate_irqs_away(void)
{
	int cpu = smp_processor_id(), hw_cpu = hard_smp_processor_id();
	unsigned int irq, virq;

	/* If we used to be the default server, move to the new "boot_cpuid" */
	if (hw_cpu == xics_default_server)
		xics_update_irq_servers();

	/* Reject any interrupt that was queued to us... */
	icp_ops->set_priority(0);

	/* Remove ourselves from the global interrupt queue */
	xics_set_cpu_giq(xics_default_distrib_server, 0);

	/* Allow IPIs again... */
	icp_ops->set_priority(DEFAULT_PRIORITY);

	for_each_irq(virq) {
		struct irq_desc *desc;
		struct irq_chip *chip;
		long server;
		unsigned long flags;
		struct ics *ics;

		/* We can't set affinity on ISA interrupts */
		if (virq < NUM_ISA_INTERRUPTS)
			continue;
		desc = irq_to_desc(virq);
		/* We only need to migrate enabled IRQS */
		if (!desc || !desc->action)
			continue;
		if (desc->irq_data.domain != xics_host)
			continue;
		irq = desc->irq_data.hwirq;
		/* We need to get IPIs still. */
		if (irq == XICS_IPI || irq == XICS_IRQ_SPURIOUS)
			continue;
		chip = irq_desc_get_chip(desc);
		if (!chip || !chip->irq_set_affinity)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		/* Locate interrupt server */
		server = -1;
		ics = irq_get_chip_data(virq);
		if (ics)
			server = ics->get_server(ics, irq);
		if (server < 0) {
			printk(KERN_ERR "%s: Can't find server for irq %d\n",
			       __func__, irq);
			goto unlock;
		}

		/* We only support delivery to all cpus or to one cpu.
		 * The irq has to be migrated only in the single cpu
		 * case.
		 */
		if (server != hw_cpu)
			goto unlock;

		/* This is expected during cpu offline. */
		if (cpu_online(cpu))
			pr_warning("IRQ %u affinity broken off cpu %u\n",
			       virq, cpu);

		/* Reset affinity to all cpus */
		raw_spin_unlock_irqrestore(&desc->lock, flags);
		irq_set_affinity(virq, cpu_all_mask);
		continue;
unlock:
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}
#endif /* CONFIG_HOTPLUG_CPU */

#ifdef CONFIG_SMP
/*
 * For the moment we only implement delivery to all cpus or one cpu.
 *
 * If the requested affinity is cpu_all_mask, we set global affinity.
 * If not we set it to the first cpu in the mask, even if multiple cpus
 * are set. This is so things like irqbalance (which set core and package
 * wide affinities) do the right thing.
 *
 * We need to fix this to implement support for the links
 */
int xics_get_irq_server(unsigned int virq, const struct cpumask *cpumask,
			unsigned int strict_check)
{

	if (!distribute_irqs)
		return xics_default_server;

	if (!cpumask_subset(cpu_possible_mask, cpumask)) {
		int server = cpumask_first_and(cpu_online_mask, cpumask);

		if (server < nr_cpu_ids)
			return get_hard_smp_processor_id(server);

		if (strict_check)
			return -1;
	}

	/*
	 * Workaround issue with some versions of JS20 firmware that
	 * deliver interrupts to cpus which haven't been started. This
	 * happens when using the maxcpus= boot option.
	 */
	if (cpumask_equal(cpu_online_mask, cpu_present_mask))
		return xics_default_distrib_server;

	return xics_default_server;
}
#endif /* CONFIG_SMP */

static int xics_host_match(struct irq_domain *h, struct device_node *node)
{
	struct ics *ics;

	list_for_each_entry(ics, &ics_list, link)
		if (ics->host_match(ics, node))
			return 1;

	return 0;
}

/* Dummies */
static void xics_ipi_unmask(struct irq_data *d) { }
static void xics_ipi_mask(struct irq_data *d) { }

static struct irq_chip xics_ipi_chip = {
	.name = "XICS",
	.irq_eoi = NULL, /* Patched at init time */
	.irq_mask = xics_ipi_mask,
	.irq_unmask = xics_ipi_unmask,
};

static int xics_host_map(struct irq_domain *h, unsigned int virq,
			 irq_hw_number_t hw)
{
	struct ics *ics;

	pr_devel("xics: map virq %d, hwirq 0x%lx\n", virq, hw);

	/* Insert the interrupt mapping into the radix tree for fast lookup */
	irq_radix_revmap_insert(xics_host, virq, hw);

	/* They aren't all level sensitive but we just don't really know */
	irq_set_status_flags(virq, IRQ_LEVEL);

	/* Don't call into ICS for IPIs */
	if (hw == XICS_IPI) {
		irq_set_chip_and_handler(virq, &xics_ipi_chip,
					 handle_percpu_irq);
		return 0;
	}

	/* Let the ICS setup the chip data */
	list_for_each_entry(ics, &ics_list, link)
		if (ics->map(ics, virq) == 0)
			return 0;

	return -EINVAL;
}

static int xics_host_xlate(struct irq_domain *h, struct device_node *ct,
			   const u32 *intspec, unsigned int intsize,
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

static struct irq_domain_ops xics_host_ops = {
	.match = xics_host_match,
	.map = xics_host_map,
	.xlate = xics_host_xlate,
};

static void __init xics_init_host(void)
{
	xics_host = irq_domain_add_tree(NULL, &xics_host_ops, NULL);
	BUG_ON(xics_host == NULL);
	irq_set_default_host(xics_host);
}

void __init xics_register_ics(struct ics *ics)
{
	list_add(&ics->link, &ics_list);
}

static void __init xics_get_server_size(void)
{
	struct device_node *np;
	const u32 *isize;

	/* We fetch the interrupt server size from the first ICS node
	 * we find if any
	 */
	np = of_find_compatible_node(NULL, NULL, "ibm,ppc-xics");
	if (!np)
		return;
	isize = of_get_property(np, "ibm,interrupt-server#-size", NULL);
	if (!isize)
		return;
	xics_interrupt_server_size = *isize;
	of_node_put(np);
}

void __init xics_init(void)
{
	int rc = -1;

	/* Fist locate ICP */
	if (firmware_has_feature(FW_FEATURE_LPAR))
		rc = icp_hv_init();
	if (rc < 0)
		rc = icp_native_init();
	if (rc < 0) {
		pr_warning("XICS: Cannot find a Presentation Controller !\n");
		return;
	}

	/* Copy get_irq callback over to ppc_md */
	ppc_md.get_irq = icp_ops->get_irq;

	/* Patch up IPI chip EOI */
	xics_ipi_chip.irq_eoi = icp_ops->eoi;

	/* Now locate ICS */
	rc = ics_rtas_init();
	if (rc < 0)
		rc = ics_opal_init();
	if (rc < 0)
		pr_warning("XICS: Cannot find a Source Controller !\n");

	/* Initialize common bits */
	xics_get_server_size();
	xics_update_irq_servers();
	xics_init_host();
	xics_setup_cpu();
}
