// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * acpi-cpufreq.c - ACPI Processor P-States Driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002 - 2004 Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2006       Denis Sadykov <denis.m.sadykov@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/compiler.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/string_helpers.h>
#include <linux/platform_device.h>

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include <acpi/processor.h>
#include <acpi/cppc_acpi.h>

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>
#include <asm/cpu_device_id.h>

MODULE_AUTHOR("Paul Diefenbaugh, Dominik Brodowski");
MODULE_DESCRIPTION("ACPI Processor P-States Driver");
MODULE_LICENSE("GPL");

enum {
	UNDEFINED_CAPABLE = 0,
	SYSTEM_INTEL_MSR_CAPABLE,
	SYSTEM_AMD_MSR_CAPABLE,
	SYSTEM_IO_CAPABLE,
};

#define INTEL_MSR_RANGE		(0xffff)
#define AMD_MSR_RANGE		(0x7)
#define HYGON_MSR_RANGE		(0x7)

#define MSR_K7_HWCR_CPB_DIS	(1ULL << 25)

struct acpi_cpufreq_data {
	unsigned int resume;
	unsigned int cpu_feature;
	unsigned int acpi_perf_cpu;
	cpumask_var_t freqdomain_cpus;
	void (*cpu_freq_write)(struct acpi_pct_register *reg, u32 val);
	u32 (*cpu_freq_read)(struct acpi_pct_register *reg);
};

/* acpi_perf_data is a pointer to percpu data. */
static struct acpi_processor_performance __percpu *acpi_perf_data;

static inline struct acpi_processor_performance *to_perf_data(struct acpi_cpufreq_data *data)
{
	return per_cpu_ptr(acpi_perf_data, data->acpi_perf_cpu);
}

static struct cpufreq_driver acpi_cpufreq_driver;

static unsigned int acpi_pstate_strict;

static bool boost_state(unsigned int cpu)
{
	u32 lo, hi;
	u64 msr;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
	case X86_VENDOR_CENTAUR:
	case X86_VENDOR_ZHAOXIN:
		rdmsr_on_cpu(cpu, MSR_IA32_MISC_ENABLE, &lo, &hi);
		msr = lo | ((u64)hi << 32);
		return !(msr & MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
	case X86_VENDOR_HYGON:
	case X86_VENDOR_AMD:
		rdmsr_on_cpu(cpu, MSR_K7_HWCR, &lo, &hi);
		msr = lo | ((u64)hi << 32);
		return !(msr & MSR_K7_HWCR_CPB_DIS);
	}
	return false;
}

static int boost_set_msr(bool enable)
{
	u32 msr_addr;
	u64 msr_mask, val;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
	case X86_VENDOR_CENTAUR:
	case X86_VENDOR_ZHAOXIN:
		msr_addr = MSR_IA32_MISC_ENABLE;
		msr_mask = MSR_IA32_MISC_ENABLE_TURBO_DISABLE;
		break;
	case X86_VENDOR_HYGON:
	case X86_VENDOR_AMD:
		msr_addr = MSR_K7_HWCR;
		msr_mask = MSR_K7_HWCR_CPB_DIS;
		break;
	default:
		return -EINVAL;
	}

	rdmsrl(msr_addr, val);

	if (enable)
		val &= ~msr_mask;
	else
		val |= msr_mask;

	wrmsrl(msr_addr, val);
	return 0;
}

static void boost_set_msr_each(void *p_en)
{
	bool enable = (bool) p_en;

	boost_set_msr(enable);
}

static int set_boost(struct cpufreq_policy *policy, int val)
{
	on_each_cpu_mask(policy->cpus, boost_set_msr_each,
			 (void *)(long)val, 1);
	pr_debug("CPU %*pbl: Core Boosting %s.\n",
		 cpumask_pr_args(policy->cpus), str_enabled_disabled(val));

	return 0;
}

static ssize_t show_freqdomain_cpus(struct cpufreq_policy *policy, char *buf)
{
	struct acpi_cpufreq_data *data = policy->driver_data;

	if (unlikely(!data))
		return -ENODEV;

	return cpufreq_show_cpus(data->freqdomain_cpus, buf);
}

