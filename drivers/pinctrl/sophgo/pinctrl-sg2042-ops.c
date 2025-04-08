// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo sg2042 SoCs pinctrl driver.
 *
 * Copyright (C) 2024 Inochi Amaoto <inochiama@outlook.com>
 *
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "../pinctrl-utils.h"
#include "../pinmux.h"

#include "pinctrl-sg2042.h"

#define PIN_IO_PULL_ONE_ENABLE		BIT(0)
#define PIN_IO_PULL_DIR_UP		(BIT(1) | PIN_IO_PULL_ONE_ENABLE)
#define PIN_IO_PULL_DIR_DOWN		(0 | PIN_IO_PULL_ONE_ENABLE)
#define PIN_IO_PULL_ONE_MASK		GENMASK(1, 0)

#define PIN_IO_PULL_UP			BIT(2)
#define PIN_IO_PULL_UP_DONW		BIT(3)
#define PIN_IO_PULL_UP_MASK		GENMASK(3, 2)

#define PIN_IO_MUX			GENMASK(5, 4)
#define PIN_IO_DRIVE			GENMASK(9, 6)
#define PIN_IO_SCHMITT_ENABLE		BIT(10)
#define PIN_IO_OUTPUT_ENABLE		BIT(11)

struct sg2042_priv {
	void __iomem				*regs;
};

static u8 sg2042_dt_get_pin_mux(u32 value)
{
	return value >> 16;
}

static inline u32 sg2042_get_pin_reg(struct sophgo_pinctrl *pctrl,
				     const struct sophgo_pin *sp)
{
	struct sg2042_priv *priv = pctrl->priv_ctrl;
	const struct sg2042_pin *pin = sophgo_to_sg2042_pin(sp);
	void __iomem *reg = priv->regs + pin->offset;

	if (sp->flags & PIN_FLAG_WRITE_HIGH)
		return readl(reg) >> 16;
	else
		return readl(reg) & 0xffff;
}

static int sg2042_set_pin_reg(struct sophgo_pinctrl *pctrl,
			      const struct sophgo_pin *sp,
			      u32 value, u32 mask)
{
	struct sg2042_priv *priv = pctrl->priv_ctrl;
	const struct sg2042_pin *pin = sophgo_to_sg2042_pin(sp);
	void __iomem *reg = priv->regs + pin->offset;
	u32 v = readl(reg);

	if (sp->flags & PIN_FLAG_WRITE_HIGH) {
		v &= ~(mask << 16);
		v |= value << 16;
	} else {
		v &= ~mask;
		v |= value;
	}

	writel(v, reg);

	return 0;
}

static void sg2042_pctrl_dbg_show(struct pinctrl_dev *pctldev,
				  struct seq_file *seq, unsigned int pin_id)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	const struct sophgo_pin *sp = sophgo_get_pin(pctrl, pin_id);
	u32 value, mux;

	value = sg2042_get_pin_reg(pctrl, sp);
	mux = FIELD_GET(PIN_IO_MUX, value);
	seq_printf(seq, "mux:%u reg:0x%04x ", mux, value);
}

const struct pinctrl_ops sg2042_pctrl_ops = {
	.get_groups_count	= pinctrl_generic_get_group_count,
	.get_group_name		= pinctrl_generic_get_group_name,
	.get_group_pins		= pinctrl_generic_get_group_pins,
	.pin_dbg_show		= sg2042_pctrl_dbg_show,
	.dt_node_to_map		= sophgo_pctrl_dt_node_to_map,
	.dt_free_map		= pinctrl_utils_free_map,
};
EXPORT_SYMBOL_GPL(sg2042_pctrl_ops);

static void sg2042_set_pinmux_config(struct sophgo_pinctrl *pctrl,
				     const struct sophgo_pin *sp, u32 config)
{
	u32 mux = sg2042_dt_get_pin_mux(config);

	if (!(sp->flags & PIN_FLAG_NO_PINMUX))
		sg2042_set_pin_reg(pctrl, sp, mux, PIN_IO_MUX);
}

const struct pinmux_ops sg2042_pmx_ops = {
	.get_functions_count	= pinmux_generic_get_function_count,
	.get_function_name	= pinmux_generic_get_function_name,
	.get_function_groups	= pinmux_generic_get_function_groups,
	.set_mux		= sophgo_pmx_set_mux,
	.strict			= true,
};
EXPORT_SYMBOL_GPL(sg2042_pmx_ops);

