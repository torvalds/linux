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
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/ptrace.h>

/*
 * bitmask of present and online CPUs.
 * The present bitmask indicates that the CPU is physically present.
 * The online bitmask indicates that the CPU is up and running.
 */
cpumask_t cpu_possible_map;
cpumask_t cpu_online_map;

/*
 * as from 2.5, kernels no longer have an init_tasks structure
 * so we need some other way of telling a new secondary core
 * where to place its SVC stack
 */
struct secondary_data secondary_data;

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

int __cpuinit __cpu_up(unsigned int cpu)
{
	struct cpuinfo_arm *ci = &per_cpu(cpu_data, cpu);
	struct task_struct *idle = ci->idle;
	pgd_t *pgd;
	pmd_t *pmd;
	int ret;

	/*
	 * Spawn a new process manually, if not already done.
	 * Grab a pointer to its task struct so we can mess with it
	 */
	if (!idle) {
		idle = fork_idle(cpu);
		if (IS_ERR(idle)) {
			printk(KERN_ERR "CPU%u: fork() failed\n", cpu);
			return PTR_ERR(idle);
		}
		ci->idle = idle;
	}

	/*
	 * Allocate initial page tables to allow the new CPU to
	 * enable the MMU safely.  This essentially means a set
	 * of our "standard" page tables, with the addition of
	 * a 1:1 mapping for the physical address of the kernel.
	 */
	pgd = pgd_alloc(&init_mm);
	pmd = pmd_offset(pgd, PHYS_OFFSET);
	*pmd = __pmd((PHYS_OFFSET & PGDIR_MASK) |
		     PMD_TYPE_SECT | PMD_SECT_AP_WRITE);

	/*
	 * We need to tell the secondary core where to find
	 * its stack and the page tables.
	 */
	secondary_data.stack = (void *)idle->thread_info + THREAD_START_SP;
	secondary_data.pgdir = virt_to_phys(pgd);
	wmb();

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(cpu, idle);
	if (ret == 0) {
		unsigned long timeout;

		/*
		 * CPU was successfully started, wait for it
		 * to come online or time out.
		 */
		timeout = jiffies + HZ;
		while (time_before(jiffies, timeout)) {
			if (cpu_online(cpu))
				break;

			udelay(10);
			barrier();
		}

		if (!cpu_online(cpu))
			ret = -EIO;
	}

	secondary_data.stack = 0;
	secondary_data.pgdir = 0;

	*pmd_offset(pgd, PHYS_OFFSET) = __pmd(0);
	pgd_free(pgd);

	if (ret) {
		printk(KERN_CRIT "CPU%u: processor failed to boot\n", cpu);

		/*
		 * FIXME: We need to clean up the new idle thread. --rmk
		 */
	}

	return ret;
}

#ifdef CONFIG_HOTPLUG_CPU
/*
 * __cpu_disable runs on the processor to be shutdown.
 */
int __cpuexit __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();
	struct task_struct *p;
	int ret;

	ret = mach_cpu_disable(cpu);
	if (ret)
		return ret;

	/*
	 * Take this CPU offline.  Once we clear this, we can't return,
	 * and we must not schedule until we're ready to give up the cpu.
	 */
	cpu_clear(cpu, cpu_online_map);

	/*
	 * OK - migrate IRQs away from this CPU
	 */
	migrate_irqs();

	/*
	 * Flush user cache and TLB mappings, and then remove this CPU
	 * from the vm mask set of all processes.
	 */
	flush_cache_all();
	local_flush_tlb_all();

	read_lock(&tasklist_lock);
	for_each_process(p) {
		if (p->mm)
			cpu_clear(cpu, p->mm->cpu_vm_mask);
	}
	read_unlock(&tasklist_lock);

	return 0;
}

/*
 * called on the thread which is asking for a CPU to be shutdown -
 * waits until shutdown has completed, or it is timed out.
 */
void __cpuexit __cpu_die(unsigned int cpu)
{
	if (!platform_cpu_kill(cpu))
		printk("CPU%u: unable to kill\n", cpu);
}

/*
 * Called from the idle thread for the CPU which has been shutdown.
 *
 * Note that we disable IRQs here, but do not re-enable them
 * before returning to the caller. This is also the behaviour
 * of the other hotplug-cpu capable cores, so presumably coming
 * out of idle fixes this.
 */
