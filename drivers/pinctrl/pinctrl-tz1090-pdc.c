/*
 * Pinctrl driver for the Toumaz Xenif TZ1090 PowerDown Controller pins
 *
 * Copyright (c) 2013, Imagination Technologies Ltd.
 *
 * Derived from Tegra code:
 * Copyright (c) 2011-2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * Derived from code:
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2010 NVIDIA Corporation
 * Copyright (C) 2009-2011 ST-Ericsson AB
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/slab.h>

/*
 * The registers may be shared with other threads/cores, so we need to use the
 * metag global lock2 for atomicity.
 */
#include <asm/global_lock.h>

#include "core.h"
#include "pinconf.h"

/* Register offsets from bank base address */
#define REG_GPIO_CONTROL0	0x00
#define REG_GPIO_CONTROL2	0x08

/* Register field information */
#define REG_GPIO_CONTROL2_PU_PD_S	16
#define REG_GPIO_CONTROL2_PDC_POS_S	 4
#define REG_GPIO_CONTROL2_PDC_DR_S	 2
#define REG_GPIO_CONTROL2_PDC_SR_S	 1
#define REG_GPIO_CONTROL2_PDC_SCHMITT_S	 0

/* PU_PD field values */
#define REG_PU_PD_TRISTATE	0
#define REG_PU_PD_UP		1
#define REG_PU_PD_DOWN		2
#define REG_PU_PD_REPEATER	3

/* DR field values */
#define REG_DR_2mA		0
#define REG_DR_4mA		1
#define REG_DR_8mA		2
#define REG_DR_12mA		3

/**
 * struct tz1090_pdc_function - TZ1090 PDC pinctrl mux function
 * @name:	The name of the function, exported to pinctrl core.
 * @groups:	An array of pin groups that may select this function.
 * @ngroups:	The number of entries in @groups.
 */
struct tz1090_pdc_function {
	const char		*name;
	const char * const	*groups;
	unsigned int		ngroups;
};

/**
 * struct tz1090_pdc_pingroup - TZ1090 PDC pin group
 * @name:	Name of pin group.
 * @pins:	Array of pin numbers in this pin group.
 * @npins:	Number of pins in this pin group.
 * @func:	Function enabled by the mux.
 * @reg:	Mux register offset.
 * @bit:	Mux register bit.
 * @drv:	Drive control supported, otherwise it's a mux.
 *		This means Schmitt, Slew, and Drive strength.
 *
 * A representation of a group of pins (possibly just one pin) in the TZ1090
 * PDC pin controller. Each group allows some parameter or parameters to be
 * configured. The most common is mux function selection.
 */
struct tz1090_pdc_pingroup {
	const char		*name;
	const unsigned int	*pins;
	unsigned int		npins;
	int			func;
	u16			reg;
	u8			bit;
	bool			drv;
};

/*
 * All PDC pins can be GPIOs. Define these first to match how the GPIO driver
 * names/numbers its pins.
 */

enum tz1090_pdc_pin {
	TZ1090_PDC_PIN_GPIO0,
	TZ1090_PDC_PIN_GPIO1,
	TZ1090_PDC_PIN_SYS_WAKE0,
	TZ1090_PDC_PIN_SYS_WAKE1,
	TZ1090_PDC_PIN_SYS_WAKE2,
	TZ1090_PDC_PIN_IR_DATA,
	TZ1090_PDC_PIN_EXT_POWER,
};

/* Pin names */

static const struct pinctrl_pin_desc tz1090_pdc_pins[] = {
	/* PDC GPIOs */
	PINCTRL_PIN(TZ1090_PDC_PIN_GPIO0,	"gpio0"),
	PINCTRL_PIN(TZ1090_PDC_PIN_GPIO1,	"gpio1"),
	PINCTRL_PIN(TZ1090_PDC_PIN_SYS_WAKE0,	"sys_wake0"),
	PINCTRL_PIN(TZ1090_PDC_PIN_SYS_WAKE1,	"sys_wake1"),
	PINCTRL_PIN(TZ1090_PDC_PIN_SYS_WAKE2,	"sys_wake2"),
	PINCTRL_PIN(TZ1090_PDC_PIN_IR_DATA,	"ir_data"),
	PINCTRL_PIN(TZ1090_PDC_PIN_EXT_POWER,	"ext_power"),
};

