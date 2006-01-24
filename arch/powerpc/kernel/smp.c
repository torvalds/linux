/*
 * SMP support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * PowerPC-64 Support added by Dave Engebretsen, Peter Bergner, and
 * Mike Corrigan {engebret|bergner|mikec}@us.ibm.com
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>
#include <linux/err.h>
#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/topology.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/cputable.h>
#include <asm/system.h>
#include <asm/mpic.h>
#include <asm/vdso_datapage.h>
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#endif

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

int smp_hw_index[NR_CPUS];
struct thread_info *secondary_ti;

cpumask_t cpu_possible_map = CPU_MASK_NONE;
cpumask_t cpu_online_map = CPU_MASK_NONE;
cpumask_t cpu_sibling_map[NR_CPUS] = { [0 ... NR_CPUS-1] = CPU_MASK_NONE };

EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_possible_map);

/* SMP operations for this machine */
struct smp_ops_t *smp_ops;

static volatile unsigned int cpu_callin_map[NR_CPUS];

void smp_call_function_interrupt(void);

int smt_enabled_at_boot = 1;

static void (*crash_ipi_function_ptr)(struct pt_regs *) = NULL;

#ifdef CONFIG_MPIC
int __init smp_mpic_probe(void)
{
	int nr_cpus;

	DBG("smp_mpic_probe()...\n");

	nr_cpus = cpus_weight(cpu_possible_map);

	DBG("nr_cpus: %d\n", nr_cpus);

	if (nr_cpus > 1)
		mpic_request_ipis();

	return nr_cpus;
}

void __devinit smp_mpic_setup_cpu(int cpu)
{
	mpic_setup_this_cpu();
}
#endif /* CONFIG_MPIC */

#ifdef CONFIG_PPC64
void __devinit smp_generic_kick_cpu(int nr)
{
	BUG_ON(nr < 0 || nr >= NR_CPUS);

	/*
	 * The processor is currently spinning, waiting for the
	 * cpu_start field to become non-zero After we set cpu_start,
	 * the processor will continue on to secondary_start
	 */
	paca[nr].cpu_start = 1;
	smp_mb();
}
#endif

void smp_message_recv(int msg, struct pt_regs *regs)
{
	switch(msg) {
	case PPC_MSG_CALL_FUNCTION:
		smp_call_function_interrupt();
		break;
	case PPC_MSG_RESCHEDULE:
		/* XXX Do we have to do this? */
		set_need_resched();
		break;
	case PPC_MSG_DEBUGGER_BREAK:
		if (crash_ipi_function_ptr) {
			crash_ipi_function_ptr(regs);
			break;
		}
#ifdef CONFIG_DEBUGGER
		debugger_ipi(regs);
		break;
#endif /* CONFIG_DEBUGGER */
		/* FALLTHROUGH */
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

void smp_send_reschedule(int cpu)
{
	smp_ops->message_pass(cpu, PPC_MSG_RESCHEDULE);
}

#ifdef CONFIG_DEBUGGER
void smp_send_debugger_break(int cpu)
{
	smp_ops->message_pass(cpu, PPC_MSG_DEBUGGER_BREAK);
}
#endif

#ifdef CONFIG_KEXEC
void crash_send_ipi(void (*crash_ipi_callback)(struct pt_regs *))
{
	crash_ipi_function_ptr = crash_ipi_callback;
	if (crash_ipi_callback) {
		mb();
		smp_ops->message_pass(MSG_ALL_BUT_SELF, PPC_MSG_DEBUGGER_BREAK);
	}
}
#endif

static void stop_this_cpu(void *dummy)
{
	local_irq_disable();
	while (1)
		;
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
}

/*
 * Structure and data for smp_call_function(). This is designed to minimise
 * static memory requirements. It also looks cleaner.
 * Stolen from the i386 version.
 */
static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(call_lock);

static struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data;

/* delay of at least 8 seconds */
#define SMP_CALL_TIMEOUT	8

/*
 * This function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 *
 * [SUMMARY] Run a function on all other CPUs.
 * <func> The function to run. This must be fast and non-blocking.
 * <info> An arbitrary pointer to pass to the function.
 * <nonatomic> currently unused.
 * <wait> If true, wait (atomically) until function has completed on other CPUs.
 * [RETURNS] 0 on success, else a negative status code. Does not return until
 * remote CPUs are nearly ready to execute <<func>> or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int smp_call_function (void (*func) (void *info), void *info, int nonatomic,
		       int wait)
{ 
	struct call_data_struct data;
	int ret = -1, cpus;
	u64 timeout;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&call_lock);
	/* Must grab online cpu count with preempt disabled, otherwise
	 * it can change. */
	cpus = num_online_cpus() - 1;
	if (!cpus) {
		ret = 0;
		goto out;
	}

	call_data = &data;
	smp_wmb();
	/* Send a message to all other CPUs and wait for them to respond */
	smp_ops->message_pass(MSG_ALL_BUT_SELF, PPC_MSG_CALL_FUNCTION);

	timeout = get_tb() + (u64) SMP_CALL_TIMEOUT * tb_ticks_per_sec;

	/* Wait for response */
	while (atomic_read(&data.started) != cpus) {
		HMT_low();
		if (get_tb() >= timeout) {
			printk("smp_call_function on cpu %d: other cpus not "
			       "responding (%d)\n", smp_processor_id(),
			       atomic_read(&data.started));
			debugger(NULL);
			goto out;
		}
	}

	if (wait) {
		while (atomic_read(&data.finished) != cpus) {
			HMT_low();
			if (get_tb() >= timeout) {
				printk("smp_call_function on cpu %d: other "
				       "cpus not finishing (%d/%d)\n",
				       smp_processor_id(),
				       atomic_read(&data.finished),
				       atomic_read(&data.started));
				debugger(NULL);
				goto out;
			}
		}
	}

	ret = 0;

 out:
	call_data = NULL;
	HMT_medium();
	spin_unlock(&call_lock);
	return ret;
}

EXPORT_SYMBOL(smp_call_function);

void smp_call_function_interrupt(void)
{
	void (*func) (void *info);
	void *info;
	int wait;

	/* call_data will be NULL if the sender timed out while
	 * waiting on us to receive the call.
	 */
	if (!call_data)
		return;

	func = call_data->func;
	info = call_data->info;
	wait = call_data->wait;

	if (!wait)
		smp_mb__before_atomic_inc();

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait) {
		smp_mb__before_atomic_inc();
		atomic_inc(&call_data->finished);
	}
}

