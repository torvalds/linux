/* linux/arch/arm/mach-exynos/tmu-exynos.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
 *
 * EXYNOS - Thermal Management support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/slab.h>

#include <mach/regs-tmu.h>
#include <mach/cpufreq.h>
#include <mach/tmu.h>
#include <mach/asv.h>
#ifdef CONFIG_BUSFREQ_OPP
#include <mach/busfreq_exynos4.h>
#include <mach/dev.h>
#endif
#include <mach/smc.h>

#include <plat/cpu.h>

static DEFINE_MUTEX(tmu_lock);

unsigned int already_limit;
unsigned int auto_refresh_changed;
static struct workqueue_struct  *tmu_monitor_wq;

static void tmu_tripped_cb(void)
{
	/* To do */
	pr_info("It is high temperature.\n");
	pr_info("If temperature is higher 3 degree than now ");
	pr_info("Power will be off automatically!!\n");
}

static int get_cur_temp(struct tmu_info *info)
{
	int curr_temp;
	int temperature;

	/* After reading temperature code from register, compensating
	 * its value and calculating celsius temperatue,
	 * get current temperatue.
	 */
	curr_temp = __raw_readl(info->tmu_base + CURRENT_TEMP) & 0xff;

	/* compensate and calculate current temperature */
	temperature = curr_temp - info->te1 + TMU_DC_VALUE;
	if (temperature < 5) {
		/* temperature code range are between min 10 and 125 */
		pr_alert("Current temperature is in inaccurate range->"
			" check if vdd_18_ts is on or room temperature.\n");
	}

	return temperature;
}

/* Sysfs interface for thermal information */
static ssize_t show_temperature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	int temperature;

	if (!dev)
		return -ENODEV;

	temperature = get_cur_temp(info);
	return sprintf(buf, "%d\n", temperature);
}

static ssize_t show_throttle(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int start, stop;
	ssize_t ret = 0;

	start = data->ts.start_throttle;
	stop = data->ts.stop_throttle;

	if (!dev)
		return -ENODEV;

	ret += sprintf(buf+ret, "Throttling/Cooling: %dc/%dc\n", start, stop);
	ret += sprintf(buf+ret, "\n");
	ret += sprintf(buf+ret, "[Change usage] echo Throttling_temp"
			" Cooling_temp > throttle_temp\n");
	ret += sprintf(buf+ret, "[Example] echo 80 75 > throttle_temp\n");

	return ret;
}

static ssize_t store_throttle(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int start, stop;
	unsigned int tmp;
	unsigned char temp_throttle;

	if (!dev)
		return -ENODEV;

	if (!sscanf(buf, "%u %u", &start, &stop)) {
		printk(KERN_ERR "Invaild format!\n");
		return -EINVAL;
	}

	if (start >= data->ts.stop_warning) {
		printk(KERN_ERR "[Wrong value] - Throttling start temp needs"
				" smaller vaule than warning stop temp\n");
		return -ENODEV;
	}

	if (stop >= start || (stop <= 0)) {
		printk(KERN_ERR "[Wrong value] - Cooling temp needs smaller"
				" positive value than throttle start temp\n");
		return -ENODEV;
	}

	mutex_lock(&tmu_lock);

	data->ts.start_throttle = start;
	data->ts.stop_throttle = stop;

	temp_throttle = data->ts.start_throttle
			+ info->te1 - TMU_DC_VALUE;

	tmp = __raw_readl(info->tmu_base + THD_TEMP_RISE);
	tmp &= ~(0xFF);
	/* Set interrupt trigger level */
	tmp |= (temp_throttle << 0);
	__raw_writel(tmp, info->tmu_base + THD_TEMP_RISE);

	mutex_unlock(&tmu_lock);

	return count;
}

static ssize_t show_warning(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int start, stop;
	ssize_t ret = 0;

	start = data->ts.start_warning;
	stop = data->ts.stop_warning;

	if (!dev)
		return -ENODEV;

	ret += sprintf(buf+ret, "Waring/Cooling: %dc/%dc\n", start, stop);
	ret += sprintf(buf+ret, "\n");
	ret += sprintf(buf+ret, "[Change usage] echo warning_temp"
			" waring_stop_temp > warning_temp\n");
	ret += sprintf(buf+ret, "[Example] echo 100 90 > warning_temp\n");

	return ret;
}

