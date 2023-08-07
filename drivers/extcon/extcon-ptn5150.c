// SPDX-License-Identifier: GPL-2.0+
//
// extcon-ptn5150.c - PTN5150 CC logic extcon driver to support USB detection
//
// Based on extcon-sm5502.c driver
// Copyright (c) 2018-2019 by Vijai Kumar K
// Author: Vijai Kumar K <vijaikumar.kanagarajan@gmail.com>
// Copyright (c) 2020 Krzysztof Kozlowski <krzk@kernel.org>

#include <linux/bitfield.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>
#include <linux/usb/role.h>

/* PTN5150 registers */
#define PTN5150_REG_DEVICE_ID			0x01
#define PTN5150_REG_CONTROL			0x02
#define PTN5150_REG_INT_STATUS			0x03
#define PTN5150_REG_CC_STATUS			0x04
#define PTN5150_REG_CON_DET			0x09
#define PTN5150_REG_VCONN_STATUS		0x0a
#define PTN5150_REG_RESET			0x0b
#define PTN5150_REG_INT_MASK			0x18
#define PTN5150_REG_INT_REG_STATUS		0x19
#define PTN5150_REG_END				PTN5150_REG_INT_REG_STATUS

#define PTN5150_DFP_ATTACHED			0x1
#define PTN5150_UFP_ATTACHED			0x2

/* Define PTN5150 MASK/SHIFT constant */
#define PTN5150_REG_DEVICE_ID_VERSION		GENMASK(7, 3)
#define PTN5150_REG_DEVICE_ID_VENDOR		GENMASK(2, 0)

#define PTN5150_REG_CC_PORT_ATTACHMENT		GENMASK(4, 2)
#define PTN5150_REG_CC_VBUS_DETECTION		BIT(7)
#define PTN5150_REG_INT_CABLE_ATTACH_MASK	BIT(0)
#define PTN5150_REG_INT_CABLE_DETACH_MASK	BIT(1)

struct ptn5150_info {
	struct device *dev;
	struct extcon_dev *edev;
	struct i2c_client *i2c;
	struct regmap *regmap;
	struct gpio_desc *int_gpiod;
	struct gpio_desc *vbus_gpiod;
	int irq;
	struct work_struct irq_work;
	struct mutex mutex;
	struct usb_role_switch *role_sw;
};

/* List of detectable cables */
static const unsigned int ptn5150_extcon_cable[] = {
	EXTCON_USB,
	EXTCON_USB_HOST,
	EXTCON_NONE,
};

static const struct regmap_config ptn5150_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= PTN5150_REG_END,
};

static void ptn5150_check_state(struct ptn5150_info *info)
{
	unsigned int port_status, reg_data, vbus;
	enum usb_role usb_role = USB_ROLE_NONE;
	int ret;

	ret = regmap_read(info->regmap, PTN5150_REG_CC_STATUS, &reg_data);
	if (ret) {
		dev_err(info->dev, "failed to read CC STATUS %d\n", ret);
		return;
	}

	port_status = FIELD_GET(PTN5150_REG_CC_PORT_ATTACHMENT, reg_data);

	switch (port_status) {
	case PTN5150_DFP_ATTACHED:
		extcon_set_state_sync(info->edev, EXTCON_USB_HOST, false);
		gpiod_set_value_cansleep(info->vbus_gpiod, 0);
		extcon_set_state_sync(info->edev, EXTCON_USB, true);
		usb_role = USB_ROLE_DEVICE;
		break;
	case PTN5150_UFP_ATTACHED:
		extcon_set_state_sync(info->edev, EXTCON_USB, false);
		vbus = FIELD_GET(PTN5150_REG_CC_VBUS_DETECTION, reg_data);
		if (vbus)
			gpiod_set_value_cansleep(info->vbus_gpiod, 0);
		else
			gpiod_set_value_cansleep(info->vbus_gpiod, 1);

		extcon_set_state_sync(info->edev, EXTCON_USB_HOST, true);
		usb_role = USB_ROLE_HOST;
		break;
	default:
		break;
	}

	if (usb_role) {
		ret = usb_role_switch_set_role(info->role_sw, usb_role);
		if (ret)
			dev_err(info->dev, "failed to set %s role: %d\n",
				usb_role_string(usb_role), ret);
	}
}

