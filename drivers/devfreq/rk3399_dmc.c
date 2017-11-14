/*
 * Copyright (c) 2016, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Lin Huang <hl@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regulator/consumer.h>
#include <linux/rwsem.h>
#include <linux/suspend.h>

#include <soc/rockchip/rockchip_sip.h>

struct dram_timing {
	unsigned int ddr3_speed_bin;
	unsigned int pd_idle;
	unsigned int sr_idle;
	unsigned int sr_mc_gate_idle;
	unsigned int srpd_lite_idle;
	unsigned int standby_idle;
	unsigned int auto_pd_dis_freq;
	unsigned int dram_dll_dis_freq;
	unsigned int phy_dll_dis_freq;
	unsigned int ddr3_odt_dis_freq;
	unsigned int ddr3_drv;
	unsigned int ddr3_odt;
	unsigned int phy_ddr3_ca_drv;
	unsigned int phy_ddr3_dq_drv;
	unsigned int phy_ddr3_odt;
	unsigned int lpddr3_odt_dis_freq;
	unsigned int lpddr3_drv;
	unsigned int lpddr3_odt;
	unsigned int phy_lpddr3_ca_drv;
	unsigned int phy_lpddr3_dq_drv;
	unsigned int phy_lpddr3_odt;
	unsigned int lpddr4_odt_dis_freq;
	unsigned int lpddr4_drv;
	unsigned int lpddr4_dq_odt;
	unsigned int lpddr4_ca_odt;
	unsigned int phy_lpddr4_ca_drv;
	unsigned int phy_lpddr4_ck_cs_drv;
	unsigned int phy_lpddr4_dq_drv;
	unsigned int phy_lpddr4_odt;
};

struct rk3399_dmcfreq {
	struct device *dev;
	struct devfreq *devfreq;
	struct devfreq_simple_ondemand_data ondemand_data;
	struct clk *dmc_clk;
	struct devfreq_event_dev *edev;
	struct mutex lock;
	struct dram_timing timing;

	/*
	 * DDR Converser of Frequency (DCF) is used to implement DDR frequency
	 * conversion without the participation of CPU, we will implement and
	 * control it in arm trust firmware.
	 */
	wait_queue_head_t	wait_dcf_queue;
	int irq;
	int wait_dcf_flag;
	struct regulator *vdd_center;
	unsigned long rate, target_rate;
	unsigned long volt, target_volt;
};

static int rk3399_dmcfreq_target(struct device *dev, unsigned long *freq,
				 u32 flags)
{
	struct rk3399_dmcfreq *dmcfreq = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long old_clk_rate = dmcfreq->rate;
	unsigned long target_volt, target_rate;
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

	/*
	 * If frequency scaling from low to high, adjust voltage first.
	 * If frequency scaling from high to low, adjust frequency first.
	 */
	if (old_clk_rate < target_rate) {
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    target_volt);
		if (err) {
			dev_err(dev, "Cannot to set voltage %lu uV\n",
				target_volt);
			goto out;
		}
	}
	dmcfreq->wait_dcf_flag = 1;

	err = clk_set_rate(dmcfreq->dmc_clk, target_rate);
	if (err) {
		dev_err(dev, "Cannot to set frequency %lu (%d)\n",
			target_rate, err);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      dmcfreq->volt);
		goto out;
	}

	/*
	 * Wait until bcf irq happen, it means freq scaling finish in
	 * arm trust firmware, use 100ms as timeout time.
	 */
	if (!wait_event_timeout(dmcfreq->wait_dcf_queue,
				!dmcfreq->wait_dcf_flag, HZ / 10))
		dev_warn(dev, "Timeout waiting for dcf interrupt\n");

	/*
	 * Check the dpll rate,
	 * There only two result we will get,
	 * 1. Ddr frequency scaling fail, we still get the old rate.
	 * 2. Ddr frequency scaling sucessful, we get the rate we set.
	 */
	dmcfreq->rate = clk_get_rate(dmcfreq->dmc_clk);

	/* If get the incorrect rate, set voltage to old value. */
	if (dmcfreq->rate != target_rate) {
		dev_err(dev, "Get wrong ddr frequency, Request frequency %lu,\
			Current frequency %lu\n", target_rate, dmcfreq->rate);
		regulator_set_voltage(dmcfreq->vdd_center, dmcfreq->volt,
				      dmcfreq->volt);
		goto out;
	} else if (old_clk_rate > target_rate)
		err = regulator_set_voltage(dmcfreq->vdd_center, target_volt,
					    target_volt);
	if (err)
		dev_err(dev, "Cannot to set vol %lu uV\n", target_volt);

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

