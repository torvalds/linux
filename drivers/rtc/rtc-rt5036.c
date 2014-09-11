/*
 *  drivers/rtc/rt5036-rtc.c
 *  Driver for Richtek RT5036 PMIC RTC driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include <linux/mfd/rt5036/rt5036.h>
#include <linux/rtc/rtc-rt5036.h>

static unsigned char rtc_init_regval[] = {
	0x1,			/*REG 0x97*/
	0x3,			/*REG 0xA5*/
};

static int rt5036_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rt5036_rtc_info *ri = dev_get_drvdata(dev);
	unsigned char val[6];
	int rc;

	RTINFO("\n");
	rc = rt5036_reg_block_read(ri->i2c, RT5036_REG_RTCTSEC, ARRAY_SIZE(val),
				   val);
	if (rc < 0) {
		dev_err(dev, "reading rtc time io error\n");
	} else {
		tm->tm_sec = val[0] & RT5036_RTC_SECMASK;
		tm->tm_min = val[1] & RT5036_RTC_MINMASK;
		tm->tm_hour = val[2] & RT5036_RTC_HOURMASK;
		tm->tm_year = (val[3] & RT5036_RTC_YEARMASK) + 100;
		tm->tm_mon = (val[4] & RT5036_RTC_MONMASK) - 1;
		tm->tm_mday = val[5] & RT5036_RTC_DAYMASK;
		RTINFO("%04d:%02d:%02d, %02d:%02d:%02d\n", tm->tm_year + 1900,
		       tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
		       tm->tm_sec);
		rc = rtc_valid_tm(tm);
		if (rc < 0) {
			dev_err(dev, "not invalid time reading from RT5036\n");
			return -EINVAL;
		}
	}
	return rc;
}

static int rt5036_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rt5036_rtc_info *ri = dev_get_drvdata(dev);
	unsigned char val[6];
	int rc;

	RTINFO("\n");
	rc = rt5036_reg_block_read(ri->i2c, RT5036_REG_RTCTSEC, ARRAY_SIZE(val),
				   val);
	if (rc < 0) {
		dev_err(dev, "reading rtc time io error\n");
	} else {
		val[0] &= ~RT5036_RTC_SECMASK;
		val[0] |= (tm->tm_sec & RT5036_RTC_SECMASK);
		val[1] &= ~RT5036_RTC_MINMASK;
		val[1] |= (tm->tm_min & RT5036_RTC_MINMASK);
		val[2] &= ~RT5036_RTC_HOURMASK;
		val[2] |= (tm->tm_hour & RT5036_RTC_HOURMASK);
		val[3] &= ~RT5036_RTC_YEARMASK;
		val[3] |= ((tm->tm_year - 100) & RT5036_RTC_YEARMASK);
		val[4] &= ~RT5036_RTC_MONMASK;
		val[4] |= ((tm->tm_mon + 1) & RT5036_RTC_MONMASK);
		val[5] &= ~RT5036_RTC_DAYMASK;
		val[5] |= (tm->tm_mday & RT5036_RTC_DAYMASK);
		val[5] &= ~RT5036_RTC_WEEKMASK;
		val[5] |=
		    ((tm->
		      tm_wday & RT5036_RTC_WEEKMASK) << RT5036_RTC_WEEKSHIFT);
		RTINFO("%04d:%02d:%02d, %02d:%02d:%02d\n", tm->tm_year + 1900,
		       tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
		       tm->tm_sec);

		if (tm->tm_year < 100)
			return -EINVAL;

		rc = rt5036_reg_block_write(ri->i2c, RT5036_REG_RTCTSEC,
					    ARRAY_SIZE(val), val);
		if (rc < 0) {
			dev_err(dev, "writing rtc time io error\n");
			return rc;
		}
	}
	return rc;
}

