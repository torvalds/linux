/*
 *  drivers/mfd/rt5036-irq.c
 *  Driver for Richtek RT5036 PMIC IRQ driver
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
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

#include <linux/mfd/rt5036/rt5036.h>
#include <linux/mfd/rt5036/rt5036-irq.h>

struct rt5036_irq_info {
	struct i2c_client *i2c;
	struct rt5036_chip *chip;
	struct device *dev;
	int irq;
	unsigned char suspend:1;
	struct delayed_work irq_delayed_work;
};

static irqreturn_t rt5036_irq_handler(int irqno, void *param)
{
	struct rt5036_irq_info *ii = param;
	unsigned char regval[3];
	unsigned int irq_event;
	int ret = 0;

	if (ii->suspend) {
		schedule_delayed_work(&ii->irq_delayed_work,
				      msecs_to_jiffies(10));
		goto irq_fin;
	}

	ret =
	    rt5036_reg_block_read(ii->i2c, RT5036_REG_CHGIRQ1,
				  ARRAY_SIZE(regval), regval);
	if (ret < 0) {
		dev_err(ii->dev, "read charger irq event fail\n");
	} else {
		irq_event = regval[0] << 16 | regval[1] << 8 | regval[2];
		RTINFO("chg event %06x\n", irq_event);
#ifdef CONFIG_CHARGER_RT5036
		if (irq_event && ii->chip->chg_info)
			rt5036_charger_irq_handler(ii->chip->chg_info,
						   irq_event);
#endif /* #ifdef CONFIG_CHARGER_RT5036 */
	}

	ret =
	    rt5036_reg_block_read(ii->i2c, RT5036_REG_BUCKLDOIRQ,
				  ARRAY_SIZE(regval), regval);
	if (ret < 0) {
		dev_err(ii->dev, "read misc irq event fail\n");
	} else {
		irq_event = regval[0] << 16 | regval[1] << 8 | regval[2];
		RTINFO("misc event %06x\n", irq_event);
#ifdef CONFIG_MISC_RT5036
		if (irq_event && ii->chip->misc_info)
			rt5036_misc_irq_handler(ii->chip->misc_info, irq_event);
#endif /* #ifdef CONFIG_MISC_RT5036 */
	}

	ret = rt5036_reg_read(ii->i2c, RT5036_REG_STBWACKIRQ);
	if (ret < 0) {
		dev_err(ii->dev, "read rtc irq event fail\n");
	} else {
		irq_event = ret;
		RTINFO("rtc event %02x\n", irq_event);
#ifdef CONFIG_RTC_RT5036
		if (irq_event && ii->chip->rtc_info)
			rt5036_rtc_irq_handler(ii->chip->rtc_info, irq_event);
#endif /* #ifdef CONFIG_RTC_RT5036 */
	}
	rt5036_set_bits(ii->i2c, RT5036_REG_BUCKVN1, RT5036_IRQPREZ_MASK);
irq_fin:
	return IRQ_HANDLED;
}

static void rt5036_irq_delayed_work(struct work_struct *work)
{
	struct rt5036_irq_info *ii =
	    (struct rt5036_irq_info *)container_of(work, struct rt5036_irq_info,
						   irq_delayed_work.work);
	rt5036_irq_handler(ii->irq, ii);
}

static int rt_parse_dt(struct rt5036_irq_info *ii, struct device *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	int val;

	val = of_get_named_gpio(np, "rt,irq-gpio", 0);
	if (gpio_is_valid(val)) {
		if (gpio_request(val, "rt5036_irq") >= 0) {
			gpio_direction_input(val);
			ii->irq = gpio_to_irq(val);
		} else {
			ii->irq = -1;
		}
	} else {
		ii->irq = -1;
	}
#endif /* #ifdef CONFIG_OF */
	RTINFO("\n");
	return 0;
}

static int rt_parse_pdata(struct rt5036_irq_info *ii,
				    struct device *dev)
{
	struct rt5036_irq_data *pdata = dev->platform_data;

	if (gpio_is_valid(pdata->irq_gpio)) {
		if (gpio_request(pdata->irq_gpio, "rt5036_irq") >= 0) {
			gpio_direction_input(pdata->irq_gpio);
			ii->irq = gpio_to_irq(pdata->irq_gpio);
		} else {
			ii->irq = -1;
		}
	} else {
		ii->irq = -1;
	}
	RTINFO("\n");
	return 0;
}

static int rt5036_irq_probe(struct platform_device *pdev)
{
	struct rt5036_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5036_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5036_irq_info *ii;
	bool use_dt = pdev->dev.of_node;

	ii = devm_kzalloc(&pdev->dev, sizeof(*ii), GFP_KERNEL);
	if (!ii)
		return -ENOMEM;

	ii->i2c = chip->i2c;
	ii->chip = chip;
	ii->dev = &pdev->dev;
	if (use_dt) {
		rt_parse_dt(ii, &pdev->dev);
	} else {
		if (!pdata)
			goto out_dev;
		pdev->dev.platform_data = pdata->irq_pdata;
		rt_parse_pdata(ii, &pdev->dev);
		dev_info(&pdev->dev, "ii->irq %d\n", ii->irq);
	}
	INIT_DELAYED_WORK(&ii->irq_delayed_work, rt5036_irq_delayed_work);

	platform_set_drvdata(pdev, ii);
	if (ii->irq >= 0) {
		if (devm_request_threaded_irq
		    (&pdev->dev, ii->irq, NULL,rt5036_irq_handler,
		     IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND | IRQF_ONESHOT,
		     "rt5036_irq", ii)) {
			dev_err(&pdev->dev, "request threaded irq fail\n");
			goto out_dev;
		}
		enable_irq_wake(ii->irq);
		schedule_delayed_work(&ii->irq_delayed_work,
				      msecs_to_jiffies(500));
	}
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
out_dev:
	return -EINVAL;
}

static int rt5036_irq_remove(struct platform_device *pdev)
{
	struct rt5036_irq_info *ii = platform_get_drvdata(pdev);

	if (ii->irq >= 0)
		devm_free_irq(&pdev->dev, ii->irq, ii);
	return 0;
}

static int rt5036_irq_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt5036_irq_info *ii = platform_get_drvdata(pdev);

	ii->suspend = 1;
	return 0;
}

static int rt5036_irq_resume(struct platform_device *pdev)
{
	struct rt5036_irq_info *ii = platform_get_drvdata(pdev);

	ii->suspend = 0;
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt5036-irq",},
	{},
};

static struct platform_driver rt5036_irq_driver = {
	.driver = {
		   .name = RT5036_DEV_NAME "-irq",
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt5036_irq_probe,
	.remove = rt5036_irq_remove,
	.suspend = rt5036_irq_suspend,
	.resume = rt5036_irq_resume,
};

static int __init rt5036_irq_init(void)
{
	return platform_driver_register(&rt5036_irq_driver);
}
subsys_initcall_sync(rt5036_irq_init);

static void __exit rt5036_irq_exit(void)
{
	platform_driver_unregister(&rt5036_irq_driver);
}
module_exit(rt5036_irq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("IRQ driver for RT5036");
MODULE_ALIAS("platform:" RT5036_DEV_NAME "-irq");
MODULE_VERSION(RT5036_DRV_VER);
