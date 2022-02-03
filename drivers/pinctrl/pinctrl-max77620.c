// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX77620 pin control driver.
 *
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Chaitanya Bandi <bandik@nvidia.com>
 *	Laxman Dewangan <ldewangan@nvidia.com>
 */

#include <linux/mfd/max77620.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regmap.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define MAX77620_PIN_NUM 8

enum max77620_pin_ppdrv {
	MAX77620_PIN_UNCONFIG_DRV,
	MAX77620_PIN_OD_DRV,
	MAX77620_PIN_PP_DRV,
};

#define MAX77620_ACTIVE_FPS_SOURCE		(PIN_CONFIG_END + 1)
#define MAX77620_ACTIVE_FPS_POWER_ON_SLOTS	(PIN_CONFIG_END + 2)
#define MAX77620_ACTIVE_FPS_POWER_DOWN_SLOTS	(PIN_CONFIG_END + 3)
#define MAX77620_SUSPEND_FPS_SOURCE		(PIN_CONFIG_END + 4)
#define MAX77620_SUSPEND_FPS_POWER_ON_SLOTS	(PIN_CONFIG_END + 5)
#define MAX77620_SUSPEND_FPS_POWER_DOWN_SLOTS	(PIN_CONFIG_END + 6)

struct max77620_pin_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
	int mux_option;
};

static const struct pinconf_generic_params max77620_cfg_params[] = {
	{
		.property = "maxim,active-fps-source",
		.param = MAX77620_ACTIVE_FPS_SOURCE,
	}, {
		.property = "maxim,active-fps-power-up-slot",
		.param = MAX77620_ACTIVE_FPS_POWER_ON_SLOTS,
	}, {
		.property = "maxim,active-fps-power-down-slot",
		.param = MAX77620_ACTIVE_FPS_POWER_DOWN_SLOTS,
	}, {
		.property = "maxim,suspend-fps-source",
		.param = MAX77620_SUSPEND_FPS_SOURCE,
	}, {
		.property = "maxim,suspend-fps-power-up-slot",
		.param = MAX77620_SUSPEND_FPS_POWER_ON_SLOTS,
	}, {
		.property = "maxim,suspend-fps-power-down-slot",
		.param = MAX77620_SUSPEND_FPS_POWER_DOWN_SLOTS,
	},
};

enum max77620_alternate_pinmux_option {
	MAX77620_PINMUX_GPIO				= 0,
	MAX77620_PINMUX_LOW_POWER_MODE_CONTROL_IN	= 1,
	MAX77620_PINMUX_FLEXIBLE_POWER_SEQUENCER_OUT	= 2,
	MAX77620_PINMUX_32K_OUT1			= 3,
	MAX77620_PINMUX_SD0_DYNAMIC_VOLTAGE_SCALING_IN	= 4,
	MAX77620_PINMUX_SD1_DYNAMIC_VOLTAGE_SCALING_IN	= 5,
	MAX77620_PINMUX_REFERENCE_OUT			= 6,
};

struct max77620_pingroup {
	const char *name;
	const unsigned int pins[1];
	unsigned int npins;
	enum max77620_alternate_pinmux_option alt_option;
};

struct max77620_pin_info {
	enum max77620_pin_ppdrv drv_type;
	int pull_config;
};

struct max77620_fps_config {
	int active_fps_src;
	int active_power_up_slots;
	int active_power_down_slots;
	int suspend_fps_src;
	int suspend_power_up_slots;
	int suspend_power_down_slots;
};

struct max77620_pctrl_info {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct regmap *rmap;
	int pins_current_opt[MAX77620_GPIO_NR];
	const struct max77620_pin_function *functions;
	unsigned int num_functions;
	const struct max77620_pingroup *pin_groups;
	int num_pin_groups;
	const struct pinctrl_pin_desc *pins;
	unsigned int num_pins;
	struct max77620_pin_info pin_info[MAX77620_PIN_NUM];
	struct max77620_fps_config fps_config[MAX77620_PIN_NUM];
};