static ssize_t store_warning(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int start, stop;
	unsigned int tmp;
	unsigned char temp_warning;

	if (!dev)
		return -ENODEV;

	if (!sscanf(buf, "%u %u", &start, &stop)) {
		printk("Invaild format!\n");
		return -EINVAL;
	}

	if (start <= data->ts.start_throttle
		|| (start >= data->ts.start_tripping)) {
		pr_err("[Wrong value] - Warning start temp needs"
			" value between throttle start and tirpping temp\n");
		return -ENODEV;
	}

	if ((stop <= data->ts.start_throttle) || (stop >= start)) {
		pr_err("[Wrong value] - Cooling temp needs"
				" value between throttle start and warning start temp\n");
		return -ENODEV;
	}

	mutex_lock(&tmu_lock);
	data->ts.start_warning = start;
	data->ts.stop_warning = stop;
	temp_warning = data->ts.start_warning
			+ info->te1 - TMU_DC_VALUE;

	tmp = __raw_readl(info->tmu_base + THD_TEMP_RISE);
	tmp = tmp & ~((0xFF)<<8);
	/* Set interrupt trigger level */
	tmp |= (temp_warning << 8);
	__raw_writel(tmp, info->tmu_base + THD_TEMP_RISE);

	mutex_unlock(&tmu_lock);

	return count;
}

static ssize_t show_throttle_freq(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int freq;
	ssize_t ret = 0;

	freq = info->throttle_freq;

	if (!dev)
		return -ENODEV;

	ret += sprintf(buf+ret, "Throttling freq level: %d (%dMHz)\n", \
			freq, (data->cpulimit.throttle_freq)/1000);
	ret += sprintf(buf+ret, "\n");
	ret += sprintf(buf+ret, "[Change usage] echo freq > throttle_freq\n");
	ret += sprintf(buf+ret, "[Example] If you want to set 800Mhz,"
				" write 800 to freq\n");

	return ret;
}

static ssize_t store_throttle_freq(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int freq;

	if (!dev)
		return -ENODEV;

	if (!sscanf(buf, "%u", &freq)) {
		printk(KERN_ERR "Invaild format!\n");
		return -EINVAL;
	}

	mutex_lock(&tmu_lock);
	data->cpulimit.throttle_freq = (freq * 1000);
	/* Set frequecny level */
	exynos_cpufreq_get_level(data->cpulimit.throttle_freq,
				&info->throttle_freq);
	mutex_unlock(&tmu_lock);

	return count;
}

static ssize_t show_warning_freq(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int freq;
	ssize_t ret = 0;

	freq = info->warning_freq;

	if (!dev)
		return -ENODEV;

	ret += sprintf(buf+ret, "Warning freq level: %d (%dMHz)\n", \
			freq, (data->cpulimit.warning_freq)/1000);
	ret += sprintf(buf+ret, "\n");
	ret += sprintf(buf+ret, "[Change usage] echo freq > warning_freq\n");
	ret += sprintf(buf+ret, "[Example] If you want to set 200Mhz,"
				" write 200 to freq\n");

	return ret;
}

static ssize_t store_warning_freq(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct tmu_info *info = dev_get_drvdata(dev);
	struct tmu_data *data = info->dev->platform_data;
	unsigned int freq;

	if (!dev)
		return -ENODEV;

	if (!sscanf(buf, "%u", &freq)) {
		printk(KERN_ERR "Invaild format!\n");
		return -EINVAL;
	}

	mutex_lock(&tmu_lock);
	data->cpulimit.warning_freq = (freq * 1000);
	/* Set frequecny level */
	exynos_cpufreq_get_level(data->cpulimit.warning_freq,
				&info->warning_freq);
	mutex_unlock(&tmu_lock);

	return count;
}
static DEVICE_ATTR(temperature, 0444, show_temperature, NULL);
static DEVICE_ATTR(throttle_temp, 0666, show_throttle, store_throttle);
static DEVICE_ATTR(warning_temp, 0666, show_warning, store_warning);
static DEVICE_ATTR(throttle_freq, 0666,
			show_throttle_freq, store_throttle_freq);
static DEVICE_ATTR(warning_freq, 0666, show_warning_freq, store_warning_freq);

