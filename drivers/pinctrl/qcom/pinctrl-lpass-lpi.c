// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include "../pinctrl-utils.h"
#include "pinctrl-lpass-lpi.h"

#define MAX_LPI_NUM_CLKS	2

struct lpi_pinctrl {
	struct device *dev;
	struct pinctrl_dev *ctrl;
	struct gpio_chip chip;
	struct pinctrl_desc desc;
	char __iomem *tlmm_base;
	char __iomem *slew_base;
	struct clk_bulk_data clks[MAX_LPI_NUM_CLKS];
	/* Protects from concurrent register updates */
	struct mutex lock;
	const struct lpi_pinctrl_variant_data *data;
};

static int lpi_gpio_read(struct lpi_pinctrl *state, unsigned int pin,
			 unsigned int addr)
{
	return ioread32(state->tlmm_base + LPI_TLMM_REG_OFFSET * pin + addr);
}

static int lpi_gpio_write(struct lpi_pinctrl *state, unsigned int pin,
			  unsigned int addr, unsigned int val)
{
	iowrite32(val, state->tlmm_base + LPI_TLMM_REG_OFFSET * pin + addr);

	return 0;
}

static const struct pinctrl_ops lpi_gpio_pinctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int lpi_gpio_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct lpi_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->data->nfunctions;
}

static const char *lpi_gpio_get_function_name(struct pinctrl_dev *pctldev,
					      unsigned int function)
{
	struct lpi_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->data->functions[function].name;
}

static int lpi_gpio_get_function_groups(struct pinctrl_dev *pctldev,
					unsigned int function,
					const char *const **groups,
					unsigned *const num_qgroups)
{
	struct lpi_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctrl->data->functions[function].groups;
	*num_qgroups = pctrl->data->functions[function].ngroups;

	return 0;
}

static int lpi_gpio_set_mux(struct pinctrl_dev *pctldev, unsigned int function,
			    unsigned int group_num)
{
	struct lpi_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct lpi_pingroup *g = &pctrl->data->groups[group_num];
	u32 val;
	int i, pin = g->pin;

	for (i = 0; i < g->nfuncs; i++) {
		if (g->funcs[i] == function)
			break;
	}

	if (WARN_ON(i == g->nfuncs))
		return -EINVAL;

	mutex_lock(&pctrl->lock);
	val = lpi_gpio_read(pctrl, pin, LPI_GPIO_CFG_REG);
	u32p_replace_bits(&val, i, LPI_GPIO_FUNCTION_MASK);
	lpi_gpio_write(pctrl, pin, LPI_GPIO_CFG_REG, val);
	mutex_unlock(&pctrl->lock);

	return 0;
}

static const struct pinmux_ops lpi_gpio_pinmux_ops = {
	.get_functions_count	= lpi_gpio_get_functions_count,
	.get_function_name	= lpi_gpio_get_function_name,
	.get_function_groups	= lpi_gpio_get_function_groups,
	.set_mux		= lpi_gpio_set_mux,
};

