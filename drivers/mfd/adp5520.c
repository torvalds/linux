/*
 * Base driver for Analog Devices ADP5520/ADP5501 MFD PMICs
 * LCD Backlight: drivers/video/backlight/adp5520_bl
 * LEDs		: drivers/led/leds-adp5520
 * GPIO		: drivers/gpio/adp5520-gpio (ADP5520 only)
 * Keys		: drivers/input/keyboard/adp5520-keys (ADP5520 only)
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Derived from da903x:
 * Copyright (C) 2008 Compulab, Ltd.
 * 	Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * 	Eric Miao <eric.miao@marvell.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/i2c.h>

#include <linux/mfd/adp5520.h>

struct adp5520_chip {
	struct i2c_client *client;
	struct device *dev;
	struct mutex lock;
	struct blocking_notifier_head notifier_list;
	int irq;
	unsigned long id;
	uint8_t mode;
};

static int __adp5520_read(struct i2c_client *client,
				int reg, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
		return ret;
	}

	*val = (uint8_t)ret;
	return 0;
}

static int __adp5520_write(struct i2c_client *client,
				 int reg, uint8_t val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writing 0x%02x to 0x%02x\n",
				val, reg);
		return ret;
	}
	return 0;
}

static int __adp5520_ack_bits(struct i2c_client *client, int reg,
			      uint8_t bit_mask)
{
	struct adp5520_chip *chip = i2c_get_clientdata(client);
	uint8_t reg_val;
	int ret;

	mutex_lock(&chip->lock);

	ret = __adp5520_read(client, reg, &reg_val);

	if (!ret) {
		reg_val |= bit_mask;
		ret = __adp5520_write(client, reg, reg_val);
	}

	mutex_unlock(&chip->lock);
	return ret;
}

int adp5520_write(struct device *dev, int reg, uint8_t val)
{
	return __adp5520_write(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(adp5520_write);

int adp5520_read(struct device *dev, int reg, uint8_t *val)
{
	return __adp5520_read(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(adp5520_read);

int adp5520_set_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct adp5520_chip *chip = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret;

	mutex_lock(&chip->lock);

	ret = __adp5520_read(chip->client, reg, &reg_val);

	if (!ret && ((reg_val & bit_mask) == 0)) {
		reg_val |= bit_mask;
		ret = __adp5520_write(chip->client, reg, reg_val);
	}

	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(adp5520_set_bits);

int adp5520_clr_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct adp5520_chip *chip = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret;

	mutex_lock(&chip->lock);

	ret = __adp5520_read(chip->client, reg, &reg_val);

	if (!ret && (reg_val & bit_mask)) {
		reg_val &= ~bit_mask;
		ret = __adp5520_write(chip->client, reg, reg_val);
	}

	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(adp5520_clr_bits);

int adp5520_register_notifier(struct device *dev, struct notifier_block *nb,
				unsigned int events)
{
	struct adp5520_chip *chip = dev_get_drvdata(dev);

	if (chip->irq) {
		adp5520_set_bits(chip->dev, ADP5520_INTERRUPT_ENABLE,
			events & (ADP5520_KP_IEN | ADP5520_KR_IEN |
			ADP5520_OVP_IEN | ADP5520_CMPR_IEN));

		return blocking_notifier_chain_register(&chip->notifier_list,
			 nb);
	}

	return -ENODEV;
}
EXPORT_SYMBOL_GPL(adp5520_register_notifier);

int adp5520_unregister_notifier(struct device *dev, struct notifier_block *nb,
				unsigned int events)
{
	struct adp5520_chip *chip = dev_get_drvdata(dev);

	adp5520_clr_bits(chip->dev, ADP5520_INTERRUPT_ENABLE,
		events & (ADP5520_KP_IEN | ADP5520_KR_IEN |
		ADP5520_OVP_IEN | ADP5520_CMPR_IEN));

	return blocking_notifier_chain_unregister(&chip->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(adp5520_unregister_notifier);

static irqreturn_t adp5520_irq_thread(int irq, void *data)
{
	struct adp5520_chip *chip = data;
	unsigned int events;
	uint8_t reg_val;
	int ret;

	ret = __adp5520_read(chip->client, ADP5520_MODE_STATUS, &reg_val);
	if (ret)
		goto out;

	events =  reg_val & (ADP5520_OVP_INT | ADP5520_CMPR_INT |
		ADP5520_GPI_INT | ADP5520_KR_INT | ADP5520_KP_INT);

	blocking_notifier_call_chain(&chip->notifier_list, events, NULL);
	/* ACK, Sticky bits are W1C */
	__adp5520_ack_bits(chip->client, ADP5520_MODE_STATUS, events);

