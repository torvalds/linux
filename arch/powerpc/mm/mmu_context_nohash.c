/*
 * This file contains the routines for handling the MMU on those
 * PowerPC implementations where the MMU is not using the hash
 * table, such as 8xx, 4xx, BookE's etc...
 *
 * Copyright 2008 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                IBM Corp.
 *
 *  Derived from previous arch/powerpc/mm/mmu_context.c
 *  and arch/powerpc/include/asm/mmu_context.h
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * TODO:
 *
 *   - The global context lock will not scale very well
 *   - The maps should be dynamically allocated to allow for processors
 *     that support more PID bits at runtime
 *   - Implement flush_tlb_mm() by making the context stale and picking
 *     a new one
 *   - More aggressively clear stale map bits and maybe find some way to
 *     also clear mm->cpu_vm_mask bits when processes are migrated
 */

#undef DEBUG
#define DEBUG_STEAL_ONLY
#undef DEBUG_MAP_CONSISTENCY
/*#define DEBUG_CLAMP_LAST_CONTEXT   15 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/cpu.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

static unsigned int first_context, last_context;
static unsigned int next_context, nr_free_contexts;
static unsigned long *context_map;
static unsigned long *stale_map[NR_CPUS];
static struct mm_struct **context_mm;
static spinlock_t context_lock = SPIN_LOCK_UNLOCKED;

#define CTX_MAP_SIZE	\
	(sizeof(unsigned long) * (last_context / BITS_PER_LONG + 1))


/* Steal a context from a task that has one at the moment.
 *
 * This is used when we are running out of available PID numbers
 * on the processors.
 *
 * This isn't an LRU system, it just frees up each context in
 * turn (sort-of pseudo-random replacement :).  This would be the
 * place to implement an LRU scheme if anyone was motivated to do it.
 *  -- paulus
 *
 * For context stealing, we use a slightly different approach for
 * SMP and UP. Basically, the UP one is simpler and doesn't use
 * the stale map as we can just flush the local CPU
 *  -- benh
 */
#ifdef CONFIG_SMP
static unsigned int steal_context_smp(unsigned int id)
{
	struct mm_struct *mm;
	unsigned int cpu, max;

 again:
	max = last_context - first_context;

	/* Attempt to free next_context first and then loop until we manage */
	while (max--) {
		/* Pick up the victim mm */
		mm = context_mm[id];

		/* We have a candidate victim, check if it's active, on SMP
		 * we cannot steal active contexts
		 */
		if (mm->context.active) {
			id++;
			if (id > last_context)
				id = first_context;
			continue;
		}
		pr_debug("[%d] steal context %d from mm @%p\n",
			 smp_processor_id(), id, mm);

		/* Mark this mm has having no context anymore */
		mm->context.id = MMU_NO_CONTEXT;

		/* Mark it stale on all CPUs that used this mm */
		for_each_cpu(cpu, mm_cpumask(mm))
			__set_bit(id, stale_map[cpu]);
		return id;
	}

	/* This will happen if you have more CPUs than available contexts,
	 * all we can do here is wait a bit and try again
	 */
	spin_unlock(&context_lock);
	cpu_relax();
	spin_lock(&context_lock);
	goto again;
}
#endif  /* CONFIG_SMP */

/* Note that this will also be called on SMP if all other CPUs are
 * offlined, which means that it may be called for cpu != 0. For
 * this to work, we somewhat assume that CPUs that are onlined
 * come up with a fully clean TLB (or are cleaned when offlined)
 */
static unsigned int steal_context_up(unsigned int id)
{
	struct mm_struct *mm;
	int cpu = smp_processor_id();

	/* Pick up the victim mm */
	mm = context_mm[id];

	pr_debug("[%d] steal context %d from mm @%p\n", cpu, id, mm);

	/* Mark this mm has having no context anymore */
	mm->context.id = MMU_NO_CONTEXT;

	/* Flush the TLB for that context */
	local_flush_tlb_mm(mm);

	/* XXX This clear should ultimately be part of local_flush_tlb_mm */
	__clear_bit(id, stale_map[cpu]);

	return id;
}

