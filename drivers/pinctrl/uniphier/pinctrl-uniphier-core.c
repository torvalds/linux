// SPDX-License-Identifier: GPL-2.0+
//
// Copyright (C) 2015-2017 Socionext Inc.
//   Author: Masahiro Yamada <yamada.masahiro@socionext.com>

#include <linux/list.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "pinctrl-uniphier.h"

#define UNIPHIER_PINCTRL_PINMUX_BASE	0x1000
#define UNIPHIER_PINCTRL_LOAD_PINMUX	0x1700
#define UNIPHIER_PINCTRL_DRVCTRL_BASE	0x1800
#define UNIPHIER_PINCTRL_DRV2CTRL_BASE	0x1900
#define UNIPHIER_PINCTRL_DRV3CTRL_BASE	0x1980
#define UNIPHIER_PINCTRL_PUPDCTRL_BASE	0x1a00
#define UNIPHIER_PINCTRL_IECTRL_BASE	0x1d00

struct uniphier_pinctrl_reg_region {
	struct list_head node;
	unsigned int base;
	unsigned int nregs;
	u32 vals[0];
};

struct uniphier_pinctrl_priv {
	struct pinctrl_desc pctldesc;
	struct pinctrl_dev *pctldev;
	struct regmap *regmap;
	const struct uniphier_pinctrl_socdata *socdata;
	struct list_head reg_regions;
};

static int uniphier_pctl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->socdata->groups_count;
}

static const char *uniphier_pctl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned selector)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->socdata->groups[selector].name;
}

static int uniphier_pctl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned selector,
					const unsigned **pins,
					unsigned *num_pins)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	*pins = priv->socdata->groups[selector].pins;
	*num_pins = priv->socdata->groups[selector].num_pins;

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void uniphier_pctl_pin_dbg_show(struct pinctrl_dev *pctldev,
				       struct seq_file *s, unsigned offset)
{
	const struct pin_desc *desc = pin_desc_get(pctldev, offset);
	const char *pull_dir, *drv_type;

	switch (uniphier_pin_get_pull_dir(desc->drv_data)) {
	case UNIPHIER_PIN_PULL_UP:
		pull_dir = "UP";
		break;
	case UNIPHIER_PIN_PULL_DOWN:
		pull_dir = "DOWN";
		break;
	case UNIPHIER_PIN_PULL_UP_FIXED:
		pull_dir = "UP(FIXED)";
		break;
	case UNIPHIER_PIN_PULL_DOWN_FIXED:
		pull_dir = "DOWN(FIXED)";
		break;
	case UNIPHIER_PIN_PULL_NONE:
		pull_dir = "NONE";
		break;
	default:
		BUG();
	}

	switch (uniphier_pin_get_drv_type(desc->drv_data)) {
	case UNIPHIER_PIN_DRV_1BIT:
		drv_type = "4/8(mA)";
		break;
	case UNIPHIER_PIN_DRV_2BIT:
		drv_type = "8/12/16/20(mA)";
		break;
	case UNIPHIER_PIN_DRV_3BIT:
		drv_type = "4/5/7/9/11/12/14/16(mA)";
		break;
	case UNIPHIER_PIN_DRV_FIXED4:
		drv_type = "4(mA)";
		break;
	case UNIPHIER_PIN_DRV_FIXED5:
		drv_type = "5(mA)";
		break;
	case UNIPHIER_PIN_DRV_FIXED8:
		drv_type = "8(mA)";
		break;
	case UNIPHIER_PIN_DRV_NONE:
		drv_type = "NONE";
		break;
	default:
		BUG();
	}

	seq_printf(s, " PULL_DIR=%s  DRV_TYPE=%s", pull_dir, drv_type);
}
#endif

