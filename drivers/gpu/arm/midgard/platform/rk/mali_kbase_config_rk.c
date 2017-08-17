/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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
#include <backend/gpu/mali_kbase_pm_internal.h>

#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/nvmem-consumer.h>
#include <linux/soc/rockchip/pvtm.h>
#include <linux/thermal.h>

#include "mali_kbase_rk.h"

#define MAX_PROP_NAME_LEN	3
#define LEAKAGE_TABLE_END	~1
#define LEAKAGE_INVALID		0xff

struct pvtm_config {
	unsigned int freq;
	unsigned int volt;
	unsigned int ch[2];
	unsigned int sample_time;
	unsigned int num;
	unsigned int err;
	unsigned int ref_temp;
	int temp_prop[2];
	const char *tz_name;
	struct thermal_zone_device *tz;
};

struct volt_sel_table {
	int min;
	int max;
	int sel;
};

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

	if (!platform->is_powered) {
		D("mali_dev is already powered off.");
		return;
	}

	if (pm_runtime_enabled(kbdev->dev)) {
		D("to put_sync_suspend mali_dev.");
		pm_runtime_put_sync_suspend(kbdev->dev);
	}

	rk_pm_disable_regulator(kbdev);

	platform->is_powered = false;
	KBASE_TIMELINE_GPU_POWER(kbdev, 0);
	wake_unlock(&platform->wake_lock);
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
	return 0;
}

static void rk_pm_callback_runtime_off(struct kbase_device *kbdev)
{
}

static int rk_pm_callback_power_on(struct kbase_device *kbdev)
{
	int ret = 1; /* Assume GPU has been powered off */
	int err = 0;
	struct rk_context *platform = get_rk_context(kbdev);

	cancel_delayed_work_sync(&platform->work);

	err = rk_pm_enable_clk(kbdev);
	if (err) {
		E("failed to enable clk: %d", err);
		return err;
	}

	if (platform->is_powered) {
		D("mali_device is already powered.");
		return 0;
	}

	/* we must enable vdd_gpu before pd_gpu_in_chip. */
	err = rk_pm_enable_regulator(kbdev);
	if (err) {
		E("fail to enable regulator, err : %d.", err);
		return err;
	}

	/* 若 mali_dev 的 runtime_pm 是 enabled 的, 则... */
	if (pm_runtime_enabled(kbdev->dev)) {
		D("to resume mali_dev syncly.");
		/* 对 pd_in_chip 的 on 操作,
		 * 将在 pm_domain 的 runtime_pm_callbacks 中完成.
		 */
		err = pm_runtime_get_sync(kbdev->dev);
		if (err < 0) {
			E("failed to runtime resume device: %d.", err);
			return err;
		} else if (err == 1) { /* runtime_pm_status is still active */
			D("chip has NOT been powered off, no need to re-init.");
			ret = 0;
		}
	}

	platform->is_powered = true;
	KBASE_TIMELINE_GPU_POWER(kbdev, 1);
	wake_lock(&platform->wake_lock);

	return ret;
}

static void rk_pm_callback_power_off(struct kbase_device *kbdev)
{
	struct rk_context *platform = get_rk_context(kbdev);

	rk_pm_disable_clk(kbdev);
	queue_delayed_work(platform->power_off_wq, &platform->work,
			   msecs_to_jiffies(platform->delay_ms));
}

int rk_kbase_device_runtime_init(struct kbase_device *kbdev)
{
	return 0;
}

void rk_kbase_device_runtime_disable(struct kbase_device *kbdev)
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

int kbase_platform_early_init(void)
{
	/* Nothing needed at this stage */
	return 0;
}

/*---------------------------------------------------------------------------*/

void kbase_platform_rk_shutdown(struct kbase_device *kbdev)
{
	I("to make vdd_gpu enabled for turning off pd_gpu in pm_framework.");
	rk_pm_enable_regulator(kbdev);
}

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_REGULATOR
static int rk_pm_enable_regulator(struct kbase_device *kbdev)
{
	int ret = 0;

	if (!kbdev->regulator) {
		W("no mali regulator control, no need to enable.");
		goto EXIT;
	}

	D("to enable regulator.");
	ret = regulator_enable(kbdev->regulator);
	if (ret) {
		E("fail to enable regulator, ret : %d.", ret);
		goto EXIT;
	}

EXIT:
	return ret;
}

static void rk_pm_disable_regulator(struct kbase_device *kbdev)
{
	if (!(kbdev->regulator)) {
		W("no mali regulator control, no need to disable.");
		return;
	}

	D("to disable regulator.");
	regulator_disable(kbdev->regulator);
}
#endif

static int rk_pm_enable_clk(struct kbase_device *kbdev)
{
	int err = 0;

	if (!(kbdev->clock)) {
		W("no mali clock control, no need to enable.");
	} else {
		D("to enable clk.");
		err = clk_enable(kbdev->clock);
		if (err)
			E("failed to enable clk: %d.", err);
	}

	return err;
}

