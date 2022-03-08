// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Lin Huang <hl@rock-chips.com>
 */

#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/interrupt.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/rwsem.h>
#include <linux/suspend.h>

#include <soc/rockchip/rk3399_grf.h>
#include <soc/rockchip/rockchip_sip.h>

#define RK3399_SET_ODT_PD_0_SR_IDLE			GENMASK(7, 0)
#define RK3399_SET_ODT_PD_0_SR_MC_GATE_IDLE		GENMASK(15, 8)
#define RK3399_SET_ODT_PD_0_STANDBY_IDLE		GENMASK(31, 16)

#define RK3399_SET_ODT_PD_1_PD_IDLE			GENMASK(11, 0)
#define RK3399_SET_ODT_PD_1_SRPD_LITE_IDLE		GENMASK(27, 16)

#define RK3399_SET_ODT_PD_2_ODT_ENABLE			BIT(0)

struct rk3399_dmcfreq {
	struct device *dev;
	struct devfreq *devfreq;
	struct devfreq_simple_ondemand_data ondemand_data;
	struct clk *dmc_clk;
	struct devfreq_event_dev *edev;
	struct mutex lock;
	struct regulator *vdd_center;
	struct regmap *regmap_pmu;
	unsigned long rate, target_rate;
	unsigned long volt, target_volt;
	unsigned int odt_dis_freq;
	int odt_pd_arg0, odt_pd_arg1;

	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;
	unsigned int ddr3_odt_dis_freq;
	unsigned int lpddr3_odt_dis_freq;
	unsigned int lpddr4_odt_dis_freq;
};

static int rk3399_dmcfreq_target(struct device *dev, unsigned long *freq,
				 u32 flags)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long old_clk_rate = dmcfreq->rate;
	unsigned long target_volt, target_rate;
	struct arm_smccc_res res;
	int err;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	target_rate = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (dmcfreq->rate == target_rate)
		return 0;

	mutex_lock(&dmcfreq->lock);

	if (dmcfreq->regmap_pmu) {
		unsigned int odt_pd_arg2 = 0;

		if (target_rate >= dmcfreq->odt_dis_freq)
			odt_pd_arg2 |= RK3399_SET_ODT_PD_2_ODT_ENABLE;

		/*
		 * This makes a SMC call to the TF-A to set the DDR PD
		 * (power-down) timings and to enable or disable the
		 * ODT (on-die termination) resistors.
		 */
		arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, dmcfreq->odt_pd_arg0,
			      dmcfreq->odt_pd_arg1,
			      ROCKCHIP_SIP_CONFIG_DRAM_SET_ODT_PD,
			      odt_pd_arg2, 0, 0, 0, &res);
	}

	/*
	 * If frequency scaling from low to high, adjust voltage first.
	 * If frequency scaling from high to low, adjust frequency first.
	 */
	if (old_clk_rate < target_rate) {
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    target_volt);
		if (err) {
			dev_err(dev, "Cannot set voltage %lu uV\n",
				target_volt);
			goto out;
		}
	}

	err = clk_set_rate(dmcfreq->dmc_clk, target_rate);
	if (err) {
		dev_err(dev, "Cannot set frequency %lu (%d)\n", target_rate,
			err);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      dmcfreq->volt);
		goto out;
	}

	/*
	 * Check the dpll rate,
	 * There only two result we will get,
	 * 1. Ddr frequency scaling fail, we still get the old rate.
	 * 2. Ddr frequency scaling sucessful, we get the rate we set.
	 */
	dmcfreq->rate = clk_get_rate(dmcfreq->dmc_clk);

	/* If get the incorrect rate, set voltage to old value. */
	if (dmcfreq->rate != target_rate) {
		dev_err(dev, "Got wrong frequency, Request %lu, Current %lu\n",
			target_rate, dmcfreq->rate);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      dmcfreq->volt);
		goto out;
	} else if (old_clk_rate > target_rate)
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    target_volt);
	if (err)
		dev_err(dev, "Cannot set voltage %lu uV\n", target_volt);

	dmcfreq->rate = target_rate;
	dmcfreq->volt = target_volt;

out:
	mutex_unlock(&dmcfreq->lock);
	return err;
}

