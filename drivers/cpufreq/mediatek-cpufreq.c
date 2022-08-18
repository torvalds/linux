// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Linaro Ltd.
 * Author: Pi-Cheng Chen <pi-cheng.chen@linaro.org>
 */

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>

struct mtk_cpufreq_platform_data {
	int min_volt_shift;
	int max_volt_shift;
	int proc_max_volt;
	int sram_min_volt;
	int sram_max_volt;
	bool ccifreq_supported;
};

/*
 * The struct mtk_cpu_dvfs_info holds necessary information for doing CPU DVFS
 * on each CPU power/clock domain of Mediatek SoCs. Each CPU cluster in
 * Mediatek SoCs has two voltage inputs, Vproc and Vsram. In some cases the two
 * voltage inputs need to be controlled under a hardware limitation:
 * 100mV < Vsram - Vproc < 200mV
 *
 * When scaling the clock frequency of a CPU clock domain, the clock source
 * needs to be switched to another stable PLL clock temporarily until
 * the original PLL becomes stable at target frequency.
 */
struct mtk_cpu_dvfs_info {
	struct cpumask cpus;
	struct device *cpu_dev;
	struct device *cci_dev;
	struct regulator *proc_reg;
	struct regulator *sram_reg;
	struct clk *cpu_clk;
	struct clk *inter_clk;
	struct list_head list_head;
	int intermediate_voltage;
	bool need_voltage_tracking;
	int vproc_on_boot;
	int pre_vproc;
	/* Avoid race condition for regulators between notify and policy */
	struct mutex reg_lock;
	struct notifier_block opp_nb;
	unsigned int opp_cpu;
	unsigned long current_freq;
	const struct mtk_cpufreq_platform_data *soc_data;
	int vtrack_max;
	bool ccifreq_bound;
};

static struct platform_device *cpufreq_pdev;

static LIST_HEAD(dvfs_info_list);

static struct mtk_cpu_dvfs_info *mtk_cpu_dvfs_info_lookup(int cpu)
{
	struct mtk_cpu_dvfs_info *info;

	list_for_each_entry(info, &dvfs_info_list, list_head) {
		if (cpumask_test_cpu(cpu, &info->cpus))
			return info;
	}

	return NULL;
}

static int mtk_cpufreq_voltage_tracking(struct mtk_cpu_dvfs_info *info,
					int new_vproc)
{
	const struct mtk_cpufreq_platform_data *soc_data = info->soc_data;
	struct regulator *proc_reg = info->proc_reg;
	struct regulator *sram_reg = info->sram_reg;
	int pre_vproc, pre_vsram, new_vsram, vsram, vproc, ret;
	int retry = info->vtrack_max;

	pre_vproc = regulator_get_voltage(proc_reg);
	if (pre_vproc < 0) {
		dev_err(info->cpu_dev,
			"invalid Vproc value: %d\n", pre_vproc);
		return pre_vproc;
	}

	pre_vsram = regulator_get_voltage(sram_reg);
	if (pre_vsram < 0) {
		dev_err(info->cpu_dev, "invalid Vsram value: %d\n", pre_vsram);
		return pre_vsram;
	}

	new_vsram = clamp(new_vproc + soc_data->min_volt_shift,
			  soc_data->sram_min_volt, soc_data->sram_max_volt);

	do {
		if (pre_vproc <= new_vproc) {
			vsram = clamp(pre_vproc + soc_data->max_volt_shift,
				      soc_data->sram_min_volt, new_vsram);
			ret = regulator_set_voltage(sram_reg, vsram,
						    soc_data->sram_max_volt);

			if (ret)
				return ret;

			if (vsram == soc_data->sram_max_volt ||
			    new_vsram == soc_data->sram_min_volt)
				vproc = new_vproc;
			else
				vproc = vsram - soc_data->min_volt_shift;

			ret = regulator_set_voltage(proc_reg, vproc,
						    soc_data->proc_max_volt);
			if (ret) {
				regulator_set_voltage(sram_reg, pre_vsram,
						      soc_data->sram_max_volt);
				return ret;
			}
		} else if (pre_vproc > new_vproc) {
			vproc = max(new_vproc,
				    pre_vsram - soc_data->max_volt_shift);
			ret = regulator_set_voltage(proc_reg, vproc,
						    soc_data->proc_max_volt);
			if (ret)
				return ret;

			if (vproc == new_vproc)
				vsram = new_vsram;
			else
				vsram = max(new_vsram,
					    vproc + soc_data->min_volt_shift);

			ret = regulator_set_voltage(sram_reg, vsram,
						    soc_data->sram_max_volt);
			if (ret) {
				regulator_set_voltage(proc_reg, pre_vproc,
						      soc_data->proc_max_volt);
				return ret;
			}
		}

		pre_vproc = vproc;
		pre_vsram = vsram;

		if (--retry < 0) {
			dev_err(info->cpu_dev,
				"over loop count, failed to set voltage\n");
			return -EINVAL;
		}
	} while (vproc != new_vproc || vsram != new_vsram);

	return 0;
}

