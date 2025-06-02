// SPDX-License-Identifier: GPL-2.0+
//
// pin-controller/pin-mux/pin-config/gpio-driver for Samsung's SoC's.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
// Copyright (c) 2012 Linaro Ltd
//		http://www.linaro.org
//
// Author: Thomas Abraham <thomas.ab@samsung.com>
//
// This driver implements the Samsung pinctrl driver. It supports setting up of
// pinmux and pinconf configurations. The gpiolib interface is also included.
// External interrupt (gpio and wakeup) support are not included in this driver
// but provides extensions to which platform specific implementation of the gpio
// and wakeup interrupts can be hooked to.

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "../core.h"
#include "pinctrl-samsung.h"

/* maximum number of the memory resources */
#define	SAMSUNG_PINCTRL_NUM_RESOURCES	2

/* list of all possible config options supported */
static struct pin_config {
	const char *property;
	enum pincfg_type param;
} cfg_params[] = {
	{ "samsung,pin-pud", PINCFG_TYPE_PUD },
	{ "samsung,pin-drv", PINCFG_TYPE_DRV },
	{ "samsung,pin-con-pdn", PINCFG_TYPE_CON_PDN },
	{ "samsung,pin-pud-pdn", PINCFG_TYPE_PUD_PDN },
	{ "samsung,pin-val", PINCFG_TYPE_DAT },
};

static int samsung_get_group_count(struct pinctrl_dev *pctldev)
{
	struct samsung_pinctrl_drv_data *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->nr_groups;
}

static const char *samsung_get_group_name(struct pinctrl_dev *pctldev,
						unsigned group)
{
	struct samsung_pinctrl_drv_data *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pin_groups[group].name;
}

static int samsung_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned group,
					const unsigned **pins,
					unsigned *num_pins)
{
	struct samsung_pinctrl_drv_data *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = pmx->pin_groups[group].pins;
	*num_pins = pmx->pin_groups[group].num_pins;

	return 0;
}

