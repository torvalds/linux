// SPDX-License-Identifier: GPL-2.0
/*
 * USB GPIO Based Connection Detection Driver
 *
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Chunfeng Yun <chunfeng.yun@mediatek.com>
 *
 * Some code borrowed from drivers/extcon/extcon-usb-gpio.c
 */

#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/role.h>

#define USB_GPIO_DEB_MS		20	/* ms */
#define USB_GPIO_DEB_US		((USB_GPIO_DEB_MS) * 1000)	/* us */

#define USB_CONN_IRQF	\
	(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT)

struct usb_conn_info {
	struct device *dev;
	struct usb_role_switch *role_sw;
	enum usb_role last_role;
	struct regulator *vbus;
	struct delayed_work dw_det;
	unsigned long debounce_jiffies;

	struct gpio_desc *id_gpiod;
	struct gpio_desc *vbus_gpiod;
	int id_irq;
	int vbus_irq;

	struct power_supply_desc desc;
	struct power_supply *charger;
};

/*
 * "DEVICE" = VBUS and "HOST" = !ID, so we have:
 * Both "DEVICE" and "HOST" can't be set as active at the same time
 * so if "HOST" is active (i.e. ID is 0)  we keep "DEVICE" inactive
 * even if VBUS is on.
 *
 *  Role          |   ID  |  VBUS
 * ------------------------------------
 *  [1] DEVICE    |   H   |   H
 *  [2] NONE      |   H   |   L
 *  [3] HOST      |   L   |   H
 *  [4] HOST      |   L   |   L
 *
 * In case we have only one of these signals:
 * - VBUS only - we want to distinguish between [1] and [2], so ID is always 1
 * - ID only - we want to distinguish between [1] and [4], so VBUS = ID
 */
static void usb_conn_detect_cable(struct work_struct *work)
{
	struct usb_conn_info *info;
	enum usb_role role;
	int id, vbus, ret;

	info = container_of(to_delayed_work(work),
			    struct usb_conn_info, dw_det);

	/* check ID and VBUS */
	id = info->id_gpiod ?
		gpiod_get_value_cansleep(info->id_gpiod) : 1;
	vbus = info->vbus_gpiod ?
		gpiod_get_value_cansleep(info->vbus_gpiod) : id;

	if (!id)
		role = USB_ROLE_HOST;
	else if (vbus)
		role = USB_ROLE_DEVICE;
	else
		role = USB_ROLE_NONE;

	dev_dbg(info->dev, "role %s -> %s, gpios: id %d, vbus %d\n",
		usb_role_string(info->last_role), usb_role_string(role), id, vbus);

	if (info->last_role == role) {
		dev_warn(info->dev, "repeated role: %s\n", usb_role_string(role));
		return;
	}

	if (info->last_role == USB_ROLE_HOST && info->vbus)
		regulator_disable(info->vbus);

	ret = usb_role_switch_set_role(info->role_sw, role);
	if (ret)
		dev_err(info->dev, "failed to set role: %d\n", ret);

	if (role == USB_ROLE_HOST && info->vbus) {
		ret = regulator_enable(info->vbus);
		if (ret)
			dev_err(info->dev, "enable vbus regulator failed\n");
	}

	info->last_role = role;

	if (info->vbus)
		dev_dbg(info->dev, "vbus regulator is %s\n",
			regulator_is_enabled(info->vbus) ? "enabled" : "disabled");

	power_supply_changed(info->charger);
}

static void usb_conn_queue_dwork(struct usb_conn_info *info,
				 unsigned long delay)
{
	queue_delayed_work(system_power_efficient_wq, &info->dw_det, delay);
}

static irqreturn_t usb_conn_isr(int irq, void *dev_id)
{
	struct usb_conn_info *info = dev_id;

	usb_conn_queue_dwork(info, info->debounce_jiffies);

	return IRQ_HANDLED;
}

static enum power_supply_property usb_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static int usb_charger_get_property(struct power_supply *psy,
				    enum power_supply_property psp,
				    union power_supply_propval *val)
{
	struct usb_conn_info *info = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = info->last_role == USB_ROLE_DEVICE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int usb_conn_psy_register(struct usb_conn_info *info)
{
	struct device *dev = info->dev;
	struct power_supply_desc *desc = &info->desc;
	struct power_supply_config cfg = {
		.of_node = dev->of_node,
	};

	desc->name = "usb-charger";
	desc->properties = usb_charger_properties;
	desc->num_properties = ARRAY_SIZE(usb_charger_properties);
	desc->get_property = usb_charger_get_property;
	desc->type = POWER_SUPPLY_TYPE_USB;
	cfg.drv_data = info;

	info->charger = devm_power_supply_register(dev, desc, &cfg);
	if (IS_ERR(info->charger))
		dev_err(dev, "Unable to register charger\n");

	return PTR_ERR_OR_ZERO(info->charger);
}

static int usb_conn_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct usb_conn_info *info;
	int ret = 0;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;
	info->id_gpiod = devm_gpiod_get_optional(dev, "id", GPIOD_IN);
	if (IS_ERR(info->id_gpiod))
		return PTR_ERR(info->id_gpiod);

