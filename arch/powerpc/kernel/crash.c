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

#undef DEBUG

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/bootmem.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/memblock.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/kexec.h>
#include <asm/kdump.h>
#include <asm/prom.h>
#include <asm/firmware.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/setjmp.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

/* This keeps a track of which one is crashing cpu. */
int crashing_cpu = -1;
static cpumask_t cpus_in_crash = CPU_MASK_NONE;
cpumask_t cpus_in_sr = CPU_MASK_NONE;

#define CRASH_HANDLER_MAX 3
/* NULL terminated list of shutdown handles */
static crash_shutdown_t crash_shutdown_handles[CRASH_HANDLER_MAX+1];
static DEFINE_SPINLOCK(crash_handlers_lock);

#ifdef CONFIG_SMP
static atomic_t enter_on_soft_reset = ATOMIC_INIT(0);

void crash_ipi_callback(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if (!cpu_online(cpu))
		return;

	hard_irq_disable();
	if (!cpumask_test_cpu(cpu, &cpus_in_crash))
		crash_save_cpu(regs, cpu);
	cpumask_set_cpu(cpu, &cpus_in_crash);

	/*
	 * Entered via soft-reset - could be the kdump
	 * process is invoked using soft-reset or user activated
	 * it if some CPU did not respond to an IPI.
	 * For soft-reset, the secondary CPU can enter this func
	 * twice. 1 - using IPI, and 2. soft-reset.
	 * Tell the kexec CPU that entered via soft-reset and ready
	 * to go down.
	 */
	if (cpumask_test_cpu(cpu, &cpus_in_sr)) {
		cpumask_clear_cpu(cpu, &cpus_in_sr);
		atomic_inc(&enter_on_soft_reset);
	}

	/*
	 * Starting the kdump boot.
	 * This barrier is needed to make sure that all CPUs are stopped.
	 * If not, soft-reset will be invoked to bring other CPUs.
	 */
	while (!cpumask_test_cpu(crashing_cpu, &cpus_in_crash))
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

/*
 * Wait until all CPUs are entered via soft-reset.
 */
static void crash_soft_reset_check(int cpu)
{
	unsigned int ncpus = num_online_cpus() - 1;/* Excluding the panic cpu */

	cpumask_clear_cpu(cpu, &cpus_in_sr);
	while (atomic_read(&enter_on_soft_reset) != ncpus)
		cpu_relax();
}


static void crash_kexec_prepare_cpus(int cpu)
{
	unsigned int msecs;

	unsigned int ncpus = num_online_cpus() - 1;/* Excluding the panic cpu */

	crash_send_ipi(crash_ipi_callback);
	smp_wmb();

	/*
	 * FIXME: Until we will have the way to stop other CPUs reliably,
	 * the crash CPU will send an IPI and wait for other CPUs to
	 * respond.
	 * Delay of at least 10 seconds.
	 */
	printk(KERN_EMERG "Sending IPI to other cpus...\n");
	msecs = 10000;
	while ((cpumask_weight(&cpus_in_crash) < ncpus) && (--msecs > 0)) {
		cpu_relax();
		mdelay(1);
	}

	/* Would it be better to replace the trap vector here? */

	/*
	 * FIXME: In case if we do not get all CPUs, one possibility: ask the
	 * user to do soft reset such that we get all.
	 * Soft-reset will be used until better mechanism is implemented.
	 */
	if (cpumask_weight(&cpus_in_crash) < ncpus) {
		printk(KERN_EMERG "done waiting: %d cpu(s) not responding\n",
			ncpus - cpumask_weight(&cpus_in_crash));
		printk(KERN_EMERG "Activate soft-reset to stop other cpu(s)\n");
		cpumask_clear(&cpus_in_sr);
		atomic_set(&enter_on_soft_reset, 0);
		while (cpumask_weight(&cpus_in_crash) < ncpus)
			cpu_relax();
	}
	/*
	 * Make sure all CPUs are entered via soft-reset if the kdump is
	 * invoked using soft-reset.
	 */
	if (cpumask_test_cpu(cpu, &cpus_in_sr))
		crash_soft_reset_check(cpu);
	/* Leave the IPI callback set */
}

/*
 * This function will be called by secondary cpus or by kexec cpu
 * if soft-reset is activated to stop some CPUs.
 */
void crash_kexec_secondary(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long flags;
	int msecs = 5;

	local_irq_save(flags);
	/* Wait 5ms if the kexec CPU is not entered yet. */
	while (crashing_cpu < 0) {
		if (--msecs < 0) {
			/*
			 * Either kdump image is not loaded or
			 * kdump process is not started - Probably xmon
			 * exited using 'x'(exit and recover) or
			 * kexec_should_crash() failed for all running tasks.
			 */
			cpumask_clear_cpu(cpu, &cpus_in_sr);
			local_irq_restore(flags);
			return;
		}
		mdelay(1);
		cpu_relax();
	}
	if (cpu == crashing_cpu) {
		/*
		 * Panic CPU will enter this func only via soft-reset.
		 * Wait until all secondary CPUs entered and
		 * then start kexec boot.
		 */
		crash_soft_reset_check(cpu);
		cpumask_set_cpu(crashing_cpu, &cpus_in_crash);
		if (ppc_md.kexec_cpu_down)
			ppc_md.kexec_cpu_down(1, 0);
		machine_kexec(kexec_crash_image);
		/* NOTREACHED */
	}
	crash_ipi_callback(regs);
}

#else	/* ! CONFIG_SMP */

static void crash_kexec_prepare_cpus(int cpu)
{
	/*
	 * move the secondarys to us so that we can copy
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
	cpumask_clear(&cpus_in_sr);
}
#endif	/* CONFIG_SMP */

/* wait for all the CPUs to hit real mode but timeout if they don't come in */
#if defined(CONFIG_SMP) && defined(CONFIG_PPC_STD_MMU_64)
static void crash_kexec_wait_realmode(int cpu)
{
	unsigned int msecs;
	int i;

	msecs = 10000;
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

static unsigned long crash_shutdown_buf[JMP_BUF_LEN];
static int crash_shutdown_cpu = -1;

static int handle_fault(struct pt_regs *regs)
{
	if (crash_shutdown_cpu == smp_processor_id())
		longjmp(crash_shutdown_buf, 1);
	return 0;
}

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
	crash_save_cpu(regs, crashing_cpu);
	crash_kexec_prepare_cpus(crashing_cpu);
	cpumask_set_cpu(crashing_cpu, &cpus_in_crash);
	crash_kexec_wait_realmode(crashing_cpu);

	machine_kexec_mask_interrupts();

	/*
	 * Call registered shutdown routines savely.  Swap out
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
