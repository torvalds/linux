/*
 * GPIO driver for the Diamond Systems GPIO-MM
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
 *
 * This driver supports the following Diamond Systems devices: GPIO-MM and
 * GPIO-MM-12.
 */
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/isa.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>

#define GPIOMM_EXTENT 8
#define MAX_NUM_GPIOMM max_num_isa_dev(GPIOMM_EXTENT)

static unsigned int base[MAX_NUM_GPIOMM];
static unsigned int num_gpiomm;
module_param_array(base, uint, &num_gpiomm, 0);
MODULE_PARM_DESC(base, "Diamond Systems GPIO-MM base addresses");

/**
 * struct gpiomm_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @io_state:	bit I/O state (whether bit is set to input or output)
 * @out_state:	output bits state
 * @control:	Control registers state
 * @lock:	synchronization lock to prevent I/O race conditions
 * @base:	base port address of the GPIO device
 */
struct gpiomm_gpio {
	struct gpio_chip chip;
	unsigned char io_state[6];
	unsigned char out_state[6];
	unsigned char control[2];
	spinlock_t lock;
	unsigned int base;
};

static int gpiomm_gpio_get_direction(struct gpio_chip *chip,
	unsigned int offset)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);
	const unsigned int port = offset / 8;
	const unsigned int mask = BIT(offset % 8);

	return !!(gpiommgpio->io_state[port] & mask);
}

static int gpiomm_gpio_direction_input(struct gpio_chip *chip,
	unsigned int offset)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);
	const unsigned int io_port = offset / 8;
	const unsigned int control_port = io_port / 3;
	const unsigned int control_addr = gpiommgpio->base + 3 + control_port*4;
	unsigned long flags;
	unsigned int control;

	spin_lock_irqsave(&gpiommgpio->lock, flags);

	/* Check if configuring Port C */
	if (io_port == 2 || io_port == 5) {
		/* Port C can be configured by nibble */
		if (offset % 8 > 3) {
			gpiommgpio->io_state[io_port] |= 0xF0;
			gpiommgpio->control[control_port] |= BIT(3);
		} else {
			gpiommgpio->io_state[io_port] |= 0x0F;
			gpiommgpio->control[control_port] |= BIT(0);
		}
	} else {
		gpiommgpio->io_state[io_port] |= 0xFF;
		if (io_port == 0 || io_port == 3)
			gpiommgpio->control[control_port] |= BIT(4);
		else
			gpiommgpio->control[control_port] |= BIT(1);
	}

	control = BIT(7) | gpiommgpio->control[control_port];
	outb(control, control_addr);

	spin_unlock_irqrestore(&gpiommgpio->lock, flags);

	return 0;
}

static int gpiomm_gpio_direction_output(struct gpio_chip *chip,
	unsigned int offset, int value)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);
	const unsigned int io_port = offset / 8;
	const unsigned int control_port = io_port / 3;
	const unsigned int mask = BIT(offset % 8);
	const unsigned int control_addr = gpiommgpio->base + 3 + control_port*4;
	const unsigned int out_port = (io_port > 2) ? io_port + 1 : io_port;
	unsigned long flags;
	unsigned int control;

	spin_lock_irqsave(&gpiommgpio->lock, flags);

	/* Check if configuring Port C */
	if (io_port == 2 || io_port == 5) {
		/* Port C can be configured by nibble */
		if (offset % 8 > 3) {
			gpiommgpio->io_state[io_port] &= 0x0F;
			gpiommgpio->control[control_port] &= ~BIT(3);
		} else {
			gpiommgpio->io_state[io_port] &= 0xF0;
			gpiommgpio->control[control_port] &= ~BIT(0);
		}
	} else {
		gpiommgpio->io_state[io_port] &= 0x00;
		if (io_port == 0 || io_port == 3)
			gpiommgpio->control[control_port] &= ~BIT(4);
		else
			gpiommgpio->control[control_port] &= ~BIT(1);
	}

	if (value)
		gpiommgpio->out_state[io_port] |= mask;
	else
		gpiommgpio->out_state[io_port] &= ~mask;

	control = BIT(7) | gpiommgpio->control[control_port];
	outb(control, control_addr);

	outb(gpiommgpio->out_state[io_port], gpiommgpio->base + out_port);

	spin_unlock_irqrestore(&gpiommgpio->lock, flags);

	return 0;
}

static int gpiomm_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);
	const unsigned int port = offset / 8;
	const unsigned int mask = BIT(offset % 8);
	const unsigned int in_port = (port > 2) ? port + 1 : port;
	unsigned long flags;
	unsigned int port_state;

	spin_lock_irqsave(&gpiommgpio->lock, flags);

	/* ensure that GPIO is set for input */
	if (!(gpiommgpio->io_state[port] & mask)) {
		spin_unlock_irqrestore(&gpiommgpio->lock, flags);
		return -EINVAL;
	}

	port_state = inb(gpiommgpio->base + in_port);

	spin_unlock_irqrestore(&gpiommgpio->lock, flags);

	return !!(port_state & mask);
}

