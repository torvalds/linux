// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for onboard USB devices
 *
 * Copyright (c) 2022, Google LLC
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/i2c.h>
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
#include <linux/usb/onboard_dev.h>
#include <linux/workqueue.h>

#include "onboard_usb_dev.h"

/* USB5744 register offset and mask */
#define USB5744_CMD_ATTACH			0xAA
#define USB5744_CMD_ATTACH_LSB			0x56
#define USB5744_CMD_CREG_ACCESS			0x99
#define USB5744_CMD_CREG_ACCESS_LSB		0x37
#define USB5744_CREG_MEM_ADDR			0x00
#define USB5744_CREG_WRITE			0x00
#define USB5744_CREG_RUNTIMEFLAGS2		0x41
#define USB5744_CREG_RUNTIMEFLAGS2_LSB		0x1D
#define USB5744_CREG_BYPASS_UDC_SUSPEND		BIT(3)

static void onboard_dev_attach_usb_driver(struct work_struct *work);

static struct usb_device_driver onboard_dev_usbdev_driver;
static DECLARE_WORK(attach_usb_driver_work, onboard_dev_attach_usb_driver);

/************************** Platform driver **************************/

struct usbdev_node {
	struct usb_device *udev;
	struct list_head list;
};

struct onboard_dev {
	struct regulator_bulk_data supplies[MAX_SUPPLIES];
	struct device *dev;
	const struct onboard_dev_pdata *pdata;
	struct gpio_desc *reset_gpio;
	bool always_powered_in_suspend;
	bool is_powered_on;
	bool going_away;
	struct list_head udev_list;
	struct mutex lock;
	struct clk *clk;
};

static int onboard_dev_get_regulators(struct onboard_dev *onboard_dev)
{
	const char * const *supply_names = onboard_dev->pdata->supply_names;
	unsigned int num_supplies = onboard_dev->pdata->num_supplies;
	struct device *dev = onboard_dev->dev;
	unsigned int i;
	int err;

	if (num_supplies > MAX_SUPPLIES)
		return dev_err_probe(dev, -EINVAL, "max %d supplies supported!\n",
				     MAX_SUPPLIES);

	for (i = 0; i < num_supplies; i++)
		onboard_dev->supplies[i].supply = supply_names[i];

	err = devm_regulator_bulk_get(dev, num_supplies, onboard_dev->supplies);
	if (err)
		dev_err(dev, "Failed to get regulator supplies: %pe\n",
			ERR_PTR(err));

	return err;
}

static int onboard_dev_power_on(struct onboard_dev *onboard_dev)
{
	int err;

	err = clk_prepare_enable(onboard_dev->clk);
	if (err) {
		dev_err(onboard_dev->dev, "failed to enable clock: %pe\n",
			ERR_PTR(err));
		return err;
	}

	err = regulator_bulk_enable(onboard_dev->pdata->num_supplies,
				    onboard_dev->supplies);
	if (err) {
		dev_err(onboard_dev->dev, "failed to enable supplies: %pe\n",
			ERR_PTR(err));
		goto disable_clk;
	}

	fsleep(onboard_dev->pdata->reset_us);
	gpiod_set_value_cansleep(onboard_dev->reset_gpio, 0);
	fsleep(onboard_dev->pdata->power_on_delay_us);

	onboard_dev->is_powered_on = true;

	return 0;

disable_clk:
	clk_disable_unprepare(onboard_dev->clk);
	return err;
}

static int onboard_dev_power_off(struct onboard_dev *onboard_dev)
{
	int err;

	gpiod_set_value_cansleep(onboard_dev->reset_gpio, 1);

	err = regulator_bulk_disable(onboard_dev->pdata->num_supplies,
				     onboard_dev->supplies);
	if (err) {
		dev_err(onboard_dev->dev, "failed to disable supplies: %pe\n",
			ERR_PTR(err));
		return err;
	}

	clk_disable_unprepare(onboard_dev->clk);

	onboard_dev->is_powered_on = false;

	return 0;
}

