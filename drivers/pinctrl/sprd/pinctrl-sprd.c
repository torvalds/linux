// SPDX-License-Identifier: GPL-2.0-only
/*
 * Spreadtrum pin controller driver
 * Copyright (C) 2017 Spreadtrum  - http://www.spreadtrum.com
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

#include "../core.h"
#include "../pinmux.h"
#include "../pinconf.h"
#include "../pinctrl-utils.h"
#include "pinctrl-sprd.h"

#define PINCTRL_BIT_MASK(width)		(~(~0UL << (width)))
#define PINCTRL_REG_OFFSET		0x20
#define PINCTRL_REG_MISC_OFFSET		0x4020
#define PINCTRL_REG_LEN			0x4

#define PIN_FUNC_MASK			(BIT(4) | BIT(5))
#define PIN_FUNC_SEL_1			~PIN_FUNC_MASK
#define PIN_FUNC_SEL_2			BIT(4)
#define PIN_FUNC_SEL_3			BIT(5)
#define PIN_FUNC_SEL_4			PIN_FUNC_MASK

#define AP_SLEEP_MODE			BIT(13)
#define PUBCP_SLEEP_MODE		BIT(14)
#define TGLDSP_SLEEP_MODE		BIT(15)
#define AGDSP_SLEEP_MODE		BIT(16)
#define CM4_SLEEP_MODE			BIT(17)
#define SLEEP_MODE_MASK			GENMASK(5, 0)
#define SLEEP_MODE_SHIFT		13

#define SLEEP_INPUT			BIT(1)
#define SLEEP_INPUT_MASK		0x1
#define SLEEP_INPUT_SHIFT		1

#define SLEEP_OUTPUT			BIT(0)
#define SLEEP_OUTPUT_MASK		0x1
#define SLEEP_OUTPUT_SHIFT		0

#define DRIVE_STRENGTH_MASK		GENMASK(3, 0)
#define DRIVE_STRENGTH_SHIFT		19

#define SLEEP_PULL_DOWN			BIT(2)
#define SLEEP_PULL_DOWN_MASK		0x1
#define SLEEP_PULL_DOWN_SHIFT		2

#define PULL_DOWN			BIT(6)
#define PULL_DOWN_MASK			0x1
#define PULL_DOWN_SHIFT			6

#define SLEEP_PULL_UP			BIT(3)
#define SLEEP_PULL_UP_MASK		0x1
#define SLEEP_PULL_UP_SHIFT		3

#define PULL_UP_20K			(BIT(12) | BIT(7))
#define PULL_UP_4_7K			BIT(12)
#define PULL_UP_MASK			0x21
#define PULL_UP_SHIFT			7

#define INPUT_SCHMITT			BIT(11)
#define INPUT_SCHMITT_MASK		0x1
#define INPUT_SCHMITT_SHIFT		11

enum pin_sleep_mode {
	AP_SLEEP = BIT(0),
	PUBCP_SLEEP = BIT(1),
	TGLDSP_SLEEP = BIT(2),
	AGDSP_SLEEP = BIT(3),
	CM4_SLEEP = BIT(4),
};

enum pin_func_sel {
	PIN_FUNC_1,
	PIN_FUNC_2,
	PIN_FUNC_3,
	PIN_FUNC_4,
	PIN_FUNC_MAX,
};

/**
 * struct sprd_pin: represent one pin's description
 * @name: pin name
 * @number: pin number
 * @type: pin type, can be GLOBAL_CTRL_PIN/COMMON_PIN/MISC_PIN
 * @reg: pin register address
 * @bit_offset: bit offset in pin register
 * @bit_width: bit width in pin register
 */
struct sprd_pin {
	const char *name;
	unsigned int number;
	enum pin_type type;
	unsigned long reg;
	unsigned long bit_offset;
	unsigned long bit_width;
};

/**
 * struct sprd_pin_group: represent one group's description
 * @name: group name
 * @npins: pin numbers of this group
 * @pins: pointer to pins array
 */
struct sprd_pin_group {
	const char *name;
	unsigned int npins;
	unsigned int *pins;
};