static int rk3399_dmcfreq_get_dev_status(struct device *dev,
					 struct devfreq_dev_status *stat)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int ret = 0;

	ret = devfreq_event_get_event(dmcfreq->edev, &edata);
	if (ret < 0)
		return ret;

	stat->current_frequency = dmcfreq->rate;
	stat->busy_time = edata.load_count;
	stat->total_time = edata.total_count;

	return ret;
}

static int rk3399_dmcfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(dev);

	*freq = dmcfreq->rate;

	return 0;
}

static struct devfreq_dev_profile rk3399_devfreq_dmc_profile = {
	.polling_ms	= 200,
	.target		= rk3399_dmcfreq_target,
	.get_dev_status	= rk3399_dmcfreq_get_dev_status,
	.get_cur_freq	= rk3399_dmcfreq_get_cur_freq,
};

static __maybe_unused int rk3399_dmcfreq_suspend(struct device *dev)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	ret = devfreq_event_disable_edev(dmcfreq->edev);
	if (ret < 0) {
		dev_err(dev, "failed to disable the devfreq-event devices\n");
		return ret;
	}

	ret = devfreq_suspend_device(dmcfreq->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to suspend the devfreq devices\n");
		return ret;
	}

	return 0;
}

static __maybe_unused int rk3399_dmcfreq_resume(struct device *dev)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	int ret = 0;

	ret = devfreq_event_enable_edev(dmcfreq->edev);
	if (ret < 0) {
		dev_err(dev, "failed to enable the devfreq-event devices\n");
		return ret;
	}

	ret = devfreq_resume_device(dmcfreq->devfreq);
	if (ret < 0) {
		dev_err(dev, "failed to resume the devfreq devices\n");
		return ret;
	}
	return ret;
}

static SIMPLE_DEV_PM_OPS(rk3399_dmcfreq_pm, rk3399_dmcfreq_suspend,
			 rk3399_dmcfreq_resume);

static int rk3399_dmcfreq_of_props(struct rk3399_dmcfreq *data,
				   struct device_node *np)
{
	int ret = 0;

	ret |= of_property_read_u32(np, "rockchip,pd_idle",
				    &data->pd_idle);
	ret |= of_property_read_u32(np, "rockchip,sr_idle",
				    &data->sr_idle);
	ret |= of_property_read_u32(np, "rockchip,sr_mc_gate_idle",
				    &data->sr_mc_gate_idle);
	ret |= of_property_read_u32(np, "rockchip,srpd_lite_idle",
				    &data->srpd_lite_idle);
	ret |= of_property_read_u32(np, "rockchip,standby_idle",
				    &data->standby_idle);
	ret |= of_property_read_u32(np, "rockchip,ddr3_odt_dis_freq",
				    &data->ddr3_odt_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,lpddr3_odt_dis_freq",
				    &data->lpddr3_odt_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,lpddr4_odt_dis_freq",
				    &data->lpddr4_odt_dis_freq);

	return ret;
}

static int rk3399_dmcfreq_probe(struct platform_device *pdev)
{
	struct arm_smccc_res res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node, *node;
	struct rk3399_dmcfreq *data;
	int ret;
	struct dev_pm_opp *opp;
	u32 ddr_type;
	u32 val;

	data = devm_kzalloc(dev, sizeof(struct rk3399_dmcfreq), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);

	data->vdd_center = devm_regulator_get(dev, "center");
	if (IS_ERR(data->vdd_center))
		return dev_err_probe(dev, PTR_ERR(data->vdd_center),
				     "Cannot get the regulator \"center\"\n");

	data->dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(data->dmc_clk))
		return dev_err_probe(dev, PTR_ERR(data->dmc_clk),
				     "Cannot get the clk dmc_clk\n");

	data->edev = devfreq_event_get_edev_by_phandle(dev, "devfreq-events", 0);
	if (IS_ERR(data->edev))
		return -EPROBE_DEFER;

	ret = devfreq_event_enable_edev(data->edev);
	if (ret < 0) {
		dev_err(dev, "failed to enable devfreq-event devices\n");
		return ret;
	}

	rk3399_dmcfreq_of_props(data, np);

	node = of_parse_phandle(np, "rockchip,pmu", 0);
	if (!node)
		goto no_pmu;

	data->regmap_pmu = syscon_node_to_regmap(node);
	of_node_put(node);
	if (IS_ERR(data->regmap_pmu)) {
		ret = PTR_ERR(data->regmap_pmu);
		goto err_edev;
	}

	regmap_read(data->regmap_pmu, RK3399_PMUGRF_OS_REG2, &val);
	ddr_type = (val >> RK3399_PMUGRF_DDRTYPE_SHIFT) &
		    RK3399_PMUGRF_DDRTYPE_MASK;

	switch (ddr_type) {
	case RK3399_PMUGRF_DDRTYPE_DDR3:
		data->odt_dis_freq = data->ddr3_odt_dis_freq;
		break;
	case RK3399_PMUGRF_DDRTYPE_LPDDR3:
		data->odt_dis_freq = data->lpddr3_odt_dis_freq;
		break;
	case RK3399_PMUGRF_DDRTYPE_LPDDR4:
		data->odt_dis_freq = data->lpddr4_odt_dis_freq;
		break;
	default:
		ret = -EINVAL;
		goto err_edev;
	}

