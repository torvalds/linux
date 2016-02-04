/*
 * RTC driver for Maxim MAX77686 and MAX77802
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 *
 *  based on rtc-max8997.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77686-private.h>
#include <linux/irqdomain.h>
#include <linux/regmap.h>

/* RTC Control Register */
#define BCD_EN_SHIFT			0
#define BCD_EN_MASK			(1 << BCD_EN_SHIFT)
#define MODEL24_SHIFT			1
#define MODEL24_MASK			(1 << MODEL24_SHIFT)
/* RTC Update Register1 */
#define RTC_UDR_SHIFT			0
#define RTC_UDR_MASK			(1 << RTC_UDR_SHIFT)
#define RTC_RBUDR_SHIFT			4
#define RTC_RBUDR_MASK			(1 << RTC_RBUDR_SHIFT)
/* RTC Hour register */
#define HOUR_PM_SHIFT			6
#define HOUR_PM_MASK			(1 << HOUR_PM_SHIFT)
/* RTC Alarm Enable */
#define ALARM_ENABLE_SHIFT		7
#define ALARM_ENABLE_MASK		(1 << ALARM_ENABLE_SHIFT)

#define REG_RTC_NONE			0xdeadbeef

/*
 * MAX77802 has separate register (RTCAE1) for alarm enable instead
 * using 1 bit from registers RTC{SEC,MIN,HOUR,DAY,MONTH,YEAR,DATE}
 * as in done in MAX77686.
 */
#define MAX77802_ALARM_ENABLE_VALUE	0x77

enum {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WEEKDAY,
	RTC_MONTH,
	RTC_YEAR,
	RTC_DATE,
	RTC_NR_TIME
};

struct max77686_rtc_driver_data {
	/* Minimum usecs needed for a RTC update */
	unsigned long		delay;
	/* Mask used to read RTC registers value */
	u8			mask;
	/* Registers offset to I2C addresses map */
	const unsigned int	*map;
	/* Has a separate alarm enable register? */
	bool			alarm_enable_reg;
	/* Has a separate I2C regmap for the RTC? */
	bool			separate_i2c_addr;
};

struct max77686_rtc_info {
	struct device		*dev;
	struct max77686_dev	*max77686;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;

	struct regmap		*regmap;

	const struct max77686_rtc_driver_data *drv_data;

	int virq;
	int rtc_24hr_mode;
};

enum MAX77686_RTC_OP {
	MAX77686_RTC_WRITE,
	MAX77686_RTC_READ,
};

/* These are not registers but just offsets that are mapped to addresses */
enum max77686_rtc_reg_offset {
	REG_RTC_CONTROLM = 0,
	REG_RTC_CONTROL,
	REG_RTC_UPDATE0,
	REG_WTSR_SMPL_CNTL,
	REG_RTC_SEC,
	REG_RTC_MIN,
	REG_RTC_HOUR,
	REG_RTC_WEEKDAY,
	REG_RTC_MONTH,
	REG_RTC_YEAR,
	REG_RTC_DATE,
	REG_ALARM1_SEC,
	REG_ALARM1_MIN,
	REG_ALARM1_HOUR,
	REG_ALARM1_WEEKDAY,
	REG_ALARM1_MONTH,
	REG_ALARM1_YEAR,
	REG_ALARM1_DATE,
	REG_ALARM2_SEC,
	REG_ALARM2_MIN,
	REG_ALARM2_HOUR,
	REG_ALARM2_WEEKDAY,
	REG_ALARM2_MONTH,
	REG_ALARM2_YEAR,
	REG_ALARM2_DATE,
	REG_RTC_AE1,
	REG_RTC_END,
};

