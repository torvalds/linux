/*
 * exynos_thermal.c - Samsung EXYNOS TMU (Thermal Management Unit)
 *
 *  Copyright (C) 2011 Samsung Electronics
 *  Donggeun Kim <dg77.kim@samsung.com>
 *  Amit Daniel Kachhap <amit.kachhap@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/platform_data/exynos_thermal.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#include <plat/cpu.h>

#include <mach/tmu.h>

/*Exynos generic registers*/
#define EXYNOS_TMU_REG_TRIMINFO		0x0
#define EXYNOS_TMU_REG_CONTROL		0x20
#define EXYNOS_TMU_REG_STATUS		0x28
#define EXYNOS_TMU_REG_CURRENT_TEMP	0x40
#define EXYNOS_TMU_REG_INTEN		0x70
#define EXYNOS_TMU_REG_INTSTAT		0x74
#define EXYNOS_TMU_REG_INTCLEAR		0x78

#define EXYNOS_TMU_TRIM_TEMP_MASK	0xff
#define EXYNOS_TMU_GAIN_SHIFT		8
#define EXYNOS_TMU_REF_VOLTAGE_SHIFT	24
#define EXYNOS_TMU_CORE_ON		3
#define EXYNOS_TMU_CORE_OFF		2
#define EXYNOS_TMU_DEF_CODE_TO_TEMP_OFFSET	50
#define EXYNOS_THERM_TRIP_EN		(1 << 12)

/*Exynos4 specific registers*/
#define EXYNOS4_TMU_REG_THRESHOLD_TEMP	0x44
#define EXYNOS4_TMU_REG_TRIG_LEVEL0	0x50
#define EXYNOS4_TMU_REG_TRIG_LEVEL1	0x54
#define EXYNOS4_TMU_REG_TRIG_LEVEL2	0x58
#define EXYNOS4_TMU_REG_TRIG_LEVEL3	0x5C
#define EXYNOS4_TMU_REG_PAST_TEMP0	0x60
#define EXYNOS4_TMU_REG_PAST_TEMP1	0x64
#define EXYNOS4_TMU_REG_PAST_TEMP2	0x68
#define EXYNOS4_TMU_REG_PAST_TEMP3	0x6C

#define EXYNOS4_TMU_TRIG_LEVEL0_MASK	0x1
#define EXYNOS4_TMU_TRIG_LEVEL1_MASK	0x10
#define EXYNOS4_TMU_TRIG_LEVEL2_MASK	0x100
#define EXYNOS4_TMU_TRIG_LEVEL3_MASK	0x1000
#define EXYNOS4_TMU_INTCLEAR_VAL	0x1111

/*Exynos5 specific registers*/
#define EXYNOS5_TMU_TRIMINFO_CON	0x14
#define EXYNOS5_THD_TEMP_RISE		0x50
#define EXYNOS5_THD_TEMP_FALL		0x54
#define EXYNOS5_EMUL_CON		0x80

#define EXYNOS5_TRIMINFO_RELOAD		0x1
#define EXYNOS5_TMU_CLEAR_RISE_INT	0x111
#define EXYNOS5_TMU_CLEAR_FALL_INT	(0x111 << 16)
#define EXYNOS5_MUX_ADDR_VALUE		6
#define EXYNOS5_MUX_ADDR_SHIFT		20
#define EXYNOS5_TMU_TRIP_MODE_SHIFT	13

#define EFUSE_MIN_VALUE 40
#define EFUSE_MAX_VALUE 100

/*In-kernel thermal framework related macros & definations*/
#define SENSOR_NAME_LEN	16
#define MAX_TRIP_COUNT	8
#define MAX_COOLING_DEVICE 4

#define PASSIVE_INTERVAL 100
#define ACTIVE_INTERVAL 300
#define IDLE_INTERVAL 1000
#define MCELSIUS	1000

/* CPU Zone information */
#define PANIC_ZONE      4
#define WARN_ZONE       3
#define MONITOR_ZONE    2
#define SAFE_ZONE       1

#define GET_ZONE(trip) (trip + 2)
#define GET_TRIP(zone) (zone - 2)

#define EXYNOS_ZONE_COUNT	3
#define EXYNOS_TMU_COUNT	4

#define COLD_TEMP		20
#define HOT_NORMAL_TEMP		95
#define HOT_CRITICAL_TEMP	110

static int old_cold;
static int old_hot;

static BLOCKING_NOTIFIER_HEAD(exynos_tmu_notifier);

struct exynos_tmu_data {
	struct exynos_tmu_platform_data *pdata;
	struct resource *mem[EXYNOS_TMU_COUNT];
	void __iomem *base[EXYNOS_TMU_COUNT];
	int id[EXYNOS_TMU_COUNT];
	int irq[EXYNOS_TMU_COUNT];
	enum soc_type soc;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk;
	u8 temp_error1[EXYNOS_TMU_COUNT];
	u8 temp_error2[EXYNOS_TMU_COUNT];
};

struct	thermal_trip_point_conf {
	int trip_val[MAX_TRIP_COUNT];
	int boost_trip_val[MAX_TRIP_COUNT];
	int trip_count;
};

