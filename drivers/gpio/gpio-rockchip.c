// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#include "../pinctrl/core.h"
#include "../pinctrl/pinctrl-rockchip.h"

#define GPIO_TYPE_V1		(0)           /* GPIO Version ID reserved */

static const struct rockchip_gpio_regs gpio_regs_v1 = {
	.port_dr = 0x00,
	.port_ddr = 0x04,
	.int_en = 0x30,
	.int_mask = 0x34,
	.int_type = 0x38,
	.int_polarity = 0x3c,
	.int_status = 0x40,
	.int_rawstatus = 0x44,
	.debounce = 0x48,
	.port_eoi = 0x4c,
	.ext_port = 0x50,
};

static int rockchip_gpio_get_direction(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(chip);
	u32 data;

	data = readl_relaxed(bank->reg_base + bank->gpio_regs->port_ddr);
	if (data & BIT(offset))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int rockchip_gpio_set_direction(struct gpio_chip *chip,
				       unsigned int offset, bool input)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(chip);
	unsigned long flags;
	u32 data;

	raw_spin_lock_irqsave(&bank->slock, flags);

	data = readl_relaxed(bank->reg_base + bank->gpio_regs->port_ddr);
	/* set bit to 1 for output, 0 for input */
	if (!input)
		data |= BIT(offset);
	else
		data &= ~BIT(offset);
	writel_relaxed(data, bank->reg_base + bank->gpio_regs->port_ddr);

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	return 0;
}

static void rockchip_gpio_set(struct gpio_chip *gc, unsigned int offset,
			      int value)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(gc);
	void __iomem *reg = bank->reg_base + bank->gpio_regs->port_dr;
	unsigned long flags;
	u32 data;

	raw_spin_lock_irqsave(&bank->slock, flags);

	data = readl(reg);
	data &= ~BIT(offset);
	if (value)
		data |= BIT(offset);
	writel(data, reg);

	raw_spin_unlock_irqrestore(&bank->slock, flags);
}

static int rockchip_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(gc);
	u32 data;

	data = readl(bank->reg_base + bank->gpio_regs->ext_port);
	data >>= offset;
	data &= 1;

	return data;
}

static void rockchip_gpio_set_debounce(struct gpio_chip *gc,
				       unsigned int offset, bool enable)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(gc);
	void __iomem *reg = bank->reg_base + bank->gpio_regs->debounce;
	unsigned long flags;
	u32 data;

	raw_spin_lock_irqsave(&bank->slock, flags);

	data = readl(reg);
	if (enable)
		data |= BIT(offset);
	else
		data &= ~BIT(offset);
	writel(data, reg);

	raw_spin_unlock_irqrestore(&bank->slock, flags);
}

static int rockchip_gpio_direction_input(struct gpio_chip *gc,
					 unsigned int offset)
{
	return rockchip_gpio_set_direction(gc, offset, true);
}

static int rockchip_gpio_direction_output(struct gpio_chip *gc,
					  unsigned int offset, int value)
{
	rockchip_gpio_set(gc, offset, value);

	return rockchip_gpio_set_direction(gc, offset, false);
}

/*
 * gpiolib set_config callback function. The setting of the pin
 * mux function as 'gpio output' will be handled by the pinctrl subsystem
 * interface.
 */
static int rockchip_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				  unsigned long config)
{
	enum pin_config_param param = pinconf_to_config_param(config);

	switch (param) {
	case PIN_CONFIG_INPUT_DEBOUNCE:
		rockchip_gpio_set_debounce(gc, offset, true);
		/*
		 * Rockchip's gpio could only support up to one period
		 * of the debounce clock(pclk), which is far away from
		 * satisftying the requirement, as pclk is usually near
		 * 100MHz shared by all peripherals. So the fact is it
		 * has crippled debounce capability could only be useful
		 * to prevent any spurious glitches from waking up the system
		 * if the gpio is conguired as wakeup interrupt source. Let's
		 * still return -ENOTSUPP as before, to make sure the caller
		 * of gpiod_set_debounce won't change its behaviour.
		 */
		return -ENOTSUPP;
	default:
		return -ENOTSUPP;
	}
}

/*
 * gpiolib gpio_to_irq callback function. Creates a mapping between a GPIO pin
 * and a virtual IRQ, if not already present.
 */
static int rockchip_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(gc);
	unsigned int virq;

	if (!bank->domain)
		return -ENXIO;

	virq = irq_create_mapping(bank->domain, offset);

	return (virq) ? : -ENXIO;
}

