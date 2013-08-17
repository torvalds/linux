/*
 * exynos-usb-switch.c - USB switch driver for Exynos
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 * Yulgon Kim <yulgon.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>

#include <plat/devs.h>
#include <plat/ehci.h>
#include <mach/usb-switch.h>

#include <mach/regs-clock.h>

#include "../gadget/s3c_udc.h"
#include "exynos-usb-switch.h"

#define DRIVER_DESC "Exynos USB Switch Driver"
#define SWITCH_WAIT_TIME	500
#define WAIT_TIMES		10

static const char switch_name[] = "exynos_usb_switch";
static struct exynos_usb_switch *our_switch;

#if defined(CONFIG_BATTERY_SAMSUNG)
void exynos_usb_cable_connect(void)
{
	samsung_cable_check_status(1);
}

void exynos_usb_cable_disconnect(void)
{
	samsung_cable_check_status(0);
}
#endif

static int is_host_detect(struct exynos_usb_switch *usb_switch)
{
	if (!usb_switch->gpio_host_detect)
		return 0;
	return !gpio_get_value(usb_switch->gpio_host_detect);
}

static int is_device_detect(struct exynos_usb_switch *usb_switch)
{
	if (!usb_switch->gpio_device_detect)
		return 0;
	return gpio_get_value(usb_switch->gpio_device_detect);
}

static void set_host_vbus(struct exynos_usb_switch *usb_switch, int value)
{
	gpio_set_value(usb_switch->gpio_host_vbus, value);
}

static int exynos_change_usb_mode(struct exynos_usb_switch *usb_switch,
				enum usb_cable_status mode)
{
	struct s3c_udc *udc;
	unsigned long cur_mode = usb_switch->connect;
	int ret = 0;

	if (test_bit(USB_DEVICE_ATTACHED, &cur_mode) ||
	    test_bit(USB_HOST_ATTACHED, &cur_mode)) {
		if (mode == USB_DEVICE_ATTACHED ||
			mode == USB_HOST_ATTACHED) {
			printk(KERN_DEBUG "Skip request %d, current %lu\n",
				mode, cur_mode);
			return -EPERM;
		}
	}

	if (!test_bit(USB_DEVICE_ATTACHED, &cur_mode) &&
			mode == USB_DEVICE_DETACHED) {
		printk(KERN_DEBUG "Skip request %d, current %lu\n",
			mode, cur_mode);
		return -EPERM;
	} else if (!test_bit(USB_HOST_ATTACHED, &cur_mode) &&
			mode == USB_HOST_DETACHED) {
		printk(KERN_DEBUG "Skip request %d, current %lu\n",
			mode, cur_mode);
		return -EPERM;
	}

	switch (mode) {
	case USB_DEVICE_DETACHED:
		if (test_bit(USB_HOST_ATTACHED, &cur_mode)) {
			printk(KERN_ERR "Abnormal request %d, current %lu\n",
				mode, cur_mode);
			return -EPERM;
		}
		udc = dev_get_drvdata(usb_switch->s3c_udc_dev);
		if (udc && udc->gadget.ops && udc->gadget.ops->vbus_session)
			udc->gadget.ops->vbus_session(&udc->gadget, 0);
		clear_bit(USB_DEVICE_ATTACHED, &usb_switch->connect);
		break;
	case USB_DEVICE_ATTACHED:
		udc = dev_get_drvdata(usb_switch->s3c_udc_dev);
		if (udc && udc->gadget.ops && udc->gadget.ops->vbus_session)
			udc->gadget.ops->vbus_session(&udc->gadget, 1);
		set_bit(USB_DEVICE_ATTACHED, &usb_switch->connect);
		break;
	case USB_HOST_DETACHED:
		if (test_bit(USB_DEVICE_ATTACHED, &cur_mode)) {
			printk(KERN_ERR "Abnormal request %d, current %lu\n",
				mode, cur_mode);
			return -EPERM;
		}
		if (usb_switch->ohci_dev)
			pm_runtime_put(usb_switch->ohci_dev);
		if (usb_switch->ehci_dev)
			pm_runtime_put(usb_switch->ehci_dev);
		if (usb_switch->gpio_host_vbus)
			set_host_vbus(usb_switch, 0);

#if defined(CONFIG_BATTERY_SAMSUNG)
		exynos_usb_cable_disconnect();
#endif
		clear_bit(USB_HOST_ATTACHED, &usb_switch->connect);
		break;
	case USB_HOST_ATTACHED:
#if defined(CONFIG_BATTERY_SAMSUNG)
		exynos_usb_cable_connect();
#endif
		if (usb_switch->gpio_host_vbus)
			set_host_vbus(usb_switch, 1);

		if (usb_switch->ehci_dev)
			pm_runtime_get_sync(usb_switch->ehci_dev);
		if (usb_switch->ohci_dev)
			pm_runtime_get_sync(usb_switch->ohci_dev);
		set_bit(USB_HOST_ATTACHED, &usb_switch->connect);
		break;
	default:
		printk(KERN_ERR "Does not changed\n");
	}
	printk(KERN_ERR "usb cable = %d\n", mode);

	return ret;
}

static void exynos_usb_switch_worker(struct work_struct *work)
{
	struct exynos_usb_switch *usb_switch =
		container_of(work, struct exynos_usb_switch, switch_work);
	int cnt = 0;

	mutex_lock(&usb_switch->mutex);
	/* If already device detached or host_detected, */
	if (!is_device_detect(usb_switch) || is_host_detect(usb_switch))
		goto done;
	if (!usb_switch->ehci_dev || !usb_switch->ohci_dev)
		goto detect;

	while (!pm_runtime_suspended(usb_switch->ehci_dev) ||
		!pm_runtime_suspended(usb_switch->ohci_dev)) {

		mutex_unlock(&usb_switch->mutex);
		msleep(SWITCH_WAIT_TIME);
		mutex_lock(&usb_switch->mutex);

		/* If already device detached or host_detected, */
		if (!is_device_detect(usb_switch) || is_host_detect(usb_switch))
			goto done;

		if (cnt++ > WAIT_TIMES) {
			printk(KERN_ERR "%s:device not attached by host\n",
				__func__);
			goto done;
		}

	}

	if (cnt > 1)
		printk(KERN_INFO "Device wait host power during %d\n", (cnt-1));
