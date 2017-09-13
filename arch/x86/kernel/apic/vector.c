/*
 * Local APIC related interfaces to support IOAPIC, MSI, HT_IRQ etc.
 *
 * Copyright (C) 1997, 1998, 1999, 2000, 2009 Ingo Molnar, Hajnalka Szabo
 *	Moved from arch/x86/kernel/apic/io_apic.c.
 * Jiang Liu <jiang.liu@linux.intel.com>
 *	Enable support of hierarchical irqdomains
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <asm/irqdomain.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/i8259.h>
#include <asm/desc.h>
#include <asm/irq_remapping.h>

#include <asm/trace/irq_vectors.h>

struct apic_chip_data {
	struct irq_cfg		cfg;
	unsigned int		cpu;
	unsigned int		prev_cpu;
	struct hlist_node	clist;
	cpumask_var_t		domain;
	cpumask_var_t		old_domain;
	u8			move_in_progress : 1;
};

struct irq_domain *x86_vector_domain;
EXPORT_SYMBOL_GPL(x86_vector_domain);
static DEFINE_RAW_SPINLOCK(vector_lock);
static cpumask_var_t vector_cpumask, vector_searchmask, searched_cpumask;
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

	return apicd ? &apicd->cfg : NULL;
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
	if (!apicd)
		return NULL;
	if (!zalloc_cpumask_var_node(&apicd->domain, GFP_KERNEL, node))
		goto out_data;
	if (!zalloc_cpumask_var_node(&apicd->old_domain, GFP_KERNEL, node))
		goto out_domain;
	INIT_HLIST_NODE(&apicd->clist);
	return apicd;
out_domain:
	free_cpumask_var(apicd->domain);
out_data:
	kfree(apicd);
	return NULL;
}

static void free_apic_chip_data(struct apic_chip_data *apicd)
{
	if (apicd) {
		free_cpumask_var(apicd->domain);
		free_cpumask_var(apicd->old_domain);
		kfree(apicd);
	}
}

static int __assign_irq_vector(int irq, struct apic_chip_data *d,
			       const struct cpumask *mask,
			       struct irq_data *irqd)
{
	/*
	 * NOTE! The local APIC isn't very good at handling
	 * multiple interrupts at the same interrupt level.
	 * As the interrupt level is determined by taking the
	 * vector number and shifting that right by 4, we
	 * want to spread these out a bit so that they don't
	 * all fall in the same interrupt level.
	 *
	 * Also, we've got to be careful not to trash gate
	 * 0x80, because int 0x80 is hm, kind of importantish. ;)
	 */
	static int current_vector = FIRST_EXTERNAL_VECTOR + VECTOR_OFFSET_START;
	static int current_offset = VECTOR_OFFSET_START % 16;
	int cpu, vector;

	/*
	 * If there is still a move in progress or the previous move has not
	 * been cleaned up completely, tell the caller to come back later.
	 */
	if (d->cfg.old_vector)
		return -EBUSY;

	/* Only try and allocate irqs on cpus that are present */
	cpumask_clear(d->old_domain);
	cpumask_clear(searched_cpumask);
	cpu = cpumask_first_and(mask, cpu_online_mask);
	while (cpu < nr_cpu_ids) {
		int new_cpu, offset;

		cpumask_copy(vector_cpumask, cpumask_of(cpu));

		/*
		 * Clear the offline cpus from @vector_cpumask for searching
		 * and verify whether the result overlaps with @mask. If true,
		 * then the call to apic->cpu_mask_to_apicid() will
		 * succeed as well. If not, no point in trying to find a
		 * vector in this mask.
		 */
		cpumask_and(vector_searchmask, vector_cpumask, cpu_online_mask);
		if (!cpumask_intersects(vector_searchmask, mask))
			goto next_cpu;

		if (cpumask_subset(vector_cpumask, d->domain)) {
			if (cpumask_equal(vector_cpumask, d->domain))
				goto success;
			/*
			 * Mark the cpus which are not longer in the mask for
			 * cleanup.
			 */
			cpumask_andnot(d->old_domain, d->domain, vector_cpumask);
			vector = d->cfg.vector;
			goto update;
		}

		vector = current_vector;
		offset = current_offset;
next:
		vector += 16;
		if (vector >= FIRST_SYSTEM_VECTOR) {
			offset = (offset + 1) % 16;
			vector = FIRST_EXTERNAL_VECTOR + offset;
		}

		/* If the search wrapped around, try the next cpu */
		if (unlikely(current_vector == vector))
			goto next_cpu;

		if (test_bit(vector, system_vectors))
			goto next;

		for_each_cpu(new_cpu, vector_searchmask) {
			if (!IS_ERR_OR_NULL(per_cpu(vector_irq, new_cpu)[vector]))
				goto next;
		}
		/* Found one! */
		current_vector = vector;
		current_offset = offset;
		/* Schedule the old vector for cleanup on all cpus */
		if (d->cfg.vector)
			cpumask_copy(d->old_domain, d->domain);
		for_each_cpu(new_cpu, vector_searchmask)
			per_cpu(vector_irq, new_cpu)[vector] = irq_to_desc(irq);
		goto update;

next_cpu:
		/*
		 * We exclude the current @vector_cpumask from the requested
		 * @mask and try again with the next online cpu in the
		 * result. We cannot modify @mask, so we use @vector_cpumask
		 * as a temporary buffer here as it will be reassigned when
		 * calling apic->vector_allocation_domain() above.
		 */
		cpumask_or(searched_cpumask, searched_cpumask, vector_cpumask);
		cpumask_andnot(vector_cpumask, mask, searched_cpumask);
		cpu = cpumask_first_and(vector_cpumask, cpu_online_mask);
		continue;
	}
	return -ENOSPC;

