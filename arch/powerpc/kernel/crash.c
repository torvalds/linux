/*
 * Architecture specific (PPC64) functions for kexec based crash dumps.
 *
 * Copyright (C) 2005, IBM Corp.
 *
 * Created by: Haren Myneni
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/export.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/kexec.h>
#include <asm/kdump.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/setjmp.h>
#include <asm/debug.h>

/*
 * The primary CPU waits a while for all secondary CPUs to enter. This is to
 * avoid sending an IPI if the secondary CPUs are entering
 * crash_kexec_secondary on their own (eg via a system reset).
 *
 * The secondary timeout has to be longer than the primary. Both timeouts are
 * in milliseconds.
 */
#define PRIMARY_TIMEOUT		500
#define SECONDARY_TIMEOUT	1000

#define IPI_TIMEOUT		10000
#define REAL_MODE_TIMEOUT	10000

/* This keeps a track of which one is the crashing cpu. */
int crashing_cpu = -1;
static int time_to_dump;

#define CRASH_HANDLER_MAX 3
/* NULL terminated list of shutdown handles */
static crash_shutdown_t crash_shutdown_handles[CRASH_HANDLER_MAX+1];
static DEFINE_SPINLOCK(crash_handlers_lock);

static unsigned long crash_shutdown_buf[JMP_BUF_LEN];
static int crash_shutdown_cpu = -1;

static int handle_fault(struct pt_regs *regs)
{
	if (crash_shutdown_cpu == smp_processor_id())
		longjmp(crash_shutdown_buf, 1);
	return 0;
}

#ifdef CONFIG_SMP

static atomic_t cpus_in_crash;
void crash_ipi_callback(struct pt_regs *regs)
{
	static cpumask_t cpus_state_saved = CPU_MASK_NONE;

	int cpu = smp_processor_id();

	if (!cpu_online(cpu))
		return;

	hard_irq_disable();
	if (!cpumask_test_cpu(cpu, &cpus_state_saved)) {
		crash_save_cpu(regs, cpu);
		cpumask_set_cpu(cpu, &cpus_state_saved);
	}

	atomic_inc(&cpus_in_crash);
	smp_mb__after_atomic_inc();

	/*
	 * Starting the kdump boot.
	 * This barrier is needed to make sure that all CPUs are stopped.
	 */
	while (!time_to_dump)
		cpu_relax();

	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 1);

#ifdef CONFIG_PPC64
	kexec_smp_wait();
#else
	for (;;);	/* FIXME */
#endif

	/* NOTREACHED */
}

static void crash_kexec_prepare_cpus(int cpu)
{
	unsigned int msecs;
	unsigned int ncpus = num_online_cpus() - 1;/* Excluding the panic cpu */
	int tries = 0;
	int (*old_handler)(struct pt_regs *regs);

	printk(KERN_EMERG "Sending IPI to other CPUs\n");

	crash_send_ipi(crash_ipi_callback);
	smp_wmb();

again:
	/*
	 * FIXME: Until we will have the way to stop other CPUs reliably,
	 * the crash CPU will send an IPI and wait for other CPUs to
	 * respond.
	 */
	msecs = IPI_TIMEOUT;
	while ((atomic_read(&cpus_in_crash) < ncpus) && (--msecs > 0))
		mdelay(1);

	/* Would it be better to replace the trap vector here? */

	if (atomic_read(&cpus_in_crash) >= ncpus) {
		printk(KERN_EMERG "IPI complete\n");
		return;
	}

	printk(KERN_EMERG "ERROR: %d cpu(s) not responding\n",
		ncpus - atomic_read(&cpus_in_crash));

	/*
	 * If we have a panic timeout set then we can't wait indefinitely
	 * for someone to activate system reset. We also give up on the
	 * second time through if system reset fail to work.
	 */
	if ((panic_timeout > 0) || (tries > 0))
		return;

	/*
	 * A system reset will cause all CPUs to take an 0x100 exception.
	 * The primary CPU returns here via setjmp, and the secondary
	 * CPUs reexecute the crash_kexec_secondary path.
	 */
	old_handler = __debugger;
	__debugger = handle_fault;
	crash_shutdown_cpu = smp_processor_id();

	if (setjmp(crash_shutdown_buf) == 0) {
		printk(KERN_EMERG "Activate system reset (dumprestart) "
				  "to stop other cpu(s)\n");

		/*
		 * A system reset will force all CPUs to execute the
		 * crash code again. We need to reset cpus_in_crash so we
		 * wait for everyone to do this.
		 */
		atomic_set(&cpus_in_crash, 0);
		smp_mb();

		while (atomic_read(&cpus_in_crash) < ncpus)
			cpu_relax();
	}

	crash_shutdown_cpu = -1;
	__debugger = old_handler;

	tries++;
	goto again;
}

/*
 * This function will be called by secondary cpus.
 */
