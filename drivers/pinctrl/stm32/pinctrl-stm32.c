// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics 2017
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 *
 * Heavily based on Mediatek's pinctrl driver
 */
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "pinctrl-stm32.h"

#define STM32_GPIO_MODER	0x00
#define STM32_GPIO_TYPER	0x04
#define STM32_GPIO_SPEEDR	0x08
#define STM32_GPIO_PUPDR	0x0c
#define STM32_GPIO_IDR		0x10
#define STM32_GPIO_ODR		0x14
#define STM32_GPIO_BSRR		0x18
#define STM32_GPIO_LCKR		0x1c
#define STM32_GPIO_AFRL		0x20
#define STM32_GPIO_AFRH		0x24

/* custom bitfield to backup pin status */
#define STM32_GPIO_BKP_MODE_SHIFT	0
#define STM32_GPIO_BKP_MODE_MASK	GENMASK(1, 0)
#define STM32_GPIO_BKP_ALT_SHIFT	2
#define STM32_GPIO_BKP_ALT_MASK		GENMASK(5, 2)
#define STM32_GPIO_BKP_SPEED_SHIFT	6
#define STM32_GPIO_BKP_SPEED_MASK	GENMASK(7, 6)
#define STM32_GPIO_BKP_PUPD_SHIFT	8
#define STM32_GPIO_BKP_PUPD_MASK	GENMASK(9, 8)
#define STM32_GPIO_BKP_TYPE		10
#define STM32_GPIO_BKP_VAL		11

#define STM32_GPIO_PINS_PER_BANK 16
#define STM32_GPIO_IRQ_LINE	 16

#define SYSCFG_IRQMUX_MASK GENMASK(3, 0)

#define gpio_range_to_bank(chip) \
		container_of(chip, struct stm32_gpio_bank, range)

#define HWSPNLCK_TIMEOUT	1000 /* usec */

static const char * const stm32_gpio_functions[] = {
	"gpio", "af0", "af1",
	"af2", "af3", "af4",
	"af5", "af6", "af7",
	"af8", "af9", "af10",
	"af11", "af12", "af13",
	"af14", "af15", "analog",
};

struct stm32_pinctrl_group {
	const char *name;
	unsigned long config;
	unsigned pin;
};

struct stm32_gpio_bank {
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	spinlock_t lock;
	struct gpio_chip gpio_chip;
	struct pinctrl_gpio_range range;
	struct fwnode_handle *fwnode;
	struct irq_domain *domain;
	u32 bank_nr;
	u32 bank_ioport_nr;
	u32 pin_backup[STM32_GPIO_PINS_PER_BANK];
	u8 irq_type[STM32_GPIO_PINS_PER_BANK];
};

struct stm32_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_desc pctl_desc;
	struct stm32_pinctrl_group *groups;
	unsigned ngroups;
	const char **grp_names;
	struct stm32_gpio_bank *banks;
	unsigned nbanks;
	const struct stm32_pinctrl_match_data *match_data;
	struct irq_domain	*domain;
	struct regmap		*regmap;
	struct regmap_field	*irqmux[STM32_GPIO_PINS_PER_BANK];
	struct hwspinlock *hwlock;
	struct stm32_desc_pin *pins;
	u32 npins;
	u32 pkg;
	u16 irqmux_map;
	spinlock_t irqmux_lock;
};

static inline int stm32_gpio_pin(int gpio)
{
	return gpio % STM32_GPIO_PINS_PER_BANK;
}

static inline u32 stm32_gpio_get_mode(u32 function)
{
	switch (function) {
	case STM32_PIN_GPIO:
		return 0;
	case STM32_PIN_AF(0) ... STM32_PIN_AF(15):
		return 2;
	case STM32_PIN_ANALOG:
		return 3;
	}

	return 0;
}

static inline u32 stm32_gpio_get_alt(u32 function)
{
	switch (function) {
	case STM32_PIN_GPIO:
		return 0;
	case STM32_PIN_AF(0) ... STM32_PIN_AF(15):
		return function - 1;
	case STM32_PIN_ANALOG:
		return 0;
	}

	return 0;
}

static void stm32_gpio_backup_value(struct stm32_gpio_bank *bank,
				    u32 offset, u32 value)
{
	bank->pin_backup[offset] &= ~BIT(STM32_GPIO_BKP_VAL);
	bank->pin_backup[offset] |= value << STM32_GPIO_BKP_VAL;
}

static void stm32_gpio_backup_mode(struct stm32_gpio_bank *bank, u32 offset,
				   u32 mode, u32 alt)
{
	bank->pin_backup[offset] &= ~(STM32_GPIO_BKP_MODE_MASK |
				      STM32_GPIO_BKP_ALT_MASK);
	bank->pin_backup[offset] |= mode << STM32_GPIO_BKP_MODE_SHIFT;
	bank->pin_backup[offset] |= alt << STM32_GPIO_BKP_ALT_SHIFT;
}

static void stm32_gpio_backup_driving(struct stm32_gpio_bank *bank, u32 offset,
				      u32 drive)
{
	bank->pin_backup[offset] &= ~BIT(STM32_GPIO_BKP_TYPE);
	bank->pin_backup[offset] |= drive << STM32_GPIO_BKP_TYPE;
}

static void stm32_gpio_backup_speed(struct stm32_gpio_bank *bank, u32 offset,
				    u32 speed)
{
	bank->pin_backup[offset] &= ~STM32_GPIO_BKP_SPEED_MASK;
	bank->pin_backup[offset] |= speed << STM32_GPIO_BKP_SPEED_SHIFT;
}

static void stm32_gpio_backup_bias(struct stm32_gpio_bank *bank, u32 offset,
				   u32 bias)
{
	bank->pin_backup[offset] &= ~STM32_GPIO_BKP_PUPD_MASK;
	bank->pin_backup[offset] |= bias << STM32_GPIO_BKP_PUPD_SHIFT;
}

/* GPIO functions */

