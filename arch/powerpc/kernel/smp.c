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
#include <asm/cputhreads.h>
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

struct thread_info *secondary_ti;

DEFINE_PER_CPU(cpumask_t, cpu_sibling_map) = CPU_MASK_NONE;
DEFINE_PER_CPU(cpumask_t, cpu_core_map) = CPU_MASK_NONE;

EXPORT_PER_CPU_SYMBOL(cpu_sibling_map);
EXPORT_PER_CPU_SYMBOL(cpu_core_map);

/* SMP operations for this machine */
struct smp_ops_t *smp_ops;

/* Can't be static due to PowerMac hackery */
volatile unsigned int cpu_callin_map[NR_CPUS];

int smt_enabled_at_boot = 1;

static void (*crash_ipi_function_ptr)(struct pt_regs *) = NULL;

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

void smp_message_recv(int msg)
{
	switch(msg) {
	case PPC_MSG_CALL_FUNCTION:
		generic_smp_call_function_interrupt();
		break;
	case PPC_MSG_RESCHEDULE:
		/* we notice need_resched on exit */
		break;
	case PPC_MSG_CALL_FUNC_SINGLE:
		generic_smp_call_function_single_interrupt();
		break;
	case PPC_MSG_DEBUGGER_BREAK:
		if (crash_ipi_function_ptr) {
			crash_ipi_function_ptr(get_irq_regs());
			break;
		}
#ifdef CONFIG_DEBUGGER
		debugger_ipi(get_irq_regs());
		break;
#endif /* CONFIG_DEBUGGER */
		/* FALLTHROUGH */
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

static irqreturn_t call_function_action(int irq, void *data)
{
	generic_smp_call_function_interrupt();
	return IRQ_HANDLED;
}

static irqreturn_t reschedule_action(int irq, void *data)
{
	/* we just need the return path side effect of checking need_resched */
	return IRQ_HANDLED;
}

static irqreturn_t call_function_single_action(int irq, void *data)
{
	generic_smp_call_function_single_interrupt();
	return IRQ_HANDLED;
}

static irqreturn_t debug_ipi_action(int irq, void *data)
{
	smp_message_recv(PPC_MSG_DEBUGGER_BREAK);
	return IRQ_HANDLED;
}

static irq_handler_t smp_ipi_action[] = {
	[PPC_MSG_CALL_FUNCTION] =  call_function_action,
	[PPC_MSG_RESCHEDULE] = reschedule_action,
	[PPC_MSG_CALL_FUNC_SINGLE] = call_function_single_action,
	[PPC_MSG_DEBUGGER_BREAK] = debug_ipi_action,
};

const char *smp_ipi_name[] = {
	[PPC_MSG_CALL_FUNCTION] =  "ipi call function",
	[PPC_MSG_RESCHEDULE] = "ipi reschedule",
	[PPC_MSG_CALL_FUNC_SINGLE] = "ipi call function single",
	[PPC_MSG_DEBUGGER_BREAK] = "ipi debugger",
};

/* optional function to request ipi, for controllers with >= 4 ipis */
int smp_request_message_ipi(int virq, int msg)
{
	int err;

	if (msg < 0 || msg > PPC_MSG_DEBUGGER_BREAK) {
		return -EINVAL;
	}
#if !defined(CONFIG_DEBUGGER) && !defined(CONFIG_KEXEC)
	if (msg == PPC_MSG_DEBUGGER_BREAK) {
		return 1;
	}
#endif
	err = request_irq(virq, smp_ipi_action[msg], IRQF_DISABLED|IRQF_PERCPU,
			  smp_ipi_name[msg], 0);
	WARN(err < 0, "unable to request_irq %d for %s (rc %d)\n",
		virq, smp_ipi_name[msg], err);

	return err;
}

void smp_send_reschedule(int cpu)
{
	if (likely(smp_ops))
		smp_ops->message_pass(cpu, PPC_MSG_RESCHEDULE);
}

void arch_send_call_function_single_ipi(int cpu)
{
	smp_ops->message_pass(cpu, PPC_MSG_CALL_FUNC_SINGLE);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		smp_ops->message_pass(cpu, PPC_MSG_CALL_FUNCTION);
}

#ifdef CONFIG_DEBUGGER
void smp_send_debugger_break(int cpu)
{
	if (likely(smp_ops))
		smp_ops->message_pass(cpu, PPC_MSG_DEBUGGER_BREAK);
}
#endif

#ifdef CONFIG_KEXEC
void crash_send_ipi(void (*crash_ipi_callback)(struct pt_regs *))
{
	crash_ipi_function_ptr = crash_ipi_callback;
	if (crash_ipi_callback && smp_ops) {
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
	smp_call_function(stop_this_cpu, NULL, 0);
}

struct thread_info *current_set[NR_CPUS];

static void __devinit smp_store_cpu_info(int id)
{
	per_cpu(cpu_pvr, id) = mfspr(SPRN_PVR);
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
	paca[cpu].kstack = (unsigned long) task_thread_info(p)
		+ THREAD_SIZE - STACK_FRAME_OVERHEAD;
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

	if (smp_ops)
		if (smp_ops->probe)
			max_cpus = smp_ops->probe();
		else
			max_cpus = NR_CPUS;
	else
		max_cpus = 1;
 
	smp_space_timers(max_cpus);

	for_each_possible_cpu(cpu)
		if (cpu != boot_cpuid)
			smp_create_idle(cpu);
}

void __devinit smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != boot_cpuid);

	set_cpu_online(boot_cpuid, true);
	cpu_set(boot_cpuid, per_cpu(cpu_sibling_map, boot_cpuid));
	cpu_set(boot_cpuid, per_cpu(cpu_core_map, boot_cpuid));
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

	set_cpu_online(cpu, false);
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
	set_cpu_online(cpu, true);
	local_irq_enable();
}
#endif

static int __devinit cpu_enable(unsigned int cpu)
{
	if (smp_ops && smp_ops->cpu_enable)
		return smp_ops->cpu_enable(cpu);

	return -ENOSYS;
}

int __cpuinit __cpu_up(unsigned int cpu)
{
	int c;

	secondary_ti = current_set[cpu];
	if (!cpu_enable(cpu))
		return 0;

	if (smp_ops == NULL ||
	    (smp_ops->cpu_bootable && !smp_ops->cpu_bootable(cpu)))
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
		for (c = 50000; c && !cpu_callin_map[cpu]; c--)
			udelay(100);
#ifdef CONFIG_HOTPLUG_CPU
	else
		/*
		 * CPUs can take much longer to come up in the
		 * hotplug case.  Wait five seconds.
		 */
		for (c = 5000; c && !cpu_callin_map[cpu]; c--)
			msleep(1);
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

/* Return the value of the reg property corresponding to the given
 * logical cpu.
 */
int cpu_to_core_id(int cpu)
{
	struct device_node *np;
	const int *reg;
	int id = -1;

	np = of_get_cpu_node(cpu, NULL);
	if (!np)
		goto out;

	reg = of_get_property(np, "reg", NULL);
	if (!reg)
		goto out;

	id = *reg;
out:
	of_node_put(np);
	return id;
}

/* Must be called when no change can occur to cpu_present_map,
 * i.e. during cpu online or offline.
 */
static struct device_node *cpu_to_l2cache(int cpu)
{
	struct device_node *np;
	struct device_node *cache;

	if (!cpu_present(cpu))
		return NULL;

	np = of_get_cpu_node(cpu, NULL);
	if (np == NULL)
		return NULL;

	cache = of_find_next_cache_node(np);

	of_node_put(np);

	return cache;
}

/* Activate a secondary processor. */
int __devinit start_secondary(void *unused)
{
	unsigned int cpu = smp_processor_id();
	struct device_node *l2_cache;
	int i, base;

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	smp_store_cpu_info(cpu);
	set_dec(tb_ticks_per_jiffy);
	preempt_disable();
	cpu_callin_map[cpu] = 1;

	if (smp_ops->setup_cpu)
		smp_ops->setup_cpu(cpu);
	if (smp_ops->take_timebase)
		smp_ops->take_timebase();

	if (system_state > SYSTEM_BOOTING)
		snapshot_timebase();

	secondary_cpu_time_init();

	ipi_call_lock();
	notify_cpu_starting(cpu);
	set_cpu_online(cpu, true);
	/* Update sibling maps */
	base = cpu_first_thread_in_core(cpu);
	for (i = 0; i < threads_per_core; i++) {
		if (cpu_is_offline(base + i))
			continue;
		cpu_set(cpu, per_cpu(cpu_sibling_map, base + i));
		cpu_set(base + i, per_cpu(cpu_sibling_map, cpu));

		/* cpu_core_map should be a superset of
		 * cpu_sibling_map even if we don't have cache
		 * information, so update the former here, too.
		 */
		cpu_set(cpu, per_cpu(cpu_core_map, base +i));
		cpu_set(base + i, per_cpu(cpu_core_map, cpu));
	}
	l2_cache = cpu_to_l2cache(cpu);
	for_each_online_cpu(i) {
		struct device_node *np = cpu_to_l2cache(i);
		if (!np)
			continue;
		if (np == l2_cache) {
			cpu_set(cpu, per_cpu(cpu_core_map, i));
			cpu_set(i, per_cpu(cpu_core_map, cpu));
		}
		of_node_put(np);
	}
	of_node_put(l2_cache);
	ipi_call_unlock();

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
	
	if (smp_ops && smp_ops->setup_cpu)
		smp_ops->setup_cpu(boot_cpuid);

	set_cpus_allowed(current, old_mask);

	snapshot_timebases();

	dump_numa_cpu_topology();
}

#ifdef CONFIG_HOTPLUG_CPU
int __cpu_disable(void)
{
	struct device_node *l2_cache;
	int cpu = smp_processor_id();
	int base, i;
	int err;

	if (!smp_ops->cpu_disable)
		return -ENOSYS;

	err = smp_ops->cpu_disable();
	if (err)
		return err;

	/* Update sibling maps */
	base = cpu_first_thread_in_core(cpu);
	for (i = 0; i < threads_per_core; i++) {
		cpu_clear(cpu, per_cpu(cpu_sibling_map, base + i));
		cpu_clear(base + i, per_cpu(cpu_sibling_map, cpu));
		cpu_clear(cpu, per_cpu(cpu_core_map, base +i));
		cpu_clear(base + i, per_cpu(cpu_core_map, cpu));
	}

	l2_cache = cpu_to_l2cache(cpu);
	for_each_present_cpu(i) {
		struct device_node *np = cpu_to_l2cache(i);
		if (!np)
			continue;
		if (np == l2_cache) {
			cpu_clear(cpu, per_cpu(cpu_core_map, i));
			cpu_clear(i, per_cpu(cpu_core_map, cpu));
		}
		of_node_put(np);
	}
	of_node_put(l2_cache);


	return 0;
}

void __cpu_die(unsigned int cpu)
{
	if (smp_ops->cpu_die)
		smp_ops->cpu_die(cpu);
}
#endif
