// SPDX-License-Identifier: GPL-2.0
/*
 * Pinctrl / GPIO driver for StarFive JH7110 SoC
 *
 * Copyright (C) 2022 Emil Renner Berthing <kernel@esmil.dk>
 * Copyright (C) 2022 StarFive Technology Co., Ltd.
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <linux/pinctrl/consumer.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include <dt-bindings/pinctrl/starfive,jh7110-pinctrl.h>

#include "../core.h"
#include "../pinctrl-utils.h"
#include "../pinmux.h"
#include "../pinconf.h"
#include "pinctrl-starfive-jh7110.h"

/* pad control bits */
#define JH7110_PADCFG_POS	BIT(7)
#define JH7110_PADCFG_SMT	BIT(6)
#define JH7110_PADCFG_SLEW	BIT(5)
#define JH7110_PADCFG_PD	BIT(4)
#define JH7110_PADCFG_PU	BIT(3)
#define JH7110_PADCFG_BIAS	(JH7110_PADCFG_PD | JH7110_PADCFG_PU)
#define JH7110_PADCFG_DS_MASK	GENMASK(2, 1)
#define JH7110_PADCFG_DS_2MA	(0U << 1)
#define JH7110_PADCFG_DS_4MA	BIT(1)
#define JH7110_PADCFG_DS_8MA	(2U << 1)
#define JH7110_PADCFG_DS_12MA	(3U << 1)
#define JH7110_PADCFG_IE	BIT(0)

/*
 * The packed pinmux values from the device tree look like this:
 *
 *  | 31 - 24 | 23 - 16 | 15 - 10 |  9 - 8   | 7 - 0 |
 *  |   din   |  dout   |  doen   | function |  pin  |
 */
static unsigned int jh7110_pinmux_din(u32 v)
{
	return (v & GENMASK(31, 24)) >> 24;
}

static u32 jh7110_pinmux_dout(u32 v)
{
	return (v & GENMASK(23, 16)) >> 16;
}

static u32 jh7110_pinmux_doen(u32 v)
{
	return (v & GENMASK(15, 10)) >> 10;
}

static u32 jh7110_pinmux_function(u32 v)
{
	return (v & GENMASK(9, 8)) >> 8;
}

static unsigned int jh7110_pinmux_pin(u32 v)
{
	return v & GENMASK(7, 0);
}

static struct jh7110_pinctrl *jh7110_from_irq_data(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	return container_of(gc, struct jh7110_pinctrl, gc);
}

struct jh7110_pinctrl *jh7110_from_irq_desc(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);

	return container_of(gc, struct jh7110_pinctrl, gc);
}
EXPORT_SYMBOL_GPL(jh7110_from_irq_desc);

#ifdef CONFIG_DEBUG_FS
static void jh7110_pin_dbg_show(struct pinctrl_dev *pctldev,
				struct seq_file *s, unsigned int pin)
{
	struct jh7110_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;

	seq_printf(s, "%s", dev_name(pctldev->dev));

	if (pin < sfp->gc.ngpio) {
		unsigned int offset = 4 * (pin / 4);
		unsigned int shift  = 8 * (pin % 4);
		u32 dout = readl_relaxed(sfp->base + info->dout_reg_base + offset);
		u32 doen = readl_relaxed(sfp->base + info->doen_reg_base + offset);
		u32 gpi = readl_relaxed(sfp->base + info->gpi_reg_base + offset);

		dout = (dout >> shift) & info->dout_mask;
		doen = (doen >> shift) & info->doen_mask;
		gpi = ((gpi >> shift) - 2) & info->gpi_mask;

		seq_printf(s, " dout=%u doen=%u din=%u", dout, doen, gpi);
	}
}
#else
#define jh7110_pin_dbg_show NULL
#endif