static inline void __stm32_gpio_set(struct stm32_gpio_bank *bank,
	unsigned offset, int value)
{
	stm32_gpio_backup_value(bank, offset, value);

	if (!value)
		offset += STM32_GPIO_PINS_PER_BANK;

	clk_enable(bank->clk);

	writel_relaxed(BIT(offset), bank->base + STM32_GPIO_BSRR);

	clk_disable(bank->clk);
}

static int stm32_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	struct pinctrl_gpio_range *range;
	int pin = offset + (bank->bank_nr * STM32_GPIO_PINS_PER_BANK);

	range = pinctrl_find_gpio_range_from_pin_nolock(pctl->pctl_dev, pin);
	if (!range) {
		dev_err(pctl->dev, "pin %d not in range.\n", pin);
		return -EINVAL;
	}

	return pinctrl_gpio_request(chip->base + offset);
}

static void stm32_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_gpio_free(chip->base + offset);
}

static int stm32_gpio_get_noclk(struct gpio_chip *chip, unsigned int offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);

	return !!(readl_relaxed(bank->base + STM32_GPIO_IDR) & BIT(offset));
}

static int stm32_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);
	int ret;

	clk_enable(bank->clk);

	ret = stm32_gpio_get_noclk(chip, offset);

	clk_disable(bank->clk);

	return ret;
}

static void stm32_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);

	__stm32_gpio_set(bank, offset, value);
}

static int stm32_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int stm32_gpio_direction_output(struct gpio_chip *chip,
	unsigned offset, int value)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);

	__stm32_gpio_set(bank, offset, value);
	pinctrl_gpio_direction_output(chip->base + offset);

	return 0;
}


static int stm32_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);
	struct irq_fwspec fwspec;

	fwspec.fwnode = bank->fwnode;
	fwspec.param_count = 2;
	fwspec.param[0] = offset;
	fwspec.param[1] = IRQ_TYPE_NONE;

	return irq_create_fwspec_mapping(&fwspec);
}

static int stm32_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(chip);
	int pin = stm32_gpio_pin(offset);
	int ret;
	u32 mode, alt;

	stm32_pmx_get_mode(bank, pin, &mode, &alt);
	if ((alt == 0) && (mode == 0))
		ret = GPIO_LINE_DIRECTION_IN;
	else if ((alt == 0) && (mode == 1))
		ret = GPIO_LINE_DIRECTION_OUT;
	else
		ret = -EINVAL;

	return ret;
}

static const struct gpio_chip stm32_gpio_template = {
	.request		= stm32_gpio_request,
	.free			= stm32_gpio_free,
	.get			= stm32_gpio_get,
	.set			= stm32_gpio_set,
	.direction_input	= stm32_gpio_direction_input,
	.direction_output	= stm32_gpio_direction_output,
	.to_irq			= stm32_gpio_to_irq,
	.get_direction		= stm32_gpio_get_direction,
	.set_config		= gpiochip_generic_config,
};

static void stm32_gpio_irq_trigger(struct irq_data *d)
{
	struct stm32_gpio_bank *bank = d->domain->host_data;
	int level;

	/* Do not access the GPIO if this is not LEVEL triggered IRQ. */
	if (!(bank->irq_type[d->hwirq] & IRQ_TYPE_LEVEL_MASK))
		return;

	/* If level interrupt type then retrig */
	level = stm32_gpio_get_noclk(&bank->gpio_chip, d->hwirq);
	if ((level == 0 && bank->irq_type[d->hwirq] == IRQ_TYPE_LEVEL_LOW) ||
	    (level == 1 && bank->irq_type[d->hwirq] == IRQ_TYPE_LEVEL_HIGH))
		irq_chip_retrigger_hierarchy(d);
}

static void stm32_gpio_irq_eoi(struct irq_data *d)
{
	irq_chip_eoi_parent(d);
	stm32_gpio_irq_trigger(d);
};

static int stm32_gpio_set_type(struct irq_data *d, unsigned int type)
{
	struct stm32_gpio_bank *bank = d->domain->host_data;
	u32 parent_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		parent_type = type;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		parent_type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		parent_type = IRQ_TYPE_EDGE_FALLING;
		break;
	default:
		return -EINVAL;
	}

	bank->irq_type[d->hwirq] = type;

	return irq_chip_set_type_parent(d, parent_type);
};

static int stm32_gpio_irq_request_resources(struct irq_data *irq_data)
{
	struct stm32_gpio_bank *bank = irq_data->domain->host_data;
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	unsigned long flags;
	int ret;

	ret = stm32_gpio_direction_input(&bank->gpio_chip, irq_data->hwirq);
	if (ret)
		return ret;

	ret = gpiochip_lock_as_irq(&bank->gpio_chip, irq_data->hwirq);
	if (ret) {
		dev_err(pctl->dev, "unable to lock HW IRQ %lu for IRQ\n",
			irq_data->hwirq);
		return ret;
	}

	flags = irqd_get_trigger_type(irq_data);
	if (flags & IRQ_TYPE_LEVEL_MASK)
		clk_enable(bank->clk);

	return 0;
}

static void stm32_gpio_irq_release_resources(struct irq_data *irq_data)
{
	struct stm32_gpio_bank *bank = irq_data->domain->host_data;

	if (bank->irq_type[irq_data->hwirq] & IRQ_TYPE_LEVEL_MASK)
		clk_disable(bank->clk);

	gpiochip_unlock_as_irq(&bank->gpio_chip, irq_data->hwirq);
}

static void stm32_gpio_irq_unmask(struct irq_data *d)
{
	irq_chip_unmask_parent(d);
	stm32_gpio_irq_trigger(d);
}

static struct irq_chip stm32_gpio_irq_chip = {
	.name		= "stm32gpio",
	.irq_eoi	= stm32_gpio_irq_eoi,
	.irq_ack	= irq_chip_ack_parent,
	.irq_mask	= irq_chip_mask_parent,
	.irq_unmask	= stm32_gpio_irq_unmask,
	.irq_set_type	= stm32_gpio_set_type,
	.irq_set_wake	= irq_chip_set_wake_parent,
	.irq_request_resources = stm32_gpio_irq_request_resources,
	.irq_release_resources = stm32_gpio_irq_release_resources,
};

