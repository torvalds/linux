// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include "../pinctrl/core.h"
#include "../pinctrl/pinctrl-rockchip.h"

#define GPIO_TYPE_V1		(0)           /* GPIO Version ID reserved */
#define GPIO_TYPE_V2		(0x01000C2B)  /* GPIO Version ID 0x01000C2B */
#define GPIO_TYPE_V2_1		(0x0101157C)  /* GPIO Version ID 0x0101157C */

#define GPIO_MAX_PINS	(32)

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

static const struct rockchip_gpio_regs gpio_regs_v2 = {
	.port_dr = 0x00,
	.port_ddr = 0x08,
	.int_en = 0x10,
	.int_mask = 0x18,
	.int_type = 0x20,
	.int_polarity = 0x28,
	.int_bothedge = 0x30,
	.int_status = 0x50,
	.int_rawstatus = 0x58,
	.debounce = 0x38,
	.dbclk_div_en = 0x40,
	.dbclk_div_con = 0x48,
	.port_eoi = 0x60,
	.ext_port = 0x70,
	.version_id = 0x78,
};

static inline void gpio_writel_v2(u32 val, void __iomem *reg)
{
	writel((val & 0xffff) | 0xffff0000, reg);
	writel((val >> 16) | 0xffff0000, reg + 0x4);
}

static inline u32 gpio_readl_v2(void __iomem *reg)
{
	return readl(reg + 0x4) << 16 | readl(reg);
}

static inline void rockchip_gpio_writel(struct rockchip_pin_bank *bank,
					u32 value, unsigned int offset)
{
	void __iomem *reg = bank->reg_base + offset;

	if (bank->gpio_type == GPIO_TYPE_V2)
		gpio_writel_v2(value, reg);
	else
		writel(value, reg);
}

static inline u32 rockchip_gpio_readl(struct rockchip_pin_bank *bank,
				      unsigned int offset)
{
	void __iomem *reg = bank->reg_base + offset;
	u32 value;

	if (bank->gpio_type == GPIO_TYPE_V2)
		value = gpio_readl_v2(reg);
	else
		value = readl(reg);

	return value;
}

static inline void rockchip_gpio_writel_bit(struct rockchip_pin_bank *bank,
					    u32 bit, u32 value,
					    unsigned int offset)
{
	void __iomem *reg = bank->reg_base + offset;
	u32 data;

	if (bank->gpio_type == GPIO_TYPE_V2) {
		if (value)
			data = BIT(bit % 16) | BIT(bit % 16 + 16);
		else
			data = BIT(bit % 16 + 16);
		writel(data, bit >= 16 ? reg + 0x4 : reg);
	} else {
		data = readl(reg);
		data &= ~BIT(bit);
		if (value)
			data |= BIT(bit);
		writel(data, reg);
	}
}

static inline u32 rockchip_gpio_readl_bit(struct rockchip_pin_bank *bank,
					  u32 bit, unsigned int offset)
{
	void __iomem *reg = bank->reg_base + offset;
	u32 data;

	if (bank->gpio_type == GPIO_TYPE_V2) {
		data = readl(bit >= 16 ? reg + 0x4 : reg);
		data >>= bit % 16;
	} else {
		data = readl(reg);
		data >>= bit;
	}

	return data & (0x1);
}

static int rockchip_gpio_get_direction(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(chip);
	u32 data;

	data = rockchip_gpio_readl_bit(bank, offset, bank->gpio_regs->port_ddr);
	if (data)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int rockchip_gpio_set_direction(struct gpio_chip *chip,
				       unsigned int offset, bool input)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(chip);
	unsigned long flags;
	u32 data = input ? 0 : 1;

	raw_spin_lock_irqsave(&bank->slock, flags);
	rockchip_gpio_writel_bit(bank, offset, data, bank->gpio_regs->port_ddr);
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	return 0;
}

static void rockchip_gpio_set(struct gpio_chip *gc, unsigned int offset,
			      int value)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(gc);
	unsigned long flags;

	raw_spin_lock_irqsave(&bank->slock, flags);
	rockchip_gpio_writel_bit(bank, offset, value, bank->gpio_regs->port_dr);
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

