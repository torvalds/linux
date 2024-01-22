// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek Pinctrl Moore Driver, which implement the generic dt-binding
 * pinctrl-bindings.txt for MediaTek SoC.
 *
 * Copyright (C) 2017-2018 MediaTek Inc.
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <dt-bindings/pinctrl/mt65xx.h>
#include <linux/gpio/driver.h>

#include <linux/pinctrl/consumer.h>

#include "pinctrl-moore.h"

#define PINCTRL_PINCTRL_DEV		KBUILD_MODNAME

/* Custom pinconf parameters */
#define MTK_PIN_CONFIG_TDSEL	(PIN_CONFIG_END + 1)
#define MTK_PIN_CONFIG_RDSEL	(PIN_CONFIG_END + 2)
#define MTK_PIN_CONFIG_PU_ADV	(PIN_CONFIG_END + 3)
#define MTK_PIN_CONFIG_PD_ADV	(PIN_CONFIG_END + 4)

static const struct pinconf_generic_params mtk_custom_bindings[] = {
	{"mediatek,tdsel",	MTK_PIN_CONFIG_TDSEL,		0},
	{"mediatek,rdsel",	MTK_PIN_CONFIG_RDSEL,		0},
	{"mediatek,pull-up-adv", MTK_PIN_CONFIG_PU_ADV,		1},
	{"mediatek,pull-down-adv", MTK_PIN_CONFIG_PD_ADV,	1},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item mtk_conf_items[] = {
	PCONFDUMP(MTK_PIN_CONFIG_TDSEL, "tdsel", NULL, true),
	PCONFDUMP(MTK_PIN_CONFIG_RDSEL, "rdsel", NULL, true),
	PCONFDUMP(MTK_PIN_CONFIG_PU_ADV, "pu-adv", NULL, true),
	PCONFDUMP(MTK_PIN_CONFIG_PD_ADV, "pd-adv", NULL, true),
};
#endif

static int mtk_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int selector, unsigned int group)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	struct function_desc *func;
	struct group_desc *grp;
	int i, err;

	func = pinmux_generic_get_function(pctldev, selector);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pctldev, group);
	if (!grp)
		return -EINVAL;

	dev_dbg(pctldev->dev, "enable function %s group %s\n",
		func->name, grp->grp.name);

	for (i = 0; i < grp->grp.npins; i++) {
		const struct mtk_pin_desc *desc;
		int *pin_modes = grp->data;
		int pin = grp->grp.pins[i];

		desc = (const struct mtk_pin_desc *)&hw->soc->pins[pin];
		if (!desc->name)
			return -ENOTSUPP;

		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_MODE,
				       pin_modes[i]);

		if (err)
			return err;
	}

	return 0;
}

static int mtk_pinmux_gpio_request_enable(struct pinctrl_dev *pctldev,
					  struct pinctrl_gpio_range *range,
					  unsigned int pin)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[pin];
	if (!desc->name)
		return -ENOTSUPP;

	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_MODE,
				hw->soc->gpio_m);
}

static int mtk_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
					 struct pinctrl_gpio_range *range,
					 unsigned int pin, bool input)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[pin];
	if (!desc->name)
		return -ENOTSUPP;

	/* hardware would take 0 as input direction */
	return mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR, !input);
}

