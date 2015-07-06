/*
 * pin-controller/pin-mux/pin-config/gpio-driver for Samsung's EXYNOS5440 SoC.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include "../core.h"

/* EXYNOS5440 GPIO and Pinctrl register offsets */
#define GPIO_MUX		0x00
#define GPIO_IE			0x04
#define GPIO_INT		0x08
#define GPIO_TYPE		0x0C
#define GPIO_VAL		0x10
#define GPIO_OE			0x14
#define GPIO_IN			0x18
#define GPIO_PE			0x1C
#define GPIO_PS			0x20
#define GPIO_SR			0x24
#define GPIO_DS0		0x28
#define GPIO_DS1		0x2C

#define EXYNOS5440_MAX_PINS		23
#define EXYNOS5440_MAX_GPIO_INT	8
#define PIN_NAME_LENGTH		10

#define GROUP_SUFFIX		"-grp"
#define GSUFFIX_LEN		sizeof(GROUP_SUFFIX)
#define FUNCTION_SUFFIX		"-mux"
#define FSUFFIX_LEN		sizeof(FUNCTION_SUFFIX)

/*
 * pin configuration type and its value are packed together into a 16-bits.
 * The upper 8-bits represent the configuration type and the lower 8-bits
 * hold the value of the configuration type.
 */
#define PINCFG_TYPE_MASK		0xFF
#define PINCFG_VALUE_SHIFT		8
#define PINCFG_VALUE_MASK		(0xFF << PINCFG_VALUE_SHIFT)
#define PINCFG_PACK(type, value)	(((value) << PINCFG_VALUE_SHIFT) | type)
#define PINCFG_UNPACK_TYPE(cfg)		((cfg) & PINCFG_TYPE_MASK)
#define PINCFG_UNPACK_VALUE(cfg)	(((cfg) & PINCFG_VALUE_MASK) >> \
						PINCFG_VALUE_SHIFT)

/**
 * enum pincfg_type - possible pin configuration types supported.
 * @PINCFG_TYPE_PUD: Pull up/down configuration.
 * @PINCFG_TYPE_DRV: Drive strength configuration.
 * @PINCFG_TYPE_SKEW_RATE: Skew rate configuration.
 * @PINCFG_TYPE_INPUT_TYPE: Pin input type configuration.
 */
enum pincfg_type {
	PINCFG_TYPE_PUD,
	PINCFG_TYPE_DRV,
	PINCFG_TYPE_SKEW_RATE,
	PINCFG_TYPE_INPUT_TYPE
};

/**
 * struct exynos5440_pin_group: represent group of pins for pincfg setting.
 * @name: name of the pin group, used to lookup the group.
 * @pins: the pins included in this group.
 * @num_pins: number of pins included in this group.
 */
struct exynos5440_pin_group {
	const char		*name;
	const unsigned int	*pins;
	u8			num_pins;
};

/**
 * struct exynos5440_pmx_func: represent a pin function.
 * @name: name of the pin function, used to lookup the function.
 * @groups: one or more names of pin groups that provide this function.
 * @num_groups: number of groups included in @groups.
 * @function: the function number to be programmed when selected.
 */
struct exynos5440_pmx_func {
	const char		*name;
	const char		**groups;
	u8			num_groups;
	unsigned long		function;
};

/**
 * struct exynos5440_pinctrl_priv_data: driver's private runtime data.
 * @reg_base: ioremapped based address of the register space.
 * @gc: gpio chip registered with gpiolib.
 * @pin_groups: list of pin groups parsed from device tree.
 * @nr_groups: number of pin groups available.
 * @pmx_functions: list of pin functions parsed from device tree.
 * @nr_functions: number of pin functions available.
 */
struct exynos5440_pinctrl_priv_data {
	void __iomem			*reg_base;
	struct gpio_chip		*gc;
	struct irq_domain		*irq_domain;

	const struct exynos5440_pin_group	*pin_groups;
	unsigned int			nr_groups;
	const struct exynos5440_pmx_func	*pmx_functions;
	unsigned int			nr_functions;
};

/**
 * struct exynos5440_gpio_intr_data: private data for gpio interrupts.
 * @priv: driver's private runtime data.
 * @gpio_int: gpio interrupt number.
 */
struct exynos5440_gpio_intr_data {
	struct exynos5440_pinctrl_priv_data	*priv;
	unsigned int				gpio_int;
};

