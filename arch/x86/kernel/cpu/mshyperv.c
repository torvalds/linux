// SPDX-License-Identifier: GPL-2.0-only
/*
 * HyperV  Detection code.
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
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
#include <linux/random.h>
#include <asm/processor.h>
#include <asm/hypervisor.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>
#include <asm/desc.h>
#include <asm/idtentry.h>
#include <asm/irq_regs.h>
#include <asm/i8259.h>
#include <asm/apic.h>
#include <asm/timer.h>
#include <asm/reboot.h>
#include <asm/nmi.h>
#include <clocksource/hyperv_timer.h>
#include <asm/numa.h>
#include <asm/svm.h>

/* Is Linux running as the root partition? */
bool hv_root_partition;
/* Is Linux running on nested Microsoft Hypervisor */
bool hv_nested;
struct ms_hyperv_info ms_hyperv;

/* Used in modules via hv_do_hypercall(): see arch/x86/include/asm/mshyperv.h */
bool hyperv_paravisor_present __ro_after_init;
EXPORT_SYMBOL_GPL(hyperv_paravisor_present);

#if IS_ENABLED(CONFIG_HYPERV)
static inline unsigned int hv_get_nested_msr(unsigned int reg)
{
	if (hv_is_sint_msr(reg))
		return reg - HV_X64_MSR_SINT0 + HV_X64_MSR_NESTED_SINT0;

	switch (reg) {
	case HV_X64_MSR_SIMP:
		return HV_X64_MSR_NESTED_SIMP;
	case HV_X64_MSR_SIEFP:
		return HV_X64_MSR_NESTED_SIEFP;
	case HV_X64_MSR_SVERSION:
		return HV_X64_MSR_NESTED_SVERSION;
	case HV_X64_MSR_SCONTROL:
		return HV_X64_MSR_NESTED_SCONTROL;
	case HV_X64_MSR_EOM:
		return HV_X64_MSR_NESTED_EOM;
	default:
		return reg;
	}
}

u64 hv_get_non_nested_msr(unsigned int reg)
{
	u64 value;

	if (hv_is_synic_msr(reg) && ms_hyperv.paravisor_present)
		hv_ivm_msr_read(reg, &value);
	else
		rdmsrl(reg, value);
	return value;
}
EXPORT_SYMBOL_GPL(hv_get_non_nested_msr);

void hv_set_non_nested_msr(unsigned int reg, u64 value)
{
	if (hv_is_synic_msr(reg) && ms_hyperv.paravisor_present) {
		hv_ivm_msr_write(reg, value);

		/* Write proxy bit via wrmsl instruction */
		if (hv_is_sint_msr(reg))
			wrmsrl(reg, value | 1 << 20);
	} else {
		wrmsrl(reg, value);
	}
}
EXPORT_SYMBOL_GPL(hv_set_non_nested_msr);

u64 hv_get_msr(unsigned int reg)
{
	if (hv_nested)
		reg = hv_get_nested_msr(reg);

	return hv_get_non_nested_msr(reg);
}
EXPORT_SYMBOL_GPL(hv_get_msr);

void hv_set_msr(unsigned int reg, u64 value)
{
	if (hv_nested)
		reg = hv_get_nested_msr(reg);

	hv_set_non_nested_msr(reg, value);
}
EXPORT_SYMBOL_GPL(hv_set_msr);

static void (*vmbus_handler)(void);
static void (*hv_stimer0_handler)(void);
static void (*hv_kexec_handler)(void);
static void (*hv_crash_handler)(struct pt_regs *regs);

DEFINE_IDTENTRY_SYSVEC(sysvec_hyperv_callback)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	inc_irq_stat(irq_hv_callback_count);
	if (vmbus_handler)
		vmbus_handler();

	if (ms_hyperv.hints & HV_DEPRECATING_AEOI_RECOMMENDED)
		apic_eoi();

	set_irq_regs(old_regs);
}

void hv_setup_vmbus_handler(void (*handler)(void))
{
	vmbus_handler = handler;
}

void hv_remove_vmbus_handler(void)
{
	/* We have no way to deallocate the interrupt gate */
	vmbus_handler = NULL;
}

