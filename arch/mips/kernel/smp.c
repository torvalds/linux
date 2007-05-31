/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Copyright (C) 2000, 2001 Kanoj Sarcar
 * Copyright (C) 2000, 2001 Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2003 Broadcom Corporation
 */
#include <linux/cache.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/cpu.h>

#include <asm/atomic.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>

#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
#endif /* CONFIG_MIPS_MT_SMTC */

cpumask_t phys_cpu_present_map;		/* Bitmask of available CPUs */
volatile cpumask_t cpu_callin_map;	/* Bitmask of started secondaries */
cpumask_t cpu_online_map;		/* Bitmask of currently online CPUs */
int __cpu_number_map[NR_CPUS];		/* Map physical to logical */
int __cpu_logical_map[NR_CPUS];		/* Map logical to physical */

EXPORT_SYMBOL(phys_cpu_present_map);
EXPORT_SYMBOL(cpu_online_map);

/* This happens early in bootup, can't really do it better */
static void smp_tune_scheduling (void)
{
	struct cache_desc *cd = &current_cpu_data.scache;
	unsigned long cachesize = cd->linesz * cd->sets * cd->ways;

	if (cachesize > max_cache_size)
		max_cache_size = cachesize;
}

extern void __init calibrate_delay(void);
extern ATTRIB_NORET void cpu_idle(void);

/*
 * First C code run on the secondary CPUs after being started up by
 * the master.
 */
asmlinkage __cpuinit void start_secondary(void)
{
	unsigned int cpu;

#ifdef CONFIG_MIPS_MT_SMTC
	/* Only do cpu_probe for first TC of CPU */
	if ((read_c0_tcbind() & TCBIND_CURTC) == 0)
#endif /* CONFIG_MIPS_MT_SMTC */
	cpu_probe();
	cpu_report();
	per_cpu_trap_init();
	prom_init_secondary();

	/*
	 * XXX parity protection should be folded in here when it's converted
	 * to an option instead of something based on .cputype
	 */

	calibrate_delay();
	preempt_disable();
	cpu = smp_processor_id();
	cpu_data[cpu].udelay_val = loops_per_jiffy;

	prom_smp_finish();

	cpu_set(cpu, cpu_callin_map);

	cpu_idle();
}

DEFINE_SPINLOCK(smp_call_lock);

struct call_data_struct *call_data;

/*
 * Run a function on all other CPUs.
 *  <func>      The function to run. This must be fast and non-blocking.
 *  <info>      An arbitrary pointer to pass to the function.
 *  <retry>     If true, keep retrying until ready.
 *  <wait>      If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until remote CPUs are nearly ready to execute <func>
 * or are or have executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler:
 *
 * CPU A                               CPU B
 * Disable interrupts
 *                                     smp_call_function()
 *                                     Take call_lock
 *                                     Send IPIs
 *                                     Wait for all cpus to acknowledge IPI
 *                                     CPU A has not responded, spin waiting
 *                                     for cpu A to respond, holding call_lock
 * smp_call_function()
 * Spin waiting for call_lock
 * Deadlock                            Deadlock
 */
int smp_call_function (void (*func) (void *info), void *info, int retry,
								int wait)
{
	struct call_data_struct data;
	int i, cpus = num_online_cpus() - 1;
	int cpu = smp_processor_id();

	/*
	 * Can die spectacularly if this CPU isn't yet marked online
	 */
	BUG_ON(!cpu_online(cpu));

	if (!cpus)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&smp_call_lock);
	call_data = &data;
	smp_mb();

	/* Send a message to all other CPUs and wait for them to respond */
	for_each_online_cpu(i)
		if (i != cpu)
			core_send_ipi(i, SMP_CALL_FUNCTION);

	/* Wait for response */
	/* FIXME: lock-up detection, backtrace on lock-up */
	while (atomic_read(&data.started) != cpus)
		barrier();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			barrier();
	call_data = NULL;
	spin_unlock(&smp_call_lock);

	return 0;
}


void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function.
	 */
	smp_mb();
	atomic_inc(&call_data->started);

	/*
	 * At this point the info structure may be out of scope unless wait==1.
	 */
	irq_enter();
	(*func)(info);
	irq_exit();

	if (wait) {
		smp_mb();
		atomic_inc(&call_data->finished);
	}
}

static void stop_this_cpu(void *dummy)
{
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	local_irq_enable();	/* May need to service _machine_restart IPI */
	for (;;);		/* Wait if available. */
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 1, 0);
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	prom_cpus_done();
}

/* called from main before smp_init() */
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	init_new_context(current, &init_mm);
	current_thread_info()->cpu = 0;
	smp_tune_scheduling();
	plat_prepare_cpus(max_cpus);
#ifndef CONFIG_HOTPLUG_CPU
	cpu_present_map = cpu_possible_map;
#endif
}

/* preload SMP state for boot cpu */
void __devinit smp_prepare_boot_cpu(void)
{
	/*
	 * This assumes that bootup is always handled by the processor
	 * with the logic and physical number 0.
	 */
	__cpu_number_map[0] = 0;
	__cpu_logical_map[0] = 0;
	cpu_set(0, phys_cpu_present_map);
	cpu_set(0, cpu_online_map);
	cpu_set(0, cpu_callin_map);
}

