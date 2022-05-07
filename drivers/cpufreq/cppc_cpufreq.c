// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPPC (Collaborative Processor Performance Control) driver for
 * interfacing with the CPUfreq layer and governors. See
 * cppc_acpi.c for CPPC specific methods.
 *
 * (C) Copyright 2014, 2015 Linaro Ltd.
 * Author: Ashwin Chaugule <ashwin.chaugule@linaro.org>
 */

#define pr_fmt(fmt)	"CPPC Cpufreq:"	fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/dmi.h>
#include <linux/time.h>
#include <linux/vmalloc.h>

#include <asm/unaligned.h>

#include <acpi/cppc_acpi.h>

/* Minimum struct length needed for the DMI processor entry we want */
#define DMI_ENTRY_PROCESSOR_MIN_LENGTH	48

/* Offest in the DMI processor structure for the max frequency */
#define DMI_PROCESSOR_MAX_SPEED  0x14

/*
 * These structs contain information parsed from per CPU
 * ACPI _CPC structures.
 * e.g. For each CPU the highest, lowest supported
 * performance capabilities, desired performance level
 * requested etc.
 */
static struct cppc_cpudata **all_cpu_data;
static bool boost_supported;

struct cppc_workaround_oem_info {
	char oem_id[ACPI_OEM_ID_SIZE + 1];
	char oem_table_id[ACPI_OEM_TABLE_ID_SIZE + 1];
	u32 oem_revision;
};

static struct cppc_workaround_oem_info wa_info[] = {
	{
		.oem_id		= "HISI  ",
		.oem_table_id	= "HIP07   ",
		.oem_revision	= 0,
	}, {
		.oem_id		= "HISI  ",
		.oem_table_id	= "HIP08   ",
		.oem_revision	= 0,
	}
};

/* Callback function used to retrieve the max frequency from DMI */
static void cppc_find_dmi_mhz(const struct dmi_header *dm, void *private)
{
	const u8 *dmi_data = (const u8 *)dm;
	u16 *mhz = (u16 *)private;

	if (dm->type == DMI_ENTRY_PROCESSOR &&
	    dm->length >= DMI_ENTRY_PROCESSOR_MIN_LENGTH) {
		u16 val = (u16)get_unaligned((const u16 *)
				(dmi_data + DMI_PROCESSOR_MAX_SPEED));
		*mhz = val > *mhz ? val : *mhz;
	}
}

/* Look up the max frequency in DMI */
static u64 cppc_get_dmi_max_khz(void)
{
	u16 mhz = 0;

	dmi_walk(cppc_find_dmi_mhz, &mhz);

	/*
	 * Real stupid fallback value, just in case there is no
	 * actual value set.
	 */
	mhz = mhz ? mhz : 1;

	return (1000 * mhz);
}

/*
 * If CPPC lowest_freq and nominal_freq registers are exposed then we can
 * use them to convert perf to freq and vice versa
 *
 * If the perf/freq point lies between Nominal and Lowest, we can treat
 * (Low perf, Low freq) and (Nom Perf, Nom freq) as 2D co-ordinates of a line
 * and extrapolate the rest
 * For perf/freq > Nominal, we use the ratio perf:freq at Nominal for conversion
 */
static unsigned int cppc_cpufreq_perf_to_khz(struct cppc_cpudata *cpu,
					unsigned int perf)
{
	static u64 max_khz;
	struct cppc_perf_caps *caps = &cpu->perf_caps;
	u64 mul, div;

	if (caps->lowest_freq && caps->nominal_freq) {
		if (perf >= caps->nominal_perf) {
			mul = caps->nominal_freq;
			div = caps->nominal_perf;
		} else {
			mul = caps->nominal_freq - caps->lowest_freq;
			div = caps->nominal_perf - caps->lowest_perf;
		}
	} else {
		if (!max_khz)
			max_khz = cppc_get_dmi_max_khz();
		mul = max_khz;
		div = caps->highest_perf;
	}
	return (u64)perf * mul / div;
}

static unsigned int cppc_cpufreq_khz_to_perf(struct cppc_cpudata *cpu,
					unsigned int freq)
{
	static u64 max_khz;
	struct cppc_perf_caps *caps = &cpu->perf_caps;
	u64  mul, div;

	if (caps->lowest_freq && caps->nominal_freq) {
		if (freq >= caps->nominal_freq) {
			mul = caps->nominal_perf;
			div = caps->nominal_freq;
		} else {
			mul = caps->lowest_perf;
			div = caps->lowest_freq;
		}
	} else {
		if (!max_khz)
			max_khz = cppc_get_dmi_max_khz();
		mul = caps->highest_perf;
		div = max_khz;
	}

	return (u64)freq * mul / div;
}