cpufreq_freq_attr_ro(freqdomain_cpus);

#ifdef CONFIG_X86_ACPI_CPUFREQ_CPB
static ssize_t store_cpb(struct cpufreq_policy *policy, const char *buf,
			 size_t count)
{
	int ret;
	unsigned int val = 0;

	if (!acpi_cpufreq_driver.set_boost)
		return -EINVAL;

	ret = kstrtouint(buf, 10, &val);
	if (ret || val > 1)
		return -EINVAL;

	cpus_read_lock();
	set_boost(policy, val);
	cpus_read_unlock();

	return count;
}

static ssize_t show_cpb(struct cpufreq_policy *policy, char *buf)
{
	return sprintf(buf, "%u\n", acpi_cpufreq_driver.boost_enabled);
}

cpufreq_freq_attr_rw(cpb);
#endif

static int check_est_cpu(unsigned int cpuid)
{
	struct cpuinfo_x86 *cpu = &cpu_data(cpuid);

	return cpu_has(cpu, X86_FEATURE_EST);
}

static int check_amd_hwpstate_cpu(unsigned int cpuid)
{
	struct cpuinfo_x86 *cpu = &cpu_data(cpuid);

	return cpu_has(cpu, X86_FEATURE_HW_PSTATE);
}

static unsigned extract_io(struct cpufreq_policy *policy, u32 value)
{
	struct acpi_cpufreq_data *data = policy->driver_data;
	struct acpi_processor_performance *perf;
	int i;

	perf = to_perf_data(data);

	for (i = 0; i < perf->state_count; i++) {
		if (value == perf->states[i].status)
			return policy->freq_table[i].frequency;
	}
	return 0;
}

