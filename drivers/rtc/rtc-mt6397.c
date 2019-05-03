/*
* Copyright (c) 2014-2015 MediaTek Inc.
* Author: Tianping.Fang <tianping.fang@mediatek.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/irqdomain.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/mfd/mt6397/core.h>

#define RTC_BBPU		0x0000
#define RTC_BBPU_CBUSY		BIT(6)

#define RTC_WRTGR		0x003c

#define RTC_IRQ_STA		0x0002
#define RTC_IRQ_STA_AL		BIT(0)
#define RTC_IRQ_STA_LP		BIT(3)

#define RTC_IRQ_EN		0x0004
#define RTC_IRQ_EN_AL		BIT(0)
#define RTC_IRQ_EN_ONESHOT	BIT(2)
#define RTC_IRQ_EN_LP		BIT(3)
#define RTC_IRQ_EN_ONESHOT_AL	(RTC_IRQ_EN_ONESHOT | RTC_IRQ_EN_AL)

#define RTC_AL_MASK		0x0008
#define RTC_AL_MASK_DOW		BIT(4)

#define RTC_TC_SEC		0x000a
/* Min, Hour, Dom... register offset to RTC_TC_SEC */
#define RTC_OFFSET_SEC		0
#define RTC_OFFSET_MIN		1
#define RTC_OFFSET_HOUR		2
#define RTC_OFFSET_DOM		3
#define RTC_OFFSET_DOW		4
#define RTC_OFFSET_MTH		5
#define RTC_OFFSET_YEAR		6
#define RTC_OFFSET_COUNT	7

#define RTC_AL_SEC		0x0018

#define RTC_PDN2		0x002e
#define RTC_PDN2_PWRON_ALARM	BIT(4)

#define RTC_MIN_YEAR		1968
#define RTC_BASE_YEAR		1900
#define RTC_NUM_YEARS		128
#define RTC_MIN_YEAR_OFFSET	(RTC_MIN_YEAR - RTC_BASE_YEAR)

struct mt6397_rtc {
	struct device		*dev;
	struct rtc_device	*rtc_dev;
	struct mutex		lock;
	struct regmap		*regmap;
	int			irq;
	u32			addr_base;
};

static int mtk_rtc_write_trigger(struct mt6397_rtc *rtc)
{
	unsigned long timeout = jiffies + HZ;
	int ret;
	u32 data;

	ret = regmap_write(rtc->regmap, rtc->addr_base + RTC_WRTGR, 1);
	if (ret < 0)
		return ret;

	while (1) {
		ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_BBPU,
				  &data);
		if (ret < 0)
			break;
		if (!(data & RTC_BBPU_CBUSY))
			break;
		if (time_after(jiffies, timeout)) {
			ret = -ETIMEDOUT;
			break;
		}
		cpu_relax();
	}

	return ret;
}

static irqreturn_t mtk_rtc_irq_handler_thread(int irq, void *data)
{
	struct mt6397_rtc *rtc = data;
	u32 irqsta, irqen;
	int ret;

	ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_IRQ_STA, &irqsta);
	if ((ret >= 0) && (irqsta & RTC_IRQ_STA_AL)) {
		rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_AF);
		irqen = irqsta & ~RTC_IRQ_EN_AL;
		mutex_lock(&rtc->lock);
		if (regmap_write(rtc->regmap, rtc->addr_base + RTC_IRQ_EN,
				 irqen) < 0)
			mtk_rtc_write_trigger(rtc);
		mutex_unlock(&rtc->lock);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int __mtk_rtc_read_time(struct mt6397_rtc *rtc,
			       struct rtc_time *tm, int *sec)
{
	int ret;
	u16 data[RTC_OFFSET_COUNT];

	mutex_lock(&rtc->lock);
	ret = regmap_bulk_read(rtc->regmap, rtc->addr_base + RTC_TC_SEC,
			       data, RTC_OFFSET_COUNT);
	if (ret < 0)
		goto exit;

	tm->tm_sec = data[RTC_OFFSET_SEC];
	tm->tm_min = data[RTC_OFFSET_MIN];
	tm->tm_hour = data[RTC_OFFSET_HOUR];
	tm->tm_mday = data[RTC_OFFSET_DOM];
	tm->tm_mon = data[RTC_OFFSET_MTH];
	tm->tm_year = data[RTC_OFFSET_YEAR];

	ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_TC_SEC, sec);
