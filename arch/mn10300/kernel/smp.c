/* SMP support routines.
 *
 * Copyright (C) 2006-2008 Panasonic Corporation
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/profile.h>
#include <linux/smp.h>
#include <asm/tlbflush.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/processor.h>
#include <asm/bug.h>
#include <asm/exceptions.h>
#include <asm/hardirq.h>
#include <asm/fpu.h>
#include <asm/mmu_context.h>
#include <asm/thread_info.h>
#include <asm/cpu-regs.h>
#include <asm/intctl-regs.h>
#include "internal.h"

#ifdef CONFIG_HOTPLUG_CPU
#include <linux/cpu.h>
#include <asm/cacheflush.h>

static unsigned long sleep_mode[NR_CPUS];

static void run_sleep_cpu(unsigned int cpu);
static void run_wakeup_cpu(unsigned int cpu);
#endif /* CONFIG_HOTPLUG_CPU */

/*
 * Debug Message function
 */

#undef DEBUG_SMP
#ifdef DEBUG_SMP
#define Dprintk(fmt, ...) printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#else
#define Dprintk(fmt, ...) no_printk(KERN_DEBUG fmt, ##__VA_ARGS__)
#endif

/* timeout value in msec for smp_nmi_call_function. zero is no timeout. */
#define	CALL_FUNCTION_NMI_IPI_TIMEOUT	0

/*
 * Structure and data for smp_nmi_call_function().
 */
struct nmi_call_data_struct {
	smp_call_func_t	func;
	void		*info;
	cpumask_t	started;
	cpumask_t	finished;
	int		wait;
	char		size_alignment[0]
	__attribute__ ((__aligned__(SMP_CACHE_BYTES)));
} __attribute__ ((__aligned__(SMP_CACHE_BYTES)));

static DEFINE_SPINLOCK(smp_nmi_call_lock);
static struct nmi_call_data_struct *nmi_call_data;

/*
 * Data structures and variables
 */
static cpumask_t cpu_callin_map;	/* Bitmask of callin CPUs */
static cpumask_t cpu_callout_map;	/* Bitmask of callout CPUs */
cpumask_t cpu_boot_map;			/* Bitmask of boot APs */
unsigned long start_stack[NR_CPUS - 1];

/*
 * Per CPU parameters
 */
struct mn10300_cpuinfo cpu_data[NR_CPUS] __cacheline_aligned;

static int cpucount;			/* The count of boot CPUs */
static cpumask_t smp_commenced_mask;
cpumask_t cpu_initialized __initdata = CPU_MASK_NONE;

/*
 * Function Prototypes
 */
static int do_boot_cpu(int);
static void smp_show_cpu_info(int cpu_id);
static void smp_callin(void);
static void smp_online(void);
static void smp_store_cpu_info(int);
static void smp_cpu_init(void);
static void smp_tune_scheduling(void);
static void send_IPI_mask(const cpumask_t *cpumask, int irq);
static void init_ipi(void);

/*
 * IPI Initialization interrupt definitions
 */
static void mn10300_ipi_disable(unsigned int irq);
static void mn10300_ipi_enable(unsigned int irq);
static void mn10300_ipi_chip_disable(struct irq_data *d);
static void mn10300_ipi_chip_enable(struct irq_data *d);
static void mn10300_ipi_ack(struct irq_data *d);
static void mn10300_ipi_nop(struct irq_data *d);

static struct irq_chip mn10300_ipi_type = {
	.name		= "cpu_ipi",
	.irq_disable	= mn10300_ipi_chip_disable,
	.irq_enable	= mn10300_ipi_chip_enable,
	.irq_ack	= mn10300_ipi_ack,
	.irq_eoi	= mn10300_ipi_nop
};

static irqreturn_t smp_reschedule_interrupt(int irq, void *dev_id);
static irqreturn_t smp_call_function_interrupt(int irq, void *dev_id);

static struct irqaction reschedule_ipi = {
	.handler	= smp_reschedule_interrupt,
	.name		= "smp reschedule IPI"
};
static struct irqaction call_function_ipi = {
	.handler	= smp_call_function_interrupt,
	.name		= "smp call function IPI"
};

#if !defined(CONFIG_GENERIC_CLOCKEVENTS) || defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)
static irqreturn_t smp_ipi_timer_interrupt(int irq, void *dev_id);
static struct irqaction local_timer_ipi = {
	.handler	= smp_ipi_timer_interrupt,
	.flags		= IRQF_DISABLED,
	.name		= "smp local timer IPI"
};
#endif

/**
 * init_ipi - Initialise the IPI mechanism
 */
