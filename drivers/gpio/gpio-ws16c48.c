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
#include <linux/bitmap.h>
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

#define WS16C48_EXTENT 16
#define MAX_NUM_WS16C48 max_num_isa_dev(WS16C48_EXTENT)

static unsigned int base[MAX_NUM_WS16C48];
static unsigned int num_ws16c48;
module_param_hw_array(base, uint, ioport, &num_ws16c48, 0);
MODULE_PARM_DESC(base, "WinSystems WS16C48 base addresses");

static unsigned int irq[MAX_NUM_WS16C48];
module_param_hw_array(irq, uint, irq, NULL, 0);
MODULE_PARM_DESC(irq, "WinSystems WS16C48 interrupt line numbers");

/**
 * struct ws16c48_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @io_state:	bit I/O state (whether bit is set to input or output)
 * @out_state:	output bits state
 * @lock:	synchronization lock to prevent I/O race conditions
 * @irq_mask:	I/O bits affected by interrupts
 * @flow_mask:	IRQ flow type mask for the respective I/O bits
 * @base:	base port address of the GPIO device
 */
struct ws16c48_gpio {
	struct gpio_chip chip;
	unsigned char io_state[6];
	unsigned char out_state[6];
	raw_spinlock_t lock;
	unsigned long irq_mask;
	unsigned long flow_mask;
	unsigned base;
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

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->io_state[port] |= mask;
	ws16c48gpio->out_state[port] &= ~mask;
	outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return 0;
}

static int ws16c48_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->io_state[port] &= ~mask;
	if (value)
		ws16c48gpio->out_state[port] |= mask;
	else
		ws16c48gpio->out_state[port] &= ~mask;
	outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return 0;
}

static int ws16c48_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;
	unsigned port_state;

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	/* ensure that GPIO is set for input */
	if (!(ws16c48gpio->io_state[port] & mask)) {
		raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
		return -EINVAL;
	}

	port_state = inb(ws16c48gpio->base + port);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return !!(port_state & mask);
}

static int ws16c48_gpio_get_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned int gpio_reg_size = 8;
	size_t i;
	const size_t num_ports = chip->ngpio / gpio_reg_size;
	unsigned int bits_offset;
	size_t word_index;
	unsigned int word_offset;
	unsigned long word_mask;
	const unsigned long port_mask = GENMASK(gpio_reg_size - 1, 0);
	unsigned long port_state;

	/* clear bits array to a clean slate */
	bitmap_zero(bits, chip->ngpio);

	/* get bits are evaluated a gpio port register at a time */
	for (i = 0; i < num_ports; i++) {
		/* gpio offset in bits array */
		bits_offset = i * gpio_reg_size;

		/* word index for bits array */
		word_index = BIT_WORD(bits_offset);

		/* gpio offset within current word of bits array */
		word_offset = bits_offset % BITS_PER_LONG;

		/* mask of get bits for current gpio within current word */
		word_mask = mask[word_index] & (port_mask << word_offset);
		if (!word_mask) {
			/* no get bits in this port so skip to next one */
			continue;
		}

		/* read bits from current gpio port */
		port_state = inb(ws16c48gpio->base + i);

		/* store acquired bits at respective bits array offset */
		bits[word_index] |= port_state << word_offset;
	}

	return 0;
}

static void ws16c48_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	unsigned long flags;

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	/* ensure that GPIO is set for output */
	if (ws16c48gpio->io_state[port] & mask) {
		raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
		return;
	}

	if (value)
		ws16c48gpio->out_state[port] |= mask;
	else
		ws16c48gpio->out_state[port] &= ~mask;
	outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
}

static void ws16c48_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	unsigned int i;
	const unsigned int gpio_reg_size = 8;
	unsigned int port;
	unsigned int iomask;
	unsigned int bitmask;
	unsigned long flags;

	/* set bits are evaluated a gpio register size at a time */
	for (i = 0; i < chip->ngpio; i += gpio_reg_size) {
		/* no more set bits in this mask word; skip to the next word */
		if (!mask[BIT_WORD(i)]) {
			i = (BIT_WORD(i) + 1) * BITS_PER_LONG - gpio_reg_size;
			continue;
		}

		port = i / gpio_reg_size;

		/* mask out GPIO configured for input */
		iomask = mask[BIT_WORD(i)] & ~ws16c48gpio->io_state[port];
		bitmask = iomask & bits[BIT_WORD(i)];

		raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

		/* update output state data and set device gpio register */
		ws16c48gpio->out_state[port] &= ~iomask;
		ws16c48gpio->out_state[port] |= bitmask;
		outb(ws16c48gpio->out_state[port], ws16c48gpio->base + port);

		raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

		/* prepare for next gpio register set */
		mask[BIT_WORD(i)] >>= gpio_reg_size;
		bits[BIT_WORD(i)] >>= gpio_reg_size;
	}
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

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	port_state = ws16c48gpio->irq_mask >> (8*port);

	outb(0x80, ws16c48gpio->base + 7);
	outb(port_state & ~mask, ws16c48gpio->base + 8 + port);
	outb(port_state | mask, ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
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

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->irq_mask &= ~mask;

	outb(0x80, ws16c48gpio->base + 7);
	outb(ws16c48gpio->irq_mask >> (8*port), ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
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

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->irq_mask |= mask;

	outb(0x80, ws16c48gpio->base + 7);
	outb(ws16c48gpio->irq_mask >> (8*port), ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
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

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

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
		raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
		return -EINVAL;
	}

	outb(0x40, ws16c48gpio->base + 7);
	outb(ws16c48gpio->flow_mask >> (8*port), ws16c48gpio->base + 8 + port);
	outb(0xC0, ws16c48gpio->base + 7);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

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
					chip->irq.domain, gpio + 8*port));
		}

		int_pending = inb(ws16c48gpio->base + 6) & 0x7;
	} while (int_pending);

	return IRQ_HANDLED;
}

