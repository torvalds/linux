/*
 * (C) COPYRIGHT RockChip Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

/* #define ENABLE_DEBUG_LOG */
#include "custom_log.h"

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_config.h>
#include <backend/gpu/mali_kbase_devfreq.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_pm_defs.h>

#if MALI_USE_CSF
#include <asm/arch_timer.h>
#endif

#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/mfd/syscon.h>
#include <linux/nvmem-consumer.h>
#include <linux/regmap.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <soc/rockchip/rockchip_system_monitor.h>

#include "mali_kbase_config_platform.h"
#include "mali_kbase_rk.h"

#define POWER_DOWN_FREQ	200000000

/**
 * @file mali_kbase_config_rk.c
 * 对 platform_config_of_rk 的具体实现.
 *
 * mali_device_driver 包含两部分 :
 *      .DP : platform_dependent_part_in_mdd :
 *		依赖 platform 部分,
 *		源码在 <mdd_src_dir>/platform/<platform_name>/
 *		在 mali_device_driver 内部,
 *			记为 platform_dependent_part,
 *			也被记为 platform_specific_code.
 *      .DP : common_parts_in_mdd :
 *		arm 实现的通用的部分,
 *		源码在 <mdd_src_dir>/ 下.
 *		在 mali_device_driver 内部, 记为 common_parts.
 */

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_REGULATOR
static int rk_pm_enable_regulator(struct kbase_device *kbdev);
static void rk_pm_disable_regulator(struct kbase_device *kbdev);
#else
static inline int rk_pm_enable_regulator(struct kbase_device *kbdev)
{
	return 0;
}

static inline void rk_pm_disable_regulator(struct kbase_device *kbdev)
{
}
#endif

static int rk_pm_enable_clk(struct kbase_device *kbdev);

static void rk_pm_disable_clk(struct kbase_device *kbdev);

static int kbase_platform_rk_create_sysfs_files(struct device *dev);

static void kbase_platform_rk_remove_sysfs_files(struct device *dev);

/*---------------------------------------------------------------------------*/

static void rk_pm_power_off_delay_work(struct work_struct *work)
{
	struct rk_context *platform =
		container_of(to_delayed_work(work), struct rk_context, work);
	struct kbase_device *kbdev = platform->kbdev;

	mutex_lock(&platform->lock);

	if (!platform->is_powered) {
		D("mali_dev is already powered off.");
		mutex_unlock(&platform->lock);
		return;
	}

	rockchip_monitor_volt_adjust_lock(kbdev->mdev_info);
	if (pm_runtime_enabled(kbdev->dev)) {
		D("to put_sync_suspend mali_dev.");
		pm_runtime_put_sync_suspend(kbdev->dev);
	}
	rockchip_monitor_volt_adjust_unlock(kbdev->mdev_info);

	rk_pm_disable_clk(kbdev);

	if (pm_runtime_suspended(kbdev->dev)) {
		rk_pm_disable_regulator(kbdev);
		platform->is_regulator_on = false;
	}

	platform->is_powered = false;
	wake_unlock(&platform->wake_lock);

	mutex_unlock(&platform->lock);
}

static int kbase_platform_rk_init(struct kbase_device *kbdev)
{
	int ret = 0;
	struct rk_context *platform;

	platform = kzalloc(sizeof(*platform), GFP_KERNEL);
	if (!platform) {
		E("err.");
		return -ENOMEM;
	}

	platform->is_powered = false;
	platform->kbdev = kbdev;

	platform->delay_ms = 200;
	if (of_property_read_u32(kbdev->dev->of_node, "power-off-delay-ms",
				 &platform->delay_ms))
		W("power-off-delay-ms not available.");

	platform->power_off_wq = create_freezable_workqueue("gpu_power_off_wq");
	if (!platform->power_off_wq) {
		E("couldn't create workqueue");
		ret = -ENOMEM;
		goto err_wq;
	}
	INIT_DEFERRABLE_WORK(&platform->work, rk_pm_power_off_delay_work);

	wake_lock_init(&platform->wake_lock, WAKE_LOCK_SUSPEND, "gpu");

	platform->utilisation_period = DEFAULT_UTILISATION_PERIOD_IN_MS;

	ret = kbase_platform_rk_create_sysfs_files(kbdev->dev);
	if (ret) {
		E("fail to create sysfs_files. ret = %d.", ret);
		goto err_sysfs_files;
	}

	kbdev->platform_context = (void *)platform;
	pm_runtime_enable(kbdev->dev);

	mutex_init(&platform->lock);

	return 0;

err_sysfs_files:
	wake_lock_destroy(&platform->wake_lock);
	destroy_workqueue(platform->power_off_wq);
err_wq:
	return ret;
}

