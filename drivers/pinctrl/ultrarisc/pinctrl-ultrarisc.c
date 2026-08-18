// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 UltraRISC Technology (Shanghai) Co., Ltd.
 *
 * Author: Jia Wang <wangjia@ultrarisc.com>
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "../core.h"
#include "../devicetree.h"
#include "../pinconf.h"
#include "../pinmux.h"

#include "pinctrl-ultrarisc.h"

#define UR_CONF_BIT_PER_PIN	4
#define UR_CONF_PIN_PER_REG	(32 / UR_CONF_BIT_PER_PIN)
static const u32 ur_drive_strengths[] = { 20, 27, 33, 40 };

static const struct ur_port_desc *ur_get_pin_port(struct pinctrl_dev *pctldev,
						  unsigned int pin)
{
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);

	if (!desc || !desc->drv_data)
		return NULL;

	return desc->drv_data;
}

static u32 ur_get_pin_conf_offset(const struct ur_port_desc *port_desc, u32 pin)
{
	return port_desc->conf_offset +
	       (pin / UR_CONF_PIN_PER_REG) * sizeof(u32);
}

static int ur_read_pin_conf(struct ur_pinctrl *pctrl, unsigned int pin, u32 *conf)
{
	const struct ur_port_desc *port_desc;
	u32 pin_offset;
	u32 reg_offset;
	u32 shift;
	u32 mask;

	port_desc = ur_get_pin_port(pctrl->pctl_dev, pin);
	if (!port_desc)
		return -EINVAL;

	pin_offset = pin - port_desc->pin_base;
	reg_offset = ur_get_pin_conf_offset(port_desc, pin_offset);
	shift = (pin_offset % UR_CONF_PIN_PER_REG) * UR_CONF_BIT_PER_PIN;
	mask = GENMASK(UR_CONF_BIT_PER_PIN - 1, 0) << shift;
	*conf = field_get(mask, readl_relaxed(pctrl->base + reg_offset));

	return 0;
}

static int ur_write_pin_conf(struct ur_pinctrl *pctrl, unsigned int pin, u32 conf)
{
	const struct ur_port_desc *port_desc;
	void __iomem *reg;
	u32 pin_offset;
	u32 reg_offset;
	u32 shift;
	u32 mask;
	u32 val;

	port_desc = ur_get_pin_port(pctrl->pctl_dev, pin);
	if (!port_desc)
		return -EINVAL;

	pin_offset = pin - port_desc->pin_base;
	reg_offset = ur_get_pin_conf_offset(port_desc, pin_offset);
	reg = pctrl->base + reg_offset;
	shift = (pin_offset % UR_CONF_PIN_PER_REG) * UR_CONF_BIT_PER_PIN;
	mask = GENMASK(UR_CONF_BIT_PER_PIN - 1, 0) << shift;

	scoped_guard(raw_spinlock_irqsave, &pctrl->lock) {
		val = readl_relaxed(reg);
		val = (val & ~mask) | field_prep(mask, conf);
		writel_relaxed(val, reg);
	}

	return 0;
}

static int ur_set_pin_mux(struct ur_pinctrl *pctrl,
			  const struct ur_port_desc *port_desc,
			  u32 pin_offset, u32 mode)
{
	void __iomem *reg = pctrl->base + port_desc->func_offset;
	u32 val;

	if (WARN_ON(pin_offset >= UR_MAX_PINS_PER_PORT))
		return -EINVAL;

	scoped_guard(raw_spinlock_irqsave, &pctrl->lock) {
		val = readl_relaxed(reg);
		val &= ~((UR_FUNC_0 | UR_FUNC_1) << pin_offset);
		val |= mode << pin_offset;
		writel_relaxed(val, reg);
	}

	return 0;
}

static int ur_set_pin_mux_by_num(struct ur_pinctrl *pctrl, unsigned int pin, u32 mode)
{
	const struct ur_port_desc *port_desc = ur_get_pin_port(pctrl->pctl_dev, pin);
	u32 pin_offset;

	if (!port_desc)
		return -EINVAL;

	if (mode != UR_FUNC_DEFAULT && !(port_desc->supported_modes & mode))
		return -EINVAL;

	pin_offset = pin - port_desc->pin_base;

	return ur_set_pin_mux(pctrl, port_desc, pin_offset, mode);
}

