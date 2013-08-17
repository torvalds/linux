/*
 * RTC driver for Maxim MAX77686
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/max77686.h>
#include <linux/mfd/max77686-private.h>

/* RTCINTM Register */
#define RTCA1M_SHIFT			1
#define RTCA1M_MASK			(1 << RTCA1M_SHIFT)
/* RTCCNTL Register */
#define BCD_EN_SHIFT			0
#define BCD_EN_MASK			(1 << BCD_EN_SHIFT)
#define MODEL24_SHIFT			1
#define MODEL24_MASK			(1 << MODEL24_SHIFT)
/* RTCUPDATE0 Register */
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
/* RTCHOUR register */
#define HOUR_PM_SHIFT			6
#define HOUR_PM_MASK			(1 << HOUR_PM_SHIFT)
/* RTC Alarm Enable */
#define ALARM_ENABLE_SHIFT		7
#define ALARM_ENABLE_MASK		(1 << ALARM_ENABLE_SHIFT)

/* PMIC STATUS2 register */
#define STATUS2_RTCA1_MASK		BIT(2)

#define MAX77686_RTC_UPDATE_DELAY	16

#define WTSR_TIMER_BITS(v)		(((v) << WTSRT_SHIFT) & WTSRT_MASK)
#define SMPL_TIMER_BITS(v)		(((v) << SMPLT_SHIFT) & SMPLT_MASK)

/* RTC Counter Register offsets */
enum rtc_cnt_reg_offset {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WEEKDAY,
	RTC_MONTH,
	RTC_YEAR,
	RTC_DATE,
	NR_RTC_CNT_REGS,
};

struct max77686_rtc_info {
	struct device		*dev;
	struct max77686_dev	*max77686;
	struct i2c_client	*rtc;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;
	int			irq;
	bool			use_irq;
	bool			wtsr_en;
	bool			alarm_enabled;
	u8			update0_reg;
};

enum MAX77686_RTC_OP {
	MAX77686_RTC_WRITE,
	MAX77686_RTC_READ,
};


static void max77686_rtc_data_to_tm(u8 *data, struct rtc_time *tm)
{
	tm->tm_sec = data[RTC_SEC] & 0x7f;
	tm->tm_min = data[RTC_MIN] & 0x7f;
	tm->tm_hour = data[RTC_HOUR] & 0x1f;
	tm->tm_wday = __fls(data[RTC_WEEKDAY] & 0x7f);
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
	data[RTC_YEAR] = tm->tm_year > 100 ? (tm->tm_year - 100) : 0;

	if (tm->tm_year < 100) {
		pr_warn("%s: MAX77686 RTC cannot handle the year %d\n",
				__func__, 1900 + tm->tm_year);
		return -EINVAL;
	}
	return 0;
}

static int max77686_rtc_update(struct max77686_rtc_info *info,
				enum MAX77686_RTC_OP op)
{
	u8 data;
	int ret;

	if (!info || !info->rtc) {
		pr_err("%s: Invalid argument\n", __func__);
		return -EINVAL;
	}

	switch (op) {
	case MAX77686_RTC_WRITE:
		data = RTC_UDR_MASK;
		break;
	case MAX77686_RTC_READ:
		data = RTC_RBUDR_MASK;
		break;
	default:
		dev_err(info->dev, "%s: invalid op(%d)\n", __func__, op);
		return -EINVAL;
	}

	data |= info->update0_reg;

	/* NOTES about UDF and RBUDF(RTCUPDATE1(0x05) register):
	 * If the user read RTCUPDATE1 register when RBUDF or UDF bit of the
	 * register was set to 1 at the same time, the value read from the
	 * register could be 0 and RBUDF or UDF bit would be cleared.
	 * The user should wait for 16msec before initiating new read/write
	 * operation and RTCUPDATE1 register will be erased from the datasheet.
	 */
	ret = max77686_write_reg(info->rtc, MAX77686_RTC_UPDATE0, data);
	if (ret < 0)
		dev_err(info->dev,
			"%s: fail to write update0 reg(ret=%d, data=0x%x)\n",
			__func__, ret, data);
	else
		msleep(MAX77686_RTC_UPDATE_DELAY);

