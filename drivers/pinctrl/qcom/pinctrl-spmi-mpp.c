/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <dt-bindings/pinctrl/qcom,pmic-mpp.h>

#include "../core.h"
#include "../pinctrl-utils.h"

#define PMIC_MPP_ADDRESS_RANGE			0x100

/*
 * Pull Up Values - it indicates whether a pull-up should be
 * applied for bidirectional mode only. The hardware ignores the
 * configuration when operating in other modes.
 */
#define PMIC_MPP_PULL_UP_0P6KOHM		0
#define PMIC_MPP_PULL_UP_10KOHM			1
#define PMIC_MPP_PULL_UP_30KOHM			2
#define PMIC_MPP_PULL_UP_OPEN			3

/* type registers base address bases */
#define PMIC_MPP_REG_TYPE			0x4
#define PMIC_MPP_REG_SUBTYPE			0x5

/* mpp peripheral type and subtype values */
#define PMIC_MPP_TYPE				0x11
#define PMIC_MPP_SUBTYPE_4CH_NO_ANA_OUT		0x3
#define PMIC_MPP_SUBTYPE_ULT_4CH_NO_ANA_OUT	0x4
#define PMIC_MPP_SUBTYPE_4CH_NO_SINK		0x5
#define PMIC_MPP_SUBTYPE_ULT_4CH_NO_SINK	0x6
#define PMIC_MPP_SUBTYPE_4CH_FULL_FUNC		0x7
#define PMIC_MPP_SUBTYPE_8CH_FULL_FUNC		0xf

#define PMIC_MPP_REG_RT_STS			0x10
#define PMIC_MPP_REG_RT_STS_VAL_MASK		0x1

/* control register base address bases */
#define PMIC_MPP_REG_MODE_CTL			0x40
#define PMIC_MPP_REG_DIG_VIN_CTL		0x41
#define PMIC_MPP_REG_DIG_PULL_CTL		0x42
#define PMIC_MPP_REG_DIG_IN_CTL			0x43
#define PMIC_MPP_REG_EN_CTL			0x46
#define PMIC_MPP_REG_AIN_CTL			0x4a
#define PMIC_MPP_REG_SINK_CTL			0x4c

/* PMIC_MPP_REG_MODE_CTL */
#define PMIC_MPP_REG_MODE_VALUE_MASK		0x1
#define PMIC_MPP_REG_MODE_FUNCTION_SHIFT	1
#define PMIC_MPP_REG_MODE_FUNCTION_MASK		0x7
#define PMIC_MPP_REG_MODE_DIR_SHIFT		4
#define PMIC_MPP_REG_MODE_DIR_MASK		0x7

/* PMIC_MPP_REG_DIG_VIN_CTL */
#define PMIC_MPP_REG_VIN_SHIFT			0
#define PMIC_MPP_REG_VIN_MASK			0x7

/* PMIC_MPP_REG_DIG_PULL_CTL */
#define PMIC_MPP_REG_PULL_SHIFT			0
#define PMIC_MPP_REG_PULL_MASK			0x7

/* PMIC_MPP_REG_EN_CTL */
#define PMIC_MPP_REG_MASTER_EN_SHIFT		7

/* PMIC_MPP_REG_AIN_CTL */
#define PMIC_MPP_REG_AIN_ROUTE_SHIFT		0
#define PMIC_MPP_REG_AIN_ROUTE_MASK		0x7

#define PMIC_MPP_MODE_DIGITAL_INPUT		0
#define PMIC_MPP_MODE_DIGITAL_OUTPUT		1
#define PMIC_MPP_MODE_DIGITAL_BIDIR		2
#define PMIC_MPP_MODE_ANALOG_BIDIR		3
#define PMIC_MPP_MODE_ANALOG_INPUT		4
#define PMIC_MPP_MODE_ANALOG_OUTPUT		5
#define PMIC_MPP_MODE_CURRENT_SINK		6

#define PMIC_MPP_PHYSICAL_OFFSET		1

/* Qualcomm specific pin configurations */
#define PMIC_MPP_CONF_AMUX_ROUTE		(PIN_CONFIG_END + 1)
#define PMIC_MPP_CONF_ANALOG_MODE		(PIN_CONFIG_END + 2)
#define PMIC_MPP_CONF_SINK_MODE			(PIN_CONFIG_END + 3)

