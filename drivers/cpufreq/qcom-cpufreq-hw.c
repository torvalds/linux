// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/bitfield.h>
#include <linux/cpufreq.h>
#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pm_opp.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qcom-cpufreq-hw.h>
#include <linux/topology.h>

#define CREATE_TRACE_POINTS
#include <trace/events/dcvsh.h>
#include <linux/units.h>

#define LUT_MAX_ENTRIES			40U
#define LUT_SRC				GENMASK(31, 30)
#define LUT_L_VAL			GENMASK(7, 0)
#define LUT_CORE_COUNT			GENMASK(18, 16)
#define LUT_VOLT			GENMASK(11, 0)
#define CLK_HW_DIV			2
#define LUT_TURBO_IND			1
#define MAX_FN_SIZE			20

#define GT_IRQ_STATUS			BIT(2)

#define CYCLE_CNTR_OFFSET(core_id, m, acc_count)		\
				(acc_count ? ((core_id + 1) * 4) : 0)

struct cpufreq_counter {
	u64 total_cycle_counter;
	u32 prev_cycle_counter;
	spinlock_t lock;
};

static struct cpufreq_counter qcom_cpufreq_counter[NR_CPUS];

struct qcom_cpufreq_soc_data {
	u32 reg_enable;
	u32 reg_domain_state;
	u32 reg_dcvs_ctrl;
	u32 reg_freq_lut;
	u32 reg_volt_lut;
	u32 reg_intr_clr;
	u32 reg_current_vote;
	u32 reg_perf_state;
	u32 reg_cycle_cntr;
	u32 lut_max_entries;
	u8 lut_row_size;
	bool accumulative_counter;
	bool turbo_ind_support;
	bool perf_lock_support;
};

struct qcom_cpufreq_data {
	void __iomem *base;
	void __iomem *pdmem_base;
	struct resource *res;
	const struct qcom_cpufreq_soc_data *soc_data;

	/*
	 * Mutex to synchronize between de-init sequence and re-starting LMh
	 * polling/interrupts
	 */
	struct mutex throttle_lock;
	int hw_clk_domain;
	int throttle_irq;
	char irq_name[15];
	bool cancel_throttle;
	struct delayed_work throttle_work;
	struct cpufreq_policy *policy;
	unsigned long last_non_boost_freq;

	bool per_core_dcvs;
	unsigned long dcvsh_freq_limit;
	struct device_attribute freq_limit_attr;
};

static unsigned long cpu_hw_rate, xo_rate;
static bool icc_scaling_enabled;

/*
 * show_hw_clk_domain - the HW clock domain per policy
 */
static ssize_t show_hw_clk_domain(struct cpufreq_policy *policy, char *buf)
{
	struct qcom_cpufreq_data *data;

	data = policy->driver_data;
	if (data)
		return scnprintf(buf, sizeof(int), "%u\n", data->hw_clk_domain);

	return -EIO;
}

cpufreq_freq_attr_ro(hw_clk_domain);

static int qcom_cpufreq_set_bw(struct cpufreq_policy *policy,
			       unsigned long freq_khz)
{
	unsigned long freq_hz = freq_khz * 1000;
	struct dev_pm_opp *opp;
	struct device *dev;
	int ret;

	dev = get_cpu_device(policy->cpu);
	if (!dev)
		return -ENODEV;

	opp = dev_pm_opp_find_freq_exact(dev, freq_hz, true);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	ret = dev_pm_opp_set_opp(dev, opp);
	dev_pm_opp_put(opp);
	return ret;
}

static int qcom_cpufreq_update_opp(struct device *cpu_dev,
				   unsigned long freq_khz,
				   unsigned long volt)
{
	unsigned long freq_hz = freq_khz * 1000;
	int ret;

	/* Skip voltage update if the opp table is not available */
	if (!icc_scaling_enabled)
		return dev_pm_opp_add(cpu_dev, freq_hz, volt);

	ret = dev_pm_opp_adjust_voltage(cpu_dev, freq_hz, volt, volt, volt);
	if (ret) {
		dev_err(cpu_dev, "Voltage update failed freq=%ld\n", freq_khz);
		return ret;
	}

	return dev_pm_opp_enable(cpu_dev, freq_hz);
}

