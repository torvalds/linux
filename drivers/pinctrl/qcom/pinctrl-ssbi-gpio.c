// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, Sony Mobile Communications AB.
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include <dt-bindings/pinctrl/qcom,pmic-gpio.h>

#include "../core.h"
#include "../pinctrl-utils.h"

/* mode */
#define PM8XXX_GPIO_MODE_ENABLED	BIT(0)
#define PM8XXX_GPIO_MODE_INPUT		0
#define PM8XXX_GPIO_MODE_OUTPUT		2

/* output buffer */
#define PM8XXX_GPIO_PUSH_PULL		0
#define PM8XXX_GPIO_OPEN_DRAIN		1

/* bias */
#define PM8XXX_GPIO_BIAS_PU_30		0
#define PM8XXX_GPIO_BIAS_PU_1P5		1
#define PM8XXX_GPIO_BIAS_PU_31P5	2
#define PM8XXX_GPIO_BIAS_PU_1P5_30	3
#define PM8XXX_GPIO_BIAS_PD		4
#define PM8XXX_GPIO_BIAS_NP		5

/* GPIO registers */
#define SSBI_REG_ADDR_GPIO_BASE		0x150
#define SSBI_REG_ADDR_GPIO(n)		(SSBI_REG_ADDR_GPIO_BASE + n)

#define PM8XXX_BANK_WRITE		BIT(7)

#define PM8XXX_MAX_GPIOS               44

#define PM8XXX_GPIO_PHYSICAL_OFFSET	1

/* custom pinconf parameters */
#define PM8XXX_QCOM_DRIVE_STRENGH      (PIN_CONFIG_END + 1)
#define PM8XXX_QCOM_PULL_UP_STRENGTH   (PIN_CONFIG_END + 2)

/**
 * struct pm8xxx_pin_data - dynamic configuration for a pin
 * @reg:               address of the control register
 * @power_source:      logical selected voltage source, mapping in static data
 *                     is used translate to register values
 * @mode:              operating mode for the pin (input/output)
 * @open_drain:        output buffer configured as open-drain (vs push-pull)
 * @output_value:      configured output value
 * @bias:              register view of configured bias
 * @pull_up_strength:  placeholder for selected pull up strength
 *                     only used to configure bias when pull up is selected
 * @output_strength:   selector of output-strength
 * @disable:           pin disabled / configured as tristate
 * @function:          pinmux selector
 * @inverted:          pin logic is inverted
 */
struct pm8xxx_pin_data {
	unsigned reg;
	u8 power_source;
	u8 mode;
	bool open_drain;
	bool output_value;
	u8 bias;
	u8 pull_up_strength;
	u8 output_strength;
	bool disable;
	u8 function;
	bool inverted;
};

struct pm8xxx_gpio {
	struct device *dev;
	struct regmap *regmap;
	struct pinctrl_dev *pctrl;
	struct gpio_chip chip;

	struct pinctrl_desc desc;
	unsigned npins;
};

static const struct pinconf_generic_params pm8xxx_gpio_bindings[] = {
	{"qcom,drive-strength",		PM8XXX_QCOM_DRIVE_STRENGH,	0},
	{"qcom,pull-up-strength",	PM8XXX_QCOM_PULL_UP_STRENGTH,	0},
};

#ifdef CONFIG_DEBUG_FS
static const struct pin_config_item pm8xxx_conf_items[ARRAY_SIZE(pm8xxx_gpio_bindings)] = {
	PCONFDUMP(PM8XXX_QCOM_DRIVE_STRENGH, "drive-strength", NULL, true),
	PCONFDUMP(PM8XXX_QCOM_PULL_UP_STRENGTH,  "pull up strength", NULL, true),
};
#endif

