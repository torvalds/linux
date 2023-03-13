// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/qcom,pmic-mpp.h>

#include "../core.h"
#include "../pinctrl-utils.h"

/* MPP registers */
#define SSBI_REG_ADDR_MPP_BASE		0x50
#define SSBI_REG_ADDR_MPP(n)		(SSBI_REG_ADDR_MPP_BASE + n)

/* MPP Type: type */
#define PM8XXX_MPP_TYPE_D_INPUT         0
#define PM8XXX_MPP_TYPE_D_OUTPUT        1
#define PM8XXX_MPP_TYPE_D_BI_DIR        2
#define PM8XXX_MPP_TYPE_A_INPUT         3
#define PM8XXX_MPP_TYPE_A_OUTPUT        4
#define PM8XXX_MPP_TYPE_SINK            5
#define PM8XXX_MPP_TYPE_DTEST_SINK      6
#define PM8XXX_MPP_TYPE_DTEST_OUTPUT    7

/* Digital Input: control */
#define PM8XXX_MPP_DIN_TO_INT           0
#define PM8XXX_MPP_DIN_TO_DBUS1         1
#define PM8XXX_MPP_DIN_TO_DBUS2         2
#define PM8XXX_MPP_DIN_TO_DBUS3         3

/* Digital Output: control */
#define PM8XXX_MPP_DOUT_CTRL_LOW        0
#define PM8XXX_MPP_DOUT_CTRL_HIGH       1
#define PM8XXX_MPP_DOUT_CTRL_MPP        2
#define PM8XXX_MPP_DOUT_CTRL_INV_MPP    3

/* Bidirectional: control */
#define PM8XXX_MPP_BI_PULLUP_1KOHM      0
#define PM8XXX_MPP_BI_PULLUP_OPEN       1
#define PM8XXX_MPP_BI_PULLUP_10KOHM     2
#define PM8XXX_MPP_BI_PULLUP_30KOHM     3

/* Analog Output: control */
#define PM8XXX_MPP_AOUT_CTRL_DISABLE            0
#define PM8XXX_MPP_AOUT_CTRL_ENABLE             1
#define PM8XXX_MPP_AOUT_CTRL_MPP_HIGH_EN        2
#define PM8XXX_MPP_AOUT_CTRL_MPP_LOW_EN         3

/* Current Sink: control */
#define PM8XXX_MPP_CS_CTRL_DISABLE      0
#define PM8XXX_MPP_CS_CTRL_ENABLE       1
#define PM8XXX_MPP_CS_CTRL_MPP_HIGH_EN  2
#define PM8XXX_MPP_CS_CTRL_MPP_LOW_EN   3

/* DTEST Current Sink: control */
#define PM8XXX_MPP_DTEST_CS_CTRL_EN1    0
#define PM8XXX_MPP_DTEST_CS_CTRL_EN2    1
#define PM8XXX_MPP_DTEST_CS_CTRL_EN3    2
#define PM8XXX_MPP_DTEST_CS_CTRL_EN4    3

/* DTEST Digital Output: control */
#define PM8XXX_MPP_DTEST_DBUS1          0
#define PM8XXX_MPP_DTEST_DBUS2          1
#define PM8XXX_MPP_DTEST_DBUS3          2
#define PM8XXX_MPP_DTEST_DBUS4          3

/* custom pinconf parameters */
#define PM8XXX_CONFIG_AMUX		(PIN_CONFIG_END + 1)
#define PM8XXX_CONFIG_DTEST_SELECTOR	(PIN_CONFIG_END + 2)
#define PM8XXX_CONFIG_ALEVEL		(PIN_CONFIG_END + 3)
#define PM8XXX_CONFIG_PAIRED		(PIN_CONFIG_END + 4)

/**
 * struct pm8xxx_pin_data - dynamic configuration for a pin
 * @reg:		address of the control register
 * @mode:		operating mode for the pin (digital, analog or current sink)
 * @input:		pin is input
 * @output:		pin is output
 * @high_z:		pin is floating
 * @paired:		mpp operates in paired mode
 * @output_value:	logical output value of the mpp
 * @power_source:	selected power source
 * @dtest:		DTEST route selector
 * @amux:		input muxing in analog mode
 * @aout_level:		selector of the output in analog mode
 * @drive_strength:	drive strength of the current sink
 * @pullup:		pull up value, when in digital bidirectional mode
 */