u64 qcom_cpufreq_get_cpu_cycle_counter(int cpu)
{
	const struct qcom_cpufreq_soc_data *soc_data;
	struct cpufreq_counter *cpu_counter;
	struct qcom_cpufreq_data *data;
	struct cpufreq_policy *policy;
	u64 cycle_counter_ret;
	unsigned long flags;
	u16 offset;
	u32 val;

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy)
		return 0;

	data = policy->driver_data;
	soc_data = data->soc_data;

	cpu_counter = &qcom_cpufreq_counter[cpu];
	spin_lock_irqsave(&cpu_counter->lock, flags);

	offset = CYCLE_CNTR_OFFSET(topology_core_id(cpu), policy->related_cpus,
					soc_data->accumulative_counter);
	val = readl_relaxed(data->base +
					soc_data->reg_cycle_cntr + offset);

	if (val < cpu_counter->prev_cycle_counter) {
		/* Handle counter overflow */
		cpu_counter->total_cycle_counter += UINT_MAX -
			cpu_counter->prev_cycle_counter + val;
		cpu_counter->prev_cycle_counter = val;
	} else {
		cpu_counter->total_cycle_counter += val -
			cpu_counter->prev_cycle_counter;
		cpu_counter->prev_cycle_counter = val;
	}
	cycle_counter_ret = cpu_counter->total_cycle_counter;
	spin_unlock_irqrestore(&cpu_counter->lock, flags);

	pr_debug("CPU %u, core-id 0x%x, offset %u\n", cpu, topology_core_id(cpu), offset);

	return cycle_counter_ret;
}
EXPORT_SYMBOL(qcom_cpufreq_get_cpu_cycle_counter);

static int qcom_cpufreq_hw_target_index(struct cpufreq_policy *policy,
					unsigned int index)
{
	struct qcom_cpufreq_data *data = policy->driver_data;
	const struct qcom_cpufreq_soc_data *soc_data = data->soc_data;
	unsigned long freq = policy->freq_table[index].frequency;
	unsigned int i;

	if (soc_data->perf_lock_support) {
		if (data->pdmem_base)
			writel_relaxed(index, data->pdmem_base);
	}

	writel_relaxed(index, data->base + soc_data->reg_perf_state);

	if (data->per_core_dcvs)
		for (i = 1; i < cpumask_weight(policy->related_cpus); i++)
			writel_relaxed(index, data->base + soc_data->reg_perf_state + i * 4);

	if (icc_scaling_enabled)
		qcom_cpufreq_set_bw(policy, freq);

	return 0;
}

static unsigned long qcom_lmh_get_throttle_freq(struct qcom_cpufreq_data *data)
{
	unsigned int lval;

	if (data->soc_data->reg_current_vote)
		lval = readl_relaxed(data->base + data->soc_data->reg_current_vote) & 0x3ff;
	else
		lval = readl_relaxed(data->base + data->soc_data->reg_domain_state) & 0xff;

	return lval * xo_rate;
}

/* Get the frequency requested by the cpufreq core for the CPU */
static unsigned int qcom_cpufreq_get_freq(unsigned int cpu)
{
	struct qcom_cpufreq_data *data;
	const struct qcom_cpufreq_soc_data *soc_data;
	struct cpufreq_policy *policy;
	unsigned int index;

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy)
		return 0;

	data = policy->driver_data;
	soc_data = data->soc_data;

	index = readl_relaxed(data->base + soc_data->reg_perf_state);
	index = min(index, soc_data->lut_max_entries - 1);

	return policy->freq_table[index].frequency;
}

static unsigned int qcom_cpufreq_hw_get(unsigned int cpu)
{
	struct qcom_cpufreq_data *data;
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get_raw(cpu);
	if (!policy)
		return 0;

	data = policy->driver_data;

	if (data->throttle_irq >= 0)
		return qcom_lmh_get_throttle_freq(data) / HZ_PER_KHZ;

	return qcom_cpufreq_get_freq(cpu);
}