/* Maps RTC registers offset to the MAX77686 register addresses */
static const unsigned int max77686_map[REG_RTC_END] = {
	[REG_RTC_CONTROLM]   = MAX77686_RTC_CONTROLM,
	[REG_RTC_CONTROL]    = MAX77686_RTC_CONTROL,
	[REG_RTC_UPDATE0]    = MAX77686_RTC_UPDATE0,
	[REG_WTSR_SMPL_CNTL] = MAX77686_WTSR_SMPL_CNTL,
	[REG_RTC_SEC]        = MAX77686_RTC_SEC,
	[REG_RTC_MIN]        = MAX77686_RTC_MIN,
	[REG_RTC_HOUR]       = MAX77686_RTC_HOUR,
	[REG_RTC_WEEKDAY]    = MAX77686_RTC_WEEKDAY,
	[REG_RTC_MONTH]      = MAX77686_RTC_MONTH,
	[REG_RTC_YEAR]       = MAX77686_RTC_YEAR,
	[REG_RTC_DATE]       = MAX77686_RTC_DATE,
	[REG_ALARM1_SEC]     = MAX77686_ALARM1_SEC,
	[REG_ALARM1_MIN]     = MAX77686_ALARM1_MIN,
	[REG_ALARM1_HOUR]    = MAX77686_ALARM1_HOUR,
	[REG_ALARM1_WEEKDAY] = MAX77686_ALARM1_WEEKDAY,
	[REG_ALARM1_MONTH]   = MAX77686_ALARM1_MONTH,
	[REG_ALARM1_YEAR]    = MAX77686_ALARM1_YEAR,
	[REG_ALARM1_DATE]    = MAX77686_ALARM1_DATE,
	[REG_ALARM2_SEC]     = MAX77686_ALARM2_SEC,
	[REG_ALARM2_MIN]     = MAX77686_ALARM2_MIN,
	[REG_ALARM2_HOUR]    = MAX77686_ALARM2_HOUR,
	[REG_ALARM2_WEEKDAY] = MAX77686_ALARM2_WEEKDAY,
	[REG_ALARM2_MONTH]   = MAX77686_ALARM2_MONTH,
	[REG_ALARM2_YEAR]    = MAX77686_ALARM2_YEAR,
	[REG_ALARM2_DATE]    = MAX77686_ALARM2_DATE,
	[REG_RTC_AE1]	     = REG_RTC_NONE,
};

static const struct max77686_rtc_driver_data max77686_drv_data = {
	.delay = 16000,
	.mask  = 0x7f,
	.map   = max77686_map,
	.alarm_enable_reg  = false,
	.separate_i2c_addr = true,
};

static const unsigned int max77802_map[REG_RTC_END] = {
	[REG_RTC_CONTROLM]   = MAX77802_RTC_CONTROLM,
	[REG_RTC_CONTROL]    = MAX77802_RTC_CONTROL,
	[REG_RTC_UPDATE0]    = MAX77802_RTC_UPDATE0,
	[REG_WTSR_SMPL_CNTL] = MAX77802_WTSR_SMPL_CNTL,
	[REG_RTC_SEC]        = MAX77802_RTC_SEC,
	[REG_RTC_MIN]        = MAX77802_RTC_MIN,
	[REG_RTC_HOUR]       = MAX77802_RTC_HOUR,
	[REG_RTC_WEEKDAY]    = MAX77802_RTC_WEEKDAY,
	[REG_RTC_MONTH]      = MAX77802_RTC_MONTH,
	[REG_RTC_YEAR]       = MAX77802_RTC_YEAR,
	[REG_RTC_DATE]       = MAX77802_RTC_DATE,
	[REG_ALARM1_SEC]     = MAX77802_ALARM1_SEC,
	[REG_ALARM1_MIN]     = MAX77802_ALARM1_MIN,
	[REG_ALARM1_HOUR]    = MAX77802_ALARM1_HOUR,
	[REG_ALARM1_WEEKDAY] = MAX77802_ALARM1_WEEKDAY,
	[REG_ALARM1_MONTH]   = MAX77802_ALARM1_MONTH,
	[REG_ALARM1_YEAR]    = MAX77802_ALARM1_YEAR,
	[REG_ALARM1_DATE]    = MAX77802_ALARM1_DATE,
	[REG_ALARM2_SEC]     = MAX77802_ALARM2_SEC,
	[REG_ALARM2_MIN]     = MAX77802_ALARM2_MIN,
	[REG_ALARM2_HOUR]    = MAX77802_ALARM2_HOUR,
	[REG_ALARM2_WEEKDAY] = MAX77802_ALARM2_WEEKDAY,
	[REG_ALARM2_MONTH]   = MAX77802_ALARM2_MONTH,
	[REG_ALARM2_YEAR]    = MAX77802_ALARM2_YEAR,
	[REG_ALARM2_DATE]    = MAX77802_ALARM2_DATE,
	[REG_RTC_AE1]	     = MAX77802_RTC_AE1,
};