extern struct gettimeofday_struct do_gtod;

struct thread_info *current_set[NR_CPUS];

DECLARE_PER_CPU(unsigned int, pvr);

static void __devinit smp_store_cpu_info(int id)
{
	per_cpu(pvr, id) = mfspr(SPRN_PVR);
}

static void __init smp_create_idle(unsigned int cpu)
{
	struct task_struct *p;

	/* create a process for the processor */
	p = fork_idle(cpu);
	if (IS_ERR(p))
		panic("failed fork for CPU %u: %li", cpu, PTR_ERR(p));
#ifdef CONFIG_PPC64
	paca[cpu].__current = p;
#endif
	current_set[cpu] = task_thread_info(p);
	task_thread_info(p)->cpu = cpu;
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu;

	DBG("smp_prepare_cpus\n");

	/* 
	 * setup_cpu may need to be called on the boot cpu. We havent
	 * spun any cpus up but lets be paranoid.
	 */
	BUG_ON(boot_cpuid != smp_processor_id());

	/* Fixup boot cpu */
	smp_store_cpu_info(boot_cpuid);
	cpu_callin_map[boot_cpuid] = 1;

	max_cpus = smp_ops->probe();
 
	smp_space_timers(max_cpus);

	for_each_cpu(cpu)
		if (cpu != boot_cpuid)
			smp_create_idle(cpu);
}

void __devinit smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != boot_cpuid);

	cpu_set(boot_cpuid, cpu_online_map);
#ifdef CONFIG_PPC64
	paca[boot_cpuid].__current = current;
#endif
	current_set[boot_cpuid] = task_thread_info(current);
}

#ifdef CONFIG_HOTPLUG_CPU
/* State of each CPU during hotplug phases */
DEFINE_PER_CPU(int, cpu_state) = { 0 };

int generic_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == boot_cpuid)
		return -EBUSY;

	cpu_clear(cpu, cpu_online_map);
#ifdef CONFIG_PPC64
	vdso_data->processorCount--;
	fixup_irqs(cpu_online_map);
#endif
	return 0;
}