/*
 * Routines to do per-architecture handling of stimer0
 * interrupts when in Direct Mode
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_hyperv_stimer0)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	inc_irq_stat(hyperv_stimer0_count);
	if (hv_stimer0_handler)
		hv_stimer0_handler();
	add_interrupt_randomness(HYPERV_STIMER0_VECTOR);
	apic_eoi();

	set_irq_regs(old_regs);
}

/* For x86/x64, override weak placeholders in hyperv_timer.c */
void hv_setup_stimer0_handler(void (*handler)(void))
{
	hv_stimer0_handler = handler;
}

void hv_remove_stimer0_handler(void)
{
	/* We have no way to deallocate the interrupt gate */
	hv_stimer0_handler = NULL;
}

void hv_setup_kexec_handler(void (*handler)(void))
{
	hv_kexec_handler = handler;
}

void hv_remove_kexec_handler(void)
{
	hv_kexec_handler = NULL;
}

void hv_setup_crash_handler(void (*handler)(struct pt_regs *regs))
{
	hv_crash_handler = handler;
}

void hv_remove_crash_handler(void)
{
	hv_crash_handler = NULL;
}

#ifdef CONFIG_KEXEC_CORE
static void hv_machine_shutdown(void)
{
	if (kexec_in_progress && hv_kexec_handler)
		hv_kexec_handler();

	/*
	 * Call hv_cpu_die() on all the CPUs, otherwise later the hypervisor
	 * corrupts the old VP Assist Pages and can crash the kexec kernel.
	 */
	if (kexec_in_progress)
		cpuhp_remove_state(CPUHP_AP_HYPERV_ONLINE);

	/* The function calls stop_other_cpus(). */
	native_machine_shutdown();

	/* Disable the hypercall page when there is only 1 active CPU. */
	if (kexec_in_progress)
		hyperv_cleanup();
}
#endif /* CONFIG_KEXEC_CORE */

#ifdef CONFIG_CRASH_DUMP
static void hv_machine_crash_shutdown(struct pt_regs *regs)
{
	if (hv_crash_handler)
		hv_crash_handler(regs);

	/* The function calls crash_smp_send_stop(). */
	native_machine_crash_shutdown(regs);

	/* Disable the hypercall page when there is only 1 active CPU. */
	hyperv_cleanup();
}
#endif /* CONFIG_CRASH_DUMP */
#endif /* CONFIG_HYPERV */

