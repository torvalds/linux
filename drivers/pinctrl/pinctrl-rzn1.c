// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2018 Renesas Electronics Europe Limited
 *
 * Phil Edworthy <phil.edworthy@renesas.com>
 * Based on a driver originally written by Michel Pollet at Renesas.
 */

#include <dt-bindings/pinctrl/rzn1-pinctrl.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include "core.h"
#include "pinconf.h"
#include "pinctrl-utils.h"

/* Field positions and masks in the pinmux registers */
#define RZN1_L1_PIN_DRIVE_STRENGTH	10
#define RZN1_L1_PIN_DRIVE_STRENGTH_4MA	0
#define RZN1_L1_PIN_DRIVE_STRENGTH_6MA	1
#define RZN1_L1_PIN_DRIVE_STRENGTH_8MA	2
#define RZN1_L1_PIN_DRIVE_STRENGTH_12MA	3
#define RZN1_L1_PIN_PULL		8
#define RZN1_L1_PIN_PULL_NONE		0
#define RZN1_L1_PIN_PULL_UP		1
#define RZN1_L1_PIN_PULL_DOWN		3
#define RZN1_L1_FUNCTION		0
#define RZN1_L1_FUNC_MASK		0xf
#define RZN1_L1_FUNCTION_L2		0xf

/*
 * The hardware manual describes two levels of multiplexing, but it's more
 * logical to think of the hardware as three levels, with level 3 consisting of
 * the multiplexing for Ethernet MDIO signals.
 *
 * Level 1 functions go from 0 to 9, with level 1 function '15' (0xf) specifying
 * that level 2 functions are used instead. Level 2 has a lot more options,
 * going from 0 to 61. Level 3 allows selection of MDIO functions which can be
 * floating, or one of seven internal peripherals. Unfortunately, there are two
 * level 2 functions that can select MDIO, and two MDIO channels so we have four
 * sets of level 3 functions.
 *
 * For this driver, we've compounded the numbers together, so:
 *    0 to   9 is level 1
 *   10 to  71 is 10 + level 2 number
 *   72 to  79 is 72 + MDIO0 source for level 2 MDIO function.
 *   80 to  87 is 80 + MDIO0 source for level 2 MDIO_E1 function.
 *   88 to  95 is 88 + MDIO1 source for level 2 MDIO function.
 *   96 to 103 is 96 + MDIO1 source for level 2 MDIO_E1 function.
 * Examples:
 *  Function 28 corresponds UART0
 *  Function 73 corresponds to MDIO0 to GMAC0
 *
 * There are 170 configurable pins (called PL_GPIO in the datasheet).
 */

/*
 * Structure detailing the HW registers on the RZ/N1 devices.
 * Both the Level 1 mux registers and Level 2 mux registers have the same
 * structure. The only difference is that Level 2 has additional MDIO registers
 * at the end.
 */
struct rzn1_pinctrl_regs {
	u32	conf[170];
	u32	pad0[86];
	u32	status_protect;	/* 0x400 */
	/* MDIO mux registers, level2 only */
	u32	l2_mdio[2];
};

/**
 * struct rzn1_pmx_func - describes rzn1 pinmux functions
 * @name: the name of this specific function
 * @groups: corresponding pin groups
 * @num_groups: the number of groups
 */
struct rzn1_pmx_func {
	const char *name;
	const char **groups;
	unsigned int num_groups;
};

/**
 * struct rzn1_pin_group - describes an rzn1 pin group
 * @name: the name of this specific pin group
 * @func: the name of the function selected by this group
 * @npins: the number of pins in this group array, i.e. the number of
 *	elements in .pins so we can iterate over that array
 * @pins: array of pins. Needed due to pinctrl_ops.get_group_pins()
 * @pin_ids: array of pin_ids, i.e. the value used to select the mux
 */
struct rzn1_pin_group {
	const char *name;
	const char *func;
	unsigned int npins;
	unsigned int *pins;
	u8 *pin_ids;
};

struct rzn1_pinctrl {
	struct device *dev;
	struct clk *clk;
	struct pinctrl_dev *pctl;
	struct rzn1_pinctrl_regs __iomem *lev1;
	struct rzn1_pinctrl_regs __iomem *lev2;
	u32 lev1_protect_phys;
	u32 lev2_protect_phys;
	u32 mdio_func[2];

