/*
 * SuperH Pin Function Controller pinmux support.
 *
 * Copyright (C) 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define DRV_NAME "sh-pfc"

#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "core.h"
#include "../core.h"
#include "../pinconf.h"

struct sh_pfc_pin_config {
	u32 type;
};

struct sh_pfc_pinctrl {
	struct pinctrl_dev *pctl;
	struct pinctrl_desc pctl_desc;

	struct sh_pfc *pfc;

	struct pinctrl_pin_desc *pins;
	struct sh_pfc_pin_config *configs;

	const char *func_prop_name;
	const char *groups_prop_name;
	const char *pins_prop_name;
};

static int sh_pfc_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->nr_groups;
}

static const char *sh_pfc_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->groups[selector].name;
}

static int sh_pfc_get_group_pins(struct pinctrl_dev *pctldev, unsigned selector,
				 const unsigned **pins, unsigned *num_pins)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*pins = pmx->pfc->info->groups[selector].pins;
	*num_pins = pmx->pfc->info->groups[selector].nr_pins;

	return 0;
}

static void sh_pfc_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned offset)
{
	seq_printf(s, "%s", DRV_NAME);
}

#ifdef CONFIG_OF
static int sh_pfc_map_add_config(struct pinctrl_map *map,
				 const char *group_or_pin,
				 enum pinctrl_map_type type,
				 unsigned long *configs,
				 unsigned int num_configs)
{
	unsigned long *cfgs;

	cfgs = kmemdup(configs, num_configs * sizeof(*cfgs),
		       GFP_KERNEL);
	if (cfgs == NULL)
		return -ENOMEM;

	map->type = type;
	map->data.configs.group_or_pin = group_or_pin;
	map->data.configs.configs = cfgs;
	map->data.configs.num_configs = num_configs;

	return 0;
}

static int sh_pfc_dt_subnode_to_map(struct pinctrl_dev *pctldev,
				    struct device_node *np,
				    struct pinctrl_map **map,
				    unsigned int *num_maps, unsigned int *index)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pmx->pfc->dev;
	struct pinctrl_map *maps = *map;
	unsigned int nmaps = *num_maps;
	unsigned int idx = *index;
	unsigned int num_configs;
	const char *function = NULL;
	unsigned long *configs;
	struct property *prop;
	unsigned int num_groups;
	unsigned int num_pins;
	const char *group;
	const char *pin;
	int ret;

	/* Support both the old Renesas-specific properties and the new standard
	 * properties. Mixing old and new properties isn't allowed, neither
	 * inside a subnode nor across subnodes.
	 */
	if (!pmx->func_prop_name) {
		if (of_find_property(np, "groups", NULL) ||
		    of_find_property(np, "pins", NULL)) {
			pmx->func_prop_name = "function";
			pmx->groups_prop_name = "groups";
			pmx->pins_prop_name = "pins";
		} else {
			pmx->func_prop_name = "renesas,function";
			pmx->groups_prop_name = "renesas,groups";
			pmx->pins_prop_name = "renesas,pins";
		}
	}

	/* Parse the function and configuration properties. At least a function
	 * or one configuration must be specified.
	 */
	ret = of_property_read_string(np, pmx->func_prop_name, &function);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(dev, "Invalid function in DT\n");
		return ret;
	}

	ret = pinconf_generic_parse_dt_config(np, NULL, &configs, &num_configs);
	if (ret < 0)
		return ret;

	if (!function && num_configs == 0) {
		dev_err(dev,
			"DT node must contain at least a function or config\n");
		ret = -ENODEV;
		goto done;
	}

	/* Count the number of pins and groups and reallocate mappings. */
	ret = of_property_count_strings(np, pmx->pins_prop_name);
	if (ret == -EINVAL) {
		num_pins = 0;
	} else if (ret < 0) {
		dev_err(dev, "Invalid pins list in DT\n");
		goto done;
	} else {
		num_pins = ret;
	}

	ret = of_property_count_strings(np, pmx->groups_prop_name);
	if (ret == -EINVAL) {
		num_groups = 0;
	} else if (ret < 0) {
		dev_err(dev, "Invalid pin groups list in DT\n");
		goto done;
	} else {
		num_groups = ret;
	}

	if (!num_pins && !num_groups) {
		dev_err(dev, "No pin or group provided in DT node\n");
		ret = -ENODEV;
		goto done;
	}

	if (function)
		nmaps += num_groups;
	if (configs)
		nmaps += num_pins + num_groups;

	maps = krealloc(maps, sizeof(*maps) * nmaps, GFP_KERNEL);
	if (maps == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	*map = maps;
	*num_maps = nmaps;

	/* Iterate over pins and groups and create the mappings. */
	of_property_for_each_string(np, pmx->groups_prop_name, prop, group) {
		if (function) {
			maps[idx].type = PIN_MAP_TYPE_MUX_GROUP;
			maps[idx].data.mux.group = group;
			maps[idx].data.mux.function = function;
			idx++;
		}

		if (configs) {
			ret = sh_pfc_map_add_config(&maps[idx], group,
						    PIN_MAP_TYPE_CONFIGS_GROUP,
						    configs, num_configs);
			if (ret < 0)
				goto done;

			idx++;
		}
	}

	if (!configs) {
		ret = 0;
		goto done;
	}

	of_property_for_each_string(np, pmx->pins_prop_name, prop, pin) {
		ret = sh_pfc_map_add_config(&maps[idx], pin,
					    PIN_MAP_TYPE_CONFIGS_PIN,
					    configs, num_configs);
		if (ret < 0)
			goto done;

		idx++;
	}

done:
	*index = idx;
	kfree(configs);
	return ret;
}

