/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/bitfield.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#define REG_APM_DLY_CNT			0xac
#define APM_SEL_SWITCH_DLY_MASK		GENMASK(7, 0)
#define APM_RESUME_CLK_DLY_MASK		GENMASK(15, 8)
#define APM_HALT_CLK_DLY_MASK		GENMASK(23, 16)
#define APM_POST_HALT_DLY_MASK		GENMASK(31, 24)

#define REG_APM_MODE			0xa8
#define APM_MODE_MASK			GENMASK(1, 0)
#define APM_MODE_MX			0
#define APM_MODE_APCC			2

#define REG_APM_STS			0xb0
#define APM_STS_MASK			GENMASK(4, 0)
#define APM_STS_MX			0
#define APM_STS_APCC			3

#define NUM_FUSE_REVS			8
#define NUM_SPEED_BINS			8
#define NUM_CPUS			8
#define NUM_CPUS_IN_CLUSTER		4

#define CPR_NUM_REF_POINTS		4
#define CPR_INIT_VOLTAGE_STEP_UV	10000
#define CPR_VOLT_FLOOR_UV		500000U
#define CPR_VOLT_CEILING_UV		1065000U
#define CPR_FUSE_OFFSET_SIGN		BIT(5)
#define CPR_FUSE_OFFSET_MASK		GENMASK(4, 0)
#define CPR_PD_COUNT			2

/* Can't query this from the regulator because it has multiple ranges */
#define VREG_STEP_UV			5000

#define to_cpr_pd(gpd) container_of(gpd, struct cpr_pd, pd)

struct cpr_fuse_desc {
	u8 offset, shift;
};

struct cpr_pd_info {
	const struct cpr_fuse_desc init_volt_fuses[CPR_NUM_REF_POINTS];
	u16 ref_pstates[CPR_NUM_REF_POINTS];
	u16 ref_mvolts[CPR_NUM_REF_POINTS];
	u16 ref_mv_adj_bins_mask;
	s16 ref_mv_adj_by_rev[NUM_FUSE_REVS][CPR_NUM_REF_POINTS];
	u16 max_pstate_override_bin_mask;
	u16 max_pstate_override;
};

struct cpr_info {
	/*
	 * Array Power Mux threshold voltage. According to downstream
	 * we should switch APM supply when APC regulator crosses
	 * this level.
	 */
	u32 apm_thr_uv;
	/*
	 * MSM8953 and SDM632 use completely different regions and 
	 * configuration for Memory Accelerator. It's not clear what
	 * those registers on SDM632 do but region is located in APCS
	 * while on MSM8953 it's in TCSR and switches supply.
	 */
	bool acc_use_apcs;
	const struct cpr_pd_info *pds[CPR_PD_COUNT];
};

static const struct cpr_pd_info msm8953_pd_info = {
	.init_volt_fuses = {{ 71, 24 }, { 71, 18 }, { 71, 12 }, { 71, 6 }},
	.ref_pstates  = { 6528, 10368, 16896, 20160 },
	.ref_mvolts = { 645, 720, 865, 1065 },
	/* Bin 0 and 7 use 2208MHz for last reference point */
	.max_pstate_override_bin_mask = BIT(0) | BIT(7),
	.max_pstate_override = 22080,
	/* Open-loop voltage adjustment for speed bins 0, 2, 6, 7
	 * with fusing revisions of 1-3 */
	.ref_mv_adj_bins_mask = BIT(0) | BIT(2) | BIT(6) | BIT(7),
	.ref_mv_adj_by_rev = {
		[1] = { 25, 0, 5, 40 },
		[2] = { 25, 0, 5, 40 },
		[3] = { 25, 0, 5, 40 },
	},
};

static const struct cpr_info msm8953_info = {
	.acc_use_apcs = false,
	.apm_thr_uv = 850000,
	/* We always provide 2 domains but on MSM8953 one is an alias
	 * to the first one */
	.pds = { &msm8953_pd_info, NULL },
};

static const struct cpr_pd_info sdm632_pwr_pd_info = {
	.init_volt_fuses = {{ 74, 18 }, { 71, 24 }, { 74, 6 }, { 74, 0 }},
	.ref_pstates = { 6144, 10368, 13632, 18048 },
	.ref_mvolts = { 645, 790, 865, 1075 },
};

