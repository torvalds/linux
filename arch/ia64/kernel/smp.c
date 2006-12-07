/*
 * SMP Support
 *
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999, 2001, 2003 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Lots of stuff stolen from arch/alpha/kernel/smp.c
 *
 * 01/05/16 Rohit Seth <rohit.seth@intel.com>  IA64-SMP functions. Reorganized
 * the existing code (on the lines of x86 port).
 * 00/09/11 David Mosberger <davidm@hpl.hp.com> Do loops_per_jiffy
 * calibration on each CPU.
 * 00/08/23 Asit Mallick <asit.k.mallick@intel.com> fixed logical processor id
 * 00/03/31 Rohit Seth <rohit.seth@intel.com>	Fixes for Bootstrap Processor
 * & cpu_online_map now gets done here (instead of setup.c)
 * 99/10/05 davidm	Update to bring it in sync with new command-line processing
 *  scheme.
 * 10/13/00 Goutham Rao <goutham.rao@intel.com> Updated smp_call_function and
 *		smp_call_function_single to resend IPI on timeouts
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/cache.h>
#include <linux/delay.h>
#include <linux/efi.h>
#include <linux/bitops.h>
#include <linux/kexec.h>

#include <asm/atomic.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/machvec.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/system.h>
#include <asm/tlbflush.h>
#include <asm/unistd.h>
#include <asm/mca.h>

/*
 * Structure and data for smp_call_function(). This is designed to minimise static memory
 * requirements. It also looks cleaner.
 */
static  __cacheline_aligned DEFINE_SPINLOCK(call_lock);

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	long wait;
	atomic_t started;
	atomic_t finished;
};

static volatile struct call_data_struct *call_data;

#define IPI_CALL_FUNC		0
#define IPI_CPU_STOP		1
#define IPI_KDUMP_CPU_STOP	3

/* This needs to be cacheline aligned because it is written to by *other* CPUs.  */
static DEFINE_PER_CPU(u64, ipi_operation) ____cacheline_aligned;

extern void cpu_halt (void);

void
lock_ipi_calllock(void)
{
	spin_lock_irq(&call_lock);
}

void
unlock_ipi_calllock(void)
{
	spin_unlock_irq(&call_lock);
}

static void
stop_this_cpu (void)
{
	/*
	 * Remove this CPU:
	 */
	cpu_clear(smp_processor_id(), cpu_online_map);
	max_xtp();
	local_irq_disable();
	cpu_halt();
}

void
cpu_die(void)
{
	max_xtp();
	local_irq_disable();
	cpu_halt();
	/* Should never be here */
	BUG();
	for (;;);
}

irqreturn_t
handle_IPI (int irq, void *dev_id)
{
	int this_cpu = get_cpu();
	unsigned long *pending_ipis = &__ia64_per_cpu_var(ipi_operation);
	unsigned long ops;

	mb();	/* Order interrupt and bit testing. */
	while ((ops = xchg(pending_ipis, 0)) != 0) {
		mb();	/* Order bit clearing and data access. */
		do {
			unsigned long which;

			which = ffz(~ops);
			ops &= ~(1 << which);

			switch (which) {
			      case IPI_CALL_FUNC:
			      {
				      struct call_data_struct *data;
				      void (*func)(void *info);
				      void *info;
				      int wait;

				      /* release the 'pointer lock' */
				      data = (struct call_data_struct *) call_data;
				      func = data->func;
				      info = data->info;
				      wait = data->wait;

				      mb();
				      atomic_inc(&data->started);
				      /*
				       * At this point the structure may be gone unless
				       * wait is true.
				       */
				      (*func)(info);

				      /* Notify the sending CPU that the task is done.  */
				      mb();
				      if (wait)
					      atomic_inc(&data->finished);
			      }
			      break;

			      case IPI_CPU_STOP:
				stop_this_cpu();
				break;
#ifdef CONFIG_CRASH_DUMP
			      case IPI_KDUMP_CPU_STOP:
				unw_init_running(kdump_cpu_freeze, NULL);
				break;
#endif
			      default:
				printk(KERN_CRIT "Unknown IPI on CPU %d: %lu\n", this_cpu, which);
				break;
			}
		} while (ops);
		mb();	/* Order data access and bit testing. */
	}
	put_cpu();
	return IRQ_HANDLED;
}

/*
 * Called with preeemption disabled.
 */
static inline void
send_IPI_single (int dest_cpu, int op)
{
	set_bit(op, &per_cpu(ipi_operation, dest_cpu));
	platform_send_ipi(dest_cpu, IA64_IPI_VECTOR, IA64_IPI_DM_INT, 0);
}

/*
 * Called with preeemption disabled.
 */
static inline void
send_IPI_allbutself (int op)
{
	unsigned int i;

	for_each_online_cpu(i) {
		if (i != smp_processor_id())
			send_IPI_single(i, op);
	}
}

/*
 * Called with preeemption disabled.
 */