/* Pin group pins */

static const unsigned int gpio0_pins[] = {
	TZ1090_PDC_PIN_GPIO0,
};

static const unsigned int gpio1_pins[] = {
	TZ1090_PDC_PIN_GPIO1,
};

static const unsigned int pdc_pins[] = {
	TZ1090_PDC_PIN_GPIO0,
	TZ1090_PDC_PIN_GPIO1,
	TZ1090_PDC_PIN_SYS_WAKE0,
	TZ1090_PDC_PIN_SYS_WAKE1,
	TZ1090_PDC_PIN_SYS_WAKE2,
	TZ1090_PDC_PIN_IR_DATA,
	TZ1090_PDC_PIN_EXT_POWER,
};

/* Mux functions */

enum tz1090_pdc_mux {
	/* PDC_GPIO0 mux */
	TZ1090_PDC_MUX_IR_MOD_STABLE_OUT,
	/* PDC_GPIO1 mux */
	TZ1090_PDC_MUX_IR_MOD_POWER_OUT,
};

/* Pin groups a function can be muxed to */

static const char * const gpio0_groups[] = {
	"gpio0",
};

static const char * const gpio1_groups[] = {
	"gpio1",
};

#define FUNCTION(mux, fname, group)			\
	[(TZ1090_PDC_MUX_ ## mux)] = {			\
		.name = #fname,				\
		.groups = group##_groups,		\
		.ngroups = ARRAY_SIZE(group##_groups),	\
	}

/* Must correlate with enum tz1090_pdc_mux */
static const struct tz1090_pdc_function tz1090_pdc_functions[] = {
	/*	 MUX			fn			pingroups */
	FUNCTION(IR_MOD_STABLE_OUT,	ir_mod_stable_out,	gpio0),
	FUNCTION(IR_MOD_POWER_OUT,	ir_mod_power_out,	gpio1),
};

/**
 * MUX_PG() - Initialise a pin group with mux control
 * @pg_name:	Pin group name (stringified, _pins appended to get pins array)
 * @f0:		Function 0 (TZ1090_PDC_MUX_ is prepended)
 * @mux_r:	Mux register (REG_PINCTRL_ is prepended)
 * @mux_b:	Bit number in register of mux field
 */
#define MUX_PG(pg_name, f0, mux_r, mux_b)			\
	{							\
		.name = #pg_name,				\
		.pins = pg_name##_pins,				\
		.npins = ARRAY_SIZE(pg_name##_pins),		\
		.func = TZ1090_PDC_MUX_ ## f0,			\
		.reg = (REG_ ## mux_r),				\
		.bit = (mux_b),					\
	}

/**
 * DRV_PG() - Initialise a pin group with drive control
 * @pg_name:	Pin group name (stringified, _pins appended to get pins array)
 */
#define DRV_PG(pg_name)				\
	{							\
		.name = #pg_name,				\
		.pins = pg_name##_pins,				\
		.npins = ARRAY_SIZE(pg_name##_pins),		\
		.drv = true,					\
	}

static const struct tz1090_pdc_pingroup tz1090_pdc_groups[] = {
	/* Muxing pin groups */
	/*     pg_name, f0,                 mux register,  mux bit */
	MUX_PG(gpio0,   IR_MOD_STABLE_OUT,  GPIO_CONTROL0, 7),
	MUX_PG(gpio1,   IR_MOD_POWER_OUT,   GPIO_CONTROL0, 6),

	/* Drive pin groups */
	/*     pg_name */
	DRV_PG(pdc),
};

/**
 * struct tz1090_pdc_pmx - Private pinctrl data
 * @dev:	Platform device
 * @pctl:	Pin control device
 * @regs:	Register region
 * @lock:	Lock protecting coherency of mux_en and gpio_en
 * @mux_en:	Muxes that have been enabled
 * @gpio_en:	Muxable GPIOs that have been enabled
 */
struct tz1090_pdc_pmx {
	struct device		*dev;
	struct pinctrl_dev	*pctl;
	void __iomem		*regs;
	spinlock_t		lock;
	u32			mux_en;
	u32			gpio_en;
};

static inline u32 pmx_read(struct tz1090_pdc_pmx *pmx, u32 reg)
{
	return ioread32(pmx->regs + reg);
}

static inline void pmx_write(struct tz1090_pdc_pmx *pmx, u32 val, u32 reg)
{
	iowrite32(val, pmx->regs + reg);
}

/*
 * Pin control operations
 */

static int tz1090_pdc_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(tz1090_pdc_groups);
}

static const char *tz1090_pdc_pinctrl_get_group_name(struct pinctrl_dev *pctl,
						     unsigned int group)
{
	return tz1090_pdc_groups[group].name;
}

static int tz1090_pdc_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					     unsigned int group,
					     const unsigned int **pins,
					     unsigned int *num_pins)
{
	*pins = tz1090_pdc_groups[group].pins;
	*num_pins = tz1090_pdc_groups[group].npins;

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static void tz1090_pdc_pinctrl_pin_dbg_show(struct pinctrl_dev *pctldev,
					    struct seq_file *s,
					    unsigned int offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}
#endif

static int reserve_map(struct device *dev, struct pinctrl_map **map,
		       unsigned int *reserved_maps, unsigned int *num_maps,
		       unsigned int reserve)
{
	unsigned int old_num = *reserved_maps;
	unsigned int new_num = *num_maps + reserve;
	struct pinctrl_map *new_map;

	if (old_num >= new_num)
		return 0;

	new_map = krealloc(*map, sizeof(*new_map) * new_num, GFP_KERNEL);
	if (!new_map) {
		dev_err(dev, "krealloc(map) failed\n");
		return -ENOMEM;
	}

	memset(new_map + old_num, 0, (new_num - old_num) * sizeof(*new_map));

	*map = new_map;
	*reserved_maps = new_num;

	return 0;
}

static int add_map_mux(struct pinctrl_map **map, unsigned int *reserved_maps,
		       unsigned int *num_maps, const char *group,
		       const char *function)
{
	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	(*map)[*num_maps].type = PIN_MAP_TYPE_MUX_GROUP;
	(*map)[*num_maps].data.mux.group = group;
	(*map)[*num_maps].data.mux.function = function;
	(*num_maps)++;

	return 0;
}

/**
 * get_group_selector() - returns the group selector for a group
 * @pin_group: the pin group to look up
 *
 * This is the same as pinctrl_get_group_selector except it doesn't produce an
 * error message if the group isn't found or debug messages.
 */
static int get_group_selector(const char *pin_group)
{
	unsigned int group;

	for (group = 0; group < ARRAY_SIZE(tz1090_pdc_groups); ++group)
		if (!strcmp(tz1090_pdc_groups[group].name, pin_group))
			return group;

	return -EINVAL;
}

static int add_map_configs(struct device *dev,
			   struct pinctrl_map **map,
			   unsigned int *reserved_maps, unsigned int *num_maps,
			   const char *group, unsigned long *configs,
			   unsigned int num_configs)
{
	unsigned long *dup_configs;
	enum pinctrl_map_type type;

	if (WARN_ON(*num_maps == *reserved_maps))
		return -ENOSPC;

	dup_configs = kmemdup(configs, num_configs * sizeof(*dup_configs),
			      GFP_KERNEL);
	if (!dup_configs) {
		dev_err(dev, "kmemdup(configs) failed\n");
		return -ENOMEM;
	}

	/*
	 * We support both pins and pin groups, but we need to figure out which
	 * one we have.
	 */
	if (get_group_selector(group) >= 0)
		type = PIN_MAP_TYPE_CONFIGS_GROUP;
	else
		type = PIN_MAP_TYPE_CONFIGS_PIN;
	(*map)[*num_maps].type = type;
	(*map)[*num_maps].data.configs.group_or_pin = group;
	(*map)[*num_maps].data.configs.configs = dup_configs;
	(*map)[*num_maps].data.configs.num_configs = num_configs;
	(*num_maps)++;

	return 0;
}

static void tz1090_pdc_pinctrl_dt_free_map(struct pinctrl_dev *pctldev,
					   struct pinctrl_map *map,
					   unsigned int num_maps)
{
	int i;

	for (i = 0; i < num_maps; i++)
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);

	kfree(map);
}

static int tz1090_pdc_pinctrl_dt_subnode_to_map(struct device *dev,
						struct device_node *np,
						struct pinctrl_map **map,
						unsigned int *reserved_maps,
						unsigned int *num_maps)
{
	int ret;
	const char *function;
	unsigned long *configs = NULL;
	unsigned int num_configs = 0;
	unsigned int reserve;
	struct property *prop;
	const char *group;

	ret = of_property_read_string(np, "tz1090,function", &function);
	if (ret < 0) {
		/* EINVAL=missing, which is fine since it's optional */
		if (ret != -EINVAL)
			dev_err(dev,
				"could not parse property function\n");
		function = NULL;
	}

	ret = pinconf_generic_parse_dt_config(np, &configs, &num_configs);
	if (ret)
		return ret;

	reserve = 0;
	if (function != NULL)
		reserve++;
	if (num_configs)
		reserve++;
	ret = of_property_count_strings(np, "tz1090,pins");
	if (ret < 0) {
		dev_err(dev, "could not parse property pins\n");
		goto exit;
	}
	reserve *= ret;

	ret = reserve_map(dev, map, reserved_maps, num_maps, reserve);
	if (ret < 0)
		goto exit;

	of_property_for_each_string(np, "tz1090,pins", prop, group) {
		if (function) {
			ret = add_map_mux(map, reserved_maps, num_maps,
					  group, function);
			if (ret < 0)
				goto exit;
		}

		if (num_configs) {
			ret = add_map_configs(dev, map, reserved_maps,
					      num_maps, group, configs,
					      num_configs);
			if (ret < 0)
				goto exit;
		}
	}

	ret = 0;

exit:
	kfree(configs);
	return ret;
}

static int tz1090_pdc_pinctrl_dt_node_to_map(struct pinctrl_dev *pctldev,
					     struct device_node *np_config,
					     struct pinctrl_map **map,
					     unsigned int *num_maps)
{
	unsigned int reserved_maps;
	struct device_node *np;
	int ret;

	reserved_maps = 0;
	*map = NULL;
	*num_maps = 0;

	for_each_child_of_node(np_config, np) {
		ret = tz1090_pdc_pinctrl_dt_subnode_to_map(pctldev->dev, np,
							   map, &reserved_maps,
							   num_maps);
		if (ret < 0) {
			tz1090_pdc_pinctrl_dt_free_map(pctldev, *map,
						       *num_maps);
			return ret;
		}
	}

	return 0;
}

static struct pinctrl_ops tz1090_pdc_pinctrl_ops = {
	.get_groups_count	= tz1090_pdc_pinctrl_get_groups_count,
	.get_group_name		= tz1090_pdc_pinctrl_get_group_name,
	.get_group_pins		= tz1090_pdc_pinctrl_get_group_pins,
#ifdef CONFIG_DEBUG_FS
	.pin_dbg_show		= tz1090_pdc_pinctrl_pin_dbg_show,
#endif
	.dt_node_to_map		= tz1090_pdc_pinctrl_dt_node_to_map,
	.dt_free_map		= tz1090_pdc_pinctrl_dt_free_map,
};

/*
 * Pin mux operations
 */

static int tz1090_pdc_pinctrl_get_funcs_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(tz1090_pdc_functions);
}

static const char *tz1090_pdc_pinctrl_get_func_name(struct pinctrl_dev *pctldev,
						    unsigned int function)
{
	return tz1090_pdc_functions[function].name;
}

static int tz1090_pdc_pinctrl_get_func_groups(struct pinctrl_dev *pctldev,
					      unsigned int function,
					      const char * const **groups,
					      unsigned int * const num_groups)
{
	*groups = tz1090_pdc_functions[function].groups;
	*num_groups = tz1090_pdc_functions[function].ngroups;

	return 0;
}

/**
 * tz1090_pdc_pinctrl_mux() - update mux bit
 * @pmx:		Pinmux data
 * @grp:		Pin mux group
 */
static void tz1090_pdc_pinctrl_mux(struct tz1090_pdc_pmx *pmx,
				   const struct tz1090_pdc_pingroup *grp)
{
	u32 reg, select;
	unsigned int pin_shift = grp->pins[0];
	unsigned long flags;

	/* select = mux && !gpio */
	select = ((pmx->mux_en & ~pmx->gpio_en) >> pin_shift) & 1;

	/* set up the mux */
	__global_lock2(flags);
	reg = pmx_read(pmx, grp->reg);
	reg &= ~BIT(grp->bit);
	reg |= select << grp->bit;
	pmx_write(pmx, reg, grp->reg);
	__global_unlock2(flags);
}

static int tz1090_pdc_pinctrl_enable(struct pinctrl_dev *pctldev,
				     unsigned int function, unsigned int group)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pdc_pingroup *grp = &tz1090_pdc_groups[group];

	dev_dbg(pctldev->dev, "%s(func=%u (%s), group=%u (%s))\n",
		__func__,
		function, tz1090_pdc_functions[function].name,
		group, tz1090_pdc_groups[group].name);

	/* is it even a mux? */
	if (grp->drv)
		return -EINVAL;

	/* does this group even control the function? */
	if (function != grp->func)
		return -EINVAL;

	/* record the pin being muxed and update mux bit */
	spin_lock(&pmx->lock);
	pmx->mux_en |= BIT(grp->pins[0]);
	tz1090_pdc_pinctrl_mux(pmx, grp);
	spin_unlock(&pmx->lock);
	return 0;
}

static void tz1090_pdc_pinctrl_disable(struct pinctrl_dev *pctldev,
				       unsigned int function,
				       unsigned int group)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pdc_pingroup *grp = &tz1090_pdc_groups[group];

	dev_dbg(pctldev->dev, "%s(func=%u (%s), group=%u (%s))\n",
		__func__,
		function, tz1090_pdc_functions[function].name,
		group, tz1090_pdc_groups[group].name);

	/* is it even a mux? */
	if (grp->drv)
		return;

	/* does this group even control the function? */
	if (function != grp->func)
		return;

	/* record the pin being unmuxed and update mux bit */
	spin_lock(&pmx->lock);
	pmx->mux_en &= ~BIT(grp->pins[0]);
	tz1090_pdc_pinctrl_mux(pmx, grp);
	spin_unlock(&pmx->lock);
}

