/*
 * Renesas R-Car GPIO Support
 *
 *  Copyright (C) 2014 Renesas Electronics Corporation
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
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

struct gpio_rcar_priv {
	void __iomem *base;
	spinlock_t lock;
	struct platform_device *pdev;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
	unsigned int irq_parent;
	atomic_t wakeup_path;
	bool has_both_edge_trigger;
};

#define IOINTSEL 0x00	/* General IO/Interrupt Switching Register */
#define INOUTSEL 0x04	/* General Input/Output Switching Register */
#define OUTDT 0x08	/* General Output Register */
#define INDT 0x0c	/* General Input Register */
#define INTDT 0x10	/* Interrupt Display Register */
#define INTCLR 0x14	/* Interrupt Clear Register */
#define INTMSK 0x18	/* Interrupt Mask Register */
#define MSKCLR 0x1c	/* Interrupt Mask Clear Register */
#define POSNEG 0x20	/* Positive/Negative Logic Select Register */
#define EDGLEVEL 0x24	/* Edge/level Select Register */
#define FILONOFF 0x28	/* Chattering Prevention On/Off Register */
#define BOTHEDGE 0x4c	/* One Edge/Both Edge Select Register */

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
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gpio_rcar_priv *p = gpiochip_get_data(gc);

	gpio_rcar_write(p, INTMSK, ~BIT(irqd_to_hwirq(d)));
}

static void gpio_rcar_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gpio_rcar_priv *p = gpiochip_get_data(gc);

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
	if (p->has_both_edge_trigger)
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
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gpio_rcar_priv *p = gpiochip_get_data(gc);
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
		if (!p->has_both_edge_trigger)
			return -EINVAL;
		gpio_rcar_config_interrupt_input_mode(p, hwirq, true, false,
						      true);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int gpio_rcar_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct gpio_rcar_priv *p = gpiochip_get_data(gc);
	int error;

	if (p->irq_parent) {
		error = irq_set_irq_wake(p->irq_parent, on);
		if (error) {
			dev_dbg(&p->pdev->dev,
				"irq %u doesn't support irq_set_wake\n",
				p->irq_parent);
			p->irq_parent = 0;
		}
	}

	if (on)
		atomic_inc(&p->wakeup_path);
	else
		atomic_dec(&p->wakeup_path);

	return 0;
}

static irqreturn_t gpio_rcar_irq_handler(int irq, void *dev_id)
{
	struct gpio_rcar_priv *p = dev_id;
	u32 pending;
	unsigned int offset, irqs_handled = 0;

	while ((pending = gpio_rcar_read(p, INTDT) &
			  gpio_rcar_read(p, INTMSK))) {
		offset = __ffs(pending);
		gpio_rcar_write(p, INTCLR, BIT(offset));
		generic_handle_irq(irq_find_mapping(p->gpio_chip.irq.domain,
						    offset));
		irqs_handled++;
	}

	return irqs_handled ? IRQ_HANDLED : IRQ_NONE;
}

static void gpio_rcar_config_general_input_output_mode(struct gpio_chip *chip,
						       unsigned int gpio,
						       bool output)
{
	struct gpio_rcar_priv *p = gpiochip_get_data(chip);
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
	struct gpio_rcar_priv *p = gpiochip_get_data(chip);
	int error;

	error = pm_runtime_get_sync(&p->pdev->dev);
	if (error < 0)
		return error;

	error = pinctrl_gpio_request(chip->base + offset);
	if (error)
		pm_runtime_put(&p->pdev->dev);

	return error;
}

static void gpio_rcar_free(struct gpio_chip *chip, unsigned offset)
{
	struct gpio_rcar_priv *p = gpiochip_get_data(chip);

	pinctrl_gpio_free(chip->base + offset);

	/*
	 * Set the GPIO as an input to ensure that the next GPIO request won't
	 * drive the GPIO pin as an output.
	 */
	gpio_rcar_config_general_input_output_mode(chip, offset, false);

	pm_runtime_put(&p->pdev->dev);
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
	if (gpio_rcar_read(gpiochip_get_data(chip), INOUTSEL) & bit)
		return !!(gpio_rcar_read(gpiochip_get_data(chip), OUTDT) & bit);
	else
		return !!(gpio_rcar_read(gpiochip_get_data(chip), INDT) & bit);
}

static void gpio_rcar_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gpio_rcar_priv *p = gpiochip_get_data(chip);
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);
	gpio_rcar_modify_bit(p, OUTDT, offset, value);
	spin_unlock_irqrestore(&p->lock, flags);
}