/* list of all possible config options supported */
static struct pin_config {
	char		*prop_cfg;
	unsigned int	cfg_type;
} pcfgs[] = {
	{ "samsung,exynos5440-pin-pud", PINCFG_TYPE_PUD },
	{ "samsung,exynos5440-pin-drv", PINCFG_TYPE_DRV },
	{ "samsung,exynos5440-pin-skew-rate", PINCFG_TYPE_SKEW_RATE },
	{ "samsung,exynos5440-pin-input-type", PINCFG_TYPE_INPUT_TYPE },
};

/* check if the selector is a valid pin group selector */
static int exynos5440_get_group_count(struct pinctrl_dev *pctldev)
{
	struct exynos5440_pinctrl_priv_data *priv;

	priv = pinctrl_dev_get_drvdata(pctldev);
	return priv->nr_groups;
}

/* return the name of the group selected by the group selector */
static const char *exynos5440_get_group_name(struct pinctrl_dev *pctldev,
						unsigned selector)
{
	struct exynos5440_pinctrl_priv_data *priv;

	priv = pinctrl_dev_get_drvdata(pctldev);
	return priv->pin_groups[selector].name;
}

/* return the pin numbers associated with the specified group */
static int exynos5440_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned selector, const unsigned **pins, unsigned *num_pins)
{
	struct exynos5440_pinctrl_priv_data *priv;

	priv = pinctrl_dev_get_drvdata(pctldev);
	*pins = priv->pin_groups[selector].pins;
	*num_pins = priv->pin_groups[selector].num_pins;
	return 0;
}

