/*
 * acpi-cpufreq.c - ACPI Processor P-States Driver
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002 - 2004 Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2006       Denis Sadykov <denis.m.sadykov@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/compiler.h>
#include <linux/dmi.h>
#include <linux/slab.h>

#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include <acpi/processor.h>

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

MODULE_AUTHOR("Paul Diefenbaugh, Dominik Brodowski");
MODULE_DESCRIPTION("ACPI Processor P-States Driver");
MODULE_LICENSE("GPL");

#define PFX "acpi-cpufreq: "

enum {
	UNDEFINED_CAPABLE = 0,
	SYSTEM_INTEL_MSR_CAPABLE,
	SYSTEM_AMD_MSR_CAPABLE,
	SYSTEM_IO_CAPABLE,
};

#define INTEL_MSR_RANGE		(0xffff)
#define AMD_MSR_RANGE		(0x7)

#define MSR_K7_HWCR_CPB_DIS	(1ULL << 25)

struct acpi_cpufreq_data {
	struct cpufreq_frequency_table *freq_table;
	unsigned int resume;
	unsigned int cpu_feature;
	unsigned int acpi_perf_cpu;
	cpumask_var_t freqdomain_cpus;
};

/* acpi_perf_data is a pointer to percpu data. */
static struct acpi_processor_performance __percpu *acpi_perf_data;

static inline struct acpi_processor_performance *to_perf_data(struct acpi_cpufreq_data *data)
{
	return per_cpu_ptr(acpi_perf_data, data->acpi_perf_cpu);
}

static struct cpufreq_driver acpi_cpufreq_driver;

static unsigned int acpi_pstate_strict;
static struct msr __percpu *msrs;

static bool boost_state(unsigned int cpu)
{
	u32 lo, hi;
	u64 msr;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		rdmsr_on_cpu(cpu, MSR_IA32_MISC_ENABLE, &lo, &hi);
		msr = lo | ((u64)hi << 32);
		return !(msr & MSR_IA32_MISC_ENABLE_TURBO_DISABLE);
	case X86_VENDOR_AMD:
		rdmsr_on_cpu(cpu, MSR_K7_HWCR, &lo, &hi);
		msr = lo | ((u64)hi << 32);
		return !(msr & MSR_K7_HWCR_CPB_DIS);
	}
	return false;
}

static void boost_set_msrs(bool enable, const struct cpumask *cpumask)
{
	u32 cpu;
	u32 msr_addr;
	u64 msr_mask;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_INTEL:
		msr_addr = MSR_IA32_MISC_ENABLE;
		msr_mask = MSR_IA32_MISC_ENABLE_TURBO_DISABLE;
		break;
	case X86_VENDOR_AMD:
		msr_addr = MSR_K7_HWCR;
		msr_mask = MSR_K7_HWCR_CPB_DIS;
		break;
	default:
		return;
	}

	rdmsr_on_cpus(cpumask, msr_addr, msrs);

	for_each_cpu(cpu, cpumask) {
		struct msr *reg = per_cpu_ptr(msrs, cpu);
		if (enable)
			reg->q &= ~msr_mask;
		else
			reg->q |= msr_mask;
	}

	wrmsr_on_cpus(cpumask, msr_addr, msrs);
}

static int _store_boost(int val)
{
	get_online_cpus();
	boost_set_msrs(val, cpu_online_mask);
	put_online_cpus();
	pr_debug("Core Boosting %sabled.\n", val ? "en" : "dis");

	return 0;
}

static ssize_t show_freqdomain_cpus(struct cpufreq_policy *policy, char *buf)
{
	struct acpi_cpufreq_data *data = policy->driver_data;

	return cpufreq_show_cpus(data->freqdomain_cpus, buf);
}

cpufreq_freq_attr_ro(freqdomain_cpus);