static int stm32_gpio_domain_translate(struct irq_domain *d,
				       struct irq_fwspec *fwspec,
				       unsigned long *hwirq,
				       unsigned int *type)
{
	if ((fwspec->param_count != 2) ||
	    (fwspec->param[0] >= STM32_GPIO_IRQ_LINE))
		return -EINVAL;

	*hwirq = fwspec->param[0];
	*type = fwspec->param[1];
	return 0;
}

static int stm32_gpio_domain_activate(struct irq_domain *d,
				      struct irq_data *irq_data, bool reserve)
{
	struct stm32_gpio_bank *bank = d->host_data;
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	int ret = 0;

	if (pctl->hwlock) {
		ret = hwspin_lock_timeout_in_atomic(pctl->hwlock,
						    HWSPNLCK_TIMEOUT);
		if (ret) {
			dev_err(pctl->dev, "Can't get hwspinlock\n");
			return ret;
		}
	}

	regmap_field_write(pctl->irqmux[irq_data->hwirq], bank->bank_ioport_nr);

	if (pctl->hwlock)
		hwspin_unlock_in_atomic(pctl->hwlock);

	return ret;
}

static int stm32_gpio_domain_alloc(struct irq_domain *d,
				   unsigned int virq,
				   unsigned int nr_irqs, void *data)
{
	struct stm32_gpio_bank *bank = d->host_data;
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	irq_hw_number_t hwirq = fwspec->param[0];
	unsigned long flags;
	int ret = 0;

	/*
	 * Check first that the IRQ MUX of that line is free.
	 * gpio irq mux is shared between several banks, protect with a lock
	 */
	spin_lock_irqsave(&pctl->irqmux_lock, flags);

	if (pctl->irqmux_map & BIT(hwirq)) {
		dev_err(pctl->dev, "irq line %ld already requested.\n", hwirq);
		ret = -EBUSY;
	} else {
		pctl->irqmux_map |= BIT(hwirq);
	}

	spin_unlock_irqrestore(&pctl->irqmux_lock, flags);
	if (ret)
		return ret;

	parent_fwspec.fwnode = d->parent->fwnode;
	parent_fwspec.param_count = 2;
	parent_fwspec.param[0] = fwspec->param[0];
	parent_fwspec.param[1] = fwspec->param[1];

	irq_domain_set_hwirq_and_chip(d, virq, hwirq, &stm32_gpio_irq_chip,
				      bank);

	return irq_domain_alloc_irqs_parent(d, virq, nr_irqs, &parent_fwspec);
}

static void stm32_gpio_domain_free(struct irq_domain *d, unsigned int virq,
				   unsigned int nr_irqs)
{
	struct stm32_gpio_bank *bank = d->host_data;
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	struct irq_data *irq_data = irq_domain_get_irq_data(d, virq);
	unsigned long flags, hwirq = irq_data->hwirq;

	irq_domain_free_irqs_common(d, virq, nr_irqs);

	spin_lock_irqsave(&pctl->irqmux_lock, flags);
	pctl->irqmux_map &= ~BIT(hwirq);
	spin_unlock_irqrestore(&pctl->irqmux_lock, flags);
}

static const struct irq_domain_ops stm32_gpio_domain_ops = {
	.translate	= stm32_gpio_domain_translate,
	.alloc		= stm32_gpio_domain_alloc,
	.free		= stm32_gpio_domain_free,
	.activate	= stm32_gpio_domain_activate,
};

/* Pinctrl functions */
static struct stm32_pinctrl_group *
stm32_pctrl_find_group_by_pin(struct stm32_pinctrl *pctl, u32 pin)
{
	int i;

	for (i = 0; i < pctl->ngroups; i++) {
		struct stm32_pinctrl_group *grp = pctl->groups + i;

		if (grp->pin == pin)
			return grp;
	}

	return NULL;
}

static bool stm32_pctrl_is_function_valid(struct stm32_pinctrl *pctl,
		u32 pin_num, u32 fnum)
{
	int i;

	for (i = 0; i < pctl->npins; i++) {
		const struct stm32_desc_pin *pin = pctl->pins + i;
		const struct stm32_desc_function *func = pin->functions;

		if (pin->pin.number != pin_num)
			continue;

		while (func && func->name) {
			if (func->num == fnum)
				return true;
			func++;
		}

		break;
	}

	dev_err(pctl->dev, "invalid function %d on pin %d .\n", fnum, pin_num);

	return false;
}

static int stm32_pctrl_dt_node_to_map_func(struct stm32_pinctrl *pctl,
		u32 pin, u32 fnum, struct stm32_pinctrl_group *grp,
		struct pinctrl_map **map, unsigned *reserved_maps,
		unsigned *num_maps)
{
	if (*num_maps == *reserved_maps)
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = grp->name;

	if (!stm32_pctrl_is_function_valid(pctl, pin, fnum))
		return -EINVAL;

	(*map)[*num_maps].data.mux.function = stm32_gpio_functions[fnum];
	(*num_maps)++;

	return 0;
}

