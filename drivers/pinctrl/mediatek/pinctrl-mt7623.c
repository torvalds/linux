// SPDX-License-Identifier: GPL-2.0
/*
 * The MT7623 driver based on Linux generic pinctrl binding.
 *
 * Copyright (C) 2015 - 2018 MediaTek Inc.
 * Author: Biao Huang <biao.huang@mediatek.com>
 *	   Ryder Lee <ryder.lee@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include "pinctrl-moore.h"

#define PIN_BOND_REG0		0xb10
#define PIN_BOND_REG1		0xf20
#define PIN_BOND_REG2		0xef0
#define BOND_PCIE_CLR		(0x77 << 3)
#define BOND_I2S_CLR		0x3
#define BOND_MSDC0E_CLR		0x1

#define PIN_FIELD15(_s_pin, _e_pin, _s_addr, _x_addrs, _s_bit, _x_bits)	\
	PIN_FIELD_CALC(_s_pin, _e_pin, 0, _s_addr, _x_addrs, _s_bit,	\
		       _x_bits, 15, false)

#define PIN_FIELD16(_s_pin, _e_pin, _s_addr, _x_addrs, _s_bit, _x_bits)	\
	PIN_FIELD_CALC(_s_pin, _e_pin, 0, _s_addr, _x_addrs, _s_bit,	\
		       _x_bits, 16, 0)

#define PINS_FIELD16(_s_pin, _e_pin, _s_addr, _x_addrs, _s_bit, _x_bits)	\
	PIN_FIELD_CALC(_s_pin, _e_pin, 0, _s_addr, _x_addrs, _s_bit,	\
		       _x_bits, 16, 1)

#define MT7623_PIN(_number, _name, _eint_n, _drv_grp)			\
	MTK_PIN(_number, _name, 0, _eint_n, _drv_grp)

static const struct mtk_pin_field_calc mt7623_pin_mode_range[] = {
	PIN_FIELD15(0, 278, 0x760, 0x10, 0, 3),
};

static const struct mtk_pin_field_calc mt7623_pin_dir_range[] = {
	PIN_FIELD16(0, 175, 0x0, 0x10, 0, 1),
	PIN_FIELD16(176, 278, 0xc0, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_di_range[] = {
	PIN_FIELD16(0, 278, 0x630, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_do_range[] = {
	PIN_FIELD16(0, 278, 0x500, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_ies_range[] = {
	PINS_FIELD16(0, 6, 0xb20, 0x10, 0, 1),
	PINS_FIELD16(7, 9, 0xb20, 0x10, 1, 1),
	PINS_FIELD16(10, 13, 0xb30, 0x10, 3, 1),
	PINS_FIELD16(14, 15, 0xb30, 0x10, 13, 1),
	PINS_FIELD16(16, 17, 0xb40, 0x10, 7, 1),
	PINS_FIELD16(18, 29, 0xb40, 0x10, 13, 1),
	PINS_FIELD16(30, 32, 0xb40, 0x10, 7, 1),
	PINS_FIELD16(33, 37, 0xb40, 0x10, 13, 1),
	PIN_FIELD16(38, 38, 0xb20, 0x10, 13, 1),
	PINS_FIELD16(39, 42, 0xb40, 0x10, 13, 1),
	PINS_FIELD16(43, 45, 0xb20, 0x10, 10, 1),
	PINS_FIELD16(47, 48, 0xb20, 0x10, 11, 1),
	PIN_FIELD16(49, 49, 0xb20, 0x10, 12, 1),
	PINS_FIELD16(50, 52, 0xb20, 0x10, 13, 1),
	PINS_FIELD16(53, 56, 0xb20, 0x10, 14, 1),
	PINS_FIELD16(57, 58, 0xb20, 0x10, 15, 1),
	PIN_FIELD16(59, 59, 0xb30, 0x10, 10, 1),
	PINS_FIELD16(60, 62, 0xb30, 0x10, 0, 1),
	PINS_FIELD16(63, 65, 0xb30, 0x10, 1, 1),
	PINS_FIELD16(66, 71, 0xb30, 0x10, 2, 1),
	PINS_FIELD16(72, 74, 0xb20, 0x10, 12, 1),
	PINS_FIELD16(75, 76, 0xb30, 0x10, 3, 1),
	PINS_FIELD16(77, 78, 0xb30, 0x10, 4, 1),
	PINS_FIELD16(79, 82, 0xb30, 0x10, 5, 1),
	PINS_FIELD16(83, 84, 0xb30, 0x10, 2, 1),
	PIN_FIELD16(85, 85, 0xda0, 0x10, 4, 1),
	PIN_FIELD16(86, 86, 0xd90, 0x10, 4, 1),
	PINS_FIELD16(87, 90, 0xdb0, 0x10, 4, 1),
	PINS_FIELD16(101, 104, 0xb30, 0x10, 6, 1),
	PIN_FIELD16(105, 105, 0xd40, 0x10, 4, 1),
	PIN_FIELD16(106, 106, 0xd30, 0x10, 4, 1),
	PINS_FIELD16(107, 110, 0xd50, 0x10, 4, 1),
	PINS_FIELD16(111, 115, 0xce0, 0x10, 4, 1),
	PIN_FIELD16(116, 116, 0xcd0, 0x10, 4, 1),
	PIN_FIELD16(117, 117, 0xcc0, 0x10, 4, 1),
	PINS_FIELD16(118, 121, 0xce0, 0x10, 4, 1),
	PINS_FIELD16(122, 125, 0xb30, 0x10, 7, 1),
	PIN_FIELD16(126, 126, 0xb20, 0x10, 12, 1),
	PINS_FIELD16(127, 142, 0xb30, 0x10, 9, 1),
	PINS_FIELD16(143, 160, 0xb30, 0x10, 10, 1),
	PINS_FIELD16(161, 168, 0xb30, 0x10, 12, 1),
	PINS_FIELD16(169, 183, 0xb30, 0x10, 10, 1),
	PINS_FIELD16(184, 186, 0xb30, 0x10, 9, 1),
	PIN_FIELD16(187, 187, 0xb30, 0x10, 14, 1),
	PIN_FIELD16(188, 188, 0xb20, 0x10, 13, 1),
	PINS_FIELD16(189, 193, 0xb30, 0x10, 15, 1),
	PINS_FIELD16(194, 198, 0xb40, 0x10, 0, 1),
	PIN_FIELD16(199, 199, 0xb20, 0x10, 1, 1),
	PINS_FIELD16(200, 202, 0xb40, 0x10, 1, 1),
	PINS_FIELD16(203, 207, 0xb40, 0x10, 2, 1),
	PINS_FIELD16(208, 209, 0xb40, 0x10, 3, 1),
	PIN_FIELD16(210, 210, 0xb40, 0x10, 4, 1),
	PINS_FIELD16(211, 235, 0xb40, 0x10, 5, 1),
	PINS_FIELD16(236, 241, 0xb40, 0x10, 6, 1),
	PINS_FIELD16(242, 243, 0xb40, 0x10, 7, 1),
	PINS_FIELD16(244, 247, 0xb40, 0x10, 8, 1),
	PIN_FIELD16(248, 248, 0xb40, 0x10, 9, 1),
	PINS_FIELD16(249, 257, 0xfc0, 0x10, 4, 1),
	PIN_FIELD16(258, 258, 0xcb0, 0x10, 4, 1),
	PIN_FIELD16(259, 259, 0xc90, 0x10, 4, 1),
	PIN_FIELD16(260, 260, 0x3a0, 0x10, 4, 1),
	PIN_FIELD16(261, 261, 0xd50, 0x10, 4, 1),
	PINS_FIELD16(262, 277, 0xb40, 0x10, 12, 1),
	PIN_FIELD16(278, 278, 0xb40, 0x10, 13, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_smt_range[] = {
	PINS_FIELD16(0, 6, 0xb50, 0x10, 0, 1),
	PINS_FIELD16(7, 9, 0xb50, 0x10, 1, 1),
	PINS_FIELD16(10, 13, 0xb60, 0x10, 3, 1),
	PINS_FIELD16(14, 15, 0xb60, 0x10, 13, 1),
	PINS_FIELD16(16, 17, 0xb70, 0x10, 7, 1),
	PINS_FIELD16(18, 29, 0xb70, 0x10, 13, 1),
	PINS_FIELD16(30, 32, 0xb70, 0x10, 7, 1),
	PINS_FIELD16(33, 37, 0xb70, 0x10, 13, 1),
	PIN_FIELD16(38, 38, 0xb50, 0x10, 13, 1),
	PINS_FIELD16(39, 42, 0xb70, 0x10, 13, 1),
	PINS_FIELD16(43, 45, 0xb50, 0x10, 10, 1),
	PINS_FIELD16(47, 48, 0xb50, 0x10, 11, 1),
	PIN_FIELD16(49, 49, 0xb50, 0x10, 12, 1),
	PINS_FIELD16(50, 52, 0xb50, 0x10, 13, 1),
	PINS_FIELD16(53, 56, 0xb50, 0x10, 14, 1),
	PINS_FIELD16(57, 58, 0xb50, 0x10, 15, 1),
	PIN_FIELD16(59, 59, 0xb60, 0x10, 10, 1),
	PINS_FIELD16(60, 62, 0xb60, 0x10, 0, 1),
	PINS_FIELD16(63, 65, 0xb60, 0x10, 1, 1),
	PINS_FIELD16(66, 71, 0xb60, 0x10, 2, 1),
	PINS_FIELD16(72, 74, 0xb50, 0x10, 12, 1),
	PINS_FIELD16(75, 76, 0xb60, 0x10, 3, 1),
	PINS_FIELD16(77, 78, 0xb60, 0x10, 4, 1),
	PINS_FIELD16(79, 82, 0xb60, 0x10, 5, 1),
	PINS_FIELD16(83, 84, 0xb60, 0x10, 2, 1),
	PIN_FIELD16(85, 85, 0xda0, 0x10, 11, 1),
	PIN_FIELD16(86, 86, 0xd90, 0x10, 11, 1),
	PIN_FIELD16(87, 87, 0xdc0, 0x10, 3, 1),
	PIN_FIELD16(88, 88, 0xdc0, 0x10, 7, 1),
	PIN_FIELD16(89, 89, 0xdc0, 0x10, 11, 1),
	PIN_FIELD16(90, 90, 0xdc0, 0x10, 15, 1),
	PINS_FIELD16(101, 104, 0xb60, 0x10, 6, 1),
	PIN_FIELD16(105, 105, 0xd40, 0x10, 11, 1),
	PIN_FIELD16(106, 106, 0xd30, 0x10, 11, 1),
	PIN_FIELD16(107, 107, 0xd60, 0x10, 3, 1),
	PIN_FIELD16(108, 108, 0xd60, 0x10, 7, 1),
	PIN_FIELD16(109, 109, 0xd60, 0x10, 11, 1),
	PIN_FIELD16(110, 110, 0xd60, 0x10, 15, 1),
	PIN_FIELD16(111, 111, 0xd00, 0x10, 15, 1),
	PIN_FIELD16(112, 112, 0xd00, 0x10, 11, 1),
	PIN_FIELD16(113, 113, 0xd00, 0x10, 7, 1),
	PIN_FIELD16(114, 114, 0xd00, 0x10, 3, 1),
	PIN_FIELD16(115, 115, 0xd10, 0x10, 3, 1),
	PIN_FIELD16(116, 116, 0xcd0, 0x10, 11, 1),
	PIN_FIELD16(117, 117, 0xcc0, 0x10, 11, 1),
	PIN_FIELD16(118, 118, 0xcf0, 0x10, 15, 1),
	PIN_FIELD16(119, 119, 0xcf0, 0x10, 7, 1),
	PIN_FIELD16(120, 120, 0xcf0, 0x10, 3, 1),
	PIN_FIELD16(121, 121, 0xcf0, 0x10, 7, 1),
	PINS_FIELD16(122, 125, 0xb60, 0x10, 7, 1),
	PIN_FIELD16(126, 126, 0xb50, 0x10, 12, 1),
	PINS_FIELD16(127, 142, 0xb60, 0x10, 9, 1),
	PINS_FIELD16(143, 160, 0xb60, 0x10, 10, 1),
	PINS_FIELD16(161, 168, 0xb60, 0x10, 12, 1),
	PINS_FIELD16(169, 183, 0xb60, 0x10, 10, 1),
	PINS_FIELD16(184, 186, 0xb60, 0x10, 9, 1),
	PIN_FIELD16(187, 187, 0xb60, 0x10, 14, 1),
	PIN_FIELD16(188, 188, 0xb50, 0x10, 13, 1),
	PINS_FIELD16(189, 193, 0xb60, 0x10, 15, 1),
	PINS_FIELD16(194, 198, 0xb70, 0x10, 0, 1),
	PIN_FIELD16(199, 199, 0xb50, 0x10, 1, 1),
	PINS_FIELD16(200, 202, 0xb70, 0x10, 1, 1),
	PINS_FIELD16(203, 207, 0xb70, 0x10, 2, 1),
	PINS_FIELD16(208, 209, 0xb70, 0x10, 3, 1),
	PIN_FIELD16(210, 210, 0xb70, 0x10, 4, 1),
	PINS_FIELD16(211, 235, 0xb70, 0x10, 5, 1),
	PINS_FIELD16(236, 241, 0xb70, 0x10, 6, 1),
	PINS_FIELD16(242, 243, 0xb70, 0x10, 7, 1),
	PINS_FIELD16(244, 247, 0xb70, 0x10, 8, 1),
	PIN_FIELD16(248, 248, 0xb70, 0x10, 9, 10),
	PIN_FIELD16(249, 249, 0x140, 0x10, 3, 1),
	PIN_FIELD16(250, 250, 0x130, 0x10, 15, 1),
	PIN_FIELD16(251, 251, 0x130, 0x10, 11, 1),
	PIN_FIELD16(252, 252, 0x130, 0x10, 7, 1),
	PIN_FIELD16(253, 253, 0x130, 0x10, 3, 1),
	PIN_FIELD16(254, 254, 0xf40, 0x10, 15, 1),
	PIN_FIELD16(255, 255, 0xf40, 0x10, 11, 1),
	PIN_FIELD16(256, 256, 0xf40, 0x10, 7, 1),
	PIN_FIELD16(257, 257, 0xf40, 0x10, 3, 1),
	PIN_FIELD16(258, 258, 0xcb0, 0x10, 11, 1),
	PIN_FIELD16(259, 259, 0xc90, 0x10, 11, 1),
	PIN_FIELD16(260, 260, 0x3a0, 0x10, 11, 1),
	PIN_FIELD16(261, 261, 0x0b0, 0x10, 3, 1),
	PINS_FIELD16(262, 277, 0xb70, 0x10, 12, 1),
	PIN_FIELD16(278, 278, 0xb70, 0x10, 13, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_pullen_range[] = {
	PIN_FIELD16(0, 278, 0x150, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_pullsel_range[] = {
	PIN_FIELD16(0, 278, 0x280, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_drv_range[] = {
	PINS_FIELD16(0, 6, 0xf50, 0x10, 0, 4),
	PINS_FIELD16(7, 9, 0xf50, 0x10, 4, 4),
	PINS_FIELD16(10, 13, 0xf50, 0x10, 4, 4),
	PINS_FIELD16(14, 15, 0xf50, 0x10, 12, 4),
	PINS_FIELD16(16, 17, 0xf60, 0x10, 0, 4),
	PINS_FIELD16(18, 21, 0xf60, 0x10, 0, 4),
	PINS_FIELD16(22, 26, 0xf60, 0x10, 8, 4),
	PINS_FIELD16(27, 29, 0xf60, 0x10, 12, 4),
	PINS_FIELD16(30, 32, 0xf60, 0x10, 0, 4),
	PINS_FIELD16(33, 37, 0xf70, 0x10, 0, 4),
	PIN_FIELD16(38, 38, 0xf70, 0x10, 4, 4),
	PINS_FIELD16(39, 42, 0xf70, 0x10, 8, 4),
	PINS_FIELD16(43, 45, 0xf70, 0x10, 12, 4),
	PINS_FIELD16(47, 48, 0xf80, 0x10, 0, 4),
	PIN_FIELD16(49, 49, 0xf80, 0x10, 4, 4),
	PINS_FIELD16(50, 52, 0xf70, 0x10, 4, 4),
	PINS_FIELD16(53, 56, 0xf80, 0x10, 12, 4),
	PINS_FIELD16(60, 62, 0xf90, 0x10, 8, 4),
	PINS_FIELD16(63, 65, 0xf90, 0x10, 12, 4),
	PINS_FIELD16(66, 71, 0xfa0, 0x10, 0, 4),
	PINS_FIELD16(72, 74, 0xf80, 0x10, 4, 4),
	PIN_FIELD16(85, 85, 0xda0, 0x10, 0, 4),
	PIN_FIELD16(86, 86, 0xd90, 0x10, 0, 4),
	PINS_FIELD16(87, 90, 0xdb0, 0x10, 0, 4),
	PIN_FIELD16(105, 105, 0xd40, 0x10, 0, 4),
	PIN_FIELD16(106, 106, 0xd30, 0x10, 0, 4),
	PINS_FIELD16(107, 110, 0xd50, 0x10, 0, 4),
	PINS_FIELD16(111, 115, 0xce0, 0x10, 0, 4),
	PIN_FIELD16(116, 116, 0xcd0, 0x10, 0, 4),
	PIN_FIELD16(117, 117, 0xcc0, 0x10, 0, 4),
	PINS_FIELD16(118, 121, 0xce0, 0x10, 0, 4),
	PIN_FIELD16(126, 126, 0xf80, 0x10, 4, 4),
	PIN_FIELD16(188, 188, 0xf70, 0x10, 4, 4),
	PINS_FIELD16(189, 193, 0xfe0, 0x10, 8, 4),
	PINS_FIELD16(194, 198, 0xfe0, 0x10, 12, 4),
	PIN_FIELD16(199, 199, 0xf50, 0x10, 4, 4),
	PINS_FIELD16(200, 202, 0xfd0, 0x10, 0, 4),
	PINS_FIELD16(203, 207, 0xfd0, 0x10, 4, 4),
	PINS_FIELD16(208, 209, 0xfd0, 0x10, 8, 4),
	PIN_FIELD16(210, 210, 0xfd0, 0x10, 12, 4),
	PINS_FIELD16(211, 235, 0xff0, 0x10, 0, 4),
	PINS_FIELD16(236, 241, 0xff0, 0x10, 4, 4),
	PINS_FIELD16(242, 243, 0xff0, 0x10, 8, 4),
	PIN_FIELD16(248, 248, 0xf00, 0x10, 0, 4),
	PINS_FIELD16(249, 256, 0xfc0, 0x10, 0, 4),
	PIN_FIELD16(257, 257, 0xce0, 0x10, 0, 4),
	PIN_FIELD16(258, 258, 0xcb0, 0x10, 0, 4),
	PIN_FIELD16(259, 259, 0xc90, 0x10, 0, 4),
	PIN_FIELD16(260, 260, 0x3a0, 0x10, 0, 4),
	PIN_FIELD16(261, 261, 0xd50, 0x10, 0, 4),
	PINS_FIELD16(262, 277, 0xf00, 0x10, 8, 4),
	PIN_FIELD16(278, 278, 0xf70, 0x10, 8, 4),
};

static const struct mtk_pin_field_calc mt7623_pin_tdsel_range[] = {
	PINS_FIELD16(262, 276, 0x4c0, 0x10, 0, 4),
};

static const struct mtk_pin_field_calc mt7623_pin_pupd_range[] = {
	/* MSDC0 */
	PIN_FIELD16(111, 111, 0xd00, 0x10, 12, 1),
	PIN_FIELD16(112, 112, 0xd00, 0x10, 8, 1),
	PIN_FIELD16(113, 113, 0xd00, 0x10, 4, 1),
	PIN_FIELD16(114, 114, 0xd00, 0x10, 0, 1),
	PIN_FIELD16(115, 115, 0xd10, 0x10, 0, 1),
	PIN_FIELD16(116, 116, 0xcd0, 0x10, 8, 1),
	PIN_FIELD16(117, 117, 0xcc0, 0x10, 8, 1),
	PIN_FIELD16(118, 118, 0xcf0, 0x10, 12, 1),
	PIN_FIELD16(119, 119, 0xcf0, 0x10, 8, 1),
	PIN_FIELD16(120, 120, 0xcf0, 0x10, 4, 1),
	PIN_FIELD16(121, 121, 0xcf0, 0x10, 0, 1),
	/* MSDC1 */
	PIN_FIELD16(105, 105, 0xd40, 0x10, 8, 1),
	PIN_FIELD16(106, 106, 0xd30, 0x10, 8, 1),
	PIN_FIELD16(107, 107, 0xd60, 0x10, 0, 1),
	PIN_FIELD16(108, 108, 0xd60, 0x10, 10, 1),
	PIN_FIELD16(109, 109, 0xd60, 0x10, 4, 1),
	PIN_FIELD16(110, 110, 0xc60, 0x10, 12, 1),
	/* MSDC1 */
	PIN_FIELD16(85, 85, 0xda0, 0x10, 8, 1),
	PIN_FIELD16(86, 86, 0xd90, 0x10, 8, 1),
	PIN_FIELD16(87, 87, 0xdc0, 0x10, 0, 1),
	PIN_FIELD16(88, 88, 0xdc0, 0x10, 10, 1),
	PIN_FIELD16(89, 89, 0xdc0, 0x10, 4, 1),
	PIN_FIELD16(90, 90, 0xdc0, 0x10, 12, 1),
	/* MSDC0E */
	PIN_FIELD16(249, 249, 0x140, 0x10, 0, 1),
	PIN_FIELD16(250, 250, 0x130, 0x10, 12, 1),
	PIN_FIELD16(251, 251, 0x130, 0x10, 8, 1),
	PIN_FIELD16(252, 252, 0x130, 0x10, 4, 1),
	PIN_FIELD16(253, 253, 0x130, 0x10, 0, 1),
	PIN_FIELD16(254, 254, 0xf40, 0x10, 12, 1),
	PIN_FIELD16(255, 255, 0xf40, 0x10, 8, 1),
	PIN_FIELD16(256, 256, 0xf40, 0x10, 4, 1),
	PIN_FIELD16(257, 257, 0xf40, 0x10, 0, 1),
	PIN_FIELD16(258, 258, 0xcb0, 0x10, 8, 1),
	PIN_FIELD16(259, 259, 0xc90, 0x10, 8, 1),
	PIN_FIELD16(261, 261, 0x140, 0x10, 8, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_r1_range[] = {
	/* MSDC0 */
	PIN_FIELD16(111, 111, 0xd00, 0x10, 13, 1),
	PIN_FIELD16(112, 112, 0xd00, 0x10, 9, 1),
	PIN_FIELD16(113, 113, 0xd00, 0x10, 5, 1),
	PIN_FIELD16(114, 114, 0xd00, 0x10, 1, 1),
	PIN_FIELD16(115, 115, 0xd10, 0x10, 1, 1),
	PIN_FIELD16(116, 116, 0xcd0, 0x10, 9, 1),
	PIN_FIELD16(117, 117, 0xcc0, 0x10, 9, 1),
	PIN_FIELD16(118, 118, 0xcf0, 0x10, 13, 1),
	PIN_FIELD16(119, 119, 0xcf0, 0x10, 9, 1),
	PIN_FIELD16(120, 120, 0xcf0, 0x10, 5, 1),
	PIN_FIELD16(121, 121, 0xcf0, 0x10, 1, 1),
	/* MSDC1 */
	PIN_FIELD16(105, 105, 0xd40, 0x10, 9, 1),
	PIN_FIELD16(106, 106, 0xd30, 0x10, 9, 1),
	PIN_FIELD16(107, 107, 0xd60, 0x10, 1, 1),
	PIN_FIELD16(108, 108, 0xd60, 0x10, 9, 1),
	PIN_FIELD16(109, 109, 0xd60, 0x10, 5, 1),
	PIN_FIELD16(110, 110, 0xc60, 0x10, 13, 1),
	/* MSDC2 */
	PIN_FIELD16(85, 85, 0xda0, 0x10, 9, 1),
	PIN_FIELD16(86, 86, 0xd90, 0x10, 9, 1),
	PIN_FIELD16(87, 87, 0xdc0, 0x10, 1, 1),
	PIN_FIELD16(88, 88, 0xdc0, 0x10, 9, 1),
	PIN_FIELD16(89, 89, 0xdc0, 0x10, 5, 1),
	PIN_FIELD16(90, 90, 0xdc0, 0x10, 13, 1),
	/* MSDC0E */
	PIN_FIELD16(249, 249, 0x140, 0x10, 1, 1),
	PIN_FIELD16(250, 250, 0x130, 0x10, 13, 1),
	PIN_FIELD16(251, 251, 0x130, 0x10, 9, 1),
	PIN_FIELD16(252, 252, 0x130, 0x10, 5, 1),
	PIN_FIELD16(253, 253, 0x130, 0x10, 1, 1),
	PIN_FIELD16(254, 254, 0xf40, 0x10, 13, 1),
	PIN_FIELD16(255, 255, 0xf40, 0x10, 9, 1),
	PIN_FIELD16(256, 256, 0xf40, 0x10, 5, 1),
	PIN_FIELD16(257, 257, 0xf40, 0x10, 1, 1),
	PIN_FIELD16(258, 258, 0xcb0, 0x10, 9, 1),
	PIN_FIELD16(259, 259, 0xc90, 0x10, 9, 1),
	PIN_FIELD16(261, 261, 0x140, 0x10, 9, 1),
};

static const struct mtk_pin_field_calc mt7623_pin_r0_range[] = {
	/* MSDC0 */
	PIN_FIELD16(111, 111, 0xd00, 0x10, 14, 1),
	PIN_FIELD16(112, 112, 0xd00, 0x10, 10, 1),
	PIN_FIELD16(113, 113, 0xd00, 0x10, 6, 1),
	PIN_FIELD16(114, 114, 0xd00, 0x10, 2, 1),
	PIN_FIELD16(115, 115, 0xd10, 0x10, 2, 1),
	PIN_FIELD16(116, 116, 0xcd0, 0x10, 10, 1),
	PIN_FIELD16(117, 117, 0xcc0, 0x10, 10, 1),
	PIN_FIELD16(118, 118, 0xcf0, 0x10, 14, 1),
	PIN_FIELD16(119, 119, 0xcf0, 0x10, 10, 1),
	PIN_FIELD16(120, 120, 0xcf0, 0x10, 6, 1),
	PIN_FIELD16(121, 121, 0xcf0, 0x10, 2, 1),
	/* MSDC1 */
	PIN_FIELD16(105, 105, 0xd40, 0x10, 10, 1),
	PIN_FIELD16(106, 106, 0xd30, 0x10, 10, 1),
	PIN_FIELD16(107, 107, 0xd60, 0x10, 2, 1),
	PIN_FIELD16(108, 108, 0xd60, 0x10, 8, 1),
	PIN_FIELD16(109, 109, 0xd60, 0x10, 6, 1),
	PIN_FIELD16(110, 110, 0xc60, 0x10, 14, 1),
	/* MSDC2 */
	PIN_FIELD16(85, 85, 0xda0, 0x10, 10, 1),
	PIN_FIELD16(86, 86, 0xd90, 0x10, 10, 1),
	PIN_FIELD16(87, 87, 0xdc0, 0x10, 2, 1),
	PIN_FIELD16(88, 88, 0xdc0, 0x10, 8, 1),
	PIN_FIELD16(89, 89, 0xdc0, 0x10, 6, 1),
	PIN_FIELD16(90, 90, 0xdc0, 0x10, 14, 1),
	/* MSDC0E */
	PIN_FIELD16(249, 249, 0x140, 0x10, 2, 1),
	PIN_FIELD16(250, 250, 0x130, 0x10, 14, 1),
	PIN_FIELD16(251, 251, 0x130, 0x10, 10, 1),
	PIN_FIELD16(252, 252, 0x130, 0x10, 6, 1),
	PIN_FIELD16(253, 253, 0x130, 0x10, 2, 1),
	PIN_FIELD16(254, 254, 0xf40, 0x10, 14, 1),
	PIN_FIELD16(255, 255, 0xf40, 0x10, 10, 1),
	PIN_FIELD16(256, 256, 0xf40, 0x10, 6, 1),
	PIN_FIELD16(257, 257, 0xf40, 0x10, 5, 1),
	PIN_FIELD16(258, 258, 0xcb0, 0x10, 10, 1),
	PIN_FIELD16(259, 259, 0xc90, 0x10, 10, 1),
	PIN_FIELD16(261, 261, 0x140, 0x10, 10, 1),
};

static const struct mtk_pin_reg_calc mt7623_reg_cals[] = {
	[PINCTRL_PIN_REG_MODE] = MTK_RANGE(mt7623_pin_mode_range),
	[PINCTRL_PIN_REG_DIR] = MTK_RANGE(mt7623_pin_dir_range),
	[PINCTRL_PIN_REG_DI] = MTK_RANGE(mt7623_pin_di_range),
	[PINCTRL_PIN_REG_DO] = MTK_RANGE(mt7623_pin_do_range),
	[PINCTRL_PIN_REG_SMT] = MTK_RANGE(mt7623_pin_smt_range),
	[PINCTRL_PIN_REG_PULLSEL] = MTK_RANGE(mt7623_pin_pullsel_range),
	[PINCTRL_PIN_REG_PULLEN] = MTK_RANGE(mt7623_pin_pullen_range),
	[PINCTRL_PIN_REG_DRV] = MTK_RANGE(mt7623_pin_drv_range),
	[PINCTRL_PIN_REG_TDSEL] = MTK_RANGE(mt7623_pin_tdsel_range),
	[PINCTRL_PIN_REG_IES] = MTK_RANGE(mt7623_pin_ies_range),
	[PINCTRL_PIN_REG_PUPD] = MTK_RANGE(mt7623_pin_pupd_range),
	[PINCTRL_PIN_REG_R0] = MTK_RANGE(mt7623_pin_r0_range),
	[PINCTRL_PIN_REG_R1] = MTK_RANGE(mt7623_pin_r1_range),
};

static const struct mtk_pin_desc mt7623_pins[] = {
	MT7623_PIN(0, "PWRAP_SPI0_MI", 148, DRV_GRP3),
	MT7623_PIN(1, "PWRAP_SPI0_MO", 149, DRV_GRP3),
	MT7623_PIN(2, "PWRAP_INT", 150, DRV_GRP3),
	MT7623_PIN(3, "PWRAP_SPI0_CK", 151, DRV_GRP3),
	MT7623_PIN(4, "PWRAP_SPI0_CSN", 152, DRV_GRP3),
	MT7623_PIN(5, "PWRAP_SPI0_CK2", 153, DRV_GRP3),
	MT7623_PIN(6, "PWRAP_SPI0_CSN2", 154, DRV_GRP3),
	MT7623_PIN(7, "SPI1_CSN", 155, DRV_GRP3),
	MT7623_PIN(8, "SPI1_MI", 156, DRV_GRP3),
	MT7623_PIN(9, "SPI1_MO", 157, DRV_GRP3),
	MT7623_PIN(10, "RTC32K_CK", 158, DRV_GRP3),
	MT7623_PIN(11, "WATCHDOG", 159, DRV_GRP3),
	MT7623_PIN(12, "SRCLKENA", 160, DRV_GRP3),
	MT7623_PIN(13, "SRCLKENAI", 161, DRV_GRP3),
	MT7623_PIN(14, "URXD2", 162, DRV_GRP1),
	MT7623_PIN(15, "UTXD2", 163, DRV_GRP1),
	MT7623_PIN(16, "I2S5_DATA_IN", 164, DRV_GRP1),
	MT7623_PIN(17, "I2S5_BCK", 165, DRV_GRP1),
	MT7623_PIN(18, "PCM_CLK", 166, DRV_GRP1),
	MT7623_PIN(19, "PCM_SYNC", 167, DRV_GRP1),
	MT7623_PIN(20, "PCM_RX", EINT_NA, DRV_GRP1),
	MT7623_PIN(21, "PCM_TX", EINT_NA, DRV_GRP1),
	MT7623_PIN(22, "EINT0", 0, DRV_GRP1),
	MT7623_PIN(23, "EINT1", 1, DRV_GRP1),
	MT7623_PIN(24, "EINT2", 2, DRV_GRP1),
	MT7623_PIN(25, "EINT3", 3, DRV_GRP1),
	MT7623_PIN(26, "EINT4", 4, DRV_GRP1),
	MT7623_PIN(27, "EINT5", 5, DRV_GRP1),
	MT7623_PIN(28, "EINT6", 6, DRV_GRP1),
	MT7623_PIN(29, "EINT7", 7, DRV_GRP1),
	MT7623_PIN(30, "I2S5_LRCK", 12, DRV_GRP1),
	MT7623_PIN(31, "I2S5_MCLK", 13, DRV_GRP1),
	MT7623_PIN(32, "I2S5_DATA", 14, DRV_GRP1),
	MT7623_PIN(33, "I2S1_DATA", 15, DRV_GRP1),
	MT7623_PIN(34, "I2S1_DATA_IN", 16, DRV_GRP1),
	MT7623_PIN(35, "I2S1_BCK", 17, DRV_GRP1),
	MT7623_PIN(36, "I2S1_LRCK", 18, DRV_GRP1),
	MT7623_PIN(37, "I2S1_MCLK", 19, DRV_GRP1),
	MT7623_PIN(38, "I2S2_DATA", 20, DRV_GRP1),
	MT7623_PIN(39, "JTMS", 21, DRV_GRP3),
	MT7623_PIN(40, "JTCK", 22, DRV_GRP3),
	MT7623_PIN(41, "JTDI", 23, DRV_GRP3),
	MT7623_PIN(42, "JTDO", 24, DRV_GRP3),
	MT7623_PIN(43, "NCLE", 25, DRV_GRP1),
	MT7623_PIN(44, "NCEB1", 26, DRV_GRP1),
	MT7623_PIN(45, "NCEB0", 27, DRV_GRP1),
	MT7623_PIN(46, "IR", 28, DRV_FIXED),
	MT7623_PIN(47, "NREB", 29, DRV_GRP1),
	MT7623_PIN(48, "NRNB", 30, DRV_GRP1),
	MT7623_PIN(49, "I2S0_DATA", 31, DRV_GRP1),
	MT7623_PIN(50, "I2S2_BCK", 32, DRV_GRP1),
	MT7623_PIN(51, "I2S2_DATA_IN", 33, DRV_GRP1),
	MT7623_PIN(52, "I2S2_LRCK", 34, DRV_GRP1),
	MT7623_PIN(53, "SPI0_CSN", 35, DRV_GRP1),
	MT7623_PIN(54, "SPI0_CK", 36, DRV_GRP1),
	MT7623_PIN(55, "SPI0_MI", 37, DRV_GRP1),
	MT7623_PIN(56, "SPI0_MO", 38, DRV_GRP1),
	MT7623_PIN(57, "SDA1", 39, DRV_FIXED),
	MT7623_PIN(58, "SCL1", 40, DRV_FIXED),
	MT7623_PIN(59, "RAMBUF_I_CLK", EINT_NA, DRV_FIXED),
	MT7623_PIN(60, "WB_RSTB", 41, DRV_GRP3),
	MT7623_PIN(61, "F2W_DATA", 42, DRV_GRP3),
	MT7623_PIN(62, "F2W_CLK", 43, DRV_GRP3),
	MT7623_PIN(63, "WB_SCLK", 44, DRV_GRP3),
	MT7623_PIN(64, "WB_SDATA", 45, DRV_GRP3),
	MT7623_PIN(65, "WB_SEN", 46, DRV_GRP3),
	MT7623_PIN(66, "WB_CRTL0", 47, DRV_GRP3),
	MT7623_PIN(67, "WB_CRTL1", 48, DRV_GRP3),
	MT7623_PIN(68, "WB_CRTL2", 49, DRV_GRP3),
	MT7623_PIN(69, "WB_CRTL3", 50, DRV_GRP3),
	MT7623_PIN(70, "WB_CRTL4", 51, DRV_GRP3),
	MT7623_PIN(71, "WB_CRTL5", 52, DRV_GRP3),
	MT7623_PIN(72, "I2S0_DATA_IN", 53, DRV_GRP1),
	MT7623_PIN(73, "I2S0_LRCK", 54, DRV_GRP1),
	MT7623_PIN(74, "I2S0_BCK", 55, DRV_GRP1),
	MT7623_PIN(75, "SDA0", 56, DRV_FIXED),
	MT7623_PIN(76, "SCL0", 57, DRV_FIXED),
	MT7623_PIN(77, "SDA2", 58, DRV_FIXED),
	MT7623_PIN(78, "SCL2", 59, DRV_FIXED),
	MT7623_PIN(79, "URXD0", 60, DRV_FIXED),
	MT7623_PIN(80, "UTXD0", 61, DRV_FIXED),
	MT7623_PIN(81, "URXD1", 62, DRV_FIXED),
	MT7623_PIN(82, "UTXD1", 63, DRV_FIXED),
	MT7623_PIN(83, "LCM_RST", 64, DRV_FIXED),
	MT7623_PIN(84, "DSI_TE", 65, DRV_FIXED),
	MT7623_PIN(85, "MSDC2_CMD", 66, DRV_GRP4),
	MT7623_PIN(86, "MSDC2_CLK", 67, DRV_GRP4),
	MT7623_PIN(87, "MSDC2_DAT0", 68, DRV_GRP4),
	MT7623_PIN(88, "MSDC2_DAT1", 69, DRV_GRP4),
	MT7623_PIN(89, "MSDC2_DAT2", 70, DRV_GRP4),
	MT7623_PIN(90, "MSDC2_DAT3", 71, DRV_GRP4),
	MT7623_PIN(91, "TDN3", EINT_NA, DRV_FIXED),
	MT7623_PIN(92, "TDP3", EINT_NA, DRV_FIXED),
	MT7623_PIN(93, "TDN2", EINT_NA, DRV_FIXED),
	MT7623_PIN(94, "TDP2", EINT_NA, DRV_FIXED),
	MT7623_PIN(95, "TCN", EINT_NA, DRV_FIXED),
	MT7623_PIN(96, "TCP", EINT_NA, DRV_FIXED),
	MT7623_PIN(97, "TDN1", EINT_NA, DRV_FIXED),
	MT7623_PIN(98, "TDP1", EINT_NA, DRV_FIXED),
	MT7623_PIN(99, "TDN0", EINT_NA, DRV_FIXED),
	MT7623_PIN(100, "TDP0", EINT_NA, DRV_FIXED),
	MT7623_PIN(101, "SPI2_CSN", 74, DRV_FIXED),
	MT7623_PIN(102, "SPI2_MI", 75, DRV_FIXED),
	MT7623_PIN(103, "SPI2_MO", 76, DRV_FIXED),
	MT7623_PIN(104, "SPI2_CLK", 77, DRV_FIXED),
	MT7623_PIN(105, "MSDC1_CMD", 78, DRV_GRP4),
	MT7623_PIN(106, "MSDC1_CLK", 79, DRV_GRP4),
	MT7623_PIN(107, "MSDC1_DAT0", 80, DRV_GRP4),
	MT7623_PIN(108, "MSDC1_DAT1", 81, DRV_GRP4),
	MT7623_PIN(109, "MSDC1_DAT2", 82, DRV_GRP4),
	MT7623_PIN(110, "MSDC1_DAT3", 83, DRV_GRP4),
	MT7623_PIN(111, "MSDC0_DAT7", 84, DRV_GRP4),
	MT7623_PIN(112, "MSDC0_DAT6", 85, DRV_GRP4),
	MT7623_PIN(113, "MSDC0_DAT5", 86, DRV_GRP4),
	MT7623_PIN(114, "MSDC0_DAT4", 87, DRV_GRP4),
	MT7623_PIN(115, "MSDC0_RSTB", 88, DRV_GRP4),
	MT7623_PIN(116, "MSDC0_CMD", 89, DRV_GRP4),
	MT7623_PIN(117, "MSDC0_CLK", 90, DRV_GRP4),
	MT7623_PIN(118, "MSDC0_DAT3", 91, DRV_GRP4),
	MT7623_PIN(119, "MSDC0_DAT2", 92, DRV_GRP4),
	MT7623_PIN(120, "MSDC0_DAT1", 93, DRV_GRP4),
	MT7623_PIN(121, "MSDC0_DAT0", 94, DRV_GRP4),
	MT7623_PIN(122, "CEC", 95, DRV_FIXED),
	MT7623_PIN(123, "HTPLG", 96, DRV_FIXED),
	MT7623_PIN(124, "HDMISCK", 97, DRV_FIXED),
	MT7623_PIN(125, "HDMISD", 98, DRV_FIXED),
	MT7623_PIN(126, "I2S0_MCLK", 99, DRV_GRP1),
	MT7623_PIN(127, "RAMBUF_IDATA0", EINT_NA, DRV_FIXED),
	MT7623_PIN(128, "RAMBUF_IDATA1", EINT_NA, DRV_FIXED),
	MT7623_PIN(129, "RAMBUF_IDATA2", EINT_NA, DRV_FIXED),
	MT7623_PIN(130, "RAMBUF_IDATA3", EINT_NA, DRV_FIXED),
	MT7623_PIN(131, "RAMBUF_IDATA4", EINT_NA, DRV_FIXED),
	MT7623_PIN(132, "RAMBUF_IDATA5", EINT_NA, DRV_FIXED),
	MT7623_PIN(133, "RAMBUF_IDATA6", EINT_NA, DRV_FIXED),
	MT7623_PIN(134, "RAMBUF_IDATA7", EINT_NA, DRV_FIXED),
	MT7623_PIN(135, "RAMBUF_IDATA8", EINT_NA, DRV_FIXED),
	MT7623_PIN(136, "RAMBUF_IDATA9", EINT_NA, DRV_FIXED),
	MT7623_PIN(137, "RAMBUF_IDATA10", EINT_NA, DRV_FIXED),
	MT7623_PIN(138, "RAMBUF_IDATA11", EINT_NA, DRV_FIXED),
	MT7623_PIN(139, "RAMBUF_IDATA12", EINT_NA, DRV_FIXED),
	MT7623_PIN(140, "RAMBUF_IDATA13", EINT_NA, DRV_FIXED),
	MT7623_PIN(141, "RAMBUF_IDATA14", EINT_NA, DRV_FIXED),
	MT7623_PIN(142, "RAMBUF_IDATA15", EINT_NA, DRV_FIXED),
	MT7623_PIN(143, "RAMBUF_ODATA0", EINT_NA, DRV_FIXED),
	MT7623_PIN(144, "RAMBUF_ODATA1", EINT_NA, DRV_FIXED),
	MT7623_PIN(145, "RAMBUF_ODATA2", EINT_NA, DRV_FIXED),
	MT7623_PIN(146, "RAMBUF_ODATA3", EINT_NA, DRV_FIXED),
	MT7623_PIN(147, "RAMBUF_ODATA4", EINT_NA, DRV_FIXED),
	MT7623_PIN(148, "RAMBUF_ODATA5", EINT_NA, DRV_FIXED),
	MT7623_PIN(149, "RAMBUF_ODATA6", EINT_NA, DRV_FIXED),
	MT7623_PIN(150, "RAMBUF_ODATA7", EINT_NA, DRV_FIXED),
	MT7623_PIN(151, "RAMBUF_ODATA8", EINT_NA, DRV_FIXED),
	MT7623_PIN(152, "RAMBUF_ODATA9", EINT_NA, DRV_FIXED),
	MT7623_PIN(153, "RAMBUF_ODATA10", EINT_NA, DRV_FIXED),
	MT7623_PIN(154, "RAMBUF_ODATA11", EINT_NA, DRV_FIXED),
	MT7623_PIN(155, "RAMBUF_ODATA12", EINT_NA, DRV_FIXED),
	MT7623_PIN(156, "RAMBUF_ODATA13", EINT_NA, DRV_FIXED),
	MT7623_PIN(157, "RAMBUF_ODATA14", EINT_NA, DRV_FIXED),
	MT7623_PIN(158, "RAMBUF_ODATA15", EINT_NA, DRV_FIXED),
	MT7623_PIN(159, "RAMBUF_BE0", EINT_NA, DRV_FIXED),
	MT7623_PIN(160, "RAMBUF_BE1", EINT_NA, DRV_FIXED),
	MT7623_PIN(161, "AP2PT_INT", EINT_NA, DRV_FIXED),
	MT7623_PIN(162, "AP2PT_INT_CLR", EINT_NA, DRV_FIXED),
	MT7623_PIN(163, "PT2AP_INT", EINT_NA, DRV_FIXED),
	MT7623_PIN(164, "PT2AP_INT_CLR", EINT_NA, DRV_FIXED),
	MT7623_PIN(165, "AP2UP_INT", EINT_NA, DRV_FIXED),
	MT7623_PIN(166, "AP2UP_INT_CLR", EINT_NA, DRV_FIXED),
	MT7623_PIN(167, "UP2AP_INT", EINT_NA, DRV_FIXED),
	MT7623_PIN(168, "UP2AP_INT_CLR", EINT_NA, DRV_FIXED),
	MT7623_PIN(169, "RAMBUF_ADDR0", EINT_NA, DRV_FIXED),
	MT7623_PIN(170, "RAMBUF_ADDR1", EINT_NA, DRV_FIXED),
	MT7623_PIN(171, "RAMBUF_ADDR2", EINT_NA, DRV_FIXED),
	MT7623_PIN(172, "RAMBUF_ADDR3", EINT_NA, DRV_FIXED),
	MT7623_PIN(173, "RAMBUF_ADDR4", EINT_NA, DRV_FIXED),
	MT7623_PIN(174, "RAMBUF_ADDR5", EINT_NA, DRV_FIXED),
	MT7623_PIN(175, "RAMBUF_ADDR6", EINT_NA, DRV_FIXED),
	MT7623_PIN(176, "RAMBUF_ADDR7", EINT_NA, DRV_FIXED),
	MT7623_PIN(177, "RAMBUF_ADDR8", EINT_NA, DRV_FIXED),
	MT7623_PIN(178, "RAMBUF_ADDR9", EINT_NA, DRV_FIXED),
	MT7623_PIN(179, "RAMBUF_ADDR10", EINT_NA, DRV_FIXED),
	MT7623_PIN(180, "RAMBUF_RW", EINT_NA, DRV_FIXED),
	MT7623_PIN(181, "RAMBUF_LAST", EINT_NA, DRV_FIXED),
	MT7623_PIN(182, "RAMBUF_HP", EINT_NA, DRV_FIXED),
	MT7623_PIN(183, "RAMBUF_REQ", EINT_NA, DRV_FIXED),
	MT7623_PIN(184, "RAMBUF_ALE", EINT_NA, DRV_FIXED),
	MT7623_PIN(185, "RAMBUF_DLE", EINT_NA, DRV_FIXED),
	MT7623_PIN(186, "RAMBUF_WDLE", EINT_NA, DRV_FIXED),
	MT7623_PIN(187, "RAMBUF_O_CLK", EINT_NA, DRV_FIXED),
	MT7623_PIN(188, "I2S2_MCLK", 100, DRV_GRP1),
	MT7623_PIN(189, "I2S3_DATA", 101, DRV_GRP1),
	MT7623_PIN(190, "I2S3_DATA_IN", 102, DRV_GRP1),
	MT7623_PIN(191, "I2S3_BCK", 103, DRV_GRP1),
	MT7623_PIN(192, "I2S3_LRCK", 104, DRV_GRP1),
	MT7623_PIN(193, "I2S3_MCLK", 105, DRV_GRP1),
	MT7623_PIN(194, "I2S4_DATA", 106, DRV_GRP1),
	MT7623_PIN(195, "I2S4_DATA_IN", 107, DRV_GRP1),
	MT7623_PIN(196, "I2S4_BCK", 108, DRV_GRP1),
	MT7623_PIN(197, "I2S4_LRCK", 109, DRV_GRP1),
	MT7623_PIN(198, "I2S4_MCLK", 110, DRV_GRP1),
	MT7623_PIN(199, "SPI1_CLK", 111, DRV_GRP3),
	MT7623_PIN(200, "SPDIF_OUT", 112, DRV_GRP1),
	MT7623_PIN(201, "SPDIF_IN0", 113, DRV_GRP1),
	MT7623_PIN(202, "SPDIF_IN1", 114, DRV_GRP1),
	MT7623_PIN(203, "PWM0", 115, DRV_GRP1),
	MT7623_PIN(204, "PWM1", 116, DRV_GRP1),
	MT7623_PIN(205, "PWM2", 117, DRV_GRP1),
	MT7623_PIN(206, "PWM3", 118, DRV_GRP1),
	MT7623_PIN(207, "PWM4", 119, DRV_GRP1),
	MT7623_PIN(208, "AUD_EXT_CK1", 120, DRV_GRP1),
	MT7623_PIN(209, "AUD_EXT_CK2", 121, DRV_GRP1),
	MT7623_PIN(210, "AUD_CLOCK", EINT_NA, DRV_GRP3),
	MT7623_PIN(211, "DVP_RESET", EINT_NA, DRV_GRP3),
	MT7623_PIN(212, "DVP_CLOCK", EINT_NA, DRV_GRP3),
	MT7623_PIN(213, "DVP_CS", EINT_NA, DRV_GRP3),
	MT7623_PIN(214, "DVP_CK", EINT_NA, DRV_GRP3),
	MT7623_PIN(215, "DVP_DI", EINT_NA, DRV_GRP3),
	MT7623_PIN(216, "DVP_DO", EINT_NA, DRV_GRP3),
	MT7623_PIN(217, "AP_CS", EINT_NA, DRV_GRP3),
	MT7623_PIN(218, "AP_CK", EINT_NA, DRV_GRP3),
	MT7623_PIN(219, "AP_DI", EINT_NA, DRV_GRP3),
	MT7623_PIN(220, "AP_DO", EINT_NA, DRV_GRP3),
	MT7623_PIN(221, "DVD_BCLK", EINT_NA, DRV_GRP3),
	MT7623_PIN(222, "T8032_CLK", EINT_NA, DRV_GRP3),
	MT7623_PIN(223, "AP_BCLK", EINT_NA, DRV_GRP3),
	MT7623_PIN(224, "HOST_CS", EINT_NA, DRV_GRP3),
	MT7623_PIN(225, "HOST_CK", EINT_NA, DRV_GRP3),
	MT7623_PIN(226, "HOST_DO0", EINT_NA, DRV_GRP3),
	MT7623_PIN(227, "HOST_DO1", EINT_NA, DRV_GRP3),
	MT7623_PIN(228, "SLV_CS", EINT_NA, DRV_GRP3),
	MT7623_PIN(229, "SLV_CK", EINT_NA, DRV_GRP3),
	MT7623_PIN(230, "SLV_DI0", EINT_NA, DRV_GRP3),
	MT7623_PIN(231, "SLV_DI1", EINT_NA, DRV_GRP3),
	MT7623_PIN(232, "AP2DSP_INT", EINT_NA, DRV_GRP3),
	MT7623_PIN(233, "AP2DSP_INT_CLR", EINT_NA, DRV_GRP3),
	MT7623_PIN(234, "DSP2AP_INT", EINT_NA, DRV_GRP3),
	MT7623_PIN(235, "DSP2AP_INT_CLR", EINT_NA, DRV_GRP3),
	MT7623_PIN(236, "EXT_SDIO3", 122, DRV_GRP1),
	MT7623_PIN(237, "EXT_SDIO2", 123, DRV_GRP1),
	MT7623_PIN(238, "EXT_SDIO1", 124, DRV_GRP1),
	MT7623_PIN(239, "EXT_SDIO0", 125, DRV_GRP1),
	MT7623_PIN(240, "EXT_XCS", 126, DRV_GRP1),
	MT7623_PIN(241, "EXT_SCK", 127, DRV_GRP1),
	MT7623_PIN(242, "URTS2", 128, DRV_GRP1),
	MT7623_PIN(243, "UCTS2", 129, DRV_GRP1),
	MT7623_PIN(244, "HDMI_SDA_RX", 130, DRV_FIXED),
	MT7623_PIN(245, "HDMI_SCL_RX", 131, DRV_FIXED),
	MT7623_PIN(246, "MHL_SENCE", 132, DRV_FIXED),
	MT7623_PIN(247, "HDMI_HPD_CBUS_RX", 69, DRV_FIXED),
	MT7623_PIN(248, "HDMI_TESTOUTP_RX", 133, DRV_GRP1),
	MT7623_PIN(249, "MSDC0E_RSTB", 134, DRV_GRP4),
	MT7623_PIN(250, "MSDC0E_DAT7", 135, DRV_GRP4),
	MT7623_PIN(251, "MSDC0E_DAT6", 136, DRV_GRP4),
	MT7623_PIN(252, "MSDC0E_DAT5", 137, DRV_GRP4),
	MT7623_PIN(253, "MSDC0E_DAT4", 138, DRV_GRP4),
	MT7623_PIN(254, "MSDC0E_DAT3", 139, DRV_GRP4),
	MT7623_PIN(255, "MSDC0E_DAT2", 140, DRV_GRP4),
	MT7623_PIN(256, "MSDC0E_DAT1", 141, DRV_GRP4),
	MT7623_PIN(257, "MSDC0E_DAT0", 142, DRV_GRP4),
	MT7623_PIN(258, "MSDC0E_CMD", 143, DRV_GRP4),
	MT7623_PIN(259, "MSDC0E_CLK", 144, DRV_GRP4),
	MT7623_PIN(260, "MSDC0E_DSL", 145, DRV_GRP4),
	MT7623_PIN(261, "MSDC1_INS", 146, DRV_GRP4),
	MT7623_PIN(262, "G2_TXEN", 8, DRV_GRP1),
	MT7623_PIN(263, "G2_TXD3", 9, DRV_GRP1),
	MT7623_PIN(264, "G2_TXD2", 10, DRV_GRP1),
	MT7623_PIN(265, "G2_TXD1", 11, DRV_GRP1),
	MT7623_PIN(266, "G2_TXD0", EINT_NA, DRV_GRP1),
	MT7623_PIN(267, "G2_TXC", EINT_NA, DRV_GRP1),
	MT7623_PIN(268, "G2_RXC", EINT_NA, DRV_GRP1),
	MT7623_PIN(269, "G2_RXD0", EINT_NA, DRV_GRP1),
	MT7623_PIN(270, "G2_RXD1", EINT_NA, DRV_GRP1),
	MT7623_PIN(271, "G2_RXD2", EINT_NA, DRV_GRP1),
	MT7623_PIN(272, "G2_RXD3", EINT_NA, DRV_GRP1),
	MT7623_PIN(273, "ESW_INT", 168, DRV_GRP1),
	MT7623_PIN(274, "G2_RXDV", EINT_NA, DRV_GRP1),
	MT7623_PIN(275, "MDC", EINT_NA, DRV_GRP1),
	MT7623_PIN(276, "MDIO", EINT_NA, DRV_GRP1),
	MT7623_PIN(277, "ESW_RST", EINT_NA, DRV_GRP1),
	MT7623_PIN(278, "JTAG_RESET", 147, DRV_GRP3),
	MT7623_PIN(279, "USB3_RES_BOND", EINT_NA, DRV_GRP1),
};

/* List all groups consisting of these pins dedicated to the enablement of
 * certain hardware block and the corresponding mode for all of the pins.
 * The hardware probably has multiple combinations of these pinouts.
 */

/* AUDIO EXT CLK */
static int mt7623_aud_ext_clk0_pins[] = { 208, };
static int mt7623_aud_ext_clk0_funcs[] = { 1, };
static int mt7623_aud_ext_clk1_pins[] = { 209, };
static int mt7623_aud_ext_clk1_funcs[] = { 1, };

/* DISP PWM */
static int mt7623_disp_pwm_0_pins[] = { 72, };
static int mt7623_disp_pwm_0_funcs[] = { 5, };
static int mt7623_disp_pwm_1_pins[] = { 203, };
static int mt7623_disp_pwm_1_funcs[] = { 2, };
static int mt7623_disp_pwm_2_pins[] = { 208, };
static int mt7623_disp_pwm_2_funcs[] = { 5, };

/* ESW */
static int mt7623_esw_int_pins[] = { 273, };
static int mt7623_esw_int_funcs[] = { 1, };
static int mt7623_esw_rst_pins[] = { 277, };
static int mt7623_esw_rst_funcs[] = { 1, };

/* EPHY */
static int mt7623_ephy_pins[] = { 262, 263, 264, 265, 266, 267, 268,
				  269, 270, 271, 272, 274, };
static int mt7623_ephy_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, };

