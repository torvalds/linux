/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <linux/percpu.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>

#ifdef CONFIG_SMP

#include <linux/sched.h>
#include <linux/module.h>
#include <linux/threads.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/hardirq.h>
#include <asm/smp.h>
#include <asm/processor.h>
#include <asm/spinlock.h>
#include <kern.h>
#include <irq_user.h>
#include <os.h>

/* Per CPU bogomips and other parameters
 * The only piece used here is the ipi pipe, which is set before SMP is
 * started and never changed.
 */
struct cpuinfo_um cpu_data[NR_CPUS];

/* A statistic, can be a little off */
int num_reschedules_sent = 0;

/* Not changed after boot */
struct task_struct *idle_threads[NR_CPUS];

void smp_send_reschedule(int cpu)
{
	os_write_file(cpu_data[cpu].ipi_pipe[1], "R", 1);
	num_reschedules_sent++;
}

void smp_send_stop(void)
{
	int i;

	printk(KERN_INFO "Stopping all CPUs...");
	for (i = 0; i < num_online_cpus(); i++) {
		if (i == current_thread->cpu)
			continue;
		os_write_file(cpu_data[i].ipi_pipe[1], "S", 1);
	}
	printk(KERN_CONT "done\n");
}

static cpumask_t smp_commenced_mask = CPU_MASK_NONE;
static cpumask_t cpu_callin_map = CPU_MASK_NONE;

static int idle_proc(void *cpup)
{
	int cpu = (int) cpup, err;

	err = os_pipe(cpu_data[cpu].ipi_pipe, 1, 1);
	if (err < 0)
		panic("CPU#%d failed to create IPI pipe, err = %d", cpu, -err);

	os_set_fd_async(cpu_data[cpu].ipi_pipe[0]);

	wmb();
	if (cpu_test_and_set(cpu, cpu_callin_map)) {
		printk(KERN_ERR "huh, CPU#%d already present??\n", cpu);
		BUG();
	}

	while (!cpu_isset(cpu, smp_commenced_mask))
		cpu_relax();

	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);
	default_idle();
	return 0;
}

static struct task_struct *idle_thread(int cpu)
{
	struct task_struct *new_task;

	current->thread.request.u.thread.proc = idle_proc;
	current->thread.request.u.thread.arg = (void *) cpu;
	new_task = fork_idle(cpu);
	if (IS_ERR(new_task))
		panic("copy_process failed in idle_thread, error = %ld",
		      PTR_ERR(new_task));

	cpu_tasks[cpu] = ((struct cpu_task)
		          { .pid = 	new_task->thread.mode.tt.extern_pid,
			    .task = 	new_task } );
	idle_threads[cpu] = new_task;
	panic("skas mode doesn't support SMP");
	return new_task;
}

void smp_prepare_cpus(unsigned int maxcpus)
{
	struct task_struct *idle;
	unsigned long waittime;
	int err, cpu, me = smp_processor_id();
	int i;

	for (i = 0; i < ncpus; ++i)
		set_cpu_possible(i, true);

	set_cpu_online(me, true);
	cpu_set(me, cpu_callin_map);

	err = os_pipe(cpu_data[me].ipi_pipe, 1, 1);
	if (err < 0)
		panic("CPU#0 failed to create IPI pipe, errno = %d", -err);

	os_set_fd_async(cpu_data[me].ipi_pipe[0]);

	for (cpu = 1; cpu < ncpus; cpu++) {
		printk(KERN_INFO "Booting processor %d...\n", cpu);

		idle = idle_thread(cpu);

		init_idle(idle, cpu);

		waittime = 200000000;
		while (waittime-- && !cpu_isset(cpu, cpu_callin_map))
			cpu_relax();

		printk(KERN_INFO "%s\n",
		       cpu_isset(cpu, cpu_calling_map) ? "done" : "failed");
	}
}

void smp_prepare_boot_cpu(void)
{
	set_cpu_online(smp_processor_id(), true);
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	cpu_set(cpu, smp_commenced_mask);
	while (!cpu_online(cpu))
		mb();
	return 0;
}

int setup_profiling_timer(unsigned int multiplier)
{
	printk(KERN_INFO "setup_profiling_timer\n");
	return 0;
}

void smp_call_function_slave(int cpu);

void IPI_handler(int cpu)
{
	unsigned char c;
	int fd;

	fd = cpu_data[cpu].ipi_pipe[0];
	while (os_read_file(fd, &c, 1) == 1) {
		switch (c) {
		case 'C':
			smp_call_function_slave(cpu);
			break;

		case 'R':
			scheduler_ipi();
			break;

		case 'S':
			printk(KERN_INFO "CPU#%d stopping\n", cpu);
			while (1)
				pause();
			break;

		default:
			printk(KERN_ERR "CPU#%d received unknown IPI [%c]!\n",
			       cpu, c);
			break;
		}
	}
}

int hard_smp_processor_id(void)
{
	return pid_to_processor_id(os_getpid());
}

static DEFINE_SPINLOCK(call_lock);
static atomic_t scf_started;
static atomic_t scf_finished;
static void (*func)(void *info);
static void *info;

void smp_call_function_slave(int cpu)
{
	atomic_inc(&scf_started);
	(*func)(info);
	atomic_inc(&scf_finished);
}

int smp_call_function(void (*_func)(void *info), void *_info, int wait)
{
	int cpus = num_online_cpus() - 1;
	int i;

	if (!cpus)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	spin_lock_bh(&call_lock);
	atomic_set(&scf_started, 0);
	atomic_set(&scf_finished, 0);
	func = _func;
	info = _info;

	for_each_online_cpu(i)
		os_write_file(cpu_data[i].ipi_pipe[1], "C", 1);

	while (atomic_read(&scf_started) != cpus)
		barrier();

	if (wait)
		while (atomic_read(&scf_finished) != cpus)
			barrier();

	spin_unlock_bh(&call_lock);
	return 0;
}

#endif
