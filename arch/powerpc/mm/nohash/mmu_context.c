// SPDX-License-Identifier: GPL-2.0-or-later
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

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/slab.h>

#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/smp.h>
#include <asm/kup.h>

#include <mm/mmu_decl.h>

/*
 * Room for two PTE table pointers, usually the kernel and current user
 * pointer to their respective root page table (pgdir).
 */
void *abatron_pteptrs[2];

/*
 * The MPC8xx has only 16 contexts. We rotate through them on each task switch.
 * A better way would be to keep track of tasks that own contexts, and implement
 * an LRU usage. That way very active tasks don't always have to pay the TLB
 * reload overhead. The kernel pages are mapped shared, so the kernel can run on
 * behalf of any task that makes a kernel entry. Shared does not mean they are
 * not protected, just that the ASID comparison is not performed. -- Dan
 *
 * The IBM4xx has 256 contexts, so we can just rotate through these as a way of
 * "switching" contexts. If the TID of the TLB is zero, the PID/TID comparison
 * is disabled, so we can use a TID of zero to represent all kernel pages as
 * shared among all contexts. -- Dan
 *
 * The IBM 47x core supports 16-bit PIDs, thus 65535 contexts. We should
 * normally never have to steal though the facility is present if needed.
 * -- BenH
 */
#define FIRST_CONTEXT 1
#if defined(CONFIG_PPC_8xx)
#define LAST_CONTEXT 16
#elif defined(CONFIG_PPC_47x)
#define LAST_CONTEXT 65535
#else
#define LAST_CONTEXT 255
#endif

static unsigned int next_context, nr_free_contexts;
static unsigned long *context_map;
static unsigned long *stale_map[NR_CPUS];
static struct mm_struct **context_mm;
static DEFINE_RAW_SPINLOCK(context_lock);

#define CTX_MAP_SIZE	\
	(sizeof(unsigned long) * (LAST_CONTEXT / BITS_PER_LONG + 1))


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
static unsigned int steal_context_smp(unsigned int id)
{
	struct mm_struct *mm;
	unsigned int cpu, max, i;

	max = LAST_CONTEXT - FIRST_CONTEXT;

	/* Attempt to free next_context first and then loop until we manage */
	while (max--) {
		/* Pick up the victim mm */
		mm = context_mm[id];

		/* We have a candidate victim, check if it's active, on SMP
		 * we cannot steal active contexts
		 */
		if (mm->context.active) {
			id++;
			if (id > LAST_CONTEXT)
				id = FIRST_CONTEXT;
			continue;
		}

		/* Mark this mm has having no context anymore */
		mm->context.id = MMU_NO_CONTEXT;

		/* Mark it stale on all CPUs that used this mm. For threaded
		 * implementations, we set it on all threads on each core
		 * represented in the mask. A future implementation will use
		 * a core map instead but this will do for now.
		 */
		for_each_cpu(cpu, mm_cpumask(mm)) {
			for (i = cpu_first_thread_sibling(cpu);
			     i <= cpu_last_thread_sibling(cpu); i++) {
				if (stale_map[i])
					__set_bit(id, stale_map[i]);
			}
			cpu = i - 1;
		}
		return id;
	}

	/* This will happen if you have more CPUs than available contexts,
	 * all we can do here is wait a bit and try again
	 */
	raw_spin_unlock(&context_lock);
	cpu_relax();
	raw_spin_lock(&context_lock);

	/* This will cause the caller to try again */
	return MMU_NO_CONTEXT;
}

