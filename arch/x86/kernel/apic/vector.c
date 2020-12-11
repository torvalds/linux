// SPDX-License-Identifier: GPL-2.0-only
/*
 * Local APIC related interfaces to support IOAPIC, MSI, etc.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 * Jiang Liu <jiang.liu@linux.intel.com>
 *	Enable support of hierarchical irqdomains
 */
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <asm/irqdomain.h>
#include <asm/hw_irq.h>
#include <asm/traps.h>
#include <asm/apic.h>
#include <asm/i8259.h>
#include <asm/desc.h>
#include <asm/irq_remapping.h>

#include <asm/trace/irq_vectors.h>

struct apic_chip_data {
	struct irq_cfg		hw_irq_cfg;
	unsigned int		vector;
	unsigned int		prev_vector;
	unsigned int		cpu;
	unsigned int		prev_cpu;
	unsigned int		irq;
	struct hlist_node	clist;
	unsigned int		move_in_progress	: 1,
				is_managed		: 1,
				can_reserve		: 1,
				has_reserved		: 1;
};

struct irq_domain *x86_vector_domain;
EXPORT_SYMBOL_GPL(x86_vector_domain);
static DEFINE_RAW_SPINLOCK(vector_lock);
static cpumask_var_t vector_searchmask;
static struct irq_chip lapic_controller;
static struct irq_matrix *vector_matrix;
#ifdef CONFIG_SMP
static DEFINE_PER_CPU(struct hlist_head, cleanup_list);
#endif

void lock_vector_lock(void)
{
	/* Used to the online set of cpus does not change
	 * during assign_irq_vector.
	 */
	raw_spin_lock(&vector_lock);
}

void unlock_vector_lock(void)
{
	raw_spin_unlock(&vector_lock);
}

void init_irq_alloc_info(struct irq_alloc_info *info,
			 const struct cpumask *mask)
{
	memset(info, 0, sizeof(*info));
	info->mask = mask;
}

void copy_irq_alloc_info(struct irq_alloc_info *dst, struct irq_alloc_info *src)
{
	if (src)
		*dst = *src;
	else
		memset(dst, 0, sizeof(*dst));
}

static struct apic_chip_data *apic_chip_data(struct irq_data *irqd)
{
	if (!irqd)
		return NULL;

	while (irqd->parent_data)
		irqd = irqd->parent_data;

	return irqd->chip_data;
}

struct irq_cfg *irqd_cfg(struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);

	return apicd ? &apicd->hw_irq_cfg : NULL;
}
EXPORT_SYMBOL_GPL(irqd_cfg);

struct irq_cfg *irq_cfg(unsigned int irq)
{
	return irqd_cfg(irq_get_irq_data(irq));
}

static struct apic_chip_data *alloc_apic_chip_data(int node)
{
	struct apic_chip_data *apicd;

	apicd = kzalloc_node(sizeof(*apicd), GFP_KERNEL, node);
	if (apicd)
		INIT_HLIST_NODE(&apicd->clist);
	return apicd;
}

static void free_apic_chip_data(struct apic_chip_data *apicd)
{
	kfree(apicd);
}

static void apic_update_irq_cfg(struct irq_data *irqd, unsigned int vector,
				unsigned int cpu)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);

	lockdep_assert_held(&vector_lock);

	apicd->hw_irq_cfg.vector = vector;
	apicd->hw_irq_cfg.dest_apicid = apic->calc_dest_apicid(cpu);
	irq_data_update_effective_affinity(irqd, cpumask_of(cpu));
	trace_vector_config(irqd->irq, vector, cpu,
			    apicd->hw_irq_cfg.dest_apicid);
}

static void apic_update_vector(struct irq_data *irqd, unsigned int newvec,
			       unsigned int newcpu)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	struct irq_desc *desc = irq_data_to_desc(irqd);
	bool managed = irqd_affinity_is_managed(irqd);

	lockdep_assert_held(&vector_lock);

	trace_vector_update(irqd->irq, newvec, newcpu, apicd->vector,
			    apicd->cpu);

	/*
	 * If there is no vector associated or if the associated vector is
	 * the shutdown vector, which is associated to make PCI/MSI
	 * shutdown mode work, then there is nothing to release. Clear out
	 * prev_vector for this and the offlined target case.
	 */
	apicd->prev_vector = 0;
	if (!apicd->vector || apicd->vector == MANAGED_IRQ_SHUTDOWN_VECTOR)
		goto setnew;
	/*
	 * If the target CPU of the previous vector is online, then mark
	 * the vector as move in progress and store it for cleanup when the
	 * first interrupt on the new vector arrives. If the target CPU is
	 * offline then the regular release mechanism via the cleanup
	 * vector is not possible and the vector can be immediately freed
	 * in the underlying matrix allocator.
	 */
	if (cpu_online(apicd->cpu)) {
		apicd->move_in_progress = true;
		apicd->prev_vector = apicd->vector;
		apicd->prev_cpu = apicd->cpu;
		WARN_ON_ONCE(apicd->cpu == newcpu);
	} else {
		irq_matrix_free(vector_matrix, apicd->cpu, apicd->vector,
				managed);
	}

