/* drivers/rtc/rtc-v3020.c
 *
 * Copyright (C) 2006 8D Technologies inc.
 * Copyright (C) 2004 Compulab Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Driver for the V3020 RTC
 *
 * Changelog:
 *
 *  10-May-2006: Raphael Assenat <raph@8d.com>
 *				- Converted to platform driver
 *				- Use the generic rtc class
 *
 *  ??-???-2004: Someone at Compulab
 *  			- Initial driver creation.
 *
 */
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/types.h>
#include <linux/bcd.h>
#include <linux/rtc-v3020.h>
#include <linux/delay.h>

#include <asm/io.h>

#undef DEBUG

struct v3020 {
	void __iomem *ioaddress;
	int leftshift;
	struct rtc_device *rtc;
};

static void v3020_set_reg(struct v3020 *chip, unsigned char address,
			unsigned char data)
{
	int i;
	unsigned char tmp;

	tmp = address;
	for (i = 0; i < 4; i++) {
		writel((tmp & 1) << chip->leftshift, chip->ioaddress);
		tmp >>= 1;
		udelay(1);
	}

	/* Commands dont have data */
	if (!V3020_IS_COMMAND(address)) {
		for (i = 0; i < 8; i++) {
			writel((data & 1) << chip->leftshift, chip->ioaddress);
			data >>= 1;
			udelay(1);
		}
	}
}

static unsigned char v3020_get_reg(struct v3020 *chip, unsigned char address)
{
	unsigned int data=0;
	int i;

	for (i = 0; i < 4; i++) {
		writel((address & 1) << chip->leftshift, chip->ioaddress);
		address >>= 1;
		udelay(1);
	}

	for (i = 0; i < 8; i++) {
		data >>= 1;
		if (readl(chip->ioaddress) & (1 << chip->leftshift))
			data |= 0x80;
		udelay(1);
	}

	return data;
}

static int v3020_read_time(struct device *dev, struct rtc_time *dt)
{
	struct v3020 *chip = dev_get_drvdata(dev);
	int tmp;

	/* Copy the current time to ram... */
	v3020_set_reg(chip, V3020_CMD_CLOCK2RAM, 0);

	/* ...and then read constant values. */
	tmp = v3020_get_reg(chip, V3020_SECONDS);
	dt->tm_sec	= BCD2BIN(tmp);
	tmp = v3020_get_reg(chip, V3020_MINUTES);
	dt->tm_min	= BCD2BIN(tmp);
	tmp = v3020_get_reg(chip, V3020_HOURS);
	dt->tm_hour	= BCD2BIN(tmp);
	tmp = v3020_get_reg(chip, V3020_MONTH_DAY);
	dt->tm_mday	= BCD2BIN(tmp);
	tmp = v3020_get_reg(chip, V3020_MONTH);
	dt->tm_mon    = BCD2BIN(tmp) - 1;
	tmp = v3020_get_reg(chip, V3020_WEEK_DAY);
	dt->tm_wday	= BCD2BIN(tmp);
	tmp = v3020_get_reg(chip, V3020_YEAR);
	dt->tm_year = BCD2BIN(tmp)+100;

#ifdef DEBUG
	printk("\n%s : Read RTC values\n",__FUNCTION__);
	printk("tm_hour: %i\n",dt->tm_hour);
	printk("tm_min : %i\n",dt->tm_min);
	printk("tm_sec : %i\n",dt->tm_sec);
	printk("tm_year: %i\n",dt->tm_year);
	printk("tm_mon : %i\n",dt->tm_mon);
	printk("tm_mday: %i\n",dt->tm_mday);
	printk("tm_wday: %i\n",dt->tm_wday);
#endif

	return 0;
}


