/*
 * Dallas DS1302 RTC Support
 *
 *  Copyright (C) 2002 David McCullough
 *  Copyright (C) 2003 - 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License version 2. See the file "COPYING" in the main directory of
 * this archive for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/bcd.h>
#include <asm/rtc.h>

#define DRV_NAME	"rtc-ds1302"
#define DRV_VERSION	"0.1.1"

#define	RTC_CMD_READ	0x81		/* Read command */
#define	RTC_CMD_WRITE	0x80		/* Write command */

#define RTC_ADDR_RAM0	0x20		/* Address of RAM0 */
#define RTC_ADDR_TCR	0x08		/* Address of trickle charge register */
#define	RTC_ADDR_YEAR	0x06		/* Address of year register */
#define	RTC_ADDR_DAY	0x05		/* Address of day of week register */
#define	RTC_ADDR_MON	0x04		/* Address of month register */
#define	RTC_ADDR_DATE	0x03		/* Address of day of month register */
#define	RTC_ADDR_HOUR	0x02		/* Address of hour register */
#define	RTC_ADDR_MIN	0x01		/* Address of minute register */
#define	RTC_ADDR_SEC	0x00		/* Address of second register */

#define	RTC_RESET	0x1000
#define	RTC_IODATA	0x0800
#define	RTC_SCLK	0x0400

#ifdef CONFIG_SH_SECUREEDGE5410
#include <mach/snapgear.h>
#define set_dp(x)	SECUREEDGE_WRITE_IOPORT(x, 0x1c00)
#define get_dp()	SECUREEDGE_READ_IOPORT()
#else
#error "Add support for your platform"
#endif

static void ds1302_sendbits(unsigned int val)
{
	int i;

	for (i = 8; (i); i--, val >>= 1) {
		set_dp((get_dp() & ~RTC_IODATA) | ((val & 0x1) ?
			RTC_IODATA : 0));
		set_dp(get_dp() | RTC_SCLK);	/* clock high */
		set_dp(get_dp() & ~RTC_SCLK);	/* clock low */
	}
}

static unsigned int ds1302_recvbits(void)
{
	unsigned int val;
	int i;

	for (i = 0, val = 0; (i < 8); i++) {
		val |= (((get_dp() & RTC_IODATA) ? 1 : 0) << i);
		set_dp(get_dp() | RTC_SCLK);	/* clock high */
		set_dp(get_dp() & ~RTC_SCLK);	/* clock low */
	}

	return val;
}

static unsigned int ds1302_readbyte(unsigned int addr)
{
	unsigned int val;

	set_dp(get_dp() & ~(RTC_RESET | RTC_IODATA | RTC_SCLK));

	set_dp(get_dp() | RTC_RESET);
	ds1302_sendbits(((addr & 0x3f) << 1) | RTC_CMD_READ);
	val = ds1302_recvbits();
	set_dp(get_dp() & ~RTC_RESET);

	return val;
}

static void ds1302_writebyte(unsigned int addr, unsigned int val)
{
	set_dp(get_dp() & ~(RTC_RESET | RTC_IODATA | RTC_SCLK));
	set_dp(get_dp() | RTC_RESET);
	ds1302_sendbits(((addr & 0x3f) << 1) | RTC_CMD_WRITE);
	ds1302_sendbits(val);
	set_dp(get_dp() & ~RTC_RESET);
}

