// SPDX-License-Identifier: GPL-2.0-only
/*
 * cppc.c: CPPC Interface for x86
 * Copyright (c) 2016, Intel Corporation.
 */

#include <acpi/cppc_acpi.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/topology.h>

/* Refer to drivers/acpi/cppc_acpi.c for the description of functions */

bool cpc_supported_by_cpu(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
	case X86_VENDOR_HYGON:
		if (boot_cpu_data.x86 == 0x19 && ((boot_cpu_data.x86_model <= 0x0f) ||
		    (boot_cpu_data.x86_model >= 0x20 && boot_cpu_data.x86_model <= 0x2f)))
			return true;
		else if (boot_cpu_data.x86 == 0x17 &&
			 boot_cpu_data.x86_model >= 0x70 && boot_cpu_data.x86_model <= 0x7f)
			return true;
		return boot_cpu_has(X86_FEATURE_CPPC);
	}
	return false;
}

bool cpc_ffh_supported(void)
{
	return true;
}

int cpc_read_ffh(int cpunum, struct cpc_reg *reg, u64 *val)
{
	int err;

	err = rdmsrl_safe_on_cpu(cpunum, reg->address, val);
	if (!err) {
		u64 mask = GENMASK_ULL(reg->bit_offset + reg->bit_width - 1,
				       reg->bit_offset);

		*val &= mask;
		*val >>= reg->bit_offset;
	}
	return err;
}

int cpc_write_ffh(int cpunum, struct cpc_reg *reg, u64 val)
{
	u64 rd_val;
	int err;

	err = rdmsrl_safe_on_cpu(cpunum, reg->address, &rd_val);
	if (!err) {
		u64 mask = GENMASK_ULL(reg->bit_offset + reg->bit_width - 1,
				       reg->bit_offset);

		val <<= reg->bit_offset;
		val &= mask;
		rd_val &= ~mask;
		rd_val |= val;
		err = wrmsrl_safe_on_cpu(cpunum, reg->address, rd_val);
	}
	return err;
}

static void amd_set_max_freq_ratio(void)
{
	struct cppc_perf_caps perf_caps;
	u64 highest_perf, nominal_perf;
	u64 perf_ratio;
	int rc;

	rc = cppc_get_perf_caps(0, &perf_caps);
	if (rc) {
		pr_debug("Could not retrieve perf counters (%d)\n", rc);
		return;
	}

	highest_perf = amd_get_highest_perf();
	nominal_perf = perf_caps.nominal_perf;

	if (!highest_perf || !nominal_perf) {
		pr_debug("Could not retrieve highest or nominal performance\n");
		return;
	}

	perf_ratio = div_u64(highest_perf * SCHED_CAPACITY_SCALE, nominal_perf);
	/* midpoint between max_boost and max_P */
	perf_ratio = (perf_ratio + SCHED_CAPACITY_SCALE) >> 1;
	if (!perf_ratio) {
		pr_debug("Non-zero highest/nominal perf values led to a 0 ratio\n");
		return;
	}

	freq_invariance_set_perf_ratio(perf_ratio, false);
}

static DEFINE_MUTEX(freq_invariance_lock);

void init_freq_invariance_cppc(void)
{
	static bool init_done;

	if (!cpu_feature_enabled(X86_FEATURE_APERFMPERF))
		return;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return;

	mutex_lock(&freq_invariance_lock);
	if (!init_done)
		amd_set_max_freq_ratio();
	init_done = true;
	mutex_unlock(&freq_invariance_lock);
}
