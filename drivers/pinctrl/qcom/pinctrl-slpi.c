// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/clk.h>

#include "../core.h"
#include "../pinctrl-utils.h"

static struct clk *clk_ssc;

/**
 * struct slpi_pin - SLPI pin definition
 * @name:	Name of the pin.
 * @ctl_reg:	Offset of the register holding control bits for this group.
 * @io_reg:	Offset of the register holding input/output bits for this group.
 * @pull_bit:	Offset in @ctl_reg for the bias configuration.
 * @mux_bit:	Offset in @ctl_reg for the pinmux function selection.
 * @drv_bit:	Offset in @ctl_reg for the drive strength configuration.
 * @oe_bit:	Offset in @ctl_reg for controlling output enable.
 * @in_bit:	Offset in @io_reg for the input bit value.
 * @out_bit:	Offset in @io_reg for the output bit value.
 */
struct slpi_pin {
	void __iomem *base;
	unsigned int offset;

	unsigned int ctl_reg;
	unsigned int io_reg;

	unsigned int pull_bit:5;
	unsigned int mux_bit:5;
	unsigned int drv_bit:5;
	unsigned int oe_bit:5;
	unsigned int in_bit:5;
	unsigned int out_bit:5;
};


/* The index of each function in slpi_pin_functions[] array */
enum slpi_pin_func_index {
	SLPI_PIN_FUNC_INDEX_GPIO	= 0x00,
	SLPI_PIN_FUNC_INDEX_FUNC1	= 0x01,
	SLPI_PIN_FUNC_INDEX_FUNC2	= 0x02,
	SLPI_PIN_FUNC_INDEX_FUNC3	= 0x03,
	SLPI_PIN_FUNC_INDEX_FUNC4	= 0x04,
	SLPI_PIN_FUNC_INDEX_FUNC5	= 0x05,
};

#define SLPI_PIN_FUNC_GPIO	"gpio"
#define SLPI_PIN_FUNC_FUNC1	"func1"
#define SLPI_PIN_FUNC_FUNC2	"func2"
#define SLPI_PIN_FUNC_FUNC3	"func3"
#define SLPI_PIN_FUNC_FUNC4	"func4"
#define SLPI_PIN_FUNC_FUNC5	"func5"

static const char *const slpi_gpio_groups[] = {
	"gpio0", "gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7",
	"gpio8", "gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14",
	"gpio15", "gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21",
	"gpio22", "gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28",
	"gpio29", "gpio30", "gpio31",
};

static const char *const slpi_pin_functions[] = {
	[SLPI_PIN_FUNC_INDEX_GPIO]	= SLPI_PIN_FUNC_GPIO,
	[SLPI_PIN_FUNC_INDEX_FUNC1]	= SLPI_PIN_FUNC_FUNC1,
	[SLPI_PIN_FUNC_INDEX_FUNC2]	= SLPI_PIN_FUNC_FUNC2,
	[SLPI_PIN_FUNC_INDEX_FUNC3]	= SLPI_PIN_FUNC_FUNC3,
	[SLPI_PIN_FUNC_INDEX_FUNC4]	= SLPI_PIN_FUNC_FUNC4,
	[SLPI_PIN_FUNC_INDEX_FUNC5]	= SLPI_PIN_FUNC_FUNC5,
};

static unsigned int slpi_read(struct slpi_pin *pin, u32 reg)
{
	return readl_relaxed(pin->base + pin->offset + reg);
}

static void  slpi_write(u32 val, struct slpi_pin *pin,  u32 reg)
{
	return writel_relaxed(val, pin->base + pin->offset + reg);
}

static int slpi_get_groups_count(struct pinctrl_dev *pctldev)
{
	/* Every PIN is a group */
	return pctldev->desc->npins;
}

static const char *slpi_get_group_name(struct pinctrl_dev *pctldev,
					unsigned int pin)
{
	return pctldev->desc->pins[pin].name;
}

static int slpi_get_group_pins(struct pinctrl_dev *pctldev,
			      unsigned int pin,
			      const unsigned int **pins,
			      unsigned int *num_pins)
{
	*pins = &pctldev->desc->pins[pin].number;
	*num_pins = 1;
	return 0;
}

static const struct pinctrl_ops slpi_pinctrl_ops = {
	.get_groups_count	= slpi_get_groups_count,
	.get_group_name		= slpi_get_group_name,
	.get_group_pins		= slpi_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int slpi_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(slpi_pin_functions);
}

