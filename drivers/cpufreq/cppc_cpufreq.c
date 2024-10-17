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

#include <linux/arch_topology.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/irq_work.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <uapi/linux/sched/types.h>

#include <linux/unaligned.h>

#include <acpi/cppc_acpi.h>

/*
 * This list contains information parsed from per CPU ACPI _CPC and _PSD
 * structures: e.g. the highest and lowest supported performance, capabilities,
 * desired performance, level requested etc. Depending on the share_type, not
 * all CPUs will have an entry in the list.
 */
static LIST_HEAD(cpu_data_list);

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

static struct cpufreq_driver cppc_cpufreq_driver;

static enum {
	FIE_UNSET = -1,
	FIE_ENABLED,
	FIE_DISABLED
} fie_disabled = FIE_UNSET;

#ifdef CONFIG_ACPI_CPPC_CPUFREQ_FIE
module_param(fie_disabled, int, 0444);
MODULE_PARM_DESC(fie_disabled, "Disable Frequency Invariance Engine (FIE)");

/* Frequency invariance support */
struct cppc_freq_invariance {
	int cpu;
	struct irq_work irq_work;
	struct kthread_work work;
	struct cppc_perf_fb_ctrs prev_perf_fb_ctrs;
	struct cppc_cpudata *cpu_data;
};

static DEFINE_PER_CPU(struct cppc_freq_invariance, cppc_freq_inv);
static struct kthread_worker *kworker_fie;

static unsigned int hisi_cppc_cpufreq_get_rate(unsigned int cpu);
static int cppc_perf_from_fbctrs(struct cppc_cpudata *cpu_data,
				 struct cppc_perf_fb_ctrs *fb_ctrs_t0,
				 struct cppc_perf_fb_ctrs *fb_ctrs_t1);

/**
 * cppc_scale_freq_workfn - CPPC arch_freq_scale updater for frequency invariance
 * @work: The work item.
 *
 * The CPPC driver register itself with the topology core to provide its own
 * implementation (cppc_scale_freq_tick()) of topology_scale_freq_tick() which
 * gets called by the scheduler on every tick.
 *
 * Note that the arch specific counters have higher priority than CPPC counters,
 * if available, though the CPPC driver doesn't need to have any special
 * handling for that.
 *
 * On an invocation of cppc_scale_freq_tick(), we schedule an irq work (since we
 * reach here from hard-irq context), which then schedules a normal work item
 * and cppc_scale_freq_workfn() updates the per_cpu arch_freq_scale variable
 * based on the counter updates since the last tick.
 */
static void cppc_scale_freq_workfn(struct kthread_work *work)
{
	struct cppc_freq_invariance *cppc_fi;
	struct cppc_perf_fb_ctrs fb_ctrs = {0};
	struct cppc_cpudata *cpu_data;
	unsigned long local_freq_scale;
	u64 perf;

	cppc_fi = container_of(work, struct cppc_freq_invariance, work);
	cpu_data = cppc_fi->cpu_data;

	if (cppc_get_perf_ctrs(cppc_fi->cpu, &fb_ctrs)) {
		pr_warn("%s: failed to read perf counters\n", __func__);
		return;
	}

	perf = cppc_perf_from_fbctrs(cpu_data, &cppc_fi->prev_perf_fb_ctrs,
				     &fb_ctrs);
	cppc_fi->prev_perf_fb_ctrs = fb_ctrs;

	perf <<= SCHED_CAPACITY_SHIFT;
	local_freq_scale = div64_u64(perf, cpu_data->perf_caps.highest_perf);

	/* This can happen due to counter's overflow */
	if (unlikely(local_freq_scale > 1024))
		local_freq_scale = 1024;

	per_cpu(arch_freq_scale, cppc_fi->cpu) = local_freq_scale;
}

static void cppc_irq_work(struct irq_work *irq_work)
{
	struct cppc_freq_invariance *cppc_fi;

	cppc_fi = container_of(irq_work, struct cppc_freq_invariance, irq_work);
	kthread_queue_work(kworker_fie, &cppc_fi->work);
}

