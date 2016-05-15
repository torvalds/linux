/*
 * GPIO driver for the WinSystems WS16C48
 * Copyright (C) 2016 William Breathitt Gray
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irqdesc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

static unsigned ws16c48_base;
module_param(ws16c48_base, uint, 0);
MODULE_PARM_DESC(ws16c48_base, "WinSystems WS16C48 base address");
static unsigned ws16c48_irq;
module_param(ws16c48_irq, uint, 0);
MODULE_PARM_DESC(ws16c48_irq, "WinSystems WS16C48 interrupt line number");

/**
 * struct ws16c48_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @io_state:	bit I/O state (whether bit is set to input or output)
 * @out_state:	output bits state
 * @lock:	synchronization lock to prevent I/O race conditions
 * @irq_mask:	I/O bits affected by interrupts
 * @flow_mask:	IRQ flow type mask for the respective I/O bits
 * @base:	base port address of the GPIO device
 * @irq:	Interrupt line number
 */
struct ws16c48_gpio {
	struct gpio_chip chip;
	unsigned char io_state[6];
	unsigned char out_state[6];
	spinlock_t lock;
	unsigned long irq_mask;
	unsigned long flow_mask;
	unsigned base;
	unsigned irq;
};

static int ws16c48_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);

	return !!(ws16c48gpio->io_state[port] & mask);
}

static int ws16c48_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->io_state[port] |= mask;
	ws16c48gpio->out_state[port] &= ~mask;
	outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return 0;
}

static int ws16c48_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->io_state[port] &= ~mask;
	if (value)
		ws16c48gpio->out_state[port] |= mask;
	else
		ws16c48gpio->out_state[port] &= ~mask;
	outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return 0;
}

static int ws16c48_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;
	unsigned port_state;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	/* ensure that GPIO is set for input */
	if (!(ws16c48gpio->io_state[port] & mask)) {
		spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
		return -EINVAL;
	}

	port_state = inb(ws16c48gpio->base + port);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return !!(port_state & mask);
}

static void ws16c48_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	/* ensure that GPIO is set for output */
	if (ws16c48gpio->io_state[port] & mask) {
		spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
		return;
	}

	if (value)
		ws16c48gpio->out_state[port] |= mask;
	else
		ws16c48gpio->out_state[port] &= ~mask;
	outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
}

static void ws16c48_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned long offset = irqd_to_hwirq(data);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;
	unsigned port_state;

	/* only the first 3 ports support interrupts */
	if (port > 2)
		return;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	port_state = ws16c48gpio->irq_mask >> (8*port);

	outb(0x80, ws16c48gpio->base + 7);
	outb(port_state & ~mask, ws16c48gpio->base + 8 + port);
	outb(port_state | mask, ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
}

static void ws16c48_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned long offset = irqd_to_hwirq(data);
	const unsigned long mask = BIT(offset);
	const unsigned port = offset / 8;
	unsigned long flags;

	/* only the first 3 ports support interrupts */
	if (port > 2)
		return;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->irq_mask &= ~mask;

	outb(0x80, ws16c48gpio->base + 7);
	outb(ws16c48gpio->irq_mask >> (8*port), ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
}

static void ws16c48_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned long offset = irqd_to_hwirq(data);
	const unsigned long mask = BIT(offset);
	const unsigned port = offset / 8;
	unsigned long flags;

	/* only the first 3 ports support interrupts */
	if (port > 2)
		return;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->irq_mask |= mask;

	outb(0x80, ws16c48gpio->base + 7);
	outb(ws16c48gpio->irq_mask >> (8*port), ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
}