	struct rzn1_pin_group *groups;
	unsigned int ngroups;

	struct rzn1_pmx_func *functions;
	unsigned int nfunctions;
};

#define RZN1_PINS_PROP "pinmux"

#define RZN1_PIN(pin) PINCTRL_PIN(pin, "pl_gpio"#pin)

static const struct pinctrl_pin_desc rzn1_pins[] = {
	RZN1_PIN(0), RZN1_PIN(1), RZN1_PIN(2), RZN1_PIN(3), RZN1_PIN(4),
	RZN1_PIN(5), RZN1_PIN(6), RZN1_PIN(7), RZN1_PIN(8), RZN1_PIN(9),
	RZN1_PIN(10), RZN1_PIN(11), RZN1_PIN(12), RZN1_PIN(13), RZN1_PIN(14),
	RZN1_PIN(15), RZN1_PIN(16), RZN1_PIN(17), RZN1_PIN(18), RZN1_PIN(19),
	RZN1_PIN(20), RZN1_PIN(21), RZN1_PIN(22), RZN1_PIN(23), RZN1_PIN(24),
	RZN1_PIN(25), RZN1_PIN(26), RZN1_PIN(27), RZN1_PIN(28), RZN1_PIN(29),
	RZN1_PIN(30), RZN1_PIN(31), RZN1_PIN(32), RZN1_PIN(33), RZN1_PIN(34),
	RZN1_PIN(35), RZN1_PIN(36), RZN1_PIN(37), RZN1_PIN(38), RZN1_PIN(39),
	RZN1_PIN(40), RZN1_PIN(41), RZN1_PIN(42), RZN1_PIN(43), RZN1_PIN(44),
	RZN1_PIN(45), RZN1_PIN(46), RZN1_PIN(47), RZN1_PIN(48), RZN1_PIN(49),
	RZN1_PIN(50), RZN1_PIN(51), RZN1_PIN(52), RZN1_PIN(53), RZN1_PIN(54),
	RZN1_PIN(55), RZN1_PIN(56), RZN1_PIN(57), RZN1_PIN(58), RZN1_PIN(59),
	RZN1_PIN(60), RZN1_PIN(61), RZN1_PIN(62), RZN1_PIN(63), RZN1_PIN(64),
	RZN1_PIN(65), RZN1_PIN(66), RZN1_PIN(67), RZN1_PIN(68), RZN1_PIN(69),
	RZN1_PIN(70), RZN1_PIN(71), RZN1_PIN(72), RZN1_PIN(73), RZN1_PIN(74),
	RZN1_PIN(75), RZN1_PIN(76), RZN1_PIN(77), RZN1_PIN(78), RZN1_PIN(79),
	RZN1_PIN(80), RZN1_PIN(81), RZN1_PIN(82), RZN1_PIN(83), RZN1_PIN(84),
	RZN1_PIN(85), RZN1_PIN(86), RZN1_PIN(87), RZN1_PIN(88), RZN1_PIN(89),
	RZN1_PIN(90), RZN1_PIN(91), RZN1_PIN(92), RZN1_PIN(93), RZN1_PIN(94),
	RZN1_PIN(95), RZN1_PIN(96), RZN1_PIN(97), RZN1_PIN(98), RZN1_PIN(99),
	RZN1_PIN(100), RZN1_PIN(101), RZN1_PIN(102), RZN1_PIN(103),
	RZN1_PIN(104), RZN1_PIN(105), RZN1_PIN(106), RZN1_PIN(107),
	RZN1_PIN(108), RZN1_PIN(109), RZN1_PIN(110), RZN1_PIN(111),
	RZN1_PIN(112), RZN1_PIN(113), RZN1_PIN(114), RZN1_PIN(115),
	RZN1_PIN(116), RZN1_PIN(117), RZN1_PIN(118), RZN1_PIN(119),
	RZN1_PIN(120), RZN1_PIN(121), RZN1_PIN(122), RZN1_PIN(123),
	RZN1_PIN(124), RZN1_PIN(125), RZN1_PIN(126), RZN1_PIN(127),
	RZN1_PIN(128), RZN1_PIN(129), RZN1_PIN(130), RZN1_PIN(131),
	RZN1_PIN(132), RZN1_PIN(133), RZN1_PIN(134), RZN1_PIN(135),
	RZN1_PIN(136), RZN1_PIN(137), RZN1_PIN(138), RZN1_PIN(139),
	RZN1_PIN(140), RZN1_PIN(141), RZN1_PIN(142), RZN1_PIN(143),
	RZN1_PIN(144), RZN1_PIN(145), RZN1_PIN(146), RZN1_PIN(147),
	RZN1_PIN(148), RZN1_PIN(149), RZN1_PIN(150), RZN1_PIN(151),
	RZN1_PIN(152), RZN1_PIN(153), RZN1_PIN(154), RZN1_PIN(155),
	RZN1_PIN(156), RZN1_PIN(157), RZN1_PIN(158), RZN1_PIN(159),
	RZN1_PIN(160), RZN1_PIN(161), RZN1_PIN(162), RZN1_PIN(163),
	RZN1_PIN(164), RZN1_PIN(165), RZN1_PIN(166), RZN1_PIN(167),
	RZN1_PIN(168), RZN1_PIN(169),
};

