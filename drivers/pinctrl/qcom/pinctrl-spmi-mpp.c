// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/gpio/driver.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string_choices.h>
#include <linux/types.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinmux.h>

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
#define PMIC_MPP_REG_AOUT_CTL			0x48
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

#define PMIC_MPP_SELECTOR_NORMAL		0
#define PMIC_MPP_SELECTOR_PAIRED		1
#define PMIC_MPP_SELECTOR_DTEST_FIRST		4

#define PMIC_MPP_PHYSICAL_OFFSET		1

/* Qualcomm specific pin configurations */
#define PMIC_MPP_CONF_AMUX_ROUTE		(PIN_CONFIG_END + 1)
#define PMIC_MPP_CONF_ANALOG_LEVEL		(PIN_CONFIG_END + 2)
#define PMIC_MPP_CONF_DTEST_SELECTOR		(PIN_CONFIG_END + 3)
#define PMIC_MPP_CONF_PAIRED			(PIN_CONFIG_END + 4)

/**
 * struct pmic_mpp_pad - keep current MPP settings
 * @base: Address base in SPMI device.
 * @is_enabled: Set to false when MPP should be put in high Z state.
 * @out_value: Cached pin output value.
 * @output_enabled: Set to true if MPP output logic is enabled.
 * @input_enabled: Set to true if MPP input buffer logic is enabled.
 * @paired: Pin operates in paired mode
 * @has_pullup: Pin has support to configure pullup
 * @num_sources: Number of power-sources supported by this MPP.
 * @power_source: Current power-source used.
 * @amux_input: Set the source for analog input.
 * @aout_level: Analog output level
 * @pullup: Pullup resistor value. Valid in Bidirectional mode only.
 * @function: See pmic_mpp_functions[].
 * @drive_strength: Amount of current in sink mode
 * @dtest: DTEST route selector
 */
struct pmic_mpp_pad {
	u16		base;
	bool		is_enabled;
	bool		out_value;
	bool		output_enabled;
	bool		input_enabled;
	bool		paired;
	bool		has_pullup;
	unsigned int	num_sources;
	unsigned int	power_source;
	unsigned int	amux_input;
	unsigned int	aout_level;
	unsigned int	pullup;
	unsigned int	function;
	unsigned int	drive_strength;
	unsigned int	dtest;
};

struct pmic_mpp_state {
	struct device	*dev;
	struct regmap	*map;
	struct pinctrl_dev *ctrl;
	struct gpio_chip chip;
};

