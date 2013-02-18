/* linux/arch/arm/mach-exynos/tmu.c
*
* Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com
*
 * EXYNOS4 - Thermal Management support
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
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/kobject.h>

#include <asm/irq.h>

#include <mach/regs-tmu.h>
#include <mach/cpufreq.h>
#include <mach/map.h>
#include <mach/smc.h>
#include <plat/s5p-tmu.h>
#include <plat/map-s5p.h>
#include <plat/gpio-cfg.h>
#include <plat/cpu.h>

#define CONFIG_TMU_DEBUG

/* for factory mode */
#define CONFIG_TMU_SYSFS

/* flags that throttling or trippint is treated */
#define THROTTLE_FLAG (0x1 << 0)
#define WARNING_FLAG (0x1 << 1)
#define TRIPPING_FLAG	(0x1 << 2)
#define MEM_THROTTLE_FLAG (0x1 << 4)

#define TIMING_AREF_OFFSET	0x30

static struct workqueue_struct  *tmu_monitor_wq;

static DEFINE_MUTEX(tmu_lock);

#ifdef CONFIG_ARCH_EXYNOS4
struct s5p_tmu_info_extend {
	void __iomem *tmu_base;
	unsigned char te1; /* triminfo_25 */
};
static struct s5p_tmu_info_extend info_ex;

unsigned int get_curr_temp_extend(void)
{
	unsigned char curr_temp_code;
	int temperature;

	if (!info_ex.tmu_base) {
		/* Called before tmu driver is loaded, can't read current_temp
		 * So return 0 for minimum voltage.
		*/
		pr_info("called before loading tmu driver!\n");
		return 0;
	}

	if (!info_ex.tmu_base) {
		/* Called before tmu driver is loaded, can't read current_temp
		 * So return 0 for minimum voltage.
		*/
		pr_info("called before loading tmu driver!\n");
		return 0;
	}

	/* After reading temperature code from register, compensating
	 * its value and calculating celsius temperatue,
	 * get current temperatue.
	*/
	curr_temp_code =
		__raw_readl(info_ex.tmu_base + EXYNOS4_TMU_CURRENT_TEMP) & 0xff;
	pr_debug("CURRENT_TEMP = 0x%02x\n", curr_temp_code);

	/* compensate and calculate current temperature */
	temperature = curr_temp_code - info_ex.te1 + TMU_DC_VALUE;
	if (temperature < 0) {
		/* temperature code range are extended btn min 0 & 125 */
		pr_info("current temp is under %d celsius degree!\n", temperature);
		temperature = 0;
	}

	return (unsigned int)temperature;
}
EXPORT_SYMBOL_GPL(get_curr_temp_extend);
#endif

static unsigned int get_curr_temp(struct s5p_tmu_info *info)
{
	unsigned char curr_temp_code;
	int temperature;

	if (!info)
		return -EAGAIN;

	/* After reading temperature code from register, compensating
	 * its value and calculating celsius temperatue,
	 * get current temperatue.
	*/
	curr_temp_code =
		__raw_readl(info->tmu_base + EXYNOS4_TMU_CURRENT_TEMP) & 0xff;

	/* Check range of temprature code with curr_temp_code & efusing info */
	pr_debug("CURRENT_TEMP = 0x%02x\n", curr_temp_code);
	if ((curr_temp_code - info->te1) < 0 || (curr_temp_code - info->te1) > 100)
		pr_err("temperature code is in inaccurate range->"
			"check if vdd_18_ts is on or room temperature.\n");

	/* compensate and calculate current temperature */
	temperature = curr_temp_code - info->te1 + TMU_DC_VALUE;
	if (temperature < TEMP_MIN_CELCIUS) {
		/* temperature code range are between min 25 and 125 */
		pr_info("current temp is under %d celsius degree!\n", TEMP_MIN_CELCIUS);
		temperature = TEMP_MIN_CELCIUS;
	}
	return (unsigned int)temperature;
}

static ssize_t show_temperature(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct s5p_tmu_info *info = dev_get_drvdata(dev);
	unsigned int temperature;

	if (!dev)
		return -ENODEV;

	mutex_lock(&tmu_lock);

	temperature = get_curr_temp(info);

	mutex_unlock(&tmu_lock);

	return sprintf(buf, "%d\n", temperature);
}

static ssize_t show_tmu_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct s5p_tmu_info *info = dev_get_drvdata(dev);

	if (!dev)
		return -ENODEV;

	return sprintf(buf, "%d\n", info->tmu_state);
}

static ssize_t show_lot_id(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 id1 = 0;
	u32 id2 = 0;
	id1 = __raw_readl(S5P_VA_CHIPID + 0x14);
	id2 = __raw_readl(S5P_VA_CHIPID + 0x18);

	return sprintf(buf, "%08x-%08x\n", id1, id2);
}
static DEVICE_ATTR(temperature, 0444, show_temperature, NULL);
static DEVICE_ATTR(tmu_state, 0444, show_tmu_state, NULL);
static DEVICE_ATTR(lot_id, 0444, show_lot_id, NULL);

