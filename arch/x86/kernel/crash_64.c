/*
 * Architecture specific (x86_64) functions for kexec based crash dumps.
 *
 * Created by: Hariprasad Nellitheertha (hari@in.ibm.com)
 *
 * Copyright (C) IBM Corporation, 2004. All rights reserved.
 *
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/irq.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/elfcore.h>
#include <linux/kdebug.h>

#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>
#include <asm/hw_irq.h>
#include <asm/mach_apic.h>

/* This keeps a track of which one is crashing cpu. */
static int crashing_cpu;

#ifdef CONFIG_SMP
static atomic_t waiting_for_crash_ipi;

static int crash_nmi_callback(struct notifier_block *self,
				unsigned long val, void *data)
{
	struct pt_regs *regs;
	int cpu;

	if (val != DIE_NMI_IPI)
		return NOTIFY_OK;

	regs = ((struct die_args *)data)->regs;
	cpu = raw_smp_processor_id();

	/*
	 * Don't do anything if this handler is invoked on crashing cpu.
	 * Otherwise, system will completely hang. Crashing cpu can get
	 * an NMI if system was initially booted with nmi_watchdog parameter.
	 */
	if (cpu == crashing_cpu)
		return NOTIFY_STOP;
	local_irq_disable();

	crash_save_cpu(regs, cpu);
	disable_local_APIC();
	atomic_dec(&waiting_for_crash_ipi);
	/* Assume hlt works */
	for(;;)
		halt();

	return 1;
}

static void smp_send_nmi_allbutself(void)
{
	send_IPI_allbutself(NMI_VECTOR);
}

/*
 * This code is a best effort heuristic to get the
 * other cpus to stop executing. So races with
 * cpu hotplug shouldn't matter.
 */

static struct notifier_block crash_nmi_nb = {
	.notifier_call = crash_nmi_callback,
};

static void nmi_shootdown_cpus(void)
{
	unsigned long msecs;

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	if (register_die_notifier(&crash_nmi_nb))
		return;         /* return what? */

	/*
	 * Ensure the new callback function is set before sending
	 * out the NMI
	 */
	wmb();

	smp_send_nmi_allbutself();

	msecs = 1000; /* Wait at most a second for the other cpus to stop */
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && msecs) {
		mdelay(1);
		msecs--;
	}
	/* Leave the nmi callback set */
	disable_local_APIC();
}
#else
static void nmi_shootdown_cpus(void)
{
	/* There are no cpus to shootdown */
}
#endif

void machine_crash_shutdown(struct pt_regs *regs)
{
	/*
	 * This function is only called after the system
	 * has panicked or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means shooting down the other cpus in
	 * an SMP system.
	 */
	/* The kernel is broken so disable interrupts */
	local_irq_disable();

	/* Make a note of crashing cpu. Will be used in NMI callback.*/
	crashing_cpu = smp_processor_id();
	nmi_shootdown_cpus();

	if(cpu_has_apic)
		 disable_local_APIC();

	disable_IO_APIC();

	crash_save_cpu(regs, smp_processor_id());
}