#ifdef CONFIG_X86_ACPI_CPUFREQ_CPB
static ssize_t store_boost(const char *buf, size_t count)
{
	int ret;
	unsigned long val = 0;

	if (!acpi_cpufreq_driver.boost_supported)
		return -EINVAL;

	ret = kstrtoul(buf, 10, &val);
	if (ret || (val > 1))
		return -EINVAL;

	_store_boost((int) val);

	return count;
}

static ssize_t store_cpb(struct cpufreq_policy *policy, const char *buf,
			 size_t count)
{
	return store_boost(buf, count);
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

static unsigned extract_io(u32 value, struct acpi_cpufreq_data *data)
{
	struct acpi_processor_performance *perf;
	int i;

	perf = to_perf_data(data);

	for (i = 0; i < perf->state_count; i++) {
		if (value == perf->states[i].status)
			return data->freq_table[i].frequency;
	}
	return 0;
}

static unsigned extract_msr(u32 msr, struct acpi_cpufreq_data *data)
{
	struct cpufreq_frequency_table *pos;
	struct acpi_processor_performance *perf;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		msr &= AMD_MSR_RANGE;
	else
		msr &= INTEL_MSR_RANGE;

	perf = to_perf_data(data);

	cpufreq_for_each_entry(pos, data->freq_table)
		if (msr == perf->states[pos->driver_data].status)
			return pos->frequency;
	return data->freq_table[0].frequency;
}

static unsigned extract_freq(u32 val, struct acpi_cpufreq_data *data)
{
	switch (data->cpu_feature) {
	case SYSTEM_INTEL_MSR_CAPABLE:
	case SYSTEM_AMD_MSR_CAPABLE:
		return extract_msr(val, data);
	case SYSTEM_IO_CAPABLE:
		return extract_io(val, data);
	default:
		return 0;
	}
}

struct msr_addr {
	u32 reg;
};

struct io_addr {
	u16 port;
	u8 bit_width;
};

struct drv_cmd {
	unsigned int type;
	const struct cpumask *mask;
	union {
		struct msr_addr msr;
		struct io_addr io;
	} addr;
	u32 val;
};

/* Called via smp_call_function_single(), on the target CPU */
static void do_drv_read(void *_cmd)
{
	struct drv_cmd *cmd = _cmd;
	u32 h;

	switch (cmd->type) {
	case SYSTEM_INTEL_MSR_CAPABLE:
	case SYSTEM_AMD_MSR_CAPABLE:
		rdmsr(cmd->addr.msr.reg, cmd->val, h);
		break;
	case SYSTEM_IO_CAPABLE:
		acpi_os_read_port((acpi_io_address)cmd->addr.io.port,
				&cmd->val,
				(u32)cmd->addr.io.bit_width);
		break;
	default:
		break;
	}
}

/* Called via smp_call_function_many(), on the target CPUs */
static void do_drv_write(void *_cmd)
{
	struct drv_cmd *cmd = _cmd;
	u32 lo, hi;

	switch (cmd->type) {
	case SYSTEM_INTEL_MSR_CAPABLE:
		rdmsr(cmd->addr.msr.reg, lo, hi);
		lo = (lo & ~INTEL_MSR_RANGE) | (cmd->val & INTEL_MSR_RANGE);
		wrmsr(cmd->addr.msr.reg, lo, hi);
		break;
	case SYSTEM_AMD_MSR_CAPABLE:
		wrmsr(cmd->addr.msr.reg, cmd->val, 0);
		break;
	case SYSTEM_IO_CAPABLE:
		acpi_os_write_port((acpi_io_address)cmd->addr.io.port,
				cmd->val,
				(u32)cmd->addr.io.bit_width);
		break;
	default:
		break;
	}
}

static void drv_read(struct drv_cmd *cmd)
{
	int err;
	cmd->val = 0;

	err = smp_call_function_any(cmd->mask, do_drv_read, cmd, 1);
	WARN_ON_ONCE(err);	/* smp_call_function_any() was buggy? */
}

static void drv_write(struct drv_cmd *cmd)
{
	int this_cpu;

	this_cpu = get_cpu();
	if (cpumask_test_cpu(this_cpu, cmd->mask))
		do_drv_write(cmd);
	smp_call_function_many(cmd->mask, do_drv_write, cmd, 1);
	put_cpu();
}

static u32
get_cur_val(const struct cpumask *mask, struct acpi_cpufreq_data *data)
{
	struct acpi_processor_performance *perf;
	struct drv_cmd cmd;

	if (unlikely(cpumask_empty(mask)))
		return 0;

	switch (data->cpu_feature) {
	case SYSTEM_INTEL_MSR_CAPABLE:
		cmd.type = SYSTEM_INTEL_MSR_CAPABLE;
		cmd.addr.msr.reg = MSR_IA32_PERF_CTL;
		break;
	case SYSTEM_AMD_MSR_CAPABLE:
		cmd.type = SYSTEM_AMD_MSR_CAPABLE;
		cmd.addr.msr.reg = MSR_AMD_PERF_CTL;
		break;
	case SYSTEM_IO_CAPABLE:
		cmd.type = SYSTEM_IO_CAPABLE;
		perf = to_perf_data(data);
		cmd.addr.io.port = perf->control_register.address;
		cmd.addr.io.bit_width = perf->control_register.bit_width;
		break;
	default:
		return 0;
	}

	cmd.mask = mask;
	drv_read(&cmd);

	pr_debug("get_cur_val = %u\n", cmd.val);

	return cmd.val;
}

static unsigned int get_cur_freq_on_cpu(unsigned int cpu)
{
	struct acpi_cpufreq_data *data;
	struct cpufreq_policy *policy;
	unsigned int freq;
	unsigned int cached_freq;

	pr_debug("get_cur_freq_on_cpu (%d)\n", cpu);

	policy = cpufreq_cpu_get(cpu);
	if (unlikely(!policy))
		return 0;

	data = policy->driver_data;
	cpufreq_cpu_put(policy);
	if (unlikely(!data || !data->freq_table))
		return 0;

	cached_freq = data->freq_table[to_perf_data(data)->state].frequency;
	freq = extract_freq(get_cur_val(cpumask_of(cpu), data), data);
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

static unsigned int check_freqs(const struct cpumask *mask, unsigned int freq,
				struct acpi_cpufreq_data *data)
{
	unsigned int cur_freq;
	unsigned int i;

	for (i = 0; i < 100; i++) {
		cur_freq = extract_freq(get_cur_val(mask, data), data);
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
	struct drv_cmd cmd;
	unsigned int next_perf_state = 0; /* Index into perf table */
	int result = 0;

	if (unlikely(data == NULL || data->freq_table == NULL)) {
		return -ENODEV;
	}

	perf = to_perf_data(data);
	next_perf_state = data->freq_table[index].driver_data;
	if (perf->state == next_perf_state) {
		if (unlikely(data->resume)) {
			pr_debug("Called after resume, resetting to P%d\n",
				next_perf_state);
			data->resume = 0;
		} else {
			pr_debug("Already at target state (P%d)\n",
				next_perf_state);
			goto out;
		}
	}

	switch (data->cpu_feature) {
	case SYSTEM_INTEL_MSR_CAPABLE:
		cmd.type = SYSTEM_INTEL_MSR_CAPABLE;
		cmd.addr.msr.reg = MSR_IA32_PERF_CTL;
		cmd.val = (u32) perf->states[next_perf_state].control;
		break;
	case SYSTEM_AMD_MSR_CAPABLE:
		cmd.type = SYSTEM_AMD_MSR_CAPABLE;
		cmd.addr.msr.reg = MSR_AMD_PERF_CTL;
		cmd.val = (u32) perf->states[next_perf_state].control;
		break;
	case SYSTEM_IO_CAPABLE:
		cmd.type = SYSTEM_IO_CAPABLE;
		cmd.addr.io.port = perf->control_register.address;
		cmd.addr.io.bit_width = perf->control_register.bit_width;
		cmd.val = (u32) perf->states[next_perf_state].control;
		break;
	default:
		result = -ENODEV;
		goto out;
	}

	/* cpufreq holds the hotplug lock, so we are safe from here on */
	if (policy->shared_type != CPUFREQ_SHARED_TYPE_ANY)
		cmd.mask = policy->cpus;
	else
		cmd.mask = cpumask_of(policy->cpu);

	drv_write(&cmd);

	if (acpi_pstate_strict) {
		if (!check_freqs(cmd.mask, data->freq_table[index].frequency,
					data)) {
			pr_debug("acpi_cpufreq_target failed (%d)\n",
				policy->cpu);
			result = -EAGAIN;
		}
	}

	if (!result)
		perf->state = next_perf_state;

out:
	return result;
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

static int boost_notify(struct notifier_block *nb, unsigned long action,
		      void *hcpu)
{
	unsigned cpu = (long)hcpu;
	const struct cpumask *cpumask;

	cpumask = get_cpu_mask(cpu);

	/*
	 * Clear the boost-disable bit on the CPU_DOWN path so that
	 * this cpu cannot block the remaining ones from boosting. On
	 * the CPU_UP path we simply keep the boost-disable flag in
	 * sync with the current global state.
	 */

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		boost_set_msrs(acpi_cpufreq_driver.boost_enabled, cpumask);
		break;

	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		boost_set_msrs(1, cpumask);
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}


static struct notifier_block boost_nb = {
	.notifier_call          = boost_notify,
};

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
	pr_debug("acpi_cpufreq_early_init\n");

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
	 * http://www.intel.com/Assets/PDF/specupdate/314554.pdf
	 * AL30: A Machine Check Exception (MCE) Occurring during an
	 * Enhanced Intel SpeedStep Technology Ratio Change May Cause
	 * Both Processor Cores to Lock Up. */
	if (c->x86_vendor == X86_VENDOR_INTEL) {
		if ((c->x86 == 15) &&
		    (c->x86_model == 6) &&
		    (c->x86_mask == 8)) {
			printk(KERN_INFO "acpi-cpufreq: Intel(R) "
			    "Xeon(R) 7100 Errata AL30, processors may "
			    "lock up on frequency changes: disabling "
			    "acpi-cpufreq.\n");
			return -ENODEV;
		    }
		}
	return 0;
}
#endif

static int acpi_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int i;
	unsigned int valid_states = 0;
	unsigned int cpu = policy->cpu;
	struct acpi_cpufreq_data *data;
	unsigned int result = 0;
	struct cpuinfo_x86 *c = &cpu_data(policy->cpu);
	struct acpi_processor_performance *perf;
#ifdef CONFIG_SMP
	static int blacklisted;
#endif

	pr_debug("acpi_cpufreq_cpu_init\n");

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

	if (check_amd_hwpstate_cpu(cpu) && !acpi_pstate_strict) {
		cpumask_clear(policy->cpus);
		cpumask_set_cpu(cpu, policy->cpus);
		cpumask_copy(data->freqdomain_cpus,
			     topology_sibling_cpumask(cpu));
		policy->shared_type = CPUFREQ_SHARED_TYPE_HW;
		pr_info_once(PFX "overriding BIOS provided _PSD data\n");
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
		break;
	case ACPI_ADR_SPACE_FIXED_HARDWARE:
		pr_debug("HARDWARE addr space\n");
		if (check_est_cpu(cpu)) {
			data->cpu_feature = SYSTEM_INTEL_MSR_CAPABLE;
			break;
		}
		if (check_amd_hwpstate_cpu(cpu)) {
			data->cpu_feature = SYSTEM_AMD_MSR_CAPABLE;
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

	data->freq_table = kzalloc(sizeof(*data->freq_table) *
		    (perf->state_count+1), GFP_KERNEL);
	if (!data->freq_table) {
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
		printk_once(KERN_INFO
			    "P-state transition latency capped at 20 uS\n");
	}

	/* table init */
	for (i = 0; i < perf->state_count; i++) {
		if (i > 0 && perf->states[i].core_frequency >=
		    data->freq_table[valid_states-1].frequency / 1000)
			continue;

		data->freq_table[valid_states].driver_data = i;
		data->freq_table[valid_states].frequency =
		    perf->states[i].core_frequency * 1000;
		valid_states++;
	}
	data->freq_table[valid_states].frequency = CPUFREQ_TABLE_END;
	perf->state = 0;

	result = cpufreq_table_validate_and_show(policy, data->freq_table);
	if (result)
		goto err_freqfree;

	if (perf->states[0].core_frequency * 1000 != policy->cpuinfo.max_freq)
		printk(KERN_WARNING FW_WARN "P-state 0 is not max freq\n");

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

	return result;

err_freqfree:
	kfree(data->freq_table);
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

	pr_debug("acpi_cpufreq_cpu_exit\n");

	if (data) {
		policy->driver_data = NULL;
		acpi_processor_unregister_performance(data->acpi_perf_cpu);
		free_cpumask_var(data->freqdomain_cpus);
		kfree(data->freq_table);
		kfree(data);
	}

	return 0;
}

static int acpi_cpufreq_resume(struct cpufreq_policy *policy)
{
	struct acpi_cpufreq_data *data = policy->driver_data;

	pr_debug("acpi_cpufreq_resume\n");

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
	.bios_limit	= acpi_processor_get_bios_limit,
	.init		= acpi_cpufreq_cpu_init,
	.exit		= acpi_cpufreq_cpu_exit,
	.resume		= acpi_cpufreq_resume,
	.name		= "acpi-cpufreq",
	.attr		= acpi_cpufreq_attr,
	.set_boost      = _store_boost,
};

static void __init acpi_cpufreq_boost_init(void)
{
	if (boot_cpu_has(X86_FEATURE_CPB) || boot_cpu_has(X86_FEATURE_IDA)) {
		msrs = msrs_alloc();

		if (!msrs)
			return;

		acpi_cpufreq_driver.boost_supported = true;
		acpi_cpufreq_driver.boost_enabled = boost_state(0);

		cpu_notifier_register_begin();

		/* Force all MSRs to the same value */
		boost_set_msrs(acpi_cpufreq_driver.boost_enabled,
			       cpu_online_mask);

		__register_cpu_notifier(&boost_nb);

		cpu_notifier_register_done();
	}
}

static void acpi_cpufreq_boost_exit(void)
{
	if (msrs) {
		unregister_cpu_notifier(&boost_nb);

		msrs_free(msrs);
		msrs = NULL;
	}
}

static int __init acpi_cpufreq_init(void)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;

	/* don't keep reloading if cpufreq_driver exists */
	if (cpufreq_get_current_driver())
		return -EEXIST;

	pr_debug("acpi_cpufreq_init\n");

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
		acpi_cpufreq_boost_exit();
	}
	return ret;
}

static void __exit acpi_cpufreq_exit(void)
{
	pr_debug("acpi_cpufreq_exit\n");

	acpi_cpufreq_boost_exit();

	cpufreq_unregister_driver(&acpi_cpufreq_driver);

	free_acpi_perf_data();
}

module_param(acpi_pstate_strict, uint, 0644);
MODULE_PARM_DESC(acpi_pstate_strict,
	"value 0 or non-zero. non-zero -> strict ACPI checks are "
	"performed during frequency changes.");

late_initcall(acpi_cpufreq_init);
module_exit(acpi_cpufreq_exit);

static const struct x86_cpu_id acpi_cpufreq_ids[] = {
	X86_FEATURE_MATCH(X86_FEATURE_ACPI),
	X86_FEATURE_MATCH(X86_FEATURE_HW_PSTATE),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, acpi_cpufreq_ids);

static const struct acpi_device_id processor_device_ids[] = {
	{ACPI_PROCESSOR_OBJECT_HID, },
	{ACPI_PROCESSOR_DEVICE_HID, },
	{},
};
MODULE_DEVICE_TABLE(acpi, processor_device_ids);

MODULE_ALIAS("acpi");