static void init_ipi(void)
{
	unsigned long flags;
	u16 tmp16;

	/* set up the reschedule IPI */
	irq_set_chip_and_handler(RESCHEDULE_IPI, &mn10300_ipi_type,
				 handle_percpu_irq);
	setup_irq(RESCHEDULE_IPI, &reschedule_ipi);
	set_intr_level(RESCHEDULE_IPI, RESCHEDULE_GxICR_LV);
	mn10300_ipi_enable(RESCHEDULE_IPI);

	/* set up the call function IPI */
	irq_set_chip_and_handler(CALL_FUNC_SINGLE_IPI, &mn10300_ipi_type,
				 handle_percpu_irq);
	setup_irq(CALL_FUNC_SINGLE_IPI, &call_function_ipi);
	set_intr_level(CALL_FUNC_SINGLE_IPI, CALL_FUNCTION_GxICR_LV);
	mn10300_ipi_enable(CALL_FUNC_SINGLE_IPI);

	/* set up the local timer IPI */
#if !defined(CONFIG_GENERIC_CLOCKEVENTS) || \
    defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)
	irq_set_chip_and_handler(LOCAL_TIMER_IPI, &mn10300_ipi_type,
				 handle_percpu_irq);
	setup_irq(LOCAL_TIMER_IPI, &local_timer_ipi);
	set_intr_level(LOCAL_TIMER_IPI, LOCAL_TIMER_GxICR_LV);
	mn10300_ipi_enable(LOCAL_TIMER_IPI);
#endif

#ifdef CONFIG_MN10300_CACHE_ENABLED
	/* set up the cache flush IPI */
	flags = arch_local_cli_save();
	__set_intr_stub(NUM2EXCEP_IRQ_LEVEL(FLUSH_CACHE_GxICR_LV),
			mn10300_low_ipi_handler);
	GxICR(FLUSH_CACHE_IPI) = FLUSH_CACHE_GxICR_LV | GxICR_DETECT;
	mn10300_ipi_enable(FLUSH_CACHE_IPI);
	arch_local_irq_restore(flags);
#endif

	/* set up the NMI call function IPI */
	flags = arch_local_cli_save();
	GxICR(CALL_FUNCTION_NMI_IPI) = GxICR_NMI | GxICR_ENABLE | GxICR_DETECT;
	tmp16 = GxICR(CALL_FUNCTION_NMI_IPI);
	arch_local_irq_restore(flags);

	/* set up the SMP boot IPI */
	flags = arch_local_cli_save();
	__set_intr_stub(NUM2EXCEP_IRQ_LEVEL(SMP_BOOT_GxICR_LV),
			mn10300_low_ipi_handler);
	arch_local_irq_restore(flags);
}

/**
 * mn10300_ipi_shutdown - Shut down handling of an IPI
 * @irq: The IPI to be shut down.
 */
static void mn10300_ipi_shutdown(unsigned int irq)
{
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();

	tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_DETECT;
	tmp = GxICR(irq);

	arch_local_irq_restore(flags);
}

/**
 * mn10300_ipi_enable - Enable an IPI
 * @irq: The IPI to be enabled.
 */
static void mn10300_ipi_enable(unsigned int irq)
{
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();

	tmp = GxICR(irq);
	GxICR(irq) = (tmp & GxICR_LEVEL) | GxICR_ENABLE;
	tmp = GxICR(irq);

	arch_local_irq_restore(flags);
}

static void mn10300_ipi_chip_enable(struct irq_data *d)
{
	mn10300_ipi_enable(d->irq);
}

/**
 * mn10300_ipi_disable - Disable an IPI
 * @irq: The IPI to be disabled.
 */
static void mn10300_ipi_disable(unsigned int irq)
{
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();

	tmp = GxICR(irq);
	GxICR(irq) = tmp & GxICR_LEVEL;
	tmp = GxICR(irq);

	arch_local_irq_restore(flags);
}

static void mn10300_ipi_chip_disable(struct irq_data *d)
{
	mn10300_ipi_disable(d->irq);
}


/**
 * mn10300_ipi_ack - Acknowledge an IPI interrupt in the PIC
 * @irq: The IPI to be acknowledged.
 *
 * Clear the interrupt detection flag for the IPI on the appropriate interrupt
 * channel in the PIC.
 */
static void mn10300_ipi_ack(struct irq_data *d)
{
	unsigned int irq = d->irq;
	unsigned long flags;
	u16 tmp;

	flags = arch_local_cli_save();
	GxICR_u8(irq) = GxICR_DETECT;
	tmp = GxICR(irq);
	arch_local_irq_restore(flags);
}

