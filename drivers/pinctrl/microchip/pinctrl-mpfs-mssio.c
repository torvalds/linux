// SPDX-License-Identifier: GPL-2.0

#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinconf.h"
#include "../pinmux.h"

#define MPFS_PINCTRL_PAD_MUX_MASK GENMASK(3, 0)

#define MPFS_PINCTRL_IOCFG_MASK GENMASK(14, 0)
#define MPFS_PINCTRL_IBUFMD_MASK GENMASK(2, 0)
#define MPFS_PINCTRL_DRV_MASK GENMASK(6, 3)
#define MPFS_PINCTRL_CLAMP BIT(7)
#define MPFS_PINCTRL_ENHYST BIT(8)
#define MPFS_PINCTRL_LOCKDN BIT(9)
#define MPFS_PINCTRL_WPD BIT(10)
#define MPFS_PINCTRL_WPU BIT(11)
#define MPFS_PINCTRL_PULL_MASK GENMASK(11, 10)
#define MPFS_PINCTRL_LP_PERSIST_EN BIT(12)
#define MPFS_PINCTRL_LP_BYPASS_EN BIT(13)

#define MPFS_PINCTRL_MSSIO_BANK2_CFG_CR 0x1c4
#define MPFS_PINCTRL_MSSIO_BANK4_CFG_CR 0x1c8
#define MPFS_PINCTRL_BANK_VOLTAGE_MASK GENMASK(19, 16)

#define MPFS_PINCTRL_IOCFG01_REG 0x234

#define MPFS_PINCTRL_INTER_BANK_GAP 0x4

#define MPFS_PINCTRL_BANK2_START 14

#define MPFS_PINCTRL_LOCKDOWN (PIN_CONFIG_END + 1)
#define MPFS_PINCTRL_CLAMP_DIODE (PIN_CONFIG_END + 2)
#define MPFS_PINCTRL_IBUFMD (PIN_CONFIG_END + 3)

struct mpfs_pinctrl_mux_config {
	u8 pin;
	u8 function;
};

struct mpfs_pinctrl {
	struct pinctrl_dev *pctrl;
	struct device *dev;
	struct regmap *regmap;
	struct regmap *sysreg_regmap;
	struct mutex mutex;
	struct pinctrl_desc desc;
};

struct mpfs_pinctrl_drive_strength {
	u8 ma;
	u8 val;
};

struct mpfs_pinctrl_bank_voltage {
	u32 uv;
	u8 val;
};

static struct mpfs_pinctrl_drive_strength mpfs_pinctrl_drive_strengths[8] = {
	{ .ma = 2, .val = 2 },
	{ .ma = 4, .val = 3 },
	{ .ma = 6, .val = 4 },
	{ .ma = 8, .val = 5 },
	{ .ma = 10, .val = 6 },
	{ .ma = 12, .val = 7 },
	{ .ma = 16, .val = 10 },
	{ .ma = 20, .val = 12 },
};
static struct mpfs_pinctrl_bank_voltage mpfs_pinctrl_bank_voltages[8] = {
	{ .uv = 1200000, .val = 0 },
	{ .uv = 1500000, .val = 2 },
	{ .uv = 1800000, .val = 4 },
	{ .uv = 2500000, .val = 6 },
	{ .uv = 3300000, .val = 8 },
	{ .uv = 0, .val = 0x3f }, // pin unused
};

static int mpfs_pinctrl_get_drive_strength_ma(u32 drive_strength)
{
	size_t num = ARRAY_SIZE(mpfs_pinctrl_drive_strengths);

	for (int i = 0; i < num; i++)
		if (drive_strength == mpfs_pinctrl_drive_strengths[i].val)
			return mpfs_pinctrl_drive_strengths[i].ma;

	return -EINVAL;
}

static int mpfs_pinctrl_get_drive_strength_val(u32 drive_strength_ma)
{
	size_t num = ARRAY_SIZE(mpfs_pinctrl_drive_strengths);

	if (!drive_strength_ma)
		return -EINVAL;

	for (int i = 0; i < num; i++)
		if (drive_strength_ma <= mpfs_pinctrl_drive_strengths[i].ma)
			return mpfs_pinctrl_drive_strengths[i].val;

	return mpfs_pinctrl_drive_strengths[num - 1].val;
}

