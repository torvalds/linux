// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Merrifield SoC GPIO driver
 *
 * Copyright (c) 2016 Intel Corporation.
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pinctrl/consumer.h>

#define GCCR		0x000	/* controller configuration */
#define GPLR		0x004	/* pin level r/o */
#define GPDR		0x01c	/* pin direction */
#define GPSR		0x034	/* pin set w/o */
#define GPCR		0x04c	/* pin clear w/o */
#define GRER		0x064	/* rising edge detect */
#define GFER		0x07c	/* falling edge detect */
#define GFBR		0x094	/* glitch filter bypass */
#define GIMR		0x0ac	/* interrupt mask */
#define GISR		0x0c4	/* interrupt source */
#define GITR		0x300	/* input type */
#define GLPR		0x318	/* level input polarity */
#define GWMR		0x400	/* wake mask */
#define GWSR		0x418	/* wake source */
#define GSIR		0xc00	/* secure input */

/* Intel Merrifield has 192 GPIO pins */
#define MRFLD_NGPIO	192

struct mrfld_gpio_pinrange {
	unsigned int gpio_base;
	unsigned int pin_base;
	unsigned int npins;
};

#define GPIO_PINRANGE(gstart, gend, pstart)		\
	{						\
		.gpio_base = (gstart),			\
		.pin_base = (pstart),			\
		.npins = (gend) - (gstart) + 1,		\
	}

struct mrfld_gpio {
	struct gpio_chip	chip;
	void __iomem		*reg_base;
	raw_spinlock_t		lock;
	struct device		*dev;
};

static const struct mrfld_gpio_pinrange mrfld_gpio_ranges[] = {
	GPIO_PINRANGE(0, 11, 146),
	GPIO_PINRANGE(12, 13, 144),
	GPIO_PINRANGE(14, 15, 35),
	GPIO_PINRANGE(16, 16, 164),
	GPIO_PINRANGE(17, 18, 105),
	GPIO_PINRANGE(19, 22, 101),
	GPIO_PINRANGE(23, 30, 107),
	GPIO_PINRANGE(32, 43, 67),
	GPIO_PINRANGE(44, 63, 195),
	GPIO_PINRANGE(64, 67, 140),
	GPIO_PINRANGE(68, 69, 165),
	GPIO_PINRANGE(70, 71, 65),
	GPIO_PINRANGE(72, 76, 228),
	GPIO_PINRANGE(77, 86, 37),
	GPIO_PINRANGE(87, 87, 48),
	GPIO_PINRANGE(88, 88, 47),
	GPIO_PINRANGE(89, 96, 49),
	GPIO_PINRANGE(97, 97, 34),
	GPIO_PINRANGE(102, 119, 83),
	GPIO_PINRANGE(120, 123, 79),
	GPIO_PINRANGE(124, 135, 115),
	GPIO_PINRANGE(137, 142, 158),
	GPIO_PINRANGE(154, 163, 24),
	GPIO_PINRANGE(164, 176, 215),
	GPIO_PINRANGE(177, 189, 127),
	GPIO_PINRANGE(190, 191, 178),
};

static void __iomem *gpio_reg(struct gpio_chip *chip, unsigned int offset,
			      unsigned int reg_type_offset)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	u8 reg = offset / 32;

	return priv->reg_base + reg_type_offset + reg * 4;
}

static int mrfld_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	void __iomem *gplr = gpio_reg(chip, offset, GPLR);

	return !!(readl(gplr) & BIT(offset % 32));
}

static void mrfld_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gpsr, *gpcr;
	unsigned long flags;

	raw_spin_lock_irqsave(&priv->lock, flags);

	if (value) {
		gpsr = gpio_reg(chip, offset, GPSR);
		writel(BIT(offset % 32), gpsr);
	} else {
		gpcr = gpio_reg(chip, offset, GPCR);
		writel(BIT(offset % 32), gpcr);
	}

	raw_spin_unlock_irqrestore(&priv->lock, flags);
}

static int mrfld_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&priv->lock, flags);

	value = readl(gpdr);
	value &= ~BIT(offset % 32);
	writel(value, gpdr);

	raw_spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int mrfld_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	unsigned long flags;

	mrfld_gpio_set(chip, offset, value);

	raw_spin_lock_irqsave(&priv->lock, flags);

	value = readl(gpdr);
	value |= BIT(offset % 32);
	writel(value, gpdr);

	raw_spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int mrfld_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);

	if (readl(gpdr) & BIT(offset % 32))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int mrfld_gpio_set_debounce(struct gpio_chip *chip, unsigned int offset,
				   unsigned int debounce)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	void __iomem *gfbr = gpio_reg(chip, offset, GFBR);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&priv->lock, flags);

	if (debounce)
		value = readl(gfbr) & ~BIT(offset % 32);
	else
		value = readl(gfbr) | BIT(offset % 32);
	writel(value, gfbr);

	raw_spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int mrfld_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				 unsigned long config)
{
	u32 debounce;

	if ((pinconf_to_config_param(config) == PIN_CONFIG_BIAS_DISABLE) ||
	    (pinconf_to_config_param(config) == PIN_CONFIG_BIAS_PULL_UP) ||
	    (pinconf_to_config_param(config) == PIN_CONFIG_BIAS_PULL_DOWN))
		return gpiochip_generic_config(chip, offset, config);

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	return mrfld_gpio_set_debounce(chip, offset, debounce);
}