static void print_temperature_params(struct s5p_tmu_info *info)
{
	struct s5p_platform_tmu *pdata = info->dev->platform_data;

	pr_info("** temperature set value **\n"
		"1st throttling stop_temp  = %d, start_temp     = %d\n"
		"2nd throttling stop_temp  = %d, start_tmep     = %d\n"
		"tripping temp             = %d, s/w emergency temp = %d\n"
		"mem throttling stop_temp  = %d, start_temp     = %d\n",
		pdata->ts.stop_1st_throttle,
		pdata->ts.start_1st_throttle,
		pdata->ts.stop_2nd_throttle,
		pdata->ts.start_2nd_throttle,
		pdata->ts.start_tripping,
		pdata->ts.start_emergency,
		pdata->ts.stop_mem_throttle,
		pdata->ts.start_mem_throttle);
}

unsigned int get_refresh_interval(unsigned int freq_ref,
					unsigned int refresh_nsec)
{
	unsigned int uRlk, refresh = 0;

	/*
	 * uRlk = FIN / 100000;
	 * refresh_usec =  (unsigned int)(fMicrosec * 10);
	 * uRegVal = ((unsigned int)(uRlk * uMicroSec / 100)) - 1;
	 * refresh =
	 * (unsigned int)(freq_ref * (unsigned int)(refresh_usec * 10) / 100) - 1;
	*/
	uRlk = freq_ref / 1000000;
	refresh = ((unsigned int)(uRlk * refresh_nsec / 1000));

	pr_info("@@@ get_refresh_interval = 0x%02x\n", refresh);
	return refresh;
}

#ifdef CONFIG_TMU_DEBUG
static int tmu_test_on;
static struct temperature_params in;
static int tmu_limit_on;
static int freq_limit_1st_throttle;
static int freq_limit_2nd_throttle;
static int set_sampling_rate;

static int tmu_print_temp_on_off;

static int __init get_temperature_params(char *str)
{
	unsigned int tmu_temp[8] = { (int)NULL, (int)NULL, (int)NULL,
		 (int)NULL, (int)NULL, (int)NULL, (int)NULL, (int)NULL };

	get_options(str, 8, tmu_temp);
	tmu_test_on = tmu_temp[0];
	printk(KERN_INFO "@@@ tmu_test enable = %d\n", tmu_test_on);

	if (tmu_temp[1] > 0)
		in.stop_1st_throttle = tmu_temp[1];

	if (tmu_temp[2] > 0)
		in.start_1st_throttle = tmu_temp[2];

	if (tmu_temp[3] > 0)
		in.stop_2nd_throttle = tmu_temp[3];

	if (tmu_temp[4] > 0)
		in.start_2nd_throttle = tmu_temp[4];

	if (tmu_temp[5] > 0)
		in.start_tripping = tmu_temp[5];

	if (tmu_temp[6] > 0)
		in.start_mem_throttle = tmu_temp[6];

	if (tmu_temp[7] > 0)
		in.start_emergency = tmu_temp[7];

	/*  output the input value */
	pr_info("@@ 1st throttling temp: start = %d, stop = %d @@\n"
		"@@ 2nd throttling temp: start = %d, stop = %d @@\n"
		"@@ trpping temp = %d, start_tq0 temp = %d     @@\n"
		"@@ emergency temp = %d\n",
		in.start_1st_throttle, in.stop_1st_throttle,
		in.start_2nd_throttle, in.stop_2nd_throttle,
		in.start_tripping, in.start_mem_throttle,
		in.start_emergency);

	return 0;
}
early_param("tmu_test", get_temperature_params);

static int __init get_cpufreq_limit_param(char *str)
{
	int tmu_temp[3] = { (int)NULL, (int)NULL, (int)NULL};

	get_options(str, 3, tmu_temp);

	tmu_limit_on = tmu_temp[0];
	printk(KERN_INFO "@@@ tmu_limit_on = %d\n", tmu_limit_on);

	if (tmu_temp[1] > 0)
		freq_limit_1st_throttle = tmu_temp[1];

	if (tmu_temp[2] > 0)
		freq_limit_2nd_throttle = tmu_temp[2];

	pr_info("@@ 1st throttling : cpu_level = %d, 2nd cpu_level = %d\n",
		freq_limit_1st_throttle, freq_limit_2nd_throttle);

	return 0;
}
early_param("cpu_level", get_cpufreq_limit_param);

static int __init get_sampling_rate_param(char *str)
{
	get_option(&str, &set_sampling_rate);
	if (set_sampling_rate < 0)
		set_sampling_rate = 0;

	return 0;
}
early_param("tmu_sampling_rate", get_sampling_rate_param);

static void exynos4_poll_cur_temp(struct work_struct *work)
{
	unsigned int cur_temp;
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct s5p_tmu_info *info =
		container_of(delayed_work, struct s5p_tmu_info, monitor);

	mutex_lock(&tmu_lock);

	cur_temp = get_curr_temp(info);

	if (tmu_print_temp_on_off)
		pr_info("curr temp in polling_interval = %d state = %d\n"
				, cur_temp, info->tmu_state);
	else
		pr_debug("curr temp in polling_interval = %d\n", cur_temp);

	queue_delayed_work_on(0, tmu_monitor_wq, &info->monitor,
			info->monitor_period);

	mutex_unlock(&tmu_lock);
}

