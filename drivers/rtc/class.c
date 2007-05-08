/*
 * RTC subsystem, base class
 *
 * Copyright (C) 2005 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * class skeleton from drivers/hwmon/hwmon.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>

#include "rtc-core.h"


static DEFINE_IDR(rtc_idr);
static DEFINE_MUTEX(idr_lock);
struct class *rtc_class;

static void rtc_device_release(struct class_device *class_dev)
{
	struct rtc_device *rtc = to_rtc_device(class_dev);
	mutex_lock(&idr_lock);
	idr_remove(&rtc_idr, rtc->id);
	mutex_unlock(&idr_lock);
	kfree(rtc);
}

/**
 * rtc_device_register - register w/ RTC class
 * @dev: the device to register
 *
 * rtc_device_unregister() must be called when the class device is no
 * longer needed.
 *
 * Returns the pointer to the new struct class device.
 */
struct rtc_device *rtc_device_register(const char *name, struct device *dev,
					const struct rtc_class_ops *ops,
					struct module *owner)
{
	struct rtc_device *rtc;
	int id, err;

	if (idr_pre_get(&rtc_idr, GFP_KERNEL) == 0) {
		err = -ENOMEM;
		goto exit;
	}


	mutex_lock(&idr_lock);
	err = idr_get_new(&rtc_idr, NULL, &id);
	mutex_unlock(&idr_lock);

	if (err < 0)
		goto exit;

	id = id & MAX_ID_MASK;

	rtc = kzalloc(sizeof(struct rtc_device), GFP_KERNEL);
	if (rtc == NULL) {
		err = -ENOMEM;
		goto exit_idr;
	}

	rtc->id = id;
	rtc->ops = ops;
	rtc->owner = owner;
	rtc->max_user_freq = 64;
	rtc->class_dev.dev = dev;
	rtc->class_dev.class = rtc_class;
	rtc->class_dev.release = rtc_device_release;

	mutex_init(&rtc->ops_lock);
	spin_lock_init(&rtc->irq_lock);
	spin_lock_init(&rtc->irq_task_lock);

	strlcpy(rtc->name, name, RTC_DEVICE_NAME_SIZE);
	snprintf(rtc->class_dev.class_id, BUS_ID_SIZE, "rtc%d", id);

	err = class_device_register(&rtc->class_dev);
	if (err)
		goto exit_kfree;

	rtc_dev_add_device(rtc);

	dev_info(dev, "rtc core: registered %s as %s\n",
			rtc->name, rtc->class_dev.class_id);

	return rtc;

exit_kfree:
	kfree(rtc);

exit_idr:
	mutex_lock(&idr_lock);
	idr_remove(&rtc_idr, id);
	mutex_unlock(&idr_lock);

exit:
	dev_err(dev, "rtc core: unable to register %s, err = %d\n",
			name, err);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(rtc_device_register);


/**
 * rtc_device_unregister - removes the previously registered RTC class device
 *
 * @rtc: the RTC class device to destroy
 */
void rtc_device_unregister(struct rtc_device *rtc)
{
	if (class_device_get(&rtc->class_dev) != NULL) {
		mutex_lock(&rtc->ops_lock);
		/* remove innards of this RTC, then disable it, before
		 * letting any rtc_class_open() users access it again
		 */
		rtc_dev_del_device(rtc);
		class_device_unregister(&rtc->class_dev);
		rtc->ops = NULL;
		mutex_unlock(&rtc->ops_lock);
		class_device_put(&rtc->class_dev);
	}
}
EXPORT_SYMBOL_GPL(rtc_device_unregister);

int rtc_interface_register(struct class_interface *intf)
{
	intf->class = rtc_class;
	return class_interface_register(intf);
}
EXPORT_SYMBOL_GPL(rtc_interface_register);

static int __init rtc_init(void)
{
	rtc_class = class_create(THIS_MODULE, "rtc");
	if (IS_ERR(rtc_class)) {
		printk(KERN_ERR "%s: couldn't create class\n", __FILE__);
		return PTR_ERR(rtc_class);
	}
	rtc_dev_init();
	return 0;
}

static void __exit rtc_exit(void)
{
	rtc_dev_exit();
	class_destroy(rtc_class);
}

subsys_initcall(rtc_init);
module_exit(rtc_exit);

MODULE_AUTHOR("Alessandro Zummo <a.zummo@towertech.it>");
MODULE_DESCRIPTION("RTC class support");
MODULE_LICENSE("GPL");