static int mpfs_pinctrl_get_bank_voltage_uv(u32 bank_voltage)
{
	size_t num = ARRAY_SIZE(mpfs_pinctrl_bank_voltages);

	for (int i = 0; i < num; i++)
		if (bank_voltage == mpfs_pinctrl_bank_voltages[i].val)
			return mpfs_pinctrl_bank_voltages[i].uv;

	return -EINVAL;
}

static int mpfs_pinctrl_get_bank_voltage_val(u32 bank_voltage_uv)
{
	size_t num = ARRAY_SIZE(mpfs_pinctrl_bank_voltages);

	for (int i = 0; i < num; i++)
		if (bank_voltage_uv <= mpfs_pinctrl_bank_voltages[i].uv)
			return mpfs_pinctrl_bank_voltages[i].val;

	return -EINVAL;
}

static u32 mpfs_pinctrl_pin_to_bank_voltage(struct mpfs_pinctrl *pctrl, unsigned int pin)
{
	u32 bank_voltage, val;

	if (pin < MPFS_PINCTRL_BANK2_START)
		regmap_read(pctrl->sysreg_regmap, MPFS_PINCTRL_MSSIO_BANK4_CFG_CR, &val);
	else
		regmap_read(pctrl->sysreg_regmap, MPFS_PINCTRL_MSSIO_BANK2_CFG_CR, &val);

	bank_voltage = FIELD_GET(MPFS_PINCTRL_BANK_VOLTAGE_MASK, val);

	return mpfs_pinctrl_get_bank_voltage_uv(bank_voltage);
}

static void mpfs_pinctrl_set_bank_voltage(struct mpfs_pinctrl *pctrl, unsigned int pin,
					  u32 bank_voltage)
{
	u32 val = FIELD_PREP(MPFS_PINCTRL_BANK_VOLTAGE_MASK, bank_voltage);

	if (pin < MPFS_PINCTRL_BANK2_START)
		regmap_assign_bits(pctrl->sysreg_regmap, MPFS_PINCTRL_MSSIO_BANK4_CFG_CR,
				   MPFS_PINCTRL_BANK_VOLTAGE_MASK, val);
	else
		regmap_assign_bits(pctrl->sysreg_regmap, MPFS_PINCTRL_MSSIO_BANK2_CFG_CR,
				   MPFS_PINCTRL_BANK_VOLTAGE_MASK, val);
}

static char *mpfs_pinctrl_function_names[] = {
	"sd",
	"emmc",
	"qspi",
	"spi",
	"usb",
	"uart",
	"i2c",
	"can",
	"mdio",
	"misc",
	"reserved",
	"gpio",
	"fabric test",
	"tied-low",
	"tied-high",
	"tristate"
};

static int mpfs_pinctrl_function_map(const char *function)
{
	size_t num = ARRAY_SIZE(mpfs_pinctrl_function_names);

	for (int i = 0; i < num; i++)
		if (!strcmp(function, mpfs_pinctrl_function_names[i]))
			return i;

	return -EINVAL;
}

static const struct pinconf_generic_params mpfs_pinctrl_custom_bindings[] = {
	{ "microchip,clamp-diode", MPFS_PINCTRL_CLAMP_DIODE, 1 },
	{ "microchip,ibufmd", MPFS_PINCTRL_IBUFMD, 0x0 },
};

static int mpfs_pinctrl_pin_to_iomux_offset(unsigned int pin)
{
	int offset;

	switch (pin) {
	case 0 ... 7:
		offset = pin * 4;
		break;
	case 8 ... 13:
		offset = (pin - 8) * 4;
		break;
	case 14 ... 21:
		offset = (pin - 14) * 4;
		break;
	case 22 ... 29:
		offset = (pin - 22) * 4;
		break;
	case 30 ... 37:
		offset = (pin - 30) * 4;
		break;
	default:
		offset = -EINVAL;
	}

	return offset;
}