out:
	return IRQ_HANDLED;
}

static int __remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int adp5520_remove_subdevs(struct adp5520_chip *chip)
{
	return device_for_each_child(chip->dev, NULL, __remove_subdev);
}

static int __devinit adp5520_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct adp5520_platform_data *pdata = client->dev.platform_data;
	struct platform_device *pdev;
	struct adp5520_chip *chip;
	int ret;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Word Data not Supported\n");
		return -EIO;
	}

	if (pdata == NULL) {
		dev_err(&client->dev, "missing platform data\n");
		return -ENODEV;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client = client;

	chip->dev = &client->dev;
	chip->irq = client->irq;
	chip->id = id->driver_data;
	mutex_init(&chip->lock);

	if (chip->irq) {
		BLOCKING_INIT_NOTIFIER_HEAD(&chip->notifier_list);

		ret = request_threaded_irq(chip->irq, NULL, adp5520_irq_thread,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				"adp5520", chip);
		if (ret) {
			dev_err(&client->dev, "failed to request irq %d\n",
					chip->irq);
			goto out_free_chip;
		}
	}

	ret = adp5520_write(chip->dev, ADP5520_MODE_STATUS, ADP5520_nSTNBY);
	if (ret) {
		dev_err(&client->dev, "failed to write\n");
		goto out_free_irq;
	}

	if (pdata->keys) {
		pdev = platform_device_register_data(chip->dev, "adp5520-keys",
				chip->id, pdata->keys, sizeof(*pdata->keys));
		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			goto out_remove_subdevs;
		}
	}

	if (pdata->gpio) {
		pdev = platform_device_register_data(chip->dev, "adp5520-gpio",
				chip->id, pdata->gpio, sizeof(*pdata->gpio));
		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			goto out_remove_subdevs;
		}
	}

	if (pdata->leds) {
		pdev = platform_device_register_data(chip->dev, "adp5520-led",
				chip->id, pdata->leds, sizeof(*pdata->leds));
		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			goto out_remove_subdevs;
		}
	}

	if (pdata->backlight) {
		pdev = platform_device_register_data(chip->dev,
						"adp5520-backlight",
						chip->id,
						pdata->backlight,
						sizeof(*pdata->backlight));
		if (IS_ERR(pdev)) {
			ret = PTR_ERR(pdev);
			goto out_remove_subdevs;
		}
	}

	return 0;

out_remove_subdevs:
	adp5520_remove_subdevs(chip);

out_free_irq:
	if (chip->irq)
		free_irq(chip->irq, chip);

out_free_chip:
	kfree(chip);

	return ret;
}

static int __devexit adp5520_remove(struct i2c_client *client)
{
	struct adp5520_chip *chip = dev_get_drvdata(&client->dev);

	if (chip->irq)
		free_irq(chip->irq, chip);

	adp5520_remove_subdevs(chip);
	adp5520_write(chip->dev, ADP5520_MODE_STATUS, 0);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int adp5520_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adp5520_chip *chip = dev_get_drvdata(&client->dev);

	adp5520_read(chip->dev, ADP5520_MODE_STATUS, &chip->mode);
	/* All other bits are W1C */
	chip->mode &= ADP5520_BL_EN | ADP5520_DIM_EN | ADP5520_nSTNBY;
	adp5520_write(chip->dev, ADP5520_MODE_STATUS, 0);
	return 0;
}

static int adp5520_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adp5520_chip *chip = dev_get_drvdata(&client->dev);

	adp5520_write(chip->dev, ADP5520_MODE_STATUS, chip->mode);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(adp5520_pm, adp5520_suspend, adp5520_resume);

static const struct i2c_device_id adp5520_id[] = {
	{ "pmic-adp5520", ID_ADP5520 },
	{ "pmic-adp5501", ID_ADP5501 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp5520_id);

static struct i2c_driver adp5520_driver = {
	.driver = {
		.name	= "adp5520",
		.owner	= THIS_MODULE,
		.pm	= &adp5520_pm,
	},
	.probe		= adp5520_probe,
	.remove		= __devexit_p(adp5520_remove),
	.id_table 	= adp5520_id,
};

static int __init adp5520_init(void)
{
	return i2c_add_driver(&adp5520_driver);
}
module_init(adp5520_init);

static void __exit adp5520_exit(void)
{
	i2c_del_driver(&adp5520_driver);
}
module_exit(adp5520_exit);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADP5520(01) PMIC-MFD Driver");
MODULE_LICENSE("GPL");
