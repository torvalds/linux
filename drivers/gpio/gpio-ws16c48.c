// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO driver for the WinSystems WS16C48
 * Copyright (C) 2016 William Breathitt Gray
 */
#include <linux/bitmap.h>
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
#include <linux/types.h>

#define WS16C48_EXTENT 10
#define MAX_NUM_WS16C48 max_num_isa_dev(WS16C48_EXTENT)

static unsigned int base[MAX_NUM_WS16C48];
static unsigned int num_ws16c48;
module_param_hw_array(base, uint, ioport, &num_ws16c48, 0);
MODULE_PARM_DESC(base, "WinSystems WS16C48 base addresses");

static unsigned int irq[MAX_NUM_WS16C48];
static unsigned int num_irq;
module_param_hw_array(irq, uint, irq, &num_irq, 0);
MODULE_PARM_DESC(irq, "WinSystems WS16C48 interrupt line numbers");

/**
 * struct ws16c48_reg - device register structure
 * @port:		Port 0 through 5 I/O
 * @int_pending:	Interrupt Pending
 * @page_lock:		Register page (Bits 7-6) and I/O port lock (Bits 5-0)
 * @pol_enab_int_id:	Interrupt polarity, enable, and ID
 */
struct ws16c48_reg {
	u8 port[6];
	u8 int_pending;
	u8 page_lock;
	u8 pol_enab_int_id[3];
};

/**
 * struct ws16c48_gpio - GPIO device private data structure
 * @chip:	instance of the gpio_chip
 * @io_state:	bit I/O state (whether bit is set to input or output)
 * @out_state:	output bits state
 * @lock:	synchronization lock to prevent I/O race conditions
 * @irq_mask:	I/O bits affected by interrupts
 * @flow_mask:	IRQ flow type mask for the respective I/O bits
 * @reg:	I/O address offset for the device registers
 */
struct ws16c48_gpio {
	struct gpio_chip chip;
	unsigned char io_state[6];
	unsigned char out_state[6];
	raw_spinlock_t lock;
	unsigned long irq_mask;
	unsigned long flow_mask;
	struct ws16c48_reg __iomem *reg;
};

static int ws16c48_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	const unsigned port = offset / 8;
	const unsigned mask = BIT(offset % 8);

	if (ws16c48gpio->io_state[port] & mask)
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
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
	iowrite8(ws16c48gpio->out_state[port], ws16c48gpio->reg->port + port);

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
	iowrite8(ws16c48gpio->out_state[port], ws16c48gpio->reg->port + port);

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

	port_state = ioread8(ws16c48gpio->reg->port + port);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return !!(port_state & mask);
}

static int ws16c48_gpio_get_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	unsigned long offset;
	unsigned long gpio_mask;
	size_t index;
	u8 __iomem *port_addr;
	unsigned long port_state;

	/* clear bits array to a clean slate */
	bitmap_zero(bits, chip->ngpio);

	for_each_set_clump8(offset, gpio_mask, mask, chip->ngpio) {
		index = offset / 8;
		port_addr = ws16c48gpio->reg->port + index;
		port_state = ioread8(port_addr) & gpio_mask;

		bitmap_set_value8(bits, port_state, offset);
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
	iowrite8(ws16c48gpio->out_state[port], ws16c48gpio->reg->port + port);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
}

static void ws16c48_gpio_set_multiple(struct gpio_chip *chip,
	unsigned long *mask, unsigned long *bits)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(chip);
	unsigned long offset;
	unsigned long gpio_mask;
	size_t index;
	u8 __iomem *port_addr;
	unsigned long bitmask;
	unsigned long flags;

	for_each_set_clump8(offset, gpio_mask, mask, chip->ngpio) {
		index = offset / 8;
		port_addr = ws16c48gpio->reg->port + index;

		/* mask out GPIO configured for input */
		gpio_mask &= ~ws16c48gpio->io_state[index];
		bitmask = bitmap_get_value8(bits, offset) & gpio_mask;

		raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

		/* update output state data and set device gpio register */
		ws16c48gpio->out_state[index] &= ~gpio_mask;
		ws16c48gpio->out_state[index] |= bitmask;
		iowrite8(ws16c48gpio->out_state[index], port_addr);

		raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);
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

	/* Select Register Page 2; Unlock all I/O ports */
	iowrite8(0x80, &ws16c48gpio->reg->page_lock);

	/* Clear pending interrupt */
	iowrite8(port_state & ~mask, ws16c48gpio->reg->pol_enab_int_id + port);
	iowrite8(port_state | mask, ws16c48gpio->reg->pol_enab_int_id + port);

	/* Select Register Page 3; Unlock all I/O ports */
	iowrite8(0xC0, &ws16c48gpio->reg->page_lock);

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
	unsigned long port_state;

	/* only the first 3 ports support interrupts */
	if (port > 2)
		return;

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	ws16c48gpio->irq_mask &= ~mask;
	gpiochip_disable_irq(chip, offset);
	port_state = ws16c48gpio->irq_mask >> (8 * port);

	/* Select Register Page 2; Unlock all I/O ports */
	iowrite8(0x80, &ws16c48gpio->reg->page_lock);

	/* Disable interrupt */
	iowrite8(port_state, ws16c48gpio->reg->pol_enab_int_id + port);

	/* Select Register Page 3; Unlock all I/O ports */
	iowrite8(0xC0, &ws16c48gpio->reg->page_lock);

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
	unsigned long port_state;

	/* only the first 3 ports support interrupts */
	if (port > 2)
		return;

	raw_spin_lock_irqsave(&ws16c48gpio->lock, flags);

	gpiochip_enable_irq(chip, offset);
	ws16c48gpio->irq_mask |= mask;
	port_state = ws16c48gpio->irq_mask >> (8 * port);

	/* Select Register Page 2; Unlock all I/O ports */
	iowrite8(0x80, &ws16c48gpio->reg->page_lock);

	/* Enable interrupt */
	iowrite8(port_state, ws16c48gpio->reg->pol_enab_int_id + port);

	/* Select Register Page 3; Unlock all I/O ports */
	iowrite8(0xC0, &ws16c48gpio->reg->page_lock);

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
	unsigned long port_state;

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

	port_state = ws16c48gpio->flow_mask >> (8 * port);

	/* Select Register Page 1; Unlock all I/O ports */
	iowrite8(0x40, &ws16c48gpio->reg->page_lock);

	/* Set interrupt polarity */
	iowrite8(port_state, ws16c48gpio->reg->pol_enab_int_id + port);

	/* Select Register Page 3; Unlock all I/O ports */
	iowrite8(0xC0, &ws16c48gpio->reg->page_lock);

	raw_spin_unlock_irqrestore(&ws16c48gpio->lock, flags);

	return 0;
}