static unsigned extract_msr(struct cpufreq_policy *policy, u32 msr)
{
	struct acpi_cpufreq_data *data = policy->driver_data;
	struct cpufreq_frequency_table *pos;
	struct acpi_processor_performance *perf;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		msr &= AMD_MSR_RANGE;
	else if (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		msr &= HYGON_MSR_RANGE;
	else
		msr &= INTEL_MSR_RANGE;

	perf = to_perf_data(data);

	cpufreq_for_each_entry(pos, policy->freq_table)
		if (msr == perf->states[pos->driver_data].status)
			return pos->frequency;
	return policy->freq_table[0].frequency;
}

static unsigned extract_freq(struct cpufreq_policy *policy, u32 val)
{
	struct acpi_cpufreq_data *data = policy->driver_data;

	switch (data->cpu_feature) {
	case SYSTEM_INTEL_MSR_CAPABLE:
	case SYSTEM_AMD_MSR_CAPABLE:
		return extract_msr(policy, val);
	case SYSTEM_IO_CAPABLE:
		return extract_io(policy, val);
	default:
		return 0;
	}
}

static u32 cpu_freq_read_intel(struct acpi_pct_register *not_used)
{
	u32 val, dummy __always_unused;

	rdmsr(MSR_IA32_PERF_CTL, val, dummy);
	return val;
}

static void cpu_freq_write_intel(struct acpi_pct_register *not_used, u32 val)
{
	u32 lo, hi;

	rdmsr(MSR_IA32_PERF_CTL, lo, hi);
	lo = (lo & ~INTEL_MSR_RANGE) | (val & INTEL_MSR_RANGE);
	wrmsr(MSR_IA32_PERF_CTL, lo, hi);
}

static u32 cpu_freq_read_amd(struct acpi_pct_register *not_used)
{
	u32 val, dummy __always_unused;

	rdmsr(MSR_AMD_PERF_CTL, val, dummy);
	return val;
}

static void cpu_freq_write_amd(struct acpi_pct_register *not_used, u32 val)
{
	wrmsr(MSR_AMD_PERF_CTL, val, 0);
}

static u32 cpu_freq_read_io(struct acpi_pct_register *reg)
{
	u32 val;

	acpi_os_read_port(reg->address, &val, reg->bit_width);
	return val;
}

static void cpu_freq_write_io(struct acpi_pct_register *reg, u32 val)
{
	acpi_os_write_port(reg->address, val, reg->bit_width);
}

struct drv_cmd {
	struct acpi_pct_register *reg;
	u32 val;
	union {
		void (*write)(struct acpi_pct_register *reg, u32 val);
		u32 (*read)(struct acpi_pct_register *reg);
	} func;
};

/* Called via smp_call_function_single(), on the target CPU */
static void do_drv_read(void *_cmd)
{
	struct drv_cmd *cmd = _cmd;

	cmd->val = cmd->func.read(cmd->reg);
}

static u32 drv_read(struct acpi_cpufreq_data *data, const struct cpumask *mask)
{
	struct acpi_processor_performance *perf = to_perf_data(data);
	struct drv_cmd cmd = {
		.reg = &perf->control_register,
		.func.read = data->cpu_freq_read,
	};
	int err;

	err = smp_call_function_any(mask, do_drv_read, &cmd, 1);
	WARN_ON_ONCE(err);	/* smp_call_function_any() was buggy? */
	return cmd.val;
}

/* Called via smp_call_function_many(), on the target CPUs */
static void do_drv_write(void *_cmd)
{
	struct drv_cmd *cmd = _cmd;

	cmd->func.write(cmd->reg, cmd->val);
}

static void drv_write(struct acpi_cpufreq_data *data,
		      const struct cpumask *mask, u32 val)
{
	struct acpi_processor_performance *perf = to_perf_data(data);
	struct drv_cmd cmd = {
		.reg = &perf->control_register,
		.val = val,
		.func.write = data->cpu_freq_write,
	};
	int this_cpu;

	this_cpu = get_cpu();
	if (cpumask_test_cpu(this_cpu, mask))
		do_drv_write(&cmd);

	smp_call_function_many(mask, do_drv_write, &cmd, 1);
	put_cpu();
}

static u32 get_cur_val(const struct cpumask *mask, struct acpi_cpufreq_data *data)
{
	u32 val;

	if (unlikely(cpumask_empty(mask)))
		return 0;

	val = drv_read(data, mask);

	pr_debug("%s = %u\n", __func__, val);

	return val;
}

static unsigned int get_cur_freq_on_cpu(unsigned int cpu)
{
	struct acpi_cpufreq_data *data;
	struct cpufreq_policy *policy;
	unsigned int freq;
	unsigned int cached_freq;

	pr_debug("%s (%d)\n", __func__, cpu);

	policy = cpufreq_cpu_get_raw(cpu);
	if (unlikely(!policy))
		return 0;

	data = policy->driver_data;
	if (unlikely(!data || !policy->freq_table))
		return 0;

	cached_freq = policy->freq_table[to_perf_data(data)->state].frequency;
	freq = extract_freq(policy, get_cur_val(cpumask_of(cpu), data));
	if (freq != cached_freq) {
		/*
		 * The dreaded BIOS frequency change behind our back.
		 * Force set the frequency on next target call.
		 */
		data->resume = 1;
	}

	pr_debug("cur freq = %u\n", freq);

	return freq;
}

static unsigned int check_freqs(struct cpufreq_policy *policy,
				const struct cpumask *mask, unsigned int freq)
{
	struct acpi_cpufreq_data *data = policy->driver_data;
	unsigned int cur_freq;
	unsigned int i;

	for (i = 0; i < 100; i++) {
		cur_freq = extract_freq(policy, get_cur_val(mask, data));
		if (cur_freq == freq)
			return 1;
		udelay(10);
	}
	return 0;
}

static int acpi_cpufreq_target(struct cpufreq_policy *policy,
			       unsigned int index)
{
	struct acpi_cpufreq_data *data = policy->driver_data;
	struct acpi_processor_performance *perf;
	const struct cpumask *mask;
	unsigned int next_perf_state = 0; /* Index into perf table */
	int result = 0;

	if (unlikely(!data)) {
		return -ENODEV;
	}

	perf = to_perf_data(data);
	next_perf_state = policy->freq_table[index].driver_data;
	if (perf->state == next_perf_state) {
		if (unlikely(data->resume)) {
			pr_debug("Called after resume, resetting to P%d\n",
				next_perf_state);
			data->resume = 0;
		} else {
			pr_debug("Already at target state (P%d)\n",
				next_perf_state);
			return 0;
		}
	}

	/*
	 * The core won't allow CPUs to go away until the governor has been
	 * stopped, so we can rely on the stability of policy->cpus.
	 */
	mask = policy->shared_type == CPUFREQ_SHARED_TYPE_ANY ?
		cpumask_of(policy->cpu) : policy->cpus;

	drv_write(data, mask, perf->states[next_perf_state].control);

	if (acpi_pstate_strict) {
		if (!check_freqs(policy, mask,
				 policy->freq_table[index].frequency)) {
			pr_debug("%s (%d)\n", __func__, policy->cpu);
			result = -EAGAIN;
		}
	}

	if (!result)
		perf->state = next_perf_state;

	return result;
}

static unsigned int acpi_cpufreq_fast_switch(struct cpufreq_policy *policy,
					     unsigned int target_freq)
{
	struct acpi_cpufreq_data *data = policy->driver_data;
	struct acpi_processor_performance *perf;
	struct cpufreq_frequency_table *entry;
	unsigned int next_perf_state, next_freq, index;

	/*
	 * Find the closest frequency above target_freq.
	 */
	if (policy->cached_target_freq == target_freq)
		index = policy->cached_resolved_idx;
	else
		index = cpufreq_table_find_index_dl(policy, target_freq,
						    false);

	entry = &policy->freq_table[index];
	next_freq = entry->frequency;
	next_perf_state = entry->driver_data;

	perf = to_perf_data(data);
	if (perf->state == next_perf_state) {
		if (unlikely(data->resume))
			data->resume = 0;
		else
			return next_freq;
	}

	data->cpu_freq_write(&perf->control_register,
			     perf->states[next_perf_state].control);
	perf->state = next_perf_state;
	return next_freq;
}

static unsigned long
acpi_cpufreq_guess_freq(struct acpi_cpufreq_data *data, unsigned int cpu)
{
	struct acpi_processor_performance *perf;

	perf = to_perf_data(data);
	if (cpu_khz) {
		/* search the closest match to cpu_khz */
		unsigned int i;
		unsigned long freq;
		unsigned long freqn = perf->states[0].core_frequency * 1000;

		for (i = 0; i < (perf->state_count-1); i++) {
			freq = freqn;
			freqn = perf->states[i+1].core_frequency * 1000;
			if ((2 * cpu_khz) > (freqn + freq)) {
				perf->state = i;
				return freq;
			}
		}
		perf->state = perf->state_count-1;
		return freqn;
	} else {
		/* assume CPU is at P0... */
		perf->state = 0;
		return perf->states[0].core_frequency * 1000;
	}
}

static void free_acpi_perf_data(void)
{
	unsigned int i;

	/* Freeing a NULL pointer is OK, and alloc_percpu zeroes. */
	for_each_possible_cpu(i)
		free_cpumask_var(per_cpu_ptr(acpi_perf_data, i)
				 ->shared_cpu_map);
	free_percpu(acpi_perf_data);
}

static int cpufreq_boost_down_prep(unsigned int cpu)
{
	/*
	 * Clear the boost-disable bit on the CPU_DOWN path so that
	 * this cpu cannot block the remaining ones from boosting.
	 */
	return boost_set_msr(1);
}

/*
 * acpi_cpufreq_early_init - initialize ACPI P-States library
 *
 * Initialize the ACPI P-States library (drivers/acpi/processor_perflib.c)
 * in order to determine correct frequency and voltage pairings. We can
 * do _PDC and _PSD and find out the processor dependency for the
 * actual init that will happen later...
 */
static int __init acpi_cpufreq_early_init(void)
{
	unsigned int i;
	pr_debug("%s\n", __func__);

	acpi_perf_data = alloc_percpu(struct acpi_processor_performance);
	if (!acpi_perf_data) {
		pr_debug("Memory allocation error for acpi_perf_data.\n");
		return -ENOMEM;
	}
	for_each_possible_cpu(i) {
		if (!zalloc_cpumask_var_node(
			&per_cpu_ptr(acpi_perf_data, i)->shared_cpu_map,
			GFP_KERNEL, cpu_to_node(i))) {

			/* Freeing a NULL pointer is OK: alloc_percpu zeroes. */
			free_acpi_perf_data();
			return -ENOMEM;
		}
	}

	/* Do initialization in ACPI core */
	acpi_processor_preregister_performance(acpi_perf_data);
	return 0;
}

#ifdef CONFIG_SMP
/*
 * Some BIOSes do SW_ANY coordination internally, either set it up in hw
 * or do it in BIOS firmware and won't inform about it to OS. If not
 * detected, this has a side effect of making CPU run at a different speed
 * than OS intended it to run at. Detect it and handle it cleanly.
 */
static int bios_with_sw_any_bug;

static int sw_any_bug_found(const struct dmi_system_id *d)
{
	bios_with_sw_any_bug = 1;
	return 0;
}

static const struct dmi_system_id sw_any_bug_dmi_table[] = {
	{
		.callback = sw_any_bug_found,
		.ident = "Supermicro Server X6DLP",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Supermicro"),
			DMI_MATCH(DMI_BIOS_VERSION, "080010"),
			DMI_MATCH(DMI_PRODUCT_NAME, "X6DLP"),
		},
	},
	{ }
};

