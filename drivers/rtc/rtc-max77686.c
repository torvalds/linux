/*
 * RTC driver for Maxim MAX77686
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
/* WTSR and SMPL Register */
#define WTSRT_SHIFT			0
#define SMPLT_SHIFT			2
#define WTSR_EN_SHIFT			6
#define SMPL_EN_SHIFT			7
#define WTSRT_MASK			(3 << WTSRT_SHIFT)
#define SMPLT_MASK			(3 << SMPLT_SHIFT)
#define WTSR_EN_MASK			(1 << WTSR_EN_SHIFT)
#define SMPL_EN_MASK			(1 << SMPL_EN_SHIFT)
/* RTC Hour register */
#define HOUR_PM_SHIFT			6
#define HOUR_PM_MASK			(1 << HOUR_PM_SHIFT)
/* RTC Alarm Enable */
#define ALARM_ENABLE_SHIFT		7
#define ALARM_ENABLE_MASK		(1 << ALARM_ENABLE_SHIFT)

#define MAX77686_RTC_UPDATE_DELAY	16
#undef MAX77686_RTC_WTSR_SMPL

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

struct max77686_rtc_info {
	struct device		*dev;
	struct max77686_dev	*max77686;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;

	struct regmap		*regmap;

	int virq;
	int rtc_24hr_mode;
};

enum MAX77686_RTC_OP {
	MAX77686_RTC_WRITE,
	MAX77686_RTC_READ,
};

static inline int max77686_rtc_calculate_wday(u8 shifted)
{
	int counter = -1;
	while (shifted) {
		shifted >>= 1;
		counter++;
	}
	return counter;
}

static void max77686_rtc_data_to_tm(u8 *data, struct rtc_time *tm,
				   int rtc_24hr_mode)
{
	tm->tm_sec = data[RTC_SEC] & 0x7f;
	tm->tm_min = data[RTC_MIN] & 0x7f;
	if (rtc_24hr_mode)
		tm->tm_hour = data[RTC_HOUR] & 0x1f;
	else {
		tm->tm_hour = data[RTC_HOUR] & 0x0f;
		if (data[RTC_HOUR] & HOUR_PM_MASK)
			tm->tm_hour += 12;
	}

	tm->tm_wday = max77686_rtc_calculate_wday(data[RTC_WEEKDAY] & 0x7f);
	tm->tm_mday = data[RTC_DATE] & 0x1f;
	tm->tm_mon = (data[RTC_MONTH] & 0x0f) - 1;
	tm->tm_year = (data[RTC_YEAR] & 0x7f) + 100;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;
}

static int max77686_rtc_tm_to_data(struct rtc_time *tm, u8 *data)
{
	data[RTC_SEC] = tm->tm_sec;
	data[RTC_MIN] = tm->tm_min;
	data[RTC_HOUR] = tm->tm_hour;
	data[RTC_WEEKDAY] = 1 << tm->tm_wday;
	data[RTC_DATE] = tm->tm_mday;
	data[RTC_MONTH] = tm->tm_mon + 1;
	data[RTC_YEAR] = tm->tm_year > 100 ? (tm->tm_year - 100) : 0 ;

	if (tm->tm_year < 100) {
		pr_warn("%s: MAX77686 RTC cannot handle the year %d."
			"Assume it's 2000.\n", __func__, 1900 + tm->tm_year);
		return -EINVAL;
	}
	return 0;
}

static int max77686_rtc_update(struct max77686_rtc_info *info,
	enum MAX77686_RTC_OP op)
{
	int ret;
	unsigned int data;

	if (op == MAX77686_RTC_WRITE)
		data = 1 << RTC_UDR_SHIFT;
	else
		data = 1 << RTC_RBUDR_SHIFT;

	ret = regmap_update_bits(info->max77686->rtc_regmap,
				 MAX77686_RTC_UPDATE0, data, data);
	if (ret < 0)
		dev_err(info->dev, "%s: fail to write update reg(ret=%d, data=0x%x)\n",
				__func__, ret, data);
	else {
		/* Minimum 16ms delay required before RTC update. */
		msleep(MAX77686_RTC_UPDATE_DELAY);
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
				MAX77686_RTC_SEC, data, RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read time reg(%d)\n", __func__,	ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, tm, info->rtc_24hr_mode);

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

	ret = max77686_rtc_tm_to_data(tm, data);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				 MAX77686_RTC_SEC, data, RTC_NR_TIME);
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
	int i, ret;

