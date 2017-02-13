/*
 * HyperV  Detection code.
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/hardirq.h>
#include <linux/efi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kexec.h>
#include <asm/processor.h>
#include <asm/hypervisor.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>
#include <asm/desc.h>
#include <asm/irq_regs.h>
#include <asm/i8259.h>
#include <asm/apic.h>
#include <asm/timer.h>
#include <asm/reboot.h>
#include <asm/nmi.h>

struct ms_hyperv_info ms_hyperv;
EXPORT_SYMBOL_GPL(ms_hyperv);

#if IS_ENABLED(CONFIG_HYPERV)
static void (*vmbus_handler)(void);
static void (*hv_kexec_handler)(void);
static void (*hv_crash_handler)(struct pt_regs *regs);

void hyperv_vector_handler(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	entering_irq();
	inc_irq_stat(irq_hv_callback_count);
	if (vmbus_handler)
		vmbus_handler();

	exiting_irq();
	set_irq_regs(old_regs);
}

void hv_setup_vmbus_irq(void (*handler)(void))
{
	vmbus_handler = handler;
	/*
	 * Setup the IDT for hypervisor callback. Prevent reallocation
	 * at module reload.
	 */
	if (!test_bit(HYPERVISOR_CALLBACK_VECTOR, used_vectors))
		alloc_intr_gate(HYPERVISOR_CALLBACK_VECTOR,
				hyperv_callback_vector);
}

void hv_remove_vmbus_irq(void)
{
	/* We have no way to deallocate the interrupt gate */
	vmbus_handler = NULL;
}
EXPORT_SYMBOL_GPL(hv_setup_vmbus_irq);
EXPORT_SYMBOL_GPL(hv_remove_vmbus_irq);

void hv_setup_kexec_handler(void (*handler)(void))
{
	hv_kexec_handler = handler;
}
EXPORT_SYMBOL_GPL(hv_setup_kexec_handler);

void hv_remove_kexec_handler(void)
{
	hv_kexec_handler = NULL;
}
EXPORT_SYMBOL_GPL(hv_remove_kexec_handler);

void hv_setup_crash_handler(void (*handler)(struct pt_regs *regs))
{
	hv_crash_handler = handler;
}
EXPORT_SYMBOL_GPL(hv_setup_crash_handler);

void hv_remove_crash_handler(void)
{
	hv_crash_handler = NULL;
}
EXPORT_SYMBOL_GPL(hv_remove_crash_handler);

#ifdef CONFIG_KEXEC_CORE
static void hv_machine_shutdown(void)
{
	if (kexec_in_progress && hv_kexec_handler)
		hv_kexec_handler();
	native_machine_shutdown();
}

static void hv_machine_crash_shutdown(struct pt_regs *regs)
{
	if (hv_crash_handler)
		hv_crash_handler(regs);
	native_machine_crash_shutdown(regs);
}
#endif /* CONFIG_KEXEC_CORE */
#endif /* CONFIG_HYPERV */

static uint32_t  __init ms_hyperv_platform(void)
{
	u32 eax;
	u32 hyp_signature[3];

	if (!boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return 0;

	cpuid(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS,
	      &eax, &hyp_signature[0], &hyp_signature[1], &hyp_signature[2]);

	if (eax >= HYPERV_CPUID_MIN &&
	    eax <= HYPERV_CPUID_MAX &&
	    !memcmp("Microsoft Hv", hyp_signature, 12))
		return HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS;

	return 0;
}

static u64 read_hv_clock(struct clocksource *arg)
{
	u64 current_tick;
	/*
	 * Read the partition counter to get the current tick count. This count
	 * is set to 0 when the partition is created and is incremented in
	 * 100 nanosecond units.
	 */
	rdmsrl(HV_X64_MSR_TIME_REF_COUNT, current_tick);
	return current_tick;
}

static struct clocksource hyperv_cs = {
	.name		= "hyperv_clocksource",
	.rating		= 400, /* use this when running on Hyperv*/
	.read		= read_hv_clock,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static unsigned char hv_get_nmi_reason(void)
{
	return 0;
}

#ifdef CONFIG_X86_LOCAL_APIC
/*
 * Prior to WS2016 Debug-VM sends NMIs to all CPUs which makes
 * it dificult to process CHANNELMSG_UNLOAD in case of crash. Handle
 * unknown NMI on the first CPU which gets it.
 */
static int hv_nmi_unknown(unsigned int val, struct pt_regs *regs)
{
	static atomic_t nmi_cpu = ATOMIC_INIT(-1);

	if (!unknown_nmi_panic)
		return NMI_DONE;

	if (atomic_cmpxchg(&nmi_cpu, -1, raw_smp_processor_id()) != -1)
		return NMI_HANDLED;

	return NMI_DONE;
}
#endif

static void __init ms_hyperv_init_platform(void)
{
	/*
	 * Extract the features and hints
	 */
	ms_hyperv.features = cpuid_eax(HYPERV_CPUID_FEATURES);
	ms_hyperv.misc_features = cpuid_edx(HYPERV_CPUID_FEATURES);
	ms_hyperv.hints    = cpuid_eax(HYPERV_CPUID_ENLIGHTMENT_INFO);

	pr_info("HyperV: features 0x%x, hints 0x%x\n",
		ms_hyperv.features, ms_hyperv.hints);

#ifdef CONFIG_X86_LOCAL_APIC
	if (ms_hyperv.features & HV_X64_MSR_APIC_FREQUENCY_AVAILABLE) {
		/*
		 * Get the APIC frequency.
		 */
		u64	hv_lapic_frequency;

		rdmsrl(HV_X64_MSR_APIC_FREQUENCY, hv_lapic_frequency);
		hv_lapic_frequency = div_u64(hv_lapic_frequency, HZ);
		lapic_timer_frequency = hv_lapic_frequency;
		pr_info("HyperV: LAPIC Timer Frequency: %#x\n",
			lapic_timer_frequency);
	}

	register_nmi_handler(NMI_UNKNOWN, hv_nmi_unknown, NMI_FLAG_FIRST,
			     "hv_nmi_unknown");
#endif

	if (ms_hyperv.features & HV_X64_MSR_TIME_REF_COUNT_AVAILABLE)
		clocksource_register_hz(&hyperv_cs, NSEC_PER_SEC/100);

#ifdef CONFIG_X86_IO_APIC
	no_timer_check = 1;
#endif

#if IS_ENABLED(CONFIG_HYPERV) && defined(CONFIG_KEXEC_CORE)
	machine_ops.shutdown = hv_machine_shutdown;
	machine_ops.crash_shutdown = hv_machine_crash_shutdown;
#endif
	mark_tsc_unstable("running on Hyper-V");

	/*
	 * Generation 2 instances don't support reading the NMI status from
	 * 0x61 port.
	 */
	if (efi_enabled(EFI_BOOT))
		x86_platform.get_nmi_reason = hv_get_nmi_reason;
}

const __refconst struct hypervisor_x86 x86_hyper_ms_hyperv = {
	.name			= "Microsoft HyperV",
	.detect			= ms_hyperv_platform,
	.init_platform		= ms_hyperv_init_platform,
};
EXPORT_SYMBOL(x86_hyper_ms_hyperv);