#define WS16C48_NGPIO 48
static const char *ws16c48_names[WS16C48_NGPIO] = {
	"Port 0 Bit 0", "Port 0 Bit 1", "Port 0 Bit 2", "Port 0 Bit 3",
	"Port 0 Bit 4", "Port 0 Bit 5", "Port 0 Bit 6", "Port 0 Bit 7",
	"Port 1 Bit 0", "Port 1 Bit 1", "Port 1 Bit 2", "Port 1 Bit 3",
	"Port 1 Bit 4", "Port 1 Bit 5", "Port 1 Bit 6", "Port 1 Bit 7",
	"Port 2 Bit 0", "Port 2 Bit 1", "Port 2 Bit 2", "Port 2 Bit 3",
	"Port 2 Bit 4", "Port 2 Bit 5", "Port 2 Bit 6", "Port 2 Bit 7",
	"Port 3 Bit 0", "Port 3 Bit 1", "Port 3 Bit 2", "Port 3 Bit 3",
	"Port 3 Bit 4", "Port 3 Bit 5", "Port 3 Bit 6", "Port 3 Bit 7",
	"Port 4 Bit 0", "Port 4 Bit 1", "Port 4 Bit 2", "Port 4 Bit 3",
	"Port 4 Bit 4", "Port 4 Bit 5", "Port 4 Bit 6", "Port 4 Bit 7",
	"Port 5 Bit 0", "Port 5 Bit 1", "Port 5 Bit 2", "Port 5 Bit 3",
	"Port 5 Bit 4", "Port 5 Bit 5", "Port 5 Bit 6", "Port 5 Bit 7"
};

static int ws16c48_probe(struct device *dev, unsigned int id)
{
	struct ws16c48_gpio *ws16c48gpio;
	const char *const name = dev_name(dev);
	int err;

	ws16c48gpio = devm_kzalloc(dev, sizeof(*ws16c48gpio), GFP_KERNEL);
	if (!ws16c48gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], WS16C48_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + WS16C48_EXTENT);
		return -EBUSY;
	}

	ws16c48gpio->chip.label = name;
	ws16c48gpio->chip.parent = dev;
	ws16c48gpio->chip.owner = THIS_MODULE;
	ws16c48gpio->chip.base = -1;
	ws16c48gpio->chip.ngpio = WS16C48_NGPIO;
	ws16c48gpio->chip.names = ws16c48_names;
	ws16c48gpio->chip.get_direction = ws16c48_gpio_get_direction;
	ws16c48gpio->chip.direction_input = ws16c48_gpio_direction_input;
	ws16c48gpio->chip.direction_output = ws16c48_gpio_direction_output;
	ws16c48gpio->chip.get = ws16c48_gpio_get;
	ws16c48gpio->chip.get_multiple = ws16c48_gpio_get_multiple;
	ws16c48gpio->chip.set = ws16c48_gpio_set;
	ws16c48gpio->chip.set_multiple = ws16c48_gpio_set_multiple;
	ws16c48gpio->base = base[id];

	raw_spin_lock_init(&ws16c48gpio->lock);

	err = devm_gpiochip_add_data(dev, &ws16c48gpio->chip, ws16c48gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	/* Disable IRQ by default */
	outb(0x80, base[id] + 7);
	outb(0, base[id] + 8);
	outb(0, base[id] + 9);
	outb(0, base[id] + 10);
	outb(0xC0, base[id] + 7);

	err = gpiochip_irqchip_add(&ws16c48gpio->chip, &ws16c48_irqchip, 0,
		handle_edge_irq, IRQ_TYPE_NONE);
	if (err) {
		dev_err(dev, "Could not add irqchip (%d)\n", err);
		return err;
	}

	err = devm_request_irq(dev, irq[id], ws16c48_irq_handler, IRQF_SHARED,
		name, ws16c48gpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static struct isa_driver ws16c48_driver = {
	.probe = ws16c48_probe,
	.driver = {
		.name = "ws16c48"
	},
};

module_isa_driver(ws16c48_driver, num_ws16c48);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("WinSystems WS16C48 GPIO driver");
MODULE_LICENSE("GPL v2");