/**
 * mn10300_ipi_nop - Dummy IPI action
 * @irq: The IPI to be acted upon.
 */
static void mn10300_ipi_nop(struct irq_data *d)
{
}

/**
 * send_IPI_mask - Send IPIs to all CPUs in list
 * @cpumask: The list of CPUs to target.
 * @irq: The IPI request to be sent.
 *
 * Send the specified IPI to all the CPUs in the list, not waiting for them to
 * finish before returning.  The caller is responsible for synchronisation if
 * that is needed.
 */
static void send_IPI_mask(const cpumask_t *cpumask, int irq)
{
	int i;
	u16 tmp;

	for (i = 0; i < NR_CPUS; i++) {
		if (cpumask_test_cpu(i, cpumask)) {
			/* send IPI */
			tmp = CROSS_GxICR(irq, i);
			CROSS_GxICR(irq, i) =
				tmp | GxICR_REQUEST | GxICR_DETECT;
			tmp = CROSS_GxICR(irq, i); /* flush write buffer */
		}
	}
}

/**
 * send_IPI_self - Send an IPI to this CPU.
 * @irq: The IPI request to be sent.
 *
 * Send the specified IPI to the current CPU.
 */
void send_IPI_self(int irq)
{
	send_IPI_mask(cpumask_of(smp_processor_id()), irq);
}

/**
 * send_IPI_allbutself - Send IPIs to all the other CPUs.
 * @irq: The IPI request to be sent.
 *
 * Send the specified IPI to all CPUs in the system barring the current one,
 * not waiting for them to finish before returning.  The caller is responsible
 * for synchronisation if that is needed.
 */
void send_IPI_allbutself(int irq)
{
	cpumask_t cpumask;

	cpumask_copy(&cpumask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &cpumask);
	send_IPI_mask(&cpumask, irq);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	BUG();
	/*send_IPI_mask(mask, CALL_FUNCTION_IPI);*/
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_IPI_mask(cpumask_of(cpu), CALL_FUNC_SINGLE_IPI);
}

/**
 * smp_send_reschedule - Send reschedule IPI to a CPU
 * @cpu: The CPU to target.
 */
void smp_send_reschedule(int cpu)
{
	send_IPI_mask(cpumask_of(cpu), RESCHEDULE_IPI);
}

/**
 * smp_nmi_call_function - Send a call function NMI IPI to all CPUs
 * @func: The function to ask to be run.
 * @info: The context data to pass to that function.
 * @wait: If true, wait (atomically) until function is run on all CPUs.
 *
 * Send a non-maskable request to all CPUs in the system, requesting them to
 * run the specified function with the given context data, and, potentially, to
 * wait for completion of that function on all CPUs.
 *
 * Returns 0 if successful, -ETIMEDOUT if we were asked to wait, but hit the
 * timeout.
 */
int smp_nmi_call_function(smp_call_func_t func, void *info, int wait)
{
	struct nmi_call_data_struct data;
	unsigned long flags;
	unsigned int cnt;
	int cpus, ret = 0;

	cpus = num_online_cpus() - 1;
	if (cpus < 1)
		return 0;

	data.func = func;
	data.info = info;
	cpumask_copy(&data.started, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &data.started);
	data.wait = wait;
	if (wait)
		data.finished = data.started;

	spin_lock_irqsave(&smp_nmi_call_lock, flags);
	nmi_call_data = &data;
	smp_mb();

	/* Send a message to all other CPUs and wait for them to respond */
	send_IPI_allbutself(CALL_FUNCTION_NMI_IPI);

	/* Wait for response */
	if (CALL_FUNCTION_NMI_IPI_TIMEOUT > 0) {
		for (cnt = 0;
		     cnt < CALL_FUNCTION_NMI_IPI_TIMEOUT &&
			     !cpumask_empty(&data.started);
		     cnt++)
			mdelay(1);

		if (wait && cnt < CALL_FUNCTION_NMI_IPI_TIMEOUT) {
			for (cnt = 0;
			     cnt < CALL_FUNCTION_NMI_IPI_TIMEOUT &&
				     !cpumask_empty(&data.finished);
			     cnt++)
				mdelay(1);
		}

		if (cnt >= CALL_FUNCTION_NMI_IPI_TIMEOUT)
			ret = -ETIMEDOUT;

	} else {
		/* If timeout value is zero, wait until cpumask has been
		 * cleared */
		while (!cpumask_empty(&data.started))
			barrier();
		if (wait)
			while (!cpumask_empty(&data.finished))
				barrier();
	}

	spin_unlock_irqrestore(&smp_nmi_call_lock, flags);
	return ret;
}