static void kbase_platform_rk_term(struct kbase_device *kbdev)
{
	struct rk_context *platform =
		(struct rk_context *)kbdev->platform_context;

	pm_runtime_disable(kbdev->dev);
	kbdev->platform_context = NULL;

	if (platform) {
		cancel_delayed_work_sync(&platform->work);
		wake_lock_destroy(&platform->wake_lock);
		destroy_workqueue(platform->power_off_wq);
		platform->is_powered = false;
		platform->kbdev = NULL;
		kfree(platform);
	}
	kbase_platform_rk_remove_sysfs_files(kbdev->dev);
}

struct kbase_platform_funcs_conf platform_funcs = {
	.platform_init_func = &kbase_platform_rk_init,
	.platform_term_func = &kbase_platform_rk_term,
};

/*---------------------------------------------------------------------------*/

static int rk_pm_callback_runtime_on(struct kbase_device *kbdev)
{
	struct rockchip_opp_info *opp_info = &kbdev->opp_info;
	int ret = 0;

	if (!kbdev->current_nominal_freq)
		return 0;

	ret = clk_bulk_prepare_enable(opp_info->num_clks,  opp_info->clks);
	if (ret) {
		dev_err(kbdev->dev, "failed to enable opp clks\n");
		return ret;
	}
	if (opp_info->data && opp_info->data->set_read_margin)
		opp_info->data->set_read_margin(kbdev->dev, opp_info,
						opp_info->target_rm);
	if (opp_info->scmi_clk) {
		if (clk_set_rate(opp_info->scmi_clk,
				 kbdev->current_nominal_freq))
			dev_err(kbdev->dev, "failed to restore clk rate\n");
	}
	clk_bulk_disable_unprepare(opp_info->num_clks, opp_info->clks);

	return 0;
}

static void rk_pm_callback_runtime_off(struct kbase_device *kbdev)
{
	struct rockchip_opp_info *opp_info = &kbdev->opp_info;

	if (opp_info->scmi_clk) {
		if (clk_set_rate(opp_info->scmi_clk, POWER_DOWN_FREQ))
			dev_err(kbdev->dev, "failed to set power down rate\n");
	}
	opp_info->current_rm = UINT_MAX;
}

static int rk_pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 1; /* Assume GPU has been powered off */
	int err = 0;
	struct rk_context *platform = get_rk_context(kbdev);

	cancel_delayed_work_sync(&platform->work);

	mutex_lock(&platform->lock);

	if (platform->is_powered) {
		D("mali_device is already powered.");
		ret = 0;
		goto out;
	}

	/* we must enable vdd_gpu before pd_gpu_in_chip. */
	if (!platform->is_regulator_on) {
		err = rk_pm_enable_regulator(kbdev);
		if (err) {
			E("fail to enable regulator, err : %d.", err);
			ret = err;
			goto out;
		}
		platform->is_regulator_on = true;
	}

	err = rk_pm_enable_clk(kbdev);
	if (err) {
		E("failed to enable clk: %d", err);
		ret = err;
		goto out;
	}

	rockchip_monitor_volt_adjust_lock(kbdev->mdev_info);
	/* 若 mali_dev 的 runtime_pm 是 enabled 的, 则... */
	if (pm_runtime_enabled(kbdev->dev)) {
		D("to resume mali_dev syncly.");
		/* 对 pd_in_chip 的 on 操作,
		 * 将在 pm_domain 的 runtime_pm_callbacks 中完成.
		 */
		err = pm_runtime_get_sync(kbdev->dev);
		if (err < 0) {
			E("failed to runtime resume device: %d.", err);
			ret = err;
			goto out;
		} else if (err == 1) { /* runtime_pm_status is still active */
			D("chip has NOT been powered off, no need to re-init.");
			ret = 0;
		}
	}
	rockchip_monitor_volt_adjust_unlock(kbdev->mdev_info);

	platform->is_powered = true;
	wake_lock(&platform->wake_lock);

