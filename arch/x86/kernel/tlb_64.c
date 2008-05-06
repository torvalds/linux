#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/interrupt.h>

#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/apicdef.h>
#include <asm/idle.h>

#include <mach_ipi.h>
/*
 *	Smarter SMP flushing macros.
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 *
 *	More scalable flush, from Andi Kleen
 *
 *	To avoid global state use 8 different call vectors.
 *	Each CPU uses a specific vector to trigger flushes on other
 *	CPUs. Depending on the received vector the target CPUs look into
 *	the right per cpu variable for the flush data.
 *
 *	With more than 8 CPUs they are hashed to the 8 available
 *	vectors. The limited global vector space forces us to this right now.
 *	In future when interrupts are split into per CPU domains this could be
 *	fixed, at the cost of triggering multiple IPIs in some cases.
 */

union smp_flush_state {
	struct {
		cpumask_t flush_cpumask;
		struct mm_struct *flush_mm;
		unsigned long flush_va;
		spinlock_t tlbstate_lock;
	};
	char pad[SMP_CACHE_BYTES];
} ____cacheline_aligned;

/* State is put into the per CPU data section, but padded
   to a full cache line because other CPUs can access it and we don't
   want false sharing in the per cpu data segment. */
static DEFINE_PER_CPU(union smp_flush_state, flush_state);

/*
 * We cannot call mmdrop() because we are in interrupt context,
 * instead update mm->cpu_vm_mask.
 */
void leave_mm(int cpu)
{
	if (read_pda(mmu_state) == TLBSTATE_OK)
		BUG();
	cpu_clear(cpu, read_pda(active_mm)->cpu_vm_mask);
	load_cr3(swapper_pg_dir);
}
EXPORT_SYMBOL_GPL(leave_mm);

/*
 *
 * The flush IPI assumes that a thread switch happens in this order:
 * [cpu0: the cpu that switches]
 * 1) switch_mm() either 1a) or 1b)
 * 1a) thread switch to a different mm
 * 1a1) cpu_clear(cpu, old_mm->cpu_vm_mask);
 *	Stop ipi delivery for the old mm. This is not synchronized with
 *	the other cpus, but smp_invalidate_interrupt ignore flush ipis
 *	for the wrong mm, and in the worst case we perform a superfluous
 *	tlb flush.
 * 1a2) set cpu mmu_state to TLBSTATE_OK
 *	Now the smp_invalidate_interrupt won't call leave_mm if cpu0
 *	was in lazy tlb mode.
 * 1a3) update cpu active_mm
 *	Now cpu0 accepts tlb flushes for the new mm.
 * 1a4) cpu_set(cpu, new_mm->cpu_vm_mask);
 *	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1b) thread switch without mm change
 *	cpu active_mm is correct, cpu0 already handles
 *	flush ipis.
 * 1b1) set cpu mmu_state to TLBSTATE_OK
 * 1b2) test_and_set the cpu bit in cpu_vm_mask.
 *	Atomically set the bit [other cpus will start sending flush ipis],
 *	and test the bit.
 * 1b3) if the bit was 0: leave_mm was called, flush the tlb.
 * 2) switch %%esp, ie current
 *
 * The interrupt must handle 2 special cases:
 * - cr3 is changed before %%esp, ie. it cannot use current->{active_,}mm.
 * - the cpu performs speculative tlb reads, i.e. even if the cpu only
 *   runs in kernel space, the cpu could load tlb entries for user space
 *   pages.
 *
 * The good news is that cpu mmu_state is local to each cpu, no
 * write/read ordering problems.
 */

/*
 * TLB flush IPI:
 *
 * 1) Flush the tlb entries if the cpu uses the mm that's being flushed.
 * 2) Leave the mm if we are in the lazy tlb mode.
 *
 * Interrupts are disabled.
 */

asmlinkage void smp_invalidate_interrupt(struct pt_regs *regs)
{
	int cpu;
	int sender;
	union smp_flush_state *f;

	cpu = smp_processor_id();
	/*
	 * orig_rax contains the negated interrupt vector.
	 * Use that to determine where the sender put the data.
	 */
	sender = ~regs->orig_ax - INVALIDATE_TLB_VECTOR_START;
	f = &per_cpu(flush_state, sender);

	if (!cpu_isset(cpu, f->flush_cpumask))
		goto out;
		/*
		 * This was a BUG() but until someone can quote me the
		 * line from the intel manual that guarantees an IPI to
		 * multiple CPUs is retried _only_ on the erroring CPUs
		 * its staying as a return
		 *
		 * BUG();
		 */

	if (f->flush_mm == read_pda(active_mm)) {
		if (read_pda(mmu_state) == TLBSTATE_OK) {
			if (f->flush_va == TLB_FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(f->flush_va);
		} else
			leave_mm(cpu);
	}
out:
	ack_APIC_irq();
	cpu_clear(cpu, f->flush_cpumask);
	add_pda(irq_tlb_count, 1);
}

void native_flush_tlb_others(const cpumask_t *cpumaskp, struct mm_struct *mm,
			     unsigned long va)
{
	int sender;
	union smp_flush_state *f;
	cpumask_t cpumask = *cpumaskp;

	/* Caller has disabled preemption */
	sender = smp_processor_id() % NUM_INVALIDATE_TLB_VECTORS;
	f = &per_cpu(flush_state, sender);

	/*
	 * Could avoid this lock when
	 * num_online_cpus() <= NUM_INVALIDATE_TLB_VECTORS, but it is
	 * probably not worth checking this for a cache-hot lock.
	 */
	spin_lock(&f->tlbstate_lock);

	f->flush_mm = mm;
	f->flush_va = va;
	cpus_or(f->flush_cpumask, cpumask, f->flush_cpumask);

	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(cpumask, INVALIDATE_TLB_VECTOR_START + sender);

	while (!cpus_empty(f->flush_cpumask))
		cpu_relax();

	f->flush_mm = NULL;
	f->flush_va = 0;
	spin_unlock(&f->tlbstate_lock);
}

static int __cpuinit init_smp_flush(void)
{
	int i;

	for_each_possible_cpu(i)
		spin_lock_init(&per_cpu(flush_state, i).tlbstate_lock);

	return 0;
}
core_initcall(init_smp_flush);

void flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	local_flush_tlb();
	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, TLB_FLUSH_ALL);
	preempt_enable();
}

void flush_tlb_mm(struct mm_struct *mm)
{
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	if (current->active_mm == mm) {
		if (current->mm)
			local_flush_tlb();
		else
			leave_mm(smp_processor_id());
	}
	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, TLB_FLUSH_ALL);

	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	if (current->active_mm == mm) {
		if (current->mm)
			__flush_tlb_one(va);
		else
			leave_mm(smp_processor_id());
	}

	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, va);

	preempt_enable();
}

static void do_flush_tlb_all(void *info)
{
	unsigned long cpu = smp_processor_id();

	__flush_tlb_all();
	if (read_pda(mmu_state) == TLBSTATE_LAZY)
		leave_mm(cpu);
}

void flush_tlb_all(void)
{
	on_each_cpu(do_flush_tlb_all, NULL, 1, 1);
}
