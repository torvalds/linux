/*
 * RTC driver for Maxim MAX77802
 *
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 *
 *  based on rtc-max77686.c
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
#include <linux/mfd/max77802.h>
#include <linux/mfd/max77802-private.h>
#if defined(CONFIG_RTC_ALARM_BOOT)
#include <linux/reboot.h>
#endif

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

#define MAX77802_RTC_UPDATE_DELAY	1
#define MAX77802_RTC_WTSR_SMPL
#define MAX77802_RTC_DEBUG

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

struct max77802_rtc_info {
	struct device		*dev;
	struct max77802_dev	*max77802;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;
	int irq;
#if defined(CONFIG_RTC_ALARM_BOOT)
	int irq2;
#endif
	int rtc_24hr_mode;
};

enum MAX77802_RTC_OP {
	MAX77802_RTC_WRITE,
	MAX77802_RTC_READ,
};

static inline int max77802_rtc_calculate_wday(u8 shifted)
{
	int counter = -1;
	while (shifted) {
		shifted >>= 1;
		counter++;
	}
	return counter;
}

static void max77802_rtc_data_to_tm(u8 *data, struct rtc_time *tm,
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

	tm->tm_wday = max77802_rtc_calculate_wday(data[RTC_WEEKDAY] & 0x7f);
	tm->tm_mday = data[RTC_DATE] & 0x1f;
	tm->tm_mon = (data[RTC_MONTH] & 0x0f) - 1;
	tm->tm_year = (data[RTC_YEAR] & 0xff) + 100;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;
}

static int max77802_rtc_tm_to_data(struct rtc_time *tm, u8 *data)
{
	data[RTC_SEC] = tm->tm_sec;
	data[RTC_MIN] = tm->tm_min;
	data[RTC_HOUR] = tm->tm_hour;
	data[RTC_WEEKDAY] = 1 << tm->tm_wday;
	data[RTC_DATE] = tm->tm_mday;
	data[RTC_MONTH] = tm->tm_mon + 1;
	data[RTC_YEAR] = tm->tm_year > 100 ? (tm->tm_year - 100) : 0 ;

	if (tm->tm_year < 100) {
		pr_warn("%s: MAX77802 RTC cannot handle the year %d."
			"Assume it's 2000.\n", __func__, 1900 + tm->tm_year);
		return -EINVAL;
	}
	return 0;
}

static inline int max77802_rtc_update(struct max77802_rtc_info *info,
	enum MAX77802_RTC_OP op)
{
	int ret;
	u8 data;

	if (!info || !info->max77802->i2c) {
		pr_err("%s: Invalid argument\n", __func__);
		return -EINVAL;
	}

	switch (op) {
	case MAX77802_RTC_WRITE:
		data = 1 << RTC_UDR_SHIFT;
		break;
	case MAX77802_RTC_READ:
		data = 1 << RTC_RBUDR_SHIFT;
		break;
	}

	ret = max77802_update_reg(info->max77802->i2c, MAX77802_RTC_UPDATE0, data, data);
	if (ret < 0)
		dev_err(info->dev, "%s: fail to write update reg(ret=%d, data=0x%x)\n",
				__func__, ret, data);
	else {
		/* Minimum 200us delay required before RTC update. */
		msleep(MAX77802_RTC_UPDATE_DELAY);
	}

	return ret;
}

static int max77802_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max77802_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	mutex_lock(&info->lock);

	ret = max77802_rtc_update(info, MAX77802_RTC_READ);
	if (ret < 0)
		goto out;

	ret = max77802_bulk_read(info->max77802->i2c, MAX77802_RTC_SEC, RTC_NR_TIME, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read time reg(%d)\n", __func__,	ret);
		goto out;
	}

	max77802_rtc_data_to_tm(data, tm, info->rtc_24hr_mode);

	ret = rtc_valid_tm(tm);

	printk(KERN_INFO "%s: %d/%d/%d %d:%d:%d(%d)\n", __func__,
		tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_wday);

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77802_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max77802_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;
#ifdef MAX77802_RTC_DEBUG
	struct task_struct *task = current;