out:
	mutex_unlock(&platform->lock);
	return ret;
}

static void rk_pm_callback_power_off(struct kbase_device *kbdev)
{
	struct rk_context *platform = get_rk_context(kbdev);

	D("enter");

	queue_delayed_work(platform->power_off_wq, &platform->work,
			   msecs_to_jiffies(platform->delay_ms));
}

static int rk_kbase_device_runtime_init(struct kbase_device *kbdev)
{
	return 0;
}

static void rk_kbase_device_runtime_disable(struct kbase_device *kbdev)
{
}

struct kbase_pm_callback_conf pm_callbacks = {
	.power_on_callback = rk_pm_callback_power_on,
	.power_off_callback = rk_pm_callback_power_off,
#ifdef CONFIG_PM
	.power_runtime_init_callback = rk_kbase_device_runtime_init,
	.power_runtime_term_callback = rk_kbase_device_runtime_disable,
	.power_runtime_on_callback = rk_pm_callback_runtime_on,
	.power_runtime_off_callback = rk_pm_callback_runtime_off,
#else				/* CONFIG_PM */
	.power_runtime_init_callback = NULL,
	.power_runtime_term_callback = NULL,
	.power_runtime_on_callback = NULL,
	.power_runtime_off_callback = NULL,
#endif				/* CONFIG_PM */
};

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_REGULATOR
static int rk_pm_enable_regulator(struct kbase_device *kbdev)
{
	int ret = 0;
	unsigned int i;

	for (i = 0; i < kbdev->nr_regulators; i++) {
		struct regulator *regulator = kbdev->regulators[i];
		if (!regulator) {
			W("no mali regulator control, no need to enable.");
			goto EXIT;
		}

		D("to enable regulator.");
		ret = regulator_enable(regulator);
		if (ret) {
			E("fail to enable regulator, ret : %d.", ret);
			goto EXIT;
		}
	}

EXIT:
	return ret;
}

static void rk_pm_disable_regulator(struct kbase_device *kbdev)
{
	unsigned int i;

	for (i = 0; i < kbdev->nr_regulators; i++) {
		struct regulator *regulator = kbdev->regulators[i];

		if (!regulator) {
			W("no mali regulator control, no need to disable.");
			return;
		}

		D("to disable regulator.");
		regulator_disable(regulator);
	}
}
#endif

static int rk_pm_enable_clk(struct kbase_device *kbdev)
{
	int err = 0;
	unsigned int i;

	for (i = 0; i < kbdev->nr_clocks; i++) {
		struct clk *clock = kbdev->clocks[i];

		if (!clock) {
			W("no mali clock control, no need to enable.");
		} else {
			D("to enable clk.");
			err = clk_enable(clock);
			if (err)
				E("failed to enable clk: %d.", err);
		}
	}

	return err;
}

static void rk_pm_disable_clk(struct kbase_device *kbdev)
{
	unsigned int i;

	for (i = 0; i < kbdev->nr_clocks; i++) {
		struct clk *clock = kbdev->clocks[i];

		if (!clock) {
			W("no mali clock control, no need to disable.");
		} else {
			D("to disable clk.");
			clk_disable(clock);
		}
	}
}

/*---------------------------------------------------------------------------*/

