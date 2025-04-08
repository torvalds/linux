// SPDX-License-Identifier: GPL-2.0+
//
// RTC driver for Maxim MAX8997
//
// Copyright (C) 2013 Samsung Electronics Co.Ltd
//
//  based on rtc-max8998.c

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/max8997-private.h>
#include <linux/irqdomain.h>

/* Module parameter for WTSR function control */
static int wtsr_en = 1;
module_param(wtsr_en, int, 0444);
MODULE_PARM_DESC(wtsr_en, "Watchdog Timeout & Software Reset (default=on)");
/* Module parameter for SMPL function control */
static int smpl_en = 1;
module_param(smpl_en, int, 0444);
MODULE_PARM_DESC(smpl_en, "Sudden Momentary Power Loss (default=on)");

/* RTC Control Register */
#define BCD_EN_SHIFT			0
#define BCD_EN_MASK			(1 << BCD_EN_SHIFT)
#define MODEL24_SHIFT			1
#define MODEL24_MASK			(1 << MODEL24_SHIFT)
/* RTC Update Register1 */
#define RTC_UDR_SHIFT			0
#define RTC_UDR_MASK			(1 << RTC_UDR_SHIFT)
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

struct max8997_rtc_info {
	struct device		*dev;
	struct max8997_dev	*max8997;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;
	int virq;
	int rtc_24hr_mode;
};

static void max8997_rtc_data_to_tm(u8 *data, struct rtc_time *tm,
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

	tm->tm_wday = fls(data[RTC_WEEKDAY] & 0x7f) - 1;
	tm->tm_mday = data[RTC_DATE] & 0x1f;
	tm->tm_mon = (data[RTC_MONTH] & 0x0f) - 1;
	tm->tm_year = (data[RTC_YEAR] & 0x7f) + 100;
	tm->tm_yday = 0;
	tm->tm_isdst = 0;
}

static int max8997_rtc_tm_to_data(struct rtc_time *tm, u8 *data)
{
	data[RTC_SEC] = tm->tm_sec;
	data[RTC_MIN] = tm->tm_min;
	data[RTC_HOUR] = tm->tm_hour;
	data[RTC_WEEKDAY] = 1 << tm->tm_wday;
	data[RTC_DATE] = tm->tm_mday;
	data[RTC_MONTH] = tm->tm_mon + 1;
	data[RTC_YEAR] = tm->tm_year > 100 ? (tm->tm_year - 100) : 0;

	if (tm->tm_year < 100) {
		pr_warn("RTC cannot handle the year %d.  Assume it's 2000.\n",
			1900 + tm->tm_year);
		return -EINVAL;
	}
	return 0;
}

static inline int max8997_rtc_set_update_reg(struct max8997_rtc_info *info)
{
	int ret;

	ret = max8997_write_reg(info->rtc, MAX8997_RTC_UPDATE1,
						RTC_UDR_MASK);
	if (ret < 0)
		dev_err(info->dev, "%s: fail to write update reg(%d)\n",
				__func__, ret);
	else {
		/* Minimum 16ms delay required before RTC update.
		 * Otherwise, we may read and update based on out-of-date
		 * value */
		msleep(20);
	}

	return ret;
}

static int max8997_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max8997_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	mutex_lock(&info->lock);
	ret = max8997_bulk_read(info->rtc, MAX8997_RTC_SEC, RTC_NR_TIME, data);
	mutex_unlock(&info->lock);

	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read time reg(%d)\n", __func__,
				ret);
		return ret;
	}

	max8997_rtc_data_to_tm(data, tm, info->rtc_24hr_mode);

	return 0;
}

