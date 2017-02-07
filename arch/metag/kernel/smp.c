/*
 *  Copyright (C) 2009,2010,2011 Imagination Technologies Ltd.
 *
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/cache.h>
#include <linux/profile.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/irq.h>
#include <linux/bootmem.h>

#include <asm/cacheflush.h>
#include <asm/cachepart.h>
#include <asm/core_reg.h>
#include <asm/cpu.h>
#include <asm/global_lock.h>
#include <asm/metag_mem.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>
#include <asm/hwthread.h>
#include <asm/traps.h>

#define SYSC_DCPART(n)	(SYSC_DCPART0 + SYSC_xCPARTn_STRIDE * (n))
#define SYSC_ICPART(n)	(SYSC_ICPART0 + SYSC_xCPARTn_STRIDE * (n))

DECLARE_PER_CPU(PTBI, pTBI);

void *secondary_data_stack;

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
	.lock	= __SPIN_LOCK_UNLOCKED(ipi_data.lock),
};

static DEFINE_SPINLOCK(boot_lock);

static DECLARE_COMPLETION(cpu_running);

/*
 * "thread" is assumed to be a valid Meta hardware thread ID.
 */
static int boot_secondary(unsigned int thread, struct task_struct *idle)
{
	u32 val;

	/*
	 * set synchronisation state between this boot processor
	 * and the secondary one
	 */
	spin_lock(&boot_lock);

	core_reg_write(TXUPC_ID, 0, thread, (unsigned int)secondary_startup);
	core_reg_write(TXUPC_ID, 1, thread, 0);

	/*
	 * Give the thread privilege (PSTAT) and clear potentially problematic
	 * bits in the process (namely ISTAT, CBMarker, CBMarkerI, LSM_STEP).
	 */
	core_reg_write(TXUCT_ID, TXSTATUS_REGNUM, thread, TXSTATUS_PSTAT_BIT);

	/* Clear the minim enable bit. */
	val = core_reg_read(TXUCT_ID, TXPRIVEXT_REGNUM, thread);
	core_reg_write(TXUCT_ID, TXPRIVEXT_REGNUM, thread, val & ~0x80);

	/*
	 * set the ThreadEnable bit (0x1) in the TXENABLE register
	 * for the specified thread - off it goes!
	 */
	val = core_reg_read(TXUCT_ID, TXENABLE_REGNUM, thread);
	core_reg_write(TXUCT_ID, TXENABLE_REGNUM, thread, val | 0x1);

	/*
	 * now the secondary core is starting up let it run its
	 * calibrations, then wait for it to finish
	 */
	spin_unlock(&boot_lock);

	return 0;
}

/**
 * describe_cachepart_change: describe a change to cache partitions.
 * @thread:	Hardware thread number.
 * @label:	Label of cache type, e.g. "dcache" or "icache".
 * @sz:		Total size of the cache.
 * @old:	Old cache partition configuration (*CPART* register).
 * @new:	New cache partition configuration (*CPART* register).
 *
 * If the cache partition has changed, prints a message to the log describing
 * those changes.
 */
static void describe_cachepart_change(unsigned int thread, const char *label,
				      unsigned int sz, unsigned int old,
				      unsigned int new)
{
	unsigned int lor1, land1, gor1, gand1;
	unsigned int lor2, land2, gor2, gand2;
	unsigned int diff = old ^ new;

	if (!diff)
		return;

	pr_info("Thread %d: %s partition changed:", thread, label);
	if (diff & (SYSC_xCPARTL_OR_BITS | SYSC_xCPARTL_AND_BITS)) {
		lor1   = (old & SYSC_xCPARTL_OR_BITS)  >> SYSC_xCPARTL_OR_S;
		lor2   = (new & SYSC_xCPARTL_OR_BITS)  >> SYSC_xCPARTL_OR_S;
		land1  = (old & SYSC_xCPARTL_AND_BITS) >> SYSC_xCPARTL_AND_S;
		land2  = (new & SYSC_xCPARTL_AND_BITS) >> SYSC_xCPARTL_AND_S;
		pr_cont(" L:%#x+%#x->%#x+%#x",
			(lor1 * sz) >> 4,
			((land1 + 1) * sz) >> 4,
			(lor2 * sz) >> 4,
			((land2 + 1) * sz) >> 4);
	}
	if (diff & (SYSC_xCPARTG_OR_BITS | SYSC_xCPARTG_AND_BITS)) {
		gor1   = (old & SYSC_xCPARTG_OR_BITS)  >> SYSC_xCPARTG_OR_S;
		gor2   = (new & SYSC_xCPARTG_OR_BITS)  >> SYSC_xCPARTG_OR_S;
		gand1  = (old & SYSC_xCPARTG_AND_BITS) >> SYSC_xCPARTG_AND_S;
		gand2  = (new & SYSC_xCPARTG_AND_BITS) >> SYSC_xCPARTG_AND_S;
		pr_cont(" G:%#x+%#x->%#x+%#x",
			(gor1 * sz) >> 4,
			((gand1 + 1) * sz) >> 4,
			(gor2 * sz) >> 4,
			((gand2 + 1) * sz) >> 4);
	}
	if (diff & SYSC_CWRMODE_BIT)
		pr_cont(" %sWR",
			(new & SYSC_CWRMODE_BIT) ? "+" : "-");
	if (diff & SYSC_DCPART_GCON_BIT)
		pr_cont(" %sGCOn",
			(new & SYSC_DCPART_GCON_BIT) ? "+" : "-");
	pr_cont("\n");
}

