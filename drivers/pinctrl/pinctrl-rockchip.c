/*
 * Pinctrl driver for Rockchip SoCs
 *
 * Copyright (c) 2013 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * With some ideas taken from pinctrl-samsung:
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2012 Linaro Ltd
 *		http://www.linaro.org
 *
 * and pinctrl-at91:
 * Copyright (C) 2011-2012 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/gpio.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/rockchip/cpu.h>
#include <dt-bindings/pinctrl/rockchip.h>

#include "core.h"
#include "pinconf.h"
#include "pinctrl-rockchip.h"

#define RK_GENMASK_VAL(h, l, v) \
	(GENMASK(((h) + 16), ((l) + 16)) | (((v) << (l)) & GENMASK((h), (l))))

/**
 * Encode variants of iomux registers into a type variable
 */
#define IOMUX_GPIO_ONLY		BIT(0)
#define IOMUX_WIDTH_4BIT	BIT(1)
#define IOMUX_SOURCE_PMU	BIT(2)
#define IOMUX_UNROUTED		BIT(3)
#define IOMUX_WIDTH_3BIT	BIT(4)
#define IOMUX_8WIDTH_2BIT	BIT(5)
#define IOMUX_WRITABLE_32BIT	BIT(6)
#define IOMUX_L_SOURCE_PMU	BIT(7)

#define PIN_BANK(id, pins, label)			\
	{						\
		.bank_num	= id,			\
		.nr_pins	= pins,			\
		.name		= label,		\
		.iomux		= {			\
			{ .offset = -1 },		\
			{ .offset = -1 },		\
			{ .offset = -1 },		\
			{ .offset = -1 },		\
		},					\
	}

#define PIN_BANK_IOMUX_FLAGS(id, pins, label, iom0, iom1, iom2, iom3)	\
	{								\
		.bank_num	= id,					\
		.nr_pins	= pins,					\
		.name		= label,				\
		.iomux		= {					\
			{ .type = iom0, .offset = -1 },			\
			{ .type = iom1, .offset = -1 },			\
			{ .type = iom2, .offset = -1 },			\
			{ .type = iom3, .offset = -1 },			\
		},							\
	}

#define PIN_BANK_IOMUX_FLAGS_OFFSET(id, pins, label, iom0, iom1, iom2,	\
				    iom3, offset0, offset1, offset2,	\
				    offset3)				\
	{								\
		.bank_num	= id,					\
		.nr_pins	= pins,					\
		.name		= label,				\
		.iomux		= {					\
			{ .type = iom0, .offset = offset0 },		\
			{ .type = iom1, .offset = offset1 },		\
			{ .type = iom2, .offset = offset2 },		\
			{ .type = iom3, .offset = offset3 },		\
		},							\
	}

#define PIN_BANK_DRV_FLAGS(id, pins, label, type0, type1, type2, type3) \
	{								\
		.bank_num	= id,					\
		.nr_pins	= pins,					\
		.name		= label,				\
		.iomux		= {					\
			{ .offset = -1 },				\
			{ .offset = -1 },				\
			{ .offset = -1 },				\
			{ .offset = -1 },				\
		},							\
		.drv		= {					\
			{ .drv_type = type0, .offset = -1 },		\
			{ .drv_type = type1, .offset = -1 },		\
			{ .drv_type = type2, .offset = -1 },		\
			{ .drv_type = type3, .offset = -1 },		\
		},							\
	}

#define PIN_BANK_DRV_FLAGS_PULL_FLAGS(id, pins, label, drv0, drv1,	\
				      drv2, drv3, pull0, pull1,		\
				      pull2, pull3)			\
	{								\
		.bank_num	= id,					\
		.nr_pins	= pins,					\
		.name		= label,				\
		.iomux		= {					\
			{ .offset = -1 },				\
			{ .offset = -1 },				\
			{ .offset = -1 },				\
			{ .offset = -1 },				\
		},							\
		.drv		= {					\
			{ .drv_type = drv0, .offset = -1 },		\
			{ .drv_type = drv1, .offset = -1 },		\
			{ .drv_type = drv2, .offset = -1 },		\
			{ .drv_type = drv3, .offset = -1 },		\
		},							\
		.pull_type[0] = pull0,					\
		.pull_type[1] = pull1,					\
		.pull_type[2] = pull2,					\
		.pull_type[3] = pull3,					\
	}

#define PIN_BANK_IOMUX_DRV_FLAGS_OFFSET(id, pins, label, iom0, iom1,	\
					iom2, iom3, drv0, drv1, drv2,	\
					drv3, offset0, offset1,		\
					offset2, offset3)		\
	{								\
		.bank_num	= id,					\
		.nr_pins	= pins,					\
		.name		= label,				\
		.iomux		= {					\
			{ .type = iom0, .offset = -1 },			\
			{ .type = iom1, .offset = -1 },			\
			{ .type = iom2, .offset = -1 },			\
			{ .type = iom3, .offset = -1 },			\
		},							\
		.drv		= {					\
			{ .drv_type = drv0, .offset = offset0 },	\
			{ .drv_type = drv1, .offset = offset1 },	\
			{ .drv_type = drv2, .offset = offset2 },	\
			{ .drv_type = drv3, .offset = offset3 },	\
		},							\
	}

#define PIN_BANK_IOMUX_FLAGS_DRV_FLAGS_OFFSET_PULL_FLAGS(id, pins,	\
					      label, iom0, iom1, iom2,  \
					      iom3, drv0, drv1, drv2,   \
					      drv3, offset0, offset1,   \
					      offset2, offset3, pull0,  \
					      pull1, pull2, pull3)	\
	{								\
		.bank_num	= id,					\
		.nr_pins	= pins,					\
		.name		= label,				\
		.iomux		= {					\
			{ .type = iom0, .offset = -1 },			\
			{ .type = iom1, .offset = -1 },			\
			{ .type = iom2, .offset = -1 },			\
			{ .type = iom3, .offset = -1 },			\
		},							\
		.drv		= {					\
			{ .drv_type = drv0, .offset = offset0 },	\
			{ .drv_type = drv1, .offset = offset1 },	\
			{ .drv_type = drv2, .offset = offset2 },	\
			{ .drv_type = drv3, .offset = offset3 },	\
		},							\
		.pull_type[0] = pull0,					\
		.pull_type[1] = pull1,					\
		.pull_type[2] = pull2,					\
		.pull_type[3] = pull3,					\
	}

#define PIN_BANK_MUX_ROUTE_FLAGS(ID, PIN, FUNC, REG, VAL, FLAG)		\
	{								\
		.bank_num	= ID,					\
		.pin		= PIN,					\
		.func		= FUNC,					\
		.route_offset	= REG,					\
		.route_val	= VAL,					\
		.route_type	= FLAG,					\
	}

#define MR_DEFAULT(ID, PIN, FUNC, REG, VAL)	\
	PIN_BANK_MUX_ROUTE_FLAGS(ID, PIN, FUNC, REG, VAL, ROUTE_TYPE_DEFAULT)

#define MR_TOPGRF(ID, PIN, FUNC, REG, VAL)	\
	PIN_BANK_MUX_ROUTE_FLAGS(ID, PIN, FUNC, REG, VAL, ROUTE_TYPE_TOPGRF)

#define MR_PMUGRF(ID, PIN, FUNC, REG, VAL)	\
	PIN_BANK_MUX_ROUTE_FLAGS(ID, PIN, FUNC, REG, VAL, ROUTE_TYPE_PMUGRF)

static struct regmap_config rockchip_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
};

static inline const struct rockchip_pin_group *pinctrl_name_to_group(
					const struct rockchip_pinctrl *info,
					const char *name)
{
	int i;

	for (i = 0; i < info->ngroups; i++) {
		if (!strcmp(info->groups[i].name, name))
			return &info->groups[i];
	}

	return NULL;
}

/*
 * given a pin number that is local to a pin controller, find out the pin bank
 * and the register base of the pin bank.
 */
static struct rockchip_pin_bank *pin_to_bank(struct rockchip_pinctrl *info,
								unsigned pin)
{
	struct rockchip_pin_bank *b = info->ctrl->pin_banks;

	while (pin >= (b->pin_base + b->nr_pins))
		b++;

	return b;
}

static struct rockchip_pin_bank *bank_num_to_bank(
					struct rockchip_pinctrl *info,
					unsigned num)
{
	struct rockchip_pin_bank *b = info->ctrl->pin_banks;
	int i;

	for (i = 0; i < info->ctrl->nr_banks; i++, b++) {
		if (b->bank_num == num)
			return b;
	}

	return ERR_PTR(-EINVAL);
}

/*
 * Pinctrl_ops handling
 */

static int rockchip_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->ngroups;
}

static const char *rockchip_get_group_name(struct pinctrl_dev *pctldev,
							unsigned selector)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->groups[selector].name;
}

static int rockchip_get_group_pins(struct pinctrl_dev *pctldev,
				      unsigned selector, const unsigned **pins,
				      unsigned *npins)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	if (selector >= info->ngroups)
		return -EINVAL;

	*pins = info->groups[selector].pins;
	*npins = info->groups[selector].npins;

	return 0;
}

static int rockchip_dt_node_to_map(struct pinctrl_dev *pctldev,
				 struct device_node *np,
				 struct pinctrl_map **map, unsigned *num_maps)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const struct rockchip_pin_group *grp;
	struct pinctrl_map *new_map;
	struct device_node *parent;
	int map_num = 1;
	int i;

	/*
	 * first find the group of this node and check if we need to create
	 * config maps for pins
	 */
	grp = pinctrl_name_to_group(info, np->name);
	if (!grp) {
		dev_err(info->dev, "unable to find group for node %s\n",
			np->name);
		return -EINVAL;
	}

	map_num += grp->npins;

	new_map = kcalloc(map_num, sizeof(*new_map), GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	*map = new_map;
	*num_maps = map_num;

	/* create mux map */
	parent = of_get_parent(np);
	if (!parent) {
		kfree(new_map);
		return -EINVAL;
	}
	new_map[0].type = PIN_MAP_TYPE_MUX_GROUP;
	new_map[0].data.mux.function = parent->name;
	new_map[0].data.mux.group = np->name;
	of_node_put(parent);

	/* create config map */
	new_map++;
	for (i = 0; i < grp->npins; i++) {
		new_map[i].type = PIN_MAP_TYPE_CONFIGS_PIN;
		new_map[i].data.configs.group_or_pin =
				pin_get_name(pctldev, grp->pins[i]);
		new_map[i].data.configs.configs = grp->data[i].configs;
		new_map[i].data.configs.num_configs = grp->data[i].nconfigs;
	}

	dev_dbg(pctldev->dev, "maps: function %s group %s num %d\n",
		(*map)->data.mux.function, (*map)->data.mux.group, map_num);

	return 0;
}

static void rockchip_dt_free_map(struct pinctrl_dev *pctldev,
				    struct pinctrl_map *map, unsigned num_maps)
{
	kfree(map);
}

static const struct pinctrl_ops rockchip_pctrl_ops = {
	.get_groups_count	= rockchip_get_groups_count,
	.get_group_name		= rockchip_get_group_name,
	.get_group_pins		= rockchip_get_group_pins,
	.dt_node_to_map		= rockchip_dt_node_to_map,
	.dt_free_map		= rockchip_dt_free_map,
};

/*
 * Hardware access
 */

static struct rockchip_mux_recalced_data rv1108_mux_recalced_data[] = {
	{
		.num = 1,
		.pin = 0,
		.reg = 0x418,
		.bit = 0,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 1,
		.reg = 0x418,
		.bit = 2,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 2,
		.reg = 0x418,
		.bit = 4,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 3,
		.reg = 0x418,
		.bit = 6,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 4,
		.reg = 0x418,
		.bit = 8,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 5,
		.reg = 0x418,
		.bit = 10,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 6,
		.reg = 0x418,
		.bit = 12,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 7,
		.reg = 0x418,
		.bit = 14,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 8,
		.reg = 0x41c,
		.bit = 0,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 9,
		.reg = 0x41c,
		.bit = 2,
		.mask = 0x3
	},
};

static struct rockchip_mux_recalced_data rv1126_mux_recalced_data[] = {
	{
		.num = 0,
		.pin = 20,
		.reg = 0x10000,
		.bit = 0,
		.mask = 0xf
	},
	{
		.num = 0,
		.pin = 21,
		.reg = 0x10000,
		.bit = 4,
		.mask = 0xf
	},
	{
		.num = 0,
		.pin = 22,
		.reg = 0x10000,
		.bit = 8,
		.mask = 0xf
	},
	{
		.num = 0,
		.pin = 23,
		.reg = 0x10000,
		.bit = 12,
		.mask = 0xf
	},
};

static  struct rockchip_mux_recalced_data rk3128_mux_recalced_data[] = {
	{
		.num = 2,
		.pin = 20,
		.reg = 0xe8,
		.bit = 0,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 21,
		.reg = 0xe8,
		.bit = 4,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 22,
		.reg = 0xe8,
		.bit = 8,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 23,
		.reg = 0xe8,
		.bit = 12,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 24,
		.reg = 0xd4,
		.bit = 12,
		.mask = 0x7
	},
};

static struct rockchip_mux_recalced_data rk3308_mux_recalced_data[] = {
	{
		.num = 1,
		.pin = 14,
		.reg = 0x28,
		.bit = 12,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 15,
		.reg = 0x2c,
		.bit = 0,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 18,
		.reg = 0x30,
		.bit = 4,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 19,
		.reg = 0x30,
		.bit = 8,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 20,
		.reg = 0x30,
		.bit = 12,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 21,
		.reg = 0x34,
		.bit = 0,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 22,
		.reg = 0x34,
		.bit = 4,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 23,
		.reg = 0x34,
		.bit = 8,
		.mask = 0xf
	}, {
		.num = 3,
		.pin = 12,
		.reg = 0x68,
		.bit = 8,
		.mask = 0xf
	}, {
		.num = 3,
		.pin = 13,
		.reg = 0x68,
		.bit = 12,
		.mask = 0xf
	},
};

static struct rockchip_mux_recalced_data rk3308b_mux_recalced_data[] = {
	{
		.num = 1,
		.pin = 14,
		.reg = 0x28,
		.bit = 12,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 15,
		.reg = 0x2c,
		.bit = 0,
		.mask = 0x3
	}, {
		.num = 1,
		.pin = 18,
		.reg = 0x30,
		.bit = 4,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 19,
		.reg = 0x30,
		.bit = 8,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 20,
		.reg = 0x30,
		.bit = 12,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 21,
		.reg = 0x34,
		.bit = 0,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 22,
		.reg = 0x34,
		.bit = 4,
		.mask = 0xf
	}, {
		.num = 1,
		.pin = 23,
		.reg = 0x34,
		.bit = 8,
		.mask = 0xf
	}, {
		.num = 3,
		.pin = 12,
		.reg = 0x68,
		.bit = 8,
		.mask = 0xf
	}, {
		.num = 3,
		.pin = 13,
		.reg = 0x68,
		.bit = 12,
		.mask = 0xf
	}, {
		.num = 2,
		.pin = 2,
		.reg = 0x608,
		.bit = 0,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 3,
		.reg = 0x608,
		.bit = 4,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 16,
		.reg = 0x610,
		.bit = 8,
		.mask = 0x7
	}, {
		.num = 3,
		.pin = 10,
		.reg = 0x610,
		.bit = 0,
		.mask = 0x7
	}, {
		.num = 3,
		.pin = 11,
		.reg = 0x610,
		.bit = 4,
		.mask = 0x7
	},
};

static struct rockchip_mux_recalced_data rk3328_mux_recalced_data[] = {
	{
		.num = 2,
		.pin = 8,
		.reg = 0x24,
		.bit = 0,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 9,
		.reg = 0x24,
		.bit = 2,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 10,
		.reg = 0x24,
		.bit = 4,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 11,
		.reg = 0x24,
		.bit = 6,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 12,
		.reg = 0x24,
		.bit = 8,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 13,
		.reg = 0x24,
		.bit = 10,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 14,
		.reg = 0x24,
		.bit = 12,
		.mask = 0x3
	}, {
		.num = 2,
		.pin = 15,
		.reg = 0x28,
		.bit = 0,
		.mask = 0x7
	}, {
		.num = 2,
		.pin = 23,
		.reg = 0x30,
		.bit = 14,
		.mask = 0x3
	},
};

static struct rockchip_mux_route_data rv1126_mux_route_data[] = {
	MR_TOPGRF(RK_GPIO3, RK_PD2, RK_FUNC_1, 0x10260, RK_GENMASK_VAL(0, 0, 0)), /* I2S0_MCLK_M0 */
	MR_TOPGRF(RK_GPIO3, RK_PB0, RK_FUNC_3, 0x10260, RK_GENMASK_VAL(0, 0, 1)), /* I2S0_MCLK_M1 */

	MR_TOPGRF(RK_GPIO0, RK_PD4, RK_FUNC_4, 0x10260, RK_GENMASK_VAL(3, 2, 0)), /* I2S1_MCLK_M0 */
	MR_TOPGRF(RK_GPIO1, RK_PD5, RK_FUNC_2, 0x10260, RK_GENMASK_VAL(3, 2, 1)), /* I2S1_MCLK_M1 */
	MR_TOPGRF(RK_GPIO2, RK_PC7, RK_FUNC_6, 0x10260, RK_GENMASK_VAL(3, 2, 2)), /* I2S1_MCLK_M2 */

	MR_TOPGRF(RK_GPIO1, RK_PD0, RK_FUNC_1, 0x10260, RK_GENMASK_VAL(4, 4, 0)), /* I2S2_MCLK_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PB3, RK_FUNC_2, 0x10260, RK_GENMASK_VAL(4, 4, 1)), /* I2S2_MCLK_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PD4, RK_FUNC_2, 0x10260, RK_GENMASK_VAL(12, 12, 0)), /* PDM_CLK0_M0 */
	MR_TOPGRF(RK_GPIO3, RK_PC0, RK_FUNC_3, 0x10260, RK_GENMASK_VAL(12, 12, 1)), /* PDM_CLK0_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PC6, RK_FUNC_1, 0x10264, RK_GENMASK_VAL(0, 0, 0)), /* CIF_CLKOUT_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD1, RK_FUNC_3, 0x10264, RK_GENMASK_VAL(0, 0, 1)), /* CIF_CLKOUT_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PA4, RK_FUNC_5, 0x10264, RK_GENMASK_VAL(5, 4, 0)), /* I2C3_SCL_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD4, RK_FUNC_7, 0x10264, RK_GENMASK_VAL(5, 4, 1)), /* I2C3_SCL_M1 */
	MR_TOPGRF(RK_GPIO1, RK_PD6, RK_FUNC_3, 0x10264, RK_GENMASK_VAL(5, 4, 2)), /* I2C3_SCL_M2 */

