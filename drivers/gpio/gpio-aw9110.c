// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for aw9110 I2C GPIO expanders
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of_gpio.h>

#define REG_INPUT_P0        0x00
#define REG_INPUT_P1        0x01
#define REG_OUTPUT_P0       0x02
#define REG_OUTPUT_P1       0x03
#define REG_CONFIG_P0       0x04
#define REG_CONFIG_P1       0x05
#define REG_INT_P0          0x06
#define REG_INT_P1          0x07
#define REG_ID              0x10
#define REG_CTRL            0x11
#define REG_WORK_MODE_P0    0x12
#define REG_WORK_MODE_P1    0x13
#define REG_EN_BREATH       0x14
#define REG_FADE_TIME       0x15
#define REG_FULL_TIME       0x16
#define REG_DLY0_BREATH     0x17
#define REG_DLY1_BREATH     0x18
#define REG_DLY2_BREATH     0x19
#define REG_DLY3_BREATH     0x1a
#define REG_DLY4_BREATH     0x1b
#define REG_DLY5_BREATH     0x1c
#define REG_DIM00           0x20
#define REG_DIM01           0x21
#define REG_DIM02           0x22
#define REG_DIM03           0x23
#define REG_DIM04           0x24
#define REG_DIM05           0x25
#define REG_DIM06           0x26
#define REG_DIM07           0x27
#define REG_DIM08           0x28
#define REG_DIM09           0x29
#define REG_SWRST           0x7F
#define REG_81H             0x81


static const struct i2c_device_id aw9110_id[] = {
	{ "aw9110", 10 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw9110_id);

#ifdef CONFIG_OF
static const struct of_device_id aw9110_of_table[] = {
	{ .compatible = "awinic,aw9110" },
	{ }
};
MODULE_DEVICE_TABLE(of, aw9110_of_table);
#endif


struct aw9110 {
	struct gpio_chip	chip;
	struct irq_chip		irqchip;
	struct i2c_client	*client;
	struct mutex		lock;		/* protect 'out' */
	unsigned int		out;		/* software latch */
	unsigned int		direct;		/* gpio direct */
	unsigned int		status;		/* current status */
	unsigned int		irq_enabled;	/* enabled irqs */

	struct device		*dev;
	int			shdn_en;	/* shutdown ctrl */

	int (*write)(struct i2c_client *client, u8 reg, u8 data);
	int (*read)(struct i2c_client *client, u8 reg);
};


static int aw9110_i2c_write_le8(struct i2c_client *client, u8 reg, u8 data)
{
	return i2c_smbus_write_byte_data(client, reg, data);
}

static int aw9110_i2c_read_le8(struct i2c_client *client, u8 reg)
{
	return (int)i2c_smbus_read_byte_data(client, reg);
}

static int aw9110_get(struct gpio_chip *chip, unsigned int offset)
{
	struct aw9110	*gpio = gpiochip_get_data(chip);
	int value = 0;

	mutex_lock(&gpio->lock);

	if (offset < 4) {
		value = gpio->read(gpio->client, REG_INPUT_P1);
		mutex_unlock(&gpio->lock);

		value = (value < 0) ? value : !!(value & (1 << offset));
	} else {
		value = gpio->read(gpio->client, REG_INPUT_P0);
		mutex_unlock(&gpio->lock);

		value = (value < 0) ? value : !!((value<<4) & (1 << offset));
	}

	return value;
}

static int aw9110_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct aw9110	*gpio = gpiochip_get_data(chip);
	unsigned int reg_val;

	reg_val = gpio->direct;

	dev_dbg(gpio->dev, "direct get: %04X, pin:%d\n", reg_val, offset);

	if (reg_val & (1<<offset))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int aw9110_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct aw9110	*gpio = gpiochip_get_data(chip);

	mutex_lock(&gpio->lock);

	/* set direct */
	gpio->direct |= (1<<offset);

	if (offset < 4)
		gpio->write(gpio->client, REG_CONFIG_P1, gpio->direct&0x0F);
	else
		gpio->write(gpio->client, REG_CONFIG_P0, (gpio->direct >> 4)&0x3F);

	mutex_unlock(&gpio->lock);

	dev_dbg(gpio->dev, "direct in: %04X, pin:%d\n", gpio->direct, offset);

	return 0;
}

static int aw9110_direction_output(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct aw9110	*gpio = gpiochip_get_data(chip);

	/* set level */
	chip->set(chip, offset, value);

	mutex_lock(&gpio->lock);

	/* set direct */
	gpio->direct &= ~(1<<offset);

	if (offset < 4)
		gpio->write(gpio->client, REG_CONFIG_P1, gpio->direct&0x0F);
	else
		gpio->write(gpio->client, REG_CONFIG_P0, (gpio->direct >> 4)&0x3F);

	mutex_unlock(&gpio->lock);

	dev_dbg(gpio->dev, "direct out: %04X, pin:%d\n", gpio->direct, offset);
	return 0;
}

