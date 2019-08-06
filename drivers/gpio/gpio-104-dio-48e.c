/*
 * GPIO driver for the ACCES 104-DIO-48E series
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
 * This driver supports the following ACCES devices: 104-DIO-48E and
 * 104-DIO-24E.
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

#define DIO48E_EXTENT 16
#define MAX_NUM_DIO48E max_num_isa_dev(DIO48E_EXTENT)

static unsigned int base[MAX_NUM_DIO48E];
static unsigned int num_dio48e;
module_param_hw_array(base, uint, ioport, &num_dio48e, 0);
MODULE_PARM_DESC(base, "ACCES 104-DIO-48E base addresses");

static unsigned int irq[MAX_NUM_DIO48E];
module_param_hw_array(irq, uint, irq, NULL, 0);
MODULE_PARM_DESC(irq, "ACCES 104-DIO-48E interrupt line numbers");

/**
 * struct dio48e_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @io_state:	bit I/O state (whether bit is set to input or output)
 * @out_state:	output bits state
 * @control:	Control registers state
 * @lock:	synchronization lock to prevent I/O race conditions
 * @base:	base port address of the GPIO device
 * @irq_mask:	I/O bits affected by interrupts
 */
struct dio48e_gpio {
	struct gpio_chip chip;
	unsigned char io_state[6];
	unsigned char out_state[6];
	unsigned char control[2];
	raw_spinlock_t lock;
	unsigned base;
	unsigned char irq_mask;
};

static int dio48e_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);

	return !!(dio48egpio->io_state[port] & mask);
}

static int dio48e_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned io_port = offset / 8;
	const unsigned int control_port = io_port / 3;
	const unsigned control_addr = dio48egpio->base + 3 + control_port*4;
	unsigned long flags;
	unsigned control;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);

	/* Check if configuring Port C */
	if (io_port == 2 || io_port == 5) {
		/* Port C can be configured by nibble */
		if (offset % 8 > 3) {
			dio48egpio->io_state[io_port] |= 0xF0;
			dio48egpio->control[control_port] |= BIT(3);
		} else {
			dio48egpio->io_state[io_port] |= 0x0F;
			dio48egpio->control[control_port] |= BIT(0);
		}
	} else {
		dio48egpio->io_state[io_port] |= 0xFF;
		if (io_port == 0 || io_port == 3)
			dio48egpio->control[control_port] |= BIT(4);
		else
			dio48egpio->control[control_port] |= BIT(1);
	}

	control = BIT(7) | dio48egpio->control[control_port];
	outb(control, control_addr);
	control &= ~BIT(7);
	outb(control, control_addr);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);

	return 0;
}

static int dio48e_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
	int value)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned io_port = offset / 8;
	const unsigned int control_port = io_port / 3;
	const unsigned mask = BIT(offset % 8);
	const unsigned control_addr = dio48egpio->base + 3 + control_port*4;
	const unsigned out_port = (io_port > 2) ? io_port + 1 : io_port;
	unsigned long flags;
	unsigned control;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);

	/* Check if configuring Port C */
	if (io_port == 2 || io_port == 5) {
		/* Port C can be configured by nibble */
		if (offset % 8 > 3) {
			dio48egpio->io_state[io_port] &= 0x0F;
			dio48egpio->control[control_port] &= ~BIT(3);
		} else {
			dio48egpio->io_state[io_port] &= 0xF0;
			dio48egpio->control[control_port] &= ~BIT(0);
		}
	} else {
		dio48egpio->io_state[io_port] &= 0x00;
		if (io_port == 0 || io_port == 3)
			dio48egpio->control[control_port] &= ~BIT(4);
		else
			dio48egpio->control[control_port] &= ~BIT(1);
	}

	if (value)
		dio48egpio->out_state[io_port] |= mask;
	else
		dio48egpio->out_state[io_port] &= ~mask;

	control = BIT(7) | dio48egpio->control[control_port];
	outb(control, control_addr);

	outb(dio48egpio->out_state[io_port], dio48egpio->base + out_port);

	control &= ~BIT(7);
	outb(control, control_addr);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);

	return 0;
}

static int dio48e_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	const unsigned in_port = (port > 2) ? port + 1 : port;
	unsigned long flags;
	unsigned port_state;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);

	/* ensure that GPIO is set for input */
	if (!(dio48egpio->io_state[port] & mask)) {
		raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);
		return -EINVAL;
	}

	port_state = inb(dio48egpio->base + in_port);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);

	return !!(port_state & mask);
}

