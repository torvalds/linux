/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS - MIF clock frequency scaling support in DEVFREQ framework
 *	This version supports EXYNOS5250 only. This changes bus frequencies.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/suspend.h>
#include <linux/opp.h>
#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/pm_qos.h>

#include <mach/map.h>
#include <mach/regs-clock.h>
#include <mach/asv-exynos.h>
#include <mach/abb-exynos.h>
#include <mach/smc.h>
#include <mach/regs-mem.h>
#include <mach/exynos5_bus.h>

#include "governor.h"

#define MAX_SAFEVOLT	1200000 /* 1.2V */

#define MIF_ABB_CONTROL_FREQUENCY 667000000
#define MIF_UPPER_FREQUENCY 667000

/* Assume that the bus is saturated if the utilization is 20% */
#define MIF_BUS_SATURATION_RATIO	20

enum mif_level_idx {
	LV_0,
	LV_1,
	LV_2,
	LV_3,
	LV_4,
	_LV_END
};

struct busfreq_data_mif {
	struct device *dev;
	struct devfreq *devfreq;
	bool disabled;
	struct regulator *vdd_mif;
	unsigned long curr_freq;
	unsigned long curr_volt;
	unsigned long suspend_freq;

	struct mutex lock;

	struct clk *mif_clk;
	struct clk *mclk_cdrex;
	struct clk *mout_mpll;
	struct clk *mout_bpll;
};

struct mif_bus_opp_table {
	unsigned int idx;
	unsigned long clk;
	unsigned long volt;
	unsigned long dmc_timingrow;
};

static struct mif_bus_opp_table exynos5_mif_opp_table[] = {
	{LV_0, 800000, 1000000, 0x34498692},
	{LV_1, 667000, 1000000, 0x2c48758f},
	{LV_2, 400000, 1000000, 0x1A255349},
	{LV_3, 160000, 1000000, 0x1A255349},
	{LV_4, 100000, 1000000, 0x1A255349},
	{0, 0, 0, 0},
};

struct exynos5_bus_mif_handle {
	struct list_head node;
	unsigned long min;
};

struct exynos5_bus_mif_handle *mif_min_hd;
static struct busfreq_data_mif *exynos5_bus_mif_data;
static DEFINE_MUTEX(exynos5_bus_mif_data_lock);
static LIST_HEAD(exynos5_bus_mif_requests);
static DEFINE_MUTEX(exynos5_bus_mif_requests_lock);
static DEFINE_MUTEX(exynos5_bus_mif_upper_freq_lock);
static bool multiple_windows = false;
static unsigned int used_dev_cnt = 0;

static void exynos5_mif_check_upper_freq(void)
{
	if (multiple_windows) {
		if (!mif_min_hd) {
			mif_min_hd = exynos5_bus_mif_min(MIF_UPPER_FREQUENCY);
			if (!mif_min_hd)
				pr_err("%s: Failed to request min_freq\n", __func__);
		}
	} else {
		if (mif_min_hd) {
			exynos5_bus_mif_put(mif_min_hd);
			mif_min_hd = NULL;
		}
	}
}

void exynos5_mif_multiple_windows(bool state)
{
	mutex_lock(&exynos5_bus_mif_upper_freq_lock);

	multiple_windows = state;
	exynos5_mif_check_upper_freq();

	mutex_unlock(&exynos5_bus_mif_upper_freq_lock);
}

void exynos5_mif_used_dev(bool power_on)
{
	mutex_lock(&exynos5_bus_mif_upper_freq_lock);

	if (power_on)
		used_dev_cnt++;
	else if (used_dev_cnt > 0)
		used_dev_cnt--;

	exynos5_mif_check_upper_freq();

	mutex_unlock(&exynos5_bus_mif_upper_freq_lock);
}

static int exynos5_mif_setvolt(struct busfreq_data_mif *data, unsigned long volt)
{
	return regulator_set_voltage(data->vdd_mif, volt, MAX_SAFEVOLT);
}

static int exynos5_mif_set_dmc_timing(unsigned int freq)
{
	int index;
	unsigned int timing0 = 0;
	unsigned int timing1 = 0;

	for (index = LV_0; index < _LV_END; index++) {
		if (freq == exynos5_mif_opp_table[index].clk)
			break;
	}

	if (index == _LV_END)
		return -EINVAL;

#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc_read_sfr(SMC_CMD_REG,
		SMC_REG_ID_SFR_R(EXYNOS5_PA_DREXII +
				EXYNOS_DMC_TIMINGROW_OFFSET),
				&timing0, 0);

	timing0 |= exynos5_mif_opp_table[index].dmc_timingrow;
	timing1 = exynos5_mif_opp_table[index].dmc_timingrow;

	exynos_smc(SMC_CMD_REG,
		SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXII +
				EXYNOS_DMC_TIMINGROW_OFFSET),
				timing0, 0);
	exynos_smc(SMC_CMD_REG,
		SMC_REG_ID_SFR_W(EXYNOS5_PA_DREXII +
				EXYNOS_DMC_TIMINGROW_OFFSET),
				timing1, 0);