enum {
	LOCK_LEVEL1 = 0x1,
	LOCK_LEVEL2 = 0x2,
	LOCK_ALL = LOCK_LEVEL1 | LOCK_LEVEL2,
};

static void rzn1_hw_set_lock(struct rzn1_pinctrl *ipctl, u8 lock, u8 value)
{
	/*
	 * The pinmux configuration is locked by writing the physical address of
	 * the status_protect register to itself. It is unlocked by writing the
	 * address | 1.
	 */
	if (lock & LOCK_LEVEL1) {
		u32 val = ipctl->lev1_protect_phys | !(value & LOCK_LEVEL1);

		writel(val, &ipctl->lev1->status_protect);
	}

	if (lock & LOCK_LEVEL2) {
		u32 val = ipctl->lev2_protect_phys | !(value & LOCK_LEVEL2);

		writel(val, &ipctl->lev2->status_protect);
	}
}

static void rzn1_pinctrl_mdio_select(struct rzn1_pinctrl *ipctl, int mdio,
				     u32 func)
{
	if (ipctl->mdio_func[mdio] >= 0 && ipctl->mdio_func[mdio] != func)
		dev_warn(ipctl->dev, "conflicting setting for mdio%d!\n", mdio);
	ipctl->mdio_func[mdio] = func;

	dev_dbg(ipctl->dev, "setting mdio%d to %u\n", mdio, func);

	writel(func, &ipctl->lev2->l2_mdio[mdio]);
}

/*
 * Using a composite pin description, set the hardware pinmux registers
 * with the corresponding values.
 * Make sure to unlock write protection and reset it afterward.
 *
 * NOTE: There is no protection for potential concurrency, it is assumed these
 * calls are serialized already.
 */
