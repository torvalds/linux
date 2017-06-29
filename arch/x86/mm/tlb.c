#include <linux/init.h>

#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/export.h>
#include <linux/cpu.h>

#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/cache.h>
#include <asm/apic.h>
#include <asm/uv/uv.h>
#include <linux/debugfs.h>

/*
 *	TLB flushing, formerly SMP-only
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 *
 *	More scalable flush, from Andi Kleen
 *
 *	Implement flush IPI by CALL_FUNCTION_VECTOR, Alex Shi
 */

atomic64_t last_mm_ctx_id = ATOMIC64_INIT(1);

void leave_mm(int cpu)
{
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);

	/*
	 * It's plausible that we're in lazy TLB mode while our mm is init_mm.
	 * If so, our callers still expect us to flush the TLB, but there
	 * aren't any user TLB entries in init_mm to worry about.
	 *
	 * This needs to happen before any other sanity checks due to
	 * intel_idle's shenanigans.
	 */
	if (loaded_mm == &init_mm)
		return;

	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_OK)
		BUG();

	switch_mm(NULL, &init_mm, NULL);
}
EXPORT_SYMBOL_GPL(leave_mm);

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *tsk)
{
	unsigned long flags;

	local_irq_save(flags);
	switch_mm_irqs_off(prev, next, tsk);
	local_irq_restore(flags);
}

void switch_mm_irqs_off(struct mm_struct *prev, struct mm_struct *next,
			struct task_struct *tsk)
{
	unsigned cpu = smp_processor_id();
	struct mm_struct *real_prev = this_cpu_read(cpu_tlbstate.loaded_mm);

	/*
	 * NB: The scheduler will call us with prev == next when
	 * switching from lazy TLB mode to normal mode if active_mm
	 * isn't changing.  When this happens, there is no guarantee
	 * that CR3 (and hence cpu_tlbstate.loaded_mm) matches next.
	 *
	 * NB: leave_mm() calls us with prev == NULL and tsk == NULL.
	 */

	this_cpu_write(cpu_tlbstate.state, TLBSTATE_OK);

	if (real_prev == next) {
		/*
		 * There's nothing to do: we always keep the per-mm control
		 * regs in sync with cpu_tlbstate.loaded_mm.  Just
		 * sanity-check mm_cpumask.
		 */
		if (WARN_ON_ONCE(!cpumask_test_cpu(cpu, mm_cpumask(next))))
			cpumask_set_cpu(cpu, mm_cpumask(next));
		return;
	}

	if (IS_ENABLED(CONFIG_VMAP_STACK)) {
		/*
		 * If our current stack is in vmalloc space and isn't
		 * mapped in the new pgd, we'll double-fault.  Forcibly
		 * map it.
		 */
		unsigned int stack_pgd_index = pgd_index(current_stack_pointer());

		pgd_t *pgd = next->pgd + stack_pgd_index;

		if (unlikely(pgd_none(*pgd)))
			set_pgd(pgd, init_mm.pgd[stack_pgd_index]);
	}

	this_cpu_write(cpu_tlbstate.loaded_mm, next);
	this_cpu_write(cpu_tlbstate.ctxs[0].ctx_id, next->context.ctx_id);
	this_cpu_write(cpu_tlbstate.ctxs[0].tlb_gen, atomic64_read(&next->context.tlb_gen));

	WARN_ON_ONCE(cpumask_test_cpu(cpu, mm_cpumask(next)));
	cpumask_set_cpu(cpu, mm_cpumask(next));

	/*
	 * Re-load page tables.
	 *
	 * This logic has an ordering constraint:
	 *
	 *  CPU 0: Write to a PTE for 'next'
	 *  CPU 0: load bit 1 in mm_cpumask.  if nonzero, send IPI.
	 *  CPU 1: set bit 1 in next's mm_cpumask
	 *  CPU 1: load from the PTE that CPU 0 writes (implicit)
	 *
	 * We need to prevent an outcome in which CPU 1 observes
	 * the new PTE value and CPU 0 observes bit 1 clear in
	 * mm_cpumask.  (If that occurs, then the IPI will never
	 * be sent, and CPU 0's TLB will contain a stale entry.)
	 *
	 * The bad outcome can occur if either CPU's load is
	 * reordered before that CPU's store, so both CPUs must
	 * execute full barriers to prevent this from happening.
	 *
	 * Thus, switch_mm needs a full barrier between the
	 * store to mm_cpumask and any operation that could load
	 * from next->pgd.  TLB fills are special and can happen
	 * due to instruction fetches or for no reason at all,
	 * and neither LOCK nor MFENCE orders them.
	 * Fortunately, load_cr3() is serializing and gives the
	 * ordering guarantee we need.
	 */
	load_cr3(next->pgd);