struct pm8xxx_pin_data {
	unsigned reg;

	u8 mode;

	bool input;
	bool output;
	bool high_z;
	bool paired;
	bool output_value;

	u8 power_source;
	u8 dtest;
	u8 amux;
	u8 aout_level;
	u8 drive_strength;
	unsigned pullup;
};

struct pm8xxx_mpp {
	struct device *dev;
	struct regmap *regmap;
	struct pinctrl_dev *pctrl;
	struct gpio_chip chip;
	struct irq_chip irq;

	struct pinctrl_desc desc;
	unsigned npins;
};

static const struct pinconf_generic_params pm8xxx_mpp_bindings[] = {
	{"qcom,amux-route",	PM8XXX_CONFIG_AMUX,		0},
	{"qcom,analog-level",	PM8XXX_CONFIG_ALEVEL,		0},
	{"qcom,dtest",		PM8XXX_CONFIG_DTEST_SELECTOR,	0},
	{"qcom,paired",		PM8XXX_CONFIG_PAIRED,		0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item pm8xxx_conf_items[] = {
	PCONFDUMP(PM8XXX_CONFIG_AMUX, "analog mux", NULL, true),
	PCONFDUMP(PM8XXX_CONFIG_ALEVEL, "analog level", NULL, true),
	PCONFDUMP(PM8XXX_CONFIG_DTEST_SELECTOR, "dtest", NULL, true),
	PCONFDUMP(PM8XXX_CONFIG_PAIRED, "paired", NULL, false),
};
#endif

#define PM8XXX_MAX_MPPS	12
#define PM8XXX_MPP_PHYSICAL_OFFSET    1

static const char * const pm8xxx_groups[PM8XXX_MAX_MPPS] = {
	"mpp1", "mpp2", "mpp3", "mpp4", "mpp5", "mpp6", "mpp7", "mpp8",
	"mpp9", "mpp10", "mpp11", "mpp12",
};

#define PM8XXX_MPP_DIGITAL	0
#define PM8XXX_MPP_ANALOG	1
#define PM8XXX_MPP_SINK		2

static const char * const pm8xxx_mpp_functions[] = {
	"digital", "analog", "sink",
};

static int pm8xxx_mpp_update(struct pm8xxx_mpp *pctrl,
			     struct pm8xxx_pin_data *pin)
{
	unsigned level;
	unsigned ctrl;
	unsigned type;
	int ret;
	u8 val;

	switch (pin->mode) {
	case PM8XXX_MPP_DIGITAL:
		if (pin->dtest) {
			type = PM8XXX_MPP_TYPE_DTEST_OUTPUT;
			ctrl = pin->dtest - 1;
		} else if (pin->input && pin->output) {
			type = PM8XXX_MPP_TYPE_D_BI_DIR;
			if (pin->high_z)
				ctrl = PM8XXX_MPP_BI_PULLUP_OPEN;
			else if (pin->pullup == 600)
				ctrl = PM8XXX_MPP_BI_PULLUP_1KOHM;
			else if (pin->pullup == 10000)
				ctrl = PM8XXX_MPP_BI_PULLUP_10KOHM;
			else
				ctrl = PM8XXX_MPP_BI_PULLUP_30KOHM;
		} else if (pin->input) {
			type = PM8XXX_MPP_TYPE_D_INPUT;
			if (pin->dtest)
				ctrl = pin->dtest;
			else
				ctrl = PM8XXX_MPP_DIN_TO_INT;
		} else {
			type = PM8XXX_MPP_TYPE_D_OUTPUT;
			ctrl = !!pin->output_value;
			if (pin->paired)
				ctrl |= BIT(1);
		}

		level = pin->power_source;
		break;
	case PM8XXX_MPP_ANALOG:
		if (pin->output) {
			type = PM8XXX_MPP_TYPE_A_OUTPUT;
			level = pin->aout_level;
			ctrl = pin->output_value;
			if (pin->paired)
				ctrl |= BIT(1);
		} else {
			type = PM8XXX_MPP_TYPE_A_INPUT;
			level = pin->amux;
			ctrl = 0;
		}
		break;
	case PM8XXX_MPP_SINK:
		level = (pin->drive_strength / 5) - 1;
		if (pin->dtest) {
			type = PM8XXX_MPP_TYPE_DTEST_SINK;
			ctrl = pin->dtest - 1;
		} else {
			type = PM8XXX_MPP_TYPE_SINK;
			ctrl = pin->output_value;
			if (pin->paired)
				ctrl |= BIT(1);
		}
		break;
	default:
		return -EINVAL;
	}

	val = type << 5 | level << 2 | ctrl;
	ret = regmap_write(pctrl->regmap, pin->reg, val);
	if (ret)
		dev_err(pctrl->dev, "failed to write register\n");

	return ret;
}

static int pm8xxx_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pm8xxx_mpp *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->npins;
}

static const char *pm8xxx_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned group)
{
	return pm8xxx_groups[group];
}


