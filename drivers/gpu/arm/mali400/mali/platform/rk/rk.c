/*
 * (C) COPYRIGHT RockChip Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

/**
 * @file rk.c
 * implementation of platform_specific_code on rk platforms, such as rk3328h.
 *
 * mali_device_driver(MDD) includes 2 parts :
 *	.DP : platform_dependent_part :
 *		located in <mdd_src_dir>/mali/platform/<platform_name>/
 *	.DP : common_part :
 *		common part implemented by ARM.
 */

#define ENABLE_DEBUG_LOG
#include "custom_log.h"

#include <linux/platform_device.h>
#include <linux/version.h>
#include <linux/pm.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#ifdef CONFIG_PM
#include <linux/pm_runtime.h>
#endif
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/rockchip/cpu.h>
#include <soc/rockchip/rockchip_opp_select.h>

#include <linux/mali/mali_utgard.h>
#include "mali_kernel_common.h"
#include "../../common/mali_osk_mali.h"

/*---------------------------------------------------------------------------*/

u32 mali_group_error;

/*---------------------------------------------------------------------------*/

#define DEFAULT_UTILISATION_PERIOD_IN_MS (100)

/*
 * rk_platform_context_of_mali_device.
 */
struct rk_context {
	/* mali device. */
	struct device *dev;
	/* is the GPU powered on?  */
	bool is_powered;
	/* debug only, the period in ms to count gpu_utilisation. */
	unsigned int utilisation_period;
};

struct rk_context *s_rk_context;

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_MALI_DEVFREQ
static ssize_t utilisation_period_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct rk_context *platform = s_rk_context;
	ssize_t ret = 0;

	ret += snprintf(buf, PAGE_SIZE, "%u\n", platform->utilisation_period);

	return ret;
}

static ssize_t utilisation_period_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	struct rk_context *platform = s_rk_context;
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
	struct rk_context *platform = s_rk_context;
	struct mali_device *mdev = dev_get_drvdata(dev);
	ssize_t ret = 0;
	unsigned long period_in_us = platform->utilisation_period * 1000;
	unsigned long total_time;
	unsigned long busy_time;
	unsigned long utilisation;

	mali_pm_reset_dvfs_utilisation(mdev);
	usleep_range(period_in_us, period_in_us + 100);
	mali_pm_get_dvfs_utilisation(mdev, &total_time, &busy_time);

	/* 'devfreq_dev_profile' instance registered to devfreq
	 * also uses mali_pm_reset_dvfs_utilisation()
	 * and mali_pm_get_dvfs_utilisation().
	 * So, it's better to disable GPU DVFS before reading this node.
	 */
	D("total_time : %lu, busy_time : %lu.", total_time, busy_time);

	utilisation = busy_time / (total_time / 100);
	ret += snprintf(buf, PAGE_SIZE, "%lu\n", utilisation);

	return ret;
}

static DEVICE_ATTR_RW(utilisation_period);
static DEVICE_ATTR_RO(utilisation);
#endif

static int rk_context_create_sysfs_files(struct device *dev)
{
#ifdef CONFIG_MALI_DEVFREQ
	int ret;

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
#else
	return 0;
#endif
}

static void rk_context_remove_sysfs_files(struct device *dev)
{
#ifdef CONFIG_MALI_DEVFREQ
	device_remove_file(dev, &dev_attr_utilisation_period);
	device_remove_file(dev, &dev_attr_utilisation);
#endif
}

/*---------------------------------------------------------------------------*/

/*
 * Init rk_platform_context of mali_device.
 */
static int rk_context_init(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct rk_context *platform; /* platform_context */

	platform = kzalloc(sizeof(*platform), GFP_KERNEL);
	if (!platform) {
		E("no mem.");
		return _MALI_OSK_ERR_NOMEM;
	}

	platform->dev = dev;
	platform->is_powered = false;

	platform->utilisation_period = DEFAULT_UTILISATION_PERIOD_IN_MS;

	ret = rk_context_create_sysfs_files(dev);
	if (ret) {
		E("fail to create sysfs files, ret = %d", ret);
		goto EXIT;
	}

	s_rk_context = platform;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

EXIT:
	return ret;
}

