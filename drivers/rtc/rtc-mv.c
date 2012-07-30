/*
 * Driver for the RTC in Marvell SoCs.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/module.h>


#define RTC_TIME_REG_OFFS	0
#define RTC_SECONDS_OFFS	0
#define RTC_MINUTES_OFFS	8
#define RTC_HOURS_OFFS		16
#define RTC_WDAY_OFFS		24
#define RTC_HOURS_12H_MODE		(1 << 22) /* 12 hours mode */

#define RTC_DATE_REG_OFFS	4
#define RTC_MDAY_OFFS		0
#define RTC_MONTH_OFFS		8
#define RTC_YEAR_OFFS		16

#define RTC_ALARM_TIME_REG_OFFS	8
#define RTC_ALARM_DATE_REG_OFFS	0xc
#define RTC_ALARM_VALID		(1 << 7)

#define RTC_ALARM_INTERRUPT_MASK_REG_OFFS	0x10
#define RTC_ALARM_INTERRUPT_CASUE_REG_OFFS	0x14

struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
	int		irq;
};

static int mv_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32	rtc_reg;

	rtc_reg = (bin2bcd(tm->tm_sec) << RTC_SECONDS_OFFS) |
		(bin2bcd(tm->tm_min) << RTC_MINUTES_OFFS) |
		(bin2bcd(tm->tm_hour) << RTC_HOURS_OFFS) |
		(bin2bcd(tm->tm_wday) << RTC_WDAY_OFFS);
	writel(rtc_reg, ioaddr + RTC_TIME_REG_OFFS);

	rtc_reg = (bin2bcd(tm->tm_mday) << RTC_MDAY_OFFS) |
		(bin2bcd(tm->tm_mon + 1) << RTC_MONTH_OFFS) |
		(bin2bcd(tm->tm_year % 100) << RTC_YEAR_OFFS);
	writel(rtc_reg, ioaddr + RTC_DATE_REG_OFFS);

	return 0;
}

static int mv_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32	rtc_time, rtc_date;
	unsigned int year, month, day, hour, minute, second, wday;

	rtc_time = readl(ioaddr + RTC_TIME_REG_OFFS);
	rtc_date = readl(ioaddr + RTC_DATE_REG_OFFS);

	second = rtc_time & 0x7f;
	minute = (rtc_time >> RTC_MINUTES_OFFS) & 0x7f;
	hour = (rtc_time >> RTC_HOURS_OFFS) & 0x3f; /* assume 24 hours mode */
	wday = (rtc_time >> RTC_WDAY_OFFS) & 0x7;

	day = rtc_date & 0x3f;
	month = (rtc_date >> RTC_MONTH_OFFS) & 0x3f;
	year = (rtc_date >> RTC_YEAR_OFFS) & 0xff;

	tm->tm_sec = bcd2bin(second);
	tm->tm_min = bcd2bin(minute);
	tm->tm_hour = bcd2bin(hour);
	tm->tm_mday = bcd2bin(day);
	tm->tm_wday = bcd2bin(wday);
	tm->tm_mon = bcd2bin(month) - 1;
	/* hw counts from year 2000, but tm_year is relative to 1900 */
	tm->tm_year = bcd2bin(year) + 100;

	return rtc_valid_tm(tm);
}

static int mv_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32	rtc_time, rtc_date;
	unsigned int year, month, day, hour, minute, second, wday;

	rtc_time = readl(ioaddr + RTC_ALARM_TIME_REG_OFFS);
	rtc_date = readl(ioaddr + RTC_ALARM_DATE_REG_OFFS);

	second = rtc_time & 0x7f;
	minute = (rtc_time >> RTC_MINUTES_OFFS) & 0x7f;
	hour = (rtc_time >> RTC_HOURS_OFFS) & 0x3f; /* assume 24 hours mode */
	wday = (rtc_time >> RTC_WDAY_OFFS) & 0x7;

	day = rtc_date & 0x3f;
	month = (rtc_date >> RTC_MONTH_OFFS) & 0x3f;
	year = (rtc_date >> RTC_YEAR_OFFS) & 0xff;

	alm->time.tm_sec = bcd2bin(second);
	alm->time.tm_min = bcd2bin(minute);
	alm->time.tm_hour = bcd2bin(hour);
	alm->time.tm_mday = bcd2bin(day);
	alm->time.tm_wday = bcd2bin(wday);
	alm->time.tm_mon = bcd2bin(month) - 1;
	/* hw counts from year 2000, but tm_year is relative to 1900 */
	alm->time.tm_year = bcd2bin(year) + 100;

	if (rtc_valid_tm(&alm->time) < 0) {
		dev_err(dev, "retrieved alarm date/time is not valid.\n");
		rtc_time_to_tm(0, &alm->time);
	}

	alm->enabled = !!readl(ioaddr + RTC_ALARM_INTERRUPT_MASK_REG_OFFS);
	return 0;
}