static void sh_pfc_dt_free_map(struct pinctrl_dev *pctldev,
			       struct pinctrl_map *map, unsigned num_maps)
{
	unsigned int i;

	if (map == NULL)
		return;

	for (i = 0; i < num_maps; ++i) {
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP ||
		    map[i].type == PIN_MAP_TYPE_CONFIGS_PIN)
			kfree(map[i].data.configs.configs);
	}

	kfree(map);
}

static int sh_pfc_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pmx->pfc->dev;
	struct device_node *child;
	unsigned int index;
	int ret;

	*map = NULL;
	*num_maps = 0;
	index = 0;

	for_each_child_of_node(np, child) {
		ret = sh_pfc_dt_subnode_to_map(pctldev, child, map, num_maps,
					       &index);
		if (ret < 0) {
			of_node_put(child);
			goto done;
		}
	}

	/* If no mapping has been found in child nodes try the config node. */
	if (*num_maps == 0) {
		ret = sh_pfc_dt_subnode_to_map(pctldev, np, map, num_maps,
					       &index);
		if (ret < 0)
			goto done;
	}

	if (*num_maps)
		return 0;

	dev_err(dev, "no mapping found in node %pOF\n", np);
	ret = -EINVAL;

done:
	if (ret < 0)
		sh_pfc_dt_free_map(pctldev, *map, *num_maps);

	return ret;
}
#endif /* CONFIG_OF */

static const struct pinctrl_ops sh_pfc_pinctrl_ops = {
	.get_groups_count	= sh_pfc_get_groups_count,
	.get_group_name		= sh_pfc_get_group_name,
	.get_group_pins		= sh_pfc_get_group_pins,
	.pin_dbg_show		= sh_pfc_pin_dbg_show,
#ifdef CONFIG_OF
	.dt_node_to_map		= sh_pfc_dt_node_to_map,
	.dt_free_map		= sh_pfc_dt_free_map,
#endif
};

static int sh_pfc_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->nr_functions;
}

static const char *sh_pfc_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned selector)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	return pmx->pfc->info->functions[selector].name;
}

static int sh_pfc_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned selector,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);

	*groups = pmx->pfc->info->functions[selector].groups;
	*num_groups = pmx->pfc->info->functions[selector].nr_groups;

	return 0;
}

static int sh_pfc_func_set_mux(struct pinctrl_dev *pctldev, unsigned selector,
			       unsigned group)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	const struct sh_pfc_pin_group *grp = &pfc->info->groups[group];
	unsigned long flags;
	unsigned int i;
	int ret = 0;

	spin_lock_irqsave(&pfc->lock, flags);

	for (i = 0; i < grp->nr_pins; ++i) {
		int idx = sh_pfc_get_pin_index(pfc, grp->pins[i]);
		struct sh_pfc_pin_config *cfg = &pmx->configs[idx];

		if (cfg->type != PINMUX_TYPE_NONE) {
			ret = -EBUSY;
			goto done;
		}
	}

	for (i = 0; i < grp->nr_pins; ++i) {
		ret = sh_pfc_config_mux(pfc, grp->mux[i], PINMUX_TYPE_FUNCTION);
		if (ret < 0)
			break;
	}

done:
	spin_unlock_irqrestore(&pfc->lock, flags);
	return ret;
}