static int max8997_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max8997_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max8997_rtc_tm_to_data(tm, data);
	if (ret < 0)
		return ret;

	mutex_lock(&info->lock);

	ret = max8997_bulk_write(info->rtc, MAX8997_RTC_SEC, RTC_NR_TIME, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write time reg(%d)\n", __func__,
				ret);
		goto out;
	}

	ret = max8997_rtc_set_update_reg(info);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max8997_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max8997_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	u8 val;
	int i, ret;

	mutex_lock(&info->lock);

	ret = max8997_bulk_read(info->rtc, MAX8997_RTC_ALARM1_SEC, RTC_NR_TIME,
			data);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	max8997_rtc_data_to_tm(data, &alrm->time, info->rtc_24hr_mode);

	alrm->enabled = 0;
	for (i = 0; i < RTC_NR_TIME; i++) {
		if (data[i] & ALARM_ENABLE_MASK) {
			alrm->enabled = 1;
			break;
		}
	}

	alrm->pending = 0;
	ret = max8997_read_reg(info->max8997->i2c, MAX8997_REG_STATUS1, &val);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read status1 reg(%d)\n",
				__func__, __LINE__, ret);
		goto out;
	}

	if (val & (1 << 4)) /* RTCA1 */
		alrm->pending = 1;

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max8997_rtc_stop_alarm(struct max8997_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret, i;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max8997_bulk_read(info->rtc, MAX8997_RTC_ALARM1_SEC, RTC_NR_TIME,
				data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	for (i = 0; i < RTC_NR_TIME; i++)
		data[i] &= ~ALARM_ENABLE_MASK;

	ret = max8997_bulk_write(info->rtc, MAX8997_RTC_ALARM1_SEC, RTC_NR_TIME,
				 data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max8997_rtc_set_update_reg(info);
out:
	return ret;
}

static int max8997_rtc_start_alarm(struct max8997_rtc_info *info)
{
	u8 data[RTC_NR_TIME];
	int ret;

	if (!mutex_is_locked(&info->lock))
		dev_warn(info->dev, "%s: should have mutex locked\n", __func__);

	ret = max8997_bulk_read(info->rtc, MAX8997_RTC_ALARM1_SEC, RTC_NR_TIME,
				data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

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

	ret = max8997_bulk_write(info->rtc, MAX8997_RTC_ALARM1_SEC, RTC_NR_TIME,
				 data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max8997_rtc_set_update_reg(info);
out:
	return ret;
}
static int max8997_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max8997_rtc_info *info = dev_get_drvdata(dev);
	u8 data[RTC_NR_TIME];
	int ret;

	ret = max8997_rtc_tm_to_data(&alrm->time, data);
	if (ret < 0)
		return ret;

	dev_info(info->dev, "%s: %d-%02d-%02d %02d:%02d:%02d\n", __func__,
			data[RTC_YEAR] + 2000, data[RTC_MONTH], data[RTC_DATE],
			data[RTC_HOUR], data[RTC_MIN], data[RTC_SEC]);

	mutex_lock(&info->lock);

	ret = max8997_rtc_stop_alarm(info);
	if (ret < 0)
		goto out;

	ret = max8997_bulk_write(info->rtc, MAX8997_RTC_ALARM1_SEC, RTC_NR_TIME,
				data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
				__func__, ret);
		goto out;
	}

	ret = max8997_rtc_set_update_reg(info);
	if (ret < 0)
		goto out;

	if (alrm->enabled)
		ret = max8997_rtc_start_alarm(info);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max8997_rtc_alarm_irq_enable(struct device *dev,
					unsigned int enabled)
{
	struct max8997_rtc_info *info = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&info->lock);
	if (enabled)
		ret = max8997_rtc_start_alarm(info);
	else
		ret = max8997_rtc_stop_alarm(info);
	mutex_unlock(&info->lock);

	return ret;
}

static irqreturn_t max8997_rtc_alarm_irq(int irq, void *data)
{
	struct max8997_rtc_info *info = data;

	dev_info(info->dev, "%s:irq(%d)\n", __func__, irq);

	rtc_update_irq(info->rtc_dev, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops max8997_rtc_ops = {
	.read_time = max8997_rtc_read_time,
	.set_time = max8997_rtc_set_time,
	.read_alarm = max8997_rtc_read_alarm,
	.set_alarm = max8997_rtc_set_alarm,
	.alarm_irq_enable = max8997_rtc_alarm_irq_enable,
};

static void max8997_rtc_enable_wtsr(struct max8997_rtc_info *info, bool enable)
{
	int ret;
	u8 val, mask;

	if (!wtsr_en)
		return;

	if (enable)
		val = (1 << WTSR_EN_SHIFT) | (3 << WTSRT_SHIFT);
	else
		val = 0;

	mask = WTSR_EN_MASK | WTSRT_MASK;

	dev_info(info->dev, "%s: %s WTSR\n", __func__,
			enable ? "enable" : "disable");

	ret = max8997_update_reg(info->rtc, MAX8997_RTC_WTSR_SMPL, val, mask);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update WTSR reg(%d)\n",
				__func__, ret);
		return;
	}

	max8997_rtc_set_update_reg(info);
}

static void max8997_rtc_enable_smpl(struct max8997_rtc_info *info, bool enable)
{
	int ret;
	u8 val, mask;

	if (!smpl_en)
		return;

	if (enable)
		val = (1 << SMPL_EN_SHIFT) | (0 << SMPLT_SHIFT);
	else
		val = 0;

	mask = SMPL_EN_MASK | SMPLT_MASK;

	dev_info(info->dev, "%s: %s SMPL\n", __func__,
			enable ? "enable" : "disable");

	ret = max8997_update_reg(info->rtc, MAX8997_RTC_WTSR_SMPL, val, mask);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update SMPL reg(%d)\n",
				__func__, ret);
		return;
	}

	max8997_rtc_set_update_reg(info);

	val = 0;
	max8997_read_reg(info->rtc, MAX8997_RTC_WTSR_SMPL, &val);
	pr_info("WTSR_SMPL(0x%02x)\n", val);
}