/**
 * struct sprd_pinctrl_soc_info: represent the SoC's pins description
 * @groups: pointer to groups of pins
 * @ngroups: group numbers of the whole SoC
 * @pins: pointer to pins description
 * @npins: pin numbers of the whole SoC
 * @grp_names: pointer to group names array
 */
struct sprd_pinctrl_soc_info {
	struct sprd_pin_group *groups;
	unsigned int ngroups;
	struct sprd_pin *pins;
	unsigned int npins;
	const char **grp_names;
};

/**
 * struct sprd_pinctrl: represent the pin controller device
 * @dev: pointer to the device structure
 * @pctl: pointer to the pinctrl handle
 * @base: base address of the controller
 * @info: pointer to SoC's pins description information
 */
struct sprd_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctl;
	void __iomem *base;
	struct sprd_pinctrl_soc_info *info;
};

#define SPRD_PIN_CONFIG_CONTROL		(PIN_CONFIG_END + 1)
#define SPRD_PIN_CONFIG_SLEEP_MODE	(PIN_CONFIG_END + 2)

static int sprd_pinctrl_get_id_by_name(struct sprd_pinctrl *sprd_pctl,
				       const char *name)
{
	struct sprd_pinctrl_soc_info *info = sprd_pctl->info;
	int i;

	for (i = 0; i < info->npins; i++) {
		if (!strcmp(info->pins[i].name, name))
			return info->pins[i].number;
	}

	return -ENODEV;
}

static struct sprd_pin *
sprd_pinctrl_get_pin_by_id(struct sprd_pinctrl *sprd_pctl, unsigned int id)
{
	struct sprd_pinctrl_soc_info *info = sprd_pctl->info;
	struct sprd_pin *pin = NULL;
	int i;

	for (i = 0; i < info->npins; i++) {
		if (info->pins[i].number == id) {
			pin = &info->pins[i];
			break;
		}
	}

	return pin;
}

static const struct sprd_pin_group *
sprd_pinctrl_find_group_by_name(struct sprd_pinctrl *sprd_pctl,
				const char *name)
{
	struct sprd_pinctrl_soc_info *info = sprd_pctl->info;
	const struct sprd_pin_group *grp = NULL;
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (!strcmp(info->groups[i].name, name)) {
			grp = &info->groups[i];
			break;
		}
	}

	return grp;
}

static int sprd_pctrl_group_count(struct pinctrl_dev *pctldev)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;

	return info->ngroups;
}

static const char *sprd_pctrl_group_name(struct pinctrl_dev *pctldev,
					 unsigned int selector)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;

	return info->groups[selector].name;
}

static int sprd_pctrl_group_pins(struct pinctrl_dev *pctldev,
				 unsigned int selector,
				 const unsigned int **pins,
				 unsigned int *npins)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins;

	return 0;
}

static int sprd_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np,
			       struct pinctrl_map **map,
			       unsigned int *num_maps)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct sprd_pin_group *grp;
	unsigned long *configs = NULL;
	unsigned int num_configs = 0;
	unsigned int reserved_maps = 0;
	unsigned int reserve = 0;
	const char *function;
	enum pinctrl_map_type type;
	int ret;

	grp = sprd_pinctrl_find_group_by_name(pctl, np->name);
	if (!grp) {
		dev_err(pctl->dev, "unable to find group for node %s\n",
			of_node_full_name(np));
		return -EINVAL;
	}

	ret = of_property_count_strings(np, "pins");
	if (ret < 0)
		return ret;

	if (ret == 1)
		type = PIN_MAP_TYPE_CONFIGS_PIN;
	else
		type = PIN_MAP_TYPE_CONFIGS_GROUP;

	ret = of_property_read_string(np, "function", &function);
	if (ret < 0) {
		if (ret != -EINVAL)
			dev_err(pctl->dev,
				"%s: could not parse property function\n",
				of_node_full_name(np));
		function = NULL;
	}

	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs,
					      &num_configs);
	if (ret < 0) {
		dev_err(pctl->dev, "%s: could not parse node property\n",
			of_node_full_name(np));
		return ret;
	}

	*map = NULL;
	*num_maps = 0;

	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;

	ret = pinctrl_utils_reserve_map(pctldev, map, &reserved_maps,
					num_maps, reserve);
	if (ret < 0)
		goto out;

	if (function) {
		ret = pinctrl_utils_add_map_mux(pctldev, map,
						&reserved_maps, num_maps,
						grp->name, function);
		if (ret < 0)
			goto out;
	}

	if (num_configs) {
		const char *group_or_pin;
		unsigned int pin_id;

		if (type == PIN_MAP_TYPE_CONFIGS_PIN) {
			pin_id = grp->pins[0];
			group_or_pin = pin_get_name(pctldev, pin_id);
		} else {
			group_or_pin = grp->name;
		}

		ret = pinctrl_utils_add_map_configs(pctldev, map,
						    &reserved_maps, num_maps,
						    group_or_pin, configs,
						    num_configs, type);
	}