static int mpfs_pinctrl_pin_to_iomux_reg(unsigned int pin)
{
	int reg;

	switch (pin) {
	case 0 ... 7:
		reg = 0x204;
		break;
	case 8 ... 13:
		reg = 0x208;
		break;
	case 14 ... 21:
		reg = 0x20c;
		break;
	case 22 ... 29:
		reg = 0x210;
		break;
	case 30 ... 37:
		reg = 0x214;
		break;
	default:
		reg = -EINVAL;
	}

	return reg;
}

static int mpfs_pinctrl_pin_to_iocfg_reg(unsigned int pin)
{
	u32 reg = MPFS_PINCTRL_IOCFG01_REG;

	if (pin >= MPFS_PINCTRL_BANK2_START)
		reg += MPFS_PINCTRL_INTER_BANK_GAP;

	// 2 pins per 32-bit register
	reg += (pin / 2) * 0x4;

	return reg;
}

static int mpfs_pinctrl_pin_to_iocfg_offset(unsigned int pin)
{
	return 16 * (pin % 2);
}

static void mpfs_pinctrl_dbg_show(struct pinctrl_dev *pctrl_dev, struct seq_file *seq,
				  unsigned int pin)
{
	struct mpfs_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 func;
	int reg, offset;

	reg = mpfs_pinctrl_pin_to_iomux_reg(pin);
	offset = mpfs_pinctrl_pin_to_iomux_offset(pin);

	seq_printf(seq, "reg: %x, offset: %u ", reg, offset);
	seq_printf(seq, "pin: %u ", pin);

	if (reg < 0 || offset < 0)
		return;

	regmap_read(pctrl->regmap, reg, &func);
	func = (func >> offset) & MPFS_PINCTRL_PAD_MUX_MASK;
	seq_printf(seq, "func: %s (%x)\n", mpfs_pinctrl_function_names[func], func);
}

static const struct pinctrl_ops mpfs_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name	= pinctrl_generic_get_group_name,
	.get_group_pins	= pinctrl_generic_get_group_pins,
	.pin_dbg_show = mpfs_pinctrl_dbg_show,
	.dt_node_to_map = pinctrl_generic_pins_function_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int mpfs_pinctrl_set_pin_func(struct mpfs_pinctrl *pctrl, u8 pin, u8 function)
{
	struct device *dev = pctrl->dev;
	int reg, offset;
	u32 func, mask;

	reg = mpfs_pinctrl_pin_to_iomux_reg(pin);
	offset = mpfs_pinctrl_pin_to_iomux_offset(pin);

	func = function << offset;
	mask = MPFS_PINCTRL_PAD_MUX_MASK << offset;

	dev_dbg(dev, "Setting pin %u. reg: %x offset %u func %x\n", pin, reg, offset, func);

	if (reg < 0 || offset < 0)
		return -EINVAL;

	regmap_update_bits(pctrl->regmap, reg, mask, func);

	return 0;
}

static int mpfs_pinctrl_set_mux(struct pinctrl_dev *pctrl_dev, unsigned int fsel,
				unsigned int gsel)
{
	struct mpfs_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct group_desc *group;
	const char **functions;

	group = pinctrl_generic_get_group(pctrl_dev, gsel);
	if (!group)
		return -EINVAL;

	functions = group->data;

	for (int i = 0; i < group->grp.npins; i++) {
		int function;

		function = mpfs_pinctrl_function_map(functions[i]);
		if (function < 0) {
			dev_err(pctrl->dev, "invalid function %s\n", functions[i]);
			return function;
		}

		mpfs_pinctrl_set_pin_func(pctrl, group->grp.pins[i], function);
	}

	return 0;
}

static const struct pinmux_ops mpfs_pinctrl_pinmux_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= mpfs_pinctrl_set_mux,
};

