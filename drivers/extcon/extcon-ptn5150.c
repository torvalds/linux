// SPDX-License-Identifier: GPL-2.0+
//
// extcon-ptn5150.c - PTN5150 CC logic extcon driver to support USB detection
//
// Based on extcon-sm5502.c driver
// Copyright (c) 2018-2019 by Vijai Kumar K
// Author: Vijai Kumar K <vijaikumar.kanagarajan@gmail.com>

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/extcon-provider.h>
#include <linux/gpio/consumer.h>

/* PTN5150 registers */
enum ptn5150_reg {
	PTN5150_REG_DEVICE_ID = 0x01,
	PTN5150_REG_CONTROL,
	PTN5150_REG_INT_STATUS,
	PTN5150_REG_CC_STATUS,
	PTN5150_REG_CON_DET = 0x09,
	PTN5150_REG_VCONN_STATUS,
	PTN5150_REG_RESET,
	PTN5150_REG_INT_MASK = 0x18,
	PTN5150_REG_INT_REG_STATUS,
	PTN5150_REG_END,
};

#define PTN5150_DFP_ATTACHED			0x1
#define PTN5150_UFP_ATTACHED			0x2

/* Define PTN5150 MASK/SHIFT constant */
#define PTN5150_REG_DEVICE_ID_VENDOR_SHIFT	0
#define PTN5150_REG_DEVICE_ID_VENDOR_MASK	\
	(0x3 << PTN5150_REG_DEVICE_ID_VENDOR_SHIFT)

#define PTN5150_REG_DEVICE_ID_VERSION_SHIFT	3
#define PTN5150_REG_DEVICE_ID_VERSION_MASK	\
	(0x1f << PTN5150_REG_DEVICE_ID_VERSION_SHIFT)

#define PTN5150_REG_CC_PORT_ATTACHMENT_SHIFT	2
#define PTN5150_REG_CC_PORT_ATTACHMENT_MASK	\
	(0x7 << PTN5150_REG_CC_PORT_ATTACHMENT_SHIFT)

#define PTN5150_REG_CC_VBUS_DETECTION_SHIFT	7
#define PTN5150_REG_CC_VBUS_DETECTION_MASK	\
	(0x1 << PTN5150_REG_CC_VBUS_DETECTION_SHIFT)

#define PTN5150_REG_INT_CABLE_ATTACH_SHIFT	0
#define PTN5150_REG_INT_CABLE_ATTACH_MASK	\
	(0x1 << PTN5150_REG_INT_CABLE_ATTACH_SHIFT)

#define PTN5150_REG_INT_CABLE_DETACH_SHIFT	1
#define PTN5150_REG_INT_CABLE_DETACH_MASK	\
	(0x1 << PTN5150_REG_CC_CABLE_DETACH_SHIFT)

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

static void ptn5150_irq_work(struct work_struct *work)
{
	struct ptn5150_info *info = container_of(work,
			struct ptn5150_info, irq_work);
	int ret = 0;
	unsigned int reg_data;
	unsigned int int_status;

	if (!info->edev)
		return;

	mutex_lock(&info->mutex);

	ret = regmap_read(info->regmap, PTN5150_REG_CC_STATUS, &reg_data);
	if (ret) {
		dev_err(info->dev, "failed to read CC STATUS %d\n", ret);
		mutex_unlock(&info->mutex);
		return;
	}

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
			unsigned int port_status;
			unsigned int vbus;

			port_status = ((reg_data &
					PTN5150_REG_CC_PORT_ATTACHMENT_MASK) >>
					PTN5150_REG_CC_PORT_ATTACHMENT_SHIFT);

			switch (port_status) {
			case PTN5150_DFP_ATTACHED:
				extcon_set_state_sync(info->edev,
						EXTCON_USB_HOST, false);
				gpiod_set_value(info->vbus_gpiod, 0);
				extcon_set_state_sync(info->edev, EXTCON_USB,
						true);
				break;
			case PTN5150_UFP_ATTACHED:
				extcon_set_state_sync(info->edev, EXTCON_USB,
						false);
				vbus = ((reg_data &
					PTN5150_REG_CC_VBUS_DETECTION_MASK) >>
					PTN5150_REG_CC_VBUS_DETECTION_SHIFT);
				if (vbus)
					gpiod_set_value(info->vbus_gpiod, 0);
				else
					gpiod_set_value(info->vbus_gpiod, 1);

				extcon_set_state_sync(info->edev,
						EXTCON_USB_HOST, true);
				break;
			default:
				dev_err(info->dev,
					"Unknown Port status : %x\n",
					port_status);
				break;
			}
		} else {
			extcon_set_state_sync(info->edev,
					EXTCON_USB_HOST, false);
			extcon_set_state_sync(info->edev,
					EXTCON_USB, false);
			gpiod_set_value(info->vbus_gpiod, 0);
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

	vendor_id = ((reg_data & PTN5150_REG_DEVICE_ID_VENDOR_MASK) >>
				PTN5150_REG_DEVICE_ID_VENDOR_SHIFT);
	version_id = ((reg_data & PTN5150_REG_DEVICE_ID_VERSION_MASK) >>
				PTN5150_REG_DEVICE_ID_VERSION_SHIFT);

	dev_info(info->dev, "Device type: version: 0x%x, vendor: 0x%x\n",
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

static int ptn5150_i2c_probe(struct i2c_client *i2c,
				 const struct i2c_device_id *id)
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
	info->int_gpiod = devm_gpiod_get(&i2c->dev, "int", GPIOD_IN);
	if (IS_ERR(info->int_gpiod)) {
		dev_err(dev, "failed to get INT GPIO\n");
		return PTR_ERR(info->int_gpiod);
	}
	info->vbus_gpiod = devm_gpiod_get(&i2c->dev, "vbus", GPIOD_IN);
	if (IS_ERR(info->vbus_gpiod)) {
		dev_err(dev, "failed to get VBUS GPIO\n");
		return PTR_ERR(info->vbus_gpiod);
	}
	ret = gpiod_direction_output(info->vbus_gpiod, 0);
	if (ret) {
		dev_err(dev, "failed to set VBUS GPIO direction\n");
		return -EINVAL;
	}

	mutex_init(&info->mutex);

	INIT_WORK(&info->irq_work, ptn5150_irq_work);

	info->regmap = devm_regmap_init_i2c(i2c, &ptn5150_regmap_config);
	if (IS_ERR(info->regmap)) {
		ret = PTR_ERR(info->regmap);
		dev_err(info->dev, "failed to allocate register map: %d\n",
				   ret);
		return ret;
	}

	if (info->int_gpiod) {
		info->irq = gpiod_to_irq(info->int_gpiod);
		if (info->irq < 0) {
			dev_err(dev, "failed to get INTB IRQ\n");
			return info->irq;
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

	/* Initialize PTN5150 device and print vendor id and version id */
	ret = ptn5150_init_dev_type(info);
	if (ret)
		return -EINVAL;

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
	.probe	= ptn5150_i2c_probe,
	.id_table = ptn5150_i2c_id,
};

static int __init ptn5150_i2c_init(void)
{
	return i2c_add_driver(&ptn5150_i2c_driver);
}
subsys_initcall(ptn5150_i2c_init);

MODULE_DESCRIPTION("NXP PTN5150 CC logic Extcon driver");
MODULE_AUTHOR("Vijai Kumar K <vijaikumar.kanagarajan@gmail.com>");
MODULE_LICENSE("GPL v2");