#ifdef DEBUG_MAP_CONSISTENCY
static void context_check_map(void)
{
	unsigned int id, nrf, nact;

	nrf = nact = 0;
	for (id = first_context; id <= last_context; id++) {
		int used = test_bit(id, context_map);
		if (!used)
			nrf++;
		if (used != (context_mm[id] != NULL))
			pr_err("MMU: Context %d is %s and MM is %p !\n",
			       id, used ? "used" : "free", context_mm[id]);
		if (context_mm[id] != NULL)
			nact += context_mm[id]->context.active;
	}
	if (nrf != nr_free_contexts) {
		pr_err("MMU: Free context count out of sync ! (%d vs %d)\n",
		       nr_free_contexts, nrf);
		nr_free_contexts = nrf;
	}
	if (nact > num_online_cpus())
		pr_err("MMU: More active contexts than CPUs ! (%d vs %d)\n",
		       nact, num_online_cpus());
	if (first_context > 0 && !test_bit(0, context_map))
		pr_err("MMU: Context 0 has been freed !!!\n");
}
#else
static void context_check_map(void) { }
#endif

void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next)
{
	unsigned int id, cpu = smp_processor_id();
	unsigned long *map;

	/* No lockless fast path .. yet */
	spin_lock(&context_lock);

#ifndef DEBUG_STEAL_ONLY
	pr_debug("[%d] activating context for mm @%p, active=%d, id=%d\n",
		 cpu, next, next->context.active, next->context.id);
#endif

#ifdef CONFIG_SMP
	/* Mark us active and the previous one not anymore */
	next->context.active++;
	if (prev) {
#ifndef DEBUG_STEAL_ONLY
		pr_debug(" old context %p active was: %d\n",
			 prev, prev->context.active);
#endif
		WARN_ON(prev->context.active < 1);
		prev->context.active--;
	}
#endif /* CONFIG_SMP */

	/* If we already have a valid assigned context, skip all that */
	id = next->context.id;
	if (likely(id != MMU_NO_CONTEXT))
		goto ctxt_ok;

	/* We really don't have a context, let's try to acquire one */
	id = next_context;
	if (id > last_context)
		id = first_context;
	map = context_map;

	/* No more free contexts, let's try to steal one */
	if (nr_free_contexts == 0) {
#ifdef CONFIG_SMP
		if (num_online_cpus() > 1) {
			id = steal_context_smp(id);
			goto stolen;
		}
#endif /* CONFIG_SMP */
		id = steal_context_up(id);
		goto stolen;
	}
	nr_free_contexts--;

	/* We know there's at least one free context, try to find it */
	while (__test_and_set_bit(id, map)) {
		id = find_next_zero_bit(map, last_context+1, id);
		if (id > last_context)
			id = first_context;
	}
 stolen:
	next_context = id + 1;
	context_mm[id] = next;
	next->context.id = id;

#ifndef DEBUG_STEAL_ONLY
	pr_debug("[%d] picked up new id %d, nrf is now %d\n",
		 cpu, id, nr_free_contexts);
#endif

	context_check_map();
 ctxt_ok:

	/* If that context got marked stale on this CPU, then flush the
	 * local TLB for it and unmark it before we use it
	 */
	if (test_bit(id, stale_map[cpu])) {
		pr_debug("[%d] flushing stale context %d for mm @%p !\n",
			 cpu, id, next);
		local_flush_tlb_mm(next);

		/* XXX This clear should ultimately be part of local_flush_tlb_mm */
		__clear_bit(id, stale_map[cpu]);
	}

	/* Flick the MMU and release lock */
	set_context(id, next->pgd);
	spin_unlock(&context_lock);
}

/*
 * Set up the context for a new address space.
 */
int init_new_context(struct task_struct *t, struct mm_struct *mm)
{
	mm->context.id = MMU_NO_CONTEXT;
	mm->context.active = 0;

	return 0;
}