/* create pinctrl_map entries by parsing device tree nodes */
static int exynos5440_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np, struct pinctrl_map **maps,
			unsigned *nmaps)
{
	struct device *dev = pctldev->dev;
	struct pinctrl_map *map;
	unsigned long *cfg = NULL;
	char *gname, *fname;
	int cfg_cnt = 0, map_cnt = 0, idx = 0;

	/* count the number of config options specfied in the node */
	for (idx = 0; idx < ARRAY_SIZE(pcfgs); idx++)
		if (of_find_property(np, pcfgs[idx].prop_cfg, NULL))
			cfg_cnt++;

	/*
	 * Find out the number of map entries to create. All the config options
	 * can be accomadated into a single config map entry.
	 */
	if (cfg_cnt)
		map_cnt = 1;
	if (of_find_property(np, "samsung,exynos5440-pin-function", NULL))
		map_cnt++;
	if (!map_cnt) {
		dev_err(dev, "node %s does not have either config or function "
				"configurations\n", np->name);
		return -EINVAL;
	}

	/* Allocate memory for pin-map entries */
	map = kzalloc(sizeof(*map) * map_cnt, GFP_KERNEL);
	if (!map) {
		dev_err(dev, "could not alloc memory for pin-maps\n");
		return -ENOMEM;
	}
	*nmaps = 0;

	/*
	 * Allocate memory for pin group name. The pin group name is derived
	 * from the node name from which these map entries are be created.
	 */
	gname = kzalloc(strlen(np->name) + GSUFFIX_LEN, GFP_KERNEL);
	if (!gname) {
		dev_err(dev, "failed to alloc memory for group name\n");
		goto free_map;
	}
	snprintf(gname, strlen(np->name) + 4, "%s%s", np->name, GROUP_SUFFIX);

	/*
	 * don't have config options? then skip over to creating function
	 * map entries.
	 */
	if (!cfg_cnt)
		goto skip_cfgs;

	/* Allocate memory for config entries */
	cfg = kzalloc(sizeof(*cfg) * cfg_cnt, GFP_KERNEL);
	if (!cfg) {
		dev_err(dev, "failed to alloc memory for configs\n");
		goto free_gname;
	}

	/* Prepare a list of config settings */
	for (idx = 0, cfg_cnt = 0; idx < ARRAY_SIZE(pcfgs); idx++) {
		u32 value;
		if (!of_property_read_u32(np, pcfgs[idx].prop_cfg, &value))
			cfg[cfg_cnt++] =
				PINCFG_PACK(pcfgs[idx].cfg_type, value);
	}

	/* create the config map entry */
	map[*nmaps].data.configs.group_or_pin = gname;
	map[*nmaps].data.configs.configs = cfg;
	map[*nmaps].data.configs.num_configs = cfg_cnt;
	map[*nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
	*nmaps += 1;

skip_cfgs:
	/* create the function map entry */
	if (of_find_property(np, "samsung,exynos5440-pin-function", NULL)) {
		fname = kzalloc(strlen(np->name) + FSUFFIX_LEN,	GFP_KERNEL);
		if (!fname) {
			dev_err(dev, "failed to alloc memory for func name\n");
			goto free_cfg;
		}
		snprintf(fname, strlen(np->name) + 4, "%s%s", np->name,
			 FUNCTION_SUFFIX);

		map[*nmaps].data.mux.group = gname;
		map[*nmaps].data.mux.function = fname;
		map[*nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		*nmaps += 1;
	}

	*maps = map;
	return 0;

free_cfg:
	kfree(cfg);
free_gname:
	kfree(gname);
free_map:
	kfree(map);
	return -ENOMEM;
}

/* free the memory allocated to hold the pin-map table */
static void exynos5440_dt_free_map(struct pinctrl_dev *pctldev,
			     struct pinctrl_map *map, unsigned num_maps)
{
	int idx;

	for (idx = 0; idx < num_maps; idx++) {
		if (map[idx].type == PIN_MAP_TYPE_MUX_GROUP) {
			kfree(map[idx].data.mux.function);
			if (!idx)
				kfree(map[idx].data.mux.group);
		} else if (map->type == PIN_MAP_TYPE_CONFIGS_GROUP) {
			kfree(map[idx].data.configs.configs);
			if (!idx)
				kfree(map[idx].data.configs.group_or_pin);
		}
	};

	kfree(map);
}

/* list of pinctrl callbacks for the pinctrl core */
static const struct pinctrl_ops exynos5440_pctrl_ops = {
	.get_groups_count	= exynos5440_get_group_count,
	.get_group_name		= exynos5440_get_group_name,
	.get_group_pins		= exynos5440_get_group_pins,
	.dt_node_to_map		= exynos5440_dt_node_to_map,
	.dt_free_map		= exynos5440_dt_free_map,
};

/* check if the selector is a valid pin function selector */
static int exynos5440_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct exynos5440_pinctrl_priv_data *priv;

	priv = pinctrl_dev_get_drvdata(pctldev);
	return priv->nr_functions;
}

/* return the name of the pin function specified */
static const char *exynos5440_pinmux_get_fname(struct pinctrl_dev *pctldev,
						unsigned selector)
{
	struct exynos5440_pinctrl_priv_data *priv;

	priv = pinctrl_dev_get_drvdata(pctldev);
	return priv->pmx_functions[selector].name;
}

/* return the groups associated for the specified function selector */
static int exynos5440_pinmux_get_groups(struct pinctrl_dev *pctldev,
		unsigned selector, const char * const **groups,
		unsigned * const num_groups)
{
	struct exynos5440_pinctrl_priv_data *priv;

	priv = pinctrl_dev_get_drvdata(pctldev);
	*groups = priv->pmx_functions[selector].groups;
	*num_groups = priv->pmx_functions[selector].num_groups;
	return 0;
}

/* enable or disable a pinmux function */
static void exynos5440_pinmux_setup(struct pinctrl_dev *pctldev, unsigned selector,
					unsigned group, bool enable)
{
	struct exynos5440_pinctrl_priv_data *priv;
	void __iomem *base;
	u32 function;
	u32 data;

	priv = pinctrl_dev_get_drvdata(pctldev);
	base = priv->reg_base;
	function = priv->pmx_functions[selector].function;

	data = readl(base + GPIO_MUX);
	if (enable)
		data |= (1 << function);
	else
		data &= ~(1 << function);
	writel(data, base + GPIO_MUX);
}

/* enable a specified pinmux by writing to registers */
static int exynos5440_pinmux_set_mux(struct pinctrl_dev *pctldev,
				     unsigned selector,
				     unsigned group)
{
	exynos5440_pinmux_setup(pctldev, selector, group, true);
	return 0;
}

/*
 * The calls to gpio_direction_output() and gpio_direction_input()
 * leads to this function call (via the pinctrl_gpio_direction_{input|output}()
 * function called from the gpiolib interface).
 */
static int exynos5440_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset, bool input)
{
	return 0;
}