/* EXT_SDIO */
static int mt7623_ext_sdio_pins[] = { 236, 237, 238, 239, 240, 241, };
static int mt7623_ext_sdio_funcs[] = { 1, 1, 1, 1, 1, 1, };

/* HDMI RX */
static int mt7623_hdmi_rx_pins[] = { 247, 248, };
static int mt7623_hdmi_rx_funcs[] = { 1, 1 };
static int mt7623_hdmi_rx_i2c_pins[] = { 244, 245, };
static int mt7623_hdmi_rx_i2c_funcs[] = { 1, 1 };

/* HDMI TX */
static int mt7623_hdmi_cec_pins[] = { 122, };
static int mt7623_hdmi_cec_funcs[] = { 1, };
static int mt7623_hdmi_htplg_pins[] = { 123, };
static int mt7623_hdmi_htplg_funcs[] = { 1, };
static int mt7623_hdmi_i2c_pins[] = { 124, 125, };
static int mt7623_hdmi_i2c_funcs[] = { 1, 1 };

/* I2C */
static int mt7623_i2c0_pins[] = { 75, 76, };
static int mt7623_i2c0_funcs[] = { 1, 1, };
static int mt7623_i2c1_0_pins[] = { 57, 58, };
static int mt7623_i2c1_0_funcs[] = { 1, 1, };
static int mt7623_i2c1_1_pins[] = { 242, 243, };
static int mt7623_i2c1_1_funcs[] = { 4, 4, };
static int mt7623_i2c1_2_pins[] = { 85, 86, };
static int mt7623_i2c1_2_funcs[] = { 3, 3, };
static int mt7623_i2c1_3_pins[] = { 105, 106, };
static int mt7623_i2c1_3_funcs[] = { 3, 3, };
static int mt7623_i2c1_4_pins[] = { 124, 125, };
static int mt7623_i2c1_4_funcs[] = { 4, 4, };
static int mt7623_i2c2_0_pins[] = { 77, 78, };
static int mt7623_i2c2_0_funcs[] = { 1, 1, };
static int mt7623_i2c2_1_pins[] = { 89, 90, };
static int mt7623_i2c2_1_funcs[] = { 3, 3, };
static int mt7623_i2c2_2_pins[] = { 109, 110, };
static int mt7623_i2c2_2_funcs[] = { 3, 3, };
static int mt7623_i2c2_3_pins[] = { 122, 123, };
static int mt7623_i2c2_3_funcs[] = { 4, 4, };

