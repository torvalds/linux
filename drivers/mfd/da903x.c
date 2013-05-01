/*
 * Base driver for Dialog Semiconductor DA9030/DA9034
 *
 * Copyright (C) 2008 Compulab, Ltd.
 * 	Mike Rapoport <mike@compulab.co.il>
 *
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * 	Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mfd/da903x.h>
#include <linux/slab.h>

#define DA9030_CHIP_ID		0x00
#define DA9030_EVENT_A		0x01
#define DA9030_EVENT_B		0x02
#define DA9030_EVENT_C		0x03
#define DA9030_STATUS		0x04
#define DA9030_IRQ_MASK_A	0x05
#define DA9030_IRQ_MASK_B	0x06
#define DA9030_IRQ_MASK_C	0x07
#define DA9030_SYS_CTRL_A	0x08
#define DA9030_SYS_CTRL_B	0x09
#define DA9030_FAULT_LOG	0x0a

#define DA9034_CHIP_ID		0x00
#define DA9034_EVENT_A		0x01
#define DA9034_EVENT_B		0x02
#define DA9034_EVENT_C		0x03
#define DA9034_EVENT_D		0x04
#define DA9034_STATUS_A		0x05
#define DA9034_STATUS_B		0x06
#define DA9034_IRQ_MASK_A	0x07
#define DA9034_IRQ_MASK_B	0x08
#define DA9034_IRQ_MASK_C	0x09
#define DA9034_IRQ_MASK_D	0x0a
#define DA9034_SYS_CTRL_A	0x0b
#define DA9034_SYS_CTRL_B	0x0c
#define DA9034_FAULT_LOG	0x0d

struct da903x_chip;

struct da903x_chip_ops {
	int	(*init_chip)(struct da903x_chip *);
	int	(*unmask_events)(struct da903x_chip *, unsigned int events);
	int	(*mask_events)(struct da903x_chip *, unsigned int events);
	int	(*read_events)(struct da903x_chip *, unsigned int *events);
	int	(*read_status)(struct da903x_chip *, unsigned int *status);
};

struct da903x_chip {
	struct i2c_client	*client;
	struct device		*dev;
	struct da903x_chip_ops	*ops;

	int			type;
	uint32_t		events_mask;

	struct mutex		lock;
	struct work_struct	irq_work;

	struct blocking_notifier_head notifier_list;
};

static inline int __da903x_read(struct i2c_client *client,
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

static inline int __da903x_reads(struct i2c_client *client, int reg,
				 int len, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading from 0x%02x\n", reg);
		return ret;
	}
	return 0;
}

static inline int __da903x_write(struct i2c_client *client,
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

static inline int __da903x_writes(struct i2c_client *client, int reg,
				  int len, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writings to 0x%02x\n", reg);
		return ret;
	}
	return 0;
}

int da903x_register_notifier(struct device *dev, struct notifier_block *nb,
				unsigned int events)
{
	struct da903x_chip *chip = dev_get_drvdata(dev);

	chip->ops->unmask_events(chip, events);
	return blocking_notifier_chain_register(&chip->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(da903x_register_notifier);

int da903x_unregister_notifier(struct device *dev, struct notifier_block *nb,
				unsigned int events)
{
	struct da903x_chip *chip = dev_get_drvdata(dev);

	chip->ops->mask_events(chip, events);
	return blocking_notifier_chain_unregister(&chip->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(da903x_unregister_notifier);

int da903x_write(struct device *dev, int reg, uint8_t val)
{
	return __da903x_write(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(da903x_write);

int da903x_writes(struct device *dev, int reg, int len, uint8_t *val)
{
	return __da903x_writes(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(da903x_writes);

int da903x_read(struct device *dev, int reg, uint8_t *val)
{
	return __da903x_read(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(da903x_read);

int da903x_reads(struct device *dev, int reg, int len, uint8_t *val)
{
	return __da903x_reads(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(da903x_reads);

int da903x_set_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct da903x_chip *chip = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&chip->lock);

	ret = __da903x_read(chip->client, reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & bit_mask) != bit_mask) {
		reg_val |= bit_mask;
		ret = __da903x_write(chip->client, reg, reg_val);
	}
out:
	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(da903x_set_bits);

int da903x_clr_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct da903x_chip *chip = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&chip->lock);

	ret = __da903x_read(chip->client, reg, &reg_val);
	if (ret)
		goto out;

	if (reg_val & bit_mask) {
		reg_val &= ~bit_mask;
		ret = __da903x_write(chip->client, reg, reg_val);
	}
out:
	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(da903x_clr_bits);

int da903x_update(struct device *dev, int reg, uint8_t val, uint8_t mask)
{
	struct da903x_chip *chip = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&chip->lock);

	ret = __da903x_read(chip->client, reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & mask) != val) {
		reg_val = (reg_val & ~mask) | val;
		ret = __da903x_write(chip->client, reg, reg_val);
	}
out:
	mutex_unlock(&chip->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(da903x_update);

int da903x_query_status(struct device *dev, unsigned int sbits)
{
	struct da903x_chip *chip = dev_get_drvdata(dev);
	unsigned int status = 0;

	chip->ops->read_status(chip, &status);
	return ((status & sbits) == sbits);
}
EXPORT_SYMBOL(da903x_query_status);

static int da9030_init_chip(struct da903x_chip *chip)
{
	uint8_t chip_id;
	int err;

	err = __da903x_read(chip->client, DA9030_CHIP_ID, &chip_id);
	if (err)
		return err;

	err = __da903x_write(chip->client, DA9030_SYS_CTRL_A, 0xE8);
	if (err)
		return err;

	dev_info(chip->dev, "DA9030 (CHIP ID: 0x%02x) detected\n", chip_id);
	return 0;
}

static int da9030_unmask_events(struct da903x_chip *chip, unsigned int events)
{
	uint8_t v[3];

	chip->events_mask &= ~events;

	v[0] = (chip->events_mask & 0xff);
	v[1] = (chip->events_mask >> 8) & 0xff;
	v[2] = (chip->events_mask >> 16) & 0xff;

	return __da903x_writes(chip->client, DA9030_IRQ_MASK_A, 3, v);
}

static int da9030_mask_events(struct da903x_chip *chip, unsigned int events)
{
	uint8_t v[3];

	chip->events_mask |= events;

	v[0] = (chip->events_mask & 0xff);
	v[1] = (chip->events_mask >> 8) & 0xff;
	v[2] = (chip->events_mask >> 16) & 0xff;

	return __da903x_writes(chip->client, DA9030_IRQ_MASK_A, 3, v);
}

static int da9030_read_events(struct da903x_chip *chip, unsigned int *events)
{
	uint8_t v[3] = {0, 0, 0};
	int ret;

	ret = __da903x_reads(chip->client, DA9030_EVENT_A, 3, v);
	if (ret < 0)
		return ret;

	*events = (v[2] << 16) | (v[1] << 8) | v[0];
	return 0;
}

static int da9030_read_status(struct da903x_chip *chip, unsigned int *status)
{
	return __da903x_read(chip->client, DA9030_STATUS, (uint8_t *)status);
}

static int da9034_init_chip(struct da903x_chip *chip)
{
	uint8_t chip_id;
	int err;

	err = __da903x_read(chip->client, DA9034_CHIP_ID, &chip_id);
	if (err)
		return err;

	err = __da903x_write(chip->client, DA9034_SYS_CTRL_A, 0xE8);
	if (err)
		return err;

	/* avoid SRAM power off during sleep*/
	__da903x_write(chip->client, 0x10, 0x07);
	__da903x_write(chip->client, 0x11, 0xff);
	__da903x_write(chip->client, 0x12, 0xff);

	/* Enable the ONKEY power down functionality */
	__da903x_write(chip->client, DA9034_SYS_CTRL_B, 0x20);
	__da903x_write(chip->client, DA9034_SYS_CTRL_A, 0x60);

	/* workaround to make LEDs work */
	__da903x_write(chip->client, 0x90, 0x01);
	__da903x_write(chip->client, 0xB0, 0x08);

	/* make ADTV1 and SDTV1 effective */
	__da903x_write(chip->client, 0x20, 0x00);

	dev_info(chip->dev, "DA9034 (CHIP ID: 0x%02x) detected\n", chip_id);
	return 0;
}