static void aw9110_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct aw9110 *gpio = gpiochip_get_data(chip);
	unsigned int bit = 1 << offset;

	mutex_lock(&gpio->lock);

	if (value)
		gpio->out |= bit;
	else
		gpio->out &= ~bit;

	if (offset < 4)
		gpio->write(gpio->client, REG_OUTPUT_P1, gpio->out >> 0);
	else
		gpio->write(gpio->client, REG_OUTPUT_P0, gpio->out >> 4);

	mutex_unlock(&gpio->lock);
}

/*-------------------------------------------------------------------------*/

static irqreturn_t aw9110_irq(int irq, void *data)
{
	struct aw9110  *gpio = data;
	unsigned long change, i, status = 0;

	int value = 0;
	int nirq;

	value = gpio->read(gpio->client, REG_INPUT_P1);
	status |= (value < 0) ? 0 : value;

	value = gpio->read(gpio->client, REG_INPUT_P0);
	status |= (value < 0) ? 0 : (value<<4);


	/*
	 * call the interrupt handler iff gpio is used as
	 * interrupt source, just to avoid bad irqs
	 */
	mutex_lock(&gpio->lock);
	change = (gpio->status ^ status) & gpio->irq_enabled;
	gpio->status = status;
	mutex_unlock(&gpio->lock);

	for_each_set_bit(i, &change, gpio->chip.ngpio) {
		nirq = irq_find_mapping(gpio->chip.irq.domain, i);
		if (nirq) {
			dev_dbg(gpio->dev, "status:%04lx,change:%04lx,index:%ld,nirq:%d\n",
					status, change, i, nirq);
			handle_nested_irq(nirq);
		}
	}

	return IRQ_HANDLED;
}

/*
 * NOP functions
 */
static void aw9110_noop(struct irq_data *data) { }

static int aw9110_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct aw9110 *gpio = irq_data_get_irq_chip_data(data);

	return irq_set_irq_wake(gpio->client->irq, on);
}

static void aw9110_irq_enable(struct irq_data *data)
{
	struct aw9110 *gpio = irq_data_get_irq_chip_data(data);

	gpio->irq_enabled |= (1 << data->hwirq);
}

static void aw9110_irq_disable(struct irq_data *data)
{
	struct aw9110 *gpio = irq_data_get_irq_chip_data(data);

	gpio->irq_enabled &= ~(1 << data->hwirq);
}

static void aw9110_irq_bus_lock(struct irq_data *data)
{
	struct aw9110 *gpio = irq_data_get_irq_chip_data(data);

	mutex_lock(&gpio->lock);
}

static void aw9110_irq_bus_sync_unlock(struct irq_data *data)
{
	struct aw9110 *gpio = irq_data_get_irq_chip_data(data);

	mutex_unlock(&gpio->lock);
}

static void aw9110_state_init(struct aw9110	*gpio)
{
	/* out4-9 push-pull */
	gpio->write(gpio->client, REG_CTRL, (1<<4));

	/* work mode : gpio */
	gpio->write(gpio->client, REG_WORK_MODE_P1, 0x0F);
	gpio->write(gpio->client, REG_WORK_MODE_P0, 0x3F);

	/* default direct */
	gpio->direct = 0x03FF;	/* 0: output, 1:input */
	gpio->write(gpio->client, REG_CONFIG_P1, gpio->direct & 0x0F);
	gpio->write(gpio->client, REG_CONFIG_P0, (gpio->direct>>4) & 0x3F);

	/* interrupt enable */
	gpio->irq_enabled = 0x03FF;	/* 0: disable 1:enable, chip: 0:enable,  1: disable */
	gpio->write(gpio->client, REG_INT_P1, ((~gpio->irq_enabled) >> 0)&0x0F);
	gpio->write(gpio->client, REG_INT_P0, ((~gpio->irq_enabled) >> 4)&0x3F);

	/* clear interrupt */
	gpio->read(gpio->client, REG_INPUT_P1);
	gpio->read(gpio->client, REG_INPUT_P1);
}

static int aw9110_parse_dt(struct aw9110 *chip, struct i2c_client *client)
{
	struct device_node *np = client->dev.of_node;
	int ret = 0;

	/* shdn_en */
	ret = of_get_named_gpio(np, "shdn_en", 0);
	if (ret < 0) {
		dev_err(chip->dev, "of get shdn_en failed\n");
		chip->shdn_en = -1;
	} else {
		chip->shdn_en = ret;

		ret = devm_gpio_request_one(chip->dev, chip->shdn_en,
					    GPIOF_OUT_INIT_LOW, "AW9110_SHDN_EN");
		if (ret) {
			dev_err(chip->dev,
				"devm_gpio_request_one shdn_en failed\n");
			return ret;
		}

		/* enable chip */
		gpio_set_value(chip->shdn_en, 1);
	}

	return 0;
}

static int aw9110_check_dev_id(struct i2c_client *client)
{
	int ret;

	ret = aw9110_i2c_read_le8(client, REG_ID);

	if (ret < 0) {
		dev_err(&client->dev, "fail to read dev id(%d)\n", ret);
		return ret;
	}

	dev_info(&client->dev, "dev id : 0x%02x\n", ret);

	return 0;
}

