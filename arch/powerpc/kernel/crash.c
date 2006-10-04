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
#include <linux/irq.h>

#include <asm/processor.h>
#include <asm/machdep.h>
#include <asm/kexec.h>
#include <asm/kdump.h>
#include <asm/lmb.h>
#include <asm/firmware.h>
#include <asm/smp.h>

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

static u32 *append_elf_note(u32 *buf, char *name, unsigned type, void *data,
							       size_t data_len)
{
	struct elf_note note;

	note.n_namesz = strlen(name) + 1;
	note.n_descsz = data_len;
	note.n_type   = type;
	memcpy(buf, &note, sizeof(note));
	buf += (sizeof(note) +3)/4;
	memcpy(buf, name, note.n_namesz);
	buf += (note.n_namesz + 3)/4;
	memcpy(buf, data, note.n_descsz);
	buf += (note.n_descsz + 3)/4;

	return buf;
}

static void final_note(u32 *buf)
{
	struct elf_note note;

	note.n_namesz = 0;
	note.n_descsz = 0;
	note.n_type   = 0;
	memcpy(buf, &note, sizeof(note));
}

static void crash_save_this_cpu(struct pt_regs *regs, int cpu)
{
	struct elf_prstatus prstatus;
	u32 *buf;

	if ((cpu < 0) || (cpu >= NR_CPUS))
		return;

	/* Using ELF notes here is opportunistic.
	 * I need a well defined structure format
	 * for the data I pass, and I need tags
	 * on the data to indicate what information I have
	 * squirrelled away.  ELF notes happen to provide
	 * all of that that no need to invent something new.
	 */
	buf = (u32*)per_cpu_ptr(crash_notes, cpu);
	if (!buf) 
		return;

	memset(&prstatus, 0, sizeof(prstatus));
	prstatus.pr_pid = current->pid;
	elf_core_copy_regs(&prstatus.pr_reg, regs);
	buf = append_elf_note(buf, "CORE", NT_PRSTATUS, &prstatus,
			sizeof(prstatus));
	final_note(buf);
}

#ifdef CONFIG_SMP
static atomic_t enter_on_soft_reset = ATOMIC_INIT(0);

void crash_ipi_callback(struct pt_regs *regs)
{
	int cpu = smp_processor_id();

	if (!cpu_online(cpu))
		return;

	hard_irq_disable();
	if (!cpu_isset(cpu, cpus_in_crash))
		crash_save_this_cpu(regs, cpu);
	cpu_set(cpu, cpus_in_crash);

	/*
	 * Entered via soft-reset - could be the kdump
	 * process is invoked using soft-reset or user activated
	 * it if some CPU did not respond to an IPI.
	 * For soft-reset, the secondary CPU can enter this func
	 * twice. 1 - using IPI, and 2. soft-reset.
	 * Tell the kexec CPU that entered via soft-reset and ready
	 * to go down.
	 */
	if (cpu_isset(cpu, cpus_in_sr)) {
		cpu_clear(cpu, cpus_in_sr);
		atomic_inc(&enter_on_soft_reset);
	}

	/*
	 * Starting the kdump boot.
	 * This barrier is needed to make sure that all CPUs are stopped.
	 * If not, soft-reset will be invoked to bring other CPUs.
	 */
	while (!cpu_isset(crashing_cpu, cpus_in_crash))
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

	cpu_clear(cpu, cpus_in_sr);
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
	 * FIXME: Until we will have the way to stop other CPUSs reliabally,
	 * the crash CPU will send an IPI and wait for other CPUs to
	 * respond.
	 * Delay of at least 10 seconds.
	 */
	printk(KERN_EMERG "Sending IPI to other cpus...\n");
	msecs = 10000;
	while ((cpus_weight(cpus_in_crash) < ncpus) && (--msecs > 0)) {
		cpu_relax();
		mdelay(1);
	}

	/* Would it be better to replace the trap vector here? */

	/*
	 * FIXME: In case if we do not get all CPUs, one possibility: ask the
	 * user to do soft reset such that we get all.
	 * Soft-reset will be used until better mechanism is implemented.
	 */
	if (cpus_weight(cpus_in_crash) < ncpus) {
		printk(KERN_EMERG "done waiting: %d cpu(s) not responding\n",
			ncpus - cpus_weight(cpus_in_crash));
		printk(KERN_EMERG "Activate soft-reset to stop other cpu(s)\n");
		cpus_in_sr = CPU_MASK_NONE;
		atomic_set(&enter_on_soft_reset, 0);
		while (cpus_weight(cpus_in_crash) < ncpus)
			cpu_relax();
	}
	/*
	 * Make sure all CPUs are entered via soft-reset if the kdump is
	 * invoked using soft-reset.
	 */
	if (cpu_isset(cpu, cpus_in_sr))
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
			cpu_clear(cpu, cpus_in_sr);
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
		cpu_set(crashing_cpu, cpus_in_crash);
		if (ppc_md.kexec_cpu_down)
			ppc_md.kexec_cpu_down(1, 0);
		machine_kexec(kexec_crash_image);
		/* NOTREACHED */
	}
	crash_ipi_callback(regs);
}

#else
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
	cpus_in_sr = CPU_MASK_NONE;
}
#endif

void default_machine_crash_shutdown(struct pt_regs *regs)
{
	unsigned int irq;

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

	for_each_irq(irq) {
		struct irq_desc *desc = irq_desc + irq;

		if (desc->status & IRQ_INPROGRESS)
			desc->chip->eoi(irq);

		if (!(desc->status & IRQ_DISABLED))
			desc->chip->disable(irq);
	}

	/*
	 * Make a note of crashing cpu. Will be used in machine_kexec
	 * such that another IPI will not be sent.
	 */
	crashing_cpu = smp_processor_id();
	crash_save_this_cpu(regs, crashing_cpu);
	crash_kexec_prepare_cpus(crashing_cpu);
	cpu_set(crashing_cpu, cpus_in_crash);
	if (ppc_md.kexec_cpu_down)
		ppc_md.kexec_cpu_down(1, 0);
}