exit:
	mutex_unlock(&rtc->lock);
	return ret;
}

static int mtk_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	time64_t time;
	struct mt6397_rtc *rtc = dev_get_drvdata(dev);
	int days, sec, ret;

	do {
		ret = __mtk_rtc_read_time(rtc, tm, &sec);
		if (ret < 0)
			goto exit;
	} while (sec < tm->tm_sec);

	/* HW register use 7 bits to store year data, minus
	 * RTC_MIN_YEAR_OFFSET before write year data to register, and plus
	 * RTC_MIN_YEAR_OFFSET back after read year from register
	 */
	tm->tm_year += RTC_MIN_YEAR_OFFSET;

	/* HW register start mon from one, but tm_mon start from zero. */
	tm->tm_mon--;
	time = rtc_tm_to_time64(tm);

	/* rtc_tm_to_time64 covert Gregorian date to seconds since
	 * 01-01-1970 00:00:00, and this date is Thursday.
	 */
	days = div_s64(time, 86400);
	tm->tm_wday = (days + 4) % 7;

exit:
	return ret;
}

static int mtk_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct mt6397_rtc *rtc = dev_get_drvdata(dev);
	int ret;
	u16 data[RTC_OFFSET_COUNT];

	tm->tm_year -= RTC_MIN_YEAR_OFFSET;
	tm->tm_mon++;

	data[RTC_OFFSET_SEC] = tm->tm_sec;
	data[RTC_OFFSET_MIN] = tm->tm_min;
	data[RTC_OFFSET_HOUR] = tm->tm_hour;
	data[RTC_OFFSET_DOM] = tm->tm_mday;
	data[RTC_OFFSET_MTH] = tm->tm_mon;
	data[RTC_OFFSET_YEAR] = tm->tm_year;

	mutex_lock(&rtc->lock);
	ret = regmap_bulk_write(rtc->regmap, rtc->addr_base + RTC_TC_SEC,
				data, RTC_OFFSET_COUNT);
	if (ret < 0)
		goto exit;

	/* Time register write to hardware after call trigger function */
	ret = mtk_rtc_write_trigger(rtc);

exit:
	mutex_unlock(&rtc->lock);
	return ret;
}

static int mtk_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rtc_time *tm = &alm->time;
	struct mt6397_rtc *rtc = dev_get_drvdata(dev);
	u32 irqen, pdn2;
	int ret;
	u16 data[RTC_OFFSET_COUNT];

	mutex_lock(&rtc->lock);
	ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_IRQ_EN, &irqen);
	if (ret < 0)
		goto err_exit;
	ret = regmap_read(rtc->regmap, rtc->addr_base + RTC_PDN2, &pdn2);
	if (ret < 0)
		goto err_exit;

	ret = regmap_bulk_read(rtc->regmap, rtc->addr_base + RTC_AL_SEC,
			       data, RTC_OFFSET_COUNT);
	if (ret < 0)
		goto err_exit;

	alm->enabled = !!(irqen & RTC_IRQ_EN_AL);
	alm->pending = !!(pdn2 & RTC_PDN2_PWRON_ALARM);
	mutex_unlock(&rtc->lock);

	tm->tm_sec = data[RTC_OFFSET_SEC];
	tm->tm_min = data[RTC_OFFSET_MIN];
	tm->tm_hour = data[RTC_OFFSET_HOUR];
	tm->tm_mday = data[RTC_OFFSET_DOM];
	tm->tm_mon = data[RTC_OFFSET_MTH];
	tm->tm_year = data[RTC_OFFSET_YEAR];

	tm->tm_year += RTC_MIN_YEAR_OFFSET;
	tm->tm_mon--;

	return 0;
err_exit:
	mutex_unlock(&rtc->lock);
	return ret;
}

