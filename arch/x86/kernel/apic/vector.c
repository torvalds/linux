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
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <asm/irqdomain.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/i8259.h>
#include <asm/desc.h>
#include <asm/irq_remapping.h>

struct apic_chip_data {
	struct irq_cfg		cfg;
	cpumask_var_t		domain;
	cpumask_var_t		old_domain;
	u8			move_in_progress : 1;
};

struct irq_domain *x86_vector_domain;
EXPORT_SYMBOL_GPL(x86_vector_domain);
static DEFINE_RAW_SPINLOCK(vector_lock);
static cpumask_var_t vector_cpumask, vector_searchmask, searched_cpumask;
static struct irq_chip lapic_controller;
#ifdef	CONFIG_X86_IO_APIC
static struct apic_chip_data *legacy_irq_data[NR_IRQS_LEGACY];
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

static struct apic_chip_data *apic_chip_data(struct irq_data *irq_data)
{
	if (!irq_data)
		return NULL;

	while (irq_data->parent_data)
		irq_data = irq_data->parent_data;

	return irq_data->chip_data;
}

struct irq_cfg *irqd_cfg(struct irq_data *irq_data)
{
	struct apic_chip_data *data = apic_chip_data(irq_data);

	return data ? &data->cfg : NULL;
}
EXPORT_SYMBOL_GPL(irqd_cfg);

struct irq_cfg *irq_cfg(unsigned int irq)
{
	return irqd_cfg(irq_get_irq_data(irq));
}

static struct apic_chip_data *alloc_apic_chip_data(int node)
{
	struct apic_chip_data *data;

	data = kzalloc_node(sizeof(*data), GFP_KERNEL, node);
	if (!data)
		return NULL;
	if (!zalloc_cpumask_var_node(&data->domain, GFP_KERNEL, node))
		goto out_data;
	if (!zalloc_cpumask_var_node(&data->old_domain, GFP_KERNEL, node))
		goto out_domain;
	return data;
out_domain:
	free_cpumask_var(data->domain);
out_data:
	kfree(data);
	return NULL;
}

static void free_apic_chip_data(struct apic_chip_data *data)
{
	if (data) {
		free_cpumask_var(data->domain);
		free_cpumask_var(data->old_domain);
		kfree(data);
	}
}

static int __assign_irq_vector(int irq, struct apic_chip_data *d,
			       const struct cpumask *mask)
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
	if (d->move_in_progress ||
	    cpumask_intersects(d->old_domain, cpu_online_mask))
		return -EBUSY;

	/* Only try and allocate irqs on cpus that are present */
	cpumask_clear(d->old_domain);
	cpumask_clear(searched_cpumask);
	cpu = cpumask_first_and(mask, cpu_online_mask);
	while (cpu < nr_cpu_ids) {
		int new_cpu, offset;

		/* Get the possible target cpus for @mask/@cpu from the apic */
		apic->vector_allocation_domain(cpu, vector_cpumask, mask);

		/*
		 * Clear the offline cpus from @vector_cpumask for searching
		 * and verify whether the result overlaps with @mask. If true,
		 * then the call to apic->cpu_mask_to_apicid_and() will
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
		if (vector >= first_system_vector) {
			offset = (offset + 1) % 16;
			vector = FIRST_EXTERNAL_VECTOR + offset;
		}

		/* If the search wrapped around, try the next cpu */
		if (unlikely(current_vector == vector))
			goto next_cpu;

		if (test_bit(vector, used_vectors))
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
	d->cfg.vector = vector;
	cpumask_copy(d->domain, vector_cpumask);
success:
	/*
	 * Cache destination APIC IDs into cfg->dest_apicid. This cannot fail
	 * as we already established, that mask & d->domain & cpu_online_mask
	 * is not empty.
	 */
	BUG_ON(apic->cpu_mask_to_apicid_and(mask, d->domain,
					    &d->cfg.dest_apicid));
	return 0;
}

static int assign_irq_vector(int irq, struct apic_chip_data *data,
			     const struct cpumask *mask)
{
	int err;
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);
	err = __assign_irq_vector(irq, data, mask);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return err;
}

static int assign_irq_vector_policy(int irq, int node,
				    struct apic_chip_data *data,
				    struct irq_alloc_info *info)
{
	if (info && info->mask)
		return assign_irq_vector(irq, data, info->mask);
	if (node != NUMA_NO_NODE &&
	    assign_irq_vector(irq, data, cpumask_of_node(node)) == 0)
		return 0;
	return assign_irq_vector(irq, data, apic->target_cpus());
}