	MR_TOPGRF(RK_GPIO3, RK_PA0, RK_FUNC_7, 0x10264, RK_GENMASK_VAL(6, 6, 0)), /* I2C4_SCL_M0 */
	MR_TOPGRF(RK_GPIO4, RK_PA0, RK_FUNC_4, 0x10264, RK_GENMASK_VAL(6, 6, 1)), /* I2C4_SCL_M1 */

	MR_TOPGRF(RK_GPIO2, RK_PA5, RK_FUNC_7, 0x10264, RK_GENMASK_VAL(9, 8, 0)), /* I2C5_SCL_M0 */
	MR_TOPGRF(RK_GPIO3, RK_PB0, RK_FUNC_5, 0x10264, RK_GENMASK_VAL(9, 8, 1)), /* I2C5_SCL_M1 */
	MR_TOPGRF(RK_GPIO1, RK_PD0, RK_FUNC_4, 0x10264, RK_GENMASK_VAL(9, 8, 2)), /* I2C5_SCL_M2 */

	MR_TOPGRF(RK_GPIO3, RK_PC0, RK_FUNC_5, 0x10264, RK_GENMASK_VAL(11, 10, 0)), /* SPI1_CLK_M0 */
	MR_TOPGRF(RK_GPIO1, RK_PC6, RK_FUNC_3, 0x10264, RK_GENMASK_VAL(11, 10, 1)), /* SPI1_CLK_M1 */
	MR_TOPGRF(RK_GPIO2, RK_PD5, RK_FUNC_6, 0x10264, RK_GENMASK_VAL(11, 10, 2)), /* SPI1_CLK_M2 */

	MR_TOPGRF(RK_GPIO3, RK_PC0, RK_FUNC_2, 0x10264, RK_GENMASK_VAL(12, 12, 0)), /* RGMII_CLK_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PB7, RK_FUNC_2, 0x10264, RK_GENMASK_VAL(12, 12, 1)), /* RGMII_CLK_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PA1, RK_FUNC_3, 0x10264, RK_GENMASK_VAL(13, 13, 0)), /* CAN_TXD_M0 */
	MR_TOPGRF(RK_GPIO3, RK_PA7, RK_FUNC_5, 0x10264, RK_GENMASK_VAL(13, 13, 1)), /* CAN_TXD_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PA4, RK_FUNC_6, 0x10268, RK_GENMASK_VAL(0, 0, 0)), /* PWM8_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD7, RK_FUNC_5, 0x10268, RK_GENMASK_VAL(0, 0, 1)), /* PWM8_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PA5, RK_FUNC_6, 0x10268, RK_GENMASK_VAL(2, 2, 0)), /* PWM9_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD6, RK_FUNC_5, 0x10268, RK_GENMASK_VAL(2, 2, 1)), /* PWM9_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PA6, RK_FUNC_6, 0x10268, RK_GENMASK_VAL(4, 4, 0)), /* PWM10_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD5, RK_FUNC_5, 0x10268, RK_GENMASK_VAL(4, 4, 1)), /* PWM10_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PA7, RK_FUNC_6, 0x10268, RK_GENMASK_VAL(6, 6, 0)), /* PWM11_IR_M0 */
	MR_TOPGRF(RK_GPIO3, RK_PA1, RK_FUNC_5, 0x10268, RK_GENMASK_VAL(6, 6, 1)), /* PWM11_IR_M1 */

	MR_TOPGRF(RK_GPIO1, RK_PA5, RK_FUNC_3, 0x10268, RK_GENMASK_VAL(8, 8, 0)), /* UART2_TX_M0 */
	MR_TOPGRF(RK_GPIO3, RK_PA2, RK_FUNC_1, 0x10268, RK_GENMASK_VAL(8, 8, 1)), /* UART2_TX_M1 */

	MR_TOPGRF(RK_GPIO3, RK_PC6, RK_FUNC_3, 0x10268, RK_GENMASK_VAL(11, 10, 0)), /* UART3_TX_M0 */
	MR_TOPGRF(RK_GPIO1, RK_PA7, RK_FUNC_2, 0x10268, RK_GENMASK_VAL(11, 10, 1)), /* UART3_TX_M1 */
	MR_TOPGRF(RK_GPIO3, RK_PA0, RK_FUNC_4, 0x10268, RK_GENMASK_VAL(11, 10, 2)), /* UART3_TX_M2 */

	MR_TOPGRF(RK_GPIO3, RK_PA4, RK_FUNC_4, 0x10268, RK_GENMASK_VAL(13, 12, 0)), /* UART4_TX_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PA6, RK_FUNC_4, 0x10268, RK_GENMASK_VAL(13, 12, 1)), /* UART4_TX_M1 */
	MR_TOPGRF(RK_GPIO1, RK_PD5, RK_FUNC_3, 0x10268, RK_GENMASK_VAL(13, 12, 2)), /* UART4_TX_M2 */

	MR_TOPGRF(RK_GPIO3, RK_PA6, RK_FUNC_4, 0x10268, RK_GENMASK_VAL(15, 14, 0)), /* UART5_TX_M0 */
	MR_TOPGRF(RK_GPIO2, RK_PB0, RK_FUNC_4, 0x10268, RK_GENMASK_VAL(15, 14, 1)), /* UART5_TX_M1 */
	MR_TOPGRF(RK_GPIO2, RK_PA0, RK_FUNC_3, 0x10268, RK_GENMASK_VAL(15, 14, 2)), /* UART5_TX_M2 */

	MR_PMUGRF(RK_GPIO0, RK_PB6, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(0, 0, 0)), /* PWM0_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PB3, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(0, 0, 1)), /* PWM0_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PB7, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(2, 2, 0)), /* PWM1_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PB2, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(2, 2, 1)), /* PWM1_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PC0, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(4, 4, 0)), /* PWM2_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PB1, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(4, 4, 1)), /* PWM2_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PC1, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(6, 6, 0)), /* PWM3_IR_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PB0, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(6, 6, 1)), /* PWM3_IR_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PC2, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(8, 8, 0)), /* PWM4_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PA7, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(8, 8, 1)), /* PWM4_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PC3, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(10, 10, 0)), /* PWM5_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PA6, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(10, 10, 1)), /* PWM5_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PB2, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(12, 12, 0)), /* PWM6_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PD4, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(12, 12, 1)), /* PWM6_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PB1, RK_FUNC_3, 0x0114, RK_GENMASK_VAL(14, 14, 0)), /* PWM7_IR_M0 */
	MR_PMUGRF(RK_GPIO3, RK_PA0, RK_FUNC_5, 0x0114, RK_GENMASK_VAL(14, 14, 1)), /* PWM7_IR_M1 */

	MR_PMUGRF(RK_GPIO0, RK_PB0, RK_FUNC_1, 0x0118, RK_GENMASK_VAL(1, 0, 0)), /* SPI0_CLK_M0 */
	MR_PMUGRF(RK_GPIO2, RK_PA1, RK_FUNC_1, 0x0118, RK_GENMASK_VAL(1, 0, 1)), /* SPI0_CLK_M1 */
	MR_PMUGRF(RK_GPIO2, RK_PB2, RK_FUNC_6, 0x0118, RK_GENMASK_VAL(1, 0, 2)), /* SPI0_CLK_M2 */

	MR_PMUGRF(RK_GPIO0, RK_PB6, RK_FUNC_2, 0x0118, RK_GENMASK_VAL(2, 2, 0)), /* UART1_TX_M0 */
	MR_PMUGRF(RK_GPIO1, RK_PD0, RK_FUNC_5, 0x0118, RK_GENMASK_VAL(2, 2, 1)), /* UART1_TX_M1 */
};

static void rockchip_get_recalced_mux(struct rockchip_pin_bank *bank, int pin,
				      int *reg, u8 *bit, int *mask)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct rockchip_mux_recalced_data *data;
	int i;

	for (i = 0; i < ctrl->niomux_recalced; i++) {
		data = &ctrl->iomux_recalced[i];
		if (data->num == bank->bank_num &&
		    data->pin == pin)
			break;
	}

	if (i >= ctrl->niomux_recalced)
		return;

	*reg = data->reg;
	*mask = data->mask;
	*bit = data->bit;
}

static struct rockchip_mux_route_data rk1808_mux_route_data[] = {
	{
		/* i2c2m0_sda */
		.bank_num = 3,
		.pin = 12,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 3),
	}, {
		/* i2c2m1_sda */
		.bank_num = 1,
		.pin = 13,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 3) | BIT(3),
	}, {
		/* spi2m0_miso */
		.bank_num = 1,
		.pin = 6,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 4),
	}, {
		/* spi2m1_miso */
		.bank_num = 2,
		.pin = 4,
		.func = 3,
		.route_offset = 0x190,
		.route_val = BIT(16 + 4) | BIT(4),
	}, {
		/* spi1m0_miso */
		.bank_num = 4,
		.pin = 15,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 5),
	}, {
		/* spi1m1_miso */
		.bank_num = 3,
		.pin = 26,
		.func = 3,
		.route_offset = 0x190,
		.route_val = BIT(16 + 5) | BIT(5),
	}, {
		/* uart1_rxm0 */
		.bank_num = 4,
		.pin = 8,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 13),
	}, {
		/* uart1_rxm1 */
		.bank_num = 1,
		.pin = 12,
		.func = 3,
		.route_offset = 0x190,
		.route_val = BIT(16 + 13) | BIT(13),
	}, {
		/* uart2_rxm0 */
		.bank_num = 4,
		.pin = 3,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 14) | BIT(16 + 15),
	}, {
		/* uart2_rxm1 */
		.bank_num = 2,
		.pin = 25,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 14) | BIT(16 + 15) | BIT(14),
	}, {
		/* uart2_rxm2 */
		.bank_num = 3,
		.pin = 4,
		.func = 2,
		.route_offset = 0x190,
		.route_val = BIT(16 + 14) | BIT(16 + 15) | BIT(15),
	},
};

static struct rockchip_mux_route_data px30_mux_route_data[] = {
	{
		/* cif-d2m0 */
		.bank_num = 2,
		.pin = 0,
		.func = 1,
		.route_offset = 0x184,
		.route_val = BIT(16 + 7),
	}, {
		/* cif-d2m1 */
		.bank_num = 3,
		.pin = 3,
		.func = 3,
		.route_offset = 0x184,
		.route_val = BIT(16 + 7) | BIT(7),
	}, {
		/* pdm-m0 */
		.bank_num = 3,
		.pin = 22,
		.func = 2,
		.route_offset = 0x184,
		.route_val = BIT(16 + 8),
	}, {
		/* pdm-m1 */
		.bank_num = 2,
		.pin = 22,
		.func = 1,
		.route_offset = 0x184,
		.route_val = BIT(16 + 8) | BIT(8),
	}, {
		/* uart2-rxm0 */
		.bank_num = 1,
		.pin = 27,
		.func = 2,
		.route_offset = 0x184,
		.route_val = BIT(16 + 10),
	}, {
		/* uart2-rxm1 */
		.bank_num = 2,
		.pin = 14,
		.func = 2,
		.route_offset = 0x184,
		.route_val = BIT(16 + 10) | BIT(10),
	}, {
		/* uart3-rxm0 */
		.bank_num = 0,
		.pin = 17,
		.func = 2,
		.route_offset = 0x184,
		.route_val = BIT(16 + 9),
	}, {
		/* uart3-rxm1 */
		.bank_num = 1,
		.pin = 15,
		.func = 2,
		.route_offset = 0x184,
		.route_val = BIT(16 + 9) | BIT(9),
	},
};

static struct rockchip_mux_route_data rk3128_mux_route_data[] = {
	{
		/* spi-0 */
		.bank_num = 1,
		.pin = 10,
		.func = 1,
		.route_offset = 0x144,
		.route_val = BIT(16 + 3) | BIT(16 + 4),
	}, {
		/* spi-1 */
		.bank_num = 1,
		.pin = 27,
		.func = 3,
		.route_offset = 0x144,
		.route_val = BIT(16 + 3) | BIT(16 + 4) | BIT(3),
	}, {
		/* spi-2 */
		.bank_num = 0,
		.pin = 13,
		.func = 2,
		.route_offset = 0x144,
		.route_val = BIT(16 + 3) | BIT(16 + 4) | BIT(4),
	}, {
		/* i2s-0 */
		.bank_num = 1,
		.pin = 5,
		.func = 1,
		.route_offset = 0x144,
		.route_val = BIT(16 + 5),
	}, {
		/* i2s-1 */
		.bank_num = 0,
		.pin = 14,
		.func = 1,
		.route_offset = 0x144,
		.route_val = BIT(16 + 5) | BIT(5),
	}, {
		/* emmc-0 */
		.bank_num = 1,
		.pin = 22,
		.func = 2,
		.route_offset = 0x144,
		.route_val = BIT(16 + 6),
	}, {
		/* emmc-1 */
		.bank_num = 2,
		.pin = 4,
		.func = 2,
		.route_offset = 0x144,
		.route_val = BIT(16 + 6) | BIT(6),
	},
};

static struct rockchip_mux_route_data rk3228_mux_route_data[] = {
	{
		/* pwm0-0 */
		.bank_num = 0,
		.pin = 26,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16),
	}, {
		/* pwm0-1 */
		.bank_num = 3,
		.pin = 21,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16) | BIT(0),
	}, {
		/* pwm1-0 */
		.bank_num = 0,
		.pin = 27,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 1),
	}, {
		/* pwm1-1 */
		.bank_num = 0,
		.pin = 30,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 1) | BIT(1),
	}, {
		/* pwm2-0 */
		.bank_num = 0,
		.pin = 28,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 2),
	}, {
		/* pwm2-1 */
		.bank_num = 1,
		.pin = 12,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 2) | BIT(2),
	}, {
		/* pwm3-0 */
		.bank_num = 3,
		.pin = 26,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 3),
	}, {
		/* pwm3-1 */
		.bank_num = 1,
		.pin = 11,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 3) | BIT(3),
	}, {
		/* sdio-0_d0 */
		.bank_num = 1,
		.pin = 1,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 4),
	}, {
		/* sdio-1_d0 */
		.bank_num = 3,
		.pin = 2,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 4) | BIT(4),
	}, {
		/* spi-0_rx */
		.bank_num = 0,
		.pin = 13,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 5),
	}, {
		/* spi-1_rx */
		.bank_num = 2,
		.pin = 0,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 5) | BIT(5),
	}, {
		/* emmc-0_cmd */
		.bank_num = 1,
		.pin = 22,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 7),
	}, {
		/* emmc-1_cmd */
		.bank_num = 2,
		.pin = 4,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 7) | BIT(7),
	}, {
		/* uart2-0_rx */
		.bank_num = 1,
		.pin = 19,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 8),
	}, {
		/* uart2-1_rx */
		.bank_num = 1,
		.pin = 10,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 8) | BIT(8),
	}, {
		/* uart1-0_rx */
		.bank_num = 1,
		.pin = 10,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 11),
	}, {
		/* uart1-1_rx */
		.bank_num = 3,
		.pin = 13,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 11) | BIT(11),
	},
};

static struct rockchip_mux_route_data rk3288_mux_route_data[] = {
	{
		/* edphdmi_cecinoutt1 */
		.bank_num = 7,
		.pin = 16,
		.func = 2,
		.route_offset = 0x264,
		.route_val = BIT(16 + 12) | BIT(12),
	}, {
		/* edphdmi_cecinout */
		.bank_num = 7,
		.pin = 23,
		.func = 4,
		.route_offset = 0x264,
		.route_val = BIT(16 + 12),
	},
};

static struct rockchip_mux_route_data rk3308_mux_route_data[] = {
	{
		/* rtc_clk */
		.bank_num = 0,
		.pin = 19,
		.func = 1,
		.route_offset = 0x314,
		.route_val = BIT(16 + 0) | BIT(0),
	}, {
		/* uart2_rxm0 */
		.bank_num = 1,
		.pin = 22,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 2) | BIT(16 + 3),
	}, {
		/* uart2_rxm1 */
		.bank_num = 4,
		.pin = 26,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 2) | BIT(16 + 3) | BIT(2),
	}, {
		/* i2c3_sdam0 */
		.bank_num = 0,
		.pin = 23,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 4),
	}, {
		/* i2c3_sdam1 */
		.bank_num = 3,
		.pin = 12,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 4) | BIT(4),
	}, {
		/* i2s-8ch-1-sclktxm0 */
		.bank_num = 1,
		.pin = 3,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3),
	}, {
		/* i2s-8ch-1-sclkrxm0 */
		.bank_num = 1,
		.pin = 4,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3),
	}, {
		/* i2s-8ch-1-sclktxm1 */
		.bank_num = 1,
		.pin = 13,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3) | BIT(3),
	}, {
		/* i2s-8ch-1-sclkrxm1 */
		.bank_num = 1,
		.pin = 14,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3) | BIT(3),
	}, {
		/* pdm-clkm0 */
		.bank_num = 1,
		.pin = 4,
		.func = 3,
		.route_offset = 0x308,
		.route_val = BIT(16 + 12) | BIT(16 + 13),
	}, {
		/* pdm-clkm1 */
		.bank_num = 1,
		.pin = 14,
		.func = 4,
		.route_offset = 0x308,
		.route_val = BIT(16 + 12) | BIT(16 + 13) | BIT(12),
	}, {
		/* pdm-clkm2 */
		.bank_num = 2,
		.pin = 6,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 12) | BIT(16 + 13) | BIT(13),
	}, {
		/* pdm-clkm-m2 */
		.bank_num = 2,
		.pin = 4,
		.func = 3,
		.route_offset = 0x600,
		.route_val = BIT(16 + 2) | BIT(2),
	},
};

