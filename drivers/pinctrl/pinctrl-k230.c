// SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause
/*
 * Copyright (C) 2024 Canaan Bright Sight Co. Ltd
 * Copyright (C) 2024 Ze Huang <18771902331@163.com>
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#include "core.h"
#include "pinconf.h"

#define K230_NPINS 64

#define K230_SHIFT_ST		(0)
#define K230_SHIFT_DS		(1)
#define K230_SHIFT_BIAS		(5)
#define K230_SHIFT_PD		(5)
#define K230_SHIFT_PU		(6)
#define K230_SHIFT_OE		(7)
#define K230_SHIFT_IE		(8)
#define K230_SHIFT_MSC		(9)
#define K230_SHIFT_SL		(10)
#define K230_SHIFT_SEL		(11)

#define K230_PC_ST		BIT(0)
#define K230_PC_DS		GENMASK(4, 1)
#define K230_PC_PD		BIT(5)
#define K230_PC_PU		BIT(6)
#define K230_PC_BIAS		GENMASK(6, 5)
#define K230_PC_OE		BIT(7)
#define K230_PC_IE		BIT(8)
#define K230_PC_MSC		BIT(9)
#define K230_PC_SL		BIT(10)
#define K230_PC_SEL		GENMASK(13, 11)

struct k230_pin_conf {
	unsigned int		func;
	unsigned long		*configs;
	unsigned int		nconfigs;
};

struct k230_pin_group {
	const char		*name;
	unsigned int		*pins;
	unsigned int		num_pins;

	struct k230_pin_conf	*data;
};

struct k230_pmx_func {
	const char		*name;
	const char		**groups;
	unsigned int		*group_idx;
	unsigned int		ngroups;
};

struct k230_pinctrl {
	struct pinctrl_desc	pctl;
	struct pinctrl_dev	*pctl_dev;
	struct regmap		*regmap_base;
	void __iomem		*base;
	struct k230_pin_group	*groups;
	unsigned int		ngroups;
	struct k230_pmx_func	*functions;
	unsigned int		nfunctions;
};

static const struct regmap_config k230_regmap_config = {
	.name		= "canaan,pinctrl",
	.reg_bits	= 32,
	.val_bits	= 32,
	.max_register	= 0x100,
	.reg_stride	= 4,
};

static int k230_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->ngroups;
}

static const char *k230_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned int selector)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->groups[selector].name;
}

static int k230_get_group_pins(struct pinctrl_dev *pctldev,
			       unsigned int selector,
			       const unsigned int **pins,
			       unsigned int *num_pins)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*num_pins = info->groups[selector].num_pins;

	return 0;
}

static inline const struct k230_pmx_func *k230_name_to_funtion(
		const struct k230_pinctrl *info, const char *name)
{
	unsigned int i;

	for (i = 0; i < info->nfunctions; i++) {
		if (!strcmp(info->functions[i].name, name))
			return &info->functions[i];
	}

	return NULL;
}

static struct pinctrl_pin_desc k230_pins[] = {
	PINCTRL_PIN(0,  "IO0"),  PINCTRL_PIN(1,  "IO1"),  PINCTRL_PIN(2,  "IO2"),
	PINCTRL_PIN(3,  "IO3"),  PINCTRL_PIN(4,  "IO4"),  PINCTRL_PIN(5,  "IO5"),
	PINCTRL_PIN(6,  "IO6"),  PINCTRL_PIN(7,  "IO7"),  PINCTRL_PIN(8,  "IO8"),
	PINCTRL_PIN(9,  "IO9"),  PINCTRL_PIN(10, "IO10"), PINCTRL_PIN(11, "IO11"),
	PINCTRL_PIN(12, "IO12"), PINCTRL_PIN(13, "IO13"), PINCTRL_PIN(14, "IO14"),
	PINCTRL_PIN(15, "IO15"), PINCTRL_PIN(16, "IO16"), PINCTRL_PIN(17, "IO17"),
	PINCTRL_PIN(18, "IO18"), PINCTRL_PIN(19, "IO19"), PINCTRL_PIN(20, "IO20"),
	PINCTRL_PIN(21, "IO21"), PINCTRL_PIN(22, "IO22"), PINCTRL_PIN(23, "IO23"),
	PINCTRL_PIN(24, "IO24"), PINCTRL_PIN(25, "IO25"), PINCTRL_PIN(26, "IO26"),
	PINCTRL_PIN(27, "IO27"), PINCTRL_PIN(28, "IO28"), PINCTRL_PIN(29, "IO29"),
	PINCTRL_PIN(30, "IO30"), PINCTRL_PIN(31, "IO31"), PINCTRL_PIN(32, "IO32"),
	PINCTRL_PIN(33, "IO33"), PINCTRL_PIN(34, "IO34"), PINCTRL_PIN(35, "IO35"),
	PINCTRL_PIN(36, "IO36"), PINCTRL_PIN(37, "IO37"), PINCTRL_PIN(38, "IO38"),
	PINCTRL_PIN(39, "IO39"), PINCTRL_PIN(40, "IO40"), PINCTRL_PIN(41, "IO41"),
	PINCTRL_PIN(42, "IO42"), PINCTRL_PIN(43, "IO43"), PINCTRL_PIN(44, "IO44"),
	PINCTRL_PIN(45, "IO45"), PINCTRL_PIN(46, "IO46"), PINCTRL_PIN(47, "IO47"),
	PINCTRL_PIN(48, "IO48"), PINCTRL_PIN(49, "IO49"), PINCTRL_PIN(50, "IO50"),
	PINCTRL_PIN(51, "IO51"), PINCTRL_PIN(52, "IO52"), PINCTRL_PIN(53, "IO53"),
	PINCTRL_PIN(54, "IO54"), PINCTRL_PIN(55, "IO55"), PINCTRL_PIN(56, "IO56"),
	PINCTRL_PIN(57, "IO57"), PINCTRL_PIN(58, "IO58"), PINCTRL_PIN(59, "IO59"),
	PINCTRL_PIN(60, "IO60"), PINCTRL_PIN(61, "IO61"), PINCTRL_PIN(62, "IO62"),
	PINCTRL_PIN(63, "IO63")
};

static void k230_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
				      struct seq_file *s, unsigned int offset)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	u32 val, bias, drive, input, slew, schmitt, power;
	struct k230_pin_group *grp = k230_pins[offset].drv_data;
	static const char * const biasing[] = {
			"pull none", "pull down", "pull up", "" };
	static const char * const enable[] = {
			"disable", "enable" };
	static const char * const power_source[] = {
			"3V3", "1V8" };

	regmap_read(info->regmap_base, offset * 4, &val);

	drive	= (val & K230_PC_DS) >> K230_SHIFT_DS;
	bias	= (val & K230_PC_BIAS) >> K230_SHIFT_BIAS;
	input	= (val & K230_PC_IE) >> K230_SHIFT_IE;
	slew	= (val & K230_PC_SL) >> K230_SHIFT_SL;
	schmitt	= (val & K230_PC_ST) >> K230_SHIFT_ST;
	power	= (val & K230_PC_MSC) >> K230_SHIFT_MSC;

	seq_printf(s, "%s - strength %d - %s - %s - slewrate %s - schmitt %s - %s",
		   grp ? grp->name : "unknown",
		   drive,
		   biasing[bias],
		   input ? "input" : "output",
		   enable[slew],
		   enable[schmitt],
		   power_source[power]);
}

static int k230_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np_config,
			       struct pinctrl_map **map,
			       unsigned int *num_maps)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = info->pctl_dev->dev;
	const struct k230_pmx_func *func;
	const struct k230_pin_group *grp;
	struct pinctrl_map *new_map;
	int map_num, i, j, idx;
	unsigned int grp_id;

	func = k230_name_to_funtion(info, np_config->name);
	if (!func) {
		dev_err(dev, "function %s not found\n", np_config->name);
		return -EINVAL;
	}

	map_num = 0;
	for (i = 0; i < func->ngroups; ++i) {
		grp_id = func->group_idx[i];
		/* npins of config map plus a mux map */
		map_num += info->groups[grp_id].num_pins + 1;
	}

	new_map = kcalloc(map_num, sizeof(*new_map), GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;
	*map = new_map;
	*num_maps = map_num;

	idx = 0;
	for (i = 0; i < func->ngroups; ++i) {
		grp_id = func->group_idx[i];
		grp = &info->groups[grp_id];
		new_map[idx].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[idx].data.mux.group = grp->name;
		new_map[idx].data.mux.function = np_config->name;
		idx++;

		for (j = 0; j < grp->num_pins; ++j) {
			new_map[idx].type = PIN_MAP_TYPE_CONFIGS_PIN;
			new_map[idx].data.configs.group_or_pin =
				pin_get_name(pctldev, grp->pins[j]);
			new_map[idx].data.configs.configs =
				grp->data[j].configs;
			new_map[idx].data.configs.num_configs =
				grp->data[j].nconfigs;
			idx++;
		}
	}

	return 0;
}

