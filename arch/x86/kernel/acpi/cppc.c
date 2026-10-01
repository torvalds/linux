// SPDX-License-Identifier: GPL-2.0-only
/*
 * cppc.c: CPPC Interface for x86
 * Copyright (c) 2016, Intel Corporation.
 */

#include <linux/bitfield.h>

#include <acpi/cppc_acpi.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/topology.h>
#include <asm/cpu_device_id.h>

#define CPPC_HIGHEST_PERF_PERFORMANCE	196
#define CPPC_HIGHEST_PERF_PREFCORE	166

enum amd_pref_core {
	AMD_PREF_CORE_UNKNOWN = 0,
	AMD_PREF_CORE_SUPPORTED,
	AMD_PREF_CORE_UNSUPPORTED,
};
static enum amd_pref_core amd_pref_core_detected;
static u64 boost_numerator;

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
			 boot_cpu_data.x86_model >= 0x30 && boot_cpu_data.x86_model <= 0x7f)
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

	err = rdmsrq_safe_on_cpu(cpunum, reg->address, val);
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

	err = rdmsrq_safe_on_cpu(cpunum, reg->address, &rd_val);
	if (!err) {
		u64 mask = GENMASK_ULL(reg->bit_offset + reg->bit_width - 1,
				       reg->bit_offset);

		val <<= reg->bit_offset;
		val &= mask;
		rd_val &= ~mask;
		rd_val |= val;
		err = wrmsrq_safe_on_cpu(cpunum, reg->address, rd_val);
	}
	return err;
}

static void amd_set_max_freq_ratio(void)
{
	u64 numerator, denominator;
	u64 perf_ratio;
	int rc;

	rc = amd_get_boost_ratio(0, &numerator, &denominator);
	if (rc) {
		pr_debug("Could not retrieve boost ratio (%d)\n", rc);
		return;
	}

	/* midpoint between max_boost and max_P */
	perf_ratio = (div_u64(numerator * SCHED_CAPACITY_SCALE, denominator) + SCHED_CAPACITY_SCALE) >> 1;

	freq_invariance_set_perf_ratio(perf_ratio, false);
}

static DEFINE_MUTEX(freq_invariance_lock);

static inline void init_freq_invariance_cppc(void)
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

void acpi_processor_init_invariance_cppc(void)
{
	init_freq_invariance_cppc();
}

/*
 * Get the highest performance register value.
 * @cpu: CPU from which to get highest performance.
 * @highest_perf: Return address for highest performance value.
 *
 * Return: 0 for success, negative error code otherwise.
 */
