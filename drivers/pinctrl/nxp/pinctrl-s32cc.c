// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Core driver for the S32 CC (Common Chassis) pin controller
 *
 * Copyright 2017-2022,2024 NXP
 * Copyright (C) 2022 SUSE LLC
 * Copyright 2015-2016 Freescale Semiconductor, Inc.
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "pinctrl-s32.h"

#define S32_PIN_ID_SHIFT	4
#define S32_PIN_ID_MASK		GENMASK(31, S32_PIN_ID_SHIFT)

#define S32_MSCR_SSS_MASK	GENMASK(2, 0)
#define S32_MSCR_PUS		BIT(12)
#define S32_MSCR_PUE		BIT(13)
#define S32_MSCR_SRE(X)		(((X) & GENMASK(3, 0)) << 14)
#define S32_MSCR_IBE		BIT(19)
#define S32_MSCR_ODE		BIT(20)
#define S32_MSCR_OBE		BIT(21)

enum s32_write_type {
	S32_PINCONF_UPDATE_ONLY,
	S32_PINCONF_OVERWRITE,
};

static struct regmap_config s32_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static u32 get_pin_no(u32 pinmux)
{
	return (pinmux & S32_PIN_ID_MASK) >> S32_PIN_ID_SHIFT;
}

static u32 get_pin_func(u32 pinmux)
{
	return pinmux & GENMASK(3, 0);
}

struct s32_pinctrl_mem_region {
	struct regmap *map;
	const struct s32_pin_range *pin_range;
	char name[8];
};

/*
 * Holds pin configuration for GPIO's.
 * @pin_id: Pin ID for this GPIO
 * @config: Pin settings
 * @list: Linked list entry for each gpio pin
 */
struct gpio_pin_config {
	unsigned int pin_id;
	unsigned int config;
	struct list_head list;
};

/*
 * Pad config save/restore for power suspend/resume.
 */
struct s32_pinctrl_context {
	unsigned int *pads;
};

/*
 * @dev: a pointer back to containing device
 * @pctl: a pointer to the pinctrl device structure
 * @regions: reserved memory regions with start/end pin
 * @info: structure containing information about the pin
 * @gpio_configs: Saved configurations for GPIO pins
 * @gpiop_configs_lock: lock for the `gpio_configs` list
 * @s32_pinctrl_context: Configuration saved over system sleep
 */
struct s32_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct s32_pinctrl_mem_region *regions;
	struct s32_pinctrl_soc_info *info;
	struct list_head gpio_configs;
	spinlock_t gpio_configs_lock;
#ifdef CONFIG_PM_SLEEP
	struct s32_pinctrl_context saved_context;
#endif
};

static struct s32_pinctrl_mem_region *
s32_get_region(struct pinctrl_dev *pctldev, unsigned int pin)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pin_range *pin_range;
	unsigned int mem_regions = ipctl->info->soc_data->mem_regions;
	unsigned int i;

	for (i = 0; i < mem_regions; i++) {
		pin_range = ipctl->regions[i].pin_range;
		if (pin >= pin_range->start && pin <= pin_range->end)
			return &ipctl->regions[i];
	}

	return NULL;
}

static inline int s32_check_pin(struct pinctrl_dev *pctldev,
				unsigned int pin)
{
	return s32_get_region(pctldev, pin) ? 0 : -EINVAL;
}

static inline int s32_regmap_read(struct pinctrl_dev *pctldev,
			   unsigned int pin, unsigned int *val)
{
	struct s32_pinctrl_mem_region *region;
	unsigned int offset;

	region = s32_get_region(pctldev, pin);
	if (!region)
		return -EINVAL;

	offset = (pin - region->pin_range->start) *
			regmap_get_reg_stride(region->map);

	return regmap_read(region->map, offset, val);
}

