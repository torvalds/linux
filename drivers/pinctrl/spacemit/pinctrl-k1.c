// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Yixun Lan <dlan@gentoo.org> */

#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "pinctrl-k1.h"

/*
 * +---------+----------+-----------+--------+--------+----------+--------+
 * |   pull  |   drive  | schmitter |  slew  |  edge  |  strong  |   mux  |
 * | up/down | strength |  trigger  |  rate  | detect |   pull   |  mode  |
 * +---------+----------+-----------+--------+--------+----------+--------+
 *   3 bits     3 bits     2 bits     1 bit    3 bits     1 bit    3 bits
 */

#define PAD_MUX			GENMASK(2, 0)
#define PAD_STRONG_PULL		BIT(3)
#define PAD_EDGE_RISE		BIT(4)
#define PAD_EDGE_FALL		BIT(5)
#define PAD_EDGE_CLEAR		BIT(6)
#define PAD_SLEW_RATE		GENMASK(12, 11)
#define PAD_SLEW_RATE_EN	BIT(7)
#define PAD_SCHMITT		GENMASK(9, 8)
#define PAD_DRIVE		GENMASK(12, 10)
#define PAD_PULLDOWN		BIT(13)
#define PAD_PULLUP		BIT(14)
#define PAD_PULL_EN		BIT(15)

struct spacemit_pin {
	u16				pin;
	u16				flags;
	u8				gpiofunc;
};

struct spacemit_pinctrl {
	struct device				*dev;
	struct pinctrl_dev			*pctl_dev;
	const struct spacemit_pinctrl_data	*data;
	struct pinctrl_desc			pdesc;

	struct mutex				mutex;
	raw_spinlock_t				lock;

	void __iomem				*regs;
};

struct spacemit_pinctrl_data {
	const struct pinctrl_pin_desc   *pins;
	const struct spacemit_pin	*data;
	u16				npins;
};

struct spacemit_pin_mux_config {
	const struct spacemit_pin	*pin;
	u32				config;
};

struct spacemit_pin_drv_strength {
	u8		val;
	u32		mA;
};

/* map pin id to pinctrl register offset, refer MFPR definition */
static unsigned int spacemit_pin_to_offset(unsigned int pin)
{
	unsigned int offset = 0;

	switch (pin) {
	case 0 ... 85:
		offset = pin + 1;
		break;
	case 86 ... 92:
		offset = pin + 37;
		break;
	case 93 ... 97:
		offset = pin + 24;
		break;
	case 98:
		offset = 93;
		break;
	case 99:
		offset = 92;
		break;
	case 100:
		offset = 91;
		break;
	case 101:
		offset = 90;
		break;
	case 102:
		offset = 95;
		break;
	case 103:
		offset = 94;
		break;
	case 104 ... 110:
		offset = pin + 6;
		break;
	case 111 ... 127:
		offset = pin + 20;
		break;
	default:
		break;
	}

	return offset << 2;
}

static inline void __iomem *spacemit_pin_to_reg(struct spacemit_pinctrl *pctrl,
						unsigned int pin)
{
	return pctrl->regs + spacemit_pin_to_offset(pin);
}

static u16 spacemit_dt_get_pin(u32 value)
{
	return value >> 16;
}

static u16 spacemit_dt_get_pin_mux(u32 value)
{
	return value & GENMASK(15, 0);
}

static const struct spacemit_pin *spacemit_get_pin(struct spacemit_pinctrl *pctrl,
						   unsigned long pin)
{
	const struct spacemit_pin *pdata = pctrl->data->data;
	int i;

	for (i = 0; i < pctrl->data->npins; i++) {
		if (pin == pdata[i].pin)
			return &pdata[i];
	}

	return NULL;
}

static inline enum spacemit_pin_io_type spacemit_to_pin_io_type(
	const struct spacemit_pin *pin)
{
	return K1_PIN_GET_IO_TYPE(pin->flags);
}