/* I2S */
static int mt7623_i2s0_pins[] = { 49, 72, 73, 74, 126, };
static int mt7623_i2s0_funcs[] = { 1, 1, 1, 1, 1, };
static int mt7623_i2s1_pins[] = { 33, 34, 35, 36, 37, };
static int mt7623_i2s1_funcs[] = { 1, 1, 1, 1, 1, };
static int mt7623_i2s2_bclk_lrclk_mclk_pins[] = { 50, 52, 188, };
static int mt7623_i2s2_bclk_lrclk_mclk_funcs[] = { 1, 1, 1, };
static int mt7623_i2s2_data_in_pins[] = { 51, };
static int mt7623_i2s2_data_in_funcs[] = { 1, };
static int mt7623_i2s2_data_0_pins[] = { 203, };
static int mt7623_i2s2_data_0_funcs[] = { 9, };
static int mt7623_i2s2_data_1_pins[] = { 38,  };
static int mt7623_i2s2_data_1_funcs[] = { 4, };
static int mt7623_i2s3_bclk_lrclk_mclk_pins[] = { 191, 192, 193, };
static int mt7623_i2s3_bclk_lrclk_mclk_funcs[] = { 1, 1, 1, };
static int mt7623_i2s3_data_in_pins[] = { 190, };
static int mt7623_i2s3_data_in_funcs[] = { 1, };
static int mt7623_i2s3_data_0_pins[] = { 204, };
static int mt7623_i2s3_data_0_funcs[] = { 9, };
static int mt7623_i2s3_data_1_pins[] = { 2, };
static int mt7623_i2s3_data_1_funcs[] = { 0, };
static int mt7623_i2s4_pins[] = { 194, 195, 196, 197, 198, };
static int mt7623_i2s4_funcs[] = { 1, 1, 1, 1, 1, };
static int mt7623_i2s5_pins[] = { 16, 17, 30, 31, 32, };
static int mt7623_i2s5_funcs[] = { 1, 1, 1, 1, 1, };