static int pm8xxx_get_group_pins(struct pinctrl_dev *pctldev,
				 unsigned group,
				 const unsigned **pins,
				 unsigned *num_pins)
{
	struct pm8xxx_mpp *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pctrl->desc.pins[group].number;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops pm8xxx_pinctrl_ops = {
	.get_groups_count	= pm8xxx_get_groups_count,
	.get_group_name		= pm8xxx_get_group_name,
	.get_group_pins         = pm8xxx_get_group_pins,
	.dt_node_to_map		= pinconf_generic_dt_node_to_map_group,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int pm8xxx_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(pm8xxx_mpp_functions);
}

static const char *pm8xxx_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned function)
{
	return pm8xxx_mpp_functions[function];
}

static int pm8xxx_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned function,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct pm8xxx_mpp *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pm8xxx_groups;
	*num_groups = pctrl->npins;
	return 0;
}

static int pm8xxx_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned function,
				 unsigned group)
{
	struct pm8xxx_mpp *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[group].drv_data;

	pin->mode = function;
	pm8xxx_mpp_update(pctrl, pin);

	return 0;
}

static const struct pinmux_ops pm8xxx_pinmux_ops = {
	.get_functions_count	= pm8xxx_get_functions_count,
	.get_function_name	= pm8xxx_get_function_name,
	.get_function_groups	= pm8xxx_get_function_groups,
	.set_mux		= pm8xxx_pinmux_set_mux,
};

static int pm8xxx_pin_config_get(struct pinctrl_dev *pctldev,
				 unsigned int offset,
				 unsigned long *config)
{
	struct pm8xxx_mpp *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	unsigned param = pinconf_to_config_param(*config);
	unsigned arg;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = pin->pullup;
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		arg = pin->high_z;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		arg = pin->input;
		break;
	case PIN_CONFIG_OUTPUT:
		arg = pin->output_value;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		arg = pin->power_source;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = pin->drive_strength;
		break;
	case PM8XXX_CONFIG_DTEST_SELECTOR:
		arg = pin->dtest;
		break;
	case PM8XXX_CONFIG_AMUX:
		arg = pin->amux;
		break;
	case PM8XXX_CONFIG_ALEVEL:
		arg = pin->aout_level;
		break;
	case PM8XXX_CONFIG_PAIRED:
		arg = pin->paired;
		break;
	default:
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int pm8xxx_pin_config_set(struct pinctrl_dev *pctldev,
				 unsigned int offset,
				 unsigned long *configs,
				 unsigned num_configs)
{
	struct pm8xxx_mpp *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	unsigned param;
	unsigned arg;
	unsigned i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			pin->pullup = arg;
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			pin->high_z = true;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			pin->input = true;
			break;
		case PIN_CONFIG_OUTPUT:
			pin->output = true;
			pin->output_value = !!arg;
			break;
		case PIN_CONFIG_POWER_SOURCE:
			pin->power_source = arg;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			pin->drive_strength = arg;
			break;
		case PM8XXX_CONFIG_DTEST_SELECTOR:
			pin->dtest = arg;
			break;
		case PM8XXX_CONFIG_AMUX:
			pin->amux = arg;
			break;
		case PM8XXX_CONFIG_ALEVEL:
			pin->aout_level = arg;
			break;
		case PM8XXX_CONFIG_PAIRED:
			pin->paired = !!arg;
			break;
		default:
			dev_err(pctrl->dev,
				"unsupported config parameter: %x\n",
				param);
			return -EINVAL;
		}
	}

	pm8xxx_mpp_update(pctrl, pin);

	return 0;
}

