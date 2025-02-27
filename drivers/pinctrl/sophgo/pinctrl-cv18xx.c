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

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/pinctrl-cv18xx.h>

#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "pinctrl-cv18xx.h"

struct cv1800_priv {
	u32					*power_cfg;
	void __iomem				*regs[2];
};

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

static int cv1800_set_power_cfg(struct sophgo_pinctrl *pctrl,
				u8 domain, u32 cfg)
{
	struct cv1800_priv *priv = pctrl->priv_ctrl;

	if (domain >= pctrl->data->npds)
		return -ENOTSUPP;

	if (priv->power_cfg[domain] && priv->power_cfg[domain] != cfg)
		return -EINVAL;

	priv->power_cfg[domain] = cfg;

	return 0;
}

static int cv1800_get_power_cfg(struct sophgo_pinctrl *pctrl,
				u8 domain)
{
	struct cv1800_priv *priv = pctrl->priv_ctrl;

	return priv->power_cfg[domain];
}

#define PIN_BGA_ID_OFFSET		8
#define PIN_BGA_ID_MASK			0xff

static const char *const io_type_desc[] = {
	"1V8",
	"18OD33",
	"AUDIO",
	"ETH"
};

static const char *cv1800_get_power_cfg_desc(struct sophgo_pinctrl *pctrl,
					     u8 domain)
{
	return pctrl->data->pdnames[domain];
}

static void cv1800_pctrl_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *seq, unsigned int pin_id)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct cv1800_priv *priv = pctrl->priv_ctrl;
	const struct sophgo_pin *sp = sophgo_get_pin(pctrl, pin_id);
	const struct cv1800_pin *pin = sophgo_to_cv1800_pin(sp);
	enum cv1800_pin_io_type type = cv1800_pin_io_type(pin);
	u32 pin_hwid = pin->pin.id;
	u32 value;
	void __iomem *reg;

	if (pin_hwid >> PIN_BGA_ID_OFFSET)
		seq_printf(seq, "pos: %c%u ",
			   'A' + (pin_hwid >> PIN_BGA_ID_OFFSET) - 1,
			   pin_hwid & PIN_BGA_ID_MASK);
	else
		seq_printf(seq, "pos: %u ", pin_hwid);

	seq_printf(seq, "power-domain: %s ",
		   cv1800_get_power_cfg_desc(pctrl, pin->power_domain));
	seq_printf(seq, "type: %s ", io_type_desc[type]);

	reg = cv1800_pinctrl_get_component_addr(priv, &pin->mux);
	value = readl(reg);
	seq_printf(seq, "mux: 0x%08x ", value);

	if (pin->pin.flags & CV1800_PIN_HAVE_MUX2) {
		reg = cv1800_pinctrl_get_component_addr(priv, &pin->mux2);
		value = readl(reg);
		seq_printf(seq, "mux2: 0x%08x ", value);
	}

	if (type == IO_TYPE_1V8_ONLY || type == IO_TYPE_1V8_OR_3V3) {
		reg = cv1800_pinctrl_get_component_addr(priv, &pin->conf);
		value = readl(reg);
		seq_printf(seq, "conf: 0x%08x ", value);
	}
}

static int cv1800_verify_pinmux_config(const struct sophgo_pin_mux_config *config)
{
	struct cv1800_pin *pin = sophgo_to_cv1800_pin(config->pin);
	unsigned int mux = cv1800_dt_get_pin_mux(config->config);
	unsigned int mux2 = cv1800_dt_get_pin_mux2(config->config);

	if (mux > pin->mux.max)
		return -EINVAL;

	if (pin->pin.flags & CV1800_PIN_HAVE_MUX2) {
		if (mux != pin->mux2.pfunc)
			return -EINVAL;

		if (mux2 > pin->mux2.max)
			return -EINVAL;
	} else {
		if (mux2 != PIN_MUX_INVALD)
			return -ENOTSUPP;
	}

	return 0;
}