void crash_kexec_secondary(struct pt_regs *regs)
{
	unsigned long flags;
	int msecs = SECONDARY_TIMEOUT;

	local_irq_save(flags);

	/* Wait for the primary crash CPU to signal its progress */
	while (crashing_cpu < 0) {
		if (--msecs < 0) {
			/* No response, kdump image may not have been loaded */
			local_irq_restore(flags);
			return;
		}

		mdelay(1);
	}

	crash_ipi_callback(regs);
}

#else	/* ! CONFIG_SMP */

static void crash_kexec_prepare_cpus(int cpu)
{
	/*
	 * move the secondaries to us so that we can copy
	 * the new kernel 0-0x100 safely
	 *
	 * do this if kexec in setup.c ?
	 */
#ifdef CONFIG_PPC64
	smp_release_cpus();
#else
	/* FIXME */
#endif
}

void crash_kexec_secondary(struct pt_regs *regs)
{
}
#endif	/* CONFIG_SMP */

/* wait for all the CPUs to hit real mode but timeout if they don't come in */
#if defined(CONFIG_SMP) && defined(CONFIG_PPC_STD_MMU_64)
static void crash_kexec_wait_realmode(int cpu)
{
	unsigned int msecs;
	int i;

	msecs = REAL_MODE_TIMEOUT;
	for (i=0; i < nr_cpu_ids && msecs > 0; i++) {
		if (i == cpu)
			continue;

		while (paca[i].kexec_state < KEXEC_STATE_REAL_MODE) {
			barrier();
			if (!cpu_possible(i) || !cpu_online(i) || (msecs <= 0))
				break;
			msecs--;
			mdelay(1);
		}
	}
	mb();
}
#else
static inline void crash_kexec_wait_realmode(int cpu) {}
#endif	/* CONFIG_SMP && CONFIG_PPC_STD_MMU_64 */

/*
 * Register a function to be called on shutdown.  Only use this if you
 * can't reset your device in the second kernel.
 */
int crash_shutdown_register(crash_shutdown_t handler)
{
	unsigned int i, rc;

	spin_lock(&crash_handlers_lock);
	for (i = 0 ; i < CRASH_HANDLER_MAX; i++)
		if (!crash_shutdown_handles[i]) {
			/* Insert handle at first empty entry */
			crash_shutdown_handles[i] = handler;
			rc = 0;
			break;
		}

	if (i == CRASH_HANDLER_MAX) {
		printk(KERN_ERR "Crash shutdown handles full, "
		       "not registered.\n");
		rc = 1;
	}

	spin_unlock(&crash_handlers_lock);
	return rc;
}
EXPORT_SYMBOL(crash_shutdown_register);

int crash_shutdown_unregister(crash_shutdown_t handler)
{
	unsigned int i, rc;

	spin_lock(&crash_handlers_lock);
	for (i = 0 ; i < CRASH_HANDLER_MAX; i++)
		if (crash_shutdown_handles[i] == handler)
			break;

	if (i == CRASH_HANDLER_MAX) {
		printk(KERN_ERR "Crash shutdown handle not found\n");
		rc = 1;
	} else {
		/* Shift handles down */
		for (; crash_shutdown_handles[i]; i++)
			crash_shutdown_handles[i] =
				crash_shutdown_handles[i+1];
		rc = 0;
	}

	spin_unlock(&crash_handlers_lock);
	return rc;
}
EXPORT_SYMBOL(crash_shutdown_unregister);

void default_machine_crash_shutdown(struct pt_regs *regs)
{
	unsigned int i;
	int (*old_handler)(struct pt_regs *regs);

	/*
	 * This function is only called after the system
	 * has panicked or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means stopping other cpus in
	 * an SMP system.
	 * The kernel is broken so disable interrupts.
	 */
	hard_irq_disable();

	/*
	 * Make a note of crashing cpu. Will be used in machine_kexec
	 * such that another IPI will not be sent.
	 */
	crashing_cpu = smp_processor_id();

	/*
	 * If we came in via system reset, wait a while for the secondary
	 * CPUs to enter.
	 */
	if (TRAP(regs) == 0x100)
		mdelay(PRIMARY_TIMEOUT);

	crash_kexec_prepare_cpus(crashing_cpu);

	crash_save_cpu(regs, crashing_cpu);

	time_to_dump = 1;

	crash_kexec_wait_realmode(crashing_cpu);

	machine_kexec_mask_interrupts();

	/*
	 * Call registered shutdown routines safely.  Swap out
	 * __debugger_fault_handler, and replace on exit.
	 */
	old_handler = __debugger_fault_handler;
	__debugger_fault_handler = handle_fault;
	crash_shutdown_cpu = smp_processor_id();
	for (i = 0; crash_shutdown_handles[i]; i++) {
		if (setjmp(crash_shutdown_buf) == 0) {
			/*
			 * Insert syncs and delay to ensure
			 * instructions in the dangerous region don't
			 * leak away from this protected region.
			 */
			asm volatile("sync; isync");
			/* dangerous region */
			crash_shutdown_handles[i]();
			asm volatile("sync; isync");
		}
	}
	crash_shutdown_cpu = -1;
	__debugger_fault_handler = old_handler;

	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 0);
}
