// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for onboard USB hubs
 *
 * Copyright (c) 2022, Google LLC
 */

#include <linux/device.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/sysfs.h>
#include <linux/usb.h>
#include <linux/usb/hcd.h>
#include <linux/usb/onboard_hub.h>
#include <linux/workqueue.h>

#include "onboard_usb_hub.h"

/*
 * Use generic names, as the actual names might differ between hubs. If a new
 * hub requires more than the currently supported supplies, add a new one here.
 */
static const char * const supply_names[] = {
	"vdd",
	"vdd2",
};

#define MAX_SUPPLIES ARRAY_SIZE(supply_names)

static void onboard_hub_attach_usb_driver(struct work_struct *work);

static struct usb_device_driver onboard_hub_usbdev_driver;
static DECLARE_WORK(attach_usb_driver_work, onboard_hub_attach_usb_driver);

/************************** Platform driver **************************/

struct usbdev_node {
	struct usb_device *udev;
	struct list_head list;
};

struct onboard_hub {
	struct regulator_bulk_data supplies[MAX_SUPPLIES];
	struct device *dev;
	const struct onboard_hub_pdata *pdata;
	struct gpio_desc *reset_gpio;
	bool always_powered_in_suspend;
	bool is_powered_on;
	bool going_away;
	struct list_head udev_list;
	struct mutex lock;
};

static int onboard_hub_power_on(struct onboard_hub *hub)
{
	int err;

	err = regulator_bulk_enable(hub->pdata->num_supplies, hub->supplies);
	if (err) {
		dev_err(hub->dev, "failed to enable supplies: %d\n", err);
		return err;
	}

	fsleep(hub->pdata->reset_us);
	gpiod_set_value_cansleep(hub->reset_gpio, 0);

	hub->is_powered_on = true;

	return 0;
}

static int onboard_hub_power_off(struct onboard_hub *hub)
{
	int err;

	gpiod_set_value_cansleep(hub->reset_gpio, 1);

	err = regulator_bulk_disable(hub->pdata->num_supplies, hub->supplies);
	if (err) {
		dev_err(hub->dev, "failed to disable supplies: %d\n", err);
		return err;
	}

	hub->is_powered_on = false;

	return 0;
}

static int __maybe_unused onboard_hub_suspend(struct device *dev)
{
	struct onboard_hub *hub = dev_get_drvdata(dev);
	struct usbdev_node *node;
	bool power_off = true;

	if (hub->always_powered_in_suspend)
		return 0;

	mutex_lock(&hub->lock);

	list_for_each_entry(node, &hub->udev_list, list) {
		if (!device_may_wakeup(node->udev->bus->controller))
			continue;

		if (usb_wakeup_enabled_descendants(node->udev)) {
			power_off = false;
			break;
		}
	}

	mutex_unlock(&hub->lock);

	if (!power_off)
		return 0;

	return onboard_hub_power_off(hub);
}

static int __maybe_unused onboard_hub_resume(struct device *dev)
{
	struct onboard_hub *hub = dev_get_drvdata(dev);

	if (hub->is_powered_on)
		return 0;

	return onboard_hub_power_on(hub);
}

static inline void get_udev_link_name(const struct usb_device *udev, char *buf, size_t size)
{
	snprintf(buf, size, "usb_dev.%s", dev_name(&udev->dev));
}

static int onboard_hub_add_usbdev(struct onboard_hub *hub, struct usb_device *udev)
{
	struct usbdev_node *node;
	char link_name[64];
	int err;

	mutex_lock(&hub->lock);

	if (hub->going_away) {
		err = -EINVAL;
		goto error;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		err = -ENOMEM;
		goto error;
	}

	node->udev = udev;

	list_add(&node->list, &hub->udev_list);

	mutex_unlock(&hub->lock);

	get_udev_link_name(udev, link_name, sizeof(link_name));
	WARN_ON(sysfs_create_link(&hub->dev->kobj, &udev->dev.kobj, link_name));

	return 0;

error:
	mutex_unlock(&hub->lock);

	return err;
}

static void onboard_hub_remove_usbdev(struct onboard_hub *hub, const struct usb_device *udev)
{
	struct usbdev_node *node;
	char link_name[64];

	get_udev_link_name(udev, link_name, sizeof(link_name));
	sysfs_remove_link(&hub->dev->kobj, link_name);

	mutex_lock(&hub->lock);

	list_for_each_entry(node, &hub->udev_list, list) {
		if (node->udev == udev) {
			list_del(&node->list);
			kfree(node);
			break;
		}
	}

	mutex_unlock(&hub->lock);
}