static const struct cpr_pd_info sdm632_perf_pd_info = {
	.init_volt_fuses = {{ 74, 18 }, { 71, 18 }, { 71, 12 }, { 71, 6 }},
	.ref_pstates = { 6336, 10944, 14016, 20160 },
	.ref_mvolts = { 645, 790, 865, 1065 },
	/* Open-loop voltage adjustment for speed bins 0, 2, 6
	 * with fusing revisions of 0-2 */
	.ref_mv_adj_bins_mask = BIT(0) | BIT(2) | BIT(6),
	.ref_mv_adj_by_rev = {
		[0] = { 30, 0, 10, 20 },
		[1] = { 30, 0, 10, 20 },
		[2] = {  0, 0, 10, 20 },
	},
};

static const struct cpr_info sdm632_info = {
	.acc_use_apcs = true,
	.apm_thr_uv = 875000,
	.pds = { &sdm632_pwr_pd_info, &sdm632_perf_pd_info },
};

static const struct of_device_id soc_match_table[] = {
	{ .compatible = "qcom,msm8953", .data = &msm8953_info },
	{ .compatible = "qcom,sdm450", .data = &msm8953_info },
	{ .compatible = "qcom,sdm632", .data = &sdm632_info },
	{},
};

struct cpr_pd_data {
	u32 pstates[CPR_NUM_REF_POINTS];
	u32 uV[CPR_NUM_REF_POINTS];
	u32 uV_step[CPR_NUM_REF_POINTS-1];
};

struct cpr_pd {
	const struct cpr_pd_data *data;
	struct generic_pm_domain pd;
	/* Votes from PM domain consumers */
	u32 uv, pstate;
};

struct cpr_drv {
	const struct cpr_info *info;
	struct generic_pm_domain *pds[CPR_PD_COUNT];
	struct genpd_onecell_data cell_data;
	struct notifier_block policy_nb;
	struct regulator *vreg;
	struct device *dev;
	struct mutex lock;
	/* Array Power Mux and Memory Accelerator mappings */
	void __iomem *apm, *acc;
	/* Floor voltage used during boot until consumers are synced */
	u32 boot_up_uv;
	/* Active (aggregated) parameters */
	u32 uv, pstate;
};

static void cpr_configure_mem_acc(struct cpr_drv *drv, u32 pstate)
{
	if (drv->info->acc_use_apcs) {
		u32 regs[5] = {0};
		int i;

		if (pstate <= 9600) {
			regs[1] = regs[4] = BIT(31);
		} else if (pstate >= 17400) {
			regs[1] = 1;
			regs[3] = BIT(16);
		}

		for (i = 0; i < ARRAY_SIZE(regs); i++) {
			writel_relaxed(regs[i], drv->acc + 4 * i);
			mb();
		}
	} else {
		u32 val = pstate < 10368;
		writel_relaxed(val, drv->acc);
		writel_relaxed(val, drv->acc + 4);
		mb();
	}
}

static void cpr_apm_init(struct cpr_drv *drv)
{
	u32 val = FIELD_PREP(APM_POST_HALT_DLY_MASK, 0x02)
		| FIELD_PREP(APM_HALT_CLK_DLY_MASK, 0x11)
		| FIELD_PREP(APM_RESUME_CLK_DLY_MASK, 0x10)
		| FIELD_PREP(APM_SEL_SWITCH_DLY_MASK, 0x01);
	writel_relaxed(val, drv->apm + REG_APM_DLY_CNT);
}

static int cpr_apm_switch_supply(struct cpr_drv *drv, bool high)
{
	u32 done_status = high ? APM_STS_APCC : APM_STS_MX;
	u32 val = high ? APM_MODE_APCC : APM_MODE_MX;
	int ret;

	writel_relaxed(val, drv->apm + REG_APM_MODE);
	ret = readl_relaxed_poll_timeout_atomic(drv->apm + REG_APM_STS,
			val, (val & APM_STS_MASK) == done_status, 1, 500);
	if (ret)
		dev_err(drv->dev, "failed to switch APM: %d", ret);

	return ret;
}


static u32 cpr_interpolate_uv(const struct cpr_pd_data *data, u32 pstate)
{
	u32 uVolt, i = 0;
	for (;i < (CPR_NUM_REF_POINTS - 1) && pstate > data->pstates[i + 1]; i++)
		continue;

	pstate = clamp(pstate, data->pstates[i], data->pstates[i + 1]);
	uVolt = data->uV[i] + data->uV_step[i] * (pstate - data->pstates[i]);
	uVolt = clamp(uVolt, data->uV[i], data->uV[i + 1]);
	uVolt = roundup(uVolt, VREG_STEP_UV);
	return clamp(uVolt, CPR_VOLT_FLOOR_UV, CPR_VOLT_CEILING_UV);
}