int amd_get_highest_perf(unsigned int cpu, u32 *highest_perf)
{
	u64 val;
	int ret;

	if (cpu_feature_enabled(X86_FEATURE_CPPC)) {
		ret = rdmsrq_safe_on_cpu(cpu, MSR_AMD_CPPC_CAP1, &val);
		if (ret)
			goto out;

		val = FIELD_GET(AMD_CPPC_HIGHEST_PERF_MASK, val);
	} else {
		ret = cppc_get_highest_perf(cpu, &val);
		if (ret)
			goto out;
	}

	WRITE_ONCE(*highest_perf, (u32)val);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(amd_get_highest_perf);

/**
 * amd_detect_prefcore: Detect if CPUs in the system support preferred cores
 * @detected: Output variable for the result of the detection.
 *
 * Determine whether CPUs in the system support preferred cores. On systems
 * that support preferred cores, different highest perf values will be found
 * on different cores. On other systems, the highest perf value will be the
 * same on all cores.
 *
 * The result of the detection will be stored in the 'detected' parameter.
 *
 * Return: 0 for success, negative error code otherwise
 */
int amd_detect_prefcore(bool *detected)
{
	int cpu, count = 0;
	u64 highest_perf[2] = {0};

	if (WARN_ON(!detected))
		return -EINVAL;

	switch (amd_pref_core_detected) {
	case AMD_PREF_CORE_SUPPORTED:
		*detected = true;
		return 0;
	case AMD_PREF_CORE_UNSUPPORTED:
		*detected = false;
		return 0;
	default:
		break;
	}

	for_each_online_cpu(cpu) {
		u32 tmp;
		int ret;

		ret = amd_get_highest_perf(cpu, &tmp);
		if (ret)
			return ret;

		if (!count || (count == 1 && tmp != highest_perf[0]))
			highest_perf[count++] = tmp;

		if (count == 2)
			break;
	}

	*detected = (count == 2);
	boost_numerator = highest_perf[0];

	amd_pref_core_detected = *detected ? AMD_PREF_CORE_SUPPORTED :
					     AMD_PREF_CORE_UNSUPPORTED;

	pr_debug("AMD CPPC preferred core is %ssupported (highest perf: 0x%llx)\n",
		 *detected ? "" : "un", highest_perf[0]);

	return 0;
}
EXPORT_SYMBOL_GPL(amd_detect_prefcore);

/**
 * amd_get_effective_highest_perf: Get the effective highest performance value
 * @cpu: CPU to get highest performance for.
 *
 * Get the effective highest performance value for a CPU, accounting for
 * preferred cores and heterogeneous topologies. On systems with preferred
 * cores, this may be a hardcoded value. On heterogeneous systems, this
 * may be a per-CPU value. On other systems, this is the shared highest
 * performance value.
 *
 * Return: Effective highest performance value, or negative error code.
 */
int amd_get_effective_highest_perf(unsigned int cpu)
{
	bool prefcore;
	int ret;
	u32 tmp;

	ret = amd_detect_prefcore(&prefcore);
	if (ret < 0)
		return ret;

	/* without preferred cores, return the highest perf register value */
	if (!prefcore)
		return boost_numerator;

	/*
	 * For AMD CPUs with Family ID 19H and Model ID range 0x70 to 0x7f,
	 * the highest performance level is set to 196.
	 * https://bugzilla.kernel.org/show_bug.cgi?id=218759
	 */
	if (cpu_feature_enabled(X86_FEATURE_ZEN4)) {
		switch (boot_cpu_data.x86_model) {
		case 0x70 ... 0x7f:
			return CPPC_HIGHEST_PERF_PERFORMANCE;
		default:
			break;
		}
	}

	/* detect if running on heterogeneous design */
	if (cpu_feature_enabled(X86_FEATURE_AMD_HTR_CORES)) {
		if (cpu_feature_enabled(X86_FEATURE_ZEN5) &&
		    cpu_data(cpu).topo.cpu_type == TOPO_CPU_TYPE_PERFORMANCE)
			return CPPC_HIGHEST_PERF_PERFORMANCE;

		/* Zen 5 efficiency, and Zen 6+ */
		ret = amd_get_highest_perf(cpu, &tmp);
		if (ret < 0)
			return ret;

		return tmp;
	}

	return CPPC_HIGHEST_PERF_PREFCORE;
}
EXPORT_SYMBOL_GPL(amd_get_effective_highest_perf);

struct amd_max_freq {
	u32 performance;
	u32 efficiency;
	u32 low_power;
};

static const struct amd_max_freq max_freq_zen6_client = {
	.performance = 5025,
	.efficiency = 3524,
	.low_power = 2399,
};

int amd_get_max_frequency(unsigned int cpu)
{
	enum x86_topology_cpu_type type = cpu_data(cpu).topo.cpu_type;
	const struct amd_max_freq *max_freq = NULL;

	if (cpu_feature_enabled(X86_FEATURE_ZEN6) &&
	    cpu_feature_enabled(X86_FEATURE_AMD_HTR_CORES))
		max_freq = &max_freq_zen6_client;

	if (!max_freq)
		return 0;

	switch (type) {
	case TOPO_CPU_TYPE_PERFORMANCE:
		return max_freq->performance;
	case TOPO_CPU_TYPE_EFFICIENCY:
		return max_freq->efficiency;
	case TOPO_CPU_TYPE_LOW_POWER:
		return max_freq->low_power;
	default:
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(amd_get_max_frequency);

/**
 * amd_get_boost_ratio: Get numerator and denominator for boost ratio
 * @cpu: CPU to get the boost ratio for.
 * @numerator: Output variable for numerator.
 * @denominator: Output variable for denominator.
 *
 * Get the numerator and denominator for calculating the boost ratio.
 *
 * Return: 0 for success, negative error code otherwise.
 */
int amd_get_boost_ratio(unsigned int cpu, u64 *numerator, u64 *denominator)
{
	struct cppc_perf_caps perf_caps;
	int max_frequency;
	int ret;

	ret = cppc_get_perf_caps(cpu, &perf_caps);
	if (ret)
		return ret;

	max_frequency = amd_get_max_frequency(cpu);
	/* Use frequency values if available */
	if (max_frequency && perf_caps.nominal_freq) {
		*numerator = max_frequency;
		*denominator = perf_caps.nominal_freq;
		return 0;
	}

	/* Fall back to performance values */
	ret = amd_get_effective_highest_perf(cpu);
	if (ret < 0)
		return ret;

	*numerator = ret;

	*denominator = perf_caps.nominal_perf;
	if (!*denominator)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL_GPL(amd_get_boost_ratio);