	/*
	 * This gets called via leave_mm() in the idle path where RCU
	 * functions differently.  Tracing normally uses RCU, so we have to
	 * call the tracepoint specially here.
	 */
	trace_tlb_flush_rcuidle(TLB_FLUSH_ON_TASK_SWITCH, TLB_FLUSH_ALL);

	/* Stop flush ipis for the previous mm */
	WARN_ON_ONCE(!cpumask_test_cpu(cpu, mm_cpumask(real_prev)) &&
		     real_prev != &init_mm);
	cpumask_clear_cpu(cpu, mm_cpumask(real_prev));

	/* Load per-mm CR4 and LDTR state */
	load_mm_cr4(next);
	switch_ldt(real_prev, next);
}

/*
 * flush_tlb_func_common()'s memory ordering requirement is that any
 * TLB fills that happen after we flush the TLB are ordered after we
 * read active_mm's tlb_gen.  We don't need any explicit barriers
 * because all x86 flush operations are serializing and the
 * atomic64_read operation won't be reordered by the compiler.
 */
static void flush_tlb_func_common(const struct flush_tlb_info *f,
				  bool local, enum tlb_flush_reason reason)
{
	/*
	 * We have three different tlb_gen values in here.  They are:
	 *
	 * - mm_tlb_gen:     the latest generation.
	 * - local_tlb_gen:  the generation that this CPU has already caught
	 *                   up to.
	 * - f->new_tlb_gen: the generation that the requester of the flush
	 *                   wants us to catch up to.
	 */
	struct mm_struct *loaded_mm = this_cpu_read(cpu_tlbstate.loaded_mm);
	u64 mm_tlb_gen = atomic64_read(&loaded_mm->context.tlb_gen);
	u64 local_tlb_gen = this_cpu_read(cpu_tlbstate.ctxs[0].tlb_gen);

	/* This code cannot presently handle being reentered. */
	VM_WARN_ON(!irqs_disabled());

	VM_WARN_ON(this_cpu_read(cpu_tlbstate.ctxs[0].ctx_id) !=
		   loaded_mm->context.ctx_id);

	if (this_cpu_read(cpu_tlbstate.state) != TLBSTATE_OK) {
		/*
		 * leave_mm() is adequate to handle any type of flush, and
		 * we would prefer not to receive further IPIs.  leave_mm()
		 * clears this CPU's bit in mm_cpumask().
		 */
		leave_mm(smp_processor_id());
		return;
	}

	if (unlikely(local_tlb_gen == mm_tlb_gen)) {
		/*
		 * There's nothing to do: we're already up to date.  This can
		 * happen if two concurrent flushes happen -- the first flush to
		 * be handled can catch us all the way up, leaving no work for
		 * the second flush.
		 */
		return;
	}

	WARN_ON_ONCE(local_tlb_gen > mm_tlb_gen);
	WARN_ON_ONCE(f->new_tlb_gen > mm_tlb_gen);

	/*
	 * If we get to this point, we know that our TLB is out of date.
	 * This does not strictly imply that we need to flush (it's
	 * possible that f->new_tlb_gen <= local_tlb_gen), but we're
	 * going to need to flush in the very near future, so we might
	 * as well get it over with.
	 *
	 * The only question is whether to do a full or partial flush.
	 *
	 * We do a partial flush if requested and two extra conditions
	 * are met:
	 *
	 * 1. f->new_tlb_gen == local_tlb_gen + 1.  We have an invariant that
	 *    we've always done all needed flushes to catch up to
	 *    local_tlb_gen.  If, for example, local_tlb_gen == 2 and
	 *    f->new_tlb_gen == 3, then we know that the flush needed to bring
	 *    us up to date for tlb_gen 3 is the partial flush we're
	 *    processing.
	 *
	 *    As an example of why this check is needed, suppose that there
	 *    are two concurrent flushes.  The first is a full flush that
	 *    changes context.tlb_gen from 1 to 2.  The second is a partial
	 *    flush that changes context.tlb_gen from 2 to 3.  If they get
	 *    processed on this CPU in reverse order, we'll see
	 *     local_tlb_gen == 1, mm_tlb_gen == 3, and end != TLB_FLUSH_ALL.
	 *    If we were to use __flush_tlb_single() and set local_tlb_gen to
	 *    3, we'd be break the invariant: we'd update local_tlb_gen above
	 *    1 without the full flush that's needed for tlb_gen 2.
	 *
	 * 2. f->new_tlb_gen == mm_tlb_gen.  This is purely an optimiation.
	 *    Partial TLB flushes are not all that much cheaper than full TLB
	 *    flushes, so it seems unlikely that it would be a performance win
	 *    to do a partial flush if that won't bring our TLB fully up to
	 *    date.  By doing a full flush instead, we can increase
	 *    local_tlb_gen all the way to mm_tlb_gen and we can probably
	 *    avoid another flush in the very near future.
	 */
	if (f->end != TLB_FLUSH_ALL &&
	    f->new_tlb_gen == local_tlb_gen + 1 &&
	    f->new_tlb_gen == mm_tlb_gen) {
		/* Partial flush */
		unsigned long addr;
		unsigned long nr_pages = (f->end - f->start) >> PAGE_SHIFT;

		addr = f->start;
		while (addr < f->end) {
			__flush_tlb_single(addr);
			addr += PAGE_SIZE;
		}
		if (local)
			count_vm_tlb_events(NR_TLB_LOCAL_FLUSH_ONE, nr_pages);
		trace_tlb_flush(reason, nr_pages);
	} else {
		/* Full flush. */
		local_flush_tlb();
		if (local)
			count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		trace_tlb_flush(reason, TLB_FLUSH_ALL);
	}

	/* Both paths above update our state to mm_tlb_gen. */
	this_cpu_write(cpu_tlbstate.ctxs[0].tlb_gen, mm_tlb_gen);
}