setnew:
	apicd->vector = newvec;
	apicd->cpu = newcpu;
	BUG_ON(!IS_ERR_OR_NULL(per_cpu(vector_irq, newcpu)[newvec]));
	per_cpu(vector_irq, newcpu)[newvec] = desc;
}

static void vector_assign_managed_shutdown(struct irq_data *irqd)
{
	unsigned int cpu = cpumask_first(cpu_online_mask);

	apic_update_irq_cfg(irqd, MANAGED_IRQ_SHUTDOWN_VECTOR, cpu);
}

static int reserve_managed_vector(struct irq_data *irqd)
{
	const struct cpumask *affmsk = irq_data_get_affinity_mask(irqd);
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&vector_lock, flags);
	apicd->is_managed = true;
	ret = irq_matrix_reserve_managed(vector_matrix, affmsk);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	trace_vector_reserve_managed(irqd->irq, ret);
	return ret;
}

static void reserve_irq_vector_locked(struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);

	irq_matrix_reserve(vector_matrix);
	apicd->can_reserve = true;
	apicd->has_reserved = true;
	irqd_set_can_reserve(irqd);
	trace_vector_reserve(irqd->irq, 0);
	vector_assign_managed_shutdown(irqd);
}

static int reserve_irq_vector(struct irq_data *irqd)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	reserve_irq_vector_locked(irqd);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return 0;
}

static int
assign_vector_locked(struct irq_data *irqd, const struct cpumask *dest)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	bool resvd = apicd->has_reserved;
	unsigned int cpu = apicd->cpu;
	int vector = apicd->vector;

	lockdep_assert_held(&vector_lock);

	/*
	 * If the current target CPU is online and in the new requested
	 * affinity mask, there is no point in moving the interrupt from
	 * one CPU to another.
	 */
	if (vector && cpu_online(cpu) && cpumask_test_cpu(cpu, dest))
		return 0;

	/*
	 * Careful here. @apicd might either have move_in_progress set or
	 * be enqueued for cleanup. Assigning a new vector would either
	 * leave a stale vector on some CPU around or in case of a pending
	 * cleanup corrupt the hlist.
	 */
	if (apicd->move_in_progress || !hlist_unhashed(&apicd->clist))
		return -EBUSY;

	vector = irq_matrix_alloc(vector_matrix, dest, resvd, &cpu);
	trace_vector_alloc(irqd->irq, vector, resvd, vector);
	if (vector < 0)
		return vector;
	apic_update_vector(irqd, vector, cpu);
	apic_update_irq_cfg(irqd, vector, cpu);

	return 0;
}

static int assign_irq_vector(struct irq_data *irqd, const struct cpumask *dest)
{
	unsigned long flags;
	int ret;

	raw_spin_lock_irqsave(&vector_lock, flags);
	cpumask_and(vector_searchmask, dest, cpu_online_mask);
	ret = assign_vector_locked(irqd, vector_searchmask);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return ret;
}

static int assign_irq_vector_any_locked(struct irq_data *irqd)
{
	/* Get the affinity mask - either irq_default_affinity or (user) set */
	const struct cpumask *affmsk = irq_data_get_affinity_mask(irqd);
	int node = irq_data_get_node(irqd);

	if (node == NUMA_NO_NODE)
		goto all;
	/* Try the intersection of @affmsk and node mask */
	cpumask_and(vector_searchmask, cpumask_of_node(node), affmsk);
	if (!assign_vector_locked(irqd, vector_searchmask))
		return 0;
	/* Try the node mask */
	if (!assign_vector_locked(irqd, cpumask_of_node(node)))
		return 0;
all:
	/* Try the full affinity mask */
	cpumask_and(vector_searchmask, affmsk, cpu_online_mask);
	if (!assign_vector_locked(irqd, vector_searchmask))
		return 0;
	/* Try the full online mask */
	return assign_vector_locked(irqd, cpu_online_mask);
}

static int
assign_irq_vector_policy(struct irq_data *irqd, struct irq_alloc_info *info)
{
	if (irqd_affinity_is_managed(irqd))
		return reserve_managed_vector(irqd);
	if (info->mask)
		return assign_irq_vector(irqd, info->mask);
	/*
	 * Make only a global reservation with no guarantee. A real vector
	 * is associated at activation time.
	 */
	return reserve_irq_vector(irqd);
}

static int
assign_managed_vector(struct irq_data *irqd, const struct cpumask *dest)
{
	const struct cpumask *affmsk = irq_data_get_affinity_mask(irqd);
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	int vector, cpu;

	cpumask_and(vector_searchmask, dest, affmsk);

	/* set_affinity might call here for nothing */
	if (apicd->vector && cpumask_test_cpu(apicd->cpu, vector_searchmask))
		return 0;
	vector = irq_matrix_alloc_managed(vector_matrix, vector_searchmask,
					  &cpu);
	trace_vector_alloc_managed(irqd->irq, vector, vector);
	if (vector < 0)
		return vector;
	apic_update_vector(irqd, vector, cpu);
	apic_update_irq_cfg(irqd, vector, cpu);
	return 0;
}

