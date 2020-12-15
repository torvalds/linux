// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the ACCES PCI-IDIO-16
 * Copyright (C) 2017 William Breathitt Gray
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
 * struct idio_16_gpio_reg - GPIO device registers structure
 * @out0_7:	Read: FET Drive Outputs 0-7
 *		Write: FET Drive Outputs 0-7
 * @in0_7:	Read: Isolated Inputs 0-7
 *		Write: Clear Interrupt
 * @irq_ctl:	Read: Enable IRQ
 *		Write: Disable IRQ
 * @filter_ctl:	Read: Activate Input Filters 0-15
 *		Write: Deactivate Input Filters 0-15
 * @out8_15:	Read: FET Drive Outputs 8-15
 *		Write: FET Drive Outputs 8-15
 * @in8_15:	Read: Isolated Inputs 8-15
 *		Write: Unused
 * @irq_status:	Read: Interrupt status
 *		Write: Unused
 */
struct idio_16_gpio_reg {
	u8 out0_7;
	u8 in0_7;
	u8 irq_ctl;
	u8 filter_ctl;
	u8 out8_15;
	u8 in8_15;
	u8 irq_status;
};

/**
 * struct idio_16_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @lock:	synchronization lock to prevent I/O race conditions
 * @reg:	I/O address offset for the GPIO device registers
 * @irq_mask:	I/O bits affected by interrupts
 */
struct idio_16_gpio {
	struct gpio_chip chip;
	raw_spinlock_t lock;
	struct idio_16_gpio_reg __iomem *reg;
	unsigned long irq_mask;
};

static int idio_16_gpio_get_direction(struct gpio_chip *chip,
	unsigned int offset)
{
	if (offset > 15)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static int idio_16_gpio_direction_input(struct gpio_chip *chip,
	unsigned int offset)
{
	return 0;
}

static int idio_16_gpio_direction_output(struct gpio_chip *chip,
	unsigned int offset, int value)
{
	chip->set(chip, offset, value);
	return 0;
}

static int idio_16_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	unsigned long mask = BIT(offset);

	if (offset < 8)
		return !!(ioread8(&idio16gpio->reg->out0_7) & mask);

	if (offset < 16)
		return !!(ioread8(&idio16gpio->reg->out8_15) & (mask >> 8));

	if (offset < 24)
		return !!(ioread8(&idio16gpio->reg->in0_7) & (mask >> 16));

	return !!(ioread8(&idio16gpio->reg->in8_15) & (mask >> 24));
}

static int idio_16_gpio_get_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	unsigned long offset;
	unsigned long gpio_mask;
	void __iomem *ports[] = {
		&idio16gpio->reg->out0_7, &idio16gpio->reg->out8_15,
		&idio16gpio->reg->in0_7, &idio16gpio->reg->in8_15,
	};
	void __iomem *port_addr;
	unsigned long port_state;

	/* clear bits array to a clean slate */
	bitmap_zero(bits, chip->ngpio);

	for_each_set_clump8(offset, gpio_mask, mask, ARRAY_SIZE(ports) * 8) {
		port_addr = ports[offset / 8];
		port_state = ioread8(port_addr) & gpio_mask;

		bitmap_set_value8(bits, port_state, offset);
	}

	return 0;
}

static void idio_16_gpio_set(struct gpio_chip *chip, unsigned int offset,
	int value)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	unsigned int mask = BIT(offset);
	void __iomem *base;
	unsigned long flags;
	unsigned int out_state;

	if (offset > 15)
		return;

	if (offset > 7) {
		mask >>= 8;
		base = &idio16gpio->reg->out8_15;
	} else
		base = &idio16gpio->reg->out0_7;

	raw_spin_lock_irqsave(&idio16gpio->lock, flags);

	if (value)
		out_state = ioread8(base) | mask;
	else
		out_state = ioread8(base) & ~mask;

	iowrite8(out_state, base);

	raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
}