static ssize_t tmu_show_print_state(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int ret;

	ret = sprintf(buf, "[TMU] tmu_print_temp_on_off=%d\n"
					, tmu_print_temp_on_off);

	return ret;
}

static ssize_t tmu_store_print_state(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;

	if (!strncmp(buf, "0", 1)) {
		tmu_print_temp_on_off = 0;
		ret = 0;
	} else if (!strncmp(buf, "1", 1)) {
		tmu_print_temp_on_off = 1;
		ret = 1;
	} else {
		dev_err(dev, "Invalid cmd !!\n");
		return -EINVAL;
	}

	return ret;
}
static DEVICE_ATTR(print_state, S_IRUGO | S_IWUSR,\
	tmu_show_print_state, tmu_store_print_state);
#endif

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
#else	/* CONFIG_ARCH_EXYNOS4 */
#ifdef CONFIG_ARM_TRUSTZONE
	exynos_smc(SMC_CMD_REG,
		SMC_REG_ID_SFR_W((EXYNOS5_PA_DMC + TIMING_AREF_OFFSET)),
		auto_refresh, 0);
#else
	/* change auto refresh period in TIMING_AREF register of dmc */
	__raw_writel(auto_refresh, S5P_VA_DMC0 + TIMING_AREF_OFFSET);
#endif
#endif	/* CONFIG_ARCH_EXYNOS4 */
}

static void set_temperature_params(struct s5p_tmu_info *info)
{
#ifdef CONFIG_TMU_DEBUG
	struct s5p_platform_tmu *data = info->dev->platform_data;

	if (tmu_test_on) {
		/* In the tmu_test mode, change temperature_params value
		 * input data.
		*/
		data->ts.stop_1st_throttle = in.stop_1st_throttle;
		data->ts.start_1st_throttle = in.start_1st_throttle;
		data->ts.stop_2nd_throttle = in.stop_2nd_throttle;
		data->ts.start_2nd_throttle = in.start_2nd_throttle;
		data->ts.start_tripping = in.start_tripping;
		data->ts.start_emergency = in.start_emergency;
		data->ts.stop_mem_throttle = in.start_mem_throttle - 5;
		data->ts.start_mem_throttle = in.start_mem_throttle;
	}
	if (tmu_limit_on) {
		info->cpufreq_level_1st_throttle = freq_limit_1st_throttle;
		info->cpufreq_level_2nd_throttle = freq_limit_2nd_throttle;
	}
	if (set_sampling_rate) {
		info->sampling_rate =
			usecs_to_jiffies(set_sampling_rate * 1000);
		info->monitor_period =
			usecs_to_jiffies(set_sampling_rate * 10 * 1000);
	}
#endif
	print_temperature_params(info);
}

static int notify_change_of_tmu_state(struct s5p_tmu_info *info)
{
	char temp_buf[20];
	char *envp[2];
	int env_offset = 0;

	snprintf(temp_buf, sizeof(temp_buf), "TMUSTATE=%d", info->tmu_state);
	envp[env_offset++] = temp_buf;
	envp[env_offset] = NULL;

	pr_info("%s: uevent: %d, name = %s\n",
			__func__, info->tmu_state, temp_buf);

	return kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp);
}