static int lpi_config_get(struct pinctrl_dev *pctldev,
			  unsigned int pin, unsigned long *config)
{
	unsigned int param = pinconf_to_config_param(*config);
	struct lpi_pinctrl *state = dev_get_drvdata(pctldev->dev);
	unsigned int arg = 0;
	int is_out;
	int pull;
	u32 ctl_reg;

	ctl_reg = lpi_gpio_read(state, pin, LPI_GPIO_CFG_REG);
	is_out = ctl_reg & LPI_GPIO_OE_MASK;
	pull = FIELD_GET(LPI_GPIO_PULL_MASK, ctl_reg);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pull == LPI_GPIO_BIAS_DISABLE)
			arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (pull == LPI_GPIO_PULL_DOWN)
			arg = 1;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (pull == LPI_GPIO_KEEPER)
			arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pull == LPI_GPIO_PULL_UP)
			arg = 1;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_OUTPUT:
		if (is_out)
			arg = 1;
		break;
	default:
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int lpi_config_set(struct pinctrl_dev *pctldev, unsigned int group,
			  unsigned long *configs, unsigned int nconfs)
{
	struct lpi_pinctrl *pctrl = dev_get_drvdata(pctldev->dev);
	unsigned int param, arg, pullup = LPI_GPIO_BIAS_DISABLE, strength = 2;
	bool value, output_enabled = false;
	const struct lpi_pingroup *g;
	unsigned long sval;
	int i, slew_offset;
	u32 val;

	g = &pctrl->data->groups[group];
	for (i = 0; i < nconfs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			pullup = LPI_GPIO_BIAS_DISABLE;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			pullup = LPI_GPIO_PULL_DOWN;
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			pullup = LPI_GPIO_KEEPER;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			pullup = LPI_GPIO_PULL_UP;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			output_enabled = false;
			break;
		case PIN_CONFIG_OUTPUT:
			output_enabled = true;
			value = arg;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			strength = arg;
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (arg > LPI_SLEW_RATE_MAX) {
				dev_err(pctldev->dev, "invalid slew rate %u for pin: %d\n",
					arg, group);
				return -EINVAL;
			}

			slew_offset = g->slew_offset;
			if (slew_offset == LPI_NO_SLEW)
				break;

			mutex_lock(&pctrl->lock);

			sval = ioread32(pctrl->slew_base + LPI_SLEW_RATE_CTL_REG);
			sval &= ~(LPI_SLEW_RATE_MASK << slew_offset);
			sval |= arg << slew_offset;
			iowrite32(sval, pctrl->slew_base + LPI_SLEW_RATE_CTL_REG);

			mutex_unlock(&pctrl->lock);
			break;
		default:
			return -EINVAL;
		}
	}

	/*
	 * As per Hardware Programming Guide, when configuring pin as output,
	 * set the pin value before setting output-enable (OE).
	 */
	if (output_enabled) {
		val = u32_encode_bits(value ? 1 : 0, LPI_GPIO_VALUE_OUT_MASK);
		lpi_gpio_write(pctrl, group, LPI_GPIO_VALUE_REG, val);
	}

	mutex_lock(&pctrl->lock);
	val = lpi_gpio_read(pctrl, group, LPI_GPIO_CFG_REG);

	u32p_replace_bits(&val, pullup, LPI_GPIO_PULL_MASK);
	u32p_replace_bits(&val, LPI_GPIO_DS_TO_VAL(strength),
			  LPI_GPIO_OUT_STRENGTH_MASK);
	u32p_replace_bits(&val, output_enabled, LPI_GPIO_OE_MASK);

	lpi_gpio_write(pctrl, group, LPI_GPIO_CFG_REG, val);
	mutex_unlock(&pctrl->lock);

	return 0;
}

static const struct pinconf_ops lpi_gpio_pinconf_ops = {
	.is_generic			= true,
	.pin_config_group_get		= lpi_config_get,
	.pin_config_group_set		= lpi_config_set,
};

static int lpi_gpio_direction_input(struct gpio_chip *chip, unsigned int pin)
{
	struct lpi_pinctrl *state = gpiochip_get_data(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_INPUT_ENABLE, 1);

	return lpi_config_set(state->ctrl, pin, &config, 1);
}

static int lpi_gpio_direction_output(struct gpio_chip *chip,
				     unsigned int pin, int val)
{
	struct lpi_pinctrl *state = gpiochip_get_data(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_OUTPUT, val);

	return lpi_config_set(state->ctrl, pin, &config, 1);
}

static int lpi_gpio_get(struct gpio_chip *chip, unsigned int pin)
{
	struct lpi_pinctrl *state = gpiochip_get_data(chip);

	return lpi_gpio_read(state, pin, LPI_GPIO_VALUE_REG) &
		LPI_GPIO_VALUE_IN_MASK;
}

static void lpi_gpio_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	struct lpi_pinctrl *state = gpiochip_get_data(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_OUTPUT, value);

	lpi_config_set(state->ctrl, pin, &config, 1);
}

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static unsigned int lpi_regval_to_drive(u32 val)
{
	return (val + 1) * 2;
}

static void lpi_gpio_dbg_show_one(struct seq_file *s,
				  struct pinctrl_dev *pctldev,
				  struct gpio_chip *chip,
				  unsigned int offset,
				  unsigned int gpio)
{
	struct lpi_pinctrl *state = gpiochip_get_data(chip);
	struct pinctrl_pin_desc pindesc;
	unsigned int func;
	int is_out;
	int drive;
	int pull;
	u32 ctl_reg;

	static const char * const pulls[] = {
		"no pull",
		"pull down",
		"keeper",
		"pull up"
	};

	pctldev = pctldev ? : state->ctrl;
	pindesc = pctldev->desc->pins[offset];
	ctl_reg = lpi_gpio_read(state, offset, LPI_GPIO_CFG_REG);
	is_out = ctl_reg & LPI_GPIO_OE_MASK;

	func = FIELD_GET(LPI_GPIO_FUNCTION_MASK, ctl_reg);
	drive = FIELD_GET(LPI_GPIO_OUT_STRENGTH_MASK, ctl_reg);
	pull = FIELD_GET(LPI_GPIO_PULL_MASK, ctl_reg);

	seq_printf(s, " %-8s: %-3s %d", pindesc.name, is_out ? "out" : "in", func);
	seq_printf(s, " %dmA", lpi_regval_to_drive(drive));
	seq_printf(s, " %s", pulls[pull]);
}

