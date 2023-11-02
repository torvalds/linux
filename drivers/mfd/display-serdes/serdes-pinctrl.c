// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * serdes-pinctrl.c  -- serdes pin control driver.
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "core.h"

static const struct mfd_cell serdes_gpio_bu18tl82_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "rohm,bu18tl82-gpio",
	},
};

static const struct mfd_cell serdes_gpio_bu18rl82_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "rohm,bu18rl82-gpio",
	},
};

static const struct mfd_cell serdes_gpio_max96745_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "maxim,max96745-gpio",
	},
};

static const struct mfd_cell serdes_gpio_max96755_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "maxim,max96755-gpio",
	},
};

static const struct mfd_cell serdes_gpio_max96789_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "maxim,max96789-gpio",
	},
};

static const struct mfd_cell serdes_gpio_max96752_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "maxim,max96752-gpio",
	},
};

static const struct mfd_cell serdes_gpio_max96772_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "maxim,max96772-gpio",
	},
};

static const struct mfd_cell serdes_gpio_rkx111_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "rockchip,rkx111-gpio",
	},
};

static const struct mfd_cell serdes_gpio_rkx121_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "rockchip,rkx121-gpio",
	},
};

static const struct mfd_cell serdes_gpio_nca9539_devs[] = {
	{
		.name = "serdes-gpio",
		.of_compatible = "novo,nca9539-gpio",
	},
};

static int serdes_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int function, unsigned int group)
{
	struct serdes_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	struct serdes *serdes = pinctrl->parent;
	int ret = 0;

	if (serdes->chip_data->pinctrl_ops->set_mux)
		ret = serdes->chip_data->pinctrl_ops->set_mux(serdes, function, group);

	SERDES_DBG_MFD("%s: %s function=%d,group=%d\n", __func__,
		 serdes->chip_data->name, function, group);
	return ret;
}

static int serdes_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct serdes_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	//enum pin_config_param param = pinconf_to_config_param(*config);
	struct serdes *serdes = pinctrl->parent;
	int ret = 0;

	if (serdes->chip_data->pinctrl_ops->pin_config_get)
		ret = serdes->chip_data->pinctrl_ops->pin_config_get(serdes,
								     pin - pinctrl->pin_base,
								     config);

	SERDES_DBG_MFD("%s: %s pin=%d,config=%d\n", __func__,
		       serdes->chip_data->name,
		       pin - pinctrl->pin_base, pinconf_to_config_param(*config));
	return ret;
}

static int serdes_pinconf_set(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *configs,
			      unsigned int num_configs)
{
	struct serdes_pinctrl *pinctrl = pinctrl_dev_get_drvdata(pctldev);
	//enum pin_config_param param = pinconf_to_config_param(*configs);
	struct serdes *serdes = pinctrl->parent;
	int ret = 0;

	if (serdes->chip_data->pinctrl_ops->pin_config_set)
		ret = serdes->chip_data->pinctrl_ops->pin_config_set(serdes,
								     pin - pinctrl->pin_base,
								     configs, num_configs);

	SERDES_DBG_MFD("%s: %s pin=%d,config=%d\n", __func__, serdes->chip_data->name,
		       pin - pinctrl->pin_base, pinconf_to_config_param(*configs));
	return ret;
}

static const struct pinconf_ops serdes_pinconf_ops = {
	.pin_config_get = serdes_pinconf_get,
	.pin_config_set = serdes_pinconf_set,
};

static const struct pinmux_ops serdes_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = serdes_pinmux_set_mux,
};