	info->vbus_gpiod = devm_gpiod_get_optional(dev, "vbus", GPIOD_IN);
	if (IS_ERR(info->vbus_gpiod))
		return PTR_ERR(info->vbus_gpiod);

	if (!info->id_gpiod && !info->vbus_gpiod) {
		dev_err(dev, "failed to get gpios\n");
		return -ENODEV;
	}

	if (info->id_gpiod)
		ret = gpiod_set_debounce(info->id_gpiod, USB_GPIO_DEB_US);
	if (!ret && info->vbus_gpiod)
		ret = gpiod_set_debounce(info->vbus_gpiod, USB_GPIO_DEB_US);
	if (ret < 0)
		info->debounce_jiffies = msecs_to_jiffies(USB_GPIO_DEB_MS);

	INIT_DELAYED_WORK(&info->dw_det, usb_conn_detect_cable);

	info->vbus = devm_regulator_get_optional(dev, "vbus");
	if (PTR_ERR(info->vbus) == -ENODEV)
		info->vbus = NULL;

	if (IS_ERR(info->vbus)) {
		ret = PTR_ERR(info->vbus);
		return dev_err_probe(dev, ret, "failed to get vbus :%d\n", ret);
	}

	info->role_sw = usb_role_switch_get(dev);
	if (IS_ERR(info->role_sw))
		return dev_err_probe(dev, PTR_ERR(info->role_sw),
				     "failed to get role switch\n");

	ret = usb_conn_psy_register(info);
	if (ret)
		goto put_role_sw;

	if (info->id_gpiod) {
		info->id_irq = gpiod_to_irq(info->id_gpiod);
		if (info->id_irq < 0) {
			dev_err(dev, "failed to get ID IRQ\n");
			ret = info->id_irq;
			goto put_role_sw;
		}

		ret = devm_request_threaded_irq(dev, info->id_irq, NULL,
						usb_conn_isr, USB_CONN_IRQF,
						pdev->name, info);
		if (ret < 0) {
			dev_err(dev, "failed to request ID IRQ\n");
			goto put_role_sw;
		}
	}

	if (info->vbus_gpiod) {
		info->vbus_irq = gpiod_to_irq(info->vbus_gpiod);
		if (info->vbus_irq < 0) {
			dev_err(dev, "failed to get VBUS IRQ\n");
			ret = info->vbus_irq;
			goto put_role_sw;
		}

		ret = devm_request_threaded_irq(dev, info->vbus_irq, NULL,
						usb_conn_isr, USB_CONN_IRQF,
						pdev->name, info);
		if (ret < 0) {
			dev_err(dev, "failed to request VBUS IRQ\n");
			goto put_role_sw;
		}
	}

	platform_set_drvdata(pdev, info);
	device_set_wakeup_capable(&pdev->dev, true);

	/* Perform initial detection */
	usb_conn_queue_dwork(info, 0);

	return 0;

put_role_sw:
	usb_role_switch_put(info->role_sw);
	return ret;
}

static int usb_conn_remove(struct platform_device *pdev)
{
	struct usb_conn_info *info = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&info->dw_det);

	if (info->last_role == USB_ROLE_HOST && info->vbus)
		regulator_disable(info->vbus);

	usb_role_switch_put(info->role_sw);

	return 0;
}

static int __maybe_unused usb_conn_suspend(struct device *dev)
{
	struct usb_conn_info *info = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (info->id_gpiod)
			enable_irq_wake(info->id_irq);
		if (info->vbus_gpiod)
			enable_irq_wake(info->vbus_irq);
		return 0;
	}

	if (info->id_gpiod)
		disable_irq(info->id_irq);
	if (info->vbus_gpiod)
		disable_irq(info->vbus_irq);

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int __maybe_unused usb_conn_resume(struct device *dev)
{
	struct usb_conn_info *info = dev_get_drvdata(dev);

	if (device_may_wakeup(dev)) {
		if (info->id_gpiod)
			disable_irq_wake(info->id_irq);
		if (info->vbus_gpiod)
			disable_irq_wake(info->vbus_irq);
		return 0;
	}

	pinctrl_pm_select_default_state(dev);

	if (info->id_gpiod)
		enable_irq(info->id_irq);
	if (info->vbus_gpiod)
		enable_irq(info->vbus_irq);

	usb_conn_queue_dwork(info, 0);

	return 0;
}

static SIMPLE_DEV_PM_OPS(usb_conn_pm_ops,
			 usb_conn_suspend, usb_conn_resume);

static const struct of_device_id usb_conn_dt_match[] = {
	{ .compatible = "gpio-usb-b-connector", },
	{ }
};
MODULE_DEVICE_TABLE(of, usb_conn_dt_match);

static struct platform_driver usb_conn_driver = {
	.probe		= usb_conn_probe,
	.remove		= usb_conn_remove,
	.driver		= {
		.name	= "usb-conn-gpio",
		.pm	= &usb_conn_pm_ops,
		.of_match_table = usb_conn_dt_match,
	},
};

module_platform_driver(usb_conn_driver);

MODULE_AUTHOR("Chunfeng Yun <chunfeng.yun@mediatek.com>");
MODULE_DESCRIPTION("USB GPIO based connection detection driver");
MODULE_LICENSE("GPL v2");
