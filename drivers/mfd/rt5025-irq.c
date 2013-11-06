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
#include <linux/wakelock.h>
#include <linux/delay.h>

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
	struct wake_lock irq_wake_lock;
	int intr_pin;
	int irq;
	int suspend;
	int acin_cnt;
	int usbin_cnt;
};

static void rt5025_work_func(struct work_struct *work)
{
	struct delayed_work *delayed_work = (struct delayed_work *)container_of(work, struct delayed_work, work);
	struct rt5025_irq_info *ii = (struct rt5025_irq_info *)container_of(delayed_work, struct rt5025_irq_info, delayed_work);
	unsigned char irq_stat[6] = {0};
	unsigned char irq_enable[6] = {0};
	uint32_t chg_event = 0, pwr_event = 0;

	//Add this to prevent i2c xfer before i2c chip is in suspend mode
	if (ii->suspend)
	{
		queue_delayed_work(ii->wq, &ii->delayed_work, msecs_to_jiffies(1));
		return;
	}

	#ifdef CONFIG_POWER_RT5025
	if (!ii->chip->power_info || !ii->chip->jeita_info || !ii->chip->battery_info)
	{
		queue_delayed_work(ii->wq, &ii->delayed_work, msecs_to_jiffies(1));
		return;
	}
	#endif

	#if 0
	if (rt5025_reg_block_read(ii->i2c, RT5025_REG_IRQEN1, 10, irq_stat) >= 0)
	{
	#endif
		/* backup the irq enable bit */
		irq_enable[0] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQEN1);
		irq_enable[1] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQEN2);
		irq_enable[2] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQEN3);
		irq_enable[3] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQEN4);
		irq_enable[4] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQEN5);
		irq_enable[5] = rt5025_reg_read(ii->i2c, RT5025_REG_GAUGEIRQEN);
		#if 1
		rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN2, irq_enable[1]&(~RT5025_CHTERMI_MASK));
		#else
		/* disable all irq enable bit first */
		rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN1, irq_enable[0]&RT5025_ADAPIRQ_MASK);
		rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN2, 0x00);
		rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN3, 0x00);
		rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN4, 0x00);
		rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN5, 0x00);
		rt5025_reg_write(ii->i2c, RT5025_REG_GAUGEIRQEN, 0x00);
		#endif //#if 0
		/* read irq status bit */
		irq_stat[0] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQSTATUS1);
		irq_stat[1] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQSTATUS2);
		irq_stat[2] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQSTATUS3);
		irq_stat[3] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQSTATUS4);
		irq_stat[4] = rt5025_reg_read(ii->i2c, RT5025_REG_IRQSTATUS5);
		irq_stat[5] = rt5025_reg_read(ii->i2c, RT5025_REG_GAUGEIRQFLG);
		RTINFO("irq1->0x%02x, irq2->0x%02x, irq3->0x%02x\n", irq_stat[0], irq_stat[1], irq_stat[2]);
		RTINFO("irq4->0x%02x, irq5->0x%02x, irq6->0x%02x\n", irq_stat[3], irq_stat[4], irq_stat[5]);
		RTINFO("stat value = %02x\n", rt5025_reg_read(ii->i2c, RT5025_REG_CHGSTAT));

		chg_event = irq_stat[0]<<16 | irq_stat[1]<<8 | irq_stat[2];
		pwr_event = irq_stat[3]<<8 | irq_stat[4];
		#ifdef CONFIG_POWER_RT5025
		if ((chg_event & CHARGER_DETECT_MASK))
		{
			if (chg_event & CHG_EVENT_CHTERMI)
			{
				ii->chip->power_info->chg_term++;
				if (ii->chip->power_info->chg_term > 3)
					ii->chip->power_info->chg_term = 4;
			}

			if (chg_event & CHG_EVENT_CHRCHGI)
				ii->chip->power_info->chg_term = 0;

			if (chg_event & (CHG_EVENT_CHSLPI_INAC | CHG_EVENT_CHSLPI_INUSB))
			{
				ii->chip->power_info->chg_term = 0;
				if (chg_event & CHG_EVENT_CHSLPI_INAC)
					ii->acin_cnt = 0;
				if (chg_event & CHG_EVENT_CHSLPI_INUSB)
					ii->usbin_cnt = 0;
				
			}

			if (chg_event & (CHG_EVENT_INAC_PLUGIN | CHG_EVENT_INUSB_PLUGIN))
			{
				RTINFO("acin_cnt %d, usbin_cnt %d\n", ii->acin_cnt, ii->usbin_cnt);
				if (ii->acin_cnt == 0 && ii->usbin_cnt == 0)
				{
					#if 1
					rt5025_charger_reset_and_reinit(ii->chip->power_info);
					rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN1, irq_enable[0]);
					rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN2, irq_enable[1]&(~RT5025_CHTERMI_MASK));
					rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN3, irq_enable[2]);
					#else
					rt5025_set_charging_buck(ii->i2c, 0);
					mdelay(50);
					rt5025_set_charging_buck(ii->i2c, 1);
					mdelay(100);
					rt5025_set_charging_buck(ii->i2c, 0);
					mdelay(50);
					rt5025_set_charging_buck(ii->i2c, 1);
					mdelay(400);
					#endif /* #if 1 */
				}

				if (chg_event & CHG_EVENT_INAC_PLUGIN)
					ii->acin_cnt = 1;
				if (chg_event & CHG_EVENT_INUSB_PLUGIN)
					ii->usbin_cnt = 1;
				RTINFO("acin_cnt %d, usbin_cnt %d\n", ii->acin_cnt, ii->usbin_cnt);
			}

			if (ii->chip->power_info->chg_term <= 3)
				rt5025_power_charge_detect(ii->chip->power_info);

		}
		#endif /* CONFIG_POWER_RT5025 */
		if (ii->event_cb)
		{
			if (chg_event)
				ii->event_cb->charger_event_callback(chg_event);
			if (pwr_event)
				ii->event_cb->power_event_callkback(pwr_event);
		}
	#if 0
	}
	else
		dev_err(ii->dev, "read irq stat io fail\n");
	#endif
	

	#ifdef CONFIG_POWER_RT5025
	if (irq_stat[5] & RT5025_FLG_TEMP)
		rt5025_swjeita_irq_handler(ii->chip->jeita_info, irq_stat[5] & RT5025_FLG_TEMP);
	if (irq_stat[5] & RT5025_FLG_VOLT)
		rt5025_gauge_irq_handler(ii->chip->battery_info, irq_stat[5] & RT5025_FLG_VOLT);
	#endif /* CONFIG_POWER_RT5025 */

	#if 1
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN2, irq_enable[1]);
	#else
	/* restore all irq enable bit */
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN1, irq_enable[0]);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN2, irq_enable[1]);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN3, irq_enable[2]);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN4, irq_enable[3]);
	rt5025_reg_write(ii->i2c, RT5025_REG_IRQEN5, irq_enable[4]);
	if (rt5025_reg_read(ii->i2c, RT5025_REG_GAUGEIRQEN) == 0)
		rt5025_reg_write(ii->i2c, RT5025_REG_GAUGEIRQEN, irq_enable[5]);
	#endif //#if 0

	//enable_irq(ii->irq);
}