struct	thermal_cooling_conf {
	struct freq_clip_table freq_data[MAX_TRIP_COUNT];
	int size[THERMAL_TRIP_CRITICAL + 1];
	int freq_clip_count;
	int boost_temps[MAX_TRIP_COUNT];
	int boost_mode;
};

struct thermal_sensor_conf {
	char name[SENSOR_NAME_LEN];
	int (*read_temperature)(void *data);
	struct thermal_trip_point_conf trip_data;
	struct thermal_cooling_conf cooling_data;
	void *private_data;
};

struct exynos_thermal_zone {
	enum thermal_device_mode mode;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev[MAX_COOLING_DEVICE];
	unsigned int cool_dev_size;
	struct platform_device *exynos4_dev;
	struct thermal_sensor_conf *sensor_conf;
};

static struct exynos_thermal_zone *th_zone;
static void exynos_unregister_thermal(void);
static int exynos_register_thermal(struct thermal_sensor_conf *sensor_conf);
static void exynos_tmu_control(struct platform_device *pdev, int id, bool on);

/* Get mode callback functions for thermal zone */
static int exynos_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	if (th_zone)
		*mode = th_zone->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int exynos_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	bool on;
	int i;

	if (!th_zone->therm_dev) {
		pr_notice("thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&th_zone->therm_dev->lock);

	if (mode == THERMAL_DEVICE_ENABLED) {
		th_zone->therm_dev->polling_delay = IDLE_INTERVAL;
		on = true;
	} else {
		th_zone->therm_dev->polling_delay = 0;
		on = false;
	}

	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		exynos_tmu_control(th_zone->exynos4_dev, i, on);

	mutex_unlock(&th_zone->therm_dev->lock);

	th_zone->mode = mode;
	thermal_zone_device_update(th_zone->therm_dev);
	pr_info("thermal polling set for duration=%d msec\n",
				th_zone->therm_dev->polling_delay);
	return 0;
}

/*
 * This function may be called from interrupt based temperature sensor
 * when threshold is changed.
 */
static void exynos_report_trigger(void)
{
	unsigned int i;
	char data[10];
	char *envp[] = { data, NULL };

	if (!th_zone || !th_zone->therm_dev)
		return;

	thermal_zone_device_update(th_zone->therm_dev);

	mutex_lock(&th_zone->therm_dev->lock);
	/* Find the level for which trip happened */
	for (i = 0; i < th_zone->sensor_conf->trip_data.trip_count; i++) {
		if (th_zone->therm_dev->last_temperature <
			th_zone->sensor_conf->trip_data.trip_val[i] * MCELSIUS)
			break;
	}

	if (th_zone->mode == THERMAL_DEVICE_ENABLED) {
		if (i > 0)
			th_zone->therm_dev->polling_delay = ACTIVE_INTERVAL;
		else
			th_zone->therm_dev->polling_delay = IDLE_INTERVAL;
	}

	snprintf(data, sizeof(data), "%u", i);
	kobject_uevent_env(&th_zone->therm_dev->device.kobj, KOBJ_CHANGE, envp);
	mutex_unlock(&th_zone->therm_dev->lock);
}

/* Get trip type callback functions for thermal zone */
static int exynos_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	switch (GET_ZONE(trip)) {
	case MONITOR_ZONE:
		*type = THERMAL_TRIP_ACTIVE;
		break;
	case WARN_ZONE:
		*type = THERMAL_TRIP_PASSIVE;
		break;
	case PANIC_ZONE:
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int exynos_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long *temp)
{
	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	*temp = th_zone->sensor_conf->trip_data.trip_val[trip];
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

static int temp_to_code(struct exynos_tmu_data *data, u8 temp, int id);

static int exynos_set_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long temp)
{
	struct exynos_tmu_platform_data *pdata;
	struct exynos_tmu_data *data;
	unsigned int interrupt_en = 0, rising_threshold = 0;
	int threshold_code, i;
	int boost_mode = th_zone->sensor_conf->cooling_data.boost_mode;

	if (trip < GET_TRIP(MONITOR_ZONE) || trip > GET_TRIP(PANIC_ZONE))
		return -EINVAL;

	if (boost_mode) {
		pr_info("Boost mode now, cannot change trigger level");
		return 0;
	}

	data = th_zone->sensor_conf->private_data;
	pdata = data->pdata;

	clk_enable(data->clk);

	th_zone->sensor_conf->trip_data.trip_val[trip] = temp;

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		rising_threshold = readl(data->base[i] + EXYNOS5_THD_TEMP_RISE);

		threshold_code = temp_to_code(data, temp, i);

		switch (trip) {
		case 0:
			rising_threshold &= ~0xff;
			rising_threshold |= threshold_code;
			interrupt_en |= pdata->trigger_level0_en;
			break;
		case 1:
			rising_threshold &= ~(0xff << 8);
			rising_threshold |= (threshold_code << 8);
			interrupt_en |= pdata->trigger_level1_en << 4;
			break;
		case 2:
			rising_threshold &= ~(0xff << 16);
			rising_threshold |= (threshold_code << 16);
			interrupt_en |= pdata->trigger_level2_en << 8;
			break;
		default:
			return -EINVAL;
		}

		/* Save same trip info in all tmu sensors */
		writel(rising_threshold, data->base[i] + EXYNOS5_THD_TEMP_RISE);
		writel(interrupt_en, data->base[i] + EXYNOS_TMU_REG_INTEN);
	}

	clk_disable(data->clk);

	printk(KERN_INFO "sysfs : set_trip_temp temp=%d regval=0x%x id=%d\n",
			th_zone->sensor_conf->trip_data.trip_val[trip],
			rising_threshold, trip);

	return 0;
}

