/*
 * pin-controller/pin-mux/pin-config/gpio-driver for Samsung's SoC's.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		http://www.linaro.org
 *
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This driver implements the Samsung pinctrl driver. It supports setting up of
 * pinmux and pinconf configurations. The gpiolib interface is also included.
 * External interrupt (gpio and wakeup) support are not included in this driver
 * but provides extensions to which platform specific implementation of the gpio
 * and wakeup interrupts can be hooked to.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/gpio.h>

#include "core.h"
#include "pinctrl-samsung.h"

#define GROUP_SUFFIX		"-grp"
#define GSUFFIX_LEN		sizeof(GROUP_SUFFIX)
#define FUNCTION_SUFFIX		"-mux"
#define FSUFFIX_LEN		sizeof(FUNCTION_SUFFIX)

/* list of all possible config options supported */
struct pin_config {
	char		*prop_cfg;
	unsigned int	cfg_type;
} pcfgs[] = {
	{ "samsung,pin-pud", PINCFG_TYPE_PUD },
	{ "samsung,pin-drv", PINCFG_TYPE_DRV },
	{ "samsung,pin-con-pdn", PINCFG_TYPE_CON_PDN },
	{ "samsung,pin-pud-pdn", PINCFG_TYPE_PUD_PDN },
};

/* check if the selector is a valid pin group selector */
static int samsung_get_group_count(struct pinctrl_dev *pctldev)
{
	struct samsung_pinctrl_drv_data *drvdata;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	return drvdata->nr_groups;
}

/* return the name of the group selected by the group selector */
static const char *samsung_get_group_name(struct pinctrl_dev *pctldev,
						unsigned selector)
{
	struct samsung_pinctrl_drv_data *drvdata;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	return drvdata->pin_groups[selector].name;
}

/* return the pin numbers associated with the specified group */
static int samsung_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned selector, const unsigned **pins, unsigned *num_pins)
{
	struct samsung_pinctrl_drv_data *drvdata;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	*pins = drvdata->pin_groups[selector].pins;
	*num_pins = drvdata->pin_groups[selector].num_pins;
	return 0;
}