static int stm32_pctrl_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				      struct device_node *node,
				      struct pinctrl_map **map,
				      unsigned *reserved_maps,
				      unsigned *num_maps)
{
	struct stm32_pinctrl *pctl;
	struct stm32_pinctrl_group *grp;
	struct property *pins;
	u32 pinfunc, pin, func;
	unsigned long *configs;
	unsigned int num_configs;
	bool has_config = 0;
	unsigned reserve = 0;
	int num_pins, num_funcs, maps_per_pin, i, err = 0;

	pctl = pinctrl_dev_get_drvdata(pctldev);

	pins = of_find_property(node, "pinmux", NULL);
	if (!pins) {
		dev_err(pctl->dev, "missing pins property in node %pOFn .\n",
				node);
		return -EINVAL;
	}

	err = pinconf_generic_parse_dt_config(node, pctldev, &configs,
		&num_configs);
	if (err)
		return err;

	if (num_configs)
		has_config = 1;

	num_pins = pins->length / sizeof(u32);
	num_funcs = num_pins;
	maps_per_pin = 0;
	if (num_funcs)
		maps_per_pin++;
	if (has_config && num_pins >= 1)
		maps_per_pin++;

	if (!num_pins || !maps_per_pin) {
		err = -EINVAL;
		goto exit;
	}

	reserve = num_pins * maps_per_pin;

	err = pinctrl_utils_reserve_map(pctldev, map,
			reserved_maps, num_maps, reserve);
	if (err)
		goto exit;

	for (i = 0; i < num_pins; i++) {
		err = of_property_read_u32_index(node, "pinmux",
				i, &pinfunc);
		if (err)
			goto exit;

		pin = STM32_GET_PIN_NO(pinfunc);
		func = STM32_GET_PIN_FUNC(pinfunc);

		if (!stm32_pctrl_is_function_valid(pctl, pin, func)) {
			err = -EINVAL;
			goto exit;
		}

		grp = stm32_pctrl_find_group_by_pin(pctl, pin);
		if (!grp) {
			dev_err(pctl->dev, "unable to match pin %d to group\n",
					pin);
			err = -EINVAL;
			goto exit;
		}

		err = stm32_pctrl_dt_node_to_map_func(pctl, pin, func, grp, map,
				reserved_maps, num_maps);
		if (err)
			goto exit;

		if (has_config) {
			err = pinctrl_utils_add_map_configs(pctldev, map,
					reserved_maps, num_maps, grp->name,
					configs, num_configs,
					PIN_MAP_TYPE_CONFIGS_GROUP);
			if (err)
				goto exit;
		}
	}

exit:
	kfree(configs);
	return err;
}

static int stm32_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np_config,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct device_node *np;
	unsigned reserved_maps;
	int ret;

	*map = NULL;
	*num_maps = 0;
	reserved_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = stm32_pctrl_dt_subnode_to_map(pctldev, np, map,
				&reserved_maps, num_maps);
		if (ret < 0) {
			pinctrl_utils_free_map(pctldev, *map, *num_maps);
			of_node_put(np);
			return ret;
		}
	}

	return 0;
}

static int stm32_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *stm32_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned group)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->groups[group].name;
}

static int stm32_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned group,
				      const unsigned **pins,
				      unsigned *num_pins)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = (unsigned *)&pctl->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops stm32_pctrl_ops = {
	.dt_node_to_map		= stm32_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
	.get_groups_count	= stm32_pctrl_get_groups_count,
	.get_group_name		= stm32_pctrl_get_group_name,
	.get_group_pins		= stm32_pctrl_get_group_pins,
};


/* Pinmux functions */

static int stm32_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(stm32_gpio_functions);
}

static const char *stm32_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned selector)
{
	return stm32_gpio_functions[selector];
}

static int stm32_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				     unsigned function,
				     const char * const **groups,
				     unsigned * const num_groups)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->grp_names;
	*num_groups = pctl->ngroups;

	return 0;
}

static int stm32_pmx_set_mode(struct stm32_gpio_bank *bank,
			      int pin, u32 mode, u32 alt)
{
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	u32 val;
	int alt_shift = (pin % 8) * 4;
	int alt_offset = STM32_GPIO_AFRL + (pin / 8) * 4;
	unsigned long flags;
	int err = 0;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	if (pctl->hwlock) {
		err = hwspin_lock_timeout_in_atomic(pctl->hwlock,
						    HWSPNLCK_TIMEOUT);
		if (err) {
			dev_err(pctl->dev, "Can't get hwspinlock\n");
			goto unlock;
		}
	}

	val = readl_relaxed(bank->base + alt_offset);
	val &= ~GENMASK(alt_shift + 3, alt_shift);
	val |= (alt << alt_shift);
	writel_relaxed(val, bank->base + alt_offset);

	val = readl_relaxed(bank->base + STM32_GPIO_MODER);
	val &= ~GENMASK(pin * 2 + 1, pin * 2);
	val |= mode << (pin * 2);
	writel_relaxed(val, bank->base + STM32_GPIO_MODER);

	if (pctl->hwlock)
		hwspin_unlock_in_atomic(pctl->hwlock);

	stm32_gpio_backup_mode(bank, pin, mode, alt);

unlock:
	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return err;
}

void stm32_pmx_get_mode(struct stm32_gpio_bank *bank, int pin, u32 *mode,
			u32 *alt)
{
	u32 val;
	int alt_shift = (pin % 8) * 4;
	int alt_offset = STM32_GPIO_AFRL + (pin / 8) * 4;
	unsigned long flags;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + alt_offset);
	val &= GENMASK(alt_shift + 3, alt_shift);
	*alt = val >> alt_shift;

	val = readl_relaxed(bank->base + STM32_GPIO_MODER);
	val &= GENMASK(pin * 2 + 1, pin * 2);
	*mode = val >> (pin * 2);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);
}

static int stm32_pmx_set_mux(struct pinctrl_dev *pctldev,
			    unsigned function,
			    unsigned group)
{
	bool ret;
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct stm32_pinctrl_group *g = pctl->groups + group;
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	u32 mode, alt;
	int pin;

	ret = stm32_pctrl_is_function_valid(pctl, g->pin, function);
	if (!ret)
		return -EINVAL;

	range = pinctrl_find_gpio_range_from_pin(pctldev, g->pin);
	if (!range) {
		dev_err(pctl->dev, "No gpio range defined.\n");
		return -EINVAL;
	}

	bank = gpiochip_get_data(range->gc);
	pin = stm32_gpio_pin(g->pin);

	mode = stm32_gpio_get_mode(function);
	alt = stm32_gpio_get_alt(function);

	return stm32_pmx_set_mode(bank, pin, mode, alt);
}