static ssize_t utilisation_period_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rk_context *platform = get_rk_context(kbdev);
	ssize_t ret = 0;

	ret += snprintf(buf, PAGE_SIZE, "%u\n", platform->utilisation_period);

	return ret;
}

static ssize_t utilisation_period_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rk_context *platform = get_rk_context(kbdev);
	int ret = 0;

	ret = kstrtouint(buf, 0, &platform->utilisation_period);
	if (ret) {
		E("invalid input period : %s.", buf);
		return ret;
	}
	D("set utilisation_period to '%d'.", platform->utilisation_period);

	return count;
}

static ssize_t utilisation_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct rk_context *platform = get_rk_context(kbdev);
	ssize_t ret = 0;
	unsigned long period_in_us = platform->utilisation_period * 1000;
	u32 utilisation;
	struct kbasep_pm_metrics metrics_when_start;
	struct kbasep_pm_metrics metrics_diff; /* between start and end. */
	u32 total_time = 0;
	u32 busy_time = 0;

	/* get current metrics data. */
	kbase_pm_get_dvfs_metrics(kbdev, &metrics_when_start, &metrics_diff);
	/* sleep for 'period_in_us'. */
	usleep_range(period_in_us, period_in_us + 100);
	/* get metrics data between start and end. */
	kbase_pm_get_dvfs_metrics(kbdev, &metrics_when_start, &metrics_diff);

	total_time = metrics_diff.time_busy + metrics_diff.time_idle;
	busy_time = metrics_diff.time_busy;
	D("total_time : %u, busy_time : %u.", total_time, busy_time);

	utilisation = busy_time * 100 / total_time;
	ret += snprintf(buf, PAGE_SIZE, "%d\n", utilisation);

	return ret;
}

static DEVICE_ATTR_RW(utilisation_period);
static DEVICE_ATTR_RO(utilisation);

static int kbase_platform_rk_create_sysfs_files(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_utilisation_period);
	if (ret) {
		E("fail to create sysfs file 'utilisation_period'.");
		goto out;
	}

	ret = device_create_file(dev, &dev_attr_utilisation);
	if (ret) {
		E("fail to create sysfs file 'utilisation'.");
		goto remove_utilisation_period;
	}

	return 0;

remove_utilisation_period:
	device_remove_file(dev, &dev_attr_utilisation_period);
out:
	return ret;
}

static void kbase_platform_rk_remove_sysfs_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_utilisation_period);
	device_remove_file(dev, &dev_attr_utilisation);
}

static int rk3588_gpu_get_soc_info(struct device *dev, struct device_node *np,
			       int *bin, int *process)
{
	int ret = 0;
	u8 value = 0;

	if (!bin)
		return 0;

	if (of_property_match_string(np, "nvmem-cell-names",
				     "specification_serial_number") >= 0) {
		ret = rockchip_nvmem_cell_read_u8(np,
						  "specification_serial_number",
						  &value);
		if (ret) {
			dev_err(dev,
				"Failed to get specification_serial_number\n");
			return ret;
		}
		/* RK3588M */
		if (value == 0xd)
			*bin = 1;
		/* RK3588J */
		else if (value == 0xa)
			*bin = 2;
	}
	if (*bin < 0)
		*bin = 0;
	dev_info(dev, "bin=%d\n", *bin);

	return ret;
}

static int rk3588_gpu_set_soc_info(struct device *dev, struct device_node *np,
			       int bin, int process, int volt_sel)
{
	struct opp_table *opp_table;
	u32 supported_hw[2];

	if (volt_sel < 0)
		return 0;
	if (bin < 0)
		bin = 0;

	if (!of_property_read_bool(np, "rockchip,supported-hw"))
		return 0;

	/* SoC Version */
	supported_hw[0] = BIT(bin);
	/* Speed Grade */
	supported_hw[1] = BIT(volt_sel);
	opp_table = dev_pm_opp_set_supported_hw(dev, supported_hw, 2);
	if (IS_ERR(opp_table)) {
		dev_err(dev, "failed to set supported opp\n");
		return PTR_ERR(opp_table);
	}

	return 0;
}

