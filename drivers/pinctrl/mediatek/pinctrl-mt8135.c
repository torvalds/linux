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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>
#include <dt-bindings/pinctrl/mt65xx.h>

#include "pinctrl-mtk-common.h"
#include "pinctrl-mtk-mt8135.h"

#define DRV_BASE1				0x500
#define DRV_BASE2				0x510
#define PUPD_BASE1				0x400
#define PUPD_BASE2				0x450
#define R0_BASE1				0x4d0
#define R1_BASE1				0x200
#define R1_BASE2				0x250

struct mtk_spec_pull_set {
	unsigned char pin;
	unsigned char pupd_bit;
	unsigned short pupd_offset;
	unsigned short r0_offset;
	unsigned short r1_offset;
	unsigned char r0_bit;
	unsigned char r1_bit;
};

#define SPEC_PULL(_pin, _pupd_offset, _pupd_bit, _r0_offset, \
	_r0_bit, _r1_offset, _r1_bit)	\
	{	\
		.pin = _pin,	\
		.pupd_offset = _pupd_offset,	\
		.pupd_bit = _pupd_bit,	\
		.r0_offset = _r0_offset, \
		.r0_bit = _r0_bit, \
		.r1_offset = _r1_offset, \
		.r1_bit = _r1_bit, \
	}

static const struct mtk_drv_group_desc mt8135_drv_grp[] =  {
	/* E8E4E2 2/4/6/8/10/12/14/16 */
	MTK_DRV_GRP(2, 16, 0, 2, 2),
	/* E8E4  4/8/12/16 */
	MTK_DRV_GRP(4, 16, 1, 2, 4),
	/* E4E2  2/4/6/8 */
	MTK_DRV_GRP(2, 8, 0, 1, 2),
	/* E16E8E4 4/8/12/16/20/24/28/32 */
	MTK_DRV_GRP(4, 32, 0, 2, 4)
};