static void cppc_scale_freq_tick(void)
{
	struct cppc_freq_invariance *cppc_fi = &per_cpu(cppc_freq_inv, smp_processor_id());

	/*
	 * cppc_get_perf_ctrs() can potentially sleep, call that from the right
	 * context.
	 */
	irq_work_queue(&cppc_fi->irq_work);
}

static struct scale_freq_data cppc_sftd = {
	.source = SCALE_FREQ_SOURCE_CPPC,
	.set_freq_scale = cppc_scale_freq_tick,
};

static void cppc_cpufreq_cpu_fie_init(struct cpufreq_policy *policy)
{
	struct cppc_freq_invariance *cppc_fi;
	int cpu, ret;

	if (fie_disabled)
		return;

	for_each_cpu(cpu, policy->cpus) {
		cppc_fi = &per_cpu(cppc_freq_inv, cpu);
		cppc_fi->cpu = cpu;
		cppc_fi->cpu_data = policy->driver_data;
		kthread_init_work(&cppc_fi->work, cppc_scale_freq_workfn);
		init_irq_work(&cppc_fi->irq_work, cppc_irq_work);

		ret = cppc_get_perf_ctrs(cpu, &cppc_fi->prev_perf_fb_ctrs);
		if (ret) {
			pr_warn("%s: failed to read perf counters for cpu:%d: %d\n",
				__func__, cpu, ret);

			/*
			 * Don't abort if the CPU was offline while the driver
			 * was getting registered.
			 */
			if (cpu_online(cpu))
				return;
		}
	}

	/* Register for freq-invariance */
	topology_set_scale_freq_source(&cppc_sftd, policy->cpus);
}

/*
 * We free all the resources on policy's removal and not on CPU removal as the
 * irq-work are per-cpu and the hotplug core takes care of flushing the pending
 * irq-works (hint: smpcfd_dying_cpu()) on CPU hotplug. Even if the kthread-work
 * fires on another CPU after the concerned CPU is removed, it won't harm.
 *
 * We just need to make sure to remove them all on policy->exit().
 */
static void cppc_cpufreq_cpu_fie_exit(struct cpufreq_policy *policy)
{
	struct cppc_freq_invariance *cppc_fi;
	int cpu;

	if (fie_disabled)
		return;

	/* policy->cpus will be empty here, use related_cpus instead */
	topology_clear_scale_freq_source(SCALE_FREQ_SOURCE_CPPC, policy->related_cpus);

	for_each_cpu(cpu, policy->related_cpus) {
		cppc_fi = &per_cpu(cppc_freq_inv, cpu);
		irq_work_sync(&cppc_fi->irq_work);
		kthread_cancel_work_sync(&cppc_fi->work);
	}
}

static void __init cppc_freq_invariance_init(void)
{
	struct sched_attr attr = {
		.size		= sizeof(struct sched_attr),
		.sched_policy	= SCHED_DEADLINE,
		.sched_nice	= 0,
		.sched_priority	= 0,
		/*
		 * Fake (unused) bandwidth; workaround to "fix"
		 * priority inheritance.
		 */
		.sched_runtime	= NSEC_PER_MSEC,
		.sched_deadline = 10 * NSEC_PER_MSEC,
		.sched_period	= 10 * NSEC_PER_MSEC,
	};
	int ret;

	if (fie_disabled != FIE_ENABLED && fie_disabled != FIE_DISABLED) {
		fie_disabled = FIE_ENABLED;
		if (cppc_perf_ctrs_in_pcc()) {
			pr_info("FIE not enabled on systems with registers in PCC\n");
			fie_disabled = FIE_DISABLED;
		}
	}

	if (fie_disabled)
		return;

	kworker_fie = kthread_create_worker(0, "cppc_fie");
	if (IS_ERR(kworker_fie)) {
		pr_warn("%s: failed to create kworker_fie: %ld\n", __func__,
			PTR_ERR(kworker_fie));
		fie_disabled = FIE_DISABLED;
		return;
	}

	ret = sched_setattr_nocheck(kworker_fie->task, &attr);
	if (ret) {
		pr_warn("%s: failed to set SCHED_DEADLINE: %d\n", __func__,
			ret);
		kthread_destroy_worker(kworker_fie);
		fie_disabled = FIE_DISABLED;
	}
}

