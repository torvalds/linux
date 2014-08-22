/*
 *  drivers/mfd/rt5025-irq.c
 *  Driver for Richtek RT5025 PMIC IRQ driver
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

#include <linux/mfd/rt5025.h>
#include <linux/mfd/rt5025-irq.h>

struct rt5025_irq_info {
	struct i2c_client *i2c;
	struct rt5025_chip *chip;
	struct device *dev;
	int irq;
	unsigned char suspend:1;
	struct delayed_work irq_delayed_work;
};

static irqreturn_t rt5025_irq_handler(int irqno, void *param)
{
	struct rt5025_irq_info *ii = param;
	unsigned char regval[6];
	unsigned int irq_event = 0;
	int ret = 0;

	if (ii->suspend) {
		schedule_delayed_work(&ii->irq_delayed_work,
			msecs_to_jiffies(10));
		goto irq_fin;
	}

	ret = rt5025_reg_read(ii->i2c, RT5025_REG_IRQFLG);
	if (ret < 0) {
		dev_err(ii->dev, "read gauge irq event fail\n");
	} else {
		irq_event = ret;
		RTINFO("gauge event %02x\n", irq_event);
		#ifdef CONFIG_BATTERY_RT5025
		if (irq_event)
			rt5025_gauge_irq_handler(ii->chip->battery_info,
				irq_event&(~RT5025_TALRT_MASK));
		#endif /* #ifdef CONFIG_RTC_RT5025 */
	}

	ret = rt5025_reg_block_read(ii->i2c, RT5025_REG_IRQEN1, 6, regval);
	if (ret < 0) {
		dev_err(ii->dev, "read charger irq event fail\n");
	} else {
		#ifdef CONFIG_BATTERY_RT5025
		/*combine gauge talrt irq into charger event*/
		irq_event = irq_event&RT5025_TALRT_MASK;
		irq_event <<= 24;
		irq_event |= (regval[1] << 16 | regval[3] << 8 | regval[5]);
		#else
		irq_event = regval[1] << 16 | regval[3] << 8 | regval[5];
		#endif
		RTINFO("chg event %08x\n", irq_event);
		#ifdef CONFIG_CHARGER_RT5025
		if (irq_event)
			rt5025_charger_irq_handler(ii->chip->charger_info,
				irq_event);
		#endif /* #ifdef CONFIG_CHARGER_RT5025 */
	}

	ret = rt5025_reg_block_read(ii->i2c, RT5025_REG_IRQEN4, 4, regval);
	if (ret < 0) {
		dev_err(ii->dev, "read misc irq event fail\n");
	} else {
		irq_event = regval[1] << 8 | regval[3];
		RTINFO("misc event %04x\n", irq_event);
		#ifdef CONFIG_MISC_RT5025
		if (irq_event)
			rt5025_misc_irq_handler(ii->chip->misc_info, irq_event);
		#endif /* #ifdef CONFIG_MISC_RT5025 */
	}
irq_fin:
	return IRQ_HANDLED;
}

static void rt5025_irq_delayed_work(struct work_struct *work)
{
	struct rt5025_irq_info *ii = (struct rt5025_irq_info *)container_of(work,
		struct rt5025_irq_info, irq_delayed_work.work);

	rt5025_irq_handler(ii->irq, ii);
}

static int rt_parse_dt(struct rt5025_irq_info *ii, struct device *dev)
{
	#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;
	int val;

	val = of_get_named_gpio(np, "rt,irq-gpio", 0);
	if (gpio_is_valid(val)) {
		if (gpio_request(val, "rt5025-irq") >= 0) {
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

static int rt_parse_pdata(struct rt5025_irq_info *ii, struct device *dev)
{
	struct rt5025_irq_data *pdata = dev->platform_data;

	if (gpio_is_valid(pdata->irq_gpio)) {
		if (gpio_request(pdata->irq_gpio, "rt5025-irq") >= 0) {
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

static int rt5025_irq_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = (pdev->dev.parent)->platform_data;
	struct rt5025_irq_info *ii;
	bool use_dt = pdev->dev.of_node;
	int ret;

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

	INIT_DELAYED_WORK(&ii->irq_delayed_work, rt5025_irq_delayed_work);

	platform_set_drvdata(pdev, ii);
	if (ii->irq >= 0) {
		ret = devm_request_irq(&pdev->dev, ii->irq, rt5025_irq_handler,
			IRQF_TRIGGER_FALLING|IRQF_NO_SUSPEND, "rt5025-irq", ii);
		if (ret != 0) {
			dev_err(&pdev->dev, "request threaded irq fail\n");
			goto out_dev;
		}
		enable_irq_wake(ii->irq);
		schedule_delayed_work(&ii->irq_delayed_work, 1*HZ);
	}
	dev_info(&pdev->dev, "driver successfully loaded\n");
	return 0;
out_dev:
	return -EINVAL;
}

static int rt5025_irq_remove(struct platform_device *pdev)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	if (ii->irq >= 0)
		devm_free_irq(&pdev->dev, ii->irq, ii);
	return 0;
}

static int rt5025_irq_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	ii->suspend = 1;
	return 0;
}

static int rt5025_irq_resume(struct platform_device *pdev)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	ii->suspend = 0;
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025-irq",},
	{},
};

static struct platform_driver rt5025_irq_driver = {
	.driver = {
		.name = RT5025_DEV_NAME "-irq",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt5025_irq_probe,
	.remove = rt5025_irq_remove,
	.suspend = rt5025_irq_suspend,
	.resume = rt5025_irq_resume,
};

static int rt5025_irq_init(void)
{
	return platform_driver_register(&rt5025_irq_driver);
}
device_initcall(rt5025_irq_init);

static void rt5025_irq_exit(void)
{
	platform_driver_unregister(&rt5025_irq_driver);
}

module_exit(rt5025_irq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("IRQ driver for RT5025");
MODULE_ALIAS("platform:"RT5025_DEV_NAME "-irq");
MODULE_VERSION(RT5025_DRV_VER);