static int ur_hw_to_config(unsigned long *config, u32 conf)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 drive = FIELD_GET(UR_DRIVE_MASK, conf);
	u32 pull = FIELD_GET(UR_PULL_MASK, conf);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		if (pull != UR_PULL_DIS)
			return -EINVAL;
		*config = pinconf_to_config_packed(param, 1);
		return 0;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pull != UR_PULL_UP)
			return -EINVAL;
		*config = pinconf_to_config_packed(param, 1);
		return 0;
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		if (pull != UR_PULL_DOWN)
			return -EINVAL;
		*config = pinconf_to_config_packed(param, 1);
		return 0;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (drive >= ARRAY_SIZE(ur_drive_strengths))
			return -EINVAL;
		*config = pinconf_to_config_packed(param, ur_drive_strengths[drive]);
		return 0;
	default:
		return -EINVAL;
	}
}

static int ur_config_to_hw(unsigned long config, u32 *conf)
{
	enum pin_config_param param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		FIELD_MODIFY(UR_PULL_MASK, conf, UR_PULL_DIS);
		return 0;
	case PIN_CONFIG_BIAS_PULL_UP:
		FIELD_MODIFY(UR_PULL_MASK, conf, UR_PULL_UP);
		return 0;
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		FIELD_MODIFY(UR_PULL_MASK, conf, UR_PULL_DOWN);
		return 0;
	case PIN_CONFIG_DRIVE_STRENGTH:
		for (u32 i = 0; i < ARRAY_SIZE(ur_drive_strengths); i++) {
			if (ur_drive_strengths[i] != arg)
				continue;
			FIELD_MODIFY(UR_DRIVE_MASK, conf, i);
			return 0;
		}
		return -EINVAL;
	case PIN_CONFIG_PERSIST_STATE:
		/*
		 * For PIN_CONFIG_PERSIST_STATE, gpiolib only treats
		 * -ENOTSUPP as an optional unsupported result.
		 * Do not use -EOPNOTSUPP here.
		 */
		return -ENOTSUPP;
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ur_find_group_route() - Find the route matching a function and pin set
 * @pctrl: pin controller instance
 * @function: mux function name
 * @group_mask: bitmap of pins in the selected group
 * @route_out: returned route entry on success
 *
 * A mux function may be associated with multiple hardware routing options,
 * each valid for a specific set of pins. This helper finds the unique route
 * whose valid_pins mask covers all pins in @group_mask.
 *
 * The routing table must guarantee that a given function plus pin set
 * resolves to exactly one route. If multiple routes match, the routing
 * description is considered ambiguous and the lookup fails.
 *
 * Return: 0 on success, or -EINVAL if no matching route exists or if the
 * routing table contains ambiguous entries.
 */
static int ur_find_group_route(struct ur_pinctrl *pctrl,
			       const char *function,
			       u64 group_mask,
			       const struct ur_func_route **route_out)
{
	const struct ur_func_route *match = NULL;

	for (u32 i = 0; i < pctrl->data->num_routes; i++) {
		const struct ur_func_route *route = &pctrl->data->routes[i];

		if (strcmp(route->function, function))
			continue;

		if ((route->valid_pins & group_mask) != group_mask)
			continue;

		if (match) {
			dev_err(pctrl->dev,
				"ambiguous route for function %s group_mask=%#llx\n",
				function, (unsigned long long)group_mask);
			return -EINVAL;
		}

		match = route;
	}

	if (match) {
		*route_out = match;
		return 0;
	}

	return -EINVAL;
}

static const char *ur_get_group_function(struct pinctrl_dev *pctldev,
					 unsigned int group_selector,
					 unsigned int pin_index)
{
	const struct group_desc *group;
	const char * const *functions;

	group = pinctrl_generic_get_group(pctldev, group_selector);
	if (!group || pin_index >= group->grp.npins || !group->data)
		return NULL;

	functions = group->data;

	return functions[pin_index];
}

static int ur_resolve_group_mux(struct pinctrl_dev *pctldev,
				struct ur_pinctrl *pctrl,
				unsigned int group_selector,
				const unsigned int *pins,
				unsigned int npins,
				const struct ur_func_route **route_out)
{
	const char *function;
	u64 group_mask = 0;

	if (!npins)
		return -EINVAL;

	function = ur_get_group_function(pctldev, group_selector, 0);
	if (!function)
		return -EINVAL;

	for (u32 i = 0; i < npins; i++)
		group_mask |= BIT_ULL(pins[i]);

	return ur_find_group_route(pctrl, function, group_mask, route_out);
}

static bool ur_function_is_gpio(struct pinctrl_dev *pctldev,
				unsigned int selector)
{
	const struct function_desc *function;

	function = pinmux_generic_get_function(pctldev, selector);
	if (!function)
		return false;

	for (u32 i = 0; i < function->func->ngroups; i++) {
		const char *func_name;
		int group_selector;

		group_selector = pinctrl_get_group_selector(pctldev,
							    function->func->groups[i]);
		if (group_selector < 0)
			return false;

		func_name = ur_get_group_function(pctldev, group_selector, 0);
		if (!func_name || strcmp(func_name, "gpio"))
			return false;
	}

	return true;
}

static const struct pinctrl_ops ur_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinctrl_generic_pins_function_dt_node_to_map,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int ur_gpio_request_enable(struct pinctrl_dev *pctldev,
				  struct pinctrl_gpio_range *range,
				  unsigned int offset)
{
	struct ur_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct ur_port_desc *port_desc;
	const struct ur_func_route *route;
	int ret;

	(void)range;

	port_desc = ur_get_pin_port(pctldev, offset);
	if (!port_desc || !port_desc->supports_gpio)
		return -EINVAL;

	ret = ur_find_group_route(pctrl, "gpio", BIT_ULL(offset), &route);
	if (ret)
		return ret;

	return ur_set_pin_mux_by_num(pctrl, offset, route->mode);
}

static int ur_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
		      unsigned int group_selector)
{
	struct ur_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct ur_func_route *route;
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	(void)func_selector;

	ret = pinctrl_generic_get_group_pins(pctldev, group_selector, &pins, &npins);
	if (ret)
		return ret;

	ret = ur_resolve_group_mux(pctldev, pctrl, group_selector, pins, npins,
				   &route);
	if (ret)
		return ret;

	for (u32 i = 0; i < npins; i++) {
		ret = ur_set_pin_mux_by_num(pctrl, pins[i], route->mode);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinmux_ops ur_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.function_is_gpio = ur_function_is_gpio,
	.set_mux = ur_set_mux,
	.gpio_request_enable = ur_gpio_request_enable,
	.strict = true,
};

static int ur_pin_config_get(struct pinctrl_dev *pctldev,
			     unsigned int pin,
			     unsigned long *config)
{
	struct ur_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	u32 conf;
	int ret;

	ret = ur_read_pin_conf(pctrl, pin, &conf);
	if (ret)
		return ret;

	return ur_hw_to_config(config, conf);
}

static int ur_pin_config_set(struct pinctrl_dev *pctldev,
			     unsigned int pin,
			     unsigned long *configs,
			     unsigned int num_configs)
{
	struct ur_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	u32 conf;
	int ret;

	ret = ur_read_pin_conf(pctrl, pin, &conf);
	if (ret)
		return ret;

	for (u32 i = 0; i < num_configs; i++) {
		ret = ur_config_to_hw(configs[i], &conf);
		if (ret)
			return ret;
	}

	return ur_write_pin_conf(pctrl, pin, conf);
}

static int ur_pin_config_group_get(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   unsigned long *config)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret || !npins)
		return ret ?: -EINVAL;

	return ur_pin_config_get(pctldev, pins[0], config);
}

