// SPDX-License-Identifier: GPL-2.0
/*
 * Alpha TLB shootdown helpers
 *
 * Copyright (C) 2025 Magnus Lindholm <linmag7@gmail.com>
 *
 * Alpha-specific TLB flush helpers that cannot be expressed purely
 * as inline functions.
 *
 * These helpers provide combined MM context handling (ASN rollover)
 * and immediate TLB invalidation for page migration and memory
 * compaction paths, where lazy shootdowns are insufficient.
 */

#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <asm/tlbflush.h>
#include <asm/pal.h>
#include <asm/mmu_context.h>

#define asn_locked() (cpu_data[smp_processor_id()].asn_lock)

/*
 * Migration/compaction helper: combine mm context (ASN) handling with an
 * immediate per-page TLB invalidate and (for exec) an instruction barrier.
 *
 * This mirrors the SMP combined IPI handler semantics, but runs locally on UP.
 */
#ifndef CONFIG_SMP
void migrate_flush_tlb_page(struct vm_area_struct *vma,
					   unsigned long addr)
{
	struct mm_struct *mm = vma->vm_mm;
	int tbi_type = (vma->vm_flags & VM_EXEC) ? 3 : 2;

	/*
	 * First do the mm-context side:
	 * If we're currently running this mm, reload a fresh context ASN.
	 * Otherwise, mark context invalid.
	 *
	 * On UP, this is mostly about matching the SMP semantics and ensuring
	 * exec/i-cache tagging assumptions hold when compaction migrates pages.
	 */
	if (mm == current->active_mm)
		flush_tlb_current(mm);
	else
		flush_tlb_other(mm);

	/*
	 * Then do the immediate translation kill for this VA.
	 * For exec mappings, order instruction fetch after invalidation.
	 */
	tbi(tbi_type, addr);
}

#else
struct tlb_mm_and_addr {
	struct mm_struct *mm;
	unsigned long addr;
	int tbi_type;	/* 2 = DTB, 3 = ITB+DTB */
};

static void ipi_flush_mm_and_page(void *x)
{
	struct tlb_mm_and_addr *d = x;

	/* Part 1: mm context side (Alpha uses ASN/context as a key mechanism). */
	if (d->mm == current->active_mm && !asn_locked())
		__load_new_mm_context(d->mm);
	else
		flush_tlb_other(d->mm);

	/* Part 2: immediate per-VA invalidation on this CPU. */
	tbi(d->tbi_type, d->addr);
}

void migrate_flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct mm_struct *mm = vma->vm_mm;
	struct tlb_mm_and_addr d = {
		.mm = mm,
		.addr = addr,
		.tbi_type = (vma->vm_flags & VM_EXEC) ? 3 : 2,
	};

	/*
	 * One synchronous rendezvous: every CPU runs ipi_flush_mm_and_page().
	 * This is the "combined" version of flush_tlb_mm + per-page invalidate.
	 */
	preempt_disable();
	on_each_cpu(ipi_flush_mm_and_page, &d, 1);

	/*
	 * mimic flush_tlb_mm()'s mm_users<=1 optimization.
	 */
	if (atomic_read(&mm->mm_users) <= 1) {

		int cpu, this_cpu;
		this_cpu = smp_processor_id();

		for (cpu = 0; cpu < NR_CPUS; cpu++) {
			if (!cpu_online(cpu) || cpu == this_cpu)
				continue;
			if (READ_ONCE(mm->context[cpu]))
				WRITE_ONCE(mm->context[cpu], 0);
		}
	}
	preempt_enable();
}

#endif