static const struct pinctrl_ops serdes_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int serdes_pinctrl_gpio_init(struct serdes *serdes)
{
	struct serdes_chip_data *chip_data = serdes->chip_data;
	struct serdes_pinctrl *pinctrl = serdes->pinctrl;
	const struct mfd_cell *serdes_devs = NULL;
	int ret = 0;
	int mfd_num = 0;

	switch (chip_data->serdes_id) {
	case ROHM_ID_BU18TL82:
		serdes_devs = serdes_gpio_bu18tl82_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_bu18tl82_devs);
		break;
	case ROHM_ID_BU18RL82:
		serdes_devs = serdes_gpio_bu18rl82_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_bu18rl82_devs);
		break;
	case MAXIM_ID_MAX96745:
		serdes_devs = serdes_gpio_max96745_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_max96745_devs);
		break;
	case MAXIM_ID_MAX96752:
		serdes_devs = serdes_gpio_max96752_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_max96752_devs);
		break;
	case MAXIM_ID_MAX96755:
		serdes_devs = serdes_gpio_max96755_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_max96755_devs);
		break;
	case MAXIM_ID_MAX96772:
		serdes_devs = serdes_gpio_max96772_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_max96772_devs);
		break;
	case MAXIM_ID_MAX96789:
		serdes_devs = serdes_gpio_max96789_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_max96789_devs);
		break;
	case ROCKCHIP_ID_RKX111:
		serdes_devs = serdes_gpio_rkx111_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_rkx111_devs);
		break;
	case ROCKCHIP_ID_RKX121:
		serdes_devs = serdes_gpio_rkx121_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_rkx121_devs);
		break;
	case NOVO_ID_NCA9539:
		serdes_devs = serdes_gpio_nca9539_devs;
		mfd_num = ARRAY_SIZE(serdes_gpio_nca9539_devs);
		break;
	default:
		dev_info(serdes->dev, "%s: unknown device\n", __func__);
		break;

	}

	ret = devm_mfd_add_devices(pinctrl->dev, PLATFORM_DEVID_AUTO, serdes_devs,
				   mfd_num, NULL, 0, NULL);
	if (ret != 0)
		dev_err(pinctrl->dev, "Failed to add serdes children\n");

	return ret;
}

static int serdes_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct serdes *serdes = dev_get_drvdata(pdev->dev.parent);
	const struct serdes_chip_data *chip_data = serdes->chip_data;
	struct serdes_pinctrl *serdes_pinctrl;
	struct pinctrl_desc *pinctrl_desc;
	const struct serdes_chip_pinctrl_info *pinctrl_info;
	struct device_node *child;
	const char *list_name = "gpio-ranges";
	struct of_phandle_args of_args;
	int pin_base = 0;
	int i, j, ret;

	if (!serdes->dev)
		return -1;

	pinctrl_info = chip_data->pinctrl_info;
	serdes_pinctrl = devm_kzalloc(dev, sizeof(*serdes_pinctrl), GFP_KERNEL);
	if (!serdes_pinctrl)
		return -ENOMEM;

	serdes_pinctrl->dev = dev;
	serdes_pinctrl->parent = serdes;
	serdes->pinctrl = serdes_pinctrl;
	platform_set_drvdata(pdev, serdes_pinctrl);

	serdes_pinctrl->regmap = dev_get_regmap(dev->parent, NULL);
	if (!serdes_pinctrl->regmap)
		return dev_err_probe(dev, -ENODEV, "failed to get serdes regmap\n");

	pinctrl_desc = devm_kzalloc(dev, sizeof(*pinctrl_desc), GFP_KERNEL);
	if (!pinctrl_desc)
		return -ENOMEM;

	pinctrl_desc->name = dev_name(dev);
	pinctrl_desc->owner = THIS_MODULE;
	pinctrl_desc->pctlops = &serdes_pinctrl_ops;
	pinctrl_desc->pmxops = &serdes_pinmux_ops;
	pinctrl_desc->confops = &serdes_pinconf_ops;

	pinctrl_desc->npins = pinctrl_info->num_pins;
	serdes_pinctrl->pdesc = devm_kcalloc(dev,
					     pinctrl_info->num_pins,
					     sizeof(*serdes_pinctrl->pdesc),
					     GFP_KERNEL);
	pinctrl_desc->pins = serdes_pinctrl->pdesc;
	if (!serdes_pinctrl->pdesc)
		return -ENOMEM;

	serdes_pinctrl->pinctrl_desc = pinctrl_desc;

	for_each_available_child_of_node(dev->of_node, child) {
		ret = of_parse_phandle_with_fixed_args(child, list_name, 3, 0, &of_args);
		if (ret) {
			dev_err(dev, "Unable to parse %s list property in %s node\n",
				list_name, child->name);
		} else {
			pin_base = of_args.args[1];
			SERDES_DBG_MFD("%s:gpio-range=<%d %d>\n", __func__, pin_base,
				       pin_base + of_args.args[2]);
		}
	}

	if (pin_base) {
		for (i = 0; i < pinctrl_info->num_pins; i++) {
			serdes_pinctrl->pdesc[i].number = pinctrl_info->pins[i].number + pin_base;
			serdes_pinctrl->pdesc[i].name = kasprintf(GFP_KERNEL, "%s-gpio%d",
								  pinctrl_info->pins[i].name,
								  serdes_pinctrl->pdesc[i].number);
			SERDES_DBG_MFD("%s:pdesc number=%d, name=%s\n", __func__,
				       serdes_pinctrl->pdesc[i].number,
				       serdes_pinctrl->pdesc[i].name);
		}
	} else {
		dev_info(serdes->dev, "no pinctrl setting\n");
		return 0;
	}

	serdes_pinctrl->pin_base = pin_base;

	/* Add pinctrl */
	ret = devm_pinctrl_register_and_init(dev, pinctrl_desc, serdes_pinctrl,
					     &serdes_pinctrl->pctl);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register serdes pinctrl\n");

	for (i = 0; i < pinctrl_info->num_groups; i++) {
		struct group_desc *group = &pinctrl_info->groups[i];
		int *grp_pins = devm_kcalloc(dev,
					     group->num_pins, sizeof(*group->pins), GFP_KERNEL);

		for (j = 0; j < group->num_pins; j++) {
			grp_pins[j] = pinctrl_info->groups[i].pins[j] + pin_base;
			SERDES_DBG_MFD("%s group name %s pin=%d base=%d\n", __func__,
				       pinctrl_info->groups[i].name, grp_pins[j], pin_base);
		}

		ret = pinctrl_generic_add_group(serdes_pinctrl->pctl, group->name,
						grp_pins, group->num_pins, group->data);
		if (ret < 0) {
			dev_err(dev, "Failed to register serdes group %s\n",
				group->name);
			return ret;
		}
	}

	for (i = 0; i < pinctrl_info->num_functions; i++) {
		const struct function_desc *func = &pinctrl_info->functions[i];

		ret = pinmux_generic_add_function(serdes_pinctrl->pctl, func->name,
						  func->group_names, func->num_group_names,
						  func->data);
		if (ret < 0) {
			dev_err(dev, "Failed to register serdes function %s\n",
				func->name);
			return ret;
		}
	}

	ret = pinctrl_enable(serdes_pinctrl->pctl);

	ret = serdes_pinctrl_gpio_init(serdes);

	/* pinctrl state*/
	serdes->pinctrl_node = devm_pinctrl_get(dev);
	if (!IS_ERR(serdes->pinctrl_node)) {
		serdes->pins_default =
			pinctrl_lookup_state(serdes->pinctrl_node, PINCTRL_STATE_DEFAULT);
		serdes->pins_init =
			pinctrl_lookup_state(serdes->pinctrl_node, PINCTRL_STATE_INIT);
		serdes->pins_sleep =
			pinctrl_lookup_state(serdes->pinctrl_node, PINCTRL_STATE_SLEEP);
	}

	dev_info(dev, "%s %s serdes_pinctrl_probe successful, pin_base=%d\n",
		 dev_name(dev->parent), serdes->chip_data->name, pin_base);

	return ret;
}