static void exynos4_handler_tmu_state(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct s5p_tmu_info *info =
		container_of(delayed_work, struct s5p_tmu_info, polling);
	struct s5p_platform_tmu *data = info->dev->platform_data;
	unsigned int cur_temp;
	static int auto_refresh_changed;
	static int check_handle;
	int trend = 0;

	mutex_lock(&tmu_lock);

	cur_temp = get_curr_temp(info);
	trend = cur_temp - info->last_temperature;
	pr_debug("curr_temp = %d, temp_diff = %d\n", cur_temp, trend);

	switch (info->tmu_state) {
	case TMU_STATUS_NORMAL:
		/* 1. change state: 1st-throttling */
		if (cur_temp >= data->ts.start_1st_throttle) {
			info->tmu_state = TMU_STATUS_THROTTLED;
			pr_info("change state: normal->throttle.\n");
		/* 2. polling end and uevent */
		} else if ((cur_temp <= data->ts.stop_1st_throttle)
			&& (cur_temp <= data->ts.stop_mem_throttle)) {
			if (check_handle & THROTTLE_FLAG) {
				exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
				check_handle &= ~(THROTTLE_FLAG);
			}
			pr_debug("check_handle = %d\n", check_handle);
			notify_change_of_tmu_state(info);
			pr_info("normal: free cpufreq_limit & interrupt enable.\n");

			/* clear to prevent from interfupt by peindig bit */
			__raw_writel(INTCLEARALL,
				info->tmu_base + EXYNOS4_TMU_INTCLEAR);
			enable_irq(info->irq);
			mutex_unlock(&tmu_lock);
			return;
		}
		break;

	case TMU_STATUS_THROTTLED:
		/* 1. change state: 2nd-throttling or warning */
		if (cur_temp >= data->ts.start_2nd_throttle) {
			info->tmu_state = TMU_STATUS_WARNING;
			pr_info("change state: 1st throttle->2nd throttle.\n");
		/* 2. cpufreq limitation and uevent */
		} else if ((cur_temp >= data->ts.start_1st_throttle) &&
			!(check_handle & THROTTLE_FLAG)) {
			if (check_handle & WARNING_FLAG) {
				exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
				check_handle &= ~(WARNING_FLAG);
			}
			exynos_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
					info->cpufreq_level_1st_throttle);
			check_handle |= THROTTLE_FLAG;
			pr_debug("check_handle = %d\n", check_handle);
			notify_change_of_tmu_state(info);
			pr_info("throttling: set cpufreq upper limit.\n");
		/* 3. change state: normal */
		} else if ((cur_temp <= data->ts.stop_1st_throttle)
			&& (trend < 0)) {
			info->tmu_state = TMU_STATUS_NORMAL;
			pr_info("change state: 1st throttle->normal.\n");
		}
		break;

	case TMU_STATUS_WARNING:
		/* 1. change state: tripping */
		if (cur_temp >= data->ts.start_tripping) {
			info->tmu_state = TMU_STATUS_TRIPPED;
			pr_info("change state: 2nd throttle->trip\n");
		/* 2. cpufreq limitation and uevent */
		} else if ((cur_temp >= data->ts.start_2nd_throttle) &&
			!(check_handle & WARNING_FLAG)) {
			if (check_handle & THROTTLE_FLAG) {
				exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_TMU);
				check_handle &= ~(THROTTLE_FLAG);
			}
			exynos_cpufreq_upper_limit(DVFS_LOCK_ID_TMU,
					info->cpufreq_level_2nd_throttle);

			check_handle |= WARNING_FLAG;
			pr_debug("check_handle = %d\n", check_handle);
			notify_change_of_tmu_state(info);
			pr_info("2nd throttle: cpufreq is limited.\n");
		/* 3. change state: 1st-throttling */
		} else if ((cur_temp <= data->ts.stop_2nd_throttle)
			&& (trend < 0)) {
			info->tmu_state = TMU_STATUS_THROTTLED;
			pr_info("change state: 2nd throttle->1st throttle, "
				"and release cpufreq upper limit.\n");
		}
		break;

	case TMU_STATUS_TRIPPED:
		/* 1. call uevent to shut-down */
		if ((cur_temp >= data->ts.start_tripping) &&
			(trend > 0) && !(check_handle & TRIPPING_FLAG)) {
			notify_change_of_tmu_state(info);
			pr_info("tripping: on waiting shutdown.\n");
			check_handle |= TRIPPING_FLAG;
			pr_debug("check_handle = %d\n", check_handle);
		/* 2. change state: 2nd-throttling or warning */
		} else if ((cur_temp <= data->ts.stop_2nd_throttle)
				&& (trend < 0)) {
			info->tmu_state = TMU_STATUS_WARNING;
			pr_info("change state: trip->2nd throttle, "
				"Check! occured only test mode.\n");
		}
		/* 3. chip protection: kernel panic as SW workaround */
		if ((cur_temp >= data->ts.start_emergency) && (trend > 0)) {
			panic("Emergency!!!! tripping is not treated!\n");
			/* clear to prevent from interfupt by peindig bit */
			__raw_writel(INTCLEARALL,
				info->tmu_state + EXYNOS4_TMU_INTCLEAR);
			enable_irq(info->irq);
			mutex_unlock(&tmu_lock);
			return;
		}
		break;

	case TMU_STATUS_INIT:
		/* sned tmu initial status to platform */
		disable_irq(info->irq);
		if (cur_temp >= data->ts.start_tripping)
			info->tmu_state = TMU_STATUS_TRIPPED;
		else if (cur_temp >= data->ts.start_2nd_throttle)
			info->tmu_state = TMU_STATUS_WARNING;
		else if (cur_temp >= data->ts.start_1st_throttle)
			info->tmu_state = TMU_STATUS_THROTTLED;
		else if (cur_temp <= data->ts.stop_1st_throttle)
			info->tmu_state = TMU_STATUS_NORMAL;

		notify_change_of_tmu_state(info);
		pr_info("%s: inform to init state to platform.\n", __func__);
		break;

	default:
		pr_warn("Bug: checked tmu_state.\n");
		if (cur_temp >= data->ts.start_tripping)
			info->tmu_state = TMU_STATUS_TRIPPED;
		else
			info->tmu_state = TMU_STATUS_WARNING;
		break;
	} /* end */

	/* memory throttling */
	if (cur_temp >= data->ts.start_mem_throttle) {
		if (!(auto_refresh_changed) && (trend > 0)) {
			pr_info("set auto_refresh 1.95us\n");
			set_refresh_rate(info->auto_refresh_tq0);
			auto_refresh_changed = 1;
		}
	} else if (cur_temp <= (data->ts.stop_mem_throttle)) {
		if ((auto_refresh_changed) && (trend < 0)) {
			pr_info("set auto_refresh 3.9us\n");
			set_refresh_rate(info->auto_refresh_normal);
			auto_refresh_changed = 0;
		}
	}

	info->last_temperature = cur_temp;

	/* reschedule the next work */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
			info->sampling_rate);

	mutex_unlock(&tmu_lock);

	return;
}

