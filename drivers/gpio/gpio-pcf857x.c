// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for pcf857x, pca857x, and pca967x I2C GPIO expanders
 *
 * Copyright (C) 2007 David Brownell
 */

#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

static const struct i2c_device_id pcf857x_id[] = {
	{ "pcf8574", 8 },
	{ "pcf8574a", 8 },
	{ "pca8574", 8 },
	{ "pca9670", 8 },
	{ "pca9672", 8 },
	{ "pca9674", 8 },
	{ "pcf8575", 16 },
	{ "pca8575", 16 },
	{ "pca9671", 16 },
	{ "pca9673", 16 },
	{ "pca9675", 16 },
	{ "max7328", 8 },
	{ "max7329", 8 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf857x_id);

static const struct of_device_id pcf857x_of_table[] = {
	{ .compatible = "nxp,pcf8574", (void *)8 },
	{ .compatible = "nxp,pcf8574a", (void *)8 },
	{ .compatible = "nxp,pca8574", (void *)8 },
	{ .compatible = "nxp,pca9670", (void *)8 },
	{ .compatible = "nxp,pca9672", (void *)8 },
	{ .compatible = "nxp,pca9674", (void *)8 },
	{ .compatible = "nxp,pcf8575", (void *)16 },
	{ .compatible = "nxp,pca8575", (void *)16 },
	{ .compatible = "nxp,pca9671", (void *)16 },
	{ .compatible = "nxp,pca9673", (void *)16 },
	{ .compatible = "nxp,pca9675", (void *)16 },
	{ .compatible = "maxim,max7328", (void *)8 },
	{ .compatible = "maxim,max7329", (void *)8 },
	{ }
};
MODULE_DEVICE_TABLE(of, pcf857x_of_table);

/*
 * The pcf857x, pca857x, and pca967x chips only expose one read and one
 * write register.  Writing a "one" bit (to match the reset state) lets
 * that pin be used as an input; it's not an open-drain model, but acts
 * a bit like one.  This is described as "quasi-bidirectional"; read the
 * chip documentation for details.
 *
 * Many other I2C GPIO expander chips (like the pca953x models) have
 * more complex register models and more conventional circuitry using
 * push/pull drivers.  They often use the same 0x20..0x27 addresses as
 * pcf857x parts, making the "legacy" I2C driver model problematic.
 */
struct pcf857x {
	struct gpio_chip	chip;
	struct i2c_client	*client;
	struct mutex		lock;		/* protect 'out' */
	unsigned int		out;		/* software latch */
	unsigned int		status;		/* current status */
	unsigned int		irq_enabled;	/* enabled irqs */

	int (*write)(struct i2c_client *client, unsigned int data);
	int (*read)(struct i2c_client *client);
};

/*-------------------------------------------------------------------------*/

/* Talk to 8-bit I/O expander */

static int i2c_write_le8(struct i2c_client *client, unsigned int data)
{
	return i2c_smbus_write_byte(client, data);
}

static int i2c_read_le8(struct i2c_client *client)
{
	return i2c_smbus_read_byte(client);
}

/* Talk to 16-bit I/O expander */

static int i2c_write_le16(struct i2c_client *client, unsigned int word)
{
	u8 buf[2] = { word & 0xff, word >> 8, };
	int status;

	status = i2c_master_send(client, buf, 2);
	return (status < 0) ? status : 0;
}

static int i2c_read_le16(struct i2c_client *client)
{
	u8 buf[2];
	int status;

	status = i2c_master_recv(client, buf, 2);
	if (status < 0)
		return status;
	return (buf[1] << 8) | buf[0];
}

/*-------------------------------------------------------------------------*/

static int pcf857x_input(struct gpio_chip *chip, unsigned int offset)
{
	struct pcf857x *gpio = gpiochip_get_data(chip);
	int status;

	mutex_lock(&gpio->lock);
	gpio->out |= (1 << offset);
	status = gpio->write(gpio->client, gpio->out);
	mutex_unlock(&gpio->lock);

	return status;
}

static int pcf857x_get(struct gpio_chip *chip, unsigned int offset)
{
	struct pcf857x *gpio = gpiochip_get_data(chip);
	int value;

	value = gpio->read(gpio->client);
	return (value < 0) ? value : !!(value & (1 << offset));
}

static int pcf857x_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				unsigned long *bits)
{
	struct pcf857x *gpio = gpiochip_get_data(chip);
	int value = gpio->read(gpio->client);

	if (value < 0)
		return value;

	*bits &= ~*mask;
	*bits |= value & *mask;

	return 0;
}