#else
	timing0 = __raw_readl(S5P_VA_DREXII +
				EXYNOS_DMC_TIMINGROW_OFFSET);
	timing0 |= exynos5_mif_opp_table[index].dmc_timingrow;
	timing1 = exynos5_mif_opp_table[index].dmc_timingrow;
	__raw_writel(timing0, S5P_VA_DREXII + EXYNOS_DMC_TIMINGROW_OFFSET);
	__raw_writel(timing1, S5P_VA_DREXII + EXYNOS_DMC_TIMINGROW_OFFSET);
#endif
	return 0;
}
static int exynos5_mif_setclk(struct busfreq_data_mif *data,
		unsigned long new_freq)
{
	unsigned err = 0;
	struct clk *old_p;
	struct clk *new_p;
	unsigned long old_p_rate;
	unsigned long new_p_rate;
	int div;

	/*
	 * Dynamic ABB control according to MIF frequency
	 * MIF frquency > 667 MHz : ABB_MODE_130V
	 * MIF frquency <= 667 MHz : ABB_MODE_BYPASS
	 */
	if (new_freq > MIF_ABB_CONTROL_FREQUENCY)
		set_abb_member(ABB_MIF, ABB_MODE_130V);

	old_p = clk_get_parent(data->mclk_cdrex);
	if (IS_ERR(old_p))
		return PTR_ERR(old_p);
	old_p_rate = clk_get_rate(old_p);
	div = DIV_ROUND_CLOSEST(old_p_rate, new_freq);

	if (abs(DIV_ROUND_UP(old_p_rate, div) - new_freq) > 1000000) {
		new_p = (old_p == data->mout_bpll) ? data->mout_mpll :
				data->mout_bpll;
		new_p_rate = clk_get_rate(new_p);

		if (new_p_rate > old_p_rate) {
			/*
			 * Needs to change to faster pll.  Change the divider
			 * first, then switch to the new pll.  This only works
			 * because the set_rate op on mif_clk doesn't know
			 * anything about its current parent, it just applies
			 * the dividers assuming the right pll has been selected
			 * for the requested frequency.
			 */
			err = clk_set_rate(data->mif_clk, new_freq);
			if (err) {
				pr_info("clk_set_rate error %d\n", err);
				goto out;
			}

			err = clk_set_parent(data->mclk_cdrex, new_p);
			if (err) {
				pr_info("clk_set_parent error %d\n", err);
				goto out;
			}
		} else {
			/*
			 * Needs to change to a slower pll.  Switch to the new
			 * pll first, then apply the new divider.
			 */
			err = clk_set_parent(data->mclk_cdrex, new_p);
			if (err) {
				pr_info("clk_set_parent error %d\n", err);
				goto out;
			}

			err = clk_set_rate(data->mif_clk, new_freq);
			if (err) {
				pr_info("clk_set_rate error %d\n", err);
				goto out;
			}
		}
	} else {
		/* No need to change pll */
		err = clk_set_rate(data->mif_clk, new_freq);
	}

	if (new_freq <= MIF_ABB_CONTROL_FREQUENCY)
		set_abb_member(ABB_MIF, ABB_MODE_BYPASS);
out:
	return err;
}