static int mtk_cpufreq_set_voltage(struct mtk_cpu_dvfs_info *info, int vproc)
{
	const struct mtk_cpufreq_platform_data *soc_data = info->soc_data;
	int ret;

	if (info->need_voltage_tracking)
		ret = mtk_cpufreq_voltage_tracking(info, vproc);
	else
		ret = regulator_set_voltage(info->proc_reg, vproc,
					    soc_data->proc_max_volt);
	if (!ret)
		info->pre_vproc = vproc;

	return ret;
}

static bool is_ccifreq_ready(struct mtk_cpu_dvfs_info *info)
{
	struct device_link *sup_link;

	if (info->ccifreq_bound)
		return true;

	sup_link = device_link_add(info->cpu_dev, info->cci_dev,
				   DL_FLAG_AUTOREMOVE_CONSUMER);
	if (!sup_link) {
		dev_err(info->cpu_dev, "cpu%d: sup_link is NULL\n", info->opp_cpu);
		return false;
	}

	if (sup_link->supplier->links.status != DL_DEV_DRIVER_BOUND)
		return false;

	info->ccifreq_bound = true;

	return true;
}

static int mtk_cpufreq_set_target(struct cpufreq_policy *policy,
				  unsigned int index)
{
	struct cpufreq_frequency_table *freq_table = policy->freq_table;
	struct clk *cpu_clk = policy->clk;
	struct clk *armpll = clk_get_parent(cpu_clk);
	struct mtk_cpu_dvfs_info *info = policy->driver_data;
	struct device *cpu_dev = info->cpu_dev;
	struct dev_pm_opp *opp;
	long freq_hz, pre_freq_hz;
	int vproc, pre_vproc, inter_vproc, target_vproc, ret;

	inter_vproc = info->intermediate_voltage;

	pre_freq_hz = clk_get_rate(cpu_clk);

	mutex_lock(&info->reg_lock);

	if (unlikely(info->pre_vproc <= 0))
		pre_vproc = regulator_get_voltage(info->proc_reg);
	else
		pre_vproc = info->pre_vproc;

	if (pre_vproc < 0) {
		dev_err(cpu_dev, "invalid Vproc value: %d\n", pre_vproc);
		ret = pre_vproc;
		goto out;
	}

	freq_hz = freq_table[index].frequency * 1000;

	opp = dev_pm_opp_find_freq_ceil(cpu_dev, &freq_hz);
	if (IS_ERR(opp)) {
		dev_err(cpu_dev, "cpu%d: failed to find OPP for %ld\n",
			policy->cpu, freq_hz);
		ret = PTR_ERR(opp);
		goto out;
	}
	vproc = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	/*
	 * If MediaTek cci is supported but is not ready, we will use the value
	 * of max(target cpu voltage, booting voltage) to prevent high freqeuncy
	 * low voltage crash.
	 */
	if (info->soc_data->ccifreq_supported && !is_ccifreq_ready(info))
		vproc = max(vproc, info->vproc_on_boot);

	/*
	 * If the new voltage or the intermediate voltage is higher than the
	 * current voltage, scale up voltage first.
	 */
	target_vproc = max(inter_vproc, vproc);
	if (pre_vproc <= target_vproc) {
		ret = mtk_cpufreq_set_voltage(info, target_vproc);
		if (ret) {
			dev_err(cpu_dev,
				"cpu%d: failed to scale up voltage!\n", policy->cpu);
			mtk_cpufreq_set_voltage(info, pre_vproc);
			goto out;
		}
	}

	/* Reparent the CPU clock to intermediate clock. */
	ret = clk_set_parent(cpu_clk, info->inter_clk);
	if (ret) {
		dev_err(cpu_dev,
			"cpu%d: failed to re-parent cpu clock!\n", policy->cpu);
		mtk_cpufreq_set_voltage(info, pre_vproc);
		goto out;
	}

	/* Set the original PLL to target rate. */
	ret = clk_set_rate(armpll, freq_hz);
	if (ret) {
		dev_err(cpu_dev,
			"cpu%d: failed to scale cpu clock rate!\n", policy->cpu);
		clk_set_parent(cpu_clk, armpll);
		mtk_cpufreq_set_voltage(info, pre_vproc);
		goto out;
	}

	/* Set parent of CPU clock back to the original PLL. */
	ret = clk_set_parent(cpu_clk, armpll);
	if (ret) {
		dev_err(cpu_dev,
			"cpu%d: failed to re-parent cpu clock!\n", policy->cpu);
		mtk_cpufreq_set_voltage(info, inter_vproc);
		goto out;
	}

	/*
	 * If the new voltage is lower than the intermediate voltage or the
	 * original voltage, scale down to the new voltage.
	 */
	if (vproc < inter_vproc || vproc < pre_vproc) {
		ret = mtk_cpufreq_set_voltage(info, vproc);
		if (ret) {
			dev_err(cpu_dev,
				"cpu%d: failed to scale down voltage!\n", policy->cpu);
			clk_set_parent(cpu_clk, info->inter_clk);
			clk_set_rate(armpll, pre_freq_hz);
			clk_set_parent(cpu_clk, armpll);
			goto out;
		}
	}

	info->current_freq = freq_hz;

out:
	mutex_unlock(&info->reg_lock);

	return ret;
}

