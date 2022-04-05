// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Min.Guo <min.guo@mediatek.com>
 */

#include <dt-bindings/pinctrl/mt65xx.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "pinctrl-mtk-common.h"
#include "pinctrl-mtk-mt8167.h"

static const struct mtk_drv_group_desc mt8167_drv_grp[] = {
	/* 0E4E8SR 4/8/12/16 */
	MTK_DRV_GRP(4, 16, 1, 2, 4),
	/* 0E2E4SR  2/4/6/8 */
	MTK_DRV_GRP(2, 8, 1, 2, 2),
	/* E8E4E2  2/4/6/8/10/12/14/16 */
	MTK_DRV_GRP(2, 16, 0, 2, 2)
};

static const struct mtk_pin_drv_grp mt8167_pin_drv[] = {
	MTK_PIN_DRV_GRP(0, 0xd00, 0, 0),
	MTK_PIN_DRV_GRP(1, 0xd00, 0, 0),
	MTK_PIN_DRV_GRP(2, 0xd00, 0, 0),
	MTK_PIN_DRV_GRP(3, 0xd00, 0, 0),
	MTK_PIN_DRV_GRP(4, 0xd00, 0, 0),

	MTK_PIN_DRV_GRP(5, 0xd00, 4, 0),
	MTK_PIN_DRV_GRP(6, 0xd00, 4, 0),
	MTK_PIN_DRV_GRP(7, 0xd00, 4, 0),
	MTK_PIN_DRV_GRP(8, 0xd00, 4, 0),
	MTK_PIN_DRV_GRP(9, 0xd00, 4, 0),
	MTK_PIN_DRV_GRP(10, 0xd00, 4, 0),

	MTK_PIN_DRV_GRP(11, 0xd00, 8, 0),
	MTK_PIN_DRV_GRP(12, 0xd00, 8, 0),
	MTK_PIN_DRV_GRP(13, 0xd00, 8, 0),

	MTK_PIN_DRV_GRP(14, 0xd00, 12, 2),
	MTK_PIN_DRV_GRP(15, 0xd00, 12, 2),
	MTK_PIN_DRV_GRP(16, 0xd00, 12, 2),
	MTK_PIN_DRV_GRP(17, 0xd00, 12, 2),

	MTK_PIN_DRV_GRP(18, 0xd10, 0, 0),
	MTK_PIN_DRV_GRP(19, 0xd10, 0, 0),
	MTK_PIN_DRV_GRP(20, 0xd10, 0, 0),

	MTK_PIN_DRV_GRP(21, 0xd00, 12, 2),
	MTK_PIN_DRV_GRP(22, 0xd00, 12, 2),
	MTK_PIN_DRV_GRP(23, 0xd00, 12, 2),

	MTK_PIN_DRV_GRP(24, 0xd00, 8, 0),
	MTK_PIN_DRV_GRP(25, 0xd00, 8, 0),

	MTK_PIN_DRV_GRP(26, 0xd10, 4, 1),
	MTK_PIN_DRV_GRP(27, 0xd10, 4, 1),
	MTK_PIN_DRV_GRP(28, 0xd10, 4, 1),
	MTK_PIN_DRV_GRP(29, 0xd10, 4, 1),
	MTK_PIN_DRV_GRP(30, 0xd10, 4, 1),

	MTK_PIN_DRV_GRP(31, 0xd10, 8, 1),
	MTK_PIN_DRV_GRP(32, 0xd10, 8, 1),
	MTK_PIN_DRV_GRP(33, 0xd10, 8, 1),

	MTK_PIN_DRV_GRP(34, 0xd10, 12, 0),
	MTK_PIN_DRV_GRP(35, 0xd10, 12, 0),

	MTK_PIN_DRV_GRP(36, 0xd20, 0, 0),
	MTK_PIN_DRV_GRP(37, 0xd20, 0, 0),
	MTK_PIN_DRV_GRP(38, 0xd20, 0, 0),
	MTK_PIN_DRV_GRP(39, 0xd20, 0, 0),

	MTK_PIN_DRV_GRP(40, 0xd20, 4, 1),

	MTK_PIN_DRV_GRP(41, 0xd20, 8, 1),
	MTK_PIN_DRV_GRP(42, 0xd20, 8, 1),
	MTK_PIN_DRV_GRP(43, 0xd20, 8, 1),

	MTK_PIN_DRV_GRP(44, 0xd20, 12, 1),
	MTK_PIN_DRV_GRP(45, 0xd20, 12, 1),
	MTK_PIN_DRV_GRP(46, 0xd20, 12, 1),
	MTK_PIN_DRV_GRP(47, 0xd20, 12, 1),

	MTK_PIN_DRV_GRP(48, 0xd30, 0, 1),
	MTK_PIN_DRV_GRP(49, 0xd30, 0, 1),
	MTK_PIN_DRV_GRP(50, 0xd30, 0, 1),
	MTK_PIN_DRV_GRP(51, 0xd30, 0, 1),

	MTK_PIN_DRV_GRP(54, 0xd30, 8, 1),

	MTK_PIN_DRV_GRP(55, 0xd30, 12, 1),
	MTK_PIN_DRV_GRP(56, 0xd30, 12, 1),
	MTK_PIN_DRV_GRP(57, 0xd30, 12, 1),

	MTK_PIN_DRV_GRP(62, 0xd40, 8, 1),
	MTK_PIN_DRV_GRP(63, 0xd40, 8, 1),
	MTK_PIN_DRV_GRP(64, 0xd40, 8, 1),
	MTK_PIN_DRV_GRP(65, 0xd40, 8, 1),
	MTK_PIN_DRV_GRP(66, 0xd40, 8, 1),
	MTK_PIN_DRV_GRP(67, 0xd40, 8, 1),

	MTK_PIN_DRV_GRP(68, 0xd40, 12, 2),

	MTK_PIN_DRV_GRP(69, 0xd50, 0, 2),

	MTK_PIN_DRV_GRP(70, 0xd50, 4, 2),
	MTK_PIN_DRV_GRP(71, 0xd50, 4, 2),
	MTK_PIN_DRV_GRP(72, 0xd50, 4, 2),
	MTK_PIN_DRV_GRP(73, 0xd50, 4, 2),

	MTK_PIN_DRV_GRP(100, 0xd50, 8, 1),
	MTK_PIN_DRV_GRP(101, 0xd50, 8, 1),
	MTK_PIN_DRV_GRP(102, 0xd50, 8, 1),
	MTK_PIN_DRV_GRP(103, 0xd50, 8, 1),

	MTK_PIN_DRV_GRP(104, 0xd50, 12, 2),

	MTK_PIN_DRV_GRP(105, 0xd60, 0, 2),

	MTK_PIN_DRV_GRP(106, 0xd60, 4, 2),
	MTK_PIN_DRV_GRP(107, 0xd60, 4, 2),
	MTK_PIN_DRV_GRP(108, 0xd60, 4, 2),
	MTK_PIN_DRV_GRP(109, 0xd60, 4, 2),

	MTK_PIN_DRV_GRP(110, 0xd70, 0, 2),
	MTK_PIN_DRV_GRP(111, 0xd70, 0, 2),
	MTK_PIN_DRV_GRP(112, 0xd70, 0, 2),
	MTK_PIN_DRV_GRP(113, 0xd70, 0, 2),

	MTK_PIN_DRV_GRP(114, 0xd70, 4, 2),

	MTK_PIN_DRV_GRP(115, 0xd60, 12, 2),

	MTK_PIN_DRV_GRP(116, 0xd60, 8, 2),

	MTK_PIN_DRV_GRP(117, 0xd70, 0, 2),
	MTK_PIN_DRV_GRP(118, 0xd70, 0, 2),
	MTK_PIN_DRV_GRP(119, 0xd70, 0, 2),
	MTK_PIN_DRV_GRP(120, 0xd70, 0, 2),
};

