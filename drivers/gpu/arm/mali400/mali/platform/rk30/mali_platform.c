/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from ARM Limited
 * (C) COPYRIGHT 2009-2012 ARM Limited
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from ARM Limited.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/gfp.h>
#include <linux/fs.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/regulator/driver.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/cpufreq.h>
#include <linux/of.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "arm_core_scaling.h"
#include "mali_platform.h"


static int mali_core_scaling_enable;

u32 mali_group_error;

struct device *mali_dev;

int mali_set_level(struct device *dev, int level)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	unsigned long freq;
	int ret;
	unsigned int current_level;

	_mali_osk_mutex_wait(drv_data->clockSetlock);

	current_level = drv_data->dvfs.current_level;
	freq = drv_data->fv_info[level].freq;

	if (level == current_level) {
		_mali_osk_mutex_signal(drv_data->clockSetlock);
		return 0;
	}

	ret = dvfs_clk_set_rate(drv_data->clk, freq);
	if (ret) {
		_mali_osk_mutex_signal(drv_data->clockSetlock);
		return ret;
	}

	dev_dbg(dev, "set freq %lu\n", freq);

	drv_data->dvfs.current_level = level;

	_mali_osk_mutex_signal(drv_data->clockSetlock);

	return 0;
}

static int mali_clock_init(struct device *dev)
{
	int ret;

	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	drv_data->pd = devm_clk_get(dev, "pd_gpu");
	if (IS_ERR(drv_data->pd)) {
		ret = PTR_ERR(drv_data->pd);
		dev_err(dev, "get pd_clk failed, %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(drv_data->pd);
	if (ret) {
		dev_err(dev, "prepare pd_clk failed, %d\n", ret);
		return ret;
	}

	drv_data->clk = clk_get_dvfs_node("clk_gpu");
	if (IS_ERR(drv_data->clk)) {
		ret = PTR_ERR(drv_data->clk);
		dev_err(dev, "prepare clk gpu failed, %d\n", ret);
		return ret;
	}

	ret = dvfs_clk_prepare_enable(drv_data->clk);
	if (ret) {
		dev_err(dev, "prepare clk failed, %d\n", ret);
		return ret;
	}

	drv_data->power_state = true;

	return 0;
}

static void mali_clock_term(struct device *dev)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	dvfs_clk_disable_unprepare(drv_data->clk);
	clk_disable_unprepare(drv_data->pd);
	drv_data->power_state = false;
}

static ssize_t show_available_frequencies(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	ssize_t ret = 0;
	u32 i;

	for (i = 0; i < drv_data->fv_info_length; i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%lu\n",
				 drv_data->fv_info[i].freq);

	return ret;
}

static ssize_t show_clock(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%lu\n", dvfs_clk_get_rate(drv_data->clk));
}

static ssize_t set_clock(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{	
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);
	unsigned long freq;
	ssize_t ret;
	u32 level;

	ret = kstrtoul(buf, 10, &freq);
	if (ret)
		return ret;

	for (level = drv_data->fv_info_length - 1; level > 0; level--) {
		unsigned long tmp  = drv_data->fv_info[level].freq;
		if (tmp <= freq)
			break;
	}

	dev_info(dev, "Using fv_info table %d: for %lu Hz\n", level, freq);

	ret = mali_set_level(dev, level);
	if (ret)
		return ret;

	return count;
}

static ssize_t show_dvfs_enable(struct device *dev,
			   	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", mali_dvfs_is_enabled(dev));
}

static ssize_t set_dvfs_enable(struct device *dev,
			      	struct device_attribute *attr, const char *buf,
			        size_t count)
{
	unsigned long enable;
	ssize_t ret;

	ret = kstrtoul(buf, 0, &enable);
	if (ret)
		return ret;

	if (enable == 1)
		mali_dvfs_enable(dev);
	else if (enable == 0)
		mali_dvfs_disable(dev);
	else
		return -EINVAL;

	return count;
}

static ssize_t show_utilisation(struct device *dev,
			   	struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", mali_dvfs_utilisation(dev));
}

static int error_count_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", mali_group_error);
}

DEVICE_ATTR(available_frequencies, S_IRUGO, show_available_frequencies, NULL);
DEVICE_ATTR(clock, S_IRUGO | S_IWUSR, show_clock, set_clock);
DEVICE_ATTR(dvfs_enable, S_IRUGO | S_IWUSR, show_dvfs_enable, set_dvfs_enable);
DEVICE_ATTR(utilisation, S_IRUGO, show_utilisation, NULL);
DEVICE_ATTR(error_count, 0644, error_count_show, NULL);