/**
 * setup_smp_cache: ensure cache coherency for new SMP thread.
 * @thread:	New hardware thread number.
 *
 * Ensures that coherency is enabled and that the threads share the same cache
 * partitions.
 */
static void setup_smp_cache(unsigned int thread)
{
	unsigned int this_thread, lflags;
	unsigned int dcsz, dcpart_this, dcpart_old, dcpart_new;
	unsigned int icsz, icpart_old, icpart_new;

	/*
	 * Copy over the current thread's cache partition configuration to the
	 * new thread so that they share cache partitions.
	 */
	__global_lock2(lflags);
	this_thread = hard_processor_id();
	/* Share dcache partition */
	dcpart_this = metag_in32(SYSC_DCPART(this_thread));
	dcpart_old = metag_in32(SYSC_DCPART(thread));
	dcpart_new = dcpart_this;
#if PAGE_OFFSET < LINGLOBAL_BASE
	/*
	 * For the local data cache to be coherent the threads must also have
	 * GCOn enabled.
	 */
	dcpart_new |= SYSC_DCPART_GCON_BIT;
	metag_out32(dcpart_new, SYSC_DCPART(this_thread));
#endif
	metag_out32(dcpart_new, SYSC_DCPART(thread));
	/* Share icache partition too */
	icpart_new = metag_in32(SYSC_ICPART(this_thread));
	icpart_old = metag_in32(SYSC_ICPART(thread));
	metag_out32(icpart_new, SYSC_ICPART(thread));
	__global_unlock2(lflags);

	/*
	 * Log if the cache partitions were altered so the user is aware of any
	 * potential unintentional cache wastage.
	 */
	dcsz = get_dcache_size();
	icsz = get_dcache_size();
	describe_cachepart_change(this_thread, "dcache", dcsz,
				  dcpart_this, dcpart_new);
	describe_cachepart_change(thread, "dcache", dcsz,
				  dcpart_old, dcpart_new);
	describe_cachepart_change(thread, "icache", icsz,
				  icpart_old, icpart_new);
}

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	unsigned int thread = cpu_2_hwthread_id[cpu];
	int ret;

	load_pgd(swapper_pg_dir, thread);

	flush_tlb_all();

	setup_smp_cache(thread);

	/*
	 * Tell the secondary CPU where to find its idle thread's stack.
	 */
	secondary_data_stack = task_stack_page(idle);

	wmb();

	/*
	 * Now bring the CPU into our world.
	 */
	ret = boot_secondary(thread, idle);
	if (ret == 0) {
		/*
		 * CPU was successfully started, wait for it
		 * to come online or time out.
		 */
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu))
			ret = -EIO;
	}

	secondary_data_stack = NULL;

	if (ret) {
		pr_crit("CPU%u: processor failed to boot\n", cpu);

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
int __cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	/*
	 * Take this CPU offline.  Once we clear this, we can't return,
	 * and we must not schedule until we're ready to give up the cpu.
	 */
	set_cpu_online(cpu, false);

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

	clear_tasks_mm_cpumask(cpu);

	return 0;
}

/*
 * called on the thread which is asking for a CPU to be shutdown -
 * waits until shutdown has completed, or it is timed out.
 */
void __cpu_die(unsigned int cpu)
{
	if (!cpu_wait_death(cpu, 1))
		pr_err("CPU%u: unable to kill\n", cpu);
}

/*
 * Called from the idle thread for the CPU which has been shutdown.
 *
 * Note that we do not return from this function. If this cpu is
 * brought online again it will need to run secondary_startup().
 */