static irqreturn_t rt5025_interrupt(int irqno, void *param)
{
	struct rt5025_irq_info *ii = (struct rt5025_irq_info *)param;

	//disable_irq_nosync(ii->irq);
	wake_lock_timeout(&ii->irq_wake_lock, 1*HZ);
	queue_delayed_work(ii->wq, &ii->delayed_work, 0);
	return IRQ_HANDLED;
}

static int __devinit rt5025_interrupt_init(struct rt5025_irq_info* ii)
{
	int ret = 0;

	RTINFO("\n");
	ii->wq = create_workqueue("rt5025_wq");
	INIT_DELAYED_WORK(&ii->delayed_work, rt5025_work_func);

	#if 0
	if (gpio_is_valid(ii->intr_pin))
	{
		ret = gpio_request(ii->intr_pin, "rt5025_interrupt");
		if (ret)
			return ret;

		ret = gpio_direction_input(ii->intr_pin);
		if (ret)
			return ret;
	#endif

		if (request_irq(ii->irq, rt5025_interrupt, IRQ_TYPE_EDGE_FALLING|IRQF_DISABLED, "RT5025_IRQ", ii))
		{
			dev_err(ii->dev, "couldn't allocate IRQ_NO(%d) !\n", ii->irq);
			return -EINVAL;
		}
		enable_irq_wake(ii->irq);
		queue_delayed_work(ii->wq, &ii->delayed_work, msecs_to_jiffies(100));
	#if 0

		if (!gpio_get_value(ii->intr_pin))
		{
			//disable_irq_nosync(ii->irq);
			queue_delayed_work(ii->wq, &ii->delayed_work, 0);
		}
	}
	else
		return -EINVAL;
	#endif

	return ret;
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

	RTINFO("\n");
	ii = kzalloc(sizeof(*ii), GFP_KERNEL);
	if (!ii)
		return -ENOMEM;

	ii->i2c = chip->i2c;
	ii->dev = &pdev->dev;
	ii->chip = chip;
	ii->intr_pin = pdata->intr_pin;
	ii->irq = chip->irq;//gpio_to_irq(pdata->intr_pin);
	if (pdata->cb)
		ii->event_cb = pdata->cb;
	wake_lock_init(&ii->irq_wake_lock, WAKE_LOCK_SUSPEND, "rt_irq_wake");

	rt5025_irq_reg_init(ii, pdata->irq_data);
	rt5025_interrupt_init(ii);

	platform_set_drvdata(pdev, ii);
	RTINFO("\n");
	return 0;
}

static int __devexit rt5025_irq_remove(struct platform_device *pdev)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	wake_lock_destroy(&ii->irq_wake_lock);
	rt5025_interrupt_deinit(ii);
	platform_set_drvdata(pdev, NULL);
	kfree(ii);
	RTINFO("\n");
	return 0;
}

static void rt5025_irq_shutdown(struct platform_device *pdev)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	if (ii->irq)
		free_irq(ii->irq, ii);

	if (ii->wq)
	{
		cancel_delayed_work_sync(&ii->delayed_work);
		flush_workqueue(ii->wq);
	}
	RTINFO("\n");
}

static int rt5025_irq_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	RTINFO("\n");
	ii->suspend = 1;
	return 0;
}

static int rt5025_irq_resume(struct platform_device *pdev)
{
	struct rt5025_irq_info *ii = platform_get_drvdata(pdev);

	RTINFO("\n");
	ii->suspend = 0;
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
	.shutdown = rt5025_irq_shutdown,
	.suspend = rt5025_irq_suspend,
	.resume = rt5025_irq_resume,
};

static int __init rt5025_irq_init(void)
{
	return platform_driver_register(&rt5025_irq_driver);
}
module_init(rt5025_irq_init);

static void __exit rt5025_irq_exit(void)
{
	platform_driver_unregister(&rt5025_irq_driver);
}
module_exit(rt5025_irq_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("IRQ driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEVICE_NAME "-irq");
MODULE_VERSION(RT5025_DRV_VER);