/* create pinctrl_map entries by parsing device tree nodes */
static int samsung_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np, struct pinctrl_map **maps,
			unsigned *nmaps)
{
	struct device *dev = pctldev->dev;
	struct pinctrl_map *map;
	unsigned long *cfg = NULL;
	char *gname, *fname;
	int cfg_cnt = 0, map_cnt = 0, idx = 0;

	/* count the number of config options specfied in the node */
	for (idx = 0; idx < ARRAY_SIZE(pcfgs); idx++) {
		if (of_find_property(np, pcfgs[idx].prop_cfg, NULL))
			cfg_cnt++;
	}

	/*
	 * Find out the number of map entries to create. All the config options
	 * can be accomadated into a single config map entry.
	 */
	if (cfg_cnt)
		map_cnt = 1;
	if (of_find_property(np, "samsung,pin-function", NULL))
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
	sprintf(gname, "%s%s", np->name, GROUP_SUFFIX);

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
	if (of_find_property(np, "samsung,pin-function", NULL)) {
		fname = kzalloc(strlen(np->name) + FSUFFIX_LEN,	GFP_KERNEL);
		if (!fname) {
			dev_err(dev, "failed to alloc memory for func name\n");
			goto free_cfg;
		}
		sprintf(fname, "%s%s", np->name, FUNCTION_SUFFIX);

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
static void samsung_dt_free_map(struct pinctrl_dev *pctldev,
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
static struct pinctrl_ops samsung_pctrl_ops = {
	.get_groups_count	= samsung_get_group_count,
	.get_group_name		= samsung_get_group_name,
	.get_group_pins		= samsung_get_group_pins,
	.dt_node_to_map		= samsung_dt_node_to_map,
	.dt_free_map		= samsung_dt_free_map,
};

/* check if the selector is a valid pin function selector */
static int samsung_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct samsung_pinctrl_drv_data *drvdata;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	return drvdata->nr_functions;
}

/* return the name of the pin function specified */
static const char *samsung_pinmux_get_fname(struct pinctrl_dev *pctldev,
						unsigned selector)
{
	struct samsung_pinctrl_drv_data *drvdata;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	return drvdata->pmx_functions[selector].name;
}

/* return the groups associated for the specified function selector */
static int samsung_pinmux_get_groups(struct pinctrl_dev *pctldev,
		unsigned selector, const char * const **groups,
		unsigned * const num_groups)
{
	struct samsung_pinctrl_drv_data *drvdata;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	*groups = drvdata->pmx_functions[selector].groups;
	*num_groups = drvdata->pmx_functions[selector].num_groups;
	return 0;
}

/*
 * given a pin number that is local to a pin controller, find out the pin bank
 * and the register base of the pin bank.
 */
static void pin_to_reg_bank(struct gpio_chip *gc, unsigned pin,
			void __iomem **reg, u32 *offset,
			struct samsung_pin_bank **bank)
{
	struct samsung_pinctrl_drv_data *drvdata;
	struct samsung_pin_bank *b;

	drvdata = dev_get_drvdata(gc->dev);
	b = drvdata->ctrl->pin_banks;

	while ((pin >= b->pin_base) &&
			((b->pin_base + b->nr_pins - 1) < pin))
		b++;

	*reg = drvdata->virt_base + b->pctl_offset;
	*offset = pin - b->pin_base;
	if (bank)
		*bank = b;

	/* some banks have two config registers in a single bank */
	if (*offset * b->func_width > BITS_PER_LONG)
		*reg += 4;
}

/* enable or disable a pinmux function */
static void samsung_pinmux_setup(struct pinctrl_dev *pctldev, unsigned selector,
					unsigned group, bool enable)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const unsigned int *pins;
	struct samsung_pin_bank *bank;
	void __iomem *reg;
	u32 mask, shift, data, pin_offset, cnt;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	pins = drvdata->pin_groups[group].pins;

	/*
	 * for each pin in the pin group selected, program the correspoding pin
	 * pin function number in the config register.
	 */
	for (cnt = 0; cnt < drvdata->pin_groups[group].num_pins; cnt++) {
		pin_to_reg_bank(drvdata->gc, pins[cnt] - drvdata->ctrl->base,
				&reg, &pin_offset, &bank);
		mask = (1 << bank->func_width) - 1;
		shift = pin_offset * bank->func_width;

		data = readl(reg);
		data &= ~(mask << shift);
		if (enable)
			data |= drvdata->pin_groups[group].func << shift;
		writel(data, reg);
	}
}

/* enable a specified pinmux by writing to registers */
static int samsung_pinmux_enable(struct pinctrl_dev *pctldev, unsigned selector,
					unsigned group)
{
	samsung_pinmux_setup(pctldev, selector, group, true);
	return 0;
}

/* disable a specified pinmux by writing to registers */
static void samsung_pinmux_disable(struct pinctrl_dev *pctldev,
					unsigned selector, unsigned group)
{
	samsung_pinmux_setup(pctldev, selector, group, false);
}

/*
 * The calls to gpio_direction_output() and gpio_direction_input()
 * leads to this function call (via the pinctrl_gpio_direction_{input|output}()
 * function called from the gpiolib interface).
 */
static int samsung_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset, bool input)
{
	struct samsung_pin_bank *bank;
	void __iomem *reg;
	u32 data, pin_offset, mask, shift;

	pin_to_reg_bank(range->gc, offset, &reg, &pin_offset, &bank);
	mask = (1 << bank->func_width) - 1;
	shift = pin_offset * bank->func_width;

	data = readl(reg);
	data &= ~(mask << shift);
	if (!input)
		data |= FUNC_OUTPUT << shift;
	writel(data, reg);
	return 0;
}

/* list of pinmux callbacks for the pinmux vertical in pinctrl core */
static struct pinmux_ops samsung_pinmux_ops = {
	.get_functions_count	= samsung_get_functions_count,
	.get_function_name	= samsung_pinmux_get_fname,
	.get_function_groups	= samsung_pinmux_get_groups,
	.enable			= samsung_pinmux_enable,
	.disable		= samsung_pinmux_disable,
	.gpio_set_direction	= samsung_pinmux_gpio_set_direction,
};