static int exynos_get_trip_temp_level(struct thermal_zone_device *thermal, int trip,
				unsigned long *temp)
{
	return 0;
}

static int exynos_set_trip_temp_level(struct thermal_zone_device *thermal,
				unsigned int temp0, unsigned int temp1,
				unsigned int temp2)
{
	int boost_mode = th_zone->sensor_conf->cooling_data.boost_mode;

	if (boost_mode) {
		pr_info("Boost mode now, cannot change cooling temperature level");
		return 0;
	}

	printk(KERN_INFO "Set temp_level\n");
	th_zone->sensor_conf->cooling_data.freq_data[0].temp_level = temp0;
	th_zone->sensor_conf->cooling_data.freq_data[1].temp_level = temp1;
	th_zone->sensor_conf->cooling_data.freq_data[2].temp_level = temp2;

	printk(KERN_INFO "temp %d %d %d\n", temp0, temp1, temp2);
	return 0;
}

static int exynos_get_trip_freq(struct thermal_zone_device *thermal, int trip,
				unsigned long *freq)
{
	return 0;
}

static int exynos_set_trip_freq(struct thermal_zone_device *thermal,
				unsigned int clip_freq0, unsigned int clip_freq1,
				unsigned int clip_freq2)
{
	int boost_mode = th_zone->sensor_conf->cooling_data.boost_mode;

	if (boost_mode) {
		pr_info("Boost mode now, cannot change cliping max frequency");
		return 0;
	}

	printk(KERN_INFO "Set freq\n");
	th_zone->sensor_conf->cooling_data.freq_data[0].freq_clip_max = clip_freq0;
	th_zone->sensor_conf->cooling_data.freq_data[1].freq_clip_max = clip_freq1;
	th_zone->sensor_conf->cooling_data.freq_data[2].freq_clip_max = clip_freq2;

	printk(KERN_INFO "freq %d %d %d\n", clip_freq0, clip_freq1, clip_freq2);

	return 0;
}

static int exynos_get_boost_mode(struct thermal_zone_device *thermal)
{
	return th_zone->sensor_conf->cooling_data.boost_mode;
}

static int exynos_set_boost_mode(struct thermal_zone_device *thermal, unsigned int boost_mode)
{
	int i;
	int boost_temp;
	int trip_count, clip_count;

	if (boost_mode == th_zone->sensor_conf->cooling_data.boost_mode)
		return 0;

	/* boost_mode off : clear boost_mode the before updating trip_point and freq_data */
	if (!boost_mode)
		th_zone->sensor_conf->cooling_data.boost_mode = boost_mode;

	trip_count = th_zone->sensor_conf->trip_data.trip_count;
	clip_count = th_zone->sensor_conf->cooling_data.freq_clip_count;

	/* update trip point */
	for (i = 0; i < trip_count; i++) {
		/* switch the tripping value, boost_trip_val <-> trip_val */
		boost_temp = th_zone->sensor_conf->trip_data.boost_trip_val[i];

		if (boost_temp) {
			th_zone->sensor_conf->trip_data.boost_trip_val[i] =
				th_zone->sensor_conf->trip_data.trip_val[i];

			exynos_set_trip_temp(thermal, i, boost_temp);
		}
	}

	/* update freq_data */
	for (i = 0; i < clip_count; i++) {
		/* switch the temperature, boost_temp <-> temp_level */
		boost_temp = th_zone->sensor_conf->cooling_data.boost_temps[i];

		if (boost_temp) {
			th_zone->sensor_conf->cooling_data.boost_temps[i] =
				th_zone->sensor_conf->cooling_data.freq_data[i].temp_level;

			th_zone->sensor_conf->cooling_data.freq_data[i].temp_level = boost_temp;
		}
	}

	/* boost_mode on : set boost_mode the after updating trip_point and freq_data */
	if (boost_mode)
		th_zone->sensor_conf->cooling_data.boost_mode = boost_mode;

	return 0;
}

/* Get critical temperature callback functions for thermal zone */
static int exynos_get_crit_temp(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	int ret;
	/* Panic zone */
	ret = exynos_get_trip_temp(thermal, GET_TRIP(PANIC_ZONE), temp);
	return ret;
}

