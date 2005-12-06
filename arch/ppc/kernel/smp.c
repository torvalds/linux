/*
 * Smp support for ppc.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) borrowing a great
 * deal of code from the sparc and intel versions.
 *
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cache.h>

#include <asm/ptrace.h>
#include <asm/atomic.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/residual.h>
#include <asm/time.h>
#include <asm/thread_info.h>
#include <asm/tlbflush.h>
#include <asm/xmon.h>
#include <asm/machdep.h>

volatile int smp_commenced;
int smp_tb_synchronized;
struct cpuinfo_PPC cpu_data[NR_CPUS];
atomic_t ipi_recv;
atomic_t ipi_sent;
cpumask_t cpu_online_map;
cpumask_t cpu_possible_map;
int smp_hw_index[NR_CPUS];
struct thread_info *secondary_ti;
static struct task_struct *idle_tasks[NR_CPUS];

EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_possible_map);

/* SMP operations for this machine */
struct smp_ops_t *smp_ops;

/* all cpu mappings are 1-1 -- Cort */
volatile unsigned long cpu_callin_map[NR_CPUS];

int start_secondary(void *);
void smp_call_function_interrupt(void);
static int __smp_call_function(void (*func) (void *info), void *info,
			       int wait, int target);

/* Low level assembly function used to backup CPU 0 state */
extern void __save_cpu_setup(void);

/* Since OpenPIC has only 4 IPIs, we use slightly different message numbers.
 *
 * Make sure this matches openpic_request_IPIs in open_pic.c, or what shows up
 * in /proc/interrupts will be wrong!!! --Troy */
#define PPC_MSG_CALL_FUNCTION	0
#define PPC_MSG_RESCHEDULE	1
#define PPC_MSG_INVALIDATE_TLB	2
#define PPC_MSG_XMON_BREAK	3

static inline void
smp_message_pass(int target, int msg)
{
	if (smp_ops) {
		atomic_inc(&ipi_sent);
		smp_ops->message_pass(target, msg);
	}
}

/*
 * Common functions
 */
void smp_message_recv(int msg, struct pt_regs *regs)
{
	atomic_inc(&ipi_recv);

	switch( msg ) {
	case PPC_MSG_CALL_FUNCTION:
		smp_call_function_interrupt();
		break;
	case PPC_MSG_RESCHEDULE:
		set_need_resched();
		break;
	case PPC_MSG_INVALIDATE_TLB:
		_tlbia();
		break;
#ifdef CONFIG_XMON
	case PPC_MSG_XMON_BREAK:
		xmon(regs);
		break;
#endif /* CONFIG_XMON */
	default:
		printk("SMP %d: smp_message_recv(): unknown msg %d\n",
		       smp_processor_id(), msg);
		break;
	}
}

/*
 * 750's don't broadcast tlb invalidates so
 * we have to emulate that behavior.
 *   -- Cort
 */
void smp_send_tlb_invalidate(int cpu)
{
	if ( PVR_VER(mfspr(SPRN_PVR)) == 8 )
		smp_message_pass(MSG_ALL_BUT_SELF, PPC_MSG_INVALIDATE_TLB);
}

void smp_send_reschedule(int cpu)
{
	/*
	 * This is only used if `cpu' is running an idle task,
	 * so it will reschedule itself anyway...
	 *
	 * This isn't the case anymore since the other CPU could be
	 * sleeping and won't reschedule until the next interrupt (such
	 * as the timer).
	 *  -- Cort
	 */
	/* This is only used if `cpu' is running an idle task,
	   so it will reschedule itself anyway... */
	smp_message_pass(cpu, PPC_MSG_RESCHEDULE);
}

#ifdef CONFIG_XMON
void smp_send_xmon_break(int cpu)
{
	smp_message_pass(cpu, PPC_MSG_XMON_BREAK);
}
#endif /* CONFIG_XMON */

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
static DEFINE_SPINLOCK(call_lock);

static struct call_data_struct {
	void (*func) (void *info);
	void *info;
	atomic_t started;
	atomic_t finished;
	int wait;
} *call_data;

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

int smp_call_function(void (*func) (void *info), void *info, int nonatomic,
		      int wait)
/*
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
{
	/* FIXME: get cpu lock with hotplug cpus, or change this to
           bitmask. --RR */
	if (num_online_cpus() <= 1)
		return 0;
	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());
	return __smp_call_function(func, info, wait, MSG_ALL_BUT_SELF);
}