static const char * const pm8xxx_groups[PM8XXX_MAX_GPIOS] = {
	"gpio1", "gpio2", "gpio3", "gpio4", "gpio5", "gpio6", "gpio7", "gpio8",
	"gpio9", "gpio10", "gpio11", "gpio12", "gpio13", "gpio14", "gpio15",
	"gpio16", "gpio17", "gpio18", "gpio19", "gpio20", "gpio21", "gpio22",
	"gpio23", "gpio24", "gpio25", "gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio32", "gpio33", "gpio34", "gpio35", "gpio36",
	"gpio37", "gpio38", "gpio39", "gpio40", "gpio41", "gpio42", "gpio43",
	"gpio44",
};

static const char * const pm8xxx_gpio_functions[] = {
	PMIC_GPIO_FUNC_NORMAL, PMIC_GPIO_FUNC_PAIRED,
	PMIC_GPIO_FUNC_FUNC1, PMIC_GPIO_FUNC_FUNC2,
	PMIC_GPIO_FUNC_DTEST1, PMIC_GPIO_FUNC_DTEST2,
	PMIC_GPIO_FUNC_DTEST3, PMIC_GPIO_FUNC_DTEST4,
};

static int pm8xxx_read_bank(struct pm8xxx_gpio *pctrl,
			    struct pm8xxx_pin_data *pin, int bank)
{
	unsigned int val = bank << 4;
	int ret;

	ret = regmap_write(pctrl->regmap, pin->reg, val);
	if (ret) {
		dev_err(pctrl->dev, "failed to select bank %d\n", bank);
		return ret;
	}

	ret = regmap_read(pctrl->regmap, pin->reg, &val);
	if (ret) {
		dev_err(pctrl->dev, "failed to read register %d\n", bank);
		return ret;
	}

	return val;
}

static int pm8xxx_write_bank(struct pm8xxx_gpio *pctrl,
			     struct pm8xxx_pin_data *pin,
			     int bank,
			     u8 val)
{
	int ret;

	val |= PM8XXX_BANK_WRITE;
	val |= bank << 4;

	ret = regmap_write(pctrl->regmap, pin->reg, val);
	if (ret)
		dev_err(pctrl->dev, "failed to write register\n");

	return ret;
}

static int pm8xxx_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct pm8xxx_gpio *pctrl = pinctrl_dev_get_drvdata(pctldev);

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
	struct pm8xxx_gpio *pctrl = pinctrl_dev_get_drvdata(pctldev);

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
	return ARRAY_SIZE(pm8xxx_gpio_functions);
}

static const char *pm8xxx_get_function_name(struct pinctrl_dev *pctldev,
					    unsigned function)
{
	return pm8xxx_gpio_functions[function];
}

static int pm8xxx_get_function_groups(struct pinctrl_dev *pctldev,
				      unsigned function,
				      const char * const **groups,
				      unsigned * const num_groups)
{
	struct pm8xxx_gpio *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pm8xxx_groups;
	*num_groups = pctrl->npins;
	return 0;
}