static int mtk_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rtc_time *tm = &alm->time;
	struct mt6397_rtc *rtc = dev_get_drvdata(dev);
	int ret;
	u16 data[RTC_OFFSET_COUNT];

	tm->tm_year -= RTC_MIN_YEAR_OFFSET;
	tm->tm_mon++;

	data[RTC_OFFSET_SEC] = tm->tm_sec;
	data[RTC_OFFSET_MIN] = tm->tm_min;
	data[RTC_OFFSET_HOUR] = tm->tm_hour;
	data[RTC_OFFSET_DOM] = tm->tm_mday;
	data[RTC_OFFSET_MTH] = tm->tm_mon;
	data[RTC_OFFSET_YEAR] = tm->tm_year;

	mutex_lock(&rtc->lock);
	if (alm->enabled) {
		ret = regmap_bulk_write(rtc->regmap,
					rtc->addr_base + RTC_AL_SEC,
					data, RTC_OFFSET_COUNT);
		if (ret < 0)
			goto exit;
		ret = regmap_write(rtc->regmap, rtc->addr_base + RTC_AL_MASK,
				   RTC_AL_MASK_DOW);
		if (ret < 0)
			goto exit;
		ret = regmap_update_bits(rtc->regmap,
					 rtc->addr_base + RTC_IRQ_EN,
					 RTC_IRQ_EN_ONESHOT_AL,
					 RTC_IRQ_EN_ONESHOT_AL);
		if (ret < 0)
			goto exit;
	} else {
		ret = regmap_update_bits(rtc->regmap,
					 rtc->addr_base + RTC_IRQ_EN,
					 RTC_IRQ_EN_ONESHOT_AL, 0);
		if (ret < 0)
			goto exit;
	}

	/* All alarm time register write to hardware after calling
	 * mtk_rtc_write_trigger. This can avoid race condition if alarm
	 * occur happen during writing alarm time register.
	 */
	ret = mtk_rtc_write_trigger(rtc);
exit:
	mutex_unlock(&rtc->lock);
	return ret;
}

static const struct rtc_class_ops mtk_rtc_ops = {
	.read_time  = mtk_rtc_read_time,
	.set_time   = mtk_rtc_set_time,
	.read_alarm = mtk_rtc_read_alarm,
	.set_alarm  = mtk_rtc_set_alarm,
};

static int mtk_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mt6397_chip *mt6397_chip = dev_get_drvdata(pdev->dev.parent);
	struct mt6397_rtc *rtc;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct mt6397_rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rtc->addr_base = res->start;

	rtc->irq = platform_get_irq(pdev, 0);
	if (rtc->irq < 0)
		return rtc->irq;

	rtc->regmap = mt6397_chip->regmap;
	rtc->dev = &pdev->dev;
	mutex_init(&rtc->lock);

	platform_set_drvdata(pdev, rtc);

	rtc->rtc_dev = devm_rtc_allocate_device(rtc->dev);
	if (IS_ERR(rtc->rtc_dev))
		return PTR_ERR(rtc->rtc_dev);

	ret = request_threaded_irq(rtc->irq, NULL,
				   mtk_rtc_irq_handler_thread,
				   IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
				   "mt6397-rtc", rtc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ: %d: %d\n",
			rtc->irq, ret);
		goto out_dispose_irq;
	}

	device_init_wakeup(&pdev->dev, 1);

	rtc->rtc_dev->ops = &mtk_rtc_ops;

	ret = rtc_register_device(rtc->rtc_dev);
	if (ret) {
		dev_err(&pdev->dev, "register rtc device failed\n");
		goto out_free_irq;
	}

	return 0;

out_free_irq:
	free_irq(rtc->irq, rtc->rtc_dev);
out_dispose_irq:
	irq_dispose_mapping(rtc->irq);
	return ret;
}

static int mtk_rtc_remove(struct platform_device *pdev)
{
	struct mt6397_rtc *rtc = platform_get_drvdata(pdev);

	free_irq(rtc->irq, rtc->rtc_dev);
	irq_dispose_mapping(rtc->irq);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mt6397_rtc_suspend(struct device *dev)
{
	struct mt6397_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->irq);

	return 0;
}

static int mt6397_rtc_resume(struct device *dev)
{
	struct mt6397_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mt6397_pm_ops, mt6397_rtc_suspend,
			mt6397_rtc_resume);

static const struct of_device_id mt6397_rtc_of_match[] = {
	{ .compatible = "mediatek,mt6397-rtc", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6397_rtc_of_match);

static struct platform_driver mtk_rtc_driver = {
	.driver = {
		.name = "mt6397-rtc",
		.of_match_table = mt6397_rtc_of_match,
		.pm = &mt6397_pm_ops,
	},
	.probe	= mtk_rtc_probe,
	.remove = mtk_rtc_remove,
};

module_platform_driver(mtk_rtc_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Tianping Fang <tianping.fang@mediatek.com>");
MODULE_DESCRIPTION("RTC Driver for MediaTek MT6397 PMIC");