static const struct mtk_pin_spec_pupd_set_samereg mt8167_spec_pupd[] = {
	MTK_PIN_PUPD_SPEC_SR(14, 0xe50, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(15, 0xe60, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(16, 0xe60, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(17, 0xe60, 10, 9, 8),

	MTK_PIN_PUPD_SPEC_SR(21, 0xe60, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(22, 0xe70, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(23, 0xe70, 6, 5, 4),

	MTK_PIN_PUPD_SPEC_SR(40, 0xe80, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(41, 0xe80, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(42, 0xe90, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(43, 0xe90, 6, 5, 4),

	MTK_PIN_PUPD_SPEC_SR(68, 0xe50, 10, 9, 8),
	MTK_PIN_PUPD_SPEC_SR(69, 0xe50, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(70, 0xe40, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(71, 0xe40, 10, 9, 8),
	MTK_PIN_PUPD_SPEC_SR(72, 0xe40, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(73, 0xe50, 2, 1, 0),

	MTK_PIN_PUPD_SPEC_SR(104, 0xe40, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(105, 0xe30, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(106, 0xe20, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(107, 0xe30, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(108, 0xe30, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(109, 0xe30, 10, 9, 8),
	MTK_PIN_PUPD_SPEC_SR(110, 0xe10, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(111, 0xe10, 10, 9, 8),
	MTK_PIN_PUPD_SPEC_SR(112, 0xe10, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(113, 0xe10, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(114, 0xe20, 10, 9, 8),
	MTK_PIN_PUPD_SPEC_SR(115, 0xe20, 2, 1, 0),
	MTK_PIN_PUPD_SPEC_SR(116, 0xe20, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(117, 0xe00, 14, 13, 12),
	MTK_PIN_PUPD_SPEC_SR(118, 0xe00, 10, 9, 8),
	MTK_PIN_PUPD_SPEC_SR(119, 0xe00, 6, 5, 4),
	MTK_PIN_PUPD_SPEC_SR(120, 0xe00, 2, 1, 0),
};

static const struct mtk_pin_ies_smt_set mt8167_ies_set[] = {
	MTK_PIN_IES_SMT_SPEC(0, 6, 0x900, 2),
	MTK_PIN_IES_SMT_SPEC(7, 10, 0x900, 3),
	MTK_PIN_IES_SMT_SPEC(11, 13, 0x900, 12),
	MTK_PIN_IES_SMT_SPEC(14, 17, 0x900, 13),
	MTK_PIN_IES_SMT_SPEC(18, 20, 0x910, 10),
	MTK_PIN_IES_SMT_SPEC(21, 23, 0x900, 13),
	MTK_PIN_IES_SMT_SPEC(24, 25, 0x900, 12),
	MTK_PIN_IES_SMT_SPEC(26, 30, 0x900, 0),
	MTK_PIN_IES_SMT_SPEC(31, 33, 0x900, 1),
	MTK_PIN_IES_SMT_SPEC(34, 39, 0x900, 2),
	MTK_PIN_IES_SMT_SPEC(40, 40, 0x910, 11),
	MTK_PIN_IES_SMT_SPEC(41, 43, 0x900, 10),
	MTK_PIN_IES_SMT_SPEC(44, 47, 0x900, 11),
	MTK_PIN_IES_SMT_SPEC(48, 51, 0x900, 14),
	MTK_PIN_IES_SMT_SPEC(52, 53, 0x910, 0),
	MTK_PIN_IES_SMT_SPEC(54, 54, 0x910, 2),
	MTK_PIN_IES_SMT_SPEC(55, 57, 0x910, 4),
	MTK_PIN_IES_SMT_SPEC(58, 59, 0x900, 15),
	MTK_PIN_IES_SMT_SPEC(60, 61, 0x910, 1),
	MTK_PIN_IES_SMT_SPEC(62, 65, 0x910, 5),
	MTK_PIN_IES_SMT_SPEC(66, 67, 0x910, 6),
	MTK_PIN_IES_SMT_SPEC(68, 68, 0x930, 2),
	MTK_PIN_IES_SMT_SPEC(69, 69, 0x930, 1),
	MTK_PIN_IES_SMT_SPEC(70, 70, 0x930, 6),
	MTK_PIN_IES_SMT_SPEC(71, 71, 0x930, 5),
	MTK_PIN_IES_SMT_SPEC(72, 72, 0x930, 4),
	MTK_PIN_IES_SMT_SPEC(73, 73, 0x930, 3),
	MTK_PIN_IES_SMT_SPEC(100, 103, 0x910, 7),
	MTK_PIN_IES_SMT_SPEC(104, 104, 0x920, 12),
	MTK_PIN_IES_SMT_SPEC(105, 105, 0x920, 11),
	MTK_PIN_IES_SMT_SPEC(106, 106, 0x930, 0),
	MTK_PIN_IES_SMT_SPEC(107, 107, 0x920, 15),
	MTK_PIN_IES_SMT_SPEC(108, 108, 0x920, 14),
	MTK_PIN_IES_SMT_SPEC(109, 109, 0x920, 13),
	MTK_PIN_IES_SMT_SPEC(110, 110, 0x920, 9),
	MTK_PIN_IES_SMT_SPEC(111, 111, 0x920, 8),
	MTK_PIN_IES_SMT_SPEC(112, 112, 0x920, 7),
	MTK_PIN_IES_SMT_SPEC(113, 113, 0x920, 6),
	MTK_PIN_IES_SMT_SPEC(114, 114, 0x920, 10),
	MTK_PIN_IES_SMT_SPEC(115, 115, 0x920, 1),
	MTK_PIN_IES_SMT_SPEC(116, 116, 0x920, 0),
	MTK_PIN_IES_SMT_SPEC(117, 117, 0x920, 5),
	MTK_PIN_IES_SMT_SPEC(118, 118, 0x920, 4),
	MTK_PIN_IES_SMT_SPEC(119, 119, 0x920, 3),
	MTK_PIN_IES_SMT_SPEC(120, 120, 0x920, 2),
	MTK_PIN_IES_SMT_SPEC(121, 124, 0x910, 9),
};

static const struct mtk_pin_ies_smt_set mt8167_smt_set[] = {
	MTK_PIN_IES_SMT_SPEC(0, 6, 0xA00, 2),
	MTK_PIN_IES_SMT_SPEC(7, 10, 0xA00, 3),
	MTK_PIN_IES_SMT_SPEC(11, 13, 0xA00, 12),
	MTK_PIN_IES_SMT_SPEC(14, 17, 0xA00, 13),
	MTK_PIN_IES_SMT_SPEC(18, 20, 0xA10, 10),
	MTK_PIN_IES_SMT_SPEC(21, 23, 0xA00, 13),
	MTK_PIN_IES_SMT_SPEC(24, 25, 0xA00, 12),
	MTK_PIN_IES_SMT_SPEC(26, 30, 0xA00, 0),
	MTK_PIN_IES_SMT_SPEC(31, 33, 0xA00, 1),
	MTK_PIN_IES_SMT_SPEC(34, 39, 0xA900, 2),
	MTK_PIN_IES_SMT_SPEC(40, 40, 0xA10, 11),
	MTK_PIN_IES_SMT_SPEC(41, 43, 0xA00, 10),
	MTK_PIN_IES_SMT_SPEC(44, 47, 0xA00, 11),
	MTK_PIN_IES_SMT_SPEC(48, 51, 0xA00, 14),
	MTK_PIN_IES_SMT_SPEC(52, 53, 0xA10, 0),
	MTK_PIN_IES_SMT_SPEC(54, 54, 0xA10, 2),
	MTK_PIN_IES_SMT_SPEC(55, 57, 0xA10, 4),
	MTK_PIN_IES_SMT_SPEC(58, 59, 0xA00, 15),
	MTK_PIN_IES_SMT_SPEC(60, 61, 0xA10, 1),
	MTK_PIN_IES_SMT_SPEC(62, 65, 0xA10, 5),
	MTK_PIN_IES_SMT_SPEC(66, 67, 0xA10, 6),
	MTK_PIN_IES_SMT_SPEC(68, 68, 0xA30, 2),
	MTK_PIN_IES_SMT_SPEC(69, 69, 0xA30, 1),
	MTK_PIN_IES_SMT_SPEC(70, 70, 0xA30, 3),
	MTK_PIN_IES_SMT_SPEC(71, 71, 0xA30, 4),
	MTK_PIN_IES_SMT_SPEC(72, 72, 0xA30, 5),
	MTK_PIN_IES_SMT_SPEC(73, 73, 0xA30, 6),

	MTK_PIN_IES_SMT_SPEC(100, 103, 0xA10, 7),
	MTK_PIN_IES_SMT_SPEC(104, 104, 0xA20, 12),
	MTK_PIN_IES_SMT_SPEC(105, 105, 0xA20, 11),
	MTK_PIN_IES_SMT_SPEC(106, 106, 0xA30, 13),
	MTK_PIN_IES_SMT_SPEC(107, 107, 0xA20, 14),
	MTK_PIN_IES_SMT_SPEC(108, 108, 0xA20, 15),
	MTK_PIN_IES_SMT_SPEC(109, 109, 0xA30, 0),
	MTK_PIN_IES_SMT_SPEC(110, 110, 0xA20, 9),
	MTK_PIN_IES_SMT_SPEC(111, 111, 0xA20, 8),
	MTK_PIN_IES_SMT_SPEC(112, 112, 0xA20, 7),
	MTK_PIN_IES_SMT_SPEC(113, 113, 0xA20, 6),
	MTK_PIN_IES_SMT_SPEC(114, 114, 0xA20, 10),
	MTK_PIN_IES_SMT_SPEC(115, 115, 0xA20, 1),
	MTK_PIN_IES_SMT_SPEC(116, 116, 0xA20, 0),
	MTK_PIN_IES_SMT_SPEC(117, 117, 0xA20, 5),
	MTK_PIN_IES_SMT_SPEC(118, 118, 0xA20, 4),
	MTK_PIN_IES_SMT_SPEC(119, 119, 0xA20, 3),
	MTK_PIN_IES_SMT_SPEC(120, 120, 0xA20, 2),
	MTK_PIN_IES_SMT_SPEC(121, 124, 0xA10, 9),
};

static const struct mtk_pinctrl_devdata mt8167_pinctrl_data = {
	.pins = mtk_pins_mt8167,
	.npins = ARRAY_SIZE(mtk_pins_mt8167),
	.grp_desc = mt8167_drv_grp,
	.n_grp_cls = ARRAY_SIZE(mt8167_drv_grp),
	.pin_drv_grp = mt8167_pin_drv,
	.n_pin_drv_grps = ARRAY_SIZE(mt8167_pin_drv),
	.spec_ies = mt8167_ies_set,
	.n_spec_ies = ARRAY_SIZE(mt8167_ies_set),
	.spec_pupd = mt8167_spec_pupd,
	.n_spec_pupd = ARRAY_SIZE(mt8167_spec_pupd),
	.spec_smt = mt8167_smt_set,
	.n_spec_smt = ARRAY_SIZE(mt8167_smt_set),
	.spec_pull_set = mtk_pctrl_spec_pull_set_samereg,
	.spec_ies_smt_set = mtk_pconf_spec_set_ies_smt_range,
	.dir_offset = 0x0000,
	.pullen_offset = 0x0500,
	.pullsel_offset = 0x0600,
	.dout_offset = 0x0100,
	.din_offset = 0x0200,
	.pinmux_offset = 0x0300,
	.type1_start = 125,
	.type1_end = 125,
	.port_shf = 4,
	.port_mask = 0xf,
	.port_align = 4,
	.mode_mask = 0xf,
	.mode_per_reg = 5,
	.mode_shf = 4,
	.eint_hw = {
		.port_mask = 7,
		.ports     = 6,
		.ap_num    = 169,
		.db_cnt    = 64,
	},
};

static const struct of_device_id mt8167_pctrl_match[] = {
	{ .compatible = "mediatek,mt8167-pinctrl", .data = &mt8167_pinctrl_data },
	{}
};

MODULE_DEVICE_TABLE(of, mt8167_pctrl_match);

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mtk_pctrl_common_probe,
	.driver = {
		.name = "mediatek-mt8167-pinctrl",
		.of_match_table = mt8167_pctrl_match,
		.pm = &mtk_eint_pm_ops,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}
arch_initcall(mtk_pinctrl_init);