static const struct gpio_chip rockchip_gpiolib_chip = {
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.set = rockchip_gpio_set,
	.get = rockchip_gpio_get,
	.get_direction	= rockchip_gpio_get_direction,
	.direction_input = rockchip_gpio_direction_input,
	.direction_output = rockchip_gpio_direction_output,
	.set_config = rockchip_gpio_set_config,
	.to_irq = rockchip_gpio_to_irq,
	.owner = THIS_MODULE,
};

static void rockchip_irq_demux(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct rockchip_pin_bank *bank = irq_desc_get_handler_data(desc);
	u32 pend;

	dev_dbg(bank->dev, "got irq for bank %s\n", bank->name);

	chained_irq_enter(chip, desc);

	pend = readl_relaxed(bank->reg_base + bank->gpio_regs->int_status);

	while (pend) {
		unsigned int irq, virq;

		irq = __ffs(pend);
		pend &= ~BIT(irq);
		virq = irq_find_mapping(bank->domain, irq);

		if (!virq) {
			dev_err(bank->dev, "unmapped irq %d\n", irq);
			continue;
		}

		dev_dbg(bank->dev, "handling irq %d\n", irq);

		/*
		 * Triggering IRQ on both rising and falling edge
		 * needs manual intervention.
		 */
		if (bank->toggle_edge_mode & BIT(irq)) {
			u32 data, data_old, polarity;
			unsigned long flags;

			data = readl_relaxed(bank->reg_base +
					     bank->gpio_regs->ext_port);
			do {
				raw_spin_lock_irqsave(&bank->slock, flags);

				polarity = readl_relaxed(bank->reg_base +
							 bank->gpio_regs->int_polarity);
				if (data & BIT(irq))
					polarity &= ~BIT(irq);
				else
					polarity |= BIT(irq);
				writel(polarity,
				       bank->reg_base +
				       bank->gpio_regs->int_polarity);

				raw_spin_unlock_irqrestore(&bank->slock, flags);

				data_old = data;
				data = readl_relaxed(bank->reg_base +
						     bank->gpio_regs->ext_port);
			} while ((data & BIT(irq)) != (data_old & BIT(irq)));
		}

		generic_handle_irq(virq);
	}

	chained_irq_exit(chip, desc);
}

static int rockchip_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct rockchip_pin_bank *bank = gc->private;
	u32 mask = BIT(d->hwirq);
	u32 polarity;
	u32 level;
	u32 data;
	unsigned long flags;

	raw_spin_lock_irqsave(&bank->slock, flags);

	data = readl_relaxed(bank->reg_base + bank->gpio_regs->port_ddr);
	data &= ~mask;
	writel_relaxed(data, bank->reg_base + bank->gpio_regs->port_ddr);

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_lock_irqsave(&bank->slock, flags);
	irq_gc_lock(gc);

	level = readl_relaxed(gc->reg_base + bank->gpio_regs->int_type);
	polarity = readl_relaxed(gc->reg_base + bank->gpio_regs->int_polarity);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		bank->toggle_edge_mode |= mask;
		level |= mask;

		/*
		 * Determine gpio state. If 1 next interrupt should be falling
		 * otherwise rising.
		 */
		data = readl(bank->reg_base + bank->gpio_regs->ext_port);
		if (data & mask)
			polarity &= ~mask;
		else
			polarity |= mask;
		break;
	case IRQ_TYPE_EDGE_RISING:
		bank->toggle_edge_mode &= ~mask;
		level |= mask;
		polarity |= mask;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		bank->toggle_edge_mode &= ~mask;
		level |= mask;
		polarity &= ~mask;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		bank->toggle_edge_mode &= ~mask;
		level &= ~mask;
		polarity |= mask;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		bank->toggle_edge_mode &= ~mask;
		level &= ~mask;
		polarity &= ~mask;
		break;
	default:
		irq_gc_unlock(gc);
		raw_spin_unlock_irqrestore(&bank->slock, flags);
		clk_disable(bank->clk);
		return -EINVAL;
	}

	writel_relaxed(level, gc->reg_base + bank->gpio_regs->int_type);
	writel_relaxed(polarity, gc->reg_base + bank->gpio_regs->int_polarity);

	irq_gc_unlock(gc);
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	return 0;
}

static void rockchip_irq_suspend(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct rockchip_pin_bank *bank = gc->private;

	bank->saved_masks = irq_reg_readl(gc, bank->gpio_regs->int_mask);
	irq_reg_writel(gc, ~gc->wake_active, bank->gpio_regs->int_mask);
}