static int mtk_pinconf_get(struct pinctrl_dev *pctldev,
			   unsigned int pin, unsigned long *config)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	u32 param = pinconf_to_config_param(*config);
	int val, val2, err, pullup, reg, ret = 1;
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[pin];
	if (!desc->name)
		return -ENOTSUPP;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (hw->soc->bias_get_combo) {
			err = hw->soc->bias_get_combo(hw, desc, &pullup, &ret);
			if (err)
				return err;
			if (ret != MTK_PUPD_SET_R1R0_00 && ret != MTK_DISABLE)
				return -EINVAL;
		} else if (hw->soc->bias_disable_get) {
			err = hw->soc->bias_disable_get(hw, desc, &ret);
			if (err)
				return err;
		} else {
			return -ENOTSUPP;
		}
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (hw->soc->bias_get_combo) {
			err = hw->soc->bias_get_combo(hw, desc, &pullup, &ret);
			if (err)
				return err;
			if (ret == MTK_PUPD_SET_R1R0_00 || ret == MTK_DISABLE)
				return -EINVAL;
			if (!pullup)
				return -EINVAL;
		} else if (hw->soc->bias_get) {
			err = hw->soc->bias_get(hw, desc, 1, &ret);
			if (err)
				return err;
		} else {
			return -ENOTSUPP;
		}
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (hw->soc->bias_get_combo) {
			err = hw->soc->bias_get_combo(hw, desc, &pullup, &ret);
			if (err)
				return err;
			if (ret == MTK_PUPD_SET_R1R0_00 || ret == MTK_DISABLE)
				return -EINVAL;
			if (pullup)
				return -EINVAL;
		} else if (hw->soc->bias_get) {
			err = hw->soc->bias_get(hw, desc, 0, &ret);
			if (err)
				return err;
		} else {
			return -ENOTSUPP;
		}
		break;
	case PIN_CONFIG_SLEW_RATE:
		err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_SR, &val);
		if (err)
			return err;

		if (!val)
			return -EINVAL;

		break;
	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_OUTPUT_ENABLE:
		err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DIR, &val);
		if (err)
			return err;

		/* HW takes input mode as zero; output mode as non-zero */
		if ((val && param == PIN_CONFIG_INPUT_ENABLE) ||
		    (!val && param == PIN_CONFIG_OUTPUT_ENABLE))
			return -EINVAL;

		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DIR, &val);
		if (err)
			return err;

		err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_SMT, &val2);
		if (err)
			return err;

		if (val || !val2)
			return -EINVAL;

		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		if (hw->soc->drive_get) {
			err = hw->soc->drive_get(hw, desc, &ret);
			if (err)
				return err;
		} else {
			err = -ENOTSUPP;
		}
		break;
	case MTK_PIN_CONFIG_TDSEL:
	case MTK_PIN_CONFIG_RDSEL:
		reg = (param == MTK_PIN_CONFIG_TDSEL) ?
		       PINCTRL_PIN_REG_TDSEL : PINCTRL_PIN_REG_RDSEL;

		err = mtk_hw_get_value(hw, desc, reg, &val);
		if (err)
			return err;

		ret = val;

		break;
	case MTK_PIN_CONFIG_PU_ADV:
	case MTK_PIN_CONFIG_PD_ADV:
		if (hw->soc->adv_pull_get) {
			bool pullup;

			pullup = param == MTK_PIN_CONFIG_PU_ADV;
			err = hw->soc->adv_pull_get(hw, desc, pullup, &ret);
			if (err)
				return err;
		} else {
			return -ENOTSUPP;
		}
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, ret);

	return 0;
}