static int rzn1_set_hw_pin_func(struct rzn1_pinctrl *ipctl, unsigned int pin,
				u32 pin_config, u8 use_locks)
{
	u32 l1_cache;
	u32 l2_cache;
	u32 l1;
	u32 l2;

	/* Level 3 MDIO multiplexing */
	if (pin_config >= RZN1_FUNC_MDIO0_HIGHZ &&
	    pin_config <= RZN1_FUNC_MDIO1_E1_SWITCH) {
		int mdio_channel;
		u32 mdio_func;

		if (pin_config <= RZN1_FUNC_MDIO1_HIGHZ)
			mdio_channel = 0;
		else
			mdio_channel = 1;

		/* Get MDIO func, and convert the func to the level 2 number */
		if (pin_config <= RZN1_FUNC_MDIO0_SWITCH) {
			mdio_func = pin_config - RZN1_FUNC_MDIO0_HIGHZ;
			pin_config = RZN1_FUNC_ETH_MDIO;
		} else if (pin_config <= RZN1_FUNC_MDIO0_E1_SWITCH) {
			mdio_func = pin_config - RZN1_FUNC_MDIO0_E1_HIGHZ;
			pin_config = RZN1_FUNC_ETH_MDIO_E1;
		} else if (pin_config <= RZN1_FUNC_MDIO1_SWITCH) {
			mdio_func = pin_config - RZN1_FUNC_MDIO1_HIGHZ;
			pin_config = RZN1_FUNC_ETH_MDIO;
		} else {
			mdio_func = pin_config - RZN1_FUNC_MDIO1_E1_HIGHZ;
			pin_config = RZN1_FUNC_ETH_MDIO_E1;
		}
		rzn1_pinctrl_mdio_select(ipctl, mdio_channel, mdio_func);
	}

	/* Note here, we do not allow anything past the MDIO Mux values */
	if (pin >= ARRAY_SIZE(ipctl->lev1->conf) ||
	    pin_config >= RZN1_FUNC_MDIO0_HIGHZ)
		return -EINVAL;

	l1 = readl(&ipctl->lev1->conf[pin]);
	l1_cache = l1;
	l2 = readl(&ipctl->lev2->conf[pin]);
	l2_cache = l2;

	dev_dbg(ipctl->dev, "setting func for pin %u to %u\n", pin, pin_config);

	l1 &= ~(RZN1_L1_FUNC_MASK << RZN1_L1_FUNCTION);

	if (pin_config < RZN1_FUNC_L2_OFFSET) {
		l1 |= (pin_config << RZN1_L1_FUNCTION);
	} else {
		l1 |= (RZN1_L1_FUNCTION_L2 << RZN1_L1_FUNCTION);

		l2 = pin_config - RZN1_FUNC_L2_OFFSET;
	}

	/* If either configuration changes, we update both anyway */
	if (l1 != l1_cache || l2 != l2_cache) {
		writel(l1, &ipctl->lev1->conf[pin]);
		writel(l2, &ipctl->lev2->conf[pin]);
	}

	return 0;
}

static const struct rzn1_pin_group *rzn1_pinctrl_find_group_by_name(
	const struct rzn1_pinctrl *ipctl, const char *name)
{
	unsigned int i;

	for (i = 0; i < ipctl->ngroups; i++) {
		if (!strcmp(ipctl->groups[i].name, name))
			return &ipctl->groups[i];
	}

	return NULL;
}

static int rzn1_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	return ipctl->ngroups;
}

static const char *rzn1_get_group_name(struct pinctrl_dev *pctldev,
				       unsigned int selector)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	return ipctl->groups[selector].name;
}

static int rzn1_get_group_pins(struct pinctrl_dev *pctldev,
			       unsigned int selector, const unsigned int **pins,
			       unsigned int *npins)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= ipctl->ngroups)
		return -EINVAL;

	*pins = ipctl->groups[selector].pins;
	*npins = ipctl->groups[selector].npins;

	return 0;
}

/*
 * This function is called for each pinctl 'Function' node.
 * Sub-nodes can be used to describe multiple 'Groups' for the 'Function'
 * If there aren't any sub-nodes, the 'Group' is essentially the 'Function'.
 * Each 'Group' uses pinmux = <...> to detail the pins and data used to select
 * the functionality. Each 'Group' has optional pin configurations that apply
 * to all pins in the 'Group'.
 */
static int rzn1_dt_node_to_map_one(struct pinctrl_dev *pctldev,
				   struct device_node *np,
				   struct pinctrl_map **map,
				   unsigned int *num_maps)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	const struct rzn1_pin_group *grp;
	unsigned long *configs = NULL;
	unsigned int reserved_maps = *num_maps;
	unsigned int num_configs = 0;
	unsigned int reserve = 1;
	int ret;

	dev_dbg(ipctl->dev, "processing node %pOF\n", np);

	grp = rzn1_pinctrl_find_group_by_name(ipctl, np->name);
	if (!grp) {
		dev_err(ipctl->dev, "unable to find group for node %pOF\n", np);

		return -EINVAL;
	}

	/* Get the group's pin configuration */
	ret = pinconf_generic_parse_dt_config(np, pctldev, &configs,
					      &num_configs);
	if (ret < 0) {
		dev_err(ipctl->dev, "%pOF: could not parse property\n", np);

		return ret;
	}

	if (num_configs)
		reserve++;

	/* Increase the number of maps to cover this group */
	ret = pinctrl_utils_reserve_map(pctldev, map, &reserved_maps, num_maps,
					reserve);
	if (ret < 0)
		goto out;

	/* Associate the group with the function */
	ret = pinctrl_utils_add_map_mux(pctldev, map, &reserved_maps, num_maps,
					grp->name, grp->func);
	if (ret < 0)
		goto out;

	if (num_configs) {
		/* Associate the group's pin configuration with the group */
		ret = pinctrl_utils_add_map_configs(pctldev, map,
				&reserved_maps, num_maps, grp->name,
				configs, num_configs,
				PIN_MAP_TYPE_CONFIGS_GROUP);
		if (ret < 0)
			goto out;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s (%d pins)\n",
		grp->func, grp->name, grp->npins);