/* External: IO voltage via external source, can be 1.8V or 3.3V */
static const char * const io_type_desc[] = {
	"None",
	"Fixed/1V8",
	"Fixed/3V3",
	"External",
};

static void spacemit_pctrl_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *seq, unsigned int pin)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct spacemit_pin *spin = spacemit_get_pin(pctrl, pin);
	enum spacemit_pin_io_type type = spacemit_to_pin_io_type(spin);
	void __iomem *reg;
	u32 value;

	seq_printf(seq, "offset: 0x%04x ", spacemit_pin_to_offset(pin));
	seq_printf(seq, "type: %s ", io_type_desc[type]);

	reg = spacemit_pin_to_reg(pctrl, pin);
	value = readl(reg);
	seq_printf(seq, "mux: %ld reg: 0x%04x", (value & PAD_MUX), value);
}

/* use IO high level output current as the table */
static struct spacemit_pin_drv_strength spacemit_ds_1v8_tbl[4] = {
	{ 0, 11 },
	{ 2, 21 },
	{ 4, 32 },
	{ 6, 42 },
};

static struct spacemit_pin_drv_strength spacemit_ds_3v3_tbl[8] = {
	{ 0,  7 },
	{ 2, 10 },
	{ 4, 13 },
	{ 6, 16 },
	{ 1, 19 },
	{ 3, 23 },
	{ 5, 26 },
	{ 7, 29 },
};

static inline u8 spacemit_get_ds_value(struct spacemit_pin_drv_strength *tbl,
				       u32 num, u32 mA)
{
	int i;

	for (i = 0; i < num; i++)
		if (mA <= tbl[i].mA)
			return tbl[i].val;

	return tbl[num - 1].val;
}

static inline u32 spacemit_get_ds_mA(struct spacemit_pin_drv_strength *tbl,
				     u32 num, u32 val)
{
	int i;

	for (i = 0; i < num; i++)
		if (val == tbl[i].val)
			return tbl[i].mA;

	return 0;
}

static inline u8 spacemit_get_driver_strength(enum spacemit_pin_io_type type,
					      u32 mA)
{
	switch (type) {
	case IO_TYPE_1V8:
		return spacemit_get_ds_value(spacemit_ds_1v8_tbl,
					     ARRAY_SIZE(spacemit_ds_1v8_tbl),
					     mA);
	case IO_TYPE_3V3:
		return spacemit_get_ds_value(spacemit_ds_3v3_tbl,
					     ARRAY_SIZE(spacemit_ds_3v3_tbl),
					     mA);
	default:
		return 0;
	}
}

static inline u32 spacemit_get_drive_strength_mA(enum spacemit_pin_io_type type,
						 u32 value)
{
	switch (type) {
	case IO_TYPE_1V8:
		return spacemit_get_ds_mA(spacemit_ds_1v8_tbl,
					  ARRAY_SIZE(spacemit_ds_1v8_tbl),
					  value & 0x6);
	case IO_TYPE_3V3:
		return spacemit_get_ds_mA(spacemit_ds_3v3_tbl,
					  ARRAY_SIZE(spacemit_ds_3v3_tbl),
					  value);
	default:
		return 0;
	}
}