#endif

	ret = max77802_rtc_tm_to_data(tm, data);
	if (ret < 0)
		return ret;

	printk(KERN_INFO "%s: %d/%d/%d %d:%d:%d(%d)\n", __func__,
		tm->tm_year, tm->tm_mon, tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec, tm->tm_wday);

	mutex_lock(&info->lock);

	ret = max77802_bulk_write(info->max77802->i2c, MAX77802_RTC_SEC, RTC_NR_TIME, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write time reg(%d)\n", __func__,
				ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);

#ifdef MAX77802_RTC_DEBUG
	printk(KERN_INFO "%s: task=%s[%d]\n", __func__, task->comm, task->pid);
#endif

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77802_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77802_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	u8 val;
	int i, ret;

	mutex_lock(&info->lock);

	ret = max77802_rtc_update(info, MAX77802_RTC_READ);
	if (ret < 0)
		goto out;

	ret = max77802_bulk_read(info->max77802->i2c, MAX77802_ALARM1_SEC, RTC_NR_TIME, data);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	max77802_rtc_data_to_tm(data, &alrm->time, info->rtc_24hr_mode);

	printk(KERN_INFO "%s: %d/%d/%d %d:%d:%d(%d)\n", __func__,
		alrm->time.tm_year, alrm->time.tm_mon, alrm->time.tm_mday,
		alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec, alrm->time.tm_wday);

	alrm->enabled = 0;
	ret = max77802_read_reg(info->max77802->i2c, MAX77802_ALARM1_ENABLE, &val);
	for (i = 0; i < RTC_NR_TIME; i++) {
		if (val & (0x01<<i)) {
			alrm->enabled = 1;
			break;
		}
	}

	alrm->pending = 0;
	ret = max77802_read_reg(info->max77802->i2c, MAX77802_REG_STATUS2, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read status1 reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	if (val & (1 << 2)) /* RTCA1 */
		alrm->pending = 1;

out:
	mutex_unlock(&info->lock);
	return 0;
}

static int max77802_rtc_stop_alarm(struct max77802_rtc_info *info)
{
	int ret;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

    ret = max77802_write_reg(info->max77802->i2c, MAX77802_ALARM1_ENABLE, 0x00);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);
out:
	return ret;
}

#if defined(CONFIG_RTC_ALARM_BOOT)
static int max77802_rtc_stop_alarm_boot(struct max77802_rtc_info *info)
{
	int ret;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

    ret = max77802_write_reg(info->max77802->i2c, MAX77802_ALARM2_ENABLE, 0x00);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);
out:
	return ret;
}

#endif

static int max77802_rtc_start_alarm(struct max77802_rtc_info *info)
{
	int ret;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77802_write_reg(info->max77802->i2c, MAX77802_ALARM1_ENABLE, 0x7F);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);
out:
	return ret;
}
#if defined(CONFIG_RTC_ALARM_BOOT)
static int max77802_rtc_start_alarm_boot(struct max77802_rtc_info *info)
{
	int ret;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max77802_write_reg(info->max77802->i2c, MAX77802_ALARM2_ENABLE, 0x7F);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);
out:
	return ret;
}
#endif

static int max77802_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77802_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max77802_rtc_tm_to_data(&alrm->time, data);
	if (ret < 0)
		return ret;

	printk(KERN_INFO "%s: %d/%d/%d %d:%d:%d(%d)\n", __func__,
		alrm->time.tm_year, alrm->time.tm_mon, alrm->time.tm_mday,
		alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec, alrm->time.tm_wday);

	mutex_lock(&info->lock);

	ret = max77802_rtc_stop_alarm(info);
	if (ret < 0)
		goto out;

	ret = max77802_bulk_write(info->max77802->i2c, MAX77802_ALARM1_SEC, RTC_NR_TIME,
				data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);
	if (ret < 0)
		goto out;

	if (alrm->enabled)
		ret = max77802_rtc_start_alarm(info);

out:
	mutex_unlock(&info->lock);
	return ret;
}

