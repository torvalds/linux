/*
 * Architecture specific (i386/x86_64) functions for kexec based crash dumps.
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
#include <linux/elf.h>
#include <linux/elfcore.h>

#include <asm/processor.h>
#include <asm/hardirq.h>
#include <asm/nmi.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/hpet.h>
#include <linux/kdebug.h>
#include <asm/cpu.h>
#include <asm/reboot.h>
#include <asm/virtext.h>
#include <asm/iommu.h>


#if defined(CONFIG_SMP) && defined(CONFIG_X86_LOCAL_APIC)

static void kdump_nmi_callback(int cpu, struct die_args *args)
{
	struct pt_regs *regs;
#ifdef CONFIG_X86_32
	struct pt_regs fixed_regs;
#endif

	regs = args->regs;

#ifdef CONFIG_X86_32
	if (!user_mode_vm(regs)) {
		crash_fixup_ss_esp(&fixed_regs, regs);
		regs = &fixed_regs;
	}
#endif
	crash_save_cpu(regs, cpu);

	/* Disable VMX or SVM if needed.
	 *
	 * We need to disable virtualization on all CPUs.
	 * Having VMX or SVM enabled on any CPU may break rebooting
	 * after the kdump kernel has finished its task.
	 */
	cpu_emergency_vmxoff();
	cpu_emergency_svm_disable();

	disable_local_APIC();
}

static void kdump_nmi_shootdown_cpus(void)
{
	nmi_shootdown_cpus(kdump_nmi_callback);

	disable_local_APIC();
}

#else
static void kdump_nmi_shootdown_cpus(void)
{
	/* There are no cpus to shootdown */
}
#endif

void native_machine_crash_shutdown(struct pt_regs *regs)
{
	/* This function is only called after the system
	 * has panicked or is otherwise in a critical state.
	 * The minimum amount of code to allow a kexec'd kernel
	 * to run successfully needs to happen here.
	 *
	 * In practice this means shooting down the other cpus in
	 * an SMP system.
	 */
	/* The kernel is broken so disable interrupts */
	local_irq_disable();

	kdump_nmi_shootdown_cpus();

	/* Booting kdump kernel with VMX or SVM enabled won't work,
	 * because (among other limitations) we can't disable paging
	 * with the virt flags.
	 */
	cpu_emergency_vmxoff();
	cpu_emergency_svm_disable();

	lapic_shutdown();
#if defined(CONFIG_X86_IO_APIC)
	disable_IO_APIC();
#endif
#ifdef CONFIG_HPET_TIMER
	hpet_disable();
#endif

#ifdef CONFIG_X86_64
	pci_iommu_shutdown();
#endif

	crash_save_cpu(regs, safe_smp_processor_id());
}
