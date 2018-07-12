// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the ACCES PCIe-IDIO-24 family
 * Copyright (C) 2018 William Breathitt Gray
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
 * This driver supports the following ACCES devices: PCIe-IDIO-24,
 * PCIe-IDI-24, PCIe-IDO-24, and PCIe-IDIO-12.
 */
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irqdesc.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/**
 * struct idio_24_gpio_reg - GPIO device registers structure
 * @out0_7:	Read: FET Outputs 0-7
 *		Write: FET Outputs 0-7
 * @out8_15:	Read: FET Outputs 8-15
 *		Write: FET Outputs 8-15
 * @out16_23:	Read: FET Outputs 16-23
 *		Write: FET Outputs 16-23
 * @ttl_out0_7:	Read: TTL/CMOS Outputs 0-7
 *		Write: TTL/CMOS Outputs 0-7
 * @in0_7:	Read: Isolated Inputs 0-7
 *		Write: Reserved
 * @in8_15:	Read: Isolated Inputs 8-15
 *		Write: Reserved
 * @in16_23:	Read: Isolated Inputs 16-23
 *		Write: Reserved
 * @ttl_in0_7:	Read: TTL/CMOS Inputs 0-7
 *		Write: Reserved
 * @cos0_7:	Read: COS Status Inputs 0-7
 *		Write: COS Clear Inputs 0-7
 * @cos8_15:	Read: COS Status Inputs 8-15
 *		Write: COS Clear Inputs 8-15
 * @cos16_23:	Read: COS Status Inputs 16-23
 *		Write: COS Clear Inputs 16-23
 * @cos_ttl0_7:	Read: COS Status TTL/CMOS 0-7
 *		Write: COS Clear TTL/CMOS 0-7
 * @ctl:	Read: Control Register
 *		Write: Control Register
 * @reserved:	Read: Reserved
 *		Write: Reserved
 * @cos_enable:	Read: COS Enable
 *		Write: COS Enable
 * @soft_reset:	Read: IRQ Output Pin Status
 *		Write: Software Board Reset
 */
struct idio_24_gpio_reg {
	u8 out0_7;
	u8 out8_15;
	u8 out16_23;
	u8 ttl_out0_7;
	u8 in0_7;
	u8 in8_15;
	u8 in16_23;
	u8 ttl_in0_7;
	u8 cos0_7;
	u8 cos8_15;
	u8 cos16_23;
	u8 cos_ttl0_7;
	u8 ctl;
	u8 reserved;
	u8 cos_enable;
	u8 soft_reset;
};

/**
 * struct idio_24_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @lock:	synchronization lock to prevent I/O race conditions
 * @reg:	I/O address offset for the GPIO device registers
 * @irq_mask:	I/O bits affected by interrupts
 */
struct idio_24_gpio {
	struct gpio_chip chip;
	raw_spinlock_t lock;
	struct idio_24_gpio_reg __iomem *reg;
	unsigned long irq_mask;
};

static int idio_24_gpio_get_direction(struct gpio_chip *chip,
	unsigned int offset)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	const unsigned long out_mode_mask = BIT(1);

	/* FET Outputs */
	if (offset < 24)
		return 0;

	/* Isolated Inputs */
	if (offset < 48)
		return 1;

	/* TTL/CMOS I/O */
	/* OUT MODE = 1 when TTL/CMOS Output Mode is set */
	return !(ioread8(&idio24gpio->reg->ctl) & out_mode_mask);
}

static int idio_24_gpio_direction_input(struct gpio_chip *chip,
	unsigned int offset)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	unsigned long flags;
	unsigned int ctl_state;
	const unsigned long out_mode_mask = BIT(1);

	/* TTL/CMOS I/O */
	if (offset > 47) {
		raw_spin_lock_irqsave(&idio24gpio->lock, flags);

		/* Clear TTL/CMOS Output Mode */
		ctl_state = ioread8(&idio24gpio->reg->ctl) & ~out_mode_mask;
		iowrite8(ctl_state, &idio24gpio->reg->ctl);

		raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
	}

	return 0;
}

static int idio_24_gpio_direction_output(struct gpio_chip *chip,
	unsigned int offset, int value)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	unsigned long flags;
	unsigned int ctl_state;
	const unsigned long out_mode_mask = BIT(1);

	/* TTL/CMOS I/O */
	if (offset > 47) {
		raw_spin_lock_irqsave(&idio24gpio->lock, flags);

		/* Set TTL/CMOS Output Mode */
		ctl_state = ioread8(&idio24gpio->reg->ctl) | out_mode_mask;
		iowrite8(ctl_state, &idio24gpio->reg->ctl);

		raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
	}

	chip->set(chip, offset, value);
	return 0;
}