static uint32_t  __init ms_hyperv_platform(void)
{
	u32 eax;
	u32 hyp_signature[3];

	if (!boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return 0;

	cpuid(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS,
	      &eax, &hyp_signature[0], &hyp_signature[1], &hyp_signature[2]);

	if (eax < HYPERV_CPUID_MIN || eax > HYPERV_CPUID_MAX ||
	    memcmp("Microsoft Hv", hyp_signature, 12))
		return 0;

	/* HYPERCALL and VP_INDEX MSRs are mandatory for all features. */
	eax = cpuid_eax(HYPERV_CPUID_FEATURES);
	if (!(eax & HV_MSR_HYPERCALL_AVAILABLE)) {
		pr_warn("x86/hyperv: HYPERCALL MSR not available.\n");
		return 0;
	}
	if (!(eax & HV_MSR_VP_INDEX_AVAILABLE)) {
		pr_warn("x86/hyperv: VP_INDEX MSR not available.\n");
		return 0;
	}

	return HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS;
}

#ifdef CONFIG_X86_LOCAL_APIC
/*
 * Prior to WS2016 Debug-VM sends NMIs to all CPUs which makes
 * it difficult to process CHANNELMSG_UNLOAD in case of crash. Handle
 * unknown NMI on the first CPU which gets it.
 */
static int hv_nmi_unknown(unsigned int val, struct pt_regs *regs)
{
	static atomic_t nmi_cpu = ATOMIC_INIT(-1);
	unsigned int old_cpu, this_cpu;

	if (!unknown_nmi_panic)
		return NMI_DONE;

	old_cpu = -1;
	this_cpu = raw_smp_processor_id();
	if (!atomic_try_cmpxchg(&nmi_cpu, &old_cpu, this_cpu))
		return NMI_HANDLED;

	return NMI_DONE;
}
#endif

static unsigned long hv_get_tsc_khz(void)
{
	unsigned long freq;

	rdmsrl(HV_X64_MSR_TSC_FREQUENCY, freq);

	return freq / 1000;
}

#if defined(CONFIG_SMP) && IS_ENABLED(CONFIG_HYPERV)
static void __init hv_smp_prepare_boot_cpu(void)
{
	native_smp_prepare_boot_cpu();
#if defined(CONFIG_X86_64) && defined(CONFIG_PARAVIRT_SPINLOCKS)
	hv_init_spinlocks();
#endif
}

static void __init hv_smp_prepare_cpus(unsigned int max_cpus)
{
#ifdef CONFIG_X86_64
	int i;
	int ret;
#endif

	native_smp_prepare_cpus(max_cpus);

	/*
	 *  Override wakeup_secondary_cpu_64 callback for SEV-SNP
	 *  enlightened guest.
	 */
	if (!ms_hyperv.paravisor_present && hv_isolation_type_snp()) {
		apic->wakeup_secondary_cpu_64 = hv_snp_boot_ap;
		return;
	}

#ifdef CONFIG_X86_64
	for_each_present_cpu(i) {
		if (i == 0)
			continue;
		ret = hv_call_add_logical_proc(numa_cpu_node(i), i, cpu_physical_id(i));
		BUG_ON(ret);
	}

	for_each_present_cpu(i) {
		if (i == 0)
			continue;
		ret = hv_call_create_vp(numa_cpu_node(i), hv_current_partition_id, i, i);
		BUG_ON(ret);
	}
#endif
}
#endif

/*
 * When a fully enlightened TDX VM runs on Hyper-V, the firmware sets the
 * HW_REDUCED flag: refer to acpi_tb_create_local_fadt(). Consequently ttyS0
 * interrupts can't work because request_irq() -> ... -> irq_to_desc() returns
 * NULL for ttyS0. This happens because mp_config_acpi_legacy_irqs() sees a
 * nr_legacy_irqs() of 0, so it doesn't initialize the array 'mp_irqs[]', and
 * later setup_IO_APIC_irqs() -> find_irq_entry() fails to find the legacy irqs
 * from the array and hence doesn't create the necessary irq description info.
 *
 * Clone arch/x86/kernel/acpi/boot.c: acpi_generic_reduced_hw_init() here,
 * except don't change 'legacy_pic', which keeps its default value
 * 'default_legacy_pic'. This way, mp_config_acpi_legacy_irqs() sees a non-zero
 * nr_legacy_irqs() and eventually serial console interrupts works properly.
 */
static void __init reduced_hw_init(void)
{
	x86_init.timers.timer_init	= x86_init_noop;
	x86_init.irqs.pre_vector_init	= x86_init_noop;
}

int hv_get_hypervisor_version(union hv_hypervisor_version_info *info)
{
	unsigned int hv_max_functions;

	hv_max_functions = cpuid_eax(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS);
	if (hv_max_functions < HYPERV_CPUID_VERSION) {
		pr_err("%s: Could not detect Hyper-V version\n", __func__);
		return -ENODEV;
	}

	cpuid(HYPERV_CPUID_VERSION, &info->eax, &info->ebx, &info->ecx, &info->edx);

	return 0;
}

static void __init ms_hyperv_init_platform(void)
{
	int hv_max_functions_eax;

#ifdef CONFIG_PARAVIRT
	pv_info.name = "Hyper-V";
#endif

	/*
	 * Extract the features and hints
	 */
	ms_hyperv.features = cpuid_eax(HYPERV_CPUID_FEATURES);
	ms_hyperv.priv_high = cpuid_ebx(HYPERV_CPUID_FEATURES);
	ms_hyperv.misc_features = cpuid_edx(HYPERV_CPUID_FEATURES);
	ms_hyperv.hints    = cpuid_eax(HYPERV_CPUID_ENLIGHTMENT_INFO);

	hv_max_functions_eax = cpuid_eax(HYPERV_CPUID_VENDOR_AND_MAX_FUNCTIONS);

	pr_info("Hyper-V: privilege flags low 0x%x, high 0x%x, hints 0x%x, misc 0x%x\n",
		ms_hyperv.features, ms_hyperv.priv_high, ms_hyperv.hints,
		ms_hyperv.misc_features);

	ms_hyperv.max_vp_index = cpuid_eax(HYPERV_CPUID_IMPLEMENT_LIMITS);
	ms_hyperv.max_lp_index = cpuid_ebx(HYPERV_CPUID_IMPLEMENT_LIMITS);

	pr_debug("Hyper-V: max %u virtual processors, %u logical processors\n",
		 ms_hyperv.max_vp_index, ms_hyperv.max_lp_index);

	/*
	 * Check CPU management privilege.
	 *
	 * To mirror what Windows does we should extract CPU management
	 * features and use the ReservedIdentityBit to detect if Linux is the
	 * root partition. But that requires negotiating CPU management
	 * interface (a process to be finalized). For now, use the privilege
	 * flag as the indicator for running as root.
	 *
	 * Hyper-V should never specify running as root and as a Confidential
	 * VM. But to protect against a compromised/malicious Hyper-V trying
	 * to exploit root behavior to expose Confidential VM memory, ignore
	 * the root partition setting if also a Confidential VM.
	 */
	if ((ms_hyperv.priv_high & HV_CPU_MANAGEMENT) &&
	    !(ms_hyperv.priv_high & HV_ISOLATION)) {
		hv_root_partition = true;
		pr_info("Hyper-V: running as root partition\n");
	}

	if (ms_hyperv.hints & HV_X64_HYPERV_NESTED) {
		hv_nested = true;
		pr_info("Hyper-V: running on a nested hypervisor\n");
	}

	if (ms_hyperv.features & HV_ACCESS_FREQUENCY_MSRS &&
	    ms_hyperv.misc_features & HV_FEATURE_FREQUENCY_MSRS_AVAILABLE) {
		x86_platform.calibrate_tsc = hv_get_tsc_khz;
		x86_platform.calibrate_cpu = hv_get_tsc_khz;
		setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);
	}

	if (ms_hyperv.priv_high & HV_ISOLATION) {
		ms_hyperv.isolation_config_a = cpuid_eax(HYPERV_CPUID_ISOLATION_CONFIG);
		ms_hyperv.isolation_config_b = cpuid_ebx(HYPERV_CPUID_ISOLATION_CONFIG);

		if (ms_hyperv.shared_gpa_boundary_active)
			ms_hyperv.shared_gpa_boundary =
				BIT_ULL(ms_hyperv.shared_gpa_boundary_bits);

		hyperv_paravisor_present = !!ms_hyperv.paravisor_present;

		pr_info("Hyper-V: Isolation Config: Group A 0x%x, Group B 0x%x\n",
			ms_hyperv.isolation_config_a, ms_hyperv.isolation_config_b);


		if (hv_get_isolation_type() == HV_ISOLATION_TYPE_SNP) {
			static_branch_enable(&isolation_type_snp);
		} else if (hv_get_isolation_type() == HV_ISOLATION_TYPE_TDX) {
			static_branch_enable(&isolation_type_tdx);

			/* A TDX VM must use x2APIC and doesn't use lazy EOI. */
			ms_hyperv.hints &= ~HV_X64_APIC_ACCESS_RECOMMENDED;

			if (!ms_hyperv.paravisor_present) {
				/*
				 * Mark the Hyper-V TSC page feature as disabled
				 * in a TDX VM without paravisor so that the
				 * Invariant TSC, which is a better clocksource
				 * anyway, is used instead.
				 */
				ms_hyperv.features &= ~HV_MSR_REFERENCE_TSC_AVAILABLE;

				/*
				 * The Invariant TSC is expected to be available
				 * in a TDX VM without paravisor, but if not,
				 * print a warning message. The slower Hyper-V MSR-based
				 * Ref Counter should end up being the clocksource.
				 */
				if (!(ms_hyperv.features & HV_ACCESS_TSC_INVARIANT))
					pr_warn("Hyper-V: Invariant TSC is unavailable\n");

				/* HV_MSR_CRASH_CTL is unsupported. */
				ms_hyperv.misc_features &= ~HV_FEATURE_GUEST_CRASH_MSR_AVAILABLE;

				/* Don't trust Hyper-V's TLB-flushing hypercalls. */
				ms_hyperv.hints &= ~HV_X64_REMOTE_TLB_FLUSH_RECOMMENDED;

				x86_init.acpi.reduced_hw_early_init = reduced_hw_init;
			}
		}
	}

	if (hv_max_functions_eax >= HYPERV_CPUID_NESTED_FEATURES) {
		ms_hyperv.nested_features =
			cpuid_eax(HYPERV_CPUID_NESTED_FEATURES);
		pr_info("Hyper-V: Nested features: 0x%x\n",
			ms_hyperv.nested_features);
	}

#ifdef CONFIG_X86_LOCAL_APIC
	if (ms_hyperv.features & HV_ACCESS_FREQUENCY_MSRS &&
	    ms_hyperv.misc_features & HV_FEATURE_FREQUENCY_MSRS_AVAILABLE) {
		/*
		 * Get the APIC frequency.
		 */
		u64	hv_lapic_frequency;

		rdmsrl(HV_X64_MSR_APIC_FREQUENCY, hv_lapic_frequency);
		hv_lapic_frequency = div_u64(hv_lapic_frequency, HZ);
		lapic_timer_period = hv_lapic_frequency;
		pr_info("Hyper-V: LAPIC Timer Frequency: %#x\n",
			lapic_timer_period);
	}

	register_nmi_handler(NMI_UNKNOWN, hv_nmi_unknown, NMI_FLAG_FIRST,
			     "hv_nmi_unknown");
#endif

#ifdef CONFIG_X86_IO_APIC
	no_timer_check = 1;
#endif

#if IS_ENABLED(CONFIG_HYPERV)
#if defined(CONFIG_KEXEC_CORE)
	machine_ops.shutdown = hv_machine_shutdown;
#endif
#if defined(CONFIG_CRASH_DUMP)
	machine_ops.crash_shutdown = hv_machine_crash_shutdown;
#endif
#endif
	if (ms_hyperv.features & HV_ACCESS_TSC_INVARIANT) {
		/*
		 * Writing to synthetic MSR 0x40000118 updates/changes the
		 * guest visible CPUIDs. Setting bit 0 of this MSR  enables
		 * guests to report invariant TSC feature through CPUID
		 * instruction, CPUID 0x800000007/EDX, bit 8. See code in
		 * early_init_intel() where this bit is examined. The
		 * setting of this MSR bit should happen before init_intel()
		 * is called.
		 */
		wrmsrl(HV_X64_MSR_TSC_INVARIANT_CONTROL, HV_EXPOSE_INVARIANT_TSC);
		setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);
	}

	/*
	 * Generation 2 instances don't support reading the NMI status from
	 * 0x61 port.
	 */
	if (efi_enabled(EFI_BOOT))
		x86_platform.get_nmi_reason = hv_get_nmi_reason;

