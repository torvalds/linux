/*
 * GPIO driver for the ACCES 104-IDIO-16 family
 * Copyright (C) 2015 William Breathitt Gray
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This driver supports the following ACCES devices: 104-IDIO-16,
 * 104-IDIO-16E, 104-IDO-16, 104-IDIO-8, 104-IDIO-8E, and 104-IDO-8.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/irqdesc.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>

#define IDIO_16_EXTENT 8
#define MAX_NUM_IDIO_16 max_num_isa_dev(IDIO_16_EXTENT)

static unsigned int base[MAX_NUM_IDIO_16];
static unsigned int num_idio_16;
module_param_hw_array(base, uint, ioport, &num_idio_16, 0);
MODULE_PARM_DESC(base, "ACCES 104-IDIO-16 base addresses");

static unsigned int irq[MAX_NUM_IDIO_16];
module_param_hw_array(irq, uint, irq, NULL, 0);
MODULE_PARM_DESC(irq, "ACCES 104-IDIO-16 interrupt line numbers");

/**
 * struct idio_16_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @lock:	synchronization lock to prevent I/O race conditions
 * @irq_mask:	I/O bits affected by interrupts
 * @base:	base port address of the GPIO device
 * @out_state:	output bits state
 */
struct idio_16_gpio {
	struct gpio_chip chip;
	raw_spinlock_t lock;
	unsigned long irq_mask;
	unsigned base;
	unsigned out_state;
};

static int idio_16_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	if (offset > 15)
		return 1;

	return 0;
}

static int idio_16_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int idio_16_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	chip->set(chip, offset, value);
	return 0;
}

static int idio_16_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	const unsigned mask = BIT(offset-16);

	if (offset < 16)
		return -EINVAL;

	if (offset < 24)
		return !!(inb(idio16gpio->base + 1) & mask);

	return !!(inb(idio16gpio->base + 5) & (mask>>8));
}

static void idio_16_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	const unsigned mask = BIT(offset);
	unsigned long flags;

	if (offset > 15)
		return;

	raw_spin_lock_irqsave(&idio16gpio->lock, flags);

	if (value)
		idio16gpio->out_state |= mask;
	else
		idio16gpio->out_state &= ~mask;

	if (offset > 7)
		outb(idio16gpio->out_state >> 8, idio16gpio->base + 4);
	else
		outb(idio16gpio->out_state, idio16gpio->base);

	raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
}

static void idio_16_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	unsigned long flags;

	raw_spin_lock_irqsave(&idio16gpio->lock, flags);

	idio16gpio->out_state &= ~*mask;
	idio16gpio->out_state |= *mask & *bits;

	if (*mask & 0xFF)
		outb(idio16gpio->out_state, idio16gpio->base);
	if ((*mask >> 8) & 0xFF)
		outb(idio16gpio->out_state >> 8, idio16gpio->base + 4);

	raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
}

static void idio_16_irq_ack(struct irq_data *data)
{
}

static void idio_16_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	const unsigned long mask = BIT(irqd_to_hwirq(data));
	unsigned long flags;

	idio16gpio->irq_mask &= ~mask;

	if (!idio16gpio->irq_mask) {
		raw_spin_lock_irqsave(&idio16gpio->lock, flags);

		outb(0, idio16gpio->base + 2);

		raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
	}
}

static void idio_16_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	const unsigned long mask = BIT(irqd_to_hwirq(data));
	const unsigned long prev_irq_mask = idio16gpio->irq_mask;
	unsigned long flags;

	idio16gpio->irq_mask |= mask;

	if (!prev_irq_mask) {
		raw_spin_lock_irqsave(&idio16gpio->lock, flags);

		inb(idio16gpio->base + 2);

		raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
	}
}

static int idio_16_irq_set_type(struct irq_data *data, unsigned flow_type)
{
	/* The only valid irq types are none and both-edges */
	if (flow_type != IRQ_TYPE_NONE &&
		(flow_type & IRQ_TYPE_EDGE_BOTH) != IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	return 0;
}

static struct irq_chip idio_16_irqchip = {
	.name = "104-idio-16",
	.irq_ack = idio_16_irq_ack,
	.irq_mask = idio_16_irq_mask,
	.irq_unmask = idio_16_irq_unmask,
	.irq_set_type = idio_16_irq_set_type
};

static irqreturn_t idio_16_irq_handler(int irq, void *dev_id)
{
	struct idio_16_gpio *const idio16gpio = dev_id;
	struct gpio_chip *const chip = &idio16gpio->chip;
	int gpio;

	for_each_set_bit(gpio, &idio16gpio->irq_mask, chip->ngpio)
		generic_handle_irq(irq_find_mapping(chip->irq.domain, gpio));

	raw_spin_lock(&idio16gpio->lock);

	outb(0, idio16gpio->base + 1);

	raw_spin_unlock(&idio16gpio->lock);

	return IRQ_HANDLED;
}

#define IDIO_16_NGPIO 32
static const char *idio_16_names[IDIO_16_NGPIO] = {
	"OUT0", "OUT1", "OUT2", "OUT3", "OUT4", "OUT5", "OUT6", "OUT7",
	"OUT8", "OUT9", "OUT10", "OUT11", "OUT12", "OUT13", "OUT14", "OUT15",
	"IIN0", "IIN1", "IIN2", "IIN3", "IIN4", "IIN5", "IIN6", "IIN7",
	"IIN8", "IIN9", "IIN10", "IIN11", "IIN12", "IIN13", "IIN14", "IIN15"
};

static int idio_16_probe(struct device *dev, unsigned int id)
{
	struct idio_16_gpio *idio16gpio;
	const char *const name = dev_name(dev);
	int err;

	idio16gpio = devm_kzalloc(dev, sizeof(*idio16gpio), GFP_KERNEL);
	if (!idio16gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], IDIO_16_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + IDIO_16_EXTENT);
		return -EBUSY;
	}

	idio16gpio->chip.label = name;
	idio16gpio->chip.parent = dev;
	idio16gpio->chip.owner = THIS_MODULE;
	idio16gpio->chip.base = -1;
	idio16gpio->chip.ngpio = IDIO_16_NGPIO;
	idio16gpio->chip.names = idio_16_names;
	idio16gpio->chip.get_direction = idio_16_gpio_get_direction;
	idio16gpio->chip.direction_input = idio_16_gpio_direction_input;
	idio16gpio->chip.direction_output = idio_16_gpio_direction_output;
	idio16gpio->chip.get = idio_16_gpio_get;
	idio16gpio->chip.set = idio_16_gpio_set;
	idio16gpio->chip.set_multiple = idio_16_gpio_set_multiple;
	idio16gpio->base = base[id];
	idio16gpio->out_state = 0xFFFF;

	raw_spin_lock_init(&idio16gpio->lock);

	err = devm_gpiochip_add_data(dev, &idio16gpio->chip, idio16gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	/* Disable IRQ by default */
	outb(0, base[id] + 2);
	outb(0, base[id] + 1);

	err = gpiochip_irqchip_add(&idio16gpio->chip, &idio_16_irqchip, 0,
		handle_edge_irq, IRQ_TYPE_NONE);
	if (err) {
		dev_err(dev, "Could not add irqchip (%d)\n", err);
		return err;
	}

	err = devm_request_irq(dev, irq[id], idio_16_irq_handler, 0, name,
		idio16gpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static struct isa_driver idio_16_driver = {
	.probe = idio_16_probe,
	.driver = {
		.name = "104-idio-16"
	},
};

module_isa_driver(idio_16_driver, num_idio_16);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-IDIO-16 GPIO driver");
MODULE_LICENSE("GPL v2");