/* IR */
static int mt7623_ir_pins[] = { 46, };
static int mt7623_ir_funcs[] = { 1, };

/* LCD */
static int mt7623_mipi_tx_pins[] = { 91, 92, 93, 94, 95, 96, 97, 98,
				     99, 100, };
static int mt7623_mipi_tx_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, };
static int mt7623_dsi_te_pins[] = { 84, };
static int mt7623_dsi_te_funcs[] = { 1, };
static int mt7623_lcm_rst_pins[] = { 83, };
static int mt7623_lcm_rst_funcs[] = { 1, };

/* MDC/MDIO */
static int mt7623_mdc_mdio_pins[] = { 275, 276, };
static int mt7623_mdc_mdio_funcs[] = { 1, 1, };

/* MSDC */
static int mt7623_msdc0_pins[] = { 111, 112, 113, 114, 115, 116, 117, 118,
				   119, 120, 121, };
static int mt7623_msdc0_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, };
static int mt7623_msdc1_pins[] = { 105, 106, 107, 108, 109, 110, };
static int mt7623_msdc1_funcs[] = { 1, 1, 1, 1, 1, 1, };
static int mt7623_msdc1_ins_pins[] = { 261, };
static int mt7623_msdc1_ins_funcs[] = { 1, };
static int mt7623_msdc1_wp_0_pins[] = { 29, };
static int mt7623_msdc1_wp_0_funcs[] = { 1, };
static int mt7623_msdc1_wp_1_pins[] = { 55, };
static int mt7623_msdc1_wp_1_funcs[] = { 3, };
static int mt7623_msdc1_wp_2_pins[] = { 209, };
static int mt7623_msdc1_wp_2_funcs[] = { 2, };
static int mt7623_msdc2_pins[] = { 85, 86, 87, 88, 89, 90, };
static int mt7623_msdc2_funcs[] = { 1, 1, 1, 1, 1, 1, };
static int mt7623_msdc3_pins[] = { 249, 250, 251, 252, 253, 254, 255, 256,
				   257, 258, 259, 260, };
