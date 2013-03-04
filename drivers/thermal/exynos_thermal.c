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
#include <linux/platform_data/exynos_thermal.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>

#include <plat/cpu.h>

/* Exynos generic registers */
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

/* Exynos4210 specific registers */
#define EXYNOS4210_TMU_REG_THRESHOLD_TEMP	0x44
#define EXYNOS4210_TMU_REG_TRIG_LEVEL0	0x50
#define EXYNOS4210_TMU_REG_TRIG_LEVEL1	0x54
#define EXYNOS4210_TMU_REG_TRIG_LEVEL2	0x58
#define EXYNOS4210_TMU_REG_TRIG_LEVEL3	0x5C
#define EXYNOS4210_TMU_REG_PAST_TEMP0	0x60
#define EXYNOS4210_TMU_REG_PAST_TEMP1	0x64
#define EXYNOS4210_TMU_REG_PAST_TEMP2	0x68
#define EXYNOS4210_TMU_REG_PAST_TEMP3	0x6C

#define EXYNOS4210_TMU_TRIG_LEVEL0_MASK	0x1
#define EXYNOS4210_TMU_TRIG_LEVEL1_MASK	0x10
#define EXYNOS4210_TMU_TRIG_LEVEL2_MASK	0x100
#define EXYNOS4210_TMU_TRIG_LEVEL3_MASK	0x1000
#define EXYNOS4210_TMU_INTCLEAR_VAL	0x1111

/* Exynos5250 and Exynos4412 specific registers */
#define EXYNOS_TMU_TRIMINFO_CON	0x14
#define EXYNOS_THD_TEMP_RISE		0x50
#define EXYNOS_THD_TEMP_FALL		0x54
#define EXYNOS_EMUL_CON		0x80

#define EXYNOS_TRIMINFO_RELOAD		0x1
#define EXYNOS_TMU_CLEAR_RISE_INT	0x111
#define EXYNOS_TMU_CLEAR_FALL_INT	(0x111 << 12)
#define EXYNOS_MUX_ADDR_VALUE		6
#define EXYNOS_MUX_ADDR_SHIFT		20
#define EXYNOS_TMU_TRIP_MODE_SHIFT	13

#define EFUSE_MIN_VALUE 40
#define EFUSE_MAX_VALUE 100

/* In-kernel thermal framework related macros & definations */
#define SENSOR_NAME_LEN	16
#define MAX_TRIP_COUNT	8
#define MAX_COOLING_DEVICE 4
#define MAX_THRESHOLD_LEVS 4

#define ACTIVE_INTERVAL 500
#define IDLE_INTERVAL 10000
#define MCELSIUS	1000

#ifdef CONFIG_EXYNOS_THERMAL_EMUL
#define EXYNOS_EMUL_TIME	0x57F0
#define EXYNOS_EMUL_TIME_SHIFT	16
#define EXYNOS_EMUL_DATA_SHIFT	8
#define EXYNOS_EMUL_DATA_MASK	0xFF
#define EXYNOS_EMUL_ENABLE	0x1
#endif /* CONFIG_EXYNOS_THERMAL_EMUL */

/* CPU Zone information */
#define PANIC_ZONE      4
#define WARN_ZONE       3
#define MONITOR_ZONE    2
#define SAFE_ZONE       1

#define GET_ZONE(trip) (trip + 2)
#define GET_TRIP(zone) (zone - 2)

#define EXYNOS_ZONE_COUNT	3

struct exynos_tmu_data {
	struct exynos_tmu_platform_data *pdata;
	struct resource *mem;
	void __iomem *base;
	int irq;
	enum soc_type soc;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk;
	u8 temp_error1, temp_error2;
};

struct	thermal_trip_point_conf {
	int trip_val[MAX_TRIP_COUNT];
	int trip_count;
	u8 trigger_falling;
};

struct	thermal_cooling_conf {
	struct freq_clip_table freq_data[MAX_TRIP_COUNT];
	int freq_clip_count;
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
	bool bind;
};