/**
 * struct pmic_mpp_pad - keep current MPP settings
 * @base: Address base in SPMI device.
 * @irq: IRQ number which this MPP generate.
 * @is_enabled: Set to false when MPP should be put in high Z state.
 * @out_value: Cached pin output value.
 * @output_enabled: Set to true if MPP output logic is enabled.
 * @input_enabled: Set to true if MPP input buffer logic is enabled.
 * @analog_mode: Set to true when MPP should operate in Analog Input, Analog
 *	Output or Bidirectional Analog mode.
 * @sink_mode: Boolean indicating if ink mode is slected
 * @num_sources: Number of power-sources supported by this MPP.
 * @power_source: Current power-source used.
 * @amux_input: Set the source for analog input.
 * @pullup: Pullup resistor value. Valid in Bidirectional mode only.
 * @function: See pmic_mpp_functions[].
 * @drive_strength: Amount of current in sink mode
 */
struct pmic_mpp_pad {
	u16		base;
	int		irq;
	bool		is_enabled;
	bool		out_value;
	bool		output_enabled;
	bool		input_enabled;
	bool		analog_mode;
	bool		sink_mode;
	unsigned int	num_sources;
	unsigned int	power_source;
	unsigned int	amux_input;
	unsigned int	pullup;
	unsigned int	function;
	unsigned int	drive_strength;
};

struct pmic_mpp_state {
	struct device	*dev;
	struct regmap	*map;
	struct pinctrl_dev *ctrl;
	struct gpio_chip chip;
};

static const struct pinconf_generic_params pmic_mpp_bindings[] = {
	{"qcom,amux-route",	PMIC_MPP_CONF_AMUX_ROUTE,	0},
	{"qcom,analog-mode",	PMIC_MPP_CONF_ANALOG_MODE,	0},
	{"qcom,sink-mode",	PMIC_MPP_CONF_SINK_MODE,	0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item pmic_conf_items[] = {
	PCONFDUMP(PMIC_MPP_CONF_AMUX_ROUTE, "analog mux", NULL, true),
	PCONFDUMP(PMIC_MPP_CONF_ANALOG_MODE, "analog output", NULL, false),
	PCONFDUMP(PMIC_MPP_CONF_SINK_MODE, "sink mode", NULL, false),
};
#endif

static const char *const pmic_mpp_groups[] = {
	"mpp1", "mpp2", "mpp3", "mpp4", "mpp5", "mpp6", "mpp7", "mpp8",
};

static const char *const pmic_mpp_functions[] = {
	PMIC_MPP_FUNC_NORMAL, PMIC_MPP_FUNC_PAIRED,
	"reserved1", "reserved2",
	PMIC_MPP_FUNC_DTEST1, PMIC_MPP_FUNC_DTEST2,
	PMIC_MPP_FUNC_DTEST3, PMIC_MPP_FUNC_DTEST4,
};

static inline struct pmic_mpp_state *to_mpp_state(struct gpio_chip *chip)
{
	return container_of(chip, struct pmic_mpp_state, chip);
};

static int pmic_mpp_read(struct pmic_mpp_state *state,
			 struct pmic_mpp_pad *pad, unsigned int addr)
{
	unsigned int val;
	int ret;

	ret = regmap_read(state->map, pad->base + addr, &val);
	if (ret < 0)
		dev_err(state->dev, "read 0x%x failed\n", addr);
	else
		ret = val;

	return ret;
}

static int pmic_mpp_write(struct pmic_mpp_state *state,
			  struct pmic_mpp_pad *pad, unsigned int addr,
			  unsigned int val)
{
	int ret;

	ret = regmap_write(state->map, pad->base + addr, val);
	if (ret < 0)
		dev_err(state->dev, "write 0x%x failed\n", addr);

	return ret;
}

static int pmic_mpp_get_groups_count(struct pinctrl_dev *pctldev)
{
	/* Every PIN is a group */
	return pctldev->desc->npins;
}

static const char *pmic_mpp_get_group_name(struct pinctrl_dev *pctldev,
					   unsigned pin)
{
	return pctldev->desc->pins[pin].name;
}

static int pmic_mpp_get_group_pins(struct pinctrl_dev *pctldev,
				   unsigned pin,
				   const unsigned **pins, unsigned *num_pins)
{
	*pins = &pctldev->desc->pins[pin].number;
	*num_pins = 1;
	return 0;
}

static const struct pinctrl_ops pmic_mpp_pinctrl_ops = {
	.get_groups_count	= pmic_mpp_get_groups_count,
	.get_group_name		= pmic_mpp_get_group_name,
	.get_group_pins		= pmic_mpp_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_dt_free_map,
};

static int pmic_mpp_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pmic_mpp_functions);
}