/* set or get the pin config settings for a specified pin */
static int samsung_pinconf_rw(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *config, bool set)
{
	struct samsung_pinctrl_drv_data *drvdata;
	struct samsung_pin_bank *bank;
	void __iomem *reg_base;
	enum pincfg_type cfg_type = PINCFG_UNPACK_TYPE(*config);
	u32 data, width, pin_offset, mask, shift;
	u32 cfg_value, cfg_reg;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	pin_to_reg_bank(drvdata->gc, pin - drvdata->ctrl->base, &reg_base,
					&pin_offset, &bank);

	switch (cfg_type) {
	case PINCFG_TYPE_PUD:
		width = bank->pud_width;
		cfg_reg = PUD_REG;
		break;
	case PINCFG_TYPE_DRV:
		width = bank->drv_width;
		cfg_reg = DRV_REG;
		break;
	case PINCFG_TYPE_CON_PDN:
		width = bank->conpdn_width;
		cfg_reg = CONPDN_REG;
		break;
	case PINCFG_TYPE_PUD_PDN:
		width = bank->pudpdn_width;
		cfg_reg = PUDPDN_REG;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	mask = (1 << width) - 1;
	shift = pin_offset * width;
	data = readl(reg_base + cfg_reg);

	if (set) {
		cfg_value = PINCFG_UNPACK_VALUE(*config);
		data &= ~(mask << shift);
		data |= (cfg_value << shift);
		writel(data, reg_base + cfg_reg);
	} else {
		data >>= shift;
		data &= mask;
		*config = PINCFG_PACK(cfg_type, data);
	}
	return 0;
}

/* set the pin config settings for a specified pin */
static int samsung_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long config)
{
	return samsung_pinconf_rw(pctldev, pin, &config, true);
}

/* get the pin config settings for a specified pin */
static int samsung_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
					unsigned long *config)
{
	return samsung_pinconf_rw(pctldev, pin, config, false);
}

/* set the pin config settings for a specified pin group */
static int samsung_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long config)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const unsigned int *pins;
	unsigned int cnt;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	pins = drvdata->pin_groups[group].pins;

	for (cnt = 0; cnt < drvdata->pin_groups[group].num_pins; cnt++)
		samsung_pinconf_set(pctldev, pins[cnt], config);

	return 0;
}

/* get the pin config settings for a specified pin group */
static int samsung_pinconf_group_get(struct pinctrl_dev *pctldev,
				unsigned int group, unsigned long *config)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const unsigned int *pins;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	pins = drvdata->pin_groups[group].pins;
	samsung_pinconf_get(pctldev, pins[0], config);
	return 0;
}

/* list of pinconfig callbacks for pinconfig vertical in the pinctrl code */
static struct pinconf_ops samsung_pinconf_ops = {
	.pin_config_get		= samsung_pinconf_get,
	.pin_config_set		= samsung_pinconf_set,
	.pin_config_group_get	= samsung_pinconf_group_get,
	.pin_config_group_set	= samsung_pinconf_group_set,
};

/* gpiolib gpio_set callback function */
static void samsung_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	void __iomem *reg;
	u32 pin_offset, data;

	pin_to_reg_bank(gc, offset, &reg, &pin_offset, NULL);
	data = readl(reg + DAT_REG);
	data &= ~(1 << pin_offset);
	if (value)
		data |= 1 << pin_offset;
	writel(data, reg + DAT_REG);
}

/* gpiolib gpio_get callback function */
static int samsung_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	void __iomem *reg;
	u32 pin_offset, data;

	pin_to_reg_bank(gc, offset, &reg, &pin_offset, NULL);
	data = readl(reg + DAT_REG);
	data >>= pin_offset;
	data &= 1;
	return data;
}

/*
 * gpiolib gpio_direction_input callback function. The setting of the pin
 * mux function as 'gpio input' will be handled by the pinctrl susbsystem
 * interface.
 */