static int mpfs_pinctrl_pinconf_get(struct pinctrl_dev *pctrl_dev, unsigned int pin,
				    unsigned long *config)
{
	struct mpfs_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	int param = pinconf_to_config_param(*config);
	int reg = mpfs_pinctrl_pin_to_iocfg_reg(pin);
	int val;
	u32 arg;
	u8 str;

	regmap_read(pctrl->regmap, reg, &val);

	val = val >> mpfs_pinctrl_pin_to_iocfg_offset(pin);
	val = val & MPFS_PINCTRL_IOCFG_MASK;

	switch (param) {
	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (!(val & MPFS_PINCTRL_WPD))
			return -EINVAL;

		if (!(val & MPFS_PINCTRL_WPU))
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (!(val & MPFS_PINCTRL_WPD))
			return -EINVAL;

		if (val & MPFS_PINCTRL_WPU)
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (!(val & MPFS_PINCTRL_WPU))
			return -EINVAL;

		if (val & MPFS_PINCTRL_WPD)
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		if (val & MPFS_PINCTRL_PULL_MASK)
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		str = FIELD_GET(MPFS_PINCTRL_DRV_MASK, val);
		if (!str)
			return -EINVAL;

		arg = mpfs_pinctrl_get_drive_strength_ma(str);
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!FIELD_GET(MPFS_PINCTRL_ENHYST, val))
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_PERSIST_STATE:
		if (!FIELD_GET(MPFS_PINCTRL_LP_PERSIST_EN, val))
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_MODE_LOW_POWER:
		if (!FIELD_GET(MPFS_PINCTRL_LP_BYPASS_EN, val))
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		arg = mpfs_pinctrl_pin_to_bank_voltage(pctrl, pin);
		break;
	case MPFS_PINCTRL_CLAMP_DIODE:
		if (!FIELD_GET(MPFS_PINCTRL_CLAMP, val))
			return -EINVAL;

		arg = 1;
		break;
	case MPFS_PINCTRL_LOCKDOWN:
		/*
		 * Lockdown is a read-only configuration, it'll get set if the
		 * tamper unit triggers global lockdown and lockdown has been
		 * set in the MSS Configurator for the bank a pin belongs to.
		 */
		if (!FIELD_GET(MPFS_PINCTRL_LOCKDN, val))
			return -EINVAL;

		arg = 1;
		break;
	case MPFS_PINCTRL_IBUFMD:
		arg = FIELD_GET(MPFS_PINCTRL_IBUFMD_MASK, val);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int mpfs_pinctrl_pinconf_generate_config(struct mpfs_pinctrl *pctrl, unsigned int pin,
						unsigned long *configs, unsigned int num_configs,
						u32 *value, u32 *bank_voltage)
{
	u32 val = 0;

	for (int i = 0; i < num_configs; i++) {
		int param, tmp;
		u32 arg;

		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_BUS_HOLD:
			val |= MPFS_PINCTRL_PULL_MASK;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			val &= ~MPFS_PINCTRL_PULL_MASK;
			val |= MPFS_PINCTRL_WPD;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			val &= ~MPFS_PINCTRL_PULL_MASK;
			val |= MPFS_PINCTRL_WPU;
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			val &= ~MPFS_PINCTRL_PULL_MASK;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			tmp = mpfs_pinctrl_get_drive_strength_val(arg);
			if (tmp < 0)
				return tmp;

			val |= FIELD_PREP(MPFS_PINCTRL_DRV_MASK, tmp);
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if (!arg)
				break;
			val |= MPFS_PINCTRL_ENHYST;
			break;
		case PIN_CONFIG_PERSIST_STATE:
			val |= MPFS_PINCTRL_LP_PERSIST_EN;
			break;
		case PIN_CONFIG_MODE_LOW_POWER:
			if (arg)
				val |= MPFS_PINCTRL_LP_BYPASS_EN;
			break;
		case PIN_CONFIG_POWER_SOURCE:
			tmp = mpfs_pinctrl_get_bank_voltage_val(arg);
			if (tmp < 0)
				return tmp;

			*bank_voltage = tmp;
			break;
		case MPFS_PINCTRL_CLAMP_DIODE:
			val |= MPFS_PINCTRL_CLAMP;
			break;
		case MPFS_PINCTRL_IBUFMD:
			val |= FIELD_PREP(MPFS_PINCTRL_IBUFMD_MASK, arg);
			break;
		default:
			dev_err(pctrl->dev, "config %u not supported\n", param);
			return -ENOTSUPP;
		}
	}

	*value = val;
	return 0;
}

