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
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/delay.h>

#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>
#include <asm/hw_irq.h>
#include <asm/mach_apic.h>

/* This keeps a track of which one is crashing cpu. */
static int crashing_cpu;

#ifdef CONFIG_SMP
static atomic_t waiting_for_crash_ipi;

static int crash_nmi_callback(struct pt_regs *regs, int cpu)
{
	/*
	 * Don't do anything if this handler is invoked on crashing cpu.
	 * Otherwise, system will completely hang. Crashing cpu can get
	 * an NMI if system was initially booted with nmi_watchdog parameter.
	 */
	if (cpu == crashing_cpu)
		return 1;
	local_irq_disable();

	disable_local_APIC();
	atomic_dec(&waiting_for_crash_ipi);
	/* Assume hlt works */
	for(;;)
		asm("hlt");

	return 1;
}

static void smp_send_nmi_allbutself(void)
{
	send_IPI_allbutself(APIC_DM_NMI);
}

/*
 * This code is a best effort heuristic to get the
 * other cpus to stop executing. So races with
 * cpu hotplug shouldn't matter.
 */

static void nmi_shootdown_cpus(void)
{
	unsigned long msecs;

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	set_nmi_callback(crash_nmi_callback);

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
	 * has paniced or is otherwise in a critical state.
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

#if defined(CONFIG_X86_IO_APIC)
	disable_IO_APIC();
#endif

}