static inline int s32_regmap_write(struct pinctrl_dev *pctldev,
			    unsigned int pin,
			    unsigned int val)
{
	struct s32_pinctrl_mem_region *region;
	unsigned int offset;

	region = s32_get_region(pctldev, pin);
	if (!region)
		return -EINVAL;

	offset = (pin - region->pin_range->start) *
			regmap_get_reg_stride(region->map);

	return regmap_write(region->map, offset, val);

}

static inline int s32_regmap_update(struct pinctrl_dev *pctldev, unsigned int pin,
			     unsigned int mask, unsigned int val)
{
	struct s32_pinctrl_mem_region *region;
	unsigned int offset;

	region = s32_get_region(pctldev, pin);
	if (!region)
		return -EINVAL;

	offset = (pin - region->pin_range->start) *
			regmap_get_reg_stride(region->map);

	return regmap_update_bits(region->map, offset, mask, val);
}

static int s32_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;

	return info->ngroups;
}

static const char *s32_get_group_name(struct pinctrl_dev *pctldev,
				      unsigned int selector)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;

	return info->groups[selector].data.name;
}

static int s32_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned int selector, const unsigned int **pins,
			      unsigned int *npins)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;

	*pins = info->groups[selector].data.pins;
	*npins = info->groups[selector].data.npins;

	return 0;
}

static void s32_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
			     unsigned int offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static int s32_dt_group_node_to_map(struct pinctrl_dev *pctldev,
				    struct device_node *np,
				    struct pinctrl_map **map,
				    unsigned int *reserved_maps,
				    unsigned int *num_maps,
				    const char *func_name)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = ipctl->dev;
	unsigned long *cfgs = NULL;
	unsigned int n_cfgs, reserve = 1;
	int n_pins, ret;

	n_pins = of_property_count_elems_of_size(np, "pinmux", sizeof(u32));
	if (n_pins < 0) {
		dev_warn(dev, "Can't find 'pinmux' property in node %pOFn\n", np);
	} else if (!n_pins) {
		return -EINVAL;
	}

	ret = pinconf_generic_parse_dt_config(np, pctldev, &cfgs, &n_cfgs);
	if (ret) {
		dev_err(dev, "%pOF: could not parse node property\n", np);
		return ret;
	}

	if (n_cfgs)
		reserve++;

	ret = pinctrl_utils_reserve_map(pctldev, map, reserved_maps, num_maps,
					reserve);
	if (ret < 0)
		goto free_cfgs;

	ret = pinctrl_utils_add_map_mux(pctldev, map, reserved_maps, num_maps,
					np->name, func_name);
	if (ret < 0)
		goto free_cfgs;

	if (n_cfgs) {
		ret = pinctrl_utils_add_map_configs(pctldev, map, reserved_maps,
						    num_maps, np->name, cfgs, n_cfgs,
						    PIN_MAP_TYPE_CONFIGS_GROUP);
		if (ret < 0)
			goto free_cfgs;
	}

free_cfgs:
	kfree(cfgs);
	return ret;
}

static int s32_dt_node_to_map(struct pinctrl_dev *pctldev,
			      struct device_node *np_config,
			      struct pinctrl_map **map,
			      unsigned int *num_maps)
{
	unsigned int reserved_maps;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_available_child_of_node_scoped(np_config, np) {
		ret = s32_dt_group_node_to_map(pctldev, np, map,
					       &reserved_maps, num_maps,
					       np_config->name);
		if (ret < 0) {
			pinctrl_utils_free_map(pctldev, *map, *num_maps);
			return ret;
		}
	}

	return 0;
}