static int dio48e_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
	unsigned long *bits)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	size_t i;
	static const size_t ports[] = { 0, 1, 2, 4, 5, 6 };
	const unsigned int gpio_reg_size = 8;
	unsigned int bits_offset;
	size_t word_index;
	unsigned int word_offset;
	unsigned long word_mask;
	const unsigned long port_mask = GENMASK(gpio_reg_size - 1, 0);
	unsigned long port_state;

	/* clear bits array to a clean slate */
	bitmap_zero(bits, chip->ngpio);

	/* get bits are evaluated a gpio port register at a time */
	for (i = 0; i < ARRAY_SIZE(ports); i++) {
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
		port_state = inb(dio48egpio->base + ports[i]);

		/* store acquired bits at respective bits array offset */
		bits[word_index] |= (port_state << word_offset) & word_mask;
	}

	return 0;
}

static void dio48e_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);
	const unsigned out_port = (port > 2) ? port + 1 : port;
	unsigned long flags;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);

	if (value)
		dio48egpio->out_state[port] |= mask;
	else
		dio48egpio->out_state[port] &= ~mask;

	outb(dio48egpio->out_state[port], dio48egpio->base + out_port);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);
}

static void dio48e_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
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

		raw_spin_lock_irqsave(&dio48egpio->lock, flags);

		/* update output state data and set device gpio register */
		dio48egpio->out_state[port] &= ~mask[BIT_WORD(i)];
		dio48egpio->out_state[port] |= bitmask;
		outb(dio48egpio->out_state[port], dio48egpio->base + out_port);

		raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);

		/* prepare for next gpio register set */
		mask[BIT_WORD(i)] >>= gpio_reg_size;
		bits[BIT_WORD(i)] >>= gpio_reg_size;
	}
}

static void dio48e_irq_ack(struct irq_data *data)
{
}

static void dio48e_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned long offset = irqd_to_hwirq(data);
	unsigned long flags;

	/* only bit 3 on each respective Port C supports interrupts */
	if (offset != 19 && offset != 43)
		return;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);

	if (offset == 19)
		dio48egpio->irq_mask &= ~BIT(0);
	else
		dio48egpio->irq_mask &= ~BIT(1);

	if (!dio48egpio->irq_mask)
		/* disable interrupts */
		inb(dio48egpio->base + 0xB);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);
}

static void dio48e_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct dio48e_gpio *const dio48egpio = gpiochip_get_data(chip);
	const unsigned long offset = irqd_to_hwirq(data);
	unsigned long flags;

	/* only bit 3 on each respective Port C supports interrupts */
	if (offset != 19 && offset != 43)
		return;

	raw_spin_lock_irqsave(&dio48egpio->lock, flags);

	if (!dio48egpio->irq_mask) {
		/* enable interrupts */
		outb(0x00, dio48egpio->base + 0xF);
		outb(0x00, dio48egpio->base + 0xB);
	}

	if (offset == 19)
		dio48egpio->irq_mask |= BIT(0);
	else
		dio48egpio->irq_mask |= BIT(1);

	raw_spin_unlock_irqrestore(&dio48egpio->lock, flags);
}