static int jh7110_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **maps,
				 unsigned int *num_maps)
{
	struct jh7110_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	struct device *dev = sfp->gc.parent;
	struct device_node *child;
	struct pinctrl_map *map;
	const char **pgnames;
	const char *grpname;
	int ngroups;
	int nmaps;
	int ret;

	ngroups = 0;
	for_each_available_child_of_node(np, child)
		ngroups += 1;
	nmaps = 2 * ngroups;

	pgnames = devm_kcalloc(dev, ngroups, sizeof(*pgnames), GFP_KERNEL);
	if (!pgnames)
		return -ENOMEM;

	map = kcalloc(nmaps, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	nmaps = 0;
	ngroups = 0;
	mutex_lock(&sfp->mutex);
	for_each_available_child_of_node_scoped(np, child) {
		int npins = of_property_count_u32_elems(child, "pinmux");
		int *pins;
		u32 *pinmux;
		int i;

		if (npins < 1) {
			dev_err(dev,
				"invalid pinctrl group %pOFn.%pOFn: pinmux not set\n",
				np, child);
			ret = -EINVAL;
			goto free_map;
		}

		grpname = devm_kasprintf(dev, GFP_KERNEL, "%pOFn.%pOFn", np, child);
		if (!grpname) {
			ret = -ENOMEM;
			goto free_map;
		}

		pgnames[ngroups++] = grpname;

		pins = devm_kcalloc(dev, npins, sizeof(*pins), GFP_KERNEL);
		if (!pins) {
			ret = -ENOMEM;
			goto free_map;
		}

		pinmux = devm_kcalloc(dev, npins, sizeof(*pinmux), GFP_KERNEL);
		if (!pinmux) {
			ret = -ENOMEM;
			goto free_map;
		}

		ret = of_property_read_u32_array(child, "pinmux", pinmux, npins);
		if (ret)
			goto free_map;

		for (i = 0; i < npins; i++)
			pins[i] = jh7110_pinmux_pin(pinmux[i]);

		map[nmaps].type = PIN_MAP_TYPE_MUX_GROUP;
		map[nmaps].data.mux.function = np->name;
		map[nmaps].data.mux.group = grpname;
		nmaps += 1;

		ret = pinctrl_generic_add_group(pctldev, grpname,
						pins, npins, pinmux);
		if (ret < 0) {
			dev_err(dev, "error adding group %s: %d\n", grpname, ret);
			goto free_map;
		}

		ret = pinconf_generic_parse_dt_config(child, pctldev,
						      &map[nmaps].data.configs.configs,
						      &map[nmaps].data.configs.num_configs);
		if (ret) {
			dev_err(dev, "error parsing pin config of group %s: %d\n",
				grpname, ret);
			goto free_map;
		}

		/* don't create a map if there are no pinconf settings */
		if (map[nmaps].data.configs.num_configs == 0)
			continue;

		map[nmaps].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		map[nmaps].data.configs.group_or_pin = grpname;
		nmaps += 1;
	}

	ret = pinmux_generic_add_function(pctldev, np->name,
					  pgnames, ngroups, NULL);
	if (ret < 0) {
		dev_err(dev, "error adding function %s: %d\n", np->name, ret);
		goto free_map;
	}
	mutex_unlock(&sfp->mutex);

	*maps = map;
	*num_maps = nmaps;
	return 0;

free_map:
	pinctrl_utils_free_map(pctldev, map, nmaps);
	mutex_unlock(&sfp->mutex);
	return ret;
}

static const struct pinctrl_ops jh7110_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name	  = pinctrl_generic_get_group_name,
	.get_group_pins   = pinctrl_generic_get_group_pins,
	.pin_dbg_show	  = jh7110_pin_dbg_show,
	.dt_node_to_map	  = jh7110_dt_node_to_map,
	.dt_free_map	  = pinctrl_utils_free_map,
};

void jh7110_set_gpiomux(struct jh7110_pinctrl *sfp, unsigned int pin,
			unsigned int din, u32 dout, u32 doen)
{
	const struct jh7110_pinctrl_soc_info *info = sfp->info;

	unsigned int offset = 4 * (pin / 4);
	unsigned int shift  = 8 * (pin % 4);
	u32 dout_mask = info->dout_mask << shift;
	u32 done_mask = info->doen_mask << shift;
	u32 ival, imask;
	void __iomem *reg_dout;
	void __iomem *reg_doen;
	void __iomem *reg_din;
	unsigned long flags;

	reg_dout = sfp->base + info->dout_reg_base + offset;
	reg_doen = sfp->base + info->doen_reg_base + offset;
	dout <<= shift;
	doen <<= shift;
	if (din != GPI_NONE) {
		unsigned int ioffset = 4 * (din / 4);
		unsigned int ishift  = 8 * (din % 4);

		reg_din = sfp->base + info->gpi_reg_base + ioffset;
		ival = (pin + 2) << ishift;
		imask = info->gpi_mask << ishift;
	} else {
		reg_din = NULL;
	}

	raw_spin_lock_irqsave(&sfp->lock, flags);
	dout |= readl_relaxed(reg_dout) & ~dout_mask;
	writel_relaxed(dout, reg_dout);
	doen |= readl_relaxed(reg_doen) & ~done_mask;
	writel_relaxed(doen, reg_doen);
	if (reg_din) {
		ival |= readl_relaxed(reg_din) & ~imask;
		writel_relaxed(ival, reg_din);
	}
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}
EXPORT_SYMBOL_GPL(jh7110_set_gpiomux);