static void clear_irq_vector(struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	bool managed = irqd_affinity_is_managed(irqd);
	unsigned int vector = apicd->vector;

	lockdep_assert_held(&vector_lock);

	if (!vector)
		return;

	trace_vector_clear(irqd->irq, vector, apicd->cpu, apicd->prev_vector,
			   apicd->prev_cpu);

	per_cpu(vector_irq, apicd->cpu)[vector] = VECTOR_SHUTDOWN;
	irq_matrix_free(vector_matrix, apicd->cpu, vector, managed);
	apicd->vector = 0;

	/* Clean up move in progress */
	vector = apicd->prev_vector;
	if (!vector)
		return;

	per_cpu(vector_irq, apicd->prev_cpu)[vector] = VECTOR_SHUTDOWN;
	irq_matrix_free(vector_matrix, apicd->prev_cpu, vector, managed);
	apicd->prev_vector = 0;
	apicd->move_in_progress = 0;
	hlist_del_init(&apicd->clist);
}

static void x86_vector_deactivate(struct irq_domain *dom, struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	unsigned long flags;

	trace_vector_deactivate(irqd->irq, apicd->is_managed,
				apicd->can_reserve, false);

	/* Regular fixed assigned interrupt */
	if (!apicd->is_managed && !apicd->can_reserve)
		return;
	/* If the interrupt has a global reservation, nothing to do */
	if (apicd->has_reserved)
		return;

	raw_spin_lock_irqsave(&vector_lock, flags);
	clear_irq_vector(irqd);
	if (apicd->can_reserve)
		reserve_irq_vector_locked(irqd);
	else
		vector_assign_managed_shutdown(irqd);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
}

static int activate_reserved(struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	int ret;

	ret = assign_irq_vector_any_locked(irqd);
	if (!ret) {
		apicd->has_reserved = false;
		/*
		 * Core might have disabled reservation mode after
		 * allocating the irq descriptor. Ideally this should
		 * happen before allocation time, but that would require
		 * completely convoluted ways of transporting that
		 * information.
		 */
		if (!irqd_can_reserve(irqd))
			apicd->can_reserve = false;
	}

	/*
	 * Check to ensure that the effective affinity mask is a subset
	 * the user supplied affinity mask, and warn the user if it is not
	 */
	if (!cpumask_subset(irq_data_get_effective_affinity_mask(irqd),
			    irq_data_get_affinity_mask(irqd))) {
		pr_warn("irq %u: Affinity broken due to vector space exhaustion.\n",
			irqd->irq);
	}

	return ret;
}

static int activate_managed(struct irq_data *irqd)
{
	const struct cpumask *dest = irq_data_get_affinity_mask(irqd);
	int ret;

	cpumask_and(vector_searchmask, dest, cpu_online_mask);
	if (WARN_ON_ONCE(cpumask_empty(vector_searchmask))) {
		/* Something in the core code broke! Survive gracefully */
		pr_err("Managed startup for irq %u, but no CPU\n", irqd->irq);
		return -EINVAL;
	}

	ret = assign_managed_vector(irqd, vector_searchmask);
	/*
	 * This should not happen. The vector reservation got buggered.  Handle
	 * it gracefully.
	 */
	if (WARN_ON_ONCE(ret < 0)) {
		pr_err("Managed startup irq %u, no vector available\n",
		       irqd->irq);
	}
	return ret;
}

static int x86_vector_activate(struct irq_domain *dom, struct irq_data *irqd,
			       bool reserve)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	unsigned long flags;
	int ret = 0;

	trace_vector_activate(irqd->irq, apicd->is_managed,
			      apicd->can_reserve, reserve);

	raw_spin_lock_irqsave(&vector_lock, flags);
	if (!apicd->can_reserve && !apicd->is_managed)
		assign_irq_vector_any_locked(irqd);
	else if (reserve || irqd_is_managed_and_shutdown(irqd))
		vector_assign_managed_shutdown(irqd);
	else if (apicd->is_managed)
		ret = activate_managed(irqd);
	else if (apicd->has_reserved)
		ret = activate_reserved(irqd);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return ret;
}

static void vector_free_reserved_and_managed(struct irq_data *irqd)
{
	const struct cpumask *dest = irq_data_get_affinity_mask(irqd);
	struct apic_chip_data *apicd = apic_chip_data(irqd);

	trace_vector_teardown(irqd->irq, apicd->is_managed,
			      apicd->has_reserved);

	if (apicd->has_reserved)
		irq_matrix_remove_reserved(vector_matrix);
	if (apicd->is_managed)
		irq_matrix_remove_managed(vector_matrix, dest);
}

static void x86_vector_free_irqs(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	struct apic_chip_data *apicd;
	struct irq_data *irqd;
	unsigned long flags;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irqd = irq_domain_get_irq_data(x86_vector_domain, virq + i);
		if (irqd && irqd->chip_data) {
			raw_spin_lock_irqsave(&vector_lock, flags);
			clear_irq_vector(irqd);
			vector_free_reserved_and_managed(irqd);
			apicd = irqd->chip_data;
			irq_domain_reset_irq_data(irqd);
			raw_spin_unlock_irqrestore(&vector_lock, flags);
			free_apic_chip_data(apicd);
		}
	}
}