static struct rockchip_mux_route_data rk3308b_mux_route_data[] = {
	{
		/* rtc_clk */
		.bank_num = 0,
		.pin = 19,
		.func = 1,
		.route_offset = 0x314,
		.route_val = BIT(16 + 0) | BIT(0),
	}, {
		/* uart2_rxm0 */
		.bank_num = 1,
		.pin = 22,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 2) | BIT(16 + 3),
	}, {
		/* uart2_rxm1 */
		.bank_num = 4,
		.pin = 26,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 2) | BIT(16 + 3) | BIT(2),
	}, {
		/* i2c3_sdam0 */
		.bank_num = 0,
		.pin = 15,
		.func = 2,
		.route_offset = 0x608,
		.route_val = BIT(16 + 8) | BIT(16 + 9),
	}, {
		/* i2c3_sdam1 */
		.bank_num = 3,
		.pin = 12,
		.func = 2,
		.route_offset = 0x608,
		.route_val = BIT(16 + 8) | BIT(16 + 9) | BIT(8),
	}, {
		/* i2c3_sdam2 */
		.bank_num = 2,
		.pin = 0,
		.func = 3,
		.route_offset = 0x608,
		.route_val = BIT(16 + 8) | BIT(16 + 9) | BIT(9),
	}, {
		/* i2s-8ch-1-sclktxm0 */
		.bank_num = 1,
		.pin = 3,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3),
	}, {
		/* i2s-8ch-1-sclkrxm0 */
		.bank_num = 1,
		.pin = 4,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3),
	}, {
		/* i2s-8ch-1-sclktxm1 */
		.bank_num = 1,
		.pin = 13,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3) | BIT(3),
	}, {
		/* i2s-8ch-1-sclkrxm1 */
		.bank_num = 1,
		.pin = 14,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 3) | BIT(3),
	}, {
		/* pdm-clkm0 */
		.bank_num = 1,
		.pin = 4,
		.func = 3,
		.route_offset = 0x308,
		.route_val =  BIT(16 + 12) | BIT(16 + 13),
	}, {
		/* pdm-clkm1 */
		.bank_num = 1,
		.pin = 14,
		.func = 4,
		.route_offset = 0x308,
		.route_val = BIT(16 + 12) | BIT(16 + 13) | BIT(12),
	}, {
		/* pdm-clkm2 */
		.bank_num = 2,
		.pin = 6,
		.func = 2,
		.route_offset = 0x308,
		.route_val = BIT(16 + 12) | BIT(16 + 13) | BIT(13),
	}, {
		/* pdm-clkm-m2 */
		.bank_num = 2,
		.pin = 4,
		.func = 3,
		.route_offset = 0x600,
		.route_val = BIT(16 + 2) | BIT(2),
	}, {
		/* spi1_miso */
		.bank_num = 3,
		.pin = 10,
		.func = 3,
		.route_offset = 0x314,
		.route_val = BIT(16 + 9),
	}, {
		/* spi1_miso_m1 */
		.bank_num = 2,
		.pin = 4,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 9) | BIT(9),
	}, {
		/* owire_m0 */
		.bank_num = 0,
		.pin = 11,
		.func = 3,
		.route_offset = 0x314,
		.route_val = BIT(16 + 10) | BIT(16 + 11),
	}, {
		/* owire_m1 */
		.bank_num = 1,
		.pin = 22,
		.func = 7,
		.route_offset = 0x314,
		.route_val = BIT(16 + 10) | BIT(16 + 11) | BIT(10),
	}, {
		/* owire_m2 */
		.bank_num = 2,
		.pin = 2,
		.func = 5,
		.route_offset = 0x314,
		.route_val = BIT(16 + 10) | BIT(16 + 11) | BIT(11),
	}, {
		/* can_rxd_m0 */
		.bank_num = 0,
		.pin = 11,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 12) | BIT(16 + 13),
	}, {
		/* can_rxd_m1 */
		.bank_num = 1,
		.pin = 22,
		.func = 5,
		.route_offset = 0x314,
		.route_val = BIT(16 + 12) | BIT(16 + 13) | BIT(12),
	}, {
		/* can_rxd_m2 */
		.bank_num = 2,
		.pin = 2,
		.func = 4,
		.route_offset = 0x314,
		.route_val = BIT(16 + 12) | BIT(16 + 13) | BIT(13),
	}, {
		/* mac_rxd0_m0 */
		.bank_num = 1,
		.pin = 20,
		.func = 3,
		.route_offset = 0x314,
		.route_val = BIT(16 + 14),
	}, {
		/* mac_rxd0_m1 */
		.bank_num = 4,
		.pin = 2,
		.func = 2,
		.route_offset = 0x314,
		.route_val = BIT(16 + 14) | BIT(14),
	}, {
		/* uart3_rx */
		.bank_num = 3,
		.pin = 12,
		.func = 4,
		.route_offset = 0x314,
		.route_val = BIT(16 + 15),
	}, {
		/* uart3_rx_m1 */
		.bank_num = 0,
		.pin = 17,
		.func = 3,
		.route_offset = 0x314,
		.route_val = BIT(16 + 15) | BIT(15),
	},
};

static struct rockchip_mux_route_data rk3328_mux_route_data[] = {
	{
		/* uart2dbg_rxm0 */
		.bank_num = 1,
		.pin = 1,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16) | BIT(16 + 1),
	}, {
		/* uart2dbg_rxm1 */
		.bank_num = 2,
		.pin = 1,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16) | BIT(16 + 1) | BIT(0),
	}, {
		/* gmac-m1_rxd0 */
		.bank_num = 1,
		.pin = 11,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 2) | BIT(2),
	}, {
		/* gmac-m1-optimized_rxd3 */
		.bank_num = 1,
		.pin = 14,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 10) | BIT(10),
	}, {
		/* pdm_sdi0m0 */
		.bank_num = 2,
		.pin = 19,
		.func = 2,
		.route_offset = 0x50,
		.route_val = BIT(16 + 3),
	}, {
		/* pdm_sdi0m1 */
		.bank_num = 1,
		.pin = 23,
		.func = 3,
		.route_offset = 0x50,
		.route_val =  BIT(16 + 3) | BIT(3),
	}, {
		/* spi_rxdm2 */
		.bank_num = 3,
		.pin = 2,
		.func = 4,
		.route_offset = 0x50,
		.route_val =  BIT(16 + 4) | BIT(16 + 5) | BIT(5),
	}, {
		/* i2s2_sdim0 */
		.bank_num = 1,
		.pin = 24,
		.func = 1,
		.route_offset = 0x50,
		.route_val = BIT(16 + 6),
	}, {
		/* i2s2_sdim1 */
		.bank_num = 3,
		.pin = 2,
		.func = 6,
		.route_offset = 0x50,
		.route_val =  BIT(16 + 6) | BIT(6),
	}, {
		/* card_iom1 */
		.bank_num = 2,
		.pin = 22,
		.func = 3,
		.route_offset = 0x50,
		.route_val =  BIT(16 + 7) | BIT(7),
	}, {
		/* tsp_d5m1 */
		.bank_num = 2,
		.pin = 16,
		.func = 3,
		.route_offset = 0x50,
		.route_val =  BIT(16 + 8) | BIT(8),
	}, {
		/* cif_data5m1 */
		.bank_num = 2,
		.pin = 16,
		.func = 4,
		.route_offset = 0x50,
		.route_val =  BIT(16 + 9) | BIT(9),
	},
};

static struct rockchip_mux_route_data rk3399_mux_route_data[] = {
	{
		/* uart2dbga_rx */
		.bank_num = 4,
		.pin = 8,
		.func = 2,
		.route_offset = 0xe21c,
		.route_val = BIT(16 + 10) | BIT(16 + 11),
	}, {
		/* uart2dbgb_rx */
		.bank_num = 4,
		.pin = 16,
		.func = 2,
		.route_offset = 0xe21c,
		.route_val = BIT(16 + 10) | BIT(16 + 11) | BIT(10),
	}, {
		/* uart2dbgc_rx */
		.bank_num = 4,
		.pin = 19,
		.func = 1,
		.route_offset = 0xe21c,
		.route_val = BIT(16 + 10) | BIT(16 + 11) | BIT(11),
	}, {
		/* pcie_clkreqn */
		.bank_num = 2,
		.pin = 26,
		.func = 2,
		.route_offset = 0xe21c,
		.route_val = BIT(16 + 14),
	}, {
		/* pcie_clkreqnb */
		.bank_num = 4,
		.pin = 24,
		.func = 1,
		.route_offset = 0xe21c,
		.route_val = BIT(16 + 14) | BIT(14),
	},
};

static struct rockchip_mux_route_data rk3568_mux_route_data[] = {
	MR_PMUGRF(RK_GPIO0, RK_PB7, RK_FUNC_1, 0x0110, RK_GENMASK_VAL(1, 0, 0)), /* PWM0 IO mux selection M0 */
	MR_PMUGRF(RK_GPIO0, RK_PC7, RK_FUNC_2, 0x0110, RK_GENMASK_VAL(1, 0, 1)), /* PWM0 IO mux selection M1 */
	MR_PMUGRF(RK_GPIO0, RK_PC0, RK_FUNC_1, 0x0110, RK_GENMASK_VAL(3, 2, 0)), /* PWM1 IO mux selection M0 */
	MR_PMUGRF(RK_GPIO0, RK_PB5, RK_FUNC_4, 0x0110, RK_GENMASK_VAL(3, 2, 1)), /* PWM1 IO mux selection M1 */
	MR_PMUGRF(RK_GPIO0, RK_PC1, RK_FUNC_1, 0x0110, RK_GENMASK_VAL(5, 4, 0)), /* PWM2 IO mux selection M0 */
	MR_PMUGRF(RK_GPIO0, RK_PB6, RK_FUNC_4, 0x0110, RK_GENMASK_VAL(5, 4, 1)), /* PWM2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO0, RK_PB3, RK_FUNC_2, 0x0300, RK_GENMASK_VAL(0, 0, 0)), /* CAN0 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PA1, RK_FUNC_4, 0x0300, RK_GENMASK_VAL(0, 0, 1)), /* CAN0 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PA1, RK_FUNC_3, 0x0300, RK_GENMASK_VAL(2, 2, 0)), /* CAN1 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC3, RK_FUNC_3, 0x0300, RK_GENMASK_VAL(2, 2, 1)), /* CAN1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PB5, RK_FUNC_3, 0x0300, RK_GENMASK_VAL(4, 4, 0)), /* CAN2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PB2, RK_FUNC_4, 0x0300, RK_GENMASK_VAL(4, 4, 1)), /* CAN2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PC4, RK_FUNC_1, 0x0300, RK_GENMASK_VAL(6, 6, 0)), /* EDPDP_HPDIN IO mux selection M0 */
	MR_TOPGRF(RK_GPIO0, RK_PC2, RK_FUNC_2, 0x0300, RK_GENMASK_VAL(6, 6, 1)), /* EDPDP_HPDIN IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB1, RK_FUNC_3, 0x0300, RK_GENMASK_VAL(8, 8, 0)), /* GMAC1 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PA7, RK_FUNC_3, 0x0300, RK_GENMASK_VAL(8, 8, 1)), /* GMAC1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PB7, RK_FUNC_3, 0x0300, RK_GENMASK_VAL(8, 8, 1)), /* GMAC1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PD1, RK_FUNC_1, 0x0300, RK_GENMASK_VAL(10, 10, 0)), /* HDMITX IO mux selection M0 */
	MR_TOPGRF(RK_GPIO0, RK_PC7, RK_FUNC_1, 0x0300, RK_GENMASK_VAL(10, 10, 1)), /* HDMITX IO mux selection M1 */
	MR_TOPGRF(RK_GPIO0, RK_PB6, RK_FUNC_1, 0x0300, RK_GENMASK_VAL(14, 14, 0)), /* I2C2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PB4, RK_FUNC_1, 0x0300, RK_GENMASK_VAL(14, 14, 1)), /* I2C2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PA0, RK_FUNC_1, 0x0304, RK_GENMASK_VAL(0, 0, 0)), /* I2C3 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PB6, RK_FUNC_4, 0x0304, RK_GENMASK_VAL(0, 0, 1)), /* I2C3 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PB2, RK_FUNC_1, 0x0304, RK_GENMASK_VAL(2, 2, 0)), /* I2C4 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PB1, RK_FUNC_2, 0x0304, RK_GENMASK_VAL(2, 2, 1)), /* I2C4 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB4, RK_FUNC_4, 0x0304, RK_GENMASK_VAL(4, 4, 0)), /* I2C5 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PD0, RK_FUNC_2, 0x0304, RK_GENMASK_VAL(4, 4, 1)), /* I2C5 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB1, RK_FUNC_5, 0x0304, RK_GENMASK_VAL(14, 14, 0)), /* PWM8 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO1, RK_PD5, RK_FUNC_4, 0x0304, RK_GENMASK_VAL(14, 14, 1)), /* PWM8 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB2, RK_FUNC_5, 0x0308, RK_GENMASK_VAL(0, 0, 0)), /* PWM9 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO1, RK_PD6, RK_FUNC_4, 0x0308, RK_GENMASK_VAL(0, 0, 1)), /* PWM9 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB5, RK_FUNC_5, 0x0308, RK_GENMASK_VAL(2, 2, 0)), /* PWM10 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PA1, RK_FUNC_2, 0x0308, RK_GENMASK_VAL(2, 2, 1)), /* PWM10 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB6, RK_FUNC_5, 0x0308, RK_GENMASK_VAL(4, 4, 0)), /* PWM11 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC0, RK_FUNC_3, 0x0308, RK_GENMASK_VAL(4, 4, 1)), /* PWM11 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PB7, RK_FUNC_2, 0x0308, RK_GENMASK_VAL(6, 6, 0)), /* PWM12 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC5, RK_FUNC_1, 0x0308, RK_GENMASK_VAL(6, 6, 1)), /* PWM12 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PC0, RK_FUNC_2, 0x0308, RK_GENMASK_VAL(8, 8, 0)), /* PWM13 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC6, RK_FUNC_1, 0x0308, RK_GENMASK_VAL(8, 8, 1)), /* PWM13 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PC4, RK_FUNC_1, 0x0308, RK_GENMASK_VAL(10, 10, 0)), /* PWM14 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC2, RK_FUNC_1, 0x0308, RK_GENMASK_VAL(10, 10, 1)), /* PWM14 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PC5, RK_FUNC_1, 0x0308, RK_GENMASK_VAL(12, 12, 0)), /* PWM15 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC3, RK_FUNC_1, 0x0308, RK_GENMASK_VAL(12, 12, 1)), /* PWM15 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PD2, RK_FUNC_3, 0x0308, RK_GENMASK_VAL(14, 14, 0)), /* SDMMC2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PA5, RK_FUNC_5, 0x0308, RK_GENMASK_VAL(14, 14, 1)), /* SDMMC2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO0, RK_PB5, RK_FUNC_2, 0x030c, RK_GENMASK_VAL(0, 0, 0)), /* SPI0 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD3, RK_FUNC_3, 0x030c, RK_GENMASK_VAL(0, 0, 1)), /* SPI0 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PB5, RK_FUNC_3, 0x030c, RK_GENMASK_VAL(2, 2, 0)), /* SPI1 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PC3, RK_FUNC_3, 0x030c, RK_GENMASK_VAL(2, 2, 1)), /* SPI1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PC1, RK_FUNC_4, 0x030c, RK_GENMASK_VAL(4, 4, 0)), /* SPI2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PA0, RK_FUNC_3, 0x030c, RK_GENMASK_VAL(4, 4, 1)), /* SPI2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PB3, RK_FUNC_4, 0x030c, RK_GENMASK_VAL(6, 6, 0)), /* SPI3 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC2, RK_FUNC_2, 0x030c, RK_GENMASK_VAL(6, 6, 1)), /* SPI3 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PB4, RK_FUNC_2, 0x030c, RK_GENMASK_VAL(8, 8, 0)), /* UART1 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO0, RK_PD1, RK_FUNC_1, 0x030c, RK_GENMASK_VAL(8, 8, 1)), /* UART1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO0, RK_PD1, RK_FUNC_1, 0x030c, RK_GENMASK_VAL(10, 10, 0)), /* UART2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO1, RK_PD5, RK_FUNC_2, 0x030c, RK_GENMASK_VAL(10, 10, 1)), /* UART2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PA1, RK_FUNC_2, 0x030c, RK_GENMASK_VAL(12, 12, 0)), /* UART3 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PB7, RK_FUNC_4, 0x030c, RK_GENMASK_VAL(12, 12, 1)), /* UART3 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PA6, RK_FUNC_2, 0x030c, RK_GENMASK_VAL(14, 14, 0)), /* UART4 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PB2, RK_FUNC_4, 0x030c, RK_GENMASK_VAL(14, 14, 1)), /* UART4 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PA2, RK_FUNC_3, 0x0310, RK_GENMASK_VAL(0, 0, 0)), /* UART5 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PC2, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(0, 0, 1)), /* UART5 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PA4, RK_FUNC_3, 0x0310, RK_GENMASK_VAL(2, 2, 0)), /* UART6 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO1, RK_PD5, RK_FUNC_3, 0x0310, RK_GENMASK_VAL(2, 2, 1)), /* UART6 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PA6, RK_FUNC_3, 0x0310, RK_GENMASK_VAL(5, 4, 0)), /* UART7 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PC4, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(5, 4, 1)), /* UART7 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PA2, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(5, 4, 2)), /* UART7 IO mux selection M2 */
	MR_TOPGRF(RK_GPIO2, RK_PC5, RK_FUNC_3, 0x0310, RK_GENMASK_VAL(6, 6, 0)), /* UART8 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD7, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(6, 6, 1)), /* UART8 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PB0, RK_FUNC_3, 0x0310, RK_GENMASK_VAL(9, 8, 0)), /* UART9 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC5, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(9, 8, 1)), /* UART9 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PA4, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(9, 8, 2)), /* UART9 IO mux selection M2 */
	MR_TOPGRF(RK_GPIO1, RK_PA2, RK_FUNC_1, 0x0310, RK_GENMASK_VAL(11, 10, 0)), /* I2S1 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PC6, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(11, 10, 1)), /* I2S1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO2, RK_PD0, RK_FUNC_5, 0x0310, RK_GENMASK_VAL(11, 10, 2)), /* I2S1 IO mux selection M2 */
	MR_TOPGRF(RK_GPIO2, RK_PC1, RK_FUNC_1, 0x0310, RK_GENMASK_VAL(12, 12, 0)), /* I2S2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PB6, RK_FUNC_5, 0x0310, RK_GENMASK_VAL(12, 12, 1)), /* I2S2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PA2, RK_FUNC_4, 0x0310, RK_GENMASK_VAL(14, 14, 0)), /* I2S3 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO4, RK_PC2, RK_FUNC_5, 0x0310, RK_GENMASK_VAL(14, 14, 1)), /* I2S3 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PA4, RK_FUNC_3, 0x0314, RK_GENMASK_VAL(1, 0, 0)), /* PDM IO mux selection M0 */
	MR_TOPGRF(RK_GPIO1, RK_PA6, RK_FUNC_3, 0x0314, RK_GENMASK_VAL(1, 0, 0)), /* PDM IO mux selection M0 */
	MR_TOPGRF(RK_GPIO3, RK_PD6, RK_FUNC_5, 0x0314, RK_GENMASK_VAL(1, 0, 1)), /* PDM IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PA0, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(1, 0, 1)), /* PDM IO mux selection M1 */
	MR_TOPGRF(RK_GPIO3, RK_PC4, RK_FUNC_5, 0x0314, RK_GENMASK_VAL(1, 0, 2)), /* PDM IO mux selection M2 */
	MR_TOPGRF(RK_GPIO0, RK_PA5, RK_FUNC_3, 0x0314, RK_GENMASK_VAL(3, 2, 0)), /* PCIE20 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD0, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(3, 2, 1)), /* PCIE20 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PB0, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(3, 2, 2)), /* PCIE20 IO mux selection M2 */
	MR_TOPGRF(RK_GPIO0, RK_PA4, RK_FUNC_3, 0x0314, RK_GENMASK_VAL(5, 4, 0)), /* PCIE30X1 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD2, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(5, 4, 1)), /* PCIE30X1 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO1, RK_PA5, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(5, 4, 2)), /* PCIE30X1 IO mux selection M2 */
	MR_TOPGRF(RK_GPIO0, RK_PA6, RK_FUNC_2, 0x0314, RK_GENMASK_VAL(7, 6, 0)), /* PCIE30X2 IO mux selection M0 */
	MR_TOPGRF(RK_GPIO2, RK_PD4, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(7, 6, 1)), /* PCIE30X2 IO mux selection M1 */
	MR_TOPGRF(RK_GPIO4, RK_PC2, RK_FUNC_4, 0x0314, RK_GENMASK_VAL(7, 6, 2)), /* PCIE30X2 IO mux selection M2 */
};