static irqreturn_t rk3399_dmc_irq(int irq, void *dev_id)
{
	struct rk3399_dmcfreq *dmcfreq = dev_id;
	struct arm_smccc_res res;

	dmcfreq->wait_dcf_flag = 0;
	wake_up(&dmcfreq->wait_dcf_queue);

	/* Clear the DCF interrupt */
	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_CLR_IRQ,
		      0, 0, 0, 0, &res);

	return IRQ_HANDLED;
}

static int of_get_ddr_timings(struct dram_timing *timing,
			      struct device_node *np)
{
	int ret = 0;

	ret = of_property_read_u32(np, "rockchip,ddr3_speed_bin",
				   &timing->ddr3_speed_bin);
	ret |= of_property_read_u32(np, "rockchip,pd_idle",
				    &timing->pd_idle);
	ret |= of_property_read_u32(np, "rockchip,sr_idle",
				    &timing->sr_idle);
	ret |= of_property_read_u32(np, "rockchip,sr_mc_gate_idle",
				    &timing->sr_mc_gate_idle);
	ret |= of_property_read_u32(np, "rockchip,srpd_lite_idle",
				    &timing->srpd_lite_idle);
	ret |= of_property_read_u32(np, "rockchip,standby_idle",
				    &timing->standby_idle);
	ret |= of_property_read_u32(np, "rockchip,auto_pd_dis_freq",
				    &timing->auto_pd_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,dram_dll_dis_freq",
				    &timing->dram_dll_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,phy_dll_dis_freq",
				    &timing->phy_dll_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,ddr3_odt_dis_freq",
				    &timing->ddr3_odt_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,ddr3_drv",
				    &timing->ddr3_drv);
	ret |= of_property_read_u32(np, "rockchip,ddr3_odt",
				    &timing->ddr3_odt);
	ret |= of_property_read_u32(np, "rockchip,phy_ddr3_ca_drv",
				    &timing->phy_ddr3_ca_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_ddr3_dq_drv",
				    &timing->phy_ddr3_dq_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_ddr3_odt",
				    &timing->phy_ddr3_odt);
	ret |= of_property_read_u32(np, "rockchip,lpddr3_odt_dis_freq",
				    &timing->lpddr3_odt_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,lpddr3_drv",
				    &timing->lpddr3_drv);
	ret |= of_property_read_u32(np, "rockchip,lpddr3_odt",
				    &timing->lpddr3_odt);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr3_ca_drv",
				    &timing->phy_lpddr3_ca_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr3_dq_drv",
				    &timing->phy_lpddr3_dq_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr3_odt",
				    &timing->phy_lpddr3_odt);
	ret |= of_property_read_u32(np, "rockchip,lpddr4_odt_dis_freq",
				    &timing->lpddr4_odt_dis_freq);
	ret |= of_property_read_u32(np, "rockchip,lpddr4_drv",
				    &timing->lpddr4_drv);
	ret |= of_property_read_u32(np, "rockchip,lpddr4_dq_odt",
				    &timing->lpddr4_dq_odt);
	ret |= of_property_read_u32(np, "rockchip,lpddr4_ca_odt",
				    &timing->lpddr4_ca_odt);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr4_ca_drv",
				    &timing->phy_lpddr4_ca_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr4_ck_cs_drv",
				    &timing->phy_lpddr4_ck_cs_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr4_dq_drv",
				    &timing->phy_lpddr4_dq_drv);
	ret |= of_property_read_u32(np, "rockchip,phy_lpddr4_odt",
				    &timing->phy_lpddr4_odt);

	return ret;
}