static const struct pinconf_generic_params pmic_mpp_bindings[] = {
	{"qcom,amux-route",	PMIC_MPP_CONF_AMUX_ROUTE,	0},
	{"qcom,analog-level",	PMIC_MPP_CONF_ANALOG_LEVEL,	0},
	{"qcom,dtest",		PMIC_MPP_CONF_DTEST_SELECTOR,	0},
	{"qcom,paired",		PMIC_MPP_CONF_PAIRED,		0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item pmic_conf_items[] = {
	PCONFDUMP(PMIC_MPP_CONF_AMUX_ROUTE, "analog mux", NULL, true),
	PCONFDUMP(PMIC_MPP_CONF_ANALOG_LEVEL, "analog level", NULL, true),
	PCONFDUMP(PMIC_MPP_CONF_DTEST_SELECTOR, "dtest", NULL, true),
	PCONFDUMP(PMIC_MPP_CONF_PAIRED, "paired", NULL, false),
};
#endif

static const char *const pmic_mpp_groups[] = {
	"mpp1", "mpp2", "mpp3", "mpp4", "mpp5", "mpp6", "mpp7", "mpp8",
};

#define PMIC_MPP_DIGITAL	0
#define PMIC_MPP_ANALOG		1
#define PMIC_MPP_SINK		2

static const char *const pmic_mpp_functions[] = {
	"digital", "analog", "sink"
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
	.dt_free_map		= pinctrl_utils_free_map,
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
	unsigned int mode;
	unsigned int sel;
	unsigned int val;
	unsigned int en;

	switch (pad->function) {
	case PMIC_MPP_ANALOG:
		if (pad->input_enabled && pad->output_enabled)
			mode = PMIC_MPP_MODE_ANALOG_BIDIR;
		else if (pad->input_enabled)
			mode = PMIC_MPP_MODE_ANALOG_INPUT;
		else
			mode = PMIC_MPP_MODE_ANALOG_OUTPUT;
		break;
	case PMIC_MPP_DIGITAL:
		if (pad->input_enabled && pad->output_enabled)
			mode = PMIC_MPP_MODE_DIGITAL_BIDIR;
		else if (pad->input_enabled)
			mode = PMIC_MPP_MODE_DIGITAL_INPUT;
		else
			mode = PMIC_MPP_MODE_DIGITAL_OUTPUT;
		break;
	case PMIC_MPP_SINK:
	default:
		mode = PMIC_MPP_MODE_CURRENT_SINK;
		break;
	}

	if (pad->dtest)
		sel = PMIC_MPP_SELECTOR_DTEST_FIRST + pad->dtest - 1;
	else if (pad->paired)
		sel = PMIC_MPP_SELECTOR_PAIRED;
	else
		sel = PMIC_MPP_SELECTOR_NORMAL;

	en = !!pad->out_value;

	val = mode << PMIC_MPP_REG_MODE_DIR_SHIFT |
	      sel << PMIC_MPP_REG_MODE_FUNCTION_SHIFT |
	      en;

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
	if (ret < 0)
		return ret;

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
		if (pad->pullup != PMIC_MPP_PULL_UP_OPEN)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		switch (pad->pullup) {
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
		if (pad->is_enabled)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		arg = pad->power_source;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		if (!pad->input_enabled)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_LEVEL:
		arg = pad->out_value;
		break;
	case PMIC_MPP_CONF_DTEST_SELECTOR:
		arg = pad->dtest;
		break;
	case PMIC_MPP_CONF_AMUX_ROUTE:
		arg = pad->amux_input;
		break;
	case PMIC_MPP_CONF_PAIRED:
		if (!pad->paired)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = pad->drive_strength;
		break;
	case PMIC_MPP_CONF_ANALOG_LEVEL:
		arg = pad->aout_level;
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
		case PIN_CONFIG_LEVEL:
			pad->output_enabled = true;
			pad->out_value = arg;
			break;
		case PMIC_MPP_CONF_DTEST_SELECTOR:
			pad->dtest = arg;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			pad->drive_strength = arg;
			break;
		case PMIC_MPP_CONF_AMUX_ROUTE:
			if (arg >= PMIC_MPP_AMUX_ROUTE_ABUS4)
				return -EINVAL;
			pad->amux_input = arg;
			break;
		case PMIC_MPP_CONF_ANALOG_LEVEL:
			pad->aout_level = arg;
			break;
		case PMIC_MPP_CONF_PAIRED:
			pad->paired = !!arg;
			break;
		default:
			return -EINVAL;
		}
	}

	val = pad->power_source << PMIC_MPP_REG_VIN_SHIFT;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_DIG_VIN_CTL, val);
	if (ret < 0)
		return ret;

	if (pad->has_pullup) {
		val = pad->pullup << PMIC_MPP_REG_PULL_SHIFT;

		ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_DIG_PULL_CTL,
				     val);
		if (ret < 0)
			return ret;
	}

	val = pad->amux_input & PMIC_MPP_REG_AIN_ROUTE_MASK;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_AIN_CTL, val);
	if (ret < 0)
		return ret;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_AOUT_CTL, pad->aout_level);
	if (ret < 0)
		return ret;

	ret = pmic_mpp_write_mode_ctl(state, pad);
	if (ret < 0)
		return ret;

	ret = pmic_mpp_write(state, pad, PMIC_MPP_REG_SINK_CTL, pad->drive_strength);
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
		seq_printf(s, " %-7s", pmic_mpp_functions[pad->function]);
		seq_printf(s, " vin-%d", pad->power_source);
		seq_printf(s, " %d", pad->aout_level);
		if (pad->has_pullup)
			seq_printf(s, " %-8s", biases[pad->pullup]);
		seq_printf(s, " %-4s", str_high_low(pad->out_value));
		if (pad->dtest)
			seq_printf(s, " dtest%d", pad->dtest);
		if (pad->paired)
			seq_puts(s, " paired");
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
	struct pmic_mpp_state *state = gpiochip_get_data(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_INPUT_ENABLE, 1);

	return pmic_mpp_config_set(state->ctrl, pin, &config, 1);
}

static int pmic_mpp_direction_output(struct gpio_chip *chip,
				     unsigned pin, int val)
{
	struct pmic_mpp_state *state = gpiochip_get_data(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_LEVEL, val);

	return pmic_mpp_config_set(state->ctrl, pin, &config, 1);
}

static int pmic_mpp_get(struct gpio_chip *chip, unsigned pin)
{
	struct pmic_mpp_state *state = gpiochip_get_data(chip);
	struct pmic_mpp_pad *pad;
	int ret;

	pad = state->ctrl->desc->pins[pin].drv_data;

	if (pad->input_enabled) {
		ret = pmic_mpp_read(state, pad, PMIC_MPP_REG_RT_STS);
		if (ret < 0)
			return ret;

		pad->out_value = ret & PMIC_MPP_REG_RT_STS_VAL_MASK;
	}

	return !!pad->out_value;
}