static enum rockchip_pin_route_type
rockchip_get_mux_route(struct rockchip_pin_bank *bank, int pin,
		       int mux, u32 *reg, u32 *value)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct rockchip_mux_route_data *data;
	int i;

	for (i = 0; i < ctrl->niomux_routes; i++) {
		data = &ctrl->iomux_routes[i];
		if ((data->bank_num == bank->bank_num) &&
		    (data->pin == pin) && (data->func == mux))
			break;
	}

	if (i >= ctrl->niomux_routes)
		return ROUTE_TYPE_INVALID;

	*reg = data->route_offset;
	*value = data->route_val;

	return data->route_type;
}

static int rockchip_verify_mux(struct rockchip_pin_bank *bank,
			       int pin, int mux)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int iomux_num = (pin / 8);

	if (iomux_num > 3)
		return -EINVAL;

	if (bank->iomux[iomux_num].type & IOMUX_UNROUTED) {
		dev_err(info->dev, "pin %d is unrouted\n", pin);
		return -EINVAL;
	}

	if (bank->iomux[iomux_num].type & IOMUX_GPIO_ONLY) {
		if (mux != RK_FUNC_GPIO) {
			dev_err(info->dev,
				"pin %d only supports a gpio mux\n", pin);
			return -ENOTSUPP;
		}
	}

	return 0;
}

static int rockchip_get_mux(struct rockchip_pin_bank *bank, int pin)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int iomux_num = (pin / 8);
	struct regmap *regmap;
	unsigned int val;
	int reg, ret, mask, mux_type;
	u8 bit;

	if (iomux_num > 3)
		return -EINVAL;

	if (bank->iomux[iomux_num].type & IOMUX_UNROUTED) {
		dev_err(info->dev, "pin %d is unrouted\n", pin);
		return -EINVAL;
	}

	if (bank->iomux[iomux_num].type & IOMUX_GPIO_ONLY)
		return 0;

	if (bank->iomux[iomux_num].type & IOMUX_SOURCE_PMU)
		regmap = info->regmap_pmu;
	else if (bank->iomux[iomux_num].type & IOMUX_L_SOURCE_PMU)
		regmap = (pin % 8 < 4) ? info->regmap_pmu : info->regmap_base;
	else
		regmap = info->regmap_base;

	/* get basic quadrupel of mux registers and the correct reg inside */
	mux_type = bank->iomux[iomux_num].type;
	reg = bank->iomux[iomux_num].offset;
	if (mux_type & IOMUX_WIDTH_4BIT) {
		if ((pin % 8) >= 4)
			reg += 0x4;
		bit = (pin % 4) * 4;
		mask = 0xf;
	} else if (mux_type & IOMUX_WIDTH_3BIT) {
		if ((pin % 8) >= 5)
			reg += 0x4;
		bit = (pin % 8 % 5) * 3;
		mask = 0x7;
	} else {
		bit = (pin % 8) * 2;
		mask = 0x3;
	}
	if (bank->recalced_mask & BIT(pin))
		rockchip_get_recalced_mux(bank, pin, &reg, &bit, &mask);

	ret = regmap_read(regmap, reg, &val);

	return ret ? ret : ((val >> bit) & mask);
}

/*
 * Set a new mux function for a pin.
 *
 * The register is divided into the upper and lower 16 bit. When changing
 * a value, the previous register value is not read and changed. Instead
 * it seems the changed bits are marked in the upper 16 bit, while the
 * changed value gets set in the same offset in the lower 16 bit.
 * All pin settings seem to be 2 bit wide in both the upper and lower
 * parts.
 * @bank: pin bank to change
 * @pin: pin to change
 * @mux: new mux function to set
 */
static int rockchip_set_mux(struct rockchip_pin_bank *bank, int pin, int mux)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int iomux_num = (pin / 8);
	struct regmap *regmap;
	int reg, ret, mask, mux_type;
	u8 bit;
	u32 data, rmask;

	ret = rockchip_verify_mux(bank, pin, mux);
	if (ret < 0)
		return ret;

	if (bank->iomux[iomux_num].type & IOMUX_GPIO_ONLY)
		return 0;

	dev_dbg(info->dev, "setting mux of GPIO%d-%d to %d\n",
						bank->bank_num, pin, mux);

	if (bank->iomux[iomux_num].type & IOMUX_SOURCE_PMU)
		regmap = info->regmap_pmu;
	else if (bank->iomux[iomux_num].type & IOMUX_L_SOURCE_PMU)
		regmap = (pin % 8 < 4) ? info->regmap_pmu : info->regmap_base;
	else
		regmap = info->regmap_base;

	/* get basic quadrupel of mux registers and the correct reg inside */
	mux_type = bank->iomux[iomux_num].type;
	reg = bank->iomux[iomux_num].offset;
	if (mux_type & IOMUX_WIDTH_4BIT) {
		if ((pin % 8) >= 4)
			reg += 0x4;
		bit = (pin % 4) * 4;
		mask = 0xf;
	} else if (mux_type & IOMUX_WIDTH_3BIT) {
		if ((pin % 8) >= 5)
			reg += 0x4;
		bit = (pin % 8 % 5) * 3;
		mask = 0x7;
	} else {
		bit = (pin % 8) * 2;
		mask = 0x3;
	}

	if (bank->recalced_mask & BIT(pin))
		rockchip_get_recalced_mux(bank, pin, &reg, &bit, &mask);

	if (mux > mask)
		return -EINVAL;

	if (bank->route_mask & BIT(pin)) {
		u32 route_reg = 0, route_val = 0;

		ret = rockchip_get_mux_route(bank, pin, mux,
					     &route_reg, &route_val);
		switch (ret) {
		case ROUTE_TYPE_DEFAULT:
			regmap_write(regmap, route_reg, route_val);
			break;
		case ROUTE_TYPE_TOPGRF:
			regmap_write(info->regmap_base, route_reg, route_val);
			break;
		case ROUTE_TYPE_PMUGRF:
			regmap_write(info->regmap_pmu, route_reg, route_val);
			break;
		case ROUTE_TYPE_INVALID: /* Fall through */
		default:
			break;
		}
	}

	if (mux_type & IOMUX_WRITABLE_32BIT) {
		ret = regmap_read(regmap, reg, &data);
		if (ret)
			return ret;

		data &= ~(mask << bit);
		data |= (mux & mask) << bit;
		ret = regmap_write(regmap, reg, data);
	} else {
		data = (mask << (bit + 16));
		rmask = data | (data >> 16);
		data |= (mux & mask) << bit;
		ret = regmap_update_bits(regmap, reg, rmask, data);
	}

	return ret;
}

#define PX30_PULL_PMU_OFFSET		0x10
#define PX30_PULL_GRF_OFFSET		0x60
#define PX30_PULL_BITS_PER_PIN		2
#define PX30_PULL_PINS_PER_REG		8
#define PX30_PULL_BANK_STRIDE		16

static void px30_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				       int pin_num, struct regmap **regmap,
				       int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 32 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = PX30_PULL_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = PX30_PULL_GRF_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * PX30_PULL_BANK_STRIDE;
	}

	*reg += ((pin_num / PX30_PULL_PINS_PER_REG) * 4);
	*bit = (pin_num % PX30_PULL_PINS_PER_REG);
	*bit *= PX30_PULL_BITS_PER_PIN;
}

#define PX30_DRV_PMU_OFFSET		0x20
#define PX30_DRV_GRF_OFFSET		0xf0
#define PX30_DRV_BITS_PER_PIN		2
#define PX30_DRV_PINS_PER_REG		8
#define PX30_DRV_BANK_STRIDE		16

static void px30_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
				      int pin_num, struct regmap **regmap,
				      int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 32 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = PX30_DRV_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = PX30_DRV_GRF_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * PX30_DRV_BANK_STRIDE;
	}

	*reg += ((pin_num / PX30_DRV_PINS_PER_REG) * 4);
	*bit = (pin_num % PX30_DRV_PINS_PER_REG);
	*bit *= PX30_DRV_BITS_PER_PIN;
}

#define PX30_SCHMITT_PMU_OFFSET			0x38
#define PX30_SCHMITT_GRF_OFFSET			0xc0
#define PX30_SCHMITT_PINS_PER_PMU_REG		16
#define PX30_SCHMITT_BANK_STRIDE		16
#define PX30_SCHMITT_PINS_PER_GRF_REG		8

static int px30_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num,
					 struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int pins_per_reg;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = PX30_SCHMITT_PMU_OFFSET;
		pins_per_reg = PX30_SCHMITT_PINS_PER_PMU_REG;
	} else {
		*regmap = info->regmap_base;
		*reg = PX30_SCHMITT_GRF_OFFSET;
		pins_per_reg = PX30_SCHMITT_PINS_PER_GRF_REG;
		*reg += (bank->bank_num  - 1) * PX30_SCHMITT_BANK_STRIDE;
	}

	*reg += ((pin_num / pins_per_reg) * 4);
	*bit = pin_num % pins_per_reg;

	return 0;
}

#define RV1108_PULL_PMU_OFFSET		0x10
#define RV1108_PULL_OFFSET		0x110
#define RV1108_PULL_PINS_PER_REG	8
#define RV1108_PULL_BITS_PER_PIN	2
#define RV1108_PULL_BANK_STRIDE		16

static void rv1108_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 24 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RV1108_PULL_PMU_OFFSET;
	} else {
		*reg = RV1108_PULL_OFFSET;
		*regmap = info->regmap_base;
		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * RV1108_PULL_BANK_STRIDE;
	}

	*reg += ((pin_num / RV1108_PULL_PINS_PER_REG) * 4);
	*bit = (pin_num % RV1108_PULL_PINS_PER_REG);
	*bit *= RV1108_PULL_BITS_PER_PIN;
}

#define RV1108_DRV_PMU_OFFSET		0x20
#define RV1108_DRV_GRF_OFFSET		0x210
#define RV1108_DRV_BITS_PER_PIN		2
#define RV1108_DRV_PINS_PER_REG		8
#define RV1108_DRV_BANK_STRIDE		16

static void rv1108_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
					int pin_num, struct regmap **regmap,
					int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 24 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RV1108_DRV_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RV1108_DRV_GRF_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * RV1108_DRV_BANK_STRIDE;
	}

	*reg += ((pin_num / RV1108_DRV_PINS_PER_REG) * 4);
	*bit = pin_num % RV1108_DRV_PINS_PER_REG;
	*bit *= RV1108_DRV_BITS_PER_PIN;
}

#define RV1108_SCHMITT_PMU_OFFSET		0x30
#define RV1108_SCHMITT_GRF_OFFSET		0x388
#define RV1108_SCHMITT_BANK_STRIDE		8
#define RV1108_SCHMITT_PINS_PER_GRF_REG		16
#define RV1108_SCHMITT_PINS_PER_PMU_REG		8

static int rv1108_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int pins_per_reg;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RV1108_SCHMITT_PMU_OFFSET;
		pins_per_reg = RV1108_SCHMITT_PINS_PER_PMU_REG;
	} else {
		*regmap = info->regmap_base;
		*reg = RV1108_SCHMITT_GRF_OFFSET;
		pins_per_reg = RV1108_SCHMITT_PINS_PER_GRF_REG;
		*reg += (bank->bank_num  - 1) * RV1108_SCHMITT_BANK_STRIDE;
	}
	*reg += ((pin_num / pins_per_reg) * 4);
	*bit = pin_num % pins_per_reg;

	return 0;
}

#define RV1126_PULL_PMU_OFFSET		0x40
#define RV1126_PULL_GRF_GPIO1A0_OFFSET		0x10108
#define RV1126_PULL_PINS_PER_REG	8
#define RV1126_PULL_BITS_PER_PIN	2
#define RV1126_PULL_BANK_STRIDE		16
#define RV1126_GPIO_C4_D7(p)	(p >= 20 && p <= 31) /* GPIO0_C4 ~ GPIO0_D7 */

static void rv1126_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 24 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		if (RV1126_GPIO_C4_D7(pin_num)) {
			*regmap = info->regmap_base;
			*reg = RV1126_PULL_GRF_GPIO1A0_OFFSET;
			*reg -= (((31 - pin_num) / RV1126_PULL_PINS_PER_REG + 1) * 4);
			*bit = pin_num % RV1126_PULL_PINS_PER_REG;
			*bit *= RV1126_PULL_BITS_PER_PIN;
			return;
		}
		*regmap = info->regmap_pmu;
		*reg = RV1126_PULL_PMU_OFFSET;
	} else {
		*reg = RV1126_PULL_GRF_GPIO1A0_OFFSET;
		*regmap = info->regmap_base;
		*reg += (bank->bank_num - 1) * RV1126_PULL_BANK_STRIDE;
	}

	*reg += ((pin_num / RV1126_PULL_PINS_PER_REG) * 4);
	*bit = (pin_num % RV1126_PULL_PINS_PER_REG);
	*bit *= RV1126_PULL_BITS_PER_PIN;
}

#define RV1126_DRV_PMU_OFFSET		0x20
#define RV1126_DRV_GRF_GPIO1A0_OFFSET		0x10090
#define RV1126_DRV_BITS_PER_PIN		4
#define RV1126_DRV_PINS_PER_REG		4
#define RV1126_DRV_BANK_STRIDE		32

static void rv1126_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
					int pin_num, struct regmap **regmap,
					int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 24 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		if (RV1126_GPIO_C4_D7(pin_num)) {
			*regmap = info->regmap_base;
			*reg = RV1126_DRV_GRF_GPIO1A0_OFFSET;
			*reg -= (((31 - pin_num) / RV1126_DRV_PINS_PER_REG + 1) * 4);
			*reg -= 0x4;
			*bit = pin_num % RV1126_DRV_PINS_PER_REG;
			*bit *= RV1126_DRV_BITS_PER_PIN;
			return;
		}
		*regmap = info->regmap_pmu;
		*reg = RV1126_DRV_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RV1126_DRV_GRF_GPIO1A0_OFFSET;
		*reg += (bank->bank_num - 1) * RV1126_DRV_BANK_STRIDE;
	}

	*reg += ((pin_num / RV1126_DRV_PINS_PER_REG) * 4);
	*bit = pin_num % RV1126_DRV_PINS_PER_REG;
	*bit *= RV1126_DRV_BITS_PER_PIN;
}

#define RV1126_SCHMITT_PMU_OFFSET		0x60
#define RV1126_SCHMITT_GRF_GPIO1A0_OFFSET		0x10188
#define RV1126_SCHMITT_BANK_STRIDE		16
#define RV1126_SCHMITT_PINS_PER_GRF_REG		8
#define RV1126_SCHMITT_PINS_PER_PMU_REG		8

static int rv1126_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int pins_per_reg;

	if (bank->bank_num == 0) {
		if (RV1126_GPIO_C4_D7(pin_num)) {
			*regmap = info->regmap_base;
			*reg = RV1126_SCHMITT_GRF_GPIO1A0_OFFSET;
			*reg -= (((31 - pin_num) / RV1126_SCHMITT_PINS_PER_GRF_REG + 1) * 4);
			*bit = pin_num % RV1126_SCHMITT_PINS_PER_GRF_REG;
			return 0;
		}
		*regmap = info->regmap_pmu;
		*reg = RV1126_SCHMITT_PMU_OFFSET;
		pins_per_reg = RV1126_SCHMITT_PINS_PER_PMU_REG;
	} else {
		*regmap = info->regmap_base;
		*reg = RV1126_SCHMITT_GRF_GPIO1A0_OFFSET;
		pins_per_reg = RV1126_SCHMITT_PINS_PER_GRF_REG;
		*reg += (bank->bank_num - 1) * RV1126_SCHMITT_BANK_STRIDE;
	}
	*reg += ((pin_num / pins_per_reg) * 4);
	*bit = pin_num % pins_per_reg;

	return 0;
}

#define RK1808_PULL_PMU_OFFSET		0x10
#define RK1808_PULL_GRF_OFFSET		0x80
#define RK1808_PULL_PINS_PER_REG	8
#define RK1808_PULL_BITS_PER_PIN	2
#define RK1808_PULL_BANK_STRIDE		16

static void rk1808_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK1808_PULL_PMU_OFFSET;
	} else {
		*reg = RK1808_PULL_GRF_OFFSET;
		*regmap = info->regmap_base;
		*reg += (bank->bank_num - 1) * RK1808_PULL_BANK_STRIDE;
	}

	*reg += ((pin_num / RK1808_PULL_PINS_PER_REG) * 4);
	*bit = (pin_num % RK1808_PULL_PINS_PER_REG);
	*bit *= RK1808_PULL_BITS_PER_PIN;
}

#define RK1808_DRV_PMU_OFFSET		0x20
#define RK1808_DRV_GRF_OFFSET		0x140
#define RK1808_DRV_BITS_PER_PIN		2
#define RK1808_DRV_PINS_PER_REG		8
#define RK1808_DRV_BANK_STRIDE		16