static int v3020_set_time(struct device *dev, struct rtc_time *dt)
{
	struct v3020 *chip = dev_get_drvdata(dev);

#ifdef DEBUG
	printk("\n%s : Setting RTC values\n",__FUNCTION__);
	printk("tm_sec : %i\n",dt->tm_sec);
	printk("tm_min : %i\n",dt->tm_min);
	printk("tm_hour: %i\n",dt->tm_hour);
	printk("tm_mday: %i\n",dt->tm_mday);
	printk("tm_wday: %i\n",dt->tm_wday);
	printk("tm_year: %i\n",dt->tm_year);
#endif

	/* Write all the values to ram... */
	v3020_set_reg(chip, V3020_SECONDS, 	BIN2BCD(dt->tm_sec));
	v3020_set_reg(chip, V3020_MINUTES, 	BIN2BCD(dt->tm_min));
	v3020_set_reg(chip, V3020_HOURS, 	BIN2BCD(dt->tm_hour));
	v3020_set_reg(chip, V3020_MONTH_DAY,	BIN2BCD(dt->tm_mday));
	v3020_set_reg(chip, V3020_MONTH,     BIN2BCD(dt->tm_mon + 1));
	v3020_set_reg(chip, V3020_WEEK_DAY, 	BIN2BCD(dt->tm_wday));
	v3020_set_reg(chip, V3020_YEAR, 	BIN2BCD(dt->tm_year % 100));

	/* ...and set the clock. */
	v3020_set_reg(chip, V3020_CMD_RAM2CLOCK, 0);

	/* Compulab used this delay here. I dont know why,
	 * the datasheet does not specify a delay. */
	/*mdelay(5);*/

	return 0;
}

static const struct rtc_class_ops v3020_rtc_ops = {
	.read_time	= v3020_read_time,
	.set_time	= v3020_set_time,
};

static int rtc_probe(struct platform_device *pdev)
{
	struct v3020_platform_data *pdata = pdev->dev.platform_data;
	struct v3020 *chip;
	struct rtc_device *rtc;
	int retval = -EBUSY;
	int i;
	int temp;

	if (pdev->num_resources != 1)
		return -EBUSY;

	if (pdev->resource[0].flags != IORESOURCE_MEM)
		return -EBUSY;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->leftshift = pdata->leftshift;
	chip->ioaddress = ioremap(pdev->resource[0].start, 1);
	if (chip->ioaddress == NULL)
		goto err_chip;

	/* Make sure the v3020 expects a communication cycle
	 * by reading 8 times */
	for (i = 0; i < 8; i++)
		temp = readl(chip->ioaddress);

	/* Test chip by doing a write/read sequence
	 * to the chip ram */
	v3020_set_reg(chip, V3020_SECONDS, 0x33);
	if(v3020_get_reg(chip, V3020_SECONDS) != 0x33) {
		retval = -ENODEV;
		goto err_io;
	}

	/* Make sure frequency measurment mode, test modes, and lock
	 * are all disabled */
	v3020_set_reg(chip, V3020_STATUS_0, 0x0);

	dev_info(&pdev->dev, "Chip available at physical address 0x%llx,"
		"data connected to D%d\n",
		(unsigned long long)pdev->resource[0].start,
		chip->leftshift);

	platform_set_drvdata(pdev, chip);

	rtc = rtc_device_register("v3020",
				&pdev->dev, &v3020_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		retval = PTR_ERR(rtc);
		goto err_io;
	}
	chip->rtc = rtc;

	return 0;

err_io:
	iounmap(chip->ioaddress);
err_chip:
	kfree(chip);

	return retval;
}

static int rtc_remove(struct platform_device *dev)
{
	struct v3020 *chip = platform_get_drvdata(dev);
	struct rtc_device *rtc = chip->rtc;

	if (rtc)
		rtc_device_unregister(rtc);

	iounmap(chip->ioaddress);
	kfree(chip);

	return 0;
}

static struct platform_driver rtc_device_driver = {
	.probe	= rtc_probe,
	.remove = rtc_remove,
	.driver = {
		.name	= "v3020",
		.owner	= THIS_MODULE,
	},
};

static __init int v3020_init(void)
{
	return platform_driver_register(&rtc_device_driver);
}

static __exit void v3020_exit(void)
{
	platform_driver_unregister(&rtc_device_driver);
}

module_init(v3020_init);
module_exit(v3020_exit);

MODULE_DESCRIPTION("V3020 RTC");
MODULE_AUTHOR("Raphael Assenat");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:v3020");