static int reserve_map(struct device *dev, struct pinctrl_map **map,
		       unsigned *reserved_maps, unsigned *num_maps,
		       unsigned reserve)
{
	unsigned old_num = *reserved_maps;
	unsigned new_num = *num_maps + reserve;
	struct pinctrl_map *new_map;

	if (old_num >= new_num)
		return 0;

	new_map = krealloc(*map, sizeof(*new_map) * new_num, GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	memset(new_map + old_num, 0, (new_num - old_num) * sizeof(*new_map));

	*map = new_map;
	*reserved_maps = new_num;

	return 0;
}

static int add_map_mux(struct pinctrl_map **map, unsigned *reserved_maps,
		       unsigned *num_maps, const char *group,
		       const char *function)
{
	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = group;
	(*map)[*num_maps].data.mux.function = function;
	(*num_maps)++;

	return 0;
}

static int add_map_configs(struct device *dev, struct pinctrl_map **map,
			   unsigned *reserved_maps, unsigned *num_maps,
			   const char *group, unsigned long *configs,
			   unsigned num_configs)
{
	unsigned long *dup_configs;

	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	dup_configs = kmemdup_array(configs, num_configs, sizeof(*dup_configs),
				    GFP_KERNEL);
	if (!dup_configs)
		return -ENOMEM;

	(*map)[*num_maps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
	(*map)[*num_maps].data.configs.group_or_pin = group;
	(*map)[*num_maps].data.configs.configs = dup_configs;
	(*map)[*num_maps].data.configs.num_configs = num_configs;
	(*num_maps)++;

	return 0;
}

static int add_config(struct device *dev, unsigned long **configs,
		      unsigned *num_configs, unsigned long config)
{
	unsigned old_num = *num_configs;
	unsigned new_num = old_num + 1;
	unsigned long *new_configs;

	new_configs = krealloc(*configs, sizeof(*new_configs) * new_num,
			       GFP_KERNEL);
	if (!new_configs)
		return -ENOMEM;

	new_configs[old_num] = config;

	*configs = new_configs;
	*num_configs = new_num;

	return 0;
}

static void samsung_dt_free_map(struct pinctrl_dev *pctldev,
				      struct pinctrl_map *map,
				      unsigned num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);

	kfree(map);
}

static int samsung_dt_subnode_to_map(struct samsung_pinctrl_drv_data *drvdata,
				     struct device *dev,
				     struct device_node *np,
				     struct pinctrl_map **map,
				     unsigned *reserved_maps,
				     unsigned *num_maps)
{
	int ret, i;
	u32 val;
	unsigned long config;
	unsigned long *configs = NULL;
	unsigned num_configs = 0;
	unsigned reserve;
	struct property *prop;
	const char *group;
	bool has_func = false;

	ret = of_property_read_u32(np, "samsung,pin-function", &val);
	if (!ret)
		has_func = true;

	for (i = 0; i < ARRAY_SIZE(cfg_params); i++) {
		ret = of_property_read_u32(np, cfg_params[i].property, &val);
		if (!ret) {
			config = PINCFG_PACK(cfg_params[i].param, val);
			ret = add_config(dev, &configs, &num_configs, config);
			if (ret < 0)
				goto exit;
		/* EINVAL=missing, which is fine since it's optional */
		} else if (ret != -EINVAL) {
			dev_err(dev, "could not parse property %s\n",
				cfg_params[i].property);
		}
	}

	reserve = 0;
	if (has_func)
		reserve++;
	if (num_configs)
		reserve++;
	ret = of_property_count_strings(np, "samsung,pins");
	if (ret < 0) {
		dev_err(dev, "could not parse property samsung,pins\n");
		goto exit;
	}
	reserve *= ret;

	ret = reserve_map(dev, map, reserved_maps, num_maps, reserve);
	if (ret < 0)
		goto exit;

	of_property_for_each_string(np, "samsung,pins", prop, group) {
		if (has_func) {
			ret = add_map_mux(map, reserved_maps,
						num_maps, group, np->full_name);
			if (ret < 0)
				goto exit;
		}

		if (num_configs) {
			ret = add_map_configs(dev, map, reserved_maps,
					      num_maps, group, configs,
					      num_configs);
			if (ret < 0)
				goto exit;
		}
	}

	ret = 0;

exit:
	kfree(configs);
	return ret;
}

static int samsung_dt_node_to_map(struct pinctrl_dev *pctldev,
					struct device_node *np_config,
					struct pinctrl_map **map,
					unsigned *num_maps)
{
	struct samsung_pinctrl_drv_data *drvdata;
	unsigned reserved_maps;
	int ret;

	drvdata = pinctrl_dev_get_drvdata(pctldev);

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	if (!of_get_child_count(np_config))
		return samsung_dt_subnode_to_map(drvdata, pctldev->dev,
							np_config, map,
							&reserved_maps,
							num_maps);

	for_each_child_of_node_scoped(np_config, np) {
		ret = samsung_dt_subnode_to_map(drvdata, pctldev->dev, np, map,
						&reserved_maps, num_maps);
		if (ret < 0) {
			samsung_dt_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
/* Forward declaration which can be used by samsung_pin_dbg_show */
static int samsung_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
					unsigned long *config);
static const char * const reg_names[] = {"CON", "DAT", "PUD", "DRV", "CON_PDN",
					 "PUD_PDN"};

static void samsung_pin_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int pin)
{
	enum pincfg_type cfg_type;
	unsigned long config;
	int ret;

	for (cfg_type = 0; cfg_type < PINCFG_TYPE_NUM; cfg_type++) {
		config = PINCFG_PACK(cfg_type, 0);
		ret = samsung_pinconf_get(pctldev, pin, &config);
		if (ret < 0)
			continue;

		seq_printf(s, " %s(0x%lx)", reg_names[cfg_type],
			   PINCFG_UNPACK_VALUE(config));
	}
}
#endif

/* list of pinctrl callbacks for the pinctrl core */
static const struct pinctrl_ops samsung_pctrl_ops = {
	.get_groups_count	= samsung_get_group_count,
	.get_group_name		= samsung_get_group_name,
	.get_group_pins		= samsung_get_group_pins,
	.dt_node_to_map		= samsung_dt_node_to_map,
	.dt_free_map		= samsung_dt_free_map,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show		= samsung_pin_dbg_show,
#endif
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
static void pin_to_reg_bank(struct samsung_pinctrl_drv_data *drvdata,
			unsigned pin, void __iomem **reg, u32 *offset,
			struct samsung_pin_bank **bank)
{
	struct samsung_pin_bank *b;

	b = drvdata->pin_banks;

	while ((pin >= b->pin_base) &&
			((b->pin_base + b->nr_pins - 1) < pin))
		b++;

	*reg = b->pctl_base + b->pctl_offset;
	*offset = pin - b->pin_base;
	if (bank)
		*bank = b;
}

/* enable or disable a pinmux function */
static int samsung_pinmux_setup(struct pinctrl_dev *pctldev, unsigned selector,
				unsigned group)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const struct samsung_pin_bank_type *type;
	struct samsung_pin_bank *bank;
	void __iomem *reg;
	u32 mask, shift, data, pin_offset;
	unsigned long flags;
	const struct samsung_pmx_func *func;
	const struct samsung_pin_group *grp;
	int ret;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	func = &drvdata->pmx_functions[selector];
	grp = &drvdata->pin_groups[group];

	pin_to_reg_bank(drvdata, grp->pins[0], &reg, &pin_offset, &bank);
	type = bank->type;
	mask = (1 << type->fld_width[PINCFG_TYPE_FUNC]) - 1;
	shift = pin_offset * type->fld_width[PINCFG_TYPE_FUNC];
	if (shift >= 32) {
		/* Some banks have two config registers */
		shift -= 32;
		reg += 4;
	}

	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(pctldev->dev, "failed to enable clock for setup\n");
		return ret;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);

	data = readl(reg + type->reg_offset[PINCFG_TYPE_FUNC]);
	data &= ~(mask << shift);
	data |= func->val << shift;
	writel(data, reg + type->reg_offset[PINCFG_TYPE_FUNC]);

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	clk_disable(drvdata->pclk);

	return 0;
}

/* enable a specified pinmux by writing to registers */
static int samsung_pinmux_set_mux(struct pinctrl_dev *pctldev,
				  unsigned selector,
				  unsigned group)
{
	return samsung_pinmux_setup(pctldev, selector, group);
}

/* list of pinmux callbacks for the pinmux vertical in pinctrl core */
static const struct pinmux_ops samsung_pinmux_ops = {
	.get_functions_count	= samsung_get_functions_count,
	.get_function_name	= samsung_pinmux_get_fname,
	.get_function_groups	= samsung_pinmux_get_groups,
	.set_mux		= samsung_pinmux_set_mux,
};

/* set or get the pin config settings for a specified pin */
static int samsung_pinconf_rw(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *config, bool set)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const struct samsung_pin_bank_type *type;
	struct samsung_pin_bank *bank;
	void __iomem *reg_base;
	enum pincfg_type cfg_type = PINCFG_UNPACK_TYPE(*config);
	u32 data, width, pin_offset, mask, shift;
	u32 cfg_value, cfg_reg;
	unsigned long flags;
	int ret;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	pin_to_reg_bank(drvdata, pin, &reg_base, &pin_offset, &bank);
	type = bank->type;

	if (cfg_type >= PINCFG_TYPE_NUM || !type->fld_width[cfg_type])
		return -EINVAL;

	width = type->fld_width[cfg_type];
	cfg_reg = type->reg_offset[cfg_type];

	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(drvdata->dev, "failed to enable clock\n");
		return ret;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);

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

	raw_spin_unlock_irqrestore(&bank->slock, flags);

	clk_disable(drvdata->pclk);

	return 0;
}