static int mpfs_pinctrl_pin_set_config(struct mpfs_pinctrl *pctrl, unsigned int pin, u32 config)
{
	int reg = mpfs_pinctrl_pin_to_iocfg_reg(pin);
	int offset = mpfs_pinctrl_pin_to_iocfg_offset(pin);
	u32 val, mask;

	mask = MPFS_PINCTRL_IOCFG_MASK << offset;
	val = config << offset;

	regmap_update_bits(pctrl->regmap, reg, mask, val);

	return 0;
}

static int mpfs_pinctrl_pinconf_set(struct pinctrl_dev *pctrl_dev, unsigned int pin,
				    unsigned long *configs, unsigned int num_configs)
{
	struct mpfs_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 val, bank_voltage = 0;
	int ret;

	ret = mpfs_pinctrl_pinconf_generate_config(pctrl, pin, configs, num_configs, &val,
						   &bank_voltage);
	if (ret)
		return ret;

	ret = mpfs_pinctrl_pin_set_config(pctrl, pin, val);
	if (ret)
		return ret;

	if (bank_voltage)
		mpfs_pinctrl_set_bank_voltage(pctrl, pin, bank_voltage);

	return 0;
}

static int mpfs_pinctrl_pinconf_group_set(struct pinctrl_dev *pctrl_dev, unsigned int gsel,
					  unsigned long *configs, unsigned int num_configs)
{
	struct mpfs_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	const struct group_desc *group;
	unsigned int pin;
	u32 val, bank_voltage = 0;
	int ret;

	group = pinctrl_generic_get_group(pctrl_dev, gsel);
	if (!group)
		return -EINVAL;

	/*
	 * Assume that the first pin in a group is representative, as the mss
	 * configurator doesn't allow splitting a function between two banks.
	 */
	pin = group->grp.pins[0];

	ret = mpfs_pinctrl_pinconf_generate_config(pctrl, pin, configs, num_configs, &val,
						   &bank_voltage);
	if (ret)
		return ret;

	for (int i = 0; i < group->grp.npins; i++)
		mpfs_pinctrl_pin_set_config(pctrl, group->grp.pins[i], val);

	if (bank_voltage)
		mpfs_pinctrl_set_bank_voltage(pctrl, group->grp.pins[0], bank_voltage);

	return 0;
}

static void mpfs_pinctrl_pinconf_dbg_show(struct pinctrl_dev *pctrl_dev, struct seq_file *seq,
					  unsigned int pin)
{
	struct mpfs_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctrl_dev);
	u32 val;
	int reg, offset;

	reg = mpfs_pinctrl_pin_to_iocfg_reg(pin);
	offset = mpfs_pinctrl_pin_to_iocfg_offset(pin);

	seq_printf(seq, "pin: %u ", pin);
	seq_printf(seq, "reg: %x offset: %u ", reg, offset);

	if (reg < 0 || offset < 0)
		return;

	regmap_read(pctrl->regmap, reg, &val);
	val = (val & (MPFS_PINCTRL_IOCFG_MASK << offset)) >> offset;
	seq_printf(seq, "val: %x\n", val);
}

static const struct pinconf_ops mpfs_pinctrl_pinconf_ops = {
	.pin_config_get			= mpfs_pinctrl_pinconf_get,
	.pin_config_set			= mpfs_pinctrl_pinconf_set,
	.pin_config_group_set		= mpfs_pinctrl_pinconf_group_set,
	.pin_config_dbg_show		= mpfs_pinctrl_pinconf_dbg_show,
	.is_generic			= true,
};

