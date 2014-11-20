/*
 * Pinctrl driver for the Wondermedia SoC's
 *
 * Copyright (c) 2013 Tony Prisk <linux@prisktech.co.nz>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "pinctrl-wmt.h"

static inline void wmt_setbits(struct wmt_pinctrl_data *data, u32 reg,
				 u32 mask)
{
	u32 val;

	val = readl_relaxed(data->base + reg);
	val |= mask;
	writel_relaxed(val, data->base + reg);
}

static inline void wmt_clearbits(struct wmt_pinctrl_data *data, u32 reg,
				   u32 mask)
{
	u32 val;

	val = readl_relaxed(data->base + reg);
	val &= ~mask;
	writel_relaxed(val, data->base + reg);
}

enum wmt_func_sel {
	WMT_FSEL_GPIO_IN = 0,
	WMT_FSEL_GPIO_OUT = 1,
	WMT_FSEL_ALT = 2,
	WMT_FSEL_COUNT = 3,
};

static const char * const wmt_functions[WMT_FSEL_COUNT] = {
	[WMT_FSEL_GPIO_IN] = "gpio_in",
	[WMT_FSEL_GPIO_OUT] = "gpio_out",
	[WMT_FSEL_ALT] = "alt",
};

static int wmt_pmx_get_functions_count(struct pinctrl_dev *pctldev)
{
	return WMT_FSEL_COUNT;
}

static const char *wmt_pmx_get_function_name(struct pinctrl_dev *pctldev,
					     unsigned selector)
{
	return wmt_functions[selector];
}

static int wmt_pmx_get_function_groups(struct pinctrl_dev *pctldev,
				       unsigned selector,
				       const char * const **groups,
				       unsigned * const num_groups)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	/* every pin does every function */
	*groups = data->groups;
	*num_groups = data->ngroups;

	return 0;
}

static int wmt_set_pinmux(struct wmt_pinctrl_data *data, unsigned func,
			  unsigned pin)
{
	u32 bank = WMT_BANK_FROM_PIN(pin);
	u32 bit = WMT_BIT_FROM_PIN(pin);
	u32 reg_en = data->banks[bank].reg_en;
	u32 reg_dir = data->banks[bank].reg_dir;

	if (reg_dir == NO_REG) {
		dev_err(data->dev, "pin:%d no direction register defined\n",
			pin);
		return -EINVAL;
	}

	/*
	 * If reg_en == NO_REG, we assume it is a dedicated GPIO and cannot be
	 * disabled (as on VT8500) and that no alternate function is available.
	 */
	switch (func) {
	case WMT_FSEL_GPIO_IN:
		if (reg_en != NO_REG)
			wmt_setbits(data, reg_en, BIT(bit));
		wmt_clearbits(data, reg_dir, BIT(bit));
		break;
	case WMT_FSEL_GPIO_OUT:
		if (reg_en != NO_REG)
			wmt_setbits(data, reg_en, BIT(bit));
		wmt_setbits(data, reg_dir, BIT(bit));
		break;
	case WMT_FSEL_ALT:
		if (reg_en == NO_REG) {
			dev_err(data->dev, "pin:%d no alt function available\n",
				pin);
			return -EINVAL;
		}
		wmt_clearbits(data, reg_en, BIT(bit));
	}

	return 0;
}

static int wmt_pmx_set_mux(struct pinctrl_dev *pctldev,
			   unsigned func_selector,
			   unsigned group_selector)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);
	u32 pinnum = data->pins[group_selector].number;

	return wmt_set_pinmux(data, func_selector, pinnum);
}

static void wmt_pmx_gpio_disable_free(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned offset)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	/* disable by setting GPIO_IN */
	wmt_set_pinmux(data, WMT_FSEL_GPIO_IN, offset);
}

static int wmt_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned offset,
				      bool input)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	wmt_set_pinmux(data, (input ? WMT_FSEL_GPIO_IN : WMT_FSEL_GPIO_OUT),
		       offset);

	return 0;
}

static struct pinmux_ops wmt_pinmux_ops = {
	.get_functions_count = wmt_pmx_get_functions_count,
	.get_function_name = wmt_pmx_get_function_name,
	.get_function_groups = wmt_pmx_get_function_groups,
	.set_mux = wmt_pmx_set_mux,
	.gpio_disable_free = wmt_pmx_gpio_disable_free,
	.gpio_set_direction = wmt_pmx_gpio_set_direction,
};