/*
 * We're finished using the context for an address space.
 */
void destroy_context(struct mm_struct *mm)
{
	unsigned int id;

	if (mm->context.id == MMU_NO_CONTEXT)
		return;

	WARN_ON(mm->context.active != 0);

	spin_lock(&context_lock);
	id = mm->context.id;
	if (id != MMU_NO_CONTEXT) {
		__clear_bit(id, context_map);
		mm->context.id = MMU_NO_CONTEXT;
#ifdef DEBUG_MAP_CONSISTENCY
		mm->context.active = 0;
		context_mm[id] = NULL;
#endif
		nr_free_contexts++;
	}
	spin_unlock(&context_lock);
}

#ifdef CONFIG_SMP

static int __cpuinit mmu_context_cpu_notify(struct notifier_block *self,
					    unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;

	/* We don't touch CPU 0 map, it's allocated at aboot and kept
	 * around forever
	 */
	if (cpu == 0)
		return NOTIFY_OK;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		pr_debug("MMU: Allocating stale context map for CPU %d\n", cpu);
		stale_map[cpu] = kzalloc(CTX_MAP_SIZE, GFP_KERNEL);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		pr_debug("MMU: Freeing stale context map for CPU %d\n", cpu);
		kfree(stale_map[cpu]);
		stale_map[cpu] = NULL;
		break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata mmu_context_cpu_nb = {
	.notifier_call	= mmu_context_cpu_notify,
};

#endif /* CONFIG_SMP */

/*
 * Initialize the context management stuff.
 */
void __init mmu_context_init(void)
{
	/* Mark init_mm as being active on all possible CPUs since
	 * we'll get called with prev == init_mm the first time
	 * we schedule on a given CPU
	 */
	init_mm.context.active = NR_CPUS;

	/*
	 *   The MPC8xx has only 16 contexts.  We rotate through them on each
	 * task switch.  A better way would be to keep track of tasks that
	 * own contexts, and implement an LRU usage.  That way very active
	 * tasks don't always have to pay the TLB reload overhead.  The
	 * kernel pages are mapped shared, so the kernel can run on behalf
	 * of any task that makes a kernel entry.  Shared does not mean they
	 * are not protected, just that the ASID comparison is not performed.
	 *      -- Dan
	 *
	 * The IBM4xx has 256 contexts, so we can just rotate through these
	 * as a way of "switching" contexts.  If the TID of the TLB is zero,
	 * the PID/TID comparison is disabled, so we can use a TID of zero
	 * to represent all kernel pages as shared among all contexts.
	 * 	-- Dan
	 */
	if (mmu_has_feature(MMU_FTR_TYPE_8xx)) {
		first_context = 0;
		last_context = 15;
	} else {
		first_context = 1;
		last_context = 255;
	}

#ifdef DEBUG_CLAMP_LAST_CONTEXT
	last_context = DEBUG_CLAMP_LAST_CONTEXT;
#endif
	/*
	 * Allocate the maps used by context management
	 */
	context_map = alloc_bootmem(CTX_MAP_SIZE);
	context_mm = alloc_bootmem(sizeof(void *) * (last_context + 1));
	stale_map[0] = alloc_bootmem(CTX_MAP_SIZE);

#ifdef CONFIG_SMP
	register_cpu_notifier(&mmu_context_cpu_nb);
#endif

	printk(KERN_INFO
	       "MMU: Allocated %zu bytes of context maps for %d contexts\n",
	       2 * CTX_MAP_SIZE + (sizeof(void *) * (last_context + 1)),
	       last_context - first_context + 1);

	/*
	 * Some processors have too few contexts to reserve one for
	 * init_mm, and require using context 0 for a normal task.
	 * Other processors reserve the use of context zero for the kernel.
	 * This code assumes first_context < 32.
	 */
	context_map[0] = (1 << first_context) - 1;
	next_context = first_context;
	nr_free_contexts = last_context - first_context + 1;
}

