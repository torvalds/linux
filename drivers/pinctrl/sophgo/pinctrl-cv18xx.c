// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo CV18XX SoCs pinctrl driver.
 *
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/bsearch.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/pinctrl-cv18xx.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinconf.h"
#include "../pinmux.h"
#include "pinctrl-cv18xx.h"

struct cv1800_pinctrl {
	struct device				*dev;
	struct pinctrl_dev			*pctl_dev;
	const struct cv1800_pinctrl_data	*data;
	struct pinctrl_desc			pdesc;
	u32					*power_cfg;

	struct mutex				mutex;
	raw_spinlock_t				lock;

	void __iomem				*regs[2];
};

struct cv1800_pin_mux_config {
	struct cv1800_pin	*pin;
	u32			config;
};

static unsigned int cv1800_dt_get_pin(u32 value)
{
	return value & GENMASK(15, 0);
}

static unsigned int cv1800_dt_get_pin_mux(u32 value)
{
	return (value >> 16) & GENMASK(7, 0);
}

static unsigned int cv1800_dt_get_pin_mux2(u32 value)
{
	return (value >> 24) & GENMASK(7, 0);
}

#define cv1800_pinctrl_get_component_addr(pctrl, _comp)		\
	((pctrl)->regs[(_comp)->area] + (_comp)->offset)

static int cv1800_cmp_pin(const void *key, const void *pivot)
{
	const struct cv1800_pin *pin = pivot;
	int pin_id = (long)key;
	int pivid = pin->pin;

	return pin_id - pivid;
}

static int cv1800_set_power_cfg(struct cv1800_pinctrl *pctrl,
				u8 domain, u32 cfg)
{
	if (domain >= pctrl->data->npd)
		return -ENOTSUPP;

	if (pctrl->power_cfg[domain] && pctrl->power_cfg[domain] != cfg)
		return -EINVAL;

	pctrl->power_cfg[domain] = cfg;

	return 0;
}

static int cv1800_get_power_cfg(struct cv1800_pinctrl *pctrl,
				u8 domain)
{
	return pctrl->power_cfg[domain];
}

static struct cv1800_pin *cv1800_get_pin(struct cv1800_pinctrl *pctrl,
					 unsigned long pin)
{
	return bsearch((void *)pin, pctrl->data->pindata, pctrl->data->npins,
		       sizeof(struct cv1800_pin), cv1800_cmp_pin);
}

#define PIN_BGA_ID_OFFSET		8
#define PIN_BGA_ID_MASK			0xff

static const char *const io_type_desc[] = {
	"1V8",
	"18OD33",
	"AUDIO",
	"ETH"
};

static const char *cv1800_get_power_cfg_desc(struct cv1800_pinctrl *pctrl,
					     u8 domain)
{
	return pctrl->data->pdnames[domain];
}

static void cv1800_pctrl_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *seq, unsigned int pin_id)
{
	struct cv1800_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct cv1800_pin *pin = cv1800_get_pin(pctrl, pin_id);
	enum cv1800_pin_io_type type = cv1800_pin_io_type(pin);
	u32 value;
	void __iomem *reg;

	if (pin->pin >> PIN_BGA_ID_OFFSET)
		seq_printf(seq, "pos: %c%u ",
			   'A' + (pin->pin >> PIN_BGA_ID_OFFSET) - 1,
			   pin->pin & PIN_BGA_ID_MASK);
	else
		seq_printf(seq, "pos: %u ", pin->pin);

	seq_printf(seq, "power-domain: %s ",
		   cv1800_get_power_cfg_desc(pctrl, pin->power_domain));
	seq_printf(seq, "type: %s ", io_type_desc[type]);

	reg = cv1800_pinctrl_get_component_addr(pctrl, &pin->mux);
	value = readl(reg);
	seq_printf(seq, "mux: 0x%08x ", value);

	if (pin->flags & CV1800_PIN_HAVE_MUX2) {
		reg = cv1800_pinctrl_get_component_addr(pctrl, &pin->mux2);
		value = readl(reg);
		seq_printf(seq, "mux2: 0x%08x ", value);
	}

	if (type == IO_TYPE_1V8_ONLY || type == IO_TYPE_1V8_OR_3V3) {
		reg = cv1800_pinctrl_get_component_addr(pctrl, &pin->conf);
		value = readl(reg);
		seq_printf(seq, "conf: 0x%08x ", value);
	}
}