static void rk1808_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
					int pin_num,
					struct regmap **regmap,
					int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK1808_DRV_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RK1808_DRV_GRF_OFFSET;
		*reg += (bank->bank_num - 1) * RK1808_DRV_BANK_STRIDE;
	}

	*reg += ((pin_num / RK1808_DRV_PINS_PER_REG) * 4);
	*bit = pin_num % RK1808_DRV_PINS_PER_REG;
	*bit *= RK1808_DRV_BITS_PER_PIN;
}

#define RK1808_SR_PMU_OFFSET		0x0030
#define RK1808_SR_GRF_OFFSET		0x00c0
#define RK1808_SR_BANK_STRIDE		16
#define RK1808_SR_PINS_PER_REG		8

static int rk1808_calc_slew_rate_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK1808_SR_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RK1808_SR_GRF_OFFSET;
		*reg += (bank->bank_num  - 1) * RK1808_SR_BANK_STRIDE;
	}
	*reg += ((pin_num / RK1808_SR_PINS_PER_REG) * 4);
	*bit = pin_num % RK1808_SR_PINS_PER_REG;

	return 0;
}

#define RK1808_SCHMITT_PMU_OFFSET		0x0040
#define RK1808_SCHMITT_GRF_OFFSET		0x0100
#define RK1808_SCHMITT_BANK_STRIDE		16
#define RK1808_SCHMITT_PINS_PER_REG		8

static int rk1808_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK1808_SCHMITT_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RK1808_SCHMITT_GRF_OFFSET;
		*reg += (bank->bank_num  - 1) * RK1808_SCHMITT_BANK_STRIDE;
	}
	*reg += ((pin_num / RK1808_SCHMITT_PINS_PER_REG) * 4);
	*bit = pin_num % RK1808_SCHMITT_PINS_PER_REG;

	return 0;
}

#define RK2928_PULL_OFFSET		0x118
#define RK2928_PULL_PINS_PER_REG	16
#define RK2928_PULL_BANK_STRIDE		8

static void rk2928_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK2928_PULL_OFFSET;
	*reg += bank->bank_num * RK2928_PULL_BANK_STRIDE;
	*reg += (pin_num / RK2928_PULL_PINS_PER_REG) * 4;

	*bit = pin_num % RK2928_PULL_PINS_PER_REG;
};

#define RK3128_PULL_OFFSET	0x118

static void rk3128_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3128_PULL_OFFSET;
	*reg += bank->bank_num * RK2928_PULL_BANK_STRIDE;
	*reg += ((pin_num / RK2928_PULL_PINS_PER_REG) * 4);

	*bit = pin_num % RK2928_PULL_PINS_PER_REG;
}

#define RK3188_PULL_OFFSET		0x164
#define RK3188_PULL_BITS_PER_PIN	2
#define RK3188_PULL_PINS_PER_REG	8
#define RK3188_PULL_BANK_STRIDE		16
#define RK3188_PULL_PMU_OFFSET		0x64

static void rk3188_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 12 pins of the first bank are located elsewhere */
	if (bank->bank_num == 0 && pin_num < 12) {
		*regmap = info->regmap_pmu ? info->regmap_pmu
					   : bank->regmap_pull;
		*reg = info->regmap_pmu ? RK3188_PULL_PMU_OFFSET : 0;
		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);
		*bit = pin_num % RK3188_PULL_PINS_PER_REG;
		*bit *= RK3188_PULL_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_pull ? info->regmap_pull
					    : info->regmap_base;
		*reg = info->regmap_pull ? 0 : RK3188_PULL_OFFSET;

		/* correct the offset, as it is the 2nd pull register */
		*reg -= 4;
		*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

		/*
		 * The bits in these registers have an inverse ordering
		 * with the lowest pin being in bits 15:14 and the highest
		 * pin in bits 1:0
		 */
		*bit = 7 - (pin_num % RK3188_PULL_PINS_PER_REG);
		*bit *= RK3188_PULL_BITS_PER_PIN;
	}
}

#define RK3288_PULL_OFFSET		0x140
static void rk3288_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 24 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3188_PULL_PMU_OFFSET;

		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);
		*bit = pin_num % RK3188_PULL_PINS_PER_REG;
		*bit *= RK3188_PULL_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3288_PULL_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3188_PULL_PINS_PER_REG);
		*bit *= RK3188_PULL_BITS_PER_PIN;
	}
}

#define RK3288_DRV_PMU_OFFSET		0x70
#define RK3288_DRV_GRF_OFFSET		0x1c0
#define RK3288_DRV_BITS_PER_PIN		2
#define RK3288_DRV_PINS_PER_REG		8
#define RK3288_DRV_BANK_STRIDE		16

static void rk3288_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 24 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3288_DRV_PMU_OFFSET;

		*reg += ((pin_num / RK3288_DRV_PINS_PER_REG) * 4);
		*bit = pin_num % RK3288_DRV_PINS_PER_REG;
		*bit *= RK3288_DRV_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3288_DRV_GRF_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * RK3288_DRV_BANK_STRIDE;
		*reg += ((pin_num / RK3288_DRV_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3288_DRV_PINS_PER_REG);
		*bit *= RK3288_DRV_BITS_PER_PIN;
	}
}

#define RK3228_PULL_OFFSET		0x100

static void rk3228_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3228_PULL_OFFSET;
	*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
	*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

	*bit = (pin_num % RK3188_PULL_PINS_PER_REG);
	*bit *= RK3188_PULL_BITS_PER_PIN;
}

#define RK3228_DRV_GRF_OFFSET		0x200

static void rk3228_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3228_DRV_GRF_OFFSET;
	*reg += bank->bank_num * RK3288_DRV_BANK_STRIDE;
	*reg += ((pin_num / RK3288_DRV_PINS_PER_REG) * 4);

	*bit = (pin_num % RK3288_DRV_PINS_PER_REG);
	*bit *= RK3288_DRV_BITS_PER_PIN;
}

#define RK3308_PULL_OFFSET		0xa0

static void rk3308_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3308_PULL_OFFSET;
	*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
	*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

	*bit = (pin_num % RK3188_PULL_PINS_PER_REG);
	*bit *= RK3188_PULL_BITS_PER_PIN;
}

#define RK3308_DRV_GRF_OFFSET		0x100

static void rk3308_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
				       int pin_num, struct regmap **regmap,
				       int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3308_DRV_GRF_OFFSET;
	*reg += bank->bank_num * RK3288_DRV_BANK_STRIDE;
	*reg += ((pin_num / RK3288_DRV_PINS_PER_REG) * 4);

	*bit = (pin_num % RK3288_DRV_PINS_PER_REG);
	*bit *= RK3288_DRV_BITS_PER_PIN;
}

#define RK3368_PULL_GRF_OFFSET		0x100
#define RK3368_PULL_PMU_OFFSET		0x10

static void rk3368_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 32 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3368_PULL_PMU_OFFSET;

		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);
		*bit = pin_num % RK3188_PULL_PINS_PER_REG;
		*bit *= RK3188_PULL_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3368_PULL_GRF_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3188_PULL_PINS_PER_REG);
		*bit *= RK3188_PULL_BITS_PER_PIN;
	}
}

#define RK3368_DRV_PMU_OFFSET		0x20
#define RK3368_DRV_GRF_OFFSET		0x200

static void rk3368_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
				    int pin_num, struct regmap **regmap,
				    int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 32 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3368_DRV_PMU_OFFSET;

		*reg += ((pin_num / RK3288_DRV_PINS_PER_REG) * 4);
		*bit = pin_num % RK3288_DRV_PINS_PER_REG;
		*bit *= RK3288_DRV_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3368_DRV_GRF_OFFSET;

		/* correct the offset, as we're starting with the 2nd bank */
		*reg -= 0x10;
		*reg += bank->bank_num * RK3288_DRV_BANK_STRIDE;
		*reg += ((pin_num / RK3288_DRV_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3288_DRV_PINS_PER_REG);
		*bit *= RK3288_DRV_BITS_PER_PIN;
	}
}

#define RK3399_PULL_GRF_OFFSET		0xe040
#define RK3399_PULL_PMU_OFFSET		0x40
#define RK3399_DRV_3BITS_PER_PIN	3

static void rk3399_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The bank0:16 and bank1:32 pins are located in PMU */
	if ((bank->bank_num == 0) || (bank->bank_num == 1)) {
		*regmap = info->regmap_pmu;
		*reg = RK3399_PULL_PMU_OFFSET;

		*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;

		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);
		*bit = pin_num % RK3188_PULL_PINS_PER_REG;
		*bit *= RK3188_PULL_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3399_PULL_GRF_OFFSET;

		/* correct the offset, as we're starting with the 3rd bank */
		*reg -= 0x20;
		*reg += bank->bank_num * RK3188_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3188_PULL_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3188_PULL_PINS_PER_REG);
		*bit *= RK3188_PULL_BITS_PER_PIN;
	}
}

static void rk3399_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
					int pin_num, struct regmap **regmap,
					int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int drv_num = (pin_num / 8);

	/*  The bank0:16 and bank1:32 pins are located in PMU */
	if ((bank->bank_num == 0) || (bank->bank_num == 1))
		*regmap = info->regmap_pmu;
	else
		*regmap = info->regmap_base;

	*reg = bank->drv[drv_num].offset;
	if ((bank->drv[drv_num].drv_type == DRV_TYPE_IO_1V8_3V0_AUTO) ||
	    (bank->drv[drv_num].drv_type == DRV_TYPE_IO_3V3_ONLY))
		*bit = (pin_num % 8) * 3;
	else
		*bit = (pin_num % 8) * 2;
}

#define RK3568_SR_PMU_OFFSET		0x60
#define RK3568_SR_GRF_OFFSET		0x0180
#define RK3568_SR_BANK_STRIDE		0x10
#define RK3568_SR_PINS_PER_REG		16

static int rk3568_calc_slew_rate_reg_and_bit(struct rockchip_pin_bank *bank,
					     int pin_num,
					     struct regmap **regmap,
					     int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3568_SR_PMU_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3568_SR_GRF_OFFSET;
		*reg += (bank->bank_num  - 1) * RK3568_SR_BANK_STRIDE;
	}
	*reg += ((pin_num / RK3568_SR_PINS_PER_REG) * 4);
	*bit = pin_num % RK3568_SR_PINS_PER_REG;

	return 0;
}

#define RK3568_PULL_PMU_OFFSET		0x20
#define RK3568_PULL_GRF_OFFSET		0x80
#define RK3568_PULL_BITS_PER_PIN	2
#define RK3568_PULL_PINS_PER_REG	8
#define RK3568_PULL_BANK_STRIDE		0x10

static void rk3568_calc_pull_reg_and_bit(struct rockchip_pin_bank *bank,
					 int pin_num, struct regmap **regmap,
					 int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3568_PULL_PMU_OFFSET;
		*reg += bank->bank_num * RK3568_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3568_PULL_PINS_PER_REG) * 4);

		*bit = pin_num % RK3568_PULL_PINS_PER_REG;
		*bit *= RK3568_PULL_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3568_PULL_GRF_OFFSET;
		*reg += (bank->bank_num - 1) * RK3568_PULL_BANK_STRIDE;
		*reg += ((pin_num / RK3568_PULL_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3568_PULL_PINS_PER_REG);
		*bit *= RK3568_PULL_BITS_PER_PIN;
	}
}

#define RK3568_DRV_PMU_OFFSET		0x70
#define RK3568_DRV_GRF_OFFSET		0x200
#define RK3568_DRV_BITS_PER_PIN		8
#define RK3568_DRV_PINS_PER_REG		2
#define RK3568_DRV_BANK_STRIDE		0x40

static void rk3568_calc_drv_reg_and_bit(struct rockchip_pin_bank *bank,
					int pin_num, struct regmap **regmap,
					int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	/* The first 32 pins of the first bank are located in PMU */
	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3568_DRV_PMU_OFFSET;
		*reg += ((pin_num / RK3568_DRV_PINS_PER_REG) * 4);

		*bit = pin_num % RK3568_DRV_PINS_PER_REG;
		*bit *= RK3568_DRV_BITS_PER_PIN;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3568_DRV_GRF_OFFSET;
		*reg += (bank->bank_num - 1) * RK3568_DRV_BANK_STRIDE;
		*reg += ((pin_num / RK3568_DRV_PINS_PER_REG) * 4);

		*bit = (pin_num % RK3568_DRV_PINS_PER_REG);
		*bit *= RK3568_DRV_BITS_PER_PIN;
	}

	if (rockchip_get_cpu_version() == 0)
		if ((bank->bank_num == 1 && (pin_num == 15 || pin_num == 23 || pin_num == 31)) ||
		    ((bank->bank_num == 2 || bank->bank_num == 3 || bank->bank_num == 4) &&
		     (pin_num == 7 || pin_num == 15 || pin_num == 23 || pin_num == 31)))
			*bit -= RK3568_DRV_BITS_PER_PIN;
}

static int rockchip_perpin_drv_list[DRV_TYPE_MAX][8] = {
	{ 2, 4, 8, 12, -1, -1, -1, -1 },
	{ 3, 6, 9, 12, -1, -1, -1, -1 },
	{ 5, 10, 15, 20, -1, -1, -1, -1 },
	{ 4, 6, 8, 10, 12, 14, 16, 18 },
	{ 4, 7, 10, 13, 16, 19, 22, 26 }
};

static int rockchip_get_drive_perpin(struct rockchip_pin_bank *bank,
				     int pin_num)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret;
	u32 data, temp, rmask_bits;
	u8 bit;
	int drv_type = bank->drv[pin_num / 8].drv_type;

	ctrl->drv_calc_reg(bank, pin_num, &regmap, &reg, &bit);

	switch (drv_type) {
	case DRV_TYPE_IO_1V8_3V0_AUTO:
	case DRV_TYPE_IO_3V3_ONLY:
		rmask_bits = RK3399_DRV_3BITS_PER_PIN;
		switch (bit) {
		case 0 ... 12:
			/* regular case, nothing to do */
			break;
		case 15:
			/*
			 * drive-strength offset is special, as it is
			 * spread over 2 registers
			 */
			ret = regmap_read(regmap, reg, &data);
			if (ret)
				return ret;

			ret = regmap_read(regmap, reg + 0x4, &temp);
			if (ret)
				return ret;

			/*
			 * the bit data[15] contains bit 0 of the value
			 * while temp[1:0] contains bits 2 and 1
			 */
			data >>= 15;
			temp &= 0x3;
			temp <<= 1;
			data |= temp;

			return rockchip_perpin_drv_list[drv_type][data];
		case 18 ... 21:
			/* setting fully enclosed in the second register */
			reg += 4;
			bit -= 16;
			break;
		default:
			dev_err(info->dev, "unsupported bit: %d for pinctrl drive type: %d\n",
				bit, drv_type);
			return -EINVAL;
		}

		break;
	case DRV_TYPE_IO_DEFAULT:
	case DRV_TYPE_IO_1V8_OR_3V0:
	case DRV_TYPE_IO_1V8_ONLY:
		rmask_bits = RK3288_DRV_BITS_PER_PIN;
		break;
	default:
		dev_err(info->dev, "unsupported pinctrl drive type: %d\n",
			drv_type);
		return -EINVAL;
	}

	ret = regmap_read(regmap, reg, &data);
	if (ret)
		return ret;

	data >>= bit;
	data &= (1 << rmask_bits) - 1;

	return rockchip_perpin_drv_list[drv_type][data];
}

static int rockchip_set_drive_perpin(struct rockchip_pin_bank *bank,
				     int pin_num, int strength)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret, i;
	u32 data, rmask, rmask_bits, temp;
	u8 bit;
	int drv_type = bank->drv[pin_num / 8].drv_type;

	dev_dbg(info->dev, "setting drive of GPIO%d-%d to %d\n",
		bank->bank_num, pin_num, strength);

	ctrl->drv_calc_reg(bank, pin_num, &regmap, &reg, &bit);
	if (ctrl->type == RV1126) {
		rmask_bits = RV1126_DRV_BITS_PER_PIN;
		ret = strength;
		goto config;
	} else if (ctrl->type == RK3568) {
		rmask_bits = RK3568_DRV_BITS_PER_PIN;
		ret = (1 << (strength + 1)) - 1;
		goto config;
	}

	ret = -EINVAL;
	for (i = 0; i < ARRAY_SIZE(rockchip_perpin_drv_list[drv_type]); i++) {
		if (rockchip_perpin_drv_list[drv_type][i] == strength) {
			ret = i;
			break;
		} else if (rockchip_perpin_drv_list[drv_type][i] < 0) {
			ret = rockchip_perpin_drv_list[drv_type][i];
			break;
		}
	}

	if (ret < 0) {
		dev_err(info->dev, "unsupported driver strength %d\n",
			strength);
		return ret;
	}

	switch (drv_type) {
	case DRV_TYPE_IO_1V8_3V0_AUTO:
	case DRV_TYPE_IO_3V3_ONLY:
		rmask_bits = RK3399_DRV_3BITS_PER_PIN;
		switch (bit) {
		case 0 ... 12:
			/* regular case, nothing to do */
			break;
		case 15:
			/*
			 * drive-strength offset is special, as it is spread
			 * over 2 registers, the bit data[15] contains bit 0
			 * of the value while temp[1:0] contains bits 2 and 1
			 */
			data = (ret & 0x1) << 15;
			temp = (ret >> 0x1) & 0x3;

			rmask = BIT(15) | BIT(31);
			data |= BIT(31);
			ret = regmap_update_bits(regmap, reg, rmask, data);
			if (ret)
				return ret;

			rmask = 0x3 | (0x3 << 16);
			temp |= (0x3 << 16);
			reg += 0x4;
			ret = regmap_update_bits(regmap, reg, rmask, temp);

			return ret;
		case 18 ... 21:
			/* setting fully enclosed in the second register */
			reg += 4;
			bit -= 16;
			break;
		default:
			dev_err(info->dev, "unsupported bit: %d for pinctrl drive type: %d\n",
				bit, drv_type);
			return -EINVAL;
		}
		break;
	case DRV_TYPE_IO_DEFAULT:
	case DRV_TYPE_IO_1V8_OR_3V0:
	case DRV_TYPE_IO_1V8_ONLY:
		rmask_bits = RK3288_DRV_BITS_PER_PIN;
		break;
	default:
		dev_err(info->dev, "unsupported pinctrl drive type: %d\n",
			drv_type);
		return -EINVAL;
	}