static const char *pmic_mpp_get_function_name(struct pinctrl_dev *pctldev,
					      unsigned function)
{
	return pmic_mpp_functions[function];
}

static int pmic_mpp_get_function_groups(struct pinctrl_dev *pctldev,
					unsigned function,
					const char *const **groups,
					unsigned *const num_qgroups)
{
	*groups = pmic_mpp_groups;
	*num_qgroups = pctldev->desc->npins;
	return 0;
}

static int pmic_mpp_write_mode_ctl(struct pmic_mpp_state *state,
				   struct pmic_mpp_pad *pad)
{
	unsigned int val;

	if (pad->analog_mode) {
		val = PMIC_MPP_MODE_ANALOG_INPUT;
		if (pad->output_enabled) {
			if (pad->input_enabled)
				val = PMIC_MPP_MODE_ANALOG_BIDIR;
			else
				val = PMIC_MPP_MODE_ANALOG_OUTPUT;
		}
	} else if (pad->sink_mode) {
		val = PMIC_MPP_MODE_CURRENT_SINK;
	} else {
		val = PMIC_MPP_MODE_DIGITAL_INPUT;
		if (pad->output_enabled) {
			if (pad->input_enabled)
				val = PMIC_MPP_MODE_DIGITAL_BIDIR;
			else
				val = PMIC_MPP_MODE_DIGITAL_OUTPUT;
		}
	}

	val = val << PMIC_MPP_REG_MODE_DIR_SHIFT;
	val |= pad->function << PMIC_MPP_REG_MODE_FUNCTION_SHIFT;
	val |= pad->out_value & PMIC_MPP_REG_MODE_VALUE_MASK;

	return pmic_mpp_write(state, pad, PMIC_MPP_REG_MODE_CTL, val);
}

static int pmic_mpp_set_mux(struct pinctrl_dev *pctldev, unsigned function,
				unsigned pin)
{
	struct pmic_mpp_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct pmic_mpp_pad *pad;
	unsigned int val;
	int ret;

	pad = pctldev->desc->pins[pin].drv_data;

	pad->function = function;

	ret = pmic_mpp_write_mode_ctl(state, pad);

	val = pad->is_enabled << PMIC_MPP_REG_MASTER_EN_SHIFT;

	return pmic_mpp_write(state, pad, PMIC_MPP_REG_EN_CTL, val);
}

static const struct pinmux_ops pmic_mpp_pinmux_ops = {
	.get_functions_count	= pmic_mpp_get_functions_count,
	.get_function_name	= pmic_mpp_get_function_name,
	.get_function_groups	= pmic_mpp_get_function_groups,
	.set_mux		= pmic_mpp_set_mux,
};

static int pmic_mpp_config_get(struct pinctrl_dev *pctldev,
			       unsigned int pin, unsigned long *config)
{
	unsigned param = pinconf_to_config_param(*config);
	struct pmic_mpp_pad *pad;
	unsigned arg = 0;

	pad = pctldev->desc->pins[pin].drv_data;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		arg = pad->pullup == PMIC_MPP_PULL_UP_OPEN;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		switch (pad->pullup) {
		case PMIC_MPP_PULL_UP_OPEN:
			arg = 0;
			break;
		case PMIC_MPP_PULL_UP_0P6KOHM:
			arg = 600;
			break;
		case PMIC_MPP_PULL_UP_10KOHM:
			arg = 10000;
			break;
		case PMIC_MPP_PULL_UP_30KOHM:
			arg = 30000;
			break;
		default:
			return -EINVAL;
		}
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		arg = !pad->is_enabled;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		arg = pad->power_source;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		arg = pad->input_enabled;
		break;
	case PIN_CONFIG_OUTPUT:
		arg = pad->out_value;
		break;
	case PMIC_MPP_CONF_AMUX_ROUTE:
		arg = pad->amux_input;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = pad->drive_strength;
		break;
	case PMIC_MPP_CONF_ANALOG_MODE:
		arg = pad->analog_mode;
		break;
	case PMIC_MPP_CONF_SINK_MODE:
		arg = pad->sink_mode;
		break;
	default:
		return -EINVAL;
	}

	/* Convert register value to pinconf value */
	*config = pinconf_to_config_packed(param, arg);
	return 0;
}