/* list of pinmux callbacks for the pinmux vertical in pinctrl core */
static const struct pinmux_ops exynos5440_pinmux_ops = {
	.get_functions_count	= exynos5440_get_functions_count,
	.get_function_name	= exynos5440_pinmux_get_fname,
	.get_function_groups	= exynos5440_pinmux_get_groups,
	.set_mux		= exynos5440_pinmux_set_mux,
	.gpio_set_direction	= exynos5440_pinmux_gpio_set_direction,
};

/* set the pin config settings for a specified pin */
static int exynos5440_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs,
				unsigned num_configs)
{
	struct exynos5440_pinctrl_priv_data *priv;
	void __iomem *base;
	enum pincfg_type cfg_type;
	u32 cfg_value;
	u32 data;
	int i;

	priv = pinctrl_dev_get_drvdata(pctldev);
	base = priv->reg_base;

	for (i = 0; i < num_configs; i++) {
		cfg_type = PINCFG_UNPACK_TYPE(configs[i]);
		cfg_value = PINCFG_UNPACK_VALUE(configs[i]);

		switch (cfg_type) {
		case PINCFG_TYPE_PUD:
			/* first set pull enable/disable bit */
			data = readl(base + GPIO_PE);
			data &= ~(1 << pin);
			if (cfg_value)
				data |= (1 << pin);
			writel(data, base + GPIO_PE);

			/* then set pull up/down bit */
			data = readl(base + GPIO_PS);
			data &= ~(1 << pin);
			if (cfg_value == 2)
				data |= (1 << pin);
			writel(data, base + GPIO_PS);
			break;

		case PINCFG_TYPE_DRV:
			/* set the first bit of the drive strength */
			data = readl(base + GPIO_DS0);
			data &= ~(1 << pin);
			data |= ((cfg_value & 1) << pin);
			writel(data, base + GPIO_DS0);
			cfg_value >>= 1;

			/* set the second bit of the driver strength */
			data = readl(base + GPIO_DS1);
			data &= ~(1 << pin);
			data |= ((cfg_value & 1) << pin);
			writel(data, base + GPIO_DS1);
			break;
		case PINCFG_TYPE_SKEW_RATE:
			data = readl(base + GPIO_SR);
			data &= ~(1 << pin);
			data |= ((cfg_value & 1) << pin);
			writel(data, base + GPIO_SR);
			break;
		case PINCFG_TYPE_INPUT_TYPE:
			data = readl(base + GPIO_TYPE);
			data &= ~(1 << pin);
			data |= ((cfg_value & 1) << pin);
			writel(data, base + GPIO_TYPE);
			break;
		default:
			WARN_ON(1);
			return -EINVAL;
		}
	} /* for each config */

	return 0;
}

/* get the pin config settings for a specified pin */
static int exynos5440_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
					unsigned long *config)
{
	struct exynos5440_pinctrl_priv_data *priv;
	void __iomem *base;
	enum pincfg_type cfg_type = PINCFG_UNPACK_TYPE(*config);
	u32 data;

	priv = pinctrl_dev_get_drvdata(pctldev);
	base = priv->reg_base;

	switch (cfg_type) {
	case PINCFG_TYPE_PUD:
		data = readl(base + GPIO_PE);
		data = (data >> pin) & 1;
		if (!data)
			*config = 0;
		else
			*config = ((readl(base + GPIO_PS) >> pin) & 1) + 1;
		break;
	case PINCFG_TYPE_DRV:
		data = readl(base + GPIO_DS0);
		data = (data >> pin) & 1;
		*config = data;
		data = readl(base + GPIO_DS1);
		data = (data >> pin) & 1;
		*config |= (data << 1);
		break;
	case PINCFG_TYPE_SKEW_RATE:
		data = readl(base + GPIO_SR);
		*config = (data >> pin) & 1;
		break;
	case PINCFG_TYPE_INPUT_TYPE:
		data = readl(base + GPIO_TYPE);
		*config = (data >> pin) & 1;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

/* set the pin config settings for a specified pin group */
static int exynos5440_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long *configs,
			unsigned num_configs)
{
	struct exynos5440_pinctrl_priv_data *priv;
	const unsigned int *pins;
	unsigned int cnt;

	priv = pinctrl_dev_get_drvdata(pctldev);
	pins = priv->pin_groups[group].pins;

	for (cnt = 0; cnt < priv->pin_groups[group].num_pins; cnt++)
		exynos5440_pinconf_set(pctldev, pins[cnt], configs,
				       num_configs);

	return 0;
}

/* get the pin config settings for a specified pin group */
static int exynos5440_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned int group, unsigned long *config)
{
	struct exynos5440_pinctrl_priv_data *priv;
	const unsigned int *pins;

	priv = pinctrl_dev_get_drvdata(pctldev);
	pins = priv->pin_groups[group].pins;
	exynos5440_pinconf_get(pctldev, pins[0], config);
	return 0;
}