static const struct of_device_id serdes_pinctrl_of_match[] = {
	{ .compatible = "rohm,bu18tl82-pinctrl" },
	{ .compatible = "rohm,bu18rl82-pinctrl" },
	{ .compatible = "maxim,max96745-pinctrl" },
	{ .compatible = "maxim,max96752-pinctrl" },
	{ .compatible = "maxim,max96755-pinctrl" },
	{ .compatible = "maxim,max96772-pinctrl" },
	{ .compatible = "maxim,max96789-pinctrl" },
	{ .compatible = "rockchip,rkx111-pinctrl" },
	{ .compatible = "rockchip,rkx121-pinctrl" },
	{ .compatible = "novo,nca9539-pinctrl" },
	{ }
};

static struct platform_driver serdes_pinctrl_driver = {
	.driver = {
		.name = "serdes-pinctrl",
		.of_match_table = of_match_ptr(serdes_pinctrl_of_match),
	},
	.probe = serdes_pinctrl_probe,
};

static int __init serdes_pinctrl_init(void)
{
	return platform_driver_register(&serdes_pinctrl_driver);
}
subsys_initcall_sync(serdes_pinctrl_init);

static void __exit serdes_pinctrl_exit(void)
{
	platform_driver_unregister(&serdes_pinctrl_driver);
}
module_exit(serdes_pinctrl_exit);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("display pinctrl interface for different serdes");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:serdes-pinctrl");