static const struct tz1090_pdc_pingroup *find_mux_group(
						struct tz1090_pdc_pmx *pmx,
						unsigned int pin)
{
	const struct tz1090_pdc_pingroup *grp;
	unsigned int group;

	grp = tz1090_pdc_groups;
	for (group = 0; group < ARRAY_SIZE(tz1090_pdc_groups); ++group, ++grp) {
		/* only match muxes */
		if (grp->drv)
			continue;

		/* with a matching pin */
		if (grp->pins[0] == pin)
			return grp;
	}

	return NULL;
}

static int tz1090_pdc_pinctrl_gpio_request_enable(
					struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned int pin)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pdc_pingroup *grp = find_mux_group(pmx, pin);

	if (grp) {
		/* record the pin in GPIO use and update mux bit */
		spin_lock(&pmx->lock);
		pmx->gpio_en |= BIT(pin);
		tz1090_pdc_pinctrl_mux(pmx, grp);
		spin_unlock(&pmx->lock);
	}
	return 0;
}

static void tz1090_pdc_pinctrl_gpio_disable_free(
					struct pinctrl_dev *pctldev,
					struct pinctrl_gpio_range *range,
					unsigned int pin)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pdc_pingroup *grp = find_mux_group(pmx, pin);

	if (grp) {
		/* record the pin not in GPIO use and update mux bit */
		spin_lock(&pmx->lock);
		pmx->gpio_en &= ~BIT(pin);
		tz1090_pdc_pinctrl_mux(pmx, grp);
		spin_unlock(&pmx->lock);
	}
}