static const struct pinctrl_ops uniphier_pctlops = {
	.get_groups_count = uniphier_pctl_get_groups_count,
	.get_group_name = uniphier_pctl_get_group_name,
	.get_group_pins = uniphier_pctl_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show = uniphier_pctl_pin_dbg_show,
#endif
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

static const unsigned int uniphier_conf_drv_strengths_1bit[] = {4, 8};
static const unsigned int uniphier_conf_drv_strengths_2bit[] = {8, 12, 16, 20};
static const unsigned int uniphier_conf_drv_strengths_3bit[] = {4, 5, 7, 9, 11,
								12, 14, 16};
static const unsigned int uniphier_conf_drv_strengths_fixed4[] = {4};
static const unsigned int uniphier_conf_drv_strengths_fixed5[] = {5};
static const unsigned int uniphier_conf_drv_strengths_fixed8[] = {8};

static int uniphier_conf_get_drvctrl_data(struct pinctrl_dev *pctldev,
					  unsigned int pin, unsigned int *reg,
					  unsigned int *shift,
					  unsigned int *mask,
					  const unsigned int **strengths)
{
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	enum uniphier_pin_drv_type type =
				uniphier_pin_get_drv_type(desc->drv_data);
	unsigned int base = 0;
	unsigned int stride = 0;
	unsigned int width = 0;
	unsigned int drvctrl;

	switch (type) {
	case UNIPHIER_PIN_DRV_1BIT:
		*strengths = uniphier_conf_drv_strengths_1bit;
		base = UNIPHIER_PINCTRL_DRVCTRL_BASE;
		stride = 1;
		width = 1;
		break;
	case UNIPHIER_PIN_DRV_2BIT:
		*strengths = uniphier_conf_drv_strengths_2bit;
		base = UNIPHIER_PINCTRL_DRV2CTRL_BASE;
		stride = 2;
		width = 2;
		break;
	case UNIPHIER_PIN_DRV_3BIT:
		*strengths = uniphier_conf_drv_strengths_3bit;
		base = UNIPHIER_PINCTRL_DRV3CTRL_BASE;
		stride = 4;
		width = 3;
		break;
	case UNIPHIER_PIN_DRV_FIXED4:
		*strengths = uniphier_conf_drv_strengths_fixed4;
		break;
	case UNIPHIER_PIN_DRV_FIXED5:
		*strengths = uniphier_conf_drv_strengths_fixed5;
		break;
	case UNIPHIER_PIN_DRV_FIXED8:
		*strengths = uniphier_conf_drv_strengths_fixed8;
		break;
	default:
		/* drive strength control is not supported for this pin */
		return -EINVAL;
	}

	drvctrl = uniphier_pin_get_drvctrl(desc->drv_data);
	drvctrl *= stride;

	*reg = base + drvctrl / 32 * 4;
	*shift = drvctrl % 32;
	*mask = (1U << width) - 1;

	return 0;
}

static int uniphier_conf_pin_bias_get(struct pinctrl_dev *pctldev,
				      unsigned int pin,
				      enum pin_config_param param)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	enum uniphier_pin_pull_dir pull_dir =
				uniphier_pin_get_pull_dir(desc->drv_data);
	unsigned int pupdctrl, reg, shift, val;
	unsigned int expected = 1;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pull_dir == UNIPHIER_PIN_PULL_NONE)
			return 0;
		if (pull_dir == UNIPHIER_PIN_PULL_UP_FIXED ||
		    pull_dir == UNIPHIER_PIN_PULL_DOWN_FIXED)
			return -EINVAL;
		expected = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pull_dir == UNIPHIER_PIN_PULL_UP_FIXED)
			return 0;
		if (pull_dir != UNIPHIER_PIN_PULL_UP)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (pull_dir == UNIPHIER_PIN_PULL_DOWN_FIXED)
			return 0;
		if (pull_dir != UNIPHIER_PIN_PULL_DOWN)
			return -EINVAL;
		break;
	default:
		BUG();
	}

	pupdctrl = uniphier_pin_get_pupdctrl(desc->drv_data);

	reg = UNIPHIER_PINCTRL_PUPDCTRL_BASE + pupdctrl / 32 * 4;
	shift = pupdctrl % 32;

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret)
		return ret;

	val = (val >> shift) & 1;

	return (val == expected) ? 0 : -EINVAL;
}