static int rk3588_gpu_set_read_margin(struct device *dev,
				      struct rockchip_opp_info *opp_info,
				      u32 rm)
{
	int ret = 0;
	u32 val;

	if (!opp_info->grf || !opp_info->volt_rm_tbl)
		return 0;
	if (rm == opp_info->current_rm || rm == UINT_MAX)
		return 0;

	dev_dbg(dev, "set rm to %d\n", rm);

	ret = regmap_read(opp_info->grf, 0x24, &val);
	if (ret < 0) {
		dev_err(dev, "failed to get rm from 0x24\n");
		return ret;
	}
	val &= ~0x1c;
	regmap_write(opp_info->grf, 0x24, val | (rm << 2));

	ret = regmap_read(opp_info->grf, 0x28, &val);
	if (ret < 0) {
		dev_err(dev, "failed to get rm from 0x28\n");
		return ret;
	}
	val &= ~0x1c;
	regmap_write(opp_info->grf, 0x28, val | (rm << 2));

	opp_info->current_rm = rm;

	return 0;
}

static const struct rockchip_opp_data rk3588_gpu_opp_data = {
	.get_soc_info = rk3588_gpu_get_soc_info,
	.set_soc_info = rk3588_gpu_set_soc_info,
	.set_read_margin = rk3588_gpu_set_read_margin,
};

static const struct of_device_id rockchip_mali_of_match[] = {
	{
		.compatible = "rockchip,rk3588",
		.data = (void *)&rk3588_gpu_opp_data,
	},
	{},
};

int kbase_platform_rk_init_opp_table(struct kbase_device *kbdev)
{
	rockchip_get_opp_data(rockchip_mali_of_match, &kbdev->opp_info);

	return rockchip_init_opp_table(kbdev->dev, &kbdev->opp_info,
				       "gpu_leakage", "mali");
}

int kbase_platform_rk_enable_regulator(struct kbase_device *kbdev)
{
	struct rk_context *platform = get_rk_context(kbdev);
	int err = 0;

	if (!platform->is_regulator_on) {
		err = rk_pm_enable_regulator(kbdev);
		if (err) {
			E("fail to enable regulator, err : %d.", err);
			return err;
		}
		platform->is_regulator_on = true;
	}

	return 0;
}

/*---------------------------------------------------------------------------*/

static void *enumerate_gpu_clk(struct kbase_device *kbdev,
		unsigned int index)
{
	if (index >= kbdev->nr_clocks)
		return NULL;

	return kbdev->clocks[index];
}

static unsigned long get_gpu_clk_rate(struct kbase_device *kbdev,
		void *gpu_clk_handle)
{
	return clk_get_rate((struct clk *)gpu_clk_handle);
}

static int gpu_clk_notifier_register(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb)
{
	compiletime_assert(offsetof(struct clk_notifier_data, clk) ==
		offsetof(struct kbase_gpu_clk_notifier_data, gpu_clk_handle),
		"mismatch in the offset of clk member");

	compiletime_assert(sizeof(((struct clk_notifier_data *)0)->clk) ==
	     sizeof(((struct kbase_gpu_clk_notifier_data *)0)->gpu_clk_handle),
	     "mismatch in the size of clk member");

	return clk_notifier_register((struct clk *)gpu_clk_handle, nb);
}

static void gpu_clk_notifier_unregister(struct kbase_device *kbdev,
		void *gpu_clk_handle, struct notifier_block *nb)
{
	clk_notifier_unregister((struct clk *)gpu_clk_handle, nb);
}

struct kbase_clk_rate_trace_op_conf clk_rate_trace_ops = {
	.get_gpu_clk_rate = get_gpu_clk_rate,
	.enumerate_gpu_clk = enumerate_gpu_clk,
	.gpu_clk_notifier_register = gpu_clk_notifier_register,
	.gpu_clk_notifier_unregister = gpu_clk_notifier_unregister,
};
