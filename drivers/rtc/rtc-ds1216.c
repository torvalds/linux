/*
 * Dallas DS1216 RTC driver
 *
 * Copyright (c) 2007 Thomas Bogendoerfer
 *
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/bcd.h>
#include <linux/slab.h>

struct ds1216_regs {
	u8 tsec;
	u8 sec;
	u8 min;
	u8 hour;
	u8 wday;
	u8 mday;
	u8 month;
	u8 year;
};

#define DS1216_HOUR_1224	(1 << 7)
#define DS1216_HOUR_AMPM	(1 << 5)

struct ds1216_priv {
	struct rtc_device *rtc;
	void __iomem *ioaddr;
};

static const u8 magic[] = {
	0xc5, 0x3a, 0xa3, 0x5c, 0xc5, 0x3a, 0xa3, 0x5c
};

/*
 * Read the 64 bit we'd like to have - It a series
 * of 64 bits showing up in the LSB of the base register.
 *
 */
static void ds1216_read(u8 __iomem *ioaddr, u8 *buf)
{
	unsigned char c;
	int i, j;

	for (i = 0; i < 8; i++) {
		c = 0;
		for (j = 0; j < 8; j++)
			c |= (readb(ioaddr) & 0x1) << j;
		buf[i] = c;
	}
}

static void ds1216_write(u8 __iomem *ioaddr, const u8 *buf)
{
	unsigned char c;
	int i, j;

	for (i = 0; i < 8; i++) {
		c = buf[i];
		for (j = 0; j < 8; j++) {
			writeb(c, ioaddr);
			c = c >> 1;
		}
	}
}

static void ds1216_switch_ds_to_clock(u8 __iomem *ioaddr)
{
	/* Reset magic pointer */
	readb(ioaddr);
	/* Write 64 bit magic to DS1216 */
	ds1216_write(ioaddr, magic);
}

static int ds1216_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ds1216_priv *priv = dev_get_drvdata(dev);
	struct ds1216_regs regs;

	ds1216_switch_ds_to_clock(priv->ioaddr);
	ds1216_read(priv->ioaddr, (u8 *)&regs);

	tm->tm_sec = bcd2bin(regs.sec);
	tm->tm_min = bcd2bin(regs.min);
	if (regs.hour & DS1216_HOUR_1224) {
		/* AM/PM mode */
		tm->tm_hour = bcd2bin(regs.hour & 0x1f);
		if (regs.hour & DS1216_HOUR_AMPM)
			tm->tm_hour += 12;
	} else
		tm->tm_hour = bcd2bin(regs.hour & 0x3f);
	tm->tm_wday = (regs.wday & 7) - 1;
	tm->tm_mday = bcd2bin(regs.mday & 0x3f);
	tm->tm_mon = bcd2bin(regs.month & 0x1f);
	tm->tm_year = bcd2bin(regs.year);
	if (tm->tm_year < 70)
		tm->tm_year += 100;

	return 0;
}

static int ds1216_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct ds1216_priv *priv = dev_get_drvdata(dev);
	struct ds1216_regs regs;

	ds1216_switch_ds_to_clock(priv->ioaddr);
	ds1216_read(priv->ioaddr, (u8 *)&regs);

	regs.tsec = 0; /* clear 0.1 and 0.01 seconds */
	regs.sec = bin2bcd(tm->tm_sec);
	regs.min = bin2bcd(tm->tm_min);
	regs.hour &= DS1216_HOUR_1224;
	if (regs.hour && tm->tm_hour > 12) {
		regs.hour |= DS1216_HOUR_AMPM;
		tm->tm_hour -= 12;
	}
	regs.hour |= bin2bcd(tm->tm_hour);
	regs.wday &= ~7;
	regs.wday |= tm->tm_wday;
	regs.mday = bin2bcd(tm->tm_mday);
	regs.month = bin2bcd(tm->tm_mon);
	regs.year = bin2bcd(tm->tm_year % 100);

	ds1216_switch_ds_to_clock(priv->ioaddr);
	ds1216_write(priv->ioaddr, (u8 *)&regs);
	return 0;
}

static const struct rtc_class_ops ds1216_rtc_ops = {
	.read_time	= ds1216_rtc_read_time,
	.set_time	= ds1216_rtc_set_time,
};

static int __init ds1216_rtc_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct ds1216_priv *priv;
	u8 dummy[8];

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->ioaddr))
		return PTR_ERR(priv->ioaddr);

	priv->rtc = devm_rtc_device_register(&pdev->dev, "ds1216",
					&ds1216_rtc_ops, THIS_MODULE);
	if (IS_ERR(priv->rtc))
		return PTR_ERR(priv->rtc);

	/* dummy read to get clock into a known state */
	ds1216_read(priv->ioaddr, dummy);
	return 0;
}

static struct platform_driver ds1216_rtc_platform_driver = {
	.driver		= {
		.name	= "rtc-ds1216",
	},
};

module_platform_driver_probe(ds1216_rtc_platform_driver, ds1216_rtc_probe);

MODULE_AUTHOR("Thomas Bogendoerfer <tsbogend@alpha.franken.de>");
MODULE_DESCRIPTION("DS1216 RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rtc-ds1216");