static unsigned int steal_all_contexts(void)
{
	struct mm_struct *mm;
	int cpu = smp_processor_id();
	unsigned int id;

	for (id = FIRST_CONTEXT; id <= LAST_CONTEXT; id++) {
		/* Pick up the victim mm */
		mm = context_mm[id];

		/* Mark this mm as having no context anymore */
		mm->context.id = MMU_NO_CONTEXT;
		if (id != FIRST_CONTEXT) {
			context_mm[id] = NULL;
			__clear_bit(id, context_map);
		}
		if (IS_ENABLED(CONFIG_SMP))
			__clear_bit(id, stale_map[cpu]);
	}

	/* Flush the TLB for all contexts (not to be used on SMP) */
	_tlbil_all();

	nr_free_contexts = LAST_CONTEXT - FIRST_CONTEXT;

	return FIRST_CONTEXT;
}

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

	/* Flush the TLB for that context */
	local_flush_tlb_mm(mm);

	/* Mark this mm has having no context anymore */
	mm->context.id = MMU_NO_CONTEXT;

	/* XXX This clear should ultimately be part of local_flush_tlb_mm */
	if (IS_ENABLED(CONFIG_SMP))
		__clear_bit(id, stale_map[cpu]);

	return id;
}

static void set_context(unsigned long id, pgd_t *pgd)
{
	if (IS_ENABLED(CONFIG_PPC_8xx)) {
		s16 offset = (s16)(__pa(swapper_pg_dir));

		/*
		 * Register M_TWB will contain base address of level 1 table minus the
		 * lower part of the kernel PGDIR base address, so that all accesses to
		 * level 1 table are done relative to lower part of kernel PGDIR base
		 * address.
		 */
		mtspr(SPRN_M_TWB, __pa(pgd) - offset);

		/* Update context */
		mtspr(SPRN_M_CASID, id - 1);

		/* sync */
		mb();
	} else if (kuap_is_disabled()) {
		if (IS_ENABLED(CONFIG_40x))
			mb();	/* sync */

		mtspr(SPRN_PID, id);
		isync();
	}
}

void switch_mmu_context(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	unsigned int id;
	unsigned int i, cpu = smp_processor_id();
	unsigned long *map;

	/* No lockless fast path .. yet */
	raw_spin_lock(&context_lock);

	if (IS_ENABLED(CONFIG_SMP)) {
		/* Mark us active and the previous one not anymore */
		next->context.active++;
		if (prev) {
			WARN_ON(prev->context.active < 1);
			prev->context.active--;
		}
	}

 again:

	/* If we already have a valid assigned context, skip all that */
	id = next->context.id;
	if (likely(id != MMU_NO_CONTEXT))
		goto ctxt_ok;

	/* We really don't have a context, let's try to acquire one */
	id = next_context;
	if (id > LAST_CONTEXT)
		id = FIRST_CONTEXT;
	map = context_map;

	/* No more free contexts, let's try to steal one */
	if (nr_free_contexts == 0) {
		if (num_online_cpus() > 1) {
			id = steal_context_smp(id);
			if (id == MMU_NO_CONTEXT)
				goto again;
			goto stolen;
		}
		if (IS_ENABLED(CONFIG_PPC_8xx))
			id = steal_all_contexts();
		else
			id = steal_context_up(id);
		goto stolen;
	}
	nr_free_contexts--;

	/* We know there's at least one free context, try to find it */
	while (__test_and_set_bit(id, map)) {
		id = find_next_zero_bit(map, LAST_CONTEXT+1, id);
		if (id > LAST_CONTEXT)
			id = FIRST_CONTEXT;
	}
 stolen:
	next_context = id + 1;
	context_mm[id] = next;
	next->context.id = id;

 ctxt_ok:

	/* If that context got marked stale on this CPU, then flush the
	 * local TLB for it and unmark it before we use it
	 */
	if (IS_ENABLED(CONFIG_SMP) && test_bit(id, stale_map[cpu])) {
		local_flush_tlb_mm(next);

		/* XXX This clear should ultimately be part of local_flush_tlb_mm */
		for (i = cpu_first_thread_sibling(cpu);
		     i <= cpu_last_thread_sibling(cpu); i++) {
			if (stale_map[i])
				__clear_bit(id, stale_map[i]);
		}
	}

	/* Flick the MMU and release lock */
	if (IS_ENABLED(CONFIG_BDI_SWITCH))
		abatron_pteptrs[1] = next->pgd;
	set_context(id, next->pgd);
#if defined(CONFIG_BOOKE_OR_40x) && defined(CONFIG_PPC_KUAP)
	tsk->thread.pid = id;
#endif
	raw_spin_unlock(&context_lock);
}