#if IS_ENABLED(CONFIG_HYPERV)
	if ((hv_get_isolation_type() == HV_ISOLATION_TYPE_VBS) ||
	    ms_hyperv.paravisor_present)
		hv_vtom_init();
	/*
	 * Setup the hook to get control post apic initialization.
	 */
	x86_platform.apic_post_init = hyperv_init;
	hyperv_setup_mmu_ops();

	/* Install system interrupt handler for hypervisor callback */
	sysvec_install(HYPERVISOR_CALLBACK_VECTOR, sysvec_hyperv_callback);

	/* Install system interrupt handler for reenlightenment notifications */
	if (ms_hyperv.features & HV_ACCESS_REENLIGHTENMENT) {
		sysvec_install(HYPERV_REENLIGHTENMENT_VECTOR, sysvec_hyperv_reenlightenment);
	}

	/* Install system interrupt handler for stimer0 */
	if (ms_hyperv.misc_features & HV_STIMER_DIRECT_MODE_AVAILABLE) {
		sysvec_install(HYPERV_STIMER0_VECTOR, sysvec_hyperv_stimer0);
	}

# ifdef CONFIG_SMP
	smp_ops.smp_prepare_boot_cpu = hv_smp_prepare_boot_cpu;
	if (hv_root_partition ||
	    (!ms_hyperv.paravisor_present && hv_isolation_type_snp()))
		smp_ops.smp_prepare_cpus = hv_smp_prepare_cpus;