static int acpi_cpufreq_blacklist(struct cpuinfo_x86 *c)
{
	/* Intel Xeon Processor 7100 Series Specification Update
	 * https://www.intel.com/Assets/PDF/specupdate/314554.pdf
	 * AL30: A Machine Check Exception (MCE) Occurring during an
	 * Enhanced Intel SpeedStep Technology Ratio Change May Cause
	 * Both Processor Cores to Lock Up. */
	if (c->x86_vendor == X86_VENDOR_INTEL) {
		if ((c->x86 == 15) &&
		    (c->x86_model == 6) &&
		    (c->x86_stepping == 8)) {
			pr_info("Intel(R) Xeon(R) 7100 Errata AL30, processors may lock up on frequency changes: disabling acpi-cpufreq\n");
			return -ENODEV;
		    }
		}
	return 0;
}
#endif

#ifdef CONFIG_ACPI_CPPC_LIB
static u64 get_max_boost_ratio(unsigned int cpu)
{
	struct cppc_perf_caps perf_caps;
	u64 highest_perf, nominal_perf;
	int ret;

	if (acpi_pstate_strict)
		return 0;

	ret = cppc_get_perf_caps(cpu, &perf_caps);
	if (ret) {
		pr_debug("CPU%d: Unable to get performance capabilities (%d)\n",
			 cpu, ret);
		return 0;
	}

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		highest_perf = amd_get_highest_perf();
	else
		highest_perf = perf_caps.highest_perf;

	nominal_perf = perf_caps.nominal_perf;

	if (!highest_perf || !nominal_perf) {
		pr_debug("CPU%d: highest or nominal performance missing\n", cpu);
		return 0;
	}

	if (highest_perf < nominal_perf) {
		pr_debug("CPU%d: nominal performance above highest\n", cpu);
		return 0;
	}

	return div_u64(highest_perf << SCHED_CAPACITY_SHIFT, nominal_perf);
}
#else
static inline u64 get_max_boost_ratio(unsigned int cpu) { return 0; }
#endif

