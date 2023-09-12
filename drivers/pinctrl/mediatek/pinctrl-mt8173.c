// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2015 MediaTek Inc.
 * Author: Hongzhou.Yang <hongzhou.yang@mediatek.com>
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/regmap.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <dt-bindings/pinctrl/mt65xx.h>

#include "pinctrl-mtk-common.h"
#include "pinctrl-mtk-mt8173.h"

#define DRV_BASE				0xb00

static const struct mtk_pin_spec_pupd_set_samereg mt8173_spec_pupd[] = {
	MTK_PIN_PUPD_SPEC_SR(119, 0xe00, 2, 1, 0),  /* KROW0 */
	MTK_PIN_PUPD_SPEC_SR(120, 0xe00, 6, 5, 4),  /* KROW1 */
	MTK_PIN_PUPD_SPEC_SR(121, 0xe00, 10, 9, 8), /* KROW2 */
	MTK_PIN_PUPD_SPEC_SR(122, 0xe10, 2, 1, 0),  /* KCOL0 */
	MTK_PIN_PUPD_SPEC_SR(123, 0xe10, 6, 5, 4),  /* KCOL1 */
	MTK_PIN_PUPD_SPEC_SR(124, 0xe10, 10, 9, 8), /* KCOL2 */

	MTK_PIN_PUPD_SPEC_SR(67, 0xd10, 2, 1, 0),   /* ms0 DS */
	MTK_PIN_PUPD_SPEC_SR(68, 0xd00, 2, 1, 0),   /* ms0 RST */
	MTK_PIN_PUPD_SPEC_SR(66, 0xc10, 2, 1, 0),   /* ms0 cmd */
	MTK_PIN_PUPD_SPEC_SR(65, 0xc00, 2, 1, 0),   /* ms0 clk */
	MTK_PIN_PUPD_SPEC_SR(57, 0xc20, 2, 1, 0),   /* ms0 data0 */
	MTK_PIN_PUPD_SPEC_SR(58, 0xc20, 2, 1, 0),   /* ms0 data1 */
	MTK_PIN_PUPD_SPEC_SR(59, 0xc20, 2, 1, 0),   /* ms0 data2 */
	MTK_PIN_PUPD_SPEC_SR(60, 0xc20, 2, 1, 0),   /* ms0 data3 */
	MTK_PIN_PUPD_SPEC_SR(61, 0xc20, 2, 1, 0),   /* ms0 data4 */
	MTK_PIN_PUPD_SPEC_SR(62, 0xc20, 2, 1, 0),   /* ms0 data5 */
	MTK_PIN_PUPD_SPEC_SR(63, 0xc20, 2, 1, 0),   /* ms0 data6 */
	MTK_PIN_PUPD_SPEC_SR(64, 0xc20, 2, 1, 0),   /* ms0 data7 */

	MTK_PIN_PUPD_SPEC_SR(78, 0xc50, 2, 1, 0),    /* ms1 cmd */
	MTK_PIN_PUPD_SPEC_SR(73, 0xd20, 2, 1, 0),    /* ms1 dat0 */
	MTK_PIN_PUPD_SPEC_SR(74, 0xd20, 6, 5, 4),    /* ms1 dat1 */
	MTK_PIN_PUPD_SPEC_SR(75, 0xd20, 10, 9, 8),   /* ms1 dat2 */
	MTK_PIN_PUPD_SPEC_SR(76, 0xd20, 14, 13, 12), /* ms1 dat3 */
	MTK_PIN_PUPD_SPEC_SR(77, 0xc40, 2, 1, 0),    /* ms1 clk */

	MTK_PIN_PUPD_SPEC_SR(100, 0xd40, 2, 1, 0),    /* ms2 dat0 */
	MTK_PIN_PUPD_SPEC_SR(101, 0xd40, 6, 5, 4),    /* ms2 dat1 */
	MTK_PIN_PUPD_SPEC_SR(102, 0xd40, 10, 9, 8),   /* ms2 dat2 */
	MTK_PIN_PUPD_SPEC_SR(103, 0xd40, 14, 13, 12), /* ms2 dat3 */
	MTK_PIN_PUPD_SPEC_SR(104, 0xc80, 2, 1, 0),    /* ms2 clk */
	MTK_PIN_PUPD_SPEC_SR(105, 0xc90, 2, 1, 0),    /* ms2 cmd */

	MTK_PIN_PUPD_SPEC_SR(22, 0xd60, 2, 1, 0),    /* ms3 dat0 */
	MTK_PIN_PUPD_SPEC_SR(23, 0xd60, 6, 5, 4),    /* ms3 dat1 */
	MTK_PIN_PUPD_SPEC_SR(24, 0xd60, 10, 9, 8),   /* ms3 dat2 */
	MTK_PIN_PUPD_SPEC_SR(25, 0xd60, 14, 13, 12), /* ms3 dat3 */
	MTK_PIN_PUPD_SPEC_SR(26, 0xcc0, 2, 1, 0),    /* ms3 clk */
	MTK_PIN_PUPD_SPEC_SR(27, 0xcd0, 2, 1, 0)     /* ms3 cmd */
};