static int pmic_mpp_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
			       unsigned long *configs, unsigned nconfs)
{
	struct pmic_mpp_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct pmic_mpp_pad *pad;
	unsigned param, arg;
	unsigned int val;
	int i, ret;

	pad = pctldev->desc->pins[pin].drv_data;

	/* Make it possible to enable the pin, by not setting high impedance */
	pad->is_enabled = true;

	for (i = 0; i < nconfs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			pad->pullup = PMIC_MPP_PULL_UP_OPEN;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			switch (arg) {
			case 600:
				pad->pullup = PMIC_MPP_PULL_UP_0P6KOHM;
				break;
			case 10000:
				pad->pullup = PMIC_MPP_PULL_UP_10KOHM;
				break;
			case 30000:
				pad->pullup = PMIC_MPP_PULL_UP_30KOHM;
				break;
			default:
				return -EINVAL;
			}
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			pad->is_enabled = false;
			break;
		case PIN_CONFIG_POWER_SOURCE:
			if (arg >= pad->num_sources)
				return -EINVAL;
			pad->power_source = arg;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			pad->input_enabled = arg ? true : false;
			break;
		case PIN_CONFIG_OUTPUT:
			pad->output_enabled = true;
			pad->out_value = arg;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			arg = pad->drive_strength;
			break;
		case PMIC_MPP_CONF_AMUX_ROUTE:
			if (arg >= PMIC_MPP_AMUX_ROUTE_ABUS4)
				return -EINVAL;
			pad->amux_input = arg;
			break;
		case PMIC_MPP_CONF_ANALOG_MODE:
			pad->analog_mode = !!arg;
			break;
		case PMIC_MPP_CONF_SINK_MODE:
			pad->sink_mode = !!arg;
			break;
		default:
			return -EINVAL;
		}
	}

	val = pad->power_source << PMIC_MPP_REG_VIN_SHIFT;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_DIG_VIN_CTL, val);
	if (ret < 0)
		return ret;

	val = pad->pullup << PMIC_MPP_REG_PULL_SHIFT;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_DIG_PULL_CTL, val);
	if (ret < 0)
		return ret;

	val = pad->amux_input & PMIC_MPP_REG_AIN_ROUTE_MASK;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_AIN_CTL, val);
	if (ret < 0)
		return ret;

	ret = pmic_mpp_write_mode_ctl(state, pad);
	if (ret < 0)
		return ret;

	val = pad->is_enabled << PMIC_MPP_REG_MASTER_EN_SHIFT;

	return pmic_mpp_write(state, pad, PMIC_MPP_REG_EN_CTL, val);
}

static void pmic_mpp_config_dbg_show(struct pinctrl_dev *pctldev,
				     struct seq_file *s, unsigned pin)
{
	struct pmic_mpp_state *state = pinctrl_dev_get_drvdata(pctldev);
	struct pmic_mpp_pad *pad;
	int ret;

	static const char *const biases[] = {
		"0.6kOhm", "10kOhm", "30kOhm", "Disabled"
	};

	static const char *const modes[] = {
		"digital", "analog", "sink"
	};

	pad = pctldev->desc->pins[pin].drv_data;

	seq_printf(s, " mpp%-2d:", pin + PMIC_MPP_PHYSICAL_OFFSET);

	if (!pad->is_enabled) {
		seq_puts(s, " ---");
	} else {

		if (pad->input_enabled) {
			ret = pmic_mpp_read(state, pad, PMIC_MPP_REG_RT_STS);
			if (ret < 0)
				return;

			ret &= PMIC_MPP_REG_RT_STS_VAL_MASK;
			pad->out_value = ret;
		}

		seq_printf(s, " %-4s", pad->output_enabled ? "out" : "in");
		seq_printf(s, " %-7s", modes[pad->analog_mode ? 1 : (pad->sink_mode ? 2 : 0)]);
		seq_printf(s, " %-7s", pmic_mpp_functions[pad->function]);
		seq_printf(s, " vin-%d", pad->power_source);
		seq_printf(s, " %-8s", biases[pad->pullup]);
		seq_printf(s, " %-4s", pad->out_value ? "high" : "low");
	}
}