static int samsung_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	return pinctrl_gpio_direction_input(gc->base + offset);
}

/*
 * gpiolib gpio_direction_output callback function. The setting of the pin
 * mux function as 'gpio output' will be handled by the pinctrl susbsystem
 * interface.
 */
static int samsung_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
							int value)
{
	samsung_gpio_set(gc, offset, value);
	return pinctrl_gpio_direction_output(gc->base + offset);
}

/*
 * Parse the pin names listed in the 'samsung,pins' property and convert it
 * into a list of gpio numbers are create a pin group from it.
 */
static int __devinit samsung_pinctrl_parse_dt_pins(struct platform_device *pdev,
			struct device_node *cfg_np, struct pinctrl_desc *pctl,
			unsigned int **pin_list, unsigned int *npins)
{
	struct device *dev = &pdev->dev;
	struct property *prop;
	struct pinctrl_pin_desc const *pdesc = pctl->pins;
	unsigned int idx = 0, cnt;
	const char *pin_name;

	*npins = of_property_count_strings(cfg_np, "samsung,pins");
	if (*npins < 0) {
		dev_err(dev, "invalid pin list in %s node", cfg_np->name);
		return -EINVAL;
	}

	*pin_list = devm_kzalloc(dev, *npins * sizeof(**pin_list), GFP_KERNEL);
	if (!*pin_list) {
		dev_err(dev, "failed to allocate memory for pin list\n");
		return -ENOMEM;
	}

	of_property_for_each_string(cfg_np, "samsung,pins", prop, pin_name) {
		for (cnt = 0; cnt < pctl->npins; cnt++) {
			if (pdesc[cnt].name) {
				if (!strcmp(pin_name, pdesc[cnt].name)) {
					(*pin_list)[idx++] = pdesc[cnt].number;
					break;
				}
			}
		}
		if (cnt == pctl->npins) {
			dev_err(dev, "pin %s not valid in %s node\n",
					pin_name, cfg_np->name);
			devm_kfree(dev, *pin_list);
			return -EINVAL;
		}
	}

	return 0;
}

/*
 * Parse the information about all the available pin groups and pin functions
 * from device node of the pin-controller. A pin group is formed with all
 * the pins listed in the "samsung,pins" property.
 */
static int __devinit samsung_pinctrl_parse_dt(struct platform_device *pdev,
				struct samsung_pinctrl_drv_data *drvdata)
{
	struct device *dev = &pdev->dev;
	struct device_node *dev_np = dev->of_node;
	struct device_node *cfg_np;
	struct samsung_pin_group *groups, *grp;
	struct samsung_pmx_func *functions, *func;
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
		if (of_find_property(cfg_np, "interrupt-controller", NULL))
			continue;

		ret = samsung_pinctrl_parse_dt_pins(pdev, cfg_np,
					&drvdata->pctl,	&pin_list, &npins);
		if (ret)
			return ret;

		/* derive pin group name from the node name */
		gname = devm_kzalloc(dev, strlen(cfg_np->name) + GSUFFIX_LEN,
					GFP_KERNEL);
		if (!gname) {
			dev_err(dev, "failed to alloc memory for group name\n");
			return -ENOMEM;
		}
		sprintf(gname, "%s%s", cfg_np->name, GROUP_SUFFIX);

		grp->name = gname;
		grp->pins = pin_list;
		grp->num_pins = npins;
		of_property_read_u32(cfg_np, "samsung,pin-function", &function);
		grp->func = function;
		grp++;

		if (!of_find_property(cfg_np, "samsung,pin-function", NULL))
			continue;

		/* derive function name from the node name */
		fname = devm_kzalloc(dev, strlen(cfg_np->name) + FSUFFIX_LEN,
					GFP_KERNEL);
		if (!fname) {
			dev_err(dev, "failed to alloc memory for func name\n");
			return -ENOMEM;
		}
		sprintf(fname, "%s%s", cfg_np->name, FUNCTION_SUFFIX);