static int sh_pfc_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned offset)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	int idx = sh_pfc_get_pin_index(pfc, offset);
	struct sh_pfc_pin_config *cfg = &pmx->configs[idx];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pfc->lock, flags);

	if (cfg->type != PINMUX_TYPE_NONE) {
		dev_err(pfc->dev,
			"Pin %u is busy, can't configure it as GPIO.\n",
			offset);
		ret = -EBUSY;
		goto done;
	}

	if (!pfc->gpio) {
		/* If GPIOs are handled externally the pin mux type need to be
		 * set to GPIO here.
		 */
		const struct sh_pfc_pin *pin = &pfc->info->pins[idx];

		ret = sh_pfc_config_mux(pfc, pin->enum_id, PINMUX_TYPE_GPIO);
		if (ret < 0)
			goto done;
	}

	cfg->type = PINMUX_TYPE_GPIO;

	ret = 0;

done:
	spin_unlock_irqrestore(&pfc->lock, flags);

	return ret;
}

static void sh_pfc_gpio_disable_free(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	int idx = sh_pfc_get_pin_index(pfc, offset);
	struct sh_pfc_pin_config *cfg = &pmx->configs[idx];
	unsigned long flags;

	spin_lock_irqsave(&pfc->lock, flags);
	cfg->type = PINMUX_TYPE_NONE;
	spin_unlock_irqrestore(&pfc->lock, flags);
}

static int sh_pfc_gpio_set_direction(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned offset, bool input)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	int new_type = input ? PINMUX_TYPE_INPUT : PINMUX_TYPE_OUTPUT;
	int idx = sh_pfc_get_pin_index(pfc, offset);
	const struct sh_pfc_pin *pin = &pfc->info->pins[idx];
	struct sh_pfc_pin_config *cfg = &pmx->configs[idx];
	unsigned long flags;
	unsigned int dir;
	int ret;

	/* Check if the requested direction is supported by the pin. Not all SoC
	 * provide pin config data, so perform the check conditionally.
	 */
	if (pin->configs) {
		dir = input ? SH_PFC_PIN_CFG_INPUT : SH_PFC_PIN_CFG_OUTPUT;
		if (!(pin->configs & dir))
			return -EINVAL;
	}

	spin_lock_irqsave(&pfc->lock, flags);

	ret = sh_pfc_config_mux(pfc, pin->enum_id, new_type);
	if (ret < 0)
		goto done;

	cfg->type = new_type;

done:
	spin_unlock_irqrestore(&pfc->lock, flags);
	return ret;
}

static const struct pinmux_ops sh_pfc_pinmux_ops = {
	.get_functions_count	= sh_pfc_get_functions_count,
	.get_function_name	= sh_pfc_get_function_name,
	.get_function_groups	= sh_pfc_get_function_groups,
	.set_mux		= sh_pfc_func_set_mux,
	.gpio_request_enable	= sh_pfc_gpio_request_enable,
	.gpio_disable_free	= sh_pfc_gpio_disable_free,
	.gpio_set_direction	= sh_pfc_gpio_set_direction,
};

static u32 sh_pfc_pinconf_find_drive_strength_reg(struct sh_pfc *pfc,
		unsigned int pin, unsigned int *offset, unsigned int *size)
{
	const struct pinmux_drive_reg_field *field;
	const struct pinmux_drive_reg *reg;
	unsigned int i;

	for (reg = pfc->info->drive_regs; reg->reg; ++reg) {
		for (i = 0; i < ARRAY_SIZE(reg->fields); ++i) {
			field = &reg->fields[i];

			if (field->size && field->pin == pin) {
				*offset = field->offset;
				*size = field->size;

				return reg->reg;
			}
		}
	}

	return 0;
}

static int sh_pfc_pinconf_get_drive_strength(struct sh_pfc *pfc,
					     unsigned int pin)
{
	unsigned long flags;
	unsigned int offset;
	unsigned int size;
	u32 reg;
	u32 val;

	reg = sh_pfc_pinconf_find_drive_strength_reg(pfc, pin, &offset, &size);
	if (!reg)
		return -EINVAL;

	spin_lock_irqsave(&pfc->lock, flags);
	val = sh_pfc_read_reg(pfc, reg, 32);
	spin_unlock_irqrestore(&pfc->lock, flags);

	val = (val >> offset) & GENMASK(size - 1, 0);

	/* Convert the value to mA based on a full drive strength value of 24mA.
	 * We can make the full value configurable later if needed.
	 */
	return (val + 1) * (size == 2 ? 6 : 3);
}