out:
	kfree(configs);
	return ret;
}

static void sprd_pctrl_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned int offset)
{
	seq_printf(s, "%s", dev_name(pctldev->dev));
}

static const struct pinctrl_ops sprd_pctrl_ops = {
	.get_groups_count = sprd_pctrl_group_count,
	.get_group_name = sprd_pctrl_group_name,
	.get_group_pins = sprd_pctrl_group_pins,
	.pin_dbg_show = sprd_pctrl_dbg_show,
	.dt_node_to_map = sprd_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int sprd_pmx_get_function_count(struct pinctrl_dev *pctldev)
{
	return PIN_FUNC_MAX;
}

static const char *sprd_pmx_get_function_name(struct pinctrl_dev *pctldev,
					      unsigned int selector)
{
	switch (selector) {
	case PIN_FUNC_1:
		return "func1";
	case PIN_FUNC_2:
		return "func2";
	case PIN_FUNC_3:
		return "func3";
	case PIN_FUNC_4:
		return "func4";
	default:
		return "null";
	}
}

static int sprd_pmx_get_function_groups(struct pinctrl_dev *pctldev,
					unsigned int selector,
					const char * const **groups,
					unsigned int * const num_groups)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;

	*groups = info->grp_names;
	*num_groups = info->ngroups;

	return 0;
}

static int sprd_pmx_set_mux(struct pinctrl_dev *pctldev,
			    unsigned int func_selector,
			    unsigned int group_selector)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;
	struct sprd_pin_group *grp = &info->groups[group_selector];
	unsigned int i, grp_pins = grp->npins;
	unsigned long reg;
	unsigned int val = 0;

	if (group_selector >= info->ngroups)
		return -EINVAL;

	switch (func_selector) {
	case PIN_FUNC_1:
		val &= PIN_FUNC_SEL_1;
		break;
	case PIN_FUNC_2:
		val |= PIN_FUNC_SEL_2;
		break;
	case PIN_FUNC_3:
		val |= PIN_FUNC_SEL_3;
		break;
	case PIN_FUNC_4:
		val |= PIN_FUNC_SEL_4;
		break;
	default:
		break;
	}

	for (i = 0; i < grp_pins; i++) {
		unsigned int pin_id = grp->pins[i];
		struct sprd_pin *pin = sprd_pinctrl_get_pin_by_id(pctl, pin_id);

		if (!pin || pin->type != COMMON_PIN)
			continue;

		reg = readl((void __iomem *)pin->reg);
		reg &= ~PIN_FUNC_MASK;
		reg |= val;
		writel(reg, (void __iomem *)pin->reg);
	}

	return 0;
}

static const struct pinmux_ops sprd_pmx_ops = {
	.get_functions_count = sprd_pmx_get_function_count,
	.get_function_name = sprd_pmx_get_function_name,
	.get_function_groups = sprd_pmx_get_function_groups,
	.set_mux = sprd_pmx_set_mux,
};

