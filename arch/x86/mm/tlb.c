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

	/* Load per-mm CR4 state */
	load_mm_cr4(next);

#ifdef CONFIG_MODIFY_LDT_SYSCALL
	/*
	 * Load the LDT, if the LDT is different.
	 *
	 * It's possible that prev->context.ldt doesn't match
	 * the LDT register.  This can happen if leave_mm(prev)
	 * was called and then modify_ldt changed
	 * prev->context.ldt but suppressed an IPI to this CPU.
	 * In this case, prev->context.ldt != NULL, because we
	 * never set context.ldt to NULL while the mm still
	 * exists.  That means that next->context.ldt !=
	 * prev->context.ldt, because mms never share an LDT.
	 */
	if (unlikely(real_prev->context.ldt != next->context.ldt))
		load_mm_ldt(next);
#endif
}

/*
 * The flush IPI assumes that a thread switch happens in this order:
 * [cpu0: the cpu that switches]
 * 1) switch_mm() either 1a) or 1b)
 * 1a) thread switch to a different mm
 * 1a1) set cpu_tlbstate to TLBSTATE_OK
 *	Now the tlb flush NMI handler flush_tlb_func won't call leave_mm
 *	if cpu0 was in lazy tlb mode.
 * 1a2) update cpu active_mm
 *	Now cpu0 accepts tlb flushes for the new mm.
 * 1a3) cpu_set(cpu, new_mm->cpu_vm_mask);
 *	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1a5) cpu_clear(cpu, old_mm->cpu_vm_mask);
 *	Stop ipi delivery for the old mm. This is not synchronized with
 *	the other cpus, but flush_tlb_func ignore flush ipis for the wrong
 *	mm, and in the worst case we perform a superfluous tlb flush.
 * 1b) thread switch without mm change
 *	cpu active_mm is correct, cpu0 already handles flush ipis.
 * 1b1) set cpu_tlbstate to TLBSTATE_OK
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
 * The good news is that cpu_tlbstate is local to each cpu, no
 * write/read ordering problems.
 */

static void flush_tlb_func_common(const struct flush_tlb_info *f,
				  bool local, enum tlb_flush_reason reason)
{
	if (this_cpu_read(cpu_tlbstate.state) != TLBSTATE_OK) {
		leave_mm(smp_processor_id());
		return;
	}

	if (f->end == TLB_FLUSH_ALL) {
		local_flush_tlb();
		if (local)
			count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
		trace_tlb_flush(reason, TLB_FLUSH_ALL);
	} else {
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
	}
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

	/* Synchronize with switch_mm. */
	smp_mb();

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

	if (mm == this_cpu_read(cpu_tlbstate.loaded_mm))
		flush_tlb_func_local(&info, TLB_LOCAL_MM_SHOOTDOWN);
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

	if (cpumask_test_cpu(cpu, &batch->cpumask))
		flush_tlb_func_local(&info, TLB_LOCAL_SHOOTDOWN);
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