static int pm8xxx_pinmux_set_mux(struct pinctrl_dev *pctldev,
				 unsigned function,
				 unsigned group)
{
	struct pm8xxx_gpio *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[group].drv_data;
	u8 val;

	pin->function = function;
	val = pin->function << 1;

	pm8xxx_write_bank(pctrl, pin, 4, val);

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
	struct pm8xxx_gpio *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	unsigned param = pinconf_to_config_param(*config);
	unsigned arg;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (pin->bias != PM8XXX_GPIO_BIAS_NP)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (pin->bias != PM8XXX_GPIO_BIAS_PD)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pin->bias > PM8XXX_GPIO_BIAS_PU_1P5_30)
			return -EINVAL;
		arg = 1;
		break;
	case PM8XXX_QCOM_PULL_UP_STRENGTH:
		arg = pin->pull_up_strength;
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		if (!pin->disable)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		if (pin->mode != PM8XXX_GPIO_MODE_INPUT)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_OUTPUT:
		if (pin->mode & PM8XXX_GPIO_MODE_OUTPUT)
			arg = pin->output_value;
		else
			arg = 0;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		arg = pin->power_source;
		break;
	case PM8XXX_QCOM_DRIVE_STRENGH:
		arg = pin->output_strength;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (pin->open_drain)
			return -EINVAL;
		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (!pin->open_drain)
			return -EINVAL;
		arg = 1;
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
	struct pm8xxx_gpio *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	unsigned param;
	unsigned arg;
	unsigned i;
	u8 banks = 0;
	u8 val;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			pin->bias = PM8XXX_GPIO_BIAS_NP;
			banks |= BIT(2);
			pin->disable = 0;
			banks |= BIT(3);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			pin->bias = PM8XXX_GPIO_BIAS_PD;
			banks |= BIT(2);
			pin->disable = 0;
			banks |= BIT(3);
			break;
		case PM8XXX_QCOM_PULL_UP_STRENGTH:
			if (arg > PM8XXX_GPIO_BIAS_PU_1P5_30) {
				dev_err(pctrl->dev, "invalid pull-up strength\n");
				return -EINVAL;
			}
			pin->pull_up_strength = arg;
			/* FALLTHROUGH */
		case PIN_CONFIG_BIAS_PULL_UP:
			pin->bias = pin->pull_up_strength;
			banks |= BIT(2);
			pin->disable = 0;
			banks |= BIT(3);
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			pin->disable = 1;
			banks |= BIT(3);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			pin->mode = PM8XXX_GPIO_MODE_INPUT;
			banks |= BIT(0) | BIT(1);
			break;
		case PIN_CONFIG_OUTPUT:
			pin->mode = PM8XXX_GPIO_MODE_OUTPUT;
			pin->output_value = !!arg;
			banks |= BIT(0) | BIT(1);
			break;
		case PIN_CONFIG_POWER_SOURCE:
			pin->power_source = arg;
			banks |= BIT(0);
			break;
		case PM8XXX_QCOM_DRIVE_STRENGH:
			if (arg > PMIC_GPIO_STRENGTH_LOW) {
				dev_err(pctrl->dev, "invalid drive strength\n");
				return -EINVAL;
			}
			pin->output_strength = arg;
			banks |= BIT(3);
			break;
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			pin->open_drain = 0;
			banks |= BIT(1);
			break;
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			pin->open_drain = 1;
			banks |= BIT(1);
			break;
		default:
			dev_err(pctrl->dev,
				"unsupported config parameter: %x\n",
				param);
			return -EINVAL;
		}
	}

	if (banks & BIT(0)) {
		val = pin->power_source << 1;
		val |= PM8XXX_GPIO_MODE_ENABLED;
		pm8xxx_write_bank(pctrl, pin, 0, val);
	}

	if (banks & BIT(1)) {
		val = pin->mode << 2;
		val |= pin->open_drain << 1;
		val |= pin->output_value;
		pm8xxx_write_bank(pctrl, pin, 1, val);
	}

	if (banks & BIT(2)) {
		val = pin->bias << 1;
		pm8xxx_write_bank(pctrl, pin, 2, val);
	}

	if (banks & BIT(3)) {
		val = pin->output_strength << 2;
		val |= pin->disable;
		pm8xxx_write_bank(pctrl, pin, 3, val);
	}

	if (banks & BIT(4)) {
		val = pin->function << 1;
		pm8xxx_write_bank(pctrl, pin, 4, val);
	}

	if (banks & BIT(5)) {
		val = 0;
		if (!pin->inverted)
			val |= BIT(3);
		pm8xxx_write_bank(pctrl, pin, 5, val);
	}

	return 0;
}

static const struct pinconf_ops pm8xxx_pinconf_ops = {
	.is_generic = true,
	.pin_config_group_get = pm8xxx_pin_config_get,
	.pin_config_group_set = pm8xxx_pin_config_set,
};

static const struct pinctrl_desc pm8xxx_pinctrl_desc = {
	.name = "pm8xxx_gpio",
	.pctlops = &pm8xxx_pinctrl_ops,
	.pmxops = &pm8xxx_pinmux_ops,
	.confops = &pm8xxx_pinconf_ops,
	.owner = THIS_MODULE,
};