static void rockchip_irq_resume(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct rockchip_pin_bank *bank = gc->private;

	irq_reg_writel(gc, bank->saved_masks, bank->gpio_regs->int_mask);
}

static void rockchip_irq_enable(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct rockchip_pin_bank *bank = gc->private;

	irq_gc_mask_clr_bit(d);
}

static void rockchip_irq_disable(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct rockchip_pin_bank *bank = gc->private;

	irq_gc_mask_set_bit(d);
	clk_disable(bank->clk);
}

static int rockchip_interrupts_register(struct rockchip_pin_bank *bank)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct irq_chip_generic *gc;
	int ret;

	bank->domain = irq_domain_add_linear(bank->of_node, 32,
					&irq_generic_chip_ops, NULL);
	if (!bank->domain) {
		dev_warn(bank->dev, "could not init irq domain for bank %s\n",
			 bank->name);
		return -EINVAL;
	}

	ret = irq_alloc_domain_generic_chips(bank->domain, 32, 1,
					     "rockchip_gpio_irq",
					     handle_level_irq,
					     clr, 0, 0);
	if (ret) {
		dev_err(bank->dev, "could not alloc generic chips for bank %s\n",
			bank->name);
		irq_domain_remove(bank->domain);
		return -EINVAL;
	}

	gc = irq_get_domain_generic_chip(bank->domain, 0);
	gc->reg_base = bank->reg_base;
	gc->private = bank;
	gc->chip_types[0].regs.mask = bank->gpio_regs->int_mask;
	gc->chip_types[0].regs.ack = bank->gpio_regs->port_eoi;
	gc->chip_types[0].chip.irq_ack = irq_gc_ack_set_bit;
	gc->chip_types[0].chip.irq_mask = irq_gc_mask_set_bit;
	gc->chip_types[0].chip.irq_unmask = irq_gc_mask_clr_bit;
	gc->chip_types[0].chip.irq_enable = rockchip_irq_enable;
	gc->chip_types[0].chip.irq_disable = rockchip_irq_disable;
	gc->chip_types[0].chip.irq_set_wake = irq_gc_set_wake;
	gc->chip_types[0].chip.irq_suspend = rockchip_irq_suspend;
	gc->chip_types[0].chip.irq_resume = rockchip_irq_resume;
	gc->chip_types[0].chip.irq_set_type = rockchip_irq_set_type;
	gc->wake_enabled = IRQ_MSK(bank->nr_pins);

	/*
	 * Linux assumes that all interrupts start out disabled/masked.
	 * Our driver only uses the concept of masked and always keeps
	 * things enabled, so for us that's all masked and all enabled.
	 */
	writel_relaxed(0xffffffff, bank->reg_base + bank->gpio_regs->int_mask);
	writel_relaxed(0xffffffff, bank->reg_base + bank->gpio_regs->port_eoi);
	writel_relaxed(0xffffffff, bank->reg_base + bank->gpio_regs->int_en);
	gc->mask_cache = 0xffffffff;

	irq_set_chained_handler_and_data(bank->irq,
					 rockchip_irq_demux, bank);

	return 0;
}