static int stm32_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned gpio,
			bool input)
{
	struct stm32_gpio_bank *bank = gpiochip_get_data(range->gc);
	int pin = stm32_gpio_pin(gpio);

	return stm32_pmx_set_mode(bank, pin, !input, 0);
}

static const struct pinmux_ops stm32_pmx_ops = {
	.get_functions_count	= stm32_pmx_get_funcs_cnt,
	.get_function_name	= stm32_pmx_get_func_name,
	.get_function_groups	= stm32_pmx_get_func_groups,
	.set_mux		= stm32_pmx_set_mux,
	.gpio_set_direction	= stm32_pmx_gpio_set_direction,
	.strict			= true,
};

/* Pinconf functions */

static int stm32_pconf_set_driving(struct stm32_gpio_bank *bank,
				   unsigned offset, u32 drive)
{
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	unsigned long flags;
	u32 val;
	int err = 0;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	if (pctl->hwlock) {
		err = hwspin_lock_timeout_in_atomic(pctl->hwlock,
						    HWSPNLCK_TIMEOUT);
		if (err) {
			dev_err(pctl->dev, "Can't get hwspinlock\n");
			goto unlock;
		}
	}

	val = readl_relaxed(bank->base + STM32_GPIO_TYPER);
	val &= ~BIT(offset);
	val |= drive << offset;
	writel_relaxed(val, bank->base + STM32_GPIO_TYPER);

	if (pctl->hwlock)
		hwspin_unlock_in_atomic(pctl->hwlock);

	stm32_gpio_backup_driving(bank, offset, drive);

unlock:
	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return err;
}

static u32 stm32_pconf_get_driving(struct stm32_gpio_bank *bank,
	unsigned int offset)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_TYPER);
	val &= BIT(offset);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return (val >> offset);
}

static int stm32_pconf_set_speed(struct stm32_gpio_bank *bank,
				 unsigned offset, u32 speed)
{
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	unsigned long flags;
	u32 val;
	int err = 0;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	if (pctl->hwlock) {
		err = hwspin_lock_timeout_in_atomic(pctl->hwlock,
						    HWSPNLCK_TIMEOUT);
		if (err) {
			dev_err(pctl->dev, "Can't get hwspinlock\n");
			goto unlock;
		}
	}

	val = readl_relaxed(bank->base + STM32_GPIO_SPEEDR);
	val &= ~GENMASK(offset * 2 + 1, offset * 2);
	val |= speed << (offset * 2);
	writel_relaxed(val, bank->base + STM32_GPIO_SPEEDR);

	if (pctl->hwlock)
		hwspin_unlock_in_atomic(pctl->hwlock);

	stm32_gpio_backup_speed(bank, offset, speed);

unlock:
	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return err;
}

static u32 stm32_pconf_get_speed(struct stm32_gpio_bank *bank,
	unsigned int offset)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_SPEEDR);
	val &= GENMASK(offset * 2 + 1, offset * 2);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return (val >> (offset * 2));
}

static int stm32_pconf_set_bias(struct stm32_gpio_bank *bank,
				unsigned offset, u32 bias)
{
	struct stm32_pinctrl *pctl = dev_get_drvdata(bank->gpio_chip.parent);
	unsigned long flags;
	u32 val;
	int err = 0;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	if (pctl->hwlock) {
		err = hwspin_lock_timeout_in_atomic(pctl->hwlock,
						    HWSPNLCK_TIMEOUT);
		if (err) {
			dev_err(pctl->dev, "Can't get hwspinlock\n");
			goto unlock;
		}
	}

	val = readl_relaxed(bank->base + STM32_GPIO_PUPDR);
	val &= ~GENMASK(offset * 2 + 1, offset * 2);
	val |= bias << (offset * 2);
	writel_relaxed(val, bank->base + STM32_GPIO_PUPDR);

	if (pctl->hwlock)
		hwspin_unlock_in_atomic(pctl->hwlock);

	stm32_gpio_backup_bias(bank, offset, bias);

unlock:
	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return err;
}

static u32 stm32_pconf_get_bias(struct stm32_gpio_bank *bank,
	unsigned int offset)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	val = readl_relaxed(bank->base + STM32_GPIO_PUPDR);
	val &= GENMASK(offset * 2 + 1, offset * 2);

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return (val >> (offset * 2));
}

static bool stm32_pconf_get(struct stm32_gpio_bank *bank,
	unsigned int offset, bool dir)
{
	unsigned long flags;
	u32 val;

	clk_enable(bank->clk);
	spin_lock_irqsave(&bank->lock, flags);

	if (dir)
		val = !!(readl_relaxed(bank->base + STM32_GPIO_IDR) &
			 BIT(offset));
	else
		val = !!(readl_relaxed(bank->base + STM32_GPIO_ODR) &
			 BIT(offset));

	spin_unlock_irqrestore(&bank->lock, flags);
	clk_disable(bank->clk);

	return val;
}

static int stm32_pconf_parse_conf(struct pinctrl_dev *pctldev,
		unsigned int pin, enum pin_config_param param,
		enum pin_config_param arg)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	int offset, ret = 0;

	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, pin);
	if (!range) {
		dev_err(pctl->dev, "No gpio range defined.\n");
		return -EINVAL;
	}

	bank = gpiochip_get_data(range->gc);
	offset = stm32_gpio_pin(pin);

	switch (param) {
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		ret = stm32_pconf_set_driving(bank, offset, 0);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		ret = stm32_pconf_set_driving(bank, offset, 1);
		break;
	case PIN_CONFIG_SLEW_RATE:
		ret = stm32_pconf_set_speed(bank, offset, arg);
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		ret = stm32_pconf_set_bias(bank, offset, 0);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		ret = stm32_pconf_set_bias(bank, offset, 1);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = stm32_pconf_set_bias(bank, offset, 2);
		break;
	case PIN_CONFIG_OUTPUT:
		__stm32_gpio_set(bank, offset, arg);
		ret = stm32_pmx_gpio_set_direction(pctldev, range, pin, false);
		break;
	default:
		ret = -ENOTSUPP;
	}

	return ret;
}

