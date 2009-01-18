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


struct rtc_plat_data {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
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

static const struct rtc_class_ops mv_rtc_ops = {
	.read_time	= mv_rtc_read_time,
	.set_time	= mv_rtc_set_time,
};

static int __init mv_rtc_probe(struct platform_device *pdev)
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

	platform_set_drvdata(pdev, pdata);
	pdata->rtc = rtc_device_register(pdev->name, &pdev->dev,
					 &mv_rtc_ops, THIS_MODULE);
	if (IS_ERR(pdata->rtc))
		return PTR_ERR(pdata->rtc);

	return 0;
}

static int __exit mv_rtc_remove(struct platform_device *pdev)
{
	struct rtc_plat_data *pdata = platform_get_drvdata(pdev);

	rtc_device_unregister(pdata->rtc);
	return 0;
}

static struct platform_driver mv_rtc_driver = {
	.remove		= __exit_p(mv_rtc_remove),
	.driver		= {
		.name	= "rtc-mv",
		.owner	= THIS_MODULE,
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