void __cpuexit cpu_die(void)
{
	unsigned int cpu = smp_processor_id();

	local_irq_disable();
	idle_task_exit();

	/*
	 * actual CPU shutdown procedure is at least platform (if not
	 * CPU) specific
	 */
	platform_cpu_die(cpu);

	/*
	 * Do not return to the idle loop - jump back to the secondary
	 * cpu initialisation.  There's some initialisation which needs
	 * to be repeated to undo the effects of taking the CPU offline.
	 */
	__asm__("mov	sp, %0\n"
	"	b	secondary_start_kernel"
		:
		: "r" ((void *)current->thread_info + THREAD_SIZE - 8));
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * This is the secondary CPU boot entry.  We're using this CPUs
 * idle thread stack, but a set of temporary page tables.
 */
asmlinkage void __cpuinit secondary_start_kernel(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	printk("CPU%u: Booted secondary processor\n", cpu);

	/*
	 * All kernel threads share the same mm context; grab a
	 * reference and switch to it.
	 */
	atomic_inc(&mm->mm_users);
	atomic_inc(&mm->mm_count);
	current->active_mm = mm;
	cpu_set(cpu, mm->cpu_vm_mask);
	cpu_switch_mm(mm->pgd, mm);
	enter_lazy_tlb(mm, current);
	local_flush_tlb_all();

	cpu_init();

	/*
	 * Give the platform a chance to do its own initialisation.
	 */
	platform_secondary_init(cpu);

	/*
	 * Enable local interrupts.
	 */
	local_irq_enable();
	local_fiq_enable();

	calibrate_delay();

	smp_store_cpu_info(cpu);

	/*
	 * OK, now it's safe to let the boot CPU continue
	 */
	cpu_set(cpu, cpu_online_map);

	/*
	 * OK, it's off to the idle thread for us
	 */
	cpu_idle();
}

/*
 * Called by both boot and secondaries to move global data into
 * per-processor storage.
 */
void __cpuinit smp_store_cpu_info(unsigned int cpuid)
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

	per_cpu(cpu_data, cpu).idle = current;

	cpu_set(cpu, cpu_possible_map);
	cpu_set(cpu, cpu_present_map);
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
		       smp_processor_id(), func, info, *cpus_addr(callmap),
		       *cpus_addr(data.pending), wait ? "" : "no ");

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

	for_each_present_cpu(cpu)
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

static int
on_each_cpu_mask(void (*func)(void *), void *info, int retry, int wait,
		 cpumask_t mask)
{
	int ret = 0;

	preempt_disable();

	ret = smp_call_function_on_cpu(func, info, retry, wait, mask);
	if (cpu_isset(smp_processor_id(), mask))
		func(info);

	preempt_enable();

	return ret;
}

/**********************************************************************/

/*
 * TLB operations
 */
struct tlb_args {
	struct vm_area_struct *ta_vma;
	unsigned long ta_start;
	unsigned long ta_end;
};

static inline void ipi_flush_tlb_all(void *ignored)
{
	local_flush_tlb_all();
}

static inline void ipi_flush_tlb_mm(void *arg)
{
	struct mm_struct *mm = (struct mm_struct *)arg;

	local_flush_tlb_mm(mm);
}

static inline void ipi_flush_tlb_page(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_page(ta->ta_vma, ta->ta_start);
}

static inline void ipi_flush_tlb_kernel_page(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_kernel_page(ta->ta_start);
}

static inline void ipi_flush_tlb_range(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_range(ta->ta_vma, ta->ta_start, ta->ta_end);
}

static inline void ipi_flush_tlb_kernel_range(void *arg)
{
	struct tlb_args *ta = (struct tlb_args *)arg;

	local_flush_tlb_kernel_range(ta->ta_start, ta->ta_end);
}

void flush_tlb_all(void)
{
	on_each_cpu(ipi_flush_tlb_all, NULL, 1, 1);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	cpumask_t mask = mm->cpu_vm_mask;

	on_each_cpu_mask(ipi_flush_tlb_mm, mm, 1, 1, mask);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr)
{
	cpumask_t mask = vma->vm_mm->cpu_vm_mask;
	struct tlb_args ta;

	ta.ta_vma = vma;
	ta.ta_start = uaddr;

	on_each_cpu_mask(ipi_flush_tlb_page, &ta, 1, 1, mask);
}

void flush_tlb_kernel_page(unsigned long kaddr)
{
	struct tlb_args ta;

	ta.ta_start = kaddr;

	on_each_cpu(ipi_flush_tlb_kernel_page, &ta, 1, 1);
}

void flush_tlb_range(struct vm_area_struct *vma,
                     unsigned long start, unsigned long end)
{
	cpumask_t mask = vma->vm_mm->cpu_vm_mask;
	struct tlb_args ta;

	ta.ta_vma = vma;
	ta.ta_start = start;
	ta.ta_end = end;

	on_each_cpu_mask(ipi_flush_tlb_range, &ta, 1, 1, mask);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	struct tlb_args ta;

	ta.ta_start = start;
	ta.ta_end = end;

	on_each_cpu(ipi_flush_tlb_kernel_range, &ta, 1, 1);
}