static bool vector_configure_legacy(unsigned int virq, struct irq_data *irqd,
				    struct apic_chip_data *apicd)
{
	unsigned long flags;
	bool realloc = false;

	apicd->vector = ISA_IRQ_VECTOR(virq);
	apicd->cpu = 0;

	raw_spin_lock_irqsave(&vector_lock, flags);
	/*
	 * If the interrupt is activated, then it must stay at this vector
	 * position. That's usually the timer interrupt (0).
	 */
	if (irqd_is_activated(irqd)) {
		trace_vector_setup(virq, true, 0);
		apic_update_irq_cfg(irqd, apicd->vector, apicd->cpu);
	} else {
		/* Release the vector */
		apicd->can_reserve = true;
		irqd_set_can_reserve(irqd);
		clear_irq_vector(irqd);
		realloc = true;
	}
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return realloc;
}

static int x86_vector_alloc_irqs(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	struct apic_chip_data *apicd;
	struct irq_data *irqd;
	int i, err, node;

	if (disable_apic)
		return -ENXIO;

	/* Currently vector allocator can't guarantee contiguous allocations */
	if ((info->flags & X86_IRQ_ALLOC_CONTIGUOUS_VECTORS) && nr_irqs > 1)
		return -ENOSYS;

	for (i = 0; i < nr_irqs; i++) {
		irqd = irq_domain_get_irq_data(domain, virq + i);
		BUG_ON(!irqd);
		node = irq_data_get_node(irqd);
		WARN_ON_ONCE(irqd->chip_data);
		apicd = alloc_apic_chip_data(node);
		if (!apicd) {
			err = -ENOMEM;
			goto error;
		}

		apicd->irq = virq + i;
		irqd->chip = &lapic_controller;
		irqd->chip_data = apicd;
		irqd->hwirq = virq + i;
		irqd_set_single_target(irqd);
		/*
		 * Prevent that any of these interrupts is invoked in
		 * non interrupt context via e.g. generic_handle_irq()
		 * as that can corrupt the affinity move state.
		 */
		irqd_set_handle_enforce_irqctx(irqd);

		/* Don't invoke affinity setter on deactivated interrupts */
		irqd_set_affinity_on_activate(irqd);

		/*
		 * Legacy vectors are already assigned when the IOAPIC
		 * takes them over. They stay on the same vector. This is
		 * required for check_timer() to work correctly as it might
		 * switch back to legacy mode. Only update the hardware
		 * config.
		 */
		if (info->flags & X86_IRQ_ALLOC_LEGACY) {
			if (!vector_configure_legacy(virq + i, irqd, apicd))
				continue;
		}

		err = assign_irq_vector_policy(irqd, info);
		trace_vector_setup(virq + i, false, err);
		if (err) {
			irqd->chip_data = NULL;
			free_apic_chip_data(apicd);
			goto error;
		}
	}

	return 0;

error:
	x86_vector_free_irqs(domain, virq, i);
	return err;
}

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
static void x86_vector_debug_show(struct seq_file *m, struct irq_domain *d,
				  struct irq_data *irqd, int ind)
{
	struct apic_chip_data apicd;
	unsigned long flags;
	int irq;

	if (!irqd) {
		irq_matrix_debug_show(m, vector_matrix, ind);
		return;
	}

	irq = irqd->irq;
	if (irq < nr_legacy_irqs() && !test_bit(irq, &io_apic_irqs)) {
		seq_printf(m, "%*sVector: %5d\n", ind, "", ISA_IRQ_VECTOR(irq));
		seq_printf(m, "%*sTarget: Legacy PIC all CPUs\n", ind, "");
		return;
	}

	if (!irqd->chip_data) {
		seq_printf(m, "%*sVector: Not assigned\n", ind, "");
		return;
	}

	raw_spin_lock_irqsave(&vector_lock, flags);
	memcpy(&apicd, irqd->chip_data, sizeof(apicd));
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	seq_printf(m, "%*sVector: %5u\n", ind, "", apicd.vector);
	seq_printf(m, "%*sTarget: %5u\n", ind, "", apicd.cpu);
	if (apicd.prev_vector) {
		seq_printf(m, "%*sPrevious vector: %5u\n", ind, "", apicd.prev_vector);
		seq_printf(m, "%*sPrevious target: %5u\n", ind, "", apicd.prev_cpu);
	}
	seq_printf(m, "%*smove_in_progress: %u\n", ind, "", apicd.move_in_progress ? 1 : 0);
	seq_printf(m, "%*sis_managed:       %u\n", ind, "", apicd.is_managed ? 1 : 0);
	seq_printf(m, "%*scan_reserve:      %u\n", ind, "", apicd.can_reserve ? 1 : 0);
	seq_printf(m, "%*shas_reserved:     %u\n", ind, "", apicd.has_reserved ? 1 : 0);
	seq_printf(m, "%*scleanup_pending:  %u\n", ind, "", !hlist_unhashed(&apicd.clist));
}
#endif