static const struct pinconf_ops pm8xxx_pinconf_ops = {
	.is_generic = true,
	.pin_config_group_get = pm8xxx_pin_config_get,
	.pin_config_group_set = pm8xxx_pin_config_set,
};

static const struct pinctrl_desc pm8xxx_pinctrl_desc = {
	.name = "pm8xxx_mpp",
	.pctlops = &pm8xxx_pinctrl_ops,
	.pmxops = &pm8xxx_pinmux_ops,
	.confops = &pm8xxx_pinconf_ops,
	.owner = THIS_MODULE,
};

static int pm8xxx_mpp_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	struct pm8xxx_mpp *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;

	switch (pin->mode) {
	case PM8XXX_MPP_DIGITAL:
		pin->input = true;
		break;
	case PM8XXX_MPP_ANALOG:
		pin->input = true;
		pin->output = true;
		break;
	case PM8XXX_MPP_SINK:
		return -EINVAL;
	}

	pm8xxx_mpp_update(pctrl, pin);

	return 0;
}

static int pm8xxx_mpp_direction_output(struct gpio_chip *chip,
					unsigned offset,
					int value)
{
	struct pm8xxx_mpp *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;

	switch (pin->mode) {
	case PM8XXX_MPP_DIGITAL:
		pin->output = true;
		break;
	case PM8XXX_MPP_ANALOG:
		pin->input = false;
		pin->output = true;
		break;
	case PM8XXX_MPP_SINK:
		pin->input = false;
		pin->output = true;
		break;
	}

	pm8xxx_mpp_update(pctrl, pin);

	return 0;
}

static int pm8xxx_mpp_get(struct gpio_chip *chip, unsigned offset)
{
	struct pm8xxx_mpp *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	bool state;
	int ret, irq;

	if (!pin->input)
		return !!pin->output_value;

	irq = chip->to_irq(chip, offset);
	if (irq < 0)
		return irq;

	ret = irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL, &state);
	if (!ret)
		ret = !!state;

	return ret;
}

static void pm8xxx_mpp_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pm8xxx_mpp *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;

	pin->output_value = !!value;

	pm8xxx_mpp_update(pctrl, pin);
}

static int pm8xxx_mpp_of_xlate(struct gpio_chip *chip,
				const struct of_phandle_args *gpio_desc,
				u32 *flags)
{
	if (chip->of_gpio_n_cells < 2)
		return -EINVAL;

	if (flags)
		*flags = gpio_desc->args[1];

	return gpio_desc->args[0] - PM8XXX_MPP_PHYSICAL_OFFSET;
}


#ifdef CONFIG_DEBUG_FS

static void pm8xxx_mpp_dbg_show_one(struct seq_file *s,
				  struct pinctrl_dev *pctldev,
				  struct gpio_chip *chip,
				  unsigned offset,
				  unsigned gpio)
{
	struct pm8xxx_mpp *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;

	static const char * const aout_lvls[] = {
		"1v25", "1v25_2", "0v625", "0v3125", "mpp", "abus1", "abus2",
		"abus3"
	};

	static const char * const amuxs[] = {
		"amux5", "amux6", "amux7", "amux8", "amux9", "abus1", "abus2",
		"abus3",
	};

	seq_printf(s, " mpp%-2d:", offset + PM8XXX_MPP_PHYSICAL_OFFSET);

	switch (pin->mode) {
	case PM8XXX_MPP_DIGITAL:
		seq_puts(s, " digital ");
		if (pin->dtest) {
			seq_printf(s, "dtest%d\n", pin->dtest);
		} else if (pin->input && pin->output) {
			if (pin->high_z)
				seq_puts(s, "bi-dir high-z");
			else
				seq_printf(s, "bi-dir %dOhm", pin->pullup);
		} else if (pin->input) {
			if (pin->dtest)
				seq_printf(s, "in dtest%d", pin->dtest);
			else
				seq_puts(s, "in gpio");
		} else if (pin->output) {
			seq_puts(s, "out ");

			if (!pin->paired) {
				seq_puts(s, pin->output_value ?
					 "high" : "low");
			} else {
				seq_puts(s, pin->output_value ?
					 "inverted" : "follow");
			}
		}
		break;
	case PM8XXX_MPP_ANALOG:
		seq_puts(s, " analog ");
		if (pin->output) {
			seq_printf(s, "out %s ", aout_lvls[pin->aout_level]);
			if (!pin->paired) {
				seq_puts(s, pin->output_value ?
					 "high" : "low");
			} else {
				seq_puts(s, pin->output_value ?
					 "inverted" : "follow");
			}
		} else {
			seq_printf(s, "input mux %s", amuxs[pin->amux]);
		}
		break;
	case PM8XXX_MPP_SINK:
		seq_printf(s, " sink %dmA ", pin->drive_strength);
		if (pin->dtest) {
			seq_printf(s, "dtest%d", pin->dtest);
		} else {
			if (!pin->paired) {
				seq_puts(s, pin->output_value ?
					 "high" : "low");
			} else {
				seq_puts(s, pin->output_value ?
					 "inverted" : "follow");
			}
		}
		break;
	}
}