no_pmu:
	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_INIT,
		      0, 0, 0, 0, &res);

	/*
	 * In TF-A there is a platform SIP call to set the PD (power-down)
	 * timings and to enable or disable the ODT (on-die termination).
	 */
	data->odt_pd_arg0 =
		FIELD_PREP(RK3399_SET_ODT_PD_0_SR_IDLE, data->sr_idle) |
		FIELD_PREP(RK3399_SET_ODT_PD_0_SR_MC_GATE_IDLE,
			   data->sr_mc_gate_idle) |
		FIELD_PREP(RK3399_SET_ODT_PD_0_STANDBY_IDLE,
			   data->standby_idle);
	data->odt_pd_arg1 =
		FIELD_PREP(RK3399_SET_ODT_PD_1_PD_IDLE, data->pd_idle) |
		FIELD_PREP(RK3399_SET_ODT_PD_1_SRPD_LITE_IDLE,
			   data->srpd_lite_idle);

	/*
	 * We add a devfreq driver to our parent since it has a device tree node
	 * with operating points.
	 */
	if (dev_pm_opp_of_add_table(dev)) {
		dev_err(dev, "Invalid operating-points in device tree.\n");
		ret = -EINVAL;
		goto err_edev;
	}

	data->ondemand_data.upthreshold = 25;
	data->ondemand_data.downdifferential = 15;

	data->rate = clk_get_rate(data->dmc_clk);

	opp = devfreq_recommended_opp(dev, &data->rate, 0);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		goto err_free_opp;
	}

	data->rate = dev_pm_opp_get_freq(opp);
	data->volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	rk3399_devfreq_dmc_profile.initial_freq = data->rate;

	data->devfreq = devm_devfreq_add_device(dev,
					   &rk3399_devfreq_dmc_profile,
					   DEVFREQ_GOV_SIMPLE_ONDEMAND,
					   &data->ondemand_data);
	if (IS_ERR(data->devfreq)) {
		ret = PTR_ERR(data->devfreq);
		goto err_free_opp;
	}

	devm_devfreq_register_opp_notifier(dev, data->devfreq);

	data->dev = dev;
	platform_set_drvdata(pdev, data);

	return 0;

err_free_opp:
	dev_pm_opp_of_remove_table(&pdev->dev);
err_edev:
	devfreq_event_disable_edev(data->edev);

	return ret;
}

static int rk3399_dmcfreq_remove(struct platform_device *pdev)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(&pdev->dev);

	/*
	 * Before remove the opp table we need to unregister the opp notifier.
	 */
	devm_devfreq_unregister_opp_notifier(dmcfreq->dev, dmcfreq->devfreq);
	dev_pm_opp_of_remove_table(dmcfreq->dev);

	return 0;
}

static const struct of_device_id rk3399dmc_devfreq_of_match[] = {
	{ .compatible = "rockchip,rk3399-dmc" },
	{ },
};
MODULE_DEVICE_TABLE(of, rk3399dmc_devfreq_of_match);

static struct platform_driver rk3399_dmcfreq_driver = {
	.probe	= rk3399_dmcfreq_probe,
	.remove = rk3399_dmcfreq_remove,
	.driver = {
		.name	= "rk3399-dmc-freq",
		.pm	= &rk3399_dmcfreq_pm,
		.of_match_table = rk3399dmc_devfreq_of_match,
	},
};
module_platform_driver(rk3399_dmcfreq_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Lin Huang <hl@rock-chips.com>");
MODULE_DESCRIPTION("RK3399 dmcfreq driver with devfreq framework");