/* list of pinconfig callbacks for pinconfig vertical in the pinctrl code */
static const struct pinconf_ops exynos5440_pinconf_ops = {
	.pin_config_get		= exynos5440_pinconf_get,
	.pin_config_set		= exynos5440_pinconf_set,
	.pin_config_group_get	= exynos5440_pinconf_group_get,
	.pin_config_group_set	= exynos5440_pinconf_group_set,
};

/* gpiolib gpio_set callback function */
static void exynos5440_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct exynos5440_pinctrl_priv_data *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->reg_base;
	u32 data;

	data = readl(base + GPIO_VAL);
	data &= ~(1 << offset);
	if (value)
		data |= 1 << offset;
	writel(data, base + GPIO_VAL);
}

/* gpiolib gpio_get callback function */
static int exynos5440_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct exynos5440_pinctrl_priv_data *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->reg_base;
	u32 data;

	data = readl(base + GPIO_IN);
	data >>= offset;
	data &= 1;
	return data;
}

/* gpiolib gpio_direction_input callback function */
static int exynos5440_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct exynos5440_pinctrl_priv_data *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->reg_base;
	u32 data;

	/* first disable the data output enable on this pin */
	data = readl(base + GPIO_OE);
	data &= ~(1 << offset);
	writel(data, base + GPIO_OE);

	/* now enable input on this pin */
	data =  readl(base + GPIO_IE);
	data |= 1 << offset;
	writel(data, base + GPIO_IE);
	return 0;
}

/* gpiolib gpio_direction_output callback function */
static int exynos5440_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
							int value)
{
	struct exynos5440_pinctrl_priv_data *priv = dev_get_drvdata(gc->dev);
	void __iomem *base = priv->reg_base;
	u32 data;

	exynos5440_gpio_set(gc, offset, value);

	/* first disable the data input enable on this pin */
	data = readl(base + GPIO_IE);
	data &= ~(1 << offset);
	writel(data, base + GPIO_IE);

	/* now enable output on this pin */
	data =  readl(base + GPIO_OE);
	data |= 1 << offset;
	writel(data, base + GPIO_OE);
	return 0;
}

/* gpiolib gpio_to_irq callback function */
static int exynos5440_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct exynos5440_pinctrl_priv_data *priv = dev_get_drvdata(gc->dev);
	unsigned int virq;

	if (offset < 16 || offset > 23)
		return -ENXIO;

	if (!priv->irq_domain)
		return -ENXIO;

	virq = irq_create_mapping(priv->irq_domain, offset - 16);
	return virq ? : -ENXIO;
}

/* parse the pin numbers listed in the 'samsung,exynos5440-pins' property */
static int exynos5440_pinctrl_parse_dt_pins(struct platform_device *pdev,
			struct device_node *cfg_np, unsigned int **pin_list,
			unsigned int *npins)
{
	struct device *dev = &pdev->dev;
	struct property *prop;

	prop = of_find_property(cfg_np, "samsung,exynos5440-pins", NULL);
	if (!prop)
		return -ENOENT;

	*npins = prop->length / sizeof(unsigned long);
	if (!*npins) {
		dev_err(dev, "invalid pin list in %s node", cfg_np->name);
		return -EINVAL;
	}

	*pin_list = devm_kzalloc(dev, *npins * sizeof(**pin_list), GFP_KERNEL);
	if (!*pin_list) {
		dev_err(dev, "failed to allocate memory for pin list\n");
		return -ENOMEM;
	}

	return of_property_read_u32_array(cfg_np, "samsung,exynos5440-pins",
			*pin_list, *npins);
}

/*
 * Parse the information about all the available pin groups and pin functions
 * from device node of the pin-controller.
 */
