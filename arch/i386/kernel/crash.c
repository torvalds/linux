/*
 * Architecture specific (i386) functions for kexec based crash dumps.
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
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/elf.h>
#include <linux/elfcore.h>

#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>
#include <asm/hw_irq.h>
#include <mach_ipi.h>

#define MAX_NOTE_BYTES 1024
typedef u32 note_buf_t[MAX_NOTE_BYTES/4];

note_buf_t crash_notes[NR_CPUS];

#ifdef CONFIG_SMP
static atomic_t waiting_for_crash_ipi;

static int crash_nmi_callback(struct pt_regs *regs, int cpu)
{
	local_irq_disable();
	atomic_dec(&waiting_for_crash_ipi);
	/* Assume hlt works */
	__asm__("hlt");
	for(;;);
	return 1;
}

/*
 * By using the NMI code instead of a vector we just sneak thru the
 * word generator coming out with just what we want.  AND it does
 * not matter if clustered_apic_mode is set or not.
 */
static void smp_send_nmi_allbutself(void)
{
	send_IPI_allbutself(APIC_DM_NMI);
}

static void nmi_shootdown_cpus(void)
{
	unsigned long msecs;
	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);

	/* Would it be better to replace the trap vector here? */
	set_nmi_callback(crash_nmi_callback);
	/* Ensure the new callback function is set before sending
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
}
#else
static void nmi_shootdown_cpus(void)
{
	/* There are no cpus to shootdown */
}
#endif

void machine_crash_shutdown(void)
{
	/* This function is only called after the system
	 * has paniced or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means shooting down the other cpus in
	 * an SMP system.
	 */
	/* The kernel is broken so disable interrupts */
	local_irq_disable();
	nmi_shootdown_cpus();
}