static int stm32_pconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group,
				 unsigned long *config)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*config = pctl->groups[group].config;

	return 0;
}

static int stm32_pconf_group_set(struct pinctrl_dev *pctldev, unsigned group,
				 unsigned long *configs, unsigned num_configs)
{
	struct stm32_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct stm32_pinctrl_group *g = &pctl->groups[group];
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		mutex_lock(&pctldev->mutex);
		ret = stm32_pconf_parse_conf(pctldev, g->pin,
			pinconf_to_config_param(configs[i]),
			pinconf_to_config_argument(configs[i]));
		mutex_unlock(&pctldev->mutex);
		if (ret < 0)
			return ret;

		g->config = configs[i];
	}

	return 0;
}

static int stm32_pconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			   unsigned long *configs, unsigned int num_configs)
{
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		ret = stm32_pconf_parse_conf(pctldev, pin,
				pinconf_to_config_param(configs[i]),
				pinconf_to_config_argument(configs[i]));
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void stm32_pconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s,
				 unsigned int pin)
{
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	int offset;
	u32 mode, alt, drive, speed, bias;
	static const char * const modes[] = {
			"input", "output", "alternate", "analog" };
	static const char * const speeds[] = {
			"low", "medium", "high", "very high" };
	static const char * const biasing[] = {
			"floating", "pull up", "pull down", "" };
	bool val;

	range = pinctrl_find_gpio_range_from_pin_nolock(pctldev, pin);
	if (!range)
		return;

	bank = gpiochip_get_data(range->gc);
	offset = stm32_gpio_pin(pin);

	stm32_pmx_get_mode(bank, offset, &mode, &alt);
	bias = stm32_pconf_get_bias(bank, offset);

	seq_printf(s, "%s ", modes[mode]);

	switch (mode) {
	/* input */
	case 0:
		val = stm32_pconf_get(bank, offset, true);
		seq_printf(s, "- %s - %s",
			   val ? "high" : "low",
			   biasing[bias]);
		break;

	/* output */
	case 1:
		drive = stm32_pconf_get_driving(bank, offset);
		speed = stm32_pconf_get_speed(bank, offset);
		val = stm32_pconf_get(bank, offset, false);
		seq_printf(s, "- %s - %s - %s - %s %s",
			   val ? "high" : "low",
			   drive ? "open drain" : "push pull",
			   biasing[bias],
			   speeds[speed], "speed");
		break;

	/* alternate */
	case 2:
		drive = stm32_pconf_get_driving(bank, offset);
		speed = stm32_pconf_get_speed(bank, offset);
		seq_printf(s, "%d - %s - %s - %s %s", alt,
			   drive ? "open drain" : "push pull",
			   biasing[bias],
			   speeds[speed], "speed");
		break;

	/* analog */
	case 3:
		break;
	}
}

static const struct pinconf_ops stm32_pconf_ops = {
	.pin_config_group_get	= stm32_pconf_group_get,
	.pin_config_group_set	= stm32_pconf_group_set,
	.pin_config_set		= stm32_pconf_set,
	.pin_config_dbg_show	= stm32_pconf_dbg_show,
};

static int stm32_gpiolib_register_bank(struct stm32_pinctrl *pctl,
	struct device_node *np)
{
	struct stm32_gpio_bank *bank = &pctl->banks[pctl->nbanks];
	int bank_ioport_nr;
	struct pinctrl_gpio_range *range = &bank->range;
	struct of_phandle_args args;
	struct device *dev = pctl->dev;
	struct resource res;
	int npins = STM32_GPIO_PINS_PER_BANK;
	int bank_nr, err, i = 0;

	if (!IS_ERR(bank->rstc))
		reset_control_deassert(bank->rstc);

	if (of_address_to_resource(np, 0, &res))
		return -ENODEV;

	bank->base = devm_ioremap_resource(dev, &res);
	if (IS_ERR(bank->base))
		return PTR_ERR(bank->base);

	err = clk_prepare(bank->clk);
	if (err) {
		dev_err(dev, "failed to prepare clk (%d)\n", err);
		return err;
	}

	bank->gpio_chip = stm32_gpio_template;

	of_property_read_string(np, "st,bank-name", &bank->gpio_chip.label);

	if (!of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, i, &args)) {
		bank_nr = args.args[1] / STM32_GPIO_PINS_PER_BANK;
		bank->gpio_chip.base = args.args[1];

		/* get the last defined gpio line (offset + nb of pins) */
		npins = args.args[0] + args.args[2];
		while (!of_parse_phandle_with_fixed_args(np, "gpio-ranges", 3, ++i, &args))
			npins = max(npins, (int)(args.args[0] + args.args[2]));
	} else {
		bank_nr = pctl->nbanks;
		bank->gpio_chip.base = bank_nr * STM32_GPIO_PINS_PER_BANK;
		range->name = bank->gpio_chip.label;
		range->id = bank_nr;
		range->pin_base = range->id * STM32_GPIO_PINS_PER_BANK;
		range->base = range->id * STM32_GPIO_PINS_PER_BANK;
		range->npins = npins;
		range->gc = &bank->gpio_chip;
		pinctrl_add_gpio_range(pctl->pctl_dev,
				       &pctl->banks[bank_nr].range);
	}

	if (of_property_read_u32(np, "st,bank-ioport", &bank_ioport_nr))
		bank_ioport_nr = bank_nr;

	bank->gpio_chip.base = bank_nr * STM32_GPIO_PINS_PER_BANK;

	bank->gpio_chip.ngpio = npins;
	bank->gpio_chip.of_node = np;
	bank->gpio_chip.parent = dev;
	bank->bank_nr = bank_nr;
	bank->bank_ioport_nr = bank_ioport_nr;
	spin_lock_init(&bank->lock);

	/* create irq hierarchical domain */
	bank->fwnode = of_node_to_fwnode(np);

	bank->domain = irq_domain_create_hierarchy(pctl->domain, 0,
					STM32_GPIO_IRQ_LINE, bank->fwnode,
					&stm32_gpio_domain_ops, bank);

	if (!bank->domain)
		return -ENODEV;

	err = gpiochip_add_data(&bank->gpio_chip, bank);
	if (err) {
		dev_err(dev, "Failed to add gpiochip(%d)!\n", bank_nr);
		return err;
	}

	dev_info(dev, "%s bank added\n", bank->gpio_chip.label);
	return 0;
}