# endif

	/*
	 * Hyper-V doesn't provide irq remapping for IO-APIC. To enable x2apic,
	 * set x2apic destination mode to physical mode when x2apic is available
	 * and Hyper-V IOMMU driver makes sure cpus assigned with IO-APIC irqs
	 * have 8-bit APIC id.
	 */
# ifdef CONFIG_X86_X2APIC
	if (x2apic_supported())
		x2apic_phys = 1;
# endif

	/* Register Hyper-V specific clocksource */
	hv_init_clocksource();
	hv_vtl_init_platform();
#endif
	/*
	 * TSC should be marked as unstable only after Hyper-V
	 * clocksource has been initialized. This ensures that the
	 * stability of the sched_clock is not altered.
	 */
	if (!(ms_hyperv.features & HV_ACCESS_TSC_INVARIANT))
		mark_tsc_unstable("running on Hyper-V");

	hardlockup_detector_disable();
}

static bool __init ms_hyperv_x2apic_available(void)
{
	return x2apic_supported();
}

/*
 * If ms_hyperv_msi_ext_dest_id() returns true, hyperv_prepare_irq_remapping()
 * returns -ENODEV and the Hyper-V IOMMU driver is not used; instead, the
 * generic support of the 15-bit APIC ID is used: see __irq_msi_compose_msg().
 *
 * Note: for a VM on Hyper-V, the I/O-APIC is the only device which
 * (logically) generates MSIs directly to the system APIC irq domain.
 * There is no HPET, and PCI MSI/MSI-X interrupts are remapped by the
 * pci-hyperv host bridge.
 *
 * Note: for a Hyper-V root partition, this will always return false.
 * The hypervisor doesn't expose these HYPERV_CPUID_VIRT_STACK_* cpuids by
 * default, they are implemented as intercepts by the Windows Hyper-V stack.
 * Even a nested root partition (L2 root) will not get them because the
 * nested (L1) hypervisor filters them out.
 */