static const struct irq_domain_ops x86_vector_domain_ops = {
	.alloc		= x86_vector_alloc_irqs,
	.free		= x86_vector_free_irqs,
	.activate	= x86_vector_activate,
	.deactivate	= x86_vector_deactivate,
#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
	.debug_show	= x86_vector_debug_show,
#endif
};

int __init arch_probe_nr_irqs(void)
{
	int nr;

	if (nr_irqs > (NR_VECTORS * nr_cpu_ids))
		nr_irqs = NR_VECTORS * nr_cpu_ids;

	nr = (gsi_top + nr_legacy_irqs()) + 8 * nr_cpu_ids;
#if defined(CONFIG_PCI_MSI)
	/*
	 * for MSI and HT dyn irq
	 */
	if (gsi_top <= NR_IRQS_LEGACY)
		nr +=  8 * nr_cpu_ids;
	else
		nr += gsi_top * 16;
#endif
	if (nr < nr_irqs)
		nr_irqs = nr;

	/*
	 * We don't know if PIC is present at this point so we need to do
	 * probe() to get the right number of legacy IRQs.
	 */
	return legacy_pic->probe();
}

void lapic_assign_legacy_vector(unsigned int irq, bool replace)
{
	/*
	 * Use assign system here so it wont get accounted as allocated
	 * and moveable in the cpu hotplug check and it prevents managed
	 * irq reservation from touching it.
	 */
	irq_matrix_assign_system(vector_matrix, ISA_IRQ_VECTOR(irq), replace);
}

void __init lapic_assign_system_vectors(void)
{
	unsigned int i, vector = 0;

	for_each_set_bit_from(vector, system_vectors, NR_VECTORS)
		irq_matrix_assign_system(vector_matrix, vector, false);

	if (nr_legacy_irqs() > 1)
		lapic_assign_legacy_vector(PIC_CASCADE_IR, false);

	/* System vectors are reserved, online it */
	irq_matrix_online(vector_matrix);

	/* Mark the preallocated legacy interrupts */
	for (i = 0; i < nr_legacy_irqs(); i++) {
		if (i != PIC_CASCADE_IR)
			irq_matrix_assign(vector_matrix, ISA_IRQ_VECTOR(i));
	}
}

int __init arch_early_irq_init(void)
{
	struct fwnode_handle *fn;

	fn = irq_domain_alloc_named_fwnode("VECTOR");
	BUG_ON(!fn);
	x86_vector_domain = irq_domain_create_tree(fn, &x86_vector_domain_ops,
						   NULL);
	BUG_ON(x86_vector_domain == NULL);
	irq_set_default_host(x86_vector_domain);

	BUG_ON(!alloc_cpumask_var(&vector_searchmask, GFP_KERNEL));

	/*
	 * Allocate the vector matrix allocator data structure and limit the
	 * search area.
	 */
	vector_matrix = irq_alloc_matrix(NR_VECTORS, FIRST_EXTERNAL_VECTOR,
					 FIRST_SYSTEM_VECTOR);
	BUG_ON(!vector_matrix);

	return arch_early_ioapic_init();
}

#ifdef CONFIG_SMP

static struct irq_desc *__setup_vector_irq(int vector)
{
	int isairq = vector - ISA_IRQ_VECTOR(0);

	/* Check whether the irq is in the legacy space */
	if (isairq < 0 || isairq >= nr_legacy_irqs())
		return VECTOR_UNUSED;
	/* Check whether the irq is handled by the IOAPIC */
	if (test_bit(isairq, &io_apic_irqs))
		return VECTOR_UNUSED;
	return irq_to_desc(isairq);
}

/* Online the local APIC infrastructure and initialize the vectors */
void lapic_online(void)
{
	unsigned int vector;

	lockdep_assert_held(&vector_lock);

	/* Online the vector matrix array for this CPU */
	irq_matrix_online(vector_matrix);

	/*
	 * The interrupt affinity logic never targets interrupts to offline
	 * CPUs. The exception are the legacy PIC interrupts. In general
	 * they are only targeted to CPU0, but depending on the platform
	 * they can be distributed to any online CPU in hardware. The
	 * kernel has no influence on that. So all active legacy vectors
	 * must be installed on all CPUs. All non legacy interrupts can be
	 * cleared.
	 */
	for (vector = 0; vector < NR_VECTORS; vector++)
		this_cpu_write(vector_irq[vector], __setup_vector_irq(vector));
}

void lapic_offline(void)
{
	lock_vector_lock();
	irq_matrix_offline(vector_matrix);
	unlock_vector_lock();
}

static int apic_set_affinity(struct irq_data *irqd,
			     const struct cpumask *dest, bool force)
{
	int err;

	if (WARN_ON_ONCE(!irqd_is_activated(irqd)))
		return -EIO;

	raw_spin_lock(&vector_lock);
	cpumask_and(vector_searchmask, dest, cpu_online_mask);
	if (irqd_affinity_is_managed(irqd))
		err = assign_managed_vector(irqd, vector_searchmask);
	else
		err = assign_vector_locked(irqd, vector_searchmask);
	raw_spin_unlock(&vector_lock);
	return err ? err : IRQ_SET_MASK_OK;
}