static int cpr_vreg_set_voltage(struct cpr_drv *drv, u32 uv)
{
	u32 apm_thr_uv = drv->info->apm_thr_uv;
	int ret;

	/* Switch APM when crossing its threshold voltage */
	apm_thr_uv = clamp(apm_thr_uv, min(uv, drv->uv), max(uv, drv->uv));
	if (apm_thr_uv == drv->info->apm_thr_uv) {
		ret = regulator_set_voltage(drv->vreg, apm_thr_uv, apm_thr_uv);
		if (ret)
			return ret;

		drv->uv = apm_thr_uv;
		ret = cpr_apm_switch_supply(drv, uv > apm_thr_uv);
		if (ret)
			return ret;
	}

	ret = regulator_set_voltage(drv->vreg, uv, uv);
	if (!ret)
		drv->uv = uv;

	return ret;
}

static int cpr_pd_set_pstate_unlocked(struct cpr_drv *drv, struct cpr_pd *cpd,
				      unsigned int pstate)
{
	u32 pstate_aggr, uv_aggr, uv, uv_prev = drv->uv;;
	int ret, i;

	lockdep_assert_held(&drv->lock);

	pstate_aggr = pstate;
	uv_aggr = uv = cpr_interpolate_uv(cpd->data, pstate);

	for (i = 0; i < CPR_PD_COUNT; i ++) {
		struct cpr_pd *other = to_cpr_pd(drv->pds[i]);
		if (other != cpd) {
			uv_aggr = max(other->uv, uv_aggr);
			pstate_aggr = max(other->pstate, pstate_aggr);
		}
	}

	uv_aggr = max(uv_aggr, drv->boot_up_uv);

	if (pstate_aggr == drv->pstate && uv_aggr == uv_prev)
		goto skip_update;

	if (uv_aggr < uv_prev)
		cpr_configure_mem_acc(drv, pstate_aggr);

	ret = cpr_vreg_set_voltage(drv, uv_aggr);
	if (ret)
		goto fail_restore_prev;

	if (uv_aggr > uv_prev)
		cpr_configure_mem_acc(drv, pstate_aggr);

skip_update:
	cpd->uv = uv;
	cpd->pstate = pstate;
	drv->pstate = pstate_aggr;
	return 0;

fail_restore_prev:
	dev_err(drv->dev, "failed to set voltage %u uV: %d\n", uv, ret);

	if (uv_aggr < uv_prev)
		cpr_configure_mem_acc(drv, drv->pstate);
	cpr_vreg_set_voltage(drv, uv_prev);
	return ret;
}


static int cpr_pd_set_pstate(struct generic_pm_domain *domain,
			     unsigned int pstate)
{
	struct cpr_drv *drv = dev_get_drvdata(domain->dev.parent);
	struct cpr_pd *cpd = to_cpr_pd(domain);
	int ret;

	mutex_lock(&drv->lock);
	ret = cpr_pd_set_pstate_unlocked(drv, cpd, pstate);
	mutex_unlock(&drv->lock);
	return ret;
}

static void cpr_remove_domain(void *data)
{
	pm_genpd_remove((struct generic_pm_domain *) data);
}

static int cpr_apply_fuse_offset(struct nvmem_device *nvmem,
				 const struct cpr_fuse_desc *desc,
				 u32* value, u32 step)
{
	u64 fuse;
	int ret;

	ret = nvmem_device_read(nvmem, 8 * desc->offset, sizeof(fuse), &fuse);
	if (ret < sizeof(fuse))
		return ret < 0 ? ret : -ENODATA;

	fuse >>= desc->shift;
	if (fuse & CPR_FUSE_OFFSET_SIGN)
		*value -= step * (fuse & CPR_FUSE_OFFSET_MASK);
	else
		*value += step * (fuse & CPR_FUSE_OFFSET_MASK);

	return 0;
}