static void gpio_rcar_set_multiple(struct gpio_chip *chip, unsigned long *mask,
				   unsigned long *bits)
{
	struct gpio_rcar_priv *p = gpiochip_get_data(chip);
	unsigned long flags;
	u32 val, bankmask;

	bankmask = mask[0] & GENMASK(chip->ngpio - 1, 0);
	if (!bankmask)
		return;

	spin_lock_irqsave(&p->lock, flags);
	val = gpio_rcar_read(p, OUTDT);
	val &= ~bankmask;
	val |= (bankmask & bits[0]);
	gpio_rcar_write(p, OUTDT, val);
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

struct gpio_rcar_info {
	bool has_both_edge_trigger;
};

static const struct gpio_rcar_info gpio_rcar_info_gen1 = {
	.has_both_edge_trigger = false,
};

static const struct gpio_rcar_info gpio_rcar_info_gen2 = {
	.has_both_edge_trigger = true,
};

static const struct of_device_id gpio_rcar_of_table[] = {
	{
		.compatible = "renesas,gpio-r8a7743",
		/* RZ/G1 GPIO is identical to R-Car Gen2. */
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7790",
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7791",
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7792",
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7793",
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7794",
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7795",
		/* Gen3 GPIO is identical to Gen2. */
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-r8a7796",
		/* Gen3 GPIO is identical to Gen2. */
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,rcar-gen1-gpio",
		.data = &gpio_rcar_info_gen1,
	}, {
		.compatible = "renesas,rcar-gen2-gpio",
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,rcar-gen3-gpio",
		/* Gen3 GPIO is identical to Gen2. */
		.data = &gpio_rcar_info_gen2,
	}, {
		.compatible = "renesas,gpio-rcar",
		.data = &gpio_rcar_info_gen1,
	}, {
		/* Terminator */
	},
};

MODULE_DEVICE_TABLE(of, gpio_rcar_of_table);

static int gpio_rcar_parse_dt(struct gpio_rcar_priv *p, unsigned int *npins)
{
	struct device_node *np = p->pdev->dev.of_node;
	const struct gpio_rcar_info *info;
	struct of_phandle_args args;
	int ret;

	info = of_device_get_match_data(&p->pdev->dev);

	ret = of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, 0, &args);
	*npins = ret == 0 ? args.args[2] : RCAR_MAX_GPIO_PER_BANK;
	p->has_both_edge_trigger = info->has_both_edge_trigger;

	if (*npins == 0 || *npins > RCAR_MAX_GPIO_PER_BANK) {
		dev_warn(&p->pdev->dev,
			 "Invalid number of gpio lines %u, using %u\n", *npins,
			 RCAR_MAX_GPIO_PER_BANK);
		*npins = RCAR_MAX_GPIO_PER_BANK;
	}

	return 0;
}

static int gpio_rcar_probe(struct platform_device *pdev)
{
	struct gpio_rcar_priv *p;
	struct resource *io, *irq;
	struct gpio_chip *gpio_chip;
	struct irq_chip *irq_chip;
	struct device *dev = &pdev->dev;
	const char *name = dev_name(dev);
	unsigned int npins;
	int ret;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->pdev = pdev;
	spin_lock_init(&p->lock);

	/* Get device configuration from DT node */
	ret = gpio_rcar_parse_dt(p, &npins);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, p);

	pm_runtime_enable(dev);

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(dev, "missing IRQ\n");
		ret = -EINVAL;
		goto err0;
	}

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	p->base = devm_ioremap_resource(dev, io);
	if (IS_ERR(p->base)) {
		ret = PTR_ERR(p->base);
		goto err0;
	}

	gpio_chip = &p->gpio_chip;
	gpio_chip->request = gpio_rcar_request;
	gpio_chip->free = gpio_rcar_free;
	gpio_chip->direction_input = gpio_rcar_direction_input;
	gpio_chip->get = gpio_rcar_get;
	gpio_chip->direction_output = gpio_rcar_direction_output;
	gpio_chip->set = gpio_rcar_set;
	gpio_chip->set_multiple = gpio_rcar_set_multiple;
	gpio_chip->label = name;
	gpio_chip->parent = dev;
	gpio_chip->owner = THIS_MODULE;
	gpio_chip->base = -1;
	gpio_chip->ngpio = npins;

	irq_chip = &p->irq_chip;
	irq_chip->name = name;
	irq_chip->parent_device = dev;
	irq_chip->irq_mask = gpio_rcar_irq_disable;
	irq_chip->irq_unmask = gpio_rcar_irq_enable;
	irq_chip->irq_set_type = gpio_rcar_irq_set_type;
	irq_chip->irq_set_wake = gpio_rcar_irq_set_wake;
	irq_chip->flags	= IRQCHIP_SET_TYPE_MASKED | IRQCHIP_MASK_ON_SUSPEND;

	ret = gpiochip_add_data(gpio_chip, p);
	if (ret) {
		dev_err(dev, "failed to add GPIO controller\n");
		goto err0;
	}

	ret = gpiochip_irqchip_add(gpio_chip, irq_chip, 0, handle_level_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "cannot add irqchip\n");
		goto err1;
	}

	p->irq_parent = irq->start;
	if (devm_request_irq(dev, irq->start, gpio_rcar_irq_handler,
			     IRQF_SHARED, name, p)) {
		dev_err(dev, "failed to request IRQ\n");
		ret = -ENOENT;
		goto err1;
	}

	dev_info(dev, "driving %d GPIOs\n", npins);

	return 0;

err1:
	gpiochip_remove(gpio_chip);
err0:
	pm_runtime_disable(dev);
	return ret;
}

static int gpio_rcar_remove(struct platform_device *pdev)
{
	struct gpio_rcar_priv *p = platform_get_drvdata(pdev);

	gpiochip_remove(&p->gpio_chip);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int __maybe_unused gpio_rcar_suspend(struct device *dev)
{
	struct gpio_rcar_priv *p = dev_get_drvdata(dev);

	if (atomic_read(&p->wakeup_path))
		device_set_wakeup_path(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(gpio_rcar_pm_ops, gpio_rcar_suspend, NULL);

static struct platform_driver gpio_rcar_device_driver = {
	.probe		= gpio_rcar_probe,
	.remove		= gpio_rcar_remove,
	.driver		= {
		.name	= "gpio_rcar",
		.pm     = &gpio_rcar_pm_ops,
		.of_match_table = of_match_ptr(gpio_rcar_of_table),
	}
};

module_platform_driver(gpio_rcar_device_driver);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("Renesas R-Car GPIO Driver");
MODULE_LICENSE("GPL v2");
