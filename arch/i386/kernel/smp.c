/*
 *	Intel SMP support routines.
 *
 *	(c) 1995 Alan Cox, Building #3 <alan@redhat.com>
 *	(c) 1998-99, 2000 Ingo Molnar <mingo@redhat.com>
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/module.h>

#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <mach_apic.h>

/*
 *	Some notes on x86 processor bugs affecting SMP operation:
 *
 *	Pentium, Pentium Pro, II, III (and all CPUs) have bugs.
 *	The Linux implications for SMP are handled as follows:
 *
 *	Pentium III / [Xeon]
 *		None of the E1AP-E3AP errata are visible to the user.
 *
 *	E1AP.	see PII A1AP
 *	E2AP.	see PII A2AP
 *	E3AP.	see PII A3AP
 *
 *	Pentium II / [Xeon]
 *		None of the A1AP-A3AP errata are visible to the user.
 *
 *	A1AP.	see PPro 1AP
 *	A2AP.	see PPro 2AP
 *	A3AP.	see PPro 7AP
 *
 *	Pentium Pro
 *		None of 1AP-9AP errata are visible to the normal user,
 *	except occasional delivery of 'spurious interrupt' as trap #15.
 *	This is very rare and a non-problem.
 *
 *	1AP.	Linux maps APIC as non-cacheable
 *	2AP.	worked around in hardware
 *	3AP.	fixed in C0 and above steppings microcode update.
 *		Linux does not use excessive STARTUP_IPIs.
 *	4AP.	worked around in hardware
 *	5AP.	symmetric IO mode (normal Linux operation) not affected.
 *		'noapic' mode has vector 0xf filled out properly.
 *	6AP.	'noapic' mode might be affected - fixed in later steppings
 *	7AP.	We do not assume writes to the LVT deassering IRQs
 *	8AP.	We do not enable low power mode (deep sleep) during MP bootup
 *	9AP.	We do not use mixed mode
 *
 *	Pentium
 *		There is a marginal case where REP MOVS on 100MHz SMP
 *	machines with B stepping processors can fail. XXX should provide
 *	an L1cache=Writethrough or L1cache=off option.
 *
 *		B stepping CPUs may hang. There are hardware work arounds
 *	for this. We warn about it in case your board doesn't have the work
 *	arounds. Basically thats so I can tell anyone with a B stepping
 *	CPU and SMP problems "tough".
 *
 *	Specific items [From Pentium Processor Specification Update]
 *
 *	1AP.	Linux doesn't use remote read
 *	2AP.	Linux doesn't trust APIC errors
 *	3AP.	We work around this
 *	4AP.	Linux never generated 3 interrupts of the same priority
 *		to cause a lost local interrupt.
 *	5AP.	Remote read is never used
 *	6AP.	not affected - worked around in hardware
 *	7AP.	not affected - worked around in hardware
 *	8AP.	worked around in hardware - we get explicit CS errors if not
 *	9AP.	only 'noapic' mode affected. Might generate spurious
 *		interrupts, we log only the first one and count the
 *		rest silently.
 *	10AP.	not affected - worked around in hardware
 *	11AP.	Linux reads the APIC between writes to avoid this, as per
 *		the documentation. Make sure you preserve this as it affects
 *		the C stepping chips too.
 *	12AP.	not affected - worked around in hardware
 *	13AP.	not affected - worked around in hardware
 *	14AP.	we always deassert INIT during bootup
 *	15AP.	not affected - worked around in hardware
 *	16AP.	not affected - worked around in hardware
 *	17AP.	not affected - worked around in hardware
 *	18AP.	not affected - worked around in hardware
 *	19AP.	not affected - worked around in BIOS
 *
 *	If this sounds worrying believe me these bugs are either ___RARE___,
 *	or are signal timing bugs worked around in hardware and there's
 *	about nothing of note with C stepping upwards.
 */

DEFINE_PER_CPU(struct tlb_state, cpu_tlbstate) ____cacheline_aligned = { &init_mm, 0, };