static int cpr_init_domain(struct cpr_drv *drv, unsigned int index)
{
	const struct cpr_pd_info *info = drv->info->pds[index];
	struct device *dev = drv->dev;
	struct nvmem_device *nvmem;
	u32 speed_bin, fusing_rev;
	struct cpr_pd_data *data;
	struct cpr_pd *cpd;
	int ret, i;

	if (!info) {
		drv->pds[index] = drv->pds[index - 1];
		return 0;
	}

	cpd = devm_kzalloc(dev, sizeof(*cpd), GFP_KERNEL);
	if (!cpd)
		return -ENOMEM;

	cpd->data = data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	drv->pds[index] = &cpd->pd;
	cpd->pd.name = index ? "cpr_pd_perf" : "cpr_pd_pwr";
	cpd->pd.set_performance_state = cpr_pd_set_pstate;
	cpd->pd.dev.parent = dev;

	ret = pm_genpd_init(&cpd->pd, NULL, false);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, cpr_remove_domain, &cpd->pd);
	if (ret)
		return ret;

	ret = nvmem_cell_read_variable_le_u32(dev, "speed_bin", &speed_bin);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read speed bin\n");

	nvmem = devm_nvmem_device_get(dev, NULL);
	if (IS_ERR(nvmem))
		return dev_err_probe(dev, ret, "failed to get nvmem device\n");

	ret = nvmem_cell_read_variable_le_u32(dev, "fusing_rev", &fusing_rev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read fusing revision\n");

	if (!index)
		dev_info(dev, "speed_bin=%d fusing_revision=%d\n",
			 speed_bin, fusing_rev);

	for (i = 0; i < CPR_NUM_REF_POINTS; i ++) {
		u32 uVolt, pstate;

		pstate = info->ref_pstates[i];
		uVolt = info->ref_mvolts[i] * 1000;

		ret = cpr_apply_fuse_offset(nvmem, &info->init_volt_fuses[i],
					    &uVolt, CPR_INIT_VOLTAGE_STEP_UV);
		if (ret < 0)
			return ret;

		/* Apply voltage adjustment per bin/revision */
		if (info->ref_mv_adj_bins_mask & BIT(speed_bin))
			uVolt += info->ref_mv_adj_by_rev[fusing_rev][i] * 1000;

		data->pstates[i] = pstate;
		data->uV[i] = uVolt;
	}

	/* We don't need nvmem device anymore */
	devm_nvmem_device_put(dev, nvmem);

	if (info->max_pstate_override_bin_mask & BIT(speed_bin))
		data->pstates[CPR_NUM_REF_POINTS - 1] = info->max_pstate_override;

	for (i = 0; i < (CPR_NUM_REF_POINTS - 1); i++) {
		/* Ensure that voltages are in ascending order */
		data->uV[i + 1] = max(data->uV[i + 1], data->uV[i]);

		/* Pre-divide voltage for interpolation */
		data->uV_step[i] = (data->uV[i + 1] - data->uV[i])
				 / (data->pstates[i + 1] - data->pstates[i]);
		dev_info(&cpd->pd.dev, "level=%5u voltage=%6u uV step=%2u uV\n",
			 data->pstates[i], data->uV[i], data->uV_step[i]);
	}

	dev_info(&cpd->pd.dev, "level=%5u voltage=%2u uV\n",
		 data->pstates[i], data->uV[i]);

	return 0;
}

/* We need to adjust OPP voltages on CPUs only to make energy model
 * aware of CPU Voltages. We are doing it in this notifier because
 * otherwise we would miss energy model initialization. Also can't do
 * it in attach_dev because OPP table can't be created at that point.
 */
static int cpr_cpufreq_policy_notifier(struct notifier_block *nb,
				       unsigned long action, void *data)
{
	struct cpr_drv *drv = container_of(nb, struct cpr_drv, policy_nb);
	struct cpufreq_policy *policy = data;
	struct dev_pm_opp *opp;
	struct device *dev;
	struct cpr_pd *cpd;
	unsigned long freq;
	unsigned cpu;

	if (action != CPUFREQ_CREATE_POLICY)
		return NOTIFY_OK;

	cpu = cpumask_first(policy->related_cpus);
	dev = get_cpu_device(cpu);
	if (cpu >= NUM_CPUS || IS_ERR_OR_NULL(dev))
		return NOTIFY_OK;

	cpd = to_cpr_pd(drv->pds[cpu / NUM_CPUS_IN_CLUSTER]);
	for (freq = 0, opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	     !IS_ERR_OR_NULL(opp);
	     freq ++, opp = dev_pm_opp_find_freq_ceil(dev, &freq)) {
		unsigned int pstate, uVolt;

		pstate = dev_pm_opp_get_required_pstate(opp, 0);
		dev_pm_opp_put(opp);
		uVolt = cpr_interpolate_uv(cpd->data, pstate);

		dev_info(&cpd->pd.dev, "Freq=%lu Voltage=%u uV\n", freq / 1000, uVolt);

		dev_pm_opp_adjust_voltage(dev, freq, uVolt, uVolt, uVolt);
	}

	return NOTIFY_OK;
}