static int acpi_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *freq_table;
	struct acpi_processor_performance *perf;
	struct acpi_cpufreq_data *data;
	unsigned int cpu = policy->cpu;
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	unsigned int valid_states = 0;
	unsigned int result = 0;
	u64 max_boost_ratio;
	unsigned int i;
#ifdef CONFIG_SMP
	static int blacklisted;
#endif

	pr_debug("%s\n", __func__);

#ifdef CONFIG_SMP
	if (blacklisted)
		return blacklisted;
	blacklisted = acpi_cpufreq_blacklist(c);
	if (blacklisted)
		return blacklisted;
#endif

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	if (!zalloc_cpumask_var(&data->freqdomain_cpus, GFP_KERNEL)) {
		result = -ENOMEM;
		goto err_free;
	}

	perf = per_cpu_ptr(acpi_perf_data, cpu);
	data->acpi_perf_cpu = cpu;
	policy->driver_data = data;

	if (cpu_has(c, X86_FEATURE_CONSTANT_TSC))
		acpi_cpufreq_driver.flags |= CPUFREQ_CONST_LOOPS;

	result = acpi_processor_register_performance(perf, cpu);
	if (result)
		goto err_free_mask;

	policy->shared_type = perf->shared_type;

	/*
	 * Will let policy->cpus know about dependency only when software
	 * coordination is required.
	 */
	if (policy->shared_type == CPUFREQ_SHARED_TYPE_ALL ||
	    policy->shared_type == CPUFREQ_SHARED_TYPE_ANY) {
		cpumask_copy(policy->cpus, perf->shared_cpu_map);
	}
	cpumask_copy(data->freqdomain_cpus, perf->shared_cpu_map);