static int mtk_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			   unsigned long *configs, unsigned int num_configs)
{
	struct mtk_pinctrl *hw = pinctrl_dev_get_drvdata(pctldev);
	const struct mtk_pin_desc *desc;
	u32 reg, param, arg;
	int cfg, err = 0;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[pin];
	if (!desc->name)
		return -ENOTSUPP;

	for (cfg = 0; cfg < num_configs; cfg++) {
		param = pinconf_to_config_param(configs[cfg]);
		arg = pinconf_to_config_argument(configs[cfg]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			if (hw->soc->bias_set_combo) {
				err = hw->soc->bias_set_combo(hw, desc, 0, MTK_DISABLE);
				if (err)
					return err;
			} else if (hw->soc->bias_disable_set) {
				err = hw->soc->bias_disable_set(hw, desc);
				if (err)
					return err;
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (hw->soc->bias_set_combo) {
				err = hw->soc->bias_set_combo(hw, desc, 1, arg);
				if (err)
					return err;
			} else if (hw->soc->bias_set) {
				err = hw->soc->bias_set(hw, desc, 1);
				if (err)
					return err;
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (hw->soc->bias_set_combo) {
				err = hw->soc->bias_set_combo(hw, desc, 0, arg);
				if (err)
					return err;
			} else if (hw->soc->bias_set) {
				err = hw->soc->bias_set(hw, desc, 0);
				if (err)
					return err;
			} else {
				return -ENOTSUPP;
			}
			break;
		case PIN_CONFIG_OUTPUT_ENABLE:
			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_SMT,
					       MTK_DISABLE);
			if (err)
				goto err;

			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR,
					       MTK_OUTPUT);
			if (err)
				goto err;
			break;
		case PIN_CONFIG_INPUT_ENABLE:

			if (hw->soc->ies_present) {
				mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_IES,
						 MTK_ENABLE);
			}

			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR,
					       MTK_INPUT);
			if (err)
				goto err;
			break;
		case PIN_CONFIG_SLEW_RATE:
			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_SR,
					       arg);
			if (err)
				goto err;

			break;
		case PIN_CONFIG_OUTPUT:
			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR,
					       MTK_OUTPUT);
			if (err)
				goto err;

			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DO,
					       arg);
			if (err)
				goto err;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			/* arg = 1: Input mode & SMT enable ;
			 * arg = 0: Output mode & SMT disable
			 */
			arg = arg ? 2 : 1;
			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR,
					       arg & 1);
			if (err)
				goto err;

			err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_SMT,
					       !!(arg & 2));
			if (err)
				goto err;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			if (hw->soc->drive_set) {
				err = hw->soc->drive_set(hw, desc, arg);
				if (err)
					return err;
			} else {
				err = -ENOTSUPP;
			}
			break;
		case MTK_PIN_CONFIG_TDSEL:
		case MTK_PIN_CONFIG_RDSEL:
			reg = (param == MTK_PIN_CONFIG_TDSEL) ?
			       PINCTRL_PIN_REG_TDSEL : PINCTRL_PIN_REG_RDSEL;

			err = mtk_hw_set_value(hw, desc, reg, arg);
			if (err)
				goto err;
			break;
		case MTK_PIN_CONFIG_PU_ADV:
		case MTK_PIN_CONFIG_PD_ADV:
			if (hw->soc->adv_pull_set) {
				bool pullup;

				pullup = param == MTK_PIN_CONFIG_PU_ADV;
				err = hw->soc->adv_pull_set(hw, desc, pullup,
							    arg);
				if (err)
					return err;
			} else {
				return -ENOTSUPP;
			}
			break;
		default:
			err = -ENOTSUPP;
		}
	}
err:
	return err;
}

static int mtk_pinconf_group_get(struct pinctrl_dev *pctldev,
				 unsigned int group, unsigned long *config)
{
	const unsigned int *pins;
	unsigned int i, npins, old = 0;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		if (mtk_pinconf_get(pctldev, pins[i], config))
			return -ENOTSUPP;

		/* configs do not match between two pins */
		if (i && old != *config)
			return -ENOTSUPP;

		old = *config;
	}

	return 0;
}

