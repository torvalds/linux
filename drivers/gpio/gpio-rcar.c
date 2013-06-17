/*
 * Renesas R-Car GPIO Support
 *
 *  Copyright (C) 2013 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

struct gpio_rcar_priv {
	void __iomem *base;
	spinlock_t lock;
	struct gpio_rcar_config config;
	struct platform_device *pdev;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
	struct irq_domain *irq_domain;
};

#define IOINTSEL 0x00
#define INOUTSEL 0x04
#define OUTDT 0x08
#define INDT 0x0c
#define INTDT 0x10
#define INTCLR 0x14
#define INTMSK 0x18
#define MSKCLR 0x1c
#define POSNEG 0x20
#define EDGLEVEL 0x24
#define FILONOFF 0x28
#define BOTHEDGE 0x4c

#define RCAR_MAX_GPIO_PER_BANK		32

static inline u32 gpio_rcar_read(struct gpio_rcar_priv *p, int offs)
{
	return ioread32(p->base + offs);
}

static inline void gpio_rcar_write(struct gpio_rcar_priv *p, int offs,
				   u32 value)
{
	iowrite32(value, p->base + offs);
}

static void gpio_rcar_modify_bit(struct gpio_rcar_priv *p, int offs,
				 int bit, bool value)
{
	u32 tmp = gpio_rcar_read(p, offs);

	if (value)
		tmp |= BIT(bit);
	else
		tmp &= ~BIT(bit);

	gpio_rcar_write(p, offs, tmp);
}

static void gpio_rcar_irq_disable(struct irq_data *d)
{
	struct gpio_rcar_priv *p = irq_data_get_irq_chip_data(d);

	gpio_rcar_write(p, INTMSK, ~BIT(irqd_to_hwirq(d)));
}

static void gpio_rcar_irq_enable(struct irq_data *d)
{
	struct gpio_rcar_priv *p = irq_data_get_irq_chip_data(d);

	gpio_rcar_write(p, MSKCLR, BIT(irqd_to_hwirq(d)));
}

static void gpio_rcar_config_interrupt_input_mode(struct gpio_rcar_priv *p,
						  unsigned int hwirq,
						  bool active_high_rising_edge,
						  bool level_trigger,
						  bool both)
{
	unsigned long flags;

	/* follow steps in the GPIO documentation for
	 * "Setting Edge-Sensitive Interrupt Input Mode" and
	 * "Setting Level-Sensitive Interrupt Input Mode"
	 */

	spin_lock_irqsave(&p->lock, flags);

	/* Configure postive or negative logic in POSNEG */
	gpio_rcar_modify_bit(p, POSNEG, hwirq, !active_high_rising_edge);

	/* Configure edge or level trigger in EDGLEVEL */
	gpio_rcar_modify_bit(p, EDGLEVEL, hwirq, !level_trigger);

	/* Select one edge or both edges in BOTHEDGE */
	if (p->config.has_both_edge_trigger)
		gpio_rcar_modify_bit(p, BOTHEDGE, hwirq, both);

	/* Select "Interrupt Input Mode" in IOINTSEL */
	gpio_rcar_modify_bit(p, IOINTSEL, hwirq, true);

	/* Write INTCLR in case of edge trigger */
	if (!level_trigger)
		gpio_rcar_write(p, INTCLR, BIT(hwirq));

	spin_unlock_irqrestore(&p->lock, flags);
}

static int gpio_rcar_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_rcar_priv *p = irq_data_get_irq_chip_data(d);
	unsigned int hwirq = irqd_to_hwirq(d);

	dev_dbg(&p->pdev->dev, "sense irq = %d, type = %d\n", hwirq, type);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_LEVEL_HIGH:
		gpio_rcar_config_interrupt_input_mode(p, hwirq, true, true,
						      false);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		gpio_rcar_config_interrupt_input_mode(p, hwirq, false, true,
						      false);
		break;
	case IRQ_TYPE_EDGE_RISING:
		gpio_rcar_config_interrupt_input_mode(p, hwirq, true, false,
						      false);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		gpio_rcar_config_interrupt_input_mode(p, hwirq, false, false,
						      false);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		if (!p->config.has_both_edge_trigger)
			return -EINVAL;
		gpio_rcar_config_interrupt_input_mode(p, hwirq, true, false,
						      true);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static irqreturn_t gpio_rcar_irq_handler(int irq, void *dev_id)
{
	struct gpio_rcar_priv *p = dev_id;
	u32 pending;
	unsigned int offset, irqs_handled = 0;

	while ((pending = gpio_rcar_read(p, INTDT))) {
		offset = __ffs(pending);
		gpio_rcar_write(p, INTCLR, BIT(offset));
		generic_handle_irq(irq_find_mapping(p->irq_domain, offset));
		irqs_handled++;
	}

	return irqs_handled ? IRQ_HANDLED : IRQ_NONE;
}

