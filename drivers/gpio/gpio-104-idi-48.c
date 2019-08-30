// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the ACCES 104-IDI-48 family
 * Copyright (C) 2015 William Breathitt Gray
 *
 * This driver supports the following ACCES devices: 104-IDI-48A,
 * 104-IDI-48AC, 104-IDI-48B, and 104-IDI-48BC.
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

#define IDI_48_EXTENT 8
#define MAX_NUM_IDI_48 max_num_isa_dev(IDI_48_EXTENT)

static unsigned int base[MAX_NUM_IDI_48];
static unsigned int num_idi_48;
module_param_hw_array(base, uint, ioport, &num_idi_48, 0);
MODULE_PARM_DESC(base, "ACCES 104-IDI-48 base addresses");

static unsigned int irq[MAX_NUM_IDI_48];
module_param_hw_array(irq, uint, irq, NULL, 0);
MODULE_PARM_DESC(irq, "ACCES 104-IDI-48 interrupt line numbers");

/**
 * struct idi_48_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @lock:	synchronization lock to prevent I/O race conditions
 * @ack_lock:	synchronization lock to prevent IRQ handler race conditions
 * @irq_mask:	input bits affected by interrupts
 * @base:	base port address of the GPIO device
 * @cos_enb:	Change-Of-State IRQ enable boundaries mask
 */
struct idi_48_gpio {
	struct gpio_chip chip;
	raw_spinlock_t lock;
	spinlock_t ack_lock;
	unsigned char irq_mask[6];
	unsigned base;
	unsigned char cos_enb;
};

static int idi_48_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	return 1;
}

static int idi_48_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return 0;
}

static int idi_48_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct idi_48_gpio *const idi48gpio = gpiochip_get_data(chip);
	unsigned i;
	const unsigned register_offset[6] = { 0, 1, 2, 4, 5, 6 };
	unsigned base_offset;
	unsigned mask;

	for (i = 0; i < 48; i += 8)
		if (offset < i + 8) {
			base_offset = register_offset[i / 8];
			mask = BIT(offset - i);

			return !!(inb(idi48gpio->base + base_offset) & mask);
		}

	/* The following line should never execute since offset < 48 */
	return 0;
}

static int idi_48_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
	unsigned long *bits)
{
	struct idi_48_gpio *const idi48gpio = gpiochip_get_data(chip);
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
		port_state = inb(idi48gpio->base + ports[i]);

		/* store acquired bits at respective bits array offset */
		bits[word_index] |= (port_state << word_offset) & word_mask;
	}

	return 0;
}

static void idi_48_irq_ack(struct irq_data *data)
{
}

static void idi_48_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct idi_48_gpio *const idi48gpio = gpiochip_get_data(chip);
	const unsigned offset = irqd_to_hwirq(data);
	unsigned i;
	unsigned mask;
	unsigned boundary;
	unsigned long flags;

	for (i = 0; i < 48; i += 8)
		if (offset < i + 8) {
			mask = BIT(offset - i);
			boundary = i / 8;

			idi48gpio->irq_mask[boundary] &= ~mask;

			if (!idi48gpio->irq_mask[boundary]) {
				idi48gpio->cos_enb &= ~BIT(boundary);

				raw_spin_lock_irqsave(&idi48gpio->lock, flags);

				outb(idi48gpio->cos_enb, idi48gpio->base + 7);

				raw_spin_unlock_irqrestore(&idi48gpio->lock,
						           flags);
			}

			return;
		}
}

static void idi_48_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct idi_48_gpio *const idi48gpio = gpiochip_get_data(chip);
	const unsigned offset = irqd_to_hwirq(data);
	unsigned i;
	unsigned mask;
	unsigned boundary;
	unsigned prev_irq_mask;
	unsigned long flags;

	for (i = 0; i < 48; i += 8)
		if (offset < i + 8) {
			mask = BIT(offset - i);
			boundary = i / 8;
			prev_irq_mask = idi48gpio->irq_mask[boundary];

			idi48gpio->irq_mask[boundary] |= mask;

			if (!prev_irq_mask) {
				idi48gpio->cos_enb |= BIT(boundary);

				raw_spin_lock_irqsave(&idi48gpio->lock, flags);

				outb(idi48gpio->cos_enb, idi48gpio->base + 7);

				raw_spin_unlock_irqrestore(&idi48gpio->lock,
						           flags);
			}

			return;
		}
}