/* set the pin config settings for a specified pin */
static int samsung_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned num_configs)
{
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		ret = samsung_pinconf_rw(pctldev, pin, &configs[i], true);
		if (ret < 0)
			return ret;
	} /* for each config */

	return 0;
}

/* get the pin config settings for a specified pin */
static int samsung_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
					unsigned long *config)
{
	return samsung_pinconf_rw(pctldev, pin, config, false);
}

/* set the pin config settings for a specified pin group */
static int samsung_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long *configs,
			unsigned num_configs)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const unsigned int *pins;
	unsigned int cnt;

	drvdata = pinctrl_dev_get_drvdata(pctldev);
	pins = drvdata->pin_groups[group].pins;

	for (cnt = 0; cnt < drvdata->pin_groups[group].num_pins; cnt++)
		samsung_pinconf_set(pctldev, pins[cnt], configs, num_configs);

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
static const struct pinconf_ops samsung_pinconf_ops = {
	.pin_config_get		= samsung_pinconf_get,
	.pin_config_set		= samsung_pinconf_set,
	.pin_config_group_get	= samsung_pinconf_group_get,
	.pin_config_group_set	= samsung_pinconf_group_set,
};

/*
 * The samsung_gpio_set_vlaue() should be called with "bank->slock" held
 * to avoid race condition.
 */
static void samsung_gpio_set_value(struct gpio_chip *gc,
					  unsigned offset, int value)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	const struct samsung_pin_bank_type *type = bank->type;
	void __iomem *reg;
	u32 data;

	reg = bank->pctl_base + bank->pctl_offset;

	data = readl(reg + type->reg_offset[PINCFG_TYPE_DAT]);
	data &= ~(1 << offset);
	if (value)
		data |= 1 << offset;
	writel(data, reg + type->reg_offset[PINCFG_TYPE_DAT]);
}

/* gpiolib gpio_set callback function */
static void samsung_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	struct samsung_pinctrl_drv_data *drvdata = bank->drvdata;
	unsigned long flags;

	if (clk_enable(drvdata->pclk)) {
		dev_err(drvdata->dev, "failed to enable clock\n");
		return;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);
	samsung_gpio_set_value(gc, offset, value);
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	clk_disable(drvdata->pclk);
}

/* gpiolib gpio_get callback function */
static int samsung_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	const void __iomem *reg;
	u32 data;
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	const struct samsung_pin_bank_type *type = bank->type;
	struct samsung_pinctrl_drv_data *drvdata = bank->drvdata;
	int ret;

	reg = bank->pctl_base + bank->pctl_offset;

	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(drvdata->dev, "failed to enable clock\n");
		return ret;
	}

	data = readl(reg + type->reg_offset[PINCFG_TYPE_DAT]);
	data >>= offset;
	data &= 1;

	clk_disable(drvdata->pclk);

	return data;
}

/*
 * The samsung_gpio_set_direction() should be called with "bank->slock" held
 * to avoid race condition.
 * The calls to gpio_direction_output() and gpio_direction_input()
 * leads to this function call.
 */
static int samsung_gpio_set_direction(struct gpio_chip *gc,
					     unsigned offset, bool input)
{
	const struct samsung_pin_bank_type *type;
	struct samsung_pin_bank *bank;
	void __iomem *reg;
	u32 data, mask, shift;

	bank = gpiochip_get_data(gc);
	type = bank->type;

	reg = bank->pctl_base + bank->pctl_offset
			+ type->reg_offset[PINCFG_TYPE_FUNC];

	mask = (1 << type->fld_width[PINCFG_TYPE_FUNC]) - 1;
	shift = offset * type->fld_width[PINCFG_TYPE_FUNC];
	if (shift >= 32) {
		/* Some banks have two config registers */
		shift -= 32;
		reg += 4;
	}

	data = readl(reg);
	data &= ~(mask << shift);
	if (!input)
		data |= PIN_CON_FUNC_OUTPUT << shift;
	writel(data, reg);

	return 0;
}

/* gpiolib gpio_direction_input callback function. */
static int samsung_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	struct samsung_pinctrl_drv_data *drvdata = bank->drvdata;
	unsigned long flags;
	int ret;

	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(drvdata->dev, "failed to enable clock\n");
		return ret;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);
	ret = samsung_gpio_set_direction(gc, offset, true);
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	clk_disable(drvdata->pclk);

	return ret;
}