/*
 * the following functions deal with sending IPIs between CPUs.
 *
 * We use 'broadcast', CPU->CPU IPIs and self-IPIs too.
 */

static inline int __prepare_ICR (unsigned int shortcut, int vector)
{
	unsigned int icr = shortcut | APIC_DEST_LOGICAL;

	switch (vector) {
	default:
		icr |= APIC_DM_FIXED | vector;
		break;
	case NMI_VECTOR:
		icr |= APIC_DM_NMI;
		break;
	}
	return icr;
}

static inline int __prepare_ICR2 (unsigned int mask)
{
	return SET_APIC_DEST_FIELD(mask);
}

void __send_IPI_shortcut(unsigned int shortcut, int vector)
{
	/*
	 * Subtle. In the case of the 'never do double writes' workaround
	 * we have to lock out interrupts to be safe.  As we don't care
	 * of the value read we use an atomic rmw access to avoid costly
	 * cli/sti.  Otherwise we use an even cheaper single atomic write
	 * to the APIC.
	 */
	unsigned int cfg;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	/*
	 * No need to touch the target chip field
	 */
	cfg = __prepare_ICR(shortcut, vector);

	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
}

void fastcall send_IPI_self(int vector)
{
	__send_IPI_shortcut(APIC_DEST_SELF, vector);
}

/*
 * This is used to send an IPI with no shorthand notation (the destination is
 * specified in bits 56 to 63 of the ICR).
 */
static inline void __send_IPI_dest_field(unsigned long mask, int vector)
{
	unsigned long cfg;

	/*
	 * Wait for idle.
	 */
	if (unlikely(vector == NMI_VECTOR))
		safe_apic_wait_icr_idle();
	else
		apic_wait_icr_idle();
		
	/*
	 * prepare target chip field
	 */
	cfg = __prepare_ICR2(mask);
	apic_write_around(APIC_ICR2, cfg);
		
	/*
	 * program the ICR 
	 */
	cfg = __prepare_ICR(0, vector);
			
	/*
	 * Send the IPI. The write to APIC_ICR fires this off.
	 */
	apic_write_around(APIC_ICR, cfg);
}

/*
 * This is only used on smaller machines.
 */
void send_IPI_mask_bitmask(cpumask_t cpumask, int vector)
{
	unsigned long mask = cpus_addr(cpumask)[0];
	unsigned long flags;

	local_irq_save(flags);
	WARN_ON(mask & ~cpus_addr(cpu_online_map)[0]);
	__send_IPI_dest_field(mask, vector);
	local_irq_restore(flags);
}

void send_IPI_mask_sequence(cpumask_t mask, int vector)
{
	unsigned long flags;
	unsigned int query_cpu;

	/*
	 * Hack. The clustered APIC addressing mode doesn't allow us to send 
	 * to an arbitrary mask, so I do a unicasts to each CPU instead. This 
	 * should be modified to do 1 message per cluster ID - mbligh
	 */ 

	local_irq_save(flags);
	for (query_cpu = 0; query_cpu < NR_CPUS; ++query_cpu) {
		if (cpu_isset(query_cpu, mask)) {
			__send_IPI_dest_field(cpu_to_logical_apicid(query_cpu),
					      vector);
		}
	}
	local_irq_restore(flags);
}

#include <mach_ipi.h> /* must come after the send_IPI functions above for inlining */

/*
 *	Smarter SMP flushing macros. 
 *		c/o Linus Torvalds.
 *
 *	These mean you can really definitely utterly forget about
 *	writing to user space from interrupts. (Its not allowed anyway).
 *
 *	Optimizations Manfred Spraul <manfred@colorfullife.com>
 */

static cpumask_t flush_cpumask;
static struct mm_struct * flush_mm;
static unsigned long flush_va;
static DEFINE_SPINLOCK(tlbstate_lock);

/*
 * We cannot call mmdrop() because we are in interrupt context, 
 * instead update mm->cpu_vm_mask.
 *
 * We need to reload %cr3 since the page tables may be going
 * away from under us..
 */