static const struct pinctrl_pin_desc max77620_pins_desc[] = {
	PINCTRL_PIN(MAX77620_GPIO0, "gpio0"),
	PINCTRL_PIN(MAX77620_GPIO1, "gpio1"),
	PINCTRL_PIN(MAX77620_GPIO2, "gpio2"),
	PINCTRL_PIN(MAX77620_GPIO3, "gpio3"),
	PINCTRL_PIN(MAX77620_GPIO4, "gpio4"),
	PINCTRL_PIN(MAX77620_GPIO5, "gpio5"),
	PINCTRL_PIN(MAX77620_GPIO6, "gpio6"),
	PINCTRL_PIN(MAX77620_GPIO7, "gpio7"),
};

static const char * const gpio_groups[] = {
	"gpio0",
	"gpio1",
	"gpio2",
	"gpio3",
	"gpio4",
	"gpio5",
	"gpio6",
	"gpio7",
};

#define FUNCTION_GROUP(fname, mux)			\
	{						\
		.name = fname,				\
		.groups = gpio_groups,			\
		.ngroups = ARRAY_SIZE(gpio_groups),	\
		.mux_option = MAX77620_PINMUX_##mux,	\
	}

static const struct max77620_pin_function max77620_pin_function[] = {
	FUNCTION_GROUP("gpio", GPIO),
	FUNCTION_GROUP("lpm-control-in", LOW_POWER_MODE_CONTROL_IN),
	FUNCTION_GROUP("fps-out", FLEXIBLE_POWER_SEQUENCER_OUT),
	FUNCTION_GROUP("32k-out1", 32K_OUT1),
	FUNCTION_GROUP("sd0-dvs-in", SD0_DYNAMIC_VOLTAGE_SCALING_IN),
	FUNCTION_GROUP("sd1-dvs-in", SD1_DYNAMIC_VOLTAGE_SCALING_IN),
	FUNCTION_GROUP("reference-out", REFERENCE_OUT),
};

#define MAX77620_PINGROUP(pg_name, pin_id, option) \
	{								\
		.name = #pg_name,					\
		.pins = {MAX77620_##pin_id},				\
		.npins = 1,						\
		.alt_option = MAX77620_PINMUX_##option,			\
	}

static const struct max77620_pingroup max77620_pingroups[] = {
	MAX77620_PINGROUP(gpio0, GPIO0, LOW_POWER_MODE_CONTROL_IN),
	MAX77620_PINGROUP(gpio1, GPIO1, FLEXIBLE_POWER_SEQUENCER_OUT),
	MAX77620_PINGROUP(gpio2, GPIO2, FLEXIBLE_POWER_SEQUENCER_OUT),
	MAX77620_PINGROUP(gpio3, GPIO3, FLEXIBLE_POWER_SEQUENCER_OUT),
	MAX77620_PINGROUP(gpio4, GPIO4, 32K_OUT1),
	MAX77620_PINGROUP(gpio5, GPIO5, SD0_DYNAMIC_VOLTAGE_SCALING_IN),
	MAX77620_PINGROUP(gpio6, GPIO6, SD1_DYNAMIC_VOLTAGE_SCALING_IN),
	MAX77620_PINGROUP(gpio7, GPIO7, REFERENCE_OUT),
};

static int max77620_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);

	return mpci->num_pin_groups;
}

static const char *max77620_pinctrl_get_group_name(
		struct pinctrl_dev *pctldev, unsigned int group)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);

	return mpci->pin_groups[group].name;
}

static int max77620_pinctrl_get_group_pins(
		struct pinctrl_dev *pctldev, unsigned int group,
		const unsigned int **pins, unsigned int *num_pins)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);

	*pins = mpci->pin_groups[group].pins;
	*num_pins = mpci->pin_groups[group].npins;

	return 0;
}

