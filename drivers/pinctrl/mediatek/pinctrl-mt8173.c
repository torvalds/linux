/*
 * Copyright (c) 2014-2015 MediaTek Inc.
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>
#include <dt-bindings/pinctrl/mt65xx.h>

#include "pinctrl-mtk-common.h"
#include "pinctrl-mtk-mt8173.h"

#define DRV_BASE				0xb00

/**
 * struct mtk_pin_ies_smt_set - For special pins' ies and smt setting.
 * @start: The start pin number of those special pins.
 * @end: The end pin number of those special pins.
 * @offset: The offset of special setting register.
 * @bit: The bit of special setting register.
 */
struct mtk_pin_ies_smt_set {
	unsigned int start;
	unsigned int end;
	unsigned int offset;
	unsigned char bit;
};

#define MTK_PIN_IES_SMT_SET(_start, _end, _offset, _bit)	\
	{	\
		.start = _start,	\
		.end = _end,	\
		.bit = _bit,	\
		.offset = _offset,	\
	}

/**
 * struct mtk_pin_spec_pupd_set - For special pins' pull up/down setting.
 * @pin: The pin number.
 * @offset: The offset of special pull up/down setting register.
 * @pupd_bit: The pull up/down bit in this register.
 * @r0_bit: The r0 bit of pull resistor.
 * @r1_bit: The r1 bit of pull resistor.
 */
struct mtk_pin_spec_pupd_set {
	unsigned int pin;
	unsigned int offset;
	unsigned char pupd_bit;
	unsigned char r1_bit;
	unsigned char r0_bit;
};

#define MTK_PIN_PUPD_SPEC(_pin, _offset, _pupd, _r1, _r0)	\
	{	\
		.pin = _pin,	\
		.offset = _offset,	\
		.pupd_bit = _pupd,	\
		.r1_bit = _r1,		\
		.r0_bit = _r0,		\
	}

static const struct mtk_pin_spec_pupd_set mt8173_spec_pupd[] = {
	MTK_PIN_PUPD_SPEC(119, 0xe00, 2, 1, 0),  /* KROW0 */
	MTK_PIN_PUPD_SPEC(120, 0xe00, 6, 5, 4),  /* KROW1 */
	MTK_PIN_PUPD_SPEC(121, 0xe00, 10, 9, 8), /* KROW2 */
	MTK_PIN_PUPD_SPEC(122, 0xe10, 2, 1, 0),  /* KCOL0 */
	MTK_PIN_PUPD_SPEC(123, 0xe10, 6, 5, 4),  /* KCOL1 */
	MTK_PIN_PUPD_SPEC(124, 0xe10, 10, 9, 8), /* KCOL2 */

	MTK_PIN_PUPD_SPEC(67, 0xd10, 2, 1, 0),   /* ms0 DS */
	MTK_PIN_PUPD_SPEC(68, 0xd00, 2, 1, 0),   /* ms0 RST */
	MTK_PIN_PUPD_SPEC(66, 0xc10, 2, 1, 0),   /* ms0 cmd */
	MTK_PIN_PUPD_SPEC(65, 0xc00, 2, 1, 0),   /* ms0 clk */
	MTK_PIN_PUPD_SPEC(57, 0xc20, 2, 1, 0),   /* ms0 data0 */
	MTK_PIN_PUPD_SPEC(58, 0xc20, 2, 1, 0),   /* ms0 data1 */
	MTK_PIN_PUPD_SPEC(59, 0xc20, 2, 1, 0),   /* ms0 data2 */
	MTK_PIN_PUPD_SPEC(60, 0xc20, 2, 1, 0),   /* ms0 data3 */
	MTK_PIN_PUPD_SPEC(61, 0xc20, 2, 1, 0),   /* ms0 data4 */
	MTK_PIN_PUPD_SPEC(62, 0xc20, 2, 1, 0),   /* ms0 data5 */
	MTK_PIN_PUPD_SPEC(63, 0xc20, 2, 1, 0),   /* ms0 data6 */
	MTK_PIN_PUPD_SPEC(64, 0xc20, 2, 1, 0),   /* ms0 data7 */

	MTK_PIN_PUPD_SPEC(78, 0xc50, 2, 1, 0),    /* ms1 cmd */
	MTK_PIN_PUPD_SPEC(73, 0xd20, 2, 1, 0),    /* ms1 dat0 */
	MTK_PIN_PUPD_SPEC(74, 0xd20, 6, 5, 4),    /* ms1 dat1 */
	MTK_PIN_PUPD_SPEC(75, 0xd20, 10, 9, 8),   /* ms1 dat2 */
	MTK_PIN_PUPD_SPEC(76, 0xd20, 14, 13, 12), /* ms1 dat3 */
	MTK_PIN_PUPD_SPEC(77, 0xc40, 2, 1, 0),    /* ms1 clk */

	MTK_PIN_PUPD_SPEC(100, 0xd40, 2, 1, 0),    /* ms2 dat0 */
	MTK_PIN_PUPD_SPEC(101, 0xd40, 6, 5, 4),    /* ms2 dat1 */
	MTK_PIN_PUPD_SPEC(102, 0xd40, 10, 9, 8),   /* ms2 dat2 */
	MTK_PIN_PUPD_SPEC(103, 0xd40, 14, 13, 12), /* ms2 dat3 */
	MTK_PIN_PUPD_SPEC(104, 0xc80, 2, 1, 0),    /* ms2 clk */
	MTK_PIN_PUPD_SPEC(105, 0xc90, 2, 1, 0),    /* ms2 cmd */

	MTK_PIN_PUPD_SPEC(22, 0xd60, 2, 1, 0),    /* ms3 dat0 */
	MTK_PIN_PUPD_SPEC(23, 0xd60, 6, 5, 4),    /* ms3 dat1 */
	MTK_PIN_PUPD_SPEC(24, 0xd60, 10, 9, 8),   /* ms3 dat2 */
	MTK_PIN_PUPD_SPEC(25, 0xd60, 14, 13, 12), /* ms3 dat3 */
	MTK_PIN_PUPD_SPEC(26, 0xcc0, 2, 1, 0),    /* ms3 clk */
	MTK_PIN_PUPD_SPEC(27, 0xcd0, 2, 1, 0)     /* ms3 cmd */
};