static int cppc_cpufreq_set_target(struct cpufreq_policy *policy,
		unsigned int target_freq,
		unsigned int relation)
{
	struct cppc_cpudata *cpu;
	struct cpufreq_freqs freqs;
	u32 desired_perf;
	int ret = 0;

	cpu = all_cpu_data[policy->cpu];

	desired_perf = cppc_cpufreq_khz_to_perf(cpu, target_freq);
	/* Return if it is exactly the same perf */
	if (desired_perf == cpu->perf_ctrls.desired_perf)
		return ret;

	cpu->perf_ctrls.desired_perf = desired_perf;
	freqs.old = policy->cur;
	freqs.new = target_freq;

	cpufreq_freq_transition_begin(policy, &freqs);
	ret = cppc_set_perf(cpu->cpu, &cpu->perf_ctrls);
	cpufreq_freq_transition_end(policy, &freqs, ret != 0);

	if (ret)
		pr_debug("Failed to set target on CPU:%d. ret:%d\n",
				cpu->cpu, ret);

	return ret;
}

static int cppc_verify_policy(struct cpufreq_policy_data *policy)
{
	cpufreq_verify_within_cpu_limits(policy);
	return 0;
}

static void cppc_cpufreq_stop_cpu(struct cpufreq_policy *policy)
{
	int cpu_num = policy->cpu;
	struct cppc_cpudata *cpu = all_cpu_data[cpu_num];
	int ret;

	cpu->perf_ctrls.desired_perf = cpu->perf_caps.lowest_perf;

	ret = cppc_set_perf(cpu_num, &cpu->perf_ctrls);
	if (ret)
		pr_debug("Err setting perf value:%d on CPU:%d. ret:%d\n",
				cpu->perf_caps.lowest_perf, cpu_num, ret);
}

/*
 * The PCC subspace describes the rate at which platform can accept commands
 * on the shared PCC channel (including READs which do not count towards freq
 * trasition requests), so ideally we need to use the PCC values as a fallback
 * if we don't have a platform specific transition_delay_us
 */
#ifdef CONFIG_ARM64
#include <asm/cputype.h>

static unsigned int cppc_cpufreq_get_transition_delay_us(int cpu)
{
	unsigned long implementor = read_cpuid_implementor();
	unsigned long part_num = read_cpuid_part_number();
	unsigned int delay_us = 0;

	switch (implementor) {
	case ARM_CPU_IMP_QCOM:
		switch (part_num) {
		case QCOM_CPU_PART_FALKOR_V1:
		case QCOM_CPU_PART_FALKOR:
			delay_us = 10000;
			break;
		default:
			delay_us = cppc_get_transition_latency(cpu) / NSEC_PER_USEC;
			break;
		}
		break;
	default:
		delay_us = cppc_get_transition_latency(cpu) / NSEC_PER_USEC;
		break;
	}

	return delay_us;
}

#else

static unsigned int cppc_cpufreq_get_transition_delay_us(int cpu)
{
	return cppc_get_transition_latency(cpu) / NSEC_PER_USEC;
}
#endif

