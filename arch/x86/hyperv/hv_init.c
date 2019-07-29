// SPDX-License-Identifier: GPL-2.0-only
/*
 * X86 specific Hyper-V initialization code.
 *
 * Copyright (C) 2016, Microsoft, Inc.
 *
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 */

#include <linux/efi.h>
#include <linux/types.h>
#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/hypervisor.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/hyperv.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>
#include <clocksource/hyperv_timer.h>

void *hv_hypercall_pg;
EXPORT_SYMBOL_GPL(hv_hypercall_pg);

u32 *hv_vp_index;
EXPORT_SYMBOL_GPL(hv_vp_index);

struct hv_vp_assist_page **hv_vp_assist_page;
EXPORT_SYMBOL_GPL(hv_vp_assist_page);

void  __percpu **hyperv_pcpu_input_arg;
EXPORT_SYMBOL_GPL(hyperv_pcpu_input_arg);

u32 hv_max_vp_index;
EXPORT_SYMBOL_GPL(hv_max_vp_index);

static int hv_cpu_init(unsigned int cpu)
{
	u64 msr_vp_index;
	struct hv_vp_assist_page **hvp = &hv_vp_assist_page[smp_processor_id()];
	void **input_arg;
	struct page *pg;

	input_arg = (void **)this_cpu_ptr(hyperv_pcpu_input_arg);
	pg = alloc_page(GFP_KERNEL);
	if (unlikely(!pg))
		return -ENOMEM;
	*input_arg = page_address(pg);

	hv_get_vp_index(msr_vp_index);

	hv_vp_index[smp_processor_id()] = msr_vp_index;

	if (msr_vp_index > hv_max_vp_index)
		hv_max_vp_index = msr_vp_index;

	if (!hv_vp_assist_page)
		return 0;

	/*
	 * The VP ASSIST PAGE is an "overlay" page (see Hyper-V TLFS's Section
	 * 5.2.1 "GPA Overlay Pages"). Here it must be zeroed out to make sure
	 * we always write the EOI MSR in hv_apic_eoi_write() *after* the
	 * EOI optimization is disabled in hv_cpu_die(), otherwise a CPU may
	 * not be stopped in the case of CPU offlining and the VM will hang.
	 */
	if (!*hvp) {
		*hvp = __vmalloc(PAGE_SIZE, GFP_KERNEL | __GFP_ZERO,
				 PAGE_KERNEL);
	}

	if (*hvp) {
		u64 val;

		val = vmalloc_to_pfn(*hvp);
		val = (val << HV_X64_MSR_VP_ASSIST_PAGE_ADDRESS_SHIFT) |
			HV_X64_MSR_VP_ASSIST_PAGE_ENABLE;

		wrmsrl(HV_X64_MSR_VP_ASSIST_PAGE, val);
	}

	return 0;
}

static void (*hv_reenlightenment_cb)(void);

static void hv_reenlightenment_notify(struct work_struct *dummy)
{
	struct hv_tsc_emulation_status emu_status;

	rdmsrl(HV_X64_MSR_TSC_EMULATION_STATUS, *(u64 *)&emu_status);

	/* Don't issue the callback if TSC accesses are not emulated */
	if (hv_reenlightenment_cb && emu_status.inprogress)
		hv_reenlightenment_cb();
}
static DECLARE_DELAYED_WORK(hv_reenlightenment_work, hv_reenlightenment_notify);

void hyperv_stop_tsc_emulation(void)
{
	u64 freq;
	struct hv_tsc_emulation_status emu_status;

	rdmsrl(HV_X64_MSR_TSC_EMULATION_STATUS, *(u64 *)&emu_status);
	emu_status.inprogress = 0;
	wrmsrl(HV_X64_MSR_TSC_EMULATION_STATUS, *(u64 *)&emu_status);

	rdmsrl(HV_X64_MSR_TSC_FREQUENCY, freq);
	tsc_khz = div64_u64(freq, 1000);
}
EXPORT_SYMBOL_GPL(hyperv_stop_tsc_emulation);

static inline bool hv_reenlightenment_available(void)
{
	/*
	 * Check for required features and priviliges to make TSC frequency
	 * change notifications work.
	 */
	return ms_hyperv.features & HV_X64_ACCESS_FREQUENCY_MSRS &&
		ms_hyperv.misc_features & HV_FEATURE_FREQUENCY_MSRS_AVAILABLE &&
		ms_hyperv.features & HV_X64_ACCESS_REENLIGHTENMENT;
}

__visible void __irq_entry hyperv_reenlightenment_intr(struct pt_regs *regs)
{
	entering_ack_irq();

	inc_irq_stat(irq_hv_reenlightenment_count);

	schedule_delayed_work(&hv_reenlightenment_work, HZ/10);

	exiting_irq();
}