static int mtk_pinconf_group_set(struct pinctrl_dev *pctldev,
				 unsigned int group, unsigned long *configs,
				 unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int i, npins;
	int ret;

	ret = pinctrl_generic_get_group_pins(pctldev, group, &pins, &npins);
	if (ret)
		return ret;

	for (i = 0; i < npins; i++) {
		ret = mtk_pinconf_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinctrl_ops mtk_pctlops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static const struct pinmux_ops mtk_pmxops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = mtk_pinmux_set_mux,
	.gpio_request_enable = mtk_pinmux_gpio_request_enable,
	.gpio_set_direction = mtk_pinmux_gpio_set_direction,
	.strict = true,
};

static const struct pinconf_ops mtk_confops = {
	.is_generic = true,
	.pin_config_get = mtk_pinconf_get,
	.pin_config_set = mtk_pinconf_set,
	.pin_config_group_get = mtk_pinconf_group_get,
	.pin_config_group_set = mtk_pinconf_group_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static struct pinctrl_desc mtk_desc = {
	.name = PINCTRL_PINCTRL_DEV,
	.pctlops = &mtk_pctlops,
	.pmxops = &mtk_pmxops,
	.confops = &mtk_confops,
	.owner = THIS_MODULE,
};

static int mtk_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	const struct mtk_pin_desc *desc;
	int value, err;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];
	if (!desc->name)
		return -ENOTSUPP;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DI, &value);
	if (err)
		return err;

	return !!value;
}

static void mtk_gpio_set(struct gpio_chip *chip, unsigned int gpio, int value)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio];
	if (!desc->name) {
		dev_err(hw->dev, "Failed to set gpio %d\n", gpio);
		return;
	}

	mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DO, !!value);
}

static int mtk_gpio_direction_output(struct gpio_chip *chip, unsigned int gpio,
				     int value)
{
	mtk_gpio_set(chip, gpio, value);

	return pinctrl_gpio_direction_output(chip, gpio);
}

static int mtk_gpio_to_irq(struct gpio_chip *chip, unsigned int offset)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	const struct mtk_pin_desc *desc;

	if (!hw->eint)
		return -ENOTSUPP;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[offset];

	if (desc->eint.eint_n == (u16)EINT_NA)
		return -ENOTSUPP;

	return mtk_eint_find_irq(hw->eint, desc->eint.eint_n);
}

static int mtk_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
			       unsigned long config)
{
	struct mtk_pinctrl *hw = gpiochip_get_data(chip);
	const struct mtk_pin_desc *desc;
	u32 debounce;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[offset];
	if (!desc->name)
		return -ENOTSUPP;

	if (!hw->eint ||
	    pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE ||
	    desc->eint.eint_n == (u16)EINT_NA)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);

	return mtk_eint_set_debounce(hw->eint, desc->eint.eint_n, debounce);
}

static int mtk_build_gpiochip(struct mtk_pinctrl *hw)
{
	struct gpio_chip *chip = &hw->chip;
	int ret;

	chip->label		= PINCTRL_PINCTRL_DEV;
	chip->parent		= hw->dev;
	chip->request		= gpiochip_generic_request;
	chip->free		= gpiochip_generic_free;
	chip->direction_input	= pinctrl_gpio_direction_input;
	chip->direction_output	= mtk_gpio_direction_output;
	chip->get		= mtk_gpio_get;
	chip->set		= mtk_gpio_set;
	chip->to_irq		= mtk_gpio_to_irq;
	chip->set_config	= mtk_gpio_set_config;
	chip->base		= -1;
	chip->ngpio		= hw->soc->npins;

	ret = gpiochip_add_data(chip, hw);
	if (ret < 0)
		return ret;

	/* Just for backward compatible for these old pinctrl nodes without
	 * "gpio-ranges" property. Otherwise, called directly from a
	 * DeviceTree-supported pinctrl driver is DEPRECATED.
	 * Please see Section 2.1 of
	 * Documentation/devicetree/bindings/gpio/gpio.txt on how to
	 * bind pinctrl and gpio drivers via the "gpio-ranges" property.
	 */
	if (!of_property_present(hw->dev->of_node, "gpio-ranges")) {
		ret = gpiochip_add_pin_range(chip, dev_name(hw->dev), 0, 0,
					     chip->ngpio);
		if (ret < 0) {
			gpiochip_remove(chip);
			return ret;
		}
	}

	return 0;
}