static int exynos4210_tmu_init(struct s5p_tmu_info *info)
{
	struct s5p_platform_tmu *data = info->dev->platform_data;
	unsigned int tmp;
	unsigned int temp_code_threshold;
	unsigned int temp_code_throttle, temp_code_warning, temp_code_trip;

	/* To compensate temperature sensor
	 * get trim informatoin and save to struct tmu_info
	 */
	tmp  = __raw_readl(info->tmu_base + EXYNOS4_TMU_TRIMINFO);
	info->te1 = tmp & TMU_TRIMINFO_MASK;
	info->te2 = ((tmp >> 8) & TMU_TRIMINFO_MASK);

	/* check boundary the triminfo */
	if ((EFUSE_MIN_VALUE > info->te1)
		|| (info->te1 > EFUSE_MAX_VALUE) ||  (info->te2 != 0))
		info->te1 = EFUSE_AVG_VALUE;

	pr_info("%s: triminfo = 0x%08x, low 8bit = %d, high 24 bit = %d\n",
			__func__, tmp, info->te1, info->te2);

	/* Need to initial regsiter setting after getting parameter info */
	/* [28:23] vref [11:8] slope - Tunning parameter */
	__raw_writel(VREF_SLOPE, info->tmu_base + EXYNOS4_TMU_CONTROL);

	/* Convert celsius temperature value to temperature code value
	 * such as threshold_level, 1st throttle, 2nd throttle,
	 * tripping temperature.
	*/
	temp_code_threshold = data->ts.stop_1st_throttle
			+ info->te1 - TMU_DC_VALUE;
	temp_code_throttle = data->ts.start_1st_throttle
			- data->ts.stop_1st_throttle;
	temp_code_warning = data->ts.start_2nd_throttle
			- data->ts.stop_1st_throttle;
	temp_code_trip = data->ts.start_tripping
			- data->ts.stop_1st_throttle;

	/* Set interrupt trigger level */
	__raw_writel(temp_code_threshold, info->tmu_base + EXYNOS4210_TMU_THRESHOLD_TEMP);
	__raw_writel(temp_code_throttle, info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL0);
	__raw_writel(temp_code_warning, info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL1);
	__raw_writel(temp_code_trip, info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL2);
	__raw_writel(TRIGGER_LEV_MAX, info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL3);

	pr_info("THD_TEMP:0x%02x:  TRIG_LEV0: 0x%02x\n"
		"TRIG_LEV1: 0x%02x TRIG_LEV2: 0x%02x, TRIG_LEV3: 0x%02x\n",
		__raw_readl(info->tmu_base + EXYNOS4210_TMU_THRESHOLD_TEMP),
		__raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL0),
		__raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL1),
		__raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL2),
		__raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL3));

	mdelay(50);

	/* Need to initial regsiter setting after getting parameter info */
	/* [28:23] vref [11:8] slope - Tunning parameter */
	__raw_writel(VREF_SLOPE, info->tmu_base + EXYNOS4_TMU_CONTROL);
	/* TMU core enable */
	tmp = __raw_readl(info->tmu_base + EXYNOS4_TMU_CONTROL);
	tmp |= TMUCORE_ENABLE;
	__raw_writel(tmp, info->tmu_base + EXYNOS4_TMU_CONTROL);

	/* check interrupt status register */
	pr_debug("tmu interrupt status: 0x%02x\n",
			__raw_readl(info->tmu_base + EXYNOS4_TMU_INTSTAT));

	/* LEV0 LEV1 LEV2 interrupt enable */
	__raw_writel(INTEN0 | INTEN1 | INTEN2, info->tmu_base + EXYNOS4_TMU_INTEN);
	return 0;
}

static int exynos4x12_tmu_init(struct s5p_tmu_info *info)
{
	struct s5p_platform_tmu *data = info->dev->platform_data;
	unsigned int tmp;
	unsigned char temp_code_throttle, temp_code_warning, temp_code_trip;

	/* To compensate temperature sensor,
	 * set triminfo control register & get trim informatoin
	 * and save to struct tmu_info
	*/
	tmp = __raw_readl(info->tmu_base + EXYNOS4x12_TMU_TRIMINFO_CONROL);
	tmp |= TMU_RELOAD;
	__raw_writel(tmp, info->tmu_base + EXYNOS4x12_TMU_TRIMINFO_CONROL);

	mdelay(1);

	tmp  = __raw_readl(info->tmu_base + EXYNOS4_TMU_TRIMINFO);
	info->te1 = tmp & TMU_TRIMINFO_MASK;
#ifdef CONFIG_ARCH_EXYNOS4
	info_ex.te1 = info->te1;
#endif

	/* In case of non e-fusing chip, s/w workaround */
	if (tmp == 0)
		info->te1 = 0x37;

	pr_debug("%s: triminfo reg = 0x%08x, value = %d\n", __func__,
			tmp, info->te1);

	/* Convert celsius temperature value to temperature code value
	 * such as 1st throttle, 2nd throttle, tripping temperature.
	 * its ranges are between 25 cesius(0x32) to 125 cesius4(0x96)
	*/
	temp_code_throttle = data->ts.start_1st_throttle
			+ info->te1 - TMU_DC_VALUE;
	temp_code_warning = data->ts.start_2nd_throttle
			+ info->te1 - TMU_DC_VALUE;
	temp_code_trip = data->ts.start_tripping
			+ info->te1 - TMU_DC_VALUE;

	pr_debug("temp_code_throttle: %d, temp_code_warning: %d\n"
		 "temp_code_trip: %d, info->te1 = %d\n",
		temp_code_throttle, temp_code_warning,
		temp_code_trip, info->te1);

	/* Set interrupt trigger level */
	tmp =  ((0xFF << 24) | (temp_code_trip << 16) |
			(temp_code_warning << 8) | (temp_code_throttle << 0));
	__raw_writel(tmp, info->tmu_base + EXYNOS4x12_TMU_TRESHOLD_TEMP_RISE);

	pr_debug("THD_TEMP_RISE: 0x%08x\n",
		__raw_readl(info->tmu_base + EXYNOS4x12_TMU_TRESHOLD_TEMP_RISE));

	mdelay(50);

	/* TMU core enable */
	tmp = __raw_readl(info->tmu_base + EXYNOS4_TMU_CONTROL);
	tmp |= (TMUCORE_ENABLE | (0x6 << 20)); /* MUX_ADDR : 110b */
	__raw_writel(tmp, info->tmu_base + EXYNOS4_TMU_CONTROL);

	/* check interrupt status register */
	pr_debug("tmu interrupt status: 0x%02x\n",
			__raw_readl(info->tmu_base + EXYNOS4_TMU_INTSTAT));

	/* THRESHOLD_TEMP_RISE0, RISE1, RISE2 interrupt enable */
	__raw_writel(INTEN_RISE0 | INTEN_RISE1 | INTEN_RISE2,
			info->tmu_base + EXYNOS4_TMU_INTEN);
	return 0;
}