static int rockchip_gpio_set_debounce(struct gpio_chip *gc,
				      unsigned int offset,
				      unsigned int debounce)
{
	struct rockchip_pin_bank *bank = gpiochip_get_data(gc);
	const struct rockchip_gpio_regs	*reg = bank->gpio_regs;
	unsigned long flags, div_reg, freq, max_debounce;
	bool div_debounce_support;
	unsigned int cur_div_reg;
	u64 div;

	if (bank->gpio_type == GPIO_TYPE_V2 && !IS_ERR(bank->db_clk)) {
		div_debounce_support = true;
		freq = clk_get_rate(bank->db_clk);
		if (!freq)
			return -EINVAL;
		max_debounce = (GENMASK(23, 0) + 1) * 2 * 1000000 / freq;
		if ((unsigned long)debounce > max_debounce)
			return -EINVAL;

		div = debounce * freq;
		div_reg = DIV_ROUND_CLOSEST_ULL(div, 2 * USEC_PER_SEC) - 1;
	} else {
		div_debounce_support = false;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);

	/* Only the v1 needs to configure div_en and div_con for dbclk */
	if (debounce) {
		if (div_debounce_support) {
			/* Configure the max debounce from consumers */
			cur_div_reg = readl(bank->reg_base +
					    reg->dbclk_div_con);
			if (cur_div_reg < div_reg)
				writel(div_reg, bank->reg_base +
				       reg->dbclk_div_con);
			rockchip_gpio_writel_bit(bank, offset, 1,
						 reg->dbclk_div_en);
		}

		rockchip_gpio_writel_bit(bank, offset, 1, reg->debounce);
	} else {
		if (div_debounce_support)
			rockchip_gpio_writel_bit(bank, offset, 0,
						 reg->dbclk_div_en);

		rockchip_gpio_writel_bit(bank, offset, 0, reg->debounce);
	}

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	/* Enable or disable dbclk at last */
	if (div_debounce_support) {
		if (debounce)
			clk_prepare_enable(bank->db_clk);
		else
			clk_disable_unprepare(bank->db_clk);
	}

	return 0;
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
	unsigned int debounce = pinconf_to_config_argument(config);
	int ret = 0;

	switch (param) {
	case PIN_CONFIG_INPUT_DEBOUNCE:
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
		rockchip_gpio_set_debounce(gc, offset, debounce);
		ret = -ENOTSUPP;
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}

	return ret;
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
	int ret = 0;

	raw_spin_lock_irqsave(&bank->slock, flags);

	rockchip_gpio_writel_bit(bank, d->hwirq, 0,
				 bank->gpio_regs->port_ddr);

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_lock_irqsave(&bank->slock, flags);

	level = rockchip_gpio_readl(bank, bank->gpio_regs->int_type);
	polarity = rockchip_gpio_readl(bank, bank->gpio_regs->int_polarity);

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		if (bank->gpio_type == GPIO_TYPE_V2) {
			bank->toggle_edge_mode &= ~mask;
			rockchip_gpio_writel_bit(bank, d->hwirq, 1,
						 bank->gpio_regs->int_bothedge);
			goto out;
		} else {
			bank->toggle_edge_mode |= mask;
			level |= mask;

			/*
			 * Determine gpio state. If 1 next interrupt should be
			 * falling otherwise rising.
			 */
			data = readl(bank->reg_base + bank->gpio_regs->ext_port);
			if (data & mask)
				polarity &= ~mask;
			else
				polarity |= mask;
		}
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
		ret = -EINVAL;
		goto out;
	}

	rockchip_gpio_writel(bank, level, bank->gpio_regs->int_type);
	rockchip_gpio_writel(bank, polarity, bank->gpio_regs->int_polarity);
out:
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	return ret;
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
	irq_gc_mask_clr_bit(d);
}

static void rockchip_irq_disable(struct irq_data *d)
{
	irq_gc_mask_set_bit(d);
}