static void gpiomm_gpio_set(struct gpio_chip *chip, unsigned int offset,
	int value)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);
	const unsigned int port = offset / 8;
	const unsigned int mask = BIT(offset % 8);
	const unsigned int out_port = (port > 2) ? port + 1 : port;
	unsigned long flags;

	spin_lock_irqsave(&gpiommgpio->lock, flags);

	if (value)
		gpiommgpio->out_state[port] |= mask;
	else
		gpiommgpio->out_state[port] &= ~mask;

	outb(gpiommgpio->out_state[port], gpiommgpio->base + out_port);

	spin_unlock_irqrestore(&gpiommgpio->lock, flags);
}

static void gpiomm_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct gpiomm_gpio *const gpiommgpio = gpiochip_get_data(chip);
	unsigned int i;
	const unsigned int gpio_reg_size = 8;
	unsigned int port;
	unsigned int out_port;
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
		out_port = (port > 2) ? port + 1 : port;
		bitmask = mask[BIT_WORD(i)] & bits[BIT_WORD(i)];

		spin_lock_irqsave(&gpiommgpio->lock, flags);

		/* update output state data and set device gpio register */
		gpiommgpio->out_state[port] &= ~mask[BIT_WORD(i)];
		gpiommgpio->out_state[port] |= bitmask;
		outb(gpiommgpio->out_state[port], gpiommgpio->base + out_port);

		spin_unlock_irqrestore(&gpiommgpio->lock, flags);

		/* prepare for next gpio register set */
		mask[BIT_WORD(i)] >>= gpio_reg_size;
		bits[BIT_WORD(i)] >>= gpio_reg_size;
	}
}

#define GPIOMM_NGPIO 48
static const char *gpiomm_names[GPIOMM_NGPIO] = {
	"Port 1A0", "Port 1A1", "Port 1A2", "Port 1A3", "Port 1A4", "Port 1A5",
	"Port 1A6", "Port 1A7", "Port 1B0", "Port 1B1", "Port 1B2", "Port 1B3",
	"Port 1B4", "Port 1B5", "Port 1B6", "Port 1B7", "Port 1C0", "Port 1C1",
	"Port 1C2", "Port 1C3", "Port 1C4", "Port 1C5", "Port 1C6", "Port 1C7",
	"Port 2A0", "Port 2A1", "Port 2A2", "Port 2A3", "Port 2A4", "Port 2A5",
	"Port 2A6", "Port 2A7", "Port 2B0", "Port 2B1", "Port 2B2", "Port 2B3",
	"Port 2B4", "Port 2B5", "Port 2B6", "Port 2B7", "Port 2C0", "Port 2C1",
	"Port 2C2", "Port 2C3", "Port 2C4", "Port 2C5", "Port 2C6", "Port 2C7",
};

static int gpiomm_probe(struct device *dev, unsigned int id)
{
	struct gpiomm_gpio *gpiommgpio;
	const char *const name = dev_name(dev);
	int err;

	gpiommgpio = devm_kzalloc(dev, sizeof(*gpiommgpio), GFP_KERNEL);
	if (!gpiommgpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], GPIOMM_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + GPIOMM_EXTENT);
		return -EBUSY;
	}

	gpiommgpio->chip.label = name;
	gpiommgpio->chip.parent = dev;
	gpiommgpio->chip.owner = THIS_MODULE;
	gpiommgpio->chip.base = -1;
	gpiommgpio->chip.ngpio = GPIOMM_NGPIO;
	gpiommgpio->chip.names = gpiomm_names;
	gpiommgpio->chip.get_direction = gpiomm_gpio_get_direction;
	gpiommgpio->chip.direction_input = gpiomm_gpio_direction_input;
	gpiommgpio->chip.direction_output = gpiomm_gpio_direction_output;
	gpiommgpio->chip.get = gpiomm_gpio_get;
	gpiommgpio->chip.set = gpiomm_gpio_set;
	gpiommgpio->chip.set_multiple = gpiomm_gpio_set_multiple;
	gpiommgpio->base = base[id];

	spin_lock_init(&gpiommgpio->lock);

	err = devm_gpiochip_add_data(dev, &gpiommgpio->chip, gpiommgpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	/* initialize all GPIO as output */
	outb(0x80, base[id] + 3);
	outb(0x00, base[id]);
	outb(0x00, base[id] + 1);
	outb(0x00, base[id] + 2);
	outb(0x80, base[id] + 7);
	outb(0x00, base[id] + 4);
	outb(0x00, base[id] + 5);
	outb(0x00, base[id] + 6);

	return 0;
}

static struct isa_driver gpiomm_driver = {
	.probe = gpiomm_probe,
	.driver = {
		.name = "gpio-mm"
	},
};

module_isa_driver(gpiomm_driver, num_gpiomm);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Diamond Systems GPIO-MM GPIO driver");
MODULE_LICENSE("GPL v2");