static int dio48e_irq_set_type(struct irq_data *data, unsigned flow_type)
{
	const unsigned long offset = irqd_to_hwirq(data);

	/* only bit 3 on each respective Port C supports interrupts */
	if (offset != 19 && offset != 43)
		return -EINVAL;

	if (flow_type != IRQ_TYPE_NONE && flow_type != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	return 0;
}

static struct irq_chip dio48e_irqchip = {
	.name = "104-dio-48e",
	.irq_ack = dio48e_irq_ack,
	.irq_mask = dio48e_irq_mask,
	.irq_unmask = dio48e_irq_unmask,
	.irq_set_type = dio48e_irq_set_type
};

static irqreturn_t dio48e_irq_handler(int irq, void *dev_id)
{
	struct dio48e_gpio *const dio48egpio = dev_id;
	struct gpio_chip *const chip = &dio48egpio->chip;
	const unsigned long irq_mask = dio48egpio->irq_mask;
	unsigned long gpio;

	for_each_set_bit(gpio, &irq_mask, 2)
		generic_handle_irq(irq_find_mapping(chip->irq.domain,
			19 + gpio*24));

	raw_spin_lock(&dio48egpio->lock);

	outb(0x00, dio48egpio->base + 0xF);

	raw_spin_unlock(&dio48egpio->lock);

	return IRQ_HANDLED;
}

#define DIO48E_NGPIO 48
static const char *dio48e_names[DIO48E_NGPIO] = {
	"PPI Group 0 Port A 0", "PPI Group 0 Port A 1", "PPI Group 0 Port A 2",
	"PPI Group 0 Port A 3", "PPI Group 0 Port A 4", "PPI Group 0 Port A 5",
	"PPI Group 0 Port A 6", "PPI Group 0 Port A 7",	"PPI Group 0 Port B 0",
	"PPI Group 0 Port B 1", "PPI Group 0 Port B 2", "PPI Group 0 Port B 3",
	"PPI Group 0 Port B 4", "PPI Group 0 Port B 5", "PPI Group 0 Port B 6",
	"PPI Group 0 Port B 7", "PPI Group 0 Port C 0", "PPI Group 0 Port C 1",
	"PPI Group 0 Port C 2", "PPI Group 0 Port C 3", "PPI Group 0 Port C 4",
	"PPI Group 0 Port C 5", "PPI Group 0 Port C 6", "PPI Group 0 Port C 7",
	"PPI Group 1 Port A 0", "PPI Group 1 Port A 1", "PPI Group 1 Port A 2",
	"PPI Group 1 Port A 3", "PPI Group 1 Port A 4", "PPI Group 1 Port A 5",
	"PPI Group 1 Port A 6", "PPI Group 1 Port A 7",	"PPI Group 1 Port B 0",
	"PPI Group 1 Port B 1", "PPI Group 1 Port B 2", "PPI Group 1 Port B 3",
	"PPI Group 1 Port B 4", "PPI Group 1 Port B 5", "PPI Group 1 Port B 6",
	"PPI Group 1 Port B 7", "PPI Group 1 Port C 0", "PPI Group 1 Port C 1",
	"PPI Group 1 Port C 2", "PPI Group 1 Port C 3", "PPI Group 1 Port C 4",
	"PPI Group 1 Port C 5", "PPI Group 1 Port C 6", "PPI Group 1 Port C 7"
};

static int dio48e_probe(struct device *dev, unsigned int id)
{
	struct dio48e_gpio *dio48egpio;
	const char *const name = dev_name(dev);
	int err;

	dio48egpio = devm_kzalloc(dev, sizeof(*dio48egpio), GFP_KERNEL);
	if (!dio48egpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], DIO48E_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + DIO48E_EXTENT);
		return -EBUSY;
	}

	dio48egpio->chip.label = name;
	dio48egpio->chip.parent = dev;
	dio48egpio->chip.owner = THIS_MODULE;
	dio48egpio->chip.base = -1;
	dio48egpio->chip.ngpio = DIO48E_NGPIO;
	dio48egpio->chip.names = dio48e_names;
	dio48egpio->chip.get_direction = dio48e_gpio_get_direction;
	dio48egpio->chip.direction_input = dio48e_gpio_direction_input;
	dio48egpio->chip.direction_output = dio48e_gpio_direction_output;
	dio48egpio->chip.get = dio48e_gpio_get;
	dio48egpio->chip.get_multiple = dio48e_gpio_get_multiple;
	dio48egpio->chip.set = dio48e_gpio_set;
	dio48egpio->chip.set_multiple = dio48e_gpio_set_multiple;
	dio48egpio->base = base[id];

	raw_spin_lock_init(&dio48egpio->lock);

	err = devm_gpiochip_add_data(dev, &dio48egpio->chip, dio48egpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	/* initialize all GPIO as output */
	outb(0x80, base[id] + 3);
	outb(0x00, base[id]);
	outb(0x00, base[id] + 1);
	outb(0x00, base[id] + 2);
	outb(0x00, base[id] + 3);
	outb(0x80, base[id] + 7);
	outb(0x00, base[id] + 4);
	outb(0x00, base[id] + 5);
	outb(0x00, base[id] + 6);
	outb(0x00, base[id] + 7);

	/* disable IRQ by default */
	inb(base[id] + 0xB);

	err = gpiochip_irqchip_add(&dio48egpio->chip, &dio48e_irqchip, 0,
		handle_edge_irq, IRQ_TYPE_NONE);
	if (err) {
		dev_err(dev, "Could not add irqchip (%d)\n", err);
		return err;
	}

	err = devm_request_irq(dev, irq[id], dio48e_irq_handler, 0, name,
		dio48egpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static struct isa_driver dio48e_driver = {
	.probe = dio48e_probe,
	.driver = {
		.name = "104-dio-48e"
	},
};
module_isa_driver(dio48e_driver, num_dio48e);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-DIO-48E GPIO driver");
MODULE_LICENSE("GPL v2");