static const struct pinconf_ops pmic_mpp_pinconf_ops = {
	.is_generic = true,
	.pin_config_group_get		= pmic_mpp_config_get,
	.pin_config_group_set		= pmic_mpp_config_set,
	.pin_config_group_dbg_show	= pmic_mpp_config_dbg_show,
};

static int pmic_mpp_direction_input(struct gpio_chip *chip, unsigned pin)
{
	struct pmic_mpp_state *state = to_mpp_state(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_INPUT_ENABLE, 1);

	return pmic_mpp_config_set(state->ctrl, pin, &config, 1);
}

static int pmic_mpp_direction_output(struct gpio_chip *chip,
				     unsigned pin, int val)
{
	struct pmic_mpp_state *state = to_mpp_state(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_OUTPUT, val);

	return pmic_mpp_config_set(state->ctrl, pin, &config, 1);
}

static int pmic_mpp_get(struct gpio_chip *chip, unsigned pin)
{
	struct pmic_mpp_state *state = to_mpp_state(chip);
	struct pmic_mpp_pad *pad;
	int ret;

	pad = state->ctrl->desc->pins[pin].drv_data;

	if (pad->input_enabled) {
		ret = pmic_mpp_read(state, pad, PMIC_MPP_REG_RT_STS);
		if (ret < 0)
			return ret;

		pad->out_value = ret & PMIC_MPP_REG_RT_STS_VAL_MASK;
	}

	return pad->out_value;
}

static void pmic_mpp_set(struct gpio_chip *chip, unsigned pin, int value)
{
	struct pmic_mpp_state *state = to_mpp_state(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_OUTPUT, value);

	pmic_mpp_config_set(state->ctrl, pin, &config, 1);
}

static int pmic_mpp_request(struct gpio_chip *chip, unsigned base)
{
	return pinctrl_request_gpio(chip->base + base);
}

static void pmic_mpp_free(struct gpio_chip *chip, unsigned base)
{
	pinctrl_free_gpio(chip->base + base);
}

static int pmic_mpp_of_xlate(struct gpio_chip *chip,
			     const struct of_phandle_args *gpio_desc,
			     u32 *flags)
{
	if (chip->of_gpio_n_cells < 2)
		return -EINVAL;

	if (flags)
		*flags = gpio_desc->args[1];

	return gpio_desc->args[0] - PMIC_MPP_PHYSICAL_OFFSET;
}

static int pmic_mpp_to_irq(struct gpio_chip *chip, unsigned pin)
{
	struct pmic_mpp_state *state = to_mpp_state(chip);
	struct pmic_mpp_pad *pad;

	pad = state->ctrl->desc->pins[pin].drv_data;

	return pad->irq;
}

static void pmic_mpp_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct pmic_mpp_state *state = to_mpp_state(chip);
	unsigned i;

	for (i = 0; i < chip->ngpio; i++) {
		pmic_mpp_config_dbg_show(state->ctrl, s, i);
		seq_puts(s, "\n");
	}
}

static const struct gpio_chip pmic_mpp_gpio_template = {
	.direction_input	= pmic_mpp_direction_input,
	.direction_output	= pmic_mpp_direction_output,
	.get			= pmic_mpp_get,
	.set			= pmic_mpp_set,
	.request		= pmic_mpp_request,
	.free			= pmic_mpp_free,
	.of_xlate		= pmic_mpp_of_xlate,
	.to_irq			= pmic_mpp_to_irq,
	.dbg_show		= pmic_mpp_dbg_show,
};

static int pmic_mpp_populate(struct pmic_mpp_state *state,
			     struct pmic_mpp_pad *pad)
{
	int type, subtype, val, dir;

	type = pmic_mpp_read(state, pad, PMIC_MPP_REG_TYPE);
	if (type < 0)
		return type;

	if (type != PMIC_MPP_TYPE) {
		dev_err(state->dev, "incorrect block type 0x%x at 0x%x\n",
			type, pad->base);
		return -ENODEV;
	}

	subtype = pmic_mpp_read(state, pad, PMIC_MPP_REG_SUBTYPE);
	if (subtype < 0)
		return subtype;