/* Bind callback functions for thermal zone */
static int exynos_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/*No matching cooling device*/
	if (i == th_zone->cool_dev_size)
		return 0;

	switch (GET_ZONE(i)) {
	case MONITOR_ZONE:
	case WARN_ZONE:
	case PANIC_ZONE:
		if (thermal_zone_bind_cooling_device(thermal, i, cdev)) {
			pr_err("error binding cooling dev inst 0\n");
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int exynos_unbind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/*No matching cooling device*/
	if (i == th_zone->cool_dev_size)
		return 0;

	switch (GET_ZONE(i)) {
	case MONITOR_ZONE:
	case WARN_ZONE:
	case PANIC_ZONE:
		if (thermal_zone_unbind_cooling_device(thermal, i, cdev)) {
			pr_err("error unbinding cooling dev\n");
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

/* Get temperature callback functions for thermal zone */
static int exynos_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
	void *data;

	if (!th_zone->sensor_conf) {
		pr_info("Temperature sensor not initialised\n");
		return -EINVAL;
	}
	data = th_zone->sensor_conf->private_data;
	*temp = th_zone->sensor_conf->read_temperature(data);
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops const exynos_dev_ops = {
	.bind = exynos_bind,
	.unbind = exynos_unbind,
	.get_temp = exynos_get_temp,
	.get_mode = exynos_get_mode,
	.set_mode = exynos_set_mode,
	.get_trip_type = exynos_get_trip_type,
	.get_trip_temp = exynos_get_trip_temp,
	.set_trip_temp = exynos_set_trip_temp,
	.get_trip_temp_level = exynos_get_trip_temp_level,
	.set_trip_temp_level = exynos_set_trip_temp_level,
	.get_trip_freq = exynos_get_trip_freq,
	.set_trip_freq = exynos_set_trip_freq,
	.get_boost_mode = exynos_get_boost_mode,
	.set_boost_mode = exynos_set_boost_mode,
	.get_crit_temp = exynos_get_crit_temp,
};

/* Register with the in-kernel thermal management */
static int exynos_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret, count, tab_size, pos = 0;
	struct freq_clip_table *tab_ptr, *clip_data;

	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone = kzalloc(sizeof(struct exynos_thermal_zone), GFP_KERNEL);
	if (!th_zone)
		return -ENOMEM;

	th_zone->sensor_conf = sensor_conf;

	tab_ptr = (struct freq_clip_table *)sensor_conf->cooling_data.freq_data;

	/* Register the cpufreq cooling device */
	for (count = 0; count < EXYNOS_ZONE_COUNT; count++) {
		tab_size = sensor_conf->cooling_data.size[count];

		clip_data = (struct freq_clip_table *)&(tab_ptr[pos]);

		th_zone->cool_dev[count] = cpufreq_cooling_register(
						clip_data, tab_size);
		pos += tab_size;

		if (IS_ERR(th_zone->cool_dev[count])) {
			pr_err("Failed to register cpufreq cooling device\n");
			ret = -EINVAL;
			th_zone->cool_dev_size = count;
			goto err_unregister;
		}
	}
	th_zone->cool_dev_size = count;

	th_zone->therm_dev = thermal_zone_device_register(sensor_conf->name,
			EXYNOS_ZONE_COUNT, 7, NULL, &exynos_dev_ops, 1, 1, PASSIVE_INTERVAL,
			IDLE_INTERVAL);

	if (IS_ERR(th_zone->therm_dev)) {
		pr_err("Failed to register thermal zone device\n");
		ret = -EINVAL;
		goto err_unregister;
	}
	th_zone->mode = THERMAL_DEVICE_ENABLED;

	pr_info("Exynos: Kernel Thermal management registered\n");

	return 0;

err_unregister:
	exynos_unregister_thermal();
	return ret;
}

/* Un-Register with the in-kernel thermal management */
static void exynos_unregister_thermal(void)
{
	int i;

	for (i = 0; i < th_zone->cool_dev_size; i++) {
		if (th_zone && th_zone->cool_dev[i])
			cpufreq_cooling_unregister(th_zone->cool_dev[i]);
	}

	if (th_zone && th_zone->therm_dev)
		thermal_zone_device_unregister(th_zone->therm_dev);

	kfree(th_zone);

	pr_info("Exynos: Kernel Thermal management unregistered\n");
}

int exynos_tmu_add_notifier(struct notifier_block *n)
{
	return blocking_notifier_chain_register(&exynos_tmu_notifier, n);
}

void exynos_tmu_call_notifier(int cold, enum tmu_noti_state_t hot)
{
	if (cold) {
		pr_info("cold state: %d to %d \n", old_cold, cold);
		blocking_notifier_call_chain(&exynos_tmu_notifier, TMU_COLD, &cold);
		old_cold = cold;
	}

	if (hot) {
		pr_info("hot state %d to %d \n", old_hot, hot);
		blocking_notifier_call_chain(&exynos_tmu_notifier, hot, &hot);
		old_hot = hot;
	}
}

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos_tmu_data *data, u8 temp, int id)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp_code;

	if (data->soc == SOC_ARCH_EXYNOS4)
		/* temp should range between 25 and 125 */
		if (temp < 25 || temp > 125) {
			temp_code = -EINVAL;
			goto out;
		}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp_code = (temp - 25) *
		    (data->temp_error2[id] - data->temp_error1[id]) /
		    (85 - 25) + data->temp_error1[id];
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp_code = temp + data->temp_error1[id] - 25;
		break;
	default:
		temp_code = temp + EXYNOS_TMU_DEF_CODE_TO_TEMP_OFFSET;
		break;
	}
out:
	return temp_code;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 */
static int code_to_temp(struct exynos_tmu_data *data, u8 temp_code, int id)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int min, max, temp;

	if (data->soc == SOC_ARCH_EXYNOS4) {
		min = 75;
		max = 175;
	} else {
		min = 46;
		max = 146;
	}

	/* temp_code should range between min and max */
	if (temp_code < min || temp_code > max) {
		temp = -ENODATA;
		goto out;
	}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp = (temp_code - data->temp_error1[id]) * (85 - 25) /
		    (data->temp_error2[id] - data->temp_error1[id]) + 25;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp = temp_code - data->temp_error1[id] + 25;
		break;
	default:
		temp = temp_code - EXYNOS_TMU_DEF_CODE_TO_TEMP_OFFSET;
		break;
	}