/**
 * smp_jump_to_debugger - Make other CPUs enter the debugger by sending an IPI
 *
 * Send a non-maskable request to all other CPUs in the system, instructing
 * them to jump into the debugger.  The caller is responsible for checking that
 * the other CPUs responded to the instruction.
 *
 * The caller should make sure that this CPU's debugger IPI is disabled.
 */
void smp_jump_to_debugger(void)
{
	if (num_online_cpus() > 1)
		/* Send a message to all other CPUs */
		send_IPI_allbutself(DEBUGGER_NMI_IPI);
}

/**
 * stop_this_cpu - Callback to stop a CPU.
 * @unused: Callback context (ignored).
 */
void stop_this_cpu(void *unused)
{
	static volatile int stopflag;
	unsigned long flags;

#ifdef CONFIG_GDBSTUB
	/* In case of single stepping smp_send_stop by other CPU,
	 * clear procindebug to avoid deadlock.
	 */
	atomic_set(&procindebug[smp_processor_id()], 0);
#endif	/* CONFIG_GDBSTUB */

	flags = arch_local_cli_save();
	set_cpu_online(smp_processor_id(), false);

	while (!stopflag)
		cpu_relax();

	set_cpu_online(smp_processor_id(), true);
	arch_local_irq_restore(flags);
}

/**
 * smp_send_stop - Send a stop request to all CPUs.
 */
void smp_send_stop(void)
{
	smp_nmi_call_function(stop_this_cpu, NULL, 0);
}

/**
 * smp_reschedule_interrupt - Reschedule IPI handler
 * @irq: The interrupt number.
 * @dev_id: The device ID.
 *
 * Returns IRQ_HANDLED to indicate we handled the interrupt successfully.
 */
static irqreturn_t smp_reschedule_interrupt(int irq, void *dev_id)
{
	scheduler_ipi();
	return IRQ_HANDLED;
}

/**
 * smp_call_function_interrupt - Call function IPI handler
 * @irq: The interrupt number.
 * @dev_id: The device ID.
 *
 * Returns IRQ_HANDLED to indicate we handled the interrupt successfully.
 */
static irqreturn_t smp_call_function_interrupt(int irq, void *dev_id)
{
	/* generic_smp_call_function_interrupt(); */
	generic_smp_call_function_single_interrupt();
	return IRQ_HANDLED;
}

/**
 * smp_nmi_call_function_interrupt - Non-maskable call function IPI handler
 */
void smp_nmi_call_function_interrupt(void)
{
	smp_call_func_t func = nmi_call_data->func;
	void *info = nmi_call_data->info;
	int wait = nmi_call_data->wait;

	/* Notify the initiating CPU that I've grabbed the data and am about to
	 * execute the function
	 */
	smp_mb();
	cpumask_clear_cpu(smp_processor_id(), &nmi_call_data->started);
	(*func)(info);

	if (wait) {
		smp_mb();
		cpumask_clear_cpu(smp_processor_id(),
				  &nmi_call_data->finished);
	}
}

#if !defined(CONFIG_GENERIC_CLOCKEVENTS) || \
    defined(CONFIG_GENERIC_CLOCKEVENTS_BROADCAST)
/**
 * smp_ipi_timer_interrupt - Local timer IPI handler
 * @irq: The interrupt number.
 * @dev_id: The device ID.
 *
 * Returns IRQ_HANDLED to indicate we handled the interrupt successfully.
 */
static irqreturn_t smp_ipi_timer_interrupt(int irq, void *dev_id)
{
	return local_timer_interrupt();
}
#endif

void __init smp_init_cpus(void)
{
	int i;
	for (i = 0; i < NR_CPUS; i++) {
		set_cpu_possible(i, true);
		set_cpu_present(i, true);
	}
}

/**
 * smp_cpu_init - Initialise AP in start_secondary.
 *
 * For this Application Processor, set up init_mm, initialise FPU and set
 * interrupt level 0-6 setting.
 */
static void __init smp_cpu_init(void)
{
	unsigned long flags;
	int cpu_id = smp_processor_id();
	u16 tmp16;

	if (test_and_set_bit(cpu_id, &cpu_initialized)) {
		printk(KERN_WARNING "CPU#%d already initialized!\n", cpu_id);
		for (;;)
			local_irq_enable();
	}
	printk(KERN_INFO "Initializing CPU#%d\n", cpu_id);

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);

	enter_lazy_tlb(&init_mm, current);

	/* Force FPU initialization */
	clear_using_fpu(current);

	GxICR(CALL_FUNC_SINGLE_IPI) = CALL_FUNCTION_GxICR_LV | GxICR_DETECT;
	mn10300_ipi_enable(CALL_FUNC_SINGLE_IPI);

	GxICR(LOCAL_TIMER_IPI) = LOCAL_TIMER_GxICR_LV | GxICR_DETECT;
	mn10300_ipi_enable(LOCAL_TIMER_IPI);

	GxICR(RESCHEDULE_IPI) = RESCHEDULE_GxICR_LV | GxICR_DETECT;
	mn10300_ipi_enable(RESCHEDULE_IPI);