out:
	kfree(configs);

	return ret;
}

static int rzn1_dt_node_to_map(struct pinctrl_dev *pctldev,
			       struct device_node *np,
			       struct pinctrl_map **map,
			       unsigned int *num_maps)
{
	struct device_node *child;
	int ret;

	*map = NULL;
	*num_maps = 0;

	ret = rzn1_dt_node_to_map_one(pctldev, np, map, num_maps);
	if (ret < 0)
		return ret;

	for_each_child_of_node(np, child) {
		ret = rzn1_dt_node_to_map_one(pctldev, child, map, num_maps);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct pinctrl_ops rzn1_pctrl_ops = {
	.get_groups_count = rzn1_get_groups_count,
	.get_group_name = rzn1_get_group_name,
	.get_group_pins = rzn1_get_group_pins,
	.dt_node_to_map = rzn1_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

static int rzn1_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	return ipctl->nfunctions;
}

static const char *rzn1_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned int selector)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	return ipctl->functions[selector].name;
}

static int rzn1_pmx_get_groups(struct pinctrl_dev *pctldev,
			       unsigned int selector,
			       const char * const **groups,
			       unsigned int * const num_groups)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = ipctl->functions[selector].groups;
	*num_groups = ipctl->functions[selector].num_groups;

	return 0;
}

static int rzn1_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			unsigned int group)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzn1_pin_group *grp = &ipctl->groups[group];
	unsigned int i, grp_pins = grp->npins;

	dev_dbg(ipctl->dev, "set mux %s(%d) group %s(%d)\n",
		ipctl->functions[selector].name, selector, grp->name, group);

	rzn1_hw_set_lock(ipctl, LOCK_ALL, LOCK_ALL);
	for (i = 0; i < grp_pins; i++)
		rzn1_set_hw_pin_func(ipctl, grp->pins[i], grp->pin_ids[i], 0);
	rzn1_hw_set_lock(ipctl, LOCK_ALL, 0);

	return 0;
}

static const struct pinmux_ops rzn1_pmx_ops = {
	.get_functions_count = rzn1_pmx_get_funcs_count,
	.get_function_name = rzn1_pmx_get_func_name,
	.get_function_groups = rzn1_pmx_get_groups,
	.set_mux = rzn1_set_mux,
};