static int cv1800_verify_pin_group(const struct sophgo_pin_mux_config *mux,
				   unsigned int npins)
{
	struct cv1800_pin *pin;
	enum cv1800_pin_io_type type;
	u8 power_domain;
	int i;

	if (npins == 1)
		return 0;

	pin = sophgo_to_cv1800_pin(mux[0].pin);
	type = cv1800_pin_io_type(pin);
	power_domain = pin->power_domain;

	for (i = 0; i < npins; i++) {
		pin = sophgo_to_cv1800_pin(mux[i].pin);

		if (type != cv1800_pin_io_type(pin) ||
		    power_domain != pin->power_domain)
			return -ENOTSUPP;
	}

	return 0;
}

static int cv1800_dt_node_to_map_post(struct device_node *cur,
				      struct sophgo_pinctrl *pctrl,
				      struct sophgo_pin_mux_config *pinmuxs,
				      unsigned int npins)
{
	const struct cv1800_pin *pin = sophgo_to_cv1800_pin(pinmuxs[0].pin);
	u32 power;
	int ret;

	ret = of_property_read_u32(cur, "power-source", &power);
	if (ret)
		return ret;

	if (!(power == PIN_POWER_STATE_3V3 || power == PIN_POWER_STATE_1V8))
		return -ENOTSUPP;

	return cv1800_set_power_cfg(pctrl, pin->power_domain, power);
}

const struct pinctrl_ops cv1800_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.pin_dbg_show		= cv1800_pctrl_dbg_show,
	.dt_node_to_map		= sophgo_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
};
EXPORT_SYMBOL_GPL(cv1800_pctrl_ops);

static void cv1800_set_pinmux_config(struct sophgo_pinctrl *pctrl,
				     const struct sophgo_pin *sp, u32 config)
{
	const struct cv1800_pin *pin = sophgo_to_cv1800_pin(sp);
	struct cv1800_priv *priv = pctrl->priv_ctrl;
	void __iomem *reg_mux;
	void __iomem *reg_mux2;
	u32 mux;
	u32 mux2;

	reg_mux = cv1800_pinctrl_get_component_addr(priv, &pin->mux);
	reg_mux2 = cv1800_pinctrl_get_component_addr(priv, &pin->mux2);
	mux = cv1800_dt_get_pin_mux(config);
	mux2 = cv1800_dt_get_pin_mux2(config);

	writel_relaxed(mux, reg_mux);
	if (mux2 != PIN_MUX_INVALD)
		writel_relaxed(mux2, reg_mux2);
}

const struct pinmux_ops cv1800_pmx_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= sophgo_pmx_set_mux,
	.strict			= true,
};
EXPORT_SYMBOL_GPL(cv1800_pmx_ops);

#define PIN_IO_PULLUP		BIT(2)
#define PIN_IO_PULLDOWN		BIT(3)
#define PIN_IO_DRIVE		GENMASK(7, 5)
#define PIN_IO_SCHMITT		GENMASK(9, 8)
#define PIN_IO_BUS_HOLD		BIT(10)
#define PIN_IO_OUT_FAST_SLEW	BIT(11)

static int cv1800_pconf_get(struct pinctrl_dev *pctldev,
			    unsigned int pin_id, unsigned long *config)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct cv1800_priv *priv = pctrl->priv_ctrl;
	int param = pinconf_to_config_param(*config);
	const struct sophgo_pin *sp = sophgo_get_pin(pctrl, pin_id);
	const struct cv1800_pin *pin = sophgo_to_cv1800_pin(sp);
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

	value = readl(cv1800_pinctrl_get_component_addr(priv, &pin->conf));

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = FIELD_GET(PIN_IO_PULLDOWN, value);
		arg = sophgo_pinctrl_typical_pull_down(pctrl, sp, priv->power_cfg);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = FIELD_GET(PIN_IO_PULLUP, value);
		arg = sophgo_pinctrl_typical_pull_up(pctrl, sp, priv->power_cfg);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		enabled = true;
		arg = FIELD_GET(PIN_IO_DRIVE, value);
		ret = sophgo_pinctrl_reg2oc(pctrl, sp, priv->power_cfg, arg);
		if (ret < 0)
			return ret;
		arg = ret;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_UV:
		arg = FIELD_GET(PIN_IO_SCHMITT, value);
		ret = sophgo_pinctrl_reg2schmitt(pctrl, sp, priv->power_cfg, arg);
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