static const struct mtk_pin_ies_smt_set mt8173_smt_set[] = {
	MTK_PIN_IES_SMT_SPEC(0, 4, 0x930, 1),
	MTK_PIN_IES_SMT_SPEC(5, 9, 0x930, 2),
	MTK_PIN_IES_SMT_SPEC(10, 13, 0x930, 10),
	MTK_PIN_IES_SMT_SPEC(14, 15, 0x940, 10),
	MTK_PIN_IES_SMT_SPEC(16, 16, 0x930, 0),
	MTK_PIN_IES_SMT_SPEC(17, 17, 0x950, 2),
	MTK_PIN_IES_SMT_SPEC(18, 21, 0x940, 3),
	MTK_PIN_IES_SMT_SPEC(22, 25, 0xce0, 13),
	MTK_PIN_IES_SMT_SPEC(26, 26, 0xcc0, 13),
	MTK_PIN_IES_SMT_SPEC(27, 27, 0xcd0, 13),
	MTK_PIN_IES_SMT_SPEC(28, 28, 0xd70, 13),
	MTK_PIN_IES_SMT_SPEC(29, 32, 0x930, 3),
	MTK_PIN_IES_SMT_SPEC(33, 33, 0x930, 4),
	MTK_PIN_IES_SMT_SPEC(34, 36, 0x930, 5),
	MTK_PIN_IES_SMT_SPEC(37, 38, 0x930, 6),
	MTK_PIN_IES_SMT_SPEC(39, 39, 0x930, 7),
	MTK_PIN_IES_SMT_SPEC(40, 41, 0x930, 9),
	MTK_PIN_IES_SMT_SPEC(42, 42, 0x940, 0),
	MTK_PIN_IES_SMT_SPEC(43, 44, 0x930, 11),
	MTK_PIN_IES_SMT_SPEC(45, 46, 0x930, 12),
	MTK_PIN_IES_SMT_SPEC(57, 64, 0xc20, 13),
	MTK_PIN_IES_SMT_SPEC(65, 65, 0xc10, 13),
	MTK_PIN_IES_SMT_SPEC(66, 66, 0xc00, 13),
	MTK_PIN_IES_SMT_SPEC(67, 67, 0xd10, 13),
	MTK_PIN_IES_SMT_SPEC(68, 68, 0xd00, 13),
	MTK_PIN_IES_SMT_SPEC(69, 72, 0x940, 14),
	MTK_PIN_IES_SMT_SPEC(73, 76, 0xc60, 13),
	MTK_PIN_IES_SMT_SPEC(77, 77, 0xc40, 13),
	MTK_PIN_IES_SMT_SPEC(78, 78, 0xc50, 13),
	MTK_PIN_IES_SMT_SPEC(79, 82, 0x940, 15),
	MTK_PIN_IES_SMT_SPEC(83, 83, 0x950, 0),
	MTK_PIN_IES_SMT_SPEC(84, 85, 0x950, 1),
	MTK_PIN_IES_SMT_SPEC(86, 91, 0x950, 2),
	MTK_PIN_IES_SMT_SPEC(92, 92, 0x930, 13),
	MTK_PIN_IES_SMT_SPEC(93, 95, 0x930, 14),
	MTK_PIN_IES_SMT_SPEC(96, 99, 0x930, 15),
	MTK_PIN_IES_SMT_SPEC(100, 103, 0xca0, 13),
	MTK_PIN_IES_SMT_SPEC(104, 104, 0xc80, 13),
	MTK_PIN_IES_SMT_SPEC(105, 105, 0xc90, 13),
	MTK_PIN_IES_SMT_SPEC(106, 107, 0x940, 4),
	MTK_PIN_IES_SMT_SPEC(108, 112, 0x940, 1),
	MTK_PIN_IES_SMT_SPEC(113, 116, 0x940, 2),
	MTK_PIN_IES_SMT_SPEC(117, 118, 0x940, 5),
	MTK_PIN_IES_SMT_SPEC(119, 124, 0x940, 6),
	MTK_PIN_IES_SMT_SPEC(125, 126, 0x940, 7),
	MTK_PIN_IES_SMT_SPEC(127, 127, 0x940, 0),
	MTK_PIN_IES_SMT_SPEC(128, 128, 0x950, 8),
	MTK_PIN_IES_SMT_SPEC(129, 130, 0x950, 9),
	MTK_PIN_IES_SMT_SPEC(131, 132, 0x950, 8),
	MTK_PIN_IES_SMT_SPEC(133, 134, 0x910, 8)
};