static int exynos5440_pinctrl_parse_dt(struct platform_device *pdev,
				struct exynos5440_pinctrl_priv_data *priv)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_np = dev->of_node;
	struct device_node *cfg_np;
	struct exynos5440_pin_group *groups, *grp;
	struct exynos5440_pmx_func *functions, *func;
	unsigned *pin_list;
	unsigned int npins, grp_cnt, func_idx = 0;
	char *gname, *fname;
	int ret;

	grp_cnt = of_get_child_count(dev_np);
	if (!grp_cnt)
		return -EINVAL;

	groups = devm_kzalloc(dev, grp_cnt * sizeof(*groups), GFP_KERNEL);
	if (!groups) {
		dev_err(dev, "failed allocate memory for ping group list\n");
		return -EINVAL;
	}
	grp = groups;

	functions = devm_kzalloc(dev, grp_cnt * sizeof(*functions), GFP_KERNEL);
	if (!functions) {
		dev_err(dev, "failed to allocate memory for function list\n");
		return -EINVAL;
	}
	func = functions;

	/*
	 * Iterate over all the child nodes of the pin controller node
	 * and create pin groups and pin function lists.
	 */
	for_each_child_of_node(dev_np, cfg_np) {
		u32 function;

		ret = exynos5440_pinctrl_parse_dt_pins(pdev, cfg_np,
					&pin_list, &npins);
		if (ret) {
			gname = NULL;
			goto skip_to_pin_function;
		}

		/* derive pin group name from the node name */
		gname = devm_kzalloc(dev, strlen(cfg_np->name) + GSUFFIX_LEN,
					GFP_KERNEL);
		if (!gname) {
			dev_err(dev, "failed to alloc memory for group name\n");
			return -ENOMEM;
		}
		snprintf(gname, strlen(cfg_np->name) + 4, "%s%s", cfg_np->name,
			 GROUP_SUFFIX);

		grp->name = gname;
		grp->pins = pin_list;
		grp->num_pins = npins;
		grp++;

skip_to_pin_function:
		ret = of_property_read_u32(cfg_np, "samsung,exynos5440-pin-function",
						&function);
		if (ret)
			continue;

		/* derive function name from the node name */
		fname = devm_kzalloc(dev, strlen(cfg_np->name) + FSUFFIX_LEN,
					GFP_KERNEL);
		if (!fname) {
			dev_err(dev, "failed to alloc memory for func name\n");
			return -ENOMEM;
		}
		snprintf(fname, strlen(cfg_np->name) + 4, "%s%s", cfg_np->name,
			 FUNCTION_SUFFIX);

		func->name = fname;
		func->groups = devm_kzalloc(dev, sizeof(char *), GFP_KERNEL);
		if (!func->groups) {
			dev_err(dev, "failed to alloc memory for group list "
					"in pin function");
			return -ENOMEM;
		}
		func->groups[0] = gname;
		func->num_groups = gname ? 1 : 0;
		func->function = function;
		func++;
		func_idx++;
	}

	priv->pin_groups = groups;
	priv->nr_groups = grp_cnt;
	priv->pmx_functions = functions;
	priv->nr_functions = func_idx;
	return 0;
}