static const struct pinctrl_ops s32_pctrl_ops = {
	.get_groups_count = s32_get_groups_count,
	.get_group_name = s32_get_group_name,
	.get_group_pins = s32_get_group_pins,
	.pin_dbg_show = s32_pin_dbg_show,
	.dt_node_to_map = s32_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int s32_pmx_set(struct pinctrl_dev *pctldev, unsigned int selector,
		       unsigned int group)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;
	int i, ret;
	struct s32_pin_group *grp;

	/*
	 * Configure the mux mode for each pin in the group for a specific
	 * function.
	 */
	grp = &info->groups[group];

	dev_dbg(ipctl->dev, "set mux for function %s group %s\n",
		info->functions[selector].name, grp->data.name);

	/* Check beforehand so we don't have a partial config. */
	for (i = 0; i < grp->data.npins; i++) {
		if (s32_check_pin(pctldev, grp->data.pins[i]) != 0) {
			dev_err(info->dev, "invalid pin: %u in group: %u\n",
				grp->data.pins[i], group);
			return -EINVAL;
		}
	}

	for (i = 0, ret = 0; i < grp->data.npins && !ret; i++) {
		ret = s32_regmap_update(pctldev, grp->data.pins[i],
					S32_MSCR_SSS_MASK, grp->pin_sss[i]);
		if (ret) {
			dev_err(info->dev, "Failed to set pin %u\n",
				grp->data.pins[i]);
			return ret;
		}
	}

	return 0;
}

static int s32_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;

	return info->nfunctions;
}

static const char *s32_pmx_get_func_name(struct pinctrl_dev *pctldev,
					 unsigned int selector)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;

	return info->functions[selector].name;
}

static int s32_pmx_get_groups(struct pinctrl_dev *pctldev,
			      unsigned int selector,
			      const char * const **groups,
			      unsigned int * const num_groups)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].ngroups;

	return 0;
}

static int s32_pmx_gpio_request_enable(struct pinctrl_dev *pctldev,
				       struct pinctrl_gpio_range *range,
				       unsigned int offset)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_pin_config *gpio_pin;
	unsigned int config;
	unsigned long flags;
	int ret;

	ret = s32_regmap_read(pctldev, offset, &config);
	if (ret)
		return ret;

	/* Save current configuration */
	gpio_pin = kmalloc(sizeof(*gpio_pin), GFP_KERNEL);
	if (!gpio_pin)
		return -ENOMEM;

	gpio_pin->pin_id = offset;
	gpio_pin->config = config;

	spin_lock_irqsave(&ipctl->gpio_configs_lock, flags);
	list_add(&gpio_pin->list, &ipctl->gpio_configs);
	spin_unlock_irqrestore(&ipctl->gpio_configs_lock, flags);

	/* GPIO pin means SSS = 0 */
	config &= ~S32_MSCR_SSS_MASK;

	return s32_regmap_write(pctldev, offset, config);
}

static void s32_pmx_gpio_disable_free(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_pin_config *gpio_pin, *tmp;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ipctl->gpio_configs_lock, flags);

	list_for_each_entry_safe(gpio_pin, tmp, &ipctl->gpio_configs, list) {
		if (gpio_pin->pin_id == offset) {
			ret = s32_regmap_write(pctldev, gpio_pin->pin_id,
						 gpio_pin->config);
			if (ret != 0)
				goto unlock;

			list_del(&gpio_pin->list);
			kfree(gpio_pin);
			break;
		}
	}

unlock:
	spin_unlock_irqrestore(&ipctl->gpio_configs_lock, flags);
}

static int s32_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range,
				      unsigned int offset,
				      bool input)
{
	/* Always enable IBE for GPIOs. This allows us to read the
	 * actual line value and compare it with the one set.
	 */
	unsigned int config = S32_MSCR_IBE;
	unsigned int mask = S32_MSCR_IBE | S32_MSCR_OBE;

	/* Enable output buffer */
	if (!input)
		config |= S32_MSCR_OBE;

	return s32_regmap_update(pctldev, offset, mask, config);
}

static const struct pinmux_ops s32_pmx_ops = {
	.get_functions_count = s32_pmx_get_funcs_count,
	.get_function_name = s32_pmx_get_func_name,
	.get_function_groups = s32_pmx_get_groups,
	.set_mux = s32_pmx_set,
	.gpio_request_enable = s32_pmx_gpio_request_enable,
	.gpio_disable_free = s32_pmx_gpio_disable_free,
	.gpio_set_direction = s32_pmx_gpio_set_direction,
};

