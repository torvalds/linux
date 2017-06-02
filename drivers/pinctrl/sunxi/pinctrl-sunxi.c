/*
 * Allwinner A1X SoCs pinctrl driver.
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/export.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/pinctrl/sun4i-a10.h>

#include "../core.h"
#include "pinctrl-sunxi.h"

static struct irq_chip sunxi_pinctrl_edge_irq_chip;
static struct irq_chip sunxi_pinctrl_level_irq_chip;

static struct sunxi_pinctrl_group *
sunxi_pinctrl_find_group_by_name(struct sunxi_pinctrl *pctl, const char *group)
{
	int i;

	for (i = 0; i < pctl->ngroups; i++) {
		struct sunxi_pinctrl_group *grp = pctl->groups + i;

		if (!strcmp(grp->name, group))
			return grp;
	}

	return NULL;
}

static struct sunxi_pinctrl_function *
sunxi_pinctrl_find_function_by_name(struct sunxi_pinctrl *pctl,
				    const char *name)
{
	struct sunxi_pinctrl_function *func = pctl->functions;
	int i;

	for (i = 0; i < pctl->nfunctions; i++) {
		if (!func[i].name)
			break;

		if (!strcmp(func[i].name, name))
			return func + i;
	}

	return NULL;
}

static struct sunxi_desc_function *
sunxi_pinctrl_desc_find_function_by_name(struct sunxi_pinctrl *pctl,
					 const char *pin_name,
					 const char *func_name)
{
	int i;

	for (i = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;

		if (!strcmp(pin->pin.name, pin_name)) {
			struct sunxi_desc_function *func = pin->functions;

			while (func->name) {
				if (!strcmp(func->name, func_name))
					return func;

				func++;
			}
		}
	}

	return NULL;
}

static struct sunxi_desc_function *
sunxi_pinctrl_desc_find_function_by_pin(struct sunxi_pinctrl *pctl,
					const u16 pin_num,
					const char *func_name)
{
	int i;

	for (i = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;

		if (pin->pin.number == pin_num) {
			struct sunxi_desc_function *func = pin->functions;

			while (func->name) {
				if (!strcmp(func->name, func_name))
					return func;

				func++;
			}
		}
	}

	return NULL;
}

static int sunxi_pctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *sunxi_pctrl_get_group_name(struct pinctrl_dev *pctldev,
					      unsigned group)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->groups[group].name;
}

static int sunxi_pctrl_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned group,
				      const unsigned **pins,
				      unsigned *num_pins)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = (unsigned *)&pctl->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static bool sunxi_pctrl_has_bias_prop(struct device_node *node)
{
	return of_find_property(node, "bias-pull-up", NULL) ||
		of_find_property(node, "bias-pull-down", NULL) ||
		of_find_property(node, "bias-disable", NULL) ||
		of_find_property(node, "allwinner,pull", NULL);
}

static bool sunxi_pctrl_has_drive_prop(struct device_node *node)
{
	return of_find_property(node, "drive-strength", NULL) ||
		of_find_property(node, "allwinner,drive", NULL);
}

static int sunxi_pctrl_parse_bias_prop(struct device_node *node)
{
	u32 val;

	/* Try the new style binding */
	if (of_find_property(node, "bias-pull-up", NULL))
		return PIN_CONFIG_BIAS_PULL_UP;

	if (of_find_property(node, "bias-pull-down", NULL))
		return PIN_CONFIG_BIAS_PULL_DOWN;

	if (of_find_property(node, "bias-disable", NULL))
		return PIN_CONFIG_BIAS_DISABLE;

	/* And fall back to the old binding */
	if (of_property_read_u32(node, "allwinner,pull", &val))
		return -EINVAL;

	switch (val) {
	case SUN4I_PINCTRL_NO_PULL:
		return PIN_CONFIG_BIAS_DISABLE;
	case SUN4I_PINCTRL_PULL_UP:
		return PIN_CONFIG_BIAS_PULL_UP;
	case SUN4I_PINCTRL_PULL_DOWN:
		return PIN_CONFIG_BIAS_PULL_DOWN;
	}

	return -EINVAL;
}

static int sunxi_pctrl_parse_drive_prop(struct device_node *node)
{
	u32 val;

	/* Try the new style binding */
	if (!of_property_read_u32(node, "drive-strength", &val)) {
		/* We can't go below 10mA ... */
		if (val < 10)
			return -EINVAL;

		/* ... and only up to 40 mA ... */
		if (val > 40)
			val = 40;

		/* by steps of 10 mA */
		return rounddown(val, 10);
	}

	/* And then fall back to the old binding */
	if (of_property_read_u32(node, "allwinner,drive", &val))
		return -EINVAL;

	return (val + 1) * 10;
}

static const char *sunxi_pctrl_parse_function_prop(struct device_node *node)
{
	const char *function;
	int ret;

	/* Try the generic binding */
	ret = of_property_read_string(node, "function", &function);
	if (!ret)
		return function;

	/* And fall back to our legacy one */
	ret = of_property_read_string(node, "allwinner,function", &function);
	if (!ret)
		return function;

	return NULL;
}