static int spacemit_pctrl_check_power(struct pinctrl_dev *pctldev,
				      struct device_node *dn,
				      struct spacemit_pin_mux_config *pinmuxs,
				      int num_pins, const char *grpname)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pctrl->dev;
	enum spacemit_pin_io_type type;
	u32 power = 0, i;

	of_property_read_u32(dn, "power-source", &power);

	for (i = 0; i < num_pins; i++) {
		type = spacemit_to_pin_io_type(pinmuxs[i].pin);

		if (type != IO_TYPE_EXTERNAL)
			continue;

		switch (power) {
		case PIN_POWER_STATE_1V8:
		case PIN_POWER_STATE_3V3:
			break;
		default:
			dev_err(dev, "group %s has unsupported power\n",
				grpname);
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int spacemit_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					 struct device_node *np,
					 struct pinctrl_map **maps,
					 unsigned int *num_maps)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = pctrl->dev;
	struct device_node *child;
	struct pinctrl_map *map;
	const char **grpnames;
	const char *grpname;
	int ngroups = 0;
	int nmaps = 0;
	int ret;

	for_each_available_child_of_node(np, child)
		ngroups += 1;

	grpnames = devm_kcalloc(dev, ngroups, sizeof(*grpnames), GFP_KERNEL);
	if (!grpnames)
		return -ENOMEM;

	map = kcalloc(ngroups * 2, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	ngroups = 0;
	guard(mutex)(&pctrl->mutex);
	for_each_available_child_of_node_scoped(np, child) {
		struct spacemit_pin_mux_config *pinmuxs;
		unsigned int config, *pins;
		int i, npins;

		npins = of_property_count_u32_elems(child, "pinmux");

		if (npins < 1) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn\n",
				np, child);
			return -EINVAL;
		}

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn",
					 np, child);
		if (!grpname)
			return -ENOMEM;

		grpnames[ngroups++] = grpname;

		pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
		if (!pins)
			return -ENOMEM;

		pinmuxs = devm_kcalloc(dev, npins, sizeof(*pinmuxs), GFP_KERNEL);
		if (!pinmuxs)
			return -ENOMEM;

		for (i = 0; i < npins; i++) {
			ret = of_property_read_u32_index(child, "pinmux",
							 i, &config);

			if (ret)
				return -EINVAL;

			pins[i] = spacemit_dt_get_pin(config);
			pinmuxs[i].config = config;
			pinmuxs[i].pin = spacemit_get_pin(pctrl, pins[i]);

			if (!pinmuxs[i].pin)
				return dev_err_probe(dev, -ENODEV, "failed to get pin %d\n", pins[i]);
		}

		ret = spacemit_pctrl_check_power(pctldev, child, pinmuxs,
						 npins, grpname);
		if (ret < 0)
			return ret;

		map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		map[nmaps].data.mux.function = np->name;
		map[nmaps].data.mux.group = grpname;
		nmaps += 1;

		ret = pinctrl_generic_add_group(pctldev, grpname,
						pins, npins, pinmuxs);
		if (ret < 0)
			return dev_err_probe(dev, ret, "failed to add group %s: %d\n", grpname, ret);

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret)
			return dev_err_probe(dev, ret, "failed to parse pin config of group %s\n",
				grpname);

		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name,
					  grpnames, ngroups, NULL);
	if (ret < 0) {
		pinctrl_utils_free_map(pctldev, map, nmaps);
		return dev_err_probe(dev, ret, "error adding function %s\n", np->name);
	}

	*maps = map;
	*num_maps = nmaps;

	return 0;
}

static const struct pinctrl_ops spacemit_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.pin_dbg_show		= spacemit_pctrl_dbg_show,
	.dt_node_to_map		= spacemit_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int spacemit_pmx_set_mux(struct pinctrl_dev *pctldev,
				unsigned int fsel, unsigned int gsel)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct spacemit_pin_mux_config *configs;
	unsigned int i, mux;
	void __iomem *reg;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	configs = group->data;

	for (i = 0; i < group->grp.npins; i++) {
		const struct spacemit_pin *spin = configs[i].pin;
		u32 value = configs[i].config;

		reg = spacemit_pin_to_reg(pctrl, spin->pin);
		mux = spacemit_dt_get_pin_mux(value);

		guard(raw_spinlock_irqsave)(&pctrl->lock);
		value = readl_relaxed(reg) & ~PAD_MUX;
		writel_relaxed(mux | value, reg);
	}

	return 0;
}

static int spacemit_request_gpio(struct pinctrl_dev *pctldev,
				 struct pinctrl_gpio_range *range,
				 unsigned int pin)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct spacemit_pin *spin = spacemit_get_pin(pctrl, pin);
	void __iomem *reg;

	reg = spacemit_pin_to_reg(pctrl, pin);
	guard(raw_spinlock_irqsave)(&pctrl->lock);
	writel_relaxed(spin->gpiofunc, reg);

	return 0;
}