static inline void leave_mm (unsigned long cpu)
{
	if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_OK)
		BUG();
	cpu_clear(cpu, per_cpu(cpu_tlbstate, cpu).active_mm->cpu_vm_mask);
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
 * 	for the wrong mm, and in the worst case we perform a superflous
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

fastcall void smp_invalidate_interrupt(struct pt_regs *regs)
{
	unsigned long cpu;

	cpu = get_cpu();

	if (!cpu_isset(cpu, flush_cpumask))
		goto out;
		/* 
		 * This was a BUG() but until someone can quote me the
		 * line from the intel manual that guarantees an IPI to
		 * multiple CPUs is retried _only_ on the erroring CPUs
		 * its staying as a return
		 *
		 * BUG();
		 */
		 
	if (flush_mm == per_cpu(cpu_tlbstate, cpu).active_mm) {
		if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_OK) {
			if (flush_va == TLB_FLUSH_ALL)
				local_flush_tlb();
			else
				__flush_tlb_one(flush_va);
		} else
			leave_mm(cpu);
	}
	ack_APIC_irq();
	smp_mb__before_clear_bit();
	cpu_clear(cpu, flush_cpumask);
	smp_mb__after_clear_bit();
out:
	put_cpu_no_resched();
}

void native_flush_tlb_others(const cpumask_t *cpumaskp, struct mm_struct *mm,
			     unsigned long va)
{
	cpumask_t cpumask = *cpumaskp;

	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	BUG_ON(cpus_empty(cpumask));
	BUG_ON(cpu_isset(smp_processor_id(), cpumask));
	BUG_ON(!mm);

#ifdef CONFIG_HOTPLUG_CPU
	/* If a CPU which we ran on has gone down, OK. */
	cpus_and(cpumask, cpumask, cpu_online_map);
	if (unlikely(cpus_empty(cpumask)))
		return;
#endif

	/*
	 * i'm not happy about this global shared spinlock in the
	 * MM hot path, but we'll see how contended it is.
	 * AK: x86-64 has a faster method that could be ported.
	 */
	spin_lock(&tlbstate_lock);
	
	flush_mm = mm;
	flush_va = va;
	cpus_or(flush_cpumask, cpumask, flush_cpumask);
	/*
	 * We have to send the IPI only to
	 * CPUs affected.
	 */
	send_IPI_mask(cpumask, INVALIDATE_TLB_VECTOR);

	while (!cpus_empty(flush_cpumask))
		/* nothing. lockup detection does not belong here */
		cpu_relax();

	flush_mm = NULL;
	flush_va = 0;
	spin_unlock(&tlbstate_lock);
}
	
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
		flush_tlb_others(cpu_mask, mm, TLB_FLUSH_ALL);

	preempt_enable();
}

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
	if (per_cpu(cpu_tlbstate, cpu).state == TLBSTATE_LAZY)
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
void native_smp_send_reschedule(int cpu)
{
	WARN_ON(cpu_is_offline(cpu));
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

void lock_ipi_call_lock(void)
{
	spin_lock_irq(&call_lock);
}

void unlock_ipi_call_lock(void)
{
	spin_unlock_irq(&call_lock);
}

static struct call_data_struct *call_data;

static void __smp_call_function(void (*func) (void *info), void *info,
				int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus = num_online_cpus() - 1;

	if (!cpus)
		return;

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	call_data = &data;
	mb();
	
	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_VECTOR);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			cpu_relax();
}


/**
 * smp_call_function_mask(): Run a function on a set of other CPUs.
 * @mask: The set of cpus to run on.  Must not include the current cpu.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @wait: If true, wait (atomically) until function has completed on other CPUs.
 *
  * Returns 0 on success, else a negative status code.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int native_smp_call_function_mask(cpumask_t mask,
				  void (*func)(void *), void *info,
				  int wait)
{
	struct call_data_struct data;
	cpumask_t allbutself;
	int cpus;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	/* Holding any lock stops cpus from going down. */
	spin_lock(&call_lock);

	allbutself = cpu_online_map;
	cpu_clear(smp_processor_id(), allbutself);

	cpus_and(mask, mask, allbutself);
	cpus = cpus_weight(mask);

	if (!cpus) {
		spin_unlock(&call_lock);
		return 0;
	}

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	call_data = &data;
	mb();

	/* Send a message to other CPUs */
	if (cpus_equal(mask, allbutself))
		send_IPI_allbutself(CALL_FUNCTION_VECTOR);
	else
		send_IPI_mask(mask, CALL_FUNCTION_VECTOR);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			cpu_relax();
	spin_unlock(&call_lock);

	return 0;
}