static const struct pinctrl_pin_desc mpfs_pinctrl_pins[] = {
	PINCTRL_PIN(0,  "bank 4 0"),
	PINCTRL_PIN(1,  "bank 4 1"),
	PINCTRL_PIN(2,  "bank 4 2"),
	PINCTRL_PIN(3,  "bank 4 3"),
	PINCTRL_PIN(4,  "bank 4 4"),
	PINCTRL_PIN(5,  "bank 4 5"),
	PINCTRL_PIN(6,  "bank 4 6"),
	PINCTRL_PIN(7,  "bank 4 7"),
	PINCTRL_PIN(8,  "bank 4 8"),
	PINCTRL_PIN(9,  "bank 4 9"),
	PINCTRL_PIN(10, "bank 4 10"),
	PINCTRL_PIN(11, "bank 4 11"),
	PINCTRL_PIN(12, "bank 4 12"),
	PINCTRL_PIN(13, "bank 4 13"),

	PINCTRL_PIN(14, "bank 2 0"),
	PINCTRL_PIN(15, "bank 2 1"),
	PINCTRL_PIN(16, "bank 2 2"),
	PINCTRL_PIN(17, "bank 2 3"),
	PINCTRL_PIN(18, "bank 2 4"),
	PINCTRL_PIN(19, "bank 2 5"),
	PINCTRL_PIN(20, "bank 2 6"),
	PINCTRL_PIN(21, "bank 2 7"),
	PINCTRL_PIN(22, "bank 2 8"),
	PINCTRL_PIN(23, "bank 2 9"),
	PINCTRL_PIN(24, "bank 2 10"),
	PINCTRL_PIN(25, "bank 2 11"),
	PINCTRL_PIN(26, "bank 2 12"),
	PINCTRL_PIN(27, "bank 2 13"),
	PINCTRL_PIN(28, "bank 2 14"),
	PINCTRL_PIN(29, "bank 2 15"),
	PINCTRL_PIN(30, "bank 2 16"),
	PINCTRL_PIN(31, "bank 2 17"),
	PINCTRL_PIN(32, "bank 2 18"),
	PINCTRL_PIN(33, "bank 2 19"),
	PINCTRL_PIN(34, "bank 2 20"),
	PINCTRL_PIN(35, "bank 2 21"),
	PINCTRL_PIN(36, "bank 2 22"),
	PINCTRL_PIN(37, "bank 2 23"),
};

static int mpfs_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mpfs_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->regmap = device_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(pctrl->regmap))
		dev_err_probe(dev, PTR_ERR(pctrl->regmap), "Failed to find syscon regmap\n");

	pctrl->sysreg_regmap = syscon_regmap_lookup_by_compatible("microchip,mpfs-sysreg-scb");
	if (IS_ERR(pctrl->sysreg_regmap))
		return PTR_ERR(pctrl->sysreg_regmap);

	pctrl->desc.name = dev_name(dev);
	pctrl->desc.pins = mpfs_pinctrl_pins;
	pctrl->desc.npins = ARRAY_SIZE(mpfs_pinctrl_pins);
	pctrl->desc.pctlops = &mpfs_pinctrl_ops;
	pctrl->desc.pmxops = &mpfs_pinctrl_pinmux_ops;
	pctrl->desc.confops = &mpfs_pinctrl_pinconf_ops;
	pctrl->desc.owner = THIS_MODULE;
	pctrl->desc.num_custom_params = ARRAY_SIZE(mpfs_pinctrl_custom_bindings);
	pctrl->desc.custom_params = mpfs_pinctrl_custom_bindings;

	pctrl->dev = dev;

	ret = devm_mutex_init(dev, &pctrl->mutex);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pctrl);

	pctrl->pctrl = devm_pinctrl_register(&pdev->dev, &pctrl->desc, pctrl);
	if (IS_ERR(pctrl->pctrl))
		return PTR_ERR(pctrl->pctrl);

	return 0;
}

static const struct of_device_id mpfs_pinctrl_of_match[] = {
	{ .compatible = "microchip,mpfs-pinctrl-mssio" },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_pinctrl_of_match);

static struct platform_driver mpfs_pinctrl_driver = {
	.driver = {
		.name = "mpfs-pinctrl",
		.of_match_table = mpfs_pinctrl_of_match,
	},
	.probe = mpfs_pinctrl_probe,
};
module_platform_driver(mpfs_pinctrl_driver);

MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_DESCRIPTION("Polarfire SoC mssio pinctrl driver");
MODULE_LICENSE("GPL");