static const char *sunxi_pctrl_find_pins_prop(struct device_node *node,
					      int *npins)
{
	int count;

	/* Try the generic binding */
	count = of_property_count_strings(node, "pins");
	if (count > 0) {
		*npins = count;
		return "pins";
	}

	/* And fall back to our legacy one */
	count = of_property_count_strings(node, "allwinner,pins");
	if (count > 0) {
		*npins = count;
		return "allwinner,pins";
	}

	return NULL;
}

static unsigned long *sunxi_pctrl_build_pin_config(struct device_node *node,
						   unsigned int *len)
{
	unsigned long *pinconfig;
	unsigned int configlen = 0, idx = 0;
	int ret;

	if (sunxi_pctrl_has_drive_prop(node))
		configlen++;
	if (sunxi_pctrl_has_bias_prop(node))
		configlen++;

	/*
	 * If we don't have any configuration, bail out
	 */
	if (!configlen)
		return NULL;

	pinconfig = kzalloc(configlen * sizeof(*pinconfig), GFP_KERNEL);
	if (!pinconfig)
		return ERR_PTR(-ENOMEM);

	if (sunxi_pctrl_has_drive_prop(node)) {
		int drive = sunxi_pctrl_parse_drive_prop(node);
		if (drive < 0) {
			ret = drive;
			goto err_free;
		}

		pinconfig[idx++] = pinconf_to_config_packed(PIN_CONFIG_DRIVE_STRENGTH,
							  drive);
	}

	if (sunxi_pctrl_has_bias_prop(node)) {
		int pull = sunxi_pctrl_parse_bias_prop(node);
		int arg = 0;
		if (pull < 0) {
			ret = pull;
			goto err_free;
		}

		if (pull != PIN_CONFIG_BIAS_DISABLE)
			arg = 1; /* hardware uses weak pull resistors */

		pinconfig[idx++] = pinconf_to_config_packed(pull, arg);
	}


	*len = configlen;
	return pinconfig;

err_free:
	kfree(pinconfig);
	return ERR_PTR(ret);
}

static int sunxi_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				      struct device_node *node,
				      struct pinctrl_map **map,
				      unsigned *num_maps)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long *pinconfig;
	struct property *prop;
	const char *function, *pin_prop;
	const char *group;
	int ret, npins, nmaps, configlen = 0, i = 0;

	*map = NULL;
	*num_maps = 0;

	function = sunxi_pctrl_parse_function_prop(node);
	if (!function) {
		dev_err(pctl->dev, "missing function property in node %s\n",
			node->name);
		return -EINVAL;
	}

	pin_prop = sunxi_pctrl_find_pins_prop(node, &npins);
	if (!pin_prop) {
		dev_err(pctl->dev, "missing pins property in node %s\n",
			node->name);
		return -EINVAL;
	}

	/*
	 * We have two maps for each pin: one for the function, one
	 * for the configuration (bias, strength, etc).
	 *
	 * We might be slightly overshooting, since we might not have
	 * any configuration.
	 */
	nmaps = npins * 2;
	*map = kmalloc(nmaps * sizeof(struct pinctrl_map), GFP_KERNEL);
	if (!*map)
		return -ENOMEM;

	pinconfig = sunxi_pctrl_build_pin_config(node, &configlen);
	if (IS_ERR(pinconfig)) {
		ret = PTR_ERR(pinconfig);
		goto err_free_map;
	}

	of_property_for_each_string(node, pin_prop, prop, group) {
		struct sunxi_pinctrl_group *grp =
			sunxi_pinctrl_find_group_by_name(pctl, group);

		if (!grp) {
			dev_err(pctl->dev, "unknown pin %s", group);
			continue;
		}

		if (!sunxi_pinctrl_desc_find_function_by_name(pctl,
							      grp->name,
							      function)) {
			dev_err(pctl->dev, "unsupported function %s on pin %s",
				function, group);
			continue;
		}

		(*map)[i].type = PIN_MAP_TYPE_MUX_GROUP;
		(*map)[i].data.mux.group = group;
		(*map)[i].data.mux.function = function;

		i++;

		if (pinconfig) {
			(*map)[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
			(*map)[i].data.configs.group_or_pin = group;
			(*map)[i].data.configs.configs = pinconfig;
			(*map)[i].data.configs.num_configs = configlen;
			i++;
		}
	}

	*num_maps = i;

	/*
	 * We know have the number of maps we need, we can resize our
	 * map array
	 */
	*map = krealloc(*map, i * sizeof(struct pinctrl_map), GFP_KERNEL);
	if (!*map)
		return -ENOMEM;

	return 0;

err_free_map:
	kfree(*map);
	*map = NULL;
	return ret;
}