static int pcf857x_output(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct pcf857x *gpio = gpiochip_get_data(chip);
	unsigned int bit = 1 << offset;
	int status;

	mutex_lock(&gpio->lock);
	if (value)
		gpio->out |= bit;
	else
		gpio->out &= ~bit;
	status = gpio->write(gpio->client, gpio->out);
	mutex_unlock(&gpio->lock);

	return status;
}

static void pcf857x_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	pcf857x_output(chip, offset, value);
}

static void pcf857x_set_multiple(struct gpio_chip *chip, unsigned long *mask,
				 unsigned long *bits)
{
	struct pcf857x *gpio = gpiochip_get_data(chip);

	mutex_lock(&gpio->lock);
	gpio->out &= ~*mask;
	gpio->out |= *bits & *mask;
	gpio->write(gpio->client, gpio->out);
	mutex_unlock(&gpio->lock);
}

/*-------------------------------------------------------------------------*/

static irqreturn_t pcf857x_irq(int irq, void *data)
{
	struct pcf857x *gpio = data;
	unsigned long change, i, status;

	status = gpio->read(gpio->client);

	/*
	 * call the interrupt handler iff gpio is used as
	 * interrupt source, just to avoid bad irqs
	 */
	mutex_lock(&gpio->lock);
	change = (gpio->status ^ status) & gpio->irq_enabled;
	gpio->status = status;
	mutex_unlock(&gpio->lock);

	for_each_set_bit(i, &change, gpio->chip.ngpio)
		handle_nested_irq(irq_find_mapping(gpio->chip.irq.domain, i));

	return IRQ_HANDLED;
}

/*
 * NOP functions
 */
static void noop(struct irq_data *data) { }

static int pcf857x_irq_set_wake(struct irq_data *data, unsigned int on)
{
	struct pcf857x *gpio = irq_data_get_irq_chip_data(data);

	return irq_set_irq_wake(gpio->client->irq, on);
}

static void pcf857x_irq_enable(struct irq_data *data)
{
	struct pcf857x *gpio = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	gpiochip_enable_irq(&gpio->chip, hwirq);
	gpio->irq_enabled |= (1 << hwirq);
}

static void pcf857x_irq_disable(struct irq_data *data)
{
	struct pcf857x *gpio = irq_data_get_irq_chip_data(data);
	irq_hw_number_t hwirq = irqd_to_hwirq(data);

	gpio->irq_enabled &= ~(1 << hwirq);
	gpiochip_disable_irq(&gpio->chip, hwirq);
}

static void pcf857x_irq_bus_lock(struct irq_data *data)
{
	struct pcf857x *gpio = irq_data_get_irq_chip_data(data);

	mutex_lock(&gpio->lock);
}

static void pcf857x_irq_bus_sync_unlock(struct irq_data *data)
{
	struct pcf857x *gpio = irq_data_get_irq_chip_data(data);

	mutex_unlock(&gpio->lock);
}