static void rk_context_deinit(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rk_context *platform = s_rk_context;

	pm_runtime_disable(dev);

	s_rk_context = NULL;

	rk_context_remove_sysfs_files(dev);

	if (platform) {
		platform->is_powered = false;
		platform->dev = NULL;
		kfree(platform);
	}
}

/*---------------------------------------------------------------------------*/
/* for devfreq cooling. */

#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)

#define FALLBACK_STATIC_TEMPERATURE 55000

static u32 dynamic_coefficient;
static u32 static_coefficient;
static s32 ts[4];
static struct thermal_zone_device *gpu_tz;

static int power_model_simple_init(struct platform_device *pdev)
{
	struct device_node *power_model_node;
	const char *tz_name;
	u32 static_power, dynamic_power;
	u32 voltage, voltage_squared, voltage_cubed, frequency;

	power_model_node = of_get_child_by_name(pdev->dev.of_node,
			"power_model");
	if (!power_model_node) {
		dev_err(&pdev->dev, "could not find power_model node\n");
		return -ENODEV;
	}
	if (!of_device_is_compatible(power_model_node,
			"arm,mali-simple-power-model")) {
		dev_err(&pdev->dev, "power_model incompatible with simple power model\n");
		return -ENODEV;
	}

	if (of_property_read_string(power_model_node, "thermal-zone",
			&tz_name)) {
		dev_err(&pdev->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	gpu_tz = thermal_zone_get_zone_by_name(tz_name);
	if (IS_ERR(gpu_tz)) {
		pr_warn_ratelimited("Error getting gpu thermal zone '%s'(%ld), not yet ready?\n",
				tz_name,
				PTR_ERR(gpu_tz));
		gpu_tz = NULL;
	}

	if (of_property_read_u32(power_model_node, "static-power",
			&static_power)) {
		dev_err(&pdev->dev, "static-power in power_model not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "dynamic-power",
			&dynamic_power)) {
		dev_err(&pdev->dev, "dynamic-power in power_model not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "voltage",
			&voltage)) {
		dev_err(&pdev->dev, "voltage in power_model not available\n");
		return -EINVAL;
	}
	if (of_property_read_u32(power_model_node, "frequency",
			&frequency)) {
		dev_err(&pdev->dev, "frequency in power_model not available\n");
		return -EINVAL;
	}
	voltage_squared = (voltage * voltage) / 1000;
	voltage_cubed = voltage * voltage * voltage;
	static_coefficient = (static_power << 20) / (voltage_cubed >> 10);
	dynamic_coefficient = (((dynamic_power * 1000) / voltage_squared)
			* 1000) / frequency;

	if (of_property_read_u32_array(power_model_node, "ts", (u32 *)ts, 4)) {
		dev_err(&pdev->dev, "ts in power_model not available\n");
		return -EINVAL;
	}

	return 0;
}

/* Calculate gpu static power example for reference */
static unsigned long rk_model_static_power(struct devfreq *devfreq,
					   unsigned long voltage)
{
	int temperature, temp;
	int temp_squared, temp_cubed, temp_scaling_factor;
	const unsigned long voltage_cubed = (voltage * voltage * voltage) >> 10;
	unsigned long static_power;

	if (gpu_tz) {
		int ret;

		ret = gpu_tz->ops->get_temp(gpu_tz, &temperature);
		if (ret) {
			MALI_DEBUG_PRINT(2, ("fail to read temp: %d\n", ret));
			temperature = FALLBACK_STATIC_TEMPERATURE;
		}
	} else {
		temperature = FALLBACK_STATIC_TEMPERATURE;
	}

	/* Calculate the temperature scaling factor. To be applied to the
	 * voltage scaled power.
	 */
	temp = temperature / 1000;
	temp_squared = temp * temp;
	temp_cubed = temp_squared * temp;
	temp_scaling_factor =
			(ts[3] * temp_cubed)
			+ (ts[2] * temp_squared)
			+ (ts[1] * temp)
			+ ts[0];

	static_power = (((static_coefficient * voltage_cubed) >> 20)
			* temp_scaling_factor)
		       / 1000000;

	return static_power;
}

/* Calculate gpu dynamic power example for reference */
static unsigned long rk_model_dynamic_power(struct devfreq *devfreq,
					    unsigned long freq,
					    unsigned long voltage)
{
	/* The inputs: freq (f) is in Hz, and voltage (v) in mV.
	 * The coefficient (c) is in mW/(MHz mV mV).
	 *
	 * This function calculates the dynamic power after this formula:
	 * Pdyn (mW) = c (mW/(MHz*mV*mV)) * v (mV) * v (mV) * f (MHz)
	 */
	const unsigned long v2 = (voltage * voltage) / 1000; /* m*(V*V) */
	const unsigned long f_mhz = freq / 1000000; /* MHz */
	unsigned long dynamic_power;

	dynamic_power = (dynamic_coefficient * v2 * f_mhz) / 1000000; /* mW */

	return dynamic_power;
}

struct devfreq_cooling_power rk_cooling_ops = {
	.get_static_power = rk_model_static_power,
	.get_dynamic_power = rk_model_dynamic_power,
};
#endif

/*---------------------------------------------------------------------------*/

#ifdef CONFIG_PM

static int rk_platform_enable_clk_gpu(struct device *dev)
{
	int ret = 0;
#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_HAVE_CLK)
	struct mali_device *mdev = dev_get_drvdata(dev);

	if (mdev->clock)
		ret = clk_enable(mdev->clock);
#endif
	return ret;
}

static void rk_platform_disable_clk_gpu(struct device *dev)
{
#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_HAVE_CLK)
	struct mali_device *mdev = dev_get_drvdata(dev);

	if (mdev->clock)
		clk_disable(mdev->clock);
#endif
}

static int rk_platform_enable_gpu_regulator(struct device *dev)
{
	int ret = 0;
#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_REGULATOR)
	struct mali_device *mdev = dev_get_drvdata(dev);

	if (mdev->regulator)
		ret = regulator_enable(mdev->regulator);
#endif
	return ret;
}

static void rk_platform_disable_gpu_regulator(struct device *dev)
{
#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_REGULATOR)
	struct mali_device *mdev = dev_get_drvdata(dev);

	if (mdev->regulator)
		regulator_disable(mdev->regulator);
#endif
}

static int rk_platform_power_on_gpu(struct device *dev)
{
	struct rk_context *platform = s_rk_context;
	int ret = 0;

	if (!(platform->is_powered)) {
		ret = rk_platform_enable_clk_gpu(dev);
		if (ret) {
			E("fail to enable clk_gpu, ret : %d.", ret);
			goto fail_to_enable_clk;
		}

		ret = rk_platform_enable_gpu_regulator(dev);
		if (ret) {
			E("fail to enable vdd_gpu, ret : %d.", ret);
			goto fail_to_enable_regulator;
		}

		platform->is_powered = true;
	}

	return 0;

fail_to_enable_regulator:
	rk_platform_disable_clk_gpu(dev);

fail_to_enable_clk:
	return ret;
}

static void rk_platform_power_off_gpu(struct device *dev)
{
	struct rk_context *platform = s_rk_context;

	if (platform->is_powered) {
		rk_platform_disable_clk_gpu(dev);
		rk_platform_disable_gpu_regulator(dev);

		platform->is_powered = false;
	}
}

int rk_platform_init_opp_table(struct device *dev)
{
	return rockchip_init_opp_table(dev, NULL, "gpu_leakage", "mali");
}

static int mali_runtime_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_suspend() called\n"));

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->runtime_suspend) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_suspend(device);
	}

	if (!ret)
		rk_platform_power_off_gpu(device);

	return ret;
}