static unsigned int qcom_cpufreq_hw_fast_switch(struct cpufreq_policy *policy,
						unsigned int target_freq)
{
	struct qcom_cpufreq_data *data = policy->driver_data;
	const struct qcom_cpufreq_soc_data *soc_data = data->soc_data;
	unsigned int index;
	unsigned int i;

	index = policy->cached_resolved_idx;
	writel_relaxed(index, data->base + soc_data->reg_perf_state);

	if (data->per_core_dcvs)
		for (i = 1; i < cpumask_weight(policy->related_cpus); i++)
			writel_relaxed(index, data->base + soc_data->reg_perf_state + i * 4);

	return policy->freq_table[index].frequency;
}

static int qcom_cpufreq_hw_read_lut(struct device *cpu_dev,
				    struct cpufreq_policy *policy)
{
	u32 data, src, lval, i, core_count, prev_freq = 0, freq;
	u32 volt, max_cc = 0;
	struct cpufreq_frequency_table	*table;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int ret;
	struct qcom_cpufreq_data *drv_data = policy->driver_data;
	const struct qcom_cpufreq_soc_data *soc_data = drv_data->soc_data;

	table = kcalloc(soc_data->lut_max_entries + 1, sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	ret = dev_pm_opp_of_add_table(cpu_dev);
	if (!ret) {
		/* Disable all opps and cross-validate against LUT later */
		icc_scaling_enabled = true;
		for (rate = 0; ; rate++) {
			opp = dev_pm_opp_find_freq_ceil(cpu_dev, &rate);
			if (IS_ERR(opp))
				break;

			dev_pm_opp_put(opp);
			dev_pm_opp_disable(cpu_dev, rate);
		}
	} else if (ret != -ENODEV) {
		dev_err(cpu_dev, "Invalid opp table in device tree\n");
		kfree(table);
		return ret;
	} else {
		policy->fast_switch_possible = true;
		icc_scaling_enabled = false;
	}

	for (i = 0; i < soc_data->lut_max_entries; i++) {
		data = readl_relaxed(drv_data->base + soc_data->reg_freq_lut +
				      i * soc_data->lut_row_size);
		src = FIELD_GET(LUT_SRC, data);
		lval = FIELD_GET(LUT_L_VAL, data);
		core_count = FIELD_GET(LUT_CORE_COUNT, data);

		if (i == 0)
			max_cc = core_count;

		data = readl_relaxed(drv_data->base + soc_data->reg_volt_lut +
				      i * soc_data->lut_row_size);
		volt = FIELD_GET(LUT_VOLT, data) * 1000;

		if (src)
			freq = xo_rate * lval / 1000;
		else
			freq = cpu_hw_rate / 1000;

		if (core_count == LUT_TURBO_IND && soc_data->turbo_ind_support)
			table[i].frequency = CPUFREQ_ENTRY_INVALID;
		else if (freq != prev_freq) {
			if (!qcom_cpufreq_update_opp(cpu_dev, freq, volt)) {
				table[i].frequency = freq;
				if (core_count < max_cc)
					table[i].flags = CPUFREQ_BOOST_FREQ;
				dev_dbg(cpu_dev, "index=%d freq=%d, core_count %d\n", i,
				freq, core_count);
			} else {
				dev_warn(cpu_dev, "failed to update OPP for freq=%d\n", freq);
				table[i].frequency = CPUFREQ_ENTRY_INVALID;
			}
		}

		/*
		 * Two of the same frequencies with the same core counts means
		 * end of table
		 */
		if (i > 0 && prev_freq == freq) {
			struct cpufreq_frequency_table *prev = &table[i - 1];

			/*
			 * Only treat the last frequency that might be a boost
			 * as the boost frequency
			 */
			if (prev->frequency == CPUFREQ_ENTRY_INVALID) {
				if (!qcom_cpufreq_update_opp(cpu_dev, prev_freq, volt)) {
					prev->frequency = prev_freq;
					prev->flags = CPUFREQ_BOOST_FREQ;
				} else {
					dev_warn(cpu_dev, "failed to update OPP for freq=%d\n",
						 freq);
				}
			}

			break;
		}

		prev_freq = freq;
	}

	table[i].frequency = CPUFREQ_TABLE_END;
	policy->freq_table = table;

	for (i = 0; i < soc_data->lut_max_entries && table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (table[i].flags == CPUFREQ_BOOST_FREQ)
			break;

		drv_data->last_non_boost_freq = table[i].frequency;
	}

	dev_pm_opp_set_sharing_cpus(cpu_dev, policy->cpus);

	return 0;
}

static void qcom_get_related_cpus(int index, struct cpumask *m)
{
	struct device_node *cpu_np;
	struct of_phandle_args args;
	int cpu, ret;

	for_each_possible_cpu(cpu) {
		cpu_np = of_cpu_device_node_get(cpu);
		if (!cpu_np)
			continue;

		ret = of_parse_phandle_with_args(cpu_np, "qcom,freq-domain",
						 "#freq-domain-cells", 0,
						 &args);
		of_node_put(cpu_np);
		if (ret < 0)
			continue;

		if (index == args.args[0])
			cpumask_set_cpu(cpu, m);
	}
}

static ssize_t dcvsh_freq_limit_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct qcom_cpufreq_data *c = container_of(attr, struct qcom_cpufreq_data,
						   freq_limit_attr);
	return scnprintf(buf, PAGE_SIZE, "%lu\n", c->dcvsh_freq_limit);
}