#ifdef CONFIG_MN10300_CACHE_ENABLED
	GxICR(FLUSH_CACHE_IPI) = FLUSH_CACHE_GxICR_LV | GxICR_DETECT;
	mn10300_ipi_enable(FLUSH_CACHE_IPI);
#endif

	mn10300_ipi_shutdown(SMP_BOOT_IRQ);

	/* Set up the non-maskable call function IPI */
	flags = arch_local_cli_save();
	GxICR(CALL_FUNCTION_NMI_IPI) = GxICR_NMI | GxICR_ENABLE | GxICR_DETECT;
	tmp16 = GxICR(CALL_FUNCTION_NMI_IPI);
	arch_local_irq_restore(flags);
}

/**
 * smp_prepare_cpu_init - Initialise CPU in startup_secondary
 *
 * Set interrupt level 0-6 setting and init ICR of the kernel debugger.
 */
void smp_prepare_cpu_init(void)
{
	int loop;

	/* Set the interrupt vector registers */
	IVAR0 = EXCEP_IRQ_LEVEL0;
	IVAR1 = EXCEP_IRQ_LEVEL1;
	IVAR2 = EXCEP_IRQ_LEVEL2;
	IVAR3 = EXCEP_IRQ_LEVEL3;
	IVAR4 = EXCEP_IRQ_LEVEL4;
	IVAR5 = EXCEP_IRQ_LEVEL5;
	IVAR6 = EXCEP_IRQ_LEVEL6;

	/* Disable all interrupts and set to priority 6 (lowest) */
	for (loop = 0; loop < GxICR_NUM_IRQS; loop++)
		GxICR(loop) = GxICR_LEVEL_6 | GxICR_DETECT;

#ifdef CONFIG_KERNEL_DEBUGGER
	/* initialise the kernel debugger interrupt */
	do {
		unsigned long flags;
		u16 tmp16;

		flags = arch_local_cli_save();
		GxICR(DEBUGGER_NMI_IPI) = GxICR_NMI | GxICR_ENABLE | GxICR_DETECT;
		tmp16 = GxICR(DEBUGGER_NMI_IPI);
		arch_local_irq_restore(flags);
	} while (0);
#endif
}

/**
 * start_secondary - Activate a secondary CPU (AP)
 * @unused: Thread parameter (ignored).
 */
int __init start_secondary(void *unused)
{
	smp_cpu_init();
	smp_callin();
	while (!cpumask_test_cpu(smp_processor_id(), &smp_commenced_mask))
		cpu_relax();

	local_flush_tlb();
	preempt_disable();
	smp_online();

#ifdef CONFIG_GENERIC_CLOCKEVENTS
	init_clockevents();
#endif
	cpu_idle();
	return 0;
}

/**
 * smp_prepare_cpus - Boot up secondary CPUs (APs)
 * @max_cpus: Maximum number of CPUs to boot.
 *
 * Call do_boot_cpu, and boot up APs.
 */
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int phy_id;

	/* Setup boot CPU information */
	smp_store_cpu_info(0);
	smp_tune_scheduling();

	init_ipi();

	/* If SMP should be disabled, then finish */
	if (max_cpus == 0) {
		printk(KERN_INFO "SMP mode deactivated.\n");
		goto smp_done;
	}

	/* Boot secondary CPUs (for which phy_id > 0) */
	for (phy_id = 0; phy_id < NR_CPUS; phy_id++) {
		/* Don't boot primary CPU */
		if (max_cpus <= cpucount + 1)
			continue;
		if (phy_id != 0)
			do_boot_cpu(phy_id);
		set_cpu_possible(phy_id, true);
		smp_show_cpu_info(phy_id);
	}

smp_done:
	Dprintk("Boot done.\n");
}

/**
 * smp_store_cpu_info - Save a CPU's information
 * @cpu: The CPU to save for.
 *
 * Save boot_cpu_data and jiffy for the specified CPU.
 */
static void __init smp_store_cpu_info(int cpu)
{
	struct mn10300_cpuinfo *ci = &cpu_data[cpu];

	*ci = boot_cpu_data;
	ci->loops_per_jiffy = loops_per_jiffy;
	ci->type = CPUREV;
}