#if defined(CONFIG_RTC_ALARM_BOOT)
static int max77802_rtc_set_alarm_boot(struct device *dev,
				      struct rtc_wkalrm *alrm)
{
	struct max77802_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	if (alrm->enabled) {
		data[RTC_SEC] = alrm->time.tm_sec;
		data[RTC_MIN] = alrm->time.tm_min;
		data[RTC_HOUR] = alrm->time.tm_hour;
		data[RTC_WEEKDAY] = 0;
		data[RTC_DATE] = alrm->time.tm_mday;
		data[RTC_MONTH] = alrm->time.tm_mon + 1;
		data[RTC_YEAR] = alrm->time.tm_year > 100
					? (alrm->time.tm_year - 100) : 0;
	} else {
		data[RTC_SEC] = 0;
		data[RTC_MIN] = 0;
		data[RTC_HOUR] = 0;
		data[RTC_WEEKDAY] = 0;
		data[RTC_DATE] = 1;
		data[RTC_MONTH] = 0;
		data[RTC_YEAR] = 0;
	}

	printk(KERN_INFO "%s: %d/%d/%d %d:%d:%d(%d)\n", __func__,
		alrm->time.tm_year, alrm->time.tm_mon, alrm->time.tm_mday,
		alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec,
		alrm->time.tm_wday);

	mutex_lock(&info->lock);

	ret = max77802_rtc_stop_alarm_boot(info);
	if (ret < 0)
		goto out;

	ret = max77802_bulk_write(info->max77802->i2c, MAX77802_ALARM2_SEC, RTC_NR_TIME,
				data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max77802_rtc_update(info, MAX77802_RTC_WRITE);
	if (ret < 0)
		goto out;

	if (alrm->enabled)
		ret = max77802_rtc_start_alarm_boot(info);
out:
	mutex_unlock(&info->lock);
	return ret;
}

#endif

static int max77802_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct max77802_rtc_info *info = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&info->lock);
	if (enabled)
		ret = max77802_rtc_start_alarm(info);
	else
		ret = max77802_rtc_stop_alarm(info);
	mutex_unlock(&info->lock);

	return ret;
}

static irqreturn_t max77802_rtc_alarm_irq(int irq, void *data)
{
	struct max77802_rtc_info *info = data;

	dev_info(info->dev, "%s:irq(%d)\n", __func__, irq);

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

#if defined(CONFIG_RTC_ALARM_BOOT)
static irqreturn_t max77802_rtc_alarm2_irq(int irq, void *data)
{
	struct max77802_rtc_info *info = data;
	int ret;
	u8 val;

	dev_info(info->dev, "%s:irq(%d)\n", __func__, irq);

#if defined(CONFIG_SLP)
	if (strstr(saved_command_line, "charger_detect_boot") != 0)
		kernel_restart(NULL);
#else
	if (lpcharge == 1)
		kernel_restart(NULL);
#endif

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}
#endif

static const struct rtc_class_ops max77802_rtc_ops = {
	.read_time = max77802_rtc_read_time,
	.set_time = max77802_rtc_set_time,
	.read_alarm = max77802_rtc_read_alarm,
#if defined(CONFIG_RTC_ALARM_BOOT) && defined(CONFIG_SLP)
	.set_alarm = max77802_rtc_set_alarm_boot,
#else
	.set_alarm = max77802_rtc_set_alarm,
#endif

#if defined(CONFIG_RTC_ALARM_BOOT)
	.set_alarm_boot = max77802_rtc_set_alarm_boot,
#endif
	.alarm_irq_enable = max77802_rtc_alarm_irq_enable,
};

#ifdef MAX77802_RTC_WTSR_SMPL
static void max77802_rtc_enable_wtsr(struct max77802_rtc_info *info, bool enable)
{
	int ret;
	u8 val, mask;

	if (enable)
		val = (1 << WTSR_EN_SHIFT) | (3 << WTSRT_SHIFT);
	else
		val = 0;

	mask = WTSR_EN_MASK | WTSRT_MASK;

	dev_info(info->dev, "%s: %s WTSR\n", __func__,
			enable ? "enable" : "disable");

	max77802_rtc_update(info, MAX77802_RTC_READ);

	ret = max77802_update_reg(info->max77802->i2c, MAX77802_WTSR_SMPL_CNTL, val, mask);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update WTSR reg(%d)\n",
				__func__, ret);
		return;
	}

	max77802_rtc_update(info, MAX77802_RTC_WRITE);
}