static const char *slpi_get_function_name(struct pinctrl_dev *pctldev,
					 unsigned int function)
{
	return slpi_pin_functions[function];
}

static int slpi_get_function_groups(struct pinctrl_dev *pctldev,
				   unsigned int function,
				   const char * const **groups,
				   unsigned int * const num_groups)
{
	*groups = slpi_gpio_groups;
	*num_groups = pctldev->desc->npins;
	return 0;
}

static int slpi_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int function,
			      unsigned int pin_index)
{
	struct slpi_pin *pin;
	u32 val;
	int ret;

	ret = clk_prepare_enable(clk_ssc);
	if (ret)
		return ret;

	pin = pctldev->desc->pins[pin_index].drv_data;

	if (WARN_ON(function >= ARRAY_SIZE(slpi_pin_functions)))
		return -EINVAL;

	val = slpi_read(pin, pin->ctl_reg);
	val &= ~(0x7 << pin->mux_bit);
	val |= function << pin->mux_bit;
	slpi_write(val, pin, pin->ctl_reg);

	clk_disable_unprepare(clk_ssc);

	return 0;
}

static const struct pinmux_ops slpi_pinmux_ops = {
	.get_functions_count	= slpi_get_functions_count,
	.get_function_name	= slpi_get_function_name,
	.get_function_groups	= slpi_get_function_groups,
	.set_mux		= slpi_pinmux_set_mux,
};

static int slpi_config_reg(const struct slpi_pin *pin,
			  unsigned int param,
			  unsigned int *mask,
			  unsigned int *bit)
{
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_BUS_HOLD:
	case PIN_CONFIG_BIAS_PULL_UP:
		*bit = pin->pull_bit;
		*mask = 3;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		*bit = pin->drv_bit;
		*mask = 7;
		break;
	case PIN_CONFIG_OUTPUT:
	case PIN_CONFIG_INPUT_ENABLE:
		*bit = pin->oe_bit;
		*mask = 1;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

#define MSM_NO_PULL	0
#define MSM_PULL_DOWN	1
#define MSM_KEEPER	2
#define MSM_PULL_UP	3

static unsigned int slpi_regval_to_drive(u32 val)
{
	return (val + 1) * 2;
}

static int slpi_config_group_get(struct pinctrl_dev *pctldev,
				unsigned int pin_index,
				unsigned long *config)
{
	unsigned int param = pinconf_to_config_param(*config);
	struct slpi_pin *pin;
	unsigned int mask;
	unsigned int arg;
	unsigned int bit;
	int ret;
	u32 val;

	ret = clk_prepare_enable(clk_ssc);
	if (ret)
		return ret;

	pin = pctldev->desc->pins[pin_index].drv_data;
	ret = slpi_config_reg(pin, param, &mask, &bit);
	if (ret < 0)
		return ret;

	val = slpi_read(pin, pin->ctl_reg);
	arg = (val >> bit) & mask;

	/* Convert register value to pinconf value */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = arg == MSM_NO_PULL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = arg == MSM_PULL_DOWN;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		arg = arg == MSM_KEEPER;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = arg == MSM_PULL_UP;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = slpi_regval_to_drive(arg);
		break;
	case PIN_CONFIG_OUTPUT:
		/* Pin is not output */
		if (!arg)
			return -EINVAL;

		val = slpi_read(pin, pin->io_reg);
		arg = !!(val & BIT(pin->in_bit));
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		/* Pin is output */
		if (arg)
			return -EINVAL;
		arg = 1;
		break;
	default:
		return -EOPNOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	clk_disable_unprepare(clk_ssc);

	return 0;
}

static int slpi_config_group_set(struct pinctrl_dev *pctldev,
				unsigned int pin_index,
				unsigned long *configs,
				unsigned int num_configs)
{
	struct slpi_pin *pin;
	unsigned int param;
	unsigned int mask;
	unsigned int arg;
	unsigned int bit;
	int ret;
	u32 val;
	int i;

	ret = clk_prepare_enable(clk_ssc);
	if (ret)
		return ret;

	pin = pctldev->desc->pins[pin_index].drv_data;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		ret = slpi_config_reg(pin, param, &mask, &bit);
		if (ret < 0)
			return ret;

		/* Convert pinconf values to register values */
		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			arg = MSM_NO_PULL;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			arg = MSM_PULL_DOWN;
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			arg = MSM_KEEPER;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			arg = MSM_PULL_UP;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* Check for invalid values */
			if (arg > 16 || arg < 2 || (arg % 2) != 0)
				arg = -1;
			else
				arg = (arg / 2) - 1;
			break;
		case PIN_CONFIG_OUTPUT:
			/* set output value */
			val = slpi_read(pin, pin->io_reg);
			if (arg)
				val |= BIT(pin->out_bit);
			else
				val &= ~BIT(pin->out_bit);
			slpi_write(val, pin, pin->io_reg);

			/* enable output */
			arg = 1;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			/* disable output */
			arg = 0;
			break;
		default:
			dev_err(pctldev->dev, "Unsupported config parameter: %x\n",
				param);
			return -EINVAL;
		}

		/* Range-check user-supplied value */
		if (arg & ~mask) {
			dev_err(pctldev->dev, "config %x: %x is invalid\n",
								param, arg);
			return -EINVAL;
		}

		val = slpi_read(pin, pin->ctl_reg);
		val &= ~(mask << bit);
		val |= arg << bit;
		slpi_write(val, pin, pin->ctl_reg);
	}

	clk_disable_unprepare(clk_ssc);

	return 0;
}