static void clear_irq_vector(int irq, struct apic_chip_data *data)
{
	struct irq_desc *desc;
	int cpu, vector;

	if (!data->cfg.vector)
		return;

	vector = data->cfg.vector;
	for_each_cpu_and(cpu, data->domain, cpu_online_mask)
		per_cpu(vector_irq, cpu)[vector] = VECTOR_UNUSED;

	data->cfg.vector = 0;
	cpumask_clear(data->domain);

	/*
	 * If move is in progress or the old_domain mask is not empty,
	 * i.e. the cleanup IPI has not been processed yet, we need to remove
	 * the old references to desc from all cpus vector tables.
	 */
	if (!data->move_in_progress && cpumask_empty(data->old_domain))
		return;

	desc = irq_to_desc(irq);
	for_each_cpu_and(cpu, data->old_domain, cpu_online_mask) {
		for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS;
		     vector++) {
			if (per_cpu(vector_irq, cpu)[vector] != desc)
				continue;
			per_cpu(vector_irq, cpu)[vector] = VECTOR_UNUSED;
			break;
		}
	}
	data->move_in_progress = 0;
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

static void x86_vector_free_irqs(struct irq_domain *domain,
				 unsigned int virq, unsigned int nr_irqs)
{
	struct apic_chip_data *apic_data;
	struct irq_data *irq_data;
	unsigned long flags;
	int i;

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(x86_vector_domain, virq + i);
		if (irq_data && irq_data->chip_data) {
			raw_spin_lock_irqsave(&vector_lock, flags);
			clear_irq_vector(virq + i, irq_data->chip_data);
			apic_data = irq_data->chip_data;
			irq_domain_reset_irq_data(irq_data);
			raw_spin_unlock_irqrestore(&vector_lock, flags);
			free_apic_chip_data(apic_data);
#ifdef	CONFIG_X86_IO_APIC
			if (virq + i < nr_legacy_irqs())
				legacy_irq_data[virq + i] = NULL;
#endif
		}
	}
}

static int x86_vector_alloc_irqs(struct irq_domain *domain, unsigned int virq,
				 unsigned int nr_irqs, void *arg)
{
	struct irq_alloc_info *info = arg;
	struct apic_chip_data *data;
	struct irq_data *irq_data;
	int i, err, node;

	if (disable_apic)
		return -ENXIO;

	/* Currently vector allocator can't guarantee contiguous allocations */
	if ((info->flags & X86_IRQ_ALLOC_CONTIGUOUS_VECTORS) && nr_irqs > 1)
		return -ENOSYS;

	for (i = 0; i < nr_irqs; i++) {
		irq_data = irq_domain_get_irq_data(domain, virq + i);
		BUG_ON(!irq_data);
		node = irq_data_get_node(irq_data);
#ifdef	CONFIG_X86_IO_APIC
		if (virq + i < nr_legacy_irqs() && legacy_irq_data[virq + i])
			data = legacy_irq_data[virq + i];
		else
#endif
			data = alloc_apic_chip_data(node);
		if (!data) {
			err = -ENOMEM;
			goto error;
		}

		irq_data->chip = &lapic_controller;
		irq_data->chip_data = data;
		irq_data->hwirq = virq + i;
		err = assign_irq_vector_policy(virq + i, node, data, info);
		if (err)
			goto error;
	}

	return 0;

error:
	x86_vector_free_irqs(domain, virq, i + 1);
	return err;
}