static struct exynos_thermal_zone *th_zone;
static void exynos_unregister_thermal(void);
static int exynos_register_thermal(struct thermal_sensor_conf *sensor_conf);

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
	if (!th_zone->therm_dev) {
		pr_notice("thermal zone not registered\n");
		return 0;
	}

	mutex_lock(&th_zone->therm_dev->lock);

	if (mode == THERMAL_DEVICE_ENABLED &&
		!th_zone->sensor_conf->trip_data.trigger_falling)
		th_zone->therm_dev->polling_delay = IDLE_INTERVAL;
	else
		th_zone->therm_dev->polling_delay = 0;

	mutex_unlock(&th_zone->therm_dev->lock);

	th_zone->mode = mode;
	thermal_zone_device_update(th_zone->therm_dev);
	pr_info("thermal polling set for duration=%d msec\n",
				th_zone->therm_dev->polling_delay);
	return 0;
}


/* Get trip type callback functions for thermal zone */
static int exynos_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	switch (GET_ZONE(trip)) {
	case MONITOR_ZONE:
	case WARN_ZONE:
		*type = THERMAL_TRIP_ACTIVE;
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

/* Get critical temperature callback functions for thermal zone */
static int exynos_get_crit_temp(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	int ret;
	/* Panic zone */
	ret = exynos_get_trip_temp(thermal, GET_TRIP(PANIC_ZONE), temp);
	return ret;
}

static int exynos_get_frequency_level(unsigned int cpu, unsigned int freq)
{
	int i = 0, ret = -EINVAL;
	struct cpufreq_frequency_table *table = NULL;
#ifdef CONFIG_CPU_FREQ
	table = cpufreq_frequency_get_table(cpu);
#endif
	if (!table)
		return ret;

	while (table[i].frequency != CPUFREQ_TABLE_END) {
		if (table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;
		if (table[i].frequency == freq)
			return i;
		i++;
	}
	return ret;
}

/* Bind callback functions for thermal zone */
static int exynos_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size, level;
	struct freq_clip_table *tab_ptr, *clip_data;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;

	tab_ptr = (struct freq_clip_table *)data->cooling_data.freq_data;
	tab_size = data->cooling_data.freq_clip_count;

	if (tab_ptr == NULL || tab_size == 0)
		return -EINVAL;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		clip_data = (struct freq_clip_table *)&(tab_ptr[i]);
		level = exynos_get_frequency_level(0, clip_data->freq_clip_max);
		if (level < 0)
			return 0;
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_bind_cooling_device(thermal, i, cdev,
								level, 0)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = true;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int exynos_unbind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, tab_size;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;

	if (th_zone->bind == false)
		return 0;

	tab_size = data->cooling_data.freq_clip_count;

	if (tab_size == 0)
		return -EINVAL;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 0; i < tab_size; i++) {
		switch (GET_ZONE(i)) {
		case MONITOR_ZONE:
		case WARN_ZONE:
			if (thermal_zone_unbind_cooling_device(thermal, i,
								cdev)) {
				pr_err("error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = false;
			break;
		default:
			ret = -EINVAL;
		}
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

/* Get the temperature trend */
static int exynos_get_trend(struct thermal_zone_device *thermal,
			int trip, enum thermal_trend *trend)
{
	int ret;
	unsigned long trip_temp;

	ret = exynos_get_trip_temp(thermal, trip, &trip_temp);
	if (ret < 0)
		return ret;

	if (thermal->temperature >= trip_temp)
		*trend = THERMAL_TREND_RAISE_FULL;
	else
		*trend = THERMAL_TREND_DROP_FULL;

	return 0;
}
/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops const exynos_dev_ops = {
	.bind = exynos_bind,
	.unbind = exynos_unbind,
	.get_temp = exynos_get_temp,
	.get_trend = exynos_get_trend,
	.get_mode = exynos_get_mode,
	.set_mode = exynos_set_mode,
	.get_trip_type = exynos_get_trip_type,
	.get_trip_temp = exynos_get_trip_temp,
	.get_crit_temp = exynos_get_crit_temp,
};

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
	if (th_zone->bind == false) {
		for (i = 0; i < th_zone->cool_dev_size; i++) {
			if (!th_zone->cool_dev[i])
				continue;
			exynos_bind(th_zone->therm_dev,
					th_zone->cool_dev[i]);
		}
	}

	thermal_zone_device_update(th_zone->therm_dev);

	mutex_lock(&th_zone->therm_dev->lock);
	/* Find the level for which trip happened */
	for (i = 0; i < th_zone->sensor_conf->trip_data.trip_count; i++) {
		if (th_zone->therm_dev->last_temperature <
			th_zone->sensor_conf->trip_data.trip_val[i] * MCELSIUS)
			break;
	}

	if (th_zone->mode == THERMAL_DEVICE_ENABLED &&
		!th_zone->sensor_conf->trip_data.trigger_falling) {
		if (i > 0)
			th_zone->therm_dev->polling_delay = ACTIVE_INTERVAL;
		else
			th_zone->therm_dev->polling_delay = IDLE_INTERVAL;
	}

	snprintf(data, sizeof(data), "%u", i);
	kobject_uevent_env(&th_zone->therm_dev->device.kobj, KOBJ_CHANGE, envp);
	mutex_unlock(&th_zone->therm_dev->lock);
}

/* Register with the in-kernel thermal management */
static int exynos_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret;
	struct cpumask mask_val;

	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone = kzalloc(sizeof(struct exynos_thermal_zone), GFP_KERNEL);
	if (!th_zone)
		return -ENOMEM;

	th_zone->sensor_conf = sensor_conf;
	cpumask_set_cpu(0, &mask_val);
	th_zone->cool_dev[0] = cpufreq_cooling_register(&mask_val);
	if (IS_ERR(th_zone->cool_dev[0])) {
		pr_err("Failed to register cpufreq cooling device\n");
		ret = -EINVAL;
		goto err_unregister;
	}
	th_zone->cool_dev_size++;

	th_zone->therm_dev = thermal_zone_device_register(sensor_conf->name,
			EXYNOS_ZONE_COUNT, 0, NULL, &exynos_dev_ops, NULL, 0,
			sensor_conf->trip_data.trigger_falling ?
			0 : IDLE_INTERVAL);

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

	if (!th_zone)
		return;

	if (th_zone->therm_dev)
		thermal_zone_device_unregister(th_zone->therm_dev);

	for (i = 0; i < th_zone->cool_dev_size; i++) {
		if (th_zone->cool_dev[i])
			cpufreq_cooling_unregister(th_zone->cool_dev[i]);
	}

	kfree(th_zone);
	pr_info("Exynos: Kernel Thermal management unregistered\n");
}

/*
 * TMU treats temperature as a mapped temperature code.
 * The temperature is converted differently depending on the calibration type.
 */
static int temp_to_code(struct exynos_tmu_data *data, u8 temp)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp_code;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		/* temp should range between 25 and 125 */
		if (temp < 25 || temp > 125) {
			temp_code = -EINVAL;
			goto out;
		}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp_code = (temp - 25) *
		    (data->temp_error2 - data->temp_error1) /
		    (85 - 25) + data->temp_error1;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp_code = temp + data->temp_error1 - 25;
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
static int code_to_temp(struct exynos_tmu_data *data, u8 temp_code)
{
	struct exynos_tmu_platform_data *pdata = data->pdata;
	int temp;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		/* temp_code should range between 75 and 175 */
		if (temp_code < 75 || temp_code > 175) {
			temp = -ENODATA;
			goto out;
		}

	switch (pdata->cal_type) {
	case TYPE_TWO_POINT_TRIMMING:
		temp = (temp_code - data->temp_error1) * (85 - 25) /
		    (data->temp_error2 - data->temp_error1) + 25;
		break;
	case TYPE_ONE_POINT_TRIMMING:
		temp = temp_code - data->temp_error1 + 25;
		break;
	default:
		temp = temp_code - EXYNOS_TMU_DEF_CODE_TO_TEMP_OFFSET;
		break;
	}
out:
	return temp;
}

static int exynos_tmu_initialize(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int status, trim_info;
	unsigned int rising_threshold = 0, falling_threshold = 0;
	int ret = 0, threshold_code, i, trigger_levs = 0;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	status = readb(data->base + EXYNOS_TMU_REG_STATUS);
	if (!status) {
		ret = -EBUSY;
		goto out;
	}

	if (data->soc == SOC_ARCH_EXYNOS) {
		__raw_writel(EXYNOS_TRIMINFO_RELOAD,
				data->base + EXYNOS_TMU_TRIMINFO_CON);
	}
	/* Save trimming info in order to perform calibration */
	trim_info = readl(data->base + EXYNOS_TMU_REG_TRIMINFO);
	data->temp_error1 = trim_info & EXYNOS_TMU_TRIM_TEMP_MASK;
	data->temp_error2 = ((trim_info >> 8) & EXYNOS_TMU_TRIM_TEMP_MASK);

	if ((EFUSE_MIN_VALUE > data->temp_error1) ||
			(data->temp_error1 > EFUSE_MAX_VALUE) ||
			(data->temp_error2 != 0))
		data->temp_error1 = pdata->efuse_value;

	/* Count trigger levels to be enabled */
	for (i = 0; i < MAX_THRESHOLD_LEVS; i++)
		if (pdata->trigger_levels[i])
			trigger_levs++;

	if (data->soc == SOC_ARCH_EXYNOS4210) {
		/* Write temperature code for threshold */
		threshold_code = temp_to_code(data, pdata->threshold);
		if (threshold_code < 0) {
			ret = threshold_code;
			goto out;
		}
		writeb(threshold_code,
			data->base + EXYNOS4210_TMU_REG_THRESHOLD_TEMP);
		for (i = 0; i < trigger_levs; i++)
			writeb(pdata->trigger_levels[i],
			data->base + EXYNOS4210_TMU_REG_TRIG_LEVEL0 + i * 4);

		writel(EXYNOS4210_TMU_INTCLEAR_VAL,
			data->base + EXYNOS_TMU_REG_INTCLEAR);
	} else if (data->soc == SOC_ARCH_EXYNOS) {
		/* Write temperature code for rising and falling threshold */
		for (i = 0; i < trigger_levs; i++) {
			threshold_code = temp_to_code(data,
						pdata->trigger_levels[i]);
			if (threshold_code < 0) {
				ret = threshold_code;
				goto out;
			}
			rising_threshold |= threshold_code << 8 * i;
			if (pdata->threshold_falling) {
				threshold_code = temp_to_code(data,
						pdata->trigger_levels[i] -
						pdata->threshold_falling);
				if (threshold_code > 0)
					falling_threshold |=
						threshold_code << 8 * i;
			}
		}

		writel(rising_threshold,
				data->base + EXYNOS_THD_TEMP_RISE);
		writel(falling_threshold,
				data->base + EXYNOS_THD_TEMP_FALL);

		writel(EXYNOS_TMU_CLEAR_RISE_INT | EXYNOS_TMU_CLEAR_FALL_INT,
				data->base + EXYNOS_TMU_REG_INTCLEAR);
	}
out:
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return ret;
}

static void exynos_tmu_control(struct platform_device *pdev, bool on)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	struct exynos_tmu_platform_data *pdata = data->pdata;
	unsigned int con, interrupt_en;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	con = pdata->reference_voltage << EXYNOS_TMU_REF_VOLTAGE_SHIFT |
		pdata->gain << EXYNOS_TMU_GAIN_SHIFT;

	if (data->soc == SOC_ARCH_EXYNOS) {
		con |= pdata->noise_cancel_mode << EXYNOS_TMU_TRIP_MODE_SHIFT;
		con |= (EXYNOS_MUX_ADDR_VALUE << EXYNOS_MUX_ADDR_SHIFT);
	}

	if (on) {
		con |= EXYNOS_TMU_CORE_ON;
		interrupt_en = pdata->trigger_level3_en << 12 |
			pdata->trigger_level2_en << 8 |
			pdata->trigger_level1_en << 4 |
			pdata->trigger_level0_en;
		if (pdata->threshold_falling)
			interrupt_en |= interrupt_en << 16;
	} else {
		con |= EXYNOS_TMU_CORE_OFF;
		interrupt_en = 0; /* Disable all interrupts */
	}
	writel(interrupt_en, data->base + EXYNOS_TMU_REG_INTEN);
	writel(con, data->base + EXYNOS_TMU_REG_CONTROL);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);
}