static void qcom_lmh_dcvs_notify(struct qcom_cpufreq_data *data)
{
	struct cpufreq_policy *policy = data->policy;
	int cpu = cpumask_first(policy->related_cpus);
	struct device *dev = get_cpu_device(cpu);
	unsigned long freq_hz, throttled_freq, thermal_pressure;
	struct dev_pm_opp *opp;

	if (!dev)
		return;

	/*
	 * Get the h/w throttled frequency, normalize it using the
	 * registered opp table and use it to calculate thermal pressure.
	 */
	freq_hz = qcom_lmh_get_throttle_freq(data);

	opp = dev_pm_opp_find_freq_floor(dev, &freq_hz);
	if (IS_ERR(opp) && PTR_ERR(opp) == -ERANGE)
		opp = dev_pm_opp_find_freq_ceil(dev, &freq_hz);

	if (IS_ERR(opp))
		dev_warn(dev, "Can't find the OPP for throttling: %pe!\n", opp);
	else
		dev_pm_opp_put(opp);

	throttled_freq = thermal_pressure = freq_hz / HZ_PER_KHZ;

	/*
	 * In the unlikely case policy is unregistered do not enable
	 * polling or h/w interrupt
	 */
	mutex_lock(&data->throttle_lock);
	if (data->cancel_throttle)
		goto out;

	/*
	 * If h/w throttled frequency is higher than what cpufreq has requested
	 * for, then stop polling and switch back to interrupt mechanism.
	 */
	if (throttled_freq >= qcom_cpufreq_get_freq(cpu)) {
		thermal_pressure = policy->cpuinfo.max_freq;

		enable_irq(data->throttle_irq);
		trace_dcvsh_throttle(cpu, 0);
	} else {
		/*
		 * If the frequency is at least the highest, non-boost
		 * frequency, then the delta vs. what's requested is likely due
		 * to core-count boost limitations and shouldn't be
		 * communicated as thermal pressure.
		 */
		if (throttled_freq >= data->last_non_boost_freq)
			thermal_pressure = policy->cpuinfo.max_freq;

		mod_delayed_work(system_highpri_wq, &data->throttle_work,
				 msecs_to_jiffies(10));
	}

	trace_dcvsh_freq(cpu, qcom_cpufreq_get_freq(cpu), throttled_freq, thermal_pressure);

	/* Update thermal pressure (the boost frequencies are accepted) */
	arch_update_thermal_pressure(policy->related_cpus, thermal_pressure);
	data->dcvsh_freq_limit = thermal_pressure;

out:
	mutex_unlock(&data->throttle_lock);
}