static int rockchip_interrupts_register(struct rockchip_pin_bank *bank)
{
	unsigned int clr = IRQ_NOREQUEST | IRQ_NOPROBE | IRQ_NOAUTOEN;
	struct irq_chip_generic *gc;
	int ret;

	bank->domain = irq_domain_create_linear(dev_fwnode(bank->dev), 32,
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
	if (bank->gpio_type == GPIO_TYPE_V2) {
		gc->reg_writel = gpio_writel_v2;
		gc->reg_readl = gpio_readl_v2;
	}

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
	rockchip_gpio_writel(bank, 0xffffffff, bank->gpio_regs->int_mask);
	rockchip_gpio_writel(bank, 0xffffffff, bank->gpio_regs->port_eoi);
	rockchip_gpio_writel(bank, 0xffffffff, bank->gpio_regs->int_en);
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

	if (!gc->base)
		gc->base = GPIO_MAX_PINS * bank->bank_num;
	if (!gc->ngpio)
		gc->ngpio = GPIO_MAX_PINS;
	if (!gc->label) {
		gc->label = kasprintf(GFP_KERNEL, "gpio%d", bank->bank_num);
		if (!gc->label)
			return -ENOMEM;
	}

	ret = gpiochip_add_data(gc, bank);
	if (ret) {
		dev_err(bank->dev, "failed to add gpiochip %s, %d\n",
			gc->label, ret);
		return ret;
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

static void rockchip_gpio_get_ver(struct rockchip_pin_bank *bank)
{
	int id = readl(bank->reg_base + gpio_regs_v2.version_id);

	/* If not gpio v2, that is default to v1. */
	if (id == GPIO_TYPE_V2 || id == GPIO_TYPE_V2_1) {
		bank->gpio_regs = &gpio_regs_v2;
		bank->gpio_type = GPIO_TYPE_V2;
	} else {
		bank->gpio_regs = &gpio_regs_v1;
		bank->gpio_type = GPIO_TYPE_V1;
	}
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

static int rockchip_gpio_of_get_bank_id(struct device *dev)
{
	static int gpio;
	int bank_id = -1;

	if (IS_ENABLED(CONFIG_OF) && dev->of_node) {
		bank_id = of_alias_get_id(dev->of_node, "gpio");
		if (bank_id < 0)
			bank_id = gpio++;
	}

	return bank_id;
}

#ifdef CONFIG_ACPI
static int rockchip_gpio_acpi_get_bank_id(struct device *dev)
{
	struct acpi_device *adev;
	unsigned long bank_id = -1;
	const char *uid;
	int ret;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENXIO;

	uid = acpi_device_uid(adev);
	if (!uid || !(*uid)) {
		dev_err(dev, "Cannot retrieve UID\n");
		return -ENODEV;
	}

	ret = kstrtoul(uid, 0, &bank_id);

	return !ret ? bank_id : -ERANGE;
}
#else
static int rockchip_gpio_acpi_get_bank_id(struct device *dev)
{
	return -ENOENT;
}
#endif /* CONFIG_ACPI */

static int rockchip_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_dev *pctldev = NULL;
	struct rockchip_pin_bank *bank = NULL;
	int bank_id = 0;
	int ret;

	bank_id = rockchip_gpio_acpi_get_bank_id(dev);
	if (bank_id < 0) {
		bank_id = rockchip_gpio_of_get_bank_id(dev);
		if (bank_id < 0)
			return bank_id;
	}

	if (!ACPI_COMPANION(dev)) {
		struct device_node *pctlnp = of_get_parent(dev->of_node);

		pctldev = of_pinctrl_get(pctlnp);
		if (!pctldev)
			return -EPROBE_DEFER;

		bank = rockchip_gpio_find_bank(pctldev, bank_id);
		if (!bank)
			return -ENODEV;
	}

	if (!bank) {
		bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
		if (!bank)
			return -ENOMEM;
	}

	bank->bank_num = bank_id;
	bank->dev = dev;

	bank->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bank->reg_base))
		return PTR_ERR(bank->reg_base);

	bank->irq = platform_get_irq(pdev, 0);
	if (bank->irq < 0)
		return bank->irq;

	raw_spin_lock_init(&bank->slock);

	if (!ACPI_COMPANION(dev)) {
		bank->clk = devm_clk_get(dev, "bus");
		if (IS_ERR(bank->clk)) {
			bank->clk = of_clk_get(dev->of_node, 0);
			if (IS_ERR(bank->clk)) {
				dev_err(dev, "fail to get apb clock\n");
				return PTR_ERR(bank->clk);
			}
		}

		bank->db_clk = devm_clk_get(dev, "db");
		if (IS_ERR(bank->db_clk)) {
			bank->db_clk = of_clk_get(dev->of_node, 1);
			if (IS_ERR(bank->db_clk))
				bank->db_clk = NULL;
		}
	}

	clk_prepare_enable(bank->clk);
	clk_prepare_enable(bank->db_clk);

	rockchip_gpio_get_ver(bank);

	/*
	 * Prevent clashes with a deferred output setting
	 * being added right at this moment.
	 */
	mutex_lock(&bank->deferred_lock);

	ret = rockchip_gpiolib_register(bank);
	if (ret) {
		dev_err(bank->dev, "Failed to register gpio %d\n", ret);
		goto err_unlock;
	}

	if (!device_property_read_bool(bank->dev, "gpio-ranges") && pctldev) {
		struct gpio_chip *gc = &bank->gpio_chip;

		ret = gpiochip_add_pin_range(gc, dev_name(pctldev->dev), 0,
					     gc->base, gc->ngpio);
		if (ret) {
			dev_err(bank->dev, "Failed to add pin range\n");
			goto err_unlock;
		}
	}

	while (!list_empty(&bank->deferred_pins)) {
		struct rockchip_pin_deferred *cfg;

		cfg = list_first_entry(&bank->deferred_pins,
				       struct rockchip_pin_deferred, head);
		if (!cfg)
			break;

		list_del(&cfg->head);

		switch (cfg->param) {
		case PIN_CONFIG_OUTPUT:
			ret = rockchip_gpio_direction_output(&bank->gpio_chip, cfg->pin, cfg->arg);
			if (ret)
				dev_warn(dev, "setting output pin %u to %u failed\n", cfg->pin,
					 cfg->arg);
			break;
		default:
			dev_warn(dev, "unknown deferred config param %d\n", cfg->param);
			break;
		}
		kfree(cfg);
	}

	mutex_unlock(&bank->deferred_lock);

	platform_set_drvdata(pdev, bank);
	dev_info(dev, "probed %pfw\n", dev_fwnode(dev));

	return 0;
err_unlock:
	mutex_unlock(&bank->deferred_lock);
	clk_disable_unprepare(bank->clk);
	clk_disable_unprepare(bank->db_clk);

	return ret;
}

static int rockchip_gpio_remove(struct platform_device *pdev)
{
	struct rockchip_pin_bank *bank = platform_get_drvdata(pdev);

	clk_disable_unprepare(bank->clk);
	clk_disable_unprepare(bank->db_clk);
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