	return ret;
}

static int max77686_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[NR_RTC_CNT_REGS];
	int ret;

	mutex_lock(&info->lock);
	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = max77686_bulk_read(info->rtc, MAX77686_RTC_SEC, NR_RTC_CNT_REGS,
			data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read time reg(%d)\n", __func__,
			ret);
		goto out;
	}

	dev_dbg(info->dev, "%s: %d-%02d-%02d %02d:%02d:%02d(0x%02x)\n",
			__func__, data[RTC_YEAR] + 2000, data[RTC_MONTH],
			data[RTC_DATE], data[RTC_HOUR], data[RTC_MIN],
			data[RTC_SEC], data[RTC_WEEKDAY]);

	max77686_rtc_data_to_tm(data, tm);
	ret = rtc_valid_tm(tm);
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77686_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[NR_RTC_CNT_REGS];
	int ret;

	ret = max77686_rtc_tm_to_data(tm, data);
	if (ret < 0)
		return ret;

	dev_dbg(info->dev, "%s: %d-%02d-%02d %02d:%02d:%02d(0x%02x)\n",
			__func__, data[RTC_YEAR] + 2000, data[RTC_MONTH],
			data[RTC_DATE], data[RTC_HOUR], data[RTC_MIN],
			data[RTC_SEC], data[RTC_WEEKDAY]);

	mutex_lock(&info->lock);
	ret = max77686_bulk_write(info->rtc, MAX77686_RTC_SEC, NR_RTC_CNT_REGS,
			data);
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
	u8 data[NR_RTC_CNT_REGS], val;
	int ret;

	mutex_lock(&info->lock);
	ret = max77686_rtc_update(info, MAX77686_RTC_READ);
	if (ret < 0)
		goto out;

	ret = max77686_bulk_read(info->rtc, MAX77686_ALARM1_SEC,
			NR_RTC_CNT_REGS, data);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read alarm reg(%d)\n",
			__func__, __LINE__, ret);
		goto out;
	}

	max77686_rtc_data_to_tm(data, &alrm->time);

	dev_dbg(info->dev, "%s: %d-%02d-%02d %02d:%02d:%02d(%d)\n", __func__,
			alrm->time.tm_year + 1900, alrm->time.tm_mon + 1,
			alrm->time.tm_mday, alrm->time.tm_hour,
			alrm->time.tm_min, alrm->time.tm_sec,
			alrm->time.tm_wday);

	alrm->enabled = info->alarm_enabled;
	alrm->pending = 0;
	ret = max77686_read_reg(info->max77686->i2c, MAX77686_REG_STATUS2,
			&val);
	if (ret < 0) {
		dev_err(info->dev, "%s:%d fail to read status1 reg(%d)\n",
			__func__, __LINE__, ret);
		goto out;
	}
	if (val & STATUS2_RTCA1_MASK)
		alrm->pending = 1;
out:
	mutex_unlock(&info->lock);
	return ret;
}

static int max77686_rtc_set_alarm_enable(struct max77686_rtc_info *info,
					 bool enabled)
{
	if (!info->use_irq)
		return -EPERM;

	if (enabled && !info->alarm_enabled) {
		info->alarm_enabled = true;
		enable_irq(info->irq);
	} else if (!enabled && info->alarm_enabled) {
		info->alarm_enabled = false;
		disable_irq(info->irq);
	}
	return 0;
}