static int tmu_initialize(struct platform_device *pdev)
{
	struct s5p_tmu_info *info = platform_get_drvdata(pdev);
	unsigned int tmp;
	unsigned ret;

	/* check if sensing is idle */
	tmp = (__raw_readl(info->tmu_base + EXYNOS4_TMU_STATUS) & 0x1);
	if (!tmp) {
		pr_err("failed to start tmu driver\n");
		return -ENOENT;
	}

	if (soc_is_exynos4210())
		ret = exynos4210_tmu_init(info);
	else
		ret = exynos4x12_tmu_init(info);

	return ret;
}

static irqreturn_t exynos4x12_tmu_irq_handler(int irq, void *id)
{
	struct s5p_tmu_info *info = id;
	unsigned int status;

	disable_irq_nosync(irq);

	status = __raw_readl(info->tmu_base + EXYNOS4_TMU_INTSTAT) & 0x1FF;
	pr_info("EXYNOS4x12_tmu interrupt: INTSTAT = 0x%08x\n", status);

	/* To handle multiple interrupt pending,
	 * interrupt by high temperature are serviced with priority.
	*/
	if (status & INTSTAT_RISE2) {
		info->tmu_state = TMU_STATUS_TRIPPED;
		__raw_writel(INTCLEAR_RISE2, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
	} else if (status & INTSTAT_RISE1) {
		info->tmu_state = TMU_STATUS_WARNING;
		__raw_writel(INTCLEAR_RISE1, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
	} else if (status & INTSTAT_RISE0) {
		info->tmu_state = TMU_STATUS_THROTTLED;
		__raw_writel(INTCLEAR_RISE0, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
	} else {
		pr_err("%s: interrupt error\n", __func__);
		__raw_writel(INTCLEARALL, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
		queue_delayed_work_on(0, tmu_monitor_wq,
			&info->polling, info->sampling_rate / 2);
		return -ENODEV;
	}

	/* read current temperature & save */
	info->last_temperature =  get_curr_temp(info);

	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
		info->sampling_rate);

	return IRQ_HANDLED;
}

static irqreturn_t exynos4210_tmu_irq_handler(int irq, void *id)
{
	struct s5p_tmu_info *info = id;
	unsigned int status;

	disable_irq_nosync(irq);

	status = __raw_readl(info->tmu_base + EXYNOS4_TMU_INTSTAT);
	pr_info("EXYNOS4212_tmu interrupt: INTSTAT = 0x%08x\n", status);

	/* To handle multiple interrupt pending,
	 * interrupt by high temperature are serviced with priority.
	*/
	if (status & TMU_INTSTAT2) {
		info->tmu_state = TMU_STATUS_TRIPPED;
		__raw_writel(INTCLEAR2, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
	} else if (status & TMU_INTSTAT1) {
		info->tmu_state = TMU_STATUS_WARNING;
		__raw_writel(INTCLEAR1, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
	} else if (status & TMU_INTSTAT0) {
		info->tmu_state = TMU_STATUS_THROTTLED;
		__raw_writel(INTCLEAR0, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
	} else {
		pr_err("%s: interrupt error\n", __func__);
		__raw_writel(INTCLEARALL, info->tmu_base + EXYNOS4_TMU_INTCLEAR);
		queue_delayed_work_on(0, tmu_monitor_wq,
			&info->polling, info->sampling_rate / 2);
		return -ENODEV;
	}

	/* read current temperature & save */
	info->last_temperature =  get_curr_temp(info);

	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
		info->sampling_rate);

	return IRQ_HANDLED;
}

#ifdef CONFIG_TMU_SYSFS
static ssize_t s5p_tmu_show_curr_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct s5p_tmu_info *info = dev_get_drvdata(dev);
	unsigned int curr_temp;

	curr_temp = get_curr_temp(info);
	curr_temp *= 10;
	pr_info("curr temp = %d\n", curr_temp);

	return sprintf(buf, "%d\n", curr_temp);
}
static DEVICE_ATTR(curr_temp, S_IRUGO, s5p_tmu_show_curr_temp, NULL);
#endif

static int __devinit s5p_tmu_probe(struct platform_device *pdev)
{
	struct s5p_tmu_info *info;
	struct s5p_platform_tmu *pdata;
	struct resource *res;
	int ret = 0;

	pr_debug("%s: probe=%p\n", __func__, pdev);

	info = kzalloc(sizeof(struct s5p_tmu_info), GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "failed to alloc memory!\n");
		ret = -ENOMEM;
		goto err_nomem;
	}
	platform_set_drvdata(pdev, info);

	info->dev = &pdev->dev;
	info->tmu_state = TMU_STATUS_INIT;

	/* set cpufreq limit level at 1st_throttle & 2nd throttle */
	pdata = info->dev->platform_data;
	if (pdata->cpufreq.limit_1st_throttle)
		exynos_cpufreq_get_level(pdata->cpufreq.limit_1st_throttle,
				&info->cpufreq_level_1st_throttle);

	if (pdata->cpufreq.limit_2nd_throttle)
		exynos_cpufreq_get_level(pdata->cpufreq.limit_2nd_throttle,
				&info->cpufreq_level_2nd_throttle);

	pr_info("@@@ %s: cpufreq_limit: 1st_throttle: %d, 2nd_throttle = %d\n",
		__func__, info->cpufreq_level_1st_throttle,
		 info->cpufreq_level_2nd_throttle);

	/* Map auto_refresh_rate of normal & tq0 mode */
	info->auto_refresh_tq0 =
		get_refresh_interval(FREQ_IN_PLL, AUTO_REFRESH_PERIOD_TQ0);
	info->auto_refresh_normal =
		get_refresh_interval(FREQ_IN_PLL, AUTO_REFRESH_PERIOD_NORMAL);

	/* To poll current temp, set sampling rate to ONE second sampling */
	info->sampling_rate  = usecs_to_jiffies(1000 * 1000);
	/* 10sec monitroing */
	info->monitor_period = usecs_to_jiffies(10000 * 1000);
	set_temperature_params(info);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get memory region resource\n");
		ret = -ENODEV;
		goto err_nores;
	}

	info->ioarea = request_mem_region(res->start,
			res->end-res->start + 1, pdev->name);
	if (!(info->ioarea)) {
		dev_err(&pdev->dev, "failed to reserve memory region\n");
		ret = -EBUSY;
		goto err_nores;
	}

	info->tmu_base = ioremap(res->start, (res->end - res->start) + 1);
	if (!(info->tmu_base)) {
		dev_err(&pdev->dev, "failed ioremap()\n");
		ret = -ENOMEM;
		goto err_nomap;
	}
#ifdef CONFIG_ARCH_EXYNOS4
	info_ex.tmu_base = info->tmu_base;
#endif
	tmu_monitor_wq = create_freezable_workqueue(dev_name(&pdev->dev));
	if (!tmu_monitor_wq) {
		pr_info("Creation of tmu_monitor_wq failed\n");
		ret = -ENOMEM;
		goto err_wq;
	}

#ifdef CONFIG_TMU_DEBUG
	INIT_DELAYED_WORK_DEFERRABLE(&info->monitor, exynos4_poll_cur_temp);
	queue_delayed_work_on(0, tmu_monitor_wq, &info->monitor,
			info->monitor_period);
#endif

	INIT_DELAYED_WORK_DEFERRABLE(&info->polling, exynos4_handler_tmu_state);

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(&pdev->dev, "no irq for thermal %d\n", info->irq);
		ret = -EINVAL;
		goto err_irq;
	}

	if (soc_is_exynos4210())
		ret = request_irq(info->irq, exynos4210_tmu_irq_handler,
				IRQF_DISABLED,  "s5p-tmu interrupt", info);
	else
		ret = request_irq(info->irq, exynos4x12_tmu_irq_handler,
				IRQF_DISABLED,  "s5p-tmu interrupt", info);

	if (ret) {
		dev_err(&pdev->dev, "request_irq is failed. %d\n", ret);
		goto err_irq;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_temperature);
	if (ret != 0) {
		pr_err("Failed to create temperatue file: %d\n", ret);
		goto err_sysfs_file1;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_tmu_state);
	if (ret != 0) {
		pr_err("Failed to create tmu_state file: %d\n", ret);
		goto err_sysfs_file2;
	}
	ret = device_create_file(&pdev->dev, &dev_attr_lot_id);
	if (ret != 0) {
		pr_err("Failed to create lot id file: %d\n", ret);
		goto err_sysfs_file3;
	}

	ret = tmu_initialize(pdev);
	if (ret)
		goto err_init;

#ifdef CONFIG_TMU_SYSFS
	ret = device_create_file(&pdev->dev, &dev_attr_curr_temp);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to create sysfs group\n");
		goto err_init;
	}
#endif

#ifdef CONFIG_TMU_DEBUG
	ret = device_create_file(&pdev->dev, &dev_attr_print_state);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create tmu sysfs group\n\n");
		return ret;
	}