/*
 * Called once for each "cpu_possible(cpu)".  Needs to spin up the cpu
 * and keep control until "cpu_online(cpu)" is set.  Note: cpu is
 * physical, not logical.
 */
int __cpuinit __cpu_up(unsigned int cpu)
{
	struct task_struct *idle;

	/*
	 * Processor goes to start_secondary(), sets online flag
	 * The following code is purely to make sure
	 * Linux can schedule processes on this slave.
	 */
	idle = fork_idle(cpu);
	if (IS_ERR(idle))
		panic(KERN_ERR "Fork failed for CPU %d", cpu);

	prom_boot_secondary(cpu, idle);

	/*
	 * Trust is futile.  We should really have timeouts ...
	 */
	while (!cpu_isset(cpu, cpu_callin_map))
		udelay(100);

	cpu_set(cpu, cpu_online_map);

	return 0;
}

/* Not really SMP stuff ... */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

static void flush_tlb_all_ipi(void *info)
{
	local_flush_tlb_all();
}

void flush_tlb_all(void)
{
	on_each_cpu(flush_tlb_all_ipi, NULL, 1, 1);
}

static void flush_tlb_mm_ipi(void *mm)
{
	local_flush_tlb_mm((struct mm_struct *)mm);
}

/*
 * Special Variant of smp_call_function for use by TLB functions:
 *
 *  o No return value
 *  o collapses to normal function call on UP kernels
 *  o collapses to normal function call on systems with a single shared
 *    primary cache.
 *  o CONFIG_MIPS_MT_SMTC currently implies there is only one physical core.
 */
static inline void smp_on_other_tlbs(void (*func) (void *info), void *info)
{
#ifndef CONFIG_MIPS_MT_SMTC
	smp_call_function(func, info, 1, 1);
#endif
}

static inline void smp_on_each_tlb(void (*func) (void *info), void *info)
{
	preempt_disable();

	smp_on_other_tlbs(func, info);
	func(info);

	preempt_enable();
}

/*
 * The following tlb flush calls are invoked when old translations are
 * being torn down, or pte attributes are changing. For single threaded
 * address spaces, a new context is obtained on the current cpu, and tlb
 * context on other cpus are invalidated to force a new context allocation
 * at switch_mm time, should the mm ever be used on other cpus. For
 * multithreaded address spaces, intercpu interrupts have to be sent.
 * Another case where intercpu interrupts are required is when the target
 * mm might be active on another cpu (eg debuggers doing the flushes on
 * behalf of debugees, kswapd stealing pages from another process etc).
 * Kanoj 07/00.
 */

void flush_tlb_mm(struct mm_struct *mm)
{
	preempt_disable();

	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		smp_on_other_tlbs(flush_tlb_mm_ipi, (void *)mm);
	} else {
		int i;
		for (i = 0; i < num_online_cpus(); i++)
			if (smp_processor_id() != i)
				cpu_context(i, mm) = 0;
	}
	local_flush_tlb_mm(mm);

	preempt_enable();
}

struct flush_tlb_data {
	struct vm_area_struct *vma;
	unsigned long addr1;
	unsigned long addr2;
};

static void flush_tlb_range_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_range(fd->vma, fd->addr1, fd->addr2);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();
	if ((atomic_read(&mm->mm_users) != 1) || (current->mm != mm)) {
		struct flush_tlb_data fd;

		fd.vma = vma;
		fd.addr1 = start;
		fd.addr2 = end;
		smp_on_other_tlbs(flush_tlb_range_ipi, (void *)&fd);
	} else {
		int i;
		for (i = 0; i < num_online_cpus(); i++)
			if (smp_processor_id() != i)
				cpu_context(i, mm) = 0;
	}
	local_flush_tlb_range(vma, start, end);
	preempt_enable();
}

static void flush_tlb_kernel_range_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_kernel_range(fd->addr1, fd->addr2);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct flush_tlb_data fd;

	fd.addr1 = start;
	fd.addr2 = end;
	on_each_cpu(flush_tlb_kernel_range_ipi, (void *)&fd, 1, 1);
}

static void flush_tlb_page_ipi(void *info)
{
	struct flush_tlb_data *fd = (struct flush_tlb_data *)info;

	local_flush_tlb_page(fd->vma, fd->addr1);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	preempt_disable();
	if ((atomic_read(&vma->vm_mm->mm_users) != 1) || (current->mm != vma->vm_mm)) {
		struct flush_tlb_data fd;

		fd.vma = vma;
		fd.addr1 = page;
		smp_on_other_tlbs(flush_tlb_page_ipi, (void *)&fd);
	} else {
		int i;
		for (i = 0; i < num_online_cpus(); i++)
			if (smp_processor_id() != i)
				cpu_context(i, vma->vm_mm) = 0;
	}
	local_flush_tlb_page(vma, page);
	preempt_enable();
}

static void flush_tlb_one_ipi(void *info)
{
	unsigned long vaddr = (unsigned long) info;

	local_flush_tlb_one(vaddr);
}

void flush_tlb_one(unsigned long vaddr)
{
	smp_on_each_tlb(flush_tlb_one_ipi, (void *) vaddr);
}

EXPORT_SYMBOL(flush_tlb_page);
EXPORT_SYMBOL(flush_tlb_one);
