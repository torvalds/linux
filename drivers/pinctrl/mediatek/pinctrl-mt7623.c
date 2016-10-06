/*
 * Copyright (c) 2016 John Crispin <blogic@openwrt.org>
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

#include <dt-bindings/pinctrl/mt65xx.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>

#include "pinctrl-mtk-common.h"
#include "pinctrl-mtk-mt7623.h"

static const struct mtk_drv_group_desc mt7623_drv_grp[] =  {
	/* 0E4E8SR 4/8/12/16 */
	MTK_DRV_GRP(4, 16, 1, 2, 4),
	/* 0E2E4SR  2/4/6/8 */
	MTK_DRV_GRP(2, 8, 1, 2, 2),
	/* E8E4E2  2/4/6/8/10/12/14/16 */
	MTK_DRV_GRP(2, 16, 0, 2, 2)
};

#define DRV_SEL0	0xf50
#define DRV_SEL1	0xf60
#define DRV_SEL2	0xf70
#define DRV_SEL3	0xf80
#define DRV_SEL4	0xf90
#define DRV_SEL5	0xfa0
#define DRV_SEL6	0xfb0
#define DRV_SEL7	0xfe0
#define DRV_SEL8	0xfd0
#define DRV_SEL9	0xff0
#define DRV_SEL10	0xf00

#define MSDC0_CTRL0	0xcc0
#define MSDC0_CTRL1	0xcd0
#define MSDC0_CTRL2	0xce0
#define MSDC0_CTRL3	0xcf0
#define MSDC0_CTRL4	0xd00
#define MSDC0_CTRL5	0xd10
#define MSDC0_CTRL6	0xd20
#define MSDC1_CTRL0	0xd30
#define MSDC1_CTRL1	0xd40
#define MSDC1_CTRL2	0xd50
#define MSDC1_CTRL3	0xd60
#define MSDC1_CTRL4	0xd70
#define MSDC1_CTRL5	0xd80
#define MSDC1_CTRL6	0xd90

#define IES_EN0		0xb20
#define IES_EN1		0xb30
#define IES_EN2		0xb40

#define SMT_EN0		0xb50
#define SMT_EN1		0xb60
#define SMT_EN2		0xb70