static int __maybe_unused onboard_dev_suspend(struct device *dev)
{
	struct onboard_dev *onboard_dev = dev_get_drvdata(dev);
	struct usbdev_node *node;
	bool power_off = true;

	if (onboard_dev->always_powered_in_suspend)
		return 0;

	mutex_lock(&onboard_dev->lock);

	list_for_each_entry(node, &onboard_dev->udev_list, list) {
		if (!device_may_wakeup(node->udev->bus->controller))
			continue;

		if (usb_wakeup_enabled_descendants(node->udev)) {
			power_off = false;
			break;
		}
	}

	mutex_unlock(&onboard_dev->lock);

	if (!power_off)
		return 0;

	return onboard_dev_power_off(onboard_dev);
}

static int __maybe_unused onboard_dev_resume(struct device *dev)
{
	struct onboard_dev *onboard_dev = dev_get_drvdata(dev);

	if (onboard_dev->is_powered_on)
		return 0;

	return onboard_dev_power_on(onboard_dev);
}

static inline void get_udev_link_name(const struct usb_device *udev, char *buf,
				      size_t size)
{
	snprintf(buf, size, "usb_dev.%s", dev_name(&udev->dev));
}

static int onboard_dev_add_usbdev(struct onboard_dev *onboard_dev,
				  struct usb_device *udev)
{
	struct usbdev_node *node;
	char link_name[64];
	int err;

	mutex_lock(&onboard_dev->lock);

	if (onboard_dev->going_away) {
		err = -EINVAL;
		goto error;
	}

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node) {
		err = -ENOMEM;
		goto error;
	}

	node->udev = udev;

	list_add(&node->list, &onboard_dev->udev_list);

	mutex_unlock(&onboard_dev->lock);

	get_udev_link_name(udev, link_name, sizeof(link_name));
	WARN_ON(sysfs_create_link(&onboard_dev->dev->kobj, &udev->dev.kobj,
				  link_name));

	return 0;

error:
	mutex_unlock(&onboard_dev->lock);

	return err;
}

static void onboard_dev_remove_usbdev(struct onboard_dev *onboard_dev,
				      const struct usb_device *udev)
{
	struct usbdev_node *node;
	char link_name[64];

	get_udev_link_name(udev, link_name, sizeof(link_name));
	sysfs_remove_link(&onboard_dev->dev->kobj, link_name);

	mutex_lock(&onboard_dev->lock);

	list_for_each_entry(node, &onboard_dev->udev_list, list) {
		if (node->udev == udev) {
			list_del(&node->list);
			kfree(node);
			break;
		}
	}

	mutex_unlock(&onboard_dev->lock);
}

static ssize_t always_powered_in_suspend_show(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	const struct onboard_dev *onboard_dev = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", onboard_dev->always_powered_in_suspend);
}

static ssize_t always_powered_in_suspend_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct onboard_dev *onboard_dev = dev_get_drvdata(dev);
	bool val;
	int ret;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;

	onboard_dev->always_powered_in_suspend = val;

	return count;
}
static DEVICE_ATTR_RW(always_powered_in_suspend);

static struct attribute *onboard_dev_attrs[] = {
	&dev_attr_always_powered_in_suspend.attr,
	NULL,
};

static umode_t onboard_dev_attrs_are_visible(struct kobject *kobj,
					     struct attribute *attr,
					     int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct onboard_dev *onboard_dev = dev_get_drvdata(dev);

	if (attr == &dev_attr_always_powered_in_suspend.attr &&
	    !onboard_dev->pdata->is_hub)
		return 0;

	return attr->mode;
}

static const struct attribute_group onboard_dev_group = {
	.is_visible = onboard_dev_attrs_are_visible,
	.attrs = onboard_dev_attrs,
};
__ATTRIBUTE_GROUPS(onboard_dev);


