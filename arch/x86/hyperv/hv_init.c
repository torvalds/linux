/*
 * X86 specific Hyper-V initialization code.
 *
 * Copyright (C) 2016, Microsoft, Inc.
 *
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 */

#include <linux/types.h>
#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/hypervisor.h>
#include <asm/hyperv.h>
#include <asm/mshyperv.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/clockchips.h>
#include <linux/hyperv.h>
#include <linux/slab.h>
#include <linux/cpuhotplug.h>

#ifdef CONFIG_HYPERV_TSCPAGE

static struct ms_hyperv_tsc_page *tsc_pg;

struct ms_hyperv_tsc_page *hv_get_tsc_page(void)
{
	return tsc_pg;
}
EXPORT_SYMBOL_GPL(hv_get_tsc_page);

static u64 read_hv_clock_tsc(struct clocksource *arg)
{
	u64 current_tick = hv_read_tsc_page(tsc_pg);

	if (current_tick == U64_MAX)
		rdmsrl(HV_X64_MSR_TIME_REF_COUNT, current_tick);

	return current_tick;
}

static struct clocksource hyperv_cs_tsc = {
		.name		= "hyperv_clocksource_tsc_page",
		.rating		= 400,
		.read		= read_hv_clock_tsc,
		.mask		= CLOCKSOURCE_MASK(64),
		.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};
#endif

static u64 read_hv_clock_msr(struct clocksource *arg)
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

static struct clocksource hyperv_cs_msr = {
	.name		= "hyperv_clocksource_msr",
	.rating		= 400,
	.read		= read_hv_clock_msr,
	.mask		= CLOCKSOURCE_MASK(64),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

void *hv_hypercall_pg;
EXPORT_SYMBOL_GPL(hv_hypercall_pg);
struct clocksource *hyperv_cs;
EXPORT_SYMBOL_GPL(hyperv_cs);

u32 *hv_vp_index;
EXPORT_SYMBOL_GPL(hv_vp_index);

u32 hv_max_vp_index;

static int hv_cpu_init(unsigned int cpu)
{
	u64 msr_vp_index;

	hv_get_vp_index(msr_vp_index);

	hv_vp_index[smp_processor_id()] = msr_vp_index;

	if (msr_vp_index > hv_max_vp_index)
		hv_max_vp_index = msr_vp_index;

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

/*
 * This function is to be invoked early in the boot sequence after the
 * hypervisor has been detected.
 *
 * 1. Setup the hypercall page.
 * 2. Register Hyper-V specific clocksource.
 */
void hyperv_init(void)
{
	u64 guest_id, required_msrs;
	union hv_x64_msr_hypercall_contents hypercall_msr;

	if (x86_hyper_type != X86_HYPER_MS_HYPERV)
		return;

	/* Absolutely required MSRs */
	required_msrs = HV_X64_MSR_HYPERCALL_AVAILABLE |
		HV_X64_MSR_VP_INDEX_AVAILABLE;

	if ((ms_hyperv.features & required_msrs) != required_msrs)
		return;

	/* Allocate percpu VP index */
	hv_vp_index = kmalloc_array(num_possible_cpus(), sizeof(*hv_vp_index),
				    GFP_KERNEL);
	if (!hv_vp_index)
		return;

	if (cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "x86/hyperv_init:online",
			      hv_cpu_init, hv_cpu_die) < 0)
		goto free_vp_index;

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
		goto free_vp_index;
	}

	rdmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);
	hypercall_msr.enable = 1;
	hypercall_msr.guest_physical_address = vmalloc_to_pfn(hv_hypercall_pg);
	wrmsrl(HV_X64_MSR_HYPERCALL, hypercall_msr.as_uint64);

	hyper_alloc_mmu();

	/*
	 * Register Hyper-V specific clocksource.
	 */
#ifdef CONFIG_HYPERV_TSCPAGE
	if (ms_hyperv.features & HV_X64_MSR_REFERENCE_TSC_AVAILABLE) {
		union hv_x64_msr_hypercall_contents tsc_msr;

		tsc_pg = __vmalloc(PAGE_SIZE, GFP_KERNEL, PAGE_KERNEL);
		if (!tsc_pg)
			goto register_msr_cs;

		hyperv_cs = &hyperv_cs_tsc;

		rdmsrl(HV_X64_MSR_REFERENCE_TSC, tsc_msr.as_uint64);

		tsc_msr.enable = 1;
		tsc_msr.guest_physical_address = vmalloc_to_pfn(tsc_pg);

		wrmsrl(HV_X64_MSR_REFERENCE_TSC, tsc_msr.as_uint64);

		hyperv_cs_tsc.archdata.vclock_mode = VCLOCK_HVCLOCK;

		clocksource_register_hz(&hyperv_cs_tsc, NSEC_PER_SEC/100);
		return;
	}
register_msr_cs:
#endif
	/*
	 * For 32 bit guests just use the MSR based mechanism for reading
	 * the partition counter.
	 */

	hyperv_cs = &hyperv_cs_msr;
	if (ms_hyperv.features & HV_X64_MSR_TIME_REF_COUNT_AVAILABLE)
		clocksource_register_hz(&hyperv_cs_msr, NSEC_PER_SEC/100);

	return;

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