static void flush_tlb_func_local(void *info, enum tlb_flush_reason reason)
{
	const struct flush_tlb_info *f = info;

	flush_tlb_func_common(f, true, reason);
}

static void flush_tlb_func_remote(void *info)
{
	const struct flush_tlb_info *f = info;

	inc_irq_stat(irq_tlb_count);

	if (f->mm && f->mm != this_cpu_read(cpu_tlbstate.loaded_mm))
		return;

	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);
	flush_tlb_func_common(f, false, TLB_REMOTE_SHOOTDOWN);
}

void native_flush_tlb_others(const struct cpumask *cpumask,
			     const struct flush_tlb_info *info)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
	if (info->end == TLB_FLUSH_ALL)
		trace_tlb_flush(TLB_REMOTE_SEND_IPI, TLB_FLUSH_ALL);
	else
		trace_tlb_flush(TLB_REMOTE_SEND_IPI,
				(info->end - info->start) >> PAGE_SHIFT);

	if (is_uv_system()) {
		unsigned int cpu;

		cpu = smp_processor_id();
		cpumask = uv_flush_tlb_others(cpumask, info);
		if (cpumask)
			smp_call_function_many(cpumask, flush_tlb_func_remote,
					       (void *)info, 1);
		return;
	}
	smp_call_function_many(cpumask, flush_tlb_func_remote,
			       (void *)info, 1);
}

/*
 * See Documentation/x86/tlb.txt for details.  We choose 33
 * because it is large enough to cover the vast majority (at
 * least 95%) of allocations, and is small enough that we are
 * confident it will not cause too much overhead.  Each single
 * flush is about 100 ns, so this caps the maximum overhead at
 * _about_ 3,000 ns.
 *
 * This is in units of pages.
 */