static const struct mtk_pin_drv_grp mt7623_pin_drv[] = {
	MTK_PIN_DRV_GRP(0, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(1, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(2, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(3, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(4, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(5, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(6, DRV_SEL0, 0, 1),
	MTK_PIN_DRV_GRP(7, DRV_SEL0, 4, 1),
	MTK_PIN_DRV_GRP(8, DRV_SEL0, 4, 1),
	MTK_PIN_DRV_GRP(9, DRV_SEL0, 4, 1),
	MTK_PIN_DRV_GRP(10, DRV_SEL0, 8, 1),
	MTK_PIN_DRV_GRP(11, DRV_SEL0, 8, 1),
	MTK_PIN_DRV_GRP(12, DRV_SEL0, 8, 1),
	MTK_PIN_DRV_GRP(13, DRV_SEL0, 8, 1),
	MTK_PIN_DRV_GRP(14, DRV_SEL0, 12, 0),
	MTK_PIN_DRV_GRP(15, DRV_SEL0, 12, 0),
	MTK_PIN_DRV_GRP(18, DRV_SEL1, 4, 0),
	MTK_PIN_DRV_GRP(19, DRV_SEL1, 4, 0),
	MTK_PIN_DRV_GRP(20, DRV_SEL1, 4, 0),
	MTK_PIN_DRV_GRP(21, DRV_SEL1, 4, 0),
	MTK_PIN_DRV_GRP(22, DRV_SEL1, 8, 0),
	MTK_PIN_DRV_GRP(23, DRV_SEL1, 8, 0),
	MTK_PIN_DRV_GRP(24, DRV_SEL1, 8, 0),
	MTK_PIN_DRV_GRP(25, DRV_SEL1, 8, 0),
	MTK_PIN_DRV_GRP(26, DRV_SEL1, 8, 0),
	MTK_PIN_DRV_GRP(27, DRV_SEL1, 12, 0),
	MTK_PIN_DRV_GRP(28, DRV_SEL1, 12, 0),
	MTK_PIN_DRV_GRP(29, DRV_SEL1, 12, 0),
	MTK_PIN_DRV_GRP(33, DRV_SEL2, 0, 0),
	MTK_PIN_DRV_GRP(34, DRV_SEL2, 0, 0),
	MTK_PIN_DRV_GRP(35, DRV_SEL2, 0, 0),
	MTK_PIN_DRV_GRP(36, DRV_SEL2, 0, 0),
	MTK_PIN_DRV_GRP(37, DRV_SEL2, 0, 0),
	MTK_PIN_DRV_GRP(39, DRV_SEL2, 8, 1),
	MTK_PIN_DRV_GRP(40, DRV_SEL2, 8, 1),
	MTK_PIN_DRV_GRP(41, DRV_SEL2, 8, 1),
	MTK_PIN_DRV_GRP(42, DRV_SEL2, 8, 1),
	MTK_PIN_DRV_GRP(43, DRV_SEL2, 12, 0),
	MTK_PIN_DRV_GRP(44, DRV_SEL2, 12, 0),
	MTK_PIN_DRV_GRP(45, DRV_SEL2, 12, 0),
	MTK_PIN_DRV_GRP(47, DRV_SEL3, 0, 0),
	MTK_PIN_DRV_GRP(48, DRV_SEL3, 0, 0),
	MTK_PIN_DRV_GRP(49, DRV_SEL3, 4, 0),
	MTK_PIN_DRV_GRP(53, DRV_SEL3, 12, 0),
	MTK_PIN_DRV_GRP(54, DRV_SEL3, 12, 0),
	MTK_PIN_DRV_GRP(55, DRV_SEL3, 12, 0),
	MTK_PIN_DRV_GRP(56, DRV_SEL3, 12, 0),
	MTK_PIN_DRV_GRP(60, DRV_SEL4, 8, 1),
	MTK_PIN_DRV_GRP(61, DRV_SEL4, 8, 1),
	MTK_PIN_DRV_GRP(62, DRV_SEL4, 8, 1),
	MTK_PIN_DRV_GRP(63, DRV_SEL4, 12, 1),
	MTK_PIN_DRV_GRP(64, DRV_SEL4, 12, 1),
	MTK_PIN_DRV_GRP(65, DRV_SEL4, 12, 1),
	MTK_PIN_DRV_GRP(66, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(67, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(68, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(69, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(70, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(71, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(72, DRV_SEL3, 4, 0),
	MTK_PIN_DRV_GRP(73, DRV_SEL3, 4, 0),
	MTK_PIN_DRV_GRP(74, DRV_SEL3, 4, 0),
	MTK_PIN_DRV_GRP(83, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(84, DRV_SEL5, 0, 1),
	MTK_PIN_DRV_GRP(105, MSDC1_CTRL1, 0, 1),
	MTK_PIN_DRV_GRP(106, MSDC1_CTRL0, 0, 1),
	MTK_PIN_DRV_GRP(107, MSDC1_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(108, MSDC1_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(109, MSDC1_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(110, MSDC1_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(111, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(112, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(113, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(114, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(115, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(116, MSDC0_CTRL1, 0, 1),
	MTK_PIN_DRV_GRP(117, MSDC0_CTRL0, 0, 1),
	MTK_PIN_DRV_GRP(118, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(119, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(120, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(121, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(126, DRV_SEL3, 4, 0),
	MTK_PIN_DRV_GRP(199, DRV_SEL0, 4, 1),
	MTK_PIN_DRV_GRP(200, DRV_SEL8, 0, 0),
	MTK_PIN_DRV_GRP(201, DRV_SEL8, 0, 0),
	MTK_PIN_DRV_GRP(203, DRV_SEL8, 4, 0),
	MTK_PIN_DRV_GRP(204, DRV_SEL8, 4, 0),
	MTK_PIN_DRV_GRP(205, DRV_SEL8, 4, 0),
	MTK_PIN_DRV_GRP(206, DRV_SEL8, 4, 0),
	MTK_PIN_DRV_GRP(207, DRV_SEL8, 4, 0),
	MTK_PIN_DRV_GRP(208, DRV_SEL8, 8, 0),
	MTK_PIN_DRV_GRP(209, DRV_SEL8, 8, 0),
	MTK_PIN_DRV_GRP(236, DRV_SEL9, 4, 0),
	MTK_PIN_DRV_GRP(237, DRV_SEL9, 4, 0),
	MTK_PIN_DRV_GRP(238, DRV_SEL9, 4, 0),
	MTK_PIN_DRV_GRP(239, DRV_SEL9, 4, 0),
	MTK_PIN_DRV_GRP(240, DRV_SEL9, 4, 0),
	MTK_PIN_DRV_GRP(241, DRV_SEL9, 4, 0),
	MTK_PIN_DRV_GRP(242, DRV_SEL9, 8, 0),
	MTK_PIN_DRV_GRP(243, DRV_SEL9, 8, 0),
	MTK_PIN_DRV_GRP(257, MSDC0_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(261, MSDC1_CTRL2, 0, 1),
	MTK_PIN_DRV_GRP(262, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(263, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(264, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(265, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(266, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(267, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(268, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(269, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(270, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(271, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(272, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(274, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(275, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(276, DRV_SEL10, 8, 0),
	MTK_PIN_DRV_GRP(278, DRV_SEL2, 8, 1),
};

static const struct mtk_pin_spec_pupd_set_samereg mt7623_spec_pupd[] = {
	MTK_PIN_PUPD_SPEC_SR(105, MSDC1_CTRL1, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(106, MSDC1_CTRL0, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(107, MSDC1_CTRL3, 0, 1, 2),
	MTK_PIN_PUPD_SPEC_SR(108, MSDC1_CTRL3, 4, 5, 6),
	MTK_PIN_PUPD_SPEC_SR(109, MSDC1_CTRL3, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(110, MSDC1_CTRL3, 12, 13, 14),
	MTK_PIN_PUPD_SPEC_SR(111, MSDC0_CTRL4, 12, 13, 14),
	MTK_PIN_PUPD_SPEC_SR(112, MSDC0_CTRL4, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(113, MSDC0_CTRL4, 4, 5, 6),
	MTK_PIN_PUPD_SPEC_SR(114, MSDC0_CTRL4, 0, 1, 2),
	MTK_PIN_PUPD_SPEC_SR(115, MSDC0_CTRL5, 0, 1, 2),
	MTK_PIN_PUPD_SPEC_SR(116, MSDC0_CTRL1, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(117, MSDC0_CTRL0, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(118, MSDC0_CTRL3, 12, 13, 14),
	MTK_PIN_PUPD_SPEC_SR(119, MSDC0_CTRL3, 8, 9, 10),
	MTK_PIN_PUPD_SPEC_SR(120, MSDC0_CTRL3, 4, 5, 6),
	MTK_PIN_PUPD_SPEC_SR(121, MSDC0_CTRL3, 0, 1, 2),
};

static int mt7623_spec_pull_set(struct regmap *regmap, unsigned int pin,
		unsigned char align, bool isup, unsigned int r1r0)
{
	return mtk_pctrl_spec_pull_set_samereg(regmap, mt7623_spec_pupd,
		ARRAY_SIZE(mt7623_spec_pupd), pin, align, isup, r1r0);
}

static const struct mtk_pin_ies_smt_set mt7623_ies_set[] = {
	MTK_PIN_IES_SMT_SPEC(0, 6, IES_EN0, 0),
	MTK_PIN_IES_SMT_SPEC(7, 9, IES_EN0, 1),
	MTK_PIN_IES_SMT_SPEC(10, 13, IES_EN0, 2),
	MTK_PIN_IES_SMT_SPEC(14, 15, IES_EN0, 3),
	MTK_PIN_IES_SMT_SPEC(18, 21, IES_EN0, 5),
	MTK_PIN_IES_SMT_SPEC(22, 26, IES_EN0, 6),
	MTK_PIN_IES_SMT_SPEC(27, 29, IES_EN0, 7),
	MTK_PIN_IES_SMT_SPEC(33, 37, IES_EN0, 8),
	MTK_PIN_IES_SMT_SPEC(39, 42, IES_EN0, 9),
	MTK_PIN_IES_SMT_SPEC(43, 45, IES_EN0, 10),
	MTK_PIN_IES_SMT_SPEC(47, 48, IES_EN0, 11),
	MTK_PIN_IES_SMT_SPEC(49, 49, IES_EN0, 12),
	MTK_PIN_IES_SMT_SPEC(53, 56, IES_EN0, 14),
	MTK_PIN_IES_SMT_SPEC(60, 62, IES_EN1, 0),
	MTK_PIN_IES_SMT_SPEC(63, 65, IES_EN1, 1),
	MTK_PIN_IES_SMT_SPEC(66, 71, IES_EN1, 2),
	MTK_PIN_IES_SMT_SPEC(72, 74, IES_EN0, 12),
	MTK_PIN_IES_SMT_SPEC(75, 76, IES_EN1, 3),
	MTK_PIN_IES_SMT_SPEC(83, 84, IES_EN1, 2),
	MTK_PIN_IES_SMT_SPEC(105, 121, MSDC1_CTRL1, 4),
	MTK_PIN_IES_SMT_SPEC(122, 125, IES_EN1, 7),
	MTK_PIN_IES_SMT_SPEC(126, 126, IES_EN0, 12),
	MTK_PIN_IES_SMT_SPEC(199, 201, IES_EN0, 1),
	MTK_PIN_IES_SMT_SPEC(203, 207, IES_EN2, 2),
	MTK_PIN_IES_SMT_SPEC(208, 209, IES_EN2, 3),
	MTK_PIN_IES_SMT_SPEC(236, 241, IES_EN2, 6),
	MTK_PIN_IES_SMT_SPEC(242, 243, IES_EN2, 7),
	MTK_PIN_IES_SMT_SPEC(261, 261, MSDC1_CTRL2, 4),
	MTK_PIN_IES_SMT_SPEC(262, 272, IES_EN2, 12),
	MTK_PIN_IES_SMT_SPEC(274, 276, IES_EN2, 12),
	MTK_PIN_IES_SMT_SPEC(278, 278, IES_EN2, 13),
};

static const struct mtk_pin_ies_smt_set mt7623_smt_set[] = {
	MTK_PIN_IES_SMT_SPEC(0, 6, SMT_EN0, 0),
	MTK_PIN_IES_SMT_SPEC(7, 9, SMT_EN0, 1),
	MTK_PIN_IES_SMT_SPEC(10, 13, SMT_EN0, 2),
	MTK_PIN_IES_SMT_SPEC(14, 15, SMT_EN0, 3),
	MTK_PIN_IES_SMT_SPEC(18, 21, SMT_EN0, 5),
	MTK_PIN_IES_SMT_SPEC(22, 26, SMT_EN0, 6),
	MTK_PIN_IES_SMT_SPEC(27, 29, SMT_EN0, 7),
	MTK_PIN_IES_SMT_SPEC(33, 37, SMT_EN0, 8),
	MTK_PIN_IES_SMT_SPEC(39, 42, SMT_EN0, 9),
	MTK_PIN_IES_SMT_SPEC(43, 45, SMT_EN0, 10),
	MTK_PIN_IES_SMT_SPEC(47, 48, SMT_EN0, 11),
	MTK_PIN_IES_SMT_SPEC(49, 49, SMT_EN0, 12),
	MTK_PIN_IES_SMT_SPEC(53, 56, SMT_EN0, 14),
	MTK_PIN_IES_SMT_SPEC(60, 62, SMT_EN1, 0),
	MTK_PIN_IES_SMT_SPEC(63, 65, SMT_EN1, 1),
	MTK_PIN_IES_SMT_SPEC(66, 71, SMT_EN1, 2),
	MTK_PIN_IES_SMT_SPEC(72, 74, SMT_EN0, 12),
	MTK_PIN_IES_SMT_SPEC(75, 76, SMT_EN1, 3),
	MTK_PIN_IES_SMT_SPEC(83, 84, SMT_EN1, 2),
	MTK_PIN_IES_SMT_SPEC(105, 106, MSDC1_CTRL1, 11),
	MTK_PIN_IES_SMT_SPEC(107, 107, MSDC1_CTRL3, 3),
	MTK_PIN_IES_SMT_SPEC(108, 108, MSDC1_CTRL3, 7),
	MTK_PIN_IES_SMT_SPEC(109, 109, MSDC1_CTRL3, 11),
	MTK_PIN_IES_SMT_SPEC(110, 111, MSDC1_CTRL3, 15),
	MTK_PIN_IES_SMT_SPEC(112, 112, MSDC0_CTRL4, 11),
	MTK_PIN_IES_SMT_SPEC(113, 113, MSDC0_CTRL4, 7),
	MTK_PIN_IES_SMT_SPEC(114, 115, MSDC0_CTRL4, 3),
	MTK_PIN_IES_SMT_SPEC(116, 117, MSDC0_CTRL1, 11),
	MTK_PIN_IES_SMT_SPEC(118, 118, MSDC0_CTRL3, 15),
	MTK_PIN_IES_SMT_SPEC(119, 119, MSDC0_CTRL3, 11),
	MTK_PIN_IES_SMT_SPEC(120, 120, MSDC0_CTRL3, 7),
	MTK_PIN_IES_SMT_SPEC(121, 121, MSDC0_CTRL3, 3),
	MTK_PIN_IES_SMT_SPEC(122, 125, SMT_EN1, 7),
	MTK_PIN_IES_SMT_SPEC(126, 126, SMT_EN0, 12),
	MTK_PIN_IES_SMT_SPEC(199, 201, SMT_EN0, 1),
	MTK_PIN_IES_SMT_SPEC(203, 207, SMT_EN2, 2),
	MTK_PIN_IES_SMT_SPEC(208, 209, SMT_EN2, 3),
	MTK_PIN_IES_SMT_SPEC(236, 241, SMT_EN2, 6),
	MTK_PIN_IES_SMT_SPEC(242, 243, SMT_EN2, 7),
	MTK_PIN_IES_SMT_SPEC(261, 261, MSDC1_CTRL6, 3),
	MTK_PIN_IES_SMT_SPEC(262, 272, SMT_EN2, 12),
	MTK_PIN_IES_SMT_SPEC(274, 276, SMT_EN2, 12),
	MTK_PIN_IES_SMT_SPEC(278, 278, SMT_EN2, 13),
};

static int mt7623_ies_smt_set(struct regmap *regmap, unsigned int pin,
		unsigned char align, int value, enum pin_config_param arg)
{
	if (arg == PIN_CONFIG_INPUT_ENABLE)
		return mtk_pconf_spec_set_ies_smt_range(regmap, mt7623_ies_set,
			ARRAY_SIZE(mt7623_ies_set), pin, align, value);
	else if (arg == PIN_CONFIG_INPUT_SCHMITT_ENABLE)
		return mtk_pconf_spec_set_ies_smt_range(regmap, mt7623_smt_set,
			ARRAY_SIZE(mt7623_smt_set), pin, align, value);
	return -EINVAL;
}

static const struct mtk_pinctrl_devdata mt7623_pinctrl_data = {
	.pins = mtk_pins_mt7623,
	.npins = ARRAY_SIZE(mtk_pins_mt7623),
	.grp_desc = mt7623_drv_grp,
	.n_grp_cls = ARRAY_SIZE(mt7623_drv_grp),
	.pin_drv_grp = mt7623_pin_drv,
	.n_pin_drv_grps = ARRAY_SIZE(mt7623_pin_drv),
	.spec_pull_set = mt7623_spec_pull_set,
	.spec_ies_smt_set = mt7623_ies_smt_set,
	.dir_offset = 0x0000,
	.pullen_offset = 0x0150,
	.pullsel_offset = 0x0280,
	.dout_offset = 0x0500,
	.din_offset = 0x0630,
	.pinmux_offset = 0x0760,
	.type1_start = 280,
	.type1_end = 280,
	.port_shf = 4,
	.port_mask = 0x1f,
	.port_align = 4,
	.eint_offsets = {
		.name = "mt7623_eint",
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
		.port_mask = 6,
		.ports     = 6,
	},
	.ap_num = 169,
	.db_cnt = 16,
};

static int mt7623_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_pctrl_init(pdev, &mt7623_pinctrl_data, NULL);
}

static const struct of_device_id mt7623_pctrl_match[] = {
	{ .compatible = "mediatek,mt7623-pinctrl", },
	{}
};
MODULE_DEVICE_TABLE(of, mt7623_pctrl_match);

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mt7623_pinctrl_probe,
	.driver = {
		.name = "mediatek-mt7623-pinctrl",
		.of_match_table = mt7623_pctrl_match,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}

arch_initcall(mtk_pinctrl_init);