static int jh7110_set_mux(struct pinctrl_dev *pctldev,
			  unsigned int fsel, unsigned int gsel)
{
	struct jh7110_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	const struct group_desc *group;
	const u32 *pinmux;
	unsigned int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	pinmux = group->data;
	for (i = 0; i < group->grp.npins; i++) {
		u32 v = pinmux[i];

		if (info->jh7110_set_one_pin_mux)
			info->jh7110_set_one_pin_mux(sfp,
					jh7110_pinmux_pin(v),
					jh7110_pinmux_din(v),
					jh7110_pinmux_dout(v),
					jh7110_pinmux_doen(v),
					jh7110_pinmux_function(v));
	}

	return 0;
}

static const struct pinmux_ops jh7110_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name   = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux	     = jh7110_set_mux,
	.strict		     = true,
};

static const u8 jh7110_drive_strength_mA[4] = { 2, 4, 8, 12 };

static u32 jh7110_padcfg_ds_to_mA(u32 padcfg)
{
	return jh7110_drive_strength_mA[(padcfg >> 1) & 3U];
}

static u32 jh7110_padcfg_ds_from_mA(u32 v)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (v <= jh7110_drive_strength_mA[i])
			break;
	}
	return i << 1;
}

static void jh7110_padcfg_rmw(struct jh7110_pinctrl *sfp,
			      unsigned int pin, u32 mask, u32 value)
{
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	void __iomem *reg;
	unsigned long flags;
	int padcfg_base;

	if (!info->jh7110_get_padcfg_base)
		return;

	padcfg_base = info->jh7110_get_padcfg_base(sfp, pin);
	if (padcfg_base < 0)
		return;

	reg = sfp->base + padcfg_base + 4 * pin;
	value &= mask;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value |= readl_relaxed(reg) & ~mask;
	writel_relaxed(value, reg);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static int jh7110_pinconf_get(struct pinctrl_dev *pctldev,
			      unsigned int pin, unsigned long *config)
{
	struct jh7110_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	int param = pinconf_to_config_param(*config);
	u32 padcfg, arg;
	bool enabled;
	int padcfg_base;

	if (!info->jh7110_get_padcfg_base)
		return 0;

	padcfg_base = info->jh7110_get_padcfg_base(sfp, pin);
	if (padcfg_base < 0)
		return 0;

	padcfg = readl_relaxed(sfp->base + padcfg_base + 4 * pin);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = !(padcfg & JH7110_PADCFG_BIAS);
		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = padcfg & JH7110_PADCFG_PD;
		arg = 1;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = padcfg & JH7110_PADCFG_PU;
		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		enabled = true;
		arg = jh7110_padcfg_ds_to_mA(padcfg);
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		enabled = padcfg & JH7110_PADCFG_IE;
		arg = enabled;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		enabled = padcfg & JH7110_PADCFG_SMT;
		arg = enabled;
		break;
	case PIN_CONFIG_SLEW_RATE:
		enabled = true;
		arg = !!(padcfg & JH7110_PADCFG_SLEW);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);
	return enabled ? 0 : -EINVAL;
}

static int jh7110_pinconf_group_get(struct pinctrl_dev *pctldev,
				    unsigned int gsel,
				    unsigned long *config)
{
	const struct group_desc *group;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	return jh7110_pinconf_get(pctldev, group->grp.pins[0], config);
}