static int cv1800_pinconf_compute_config(struct sophgo_pinctrl *pctrl,
					 const struct sophgo_pin *sp,
					 unsigned long *configs,
					 unsigned int num_configs,
					 u32 *value, u32 *mask)
{
	struct cv1800_priv *priv = pctrl->priv_ctrl;
	const struct cv1800_pin *pin = sophgo_to_cv1800_pin(sp);
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
			ret = sophgo_pinctrl_oc2reg(pctrl, sp,
						    priv->power_cfg, arg);
			if (ret < 0)
				return ret;
			v &= ~PIN_IO_DRIVE;
			v |= FIELD_PREP(PIN_IO_DRIVE, ret);
			m |= PIN_IO_DRIVE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_UV:
			ret = sophgo_pinctrl_schmitt2reg(pctrl, sp,
							 priv->power_cfg, arg);
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

static int cv1800_set_pinconf_config(struct sophgo_pinctrl *pctrl,
				     const struct sophgo_pin *sp,
				     u32 value, u32 mask)
{
	struct cv1800_priv *priv = pctrl->priv_ctrl;
	struct cv1800_pin *pin = sophgo_to_cv1800_pin(sp);
	void __iomem *addr;
	u32 reg;

	addr = cv1800_pinctrl_get_component_addr(priv, &pin->conf);

	reg = readl(addr);
	reg &= ~mask;
	reg |= value;
	writel(reg, addr);

	return 0;
}

const struct pinconf_ops cv1800_pconf_ops = {
	.pin_config_get			= cv1800_pconf_get,
	.pin_config_set			= sophgo_pconf_set,
	.pin_config_group_set		= sophgo_pconf_group_set,
	.is_generic			= true,
};
EXPORT_SYMBOL_GPL(cv1800_pconf_ops);

static int cv1800_pinctrl_init(struct platform_device *pdev,
			       struct sophgo_pinctrl *pctrl)
{
	const struct sophgo_pinctrl_data *pctrl_data = pctrl->data;
	struct cv1800_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct cv1800_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->power_cfg = devm_kcalloc(&pdev->dev, pctrl_data->npds,
				       sizeof(u32), GFP_KERNEL);
	if (!priv->power_cfg)
		return -ENOMEM;

	priv->regs[0] = devm_platform_ioremap_resource_byname(pdev, "sys");
	if (IS_ERR(priv->regs[0]))
		return PTR_ERR(priv->regs[0]);

	priv->regs[1] = devm_platform_ioremap_resource_byname(pdev, "rtc");
	if (IS_ERR(priv->regs[1]))
		return PTR_ERR(priv->regs[1]);

	pctrl->priv_ctrl = priv;

	return 0;
}

const struct sophgo_cfg_ops cv1800_cfg_ops = {
	.pctrl_init = cv1800_pinctrl_init,
	.verify_pinmux_config = cv1800_verify_pinmux_config,
	.verify_pin_group = cv1800_verify_pin_group,
	.dt_node_to_map_post = cv1800_dt_node_to_map_post,
	.compute_pinconf_config = cv1800_pinconf_compute_config,
	.set_pinconf_config = cv1800_set_pinconf_config,
	.set_pinmux_config = cv1800_set_pinmux_config,
};
EXPORT_SYMBOL_GPL(cv1800_cfg_ops);