static const struct max77686_rtc_driver_data max77802_drv_data = {
	.delay = 200,
	.mask  = 0xff,
	.map   = max77802_map,
	.alarm_enable_reg  = true,
	.separate_i2c_addr = false,
};

static void max77686_rtc_data_to_tm(u8 *data, struct rtc_time *tm,
				    struct max77686_rtc_info *info)
{
	u8 mask = info->drv_data->mask;

	tm->tm_sec = data[RTC_SEC] & mask;
	tm->tm_min = data[RTC_MIN] & mask;
	if (info->rtc_24hr_mode)
		tm->tm_hour = data[RTC_HOUR] & 0x1f;
	else {
		tm->tm_hour = data[RTC_HOUR] & 0x0f;
		if (data[RTC_HOUR] & HOUR_PM_MASK)
			tm->tm_hour += 12;
	}

	/* Only a single bit is set in data[], so fls() would be equivalent */
	tm->tm_wday = ffs(data[RTC_WEEKDAY] & mask) - 1;
	tm->tm_mday = data[RTC_DATE] & 0x1f;
	tm->tm_mon = (data[RTC_MONTH] & 0x0f) - 1;
	tm->tm_year = data[RTC_YEAR] & mask;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;

	/*
	 * MAX77686 uses 1 bit from sec/min/hour/etc RTC registers and the
	 * year values are just 0..99 so add 100 to support up to 2099.
	 */
	if (!info->drv_data->alarm_enable_reg)
		tm->tm_year += 100;
}

static int max77686_rtc_tm_to_data(struct rtc_time *tm, u8 *data,
				   struct max77686_rtc_info *info)
{
	data[RTC_SEC] = tm->tm_sec;
	data[RTC_MIN] = tm->tm_min;
	data[RTC_HOUR] = tm->tm_hour;
	data[RTC_WEEKDAY] = 1 << tm->tm_wday;
	data[RTC_DATE] = tm->tm_mday;
	data[RTC_MONTH] = tm->tm_mon + 1;

	if (info->drv_data->alarm_enable_reg) {
		data[RTC_YEAR] = tm->tm_year;
		return 0;
	}

	data[RTC_YEAR] = tm->tm_year > 100 ? (tm->tm_year - 100) : 0;

	if (tm->tm_year < 100) {
		dev_err(info->dev, "RTC cannot handle the year %d.\n",
			1900 + tm->tm_year);
		return -EINVAL;
	}

	return 0;
}

static int max77686_rtc_update(struct max77686_rtc_info *info,
	enum MAX77686_RTC_OP op)
{
	int ret;
	unsigned int data;
	unsigned long delay = info->drv_data->delay;

	if (op == MAX77686_RTC_WRITE)
		data = 1 << RTC_UDR_SHIFT;
	else
		data = 1 << RTC_RBUDR_SHIFT;

	ret = regmap_update_bits(info->max77686->rtc_regmap,
				 info->drv_data->map[REG_RTC_UPDATE0],
				 data, data);
	if (ret < 0)
		dev_err(info->dev, "%s: fail to write update reg(ret=%d, data=0x%x)\n",
				__func__, ret, data);
	else {
		/* Minimum delay required before RTC update. */
		usleep_range(delay, delay * 2);
	}

	return ret;
}