#ifdef CONFIG_SMP
	dmi_check_system(sw_any_bug_dmi_table);
	if (bios_with_sw_any_bug && !policy_is_shared(policy)) {
		policy->shared_type = CPUFREQ_SHARED_TYPE_ALL;
		cpumask_copy(policy->cpus, topology_core_cpumask(cpu));
	}

	if (check_amd_hwpstate_cpu(cpu) && boot_cpu_data.x86 < 0x19 &&
	    !acpi_pstate_strict) {
		cpumask_clear(policy->cpus);
		cpumask_set_cpu(cpu, policy->cpus);
		cpumask_copy(data->freqdomain_cpus,
			     topology_sibling_cpumask(cpu));
		policy->shared_type = CPUFREQ_SHARED_TYPE_HW;
		pr_info_once("overriding BIOS provided _PSD data\n");
	}
#endif

	/* capability check */
	if (perf->state_count <= 1) {
		pr_debug("No P-States\n");
		result = -ENODEV;
		goto err_unreg;
	}

	if (perf->control_register.space_id != perf->status_register.space_id) {
		result = -ENODEV;
		goto err_unreg;
	}

	switch (perf->control_register.space_id) {
	case ACPI_ADR_SPACE_SYSTEM_IO:
		if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
		    boot_cpu_data.x86 == 0xf) {
			pr_debug("AMD K8 systems must use native drivers.\n");
			result = -ENODEV;
			goto err_unreg;
		}
		pr_debug("SYSTEM IO addr space\n");
		data->cpu_feature = SYSTEM_IO_CAPABLE;
		data->cpu_freq_read = cpu_freq_read_io;
		data->cpu_freq_write = cpu_freq_write_io;
		break;
	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		pr_debug("HARDWARE addr space\n");
		if (check_est_cpu(cpu)) {
			data->cpu_feature = SYSTEM_INTEL_MSR_CAPABLE;
			data->cpu_freq_read = cpu_freq_read_intel;
			data->cpu_freq_write = cpu_freq_write_intel;
			break;
		}
		if (check_amd_hwpstate_cpu(cpu)) {
			data->cpu_feature = SYSTEM_AMD_MSR_CAPABLE;
			data->cpu_freq_read = cpu_freq_read_amd;
			data->cpu_freq_write = cpu_freq_write_amd;
			break;
		}
		result = -ENODEV;
		goto err_unreg;
	default:
		pr_debug("Unknown addr space %d\n",
			(u32) (perf->control_register.space_id));
		result = -ENODEV;
		goto err_unreg;
	}

	freq_table = kcalloc(perf->state_count + 1, sizeof(*freq_table),
			     GFP_KERNEL);
	if (!freq_table) {
		result = -ENOMEM;
		goto err_unreg;
	}

	/* detect transition latency */
	policy->cpuinfo.transition_latency = 0;
	for (i = 0; i < perf->state_count; i++) {
		if ((perf->states[i].transition_latency * 1000) >
		    policy->cpuinfo.transition_latency)
			policy->cpuinfo.transition_latency =
			    perf->states[i].transition_latency * 1000;
	}

	/* Check for high latency (>20uS) from buggy BIOSes, like on T42 */
	if (perf->control_register.space_id == ACPI_ADR_SPACE_FIXED_HARDWARE &&
	    policy->cpuinfo.transition_latency > 20 * 1000) {
		policy->cpuinfo.transition_latency = 20 * 1000;
		pr_info_once("P-state transition latency capped at 20 uS\n");
	}

	/* table init */
	for (i = 0; i < perf->state_count; i++) {
		if (i > 0 && perf->states[i].core_frequency >=
		    freq_table[valid_states-1].frequency / 1000)
			continue;

		freq_table[valid_states].driver_data = i;
		freq_table[valid_states].frequency =
		    perf->states[i].core_frequency * 1000;
		valid_states++;
	}
	freq_table[valid_states].frequency = CPUFREQ_TABLE_END;

	max_boost_ratio = get_max_boost_ratio(cpu);
	if (max_boost_ratio) {
		unsigned int freq = freq_table[0].frequency;

		/*
		 * Because the loop above sorts the freq_table entries in the
		 * descending order, freq is the maximum frequency in the table.
		 * Assume that it corresponds to the CPPC nominal frequency and
		 * use it to set cpuinfo.max_freq.
		 */
		policy->cpuinfo.max_freq = freq * max_boost_ratio >> SCHED_CAPACITY_SHIFT;
	} else {
		/*
		 * If the maximum "boost" frequency is unknown, ask the arch
		 * scale-invariance code to use the "nominal" performance for
		 * CPU utilization scaling so as to prevent the schedutil
		 * governor from selecting inadequate CPU frequencies.
		 */
		arch_set_max_freq_ratio(true);
	}

	policy->freq_table = freq_table;
	perf->state = 0;

	switch (perf->control_register.space_id) {
	case ACPI_ADR_SPACE_SYSTEM_IO:
		/*
		 * The core will not set policy->cur, because
		 * cpufreq_driver->get is NULL, so we need to set it here.
		 * However, we have to guess it, because the current speed is
		 * unknown and not detectable via IO ports.
		 */
		policy->cur = acpi_cpufreq_guess_freq(data, policy->cpu);
		break;
	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		acpi_cpufreq_driver.get = get_cur_freq_on_cpu;
		break;
	default:
		break;
	}

	/* notify BIOS that we exist */
	acpi_processor_notify_smm(THIS_MODULE);

	pr_debug("CPU%u - ACPI performance management activated.\n", cpu);
	for (i = 0; i < perf->state_count; i++)
		pr_debug("     %cP%d: %d MHz, %d mW, %d uS\n",
			(i == perf->state ? '*' : ' '), i,
			(u32) perf->states[i].core_frequency,
			(u32) perf->states[i].power,
			(u32) perf->states[i].transition_latency);

	/*
	 * the first call to ->target() should result in us actually
	 * writing something to the appropriate registers.
	 */
	data->resume = 1;

	policy->fast_switch_possible = !acpi_pstate_strict &&
		!(policy_is_shared(policy) && policy->shared_type != CPUFREQ_SHARED_TYPE_ANY);

	if (perf->states[0].core_frequency * 1000 != freq_table[0].frequency)
		pr_warn(FW_WARN "P-state 0 is not max freq\n");

	if (acpi_cpufreq_driver.set_boost)
		set_boost(policy, acpi_cpufreq_driver.boost_enabled);

	return result;