static int exynos5_busfreq_mif_target(struct device *dev, unsigned long *_freq,
			      u32 flags)
{
	int err = 0;
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);
	struct opp *opp;
	unsigned long old_freq, freq;
	unsigned long volt;
	struct exynos5_bus_mif_handle *handle;

	mutex_lock(&exynos5_bus_mif_requests_lock);
	list_for_each_entry(handle, &exynos5_bus_mif_requests, node) {
		if (handle->min > *_freq) {
			*_freq = handle->min;
			flags &= ~DEVFREQ_FLAG_LEAST_UPPER_BOUND;
		}
	}
	mutex_unlock(&exynos5_bus_mif_requests_lock);

	mutex_lock(&data->lock);

	if (data->devfreq->max_freq && *_freq > data->devfreq->max_freq) {
		*_freq = data->devfreq->max_freq;
		flags |= DEVFREQ_FLAG_LEAST_UPPER_BOUND;
	}

	rcu_read_lock();
	opp = devfreq_recommended_opp(dev, _freq, flags);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "%s: Invalid OPP.\n", __func__);
		err = PTR_ERR(opp);
		goto out;
	}

	freq = opp_get_freq(opp);
	volt = opp_get_voltage(opp);
	rcu_read_unlock();

	old_freq = data->curr_freq;

	if (old_freq == freq)
		goto out;

	dev_dbg(dev, "targetting %lukHz %luuV\n", freq, volt);

	if (data->disabled)
		goto out;

	if (old_freq < freq) {
		err = exynos5_mif_setvolt(data, volt);
		if (err)
			goto out;
		err = exynos5_mif_set_dmc_timing(freq);
		if (err)
			goto out;
	}

	err = exynos5_mif_setclk(data, freq * 1000);
	if (err)
		goto out;

	if (old_freq > freq) {
		err = exynos5_mif_set_dmc_timing(freq);
		if (err)
			goto out;
		err = exynos5_mif_setvolt(data, volt);
		if (err)
			goto out;
	}

	data->curr_freq = freq;
	data->curr_volt = volt;
out:
	mutex_unlock(&data->lock);
	return err;
}

int exynos5_bus_mif_update(struct exynos5_bus_mif_handle *handle,
		unsigned long min_freq)
{
	mutex_lock(&exynos5_bus_mif_requests_lock);
	handle->min = min_freq;
	mutex_unlock(&exynos5_bus_mif_requests_lock);

	mutex_lock(&exynos5_bus_mif_data_lock);
	if (exynos5_bus_mif_data) {
		mutex_lock(&exynos5_bus_mif_data->devfreq->lock);
		update_devfreq(exynos5_bus_mif_data->devfreq);
		mutex_unlock(&exynos5_bus_mif_data->devfreq->lock);
	}
	mutex_unlock(&exynos5_bus_mif_data_lock);

	return 0;
}

struct exynos5_bus_mif_handle *exynos5_bus_mif_get(unsigned long min_freq)
{
	struct exynos5_bus_mif_handle *handle;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return NULL;

	handle->min = min_freq;

	mutex_lock(&exynos5_bus_mif_requests_lock);
	list_add_tail(&handle->node, &exynos5_bus_mif_requests);
	mutex_unlock(&exynos5_bus_mif_requests_lock);

	mutex_lock(&exynos5_bus_mif_data_lock);
	if (exynos5_bus_mif_data) {
		mutex_lock(&exynos5_bus_mif_data->devfreq->lock);
		update_devfreq(exynos5_bus_mif_data->devfreq);
		mutex_unlock(&exynos5_bus_mif_data->devfreq->lock);
	}
	mutex_unlock(&exynos5_bus_mif_data_lock);


	return handle;
}

int exynos5_bus_mif_put(struct exynos5_bus_mif_handle *handle)
{
	int ret = 0;

	mutex_lock(&exynos5_bus_mif_requests_lock);
	list_del(&handle->node);
	mutex_unlock(&exynos5_bus_mif_requests_lock);

	mutex_lock(&exynos5_bus_mif_data_lock);
	if (exynos5_bus_mif_data) {
		mutex_lock(&exynos5_bus_mif_data->devfreq->lock);
		update_devfreq(exynos5_bus_mif_data->devfreq);
		mutex_unlock(&exynos5_bus_mif_data->devfreq->lock);
	}
	mutex_unlock(&exynos5_bus_mif_data_lock);

	kfree(handle);

	return ret;
}


static void exynos5_mif_exit(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device,
						    dev);
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);

	devfreq_unregister_opp_notifier(dev, data->devfreq);
}

static struct devfreq_dev_profile exynos5_devfreq_mif_profile = {
	.initial_freq		= 400000,
	.target			= exynos5_busfreq_mif_target,
	.exit			= exynos5_mif_exit,
};

static int exynos5250_init_mif_tables(struct busfreq_data_mif *data)
{
	int i, err = 0;

	for (i = LV_0; i < _LV_END; i++) {
		exynos5_mif_opp_table[i].volt = asv_get_volt(ID_MIF, exynos5_mif_opp_table[i].clk);
		if (exynos5_mif_opp_table[i].volt == 0) {
			dev_err(data->dev, "Invalid value for frequency %lu\n",
				exynos5_mif_opp_table[i].clk);
			continue;
		}
		err = opp_add(data->dev, exynos5_mif_opp_table[i].clk,
				exynos5_mif_opp_table[i].volt);
		if (err) {
			dev_err(data->dev, "Cannot add opp entries.\n");
			return err;
		}
	}

	return 0;
}

