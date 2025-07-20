// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Processor P-state Frequency Driver Unit Test
 *
 * Copyright (C) 2022 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 * Author: Meng Li <li.meng@amd.com>
 *
 * The AMD P-State Unit Test is a test module for testing the amd-pstate
 * driver. 1) It can help all users to verify their processor support
 * (SBIOS/Firmware or Hardware). 2) Kernel can have a basic function
 * test to avoid the kernel regression during the update. 3) We can
 * introduce more functional or performance tests to align the result
 * together, it will benefit power and performance scale optimization.
 *
 * This driver implements basic framework with plans to enhance it with
 * additional test cases to improve the depth and coverage of the test.
 *
 * See Documentation/admin-guide/pm/amd-pstate.rst Unit Tests for
 * amd-pstate to get more detail.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/cleanup.h>

#include <acpi/cppc_acpi.h>

#include <asm/msr.h>

#include "amd-pstate.h"


struct amd_pstate_ut_struct {
	const char *name;
	int (*func)(u32 index);
};

/*
 * Kernel module for testing the AMD P-State unit test
 */
static int amd_pstate_ut_acpi_cpc_valid(u32 index);
static int amd_pstate_ut_check_enabled(u32 index);
static int amd_pstate_ut_check_perf(u32 index);
static int amd_pstate_ut_check_freq(u32 index);
static int amd_pstate_ut_check_driver(u32 index);

static struct amd_pstate_ut_struct amd_pstate_ut_cases[] = {
	{"amd_pstate_ut_acpi_cpc_valid",   amd_pstate_ut_acpi_cpc_valid   },
	{"amd_pstate_ut_check_enabled",    amd_pstate_ut_check_enabled    },
	{"amd_pstate_ut_check_perf",       amd_pstate_ut_check_perf       },
	{"amd_pstate_ut_check_freq",       amd_pstate_ut_check_freq       },
	{"amd_pstate_ut_check_driver",	   amd_pstate_ut_check_driver     }
};

static bool get_shared_mem(void)
{
	bool result = false;

	if (!boot_cpu_has(X86_FEATURE_CPPC))
		result = true;

	return result;
}

/*
 * check the _CPC object is present in SBIOS.
 */
static int amd_pstate_ut_acpi_cpc_valid(u32 index)
{
	if (!acpi_cpc_valid()) {
		pr_err("%s the _CPC object is not present in SBIOS!\n", __func__);
		return -EINVAL;
	}

	return 0;
}

/*
 * check if amd pstate is enabled
 */
static int amd_pstate_ut_check_enabled(u32 index)
{
	u64 cppc_enable = 0;
	int ret;

	if (get_shared_mem())
		return 0;

	ret = rdmsrq_safe(MSR_AMD_CPPC_ENABLE, &cppc_enable);
	if (ret) {
		pr_err("%s rdmsrq_safe MSR_AMD_CPPC_ENABLE ret=%d error!\n", __func__, ret);
		return ret;
	}

	if (!cppc_enable) {
		pr_err("%s amd pstate must be enabled!\n", __func__);
		return -EINVAL;
	}

	return 0;
}

/*
 * check if performance values are reasonable.
 * highest_perf >= nominal_perf > lowest_nonlinear_perf > lowest_perf > 0
 */
