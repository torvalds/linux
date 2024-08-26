// SPDX-License-Identifier: GPL-2.0-only
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include <linux/mfd/88pm886.h>

struct pm886_onkey {
	struct input_dev *idev;
	struct pm886_chip *chip;
};

static irqreturn_t pm886_onkey_irq_handler(int irq, void *data)
{
	struct pm886_onkey *onkey = data;
	struct regmap *regmap = onkey->chip->regmap;
	struct input_dev *idev = onkey->idev;
	struct device *parent = idev->dev.parent;
	unsigned int val;
	int err;

	err = regmap_read(regmap, PM886_REG_STATUS1, &val);
	if (err) {
		dev_err(parent, "Failed to read status: %d\n", err);
		return IRQ_NONE;
	}
	val &= PM886_ONKEY_STS1;

	input_report_key(idev, KEY_POWER, val);
	input_sync(idev);

	return IRQ_HANDLED;
}

static int pm886_onkey_probe(struct platform_device *pdev)
{
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct pm886_onkey *onkey;
	struct input_dev *idev;
	int irq, err;

	onkey = devm_kzalloc(dev, sizeof(*onkey), GFP_KERNEL);
	if (!onkey)
		return -ENOMEM;

	onkey->chip = chip;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "Failed to get IRQ\n");

	idev = devm_input_allocate_device(dev);
	if (!idev) {
		dev_err(dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}
	onkey->idev = idev;

	idev->name = "88pm886-onkey";
	idev->phys = "88pm886-onkey/input0";
	idev->id.bustype = BUS_I2C;

	input_set_capability(idev, EV_KEY, KEY_POWER);

	err = devm_request_threaded_irq(dev, irq, NULL, pm886_onkey_irq_handler,
					IRQF_ONESHOT | IRQF_NO_SUSPEND, "onkey",
					onkey);
	if (err)
		return dev_err_probe(dev, err, "Failed to request IRQ\n");

	err = input_register_device(idev);
	if (err)
		return dev_err_probe(dev, err, "Failed to register input device\n");

	return 0;
}

static const struct platform_device_id pm886_onkey_id_table[] = {
	{ "88pm886-onkey", },
	{ }
};
MODULE_DEVICE_TABLE(platform, pm886_onkey_id_table);

static struct platform_driver pm886_onkey_driver = {
	.driver = {
		.name = "88pm886-onkey",
	},
	.probe = pm886_onkey_probe,
	.id_table = pm886_onkey_id_table,
};
module_platform_driver(pm886_onkey_driver);

MODULE_DESCRIPTION("Marvell 88PM886 onkey driver");
MODULE_AUTHOR("Karel Balej <balejk@matfyz.cz>");
MODULE_LICENSE("GPL");