err_unreg:
	acpi_processor_unregister_performance(cpu);
err_free_mask:
	free_cpumask_var(data->freqdomain_cpus);
err_free:
	kfree(data);
	policy->driver_data = NULL;

	return result;
}

static int acpi_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	struct acpi_cpufreq_data *data = policy->driver_data;

	pr_debug("%s\n", __func__);

	cpufreq_boost_down_prep(policy->cpu);
	policy->fast_switch_possible = false;
	policy->driver_data = NULL;
	acpi_processor_unregister_performance(data->acpi_perf_cpu);
	free_cpumask_var(data->freqdomain_cpus);
	kfree(policy->freq_table);
	kfree(data);

	return 0;
}

static int acpi_cpufreq_resume(struct cpufreq_policy *policy)
{
	struct acpi_cpufreq_data *data = policy->driver_data;

	pr_debug("%s\n", __func__);

	data->resume = 1;

	return 0;
}

static struct freq_attr *acpi_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&freqdomain_cpus,
#ifdef CONFIG_X86_ACPI_CPUFREQ_CPB
	&cpb,
#endif
	NULL,
};

static struct cpufreq_driver acpi_cpufreq_driver = {
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= acpi_cpufreq_target,
	.fast_switch	= acpi_cpufreq_fast_switch,
	.bios_limit	= acpi_processor_get_bios_limit,
	.init		= acpi_cpufreq_cpu_init,
	.exit		= acpi_cpufreq_cpu_exit,
	.resume		= acpi_cpufreq_resume,
	.name		= "acpi-cpufreq",
	.attr		= acpi_cpufreq_attr,
};