static void qcom_lmh_dcvs_poll(struct work_struct *work)
{
	struct qcom_cpufreq_data *data;

	data = container_of(work, struct qcom_cpufreq_data, throttle_work.work);
	qcom_lmh_dcvs_notify(data);
}

static irqreturn_t qcom_lmh_dcvs_handle_irq(int irq, void *data)
{
	struct qcom_cpufreq_data *c_data = data;
	struct cpufreq_policy *policy = c_data->policy;

	/* Disable interrupt and enable polling */
	disable_irq_nosync(c_data->throttle_irq);
	trace_dcvsh_throttle(cpumask_first(policy->cpus), 1);
	schedule_delayed_work(&c_data->throttle_work, 0);

	if (c_data->soc_data->reg_intr_clr)
		writel_relaxed(GT_IRQ_STATUS,
			       c_data->base + c_data->soc_data->reg_intr_clr);

	return IRQ_HANDLED;
}

static const struct qcom_cpufreq_soc_data qcom_soc_data = {
	.reg_enable = 0x0,
	.reg_dcvs_ctrl = 0xbc,
	.reg_freq_lut = 0x110,
	.reg_volt_lut = 0x114,
	.reg_current_vote = 0x704,
	.reg_perf_state = 0x920,
	.reg_cycle_cntr = 0x9c0,
	.lut_row_size = 32,
	.lut_max_entries = LUT_MAX_ENTRIES,
	.accumulative_counter = false,
	.turbo_ind_support = true,
};

static const struct qcom_cpufreq_soc_data epss_soc_data = {
	.reg_enable = 0x0,
	.reg_domain_state = 0x20,
	.reg_dcvs_ctrl = 0xb0,
	.reg_freq_lut = 0x100,
	.reg_volt_lut = 0x200,
	.reg_intr_clr = 0x308,
	.reg_perf_state = 0x320,
	.reg_cycle_cntr = 0x3c4,
	.lut_row_size = 4,
	.lut_max_entries = LUT_MAX_ENTRIES,
	.accumulative_counter = true,
	.turbo_ind_support = false,
	.perf_lock_support = false,
};

static const struct qcom_cpufreq_soc_data epss_pdmem_soc_data = {
	.reg_enable = 0x0,
	.reg_domain_state = 0x20,
	.reg_dcvs_ctrl = 0xb0,
	.reg_freq_lut = 0x100,
	.reg_volt_lut = 0x200,
	.reg_intr_clr = 0x308,
	.reg_perf_state = 0x320,
	.reg_cycle_cntr = 0x3c4,
	.lut_row_size = 4,
	.lut_max_entries = LUT_MAX_ENTRIES,
	.accumulative_counter = true,
	.turbo_ind_support = false,
	.perf_lock_support = true,
};

static const struct qcom_cpufreq_soc_data rimps_soc_data = {
	.reg_enable = 0x0,
	.reg_domain_state = 0x20,
	.reg_dcvs_ctrl = 0xb0,
	.reg_freq_lut = 0x100,
	.reg_volt_lut = 0x200,
	.reg_intr_clr = 0x308,
	.reg_perf_state = 0x320,
	.reg_cycle_cntr = 0x3c4,
	.lut_row_size = 4,
	.lut_max_entries = 12,
	.accumulative_counter = true,
	.turbo_ind_support = false,
	.perf_lock_support = false,
};

static const struct qcom_cpufreq_soc_data rimps_pdmem_soc_data = {
	.reg_enable = 0x0,
	.reg_domain_state = 0x20,
	.reg_dcvs_ctrl = 0xb0,
	.reg_freq_lut = 0x100,
	.reg_volt_lut = 0x200,
	.reg_intr_clr = 0x308,
	.reg_perf_state = 0x320,
	.reg_cycle_cntr = 0x3c4,
	.lut_row_size = 4,
	.lut_max_entries = 12,
	.accumulative_counter = true,
	.turbo_ind_support = false,
	.perf_lock_support = true,
};

