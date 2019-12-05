// SPDX-License-Identifier: GPL-2.0
/*
 * TQ-Systems TQMx86 PLD GPIO driver
 *
 * Based on vendor driver by:
 *   Vadim V.Vlasov <vvlasov@dev.rtsoft.ru>
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#define TQMX86_NGPIO	8
#define TQMX86_NGPO	4	/* 0-3 - output */
#define TQMX86_NGPI	4	/* 4-7 - input */
#define TQMX86_DIR_INPUT_MASK	0xf0	/* 0-3 - output, 4-7 - input */

#define TQMX86_GPIODD	0	/* GPIO Data Direction Register */
#define TQMX86_GPIOD	1	/* GPIO Data Register */
#define TQMX86_GPIIC	3	/* GPI Interrupt Configuration Register */
#define TQMX86_GPIIS	4	/* GPI Interrupt Status Register */

#define TQMX86_GPII_FALLING	BIT(0)
#define TQMX86_GPII_RISING	BIT(1)
#define TQMX86_GPII_MASK	(BIT(0) | BIT(1))
#define TQMX86_GPII_BITS	2

struct tqmx86_gpio_data {
	struct gpio_chip	chip;
	struct irq_chip		irq_chip;
	void __iomem		*io_base;
	int			irq;
	raw_spinlock_t		spinlock;
	u8			irq_type[TQMX86_NGPI];
};

static u8 tqmx86_gpio_read(struct tqmx86_gpio_data *gd, unsigned int reg)
{
	return ioread8(gd->io_base + reg);
}

static void tqmx86_gpio_write(struct tqmx86_gpio_data *gd, u8 val,
			      unsigned int reg)
{
	iowrite8(val, gd->io_base + reg);
}

static int tqmx86_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(chip);

	return !!(tqmx86_gpio_read(gpio, TQMX86_GPIOD) & BIT(offset));
}

static void tqmx86_gpio_set(struct gpio_chip *chip, unsigned int offset,
			    int value)
{
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(chip);
	unsigned long flags;
	u8 val;

	raw_spin_lock_irqsave(&gpio->spinlock, flags);
	val = tqmx86_gpio_read(gpio, TQMX86_GPIOD);
	if (value)
		val |= BIT(offset);
	else
		val &= ~BIT(offset);
	tqmx86_gpio_write(gpio, val, TQMX86_GPIOD);
	raw_spin_unlock_irqrestore(&gpio->spinlock, flags);
}

static int tqmx86_gpio_direction_input(struct gpio_chip *chip,
				       unsigned int offset)
{
	/* Direction cannot be changed. Validate is an input. */
	if (BIT(offset) & TQMX86_DIR_INPUT_MASK)
		return 0;
	else
		return -EINVAL;
}

static int tqmx86_gpio_direction_output(struct gpio_chip *chip,
					unsigned int offset,
					int value)
{
	/* Direction cannot be changed, validate is an output */
	if (BIT(offset) & TQMX86_DIR_INPUT_MASK)
		return -EINVAL;

	tqmx86_gpio_set(chip, offset, value);
	return 0;
}

static int tqmx86_gpio_get_direction(struct gpio_chip *chip,
				     unsigned int offset)
{
	if (TQMX86_DIR_INPUT_MASK & BIT(offset))
		return GPIO_LINE_DIRECTION_IN;

	return GPIO_LINE_DIRECTION_OUT;
}

static void tqmx86_gpio_irq_mask(struct irq_data *data)
{
	unsigned int offset = (data->hwirq - TQMX86_NGPO);
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(
		irq_data_get_irq_chip_data(data));
	unsigned long flags;
	u8 gpiic, mask;

	mask = TQMX86_GPII_MASK << (offset * TQMX86_GPII_BITS);

	raw_spin_lock_irqsave(&gpio->spinlock, flags);
	gpiic = tqmx86_gpio_read(gpio, TQMX86_GPIIC);
	gpiic &= ~mask;
	tqmx86_gpio_write(gpio, gpiic, TQMX86_GPIIC);
	raw_spin_unlock_irqrestore(&gpio->spinlock, flags);
}

static void tqmx86_gpio_irq_unmask(struct irq_data *data)
{
	unsigned int offset = (data->hwirq - TQMX86_NGPO);
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(
		irq_data_get_irq_chip_data(data));
	unsigned long flags;
	u8 gpiic, mask;

	mask = TQMX86_GPII_MASK << (offset * TQMX86_GPII_BITS);

	raw_spin_lock_irqsave(&gpio->spinlock, flags);
	gpiic = tqmx86_gpio_read(gpio, TQMX86_GPIIC);
	gpiic &= ~mask;
	gpiic |= gpio->irq_type[offset] << (offset * TQMX86_GPII_BITS);
	tqmx86_gpio_write(gpio, gpiic, TQMX86_GPIIC);
	raw_spin_unlock_irqrestore(&gpio->spinlock, flags);
}

static int tqmx86_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(
		irq_data_get_irq_chip_data(data));
	unsigned int offset = (data->hwirq - TQMX86_NGPO);
	unsigned int edge_type = type & IRQF_TRIGGER_MASK;
	unsigned long flags;
	u8 new_type, gpiic;

	switch (edge_type) {
	case IRQ_TYPE_EDGE_RISING:
		new_type = TQMX86_GPII_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		new_type = TQMX86_GPII_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		new_type = TQMX86_GPII_FALLING | TQMX86_GPII_RISING;
		break;
	default:
		return -EINVAL; /* not supported */
	}

	gpio->irq_type[offset] = new_type;

	raw_spin_lock_irqsave(&gpio->spinlock, flags);
	gpiic = tqmx86_gpio_read(gpio, TQMX86_GPIIC);
	gpiic &= ~((TQMX86_GPII_MASK) << (offset * TQMX86_GPII_BITS));
	gpiic |= new_type << (offset * TQMX86_GPII_BITS);
	tqmx86_gpio_write(gpio, gpiic, TQMX86_GPIIC);
	raw_spin_unlock_irqrestore(&gpio->spinlock, flags);

	return 0;
}