static const struct pinconf_ops slpi_pinconf_ops = {
	.is_generic		= true,
	.pin_config_group_get	= slpi_config_group_get,
	.pin_config_group_set	= slpi_config_group_set,
};

static int slpi_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_dev *pctldev;
	struct pinctrl_pin_desc *pindesc;
	struct pinctrl_desc *pctrldesc;
	struct slpi_pin *pin, *pins;
	struct resource *res;
	int ret, npins, i;
	void __iomem *base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_ssc = devm_clk_get(dev, "ssc-clk");
	if (IS_ERR_OR_NULL(clk_ssc)) {
		dev_err(dev, "Error in getting ssc-clk\n");
		return -EPROBE_DEFER;
	}

	ret = of_property_read_u32(dev->of_node, "qcom,num-pins", &npins);
	if (ret < 0)
		return ret;

	pindesc = devm_kcalloc(dev, npins, sizeof(*pindesc), GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	WARN_ON(npins > ARRAY_SIZE(slpi_gpio_groups));

	pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pctrldesc = devm_kzalloc(dev, sizeof(*pctrldesc), GFP_KERNEL);
	if (!pctrldesc)
		return -ENOMEM;

	pctrldesc->pctlops = &slpi_pinctrl_ops;
	pctrldesc->pmxops = &slpi_pinmux_ops;
	pctrldesc->confops = &slpi_pinconf_ops;
	pctrldesc->owner = THIS_MODULE;
	pctrldesc->name = dev_name(&pdev->dev);
	pctrldesc->pins = pindesc;
	pctrldesc->npins = npins;

	for (i = 0; i < npins; i++, pindesc++) {
		pin = &pins[i];
		pindesc->drv_data = pin;
		pindesc->number = i;
		pindesc->name = slpi_gpio_groups[i];

		pin->base = base;
		pin->offset = i * 0x1000;
		pin->ctl_reg = 0x0;
		pin->io_reg = 0x4;

		pin->pull_bit = 0;
		pin->out_bit = 1;
		pin->mux_bit = 2;
		pin->oe_bit = 9;
		pin->drv_bit = 6;
		pin->in_bit = 0;
	}

	pctldev = devm_pinctrl_register(&pdev->dev, pctrldesc, NULL);
	if (IS_ERR(pctldev)) {
		dev_err(dev, "Failed to register pinctrl device\n");
		return PTR_ERR(pctldev);
	}
	return 0;
}

static const struct of_device_id slpi_pinctrl_of_match[] = {
	{ .compatible = "qcom,slpi-pinctrl" }, /* Generic */
	{ },
};

MODULE_DEVICE_TABLE(of, slpi_pinctrl_of_match);

static struct platform_driver slpi_pinctrl_driver = {
	.driver = {
		   .name = "qcom-slpi-pinctrl",
		   .of_match_table = slpi_pinctrl_of_match,
	},
	.probe = slpi_pinctrl_probe,
};

module_platform_driver(slpi_pinctrl_driver);

MODULE_DESCRIPTION("QTI SLPI GPIO pin control driver");
MODULE_LICENSE("GPL");
