/*
 * ams AS3722 pin control and GPIO driver.
 *
 * Copyright (c) 2013, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/as3722.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

#define AS3722_PIN_GPIO0		0
#define AS3722_PIN_GPIO1		1
#define AS3722_PIN_GPIO2		2
#define AS3722_PIN_GPIO3		3
#define AS3722_PIN_GPIO4		4
#define AS3722_PIN_GPIO5		5
#define AS3722_PIN_GPIO6		6
#define AS3722_PIN_GPIO7		7
#define AS3722_PIN_NUM			(AS3722_PIN_GPIO7 + 1)

#define AS3722_GPIO_MODE_PULL_UP           BIT(PIN_CONFIG_BIAS_PULL_UP)
#define AS3722_GPIO_MODE_PULL_DOWN         BIT(PIN_CONFIG_BIAS_PULL_DOWN)
#define AS3722_GPIO_MODE_HIGH_IMPED        BIT(PIN_CONFIG_BIAS_HIGH_IMPEDANCE)
#define AS3722_GPIO_MODE_OPEN_DRAIN        BIT(PIN_CONFIG_DRIVE_OPEN_DRAIN)

struct as3722_pin_function {
	const char *name;
	const char * const *groups;
	unsigned ngroups;
	int mux_option;
};

struct as3722_gpio_pin_control {
	unsigned mode_prop;
	int io_function;
};

struct as3722_pingroup {
	const char *name;
	const unsigned pins[1];
	unsigned npins;
};

struct as3722_pctrl_info {
	struct device *dev;
	struct pinctrl_dev *pctl;
	struct as3722 *as3722;
	struct gpio_chip gpio_chip;
	int pins_current_opt[AS3722_PIN_NUM];
	const struct as3722_pin_function *functions;
	unsigned num_functions;
	const struct as3722_pingroup *pin_groups;
	int num_pin_groups;
	const struct pinctrl_pin_desc *pins;
	unsigned num_pins;
	struct as3722_gpio_pin_control gpio_control[AS3722_PIN_NUM];
};

static const struct pinctrl_pin_desc as3722_pins_desc[] = {
	PINCTRL_PIN(AS3722_PIN_GPIO0, "gpio0"),
	PINCTRL_PIN(AS3722_PIN_GPIO1, "gpio1"),
	PINCTRL_PIN(AS3722_PIN_GPIO2, "gpio2"),
	PINCTRL_PIN(AS3722_PIN_GPIO3, "gpio3"),
	PINCTRL_PIN(AS3722_PIN_GPIO4, "gpio4"),
	PINCTRL_PIN(AS3722_PIN_GPIO5, "gpio5"),
	PINCTRL_PIN(AS3722_PIN_GPIO6, "gpio6"),
	PINCTRL_PIN(AS3722_PIN_GPIO7, "gpio7"),
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

enum as3722_pinmux_option {
	AS3722_PINMUX_GPIO			= 0,
	AS3722_PINMUX_INTERRUPT_OUT		= 1,
	AS3722_PINMUX_VSUB_VBAT_UNDEB_LOW_OUT	= 2,
	AS3722_PINMUX_GPIO_INTERRUPT		= 3,
	AS3722_PINMUX_PWM_INPUT			= 4,
	AS3722_PINMUX_VOLTAGE_IN_STBY		= 5,
	AS3722_PINMUX_OC_PG_SD0			= 6,
	AS3722_PINMUX_PG_OUT			= 7,
	AS3722_PINMUX_CLK32K_OUT		= 8,
	AS3722_PINMUX_WATCHDOG_INPUT		= 9,
	AS3722_PINMUX_SOFT_RESET_IN		= 11,
	AS3722_PINMUX_PWM_OUTPUT		= 12,
	AS3722_PINMUX_VSUB_VBAT_LOW_DEB_OUT	= 13,
	AS3722_PINMUX_OC_PG_SD6			= 14,
};

#define FUNCTION_GROUP(fname, mux)			\
	{						\
		.name = #fname,				\
		.groups = gpio_groups,			\
		.ngroups = ARRAY_SIZE(gpio_groups),	\
		.mux_option = AS3722_PINMUX_##mux,	\
	}

static const struct as3722_pin_function as3722_pin_function[] = {
	FUNCTION_GROUP(gpio, GPIO),
	FUNCTION_GROUP(interrupt-out, INTERRUPT_OUT),
	FUNCTION_GROUP(gpio-in-interrupt, GPIO_INTERRUPT),
	FUNCTION_GROUP(vsup-vbat-low-undebounce-out, VSUB_VBAT_UNDEB_LOW_OUT),
	FUNCTION_GROUP(vsup-vbat-low-debounce-out, VSUB_VBAT_LOW_DEB_OUT),
	FUNCTION_GROUP(voltage-in-standby, VOLTAGE_IN_STBY),
	FUNCTION_GROUP(oc-pg-sd0, OC_PG_SD0),
	FUNCTION_GROUP(oc-pg-sd6, OC_PG_SD6),
	FUNCTION_GROUP(powergood-out, PG_OUT),
	FUNCTION_GROUP(pwm-in, PWM_INPUT),
	FUNCTION_GROUP(pwm-out, PWM_OUTPUT),
	FUNCTION_GROUP(clk32k-out, CLK32K_OUT),
	FUNCTION_GROUP(watchdog-in, WATCHDOG_INPUT),
	FUNCTION_GROUP(soft-reset-in, SOFT_RESET_IN),
};

#define AS3722_PINGROUP(pg_name, pin_id) \
	{								\
		.name = #pg_name,					\
		.pins = {AS3722_PIN_##pin_id},				\
		.npins = 1,						\
	}

static const struct as3722_pingroup as3722_pingroups[] = {
	AS3722_PINGROUP(gpio0,	GPIO0),
	AS3722_PINGROUP(gpio1,	GPIO1),
	AS3722_PINGROUP(gpio2,	GPIO2),
	AS3722_PINGROUP(gpio3,	GPIO3),
	AS3722_PINGROUP(gpio4,	GPIO4),
	AS3722_PINGROUP(gpio5,	GPIO5),
	AS3722_PINGROUP(gpio6,	GPIO6),
	AS3722_PINGROUP(gpio7,	GPIO7),
};

static int as3722_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->num_pin_groups;
}

static const char *as3722_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
		unsigned group)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->pin_groups[group].name;
}

static int as3722_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned group, const unsigned **pins, unsigned *num_pins)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	*pins = as_pci->pin_groups[group].pins;
	*num_pins = as_pci->pin_groups[group].npins;
	return 0;
}

static const struct pinctrl_ops as3722_pinctrl_ops = {
	.get_groups_count = as3722_pinctrl_get_groups_count,
	.get_group_name = as3722_pinctrl_get_group_name,
	.get_group_pins = as3722_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int as3722_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->num_functions;
}

static const char *as3722_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
			unsigned function)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	return as_pci->functions[function].name;
}

static int as3722_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
		unsigned function, const char * const **groups,
		unsigned * const num_groups)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	*groups = as_pci->functions[function].groups;
	*num_groups = as_pci->functions[function].ngroups;
	return 0;
}

static int as3722_pinctrl_set(struct pinctrl_dev *pctldev, unsigned function,
		unsigned group)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	int gpio_cntr_reg = AS3722_GPIOn_CONTROL_REG(group);
	u8 val = AS3722_GPIO_IOSF_VAL(as_pci->functions[function].mux_option);
	int ret;

	dev_dbg(as_pci->dev, "%s(): GPIO %u pin to function %u and val %u\n",
		__func__, group, function, val);

	ret = as3722_update_bits(as_pci->as3722, gpio_cntr_reg,
			AS3722_GPIO_IOSF_MASK, val);
	if (ret < 0) {
		dev_err(as_pci->dev, "GPIO%d_CTRL_REG update failed %d\n",
			group, ret);
		return ret;
	}
	as_pci->gpio_control[group].io_function = function;

	switch (val) {
	case AS3722_GPIO_IOSF_SD0_OUT:
	case AS3722_GPIO_IOSF_PWR_GOOD_OUT:
	case AS3722_GPIO_IOSF_Q32K_OUT:
	case AS3722_GPIO_IOSF_PWM_OUT:
	case AS3722_GPIO_IOSF_SD6_LOW_VOLT_LOW:
		ret = as3722_update_bits(as_pci->as3722, gpio_cntr_reg,
			AS3722_GPIO_MODE_MASK, AS3722_GPIO_MODE_OUTPUT_VDDH);
		if (ret < 0) {
			dev_err(as_pci->dev, "GPIO%d_CTRL update failed %d\n",
				group, ret);
			return ret;
		}
		as_pci->gpio_control[group].mode_prop =
				AS3722_GPIO_MODE_OUTPUT_VDDH;
		break;
	default:
		break;
	}
	return ret;
}

static int as3722_pinctrl_gpio_get_mode(unsigned gpio_mode_prop, bool input)
{
	if (gpio_mode_prop & AS3722_GPIO_MODE_HIGH_IMPED)
		return -EINVAL;

	if (gpio_mode_prop & AS3722_GPIO_MODE_OPEN_DRAIN) {
		if (gpio_mode_prop & AS3722_GPIO_MODE_PULL_UP)
			return AS3722_GPIO_MODE_IO_OPEN_DRAIN_PULL_UP;
		return AS3722_GPIO_MODE_IO_OPEN_DRAIN;
	}
	if (input) {
		if (gpio_mode_prop & AS3722_GPIO_MODE_PULL_UP)
			return AS3722_GPIO_MODE_INPUT_PULL_UP;
		else if (gpio_mode_prop & AS3722_GPIO_MODE_PULL_DOWN)
			return AS3722_GPIO_MODE_INPUT_PULL_DOWN;
		return AS3722_GPIO_MODE_INPUT;
	}
	if (gpio_mode_prop & AS3722_GPIO_MODE_PULL_DOWN)
		return AS3722_GPIO_MODE_OUTPUT_VDDL;
	return AS3722_GPIO_MODE_OUTPUT_VDDH;
}

static int as3722_pinctrl_gpio_request_enable(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);

	if (as_pci->gpio_control[offset].io_function)
		return -EBUSY;
	return 0;
}

static int as3722_pinctrl_gpio_set_direction(struct pinctrl_dev *pctldev,
		struct pinctrl_gpio_range *range, unsigned offset, bool input)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	struct as3722 *as3722 = as_pci->as3722;
	int mode;

	mode = as3722_pinctrl_gpio_get_mode(
			as_pci->gpio_control[offset].mode_prop, input);
	if (mode < 0) {
		dev_err(as_pci->dev, "%s direction for GPIO %d not supported\n",
			(input) ? "Input" : "Output", offset);
		return mode;
	}

	return as3722_update_bits(as3722, AS3722_GPIOn_CONTROL_REG(offset),
				AS3722_GPIO_MODE_MASK, mode);
}

static const struct pinmux_ops as3722_pinmux_ops = {
	.get_functions_count	= as3722_pinctrl_get_funcs_count,
	.get_function_name	= as3722_pinctrl_get_func_name,
	.get_function_groups	= as3722_pinctrl_get_func_groups,
	.set_mux		= as3722_pinctrl_set,
	.gpio_request_enable	= as3722_pinctrl_gpio_request_enable,
	.gpio_set_direction	= as3722_pinctrl_gpio_set_direction,
};

static int as3722_pinconf_get(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *config)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int arg = 0;
	u16 prop;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		prop = AS3722_GPIO_MODE_PULL_UP |
				AS3722_GPIO_MODE_PULL_DOWN;
		if (!(as_pci->gpio_control[pin].mode_prop & prop))
			arg = 1;
		prop = 0;
		break;

	case PIN_CONFIG_BIAS_PULL_UP:
		prop = AS3722_GPIO_MODE_PULL_UP;
		break;

	case PIN_CONFIG_BIAS_PULL_DOWN:
		prop = AS3722_GPIO_MODE_PULL_DOWN;
		break;

	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		prop = AS3722_GPIO_MODE_OPEN_DRAIN;
		break;

	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		prop = AS3722_GPIO_MODE_HIGH_IMPED;
		break;

	default:
		dev_err(as_pci->dev, "Properties not supported\n");
		return -ENOTSUPP;
	}

	if (as_pci->gpio_control[pin].mode_prop & prop)
		arg = 1;

	*config = pinconf_to_config_packed(param, (u16)arg);
	return 0;
}

static int as3722_pinconf_set(struct pinctrl_dev *pctldev,
			unsigned pin, unsigned long *configs,
			unsigned num_configs)
{
	struct as3722_pctrl_info *as_pci = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	int mode_prop;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		mode_prop = as_pci->gpio_control[pin].mode_prop;

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
			break;

		case PIN_CONFIG_BIAS_DISABLE:
			mode_prop &= ~(AS3722_GPIO_MODE_PULL_UP |
					AS3722_GPIO_MODE_PULL_DOWN);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			mode_prop |= AS3722_GPIO_MODE_PULL_UP;
			break;

		case PIN_CONFIG_BIAS_PULL_DOWN:
			mode_prop |= AS3722_GPIO_MODE_PULL_DOWN;
			break;

		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			mode_prop |= AS3722_GPIO_MODE_HIGH_IMPED;
			break;

		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			mode_prop |= AS3722_GPIO_MODE_OPEN_DRAIN;
			break;

		default:
			dev_err(as_pci->dev, "Properties not supported\n");
			return -ENOTSUPP;
		}

		as_pci->gpio_control[pin].mode_prop = mode_prop;
	}
	return 0;
}

static const struct pinconf_ops as3722_pinconf_ops = {
	.pin_config_get = as3722_pinconf_get,
	.pin_config_set = as3722_pinconf_set,
};

static struct pinctrl_desc as3722_pinctrl_desc = {
	.pctlops = &as3722_pinctrl_ops,
	.pmxops = &as3722_pinmux_ops,
	.confops = &as3722_pinconf_ops,
	.owner = THIS_MODULE,
};

static inline struct as3722_pctrl_info *to_as_pci(struct gpio_chip *chip)
{
	return container_of(chip, struct as3722_pctrl_info, gpio_chip);
}

static int as3722_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct as3722_pctrl_info *as_pci = to_as_pci(chip);
	struct as3722 *as3722 = as_pci->as3722;
	int ret;
	u32 reg;
	u32 control;
	u32 val;
	int mode;
	int invert_enable;

	ret = as3722_read(as3722, AS3722_GPIOn_CONTROL_REG(offset), &control);
	if (ret < 0) {
		dev_err(as_pci->dev,
			"GPIO_CONTROL%d_REG read failed: %d\n", offset, ret);
		return ret;
	}

	invert_enable = !!(control & AS3722_GPIO_INV);
	mode = control & AS3722_GPIO_MODE_MASK;
	switch (mode) {
	case AS3722_GPIO_MODE_INPUT:
	case AS3722_GPIO_MODE_INPUT_PULL_UP:
	case AS3722_GPIO_MODE_INPUT_PULL_DOWN:
	case AS3722_GPIO_MODE_IO_OPEN_DRAIN:
	case AS3722_GPIO_MODE_IO_OPEN_DRAIN_PULL_UP:
		reg = AS3722_GPIO_SIGNAL_IN_REG;
		break;
	case AS3722_GPIO_MODE_OUTPUT_VDDH:
	case AS3722_GPIO_MODE_OUTPUT_VDDL:
		reg = AS3722_GPIO_SIGNAL_OUT_REG;
		break;
	default:
		return -EINVAL;
	}

	ret = as3722_read(as3722, reg, &val);
	if (ret < 0) {
		dev_err(as_pci->dev,
			"GPIO_SIGNAL_IN_REG read failed: %d\n", ret);
		return ret;
	}

	val = !!(val & AS3722_GPIOn_SIGNAL(offset));
	return (invert_enable) ? !val : val;
}

static void as3722_gpio_set(struct gpio_chip *chip, unsigned offset,
		int value)
{
	struct as3722_pctrl_info *as_pci = to_as_pci(chip);
	struct as3722 *as3722 = as_pci->as3722;
	int en_invert;
	u32 val;
	int ret;

	ret = as3722_read(as3722, AS3722_GPIOn_CONTROL_REG(offset), &val);
	if (ret < 0) {
		dev_err(as_pci->dev,
			"GPIO_CONTROL%d_REG read failed: %d\n", offset, ret);
		return;
	}
	en_invert = !!(val & AS3722_GPIO_INV);

	if (value)
		val = (en_invert) ? 0 : AS3722_GPIOn_SIGNAL(offset);
	else
		val = (en_invert) ? AS3722_GPIOn_SIGNAL(offset) : 0;

	ret = as3722_update_bits(as3722, AS3722_GPIO_SIGNAL_OUT_REG,
			AS3722_GPIOn_SIGNAL(offset), val);
	if (ret < 0)
		dev_err(as_pci->dev,
			"GPIO_SIGNAL_OUT_REG update failed: %d\n", ret);
}

static int as3722_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_gpio_direction_input(chip->base + offset);
}

static int as3722_gpio_direction_output(struct gpio_chip *chip,
		unsigned offset, int value)
{
	as3722_gpio_set(chip, offset, value);
	return pinctrl_gpio_direction_output(chip->base + offset);
}

static int as3722_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct as3722_pctrl_info *as_pci = to_as_pci(chip);

	return as3722_irq_get_virq(as_pci->as3722, offset);
}

static int as3722_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return pinctrl_request_gpio(chip->base + offset);
}

static void as3722_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	pinctrl_free_gpio(chip->base + offset);
}

static const struct gpio_chip as3722_gpio_chip = {
	.label			= "as3722-gpio",
	.owner			= THIS_MODULE,
	.request		= as3722_gpio_request,
	.free			= as3722_gpio_free,
	.get			= as3722_gpio_get,
	.set			= as3722_gpio_set,
	.direction_input	= as3722_gpio_direction_input,
	.direction_output	= as3722_gpio_direction_output,
	.to_irq			= as3722_gpio_to_irq,
	.can_sleep		= true,
	.ngpio			= AS3722_PIN_NUM,
	.base			= -1,
};

static int as3722_pinctrl_probe(struct platform_device *pdev)
{
	struct as3722_pctrl_info *as_pci;
	int ret;

	as_pci = devm_kzalloc(&pdev->dev, sizeof(*as_pci), GFP_KERNEL);
	if (!as_pci)
		return -ENOMEM;

	as_pci->dev = &pdev->dev;
	as_pci->dev->of_node = pdev->dev.parent->of_node;
	as_pci->as3722 = dev_get_drvdata(pdev->dev.parent);
	platform_set_drvdata(pdev, as_pci);

	as_pci->pins = as3722_pins_desc;
	as_pci->num_pins = ARRAY_SIZE(as3722_pins_desc);
	as_pci->functions = as3722_pin_function;
	as_pci->num_functions = ARRAY_SIZE(as3722_pin_function);
	as_pci->pin_groups = as3722_pingroups;
	as_pci->num_pin_groups = ARRAY_SIZE(as3722_pingroups);
	as3722_pinctrl_desc.name = dev_name(&pdev->dev);
	as3722_pinctrl_desc.pins = as3722_pins_desc;
	as3722_pinctrl_desc.npins = ARRAY_SIZE(as3722_pins_desc);
	as_pci->pctl = pinctrl_register(&as3722_pinctrl_desc,
					&pdev->dev, as_pci);
	if (!as_pci->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -EINVAL;
	}

	as_pci->gpio_chip = as3722_gpio_chip;
	as_pci->gpio_chip.dev = &pdev->dev;
	as_pci->gpio_chip.of_node = pdev->dev.parent->of_node;
	ret = gpiochip_add(&as_pci->gpio_chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't register gpiochip, %d\n", ret);
		goto fail_chip_add;
	}

	ret = gpiochip_add_pin_range(&as_pci->gpio_chip, dev_name(&pdev->dev),
				0, 0, AS3722_PIN_NUM);
	if (ret < 0) {
		dev_err(&pdev->dev, "Couldn't add pin range, %d\n", ret);
		goto fail_range_add;
	}

	return 0;

fail_range_add:
	gpiochip_remove(&as_pci->gpio_chip);
fail_chip_add:
	pinctrl_unregister(as_pci->pctl);
	return ret;
}

static int as3722_pinctrl_remove(struct platform_device *pdev)
{
	struct as3722_pctrl_info *as_pci = platform_get_drvdata(pdev);

	gpiochip_remove(&as_pci->gpio_chip);
	pinctrl_unregister(as_pci->pctl);
	return 0;
}

static struct of_device_id as3722_pinctrl_of_match[] = {
	{ .compatible = "ams,as3722-pinctrl", },
	{ },
};
MODULE_DEVICE_TABLE(of, as3722_pinctrl_of_match);

static struct platform_driver as3722_pinctrl_driver = {
	.driver = {
		.name = "as3722-pinctrl",
		.of_match_table = as3722_pinctrl_of_match,
	},
	.probe = as3722_pinctrl_probe,
	.remove = as3722_pinctrl_remove,
};
module_platform_driver(as3722_pinctrl_driver);

MODULE_ALIAS("platform:as3722-pinctrl");
MODULE_DESCRIPTION("AS3722 pin control and GPIO driver");
MODULE_AUTHOR("Laxman Dewangan<ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