static const struct irq_chip pcf857x_irq_chip = {
	.name			= "pcf857x",
	.irq_enable		= pcf857x_irq_enable,
	.irq_disable		= pcf857x_irq_disable,
	.irq_ack		= noop,
	.irq_mask		= noop,
	.irq_unmask		= noop,
	.irq_set_wake		= pcf857x_irq_set_wake,
	.irq_bus_lock		= pcf857x_irq_bus_lock,
	.irq_bus_sync_unlock	= pcf857x_irq_bus_sync_unlock,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

/*-------------------------------------------------------------------------*/

static int pcf857x_probe(struct i2c_client *client)
{
	struct pcf857x *gpio;
	unsigned int n_latch = 0;
	int status;

	device_property_read_u32(&client->dev, "lines-initial-states", &n_latch);

	/* Allocate, initialize, and register this gpio_chip. */
	gpio = devm_kzalloc(&client->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	mutex_init(&gpio->lock);

	gpio->chip.base			= -1;
	gpio->chip.can_sleep		= true;
	gpio->chip.parent		= &client->dev;
	gpio->chip.owner		= THIS_MODULE;
	gpio->chip.get			= pcf857x_get;
	gpio->chip.get_multiple		= pcf857x_get_multiple;
	gpio->chip.set			= pcf857x_set;
	gpio->chip.set_multiple		= pcf857x_set_multiple;
	gpio->chip.direction_input	= pcf857x_input;
	gpio->chip.direction_output	= pcf857x_output;
	gpio->chip.ngpio		= (uintptr_t)i2c_get_match_data(client);

	/* NOTE:  the OnSemi jlc1562b is also largely compatible with
	 * these parts, notably for output.  It has a low-resolution
	 * DAC instead of pin change IRQs; and its inputs can be the
	 * result of comparators.
	 */

	/* 8574 addresses are 0x20..0x27; 8574a uses 0x38..0x3f;
	 * 9670, 9672, 9764, and 9764a use quite a variety.
	 *
	 * NOTE: we don't distinguish here between *4 and *4a parts.
	 */
	if (gpio->chip.ngpio == 8) {
		gpio->write	= i2c_write_le8;
		gpio->read	= i2c_read_le8;

		if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE))
			status = -EIO;

		/* fail if there's no chip present */
		else
			status = i2c_smbus_read_byte(client);

	/* '75/'75c addresses are 0x20..0x27, just like the '74;
	 * the '75c doesn't have a current source pulling high.
	 * 9671, 9673, and 9765 use quite a variety of addresses.
	 *
	 * NOTE: we don't distinguish here between '75 and '75c parts.
	 */
	} else if (gpio->chip.ngpio == 16) {
		gpio->write	= i2c_write_le16;
		gpio->read	= i2c_read_le16;

		if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
			status = -EIO;

		/* fail if there's no chip present */
		else
			status = i2c_read_le16(client);

	} else {
		dev_dbg(&client->dev, "unsupported number of gpios\n");
		status = -EINVAL;
	}

	if (status < 0)
		goto fail;

	gpio->chip.label = client->name;

	gpio->client = client;
	i2c_set_clientdata(client, gpio);

	/* NOTE:  these chips have strange "quasi-bidirectional" I/O pins.
	 * We can't actually know whether a pin is configured (a) as output
	 * and driving the signal low, or (b) as input and reporting a low
	 * value ... without knowing the last value written since the chip
	 * came out of reset (if any).  We can't read the latched output.
	 *
	 * In short, the only reliable solution for setting up pin direction
	 * is to do it explicitly.  The setup() method can do that, but it
	 * may cause transient glitching since it can't know the last value
	 * written (some pins may need to be driven low).
	 *
	 * Using n_latch avoids that trouble.  When left initialized to zero,
	 * our software copy of the "latch" then matches the chip's all-ones
	 * reset state.  Otherwise it flags pins to be driven low.
	 */
	gpio->out = ~n_latch;
	gpio->status = gpio->read(gpio->client);

	/* Enable irqchip if we have an interrupt */
	if (client->irq) {
		struct gpio_irq_chip *girq;

		status = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, pcf857x_irq, IRQF_ONESHOT |
					IRQF_TRIGGER_FALLING | IRQF_SHARED,
					dev_name(&client->dev), gpio);
		if (status)
			goto fail;

		girq = &gpio->chip.irq;
		gpio_irq_chip_set_chip(girq, &pcf857x_irq_chip);
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
	dev_dbg(&client->dev, "probe error %d for '%s'\n", status,
		client->name);

	return status;
}

static void pcf857x_shutdown(struct i2c_client *client)
{
	struct pcf857x *gpio = i2c_get_clientdata(client);

	/* Drive all the I/O lines high */
	gpio->write(gpio->client, BIT(gpio->chip.ngpio) - 1);
}

static struct i2c_driver pcf857x_driver = {
	.driver = {
		.name	= "pcf857x",
		.of_match_table = pcf857x_of_table,
	},
	.probe = pcf857x_probe,
	.shutdown = pcf857x_shutdown,
	.id_table = pcf857x_id,
};

static int __init pcf857x_init(void)
{
	return i2c_add_driver(&pcf857x_driver);
}
/* register after i2c postcore initcall and before
 * subsys initcalls that may rely on these GPIOs
 */
subsys_initcall(pcf857x_init);

static void __exit pcf857x_exit(void)
{
	i2c_del_driver(&pcf857x_driver);
}
module_exit(pcf857x_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Brownell");