static const struct pinctrl_ops max77620_pinctrl_ops = {
	.get_groups_count = max77620_pinctrl_get_groups_count,
	.get_group_name = max77620_pinctrl_get_group_name,
	.get_group_pins = max77620_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int max77620_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);

	return mpci->num_functions;
}

static const char *max77620_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						  unsigned int function)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);

	return mpci->functions[function].name;
}

static int max77620_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					    unsigned int function,
					    const char * const **groups,
					    unsigned int * const num_groups)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);

	*groups = mpci->functions[function].groups;
	*num_groups = mpci->functions[function].ngroups;

	return 0;
}

static int max77620_pinctrl_enable(struct pinctrl_dev *pctldev,
				   unsigned int function, unsigned int group)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);
	u8 val;
	int ret;

	if (function == MAX77620_PINMUX_GPIO) {
		val = 0;
	} else if (function == mpci->pin_groups[group].alt_option) {
		val = 1 << group;
	} else {
		dev_err(mpci->dev, "GPIO %u doesn't have function %u\n",
			group, function);
		return -EINVAL;
	}
	ret = regmap_update_bits(mpci->rmap, MAX77620_REG_AME_GPIO,
				 BIT(group), val);
	if (ret < 0)
		dev_err(mpci->dev, "REG AME GPIO update failed: %d\n", ret);

	return ret;
}

static const struct pinmux_ops max77620_pinmux_ops = {
	.get_functions_count	= max77620_pinctrl_get_funcs_count,
	.get_function_name	= max77620_pinctrl_get_func_name,
	.get_function_groups	= max77620_pinctrl_get_func_groups,
	.set_mux		= max77620_pinctrl_enable,
};

static int max77620_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = mpci->dev;
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int val;
	int arg = 0;
	int ret;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (mpci->pin_info[pin].drv_type == MAX77620_PIN_OD_DRV)
			arg = 1;
		break;

	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (mpci->pin_info[pin].drv_type == MAX77620_PIN_PP_DRV)
			arg = 1;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		ret = regmap_read(mpci->rmap, MAX77620_REG_PUE_GPIO, &val);
		if (ret < 0) {
			dev_err(dev, "Reg PUE_GPIO read failed: %d\n", ret);
			return ret;
		}
		if (val & BIT(pin))
			arg = 1;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		ret = regmap_read(mpci->rmap, MAX77620_REG_PDE_GPIO, &val);
		if (ret < 0) {
			dev_err(dev, "Reg PDE_GPIO read failed: %d\n", ret);
			return ret;
		}
		if (val & BIT(pin))
			arg = 1;
		break;

	default:
		dev_err(dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, (u16)arg);

	return 0;
}

static int max77620_get_default_fps(struct max77620_pctrl_info *mpci,
				    int addr, int *fps)
{
	unsigned int val;
	int ret;

	ret = regmap_read(mpci->rmap, addr, &val);
	if (ret < 0) {
		dev_err(mpci->dev, "Reg PUE_GPIO read failed: %d\n", ret);
		return ret;
	}
	*fps = (val & MAX77620_FPS_SRC_MASK) >> MAX77620_FPS_SRC_SHIFT;

	return 0;
}