static int pmic_mpp_set(struct gpio_chip *chip, unsigned int pin, int value)
{
	struct pmic_mpp_state *state = gpiochip_get_data(chip);
	unsigned long config;

	config = pinconf_to_config_packed(PIN_CONFIG_LEVEL, value);

	return pmic_mpp_config_set(state->ctrl, pin, &config, 1);
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

static void pmic_mpp_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct pmic_mpp_state *state = gpiochip_get_data(chip);
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
	.request		= gpiochip_generic_request,
	.free			= gpiochip_generic_free,
	.of_xlate		= pmic_mpp_of_xlate,
	.dbg_show		= pmic_mpp_dbg_show,
};

static int pmic_mpp_populate(struct pmic_mpp_state *state,
			     struct pmic_mpp_pad *pad)
{
	int type, subtype, val, dir;
	unsigned int sel;

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
		pad->function = PMIC_MPP_DIGITAL;
		break;
	case PMIC_MPP_MODE_DIGITAL_OUTPUT:
		pad->input_enabled = false;
		pad->output_enabled = true;
		pad->function = PMIC_MPP_DIGITAL;
		break;
	case PMIC_MPP_MODE_DIGITAL_BIDIR:
		pad->input_enabled = true;
		pad->output_enabled = true;
		pad->function = PMIC_MPP_DIGITAL;
		break;
	case PMIC_MPP_MODE_ANALOG_BIDIR:
		pad->input_enabled = true;
		pad->output_enabled = true;
		pad->function = PMIC_MPP_ANALOG;
		break;
	case PMIC_MPP_MODE_ANALOG_INPUT:
		pad->input_enabled = true;
		pad->output_enabled = false;
		pad->function = PMIC_MPP_ANALOG;
		break;
	case PMIC_MPP_MODE_ANALOG_OUTPUT:
		pad->input_enabled = false;
		pad->output_enabled = true;
		pad->function = PMIC_MPP_ANALOG;
		break;
	case PMIC_MPP_MODE_CURRENT_SINK:
		pad->input_enabled = false;
		pad->output_enabled = true;
		pad->function = PMIC_MPP_SINK;
		break;
	default:
		dev_err(state->dev, "unknown MPP direction\n");
		return -ENODEV;
	}

	sel = val >> PMIC_MPP_REG_MODE_FUNCTION_SHIFT;
	sel &= PMIC_MPP_REG_MODE_FUNCTION_MASK;

	if (sel >= PMIC_MPP_SELECTOR_DTEST_FIRST)
		pad->dtest = sel + 1;
	else if (sel == PMIC_MPP_SELECTOR_PAIRED)
		pad->paired = true;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_DIG_VIN_CTL);
	if (val < 0)
		return val;

	pad->power_source = val >> PMIC_MPP_REG_VIN_SHIFT;
	pad->power_source &= PMIC_MPP_REG_VIN_MASK;

	if (subtype != PMIC_MPP_SUBTYPE_ULT_4CH_NO_ANA_OUT &&
	    subtype != PMIC_MPP_SUBTYPE_ULT_4CH_NO_SINK) {
		val = pmic_mpp_read(state, pad, PMIC_MPP_REG_DIG_PULL_CTL);
		if (val < 0)
			return val;

		pad->pullup = val >> PMIC_MPP_REG_PULL_SHIFT;
		pad->pullup &= PMIC_MPP_REG_PULL_MASK;
		pad->has_pullup = true;
	}

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_AIN_CTL);
	if (val < 0)
		return val;

	pad->amux_input = val >> PMIC_MPP_REG_AIN_ROUTE_SHIFT;
	pad->amux_input &= PMIC_MPP_REG_AIN_ROUTE_MASK;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_SINK_CTL);
	if (val < 0)
		return val;

	pad->drive_strength = val;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_AOUT_CTL);
	if (val < 0)
		return val;

	pad->aout_level = val;

	val = pmic_mpp_read(state, pad, PMIC_MPP_REG_EN_CTL);
	if (val < 0)
		return val;

	pad->is_enabled = !!val;

	return 0;
}

static int pmic_mpp_domain_translate(struct irq_domain *domain,
				      struct irq_fwspec *fwspec,
				      unsigned long *hwirq,
				      unsigned int *type)
{
	struct pmic_mpp_state *state = container_of(domain->host_data,
						     struct pmic_mpp_state,
						     chip);

	if (fwspec->param_count != 2 ||
	    fwspec->param[0] < 1 || fwspec->param[0] > state->chip.ngpio)
		return -EINVAL;

	*hwirq = fwspec->param[0] - PMIC_MPP_PHYSICAL_OFFSET;
	*type = fwspec->param[1];

	return 0;
}

static unsigned int pmic_mpp_child_offset_to_irq(struct gpio_chip *chip,
						  unsigned int offset)
{
	return offset + PMIC_MPP_PHYSICAL_OFFSET;
}