static int cppc_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct cppc_cpudata *cpu;
	unsigned int cpu_num = policy->cpu;
	int ret = 0;

	cpu = all_cpu_data[policy->cpu];

	cpu->cpu = cpu_num;
	ret = cppc_get_perf_caps(policy->cpu, &cpu->perf_caps);

	if (ret) {
		pr_debug("Err reading CPU%d perf capabilities. ret:%d\n",
				cpu_num, ret);
		return ret;
	}

	/* Convert the lowest and nominal freq from MHz to KHz */
	cpu->perf_caps.lowest_freq *= 1000;
	cpu->perf_caps.nominal_freq *= 1000;

	/*
	 * Set min to lowest nonlinear perf to avoid any efficiency penalty (see
	 * Section 8.4.7.1.1.5 of ACPI 6.1 spec)
	 */
	policy->min = cppc_cpufreq_perf_to_khz(cpu, cpu->perf_caps.lowest_nonlinear_perf);
	policy->max = cppc_cpufreq_perf_to_khz(cpu, cpu->perf_caps.nominal_perf);

	/*
	 * Set cpuinfo.min_freq to Lowest to make the full range of performance
	 * available if userspace wants to use any perf between lowest & lowest
	 * nonlinear perf
	 */
	policy->cpuinfo.min_freq = cppc_cpufreq_perf_to_khz(cpu, cpu->perf_caps.lowest_perf);
	policy->cpuinfo.max_freq = cppc_cpufreq_perf_to_khz(cpu, cpu->perf_caps.nominal_perf);

	policy->transition_delay_us = cppc_cpufreq_get_transition_delay_us(cpu_num);
	policy->shared_type = cpu->shared_type;

	if (policy->shared_type == CPUFREQ_SHARED_TYPE_ANY) {
		int i;

		cpumask_copy(policy->cpus, cpu->shared_cpu_map);

		for_each_cpu(i, policy->cpus) {
			if (unlikely(i == policy->cpu))
				continue;

			memcpy(&all_cpu_data[i]->perf_caps, &cpu->perf_caps,
			       sizeof(cpu->perf_caps));
		}
	} else if (policy->shared_type == CPUFREQ_SHARED_TYPE_ALL) {
		/* Support only SW_ANY for now. */
		pr_debug("Unsupported CPU co-ord type\n");
		return -EFAULT;
	}

	cpu->cur_policy = policy;

	/*
	 * If 'highest_perf' is greater than 'nominal_perf', we assume CPU Boost
	 * is supported.
	 */
	if (cpu->perf_caps.highest_perf > cpu->perf_caps.nominal_perf)
		boost_supported = true;

	/* Set policy->cur to max now. The governors will adjust later. */
	policy->cur = cppc_cpufreq_perf_to_khz(cpu,
					cpu->perf_caps.highest_perf);
	cpu->perf_ctrls.desired_perf = cpu->perf_caps.highest_perf;

	ret = cppc_set_perf(cpu_num, &cpu->perf_ctrls);
	if (ret)
		pr_debug("Err setting perf value:%d on CPU:%d. ret:%d\n",
				cpu->perf_caps.highest_perf, cpu_num, ret);

	return ret;
}

static inline u64 get_delta(u64 t1, u64 t0)
{
	if (t1 > t0 || t0 > ~(u32)0)
		return t1 - t0;

	return (u32)t1 - (u32)t0;
}

static int cppc_get_rate_from_fbctrs(struct cppc_cpudata *cpu,
				     struct cppc_perf_fb_ctrs fb_ctrs_t0,
				     struct cppc_perf_fb_ctrs fb_ctrs_t1)
{
	u64 delta_reference, delta_delivered;
	u64 reference_perf, delivered_perf;

	reference_perf = fb_ctrs_t0.reference_perf;

	delta_reference = get_delta(fb_ctrs_t1.reference,
				    fb_ctrs_t0.reference);
	delta_delivered = get_delta(fb_ctrs_t1.delivered,
				    fb_ctrs_t0.delivered);

	/* Check to avoid divide-by zero */
	if (delta_reference || delta_delivered)
		delivered_perf = (reference_perf * delta_delivered) /
					delta_reference;
	else
		delivered_perf = cpu->perf_ctrls.desired_perf;

	return cppc_cpufreq_perf_to_khz(cpu, delivered_perf);
}

static unsigned int cppc_cpufreq_get_rate(unsigned int cpunum)
{
	struct cppc_perf_fb_ctrs fb_ctrs_t0 = {0}, fb_ctrs_t1 = {0};
	struct cppc_cpudata *cpu = all_cpu_data[cpunum];
	int ret;

	ret = cppc_get_perf_ctrs(cpunum, &fb_ctrs_t0);
	if (ret)
		return ret;

	udelay(2); /* 2usec delay between sampling */

	ret = cppc_get_perf_ctrs(cpunum, &fb_ctrs_t1);
	if (ret)
		return ret;

	return cppc_get_rate_from_fbctrs(cpu, fb_ctrs_t0, fb_ctrs_t1);
}

static int cppc_cpufreq_set_boost(struct cpufreq_policy *policy, int state)
{
	struct cppc_cpudata *cpudata;
	int ret;

	if (!boost_supported) {
		pr_err("BOOST not supported by CPU or firmware\n");
		return -EINVAL;
	}

	cpudata = all_cpu_data[policy->cpu];
	if (state)
		policy->max = cppc_cpufreq_perf_to_khz(cpudata,
					cpudata->perf_caps.highest_perf);
	else
		policy->max = cppc_cpufreq_perf_to_khz(cpudata,
					cpudata->perf_caps.nominal_perf);
	policy->cpuinfo.max_freq = policy->max;

	ret = freq_qos_update_request(policy->max_freq_req, policy->max);
	if (ret < 0)
		return ret;

	return 0;
}