config:
	/* enable the write to the equivalent lower bits */
	data = ((1 << rmask_bits) - 1) << (bit + 16);
	rmask = data | (data >> 16);
	data |= (ret << bit);

	ret = regmap_update_bits(regmap, reg, rmask, data);
	if (ret)
		return ret;

	if (ctrl->type == RK3568 && rockchip_get_cpu_version() == 0) {
		if (bank->bank_num == 1 && pin_num == 21)
			reg = 0x0840;
		else if (bank->bank_num == 2 && pin_num == 2)
			reg = 0x0844;
		else if (bank->bank_num == 2 && pin_num == 8)
			reg = 0x0848;
		else if (bank->bank_num == 3 && pin_num == 0)
			reg = 0x084c;
		else if (bank->bank_num == 3 && pin_num == 6)
			reg = 0x0850;
		else if (bank->bank_num == 4 && pin_num == 0)
			reg = 0x0854;
		else
			return 0;

		data = ((1 << rmask_bits) - 1) << 16;
		rmask = data | (data >> 16);
		data |= (1 << (strength + 1)) - 1;

		ret = regmap_update_bits(regmap, reg, rmask, data);
		if (ret)
			return ret;
	}

	return 0;
}

static int rockchip_pull_list[PULL_TYPE_MAX][4] = {
	{
		PIN_CONFIG_BIAS_DISABLE,
		PIN_CONFIG_BIAS_PULL_UP,
		PIN_CONFIG_BIAS_PULL_DOWN,
		PIN_CONFIG_BIAS_BUS_HOLD
	},
	{
		PIN_CONFIG_BIAS_DISABLE,
		PIN_CONFIG_BIAS_PULL_DOWN,
		PIN_CONFIG_BIAS_DISABLE,
		PIN_CONFIG_BIAS_PULL_UP
	},
};

static int rockchip_get_pull(struct rockchip_pin_bank *bank, int pin_num)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret, pull_type;
	u8 bit;
	u32 data;

	/* rk3066b does support any pulls */
	if (ctrl->type == RK3066B)
		return PIN_CONFIG_BIAS_DISABLE;

	ctrl->pull_calc_reg(bank, pin_num, &regmap, &reg, &bit);

	ret = regmap_read(regmap, reg, &data);
	if (ret)
		return ret;

	switch (ctrl->type) {
	case RK2928:
	case RK3128:
		return !(data & BIT(bit))
				? PIN_CONFIG_BIAS_PULL_PIN_DEFAULT
				: PIN_CONFIG_BIAS_DISABLE;
	case PX30:
	case RV1108:
	case RK1808:
	case RK3188:
	case RK3288:
	case RK3308:
	case RK3368:
	case RK3399:
	case RK3568:
		pull_type = bank->pull_type[pin_num / 8];
		data >>= bit;
		data &= (1 << RK3188_PULL_BITS_PER_PIN) - 1;

		return rockchip_pull_list[pull_type][data];
	default:
		dev_err(info->dev, "unsupported pinctrl type\n");
		return -EINVAL;
	};
}

static int rockchip_set_pull(struct rockchip_pin_bank *bank,
					int pin_num, int pull)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret, i, pull_type;
	u8 bit;
	u32 data, rmask;

	dev_dbg(info->dev, "setting pull of GPIO%d-%d to %d\n",
		 bank->bank_num, pin_num, pull);

	/* rk3066b does support any pulls */
	if (ctrl->type == RK3066B)
		return pull ? -EINVAL : 0;

	ctrl->pull_calc_reg(bank, pin_num, &regmap, &reg, &bit);

	switch (ctrl->type) {
	case RK2928:
	case RK3128:
		data = BIT(bit + 16);
		if (pull == PIN_CONFIG_BIAS_DISABLE)
			data |= BIT(bit);
		ret = regmap_write(regmap, reg, data);
		break;
	case PX30:
	case RV1108:
	case RV1126:
	case RK1808:
	case RK3188:
	case RK3288:
	case RK3308:
	case RK3368:
	case RK3399:
	case RK3568:
		pull_type = bank->pull_type[pin_num / 8];
		ret = -EINVAL;
		for (i = 0; i < ARRAY_SIZE(rockchip_pull_list[pull_type]);
			i++) {
			if (rockchip_pull_list[pull_type][i] == pull) {
				ret = i;
				break;
			}
		}

		if (ctrl->type == RK3568 && bank->bank_num == 0 && pin_num >= 27 && pin_num <= 30) {
			if (ret == 1)
				ret = 3;
		}

		if (ret < 0) {
			dev_err(info->dev, "unsupported pull setting %d\n",
				pull);
			return ret;
		}

		/* enable the write to the equivalent lower bits */
		data = ((1 << RK3188_PULL_BITS_PER_PIN) - 1) << (bit + 16);
		rmask = data | (data >> 16);
		data |= (ret << bit);

		ret = regmap_update_bits(regmap, reg, rmask, data);
		break;
	default:
		dev_err(info->dev, "unsupported pinctrl type\n");
		return -EINVAL;
	}

	return ret;
}

#define RK3308_SCHMITT_PINS_PER_REG		8
#define RK3308_SCHMITT_BANK_STRIDE		16
#define RK3308_SCHMITT_GRF_OFFSET		0x1a0

static int rk3308_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3308_SCHMITT_GRF_OFFSET;

	*reg += bank->bank_num * RK3308_SCHMITT_BANK_STRIDE;
	*reg += ((pin_num / RK3308_SCHMITT_PINS_PER_REG) * 4);
	*bit = pin_num % RK3308_SCHMITT_PINS_PER_REG;

	return 0;
}

#define RK3328_SCHMITT_BITS_PER_PIN		1
#define RK3328_SCHMITT_PINS_PER_REG		16
#define RK3328_SCHMITT_BANK_STRIDE		8
#define RK3328_SCHMITT_GRF_OFFSET		0x380

static int rk3328_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	*regmap = info->regmap_base;
	*reg = RK3328_SCHMITT_GRF_OFFSET;

	*reg += bank->bank_num * RK3328_SCHMITT_BANK_STRIDE;
	*reg += ((pin_num / RK3328_SCHMITT_PINS_PER_REG) * 4);
	*bit = pin_num % RK3328_SCHMITT_PINS_PER_REG;

	return 0;
}

#define RK3568_SCHMITT_BITS_PER_PIN		2
#define RK3568_SCHMITT_PINS_PER_REG		8
#define RK3568_SCHMITT_BANK_STRIDE		0x10
#define RK3568_SCHMITT_GRF_OFFSET		0xc0
#define RK3568_SCHMITT_PMUGRF_OFFSET		0x30

static int rk3568_calc_schmitt_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = RK3568_SCHMITT_PMUGRF_OFFSET;
	} else {
		*regmap = info->regmap_base;
		*reg = RK3568_SCHMITT_GRF_OFFSET;
		*reg += (bank->bank_num - 1) * RK3568_SCHMITT_BANK_STRIDE;
	}

	*reg += ((pin_num / RK3568_SCHMITT_PINS_PER_REG) * 4);
	*bit = pin_num % RK3568_SCHMITT_PINS_PER_REG;
	*bit *= RK3568_SCHMITT_BITS_PER_PIN;

	return 0;
}

static int rockchip_get_schmitt(struct rockchip_pin_bank *bank, int pin_num)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret;
	u8 bit;
	u32 data;

	ret = ctrl->schmitt_calc_reg(bank, pin_num, &regmap, &reg, &bit);
	if (ret)
		return ret;

	ret = regmap_read(regmap, reg, &data);
	if (ret)
		return ret;

	data >>= bit;
	switch (ctrl->type) {
	case RK3568:
		return data & ((1 << RK3568_SCHMITT_BITS_PER_PIN) - 1);
	default:
		break;
	}

	return data & 0x1;
}

static int rockchip_set_schmitt(struct rockchip_pin_bank *bank,
				int pin_num, int enable)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret;
	u8 bit;
	u32 data, rmask;

	dev_dbg(info->dev, "setting input schmitt of GPIO%d-%d to %d\n",
		bank->bank_num, pin_num, enable);

	ret = ctrl->schmitt_calc_reg(bank, pin_num, &regmap, &reg, &bit);
	if (ret)
		return ret;

	/* enable the write to the equivalent lower bits */
	switch (ctrl->type) {
	case RK3568:
		data = ((1 << RK3568_SCHMITT_BITS_PER_PIN) - 1) << (bit + 16);
		rmask = data | (data >> 16);
		data |= ((enable ? 0x2 : 0x1) << bit);
		break;
	default:
		data = BIT(bit + 16) | (enable << bit);
		rmask = BIT(bit + 16) | BIT(bit);
		break;
	}

	return regmap_update_bits(regmap, reg, rmask, data);
}

#define PX30_SLEW_RATE_PMU_OFFSET		0x30
#define PX30_SLEW_RATE_GRF_OFFSET		0x90
#define PX30_SLEW_RATE_PINS_PER_PMU_REG		16
#define PX30_SLEW_RATE_BANK_STRIDE		16
#define PX30_SLEW_RATE_PINS_PER_GRF_REG		8

static int px30_calc_slew_rate_reg_and_bit(struct rockchip_pin_bank *bank,
					   int pin_num,
					   struct regmap **regmap,
					   int *reg, u8 *bit)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	int pins_per_reg;

	if (bank->bank_num == 0) {
		*regmap = info->regmap_pmu;
		*reg = PX30_SLEW_RATE_PMU_OFFSET;
		pins_per_reg = PX30_SLEW_RATE_PINS_PER_PMU_REG;
	} else {
		*regmap = info->regmap_base;
		*reg = PX30_SLEW_RATE_GRF_OFFSET;
		pins_per_reg = PX30_SLEW_RATE_PINS_PER_GRF_REG;
		*reg += (bank->bank_num  - 1) * PX30_SLEW_RATE_BANK_STRIDE;
	}
	*reg += ((pin_num / pins_per_reg) * 4);
	*bit = pin_num % pins_per_reg;

	return 0;
}

static int rockchip_get_slew_rate(struct rockchip_pin_bank *bank, int pin_num)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret;
	u8 bit;
	u32 data;

	ret = ctrl->slew_rate_calc_reg(bank, pin_num, &regmap, &reg, &bit);
	if (ret)
		return ret;

	ret = regmap_read(regmap, reg, &data);
	if (ret)
		return ret;

	data >>= bit;
	return data & 0x1;
}

static int rockchip_set_slew_rate(struct rockchip_pin_bank *bank,
				  int pin_num, int speed)
{
	struct rockchip_pinctrl *info = bank->drvdata;
	struct rockchip_pin_ctrl *ctrl = info->ctrl;
	struct regmap *regmap;
	int reg, ret;
	u8 bit;
	u32 data, rmask;

	dev_dbg(info->dev, "setting slew rate of GPIO%d-%d to %d\n",
		bank->bank_num, pin_num, speed);

	ret = ctrl->slew_rate_calc_reg(bank, pin_num, &regmap, &reg, &bit);
	if (ret)
		return ret;

	/* enable the write to the equivalent lower bits */
	data = BIT(bit + 16) | (speed << bit);
	rmask = BIT(bit + 16) | BIT(bit);

	return regmap_update_bits(regmap, reg, rmask, data);
}

/*
 * Pinmux_ops handling
 */

static int rockchip_pmx_get_funcs_count(struct pinctrl_dev *pctldev)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->nfunctions;
}

static const char *rockchip_pmx_get_func_name(struct pinctrl_dev *pctldev,
					  unsigned selector)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	return info->functions[selector].name;
}

static int rockchip_pmx_get_groups(struct pinctrl_dev *pctldev,
				unsigned selector, const char * const **groups,
				unsigned * const num_groups)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);

	*groups = info->functions[selector].groups;
	*num_groups = info->functions[selector].ngroups;

	return 0;
}

static int rockchip_pmx_set(struct pinctrl_dev *pctldev, unsigned selector,
			    unsigned group)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	const unsigned int *pins = info->groups[group].pins;
	const struct rockchip_pin_config *data = info->groups[group].data;
	struct rockchip_pin_bank *bank;
	int cnt, ret = 0;

	dev_dbg(info->dev, "enable function %s group %s\n",
		info->functions[selector].name, info->groups[group].name);

	/*
	 * for each pin in the pin group selected, program the corresponding
	 * pin function number in the config register.
	 */
	for (cnt = 0; cnt < info->groups[group].npins; cnt++) {
		bank = pin_to_bank(info, pins[cnt]);
		ret = rockchip_set_mux(bank, pins[cnt] - bank->pin_base,
				       data[cnt].func);
		if (ret)
			break;
	}

	if (ret && cnt) {
		/* revert the already done pin settings */
		for (cnt--; cnt >= 0 && !data[cnt].func; cnt--)
			rockchip_set_mux(bank, pins[cnt] - bank->pin_base, 0);

		return ret;
	}

	return 0;
}

static const struct pinmux_ops rockchip_pmx_ops = {
	.get_functions_count	= rockchip_pmx_get_funcs_count,
	.get_function_name	= rockchip_pmx_get_func_name,
	.get_function_groups	= rockchip_pmx_get_groups,
	.set_mux		= rockchip_pmx_set,
};

/*
 * Pinconf_ops handling
 */

static bool rockchip_pinconf_pull_valid(struct rockchip_pin_ctrl *ctrl,
					enum pin_config_param pull)
{
	switch (ctrl->type) {
	case RK2928:
	case RK3128:
		return (pull == PIN_CONFIG_BIAS_PULL_PIN_DEFAULT ||
					pull == PIN_CONFIG_BIAS_DISABLE);
	case RK3066B:
		return pull ? false : true;
	case PX30:
	case RV1108:
	case RV1126:
	case RK1808:
	case RK3188:
	case RK3288:
	case RK3308:
	case RK3368:
	case RK3399:
	case RK3568:
		return (pull != PIN_CONFIG_BIAS_PULL_PIN_DEFAULT);
	}

	return false;
}

/* set the pin config settings for a specified pin */
static int rockchip_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
				unsigned long *configs, unsigned num_configs)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct rockchip_pin_bank *bank = pin_to_bank(info, pin);
	struct gpio_chip *gpio = &bank->gpio_chip;
	enum pin_config_param param;
	u32 arg;
	int i;
	int rc;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			rc =  rockchip_set_pull(bank, pin - bank->pin_base,
				param);
			if (rc)
				return rc;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
		case PIN_CONFIG_BIAS_PULL_DOWN:
		case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
		case PIN_CONFIG_BIAS_BUS_HOLD:
			if (!rockchip_pinconf_pull_valid(info->ctrl, param))
				return -ENOTSUPP;

			if (!arg)
				return -EINVAL;

			rc = rockchip_set_pull(bank, pin - bank->pin_base,
				param);
			if (rc)
				return rc;
			break;
		case PIN_CONFIG_DRIVE_STRENGTH:
			/* rk3288 is the first with per-pin drive-strength */
			if (!info->ctrl->drv_calc_reg)
				return -ENOTSUPP;

			rc = rockchip_set_drive_perpin(bank,
						pin - bank->pin_base, arg);
			if (rc < 0)
				return rc;
			break;
		case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
			if (!info->ctrl->schmitt_calc_reg)
				return -ENOTSUPP;

			rc = rockchip_set_schmitt(bank,
						  pin - bank->pin_base, arg);
			if (rc < 0)
				return rc;
			break;
		case PIN_CONFIG_SLEW_RATE:
			if (!info->ctrl->slew_rate_calc_reg)
				return -ENOTSUPP;

			rc = rockchip_set_slew_rate(bank,
						    pin - bank->pin_base, arg);
			if (rc < 0)
				return rc;
			break;
		case PIN_CONFIG_OUTPUT:
			rc = rockchip_get_mux(bank, pin - bank->pin_base);
			if (rc != 0) {
				dev_err(info->dev, "pin-%d has been mux to func%d\n", pin, rc);
				return -EINVAL;
			}

			rc = gpio->direction_output(gpio, pin - bank->pin_base, arg);
			if (rc)
				return rc;
			break;
		default:
			return -ENOTSUPP;
		}
	} /* for each config */

	return 0;
}

