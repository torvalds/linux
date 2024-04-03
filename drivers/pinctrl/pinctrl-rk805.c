// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pinctrl driver for Rockchip RK805/RK806 PMIC
 *
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd.
 *
 * Author: Joseph Chen <chenjh@rock-chips.com>
 * Author: Xu Shengfei <xsf@rock-chips.com>
 *
 * Based on the pinctrl-as3722 driver
 */

#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/rk808.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/property.h>
#include <linux/slab.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

struct rk805_pin_function {
	const char *name;
	const char *const *groups;
	unsigned int ngroups;
	int mux_option;
};

struct rk805_pin_group {
	const char *name;
	const unsigned int pins[1];
	unsigned int npins;
};

/*
 * @reg: gpio setting register;
 * @fun_reg: functions select register;
 * @fun_mask: functions select mask value, when set is gpio;
 * @dir_mask: input or output mask value, when set is output, otherwise input;
 * @val_mask: gpio set value, when set is level high, otherwise low;
 *
 * Different PMIC has different pin features, belowing 3 mask members are not
 * all necessary for every PMIC. For example, RK805 has 2 pins that can be used
 * as output only GPIOs, so func_mask and dir_mask are not needed. RK816 has 1
 * pin that can be used as TS/GPIO, so fun_mask, dir_mask and val_mask are all
 * necessary.
 */
struct rk805_pin_config {
	u8 reg;
	u8 fun_reg;
	u8 fun_msk;
	u8 dir_msk;
	u8 val_msk;
};

struct rk805_pctrl_info {
	struct rk808 *rk808;
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct gpio_chip gpio_chip;
	struct pinctrl_desc pinctrl_desc;
	const struct rk805_pin_function *functions;
	unsigned int num_functions;
	const struct rk805_pin_group *groups;
	int num_pin_groups;
	const struct pinctrl_pin_desc *pins;
	unsigned int num_pins;
	const struct rk805_pin_config *pin_cfg;
};

enum rk805_pinmux_option {
	RK805_PINMUX_GPIO,
};

enum rk806_pinmux_option {
	RK806_PINMUX_FUN0 = 0,
	RK806_PINMUX_FUN1,
	RK806_PINMUX_FUN2,
	RK806_PINMUX_FUN3,
	RK806_PINMUX_FUN4,
	RK806_PINMUX_FUN5,
};

enum {
	RK805_GPIO0,
	RK805_GPIO1,
};

enum {
	RK806_GPIO_DVS1,
	RK806_GPIO_DVS2,
	RK806_GPIO_DVS3
};

static const char *const rk805_gpio_groups[] = {
	"gpio0",
	"gpio1",
};

static const char *const rk806_gpio_groups[] = {
	"gpio_pwrctrl1",
	"gpio_pwrctrl2",
	"gpio_pwrctrl3",
};

/* RK805: 2 output only GPIOs */
static const struct pinctrl_pin_desc rk805_pins_desc[] = {
	PINCTRL_PIN(RK805_GPIO0, "gpio0"),
	PINCTRL_PIN(RK805_GPIO1, "gpio1"),
};

/* RK806 */
static const struct pinctrl_pin_desc rk806_pins_desc[] = {
	PINCTRL_PIN(RK806_GPIO_DVS1, "gpio_pwrctrl1"),
	PINCTRL_PIN(RK806_GPIO_DVS2, "gpio_pwrctrl2"),
	PINCTRL_PIN(RK806_GPIO_DVS3, "gpio_pwrctrl3"),
};

static const struct rk805_pin_function rk805_pin_functions[] = {
	{
		.name = "gpio",
		.groups = rk805_gpio_groups,
		.ngroups = ARRAY_SIZE(rk805_gpio_groups),
		.mux_option = RK805_PINMUX_GPIO,
	},
};

static const struct rk805_pin_function rk806_pin_functions[] = {
	{
		.name = "pin_fun0",
		.groups = rk806_gpio_groups,
		.ngroups = ARRAY_SIZE(rk806_gpio_groups),
		.mux_option = RK806_PINMUX_FUN0,
	},
	{
		.name = "pin_fun1",
		.groups = rk806_gpio_groups,
		.ngroups = ARRAY_SIZE(rk806_gpio_groups),
		.mux_option = RK806_PINMUX_FUN1,
	},
	{
		.name = "pin_fun2",
		.groups = rk806_gpio_groups,
		.ngroups = ARRAY_SIZE(rk806_gpio_groups),
		.mux_option = RK806_PINMUX_FUN2,
	},
	{
		.name = "pin_fun3",
		.groups = rk806_gpio_groups,
		.ngroups = ARRAY_SIZE(rk806_gpio_groups),
		.mux_option = RK806_PINMUX_FUN3,
	},
	{
		.name = "pin_fun4",
		.groups = rk806_gpio_groups,
		.ngroups = ARRAY_SIZE(rk806_gpio_groups),
		.mux_option = RK806_PINMUX_FUN4,
	},
	{
		.name = "pin_fun5",
		.groups = rk806_gpio_groups,
		.ngroups = ARRAY_SIZE(rk806_gpio_groups),
		.mux_option = RK806_PINMUX_FUN5,
	},
};