static int sprd_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin_id,
			    unsigned long *config)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pin *pin = sprd_pinctrl_get_pin_by_id(pctl, pin_id);
	unsigned int param = pinconf_to_config_param(*config);
	unsigned int reg, arg;

	if (!pin)
		return -EINVAL;

	if (pin->type == GLOBAL_CTRL_PIN) {
		reg = (readl((void __iomem *)pin->reg) >>
			   pin->bit_offset) & PINCTRL_BIT_MASK(pin->bit_width);
	} else {
		reg = readl((void __iomem *)pin->reg);
	}

	if (pin->type == GLOBAL_CTRL_PIN &&
	    param == SPRD_PIN_CONFIG_CONTROL) {
		arg = reg;
	} else if (pin->type == COMMON_PIN || pin->type == MISC_PIN) {
		switch (param) {
		case SPRD_PIN_CONFIG_SLEEP_MODE:
			arg = (reg >> SLEEP_MODE_SHIFT) & SLEEP_MODE_MASK;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			arg = (reg >> SLEEP_INPUT_SHIFT) & SLEEP_INPUT_MASK;
			break;
		case PIN_CONFIG_OUTPUT:
			arg = reg & SLEEP_OUTPUT_MASK;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			arg = (reg >> DRIVE_STRENGTH_SHIFT) &
				DRIVE_STRENGTH_MASK;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			/* combine sleep pull down and pull down config */
			arg = ((reg >> SLEEP_PULL_DOWN_SHIFT) &
			       SLEEP_PULL_DOWN_MASK) << 16;
			arg |= (reg >> PULL_DOWN_SHIFT) & PULL_DOWN_MASK;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			arg = (reg >> INPUT_SCHMITT_SHIFT) & INPUT_SCHMITT_MASK;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			/* combine sleep pull up and pull up config */
			arg = ((reg >> SLEEP_PULL_UP_SHIFT) &
			       SLEEP_PULL_UP_MASK) << 16;
			arg |= (reg >> PULL_UP_SHIFT) & PULL_UP_MASK;
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			if ((reg & (SLEEP_PULL_DOWN | SLEEP_PULL_UP)) ||
			    (reg & (PULL_DOWN | PULL_UP_4_7K | PULL_UP_20K)))
				return -EINVAL;

			arg = 1;
			break;
		case PIN_CONFIG_SLEEP_HARDWARE_STATE:
			arg = 0;
			break;
		default:
			return -ENOTSUPP;
		}
	} else {
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static unsigned int sprd_pinconf_drive(unsigned int mA)
{
	unsigned int val = 0;

	switch (mA) {
	case 2:
		break;
	case 4:
		val |= BIT(19);
		break;
	case 6:
		val |= BIT(20);
		break;
	case 8:
		val |= BIT(19) | BIT(20);
		break;
	case 10:
		val |= BIT(21);
		break;
	case 12:
		val |= BIT(21) | BIT(19);
		break;
	case 14:
		val |= BIT(21) | BIT(20);
		break;
	case 16:
		val |= BIT(19) | BIT(20) | BIT(21);
		break;
	case 20:
		val |= BIT(22);
		break;
	case 21:
		val |= BIT(22) | BIT(19);
		break;
	case 24:
		val |= BIT(22) | BIT(20);
		break;
	case 25:
		val |= BIT(22) | BIT(20) | BIT(19);
		break;
	case 27:
		val |= BIT(22) | BIT(21);
		break;
	case 29:
		val |= BIT(22) | BIT(21) | BIT(19);
		break;
	case 31:
		val |= BIT(22) | BIT(21) | BIT(20);
		break;
	case 33:
		val |= BIT(22) | BIT(21) | BIT(20) | BIT(19);
		break;
	default:
		break;
	}

	return val;
}

static bool sprd_pinctrl_check_sleep_config(unsigned long *configs,
					    unsigned int num_configs)
{
	unsigned int param;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		if (param == PIN_CONFIG_SLEEP_HARDWARE_STATE)
			return true;
	}

	return false;
}