static void rk_pm_disable_clk(struct kbase_device *kbdev)
{
	if (!(kbdev->clock)) {
		W("no mali clock control, no need to disable.");
	} else {
		D("to disable clk.");
		clk_disable(kbdev->clock);
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
	unsigned long total_time;
	unsigned long busy_time;
	unsigned long utilisation;

	kbase_pm_reset_dvfs_utilisation(kbdev);
	usleep_range(period_in_us, period_in_us + 100);
	kbase_pm_get_dvfs_utilisation(kbdev, &total_time, &busy_time);
	/* 'devfreq_dev_profile' instance registered to devfreq
	 * also uses kbase_pm_reset_dvfs_utilisation
	 * and kbase_pm_get_dvfs_utilisation.
	 * it's better to cat this file when DVFS is disabled.
	 */
	D("total_time : %lu, busy_time : %lu.", total_time, busy_time);

	utilisation = busy_time * 100 / total_time;
	ret += snprintf(buf, PAGE_SIZE, "%ld\n", utilisation);

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

static int rockchip_get_efuse_value(struct device_node *np, char *porp_name,
				    int *value)
{
	struct nvmem_cell *cell;
	unsigned char *buf;
	size_t len;

	cell = of_nvmem_cell_get(np, porp_name);
	if (IS_ERR(cell))
		return PTR_ERR(cell);

	buf = (unsigned char *)nvmem_cell_read(cell, &len);

	nvmem_cell_put(cell);

	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (buf[0] == LEAKAGE_INVALID)
		return -EINVAL;

	*value = buf[0];

	kfree(buf);

	return 0;
}

static int rockchip_get_volt_sel_table(struct device_node *np, char *porp_name,
				       struct volt_sel_table **table)
{
	struct volt_sel_table *sel_table;
	const struct property *prop;
	int count, i;

	prop = of_find_property(np, porp_name, NULL);
	if (!prop)
		return -EINVAL;

	if (!prop->value)
		return -ENODATA;

	count = of_property_count_u32_elems(np, porp_name);
	if (count < 0)
		return -EINVAL;

	if (count % 3)
		return -EINVAL;

	sel_table = kzalloc(sizeof(*sel_table) * (count / 3 + 1), GFP_KERNEL);
	if (!sel_table)
		return -ENOMEM;

	for (i = 0; i < count / 3; i++) {
		of_property_read_u32_index(np, porp_name, 3 * i,
					   &sel_table[i].min);
		of_property_read_u32_index(np, porp_name, 3 * i + 1,
					   &sel_table[i].max);
		of_property_read_u32_index(np, porp_name, 3 * i + 2,
					   &sel_table[i].sel);
	}
	sel_table[i].min = 0;
	sel_table[i].max = 0;
	sel_table[i].sel = LEAKAGE_TABLE_END;

	*table = sel_table;

	return 0;
}

static int rockchip_get_volt_sel(struct device_node *np, char *name,
				 int value, int *sel)
{
	struct volt_sel_table *table;
	int i, j = -1, ret;

	ret = rockchip_get_volt_sel_table(np, name, &table);
	if (ret)
		return -EINVAL;

	for (i = 0; table[i].sel != LEAKAGE_TABLE_END; i++) {
		if (value >= table[i].min)
			j = i;
	}
	if (j != -1)
		*sel = table[j].sel;
	else
		ret = -EINVAL;

	kfree(table);

	return ret;
}

static int rockchip_parse_pvtm_config(struct device_node *np,
				      struct pvtm_config *pvtm)
{
	if (!of_find_property(np, "rockchip,pvtm-voltage-sel", NULL))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-freq", &pvtm->freq))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-volt", &pvtm->volt))
		return -EINVAL;
	if (of_property_read_u32_array(np, "rockchip,pvtm-ch", pvtm->ch, 2))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-sample-time",
				 &pvtm->sample_time))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-number", &pvtm->num))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-error", &pvtm->err))
		return -EINVAL;
	if (of_property_read_u32(np, "rockchip,pvtm-ref-temp", &pvtm->ref_temp))
		return -EINVAL;
	if (of_property_read_u32_array(np, "rockchip,pvtm-temp-prop",
				       pvtm->temp_prop, 2))
		return -EINVAL;
	if (of_property_read_string(np, "rockchip,pvtm-thermal-zone",
				    &pvtm->tz_name))
		return -EINVAL;
	pvtm->tz = thermal_zone_get_zone_by_name(pvtm->tz_name);
	if (IS_ERR(pvtm->tz))
		return -EINVAL;
	if (!pvtm->tz->ops->get_temp)
		return -EINVAL;

	return 0;
}