static int pm8xxx_gpio_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	struct pm8xxx_gpio *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	u8 val;

	pin->mode = PM8XXX_GPIO_MODE_INPUT;
	val = pin->mode << 2;

	pm8xxx_write_bank(pctrl, pin, 1, val);

	return 0;
}

static int pm8xxx_gpio_direction_output(struct gpio_chip *chip,
					unsigned offset,
					int value)
{
	struct pm8xxx_gpio *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	u8 val;

	pin->mode = PM8XXX_GPIO_MODE_OUTPUT;
	pin->output_value = !!value;

	val = pin->mode << 2;
	val |= pin->open_drain << 1;
	val |= pin->output_value;

	pm8xxx_write_bank(pctrl, pin, 1, val);

	return 0;
}

static int pm8xxx_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct pm8xxx_gpio *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	int ret, irq;
	bool state;

	if (pin->mode == PM8XXX_GPIO_MODE_OUTPUT)
		return pin->output_value;

	irq = chip->to_irq(chip, offset);
	if (irq >= 0) {
		ret = irq_get_irqchip_state(irq, IRQCHIP_STATE_LINE_LEVEL,
					    &state);
		if (!ret)
			ret = !!state;
	} else
		ret = -EINVAL;

	return ret;
}

static void pm8xxx_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct pm8xxx_gpio *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;
	u8 val;

	pin->output_value = !!value;

	val = pin->mode << 2;
	val |= pin->open_drain << 1;
	val |= pin->output_value;

	pm8xxx_write_bank(pctrl, pin, 1, val);
}

static int pm8xxx_gpio_of_xlate(struct gpio_chip *chip,
				const struct of_phandle_args *gpio_desc,
				u32 *flags)
{
	if (chip->of_gpio_n_cells < 2)
		return -EINVAL;

	if (flags)
		*flags = gpio_desc->args[1];

	return gpio_desc->args[0] - PM8XXX_GPIO_PHYSICAL_OFFSET;
}


#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static void pm8xxx_gpio_dbg_show_one(struct seq_file *s,
				  struct pinctrl_dev *pctldev,
				  struct gpio_chip *chip,
				  unsigned offset,
				  unsigned gpio)
{
	struct pm8xxx_gpio *pctrl = gpiochip_get_data(chip);
	struct pm8xxx_pin_data *pin = pctrl->desc.pins[offset].drv_data;

	static const char * const modes[] = {
		"in", "both", "out", "off"
	};
	static const char * const biases[] = {
		"pull-up 30uA", "pull-up 1.5uA", "pull-up 31.5uA",
		"pull-up 1.5uA + 30uA boost", "pull-down 10uA", "no pull"
	};
	static const char * const buffer_types[] = {
		"push-pull", "open-drain"
	};
	static const char * const strengths[] = {
		"no", "high", "medium", "low"
	};

	seq_printf(s, " gpio%-2d:", offset + PM8XXX_GPIO_PHYSICAL_OFFSET);
	if (pin->disable) {
		seq_puts(s, " ---");
	} else {
		seq_printf(s, " %-4s", modes[pin->mode]);
		seq_printf(s, " %-7s", pm8xxx_gpio_functions[pin->function]);
		seq_printf(s, " VIN%d", pin->power_source);
		seq_printf(s, " %-27s", biases[pin->bias]);
		seq_printf(s, " %-10s", buffer_types[pin->open_drain]);
		seq_printf(s, " %-4s", pin->output_value ? "high" : "low");
		seq_printf(s, " %-7s", strengths[pin->output_strength]);
		if (pin->inverted)
			seq_puts(s, " inverted");
	}
}

static void pm8xxx_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	unsigned gpio = chip->base;
	unsigned i;

	for (i = 0; i < chip->ngpio; i++, gpio++) {
		pm8xxx_gpio_dbg_show_one(s, NULL, chip, i, gpio);
		seq_puts(s, "\n");
	}
}

#else
#define pm8xxx_gpio_dbg_show NULL
#endif