/* get the pin config settings for a specified pin */
static int rockchip_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
							unsigned long *config)
{
	struct rockchip_pinctrl *info = pinctrl_dev_get_drvdata(pctldev);
	struct rockchip_pin_bank *bank = pin_to_bank(info, pin);
	struct gpio_chip *gpio = &bank->gpio_chip;
	enum pin_config_param param = pinconf_to_config_param(*config);
	u16 arg;
	int rc;

	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		if (rockchip_get_pull(bank, pin - bank->pin_base) != param)
			return -EINVAL;

		arg = 0;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_PIN_DEFAULT:
	case PIN_CONFIG_BIAS_BUS_HOLD:
		if (!rockchip_pinconf_pull_valid(info->ctrl, param))
			return -ENOTSUPP;

		if (rockchip_get_pull(bank, pin - bank->pin_base) != param)
			return -EINVAL;

		arg = 1;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		/* rk3288 is the first with per-pin drive-strength */
		if (!info->ctrl->drv_calc_reg)
			return -ENOTSUPP;

		rc = rockchip_get_drive_perpin(bank, pin - bank->pin_base);
		if (rc < 0)
			return rc;

		arg = rc;
		break;
	case PIN_CONFIG_INPUT_SCHMITT_ENABLE:
		if (!info->ctrl->schmitt_calc_reg)
			return -ENOTSUPP;

		rc = rockchip_get_schmitt(bank, pin - bank->pin_base);
		if (rc < 0)
			return rc;

		arg = rc;
		break;
	case PIN_CONFIG_SLEW_RATE:
		if (!info->ctrl->slew_rate_calc_reg)
			return -ENOTSUPP;

		rc = rockchip_get_slew_rate(bank, pin - bank->pin_base);
		if (rc < 0)
			return rc;

		arg = rc;
		break;
	case PIN_CONFIG_OUTPUT:
		rc = rockchip_get_mux(bank, pin - bank->pin_base);
		if (rc != 0) {
			dev_err(info->dev, "pin-%d has been mux to func%d\n", pin, rc);
			return -EINVAL;
		}

		rc = gpio->get(gpio, pin - bank->pin_base);
		if (rc < 0)
			return rc;

		arg = rc ? 1 : 0;
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static const struct pinconf_ops rockchip_pinconf_ops = {
	.pin_config_get			= rockchip_pinconf_get,
	.pin_config_set			= rockchip_pinconf_set,
	.is_generic			= true,
};

static const struct of_device_id rockchip_bank_match[] = {
	{ .compatible = "rockchip,gpio-bank" },
	{ .compatible = "rockchip,rk3188-gpio-bank0" },
	{},
};

static void rockchip_pinctrl_child_count(struct rockchip_pinctrl *info,
						struct device_node *np)
{
	struct device_node *child;

	for_each_child_of_node(np, child) {
		if (of_match_node(rockchip_bank_match, child))
			continue;

		info->nfunctions++;
		info->ngroups += of_get_child_count(child);
	}
}

static int rockchip_pinctrl_parse_groups(struct device_node *np,
					      struct rockchip_pin_group *grp,
					      struct rockchip_pinctrl *info,
					      u32 index)
{
	struct rockchip_pin_bank *bank;
	int size;
	const __be32 *list;
	int num;
	int i, j;
	int ret;

	dev_dbg(info->dev, "group(%d): %s\n", index, np->name);

	/* Initialise group */
	grp->name = np->name;

	/*
	 * the binding format is rockchip,pins = <bank pin mux CONFIG>,
	 * do sanity check and calculate pins number
	 */
	list = of_get_property(np, "rockchip,pins", &size);
	/* we do not check return since it's safe node passed down */
	size /= sizeof(*list);
	if (!size || size % 4) {
		dev_err(info->dev, "wrong pins number or pins and configs should be by 4\n");
		return -EINVAL;
	}

	grp->npins = size / 4;

	grp->pins = devm_kcalloc(info->dev, grp->npins, sizeof(unsigned int),
						GFP_KERNEL);
	grp->data = devm_kcalloc(info->dev,
					grp->npins,
					sizeof(struct rockchip_pin_config),
					GFP_KERNEL);
	if (!grp->pins || !grp->data)
		return -ENOMEM;

	for (i = 0, j = 0; i < size; i += 4, j++) {
		const __be32 *phandle;
		struct device_node *np_config;

		num = be32_to_cpu(*list++);
		bank = bank_num_to_bank(info, num);
		if (IS_ERR(bank))
			return PTR_ERR(bank);

		grp->pins[j] = bank->pin_base + be32_to_cpu(*list++);
		grp->data[j].func = be32_to_cpu(*list++);

		phandle = list++;
		if (!phandle)
			return -EINVAL;

		np_config = of_find_node_by_phandle(be32_to_cpup(phandle));
		ret = pinconf_generic_parse_dt_config(np_config, NULL,
				&grp->data[j].configs, &grp->data[j].nconfigs);
		if (ret)
			return ret;
	}

	return 0;
}

static int rockchip_pinctrl_parse_functions(struct device_node *np,
						struct rockchip_pinctrl *info,
						u32 index)
{
	struct device_node *child;
	struct rockchip_pmx_func *func;
	struct rockchip_pin_group *grp;
	int ret;
	static u32 grp_index;
	u32 i = 0;

	dev_dbg(info->dev, "parse function(%d): %s\n", index, np->name);

	func = &info->functions[index];

	/* Initialise function */
	func->name = np->name;
	func->ngroups = of_get_child_count(np);
	if (func->ngroups <= 0)
		return 0;

	func->groups = devm_kcalloc(info->dev,
			func->ngroups, sizeof(char *), GFP_KERNEL);
	if (!func->groups)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		func->groups[i] = child->name;
		grp = &info->groups[grp_index++];
		ret = rockchip_pinctrl_parse_groups(child, grp, info, i++);
		if (ret) {
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

static int rockchip_pinctrl_parse_dt(struct platform_device *pdev,
					      struct rockchip_pinctrl *info)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *child;
	int ret;
	int i;

	rockchip_pinctrl_child_count(info, np);

	dev_dbg(&pdev->dev, "nfunctions = %d\n", info->nfunctions);
	dev_dbg(&pdev->dev, "ngroups = %d\n", info->ngroups);

	info->functions = devm_kcalloc(dev,
					      info->nfunctions,
					      sizeof(struct rockchip_pmx_func),
					      GFP_KERNEL);
	if (!info->functions)
		return -EINVAL;

	info->groups = devm_kcalloc(dev,
					    info->ngroups,
					    sizeof(struct rockchip_pin_group),
					    GFP_KERNEL);
	if (!info->groups)
		return -EINVAL;

	i = 0;

	for_each_child_of_node(np, child) {
		if (of_match_node(rockchip_bank_match, child))
			continue;

		ret = rockchip_pinctrl_parse_functions(child, info, i++);
		if (ret) {
			dev_err(&pdev->dev, "failed to parse function\n");
			of_node_put(child);
			return ret;
		}
	}

	return 0;
}

static int rockchip_pinctrl_register(struct platform_device *pdev,
					struct rockchip_pinctrl *info)
{
	struct pinctrl_desc *ctrldesc = &info->pctl;
	struct pinctrl_pin_desc *pindesc, *pdesc;
	struct rockchip_pin_bank *pin_bank;
	int pin, bank, ret;
	int k;

	ctrldesc->name = "rockchip-pinctrl";
	ctrldesc->owner = THIS_MODULE;
	ctrldesc->pctlops = &rockchip_pctrl_ops;
	ctrldesc->pmxops = &rockchip_pmx_ops;
	ctrldesc->confops = &rockchip_pinconf_ops;

	pindesc = devm_kcalloc(&pdev->dev,
			       info->ctrl->nr_pins, sizeof(*pindesc),
			       GFP_KERNEL);
	if (!pindesc)
		return -ENOMEM;

	ctrldesc->pins = pindesc;
	ctrldesc->npins = info->ctrl->nr_pins;

	pdesc = pindesc;
	for (bank = 0, k = 0; bank < info->ctrl->nr_banks; bank++) {
		pin_bank = &info->ctrl->pin_banks[bank];
		for (pin = 0; pin < pin_bank->nr_pins; pin++, k++) {
			pdesc->number = k;
			pdesc->name = kasprintf(GFP_KERNEL, "%s-%d",
						pin_bank->name, pin);
			pdesc++;
		}
	}

	ret = rockchip_pinctrl_parse_dt(pdev, info);
	if (ret)
		return ret;

	info->pctl_dev = devm_pinctrl_register(&pdev->dev, ctrldesc, info);
	if (IS_ERR(info->pctl_dev)) {
		dev_err(&pdev->dev, "could not register pinctrl driver\n");
		return PTR_ERR(info->pctl_dev);
	}

	return 0;
}

static const struct of_device_id rockchip_pinctrl_dt_match[];

/* Ctrl data specially handle */
static int rk3308b_ctrl_data_re_init(struct rockchip_pin_ctrl *ctrl)
{
	/*
	 * Special for rk3308b, where we need to replace the recalced
	 * and routed arrays.
	 */
	if (soc_is_rk3308b()) {
		ctrl->iomux_recalced = rk3308b_mux_recalced_data;
		ctrl->niomux_recalced = ARRAY_SIZE(rk3308b_mux_recalced_data);
		ctrl->iomux_routes = rk3308b_mux_route_data;
		ctrl->niomux_routes = ARRAY_SIZE(rk3308b_mux_route_data);

	}

	return 0;
}

/* retrieve the soc specific data */
static struct rockchip_pin_ctrl *rockchip_pinctrl_get_soc_data(
						struct rockchip_pinctrl *d,
						struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct device_node *node = pdev->dev.of_node;
	struct rockchip_pin_ctrl *ctrl;
	struct rockchip_pin_bank *bank;
	int grf_offs, pmu_offs, drv_grf_offs, drv_pmu_offs, i, j;

	match = of_match_node(rockchip_pinctrl_dt_match, node);
	ctrl = (struct rockchip_pin_ctrl *)match->data;

	/* Ctrl data re-initialize for some Socs */
	if (ctrl->ctrl_data_re_init) {
		if (ctrl->ctrl_data_re_init(ctrl))
			return NULL;
	}

	grf_offs = ctrl->grf_mux_offset;
	pmu_offs = ctrl->pmu_mux_offset;
	drv_pmu_offs = ctrl->pmu_drv_offset;
	drv_grf_offs = ctrl->grf_drv_offset;
	bank = ctrl->pin_banks;
	for (i = 0; i < ctrl->nr_banks; ++i, ++bank) {
		int bank_pins = 0;

		raw_spin_lock_init(&bank->slock);
		bank->drvdata = d;
		bank->pin_base = ctrl->nr_pins;
		ctrl->nr_pins += bank->nr_pins;

		/* calculate iomux and drv offsets */
		for (j = 0; j < 4; j++) {
			struct rockchip_iomux *iom = &bank->iomux[j];
			struct rockchip_drv *drv = &bank->drv[j];
			int inc;

			if (bank_pins >= bank->nr_pins)
				break;

			/* preset iomux offset value, set new start value */
			if (iom->offset >= 0) {
				if ((iom->type & IOMUX_SOURCE_PMU) || (iom->type & IOMUX_L_SOURCE_PMU))
					pmu_offs = iom->offset;
				else
					grf_offs = iom->offset;
			} else { /* set current iomux offset */
				iom->offset = ((iom->type & IOMUX_SOURCE_PMU) ||
					       (iom->type & IOMUX_L_SOURCE_PMU)) ?
							pmu_offs : grf_offs;
			}

			/* preset drv offset value, set new start value */
			if (drv->offset >= 0) {
				if (iom->type & IOMUX_SOURCE_PMU)
					drv_pmu_offs = drv->offset;
				else
					drv_grf_offs = drv->offset;
			} else { /* set current drv offset */
				drv->offset = (iom->type & IOMUX_SOURCE_PMU) ?
						drv_pmu_offs : drv_grf_offs;
			}

			dev_dbg(d->dev, "bank %d, iomux %d has iom_offset 0x%x drv_offset 0x%x\n",
				i, j, iom->offset, drv->offset);

			/*
			 * Increase offset according to iomux width.
			 * 4bit iomux'es are spread over two registers.
			 */
			inc = (iom->type & (IOMUX_WIDTH_4BIT |
					    IOMUX_WIDTH_3BIT |
					    IOMUX_8WIDTH_2BIT)) ? 8 : 4;
			if ((iom->type & IOMUX_SOURCE_PMU) || (iom->type & IOMUX_L_SOURCE_PMU))
				pmu_offs += inc;
			else
				grf_offs += inc;

			/*
			 * Increase offset according to drv width.
			 * 3bit drive-strenth'es are spread over two registers.
			 */
			if ((drv->drv_type == DRV_TYPE_IO_1V8_3V0_AUTO) ||
			    (drv->drv_type == DRV_TYPE_IO_3V3_ONLY))
				inc = 8;
			else
				inc = 4;

			if (iom->type & IOMUX_SOURCE_PMU)
				drv_pmu_offs += inc;
			else
				drv_grf_offs += inc;

			bank_pins += 8;
		}

		/* calculate the per-bank recalced_mask */
		for (j = 0; j < ctrl->niomux_recalced; j++) {
			int pin = 0;

			if (ctrl->iomux_recalced[j].num == bank->bank_num) {
				pin = ctrl->iomux_recalced[j].pin;
				bank->recalced_mask |= BIT(pin);
			}
		}

		/* calculate the per-bank route_mask */
		for (j = 0; j < ctrl->niomux_routes; j++) {
			int pin = 0;

			if (ctrl->iomux_routes[j].bank_num == bank->bank_num) {
				pin = ctrl->iomux_routes[j].pin;
				bank->route_mask |= BIT(pin);
			}
		}
	}

	return ctrl;
}

#define RK3288_GRF_GPIO6C_IOMUX		0x64
#define GPIO6C6_SEL_WRITE_ENABLE	BIT(28)

static u32 rk3288_grf_gpio6c_iomux;

static int __maybe_unused rockchip_pinctrl_suspend(struct device *dev)
{
	struct rockchip_pinctrl *info = dev_get_drvdata(dev);
	int ret = pinctrl_force_sleep(info->pctl_dev);

	if (ret)
		return ret;

	/*
	 * RK3288 GPIO6_C6 mux would be modified by Maskrom when resume, so save
	 * the setting here, and restore it at resume.
	 */
	if (info->ctrl->type == RK3288) {
		ret = regmap_read(info->regmap_base, RK3288_GRF_GPIO6C_IOMUX,
				  &rk3288_grf_gpio6c_iomux);
		if (ret) {
			pinctrl_force_default(info->pctl_dev);
			return ret;
		}
	}

	return 0;
}

static int __maybe_unused rockchip_pinctrl_resume(struct device *dev)
{
	struct rockchip_pinctrl *info = dev_get_drvdata(dev);
	int ret;

	if (info->ctrl->type == RK3288) {
		ret = regmap_write(info->regmap_base, RK3288_GRF_GPIO6C_IOMUX,
				   rk3288_grf_gpio6c_iomux |
				   GPIO6C6_SEL_WRITE_ENABLE);
		if (ret)
			return ret;
	}

	return pinctrl_force_default(info->pctl_dev);
}

static SIMPLE_DEV_PM_OPS(rockchip_pinctrl_dev_pm_ops, rockchip_pinctrl_suspend,
			 rockchip_pinctrl_resume);

/* SoC data specially handle */

/* rk3308b SoC data initialize */
#define RK3308B_GRF_SOC_CON13			0x608
#define RK3308B_GRF_SOC_CON15			0x610

/* RK3308B_GRF_SOC_CON13 */
#define RK3308B_GRF_I2C3_IOFUNC_SRC_CTRL	(BIT(16 + 10) | BIT(10))
#define RK3308B_GRF_GPIO2A3_SEL_SRC_CTRL	(BIT(16 + 7)  | BIT(7))
#define RK3308B_GRF_GPIO2A2_SEL_SRC_CTRL	(BIT(16 + 3)  | BIT(3))

/* RK3308B_GRF_SOC_CON15 */
#define RK3308B_GRF_GPIO2C0_SEL_SRC_CTRL	(BIT(16 + 11) | BIT(11))
#define RK3308B_GRF_GPIO3B3_SEL_SRC_CTRL	(BIT(16 + 7)  | BIT(7))
#define RK3308B_GRF_GPIO3B2_SEL_SRC_CTRL	(BIT(16 + 3)  | BIT(3))

static int rk3308b_soc_data_init(struct rockchip_pinctrl *info)
{
	int ret;

	/*
	 * Enable the special ctrl of selected sources.
	 */
	if (soc_is_rk3308b()) {
		ret = regmap_write(info->regmap_base, RK3308B_GRF_SOC_CON13,
				   RK3308B_GRF_I2C3_IOFUNC_SRC_CTRL |
				   RK3308B_GRF_GPIO2A3_SEL_SRC_CTRL |
				   RK3308B_GRF_GPIO2A2_SEL_SRC_CTRL);
		if (ret)
			return ret;

		ret = regmap_write(info->regmap_base, RK3308B_GRF_SOC_CON15,
				   RK3308B_GRF_GPIO2C0_SEL_SRC_CTRL |
				   RK3308B_GRF_GPIO3B3_SEL_SRC_CTRL |
				   RK3308B_GRF_GPIO3B2_SEL_SRC_CTRL);
		if (ret)
			return ret;
	}

	return 0;
}

static int rockchip_pinctrl_probe(struct platform_device *pdev)
{
	struct rockchip_pinctrl *info;
	struct device *dev = &pdev->dev;
	struct rockchip_pin_ctrl *ctrl;
	struct device_node *np = pdev->dev.of_node, *node;
	struct resource *res;
	void __iomem *base;
	int ret;

	if (!dev->of_node) {
		dev_err(dev, "device tree node not found\n");
		return -ENODEV;
	}

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = dev;

	ctrl = rockchip_pinctrl_get_soc_data(info, pdev);
	if (!ctrl) {
		dev_err(dev, "driver data not available\n");
		return -EINVAL;
	}
	info->ctrl = ctrl;

	node = of_parse_phandle(np, "rockchip,grf", 0);
	if (node) {
		info->regmap_base = syscon_node_to_regmap(node);
		if (IS_ERR(info->regmap_base))
			return PTR_ERR(info->regmap_base);
	} else {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		base = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(base))
			return PTR_ERR(base);

		rockchip_regmap_config.max_register = resource_size(res) - 4;
		rockchip_regmap_config.name = "rockchip,pinctrl";
		info->regmap_base = devm_regmap_init_mmio(&pdev->dev, base,
						    &rockchip_regmap_config);

		/* to check for the old dt-bindings */
		info->reg_size = resource_size(res);

		/* Honor the old binding, with pull registers as 2nd resource */
		if (ctrl->type == RK3188 && info->reg_size < 0x200) {
			res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
			base = devm_ioremap_resource(&pdev->dev, res);
			if (IS_ERR(base))
				return PTR_ERR(base);

			rockchip_regmap_config.max_register =
							resource_size(res) - 4;
			rockchip_regmap_config.name = "rockchip,pinctrl-pull";
			info->regmap_pull = devm_regmap_init_mmio(&pdev->dev,
						    base,
						    &rockchip_regmap_config);
		}
	}

	/* try to find the optional reference to the pmu syscon */
	node = of_parse_phandle(np, "rockchip,pmu", 0);
	if (node) {
		info->regmap_pmu = syscon_node_to_regmap(node);
		if (IS_ERR(info->regmap_pmu))
			return PTR_ERR(info->regmap_pmu);
	}

	/* Special handle for some Socs */
	if (ctrl->soc_data_init) {
		ret = ctrl->soc_data_init(info);
		if (ret)
			return ret;
	}

	ret = rockchip_pinctrl_register(pdev, info);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, info);

	ret = of_platform_populate(np, rockchip_bank_match, NULL, NULL);
	if (ret) {
		dev_err(&pdev->dev, "failed to register gpio device\n");
		return ret;
	}
	dev_info(dev, "probed %s\n", dev_name(dev));

	return 0;
}

static struct rockchip_pin_bank px30_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU
			    ),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT
			    ),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT
			    ),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT
			    ),
};

static struct rockchip_pin_ctrl px30_pin_ctrl = {
		.pin_banks		= px30_pin_banks,
		.nr_banks		= ARRAY_SIZE(px30_pin_banks),
		.label			= "PX30-GPIO",
		.type			= PX30,
		.grf_mux_offset		= 0x0,
		.pmu_mux_offset		= 0x0,
		.iomux_routes		= px30_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(px30_mux_route_data),
		.pull_calc_reg		= px30_calc_pull_reg_and_bit,
		.drv_calc_reg		= px30_calc_drv_reg_and_bit,
		.schmitt_calc_reg	= px30_calc_schmitt_reg_and_bit,
		.slew_rate_calc_reg	= px30_calc_slew_rate_reg_and_bit,
};

static struct rockchip_pin_bank rv1108_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", 0, 0, 0, 0),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", 0, 0, 0, 0),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3", 0, 0, 0, 0),
};