static void ptn5150_irq_work(struct work_struct *work)
{
	struct ptn5150_info *info = container_of(work,
			struct ptn5150_info, irq_work);
	int ret = 0;
	unsigned int int_status;

	if (!info->edev)
		return;

	mutex_lock(&info->mutex);

	/* Clear interrupt. Read would clear the register */
	ret = regmap_read(info->regmap, PTN5150_REG_INT_STATUS, &int_status);
	if (ret) {
		dev_err(info->dev, "failed to read INT STATUS %d\n", ret);
		mutex_unlock(&info->mutex);
		return;
	}

	if (int_status) {
		unsigned int cable_attach;

		cable_attach = int_status & PTN5150_REG_INT_CABLE_ATTACH_MASK;
		if (cable_attach) {
			ptn5150_check_state(info);
		} else {
			extcon_set_state_sync(info->edev,
					EXTCON_USB_HOST, false);
			extcon_set_state_sync(info->edev,
					EXTCON_USB, false);
			gpiod_set_value_cansleep(info->vbus_gpiod, 0);

			ret = usb_role_switch_set_role(info->role_sw,
						       USB_ROLE_NONE);
			if (ret)
				dev_err(info->dev,
					"failed to set none role: %d\n",
					ret);
		}
	}

	/* Clear interrupt. Read would clear the register */
	ret = regmap_read(info->regmap, PTN5150_REG_INT_REG_STATUS,
			&int_status);
	if (ret) {
		dev_err(info->dev,
			"failed to read INT REG STATUS %d\n", ret);
		mutex_unlock(&info->mutex);
		return;
	}

	mutex_unlock(&info->mutex);
}


static irqreturn_t ptn5150_irq_handler(int irq, void *data)
{
	struct ptn5150_info *info = data;

	schedule_work(&info->irq_work);

	return IRQ_HANDLED;
}

static int ptn5150_init_dev_type(struct ptn5150_info *info)
{
	unsigned int reg_data, vendor_id, version_id;
	int ret;

	ret = regmap_read(info->regmap, PTN5150_REG_DEVICE_ID, &reg_data);
	if (ret) {
		dev_err(info->dev, "failed to read DEVICE_ID %d\n", ret);
		return -EINVAL;
	}

	vendor_id = FIELD_GET(PTN5150_REG_DEVICE_ID_VENDOR, reg_data);
	version_id = FIELD_GET(PTN5150_REG_DEVICE_ID_VERSION, reg_data);
	dev_dbg(info->dev, "Device type: version: 0x%x, vendor: 0x%x\n",
		version_id, vendor_id);

	/* Clear any existing interrupts */
	ret = regmap_read(info->regmap, PTN5150_REG_INT_STATUS, &reg_data);
	if (ret) {
		dev_err(info->dev,
			"failed to read PTN5150_REG_INT_STATUS %d\n",
			ret);
		return -EINVAL;
	}

	ret = regmap_read(info->regmap, PTN5150_REG_INT_REG_STATUS, &reg_data);
	if (ret) {
		dev_err(info->dev,
			"failed to read PTN5150_REG_INT_REG_STATUS %d\n", ret);
		return -EINVAL;
	}

	return 0;
}

static void ptn5150_work_sync_and_put(void *data)
{
	struct ptn5150_info *info = data;

	cancel_work_sync(&info->irq_work);
	usb_role_switch_put(info->role_sw);
}