static inline struct gpio_rcar_priv *gpio_to_priv(struct gpio_chip *chip)
{
	return container_of(chip, struct gpio_rcar_priv, gpio_chip);
}

static void gpio_rcar_config_general_input_output_mode(struct gpio_chip *chip,
						       unsigned int gpio,
						       bool output)
{
	struct gpio_rcar_priv *p = gpio_to_priv(chip);
	unsigned long flags;

	/* follow steps in the GPIO documentation for
	 * "Setting General Output Mode" and
	 * "Setting General Input Mode"
	 */

	spin_lock_irqsave(&p->lock, flags);

	/* Configure postive logic in POSNEG */
	gpio_rcar_modify_bit(p, POSNEG, gpio, false);

	/* Select "General Input/Output Mode" in IOINTSEL */
	gpio_rcar_modify_bit(p, IOINTSEL, gpio, false);

	/* Select Input Mode or Output Mode in INOUTSEL */
	gpio_rcar_modify_bit(p, INOUTSEL, gpio, output);

	spin_unlock_irqrestore(&p->lock, flags);
}

static int gpio_rcar_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void gpio_rcar_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);

	/* Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	gpio_rcar_config_general_input_output_mode(chip, offset, false);
}

static int gpio_rcar_direction_input(struct gpio_chip *chip, unsigned offset)
{
	gpio_rcar_config_general_input_output_mode(chip, offset, false);
	return 0;
}

static int gpio_rcar_get(struct gpio_chip *chip, unsigned offset)
{
	u32 bit = BIT(offset);

	/* testing on r8a7790 shows that INDT does not show correct pin state
	 * when configured as output, so use OUTDT in case of output pins */
	if (gpio_rcar_read(gpio_to_priv(chip), INOUTSEL) & bit)
		return (int)(gpio_rcar_read(gpio_to_priv(chip), OUTDT) & bit);
	else
		return (int)(gpio_rcar_read(gpio_to_priv(chip), INDT) & bit);
}

static void gpio_rcar_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_rcar_priv *p = gpio_to_priv(chip);
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	gpio_rcar_modify_bit(p, OUTDT, offset, value);
	spin_unlock_irqrestore(&p->lock, flags);
}

static int gpio_rcar_direction_output(struct gpio_chip *chip, unsigned offset,
				      int value)
{
	/* write GPIO value to output before selecting output mode of pin */
	gpio_rcar_set(chip, offset, value);
	gpio_rcar_config_general_input_output_mode(chip, offset, true);
	return 0;
}

static int gpio_rcar_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return irq_create_mapping(gpio_to_priv(chip)->irq_domain, offset);
}

static int gpio_rcar_irq_domain_map(struct irq_domain *h, unsigned int virq,
				 irq_hw_number_t hw)
{
	struct gpio_rcar_priv *p = h->host_data;

	dev_dbg(&p->pdev->dev, "map hw irq = %d, virq = %d\n", (int)hw, virq);

	irq_set_chip_data(virq, h->host_data);
	irq_set_chip_and_handler(virq, &p->irq_chip, handle_level_irq);
	set_irq_flags(virq, IRQF_VALID); /* kill me now */
	return 0;
}

static struct irq_domain_ops gpio_rcar_irq_domain_ops = {
	.map	= gpio_rcar_irq_domain_map,
};

static void gpio_rcar_parse_pdata(struct gpio_rcar_priv *p)
{
	struct gpio_rcar_config *pdata = p->pdev->dev.platform_data;
	struct device_node *np = p->pdev->dev.of_node;
	struct of_phandle_args args;
	int ret;

	if (pdata) {
		p->config = *pdata;
	} else if (IS_ENABLED(CONFIG_OF) && np) {
		ret = of_parse_phandle_with_args(np, "gpio-ranges",
				"#gpio-range-cells", 0, &args);
		p->config.number_of_pins = ret == 0 && args.args_count == 3
					 ? args.args[2]
					 : RCAR_MAX_GPIO_PER_BANK;
		p->config.gpio_base = -1;
	}

	if (p->config.number_of_pins == 0 ||
	    p->config.number_of_pins > RCAR_MAX_GPIO_PER_BANK) {
		dev_warn(&p->pdev->dev,
			 "Invalid number of gpio lines %u, using %u\n",
			 p->config.number_of_pins, RCAR_MAX_GPIO_PER_BANK);
		p->config.number_of_pins = RCAR_MAX_GPIO_PER_BANK;
	}
}