static struct pinmux_ops tz1090_pdc_pinmux_ops = {
	.get_functions_count	= tz1090_pdc_pinctrl_get_funcs_count,
	.get_function_name	= tz1090_pdc_pinctrl_get_func_name,
	.get_function_groups	= tz1090_pdc_pinctrl_get_func_groups,
	.enable			= tz1090_pdc_pinctrl_enable,
	.disable		= tz1090_pdc_pinctrl_disable,
	.gpio_request_enable	= tz1090_pdc_pinctrl_gpio_request_enable,
	.gpio_disable_free	= tz1090_pdc_pinctrl_gpio_disable_free,
};

/*
 * Pin config operations
 */

static int tz1090_pdc_pinconf_reg(struct pinctrl_dev *pctldev,
				  unsigned int pin,
				  enum pin_config_param param,
				  bool report_err,
				  u32 *reg, u32 *width, u32 *mask, u32 *shift,
				  u32 *val)
{
	/* Find information about parameter's register */
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		*val = REG_PU_PD_TRISTATE;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		*val = REG_PU_PD_UP;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*val = REG_PU_PD_DOWN;
		break;
	case PIN_CONFIG_BIAS_BUS_HOLD:
		*val = REG_PU_PD_REPEATER;
		break;
	default:
		return -ENOTSUPP;
	};

	/* Only input bias parameters supported */
	*reg = REG_GPIO_CONTROL2;
	*shift = REG_GPIO_CONTROL2_PU_PD_S + pin*2;
	*width = 2;

	/* Calculate field information */
	*mask = (BIT(*width) - 1) << *shift;

	return 0;
}