static int jh7110_pinconf_group_set(struct pinctrl_dev *pctldev,
				    unsigned int gsel,
				    unsigned long *configs,
				    unsigned int num_configs)
{
	struct jh7110_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct group_desc *group;
	u16 mask, value;
	int i;

	group = pinctrl_generic_get_group(pctldev, gsel);
	if (!group)
		return -EINVAL;

	mask = 0;
	value = 0;
	for (i = 0; i < num_configs; i++) {
		int param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			mask |= JH7110_PADCFG_BIAS;
			value &= ~JH7110_PADCFG_BIAS;
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= JH7110_PADCFG_BIAS;
			value = (value & ~JH7110_PADCFG_BIAS) | JH7110_PADCFG_PD;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			if (arg == 0)
				return -ENOTSUPP;
			mask |= JH7110_PADCFG_BIAS;
			value = (value & ~JH7110_PADCFG_BIAS) | JH7110_PADCFG_PU;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			mask |= JH7110_PADCFG_DS_MASK;
			value = (value & ~JH7110_PADCFG_DS_MASK) |
				jh7110_padcfg_ds_from_mA(arg);
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			mask |= JH7110_PADCFG_IE;
			if (arg)
				value |= JH7110_PADCFG_IE;
			else
				value &= ~JH7110_PADCFG_IE;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			mask |= JH7110_PADCFG_SMT;
			if (arg)
				value |= JH7110_PADCFG_SMT;
			else
				value &= ~JH7110_PADCFG_SMT;
			break;
		case PIN_CONFIG_SLEW_RATE:
			mask |= JH7110_PADCFG_SLEW;
			if (arg)
				value |= JH7110_PADCFG_SLEW;
			else
				value &= ~JH7110_PADCFG_SLEW;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	for (i = 0; i < group->grp.npins; i++)
		jh7110_padcfg_rmw(sfp, group->grp.pins[i], mask, value);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void jh7110_pinconf_dbg_show(struct pinctrl_dev *pctldev,
				    struct seq_file *s, unsigned int pin)
{
	struct jh7110_pinctrl *sfp = pinctrl_dev_get_drvdata(pctldev);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	u32 value;
	int padcfg_base;

	if (!info->jh7110_get_padcfg_base)
		return;

	padcfg_base = info->jh7110_get_padcfg_base(sfp, pin);
	if (padcfg_base < 0)
		return;

	value = readl_relaxed(sfp->base + padcfg_base + 4 * pin);
	seq_printf(s, " (0x%02x)", value);
}
#else
#define jh7110_pinconf_dbg_show NULL
#endif

static const struct pinconf_ops jh7110_pinconf_ops = {
	.pin_config_get		= jh7110_pinconf_get,
	.pin_config_group_get	= jh7110_pinconf_group_get,
	.pin_config_group_set	= jh7110_pinconf_group_set,
	.pin_config_dbg_show	= jh7110_pinconf_dbg_show,
	.is_generic		= true,
};

static int jh7110_gpio_get_direction(struct gpio_chip *gc,
				     unsigned int gpio)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	unsigned int offset = 4 * (gpio / 4);
	unsigned int shift  = 8 * (gpio % 4);
	u32 doen = readl_relaxed(sfp->base + info->doen_reg_base + offset);

	doen = (doen >> shift) & info->doen_mask;

	return doen == GPOEN_ENABLE ?
		GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int jh7110_gpio_direction_input(struct gpio_chip *gc,
				       unsigned int gpio)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;

	/* enable input and schmitt trigger */
	jh7110_padcfg_rmw(sfp, gpio,
			  JH7110_PADCFG_IE | JH7110_PADCFG_SMT,
			  JH7110_PADCFG_IE | JH7110_PADCFG_SMT);

	if (info->jh7110_set_one_pin_mux)
		info->jh7110_set_one_pin_mux(sfp, gpio,
				GPI_NONE, GPOUT_LOW, GPOEN_DISABLE, 0);

	return 0;
}

static int jh7110_gpio_direction_output(struct gpio_chip *gc,
					unsigned int gpio, int value)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;

	if (info->jh7110_set_one_pin_mux)
		info->jh7110_set_one_pin_mux(sfp, gpio,
				GPI_NONE, value ? GPOUT_HIGH : GPOUT_LOW,
				GPOEN_ENABLE, 0);

	/* disable input, schmitt trigger and bias */
	jh7110_padcfg_rmw(sfp, gpio,
			  JH7110_PADCFG_IE | JH7110_PADCFG_SMT |
			  JH7110_PADCFG_BIAS, 0);
	return 0;
}

static int jh7110_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	void __iomem *reg = sfp->base + info->gpioin_reg_base
			+ 4 * (gpio / 32);

	return !!(readl_relaxed(reg) & BIT(gpio % 32));
}

