/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *      (c) 2002,2003 Andi Kleen, SuSE Labs.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/interrupt.h>

#include <asm/mtrr.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/mach_apic.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/apicdef.h>
#include <asm/idle.h>

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 *
 * 	More scalable flush, from Andi Kleen
 *
 * 	To avoid global state use 8 different call vectors.
 * 	Each CPU uses a specific vector to trigger flushes on other
 * 	CPUs. Depending on the received vector the target CPUs look into
 *	the right per cpu variable for the flush data.
 *
 * 	With more than 8 CPUs they are hashed to the 8 available
 * 	vectors. The limited global vector space forces us to this right now.
 *	In future when interrupts are split into per CPU domains this could be
 *	fixed, at the cost of triggering multiple IPIs in some cases.
 */

union smp_flush_state {
	struct {
		cpumask_t flush_cpumask;
		struct mm_struct *flush_mm;
		unsigned long flush_va;
#define FLUSH_ALL	-1ULL
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
static inline void leave_mm(int cpu)
{
	if (read_pda(mmu_state) == TLBSTATE_OK)
		BUG();
	cpu_clear(cpu, read_pda(active_mm)->cpu_vm_mask);
	load_cr3(swapper_pg_dir);
}

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
 * 1a2) set cpu mmu_state to TLBSTATE_OK
 * 	Now the smp_invalidate_interrupt won't call leave_mm if cpu0
 *	was in lazy tlb mode.
 * 1a3) update cpu active_mm
 * 	Now cpu0 accepts tlb flushes for the new mm.
 * 1a4) cpu_set(cpu, new_mm->cpu_vm_mask);
 * 	Now the other cpus will send tlb flush ipis.
 * 1a4) change cr3.
 * 1b) thread switch without mm change
 *	cpu active_mm is correct, cpu0 already handles
 *	flush ipis.
 * 1b1) set cpu mmu_state to TLBSTATE_OK
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
	sender = ~regs->orig_rax - INVALIDATE_TLB_VECTOR_START;
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
			if (f->flush_va == FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(f->flush_va);
		} else
			leave_mm(cpu);
	}
out:
	ack_APIC_irq();
	cpu_clear(cpu, f->flush_cpumask);
}

static void flush_tlb_others(cpumask_t cpumask, struct mm_struct *mm,
						unsigned long va)
{
	int sender;
	union smp_flush_state *f;

	/* Caller has disabled preemption */
	sender = smp_processor_id() % NUM_INVALIDATE_TLB_VECTORS;
	f = &per_cpu(flush_state, sender);

	/* Could avoid this lock when
	   num_online_cpus() <= NUM_INVALIDATE_TLB_VECTORS, but it is
	   probably not worth checking this for a cache-hot lock. */
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

int __cpuinit init_smp_flush(void)
{
	int i;
	for_each_cpu_mask(i, cpu_possible_map) {
		spin_lock_init(&per_cpu(flush_state, i).tlbstate_lock);
	}
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
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);
	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_current_task);

void flush_tlb_mm (struct mm_struct * mm)
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
		flush_tlb_others(cpu_mask, mm, FLUSH_ALL);

	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_mm);

void flush_tlb_page(struct vm_area_struct * vma, unsigned long va)
{
	struct mm_struct *mm = vma->vm_mm;
	cpumask_t cpu_mask;

	preempt_disable();
	cpu_mask = mm->cpu_vm_mask;
	cpu_clear(smp_processor_id(), cpu_mask);

	if (current->active_mm == mm) {
		if(current->mm)
			__flush_tlb_one(va);
		 else
		 	leave_mm(smp_processor_id());
	}

	if (!cpus_empty(cpu_mask))
		flush_tlb_others(cpu_mask, mm, va);

	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_page);

static void do_flush_tlb_all(void* info)
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

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */

void smp_send_reschedule(int cpu)
{
	send_IPI_mask(cpumask_of_cpu(cpu), RESCHEDULE_VECTOR);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 */
static DEFINE_SPINLOCK(call_lock);

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
};

static struct call_data_struct * call_data;

void lock_ipi_call_lock(void)
{
	spin_lock_irq(&call_lock);
}

void unlock_ipi_call_lock(void)
{
	spin_unlock_irq(&call_lock);
}

/*
 * this function sends a 'generic call function' IPI to one other CPU
 * in the system.
 *
 * cpu is a standard Linux logical CPU number.
 */
static void
__smp_call_function_single(int cpu, void (*func) (void *info), void *info,
				int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus = 1;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	call_data = &data;
	wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_mask(cpumask_of_cpu(cpu), CALL_FUNCTION_VECTOR);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (!wait)
		return;

	while (atomic_read(&data.finished) != cpus)
		cpu_relax();
}

/*
 * smp_call_function_single - Run a function on another CPU
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @nonatomic: Currently unused.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Retrurns 0 on success, else a negative status code.
 *
 * Does not return until the remote CPU is nearly ready to execute <func>
 * or is or has executed.
 */

int smp_call_function_single (int cpu, void (*func) (void *info), void *info,
	int nonatomic, int wait)
{
	/* prevent preemption and reschedule on another processor */
	int me = get_cpu();
	if (cpu == me) {
		put_cpu();
		return 0;
	}
	spin_lock_bh(&call_lock);
	__smp_call_function_single(cpu, func, info, nonatomic, wait);
	spin_unlock_bh(&call_lock);
	put_cpu();
	return 0;
}

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */
static void __smp_call_function (void (*func) (void *info), void *info,
				int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus = num_online_cpus()-1;

	if (!cpus)
		return;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	call_data = &data;
	wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (!wait)
		return;

	while (atomic_read(&data.finished) != cpus)
		cpu_relax();
}

/*
 * smp_call_function - run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @nonatomic: currently unused.
 * @wait: If true, wait (atomically) until function has completed on other
 *        CPUs.
 *
 * Returns 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute func or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 * Actually there are a few legal cases, like panic.
 */
int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
			int wait)
{
	spin_lock(&call_lock);
	__smp_call_function(func,info,nonatomic,wait);
	spin_unlock(&call_lock);
	return 0;
}
EXPORT_SYMBOL(smp_call_function);

void smp_stop_cpu(void)
{
	unsigned long flags;
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	local_irq_save(flags);
	disable_local_APIC();
	local_irq_restore(flags);
}

static void smp_really_stop_cpu(void *dummy)
{
	smp_stop_cpu(); 
	for (;;) 
		halt();
} 

void smp_send_stop(void)
{
	int nolock = 0;
	if (reboot_force)
		return;
	/* Don't deadlock on the call lock in panic */
	if (!spin_trylock(&call_lock)) {
		/* ignore locking because we have panicked anyways */
		nolock = 1;
	}
	__smp_call_function(smp_really_stop_cpu, NULL, 0, 0);
	if (!nolock)
		spin_unlock(&call_lock);

	local_irq_disable();
	disable_local_APIC();
	local_irq_enable();
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
asmlinkage void smp_reschedule_interrupt(void)
{
	ack_APIC_irq();
}

asmlinkage void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	ack_APIC_irq();
	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	mb();
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	exit_idle();
	irq_enter();
	(*func)(info);
	irq_exit();
	if (wait) {
		mb();
		atomic_inc(&call_data->finished);
	}
}