static void cppc_freq_invariance_exit(void)
{
	if (fie_disabled)
		return;

	kthread_destroy_worker(kworker_fie);
}

#else
static inline void cppc_cpufreq_cpu_fie_init(struct cpufreq_policy *policy)
{
}

static inline void cppc_cpufreq_cpu_fie_exit(struct cpufreq_policy *policy)
{
}

static inline void cppc_freq_invariance_init(void)
{
}

static inline void cppc_freq_invariance_exit(void)
{
}
#endif /* CONFIG_ACPI_CPPC_CPUFREQ_FIE */

static int cppc_cpufreq_set_target(struct cpufreq_policy *policy,
				   unsigned int target_freq,
				   unsigned int relation)
{
	struct cppc_cpudata *cpu_data = policy->driver_data;
	unsigned int cpu = policy->cpu;
	struct cpufreq_freqs freqs;
	int ret = 0;

	cpu_data->perf_ctrls.desired_perf =
			cppc_khz_to_perf(&cpu_data->perf_caps, target_freq);
	freqs.old = policy->cur;
	freqs.new = target_freq;

	cpufreq_freq_transition_begin(policy, &freqs);
	ret = cppc_set_perf(cpu, &cpu_data->perf_ctrls);
	cpufreq_freq_transition_end(policy, &freqs, ret != 0);

	if (ret)
		pr_debug("Failed to set target on CPU:%d. ret:%d\n",
			 cpu, ret);

	return ret;
}

static unsigned int cppc_cpufreq_fast_switch(struct cpufreq_policy *policy,
					      unsigned int target_freq)
{
	struct cppc_cpudata *cpu_data = policy->driver_data;
	unsigned int cpu = policy->cpu;
	u32 desired_perf;
	int ret;

	desired_perf = cppc_khz_to_perf(&cpu_data->perf_caps, target_freq);
	cpu_data->perf_ctrls.desired_perf = desired_perf;
	ret = cppc_set_perf(cpu, &cpu_data->perf_ctrls);

	if (ret) {
		pr_debug("Failed to set target on CPU:%d. ret:%d\n",
			 cpu, ret);
		return 0;
	}

	return target_freq;
}

static int cppc_verify_policy(struct cpufreq_policy_data *policy)
{
	cpufreq_verify_within_cpu_limits(policy);
	return 0;
}

/*
 * The PCC subspace describes the rate at which platform can accept commands
 * on the shared PCC channel (including READs which do not count towards freq
 * transition requests), so ideally we need to use the PCC values as a fallback
 * if we don't have a platform specific transition_delay_us
 */
#ifdef CONFIG_ARM64
#include <asm/cputype.h>

static unsigned int cppc_cpufreq_get_transition_delay_us(unsigned int cpu)
{
	unsigned long implementor = read_cpuid_implementor();
	unsigned long part_num = read_cpuid_part_number();

	switch (implementor) {
	case ARM_CPU_IMP_QCOM:
		switch (part_num) {
		case QCOM_CPU_PART_FALKOR_V1:
		case QCOM_CPU_PART_FALKOR:
			return 10000;
		}
	}
	return cppc_get_transition_latency(cpu) / NSEC_PER_USEC;
}
#else
static unsigned int cppc_cpufreq_get_transition_delay_us(unsigned int cpu)
{
	return cppc_get_transition_latency(cpu) / NSEC_PER_USEC;
}
#endif

#if defined(CONFIG_ARM64) && defined(CONFIG_ENERGY_MODEL)

static DEFINE_PER_CPU(unsigned int, efficiency_class);
static void cppc_cpufreq_register_em(struct cpufreq_policy *policy);

/* Create an artificial performance state every CPPC_EM_CAP_STEP capacity unit. */
#define CPPC_EM_CAP_STEP	(20)
/* Increase the cost value by CPPC_EM_COST_STEP every performance state. */
#define CPPC_EM_COST_STEP	(1)
/* Add a cost gap correspnding to the energy of 4 CPUs. */
#define CPPC_EM_COST_GAP	(4 * SCHED_CAPACITY_SCALE * CPPC_EM_COST_STEP \
				/ CPPC_EM_CAP_STEP)