static int exynos_tmu_read(struct exynos_tmu_data *data)
{
	u8 temp_code;
	int temp;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	temp_code = readb(data->base + EXYNOS_TMU_REG_CURRENT_TEMP);
	temp = code_to_temp(data, temp_code);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	return temp;
}

static void exynos_tmu_work(struct work_struct *work)
{
	struct exynos_tmu_data *data = container_of(work,
			struct exynos_tmu_data, irq_work);

	exynos_report_trigger();
	mutex_lock(&data->lock);
	clk_enable(data->clk);
	if (data->soc == SOC_ARCH_EXYNOS)
		writel(EXYNOS_TMU_CLEAR_RISE_INT |
				EXYNOS_TMU_CLEAR_FALL_INT,
				data->base + EXYNOS_TMU_REG_INTCLEAR);
	else
		writel(EXYNOS4210_TMU_INTCLEAR_VAL,
				data->base + EXYNOS_TMU_REG_INTCLEAR);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	enable_irq(data->irq);
}

static irqreturn_t exynos_tmu_irq(int irq, void *id)
{
	struct exynos_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}
static struct thermal_sensor_conf exynos_sensor_conf = {
	.name			= "exynos-therm",
	.read_temperature	= (int (*)(void *))exynos_tmu_read,
};

#if defined(CONFIG_CPU_EXYNOS4210)
static struct exynos_tmu_platform_data const exynos4210_default_tmu_data = {
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
	.type = SOC_ARCH_EXYNOS4210,
};
#define EXYNOS4210_TMU_DRV_DATA (&exynos4210_default_tmu_data)
#else
#define EXYNOS4210_TMU_DRV_DATA (NULL)
#endif

