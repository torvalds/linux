// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl driver for Ambarella SoCs
 *
 * Copyright (C) 2012-2026, Ambarella, Inc.
 */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string_helpers.h>
#include <linux/types.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"
#include "pinctrl-ambarella.h"

#define IOMUX_REG(bank, n)		(((bank) * 0xc) + ((n) * 4))
#define IOMUX_CTRL_SET			0xf0

#define PINID_TO_BANK(p)		((p) >> 5)
#define PINID_TO_OFFSET(p)		((p) & 0x1f)

struct amb_pinctrl {
	struct pinctrl_desc desc;
	void __iomem *iomux_base;
	struct regmap *ds_regmap;
	struct regmap *pull_regmap;
	const struct amb_pinctrl_data *data;
	struct device *dev;
	struct pinctrl_dev *pctl;
	spinlock_t lock;
};

static const int amb_ds1_ma[] = { 2, 4, 8, 12 };
static const int amb_ds2_ma[] = { 3, 4, 6, 8, 9, 12 };

static const struct pinctrl_ops amb_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_all,
	.dt_free_map		= pinconf_generic_dt_free_map,
};

static void amb_iomux_commit(struct amb_pinctrl *ipc)
{
	writel(0x1, ipc->iomux_base + IOMUX_CTRL_SET);
	writel(0x0, ipc->iomux_base + IOMUX_CTRL_SET);
}

static void amb_pinmux_set_altfunc(struct amb_pinctrl *ipc, u32 bank,
				   u32 offset, u32 altfunc)
{
	if (bank >= ipc->data->nr_banks)
		return;

	for (unsigned int i = 0; i < 3; i++) {
		unsigned long data;

		data = readl_relaxed(ipc->iomux_base + IOMUX_REG(bank, i));
		__assign_bit(offset, &data, altfunc & BIT(i));
		writel_relaxed(data, ipc->iomux_base + IOMUX_REG(bank, i));
	}
}

static int amb_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int selector, unsigned int group)
{
	struct amb_pinctrl *ipc = pinctrl_dev_get_drvdata(pctldev);
	struct group_desc *grp;
	const u8 *alts;
	unsigned long flags;

	(void)selector;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp || !grp->data)
		return -EINVAL;

	alts = grp->data;

	spin_lock_irqsave(&ipc->lock, flags);
	for (unsigned int i = 0; i < grp->grp.npins; i++) {
		unsigned int pin = grp->grp.pins[i];

		amb_pinmux_set_altfunc(ipc, PINID_TO_BANK(pin),
				       PINID_TO_OFFSET(pin), alts[i]);
	}
	amb_iomux_commit(ipc);
	spin_unlock_irqrestore(&ipc->lock, flags);

	return 0;
}

static int amb_pinmux_gpio_request_enable(struct pinctrl_dev *pctldev,
					  struct pinctrl_gpio_range *range,
					  unsigned int pin)
{
	struct amb_pinctrl *ipc = pinctrl_dev_get_drvdata(pctldev);
	unsigned long flags;

	if (!range || !range->gc)
		return -EINVAL;

	if (pin >= ipc->data->npins)
		return -EINVAL;

	spin_lock_irqsave(&ipc->lock, flags);
	amb_pinmux_set_altfunc(ipc, PINID_TO_BANK(pin), PINID_TO_OFFSET(pin), 0);
	amb_iomux_commit(ipc);
	spin_unlock_irqrestore(&ipc->lock, flags);

	return 0;
}

static const struct pinmux_ops amb_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= amb_pinmux_set_mux,
	.gpio_request_enable	= amb_pinmux_gpio_request_enable,
	.strict			= true,
};

static int amb_drive_strength_to_reg_ds1(u32 strength)
{
	for (unsigned int i = 0; i < ARRAY_SIZE(amb_ds1_ma); i++) {
		if (amb_ds1_ma[i] == strength)
			return i;
	}

	return -EINVAL;
}

static int amb_drive_strength_to_reg_ds2(u32 strength)
{
	/* Hardware steps are 3, 4, 6, 8, 9, 12 mA; 5 and 7 are accepted aliases */
	if (strength == 5)
		strength = 4;
	else if (strength == 7)
		strength = 8;

	for (unsigned int i = 0; i < ARRAY_SIZE(amb_ds2_ma); i++) {
		if (amb_ds2_ma[i] == strength)
			return i;
	}

	return -EINVAL;
}

static int amb_drive_strength_to_reg(const struct amb_pinctrl *ipc, u32 strength)
{
	if (ipc->data->have_ds2)
		return amb_drive_strength_to_reg_ds2(strength);

	return amb_drive_strength_to_reg_ds1(strength);
}

static int amb_reg_to_drive_strength(const struct amb_pinctrl *ipc, u32 ds)
{
	if (ipc->data->have_ds2) {
		if (ds >= ARRAY_SIZE(amb_ds2_ma))
			return -EINVAL;
		return amb_ds2_ma[ds];
	}

	if (ds >= ARRAY_SIZE(amb_ds1_ma))
		return -EINVAL;

	return amb_ds1_ma[ds];
}