static void mrfld_irq_ack(struct irq_data *d)
{
	struct mrfld_gpio *priv = irq_data_get_irq_chip_data(d);
	u32 gpio = irqd_to_hwirq(d);
	void __iomem *gisr = gpio_reg(&priv->chip, gpio, GISR);
	unsigned long flags;

	raw_spin_lock_irqsave(&priv->lock, flags);

	writel(BIT(gpio % 32), gisr);

	raw_spin_unlock_irqrestore(&priv->lock, flags);
}

static void mrfld_irq_unmask_mask(struct irq_data *d, bool unmask)
{
	struct mrfld_gpio *priv = irq_data_get_irq_chip_data(d);
	u32 gpio = irqd_to_hwirq(d);
	void __iomem *gimr = gpio_reg(&priv->chip, gpio, GIMR);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&priv->lock, flags);

	if (unmask)
		value = readl(gimr) | BIT(gpio % 32);
	else
		value = readl(gimr) & ~BIT(gpio % 32);
	writel(value, gimr);

	raw_spin_unlock_irqrestore(&priv->lock, flags);
}

static void mrfld_irq_mask(struct irq_data *d)
{
	mrfld_irq_unmask_mask(d, false);
}

static void mrfld_irq_unmask(struct irq_data *d)
{
	mrfld_irq_unmask_mask(d, true);
}

static int mrfld_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mrfld_gpio *priv = gpiochip_get_data(gc);
	u32 gpio = irqd_to_hwirq(d);
	void __iomem *grer = gpio_reg(&priv->chip, gpio, GRER);
	void __iomem *gfer = gpio_reg(&priv->chip, gpio, GFER);
	void __iomem *gitr = gpio_reg(&priv->chip, gpio, GITR);
	void __iomem *glpr = gpio_reg(&priv->chip, gpio, GLPR);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&priv->lock, flags);

	if (type & IRQ_TYPE_EDGE_RISING)
		value = readl(grer) | BIT(gpio % 32);
	else
		value = readl(grer) & ~BIT(gpio % 32);
	writel(value, grer);

	if (type & IRQ_TYPE_EDGE_FALLING)
		value = readl(gfer) | BIT(gpio % 32);
	else
		value = readl(gfer) & ~BIT(gpio % 32);
	writel(value, gfer);

	/*
	 * To prevent glitches from triggering an unintended level interrupt,
	 * configure GLPR register first and then configure GITR.
	 */
	if (type & IRQ_TYPE_LEVEL_LOW)
		value = readl(glpr) | BIT(gpio % 32);
	else
		value = readl(glpr) & ~BIT(gpio % 32);
	writel(value, glpr);

	if (type & IRQ_TYPE_LEVEL_MASK) {
		value = readl(gitr) | BIT(gpio % 32);
		writel(value, gitr);

		irq_set_handler_locked(d, handle_level_irq);
	} else if (type & IRQ_TYPE_EDGE_BOTH) {
		value = readl(gitr) & ~BIT(gpio % 32);
		writel(value, gitr);

		irq_set_handler_locked(d, handle_edge_irq);
	}

	raw_spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int mrfld_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct mrfld_gpio *priv = gpiochip_get_data(gc);
	u32 gpio = irqd_to_hwirq(d);
	void __iomem *gwmr = gpio_reg(&priv->chip, gpio, GWMR);
	void __iomem *gwsr = gpio_reg(&priv->chip, gpio, GWSR);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&priv->lock, flags);

	/* Clear the existing wake status */
	writel(BIT(gpio % 32), gwsr);

	if (on)
		value = readl(gwmr) | BIT(gpio % 32);
	else
		value = readl(gwmr) & ~BIT(gpio % 32);
	writel(value, gwmr);

	raw_spin_unlock_irqrestore(&priv->lock, flags);

	dev_dbg(priv->dev, "%sable wake for gpio %u\n", on ? "en" : "dis", gpio);
	return 0;
}

static struct irq_chip mrfld_irqchip = {
	.name		= "gpio-merrifield",
	.irq_ack	= mrfld_irq_ack,
	.irq_mask	= mrfld_irq_mask,
	.irq_unmask	= mrfld_irq_unmask,
	.irq_set_type	= mrfld_irq_set_type,
	.irq_set_wake	= mrfld_irq_set_wake,
};

static void mrfld_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct mrfld_gpio *priv = gpiochip_get_data(gc);
	struct irq_chip *irqchip = irq_desc_get_chip(desc);
	unsigned long base, gpio;

	chained_irq_enter(irqchip, desc);

	/* Check GPIO controller to check which pin triggered the interrupt */
	for (base = 0; base < priv->chip.ngpio; base += 32) {
		void __iomem *gisr = gpio_reg(&priv->chip, base, GISR);
		void __iomem *gimr = gpio_reg(&priv->chip, base, GIMR);
		unsigned long pending, enabled;

		pending = readl(gisr);
		enabled = readl(gimr);

		/* Only interrupts that are enabled */
		pending &= enabled;

		for_each_set_bit(gpio, &pending, 32)
			generic_handle_domain_irq(gc->irq.domain, base + gpio);
	}

	chained_irq_exit(irqchip, desc);
}