static int wmt_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	return data->ngroups;
}

static const char *wmt_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned selector)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	return data->groups[selector];
}

static int wmt_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned selector,
			      const unsigned **pins,
			      unsigned *num_pins)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	*pins = &data->pins[selector].number;
	*num_pins = 1;

	return 0;
}

static int wmt_pctl_find_group_by_pin(struct wmt_pinctrl_data *data, u32 pin)
{
	int i;

	for (i = 0; i < data->npins; i++) {
		if (data->pins[i].number == pin)
			return i;
	}

	return -EINVAL;
}

static int wmt_pctl_dt_node_to_map_func(struct wmt_pinctrl_data *data,
					struct device_node *np,
					u32 pin, u32 fnum,
					struct pinctrl_map **maps)
{
	int group;
	struct pinctrl_map *map = *maps;

	if (fnum >= ARRAY_SIZE(wmt_functions)) {
		dev_err(data->dev, "invalid wm,function %d\n", fnum);
		return -EINVAL;
	}

	group = wmt_pctl_find_group_by_pin(data, pin);
	if (group < 0) {
		dev_err(data->dev, "unable to match pin %d to group\n", pin);
		return group;
	}

	map->type = PIN_MAP_TYPE_MUX_GROUP;
	map->data.mux.group = data->groups[group];
	map->data.mux.function = wmt_functions[fnum];
	(*maps)++;

	return 0;
}

static int wmt_pctl_dt_node_to_map_pull(struct wmt_pinctrl_data *data,
					struct device_node *np,
					u32 pin, u32 pull,
					struct pinctrl_map **maps)
{
	int group;
	unsigned long *configs;
	struct pinctrl_map *map = *maps;

	if (pull > 2) {
		dev_err(data->dev, "invalid wm,pull %d\n", pull);
		return -EINVAL;
	}

	group = wmt_pctl_find_group_by_pin(data, pin);
	if (group < 0) {
		dev_err(data->dev, "unable to match pin %d to group\n", pin);
		return group;
	}

	configs = kzalloc(sizeof(*configs), GFP_KERNEL);
	if (!configs)
		return -ENOMEM;

	switch (pull) {
	case 0:
		configs[0] = PIN_CONFIG_BIAS_DISABLE;
		break;
	case 1:
		configs[0] = PIN_CONFIG_BIAS_PULL_DOWN;
		break;
	case 2:
		configs[0] = PIN_CONFIG_BIAS_PULL_UP;
		break;
	default:
		configs[0] = PIN_CONFIG_BIAS_DISABLE;
		dev_err(data->dev, "invalid pull state %d - disabling\n", pull);
	}

	map->type = PIN_MAP_TYPE_CONFIGS_PIN;
	map->data.configs.group_or_pin = data->groups[group];
	map->data.configs.configs = configs;
	map->data.configs.num_configs = 1;
	(*maps)++;

	return 0;
}