static int ws16c48_irq_set_type(struct irq_data *data, unsigned flow_type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned long offset = irqd_to_hwirq(data);
	const unsigned long mask = BIT(offset);
	const unsigned port = offset / 8;
	unsigned long flags;

	/* only the first 3 ports support interrupts */
	if (port > 2)
		return -EINVAL;

	spin_lock_irqsave(&ws16c48gpio->lock, flags);

	switch (flow_type) {
	case IRQ_TYPE_NONE:
		break;
	case IRQ_TYPE_EDGE_RISING:
		ws16c48gpio->flow_mask |= mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		ws16c48gpio->flow_mask &= ~mask;
		break;
	default:
		spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
		return -EINVAL;
	}

	outb(0x40, ws16c48gpio->base + 7);
	outb(ws16c48gpio->flow_mask >> (8*port), ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return 0;
}

static struct irq_chip ws16c48_irqchip = {
	.name = "ws16c48",
	.irq_ack = ws16c48_irq_ack,
	.irq_mask = ws16c48_irq_mask,
	.irq_unmask = ws16c48_irq_unmask,
	.irq_set_type = ws16c48_irq_set_type
};

static irqreturn_t ws16c48_irq_handler(int irq, void *dev_id)
{
	struct ws16c48_gpio *const ws16c48gpio = dev_id;
	struct gpio_chip *const chip = &ws16c48gpio->chip;
	unsigned long int_pending;
	unsigned long port;
	unsigned long int_id;
	unsigned long gpio;

	int_pending = inb(ws16c48gpio->base + 6) & 0x7;
	if (!int_pending)
		return IRQ_NONE;

	/* loop until all pending interrupts are handled */
	do {
		for_each_set_bit(port, &int_pending, 3) {
			int_id = inb(ws16c48gpio->base + 8 + port);
			for_each_set_bit(gpio, &int_id, 8)
				generic_handle_irq(irq_find_mapping(
					chip->irqdomain, gpio + 8*port));
		}

		int_pending = inb(ws16c48gpio->base + 6) & 0x7;
	} while (int_pending);

	return IRQ_HANDLED;
}

static int __init ws16c48_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ws16c48_gpio *ws16c48gpio;
	const unsigned base = ws16c48_base;
	const unsigned extent = 16;
	const char *const name = dev_name(dev);
	int err;
	const unsigned irq = ws16c48_irq;

	ws16c48gpio = devm_kzalloc(dev, sizeof(*ws16c48gpio), GFP_KERNEL);
	if (!ws16c48gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base, extent, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base, base + extent);
		return -EBUSY;
	}

	ws16c48gpio->chip.label = name;
	ws16c48gpio->chip.parent = dev;
	ws16c48gpio->chip.owner = THIS_MODULE;
	ws16c48gpio->chip.base = -1;
	ws16c48gpio->chip.ngpio = 48;
	ws16c48gpio->chip.get_direction = ws16c48_gpio_get_direction;
	ws16c48gpio->chip.direction_input = ws16c48_gpio_direction_input;
	ws16c48gpio->chip.direction_output = ws16c48_gpio_direction_output;
	ws16c48gpio->chip.get = ws16c48_gpio_get;
	ws16c48gpio->chip.set = ws16c48_gpio_set;
	ws16c48gpio->base = base;
	ws16c48gpio->irq = irq;

	spin_lock_init(&ws16c48gpio->lock);

	dev_set_drvdata(dev, ws16c48gpio);

	err = gpiochip_add_data(&ws16c48gpio->chip, ws16c48gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	/* Disable IRQ by default */
	outb(0x80, base + 7);
	outb(0, base + 8);
	outb(0, base + 9);
	outb(0, base + 10);
	outb(0xC0, base + 7);

	err = gpiochip_irqchip_add(&ws16c48gpio->chip, &ws16c48_irqchip, 0,
		handle_edge_irq, IRQ_TYPE_NONE);
	if (err) {
		dev_err(dev, "Could not add irqchip (%d)\n", err);
		goto err_gpiochip_remove;
	}

	err = request_irq(irq, ws16c48_irq_handler, IRQF_SHARED, name,
		ws16c48gpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		goto err_gpiochip_remove;
	}

	return 0;

err_gpiochip_remove:
	gpiochip_remove(&ws16c48gpio->chip);
	return err;
}

static int ws16c48_remove(struct platform_device *pdev)
{
	struct ws16c48_gpio *const ws16c48gpio = platform_get_drvdata(pdev);

	free_irq(ws16c48gpio->irq, ws16c48gpio);
	gpiochip_remove(&ws16c48gpio->chip);

	return 0;
}

static struct platform_device *ws16c48_device;

static struct platform_driver ws16c48_driver = {
	.driver = {
		.name = "ws16c48"
	},
	.remove = ws16c48_remove
};

static void __exit ws16c48_exit(void)
{
	platform_device_unregister(ws16c48_device);
	platform_driver_unregister(&ws16c48_driver);
}

static int __init ws16c48_init(void)
{
	int err;

	ws16c48_device = platform_device_alloc(ws16c48_driver.driver.name, -1);
	if (!ws16c48_device)
		return -ENOMEM;

	err = platform_device_add(ws16c48_device);
	if (err)
		goto err_platform_device;

	err = platform_driver_probe(&ws16c48_driver, ws16c48_probe);
	if (err)
		goto err_platform_driver;

	return 0;

err_platform_driver:
	platform_device_del(ws16c48_device);
err_platform_device:
	platform_device_put(ws16c48_device);
	return err;
}

module_init(ws16c48_init);
module_exit(ws16c48_exit);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("WinSystems WS16C48 GPIO driver");
MODULE_LICENSE("GPL v2");