static int mt7623_msdc3_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, };

/* NAND */
static int mt7623_nandc_pins[] = { 43, 47, 48, 111, 112, 113, 114, 115,
				   116, 117, 118, 119, 120, 121, };
static int mt7623_nandc_funcs[] = { 1, 1, 1, 4, 4, 4, 4, 4, 4, 4, 4, 4,
				   4, 4, };
static int mt7623_nandc_ceb0_pins[] = { 45, };
static int mt7623_nandc_ceb0_funcs[] = { 1, };
static int mt7623_nandc_ceb1_pins[] = { 44, };
static int mt7623_nandc_ceb1_funcs[] = { 1, };

/* RTC */
static int mt7623_rtc_pins[] = { 10, };
static int mt7623_rtc_funcs[] = { 1, };

/* OTG */
static int mt7623_otg_iddig0_0_pins[] = { 29, };
static int mt7623_otg_iddig0_0_funcs[] = { 1, };
static int mt7623_otg_iddig0_1_pins[] = { 44, };
static int mt7623_otg_iddig0_1_funcs[] = { 2, };
static int mt7623_otg_iddig0_2_pins[] = { 236, };
static int mt7623_otg_iddig0_2_funcs[] = { 2, };
static int mt7623_otg_iddig1_0_pins[] = { 27, };
static int mt7623_otg_iddig1_0_funcs[] = { 2, };
static int mt7623_otg_iddig1_1_pins[] = { 47, };
static int mt7623_otg_iddig1_1_funcs[] = { 2, };
static int mt7623_otg_iddig1_2_pins[] = { 238, };
static int mt7623_otg_iddig1_2_funcs[] = { 2, };
static int mt7623_otg_drv_vbus0_0_pins[] = { 28, };
static int mt7623_otg_drv_vbus0_0_funcs[] = { 1, };
static int mt7623_otg_drv_vbus0_1_pins[] = { 45, };
static int mt7623_otg_drv_vbus0_1_funcs[] = { 2, };
static int mt7623_otg_drv_vbus0_2_pins[] = { 237, };
static int mt7623_otg_drv_vbus0_2_funcs[] = { 2, };
static int mt7623_otg_drv_vbus1_0_pins[] = { 26, };
static int mt7623_otg_drv_vbus1_0_funcs[] = { 2, };
static int mt7623_otg_drv_vbus1_1_pins[] = { 48, };
static int mt7623_otg_drv_vbus1_1_funcs[] = { 2, };
static int mt7623_otg_drv_vbus1_2_pins[] = { 239, };
static int mt7623_otg_drv_vbus1_2_funcs[] = { 2, };