static const struct pinmux_ops spacemit_pmx_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= spacemit_pmx_set_mux,
	.gpio_request_enable	= spacemit_request_gpio,
	.strict			= true,
};

static int spacemit_pinconf_get(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *config)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	u32 value, arg = 0;

	if (!pin)
		return -EINVAL;

	value = readl(spacemit_pin_to_reg(pctrl, pin));

	switch (param) {
	case PIN_CONFIG_SLEW_RATE:
		if (FIELD_GET(PAD_SLEW_RATE_EN, value))
			arg = FIELD_GET(PAD_SLEW_RATE, value) + 2;
		else
			arg = 0;
		break;
	default:
		return -EINVAL;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

#define ENABLE_DRV_STRENGTH	BIT(1)
#define ENABLE_SLEW_RATE	BIT(2)
static int spacemit_pinconf_generate_config(const struct spacemit_pin *spin,
					    unsigned long *configs,
					    unsigned int num_configs,
					    u32 *value)
{
	enum spacemit_pin_io_type type;
	int i, param;
	u32 v = 0, voltage = 0, arg, val;
	u32 flag = 0, drv_strength, slew_rate;

	if (!spin)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			v &= ~(PAD_PULL_EN | PAD_PULLDOWN | PAD_PULLUP);
			v &= ~PAD_STRONG_PULL;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			v &= ~(PAD_PULLUP | PAD_STRONG_PULL);
			v |= (PAD_PULL_EN | PAD_PULLDOWN);
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			v &= ~PAD_PULLDOWN;
			v |= (PAD_PULL_EN | PAD_PULLUP);

			if (arg == 1)
				v |= PAD_STRONG_PULL;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			flag |= ENABLE_DRV_STRENGTH;
			drv_strength = arg;
			break;
		case PIN_CONFIG_INPUT_SCHMITT:
			v &= ~PAD_SCHMITT;
			v |= FIELD_PREP(PAD_SCHMITT, arg);
			break;
		case PIN_CONFIG_POWER_SOURCE:
			voltage = arg;
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (arg) {
				flag |= ENABLE_SLEW_RATE;
				v |= PAD_SLEW_RATE_EN;
				slew_rate = arg;
			} else {
				v &= ~PAD_SLEW_RATE_EN;
			}
			break;
		default:
			return -EINVAL;
		}
	}

	if (flag & ENABLE_DRV_STRENGTH) {
		type = spacemit_to_pin_io_type(spin);

		/* fix external io type */
		if (type == IO_TYPE_EXTERNAL) {
			switch (voltage) {
			case 1800:
				type = IO_TYPE_1V8;
				break;
			case 3300:
				type = IO_TYPE_3V3;
				break;
			default:
				return -EINVAL;
			}
		}

		val = spacemit_get_driver_strength(type, drv_strength);

		v &= ~PAD_DRIVE;
		v |= FIELD_PREP(PAD_DRIVE, val);
	}

	if (flag & ENABLE_SLEW_RATE) {
		/* check, driver strength & slew rate */
		if (flag & ENABLE_DRV_STRENGTH) {
			val = FIELD_GET(PAD_SLEW_RATE, v) + 2;
			if (slew_rate > 1 && slew_rate != val) {
				pr_err("slew rate conflict with drive strength\n");
				return -EINVAL;
			}
		} else {
			v &= ~PAD_SLEW_RATE;
			slew_rate = slew_rate > 1 ? (slew_rate - 2) : 0;
			v |= FIELD_PREP(PAD_SLEW_RATE, slew_rate);
		}
	}

	*value = v;

	return 0;
}

static int spacemit_pin_set_config(struct spacemit_pinctrl *pctrl,
				   unsigned int pin, u32 value)
{
	const struct spacemit_pin *spin = spacemit_get_pin(pctrl, pin);
	void __iomem *reg;
	unsigned int mux;

	if (!pin)
		return -EINVAL;

	reg = spacemit_pin_to_reg(pctrl, spin->pin);

	guard(raw_spinlock_irqsave)(&pctrl->lock);
	mux = readl_relaxed(reg) & PAD_MUX;
	writel_relaxed(mux | value, reg);

	return 0;
}