static struct irq_domain *stm32_pctrl_get_irq_domain(struct device_node *np)
{
	struct device_node *parent;
	struct irq_domain *domain;

	if (!of_find_property(np, "interrupt-parent", NULL))
		return NULL;

	parent = of_irq_find_parent(np);
	if (!parent)
		return ERR_PTR(-ENXIO);

	domain = irq_find_host(parent);
	if (!domain)
		/* domain not registered yet */
		return ERR_PTR(-EPROBE_DEFER);

	return domain;
}

static int stm32_pctrl_dt_setup_irq(struct platform_device *pdev,
			   struct stm32_pinctrl *pctl)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct regmap *rm;
	int offset, ret, i;
	int mask, mask_width;

	pctl->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(pctl->regmap))
		return PTR_ERR(pctl->regmap);

	rm = pctl->regmap;

	ret = of_property_read_u32_index(np, "st,syscfg", 1, &offset);
	if (ret)
		return ret;

	ret = of_property_read_u32_index(np, "st,syscfg", 2, &mask);
	if (ret)
		mask = SYSCFG_IRQMUX_MASK;

	mask_width = fls(mask);

	for (i = 0; i < STM32_GPIO_PINS_PER_BANK; i++) {
		struct reg_field mux;

		mux.reg = offset + (i / 4) * 4;
		mux.lsb = (i % 4) * mask_width;
		mux.msb = mux.lsb + mask_width - 1;

		dev_dbg(dev, "irqmux%d: reg:%#x, lsb:%d, msb:%d\n",
			i, mux.reg, mux.lsb, mux.msb);

		pctl->irqmux[i] = devm_regmap_field_alloc(dev, rm, mux);
		if (IS_ERR(pctl->irqmux[i]))
			return PTR_ERR(pctl->irqmux[i]);
	}

	return 0;
}

static int stm32_pctrl_build_state(struct platform_device *pdev)
{
	struct stm32_pinctrl *pctl = platform_get_drvdata(pdev);
	int i;

	pctl->ngroups = pctl->npins;

	/* Allocate groups */
	pctl->groups = devm_kcalloc(&pdev->dev, pctl->ngroups,
				    sizeof(*pctl->groups), GFP_KERNEL);
	if (!pctl->groups)
		return -ENOMEM;

	/* We assume that one pin is one group, use pin name as group name. */
	pctl->grp_names = devm_kcalloc(&pdev->dev, pctl->ngroups,
				       sizeof(*pctl->grp_names), GFP_KERNEL);
	if (!pctl->grp_names)
		return -ENOMEM;

	for (i = 0; i < pctl->npins; i++) {
		const struct stm32_desc_pin *pin = pctl->pins + i;
		struct stm32_pinctrl_group *group = pctl->groups + i;

		group->name = pin->pin.name;
		group->pin = pin->pin.number;
		pctl->grp_names[i] = pin->pin.name;
	}

	return 0;
}

static int stm32_pctrl_create_pins_tab(struct stm32_pinctrl *pctl,
				       struct stm32_desc_pin *pins)
{
	const struct stm32_desc_pin *p;
	int i, nb_pins_available = 0;

	for (i = 0; i < pctl->match_data->npins; i++) {
		p = pctl->match_data->pins + i;
		if (pctl->pkg && !(pctl->pkg & p->pkg))
			continue;
		pins->pin = p->pin;
		pins->functions = p->functions;
		pins++;
		nb_pins_available++;
	}

	pctl->npins = nb_pins_available;

	return 0;
}

static void stm32_pctl_get_package(struct device_node *np,
				   struct stm32_pinctrl *pctl)
{
	if (of_property_read_u32(np, "st,package", &pctl->pkg)) {
		pctl->pkg = 0;
		dev_warn(pctl->dev, "No package detected, use default one\n");
	} else {
		dev_dbg(pctl->dev, "package detected: %x\n", pctl->pkg);
	}
}