static int tz1090_pdc_pinconf_get(struct pinctrl_dev *pctldev,
				  unsigned int pin, unsigned long *config)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	int ret;
	u32 reg, width, mask, shift, val, tmp, arg;

	/* Get register information */
	ret = tz1090_pdc_pinconf_reg(pctldev, pin, param, true,
				     &reg, &width, &mask, &shift, &val);
	if (ret < 0)
		return ret;

	/* Extract field from register */
	tmp = pmx_read(pmx, reg);
	arg = ((tmp & mask) >> shift) == val;

	/* Config not active */
	if (!arg)
		return -EINVAL;

	/* And pack config */
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int tz1090_pdc_pinconf_set(struct pinctrl_dev *pctldev,
				  unsigned int pin, unsigned long config)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(config);
	unsigned int arg = pinconf_to_config_argument(config);
	int ret;
	u32 reg, width, mask, shift, val, tmp;
	unsigned long flags;

	dev_dbg(pctldev->dev, "%s(pin=%s, config=%#lx)\n",
		__func__, tz1090_pdc_pins[pin].name, config);

	/* Get register information */
	ret = tz1090_pdc_pinconf_reg(pctldev, pin, param, true,
				     &reg, &width, &mask, &shift, &val);
	if (ret < 0)
		return ret;

	/* Unpack argument and range check it */
	if (arg > 1) {
		dev_dbg(pctldev->dev, "%s: arg %u out of range\n",
			__func__, arg);
		return -EINVAL;
	}

	/* Write register field */
	__global_lock2(flags);
	tmp = pmx_read(pmx, reg);
	tmp &= ~mask;
	if (arg)
		tmp |= val << shift;
	pmx_write(pmx, tmp, reg);
	__global_unlock2(flags);

	return 0;
}