static int max77686_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	mutex_lock(&info->lock);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77686->rtc_regmap,
			       info->drv_data->map[REG_RTC_SEC],
			       data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read time reg(%d)\n", __func__,	ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, tm, info);

	ret = rtc_valid_tm(tm);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77686_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max77686_rtc_tm_to_data(tm, data, info);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				info->drv_data->map[REG_RTC_SEC],
				data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write time reg(%d)\n", __func__,
				ret);
		goto out;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77686_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	unsigned int val;
	const unsigned int *map = info->drv_data->map;
	int i, ret;

	mutex_lock(&info->lock);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77686->rtc_regmap,
			       map[REG_ALARM1_SEC], data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, &alrm->time, info);

	alrm->enabled = 0;

	if (info->drv_data->alarm_enable_reg) {
		if (map[REG_RTC_AE1] == REG_RTC_NONE) {
			ret = -EINVAL;
			dev_err(info->dev,
				"alarm enable register not set(%d)\n", ret);
			goto out;
		}

		ret = regmap_read(info->max77686->regmap,
				  map[REG_RTC_AE1], &val);
		if (ret < 0) {
			dev_err(info->dev,
				"fail to read alarm enable(%d)\n", ret);
			goto out;
		}

		if (val)
			alrm->enabled = 1;
	} else {
		for (i = 0; i < ARRAY_SIZE(data); i++) {
			if (data[i] & ALARM_ENABLE_MASK) {
				alrm->enabled = 1;
				break;
			}
		}
	}

	alrm->pending = 0;
	ret = regmap_read(info->max77686->regmap, MAX77686_REG_STATUS2, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read status2 reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	if (val & (1 << 4)) /* RTCA1 */
		alrm->pending = 1;

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77686_rtc_stop_alarm(struct max77686_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret, i;
	struct rtc_time tm;
	const unsigned int *map = info->drv_data->map;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	if (info->drv_data->alarm_enable_reg) {
		if (map[REG_RTC_AE1] == REG_RTC_NONE) {
			ret = -EINVAL;
			dev_err(info->dev,
				"alarm enable register not set(%d)\n", ret);
			goto out;
		}

		ret = regmap_write(info->max77686->regmap, map[REG_RTC_AE1], 0);
	} else {
		ret = regmap_bulk_read(info->max77686->rtc_regmap,
				       map[REG_ALARM1_SEC], data,
				       ARRAY_SIZE(data));
		if (ret < 0) {
			dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
			goto out;
		}

		max77686_rtc_data_to_tm(data, &tm, info);

		for (i = 0; i < ARRAY_SIZE(data); i++)
			data[i] &= ~ALARM_ENABLE_MASK;

		ret = regmap_bulk_write(info->max77686->rtc_regmap,
					map[REG_ALARM1_SEC], data,
					ARRAY_SIZE(data));
	}

	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
out:
	return ret;
}

static int max77686_rtc_start_alarm(struct max77686_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret;
	struct rtc_time tm;
	const unsigned int *map = info->drv_data->map;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	if (info->drv_data->alarm_enable_reg) {
		ret = regmap_write(info->max77686->regmap, map[REG_RTC_AE1],
				   MAX77802_ALARM_ENABLE_VALUE);
	} else {
		ret = regmap_bulk_read(info->max77686->rtc_regmap,
				       map[REG_ALARM1_SEC], data,
				       ARRAY_SIZE(data));
		if (ret < 0) {
			dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
			goto out;
		}

		max77686_rtc_data_to_tm(data, &tm, info);

		data[RTC_SEC] |= (1 << ALARM_ENABLE_SHIFT);
		data[RTC_MIN] |= (1 << ALARM_ENABLE_SHIFT);
		data[RTC_HOUR] |= (1 << ALARM_ENABLE_SHIFT);
		data[RTC_WEEKDAY] &= ~ALARM_ENABLE_MASK;
		if (data[RTC_MONTH] & 0xf)
			data[RTC_MONTH] |= (1 << ALARM_ENABLE_SHIFT);
		if (data[RTC_YEAR] & info->drv_data->mask)
			data[RTC_YEAR] |= (1 << ALARM_ENABLE_SHIFT);
		if (data[RTC_DATE] & 0x1f)
			data[RTC_DATE] |= (1 << ALARM_ENABLE_SHIFT);

		ret = regmap_bulk_write(info->max77686->rtc_regmap,
					map[REG_ALARM1_SEC], data,
					ARRAY_SIZE(data));
	}

	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
out:
	return ret;
}

static int max77686_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max77686_rtc_tm_to_data(&alrm->time, data, info);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = max77686_rtc_stop_alarm(info);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				info->drv_data->map[REG_ALARM1_SEC],
				data, ARRAY_SIZE(data));

	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
	if (ret < 0)
		goto out;

	if (alrm->enabled)
		ret = max77686_rtc_start_alarm(info);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77686_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&info->lock);
	if (enabled)
		ret = max77686_rtc_start_alarm(info);
	else
		ret = max77686_rtc_stop_alarm(info);
	mutex_unlock(&info->lock);

	return ret;
}