#define DYNAMIC_POWER "dynamic-power-coefficient"

static int mtk_cpufreq_opp_notifier(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	struct dev_pm_opp *opp = data;
	struct dev_pm_opp *new_opp;
	struct mtk_cpu_dvfs_info *info;
	unsigned long freq, volt;
	struct cpufreq_policy *policy;
	int ret = 0;

	info = container_of(nb, struct mtk_cpu_dvfs_info, opp_nb);

	if (event == OPP_EVENT_ADJUST_VOLTAGE) {
		freq = dev_pm_opp_get_freq(opp);

		mutex_lock(&info->reg_lock);
		if (info->current_freq == freq) {
			volt = dev_pm_opp_get_voltage(opp);
			ret = mtk_cpufreq_set_voltage(info, volt);
			if (ret)
				dev_err(info->cpu_dev,
					"failed to scale voltage: %d\n", ret);
		}
		mutex_unlock(&info->reg_lock);
	} else if (event == OPP_EVENT_DISABLE) {
		freq = dev_pm_opp_get_freq(opp);

		/* case of current opp item is disabled */
		if (info->current_freq == freq) {
			freq = 1;
			new_opp = dev_pm_opp_find_freq_ceil(info->cpu_dev,
							    &freq);
			if (IS_ERR(new_opp)) {
				dev_err(info->cpu_dev,
					"all opp items are disabled\n");
				ret = PTR_ERR(new_opp);
				return notifier_from_errno(ret);
			}

			dev_pm_opp_put(new_opp);
			policy = cpufreq_cpu_get(info->opp_cpu);
			if (policy) {
				cpufreq_driver_target(policy, freq / 1000,
						      CPUFREQ_RELATION_L);
				cpufreq_cpu_put(policy);
			}
		}
	}

	return notifier_from_errno(ret);
}

static struct device *of_get_cci(struct device *cpu_dev)
{
	struct device_node *np;
	struct platform_device *pdev;

	np = of_parse_phandle(cpu_dev->of_node, "mediatek,cci", 0);
	if (IS_ERR_OR_NULL(np))
		return NULL;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (IS_ERR_OR_NULL(pdev))
		return NULL;

	return &pdev->dev;
}