static void sunxi_pctrl_dt_free_map(struct pinctrl_dev *pctldev,
				    struct pinctrl_map *map,
				    unsigned num_maps)
{
	int i;

	/* pin config is never in the first map */
	for (i = 1; i < num_maps; i++) {
		if (map[i].type != PIN_MAP_TYPE_CONFIGS_GROUP)
			continue;

		/*
		 * All the maps share the same pin config,
		 * free only the first one we find.
		 */
		kfree(map[i].data.configs.configs);
		break;
	}

	kfree(map);
}

static const struct pinctrl_ops sunxi_pctrl_ops = {
	.dt_node_to_map		= sunxi_pctrl_dt_node_to_map,
	.dt_free_map		= sunxi_pctrl_dt_free_map,
	.get_groups_count	= sunxi_pctrl_get_groups_count,
	.get_group_name		= sunxi_pctrl_get_group_name,
	.get_group_pins		= sunxi_pctrl_get_group_pins,
};

static int sunxi_pconf_reg(unsigned pin, enum pin_config_param param,
			   u32 *offset, u32 *shift, u32 *mask)
{
	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		*offset = sunxi_dlevel_reg(pin);
		*shift = sunxi_dlevel_offset(pin);
		*mask = DLEVEL_PINS_MASK;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_DISABLE:
		*offset = sunxi_pull_reg(pin);
		*shift = sunxi_pull_offset(pin);
		*mask = PULL_PINS_MASK;
		break;

	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int sunxi_pconf_get(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *config)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 offset, shift, mask, val;
	u16 arg;
	int ret;

	pin -= pctl->desc->pin_base;

	ret = sunxi_pconf_reg(pin, param, &offset, &shift, &mask);
	if (ret < 0)
		return ret;

	val = (readl(pctl->membase + offset) >> shift) & mask;

	switch (pinconf_to_config_param(*config)) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = (val + 1) * 10;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		if (val != SUN4I_PINCTRL_PULL_UP)
			return -EINVAL;
		arg = 1; /* hardware is weak pull-up */
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (val != SUN4I_PINCTRL_PULL_DOWN)
			return -EINVAL;
		arg = 1; /* hardware is weak pull-down */
		break;

	case PIN_CONFIG_BIAS_DISABLE:
		if (val != SUN4I_PINCTRL_NO_PULL)
			return -EINVAL;
		arg = 0;
		break;

	default:
		/* sunxi_pconf_reg should catch anything unsupported */
		WARN_ON(1);
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int sunxi_pconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned group,
				 unsigned long *config)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sunxi_pinctrl_group *g = &pctl->groups[group];

	/* We only support 1 pin per group. Chain it to the pin callback */
	return sunxi_pconf_get(pctldev, g->pin, config);
}

static int sunxi_pconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned group,
				 unsigned long *configs,
				 unsigned num_configs)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sunxi_pinctrl_group *g = &pctl->groups[group];
	unsigned pin = g->pin - pctl->desc->pin_base;
	int i;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param;
		unsigned long flags;
		u32 offset, shift, mask, reg;
		u32 arg, val;
		int ret;

		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		ret = sunxi_pconf_reg(pin, param, &offset, &shift, &mask);
		if (ret < 0)
			return ret;

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (arg < 10 || arg > 40)
				return -EINVAL;
			/*
			 * We convert from mA to what the register expects:
			 *   0: 10mA
			 *   1: 20mA
			 *   2: 30mA
			 *   3: 40mA
			 */
			val = arg / 10 - 1;
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			continue;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -EINVAL;
			val = 1;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -EINVAL;
			val = 2;
			break;
		default:
			/* sunxi_pconf_reg should catch anything unsupported */
			WARN_ON(1);
			return -ENOTSUPP;
		}

		raw_spin_lock_irqsave(&pctl->lock, flags);
		reg = readl(pctl->membase + offset);
		reg &= ~(mask << shift);
		writel(reg | val << shift, pctl->membase + offset);
		raw_spin_unlock_irqrestore(&pctl->lock, flags);
	} /* for each config */

	return 0;
}

static const struct pinconf_ops sunxi_pconf_ops = {
	.is_generic		= true,
	.pin_config_get		= sunxi_pconf_get,
	.pin_config_group_get	= sunxi_pconf_group_get,
	.pin_config_group_set	= sunxi_pconf_group_set,
};

static int sunxi_pmx_get_funcs_cnt(struct pinctrl_dev *pctldev)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->nfunctions;
}

static const char *sunxi_pmx_get_func_name(struct pinctrl_dev *pctldev,
					   unsigned function)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->functions[function].name;
}

static int sunxi_pmx_get_func_groups(struct pinctrl_dev *pctldev,
				     unsigned function,
				     const char * const **groups,
				     unsigned * const num_groups)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->functions[function].groups;
	*num_groups = pctl->functions[function].ngroups;

	return 0;
}