static int spacemit_pinconf_set(struct pinctrl_dev *pctldev,
				unsigned int pin, unsigned long *configs,
				unsigned int num_configs)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct spacemit_pin *spin = spacemit_get_pin(pctrl, pin);
	u32 value;

	if (spacemit_pinconf_generate_config(spin, configs, num_configs, &value))
		return -EINVAL;

	return spacemit_pin_set_config(pctrl, pin, value);
}

static int spacemit_pinconf_group_set(struct pinctrl_dev *pctldev,
				      unsigned int gsel,
				      unsigned long *configs,
				      unsigned int num_configs)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct spacemit_pin *spin;
	const struct group_desc *group;
	u32 value;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	spin = spacemit_get_pin(pctrl, group->grp.pins[0]);
	if (spacemit_pinconf_generate_config(spin, configs, num_configs, &value))
		return -EINVAL;

	for (i = 0; i < group->grp.npins; i++)
		spacemit_pin_set_config(pctrl, group->grp.pins[i], value);

	return 0;
}

static void spacemit_pinconf_dbg_pull(struct seq_file *seq, unsigned int value)
{
	u32 normal, strong;

	if (!FIELD_GET(PAD_PULL_EN, value)) {
		seq_puts(seq, ", bias pull disabled");
		return;
	}

	if (FIELD_GET(PAD_PULLDOWN, value))
		seq_puts(seq, ", bias pull down");

	normal = FIELD_GET(PAD_PULLUP, value);
	strong = FIELD_GET(PAD_STRONG_PULL, value);

	if (normal && strong)
		seq_puts(seq, ", bias strong pull up");
	else if (normal)
		seq_puts(seq, ", bias normal pull up");
}

static void spacemit_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				      struct seq_file *seq, unsigned int pin)
{
	struct spacemit_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct spacemit_pin *spin = spacemit_get_pin(pctrl, pin);
	enum spacemit_pin_io_type type = spacemit_to_pin_io_type(spin);
	void __iomem *reg = spacemit_pin_to_reg(pctrl, pin);
	u32 value, tmp, mA;

	value = readl(reg);
	spacemit_pinconf_dbg_pull(seq, value);

	seq_printf(seq, ", io type (%s)", io_type_desc[type]);

	tmp = FIELD_GET(PAD_DRIVE, value);
	if (type == IO_TYPE_1V8 || type == IO_TYPE_3V3) {
		mA = spacemit_get_drive_strength_mA(type, tmp);
		seq_printf(seq, ", drive strength (%d mA)", mA);
	}

	/* drive strength depend on power source, so show all values */
	if (type == IO_TYPE_EXTERNAL)
		seq_printf(seq, ", drive strength (%d or %d mA)",
			   spacemit_get_drive_strength_mA(IO_TYPE_1V8, tmp),
			   spacemit_get_drive_strength_mA(IO_TYPE_3V3, tmp));

	seq_printf(seq, ", register (0x%04x)\n", value);
}

static const struct pinconf_ops spacemit_pinconf_ops = {
	.pin_config_get			= spacemit_pinconf_get,
	.pin_config_set			= spacemit_pinconf_set,
	.pin_config_group_set		= spacemit_pinconf_group_set,
	.pin_config_dbg_show		= spacemit_pinconf_dbg_show,
	.is_generic			= true,
};