static const struct of_device_id qcom_cpufreq_hw_match[] = {
	{ .compatible = "qcom,cpufreq-hw", .data = &qcom_soc_data },
	{ .compatible = "qcom,cpufreq-epss", .data = &epss_soc_data },
	{ .compatible = "qcom,cpufreq-epss-pdmem", .data = &epss_pdmem_soc_data },
	{ .compatible = "qcom,cpufreq-rimps", .data = &rimps_soc_data },
	{ .compatible = "qcom,cpufreq-rimps-pdmem", .data = &rimps_pdmem_soc_data },
	{}
};
MODULE_DEVICE_TABLE(of, qcom_cpufreq_hw_match);

static int qcom_cpufreq_hw_lmh_init(struct cpufreq_policy *policy, int index,
				    struct device *cpu_dev)
{
	struct qcom_cpufreq_data *data = policy->driver_data;
	struct platform_device *pdev = cpufreq_get_driver_data();
	int ret;

	/*
	 * Look for LMh interrupt. If no interrupt line is specified /
	 * if there is an error, allow cpufreq to be enabled as usual.
	 */
	data->throttle_irq = platform_get_irq_optional(pdev, index);
	if (data->throttle_irq == -ENXIO)
		return 0;
	if (data->throttle_irq < 0)
		return data->throttle_irq;

	data->cancel_throttle = false;
	data->policy = policy;

	mutex_init(&data->throttle_lock);
	INIT_DELAYED_WORK(&data->throttle_work, qcom_lmh_dcvs_poll);

	snprintf(data->irq_name, sizeof(data->irq_name), "dcvsh-irq-%u", policy->cpu);
	ret = request_threaded_irq(data->throttle_irq, NULL, qcom_lmh_dcvs_handle_irq,
				   IRQF_ONESHOT | IRQF_NO_AUTOEN, data->irq_name, data);
	if (ret) {
		dev_err(&pdev->dev, "Error registering %s: %d\n", data->irq_name, ret);
		return 0;
	}

	ret = irq_set_affinity_and_hint(data->throttle_irq, policy->cpus);
	if (ret)
		dev_err(&pdev->dev, "Failed to set CPU affinity of %s[%d]\n",
			data->irq_name, data->throttle_irq);

	sysfs_attr_init(&data->freq_limit_attr.attr);
	data->freq_limit_attr.attr.name = "dcvsh_freq_limit";
	data->freq_limit_attr.show = dcvsh_freq_limit_show;
	data->freq_limit_attr.attr.mode = 0444;
	data->dcvsh_freq_limit = U32_MAX;
	device_create_file(cpu_dev, &data->freq_limit_attr);

	return 0;
}

static int qcom_cpufreq_hw_cpu_online(struct cpufreq_policy *policy)
{
	struct qcom_cpufreq_data *data = policy->driver_data;
	struct platform_device *pdev = cpufreq_get_driver_data();
	int ret;

	if (data->throttle_irq <= 0)
		return 0;

	mutex_lock(&data->throttle_lock);
	data->cancel_throttle = false;
	mutex_unlock(&data->throttle_lock);

	ret = irq_set_affinity_and_hint(data->throttle_irq, policy->cpus);
	if (ret)
		dev_err(&pdev->dev, "Failed to set CPU affinity of %s[%d]\n",
			data->irq_name, data->throttle_irq);

	return ret;
}

static int qcom_cpufreq_hw_cpu_offline(struct cpufreq_policy *policy)
{
	struct qcom_cpufreq_data *data = policy->driver_data;

	if (data->throttle_irq <= 0)
		return 0;

	mutex_lock(&data->throttle_lock);
	data->cancel_throttle = true;
	mutex_unlock(&data->throttle_lock);

	cancel_delayed_work_sync(&data->throttle_work);
	irq_set_affinity_and_hint(data->throttle_irq, NULL);
	disable_irq_nosync(data->throttle_irq);

	arch_update_thermal_pressure(policy->related_cpus, U32_MAX);
	trace_dcvsh_throttle(cpumask_first(policy->related_cpus), 0);

	return 0;
}

static void qcom_cpufreq_hw_lmh_exit(struct qcom_cpufreq_data *data)
{
	if (data->throttle_irq <= 0)
		return;

	free_irq(data->throttle_irq, data);
}