static int rockchip_gpiolib_register(struct rockchip_pin_bank *bank)
{
	struct gpio_chip *gc;
	int ret;

	bank->gpio_chip = rockchip_gpiolib_chip;

	gc = &bank->gpio_chip;
	gc->base = bank->pin_base;
	gc->ngpio = bank->nr_pins;
	gc->label = bank->name;
	gc->parent = bank->dev;
#ifdef CONFIG_OF_GPIO
	gc->of_node = of_node_get(bank->of_node);
#endif

	ret = gpiochip_add_data(gc, bank);
	if (ret) {
		dev_err(bank->dev, "failed to add gpiochip %s, %d\n",
			gc->label, ret);
		return ret;
	}

	/*
	 * For DeviceTree-supported systems, the gpio core checks the
	 * pinctrl's device node for the "gpio-ranges" property.
	 * If it is present, it takes care of adding the pin ranges
	 * for the driver. In this case the driver can skip ahead.
	 *
	 * In order to remain compatible with older, existing DeviceTree
	 * files which don't set the "gpio-ranges" property or systems that
	 * utilize ACPI the driver has to call gpiochip_add_pin_range().
	 */
	if (!of_property_read_bool(bank->of_node, "gpio-ranges")) {
		struct device_node *pctlnp = of_get_parent(bank->of_node);
		struct pinctrl_dev *pctldev = NULL;

		if (!pctlnp)
			return -ENODATA;

		pctldev = of_pinctrl_get(pctlnp);
		if (!pctldev)
			return -ENODEV;

		ret = gpiochip_add_pin_range(gc, dev_name(pctldev->dev), 0,
					     gc->base, gc->ngpio);
		if (ret) {
			dev_err(bank->dev, "Failed to add pin range\n");
			goto fail;
		}
	}

	ret = rockchip_interrupts_register(bank);
	if (ret) {
		dev_err(bank->dev, "failed to register interrupt, %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	gpiochip_remove(&bank->gpio_chip);

	return ret;
}

static int rockchip_get_bank_data(struct rockchip_pin_bank *bank)
{
	struct resource res;

	if (of_address_to_resource(bank->of_node, 0, &res)) {
		dev_err(bank->dev, "cannot find IO resource for bank\n");
		return -ENOENT;
	}

	bank->reg_base = devm_ioremap_resource(bank->dev, &res);
	if (IS_ERR(bank->reg_base))
		return PTR_ERR(bank->reg_base);

	bank->irq = irq_of_parse_and_map(bank->of_node, 0);

	bank->gpio_regs = &gpio_regs_v1;
	bank->gpio_type = GPIO_TYPE_V1;

	bank->clk = of_clk_get(bank->of_node, 0);
	if (!IS_ERR(bank->clk))
		return clk_prepare_enable(bank->clk);

	bank->clk = NULL;
	return 0;
}

static struct rockchip_pin_bank *
rockchip_gpio_find_bank(struct pinctrl_dev *pctldev, int id)
{
	struct rockchip_pinctrl *info;
	struct rockchip_pin_bank *bank;
	int i, found = 0;

	info = pinctrl_dev_get_drvdata(pctldev);
	bank = info->ctrl->pin_banks;
	for (i = 0; i < info->ctrl->nr_banks; i++, bank++) {
		if (bank->bank_num == id) {
			found = 1;
			break;
		}
	}

	return found ? bank : NULL;
}

static int rockchip_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *pctlnp = of_get_parent(np);
	struct pinctrl_dev *pctldev = NULL;
	struct rockchip_pin_bank *bank = NULL;
	static int gpio;
	int id, ret;

	if (!np || !pctlnp)
		return -ENODEV;

	pctldev = of_pinctrl_get(pctlnp);
	if (!pctldev)
		return -EPROBE_DEFER;

	id = of_alias_get_id(np, "gpio");
	if (id < 0)
		id = gpio++;

	bank = rockchip_gpio_find_bank(pctldev, id);
	if (!bank)
		return -EINVAL;

	bank->dev = dev;
	bank->of_node = np;

	raw_spin_lock_init(&bank->slock);

	ret = rockchip_get_bank_data(bank);
	if (ret)
		return ret;

	ret = rockchip_gpiolib_register(bank);
	if (ret) {
		clk_disable_unprepare(bank->clk);
		return ret;
	}

	platform_set_drvdata(pdev, bank);
	dev_info(dev, "probed %pOF\n", np);

	return 0;
}

static int rockchip_gpio_remove(struct platform_device *pdev)
{
	struct rockchip_pin_bank *bank = platform_get_drvdata(pdev);

	clk_disable_unprepare(bank->clk);
	gpiochip_remove(&bank->gpio_chip);

	return 0;
}

static const struct of_device_id rockchip_gpio_match[] = {
	{ .compatible = "rockchip,gpio-bank", },
	{ .compatible = "rockchip,rk3188-gpio-bank0" },
	{ },
};

static struct platform_driver rockchip_gpio_driver = {
	.probe		= rockchip_gpio_probe,
	.remove		= rockchip_gpio_remove,
	.driver		= {
		.name	= "rockchip-gpio",
		.of_match_table = rockchip_gpio_match,
	},
};

static int __init rockchip_gpio_init(void)
{
	return platform_driver_register(&rockchip_gpio_driver);
}
postcore_initcall(rockchip_gpio_init);

static void __exit rockchip_gpio_exit(void)
{
	platform_driver_unregister(&rockchip_gpio_driver);
}
module_exit(rockchip_gpio_exit);

MODULE_DESCRIPTION("Rockchip gpio driver");
MODULE_ALIAS("platform:rockchip-gpio");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, rockchip_gpio_match);
