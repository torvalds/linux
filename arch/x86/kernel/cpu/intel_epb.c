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
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/syscore_ops.h>
#include <linux/pm.h>

#include <asm/cpu_device_id.h>
#include <asm/cpufeature.h>
#include <asm/msr.h>

/**
 * DOC: overview
 *
 * The Performance and Energy Bias Hint (EPB) allows software to specify its
 * preference with respect to the power-performance tradeoffs present in the
 * processor.  Generally, the EPB is expected to be set by user space (directly
 * via sysfs or with the help of the x86_energy_perf_policy tool), but there are
 * two reasons for the kernel to update it.
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
#define MAX_EPB		EPB_MASK

enum energy_perf_value_index {
	EPB_INDEX_PERFORMANCE,
	EPB_INDEX_BALANCE_PERFORMANCE,
	EPB_INDEX_NORMAL,
	EPB_INDEX_BALANCE_POWERSAVE,
	EPB_INDEX_POWERSAVE,
};

static u8 energ_perf_values[] = {
	[EPB_INDEX_PERFORMANCE] = ENERGY_PERF_BIAS_PERFORMANCE,
	[EPB_INDEX_BALANCE_PERFORMANCE] = ENERGY_PERF_BIAS_BALANCE_PERFORMANCE,
	[EPB_INDEX_NORMAL] = ENERGY_PERF_BIAS_NORMAL,
	[EPB_INDEX_BALANCE_POWERSAVE] = ENERGY_PERF_BIAS_BALANCE_POWERSAVE,
	[EPB_INDEX_POWERSAVE] = ENERGY_PERF_BIAS_POWERSAVE,
};

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
			val = energ_perf_values[EPB_INDEX_NORMAL];
			pr_warn_once("ENERGY_PERF_BIAS: Set to 'normal', was 'performance'\n");
		}
	}
	wrmsrl(MSR_IA32_ENERGY_PERF_BIAS, (epb & ~EPB_MASK) | val);
}

static struct syscore_ops intel_epb_syscore_ops = {
	.suspend = intel_epb_save,
	.resume = intel_epb_restore,
};

static const char * const energy_perf_strings[] = {
	[EPB_INDEX_PERFORMANCE] = "performance",
	[EPB_INDEX_BALANCE_PERFORMANCE] = "balance-performance",
	[EPB_INDEX_NORMAL] = "normal",
	[EPB_INDEX_BALANCE_POWERSAVE] = "balance-power",
	[EPB_INDEX_POWERSAVE] = "power",
};

static ssize_t energy_perf_bias_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	unsigned int cpu = dev->id;
	u64 epb;
	int ret;

	ret = rdmsrl_on_cpu(cpu, MSR_IA32_ENERGY_PERF_BIAS, &epb);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%llu\n", epb);
}

static ssize_t energy_perf_bias_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned int cpu = dev->id;
	u64 epb, val;
	int ret;

	ret = __sysfs_match_string(energy_perf_strings,
				   ARRAY_SIZE(energy_perf_strings), buf);
	if (ret >= 0)
		val = energ_perf_values[ret];
	else if (kstrtou64(buf, 0, &val) || val > MAX_EPB)
		return -EINVAL;

	ret = rdmsrl_on_cpu(cpu, MSR_IA32_ENERGY_PERF_BIAS, &epb);
	if (ret < 0)
		return ret;

	ret = wrmsrl_on_cpu(cpu, MSR_IA32_ENERGY_PERF_BIAS,
			    (epb & ~EPB_MASK) | val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RW(energy_perf_bias);

static struct attribute *intel_epb_attrs[] = {
	&dev_attr_energy_perf_bias.attr,
	NULL
};

static const struct attribute_group intel_epb_attr_group = {
	.name = power_group_name,
	.attrs =  intel_epb_attrs
};

static int intel_epb_online(unsigned int cpu)
{
	struct device *cpu_dev = get_cpu_device(cpu);

	intel_epb_restore();
	if (!cpuhp_tasks_frozen)
		sysfs_merge_group(&cpu_dev->kobj, &intel_epb_attr_group);

	return 0;
}

static int intel_epb_offline(unsigned int cpu)
{
	struct device *cpu_dev = get_cpu_device(cpu);

	if (!cpuhp_tasks_frozen)
		sysfs_unmerge_group(&cpu_dev->kobj, &intel_epb_attr_group);

	intel_epb_save();
	return 0;
}

static const struct x86_cpu_id intel_epb_normal[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_L,
				   ENERGY_PERF_BIAS_NORMAL_POWERSAVE),
	X86_MATCH_INTEL_FAM6_MODEL(ALDERLAKE_N,
				   ENERGY_PERF_BIAS_NORMAL_POWERSAVE),
	X86_MATCH_INTEL_FAM6_MODEL(RAPTORLAKE_P,
				   ENERGY_PERF_BIAS_NORMAL_POWERSAVE),
	{}
};

static __init int intel_epb_init(void)
{
	const struct x86_cpu_id *id = x86_match_cpu(intel_epb_normal);
	int ret;

	if (!boot_cpu_has(X86_FEATURE_EPB))
		return -ENODEV;

	if (id)
		energ_perf_values[EPB_INDEX_NORMAL] = id->driver_data;

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