static int rzn1_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *config)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param = pinconf_to_config_param(*config);
	const u32 reg_drive[4] = { 4, 6, 8, 12 };
	u32 pull, drive, l1mux;
	u32 l1, l2, arg = 0;

	if (pin >= ARRAY_SIZE(ipctl->lev1->conf))
		return -EINVAL;

	l1 = readl(&ipctl->lev1->conf[pin]);

	l1mux = l1 & RZN1_L1_FUNC_MASK;
	pull = (l1 >> RZN1_L1_PIN_PULL) & 0x3;
	drive = (l1 >> RZN1_L1_PIN_DRIVE_STRENGTH) & 0x3;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		if (pull != RZN1_L1_PIN_PULL_UP)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (pull != RZN1_L1_PIN_PULL_DOWN)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		if (pull != RZN1_L1_PIN_PULL_NONE)
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = reg_drive[drive];
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		l2 = readl(&ipctl->lev2->conf[pin]);
		if (l1mux == RZN1_L1_FUNCTION_L2) {
			if (l2 != 0)
				return -EINVAL;
		} else if (l1mux != RZN1_FUNC_HIGHZ) {
			return -EINVAL;
		}
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int rzn1_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			    unsigned long *configs, unsigned int num_configs)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	unsigned int i;
	u32 l1, l1_cache;
	u32 drv;
	u32 arg;

	if (pin >= ARRAY_SIZE(ipctl->lev1->conf))
		return -EINVAL;

	l1 = readl(&ipctl->lev1->conf[pin]);
	l1_cache = l1;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			dev_dbg(ipctl->dev, "set pin %d pull up\n", pin);
			l1 &= ~(0x3 << RZN1_L1_PIN_PULL);
			l1 |= (RZN1_L1_PIN_PULL_UP << RZN1_L1_PIN_PULL);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			dev_dbg(ipctl->dev, "set pin %d pull down\n", pin);
			l1 &= ~(0x3 << RZN1_L1_PIN_PULL);
			l1 |= (RZN1_L1_PIN_PULL_DOWN << RZN1_L1_PIN_PULL);
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			dev_dbg(ipctl->dev, "set pin %d bias off\n", pin);
			l1 &= ~(0x3 << RZN1_L1_PIN_PULL);
			l1 |= (RZN1_L1_PIN_PULL_NONE << RZN1_L1_PIN_PULL);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			dev_dbg(ipctl->dev, "set pin %d drv %umA\n", pin, arg);
			switch (arg) {
			case 4:
				drv = RZN1_L1_PIN_DRIVE_STRENGTH_4MA;
				break;
			case 6:
				drv = RZN1_L1_PIN_DRIVE_STRENGTH_6MA;
				break;
			case 8:
				drv = RZN1_L1_PIN_DRIVE_STRENGTH_8MA;
				break;
			case 12:
				drv = RZN1_L1_PIN_DRIVE_STRENGTH_12MA;
				break;
			default:
				dev_err(ipctl->dev,
					"Drive strength %umA not supported\n",
					arg);

				return -EINVAL;
			}

			l1 &= ~(0x3 << RZN1_L1_PIN_DRIVE_STRENGTH);
			l1 |= (drv << RZN1_L1_PIN_DRIVE_STRENGTH);
			break;

		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			dev_dbg(ipctl->dev, "set pin %d High-Z\n", pin);
			l1 &= ~RZN1_L1_FUNC_MASK;
			l1 |= RZN1_FUNC_HIGHZ;
			break;
		default:
			return -ENOTSUPP;
		}
	}

	if (l1 != l1_cache) {
		rzn1_hw_set_lock(ipctl, LOCK_LEVEL1, LOCK_LEVEL1);
		writel(l1, &ipctl->lev1->conf[pin]);
		rzn1_hw_set_lock(ipctl, LOCK_LEVEL1, 0);
	}

	return 0;
}

static int rzn1_pinconf_group_get(struct pinctrl_dev *pctldev,
				  unsigned int selector,
				  unsigned long *config)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzn1_pin_group *grp = &ipctl->groups[selector];
	unsigned long old = 0;
	unsigned int i;

	dev_dbg(ipctl->dev, "group get %s selector:%u\n", grp->name, selector);

	for (i = 0; i < grp->npins; i++) {
		if (rzn1_pinconf_get(pctldev, grp->pins[i], config))
			return -ENOTSUPP;

		/* configs do not match between two pins */
		if (i && (old != *config))
			return -ENOTSUPP;

		old = *config;
	}

	return 0;
}