#else
# define apic_set_affinity	NULL
#endif

static int apic_retrigger_irq(struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	apic->send_IPI(apicd->cpu, apicd->vector);
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	return 1;
}

void apic_ack_irq(struct irq_data *irqd)
{
	irq_move_irq(irqd);
	ack_APIC_irq();
}

void apic_ack_edge(struct irq_data *irqd)
{
	irq_complete_move(irqd_cfg(irqd));
	apic_ack_irq(irqd);
}

static struct irq_chip lapic_controller = {
	.name			= "APIC",
	.irq_ack		= apic_ack_edge,
	.irq_set_affinity	= apic_set_affinity,
	.irq_compose_msi_msg	= x86_vector_msi_compose_msg,
	.irq_retrigger		= apic_retrigger_irq,
};

#ifdef CONFIG_SMP

static void free_moved_vector(struct apic_chip_data *apicd)
{
	unsigned int vector = apicd->prev_vector;
	unsigned int cpu = apicd->prev_cpu;
	bool managed = apicd->is_managed;

	/*
	 * Managed interrupts are usually not migrated away
	 * from an online CPU, but CPU isolation 'managed_irq'
	 * can make that happen.
	 * 1) Activation does not take the isolation into account
	 *    to keep the code simple
	 * 2) Migration away from an isolated CPU can happen when
	 *    a non-isolated CPU which is in the calculated
	 *    affinity mask comes online.
	 */
	trace_vector_free_moved(apicd->irq, cpu, vector, managed);
	irq_matrix_free(vector_matrix, cpu, vector, managed);
	per_cpu(vector_irq, cpu)[vector] = VECTOR_UNUSED;
	hlist_del_init(&apicd->clist);
	apicd->prev_vector = 0;
	apicd->move_in_progress = 0;
}

DEFINE_IDTENTRY_SYSVEC(sysvec_irq_move_cleanup)
{
	struct hlist_head *clhead = this_cpu_ptr(&cleanup_list);
	struct apic_chip_data *apicd;
	struct hlist_node *tmp;

	ack_APIC_irq();
	/* Prevent vectors vanishing under us */
	raw_spin_lock(&vector_lock);

	hlist_for_each_entry_safe(apicd, tmp, clhead, clist) {
		unsigned int irr, vector = apicd->prev_vector;

		/*
		 * Paranoia: Check if the vector that needs to be cleaned
		 * up is registered at the APICs IRR. If so, then this is
		 * not the best time to clean it up. Clean it up in the
		 * next attempt by sending another IRQ_MOVE_CLEANUP_VECTOR
		 * to this CPU. IRQ_MOVE_CLEANUP_VECTOR is the lowest
		 * priority external vector, so on return from this
		 * interrupt the device interrupt will happen first.
		 */
		irr = apic_read(APIC_IRR + (vector / 32 * 0x10));
		if (irr & (1U << (vector % 32))) {
			apic->send_IPI_self(IRQ_MOVE_CLEANUP_VECTOR);
			continue;
		}
		free_moved_vector(apicd);
	}

	raw_spin_unlock(&vector_lock);
}

static void __send_cleanup_vector(struct apic_chip_data *apicd)
{
	unsigned int cpu;

	raw_spin_lock(&vector_lock);
	apicd->move_in_progress = 0;
	cpu = apicd->prev_cpu;
	if (cpu_online(cpu)) {
		hlist_add_head(&apicd->clist, per_cpu_ptr(&cleanup_list, cpu));
		apic->send_IPI(cpu, IRQ_MOVE_CLEANUP_VECTOR);
	} else {
		apicd->prev_vector = 0;
	}
	raw_spin_unlock(&vector_lock);
}

void send_cleanup_vector(struct irq_cfg *cfg)
{
	struct apic_chip_data *apicd;

	apicd = container_of(cfg, struct apic_chip_data, hw_irq_cfg);
	if (apicd->move_in_progress)
		__send_cleanup_vector(apicd);
}

void irq_complete_move(struct irq_cfg *cfg)
{
	struct apic_chip_data *apicd;

	apicd = container_of(cfg, struct apic_chip_data, hw_irq_cfg);
	if (likely(!apicd->move_in_progress))
		return;

	/*
	 * If the interrupt arrived on the new target CPU, cleanup the
	 * vector on the old target CPU. A vector check is not required
	 * because an interrupt can never move from one vector to another
	 * on the same CPU.
	 */
	if (apicd->cpu == smp_processor_id())
		__send_cleanup_vector(apicd);
}

/*
 * Called from fixup_irqs() with @desc->lock held and interrupts disabled.
 */