void set_hv_tscchange_cb(void (*cb)(void))
{
	struct hv_reenlightenment_control re_ctrl = {
		.vector = HYPERV_REENLIGHTENMENT_VECTOR,
		.enabled = 1,
		.target_vp = hv_vp_index[smp_processor_id()]
	};
	struct hv_tsc_emulation_control emu_ctrl = {.enabled = 1};

	if (!hv_reenlightenment_available()) {
		pr_warn("Hyper-V: reenlightenment support is unavailable\n");
		return;
	}

	hv_reenlightenment_cb = cb;

	/* Make sure callback is registered before we write to MSRs */
	wmb();

	wrmsrl(HV_X64_MSR_REENLIGHTENMENT_CONTROL, *((u64 *)&re_ctrl));
	wrmsrl(HV_X64_MSR_TSC_EMULATION_CONTROL, *((u64 *)&emu_ctrl));
}
EXPORT_SYMBOL_GPL(set_hv_tscchange_cb);

void clear_hv_tscchange_cb(void)
{
	struct hv_reenlightenment_control re_ctrl;

	if (!hv_reenlightenment_available())
		return;

	rdmsrl(HV_X64_MSR_REENLIGHTENMENT_CONTROL, *(u64 *)&re_ctrl);
	re_ctrl.enabled = 0;
	wrmsrl(HV_X64_MSR_REENLIGHTENMENT_CONTROL, *(u64 *)&re_ctrl);

	hv_reenlightenment_cb = NULL;
}
EXPORT_SYMBOL_GPL(clear_hv_tscchange_cb);

static int hv_cpu_die(unsigned int cpu)
{
	struct hv_reenlightenment_control re_ctrl;
	unsigned int new_cpu;
	unsigned long flags;
	void **input_arg;
	void *input_pg = NULL;

	local_irq_save(flags);
	input_arg = (void **)this_cpu_ptr(hyperv_pcpu_input_arg);
	input_pg = *input_arg;
	*input_arg = NULL;
	local_irq_restore(flags);
	free_page((unsigned long)input_pg);

	if (hv_vp_assist_page && hv_vp_assist_page[cpu])
		wrmsrl(HV_X64_MSR_VP_ASSIST_PAGE, 0);

	if (hv_reenlightenment_cb == NULL)
		return 0;

	rdmsrl(HV_X64_MSR_REENLIGHTENMENT_CONTROL, *((u64 *)&re_ctrl));
	if (re_ctrl.target_vp == hv_vp_index[cpu]) {
		/* Reassign to some other online CPU */
		new_cpu = cpumask_any_but(cpu_online_mask, cpu);

		re_ctrl.target_vp = hv_vp_index[new_cpu];
		wrmsrl(HV_X64_MSR_REENLIGHTENMENT_CONTROL, *((u64 *)&re_ctrl));
	}

	return 0;
}

static int __init hv_pci_init(void)
{
	int gen2vm = efi_enabled(EFI_BOOT);

	/*
	 * For Generation-2 VM, we exit from pci_arch_init() by returning 0.
	 * The purpose is to suppress the harmless warning:
	 * "PCI: Fatal: No config space access function found"
	 */
	if (gen2vm)
		return 0;

	/* For Generation-1 VM, we'll proceed in pci_arch_init().  */
	return 1;
}

/*
 * This function is to be invoked early in the boot sequence after the
 * hypervisor has been detected.
 *
 * 1. Setup the hypercall page.
 * 2. Register Hyper-V specific clocksource.
 * 3. Setup Hyper-V specific APIC entry points.
 */
void __init hyperv_init(void)
{
	u64 guest_id, required_msrs;
	union hv_x64_msr_hypercall_contents hypercall_msr;
	int cpuhp, i;

	if (x86_hyper_type != X86_HYPER_MS_HYPERV)
		return;

	/* Absolutely required MSRs */
	required_msrs = HV_X64_MSR_HYPERCALL_AVAILABLE |
		HV_X64_MSR_VP_INDEX_AVAILABLE;

	if ((ms_hyperv.features & required_msrs) != required_msrs)
		return;

	/*
	 * Allocate the per-CPU state for the hypercall input arg.
	 * If this allocation fails, we will not be able to setup
	 * (per-CPU) hypercall input page and thus this failure is
	 * fatal on Hyper-V.
	 */
	hyperv_pcpu_input_arg = alloc_percpu(void  *);

	BUG_ON(hyperv_pcpu_input_arg == NULL);

	/* Allocate percpu VP index */
	hv_vp_index = kmalloc_array(num_possible_cpus(), sizeof(*hv_vp_index),
				    GFP_KERNEL);
	if (!hv_vp_index)
		return;

	for (i = 0; i < num_possible_cpus(); i++)
		hv_vp_index[i] = VP_INVAL;

	hv_vp_assist_page = kcalloc(num_possible_cpus(),
				    sizeof(*hv_vp_assist_page), GFP_KERNEL);
	if (!hv_vp_assist_page) {
		ms_hyperv.hints &= ~HV_X64_ENLIGHTENED_VMCS_RECOMMENDED;
		goto free_vp_index;
	}

	cpuhp = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/hyperv_init:online",
				  hv_cpu_init, hv_cpu_die);
	if (cpuhp < 0)
		goto free_vp_assist_page;

	/*
	 * Setup the hypercall page and enable hypercalls.
	 * 1. Register the guest ID
	 * 2. Enable the hypercall and register the hypercall page
	 */
	guest_id = generate_guest_id(0, LINUX_VERSION_CODE, 0);
	wrmsrl(HV_X64_MSR_GUEST_OS_ID, guest_id);

	hv_hypercall_pg  = __vmalloc(PAGE_SIZE, GFP_KERNEL, PAGE_KERNEL_RX);
	if (hv_hypercall_pg == NULL) {
		wrmsrl(HV_X64_MSR_GUEST_OS_ID, 0);
		goto remove_cpuhp_state;
	}

	rdmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);
	hypercall_msr.enable = 1;
	hypercall_msr.guest_physical_address = vmalloc_to_pfn(hv_hypercall_pg);
	wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	hv_apic_init();

	x86_init.pci.arch_init = hv_pci_init;

	/* Register Hyper-V specific clocksource */
	hv_init_clocksource();
	return;