static const int tz1090_pdc_boolean_map[] = {
	[0]		= -EINVAL,
	[1]		= 1,
};

static const int tz1090_pdc_dr_map[] = {
	[REG_DR_2mA]	= 2,
	[REG_DR_4mA]	= 4,
	[REG_DR_8mA]	= 8,
	[REG_DR_12mA]	= 12,
};

static int tz1090_pdc_pinconf_group_reg(struct pinctrl_dev *pctldev,
					const struct tz1090_pdc_pingroup *g,
					enum pin_config_param param,
					bool report_err, u32 *reg, u32 *width,
					u32 *mask, u32 *shift, const int **map)
{
	/* Drive configuration applies in groups, but not to all groups. */
	if (!g->drv) {
		if (report_err)
			dev_dbg(pctldev->dev,
				"%s: group %s has no drive control\n",
				__func__, g->name);
		return -ENOTSUPP;
	}

	/* Find information about drive parameter's register */
	*reg = REG_GPIO_CONTROL2;
	switch (param) {
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		*shift = REG_GPIO_CONTROL2_PDC_SCHMITT_S;
		*width = 1;
		*map = tz1090_pdc_boolean_map;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		*shift = REG_GPIO_CONTROL2_PDC_DR_S;
		*width = 2;
		*map = tz1090_pdc_dr_map;
		break;
	case PIN_CONFIG_LOW_POWER_MODE:
		*shift = REG_GPIO_CONTROL2_PDC_POS_S;
		*width = 1;
		*map = tz1090_pdc_boolean_map;
		break;
	default:
		return -ENOTSUPP;
	};

	/* Calculate field information */
	*mask = (BIT(*width) - 1) << *shift;

	return 0;
}