static int qcom_cpufreq_hw_cpu_init(struct cpufreq_policy *policy)
{
	struct platform_device *pdev = cpufreq_get_driver_data();
	struct device *dev = &pdev->dev;
	struct of_phandle_args args;
	struct device_node *cpu_np;
	struct device *cpu_dev;
	struct resource *res;
	void __iomem *base;
	struct qcom_cpufreq_data *data;
	char pdmem_name[MAX_FN_SIZE] = {};
	int ret, index;

	cpu_dev = get_cpu_device(policy->cpu);
	if (!cpu_dev) {
		pr_err("%s: failed to get cpu%d device\n", __func__,
		       policy->cpu);
		return -ENODEV;
	}

	cpu_np = of_cpu_device_node_get(policy->cpu);
	if (!cpu_np)
		return -EINVAL;

	ret = of_parse_phandle_with_args(cpu_np, "qcom,freq-domain",
					 "#freq-domain-cells", 0, &args);
	of_node_put(cpu_np);
	if (ret)
		return ret;

	index = args.args[0];

	data = policy->driver_data;

	if (!data) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, index);
		if (!res) {
			dev_err(dev, "failed to get mem resource %d\n", index);
			return -ENODEV;
		}

		if (!devm_request_mem_region(dev, res->start, resource_size(res), res->name)) {
			dev_err(dev, "failed to request resource %pR\n", res);
			return -EBUSY;
		}

		base = devm_ioremap(dev, res->start, resource_size(res));
		if (!base) {
			dev_err(dev, "failed to map resource %pR\n", res);
			return -ENOMEM;
		}

		data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
		if (!data) {
			return -ENOMEM;
		}

		ret = sysfs_create_file(&policy->kobj, &hw_clk_domain.attr);
		if (ret) {
			pr_err("%s: cannot register HW clock domain sysfs file\n", __func__);
			return ret;
		}

		data->soc_data = of_device_get_match_data(&pdev->dev);
		data->base = base;
		data->res = res;
		data->hw_clk_domain = index;
	}

	base = data->base;

	/* HW should be in enabled state to proceed */
	if (!(readl_relaxed(base + data->soc_data->reg_enable) & 0x1)) {
		dev_err(dev, "Domain-%d cpufreq hardware not enabled\n", index);
		ret = -ENODEV;
		goto error;
	}

	if (readl_relaxed(base + data->soc_data->reg_dcvs_ctrl) & 0x1)
		data->per_core_dcvs = true;

	qcom_get_related_cpus(index, policy->cpus);
	if (cpumask_empty(policy->cpus)) {
		dev_err(dev, "Domain-%d failed to get related CPUs\n", index);
		ret = -ENOENT;
		goto error;
	}

	policy->driver_data = data;
	policy->dvfs_possible_from_any_cpu = true;

	ret = qcom_cpufreq_hw_read_lut(cpu_dev, policy);
	if (ret) {
		dev_err(dev, "Domain-%d failed to read LUT\n", index);
		goto error;
	}

	if (data->soc_data->perf_lock_support) {
		snprintf(pdmem_name, sizeof(pdmem_name), "pdmem-domain%d",
								index);
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
								pdmem_name);
		if (!res)
			dev_err(dev, "PDMEM domain-%d failed\n", index);

		base = devm_ioremap_resource(dev, res);
		if (IS_ERR(base))
			dev_err(dev, "Failed to map PDMEM domain-%d\n", index);
		else
			data->pdmem_base = base;
	}

	ret = dev_pm_opp_get_opp_count(cpu_dev);
	if (ret <= 0) {
		dev_err(cpu_dev, "Failed to add OPPs\n");
		ret = -ENODEV;
		goto error;
	}

	if (policy_has_boost_freq(policy)) {
		ret = cpufreq_enable_boost_support();
		if (ret)
			dev_warn(cpu_dev, "failed to enable boost: %d\n", ret);
	}

	ret = qcom_cpufreq_hw_lmh_init(policy, index, cpu_dev);
	if (ret)
		goto error;

	return 0;