out:
	return temp;
}

static int exynos_tmu_initialize(struct platform_device *pdev, int id)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int status, trim_info, rising_threshold;
	int ret = 0, threshold_code;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	status = readb(data->base[id] + EXYNOS_TMU_REG_STATUS);
	if (!status) {
		ret = -EBUSY;
		goto out;
	}

	if (data->soc == SOC_ARCH_EXYNOS5) {
		__raw_writel(EXYNOS5_TRIMINFO_RELOAD,
				data->base[id] + EXYNOS5_TMU_TRIMINFO_CON);
	}
	/* Save trimming info in order to perform calibration */
	trim_info = readl(data->base[id] + EXYNOS_TMU_REG_TRIMINFO);
	data->temp_error1[id] = trim_info & EXYNOS_TMU_TRIM_TEMP_MASK;
	data->temp_error2[id] = ((trim_info >> 8) & EXYNOS_TMU_TRIM_TEMP_MASK);

	if ((EFUSE_MIN_VALUE > data->temp_error1[id]) ||
			(data->temp_error1[id] > EFUSE_MAX_VALUE) ||
			(data->temp_error2[id] != 0))
		data->temp_error1[id] = pdata->efuse_value;

	if (data->soc == SOC_ARCH_EXYNOS4) {
		/* Write temperature code for threshold */
		threshold_code = temp_to_code(data, pdata->threshold, id);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		writeb(threshold_code,
			data->base[id] + EXYNOS4_TMU_REG_THRESHOLD_TEMP);

		writeb(pdata->trigger_levels[0],
			data->base[id] + EXYNOS4_TMU_REG_TRIG_LEVEL0);
		writeb(pdata->trigger_levels[1],
			data->base[id] + EXYNOS4_TMU_REG_TRIG_LEVEL1);
		writeb(pdata->trigger_levels[2],
			data->base[id] + EXYNOS4_TMU_REG_TRIG_LEVEL2);
		writeb(pdata->trigger_levels[3],
			data->base[id] + EXYNOS4_TMU_REG_TRIG_LEVEL3);

		writel(EXYNOS4_TMU_INTCLEAR_VAL,
			data->base[id] + EXYNOS_TMU_REG_INTCLEAR);
	} else if (data->soc == SOC_ARCH_EXYNOS5) {
		/* Write temperature code for threshold */
		threshold_code = temp_to_code(data, pdata->trigger_levels[0], id);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		rising_threshold = threshold_code;
		threshold_code = temp_to_code(data, pdata->trigger_levels[1], id);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		rising_threshold |= (threshold_code << 8);
		threshold_code = temp_to_code(data, pdata->trigger_levels[2], id);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		rising_threshold |= (threshold_code << 16);
		threshold_code = temp_to_code(data, pdata->trigger_levels[3], id);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		rising_threshold |= (threshold_code << 24);

		writel(rising_threshold,
				data->base[id] + EXYNOS5_THD_TEMP_RISE);
		writel(0, data->base[id] + EXYNOS5_THD_TEMP_FALL);

		writel(EXYNOS5_TMU_CLEAR_RISE_INT|EXYNOS5_TMU_CLEAR_FALL_INT,
				data->base[id] + EXYNOS_TMU_REG_INTCLEAR);
	}
out:
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