static int gpio_rcar_probe(struct platform_device *pdev)
{
	struct gpio_rcar_priv *p;
	struct resource *io, *irq;
	struct gpio_chip *gpio_chip;
	struct irq_chip *irq_chip;
	const char *name = dev_name(&pdev->dev);
	int ret;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		ret = -ENOMEM;
		goto err0;
	}

	p->pdev = pdev;
	spin_lock_init(&p->lock);

	/* Get device configuration from DT node or platform data. */
	gpio_rcar_parse_pdata(p);

	platform_set_drvdata(pdev, p);

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);

	if (!io || !irq) {
		dev_err(&pdev->dev, "missing IRQ or IOMEM\n");
		ret = -EINVAL;
		goto err0;
	}

	p->base = devm_ioremap_nocache(&pdev->dev, io->start,
				       resource_size(io));
	if (!p->base) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		ret = -ENXIO;
		goto err0;
	}

	gpio_chip = &p->gpio_chip;
	gpio_chip->request = gpio_rcar_request;
	gpio_chip->free = gpio_rcar_free;
	gpio_chip->direction_input = gpio_rcar_direction_input;
	gpio_chip->get = gpio_rcar_get;
	gpio_chip->direction_output = gpio_rcar_direction_output;
	gpio_chip->set = gpio_rcar_set;
	gpio_chip->to_irq = gpio_rcar_to_irq;
	gpio_chip->label = name;
	gpio_chip->dev = &pdev->dev;
	gpio_chip->owner = THIS_MODULE;
	gpio_chip->base = p->config.gpio_base;
	gpio_chip->ngpio = p->config.number_of_pins;

	irq_chip = &p->irq_chip;
	irq_chip->name = name;
	irq_chip->irq_mask = gpio_rcar_irq_disable;
	irq_chip->irq_unmask = gpio_rcar_irq_enable;
	irq_chip->irq_enable = gpio_rcar_irq_enable;
	irq_chip->irq_disable = gpio_rcar_irq_disable;
	irq_chip->irq_set_type = gpio_rcar_irq_set_type;
	irq_chip->flags	= IRQCHIP_SKIP_SET_WAKE | IRQCHIP_SET_TYPE_MASKED;

	p->irq_domain = irq_domain_add_simple(pdev->dev.of_node,
					      p->config.number_of_pins,
					      p->config.irq_base,
					      &gpio_rcar_irq_domain_ops, p);
	if (!p->irq_domain) {
		ret = -ENXIO;
		dev_err(&pdev->dev, "cannot initialize irq domain\n");
		goto err0;
	}

	if (devm_request_irq(&pdev->dev, irq->start,
			     gpio_rcar_irq_handler, IRQF_SHARED, name, p)) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		ret = -ENOENT;
		goto err1;
	}

	ret = gpiochip_add(gpio_chip);
	if (ret) {
		dev_err(&pdev->dev, "failed to add GPIO controller\n");
		goto err1;
	}

	dev_info(&pdev->dev, "driving %d GPIOs\n", p->config.number_of_pins);

	/* warn in case of mismatch if irq base is specified */
	if (p->config.irq_base) {
		ret = irq_find_mapping(p->irq_domain, 0);
		if (p->config.irq_base != ret)
			dev_warn(&pdev->dev, "irq base mismatch (%u/%u)\n",
				 p->config.irq_base, ret);
	}

	if (p->config.pctl_name) {
		ret = gpiochip_add_pin_range(gpio_chip, p->config.pctl_name, 0,
					     gpio_chip->base, gpio_chip->ngpio);
		if (ret < 0)
			dev_warn(&pdev->dev, "failed to add pin range\n");
	}

	return 0;

err1:
	irq_domain_remove(p->irq_domain);
err0:
	return ret;
}

static int gpio_rcar_remove(struct platform_device *pdev)
{
	struct gpio_rcar_priv *p = platform_get_drvdata(pdev);
	int ret;

	ret = gpiochip_remove(&p->gpio_chip);
	if (ret)
		return ret;

	irq_domain_remove(p->irq_domain);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gpio_rcar_of_table[] = {
	{
		.compatible = "renesas,gpio-rcar",
	},
	{ },
};

MODULE_DEVICE_TABLE(of, gpio_rcar_of_table);
#endif

static struct platform_driver gpio_rcar_device_driver = {
	.probe		= gpio_rcar_probe,
	.remove		= gpio_rcar_remove,
	.driver		= {
		.name	= "gpio_rcar",
		.of_match_table = of_match_ptr(gpio_rcar_of_table),
	}
};

module_platform_driver(gpio_rcar_device_driver);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("Renesas R-Car GPIO Driver");
MODULE_LICENSE("GPL v2");