static int idi_48_irq_set_type(struct irq_data *data, unsigned flow_type)
{
	/* The only valid irq types are none and both-edges */
	if (flow_type != IRQ_TYPE_NONE &&
		(flow_type & IRQ_TYPE_EDGE_BOTH) != IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	return 0;
}

static struct irq_chip idi_48_irqchip = {
	.name = "104-idi-48",
	.irq_ack = idi_48_irq_ack,
	.irq_mask = idi_48_irq_mask,
	.irq_unmask = idi_48_irq_unmask,
	.irq_set_type = idi_48_irq_set_type
};

static irqreturn_t idi_48_irq_handler(int irq, void *dev_id)
{
	struct idi_48_gpio *const idi48gpio = dev_id;
	unsigned long cos_status;
	unsigned long boundary;
	unsigned long irq_mask;
	unsigned long bit_num;
	unsigned long gpio;
	struct gpio_chip *const chip = &idi48gpio->chip;

	spin_lock(&idi48gpio->ack_lock);

	raw_spin_lock(&idi48gpio->lock);

	cos_status = inb(idi48gpio->base + 7);

	raw_spin_unlock(&idi48gpio->lock);

	/* IRQ Status (bit 6) is active low (0 = IRQ generated by device) */
	if (cos_status & BIT(6)) {
		spin_unlock(&idi48gpio->ack_lock);
		return IRQ_NONE;
	}

	/* Bit 0-5 indicate which Change-Of-State boundary triggered the IRQ */
	cos_status &= 0x3F;

	for_each_set_bit(boundary, &cos_status, 6) {
		irq_mask = idi48gpio->irq_mask[boundary];

		for_each_set_bit(bit_num, &irq_mask, 8) {
			gpio = bit_num + boundary * 8;

			generic_handle_irq(irq_find_mapping(chip->irq.domain,
				gpio));
		}
	}

	spin_unlock(&idi48gpio->ack_lock);

	return IRQ_HANDLED;
}

#define IDI48_NGPIO 48
static const char *idi48_names[IDI48_NGPIO] = {
	"Bit 0 A", "Bit 1 A", "Bit 2 A", "Bit 3 A", "Bit 4 A", "Bit 5 A",
	"Bit 6 A", "Bit 7 A", "Bit 8 A", "Bit 9 A", "Bit 10 A", "Bit 11 A",
	"Bit 12 A", "Bit 13 A", "Bit 14 A", "Bit 15 A",	"Bit 16 A", "Bit 17 A",
	"Bit 18 A", "Bit 19 A", "Bit 20 A", "Bit 21 A", "Bit 22 A", "Bit 23 A",
	"Bit 0 B", "Bit 1 B", "Bit 2 B", "Bit 3 B", "Bit 4 B", "Bit 5 B",
	"Bit 6 B", "Bit 7 B", "Bit 8 B", "Bit 9 B", "Bit 10 B", "Bit 11 B",
	"Bit 12 B", "Bit 13 B", "Bit 14 B", "Bit 15 B",	"Bit 16 B", "Bit 17 B",
	"Bit 18 B", "Bit 19 B", "Bit 20 B", "Bit 21 B", "Bit 22 B", "Bit 23 B"
};

static int idi_48_probe(struct device *dev, unsigned int id)
{
	struct idi_48_gpio *idi48gpio;
	const char *const name = dev_name(dev);
	int err;

	idi48gpio = devm_kzalloc(dev, sizeof(*idi48gpio), GFP_KERNEL);
	if (!idi48gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], IDI_48_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + IDI_48_EXTENT);
		return -EBUSY;
	}

	idi48gpio->chip.label = name;
	idi48gpio->chip.parent = dev;
	idi48gpio->chip.owner = THIS_MODULE;
	idi48gpio->chip.base = -1;
	idi48gpio->chip.ngpio = IDI48_NGPIO;
	idi48gpio->chip.names = idi48_names;
	idi48gpio->chip.get_direction = idi_48_gpio_get_direction;
	idi48gpio->chip.direction_input = idi_48_gpio_direction_input;
	idi48gpio->chip.get = idi_48_gpio_get;
	idi48gpio->chip.get_multiple = idi_48_gpio_get_multiple;
	idi48gpio->base = base[id];

	raw_spin_lock_init(&idi48gpio->lock);
	spin_lock_init(&idi48gpio->ack_lock);

	err = devm_gpiochip_add_data(dev, &idi48gpio->chip, idi48gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	/* Disable IRQ by default */
	outb(0, base[id] + 7);
	inb(base[id] + 7);

	err = gpiochip_irqchip_add(&idi48gpio->chip, &idi_48_irqchip, 0,
		handle_edge_irq, IRQ_TYPE_NONE);
	if (err) {
		dev_err(dev, "Could not add irqchip (%d)\n", err);
		return err;
	}

	err = devm_request_irq(dev, irq[id], idi_48_irq_handler, IRQF_SHARED,
		name, idi48gpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static struct isa_driver idi_48_driver = {
	.probe = idi_48_probe,
	.driver = {
		.name = "104-idi-48"
	},
};
module_isa_driver(idi_48_driver, num_idi_48);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES 104-IDI-48 GPIO driver");
MODULE_LICENSE("GPL v2");