	switch (subtype) {
	case PMIC_MPP_SUBTYPE_4CH_NO_ANA_OUT:
	case PMIC_MPP_SUBTYPE_ULT_4CH_NO_ANA_OUT:
	case PMIC_MPP_SUBTYPE_4CH_NO_SINK:
	case PMIC_MPP_SUBTYPE_ULT_4CH_NO_SINK:
	case PMIC_MPP_SUBTYPE_4CH_FULL_FUNC:
		pad->num_sources = 4;
		break;
	case PMIC_MPP_SUBTYPE_8CH_FULL_FUNC:
		pad->num_sources = 8;
		break;
	default:
		dev_err(state->dev, "unknown MPP type 0x%x at 0x%x\n",
			subtype, pad->base);
		return -ENODEV;
	}

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_MODE_CTL);
	if (val < 0)
		return val;

	pad->out_value = val & PMIC_MPP_REG_MODE_VALUE_MASK;

	dir = val >> PMIC_MPP_REG_MODE_DIR_SHIFT;
	dir &= PMIC_MPP_REG_MODE_DIR_MASK;

	switch (dir) {
	case PMIC_MPP_MODE_DIGITAL_INPUT:
		pad->input_enabled = true;
		pad->output_enabled = false;
		pad->analog_mode = false;
		pad->sink_mode = false;
		break;
	case PMIC_MPP_MODE_DIGITAL_OUTPUT:
		pad->input_enabled = false;
		pad->output_enabled = true;
		pad->analog_mode = false;
		pad->sink_mode = false;
		break;
	case PMIC_MPP_MODE_DIGITAL_BIDIR:
		pad->input_enabled = true;
		pad->output_enabled = true;
		pad->analog_mode = false;
		pad->sink_mode = false;
		break;
	case PMIC_MPP_MODE_ANALOG_BIDIR:
		pad->input_enabled = true;
		pad->output_enabled = true;
		pad->analog_mode = true;
		pad->sink_mode = false;
		break;
	case PMIC_MPP_MODE_ANALOG_INPUT:
		pad->input_enabled = true;
		pad->output_enabled = false;
		pad->analog_mode = true;
		pad->sink_mode = false;
		break;
	case PMIC_MPP_MODE_ANALOG_OUTPUT:
		pad->input_enabled = false;
		pad->output_enabled = true;
		pad->analog_mode = true;
		pad->sink_mode = false;
		break;
	case PMIC_MPP_MODE_CURRENT_SINK:
		pad->input_enabled = false;
		pad->output_enabled = true;
		pad->analog_mode = false;
		pad->sink_mode = true;
		break;
	default:
		dev_err(state->dev, "unknown MPP direction\n");
		return -ENODEV;
	}

	pad->function = val >> PMIC_MPP_REG_MODE_FUNCTION_SHIFT;
	pad->function &= PMIC_MPP_REG_MODE_FUNCTION_MASK;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_DIG_VIN_CTL);
	if (val < 0)
		return val;

	pad->power_source = val >> PMIC_MPP_REG_VIN_SHIFT;
	pad->power_source &= PMIC_MPP_REG_VIN_MASK;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_DIG_PULL_CTL);
	if (val < 0)
		return val;

	pad->pullup = val >> PMIC_MPP_REG_PULL_SHIFT;
	pad->pullup &= PMIC_MPP_REG_PULL_MASK;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_AIN_CTL);
	if (val < 0)
		return val;

	pad->amux_input = val >> PMIC_MPP_REG_AIN_ROUTE_SHIFT;
	pad->amux_input &= PMIC_MPP_REG_AIN_ROUTE_MASK;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_SINK_CTL);
	if (val < 0)
		return val;

	pad->drive_strength = val;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_EN_CTL);
	if (val < 0)
		return val;

	pad->is_enabled = !!val;

	return 0;
}