/* Set the reserved elements as -1 */
static const int support_slew[] = {208, -1, -1, -1, 166, 150, 133, 83};

static int s32_get_slew_regval(int arg)
{
	unsigned int i;

	/* Translate a real slew rate (MHz) to a register value */
	for (i = 0; i < ARRAY_SIZE(support_slew); i++) {
		if (arg == support_slew[i])
			return i;
	}

	return -EINVAL;
}

static inline void s32_pin_set_pull(enum pin_config_param param,
				   unsigned int *mask, unsigned int *config)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		*config &= ~(S32_MSCR_PUS | S32_MSCR_PUE);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*config |= S32_MSCR_PUS | S32_MSCR_PUE;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*config &= ~S32_MSCR_PUS;
		*config |= S32_MSCR_PUE;
		break;
	default:
		return;
	}

	*mask |= S32_MSCR_PUS | S32_MSCR_PUE;
}

static int s32_parse_pincfg(unsigned long pincfg, unsigned int *mask,
			    unsigned int *config)
{
	enum pin_config_param param;
	u32 arg;
	int ret;

	param = pinconf_to_config_param(pincfg);
	arg = pinconf_to_config_argument(pincfg);

	switch (param) {
	/* All pins are persistent over suspend */
	case PIN_CONFIG_PERSIST_STATE:
		return 0;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		*config |= S32_MSCR_ODE;
		*mask |= S32_MSCR_ODE;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		*config &= ~S32_MSCR_ODE;
		*mask |= S32_MSCR_ODE;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		if (arg)
			*config |= S32_MSCR_OBE;
		else
			*config &= ~S32_MSCR_OBE;
		*mask |= S32_MSCR_OBE;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		if (arg)
			*config |= S32_MSCR_IBE;
		else
			*config &= ~S32_MSCR_IBE;
		*mask |= S32_MSCR_IBE;
		break;
	case PIN_CONFIG_SLEW_RATE:
		ret = s32_get_slew_regval(arg);
		if (ret < 0)
			return ret;
		*config |= S32_MSCR_SRE((u32)ret);
		*mask |= S32_MSCR_SRE(~0);
		break;
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		s32_pin_set_pull(param, mask, config);
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		*config &= ~(S32_MSCR_ODE | S32_MSCR_OBE | S32_MSCR_IBE);
		*mask |= S32_MSCR_ODE | S32_MSCR_OBE | S32_MSCR_IBE;
		s32_pin_set_pull(param, mask, config);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int s32_pinconf_mscr_write(struct pinctrl_dev *pctldev,
				   unsigned int pin_id,
				   unsigned long *configs,
				   unsigned int num_configs,
				   enum s32_write_type write_type)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	unsigned int config = 0, mask = 0;
	int i, ret;

	ret = s32_check_pin(pctldev, pin_id);
	if (ret)
		return ret;

	dev_dbg(ipctl->dev, "pinconf set pin %s with %u configs\n",
		pin_get_name(pctldev, pin_id), num_configs);

	for (i = 0; i < num_configs; i++) {
		ret = s32_parse_pincfg(configs[i], &mask, &config);
		if (ret)
			return ret;
	}

	/* If the MSCR configuration has to be written,
	 * the SSS field should not be touched.
	 */
	if (write_type == S32_PINCONF_OVERWRITE)
		mask = (unsigned int)~S32_MSCR_SSS_MASK;

	if (!config && !mask)
		return 0;

	if (write_type == S32_PINCONF_OVERWRITE)
		dev_dbg(ipctl->dev, "set: pin %u cfg 0x%x\n", pin_id, config);
	else
		dev_dbg(ipctl->dev, "update: pin %u cfg 0x%x\n", pin_id,
			config);

	return s32_regmap_update(pctldev, pin_id, mask, config);
}

static int s32_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned int pin_id,
			   unsigned long *config)
{
	return s32_regmap_read(pctldev, pin_id, (unsigned int *)config);
}