static void onboard_dev_attach_usb_driver(struct work_struct *work)
{
	int err;

	err = driver_attach(&onboard_dev_usbdev_driver.driver);
	if (err)
		pr_err("Failed to attach USB driver: %pe\n", ERR_PTR(err));
}

static int onboard_dev_5744_i2c_init(struct i2c_client *client)
{
#if IS_ENABLED(CONFIG_USB_ONBOARD_DEV_USB5744)
	struct device *dev = &client->dev;
	int ret;

	/*
	 * Set BYPASS_UDC_SUSPEND bit to ensure MCU is always enabled
	 * and ready to respond to SMBus runtime commands.
	 * The command writes 5 bytes to memory and single data byte in
	 * configuration register.
	 */
	char wr_buf[7] = {USB5744_CREG_MEM_ADDR, 5,
			  USB5744_CREG_WRITE, 1,
			  USB5744_CREG_RUNTIMEFLAGS2,
			  USB5744_CREG_RUNTIMEFLAGS2_LSB,
			  USB5744_CREG_BYPASS_UDC_SUSPEND};

	ret = i2c_smbus_write_block_data(client, 0, sizeof(wr_buf), wr_buf);
	if (ret)
		return dev_err_probe(dev, ret, "BYPASS_UDC_SUSPEND bit configuration failed\n");

	ret = i2c_smbus_write_word_data(client, USB5744_CMD_CREG_ACCESS,
					USB5744_CMD_CREG_ACCESS_LSB);
	if (ret)
		return dev_err_probe(dev, ret, "Configuration Register Access Command failed\n");

	/* Send SMBus command to boot hub. */
	ret = i2c_smbus_write_word_data(client, USB5744_CMD_ATTACH,
					USB5744_CMD_ATTACH_LSB);
	if (ret < 0)
		return dev_err_probe(dev, ret, "USB Attach with SMBus command failed\n");

	return ret;
#else
	return -ENODEV;
#endif
}

static int onboard_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct onboard_dev *onboard_dev;
	struct device_node *i2c_node;
	int err;

	onboard_dev = devm_kzalloc(dev, sizeof(*onboard_dev), GFP_KERNEL);
	if (!onboard_dev)
		return -ENOMEM;

	onboard_dev->pdata = device_get_match_data(dev);
	if (!onboard_dev->pdata)
		return -EINVAL;

	if (!onboard_dev->pdata->is_hub)
		onboard_dev->always_powered_in_suspend = true;

	onboard_dev->dev = dev;

	err = onboard_dev_get_regulators(onboard_dev);
	if (err)
		return err;

	onboard_dev->clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(onboard_dev->clk))
		return dev_err_probe(dev, PTR_ERR(onboard_dev->clk),
				     "failed to get clock\n");

	onboard_dev->reset_gpio = devm_gpiod_get_optional(dev, "reset",
							  GPIOD_OUT_HIGH);
	if (IS_ERR(onboard_dev->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(onboard_dev->reset_gpio),
				     "failed to get reset GPIO\n");

	mutex_init(&onboard_dev->lock);
	INIT_LIST_HEAD(&onboard_dev->udev_list);

	dev_set_drvdata(dev, onboard_dev);

	err = onboard_dev_power_on(onboard_dev);
	if (err)
		return err;

	i2c_node = of_parse_phandle(pdev->dev.of_node, "i2c-bus", 0);
	if (i2c_node) {
		struct i2c_client *client = NULL;

#if IS_ENABLED(CONFIG_USB_ONBOARD_DEV_USB5744)
		client = of_find_i2c_device_by_node(i2c_node);
#endif
		of_node_put(i2c_node);

		if (!client) {
			err = -EPROBE_DEFER;
			goto err_power_off;
		}

		if (of_device_is_compatible(pdev->dev.of_node, "usb424,2744") ||
		    of_device_is_compatible(pdev->dev.of_node, "usb424,5744"))
			err = onboard_dev_5744_i2c_init(client);

		put_device(&client->dev);
		if (err < 0)
			goto err_power_off;
	}

	/*
	 * The USB driver might have been detached from the USB devices by
	 * onboard_dev_remove() (e.g. through an 'unbind' by userspace),
	 * make sure to re-attach it if needed.
	 *
	 * This needs to be done deferred to avoid self-deadlocks on systems
	 * with nested onboard hubs.
	 */
	schedule_work(&attach_usb_driver_work);

	return 0;

err_power_off:
	onboard_dev_power_off(onboard_dev);
	return err;
}