static int sprd_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin_id,
			    unsigned long *configs, unsigned int num_configs)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pin *pin = sprd_pinctrl_get_pin_by_id(pctl, pin_id);
	bool is_sleep_config;
	unsigned long reg;
	int i;

	if (!pin)
		return -EINVAL;

	is_sleep_config = sprd_pinctrl_check_sleep_config(configs, num_configs);

	for (i = 0; i < num_configs; i++) {
		unsigned int param, arg, shift, mask, val;

		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		val = 0;
		shift = 0;
		mask = 0;
		if (pin->type == GLOBAL_CTRL_PIN &&
		    param == SPRD_PIN_CONFIG_CONTROL) {
			val = arg;
		} else if (pin->type == COMMON_PIN || pin->type == MISC_PIN) {
			switch (param) {
			case SPRD_PIN_CONFIG_SLEEP_MODE:
				if (arg & AP_SLEEP)
					val |= AP_SLEEP_MODE;
				if (arg & PUBCP_SLEEP)
					val |= PUBCP_SLEEP_MODE;
				if (arg & TGLDSP_SLEEP)
					val |= TGLDSP_SLEEP_MODE;
				if (arg & AGDSP_SLEEP)
					val |= AGDSP_SLEEP_MODE;
				if (arg & CM4_SLEEP)
					val |= CM4_SLEEP_MODE;

				mask = SLEEP_MODE_MASK;
				shift = SLEEP_MODE_SHIFT;
				break;
			case PIN_CONFIG_INPUT_ENABLE:
				if (is_sleep_config == true) {
					if (arg > 0)
						val |= SLEEP_INPUT;
					else
						val &= ~SLEEP_INPUT;

					mask = SLEEP_INPUT_MASK;
					shift = SLEEP_INPUT_SHIFT;
				}
				break;
			case PIN_CONFIG_OUTPUT:
				if (is_sleep_config == true) {
					val |= SLEEP_OUTPUT;
					mask = SLEEP_OUTPUT_MASK;
					shift = SLEEP_OUTPUT_SHIFT;
				}
				break;
			case PIN_CONFIG_DRIVE_STRENGTH:
				if (arg < 2 || arg > 60)
					return -EINVAL;

				val = sprd_pinconf_drive(arg);
				mask = DRIVE_STRENGTH_MASK;
				shift = DRIVE_STRENGTH_SHIFT;
				break;
			case PIN_CONFIG_BIAS_PULL_DOWN:
				if (is_sleep_config == true) {
					val |= SLEEP_PULL_DOWN;
					mask = SLEEP_PULL_DOWN_MASK;
					shift = SLEEP_PULL_DOWN_SHIFT;
				} else {
					val |= PULL_DOWN;
					mask = PULL_DOWN_MASK;
					shift = PULL_DOWN_SHIFT;
				}
				break;
			case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
				if (arg > 0)
					val |= INPUT_SCHMITT;
				else
					val &= ~INPUT_SCHMITT;

				mask = INPUT_SCHMITT_MASK;
				shift = INPUT_SCHMITT_SHIFT;
				break;
			case PIN_CONFIG_BIAS_PULL_UP:
				if (is_sleep_config == true) {
					val |= SLEEP_PULL_UP;
					mask = SLEEP_PULL_UP_MASK;
					shift = SLEEP_PULL_UP_SHIFT;
				} else {
					if (arg == 20000)
						val |= PULL_UP_20K;
					else if (arg == 4700)
						val |= PULL_UP_4_7K;

					mask = PULL_UP_MASK;
					shift = PULL_UP_SHIFT;
				}
				break;
			case PIN_CONFIG_BIAS_DISABLE:
				if (is_sleep_config == true) {
					val = shift = 0;
					mask = SLEEP_PULL_DOWN | SLEEP_PULL_UP;
				} else {
					val = shift = 0;
					mask = PULL_DOWN | PULL_UP_20K |
						PULL_UP_4_7K;
				}
				break;
			case PIN_CONFIG_SLEEP_HARDWARE_STATE:
				continue;
			default:
				return -ENOTSUPP;
			}
		} else {
			return -ENOTSUPP;
		}

		if (pin->type == GLOBAL_CTRL_PIN) {
			reg = readl((void __iomem *)pin->reg);
			reg &= ~(PINCTRL_BIT_MASK(pin->bit_width)
				<< pin->bit_offset);
			reg |= (val & PINCTRL_BIT_MASK(pin->bit_width))
				<< pin->bit_offset;
			writel(reg, (void __iomem *)pin->reg);
		} else {
			reg = readl((void __iomem *)pin->reg);
			reg &= ~(mask << shift);
			reg |= val;
			writel(reg, (void __iomem *)pin->reg);
		}
	}

	return 0;
}

