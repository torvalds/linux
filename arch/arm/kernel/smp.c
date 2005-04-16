/*
 *  linux/arch/arm/kernel/smp.c
 *
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/cache.h>
#include <linux/profile.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/seq_file.h>

#include <asm/atomic.h>
#include <asm/cacheflush.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/ptrace.h>

/*
 * bitmask of present and online CPUs.
 * The present bitmask indicates that the CPU is physically present.
 * The online bitmask indicates that the CPU is up and running.
 */
cpumask_t cpu_present_mask;
cpumask_t cpu_online_map;

/*
 * structures for inter-processor calls
 * - A collection of single bit ipi messages.
 */
struct ipi_data {
	spinlock_t lock;
	unsigned long ipi_count;
	unsigned long bits;
};

static DEFINE_PER_CPU(struct ipi_data, ipi_data) = {
	.lock	= SPIN_LOCK_UNLOCKED,
};

enum ipi_msg_type {
	IPI_TIMER,
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_CPU_STOP,
};

struct smp_call_struct {
	void (*func)(void *info);
	void *info;
	int wait;
	cpumask_t pending;
	cpumask_t unfinished;
};

static struct smp_call_struct * volatile smp_call_function_data;
static DEFINE_SPINLOCK(smp_call_function_lock);

int __init __cpu_up(unsigned int cpu)
{
	struct task_struct *idle;
	int ret;

	/*
	 * Spawn a new process manually.  Grab a pointer to
	 * its task struct so we can mess with it
	 */
	idle = fork_idle(cpu);
	if (IS_ERR(idle)) {
		printk(KERN_ERR "CPU%u: fork() failed\n", cpu);
		return PTR_ERR(idle);
	}

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(cpu, idle);
	if (ret) {
		printk(KERN_CRIT "cpu_up: processor %d failed to boot\n", cpu);
		/*
		 * FIXME: We need to clean up the new idle thread. --rmk
		 */
	}

	return ret;
}

/*
 * Called by both boot and secondaries to move global data into
 * per-processor storage.
 */
void __init smp_store_cpu_info(unsigned int cpuid)
{
	struct cpuinfo_arm *cpu_info = &per_cpu(cpu_data, cpuid);

	cpu_info->loops_per_jiffy = loops_per_jiffy;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	int cpu;
	unsigned long bogosum = 0;

	for_each_online_cpu(cpu)
		bogosum += per_cpu(cpu_data, cpu).loops_per_jiffy;

	printk(KERN_INFO "SMP: Total of %d processors activated "
	       "(%lu.%02lu BogoMIPS).\n",
	       num_online_cpus(),
	       bogosum / (500000/HZ),
	       (bogosum / (5000/HZ)) % 100);
}

void __init smp_prepare_boot_cpu(void)
{
	unsigned int cpu = smp_processor_id();

	cpu_set(cpu, cpu_present_mask);
	cpu_set(cpu, cpu_online_map);
}

static void send_ipi_message(cpumask_t callmap, enum ipi_msg_type msg)
{
	unsigned long flags;
	unsigned int cpu;

	local_irq_save(flags);

	for_each_cpu_mask(cpu, callmap) {
		struct ipi_data *ipi = &per_cpu(ipi_data, cpu);

		spin_lock(&ipi->lock);
		ipi->bits |= 1 << msg;
		spin_unlock(&ipi->lock);
	}

	/*
	 * Call the platform specific cross-CPU call function.
	 */
	smp_cross_call(callmap);

	local_irq_restore(flags);
}

/*
 * You must not call this function with disabled interrupts, from a
 * hardware interrupt handler, nor from a bottom half handler.
 */
int smp_call_function_on_cpu(void (*func)(void *info), void *info, int retry,
                             int wait, cpumask_t callmap)
{
	struct smp_call_struct data;
	unsigned long timeout;
	int ret = 0;

	data.func = func;
	data.info = info;
	data.wait = wait;

	cpu_clear(smp_processor_id(), callmap);
	if (cpus_empty(callmap))
		goto out;

	data.pending = callmap;
	if (wait)
		data.unfinished = callmap;

	/*
	 * try to get the mutex on smp_call_function_data
	 */
	spin_lock(&smp_call_function_lock);
	smp_call_function_data = &data;

	send_ipi_message(callmap, IPI_CALL_FUNC);

	timeout = jiffies + HZ;
	while (!cpus_empty(data.pending) && time_before(jiffies, timeout))
		barrier();

	/*
	 * did we time out?
	 */
	if (!cpus_empty(data.pending)) {
		/*
		 * this may be causing our panic - report it
		 */
		printk(KERN_CRIT
		       "CPU%u: smp_call_function timeout for %p(%p)\n"
		       "      callmap %lx pending %lx, %swait\n",
		       smp_processor_id(), func, info, callmap, data.pending,
		       wait ? "" : "no ");

		/*
		 * TRACE
		 */
		timeout = jiffies + (5 * HZ);
		while (!cpus_empty(data.pending) && time_before(jiffies, timeout))
			barrier();

		if (cpus_empty(data.pending))
			printk(KERN_CRIT "     RESOLVED\n");
		else
			printk(KERN_CRIT "     STILL STUCK\n");
	}

	/*
	 * whatever happened, we're done with the data, so release it
	 */
	smp_call_function_data = NULL;
	spin_unlock(&smp_call_function_lock);

	if (!cpus_empty(data.pending)) {
		ret = -ETIMEDOUT;
		goto out;
	}

	if (wait)
		while (!cpus_empty(data.unfinished))
			barrier();
 out:

	return 0;
}