/* gpiolib gpio_direction_output callback function. */
static int samsung_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
							int value)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	struct samsung_pinctrl_drv_data *drvdata = bank->drvdata;
	unsigned long flags;
	int ret;

	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(drvdata->dev, "failed to enable clock\n");
		return ret;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);
	samsung_gpio_set_value(gc, offset, value);
	ret = samsung_gpio_set_direction(gc, offset, false);
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	clk_disable(drvdata->pclk);

	return ret;
}

/*
 * gpiod_to_irq() callback function. Creates a mapping between a GPIO pin
 * and a virtual IRQ, if not already present.
 */
static int samsung_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	unsigned int virq;

	if (!bank->irq_domain)
		return -ENXIO;

	virq = irq_create_mapping(bank->irq_domain, offset);

	return (virq) ? : -ENXIO;
}

static int samsung_add_pin_ranges(struct gpio_chip *gc)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);

	bank->grange.name = bank->name;
	bank->grange.id = bank->id;
	bank->grange.pin_base = bank->pin_base;
	bank->grange.base = gc->base;
	bank->grange.npins = bank->nr_pins;
	bank->grange.gc = &bank->gpio_chip;
	pinctrl_add_gpio_range(bank->drvdata->pctl_dev, &bank->grange);

	return 0;
}

static struct samsung_pin_group *samsung_pinctrl_create_groups(
				struct device *dev,
				struct samsung_pinctrl_drv_data *drvdata,
				unsigned int *cnt)
{
	struct pinctrl_desc *ctrldesc = &drvdata->pctl;
	struct samsung_pin_group *groups, *grp;
	const struct pinctrl_pin_desc *pdesc;
	int i;

	groups = devm_kcalloc(dev, ctrldesc->npins, sizeof(*groups),
				GFP_KERNEL);
	if (!groups)
		return ERR_PTR(-EINVAL);
	grp = groups;

	pdesc = ctrldesc->pins;
	for (i = 0; i < ctrldesc->npins; ++i, ++pdesc, ++grp) {
		grp->name = pdesc->name;
		grp->pins = &pdesc->number;
		grp->num_pins = 1;
	}

	*cnt = ctrldesc->npins;
	return groups;
}

static int samsung_pinctrl_create_function(struct device *dev,
				struct samsung_pinctrl_drv_data *drvdata,
				struct device_node *func_np,
				struct samsung_pmx_func *func)
{
	int npins;
	int ret;
	int i;

	if (of_property_read_u32(func_np, "samsung,pin-function", &func->val))
		return 0;

	npins = of_property_count_strings(func_np, "samsung,pins");
	if (npins < 1) {
		dev_err(dev, "invalid pin list in %pOFn node", func_np);
		return -EINVAL;
	}

	func->name = func_np->full_name;

	func->groups = devm_kcalloc(dev, npins, sizeof(char *), GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	for (i = 0; i < npins; ++i) {
		const char *gname;

		ret = of_property_read_string_index(func_np, "samsung,pins",
							i, &gname);
		if (ret) {
			dev_err(dev,
				"failed to read pin name %d from %pOFn node\n",
				i, func_np);
			return ret;
		}

		func->groups[i] = gname;
	}

	func->num_groups = npins;
	return 1;
}

static struct samsung_pmx_func *samsung_pinctrl_create_functions(
				struct device *dev,
				struct samsung_pinctrl_drv_data *drvdata,
				unsigned int *cnt)
{
	struct samsung_pmx_func *functions, *func;
	struct device_node *dev_np = dev->of_node;
	struct device_node *cfg_np;
	unsigned int func_cnt = 0;
	int ret;

	/*
	 * Iterate over all the child nodes of the pin controller node
	 * and create pin groups and pin function lists.
	 */
	for_each_child_of_node(dev_np, cfg_np) {
		struct device_node *func_np;

		if (!of_get_child_count(cfg_np)) {
			if (!of_property_present(cfg_np,
			    "samsung,pin-function"))
				continue;
			++func_cnt;
			continue;
		}

		for_each_child_of_node(cfg_np, func_np) {
			if (!of_property_present(func_np,
			    "samsung,pin-function"))
				continue;
			++func_cnt;
		}
	}

	functions = devm_kcalloc(dev, func_cnt, sizeof(*functions),
					GFP_KERNEL);
	if (!functions)
		return ERR_PTR(-ENOMEM);
	func = functions;

	/*
	 * Iterate over all the child nodes of the pin controller node
	 * and create pin groups and pin function lists.
	 */
	func_cnt = 0;
	for_each_child_of_node_scoped(dev_np, cfg_np) {
		if (!of_get_child_count(cfg_np)) {
			ret = samsung_pinctrl_create_function(dev, drvdata,
							cfg_np, func);
			if (ret < 0)
				return ERR_PTR(ret);
			if (ret > 0) {
				++func;
				++func_cnt;
			}
			continue;
		}

		for_each_child_of_node_scoped(cfg_np, func_np) {
			ret = samsung_pinctrl_create_function(dev, drvdata,
						func_np, func);
			if (ret < 0)
				return ERR_PTR(ret);
			if (ret > 0) {
				++func;
				++func_cnt;
			}
		}
	}

	*cnt = func_cnt;
	return functions;
}

/*
 * Parse the information about all the available pin groups and pin functions
 * from device node of the pin-controller. A pin group is formed with all
 * the pins listed in the "samsung,pins" property.
 */

static int samsung_pinctrl_parse_dt(struct platform_device *pdev,
				    struct samsung_pinctrl_drv_data *drvdata)
{
	struct device *dev = &pdev->dev;
	struct samsung_pin_group *groups;
	struct samsung_pmx_func *functions;
	unsigned int grp_cnt = 0, func_cnt = 0;