static void jh7110_gpio_set(struct gpio_chip *gc,
			    unsigned int gpio, int value)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);
	const struct jh7110_pinctrl_soc_info *info = sfp->info;
	unsigned int offset = 4 * (gpio / 4);
	unsigned int shift  = 8 * (gpio % 4);
	void __iomem *reg_dout = sfp->base + info->dout_reg_base + offset;
	u32 dout = (value ? GPOUT_HIGH : GPOUT_LOW) << shift;
	u32 mask = info->dout_mask << shift;
	unsigned long flags;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	dout |= readl_relaxed(reg_dout) & ~mask;
	writel_relaxed(dout, reg_dout);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static int jh7110_gpio_set_config(struct gpio_chip *gc,
				  unsigned int gpio, unsigned long config)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);
	u32 arg = pinconf_to_config_argument(config);
	u32 value;
	u32 mask;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_DISABLE:
		mask  = JH7110_PADCFG_BIAS;
		value = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (arg == 0)
			return -ENOTSUPP;
		mask  = JH7110_PADCFG_BIAS;
		value = JH7110_PADCFG_PD;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (arg == 0)
			return -ENOTSUPP;
		mask  = JH7110_PADCFG_BIAS;
		value = JH7110_PADCFG_PU;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return 0;
	case PIN_CONFIG_INPUT_ENABLE:
		mask  = JH7110_PADCFG_IE;
		value = arg ? JH7110_PADCFG_IE : 0;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		mask  = JH7110_PADCFG_SMT;
		value = arg ? JH7110_PADCFG_SMT : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	jh7110_padcfg_rmw(sfp, gpio, mask, value);
	return 0;
}

static int jh7110_gpio_add_pin_ranges(struct gpio_chip *gc)
{
	struct jh7110_pinctrl *sfp = container_of(gc,
			struct jh7110_pinctrl, gc);

	sfp->gpios.name = sfp->gc.label;
	sfp->gpios.base = sfp->gc.base;
	sfp->gpios.pin_base = 0;
	sfp->gpios.npins = sfp->gc.ngpio;
	sfp->gpios.gc = &sfp->gc;
	pinctrl_add_gpio_range(sfp->pctl, &sfp->gpios);
	return 0;
}

static void jh7110_irq_ack(struct irq_data *d)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_data(d);
	const struct jh7110_gpio_irq_reg *irq_reg = sfp->info->irq_reg;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ic = sfp->base + irq_reg->ic_reg_base
		+ 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ic) & ~mask;
	writel_relaxed(value, ic);
	writel_relaxed(value | mask, ic);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static void jh7110_irq_mask(struct irq_data *d)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_data(d);
	const struct jh7110_gpio_irq_reg *irq_reg = sfp->info->irq_reg;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ie = sfp->base + irq_reg->ie_reg_base
		+ 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ie) & ~mask;
	writel_relaxed(value, ie);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);

	gpiochip_disable_irq(&sfp->gc, d->hwirq);
}

static void jh7110_irq_mask_ack(struct irq_data *d)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_data(d);
	const struct jh7110_gpio_irq_reg *irq_reg = sfp->info->irq_reg;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ie = sfp->base + irq_reg->ie_reg_base
		+ 4 * (gpio / 32);
	void __iomem *ic = sfp->base + irq_reg->ic_reg_base
		+ 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ie) & ~mask;
	writel_relaxed(value, ie);

	value = readl_relaxed(ic) & ~mask;
	writel_relaxed(value, ic);
	writel_relaxed(value | mask, ic);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static void jh7110_irq_unmask(struct irq_data *d)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_data(d);
	const struct jh7110_gpio_irq_reg *irq_reg = sfp->info->irq_reg;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *ie = sfp->base + irq_reg->ie_reg_base
		+ 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	unsigned long flags;
	u32 value;

	gpiochip_enable_irq(&sfp->gc, d->hwirq);

	raw_spin_lock_irqsave(&sfp->lock, flags);
	value = readl_relaxed(ie) | mask;
	writel_relaxed(value, ie);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
}