/**
 * smp_tune_scheduling - Set time slice value
 *
 * Nothing to do here.
 */
static void __init smp_tune_scheduling(void)
{
}

/**
 * do_boot_cpu: Boot up one CPU
 * @phy_id: Physical ID of CPU to boot.
 *
 * Send an IPI to a secondary CPU to boot it.  Returns 0 on success, 1
 * otherwise.
 */
static int __init do_boot_cpu(int phy_id)
{
	struct task_struct *idle;
	unsigned long send_status, callin_status;
	int timeout, cpu_id;

	send_status = GxICR_REQUEST;
	callin_status = 0;
	timeout = 0;
	cpu_id = phy_id;

	cpucount++;

	/* Create idle thread for this CPU */
	idle = fork_idle(cpu_id);
	if (IS_ERR(idle))
		panic("Failed fork for CPU#%d.", cpu_id);

	idle->thread.pc = (unsigned long)start_secondary;

	printk(KERN_NOTICE "Booting CPU#%d\n", cpu_id);
	start_stack[cpu_id - 1] = idle->thread.sp;

	task_thread_info(idle)->cpu = cpu_id;

	/* Send boot IPI to AP */
	send_IPI_mask(cpumask_of(phy_id), SMP_BOOT_IRQ);

	Dprintk("Waiting for send to finish...\n");

	/* Wait for AP's IPI receive in 100[ms] */
	do {
		udelay(1000);
		send_status =
			CROSS_GxICR(SMP_BOOT_IRQ, phy_id) & GxICR_REQUEST;
	} while (send_status == GxICR_REQUEST && timeout++ < 100);

	Dprintk("Waiting for cpu_callin_map.\n");

	if (send_status == 0) {
		/* Allow AP to start initializing */
		cpumask_set_cpu(cpu_id, &cpu_callout_map);

		/* Wait for setting cpu_callin_map */
		timeout = 0;
		do {
			udelay(1000);
			callin_status = cpumask_test_cpu(cpu_id,
							 &cpu_callin_map);
		} while (callin_status == 0 && timeout++ < 5000);

		if (callin_status == 0)
			Dprintk("Not responding.\n");
	} else {
		printk(KERN_WARNING "IPI not delivered.\n");
	}

	if (send_status == GxICR_REQUEST || callin_status == 0) {
		cpumask_clear_cpu(cpu_id, &cpu_callout_map);
		cpumask_clear_cpu(cpu_id, &cpu_callin_map);
		cpumask_clear_cpu(cpu_id, &cpu_initialized);
		cpucount--;
		return 1;
	}
	return 0;
}

/**
 * smp_show_cpu_info - Show SMP CPU information
 * @cpu: The CPU of interest.
 */
static void __init smp_show_cpu_info(int cpu)
{
	struct mn10300_cpuinfo *ci = &cpu_data[cpu];

	printk(KERN_INFO
	       "CPU#%d : ioclk speed: %lu.%02luMHz : bogomips : %lu.%02lu\n",
	       cpu,
	       MN10300_IOCLK / 1000000,
	       (MN10300_IOCLK / 10000) % 100,
	       ci->loops_per_jiffy / (500000 / HZ),
	       (ci->loops_per_jiffy / (5000 / HZ)) % 100);
}

/**
 * smp_callin - Set cpu_callin_map of the current CPU ID
 */
static void __init smp_callin(void)
{
	unsigned long timeout;
	int cpu;

	cpu = smp_processor_id();
	timeout = jiffies + (2 * HZ);

	if (cpumask_test_cpu(cpu, &cpu_callin_map)) {
		printk(KERN_ERR "CPU#%d already present.\n", cpu);
		BUG();
	}
	Dprintk("CPU#%d waiting for CALLOUT\n", cpu);

	/* Wait for AP startup 2s total */
	while (time_before(jiffies, timeout)) {
		if (cpumask_test_cpu(cpu, &cpu_callout_map))
			break;
		cpu_relax();
	}

	if (!time_before(jiffies, timeout)) {
		printk(KERN_ERR
		       "BUG: CPU#%d started up but did not get a callout!\n",
		       cpu);
		BUG();
	}

#ifdef CONFIG_CALIBRATE_DELAY
	calibrate_delay();		/* Get our bogomips */
#endif

	/* Save our processor parameters */
	smp_store_cpu_info(cpu);

	/* Allow the boot processor to continue */
	cpumask_set_cpu(cpu, &cpu_callin_map);
}

/**
 * smp_online - Set cpu_online_mask
 */