static const struct rk805_pin_group rk805_pin_groups[] = {
	{
		.name = "gpio0",
		.pins = { RK805_GPIO0 },
		.npins = 1,
	},
	{
		.name = "gpio1",
		.pins = { RK805_GPIO1 },
		.npins = 1,
	},
};

static const struct rk805_pin_group rk806_pin_groups[] = {
	{
		.name = "gpio_pwrctrl1",
		.pins = { RK806_GPIO_DVS1 },
		.npins = 1,
	},
	{
		.name = "gpio_pwrctrl2",
		.pins = { RK806_GPIO_DVS2 },
		.npins = 1,
	},
	{
		.name = "gpio_pwrctrl3",
		.pins = { RK806_GPIO_DVS3 },
		.npins = 1,
	}
};

#define RK805_GPIO0_VAL_MSK	BIT(0)
#define RK805_GPIO1_VAL_MSK	BIT(1)

static const struct rk805_pin_config rk805_gpio_cfgs[] = {
	{
		.reg = RK805_OUT_REG,
		.val_msk = RK805_GPIO0_VAL_MSK,
	},
	{
		.reg = RK805_OUT_REG,
		.val_msk = RK805_GPIO1_VAL_MSK,
	},
};

#define RK806_PWRCTRL1_DR	BIT(0)
#define RK806_PWRCTRL2_DR	BIT(1)
#define RK806_PWRCTRL3_DR	BIT(2)
#define RK806_PWRCTRL1_DATA	BIT(4)
#define RK806_PWRCTRL2_DATA	BIT(5)
#define RK806_PWRCTRL3_DATA	BIT(6)
#define RK806_PWRCTRL1_FUN	GENMASK(2, 0)
#define RK806_PWRCTRL2_FUN	GENMASK(6, 4)
#define RK806_PWRCTRL3_FUN	GENMASK(2, 0)

static struct rk805_pin_config rk806_gpio_cfgs[] = {
	{
		.fun_reg = RK806_SLEEP_CONFIG0,
		.fun_msk = RK806_PWRCTRL1_FUN,
		.reg = RK806_SLEEP_GPIO,
		.val_msk = RK806_PWRCTRL1_DATA,
		.dir_msk = RK806_PWRCTRL1_DR,
	},
	{
		.fun_reg = RK806_SLEEP_CONFIG0,
		.fun_msk = RK806_PWRCTRL2_FUN,
		.reg = RK806_SLEEP_GPIO,
		.val_msk = RK806_PWRCTRL2_DATA,
		.dir_msk = RK806_PWRCTRL2_DR,
	},
	{
		.fun_reg = RK806_SLEEP_CONFIG1,
		.fun_msk = RK806_PWRCTRL3_FUN,
		.reg = RK806_SLEEP_GPIO,
		.val_msk = RK806_PWRCTRL3_DATA,
		.dir_msk = RK806_PWRCTRL3_DR,
	}
};

/* generic gpio chip */
static int rk805_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct rk805_pctrl_info *pci = gpiochip_get_data(chip);
	int ret, val;

	ret = regmap_read(pci->rk808->regmap, pci->pin_cfg[offset].reg, &val);
	if (ret) {
		dev_err(pci->dev, "get gpio%d value failed\n", offset);
		return ret;
	}

	return !!(val & pci->pin_cfg[offset].val_msk);
}

static void rk805_gpio_set(struct gpio_chip *chip,
			   unsigned int offset,
			   int value)
{
	struct rk805_pctrl_info *pci = gpiochip_get_data(chip);
	int ret;

	ret = regmap_update_bits(pci->rk808->regmap,
				 pci->pin_cfg[offset].reg,
				 pci->pin_cfg[offset].val_msk,
				 value ? pci->pin_cfg[offset].val_msk : 0);
	if (ret)
		dev_err(pci->dev, "set gpio%d value %d failed\n",
			offset, value);
}

static int rk805_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	rk805_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip, offset);
}