static int tz1090_pdc_pinconf_group_get(struct pinctrl_dev *pctldev,
					unsigned int group,
					unsigned long *config)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pdc_pingroup *g = &tz1090_pdc_groups[group];
	enum pin_config_param param = pinconf_to_config_param(*config);
	int ret, arg;
	u32 reg, width, mask, shift, val;
	const int *map;

	/* Get register information */
	ret = tz1090_pdc_pinconf_group_reg(pctldev, g, param, true,
					   &reg, &width, &mask, &shift, &map);
	if (ret < 0)
		return ret;

	/* Extract field from register */
	val = pmx_read(pmx, reg);
	arg = map[(val & mask) >> shift];
	if (arg < 0)
		return arg;

	/* And pack config */
	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int tz1090_pdc_pinconf_group_set(struct pinctrl_dev *pctldev,
					unsigned int group,
					unsigned long config)
{
	struct tz1090_pdc_pmx *pmx = pinctrl_dev_get_drvdata(pctldev);
	const struct tz1090_pdc_pingroup *g = &tz1090_pdc_groups[group];
	enum pin_config_param param = pinconf_to_config_param(config);
	const unsigned int *pit;
	unsigned int i;
	int ret, arg;
	u32 reg, width, mask, shift, val;
	unsigned long flags;
	const int *map;

	dev_dbg(pctldev->dev, "%s(group=%s, config=%#lx)\n",
		__func__, g->name, config);

	/* Get register information */
	ret = tz1090_pdc_pinconf_group_reg(pctldev, g, param, true,
					   &reg, &width, &mask, &shift, &map);
	if (ret < 0) {
		/*
		 * Maybe we're trying to set a per-pin configuration of a group,
		 * so do the pins one by one. This is mainly as a convenience.
		 */
		for (i = 0, pit = g->pins; i < g->npins; ++i, ++pit) {
			ret = tz1090_pdc_pinconf_set(pctldev, *pit, config);
			if (ret)
				return ret;
		}
		return 0;
	}

	/* Unpack argument and map it to register value */
	arg = pinconf_to_config_argument(config);
	for (i = 0; i < BIT(width); ++i) {
		if (map[i] == arg || (map[i] == -EINVAL && !arg)) {
			/* Write register field */
			__global_lock2(flags);
			val = pmx_read(pmx, reg);
			val &= ~mask;
			val |= i << shift;
			pmx_write(pmx, val, reg);
			__global_unlock2(flags);
			return 0;
		}
	}

	dev_dbg(pctldev->dev, "%s: arg %u not supported\n",
		__func__, arg);
	return 0;
}