static ssize_t always_powered_in_suspend_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	const struct onboard_hub *hub = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", hub->always_powered_in_suspend);
}

static ssize_t always_powered_in_suspend_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct onboard_hub *hub = dev_get_drvdata(dev);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;

	hub->always_powered_in_suspend = val;

	return count;
}
static DEVICE_ATTR_RW(always_powered_in_suspend);

static struct attribute *onboard_hub_attrs[] = {
	&dev_attr_always_powered_in_suspend.attr,
	NULL,
};
ATTRIBUTE_GROUPS(onboard_hub);

static void onboard_hub_attach_usb_driver(struct work_struct *work)
{
	int err;

	err = driver_attach(&onboard_hub_usbdev_driver.drvwrap.driver);
	if (err)
		pr_err("Failed to attach USB driver: %d\n", err);
}

static int onboard_hub_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device *dev = &pdev->dev;
	struct onboard_hub *hub;
	unsigned int i;
	int err;

	hub = devm_kzalloc(dev, sizeof(*hub), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	of_id = of_match_device(onboard_hub_match, &pdev->dev);
	if (!of_id)
		return -ENODEV;

	hub->pdata = of_id->data;
	if (!hub->pdata)
		return -EINVAL;

	if (hub->pdata->num_supplies > MAX_SUPPLIES)
		return dev_err_probe(dev, -EINVAL, "max %zu supplies supported!\n",
				     MAX_SUPPLIES);

	for (i = 0; i < hub->pdata->num_supplies; i++)
		hub->supplies[i].supply = supply_names[i];

	err = devm_regulator_bulk_get(dev, hub->pdata->num_supplies, hub->supplies);
	if (err) {
		dev_err(dev, "Failed to get regulator supplies: %d\n", err);
		return err;
	}

	hub->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						  GPIOD_OUT_HIGH);
	if (IS_ERR(hub->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(hub->reset_gpio), "failed to get reset GPIO\n");

	hub->dev = dev;
	mutex_init(&hub->lock);
	INIT_LIST_HEAD(&hub->udev_list);

	dev_set_drvdata(dev, hub);

	err = onboard_hub_power_on(hub);
	if (err)
		return err;

	/*
	 * The USB driver might have been detached from the USB devices by
	 * onboard_hub_remove() (e.g. through an 'unbind' by userspace),
	 * make sure to re-attach it if needed.
	 *
	 * This needs to be done deferred to avoid self-deadlocks on systems
	 * with nested onboard hubs.
	 */
	schedule_work(&attach_usb_driver_work);

	return 0;
}

static void onboard_hub_remove(struct platform_device *pdev)
{
	struct onboard_hub *hub = dev_get_drvdata(&pdev->dev);
	struct usbdev_node *node;
	struct usb_device *udev;

	hub->going_away = true;

	mutex_lock(&hub->lock);

	/* unbind the USB devices to avoid dangling references to this device */
	while (!list_empty(&hub->udev_list)) {
		node = list_first_entry(&hub->udev_list, struct usbdev_node, list);
		udev = node->udev;

		/*
		 * Unbinding the driver will call onboard_hub_remove_usbdev(),
		 * which acquires hub->lock.  We must release the lock first.
		 */
		get_device(&udev->dev);
		mutex_unlock(&hub->lock);
		device_release_driver(&udev->dev);
		put_device(&udev->dev);
		mutex_lock(&hub->lock);
	}

	mutex_unlock(&hub->lock);

	onboard_hub_power_off(hub);
}

MODULE_DEVICE_TABLE(of, onboard_hub_match);

static const struct dev_pm_ops __maybe_unused onboard_hub_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(onboard_hub_suspend, onboard_hub_resume)
};

static struct platform_driver onboard_hub_driver = {
	.probe = onboard_hub_probe,
	.remove_new = onboard_hub_remove,

	.driver = {
		.name = "onboard-usb-hub",
		.of_match_table = onboard_hub_match,
		.pm = pm_ptr(&onboard_hub_pm_ops),
		.dev_groups = onboard_hub_groups,
	},
};

/************************** USB driver **************************/

#define VENDOR_ID_CYPRESS	0x04b4
#define VENDOR_ID_GENESYS	0x05e3
#define VENDOR_ID_MICROCHIP	0x0424
#define VENDOR_ID_REALTEK	0x0bda
#define VENDOR_ID_TI		0x0451
#define VENDOR_ID_VIA		0x2109

/*
 * Returns the onboard_hub platform device that is associated with the USB
 * device passed as parameter.
 */