static int sprd_pinconf_group_get(struct pinctrl_dev *pctldev,
				  unsigned int selector, unsigned long *config)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;
	struct sprd_pin_group *grp;
	unsigned int pin_id;

	if (selector >= info->ngroups)
		return -EINVAL;

	grp = &info->groups[selector];
	pin_id = grp->pins[0];

	return sprd_pinconf_get(pctldev, pin_id, config);
}

static int sprd_pinconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int selector,
				  unsigned long *configs,
				  unsigned int num_configs)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;
	struct sprd_pin_group *grp;
	int ret, i;

	if (selector >= info->ngroups)
		return -EINVAL;

	grp = &info->groups[selector];

	for (i = 0; i < grp->npins; i++) {
		unsigned int pin_id = grp->pins[i];

		ret = sprd_pinconf_set(pctldev, pin_id, configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static int sprd_pinconf_get_config(struct pinctrl_dev *pctldev,
				   unsigned int pin_id,
				   unsigned long *config)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pin *pin = sprd_pinctrl_get_pin_by_id(pctl, pin_id);

	if (!pin)
		return -EINVAL;

	if (pin->type == GLOBAL_CTRL_PIN) {
		*config = (readl((void __iomem *)pin->reg) >>
			   pin->bit_offset) & PINCTRL_BIT_MASK(pin->bit_width);
	} else {
		*config = readl((void __iomem *)pin->reg);
	}

	return 0;
}

static void sprd_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *s, unsigned int pin_id)
{
	unsigned long config;
	int ret;

	ret = sprd_pinconf_get_config(pctldev, pin_id, &config);
	if (ret)
		return;

	seq_printf(s, "0x%lx", config);
}

static void sprd_pinconf_group_dbg_show(struct pinctrl_dev *pctldev,
					struct seq_file *s,
					unsigned int selector)
{
	struct sprd_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct sprd_pinctrl_soc_info *info = pctl->info;
	struct sprd_pin_group *grp;
	unsigned long config;
	const char *name;
	int i, ret;

	if (selector >= info->ngroups)
		return;

	grp = &info->groups[selector];

	seq_putc(s, '\n');
	for (i = 0; i < grp->npins; i++, config++) {
		unsigned int pin_id = grp->pins[i];

		name = pin_get_name(pctldev, pin_id);
		ret = sprd_pinconf_get_config(pctldev, pin_id, &config);
		if (ret)
			return;

		seq_printf(s, "%s: 0x%lx ", name, config);
	}
}

static const struct pinconf_ops sprd_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = sprd_pinconf_get,
	.pin_config_set = sprd_pinconf_set,
	.pin_config_group_get = sprd_pinconf_group_get,
	.pin_config_group_set = sprd_pinconf_group_set,
	.pin_config_dbg_show = sprd_pinconf_dbg_show,
	.pin_config_group_dbg_show = sprd_pinconf_group_dbg_show,
};