static int sg2042_pconf_get(struct pinctrl_dev *pctldev,
			    unsigned int pin_id, unsigned long *config)
{
	struct sophgo_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int param = pinconf_to_config_param(*config);
	const struct sophgo_pin *sp = sophgo_get_pin(pctrl, pin_id);
	u32 value;
	u32 arg;
	bool enabled;
	int ret;

	if (!sp)
		return -EINVAL;

	value = sg2042_get_pin_reg(pctrl, sp);

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (sp->flags & PIN_FLAG_ONLY_ONE_PULL)
			arg = FIELD_GET(PIN_IO_PULL_ONE_ENABLE, value);
		else
			arg = FIELD_GET(PIN_IO_PULL_UP_MASK, value);
		enabled = arg == 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (sp->flags & PIN_FLAG_ONLY_ONE_PULL) {
			arg = FIELD_GET(PIN_IO_PULL_ONE_MASK, value);
			enabled = arg == PIN_IO_PULL_DIR_DOWN;
		} else {
			enabled = FIELD_GET(PIN_IO_PULL_UP_DONW, value) != 0;
		}
		arg = sophgo_pinctrl_typical_pull_down(pctrl, sp, NULL);
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (sp->flags & PIN_FLAG_ONLY_ONE_PULL) {
			arg = FIELD_GET(PIN_IO_PULL_ONE_MASK, value);
			enabled = arg == PIN_IO_PULL_DIR_UP;
		} else {
			enabled = FIELD_GET(PIN_IO_PULL_UP, value) != 0;
		}
		arg = sophgo_pinctrl_typical_pull_up(pctrl, sp, NULL);
		break;
	case PIN_CONFIG_DRIVE_STRENGTH_UA:
		enabled = FIELD_GET(PIN_IO_OUTPUT_ENABLE, value) != 0;
		arg = FIELD_GET(PIN_IO_DRIVE, value);
		ret = sophgo_pinctrl_reg2oc(pctrl, sp, NULL, arg);
		if (ret < 0)
			return ret;
		arg = ret;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		arg = FIELD_GET(PIN_IO_SCHMITT_ENABLE, value);
		enabled = arg != 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return enabled ? 0 : -EINVAL;
}

static int sg2042_pinconf_compute_config(struct sophgo_pinctrl *pctrl,
					 const struct sophgo_pin *sp,
					 unsigned long *configs,
					 unsigned int num_configs,
					 u32 *value, u32 *mask)
{
	int i;
	u16 v = 0, m = 0;
	int ret;

	if (!sp)
		return -EINVAL;

	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			if (sp->flags & PIN_FLAG_ONLY_ONE_PULL) {
				v &= ~PIN_IO_PULL_ONE_ENABLE;
				m |= PIN_IO_PULL_ONE_ENABLE;
			} else {
				v &= ~PIN_IO_PULL_UP_MASK;
				m |= PIN_IO_PULL_UP_MASK;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (sp->flags & PIN_FLAG_ONLY_ONE_PULL) {
				v &= ~PIN_IO_PULL_ONE_MASK;
				v |= PIN_IO_PULL_DIR_DOWN;
				m |= PIN_IO_PULL_ONE_MASK;
			} else {
				v |= PIN_IO_PULL_UP_DONW;
				m |= PIN_IO_PULL_UP_DONW;
			}
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (sp->flags & PIN_FLAG_ONLY_ONE_PULL) {
				v &= ~PIN_IO_PULL_ONE_MASK;
				v |= PIN_IO_PULL_DIR_UP;
				m |= PIN_IO_PULL_ONE_MASK;
			} else {
				v |= PIN_IO_PULL_UP;
				m |= PIN_IO_PULL_UP;
			}
			break;
		case PIN_CONFIG_DRIVE_STRENGTH_UA:
			v &= ~(PIN_IO_DRIVE | PIN_IO_OUTPUT_ENABLE);
			if (arg != 0) {
				ret = sophgo_pinctrl_oc2reg(pctrl, sp, NULL, arg);
				if (ret < 0)
					return ret;
				if (!(sp->flags & PIN_FLAG_NO_OEX_EN))
					v |= PIN_IO_OUTPUT_ENABLE;
				v |= FIELD_PREP(PIN_IO_DRIVE, ret);
			}
			m |= PIN_IO_DRIVE | PIN_IO_OUTPUT_ENABLE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			v |= PIN_IO_SCHMITT_ENABLE;
			m |= PIN_IO_SCHMITT_ENABLE;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	*value = v;
	*mask = m;

	return 0;
}

const struct pinconf_ops sg2042_pconf_ops = {
	.pin_config_get			= sg2042_pconf_get,
	.pin_config_set			= sophgo_pconf_set,
	.pin_config_group_set		= sophgo_pconf_group_set,
	.is_generic			= true,
};
EXPORT_SYMBOL_GPL(sg2042_pconf_ops);

static int sophgo_pinctrl_init(struct platform_device *pdev,
			       struct sophgo_pinctrl *pctrl)
{
	struct sg2042_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->regs))
		return PTR_ERR(priv->regs);

	pctrl->priv_ctrl = priv;

	return 0;
}

const struct sophgo_cfg_ops sg2042_cfg_ops = {
	.pctrl_init = sophgo_pinctrl_init,
	.compute_pinconf_config = sg2042_pinconf_compute_config,
	.set_pinconf_config = sg2042_set_pin_reg,
	.set_pinmux_config = sg2042_set_pinmux_config,
};
EXPORT_SYMBOL_GPL(sg2042_cfg_ops);