static struct rockchip_pin_ctrl rv1108_pin_ctrl = {
	.pin_banks		= rv1108_pin_banks,
	.nr_banks		= ARRAY_SIZE(rv1108_pin_banks),
	.label			= "RV1108-GPIO",
	.type			= RV1108,
	.grf_mux_offset		= 0x10,
	.pmu_mux_offset		= 0x0,
	.iomux_recalced		= rv1108_mux_recalced_data,
	.niomux_recalced	= ARRAY_SIZE(rv1108_mux_recalced_data),
	.pull_calc_reg		= rv1108_calc_pull_reg_and_bit,
	.drv_calc_reg		= rv1108_calc_drv_reg_and_bit,
	.schmitt_calc_reg	= rv1108_calc_schmitt_reg_and_bit,
};

static struct rockchip_pin_bank rv1126_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0",
			     IOMUX_WIDTH_4BIT | IOMUX_SOURCE_PMU,
			     IOMUX_WIDTH_4BIT | IOMUX_SOURCE_PMU,
			     IOMUX_WIDTH_4BIT | IOMUX_L_SOURCE_PMU,
			     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS_OFFSET(1, 32, "gpio1",
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     0x10010, 0x10018, 0x10020, 0x10028),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2",
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3",
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT,
			     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(4, 2, "gpio4",
			     IOMUX_WIDTH_4BIT, 0, 0, 0),
};

static struct rockchip_pin_ctrl rv1126_pin_ctrl = {
	.pin_banks		= rv1126_pin_banks,
	.nr_banks		= ARRAY_SIZE(rv1126_pin_banks),
	.label			= "RV1126-GPIO",
	.type			= RV1126,
	.grf_mux_offset		= 0x10004, /* mux offset from GPIO0_D0 */
	.pmu_mux_offset		= 0x0,
	.iomux_routes		= rv1126_mux_route_data,
	.niomux_routes		= ARRAY_SIZE(rv1126_mux_route_data),
	.iomux_recalced		= rv1126_mux_recalced_data,
	.niomux_recalced	= ARRAY_SIZE(rv1126_mux_recalced_data),
	.pull_calc_reg		= rv1126_calc_pull_reg_and_bit,
	.drv_calc_reg		= rv1126_calc_drv_reg_and_bit,
	.schmitt_calc_reg	= rv1126_calc_schmitt_reg_and_bit,
};

static struct rockchip_pin_bank rk1808_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(4, 32, "gpio4", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
};

static struct rockchip_pin_ctrl rk1808_pin_ctrl = {
	.pin_banks		= rk1808_pin_banks,
	.nr_banks		= ARRAY_SIZE(rk1808_pin_banks),
	.label			= "RK1808-GPIO",
	.type			= RK1808,
	.iomux_routes		= rk1808_mux_route_data,
	.niomux_routes		= ARRAY_SIZE(rk1808_mux_route_data),
	.grf_mux_offset		= 0x0,
	.pmu_mux_offset		= 0x0,
	.pull_calc_reg		= rk1808_calc_pull_reg_and_bit,
	.drv_calc_reg		= rk1808_calc_drv_reg_and_bit,
	.schmitt_calc_reg	= rk1808_calc_schmitt_reg_and_bit,
	.slew_rate_calc_reg	= rk1808_calc_slew_rate_reg_and_bit,
};

static struct rockchip_pin_bank rk2928_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
};

static struct rockchip_pin_ctrl rk2928_pin_ctrl = {
		.pin_banks		= rk2928_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk2928_pin_banks),
		.label			= "RK2928-GPIO",
		.type			= RK2928,
		.grf_mux_offset		= 0xa8,
		.pull_calc_reg		= rk2928_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3036_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
};

static struct rockchip_pin_ctrl rk3036_pin_ctrl = {
		.pin_banks		= rk3036_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3036_pin_banks),
		.label			= "RK3036-GPIO",
		.type			= RK2928,
		.grf_mux_offset		= 0xa8,
		.pull_calc_reg		= rk2928_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3066a_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
	PIN_BANK(4, 32, "gpio4"),
	PIN_BANK(6, 16, "gpio6"),
};

static struct rockchip_pin_ctrl rk3066a_pin_ctrl = {
		.pin_banks		= rk3066a_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3066a_pin_banks),
		.label			= "RK3066a-GPIO",
		.type			= RK2928,
		.grf_mux_offset		= 0xa8,
		.pull_calc_reg		= rk2928_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3066b_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
};

static struct rockchip_pin_ctrl rk3066b_pin_ctrl = {
		.pin_banks	= rk3066b_pin_banks,
		.nr_banks	= ARRAY_SIZE(rk3066b_pin_banks),
		.label		= "RK3066b-GPIO",
		.type		= RK3066B,
		.grf_mux_offset	= 0x60,
};

static struct rockchip_pin_bank rk3128_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
};

static struct rockchip_pin_ctrl rk3128_pin_ctrl = {
		.pin_banks		= rk3128_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3128_pin_banks),
		.label			= "RK3128-GPIO",
		.type			= RK3128,
		.grf_mux_offset		= 0xa8,
		.iomux_recalced		= rk3128_mux_recalced_data,
		.niomux_recalced	= ARRAY_SIZE(rk3128_mux_recalced_data),
		.iomux_routes		= rk3128_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(rk3128_mux_route_data),
		.pull_calc_reg		= rk3128_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3188_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_GPIO_ONLY, 0, 0, 0),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
};

static struct rockchip_pin_ctrl rk3188_pin_ctrl = {
		.pin_banks		= rk3188_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3188_pin_banks),
		.label			= "RK3188-GPIO",
		.type			= RK3188,
		.grf_mux_offset		= 0x60,
		.pull_calc_reg		= rk3188_calc_pull_reg_and_bit,
};

static struct rockchip_pin_bank rk3228_pin_banks[] = {
	PIN_BANK(0, 32, "gpio0"),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
};

static struct rockchip_pin_ctrl rk3228_pin_ctrl = {
		.pin_banks		= rk3228_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3228_pin_banks),
		.label			= "RK3228-GPIO",
		.type			= RK3288,
		.grf_mux_offset		= 0x0,
		.iomux_routes		= rk3228_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(rk3228_mux_route_data),
		.pull_calc_reg		= rk3228_calc_pull_reg_and_bit,
		.drv_calc_reg		= rk3228_calc_drv_reg_and_bit,
};

static struct rockchip_pin_bank rk3288_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 24, "gpio0", IOMUX_SOURCE_PMU | IOMUX_WRITABLE_32BIT,
					     IOMUX_SOURCE_PMU | IOMUX_WRITABLE_32BIT,
					     IOMUX_SOURCE_PMU | IOMUX_WRITABLE_32BIT,
					     IOMUX_UNROUTED
			    ),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", IOMUX_UNROUTED,
					     IOMUX_UNROUTED,
					     IOMUX_UNROUTED,
					     0
			    ),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", 0, 0, 0, IOMUX_UNROUTED),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3", 0, 0, 0, IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(4, 32, "gpio4", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     0,
					     0
			    ),
	PIN_BANK_IOMUX_FLAGS(5, 32, "gpio5", IOMUX_UNROUTED,
					     0,
					     0,
					     IOMUX_UNROUTED
			    ),
	PIN_BANK_IOMUX_FLAGS(6, 32, "gpio6", 0, 0, 0, IOMUX_UNROUTED),
	PIN_BANK_IOMUX_FLAGS(7, 32, "gpio7", 0,
					     0,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_UNROUTED
			    ),
	PIN_BANK(8, 16, "gpio8"),
};

static struct rockchip_pin_ctrl rk3288_pin_ctrl = {
		.pin_banks		= rk3288_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3288_pin_banks),
		.label			= "RK3288-GPIO",
		.type			= RK3288,
		.grf_mux_offset		= 0x0,
		.pmu_mux_offset		= 0x84,
		.iomux_routes		= rk3288_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(rk3288_mux_route_data),
		.pull_calc_reg		= rk3288_calc_pull_reg_and_bit,
		.drv_calc_reg		= rk3288_calc_drv_reg_and_bit,
};

static struct rockchip_pin_bank rk3308_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3", IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT),
	PIN_BANK_IOMUX_FLAGS(4, 32, "gpio4", IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT,
					     IOMUX_8WIDTH_2BIT),
};

static struct rockchip_pin_ctrl rk3308_pin_ctrl = {
		.pin_banks		= rk3308_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3308_pin_banks),
		.label			= "RK3308-GPIO",
		.type			= RK3308,
		.grf_mux_offset		= 0x0,
		.iomux_recalced		= rk3308_mux_recalced_data,
		.niomux_recalced	= ARRAY_SIZE(rk3308_mux_recalced_data),
		.iomux_routes		= rk3308_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(rk3308_mux_route_data),
		.ctrl_data_re_init	= rk3308b_ctrl_data_re_init,
		.soc_data_init		= rk3308b_soc_data_init,
		.pull_calc_reg		= rk3308_calc_pull_reg_and_bit,
		.drv_calc_reg		= rk3308_calc_drv_reg_and_bit,
		.schmitt_calc_reg	= rk3308_calc_schmitt_reg_and_bit,
};

static struct rockchip_pin_bank rk3328_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", 0, 0, 0, 0),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", 0, 0, 0, 0),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", 0,
			     IOMUX_WIDTH_3BIT,
			     IOMUX_WIDTH_3BIT,
			     0),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3",
			     IOMUX_WIDTH_3BIT,
			     IOMUX_WIDTH_3BIT,
			     0,
			     0),
};

static struct rockchip_pin_ctrl rk3328_pin_ctrl = {
		.pin_banks		= rk3328_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3328_pin_banks),
		.label			= "RK3328-GPIO",
		.type			= RK3288,
		.grf_mux_offset		= 0x0,
		.iomux_recalced		= rk3328_mux_recalced_data,
		.niomux_recalced	= ARRAY_SIZE(rk3328_mux_recalced_data),
		.iomux_routes		= rk3328_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(rk3328_mux_route_data),
		.pull_calc_reg		= rk3228_calc_pull_reg_and_bit,
		.drv_calc_reg		= rk3228_calc_drv_reg_and_bit,
		.schmitt_calc_reg	= rk3328_calc_schmitt_reg_and_bit,
};

static struct rockchip_pin_bank rk3368_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU,
					     IOMUX_SOURCE_PMU
			    ),
	PIN_BANK(1, 32, "gpio1"),
	PIN_BANK(2, 32, "gpio2"),
	PIN_BANK(3, 32, "gpio3"),
};

static struct rockchip_pin_ctrl rk3368_pin_ctrl = {
		.pin_banks		= rk3368_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3368_pin_banks),
		.label			= "RK3368-GPIO",
		.type			= RK3368,
		.grf_mux_offset		= 0x0,
		.pmu_mux_offset		= 0x0,
		.pull_calc_reg		= rk3368_calc_pull_reg_and_bit,
		.drv_calc_reg		= rk3368_calc_drv_reg_and_bit,
};

static struct rockchip_pin_bank rk3399_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS_DRV_FLAGS_OFFSET_PULL_FLAGS(0, 32, "gpio0",
							 IOMUX_SOURCE_PMU,
							 IOMUX_SOURCE_PMU,
							 IOMUX_SOURCE_PMU,
							 IOMUX_SOURCE_PMU,
							 DRV_TYPE_IO_1V8_ONLY,
							 DRV_TYPE_IO_1V8_ONLY,
							 DRV_TYPE_IO_DEFAULT,
							 DRV_TYPE_IO_DEFAULT,
							 0x80,
							 0x88,
							 -1,
							 -1,
							 PULL_TYPE_IO_1V8_ONLY,
							 PULL_TYPE_IO_1V8_ONLY,
							 PULL_TYPE_IO_DEFAULT,
							 PULL_TYPE_IO_DEFAULT
							),
	PIN_BANK_IOMUX_DRV_FLAGS_OFFSET(1, 32, "gpio1", IOMUX_SOURCE_PMU,
					IOMUX_SOURCE_PMU,
					IOMUX_SOURCE_PMU,
					IOMUX_SOURCE_PMU,
					DRV_TYPE_IO_1V8_OR_3V0,
					DRV_TYPE_IO_1V8_OR_3V0,
					DRV_TYPE_IO_1V8_OR_3V0,
					DRV_TYPE_IO_1V8_OR_3V0,
					0xa0,
					0xa8,
					0xb0,
					0xb8
					),
	PIN_BANK_DRV_FLAGS_PULL_FLAGS(2, 32, "gpio2", DRV_TYPE_IO_1V8_OR_3V0,
				      DRV_TYPE_IO_1V8_OR_3V0,
				      DRV_TYPE_IO_1V8_ONLY,
				      DRV_TYPE_IO_1V8_ONLY,
				      PULL_TYPE_IO_DEFAULT,
				      PULL_TYPE_IO_DEFAULT,
				      PULL_TYPE_IO_1V8_ONLY,
				      PULL_TYPE_IO_1V8_ONLY
				      ),
	PIN_BANK_DRV_FLAGS(3, 32, "gpio3", DRV_TYPE_IO_3V3_ONLY,
			   DRV_TYPE_IO_3V3_ONLY,
			   DRV_TYPE_IO_3V3_ONLY,
			   DRV_TYPE_IO_1V8_OR_3V0
			   ),
	PIN_BANK_DRV_FLAGS(4, 32, "gpio4", DRV_TYPE_IO_1V8_OR_3V0,
			   DRV_TYPE_IO_1V8_3V0_AUTO,
			   DRV_TYPE_IO_1V8_OR_3V0,
			   DRV_TYPE_IO_1V8_OR_3V0
			   ),
};

static struct rockchip_pin_ctrl rk3399_pin_ctrl = {
		.pin_banks		= rk3399_pin_banks,
		.nr_banks		= ARRAY_SIZE(rk3399_pin_banks),
		.label			= "RK3399-GPIO",
		.type			= RK3399,
		.grf_mux_offset		= 0xe000,
		.pmu_mux_offset		= 0x0,
		.grf_drv_offset		= 0xe100,
		.pmu_drv_offset		= 0x80,
		.iomux_routes		= rk3399_mux_route_data,
		.niomux_routes		= ARRAY_SIZE(rk3399_mux_route_data),
		.pull_calc_reg		= rk3399_calc_pull_reg_and_bit,
		.drv_calc_reg		= rk3399_calc_drv_reg_and_bit,
};

static struct rockchip_pin_bank rk3568_pin_banks[] = {
	PIN_BANK_IOMUX_FLAGS(0, 32, "gpio0", IOMUX_SOURCE_PMU | IOMUX_WIDTH_4BIT,
					     IOMUX_SOURCE_PMU | IOMUX_WIDTH_4BIT,
					     IOMUX_SOURCE_PMU | IOMUX_WIDTH_4BIT,
					     IOMUX_SOURCE_PMU | IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(1, 32, "gpio1", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(2, 32, "gpio2", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(3, 32, "gpio3", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
	PIN_BANK_IOMUX_FLAGS(4, 32, "gpio4", IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT,
					     IOMUX_WIDTH_4BIT),
};

static struct rockchip_pin_ctrl rk3568_pin_ctrl = {
	.pin_banks		= rk3568_pin_banks,
	.nr_banks		= ARRAY_SIZE(rk3568_pin_banks),
	.label			= "RK3568-GPIO",
	.type			= RK3568,
	.grf_mux_offset		= 0x0,
	.pmu_mux_offset		= 0x0,
	.grf_drv_offset		= 0x0200,
	.pmu_drv_offset		= 0x0070,
	.iomux_routes		= rk3568_mux_route_data,
	.niomux_routes		= ARRAY_SIZE(rk3568_mux_route_data),
	.pull_calc_reg		= rk3568_calc_pull_reg_and_bit,
	.drv_calc_reg		= rk3568_calc_drv_reg_and_bit,
	.slew_rate_calc_reg	= rk3568_calc_slew_rate_reg_and_bit,
	.schmitt_calc_reg	= rk3568_calc_schmitt_reg_and_bit,
};

static const struct of_device_id rockchip_pinctrl_dt_match[] = {
	{ .compatible = "rockchip,px30-pinctrl",
		.data = &px30_pin_ctrl },
	{ .compatible = "rockchip,rv1108-pinctrl",
		.data = &rv1108_pin_ctrl },
	{ .compatible = "rockchip,rv1126-pinctrl",
		.data = &rv1126_pin_ctrl },
	{ .compatible = "rockchip,rk1808-pinctrl",
		.data = &rk1808_pin_ctrl },
	{ .compatible = "rockchip,rk2928-pinctrl",
		.data = &rk2928_pin_ctrl },
	{ .compatible = "rockchip,rk3036-pinctrl",
		.data = &rk3036_pin_ctrl },
	{ .compatible = "rockchip,rk3066a-pinctrl",
		.data = &rk3066a_pin_ctrl },
	{ .compatible = "rockchip,rk3066b-pinctrl",
		.data = &rk3066b_pin_ctrl },
	{ .compatible = "rockchip,rk3128-pinctrl",
		.data = (void *)&rk3128_pin_ctrl },
	{ .compatible = "rockchip,rk3188-pinctrl",
		.data = &rk3188_pin_ctrl },
	{ .compatible = "rockchip,rk3228-pinctrl",
		.data = &rk3228_pin_ctrl },
	{ .compatible = "rockchip,rk3288-pinctrl",
		.data = &rk3288_pin_ctrl },
	{ .compatible = "rockchip,rk3308-pinctrl",
		.data = &rk3308_pin_ctrl },
	{ .compatible = "rockchip,rk3328-pinctrl",
		.data = &rk3328_pin_ctrl },
	{ .compatible = "rockchip,rk3368-pinctrl",
		.data = &rk3368_pin_ctrl },
	{ .compatible = "rockchip,rk3399-pinctrl",
		.data = &rk3399_pin_ctrl },
	{ .compatible = "rockchip,rk3568-pinctrl",
		.data = &rk3568_pin_ctrl },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_pinctrl_dt_match);

static struct platform_driver rockchip_pinctrl_driver = {
	.probe		= rockchip_pinctrl_probe,
	.driver = {
		.name	= "rockchip-pinctrl",
		.pm = &rockchip_pinctrl_dev_pm_ops,
		.of_match_table = rockchip_pinctrl_dt_match,
	},
};

static int __init rockchip_pinctrl_drv_register(void)
{
	return platform_driver_register(&rockchip_pinctrl_driver);
}
postcore_initcall(rockchip_pinctrl_drv_register);

MODULE_DESCRIPTION("ROCKCHIP Pin Controller Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pinctrl-rockchip");