static void exynos_tmu_control(struct platform_device *pdev, int id, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int con, interrupt_en;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	con = pdata->reference_voltage << EXYNOS_TMU_REF_VOLTAGE_SHIFT |
		pdata->gain << EXYNOS_TMU_GAIN_SHIFT;

	if (data->soc == SOC_ARCH_EXYNOS5) {
		con |= pdata->noise_cancel_mode << EXYNOS5_TMU_TRIP_MODE_SHIFT;
		con |= (EXYNOS5_MUX_ADDR_VALUE << EXYNOS5_MUX_ADDR_SHIFT);
	}

	if (on) {
		con |= (EXYNOS_TMU_CORE_ON | EXYNOS_THERM_TRIP_EN);
		interrupt_en =
			pdata->trigger_level2_en << 8 |
			pdata->trigger_level1_en << 4 |
			pdata->trigger_level0_en;
	} else {
		con |= EXYNOS_TMU_CORE_OFF;
		interrupt_en = 0; /* Disable all interrupts */
	}
	writel(interrupt_en, data->base[id] + EXYNOS_TMU_REG_INTEN);
	writel(con, data->base[id] + EXYNOS_TMU_REG_CONTROL);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static int exynos_tmu_read(struct exynos_tmu_data *data)
{
	u8 temp_code;
	int temp, i, max = INT_MIN, min = INT_MAX;
	int cold_event = 0;
	int hot_event = 0;
	enum tmu_noti_state_t cur_state;

	if (!th_zone || !th_zone->therm_dev)
		return -EPERM;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		temp_code = readb(data->base[i] + EXYNOS_TMU_REG_CURRENT_TEMP);
		temp = code_to_temp(data, temp_code, i);
		if (temp < 0)
			continue;

		if (temp > max)
			max = temp;
		if (temp < min)
			min = temp;
	}

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	/* check current tmu state */
	if (min <= COLD_TEMP && !old_cold)
		cold_event = 1;

	if (max >= HOT_CRITICAL_TEMP)
		cur_state = TMU_CRITICAL;
	else if (max > HOT_NORMAL_TEMP && max < HOT_CRITICAL_TEMP)
		cur_state = TMU_HOT;
	else if (max <= HOT_NORMAL_TEMP)
		cur_state = TMU_NORMAL;
	else
		cur_state = TMU_NORMAL;

	if (old_hot != cur_state)
		hot_event = cur_state;

	th_zone->therm_dev->last_temperature = max;

	exynos_tmu_call_notifier(cold_event, hot_event);

	return max;
}

static void exynos_tmu_work(struct work_struct *work)
{
	struct exynos_tmu_data *data = container_of(work,
			struct exynos_tmu_data, irq_work);

	int i;

	mutex_lock(&data->lock);
	clk_enable(data->clk);


	if (data->soc == SOC_ARCH_EXYNOS5) {
		for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
			writel(EXYNOS5_TMU_CLEAR_RISE_INT,
					data->base[i] + EXYNOS_TMU_REG_INTCLEAR);
		}
	} else {
		writel(EXYNOS4_TMU_INTCLEAR_VAL,
				data->base + EXYNOS_TMU_REG_INTCLEAR);
	}

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
	exynos_report_trigger();
	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		enable_irq(data->irq[i]);
}