static int __smp_call_function(void (*func) (void *info), void *info,
			       int wait, int target)
{
	struct call_data_struct data;
	int ret = -1;
	int timeout;
	int ncpus = 1;

	if (target == MSG_ALL_BUT_SELF)
		ncpus = num_online_cpus() - 1;
	else if (target == MSG_ALL)
		ncpus = num_online_cpus();

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock(&call_lock);
	call_data = &data;
	/* Send a message to all other CPUs and wait for them to respond */
	smp_message_pass(target, PPC_MSG_CALL_FUNCTION);

	/* Wait for response */
	timeout = 1000000;
	while (atomic_read(&data.started) != ncpus) {
		if (--timeout == 0) {
			printk("smp_call_function on cpu %d: other cpus not responding (%d)\n",
			       smp_processor_id(), atomic_read(&data.started));
			goto out;
		}
		barrier();
		udelay(1);
	}

	if (wait) {
		timeout = 1000000;
		while (atomic_read(&data.finished) != ncpus) {
			if (--timeout == 0) {
				printk("smp_call_function on cpu %d: other cpus not finishing (%d/%d)\n",
				       smp_processor_id(), atomic_read(&data.finished), atomic_read(&data.started));
				goto out;
			}
			barrier();
			udelay(1);
		}
	}
	ret = 0;

 out:
	spin_unlock(&call_lock);
	return ret;
}

void smp_call_function_interrupt(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	/*
	 * Notify initiating CPU that I've grabbed the data and am
	 * about to execute the function
	 */
	atomic_inc(&call_data->started);
	/*
	 * At this point the info structure may be out of scope unless wait==1
	 */
	(*func)(info);
	if (wait)
		atomic_inc(&call_data->finished);
}

static void __devinit smp_store_cpu_info(int id)
{
        struct cpuinfo_PPC *c = &cpu_data[id];

	/* assume bogomips are same for everything */
        c->loops_per_jiffy = loops_per_jiffy;
        c->pvr = mfspr(SPRN_PVR);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int num_cpus, i, cpu;
	struct task_struct *p;

	/* Fixup boot cpu */
        smp_store_cpu_info(smp_processor_id());
	cpu_callin_map[smp_processor_id()] = 1;

	if (smp_ops == NULL) {
		printk("SMP not supported on this machine.\n");
		return;
	}

	/* Probe platform for CPUs: always linear. */
	num_cpus = smp_ops->probe();
	for (i = 0; i < num_cpus; ++i)
		cpu_set(i, cpu_possible_map);

	/* Backup CPU 0 state */
	__save_cpu_setup();

	for_each_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		/* create a process for the processor */
		p = fork_idle(cpu);
		if (IS_ERR(p))
			panic("failed fork for CPU %u: %li", cpu, PTR_ERR(p));
		p->thread_info->cpu = cpu;
		idle_tasks[cpu] = p;
	}
}

void __devinit smp_prepare_boot_cpu(void)
{
	cpu_set(smp_processor_id(), cpu_online_map);
	cpu_set(smp_processor_id(), cpu_possible_map);
}

int __init setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

/* Processor coming up starts here */
int __devinit start_secondary(void *unused)
{
	int cpu;

	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	cpu = smp_processor_id();
        smp_store_cpu_info(cpu);
	set_dec(tb_ticks_per_jiffy);
	preempt_disable();
	cpu_callin_map[cpu] = 1;

	printk("CPU %d done callin...\n", cpu);
	smp_ops->setup_cpu(cpu);
	printk("CPU %d done setup...\n", cpu);
	smp_ops->take_timebase();
	printk("CPU %d done timebase take...\n", cpu);

	spin_lock(&call_lock);
	cpu_set(cpu, cpu_online_map);
	spin_unlock(&call_lock);

	local_irq_enable();

	cpu_idle();
	return 0;
}

int __cpu_up(unsigned int cpu)
{
	char buf[32];
	int c;

	secondary_ti = idle_tasks[cpu]->thread_info;
	mb();

	/*
	 * There was a cache flush loop here to flush the cache
	 * to memory for the first 8MB of RAM.  The cache flush
	 * has been pushed into the kick_cpu function for those
	 * platforms that need it.
	 */

	/* wake up cpu */
	smp_ops->kick_cpu(cpu);
	
	/*
	 * wait to see if the cpu made a callin (is actually up).
	 * use this value that I found through experimentation.
	 * -- Cort
	 */
	for (c = 1000; c && !cpu_callin_map[cpu]; c--)
		udelay(100);

	if (!cpu_callin_map[cpu]) {
		sprintf(buf, "didn't find cpu %u", cpu);
		if (ppc_md.progress) ppc_md.progress(buf, 0x360+cpu);
		printk("Processor %u is stuck.\n", cpu);
		return -ENOENT;
	}

	sprintf(buf, "found cpu %u", cpu);
	if (ppc_md.progress) ppc_md.progress(buf, 0x350+cpu);
	printk("Processor %d found.\n", cpu);

	smp_ops->give_timebase();

	/* Wait until cpu puts itself in the online map */
	while (!cpu_online(cpu))
		cpu_relax();

	return 0;
}

void smp_cpus_done(unsigned int max_cpus)
{
	smp_ops->setup_cpu(0);
}