static int thermal_create_sysfs_file(struct device *dev)
{
	if (device_create_file(dev, &dev_attr_temperature)) {
		pr_err("Failed to create sysfs file [temperature]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_throttle_temp)) {
		pr_err("Failed to create sysfs file [throttle_temp]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_warning_temp)) {
		pr_err("Failed to create sysfs file [warning_temp]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_throttle_freq)) {
		pr_err("Failed to create sysfs file [throttle_freq]\n");
		goto out;
	}

	if (device_create_file(dev, &dev_attr_warning_freq)) {
		pr_err("Failed to create sysfs file [warning_freq]\n");
		goto out;
}
	return 0;
out:
	return -ENOENT;
}

static void thermal_remove_sysfs_file(struct device *dev)
{
	device_remove_file(dev, &dev_attr_temperature);
	device_remove_file(dev, &dev_attr_throttle_temp);
	device_remove_file(dev, &dev_attr_warning_temp);
	device_remove_file(dev, &dev_attr_throttle_freq);
	device_remove_file(dev, &dev_attr_warning_freq);
}
/* End of Interface sysfs for thermal information */

static void print_temperature_params(struct tmu_info *info)
{
	struct tmu_data *data = info->dev->platform_data;

	pr_info("** temperature set value **\n"
		"Throttling stop_temp  = %d  start_temp     = %d\n"
		"Waring stop_temp      = %d start_tmep     = %d\n"
		"Tripping temp         = %d\n"
		"Hw_tripping temp      = %d\n"
		"Mem throttle stop_temp= %d, start_temp     = %d\n"
		"Trhottling freq = %d   Warning freq = %d\n",
		data->ts.stop_throttle,
		data->ts.start_throttle,
		data->ts.stop_warning,
		data->ts.start_warning,
		data->ts.start_tripping,
		data->ts.start_hw_tripping,
		data->ts.stop_mem_throttle,
		data->ts.start_mem_throttle,
		data->cpulimit.throttle_freq,
		data->cpulimit.warning_freq);
#if defined(CONFIG_TC_VOLTAGE)
	pr_info("TC_voltage stop_temp = %d  Start_temp = %d\n",
		data->ts.stop_tc, data->ts.start_tc);
#endif
}

#ifdef CONFIG_TMU_DEBUG
static void cur_temp_monitor(struct work_struct *work)
{
	int cur_temp;
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct tmu_info *info =
		 container_of(delayed_work, struct tmu_info, monitor);

	cur_temp = get_cur_temp(info);
	pr_info("current temp = %d\n", cur_temp);
	queue_delayed_work_on(0, tmu_monitor_wq,
			&info->monitor, info->sampling_rate);
}
#endif


unsigned int get_refresh_interval(unsigned int freq_ref,
					unsigned int refresh_nsec)
{
	unsigned int uRlk, refresh = 0;

	/*
	 * uRlk = FIN / 100000;
	 * refresh_usec =  (unsigned int)(fMicrosec * 10);
	 * uRegVal = ((unsigned int)(uRlk * uMicroSec / 100)) - 1;
	 * refresh =
	 * (unsigned int)(freq_ref * (unsigned int)(refresh_usec * 10) /
	 * 100) - 1;
	*/
	uRlk = freq_ref / 1000000;
	refresh = ((unsigned int)(uRlk * refresh_nsec / 1000));

	pr_info("@@@ get_refresh_interval = 0x%02x\n", refresh);
	return refresh;
}

void set_refresh_rate(unsigned int auto_refresh)
{
	/*
	 * uRlk = FIN / 100000;
	 * refresh_usec =  (unsigned int)(fMicrosec * 10);
	 * uRegVal = ((unsigned int)(uRlk * uMicroSec / 100)) - 1;
	*/
	pr_debug("@@@ set_auto_refresh = 0x%02x\n", auto_refresh);

#ifdef CONFIG_ARCH_EXYNOS4
#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc(SMC_CMD_REG,
		SMC_REG_ID_SFR_W((EXYNOS4_PA_DMC0_4212 + TIMING_AREF_OFFSET)),
		auto_refresh, 0);
	exynos_smc(SMC_CMD_REG,
		SMC_REG_ID_SFR_W((EXYNOS4_PA_DMC1_4212 + TIMING_AREF_OFFSET)),
		auto_refresh, 0);
#else
	/* change auto refresh period in TIMING_AREF register of dmc0  */
	__raw_writel(auto_refresh, S5P_VA_DMC0 + TIMING_AREF_OFFSET);

	/* change auto refresh period in TIMING_AREF regisger of dmc1 */
	__raw_writel(auto_refresh, S5P_VA_DMC1 + TIMING_AREF_OFFSET);
#endif
#endif	/* CONFIG_ARCH_EXYNOS4 */
}

#if defined(CONFIG_TC_VOLTAGE)
/**
 * exynos_tc_volt - locks or frees vdd_arm, vdd_mif/int and vdd_g3d for
 * temperature compensation.
 *
 * This function limits or free voltage of cpufreq, busfreq, and mali driver
 * according to 2nd arguments.
 */
static int exynos_tc_volt(struct tmu_info *info, int enable)
{
	struct tmu_data *data = info->dev->platform_data;
	static int usage;
	int ret = 0;

	if (!info)
		return -EPERM;

	if (enable == usage) {
		pr_debug("TMU: already is %s.\n",
			enable ? "locked" : "unlocked");
		return 0;
	}

	if (enable) {
		ret = exynos_cpufreq_lock(DVFS_LOCK_ID_TMU, info->cpulevel_tc);
		if (ret)
			goto err_lock;
#ifdef CONFIG_BUSFREQ_OPP
		ret = dev_lock(info->bus_dev, info->dev, info->busfreq_tc);
		if (ret)
			goto err_lock;
#endif
		/* ret = mali_voltage_lock_push(data->temp_compensate.g3d_volt);
		if (ret < 0) {
			pr_err("TMU: g3d_push error: %u uV\n",
				data->temp_compensate.g3d_volt);
			goto err_lock;
		}
		pr_info("Lock for TC is sucessful..\n"); */ 
	} else {
		exynos_cpufreq_lock_free(DVFS_LOCK_ID_TMU);
#ifdef CONFIG_BUSFREQ_OPP
		ret = dev_unlock(info->bus_dev, info->dev);
		if (ret)
			goto err_unlock;
#endif
		/*ret = mali_voltage_lock_pop();
		if (ret < 0) {
			pr_err("TMU: g3d_pop error\n");
			goto err_unlock;
		}
		pr_info("Unlock for TC is sucessful..\n"); */
	}
	usage = enable;
	return ret;

err_lock:
err_unlock:
	pr_err("TMU: %s is fail.\n", enable ? "Lock" : "Unlock");
	return ret;
}
#endif

static void tmu_monitor(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct tmu_info *info =
		container_of(delayed_work, struct tmu_info, polling);
	struct tmu_data *data = info->dev->platform_data;
	int cur_temp;

	cur_temp = get_cur_temp(info);
#ifdef CONFIG_TMU_DEBUG
	cancel_delayed_work(&info->monitor);
	pr_info("Current: %dc, FLAG=%d\n",
			cur_temp, info->tmu_state);
#endif
	mutex_lock(&tmu_lock);
	switch (info->tmu_state) {
#if defined(CONFIG_TC_VOLTAGE)
	case TMU_STATUS_TC:
		if (cur_temp >= data->ts.stop_tc) {
			if (exynos_tc_volt(info, 0) < 0)
				pr_err("%s\n", __func__);
			info->tmu_state = TMU_STATUS_NORMAL;
			already_limit = 0;
			pr_info("TC limit is released!!\n");
		} else if (cur_temp <= data->ts.start_tc && !already_limit) {
			if (exynos_tc_volt(info, 1) < 0)
				pr_err("%s\n", __func__);
			already_limit = 1;
		}
		break;
#endif
	case TMU_STATUS_NORMAL:
#ifdef CONFIG_TMU_DEBUG
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->monitor, info->sampling_rate);
#endif
		__raw_writel((CLEAR_RISE_INT|CLEAR_FALL_INT),
					info->tmu_base + INTCLEAR);
		enable_irq(info->irq);
		mutex_unlock(&tmu_lock);
		return;

	case TMU_STATUS_THROTTLED:
		if (cur_temp >= data->ts.start_warning) {
			info->tmu_state = TMU_STATUS_WARNING;
			exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
			already_limit = 0;
		} else if (cur_temp > data->ts.stop_throttle &&
				cur_temp < data->ts.start_warning &&
							!already_limit) {
			exynos_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
				info->throttle_freq);
			already_limit = 1;
		} else if (cur_temp <= data->ts.stop_throttle) {
			info->tmu_state = TMU_STATUS_NORMAL;
			exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
			pr_info("Freq limit is released!!\n");
			already_limit = 0;
		}
		break;

	case TMU_STATUS_WARNING:
		if (cur_temp >= data->ts.start_tripping) {
			info->tmu_state = TMU_STATUS_TRIPPED;
			already_limit = 0;
		} else if (cur_temp > data->ts.stop_warning && \
				cur_temp < data->ts.start_tripping &&
							!already_limit) {
			exynos_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
							info->warning_freq);
			already_limit = 1;
		} else if (cur_temp <= data->ts.stop_warning) {
			info->tmu_state = TMU_STATUS_THROTTLED;
			exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
			already_limit = 0;
		}
		break;

	case TMU_STATUS_TRIPPED:
		mutex_unlock(&tmu_lock);
		tmu_tripped_cb();
		return;
	default:
	    break;
	}

	/* memory throttling */
	if (cur_temp >= data->ts.start_mem_throttle
				&& !(auto_refresh_changed)) {
			pr_info("set auto_refresh 1.95us\n");
			set_refresh_rate(info->auto_refresh_tq0);
			auto_refresh_changed = 1;
	} else if (cur_temp <= (data->ts.stop_mem_throttle)
				&& (auto_refresh_changed)) {
			pr_info("set auto_refresh 3.9us\n");
			set_refresh_rate(info->auto_refresh_normal);
			auto_refresh_changed = 0;
	}

	queue_delayed_work_on(0, tmu_monitor_wq,
			&info->polling, info->sampling_rate);
	mutex_unlock(&tmu_lock);

	return;
}

