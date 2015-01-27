/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Hongzhou.Yang <hongzhou.yang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PINCTRL_MTK_COMMON_H
#define __PINCTRL_MTK_COMMON_H

#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>

#define NO_EINT_SUPPORT    255
#define MTK_CHIP_TYPE_BASE     0
#define MTK_CHIP_TYPE_PMIC     1
#define MT_EDGE_SENSITIVE           0
#define MT_LEVEL_SENSITIVE          1
#define EINT_DBNC_SET_DBNC_BITS     4
#define EINT_DBNC_RST_BIT           (0x1 << 1)
#define EINT_DBNC_SET_EN            (0x1 << 0)

struct mtk_desc_function {
	const char *name;
	unsigned char muxval;
};

struct mtk_desc_eint {
	unsigned char eintmux;
	unsigned char eintnum;
};

struct mtk_desc_pin {
	struct pinctrl_pin_desc	pin;
	const char *chip;
	const struct mtk_desc_eint eint;
	const struct mtk_desc_function	*functions;
};

#define MTK_PIN(_pin, _pad, _chip, _eint, ...)		\
	{							\
		.pin = _pin,					\
		.chip = _chip,					\
		.eint = _eint,					\
		.functions = (struct mtk_desc_function[]){	\
			__VA_ARGS__, { } },			\
	}

#define MTK_EINT_FUNCTION(_eintmux, _eintnum)				\
	{							\
		.eintmux = _eintmux,					\
		.eintnum = _eintnum,					\
	}

#define MTK_FUNCTION(_val, _name)				\
	{							\
		.muxval = _val,					\
		.name = _name,					\
	}

#define SET_ADDR(x, y)  (x + (y->devdata->port_align))
#define CLR_ADDR(x, y)  (x + (y->devdata->port_align << 1))

struct mtk_pinctrl_group {
	const char	*name;
	unsigned long	config;
	unsigned	pin;
};

/**
 * struct mtk_drv_group_desc - Provide driving group data.
 * @max_drv: The maximum current of this group.
 * @min_drv: The minimum current of this group.
 * @low_bit: The lowest bit of this group.
 * @high_bit: The highest bit of this group.
 * @step: The step current of this group.
 */
struct mtk_drv_group_desc {
	unsigned char min_drv;
	unsigned char max_drv;
	unsigned char low_bit;
	unsigned char high_bit;
	unsigned char step;
};

#define MTK_DRV_GRP(_min, _max, _low, _high, _step)	\
	{	\
		.min_drv = _min,	\
		.max_drv = _max,	\
		.low_bit = _low,	\
		.high_bit = _high,	\
		.step = _step,		\
	}

/**
 * struct mtk_pin_drv_grp - Provide each pin driving info.
 * @pin: The pin number.
 * @offset: The offset of driving register for this pin.
 * @bit: The bit of driving register for this pin.
 * @grp: The group for this pin belongs to.
 */
struct mtk_pin_drv_grp {
	unsigned int pin;
	unsigned int offset;
	unsigned char bit;
	unsigned char grp;
};

#define MTK_PIN_DRV_GRP(_pin, _offset, _bit, _grp)	\
	{	\
		.pin = _pin,	\
		.offset = _offset,	\
		.bit = _bit,	\
		.grp = _grp,	\
	}

struct mtk_eint_offsets {
	const char *name;
	unsigned int  stat;
	unsigned int  ack;
	unsigned int  mask;
	unsigned int  mask_set;
	unsigned int  mask_clr;
	unsigned int  sens;
	unsigned int  sens_set;
	unsigned int  sens_clr;
	unsigned int  soft;
	unsigned int  soft_set;
	unsigned int  soft_clr;
	unsigned int  pol;
	unsigned int  pol_set;
	unsigned int  pol_clr;
	unsigned int  dom_en;
	unsigned int  dbnc_ctrl;
	unsigned int  dbnc_set;
	unsigned int  dbnc_clr;
	u8  port_mask;
	u8  ports;
};

/**
 * struct mtk_pinctrl_devdata - Provide HW GPIO related data.
 * @pins: An array describing all pins the pin controller affects.
 * @npins: The number of entries in @pins.
 *
 * @grp_desc: The driving group info.
 * @pin_drv_grp: The driving group for all pins.
 * @spec_pull_set: Each SoC may have special pins for pull up/down setting,
 *  these pins' pull setting are very different, they have separate pull
 *  up/down bit, R0 and R1 resistor bit, so they need special pull setting.
 *  If special setting is success, this should return 0, otherwise it should
 *  return non-zero value.
 * @spec_ies_smt_set: Some pins are irregular, their input enable and smt
 * control register are discontinuous, but they are mapping together. That
 * means when user set smt, input enable is set at the same time. So they
 * also need special control. If special control is success, this should
 * return 0, otherwise return non-zero value.
 *
 * @dir_offset: The direction register offset.
 * @pullen_offset: The pull-up/pull-down enable register offset.
 * @pinmux_offset: The pinmux register offset.
 *
 * @type1_start: Some chips have two base addresses for pull select register,
 *  that means some pins use the first address and others use the second. This
 *  member record the start of pin number to use the second address.
 * @type1_end: The end of pin number to use the second address.
 *
 * @port_shf: The shift between two registers.
 * @port_mask: The mask of register.
 * @port_align: Provide clear register and set register step.
 */
struct mtk_pinctrl_devdata {
	const struct mtk_desc_pin	*pins;
	unsigned int				npins;
	const struct mtk_drv_group_desc	*grp_desc;
	unsigned int	n_grp_cls;
	const struct mtk_pin_drv_grp	*pin_drv_grp;
	unsigned int	n_pin_drv_grps;
	int (*spec_pull_set)(struct regmap *reg, unsigned int pin,
			unsigned char align, bool isup, unsigned int arg);
	int (*spec_ies_smt_set)(struct regmap *reg, unsigned int pin,
			unsigned char align, int value);
	unsigned int dir_offset;
	unsigned int ies_offset;
	unsigned int smt_offset;
	unsigned int pullen_offset;
	unsigned int pullsel_offset;
	unsigned int drv_offset;
	unsigned int invser_offset;
	unsigned int dout_offset;
	unsigned int din_offset;
	unsigned int pinmux_offset;
	unsigned short type1_start;
	unsigned short type1_end;
	unsigned char  port_shf;
	unsigned char  port_mask;
	unsigned char  port_align;
	unsigned char	chip_type;
	struct mtk_eint_offsets eint_offsets;
	unsigned int	ap_num;
	unsigned int	db_cnt;
};

struct mtk_pinctrl {
	struct regmap	*regmap1;
	struct regmap	*regmap2;
	struct device           *dev;
	struct gpio_chip	*chip;
	struct mtk_pinctrl_group	*groups;
	unsigned			ngroups;
	const char          **grp_names;
	struct pinctrl_dev      *pctl_dev;
	const struct mtk_pinctrl_devdata  *devdata;
	void __iomem		*eint_reg_base;
	struct irq_domain	*domain;
	int			*eint_dual_edges;
};

int mtk_pctrl_init(struct platform_device *pdev,
		const struct mtk_pinctrl_devdata *data);

#endif /* __PINCTRL_MTK_COMMON_H */