static int spacemit_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spacemit_pinctrl *pctrl;
	const struct spacemit_pinctrl_data *pctrl_data;
	int ret;

	pctrl_data = device_get_match_data(dev);
	if (!pctrl_data)
		return -ENODEV;

	if (pctrl_data->npins == 0)
		return dev_err_probe(dev, -EINVAL, "invalid pin data\n");

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->regs))
		return PTR_ERR(pctrl->regs);

	pctrl->pdesc.name = dev_name(dev);
	pctrl->pdesc.pins = pctrl_data->pins;
	pctrl->pdesc.npins = pctrl_data->npins;
	pctrl->pdesc.pctlops = &spacemit_pctrl_ops;
	pctrl->pdesc.pmxops = &spacemit_pmx_ops;
	pctrl->pdesc.confops = &spacemit_pinconf_ops;
	pctrl->pdesc.owner = THIS_MODULE;

	pctrl->data = pctrl_data;
	pctrl->dev = dev;
	raw_spin_lock_init(&pctrl->lock);

	ret = devm_mutex_init(dev, &pctrl->mutex);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, pctrl);

	ret = devm_pinctrl_register_and_init(dev, &pctrl->pdesc,
					     pctrl, &pctrl->pctl_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "fail to register pinctrl driver\n");

	return pinctrl_enable(pctrl->pctl_dev);
}