static int s32_pinconf_set(struct pinctrl_dev *pctldev,
			   unsigned int pin_id, unsigned long *configs,
			   unsigned int num_configs)
{
	return s32_pinconf_mscr_write(pctldev, pin_id, configs,
				       num_configs, S32_PINCONF_UPDATE_ONLY);
}

static int s32_pconf_group_set(struct pinctrl_dev *pctldev, unsigned int selector,
			       unsigned long *configs, unsigned int num_configs)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;
	struct s32_pin_group *grp;
	int i, ret;

	grp = &info->groups[selector];
	for (i = 0; i < grp->data.npins; i++) {
		ret = s32_pinconf_mscr_write(pctldev, grp->data.pins[i],
					      configs, num_configs, S32_PINCONF_OVERWRITE);
		if (ret)
			return ret;
	}

	return 0;
}

static void s32_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				 struct seq_file *s, unsigned int pin_id)
{
	unsigned int config;
	int ret;

	ret = s32_regmap_read(pctldev, pin_id, &config);
	if (ret)
		return;

	seq_printf(s, "0x%x", config);
}

static void s32_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s, unsigned int selector)
{
	struct s32_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;
	struct s32_pin_group *grp;
	unsigned int config;
	const char *name;
	int i, ret;

	seq_puts(s, "\n");
	grp = &info->groups[selector];
	for (i = 0; i < grp->data.npins; i++) {
		name = pin_get_name(pctldev, grp->data.pins[i]);
		ret = s32_regmap_read(pctldev, grp->data.pins[i], &config);
		if (ret)
			return;
		seq_printf(s, "%s: 0x%x\n", name, config);
	}
}

static const struct pinconf_ops s32_pinconf_ops = {
	.pin_config_get = s32_pinconf_get,
	.pin_config_set	= s32_pinconf_set,
	.pin_config_group_set = s32_pconf_group_set,
	.pin_config_dbg_show = s32_pinconf_dbg_show,
	.pin_config_group_dbg_show = s32_pinconf_group_dbg_show,
};

#ifdef CONFIG_PM_SLEEP
static bool s32_pinctrl_should_save(struct s32_pinctrl *ipctl,
				    unsigned int pin)
{
	const struct pin_desc *pd = pin_desc_get(ipctl->pctl, pin);

	if (!pd)
		return false;

	/*
	 * Only restore the pin if it is actually in use by the kernel (or
	 * by userspace).
	 */
	if (pd->mux_owner || pd->gpio_owner)
		return true;

	return false;
}

int s32_pinctrl_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s32_pinctrl *ipctl = platform_get_drvdata(pdev);
	const struct pinctrl_pin_desc *pin;
	const struct s32_pinctrl_soc_info *info = ipctl->info;
	struct s32_pinctrl_context *saved_context = &ipctl->saved_context;
	int i;
	int ret;
	unsigned int config;

	for (i = 0; i < info->soc_data->npins; i++) {
		pin = &info->soc_data->pins[i];

		if (!s32_pinctrl_should_save(ipctl, pin->number))
			continue;

		ret = s32_regmap_read(ipctl->pctl, pin->number, &config);
		if (ret)
			return -EINVAL;

		saved_context->pads[i] = config;
	}

	return 0;
}

int s32_pinctrl_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct s32_pinctrl *ipctl = platform_get_drvdata(pdev);
	const struct s32_pinctrl_soc_info *info = ipctl->info;
	const struct pinctrl_pin_desc *pin;
	struct s32_pinctrl_context *saved_context = &ipctl->saved_context;
	int ret, i;

	for (i = 0; i < info->soc_data->npins; i++) {
		pin = &info->soc_data->pins[i];

		if (!s32_pinctrl_should_save(ipctl, pin->number))
			continue;

		ret = s32_regmap_write(ipctl->pctl, pin->number,
					 saved_context->pads[i]);
		if (ret)
			return ret;
	}

	return 0;
}
#endif