static int mtk_build_groups(struct mtk_pinctrl *hw)
{
	int err, i;

	for (i = 0; i < hw->soc->ngrps; i++) {
		const struct group_desc *group = hw->soc->grps + i;
		const struct pingroup *grp = &group->grp;

		err = pinctrl_generic_add_group(hw->pctrl, grp->name, grp->pins, grp->npins,
						group->data);
		if (err < 0) {
			dev_err(hw->dev, "Failed to register group %s\n", grp->name);
			return err;
		}
	}

	return 0;
}

static int mtk_build_functions(struct mtk_pinctrl *hw)
{
	int i, err;

	for (i = 0; i < hw->soc->nfuncs ; i++) {
		const struct function_desc *func = hw->soc->funcs + i;

		err = pinmux_generic_add_function(hw->pctrl, func->name,
						  func->group_names,
						  func->num_group_names,
						  func->data);
		if (err < 0) {
			dev_err(hw->dev, "Failed to register function %s\n",
				func->name);
			return err;
		}
	}

	return 0;
}

int mtk_moore_pinctrl_probe(struct platform_device *pdev,
			    const struct mtk_pin_soc *soc)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_pin_desc *pins;
	struct mtk_pinctrl *hw;
	int err, i;

	hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->soc = soc;
	hw->dev = &pdev->dev;

	if (!hw->soc->nbase_names)
		return dev_err_probe(dev, -EINVAL,
			"SoC should be assigned at least one register base\n");

	hw->base = devm_kmalloc_array(&pdev->dev, hw->soc->nbase_names,
				      sizeof(*hw->base), GFP_KERNEL);
	if (!hw->base)
		return -ENOMEM;

	for (i = 0; i < hw->soc->nbase_names; i++) {
		hw->base[i] = devm_platform_ioremap_resource_byname(pdev,
						hw->soc->base_names[i]);
		if (IS_ERR(hw->base[i]))
			return PTR_ERR(hw->base[i]);
	}

	hw->nbase = hw->soc->nbase_names;

	spin_lock_init(&hw->lock);

	/* Copy from internal struct mtk_pin_desc to register to the core */
	pins = devm_kmalloc_array(&pdev->dev, hw->soc->npins, sizeof(*pins),
				  GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	for (i = 0; i < hw->soc->npins; i++) {
		pins[i].number = hw->soc->pins[i].number;
		pins[i].name = hw->soc->pins[i].name;
	}

	/* Setup pins descriptions per SoC types */
	mtk_desc.pins = (const struct pinctrl_pin_desc *)pins;
	mtk_desc.npins = hw->soc->npins;
	mtk_desc.num_custom_params = ARRAY_SIZE(mtk_custom_bindings);
	mtk_desc.custom_params = mtk_custom_bindings;
#ifdef CONFIG_DEBUG_FS
	mtk_desc.custom_conf_items = mtk_conf_items;
#endif

	err = devm_pinctrl_register_and_init(&pdev->dev, &mtk_desc, hw,
					     &hw->pctrl);
	if (err)
		return err;

	/* Setup groups descriptions per SoC types */
	err = mtk_build_groups(hw);
	if (err)
		return dev_err_probe(dev, err, "Failed to build groups\n");

	/* Setup functions descriptions per SoC types */
	err = mtk_build_functions(hw);
	if (err)
		return dev_err_probe(dev, err, "Failed to build functions\n");

	/* For able to make pinctrl_claim_hogs, we must not enable pinctrl
	 * until all groups and functions are being added one.
	 */
	err = pinctrl_enable(hw->pctrl);
	if (err)
		return err;

	err = mtk_build_eint(hw, pdev);
	if (err)
		dev_warn(&pdev->dev,
			 "Failed to add EINT, but pinctrl still can work\n");

	/* Build gpiochip should be after pinctrl_enable is done */
	err = mtk_build_gpiochip(hw);
	if (err)
		return dev_err_probe(dev, err, "Failed to add gpio_chip\n");

	platform_set_drvdata(pdev, hw);

	return 0;
}