static int uniphier_conf_pin_drive_get(struct pinctrl_dev *pctldev,
				       unsigned int pin, u32 *strength)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int reg, shift, mask, val;
	const unsigned int *strengths;
	int ret;

	ret = uniphier_conf_get_drvctrl_data(pctldev, pin, &reg, &shift,
					     &mask, &strengths);
	if (ret)
		return ret;

	if (mask) {
		ret = regmap_read(priv->regmap, reg, &val);
		if (ret)
			return ret;
	} else {
		val = 0;
	}

	*strength = strengths[(val >> shift) & mask];

	return 0;
}

static int uniphier_conf_pin_input_enable_get(struct pinctrl_dev *pctldev,
					      unsigned int pin)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	unsigned int iectrl = uniphier_pin_get_iectrl(desc->drv_data);
	unsigned int reg, mask, val;
	int ret;

	if (iectrl == UNIPHIER_PIN_IECTRL_NONE)
		/* This pin is always input-enabled. */
		return 0;

	if (priv->socdata->caps & UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL)
		iectrl = pin;

	reg = UNIPHIER_PINCTRL_IECTRL_BASE + iectrl / 32 * 4;
	mask = BIT(iectrl % 32);

	ret = regmap_read(priv->regmap, reg, &val);
	if (ret)
		return ret;

	return val & mask ? 0 : -EINVAL;
}

static int uniphier_conf_pin_config_get(struct pinctrl_dev *pctldev,
					unsigned pin,
					unsigned long *configs)
{
	enum pin_config_param param = pinconf_to_config_param(*configs);
	bool has_arg = false;
	u32 arg;
	int ret;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = uniphier_conf_pin_bias_get(pctldev, pin, param);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = uniphier_conf_pin_drive_get(pctldev, pin, &arg);
		has_arg = true;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		ret = uniphier_conf_pin_input_enable_get(pctldev, pin);
		break;
	default:
		/* unsupported parameter */
		ret = -EINVAL;
		break;
	}

	if (ret == 0 && has_arg)
		*configs = pinconf_to_config_packed(param, arg);

	return ret;
}

static int uniphier_conf_pin_bias_set(struct pinctrl_dev *pctldev,
				      unsigned int pin,
				      enum pin_config_param param, u32 arg)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	enum uniphier_pin_pull_dir pull_dir =
				uniphier_pin_get_pull_dir(desc->drv_data);
	unsigned int pupdctrl, reg, shift;
	unsigned int val = 1;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pull_dir == UNIPHIER_PIN_PULL_NONE)
			return 0;
		if (pull_dir == UNIPHIER_PIN_PULL_UP_FIXED ||
		    pull_dir == UNIPHIER_PIN_PULL_DOWN_FIXED) {
			dev_err(pctldev->dev,
				"can not disable pull register for pin %s\n",
				desc->name);
			return -EINVAL;
		}
		val = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pull_dir == UNIPHIER_PIN_PULL_UP_FIXED && arg != 0)
			return 0;
		if (pull_dir != UNIPHIER_PIN_PULL_UP) {
			dev_err(pctldev->dev,
				"pull-up is unsupported for pin %s\n",
				desc->name);
			return -EINVAL;
		}
		if (arg == 0) {
			dev_err(pctldev->dev, "pull-up can not be total\n");
			return -EINVAL;
		}
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (pull_dir == UNIPHIER_PIN_PULL_DOWN_FIXED && arg != 0)
			return 0;
		if (pull_dir != UNIPHIER_PIN_PULL_DOWN) {
			dev_err(pctldev->dev,
				"pull-down is unsupported for pin %s\n",
				desc->name);
			return -EINVAL;
		}
		if (arg == 0) {
			dev_err(pctldev->dev, "pull-down can not be total\n");
			return -EINVAL;
		}
		break;
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		if (pull_dir == UNIPHIER_PIN_PULL_NONE) {
			dev_err(pctldev->dev,
				"pull-up/down is unsupported for pin %s\n",
				desc->name);
			return -EINVAL;
		}

		if (arg == 0)
			return 0; /* configuration ingored */
		break;
	default:
		BUG();
	}

	pupdctrl = uniphier_pin_get_pupdctrl(desc->drv_data);

	reg = UNIPHIER_PINCTRL_PUPDCTRL_BASE + pupdctrl / 32 * 4;
	shift = pupdctrl % 32;

	return regmap_update_bits(priv->regmap, reg, 1 << shift, val << shift);
}