static int sh_pfc_pinconf_set_drive_strength(struct sh_pfc *pfc,
					     unsigned int pin, u16 strength)
{
	unsigned long flags;
	unsigned int offset;
	unsigned int size;
	unsigned int step;
	u32 reg;
	u32 val;

	reg = sh_pfc_pinconf_find_drive_strength_reg(pfc, pin, &offset, &size);
	if (!reg)
		return -EINVAL;

	step = size == 2 ? 6 : 3;

	if (strength < step || strength > 24)
		return -EINVAL;

	/* Convert the value from mA based on a full drive strength value of
	 * 24mA. We can make the full value configurable later if needed.
	 */
	strength = strength / step - 1;

	spin_lock_irqsave(&pfc->lock, flags);

	val = sh_pfc_read_reg(pfc, reg, 32);
	val &= ~GENMASK(offset + size - 1, offset);
	val |= strength << offset;

	sh_pfc_write_reg(pfc, reg, 32, val);

	spin_unlock_irqrestore(&pfc->lock, flags);

	return 0;
}

/* Check whether the requested parameter is supported for a pin. */
static bool sh_pfc_pinconf_validate(struct sh_pfc *pfc, unsigned int _pin,
				    enum pin_config_param param)
{
	int idx = sh_pfc_get_pin_index(pfc, _pin);
	const struct sh_pfc_pin *pin = &pfc->info->pins[idx];

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		return pin->configs &
			(SH_PFC_PIN_CFG_PULL_UP | SH_PFC_PIN_CFG_PULL_DOWN);

	case PIN_CONFIG_BIAS_PULL_UP:
		return pin->configs & SH_PFC_PIN_CFG_PULL_UP;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		return pin->configs & SH_PFC_PIN_CFG_PULL_DOWN;

	case PIN_CONFIG_DRIVE_STRENGTH:
		return pin->configs & SH_PFC_PIN_CFG_DRIVE_STRENGTH;

	case PIN_CONFIG_POWER_SOURCE:
		return pin->configs & SH_PFC_PIN_CFG_IO_VOLTAGE;

	default:
		return false;
	}
}

static int sh_pfc_pinconf_get(struct pinctrl_dev *pctldev, unsigned _pin,
			      unsigned long *config)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned long flags;
	unsigned int arg;

	if (!sh_pfc_pinconf_validate(pfc, _pin, param))
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN: {
		unsigned int bias;

		if (!pfc->info->ops || !pfc->info->ops->get_bias)
			return -ENOTSUPP;

		spin_lock_irqsave(&pfc->lock, flags);
		bias = pfc->info->ops->get_bias(pfc, _pin);
		spin_unlock_irqrestore(&pfc->lock, flags);

		if (bias != param)
			return -EINVAL;

		arg = 0;
		break;
	}

	case PIN_CONFIG_DRIVE_STRENGTH: {
		int ret;

		ret = sh_pfc_pinconf_get_drive_strength(pfc, _pin);
		if (ret < 0)
			return ret;

		arg = ret;
		break;
	}

	case PIN_CONFIG_POWER_SOURCE: {
		u32 pocctrl, val;
		int bit;

		if (!pfc->info->ops || !pfc->info->ops->pin_to_pocctrl)
			return -ENOTSUPP;

		bit = pfc->info->ops->pin_to_pocctrl(pfc, _pin, &pocctrl);
		if (WARN(bit < 0, "invalid pin %#x", _pin))
			return bit;

		spin_lock_irqsave(&pfc->lock, flags);
		val = sh_pfc_read_reg(pfc, pocctrl, 32);
		spin_unlock_irqrestore(&pfc->lock, flags);

		arg = (val & BIT(bit)) ? 3300 : 1800;
		break;
	}

	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int sh_pfc_pinconf_set(struct pinctrl_dev *pctldev, unsigned _pin,
			      unsigned long *configs, unsigned num_configs)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	struct sh_pfc *pfc = pmx->pfc;
	enum pin_config_param param;
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);

		if (!sh_pfc_pinconf_validate(pfc, _pin, param))
			return -ENOTSUPP;

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_DISABLE:
			if (!pfc->info->ops || !pfc->info->ops->set_bias)
				return -ENOTSUPP;

			spin_lock_irqsave(&pfc->lock, flags);
			pfc->info->ops->set_bias(pfc, _pin, param);
			spin_unlock_irqrestore(&pfc->lock, flags);

			break;

		case PIN_CONFIG_DRIVE_STRENGTH: {
			unsigned int arg =
				pinconf_to_config_argument(configs[i]);
			int ret;

			ret = sh_pfc_pinconf_set_drive_strength(pfc, _pin, arg);
			if (ret < 0)
				return ret;

			break;
		}

		case PIN_CONFIG_POWER_SOURCE: {
			unsigned int mV = pinconf_to_config_argument(configs[i]);
			u32 pocctrl, val;
			int bit;

			if (!pfc->info->ops || !pfc->info->ops->pin_to_pocctrl)
				return -ENOTSUPP;

			bit = pfc->info->ops->pin_to_pocctrl(pfc, _pin, &pocctrl);
			if (WARN(bit < 0, "invalid pin %#x", _pin))
				return bit;

			if (mV != 1800 && mV != 3300)
				return -EINVAL;

			spin_lock_irqsave(&pfc->lock, flags);
			val = sh_pfc_read_reg(pfc, pocctrl, 32);
			if (mV == 3300)
				val |= BIT(bit);
			else
				val &= ~BIT(bit);
			sh_pfc_write_reg(pfc, pocctrl, 32, val);
			spin_unlock_irqrestore(&pfc->lock, flags);

			break;
		}

		default:
			return -ENOTSUPP;
		}
	} /* for each config */

	return 0;
}