#if defined(CONFIG_SOC_EXYNOS5250) || defined(CONFIG_SOC_EXYNOS4412)
static struct exynos_tmu_platform_data const exynos_default_tmu_data = {
	.threshold_falling = 10,
	.trigger_levels[0] = 85,
	.trigger_levels[1] = 103,
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
		.freq_clip_max = 800 * 1000,
		.temp_level = 85,
	},
	.freq_tab[1] = {
		.freq_clip_max = 200 * 1000,
		.temp_level = 103,
	},
	.freq_tab_count = 2,
	.type = SOC_ARCH_EXYNOS,
};
#define EXYNOS_TMU_DRV_DATA (&exynos_default_tmu_data)
#else
#define EXYNOS_TMU_DRV_DATA (NULL)
#endif

#ifdef CONFIG_OF
static const struct of_device_id exynos_tmu_match[] = {
	{
		.compatible = "samsung,exynos4210-tmu",
		.data = (void *)EXYNOS4210_TMU_DRV_DATA,
	},
	{
		.compatible = "samsung,exynos5250-tmu",
		.data = (void *)EXYNOS_TMU_DRV_DATA,
	},
	{},
};
MODULE_DEVICE_TABLE(of, exynos_tmu_match);
#endif

static struct platform_device_id exynos_tmu_driver_ids[] = {
	{
		.name		= "exynos4210-tmu",
		.driver_data    = (kernel_ulong_t)EXYNOS4210_TMU_DRV_DATA,
	},
	{
		.name		= "exynos5250-tmu",
		.driver_data    = (kernel_ulong_t)EXYNOS_TMU_DRV_DATA,
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

#ifdef CONFIG_EXYNOS_THERMAL_EMUL
static ssize_t exynos_tmu_emulation_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int reg;
	u8 temp_code;
	int temp = 0;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		goto out;

	mutex_lock(&data->lock);
	clk_enable(data->clk);
	reg = readl(data->base + EXYNOS_EMUL_CON);
	clk_disable(data->clk);
	mutex_unlock(&data->lock);

	if (reg & EXYNOS_EMUL_ENABLE) {
		reg >>= EXYNOS_EMUL_DATA_SHIFT;
		temp_code = reg & EXYNOS_EMUL_DATA_MASK;
		temp = code_to_temp(data, temp_code);
	}
out:
	return sprintf(buf, "%d\n", temp * MCELSIUS);
}

static ssize_t exynos_tmu_emulation_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = container_of(dev,
					struct platform_device, dev);
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);
	unsigned int reg;
	int temp;

	if (data->soc == SOC_ARCH_EXYNOS4210)
		goto out;

	if (!sscanf(buf, "%d\n", &temp) || temp < 0)
		return -EINVAL;

	mutex_lock(&data->lock);
	clk_enable(data->clk);

	reg = readl(data->base + EXYNOS_EMUL_CON);

	if (temp) {
		/* Both CELSIUS and MCELSIUS type are available for input */
		if (temp > MCELSIUS)
			temp /= MCELSIUS;

		reg = (EXYNOS_EMUL_TIME << EXYNOS_EMUL_TIME_SHIFT) |
			(temp_to_code(data, (temp / MCELSIUS))
			 << EXYNOS_EMUL_DATA_SHIFT) | EXYNOS_EMUL_ENABLE;
	} else {
		reg &= ~EXYNOS_EMUL_ENABLE;
	}

	writel(reg, data->base + EXYNOS_EMUL_CON);

	clk_disable(data->clk);
	mutex_unlock(&data->lock);

