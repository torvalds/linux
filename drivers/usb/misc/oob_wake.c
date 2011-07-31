/*
 * Copyright (C) 2011 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/oob_wake.h>
#ifdef CONFIG_HAS_WAKELOCK
#include <linux/wakelock.h>
#endif /* CONFIG_HAS_WAKELOCK */

#define GPIO_MAX_NAME 30

/* list of registered interfaces (intf_entry) */
static LIST_HEAD(intf_list);
static DEFINE_MUTEX(intf_list_lock);

struct intf_entry {
	struct usb_interface *data;
	struct list_head node;
};

struct oob_wake_info {
	unsigned int gpio;
	char name[GPIO_MAX_NAME];
	__le16 vendor;
	__le16 product;
#ifdef CONFIG_HAS_WAKELOCK
	struct wake_lock wake_lock;
#endif /* CONFIG_HAS_WAKELOCK */
};

/* adds a single usb interface to the list to be woken up by the
 * out of band interrupt.  Only "unique wake events" are added.
 * Meaning interfaces that coming from the same device and bus
 * will be considered equivalent and only the first will be added.
 */
int oob_wake_register(struct usb_interface *intf)
{
	bool unique_wake_event = 1;
	struct list_head *ptr;
	struct usb_device *udev;
	struct usb_device *new_udev;
	struct intf_entry *entry;
	struct intf_entry *new_entry = kzalloc(sizeof(struct intf_entry),
		GFP_KERNEL);

	if (!new_entry) {
		return -ENOMEM;
	}
	new_entry->data = intf;
	new_udev = interface_to_usbdev(intf);

	mutex_lock(&intf_list_lock);
	list_for_each(ptr, &intf_list) {
		entry = list_entry(ptr, struct intf_entry, node);
		udev = interface_to_usbdev(entry->data);
		if ((udev->devnum == new_udev->devnum) &&
		    (udev->bus->busnum == new_udev->bus->busnum)) {
			unique_wake_event = 0;
			break;
		}
	}

	if (unique_wake_event)
		list_add(&new_entry->node, &intf_list);
	mutex_unlock(&intf_list_lock);

	if (!unique_wake_event)
		kfree(new_entry);

	return 0;
}
EXPORT_SYMBOL(oob_wake_register);

/* removes the given interface from the list to be woken up by
   the out of band interrupt */
void oob_wake_unregister(struct usb_interface *intf)
{
	struct list_head *ptr;
	struct list_head *next;
	struct intf_entry *entry;

	mutex_lock(&intf_list_lock);
	list_for_each_safe(ptr, next, &intf_list) {
		entry = list_entry(ptr, struct intf_entry, node);
		if (intf == entry->data) {
			list_del(&entry->node);
			kfree(entry);
			break;
		}
	}
	mutex_unlock(&intf_list_lock);
}
EXPORT_SYMBOL(oob_wake_unregister);

/* wake up the usb bus if needed */
static void wake_interface(struct usb_interface *intf)
{
	pr_debug("%s: called\n", __func__);

	device_lock(&intf->dev);

	if (intf->dev.power.status >= DPM_OFF ||
			intf->dev.power.status == DPM_RESUMING) {
		device_unlock(&intf->dev);
		return;
	}

	if (usb_autopm_get_interface(intf) == 0)
		usb_autopm_put_interface_async(intf);

	device_unlock(&intf->dev);
}

/* Wake the interface for the associated device (vendor/product) */
static irqreturn_t oob_wake_fn(int irq, void *data)
{
	struct list_head *ptr;
	struct intf_entry *entry;
	struct usb_device *udev;
	struct oob_wake_info *info = (struct oob_wake_info *) data;
	pr_debug("%s: irq (%d) fired\n", __func__, gpio_to_irq(info->gpio));

	mutex_lock(&intf_list_lock);
	list_for_each(ptr, &intf_list) {
		entry = list_entry(ptr, struct intf_entry, node);
		udev = interface_to_usbdev(entry->data);
		pr_debug("%s: %04x:%04x\n", __func__, udev->descriptor.idVendor,
				udev->descriptor.idProduct);
		if ((udev->descriptor.idVendor == info->vendor) &&
				(udev->descriptor.idProduct == info->product))
			wake_interface(entry->data);
	}
	mutex_unlock(&intf_list_lock);

#ifdef CONFIG_HAS_WAKELOCK
	pr_debug("%s: release wakelock %s\n", __func__, info->wake_lock.name);
	wake_unlock(&info->wake_lock);
#endif /* CONFIG_HAS_WAKELOCK */

	return IRQ_HANDLED;
}