		func->name = fname;
		func->groups = devm_kzalloc(dev, sizeof(char *), GFP_KERNEL);
		if (!func->groups) {
			dev_err(dev, "failed to alloc memory for group list "
					"in pin function");
			return -ENOMEM;
		}
		func->groups[0] = gname;
		func->num_groups = 1;
		func++;
		func_idx++;
	}

	drvdata->pin_groups = groups;
	drvdata->nr_groups = grp_cnt;
	drvdata->pmx_functions = functions;
	drvdata->nr_functions = func_idx;

	return 0;
}

/* register the pinctrl interface with the pinctrl subsystem */
static int __devinit samsung_pinctrl_register(struct platform_device *pdev,
				struct samsung_pinctrl_drv_data *drvdata)
{
	struct pinctrl_desc *ctrldesc = &drvdata->pctl;
	struct pinctrl_pin_desc *pindesc, *pdesc;
	struct samsung_pin_bank *pin_bank;
	char *pin_names;
	int pin, bank, ret;

	ctrldesc->name = "samsung-pinctrl";
	ctrldesc->owner = THIS_MODULE;
	ctrldesc->pctlops = &samsung_pctrl_ops;
	ctrldesc->pmxops = &samsung_pinmux_ops;
	ctrldesc->confops = &samsung_pinconf_ops;

	pindesc = devm_kzalloc(&pdev->dev, sizeof(*pindesc) *
			drvdata->ctrl->nr_pins, GFP_KERNEL);
	if (!pindesc) {
		dev_err(&pdev->dev, "mem alloc for pin descriptors failed\n");
		return -ENOMEM;
	}
	ctrldesc->pins = pindesc;
	ctrldesc->npins = drvdata->ctrl->nr_pins;
	ctrldesc->npins = drvdata->ctrl->nr_pins;

	/* dynamically populate the pin number and pin name for pindesc */
	for (pin = 0, pdesc = pindesc; pin < ctrldesc->npins; pin++, pdesc++)
		pdesc->number = pin + drvdata->ctrl->base;

	/*
	 * allocate space for storing the dynamically generated names for all
	 * the pins which belong to this pin-controller.
	 */
	pin_names = devm_kzalloc(&pdev->dev, sizeof(char) * PIN_NAME_LENGTH *
					drvdata->ctrl->nr_pins, GFP_KERNEL);
	if (!pin_names) {
		dev_err(&pdev->dev, "mem alloc for pin names failed\n");
		return -ENOMEM;
	}

	/* for each pin, the name of the pin is pin-bank name + pin number */
	for (bank = 0; bank < drvdata->ctrl->nr_banks; bank++) {
		pin_bank = &drvdata->ctrl->pin_banks[bank];
		for (pin = 0; pin < pin_bank->nr_pins; pin++) {
			sprintf(pin_names, "%s-%d", pin_bank->name, pin);
			pdesc = pindesc + pin_bank->pin_base + pin;
			pdesc->name = pin_names;
			pin_names += PIN_NAME_LENGTH;
		}
	}

	drvdata->pctl_dev = pinctrl_register(ctrldesc, &pdev->dev, drvdata);
	if (!drvdata->pctl_dev) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return -EINVAL;
	}

	drvdata->grange.name = "samsung-pctrl-gpio-range";
	drvdata->grange.id = 0;
	drvdata->grange.base = drvdata->ctrl->base;
	drvdata->grange.npins = drvdata->ctrl->nr_pins;
	drvdata->grange.gc = drvdata->gc;
	pinctrl_add_gpio_range(drvdata->pctl_dev, &drvdata->grange);

	ret = samsung_pinctrl_parse_dt(pdev, drvdata);
	if (ret) {
		pinctrl_unregister(drvdata->pctl_dev);
		return ret;
	}

	return 0;
}

/* register the gpiolib interface with the gpiolib subsystem */
static int __devinit samsung_gpiolib_register(struct platform_device *pdev,
				struct samsung_pinctrl_drv_data *drvdata)
{
	struct gpio_chip *gc;
	int ret;

	gc = devm_kzalloc(&pdev->dev, sizeof(*gc), GFP_KERNEL);
	if (!gc) {
		dev_err(&pdev->dev, "mem alloc for gpio_chip failed\n");
		return -ENOMEM;
	}