static const struct pinctrl_pin_desc k1_pin_desc[] = {
	PINCTRL_PIN(0, "GPIO_00"),
	PINCTRL_PIN(1, "GPIO_01"),
	PINCTRL_PIN(2, "GPIO_02"),
	PINCTRL_PIN(3, "GPIO_03"),
	PINCTRL_PIN(4, "GPIO_04"),
	PINCTRL_PIN(5, "GPIO_05"),
	PINCTRL_PIN(6, "GPIO_06"),
	PINCTRL_PIN(7, "GPIO_07"),
	PINCTRL_PIN(8, "GPIO_08"),
	PINCTRL_PIN(9, "GPIO_09"),
	PINCTRL_PIN(10, "GPIO_10"),
	PINCTRL_PIN(11, "GPIO_11"),
	PINCTRL_PIN(12, "GPIO_12"),
	PINCTRL_PIN(13, "GPIO_13"),
	PINCTRL_PIN(14, "GPIO_14"),
	PINCTRL_PIN(15, "GPIO_15"),
	PINCTRL_PIN(16, "GPIO_16"),
	PINCTRL_PIN(17, "GPIO_17"),
	PINCTRL_PIN(18, "GPIO_18"),
	PINCTRL_PIN(19, "GPIO_19"),
	PINCTRL_PIN(20, "GPIO_20"),
	PINCTRL_PIN(21, "GPIO_21"),
	PINCTRL_PIN(22, "GPIO_22"),
	PINCTRL_PIN(23, "GPIO_23"),
	PINCTRL_PIN(24, "GPIO_24"),
	PINCTRL_PIN(25, "GPIO_25"),
	PINCTRL_PIN(26, "GPIO_26"),
	PINCTRL_PIN(27, "GPIO_27"),
	PINCTRL_PIN(28, "GPIO_28"),
	PINCTRL_PIN(29, "GPIO_29"),
	PINCTRL_PIN(30, "GPIO_30"),
	PINCTRL_PIN(31, "GPIO_31"),
	PINCTRL_PIN(32, "GPIO_32"),
	PINCTRL_PIN(33, "GPIO_33"),
	PINCTRL_PIN(34, "GPIO_34"),
	PINCTRL_PIN(35, "GPIO_35"),
	PINCTRL_PIN(36, "GPIO_36"),
	PINCTRL_PIN(37, "GPIO_37"),
	PINCTRL_PIN(38, "GPIO_38"),
	PINCTRL_PIN(39, "GPIO_39"),
	PINCTRL_PIN(40, "GPIO_40"),
	PINCTRL_PIN(41, "GPIO_41"),
	PINCTRL_PIN(42, "GPIO_42"),
	PINCTRL_PIN(43, "GPIO_43"),
	PINCTRL_PIN(44, "GPIO_44"),
	PINCTRL_PIN(45, "GPIO_45"),
	PINCTRL_PIN(46, "GPIO_46"),
	PINCTRL_PIN(47, "GPIO_47"),
	PINCTRL_PIN(48, "GPIO_48"),
	PINCTRL_PIN(49, "GPIO_49"),
	PINCTRL_PIN(50, "GPIO_50"),
	PINCTRL_PIN(51, "GPIO_51"),
	PINCTRL_PIN(52, "GPIO_52"),
	PINCTRL_PIN(53, "GPIO_53"),
	PINCTRL_PIN(54, "GPIO_54"),
	PINCTRL_PIN(55, "GPIO_55"),
	PINCTRL_PIN(56, "GPIO_56"),
	PINCTRL_PIN(57, "GPIO_57"),
	PINCTRL_PIN(58, "GPIO_58"),
	PINCTRL_PIN(59, "GPIO_59"),
	PINCTRL_PIN(60, "GPIO_60"),
	PINCTRL_PIN(61, "GPIO_61"),
	PINCTRL_PIN(62, "GPIO_62"),
	PINCTRL_PIN(63, "GPIO_63"),
	PINCTRL_PIN(64, "GPIO_64"),
	PINCTRL_PIN(65, "GPIO_65"),
	PINCTRL_PIN(66, "GPIO_66"),
	PINCTRL_PIN(67, "GPIO_67"),
	PINCTRL_PIN(68, "GPIO_68"),
	PINCTRL_PIN(69, "GPIO_69"),
	PINCTRL_PIN(70, "GPIO_70/PRI_DTI"),
	PINCTRL_PIN(71, "GPIO_71/PRI_TMS"),
	PINCTRL_PIN(72, "GPIO_72/PRI_TCK"),
	PINCTRL_PIN(73, "GPIO_73/PRI_TDO"),
	PINCTRL_PIN(74, "GPIO_74"),
	PINCTRL_PIN(75, "GPIO_75"),
	PINCTRL_PIN(76, "GPIO_76"),
	PINCTRL_PIN(77, "GPIO_77"),
	PINCTRL_PIN(78, "GPIO_78"),
	PINCTRL_PIN(79, "GPIO_79"),
	PINCTRL_PIN(80, "GPIO_80"),
	PINCTRL_PIN(81, "GPIO_81"),
	PINCTRL_PIN(82, "GPIO_82"),
	PINCTRL_PIN(83, "GPIO_83"),
	PINCTRL_PIN(84, "GPIO_84"),
	PINCTRL_PIN(85, "GPIO_85"),
	PINCTRL_PIN(86, "GPIO_86"),
	PINCTRL_PIN(87, "GPIO_87"),
	PINCTRL_PIN(88, "GPIO_88"),
	PINCTRL_PIN(89, "GPIO_89"),
	PINCTRL_PIN(90, "GPIO_90"),
	PINCTRL_PIN(91, "GPIO_91"),
	PINCTRL_PIN(92, "GPIO_92"),
	PINCTRL_PIN(93, "GPIO_93/PWR_SCL"),
	PINCTRL_PIN(94, "GPIO_94/PWR_SDA"),
	PINCTRL_PIN(95, "GPIO_95/VCX0_EN"),
	PINCTRL_PIN(96, "GPIO_96/DVL0"),
	PINCTRL_PIN(97, "GPIO_97/DVL1"),
	PINCTRL_PIN(98,  "GPIO_98/QSPI_DAT3"),
	PINCTRL_PIN(99,  "GPIO_99/QSPI_DAT2"),
	PINCTRL_PIN(100, "GPIO_100/QSPI_DAT1"),
	PINCTRL_PIN(101, "GPIO_101/QSPI_DAT0"),
	PINCTRL_PIN(102, "GPIO_102/QSPI_CLK"),
	PINCTRL_PIN(103, "GPIO_103/QSPI_CS1"),
	PINCTRL_PIN(104, "GPIO_104/MMC1_DAT3"),
	PINCTRL_PIN(105, "GPIO_105/MMC1_DAT2"),
	PINCTRL_PIN(106, "GPIO_106/MMC1_DAT1"),
	PINCTRL_PIN(107, "GPIO_107/MMC1_DAT0"),
	PINCTRL_PIN(108, "GPIO_108/MMC1_CMD"),
	PINCTRL_PIN(109, "GPIO_109/MMC1_CLK"),
	PINCTRL_PIN(110, "GPIO_110"),
	PINCTRL_PIN(111, "GPIO_111"),
	PINCTRL_PIN(112, "GPIO_112"),
	PINCTRL_PIN(113, "GPIO_113"),
	PINCTRL_PIN(114, "GPIO_114"),
	PINCTRL_PIN(115, "GPIO_115"),
	PINCTRL_PIN(116, "GPIO_116"),
	PINCTRL_PIN(117, "GPIO_117"),
	PINCTRL_PIN(118, "GPIO_118"),
	PINCTRL_PIN(119, "GPIO_119"),
	PINCTRL_PIN(120, "GPIO_120"),
	PINCTRL_PIN(121, "GPIO_121"),
	PINCTRL_PIN(122, "GPIO_122"),
	PINCTRL_PIN(123, "GPIO_123"),
	PINCTRL_PIN(124, "GPIO_124"),
	PINCTRL_PIN(125, "GPIO_125"),
	PINCTRL_PIN(126, "GPIO_126"),
	PINCTRL_PIN(127, "GPIO_127"),
};