static void onboard_dev_remove(struct platform_device *pdev)
{
	struct onboard_dev *onboard_dev = dev_get_drvdata(&pdev->dev);
	struct usbdev_node *node;
	struct usb_device *udev;

	onboard_dev->going_away = true;

	mutex_lock(&onboard_dev->lock);

	/* unbind the USB devices to avoid dangling references to this device */
	while (!list_empty(&onboard_dev->udev_list)) {
		node = list_first_entry(&onboard_dev->udev_list,
					struct usbdev_node, list);
		udev = node->udev;

		/*
		 * Unbinding the driver will call onboard_dev_remove_usbdev(),
		 * which acquires onboard_dev->lock. We must release the lock
		 * first.
		 */
		get_device(&udev->dev);
		mutex_unlock(&onboard_dev->lock);
		device_release_driver(&udev->dev);
		put_device(&udev->dev);
		mutex_lock(&onboard_dev->lock);
	}

	mutex_unlock(&onboard_dev->lock);

	onboard_dev_power_off(onboard_dev);
}

MODULE_DEVICE_TABLE(of, onboard_dev_match);

static const struct dev_pm_ops __maybe_unused onboard_dev_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(onboard_dev_suspend, onboard_dev_resume)
};

static struct platform_driver onboard_dev_driver = {
	.probe = onboard_dev_probe,
	.remove_new = onboard_dev_remove,

	.driver = {
		.name = "onboard-usb-dev",
		.of_match_table = onboard_dev_match,
		.pm = pm_ptr(&onboard_dev_pm_ops),
		.dev_groups = onboard_dev_groups,
	},
};

/************************** USB driver **************************/

#define VENDOR_ID_CYPRESS	0x04b4
#define VENDOR_ID_GENESYS	0x05e3
#define VENDOR_ID_MICROCHIP	0x0424
#define VENDOR_ID_REALTEK	0x0bda
#define VENDOR_ID_TI		0x0451
#define VENDOR_ID_VIA		0x2109
#define VENDOR_ID_XMOS		0x20B1

/*
 * Returns the onboard_dev platform device that is associated with the USB
 * device passed as parameter.
 */
static struct onboard_dev *_find_onboard_dev(struct device *dev)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct onboard_dev *onboard_dev;

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

	onboard_dev = dev_get_drvdata(&pdev->dev);
	put_device(&pdev->dev);

	/*
	 * The presence of drvdata indicates that the platform driver finished
	 * probing. This handles the case where (conceivably) we could be
	 * running at the exact same time as the platform driver's probe. If
	 * we detect the race we request probe deferral and we'll come back and
	 * try again.
	 */
	if (!onboard_dev)
		return ERR_PTR(-EPROBE_DEFER);

	return onboard_dev;
}

static bool onboard_dev_usbdev_match(struct usb_device *udev)
{
	/* Onboard devices using this driver must have a device tree node */
	return !!udev->dev.of_node;
}

static int onboard_dev_usbdev_probe(struct usb_device *udev)
{
	struct device *dev = &udev->dev;
	struct onboard_dev *onboard_dev;
	int err;

	onboard_dev = _find_onboard_dev(dev);
	if (IS_ERR(onboard_dev))
		return PTR_ERR(onboard_dev);

	dev_set_drvdata(dev, onboard_dev);

	err = onboard_dev_add_usbdev(onboard_dev, udev);
	if (err)
		return err;

	return 0;
}