static void k230_dt_free_map(struct pinctrl_dev *pctldev,
			     struct pinctrl_map *map, unsigned int num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops k230_pctrl_ops = {
	.get_groups_count	= k230_get_groups_count,
	.get_group_name		= k230_get_group_name,
	.get_group_pins		= k230_get_group_pins,
	.pin_dbg_show		= k230_pinctrl_pin_dbg_show,
	.dt_node_to_map		= k230_dt_node_to_map,
	.dt_free_map		= k230_dt_free_map,
};

static int k230_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *config)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int val, arg;

	regmap_read(info->regmap_base, pin * 4, &val);

	switch (param) {
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		arg = (val & K230_PC_ST) ? 1 : 0;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = (val & K230_PC_DS) >> K230_SHIFT_DS;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		arg = (val & K230_PC_BIAS) ? 0 : 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = (val & K230_PC_PD) ? 1 : 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = (val & K230_PC_PU) ? 1 : 0;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		arg = (val & K230_PC_OE) ? 1 : 0;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		arg = (val & K230_PC_IE) ? 1 : 0;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		arg = (val & K230_PC_MSC) ? 1 : 0;
		break;
	case PIN_CONFIG_SLEW_RATE:
		arg = (val & K230_PC_SL) ? 1 : 0;
		break;
	default:
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int k230_pinconf_set_param(struct pinctrl_dev *pctldev, unsigned int pin,
				  enum pin_config_param param, unsigned int arg)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned int val;

	regmap_read(info->regmap_base, pin * 4, &val);

	switch (param) {
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (arg)
			val |= K230_PC_ST;
		else
			val &= ~K230_PC_ST;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		val &= ~K230_PC_DS;
		val |= (arg << K230_SHIFT_DS) & K230_PC_DS;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		val &= ~K230_PC_BIAS;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!arg)
			return -EINVAL;
		val |= K230_PC_PD;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!arg)
			return -EINVAL;
		val |= K230_PC_PU;
		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
		if (!arg)
			return -EINVAL;
		val |= K230_PC_OE;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		if (!arg)
			return -EINVAL;
		val |= K230_PC_IE;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		if (arg)
			val |= K230_PC_MSC;
		else
			val &= ~K230_PC_MSC;
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (arg)
			val |= K230_PC_SL;
		else
			val &= ~K230_PC_SL;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(info->regmap_base, pin * 4, val);

	return 0;
}