static int max8997_rtc_init_reg(struct max8997_rtc_info *info)
{
	u8 data[2];
	int ret;

	/* Set RTC control register : Binary mode, 24hour mdoe */
	data[0] = (1 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);
	data[1] = (0 << BCD_EN_SHIFT) | (1 << MODEL24_SHIFT);

	info->rtc_24hr_mode = 1;

	ret = max8997_bulk_write(info->rtc, MAX8997_RTC_CTRLMASK, 2, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write controlm reg(%d)\n",
				__func__, ret);
		return ret;
	}

	ret = max8997_rtc_set_update_reg(info);
	return ret;
}

static int max8997_rtc_probe(struct platform_device *pdev)
{
	struct max8997_dev *max8997 = dev_get_drvdata(pdev->dev.parent);
	struct max8997_rtc_info *info;
	int ret, virq;

	info = devm_kzalloc(&pdev->dev, sizeof(struct max8997_rtc_info),
			GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->max8997 = max8997;
	info->rtc = max8997->rtc;

	platform_set_drvdata(pdev, info);

	ret = max8997_rtc_init_reg(info);

	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		return ret;
	}

	max8997_rtc_enable_wtsr(info, true);
	max8997_rtc_enable_smpl(info, true);

	device_init_wakeup(&pdev->dev, true);

	info->rtc_dev = devm_rtc_device_register(&pdev->dev, "max8997-rtc",
					&max8997_rtc_ops, THIS_MODULE);

	if (IS_ERR(info->rtc_dev)) {
		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		return ret;
	}

	virq = irq_create_mapping(max8997->irq_domain, MAX8997_PMICIRQ_RTCA1);
	if (!virq) {
		dev_err(&pdev->dev, "Failed to create mapping alarm IRQ\n");
		ret = -ENXIO;
		goto err_out;
	}
	info->virq = virq;

	ret = devm_request_threaded_irq(&pdev->dev, virq, NULL,
				max8997_rtc_alarm_irq, 0,
				"rtc-alarm0", info);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			info->virq, ret);

err_out:
	return ret;
}

static void max8997_rtc_shutdown(struct platform_device *pdev)
{
	struct max8997_rtc_info *info = platform_get_drvdata(pdev);

	max8997_rtc_enable_wtsr(info, false);
	max8997_rtc_enable_smpl(info, false);
}

static const struct platform_device_id rtc_id[] = {
	{ "max8997-rtc", 0 },
	{},
};
MODULE_DEVICE_TABLE(platform, rtc_id);

static struct platform_driver max8997_rtc_driver = {
	.driver		= {
		.name	= "max8997-rtc",
	},
	.probe		= max8997_rtc_probe,
	.shutdown	= max8997_rtc_shutdown,
	.id_table	= rtc_id,
};

module_platform_driver(max8997_rtc_driver);

MODULE_DESCRIPTION("Maxim MAX8997 RTC driver");
MODULE_AUTHOR("<ms925.kim@samsung.com>");
MODULE_LICENSE("GPL");