static int mali_runtime_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_resume() called\n"));

	rk_platform_power_on_gpu(device);

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->runtime_resume) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_resume(device);
	}

	return ret;
}

static int mali_runtime_idle(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_runtime_idle() called\n"));

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->runtime_idle) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->runtime_idle(device);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static int mali_os_suspend(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_suspend() called\n"));

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->suspend) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->suspend(device);
	}

	if (!ret)
		rk_platform_power_off_gpu(device);

	return ret;
}

static int mali_os_resume(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_resume() called\n"));

	rk_platform_power_on_gpu(device);

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->resume) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->resume(device);
	}

	return ret;
}

static int mali_os_freeze(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_freeze() called\n"));

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->freeze) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->freeze(device);
	}

	return ret;
}

static int mali_os_thaw(struct device *device)
{
	int ret = 0;

	MALI_DEBUG_PRINT(4, ("mali_os_thaw() called\n"));

	if (device->driver &&
	    device->driver->pm &&
	    device->driver->pm->thaw) {
		/* Need to notify Mali driver about this event */
		ret = device->driver->pm->thaw(device);
	}

	return ret;
}

static const struct dev_pm_ops mali_gpu_device_type_pm_ops = {
	.suspend = mali_os_suspend,
	.resume = mali_os_resume,
	.freeze = mali_os_freeze,
	.thaw = mali_os_thaw,
#ifdef CONFIG_PM
	.runtime_suspend = mali_runtime_suspend,
	.runtime_resume = mali_runtime_resume,
	.runtime_idle = mali_runtime_idle,
#endif
};