static void sunxi_pmx_set(struct pinctrl_dev *pctldev,
				 unsigned pin,
				 u8 config)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;
	u32 val, mask;

	raw_spin_lock_irqsave(&pctl->lock, flags);

	pin -= pctl->desc->pin_base;
	val = readl(pctl->membase + sunxi_mux_reg(pin));
	mask = MUX_PINS_MASK << sunxi_mux_offset(pin);
	writel((val & ~mask) | config << sunxi_mux_offset(pin),
		pctl->membase + sunxi_mux_reg(pin));

	raw_spin_unlock_irqrestore(&pctl->lock, flags);
}

static int sunxi_pmx_set_mux(struct pinctrl_dev *pctldev,
			     unsigned function,
			     unsigned group)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sunxi_pinctrl_group *g = pctl->groups + group;
	struct sunxi_pinctrl_function *func = pctl->functions + function;
	struct sunxi_desc_function *desc =
		sunxi_pinctrl_desc_find_function_by_name(pctl,
							 g->name,
							 func->name);

	if (!desc)
		return -EINVAL;

	sunxi_pmx_set(pctldev, g->pin, desc->muxval);

	return 0;
}

static int
sunxi_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned offset,
			bool input)
{
	struct sunxi_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sunxi_desc_function *desc;
	const char *func;

	if (input)
		func = "gpio_in";
	else
		func = "gpio_out";

	desc = sunxi_pinctrl_desc_find_function_by_pin(pctl, offset, func);
	if (!desc)
		return -EINVAL;

	sunxi_pmx_set(pctldev, offset, desc->muxval);

	return 0;
}

static const struct pinmux_ops sunxi_pmx_ops = {
	.get_functions_count	= sunxi_pmx_get_funcs_cnt,
	.get_function_name	= sunxi_pmx_get_func_name,
	.get_function_groups	= sunxi_pmx_get_func_groups,
	.set_mux		= sunxi_pmx_set_mux,
	.gpio_set_direction	= sunxi_pmx_gpio_set_direction,
};

static int sunxi_pinctrl_gpio_direction_input(struct gpio_chip *chip,
					unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int sunxi_pinctrl_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct sunxi_pinctrl *pctl = gpiochip_get_data(chip);
	u32 reg = sunxi_data_reg(offset);
	u8 index = sunxi_data_offset(offset);
	bool set_mux = pctl->desc->irq_read_needs_mux &&
		gpiochip_line_is_irq(chip, offset);
	u32 pin = offset + chip->base;
	u32 val;

	if (set_mux)
		sunxi_pmx_set(pctl->pctl_dev, pin, SUN4I_FUNC_INPUT);

	val = (readl(pctl->membase + reg) >> index) & DATA_PINS_MASK;

	if (set_mux)
		sunxi_pmx_set(pctl->pctl_dev, pin, SUN4I_FUNC_IRQ);

	return !!val;
}

static void sunxi_pinctrl_gpio_set(struct gpio_chip *chip,
				unsigned offset, int value)
{
	struct sunxi_pinctrl *pctl = gpiochip_get_data(chip);
	u32 reg = sunxi_data_reg(offset);
	u8 index = sunxi_data_offset(offset);
	unsigned long flags;
	u32 regval;

	raw_spin_lock_irqsave(&pctl->lock, flags);

	regval = readl(pctl->membase + reg);

	if (value)
		regval |= BIT(index);
	else
		regval &= ~(BIT(index));

	writel(regval, pctl->membase + reg);

	raw_spin_unlock_irqrestore(&pctl->lock, flags);
}

static int sunxi_pinctrl_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset, int value)
{
	sunxi_pinctrl_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static int sunxi_pinctrl_gpio_of_xlate(struct gpio_chip *gc,
				const struct of_phandle_args *gpiospec,
				u32 *flags)
{
	int pin, base;

	base = PINS_PER_BANK * gpiospec->args[0];
	pin = base + gpiospec->args[1];

	if (pin > gc->ngpio)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[2];

	return pin;
}

static int sunxi_pinctrl_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct sunxi_pinctrl *pctl = gpiochip_get_data(chip);
	struct sunxi_desc_function *desc;
	unsigned pinnum = pctl->desc->pin_base + offset;
	unsigned irqnum;

	if (offset >= chip->ngpio)
		return -ENXIO;

	desc = sunxi_pinctrl_desc_find_function_by_pin(pctl, pinnum, "irq");
	if (!desc)
		return -EINVAL;

	irqnum = desc->irqbank * IRQ_PER_BANK + desc->irqnum;

	dev_dbg(chip->parent, "%s: request IRQ for GPIO %d, return %d\n",
		chip->label, offset + chip->base, irqnum);

	return irq_find_mapping(pctl->domain, irqnum);
}