static int jh7110_irq_set_type(struct irq_data *d, unsigned int trigger)
{
	struct jh7110_pinctrl *sfp = jh7110_from_irq_data(d);
	const struct jh7110_gpio_irq_reg *irq_reg = sfp->info->irq_reg;
	irq_hw_number_t gpio = irqd_to_hwirq(d);
	void __iomem *base = sfp->base + 4 * (gpio / 32);
	u32 mask = BIT(gpio % 32);
	u32 irq_type, edge_both, polarity;
	unsigned long flags;

	switch (trigger) {
	case IRQ_TYPE_EDGE_RISING:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = 0;    /* 0: single edge */
		polarity  = mask; /* 1: rising edge */
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = 0;    /* 0: single edge */
		polarity  = 0;    /* 0: falling edge */
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irq_type  = mask; /* 1: edge triggered */
		edge_both = mask; /* 1: both edges */
		polarity  = 0;    /* 0: ignored */
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_type  = 0;    /* 0: level triggered */
		edge_both = 0;    /* 0: ignored */
		polarity  = 0;    /* 0: high level */
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_type  = 0;    /* 0: level triggered */
		edge_both = 0;    /* 0: ignored */
		polarity  = mask; /* 1: low level */
		break;
	default:
		return -EINVAL;
	}

	if (trigger & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else
		irq_set_handler_locked(d, handle_level_irq);

	raw_spin_lock_irqsave(&sfp->lock, flags);
	irq_type |= readl_relaxed(base + irq_reg->is_reg_base) & ~mask;
	writel_relaxed(irq_type, base + irq_reg->is_reg_base);

	edge_both |= readl_relaxed(base + irq_reg->ibe_reg_base) & ~mask;
	writel_relaxed(edge_both, base + irq_reg->ibe_reg_base);

	polarity |= readl_relaxed(base + irq_reg->iev_reg_base) & ~mask;
	writel_relaxed(polarity, base + irq_reg->iev_reg_base);
	raw_spin_unlock_irqrestore(&sfp->lock, flags);
	return 0;
}

static struct irq_chip jh7110_irq_chip = {
	.irq_ack      = jh7110_irq_ack,
	.irq_mask     = jh7110_irq_mask,
	.irq_mask_ack = jh7110_irq_mask_ack,
	.irq_unmask   = jh7110_irq_unmask,
	.irq_set_type = jh7110_irq_set_type,
	.flags	      = IRQCHIP_IMMUTABLE | IRQCHIP_SET_TYPE_MASKED,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void jh7110_disable_clock(void *data)
{
	clk_disable_unprepare(data);
}

int jh7110_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct jh7110_pinctrl_soc_info *info;
	struct jh7110_pinctrl *sfp;
	struct pinctrl_desc *jh7110_pinctrl_desc;
	struct reset_control *rst;
	struct clk *clk;
	int ret;

	info = of_device_get_match_data(&pdev->dev);
	if (!info)
		return -ENODEV;

	if (!info->pins || !info->npins) {
		dev_err(dev, "wrong pinctrl info\n");
		return -EINVAL;
	}

	sfp = devm_kzalloc(dev, sizeof(*sfp), GFP_KERNEL);
	if (!sfp)
		return -ENOMEM;

#if IS_ENABLED(CONFIG_PM_SLEEP)
	sfp->saved_regs = devm_kcalloc(dev, info->nsaved_regs,
				       sizeof(*sfp->saved_regs), GFP_KERNEL);
	if (!sfp->saved_regs)
		return -ENOMEM;
#endif

	sfp->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sfp->base))
		return PTR_ERR(sfp->base);

	clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "could not get clock\n");

	rst = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "could not get reset\n");

	/*
	 * we don't want to assert reset and risk undoing pin muxing for the
	 * early boot serial console, but let's make sure the reset line is
	 * deasserted in case someone runs a really minimal bootloader.
	 */
	ret = reset_control_deassert(rst);
	if (ret)
		return dev_err_probe(dev, ret, "could not deassert reset\n");

	if (clk) {
		ret = clk_prepare_enable(clk);
		if (ret)
			return dev_err_probe(dev, ret, "could not enable clock\n");

		ret = devm_add_action_or_reset(dev, jh7110_disable_clock, clk);
		if (ret)
			return ret;
	}

	jh7110_pinctrl_desc = devm_kzalloc(&pdev->dev,
					   sizeof(*jh7110_pinctrl_desc),
					   GFP_KERNEL);
	if (!jh7110_pinctrl_desc)
		return -ENOMEM;

	jh7110_pinctrl_desc->name = dev_name(dev);
	jh7110_pinctrl_desc->pins = info->pins;
	jh7110_pinctrl_desc->npins = info->npins;
	jh7110_pinctrl_desc->pctlops = &jh7110_pinctrl_ops;
	jh7110_pinctrl_desc->pmxops = &jh7110_pinmux_ops;
	jh7110_pinctrl_desc->confops = &jh7110_pinconf_ops;
	jh7110_pinctrl_desc->owner = THIS_MODULE;

	sfp->info = info;
	sfp->dev = dev;
	platform_set_drvdata(pdev, sfp);
	sfp->gc.parent = dev;
	raw_spin_lock_init(&sfp->lock);
	mutex_init(&sfp->mutex);

	ret = devm_pinctrl_register_and_init(dev,
					     jh7110_pinctrl_desc,
					     sfp, &sfp->pctl);
	if (ret)
		return dev_err_probe(dev, ret,
				"could not register pinctrl driver\n");

	sfp->gc.label = dev_name(dev);
	sfp->gc.owner = THIS_MODULE;
	sfp->gc.request = pinctrl_gpio_request;
	sfp->gc.free = pinctrl_gpio_free;
	sfp->gc.get_direction = jh7110_gpio_get_direction;
	sfp->gc.direction_input = jh7110_gpio_direction_input;
	sfp->gc.direction_output = jh7110_gpio_direction_output;
	sfp->gc.get = jh7110_gpio_get;
	sfp->gc.set = jh7110_gpio_set;
	sfp->gc.set_config = jh7110_gpio_set_config;
	sfp->gc.add_pin_ranges = jh7110_gpio_add_pin_ranges;
	sfp->gc.base = info->gc_base;
	sfp->gc.ngpio = info->ngpios;

	jh7110_irq_chip.name = sfp->gc.label;
	gpio_irq_chip_set_chip(&sfp->gc.irq, &jh7110_irq_chip);
	sfp->gc.irq.parent_handler = info->jh7110_gpio_irq_handler;
	sfp->gc.irq.num_parents = 1;
	sfp->gc.irq.parents = devm_kcalloc(dev, sfp->gc.irq.num_parents,
					   sizeof(*sfp->gc.irq.parents),
					   GFP_KERNEL);
	if (!sfp->gc.irq.parents)
		return -ENOMEM;
	sfp->gc.irq.default_type = IRQ_TYPE_NONE;
	sfp->gc.irq.handler = handle_bad_irq;
	sfp->gc.irq.init_hw = info->jh7110_gpio_init_hw;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	sfp->gc.irq.parents[0] = ret;

	ret = devm_gpiochip_add_data(dev, &sfp->gc, sfp);
	if (ret)
		return dev_err_probe(dev, ret, "could not register gpiochip\n");

	dev_info(dev, "StarFive GPIO chip registered %d GPIOs\n", sfp->gc.ngpio);

	return pinctrl_enable(sfp->pctl);
}
EXPORT_SYMBOL_GPL(jh7110_pinctrl_probe);