static int ptn5150_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct device_node *np = i2c->dev.of_node;
	struct ptn5150_info *info;
	int ret;

	if (!np)
		return -EINVAL;

	info = devm_kzalloc(&i2c->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	i2c_set_clientdata(i2c, info);

	info->dev = &i2c->dev;
	info->i2c = i2c;
	info->vbus_gpiod = devm_gpiod_get(&i2c->dev, "vbus", GPIOD_OUT_LOW);
	if (IS_ERR(info->vbus_gpiod)) {
		ret = PTR_ERR(info->vbus_gpiod);
		if (ret == -ENOENT) {
			dev_info(dev, "No VBUS GPIO, ignoring VBUS control\n");
			info->vbus_gpiod = NULL;
		} else {
			return dev_err_probe(dev, ret, "failed to get VBUS GPIO\n");
		}
	}

	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, ptn5150_irq_work);

	info->regmap = devm_regmap_init_i2c(i2c, &ptn5150_regmap_config);
	if (IS_ERR(info->regmap)) {
		return dev_err_probe(info->dev, PTR_ERR(info->regmap),
				     "failed to allocate register map\n");
	}

	if (i2c->irq > 0) {
		info->irq = i2c->irq;
	} else {
		info->int_gpiod = devm_gpiod_get(&i2c->dev, "int", GPIOD_IN);
		if (IS_ERR(info->int_gpiod)) {
			return dev_err_probe(dev, PTR_ERR(info->int_gpiod),
					     "failed to get INT GPIO\n");
		}

		info->irq = gpiod_to_irq(info->int_gpiod);
		if (info->irq < 0) {
			dev_err(dev, "failed to get INTB IRQ\n");
			return info->irq;
		}
	}

	ret = devm_request_threaded_irq(dev, info->irq, NULL,
					ptn5150_irq_handler,
					IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT,
					i2c->name, info);
	if (ret < 0) {
		dev_err(dev, "failed to request handler for INTB IRQ\n");
		return ret;
	}

	/* Allocate extcon device */
	info->edev = devm_extcon_dev_allocate(info->dev, ptn5150_extcon_cable);
	if (IS_ERR(info->edev)) {
		dev_err(info->dev, "failed to allocate memory for extcon\n");
		return -ENOMEM;
	}

	/* Register extcon device */
	ret = devm_extcon_dev_register(info->dev, info->edev);
	if (ret) {
		dev_err(info->dev, "failed to register extcon device\n");
		return ret;
	}

	extcon_set_property_capability(info->edev, EXTCON_USB,
					EXTCON_PROP_USB_VBUS);
	extcon_set_property_capability(info->edev, EXTCON_USB_HOST,
					EXTCON_PROP_USB_VBUS);
	extcon_set_property_capability(info->edev, EXTCON_USB_HOST,
					EXTCON_PROP_USB_TYPEC_POLARITY);

	/* Initialize PTN5150 device and print vendor id and version id */
	ret = ptn5150_init_dev_type(info);
	if (ret)
		return -EINVAL;

	info->role_sw = usb_role_switch_get(info->dev);
	if (IS_ERR(info->role_sw))
		return dev_err_probe(info->dev, PTR_ERR(info->role_sw),
				     "failed to get role switch\n");

	ret = devm_add_action_or_reset(dev, ptn5150_work_sync_and_put, info);
	if (ret)
		return ret;

	/*
	 * Update current extcon state if for example OTG connection was there
	 * before the probe
	 */
	mutex_lock(&info->mutex);
	ptn5150_check_state(info);
	mutex_unlock(&info->mutex);

	return 0;
}

static const struct of_device_id ptn5150_dt_match[] = {
	{ .compatible = "nxp,ptn5150" },
	{ },
};
MODULE_DEVICE_TABLE(of, ptn5150_dt_match);

static const struct i2c_device_id ptn5150_i2c_id[] = {
	{ "ptn5150", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ptn5150_i2c_id);

static struct i2c_driver ptn5150_i2c_driver = {
	.driver		= {
		.name	= "ptn5150",
		.of_match_table = ptn5150_dt_match,
	},
	.probe		= ptn5150_i2c_probe,
	.id_table = ptn5150_i2c_id,
};
module_i2c_driver(ptn5150_i2c_driver);

MODULE_DESCRIPTION("NXP PTN5150 CC logic Extcon driver");
MODULE_AUTHOR("Vijai Kumar K <vijaikumar.kanagarajan@gmail.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_LICENSE("GPL v2");