update:
	/*
	 * Exclude offline cpus from the cleanup mask and set the
	 * move_in_progress flag when the result is not empty.
	 */
	cpumask_and(d->old_domain, d->old_domain, cpu_online_mask);
	d->move_in_progress = !cpumask_empty(d->old_domain);
	d->cfg.old_vector = d->move_in_progress ? d->cfg.vector : 0;
	d->prev_cpu = d->cpu;
	d->cfg.vector = vector;
	cpumask_copy(d->domain, vector_cpumask);
success:
	/*
	 * Cache destination APIC IDs into cfg->dest_apicid. This cannot fail
	 * as we already established, that mask & d->domain & cpu_online_mask
	 * is not empty.
	 *
	 * vector_searchmask is a subset of d->domain and has the offline
	 * cpus masked out.
	 */
	cpumask_and(vector_searchmask, vector_searchmask, mask);
	BUG_ON(apic->cpu_mask_to_apicid(vector_searchmask, irqd,
					&d->cfg.dest_apicid));
	d->cpu = cpumask_first(vector_searchmask);
	return 0;
}

static int assign_irq_vector(int irq, struct apic_chip_data *apicd,
			     const struct cpumask *mask,
			     struct irq_data *irqd)
{
	int err;
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	err = __assign_irq_vector(irq, apicd, mask, irqd);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return err;
}

static int assign_irq_vector_policy(int irq, int node,
				    struct apic_chip_data *apicd,
				    struct irq_alloc_info *info,
				    struct irq_data *irqd)
{
	if (info->mask)
		return assign_irq_vector(irq, apicd, info->mask, irqd);
	if (node != NUMA_NO_NODE &&
	    assign_irq_vector(irq, apicd, cpumask_of_node(node), irqd) == 0)
		return 0;
	return assign_irq_vector(irq, apicd, cpu_online_mask, irqd);
}