static int max77620_set_fps_param(struct max77620_pctrl_info *mpci,
				  int pin, int param)
{
	struct max77620_fps_config *fps_config = &mpci->fps_config[pin];
	int addr, ret;
	int param_val;
	int mask, shift;

	if ((pin < MAX77620_GPIO1) || (pin > MAX77620_GPIO3))
		return 0;

	addr = MAX77620_REG_FPS_GPIO1 + pin - 1;
	switch (param) {
	case MAX77620_ACTIVE_FPS_SOURCE:
	case MAX77620_SUSPEND_FPS_SOURCE:
		mask = MAX77620_FPS_SRC_MASK;
		shift = MAX77620_FPS_SRC_SHIFT;
		param_val = fps_config->active_fps_src;
		if (param == MAX77620_SUSPEND_FPS_SOURCE)
			param_val = fps_config->suspend_fps_src;
		break;

	case MAX77620_ACTIVE_FPS_POWER_ON_SLOTS:
	case MAX77620_SUSPEND_FPS_POWER_ON_SLOTS:
		mask = MAX77620_FPS_PU_PERIOD_MASK;
		shift = MAX77620_FPS_PU_PERIOD_SHIFT;
		param_val = fps_config->active_power_up_slots;
		if (param == MAX77620_SUSPEND_FPS_POWER_ON_SLOTS)
			param_val = fps_config->suspend_power_up_slots;
		break;

	case MAX77620_ACTIVE_FPS_POWER_DOWN_SLOTS:
	case MAX77620_SUSPEND_FPS_POWER_DOWN_SLOTS:
		mask = MAX77620_FPS_PD_PERIOD_MASK;
		shift = MAX77620_FPS_PD_PERIOD_SHIFT;
		param_val = fps_config->active_power_down_slots;
		if (param == MAX77620_SUSPEND_FPS_POWER_DOWN_SLOTS)
			param_val = fps_config->suspend_power_down_slots;
		break;

	default:
		dev_err(mpci->dev, "Invalid parameter %d for pin %d\n",
			param, pin);
		return -EINVAL;
	}

	if (param_val < 0)
		return 0;

	ret = regmap_update_bits(mpci->rmap, addr, mask, param_val << shift);
	if (ret < 0)
		dev_err(mpci->dev, "Reg 0x%02x update failed %d\n", addr, ret);

	return ret;
}