static struct attribute *mali_sysfs_entries[] = {
	&dev_attr_available_frequencies.attr,
	&dev_attr_clock.attr,
	&dev_attr_dvfs_enable.attr,
	&dev_attr_utilisation.attr,
	&dev_attr_error_count.attr,
	NULL,
};

static const struct attribute_group mali_attr_group = {
	.attrs	= mali_sysfs_entries,
};

static int mali_create_sysfs(struct device *dev)
{
	int ret;

	ret = sysfs_create_group(&dev->kobj, &mali_attr_group);
	if (ret)
		dev_err(dev, "create sysfs group error, %d\n", ret);

	return ret;
}

void mali_remove_sysfs(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &mali_attr_group);
}

_mali_osk_errcode_t mali_platform_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mali_platform_drv_data *mali_drv_data;
	int ret;

	mali_drv_data = devm_kzalloc(dev, sizeof(*mali_drv_data), GFP_KERNEL);
	if (!mali_drv_data) {
		dev_err(dev, "no mem\n");
		return _MALI_OSK_ERR_NOMEM;
	}

	dev_set_drvdata(dev, mali_drv_data);

	mali_drv_data->dev = dev;

	mali_dev = dev;

	ret = mali_clock_init(dev);
	if (ret)
		goto err_init;

	ret = mali_dvfs_init(dev);
	if (ret)
		goto err_init;

	ret = mali_create_sysfs(dev);
	if (ret)
		goto term_clk;

	mali_drv_data->clockSetlock = _mali_osk_mutex_init(_MALI_OSK_LOCKFLAG_ORDERED,
				_MALI_OSK_LOCK_ORDER_UTILIZATION);
	mali_core_scaling_enable = 1;

   	return 0;
term_clk:
	mali_clock_term(dev);
err_init:
 	return _MALI_OSK_ERR_FAULT;
}

_mali_osk_errcode_t mali_platform_deinit(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(dev);

	mali_core_scaling_term();
	mali_clock_term(dev);
	_mali_osk_mutex_term(drv_data->clockSetlock);

	return 0;
}

_mali_osk_errcode_t mali_power_domain_control(u32 bpower_off)
{
	struct mali_platform_drv_data *drv_data = dev_get_drvdata(mali_dev);

	if (bpower_off == 0) {
		if (!drv_data->power_state) {
			dvfs_clk_prepare_enable(drv_data->clk);
			clk_prepare_enable(drv_data->pd);
			drv_data->power_state = true;
		}
	} else if (bpower_off == 1) {
		if (drv_data->power_state) {
			dvfs_clk_disable_unprepare(drv_data->clk);
			clk_disable_unprepare(drv_data->pd);
			drv_data->power_state = false;
		}
	}

	return 0;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
	switch(power_mode) {
		case MALI_POWER_MODE_ON:
			MALI_DEBUG_PRINT(2, ("MALI_POWER_MODE_ON\r\n"));
			mali_power_domain_control(MALI_POWER_MODE_ON);
			break;
		case MALI_POWER_MODE_LIGHT_SLEEP:
			MALI_DEBUG_PRINT(2, ("MALI_POWER_MODE_LIGHT_SLEEP\r\n"));
			mali_power_domain_control(MALI_POWER_MODE_LIGHT_SLEEP);
			break;
		case MALI_POWER_MODE_DEEP_SLEEP:
			MALI_DEBUG_PRINT(2, ("MALI_POWER_MODE_DEEP_SLEEP\r\n"));
			mali_power_domain_control(MALI_POWER_MODE_DEEP_SLEEP);
			break;
		default:
			MALI_DEBUG_PRINT(2, ("mali_platform_power_mode_change:power_mode(%d) not support \r\n",
					 power_mode));
	}
	
    return 0;
}
void mali_gpu_utilization_handler(struct mali_gpu_utilization_data *data)
{
	if(data->utilization_pp > 256)
		return;

	if (mali_core_scaling_enable)
		mali_core_scaling_update(data);

	// dev_dbg(mali_dev, "utilization:%d\r\n", data->utilization_pp);

	mali_dvfs_event(mali_dev, data->utilization_pp);
}