/* PCIE */
static int mt7623_pcie0_0_perst_pins[] = { 208, };
static int mt7623_pcie0_0_perst_funcs[] = { 3, };
static int mt7623_pcie0_1_perst_pins[] = { 22, };
static int mt7623_pcie0_1_perst_funcs[] = { 2, };
static int mt7623_pcie1_0_perst_pins[] = { 209, };
static int mt7623_pcie1_0_perst_funcs[] = { 3, };
static int mt7623_pcie1_1_perst_pins[] = { 23, };
static int mt7623_pcie1_1_perst_funcs[] = { 2, };
static int mt7623_pcie2_0_perst_pins[] = { 24, };
static int mt7623_pcie2_0_perst_funcs[] = { 2, };
static int mt7623_pcie2_1_perst_pins[] = { 29, };
static int mt7623_pcie2_1_perst_funcs[] = { 6, };
static int mt7623_pcie0_0_wake_pins[] = { 28, };
static int mt7623_pcie0_0_wake_funcs[] = { 6, };
static int mt7623_pcie0_1_wake_pins[] = { 251, };
static int mt7623_pcie0_1_wake_funcs[] = { 6, };
static int mt7623_pcie1_0_wake_pins[] = { 27, };
static int mt7623_pcie1_0_wake_funcs[] = { 6, };
static int mt7623_pcie1_1_wake_pins[] = { 253, };
static int mt7623_pcie1_1_wake_funcs[] = { 6, };
static int mt7623_pcie2_0_wake_pins[] = { 26, };
static int mt7623_pcie2_0_wake_funcs[] = { 6, };
static int mt7623_pcie2_1_wake_pins[] = { 255, };
static int mt7623_pcie2_1_wake_funcs[] = { 6, };
static int mt7623_pcie0_clkreq_pins[] = { 250, };
static int mt7623_pcie0_clkreq_funcs[] = { 6, };
static int mt7623_pcie1_clkreq_pins[] = { 252, };
static int mt7623_pcie1_clkreq_funcs[] = { 6, };
static int mt7623_pcie2_clkreq_pins[] = { 254, };
static int mt7623_pcie2_clkreq_funcs[] = { 6, };

/* the pcie_*_rev are only used for MT7623 */
static int mt7623_pcie0_0_rev_perst_pins[] = { 208, };
static int mt7623_pcie0_0_rev_perst_funcs[] = { 11, };
static int mt7623_pcie0_1_rev_perst_pins[] = { 22, };
static int mt7623_pcie0_1_rev_perst_funcs[] = { 10, };
static int mt7623_pcie1_0_rev_perst_pins[] = { 209, };
static int mt7623_pcie1_0_rev_perst_funcs[] = { 11, };
static int mt7623_pcie1_1_rev_perst_pins[] = { 23, };
static int mt7623_pcie1_1_rev_perst_funcs[] = { 10, };
static int mt7623_pcie2_0_rev_perst_pins[] = { 24, };
static int mt7623_pcie2_0_rev_perst_funcs[] = { 11, };
static int mt7623_pcie2_1_rev_perst_pins[] = { 29, };
static int mt7623_pcie2_1_rev_perst_funcs[] = { 14, };

/* PCM */
static int mt7623_pcm_clk_0_pins[] = { 18, };
static int mt7623_pcm_clk_0_funcs[] = { 1, };
static int mt7623_pcm_clk_1_pins[] = { 17, };
static int mt7623_pcm_clk_1_funcs[] = { 3, };
static int mt7623_pcm_clk_2_pins[] = { 35, };
static int mt7623_pcm_clk_2_funcs[] = { 3, };
static int mt7623_pcm_clk_3_pins[] = { 50, };
static int mt7623_pcm_clk_3_funcs[] = { 3, };
static int mt7623_pcm_clk_4_pins[] = { 74, };
static int mt7623_pcm_clk_4_funcs[] = { 3, };
static int mt7623_pcm_clk_5_pins[] = { 191, };
static int mt7623_pcm_clk_5_funcs[] = { 3, };
static int mt7623_pcm_clk_6_pins[] = { 196, };
static int mt7623_pcm_clk_6_funcs[] = { 3, };
static int mt7623_pcm_sync_0_pins[] = { 19, };
static int mt7623_pcm_sync_0_funcs[] = { 1, };
static int mt7623_pcm_sync_1_pins[] = { 30, };
static int mt7623_pcm_sync_1_funcs[] = { 3, };
static int mt7623_pcm_sync_2_pins[] = { 36, };
static int mt7623_pcm_sync_2_funcs[] = { 3, };
static int mt7623_pcm_sync_3_pins[] = { 52, };
static int mt7623_pcm_sync_3_funcs[] = { 31, };
static int mt7623_pcm_sync_4_pins[] = { 73, };
static int mt7623_pcm_sync_4_funcs[] = { 3, };
static int mt7623_pcm_sync_5_pins[] = { 192, };
static int mt7623_pcm_sync_5_funcs[] = { 3, };
static int mt7623_pcm_sync_6_pins[] = { 197, };
static int mt7623_pcm_sync_6_funcs[] = { 3, };
static int mt7623_pcm_rx_0_pins[] = { 20, };
static int mt7623_pcm_rx_0_funcs[] = { 1, };
static int mt7623_pcm_rx_1_pins[] = { 16, };
static int mt7623_pcm_rx_1_funcs[] = { 3, };
static int mt7623_pcm_rx_2_pins[] = { 34, };
static int mt7623_pcm_rx_2_funcs[] = { 3, };
static int mt7623_pcm_rx_3_pins[] = { 51, };
static int mt7623_pcm_rx_3_funcs[] = { 3, };
static int mt7623_pcm_rx_4_pins[] = { 72, };
static int mt7623_pcm_rx_4_funcs[] = { 3, };
static int mt7623_pcm_rx_5_pins[] = { 190, };
static int mt7623_pcm_rx_5_funcs[] = { 3, };
static int mt7623_pcm_rx_6_pins[] = { 195, };
static int mt7623_pcm_rx_6_funcs[] = { 3, };
static int mt7623_pcm_tx_0_pins[] = { 21, };
static int mt7623_pcm_tx_0_funcs[] = { 1, };
static int mt7623_pcm_tx_1_pins[] = { 32, };
static int mt7623_pcm_tx_1_funcs[] = { 3, };
static int mt7623_pcm_tx_2_pins[] = { 33, };
static int mt7623_pcm_tx_2_funcs[] = { 3, };
static int mt7623_pcm_tx_3_pins[] = { 38, };
static int mt7623_pcm_tx_3_funcs[] = { 3, };
static int mt7623_pcm_tx_4_pins[] = { 49, };
static int mt7623_pcm_tx_4_funcs[] = { 3, };
static int mt7623_pcm_tx_5_pins[] = { 189, };
static int mt7623_pcm_tx_5_funcs[] = { 3, };
static int mt7623_pcm_tx_6_pins[] = { 194, };
static int mt7623_pcm_tx_6_funcs[] = { 3, };

/* PWM */
static int mt7623_pwm_ch1_0_pins[] = { 203, };
static int mt7623_pwm_ch1_0_funcs[] = { 1, };
static int mt7623_pwm_ch1_1_pins[] = { 208, };
static int mt7623_pwm_ch1_1_funcs[] = { 2, };
static int mt7623_pwm_ch1_2_pins[] = { 72, };
static int mt7623_pwm_ch1_2_funcs[] = { 4, };
static int mt7623_pwm_ch1_3_pins[] = { 88, };
static int mt7623_pwm_ch1_3_funcs[] = { 3, };
static int mt7623_pwm_ch1_4_pins[] = { 108, };
static int mt7623_pwm_ch1_4_funcs[] = { 3, };
static int mt7623_pwm_ch2_0_pins[] = { 204, };
static int mt7623_pwm_ch2_0_funcs[] = { 1, };
static int mt7623_pwm_ch2_1_pins[] = { 53, };
static int mt7623_pwm_ch2_1_funcs[] = { 5, };
static int mt7623_pwm_ch2_2_pins[] = { 88, };
static int mt7623_pwm_ch2_2_funcs[] = { 6, };
static int mt7623_pwm_ch2_3_pins[] = { 108, };
static int mt7623_pwm_ch2_3_funcs[] = { 6, };
static int mt7623_pwm_ch2_4_pins[] = { 209, };
static int mt7623_pwm_ch2_4_funcs[] = { 5, };
static int mt7623_pwm_ch3_0_pins[] = { 205, };
static int mt7623_pwm_ch3_0_funcs[] = { 1, };
static int mt7623_pwm_ch3_1_pins[] = { 55, };
static int mt7623_pwm_ch3_1_funcs[] = { 5, };
static int mt7623_pwm_ch3_2_pins[] = { 89, };
static int mt7623_pwm_ch3_2_funcs[] = { 6, };
static int mt7623_pwm_ch3_3_pins[] = { 109, };
static int mt7623_pwm_ch3_3_funcs[] = { 6, };
static int mt7623_pwm_ch4_0_pins[] = { 206, };
static int mt7623_pwm_ch4_0_funcs[] = { 1, };
static int mt7623_pwm_ch4_1_pins[] = { 90, };
static int mt7623_pwm_ch4_1_funcs[] = { 6, };
static int mt7623_pwm_ch4_2_pins[] = { 110, };
static int mt7623_pwm_ch4_2_funcs[] = { 6, };
static int mt7623_pwm_ch4_3_pins[] = { 124, };
static int mt7623_pwm_ch4_3_funcs[] = { 5, };
static int mt7623_pwm_ch5_0_pins[] = { 207, };
static int mt7623_pwm_ch5_0_funcs[] = { 1, };
static int mt7623_pwm_ch5_1_pins[] = { 125, };
static int mt7623_pwm_ch5_1_funcs[] = { 5, };

/* PWRAP */
static int mt7623_pwrap_pins[] = { 0, 1, 2, 3, 4, 5, 6, };
static int mt7623_pwrap_funcs[] = { 1, 1, 1, 1, 1, 1, 1, };

/* SPDIF */
static int mt7623_spdif_in0_0_pins[] = { 56, };
static int mt7623_spdif_in0_0_funcs[] = { 3, };
static int mt7623_spdif_in0_1_pins[] = { 201, };
static int mt7623_spdif_in0_1_funcs[] = { 1, };
static int mt7623_spdif_in1_0_pins[] = { 54, };
static int mt7623_spdif_in1_0_funcs[] = { 3, };
static int mt7623_spdif_in1_1_pins[] = { 202, };
static int mt7623_spdif_in1_1_funcs[] = { 1, };
static int mt7623_spdif_out_pins[] = { 202, };
static int mt7623_spdif_out_funcs[] = { 1, };

/* SPI */
static int mt7623_spi0_pins[] = { 53, 54, 55, 56, };
static int mt7623_spi0_funcs[] = { 1, 1, 1, 1, };
static int mt7623_spi1_pins[] = { 7, 199, 8, 9, };
static int mt7623_spi1_funcs[] = { 1, 1, 1, 1, };
static int mt7623_spi2_pins[] = { 101, 104, 102, 103, };
static int mt7623_spi2_funcs[] = { 1, 1, 1, 1, };

/* UART */
static int mt7623_uart0_0_txd_rxd_pins[] = { 79, 80, };
static int mt7623_uart0_0_txd_rxd_funcs[] = { 1, 1, };
static int mt7623_uart0_1_txd_rxd_pins[] = { 87, 88, };
static int mt7623_uart0_1_txd_rxd_funcs[] = { 5, 5, };
static int mt7623_uart0_2_txd_rxd_pins[] = { 107, 108, };
static int mt7623_uart0_2_txd_rxd_funcs[] = { 5, 5, };
static int mt7623_uart0_3_txd_rxd_pins[] = { 123, 122, };
static int mt7623_uart0_3_txd_rxd_funcs[] = { 5, 5, };
static int mt7623_uart0_rts_cts_pins[] = { 22, 23, };
static int mt7623_uart0_rts_cts_funcs[] = { 1, 1, };
static int mt7623_uart1_0_txd_rxd_pins[] = { 81, 82, };
static int mt7623_uart1_0_txd_rxd_funcs[] = { 1, 1, };
static int mt7623_uart1_1_txd_rxd_pins[] = { 89, 90, };
static int mt7623_uart1_1_txd_rxd_funcs[] = { 5, 5, };
static int mt7623_uart1_2_txd_rxd_pins[] = { 109, 110, };
static int mt7623_uart1_2_txd_rxd_funcs[] = { 5, 5, };
static int mt7623_uart1_rts_cts_pins[] = { 24, 25, };
static int mt7623_uart1_rts_cts_funcs[] = { 1, 1, };
static int mt7623_uart2_0_txd_rxd_pins[] = { 14, 15, };
static int mt7623_uart2_0_txd_rxd_funcs[] = { 1, 1, };
static int mt7623_uart2_1_txd_rxd_pins[] = { 200, 201, };
static int mt7623_uart2_1_txd_rxd_funcs[] = { 6, 6, };
static int mt7623_uart2_rts_cts_pins[] = { 242, 243, };
static int mt7623_uart2_rts_cts_funcs[] = { 1, 1, };
static int mt7623_uart3_txd_rxd_pins[] = { 242, 243, };
static int mt7623_uart3_txd_rxd_funcs[] = { 2, 2, };
static int mt7623_uart3_rts_cts_pins[] = { 26, 27, };
static int mt7623_uart3_rts_cts_funcs[] = { 1, 1, };

/* Watchdog */
static int mt7623_watchdog_0_pins[] = { 11, };
static int mt7623_watchdog_0_funcs[] = { 1, };
static int mt7623_watchdog_1_pins[] = { 121, };
static int mt7623_watchdog_1_funcs[] = { 5, };