out:
	return count;
}

static DEVICE_ATTR(emulation, 0644, exynos_tmu_emulation_show,
					exynos_tmu_emulation_store);
static int create_emulation_sysfs(struct device *dev)
{
	return device_create_file(dev, &dev_attr_emulation);
}
static void remove_emulation_sysfs(struct device *dev)
{
	device_remove_file(dev, &dev_attr_emulation);
}
#else
static inline int create_emulation_sysfs(struct device *dev) { return 0; }
static inline void remove_emulation_sysfs(struct device *dev) {}
#endif

static int exynos_tmu_probe(struct platform_device *pdev)
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
	data = devm_kzalloc(&pdev->dev, sizeof(struct exynos_tmu_data),
					GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		return data->irq;
	}

	INIT_WORK(&data->irq_work, exynos_tmu_work);

	data->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!data->mem) {
		dev_err(&pdev->dev, "Failed to get platform resource\n");
		return -ENOENT;
	}

	data->base = devm_ioremap_resource(&pdev->dev, data->mem);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	ret = devm_request_irq(&pdev->dev, data->irq, exynos_tmu_irq,
		IRQF_TRIGGER_RISING, "exynos-tmu", data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		return ret;
	}

	data->clk = clk_get(NULL, "tmu_apbif");
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return  PTR_ERR(data->clk);
	}

	if (pdata->type == SOC_ARCH_EXYNOS ||
				pdata->type == SOC_ARCH_EXYNOS4210)
		data->soc = pdata->type;
	else {
		ret = -EINVAL;
		dev_err(&pdev->dev, "Platform not supported\n");
		goto err_clk;
	}

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	ret = exynos_tmu_initialize(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize TMU\n");
		goto err_clk;
	}

	exynos_tmu_control(pdev, true);

	/* Register the sensor with thermal management interface */
	(&exynos_sensor_conf)->private_data = data;
	exynos_sensor_conf.trip_data.trip_count = pdata->trigger_level0_en +
			pdata->trigger_level1_en + pdata->trigger_level2_en +
			pdata->trigger_level3_en;

	for (i = 0; i < exynos_sensor_conf.trip_data.trip_count; i++)
		exynos_sensor_conf.trip_data.trip_val[i] =
			pdata->threshold + pdata->trigger_levels[i];

	exynos_sensor_conf.trip_data.trigger_falling = pdata->threshold_falling;

	exynos_sensor_conf.cooling_data.freq_clip_count =
						pdata->freq_tab_count;
	for (i = 0; i < pdata->freq_tab_count; i++) {
		exynos_sensor_conf.cooling_data.freq_data[i].freq_clip_max =
					pdata->freq_tab[i].freq_clip_max;
		exynos_sensor_conf.cooling_data.freq_data[i].temp_level =
					pdata->freq_tab[i].temp_level;
	}

	ret = exynos_register_thermal(&exynos_sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_clk;
	}

	ret = create_emulation_sysfs(&pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "Failed to create emulation mode sysfs node\n");

	return 0;