static int rt5036_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rt5036_rtc_info *ri = dev_get_drvdata(dev);
	struct rtc_time *tm = &(alarm->time);
	unsigned char val[6];
	int rc;

	RTINFO("\n");
	rc = rt5036_reg_block_read(ri->i2c, RT5036_REG_RTCASEC, ARRAY_SIZE(val),
				   val);
	if (rc < 0) {
		dev_err(dev, "reading alarm time io error\n");
	} else {
		tm->tm_sec = val[0] & RT5036_RTC_SECMASK;
		tm->tm_min = val[1] & RT5036_RTC_MINMASK;
		tm->tm_hour = val[2] & RT5036_RTC_HOURMASK;
		tm->tm_year = (val[3] & RT5036_RTC_YEARMASK) + 100;
		tm->tm_mon = (val[4] & RT5036_RTC_MONMASK) - 1;
		tm->tm_mday = val[5] & RT5036_RTC_DAYMASK;
		RTINFO("%04d_%02d_%02d, %02d:%02d:%02d\n", tm->tm_year + 1900,
		       tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
		       tm->tm_sec);
		rc = rtc_valid_tm(tm);
		if (rc < 0) {
			dev_err(dev, "not invalid alarm reading from RT5036\n");
			return -EINVAL;
		}
	}
	return rc;
}

static int rt5036_alarm_irq_enable(struct device *dev, unsigned int enabled);

static int rt5036_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rt5036_rtc_info *ri = dev_get_drvdata(dev);
	struct rtc_time *tm = &alarm->time;
	unsigned char val[6];
	int rc;

	RTINFO("\n");
	rt5036_alarm_irq_enable(ri->dev, 0);
	rc = rt5036_reg_block_read(ri->i2c, RT5036_REG_RTCASEC, ARRAY_SIZE(val),
				   val);
	if (rc < 0) {
		dev_err(dev, "reading rtc time io error\n");
	} else {
		val[0] &= ~RT5036_RTC_SECMASK;
		val[0] |= (tm->tm_sec & RT5036_RTC_SECMASK);
		val[1] &= ~RT5036_RTC_MINMASK;
		val[1] |= (tm->tm_min & RT5036_RTC_MINMASK);
		val[2] &= ~RT5036_RTC_HOURMASK;
		val[2] |= (tm->tm_hour & RT5036_RTC_HOURMASK);
		val[3] &= ~RT5036_RTC_YEARMASK;
		val[3] |= ((tm->tm_year - 100) & RT5036_RTC_YEARMASK);
		val[4] &= ~RT5036_RTC_MONMASK;
		val[4] |= ((tm->tm_mon + 1) & RT5036_RTC_MONMASK);
		val[5] &= ~RT5036_RTC_DAYMASK;
		val[5] |= (tm->tm_mday & RT5036_RTC_DAYMASK);
		RTINFO("%04d:%02d:%02d, %02d:%02d:%02d\n", tm->tm_year + 1900,
		       tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
		       tm->tm_sec);

		if (tm->tm_year < 100)
			return -EINVAL;

		rc = rt5036_reg_block_write(ri->i2c, RT5036_REG_RTCASEC,
					    ARRAY_SIZE(val), val);
		if (rc < 0) {
			dev_err(dev, "writing alarm time io error\n");
			return rc;
		}
	}
	rt5036_alarm_irq_enable(ri->dev, alarm->enabled);
	return rc;
}

static int rt5036_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rt5036_rtc_info *ri = dev_get_drvdata(dev);

	RTINFO("enable=%d\n", enabled);
	if (enabled) {
		rt5036_clr_bits(ri->i2c, RT5036_REG_STBWACKIRQMASK,
				RT5036_RTCAIRQ_MASK);
		rt5036_set_bits(ri->i2c, RT5036_REG_STBMODE,
				RT5036_RTCAEN_MASK);
	} else {
		rt5036_clr_bits(ri->i2c, RT5036_REG_STBMODE,
				RT5036_RTCAEN_MASK);
		rt5036_set_bits(ri->i2c, RT5036_REG_STBWACKIRQMASK,
				RT5036_RTCAIRQ_MASK);
	}
	return 0;
}