remove_cpuhp_state:
	cpuhp_remove_state(cpuhp);
free_vp_assist_page:
	kfree(hv_vp_assist_page);
	hv_vp_assist_page = NULL;
free_vp_index:
	kfree(hv_vp_index);
	hv_vp_index = NULL;
}

/*
 * This routine is called before kexec/kdump, it does the required cleanup.
 */
void hyperv_cleanup(void)
{
	union hv_x64_msr_hypercall_contents hypercall_msr;

	/* Reset our OS id */
	wrmsrl(HV_X64_MSR_GUEST_OS_ID, 0);

	/*
	 * Reset hypercall page reference before reset the page,
	 * let hypercall operations fail safely rather than
	 * panic the kernel for using invalid hypercall page
	 */
	hv_hypercall_pg = NULL;

	/* Reset the hypercall page */
	hypercall_msr.as_uint64 = 0;
	wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	/* Reset the TSC page */
	hypercall_msr.as_uint64 = 0;
	wrmsrl(HV_X64_MSR_REFERENCE_TSC, hypercall_msr.as_uint64);
}
EXPORT_SYMBOL_GPL(hyperv_cleanup);

void hyperv_report_panic(struct pt_regs *regs, long err)
{
	static bool panic_reported;
	u64 guest_id;

	/*
	 * We prefer to report panic on 'die' chain as we have proper
	 * registers to report, but if we miss it (e.g. on BUG()) we need
	 * to report it on 'panic'.
	 */
	if (panic_reported)
		return;
	panic_reported = true;

	rdmsrl(HV_X64_MSR_GUEST_OS_ID, guest_id);

	wrmsrl(HV_X64_MSR_CRASH_P0, err);
	wrmsrl(HV_X64_MSR_CRASH_P1, guest_id);
	wrmsrl(HV_X64_MSR_CRASH_P2, regs->ip);
	wrmsrl(HV_X64_MSR_CRASH_P3, regs->ax);
	wrmsrl(HV_X64_MSR_CRASH_P4, regs->sp);

	/*
	 * Let Hyper-V know there is crash data available
	 */
	wrmsrl(HV_X64_MSR_CRASH_CTL, HV_CRASH_CTL_CRASH_NOTIFY);
}
EXPORT_SYMBOL_GPL(hyperv_report_panic);

/**
 * hyperv_report_panic_msg - report panic message to Hyper-V
 * @pa: physical address of the panic page containing the message
 * @size: size of the message in the page
 */
void hyperv_report_panic_msg(phys_addr_t pa, size_t size)
{
	/*
	 * P3 to contain the physical address of the panic page & P4 to
	 * contain the size of the panic data in that page. Rest of the
	 * registers are no-op when the NOTIFY_MSG flag is set.
	 */
	wrmsrl(HV_X64_MSR_CRASH_P0, 0);
	wrmsrl(HV_X64_MSR_CRASH_P1, 0);
	wrmsrl(HV_X64_MSR_CRASH_P2, 0);
	wrmsrl(HV_X64_MSR_CRASH_P3, pa);
	wrmsrl(HV_X64_MSR_CRASH_P4, size);

	/*
	 * Let Hyper-V know there is crash data available along with
	 * the panic message.
	 */
	wrmsrl(HV_X64_MSR_CRASH_CTL,
	       (HV_CRASH_CTL_CRASH_NOTIFY | HV_CRASH_CTL_CRASH_NOTIFY_MSG));
}
EXPORT_SYMBOL_GPL(hyperv_report_panic_msg);

bool hv_is_hyperv_initialized(void)
{
	union hv_x64_msr_hypercall_contents hypercall_msr;

	/*
	 * Ensure that we're really on Hyper-V, and not a KVM or Xen
	 * emulation of Hyper-V
	 */
	if (x86_hyper_type != X86_HYPER_MS_HYPERV)
		return false;

	/*
	 * Verify that earlier initialization succeeded by checking
	 * that the hypercall page is setup
	 */
	hypercall_msr.as_uint64 = 0;
	rdmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	return hypercall_msr.enable;
}
EXPORT_SYMBOL_GPL(hv_is_hyperv_initialized);