static const struct pinconf_generic_params sprd_dt_params[] = {
	{"sprd,control", SPRD_PIN_CONFIG_CONTROL, 0},
	{"sprd,sleep-mode", SPRD_PIN_CONFIG_SLEEP_MODE, 0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item sprd_conf_items[] = {
	PCONFDUMP(SPRD_PIN_CONFIG_CONTROL, "global control", NULL, true),
	PCONFDUMP(SPRD_PIN_CONFIG_SLEEP_MODE, "sleep mode", NULL, true),
};
#endif

static struct pinctrl_desc sprd_pinctrl_desc = {
	.pctlops = &sprd_pctrl_ops,
	.pmxops = &sprd_pmx_ops,
	.confops = &sprd_pinconf_ops,
	.num_custom_params = ARRAY_SIZE(sprd_dt_params),
	.custom_params = sprd_dt_params,
#ifdef CONFIG_DEBUG_FS
	.custom_conf_items = sprd_conf_items,
#endif
	.owner = THIS_MODULE,
};

static int sprd_pinctrl_parse_groups(struct device_node *np,
				     struct sprd_pinctrl *sprd_pctl,
				     struct sprd_pin_group *grp)
{
	struct property *prop;
	const char *pin_name;
	int ret, i = 0;

	ret = of_property_count_strings(np, "pins");
	if (ret < 0)
		return ret;

	grp->name = np->name;
	grp->npins = ret;
	grp->pins = devm_kcalloc(sprd_pctl->dev,
				 grp->npins, sizeof(unsigned int),
				 GFP_KERNEL);
	if (!grp->pins)
		return -ENOMEM;

	of_property_for_each_string(np, "pins", prop, pin_name) {
		ret = sprd_pinctrl_get_id_by_name(sprd_pctl, pin_name);
		if (ret >= 0)
			grp->pins[i++] = ret;
	}

	for (i = 0; i < grp->npins; i++) {
		dev_dbg(sprd_pctl->dev,
			"Group[%s] contains [%d] pins: id = %d\n",
			grp->name, grp->npins, grp->pins[i]);
	}

	return 0;
}

static unsigned int sprd_pinctrl_get_groups(struct device_node *np)
{
	struct device_node *child;
	unsigned int group_cnt, cnt;

	group_cnt = of_get_child_count(np);

	for_each_child_of_node(np, child) {
		cnt = of_get_child_count(child);
		if (cnt > 0)
			group_cnt += cnt;
	}

	return group_cnt;
}

static int sprd_pinctrl_parse_dt(struct sprd_pinctrl *sprd_pctl)
{
	struct sprd_pinctrl_soc_info *info = sprd_pctl->info;
	struct device_node *np = sprd_pctl->dev->of_node;
	struct device_node *child, *sub_child;
	struct sprd_pin_group *grp;
	const char **temp;
	int ret;

	if (!np)
		return -ENODEV;

	info->ngroups = sprd_pinctrl_get_groups(np);
	if (!info->ngroups)
		return 0;

	info->groups = devm_kcalloc(sprd_pctl->dev,
				    info->ngroups,
				    sizeof(struct sprd_pin_group),
				    GFP_KERNEL);
	if (!info->groups)
		return -ENOMEM;

	info->grp_names = devm_kcalloc(sprd_pctl->dev,
				       info->ngroups, sizeof(char *),
				       GFP_KERNEL);
	if (!info->grp_names)
		return -ENOMEM;

	temp = info->grp_names;
	grp = info->groups;

	for_each_child_of_node(np, child) {
		ret = sprd_pinctrl_parse_groups(child, sprd_pctl, grp);
		if (ret) {
			of_node_put(child);
			return ret;
		}

		*temp++ = grp->name;
		grp++;

		if (of_get_child_count(child) > 0) {
			for_each_child_of_node(child, sub_child) {
				ret = sprd_pinctrl_parse_groups(sub_child,
								sprd_pctl, grp);
				if (ret) {
					of_node_put(sub_child);
					of_node_put(child);
					return ret;
				}

				*temp++ = grp->name;
				grp++;
			}
		}
	}

	return 0;
}

static int sprd_pinctrl_add_pins(struct sprd_pinctrl *sprd_pctl,
				 struct sprd_pins_info *sprd_soc_pin_info,
				 int pins_cnt)
{
	struct sprd_pinctrl_soc_info *info = sprd_pctl->info;
	unsigned int ctrl_pin = 0, com_pin = 0;
	struct sprd_pin *pin;
	int i;

	info->npins = pins_cnt;
	info->pins = devm_kcalloc(sprd_pctl->dev,
				  info->npins, sizeof(struct sprd_pin),
				  GFP_KERNEL);
	if (!info->pins)
		return -ENOMEM;

	for (i = 0, pin = info->pins; i < info->npins; i++, pin++) {
		unsigned int reg;

		pin->name = sprd_soc_pin_info[i].name;
		pin->type = sprd_soc_pin_info[i].type;
		pin->number = sprd_soc_pin_info[i].num;
		reg = sprd_soc_pin_info[i].reg;
		if (pin->type == GLOBAL_CTRL_PIN) {
			pin->reg = (unsigned long)sprd_pctl->base +
				PINCTRL_REG_LEN * reg;
			pin->bit_offset = sprd_soc_pin_info[i].bit_offset;
			pin->bit_width = sprd_soc_pin_info[i].bit_width;
			ctrl_pin++;
		} else if (pin->type == COMMON_PIN) {
			pin->reg = (unsigned long)sprd_pctl->base +
				PINCTRL_REG_OFFSET + PINCTRL_REG_LEN *
				(i - ctrl_pin);
			com_pin++;
		} else if (pin->type == MISC_PIN) {
			pin->reg = (unsigned long)sprd_pctl->base +
				PINCTRL_REG_MISC_OFFSET + PINCTRL_REG_LEN *
				(i - ctrl_pin - com_pin);
		}
	}

	for (i = 0, pin = info->pins; i < info->npins; pin++, i++) {
		dev_dbg(sprd_pctl->dev, "pin name[%s-%d], type = %d, "
			"bit offset = %ld, bit width = %ld, reg = 0x%lx\n",
			pin->name, pin->number, pin->type,
			pin->bit_offset, pin->bit_width, pin->reg);
	}

	return 0;
}

int sprd_pinctrl_core_probe(struct platform_device *pdev,
			    struct sprd_pins_info *sprd_soc_pin_info,
			    int pins_cnt)
{
	struct sprd_pinctrl *sprd_pctl;
	struct sprd_pinctrl_soc_info *pinctrl_info;
	struct pinctrl_pin_desc *pin_desc;
	int ret, i;

	sprd_pctl = devm_kzalloc(&pdev->dev, sizeof(struct sprd_pinctrl),
				 GFP_KERNEL);
	if (!sprd_pctl)
		return -ENOMEM;

	sprd_pctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sprd_pctl->base))
		return PTR_ERR(sprd_pctl->base);

	pinctrl_info = devm_kzalloc(&pdev->dev,
				    sizeof(struct sprd_pinctrl_soc_info),
				    GFP_KERNEL);
	if (!pinctrl_info)
		return -ENOMEM;

	sprd_pctl->info = pinctrl_info;
	sprd_pctl->dev = &pdev->dev;
	platform_set_drvdata(pdev, sprd_pctl);

	ret = sprd_pinctrl_add_pins(sprd_pctl, sprd_soc_pin_info, pins_cnt);
	if (ret) {
		dev_err(&pdev->dev, "fail to add pins information\n");
		return ret;
	}

	ret = sprd_pinctrl_parse_dt(sprd_pctl);
	if (ret) {
		dev_err(&pdev->dev, "fail to parse dt properties\n");
		return ret;
	}

	pin_desc = devm_kcalloc(&pdev->dev,
				pinctrl_info->npins,
				sizeof(struct pinctrl_pin_desc),
				GFP_KERNEL);
	if (!pin_desc)
		return -ENOMEM;

	for (i = 0; i < pinctrl_info->npins; i++) {
		pin_desc[i].number = pinctrl_info->pins[i].number;
		pin_desc[i].name = pinctrl_info->pins[i].name;
		pin_desc[i].drv_data = pinctrl_info;
	}

	sprd_pinctrl_desc.pins = pin_desc;
	sprd_pinctrl_desc.name = dev_name(&pdev->dev);
	sprd_pinctrl_desc.npins = pinctrl_info->npins;

	sprd_pctl->pctl = pinctrl_register(&sprd_pinctrl_desc,
					   &pdev->dev, (void *)sprd_pctl);
	if (IS_ERR(sprd_pctl->pctl)) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return PTR_ERR(sprd_pctl->pctl);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sprd_pinctrl_core_probe);

int sprd_pinctrl_remove(struct platform_device *pdev)
{
	struct sprd_pinctrl *sprd_pctl = platform_get_drvdata(pdev);

	pinctrl_unregister(sprd_pctl->pctl);
	return 0;
}
EXPORT_SYMBOL_GPL(sprd_pinctrl_remove);

void sprd_pinctrl_shutdown(struct platform_device *pdev)
{
	struct pinctrl *pinctl;
	struct pinctrl_state *state;

	pinctl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pinctl))
		return;
	state = pinctrl_lookup_state(pinctl, "shutdown");
	if (IS_ERR(state))
		return;
	pinctrl_select_state(pinctl, state);
}
EXPORT_SYMBOL_GPL(sprd_pinctrl_shutdown);

MODULE_DESCRIPTION("SPREADTRUM Pin Controller Driver");
MODULE_AUTHOR("Baolin Wang <baolin.wang@spreadtrum.com>");
MODULE_LICENSE("GPL v2");