static void lpi_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned int gpio = chip->base;
	unsigned int i;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		lpi_gpio_dbg_show_one(s, NULL, chip, i, gpio);
		seq_puts(s, "\n");
	}
}

#else
#define lpi_gpio_dbg_show NULL
#endif

static const struct gpio_chip lpi_gpio_template = {
	.direction_input	= lpi_gpio_direction_input,
	.direction_output	= lpi_gpio_direction_output,
	.get			= lpi_gpio_get,
	.set			= lpi_gpio_set,
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.dbg_show		= lpi_gpio_dbg_show,
};

static int lpi_build_pin_desc_groups(struct lpi_pinctrl *pctrl)
{
	int i, ret;

	for (i = 0; i < pctrl->data->npins; i++) {
		const struct pinctrl_pin_desc *pin_info = pctrl->desc.pins + i;

		ret = pinctrl_generic_add_group(pctrl->ctrl, pin_info->name,
						  (int *)&pin_info->number, 1, NULL);
		if (ret < 0)
			goto err_pinctrl;
	}

	return 0;

err_pinctrl:
	for (; i > 0; i--)
		pinctrl_generic_remove_group(pctrl->ctrl, i - 1);

	return ret;
}

int lpi_pinctrl_probe(struct platform_device *pdev)
{
	const struct lpi_pinctrl_variant_data *data;
	struct device *dev = &pdev->dev;
	struct lpi_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	platform_set_drvdata(pdev, pctrl);

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	pctrl->data = data;
	pctrl->dev = &pdev->dev;

	pctrl->clks[0].id = "core";
	pctrl->clks[1].id = "audio";

	pctrl->tlmm_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->tlmm_base))
		return dev_err_probe(dev, PTR_ERR(pctrl->tlmm_base),
				     "TLMM resource not provided\n");

	pctrl->slew_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(pctrl->slew_base))
		return dev_err_probe(dev, PTR_ERR(pctrl->slew_base),
				     "Slew resource not provided\n");

	if (of_property_read_bool(dev->of_node, "qcom,adsp-bypass-mode"))
		ret = devm_clk_bulk_get_optional(dev, MAX_LPI_NUM_CLKS, pctrl->clks);
	else
		ret = devm_clk_bulk_get(dev, MAX_LPI_NUM_CLKS, pctrl->clks);

	if (ret)
		return ret;

	ret = clk_bulk_prepare_enable(MAX_LPI_NUM_CLKS, pctrl->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Can't enable clocks\n");

	pctrl->desc.pctlops = &lpi_gpio_pinctrl_ops;
	pctrl->desc.pmxops = &lpi_gpio_pinmux_ops;
	pctrl->desc.confops = &lpi_gpio_pinconf_ops;
	pctrl->desc.owner = THIS_MODULE;
	pctrl->desc.name = dev_name(dev);
	pctrl->desc.pins = data->pins;
	pctrl->desc.npins = data->npins;
	pctrl->chip = lpi_gpio_template;
	pctrl->chip.parent = dev;
	pctrl->chip.base = -1;
	pctrl->chip.ngpio = data->npins;
	pctrl->chip.label = dev_name(dev);
	pctrl->chip.of_gpio_n_cells = 2;
	pctrl->chip.can_sleep = false;

	mutex_init(&pctrl->lock);

	pctrl->ctrl = devm_pinctrl_register(dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->ctrl)) {
		ret = PTR_ERR(pctrl->ctrl);
		dev_err(dev, "failed to add pin controller\n");
		goto err_pinctrl;
	}

	ret = lpi_build_pin_desc_groups(pctrl);
	if (ret)
		goto err_pinctrl;

	ret = devm_gpiochip_add_data(dev, &pctrl->chip, pctrl);
	if (ret) {
		dev_err(pctrl->dev, "can't add gpio chip\n");
		goto err_pinctrl;
	}

	return 0;

err_pinctrl:
	mutex_destroy(&pctrl->lock);
	clk_bulk_disable_unprepare(MAX_LPI_NUM_CLKS, pctrl->clks);

	return ret;
}
EXPORT_SYMBOL_GPL(lpi_pinctrl_probe);

int lpi_pinctrl_remove(struct platform_device *pdev)
{
	struct lpi_pinctrl *pctrl = platform_get_drvdata(pdev);
	int i;

	mutex_destroy(&pctrl->lock);
	clk_bulk_disable_unprepare(MAX_LPI_NUM_CLKS, pctrl->clks);

	for (i = 0; i < pctrl->data->npins; i++)
		pinctrl_generic_remove_group(pctrl->ctrl, i);

	return 0;
}
EXPORT_SYMBOL_GPL(lpi_pinctrl_remove);

MODULE_DESCRIPTION("QTI LPI GPIO pin control driver");
MODULE_LICENSE("GPL");