	groups = samsung_pinctrl_create_groups(dev, drvdata, &grp_cnt);
	if (IS_ERR(groups)) {
		dev_err(dev, "failed to parse pin groups\n");
		return PTR_ERR(groups);
	}

	functions = samsung_pinctrl_create_functions(dev, drvdata, &func_cnt);
	if (IS_ERR(functions)) {
		dev_err(dev, "failed to parse pin functions\n");
		return PTR_ERR(functions);
	}

	drvdata->pin_groups = groups;
	drvdata->nr_groups = grp_cnt;
	drvdata->pmx_functions = functions;
	drvdata->nr_functions = func_cnt;

	return 0;
}

/* register the pinctrl interface with the pinctrl subsystem */
static int samsung_pinctrl_register(struct platform_device *pdev,
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

	pindesc = devm_kcalloc(&pdev->dev,
			       drvdata->nr_pins, sizeof(*pindesc),
			       GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;
	ctrldesc->pins = pindesc;
	ctrldesc->npins = drvdata->nr_pins;

	/* dynamically populate the pin number and pin name for pindesc */
	for (pin = 0, pdesc = pindesc; pin < ctrldesc->npins; pin++, pdesc++)
		pdesc->number = pin;

	/*
	 * allocate space for storing the dynamically generated names for all
	 * the pins which belong to this pin-controller.
	 */
	pin_names = devm_kzalloc(&pdev->dev,
				 array3_size(sizeof(char), PIN_NAME_LENGTH,
					     drvdata->nr_pins),
				 GFP_KERNEL);
	if (!pin_names)
		return -ENOMEM;

	/* for each pin, the name of the pin is pin-bank name + pin number */
	for (bank = 0; bank < drvdata->nr_banks; bank++) {
		pin_bank = &drvdata->pin_banks[bank];
		pin_bank->id = bank;
		for (pin = 0; pin < pin_bank->nr_pins; pin++) {
			sprintf(pin_names, "%s-%d", pin_bank->name, pin);
			pdesc = pindesc + pin_bank->pin_base + pin;
			pdesc->name = pin_names;
			pin_names += PIN_NAME_LENGTH;
		}
	}

	ret = samsung_pinctrl_parse_dt(pdev, drvdata);
	if (ret)
		return ret;

	ret = devm_pinctrl_register_and_init(&pdev->dev, ctrldesc, drvdata,
					     &drvdata->pctl_dev);
	if (ret) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return ret;
	}

	return 0;
}

/* unregister the pinctrl interface with the pinctrl subsystem */
static int samsung_pinctrl_unregister(struct platform_device *pdev,
				      struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank)
		pinctrl_remove_gpio_range(drvdata->pctl_dev, &bank->grange);

	return 0;
}

static void samsung_pud_value_init(struct samsung_pinctrl_drv_data *drvdata)
{
	unsigned int  *pud_val = drvdata->pud_val;

	pud_val[PUD_PULL_DISABLE] = EXYNOS_PIN_PUD_PULL_DISABLE;
	pud_val[PUD_PULL_DOWN] = EXYNOS_PIN_PID_PULL_DOWN;
	pud_val[PUD_PULL_UP] = EXYNOS_PIN_PID_PULL_UP;
}

/*
 * Enable or Disable the pull-down and pull-up for the gpio pins in the
 * PUD register.
 */
static void samsung_gpio_set_pud(struct gpio_chip *gc, unsigned int offset,
				 unsigned int value)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	const struct samsung_pin_bank_type *type = bank->type;
	void __iomem *reg;
	unsigned int data, mask;

	reg = bank->pctl_base + bank->pctl_offset;
	data = readl(reg + type->reg_offset[PINCFG_TYPE_PUD]);
	mask = (1 << type->fld_width[PINCFG_TYPE_PUD]) - 1;
	data &= ~(mask << (offset * type->fld_width[PINCFG_TYPE_PUD]));
	data |= value << (offset * type->fld_width[PINCFG_TYPE_PUD]);
	writel(data, reg + type->reg_offset[PINCFG_TYPE_PUD]);
}

/*
 * Identify the type of PUD config based on the gpiolib request to enable
 * or disable the PUD config.
 */
static int samsung_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				   unsigned long config)
{
	struct samsung_pin_bank *bank = gpiochip_get_data(gc);
	struct samsung_pinctrl_drv_data *drvdata = bank->drvdata;
	unsigned int value;
	int ret = 0;
	unsigned long flags;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_DISABLE:
		value = drvdata->pud_val[PUD_PULL_DISABLE];
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		value = drvdata->pud_val[PUD_PULL_DOWN];
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		value = drvdata->pud_val[PUD_PULL_UP];
		break;
	default:
		return -ENOTSUPP;
	}

	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(drvdata->dev, "failed to enable clock\n");
		return ret;
	}

	raw_spin_lock_irqsave(&bank->slock, flags);
	samsung_gpio_set_pud(gc, offset, value);
	raw_spin_unlock_irqrestore(&bank->slock, flags);

	clk_disable(drvdata->pclk);

	return ret;
}

static const struct gpio_chip samsung_gpiolib_chip = {
	.request = gpiochip_generic_request,
	.free = gpiochip_generic_free,
	.set = samsung_gpio_set,
	.get = samsung_gpio_get,
	.direction_input = samsung_gpio_direction_input,
	.direction_output = samsung_gpio_direction_output,
	.to_irq = samsung_gpio_to_irq,
	.add_pin_ranges = samsung_add_pin_ranges,
	.set_config = samsung_gpio_set_config,
	.owner = THIS_MODULE,
};

