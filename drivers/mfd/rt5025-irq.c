/*
 *  drivers/mfd/rt5025-irq.c
 *  Driver foo Richtek RT5025 PMIC irq
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include <linux/mfd/rt5025.h>
#include <linux/mfd/rt5025-irq.h>

struct rt5025_irq_info {
	struct i2c_client *i2c;
	struct device *dev;
	struct rt5025_chip *chip;
	struct workqueue_struct *wq;
	struct rt5025_event_callback *event_cb;
	struct delayed_work delayed_work;
	int intr_pin;
	int irq;
};

static void rt5025_work_func(struct work_struct *work)
{
	struct delayed_work *delayed_work = (struct delayed_work *)container_of(work, struct delayed_work, work);
	struct rt5025_irq_info *ii = (struct rt5025_irq_info *)container_of(delayed_work, struct rt5025_irq_info, delayed_work);
	unsigned char irq_stat[10] = {0};
	uint32_t chg_event = 0, pwr_event = 0;

	if (rt5025_reg_block_read(ii->i2c, RT5025_REG_IRQEN1, 10, irq_stat) >= 0)
	{
		RTINFO("irq1->%02x, irq2->%02x, irq3->%02x\n", irq_stat[1], irq_stat[3], irq_stat[5]);
		RTINFO("irq4->%02x, irq5->%02x\n", irq_stat[7], irq_stat[9]);
		RTINFO("stat value = %02x\n", rt5025_reg_read(ii->i2c, RT5025_REG_CHGSTAT));

		chg_event = irq_stat[1]<<16 | irq_stat[3]<<8 | irq_stat[5];
		pwr_event = irq_stat[7]<<8 | irq_stat[9];
		#ifdef CONFIG_POWER_RT5025
		if (chg_event & CHARGER_DETECT_MASK)
			rt5025_power_charge_detect(ii->chip->power_info);
		#endif /* CONFIG_POWER_RT5025 */
		if (ii->event_cb)
		{
			if (chg_event)
				ii->event_cb->charger_event_callback(chg_event);
			if (pwr_event)
				ii->event_cb->power_event_callkback(pwr_event);
		}
	}
	else
		dev_err(ii->dev, "read irq stat io fail\n");

	#ifdef CONFIG_POWER_RT5025
	rt5025_power_passirq_to_gauge(ii->chip->power_info);
	#endif /* CONFIG_POWER_RT5025 */

	enable_irq(ii->irq);
}

static irqreturn_t rt5025_interrupt(int irqno, void *param)
{
	struct rt5025_irq_info *ii = (struct rt5025_irq_info *)param;

	disable_irq_nosync(ii->irq);
	queue_delayed_work(ii->wq, &ii->delayed_work, 0);
	return IRQ_HANDLED;
}

static int __devinit rt5025_interrupt_init(struct rt5025_irq_info* ii)
{
	int ret;

	RTINFO("\n");
	ii->wq = create_workqueue("rt5025_wq");
	INIT_DELAYED_WORK(&ii->delayed_work, rt5025_work_func);

	if (gpio_is_valid(ii->intr_pin))
	{
		ret = gpio_request(ii->intr_pin, "rt5025_interrupt");
		if (ret)
			return ret;

		ret = gpio_direction_input(ii->intr_pin);
		if (ret)
			return ret;

		if (request_irq(ii->irq, rt5025_interrupt, IRQ_TYPE_EDGE_FALLING|IRQF_DISABLED, "RT5025_IRQ", ii))
		{
			dev_err(ii->dev, "couldn't allocate IRQ_NO(%d) !\n", ii->irq);
			return -EINVAL;
		}
		enable_irq_wake(ii->irq);

		if (!gpio_get_value(ii->intr_pin))
		{
			disable_irq_nosync(ii->irq);
			queue_delayed_work(ii->wq, &ii->delayed_work, 0);
		}
	}
	else
		return -EINVAL;

	return 0;
}

static void __devexit rt5025_interrupt_deinit(struct rt5025_irq_info* ii)
{
	if (ii->irq)
		free_irq(ii->irq, ii);

	if (ii->wq)
	{
		cancel_delayed_work_sync(&ii->delayed_work);
		flush_workqueue(ii->wq);
		destroy_workqueue(ii->wq);
	}
}

static int __devinit rt5025_irq_reg_init(struct rt5025_irq_info* ii, struct rt5025_irq_data* irq_data)
{
	RTINFO("\n");
	// will just enable the interrupt that we want
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN1, irq_data->irq_enable1.val);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN2, irq_data->irq_enable2.val);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN3, irq_data->irq_enable3.val);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN4, irq_data->irq_enable4.val);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN5, irq_data->irq_enable5.val);
	return 0;
}

static int __devinit rt5025_irq_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_platform_data *pdata = chip->dev->platform_data;
	struct rt5025_irq_info *ii;
	printk("%s,line=%d\n", __func__,__LINE__);	

	RTINFO("\n");
	ii = kzalloc(sizeof(*ii), GFP_KERNEL);
	if (!ii)
		return -ENOMEM;

	ii->i2c = chip->i2c;
	ii->dev = &pdev->dev;
	ii->chip = chip;
	ii->intr_pin = pdata->intr_pin;
	ii->irq = gpio_to_irq(pdata->intr_pin);
	if (pdata->cb)
		ii->event_cb = pdata->cb;

	rt5025_irq_reg_init(ii, pdata->irq_data);
	rt5025_interrupt_init(ii);

	platform_set_drvdata(pdev, ii);
	return 0;
}

static int __devexit rt5025_irq_remove(struct platform_device *pdev)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	rt5025_interrupt_deinit(ii);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver rt5025_irq_driver = 
{
	.driver = {
		.name = RT5025_DEVICE_NAME "-irq",
		.owner = THIS_MODULE,
	},
	.probe = rt5025_irq_probe,
	.remove = __devexit_p(rt5025_irq_remove),
};

static int __init rt5025_irq_init(void)
{
	return platform_driver_register(&rt5025_irq_driver);
}
subsys_initcall_sync(rt5025_irq_init);

static void __exit rt5025_irq_exit(void)
{
	platform_driver_unregister(&rt5025_irq_driver);
}
module_exit(rt5025_irq_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("IRQ driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-irq");