#endif

	/* initialize tmu_state */
	queue_delayed_work_on(0, tmu_monitor_wq, &info->polling,
		info->sampling_rate);

	return ret;

err_init:
	device_remove_file(&pdev->dev, &dev_attr_lot_id);

err_sysfs_file3:
	device_remove_file(&pdev->dev, &dev_attr_tmu_state);

err_sysfs_file2:
	device_remove_file(&pdev->dev, &dev_attr_temperature);

err_sysfs_file1:
	if (info->irq >= 0)
		free_irq(info->irq, info);

err_irq:
	destroy_workqueue(tmu_monitor_wq);

err_wq:
	iounmap(info->tmu_base);

err_nomap:
	release_resource(info->ioarea);
	kfree(info->ioarea);

err_nores:
	kfree(info);
	info = NULL;

err_nomem:
	dev_err(&pdev->dev, "initialization failed.\n");

	return ret;
}

static int __devinit s5p_tmu_remove(struct platform_device *pdev)
{
	struct s5p_tmu_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work(&info->polling);
	destroy_workqueue(tmu_monitor_wq);

	device_remove_file(&pdev->dev, &dev_attr_temperature);
	device_remove_file(&pdev->dev, &dev_attr_tmu_state);

	if (info->irq >= 0)
		free_irq(info->irq, info);

	iounmap(info->tmu_base);

	release_resource(info->ioarea);
	kfree(info->ioarea);

	kfree(info);
	info = NULL;

	pr_info("%s is removed\n", dev_name(&pdev->dev));
	return 0;
}