static int ur_pin_config_group_set(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   unsigned long *configs,
				   unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret)
		return ret;

	for (u32 i = 0; i < npins; i++) {
		ret = ur_pin_config_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops ur_pinconf_ops = {
	.pin_config_get = ur_pin_config_get,
	.pin_config_set = ur_pin_config_set,
	.pin_config_group_get = ur_pin_config_group_get,
	.pin_config_group_set = ur_pin_config_group_set,
	.is_generic = true,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

int ur_pinctrl_probe(struct platform_device *pdev,
		     const struct ur_pinctrl_data *data)
{
	struct pinctrl_desc *desc;
	struct ur_pinctrl *pctrl;
	int ret;

	if (!data)
		return -ENODEV;

	desc = devm_kzalloc(&pdev->dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);
	pctrl->dev = &pdev->dev;
	pctrl->data = data;

	raw_spin_lock_init(&pctrl->lock);

	desc->name = dev_name(&pdev->dev);
	desc->owner = THIS_MODULE;
	desc->pins = data->pins;
	desc->npins = data->npins;
	desc->pctlops = &ur_pinctrl_ops;
	desc->pmxops = &ur_pinmux_ops;
	desc->confops = &ur_pinconf_ops;

	ret = devm_pinctrl_register_and_init(&pdev->dev, desc, pctrl, &pctrl->pctl_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to register pinctrl\n");

	platform_set_drvdata(pdev, pctrl);

	return pinctrl_enable(pctrl->pctl_dev);
}
EXPORT_SYMBOL_GPL(ur_pinctrl_probe);

MODULE_DESCRIPTION("UltraRISC pinctrl core driver");
MODULE_LICENSE("GPL");