/**
 * smp_call_function(): Run a function on all other CPUs.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @nonatomic: Unused.
 * @wait: If true, wait (atomically) until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function(void (*func) (void *info), void *info, int nonatomic,
		      int wait)
{
	return smp_call_function_mask(cpu_online_map, func, info, wait);
}
EXPORT_SYMBOL(smp_call_function);

/**
 * smp_call_function_single - Run a function on another CPU
 * @cpu: The target CPU.  Cannot be the calling CPU.
 * @func: The function to run. This must be fast and non-blocking.
 * @info: An arbitrary pointer to pass to the function.
 * @nonatomic: Unused.
 * @wait: If true, wait until function has completed on other CPUs.
 *
 * Returns 0 on success, else a negative status code.
 *
 * If @wait is true, then returns once @func has returned; otherwise
 * it returns just before the target cpu calls @func.
 */
int smp_call_function_single(int cpu, void (*func) (void *info), void *info,
			     int nonatomic, int wait)
{
	/* prevent preemption and reschedule on another processor */
	int ret;
	int me = get_cpu();
	if (cpu == me) {
		WARN_ON(1);
		put_cpu();
		return -EBUSY;
	}

	ret = smp_call_function_mask(cpumask_of_cpu(cpu), func, info, wait);

	put_cpu();
	return ret;
}
EXPORT_SYMBOL(smp_call_function_single);

static void stop_this_cpu (void * dummy)
{
	local_irq_disable();
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	disable_local_APIC();
	if (cpu_data[smp_processor_id()].hlt_works_ok)
		for(;;) halt();
	for (;;);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

void native_smp_send_stop(void)
{
	/* Don't deadlock on the call lock in panic */
	int nolock = !spin_trylock(&call_lock);
	unsigned long flags;

	local_irq_save(flags);
	__smp_call_function(stop_this_cpu, NULL, 0, 0);
	if (!nolock)
		spin_unlock(&call_lock);
	disable_local_APIC();
	local_irq_restore(flags);
}

/*
 * Reschedule call back. Nothing to do,
 * all the work is done automatically when
 * we return from the interrupt.
 */
fastcall void smp_reschedule_interrupt(struct pt_regs *regs)
{
	ack_APIC_irq();
}

fastcall void smp_call_function_interrupt(struct pt_regs *regs)
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
	irq_enter();
	(*func)(info);
	irq_exit();

	if (wait) {
		mb();
		atomic_inc(&call_data->finished);
	}
}

static int convert_apicid_to_cpu(int apic_id)
{
	int i;

	for (i = 0; i < NR_CPUS; i++) {
		if (x86_cpu_to_apicid[i] == apic_id)
			return i;
	}
	return -1;
}

int safe_smp_processor_id(void)
{
	int apicid, cpuid;

	if (!boot_cpu_has(X86_FEATURE_APIC))
		return 0;

	apicid = hard_smp_processor_id();
	if (apicid == BAD_APICID)
		return 0;

	cpuid = convert_apicid_to_cpu(apicid);

	return cpuid >= 0 ? cpuid : 0;
}

struct smp_ops smp_ops = {
	.smp_prepare_boot_cpu = native_smp_prepare_boot_cpu,
	.smp_prepare_cpus = native_smp_prepare_cpus,
	.cpu_up = native_cpu_up,
	.smp_cpus_done = native_smp_cpus_done,

	.smp_send_stop = native_smp_send_stop,
	.smp_send_reschedule = native_smp_send_reschedule,
	.smp_call_function_mask = native_smp_call_function_mask,
};