static void __init smp_online(void)
{
	int cpu;

	cpu = smp_processor_id();

	local_irq_enable();

	set_cpu_online(cpu, true);
	smp_wmb();
}

/**
 * smp_cpus_done -
 * @max_cpus: Maximum CPU count.
 *
 * Do nothing.
 */
void __init smp_cpus_done(unsigned int max_cpus)
{
}

/*
 * smp_prepare_boot_cpu - Set up stuff for the boot processor.
 *
 * Set up the cpu_online_mask, cpu_callout_map and cpu_callin_map of the boot
 * processor (CPU 0).
 */
void __devinit smp_prepare_boot_cpu(void)
{
	cpumask_set_cpu(0, &cpu_callout_map);
	cpumask_set_cpu(0, &cpu_callin_map);
	current_thread_info()->cpu = 0;
}

/*
 * initialize_secondary - Initialise a secondary CPU (Application Processor).
 *
 * Set SP register and jump to thread's PC address.
 */
void initialize_secondary(void)
{
	asm volatile (
		"mov	%0,sp	\n"
		"jmp	(%1)	\n"
		:
		: "a"(current->thread.sp), "a"(current->thread.pc));
}

/**
 * __cpu_up - Set smp_commenced_mask for the nominated CPU
 * @cpu: The target CPU.
 */
int __devinit __cpu_up(unsigned int cpu)
{
	int timeout;

#ifdef CONFIG_HOTPLUG_CPU
	if (num_online_cpus() == 1)
		disable_hlt();
	if (sleep_mode[cpu])
		run_wakeup_cpu(cpu);
#endif /* CONFIG_HOTPLUG_CPU */

	cpumask_set_cpu(cpu, &smp_commenced_mask);

	/* Wait 5s total for a response */
	for (timeout = 0 ; timeout < 5000 ; timeout++) {
		if (cpu_online(cpu))
			break;
		udelay(1000);
	}

	BUG_ON(!cpu_online(cpu));
	return 0;
}

/**
 * setup_profiling_timer - Set up the profiling timer
 * @multiplier - The frequency multiplier to use
 *
 * The frequency of the profiling timer can be changed by writing a multiplier
 * value into /proc/profile.
 */
int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

/*
 * CPU hotplug routines
 */
#ifdef CONFIG_HOTPLUG_CPU

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int __init topology_init(void)
{
	int cpu, ret;

	for_each_cpu(cpu) {
		ret = register_cpu(&per_cpu(cpu_devices, cpu), cpu, NULL);
		if (ret)
			printk(KERN_WARNING
			       "topology_init: register_cpu %d failed (%d)\n",
			       cpu, ret);
	}
	return 0;
}

subsys_initcall(topology_init);

int __cpu_disable(void)
{
	int cpu = smp_processor_id();
	if (cpu == 0)
		return -EBUSY;

	migrate_irqs();
	cpumask_clear_cpu(cpu, &mm_cpumask(current->active_mm));
	return 0;
}

void __cpu_die(unsigned int cpu)
{
	run_sleep_cpu(cpu);

	if (num_online_cpus() == 1)
		enable_hlt();
}

#ifdef CONFIG_MN10300_CACHE_ENABLED
static inline void hotplug_cpu_disable_cache(void)
{
	int tmp;
	asm volatile(
		"	movhu	(%1),%0	\n"
		"	and	%2,%0	\n"
		"	movhu	%0,(%1)	\n"
		"1:	movhu	(%1),%0	\n"
		"	btst	%3,%0	\n"
		"	bne	1b	\n"
		: "=&r"(tmp)
		: "a"(&CHCTR),
		  "i"(~(CHCTR_ICEN | CHCTR_DCEN)),
		  "i"(CHCTR_ICBUSY | CHCTR_DCBUSY)
		: "memory", "cc");
}

static inline void hotplug_cpu_enable_cache(void)
{
	int tmp;
	asm volatile(
		"movhu	(%1),%0	\n"
		"or	%2,%0	\n"
		"movhu	%0,(%1)	\n"
		: "=&r"(tmp)
		: "a"(&CHCTR),
		  "i"(CHCTR_ICEN | CHCTR_DCEN)
		: "memory", "cc");
}

static inline void hotplug_cpu_invalidate_cache(void)
{
	int tmp;
	asm volatile (
		"movhu	(%1),%0	\n"
		"or	%2,%0	\n"
		"movhu	%0,(%1)	\n"
		: "=&r"(tmp)
		: "a"(&CHCTR),
		  "i"(CHCTR_ICINV | CHCTR_DCINV)
		: "cc");
}