int stm32_pctl_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	const struct of_device_id *match;
	struct device *dev = &pdev->dev;
	struct stm32_pinctrl *pctl;
	struct pinctrl_pin_desc *pins;
	int i, ret, hwlock_id, banks = 0;

	if (!np)
		return -EINVAL;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (!match || !match->data)
		return -EINVAL;

	if (!of_find_property(np, "pins-are-numbered", NULL)) {
		dev_err(dev, "only support pins-are-numbered format\n");
		return -EINVAL;
	}

	pctl = devm_kzalloc(dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pctl);

	/* check for IRQ controller (may require deferred probe) */
	pctl->domain = stm32_pctrl_get_irq_domain(np);
	if (IS_ERR(pctl->domain))
		return PTR_ERR(pctl->domain);

	/* hwspinlock is optional */
	hwlock_id = of_hwspin_lock_get_id(pdev->dev.of_node, 0);
	if (hwlock_id < 0) {
		if (hwlock_id == -EPROBE_DEFER)
			return hwlock_id;
	} else {
		pctl->hwlock = hwspin_lock_request_specific(hwlock_id);
	}

	spin_lock_init(&pctl->irqmux_lock);

	pctl->dev = dev;
	pctl->match_data = match->data;

	/*  get package information */
	stm32_pctl_get_package(np, pctl);

	pctl->pins = devm_kcalloc(pctl->dev, pctl->match_data->npins,
				  sizeof(*pctl->pins), GFP_KERNEL);
	if (!pctl->pins)
		return -ENOMEM;

	ret = stm32_pctrl_create_pins_tab(pctl, pctl->pins);
	if (ret)
		return ret;

	ret = stm32_pctrl_build_state(pdev);
	if (ret) {
		dev_err(dev, "build state failed: %d\n", ret);
		return -EINVAL;
	}

	if (pctl->domain) {
		ret = stm32_pctrl_dt_setup_irq(pdev, pctl);
		if (ret)
			return ret;
	}

	pins = devm_kcalloc(&pdev->dev, pctl->npins, sizeof(*pins),
			    GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < pctl->npins; i++)
		pins[i] = pctl->pins[i].pin;

	pctl->pctl_desc.name = dev_name(&pdev->dev);
	pctl->pctl_desc.owner = THIS_MODULE;
	pctl->pctl_desc.pins = pins;
	pctl->pctl_desc.npins = pctl->npins;
	pctl->pctl_desc.link_consumers = true;
	pctl->pctl_desc.confops = &stm32_pconf_ops;
	pctl->pctl_desc.pctlops = &stm32_pctrl_ops;
	pctl->pctl_desc.pmxops = &stm32_pmx_ops;
	pctl->dev = &pdev->dev;

	pctl->pctl_dev = devm_pinctrl_register(&pdev->dev, &pctl->pctl_desc,
					       pctl);

	if (IS_ERR(pctl->pctl_dev)) {
		dev_err(&pdev->dev, "Failed pinctrl registration\n");
		return PTR_ERR(pctl->pctl_dev);
	}

	for_each_available_child_of_node(np, child)
		if (of_property_read_bool(child, "gpio-controller"))
			banks++;

	if (!banks) {
		dev_err(dev, "at least one GPIO bank is required\n");
		return -EINVAL;
	}
	pctl->banks = devm_kcalloc(dev, banks, sizeof(*pctl->banks),
			GFP_KERNEL);
	if (!pctl->banks)
		return -ENOMEM;

	i = 0;
	for_each_available_child_of_node(np, child) {
		struct stm32_gpio_bank *bank = &pctl->banks[i];

		if (of_property_read_bool(child, "gpio-controller")) {
			bank->rstc = of_reset_control_get_exclusive(child,
								    NULL);
			if (PTR_ERR(bank->rstc) == -EPROBE_DEFER) {
				of_node_put(child);
				return -EPROBE_DEFER;
			}

			bank->clk = of_clk_get_by_name(child, NULL);
			if (IS_ERR(bank->clk)) {
				if (PTR_ERR(bank->clk) != -EPROBE_DEFER)
					dev_err(dev,
						"failed to get clk (%ld)\n",
						PTR_ERR(bank->clk));
				of_node_put(child);
				return PTR_ERR(bank->clk);
			}
			i++;
		}
	}

	for_each_available_child_of_node(np, child) {
		if (of_property_read_bool(child, "gpio-controller")) {
			ret = stm32_gpiolib_register_bank(pctl, child);
			if (ret) {
				of_node_put(child);
				return ret;
			}

			pctl->nbanks++;
		}
	}

	dev_info(dev, "Pinctrl STM32 initialized\n");

	return 0;
}

static int __maybe_unused stm32_pinctrl_restore_gpio_regs(
					struct stm32_pinctrl *pctl, u32 pin)
{
	const struct pin_desc *desc = pin_desc_get(pctl->pctl_dev, pin);
	u32 val, alt, mode, offset = stm32_gpio_pin(pin);
	struct pinctrl_gpio_range *range;
	struct stm32_gpio_bank *bank;
	bool pin_is_irq;
	int ret;

	range = pinctrl_find_gpio_range_from_pin(pctl->pctl_dev, pin);
	if (!range)
		return 0;

	pin_is_irq = gpiochip_line_is_irq(range->gc, offset);

	if (!desc || (!pin_is_irq && !desc->gpio_owner))
		return 0;

	bank = gpiochip_get_data(range->gc);

	alt = bank->pin_backup[offset] & STM32_GPIO_BKP_ALT_MASK;
	alt >>= STM32_GPIO_BKP_ALT_SHIFT;
	mode = bank->pin_backup[offset] & STM32_GPIO_BKP_MODE_MASK;
	mode >>= STM32_GPIO_BKP_MODE_SHIFT;

	ret = stm32_pmx_set_mode(bank, offset, mode, alt);
	if (ret)
		return ret;

	if (mode == 1) {
		val = bank->pin_backup[offset] & BIT(STM32_GPIO_BKP_VAL);
		val = val >> STM32_GPIO_BKP_VAL;
		__stm32_gpio_set(bank, offset, val);
	}

	val = bank->pin_backup[offset] & BIT(STM32_GPIO_BKP_TYPE);
	val >>= STM32_GPIO_BKP_TYPE;
	ret = stm32_pconf_set_driving(bank, offset, val);
	if (ret)
		return ret;

	val = bank->pin_backup[offset] & STM32_GPIO_BKP_SPEED_MASK;
	val >>= STM32_GPIO_BKP_SPEED_SHIFT;
	ret = stm32_pconf_set_speed(bank, offset, val);
	if (ret)
		return ret;

	val = bank->pin_backup[offset] & STM32_GPIO_BKP_PUPD_MASK;
	val >>= STM32_GPIO_BKP_PUPD_SHIFT;
	ret = stm32_pconf_set_bias(bank, offset, val);
	if (ret)
		return ret;

	if (pin_is_irq)
		regmap_field_write(pctl->irqmux[offset], bank->bank_ioport_nr);

	return 0;
}

int __maybe_unused stm32_pinctrl_resume(struct device *dev)
{
	struct stm32_pinctrl *pctl = dev_get_drvdata(dev);
	struct stm32_pinctrl_group *g = pctl->groups;
	int i;

	for (i = 0; i < pctl->ngroups; i++, g++)
		stm32_pinctrl_restore_gpio_regs(pctl, g->pin);

	return 0;
}