int smp_call_function(void (*func)(void *info), void *info, int retry,
                      int wait)
{
	return smp_call_function_on_cpu(func, info, retry, wait,
					cpu_online_map);
}

void show_ipi_list(struct seq_file *p)
{
	unsigned int cpu;

	seq_puts(p, "IPI:");

	for_each_online_cpu(cpu)
		seq_printf(p, " %10lu", per_cpu(ipi_data, cpu).ipi_count);

	seq_putc(p, '\n');
}

static void ipi_timer(struct pt_regs *regs)
{
	int user = user_mode(regs);

	irq_enter();
	profile_tick(CPU_PROFILING, regs);
	update_process_times(user);
	irq_exit();
}

/*
 * ipi_call_function - handle IPI from smp_call_function()
 *
 * Note that we copy data out of the cross-call structure and then
 * let the caller know that we're here and have done with their data
 */
static void ipi_call_function(unsigned int cpu)
{
	struct smp_call_struct *data = smp_call_function_data;
	void (*func)(void *info) = data->func;
	void *info = data->info;
	int wait = data->wait;

	cpu_clear(cpu, data->pending);

	func(info);

	if (wait)
		cpu_clear(cpu, data->unfinished);
}

static DEFINE_SPINLOCK(stop_lock);

/*
 * ipi_cpu_stop - handle IPI from smp_send_stop()
 */
static void ipi_cpu_stop(unsigned int cpu)
{
	spin_lock(&stop_lock);
	printk(KERN_CRIT "CPU%u: stopping\n", cpu);
	dump_stack();
	spin_unlock(&stop_lock);

	cpu_clear(cpu, cpu_online_map);

	local_fiq_disable();
	local_irq_disable();

	while (1)
		cpu_relax();
}

/*
 * Main handler for inter-processor interrupts
 *
 * For ARM, the ipimask now only identifies a single
 * category of IPI (Bit 1 IPIs have been replaced by a
 * different mechanism):
 *
 *  Bit 0 - Inter-processor function call
 */
void do_IPI(struct pt_regs *regs)
{
	unsigned int cpu = smp_processor_id();
	struct ipi_data *ipi = &per_cpu(ipi_data, cpu);

	ipi->ipi_count++;

	for (;;) {
		unsigned long msgs;

		spin_lock(&ipi->lock);
		msgs = ipi->bits;
		ipi->bits = 0;
		spin_unlock(&ipi->lock);

		if (!msgs)
			break;

		do {
			unsigned nextmsg;

			nextmsg = msgs & -msgs;
			msgs &= ~nextmsg;
			nextmsg = ffz(~nextmsg);

			switch (nextmsg) {
			case IPI_TIMER:
				ipi_timer(regs);
				break;

			case IPI_RESCHEDULE:
				/*
				 * nothing more to do - eveything is
				 * done on the interrupt return path
				 */
				break;

			case IPI_CALL_FUNC:
				ipi_call_function(cpu);
				break;

			case IPI_CPU_STOP:
				ipi_cpu_stop(cpu);
				break;

			default:
				printk(KERN_CRIT "CPU%u: Unknown IPI message 0x%x\n",
				       cpu, nextmsg);
				break;
			}
		} while (msgs);
	}
}

void smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of_cpu(cpu), IPI_RESCHEDULE);
}

void smp_send_timer(void)
{
	cpumask_t mask = cpu_online_map;
	cpu_clear(smp_processor_id(), mask);
	send_ipi_message(mask, IPI_TIMER);
}

void smp_send_stop(void)
{
	cpumask_t mask = cpu_online_map;
	cpu_clear(smp_processor_id(), mask);
	send_ipi_message(mask, IPI_CPU_STOP);
}

/*
 * not supported here
 */
int __init setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}