static void pm8xxx_mpp_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned gpio = chip->base;
	unsigned i;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		pm8xxx_mpp_dbg_show_one(s, NULL, chip, i, gpio);
		seq_puts(s, "\n");
	}
}

#else
#define pm8xxx_mpp_dbg_show NULL
#endif

static const struct gpio_chip pm8xxx_mpp_template = {
	.direction_input = pm8xxx_mpp_direction_input,
	.direction_output = pm8xxx_mpp_direction_output,
	.get = pm8xxx_mpp_get,
	.set = pm8xxx_mpp_set,
	.of_xlate = pm8xxx_mpp_of_xlate,
	.dbg_show = pm8xxx_mpp_dbg_show,
	.owner = THIS_MODULE,
};

static int pm8xxx_pin_populate(struct pm8xxx_mpp *pctrl,
			       struct pm8xxx_pin_data *pin)
{
	unsigned int val;
	unsigned level;
	unsigned ctrl;
	unsigned type;
	int ret;

	ret = regmap_read(pctrl->regmap, pin->reg, &val);
	if (ret) {
		dev_err(pctrl->dev, "failed to read register\n");
		return ret;
	}

	type = (val >> 5) & 7;
	level = (val >> 2) & 7;
	ctrl = (val) & 3;

	switch (type) {
	case PM8XXX_MPP_TYPE_D_INPUT:
		pin->mode = PM8XXX_MPP_DIGITAL;
		pin->input = true;
		pin->power_source = level;
		pin->dtest = ctrl;
		break;
	case PM8XXX_MPP_TYPE_D_OUTPUT:
		pin->mode = PM8XXX_MPP_DIGITAL;
		pin->output = true;
		pin->power_source = level;
		pin->output_value = !!(ctrl & BIT(0));
		pin->paired = !!(ctrl & BIT(1));
		break;
	case PM8XXX_MPP_TYPE_D_BI_DIR:
		pin->mode = PM8XXX_MPP_DIGITAL;
		pin->input = true;
		pin->output = true;
		pin->power_source = level;
		switch (ctrl) {
		case PM8XXX_MPP_BI_PULLUP_1KOHM:
			pin->pullup = 600;
			break;
		case PM8XXX_MPP_BI_PULLUP_OPEN:
			pin->high_z = true;
			break;
		case PM8XXX_MPP_BI_PULLUP_10KOHM:
			pin->pullup = 10000;
			break;
		case PM8XXX_MPP_BI_PULLUP_30KOHM:
			pin->pullup = 30000;
			break;
		}
		break;
	case PM8XXX_MPP_TYPE_A_INPUT:
		pin->mode = PM8XXX_MPP_ANALOG;
		pin->input = true;
		pin->amux = level;
		break;
	case PM8XXX_MPP_TYPE_A_OUTPUT:
		pin->mode = PM8XXX_MPP_ANALOG;
		pin->output = true;
		pin->aout_level = level;
		pin->output_value = !!(ctrl & BIT(0));
		pin->paired = !!(ctrl & BIT(1));
		break;
	case PM8XXX_MPP_TYPE_SINK:
		pin->mode = PM8XXX_MPP_SINK;
		pin->drive_strength = 5 * (level + 1);
		pin->output_value = !!(ctrl & BIT(0));
		pin->paired = !!(ctrl & BIT(1));
		break;
	case PM8XXX_MPP_TYPE_DTEST_SINK:
		pin->mode = PM8XXX_MPP_SINK;
		pin->dtest = ctrl + 1;
		pin->drive_strength = 5 * (level + 1);
		break;
	case PM8XXX_MPP_TYPE_DTEST_OUTPUT:
		pin->mode = PM8XXX_MPP_DIGITAL;
		pin->power_source = level;
		if (ctrl >= 1)
			pin->dtest = ctrl;
		break;
	}