int generic_cpu_enable(unsigned int cpu)
{
	/* Do the normal bootup if we haven't
	 * already bootstrapped. */
	if (system_state != SYSTEM_RUNNING)
		return -ENOSYS;

	/* get the target out of it's holding state */
	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;
	smp_wmb();

	while (!cpu_online(cpu))
		cpu_relax();

#ifdef CONFIG_PPC64
	fixup_irqs(cpu_online_map);
	/* counter the irq disable in fixup_irqs */
	local_irq_enable();
#endif
	return 0;
}

void generic_cpu_die(unsigned int cpu)
{
	int i;

	for (i = 0; i < 100; i++) {
		smp_rmb();
		if (per_cpu(cpu_state, cpu) == CPU_DEAD)
			return;
		msleep(100);
	}
	printk(KERN_ERR "CPU%d didn't die...\n", cpu);
}

void generic_mach_cpu_die(void)
{
	unsigned int cpu;

	local_irq_disable();
	cpu = smp_processor_id();
	printk(KERN_DEBUG "CPU%d offline\n", cpu);
	__get_cpu_var(cpu_state) = CPU_DEAD;
	smp_wmb();
	while (__get_cpu_var(cpu_state) != CPU_UP_PREPARE)
		cpu_relax();

#ifdef CONFIG_PPC64
	flush_tlb_pending();
#endif
	cpu_set(cpu, cpu_online_map);
	local_irq_enable();
}
#endif

static int __devinit cpu_enable(unsigned int cpu)
{
	if (smp_ops->cpu_enable)
		return smp_ops->cpu_enable(cpu);

	return -ENOSYS;
}

int __devinit __cpu_up(unsigned int cpu)
{
	int c;

	secondary_ti = current_set[cpu];
	if (!cpu_enable(cpu))
		return 0;

	if (smp_ops->cpu_bootable && !smp_ops->cpu_bootable(cpu))
		return -EINVAL;

	/* Make sure callin-map entry is 0 (can be leftover a CPU
	 * hotplug
	 */
	cpu_callin_map[cpu] = 0;

	/* The information for processor bringup must
	 * be written out to main store before we release
	 * the processor.
	 */
	smp_mb();

	/* wake up cpus */
	DBG("smp: kicking cpu %d\n", cpu);
	smp_ops->kick_cpu(cpu);

	/*
	 * wait to see if the cpu made a callin (is actually up).
	 * use this value that I found through experimentation.
	 * -- Cort
	 */
	if (system_state < SYSTEM_RUNNING)
		for (c = 5000; c && !cpu_callin_map[cpu]; c--)
			udelay(100);
#ifdef CONFIG_HOTPLUG_CPU
	else
		/*
		 * CPUs can take much longer to come up in the
		 * hotplug case.  Wait five seconds.
		 */
		for (c = 25; c && !cpu_callin_map[cpu]; c--) {
			msleep(200);
		}
#endif

	if (!cpu_callin_map[cpu]) {
		printk("Processor %u is stuck.\n", cpu);
		return -ENOENT;
	}

	printk("Processor %u found.\n", cpu);

	if (smp_ops->give_timebase)
		smp_ops->give_timebase();

	/* Wait until cpu puts itself in the online map */
	while (!cpu_online(cpu))
		cpu_relax();

	return 0;
}


/* Activate a secondary processor. */
int __devinit start_secondary(void *unused)
{
	unsigned int cpu = smp_processor_id();

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	smp_store_cpu_info(cpu);
	set_dec(tb_ticks_per_jiffy);
	preempt_disable();
	cpu_callin_map[cpu] = 1;

	smp_ops->setup_cpu(cpu);
	if (smp_ops->take_timebase)
		smp_ops->take_timebase();

	spin_lock(&call_lock);
	cpu_set(cpu, cpu_online_map);
	spin_unlock(&call_lock);

	local_irq_enable();

	cpu_idle();
	return 0;
}

int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	cpumask_t old_mask;

	/* We want the setup_cpu() here to be called from CPU 0, but our
	 * init thread may have been "borrowed" by another CPU in the meantime
	 * se we pin us down to CPU 0 for a short while
	 */
	old_mask = current->cpus_allowed;
	set_cpus_allowed(current, cpumask_of_cpu(boot_cpuid));
	
	smp_ops->setup_cpu(boot_cpuid);

	set_cpus_allowed(current, old_mask);

	dump_numa_cpu_topology();
}

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void)
{
	if (smp_ops->cpu_disable)
		return smp_ops->cpu_disable();

	return -ENOSYS;
}

void __cpu_die(unsigned int cpu)
{
	if (smp_ops->cpu_die)
		smp_ops->cpu_die(cpu);
}
#endif