static int max77620_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *configs,
				unsigned int num_configs)
{
	struct max77620_pctrl_info *mpci = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = mpci->dev;
	struct max77620_fps_config *fps_config;
	int param;
	u32 param_val;
	unsigned int val;
	unsigned int pu_val;
	unsigned int pd_val;
	int addr, ret;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		param_val = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			val = param_val ? 0 : 1;
			ret = regmap_update_bits(mpci->rmap,
						 MAX77620_REG_GPIO0 + pin,
						 MAX77620_CNFG_GPIO_DRV_MASK,
						 val);
			if (ret)
				goto report_update_failure;

			mpci->pin_info[pin].drv_type = val ?
				MAX77620_PIN_PP_DRV : MAX77620_PIN_OD_DRV;
			break;

		case PIN_CONFIG_DRIVE_PUSH_PULL:
			val = param_val ? 1 : 0;
			ret = regmap_update_bits(mpci->rmap,
						 MAX77620_REG_GPIO0 + pin,
						 MAX77620_CNFG_GPIO_DRV_MASK,
						 val);
			if (ret)
				goto report_update_failure;

			mpci->pin_info[pin].drv_type = val ?
				MAX77620_PIN_PP_DRV : MAX77620_PIN_OD_DRV;
			break;

		case MAX77620_ACTIVE_FPS_SOURCE:
		case MAX77620_ACTIVE_FPS_POWER_ON_SLOTS:
		case MAX77620_ACTIVE_FPS_POWER_DOWN_SLOTS:
			if ((pin < MAX77620_GPIO1) || (pin > MAX77620_GPIO3))
				return -EINVAL;

			fps_config = &mpci->fps_config[pin];

			if ((param == MAX77620_ACTIVE_FPS_SOURCE) &&
			    (param_val == MAX77620_FPS_SRC_DEF)) {
				addr = MAX77620_REG_FPS_GPIO1 + pin - 1;
				ret = max77620_get_default_fps(
						mpci, addr,
						&fps_config->active_fps_src);
				if (ret < 0)
					return ret;
				break;
			}

			if (param == MAX77620_ACTIVE_FPS_SOURCE)
				fps_config->active_fps_src = param_val;
			else if (param == MAX77620_ACTIVE_FPS_POWER_ON_SLOTS)
				fps_config->active_power_up_slots = param_val;
			else
				fps_config->active_power_down_slots = param_val;

			ret = max77620_set_fps_param(mpci, pin, param);
			if (ret < 0)
				return ret;
			break;

		case MAX77620_SUSPEND_FPS_SOURCE:
		case MAX77620_SUSPEND_FPS_POWER_ON_SLOTS:
		case MAX77620_SUSPEND_FPS_POWER_DOWN_SLOTS:
			if ((pin < MAX77620_GPIO1) || (pin > MAX77620_GPIO3))
				return -EINVAL;

			fps_config = &mpci->fps_config[pin];

			if ((param == MAX77620_SUSPEND_FPS_SOURCE) &&
			    (param_val == MAX77620_FPS_SRC_DEF)) {
				addr = MAX77620_REG_FPS_GPIO1 + pin - 1;
				ret = max77620_get_default_fps(
						mpci, addr,
						&fps_config->suspend_fps_src);
				if (ret < 0)
					return ret;
				break;
			}

			if (param == MAX77620_SUSPEND_FPS_SOURCE)
				fps_config->suspend_fps_src = param_val;
			else if (param == MAX77620_SUSPEND_FPS_POWER_ON_SLOTS)
				fps_config->suspend_power_up_slots = param_val;
			else
				fps_config->suspend_power_down_slots =
								param_val;
			break;

		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
			pu_val = (param == PIN_CONFIG_BIAS_PULL_UP) ?
							BIT(pin) : 0;
			pd_val = (param == PIN_CONFIG_BIAS_PULL_DOWN) ?
							BIT(pin) : 0;

			ret = regmap_update_bits(mpci->rmap,
						 MAX77620_REG_PUE_GPIO,
						 BIT(pin), pu_val);
			if (ret < 0) {
				dev_err(dev, "PUE_GPIO update failed: %d\n",
					ret);
				return ret;
			}

			ret = regmap_update_bits(mpci->rmap,
						 MAX77620_REG_PDE_GPIO,
						 BIT(pin), pd_val);
			if (ret < 0) {
				dev_err(dev, "PDE_GPIO update failed: %d\n",
					ret);
				return ret;
			}
			break;

		default:
			dev_err(dev, "Properties not supported\n");
			return -ENOTSUPP;
		}
	}

	return 0;

report_update_failure:
	dev_err(dev, "Reg 0x%02x update failed %d\n",
		MAX77620_REG_GPIO0 + pin, ret);
	return ret;
}

static const struct pinconf_ops max77620_pinconf_ops = {
	.pin_config_get = max77620_pinconf_get,
	.pin_config_set = max77620_pinconf_set,
};

static struct pinctrl_desc max77620_pinctrl_desc = {
	.pctlops = &max77620_pinctrl_ops,
	.pmxops = &max77620_pinmux_ops,
	.confops = &max77620_pinconf_ops,
};