void cpu_die(void)
{
	local_irq_disable();
	idle_task_exit();
	irq_ctx_exit(smp_processor_id());

	(void)cpu_report_death();

	asm ("XOR	TXENABLE, D0Re0,D0Re0\n");
}
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * Called by both boot and secondaries to move global data into
 * per-processor storage.
 */
void smp_store_cpu_info(unsigned int cpuid)
{
	struct cpuinfo_metag *cpu_info = &per_cpu(cpu_data, cpuid);

	cpu_info->loops_per_jiffy = loops_per_jiffy;
}

/*
 * This is the secondary CPU boot entry.  We're using this CPUs
 * idle thread stack and the global page tables.
 */
asmlinkage void secondary_start_kernel(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int cpu = smp_processor_id();

	/*
	 * All kernel threads share the same mm context; grab a
	 * reference and switch to it.
	 */
	atomic_inc(&mm->mm_users);
	mmgrab(mm);
	current->active_mm = mm;
	cpumask_set_cpu(cpu, mm_cpumask(mm));
	enter_lazy_tlb(mm, current);
	local_flush_tlb_all();

	/*
	 * TODO: Some day it might be useful for each Linux CPU to
	 * have its own TBI structure. That would allow each Linux CPU
	 * to run different interrupt handlers for the same IRQ
	 * number.
	 *
	 * For now, simply copying the pointer to the boot CPU's TBI
	 * structure is sufficient because we always want to run the
	 * same interrupt handler whatever CPU takes the interrupt.
	 */
	per_cpu(pTBI, cpu) = __TBI(TBID_ISTAT_BIT);

	if (!per_cpu(pTBI, cpu))
		panic("No TBI found!");

	per_cpu_trap_init(cpu);
	irq_ctx_init(cpu);

	preempt_disable();

	setup_priv();

	notify_cpu_starting(cpu);

	pr_info("CPU%u (thread %u): Booted secondary processor\n",
		cpu, cpu_2_hwthread_id[cpu]);

	calibrate_delay();
	smp_store_cpu_info(cpu);

	/*
	 * OK, now it's safe to let the boot CPU continue
	 */
	set_cpu_online(cpu, true);
	complete(&cpu_running);

	/*
	 * Enable local interrupts.
	 */
	tbi_startup_interrupt(TBID_SIGNUM_TRT);
	local_irq_enable();

	/*
	 * OK, it's off to the idle thread for us
	 */
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	int cpu;
	unsigned long bogosum = 0;

	for_each_online_cpu(cpu)
		bogosum += per_cpu(cpu_data, cpu).loops_per_jiffy;

	pr_info("SMP: Total of %d processors activated (%lu.%02lu BogoMIPS).\n",
		num_online_cpus(),
		bogosum / (500000/HZ),
		(bogosum / (5000/HZ)) % 100);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned int cpu = smp_processor_id();

	init_new_context(current, &init_mm);
	current_thread_info()->cpu = cpu;

	smp_store_cpu_info(cpu);
	init_cpu_present(cpu_possible_mask);
}

void __init smp_prepare_boot_cpu(void)
{
	unsigned int cpu = smp_processor_id();

	per_cpu(pTBI, cpu) = __TBI(TBID_ISTAT_BIT);

	if (!per_cpu(pTBI, cpu))
		panic("No TBI found!");
}

static void smp_cross_call(cpumask_t callmap, enum ipi_msg_type msg);

static void send_ipi_message(const struct cpumask *mask, enum ipi_msg_type msg)
{
	unsigned long flags;
	unsigned int cpu;
	cpumask_t map;

	cpumask_clear(&map);
	local_irq_save(flags);

	for_each_cpu(cpu, mask) {
		struct ipi_data *ipi = &per_cpu(ipi_data, cpu);

		spin_lock(&ipi->lock);

		/*
		 * KICK interrupts are queued in hardware so we'll get
		 * multiple interrupts if we call smp_cross_call()
		 * multiple times for one msg. The problem is that we
		 * only have one bit for each message - we can't queue
		 * them in software.
		 *
		 * The first time through ipi_handler() we'll clear
		 * the msg bit, having done all the work. But when we
		 * return we'll get _another_ interrupt (and another,
		 * and another until we've handled all the queued
		 * KICKs). Running ipi_handler() when there's no work
		 * to do is bad because that's how kick handler
		 * chaining detects who the KICK was intended for.
		 * See arch/metag/kernel/kick.c for more details.
		 *
		 * So only add 'cpu' to 'map' if we haven't already
		 * queued a KICK interrupt for 'msg'.
		 */
		if (!(ipi->bits & (1 << msg))) {
			ipi->bits |= 1 << msg;
			cpumask_set_cpu(cpu, &map);
		}

		spin_unlock(&ipi->lock);
	}

	/*
	 * Call the platform specific cross-CPU call function.
	 */
	smp_cross_call(map, msg);

	local_irq_restore(flags);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	send_ipi_message(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_CALL_FUNC);
}