/* register the pinctrl interface with the pinctrl subsystem */
static int exynos5440_pinctrl_register(struct platform_device *pdev,
				struct exynos5440_pinctrl_priv_data *priv)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_desc *ctrldesc;
	struct pinctrl_dev *pctl_dev;
	struct pinctrl_pin_desc *pindesc, *pdesc;
	struct pinctrl_gpio_range grange;
	char *pin_names;
	int pin, ret;

	ctrldesc = devm_kzalloc(dev, sizeof(*ctrldesc), GFP_KERNEL);
	if (!ctrldesc) {
		dev_err(dev, "could not allocate memory for pinctrl desc\n");
		return -ENOMEM;
	}

	ctrldesc->name = "exynos5440-pinctrl";
	ctrldesc->owner = THIS_MODULE;
	ctrldesc->pctlops = &exynos5440_pctrl_ops;
	ctrldesc->pmxops = &exynos5440_pinmux_ops;
	ctrldesc->confops = &exynos5440_pinconf_ops;

	pindesc = devm_kzalloc(&pdev->dev, sizeof(*pindesc) *
				EXYNOS5440_MAX_PINS, GFP_KERNEL);
	if (!pindesc) {
		dev_err(&pdev->dev, "mem alloc for pin descriptors failed\n");
		return -ENOMEM;
	}
	ctrldesc->pins = pindesc;
	ctrldesc->npins = EXYNOS5440_MAX_PINS;

	/* dynamically populate the pin number and pin name for pindesc */
	for (pin = 0, pdesc = pindesc; pin < ctrldesc->npins; pin++, pdesc++)
		pdesc->number = pin;

	/*
	 * allocate space for storing the dynamically generated names for all
	 * the pins which belong to this pin-controller.
	 */
	pin_names = devm_kzalloc(&pdev->dev, sizeof(char) * PIN_NAME_LENGTH *
					ctrldesc->npins, GFP_KERNEL);
	if (!pin_names) {
		dev_err(&pdev->dev, "mem alloc for pin names failed\n");
		return -ENOMEM;
	}

	/* for each pin, set the name of the pin */
	for (pin = 0; pin < ctrldesc->npins; pin++) {
		snprintf(pin_names, 6, "gpio%02d", pin);
		pdesc = pindesc + pin;
		pdesc->name = pin_names;
		pin_names += PIN_NAME_LENGTH;
	}

	ret = exynos5440_pinctrl_parse_dt(pdev, priv);
	if (ret)
		return ret;

	pctl_dev = pinctrl_register(ctrldesc, &pdev->dev, priv);
	if (IS_ERR(pctl_dev)) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return PTR_ERR(pctl_dev);
	}

	grange.name = "exynos5440-pctrl-gpio-range";
	grange.id = 0;
	grange.base = 0;
	grange.npins = EXYNOS5440_MAX_PINS;
	grange.gc = priv->gc;
	pinctrl_add_gpio_range(pctl_dev, &grange);
	return 0;
}

/* register the gpiolib interface with the gpiolib subsystem */
static int exynos5440_gpiolib_register(struct platform_device *pdev,
				struct exynos5440_pinctrl_priv_data *priv)
{
	struct gpio_chip *gc;
	int ret;

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc) {
		dev_err(&pdev->dev, "mem alloc for gpio_chip failed\n");
		return -ENOMEM;
	}

	priv->gc = gc;
	gc->base = 0;
	gc->ngpio = EXYNOS5440_MAX_PINS;
	gc->dev = &pdev->dev;
	gc->set = exynos5440_gpio_set;
	gc->get = exynos5440_gpio_get;
	gc->direction_input = exynos5440_gpio_direction_input;
	gc->direction_output = exynos5440_gpio_direction_output;
	gc->to_irq = exynos5440_gpio_to_irq;
	gc->label = "gpiolib-exynos5440";
	gc->owner = THIS_MODULE;
	ret = gpiochip_add(gc);
	if (ret) {
		dev_err(&pdev->dev, "failed to register gpio_chip %s, error "
					"code: %d\n", gc->label, ret);
		return ret;
	}

	return 0;
}

/* unregister the gpiolib interface with the gpiolib subsystem */
static int exynos5440_gpiolib_unregister(struct platform_device *pdev,
				struct exynos5440_pinctrl_priv_data *priv)
{
	gpiochip_remove(priv->gc);
	return 0;
}

static void exynos5440_gpio_irq_unmask(struct irq_data *irqd)
{
	struct exynos5440_pinctrl_priv_data *d;
	unsigned long gpio_int;

	d = irq_data_get_irq_chip_data(irqd);
	gpio_int = readl(d->reg_base + GPIO_INT);
	gpio_int |= 1 << irqd->hwirq;
	writel(gpio_int, d->reg_base + GPIO_INT);
}

static void exynos5440_gpio_irq_mask(struct irq_data *irqd)
{
	struct exynos5440_pinctrl_priv_data *d;
	unsigned long gpio_int;

	d = irq_data_get_irq_chip_data(irqd);
	gpio_int = readl(d->reg_base + GPIO_INT);
	gpio_int &= ~(1 << irqd->hwirq);
	writel(gpio_int, d->reg_base + GPIO_INT);
}

/* irq_chip for gpio interrupts */
static struct irq_chip exynos5440_gpio_irq_chip = {
	.name		= "exynos5440_gpio_irq_chip",
	.irq_unmask	= exynos5440_gpio_irq_unmask,
	.irq_mask	= exynos5440_gpio_irq_mask,
};