/*-------------------------------------------------------------------------*/

static int aw9110_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct aw9110			*gpio;
	int				status;

	dev_info(&client->dev, "===aw9110 probe===\n");

	/* Allocate, initialize, and register this gpio_chip. */
	gpio = devm_kzalloc(&client->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->dev = &client->dev;

	aw9110_parse_dt(gpio, client);

	mutex_init(&gpio->lock);

	gpio->chip.base			= -1;
	gpio->chip.can_sleep		= true;
	gpio->chip.parent		= &client->dev;
	gpio->chip.owner		= THIS_MODULE;
	gpio->chip.get			= aw9110_get;
	gpio->chip.set			= aw9110_set;
	gpio->chip.get_direction	= aw9110_get_direction;
	gpio->chip.direction_input	= aw9110_direction_input;
	gpio->chip.direction_output	= aw9110_direction_output;
	gpio->chip.ngpio		= id->driver_data;

	gpio->write	= aw9110_i2c_write_le8;
	gpio->read	= aw9110_i2c_read_le8;

	gpio->chip.label = client->name;

	gpio->client = client;
	i2c_set_clientdata(client, gpio);

	status = aw9110_check_dev_id(client);
	if (status < 0) {
		dev_err(&client->dev, "check device id fail(%d)\n", status);
		goto fail;
	}

	aw9110_state_init(gpio);

	/* Enable irqchip if we have an interrupt */
	if (client->irq) {
		struct gpio_irq_chip *girq;

		gpio->irqchip.name = "aw9110";
		gpio->irqchip.irq_enable = aw9110_irq_enable;
		gpio->irqchip.irq_disable = aw9110_irq_disable;
		gpio->irqchip.irq_ack = aw9110_noop;
		gpio->irqchip.irq_mask = aw9110_noop;
		gpio->irqchip.irq_unmask = aw9110_noop;
		gpio->irqchip.irq_set_wake = aw9110_irq_set_wake;
		gpio->irqchip.irq_bus_lock = aw9110_irq_bus_lock;
		gpio->irqchip.irq_bus_sync_unlock = aw9110_irq_bus_sync_unlock;

		status = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, aw9110_irq, IRQF_ONESHOT |
					IRQF_TRIGGER_FALLING | IRQF_SHARED,
					dev_name(&client->dev), gpio);
		if (status)
			goto fail;

		girq = &gpio->chip.irq;
		girq->chip = &gpio->irqchip;
		/* This will let us handle the parent IRQ in the driver */
		girq->parent_handler = NULL;
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
		girq->threaded = true;
	}

	status = devm_gpiochip_add_data(&client->dev, &gpio->chip, gpio);
	if (status < 0)
		goto fail;

	dev_info(&client->dev, "probed\n");

	return 0;

fail:
	dev_err(&client->dev, "probe error %d for '%s'\n", status,
		client->name);

	return status;
}

static int aw9110_pm_resume(struct device *dev)
{
	struct aw9110 *gpio = dev->driver_data;

	/* out4-9 push-pull */
	gpio->write(gpio->client, REG_CTRL, (1<<4));

	/* work mode : gpio */
	gpio->write(gpio->client, REG_WORK_MODE_P1, 0x0F);
	gpio->write(gpio->client, REG_WORK_MODE_P0, 0x3F);

	/* direct */
	//gpio->direct = 0x03FF;	/* 0: output, 1:input */
	gpio->write(gpio->client, REG_CONFIG_P1, gpio->direct & 0x0F);
	gpio->write(gpio->client, REG_CONFIG_P0, (gpio->direct>>4) & 0x3F);

	/* out */
	gpio->write(gpio->client, REG_OUTPUT_P1, gpio->out >> 0);
	gpio->write(gpio->client, REG_OUTPUT_P0, gpio->out >> 4);

	/* interrupt enable */
	//gpio->irq_enabled = 0x03FF;	/* 0: disable 1:enable, chip: 0:enable,  1: disable */
	gpio->write(gpio->client, REG_INT_P1, ((~gpio->irq_enabled) >> 0)&0x0F);
	gpio->write(gpio->client, REG_INT_P0, ((~gpio->irq_enabled) >> 4)&0x3F);

	return 0;
}

static const struct dev_pm_ops aw9110_pm_ops = {
	.resume = aw9110_pm_resume,
};

static struct i2c_driver aw9110_driver = {
	.driver = {
		.name	= "aw9110",
		.pm = &aw9110_pm_ops,
		.of_match_table = of_match_ptr(aw9110_of_table),
	},
	.probe	= aw9110_probe,
	.id_table = aw9110_id,
};

static int __init aw9110_init(void)
{
	return i2c_add_driver(&aw9110_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(aw9110_init);

static void __exit aw9110_exit(void)
{
	i2c_del_driver(&aw9110_driver);
}
module_exit(aw9110_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jake Wu <jake.wu@rock-chips.com>");
MODULE_DESCRIPTION("AW9110 i2c expander gpio driver");