static int rockchip_get_pvtm_specific_value(struct device *dev,
					    struct device_node *np,
					    struct clk *clk,
					    struct regulator *reg,
					    int *target_value)
{
	struct pvtm_config *pvtm;
	unsigned long old_freq;
	unsigned int old_volt;
	int cur_temp, diff_temp;
	int cur_value, total_value, avg_value, diff_value;
	int min_value, max_value;
	int ret = 0, i = 0, retry = 2;

	pvtm = kzalloc(sizeof(*pvtm), GFP_KERNEL);
	if (!pvtm)
		return -ENOMEM;

	ret = rockchip_parse_pvtm_config(np, pvtm);
	if (ret)
		goto pvtm_value_out;

	old_freq = clk_get_rate(clk);
	old_volt = regulator_get_voltage(reg);

	/*
	 * Set pvtm_freq to the lowest frequency in dts,
	 * so change frequency first.
	 */
	ret = clk_set_rate(clk, pvtm->freq * 1000);
	if (ret) {
		dev_err(dev, "Failed to set pvtm freq\n");
		goto pvtm_value_out;
	}

	ret = regulator_set_voltage(reg, pvtm->volt, pvtm->volt);
	if (ret) {
		dev_err(dev, "Failed to set pvtm_volt\n");
		goto restore_clk;
	}

	/* The first few values may be fluctuant, if error is too big, retry*/
	while (retry--) {
		total_value = 0;
		min_value = INT_MAX;
		max_value = 0;

		for (i = 0; i < pvtm->num; i++) {
			cur_value = rockchip_get_pvtm_value(pvtm->ch[0],
							    pvtm->ch[1],
							    pvtm->sample_time);
			if (!cur_value)
				goto resetore_volt;
			if (cur_value < min_value)
				min_value = cur_value;
			if (cur_value > max_value)
				max_value = cur_value;
			total_value += cur_value;
		}
		if (max_value - min_value < pvtm->err)
			break;
	}
	avg_value = total_value / pvtm->num;

	/*
	 * As pvtm is influenced by temperature, compute difference between
	 * current temperature and reference temperature
	 */
	pvtm->tz->ops->get_temp(pvtm->tz, &cur_temp);
	diff_temp = (cur_temp / 1000 - pvtm->ref_temp);
	diff_value = diff_temp *
		(diff_temp < 0 ? pvtm->temp_prop[0] : pvtm->temp_prop[1]);
	*target_value = avg_value + diff_value;

	dev_info(dev, "temp=%d, pvtm=%d (%d + %d)\n",
		 cur_temp, *target_value, avg_value, diff_value);

resetore_volt:
	regulator_set_voltage(reg, old_volt, old_volt);
restore_clk:
	clk_set_rate(clk, old_freq);
pvtm_value_out:
	kfree(pvtm);

	return ret;
}

void kbase_platform_rk_set_opp_info(struct kbase_device *kbdev)
{
	struct device_node *np;
	char name[MAX_PROP_NAME_LEN];
	int pvmt_value, leakage;
	int lkg_volt_sel, pvtm_volt_sel, volt_sel = -1;
	int err = 0;

	if (!kbdev)
		return;

	if (IS_ERR_OR_NULL(kbdev->regulator))
		return;
	if (IS_ERR_OR_NULL(kbdev->clock))
		return;

	np = of_parse_phandle(kbdev->dev->of_node, "operating-points-v2", 0);
	if (!np) {
		dev_warn(kbdev->dev, "OPP-v2 not supported\n");
		return;
	}

	err = rockchip_get_efuse_value(np, "gpu_leakage", &leakage);
	if (!err) {
		dev_info(kbdev->dev, "leakage=%d\n", leakage);
		err = rockchip_get_volt_sel(np, "rockchip,leakage-voltage-sel",
					    leakage, &lkg_volt_sel);
		if (!err) {
			dev_info(kbdev->dev, "leakage-sel=%d\n", lkg_volt_sel);
			volt_sel = lkg_volt_sel;
		}
	}

	err = rockchip_get_pvtm_specific_value(kbdev->dev, np, kbdev->clock,
					       kbdev->regulator,
					       &pvmt_value);
	if (!err) {
		err = rockchip_get_volt_sel(np, "rockchip,pvtm-voltage-sel",
					    pvmt_value, &pvtm_volt_sel);
		if (!err) {
			dev_info(kbdev->dev, "pvtm-sel=%d\n", pvtm_volt_sel);
			if (volt_sel < 0 || volt_sel > pvtm_volt_sel)
				volt_sel = pvtm_volt_sel;
		}
	}

	if (volt_sel >= 0) {
		snprintf(name, MAX_PROP_NAME_LEN, "L%d", volt_sel);
		err = dev_pm_opp_set_prop_name(kbdev->dev, name);
		if (err)
			dev_err(kbdev->dev, "Failed to set prop name\n");
	}
}