static void pm_tmu_save(struct tmu_info *info)
{
	info->reg_save[0] = __raw_readl(info->tmu_base + TMU_CON);
	info->reg_save[1] = __raw_readl(info->tmu_base + SAMPLING_INTERNAL);
	info->reg_save[2] = __raw_readl(info->tmu_base + CNT_VALUE0);
	info->reg_save[3] = __raw_readl(info->tmu_base + CNT_VALUE1);
	info->reg_save[4] = __raw_readl(info->tmu_base + INTEN);

	if (soc_is_exynos4210()) {
		info->reg_save[5] = __raw_readl(info->tmu_base	\
						+ THRESHOLD_TEMP);
		info->reg_save[6] = __raw_readl(info->tmu_base + TRIG_LEV0);
		info->reg_save[7] = __raw_readl(info->tmu_base + TRIG_LEV1);
		info->reg_save[8] = __raw_readl(info->tmu_base + TRIG_LEV2);
		info->reg_save[9] = __raw_readl(info->tmu_base + TRIG_LEV3);
	} else {
		info->reg_save[5] = __raw_readl(info->tmu_base + THD_TEMP_RISE);
#if defined(CONFIG_TC_VOLTAGE)
		info->reg_save[6] = __raw_readl(info->tmu_base
					+ THD_TEMP_FALL);
#endif
	}
}