static irqreturn_t oob_wake_isr(int irq, void *data)
{
	struct oob_wake_info *info = (struct oob_wake_info *) data;

#ifdef CONFIG_HAS_WAKELOCK
	pr_debug("%s: take wakelock %s\n", __func__, info->wake_lock.name);
	wake_lock(&info->wake_lock);
#endif /* CONFIG_HAS_WAKELOCK */

	return IRQ_WAKE_THREAD;
}

static int __devinit oob_wake_probe(struct platform_device *pdev)
{
	struct oob_wake_platform_data *pdata = pdev->dev.platform_data;
	struct oob_wake_info *info;
	int irq;
	int err = 0;

	pr_info("%s: %s\n", __func__, dev_name(&pdev->dev));
	info = kzalloc(sizeof(struct oob_wake_info), GFP_KERNEL);
	if (!info) {
		return -ENOMEM;
	}

	info->gpio = pdata->gpio;
	info->vendor = pdata->vendor;
	info->product = pdata->product;

	platform_set_drvdata(pdev, info);

#ifdef CONFIG_HAS_WAKELOCK
	wake_lock_init(&info->wake_lock, WAKE_LOCK_SUSPEND,
			dev_name(&pdev->dev));
#endif /* CONFIG_HAS_WAKELOCK */

	snprintf(info->name, GPIO_MAX_NAME, "%s-%s",
		dev_name(&pdev->dev), "host-wake");
	err = gpio_request(info->gpio, info->name);
	if (err) {
		pr_err("%s: error requesting host wake gpio\n", __func__);
		return err;
	}
	gpio_direction_input(info->gpio);

	irq = gpio_to_irq(info->gpio);
	err = request_threaded_irq(irq, oob_wake_isr, oob_wake_fn,
		IRQ_TYPE_EDGE_FALLING, info->name, info);
	if (err) {
		pr_err("%s: error requesting host wake irq\n", __func__);
		gpio_free(info->gpio);
		return err;
	}

	err = enable_irq_wake(irq);
	if (err) {
		pr_err("%s: request host_wake irq (%d) %s failed\n",
			__func__, irq, info->name);
		free_irq(irq, info);
		gpio_free(info->gpio);
		return err;
	}
	gpio_export(info->gpio, false);

	return 0;
}

static void __devexit oob_wake_shutdown(struct platform_device *pdev)
{
	struct list_head *ptr;
	struct list_head *next;
	struct intf_entry *entry;
	struct oob_wake_info *info = platform_get_drvdata(pdev);

	pr_info("%s: %s\n", __func__, dev_name(&pdev->dev));
	if (info) {
		disable_irq_wake(gpio_to_irq(info->gpio));
		free_irq(gpio_to_irq(info->gpio), info);
		gpio_free(info->gpio);
#ifdef CONFIG_HAS_WAKELOCK
		wake_lock_destroy(&info->wake_lock);
#endif /* CONFIG_HAS_WAKELOCK */

		mutex_lock(&intf_list_lock);
		list_for_each_safe(ptr, next, &intf_list) {
			entry = list_entry(ptr, struct intf_entry, node);
			list_del(&entry->node);
			kfree(entry);
		}
		mutex_unlock(&intf_list_lock);
		kfree(info);

		platform_set_drvdata(pdev, NULL);
	}
}

static struct platform_driver oob_wake_driver = {
	.probe = oob_wake_probe,
	.shutdown = __devexit_p(oob_wake_shutdown),
	.driver = {
		.name = "oob-wake",
		.owner = THIS_MODULE,
	},
};

static int __init oob_wake_init(void)
{
	pr_info("%s: initializing %s\n", __func__, oob_wake_driver.driver.name);

	return platform_driver_register(&oob_wake_driver);
}

static void __exit oob_wake_exit(void)
{
	pr_info("%s: exiting %s\n", __func__, oob_wake_driver.driver.name);
	return platform_driver_unregister(&oob_wake_driver);
}

module_init(oob_wake_init);
module_exit(oob_wake_exit);

MODULE_AUTHOR("Jim Wylder <james.wylder@motorola.com>");
MODULE_DESCRIPTION("USB Out-of-Bounds Wake");
MODULE_LICENSE("GPL");