void irq_force_complete_move(struct irq_desc *desc)
{
	struct apic_chip_data *apicd;
	struct irq_data *irqd;
	unsigned int vector;

	/*
	 * The function is called for all descriptors regardless of which
	 * irqdomain they belong to. For example if an IRQ is provided by
	 * an irq_chip as part of a GPIO driver, the chip data for that
	 * descriptor is specific to the irq_chip in question.
	 *
	 * Check first that the chip_data is what we expect
	 * (apic_chip_data) before touching it any further.
	 */
	irqd = irq_domain_get_irq_data(x86_vector_domain,
				       irq_desc_get_irq(desc));
	if (!irqd)
		return;

	raw_spin_lock(&vector_lock);
	apicd = apic_chip_data(irqd);
	if (!apicd)
		goto unlock;

	/*
	 * If prev_vector is empty, no action required.
	 */
	vector = apicd->prev_vector;
	if (!vector)
		goto unlock;

	/*
	 * This is tricky. If the cleanup of the old vector has not been
	 * done yet, then the following setaffinity call will fail with
	 * -EBUSY. This can leave the interrupt in a stale state.
	 *
	 * All CPUs are stuck in stop machine with interrupts disabled so
	 * calling __irq_complete_move() would be completely pointless.
	 *
	 * 1) The interrupt is in move_in_progress state. That means that we
	 *    have not seen an interrupt since the io_apic was reprogrammed to
	 *    the new vector.
	 *
	 * 2) The interrupt has fired on the new vector, but the cleanup IPIs
	 *    have not been processed yet.
	 */
	if (apicd->move_in_progress) {
		/*
		 * In theory there is a race:
		 *
		 * set_ioapic(new_vector) <-- Interrupt is raised before update
		 *			      is effective, i.e. it's raised on
		 *			      the old vector.
		 *
		 * So if the target cpu cannot handle that interrupt before
		 * the old vector is cleaned up, we get a spurious interrupt
		 * and in the worst case the ioapic irq line becomes stale.
		 *
		 * But in case of cpu hotplug this should be a non issue
		 * because if the affinity update happens right before all
		 * cpus rendevouz in stop machine, there is no way that the
		 * interrupt can be blocked on the target cpu because all cpus
		 * loops first with interrupts enabled in stop machine, so the
		 * old vector is not yet cleaned up when the interrupt fires.
		 *
		 * So the only way to run into this issue is if the delivery
		 * of the interrupt on the apic/system bus would be delayed
		 * beyond the point where the target cpu disables interrupts
		 * in stop machine. I doubt that it can happen, but at least
		 * there is a theroretical chance. Virtualization might be
		 * able to expose this, but AFAICT the IOAPIC emulation is not
		 * as stupid as the real hardware.
		 *
		 * Anyway, there is nothing we can do about that at this point
		 * w/o refactoring the whole fixup_irq() business completely.
		 * We print at least the irq number and the old vector number,
		 * so we have the necessary information when a problem in that
		 * area arises.
		 */
		pr_warn("IRQ fixup: irq %d move in progress, old vector %d\n",
			irqd->irq, vector);
	}
	free_moved_vector(apicd);
unlock:
	raw_spin_unlock(&vector_lock);
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Note, this is not accurate accounting, but at least good enough to
 * prevent that the actual interrupt move will run out of vectors.
 */
int lapic_can_unplug_cpu(void)
{
	unsigned int rsvd, avl, tomove, cpu = smp_processor_id();
	int ret = 0;

	raw_spin_lock(&vector_lock);
	tomove = irq_matrix_allocated(vector_matrix);
	avl = irq_matrix_available(vector_matrix, true);
	if (avl < tomove) {
		pr_warn("CPU %u has %u vectors, %u available. Cannot disable CPU\n",
			cpu, tomove, avl);
		ret = -ENOSPC;
		goto out;
	}
	rsvd = irq_matrix_reserved(vector_matrix);
	if (avl < rsvd) {
		pr_warn("Reserved vectors %u > available %u. IRQ request may fail\n",
			rsvd, avl);
	}
out:
	raw_spin_unlock(&vector_lock);
	return ret;
}
#endif /* HOTPLUG_CPU */
#endif /* SMP */

static void __init print_APIC_field(int base)
{
	int i;

	printk(KERN_DEBUG);

	for (i = 0; i < 8; i++)
		pr_cont("%08x", apic_read(base + i*0x10));

	pr_cont("\n");
}

static void __init print_local_APIC(void *dummy)
{
	unsigned int i, v, ver, maxlvt;
	u64 icr;

	pr_debug("printing local APIC contents on CPU#%d/%d:\n",
		 smp_processor_id(), hard_smp_processor_id());
	v = apic_read(APIC_ID);
	pr_info("... APIC ID:      %08x (%01x)\n", v, read_apic_id());
	v = apic_read(APIC_LVR);
	pr_info("... APIC VERSION: %08x\n", v);
	ver = GET_APIC_VERSION(v);
	maxlvt = lapic_get_maxlvt();

	v = apic_read(APIC_TASKPRI);
	pr_debug("... APIC TASKPRI: %08x (%02x)\n", v, v & APIC_TPRI_MASK);

	/* !82489DX */
	if (APIC_INTEGRATED(ver)) {
		if (!APIC_XAPIC(ver)) {
			v = apic_read(APIC_ARBPRI);
			pr_debug("... APIC ARBPRI: %08x (%02x)\n",
				 v, v & APIC_ARBPRI_MASK);
		}
		v = apic_read(APIC_PROCPRI);
		pr_debug("... APIC PROCPRI: %08x\n", v);
	}

	/*
	 * Remote read supported only in the 82489DX and local APIC for
	 * Pentium processors.
	 */
	if (!APIC_INTEGRATED(ver) || maxlvt == 3) {
		v = apic_read(APIC_RRR);
		pr_debug("... APIC RRR: %08x\n", v);
	}

	v = apic_read(APIC_LDR);
	pr_debug("... APIC LDR: %08x\n", v);
	if (!x2apic_enabled()) {
		v = apic_read(APIC_DFR);
		pr_debug("... APIC DFR: %08x\n", v);
	}
	v = apic_read(APIC_SPIV);
	pr_debug("... APIC SPIV: %08x\n", v);

	pr_debug("... APIC ISR field:\n");
	print_APIC_field(APIC_ISR);
	pr_debug("... APIC TMR field:\n");
	print_APIC_field(APIC_TMR);
	pr_debug("... APIC IRR field:\n");
	print_APIC_field(APIC_IRR);

	/* !82489DX */
	if (APIC_INTEGRATED(ver)) {
		/* Due to the Pentium erratum 3AP. */
		if (maxlvt > 3)
			apic_write(APIC_ESR, 0);

		v = apic_read(APIC_ESR);
		pr_debug("... APIC ESR: %08x\n", v);
	}

	icr = apic_icr_read();
	pr_debug("... APIC ICR: %08x\n", (u32)icr);
	pr_debug("... APIC ICR2: %08x\n", (u32)(icr >> 32));

	v = apic_read(APIC_LVTT);
	pr_debug("... APIC LVTT: %08x\n", v);

	if (maxlvt > 3) {
		/* PC is LVT#4. */
		v = apic_read(APIC_LVTPC);
		pr_debug("... APIC LVTPC: %08x\n", v);
	}
	v = apic_read(APIC_LVT0);
	pr_debug("... APIC LVT0: %08x\n", v);
	v = apic_read(APIC_LVT1);
	pr_debug("... APIC LVT1: %08x\n", v);

	if (maxlvt > 2) {
		/* ERR is LVT#3. */
		v = apic_read(APIC_LVTERR);
		pr_debug("... APIC LVTERR: %08x\n", v);
	}

	v = apic_read(APIC_TMICT);
	pr_debug("... APIC TMICT: %08x\n", v);
	v = apic_read(APIC_TMCCT);
	pr_debug("... APIC TMCCT: %08x\n", v);
	v = apic_read(APIC_TDCR);
	pr_debug("... APIC TDCR: %08x\n", v);

	if (boot_cpu_has(X86_FEATURE_EXTAPIC)) {
		v = apic_read(APIC_EFEAT);
		maxlvt = (v >> 16) & 0xff;
		pr_debug("... APIC EFEAT: %08x\n", v);
		v = apic_read(APIC_ECTRL);
		pr_debug("... APIC ECTRL: %08x\n", v);
		for (i = 0; i < maxlvt; i++) {
			v = apic_read(APIC_EILVTn(i));
			pr_debug("... APIC EILVT%d: %08x\n", i, v);
		}
	}
	pr_cont("\n");
}

static void __init print_local_APICs(int maxcpu)
{
	int cpu;

	if (!maxcpu)
		return;

	preempt_disable();
	for_each_online_cpu(cpu) {
		if (cpu >= maxcpu)
			break;
		smp_call_function_single(cpu, print_local_APIC, NULL, 1);
	}
	preempt_enable();
}

static void __init print_PIC(void)
{
	unsigned int v;
	unsigned long flags;

	if (!nr_legacy_irqs())
		return;

	pr_debug("\nprinting PIC contents\n");

	raw_spin_lock_irqsave(&i8259A_lock, flags);

	v = inb(0xa1) << 8 | inb(0x21);
	pr_debug("... PIC  IMR: %04x\n", v);

	v = inb(0xa0) << 8 | inb(0x20);
	pr_debug("... PIC  IRR: %04x\n", v);

	outb(0x0b, 0xa0);
	outb(0x0b, 0x20);
	v = inb(0xa0) << 8 | inb(0x20);
	outb(0x0a, 0xa0);
	outb(0x0a, 0x20);

	raw_spin_unlock_irqrestore(&i8259A_lock, flags);

	pr_debug("... PIC  ISR: %04x\n", v);

	v = inb(0x4d1) << 8 | inb(0x4d0);
	pr_debug("... PIC ELCR: %04x\n", v);
}

static int show_lapic __initdata = 1;
static __init int setup_show_lapic(char *arg)
{
	int num = -1;

	if (strcmp(arg, "all") == 0) {
		show_lapic = CONFIG_NR_CPUS;
	} else {
		get_option(&arg, &num);
		if (num >= 0)
			show_lapic = num;
	}

	return 1;
}
__setup("show_lapic=", setup_show_lapic);

static int __init print_ICs(void)
{
	if (apic_verbosity == APIC_QUIET)
		return 0;

	print_PIC();

	/* don't print out if apic is not there */
	if (!boot_cpu_has(X86_FEATURE_APIC) && !apic_from_smp_config())
		return 0;

	print_local_APICs(show_lapic);
	print_IO_APICs();

	return 0;
}

late_initcall(print_ICs);