static int rk3399_dmcfreq_probe(struct platform_device *pdev)
{
	struct arm_smccc_res res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct rk3399_dmcfreq *data;
	int ret, irq, index, size;
	uint32_t *timing;
	struct dev_pm_opp *opp;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev,
			"Cannot get the dmc interrupt resource: %d\n", irq);
		return irq;
	}
	data = devm_kzalloc(dev, sizeof(struct rk3399_dmcfreq), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);

	data->vdd_center = devm_regulator_get(dev, "center");
	if (IS_ERR(data->vdd_center)) {
		dev_err(dev, "Cannot get the regulator \"center\"\n");
		return PTR_ERR(data->vdd_center);
	}

	data->dmc_clk = devm_clk_get(dev, "dmc_clk");
	if (IS_ERR(data->dmc_clk)) {
		dev_err(dev, "Cannot get the clk dmc_clk\n");
		return PTR_ERR(data->dmc_clk);
	};

	data->irq = irq;
	ret = devm_request_irq(dev, irq, rk3399_dmc_irq, 0,
			       dev_name(dev), data);
	if (ret) {
		dev_err(dev, "Failed to request dmc irq: %d\n", ret);
		return ret;
	}

	init_waitqueue_head(&data->wait_dcf_queue);
	data->wait_dcf_flag = 0;

	data->edev = devfreq_event_get_edev_by_phandle(dev, 0);
	if (IS_ERR(data->edev))
		return -EPROBE_DEFER;

	ret = devfreq_event_enable_edev(data->edev);
	if (ret < 0) {
		dev_err(dev, "failed to enable devfreq-event devices\n");
		return ret;
	}

	/*
	 * Get dram timing and pass it to arm trust firmware,
	 * the dram drvier in arm trust firmware will get these
	 * timing and to do dram initial.
	 */
	if (!of_get_ddr_timings(&data->timing, np)) {
		timing = &data->timing.ddr3_speed_bin;
		size = sizeof(struct dram_timing) / 4;
		for (index = 0; index < size; index++) {
			arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, *timing++, index,
				      ROCKCHIP_SIP_CONFIG_DRAM_SET_PARAM,
				      0, 0, 0, 0, &res);
			if (res.a0) {
				dev_err(dev, "Failed to set dram param: %ld\n",
					res.a0);
				return -EINVAL;
			}
		}
	}

	arm_smccc_smc(ROCKCHIP_SIP_DRAM_FREQ, 0, 0,
		      ROCKCHIP_SIP_CONFIG_DRAM_INIT,
		      0, 0, 0, 0, &res);

	/*
	 * We add a devfreq driver to our parent since it has a device tree node
	 * with operating points.
	 */
	if (dev_pm_opp_of_add_table(dev)) {
		dev_err(dev, "Invalid operating-points in device tree.\n");
		return -EINVAL;
	}

	of_property_read_u32(np, "upthreshold",
			     &data->ondemand_data.upthreshold);
	of_property_read_u32(np, "downdifferential",
			     &data->ondemand_data.downdifferential);

	data->rate = clk_get_rate(data->dmc_clk);

	opp = devfreq_recommended_opp(dev, &data->rate, 0);
	if (IS_ERR(opp))
		return PTR_ERR(opp);

	data->rate = dev_pm_opp_get_freq(opp);
	data->volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	rk3399_devfreq_dmc_profile.initial_freq = data->rate;

	data->devfreq = devm_devfreq_add_device(dev,
					   &rk3399_devfreq_dmc_profile,
					   DEVFREQ_GOV_SIMPLE_ONDEMAND,
					   &data->ondemand_data);
	if (IS_ERR(data->devfreq))
		return PTR_ERR(data->devfreq);
	devm_devfreq_register_opp_notifier(dev, data->devfreq);

	data->dev = dev;
	platform_set_drvdata(pdev, data);

	return 0;
}

static const struct of_device_id rk3399dmc_devfreq_of_match[] = {
	{ .compatible = "rockchip,rk3399-dmc" },
	{ },
};
MODULE_DEVICE_TABLE(of, rk3399dmc_devfreq_of_match);

static struct platform_driver rk3399_dmcfreq_driver = {
	.probe	= rk3399_dmcfreq_probe,
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
