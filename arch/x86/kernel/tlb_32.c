#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/interrupt.h>

#include <asm/tlbflush.h>

DEFINE_PER_CPU(struct tlb_state, cpu_tlbstate)
			____cacheline_aligned = { &init_mm, 0, };

/* must come after the send_IPI functions above for inlining */
#include <mach_ipi.h>

/*
 *	Smarter SMP flushing macros.
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 */

static cpumask_var_t flush_cpumask;
static struct mm_struct *flush_mm;
static unsigned long flush_va;
static DEFINE_SPINLOCK(tlbstate_lock);

/*
 * We cannot call mmdrop() because we are in interrupt context,
 * instead update mm->cpu_vm_mask.
 *
 * We need to reload %cr3 since the page tables may be going
 * away from under us..
 */
void leave_mm(int cpu)
{
	BUG_ON(percpu_read(cpu_tlbstate.state) == TLBSTATE_OK);
	cpu_clear(cpu, percpu_read(cpu_tlbstate.active_mm)->cpu_vm_mask);
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
 * 	Stop ipi delivery for the old mm. This is not synchronized with
 * 	the other cpus, but smp_invalidate_interrupt ignore flush ipis
 * 	for the wrong mm, and in the worst case we perform a superfluous
 * 	tlb flush.
 * 1a2) set cpu_tlbstate to TLBSTATE_OK
 * 	Now the smp_invalidate_interrupt won't call leave_mm if cpu0
 *	was in lazy tlb mode.
 * 1a3) update cpu_tlbstate[].active_mm
 * 	Now cpu0 accepts tlb flushes for the new mm.
 * 1a4) cpu_set(cpu, new_mm->cpu_vm_mask);
 * 	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1b) thread switch without mm change
 *	cpu_tlbstate[].active_mm is correct, cpu0 already handles
 *	flush ipis.
 * 1b1) set cpu_tlbstate to TLBSTATE_OK
 * 1b2) test_and_set the cpu bit in cpu_vm_mask.
 * 	Atomically set the bit [other cpus will start sending flush ipis],
 * 	and test the bit.
 * 1b3) if the bit was 0: leave_mm was called, flush the tlb.
 * 2) switch %%esp, ie current
 *
 * The interrupt must handle 2 special cases:
 * - cr3 is changed before %%esp, ie. it cannot use current->{active_,}mm.
 * - the cpu performs speculative tlb reads, i.e. even if the cpu only
 *   runs in kernel space, the cpu could load tlb entries for user space
 *   pages.
 *
 * The good news is that cpu_tlbstate is local to each cpu, no
 * write/read ordering problems.
 */

/*
 * TLB flush IPI:
 *
 * 1) Flush the tlb entries if the cpu uses the mm that's being flushed.
 * 2) Leave the mm if we are in the lazy tlb mode.
 */

void smp_invalidate_interrupt(struct pt_regs *regs)
{
	unsigned long cpu;

	cpu = get_cpu();

	if (!cpumask_test_cpu(cpu, flush_cpumask))
		goto out;
		/*
		 * This was a BUG() but until someone can quote me the
		 * line from the intel manual that guarantees an IPI to
		 * multiple CPUs is retried _only_ on the erroring CPUs
		 * its staying as a return
		 *
		 * BUG();
		 */

	if (flush_mm == percpu_read(cpu_tlbstate.active_mm)) {
		if (percpu_read(cpu_tlbstate.state) == TLBSTATE_OK) {
			if (flush_va == TLB_FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(flush_va);
		} else
			leave_mm(cpu);
	}
	ack_APIC_irq();
	smp_mb__before_clear_bit();
	cpumask_clear_cpu(cpu, flush_cpumask);
	smp_mb__after_clear_bit();
out:
	put_cpu_no_resched();
	inc_irq_stat(irq_tlb_count);
}

void native_flush_tlb_others(const struct cpumask *cpumask,
			     struct mm_struct *mm, unsigned long va)
{
	/*
	 * - mask must exist :)
	 */
	BUG_ON(cpumask_empty(cpumask));
	BUG_ON(!mm);

	/*
	 * i'm not happy about this global shared spinlock in the
	 * MM hot path, but we'll see how contended it is.
	 * AK: x86-64 has a faster method that could be ported.
	 */
	spin_lock(&tlbstate_lock);

	cpumask_andnot(flush_cpumask, cpumask, cpumask_of(smp_processor_id()));
#ifdef CONFIG_HOTPLUG_CPU
	/* If a CPU which we ran on has gone down, OK. */
	cpumask_and(flush_cpumask, flush_cpumask, cpu_online_mask);
	if (unlikely(cpumask_empty(flush_cpumask))) {
		spin_unlock(&tlbstate_lock);
		return;
	}
#endif
	flush_mm = mm;
	flush_va = va;

	/*
	 * Make the above memory operations globally visible before
	 * sending the IPI.
	 */
	smp_mb();
	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(flush_cpumask, INVALIDATE_TLB_VECTOR);

	while (!cpumask_empty(flush_cpumask))
		/* nothing. lockup detection does not belong here */
		cpu_relax();

	flush_mm = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}

void flush_tlb_current_task(void)
{
	struct mm_struct *mm = current->mm;

	preempt_disable();

	local_flush_tlb();
	if (cpumask_any_but(&mm->cpu_vm_mask, smp_processor_id()) < nr_cpu_ids)
		flush_tlb_others(&mm->cpu_vm_mask, mm, TLB_FLUSH_ALL);
	preempt_enable();
}

void flush_tlb_mm(struct mm_struct *mm)
{

	preempt_disable();

	if (current->active_mm == mm) {
		if (current->mm)
			local_flush_tlb();
		else
			leave_mm(smp_processor_id());
	}
	if (cpumask_any_but(&mm->cpu_vm_mask, smp_processor_id()) < nr_cpu_ids)
		flush_tlb_others(&mm->cpu_vm_mask, mm, TLB_FLUSH_ALL);

	preempt_enable();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();

	if (current->active_mm == mm) {
		if (current->mm)
			__flush_tlb_one(va);
		 else
			leave_mm(smp_processor_id());
	}

	if (cpumask_any_but(&mm->cpu_vm_mask, smp_processor_id()) < nr_cpu_ids)
		flush_tlb_others(&mm->cpu_vm_mask, mm, va);
	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_page);

static void do_flush_tlb_all(void *info)
{
	unsigned long cpu = smp_processor_id();

	__flush_tlb_all();
	if (percpu_read(cpu_tlbstate.state) == TLBSTATE_LAZY)
		leave_mm(cpu);
}

void flush_tlb_all(void)
{
	on_each_cpu(do_flush_tlb_all, NULL, 1);
}

void reset_lazy_tlbstate(void)
{
	int cpu = raw_smp_processor_id();

	per_cpu(cpu_tlbstate, cpu).state = 0;
	per_cpu(cpu_tlbstate, cpu).active_mm = &init_mm;
}

static int init_flush_cpumask(void)
{
	alloc_cpumask_var(&flush_cpumask, GFP_KERNEL);
	return 0;
}
early_initcall(init_flush_cpumask);