static void wmt_pctl_dt_free_map(struct pinctrl_dev *pctldev,
				 struct pinctrl_map *maps,
				 unsigned num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (maps[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(maps[i].data.configs.configs);

	kfree(maps);
}

static int wmt_pctl_dt_node_to_map(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **map,
				   unsigned *num_maps)
{
	struct pinctrl_map *maps, *cur_map;
	struct property *pins, *funcs, *pulls;
	u32 pin, func, pull;
	int num_pins, num_funcs, num_pulls, maps_per_pin;
	int i, err;
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);

	pins = of_find_property(np, "wm,pins", NULL);
	if (!pins) {
		dev_err(data->dev, "missing wmt,pins property\n");
		return -EINVAL;
	}

	funcs = of_find_property(np, "wm,function", NULL);
	pulls = of_find_property(np, "wm,pull", NULL);

	if (!funcs && !pulls) {
		dev_err(data->dev, "neither wm,function nor wm,pull specified\n");
		return -EINVAL;
	}

	/*
	 * The following lines calculate how many values are defined for each
	 * of the properties.
	 */
	num_pins = pins->length / sizeof(u32);
	num_funcs = funcs ? (funcs->length / sizeof(u32)) : 0;
	num_pulls = pulls ? (pulls->length / sizeof(u32)) : 0;

	if (num_funcs > 1 && num_funcs != num_pins) {
		dev_err(data->dev, "wm,function must have 1 or %d entries\n",
			num_pins);
		return -EINVAL;
	}

	if (num_pulls > 1 && num_pulls != num_pins) {
		dev_err(data->dev, "wm,pull must have 1 or %d entries\n",
			num_pins);
		return -EINVAL;
	}

	maps_per_pin = 0;
	if (num_funcs)
		maps_per_pin++;
	if (num_pulls)
		maps_per_pin++;

	cur_map = maps = kzalloc(num_pins * maps_per_pin * sizeof(*maps),
				 GFP_KERNEL);
	if (!maps)
		return -ENOMEM;

	for (i = 0; i < num_pins; i++) {
		err = of_property_read_u32_index(np, "wm,pins", i, &pin);
		if (err)
			goto fail;

		if (pin >= (data->nbanks * 32)) {
			dev_err(data->dev, "invalid wm,pins value\n");
			err = -EINVAL;
			goto fail;
		}

		if (num_funcs) {
			err = of_property_read_u32_index(np, "wm,function",
						(num_funcs > 1 ? i : 0), &func);
			if (err)
				goto fail;

			err = wmt_pctl_dt_node_to_map_func(data, np, pin, func,
							   &cur_map);
			if (err)
				goto fail;
		}

		if (num_pulls) {
			err = of_property_read_u32_index(np, "wm,pull",
						(num_pulls > 1 ? i : 0), &pull);
			if (err)
				goto fail;

			err = wmt_pctl_dt_node_to_map_pull(data, np, pin, pull,
							   &cur_map);
			if (err)
				goto fail;
		}
	}
	*map = maps;
	*num_maps = num_pins * maps_per_pin;
	return 0;

/*
 * The fail path removes any maps that have been allocated. The fail path is
 * only called from code after maps has been kzalloc'd. It is also safe to
 * pass 'num_pins * maps_per_pin' as the map count even though we probably
 * failed before all the mappings were read as all maps are allocated at once,
 * and configs are only allocated for .type = PIN_MAP_TYPE_CONFIGS_PIN - there
 * is no failpath where a config can be allocated without .type being set.
 */
fail:
	wmt_pctl_dt_free_map(pctldev, maps, num_pins * maps_per_pin);
	return err;
}

static struct pinctrl_ops wmt_pctl_ops = {
	.get_groups_count = wmt_get_groups_count,
	.get_group_name	= wmt_get_group_name,
	.get_group_pins	= wmt_get_group_pins,
	.dt_node_to_map = wmt_pctl_dt_node_to_map,
	.dt_free_map = wmt_pctl_dt_free_map,
};

static int wmt_pinconf_get(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *config)
{
	return -ENOTSUPP;
}

static int wmt_pinconf_set(struct pinctrl_dev *pctldev, unsigned pin,
			   unsigned long *configs, unsigned num_configs)
{
	struct wmt_pinctrl_data *data = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u16 arg;
	u32 bank = WMT_BANK_FROM_PIN(pin);
	u32 bit = WMT_BIT_FROM_PIN(pin);
	u32 reg_pull_en = data->banks[bank].reg_pull_en;
	u32 reg_pull_cfg = data->banks[bank].reg_pull_cfg;
	int i;

	if ((reg_pull_en == NO_REG) || (reg_pull_cfg == NO_REG)) {
		dev_err(data->dev, "bias functions not supported on pin %d\n",
			pin);
		return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		if ((param == PIN_CONFIG_BIAS_PULL_DOWN) ||
		    (param == PIN_CONFIG_BIAS_PULL_UP)) {
			if (arg == 0)
				param = PIN_CONFIG_BIAS_DISABLE;
		}

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			wmt_clearbits(data, reg_pull_en, BIT(bit));
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			wmt_clearbits(data, reg_pull_cfg, BIT(bit));
			wmt_setbits(data, reg_pull_en, BIT(bit));
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			wmt_setbits(data, reg_pull_cfg, BIT(bit));
			wmt_setbits(data, reg_pull_en, BIT(bit));
			break;
		default:
			dev_err(data->dev, "unknown pinconf param\n");
			return -EINVAL;
		}
	} /* for each config */

	return 0;
}

static struct pinconf_ops wmt_pinconf_ops = {
	.pin_config_get = wmt_pinconf_get,
	.pin_config_set = wmt_pinconf_set,
};