	mutex_lock(&info->lock);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77686->rtc_regmap,
				 MAX77686_ALARM1_SEC, data, RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, &alrm->time, info->rtc_24hr_mode);

	alrm->enabled = 0;
	for (i = 0; i < RTC_NR_TIME; i++) {
		if (data[i] & ALARM_ENABLE_MASK) {
			alrm->enabled = 1;
			break;
		}
	}

	alrm->pending = 0;
	ret = regmap_read(info->max77686->regmap, MAX77686_REG_STATUS1, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read status1 reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	if (val & (1 << 4)) /* RTCA1 */
		alrm->pending = 1;

out:
	mutex_unlock(&info->lock);
	return 0;
}

static int max77686_rtc_stop_alarm(struct max77686_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret, i;
	struct rtc_time tm;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77686->rtc_regmap,
				 MAX77686_ALARM1_SEC, data, RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, &tm, info->rtc_24hr_mode);

	for (i = 0; i < RTC_NR_TIME; i++)
		data[i] &= ~ALARM_ENABLE_MASK;

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				 MAX77686_ALARM1_SEC, data, RTC_NR_TIME);
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

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_read(info->max77686->rtc_regmap,
				 MAX77686_ALARM1_SEC, data, RTC_NR_TIME);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, &tm, info->rtc_24hr_mode);

	data[RTC_SEC] |= (1 << ALARM_ENABLE_SHIFT);
	data[RTC_MIN] |= (1 << ALARM_ENABLE_SHIFT);
	data[RTC_HOUR] |= (1 << ALARM_ENABLE_SHIFT);
	data[RTC_WEEKDAY] &= ~ALARM_ENABLE_MASK;
	if (data[RTC_MONTH] & 0xf)
		data[RTC_MONTH] |= (1 << ALARM_ENABLE_SHIFT);
	if (data[RTC_YEAR] & 0x7f)
		data[RTC_YEAR] |= (1 << ALARM_ENABLE_SHIFT);
	if (data[RTC_DATE] & 0x1f)
		data[RTC_DATE] |= (1 << ALARM_ENABLE_SHIFT);

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				 MAX77686_ALARM1_SEC, data, RTC_NR_TIME);
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

	ret = max77686_rtc_tm_to_data(&alrm->time, data);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = max77686_rtc_stop_alarm(info);
	if (ret < 0)
		goto out;

	ret = regmap_bulk_write(info->max77686->rtc_regmap,
				 MAX77686_ALARM1_SEC, data, RTC_NR_TIME);

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

#ifdef MAX77686_RTC_WTSR_SMPL
static void max77686_rtc_enable_wtsr(struct max77686_rtc_info *info, bool enable)
{
	int ret;
	unsigned int val, mask;

	if (enable)
		val = (1 << WTSR_EN_SHIFT) | (3 << WTSRT_SHIFT);
	else
		val = 0;

	mask = WTSR_EN_MASK | WTSRT_MASK;

	dev_info(info->dev, "%s: %s WTSR\n", __func__,
			enable ? "enable" : "disable");

	ret = regmap_update_bits(info->max77686->rtc_regmap,
				 MAX77686_WTSR_SMPL_CNTL, mask, val);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update WTSR reg(%d)\n",
				__func__, ret);
		return;
	}

	max77686_rtc_update(info, MAX77686_RTC_WRITE);
}

static void max77686_rtc_enable_smpl(struct max77686_rtc_info *info, bool enable)
{
	int ret;
	unsigned int val, mask;

	if (enable)
		val = (1 << SMPL_EN_SHIFT) | (0 << SMPLT_SHIFT);
	else
		val = 0;

	mask = SMPL_EN_MASK | SMPLT_MASK;

	dev_info(info->dev, "%s: %s SMPL\n", __func__,
			enable ? "enable" : "disable");

	ret = regmap_update_bits(info->max77686->rtc_regmap,
				 MAX77686_WTSR_SMPL_CNTL, mask, val);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update SMPL reg(%d)\n",
				__func__, ret);
		return;
	}

	max77686_rtc_update(info, MAX77686_RTC_WRITE);

	val = 0;
	regmap_read(info->max77686->rtc_regmap, MAX77686_WTSR_SMPL_CNTL, &val);
	dev_info(info->dev, "%s: WTSR_SMPL(0x%02x)\n", __func__, val);
}
#endif /* MAX77686_RTC_WTSR_SMPL */