static void max77802_rtc_enable_smpl(struct max77802_rtc_info *info, bool enable)
{
	int ret;
	u8 val, mask;

	if (enable)
		val = (1 << SMPL_EN_SHIFT) | (0 << SMPLT_SHIFT);
	else
		val = 0;

	mask = SMPL_EN_MASK | SMPLT_MASK;

	dev_info(info->dev, "%s: %s SMPL\n", __func__,
			enable ? "enable" : "disable");

	max77802_rtc_update(info, MAX77802_RTC_READ);

	ret = max77802_update_reg(info->max77802->i2c, MAX77802_WTSR_SMPL_CNTL, val, mask);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update SMPL reg(%d)\n",
				__func__, ret);
		return;
	}

	max77802_rtc_update(info, MAX77802_RTC_WRITE);
}
#endif /* MAX77802_RTC_WTSR_SMPL */

static int max77802_rtc_init_reg(struct max77802_rtc_info *info)
{
	u8 data[2];
	u8 buf;
	int ret = 0;
	struct rtc_time tm;
#if defined(CONFIG_RTC_ALARM_BOOT)
	u8 data_alm2[RTC_NR_TIME];

	ret = max77802_rtc_update(info, MAX77802_RTC_READ);
	if (ret < 0)
		return ret;

	ret = max77802_bulk_read(info->max77802->i2c, MAX77802_ALARM2_SEC,
						RTC_NR_TIME, data_alm2);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
				__func__, __LINE__, ret);
		return ret;
	}

	printk(KERN_INFO "%s:alm2: %d/%d/%d %d:%d:%d(%d)\n", __func__,
		data_alm2[RTC_YEAR], data_alm2[RTC_MONTH], data_alm2[RTC_DATE],
		data_alm2[RTC_HOUR], data_alm2[RTC_MIN], data_alm2[RTC_SEC],
		data_alm2[RTC_WEEKDAY]);
#endif

	max77802_rtc_update(info, MAX77802_RTC_READ);

	ret = max77802_read_reg(info->max77802->i2c, MAX77802_RTC_CONTROL, &buf);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read control reg(%d)\n",
				__func__, ret);
		return ret;
	}

	if (buf & (1 << MODEL24_SHIFT)) {
		dev_info(info->dev, "%s: bypass init\n", __func__);
		return ret;
	}

	/* Set RTC control register : Binary mode, 24hour mdoe */
	data[0] = (1 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);
	data[1] = (0 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);

	ret = max77802_bulk_write(info->max77802->i2c, MAX77802_RTC_CONTROLM, 2, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write controlm reg(%d)\n",
				__func__, ret);
		return ret;
	}

	max77802_rtc_update(info, MAX77802_RTC_WRITE);

	/* Mask control register */
	max77802_rtc_update(info, MAX77802_RTC_READ);

	ret = max77802_update_reg(info->max77802->i2c, MAX77802_RTC_CONTROLM, 0x0, 0x3);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to mask CONTROLM reg(%d)\n",
				__func__, ret);
		return ret;
	}

	max77802_rtc_update(info, MAX77802_RTC_WRITE);

	/* If it's first boot, reset rtc to 1/1/2012 00:00:00(SUN) */
	if (buf == 0) {
		dev_info(info->dev, "rtc init\n");
		tm.tm_sec = 0;
		tm.tm_min = 0;
		tm.tm_hour = 0;
		tm.tm_wday = 0;
		tm.tm_mday = 1;
		tm.tm_mon = 0;
		tm.tm_year = 112;
		tm.tm_yday = 0;
		tm.tm_isdst = 0;
		max77802_rtc_set_time(info->dev, &tm);
	}

	return ret;
}