static int cv1800_verify_pinmux_config(const struct cv1800_pin_mux_config *config)
{
	unsigned int mux = cv1800_dt_get_pin_mux(config->config);
	unsigned int mux2 = cv1800_dt_get_pin_mux2(config->config);

	if (mux > config->pin->mux.max)
		return -EINVAL;

	if (config->pin->flags & CV1800_PIN_HAVE_MUX2) {
		if (mux != config->pin->mux2.pfunc)
			return -EINVAL;

		if (mux2 > config->pin->mux2.max)
			return -EINVAL;
	} else {
		if (mux2 != PIN_MUX_INVALD)
			return -ENOTSUPP;
	}

	return 0;
}

static int cv1800_verify_pin_group(const struct cv1800_pin_mux_config *mux,
				   unsigned long npins)
{
	enum cv1800_pin_io_type type;
	u8 power_domain;
	int i;

	if (npins == 1)
		return 0;

	type = cv1800_pin_io_type(mux[0].pin);
	power_domain = mux[0].pin->power_domain;

	for (i = 0; i < npins; i++) {
		if (type != cv1800_pin_io_type(mux[i].pin) ||
		    power_domain != mux[i].pin->power_domain)
			return -ENOTSUPP;
	}

	return 0;
}

static int cv1800_pctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
				       struct device_node *np,
				       struct pinctrl_map **maps,
				       unsigned int *num_maps)
{
	struct cv1800_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
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
	mutex_lock(&pctrl->mutex);
	for_each_available_child_of_node(np, child) {
		int npins = of_property_count_u32_elems(child, "pinmux");
		unsigned int *pins;
		struct cv1800_pin_mux_config *pinmuxs;
		u32 config, power;
		int i;

		if (npins < 1) {
			dev_err(dev, "invalid pinctrl group %pOFn.%pOFn\n",
				np, child);
			ret = -EINVAL;
			goto dt_failed;
		}

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn",
					 np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto dt_failed;
		}

		grpnames[ngroups++] = grpname;

		pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
		if (!pins) {
			ret = -ENOMEM;
			goto dt_failed;
		}

		pinmuxs = devm_kcalloc(dev, npins, sizeof(*pinmuxs), GFP_KERNEL);
		if (!pinmuxs) {
			ret = -ENOMEM;
			goto dt_failed;
		}

		for (i = 0; i < npins; i++) {
			ret = of_property_read_u32_index(child, "pinmux",
							 i, &config);
			if (ret)
				goto dt_failed;

			pins[i] = cv1800_dt_get_pin(config);
			pinmuxs[i].config = config;
			pinmuxs[i].pin = cv1800_get_pin(pctrl, pins[i]);

			if (!pinmuxs[i].pin) {
				dev_err(dev, "failed to get pin %d\n", pins[i]);
				ret = -ENODEV;
				goto dt_failed;
			}

			ret = cv1800_verify_pinmux_config(&pinmuxs[i]);
			if (ret) {
				dev_err(dev, "group %s pin %d is invalid\n",
					grpname, i);
				goto dt_failed;
			}
		}

		ret = cv1800_verify_pin_group(pinmuxs, npins);
		if (ret) {
			dev_err(dev, "group %s is invalid\n", grpname);
			goto dt_failed;
		}

		ret = of_property_read_u32(child, "power-source", &power);
		if (ret)
			goto dt_failed;

		if (!(power == PIN_POWER_STATE_3V3 || power == PIN_POWER_STATE_1V8)) {
			dev_err(dev, "group %s have unsupported power: %u\n",
				grpname, power);
			ret = -ENOTSUPP;
			goto dt_failed;
		}

		ret = cv1800_set_power_cfg(pctrl, pinmuxs[0].pin->power_domain,
					   power);
		if (ret)
			goto dt_failed;

		map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		map[nmaps].data.mux.function = np->name;
		map[nmaps].data.mux.group = grpname;
		nmaps += 1;

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret) {
			dev_err(dev, "failed to parse pin config of group %s: %d\n",
				grpname, ret);
			goto dt_failed;
		}

		ret = pinctrl_generic_add_group(pctldev, grpname,
						pins, npins, pinmuxs);
		if (ret < 0) {
			dev_err(dev, "failed to add group %s: %d\n", grpname, ret);
			goto dt_failed;
		}

		/* don't create a map if there are no pinconf settings */
		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name,
					  grpnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto function_failed;
	}

	*maps = map;
	*num_maps = nmaps;
	mutex_unlock(&pctrl->mutex);

	return 0;