/* register the gpiolib interface with the gpiolib subsystem */
static int samsung_gpiolib_register(struct platform_device *pdev,
				    struct samsung_pinctrl_drv_data *drvdata)
{
	struct samsung_pin_bank *bank = drvdata->pin_banks;
	struct gpio_chip *gc;
	int ret;
	int i;

	for (i = 0; i < drvdata->nr_banks; ++i, ++bank) {
		bank->gpio_chip = samsung_gpiolib_chip;

		gc = &bank->gpio_chip;
		gc->base = -1; /* Dynamic allocation */
		gc->ngpio = bank->nr_pins;
		gc->parent = &pdev->dev;
		gc->fwnode = bank->fwnode;
		gc->label = bank->name;

		ret = devm_gpiochip_add_data(&pdev->dev, gc, bank);
		if (ret) {
			dev_err(&pdev->dev, "failed to register gpio_chip %s, error code: %d\n",
							gc->label, ret);
			return ret;
		}
	}

	return 0;
}

static const struct samsung_pin_ctrl *
samsung_pinctrl_get_soc_data_for_of_alias(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct samsung_pinctrl_of_match_data *of_data;
	int id;

	id = of_alias_get_id(node, "pinctrl");
	if (id < 0) {
		dev_err(&pdev->dev, "failed to get alias id\n");
		return NULL;
	}

	of_data = of_device_get_match_data(&pdev->dev);
	if (id >= of_data->num_ctrl) {
		dev_err(&pdev->dev, "invalid alias id %d\n", id);
		return NULL;
	}

	return &(of_data->ctrl[id]);
}

static void samsung_banks_node_put(struct samsung_pinctrl_drv_data *d)
{
	struct samsung_pin_bank *bank;
	unsigned int i;

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank)
		fwnode_handle_put(bank->fwnode);
}

/*
 * Iterate over all driver pin banks to find one matching the name of node,
 * skipping optional "-gpio" node suffix. When found, assign node to the bank.
 */
static void samsung_banks_node_get(struct device *dev, struct samsung_pinctrl_drv_data *d)
{
	const char *suffix = "-gpio-bank";
	struct samsung_pin_bank *bank;
	struct fwnode_handle *child;
	/* Pin bank names are up to 4 characters */
	char node_name[20];
	unsigned int i;
	size_t len;

	bank = d->pin_banks;
	for (i = 0; i < d->nr_banks; ++i, ++bank) {
		strscpy(node_name, bank->name, sizeof(node_name));
		len = strlcat(node_name, suffix, sizeof(node_name));
		if (len >= sizeof(node_name)) {
			dev_err(dev, "Too long pin bank name '%s', ignoring\n",
				bank->name);
			continue;
		}

		for_each_gpiochip_node(dev, child) {
			struct device_node *np = to_of_node(child);

			if (of_node_name_eq(np, node_name))
				break;
			if (of_node_name_eq(np, bank->name))
				break;
		}

		if (child)
			bank->fwnode = child;
		else
			dev_warn(dev, "Missing node for bank %s - invalid DTB\n",
				 bank->name);
		/* child reference dropped in samsung_banks_node_put() */
	}
}

/* retrieve the soc specific data */
static const struct samsung_pin_ctrl *
samsung_pinctrl_get_soc_data(struct samsung_pinctrl_drv_data *d,
			     struct platform_device *pdev)
{
	const struct samsung_pin_bank_data *bdata;
	const struct samsung_pin_ctrl *ctrl;
	struct samsung_pin_bank *bank;
	struct resource *res;
	void __iomem *virt_base[SAMSUNG_PINCTRL_NUM_RESOURCES];
	unsigned int i;

	ctrl = samsung_pinctrl_get_soc_data_for_of_alias(pdev);
	if (!ctrl)
		return ERR_PTR(-ENOENT);

	d->suspend = ctrl->suspend;
	d->resume = ctrl->resume;
	d->nr_banks = ctrl->nr_banks;
	d->pin_banks = devm_kcalloc(&pdev->dev, d->nr_banks,
					sizeof(*d->pin_banks), GFP_KERNEL);
	if (!d->pin_banks)
		return ERR_PTR(-ENOMEM);

	if (ctrl->nr_ext_resources + 1 > SAMSUNG_PINCTRL_NUM_RESOURCES)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < ctrl->nr_ext_resources + 1; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (!res) {
			dev_err(&pdev->dev, "failed to get mem%d resource\n", i);
			return ERR_PTR(-EINVAL);
		}
		virt_base[i] = devm_ioremap(&pdev->dev, res->start,
						resource_size(res));
		if (!virt_base[i]) {
			dev_err(&pdev->dev, "failed to ioremap %pR\n", res);
			return ERR_PTR(-EIO);
		}
	}

	bank = d->pin_banks;
	bdata = ctrl->pin_banks;
	for (i = 0; i < ctrl->nr_banks; ++i, ++bdata, ++bank) {
		bank->type = bdata->type;
		bank->pctl_offset = bdata->pctl_offset;
		bank->nr_pins = bdata->nr_pins;
		bank->eint_func = bdata->eint_func;
		bank->eint_type = bdata->eint_type;
		bank->eint_mask = bdata->eint_mask;
		bank->eint_offset = bdata->eint_offset;
		bank->eint_con_offset = bdata->eint_con_offset;
		bank->eint_mask_offset = bdata->eint_mask_offset;
		bank->eint_pend_offset = bdata->eint_pend_offset;
		bank->eint_fltcon_offset = bdata->eint_fltcon_offset;
		bank->name = bdata->name;

		raw_spin_lock_init(&bank->slock);
		bank->drvdata = d;
		bank->pin_base = d->nr_pins;
		d->nr_pins += bank->nr_pins;

		bank->eint_base = virt_base[0];
		bank->pctl_base = virt_base[bdata->pctl_res_idx];
	}
	/*
	 * Legacy platforms should provide only one resource with IO memory.
	 * Store it as virt_base because legacy driver needs to access it
	 * through samsung_pinctrl_drv_data.
	 */
	d->virt_base = virt_base[0];

	samsung_banks_node_get(&pdev->dev, d);

	return ctrl;
}