static int ds1302_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	tm->tm_sec	= bcd2bin(ds1302_readbyte(RTC_ADDR_SEC));
	tm->tm_min	= bcd2bin(ds1302_readbyte(RTC_ADDR_MIN));
	tm->tm_hour	= bcd2bin(ds1302_readbyte(RTC_ADDR_HOUR));
	tm->tm_wday	= bcd2bin(ds1302_readbyte(RTC_ADDR_DAY));
	tm->tm_mday	= bcd2bin(ds1302_readbyte(RTC_ADDR_DATE));
	tm->tm_mon	= bcd2bin(ds1302_readbyte(RTC_ADDR_MON)) - 1;
	tm->tm_year	= bcd2bin(ds1302_readbyte(RTC_ADDR_YEAR));

	if (tm->tm_year < 70)
		tm->tm_year += 100;

	dev_dbg(dev, "%s: tm is secs=%d, mins=%d, hours=%d, "
		"mday=%d, mon=%d, year=%d, wday=%d\n",
		__func__,
		tm->tm_sec, tm->tm_min, tm->tm_hour,
		tm->tm_mday, tm->tm_mon + 1, tm->tm_year, tm->tm_wday);

	return rtc_valid_tm(tm);
}

static int ds1302_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	/* Stop RTC */
	ds1302_writebyte(RTC_ADDR_SEC, ds1302_readbyte(RTC_ADDR_SEC) | 0x80);

	ds1302_writebyte(RTC_ADDR_SEC, bin2bcd(tm->tm_sec));
	ds1302_writebyte(RTC_ADDR_MIN, bin2bcd(tm->tm_min));
	ds1302_writebyte(RTC_ADDR_HOUR, bin2bcd(tm->tm_hour));
	ds1302_writebyte(RTC_ADDR_DAY, bin2bcd(tm->tm_wday));
	ds1302_writebyte(RTC_ADDR_DATE, bin2bcd(tm->tm_mday));
	ds1302_writebyte(RTC_ADDR_MON, bin2bcd(tm->tm_mon + 1));
	ds1302_writebyte(RTC_ADDR_YEAR, bin2bcd(tm->tm_year % 100));

	/* Start RTC */
	ds1302_writebyte(RTC_ADDR_SEC, ds1302_readbyte(RTC_ADDR_SEC) & ~0x80);

	return 0;
}

static int ds1302_rtc_ioctl(struct device *dev, unsigned int cmd,
			    unsigned long arg)
{
	switch (cmd) {
#ifdef RTC_SET_CHARGE
	case RTC_SET_CHARGE:
	{
		struct ds1302_rtc *rtc = dev_get_drvdata(dev);
		int tcs_val;

		if (copy_from_user(&tcs_val, (int __user *)arg, sizeof(int)))
			return -EFAULT;

		ds1302_writebyte(RTC_ADDR_TCR, (0xa0 | tcs_val * 0xf));
		return 0;
	}
#endif
	}

	return -ENOIOCTLCMD;
}

static struct rtc_class_ops ds1302_rtc_ops = {
	.read_time	= ds1302_rtc_read_time,
	.set_time	= ds1302_rtc_set_time,
	.ioctl		= ds1302_rtc_ioctl,
};

static int __init ds1302_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;

	/* Reset */
	set_dp(get_dp() & ~(RTC_RESET | RTC_IODATA | RTC_SCLK));

	/* Write a magic value to the DS1302 RAM, and see if it sticks. */
	ds1302_writebyte(RTC_ADDR_RAM0, 0x42);
	if (ds1302_readbyte(RTC_ADDR_RAM0) != 0x42)
		return -ENODEV;

	rtc = rtc_device_register("ds1302", &pdev->dev,
					   &ds1302_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	platform_set_drvdata(pdev, rtc);

	return 0;
}

static int __devexit ds1302_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver ds1302_platform_driver = {
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.remove		= __exit_p(ds1302_rtc_remove),
};

static int __init ds1302_rtc_init(void)
{
	return platform_driver_probe(&ds1302_platform_driver, ds1302_rtc_probe);
}

static void __exit ds1302_rtc_exit(void)
{
	platform_driver_unregister(&ds1302_platform_driver);
}

module_init(ds1302_rtc_init);
module_exit(ds1302_rtc_exit);

MODULE_DESCRIPTION("Dallas DS1302 RTC driver");
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR("Paul Mundt, David McCullough");
MODULE_LICENSE("GPL v2");