static int mtk_cpu_dvfs_info_init(struct mtk_cpu_dvfs_info *info, int cpu)
{
	struct device *cpu_dev;
	struct dev_pm_opp *opp;
	unsigned long rate;
	int ret;

	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev) {
		dev_err(cpu_dev, "failed to get cpu%d device\n", cpu);
		return -ENODEV;
	}
	info->cpu_dev = cpu_dev;

	info->ccifreq_bound = false;
	if (info->soc_data->ccifreq_supported) {
		info->cci_dev = of_get_cci(info->cpu_dev);
		if (IS_ERR_OR_NULL(info->cci_dev)) {
			ret = PTR_ERR(info->cci_dev);
			dev_err(cpu_dev, "cpu%d: failed to get cci device\n", cpu);
			return -ENODEV;
		}
	}

	info->cpu_clk = clk_get(cpu_dev, "cpu");
	if (IS_ERR(info->cpu_clk)) {
		ret = PTR_ERR(info->cpu_clk);
		return dev_err_probe(cpu_dev, ret,
				     "cpu%d: failed to get cpu clk\n", cpu);
	}

	info->inter_clk = clk_get(cpu_dev, "intermediate");
	if (IS_ERR(info->inter_clk)) {
		ret = PTR_ERR(info->inter_clk);
		dev_err_probe(cpu_dev, ret,
			      "cpu%d: failed to get intermediate clk\n", cpu);
		goto out_free_resources;
	}

	info->proc_reg = regulator_get_optional(cpu_dev, "proc");
	if (IS_ERR(info->proc_reg)) {
		ret = PTR_ERR(info->proc_reg);
		dev_err_probe(cpu_dev, ret,
			      "cpu%d: failed to get proc regulator\n", cpu);
		goto out_free_resources;
	}

	ret = regulator_enable(info->proc_reg);
	if (ret) {
		dev_warn(cpu_dev, "cpu%d: failed to enable vproc\n", cpu);
		goto out_free_resources;
	}

	/* Both presence and absence of sram regulator are valid cases. */
	info->sram_reg = regulator_get_optional(cpu_dev, "sram");
	if (IS_ERR(info->sram_reg)) {
		ret = PTR_ERR(info->sram_reg);
		if (ret == -EPROBE_DEFER)
			goto out_free_resources;

		info->sram_reg = NULL;
	} else {
		ret = regulator_enable(info->sram_reg);
		if (ret) {
			dev_warn(cpu_dev, "cpu%d: failed to enable vsram\n", cpu);
			goto out_free_resources;
		}
	}

	/* Get OPP-sharing information from "operating-points-v2" bindings */
	ret = dev_pm_opp_of_get_sharing_cpus(cpu_dev, &info->cpus);
	if (ret) {
		dev_err(cpu_dev,
			"cpu%d: failed to get OPP-sharing information\n", cpu);
		goto out_free_resources;
	}

	ret = dev_pm_opp_of_cpumask_add_table(&info->cpus);
	if (ret) {
		dev_warn(cpu_dev, "cpu%d: no OPP table\n", cpu);
		goto out_free_resources;
	}

	ret = clk_prepare_enable(info->cpu_clk);
	if (ret)
		goto out_free_opp_table;

	ret = clk_prepare_enable(info->inter_clk);
	if (ret)
		goto out_disable_mux_clock;

	if (info->soc_data->ccifreq_supported) {
		info->vproc_on_boot = regulator_get_voltage(info->proc_reg);
		if (info->vproc_on_boot < 0) {
			dev_err(info->cpu_dev,
				"invalid Vproc value: %d\n", info->vproc_on_boot);
			goto out_disable_inter_clock;
		}
	}

	/* Search a safe voltage for intermediate frequency. */
	rate = clk_get_rate(info->inter_clk);
	opp = dev_pm_opp_find_freq_ceil(cpu_dev, &rate);
	if (IS_ERR(opp)) {
		dev_err(cpu_dev, "cpu%d: failed to get intermediate opp\n", cpu);
		ret = PTR_ERR(opp);
		goto out_disable_inter_clock;
	}
	info->intermediate_voltage = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	mutex_init(&info->reg_lock);
	info->current_freq = clk_get_rate(info->cpu_clk);

	info->opp_cpu = cpu;
	info->opp_nb.notifier_call = mtk_cpufreq_opp_notifier;
	ret = dev_pm_opp_register_notifier(cpu_dev, &info->opp_nb);
	if (ret) {
		dev_err(cpu_dev, "cpu%d: failed to register opp notifier\n", cpu);
		goto out_disable_inter_clock;
	}

	/*
	 * If SRAM regulator is present, software "voltage tracking" is needed
	 * for this CPU power domain.
	 */
	info->need_voltage_tracking = (info->sram_reg != NULL);

	/*
	 * We assume min voltage is 0 and tracking target voltage using
	 * min_volt_shift for each iteration.
	 * The vtrack_max is 3 times of expeted iteration count.
	 */
	info->vtrack_max = 3 * DIV_ROUND_UP(max(info->soc_data->sram_max_volt,
						info->soc_data->proc_max_volt),
					    info->soc_data->min_volt_shift);

	return 0;