static int pmic_mpp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pinctrl_pin_desc *pindesc;
	struct pinctrl_desc *pctrldesc;
	struct pmic_mpp_pad *pad, *pads;
	struct pmic_mpp_state *state;
	int ret, npins, i;
	u32 res[2];

	ret = of_property_read_u32_array(dev->of_node, "reg", res, 2);
	if (ret < 0) {
		dev_err(dev, "missing base address and/or range");
		return ret;
	}

	npins = res[1] / PMIC_MPP_ADDRESS_RANGE;
	if (!npins)
		return -EINVAL;

	BUG_ON(npins > ARRAY_SIZE(pmic_mpp_groups));

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	platform_set_drvdata(pdev, state);

	state->dev = &pdev->dev;
	state->map = dev_get_regmap(dev->parent, NULL);

	pindesc = devm_kcalloc(dev, npins, sizeof(*pindesc), GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	pads = devm_kcalloc(dev, npins, sizeof(*pads), GFP_KERNEL);
	if (!pads)
		return -ENOMEM;

	pctrldesc = devm_kzalloc(dev, sizeof(*pctrldesc), GFP_KERNEL);
	if (!pctrldesc)
		return -ENOMEM;

	pctrldesc->pctlops = &pmic_mpp_pinctrl_ops;
	pctrldesc->pmxops = &pmic_mpp_pinmux_ops;
	pctrldesc->confops = &pmic_mpp_pinconf_ops;
	pctrldesc->owner = THIS_MODULE;
	pctrldesc->name = dev_name(dev);
	pctrldesc->pins = pindesc;
	pctrldesc->npins = npins;

	pctrldesc->num_custom_params = ARRAY_SIZE(pmic_mpp_bindings);
	pctrldesc->custom_params = pmic_mpp_bindings;
#ifdef CONFIG_DEBUG_FS
	pctrldesc->custom_conf_items = pmic_conf_items;
#endif

	for (i = 0; i < npins; i++, pindesc++) {
		pad = &pads[i];
		pindesc->drv_data = pad;
		pindesc->number = i;
		pindesc->name = pmic_mpp_groups[i];

		pad->irq = platform_get_irq(pdev, i);
		if (pad->irq < 0)
			return pad->irq;

		pad->base = res[0] + i * PMIC_MPP_ADDRESS_RANGE;

		ret = pmic_mpp_populate(state, pad);
		if (ret < 0)
			return ret;
	}

	state->chip = pmic_mpp_gpio_template;
	state->chip.dev = dev;
	state->chip.base = -1;
	state->chip.ngpio = npins;
	state->chip.label = dev_name(dev);
	state->chip.of_gpio_n_cells = 2;
	state->chip.can_sleep = false;

	state->ctrl = pinctrl_register(pctrldesc, dev, state);
	if (IS_ERR(state->ctrl))
		return PTR_ERR(state->ctrl);

	ret = gpiochip_add(&state->chip);
	if (ret) {
		dev_err(state->dev, "can't add gpio chip\n");
		goto err_chip;
	}

	ret = gpiochip_add_pin_range(&state->chip, dev_name(dev), 0, 0, npins);
	if (ret) {
		dev_err(dev, "failed to add pin range\n");
		goto err_range;
	}

	return 0;

err_range:
	gpiochip_remove(&state->chip);
err_chip:
	pinctrl_unregister(state->ctrl);
	return ret;
}

static int pmic_mpp_remove(struct platform_device *pdev)
{
	struct pmic_mpp_state *state = platform_get_drvdata(pdev);

	gpiochip_remove(&state->chip);
	pinctrl_unregister(state->ctrl);
	return 0;
}

static const struct of_device_id pmic_mpp_of_match[] = {
	{ .compatible = "qcom,pm8841-mpp" },	/* 4 MPP's */
	{ .compatible = "qcom,pm8916-mpp" },	/* 4 MPP's */
	{ .compatible = "qcom,pm8941-mpp" },	/* 8 MPP's */
	{ .compatible = "qcom,pma8084-mpp" },	/* 8 MPP's */
	{ },
};

MODULE_DEVICE_TABLE(of, pmic_mpp_of_match);

static struct platform_driver pmic_mpp_driver = {
	.driver = {
		   .name = "qcom-spmi-mpp",
		   .of_match_table = pmic_mpp_of_match,
	},
	.probe	= pmic_mpp_probe,
	.remove = pmic_mpp_remove,
};

module_platform_driver(pmic_mpp_driver);

MODULE_AUTHOR("Ivan T. Ivanov <iivanov@mm-sol.com>");
MODULE_DESCRIPTION("Qualcomm SPMI PMIC MPP pin control driver");
MODULE_ALIAS("platform:qcom-spmi-mpp");
MODULE_LICENSE("GPL v2");