static int mrfld_irq_init_hw(struct gpio_chip *chip)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	void __iomem *reg;
	unsigned int base;

	for (base = 0; base < priv->chip.ngpio; base += 32) {
		/* Clear the rising-edge detect register */
		reg = gpio_reg(&priv->chip, base, GRER);
		writel(0, reg);
		/* Clear the falling-edge detect register */
		reg = gpio_reg(&priv->chip, base, GFER);
		writel(0, reg);
	}

	return 0;
}

static const char *mrfld_gpio_get_pinctrl_dev_name(struct mrfld_gpio *priv)
{
	struct acpi_device *adev;
	const char *name;

	adev = acpi_dev_get_first_match_dev("INTC1002", NULL, -1);
	if (adev) {
		name = devm_kstrdup(priv->dev, acpi_dev_name(adev), GFP_KERNEL);
		acpi_dev_put(adev);
	} else {
		name = "pinctrl-merrifield";
	}

	return name;
}

static int mrfld_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct mrfld_gpio *priv = gpiochip_get_data(chip);
	const struct mrfld_gpio_pinrange *range;
	const char *pinctrl_dev_name;
	unsigned int i;
	int retval;

	pinctrl_dev_name = mrfld_gpio_get_pinctrl_dev_name(priv);
	for (i = 0; i < ARRAY_SIZE(mrfld_gpio_ranges); i++) {
		range = &mrfld_gpio_ranges[i];
		retval = gpiochip_add_pin_range(&priv->chip, pinctrl_dev_name,
						range->gpio_base,
						range->pin_base,
						range->npins);
		if (retval) {
			dev_err(priv->dev, "failed to add GPIO pin range\n");
			return retval;
		}
	}

	return 0;
}

static int mrfld_gpio_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct gpio_irq_chip *girq;
	struct mrfld_gpio *priv;
	u32 gpio_base, irq_base;
	void __iomem *base;
	int retval;

	retval = pcim_enable_device(pdev);
	if (retval)
		return retval;

	retval = pcim_iomap_regions(pdev, BIT(1) | BIT(0), pci_name(pdev));
	if (retval) {
		dev_err(&pdev->dev, "I/O memory mapping error\n");
		return retval;
	}

	base = pcim_iomap_table(pdev)[1];

	irq_base = readl(base + 0 * sizeof(u32));
	gpio_base = readl(base + 1 * sizeof(u32));

	/* Release the IO mapping, since we already get the info from BAR1 */
	pcim_iounmap_regions(pdev, BIT(1));

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = &pdev->dev;
	priv->reg_base = pcim_iomap_table(pdev)[0];

	priv->chip.label = dev_name(&pdev->dev);
	priv->chip.parent = &pdev->dev;
	priv->chip.request = gpiochip_generic_request;
	priv->chip.free = gpiochip_generic_free;
	priv->chip.direction_input = mrfld_gpio_direction_input;
	priv->chip.direction_output = mrfld_gpio_direction_output;
	priv->chip.get = mrfld_gpio_get;
	priv->chip.set = mrfld_gpio_set;
	priv->chip.get_direction = mrfld_gpio_get_direction;
	priv->chip.set_config = mrfld_gpio_set_config;
	priv->chip.base = gpio_base;
	priv->chip.ngpio = MRFLD_NGPIO;
	priv->chip.can_sleep = false;
	priv->chip.add_pin_ranges = mrfld_gpio_add_pin_ranges;

	raw_spin_lock_init(&priv->lock);

	retval = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (retval < 0)
		return retval;

	girq = &priv->chip.irq;
	girq->chip = &mrfld_irqchip;
	girq->init_hw = mrfld_irq_init_hw;
	girq->parent_handler = mrfld_irq_handler;
	girq->num_parents = 1;
	girq->parents = devm_kcalloc(&pdev->dev, girq->num_parents,
				     sizeof(*girq->parents), GFP_KERNEL);
	if (!girq->parents)
		return -ENOMEM;
	girq->parents[0] = pci_irq_vector(pdev, 0);
	girq->first = irq_base;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

	retval = devm_gpiochip_add_data(&pdev->dev, &priv->chip, priv);
	if (retval) {
		dev_err(&pdev->dev, "gpiochip_add error %d\n", retval);
		return retval;
	}

	pci_set_drvdata(pdev, priv);
	return 0;
}

static const struct pci_device_id mrfld_gpio_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x1199) },
	{ }
};
MODULE_DEVICE_TABLE(pci, mrfld_gpio_ids);

static struct pci_driver mrfld_gpio_driver = {
	.name		= "gpio-merrifield",
	.id_table	= mrfld_gpio_ids,
	.probe		= mrfld_gpio_probe,
};

module_pci_driver(mrfld_gpio_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Intel Merrifield SoC GPIO driver");
MODULE_LICENSE("GPL v2");