static int exynos5_bus_mif_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct opp *max_opp;
	struct opp *opp;
	unsigned long maxfreq = ULONG_MAX;
	unsigned long volt;
	unsigned long freq;
	int err = 0;
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);

	/*
	 * Set the frequency to the maximum enabled frequency, but set the
	 * voltage to the maximum possible voltage in case the bootloader
	 * sets the frequency to maximum during resume.  Frequency can only
	 * go up, so set voltage and timing before clock.
	 */
	mutex_lock(&data->lock);

	data->disabled = true;

	rcu_read_lock();
	max_opp = opp_find_freq_floor(data->dev, &maxfreq);
	if (IS_ERR(max_opp)) {
		rcu_read_unlock();
		err = PTR_ERR(max_opp);
		goto unlock;
	}

	maxfreq = ULONG_MAX;
	if (data->devfreq->max_freq)
		maxfreq = data->devfreq->max_freq;

	opp = opp_find_freq_floor(data->dev, &maxfreq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		err = PTR_ERR(opp);
		goto unlock;
	}

	freq = opp_get_freq(opp);
	volt = opp_get_voltage(max_opp);
	rcu_read_unlock();

	err = exynos5_mif_setvolt(data, volt);
	if (err)
		goto unlock;

	err = exynos5_mif_set_dmc_timing(freq);

	if (err)
		goto unlock;

	err = exynos5_mif_setclk(data, freq * 1000);
	if (err)
		goto unlock;

	data->suspend_freq = freq;

unlock:
	mutex_unlock(&data->lock);
	return err;
}

static int exynos5_bus_mif_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int err = 0;
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);

	/*
	 * Set the frequency to the maximum enabled frequency in case the
	 * bootloader raised it during resume.  Frequency can only go down,
	 * so set timing after updating clock.
	 */
	mutex_lock(&data->lock);

	err = exynos5_mif_set_dmc_timing(data->suspend_freq);
	if (err)
		goto unlock;

	err = exynos5_mif_setclk(data, data->suspend_freq * 1000);
	if (err)
		goto unlock;

unlock:
	mutex_unlock(&data->lock);
	return err;
}

static int exynos5_bus_mif_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);
	int err = 0;

	/*
	 * Restore the frequency and voltage to the values when suspend was
	 * started.  Frequency can only go down, so set timing and voltage
	 * after updating clock.
	 */
	mutex_lock(&data->lock);

	data->disabled = false;

	err = exynos5_mif_setclk(data, data->curr_freq * 1000);
	if (err)
		goto unlock;

	err = exynos5_mif_set_dmc_timing(data->curr_freq);
	if (err)
		goto unlock;

	err = exynos5_mif_setvolt(data, data->curr_volt);
	if (err)
		goto unlock;

unlock:
	mutex_unlock(&data->lock);
	return err;
}

static struct devfreq_pm_qos_data exynos5_devfreq_mif_pm_qos_data = {
	.bytes_per_sec_per_hz = 8,
	.pm_qos_class = PM_QOS_MEMORY_THROUGHPUT,
};