static const struct device_type mali_gpu_device_device_type = {
	.pm = &mali_gpu_device_type_pm_ops,
};

/*
 * platform_specific_data of platform_device of mali_gpu.
 */
static const struct mali_gpu_device_data mali_gpu_data = {
	.shared_mem_size = 1024 * 1024 * 1024, /* 1GB */
	.max_job_runtime = 60000, /* 60 seconds */
#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)
	.gpu_cooling_ops = &rk_cooling_ops,
#endif
};

static void mali_platform_device_add_config(struct platform_device *pdev)
{
	pdev->name = MALI_GPU_NAME_UTGARD,
	pdev->id = 0;
	pdev->dev.type = &mali_gpu_device_device_type;
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask,
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
}

/*---------------------------------------------------------------------------*/
/* platform_device_functions called by common_part. */

int mali_platform_device_init(struct platform_device *pdev)
{
	int err = 0;

	mali_platform_device_add_config(pdev);

	D("to add platform_specific_data to platform_device_of_mali.");
	err = platform_device_add_data(pdev,
				       &mali_gpu_data,
				       sizeof(mali_gpu_data));
	if (err) {
		E("fail to add platform_specific_data. err : %d.", err);
		goto add_data_failed;
	}

	err = rk_context_init(pdev);
	if (err) {
		E("fail to init rk_context. err : %d.", err);
		goto init_rk_context_failed;
	}

#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)
	if (of_machine_is_compatible("rockchip,rk3036"))
		return 0;

	err = power_model_simple_init(pdev);
	if (err) {
		E("fail to init simple_power_model, err : %d.", err);
		goto init_power_model_failed;
	}
#endif

	return 0;

#if defined(CONFIG_MALI_DEVFREQ) && defined(CONFIG_DEVFREQ_THERMAL)
init_power_model_failed:
	rk_context_deinit(pdev);
#endif
init_rk_context_failed:
add_data_failed:
	return err;
}

void mali_platform_device_deinit(struct platform_device *pdev)
{
	MALI_DEBUG_PRINT(4, ("mali_platform_device_unregister() called\n"));

	rk_context_deinit(pdev);
}