static int uniphier_conf_pin_drive_set(struct pinctrl_dev *pctldev,
				       unsigned int pin, u32 strength)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	unsigned int reg, shift, mask, val;
	const unsigned int *strengths;
	int ret;

	ret = uniphier_conf_get_drvctrl_data(pctldev, pin, &reg, &shift,
					     &mask, &strengths);
	if (ret) {
		dev_err(pctldev->dev, "cannot set drive strength for pin %s\n",
			desc->name);
		return ret;
	}

	for (val = 0; val <= mask; val++) {
		if (strengths[val] > strength)
			break;
	}

	if (val == 0) {
		dev_err(pctldev->dev,
			"unsupported drive strength %u mA for pin %s\n",
			strength, desc->name);
		return -EINVAL;
	}

	if (!mask)
		return 0;

	val--;

	return regmap_update_bits(priv->regmap, reg,
				  mask << shift, val << shift);
}

static int uniphier_conf_pin_input_enable(struct pinctrl_dev *pctldev,
					  unsigned int pin, u32 enable)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct pin_desc *desc = pin_desc_get(pctldev, pin);
	unsigned int iectrl = uniphier_pin_get_iectrl(desc->drv_data);
	unsigned int reg, mask;

	/*
	 * Multiple pins share one input enable, per-pin disabling is
	 * impossible.
	 */
	if (!(priv->socdata->caps & UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL) &&
	    !enable)
		return -EINVAL;

	/* UNIPHIER_PIN_IECTRL_NONE means the pin is always input-enabled */
	if (iectrl == UNIPHIER_PIN_IECTRL_NONE)
		return enable ? 0 : -EINVAL;

	if (priv->socdata->caps & UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL)
		iectrl = pin;

	reg = UNIPHIER_PINCTRL_IECTRL_BASE + iectrl / 32 * 4;
	mask = BIT(iectrl % 32);

	return regmap_update_bits(priv->regmap, reg, mask, enable ? mask : 0);
}

static int uniphier_conf_pin_config_set(struct pinctrl_dev *pctldev,
					unsigned pin,
					unsigned long *configs,
					unsigned num_configs)
{
	int i, ret;

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param =
					pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
			ret = uniphier_conf_pin_bias_set(pctldev, pin,
							 param, arg);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			ret = uniphier_conf_pin_drive_set(pctldev, pin, arg);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			ret = uniphier_conf_pin_input_enable(pctldev, pin, arg);
			break;
		default:
			dev_err(pctldev->dev,
				"unsupported configuration parameter %u\n",
				param);
			return -EINVAL;
		}

		if (ret)
			return ret;
	}

	return 0;
}