static bool __init ms_hyperv_msi_ext_dest_id(void)
{
	u32 eax;

	eax = cpuid_eax(HYPERV_CPUID_VIRT_STACK_INTERFACE);
	if (eax != HYPERV_VS_INTERFACE_EAX_SIGNATURE)
		return false;

	eax = cpuid_eax(HYPERV_CPUID_VIRT_STACK_PROPERTIES);
	return eax & HYPERV_VS_PROPERTIES_EAX_EXTENDED_IOAPIC_RTE;
}

#ifdef CONFIG_AMD_MEM_ENCRYPT
static void hv_sev_es_hcall_prepare(struct ghcb *ghcb, struct pt_regs *regs)
{
	/* RAX and CPL are already in the GHCB */
	ghcb_set_rcx(ghcb, regs->cx);
	ghcb_set_rdx(ghcb, regs->dx);
	ghcb_set_r8(ghcb, regs->r8);
}

static bool hv_sev_es_hcall_finish(struct ghcb *ghcb, struct pt_regs *regs)
{
	/* No checking of the return state needed */
	return true;
}
#endif

const __initconst struct hypervisor_x86 x86_hyper_ms_hyperv = {
	.name			= "Microsoft Hyper-V",
	.detect			= ms_hyperv_platform,
	.type			= X86_HYPER_MS_HYPERV,
	.init.x2apic_available	= ms_hyperv_x2apic_available,
	.init.msi_ext_dest_id	= ms_hyperv_msi_ext_dest_id,
	.init.init_platform	= ms_hyperv_init_platform,
	.init.guest_late_init	= ms_hyperv_late_init,
#ifdef CONFIG_AMD_MEM_ENCRYPT
	.runtime.sev_es_hcall_prepare = hv_sev_es_hcall_prepare,
	.runtime.sev_es_hcall_finish = hv_sev_es_hcall_finish,
#endif
};