error:
	policy->driver_data = NULL;
	return ret;
}

static int qcom_cpufreq_hw_cpu_exit(struct cpufreq_policy *policy)
{
	struct device *cpu_dev = get_cpu_device(policy->cpu);

	qcom_cpufreq_hw_lmh_exit(policy->driver_data);
	dev_pm_opp_remove_all_dynamic(cpu_dev);
	dev_pm_opp_of_cpumask_remove_table(policy->related_cpus);
	kfree(policy->freq_table);

	return 0;
}

static void qcom_cpufreq_ready(struct cpufreq_policy *policy)
{
	struct qcom_cpufreq_data *data = policy->driver_data;

	if (data->throttle_irq >= 0)
		enable_irq(data->throttle_irq);
}

static struct freq_attr *qcom_cpufreq_hw_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&cpufreq_freq_attr_scaling_boost_freqs,
	NULL
};

static struct cpufreq_driver cpufreq_qcom_hw_driver = {
	.flags		= CPUFREQ_NEED_INITIAL_FREQ_CHECK |
			  CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
			  CPUFREQ_IS_COOLING_DEV,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= qcom_cpufreq_hw_target_index,
	.get		= qcom_cpufreq_hw_get,
	.init		= qcom_cpufreq_hw_cpu_init,
	.exit		= qcom_cpufreq_hw_cpu_exit,
	.online		= qcom_cpufreq_hw_cpu_online,
	.offline	= qcom_cpufreq_hw_cpu_offline,
	.register_em	= cpufreq_register_em_with_opp,
	.fast_switch    = qcom_cpufreq_hw_fast_switch,
	.name		= "qcom-cpufreq-hw",
	.attr		= qcom_cpufreq_hw_attr,
	.ready		= qcom_cpufreq_ready,
	.boost_enabled	= true,
};

static int qcom_cpufreq_hw_driver_probe(struct platform_device *pdev)
{
	struct device *cpu_dev;
	struct clk *clk;
	int ret, cpu;

	clk = clk_get(&pdev->dev, "xo");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	xo_rate = clk_get_rate(clk);
	clk_put(clk);

	clk = clk_get(&pdev->dev, "alternate");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	cpu_hw_rate = clk_get_rate(clk) / CLK_HW_DIV;
	clk_put(clk);

	cpufreq_qcom_hw_driver.driver_data = pdev;

	/* Check for optional interconnect paths on CPU0 */
	cpu_dev = get_cpu_device(0);
	if (!cpu_dev)
		return -EPROBE_DEFER;

	ret = dev_pm_opp_of_find_icc_paths(cpu_dev, NULL);
	if (ret)
		return ret;

	for_each_possible_cpu(cpu)
		spin_lock_init(&qcom_cpufreq_counter[cpu].lock);

	ret = cpufreq_register_driver(&cpufreq_qcom_hw_driver);
	if (ret)
		dev_err(&pdev->dev, "CPUFreq HW driver failed to register\n");
	else
		dev_dbg(&pdev->dev, "QCOM CPUFreq HW driver initialized\n");

	return ret;
}

static int qcom_cpufreq_hw_driver_remove(struct platform_device *pdev)
{
	return cpufreq_unregister_driver(&cpufreq_qcom_hw_driver);
}

static struct platform_driver qcom_cpufreq_hw_driver = {
	.probe = qcom_cpufreq_hw_driver_probe,
	.remove = qcom_cpufreq_hw_driver_remove,
	.driver = {
		.name = "qcom-cpufreq-hw",
		.of_match_table = qcom_cpufreq_hw_match,
	},
};

static int __init qcom_cpufreq_hw_init(void)
{
	return platform_driver_register(&qcom_cpufreq_hw_driver);
}
postcore_initcall(qcom_cpufreq_hw_init);

static void __exit qcom_cpufreq_hw_exit(void)
{
	platform_driver_unregister(&qcom_cpufreq_hw_driver);
}
module_exit(qcom_cpufreq_hw_exit);

MODULE_DESCRIPTION("QCOM CPUFREQ HW Driver");
MODULE_LICENSE("GPL v2");