static void tqmx86_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct tqmx86_gpio_data *gpio = gpiochip_get_data(chip);
	struct irq_chip *irq_chip = irq_desc_get_chip(desc);
	unsigned long irq_bits;
	int i = 0, child_irq;
	u8 irq_status;

	chained_irq_enter(irq_chip, desc);

	irq_status = tqmx86_gpio_read(gpio, TQMX86_GPIIS);
	tqmx86_gpio_write(gpio, irq_status, TQMX86_GPIIS);

	irq_bits = irq_status;
	for_each_set_bit(i, &irq_bits, TQMX86_NGPI) {
		child_irq = irq_find_mapping(gpio->chip.irq.domain,
					     i + TQMX86_NGPO);
		generic_handle_irq(child_irq);
	}

	chained_irq_exit(irq_chip, desc);
}

/* Minimal runtime PM is needed by the IRQ subsystem */
static int __maybe_unused tqmx86_gpio_runtime_suspend(struct device *dev)
{
	return 0;
}

static int __maybe_unused tqmx86_gpio_runtime_resume(struct device *dev)
{
	return 0;
}

static const struct dev_pm_ops tqmx86_gpio_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(tqmx86_gpio_runtime_suspend,
			   tqmx86_gpio_runtime_resume, NULL)
};

static void tqmx86_init_irq_valid_mask(struct gpio_chip *chip,
				       unsigned long *valid_mask,
				       unsigned int ngpios)
{
	/* Only GPIOs 4-7 are valid for interrupts. Clear the others */
	clear_bit(0, valid_mask);
	clear_bit(1, valid_mask);
	clear_bit(2, valid_mask);
	clear_bit(3, valid_mask);
}

static int tqmx86_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tqmx86_gpio_data *gpio;
	struct gpio_chip *chip;
	struct gpio_irq_chip *girq;
	void __iomem *io_base;
	struct resource *res;
	int ret, irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res) {
		dev_err(&pdev->dev, "Cannot get I/O\n");
		return -ENODEV;
	}

	io_base = devm_ioport_map(&pdev->dev, res->start, resource_size(res));
	if (!io_base)
		return -ENOMEM;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	raw_spin_lock_init(&gpio->spinlock);
	gpio->io_base = io_base;

	tqmx86_gpio_write(gpio, (u8)~TQMX86_DIR_INPUT_MASK, TQMX86_GPIODD);

	platform_set_drvdata(pdev, gpio);

	chip = &gpio->chip;
	chip->label = "gpio-tqmx86";
	chip->owner = THIS_MODULE;
	chip->can_sleep = false;
	chip->base = -1;
	chip->direction_input = tqmx86_gpio_direction_input;
	chip->direction_output = tqmx86_gpio_direction_output;
	chip->get_direction = tqmx86_gpio_get_direction;
	chip->get = tqmx86_gpio_get;
	chip->set = tqmx86_gpio_set;
	chip->ngpio = TQMX86_NGPIO;
	chip->parent = pdev->dev.parent;

	pm_runtime_enable(&pdev->dev);

	if (irq) {
		struct irq_chip *irq_chip = &gpio->irq_chip;
		u8 irq_status;

		irq_chip->name = chip->label;
		irq_chip->parent_device = &pdev->dev;
		irq_chip->irq_mask = tqmx86_gpio_irq_mask;
		irq_chip->irq_unmask = tqmx86_gpio_irq_unmask;
		irq_chip->irq_set_type = tqmx86_gpio_irq_set_type;

		/* Mask all interrupts */
		tqmx86_gpio_write(gpio, 0, TQMX86_GPIIC);

		/* Clear all pending interrupts */
		irq_status = tqmx86_gpio_read(gpio, TQMX86_GPIIS);
		tqmx86_gpio_write(gpio, irq_status, TQMX86_GPIIS);

		girq = &chip->irq;
		girq->chip = irq_chip;
		girq->parent_handler = tqmx86_gpio_irq_handler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(&pdev->dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents) {
			ret = -ENOMEM;
			goto out_pm_dis;
		}
		girq->parents[0] = irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_simple_irq;
		girq->init_valid_mask = tqmx86_init_irq_valid_mask;
	}

	ret = devm_gpiochip_add_data(dev, chip, gpio);
	if (ret) {
		dev_err(dev, "Could not register GPIO chip\n");
		goto out_pm_dis;
	}

	dev_info(dev, "GPIO functionality initialized with %d pins\n",
		 chip->ngpio);

	return 0;

out_pm_dis:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static struct platform_driver tqmx86_gpio_driver = {
	.driver = {
		.name = "tqmx86-gpio",
		.pm = &tqmx86_gpio_dev_pm_ops,
	},
	.probe		= tqmx86_gpio_probe,
};

module_platform_driver(tqmx86_gpio_driver);

MODULE_DESCRIPTION("TQMx86 PLD GPIO Driver");
MODULE_AUTHOR("Andrew Lunn <andrew@lunn.ch>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tqmx86-gpio");