	return 0;
}

static int pm8xxx_mpp_domain_translate(struct irq_domain *domain,
				   struct irq_fwspec *fwspec,
				   unsigned long *hwirq,
				   unsigned int *type)
{
	struct pm8xxx_mpp *pctrl = container_of(domain->host_data,
						 struct pm8xxx_mpp, chip);

	if (fwspec->param_count != 2 ||
	    fwspec->param[0] < PM8XXX_MPP_PHYSICAL_OFFSET ||
	    fwspec->param[0] > pctrl->chip.ngpio)
		return -EINVAL;

	*hwirq = fwspec->param[0] - PM8XXX_MPP_PHYSICAL_OFFSET;
	*type = fwspec->param[1];

	return 0;
}

static unsigned int pm8xxx_mpp_child_offset_to_irq(struct gpio_chip *chip,
						   unsigned int offset)
{
	return offset + PM8XXX_MPP_PHYSICAL_OFFSET;
}

static int pm8821_mpp_child_to_parent_hwirq(struct gpio_chip *chip,
					    unsigned int child_hwirq,
					    unsigned int child_type,
					    unsigned int *parent_hwirq,
					    unsigned int *parent_type)
{
	*parent_hwirq = child_hwirq + 24;
	*parent_type = child_type;

	return 0;
}

static int pm8xxx_mpp_child_to_parent_hwirq(struct gpio_chip *chip,
					    unsigned int child_hwirq,
					    unsigned int child_type,
					    unsigned int *parent_hwirq,
					    unsigned int *parent_type)
{
	*parent_hwirq = child_hwirq + 0x80;
	*parent_type = child_type;

	return 0;
}

static const struct of_device_id pm8xxx_mpp_of_match[] = {
	{ .compatible = "qcom,pm8018-mpp", .data = (void *) 6 },
	{ .compatible = "qcom,pm8038-mpp", .data = (void *) 6 },
	{ .compatible = "qcom,pm8058-mpp", .data = (void *) 12 },
	{ .compatible = "qcom,pm8821-mpp", .data = (void *) 4 },
	{ .compatible = "qcom,pm8917-mpp", .data = (void *) 10 },
	{ .compatible = "qcom,pm8921-mpp", .data = (void *) 12 },
	{ },
};
MODULE_DEVICE_TABLE(of, pm8xxx_mpp_of_match);

static int pm8xxx_mpp_probe(struct platform_device *pdev)
{
	struct pm8xxx_pin_data *pin_data;
	struct irq_domain *parent_domain;
	struct device_node *parent_node;
	struct pinctrl_pin_desc *pins;
	struct gpio_irq_chip *girq;
	struct pm8xxx_mpp *pctrl;
	int ret;
	int i;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->dev = &pdev->dev;
	pctrl->npins = (uintptr_t) device_get_match_data(&pdev->dev);

	pctrl->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pctrl->regmap) {
		dev_err(&pdev->dev, "parent regmap unavailable\n");
		return -ENXIO;
	}

	pctrl->desc = pm8xxx_pinctrl_desc;
	pctrl->desc.npins = pctrl->npins;

	pins = devm_kcalloc(&pdev->dev,
			    pctrl->desc.npins,
			    sizeof(struct pinctrl_pin_desc),
			    GFP_KERNEL);
	if (!pins)
		return -ENOMEM;

	pin_data = devm_kcalloc(&pdev->dev,
				pctrl->desc.npins,
				sizeof(struct pm8xxx_pin_data),
				GFP_KERNEL);
	if (!pin_data)
		return -ENOMEM;

	for (i = 0; i < pctrl->desc.npins; i++) {
		pin_data[i].reg = SSBI_REG_ADDR_MPP(i);

		ret = pm8xxx_pin_populate(pctrl, &pin_data[i]);
		if (ret)
			return ret;

		pins[i].number = i;
		pins[i].name = pm8xxx_groups[i];
		pins[i].drv_data = &pin_data[i];
	}
	pctrl->desc.pins = pins;

	pctrl->desc.num_custom_params = ARRAY_SIZE(pm8xxx_mpp_bindings);
	pctrl->desc.custom_params = pm8xxx_mpp_bindings;