void show_ipi_list(struct seq_file *p)
{
	unsigned int cpu;

	seq_puts(p, "IPI:");

	for_each_present_cpu(cpu)
		seq_printf(p, " %10lu", per_cpu(ipi_data, cpu).ipi_count);

	seq_putc(p, '\n');
}

static DEFINE_SPINLOCK(stop_lock);

/*
 * Main handler for inter-processor interrupts
 *
 * For Meta, the ipimask now only identifies a single
 * category of IPI (Bit 1 IPIs have been replaced by a
 * different mechanism):
 *
 *  Bit 0 - Inter-processor function call
 */
static int do_IPI(void)
{
	unsigned int cpu = smp_processor_id();
	struct ipi_data *ipi = &per_cpu(ipi_data, cpu);
	unsigned long msgs, nextmsg;
	int handled = 0;

	ipi->ipi_count++;

	spin_lock(&ipi->lock);
	msgs = ipi->bits;
	nextmsg = msgs & -msgs;
	ipi->bits &= ~nextmsg;
	spin_unlock(&ipi->lock);

	if (nextmsg) {
		handled = 1;

		nextmsg = ffz(~nextmsg);
		switch (nextmsg) {
		case IPI_RESCHEDULE:
			scheduler_ipi();
			break;

		case IPI_CALL_FUNC:
			generic_smp_call_function_interrupt();
			break;

		default:
			pr_crit("CPU%u: Unknown IPI message 0x%lx\n",
				cpu, nextmsg);
			break;
		}
	}

	return handled;
}

void smp_send_reschedule(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

static void stop_this_cpu(void *data)
{
	unsigned int cpu = smp_processor_id();

	if (system_state == SYSTEM_BOOTING ||
	    system_state == SYSTEM_RUNNING) {
		spin_lock(&stop_lock);
		pr_crit("CPU%u: stopping\n", cpu);
		dump_stack();
		spin_unlock(&stop_lock);
	}

	set_cpu_online(cpu, false);

	local_irq_disable();

	hard_processor_halt(HALT_OK);
}

void smp_send_stop(void)
{
	smp_call_function(stop_this_cpu, NULL, 0);
}

/*
 * not supported here
 */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

/*
 * We use KICKs for inter-processor interrupts.
 *
 * For every CPU in "callmap" the IPI data must already have been
 * stored in that CPU's "ipi_data" member prior to calling this
 * function.
 */
static void kick_raise_softirq(cpumask_t callmap, unsigned int irq)
{
	int cpu;

	for_each_cpu(cpu, &callmap) {
		unsigned int thread;

		thread = cpu_2_hwthread_id[cpu];

		BUG_ON(thread == BAD_HWTHREAD_ID);

		metag_out32(1, T0KICKI + (thread * TnXKICK_STRIDE));
	}
}

static TBIRES ipi_handler(TBIRES State, int SigNum, int Triggers,
		   int Inst, PTBI pTBI, int *handled)
{
	*handled = do_IPI();

	return State;
}

static struct kick_irq_handler ipi_irq = {
	.func = ipi_handler,
};

static void smp_cross_call(cpumask_t callmap, enum ipi_msg_type msg)
{
	kick_raise_softirq(callmap, 1);
}

static inline unsigned int get_core_count(void)
{
	int i;
	unsigned int ret = 0;

	for (i = 0; i < CONFIG_NR_CPUS; i++) {
		if (core_reg_read(TXUCT_ID, TXENABLE_REGNUM, i))
			ret++;
	}

	return ret;
}

/*
 * Initialise the CPU possible map early - this describes the CPUs
 * which may be present or become present in the system.
 */
void __init smp_init_cpus(void)
{
	unsigned int i, ncores = get_core_count();

	/* If no hwthread_map early param was set use default mapping */
	for (i = 0; i < NR_CPUS; i++)
		if (cpu_2_hwthread_id[i] == BAD_HWTHREAD_ID) {
			cpu_2_hwthread_id[i] = i;
			hwthread_id_2_cpu[i] = i;
		}

	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);

	kick_register_func(&ipi_irq);
}