static int samsung_pinctrl_probe(struct platform_device *pdev)
{
	struct samsung_pinctrl_drv_data *drvdata;
	const struct samsung_pin_ctrl *ctrl;
	struct device *dev = &pdev->dev;
	int ret;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	ctrl = samsung_pinctrl_get_soc_data(drvdata, pdev);
	if (IS_ERR(ctrl)) {
		dev_err(&pdev->dev, "driver data not available\n");
		return PTR_ERR(ctrl);
	}
	drvdata->dev = dev;

	ret = platform_get_irq_optional(pdev, 0);
	if (ret < 0 && ret != -ENXIO)
		goto err_put_banks;
	if (ret > 0)
		drvdata->irq = ret;

	if (ctrl->retention_data) {
		drvdata->retention_ctrl = ctrl->retention_data->init(drvdata,
							  ctrl->retention_data);
		if (IS_ERR(drvdata->retention_ctrl)) {
			ret = PTR_ERR(drvdata->retention_ctrl);
			goto err_put_banks;
		}
	}

	drvdata->pclk = devm_clk_get_optional_prepared(dev, "pclk");
	if (IS_ERR(drvdata->pclk)) {
		ret = PTR_ERR(drvdata->pclk);
		goto err_put_banks;
	}

	ret = samsung_pinctrl_register(pdev, drvdata);
	if (ret)
		goto err_put_banks;

	if (ctrl->eint_gpio_init)
		ctrl->eint_gpio_init(drvdata);
	if (ctrl->eint_wkup_init)
		ctrl->eint_wkup_init(drvdata);

	if (ctrl->pud_value_init)
		ctrl->pud_value_init(drvdata);
	else
		samsung_pud_value_init(drvdata);

	ret = samsung_gpiolib_register(pdev, drvdata);
	if (ret)
		goto err_unregister;

	ret = pinctrl_enable(drvdata->pctl_dev);
	if (ret)
		goto err_unregister;

	platform_set_drvdata(pdev, drvdata);

	return 0;

err_unregister:
	samsung_pinctrl_unregister(pdev, drvdata);
err_put_banks:
	samsung_banks_node_put(drvdata);
	return ret;
}

/*
 * samsung_pinctrl_suspend - save pinctrl state for suspend
 *
 * Save data for all banks handled by this device.
 */
static int __maybe_unused samsung_pinctrl_suspend(struct device *dev)
{
	struct samsung_pinctrl_drv_data *drvdata = dev_get_drvdata(dev);
	int i;

	i = clk_enable(drvdata->pclk);
	if (i) {
		dev_err(drvdata->dev,
			"failed to enable clock for saving state\n");
		return i;
	}

	for (i = 0; i < drvdata->nr_banks; i++) {
		struct samsung_pin_bank *bank = &drvdata->pin_banks[i];
		const void __iomem *reg = bank->pctl_base + bank->pctl_offset;
		const u8 *offs = bank->type->reg_offset;
		const u8 *widths = bank->type->fld_width;
		enum pincfg_type type;

		/* Registers without a powerdown config aren't lost */
		if (!widths[PINCFG_TYPE_CON_PDN])
			continue;

		for (type = 0; type < PINCFG_TYPE_NUM; type++)
			if (widths[type])
				bank->pm_save[type] = readl(reg + offs[type]);

		if (widths[PINCFG_TYPE_FUNC] * bank->nr_pins > 32) {
			/* Some banks have two config registers */
			bank->pm_save[PINCFG_TYPE_NUM] =
				readl(reg + offs[PINCFG_TYPE_FUNC] + 4);
			pr_debug("Save %s @ %p (con %#010x %08x)\n",
				 bank->name, reg,
				 bank->pm_save[PINCFG_TYPE_FUNC],
				 bank->pm_save[PINCFG_TYPE_NUM]);
		} else {
			pr_debug("Save %s @ %p (con %#010x)\n", bank->name,
				 reg, bank->pm_save[PINCFG_TYPE_FUNC]);
		}
	}

	clk_disable(drvdata->pclk);

	if (drvdata->suspend)
		drvdata->suspend(drvdata);
	if (drvdata->retention_ctrl && drvdata->retention_ctrl->enable)
		drvdata->retention_ctrl->enable(drvdata);

	return 0;
}

/*
 * samsung_pinctrl_resume - restore pinctrl state from suspend
 *
 * Restore one of the banks that was saved during suspend.
 *
 * We don't bother doing anything complicated to avoid glitching lines since
 * we're called before pad retention is turned off.
 */
