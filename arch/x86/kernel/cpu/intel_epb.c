// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Performance and Energy Bias Hint support.
 *
 * Copyright (C) 2019 Intel Corporation
 *
 * Author:
 *	Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include <linux/cpuhotplug.h>
#include <linux/kernel.h>
#include <linux/syscore_ops.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>

/**
 * DOC: overview
 *
 * The Performance and Energy Bias Hint (EPB) allows software to specify its
 * preference with respect to the power-performance tradeoffs present in the
 * processor.  Generally, the EPB is expected to be set by user space through
 * the generic MSR interface (with the help of the x86_energy_perf_policy tool),
 * but there are two reasons for the kernel to touch it.
 *
 * First, there are systems where the platform firmware resets the EPB during
 * system-wide transitions from sleep states back into the working state
 * effectively causing the previous EPB updates by user space to be lost.
 * Thus the kernel needs to save the current EPB values for all CPUs during
 * system-wide transitions to sleep states and restore them on the way back to
 * the working state.  That can be achieved by saving EPB for secondary CPUs
 * when they are taken offline during transitions into system sleep states and
 * for the boot CPU in a syscore suspend operation, so that it can be restored
 * for the boot CPU in a syscore resume operation and for the other CPUs when
 * they are brought back online.  However, CPUs that are already offline when
 * a system-wide PM transition is started are not taken offline again, but their
 * EPB values may still be reset by the platform firmware during the transition,
 * so in fact it is necessary to save the EPB of any CPU taken offline and to
 * restore it when the given CPU goes back online at all times.
 *
 * Second, on many systems the initial EPB value coming from the platform
 * firmware is 0 ('performance') and at least on some of them that is because
 * the platform firmware does not initialize EPB at all with the assumption that
 * the OS will do that anyway.  That sometimes is problematic, as it may cause
 * the system battery to drain too fast, for example, so it is better to adjust
 * it on CPU bring-up and if the initial EPB value for a given CPU is 0, the
 * kernel changes it to 6 ('normal').
 */

static DEFINE_PER_CPU(u8, saved_epb);

#define EPB_MASK	0x0fULL
#define EPB_SAVED	0x10ULL

static int intel_epb_save(void)
{
	u64 epb;

	rdmsrl(MSR_IA32_ENERGY_PERF_BIAS, epb);
	/*
	 * Ensure that saved_epb will always be nonzero after this write even if
	 * the EPB value read from the MSR is 0.
	 */
	this_cpu_write(saved_epb, (epb & EPB_MASK) | EPB_SAVED);

	return 0;
}

static void intel_epb_restore(void)
{
	u64 val = this_cpu_read(saved_epb);
	u64 epb;

	rdmsrl(MSR_IA32_ENERGY_PERF_BIAS, epb);
	if (val) {
		val &= EPB_MASK;
	} else {
		/*
		 * Because intel_epb_save() has not run for the current CPU yet,
		 * it is going online for the first time, so if its EPB value is
		 * 0 ('performance') at this point, assume that it has not been
		 * initialized by the platform firmware and set it to 6
		 * ('normal').
		 */
		val = epb & EPB_MASK;
		if (val == ENERGY_PERF_BIAS_PERFORMANCE) {
			val = ENERGY_PERF_BIAS_NORMAL;
			pr_warn_once("ENERGY_PERF_BIAS: Set to 'normal', was 'performance'\n");
		}
	}
	wrmsrl(MSR_IA32_ENERGY_PERF_BIAS, (epb & ~EPB_MASK) | val);
}

static struct syscore_ops intel_epb_syscore_ops = {
	.suspend = intel_epb_save,
	.resume = intel_epb_restore,
};

static int intel_epb_online(unsigned int cpu)
{
	intel_epb_restore();
	return 0;
}

static int intel_epb_offline(unsigned int cpu)
{
	return intel_epb_save();
}

static __init int intel_epb_init(void)
{
	int ret;

	if (!boot_cpu_has(X86_FEATURE_EPB))
		return -ENODEV;

	ret = cpuhp_setup_state(CPUHP_AP_X86_INTEL_EPB_ONLINE,
				"x86/intel/epb:online", intel_epb_online,
				intel_epb_offline);
	if (ret < 0)
		goto err_out_online;

	register_syscore_ops(&intel_epb_syscore_ops);
	return 0;

err_out_online:
	cpuhp_remove_state(CPUHP_AP_X86_INTEL_EPB_ONLINE);
	return ret;
}
subsys_initcall(intel_epb_init);