static int pmic_mpp_child_to_parent_hwirq(struct gpio_chip *chip,
					   unsigned int child_hwirq,
					   unsigned int child_type,
					   unsigned int *parent_hwirq,
					   unsigned int *parent_type)
{
	*parent_hwirq = child_hwirq + 0xc0;
	*parent_type = child_type;

	return 0;
}

static void pmic_mpp_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	irq_chip_mask_parent(d);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void pmic_mpp_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	irq_chip_unmask_parent(d);
}

static const struct irq_chip pmic_mpp_irq_chip = {
	.name = "spmi-mpp",
	.irq_ack = irq_chip_ack_parent,
	.irq_mask = pmic_mpp_irq_mask,
	.irq_unmask = pmic_mpp_irq_unmask,
	.irq_set_type = irq_chip_set_type_parent,
	.irq_set_wake = irq_chip_set_wake_parent,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int pmic_mpp_probe(struct platform_device *pdev)
{
	struct irq_domain *parent_domain;
	struct device_node *parent_node;
	struct device *dev = &pdev->dev;
	struct pinctrl_pin_desc *pindesc;
	struct pinctrl_desc *pctrldesc;
	struct pmic_mpp_pad *pad, *pads;
	struct pmic_mpp_state *state;
	struct gpio_irq_chip *girq;
	int ret, npins, i;
	u32 reg;

	ret = of_property_read_u32(dev->of_node, "reg", &reg);
	if (ret < 0) {
		dev_err(dev, "missing base address");
		return ret;
	}

	npins = (uintptr_t) device_get_match_data(&pdev->dev);

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

		pad->base = reg + i * PMIC_MPP_ADDRESS_RANGE;

		ret = pmic_mpp_populate(state, pad);
		if (ret < 0)
			return ret;
	}

	state->chip = pmic_mpp_gpio_template;
	state->chip.parent = dev;
	state->chip.base = -1;
	state->chip.ngpio = npins;
	state->chip.label = dev_name(dev);
	state->chip.of_gpio_n_cells = 2;
	state->chip.can_sleep = false;

	state->ctrl = devm_pinctrl_register(dev, pctrldesc, state);
	if (IS_ERR(state->ctrl))
		return PTR_ERR(state->ctrl);

	parent_node = of_irq_find_parent(state->dev->of_node);
	if (!parent_node)
		return -ENXIO;

	parent_domain = irq_find_host(parent_node);
	of_node_put(parent_node);
	if (!parent_domain)
		return -ENXIO;

	girq = &state->chip.irq;
	gpio_irq_chip_set_chip(girq, &pmic_mpp_irq_chip);
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;
	girq->fwnode = dev_fwnode(state->dev);
	girq->parent_domain = parent_domain;
	girq->child_to_parent_hwirq = pmic_mpp_child_to_parent_hwirq;
	girq->populate_parent_alloc_arg = gpiochip_populate_parent_fwspec_fourcell;
	girq->child_offset_to_irq = pmic_mpp_child_offset_to_irq;
	girq->child_irq_domain_ops.translate = pmic_mpp_domain_translate;

	ret = gpiochip_add_data(&state->chip, state);
	if (ret) {
		dev_err(state->dev, "can't add gpio chip\n");
		return ret;
	}

	ret = gpiochip_add_pin_range(&state->chip, dev_name(dev), 0, 0, npins);
	if (ret) {
		dev_err(dev, "failed to add pin range\n");
		goto err_range;
	}

	return 0;

err_range:
	gpiochip_remove(&state->chip);
	return ret;
}

static void pmic_mpp_remove(struct platform_device *pdev)
{
	struct pmic_mpp_state *state = platform_get_drvdata(pdev);

	gpiochip_remove(&state->chip);
}

static const struct of_device_id pmic_mpp_of_match[] = {
	{ .compatible = "qcom,pm8019-mpp", .data = (void *) 6 },
	{ .compatible = "qcom,pm8226-mpp", .data = (void *) 8 },
	{ .compatible = "qcom,pm8841-mpp", .data = (void *) 4 },
	{ .compatible = "qcom,pm8916-mpp", .data = (void *) 4 },
	{ .compatible = "qcom,pm8937-mpp", .data = (void *) 4 },
	{ .compatible = "qcom,pm8941-mpp", .data = (void *) 8 },
	{ .compatible = "qcom,pm8950-mpp", .data = (void *) 4 },
	{ .compatible = "qcom,pmi8950-mpp", .data = (void *) 4 },
	{ .compatible = "qcom,pm8994-mpp", .data = (void *) 8 },
	{ .compatible = "qcom,pma8084-mpp", .data = (void *) 8 },
	{ .compatible = "qcom,pmi8994-mpp", .data = (void *) 4 },
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