static int jh7110_pinctrl_suspend(struct device *dev)
{
	struct jh7110_pinctrl *sfp = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int i;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	for (i = 0 ; i < sfp->info->nsaved_regs ; i++)
		sfp->saved_regs[i] = readl_relaxed(sfp->base + 4 * i);

	raw_spin_unlock_irqrestore(&sfp->lock, flags);
	return 0;
}

static int jh7110_pinctrl_resume(struct device *dev)
{
	struct jh7110_pinctrl *sfp = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int i;

	raw_spin_lock_irqsave(&sfp->lock, flags);
	for (i = 0 ; i < sfp->info->nsaved_regs ; i++)
		writel_relaxed(sfp->saved_regs[i], sfp->base + 4 * i);

	raw_spin_unlock_irqrestore(&sfp->lock, flags);
	return 0;
}

const struct dev_pm_ops jh7110_pinctrl_pm_ops = {
	LATE_SYSTEM_SLEEP_PM_OPS(jh7110_pinctrl_suspend, jh7110_pinctrl_resume)
};
EXPORT_SYMBOL_GPL(jh7110_pinctrl_pm_ops);

MODULE_DESCRIPTION("Pinctrl driver for the StarFive JH7110 SoC");
MODULE_AUTHOR("Emil Renner Berthing <kernel@esmil.dk>");
MODULE_AUTHOR("Jianlong Huang <jianlong.huang@starfivetech.com>");
MODULE_LICENSE("GPL");