static const struct gpio_chip pm8xxx_gpio_template = {
	.direction_input = pm8xxx_gpio_direction_input,
	.direction_output = pm8xxx_gpio_direction_output,
	.get = pm8xxx_gpio_get,
	.set = pm8xxx_gpio_set,
	.of_xlate = pm8xxx_gpio_of_xlate,
	.dbg_show = pm8xxx_gpio_dbg_show,
	.owner = THIS_MODULE,
};

static int pm8xxx_pin_populate(struct pm8xxx_gpio *pctrl,
			       struct pm8xxx_pin_data *pin)
{
	int val;

	val = pm8xxx_read_bank(pctrl, pin, 0);
	if (val < 0)
		return val;

	pin->power_source = (val >> 1) & 0x7;

	val = pm8xxx_read_bank(pctrl, pin, 1);
	if (val < 0)
		return val;

	pin->mode = (val >> 2) & 0x3;
	pin->open_drain = !!(val & BIT(1));
	pin->output_value = val & BIT(0);

	val = pm8xxx_read_bank(pctrl, pin, 2);
	if (val < 0)
		return val;

	pin->bias = (val >> 1) & 0x7;
	if (pin->bias <= PM8XXX_GPIO_BIAS_PU_1P5_30)
		pin->pull_up_strength = pin->bias;
	else
		pin->pull_up_strength = PM8XXX_GPIO_BIAS_PU_30;

	val = pm8xxx_read_bank(pctrl, pin, 3);
	if (val < 0)
		return val;

	pin->output_strength = (val >> 2) & 0x3;
	pin->disable = val & BIT(0);

	val = pm8xxx_read_bank(pctrl, pin, 4);
	if (val < 0)
		return val;

	pin->function = (val >> 1) & 0x7;

	val = pm8xxx_read_bank(pctrl, pin, 5);
	if (val < 0)
		return val;

	pin->inverted = !(val & BIT(3));

	return 0;
}

static struct irq_chip pm8xxx_irq_chip = {
	.name = "ssbi-gpio",
	.irq_mask_ack = irq_chip_mask_ack_parent,
	.irq_unmask = irq_chip_unmask_parent,
	.irq_set_type = irq_chip_set_type_parent,
	.flags = IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE,
};

static int pm8xxx_domain_translate(struct irq_domain *domain,
				   struct irq_fwspec *fwspec,
				   unsigned long *hwirq,
				   unsigned int *type)
{
	struct pm8xxx_gpio *pctrl = container_of(domain->host_data,
						 struct pm8xxx_gpio, chip);

	if (fwspec->param_count != 2 || fwspec->param[0] < 1 ||
	    fwspec->param[0] > pctrl->chip.ngpio)
		return -EINVAL;

	*hwirq = fwspec->param[0] - PM8XXX_GPIO_PHYSICAL_OFFSET;
	*type = fwspec->param[1];

	return 0;
}

static unsigned int pm8xxx_child_offset_to_irq(struct gpio_chip *chip,
					       unsigned int offset)
{
	return offset + PM8XXX_GPIO_PHYSICAL_OFFSET;
}

static int pm8xxx_child_to_parent_hwirq(struct gpio_chip *chip,
					unsigned int child_hwirq,
					unsigned int child_type,
					unsigned int *parent_hwirq,
					unsigned int *parent_type)
{
	*parent_hwirq = child_hwirq + 0xc0;
	*parent_type = child_type;

	return 0;
}

static const struct of_device_id pm8xxx_gpio_of_match[] = {
	{ .compatible = "qcom,pm8018-gpio", .data = (void *) 6 },
	{ .compatible = "qcom,pm8038-gpio", .data = (void *) 12 },
	{ .compatible = "qcom,pm8058-gpio", .data = (void *) 44 },
	{ .compatible = "qcom,pm8917-gpio", .data = (void *) 38 },
	{ .compatible = "qcom,pm8921-gpio", .data = (void *) 44 },
	{ },
};
MODULE_DEVICE_TABLE(of, pm8xxx_gpio_of_match);