static int sh_pfc_pinconf_group_set(struct pinctrl_dev *pctldev, unsigned group,
				    unsigned long *configs,
				    unsigned num_configs)
{
	struct sh_pfc_pinctrl *pmx = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins;
	unsigned int num_pins;
	unsigned int i, ret;

	pins = pmx->pfc->info->groups[group].pins;
	num_pins = pmx->pfc->info->groups[group].nr_pins;

	for (i = 0; i < num_pins; ++i) {
		ret = sh_pfc_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops sh_pfc_pinconf_ops = {
	.is_generic			= true,
	.pin_config_get			= sh_pfc_pinconf_get,
	.pin_config_set			= sh_pfc_pinconf_set,
	.pin_config_group_set		= sh_pfc_pinconf_group_set,
	.pin_config_config_dbg_show	= pinconf_generic_dump_config,
};

/* PFC ranges -> pinctrl pin descs */
static int sh_pfc_map_pins(struct sh_pfc *pfc, struct sh_pfc_pinctrl *pmx)
{
	unsigned int i;

	/* Allocate and initialize the pins and configs arrays. */
	pmx->pins = devm_kzalloc(pfc->dev,
				 sizeof(*pmx->pins) * pfc->info->nr_pins,
				 GFP_KERNEL);
	if (unlikely(!pmx->pins))
		return -ENOMEM;

	pmx->configs = devm_kzalloc(pfc->dev,
				    sizeof(*pmx->configs) * pfc->info->nr_pins,
				    GFP_KERNEL);
	if (unlikely(!pmx->configs))
		return -ENOMEM;

	for (i = 0; i < pfc->info->nr_pins; ++i) {
		const struct sh_pfc_pin *info = &pfc->info->pins[i];
		struct sh_pfc_pin_config *cfg = &pmx->configs[i];
		struct pinctrl_pin_desc *pin = &pmx->pins[i];

		/* If the pin number is equal to -1 all pins are considered */
		pin->number = info->pin != (u16)-1 ? info->pin : i;
		pin->name = info->name;
		cfg->type = PINMUX_TYPE_NONE;
	}

	return 0;
}

int sh_pfc_register_pinctrl(struct sh_pfc *pfc)
{
	struct sh_pfc_pinctrl *pmx;
	int ret;

	pmx = devm_kzalloc(pfc->dev, sizeof(*pmx), GFP_KERNEL);
	if (unlikely(!pmx))
		return -ENOMEM;

	pmx->pfc = pfc;

	ret = sh_pfc_map_pins(pfc, pmx);
	if (ret < 0)
		return ret;

	pmx->pctl_desc.name = DRV_NAME;
	pmx->pctl_desc.owner = THIS_MODULE;
	pmx->pctl_desc.pctlops = &sh_pfc_pinctrl_ops;
	pmx->pctl_desc.pmxops = &sh_pfc_pinmux_ops;
	pmx->pctl_desc.confops = &sh_pfc_pinconf_ops;
	pmx->pctl_desc.pins = pmx->pins;
	pmx->pctl_desc.npins = pfc->info->nr_pins;

	ret = devm_pinctrl_register_and_init(pfc->dev, &pmx->pctl_desc, pmx,
					     &pmx->pctl);
	if (ret) {
		dev_err(pfc->dev, "could not register: %i\n", ret);

		return ret;
	}

	return pinctrl_enable(pmx->pctl);
}