static void idio_16_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(chip);
	unsigned long offset;
	unsigned long gpio_mask;
	void __iomem *ports[] = {
		&idio16gpio->reg->out0_7, &idio16gpio->reg->out8_15,
	};
	size_t index;
	void __iomem *port_addr;
	unsigned long bitmask;
	unsigned long flags;
	unsigned long out_state;

	for_each_set_clump8(offset, gpio_mask, mask, ARRAY_SIZE(ports) * 8) {
		index = offset / 8;
		port_addr = ports[index];

		bitmask = bitmap_get_value8(bits, offset) & gpio_mask;

		raw_spin_lock_irqsave(&idio16gpio->lock, flags);

		out_state = ioread8(port_addr) & ~gpio_mask;
		out_state |= bitmask;
		iowrite8(out_state, port_addr);

		raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
	}
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

		iowrite8(0, &idio16gpio->reg->irq_ctl);

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

		ioread8(&idio16gpio->reg->irq_ctl);

		raw_spin_unlock_irqrestore(&idio16gpio->lock, flags);
	}
}

static int idio_16_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	/* The only valid irq types are none and both-edges */
	if (flow_type != IRQ_TYPE_NONE &&
		(flow_type & IRQ_TYPE_EDGE_BOTH) != IRQ_TYPE_EDGE_BOTH)
		return -EINVAL;

	return 0;
}

static struct irq_chip idio_16_irqchip = {
	.name = "pci-idio-16",
	.irq_ack = idio_16_irq_ack,
	.irq_mask = idio_16_irq_mask,
	.irq_unmask = idio_16_irq_unmask,
	.irq_set_type = idio_16_irq_set_type
};

static irqreturn_t idio_16_irq_handler(int irq, void *dev_id)
{
	struct idio_16_gpio *const idio16gpio = dev_id;
	unsigned int irq_status;
	struct gpio_chip *const chip = &idio16gpio->chip;
	int gpio;

	raw_spin_lock(&idio16gpio->lock);

	irq_status = ioread8(&idio16gpio->reg->irq_status);

	raw_spin_unlock(&idio16gpio->lock);

	/* Make sure our device generated IRQ */
	if (!(irq_status & 0x3) || !(irq_status & 0x4))
		return IRQ_NONE;

	for_each_set_bit(gpio, &idio16gpio->irq_mask, chip->ngpio)
		generic_handle_irq(irq_find_mapping(chip->irq.domain, gpio));

	raw_spin_lock(&idio16gpio->lock);

	/* Clear interrupt */
	iowrite8(0, &idio16gpio->reg->in0_7);

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

static int idio_16_irq_init_hw(struct gpio_chip *gc)
{
	struct idio_16_gpio *const idio16gpio = gpiochip_get_data(gc);

	/* Disable IRQ by default and clear any pending interrupt */
	iowrite8(0, &idio16gpio->reg->irq_ctl);
	iowrite8(0, &idio16gpio->reg->in0_7);

	return 0;
}

static int idio_16_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *const dev = &pdev->dev;
	struct idio_16_gpio *idio16gpio;
	int err;
	const size_t pci_bar_index = 2;
	const char *const name = pci_name(pdev);
	struct gpio_irq_chip *girq;

	idio16gpio = devm_kzalloc(dev, sizeof(*idio16gpio), GFP_KERNEL);
	if (!idio16gpio)
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

	idio16gpio->reg = pcim_iomap_table(pdev)[pci_bar_index];

	/* Deactivate input filters */
	iowrite8(0, &idio16gpio->reg->filter_ctl);

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
	idio16gpio->chip.get_multiple = idio_16_gpio_get_multiple;
	idio16gpio->chip.set = idio_16_gpio_set;
	idio16gpio->chip.set_multiple = idio_16_gpio_set_multiple;

	girq = &idio16gpio->chip.irq;
	girq->chip = &idio_16_irqchip;
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;
	girq->init_hw = idio_16_irq_init_hw;

	raw_spin_lock_init(&idio16gpio->lock);

	err = devm_gpiochip_add_data(dev, &idio16gpio->chip, idio16gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
		return err;
	}

	err = devm_request_irq(dev, pdev->irq, idio_16_irq_handler, IRQF_SHARED,
		name, idio16gpio);
	if (err) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", err);
		return err;
	}

	return 0;
}

static const struct pci_device_id idio_16_pci_dev_id[] = {
	{ PCI_DEVICE(0x494F, 0x0DC8) }, { 0 }
};
MODULE_DEVICE_TABLE(pci, idio_16_pci_dev_id);

static struct pci_driver idio_16_driver = {
	.name = "pci-idio-16",
	.id_table = idio_16_pci_dev_id,
	.probe = idio_16_probe
};

module_pci_driver(idio_16_driver);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("ACCES PCI-IDIO-16 GPIO driver");
MODULE_LICENSE("GPL v2");