dt_failed:
	of_node_put(child);
function_failed:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	mutex_unlock(&pctrl->mutex);
	return ret;
}

static const struct pinctrl_ops cv1800_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.pin_dbg_show		= cv1800_pctrl_dbg_show,
	.dt_node_to_map		= cv1800_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
};

static int cv1800_pmx_set_mux(struct pinctrl_dev *pctldev,
			      unsigned int fsel, unsigned int gsel)
{
	struct cv1800_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct cv1800_pin_mux_config *configs;
	unsigned int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	configs = group->data;

	for (i = 0; i < group->grp.npins; i++) {
		const struct cv1800_pin *pin = configs[i].pin;
		u32 value = configs[i].config;
		void __iomem *reg_mux;
		void __iomem *reg_mux2;
		unsigned long flags;
		u32 mux;
		u32 mux2;

		reg_mux = cv1800_pinctrl_get_component_addr(pctrl, &pin->mux);
		reg_mux2 = cv1800_pinctrl_get_component_addr(pctrl, &pin->mux2);
		mux = cv1800_dt_get_pin_mux(value);
		mux2 = cv1800_dt_get_pin_mux2(value);

		raw_spin_lock_irqsave(&pctrl->lock, flags);
		writel_relaxed(mux, reg_mux);
		if (mux2 != PIN_MUX_INVALD)
			writel_relaxed(mux2, reg_mux2);
		raw_spin_unlock_irqrestore(&pctrl->lock, flags);
	}

	return 0;
}

static const struct pinmux_ops cv1800_pmx_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= cv1800_pmx_set_mux,
	.strict			= true,
};

#define PIN_IO_PULLUP		BIT(2)
#define PIN_IO_PULLDOWN		BIT(3)
#define PIN_IO_DRIVE		GENMASK(7, 5)
#define PIN_IO_SCHMITT		GENMASK(9, 8)
#define PIN_IO_BUS_HOLD		BIT(10)
#define PIN_IO_OUT_FAST_SLEW	BIT(11)

static u32 cv1800_pull_down_typical_resistor(struct cv1800_pinctrl *pctrl,
					     struct cv1800_pin *pin)
{
	return pctrl->data->vddio_ops->get_pull_down(pin, pctrl->power_cfg);
}

static u32 cv1800_pull_up_typical_resistor(struct cv1800_pinctrl *pctrl,
					   struct cv1800_pin *pin)
{
	return pctrl->data->vddio_ops->get_pull_up(pin, pctrl->power_cfg);
}

static int cv1800_pinctrl_oc2reg(struct cv1800_pinctrl *pctrl,
				 struct cv1800_pin *pin, u32 target)
{
	const u32 *map;
	int i, len;

	len = pctrl->data->vddio_ops->get_oc_map(pin, pctrl->power_cfg, &map);
	if (len < 0)
		return len;

	for (i = 0; i < len; i++) {
		if (map[i] >= target)
			return i;
	}

	return -EINVAL;
}

static int cv1800_pinctrl_reg2oc(struct cv1800_pinctrl *pctrl,
				 struct cv1800_pin *pin, u32 reg)
{
	const u32 *map;
	int len;

	len = pctrl->data->vddio_ops->get_oc_map(pin, pctrl->power_cfg, &map);
	if (len < 0)
		return len;

	if (reg >= len)
		return -EINVAL;

	return map[reg];
}

static int cv1800_pinctrl_schmitt2reg(struct cv1800_pinctrl *pctrl,
				      struct cv1800_pin *pin, u32 target)
{
	const u32 *map;
	int i, len;

	len = pctrl->data->vddio_ops->get_schmitt_map(pin, pctrl->power_cfg,
						      &map);
	if (len < 0)
		return len;

	for (i = 0; i < len; i++) {
		if (map[i] == target)
			return i;
	}

	return -EINVAL;
}

static int cv1800_pinctrl_reg2schmitt(struct cv1800_pinctrl *pctrl,
				      struct cv1800_pin *pin, u32 reg)
{
	const u32 *map;
	int len;