static unsigned long tlb_single_page_flush_ceiling __read_mostly = 33;

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned long vmflag)
{
	int cpu;

	struct flush_tlb_info info = {
		.mm = mm,
	};

	cpu = get_cpu();

	/* This is also a barrier that synchronizes with switch_mm(). */
	info.new_tlb_gen = inc_mm_tlb_gen(mm);

	/* Should we flush just the requested range? */
	if ((end != TLB_FLUSH_ALL) &&
	    !(vmflag & VM_HUGETLB) &&
	    ((end - start) >> PAGE_SHIFT) <= tlb_single_page_flush_ceiling) {
		info.start = start;
		info.end = end;
	} else {
		info.start = 0UL;
		info.end = TLB_FLUSH_ALL;
	}

	if (mm == this_cpu_read(cpu_tlbstate.loaded_mm)) {
		VM_WARN_ON(irqs_disabled());
		local_irq_disable();
		flush_tlb_func_local(&info, TLB_LOCAL_MM_SHOOTDOWN);
		local_irq_enable();
	}

	if (cpumask_any_but(mm_cpumask(mm), cpu) < nr_cpu_ids)
		flush_tlb_others(mm_cpumask(mm), &info);
	put_cpu();
}


static void do_flush_tlb_all(void *info)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH_RECEIVED);
	__flush_tlb_all();
	if (this_cpu_read(cpu_tlbstate.state) == TLBSTATE_LAZY)
		leave_mm(smp_processor_id());
}

void flush_tlb_all(void)
{
	count_vm_tlb_event(NR_TLB_REMOTE_FLUSH);
	on_each_cpu(do_flush_tlb_all, NULL, 1);
}

static void do_kernel_range_flush(void *info)
{
	struct flush_tlb_info *f = info;
	unsigned long addr;

	/* flush range by one by one 'invlpg' */
	for (addr = f->start; addr < f->end; addr += PAGE_SIZE)
		__flush_tlb_single(addr);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{

	/* Balance as user space task's flush, a bit conservative */
	if (end == TLB_FLUSH_ALL ||
	    (end - start) > tlb_single_page_flush_ceiling << PAGE_SHIFT) {
		on_each_cpu(do_flush_tlb_all, NULL, 1);
	} else {
		struct flush_tlb_info info;
		info.start = start;
		info.end = end;
		on_each_cpu(do_kernel_range_flush, &info, 1);
	}
}

void arch_tlbbatch_flush(struct arch_tlbflush_unmap_batch *batch)
{
	struct flush_tlb_info info = {
		.mm = NULL,
		.start = 0UL,
		.end = TLB_FLUSH_ALL,
	};

	int cpu = get_cpu();

	if (cpumask_test_cpu(cpu, &batch->cpumask)) {
		VM_WARN_ON(irqs_disabled());
		local_irq_disable();
		flush_tlb_func_local(&info, TLB_LOCAL_SHOOTDOWN);
		local_irq_enable();
	}

	if (cpumask_any_but(&batch->cpumask, cpu) < nr_cpu_ids)
		flush_tlb_others(&batch->cpumask, &info);
	cpumask_clear(&batch->cpumask);

	put_cpu();
}

static ssize_t tlbflush_read_file(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	char buf[32];
	unsigned int len;

	len = sprintf(buf, "%ld\n", tlb_single_page_flush_ceiling);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t tlbflush_write_file(struct file *file,
		 const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	ssize_t len;
	int ceiling;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';
	if (kstrtoint(buf, 0, &ceiling))
		return -EINVAL;

	if (ceiling < 0)
		return -EINVAL;

	tlb_single_page_flush_ceiling = ceiling;
	return count;
}

static const struct file_operations fops_tlbflush = {
	.read = tlbflush_read_file,
	.write = tlbflush_write_file,
	.llseek = default_llseek,
};

static int __init create_tlb_single_page_flush_ceiling(void)
{
	debugfs_create_file("tlb_single_page_flush_ceiling", S_IRUSR | S_IWUSR,
			    arch_debugfs_dir, NULL, &fops_tlbflush);
	return 0;
}
late_initcall(create_tlb_single_page_flush_ceiling);