detect:
	/* Check Device, VBUS PIN high active */
	exynos_change_usb_mode(usb_switch, USB_DEVICE_ATTACHED);
done:
	mutex_unlock(&usb_switch->mutex);
}

static irqreturn_t exynos_host_detect_thread(int irq, void *data)
{
	struct exynos_usb_switch *usb_switch = data;

	mutex_lock(&usb_switch->mutex);

	if (is_host_detect(usb_switch))
		exynos_change_usb_mode(usb_switch, USB_HOST_ATTACHED);
	else
		exynos_change_usb_mode(usb_switch, USB_HOST_DETACHED);

	mutex_unlock(&usb_switch->mutex);

	return IRQ_HANDLED;
}

static irqreturn_t exynos_device_detect_thread(int irq, void *data)
{
	struct exynos_usb_switch *usb_switch = data;

	mutex_lock(&usb_switch->mutex);

	/* Debounce connect delay */
	msleep(20);

	if (is_host_detect(usb_switch))
		printk(KERN_DEBUG "Not expected situation\n");
	else if (is_device_detect(usb_switch)) {
		if (usb_switch->gpio_host_vbus)
			exynos_change_usb_mode(usb_switch, USB_DEVICE_ATTACHED);
		else
			queue_work(usb_switch->workqueue, &usb_switch->switch_work);
	} else {
		/* VBUS PIN low */
		exynos_change_usb_mode(usb_switch, USB_DEVICE_DETACHED);
	}

	mutex_unlock(&usb_switch->mutex);

	return IRQ_HANDLED;
}

static int exynos_usb_status_init(struct exynos_usb_switch *usb_switch)
{
	mutex_lock(&usb_switch->mutex);

	/* 2.0 USB */
	if (is_host_detect(usb_switch))
		exynos_change_usb_mode(usb_switch, USB_HOST_ATTACHED);
	else if (is_device_detect(usb_switch)) {
		if (usb_switch->gpio_host_vbus)
			exynos_change_usb_mode(usb_switch,
				USB_DEVICE_ATTACHED);
		else
			queue_work(usb_switch->workqueue,
				&usb_switch->switch_work);
	}

	mutex_unlock(&usb_switch->mutex);

	return 0;
}

#ifdef CONFIG_PM
static int exynos_usbswitch_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_usb_switch *usb_switch = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s\n", __func__);
	mutex_lock(&usb_switch->mutex);
	if (test_bit(USB_DEVICE_ATTACHED, &usb_switch->connect))
		exynos_change_usb_mode(usb_switch, USB_DEVICE_DETACHED);

	usb_switch->connect = 0;
	mutex_unlock(&usb_switch->mutex);

	return 0;
}

static int exynos_usbswitch_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos_usb_switch *usb_switch = platform_get_drvdata(pdev);

	dev_dbg(dev, "%s\n", __func__);
	exynos_usb_status_init(usb_switch);

	return 0;
}
#else
#define exynos_usbswitch_suspend	NULL
#define exynos_usbswitch_resume		NULL
#endif