static __devinit int exynos5_busfreq_mif_probe(struct platform_device *pdev)
{
	struct busfreq_data_mif *data;
	struct opp *opp;
	struct device *dev = &pdev->dev;
	unsigned long initial_freq;
	unsigned long initial_volt;
	int err = 0;
	struct exynos5_bus_mif_platform_data *pdata = pdev->dev.platform_data;

	data = devm_kzalloc(&pdev->dev, sizeof(struct busfreq_data_mif), GFP_KERNEL);
	if (data == NULL) {
		dev_err(dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	data->dev = dev;
	mutex_init(&data->lock);

	err = exynos5250_init_mif_tables(data);
	if (err)
		goto err_regulator;

	data->vdd_mif = regulator_get(dev, "vdd_mif");
	if (IS_ERR(data->vdd_mif)) {
		dev_err(dev, "Cannot get the regulator \"vdd_mif\"\n");
		err = PTR_ERR(data->vdd_mif);
		goto err_regulator;
	}

	data->mif_clk = clk_get(dev, "mif_clk");
	if (IS_ERR(data->mif_clk)) {
		dev_err(dev, "Cannot get clock \"mif_clk\"\n");
		err = PTR_ERR(data->mif_clk);
		goto err_clock;
	}

	data->mclk_cdrex = clk_get(dev, "mclk_cdrex");
	if (IS_ERR(data->mclk_cdrex)) {
		dev_err(dev, "Cannot get clock \"mclk_crex\"\n");
		err = PTR_ERR(data->mclk_cdrex);
		goto err_mclk_cdrex;
	}

	data->mout_mpll = clk_get(dev, "mout_mpll");
	if (IS_ERR(data->mout_mpll)) {
		dev_err(dev, "Cannot get clock \"mout_mpll\"\n");
		err = PTR_ERR(data->mout_mpll);
		goto err_mout_mpll;
	}

	data->mout_bpll = clk_get(dev, "mout_bpll");
	if (IS_ERR(data->mout_bpll)) {
		dev_err(dev, "Cannot get clock \"mout_bpll\"\n");
		err = PTR_ERR(data->mout_bpll);
		goto err_mout_bpll;
	}

	if (pdata && pdata->max_freq)
		exynos5_devfreq_mif_profile.initial_freq = pdata->max_freq;

	rcu_read_lock();
	opp = opp_find_freq_floor(dev, &exynos5_devfreq_mif_profile.initial_freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		dev_err(dev, "Invalid initial frequency %lu kHz.\n",
		       exynos5_devfreq_mif_profile.initial_freq);
		err = PTR_ERR(opp);
		goto err_opp_add;
	}
	initial_freq = opp_get_freq(opp);
	initial_volt = opp_get_voltage(opp);
	rcu_read_unlock();

	data->curr_freq = initial_freq;
	data->curr_volt = initial_volt;

	err = exynos5_mif_setclk(data, initial_freq * 1000);
	if (err) {
		dev_err(dev, "Failed to set initial frequency\n");
		goto err_opp_add;
	}

	err = exynos5_mif_set_dmc_timing(initial_freq);
	if (err) {
		dev_err(dev, "Failed to set dmc timingrow\n");
		goto err_opp_add;
	}

	err = exynos5_mif_setvolt(data, initial_volt);
	if (err)
		goto err_opp_add;

	platform_set_drvdata(pdev, data);

	data->devfreq = devfreq_add_device(dev, &exynos5_devfreq_mif_profile,
					   &devfreq_pm_qos,
					   &exynos5_devfreq_mif_pm_qos_data);

	if (IS_ERR(data->devfreq)) {
		err = PTR_ERR(data->devfreq);
		goto err_devfreq_add;
	}

	if (pdata && pdata->max_freq)
		data->devfreq->max_freq = pdata->max_freq;

	devfreq_register_opp_notifier(dev, data->devfreq);

	mutex_lock(&exynos5_bus_mif_data_lock);
	exynos5_bus_mif_data = data;
	mutex_unlock(&exynos5_bus_mif_data_lock);
	return 0;

err_devfreq_add:
	devfreq_remove_device(data->devfreq);
	platform_set_drvdata(pdev, NULL);
err_opp_add:
	clk_put(data->mout_bpll);
err_mout_bpll:
	clk_put(data->mout_mpll);
err_mout_mpll:
	clk_put(data->mclk_cdrex);
err_mclk_cdrex:
	clk_put(data->mif_clk);
err_clock:
	regulator_put(data->vdd_mif);
err_regulator:
	return err;
}

static __devexit int exynos5_busfreq_mif_remove(struct platform_device *pdev)
{
	struct busfreq_data_mif *data = platform_get_drvdata(pdev);

	mutex_lock(&exynos5_bus_mif_data_lock);
	exynos5_bus_mif_data = NULL;
	mutex_unlock(&exynos5_bus_mif_data_lock);

	devfreq_remove_device(data->devfreq);
	regulator_put(data->vdd_mif);
	clk_put(data->mif_clk);
	clk_put(data->mclk_cdrex);
	clk_put(data->mout_mpll);
	clk_put(data->mout_bpll);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct dev_pm_ops exynos5_bus_mif_pm_ops = {
	.suspend = exynos5_bus_mif_suspend,
	.resume_noirq = exynos5_bus_mif_resume_noirq,
	.resume = exynos5_bus_mif_resume,
};

static struct platform_driver exynos5_busfreq_mif_driver = {
	.probe		= exynos5_busfreq_mif_probe,
	.remove		= __devexit_p(exynos5_busfreq_mif_remove),
	.driver		= {
		.name		= "exynos5-bus-mif",
		.owner		= THIS_MODULE,
		.pm		= &exynos5_bus_mif_pm_ops,
	},
};

static int __init exynos5_busfreq_mif_init(void)
{
	return platform_driver_register(&exynos5_busfreq_mif_driver);
}
late_initcall(exynos5_busfreq_mif_init);

static void __exit exynos5_busfreq_mif_exit(void)
{
	platform_driver_unregister(&exynos5_busfreq_mif_driver);
}
module_exit(exynos5_busfreq_mif_exit);