static int rzn1_pinconf_group_set(struct pinctrl_dev *pctldev,
				  unsigned int selector,
				  unsigned long *configs,
				  unsigned int num_configs)
{
	struct rzn1_pinctrl *ipctl = pinctrl_dev_get_drvdata(pctldev);
	struct rzn1_pin_group *grp = &ipctl->groups[selector];
	unsigned int i;
	int ret;

	dev_dbg(ipctl->dev, "group set %s selector:%u configs:%p/%d\n",
		grp->name, selector, configs, num_configs);

	for (i = 0; i < grp->npins; i++) {
		unsigned int pin = grp->pins[i];

		ret = rzn1_pinconf_set(pctldev, pin, configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops rzn1_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = rzn1_pinconf_get,
	.pin_config_set = rzn1_pinconf_set,
	.pin_config_group_get = rzn1_pinconf_group_get,
	.pin_config_group_set = rzn1_pinconf_group_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static struct pinctrl_desc rzn1_pinctrl_desc = {
	.pctlops = &rzn1_pctrl_ops,
	.pmxops = &rzn1_pmx_ops,
	.confops = &rzn1_pinconf_ops,
	.owner = THIS_MODULE,
};

static int rzn1_pinctrl_parse_groups(struct device_node *np,
				     struct rzn1_pin_group *grp,
				     struct rzn1_pinctrl *ipctl)
{
	const __be32 *list;
	unsigned int i;
	int size;

	dev_dbg(ipctl->dev, "%s: %s\n", __func__, np->name);

	/* Initialise group */
	grp->name = np->name;

	/*
	 * The binding format is
	 *	pinmux = <PIN_FUNC_ID CONFIG ...>,
	 * do sanity check and calculate pins number
	 */
	list = of_get_property(np, RZN1_PINS_PROP, &size);
	if (!list) {
		dev_err(ipctl->dev,
			"no " RZN1_PINS_PROP " property in node %pOF\n", np);

		return -EINVAL;
	}

	if (!size) {
		dev_err(ipctl->dev, "Invalid " RZN1_PINS_PROP " in node %pOF\n",
			np);

		return -EINVAL;
	}

	grp->npins = size / sizeof(list[0]);
	grp->pin_ids = devm_kmalloc_array(ipctl->dev,
					  grp->npins, sizeof(grp->pin_ids[0]),
					  GFP_KERNEL);
	grp->pins = devm_kmalloc_array(ipctl->dev,
				       grp->npins, sizeof(grp->pins[0]),
				       GFP_KERNEL);
	if (!grp->pin_ids || !grp->pins)
		return -ENOMEM;

	for (i = 0; i < grp->npins; i++) {
		u32 pin_id = be32_to_cpu(*list++);

		grp->pins[i] = pin_id & 0xff;
		grp->pin_ids[i] = (pin_id >> 8) & 0x7f;
	}

	return grp->npins;
}

static int rzn1_pinctrl_count_function_groups(struct device_node *np)
{
	struct device_node *child;
	int count = 0;

	if (of_property_count_u32_elems(np, RZN1_PINS_PROP) > 0)
		count++;

	for_each_child_of_node(np, child) {
		if (of_property_count_u32_elems(child, RZN1_PINS_PROP) > 0)
			count++;
	}

	return count;
}

static int rzn1_pinctrl_parse_functions(struct device_node *np,
					struct rzn1_pinctrl *ipctl,
					unsigned int index)
{
	struct rzn1_pmx_func *func;
	struct rzn1_pin_group *grp;
	struct device_node *child;
	unsigned int i = 0;
	int ret;

	func = &ipctl->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->num_groups = rzn1_pinctrl_count_function_groups(np);
	if (func->num_groups == 0) {
		dev_err(ipctl->dev, "no groups defined in %pOF\n", np);
		return -EINVAL;
	}
	dev_dbg(ipctl->dev, "function %s has %d groups\n",
		np->name, func->num_groups);

	func->groups = devm_kmalloc_array(ipctl->dev,
					  func->num_groups, sizeof(char *),
					  GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	if (of_property_count_u32_elems(np, RZN1_PINS_PROP) > 0) {
		func->groups[i] = np->name;
		grp = &ipctl->groups[ipctl->ngroups];
		grp->func = func->name;
		ret = rzn1_pinctrl_parse_groups(np, grp, ipctl);
		if (ret < 0)
			return ret;
		i++;
		ipctl->ngroups++;
	}

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &ipctl->groups[ipctl->ngroups];
		grp->func = func->name;
		ret = rzn1_pinctrl_parse_groups(child, grp, ipctl);
		if (ret < 0)
			return ret;
		i++;
		ipctl->ngroups++;
	}

	dev_dbg(ipctl->dev, "function %s parsed %u/%u groups\n",
		np->name, i, func->num_groups);

	return 0;
}

static int rzn1_pinctrl_probe_dt(struct platform_device *pdev,
				 struct rzn1_pinctrl *ipctl)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child;
	unsigned int maxgroups = 0;
	unsigned int nfuncs = 0;
	unsigned int i = 0;
	int ret;

	nfuncs = of_get_child_count(np);
	if (nfuncs <= 0)
		return 0;

	ipctl->nfunctions = nfuncs;
	ipctl->functions = devm_kmalloc_array(&pdev->dev, nfuncs,
					      sizeof(*ipctl->functions),
					      GFP_KERNEL);
	if (!ipctl->functions)
		return -ENOMEM;

	ipctl->ngroups = 0;
	for_each_child_of_node(np, child)
		maxgroups += rzn1_pinctrl_count_function_groups(child);

	ipctl->groups = devm_kmalloc_array(&pdev->dev,
					   maxgroups,
					   sizeof(*ipctl->groups),
					   GFP_KERNEL);
	if (!ipctl->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		ret = rzn1_pinctrl_parse_functions(child, ipctl, i++);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int rzn1_pinctrl_probe(struct platform_device *pdev)
{
	struct rzn1_pinctrl *ipctl;
	struct resource *res;
	int ret;

	/* Create state holders etc for this driver */
	ipctl = devm_kzalloc(&pdev->dev, sizeof(*ipctl), GFP_KERNEL);
	if (!ipctl)
		return -ENOMEM;

	ipctl->mdio_func[0] = -1;
	ipctl->mdio_func[1] = -1;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ipctl->lev1_protect_phys = (u32)res->start + 0x400;
	ipctl->lev1 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ipctl->lev1))
		return PTR_ERR(ipctl->lev1);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	ipctl->lev2_protect_phys = (u32)res->start + 0x400;
	ipctl->lev2 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ipctl->lev2))
		return PTR_ERR(ipctl->lev2);

	ipctl->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ipctl->clk))
		return PTR_ERR(ipctl->clk);
	ret = clk_prepare_enable(ipctl->clk);
	if (ret)
		return ret;

	ipctl->dev = &pdev->dev;
	rzn1_pinctrl_desc.name = dev_name(&pdev->dev);
	rzn1_pinctrl_desc.pins = rzn1_pins;
	rzn1_pinctrl_desc.npins = ARRAY_SIZE(rzn1_pins);

	ret = rzn1_pinctrl_probe_dt(pdev, ipctl);
	if (ret) {
		dev_err(&pdev->dev, "fail to probe dt properties\n");
		goto err_clk;
	}

	platform_set_drvdata(pdev, ipctl);

	ret = devm_pinctrl_register_and_init(&pdev->dev, &rzn1_pinctrl_desc,
					     ipctl, &ipctl->pctl);
	if (ret) {
		dev_err(&pdev->dev, "could not register rzn1 pinctrl driver\n");
		goto err_clk;
	}

	ret = pinctrl_enable(ipctl->pctl);
	if (ret)
		goto err_clk;

	dev_info(&pdev->dev, "probed\n");

	return 0;

err_clk:
	clk_disable_unprepare(ipctl->clk);

	return ret;
}

static int rzn1_pinctrl_remove(struct platform_device *pdev)
{
	struct rzn1_pinctrl *ipctl = platform_get_drvdata(pdev);

	clk_disable_unprepare(ipctl->clk);

	return 0;
}

static const struct of_device_id rzn1_pinctrl_match[] = {
	{ .compatible = "renesas,rzn1-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, rzn1_pinctrl_match);

static struct platform_driver rzn1_pinctrl_driver = {
	.probe	= rzn1_pinctrl_probe,
	.remove = rzn1_pinctrl_remove,
	.driver	= {
		.name		= "rzn1-pinctrl",
		.of_match_table	= rzn1_pinctrl_match,
	},
};

static int __init _pinctrl_drv_register(void)
{
	return platform_driver_register(&rzn1_pinctrl_driver);
}
subsys_initcall(_pinctrl_drv_register);

MODULE_AUTHOR("Phil Edworthy <phil.edworthy@renesas.com>");
MODULE_DESCRIPTION("Renesas RZ/N1 pinctrl driver");
MODULE_LICENSE("GPL v2");