static int da9034_unmask_events(struct da903x_chip *chip, unsigned int events)
{
	uint8_t v[4];

	chip->events_mask &= ~events;

	v[0] = (chip->events_mask & 0xff);
	v[1] = (chip->events_mask >> 8) & 0xff;
	v[2] = (chip->events_mask >> 16) & 0xff;
	v[3] = (chip->events_mask >> 24) & 0xff;

	return __da903x_writes(chip->client, DA9034_IRQ_MASK_A, 4, v);
}

static int da9034_mask_events(struct da903x_chip *chip, unsigned int events)
{
	uint8_t v[4];

	chip->events_mask |= events;

	v[0] = (chip->events_mask & 0xff);
	v[1] = (chip->events_mask >> 8) & 0xff;
	v[2] = (chip->events_mask >> 16) & 0xff;
	v[3] = (chip->events_mask >> 24) & 0xff;

	return __da903x_writes(chip->client, DA9034_IRQ_MASK_A, 4, v);
}

static int da9034_read_events(struct da903x_chip *chip, unsigned int *events)
{
	uint8_t v[4] = {0, 0, 0, 0};
	int ret;

	ret = __da903x_reads(chip->client, DA9034_EVENT_A, 4, v);
	if (ret < 0)
		return ret;

	*events = (v[3] << 24) | (v[2] << 16) | (v[1] << 8) | v[0];
	return 0;
}