static struct pinctrl_desc wmt_desc = {
	.owner = THIS_MODULE,
	.name = "pinctrl-wmt",
	.pctlops = &wmt_pctl_ops,
	.pmxops = &wmt_pinmux_ops,
	.confops = &wmt_pinconf_ops,
};

static int wmt_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void wmt_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static int wmt_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct wmt_pinctrl_data *data = dev_get_drvdata(chip->dev);
	u32 bank = WMT_BANK_FROM_PIN(offset);
	u32 bit = WMT_BIT_FROM_PIN(offset);
	u32 reg_dir = data->banks[bank].reg_dir;
	u32 val;

	val = readl_relaxed(data->base + reg_dir);
	if (val & BIT(bit))
		return GPIOF_DIR_OUT;
	else
		return GPIOF_DIR_IN;
}

static int wmt_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	struct wmt_pinctrl_data *data = dev_get_drvdata(chip->dev);
	u32 bank = WMT_BANK_FROM_PIN(offset);
	u32 bit = WMT_BIT_FROM_PIN(offset);
	u32 reg_data_in = data->banks[bank].reg_data_in;

	if (reg_data_in == NO_REG) {
		dev_err(data->dev, "no data in register defined\n");
		return -EINVAL;
	}

	return !!(readl_relaxed(data->base + reg_data_in) & BIT(bit));
}

static void wmt_gpio_set_value(struct gpio_chip *chip, unsigned offset,
			       int val)
{
	struct wmt_pinctrl_data *data = dev_get_drvdata(chip->dev);
	u32 bank = WMT_BANK_FROM_PIN(offset);
	u32 bit = WMT_BIT_FROM_PIN(offset);
	u32 reg_data_out = data->banks[bank].reg_data_out;

	if (reg_data_out == NO_REG) {
		dev_err(data->dev, "no data out register defined\n");
		return;
	}

	if (val)
		wmt_setbits(data, reg_data_out, BIT(bit));
	else
		wmt_clearbits(data, reg_data_out, BIT(bit));
}

static int wmt_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int wmt_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
				     int value)
{
	wmt_gpio_set_value(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static struct gpio_chip wmt_gpio_chip = {
	.label = "gpio-wmt",
	.owner = THIS_MODULE,
	.request = wmt_gpio_request,
	.free = wmt_gpio_free,
	.get_direction = wmt_gpio_get_direction,
	.direction_input = wmt_gpio_direction_input,
	.direction_output = wmt_gpio_direction_output,
	.get = wmt_gpio_get_value,
	.set = wmt_gpio_set_value,
	.can_sleep = false,
};

int wmt_pinctrl_probe(struct platform_device *pdev,
		      struct wmt_pinctrl_data *data)
{
	int err;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	wmt_desc.pins = data->pins;
	wmt_desc.npins = data->npins;

	data->gpio_chip = wmt_gpio_chip;
	data->gpio_chip.dev = &pdev->dev;
	data->gpio_chip.of_node = pdev->dev.of_node;
	data->gpio_chip.ngpio = data->nbanks * 32;

	platform_set_drvdata(pdev, data);

	data->dev = &pdev->dev;

	data->pctl_dev = pinctrl_register(&wmt_desc, &pdev->dev, data);
	if (!data->pctl_dev) {
		dev_err(&pdev->dev, "Failed to register pinctrl\n");
		return -EINVAL;
	}

	err = gpiochip_add(&data->gpio_chip);
	if (err) {
		dev_err(&pdev->dev, "could not add GPIO chip\n");
		goto fail_gpio;
	}

	err = gpiochip_add_pin_range(&data->gpio_chip, dev_name(data->dev),
				     0, 0, data->nbanks * 32);
	if (err)
		goto fail_range;

	dev_info(&pdev->dev, "Pin controller initialized\n");

	return 0;

fail_range:
	gpiochip_remove(&data->gpio_chip);
fail_gpio:
	pinctrl_unregister(data->pctl_dev);
	return err;
}

int wmt_pinctrl_remove(struct platform_device *pdev)
{
	struct wmt_pinctrl_data *data = platform_get_drvdata(pdev);

	gpiochip_remove(&data->gpio_chip);
	pinctrl_unregister(data->pctl_dev);

	return 0;
}