static unsigned int get_perf_level_count(struct cpufreq_policy *policy)
{
	struct cppc_perf_caps *perf_caps;
	unsigned int min_cap, max_cap;
	struct cppc_cpudata *cpu_data;
	int cpu = policy->cpu;

	cpu_data = policy->driver_data;
	perf_caps = &cpu_data->perf_caps;
	max_cap = arch_scale_cpu_capacity(cpu);
	min_cap = div_u64((u64)max_cap * perf_caps->lowest_perf,
			  perf_caps->highest_perf);
	if ((min_cap == 0) || (max_cap < min_cap))
		return 0;
	return 1 + max_cap / CPPC_EM_CAP_STEP - min_cap / CPPC_EM_CAP_STEP;
}

/*
 * The cost is defined as:
 *   cost = power * max_frequency / frequency
 */
static inline unsigned long compute_cost(int cpu, int step)
{
	return CPPC_EM_COST_GAP * per_cpu(efficiency_class, cpu) +
			step * CPPC_EM_COST_STEP;
}

static int cppc_get_cpu_power(struct device *cpu_dev,
		unsigned long *power, unsigned long *KHz)
{
	unsigned long perf_step, perf_prev, perf, perf_check;
	unsigned int min_step, max_step, step, step_check;
	unsigned long prev_freq = *KHz;
	unsigned int min_cap, max_cap;
	struct cpufreq_policy *policy;

	struct cppc_perf_caps *perf_caps;
	struct cppc_cpudata *cpu_data;

	policy = cpufreq_cpu_get_raw(cpu_dev->id);
	cpu_data = policy->driver_data;
	perf_caps = &cpu_data->perf_caps;
	max_cap = arch_scale_cpu_capacity(cpu_dev->id);
	min_cap = div_u64((u64)max_cap * perf_caps->lowest_perf,
			  perf_caps->highest_perf);
	perf_step = div_u64((u64)CPPC_EM_CAP_STEP * perf_caps->highest_perf,
			    max_cap);
	min_step = min_cap / CPPC_EM_CAP_STEP;
	max_step = max_cap / CPPC_EM_CAP_STEP;

	perf_prev = cppc_khz_to_perf(perf_caps, *KHz);
	step = perf_prev / perf_step;

	if (step > max_step)
		return -EINVAL;

	if (min_step == max_step) {
		step = max_step;
		perf = perf_caps->highest_perf;
	} else if (step < min_step) {
		step = min_step;
		perf = perf_caps->lowest_perf;
	} else {
		step++;
		if (step == max_step)
			perf = perf_caps->highest_perf;
		else
			perf = step * perf_step;
	}

	*KHz = cppc_perf_to_khz(perf_caps, perf);
	perf_check = cppc_khz_to_perf(perf_caps, *KHz);
	step_check = perf_check / perf_step;

	/*
	 * To avoid bad integer approximation, check that new frequency value
	 * increased and that the new frequency will be converted to the
	 * desired step value.
	 */
	while ((*KHz == prev_freq) || (step_check != step)) {
		perf++;
		*KHz = cppc_perf_to_khz(perf_caps, perf);
		perf_check = cppc_khz_to_perf(perf_caps, *KHz);
		step_check = perf_check / perf_step;
	}

	/*
	 * With an artificial EM, only the cost value is used. Still the power
	 * is populated such as 0 < power < EM_MAX_POWER. This allows to add
	 * more sense to the artificial performance states.
	 */
	*power = compute_cost(cpu_dev->id, step);

	return 0;
}

static int cppc_get_cpu_cost(struct device *cpu_dev, unsigned long KHz,
		unsigned long *cost)
{
	unsigned long perf_step, perf_prev;
	struct cppc_perf_caps *perf_caps;
	struct cpufreq_policy *policy;
	struct cppc_cpudata *cpu_data;
	unsigned int max_cap;
	int step;

	policy = cpufreq_cpu_get_raw(cpu_dev->id);
	cpu_data = policy->driver_data;
	perf_caps = &cpu_data->perf_caps;
	max_cap = arch_scale_cpu_capacity(cpu_dev->id);

	perf_prev = cppc_khz_to_perf(perf_caps, KHz);
	perf_step = CPPC_EM_CAP_STEP * perf_caps->highest_perf / max_cap;
	step = perf_prev / perf_step;

	*cost = compute_cost(cpu_dev->id, step);

	return 0;
}