static int idio_24_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	const unsigned long offset_mask = BIT(offset % 8);
	const unsigned long out_mode_mask = BIT(1);

	/* FET Outputs */
	if (offset < 8)
		return !!(ioread8(&idio24gpio->reg->out0_7) & offset_mask);

	if (offset < 16)
		return !!(ioread8(&idio24gpio->reg->out8_15) & offset_mask);

	if (offset < 24)
		return !!(ioread8(&idio24gpio->reg->out16_23) & offset_mask);

	/* Isolated Inputs */
	if (offset < 32)
		return !!(ioread8(&idio24gpio->reg->in0_7) & offset_mask);

	if (offset < 40)
		return !!(ioread8(&idio24gpio->reg->in8_15) & offset_mask);

	if (offset < 48)
		return !!(ioread8(&idio24gpio->reg->in16_23) & offset_mask);

	/* TTL/CMOS Outputs */
	if (ioread8(&idio24gpio->reg->ctl) & out_mode_mask)
		return !!(ioread8(&idio24gpio->reg->ttl_out0_7) & offset_mask);

	/* TTL/CMOS Inputs */
	return !!(ioread8(&idio24gpio->reg->ttl_in0_7) & offset_mask);
}

static int idio_24_gpio_get_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	size_t i;
	const unsigned int gpio_reg_size = 8;
	unsigned int bits_offset;
	size_t word_index;
	unsigned int word_offset;
	unsigned long word_mask;
	const unsigned long port_mask = GENMASK(gpio_reg_size - 1, 0);
	unsigned long port_state;
	void __iomem *ports[] = {
		&idio24gpio->reg->out0_7, &idio24gpio->reg->out8_15,
		&idio24gpio->reg->out16_23, &idio24gpio->reg->in0_7,
		&idio24gpio->reg->in8_15, &idio24gpio->reg->in16_23,
	};
	const unsigned long out_mode_mask = BIT(1);

	/* clear bits array to a clean slate */
	bitmap_zero(bits, chip->ngpio);

	/* get bits are evaluated a gpio port register at a time */
	for (i = 0; i < ARRAY_SIZE(ports) + 1; i++) {
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

		/* read bits from current gpio port (port 6 is TTL GPIO) */
		if (i < 6)
			port_state = ioread8(ports[i]);
		else if (ioread8(&idio24gpio->reg->ctl) & out_mode_mask)
			port_state = ioread8(&idio24gpio->reg->ttl_out0_7);
		else
			port_state = ioread8(&idio24gpio->reg->ttl_in0_7);

		/* store acquired bits at respective bits array offset */
		bits[word_index] |= port_state << word_offset;
	}

	return 0;
}

static void idio_24_gpio_set(struct gpio_chip *chip, unsigned int offset,
	int value)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	const unsigned long out_mode_mask = BIT(1);
	void __iomem *base;
	const unsigned int mask = BIT(offset % 8);
	unsigned long flags;
	unsigned int out_state;

	/* Isolated Inputs */
	if (offset > 23 && offset < 48)
		return;

	/* TTL/CMOS Inputs */
	if (offset > 47 && !(ioread8(&idio24gpio->reg->ctl) & out_mode_mask))
		return;

	/* TTL/CMOS Outputs */
	if (offset > 47)
		base = &idio24gpio->reg->ttl_out0_7;
	/* FET Outputs */
	else if (offset > 15)
		base = &idio24gpio->reg->out16_23;
	else if (offset > 7)
		base = &idio24gpio->reg->out8_15;
	else
		base = &idio24gpio->reg->out0_7;

	raw_spin_lock_irqsave(&idio24gpio->lock, flags);

	if (value)
		out_state = ioread8(base) | mask;
	else
		out_state = ioread8(base) & ~mask;

	iowrite8(out_state, base);

	raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
}