static void onboard_dev_usbdev_disconnect(struct usb_device *udev)
{
	struct onboard_dev *onboard_dev = dev_get_drvdata(&udev->dev);

	onboard_dev_remove_usbdev(onboard_dev, udev);
}

static const struct usb_device_id onboard_dev_id_table[] = {
	{ USB_DEVICE(VENDOR_ID_CYPRESS, 0x6504) }, /* CYUSB33{0,1,2}x/CYUSB230x 3.0 HUB */
	{ USB_DEVICE(VENDOR_ID_CYPRESS, 0x6506) }, /* CYUSB33{0,1,2}x/CYUSB230x 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_CYPRESS, 0x6570) }, /* CY7C6563x 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_GENESYS, 0x0608) }, /* Genesys Logic GL850G USB 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_GENESYS, 0x0610) }, /* Genesys Logic GL852G USB 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_GENESYS, 0x0620) }, /* Genesys Logic GL3523 USB 3.1 HUB */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2412) }, /* USB2412 USB 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2514) }, /* USB2514B USB 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2517) }, /* USB2517 USB 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x2744) }, /* USB5744 USB 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_MICROCHIP, 0x5744) }, /* USB5744 USB 3.0 HUB */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x0411) }, /* RTS5411 USB 3.1 HUB */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x5411) }, /* RTS5411 USB 2.1 HUB */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x0414) }, /* RTS5414 USB 3.2 HUB */
	{ USB_DEVICE(VENDOR_ID_REALTEK, 0x5414) }, /* RTS5414 USB 2.1 HUB */
	{ USB_DEVICE(VENDOR_ID_TI, 0x8025) }, /* TI USB8020B 3.0 HUB */
	{ USB_DEVICE(VENDOR_ID_TI, 0x8027) }, /* TI USB8020B 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_TI, 0x8140) }, /* TI USB8041 3.0 HUB */
	{ USB_DEVICE(VENDOR_ID_TI, 0x8142) }, /* TI USB8041 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_VIA, 0x0817) }, /* VIA VL817 3.1 HUB */
	{ USB_DEVICE(VENDOR_ID_VIA, 0x2817) }, /* VIA VL817 2.0 HUB */
	{ USB_DEVICE(VENDOR_ID_XMOS, 0x0013) }, /* XMOS XVF3500 Voice Processor */
	{}
};
MODULE_DEVICE_TABLE(usb, onboard_dev_id_table);

static struct usb_device_driver onboard_dev_usbdev_driver = {
	.name = "onboard-usb-dev",
	.match = onboard_dev_usbdev_match,
	.probe = onboard_dev_usbdev_probe,
	.disconnect = onboard_dev_usbdev_disconnect,
	.generic_subclass = 1,
	.supports_autosuspend =	1,
	.id_table = onboard_dev_id_table,
};

static int __init onboard_dev_init(void)
{
	int ret;

	ret = usb_register_device_driver(&onboard_dev_usbdev_driver, THIS_MODULE);
	if (ret)
		return ret;

	ret = platform_driver_register(&onboard_dev_driver);
	if (ret)
		usb_deregister_device_driver(&onboard_dev_usbdev_driver);

	return ret;
}
module_init(onboard_dev_init);

static void __exit onboard_dev_exit(void)
{
	usb_deregister_device_driver(&onboard_dev_usbdev_driver);
	platform_driver_unregister(&onboard_dev_driver);

	cancel_work_sync(&attach_usb_driver_work);
}
module_exit(onboard_dev_exit);

MODULE_AUTHOR("Matthias Kaehlcke <mka@chromium.org>");
MODULE_DESCRIPTION("Driver for discrete onboard USB devices");
MODULE_LICENSE("GPL v2");