static int pm8xxx_gpio_probe(struct platform_device *pdev)
{
	struct pm8xxx_pin_data *pin_data;
	struct irq_domain *parent_domain;
	struct device_node *parent_node;
	struct pinctrl_pin_desc *pins;
	struct gpio_irq_chip *girq;
	struct pm8xxx_gpio *pctrl;
	int ret, i;

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
		pin_data[i].reg = SSBI_REG_ADDR_GPIO(i);

		ret = pm8xxx_pin_populate(pctrl, &pin_data[i]);
		if (ret)
			return ret;

		pins[i].number = i;
		pins[i].name = pm8xxx_groups[i];
		pins[i].drv_data = &pin_data[i];
	}
	pctrl->desc.pins = pins;

	pctrl->desc.num_custom_params = ARRAY_SIZE(pm8xxx_gpio_bindings);
	pctrl->desc.custom_params = pm8xxx_gpio_bindings;
#ifdef CONFIG_DEBUG_FS
	pctrl->desc.custom_conf_items = pm8xxx_conf_items;
#endif

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pctrl)) {
		dev_err(&pdev->dev, "couldn't register pm8xxx gpio driver\n");
		return PTR_ERR(pctrl->pctrl);
	}

	pctrl->chip = pm8xxx_gpio_template;
	pctrl->chip.base = -1;
	pctrl->chip.parent = &pdev->dev;
	pctrl->chip.of_node = pdev->dev.of_node;
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

	girq = &pctrl->chip.irq;
	girq->chip = &pm8xxx_irq_chip;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;
	girq->fwnode = of_node_to_fwnode(pctrl->dev->of_node);
	girq->parent_domain = parent_domain;
	girq->child_to_parent_hwirq = pm8xxx_child_to_parent_hwirq;
	girq->populate_parent_alloc_arg = gpiochip_populate_parent_fwspec_fourcell;
	girq->child_offset_to_irq = pm8xxx_child_offset_to_irq;
	girq->child_irq_domain_ops.translate = pm8xxx_domain_translate;

	ret = gpiochip_add_data(&pctrl->chip, pctrl);
	if (ret) {
		dev_err(&pdev->dev, "failed register gpiochip\n");
		return ret;
	}

	/*
	 * For DeviceTree-supported systems, the gpio core checks the
	 * pinctrl's device node for the "gpio-ranges" property.
	 * If it is present, it takes care of adding the pin ranges
	 * for the driver. In this case the driver can skip ahead.
	 *
	 * In order to remain compatible with older, existing DeviceTree
	 * files which don't set the "gpio-ranges" property or systems that
	 * utilize ACPI the driver has to call gpiochip_add_pin_range().
	 */
	if (!of_property_read_bool(pctrl->dev->of_node, "gpio-ranges")) {
		ret = gpiochip_add_pin_range(&pctrl->chip, dev_name(pctrl->dev),
					     0, 0, pctrl->chip.ngpio);
		if (ret) {
			dev_err(pctrl->dev, "failed to add pin range\n");
			goto unregister_gpiochip;
		}
	}

	platform_set_drvdata(pdev, pctrl);

	dev_dbg(&pdev->dev, "Qualcomm pm8xxx gpio driver probed\n");

	return 0;

unregister_gpiochip:
	gpiochip_remove(&pctrl->chip);

	return ret;
}

static int pm8xxx_gpio_remove(struct platform_device *pdev)
{
	struct pm8xxx_gpio *pctrl = platform_get_drvdata(pdev);

	gpiochip_remove(&pctrl->chip);

	return 0;
}

static struct platform_driver pm8xxx_gpio_driver = {
	.driver = {
		.name = "qcom-ssbi-gpio",
		.of_match_table = pm8xxx_gpio_of_match,
	},
	.probe = pm8xxx_gpio_probe,
	.remove = pm8xxx_gpio_remove,
};

module_platform_driver(pm8xxx_gpio_driver);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm PM8xxx GPIO driver");
MODULE_LICENSE("GPL v2");
