/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/kernel_stat.h>
#include <linux/bootmem.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>

/* State of each CPU. */
static DEFINE_PER_CPU(int, cpu_state) = { 0 };

/* The messaging code jumps to this pointer during boot-up */
unsigned long start_cpu_function_addr;

/* Called very early during startup to mark boot cpu as online */
void __init smp_prepare_boot_cpu(void)
{
	int cpu = smp_processor_id();
	set_cpu_online(cpu, 1);
	set_cpu_present(cpu, 1);
	__this_cpu_write(cpu_state, CPU_ONLINE);

	init_messaging();
}

static void start_secondary(void);

/*
 * Called at the top of init() to launch all the other CPUs.
 * They run free to complete their initialization and then wait
 * until they get an IPI from the boot cpu to come online.
 */
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	long rc;
	int cpu, cpu_count;
	int boot_cpu = smp_processor_id();

	current_thread_info()->cpu = boot_cpu;

	/*
	 * Pin this task to the boot CPU while we bring up the others,
	 * just to make sure we don't uselessly migrate as they come up.
	 */
	rc = sched_setaffinity(current->pid, cpumask_of(boot_cpu));
	if (rc != 0)
		pr_err("Couldn't set init affinity to boot cpu (%ld)\n", rc);

	/* Print information about disabled and dataplane cpus. */
	print_disabled_cpus();

	/*
	 * Tell the messaging subsystem how to respond to the
	 * startup message.  We use a level of indirection to avoid
	 * confusing the linker with the fact that the messaging
	 * subsystem is calling __init code.
	 */
	start_cpu_function_addr = (unsigned long) &online_secondary;

	/* Set up thread context for all new processors. */
	cpu_count = 1;
	for (cpu = 0; cpu < NR_CPUS; ++cpu)	{
		struct task_struct *idle;

		if (cpu == boot_cpu)
			continue;

		if (!cpu_possible(cpu)) {
			/*
			 * Make this processor do nothing on boot.
			 * Note that we don't give the boot_pc function
			 * a stack, so it has to be assembly code.
			 */
			per_cpu(boot_sp, cpu) = 0;
			per_cpu(boot_pc, cpu) = (unsigned long) smp_nap;
			continue;
		}

		/* Create a new idle thread to run start_secondary() */
		idle = fork_idle(cpu);
		if (IS_ERR(idle))
			panic("failed fork for CPU %d", cpu);
		idle->thread.pc = (unsigned long) start_secondary;

		/* Make this thread the boot thread for this processor */
		per_cpu(boot_sp, cpu) = task_ksp0(idle);
		per_cpu(boot_pc, cpu) = idle->thread.pc;

		++cpu_count;
	}
	BUG_ON(cpu_count > (max_cpus ? max_cpus : 1));

	/* Fire up the other tiles, if any */
	init_cpu_present(cpu_possible_mask);
	if (cpumask_weight(cpu_present_mask) > 1) {
		mb();  /* make sure all data is visible to new processors */
		hv_start_all_tiles();
	}
}

static __initdata struct cpumask init_affinity;

static __init int reset_init_affinity(void)
{
	long rc = sched_setaffinity(current->pid, &init_affinity);
	if (rc != 0)
		pr_warn("couldn't reset init affinity (%ld)\n", rc);
	return 0;
}
late_initcall(reset_init_affinity);

static struct cpumask cpu_started;

/*
 * Activate a secondary processor.  Very minimal; don't add anything
 * to this path without knowing what you're doing, since SMP booting
 * is pretty fragile.
 */
static void start_secondary(void)
{
	int cpuid;

	preempt_disable();

	cpuid = smp_processor_id();

	/* Set our thread pointer appropriately. */
	set_my_cpu_offset(__per_cpu_offset[cpuid]);

	/*
	 * In large machines even this will slow us down, since we
	 * will be contending for for the printk spinlock.
	 */
	/* printk(KERN_DEBUG "Initializing CPU#%d\n", cpuid); */

	/* Initialize the current asid for our first page table. */
	__this_cpu_write(current_asid, min_asid);

	/* Set up this thread as another owner of the init_mm */
	mmgrab(&init_mm);
	current->active_mm = &init_mm;
	if (current->mm)
		BUG();
	enter_lazy_tlb(&init_mm, current);

	/* Allow hypervisor messages to be received */
	init_messaging();
	local_irq_enable();

	/* Indicate that we're ready to come up. */
	/* Must not do this before we're ready to receive messages */
	if (cpumask_test_and_set_cpu(cpuid, &cpu_started)) {
		pr_warn("CPU#%d already started!\n", cpuid);
		for (;;)
			local_irq_enable();
	}

	smp_nap();
}

/*
 * Bring a secondary processor online.
 */
void online_secondary(void)
{
	/*
	 * low-memory mappings have been cleared, flush them from
	 * the local TLBs too.
	 */
	local_flush_tlb();

	BUG_ON(in_interrupt());

	/* This must be done before setting cpu_online_mask */
	wmb();

	notify_cpu_starting(smp_processor_id());

	set_cpu_online(smp_processor_id(), 1);
	__this_cpu_write(cpu_state, CPU_ONLINE);

	/* Set up tile-specific state for this cpu. */
	setup_cpu(0);

	/* Set up tile-timer clock-event device on this cpu */
	setup_tile_timer();

	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	/* Wait 5s total for all CPUs for them to come online */
	static int timeout;
	for (; !cpumask_test_cpu(cpu, &cpu_started); timeout++) {
		if (timeout >= 50000) {
			pr_info("skipping unresponsive cpu%d\n", cpu);
			local_irq_enable();
			return -EIO;
		}
		udelay(100);
	}

	local_irq_enable();
	per_cpu(cpu_state, cpu) = CPU_UP_PREPARE;

	/* Unleash the CPU! */
	send_IPI_single(cpu, MSG_TAG_START_CPU);
	while (!cpumask_test_cpu(cpu, cpu_online_mask))
		cpu_relax();
	return 0;
}

static void panic_start_cpu(void)
{
	panic("Received a MSG_START_CPU IPI after boot finished.");
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	int cpu, next, rc;

	/* Reset the response to a (now illegal) MSG_START_CPU IPI. */
	start_cpu_function_addr = (unsigned long) &panic_start_cpu;

	cpumask_copy(&init_affinity, cpu_online_mask);

	/*
	 * Pin ourselves to a single cpu in the initial affinity set
	 * so that kernel mappings for the rootfs are not in the dataplane,
	 * if set, and to avoid unnecessary migrating during bringup.
	 * Use the last cpu just in case the whole chip has been
	 * isolated from the scheduler, to keep init away from likely
	 * more useful user code.  This also ensures that work scheduled
	 * via schedule_delayed_work() in the init routines will land
	 * on this cpu.
	 */
	for (cpu = cpumask_first(&init_affinity);
	     (next = cpumask_next(cpu, &init_affinity)) < nr_cpu_ids;
	     cpu = next)
		;
	rc = sched_setaffinity(current->pid, cpumask_of(cpu));
	if (rc != 0)
		pr_err("Couldn't set init affinity to cpu %d (%d)\n", cpu, rc);
}