static int sunxi_pinctrl_irq_request_resources(struct irq_data *d)
{
	struct sunxi_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	struct sunxi_desc_function *func;
	int ret;

	func = sunxi_pinctrl_desc_find_function_by_pin(pctl,
					pctl->irq_array[d->hwirq], "irq");
	if (!func)
		return -EINVAL;

	ret = gpiochip_lock_as_irq(pctl->chip,
			pctl->irq_array[d->hwirq] - pctl->desc->pin_base);
	if (ret) {
		dev_err(pctl->dev, "unable to lock HW IRQ %lu for IRQ\n",
			irqd_to_hwirq(d));
		return ret;
	}

	/* Change muxing to INT mode */
	sunxi_pmx_set(pctl->pctl_dev, pctl->irq_array[d->hwirq], func->muxval);

	return 0;
}

static void sunxi_pinctrl_irq_release_resources(struct irq_data *d)
{
	struct sunxi_pinctrl *pctl = irq_data_get_irq_chip_data(d);

	gpiochip_unlock_as_irq(pctl->chip,
			      pctl->irq_array[d->hwirq] - pctl->desc->pin_base);
}

static int sunxi_pinctrl_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct sunxi_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	u32 reg = sunxi_irq_cfg_reg(d->hwirq, pctl->desc->irq_bank_base);
	u8 index = sunxi_irq_cfg_offset(d->hwirq);
	unsigned long flags;
	u32 regval;
	u8 mode;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		mode = IRQ_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		mode = IRQ_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		mode = IRQ_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		mode = IRQ_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		mode = IRQ_LEVEL_LOW;
		break;
	default:
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&pctl->lock, flags);

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_chip_handler_name_locked(d, &sunxi_pinctrl_level_irq_chip,
						 handle_fasteoi_irq, NULL);
	else
		irq_set_chip_handler_name_locked(d, &sunxi_pinctrl_edge_irq_chip,
						 handle_edge_irq, NULL);

	regval = readl(pctl->membase + reg);
	regval &= ~(IRQ_CFG_IRQ_MASK << index);
	writel(regval | (mode << index), pctl->membase + reg);

	raw_spin_unlock_irqrestore(&pctl->lock, flags);

	return 0;
}

static void sunxi_pinctrl_irq_ack(struct irq_data *d)
{
	struct sunxi_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	u32 status_reg = sunxi_irq_status_reg(d->hwirq,
					      pctl->desc->irq_bank_base);
	u8 status_idx = sunxi_irq_status_offset(d->hwirq);

	/* Clear the IRQ */
	writel(1 << status_idx, pctl->membase + status_reg);
}

static void sunxi_pinctrl_irq_mask(struct irq_data *d)
{
	struct sunxi_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	u32 reg = sunxi_irq_ctrl_reg(d->hwirq, pctl->desc->irq_bank_base);
	u8 idx = sunxi_irq_ctrl_offset(d->hwirq);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pctl->lock, flags);

	/* Mask the IRQ */
	val = readl(pctl->membase + reg);
	writel(val & ~(1 << idx), pctl->membase + reg);

	raw_spin_unlock_irqrestore(&pctl->lock, flags);
}

static void sunxi_pinctrl_irq_unmask(struct irq_data *d)
{
	struct sunxi_pinctrl *pctl = irq_data_get_irq_chip_data(d);
	u32 reg = sunxi_irq_ctrl_reg(d->hwirq, pctl->desc->irq_bank_base);
	u8 idx = sunxi_irq_ctrl_offset(d->hwirq);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&pctl->lock, flags);

	/* Unmask the IRQ */
	val = readl(pctl->membase + reg);
	writel(val | (1 << idx), pctl->membase + reg);

	raw_spin_unlock_irqrestore(&pctl->lock, flags);
}

static void sunxi_pinctrl_irq_ack_unmask(struct irq_data *d)
{
	sunxi_pinctrl_irq_ack(d);
	sunxi_pinctrl_irq_unmask(d);
}

static struct irq_chip sunxi_pinctrl_edge_irq_chip = {
	.name		= "sunxi_pio_edge",
	.irq_ack	= sunxi_pinctrl_irq_ack,
	.irq_mask	= sunxi_pinctrl_irq_mask,
	.irq_unmask	= sunxi_pinctrl_irq_unmask,
	.irq_request_resources = sunxi_pinctrl_irq_request_resources,
	.irq_release_resources = sunxi_pinctrl_irq_release_resources,
	.irq_set_type	= sunxi_pinctrl_irq_set_type,
	.flags		= IRQCHIP_SKIP_SET_WAKE,
};

static struct irq_chip sunxi_pinctrl_level_irq_chip = {
	.name		= "sunxi_pio_level",
	.irq_eoi	= sunxi_pinctrl_irq_ack,
	.irq_mask	= sunxi_pinctrl_irq_mask,
	.irq_unmask	= sunxi_pinctrl_irq_unmask,
	/* Define irq_enable / disable to avoid spurious irqs for drivers
	 * using these to suppress irqs while they clear the irq source */
	.irq_enable	= sunxi_pinctrl_irq_ack_unmask,
	.irq_disable	= sunxi_pinctrl_irq_mask,
	.irq_request_resources = sunxi_pinctrl_irq_request_resources,
	.irq_release_resources = sunxi_pinctrl_irq_release_resources,
	.irq_set_type	= sunxi_pinctrl_irq_set_type,
	.flags		= IRQCHIP_SKIP_SET_WAKE | IRQCHIP_EOI_THREADED |
			  IRQCHIP_EOI_IF_HANDLED,
};