static int max77686_rtc_init_reg(struct max77686_rtc_info *info)
{
	u8 data[2];
	int ret;

	/* Set RTC control register : Binary mode, 24hour mdoe */
	data[0] = (1 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);
	data[1] = (0 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);

	info->rtc_24hr_mode = 1;

	ret = regmap_bulk_write(info->max77686->rtc_regmap, MAX77686_RTC_CONTROLM, data, 2);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write controlm reg(%d)\n",
				__func__, ret);
		return ret;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
	return ret;
}

static struct regmap_config max77686_rtc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int max77686_rtc_probe(struct platform_device *pdev)
{
	struct max77686_dev *max77686 = dev_get_drvdata(pdev->dev.parent);
	struct max77686_rtc_info *info;
	int ret, virq;

	dev_info(&pdev->dev, "%s\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(struct max77686_rtc_info),
				GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->max77686 = max77686;
	info->rtc = max77686->rtc;
	info->max77686->rtc_regmap = devm_regmap_init_i2c(info->max77686->rtc,
					 &max77686_rtc_regmap_config);
	if (IS_ERR(info->max77686->rtc_regmap)) {
		ret = PTR_ERR(info->max77686->rtc_regmap);
		dev_err(info->max77686->dev, "Failed to allocate register map: %d\n",
				ret);
		return ret;
	}
	platform_set_drvdata(pdev, info);

	ret = max77686_rtc_init_reg(info);

	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		goto err_rtc;
	}

#ifdef MAX77686_RTC_WTSR_SMPL
	max77686_rtc_enable_wtsr(info, true);
	max77686_rtc_enable_smpl(info, true);
#endif

	device_init_wakeup(&pdev->dev, 1);

	info->rtc_dev = devm_rtc_device_register(&pdev->dev, "max77686-rtc",
					&max77686_rtc_ops, THIS_MODULE);

	if (IS_ERR(info->rtc_dev)) {
		dev_info(&pdev->dev, "%s: fail\n", __func__);

		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		if (ret == 0)
			ret = -EINVAL;
		goto err_rtc;
	}
	virq = irq_create_mapping(max77686->irq_domain, MAX77686_RTCIRQ_RTCA1);
	if (!virq) {
		ret = -ENXIO;
		goto err_rtc;
	}
	info->virq = virq;

	ret = devm_request_threaded_irq(&pdev->dev, virq, NULL,
				max77686_rtc_alarm_irq, 0, "rtc-alarm0", info);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			info->virq, ret);

err_rtc:
	return ret;
}

static int max77686_rtc_remove(struct platform_device *pdev)
{
	return 0;
}

static void max77686_rtc_shutdown(struct platform_device *pdev)
{
#ifdef MAX77686_RTC_WTSR_SMPL
	struct max77686_rtc_info *info = platform_get_drvdata(pdev);
	int i;
	u8 val = 0;

	for (i = 0; i < 3; i++) {
		max77686_rtc_enable_wtsr(info, false);
		regmap_read(info->max77686->rtc_regmap, MAX77686_WTSR_SMPL_CNTL, &val);
		dev_info(info->dev, "%s: WTSR_SMPL reg(0x%02x)\n", __func__,
				val);
		if (val & WTSR_EN_MASK) {
			dev_emerg(info->dev, "%s: fail to disable WTSR\n",
					__func__);
		} else {
			dev_info(info->dev, "%s: success to disable WTSR\n",
					__func__);
			break;
		}
	}

	/* Disable SMPL when power off */
	max77686_rtc_enable_smpl(info, false);
#endif /* MAX77686_RTC_WTSR_SMPL */
}

static const struct platform_device_id rtc_id[] = {
	{ "max77686-rtc", 0 },
	{},
};

static struct platform_driver max77686_rtc_driver = {
	.driver		= {
		.name	= "max77686-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= max77686_rtc_probe,
	.remove		= max77686_rtc_remove,
	.shutdown	= max77686_rtc_shutdown,
	.id_table	= rtc_id,
};

module_platform_driver(max77686_rtc_driver);

MODULE_DESCRIPTION("Maxim MAX77686 RTC driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_LICENSE("GPL");