static void pm_tmu_restore(struct tmu_info *info)
{
	if (soc_is_exynos4210()) {
		__raw_writel(info->reg_save[9], info->tmu_base + TRIG_LEV3);
		__raw_writel(info->reg_save[8], info->tmu_base + TRIG_LEV2);
		__raw_writel(info->reg_save[7], info->tmu_base + TRIG_LEV1);
		__raw_writel(info->reg_save[6], info->tmu_base + TRIG_LEV0);
		__raw_writel(info->reg_save[5], info->tmu_base	\
						+ THRESHOLD_TEMP);
	} else {
#if defined(CONFIG_TC_VOLTAGE)
		 __raw_writel(info->reg_save[6], info->tmu_base \
						+ THD_TEMP_FALL);
#endif
		__raw_writel(info->reg_save[5], info->tmu_base + THD_TEMP_RISE);
	}
	__raw_writel(info->reg_save[4], info->tmu_base + INTEN);
	__raw_writel(info->reg_save[3], info->tmu_base + CNT_VALUE1);
	__raw_writel(info->reg_save[2], info->tmu_base + CNT_VALUE0);
	__raw_writel(info->reg_save[0], info->tmu_base + TMU_CON);
	__raw_writel(info->reg_save[1], info->tmu_base + SAMPLING_INTERNAL);
}