	len = pctrl->data->vddio_ops->get_schmitt_map(pin, pctrl->power_cfg,
						      &map);
	if (len < 0)
		return len;

	if (reg >= len)
		return -EINVAL;

	return map[reg];
}

static int cv1800_pconf_get(struct pinctrl_dev *pctldev,
			    unsigned int pin_id, unsigned long *config)
{
	struct cv1800_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	struct cv1800_pin *pin = cv1800_get_pin(pctrl, pin_id);
	enum cv1800_pin_io_type type;
	u32 value;
	u32 arg;
	bool enabled;
	int ret;

	if (!pin)
		return -EINVAL;

	type = cv1800_pin_io_type(pin);
	if (type == IO_TYPE_ETH || type == IO_TYPE_AUDIO)
		return -ENOTSUPP;

	value = readl(cv1800_pinctrl_get_component_addr(pctrl, &pin->conf));

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = FIELD_GET(PIN_IO_PULLDOWN, value);
		arg = cv1800_pull_down_typical_resistor(pctrl, pin);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = FIELD_GET(PIN_IO_PULLUP, value);
		arg = cv1800_pull_up_typical_resistor(pctrl, pin);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		enabled = true;
		arg = FIELD_GET(PIN_IO_DRIVE, value);
		ret = cv1800_pinctrl_reg2oc(pctrl, pin, arg);
		if (ret < 0)
			return ret;
		arg = ret;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_UV:
		arg = FIELD_GET(PIN_IO_SCHMITT, value);
		ret = cv1800_pinctrl_reg2schmitt(pctrl, pin, arg);
		if (ret < 0)
			return ret;
		arg = ret;
		enabled = arg != 0;
		break;
	case PIN_CONFIG_POWER_SOURCE:
		enabled = true;
		arg = cv1800_get_power_cfg(pctrl, pin->power_domain);
		break;
	case PIN_CONFIG_SLEW_RATE:
		enabled = true;
		arg = FIELD_GET(PIN_IO_OUT_FAST_SLEW, value);
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		arg = FIELD_GET(PIN_IO_BUS_HOLD, value);
		enabled = arg != 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return enabled ? 0 : -EINVAL;
}

static int cv1800_pinconf_compute_config(struct cv1800_pinctrl *pctrl,
					 struct cv1800_pin *pin,
					 unsigned long *configs,
					 unsigned int num_configs,
					 u32 *value, u32 *mask)
{
	int i;
	u32 v = 0, m = 0;
	enum cv1800_pin_io_type type;
	int ret;

	if (!pin)
		return -EINVAL;

	type = cv1800_pin_io_type(pin);
	if (type == IO_TYPE_ETH || type == IO_TYPE_AUDIO)
		return -ENOTSUPP;

	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_DOWN:
			v &= ~PIN_IO_PULLDOWN;
			v |= FIELD_PREP(PIN_IO_PULLDOWN, arg);
			m |= PIN_IO_PULLDOWN;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			v &= ~PIN_IO_PULLUP;
			v |= FIELD_PREP(PIN_IO_PULLUP, arg);
			m |= PIN_IO_PULLUP;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			ret = cv1800_pinctrl_oc2reg(pctrl, pin, arg);
			if (ret < 0)
				return ret;
			v &= ~PIN_IO_DRIVE;
			v |= FIELD_PREP(PIN_IO_DRIVE, ret);
			m |= PIN_IO_DRIVE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_UV:
			ret = cv1800_pinctrl_schmitt2reg(pctrl, pin, arg);
			if (ret < 0)
				return ret;
			v &= ~PIN_IO_SCHMITT;
			v |= FIELD_PREP(PIN_IO_SCHMITT, ret);
			m |= PIN_IO_SCHMITT;
			break;
		case PIN_CONFIG_POWER_SOURCE:
			/* Ignore power source as it is always fixed */
			break;
		case PIN_CONFIG_SLEW_RATE:
			v &= ~PIN_IO_OUT_FAST_SLEW;
			v |= FIELD_PREP(PIN_IO_OUT_FAST_SLEW, arg);
			m |= PIN_IO_OUT_FAST_SLEW;
			break;
		case PIN_CONFIG_BIAS_BUS_HOLD:
			v &= ~PIN_IO_BUS_HOLD;
			v |= FIELD_PREP(PIN_IO_BUS_HOLD, arg);
			m |= PIN_IO_BUS_HOLD;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	*value = v;
	*mask = m;

	return 0;
}