static void clear_irq_vector(int irq, struct apic_chip_data *apicd)
{
	unsigned int vector = apicd->cfg.vector;

	if (!vector)
		return;

	per_cpu(vector_irq, apicd->cpu)[vector] = VECTOR_UNUSED;
	apicd->cfg.vector = 0;

	/* Clean up move in progress */
	vector = apicd->cfg.old_vector;
	if (!vector)
		return;

	per_cpu(vector_irq, apicd->prev_cpu)[vector] = VECTOR_UNUSED;
	apicd->move_in_progress = 0;
	hlist_del_init(&apicd->clist);
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
			clear_irq_vector(virq + i, irqd->chip_data);
			apicd = irqd->chip_data;
			irq_domain_reset_irq_data(irqd);
			raw_spin_unlock_irqrestore(&vector_lock, flags);
			free_apic_chip_data(apicd);
		}
	}
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

		irqd->chip = &lapic_controller;
		irqd->chip_data = apicd;
		irqd->hwirq = virq + i;
		irqd_set_single_target(irqd);
		/*
		 * Make sure, that the legacy to IOAPIC transition stays on
		 * the same vector. This is required for check_timer() to
		 * work correctly as it might switch back to legacy mode.
		 */
		if (info->flags & X86_IRQ_ALLOC_LEGACY) {
			apicd->cfg.vector = ISA_IRQ_VECTOR(virq + i);
			apicd->cpu = 0;
			cpumask_copy(apicd->domain, cpumask_of(0));
		}

		err = assign_irq_vector_policy(virq + i, node, apicd, info,
					       irqd);
		if (err)
			goto error;
	}

	return 0;

error:
	x86_vector_free_irqs(domain, virq, i + 1);
	return err;
}

#ifdef CONFIG_GENERIC_IRQ_DEBUGFS
void x86_vector_debug_show(struct seq_file *m, struct irq_domain *d,
			   struct irq_data *irqd, int ind)
{
	unsigned int cpu, vec, prev_cpu, prev_vec;
	struct apic_chip_data *apicd;
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

	apicd = irqd->chip_data;
	if (!apicd) {
		seq_printf(m, "%*sVector: Not assigned\n", ind, "");
		return;
	}

	raw_spin_lock_irqsave(&vector_lock, flags);
	cpu = apicd->cpu;
	vec = apicd->cfg.vector;
	prev_cpu = apicd->prev_cpu;
	prev_vec = apicd->cfg.old_vector;
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	seq_printf(m, "%*sVector: %5u\n", ind, "", vec);
	seq_printf(m, "%*sTarget: %5u\n", ind, "", cpu);
	if (prev_vec) {
		seq_printf(m, "%*sPrevious vector: %5u\n", ind, "", prev_vec);
		seq_printf(m, "%*sPrevious target: %5u\n", ind, "", prev_cpu);
	}
}
#endif

static const struct irq_domain_ops x86_vector_domain_ops = {
	.alloc		= x86_vector_alloc_irqs,
	.free		= x86_vector_free_irqs,
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
#if defined(CONFIG_PCI_MSI) || defined(CONFIG_HT_IRQ)
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
	irq_domain_free_fwnode(fn);
	irq_set_default_host(x86_vector_domain);

	arch_init_msi_domain(x86_vector_domain);
	arch_init_htirq_domain(x86_vector_domain);

	BUG_ON(!alloc_cpumask_var(&vector_cpumask, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&vector_searchmask, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&searched_cpumask, GFP_KERNEL));

	/*
	 * Allocate the vector matrix allocator data structure and limit the
	 * search area.
	 */
	vector_matrix = irq_alloc_matrix(NR_VECTORS, FIRST_EXTERNAL_VECTOR,
					 FIRST_SYSTEM_VECTOR);
	BUG_ON(!vector_matrix);

	return arch_early_ioapic_init();
}