static int rk805_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct rk805_pctrl_info *pci = gpiochip_get_data(chip);
	unsigned int val;
	int ret;

	/* default output*/
	if (!pci->pin_cfg[offset].dir_msk)
		return GPIO_LINE_DIRECTION_OUT;

	ret = regmap_read(pci->rk808->regmap,
			  pci->pin_cfg[offset].reg,
			  &val);
	if (ret) {
		dev_err(pci->dev, "get gpio%d direction failed\n", offset);
		return ret;
	}

	if (val & pci->pin_cfg[offset].dir_msk)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static const struct gpio_chip rk805_gpio_chip = {
	.label			= "rk805-gpio",
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.get_direction		= rk805_gpio_get_direction,
	.get			= rk805_gpio_get,
	.set			= rk805_gpio_set,
	.direction_input	= pinctrl_gpio_direction_input,
	.direction_output	= rk805_gpio_direction_output,
	.can_sleep		= true,
	.base			= -1,
	.owner			= THIS_MODULE,
};

/* generic pinctrl */
static int rk805_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->num_pin_groups;
}

static const char *rk805_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int group)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->groups[group].name;
}

static int rk805_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int group,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	*pins = pci->groups[group].pins;
	*num_pins = pci->groups[group].npins;

	return 0;
}

static const struct pinctrl_ops rk805_pinctrl_ops = {
	.get_groups_count = rk805_pinctrl_get_groups_count,
	.get_group_name = rk805_pinctrl_get_group_name,
	.get_group_pins = rk805_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_free_map,
};

static int rk805_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->num_functions;
}

static const char *rk805_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
					       unsigned int function)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	return pci->functions[function].name;
}

static int rk805_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					 unsigned int function,
					 const char *const **groups,
					 unsigned int *const num_groups)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	*groups = pci->functions[function].groups;
	*num_groups = pci->functions[function].ngroups;

	return 0;
}

static int _rk805_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				  unsigned int offset,
				  int mux)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	int ret;

	if (!pci->pin_cfg[offset].fun_msk)
		return 0;

	mux <<= ffs(pci->pin_cfg[offset].fun_msk) - 1;
	ret = regmap_update_bits(pci->rk808->regmap,
				 pci->pin_cfg[offset].fun_reg,
				 pci->pin_cfg[offset].fun_msk, mux);

	if (ret)
		dev_err(pci->dev, "set gpio%d func%d failed\n", offset, mux);

	return 0;
}

static int rk805_pinctrl_set_mux(struct pinctrl_dev *pctldev,
				 unsigned int function,
				 unsigned int group)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	int mux = pci->functions[function].mux_option;
	int offset = group;

	return _rk805_pinctrl_set_mux(pctldev, offset, mux);
}

static int rk805_pinctrl_gpio_request_enable(struct pinctrl_dev *pctldev,
					     struct pinctrl_gpio_range *range,
					     unsigned int offset)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);

	switch (pci->rk808->variant) {
	case RK805_ID:
		return _rk805_pinctrl_set_mux(pctldev, offset, RK805_PINMUX_GPIO);
	case RK806_ID:
		return _rk805_pinctrl_set_mux(pctldev, offset, RK806_PINMUX_FUN5);
	}

	return -ENOTSUPP;
}

static int rk805_pmx_gpio_set_direction(struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned int offset, bool input)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	int ret;

	/* set direction */
	if (!pci->pin_cfg[offset].dir_msk)
		return 0;

	ret = regmap_update_bits(pci->rk808->regmap,
				 pci->pin_cfg[offset].reg,
				 pci->pin_cfg[offset].dir_msk,
				 input ? 0 : pci->pin_cfg[offset].dir_msk);
	if (ret) {
		dev_err(pci->dev, "set gpio%d direction failed\n", offset);
		return ret;
	}

	return ret;
}

static const struct pinmux_ops rk805_pinmux_ops = {
	.get_functions_count	= rk805_pinctrl_get_funcs_count,
	.get_function_name	= rk805_pinctrl_get_func_name,
	.get_function_groups	= rk805_pinctrl_get_func_groups,
	.set_mux		= rk805_pinctrl_set_mux,
	.gpio_request_enable	= rk805_pinctrl_gpio_request_enable,
	.gpio_set_direction	= rk805_pmx_gpio_set_direction,
};

static int rk805_pinconf_get(struct pinctrl_dev *pctldev,
			     unsigned int pin, unsigned long *config)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	u32 arg = 0;

	switch (param) {
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_INPUT_ENABLE:
		arg = rk805_gpio_get(&pci->gpio_chip, pin);
		break;
	default:
		dev_err(pci->dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, (u16)arg);

	return 0;
}