static const struct irq_chip ws16c48_irqchip = {
	.name = "ws16c48",
	.irq_ack = ws16c48_irq_ack,
	.irq_mask = ws16c48_irq_mask,
	.irq_unmask = ws16c48_irq_unmask,
	.irq_set_type = ws16c48_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t ws16c48_irq_handler(int irq, void *dev_id)
{
	struct ws16c48_gpio *const ws16c48gpio = dev_id;
	struct gpio_chip *const chip = &ws16c48gpio->chip;
	struct ws16c48_reg __iomem *const reg = ws16c48gpio->reg;
	unsigned long int_pending;
	unsigned long port;
	unsigned long int_id;
	unsigned long gpio;

	int_pending = ioread8(&reg->int_pending) & 0x7;
	if (!int_pending)
		return IRQ_NONE;

	/* loop until all pending interrupts are handled */
	do {
		for_each_set_bit(port, &int_pending, 3) {
			int_id = ioread8(reg->pol_enab_int_id + port);
			for_each_set_bit(gpio, &int_id, 8)
				generic_handle_domain_irq(chip->irq.domain,
							  gpio + 8*port);
		}

		int_pending = ioread8(&reg->int_pending) & 0x7;
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

static int ws16c48_irq_init_hw(struct gpio_chip *gc)
{
	struct ws16c48_gpio *const ws16c48gpio = gpiochip_get_data(gc);

	/* Select Register Page 2; Unlock all I/O ports */
	iowrite8(0x80, &ws16c48gpio->reg->page_lock);

	/* Disable interrupts for all lines */
	iowrite8(0, &ws16c48gpio->reg->pol_enab_int_id[0]);
	iowrite8(0, &ws16c48gpio->reg->pol_enab_int_id[1]);
	iowrite8(0, &ws16c48gpio->reg->pol_enab_int_id[2]);

	/* Select Register Page 3; Unlock all I/O ports */
	iowrite8(0xC0, &ws16c48gpio->reg->page_lock);

	return 0;
}

static int ws16c48_probe(struct device *dev, unsigned int id)
{
	struct ws16c48_gpio *ws16c48gpio;
	const char *const name = dev_name(dev);
	struct gpio_irq_chip *girq;
	int err;

	ws16c48gpio = devm_kzalloc(dev, sizeof(*ws16c48gpio), GFP_KERNEL);
	if (!ws16c48gpio)
		return -ENOMEM;

	if (!devm_request_region(dev, base[id], WS16C48_EXTENT, name)) {
		dev_err(dev, "Unable to lock port addresses (0x%X-0x%X)\n",
			base[id], base[id] + WS16C48_EXTENT);
		return -EBUSY;
	}

	ws16c48gpio->reg = devm_ioport_map(dev, base[id], WS16C48_EXTENT);
	if (!ws16c48gpio->reg)
		return -ENOMEM;

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

	girq = &ws16c48gpio->chip.irq;
	gpio_irq_chip_set_chip(girq, &ws16c48_irqchip);
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;
	girq->init_hw = ws16c48_irq_init_hw;

	raw_spin_lock_init(&ws16c48gpio->lock);

	err = devm_gpiochip_add_data(dev, &ws16c48gpio->chip, ws16c48gpio);
	if (err) {
		dev_err(dev, "GPIO registering failed (%d)\n", err);
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

module_isa_driver_with_irq(ws16c48_driver, num_ws16c48, num_irq);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("WinSystems WS16C48 GPIO driver");
MODULE_LICENSE("GPL v2");
