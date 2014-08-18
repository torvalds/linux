/*
 * drivers/rtc/rtc-vt8500.c
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * Based on rtc-pxa.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of.h>

/*
 * Register definitions
 */
#define VT8500_RTC_TS		0x00	/* Time set */
#define VT8500_RTC_DS		0x04	/* Date set */
#define VT8500_RTC_AS		0x08	/* Alarm set */
#define VT8500_RTC_CR		0x0c	/* Control */
#define VT8500_RTC_TR		0x10	/* Time read */
#define VT8500_RTC_DR		0x14	/* Date read */
#define VT8500_RTC_WS		0x18	/* Write status */
#define VT8500_RTC_CL		0x20	/* Calibration */
#define VT8500_RTC_IS		0x24	/* Interrupt status */
#define VT8500_RTC_ST		0x28	/* Status */

#define INVALID_TIME_BIT	(1 << 31)

#define DATE_CENTURY_S		19
#define DATE_YEAR_S		11
#define DATE_YEAR_MASK		(0xff << DATE_YEAR_S)
#define DATE_MONTH_S		6
#define DATE_MONTH_MASK		(0x1f << DATE_MONTH_S)
#define DATE_DAY_MASK		0x3f

#define TIME_DOW_S		20
#define TIME_DOW_MASK		(0x07 << TIME_DOW_S)
#define TIME_HOUR_S		14
#define TIME_HOUR_MASK		(0x3f << TIME_HOUR_S)
#define TIME_MIN_S		7
#define TIME_MIN_MASK		(0x7f << TIME_MIN_S)
#define TIME_SEC_MASK		0x7f

#define ALARM_DAY_S		20
#define ALARM_DAY_MASK		(0x3f << ALARM_DAY_S)

#define ALARM_DAY_BIT		(1 << 29)
#define ALARM_HOUR_BIT		(1 << 28)
#define ALARM_MIN_BIT		(1 << 27)
#define ALARM_SEC_BIT		(1 << 26)

#define ALARM_ENABLE_MASK	(ALARM_DAY_BIT \
				| ALARM_HOUR_BIT \
				| ALARM_MIN_BIT \
				| ALARM_SEC_BIT)

#define VT8500_RTC_CR_ENABLE	(1 << 0)	/* Enable RTC */
#define VT8500_RTC_CR_12H	(1 << 1)	/* 12h time format */
#define VT8500_RTC_CR_SM_ENABLE	(1 << 2)	/* Enable periodic irqs */
#define VT8500_RTC_CR_SM_SEC	(1 << 3)	/* 0: 1Hz/60, 1: 1Hz */
#define VT8500_RTC_CR_CALIB	(1 << 4)	/* Enable calibration */

#define VT8500_RTC_IS_ALARM	(1 << 0)	/* Alarm interrupt status */

struct vt8500_rtc {
	void __iomem		*regbase;
	int			irq_alarm;
	struct rtc_device	*rtc;
	spinlock_t		lock;		/* Protects this structure */
};

static irqreturn_t vt8500_rtc_irq(int irq, void *dev_id)
{
	struct vt8500_rtc *vt8500_rtc = dev_id;
	u32 isr;
	unsigned long events = 0;

	spin_lock(&vt8500_rtc->lock);

	/* clear interrupt sources */
	isr = readl(vt8500_rtc->regbase + VT8500_RTC_IS);
	writel(isr, vt8500_rtc->regbase + VT8500_RTC_IS);

	spin_unlock(&vt8500_rtc->lock);

	if (isr & VT8500_RTC_IS_ALARM)
		events |= RTC_AF | RTC_IRQF;

	rtc_update_irq(vt8500_rtc->rtc, 1, events);

	return IRQ_HANDLED;
}

static int vt8500_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct vt8500_rtc *vt8500_rtc = dev_get_drvdata(dev);
	u32 date, time;

	date = readl(vt8500_rtc->regbase + VT8500_RTC_DR);
	time = readl(vt8500_rtc->regbase + VT8500_RTC_TR);

	tm->tm_sec = bcd2bin(time & TIME_SEC_MASK);
	tm->tm_min = bcd2bin((time & TIME_MIN_MASK) >> TIME_MIN_S);
	tm->tm_hour = bcd2bin((time & TIME_HOUR_MASK) >> TIME_HOUR_S);
	tm->tm_mday = bcd2bin(date & DATE_DAY_MASK);
	tm->tm_mon = bcd2bin((date & DATE_MONTH_MASK) >> DATE_MONTH_S) - 1;
	tm->tm_year = bcd2bin((date & DATE_YEAR_MASK) >> DATE_YEAR_S)
			+ ((date >> DATE_CENTURY_S) & 1 ? 200 : 100);
	tm->tm_wday = (time & TIME_DOW_MASK) >> TIME_DOW_S;

	return 0;
}

static int vt8500_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct vt8500_rtc *vt8500_rtc = dev_get_drvdata(dev);

	if (tm->tm_year < 100) {
		dev_warn(dev, "Only years 2000-2199 are supported by the "
			      "hardware!\n");
		return -EINVAL;
	}

	writel((bin2bcd(tm->tm_year % 100) << DATE_YEAR_S)
		| (bin2bcd(tm->tm_mon + 1) << DATE_MONTH_S)
		| (bin2bcd(tm->tm_mday))
		| ((tm->tm_year >= 200) << DATE_CENTURY_S),
		vt8500_rtc->regbase + VT8500_RTC_DS);
	writel((bin2bcd(tm->tm_wday) << TIME_DOW_S)
		| (bin2bcd(tm->tm_hour) << TIME_HOUR_S)
		| (bin2bcd(tm->tm_min) << TIME_MIN_S)
		| (bin2bcd(tm->tm_sec)),
		vt8500_rtc->regbase + VT8500_RTC_TS);

	return 0;
}