static int s32_pinctrl_parse_groups(struct device_node *np,
				     struct s32_pin_group *grp,
				     struct s32_pinctrl_soc_info *info)
{
	struct device *dev;
	unsigned int *pins, *sss;
	int i, npins;
	u32 pinmux;

	dev = info->dev;

	dev_dbg(dev, "group: %pOFn\n", np);

	/* Initialise group */
	grp->data.name = np->name;

	npins = of_property_count_elems_of_size(np, "pinmux", sizeof(u32));
	if (npins < 0) {
		dev_err(dev, "Failed to read 'pinmux' property in node %s.\n",
			grp->data.name);
		return -EINVAL;
	}
	if (!npins) {
		dev_err(dev, "The group %s has no pins.\n", grp->data.name);
		return -EINVAL;
	}

	grp->data.npins = npins;

	pins = devm_kcalloc(info->dev, npins, sizeof(*pins), GFP_KERNEL);
	sss = devm_kcalloc(info->dev, npins, sizeof(*sss), GFP_KERNEL);
	if (!pins || !sss)
		return -ENOMEM;

	i = 0;
	of_property_for_each_u32(np, "pinmux", pinmux) {
		pins[i] = get_pin_no(pinmux);
		sss[i] = get_pin_func(pinmux);

		dev_dbg(info->dev, "pin: 0x%x, sss: 0x%x", pins[i], sss[i]);
		i++;
	}

	grp->data.pins = pins;
	grp->pin_sss = sss;

	return 0;
}

static int s32_pinctrl_parse_functions(struct device_node *np,
					struct s32_pinctrl_soc_info *info,
					u32 index)
{
	struct pinfunction *func;
	struct s32_pin_group *grp;
	const char **groups;
	u32 i = 0;
	int ret = 0;

	dev_dbg(info->dev, "parse function(%u): %pOFn\n", index, np);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->ngroups = of_get_child_count(np);
	if (func->ngroups == 0) {
		dev_err(info->dev, "no groups defined in %pOF\n", np);
		return -EINVAL;
	}

	groups = devm_kcalloc(info->dev, func->ngroups,
				    sizeof(*func->groups), GFP_KERNEL);
	if (!groups)
		return -ENOMEM;

	for_each_child_of_node_scoped(np, child) {
		groups[i] = child->name;
		grp = &info->groups[info->grp_index++];
		ret = s32_pinctrl_parse_groups(child, grp, info);
		if (ret)
			return ret;
		i++;
	}

	func->groups = groups;

	return 0;
}

static int s32_pinctrl_probe_dt(struct platform_device *pdev,
				struct s32_pinctrl *ipctl)
{
	struct s32_pinctrl_soc_info *info = ipctl->info;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct regmap *map;
	void __iomem *base;
	unsigned int mem_regions = info->soc_data->mem_regions;
	int ret;
	u32 nfuncs = 0;
	u32 i = 0;

	if (!np)
		return -ENODEV;

	if (mem_regions == 0 || mem_regions >= 10000) {
		dev_err(&pdev->dev, "mem_regions is invalid: %u\n", mem_regions);
		return -EINVAL;
	}

	ipctl->regions = devm_kcalloc(&pdev->dev, mem_regions,
				      sizeof(*ipctl->regions), GFP_KERNEL);
	if (!ipctl->regions)
		return -ENOMEM;

	for (i = 0; i < mem_regions; i++) {
		base = devm_platform_get_and_ioremap_resource(pdev, i, &res);
		if (IS_ERR(base))
			return PTR_ERR(base);

		snprintf(ipctl->regions[i].name,
			 sizeof(ipctl->regions[i].name), "map%u", i);

		s32_regmap_config.name = ipctl->regions[i].name;
		s32_regmap_config.max_register = resource_size(res) -
						 s32_regmap_config.reg_stride;

		map = devm_regmap_init_mmio(&pdev->dev, base,
						&s32_regmap_config);
		if (IS_ERR(map)) {
			dev_err(&pdev->dev, "Failed to init regmap[%u]\n", i);
			return PTR_ERR(map);
		}

		ipctl->regions[i].map = map;
		ipctl->regions[i].pin_range = &info->soc_data->mem_pin_ranges[i];
	}