static int sunxi_pinctrl_irq_of_xlate(struct irq_domain *d,
				      struct device_node *node,
				      const u32 *intspec,
				      unsigned int intsize,
				      unsigned long *out_hwirq,
				      unsigned int *out_type)
{
	struct sunxi_pinctrl *pctl = d->host_data;
	struct sunxi_desc_function *desc;
	int pin, base;

	if (intsize < 3)
		return -EINVAL;

	base = PINS_PER_BANK * intspec[0];
	pin = pctl->desc->pin_base + base + intspec[1];

	desc = sunxi_pinctrl_desc_find_function_by_pin(pctl, pin, "irq");
	if (!desc)
		return -EINVAL;

	*out_hwirq = desc->irqbank * PINS_PER_BANK + desc->irqnum;
	*out_type = intspec[2];

	return 0;
}

static const struct irq_domain_ops sunxi_pinctrl_irq_domain_ops = {
	.xlate		= sunxi_pinctrl_irq_of_xlate,
};

static void sunxi_pinctrl_irq_handler(struct irq_desc *desc)
{
	unsigned int irq = irq_desc_get_irq(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct sunxi_pinctrl *pctl = irq_desc_get_handler_data(desc);
	unsigned long bank, reg, val;

	for (bank = 0; bank < pctl->desc->irq_banks; bank++)
		if (irq == pctl->irq[bank])
			break;

	if (bank == pctl->desc->irq_banks)
		return;

	reg = sunxi_irq_status_reg_from_bank(bank, pctl->desc->irq_bank_base);
	val = readl(pctl->membase + reg);

	if (val) {
		int irqoffset;

		chained_irq_enter(chip, desc);
		for_each_set_bit(irqoffset, &val, IRQ_PER_BANK) {
			int pin_irq = irq_find_mapping(pctl->domain,
						       bank * IRQ_PER_BANK + irqoffset);
			generic_handle_irq(pin_irq);
		}
		chained_irq_exit(chip, desc);
	}
}

static int sunxi_pinctrl_add_function(struct sunxi_pinctrl *pctl,
					const char *name)
{
	struct sunxi_pinctrl_function *func = pctl->functions;

	while (func->name) {
		/* function already there */
		if (strcmp(func->name, name) == 0) {
			func->ngroups++;
			return -EEXIST;
		}
		func++;
	}

	func->name = name;
	func->ngroups = 1;

	pctl->nfunctions++;

	return 0;
}

static int sunxi_pinctrl_build_state(struct platform_device *pdev)
{
	struct sunxi_pinctrl *pctl = platform_get_drvdata(pdev);
	int i;

	/*
	 * Allocate groups
	 *
	 * We assume that the number of groups is the number of pins
	 * given in the data array.

	 * This will not always be true, since some pins might not be
	 * available in the current variant, but fortunately for us,
	 * this means that the number of pins is the maximum group
	 * number we will ever see.
	 */
	pctl->groups = devm_kzalloc(&pdev->dev,
				    pctl->desc->npins * sizeof(*pctl->groups),
				    GFP_KERNEL);
	if (!pctl->groups)
		return -ENOMEM;

	for (i = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;
		struct sunxi_pinctrl_group *group = pctl->groups + pctl->ngroups;

		if (pin->variant && !(pctl->variant & pin->variant))
			continue;

		group->name = pin->pin.name;
		group->pin = pin->pin.number;

		/* And now we count the actual number of pins / groups */
		pctl->ngroups++;
	}

	/*
	 * We suppose that we won't have any more functions than pins,
	 * we'll reallocate that later anyway
	 */
	pctl->functions = devm_kzalloc(&pdev->dev,
				       pctl->ngroups * sizeof(*pctl->functions),
				       GFP_KERNEL);
	if (!pctl->functions)
		return -ENOMEM;

	/* Count functions and their associated groups */
	for (i = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;
		struct sunxi_desc_function *func;

		if (pin->variant && !(pctl->variant & pin->variant))
			continue;

		for (func = pin->functions; func->name; func++) {
			if (func->variant && !(pctl->variant & func->variant))
				continue;

			/* Create interrupt mapping while we're at it */
			if (!strcmp(func->name, "irq")) {
				int irqnum = func->irqnum + func->irqbank * IRQ_PER_BANK;
				pctl->irq_array[irqnum] = pin->pin.number;
			}

			sunxi_pinctrl_add_function(pctl, func->name);
		}
	}

	/* And now allocated and fill the array for real */
	pctl->functions = krealloc(pctl->functions,
				   pctl->nfunctions * sizeof(*pctl->functions),
				   GFP_KERNEL);
	if (!pctl->functions) {
		kfree(pctl->functions);
		return -ENOMEM;
	}

	for (i = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;
		struct sunxi_desc_function *func;

		if (pin->variant && !(pctl->variant & pin->variant))
			continue;

		for (func = pin->functions; func->name; func++) {
			struct sunxi_pinctrl_function *func_item;
			const char **func_grp;

			if (func->variant && !(pctl->variant & func->variant))
				continue;

			func_item = sunxi_pinctrl_find_function_by_name(pctl,
									func->name);
			if (!func_item)
				return -EINVAL;

			if (!func_item->groups) {
				func_item->groups =
					devm_kzalloc(&pdev->dev,
						     func_item->ngroups * sizeof(*func_item->groups),
						     GFP_KERNEL);
				if (!func_item->groups)
					return -ENOMEM;
			}

			func_grp = func_item->groups;
			while (*func_grp)
				func_grp++;

			*func_grp = pin->pin.name;
		}
	}

	return 0;
}

static int sunxi_pinctrl_get_debounce_div(struct clk *clk, int freq, int *diff)
{
	unsigned long clock = clk_get_rate(clk);
	unsigned int best_diff, best_div;
	int i;

	best_diff = abs(freq - clock);
	best_div = 0;

	for (i = 1; i < 8; i++) {
		int cur_diff = abs(freq - (clock >> i));

		if (cur_diff < best_diff) {
			best_diff = cur_diff;
			best_div = i;
		}
	}

	*diff = best_diff;
	return best_div;
}

static int sunxi_pinctrl_setup_debounce(struct sunxi_pinctrl *pctl,
					struct device_node *node)
{
	unsigned int hosc_diff, losc_diff;
	unsigned int hosc_div, losc_div;
	struct clk *hosc, *losc;
	u8 div, src;
	int i, ret;

	/* Deal with old DTs that didn't have the oscillators */
	if (of_count_phandle_with_args(node, "clocks", "#clock-cells") != 3)
		return 0;

	/* If we don't have any setup, bail out */
	if (!of_find_property(node, "input-debounce", NULL))
		return 0;

	losc = devm_clk_get(pctl->dev, "losc");
	if (IS_ERR(losc))
		return PTR_ERR(losc);

	hosc = devm_clk_get(pctl->dev, "hosc");
	if (IS_ERR(hosc))
		return PTR_ERR(hosc);

	for (i = 0; i < pctl->desc->irq_banks; i++) {
		unsigned long debounce_freq;
		u32 debounce;

		ret = of_property_read_u32_index(node, "input-debounce",
						 i, &debounce);
		if (ret)
			return ret;

		if (!debounce)
			continue;

		debounce_freq = DIV_ROUND_CLOSEST(USEC_PER_SEC, debounce);
		losc_div = sunxi_pinctrl_get_debounce_div(losc,
							  debounce_freq,
							  &losc_diff);

		hosc_div = sunxi_pinctrl_get_debounce_div(hosc,
							  debounce_freq,
							  &hosc_diff);

		if (hosc_diff < losc_diff) {
			div = hosc_div;
			src = 1;
		} else {
			div = losc_div;
			src = 0;
		}

		writel(src | div << 4,
		       pctl->membase +
		       sunxi_irq_debounce_reg_from_bank(i,
							pctl->desc->irq_bank_base));
	}

	return 0;
}

int sunxi_pinctrl_init_with_variant(struct platform_device *pdev,
				    const struct sunxi_pinctrl_desc *desc,
				    unsigned long variant)
{
	struct device_node *node = pdev->dev.of_node;
	struct pinctrl_desc *pctrl_desc;
	struct pinctrl_pin_desc *pins;
	struct sunxi_pinctrl *pctl;
	struct resource *res;
	int i, ret, last_pin, pin_idx;
	struct clk *clk;

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;
	platform_set_drvdata(pdev, pctl);

	raw_spin_lock_init(&pctl->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pctl->membase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctl->membase))
		return PTR_ERR(pctl->membase);

	pctl->dev = &pdev->dev;
	pctl->desc = desc;
	pctl->variant = variant;

	pctl->irq_array = devm_kcalloc(&pdev->dev,
				       IRQ_PER_BANK * pctl->desc->irq_banks,
				       sizeof(*pctl->irq_array),
				       GFP_KERNEL);
	if (!pctl->irq_array)
		return -ENOMEM;

	ret = sunxi_pinctrl_build_state(pdev);
	if (ret) {
		dev_err(&pdev->dev, "dt probe failed: %d\n", ret);
		return ret;
	}

	pins = devm_kzalloc(&pdev->dev,
			    pctl->desc->npins * sizeof(*pins),
			    GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0, pin_idx = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;

		if (pin->variant && !(pctl->variant & pin->variant))
			continue;

		pins[pin_idx++] = pin->pin;
	}

	pctrl_desc = devm_kzalloc(&pdev->dev,
				  sizeof(*pctrl_desc),
				  GFP_KERNEL);
	if (!pctrl_desc)
		return -ENOMEM;

	pctrl_desc->name = dev_name(&pdev->dev);
	pctrl_desc->owner = THIS_MODULE;
	pctrl_desc->pins = pins;
	pctrl_desc->npins = pctl->ngroups;
	pctrl_desc->confops = &sunxi_pconf_ops;
	pctrl_desc->pctlops = &sunxi_pctrl_ops;
	pctrl_desc->pmxops =  &sunxi_pmx_ops;

	pctl->pctl_dev = devm_pinctrl_register(&pdev->dev, pctrl_desc, pctl);
	if (IS_ERR(pctl->pctl_dev)) {
		dev_err(&pdev->dev, "couldn't register pinctrl driver\n");
		return PTR_ERR(pctl->pctl_dev);
	}

	pctl->chip = devm_kzalloc(&pdev->dev, sizeof(*pctl->chip), GFP_KERNEL);
	if (!pctl->chip)
		return -ENOMEM;

	last_pin = pctl->desc->pins[pctl->desc->npins - 1].pin.number;
	pctl->chip->owner = THIS_MODULE;
	pctl->chip->request = gpiochip_generic_request,
	pctl->chip->free = gpiochip_generic_free,
	pctl->chip->direction_input = sunxi_pinctrl_gpio_direction_input,
	pctl->chip->direction_output = sunxi_pinctrl_gpio_direction_output,
	pctl->chip->get = sunxi_pinctrl_gpio_get,
	pctl->chip->set = sunxi_pinctrl_gpio_set,
	pctl->chip->of_xlate = sunxi_pinctrl_gpio_of_xlate,
	pctl->chip->to_irq = sunxi_pinctrl_gpio_to_irq,
	pctl->chip->of_gpio_n_cells = 3,
	pctl->chip->can_sleep = false,
	pctl->chip->ngpio = round_up(last_pin, PINS_PER_BANK) -
			    pctl->desc->pin_base;
	pctl->chip->label = dev_name(&pdev->dev);
	pctl->chip->parent = &pdev->dev;
	pctl->chip->base = pctl->desc->pin_base;

	ret = gpiochip_add_data(pctl->chip, pctl);
	if (ret)
		return ret;

	for (i = 0; i < pctl->desc->npins; i++) {
		const struct sunxi_desc_pin *pin = pctl->desc->pins + i;

		ret = gpiochip_add_pin_range(pctl->chip, dev_name(&pdev->dev),
					     pin->pin.number - pctl->desc->pin_base,
					     pin->pin.number, 1);
		if (ret)
			goto gpiochip_error;
	}

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto gpiochip_error;
	}

	ret = clk_prepare_enable(clk);
	if (ret)
		goto gpiochip_error;

	pctl->irq = devm_kcalloc(&pdev->dev,
				 pctl->desc->irq_banks,
				 sizeof(*pctl->irq),
				 GFP_KERNEL);
	if (!pctl->irq) {
		ret = -ENOMEM;
		goto clk_error;
	}

	for (i = 0; i < pctl->desc->irq_banks; i++) {
		pctl->irq[i] = platform_get_irq(pdev, i);
		if (pctl->irq[i] < 0) {
			ret = pctl->irq[i];
			goto clk_error;
		}
	}

	pctl->domain = irq_domain_add_linear(node,
					     pctl->desc->irq_banks * IRQ_PER_BANK,
					     &sunxi_pinctrl_irq_domain_ops,
					     pctl);
	if (!pctl->domain) {
		dev_err(&pdev->dev, "Couldn't register IRQ domain\n");
		ret = -ENOMEM;
		goto clk_error;
	}

	for (i = 0; i < (pctl->desc->irq_banks * IRQ_PER_BANK); i++) {
		int irqno = irq_create_mapping(pctl->domain, i);

		irq_set_chip_and_handler(irqno, &sunxi_pinctrl_edge_irq_chip,
					 handle_edge_irq);
		irq_set_chip_data(irqno, pctl);
	}

	for (i = 0; i < pctl->desc->irq_banks; i++) {
		/* Mask and clear all IRQs before registering a handler */
		writel(0, pctl->membase + sunxi_irq_ctrl_reg_from_bank(i,
						pctl->desc->irq_bank_base));
		writel(0xffffffff,
		       pctl->membase + sunxi_irq_status_reg_from_bank(i,
						pctl->desc->irq_bank_base));

		irq_set_chained_handler_and_data(pctl->irq[i],
						 sunxi_pinctrl_irq_handler,
						 pctl);
	}

	sunxi_pinctrl_setup_debounce(pctl, node);

	dev_info(&pdev->dev, "initialized sunXi PIO driver\n");

	return 0;

clk_error:
	clk_disable_unprepare(clk);
gpiochip_error:
	gpiochip_remove(pctl->chip);
	return ret;
}