static int exynos4210_tmu_init(struct tmu_info *info)
{
	struct tmu_data *data = info->dev->platform_data;
	int con;
	unsigned int te_temp;
	unsigned int temp_threshold;
	unsigned int temp_throttle, temp_warning, temp_trip;

	/* get the compensation parameter */
	te_temp = __raw_readl(info->tmu_base + TRIMINFO);
	info->te1 = te_temp & TRIM_INFO_MASK;
	info->te2 = ((te_temp >> 8) & TRIM_INFO_MASK);

	if ((EFUSE_MIN_VALUE > info->te1) || (info->te1 > EFUSE_MAX_VALUE)
		||  (info->te2 != 0))
		info->te1 = data->efuse_value;

	/* Convert celsius temperature value to temperature code value
	* such as threshold_level, 1st throttle, 2nd throttle,
	* tripping temperature.
	*/
	temp_threshold = data->ts.stop_throttle
			+ info->te1 - TMU_DC_VALUE;
	temp_throttle = data->ts.start_throttle
			- data->ts.stop_throttle;
	temp_warning = data->ts.start_warning
			- data->ts.stop_throttle;
	temp_trip = data->ts.start_tripping
			- data->ts.stop_throttle;

	/* Set interrupt trigger level */
	__raw_writel(temp_threshold, info->tmu_base + THRESHOLD_TEMP);
	__raw_writel(temp_throttle, info->tmu_base + TRIG_LEV0);
	__raw_writel(temp_warning, info->tmu_base + TRIG_LEV1);
	__raw_writel(temp_trip, info->tmu_base + TRIG_LEV2);

	/* Clear interrupt ot eliminate dummy interrupt signal */
	__raw_writel(INTCLEARALL, info->tmu_base + INTCLEAR);

	/* Set frequecny level */
	exynos_cpufreq_get_level(data->cpulimit.throttle_freq,
				&info->throttle_freq);
	exynos_cpufreq_get_level(data->cpulimit.warning_freq,
				&info->warning_freq);

	/* Need to initail regsiter setting after getting parameter info */
	/* [28:23] vref [11:8] slope - Tunning parameter */
	/* TMU core enable */
	con = __raw_readl(info->tmu_base + TMU_CON);
	con |= (data->slope | CORE_EN);
	__raw_writel(con, info->tmu_base + TMU_CON);

	/* Because temperature sensing time is appro 940us,
	* tmu is enabled and 1st valid sample can get 1ms after.
	*/
	mdelay(1);
	__raw_writel(INTEN0 | INTEN1 | INTEN2, info->tmu_base + INTEN);

	return 0;

}
static int exynos_tmu_init(struct tmu_info *info)
{
	struct tmu_data *data = info->dev->platform_data;
	unsigned int te_temp, con;
	unsigned int temp_throttle, temp_warning, temp_trip;
	unsigned int hw_temp_trip;
	unsigned int rising_thr = 0, cooling_thr = 0;

	/* must reload for using efuse value at EXYNOS4212 */
	__raw_writel(TRIMINFO_RELOAD, info->tmu_base + TRIMINFO_CON);

	/* get the compensation parameter */
	te_temp = __raw_readl(info->tmu_base + TRIMINFO);
	info->te1 = te_temp & TRIM_INFO_MASK;
	info->te2 = ((te_temp >> 8) & TRIM_INFO_MASK);

	if ((EFUSE_MIN_VALUE > info->te1) || (info->te1 > EFUSE_MAX_VALUE)
		||  (info->te2 != 0))
		info->te1 = data->efuse_value;

	/*Get rising Threshold and Set interrupt level*/
	temp_throttle = data->ts.start_throttle
			+ info->te1 - TMU_DC_VALUE;
	temp_warning = data->ts.start_warning
			+ info->te1 - TMU_DC_VALUE;
	temp_trip =  data->ts.start_tripping
			+ info->te1 - TMU_DC_VALUE;
	hw_temp_trip = data->ts.start_hw_tripping
			+ info->te1 - TMU_DC_VALUE;

	rising_thr = (temp_throttle | (temp_warning<<8) | \
		(temp_trip<<16) | (hw_temp_trip<<24));

	__raw_writel(rising_thr, info->tmu_base + THD_TEMP_RISE);

#if defined(CONFIG_TC_VOLTAGE)
	/* Get set temperature for tc_voltage and set falling interrupt
	 * trigger level
	*/
	cooling_thr = data->ts.start_tc
			+ info->te1 - TMU_DC_VALUE;
#endif
	__raw_writel(cooling_thr, info->tmu_base + THD_TEMP_FALL);

	/* Set TMU status */
	info->tmu_state = TMU_STATUS_INIT;

	/* Set frequecny level */
	exynos_cpufreq_get_level(data->cpulimit.throttle_freq,
				&info->throttle_freq);
	exynos_cpufreq_get_level(data->cpulimit.warning_freq,
				&info->warning_freq);
	/* Map auto_refresh_rate of normal & tq0 mode */
	info->auto_refresh_tq0 =
		get_refresh_interval(FREQ_IN_PLL, AUTO_REFRESH_PERIOD_TQ0);
	info->auto_refresh_normal =
		get_refresh_interval(FREQ_IN_PLL, AUTO_REFRESH_PERIOD_NORMAL);

	/* To poll current temp, set sampling rate */
	info->sampling_rate  = usecs_to_jiffies(200 * 1000);

#if defined(CONFIG_TC_VOLTAGE) /* Temperature compensated voltage */
	if (exynos_find_cpufreq_level_by_volt(data->temp_compensate.arm_volt,
		&info->cpulevel_tc) < 0) {
		pr_err("cpufreq_get_level error\n");
		return  -EINVAL;
	}
#ifdef CONFIG_BUSFREQ_OPP
	/* To lock bus frequency in OPP mode */
	info->bus_dev = dev_get("exynos-busfreq");
	if (info->bus_dev < 0) {
		pr_err("Failed to get_dev\n");
		return -EINVAL;
	}
	if (exynos4x12_find_busfreq_by_volt(data->temp_compensate.bus_volt,
		&info->busfreq_tc)) {
		pr_err("get_busfreq_value error\n");
	}
#endif
	/*if (mali_voltage_lock_init()) {
		pr_err("Failed to initialize mail voltage lock.\n");
		return -EINVAL;
	}*/

	pr_info("%s: cpufreq_level[%d], busfreq_value[%d]\n",
		 __func__, info->cpulevel_tc, info->busfreq_tc);
#endif
	/* Need to initail regsiter setting after getting parameter info */
	/* [28:23] vref [11:8] slope - Tunning parameter */
	__raw_writel(data->slope, info->tmu_base + TMU_CON);

	__raw_writel((CLEAR_RISE_INT | CLEAR_FALL_INT),	\
				info->tmu_base + INTCLEAR);

	/* TMU core enable and HW trpping enable */
	con = __raw_readl(info->tmu_base + TMU_CON);
	con &= ~(HW_TRIP_MODE);
	con |= (HW_TRIPPING_EN | MUX_ADDR_VALUE<<20 | CORE_EN);
	__raw_writel(con, info->tmu_base + TMU_CON);

	/* Because temperature sensing time is appro 940us,
	* tmu is enabled and 1st valid sample can get 1ms after.
	*/
	mdelay(1);

	te_temp = __raw_readl(S5P_PMU_PS_HOLD_CONTROL);
	te_temp |= S5P_PS_HOLD_EN;
	__raw_writel(te_temp, S5P_PMU_PS_HOLD_CONTROL);

	/*LEV0 LEV1 LEV2 interrupt enable */
	__raw_writel(INTEN_RISE0 | INTEN_RISE1 | INTEN_RISE2,	\
		     info->tmu_base + INTEN);

#if defined(CONFIG_TC_VOLTAGE)
	te_temp = __raw_readl(info->tmu_base + INTEN);
	te_temp |= INTEN_FALL0;
	__raw_writel(te_temp, info->tmu_base + INTEN);

	/* s/w workaround for fast service when interrupt is not occured,
	* such as current temp is lower than tc interrupt temperature
	* or current temp is continuosly increased.
	*/
	if (get_cur_temp(info) <= data->ts.start_tc) {
		disable_irq_nosync(info->irq);
		if (exynos_tc_volt(info, 1) < 0)
			pr_err("%s\n", __func__);

		info->tmu_state = TMU_STATUS_TC;
		already_limit = 1;
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->polling, usecs_to_jiffies(1000));
}
#endif
	return 0;
}