/* interrupt handler for GPIO interrupts 0..7 */
static irqreturn_t exynos5440_gpio_irq(int irq, void *data)
{
	struct exynos5440_gpio_intr_data *intd = data;
	struct exynos5440_pinctrl_priv_data *d = intd->priv;
	int virq;

	virq = irq_linear_revmap(d->irq_domain, intd->gpio_int);
	if (!virq)
		return IRQ_NONE;
	generic_handle_irq(virq);
	return IRQ_HANDLED;
}

static int exynos5440_gpio_irq_map(struct irq_domain *h, unsigned int virq,
					irq_hw_number_t hw)
{
	struct exynos5440_pinctrl_priv_data *d = h->host_data;

	irq_set_chip_data(virq, d);
	irq_set_chip_and_handler(virq, &exynos5440_gpio_irq_chip,
					handle_level_irq);
	set_irq_flags(virq, IRQF_VALID);
	return 0;
}

/* irq domain callbacks for gpio interrupt controller */
static const struct irq_domain_ops exynos5440_gpio_irqd_ops = {
	.map	= exynos5440_gpio_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

/* setup handling of gpio interrupts */
static int exynos5440_gpio_irq_init(struct platform_device *pdev,
				struct exynos5440_pinctrl_priv_data *priv)
{
	struct device *dev = &pdev->dev;
	struct exynos5440_gpio_intr_data *intd;
	int i, irq, ret;

	intd = devm_kzalloc(dev, sizeof(*intd) * EXYNOS5440_MAX_GPIO_INT,
					GFP_KERNEL);
	if (!intd) {
		dev_err(dev, "failed to allocate memory for gpio intr data\n");
		return -ENOMEM;
	}

	for (i = 0; i < EXYNOS5440_MAX_GPIO_INT; i++) {
		irq = irq_of_parse_and_map(dev->of_node, i);
		if (irq <= 0) {
			dev_err(dev, "irq parsing failed\n");
			return -EINVAL;
		}

		intd->gpio_int = i;
		intd->priv = priv;
		ret = devm_request_irq(dev, irq, exynos5440_gpio_irq,
					0, dev_name(dev), intd++);
		if (ret) {
			dev_err(dev, "irq request failed\n");
			return -ENXIO;
		}
	}

	priv->irq_domain = irq_domain_add_linear(dev->of_node,
				EXYNOS5440_MAX_GPIO_INT,
				&exynos5440_gpio_irqd_ops, priv);
	if (!priv->irq_domain) {
		dev_err(dev, "failed to create irq domain\n");
		return -ENXIO;
	}

	return 0;
}

static int exynos5440_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct exynos5440_pinctrl_priv_data *priv;
	struct resource *res;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "could not allocate memory for private data\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	ret = exynos5440_gpiolib_register(pdev, priv);
	if (ret)
		return ret;

	ret = exynos5440_pinctrl_register(pdev, priv);
	if (ret) {
		exynos5440_gpiolib_unregister(pdev, priv);
		return ret;
	}

	ret = exynos5440_gpio_irq_init(pdev, priv);
	if (ret) {
		dev_err(dev, "failed to setup gpio interrupts\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);
	dev_info(dev, "EXYNOS5440 pinctrl driver registered\n");
	return 0;
}

static const struct of_device_id exynos5440_pinctrl_dt_match[] = {
	{ .compatible = "samsung,exynos5440-pinctrl" },
	{},
};
MODULE_DEVICE_TABLE(of, exynos5440_pinctrl_dt_match);

static struct platform_driver exynos5440_pinctrl_driver = {
	.probe		= exynos5440_pinctrl_probe,
	.driver = {
		.name	= "exynos5440-pinctrl",
		.of_match_table = exynos5440_pinctrl_dt_match,
	},
};

static int __init exynos5440_pinctrl_drv_register(void)
{
	return platform_driver_register(&exynos5440_pinctrl_driver);
}
postcore_initcall(exynos5440_pinctrl_drv_register);

static void __exit exynos5440_pinctrl_drv_unregister(void)
{
	platform_driver_unregister(&exynos5440_pinctrl_driver);
}
module_exit(exynos5440_pinctrl_drv_unregister);

MODULE_AUTHOR("Thomas Abraham <thomas.ab@samsung.com>");
MODULE_DESCRIPTION("Samsung EXYNOS5440 SoC pinctrl driver");
MODULE_LICENSE("GPL v2");