out_disable_inter_clock:
	clk_disable_unprepare(info->inter_clk);

out_disable_mux_clock:
	clk_disable_unprepare(info->cpu_clk);

out_free_opp_table:
	dev_pm_opp_of_cpumask_remove_table(&info->cpus);

out_free_resources:
	if (regulator_is_enabled(info->proc_reg))
		regulator_disable(info->proc_reg);
	if (info->sram_reg && regulator_is_enabled(info->sram_reg))
		regulator_disable(info->sram_reg);

	if (!IS_ERR(info->proc_reg))
		regulator_put(info->proc_reg);
	if (!IS_ERR(info->sram_reg))
		regulator_put(info->sram_reg);
	if (!IS_ERR(info->cpu_clk))
		clk_put(info->cpu_clk);
	if (!IS_ERR(info->inter_clk))
		clk_put(info->inter_clk);

	return ret;
}

static void mtk_cpu_dvfs_info_release(struct mtk_cpu_dvfs_info *info)
{
	if (!IS_ERR(info->proc_reg)) {
		regulator_disable(info->proc_reg);
		regulator_put(info->proc_reg);
	}
	if (!IS_ERR(info->sram_reg)) {
		regulator_disable(info->sram_reg);
		regulator_put(info->sram_reg);
	}
	if (!IS_ERR(info->cpu_clk)) {
		clk_disable_unprepare(info->cpu_clk);
		clk_put(info->cpu_clk);
	}
	if (!IS_ERR(info->inter_clk)) {
		clk_disable_unprepare(info->inter_clk);
		clk_put(info->inter_clk);
	}

	dev_pm_opp_of_cpumask_remove_table(&info->cpus);
	dev_pm_opp_unregister_notifier(info->cpu_dev, &info->opp_nb);
}

static int mtk_cpufreq_init(struct cpufreq_policy *policy)
{
	struct mtk_cpu_dvfs_info *info;
	struct cpufreq_frequency_table *freq_table;
	int ret;

	info = mtk_cpu_dvfs_info_lookup(policy->cpu);
	if (!info) {
		pr_err("dvfs info for cpu%d is not initialized.\n",
			policy->cpu);
		return -EINVAL;
	}

	ret = dev_pm_opp_init_cpufreq_table(info->cpu_dev, &freq_table);
	if (ret) {
		dev_err(info->cpu_dev,
			"failed to init cpufreq table for cpu%d: %d\n",
			policy->cpu, ret);
		return ret;
	}

	cpumask_copy(policy->cpus, &info->cpus);
	policy->freq_table = freq_table;
	policy->driver_data = info;
	policy->clk = info->cpu_clk;

	return 0;
}

static int mtk_cpufreq_exit(struct cpufreq_policy *policy)
{
	struct mtk_cpu_dvfs_info *info = policy->driver_data;

	dev_pm_opp_free_cpufreq_table(info->cpu_dev, &policy->freq_table);

	return 0;
}

static struct cpufreq_driver mtk_cpufreq_driver = {
	.flags = CPUFREQ_NEED_INITIAL_FREQ_CHECK |
		 CPUFREQ_HAVE_GOVERNOR_PER_POLICY |
		 CPUFREQ_IS_COOLING_DEV,
	.verify = cpufreq_generic_frequency_table_verify,
	.target_index = mtk_cpufreq_set_target,
	.get = cpufreq_generic_get,
	.init = mtk_cpufreq_init,
	.exit = mtk_cpufreq_exit,
	.register_em = cpufreq_register_em_with_opp,
	.name = "mtk-cpufreq",
	.attr = cpufreq_generic_attr,
};