#ifdef CONFIG_PM
static int s5p_tmu_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct s5p_tmu_info *info = platform_get_drvdata(pdev);

	/* save register value */
	info->reg_save[0] = __raw_readl(info->tmu_base + EXYNOS4_TMU_CONTROL);
	info->reg_save[1] = __raw_readl(info->tmu_base + EXYNOS4_TMU_SAMPLING_INTERNAL);
	info->reg_save[2] = __raw_readl(info->tmu_base + EXYNOS4_TMU_COUNTER_VALUE0);
	info->reg_save[3] = __raw_readl(info->tmu_base + EXYNOS4_TMU_COUNTER_VALUE1);
	info->reg_save[4] = __raw_readl(info->tmu_base + EXYNOS4_TMU_INTEN);

	if (soc_is_exynos4210()) {
		info->reg_save[5] =
			__raw_readl(info->tmu_base + EXYNOS4210_TMU_THRESHOLD_TEMP);
		info->reg_save[6] =
			 __raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL0);
		info->reg_save[7] =
			 __raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL1);
		info->reg_save[8] =
			 __raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL2);
		info->reg_save[9] =
			 __raw_readl(info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL3);
	} else {
		info->reg_save[5] =
			__raw_readl(info->tmu_base + EXYNOS4x12_TMU_TRESHOLD_TEMP_RISE);
	}
	disable_irq(info->irq);

	return 0;
}

static int s5p_tmu_resume(struct platform_device *pdev)
{
	struct s5p_tmu_info *info = platform_get_drvdata(pdev);

	/* restore tmu register value */
	__raw_writel(info->reg_save[0], info->tmu_base + EXYNOS4_TMU_CONTROL);
	__raw_writel(info->reg_save[1],
			info->tmu_base + EXYNOS4_TMU_SAMPLING_INTERNAL);
	__raw_writel(info->reg_save[2],
			info->tmu_base + EXYNOS4_TMU_COUNTER_VALUE0);
	__raw_writel(info->reg_save[3],
			info->tmu_base + EXYNOS4_TMU_COUNTER_VALUE1);
	__raw_writel(info->reg_save[4],
			info->tmu_base + EXYNOS4_TMU_INTEN);

	if (soc_is_exynos4210()) {
		__raw_writel(info->reg_save[5],
			info->tmu_base + EXYNOS4210_TMU_THRESHOLD_TEMP);
		__raw_writel(info->reg_save[6],
			info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL0);
		__raw_writel(info->reg_save[7],
			info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL1);
		__raw_writel(info->reg_save[8],
			info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL2);
		__raw_writel(info->reg_save[9],
			info->tmu_base + EXYNOS4210_TMU_TRIG_LEVEL3);
	} else
		__raw_writel(info->reg_save[5],
			info->tmu_base + EXYNOS4x12_TMU_TRESHOLD_TEMP_RISE);

	enable_irq(info->irq);

	return 0;
}
#else
#define s5p_tmu_suspend	NULL
#define s5p_tmu_resume	NULL
#endif

static struct platform_driver s5p_tmu_driver = {
	.probe		= s5p_tmu_probe,
	.remove		= s5p_tmu_remove,
	.suspend	= s5p_tmu_suspend,
	.resume		= s5p_tmu_resume,
	.driver		= {
		.name   = "s5p-tmu",
		.owner  = THIS_MODULE,
	},
};

static int __init s5p_tmu_driver_init(void)
{
	return platform_driver_register(&s5p_tmu_driver);
}

static void __exit s5p_tmu_driver_exit(void)
{
	platform_driver_unregister(&s5p_tmu_driver);
}
late_initcall(s5p_tmu_driver_init);
module_exit(s5p_tmu_driver_exit);