static int cv1800_pin_set_config(struct cv1800_pinctrl *pctrl,
				 unsigned int pin_id,
				 u32 value, u32 mask)
{
	struct cv1800_pin *pin = cv1800_get_pin(pctrl, pin_id);
	unsigned long flags;
	void __iomem *addr;
	u32 reg;

	if (!pin)
		return -EINVAL;

	addr = cv1800_pinctrl_get_component_addr(pctrl, &pin->conf);

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	reg = readl(addr);
	reg &= ~mask;
	reg |= value;
	writel(reg, addr);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return 0;
}

static int cv1800_pconf_set(struct pinctrl_dev *pctldev,
			    unsigned int pin_id, unsigned long *configs,
			    unsigned int num_configs)
{
	struct cv1800_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct cv1800_pin *pin = cv1800_get_pin(pctrl, pin_id);
	u32 value, mask;

	if (!pin)
		return -ENODEV;

	if (cv1800_pinconf_compute_config(pctrl, pin,
					  configs, num_configs,
					  &value, &mask))
		return -ENOTSUPP;

	return cv1800_pin_set_config(pctrl, pin_id, value, mask);
}

static int cv1800_pconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int gsel,
				  unsigned long *configs,
				  unsigned int num_configs)
{
	struct cv1800_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	const struct cv1800_pin_mux_config *pinmuxs;
	u32 value, mask;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	pinmuxs = group->data;

	if (cv1800_pinconf_compute_config(pctrl, pinmuxs[0].pin,
					  configs, num_configs,
					  &value, &mask))
		return -ENOTSUPP;

	for (i = 0; i < group->grp.npins; i++)
		cv1800_pin_set_config(pctrl, group->grp.pins[i], value, mask);

	return 0;
}

static const struct pinconf_ops cv1800_pconf_ops = {
	.pin_config_get			= cv1800_pconf_get,
	.pin_config_set			= cv1800_pconf_set,
	.pin_config_group_set		= cv1800_pconf_group_set,
	.is_generic			= true,
};

int cv1800_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cv1800_pinctrl *pctrl;
	const struct cv1800_pinctrl_data *pctrl_data;
	int ret;

	pctrl_data = device_get_match_data(dev);
	if (!pctrl_data)
		return -ENODEV;

	if (pctrl_data->npins == 0 || pctrl_data->npd == 0)
		return dev_err_probe(dev, -EINVAL, "invalid pin data\n");

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->power_cfg = devm_kcalloc(dev, pctrl_data->npd,
					sizeof(u32), GFP_KERNEL);
	if (!pctrl->power_cfg)
		return -ENOMEM;

	pctrl->regs[0] = devm_platform_ioremap_resource_byname(pdev, "sys");
	if (IS_ERR(pctrl->regs[0]))
		return PTR_ERR(pctrl->regs[0]);

	pctrl->regs[1] = devm_platform_ioremap_resource_byname(pdev, "rtc");
	if (IS_ERR(pctrl->regs[1]))
		return PTR_ERR(pctrl->regs[1]);

	pctrl->pdesc.name = dev_name(dev);
	pctrl->pdesc.pins = pctrl_data->pins;
	pctrl->pdesc.npins = pctrl_data->npins;
	pctrl->pdesc.pctlops = &cv1800_pctrl_ops;
	pctrl->pdesc.pmxops = &cv1800_pmx_ops;
	pctrl->pdesc.confops = &cv1800_pconf_ops;
	pctrl->pdesc.owner = THIS_MODULE;

	pctrl->data = pctrl_data;
	pctrl->dev = dev;
	raw_spin_lock_init(&pctrl->lock);
	mutex_init(&pctrl->mutex);

	platform_set_drvdata(pdev, pctrl);

	ret = devm_pinctrl_register_and_init(dev, &pctrl->pdesc,
					     pctrl, &pctrl->pctl_dev);
	if (ret)
		return dev_err_probe(dev, ret,
				     "fail to register pinctrl driver\n");

	return pinctrl_enable(pctrl->pctl_dev);
}
EXPORT_SYMBOL_GPL(cv1800_pinctrl_probe);