static irqreturn_t max77686_rtc_alarm_irq(int irq, void *data)
{
	struct max77686_rtc_info *info = data;

	dev_info(info->dev, "%s:irq(%d)\n", __func__, irq);

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops max77686_rtc_ops = {
	.read_time = max77686_rtc_read_time,
	.set_time = max77686_rtc_set_time,
	.read_alarm = max77686_rtc_read_alarm,
	.set_alarm = max77686_rtc_set_alarm,
	.alarm_irq_enable = max77686_rtc_alarm_irq_enable,
};

static int max77686_rtc_init_reg(struct max77686_rtc_info *info)
{
	u8 data[2];
	int ret;

	/* Set RTC control register : Binary mode, 24hour mdoe */
	data[0] = (1 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);
	data[1] = (0 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);

	info->rtc_24hr_mode = 1;

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				info->drv_data->map[REG_RTC_CONTROLM],
				data, ARRAY_SIZE(data));
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write controlm reg(%d)\n",
				__func__, ret);
		return ret;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
	return ret;
}

static int max77686_rtc_probe(struct platform_device *pdev)
{
	struct max77686_dev *max77686 = dev_get_drvdata(pdev->dev.parent);
	struct max77686_rtc_info *info;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	int ret;

	dev_info(&pdev->dev, "%s\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(struct max77686_rtc_info),
				GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->max77686 = max77686;
	info->rtc = max77686->rtc;
	info->drv_data = (const struct max77686_rtc_driver_data *)
		id->driver_data;

	if (!info->drv_data->separate_i2c_addr)
		info->max77686->rtc_regmap = info->max77686->regmap;

	platform_set_drvdata(pdev, info);

	ret = max77686_rtc_init_reg(info);

	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		goto err_rtc;
	}

	device_init_wakeup(&pdev->dev, 1);

	info->rtc_dev = devm_rtc_device_register(&pdev->dev, id->name,
					&max77686_rtc_ops, THIS_MODULE);

	if (IS_ERR(info->rtc_dev)) {
		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		if (ret == 0)
			ret = -EINVAL;
		goto err_rtc;
	}

	if (!max77686->rtc_irq_data) {
		ret = -EINVAL;
		dev_err(&pdev->dev, "%s: no RTC regmap IRQ chip\n", __func__);
		goto err_rtc;
	}

	info->virq = regmap_irq_get_virq(max77686->rtc_irq_data,
					 MAX77686_RTCIRQ_RTCA1);
	if (info->virq <= 0) {
		ret = -ENXIO;
		goto err_rtc;
	}

	ret = devm_request_threaded_irq(&pdev->dev, info->virq, NULL,
				max77686_rtc_alarm_irq, 0, "rtc-alarm1", info);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			info->virq, ret);

err_rtc:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int max77686_rtc_suspend(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		struct max77686_rtc_info *info = dev_get_drvdata(dev);

		return enable_irq_wake(info->virq);
	}

	return 0;
}

static int max77686_rtc_resume(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		struct max77686_rtc_info *info = dev_get_drvdata(dev);

		return disable_irq_wake(info->virq);
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(max77686_rtc_pm_ops,
			 max77686_rtc_suspend, max77686_rtc_resume);

static const struct platform_device_id rtc_id[] = {
	{ "max77686-rtc", .driver_data = (kernel_ulong_t)&max77686_drv_data, },
	{ "max77802-rtc", .driver_data = (kernel_ulong_t)&max77802_drv_data, },
	{},
};
MODULE_DEVICE_TABLE(platform, rtc_id);

static struct platform_driver max77686_rtc_driver = {
	.driver		= {
		.name	= "max77686-rtc",
		.pm	= &max77686_rtc_pm_ops,
	},
	.probe		= max77686_rtc_probe,
	.id_table	= rtc_id,
};

module_platform_driver(max77686_rtc_driver);

MODULE_DESCRIPTION("Maxim MAX77686 RTC driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