static int __devinit exynos_usbswitch_probe(struct platform_device *pdev)
{
	struct s5p_usbswitch_platdata *pdata = dev_get_platdata(&pdev->dev);
	struct device *dev = &pdev->dev;
	struct exynos_usb_switch *usb_switch;
	int irq;
	int ret = 0;

	usb_switch = kzalloc(sizeof(struct exynos_usb_switch), GFP_KERNEL);
	if (!usb_switch) {
		ret = -ENOMEM;
		return ret;
	}

	our_switch = usb_switch;
	mutex_init(&usb_switch->mutex);
	usb_switch->workqueue = create_singlethread_workqueue("usb_switch");
	INIT_WORK(&usb_switch->switch_work, exynos_usb_switch_worker);
	usb_switch->gpio_host_detect = pdata->gpio_host_detect;
	usb_switch->gpio_device_detect = pdata->gpio_device_detect;
	usb_switch->gpio_host_vbus = pdata->gpio_host_vbus;

	usb_switch->ehci_dev = pdata->ehci_dev;
	usb_switch->ohci_dev = pdata->ohci_dev;

	usb_switch->s3c_udc_dev = pdata->s3c_udc_dev;

	/* USB Device detect IRQ */
	irq = platform_get_irq(pdev, 1);
	if (irq > 0 && usb_switch->s3c_udc_dev) {
		ret = request_threaded_irq(irq, NULL,
				exynos_device_detect_thread,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"DEVICE_DETECT", usb_switch);
		if (ret) {
			dev_err(dev, "Failed to request device irq %d\n", irq);
			goto fail;
		}
		usb_switch->device_detect_irq = irq;
	} else if (usb_switch->s3c_udc_dev)
		exynos_change_usb_mode(usb_switch, USB_DEVICE_ATTACHED);
	else
		dev_info(dev, "Disable device detect IRQ\n");

	/* USB Host detect IRQ */
	irq = platform_get_irq(pdev, 0);
	if (irq > 0 && (usb_switch->ehci_dev || usb_switch->ohci_dev)) {
		ret = request_threaded_irq(irq, NULL,
				exynos_host_detect_thread,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"HOST_DETECT", usb_switch);
		if (ret) {
			dev_err(dev, "Failed to request host irq %d\n", irq);
			goto fail_gpio_device_detect;
		}
		usb_switch->host_detect_irq = irq;
	} else if (usb_switch->ehci_dev || usb_switch->ohci_dev)
		exynos_change_usb_mode(usb_switch, USB_HOST_ATTACHED);
	else
		dev_info(dev, "Disable host detect IRQ\n");

	exynos_usb_status_init(usb_switch);

	platform_set_drvdata(pdev, usb_switch);

	return ret;

fail_gpio_device_detect:
	free_irq(usb_switch->device_detect_irq, usb_switch);
fail:
	cancel_work_sync(&usb_switch->switch_work);
	destroy_workqueue(usb_switch->workqueue);
	mutex_destroy(&usb_switch->mutex);
	kfree(usb_switch);
	return ret;
}

static int __devexit exynos_usbswitch_remove(struct platform_device *pdev)
{
	struct exynos_usb_switch *usb_switch = platform_get_drvdata(pdev);

	free_irq(usb_switch->host_detect_irq, usb_switch);
	free_irq(usb_switch->device_detect_irq, usb_switch);
	platform_set_drvdata(pdev, 0);

	cancel_work_sync(&usb_switch->switch_work);
	destroy_workqueue(usb_switch->workqueue);
	mutex_destroy(&usb_switch->mutex);
	kfree(usb_switch);

	return 0;
}

static const struct dev_pm_ops exynos_usbswitch_pm_ops = {
	.suspend                = exynos_usbswitch_suspend,
	.resume                 = exynos_usbswitch_resume,
};

static struct platform_driver exynos_usbswitch_driver = {
	.probe		= exynos_usbswitch_probe,
	.remove		= __devexit_p(exynos_usbswitch_remove),
	.driver		= {
		.name	= "exynos-usb-switch",
		.owner	= THIS_MODULE,
		.pm	= &exynos_usbswitch_pm_ops,
	},
};

static int __init exynos_usbswitch_init(void)
{
	int ret;

	ret = platform_device_register(&s5p_device_usbswitch);
	if (ret < 0)
		return ret;

	ret = platform_driver_register(&exynos_usbswitch_driver);
	if (!ret)
		printk(KERN_INFO "%s: " DRIVER_DESC "\n", switch_name);

	return ret;
}
late_initcall(exynos_usbswitch_init);

static void __exit exynos_usbswitch_exit(void)
{
	platform_driver_unregister(&exynos_usbswitch_driver);
}
module_exit(exynos_usbswitch_exit);

MODULE_DESCRIPTION("Exynos USB switch driver");
MODULE_AUTHOR("<yulgon.kim@samsung.com>");
MODULE_LICENSE("GPL");