static const struct mtk_pin_drv_grp mt8135_pin_drv[] = {
	MTK_PIN_DRV_GRP(0, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(1, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(2, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(3, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(4, DRV_BASE1, 4, 0),
	MTK_PIN_DRV_GRP(5, DRV_BASE1, 8, 0),
	MTK_PIN_DRV_GRP(6, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(7, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(8, DRV_BASE1, 0, 0),
	MTK_PIN_DRV_GRP(9, DRV_BASE1, 0, 0),

	MTK_PIN_DRV_GRP(10, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(11, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(12, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(13, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(14, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(15, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(16, DRV_BASE1, 12, 1),
	MTK_PIN_DRV_GRP(17, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(18, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(19, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(20, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(21, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(22, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(23, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(24, DRV_BASE1, 16, 1),
	MTK_PIN_DRV_GRP(33, DRV_BASE1, 24, 1),
	MTK_PIN_DRV_GRP(34, DRV_BASE2, 12, 2),
	MTK_PIN_DRV_GRP(37, DRV_BASE2, 20, 1),
	MTK_PIN_DRV_GRP(38, DRV_BASE2, 20, 1),
	MTK_PIN_DRV_GRP(39, DRV_BASE2, 20, 1),
	MTK_PIN_DRV_GRP(40, DRV_BASE2, 24, 1),
	MTK_PIN_DRV_GRP(41, DRV_BASE2, 24, 1),
	MTK_PIN_DRV_GRP(42, DRV_BASE2, 24, 1),
	MTK_PIN_DRV_GRP(43, DRV_BASE2, 28, 1),
	MTK_PIN_DRV_GRP(44, DRV_BASE2, 28, 1),
	MTK_PIN_DRV_GRP(45, DRV_BASE2, 28, 1),
	MTK_PIN_DRV_GRP(46, DRV_BASE2, 28, 1),
	MTK_PIN_DRV_GRP(47, DRV_BASE2, 28, 1),

	MTK_PIN_DRV_GRP(49, DRV_BASE2+0x10, 0, 1),
	MTK_PIN_DRV_GRP(50, DRV_BASE2+0x10, 4, 1),
	MTK_PIN_DRV_GRP(51, DRV_BASE2+0x10, 8, 1),
	MTK_PIN_DRV_GRP(52, DRV_BASE2+0x10, 12, 2),
	MTK_PIN_DRV_GRP(53, DRV_BASE2+0x10, 16, 1),
	MTK_PIN_DRV_GRP(54, DRV_BASE2+0x10, 20, 1),
	MTK_PIN_DRV_GRP(55, DRV_BASE2+0x10, 24, 1),
	MTK_PIN_DRV_GRP(56, DRV_BASE2+0x10, 28, 1),

	MTK_PIN_DRV_GRP(57, DRV_BASE2+0x20, 0, 1),
	MTK_PIN_DRV_GRP(58, DRV_BASE2+0x20, 0, 1),
	MTK_PIN_DRV_GRP(59, DRV_BASE2+0x20, 0, 1),
	MTK_PIN_DRV_GRP(60, DRV_BASE2+0x20, 0, 1),
	MTK_PIN_DRV_GRP(61, DRV_BASE2+0x20, 0, 1),
	MTK_PIN_DRV_GRP(62, DRV_BASE2+0x20, 0, 1),
	MTK_PIN_DRV_GRP(63, DRV_BASE2+0x20, 4, 1),
	MTK_PIN_DRV_GRP(64, DRV_BASE2+0x20, 8, 1),
	MTK_PIN_DRV_GRP(65, DRV_BASE2+0x20, 12, 1),
	MTK_PIN_DRV_GRP(66, DRV_BASE2+0x20, 16, 1),
	MTK_PIN_DRV_GRP(67, DRV_BASE2+0x20, 20, 1),
	MTK_PIN_DRV_GRP(68, DRV_BASE2+0x20, 24, 1),
	MTK_PIN_DRV_GRP(69, DRV_BASE2+0x20, 28, 1),

	MTK_PIN_DRV_GRP(70, DRV_BASE2+0x30, 0, 1),
	MTK_PIN_DRV_GRP(71, DRV_BASE2+0x30, 4, 1),
	MTK_PIN_DRV_GRP(72, DRV_BASE2+0x30, 8, 1),
	MTK_PIN_DRV_GRP(73, DRV_BASE2+0x30, 12, 1),
	MTK_PIN_DRV_GRP(74, DRV_BASE2+0x30, 16, 1),
	MTK_PIN_DRV_GRP(75, DRV_BASE2+0x30, 20, 1),
	MTK_PIN_DRV_GRP(76, DRV_BASE2+0x30, 24, 1),
	MTK_PIN_DRV_GRP(77, DRV_BASE2+0x30, 28, 3),
	MTK_PIN_DRV_GRP(78, DRV_BASE2+0x30, 28, 3),

	MTK_PIN_DRV_GRP(79, DRV_BASE2+0x40, 0, 3),
	MTK_PIN_DRV_GRP(80, DRV_BASE2+0x40, 4, 3),

	MTK_PIN_DRV_GRP(81, DRV_BASE2+0x30, 28, 3),
	MTK_PIN_DRV_GRP(82, DRV_BASE2+0x30, 28, 3),

	MTK_PIN_DRV_GRP(83, DRV_BASE2+0x40, 8, 3),
	MTK_PIN_DRV_GRP(84, DRV_BASE2+0x40, 8, 3),
	MTK_PIN_DRV_GRP(85, DRV_BASE2+0x40, 12, 3),
	MTK_PIN_DRV_GRP(86, DRV_BASE2+0x40, 16, 3),
	MTK_PIN_DRV_GRP(87, DRV_BASE2+0x40, 8, 3),
	MTK_PIN_DRV_GRP(88, DRV_BASE2+0x40, 8, 3),

	MTK_PIN_DRV_GRP(89, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(90, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(91, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(92, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(93, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(94, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(95, DRV_BASE2+0x50, 12, 0),

	MTK_PIN_DRV_GRP(96, DRV_BASE1+0xb0, 28, 0),

	MTK_PIN_DRV_GRP(97, DRV_BASE2+0x50, 12, 0),
	MTK_PIN_DRV_GRP(98, DRV_BASE2+0x50, 16, 0),
	MTK_PIN_DRV_GRP(99, DRV_BASE2+0x50, 20, 1),
	MTK_PIN_DRV_GRP(102, DRV_BASE2+0x50, 24, 1),
	MTK_PIN_DRV_GRP(103, DRV_BASE2+0x50, 28, 1),


	MTK_PIN_DRV_GRP(104, DRV_BASE2+0x60, 0, 1),
	MTK_PIN_DRV_GRP(105, DRV_BASE2+0x60, 4, 1),
	MTK_PIN_DRV_GRP(106, DRV_BASE2+0x60, 4, 1),
	MTK_PIN_DRV_GRP(107, DRV_BASE2+0x60, 4, 1),
	MTK_PIN_DRV_GRP(108, DRV_BASE2+0x60, 4, 1),
	MTK_PIN_DRV_GRP(109, DRV_BASE2+0x60, 8, 2),
	MTK_PIN_DRV_GRP(110, DRV_BASE2+0x60, 12, 2),
	MTK_PIN_DRV_GRP(111, DRV_BASE2+0x60, 16, 2),
	MTK_PIN_DRV_GRP(112, DRV_BASE2+0x60, 20, 2),
	MTK_PIN_DRV_GRP(113, DRV_BASE2+0x60, 24, 2),
	MTK_PIN_DRV_GRP(114, DRV_BASE2+0x60, 28, 2),

	MTK_PIN_DRV_GRP(115, DRV_BASE2+0x70, 0, 2),
	MTK_PIN_DRV_GRP(116, DRV_BASE2+0x70, 4, 2),
	MTK_PIN_DRV_GRP(117, DRV_BASE2+0x70, 8, 2),
	MTK_PIN_DRV_GRP(118, DRV_BASE2+0x70, 12, 2),
	MTK_PIN_DRV_GRP(119, DRV_BASE2+0x70, 16, 2),
	MTK_PIN_DRV_GRP(120, DRV_BASE2+0x70, 20, 2),

	MTK_PIN_DRV_GRP(181, DRV_BASE1+0xa0, 12, 1),
	MTK_PIN_DRV_GRP(182, DRV_BASE1+0xa0, 16, 1),
	MTK_PIN_DRV_GRP(183, DRV_BASE1+0xa0, 20, 1),
	MTK_PIN_DRV_GRP(184, DRV_BASE1+0xa0, 24, 1),
	MTK_PIN_DRV_GRP(185, DRV_BASE1+0xa0, 28, 1),

	MTK_PIN_DRV_GRP(186, DRV_BASE1+0xb0, 0, 2),
	MTK_PIN_DRV_GRP(187, DRV_BASE1+0xb0, 0, 2),
	MTK_PIN_DRV_GRP(188, DRV_BASE1+0xb0, 0, 2),
	MTK_PIN_DRV_GRP(189, DRV_BASE1+0xb0, 0, 2),
	MTK_PIN_DRV_GRP(190, DRV_BASE1+0xb0, 4, 1),
	MTK_PIN_DRV_GRP(191, DRV_BASE1+0xb0, 8, 1),
	MTK_PIN_DRV_GRP(192, DRV_BASE1+0xb0, 12, 1),

	MTK_PIN_DRV_GRP(197, DRV_BASE1+0xb0, 16, 0),
	MTK_PIN_DRV_GRP(198, DRV_BASE1+0xb0, 16, 0),
	MTK_PIN_DRV_GRP(199, DRV_BASE1+0xb0, 20, 0),
	MTK_PIN_DRV_GRP(200, DRV_BASE1+0xb0, 24, 0),
	MTK_PIN_DRV_GRP(201, DRV_BASE1+0xb0, 16, 0),
	MTK_PIN_DRV_GRP(202, DRV_BASE1+0xb0, 16, 0)
};

static const struct mtk_spec_pull_set spec_pupd[] = {
	SPEC_PULL(0, PUPD_BASE1, 0, R0_BASE1, 9, R1_BASE1, 0),
	SPEC_PULL(1, PUPD_BASE1, 1, R0_BASE1, 8, R1_BASE1, 1),
	SPEC_PULL(2, PUPD_BASE1, 2, R0_BASE1, 7, R1_BASE1, 2),
	SPEC_PULL(3, PUPD_BASE1, 3, R0_BASE1, 6, R1_BASE1, 3),
	SPEC_PULL(4, PUPD_BASE1, 4, R0_BASE1, 1, R1_BASE1, 4),
	SPEC_PULL(5, PUPD_BASE1, 5, R0_BASE1, 0, R1_BASE1, 5),
	SPEC_PULL(6, PUPD_BASE1, 6, R0_BASE1, 5, R1_BASE1, 6),
	SPEC_PULL(7, PUPD_BASE1, 7, R0_BASE1, 4, R1_BASE1, 7),
	SPEC_PULL(8, PUPD_BASE1, 8, R0_BASE1, 3, R1_BASE1, 8),
	SPEC_PULL(9, PUPD_BASE1, 9, R0_BASE1, 2, R1_BASE1, 9),
	SPEC_PULL(89, PUPD_BASE2, 9, R0_BASE1, 18, R1_BASE2, 9),
	SPEC_PULL(90, PUPD_BASE2, 10, R0_BASE1, 19, R1_BASE2, 10),
	SPEC_PULL(91, PUPD_BASE2, 11, R0_BASE1, 23, R1_BASE2, 11),
	SPEC_PULL(92, PUPD_BASE2, 12, R0_BASE1, 24, R1_BASE2, 12),
	SPEC_PULL(93, PUPD_BASE2, 13, R0_BASE1, 25, R1_BASE2, 13),
	SPEC_PULL(94, PUPD_BASE2, 14, R0_BASE1, 22, R1_BASE2, 14),
	SPEC_PULL(95, PUPD_BASE2, 15, R0_BASE1, 20, R1_BASE2, 15),
	SPEC_PULL(96, PUPD_BASE2+0x10, 0, R0_BASE1, 16, R1_BASE2+0x10, 0),
	SPEC_PULL(97, PUPD_BASE2+0x10, 1, R0_BASE1, 21, R1_BASE2+0x10, 1),
	SPEC_PULL(98, PUPD_BASE2+0x10, 2, R0_BASE1, 17, R1_BASE2+0x10, 2),
	SPEC_PULL(197, PUPD_BASE1+0xc0, 5, R0_BASE1, 13, R1_BASE2+0xc0, 5),
	SPEC_PULL(198, PUPD_BASE2+0xc0, 6, R0_BASE1, 14, R1_BASE2+0xc0, 6),
	SPEC_PULL(199, PUPD_BASE2+0xc0, 7, R0_BASE1, 11, R1_BASE2+0xc0, 7),
	SPEC_PULL(200, PUPD_BASE2+0xc0, 8, R0_BASE1, 10, R1_BASE2+0xc0, 8),
	SPEC_PULL(201, PUPD_BASE2+0xc0, 9, R0_BASE1, 13, R1_BASE2+0xc0, 9),
	SPEC_PULL(202, PUPD_BASE2+0xc0, 10, R0_BASE1, 12, R1_BASE2+0xc0, 10)
};

static int spec_pull_set(struct regmap *regmap, unsigned int pin,
		unsigned char align, bool isup, unsigned int r1r0)
{
	unsigned int i;
	unsigned int reg_pupd, reg_set_r0, reg_set_r1;
	unsigned int reg_rst_r0, reg_rst_r1;
	bool find = false;

	for (i = 0; i < ARRAY_SIZE(spec_pupd); i++) {
		if (pin == spec_pupd[i].pin) {
			find = true;
			break;
		}
	}

	if (!find)
		return -EINVAL;

	if (isup)
		reg_pupd = spec_pupd[i].pupd_offset + align;
	else
		reg_pupd = spec_pupd[i].pupd_offset + (align << 1);

	regmap_write(regmap, reg_pupd, spec_pupd[i].pupd_bit);

	reg_set_r0 = spec_pupd[i].r0_offset + align;
	reg_rst_r0 = spec_pupd[i].r0_offset + (align << 1);
	reg_set_r1 = spec_pupd[i].r1_offset + align;
	reg_rst_r1 = spec_pupd[i].r1_offset + (align << 1);

	switch (r1r0) {
	case MTK_PUPD_SET_R1R0_00:
		regmap_write(regmap, reg_rst_r0, spec_pupd[i].r0_bit);
		regmap_write(regmap, reg_rst_r1, spec_pupd[i].r1_bit);
		break;
	case MTK_PUPD_SET_R1R0_01:
		regmap_write(regmap, reg_set_r0, spec_pupd[i].r0_bit);
		regmap_write(regmap, reg_rst_r1, spec_pupd[i].r1_bit);
		break;
	case MTK_PUPD_SET_R1R0_10:
		regmap_write(regmap, reg_rst_r0, spec_pupd[i].r0_bit);
		regmap_write(regmap, reg_set_r1, spec_pupd[i].r1_bit);
		break;
	case MTK_PUPD_SET_R1R0_11:
		regmap_write(regmap, reg_set_r0, spec_pupd[i].r0_bit);
		regmap_write(regmap, reg_set_r1, spec_pupd[i].r1_bit);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct mtk_pinctrl_devdata mt8135_pinctrl_data = {
	.pins = mtk_pins_mt8135,
	.npins = ARRAY_SIZE(mtk_pins_mt8135),
	.grp_desc = mt8135_drv_grp,
	.n_grp_cls = ARRAY_SIZE(mt8135_drv_grp),
	.pin_drv_grp = mt8135_pin_drv,
	.n_pin_drv_grps = ARRAY_SIZE(mt8135_pin_drv),
	.spec_pull_set = spec_pull_set,
	.dir_offset = 0x0000,
	.ies_offset = 0x0100,
	.pullen_offset = 0x0200,
	.smt_offset = 0x0300,
	.pullsel_offset = 0x0400,
	.dout_offset = 0x0800,
	.din_offset = 0x0A00,
	.pinmux_offset = 0x0C00,
	.type1_start = 34,
	.type1_end = 149,
	.port_shf = 4,
	.port_mask = 0xf,
	.port_align = 4,
	.eint_offsets = {
		.name = "mt8135_eint",
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
	.ap_num = 192,
	.db_cnt = 16,
};

static int mt8135_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_pctrl_init(pdev, &mt8135_pinctrl_data, NULL);
}

static const struct of_device_id mt8135_pctrl_match[] = {
	{
		.compatible = "mediatek,mt8135-pinctrl",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mt8135_pctrl_match);

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mt8135_pinctrl_probe,
	.driver = {
		.name = "mediatek-mt8135-pinctrl",
		.of_match_table = mt8135_pctrl_match,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}

arch_initcall(mtk_pinctrl_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek Pinctrl Driver");
MODULE_AUTHOR("Hongzhou Yang <hongzhou.yang@mediatek.com>");