static struct onboard_hub *_find_onboard_hub(struct device *dev)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct onboard_hub *hub;

	pdev = of_find_device_by_node(dev->of_node);
	if (!pdev) {
		np = of_parse_phandle(dev->of_node, "peer-hub", 0);
		if (!np) {
			dev_err(dev, "failed to find device node for peer hub\n");
			return ERR_PTR(-EINVAL);
		}

		pdev = of_find_device_by_node(np);
		of_node_put(np);

		if (!pdev)
			return ERR_PTR(-ENODEV);
	}

	hub = dev_get_drvdata(&pdev->dev);
	put_device(&pdev->dev);

	/*
	 * The presence of drvdata ('hub') indicates that the platform driver
	 * finished probing. This handles the case where (conceivably) we could
	 * be running at the exact same time as the platform driver's probe. If
	 * we detect the race we request probe deferral and we'll come back and
	 * try again.
	 */
	if (!hub)
		return ERR_PTR(-EPROBE_DEFER);

	return hub;
}

static int onboard_hub_usbdev_probe(struct usb_device *udev)
{
	struct device *dev = &udev->dev;
	struct onboard_hub *hub;
	int err;

	/* ignore supported hubs without device tree node */
	if (!dev->of_node)
		return -ENODEV;

	hub = _find_onboard_hub(dev);
	if (IS_ERR(hub))
		return PTR_ERR(hub);

	dev_set_drvdata(dev, hub);

	err = onboard_hub_add_usbdev(hub, udev);
	if (err)
		return err;

	return 0;
}

static void onboard_hub_usbdev_disconnect(struct usb_device *udev)
{
	struct onboard_hub *hub = dev_get_drvdata(&udev->dev);

	onboard_hub_remove_usbdev(hub, udev);
}

static const struct usb_device_id onboard_hub_id_table[] = {
	{ USB_DEVICE(VENDOR_ID_CYPRESS, 0x6504) }, /* CYUSB33{0,1,2}x/CYUSB230x 3.0 */
	{ USB_DEVICE(VENDOR_ID_CYPRESS, 0x6506) }, /* CYUSB33{0,1,2}x/CYUSB230x 2.0 */
	{ USB_DEVICE(VENDOR_ID_GENESYS, 0x0608) }, /* Genesys Logic GL850G USB 2.0 */
	{ USB_DEVICE(VENDOR_ID_GENESYS, 0x0610) }, /* Genesys Logic GL852G USB 2.0 */
	{ USB_DEVICE(VENDOR_ID_GENESYS, 0x0620) }, /* Genesys Logic GL3523 USB 3.1 */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2412) }, /* USB2412 USB 2.0 */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2514) }, /* USB2514B USB 2.0 */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2517) }, /* USB2517 USB 2.0 */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x0411) }, /* RTS5411 USB 3.1 */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x5411) }, /* RTS5411 USB 2.1 */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x0414) }, /* RTS5414 USB 3.2 */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x5414) }, /* RTS5414 USB 2.1 */
	{ USB_DEVICE(VENDOR_ID_TI, 0x8140) }, /* TI USB8041 3.0 */
	{ USB_DEVICE(VENDOR_ID_TI, 0x8142) }, /* TI USB8041 2.0 */
	{ USB_DEVICE(VENDOR_ID_VIA, 0x0817) }, /* VIA VL817 3.1 */
	{ USB_DEVICE(VENDOR_ID_VIA, 0x2817) }, /* VIA VL817 2.0 */
	{}
};
MODULE_DEVICE_TABLE(usb, onboard_hub_id_table);

static struct usb_device_driver onboard_hub_usbdev_driver = {
	.name = "onboard-usb-hub",
	.probe = onboard_hub_usbdev_probe,
	.disconnect = onboard_hub_usbdev_disconnect,
	.generic_subclass = 1,
	.supports_autosuspend =	1,
	.id_table = onboard_hub_id_table,
};

static int __init onboard_hub_init(void)
{
	int ret;

	ret = usb_register_device_driver(&onboard_hub_usbdev_driver, THIS_MODULE);
	if (ret)
		return ret;

	ret = platform_driver_register(&onboard_hub_driver);
	if (ret)
		usb_deregister_device_driver(&onboard_hub_usbdev_driver);

	return ret;
}
module_init(onboard_hub_init);

static void __exit onboard_hub_exit(void)
{
	usb_deregister_device_driver(&onboard_hub_usbdev_driver);
	platform_driver_unregister(&onboard_hub_driver);

	cancel_work_sync(&attach_usb_driver_work);
}
module_exit(onboard_hub_exit);

MODULE_AUTHOR("Matthias Kaehlcke <mka@chromium.org>");
MODULE_DESCRIPTION("Driver for discrete onboard USB hubs");
MODULE_LICENSE("GPL v2");