/*
 * Set up the context for a new address space.
 */
int init_new_context(struct task_struct *t, struct mm_struct *mm)
{
	mm->context.id = MMU_NO_CONTEXT;
	mm->context.active = 0;
	pte_frag_set(&mm->context, NULL);
	return 0;
}

/*
 * We're finished using the context for an address space.
 */
void destroy_context(struct mm_struct *mm)
{
	unsigned long flags;
	unsigned int id;

	if (mm->context.id == MMU_NO_CONTEXT)
		return;

	WARN_ON(mm->context.active != 0);

	raw_spin_lock_irqsave(&context_lock, flags);
	id = mm->context.id;
	if (id != MMU_NO_CONTEXT) {
		__clear_bit(id, context_map);
		mm->context.id = MMU_NO_CONTEXT;
		context_mm[id] = NULL;
		nr_free_contexts++;
	}
	raw_spin_unlock_irqrestore(&context_lock, flags);
}

static int mmu_ctx_cpu_prepare(unsigned int cpu)
{
	/* We don't touch CPU 0 map, it's allocated at aboot and kept
	 * around forever
	 */
	if (cpu == boot_cpuid)
		return 0;

	stale_map[cpu] = kzalloc(CTX_MAP_SIZE, GFP_KERNEL);
	return 0;
}

static int mmu_ctx_cpu_dead(unsigned int cpu)
{
#ifdef CONFIG_HOTPLUG_CPU
	if (cpu == boot_cpuid)
		return 0;

	kfree(stale_map[cpu]);
	stale_map[cpu] = NULL;

	/* We also clear the cpu_vm_mask bits of CPUs going away */
	clear_tasks_mm_cpumask(cpu);
#endif
	return 0;
}

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
	 * Allocate the maps used by context management
	 */
	context_map = memblock_alloc(CTX_MAP_SIZE, SMP_CACHE_BYTES);
	if (!context_map)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      CTX_MAP_SIZE);
	context_mm = memblock_alloc(sizeof(void *) * (LAST_CONTEXT + 1),
				    SMP_CACHE_BYTES);
	if (!context_mm)
		panic("%s: Failed to allocate %zu bytes\n", __func__,
		      sizeof(void *) * (LAST_CONTEXT + 1));
	if (IS_ENABLED(CONFIG_SMP)) {
		stale_map[boot_cpuid] = memblock_alloc(CTX_MAP_SIZE, SMP_CACHE_BYTES);
		if (!stale_map[boot_cpuid])
			panic("%s: Failed to allocate %zu bytes\n", __func__,
			      CTX_MAP_SIZE);

		cpuhp_setup_state_nocalls(CPUHP_POWERPC_MMU_CTX_PREPARE,
					  "powerpc/mmu/ctx:prepare",
					  mmu_ctx_cpu_prepare, mmu_ctx_cpu_dead);
	}

	printk(KERN_INFO
	       "MMU: Allocated %zu bytes of context maps for %d contexts\n",
	       2 * CTX_MAP_SIZE + (sizeof(void *) * (LAST_CONTEXT + 1)),
	       LAST_CONTEXT - FIRST_CONTEXT + 1);

	/*
	 * Some processors have too few contexts to reserve one for
	 * init_mm, and require using context 0 for a normal task.
	 * Other processors reserve the use of context zero for the kernel.
	 * This code assumes FIRST_CONTEXT < 32.
	 */
	context_map[0] = (1 << FIRST_CONTEXT) - 1;
	next_context = FIRST_CONTEXT;
	nr_free_contexts = LAST_CONTEXT - FIRST_CONTEXT + 1;
}