static struct cpufreq_driver cppc_cpufreq_driver = {
	.flags = CPUFREQ_CONST_LOOPS,
	.verify = cppc_verify_policy,
	.target = cppc_cpufreq_set_target,
	.get = cppc_cpufreq_get_rate,
	.init = cppc_cpufreq_cpu_init,
	.stop_cpu = cppc_cpufreq_stop_cpu,
	.set_boost = cppc_cpufreq_set_boost,
	.name = "cppc_cpufreq",
};

/*
 * HISI platform does not support delivered performance counter and
 * reference performance counter. It can calculate the performance using the
 * platform specific mechanism. We reuse the desired performance register to
 * store the real performance calculated by the platform.
 */
static unsigned int hisi_cppc_cpufreq_get_rate(unsigned int cpunum)
{
	struct cppc_cpudata *cpudata = all_cpu_data[cpunum];
	u64 desired_perf;
	int ret;

	ret = cppc_get_desired_perf(cpunum, &desired_perf);
	if (ret < 0)
		return -EIO;

	return cppc_cpufreq_perf_to_khz(cpudata, desired_perf);
}

static void cppc_check_hisi_workaround(void)
{
	struct acpi_table_header *tbl;
	acpi_status status = AE_OK;
	int i;

	status = acpi_get_table(ACPI_SIG_PCCT, 0, &tbl);
	if (ACPI_FAILURE(status) || !tbl)
		return;

	for (i = 0; i < ARRAY_SIZE(wa_info); i++) {
		if (!memcmp(wa_info[i].oem_id, tbl->oem_id, ACPI_OEM_ID_SIZE) &&
		    !memcmp(wa_info[i].oem_table_id, tbl->oem_table_id, ACPI_OEM_TABLE_ID_SIZE) &&
		    wa_info[i].oem_revision == tbl->oem_revision) {
			/* Overwrite the get() callback */
			cppc_cpufreq_driver.get = hisi_cppc_cpufreq_get_rate;
			break;
		}
	}

	acpi_put_table(tbl);
}

static int __init cppc_cpufreq_init(void)
{
	int i, ret = 0;
	struct cppc_cpudata *cpu;

	if (acpi_disabled)
		return -ENODEV;

	all_cpu_data = kcalloc(num_possible_cpus(), sizeof(void *),
			       GFP_KERNEL);
	if (!all_cpu_data)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		all_cpu_data[i] = kzalloc(sizeof(struct cppc_cpudata), GFP_KERNEL);
		if (!all_cpu_data[i])
			goto out;

		cpu = all_cpu_data[i];
		if (!zalloc_cpumask_var(&cpu->shared_cpu_map, GFP_KERNEL))
			goto out;
	}

	ret = acpi_get_psd_map(all_cpu_data);
	if (ret) {
		pr_debug("Error parsing PSD data. Aborting cpufreq registration.\n");
		goto out;
	}

	cppc_check_hisi_workaround();

	ret = cpufreq_register_driver(&cppc_cpufreq_driver);
	if (ret)
		goto out;

	return ret;

out:
	for_each_possible_cpu(i) {
		cpu = all_cpu_data[i];
		if (!cpu)
			break;
		free_cpumask_var(cpu->shared_cpu_map);
		kfree(cpu);
	}

	kfree(all_cpu_data);
	return -ENODEV;
}

static void __exit cppc_cpufreq_exit(void)
{
	struct cppc_cpudata *cpu;
	int i;

	cpufreq_unregister_driver(&cppc_cpufreq_driver);

	for_each_possible_cpu(i) {
		cpu = all_cpu_data[i];
		free_cpumask_var(cpu->shared_cpu_map);
		kfree(cpu);
	}

	kfree(all_cpu_data);
}

module_exit(cppc_cpufreq_exit);
MODULE_AUTHOR("Ashwin Chaugule");
MODULE_DESCRIPTION("CPUFreq driver based on the ACPI CPPC v5.0+ spec");
MODULE_LICENSE("GPL");

late_initcall(cppc_cpufreq_init);

static const struct acpi_device_id cppc_acpi_ids[] __used = {
	{ACPI_PROCESSOR_DEVICE_HID, },
	{}
};

MODULE_DEVICE_TABLE(acpi, cppc_acpi_ids);
