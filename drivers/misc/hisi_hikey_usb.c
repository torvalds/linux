// SPDX-License-Identifier: GPL-2.0
/*
 * Support for usb functionality of Hikey series boards
 * based on Hisilicon Kirin Soc.
 *
 * Copyright (C) 2017-2018 Hilisicon Electronics Co., Ltd.
 *		http://www.huawei.com
 *
 * Authors: Yu Chen <chenyu56@huawei.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/usb/role.h>

#define DEVICE_DRIVER_NAME "hisi_hikey_usb"

#define HUB_VBUS_POWER_ON 1
#define HUB_VBUS_POWER_OFF 0
#define USB_SWITCH_TO_HUB 1
#define USB_SWITCH_TO_TYPEC 0
#define TYPEC_VBUS_POWER_ON 1
#define TYPEC_VBUS_POWER_OFF 0

struct hisi_hikey_usb {
	struct gpio_desc *otg_switch;
	struct gpio_desc *typec_vbus;
	struct gpio_desc *hub_vbus;

	struct usb_role_switch *hub_role_sw;

	struct usb_role_switch *dev_role_sw;
	enum usb_role role;

	struct mutex lock;
	struct work_struct work;

	struct notifier_block nb;
};

static void hub_power_ctrl(struct hisi_hikey_usb *hisi_hikey_usb, int value)
{
	gpiod_set_value_cansleep(hisi_hikey_usb->hub_vbus, value);
}

static void usb_switch_ctrl(struct hisi_hikey_usb *hisi_hikey_usb,
			    int switch_to)
{
	gpiod_set_value_cansleep(hisi_hikey_usb->otg_switch, switch_to);
}

static void usb_typec_power_ctrl(struct hisi_hikey_usb *hisi_hikey_usb,
				 int value)
{
	gpiod_set_value_cansleep(hisi_hikey_usb->typec_vbus, value);
}



static void relay_set_role_switch(struct work_struct *work)
{
	struct hisi_hikey_usb *hisi_hikey_usb = container_of(work,
							struct hisi_hikey_usb,
							work);
	struct usb_role_switch *sw;
	enum usb_role role;

	if (!hisi_hikey_usb || !hisi_hikey_usb->dev_role_sw)
		return;

	mutex_lock(&hisi_hikey_usb->lock);
	switch (hisi_hikey_usb->role) {
	case USB_ROLE_NONE:
		usb_typec_power_ctrl(hisi_hikey_usb, TYPEC_VBUS_POWER_OFF);
		usb_switch_ctrl(hisi_hikey_usb, USB_SWITCH_TO_HUB);
		hub_power_ctrl(hisi_hikey_usb, HUB_VBUS_POWER_ON);
		break;
	case USB_ROLE_HOST:
		hub_power_ctrl(hisi_hikey_usb, HUB_VBUS_POWER_OFF);
		usb_switch_ctrl(hisi_hikey_usb, USB_SWITCH_TO_TYPEC);
		usb_typec_power_ctrl(hisi_hikey_usb, TYPEC_VBUS_POWER_ON);
		break;
	case USB_ROLE_DEVICE:
		hub_power_ctrl(hisi_hikey_usb, HUB_VBUS_POWER_OFF);
		usb_typec_power_ctrl(hisi_hikey_usb, TYPEC_VBUS_POWER_OFF);
		usb_switch_ctrl(hisi_hikey_usb, USB_SWITCH_TO_TYPEC);
		break;
	default:
		break;
	}
	sw = hisi_hikey_usb->dev_role_sw;
	role = hisi_hikey_usb->role;
	mutex_unlock(&hisi_hikey_usb->lock);

	usb_role_switch_set_role(sw, role);
}

static int hub_usb_role_switch_set(struct usb_role_switch *sw, enum usb_role role)
{
	struct hisi_hikey_usb *hisi_hikey_usb = usb_role_switch_get_drvdata(sw);

	if (!hisi_hikey_usb || !hisi_hikey_usb->dev_role_sw)
		return -EINVAL;

	mutex_lock(&hisi_hikey_usb->lock);
	hisi_hikey_usb->role = role;
	mutex_unlock(&hisi_hikey_usb->lock);

	schedule_work(&hisi_hikey_usb->work);

	return 0;
}

static int hisi_hikey_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_hikey_usb *hisi_hikey_usb;
	struct usb_role_switch_desc hub_role_switch = {NULL};

	hisi_hikey_usb = devm_kzalloc(dev, sizeof(*hisi_hikey_usb), GFP_KERNEL);
	if (!hisi_hikey_usb)
		return -ENOMEM;

	hisi_hikey_usb->typec_vbus = devm_gpiod_get(dev, "typec-vbus",
						    GPIOD_OUT_LOW);
	if (IS_ERR(hisi_hikey_usb->typec_vbus))
		return PTR_ERR(hisi_hikey_usb->typec_vbus);

	hisi_hikey_usb->otg_switch = devm_gpiod_get(dev, "otg-switch",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(hisi_hikey_usb->otg_switch))
		return PTR_ERR(hisi_hikey_usb->otg_switch);

	/* hub-vdd33-en is optional */
	hisi_hikey_usb->hub_vbus = devm_gpiod_get_optional(dev, "hub-vdd33-en",
							   GPIOD_OUT_HIGH);
	if (IS_ERR(hisi_hikey_usb->hub_vbus))
		return PTR_ERR(hisi_hikey_usb->hub_vbus);

	hisi_hikey_usb->dev_role_sw = usb_role_switch_get(dev);
	if (!hisi_hikey_usb->dev_role_sw)
		return -EPROBE_DEFER;
	if (IS_ERR(hisi_hikey_usb->dev_role_sw))
		return PTR_ERR(hisi_hikey_usb->dev_role_sw);


	INIT_WORK(&hisi_hikey_usb->work, relay_set_role_switch);
	mutex_init(&hisi_hikey_usb->lock);

	hub_role_switch.fwnode = dev_fwnode(dev);
	hub_role_switch.set = hub_usb_role_switch_set;
	hub_role_switch.driver_data = hisi_hikey_usb;

	hisi_hikey_usb->hub_role_sw = usb_role_switch_register(dev,
							&hub_role_switch);

	if (IS_ERR(hisi_hikey_usb->hub_role_sw)) {
		usb_role_switch_put(hisi_hikey_usb->dev_role_sw);
		return PTR_ERR(hisi_hikey_usb->hub_role_sw);
	}

	platform_set_drvdata(pdev, hisi_hikey_usb);

	return 0;
}

static int  hisi_hikey_usb_remove(struct platform_device *pdev)
{
	struct hisi_hikey_usb *hisi_hikey_usb = platform_get_drvdata(pdev);

	if (hisi_hikey_usb->hub_role_sw)
		usb_role_switch_unregister(hisi_hikey_usb->hub_role_sw);

	if (hisi_hikey_usb->dev_role_sw)
		usb_role_switch_put(hisi_hikey_usb->dev_role_sw);

	return 0;
}

static const struct of_device_id id_table_hisi_hikey_usb[] = {
	{.compatible = "hisilicon,gpio_hubv1"},
	{}
};
MODULE_DEVICE_TABLE(of, id_table_hisi_hikey_usb);

static struct platform_driver hisi_hikey_usb_driver = {
	.probe = hisi_hikey_usb_probe,
	.remove = hisi_hikey_usb_remove,
	.driver = {
		.name = DEVICE_DRIVER_NAME,
		.of_match_table = id_table_hisi_hikey_usb,
	},
};

module_platform_driver(hisi_hikey_usb_driver);

MODULE_AUTHOR("Yu Chen <chenyu56@huawei.com>");
MODULE_DESCRIPTION("Driver Support for USB functionality of Hikey");
MODULE_LICENSE("GPL v2");