static int rk805_pinconf_set(struct pinctrl_dev *pctldev,
			     unsigned int pin, unsigned long *configs,
			     unsigned int num_configs)
{
	struct rk805_pctrl_info *pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	u32 i, arg = 0;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_OUTPUT:
			rk805_gpio_set(&pci->gpio_chip, pin, arg);
			rk805_pmx_gpio_set_direction(pctldev, NULL, pin, false);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			if (pci->rk808->variant != RK805_ID && arg) {
				rk805_pmx_gpio_set_direction(pctldev, NULL, pin, true);
				break;
			}
			fallthrough;
		default:
			dev_err(pci->dev, "Properties not supported\n");
			return -ENOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops rk805_pinconf_ops = {
	.pin_config_get = rk805_pinconf_get,
	.pin_config_set = rk805_pinconf_set,
};

static const struct pinctrl_desc rk805_pinctrl_desc = {
	.name = "rk805-pinctrl",
	.pctlops = &rk805_pinctrl_ops,
	.pmxops = &rk805_pinmux_ops,
	.confops = &rk805_pinconf_ops,
	.owner = THIS_MODULE,
};

static int rk805_pinctrl_probe(struct platform_device *pdev)
{
	struct rk805_pctrl_info *pci;
	int ret;

	device_set_node(&pdev->dev, dev_fwnode(pdev->dev.parent));

	pci = devm_kzalloc(&pdev->dev, sizeof(*pci), GFP_KERNEL);
	if (!pci)
		return -ENOMEM;

	pci->dev = &pdev->dev;
	pci->rk808 = dev_get_drvdata(pdev->dev.parent);

	pci->pinctrl_desc = rk805_pinctrl_desc;
	pci->gpio_chip = rk805_gpio_chip;
	pci->gpio_chip.parent = &pdev->dev;

	platform_set_drvdata(pdev, pci);

	switch (pci->rk808->variant) {
	case RK805_ID:
		pci->pins = rk805_pins_desc;
		pci->num_pins = ARRAY_SIZE(rk805_pins_desc);
		pci->functions = rk805_pin_functions;
		pci->num_functions = ARRAY_SIZE(rk805_pin_functions);
		pci->groups = rk805_pin_groups;
		pci->num_pin_groups = ARRAY_SIZE(rk805_pin_groups);
		pci->pinctrl_desc.pins = rk805_pins_desc;
		pci->pinctrl_desc.npins = ARRAY_SIZE(rk805_pins_desc);
		pci->pin_cfg = rk805_gpio_cfgs;
		pci->gpio_chip.ngpio = ARRAY_SIZE(rk805_gpio_cfgs);
		break;
	case RK806_ID:
		pci->pins = rk806_pins_desc;
		pci->num_pins = ARRAY_SIZE(rk806_pins_desc);
		pci->functions = rk806_pin_functions;
		pci->num_functions = ARRAY_SIZE(rk806_pin_functions);
		pci->groups = rk806_pin_groups;
		pci->num_pin_groups = ARRAY_SIZE(rk806_pin_groups);
		pci->pinctrl_desc.pins = rk806_pins_desc;
		pci->pinctrl_desc.npins = ARRAY_SIZE(rk806_pins_desc);
		pci->pin_cfg = rk806_gpio_cfgs;
		pci->gpio_chip.ngpio = ARRAY_SIZE(rk806_gpio_cfgs);
		break;
	default:
		dev_err(&pdev->dev, "unsupported RK805 ID %lu\n",
			pci->rk808->variant);
		return -EINVAL;
	}

	/* Add gpio chip */
	ret = devm_gpiochip_add_data(&pdev->dev, &pci->gpio_chip, pci);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't add gpiochip\n");
		return ret;
	}

	/* Add pinctrl */
	pci->pctl = devm_pinctrl_register(&pdev->dev, &pci->pinctrl_desc, pci);
	if (IS_ERR(pci->pctl)) {
		dev_err(&pdev->dev, "Couldn't add pinctrl\n");
		return PTR_ERR(pci->pctl);
	}

	/* Add pin range */
	ret = gpiochip_add_pin_range(&pci->gpio_chip, dev_name(&pdev->dev),
				     0, 0, pci->gpio_chip.ngpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't add gpiochip pin range\n");
		return ret;
	}

	return 0;
}

static struct platform_driver rk805_pinctrl_driver = {
	.probe = rk805_pinctrl_probe,
	.driver = {
		.name = "rk805-pinctrl",
	},
};
module_platform_driver(rk805_pinctrl_driver);

MODULE_DESCRIPTION("RK805 pin control and GPIO driver");
MODULE_AUTHOR("Xu Shengfei <xsf@rock-chips.com>");
MODULE_AUTHOR("Joseph Chen <chenjh@rock-chips.com>");
MODULE_LICENSE("GPL v2");