#ifdef CONFIG_DEBUG_FS
	pctrl->desc.custom_conf_items = pm8xxx_conf_items;
#endif

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pctrl)) {
		dev_err(&pdev->dev, "couldn't register pm8xxx mpp driver\n");
		return PTR_ERR(pctrl->pctrl);
	}

	pctrl->chip = pm8xxx_mpp_template;
	pctrl->chip.base = -1;
	pctrl->chip.parent = &pdev->dev;
	pctrl->chip.of_gpio_n_cells = 2;
	pctrl->chip.label = dev_name(pctrl->dev);
	pctrl->chip.ngpio = pctrl->npins;

	parent_node = of_irq_find_parent(pctrl->dev->of_node);
	if (!parent_node)
		return -ENXIO;

	parent_domain = irq_find_host(parent_node);
	of_node_put(parent_node);
	if (!parent_domain)
		return -ENXIO;

	pctrl->irq.name = "ssbi-mpp";
	pctrl->irq.irq_mask_ack = irq_chip_mask_ack_parent;
	pctrl->irq.irq_unmask = irq_chip_unmask_parent;
	pctrl->irq.irq_set_type = irq_chip_set_type_parent;
	pctrl->irq.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE;

	girq = &pctrl->chip.irq;
	girq->chip = &pctrl->irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;
	girq->fwnode = dev_fwnode(pctrl->dev);
	girq->parent_domain = parent_domain;
	if (of_device_is_compatible(pdev->dev.of_node, "qcom,pm8821-mpp"))
		girq->child_to_parent_hwirq = pm8821_mpp_child_to_parent_hwirq;
	else
		girq->child_to_parent_hwirq = pm8xxx_mpp_child_to_parent_hwirq;
	girq->populate_parent_alloc_arg = gpiochip_populate_parent_fwspec_twocell;
	girq->child_offset_to_irq = pm8xxx_mpp_child_offset_to_irq;
	girq->child_irq_domain_ops.translate = pm8xxx_mpp_domain_translate;

	ret = gpiochip_add_data(&pctrl->chip, pctrl);
	if (ret) {
		dev_err(&pdev->dev, "failed register gpiochip\n");
		return ret;
	}

	ret = gpiochip_add_pin_range(&pctrl->chip,
				     dev_name(pctrl->dev),
				     0, 0, pctrl->chip.ngpio);
	if (ret) {
		dev_err(pctrl->dev, "failed to add pin range\n");
		goto unregister_gpiochip;
	}

	platform_set_drvdata(pdev, pctrl);

	dev_dbg(&pdev->dev, "Qualcomm pm8xxx mpp driver probed\n");

	return 0;

unregister_gpiochip:
	gpiochip_remove(&pctrl->chip);

	return ret;
}

static int pm8xxx_mpp_remove(struct platform_device *pdev)
{
	struct pm8xxx_mpp *pctrl = platform_get_drvdata(pdev);

	gpiochip_remove(&pctrl->chip);

	return 0;
}

static struct platform_driver pm8xxx_mpp_driver = {
	.driver = {
		.name = "qcom-ssbi-mpp",
		.of_match_table = pm8xxx_mpp_of_match,
	},
	.probe = pm8xxx_mpp_probe,
	.remove = pm8xxx_mpp_remove,
};

module_platform_driver(pm8xxx_mpp_driver);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm PM8xxx MPP driver");
MODULE_LICENSE("GPL v2");