err_clk:
	platform_set_drvdata(pdev, NULL);
	clk_put(data->clk);
	return ret;
}

static int exynos_tmu_remove(struct platform_device *pdev)
{
	struct exynos_tmu_data *data = platform_get_drvdata(pdev);

	remove_emulation_sysfs(&pdev->dev);

	exynos_tmu_control(pdev, false);

	exynos_unregister_thermal();

	clk_put(data->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_tmu_suspend(struct device *dev)
{
	exynos_tmu_control(to_platform_device(dev), false);

	return 0;
}

static int exynos_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	exynos_tmu_initialize(pdev);
	exynos_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(exynos_tmu_pm,
			 exynos_tmu_suspend, exynos_tmu_resume);
#define EXYNOS_TMU_PM	(&exynos_tmu_pm)
#else
#define EXYNOS_TMU_PM	NULL
#endif

static struct platform_driver exynos_tmu_driver = {
	.driver = {
		.name   = "exynos-tmu",
		.owner  = THIS_MODULE,
		.pm     = EXYNOS_TMU_PM,
		.of_match_table = of_match_ptr(exynos_tmu_match),
	},
	.probe = exynos_tmu_probe,
	.remove	= exynos_tmu_remove,
	.id_table = exynos_tmu_driver_ids,
};

module_platform_driver(exynos_tmu_driver);

MODULE_DESCRIPTION("EXYNOS TMU Driver");
MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:exynos-tmu");
