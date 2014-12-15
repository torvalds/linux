/*
 * Amlogic Watchdog Timer Driver for Meson Chip
 *
 * Author: Bobby Yang <bo.yang@amlogic.com>
 *
 * Copyright (C) 2011 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/watchdog.h>
#include <linux/of.h>
#include <linux/amlogic/aml_wdt.h>
#include <uapi/linux/reboot.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/reboot.h>

struct aml_wdt_dev *awdtv=NULL;

static unsigned int read_watchdog_time(void)
{
	printk(KERN_INFO "** read watchdog time\n");
	return aml_read_reg32(P_WATCHDOG_TC)&((1 << WATCHDOG_ENABLE_BIT)-1);	
}

static int aml_wdt_start(struct watchdog_device *wdog)
{
	struct aml_wdt_dev *wdev = watchdog_get_drvdata(wdog);
	mutex_lock(&wdev->lock);
	if(wdog->timeout==0xffffffff)
		enable_watchdog(wdev->default_timeout * wdev->one_second);
	else
		enable_watchdog(wdog->timeout* wdev->one_second);
	mutex_unlock(&wdev->lock);
#if 0
	if(wdev->boot_queue)
		cancel_delayed_work(&wdev->boot_queue);
#endif
	return 0;
}

static int aml_wdt_stop(struct watchdog_device *wdog)
{
	struct aml_wdt_dev *wdev = watchdog_get_drvdata(wdog);
	mutex_lock(&wdev->lock);
	disable_watchdog();
	mutex_unlock(&wdev->lock);
	return 0;
}

static int aml_wdt_ping(struct watchdog_device *wdog)
{
	struct aml_wdt_dev *wdev = watchdog_get_drvdata(wdog);
	mutex_lock(&wdev->lock);
	reset_watchdog();
	mutex_unlock(&wdev->lock);

	return 0;
}

static int aml_wdt_set_timeout(struct watchdog_device *wdog,
				unsigned int timeout)
{
	struct aml_wdt_dev *wdev = watchdog_get_drvdata(wdog);

	mutex_lock(&wdev->lock);
	wdog->timeout = timeout;
	wdev->timeout = timeout;
	mutex_unlock(&wdev->lock);

	return 0;
}
unsigned int aml_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct aml_wdt_dev *wdev = watchdog_get_drvdata(wdog);
	unsigned int timeleft;
	mutex_lock(&wdev->lock);
	timeleft=read_watchdog_time();
	mutex_unlock(&wdev->lock);
	return timeleft/wdev->one_second;
}

static void boot_moniter_work(struct work_struct *work)
{
	struct aml_wdt_dev *wdev=container_of(work,struct aml_wdt_dev,boot_queue.work);
	reset_watchdog();
	mod_delayed_work(system_freezable_wq, &wdev->boot_queue,
				 round_jiffies(msecs_to_jiffies(wdev->reset_watchdog_time*1000)));
}

static const struct watchdog_info aml_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING,
	.identity = "aml Watchdog",
};

static const struct watchdog_ops aml_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= aml_wdt_start,
	.stop		= aml_wdt_stop,
	.ping		= aml_wdt_ping,
	.set_timeout	= aml_wdt_set_timeout,
	.get_timeleft   = aml_wdt_get_timeleft,
};
void aml_init_pdata(struct aml_wdt_dev *wdev)
{
	int ret;
	ret=of_property_read_u32(wdev->dev->of_node, "default_timeout", &wdev->default_timeout);
	if(ret){
		dev_err(wdev->dev, "dt probe default_timeout failed: %d using default value\n", ret);
		wdev->default_timeout=5;
	}
	ret=of_property_read_u32(wdev->dev->of_node, "reset_watchdog_method", &wdev->reset_watchdog_method);
	if(ret){
		dev_err(wdev->dev, "dt probe reset_watchdog_method failed: %d using default value\n", ret);
		wdev->reset_watchdog_method=1;
	}
	ret=of_property_read_u32(wdev->dev->of_node, "reset_watchdog_time", &wdev->reset_watchdog_time);
	if(ret){
		dev_err(wdev->dev, "dt probe reset_watchdog_time failed: %d using default value\n", ret);
		wdev->reset_watchdog_time=2;
	}
	
	ret=of_property_read_u32(wdev->dev->of_node, "shutdown_timeout", &wdev->shutdown_timeout);
	if(ret){
		dev_err(wdev->dev, "dt probe shutdown_timeout failed: %d using default value\n", ret);
		wdev->shutdown_timeout=10;
	}
	
	ret=of_property_read_u32(wdev->dev->of_node, "firmware_timeout", &wdev->firmware_timeout);
	if(ret){
		dev_err(wdev->dev, "dt probe firmware_timeout failed: %d using default value\n", ret);
		wdev->firmware_timeout=6;
	}
	
	ret=of_property_read_u32(wdev->dev->of_node, "suspend_timeout", &wdev->suspend_timeout);
	if(ret){
		dev_err(wdev->dev, "dt probe suspend_timeout failed: %d using default value\n", ret);
		wdev->suspend_timeout=6;
	}
	
	wdev->one_second=WDT_ONE_SECOND;
	wdev->max_timeout=MAX_TIMEOUT;
	wdev->min_timeout=MIN_TIMEOUT;
	
	printk("one-secod=%d,min_timeout=%d,max_timeout=%d,default_timeout=%d,reset_watchdog_method=%d,reset_watchdog_time=%d,shutdown_timeout=%d,firmware_timeout=%d,suspend_timeout=%d\n",
		wdev->one_second,wdev->min_timeout,wdev->max_timeout,
		wdev->default_timeout,wdev->reset_watchdog_method,
		wdev->reset_watchdog_time,wdev->shutdown_timeout,
		wdev->firmware_timeout,wdev->suspend_timeout);

	return;
}
static int aml_wtd_pm_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	
	if (event == PM_SUSPEND_PREPARE) {
		printk("set watch dog suspend timeout %d seconds\n",awdtv->suspend_timeout);
		enable_watchdog(awdtv->suspend_timeout*awdtv->one_second);
	} 
	if (event == PM_POST_SUSPEND){
		printk("resume watch dog finish\n");
		if(awdtv->timeout==0xffffffff)
			enable_watchdog(awdtv->default_timeout * awdtv->one_second);
		else
			enable_watchdog(awdtv->timeout* awdtv->one_second);
	}
	return NOTIFY_OK;
}
static int aml_wtd_reboot_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	if (event == SYS_POWER_OFF) {
		printk("set watch dog shut down timeout %d seconds\n",awdtv->suspend_timeout);
		enable_watchdog(awdtv->shutdown_timeout*awdtv->one_second);
		aml_write_reg32(P_AO_RTI_STATUS_REG1, MESON_CHARGING_REBOOT);
	} 
	return NOTIFY_OK;
}


static struct notifier_block aml_wdt_pm_notifier = {
	.notifier_call = aml_wtd_pm_notify,
};
static struct notifier_block aml_wdt_reboot_notifier = {
	.notifier_call = aml_wtd_reboot_notify,
};

static int aml_wdt_probe(struct platform_device *pdev)
{
	struct watchdog_device *aml_wdt;
	struct aml_wdt_dev *wdev;
	int ret;
	aml_wdt = devm_kzalloc(&pdev->dev, sizeof(*aml_wdt), GFP_KERNEL);
	if (!aml_wdt)
		return -ENOMEM;

	wdev = devm_kzalloc(&pdev->dev, sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return -ENOMEM;
	wdev->dev		= &pdev->dev;
	mutex_init(&wdev->lock);
	aml_init_pdata(wdev);

	aml_wdt->info	      = &aml_wdt_info;
	aml_wdt->ops	      = &aml_wdt_ops;
	aml_wdt->min_timeout = wdev->min_timeout;
	aml_wdt->max_timeout = wdev->max_timeout;
	aml_wdt->timeout=0xffffffff;
	wdev->timeout=0xffffffff;

	watchdog_set_drvdata(aml_wdt, wdev);
	platform_set_drvdata(pdev, aml_wdt);
	if(wdev->reset_watchdog_method==1)
	{
		
		INIT_DELAYED_WORK(&wdev->boot_queue, boot_moniter_work);
		mod_delayed_work(system_freezable_wq, &wdev->boot_queue,
					 round_jiffies(msecs_to_jiffies(wdev->reset_watchdog_time*1000)));
		enable_watchdog(wdev->default_timeout * wdev->one_second);
		printk("creat work queue for watch dog\n");
	}
	ret = watchdog_register_device(aml_wdt);
	if (ret) 
		return ret;
	awdtv=wdev;
	register_pm_notifier(&aml_wdt_pm_notifier);
	register_reboot_notifier(&aml_wdt_reboot_notifier);
	pr_info("AML Watchdog Timer probed done \n");

	return 0;
}

static void aml_wdt_shutdown(struct platform_device *pdev)
{
	struct watchdog_device *wdog = platform_get_drvdata(pdev);
	struct aml_wdt_dev *wdev = watchdog_get_drvdata(wdog);
	if(wdev->reset_watchdog_method==1)
		cancel_delayed_work(&wdev->boot_queue);
	reset_watchdog();
}

static int aml_wdt_remove(struct platform_device *pdev)
{
	struct watchdog_device *wdog = platform_get_drvdata(pdev);
	aml_wdt_stop(wdog);
	return 0;
}

#ifdef	CONFIG_PM
static int aml_wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	reset_watchdog();
	return 0;
}

static int aml_wdt_resume(struct platform_device *pdev)
{
	reset_watchdog();
	return 0;
}

#else
#define	aml_wdt_suspend	NULL
#define	aml_wdt_resume		NULL
#endif

static const struct of_device_id aml_wdt_of_match[] = {
	{ .compatible = "amlogic,aml-wdt", },
	{},
};
MODULE_DEVICE_TABLE(of, aml_wdt_of_match);

static struct platform_driver aml_wdt_driver = {
	.probe		= aml_wdt_probe,
	.remove		= aml_wdt_remove,
	.shutdown	= aml_wdt_shutdown,
	.suspend	= aml_wdt_suspend,
	.resume		= aml_wdt_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "aml_wdt",
		.of_match_table = aml_wdt_of_match,
	},
};
static int __init aml_wdt_driver_init(void) 
{
	printk("%s,%d\n",__func__,__LINE__);
	disable_watchdog();
	return platform_driver_register(&(aml_wdt_driver)); 
} 
module_init(aml_wdt_driver_init); 
static void __exit aml_wdt_driver_exit(void) 
{ 
	platform_driver_unregister(&(aml_wdt_driver) ); 
} 
module_exit(aml_wdt_driver_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:aml_wdt");