static int amd_pstate_ut_check_perf(u32 index)
{
	int cpu = 0, ret = 0;
	u32 highest_perf = 0, nominal_perf = 0, lowest_nonlinear_perf = 0, lowest_perf = 0;
	u64 cap1 = 0;
	struct cppc_perf_caps cppc_perf;
	union perf_cached cur_perf;

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy __free(put_cpufreq_policy) = NULL;
		struct amd_cpudata *cpudata;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		cpudata = policy->driver_data;

		if (get_shared_mem()) {
			ret = cppc_get_perf_caps(cpu, &cppc_perf);
			if (ret) {
				pr_err("%s cppc_get_perf_caps ret=%d error!\n", __func__, ret);
				return ret;
			}

			highest_perf = cppc_perf.highest_perf;
			nominal_perf = cppc_perf.nominal_perf;
			lowest_nonlinear_perf = cppc_perf.lowest_nonlinear_perf;
			lowest_perf = cppc_perf.lowest_perf;
		} else {
			ret = rdmsrq_safe_on_cpu(cpu, MSR_AMD_CPPC_CAP1, &cap1);
			if (ret) {
				pr_err("%s read CPPC_CAP1 ret=%d error!\n", __func__, ret);
				return ret;
			}

			highest_perf = FIELD_GET(AMD_CPPC_HIGHEST_PERF_MASK, cap1);
			nominal_perf = FIELD_GET(AMD_CPPC_NOMINAL_PERF_MASK, cap1);
			lowest_nonlinear_perf = FIELD_GET(AMD_CPPC_LOWNONLIN_PERF_MASK, cap1);
			lowest_perf = FIELD_GET(AMD_CPPC_LOWEST_PERF_MASK, cap1);
		}

		cur_perf = READ_ONCE(cpudata->perf);
		if (highest_perf != cur_perf.highest_perf && !cpudata->hw_prefcore) {
			pr_err("%s cpu%d highest=%d %d highest perf doesn't match\n",
				__func__, cpu, highest_perf, cur_perf.highest_perf);
			return -EINVAL;
		}
		if (nominal_perf != cur_perf.nominal_perf ||
		   (lowest_nonlinear_perf != cur_perf.lowest_nonlinear_perf) ||
		   (lowest_perf != cur_perf.lowest_perf)) {
			pr_err("%s cpu%d nominal=%d %d lowest_nonlinear=%d %d lowest=%d %d, they should be equal!\n",
				__func__, cpu, nominal_perf, cur_perf.nominal_perf,
				lowest_nonlinear_perf, cur_perf.lowest_nonlinear_perf,
				lowest_perf, cur_perf.lowest_perf);
			return -EINVAL;
		}

		if (!((highest_perf >= nominal_perf) &&
			(nominal_perf > lowest_nonlinear_perf) &&
			(lowest_nonlinear_perf >= lowest_perf) &&
			(lowest_perf > 0))) {
			pr_err("%s cpu%d highest=%d >= nominal=%d > lowest_nonlinear=%d > lowest=%d > 0, the formula is incorrect!\n",
				__func__, cpu, highest_perf, nominal_perf,
				lowest_nonlinear_perf, lowest_perf);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Check if frequency values are reasonable.
 * max_freq >= nominal_freq > lowest_nonlinear_freq > min_freq > 0
 * check max freq when set support boost mode.
 */
static int amd_pstate_ut_check_freq(u32 index)
{
	int cpu = 0;

	for_each_online_cpu(cpu) {
		struct cpufreq_policy *policy __free(put_cpufreq_policy) = NULL;
		struct amd_cpudata *cpudata;

		policy = cpufreq_cpu_get(cpu);
		if (!policy)
			continue;
		cpudata = policy->driver_data;

		if (!((policy->cpuinfo.max_freq >= cpudata->nominal_freq) &&
			(cpudata->nominal_freq > cpudata->lowest_nonlinear_freq) &&
			(cpudata->lowest_nonlinear_freq >= policy->cpuinfo.min_freq) &&
			(policy->cpuinfo.min_freq > 0))) {
			pr_err("%s cpu%d max=%d >= nominal=%d > lowest_nonlinear=%d > min=%d > 0, the formula is incorrect!\n",
				__func__, cpu, policy->cpuinfo.max_freq, cpudata->nominal_freq,
				cpudata->lowest_nonlinear_freq, policy->cpuinfo.min_freq);
			return -EINVAL;
		}

		if (cpudata->lowest_nonlinear_freq != policy->min) {
			pr_err("%s cpu%d cpudata_lowest_nonlinear_freq=%d policy_min=%d, they should be equal!\n",
				__func__, cpu, cpudata->lowest_nonlinear_freq, policy->min);
			return -EINVAL;
		}

		if (cpudata->boost_supported) {
			if ((policy->max != policy->cpuinfo.max_freq) &&
			    (policy->max != cpudata->nominal_freq)) {
				pr_err("%s cpu%d policy_max=%d should be equal cpu_max=%d or cpu_nominal=%d !\n",
					__func__, cpu, policy->max, policy->cpuinfo.max_freq,
					cpudata->nominal_freq);
				return -EINVAL;
			}
		} else {
			pr_err("%s cpu%d must support boost!\n", __func__, cpu);
			return -EINVAL;
		}
	}

	return 0;
}

static int amd_pstate_set_mode(enum amd_pstate_mode mode)
{
	const char *mode_str = amd_pstate_get_mode_string(mode);

	pr_debug("->setting mode to %s\n", mode_str);

	return amd_pstate_update_status(mode_str, strlen(mode_str));
}

static int amd_pstate_ut_check_driver(u32 index)
{
	enum amd_pstate_mode mode1, mode2 = AMD_PSTATE_DISABLE;
	enum amd_pstate_mode orig_mode = amd_pstate_get_status();
	int ret;

	for (mode1 = AMD_PSTATE_DISABLE; mode1 < AMD_PSTATE_MAX; mode1++) {
		ret = amd_pstate_set_mode(mode1);
		if (ret)
			return ret;
		for (mode2 = AMD_PSTATE_DISABLE; mode2 < AMD_PSTATE_MAX; mode2++) {
			if (mode1 == mode2)
				continue;
			ret = amd_pstate_set_mode(mode2);
			if (ret)
				goto out;
		}
	}

out:
	if (ret)
		pr_warn("%s: failed to update status for %s->%s: %d\n", __func__,
			amd_pstate_get_mode_string(mode1),
			amd_pstate_get_mode_string(mode2), ret);

	amd_pstate_set_mode(orig_mode);
	return ret;
}

static int __init amd_pstate_ut_init(void)
{
	u32 i = 0, arr_size = ARRAY_SIZE(amd_pstate_ut_cases);

	for (i = 0; i < arr_size; i++) {
		int ret = amd_pstate_ut_cases[i].func(i);

		if (ret)
			pr_err("%-4d %-20s\t fail: %d!\n", i+1, amd_pstate_ut_cases[i].name, ret);
		else
			pr_info("%-4d %-20s\t success!\n", i+1, amd_pstate_ut_cases[i].name);
	}

	return 0;
}

static void __exit amd_pstate_ut_exit(void)
{
}

module_init(amd_pstate_ut_init);
module_exit(amd_pstate_ut_exit);

MODULE_AUTHOR("Meng Li <li.meng@amd.com>");
MODULE_DESCRIPTION("AMD P-state driver Test module");
MODULE_LICENSE("GPL");