static int mv_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rtc_plat_data *pdata = dev_get_drvdata(dev);
	void __iomem *ioaddr = pdata->ioaddr;
	u32 rtc_reg = 0;

	if (alm->time.tm_sec >= 0)
		rtc_reg |= (RTC_ALARM_VALID | bin2bcd(alm->time.tm_sec))
			<< RTC_SECONDS_OFFS;
	if (alm->time.tm_min >= 0)
		rtc_reg |= (RTC_ALARM_VALID | bin2bcd(alm->time.tm_min))
			<< RTC_MINUTES_OFFS;
	if (alm->time.tm_hour >= 0)
		rtc_reg |= (RTC_ALARM_VALID | bin2bcd(alm->time.tm_hour))
			<< RTC_HOURS_OFFS;

	writel(rtc_reg, ioaddr + RTC_ALARM_TIME_REG_OFFS);

	if (alm->time.tm_mday >= 0)
		rtc_reg = (RTC_ALARM_VALID | bin2bcd(alm->time.tm_mday))
			<< RTC_MDAY_OFFS;
	else
		rtc_reg = 0;

	if (alm->time.tm_mon >= 0)
		rtc_reg |= (RTC_ALARM_VALID | bin2bcd(alm->time.tm_mon + 1))
			<< RTC_MONTH_OFFS;

	if (alm->time.tm_year >= 0)
		rtc_reg |= (RTC_ALARM_VALID | bin2bcd(alm->time.tm_year % 100))
			<< RTC_YEAR_OFFS;

	writel(rtc_reg, ioaddr + RTC_ALARM_DATE_REG_OFFS);
	writel(0, ioaddr + RTC_ALARM_INTERRUPT_CASUE_REG_OFFS);
	writel(alm->enabled ? 1 : 0,
	       ioaddr + RTC_ALARM_INTERRUPT_MASK_REG_OFFS);

	return 0;
}

static int mv_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);
	void __iomem *ioaddr = pdata->ioaddr;

	if (pdata->irq < 0)
		return -EINVAL; /* fall back into rtc-dev's emulation */

	if (enabled)
		writel(1, ioaddr + RTC_ALARM_INTERRUPT_MASK_REG_OFFS);
	else
		writel(0, ioaddr + RTC_ALARM_INTERRUPT_MASK_REG_OFFS);
	return 0;
}

static irqreturn_t mv_rtc_interrupt(int irq, void *data)
{
	struct rtc_plat_data *pdata = data;
	void __iomem *ioaddr = pdata->ioaddr;

	/* alarm irq? */
	if (!readl(ioaddr + RTC_ALARM_INTERRUPT_CASUE_REG_OFFS))
		return IRQ_NONE;

	/* clear interrupt */
	writel(0, ioaddr + RTC_ALARM_INTERRUPT_CASUE_REG_OFFS);
	rtc_update_irq(pdata->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops mv_rtc_ops = {
	.read_time	= mv_rtc_read_time,
	.set_time	= mv_rtc_set_time,
};

static const struct rtc_class_ops mv_rtc_alarm_ops = {
	.read_time	= mv_rtc_read_time,
	.set_time	= mv_rtc_set_time,
	.read_alarm	= mv_rtc_read_alarm,
	.set_alarm	= mv_rtc_set_alarm,
	.alarm_irq_enable = mv_rtc_alarm_irq_enable,
};

static int __devinit mv_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct rtc_plat_data *pdata;
	resource_size_t size;
	u32 rtc_time;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	size = resource_size(res);
	if (!devm_request_mem_region(&pdev->dev, res->start, size,
				     pdev->name))
		return -EBUSY;

	pdata->ioaddr = devm_ioremap(&pdev->dev, res->start, size);
	if (!pdata->ioaddr)
		return -ENOMEM;

	/* make sure the 24 hours mode is enabled */
	rtc_time = readl(pdata->ioaddr + RTC_TIME_REG_OFFS);
	if (rtc_time & RTC_HOURS_12H_MODE) {
		dev_err(&pdev->dev, "24 Hours mode not supported.\n");
		return -EINVAL;
	}

	/* make sure it is actually functional */
	if (rtc_time == 0x01000000) {
		ssleep(1);
		rtc_time = readl(pdata->ioaddr + RTC_TIME_REG_OFFS);
		if (rtc_time == 0x01000000) {
			dev_err(&pdev->dev, "internal RTC not ticking\n");
			return -ENODEV;
		}
	}

	pdata->irq = platform_get_irq(pdev, 0);

	platform_set_drvdata(pdev, pdata);

	if (pdata->irq >= 0) {
		device_init_wakeup(&pdev->dev, 1);
		pdata->rtc = rtc_device_register(pdev->name, &pdev->dev,
						 &mv_rtc_alarm_ops,
						 THIS_MODULE);
	} else
		pdata->rtc = rtc_device_register(pdev->name, &pdev->dev,
						 &mv_rtc_ops, THIS_MODULE);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	if (pdata->irq >= 0) {
		writel(0, pdata->ioaddr + RTC_ALARM_INTERRUPT_MASK_REG_OFFS);
		if (devm_request_irq(&pdev->dev, pdata->irq, mv_rtc_interrupt,
				     IRQF_SHARED,
				     pdev->name, pdata) < 0) {
			dev_warn(&pdev->dev, "interrupt not available.\n");
			pdata->irq = -1;
		}
	}

	return 0;
}

static int __exit mv_rtc_remove(struct platform_device *pdev)
{
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	if (pdata->irq >= 0)
		device_init_wakeup(&pdev->dev, 0);

	rtc_device_unregister(pdata->rtc);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id rtc_mv_of_match_table[] = {
	{ .compatible = "marvell,orion-rtc", },
	{}
};
#endif

static struct platform_driver mv_rtc_driver = {
	.remove		= __exit_p(mv_rtc_remove),
	.driver		= {
		.name	= "rtc-mv",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(rtc_mv_of_match_table),
	},
};

static __init int mv_init(void)
{
	return platform_driver_probe(&mv_rtc_driver, mv_rtc_probe);
}

static __exit void mv_exit(void)
{
	platform_driver_unregister(&mv_rtc_driver);
}

module_init(mv_init);
module_exit(mv_exit);

MODULE_AUTHOR("Saeed Bishara <saeed@marvell.com>");
MODULE_DESCRIPTION("Marvell RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-mv");