static int vt8500_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct vt8500_rtc *vt8500_rtc = dev_get_drvdata(dev);
	u32 isr, alarm;

	alarm = readl(vt8500_rtc->regbase + VT8500_RTC_AS);
	isr = readl(vt8500_rtc->regbase + VT8500_RTC_IS);

	alrm->time.tm_mday = bcd2bin((alarm & ALARM_DAY_MASK) >> ALARM_DAY_S);
	alrm->time.tm_hour = bcd2bin((alarm & TIME_HOUR_MASK) >> TIME_HOUR_S);
	alrm->time.tm_min = bcd2bin((alarm & TIME_MIN_MASK) >> TIME_MIN_S);
	alrm->time.tm_sec = bcd2bin((alarm & TIME_SEC_MASK));

	alrm->enabled = (alarm & ALARM_ENABLE_MASK) ? 1 : 0;
	alrm->pending = (isr & VT8500_RTC_IS_ALARM) ? 1 : 0;

	return rtc_valid_tm(&alrm->time);
}

static int vt8500_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct vt8500_rtc *vt8500_rtc = dev_get_drvdata(dev);

	writel((alrm->enabled ? ALARM_ENABLE_MASK : 0)
		| (bin2bcd(alrm->time.tm_mday) << ALARM_DAY_S)
		| (bin2bcd(alrm->time.tm_hour) << TIME_HOUR_S)
		| (bin2bcd(alrm->time.tm_min) << TIME_MIN_S)
		| (bin2bcd(alrm->time.tm_sec)),
		vt8500_rtc->regbase + VT8500_RTC_AS);

	return 0;
}

static int vt8500_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct vt8500_rtc *vt8500_rtc = dev_get_drvdata(dev);
	unsigned long tmp = readl(vt8500_rtc->regbase + VT8500_RTC_AS);

	if (enabled)
		tmp |= ALARM_ENABLE_MASK;
	else
		tmp &= ~ALARM_ENABLE_MASK;

	writel(tmp, vt8500_rtc->regbase + VT8500_RTC_AS);
	return 0;
}

static const struct rtc_class_ops vt8500_rtc_ops = {
	.read_time = vt8500_rtc_read_time,
	.set_time = vt8500_rtc_set_time,
	.read_alarm = vt8500_rtc_read_alarm,
	.set_alarm = vt8500_rtc_set_alarm,
	.alarm_irq_enable = vt8500_alarm_irq_enable,
};

static int vt8500_rtc_probe(struct platform_device *pdev)
{
	struct vt8500_rtc *vt8500_rtc;
	struct resource	*res;
	int ret;

	vt8500_rtc = devm_kzalloc(&pdev->dev,
			   sizeof(struct vt8500_rtc), GFP_KERNEL);
	if (!vt8500_rtc)
		return -ENOMEM;

	spin_lock_init(&vt8500_rtc->lock);
	platform_set_drvdata(pdev, vt8500_rtc);

	vt8500_rtc->irq_alarm = platform_get_irq(pdev, 0);
	if (vt8500_rtc->irq_alarm < 0) {
		dev_err(&pdev->dev, "No alarm IRQ resource defined\n");
		return vt8500_rtc->irq_alarm;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vt8500_rtc->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(vt8500_rtc->regbase))
		return PTR_ERR(vt8500_rtc->regbase);

	/* Enable RTC and set it to 24-hour mode */
	writel(VT8500_RTC_CR_ENABLE,
	       vt8500_rtc->regbase + VT8500_RTC_CR);

	vt8500_rtc->rtc = devm_rtc_device_register(&pdev->dev, "vt8500-rtc",
					      &vt8500_rtc_ops, THIS_MODULE);
	if (IS_ERR(vt8500_rtc->rtc)) {
		ret = PTR_ERR(vt8500_rtc->rtc);
		dev_err(&pdev->dev,
			"Failed to register RTC device -> %d\n", ret);
		goto err_return;
	}

	ret = devm_request_irq(&pdev->dev, vt8500_rtc->irq_alarm,
				vt8500_rtc_irq, 0, "rtc alarm", vt8500_rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "can't get irq %i, err %d\n",
			vt8500_rtc->irq_alarm, ret);
		goto err_return;
	}

	return 0;

err_return:
	return ret;
}

static int vt8500_rtc_remove(struct platform_device *pdev)
{
	struct vt8500_rtc *vt8500_rtc = platform_get_drvdata(pdev);

	/* Disable alarm matching */
	writel(0, vt8500_rtc->regbase + VT8500_RTC_IS);

	return 0;
}

static const struct of_device_id wmt_dt_ids[] = {
	{ .compatible = "via,vt8500-rtc", },
	{}
};

static struct platform_driver vt8500_rtc_driver = {
	.probe		= vt8500_rtc_probe,
	.remove		= vt8500_rtc_remove,
	.driver		= {
		.name	= "vt8500-rtc",
		.owner	= THIS_MODULE,
		.of_match_table = wmt_dt_ids,
	},
};

module_platform_driver(vt8500_rtc_driver);

MODULE_AUTHOR("Alexey Charkov <alchark@gmail.com>");
MODULE_DESCRIPTION("VIA VT8500 SoC Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:vt8500-rtc");