static const struct spacemit_pin k1_pin_data[ARRAY_SIZE(k1_pin_desc)] = {
	K1_FUNC_PIN(0, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(1, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(2, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(3, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(4, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(5, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(6, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(7, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(8, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(9, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(10, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(11, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(12, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(13, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(14, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(15, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(16, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(17, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(18, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(19, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(20, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(21, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(22, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(23, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(24, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(25, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(26, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(27, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(28, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(29, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(30, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(31, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(32, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(33, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(34, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(35, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(36, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(37, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(38, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(39, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(40, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(41, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(42, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(43, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(44, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(45, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(46, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(47, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(48, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(49, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(50, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(51, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(52, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(53, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(54, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(55, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(56, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(57, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(58, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(59, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(60, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(61, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(62, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(63, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(64, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(65, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(66, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(67, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(68, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(69, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(70, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(71, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(72, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(73, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(74, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(75, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(76, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(77, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(78, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(79, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(80, 0, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(81, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(82, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(83, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(84, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(85, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(86, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(87, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(88, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(89, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(90, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(91, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(92, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(93, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(94, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(95, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(96, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(97, 1, IO_TYPE_1V8),
	K1_FUNC_PIN(98, 1, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(99, 1, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(100, 1, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(101, 1, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(102, 1, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(103, 1, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(104, 4, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(105, 4, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(106, 4, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(107, 4, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(108, 4, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(109, 4, IO_TYPE_EXTERNAL),
	K1_FUNC_PIN(110, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(111, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(112, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(113, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(114, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(115, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(116, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(117, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(118, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(119, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(120, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(121, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(122, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(123, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(124, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(125, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(126, 0, IO_TYPE_1V8),
	K1_FUNC_PIN(127, 0, IO_TYPE_1V8),
};

static const struct spacemit_pinctrl_data k1_pinctrl_data = {
	.pins = k1_pin_desc,
	.data = k1_pin_data,
	.npins = ARRAY_SIZE(k1_pin_desc),
};

static const struct of_device_id k1_pinctrl_ids[] = {
	{ .compatible = "spacemit,k1-pinctrl", .data = &k1_pinctrl_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, k1_pinctrl_ids);

static struct platform_driver k1_pinctrl_driver = {
	.probe	= spacemit_pinctrl_probe,
	.driver	= {
		.name			= "k1-pinctrl",
		.suppress_bind_attrs	= true,
		.of_match_table		= k1_pinctrl_ids,
	},
};
builtin_platform_driver(k1_pinctrl_driver);

MODULE_AUTHOR("Yixun Lan <dlan@gentoo.org>");
MODULE_DESCRIPTION("Pinctrl driver for the SpacemiT K1 SoC");
MODULE_LICENSE("GPL");