static inline void
send_IPI_all (int op)
{
	int i;

	for_each_online_cpu(i) {
		send_IPI_single(i, op);
	}
}

/*
 * Called with preeemption disabled.
 */
static inline void
send_IPI_self (int op)
{
	send_IPI_single(smp_processor_id(), op);
}

#ifdef CONFIG_CRASH_DUMP
void
kdump_smp_send_stop()
{
 	send_IPI_allbutself(IPI_KDUMP_CPU_STOP);
}

void
kdump_smp_send_init()
{
	unsigned int cpu, self_cpu;
	self_cpu = smp_processor_id();
	for_each_online_cpu(cpu) {
		if (cpu != self_cpu) {
			if(kdump_status[cpu] == 0)
				platform_send_ipi(cpu, 0, IA64_IPI_DM_INIT, 0);
		}
	}
}
#endif
/*
 * Called with preeemption disabled.
 */
void
smp_send_reschedule (int cpu)
{
	platform_send_ipi(cpu, IA64_IPI_RESCHEDULE, IA64_IPI_DM_INT, 0);
}

void
smp_flush_tlb_all (void)
{
	on_each_cpu((void (*)(void *))local_flush_tlb_all, NULL, 1, 1);
}

void
smp_flush_tlb_mm (struct mm_struct *mm)
{
	preempt_disable();
	/* this happens for the common case of a single-threaded fork():  */
	if (likely(mm == current->active_mm && atomic_read(&mm->mm_users) == 1))
	{
		local_finish_flush_tlb_mm(mm);
		preempt_enable();
		return;
	}

	preempt_enable();
	/*
	 * We could optimize this further by using mm->cpu_vm_mask to track which CPUs
	 * have been running in the address space.  It's not clear that this is worth the
	 * trouble though: to avoid races, we have to raise the IPI on the target CPU
	 * anyhow, and once a CPU is interrupted, the cost of local_flush_tlb_all() is
	 * rather trivial.
	 */
	on_each_cpu((void (*)(void *))local_finish_flush_tlb_mm, mm, 1, 1);
}

/*
 * Run a function on another CPU
 *  <func>	The function to run. This must be fast and non-blocking.
 *  <info>	An arbitrary pointer to pass to the function.
 *  <nonatomic>	Currently unused.
 *  <wait>	If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until the remote CPU is nearly ready to execute <func>
 * or is or has executed.
 */

int
smp_call_function_single (int cpuid, void (*func) (void *info), void *info, int nonatomic,
			  int wait)
{
	struct call_data_struct data;
	int cpus = 1;
	int me = get_cpu(); /* prevent preemption and reschedule on another processor */

	if (cpuid == me) {
		printk(KERN_INFO "%s: trying to call self\n", __FUNCTION__);
		put_cpu();
		return -EBUSY;
	}

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	spin_lock_bh(&call_lock);

	call_data = &data;
	mb();	/* ensure store to call_data precedes setting of IPI_CALL_FUNC */
  	send_IPI_single(cpuid, IPI_CALL_FUNC);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			cpu_relax();
	call_data = NULL;

	spin_unlock_bh(&call_lock);
	put_cpu();
	return 0;
}
EXPORT_SYMBOL(smp_call_function_single);

/*
 * this function sends a 'generic call function' IPI to all other CPUs
 * in the system.
 */

/*
 *  [SUMMARY]	Run a function on all other CPUs.
 *  <func>	The function to run. This must be fast and non-blocking.
 *  <info>	An arbitrary pointer to pass to the function.
 *  <nonatomic>	currently unused.
 *  <wait>	If true, wait (atomically) until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until remote CPUs are nearly ready to execute <func> or are or have
 * executed.
 *
 * You must not call this function with disabled interrupts or from a
 * hardware interrupt handler or from a bottom half handler.
 */
int
smp_call_function (void (*func) (void *info), void *info, int nonatomic, int wait)
{
	struct call_data_struct data;
	int cpus;

	spin_lock(&call_lock);
	cpus = num_online_cpus() - 1;
	if (!cpus) {
		spin_unlock(&call_lock);
		return 0;
	}

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	data.func = func;
	data.info = info;
	atomic_set(&data.started, 0);
	data.wait = wait;
	if (wait)
		atomic_set(&data.finished, 0);

	call_data = &data;
	mb();	/* ensure store to call_data precedes setting of IPI_CALL_FUNC */
	send_IPI_allbutself(IPI_CALL_FUNC);

	/* Wait for response */
	while (atomic_read(&data.started) != cpus)
		cpu_relax();

	if (wait)
		while (atomic_read(&data.finished) != cpus)
			cpu_relax();
	call_data = NULL;

	spin_unlock(&call_lock);
	return 0;
}
EXPORT_SYMBOL(smp_call_function);

/*
 * this function calls the 'stop' function on all other CPUs in the system.
 */
void
smp_send_stop (void)
{
	send_IPI_allbutself(IPI_CPU_STOP);
}

int __init
setup_profiling_timer (unsigned int multiplier)
{
	return -EINVAL;
}