static const struct rtc_class_ops rt5036_rtc_ops = {
	.read_time = rt5036_read_time,
	.set_time = rt5036_set_time,
	.read_alarm = rt5036_read_alarm,
	.set_alarm = rt5036_set_alarm,
	.alarm_irq_enable = rt5036_alarm_irq_enable,
};

static void rt5036_general_irq_handler(void *info, int eventno)
{
	struct rt5036_rtc_info *ri = info;

	dev_info(ri->dev, "eventno=%02d\n", eventno);
	switch (eventno) {
	case RTCEVENT_CAIRQ:
		rt5036_alarm_irq_enable(ri->dev, 0);
		break;
	default:
		break;
	}
}

static rt_irq_handler rt_rtcirq_handler[RTCEVENT_MAX] = {
	[RTCEVENT_CAIRQ] = rt5036_general_irq_handler,
	[RTCEVENT_CDIRQ] = rt5036_general_irq_handler,
};

void rt5036_rtc_irq_handler(struct rt5036_rtc_info *ri, unsigned int irqevent)
{
	int i;

	for (i = 0; i < RTCEVENT_MAX; i++) {
		if ((irqevent & (1 << i)) && rt_rtcirq_handler[i])
			rt_rtcirq_handler[i] (ri, i);
	}
}
EXPORT_SYMBOL(rt5036_rtc_irq_handler);

static int rt5036_rtc_reginit(struct i2c_client *i2c)
{
	rt5036_reg_write(i2c, RT5036_REG_STBMODE, rtc_init_regval[0]);
	rt5036_reg_write(i2c, RT5036_REG_STBWACKIRQMASK, rtc_init_regval[1]);
	/*always clear at the fist time*/
	rt5036_reg_read(i2c, RT5036_REG_STBWACKIRQ);
	return 0;
}

static int rt_parse_dt(struct rt5036_rtc_info *ri, struct device *dev)
{
	rt5036_rtc_reginit(ri->i2c);
	RTINFO("\n");
	return 0;
}

static int rt_parse_pdata(struct rt5036_rtc_info *ri,
				    struct device *dev)
{
	rt5036_rtc_reginit(ri->i2c);
	RTINFO("\n");
	return 0;
}

static int rt5036_rtc_probe(struct platform_device *pdev)
{
	struct rt5036_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5036_rtc_info *ri;
	bool use_dt = pdev->dev.of_node;

	ri = devm_kzalloc(&pdev->dev, sizeof(*ri), GFP_KERNEL);
	if (!ri)
		return -ENOMEM;

	ri->i2c = chip->i2c;
	if (use_dt)
		rt_parse_dt(ri, &pdev->dev);
	else
		rt_parse_pdata(ri, &pdev->dev);

	ri->dev = &pdev->dev;
	platform_set_drvdata(pdev, ri);

	ri->rtc = rtc_device_register("rt5036-rtc", &pdev->dev,
				      &rt5036_rtc_ops, THIS_MODULE);
	if (IS_ERR(ri->rtc)) {
		dev_err(&pdev->dev, "rtc device register failed\n");
		goto out_dev;
	}
	chip->rtc_info = ri;
	device_init_wakeup(&pdev->dev, 1);
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
out_dev:
	return -EINVAL;
}

static int rt5036_rtc_remove(struct platform_device *pdev)
{
	struct rt5036_rtc_info *ri = platform_get_drvdata(pdev);

	rtc_device_unregister(ri->rtc);
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt5036-rtc",},
	{},
};

static struct platform_driver rt5036_rtc_driver = {
	.driver = {
		   .name = RT5036_DEV_NAME "-rtc",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt5036_rtc_probe,
	.remove = rt5036_rtc_remove,
};

static int __init rt5036_rtc_init(void)
{
	return platform_driver_register(&rt5036_rtc_driver);
}
subsys_initcall(rt5036_rtc_init);

static void __exit rt5036_rtc_exit(void)
{
	platform_driver_unregister(&rt5036_rtc_driver);
}
module_exit(rt5036_rtc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("RTC driver for RT5036");
MODULE_ALIAS("platform:" RT5036_DEV_NAME "-rtc");
MODULE_VERSION(RT5036_DRV_VER);