#else /* CONFIG_MN10300_CACHE_ENABLED */
#define hotplug_cpu_disable_cache()	do {} while (0)
#define hotplug_cpu_enable_cache()	do {} while (0)
#define hotplug_cpu_invalidate_cache()	do {} while (0)
#endif /* CONFIG_MN10300_CACHE_ENABLED */

/**
 * hotplug_cpu_nmi_call_function - Call a function on other CPUs for hotplug
 * @cpumask: List of target CPUs.
 * @func: The function to call on those CPUs.
 * @info: The context data for the function to be called.
 * @wait: Whether to wait for the calls to complete.
 *
 * Non-maskably call a function on another CPU for hotplug purposes.
 *
 * This function must be called with maskable interrupts disabled.
 */
static int hotplug_cpu_nmi_call_function(cpumask_t cpumask,
					 smp_call_func_t func, void *info,
					 int wait)
{
	/*
	 * The address and the size of nmi_call_func_mask_data
	 * need to be aligned on L1_CACHE_BYTES.
	 */
	static struct nmi_call_data_struct nmi_call_func_mask_data
		__cacheline_aligned;
	unsigned long start, end;

	start = (unsigned long)&nmi_call_func_mask_data;
	end = start + sizeof(struct nmi_call_data_struct);

	nmi_call_func_mask_data.func = func;
	nmi_call_func_mask_data.info = info;
	nmi_call_func_mask_data.started = cpumask;
	nmi_call_func_mask_data.wait = wait;
	if (wait)
		nmi_call_func_mask_data.finished = cpumask;

	spin_lock(&smp_nmi_call_lock);
	nmi_call_data = &nmi_call_func_mask_data;
	mn10300_local_dcache_flush_range(start, end);
	smp_wmb();

	send_IPI_mask(cpumask, CALL_FUNCTION_NMI_IPI);

	do {
		mn10300_local_dcache_inv_range(start, end);
		barrier();
	} while (!cpumask_empty(&nmi_call_func_mask_data.started));

	if (wait) {
		do {
			mn10300_local_dcache_inv_range(start, end);
			barrier();
		} while (!cpumask_empty(&nmi_call_func_mask_data.finished));
	}

	spin_unlock(&smp_nmi_call_lock);
	return 0;
}

static void restart_wakeup_cpu(void)
{
	unsigned int cpu = smp_processor_id();

	cpumask_set_cpu(cpu, &cpu_callin_map);
	local_flush_tlb();
	set_cpu_online(cpu, true);
	smp_wmb();
}

static void prepare_sleep_cpu(void *unused)
{
	sleep_mode[smp_processor_id()] = 1;
	smp_mb();
	mn10300_local_dcache_flush_inv();
	hotplug_cpu_disable_cache();
	hotplug_cpu_invalidate_cache();
}

/* when this function called, IE=0, NMID=0. */
static void sleep_cpu(void *unused)
{
	unsigned int cpu_id = smp_processor_id();
	/*
	 * CALL_FUNCTION_NMI_IPI for wakeup_cpu() shall not be requested,
	 * before this cpu goes in SLEEP mode.
	 */
	do {
		smp_mb();
		__sleep_cpu();
	} while (sleep_mode[cpu_id]);
	restart_wakeup_cpu();
}

static void run_sleep_cpu(unsigned int cpu)
{
	unsigned long flags;
	cpumask_t cpumask;

	cpumask_copy(&cpumask, &cpumask_of(cpu));
	flags = arch_local_cli_save();
	hotplug_cpu_nmi_call_function(cpumask, prepare_sleep_cpu, NULL, 1);
	hotplug_cpu_nmi_call_function(cpumask, sleep_cpu, NULL, 0);
	udelay(1);		/* delay for the cpu to sleep. */
	arch_local_irq_restore(flags);
}

static void wakeup_cpu(void)
{
	hotplug_cpu_invalidate_cache();
	hotplug_cpu_enable_cache();
	smp_mb();
	sleep_mode[smp_processor_id()] = 0;
}

static void run_wakeup_cpu(unsigned int cpu)
{
	unsigned long flags;

	flags = arch_local_cli_save();
#if NR_CPUS == 2
	mn10300_local_dcache_flush_inv();
#else
	/*
	 * Before waking up the cpu,
	 * all online cpus should stop and flush D-Cache for global data.
	 */
#error not support NR_CPUS > 2, when CONFIG_HOTPLUG_CPU=y.
#endif
	hotplug_cpu_nmi_call_function(cpumask_of(cpu), wakeup_cpu, NULL, 1);
	arch_local_irq_restore(flags);
}

#endif /* CONFIG_HOTPLUG_CPU */