static int populate_efficiency_class(void)
{
	struct acpi_madt_generic_interrupt *gicc;
	DECLARE_BITMAP(used_classes, 256) = {};
	int class, cpu, index;

	for_each_possible_cpu(cpu) {
		gicc = acpi_cpu_get_madt_gicc(cpu);
		class = gicc->efficiency_class;
		bitmap_set(used_classes, class, 1);
	}

	if (bitmap_weight(used_classes, 256) <= 1) {
		pr_debug("Efficiency classes are all equal (=%d). "
			"No EM registered", class);
		return -EINVAL;
	}

	/*
	 * Squeeze efficiency class values on [0:#efficiency_class-1].
	 * Values are per spec in [0:255].
	 */
	index = 0;
	for_each_set_bit(class, used_classes, 256) {
		for_each_possible_cpu(cpu) {
			gicc = acpi_cpu_get_madt_gicc(cpu);
			if (gicc->efficiency_class == class)
				per_cpu(efficiency_class, cpu) = index;
		}
		index++;
	}
	cppc_cpufreq_driver.register_em = cppc_cpufreq_register_em;

	return 0;
}

static void cppc_cpufreq_register_em(struct cpufreq_policy *policy)
{
	struct cppc_cpudata *cpu_data;
	struct em_data_callback em_cb =
		EM_ADV_DATA_CB(cppc_get_cpu_power, cppc_get_cpu_cost);

	cpu_data = policy->driver_data;
	em_dev_register_perf_domain(get_cpu_device(policy->cpu),
			get_perf_level_count(policy), &em_cb,
			cpu_data->shared_cpu_map, 0);
}

#else
static int populate_efficiency_class(void)
{
	return 0;
}
#endif

static struct cppc_cpudata *cppc_cpufreq_get_cpu_data(unsigned int cpu)
{
	struct cppc_cpudata *cpu_data;
	int ret;

	cpu_data = kzalloc(sizeof(struct cppc_cpudata), GFP_KERNEL);
	if (!cpu_data)
		goto out;

	if (!zalloc_cpumask_var(&cpu_data->shared_cpu_map, GFP_KERNEL))
		goto free_cpu;

	ret = acpi_get_psd_map(cpu, cpu_data);
	if (ret) {
		pr_debug("Err parsing CPU%d PSD data: ret:%d\n", cpu, ret);
		goto free_mask;
	}

	ret = cppc_get_perf_caps(cpu, &cpu_data->perf_caps);
	if (ret) {
		pr_debug("Err reading CPU%d perf caps: ret:%d\n", cpu, ret);
		goto free_mask;
	}

	list_add(&cpu_data->node, &cpu_data_list);

	return cpu_data;

free_mask:
	free_cpumask_var(cpu_data->shared_cpu_map);
free_cpu:
	kfree(cpu_data);
out:
	return NULL;
}

static void cppc_cpufreq_put_cpu_data(struct cpufreq_policy *policy)
{
	struct cppc_cpudata *cpu_data = policy->driver_data;

	list_del(&cpu_data->node);
	free_cpumask_var(cpu_data->shared_cpu_map);
	kfree(cpu_data);
	policy->driver_data = NULL;
}

