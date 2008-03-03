#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>

#include <asm/mtrr.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#ifdef CONFIG_X86_32
#include <mach_apic.h>
#include <mach_ipi.h>
#else
#include <asm/mach_apic.h>
#endif

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
static void native_smp_send_reschedule(int cpu)
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
static int
native_smp_call_function_mask(cpumask_t mask,
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
	wmb();

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

static void stop_this_cpu(void *dummy)
{
	local_irq_disable();
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	disable_local_APIC();
	if (hlt_works(smp_processor_id()))
		for (;;) halt();
	for (;;);
}

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */

static void native_smp_send_stop(void)
{
	int nolock;
	unsigned long flags;

	if (reboot_force)
		return;

	/* Don't deadlock on the call lock in panic */
	nolock = !spin_trylock(&call_lock);
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
void smp_reschedule_interrupt(struct pt_regs *regs)
{
	ack_APIC_irq();
#ifdef CONFIG_X86_32
	__get_cpu_var(irq_stat).irq_resched_count++;
#else
	add_pda(irq_resched_count, 1);
#endif
}

void smp_call_function_interrupt(struct pt_regs *regs)
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
#ifdef CONFIG_X86_32
	__get_cpu_var(irq_stat).irq_call_count++;
#else
	add_pda(irq_call_count, 1);
#endif
	irq_exit();

	if (wait) {
		mb();
		atomic_inc(&call_data->finished);
	}
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
EXPORT_SYMBOL_GPL(smp_ops);