static int k230_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int num_configs)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = info->pctl_dev->dev;
	enum pin_config_param param;
	unsigned int arg, i;
	int ret;

	if (pin >= K230_NPINS) {
		dev_err(dev, "pin number out of range\n");
		return -EINVAL;
	}

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);
		ret = k230_pinconf_set_param(pctldev, pin, param, arg);
		if (ret)
			return ret;
	}

	return 0;
}

static void k230_pconf_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int pin)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	unsigned int val;

	regmap_read(info->regmap_base, pin * 4, &val);

	seq_printf(s, " 0x%08x", val);
}

static const struct pinconf_ops k230_pinconf_ops = {
	.is_generic		= true,
	.pin_config_get		= k230_pinconf_get,
	.pin_config_set		= k230_pinconf_set,
	.pin_config_dbg_show	= k230_pconf_dbg_show,
};

static int k230_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->nfunctions;
}

static const char *k230_get_fname(struct pinctrl_dev *pctldev,
				  unsigned int selector)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->functions[selector].name;
}

static int k230_get_groups(struct pinctrl_dev *pctldev, unsigned int selector,
			   const char * const **groups, unsigned int *num_groups)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].ngroups;

	return 0;
}

static int k230_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			unsigned int group)
{
	struct k230_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct k230_pin_conf *data = info->groups[group].data;
	struct k230_pin_group *grp = &info->groups[group];
	const unsigned int *pins = grp->pins;
	struct regmap *regmap;
	unsigned int value, mask;
	int cnt, reg;

	regmap = info->regmap_base;

	for (cnt = 0; cnt < grp->num_pins; cnt++) {
		reg = pins[cnt] * 4;
		value = data[cnt].func << K230_SHIFT_SEL;
		mask = K230_PC_SEL;
		regmap_update_bits(regmap, reg, mask, value);
		k230_pins[pins[cnt]].drv_data = grp;
	}

	return 0;
}

static const struct pinmux_ops k230_pmxops = {
	.get_functions_count	= k230_get_functions_count,
	.get_function_name	= k230_get_fname,
	.get_function_groups	= k230_get_groups,
	.set_mux		= k230_set_mux,
	.strict			= true,
};