static int cppc_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct cppc_cpudata *cpu_data;
	struct cppc_perf_caps *caps;
	int ret;

	cpu_data = cppc_cpufreq_get_cpu_data(cpu);
	if (!cpu_data) {
		pr_err("Error in acquiring _CPC/_PSD data for CPU%d.\n", cpu);
		return -ENODEV;
	}
	caps = &cpu_data->perf_caps;
	policy->driver_data = cpu_data;

	/*
	 * Set min to lowest nonlinear perf to avoid any efficiency penalty (see
	 * Section 8.4.7.1.1.5 of ACPI 6.1 spec)
	 */
	policy->min = cppc_perf_to_khz(caps, caps->lowest_nonlinear_perf);
	policy->max = cppc_perf_to_khz(caps, caps->nominal_perf);

	/*
	 * Set cpuinfo.min_freq to Lowest to make the full range of performance
	 * available if userspace wants to use any perf between lowest & lowest
	 * nonlinear perf
	 */
	policy->cpuinfo.min_freq = cppc_perf_to_khz(caps, caps->lowest_perf);
	policy->cpuinfo.max_freq = cppc_perf_to_khz(caps, caps->nominal_perf);

	policy->transition_delay_us = cppc_cpufreq_get_transition_delay_us(cpu);
	policy->shared_type = cpu_data->shared_type;

	switch (policy->shared_type) {
	case CPUFREQ_SHARED_TYPE_HW:
	case CPUFREQ_SHARED_TYPE_NONE:
		/* Nothing to be done - we'll have a policy for each CPU */
		break;
	case CPUFREQ_SHARED_TYPE_ANY:
		/*
		 * All CPUs in the domain will share a policy and all cpufreq
		 * operations will use a single cppc_cpudata structure stored
		 * in policy->driver_data.
		 */
		cpumask_copy(policy->cpus, cpu_data->shared_cpu_map);
		break;
	default:
		pr_debug("Unsupported CPU co-ord type: %d\n",
			 policy->shared_type);
		ret = -EFAULT;
		goto out;
	}

	policy->fast_switch_possible = cppc_allow_fast_switch();
	policy->dvfs_possible_from_any_cpu = true;

	/*
	 * If 'highest_perf' is greater than 'nominal_perf', we assume CPU Boost
	 * is supported.
	 */
	if (caps->highest_perf > caps->nominal_perf)
		boost_supported = true;

	/* Set policy->cur to max now. The governors will adjust later. */
	policy->cur = cppc_perf_to_khz(caps, caps->highest_perf);
	cpu_data->perf_ctrls.desired_perf =  caps->highest_perf;

	ret = cppc_set_perf(cpu, &cpu_data->perf_ctrls);
	if (ret) {
		pr_debug("Err setting perf value:%d on CPU:%d. ret:%d\n",
			 caps->highest_perf, cpu, ret);
		goto out;
	}

	cppc_cpufreq_cpu_fie_init(policy);
	return 0;

out:
	cppc_cpufreq_put_cpu_data(policy);
	return ret;
}

static void cppc_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	struct cppc_cpudata *cpu_data = policy->driver_data;
	struct cppc_perf_caps *caps = &cpu_data->perf_caps;
	unsigned int cpu = policy->cpu;
	int ret;

	cppc_cpufreq_cpu_fie_exit(policy);

	cpu_data->perf_ctrls.desired_perf = caps->lowest_perf;

	ret = cppc_set_perf(cpu, &cpu_data->perf_ctrls);
	if (ret)
		pr_debug("Err setting perf value:%d on CPU:%d. ret:%d\n",
			 caps->lowest_perf, cpu, ret);

	cppc_cpufreq_put_cpu_data(policy);
}

static inline u64 get_delta(u64 t1, u64 t0)
{
	if (t1 > t0 || t0 > ~(u32)0)
		return t1 - t0;

	return (u32)t1 - (u32)t0;
}

static int cppc_perf_from_fbctrs(struct cppc_cpudata *cpu_data,
				 struct cppc_perf_fb_ctrs *fb_ctrs_t0,
				 struct cppc_perf_fb_ctrs *fb_ctrs_t1)
{
	u64 delta_reference, delta_delivered;
	u64 reference_perf;

	reference_perf = fb_ctrs_t0->reference_perf;

	delta_reference = get_delta(fb_ctrs_t1->reference,
				    fb_ctrs_t0->reference);
	delta_delivered = get_delta(fb_ctrs_t1->delivered,
				    fb_ctrs_t0->delivered);

	/* Check to avoid divide-by zero and invalid delivered_perf */
	if (!delta_reference || !delta_delivered)
		return cpu_data->perf_ctrls.desired_perf;

	return (reference_perf * delta_delivered) / delta_reference;
}