static const struct group_desc mt7623_groups[] = {
	PINCTRL_PIN_GROUP("aud_ext_clk0", mt7623_aud_ext_clk0),
	PINCTRL_PIN_GROUP("aud_ext_clk1", mt7623_aud_ext_clk1),
	PINCTRL_PIN_GROUP("dsi_te", mt7623_dsi_te),
	PINCTRL_PIN_GROUP("disp_pwm_0", mt7623_disp_pwm_0),
	PINCTRL_PIN_GROUP("disp_pwm_1", mt7623_disp_pwm_1),
	PINCTRL_PIN_GROUP("disp_pwm_2", mt7623_disp_pwm_2),
	PINCTRL_PIN_GROUP("ephy", mt7623_ephy),
	PINCTRL_PIN_GROUP("esw_int", mt7623_esw_int),
	PINCTRL_PIN_GROUP("esw_rst", mt7623_esw_rst),
	PINCTRL_PIN_GROUP("ext_sdio", mt7623_ext_sdio),
	PINCTRL_PIN_GROUP("hdmi_cec", mt7623_hdmi_cec),
	PINCTRL_PIN_GROUP("hdmi_htplg", mt7623_hdmi_htplg),
	PINCTRL_PIN_GROUP("hdmi_i2c", mt7623_hdmi_i2c),
	PINCTRL_PIN_GROUP("hdmi_rx", mt7623_hdmi_rx),
	PINCTRL_PIN_GROUP("hdmi_rx_i2c", mt7623_hdmi_rx_i2c),
	PINCTRL_PIN_GROUP("i2c0", mt7623_i2c0),
	PINCTRL_PIN_GROUP("i2c1_0", mt7623_i2c1_0),
	PINCTRL_PIN_GROUP("i2c1_1", mt7623_i2c1_1),
	PINCTRL_PIN_GROUP("i2c1_2", mt7623_i2c1_2),
	PINCTRL_PIN_GROUP("i2c1_3", mt7623_i2c1_3),
	PINCTRL_PIN_GROUP("i2c1_4", mt7623_i2c1_4),
	PINCTRL_PIN_GROUP("i2c2_0", mt7623_i2c2_0),
	PINCTRL_PIN_GROUP("i2c2_1", mt7623_i2c2_1),
	PINCTRL_PIN_GROUP("i2c2_2", mt7623_i2c2_2),
	PINCTRL_PIN_GROUP("i2c2_3", mt7623_i2c2_3),
	PINCTRL_PIN_GROUP("i2s0", mt7623_i2s0),
	PINCTRL_PIN_GROUP("i2s1", mt7623_i2s1),
	PINCTRL_PIN_GROUP("i2s4", mt7623_i2s4),
	PINCTRL_PIN_GROUP("i2s5", mt7623_i2s5),
	PINCTRL_PIN_GROUP("i2s2_bclk_lrclk_mclk", mt7623_i2s2_bclk_lrclk_mclk),
	PINCTRL_PIN_GROUP("i2s3_bclk_lrclk_mclk", mt7623_i2s3_bclk_lrclk_mclk),
	PINCTRL_PIN_GROUP("i2s2_data_in", mt7623_i2s2_data_in),
	PINCTRL_PIN_GROUP("i2s3_data_in", mt7623_i2s3_data_in),
	PINCTRL_PIN_GROUP("i2s2_data_0", mt7623_i2s2_data_0),
	PINCTRL_PIN_GROUP("i2s2_data_1", mt7623_i2s2_data_1),
	PINCTRL_PIN_GROUP("i2s3_data_0", mt7623_i2s3_data_0),
	PINCTRL_PIN_GROUP("i2s3_data_1", mt7623_i2s3_data_1),
	PINCTRL_PIN_GROUP("ir", mt7623_ir),
	PINCTRL_PIN_GROUP("lcm_rst", mt7623_lcm_rst),
	PINCTRL_PIN_GROUP("mdc_mdio", mt7623_mdc_mdio),
	PINCTRL_PIN_GROUP("mipi_tx", mt7623_mipi_tx),
	PINCTRL_PIN_GROUP("msdc0", mt7623_msdc0),
	PINCTRL_PIN_GROUP("msdc1", mt7623_msdc1),
	PINCTRL_PIN_GROUP("msdc1_ins", mt7623_msdc1_ins),
	PINCTRL_PIN_GROUP("msdc1_wp_0", mt7623_msdc1_wp_0),
	PINCTRL_PIN_GROUP("msdc1_wp_1", mt7623_msdc1_wp_1),
	PINCTRL_PIN_GROUP("msdc1_wp_2", mt7623_msdc1_wp_2),
	PINCTRL_PIN_GROUP("msdc2", mt7623_msdc2),
	PINCTRL_PIN_GROUP("msdc3", mt7623_msdc3),
	PINCTRL_PIN_GROUP("nandc", mt7623_nandc),
	PINCTRL_PIN_GROUP("nandc_ceb0", mt7623_nandc_ceb0),
	PINCTRL_PIN_GROUP("nandc_ceb1", mt7623_nandc_ceb1),
	PINCTRL_PIN_GROUP("otg_iddig0_0", mt7623_otg_iddig0_0),
	PINCTRL_PIN_GROUP("otg_iddig0_1", mt7623_otg_iddig0_1),
	PINCTRL_PIN_GROUP("otg_iddig0_2", mt7623_otg_iddig0_2),
	PINCTRL_PIN_GROUP("otg_iddig1_0", mt7623_otg_iddig1_0),
	PINCTRL_PIN_GROUP("otg_iddig1_1", mt7623_otg_iddig1_1),
	PINCTRL_PIN_GROUP("otg_iddig1_2", mt7623_otg_iddig1_2),
	PINCTRL_PIN_GROUP("otg_drv_vbus0_0", mt7623_otg_drv_vbus0_0),
	PINCTRL_PIN_GROUP("otg_drv_vbus0_1", mt7623_otg_drv_vbus0_1),
	PINCTRL_PIN_GROUP("otg_drv_vbus0_2", mt7623_otg_drv_vbus0_2),
	PINCTRL_PIN_GROUP("otg_drv_vbus1_0", mt7623_otg_drv_vbus1_0),
	PINCTRL_PIN_GROUP("otg_drv_vbus1_1", mt7623_otg_drv_vbus1_1),
	PINCTRL_PIN_GROUP("otg_drv_vbus1_2", mt7623_otg_drv_vbus1_2),
	PINCTRL_PIN_GROUP("pcie0_0_perst", mt7623_pcie0_0_perst),
	PINCTRL_PIN_GROUP("pcie0_1_perst", mt7623_pcie0_1_perst),
	PINCTRL_PIN_GROUP("pcie1_0_perst", mt7623_pcie1_0_perst),
	PINCTRL_PIN_GROUP("pcie1_1_perst", mt7623_pcie1_1_perst),
	PINCTRL_PIN_GROUP("pcie1_1_perst", mt7623_pcie1_1_perst),
	PINCTRL_PIN_GROUP("pcie0_0_rev_perst", mt7623_pcie0_0_rev_perst),
	PINCTRL_PIN_GROUP("pcie0_1_rev_perst", mt7623_pcie0_1_rev_perst),
	PINCTRL_PIN_GROUP("pcie1_0_rev_perst", mt7623_pcie1_0_rev_perst),
	PINCTRL_PIN_GROUP("pcie1_1_rev_perst", mt7623_pcie1_1_rev_perst),
	PINCTRL_PIN_GROUP("pcie2_0_rev_perst", mt7623_pcie2_0_rev_perst),
	PINCTRL_PIN_GROUP("pcie2_1_rev_perst", mt7623_pcie2_1_rev_perst),
	PINCTRL_PIN_GROUP("pcie2_0_perst", mt7623_pcie2_0_perst),
	PINCTRL_PIN_GROUP("pcie2_1_perst", mt7623_pcie2_1_perst),
	PINCTRL_PIN_GROUP("pcie0_0_wake", mt7623_pcie0_0_wake),
	PINCTRL_PIN_GROUP("pcie0_1_wake", mt7623_pcie0_1_wake),
	PINCTRL_PIN_GROUP("pcie1_0_wake", mt7623_pcie1_0_wake),
	PINCTRL_PIN_GROUP("pcie1_1_wake", mt7623_pcie1_1_wake),
	PINCTRL_PIN_GROUP("pcie2_0_wake", mt7623_pcie2_0_wake),
	PINCTRL_PIN_GROUP("pcie2_1_wake", mt7623_pcie2_1_wake),
	PINCTRL_PIN_GROUP("pcie0_clkreq", mt7623_pcie0_clkreq),
	PINCTRL_PIN_GROUP("pcie1_clkreq", mt7623_pcie1_clkreq),
	PINCTRL_PIN_GROUP("pcie2_clkreq", mt7623_pcie2_clkreq),
	PINCTRL_PIN_GROUP("pcm_clk_0", mt7623_pcm_clk_0),
	PINCTRL_PIN_GROUP("pcm_clk_1", mt7623_pcm_clk_1),
	PINCTRL_PIN_GROUP("pcm_clk_2", mt7623_pcm_clk_2),
	PINCTRL_PIN_GROUP("pcm_clk_3", mt7623_pcm_clk_3),
	PINCTRL_PIN_GROUP("pcm_clk_4", mt7623_pcm_clk_4),
	PINCTRL_PIN_GROUP("pcm_clk_5", mt7623_pcm_clk_5),
	PINCTRL_PIN_GROUP("pcm_clk_6", mt7623_pcm_clk_6),
	PINCTRL_PIN_GROUP("pcm_sync_0", mt7623_pcm_sync_0),
	PINCTRL_PIN_GROUP("pcm_sync_1", mt7623_pcm_sync_1),
	PINCTRL_PIN_GROUP("pcm_sync_2", mt7623_pcm_sync_2),
	PINCTRL_PIN_GROUP("pcm_sync_3", mt7623_pcm_sync_3),
	PINCTRL_PIN_GROUP("pcm_sync_4", mt7623_pcm_sync_4),
	PINCTRL_PIN_GROUP("pcm_sync_5", mt7623_pcm_sync_5),
	PINCTRL_PIN_GROUP("pcm_sync_6", mt7623_pcm_sync_6),
	PINCTRL_PIN_GROUP("pcm_rx_0", mt7623_pcm_rx_0),
	PINCTRL_PIN_GROUP("pcm_rx_1", mt7623_pcm_rx_1),
	PINCTRL_PIN_GROUP("pcm_rx_2", mt7623_pcm_rx_2),
	PINCTRL_PIN_GROUP("pcm_rx_3", mt7623_pcm_rx_3),
	PINCTRL_PIN_GROUP("pcm_rx_4", mt7623_pcm_rx_4),
	PINCTRL_PIN_GROUP("pcm_rx_5", mt7623_pcm_rx_5),
	PINCTRL_PIN_GROUP("pcm_rx_6", mt7623_pcm_rx_6),
	PINCTRL_PIN_GROUP("pcm_tx_0", mt7623_pcm_tx_0),
	PINCTRL_PIN_GROUP("pcm_tx_1", mt7623_pcm_tx_1),
	PINCTRL_PIN_GROUP("pcm_tx_2", mt7623_pcm_tx_2),
	PINCTRL_PIN_GROUP("pcm_tx_3", mt7623_pcm_tx_3),
	PINCTRL_PIN_GROUP("pcm_tx_4", mt7623_pcm_tx_4),
	PINCTRL_PIN_GROUP("pcm_tx_5", mt7623_pcm_tx_5),
	PINCTRL_PIN_GROUP("pcm_tx_6", mt7623_pcm_tx_6),
	PINCTRL_PIN_GROUP("pwm_ch1_0", mt7623_pwm_ch1_0),
	PINCTRL_PIN_GROUP("pwm_ch1_1", mt7623_pwm_ch1_1),
	PINCTRL_PIN_GROUP("pwm_ch1_2", mt7623_pwm_ch1_2),
	PINCTRL_PIN_GROUP("pwm_ch1_3", mt7623_pwm_ch1_3),
	PINCTRL_PIN_GROUP("pwm_ch1_4", mt7623_pwm_ch1_4),
	PINCTRL_PIN_GROUP("pwm_ch2_0", mt7623_pwm_ch2_0),
	PINCTRL_PIN_GROUP("pwm_ch2_1", mt7623_pwm_ch2_1),
	PINCTRL_PIN_GROUP("pwm_ch2_2", mt7623_pwm_ch2_2),
	PINCTRL_PIN_GROUP("pwm_ch2_3", mt7623_pwm_ch2_3),
	PINCTRL_PIN_GROUP("pwm_ch2_4", mt7623_pwm_ch2_4),
	PINCTRL_PIN_GROUP("pwm_ch3_0", mt7623_pwm_ch3_0),
	PINCTRL_PIN_GROUP("pwm_ch3_1", mt7623_pwm_ch3_1),
	PINCTRL_PIN_GROUP("pwm_ch3_2", mt7623_pwm_ch3_2),
	PINCTRL_PIN_GROUP("pwm_ch3_3", mt7623_pwm_ch3_3),
	PINCTRL_PIN_GROUP("pwm_ch4_0", mt7623_pwm_ch4_0),
	PINCTRL_PIN_GROUP("pwm_ch4_1", mt7623_pwm_ch4_1),
	PINCTRL_PIN_GROUP("pwm_ch4_2", mt7623_pwm_ch4_2),
	PINCTRL_PIN_GROUP("pwm_ch4_3", mt7623_pwm_ch4_3),
	PINCTRL_PIN_GROUP("pwm_ch5_0", mt7623_pwm_ch5_0),
	PINCTRL_PIN_GROUP("pwm_ch5_1", mt7623_pwm_ch5_1),
	PINCTRL_PIN_GROUP("pwrap", mt7623_pwrap),
	PINCTRL_PIN_GROUP("rtc", mt7623_rtc),
	PINCTRL_PIN_GROUP("spdif_in0_0", mt7623_spdif_in0_0),
	PINCTRL_PIN_GROUP("spdif_in0_1", mt7623_spdif_in0_1),
	PINCTRL_PIN_GROUP("spdif_in1_0", mt7623_spdif_in1_0),
	PINCTRL_PIN_GROUP("spdif_in1_1", mt7623_spdif_in1_1),
	PINCTRL_PIN_GROUP("spdif_out", mt7623_spdif_out),
	PINCTRL_PIN_GROUP("spi0", mt7623_spi0),
	PINCTRL_PIN_GROUP("spi1", mt7623_spi1),
	PINCTRL_PIN_GROUP("spi2", mt7623_spi2),
	PINCTRL_PIN_GROUP("uart0_0_txd_rxd",  mt7623_uart0_0_txd_rxd),
	PINCTRL_PIN_GROUP("uart0_1_txd_rxd",  mt7623_uart0_1_txd_rxd),
	PINCTRL_PIN_GROUP("uart0_2_txd_rxd",  mt7623_uart0_2_txd_rxd),
	PINCTRL_PIN_GROUP("uart0_3_txd_rxd",  mt7623_uart0_3_txd_rxd),
	PINCTRL_PIN_GROUP("uart1_0_txd_rxd",  mt7623_uart1_0_txd_rxd),
	PINCTRL_PIN_GROUP("uart1_1_txd_rxd",  mt7623_uart1_1_txd_rxd),
	PINCTRL_PIN_GROUP("uart1_2_txd_rxd",  mt7623_uart1_2_txd_rxd),
	PINCTRL_PIN_GROUP("uart2_0_txd_rxd",  mt7623_uart2_0_txd_rxd),
	PINCTRL_PIN_GROUP("uart2_1_txd_rxd",  mt7623_uart2_1_txd_rxd),
	PINCTRL_PIN_GROUP("uart3_txd_rxd",  mt7623_uart3_txd_rxd),
	PINCTRL_PIN_GROUP("uart0_rts_cts",  mt7623_uart0_rts_cts),
	PINCTRL_PIN_GROUP("uart1_rts_cts",  mt7623_uart1_rts_cts),
	PINCTRL_PIN_GROUP("uart2_rts_cts",  mt7623_uart2_rts_cts),
	PINCTRL_PIN_GROUP("uart3_rts_cts",  mt7623_uart3_rts_cts),
	PINCTRL_PIN_GROUP("watchdog_0", mt7623_watchdog_0),
	PINCTRL_PIN_GROUP("watchdog_1", mt7623_watchdog_1),
};