static int mtk_cpufreq_probe(struct platform_device *pdev)
{
	const struct mtk_cpufreq_platform_data *data;
	struct mtk_cpu_dvfs_info *info, *tmp;
	int cpu, ret;

	data = dev_get_platdata(&pdev->dev);
	if (!data) {
		dev_err(&pdev->dev,
			"failed to get mtk cpufreq platform data\n");
		return -ENODEV;
	}

	for_each_possible_cpu(cpu) {
		info = mtk_cpu_dvfs_info_lookup(cpu);
		if (info)
			continue;

		info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
		if (!info) {
			ret = -ENOMEM;
			goto release_dvfs_info_list;
		}

		info->soc_data = data;
		ret = mtk_cpu_dvfs_info_init(info, cpu);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to initialize dvfs info for cpu%d\n",
				cpu);
			goto release_dvfs_info_list;
		}

		list_add(&info->list_head, &dvfs_info_list);
	}

	ret = cpufreq_register_driver(&mtk_cpufreq_driver);
	if (ret) {
		dev_err(&pdev->dev, "failed to register mtk cpufreq driver\n");
		goto release_dvfs_info_list;
	}

	return 0;

release_dvfs_info_list:
	list_for_each_entry_safe(info, tmp, &dvfs_info_list, list_head) {
		mtk_cpu_dvfs_info_release(info);
		list_del(&info->list_head);
	}

	return ret;
}

static struct platform_driver mtk_cpufreq_platdrv = {
	.driver = {
		.name	= "mtk-cpufreq",
	},
	.probe		= mtk_cpufreq_probe,
};

static const struct mtk_cpufreq_platform_data mt2701_platform_data = {
	.min_volt_shift = 100000,
	.max_volt_shift = 200000,
	.proc_max_volt = 1150000,
	.sram_min_volt = 0,
	.sram_max_volt = 1150000,
	.ccifreq_supported = false,
};

static const struct mtk_cpufreq_platform_data mt8183_platform_data = {
	.min_volt_shift = 100000,
	.max_volt_shift = 200000,
	.proc_max_volt = 1150000,
	.sram_min_volt = 0,
	.sram_max_volt = 1150000,
	.ccifreq_supported = true,
};

static const struct mtk_cpufreq_platform_data mt8186_platform_data = {
	.min_volt_shift = 100000,
	.max_volt_shift = 250000,
	.proc_max_volt = 1118750,
	.sram_min_volt = 850000,
	.sram_max_volt = 1118750,
	.ccifreq_supported = true,
};

/* List of machines supported by this driver */
static const struct of_device_id mtk_cpufreq_machines[] __initconst = {
	{ .compatible = "mediatek,mt2701", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt2712", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt7622", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt7623", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt8167", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt817x", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt8173", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt8176", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt8183", .data = &mt8183_platform_data },
	{ .compatible = "mediatek,mt8186", .data = &mt8186_platform_data },
	{ .compatible = "mediatek,mt8365", .data = &mt2701_platform_data },
	{ .compatible = "mediatek,mt8516", .data = &mt2701_platform_data },
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_cpufreq_machines);

static int __init mtk_cpufreq_driver_init(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	const struct mtk_cpufreq_platform_data *data;
	int err;

	np = of_find_node_by_path("/");
	if (!np)
		return -ENODEV;

	match = of_match_node(mtk_cpufreq_machines, np);
	of_node_put(np);
	if (!match) {
		pr_debug("Machine is not compatible with mtk-cpufreq\n");
		return -ENODEV;
	}
	data = match->data;

	err = platform_driver_register(&mtk_cpufreq_platdrv);
	if (err)
		return err;

	/*
	 * Since there's no place to hold device registration code and no
	 * device tree based way to match cpufreq driver yet, both the driver
	 * and the device registration codes are put here to handle defer
	 * probing.
	 */
	cpufreq_pdev = platform_device_register_data(NULL, "mtk-cpufreq", -1,
						     data, sizeof(*data));
	if (IS_ERR(cpufreq_pdev)) {
		pr_err("failed to register mtk-cpufreq platform device\n");
		platform_driver_unregister(&mtk_cpufreq_platdrv);
		return PTR_ERR(cpufreq_pdev);
	}

	return 0;
}
module_init(mtk_cpufreq_driver_init)

static void __exit mtk_cpufreq_driver_exit(void)
{
	platform_device_unregister(cpufreq_pdev);
	platform_driver_unregister(&mtk_cpufreq_platdrv);
}
module_exit(mtk_cpufreq_driver_exit)

MODULE_DESCRIPTION("MediaTek CPUFreq driver");
MODULE_AUTHOR("Pi-Cheng Chen <pi-cheng.chen@linaro.org>");
MODULE_LICENSE("GPL v2");