	drvdata->gc = gc;
	gc->base = drvdata->ctrl->base;
	gc->ngpio = drvdata->ctrl->nr_pins;
	gc->dev = &pdev->dev;
	gc->set = samsung_gpio_set;
	gc->get = samsung_gpio_get;
	gc->direction_input = samsung_gpio_direction_input;
	gc->direction_output = samsung_gpio_direction_output;
	gc->label = drvdata->ctrl->label;
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
static int __devinit samsung_gpiolib_unregister(struct platform_device *pdev,
				struct samsung_pinctrl_drv_data *drvdata)
{
	int ret = gpiochip_remove(drvdata->gc);
	if (ret) {
		dev_err(&pdev->dev, "gpio chip remove failed\n");
		return ret;
	}
	return 0;
}

static const struct of_device_id samsung_pinctrl_dt_match[];

/* retrieve the soc specific data */
static struct samsung_pin_ctrl *samsung_pinctrl_get_soc_data(
				struct platform_device *pdev)
{
	int id;
	const struct of_device_id *match;
	const struct device_node *node = pdev->dev.of_node;

	id = of_alias_get_id(pdev->dev.of_node, "pinctrl");
	if (id < 0) {
		dev_err(&pdev->dev, "failed to get alias id\n");
		return NULL;
	}
	match = of_match_node(samsung_pinctrl_dt_match, node);
	return (struct samsung_pin_ctrl *)match->data + id;
}

static int __devinit samsung_pinctrl_probe(struct platform_device *pdev)
{
	struct samsung_pinctrl_drv_data *drvdata;
	struct device *dev = &pdev->dev;
	struct samsung_pin_ctrl *ctrl;
	struct resource *res;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	ctrl = samsung_pinctrl_get_soc_data(pdev);
	if (!ctrl) {
		dev_err(&pdev->dev, "driver data not available\n");
		return -EINVAL;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		dev_err(dev, "failed to allocate memory for driver's "
				"private data\n");
		return -ENOMEM;
	}
	drvdata->ctrl = ctrl;
	drvdata->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "cannot find IO resource\n");
		return -ENOENT;
	}

	drvdata->virt_base = devm_request_and_ioremap(&pdev->dev, res);
	if (!drvdata->virt_base) {
		dev_err(dev, "ioremap failed\n");
		return -ENODEV;
	}

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res)
		drvdata->irq = res->start;

	ret = samsung_gpiolib_register(pdev, drvdata);
	if (ret)
		return ret;

	ret = samsung_pinctrl_register(pdev, drvdata);
	if (ret) {
		samsung_gpiolib_unregister(pdev, drvdata);
		return ret;
	}

	if (ctrl->eint_gpio_init)
		ctrl->eint_gpio_init(drvdata);
	if (ctrl->eint_wkup_init)
		ctrl->eint_wkup_init(drvdata);

	platform_set_drvdata(pdev, drvdata);
	return 0;
}

static const struct of_device_id samsung_pinctrl_dt_match[] = {
	{ .compatible = "samsung,pinctrl-exynos4210",
		.data = (void *)exynos4210_pin_ctrl },
	{},
};
MODULE_DEVICE_TABLE(of, samsung_pinctrl_dt_match);

static struct platform_driver samsung_pinctrl_driver = {
	.probe		= samsung_pinctrl_probe,
	.driver = {
		.name	= "samsung-pinctrl",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(samsung_pinctrl_dt_match),
	},
};

static int __init samsung_pinctrl_drv_register(void)
{
	return platform_driver_register(&samsung_pinctrl_driver);
}
postcore_initcall(samsung_pinctrl_drv_register);

static void __exit samsung_pinctrl_drv_unregister(void)
{
	platform_driver_unregister(&samsung_pinctrl_driver);
}
module_exit(samsung_pinctrl_drv_unregister);

MODULE_AUTHOR("Thomas Abraham <thomas.ab@samsung.com>");
MODULE_DESCRIPTION("Samsung pinctrl driver");
MODULE_LICENSE("GPL v2");