static int uniphier_conf_pin_config_group_set(struct pinctrl_dev *pctldev,
					      unsigned selector,
					      unsigned long *configs,
					      unsigned num_configs)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const unsigned *pins = priv->socdata->groups[selector].pins;
	unsigned num_pins = priv->socdata->groups[selector].num_pins;
	int i, ret;

	for (i = 0; i < num_pins; i++) {
		ret = uniphier_conf_pin_config_set(pctldev, pins[i],
						   configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops uniphier_confops = {
	.is_generic = true,
	.pin_config_get = uniphier_conf_pin_config_get,
	.pin_config_set = uniphier_conf_pin_config_set,
	.pin_config_group_set = uniphier_conf_pin_config_group_set,
};

static int uniphier_pmx_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->socdata->functions_count;
}

static const char *uniphier_pmx_get_function_name(struct pinctrl_dev *pctldev,
						  unsigned selector)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	return priv->socdata->functions[selector].name;
}

static int uniphier_pmx_get_function_groups(struct pinctrl_dev *pctldev,
					    unsigned selector,
					    const char * const **groups,
					    unsigned *num_groups)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	*groups = priv->socdata->functions[selector].groups;
	*num_groups = priv->socdata->functions[selector].num_groups;

	return 0;
}

static int uniphier_pmx_set_one_mux(struct pinctrl_dev *pctldev, unsigned pin,
				    int muxval)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int mux_bits, reg_stride, reg, reg_end, shift, mask;
	bool load_pinctrl;
	int ret;

	/* some pins need input-enabling */
	ret = uniphier_conf_pin_input_enable(pctldev, pin, 1);
	if (ret)
		return ret;

	if (muxval < 0)
		return 0;	/* dedicated pin; nothing to do for pin-mux */

	if (priv->socdata->caps & UNIPHIER_PINCTRL_CAPS_DBGMUX_SEPARATE) {
		/*
		 *  Mode     reg_offset     bit_position
		 *  Normal    4 * n        shift+3:shift
		 *  Debug     4 * n        shift+7:shift+4
		 */
		mux_bits = 4;
		reg_stride = 8;
		load_pinctrl = true;
	} else {
		/*
		 *  Mode     reg_offset     bit_position
		 *  Normal    8 * n        shift+3:shift
		 *  Debug     8 * n + 4    shift+3:shift
		 */
		mux_bits = 8;
		reg_stride = 4;
		load_pinctrl = false;
	}

	reg = UNIPHIER_PINCTRL_PINMUX_BASE + pin * mux_bits / 32 * reg_stride;
	reg_end = reg + reg_stride;
	shift = pin * mux_bits % 32;
	mask = (1U << mux_bits) - 1;

	/*
	 * If reg_stride is greater than 4, the MSB of each pinsel shall be
	 * stored in the offset+4.
	 */
	for (; reg < reg_end; reg += 4) {
		ret = regmap_update_bits(priv->regmap, reg,
					 mask << shift, muxval << shift);
		if (ret)
			return ret;
		muxval >>= mux_bits;
	}

	if (load_pinctrl) {
		ret = regmap_write(priv->regmap,
				   UNIPHIER_PINCTRL_LOAD_PINMUX, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int uniphier_pmx_set_mux(struct pinctrl_dev *pctldev,
				unsigned func_selector,
				unsigned group_selector)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	const struct uniphier_pinctrl_group *grp =
					&priv->socdata->groups[group_selector];
	int i;
	int ret;

	for (i = 0; i < grp->num_pins; i++) {
		ret = uniphier_pmx_set_one_mux(pctldev, grp->pins[i],
					       grp->muxvals[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int uniphier_pmx_gpio_request_enable(struct pinctrl_dev *pctldev,
					    struct pinctrl_gpio_range *range,
					    unsigned offset)
{
	struct uniphier_pinctrl_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	unsigned int gpio_offset;
	int muxval, i;

	if (range->pins) {
		for (i = 0; i < range->npins; i++)
			if (range->pins[i] == offset)
				break;

		if (WARN_ON(i == range->npins))
			return -EINVAL;

		gpio_offset = i;
	} else {
		gpio_offset = offset - range->pin_base;
	}

	gpio_offset += range->id;

	muxval = priv->socdata->get_gpio_muxval(offset, gpio_offset);

	return uniphier_pmx_set_one_mux(pctldev, offset, muxval);
}

static const struct pinmux_ops uniphier_pmxops = {
	.get_functions_count = uniphier_pmx_get_functions_count,
	.get_function_name = uniphier_pmx_get_function_name,
	.get_function_groups = uniphier_pmx_get_function_groups,
	.set_mux = uniphier_pmx_set_mux,
	.gpio_request_enable = uniphier_pmx_gpio_request_enable,
	.strict = true,
};

#ifdef CONFIG_PM_SLEEP
static int uniphier_pinctrl_suspend(struct device *dev)
{
	struct uniphier_pinctrl_priv *priv = dev_get_drvdata(dev);
	struct uniphier_pinctrl_reg_region *r;
	int ret;

	list_for_each_entry(r, &priv->reg_regions, node) {
		ret = regmap_bulk_read(priv->regmap, r->base, r->vals,
				       r->nregs);
		if (ret)
			return ret;
	}

	return 0;
}

static int uniphier_pinctrl_resume(struct device *dev)
{
	struct uniphier_pinctrl_priv *priv = dev_get_drvdata(dev);
	struct uniphier_pinctrl_reg_region *r;
	int ret;

	list_for_each_entry(r, &priv->reg_regions, node) {
		ret = regmap_bulk_write(priv->regmap, r->base, r->vals,
					r->nregs);
		if (ret)
			return ret;
	}

	if (priv->socdata->caps & UNIPHIER_PINCTRL_CAPS_DBGMUX_SEPARATE) {
		ret = regmap_write(priv->regmap,
				   UNIPHIER_PINCTRL_LOAD_PINMUX, 1);
		if (ret)
			return ret;
	}

	return 0;
}

static int uniphier_pinctrl_add_reg_region(struct device *dev,
					   struct uniphier_pinctrl_priv *priv,
					   unsigned int base,
					   unsigned int count,
					   unsigned int width)
{
	struct uniphier_pinctrl_reg_region *region;
	unsigned int nregs;

	if (!count)
		return 0;

	nregs = DIV_ROUND_UP(count * width, 32);

	region = devm_kzalloc(dev, struct_size(region, vals, nregs),
			      GFP_KERNEL);
	if (!region)
		return -ENOMEM;

	region->base = base;
	region->nregs = nregs;

	list_add_tail(&region->node, &priv->reg_regions);

	return 0;
}
#endif

static int uniphier_pinctrl_pm_init(struct device *dev,
				    struct uniphier_pinctrl_priv *priv)
{
#ifdef CONFIG_PM_SLEEP
	const struct uniphier_pinctrl_socdata *socdata = priv->socdata;
	unsigned int num_drvctrl = 0;
	unsigned int num_drv2ctrl = 0;
	unsigned int num_drv3ctrl = 0;
	unsigned int num_pupdctrl = 0;
	unsigned int num_iectrl = 0;
	unsigned int iectrl, drvctrl, pupdctrl;
	enum uniphier_pin_drv_type drv_type;
	enum uniphier_pin_pull_dir pull_dir;
	int i, ret;

	for (i = 0; i < socdata->npins; i++) {
		void *drv_data = socdata->pins[i].drv_data;

		drvctrl = uniphier_pin_get_drvctrl(drv_data);
		drv_type = uniphier_pin_get_drv_type(drv_data);
		pupdctrl = uniphier_pin_get_pupdctrl(drv_data);
		pull_dir = uniphier_pin_get_pull_dir(drv_data);
		iectrl = uniphier_pin_get_iectrl(drv_data);

		switch (drv_type) {
		case UNIPHIER_PIN_DRV_1BIT:
			num_drvctrl = max(num_drvctrl, drvctrl + 1);
			break;
		case UNIPHIER_PIN_DRV_2BIT:
			num_drv2ctrl = max(num_drv2ctrl, drvctrl + 1);
			break;
		case UNIPHIER_PIN_DRV_3BIT:
			num_drv3ctrl = max(num_drv3ctrl, drvctrl + 1);
			break;
		default:
			break;
		}

		if (pull_dir == UNIPHIER_PIN_PULL_UP ||
		    pull_dir == UNIPHIER_PIN_PULL_DOWN)
			num_pupdctrl = max(num_pupdctrl, pupdctrl + 1);

		if (iectrl != UNIPHIER_PIN_IECTRL_NONE) {
			if (socdata->caps & UNIPHIER_PINCTRL_CAPS_PERPIN_IECTRL)
				iectrl = i;
			num_iectrl = max(num_iectrl, iectrl + 1);
		}
	}

	INIT_LIST_HEAD(&priv->reg_regions);

	ret = uniphier_pinctrl_add_reg_region(dev, priv,
					      UNIPHIER_PINCTRL_PINMUX_BASE,
					      socdata->npins, 8);
	if (ret)
		return ret;

	ret = uniphier_pinctrl_add_reg_region(dev, priv,
					      UNIPHIER_PINCTRL_DRVCTRL_BASE,
					      num_drvctrl, 1);
	if (ret)
		return ret;

	ret = uniphier_pinctrl_add_reg_region(dev, priv,
					      UNIPHIER_PINCTRL_DRV2CTRL_BASE,
					      num_drv2ctrl, 2);
	if (ret)
		return ret;

	ret = uniphier_pinctrl_add_reg_region(dev, priv,
					      UNIPHIER_PINCTRL_DRV3CTRL_BASE,
					      num_drv3ctrl, 3);
	if (ret)
		return ret;

	ret = uniphier_pinctrl_add_reg_region(dev, priv,
					      UNIPHIER_PINCTRL_PUPDCTRL_BASE,
					      num_pupdctrl, 1);
	if (ret)
		return ret;

	ret = uniphier_pinctrl_add_reg_region(dev, priv,
					      UNIPHIER_PINCTRL_IECTRL_BASE,
					      num_iectrl, 1);
	if (ret)
		return ret;
#endif
	return 0;
}

const struct dev_pm_ops uniphier_pinctrl_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(uniphier_pinctrl_suspend,
				     uniphier_pinctrl_resume)
};

int uniphier_pinctrl_probe(struct platform_device *pdev,
			   const struct uniphier_pinctrl_socdata *socdata)
{
	struct device *dev = &pdev->dev;
	struct uniphier_pinctrl_priv *priv;
	struct device_node *parent;
	int ret;

	if (!socdata ||
	    !socdata->pins || !socdata->npins ||
	    !socdata->groups || !socdata->groups_count ||
	    !socdata->functions || !socdata->functions_count) {
		dev_err(dev, "pinctrl socdata lacks necessary members\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	parent = of_get_parent(dev->of_node);
	priv->regmap = syscon_node_to_regmap(parent);
	of_node_put(parent);

	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "failed to get regmap\n");
		return PTR_ERR(priv->regmap);
	}

	priv->socdata = socdata;
	priv->pctldesc.name = dev->driver->name;
	priv->pctldesc.pins = socdata->pins;
	priv->pctldesc.npins = socdata->npins;
	priv->pctldesc.pctlops = &uniphier_pctlops;
	priv->pctldesc.pmxops = &uniphier_pmxops;
	priv->pctldesc.confops = &uniphier_confops;
	priv->pctldesc.owner = dev->driver->owner;

	ret = uniphier_pinctrl_pm_init(dev, priv);
	if (ret)
		return ret;

	priv->pctldev = devm_pinctrl_register(dev, &priv->pctldesc, priv);
	if (IS_ERR(priv->pctldev)) {
		dev_err(dev, "failed to register UniPhier pinctrl driver\n");
		return PTR_ERR(priv->pctldev);
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}