static int amb_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			   unsigned long *configs, unsigned int num_configs)
{
	struct amb_pinctrl *ipc = pinctrl_dev_get_drvdata(pctldev);
	u32 bank = PINID_TO_BANK(pin);
	u32 offset = PINID_TO_OFFSET(pin);
	unsigned int mask = BIT(offset);
	int ret;

	if (bank >= ipc->data->nr_banks)
		return -EINVAL;

	for (unsigned int i = 0; i < num_configs; i++) {
		enum pin_config_param param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);
		int ds;

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			ret = regmap_assign_bits(ipc->pull_regmap, ipc->data->pull_en[bank],
						 mask, false);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_PULL_UP:
			ret = regmap_assign_bits(ipc->pull_regmap, ipc->data->pull_dir[bank],
						 mask, param == PIN_CONFIG_BIAS_PULL_UP);
			if (ret)
				return ret;
			ret = regmap_assign_bits(ipc->pull_regmap, ipc->data->pull_en[bank],
						 mask, true);
			if (ret)
				return ret;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			ds = amb_drive_strength_to_reg(ipc, arg);
			if (ds < 0)
				return ds;
			if (ipc->data->have_ds2) {
				ret = regmap_assign_bits(ipc->ds_regmap, ipc->data->ds0[bank],
							 mask, ds & BIT(0));
				if (ret)
					return ret;
				ret = regmap_assign_bits(ipc->ds_regmap, ipc->data->ds1[bank],
							 mask, ds & BIT(1));
				if (ret)
					return ret;
				ret = regmap_assign_bits(ipc->ds_regmap, ipc->data->ds2[bank],
							 mask, ds & BIT(2));
				if (ret)
					return ret;
			} else {
				ret = regmap_assign_bits(ipc->ds_regmap, ipc->data->ds0[bank],
							 mask, ds & BIT(1));
				if (ret)
					return ret;
				ret = regmap_assign_bits(ipc->ds_regmap, ipc->data->ds1[bank],
							 mask, ds & BIT(0));
				if (ret)
					return ret;
			}
			break;
		default:
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int amb_pinconf_group_set(struct pinctrl_dev *pctldev,
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

	for (unsigned int i = 0; i < npins; i++) {
		ret = amb_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static int amb_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned int pin, unsigned long *config)
{
	struct amb_pinctrl *ipc = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 bank = PINID_TO_BANK(pin);
	u32 offset = PINID_TO_OFFSET(pin);
	u32 pull_en, pull_dir, ds0, ds1, ds2, ds;
	bool en, dir, b0, b1, b2;
	int ret, strength;

	if (bank >= ipc->data->nr_banks)
		return -EINVAL;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		ret = regmap_read(ipc->pull_regmap, ipc->data->pull_en[bank],
				  &pull_en);
		if (ret)
			return ret;

		ret = regmap_read(ipc->pull_regmap, ipc->data->pull_dir[bank],
				  &pull_dir);
		if (ret)
			return ret;

		en = !!(pull_en & BIT(offset));
		dir = !!(pull_dir & BIT(offset));

		if (param == PIN_CONFIG_BIAS_DISABLE) {
			if (en)
				return -EINVAL;
			*config = pinconf_to_config_packed(param, 0);
			return 0;
		}

		if (!en)
			return -EINVAL;
		if (param == PIN_CONFIG_BIAS_PULL_UP && !dir)
			return -EINVAL;
		if (param == PIN_CONFIG_BIAS_PULL_DOWN && dir)
			return -EINVAL;

		*config = pinconf_to_config_packed(param, 1);
		return 0;

	case PIN_CONFIG_DRIVE_STRENGTH:
		ret = regmap_read(ipc->ds_regmap, ipc->data->ds0[bank], &ds0);
		if (ret)
			return ret;

		ret = regmap_read(ipc->ds_regmap, ipc->data->ds1[bank], &ds1);
		if (ret)
			return ret;

		b0 = !!(ds0 & BIT(offset));
		b1 = !!(ds1 & BIT(offset));
		if (ipc->data->have_ds2) {
			ret = regmap_read(ipc->ds_regmap, ipc->data->ds2[bank],
					  &ds2);
			if (ret)
				return ret;

			b2 = !!(ds2 & BIT(offset));
			ds = (b2 << 2) | (b1 << 1) | b0;
		} else {
			ds = (b0 << 1) | b1;
		}

		strength = amb_reg_to_drive_strength(ipc, ds);
		if (strength < 0)
			return strength;

		*config = pinconf_to_config_packed(param, strength);
		return 0;

	default:
		return -ENOTSUPP;
	}
}

static const struct pinconf_ops amb_pinconf_ops = {
	.is_generic		= true,
	.pin_config_get		= amb_pinconf_get,
	.pin_config_set		= amb_pinconf_set,
	.pin_config_group_set	= amb_pinconf_group_set,
};

static int amb_pinctrl_add_groups(struct amb_pinctrl *ipc)
{
	const struct amb_pinctrl_data *data = ipc->data;

	for (unsigned int i = 0; i < data->ngroups; i++) {
		const struct amb_pinmux_group *g = &data->groups[i];
		int ret;

		if (!g->grp || !g->grp->name || !g->grp->pins ||
		    !g->grp->npins || !g->alts)
			return -EINVAL;

		for (unsigned int j = 0; j < g->grp->npins; j++) {
			if (g->grp->pins[j] >= data->npins)
				return -EINVAL;
		}

		ret = pinctrl_generic_add_group(ipc->pctl, g->grp->name,
						g->grp->pins, g->grp->npins,
						(void *)g->alts);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int amb_pinctrl_add_functions(struct amb_pinctrl *ipc)
{
	const struct amb_pinctrl_data *data = ipc->data;

	for (unsigned int i = 0; i < data->nfunctions; i++) {
		int ret;

		ret = pinmux_generic_add_pinfunction(ipc->pctl,
						     &data->functions[i], NULL);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int amb_pinctrl_register(struct amb_pinctrl *ipc)
{
	struct pinctrl_pin_desc *pindesc;
	char **names;
	int ret;

	pindesc = devm_kcalloc(ipc->dev, ipc->data->npins, sizeof(*pindesc),
			       GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	names = devm_kasprintf_strarray(ipc->dev, "io", ipc->data->npins);
	if (IS_ERR(names))
		return PTR_ERR(names);

	for (unsigned int pin = 0; pin < ipc->data->npins; pin++) {
		pindesc[pin].number = pin;
		pindesc[pin].name = names[pin];
	}

	ipc->desc.name = dev_name(ipc->dev);
	ipc->desc.pins = pindesc;
	ipc->desc.npins = ipc->data->npins;
	ipc->desc.pctlops = &amb_pctrl_ops;
	ipc->desc.pmxops = &amb_pinmux_ops;
	ipc->desc.confops = &amb_pinconf_ops;
	ipc->desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(ipc->dev, &ipc->desc, ipc, &ipc->pctl);
	if (ret)
		return ret;

	ret = amb_pinctrl_add_groups(ipc);
	if (ret)
		return ret;

	ret = amb_pinctrl_add_functions(ipc);
	if (ret)
		return ret;

	return pinctrl_enable(ipc->pctl);
}

static int amb_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct amb_pinctrl *ipc;
	int ret;

	ipc = devm_kzalloc(dev, sizeof(*ipc), GFP_KERNEL);
	if (!ipc)
		return -ENOMEM;

	ipc->dev = dev;
	ipc->data = device_get_match_data(dev);
	if (!ipc->data)
		return dev_err_probe(dev, -ENODATA, "missing SoC data\n");

	if (!ipc->data->nr_banks || ipc->data->nr_banks > AMBA_MAX_BANKS ||
	    !ipc->data->npins ||
	    !ipc->data->groups || !ipc->data->ngroups ||
	    !ipc->data->functions || !ipc->data->nfunctions)
		return dev_err_probe(dev, -EINVAL, "invalid SoC data\n");

	ipc->iomux_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ipc->iomux_base))
		return PTR_ERR(ipc->iomux_base);

	ipc->ds_regmap = syscon_regmap_lookup_by_phandle(dev_of_node(dev),
					"ambarella,drive-strength-syscon");
	if (IS_ERR(ipc->ds_regmap))
		return dev_err_probe(dev, PTR_ERR(ipc->ds_regmap),
				     "missing drive-strength syscon\n");

	ipc->pull_regmap = syscon_regmap_lookup_by_phandle(dev_of_node(dev),
					"ambarella,pull-syscon");
	if (IS_ERR(ipc->pull_regmap))
		return dev_err_probe(dev, PTR_ERR(ipc->pull_regmap),
				     "missing pull syscon\n");

	spin_lock_init(&ipc->lock);

	ret = amb_pinctrl_register(ipc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register pinctrl\n");

	platform_set_drvdata(pdev, ipc);

	return 0;
}

static const struct of_device_id amb_pinctrl_dt_match[] = {
	{
		.compatible = "ambarella,cv75-pinctrl",
		.data = &ambarella_cv75_pinctrl_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, amb_pinctrl_dt_match);

static struct platform_driver amb_pinctrl_driver = {
	.probe = amb_pinctrl_probe,
	.driver = {
		.name = "ambarella-pinctrl",
		.of_match_table = amb_pinctrl_dt_match,
	},
};

static int __init amb_pinctrl_drv_register(void)
{
	return platform_driver_register(&amb_pinctrl_driver);
}
arch_initcall(amb_pinctrl_drv_register);

MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_DESCRIPTION("Ambarella SoC pinctrl driver");
MODULE_LICENSE("GPL");