static const struct irq_domain_ops x86_vector_domain_ops = {
	.alloc	= x86_vector_alloc_irqs,
	.free	= x86_vector_free_irqs,
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

#ifdef	CONFIG_X86_IO_APIC
static void init_legacy_irqs(void)
{
	int i, node = cpu_to_node(0);
	struct apic_chip_data *data;

	/*
	 * For legacy IRQ's, start with assigning irq0 to irq15 to
	 * ISA_IRQ_VECTOR(i) for all cpu's.
	 */
	for (i = 0; i < nr_legacy_irqs(); i++) {
		data = legacy_irq_data[i] = alloc_apic_chip_data(node);
		BUG_ON(!data);

		data->cfg.vector = ISA_IRQ_VECTOR(i);
		cpumask_setall(data->domain);
		irq_set_chip_data(i, data);
	}
}
#else
static void init_legacy_irqs(void) { }
#endif

int __init arch_early_irq_init(void)
{
	init_legacy_irqs();

	x86_vector_domain = irq_domain_add_tree(NULL, &x86_vector_domain_ops,
						NULL);
	BUG_ON(x86_vector_domain == NULL);
	irq_set_default_host(x86_vector_domain);

	arch_init_msi_domain(x86_vector_domain);
	arch_init_htirq_domain(x86_vector_domain);

	BUG_ON(!alloc_cpumask_var(&vector_cpumask, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&vector_searchmask, GFP_KERNEL));
	BUG_ON(!alloc_cpumask_var(&searched_cpumask, GFP_KERNEL));

	return arch_early_ioapic_init();
}

/* Initialize vector_irq on a new cpu */
static void __setup_vector_irq(int cpu)
{
	struct apic_chip_data *data;
	struct irq_desc *desc;
	int irq, vector;

	/* Mark the inuse vectors */
	for_each_irq_desc(irq, desc) {
		struct irq_data *idata = irq_desc_get_irq_data(desc);

		data = apic_chip_data(idata);
		if (!data || !cpumask_test_cpu(cpu, data->domain))
			continue;
		vector = data->cfg.vector;
		per_cpu(vector_irq, cpu)[vector] = desc;
	}
	/* Mark the free vectors */
	for (vector = 0; vector < NR_VECTORS; ++vector) {
		desc = per_cpu(vector_irq, cpu)[vector];
		if (IS_ERR_OR_NULL(desc))
			continue;

		data = apic_chip_data(irq_desc_get_irq_data(desc));
		if (!cpumask_test_cpu(cpu, data->domain))
			per_cpu(vector_irq, cpu)[vector] = VECTOR_UNUSED;
	}
}

/*
 * Setup the vector to irq mappings. Must be called with vector_lock held.
 */
void setup_vector_irq(int cpu)
{
	int irq;

	lockdep_assert_held(&vector_lock);
	/*
	 * On most of the platforms, legacy PIC delivers the interrupts on the
	 * boot cpu. But there are certain platforms where PIC interrupts are
	 * delivered to multiple cpu's. If the legacy IRQ is handled by the
	 * legacy PIC, for the new cpu that is coming online, setup the static
	 * legacy vector to irq mapping:
	 */
	for (irq = 0; irq < nr_legacy_irqs(); irq++)
		per_cpu(vector_irq, cpu)[ISA_IRQ_VECTOR(irq)] = irq_to_desc(irq);

	__setup_vector_irq(cpu);
}

static int apic_retrigger_irq(struct irq_data *irq_data)
{
	struct apic_chip_data *data = apic_chip_data(irq_data);
	unsigned long flags;
	int cpu;

	raw_spin_lock_irqsave(&vector_lock, flags);
	cpu = cpumask_first_and(data->domain, cpu_online_mask);
	apic->send_IPI_mask(cpumask_of(cpu), data->cfg.vector);
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	return 1;
}

void apic_ack_edge(struct irq_data *data)
{
	irq_complete_move(irqd_cfg(data));
	irq_move_irq(data);
	ack_APIC_irq();
}

static int apic_set_affinity(struct irq_data *irq_data,
			     const struct cpumask *dest, bool force)
{
	struct apic_chip_data *data = irq_data->chip_data;
	int err, irq = irq_data->irq;

	if (!IS_ENABLED(CONFIG_SMP))
		return -EPERM;

	if (!cpumask_intersects(dest, cpu_online_mask))
		return -EINVAL;

	err = assign_irq_vector(irq, data, dest);
	return err ? err : IRQ_SET_MASK_OK;
}

static struct irq_chip lapic_controller = {
	.irq_ack		= apic_ack_edge,
	.irq_set_affinity	= apic_set_affinity,
	.irq_retrigger		= apic_retrigger_irq,
};

#ifdef CONFIG_SMP
static void __send_cleanup_vector(struct apic_chip_data *data)
{
	raw_spin_lock(&vector_lock);
	cpumask_and(data->old_domain, data->old_domain, cpu_online_mask);
	data->move_in_progress = 0;
	if (!cpumask_empty(data->old_domain))
		apic->send_IPI_mask(data->old_domain, IRQ_MOVE_CLEANUP_VECTOR);
	raw_spin_unlock(&vector_lock);
}

void send_cleanup_vector(struct irq_cfg *cfg)
{
	struct apic_chip_data *data;

	data = container_of(cfg, struct apic_chip_data, cfg);
	if (data->move_in_progress)
		__send_cleanup_vector(data);
}

asmlinkage __visible void smp_irq_move_cleanup_interrupt(void)
{
	unsigned vector, me;

	entering_ack_irq();

	/* Prevent vectors vanishing under us */
	raw_spin_lock(&vector_lock);

	me = smp_processor_id();
	for (vector = FIRST_EXTERNAL_VECTOR; vector < NR_VECTORS; vector++) {
		struct apic_chip_data *data;
		struct irq_desc *desc;
		unsigned int irr;

	retry:
		desc = __this_cpu_read(vector_irq[vector]);
		if (IS_ERR_OR_NULL(desc))
			continue;

		if (!raw_spin_trylock(&desc->lock)) {
			raw_spin_unlock(&vector_lock);
			cpu_relax();
			raw_spin_lock(&vector_lock);
			goto retry;
		}

		data = apic_chip_data(irq_desc_get_irq_data(desc));
		if (!data)
			goto unlock;

		/*
		 * Nothing to cleanup if irq migration is in progress
		 * or this cpu is not set in the cleanup mask.
		 */
		if (data->move_in_progress ||
		    !cpumask_test_cpu(me, data->old_domain))
			goto unlock;

		/*
		 * We have two cases to handle here:
		 * 1) vector is unchanged but the target mask got reduced
		 * 2) vector and the target mask has changed
		 *
		 * #1 is obvious, but in #2 we have two vectors with the same
		 * irq descriptor: the old and the new vector. So we need to
		 * make sure that we only cleanup the old vector. The new
		 * vector has the current @vector number in the config and
		 * this cpu is part of the target mask. We better leave that
		 * one alone.
		 */
		if (vector == data->cfg.vector &&
		    cpumask_test_cpu(me, data->domain))
			goto unlock;

		irr = apic_read(APIC_IRR + (vector / 32 * 0x10));
		/*
		 * Check if the vector that needs to be cleanedup is
		 * registered at the cpu's IRR. If so, then this is not
		 * the best time to clean it up. Lets clean it up in the
		 * next attempt by sending another IRQ_MOVE_CLEANUP_VECTOR
		 * to myself.
		 */
		if (irr  & (1 << (vector % 32))) {
			apic->send_IPI_self(IRQ_MOVE_CLEANUP_VECTOR);
			goto unlock;
		}
		__this_cpu_write(vector_irq[vector], VECTOR_UNUSED);
		cpumask_clear_cpu(me, data->old_domain);
unlock:
		raw_spin_unlock(&desc->lock);
	}

	raw_spin_unlock(&vector_lock);

	exiting_irq();
}

static void __irq_complete_move(struct irq_cfg *cfg, unsigned vector)
{
	unsigned me;
	struct apic_chip_data *data;

	data = container_of(cfg, struct apic_chip_data, cfg);
	if (likely(!data->move_in_progress))
		return;

	me = smp_processor_id();
	if (vector == data->cfg.vector && cpumask_test_cpu(me, data->domain))
		__send_cleanup_vector(data);
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
	struct irq_data *irqdata = irq_desc_get_irq_data(desc);
	struct apic_chip_data *data = apic_chip_data(irqdata);
	struct irq_cfg *cfg = data ? &data->cfg : NULL;
	unsigned int cpu;

	if (!cfg)
		return;

	/*
	 * This is tricky. If the cleanup of @data->old_domain has not been
	 * done yet, then the following setaffinity call will fail with
	 * -EBUSY. This can leave the interrupt in a stale state.
	 *
	 * All CPUs are stuck in stop machine with interrupts disabled so
	 * calling __irq_complete_move() would be completely pointless.
	 */
	raw_spin_lock(&vector_lock);
	/*
	 * Clean out all offline cpus (including the outgoing one) from the
	 * old_domain mask.
	 */
	cpumask_and(data->old_domain, data->old_domain, cpu_online_mask);

	/*
	 * If move_in_progress is cleared and the old_domain mask is empty,
	 * then there is nothing to cleanup. fixup_irqs() will take care of
	 * the stale vectors on the outgoing cpu.
	 */
	if (!data->move_in_progress && cpumask_empty(data->old_domain)) {
		raw_spin_unlock(&vector_lock);
		return;
	}

	/*
	 * 1) The interrupt is in move_in_progress state. That means that we
	 *    have not seen an interrupt since the io_apic was reprogrammed to
	 *    the new vector.
	 *
	 * 2) The interrupt has fired on the new vector, but the cleanup IPIs
	 *    have not been processed yet.
	 */
	if (data->move_in_progress) {
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
			irqdata->irq, cfg->old_vector);
	}
	/*
	 * If old_domain is not empty, then other cpus still have the irq
	 * descriptor set in their vector array. Clean it up.
	 */
	for_each_cpu(cpu, data->old_domain)
		per_cpu(vector_irq, cpu)[cfg->old_vector] = VECTOR_UNUSED;

	/* Cleanup the left overs of the (half finished) move */
	cpumask_clear(data->old_domain);
	data->move_in_progress = 0;
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