static int max77620_pinctrl_probe(struct platform_device *pdev)
{
	struct max77620_chip *max77620 = dev_get_drvdata(pdev->dev.parent);
	struct max77620_pctrl_info *mpci;
	int i;

	device_set_node(&pdev->dev, dev_fwnode(pdev->dev.parent));

	mpci = devm_kzalloc(&pdev->dev, sizeof(*mpci), GFP_KERNEL);
	if (!mpci)
		return -ENOMEM;

	mpci->dev = &pdev->dev;
	mpci->rmap = max77620->rmap;

	mpci->pins = max77620_pins_desc;
	mpci->num_pins = ARRAY_SIZE(max77620_pins_desc);
	mpci->functions = max77620_pin_function;
	mpci->num_functions = ARRAY_SIZE(max77620_pin_function);
	mpci->pin_groups = max77620_pingroups;
	mpci->num_pin_groups = ARRAY_SIZE(max77620_pingroups);
	platform_set_drvdata(pdev, mpci);

	max77620_pinctrl_desc.name = dev_name(&pdev->dev);
	max77620_pinctrl_desc.pins = max77620_pins_desc;
	max77620_pinctrl_desc.npins = ARRAY_SIZE(max77620_pins_desc);
	max77620_pinctrl_desc.num_custom_params =
				ARRAY_SIZE(max77620_cfg_params);
	max77620_pinctrl_desc.custom_params = max77620_cfg_params;

	for (i = 0; i < MAX77620_PIN_NUM; ++i) {
		mpci->fps_config[i].active_fps_src = -1;
		mpci->fps_config[i].active_power_up_slots = -1;
		mpci->fps_config[i].active_power_down_slots = -1;
		mpci->fps_config[i].suspend_fps_src = -1;
		mpci->fps_config[i].suspend_power_up_slots = -1;
		mpci->fps_config[i].suspend_power_down_slots = -1;
	}

	mpci->pctl = devm_pinctrl_register(&pdev->dev, &max77620_pinctrl_desc,
					   mpci);
	if (IS_ERR(mpci->pctl)) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return PTR_ERR(mpci->pctl);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max77620_suspend_fps_param[] = {
	MAX77620_SUSPEND_FPS_SOURCE,
	MAX77620_SUSPEND_FPS_POWER_ON_SLOTS,
	MAX77620_SUSPEND_FPS_POWER_DOWN_SLOTS,
};

static int max77620_active_fps_param[] = {
	MAX77620_ACTIVE_FPS_SOURCE,
	MAX77620_ACTIVE_FPS_POWER_ON_SLOTS,
	MAX77620_ACTIVE_FPS_POWER_DOWN_SLOTS,
};

static int max77620_pinctrl_suspend(struct device *dev)
{
	struct max77620_pctrl_info *mpci = dev_get_drvdata(dev);
	int pin, p;

	for (pin = 0; pin < MAX77620_PIN_NUM; ++pin) {
		if ((pin < MAX77620_GPIO1) || (pin > MAX77620_GPIO3))
			continue;
		for (p = 0; p < 3; ++p)
			max77620_set_fps_param(
				mpci, pin, max77620_suspend_fps_param[p]);
	}

	return 0;
};

static int max77620_pinctrl_resume(struct device *dev)
{
	struct max77620_pctrl_info *mpci = dev_get_drvdata(dev);
	int pin, p;

	for (pin = 0; pin < MAX77620_PIN_NUM; ++pin) {
		if ((pin < MAX77620_GPIO1) || (pin > MAX77620_GPIO3))
			continue;
		for (p = 0; p < 3; ++p)
			max77620_set_fps_param(
				mpci, pin, max77620_active_fps_param[p]);
	}

	return 0;
}
#endif

static const struct dev_pm_ops max77620_pinctrl_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		max77620_pinctrl_suspend, max77620_pinctrl_resume)
};

static const struct platform_device_id max77620_pinctrl_devtype[] = {
	{ .name = "max77620-pinctrl", },
	{ .name = "max20024-pinctrl", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77620_pinctrl_devtype);

static struct platform_driver max77620_pinctrl_driver = {
	.driver = {
		.name = "max77620-pinctrl",
		.pm = &max77620_pinctrl_pm_ops,
	},
	.probe = max77620_pinctrl_probe,
	.id_table = max77620_pinctrl_devtype,
};

module_platform_driver(max77620_pinctrl_driver);

MODULE_DESCRIPTION("MAX77620/MAX20024 pin control driver");
MODULE_AUTHOR("Chaitanya Bandi<bandik@nvidia.com>");
MODULE_AUTHOR("Laxman Dewangan<ldewangan@nvidia.com>");
MODULE_ALIAS("platform:max77620-pinctrl");
MODULE_LICENSE("GPL v2");