static void idio_24_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	size_t i;
	unsigned long bits_offset;
	unsigned long gpio_mask;
	const unsigned int gpio_reg_size = 8;
	const unsigned long port_mask = GENMASK(gpio_reg_size, 0);
	unsigned long flags;
	unsigned int out_state;
	void __iomem *ports[] = {
		&idio24gpio->reg->out0_7, &idio24gpio->reg->out8_15,
		&idio24gpio->reg->out16_23
	};
	const unsigned long out_mode_mask = BIT(1);
	const unsigned int ttl_offset = 48;
	const size_t ttl_i = BIT_WORD(ttl_offset);
	const unsigned int word_offset = ttl_offset % BITS_PER_LONG;
	const unsigned long ttl_mask = (mask[ttl_i] >> word_offset) & port_mask;
	const unsigned long ttl_bits = (bits[ttl_i] >> word_offset) & ttl_mask;

	/* set bits are processed a gpio port register at a time */
	for (i = 0; i < ARRAY_SIZE(ports); i++) {
		/* gpio offset in bits array */
		bits_offset = i * gpio_reg_size;

		/* check if any set bits for current port */
		gpio_mask = (*mask >> bits_offset) & port_mask;
		if (!gpio_mask) {
			/* no set bits for this port so move on to next port */
			continue;
		}

		raw_spin_lock_irqsave(&idio24gpio->lock, flags);

		/* process output lines */
		out_state = ioread8(ports[i]) & ~gpio_mask;
		out_state |= (*bits >> bits_offset) & gpio_mask;
		iowrite8(out_state, ports[i]);

		raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
	}

	/* check if setting TTL lines and if they are in output mode */
	if (!ttl_mask || !(ioread8(&idio24gpio->reg->ctl) & out_mode_mask))
		return;

	/* handle TTL output */
	raw_spin_lock_irqsave(&idio24gpio->lock, flags);

	/* process output lines */
	out_state = ioread8(&idio24gpio->reg->ttl_out0_7) & ~ttl_mask;
	out_state |= ttl_bits;
	iowrite8(out_state, &idio24gpio->reg->ttl_out0_7);

	raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
}

static void idio_24_irq_ack(struct irq_data *data)
{
}

static void idio_24_irq_mask(struct irq_data *data)
{
	struct gpio_chip *const chip = irq_data_get_irq_chip_data(data);
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	unsigned long flags;
	const unsigned long bit_offset = irqd_to_hwirq(data) - 24;
	unsigned char new_irq_mask;
	const unsigned long bank_offset = bit_offset/8 * 8;
	unsigned char cos_enable_state;

	raw_spin_lock_irqsave(&idio24gpio->lock, flags);

	idio24gpio->irq_mask &= BIT(bit_offset);
	new_irq_mask = idio24gpio->irq_mask >> bank_offset;

	if (!new_irq_mask) {
		cos_enable_state = ioread8(&idio24gpio->reg->cos_enable);

		/* Disable Rising Edge detection */
		cos_enable_state &= ~BIT(bank_offset);
		/* Disable Falling Edge detection */
		cos_enable_state &= ~BIT(bank_offset + 4);

		iowrite8(cos_enable_state, &idio24gpio->reg->cos_enable);
	}

	raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
}

static void idio_24_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *const chip = irq_data_get_irq_chip_data(data);
	struct idio_24_gpio *const idio24gpio = gpiochip_get_data(chip);
	unsigned long flags;
	unsigned char prev_irq_mask;
	const unsigned long bit_offset = irqd_to_hwirq(data) - 24;
	const unsigned long bank_offset = bit_offset/8 * 8;
	unsigned char cos_enable_state;

	raw_spin_lock_irqsave(&idio24gpio->lock, flags);

	prev_irq_mask = idio24gpio->irq_mask >> bank_offset;
	idio24gpio->irq_mask |= BIT(bit_offset);

	if (!prev_irq_mask) {
		cos_enable_state = ioread8(&idio24gpio->reg->cos_enable);

		/* Enable Rising Edge detection */
		cos_enable_state |= BIT(bank_offset);
		/* Enable Falling Edge detection */
		cos_enable_state |= BIT(bank_offset + 4);

		iowrite8(cos_enable_state, &idio24gpio->reg->cos_enable);
	}

	raw_spin_unlock_irqrestore(&idio24gpio->lock, flags);
}

static int idio_24_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	/* The only valid irq types are none and both-edges */
	if (flow_type != IRQ_TYPE_NONE &&
		(flow_type & IRQ_TYPE_EDGE_BOTH) != IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	return 0;
}

static struct irq_chip idio_24_irqchip = {
	.name = "pcie-idio-24",
	.irq_ack = idio_24_irq_ack,
	.irq_mask = idio_24_irq_mask,
	.irq_unmask = idio_24_irq_unmask,
	.irq_set_type = idio_24_irq_set_type
};