static unsigned int cppc_cpufreq_get_rate(unsigned int cpu)
{
	struct cppc_perf_fb_ctrs fb_ctrs_t0 = {0}, fb_ctrs_t1 = {0};
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct cppc_cpudata *cpu_data;
	u64 delivered_perf;
	int ret;

	if (!policy)
		return -ENODEV;

	cpu_data = policy->driver_data;

	cpufreq_cpu_put(policy);

	ret = cppc_get_perf_ctrs(cpu, &fb_ctrs_t0);
	if (ret)
		return 0;

	udelay(2); /* 2usec delay between sampling */

	ret = cppc_get_perf_ctrs(cpu, &fb_ctrs_t1);
	if (ret)
		return 0;

	delivered_perf = cppc_perf_from_fbctrs(cpu_data, &fb_ctrs_t0,
					       &fb_ctrs_t1);

	return cppc_perf_to_khz(&cpu_data->perf_caps, delivered_perf);
}

static int cppc_cpufreq_set_boost(struct cpufreq_policy *policy, int state)
{
	struct cppc_cpudata *cpu_data = policy->driver_data;
	struct cppc_perf_caps *caps = &cpu_data->perf_caps;
	int ret;

	if (!boost_supported) {
		pr_err("BOOST not supported by CPU or firmware\n");
		return -EINVAL;
	}

	if (state)
		policy->max = cppc_perf_to_khz(caps, caps->highest_perf);
	else
		policy->max = cppc_perf_to_khz(caps, caps->nominal_perf);
	policy->cpuinfo.max_freq = policy->max;

	ret = freq_qos_update_request(policy->max_freq_req, policy->max);
	if (ret < 0)
		return ret;

	return 0;
}

static ssize_t show_freqdomain_cpus(struct cpufreq_policy *policy, char *buf)
{
	struct cppc_cpudata *cpu_data = policy->driver_data;

	return cpufreq_show_cpus(cpu_data->shared_cpu_map, buf);
}
cpufreq_freq_attr_ro(freqdomain_cpus);

static struct freq_attr *cppc_cpufreq_attr[] = {
	&freqdomain_cpus,
	NULL,
};

static struct cpufreq_driver cppc_cpufreq_driver = {
	.flags = CPUFREQ_CONST_LOOPS,
	.verify = cppc_verify_policy,
	.target = cppc_cpufreq_set_target,
	.get = cppc_cpufreq_get_rate,
	.fast_switch = cppc_cpufreq_fast_switch,
	.init = cppc_cpufreq_cpu_init,
	.exit = cppc_cpufreq_cpu_exit,
	.set_boost = cppc_cpufreq_set_boost,
	.attr = cppc_cpufreq_attr,
	.name = "cppc_cpufreq",
};

/*
 * HISI platform does not support delivered performance counter and
 * reference performance counter. It can calculate the performance using the
 * platform specific mechanism. We reuse the desired performance register to
 * store the real performance calculated by the platform.
 */
static unsigned int hisi_cppc_cpufreq_get_rate(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	struct cppc_cpudata *cpu_data;
	u64 desired_perf;
	int ret;

	if (!policy)
		return -ENODEV;

	cpu_data = policy->driver_data;

	cpufreq_cpu_put(policy);

	ret = cppc_get_desired_perf(cpu, &desired_perf);
	if (ret < 0)
		return -EIO;

	return cppc_perf_to_khz(&cpu_data->perf_caps, desired_perf);
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
			fie_disabled = FIE_DISABLED;
			break;
		}
	}

	acpi_put_table(tbl);
}

static int __init cppc_cpufreq_init(void)
{
	int ret;

	if (!acpi_cpc_valid())
		return -ENODEV;

	cppc_check_hisi_workaround();
	cppc_freq_invariance_init();
	populate_efficiency_class();

	ret = cpufreq_register_driver(&cppc_cpufreq_driver);
	if (ret)
		cppc_freq_invariance_exit();

	return ret;
}

static inline void free_cpu_data(void)
{
	struct cppc_cpudata *iter, *tmp;

	list_for_each_entry_safe(iter, tmp, &cpu_data_list, node) {
		free_cpumask_var(iter->shared_cpu_map);
		list_del(&iter->node);
		kfree(iter);
	}

}

static void __exit cppc_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&cppc_cpufreq_driver);
	cppc_freq_invariance_exit();

	free_cpu_data();
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