static void __init acpi_cpufreq_boost_init(void)
{
	if (!(boot_cpu_has(X86_FEATURE_CPB) || boot_cpu_has(X86_FEATURE_IDA))) {
		pr_debug("Boost capabilities not present in the processor\n");
		return;
	}

	acpi_cpufreq_driver.set_boost = set_boost;
	acpi_cpufreq_driver.boost_enabled = boost_state(0);
}

static int __init acpi_cpufreq_probe(struct platform_device *pdev)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	/* don't keep reloading if cpufreq_driver exists */
	if (cpufreq_get_current_driver())
		return -ENODEV;

	pr_debug("%s\n", __func__);

	ret = acpi_cpufreq_early_init();
	if (ret)
		return ret;

#ifdef CONFIG_X86_ACPI_CPUFREQ_CPB
	/* this is a sysfs file with a strange name and an even stranger
	 * semantic - per CPU instantiation, but system global effect.
	 * Lets enable it only on AMD CPUs for compatibility reasons and
	 * only if configured. This is considered legacy code, which
	 * will probably be removed at some point in the future.
	 */
	if (!check_amd_hwpstate_cpu(0)) {
		struct freq_attr **attr;

		pr_debug("CPB unsupported, do not expose it\n");

		for (attr = acpi_cpufreq_attr; *attr; attr++)
			if (*attr == &cpb) {
				*attr = NULL;
				break;
			}
	}
#endif
	acpi_cpufreq_boost_init();

	ret = cpufreq_register_driver(&acpi_cpufreq_driver);
	if (ret) {
		free_acpi_perf_data();
	}
	return ret;
}

static int acpi_cpufreq_remove(struct platform_device *pdev)
{
	pr_debug("%s\n", __func__);

	cpufreq_unregister_driver(&acpi_cpufreq_driver);

	free_acpi_perf_data();

	return 0;
}

static struct platform_driver acpi_cpufreq_platdrv = {
	.driver = {
		.name	= "acpi-cpufreq",
	},
	.remove		= acpi_cpufreq_remove,
};

static int __init acpi_cpufreq_init(void)
{
	return platform_driver_probe(&acpi_cpufreq_platdrv, acpi_cpufreq_probe);
}

static void __exit acpi_cpufreq_exit(void)
{
	platform_driver_unregister(&acpi_cpufreq_platdrv);
}

module_param(acpi_pstate_strict, uint, 0644);
MODULE_PARM_DESC(acpi_pstate_strict,
	"value 0 or non-zero. non-zero -> strict ACPI checks are "
	"performed during frequency changes.");

late_initcall(acpi_cpufreq_init);
module_exit(acpi_cpufreq_exit);

MODULE_ALIAS("platform:acpi-cpufreq");