static int __devinit max77802_rtc_probe(struct platform_device *pdev)
{
	struct max77802_dev *max77802 = dev_get_drvdata(pdev->dev.parent);
	struct max77802_rtc_info *info;
	int ret;

	printk(KERN_INFO "%s\n", __func__);

	info = kzalloc(sizeof(struct max77802_rtc_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->max77802 = max77802;
	info->irq = max77802->irq_base + MAX77802_RTCIRQ_RTCA1;
#if defined(CONFIG_RTC_ALARM_BOOT)
	info->irq2 = max77802->irq_base + MAX77802_RTCIRQ_RTCA2;
#endif
	info->rtc_24hr_mode = 1;

	platform_set_drvdata(pdev, info);

	ret = max77802_rtc_init_reg(info);

	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		goto err_rtc;
	}

#ifdef MAX77802_RTC_WTSR_SMPL
	if (max77802->wtsr_smpl & MAX77802_WTSR_ENABLE)
		max77802_rtc_enable_wtsr(info, true);
#endif

	device_init_wakeup(&pdev->dev, 1);

	info->rtc_dev = rtc_device_register("max77802-rtc", &pdev->dev,
			&max77802_rtc_ops, THIS_MODULE);

	if (IS_ERR(info->rtc_dev)) {
		printk(KERN_INFO "%s: fail\n", __func__);

		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		if (ret == 0)
			ret = -EINVAL;
		goto err_rtc;
	}

	ret = request_threaded_irq(info->irq, NULL, max77802_rtc_alarm_irq, 0,
			"rtc-alarm0", info);
	if (ret < 0) {
		rtc_device_unregister(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			info->irq, ret);
		goto err_rtc;
	}

#if defined(CONFIG_RTC_ALARM_BOOT)
	ret = request_threaded_irq(info->irq2, NULL, max77802_rtc_alarm2_irq, 0,
			"rtc-alarm0", info);
	if (ret < 0) {
		rtc_device_unregister(info->rtc_dev);
		free_irq(info->irq, info);
		dev_err(&pdev->dev, "Failed to request alarm2 IRQ: %d: %d\n",
			info->irq2, ret);
		goto err_rtc;
	}
#endif

	goto out;
err_rtc:
	kfree(info);
	return ret;
out:
	return ret;
}

static int __devexit max77802_rtc_remove(struct platform_device *pdev)
{
	struct max77802_rtc_info *info = platform_get_drvdata(pdev);

	if (info) {
		free_irq(info->irq, info);
#if defined(CONFIG_RTC_ALARM_BOOT)
		free_irq(info->irq2, info);
#endif
		rtc_device_unregister(info->rtc_dev);
		kfree(info);
	}

	return 0;
}

static void max77802_rtc_shutdown(struct platform_device *pdev)
{
#ifdef MAX77802_RTC_WTSR_SMPL
	struct max77802_rtc_info *info = platform_get_drvdata(pdev);
	int i;
	u8 val = 0;

	for (i = 0; i < 3; i++) {
		max77802_rtc_enable_wtsr(info, false);

		max77802_rtc_update(info, MAX77802_RTC_READ);
		max77802_read_reg(info->max77802->i2c, MAX77802_WTSR_SMPL_CNTL, &val);
		pr_info("%s: WTSR_SMPL reg(0x%02x)\n", __func__, val);
		if (val & WTSR_EN_MASK)
			pr_emerg("%s: fail to disable WTSR\n", __func__);
		else {
			pr_info("%s: success to disable WTSR\n", __func__);
			break;
		}
	}

	/* Disable SMPL when power off */
	max77802_rtc_enable_smpl(info, false);
#endif /* MAX77802_RTC_WTSR_SMPL */
}

static const struct platform_device_id rtc_id[] = {
	{ "max77802-rtc", 0 },
	{},
};

static struct platform_driver max77802_rtc_driver = {
	.driver		= {
		.name	= "max77802-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= max77802_rtc_probe,
	.remove		= __devexit_p(max77802_rtc_remove),
	.shutdown	= max77802_rtc_shutdown,
	.id_table	= rtc_id,
};

static int __init max77802_rtc_init(void)
{
	return platform_driver_register(&max77802_rtc_driver);
}
module_init(max77802_rtc_init);

static void __exit max77802_rtc_exit(void)
{
	platform_driver_unregister(&max77802_rtc_driver);
}
module_exit(max77802_rtc_exit);

MODULE_DESCRIPTION("Maxim MAX77802 RTC driver");
MODULE_AUTHOR("Chiwoong Byun <woong.byun@samsung.com>");
MODULE_AUTHOR("Kangwon Lee <kw4.lee@samsung.com>");
MODULE_LICENSE("GPL");
