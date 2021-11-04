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
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
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
	struct device *dev;
	struct gpio_desc *otg_switch;
	struct gpio_desc *typec_vbus;
	struct gpio_desc *reset;

	struct regulator *regulator;

	struct usb_role_switch *hub_role_sw;

	struct usb_role_switch *dev_role_sw;
	enum usb_role role;

	struct mutex lock;
	struct work_struct work;

	struct notifier_block nb;
};

static void hub_power_ctrl(struct hisi_hikey_usb *hisi_hikey_usb, int value)
{
	int ret, status;

	if (!hisi_hikey_usb->regulator)
		return;

	status = regulator_is_enabled(hisi_hikey_usb->regulator);
	if (status == !!value)
		return;

	if (value)
		ret = regulator_enable(hisi_hikey_usb->regulator);
	else
		ret = regulator_disable(hisi_hikey_usb->regulator);

	if (ret)
		dev_err(hisi_hikey_usb->dev,
			"Can't switch regulator state to %s\n",
			value ? "enabled" : "disabled");
}

static void usb_switch_ctrl(struct hisi_hikey_usb *hisi_hikey_usb,
			    int switch_to)
{
	if (!hisi_hikey_usb->otg_switch)
		return;

	gpiod_set_value_cansleep(hisi_hikey_usb->otg_switch, switch_to);
}

static void usb_typec_power_ctrl(struct hisi_hikey_usb *hisi_hikey_usb,
				 int value)
{
	if (!hisi_hikey_usb->typec_vbus)
		return;

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

static int hisi_hikey_usb_of_role_switch(struct platform_device *pdev,
					 struct hisi_hikey_usb *hisi_hikey_usb)
{
	struct device *dev = &pdev->dev;
	struct usb_role_switch_desc hub_role_switch = {NULL};

	if (!device_property_read_bool(dev, "usb-role-switch"))
		return 0;

	hisi_hikey_usb->otg_switch = devm_gpiod_get(dev, "otg-switch",
						    GPIOD_OUT_HIGH);
	if (IS_ERR(hisi_hikey_usb->otg_switch)) {
		dev_err(dev, "get otg-switch failed with error %ld\n",
			PTR_ERR(hisi_hikey_usb->otg_switch));
		return PTR_ERR(hisi_hikey_usb->otg_switch);
	}

	hisi_hikey_usb->typec_vbus = devm_gpiod_get(dev, "typec-vbus",
						    GPIOD_OUT_LOW);
	if (IS_ERR(hisi_hikey_usb->typec_vbus)) {
		dev_err(dev, "get typec-vbus failed with error %ld\n",
			PTR_ERR(hisi_hikey_usb->typec_vbus));
		return PTR_ERR(hisi_hikey_usb->typec_vbus);
	}

	hisi_hikey_usb->reset = devm_gpiod_get_optional(dev,
							"hub-reset-en",
							GPIOD_OUT_HIGH);
	if (IS_ERR(hisi_hikey_usb->reset)) {
		dev_err(dev, "get hub-reset-en failed with error %ld\n",
			PTR_ERR(hisi_hikey_usb->reset));
		return PTR_ERR(hisi_hikey_usb->reset);
	}

	hisi_hikey_usb->dev_role_sw = usb_role_switch_get(dev);
	if (!hisi_hikey_usb->dev_role_sw)
		return -EPROBE_DEFER;
	if (IS_ERR(hisi_hikey_usb->dev_role_sw)) {
		dev_err(dev, "get device role switch failed with error %ld\n",
			PTR_ERR(hisi_hikey_usb->dev_role_sw));
		return PTR_ERR(hisi_hikey_usb->dev_role_sw);
	}

	INIT_WORK(&hisi_hikey_usb->work, relay_set_role_switch);

	hub_role_switch.fwnode = dev_fwnode(dev);
	hub_role_switch.set = hub_usb_role_switch_set;
	hub_role_switch.driver_data = hisi_hikey_usb;

	hisi_hikey_usb->hub_role_sw = usb_role_switch_register(dev,
							       &hub_role_switch);

	if (IS_ERR(hisi_hikey_usb->hub_role_sw)) {
		dev_err(dev,
			"failed to register hub role with error %ld\n",
			PTR_ERR(hisi_hikey_usb->hub_role_sw));
		usb_role_switch_put(hisi_hikey_usb->dev_role_sw);
		return PTR_ERR(hisi_hikey_usb->hub_role_sw);
	}

	return 0;
}

static int hisi_hikey_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hisi_hikey_usb *hisi_hikey_usb;
	int ret;

	hisi_hikey_usb = devm_kzalloc(dev, sizeof(*hisi_hikey_usb), GFP_KERNEL);
	if (!hisi_hikey_usb)
		return -ENOMEM;

	hisi_hikey_usb->dev = &pdev->dev;
	mutex_init(&hisi_hikey_usb->lock);

	hisi_hikey_usb->regulator = devm_regulator_get(dev, "hub-vdd");
	if (IS_ERR(hisi_hikey_usb->regulator)) {
		if (PTR_ERR(hisi_hikey_usb->regulator) == -EPROBE_DEFER) {
			dev_info(dev, "waiting for hub-vdd-supply\n");
			return PTR_ERR(hisi_hikey_usb->regulator);
		}
		dev_err(dev, "get hub-vdd-supply failed with error %ld\n",
			PTR_ERR(hisi_hikey_usb->regulator));
		return PTR_ERR(hisi_hikey_usb->regulator);
	}

	ret = hisi_hikey_usb_of_role_switch(pdev, hisi_hikey_usb);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, hisi_hikey_usb);

	return 0;
}

static int  hisi_hikey_usb_remove(struct platform_device *pdev)
{
	struct hisi_hikey_usb *hisi_hikey_usb = platform_get_drvdata(pdev);

	if (hisi_hikey_usb->hub_role_sw) {
		usb_role_switch_unregister(hisi_hikey_usb->hub_role_sw);

		if (hisi_hikey_usb->dev_role_sw)
			usb_role_switch_put(hisi_hikey_usb->dev_role_sw);
	} else {
		hub_power_ctrl(hisi_hikey_usb, HUB_VBUS_POWER_OFF);
	}

	return 0;
}

static const struct of_device_id id_table_hisi_hikey_usb[] = {
	{ .compatible = "hisilicon,usbhub" },
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