static int tmu_initialize(struct platform_device *pdev)
{
	struct tmu_info *info = platform_get_drvdata(pdev);
	unsigned int en;
	int ret;

	en = (__raw_readl(info->tmu_base + TMU_STATUS) & 0x1);

	if (!en) {
		dev_err(&pdev->dev, "failed to start tmu drvier\n");
		return -ENOENT;
	}

	if (soc_is_exynos4210())
		ret = exynos4210_tmu_init(info);
	else
		ret = exynos_tmu_init(info);

	return ret;
}

static irqreturn_t tmu_irq(int irq, void *id)
{
	struct tmu_info *info = id;
	unsigned int status;

	disable_irq_nosync(irq);

	status = __raw_readl(info->tmu_base + INTSTAT);

	/* To handle multiple interrupt pending,
	* interrupt by high temperature are serviced with priority.
	*/

#if defined(CONFIG_TC_VOLTAGE)
	if (status & INTSTAT_FALL0) {
		pr_info("TC interrupt occured..!\n");
		__raw_writel(INTCLEAR_FALL0, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_TC;
	} else if (status & INTSTAT_RISE2) {
#else
	if (status & INTSTAT_RISE2) {
#endif
		pr_info("Tripping interrupt occured..!\n");
		info->tmu_state = TMU_STATUS_TRIPPED;
		__raw_writel(INTCLEAR_RISE2, info->tmu_base + INTCLEAR);
	} else if (status & INTSTAT_RISE1) {
		pr_info("Warning interrupt occured..!\n");
		__raw_writel(INTCLEAR_RISE1, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_WARNING;
	} else if (status & INTSTAT_RISE0) {
		pr_info("Throttling interrupt occured..!\n");
		__raw_writel(INTCLEAR_RISE0, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_THROTTLED;
	} else {
		pr_err("%s: TMU interrupt error\n", __func__);
		return -ENODEV;
	}

	queue_delayed_work_on(0, tmu_monitor_wq,
			&info->polling, usecs_to_jiffies(1 * 1000));
	return IRQ_HANDLED;
}

static irqreturn_t exynos4210_tmu_irq(int irq, void *id)
{
	struct tmu_info *info = id;
	unsigned int status;

	disable_irq_nosync(irq);

	status = __raw_readl(info->tmu_base + INTSTAT);

	if (status & INTSTAT2) {
		pr_info("Tripping interrupt occured..!\n");
		info->tmu_state = TMU_STATUS_TRIPPED;
		__raw_writel(INTCLEAR2, info->tmu_base + INTCLEAR);
	} else if (status & INTSTAT1) {
		pr_info("Warning interrupt occured..!\n");
		__raw_writel(INTCLEAR1, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_WARNING;
	} else if (status & INTSTAT0) {
		pr_info("Throttling interrupt occured..!\n");
		__raw_writel(INTCLEAR0, info->tmu_base + INTCLEAR);
		info->tmu_state = TMU_STATUS_THROTTLED;
	} else {
		pr_err("%s: TMU interrupt error\n", __func__);
		return -ENODEV;
	}

	queue_delayed_work_on(0, tmu_monitor_wq,
			&info->polling, usecs_to_jiffies(1000));
	return IRQ_HANDLED;
}

static int __devinit tmu_probe(struct platform_device *pdev)
{
	struct tmu_info *info;
	struct resource *res;
	int	ret = 0;

	pr_debug("%s: probe=%p\n", __func__, pdev);

	info = kzalloc(sizeof(struct tmu_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to alloc memory!\n");
		ret = -ENOMEM;
		goto err_nomem;
	}
	pr_emerg("TMU: Memory Allocation Sucessful\n");
	
	platform_set_drvdata(pdev, info);
	pr_emerg("TMU: Platform data set\n");
	
	info->dev = &pdev->dev;
	pr_emerg("TMU: Copied the Dev access Information \n");
	
	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(&pdev->dev, "no irq for thermal\n");
		ret = -ENOENT;
		goto err_noirq;
	}
	if (soc_is_exynos4210())
		ret = request_irq(info->irq, exynos4210_tmu_irq,
				IRQF_DISABLED,  "tmu interrupt", info);
	else
		ret = request_irq(info->irq, tmu_irq,
				IRQF_DISABLED,  "tmu interrupt", info);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", info->irq, ret);
		goto err_noirq;
	}
	pr_emerg("TMU: IRQ Granted!\n");
	
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get memory region resource\n");
		ret = -ENODEV;
		goto err_nores;
	}
	pr_emerg("TMU: IO Resource alloced on Memory\n");
	
	info->ioarea = request_mem_region(res->start,
			res->end-res->start+1, pdev->name);
	if (!(info->ioarea)) {
		dev_err(&pdev->dev, "failed to reserve memory region\n");
		ret = -EBUSY;
		goto err_nores;
	}
	pr_emerg("TMU: Memory area resersed\n");
	
	info->tmu_base = ioremap(res->start, (res->end - res->start) + 1);
	if (!(info->tmu_base)) {
		dev_err(&pdev->dev, "failed ioremap()\n");
		ret = -EINVAL;
		goto err_nomap;
	}
	pr_emerg("TMU: IO Memory Remapped\n");
	
	if (thermal_create_sysfs_file(&pdev->dev))
		goto err_sysfs;

	pr_emerg("TMU: Created Sysfs\n");
	
	tmu_monitor_wq = create_freezable_workqueue("tmu");
	if (!tmu_monitor_wq) {
		dev_err(&pdev->dev, "Creation of tmu_monitor_wq failed\n");
		ret = -EFAULT;
		goto err_wq;
	}
	pr_emerg("TMU: Workqueue Created\n");
	
	INIT_DELAYED_WORK_DEFERRABLE(&info->polling, tmu_monitor);
	pr_emerg("TMU: Work Created\n");
#ifdef CONFIG_TMU_DEBUG
	INIT_DELAYED_WORK_DEFERRABLE(&info->monitor, cur_temp_monitor);
#endif

	print_temperature_params(info);
	pr_emerg("TMU: Printed Parameters\n");
	
	ret = tmu_initialize(pdev);
	if (ret < 0)
		goto err_noinit;

#ifdef CONFIG_TMU_DEBUG
	queue_delayed_work_on(0, tmu_monitor_wq,
			&info->monitor, info->sampling_rate);
#endif
	pr_info("Tmu Initialization is sucessful...!\n");
	return ret;

err_noinit:
	destroy_workqueue(tmu_monitor_wq);
err_wq:
	thermal_remove_sysfs_file(&pdev->dev);
err_sysfs:
	iounmap(info->tmu_base);
err_nomap:
	release_resource(info->ioarea);
err_nores:
	free_irq(info->irq, info);
err_noirq:
	kfree(info);
	info = NULL;
err_nomem:
	dev_err(&pdev->dev, "initialization failed.\n");
	return ret;
}

static int __devinit tmu_remove(struct platform_device *pdev)
{
	struct tmu_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work(&info->polling);
	destroy_workqueue(tmu_monitor_wq);

	thermal_remove_sysfs_file(&pdev->dev);

	iounmap(info->tmu_base);
	release_resource(info->ioarea);

	free_irq(info->irq, (void *)pdev);

	kfree(info);
	info = NULL;

	pr_info("%s is removed\n", dev_name(&pdev->dev));
	return 0;
}

#ifdef CONFIG_PM
static int tmu_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct tmu_info *info = platform_get_drvdata(pdev);
	pm_tmu_save(info);

	return 0;
}

static int tmu_resume(struct platform_device *pdev)
{
	struct tmu_info *info = platform_get_drvdata(pdev);
#if defined(CONFIG_TC_VOLTAGE)
	struct tmu_data *data = info->dev->platform_data;
#endif
	pm_tmu_restore(info);

#if defined(CONFIG_TC_VOLTAGE)
	/* s/w workaround for fast service when interrupt is not occured,
	* such as current temp is lower than tc interrupt temperature
	* or current temp is continuosly increased.
	*/
	mdelay(1);
	if (get_cur_temp(info) <= data->ts.start_tc) {
		disable_irq_nosync(info->irq);
		if (exynos_tc_volt(info, 1) < 0)
			pr_err("%s\n", __func__);

		info->tmu_state = TMU_STATUS_TC;
		already_limit = 1;
		queue_delayed_work_on(0, tmu_monitor_wq,
				&info->polling, usecs_to_jiffies(1 * 1000));
	}
#endif
	return 0;
}

#else
#define tmu_suspend	NULL
#define tmu_resume	NULL
#endif

static struct platform_driver tmu_driver = {
	.probe		= tmu_probe,
	.remove		= tmu_remove,
	.suspend	= tmu_suspend,
	.resume		= tmu_resume,
	.driver		= {
		.name	=	"tmu",
		.owner	=	THIS_MODULE,
		},
};

static int __init tmu_driver_init(void)
{
	return platform_driver_register(&tmu_driver);
}

late_initcall(tmu_driver_init);