static int spec_pull_set(struct regmap *regmap, unsigned int pin,
		unsigned char align, bool isup, unsigned int r1r0)
{
	unsigned int i;
	unsigned int reg_pupd, reg_set, reg_rst;
	unsigned int bit_pupd, bit_r0, bit_r1;
	const struct mtk_pin_spec_pupd_set *spec_pupd_pin;
	bool find = false;

	for (i = 0; i < ARRAY_SIZE(mt8173_spec_pupd); i++) {
		if (pin == mt8173_spec_pupd[i].pin) {
			find = true;
			break;
		}
	}

	if (!find)
		return -EINVAL;

	spec_pupd_pin = mt8173_spec_pupd + i;
	reg_set = spec_pupd_pin->offset + align;
	reg_rst = spec_pupd_pin->offset + (align << 1);

	if (isup)
		reg_pupd = reg_rst;
	else
		reg_pupd = reg_set;

	bit_pupd = BIT(spec_pupd_pin->pupd_bit);
	regmap_write(regmap, reg_pupd, bit_pupd);

	bit_r0 = BIT(spec_pupd_pin->r0_bit);
	bit_r1 = BIT(spec_pupd_pin->r1_bit);

	switch (r1r0) {
	case MTK_PUPD_SET_R1R0_00:
		regmap_write(regmap, reg_rst, bit_r0);
		regmap_write(regmap, reg_rst, bit_r1);
		break;
	case MTK_PUPD_SET_R1R0_01:
		regmap_write(regmap, reg_set, bit_r0);
		regmap_write(regmap, reg_rst, bit_r1);
		break;
	case MTK_PUPD_SET_R1R0_10:
		regmap_write(regmap, reg_rst, bit_r0);
		regmap_write(regmap, reg_set, bit_r1);
		break;
	case MTK_PUPD_SET_R1R0_11:
		regmap_write(regmap, reg_set, bit_r0);
		regmap_write(regmap, reg_set, bit_r1);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct mtk_pin_ies_smt_set mt8173_ies_smt_set[] = {
	MTK_PIN_IES_SMT_SET(0, 4, 0x930, 1),
	MTK_PIN_IES_SMT_SET(5, 9, 0x930, 2),
	MTK_PIN_IES_SMT_SET(10, 13, 0x930, 10),
	MTK_PIN_IES_SMT_SET(14, 15, 0x940, 10),
	MTK_PIN_IES_SMT_SET(16, 16, 0x930, 0),
	MTK_PIN_IES_SMT_SET(17, 17, 0x950, 2),
	MTK_PIN_IES_SMT_SET(18, 21, 0x940, 3),
	MTK_PIN_IES_SMT_SET(29, 32, 0x930, 3),
	MTK_PIN_IES_SMT_SET(33, 33, 0x930, 4),
	MTK_PIN_IES_SMT_SET(34, 36, 0x930, 5),
	MTK_PIN_IES_SMT_SET(37, 38, 0x930, 6),
	MTK_PIN_IES_SMT_SET(39, 39, 0x930, 7),
	MTK_PIN_IES_SMT_SET(40, 41, 0x930, 9),
	MTK_PIN_IES_SMT_SET(42, 42, 0x940, 0),
	MTK_PIN_IES_SMT_SET(43, 44, 0x930, 11),
	MTK_PIN_IES_SMT_SET(45, 46, 0x930, 12),
	MTK_PIN_IES_SMT_SET(57, 64, 0xc20, 13),
	MTK_PIN_IES_SMT_SET(65, 65, 0xc10, 13),
	MTK_PIN_IES_SMT_SET(66, 66, 0xc00, 13),
	MTK_PIN_IES_SMT_SET(67, 67, 0xd10, 13),
	MTK_PIN_IES_SMT_SET(68, 68, 0xd00, 13),
	MTK_PIN_IES_SMT_SET(69, 72, 0x940, 14),
	MTK_PIN_IES_SMT_SET(73, 76, 0xc60, 13),
	MTK_PIN_IES_SMT_SET(77, 77, 0xc40, 13),
	MTK_PIN_IES_SMT_SET(78, 78, 0xc50, 13),
	MTK_PIN_IES_SMT_SET(79, 82, 0x940, 15),
	MTK_PIN_IES_SMT_SET(83, 83, 0x950, 0),
	MTK_PIN_IES_SMT_SET(84, 85, 0x950, 1),
	MTK_PIN_IES_SMT_SET(86, 91, 0x950, 2),
	MTK_PIN_IES_SMT_SET(92, 92, 0x930, 13),
	MTK_PIN_IES_SMT_SET(93, 95, 0x930, 14),
	MTK_PIN_IES_SMT_SET(96, 99, 0x930, 15),
	MTK_PIN_IES_SMT_SET(100, 103, 0xca0, 13),
	MTK_PIN_IES_SMT_SET(104, 104, 0xc80, 13),
	MTK_PIN_IES_SMT_SET(105, 105, 0xc90, 13),
	MTK_PIN_IES_SMT_SET(106, 107, 0x940, 4),
	MTK_PIN_IES_SMT_SET(108, 112, 0x940, 1),
	MTK_PIN_IES_SMT_SET(113, 116, 0x940, 2),
	MTK_PIN_IES_SMT_SET(117, 118, 0x940, 5),
	MTK_PIN_IES_SMT_SET(119, 124, 0x940, 6),
	MTK_PIN_IES_SMT_SET(125, 126, 0x940, 7),
	MTK_PIN_IES_SMT_SET(127, 127, 0x940, 0),
	MTK_PIN_IES_SMT_SET(128, 128, 0x950, 8),
	MTK_PIN_IES_SMT_SET(129, 130, 0x950, 9),
	MTK_PIN_IES_SMT_SET(131, 132, 0x950, 8),
	MTK_PIN_IES_SMT_SET(133, 134, 0x910, 8)
};

static int spec_ies_smt_set(struct regmap *regmap, unsigned int pin,
		unsigned char align, int value)
{
	unsigned int i, reg_addr, bit;
	bool find = false;

	for (i = 0; i < ARRAY_SIZE(mt8173_ies_smt_set); i++) {
		if (pin >= mt8173_ies_smt_set[i].start &&
				pin <= mt8173_ies_smt_set[i].end) {
			find = true;
			break;
		}
	}

	if (!find)
		return -EINVAL;

	if (value)
		reg_addr = mt8173_ies_smt_set[i].offset + align;
	else
		reg_addr = mt8173_ies_smt_set[i].offset + (align << 1);

	bit = BIT(mt8173_ies_smt_set[i].bit);
	regmap_write(regmap, reg_addr, bit);
	return 0;
}

static const struct mtk_drv_group_desc mt8173_drv_grp[] =  {
	/* 0E4E8SR 4/8/12/16 */
	MTK_DRV_GRP(4, 16, 1, 2, 4),
	/* 0E2E4SR  2/4/6/8 */
	MTK_DRV_GRP(2, 8, 1, 2, 2),
	/* E8E4E2  2/4/6/8/10/12/14/16 */
	MTK_DRV_GRP(2, 16, 0, 2, 2)
};

static const struct mtk_pin_drv_grp mt8173_pin_drv[] = {
	MTK_PIN_DRV_GRP(0, DRV_BASE+0x20, 12, 0),
	MTK_PIN_DRV_GRP(1, DRV_BASE+0x20, 12, 0),
	MTK_PIN_DRV_GRP(2, DRV_BASE+0x20, 12, 0),
	MTK_PIN_DRV_GRP(3, DRV_BASE+0x20, 12, 0),
	MTK_PIN_DRV_GRP(4, DRV_BASE+0x20, 12, 0),
	MTK_PIN_DRV_GRP(5, DRV_BASE+0x30, 0, 0),
	MTK_PIN_DRV_GRP(6, DRV_BASE+0x30, 0, 0),
	MTK_PIN_DRV_GRP(7, DRV_BASE+0x30, 0, 0),
	MTK_PIN_DRV_GRP(8, DRV_BASE+0x30, 0, 0),
	MTK_PIN_DRV_GRP(9, DRV_BASE+0x30, 0, 0),
	MTK_PIN_DRV_GRP(10, DRV_BASE+0x30, 4, 1),
	MTK_PIN_DRV_GRP(11, DRV_BASE+0x30, 4, 1),
	MTK_PIN_DRV_GRP(12, DRV_BASE+0x30, 4, 1),
	MTK_PIN_DRV_GRP(13, DRV_BASE+0x30, 4, 1),
	MTK_PIN_DRV_GRP(14, DRV_BASE+0x40, 8, 1),
	MTK_PIN_DRV_GRP(15, DRV_BASE+0x40, 8, 1),
	MTK_PIN_DRV_GRP(16, DRV_BASE, 8, 1),
	MTK_PIN_DRV_GRP(17, 0xce0, 8, 2),
	MTK_PIN_DRV_GRP(22, 0xce0, 8, 2),
	MTK_PIN_DRV_GRP(23, 0xce0, 8, 2),
	MTK_PIN_DRV_GRP(24, 0xce0, 8, 2),
	MTK_PIN_DRV_GRP(25, 0xce0, 8, 2),
	MTK_PIN_DRV_GRP(26, 0xcc0, 8, 2),
	MTK_PIN_DRV_GRP(27, 0xcd0, 8, 2),
	MTK_PIN_DRV_GRP(28, 0xd70, 8, 2),
	MTK_PIN_DRV_GRP(29, DRV_BASE+0x80, 12, 1),
	MTK_PIN_DRV_GRP(30, DRV_BASE+0x80, 12, 1),
	MTK_PIN_DRV_GRP(31, DRV_BASE+0x80, 12, 1),
	MTK_PIN_DRV_GRP(32, DRV_BASE+0x80, 12, 1),
	MTK_PIN_DRV_GRP(33, DRV_BASE+0x10, 12, 1),
	MTK_PIN_DRV_GRP(34, DRV_BASE+0x10, 8, 1),
	MTK_PIN_DRV_GRP(35, DRV_BASE+0x10, 8, 1),
	MTK_PIN_DRV_GRP(36, DRV_BASE+0x10, 8, 1),
	MTK_PIN_DRV_GRP(37, DRV_BASE+0x10, 4, 1),
	MTK_PIN_DRV_GRP(38, DRV_BASE+0x10, 4, 1),
	MTK_PIN_DRV_GRP(39, DRV_BASE+0x20, 0, 0),
	MTK_PIN_DRV_GRP(40, DRV_BASE+0x20, 8, 0),
	MTK_PIN_DRV_GRP(41, DRV_BASE+0x20, 8, 0),
	MTK_PIN_DRV_GRP(42, DRV_BASE+0x50, 8, 1),
	MTK_PIN_DRV_GRP(57, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(58, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(59, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(60, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(61, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(62, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(63, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(64, 0xc20, 8, 2),
	MTK_PIN_DRV_GRP(65, 0xc00, 8, 2),
	MTK_PIN_DRV_GRP(66, 0xc10, 8, 2),
	MTK_PIN_DRV_GRP(67, 0xd10, 8, 2),
	MTK_PIN_DRV_GRP(68, 0xd00, 8, 2),
	MTK_PIN_DRV_GRP(69, DRV_BASE+0x80, 0, 1),
	MTK_PIN_DRV_GRP(70, DRV_BASE+0x80, 0, 1),
	MTK_PIN_DRV_GRP(71, DRV_BASE+0x80, 0, 1),
	MTK_PIN_DRV_GRP(72, DRV_BASE+0x80, 0, 1),
	MTK_PIN_DRV_GRP(73, 0xc60, 8, 2),
	MTK_PIN_DRV_GRP(74, 0xc60, 8, 2),
	MTK_PIN_DRV_GRP(75, 0xc60, 8, 2),
	MTK_PIN_DRV_GRP(76, 0xc60, 8, 2),
	MTK_PIN_DRV_GRP(77, 0xc40, 8, 2),
	MTK_PIN_DRV_GRP(78, 0xc50, 8, 2),
	MTK_PIN_DRV_GRP(79, DRV_BASE+0x70, 12, 1),
	MTK_PIN_DRV_GRP(80, DRV_BASE+0x70, 12, 1),
	MTK_PIN_DRV_GRP(81, DRV_BASE+0x70, 12, 1),
	MTK_PIN_DRV_GRP(82, DRV_BASE+0x70, 12, 1),
	MTK_PIN_DRV_GRP(83, DRV_BASE, 4, 1),
	MTK_PIN_DRV_GRP(84, DRV_BASE, 0, 1),
	MTK_PIN_DRV_GRP(85, DRV_BASE, 0, 1),
	MTK_PIN_DRV_GRP(85, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(86, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(87, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(88, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(89, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(90, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(91, DRV_BASE+0x60, 8, 1),
	MTK_PIN_DRV_GRP(92, DRV_BASE+0x60, 4, 0),
	MTK_PIN_DRV_GRP(93, DRV_BASE+0x60, 0, 0),
	MTK_PIN_DRV_GRP(94, DRV_BASE+0x60, 0, 0),
	MTK_PIN_DRV_GRP(95, DRV_BASE+0x60, 0, 0),
	MTK_PIN_DRV_GRP(96, DRV_BASE+0x80, 8, 1),
	MTK_PIN_DRV_GRP(97, DRV_BASE+0x80, 8, 1),
	MTK_PIN_DRV_GRP(98, DRV_BASE+0x80, 8, 1),
	MTK_PIN_DRV_GRP(99, DRV_BASE+0x80, 8, 1),
	MTK_PIN_DRV_GRP(100, 0xca0, 8, 2),
	MTK_PIN_DRV_GRP(101, 0xca0, 8, 2),
	MTK_PIN_DRV_GRP(102, 0xca0, 8, 2),
	MTK_PIN_DRV_GRP(103, 0xca0, 8, 2),
	MTK_PIN_DRV_GRP(104, 0xc80, 8, 2),
	MTK_PIN_DRV_GRP(105, 0xc90, 8, 2),
	MTK_PIN_DRV_GRP(108, DRV_BASE+0x50, 0, 1),
	MTK_PIN_DRV_GRP(109, DRV_BASE+0x50, 0, 1),
	MTK_PIN_DRV_GRP(110, DRV_BASE+0x50, 0, 1),
	MTK_PIN_DRV_GRP(111, DRV_BASE+0x50, 0, 1),
	MTK_PIN_DRV_GRP(112, DRV_BASE+0x50, 0, 1),
	MTK_PIN_DRV_GRP(113, DRV_BASE+0x80, 4, 1),
	MTK_PIN_DRV_GRP(114, DRV_BASE+0x80, 4, 1),
	MTK_PIN_DRV_GRP(115, DRV_BASE+0x80, 4, 1),
	MTK_PIN_DRV_GRP(116, DRV_BASE+0x80, 4, 1),
	MTK_PIN_DRV_GRP(117, DRV_BASE+0x90, 0, 1),
	MTK_PIN_DRV_GRP(118, DRV_BASE+0x90, 0, 1),
	MTK_PIN_DRV_GRP(119, DRV_BASE+0x50, 4, 1),
	MTK_PIN_DRV_GRP(120, DRV_BASE+0x50, 4, 1),
	MTK_PIN_DRV_GRP(121, DRV_BASE+0x50, 4, 1),
	MTK_PIN_DRV_GRP(122, DRV_BASE+0x50, 4, 1),
	MTK_PIN_DRV_GRP(123, DRV_BASE+0x50, 4, 1),
	MTK_PIN_DRV_GRP(124, DRV_BASE+0x50, 4, 1),
	MTK_PIN_DRV_GRP(125, DRV_BASE+0x30, 12, 1),
	MTK_PIN_DRV_GRP(126, DRV_BASE+0x30, 12, 1),
	MTK_PIN_DRV_GRP(127, DRV_BASE+0x50, 8, 1),
	MTK_PIN_DRV_GRP(128, DRV_BASE+0x40, 0, 1),
	MTK_PIN_DRV_GRP(129, DRV_BASE+0x40, 0, 1),
	MTK_PIN_DRV_GRP(130, DRV_BASE+0x40, 0, 1),
	MTK_PIN_DRV_GRP(131, DRV_BASE+0x40, 0, 1),
	MTK_PIN_DRV_GRP(132, DRV_BASE+0x40, 0, 1)
};

static const struct mtk_pinctrl_devdata mt8173_pinctrl_data = {
	.pins = mtk_pins_mt8173,
	.npins = ARRAY_SIZE(mtk_pins_mt8173),
	.grp_desc = mt8173_drv_grp,
	.n_grp_cls = ARRAY_SIZE(mt8173_drv_grp),
	.pin_drv_grp = mt8173_pin_drv,
	.n_pin_drv_grps = ARRAY_SIZE(mt8173_pin_drv),
	.spec_pull_set = spec_pull_set,
	.spec_ies_smt_set = spec_ies_smt_set,
	.dir_offset = 0x0000,
	.pullen_offset = 0x0100,
	.pullsel_offset = 0x0200,
	.dout_offset = 0x0400,
	.din_offset = 0x0500,
	.pinmux_offset = 0x0600,
	.type1_start = 135,
	.type1_end = 135,
	.port_shf = 4,
	.port_mask = 0xf,
	.port_align = 4,
	.eint_offsets = {
		.name = "mt8173_eint",
		.stat      = 0x000,
		.ack       = 0x040,
		.mask      = 0x080,
		.mask_set  = 0x0c0,
		.mask_clr  = 0x100,
		.sens      = 0x140,
		.sens_set  = 0x180,
		.sens_clr  = 0x1c0,
		.soft      = 0x200,
		.soft_set  = 0x240,
		.soft_clr  = 0x280,
		.pol       = 0x300,
		.pol_set   = 0x340,
		.pol_clr   = 0x380,
		.dom_en    = 0x400,
		.dbnc_ctrl = 0x500,
		.dbnc_set  = 0x600,
		.dbnc_clr  = 0x700,
		.port_mask = 7,
		.ports     = 6,
	},
	.ap_num = 224,
	.db_cnt = 16,
};

static int mt8173_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_pctrl_init(pdev, &mt8173_pinctrl_data);
}

static const struct of_device_id mt8173_pctrl_match[] = {
	{ .compatible = "mediatek,mt8173-pinctrl", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8173_pctrl_match);

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mt8173_pinctrl_probe,
	.driver = {
		.name = "mediatek-mt8173-pinctrl",
		.of_match_table = mt8173_pctrl_match,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}

module_init(mtk_pinctrl_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek Pinctrl Driver");
MODULE_AUTHOR("Hongzhou Yang <hongzhou.yang@mediatek.com>");