static int k230_pinctrl_parse_groups(struct device_node *np,
				     struct k230_pin_group *grp,
				     struct k230_pinctrl *info,
				     unsigned int index)
{
	struct device *dev = info->pctl_dev->dev;
	const __be32 *list;
	int size, i, ret;

	grp->name = np->name;

	list = of_get_property(np, "pinmux", &size);
	if (!list) {
		dev_err(dev, "failed to get pinmux property\n");
		return -EINVAL;
	}
	size /= sizeof(*list);

	grp->num_pins = size;
	grp->pins = devm_kcalloc(dev, grp->num_pins, sizeof(*grp->pins),
				 GFP_KERNEL);
	grp->data = devm_kcalloc(dev, grp->num_pins, sizeof(*grp->data),
				 GFP_KERNEL);
	if (!grp->pins || !grp->data)
		return -ENOMEM;

	for (i = 0; i < size; i++) {
		unsigned int mux_data = be32_to_cpu(*list++);

		grp->pins[i] = (mux_data >> 8);
		grp->data[i].func = (mux_data & 0xff);

		ret = pinconf_generic_parse_dt_config(np, NULL,
						      &grp->data[i].configs,
						      &grp->data[i].nconfigs);
		if (ret)
			return ret;
	}

	return 0;
}

static int k230_pinctrl_parse_functions(struct device_node *np,
					struct k230_pinctrl *info,
					unsigned int index)
{
	struct device *dev = info->pctl_dev->dev;
	struct k230_pmx_func *func;
	struct k230_pin_group *grp;
	static unsigned int idx, i;
	int ret;

	func = &info->functions[index];

	func->name = np->name;
	func->ngroups = of_get_child_count(np);
	if (func->ngroups <= 0)
		return 0;

	func->groups = devm_kcalloc(dev, func->ngroups,
				    sizeof(*func->groups), GFP_KERNEL);
	func->group_idx = devm_kcalloc(dev, func->ngroups,
				       sizeof(*func->group_idx), GFP_KERNEL);
	if (!func->groups || !func->group_idx)
		return -ENOMEM;

	i = 0;

	for_each_child_of_node_scoped(np, child) {
		func->groups[i] = child->name;
		func->group_idx[i] = idx;
		grp = &info->groups[idx];
		idx++;
		ret = k230_pinctrl_parse_groups(child, grp, info, i++);
		if (ret)
			return ret;
	}

	return 0;
}

static void k230_pinctrl_child_count(struct k230_pinctrl *info,
				     struct device_node *np)
{
	for_each_child_of_node_scoped(np, child) {
		info->nfunctions++;
		info->ngroups += of_get_child_count(child);
	}
}

static int k230_pinctrl_parse_dt(struct platform_device *pdev,
				 struct k230_pinctrl *info)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	unsigned int i;
	int ret;

	k230_pinctrl_child_count(info, np);

	info->functions = devm_kcalloc(dev, info->nfunctions,
				       sizeof(*info->functions), GFP_KERNEL);
	info->groups = devm_kcalloc(dev, info->ngroups,
				    sizeof(*info->groups), GFP_KERNEL);
	if (!info->functions || !info->groups)
		return -ENOMEM;

	i = 0;

	for_each_child_of_node_scoped(np, child) {
		ret = k230_pinctrl_parse_functions(child, info, i++);
		if (ret) {
			dev_err(dev, "failed to parse function\n");
			return ret;
		}
	}

	return 0;
}

static int k230_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct k230_pinctrl *info;
	struct pinctrl_desc *pctl;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pctl = &info->pctl;

	pctl->name	= "k230-pinctrl";
	pctl->owner	= THIS_MODULE;
	pctl->pins	= k230_pins;
	pctl->npins	= ARRAY_SIZE(k230_pins);
	pctl->pctlops	= &k230_pctrl_ops;
	pctl->pmxops	= &k230_pmxops;
	pctl->confops	= &k230_pinconf_ops;

	info->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(info->base))
		return PTR_ERR(info->base);

	info->regmap_base = devm_regmap_init_mmio(dev, info->base,
						  &k230_regmap_config);
	if (IS_ERR(info->regmap_base))
		return dev_err_probe(dev, PTR_ERR(info->regmap_base),
				     "failed to init regmap\n");

	ret = k230_pinctrl_parse_dt(pdev, info);
	if (ret)
		return ret;

	info->pctl_dev = devm_pinctrl_register(dev, pctl, info);
	if (IS_ERR(info->pctl_dev))
		return dev_err_probe(dev, PTR_ERR(info->pctl_dev),
				     "devm_pinctrl_register failed\n");

	return 0;
}

static const struct of_device_id k230_dt_ids[] = {
	{ .compatible = "canaan,k230-pinctrl", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, k230_dt_ids);

static struct platform_driver k230_pinctrl_driver = {
	.probe = k230_pinctrl_probe,
	.driver = {
		.name = "k230-pinctrl",
		.of_match_table = k230_dt_ids,
	},
};
module_platform_driver(k230_pinctrl_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ze Huang <18771902331@163.com>");
MODULE_DESCRIPTION("Canaan K230 pinctrl driver");