static int __maybe_unused samsung_pinctrl_resume(struct device *dev)
{
	struct samsung_pinctrl_drv_data *drvdata = dev_get_drvdata(dev);
	int ret;
	int i;

	/*
	 * enable clock before the callback, as we don't want to have to deal
	 * with callback cleanup on clock failures.
	 */
	ret = clk_enable(drvdata->pclk);
	if (ret) {
		dev_err(drvdata->dev,
			"failed to enable clock for restoring state\n");
		return ret;
	}

	if (drvdata->resume)
		drvdata->resume(drvdata);

	for (i = 0; i < drvdata->nr_banks; i++) {
		struct samsung_pin_bank *bank = &drvdata->pin_banks[i];
		void __iomem *reg = bank->pctl_base + bank->pctl_offset;
		const u8 *offs = bank->type->reg_offset;
		const u8 *widths = bank->type->fld_width;
		enum pincfg_type type;

		/* Registers without a powerdown config aren't lost */
		if (!widths[PINCFG_TYPE_CON_PDN])
			continue;

		if (widths[PINCFG_TYPE_FUNC] * bank->nr_pins > 32) {
			/* Some banks have two config registers */
			pr_debug("%s @ %p (con %#010x %08x => %#010x %08x)\n",
				 bank->name, reg,
				 readl(reg + offs[PINCFG_TYPE_FUNC]),
				 readl(reg + offs[PINCFG_TYPE_FUNC] + 4),
				 bank->pm_save[PINCFG_TYPE_FUNC],
				 bank->pm_save[PINCFG_TYPE_NUM]);
			writel(bank->pm_save[PINCFG_TYPE_NUM],
			       reg + offs[PINCFG_TYPE_FUNC] + 4);
		} else {
			pr_debug("%s @ %p (con %#010x => %#010x)\n", bank->name,
				 reg, readl(reg + offs[PINCFG_TYPE_FUNC]),
				 bank->pm_save[PINCFG_TYPE_FUNC]);
		}
		for (type = 0; type < PINCFG_TYPE_NUM; type++)
			if (widths[type])
				writel(bank->pm_save[type], reg + offs[type]);
	}

	clk_disable(drvdata->pclk);

	if (drvdata->retention_ctrl && drvdata->retention_ctrl->disable)
		drvdata->retention_ctrl->disable(drvdata);

	return 0;
}

static const struct of_device_id samsung_pinctrl_dt_match[] = {
#ifdef CONFIG_PINCTRL_EXYNOS_ARM
	{ .compatible = "samsung,exynos3250-pinctrl",
		.data = &exynos3250_of_data },
	{ .compatible = "samsung,exynos4210-pinctrl",
		.data = &exynos4210_of_data },
	{ .compatible = "samsung,exynos4x12-pinctrl",
		.data = &exynos4x12_of_data },
	{ .compatible = "samsung,exynos5250-pinctrl",
		.data = &exynos5250_of_data },
	{ .compatible = "samsung,exynos5260-pinctrl",
		.data = &exynos5260_of_data },
	{ .compatible = "samsung,exynos5410-pinctrl",
		.data = &exynos5410_of_data },
	{ .compatible = "samsung,exynos5420-pinctrl",
		.data = &exynos5420_of_data },
	{ .compatible = "samsung,s5pv210-pinctrl",
		.data = &s5pv210_of_data },
#endif
#ifdef CONFIG_PINCTRL_EXYNOS_ARM64
	{ .compatible = "google,gs101-pinctrl",
		.data = &gs101_of_data },
	{ .compatible = "samsung,exynos2200-pinctrl",
		.data = &exynos2200_of_data },
	{ .compatible = "samsung,exynos5433-pinctrl",
		.data = &exynos5433_of_data },
	{ .compatible = "samsung,exynos7-pinctrl",
		.data = &exynos7_of_data },
	{ .compatible = "samsung,exynos7870-pinctrl",
		.data = &exynos7870_of_data },
	{ .compatible = "samsung,exynos7885-pinctrl",
		.data = &exynos7885_of_data },
	{ .compatible = "samsung,exynos850-pinctrl",
		.data = &exynos850_of_data },
	{ .compatible = "samsung,exynos8895-pinctrl",
		.data = &exynos8895_of_data },
	{ .compatible = "samsung,exynos9810-pinctrl",
		.data = &exynos9810_of_data },
	{ .compatible = "samsung,exynos990-pinctrl",
		.data = &exynos990_of_data },
	{ .compatible = "samsung,exynosautov9-pinctrl",
		.data = &exynosautov9_of_data },
	{ .compatible = "samsung,exynosautov920-pinctrl",
		.data = &exynosautov920_of_data },
	{ .compatible = "tesla,fsd-pinctrl",
		.data = &fsd_of_data },
#endif
#ifdef CONFIG_PINCTRL_S3C64XX
	{ .compatible = "samsung,s3c64xx-pinctrl",
		.data = &s3c64xx_of_data },
#endif
	{},
};

static const struct dev_pm_ops samsung_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(samsung_pinctrl_suspend,
				     samsung_pinctrl_resume)
};

static struct platform_driver samsung_pinctrl_driver = {
	.probe		= samsung_pinctrl_probe,
	.driver = {
		.name	= "samsung-pinctrl",
		.of_match_table = samsung_pinctrl_dt_match,
		.suppress_bind_attrs = true,
		.pm = &samsung_pinctrl_pm_ops,
	},
};

static int __init samsung_pinctrl_drv_register(void)
{
	return platform_driver_register(&samsung_pinctrl_driver);
}
postcore_initcall(samsung_pinctrl_drv_register);