static const struct mtk_pin_ies_smt_set mt8173_ies_set[] = {
	MTK_PIN_IES_SMT_SPEC(0, 4, 0x900, 1),
	MTK_PIN_IES_SMT_SPEC(5, 9, 0x900, 2),
	MTK_PIN_IES_SMT_SPEC(10, 13, 0x900, 10),
	MTK_PIN_IES_SMT_SPEC(14, 15, 0x910, 10),
	MTK_PIN_IES_SMT_SPEC(16, 16, 0x900, 0),
	MTK_PIN_IES_SMT_SPEC(17, 17, 0x920, 2),
	MTK_PIN_IES_SMT_SPEC(18, 21, 0x910, 3),
	MTK_PIN_IES_SMT_SPEC(22, 25, 0xce0, 14),
	MTK_PIN_IES_SMT_SPEC(26, 26, 0xcc0, 14),
	MTK_PIN_IES_SMT_SPEC(27, 27, 0xcd0, 14),
	MTK_PIN_IES_SMT_SPEC(28, 28, 0xd70, 14),
	MTK_PIN_IES_SMT_SPEC(29, 32, 0x900, 3),
	MTK_PIN_IES_SMT_SPEC(33, 33, 0x900, 4),
	MTK_PIN_IES_SMT_SPEC(34, 36, 0x900, 5),
	MTK_PIN_IES_SMT_SPEC(37, 38, 0x900, 6),
	MTK_PIN_IES_SMT_SPEC(39, 39, 0x900, 7),
	MTK_PIN_IES_SMT_SPEC(40, 41, 0x900, 9),
	MTK_PIN_IES_SMT_SPEC(42, 42, 0x910, 0),
	MTK_PIN_IES_SMT_SPEC(43, 44, 0x900, 11),
	MTK_PIN_IES_SMT_SPEC(45, 46, 0x900, 12),
	MTK_PIN_IES_SMT_SPEC(57, 64, 0xc20, 14),
	MTK_PIN_IES_SMT_SPEC(65, 65, 0xc10, 14),
	MTK_PIN_IES_SMT_SPEC(66, 66, 0xc00, 14),
	MTK_PIN_IES_SMT_SPEC(67, 67, 0xd10, 14),
	MTK_PIN_IES_SMT_SPEC(68, 68, 0xd00, 14),
	MTK_PIN_IES_SMT_SPEC(69, 72, 0x910, 14),
	MTK_PIN_IES_SMT_SPEC(73, 76, 0xc60, 14),
	MTK_PIN_IES_SMT_SPEC(77, 77, 0xc40, 14),
	MTK_PIN_IES_SMT_SPEC(78, 78, 0xc50, 14),
	MTK_PIN_IES_SMT_SPEC(79, 82, 0x910, 15),
	MTK_PIN_IES_SMT_SPEC(83, 83, 0x920, 0),
	MTK_PIN_IES_SMT_SPEC(84, 85, 0x920, 1),
	MTK_PIN_IES_SMT_SPEC(86, 91, 0x920, 2),
	MTK_PIN_IES_SMT_SPEC(92, 92, 0x900, 13),
	MTK_PIN_IES_SMT_SPEC(93, 95, 0x900, 14),
	MTK_PIN_IES_SMT_SPEC(96, 99, 0x900, 15),
	MTK_PIN_IES_SMT_SPEC(100, 103, 0xca0, 14),
	MTK_PIN_IES_SMT_SPEC(104, 104, 0xc80, 14),
	MTK_PIN_IES_SMT_SPEC(105, 105, 0xc90, 14),
	MTK_PIN_IES_SMT_SPEC(106, 107, 0x910, 4),
	MTK_PIN_IES_SMT_SPEC(108, 112, 0x910, 1),
	MTK_PIN_IES_SMT_SPEC(113, 116, 0x910, 2),
	MTK_PIN_IES_SMT_SPEC(117, 118, 0x910, 5),
	MTK_PIN_IES_SMT_SPEC(119, 124, 0x910, 6),
	MTK_PIN_IES_SMT_SPEC(125, 126, 0x910, 7),
	MTK_PIN_IES_SMT_SPEC(127, 127, 0x910, 0),
	MTK_PIN_IES_SMT_SPEC(128, 128, 0x920, 8),
	MTK_PIN_IES_SMT_SPEC(129, 130, 0x920, 9),
	MTK_PIN_IES_SMT_SPEC(131, 132, 0x920, 8),
	MTK_PIN_IES_SMT_SPEC(133, 134, 0x910, 8)
};

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
	.spec_ies = mt8173_ies_set,
	.n_spec_ies = ARRAY_SIZE(mt8173_ies_set),
	.spec_pupd = mt8173_spec_pupd,
	.n_spec_pupd = ARRAY_SIZE(mt8173_spec_pupd),
	.spec_smt = mt8173_smt_set,
	.n_spec_smt = ARRAY_SIZE(mt8173_smt_set),
	.spec_pull_set = mtk_pctrl_spec_pull_set_samereg,
	.spec_ies_smt_set = mtk_pconf_spec_set_ies_smt_range,
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
	.mode_mask = 0xf,
	.mode_per_reg = 5,
	.mode_shf = 4,
	.eint_hw = {
		.port_mask = 7,
		.ports     = 6,
		.ap_num    = 224,
		.db_cnt    = 16,
		.db_time   = debounce_time_mt2701,
	},
};

static int mt8173_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_pctrl_init(pdev, &mt8173_pinctrl_data, NULL);
}

static const struct of_device_id mt8173_pctrl_match[] = {
	{
		.compatible = "mediatek,mt8173-pinctrl",
	},
	{ }
};

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mt8173_pinctrl_probe,
	.driver = {
		.name = "mediatek-mt8173-pinctrl",
		.of_match_table = mt8173_pctrl_match,
		.pm = &mtk_eint_pm_ops,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}
arch_initcall(mtk_pinctrl_init);