static int max77686_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct max77686_rtc_info *info = dev_get_drvdata(dev);
	u8 data[NR_RTC_CNT_REGS];
	int ret, i;

	mutex_lock(&info->lock);
	ret = max77686_rtc_tm_to_data(&alrm->time, data);
	if (ret < 0)
		goto out;

	dev_dbg(info->dev, "%s: %d-%02d-%02d %02d:%02d:%02d(0x%02x)\n",
			__func__, data[RTC_YEAR] + 2000, data[RTC_MONTH],
			data[RTC_DATE], data[RTC_HOUR], data[RTC_MIN],
			data[RTC_SEC], data[RTC_WEEKDAY]);

	for (i = 0; i < NR_RTC_CNT_REGS; i++)
		data[i] |= ALARM_ENABLE_MASK;

	ret = max77686_bulk_write(info->rtc, MAX77686_ALARM1_SEC,
			NR_RTC_CNT_REGS, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write alarm reg(%d)\n",
			__func__, ret);
		goto out;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
	if (ret < 0)
		goto out;

	ret = max77686_rtc_set_alarm_enable(info, alrm->enabled);
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
	ret = max77686_rtc_set_alarm_enable(info, enabled);
	mutex_unlock(&info->lock);
	return ret;
}

static irqreturn_t max77686_rtc_alarm_irq(int irq, void *data)
{
	struct max77686_rtc_info *info = data;

	if (!info->rtc_dev)
		return IRQ_HANDLED;

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

static void __devinit
max77686_rtc_enable_wtsr_smpl(struct max77686_rtc_info *info,
			      struct max77686_platform_data *pdata)
{
	u8 val;
	int ret;

	val = (pdata->wtsr_smpl->wtsr_en << WTSR_EN_SHIFT)
		| (pdata->wtsr_smpl->smpl_en << SMPL_EN_SHIFT)
		| WTSR_TIMER_BITS(pdata->wtsr_smpl->wtsr_timer_val)
		| SMPL_TIMER_BITS(pdata->wtsr_smpl->smpl_timer_val);

	dev_info(info->dev, "%s: WTSR: %s, SMPL: %s\n", __func__,
			pdata->wtsr_smpl->wtsr_en ? "enable" : "disable",
			pdata->wtsr_smpl->smpl_en ? "enable" : "disable");

	ret = max77686_write_reg(info->rtc, MAX77686_WTSR_SMPL_CNTL, val);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write WTSR/SMPL reg(%d)\n",
				__func__, ret);
		return;
	}
	info->wtsr_en = pdata->wtsr_smpl->wtsr_en;

	max77686_rtc_update(info, MAX77686_RTC_WRITE);
}

static void max77686_rtc_disable_wtsr(struct max77686_rtc_info *info)
{
	int ret;

	dev_info(info->dev, "%s: disable WTSR\n", __func__);
	max77686_rtc_update(info, MAX77686_RTC_READ);
	ret = max77686_update_reg(info->rtc, MAX77686_WTSR_SMPL_CNTL, 0,
			WTSR_EN_MASK);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to update WTSR reg(%d)\n",
				__func__, ret);
		return;
	}
	max77686_rtc_update(info, MAX77686_RTC_WRITE);
}

static int __devinit max77686_rtc_init_reg(struct max77686_rtc_info *info,
					struct max77686_platform_data *pdata)
{
	u8 data[2], update0, cntl;
	int ret;

	max77686_rtc_update(info, MAX77686_RTC_READ);

	ret = max77686_read_reg(info->rtc, MAX77686_RTC_CONTROL, &cntl);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read control reg(%d)\n",
			__func__, ret);
		return ret;
	}

	ret = max77686_read_reg(info->rtc, MAX77686_RTC_UPDATE0, &update0);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to read update0 reg(%d)\n",
			__func__, ret);
		return ret;
	}
	info->update0_reg = update0 & ~(RTC_UDR_MASK | RTC_RBUDR_MASK);

	/* If the value of CONTROL register is 0, RTC registers were reset */
	if (cntl == MODEL24_MASK)
		return 0;

	/* Set RTC control register : Binary mode, 24hour mode */
	data[0] = BCD_EN_MASK | MODEL24_MASK;
	data[1] = MODEL24_MASK;

	ret = max77686_bulk_write(info->rtc, MAX77686_RTC_CONTROLM, 2, data);
	if (ret < 0) {
		dev_err(info->dev, "%s: fail to write controlm reg(%d)\n",
			__func__, ret);
		return ret;
	}

	ret = max77686_rtc_update(info, MAX77686_RTC_WRITE);
	if (ret < 0)
		return ret;

	if (pdata->init_time) {
		dev_info(info->dev, "%s: initialize RTC time\n", __func__);
		ret = max77686_rtc_set_time(info->dev, pdata->init_time);
	}
	return ret;
}