/* Temporary hack to keep things working */
static void vector_update_shutdown_irqs(void)
{
	struct irq_desc *desc;
	int irq;

	for_each_irq_desc(irq, desc) {
		struct irq_data *irqd = irq_desc_get_irq_data(desc);
		struct apic_chip_data *ad = apic_chip_data(irqd);

		if (ad && ad->cfg.vector && ad->cpu == smp_processor_id())
			this_cpu_write(vector_irq[ad->cfg.vector], desc);
	}
}

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

	/*
	 * Until the rewrite of the managed interrupt management is in
	 * place it's necessary to walk the irq descriptors and check for
	 * interrupts which are targeted at this CPU.
	 */
	vector_update_shutdown_irqs();
}

void lapic_offline(void)
{
	lock_vector_lock();
	irq_matrix_offline(vector_matrix);
	unlock_vector_lock();
}

static int apic_retrigger_irq(struct irq_data *irqd)
{
	struct apic_chip_data *apicd = apic_chip_data(irqd);
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	apic->send_IPI(apicd->cpu, apicd->cfg.vector);
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	return 1;
}

void apic_ack_edge(struct irq_data *irqd)
{
	irq_complete_move(irqd_cfg(irqd));
	irq_move_irq(irqd);
	ack_APIC_irq();
}

static int apic_set_affinity(struct irq_data *irqd,
			     const struct cpumask *dest, bool force)
{
	struct apic_chip_data *apicd = irqd->chip_data;
	int err, irq = irqd->irq;

	if (!IS_ENABLED(CONFIG_SMP))
		return -EPERM;

	if (!cpumask_intersects(dest, cpu_online_mask))
		return -EINVAL;

	err = assign_irq_vector(irq, apicd, dest, irqd);
	return err ? err : IRQ_SET_MASK_OK;
}

static struct irq_chip lapic_controller = {
	.name			= "APIC",
	.irq_ack		= apic_ack_edge,
	.irq_set_affinity	= apic_set_affinity,
	.irq_retrigger		= apic_retrigger_irq,
};

#ifdef CONFIG_SMP

asmlinkage __visible void __irq_entry smp_irq_move_cleanup_interrupt(void)
{
	struct hlist_head *clhead = this_cpu_ptr(&cleanup_list);
	struct apic_chip_data *apicd;
	struct hlist_node *tmp;

	entering_ack_irq();
	/* Prevent vectors vanishing under us */
	raw_spin_lock(&vector_lock);

	hlist_for_each_entry_safe(apicd, tmp, clhead, clist) {
		unsigned int irr, vector = apicd->cfg.old_vector;

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
		hlist_del_init(&apicd->clist);
		__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
		apicd->cfg.old_vector = 0;
	}

	raw_spin_unlock(&vector_lock);
	exiting_irq();
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
		apicd->cfg.old_vector = 0;
	}
	raw_spin_unlock(&vector_lock);
}

void send_cleanup_vector(struct irq_cfg *cfg)
{
	struct apic_chip_data *apicd;

	apicd = container_of(cfg, struct apic_chip_data, cfg);
	if (apicd->move_in_progress)
		__send_cleanup_vector(apicd);
}

static void __irq_complete_move(struct irq_cfg *cfg, unsigned vector)
{
	struct apic_chip_data *apicd;

	apicd = container_of(cfg, struct apic_chip_data, cfg);
	if (likely(!apicd->move_in_progress))
		return;

	if (vector == apicd->cfg.vector && apicd->cpu == smp_processor_id())
		__send_cleanup_vector(apicd);
}

void irq_complete_move(struct irq_cfg *cfg)
{
	__irq_complete_move(cfg, ~get_irq_regs()->orig_ax);
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
	 * If old_vector is empty, no action required.
	 */
	vector = apicd->cfg.old_vector;
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
	per_cpu(vector_irq, apicd->prev_cpu)[vector] = VECTOR_UNUSED;
	/* Cleanup the left overs of the (half finished) move */
	cpumask_clear(apicd->old_domain);
	apicd->cfg.old_vector = 0;
	apicd->move_in_progress = 0;
	hlist_del_init(&apicd->clist);
unlock:
	raw_spin_unlock(&vector_lock);
}
#endif

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