static irqreturn_t exynos_tmu_irq(int irq, void *id)
{
	struct exynos_tmu_data *data = id;
	int i;

	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		disable_irq_nosync(data->irq[i]);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

static struct thermal_sensor_conf exynos_sensor_conf = {
	.name			= "exynos-therm",
	.read_temperature	= (int (*)(void *))exynos_tmu_read,
};

static int exynos_pm_notifier(struct notifier_block *notifier,
			unsigned long pm_event, void *v)
{
	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		exynos_tmu_call_notifier(1, old_hot);
		break;
	case PM_POST_SUSPEND:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block exynos_pm_nb = {
	.notifier_call = exynos_pm_notifier,
};

#if defined(CONFIG_SOC_EXYNOS4212) || defined(CONFIG_SOC_EXYNOS4412)
static struct exynos_tmu_platform_data const exynos4_default_tmu_data = {
	.threshold = 80,
	.trigger_levels[0] = 5,
	.trigger_levels[1] = 20,
	.trigger_levels[2] = 30,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 0,
	.gain = 15,
	.reference_voltage = 7,
	.cal_type = TYPE_ONE_POINT_TRIMMING,
	.freq_tab[0] = {
		.freq_clip_max = 800 * 1000,
		.temp_level = 85,
	},
	.freq_tab[1] = {
		.freq_clip_max = 200 * 1000,
		.temp_level = 100,
	},
	.freq_tab_count = 2,
	.type = SOC_ARCH_EXYNOS4,
};
#define EXYNOS4_TMU_DRV_DATA (&exynos4_default_tmu_data)
#else
#define EXYNOS4_TMU_DRV_DATA (NULL)
#endif

#if defined(CONFIG_SOC_EXYNOS5410)
static struct exynos_tmu_platform_data const exynos5_default_tmu_data = {
	.trigger_levels[0] = 70,
	.trigger_levels[1] = 75,
	.trigger_levels[2] = 110,
	.trigger_level0_en = 1,
	.trigger_level1_en = 1,
	.trigger_level2_en = 1,
	.trigger_level3_en = 0,
	.gain = 8,
	.reference_voltage = 16,
	.noise_cancel_mode = 4,
	.cal_type = TYPE_ONE_POINT_TRIMMING,
	.efuse_value = 55,
	.freq_tab[0] = {
		.freq_clip_max = 1600 * 1000,
		.temp_level = 70,
	},
	.freq_tab[1] = {
		.freq_clip_max = 1400 * 1000,
		.temp_level = 75,
	},
	.freq_tab[2] = {
		.freq_clip_max = 1200 * 1000,
		.temp_level = 80,
	},
	.freq_tab[3] = {
		.freq_clip_max = 800 * 1000,
		.temp_level = 85,
	},
	.freq_tab[4] = {
		.freq_clip_max = 400 * 1000,
		.temp_level = 100,
	},
	.size[THERMAL_TRIP_ACTIVE] = 1,
	.size[THERMAL_TRIP_PASSIVE] = 3,
	.size[THERMAL_TRIP_HOT] = 1,
	.freq_tab_count = 5,
	.type = SOC_ARCH_EXYNOS5,
};
#define EXYNOS5_TMU_DRV_DATA (&exynos5_default_tmu_data)
#else
#define EXYNOS5_TMU_DRV_DATA (NULL)
#endif

#ifdef CONFIG_OF
static const struct of_device_id exynos_tmu_match[] = {
	{
		.compatible = "samsung,exynos4-tmu",
		.data = (void *)EXYNOS4_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5-tmu",
		.data = (void *)EXYNOS5_TMU_DRV_DATA,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_tmu_match);
#else
#define  exynos_tmu_match NULL
#endif

static struct platform_device_id exynos_tmu_driver_ids[] = {
	{
		.name		= "exynos4-tmu",
		.driver_data    = (kernel_ulong_t)EXYNOS4_TMU_DRV_DATA,
	},
	{
		.name		= "exynos5-tmu",
		.driver_data    = (kernel_ulong_t)EXYNOS5_TMU_DRV_DATA,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, exynos_tmu_driver_ids);

static inline struct  exynos_tmu_platform_data *exynos_get_driver_data(
		struct platform_device *pdev)
{
#ifdef CONFIG_OF
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(exynos_tmu_match, pdev->dev.of_node);
		if (!match)
			return NULL;
		return (struct exynos_tmu_platform_data *) match->data;
	}
#endif
	return (struct exynos_tmu_platform_data *)
		platform_get_device_id(pdev)->driver_data;
}

/* sysfs interface : /sys/devices/platform/exynos5-tmu/temp */
static ssize_t
exynos_thermal_sensor_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct exynos_tmu_data *data = th_zone->sensor_conf->private_data;
	u8 temp_code;
	unsigned long temp[EXYNOS_TMU_COUNT];
	int i = 0;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		temp_code = readb(data->base[i] + EXYNOS_TMU_REG_CURRENT_TEMP);
		if (temp_code == 0xff)
			continue;
		temp[i] = code_to_temp(data, temp_code, i);
	}

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return sprintf(&buf[0], "%ld %ld %ld %ld \n", temp[0],temp[1],temp[2],temp[3]);
}

static DEVICE_ATTR(temp, S_IRWXUGO , exynos_thermal_sensor_temp, NULL);

static struct attribute *exynos_thermal_sensor_attributes[] = {
	&dev_attr_temp.attr,
	NULL
};

static const struct attribute_group exynos_thermal_sensor_attr_group = {
	.attrs = exynos_thermal_sensor_attributes,
};

unsigned long exynos_thermal_get_value(void)
{
	struct exynos_tmu_data *data = th_zone->sensor_conf->private_data;
	u8 temp_code;
	unsigned long temp[EXYNOS_TMU_COUNT], max=0;
	int i = 0;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		temp_code = readb(data->base[i] + EXYNOS_TMU_REG_CURRENT_TEMP);
		if (temp_code == 0xff)
			continue;
		temp[i] = code_to_temp(data, temp_code, i);
		
		if(max < temp[i]) max = temp[i];
	}

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return max;
}
EXPORT_SYMBOL(exynos_thermal_get_value);