static int __devinit max77686_rtc_probe(struct platform_device *pdev)
{
	struct max77686_dev *max77686 = dev_get_drvdata(pdev->dev.parent);
	struct max77686_platform_data *pdata = dev_get_platdata(max77686->dev);
	struct max77686_rtc_info *info;
	int ret;

	if (!pdata) {
		dev_err(pdev->dev.parent, "RTC: No platform data supplied.\n");
		return -ENODEV;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;
	info->max77686 = max77686;
	info->rtc = max77686->rtc;
	info->irq = max77686->irq_base + MAX77686_RTCIRQ_RTCA1;

	platform_set_drvdata(pdev, info);

	ret = max77686_rtc_init_reg(info, pdata);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to initialize RTC reg:%d\n", ret);
		goto err_init_reg;
	}

	if (pdata->wtsr_smpl)
		max77686_rtc_enable_wtsr_smpl(info, pdata);

	device_init_wakeup(&pdev->dev, 1);

	ret = request_threaded_irq(info->irq, NULL, max77686_rtc_alarm_irq, 0,
				   "rtc-alarm0", info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			info->irq, ret);
		goto err_request_irq;
	}
	disable_irq(info->irq);
	disable_irq(info->irq);
	info->use_irq = true;

	info->rtc_dev = rtc_device_register("max77686-rtc", &pdev->dev,
					    &max77686_rtc_ops, THIS_MODULE);
	if (IS_ERR(info->rtc_dev)) {
		ret = PTR_ERR(info->rtc_dev);
		dev_err(&pdev->dev, "Failed to register RTC device: %d\n", ret);
		if (!ret)
			ret = -EINVAL;
		goto err_rtc_dev_register;
	}
	enable_irq(info->irq);
	return 0;

err_rtc_dev_register:
	enable_irq(info->irq);
	enable_irq(info->irq);
	free_irq(info->irq, info);
err_request_irq:
err_init_reg:
	kfree(info);
	return ret;
}

static int __devexit max77686_rtc_remove(struct platform_device *pdev)
{
	struct max77686_rtc_info *info = platform_get_drvdata(pdev);

	if (!info->alarm_enabled)
		enable_irq(info->irq);

	free_irq(info->irq, info);
	rtc_device_unregister(info->rtc_dev);
	kfree(info);

	return 0;
}

static void max77686_rtc_shutdown(struct platform_device *pdev)
{
	struct max77686_rtc_info *info = platform_get_drvdata(pdev);

	if (info->wtsr_en)
		max77686_rtc_disable_wtsr(info);
}

static const struct platform_device_id rtc_id[] = {
	{"max77686-rtc", 0},
	{},
};

static struct platform_driver max77686_rtc_driver = {
	.driver = {
		.name	= "max77686-rtc",
		.owner	= THIS_MODULE,
	},
	.probe		= max77686_rtc_probe,
	.remove		= __devexit_p(max77686_rtc_remove),
	.shutdown	= max77686_rtc_shutdown,
	.id_table	= rtc_id,
};

static int __init max77686_rtc_init(void)
{
	return platform_driver_register(&max77686_rtc_driver);
}

module_init(max77686_rtc_init);

static void __exit max77686_rtc_exit(void)
{
	platform_driver_unregister(&max77686_rtc_driver);
}

module_exit(max77686_rtc_exit);

MODULE_DESCRIPTION("Maxim MAX77686 RTC driver");
MODULE_AUTHOR("<ms925.kim@samsung.com>");
MODULE_LICENSE("GPL");