static struct pinconf_ops tz1090_pdc_pinconf_ops = {
	.is_generic			= true,
	.pin_config_get			= tz1090_pdc_pinconf_get,
	.pin_config_set			= tz1090_pdc_pinconf_set,
	.pin_config_group_get		= tz1090_pdc_pinconf_group_get,
	.pin_config_group_set		= tz1090_pdc_pinconf_group_set,
	.pin_config_config_dbg_show	= pinconf_generic_dump_config,
};

/*
 * Pin control driver setup
 */

static struct pinctrl_desc tz1090_pdc_pinctrl_desc = {
	.pctlops	= &tz1090_pdc_pinctrl_ops,
	.pmxops		= &tz1090_pdc_pinmux_ops,
	.confops	= &tz1090_pdc_pinconf_ops,
	.owner		= THIS_MODULE,
};

static int tz1090_pdc_pinctrl_probe(struct platform_device *pdev)
{
	struct tz1090_pdc_pmx *pmx;
	struct resource *res;

	pmx = devm_kzalloc(&pdev->dev, sizeof(*pmx), GFP_KERNEL);
	if (!pmx) {
		dev_err(&pdev->dev, "Can't alloc tz1090_pdc_pmx\n");
		return -ENOMEM;
	}
	pmx->dev = &pdev->dev;
	spin_lock_init(&pmx->lock);

	tz1090_pdc_pinctrl_desc.name = dev_name(&pdev->dev);
	tz1090_pdc_pinctrl_desc.pins = tz1090_pdc_pins;
	tz1090_pdc_pinctrl_desc.npins = ARRAY_SIZE(tz1090_pdc_pins);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pmx->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pmx->regs))
		return PTR_ERR(pmx->regs);

	pmx->pctl = pinctrl_register(&tz1090_pdc_pinctrl_desc, &pdev->dev, pmx);
	if (!pmx->pctl) {
		dev_err(&pdev->dev, "Couldn't register pinctrl driver\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, pmx);

	dev_info(&pdev->dev, "TZ1090 PDC pinctrl driver initialised\n");

	return 0;
}

static int tz1090_pdc_pinctrl_remove(struct platform_device *pdev)
{
	struct tz1090_pdc_pmx *pmx = platform_get_drvdata(pdev);

	pinctrl_unregister(pmx->pctl);

	return 0;
}

static struct of_device_id tz1090_pdc_pinctrl_of_match[] = {
	{ .compatible = "img,tz1090-pdc-pinctrl", },
	{ },
};

static struct platform_driver tz1090_pdc_pinctrl_driver = {
	.driver = {
		.name		= "tz1090-pdc-pinctrl",
		.owner		= THIS_MODULE,
		.of_match_table	= tz1090_pdc_pinctrl_of_match,
	},
	.probe	= tz1090_pdc_pinctrl_probe,
	.remove	= tz1090_pdc_pinctrl_remove,
};

static int __init tz1090_pdc_pinctrl_init(void)
{
	return platform_driver_register(&tz1090_pdc_pinctrl_driver);
}
arch_initcall(tz1090_pdc_pinctrl_init);

static void __exit tz1090_pdc_pinctrl_exit(void)
{
	platform_driver_unregister(&tz1090_pdc_pinctrl_driver);
}
module_exit(tz1090_pdc_pinctrl_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("Toumaz Xenif TZ1090 PDC pinctrl driver");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, tz1090_pdc_pinctrl_of_match);