/* Joint those groups owning the same capability in user point of view which
 * allows that people tend to use through the device tree.
 */
static const char *mt7623_aud_clk_groups[] = { "aud_ext_clk0",
					       "aud_ext_clk1", };
static const char *mt7623_disp_pwm_groups[] = { "disp_pwm_0", "disp_pwm_1",
						"disp_pwm_2", };
static const char *mt7623_ethernet_groups[] = { "esw_int", "esw_rst",
						"ephy", "mdc_mdio", };
static const char *mt7623_ext_sdio_groups[] = { "ext_sdio", };
static const char *mt7623_hdmi_groups[] = { "hdmi_cec", "hdmi_htplg",
					    "hdmi_i2c", "hdmi_rx",
					    "hdmi_rx_i2c", };
static const char *mt7623_i2c_groups[] = { "i2c0", "i2c1_0", "i2c1_1",
					   "i2c1_2", "i2c1_3", "i2c1_4",
					   "i2c2_0", "i2c2_1", "i2c2_2",
					   "i2c2_3", };
static const char *mt7623_i2s_groups[] = { "i2s0", "i2s1",
					   "i2s2_bclk_lrclk_mclk",
					   "i2s3_bclk_lrclk_mclk",
					   "i2s4", "i2s5",
					   "i2s2_data_in", "i2s3_data_in",
					   "i2s2_data_0", "i2s2_data_1",
					   "i2s3_data_0", "i2s3_data_1", };
static const char *mt7623_ir_groups[] = { "ir", };
static const char *mt7623_lcd_groups[] = { "dsi_te", "lcm_rst", "mipi_tx", };
static const char *mt7623_msdc_groups[] = { "msdc0", "msdc1", "msdc1_ins",
					    "msdc1_wp_0", "msdc1_wp_1",
					    "msdc1_wp_2", "msdc2",
						"msdc3", };
static const char *mt7623_nandc_groups[] = { "nandc", "nandc_ceb0",
					     "nandc_ceb1", };
static const char *mt7623_otg_groups[] = { "otg_iddig0_0", "otg_iddig0_1",
					    "otg_iddig0_2", "otg_iddig1_0",
					    "otg_iddig1_1", "otg_iddig1_2",
					    "otg_drv_vbus0_0",
					    "otg_drv_vbus0_1",
					    "otg_drv_vbus0_2",
					    "otg_drv_vbus1_0",
					    "otg_drv_vbus1_1",
					    "otg_drv_vbus1_2", };
static const char *mt7623_pcie_groups[] = { "pcie0_0_perst", "pcie0_1_perst",
					    "pcie1_0_perst", "pcie1_1_perst",
					    "pcie2_0_perst", "pcie2_1_perst",
					    "pcie0_0_rev_perst",
					    "pcie0_1_rev_perst",
					    "pcie1_0_rev_perst",
					    "pcie1_1_rev_perst",
					    "pcie2_0_rev_perst",
					    "pcie2_1_rev_perst",
					    "pcie0_0_wake", "pcie0_1_wake",
					    "pcie2_0_wake", "pcie2_1_wake",
					    "pcie0_clkreq", "pcie1_clkreq",
					    "pcie2_clkreq", };
static const char *mt7623_pcm_groups[] = { "pcm_clk_0", "pcm_clk_1",
					   "pcm_clk_2", "pcm_clk_3",
					   "pcm_clk_4", "pcm_clk_5",
					   "pcm_clk_6", "pcm_sync_0",
					   "pcm_sync_1", "pcm_sync_2",
					   "pcm_sync_3", "pcm_sync_4",
					   "pcm_sync_5", "pcm_sync_6",
					   "pcm_rx_0", "pcm_rx_1",
					   "pcm_rx_2", "pcm_rx_3",
					   "pcm_rx_4", "pcm_rx_5",
					   "pcm_rx_6", "pcm_tx_0",
					   "pcm_tx_1", "pcm_tx_2",
					   "pcm_tx_3", "pcm_tx_4",
					   "pcm_tx_5", "pcm_tx_6", };
static const char *mt7623_pwm_groups[] = { "pwm_ch1_0", "pwm_ch1_1",
					   "pwm_ch1_2", "pwm_ch2_0",
					   "pwm_ch2_1", "pwm_ch2_2",
					   "pwm_ch3_0", "pwm_ch3_1",
					   "pwm_ch3_2", "pwm_ch4_0",
					   "pwm_ch4_1", "pwm_ch4_2",
					   "pwm_ch4_3", "pwm_ch5_0",
					   "pwm_ch5_1", "pwm_ch5_2",
					   "pwm_ch6_0", "pwm_ch6_1",
					   "pwm_ch6_2", "pwm_ch6_3",
					   "pwm_ch7_0", "pwm_ch7_1",
					   "pwm_ch7_2", };
static const char *mt7623_pwrap_groups[] = { "pwrap", };
static const char *mt7623_rtc_groups[] = { "rtc", };
static const char *mt7623_spi_groups[] = { "spi0", "spi2", "spi2", };
static const char *mt7623_spdif_groups[] = { "spdif_in0_0", "spdif_in0_1",
					     "spdif_in1_0", "spdif_in1_1",
					     "spdif_out", };
static const char *mt7623_uart_groups[] = { "uart0_0_txd_rxd",
					    "uart0_1_txd_rxd",
					    "uart0_2_txd_rxd",
					    "uart0_3_txd_rxd",
					    "uart1_0_txd_rxd",
					    "uart1_1_txd_rxd",
					    "uart1_2_txd_rxd",
					    "uart2_0_txd_rxd",
					    "uart2_1_txd_rxd",
					    "uart3_txd_rxd",
					    "uart0_rts_cts",
					    "uart1_rts_cts",
					    "uart2_rts_cts",
					    "uart3_rts_cts", };
static const char *mt7623_wdt_groups[] = { "watchdog_0", "watchdog_1", };

static const struct function_desc mt7623_functions[] = {
	PINCTRL_PIN_FUNCTION("audck", mt7623_aud_clk),
	PINCTRL_PIN_FUNCTION("disp", mt7623_disp_pwm),
	PINCTRL_PIN_FUNCTION("eth", mt7623_ethernet),
	PINCTRL_PIN_FUNCTION("sdio", mt7623_ext_sdio),
	PINCTRL_PIN_FUNCTION("hdmi", mt7623_hdmi),
	PINCTRL_PIN_FUNCTION("i2c", mt7623_i2c),
	PINCTRL_PIN_FUNCTION("i2s", mt7623_i2s),
	PINCTRL_PIN_FUNCTION("ir", mt7623_ir),
	PINCTRL_PIN_FUNCTION("lcd", mt7623_lcd),
	PINCTRL_PIN_FUNCTION("msdc", mt7623_msdc),
	PINCTRL_PIN_FUNCTION("nand", mt7623_nandc),
	PINCTRL_PIN_FUNCTION("otg", mt7623_otg),
	PINCTRL_PIN_FUNCTION("pcie", mt7623_pcie),
	PINCTRL_PIN_FUNCTION("pcm", mt7623_pcm),
	PINCTRL_PIN_FUNCTION("pwm", mt7623_pwm),
	PINCTRL_PIN_FUNCTION("pwrap", mt7623_pwrap),
	PINCTRL_PIN_FUNCTION("rtc", mt7623_rtc),
	PINCTRL_PIN_FUNCTION("spi", mt7623_spi),
	PINCTRL_PIN_FUNCTION("spdif", mt7623_spdif),
	PINCTRL_PIN_FUNCTION("uart", mt7623_uart),
	PINCTRL_PIN_FUNCTION("watchdog", mt7623_wdt),
};

static const struct mtk_eint_hw mt7623_eint_hw = {
	.port_mask = 6,
	.ports     = 6,
	.ap_num    = 169,
	.db_cnt    = 20,
	.db_time   = debounce_time_mt2701,
};

static struct mtk_pin_soc mt7623_data = {
	.reg_cal = mt7623_reg_cals,
	.pins = mt7623_pins,
	.npins = ARRAY_SIZE(mt7623_pins),
	.grps = mt7623_groups,
	.ngrps = ARRAY_SIZE(mt7623_groups),
	.funcs = mt7623_functions,
	.nfuncs = ARRAY_SIZE(mt7623_functions),
	.eint_hw = &mt7623_eint_hw,
	.gpio_m = 0,
	.ies_present = true,
	.base_names = mtk_default_register_base_names,
	.nbase_names = ARRAY_SIZE(mtk_default_register_base_names),
	.bias_disable_set = mtk_pinconf_bias_disable_set_rev1,
	.bias_disable_get = mtk_pinconf_bias_disable_get_rev1,
	.bias_set = mtk_pinconf_bias_set_rev1,
	.bias_get = mtk_pinconf_bias_get_rev1,
	.drive_set = mtk_pinconf_drive_set_rev1,
	.drive_get = mtk_pinconf_drive_get_rev1,
	.adv_pull_get = mtk_pinconf_adv_pull_get,
	.adv_pull_set = mtk_pinconf_adv_pull_set,
};

/*
 * There are some specific pins have mux functions greater than 8,
 * and if we want to switch thees high modes we need to disable
 * bonding constraints firstly.
 */
static void mt7623_bonding_disable(struct platform_device *pdev)
{
	struct mtk_pinctrl *hw = platform_get_drvdata(pdev);

	mtk_rmw(hw, 0, PIN_BOND_REG0, BOND_PCIE_CLR, BOND_PCIE_CLR);
	mtk_rmw(hw, 0, PIN_BOND_REG1, BOND_I2S_CLR, BOND_I2S_CLR);
	mtk_rmw(hw, 0, PIN_BOND_REG2, BOND_MSDC0E_CLR, BOND_MSDC0E_CLR);
}

static const struct of_device_id mt7623_pctrl_match[] = {
	{ .compatible = "mediatek,mt7623-moore-pinctrl", },
	{}
};

static int mt7623_pinctrl_probe(struct platform_device *pdev)
{
	int err;

	err = mtk_moore_pinctrl_probe(pdev, &mt7623_data);
	if (err)
		return err;

	mt7623_bonding_disable(pdev);

	return 0;
}

static struct platform_driver mtk_pinctrl_driver = {
	.probe = mt7623_pinctrl_probe,
	.driver = {
		.name = "mt7623-moore-pinctrl",
		.of_match_table = mt7623_pctrl_match,
	},
};

static int __init mtk_pinctrl_init(void)
{
	return platform_driver_register(&mtk_pinctrl_driver);
}
arch_initcall(mtk_pinctrl_init);