static void cpr_sync_state(struct device *dev)
{
	struct cpr_drv *drv = dev_get_drvdata(dev);
	struct cpr_pd *cpd = to_cpr_pd(drv->pds[0]);

	mutex_lock(&drv->lock);
	drv->boot_up_uv = 0;
	cpr_pd_set_pstate_unlocked(drv, cpd, cpd->pstate);
	mutex_unlock(&drv->lock);
}

static int cpr_probe(struct platform_device *pdev)
{

	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	const struct cpr_info *info;
	const char *acc_reg_name;
	struct device_node *np;
	struct cpr_drv *drv;
	int index, ret;

	np = of_find_node_by_path("/");
	if (!np)
		return -ENOENT;

	match = of_match_node(soc_match_table, np);
	of_node_put(np);
	if (!match)
		return dev_err_probe(dev, -EINVAL,
				"couldn't match SoC compatible\n");
	drv = devm_kzalloc(dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	dev_set_drvdata(dev, drv);
	mutex_init(&drv->lock);
	drv->dev = dev;
	drv->info = info = match->data;
	drv->policy_nb.notifier_call = cpr_cpufreq_policy_notifier;
	drv->cell_data.domains = drv->pds;
	drv->cell_data.num_domains = CPR_PD_COUNT;
	drv->vreg = devm_regulator_get(drv->dev, "apc");
	if (IS_ERR(drv->vreg))
		return dev_err_probe(drv->dev, PTR_ERR(drv->vreg),
				"could not get regulator\n");

	ret = regulator_get_voltage(drv->vreg);
	if (ret < 0)
		return ret;

	drv->boot_up_uv = drv->uv = ret;

	dev_info(dev, "boot time voltage: %u uV\n", drv->boot_up_uv);

	acc_reg_name = info->acc_use_apcs ? "apcs-mem-acc" : "tcsr-mem-acc";

	drv->acc = devm_platform_ioremap_resource_byname(pdev, acc_reg_name);
	if (IS_ERR_OR_NULL(drv->acc))
		return dev_err_probe(drv->dev, PTR_ERR(drv->acc) ?: -ENODATA,
				"could not map ACC memory\n");

	drv->apm = devm_platform_ioremap_resource_byname(pdev, "apm");
	if (IS_ERR_OR_NULL(drv->apm))
		return dev_err_probe(drv->dev, PTR_ERR(drv->apm) ?: -ENODATA,
				"could not map APM memory\n");

	cpr_apm_init(drv);

	for (index = 0; index < CPR_PD_COUNT; index ++) {
		ret = cpr_init_domain(drv, index);
		if (ret)
			return ret;
	}

	ret = cpufreq_register_notifier(&drv->policy_nb, CPUFREQ_POLICY_NOTIFIER);
	if (ret)
		return dev_err_probe(drv->dev, ret,
				"could not register cpufreq notifier\n");

	return of_genpd_add_provider_onecell(dev->of_node, &drv->cell_data);
}

static void cpr_remove(struct platform_device *pdev)
{
	struct cpr_drv *drv = platform_get_drvdata(pdev);
	of_genpd_del_provider(pdev->dev.of_node);
	cpufreq_unregister_notifier(&drv->policy_nb, CPUFREQ_POLICY_NOTIFIER);
}

static const struct of_device_id cpr_match_table[] = {
	{ .compatible = "qcom,msm8953-cpr4pd" },
	{ }
};
MODULE_DEVICE_TABLE(of, cpr_match_table);

static struct platform_driver cpr_driver = {
	.driver = {
		.name		= "qcom-cpr4pd",
		.of_match_table = cpr_match_table,
		.sync_state	= cpr_sync_state,
	},
	.probe	= cpr_probe,
	.remove	= cpr_remove,
};
module_platform_driver(cpr_driver);

MODULE_DESCRIPTION("Core Power Reduction (CPR) V4 driver for MSM8953");
MODULE_AUTHOR("Vladimir Lypak <vladimir.lypak@gmail.com>");
MODULE_LICENSE("GPL v2");