static int __devinit exynos_tmu_probe(struct platform_device *pdev)
{
	struct exynos_tmu_data *data;
	struct exynos_tmu_platform_data *pdata = pdev->dev.platform_data;
	int ret, i;

	if (!pdata)
		pdata = exynos_get_driver_data(pdev);

	if (!pdata) {
		dev_err(&pdev->dev, "No platform init data supplied.\n");
		return -ENODEV;
	}
	data = kzalloc(sizeof(struct exynos_tmu_data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	INIT_WORK(&data->irq_work, exynos_tmu_work);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		data->irq[i] = platform_get_irq(pdev, i);
		if (data->irq[i] < 0) {
			ret = data->irq[i];
			dev_err(&pdev->dev, "Failed to get platform irq\n");
			goto err_free;
		}

		data->mem[i] = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!data->mem[i]) {
			ret = -ENOENT;
			dev_err(&pdev->dev, "Failed to get platform resource\n");
			goto err_free;
		}

		data->mem[i] = request_mem_region(data->mem[i]->start,
				resource_size(data->mem[i]), pdev->name);
		if (!data->mem[i]) {
			ret = -ENODEV;
			dev_err(&pdev->dev, "Failed to request memory region\n");
			goto err_free;
		}

		data->base[i] = ioremap(data->mem[i]->start, resource_size(data->mem[i]));
		if (!data->base[i]) {
			ret = -ENODEV;
			dev_err(&pdev->dev, "Failed to ioremap memory\n");
			goto err_mem_region;
		}

		ret = request_irq(data->irq[i], exynos_tmu_irq,
				IRQF_TRIGGER_RISING, "exynos-tmu", data);
		if (ret) {
			dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq[i]);
			goto err_io_remap;
		}
	}

	data->clk = clk_get(NULL, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		ret = PTR_ERR(data->clk);
		dev_err(&pdev->dev, "Failed to get clock\n");
		goto err_irq;
	}

	if (pdata->type == SOC_ARCH_EXYNOS5 ||
				pdata->type == SOC_ARCH_EXYNOS4)
		data->soc = pdata->type;
	else {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Platform not supported\n");
		goto err_clk;
	}

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		ret = exynos_tmu_initialize(pdev, i);
		if (ret) {
			dev_err(&pdev->dev, "Failed to initialize TMU[%d]\n", i);
			goto err_clk;
		}

		exynos_tmu_control(pdev, i, true);
	}

	/*Register the sensor with thermal management interface*/
	(&exynos_sensor_conf)->private_data = data;
	exynos_sensor_conf.trip_data.trip_count = pdata->trigger_level0_en +
			pdata->trigger_level1_en + pdata->trigger_level2_en +
			pdata->trigger_level3_en;

	for (i = 0; i < exynos_sensor_conf.trip_data.trip_count; i++) {
		exynos_sensor_conf.trip_data.trip_val[i] =
			pdata->threshold + pdata->trigger_levels[i];
		exynos_sensor_conf.trip_data.boost_trip_val[i] =
			pdata->threshold + pdata->boost_trigger_levels[i];
	}

	exynos_sensor_conf.cooling_data.freq_clip_count =
						pdata->freq_tab_count;
	exynos_sensor_conf.cooling_data.boost_mode = 0;

	for (i = 0; i < pdata->freq_tab_count; i++) {
		exynos_sensor_conf.cooling_data.freq_data[i].freq_clip_max =
					pdata->freq_tab[i].freq_clip_max;
		exynos_sensor_conf.cooling_data.freq_data[i].temp_level =
					pdata->freq_tab[i].temp_level;
		exynos_sensor_conf.cooling_data.freq_data[i].mask_val = cpu_all_mask;
		exynos_sensor_conf.cooling_data.size[i] =
					pdata->size[i];

		exynos_sensor_conf.cooling_data.boost_temps[i] =
					pdata->boost_temps[i];
	}

	register_pm_notifier(&exynos_pm_nb);

	ret = exynos_register_thermal(&exynos_sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_clk;
	}

	th_zone->exynos4_dev = pdev;

	ret = sysfs_create_group(&pdev->dev.kobj, &exynos_thermal_sensor_attr_group);
	if (ret)
		dev_err(&pdev->dev, "cannot create thermal sensor attributes\n");

	return 0;
err_clk:
	platform_set_drvdata(pdev, NULL);
	clk_put(data->clk);
err_irq:
	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		if (data->irq[i])
			free_irq(data->irq[i], data);
	}
err_io_remap:
	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		if (data->base[i])
			iounmap(data->base[i]);
	}
err_mem_region:
	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		if (data->mem[i])
			release_mem_region(data->mem[i]->start, resource_size(data->mem[i]));
	}
err_free:
	kfree(data);

	return ret;
}

static int __devexit exynos_tmu_remove(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		exynos_tmu_control(pdev, i, false);

	exynos_unregister_thermal();

	clk_put(data->clk);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		free_irq(data->irq[i], data);

	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		iounmap(data->base[i]);
	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		release_mem_region(data->mem[i]->start, resource_size(data->mem[i]));

	platform_set_drvdata(pdev, NULL);

	kfree(data);

	return 0;
}

#ifdef CONFIG_PM
static int exynos_tmu_suspend(struct platform_device *pdev, pm_message_t state)
{
	int i;
	for (i = 0; i < EXYNOS_TMU_COUNT; i++)
		exynos_tmu_control(pdev, i, false);

	return 0;
}

static int exynos_tmu_resume(struct platform_device *pdev)
{
	int i;
	bool on = false;

	if (th_zone->mode == THERMAL_DEVICE_ENABLED)
		on = true;

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		exynos_tmu_initialize(pdev, i);
		exynos_tmu_control(pdev, i, on);
	}

	return 0;
}
#else
#define exynos_tmu_suspend NULL
#define exynos_tmu_resume NULL
#endif

static struct platform_driver exynos_tmu_driver = {
	.driver = {
		.name   = "exynos-tmu",
		.owner  = THIS_MODULE,
		.of_match_table = exynos_tmu_match,
	},
	.probe = exynos_tmu_probe,
	.remove	= __devexit_p(exynos_tmu_remove),
	.suspend = exynos_tmu_suspend,
	.resume = exynos_tmu_resume,
	.id_table = exynos_tmu_driver_ids,
};

module_platform_driver(exynos_tmu_driver);

MODULE_DESCRIPTION("EXYNOS TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos-tmu");