	nfuncs = of_get_child_count(np);
	if (nfuncs <= 0) {
		dev_err(&pdev->dev, "no functions defined\n");
		return -EINVAL;
	}

	info->nfunctions = nfuncs;
	info->functions = devm_kcalloc(&pdev->dev, nfuncs,
				       sizeof(*info->functions), GFP_KERNEL);
	if (!info->functions)
		return -ENOMEM;

	info->ngroups = 0;
	for_each_child_of_node_scoped(np, child)
		info->ngroups += of_get_child_count(child);

	info->groups = devm_kcalloc(&pdev->dev, info->ngroups,
				    sizeof(*info->groups), GFP_KERNEL);
	if (!info->groups)
		return -ENOMEM;

	i = 0;
	for_each_child_of_node_scoped(np, child) {
		ret = s32_pinctrl_parse_functions(child, info, i++);
		if (ret)
			return ret;
	}

	return 0;
}

int s32_pinctrl_probe(struct platform_device *pdev,
		      const struct s32_pinctrl_soc_data *soc_data)
{
	struct s32_pinctrl *ipctl;
	int ret;
	struct pinctrl_desc *s32_pinctrl_desc;
	struct s32_pinctrl_soc_info *info;
#ifdef CONFIG_PM_SLEEP
	struct s32_pinctrl_context *saved_context;
#endif

	if (!soc_data || !soc_data->pins || !soc_data->npins) {
		dev_err(&pdev->dev, "wrong pinctrl info\n");
		return -EINVAL;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->soc_data = soc_data;
	info->dev = &pdev->dev;

	/* Create state holders etc for this driver */
	ipctl = devm_kzalloc(&pdev->dev, sizeof(*ipctl), GFP_KERNEL);
	if (!ipctl)
		return -ENOMEM;

	ipctl->info = info;
	ipctl->dev = info->dev;
	platform_set_drvdata(pdev, ipctl);

	INIT_LIST_HEAD(&ipctl->gpio_configs);
	spin_lock_init(&ipctl->gpio_configs_lock);

	s32_pinctrl_desc =
		devm_kmalloc(&pdev->dev, sizeof(*s32_pinctrl_desc), GFP_KERNEL);
	if (!s32_pinctrl_desc)
		return -ENOMEM;

	s32_pinctrl_desc->name = dev_name(&pdev->dev);
	s32_pinctrl_desc->pins = info->soc_data->pins;
	s32_pinctrl_desc->npins = info->soc_data->npins;
	s32_pinctrl_desc->pctlops = &s32_pctrl_ops;
	s32_pinctrl_desc->pmxops = &s32_pmx_ops;
	s32_pinctrl_desc->confops = &s32_pinconf_ops;
	s32_pinctrl_desc->owner = THIS_MODULE;

	ret = s32_pinctrl_probe_dt(pdev, ipctl);
	if (ret) {
		dev_err(&pdev->dev, "fail to probe dt properties\n");
		return ret;
	}

	ipctl->pctl = devm_pinctrl_register(&pdev->dev, s32_pinctrl_desc,
					    ipctl);
	if (IS_ERR(ipctl->pctl))
		return dev_err_probe(&pdev->dev, PTR_ERR(ipctl->pctl),
				     "could not register s32 pinctrl driver\n");

#ifdef CONFIG_PM_SLEEP
	saved_context = &ipctl->saved_context;
	saved_context->pads =
		devm_kcalloc(&pdev->dev, info->soc_data->npins,
			     sizeof(*saved_context->pads),
			     GFP_KERNEL);
	if (!saved_context->pads)
		return -ENOMEM;
#endif

	dev_info(&pdev->dev, "initialized s32 pinctrl driver\n");

	return 0;
}