static int da9034_read_status(struct da903x_chip *chip, unsigned int *status)
{
	uint8_t v[2] = {0, 0};
	int ret = 0;

	ret = __da903x_reads(chip->client, DA9034_STATUS_A, 2, v);
	if (ret)
		return ret;

	*status = (v[1] << 8) | v[0];
	return 0;
}

static void da903x_irq_work(struct work_struct *work)
{
	struct da903x_chip *chip =
		container_of(work, struct da903x_chip, irq_work);
	unsigned int events = 0;

	while (1) {
		if (chip->ops->read_events(chip, &events))
			break;

		events &= ~chip->events_mask;
		if (events == 0)
			break;

		blocking_notifier_call_chain(
				&chip->notifier_list, events, NULL);
	}
	enable_irq(chip->client->irq);
}

static irqreturn_t da903x_irq_handler(int irq, void *data)
{
	struct da903x_chip *chip = data;

	disable_irq_nosync(irq);
	(void)schedule_work(&chip->irq_work);

	return IRQ_HANDLED;
}

static struct da903x_chip_ops da903x_ops[] = {
	[0] = {
		.init_chip	= da9030_init_chip,
		.unmask_events	= da9030_unmask_events,
		.mask_events	= da9030_mask_events,
		.read_events	= da9030_read_events,
		.read_status	= da9030_read_status,
	},
	[1] = {
		.init_chip	= da9034_init_chip,
		.unmask_events	= da9034_unmask_events,
		.mask_events	= da9034_mask_events,
		.read_events	= da9034_read_events,
		.read_status	= da9034_read_status,
	}
};

static const struct i2c_device_id da903x_id_table[] = {
	{ "da9030", 0 },
	{ "da9034", 1 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, da903x_id_table);

static int __remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int da903x_remove_subdevs(struct da903x_chip *chip)
{
	return device_for_each_child(chip->dev, NULL, __remove_subdev);
}

static int da903x_add_subdevs(struct da903x_chip *chip,
					struct da903x_platform_data *pdata)
{
	struct da903x_subdev_info *subdev;
	struct platform_device *pdev;
	int i, ret = 0;

	for (i = 0; i < pdata->num_subdevs; i++) {
		subdev = &pdata->subdevs[i];

		pdev = platform_device_alloc(subdev->name, subdev->id);
		if (!pdev) {
			ret = -ENOMEM;
			goto failed;
		}

		pdev->dev.parent = chip->dev;
		pdev->dev.platform_data = subdev->platform_data;

		ret = platform_device_add(pdev);
		if (ret) {
			platform_device_put(pdev);
			goto failed;
		}
	}
	return 0;

failed:
	da903x_remove_subdevs(chip);
	return ret;
}

static int da903x_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct da903x_platform_data *pdata = client->dev.platform_data;
	struct da903x_chip *chip;
	unsigned int tmp;
	int ret;

	chip = kzalloc(sizeof(struct da903x_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;
	chip->ops = &da903x_ops[id->driver_data];

	mutex_init(&chip->lock);
	INIT_WORK(&chip->irq_work, da903x_irq_work);
	BLOCKING_INIT_NOTIFIER_HEAD(&chip->notifier_list);

	i2c_set_clientdata(client, chip);

	ret = chip->ops->init_chip(chip);
	if (ret)
		goto out_free_chip;

	/* mask and clear all IRQs */
	chip->events_mask = 0xffffffff;
	chip->ops->mask_events(chip, chip->events_mask);
	chip->ops->read_events(chip, &tmp);

	ret = request_irq(client->irq, da903x_irq_handler,
			IRQF_TRIGGER_FALLING,
			"da903x", chip);
	if (ret) {
		dev_err(&client->dev, "failed to request irq %d\n",
				client->irq);
		goto out_free_chip;
	}

	ret = da903x_add_subdevs(chip, pdata);
	if (ret)
		goto out_free_irq;

	return 0;

out_free_irq:
	free_irq(client->irq, chip);
out_free_chip:
	kfree(chip);
	return ret;
}

static int da903x_remove(struct i2c_client *client)
{
	struct da903x_chip *chip = i2c_get_clientdata(client);

	da903x_remove_subdevs(chip);
	free_irq(client->irq, chip);
	kfree(chip);
	return 0;
}

static struct i2c_driver da903x_driver = {
	.driver	= {
		.name	= "da903x",
		.owner	= THIS_MODULE,
	},
	.probe		= da903x_probe,
	.remove		= da903x_remove,
	.id_table	= da903x_id_table,
};

static int __init da903x_init(void)
{
	return i2c_add_driver(&da903x_driver);
}
subsys_initcall(da903x_init);

static void __exit da903x_exit(void)
{
	i2c_del_driver(&da903x_driver);
}
module_exit(da903x_exit);

MODULE_DESCRIPTION("PMIC Driver for Dialog Semiconductor DA9034");
MODULE_AUTHOR("Eric Miao <eric.miao@marvell.com>"
	      "Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL");