static irqreturn_t idio_24_irq_handler(int irq, void *dev_id)
{
	struct idio_24_gpio *const idio24gpio = dev_id;
	unsigned long irq_status;
	struct gpio_chip *const chip = &idio24gpio->chip;
	unsigned long irq_mask;
	int gpio;

	raw_spin_lock(&idio24gpio->lock);

	/* Read Change-Of-State status */
	irq_status = ioread32(&idio24gpio->reg->cos0_7);

	raw_spin_unlock(&idio24gpio->lock);

	/* Make sure our device generated IRQ */
	if (!irq_status)
		return IRQ_NONE;

	/* Handle only unmasked IRQ */
	irq_mask = idio24gpio->irq_mask & irq_status;

	for_each_set_bit(gpio, &irq_mask, chip->ngpio - 24)
		generic_handle_irq(irq_find_mapping(chip->irq.domain,
			gpio + 24));

	raw_spin_lock(&idio24gpio->lock);

	/* Clear Change-Of-State status */
	iowrite32(irq_status, &idio24gpio->reg->cos0_7);

	raw_spin_unlock(&idio24gpio->lock);

	return IRQ_HANDLED;
}

#define IDIO_24_NGPIO 56
static const char *idio_24_names[IDIO_24_NGPIO] = {
	"OUT0", "OUT1", "OUT2", "OUT3", "OUT4", "OUT5", "OUT6", "OUT7",
	"OUT8", "OUT9", "OUT10", "OUT11", "OUT12", "OUT13", "OUT14", "OUT15",
	"OUT16", "OUT17", "OUT18", "OUT19", "OUT20", "OUT21", "OUT22", "OUT23",
	"IIN0", "IIN1", "IIN2", "IIN3", "IIN4", "IIN5", "IIN6", "IIN7",
	"IIN8", "IIN9", "IIN10", "IIN11", "IIN12", "IIN13", "IIN14", "IIN15",
	"IIN16", "IIN17", "IIN18", "IIN19", "IIN20", "IIN21", "IIN22", "IIN23",
	"TTL0", "TTL1", "TTL2", "TTL3", "TTL4", "TTL5", "TTL6", "TTL7"
};

static int idio_24_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *const dev = &pdev->dev;
	struct idio_24_gpio *idio24gpio;
	int err;
	const size_t pci_bar_index = 2;
	const char *const name = pci_name(pdev);

	idio24gpio = devm_kzalloc(dev, sizeof(*idio24gpio), GFP_KERNEL);
	if (!idio24gpio)
		return -ENOMEM;

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device (%d)\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, BIT(pci_bar_index), name);
	if (err) {
		dev_err(dev, "Unable to map PCI I/O addresses (%d)\n", err);
		return err;
	}

	idio24gpio->reg = pcim_iomap_table(pdev)[pci_bar_index];

	idio24gpio->chip.label = name;
	idio24gpio->chip.parent = dev;
	idio24gpio->chip.owner = THIS_MODULE;
	idio24gpio->chip.base = -1;
	idio24gpio->chip.ngpio = IDIO_24_NGPIO;
	idio24gpio->chip.names = idio_24_names;
	idio24gpio->chip.get_direction = idio_24_gpio_get_direction;
	idio24gpio->chip.direction_input = idio_24_gpio_direction_input;
	idio24gpio->chip.direction_output = idio_24_gpio_direction_output;
	idio24gpio->chip.get = idio_24_gpio_get;
	idio24gpio->chip.get_multiple = idio_24_gpio_get_multiple;
	idio24gpio->chip.set = idio_24_gpio_set;
	idio24gpio->chip.set_multiple = idio_24_gpio_set_multiple;

	raw_spin_lock_init(&idio24gpio->lock);

	/* Software board reset */
	iowrite8(0, &idio24gpio->reg->soft_reset);

	err = devm_gpiochip_add_data(dev, &idio24gpio->chip, idio24gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	err = gpiochip_irqchip_add(&idio24gpio->chip, &idio_24_irqchip, 0,
		handle_edge_irq, IRQ_TYPE_NONE);
	if (err) {
		dev_err(dev, "Could not add irqchip (%d)\n", err);
		return err;
	}

	err = devm_request_irq(dev, pdev->irq, idio_24_irq_handler, IRQF_SHARED,
		name, idio24gpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static const struct pci_device_id idio_24_pci_dev_id[] = {
	{ PCI_DEVICE(0x494F, 0x0FD0) }, { PCI_DEVICE(0x494F, 0x0BD0) },
	{ PCI_DEVICE(0x494F, 0x07D0) }, { PCI_DEVICE(0x494F, 0x0FC0) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, idio_24_pci_dev_id);

static struct pci_driver idio_24_driver = {
	.name = "pcie-idio-24",
	.id_table = idio_24_pci_dev_id,
	.probe = idio_24_probe
};

module_pci_driver(idio_24_driver);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES PCIe-IDIO-24 GPIO driver");
MODULE_LICENSE("GPL v2");
