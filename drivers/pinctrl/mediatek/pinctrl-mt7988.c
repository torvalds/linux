// SPDX-License-Identifier: GPL-2.0
/*
 * The MT7988 driver based on Linux generic pinctrl binding.
 *
 * Copyright (C) 2020 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#include "pinctrl-moore.h"

enum mt7988_pinctrl_reg_page {
	GPIO_BASE,
	IOCFG_TR_BASE,
	IOCFG_BR_BASE,
	IOCFG_RB_BASE,
	IOCFG_LB_BASE,
	IOCFG_TL_BASE,
};

#define MT7988_PIN(_number, _name) MTK_PIN(_number, _name, 0, _number, DRV_GRP4)

#define PIN_FIELD_BASE(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit,     \
		       _x_bits)                                                \
	PIN_FIELD_CALC(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit,     \
		       _x_bits, 32, 0)

#define PINS_FIELD_BASE(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit,    \
			_x_bits)                                               \
	PIN_FIELD_CALC(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit,     \
		       _x_bits, 32, 1)

static const struct mtk_pin_field_calc mt7988_pin_mode_range[] = {
	PIN_FIELD(0, 83, 0x300, 0x10, 0, 4),
};

static const struct mtk_pin_field_calc mt7988_pin_dir_range[] = {
	PIN_FIELD(0, 83, 0x0, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_di_range[] = {
	PIN_FIELD(0, 83, 0x200, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_do_range[] = {
	PIN_FIELD(0, 83, 0x100, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_ies_range[] = {
	PIN_FIELD_BASE(0, 0, 5, 0x30, 0x10, 13, 1),
	PIN_FIELD_BASE(1, 1, 5, 0x30, 0x10, 14, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x30, 0x10, 11, 1),
	PIN_FIELD_BASE(3, 3, 5, 0x30, 0x10, 12, 1),
	PIN_FIELD_BASE(4, 4, 5, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(5, 5, 5, 0x30, 0x10, 9, 1),
	PIN_FIELD_BASE(6, 6, 5, 0x30, 0x10, 10, 1),

	PIN_FIELD_BASE(7, 7, 4, 0x30, 0x10, 8, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x30, 0x10, 6, 1),
	PIN_FIELD_BASE(9, 9, 4, 0x30, 0x10, 5, 1),
	PIN_FIELD_BASE(10, 10, 4, 0x30, 0x10, 3, 1),

	PIN_FIELD_BASE(11, 11, 1, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(12, 12, 1, 0x40, 0x10, 21, 1),
	PIN_FIELD_BASE(13, 13, 1, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(14, 14, 1, 0x40, 0x10, 2, 1),

	PIN_FIELD_BASE(15, 15, 5, 0x30, 0x10, 7, 1),
	PIN_FIELD_BASE(16, 16, 5, 0x30, 0x10, 8, 1),
	PIN_FIELD_BASE(17, 17, 5, 0x30, 0x10, 3, 1),
	PIN_FIELD_BASE(18, 18, 5, 0x30, 0x10, 4, 1),

	PIN_FIELD_BASE(19, 19, 4, 0x30, 0x10, 7, 1),
	PIN_FIELD_BASE(20, 20, 4, 0x30, 0x10, 4, 1),

	PIN_FIELD_BASE(21, 21, 3, 0x50, 0x10, 17, 1),
	PIN_FIELD_BASE(22, 22, 3, 0x50, 0x10, 23, 1),
	PIN_FIELD_BASE(23, 23, 3, 0x50, 0x10, 20, 1),
	PIN_FIELD_BASE(24, 24, 3, 0x50, 0x10, 19, 1),
	PIN_FIELD_BASE(25, 25, 3, 0x50, 0x10, 21, 1),
	PIN_FIELD_BASE(26, 26, 3, 0x50, 0x10, 22, 1),
	PIN_FIELD_BASE(27, 27, 3, 0x50, 0x10, 18, 1),
	PIN_FIELD_BASE(28, 28, 3, 0x50, 0x10, 25, 1),
	PIN_FIELD_BASE(29, 29, 3, 0x50, 0x10, 26, 1),
	PIN_FIELD_BASE(30, 30, 3, 0x50, 0x10, 27, 1),
	PIN_FIELD_BASE(31, 31, 3, 0x50, 0x10, 24, 1),
	PIN_FIELD_BASE(32, 32, 3, 0x50, 0x10, 28, 1),
	PIN_FIELD_BASE(33, 33, 3, 0x60, 0x10, 0, 1),
	PIN_FIELD_BASE(34, 34, 3, 0x50, 0x10, 31, 1),
	PIN_FIELD_BASE(35, 35, 3, 0x50, 0x10, 29, 1),
	PIN_FIELD_BASE(36, 36, 3, 0x50, 0x10, 30, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x60, 0x10, 1, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x50, 0x10, 11, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x50, 0x10, 10, 1),
	PIN_FIELD_BASE(40, 40, 3, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(41, 41, 3, 0x50, 0x10, 1, 1),
	PIN_FIELD_BASE(42, 42, 3, 0x50, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 3, 0x50, 0x10, 8, 1),
	PIN_FIELD_BASE(44, 44, 3, 0x50, 0x10, 7, 1),
	PIN_FIELD_BASE(45, 45, 3, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(46, 46, 3, 0x50, 0x10, 5, 1),
	PIN_FIELD_BASE(47, 47, 3, 0x50, 0x10, 4, 1),
	PIN_FIELD_BASE(48, 48, 3, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(49, 49, 3, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(50, 50, 3, 0x50, 0x10, 15, 1),
	PIN_FIELD_BASE(51, 51, 3, 0x50, 0x10, 12, 1),
	PIN_FIELD_BASE(52, 52, 3, 0x50, 0x10, 13, 1),
	PIN_FIELD_BASE(53, 53, 3, 0x50, 0x10, 14, 1),
	PIN_FIELD_BASE(54, 54, 3, 0x50, 0x10, 16, 1),

	PIN_FIELD_BASE(55, 55, 1, 0x40, 0x10, 14, 1),
	PIN_FIELD_BASE(56, 56, 1, 0x40, 0x10, 15, 1),
	PIN_FIELD_BASE(57, 57, 1, 0x40, 0x10, 13, 1),
	PIN_FIELD_BASE(58, 58, 1, 0x40, 0x10, 4, 1),
	PIN_FIELD_BASE(59, 59, 1, 0x40, 0x10, 5, 1),
	PIN_FIELD_BASE(60, 60, 1, 0x40, 0x10, 6, 1),
	PIN_FIELD_BASE(61, 61, 1, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(62, 62, 1, 0x40, 0x10, 7, 1),
	PIN_FIELD_BASE(63, 63, 1, 0x40, 0x10, 20, 1),
	PIN_FIELD_BASE(64, 64, 1, 0x40, 0x10, 8, 1),
	PIN_FIELD_BASE(65, 65, 1, 0x40, 0x10, 9, 1),
	PIN_FIELD_BASE(66, 66, 1, 0x40, 0x10, 10, 1),
	PIN_FIELD_BASE(67, 67, 1, 0x40, 0x10, 11, 1),
	PIN_FIELD_BASE(68, 68, 1, 0x40, 0x10, 12, 1),

	PIN_FIELD_BASE(69, 69, 5, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(70, 70, 5, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(71, 71, 5, 0x30, 0x10, 5, 1),
	PIN_FIELD_BASE(72, 72, 5, 0x30, 0x10, 6, 1),

	PIN_FIELD_BASE(73, 73, 4, 0x30, 0x10, 10, 1),
	PIN_FIELD_BASE(74, 74, 4, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(75, 75, 4, 0x30, 0x10, 11, 1),
	PIN_FIELD_BASE(76, 76, 4, 0x30, 0x10, 9, 1),
	PIN_FIELD_BASE(77, 77, 4, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(78, 78, 4, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(79, 79, 4, 0x30, 0x10, 12, 1),

	PIN_FIELD_BASE(80, 80, 1, 0x40, 0x10, 18, 1),
	PIN_FIELD_BASE(81, 81, 1, 0x40, 0x10, 19, 1),
	PIN_FIELD_BASE(82, 82, 1, 0x40, 0x10, 16, 1),
	PIN_FIELD_BASE(83, 83, 1, 0x40, 0x10, 17, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_smt_range[] = {
	PIN_FIELD_BASE(0, 0, 5, 0xc0, 0x10, 13, 1),
	PIN_FIELD_BASE(1, 1, 5, 0xc0, 0x10, 14, 1),
	PIN_FIELD_BASE(2, 2, 5, 0xc0, 0x10, 11, 1),
	PIN_FIELD_BASE(3, 3, 5, 0xc0, 0x10, 12, 1),
	PIN_FIELD_BASE(4, 4, 5, 0xc0, 0x10, 0, 1),
	PIN_FIELD_BASE(5, 5, 5, 0xc0, 0x10, 9, 1),
	PIN_FIELD_BASE(6, 6, 5, 0xc0, 0x10, 10, 1),

	PIN_FIELD_BASE(7, 7, 4, 0xb0, 0x10, 8, 1),
	PIN_FIELD_BASE(8, 8, 4, 0xb0, 0x10, 6, 1),
	PIN_FIELD_BASE(9, 9, 4, 0xb0, 0x10, 5, 1),
	PIN_FIELD_BASE(10, 10, 4, 0xb0, 0x10, 3, 1),

	PIN_FIELD_BASE(11, 11, 1, 0xe0, 0x10, 0, 1),
	PIN_FIELD_BASE(12, 12, 1, 0xe0, 0x10, 21, 1),
	PIN_FIELD_BASE(13, 13, 1, 0xe0, 0x10, 1, 1),
	PIN_FIELD_BASE(14, 14, 1, 0xe0, 0x10, 2, 1),

	PIN_FIELD_BASE(15, 15, 5, 0xc0, 0x10, 7, 1),
	PIN_FIELD_BASE(16, 16, 5, 0xc0, 0x10, 8, 1),
	PIN_FIELD_BASE(17, 17, 5, 0xc0, 0x10, 3, 1),
	PIN_FIELD_BASE(18, 18, 5, 0xc0, 0x10, 4, 1),

	PIN_FIELD_BASE(19, 19, 4, 0xb0, 0x10, 7, 1),
	PIN_FIELD_BASE(20, 20, 4, 0xb0, 0x10, 4, 1),

	PIN_FIELD_BASE(21, 21, 3, 0x140, 0x10, 17, 1),
	PIN_FIELD_BASE(22, 22, 3, 0x140, 0x10, 23, 1),
	PIN_FIELD_BASE(23, 23, 3, 0x140, 0x10, 20, 1),
	PIN_FIELD_BASE(24, 24, 3, 0x140, 0x10, 19, 1),
	PIN_FIELD_BASE(25, 25, 3, 0x140, 0x10, 21, 1),
	PIN_FIELD_BASE(26, 26, 3, 0x140, 0x10, 22, 1),
	PIN_FIELD_BASE(27, 27, 3, 0x140, 0x10, 18, 1),
	PIN_FIELD_BASE(28, 28, 3, 0x140, 0x10, 25, 1),
	PIN_FIELD_BASE(29, 29, 3, 0x140, 0x10, 26, 1),
	PIN_FIELD_BASE(30, 30, 3, 0x140, 0x10, 27, 1),
	PIN_FIELD_BASE(31, 31, 3, 0x140, 0x10, 24, 1),
	PIN_FIELD_BASE(32, 32, 3, 0x140, 0x10, 28, 1),
	PIN_FIELD_BASE(33, 33, 3, 0x150, 0x10, 0, 1),
	PIN_FIELD_BASE(34, 34, 3, 0x140, 0x10, 31, 1),
	PIN_FIELD_BASE(35, 35, 3, 0x140, 0x10, 29, 1),
	PIN_FIELD_BASE(36, 36, 3, 0x140, 0x10, 30, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x150, 0x10, 1, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x140, 0x10, 11, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x140, 0x10, 10, 1),
	PIN_FIELD_BASE(40, 40, 3, 0x140, 0x10, 0, 1),
	PIN_FIELD_BASE(41, 41, 3, 0x140, 0x10, 1, 1),
	PIN_FIELD_BASE(42, 42, 3, 0x140, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 3, 0x140, 0x10, 8, 1),
	PIN_FIELD_BASE(44, 44, 3, 0x140, 0x10, 7, 1),
	PIN_FIELD_BASE(45, 45, 3, 0x140, 0x10, 6, 1),
	PIN_FIELD_BASE(46, 46, 3, 0x140, 0x10, 5, 1),
	PIN_FIELD_BASE(47, 47, 3, 0x140, 0x10, 4, 1),
	PIN_FIELD_BASE(48, 48, 3, 0x140, 0x10, 3, 1),
	PIN_FIELD_BASE(49, 49, 3, 0x140, 0x10, 2, 1),
	PIN_FIELD_BASE(50, 50, 3, 0x140, 0x10, 15, 1),
	PIN_FIELD_BASE(51, 51, 3, 0x140, 0x10, 12, 1),
	PIN_FIELD_BASE(52, 52, 3, 0x140, 0x10, 13, 1),
	PIN_FIELD_BASE(53, 53, 3, 0x140, 0x10, 14, 1),
	PIN_FIELD_BASE(54, 54, 3, 0x140, 0x10, 16, 1),

	PIN_FIELD_BASE(55, 55, 1, 0xe0, 0x10, 14, 1),
	PIN_FIELD_BASE(56, 56, 1, 0xe0, 0x10, 15, 1),
	PIN_FIELD_BASE(57, 57, 1, 0xe0, 0x10, 13, 1),
	PIN_FIELD_BASE(58, 58, 1, 0xe0, 0x10, 4, 1),
	PIN_FIELD_BASE(59, 59, 1, 0xe0, 0x10, 5, 1),
	PIN_FIELD_BASE(60, 60, 1, 0xe0, 0x10, 6, 1),
	PIN_FIELD_BASE(61, 61, 1, 0xe0, 0x10, 3, 1),
	PIN_FIELD_BASE(62, 62, 1, 0xe0, 0x10, 7, 1),
	PIN_FIELD_BASE(63, 63, 1, 0xe0, 0x10, 20, 1),
	PIN_FIELD_BASE(64, 64, 1, 0xe0, 0x10, 8, 1),
	PIN_FIELD_BASE(65, 65, 1, 0xe0, 0x10, 9, 1),
	PIN_FIELD_BASE(66, 66, 1, 0xe0, 0x10, 10, 1),
	PIN_FIELD_BASE(67, 67, 1, 0xe0, 0x10, 11, 1),
	PIN_FIELD_BASE(68, 68, 1, 0xe0, 0x10, 12, 1),

	PIN_FIELD_BASE(69, 69, 5, 0xc0, 0x10, 1, 1),
	PIN_FIELD_BASE(70, 70, 5, 0xc0, 0x10, 2, 1),
	PIN_FIELD_BASE(71, 71, 5, 0xc0, 0x10, 5, 1),
	PIN_FIELD_BASE(72, 72, 5, 0xc0, 0x10, 6, 1),

	PIN_FIELD_BASE(73, 73, 4, 0xb0, 0x10, 10, 1),
	PIN_FIELD_BASE(74, 74, 4, 0xb0, 0x10, 1, 1),
	PIN_FIELD_BASE(75, 75, 4, 0xb0, 0x10, 11, 1),
	PIN_FIELD_BASE(76, 76, 4, 0xb0, 0x10, 9, 1),
	PIN_FIELD_BASE(77, 77, 4, 0xb0, 0x10, 2, 1),
	PIN_FIELD_BASE(78, 78, 4, 0xb0, 0x10, 0, 1),
	PIN_FIELD_BASE(79, 79, 4, 0xb0, 0x10, 12, 1),

	PIN_FIELD_BASE(80, 80, 1, 0xe0, 0x10, 18, 1),
	PIN_FIELD_BASE(81, 81, 1, 0xe0, 0x10, 19, 1),
	PIN_FIELD_BASE(82, 82, 1, 0xe0, 0x10, 16, 1),
	PIN_FIELD_BASE(83, 83, 1, 0xe0, 0x10, 17, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_pu_range[] = {
	PIN_FIELD_BASE(7, 7, 4, 0x60, 0x10, 5, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x60, 0x10, 4, 1),
	PIN_FIELD_BASE(9, 9, 4, 0x60, 0x10, 3, 1),
	PIN_FIELD_BASE(10, 10, 4, 0x60, 0x10, 2, 1),

	PIN_FIELD_BASE(13, 13, 1, 0x70, 0x10, 0, 1),
	PIN_FIELD_BASE(14, 14, 1, 0x70, 0x10, 1, 1),
	PIN_FIELD_BASE(63, 63, 1, 0x70, 0x10, 2, 1),

	PIN_FIELD_BASE(75, 75, 4, 0x60, 0x10, 7, 1),
	PIN_FIELD_BASE(76, 76, 4, 0x60, 0x10, 6, 1),
	PIN_FIELD_BASE(77, 77, 4, 0x60, 0x10, 1, 1),
	PIN_FIELD_BASE(78, 78, 4, 0x60, 0x10, 0, 1),
	PIN_FIELD_BASE(79, 79, 4, 0x60, 0x10, 8, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_pd_range[] = {
	PIN_FIELD_BASE(7, 7, 4, 0x40, 0x10, 5, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x40, 0x10, 4, 1),
	PIN_FIELD_BASE(9, 9, 4, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(10, 10, 4, 0x40, 0x10, 2, 1),

	PIN_FIELD_BASE(13, 13, 1, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(14, 14, 1, 0x50, 0x10, 1, 1),

	PIN_FIELD_BASE(15, 15, 5, 0x40, 0x10, 4, 1),
	PIN_FIELD_BASE(16, 16, 5, 0x40, 0x10, 5, 1),
	PIN_FIELD_BASE(17, 17, 5, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(18, 18, 5, 0x40, 0x10, 1, 1),

	PIN_FIELD_BASE(63, 63, 1, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(71, 71, 5, 0x40, 0x10, 2, 1),
	PIN_FIELD_BASE(72, 72, 5, 0x40, 0x10, 3, 1),

	PIN_FIELD_BASE(75, 75, 4, 0x40, 0x10, 7, 1),
	PIN_FIELD_BASE(76, 76, 4, 0x40, 0x10, 6, 1),
	PIN_FIELD_BASE(77, 77, 4, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(78, 78, 4, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(79, 79, 4, 0x40, 0x10, 8, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_drv_range[] = {
	PIN_FIELD_BASE(0, 0, 5, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(1, 1, 5, 0x00, 0x10, 24, 3),
	PIN_FIELD_BASE(2, 2, 5, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(3, 3, 5, 0x00, 0x10, 18, 3),
	PIN_FIELD_BASE(4, 4, 5, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(5, 5, 5, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(6, 6, 5, 0x00, 0x10, 12, 3),

	PIN_FIELD_BASE(7, 7, 4, 0x00, 0x10, 24, 3),
	PIN_FIELD_BASE(8, 8, 4, 0x00, 0x10, 28, 3),
	PIN_FIELD_BASE(9, 9, 4, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(10, 10, 4, 0x00, 0x10, 9, 3),

	PIN_FIELD_BASE(11, 11, 1, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(12, 12, 1, 0x20, 0x10, 3, 3),
	PIN_FIELD_BASE(13, 13, 1, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(14, 14, 1, 0x00, 0x10, 6, 3),

	PIN_FIELD_BASE(19, 19, 4, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(20, 20, 4, 0x00, 0x10, 12, 3),

	PIN_FIELD_BASE(21, 21, 3, 0x10, 0x10, 21, 3),
	PIN_FIELD_BASE(22, 22, 3, 0x20, 0x10, 9, 3),
	PIN_FIELD_BASE(23, 23, 3, 0x20, 0x10, 0, 3),
	PIN_FIELD_BASE(24, 24, 3, 0x10, 0x10, 27, 3),
	PIN_FIELD_BASE(25, 25, 3, 0x20, 0x10, 3, 3),
	PIN_FIELD_BASE(26, 26, 3, 0x20, 0x10, 6, 3),
	PIN_FIELD_BASE(27, 27, 3, 0x10, 0x10, 24, 3),
	PIN_FIELD_BASE(28, 28, 3, 0x20, 0x10, 15, 3),
	PIN_FIELD_BASE(29, 29, 3, 0x20, 0x10, 18, 3),
	PIN_FIELD_BASE(30, 30, 3, 0x20, 0x10, 21, 3),
	PIN_FIELD_BASE(31, 31, 3, 0x20, 0x10, 12, 3),
	PIN_FIELD_BASE(32, 32, 3, 0x20, 0x10, 24, 3),
	PIN_FIELD_BASE(33, 33, 3, 0x30, 0x10, 6, 3),
	PIN_FIELD_BASE(34, 34, 3, 0x30, 0x10, 3, 3),
	PIN_FIELD_BASE(35, 35, 3, 0x20, 0x10, 27, 3),
	PIN_FIELD_BASE(36, 36, 3, 0x30, 0x10, 0, 3),
	PIN_FIELD_BASE(37, 37, 3, 0x30, 0x10, 9, 3),
	PIN_FIELD_BASE(38, 38, 3, 0x10, 0x10, 3, 3),
	PIN_FIELD_BASE(39, 39, 3, 0x10, 0x10, 0, 3),
	PIN_FIELD_BASE(40, 40, 3, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(41, 41, 3, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(42, 42, 3, 0x00, 0x10, 27, 3),
	PIN_FIELD_BASE(43, 43, 3, 0x00, 0x10, 24, 3),
	PIN_FIELD_BASE(44, 44, 3, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(45, 45, 3, 0x00, 0x10, 18, 3),
	PIN_FIELD_BASE(46, 46, 3, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(47, 47, 3, 0x00, 0x10, 12, 3),
	PIN_FIELD_BASE(48, 48, 3, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(49, 49, 3, 0x00, 0x10, 6, 3),
	PIN_FIELD_BASE(50, 50, 3, 0x10, 0x10, 15, 3),
	PIN_FIELD_BASE(51, 51, 3, 0x10, 0x10, 6, 3),
	PIN_FIELD_BASE(52, 52, 3, 0x10, 0x10, 9, 3),
	PIN_FIELD_BASE(53, 53, 3, 0x10, 0x10, 12, 3),
	PIN_FIELD_BASE(54, 54, 3, 0x10, 0x10, 18, 3),

	PIN_FIELD_BASE(55, 55, 1, 0x10, 0x10, 12, 3),
	PIN_FIELD_BASE(56, 56, 1, 0x10, 0x10, 15, 3),
	PIN_FIELD_BASE(57, 57, 1, 0x10, 0x10, 9, 3),
	PIN_FIELD_BASE(58, 58, 1, 0x00, 0x10, 12, 3),
	PIN_FIELD_BASE(59, 59, 1, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(60, 60, 1, 0x00, 0x10, 18, 3),
	PIN_FIELD_BASE(61, 61, 1, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(62, 62, 1, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(63, 63, 1, 0x20, 0x10, 0, 3),
	PIN_FIELD_BASE(64, 64, 1, 0x00, 0x10, 24, 3),
	PIN_FIELD_BASE(65, 65, 1, 0x00, 0x10, 27, 3),
	PIN_FIELD_BASE(66, 66, 1, 0x10, 0x10, 0, 3),
	PIN_FIELD_BASE(67, 67, 1, 0x10, 0x10, 3, 3),
	PIN_FIELD_BASE(68, 68, 1, 0x10, 0x10, 6, 3),

	PIN_FIELD_BASE(69, 69, 5, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(70, 70, 5, 0x00, 0x10, 6, 3),

	PIN_FIELD_BASE(73, 73, 4, 0x10, 0x10, 0, 3),
	PIN_FIELD_BASE(74, 74, 4, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(75, 75, 4, 0x10, 0x10, 3, 3),
	PIN_FIELD_BASE(76, 76, 4, 0x00, 0x10, 27, 3),
	PIN_FIELD_BASE(77, 77, 4, 0x00, 0x10, 6, 3),
	PIN_FIELD_BASE(78, 78, 4, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(79, 79, 4, 0x10, 0x10, 6, 3),

	PIN_FIELD_BASE(80, 80, 1, 0x10, 0x10, 24, 3),
	PIN_FIELD_BASE(81, 81, 1, 0x10, 0x10, 27, 3),
	PIN_FIELD_BASE(82, 82, 1, 0x10, 0x10, 18, 3),
	PIN_FIELD_BASE(83, 83, 1, 0x10, 0x10, 21, 3),
};

static const struct mtk_pin_field_calc mt7988_pin_pupd_range[] = {
	PIN_FIELD_BASE(0, 0, 5, 0x50, 0x10, 7, 1),
	PIN_FIELD_BASE(1, 1, 5, 0x50, 0x10, 8, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x50, 0x10, 5, 1),
	PIN_FIELD_BASE(3, 3, 5, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 5, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(5, 5, 5, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(6, 6, 5, 0x50, 0x10, 4, 1),

	PIN_FIELD_BASE(11, 11, 1, 0x60, 0x10, 0, 1),
	PIN_FIELD_BASE(12, 12, 1, 0x60, 0x10, 18, 1),

	PIN_FIELD_BASE(19, 19, 4, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 4, 0x50, 0x10, 1, 1),

	PIN_FIELD_BASE(21, 21, 3, 0x70, 0x10, 17, 1),
	PIN_FIELD_BASE(22, 22, 3, 0x70, 0x10, 23, 1),
	PIN_FIELD_BASE(23, 23, 3, 0x70, 0x10, 20, 1),
	PIN_FIELD_BASE(24, 24, 3, 0x70, 0x10, 19, 1),
	PIN_FIELD_BASE(25, 25, 3, 0x70, 0x10, 21, 1),
	PIN_FIELD_BASE(26, 26, 3, 0x70, 0x10, 22, 1),
	PIN_FIELD_BASE(27, 27, 3, 0x70, 0x10, 18, 1),
	PIN_FIELD_BASE(28, 28, 3, 0x70, 0x10, 25, 1),
	PIN_FIELD_BASE(29, 29, 3, 0x70, 0x10, 26, 1),
	PIN_FIELD_BASE(30, 30, 3, 0x70, 0x10, 27, 1),
	PIN_FIELD_BASE(31, 31, 3, 0x70, 0x10, 24, 1),
	PIN_FIELD_BASE(32, 32, 3, 0x70, 0x10, 28, 1),
	PIN_FIELD_BASE(33, 33, 3, 0x80, 0x10, 0, 1),
	PIN_FIELD_BASE(34, 34, 3, 0x70, 0x10, 31, 1),
	PIN_FIELD_BASE(35, 35, 3, 0x70, 0x10, 29, 1),
	PIN_FIELD_BASE(36, 36, 3, 0x70, 0x10, 30, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x80, 0x10, 1, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x70, 0x10, 11, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x70, 0x10, 10, 1),
	PIN_FIELD_BASE(40, 40, 3, 0x70, 0x10, 0, 1),
	PIN_FIELD_BASE(41, 41, 3, 0x70, 0x10, 1, 1),
	PIN_FIELD_BASE(42, 42, 3, 0x70, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 3, 0x70, 0x10, 8, 1),
	PIN_FIELD_BASE(44, 44, 3, 0x70, 0x10, 7, 1),
	PIN_FIELD_BASE(45, 45, 3, 0x70, 0x10, 6, 1),
	PIN_FIELD_BASE(46, 46, 3, 0x70, 0x10, 5, 1),
	PIN_FIELD_BASE(47, 47, 3, 0x70, 0x10, 4, 1),
	PIN_FIELD_BASE(48, 48, 3, 0x70, 0x10, 3, 1),
	PIN_FIELD_BASE(49, 49, 3, 0x70, 0x10, 2, 1),
	PIN_FIELD_BASE(50, 50, 3, 0x70, 0x10, 15, 1),
	PIN_FIELD_BASE(51, 51, 3, 0x70, 0x10, 12, 1),
	PIN_FIELD_BASE(52, 52, 3, 0x70, 0x10, 13, 1),
	PIN_FIELD_BASE(53, 53, 3, 0x70, 0x10, 14, 1),
	PIN_FIELD_BASE(54, 54, 3, 0x70, 0x10, 16, 1),

	PIN_FIELD_BASE(55, 55, 1, 0x60, 0x10, 12, 1),
	PIN_FIELD_BASE(56, 56, 1, 0x60, 0x10, 13, 1),
	PIN_FIELD_BASE(57, 57, 1, 0x60, 0x10, 11, 1),
	PIN_FIELD_BASE(58, 58, 1, 0x60, 0x10, 2, 1),
	PIN_FIELD_BASE(59, 59, 1, 0x60, 0x10, 3, 1),
	PIN_FIELD_BASE(60, 60, 1, 0x60, 0x10, 4, 1),
	PIN_FIELD_BASE(61, 61, 1, 0x60, 0x10, 1, 1),
	PIN_FIELD_BASE(62, 62, 1, 0x60, 0x10, 5, 1),
	PIN_FIELD_BASE(64, 64, 1, 0x60, 0x10, 6, 1),
	PIN_FIELD_BASE(65, 65, 1, 0x60, 0x10, 7, 1),
	PIN_FIELD_BASE(66, 66, 1, 0x60, 0x10, 8, 1),
	PIN_FIELD_BASE(67, 67, 1, 0x60, 0x10, 9, 1),
	PIN_FIELD_BASE(68, 68, 1, 0x60, 0x10, 10, 1),

	PIN_FIELD_BASE(69, 69, 5, 0x50, 0x10, 1, 1),
	PIN_FIELD_BASE(70, 70, 5, 0x50, 0x10, 2, 1),

	PIN_FIELD_BASE(73, 73, 4, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(74, 74, 4, 0x50, 0x10, 0, 1),

	PIN_FIELD_BASE(80, 80, 1, 0x60, 0x10, 16, 1),
	PIN_FIELD_BASE(81, 81, 1, 0x60, 0x10, 17, 1),
	PIN_FIELD_BASE(82, 82, 1, 0x60, 0x10, 14, 1),
	PIN_FIELD_BASE(83, 83, 1, 0x60, 0x10, 15, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_r0_range[] = {
	PIN_FIELD_BASE(0, 0, 5, 0x60, 0x10, 7, 1),
	PIN_FIELD_BASE(1, 1, 5, 0x60, 0x10, 8, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x60, 0x10, 5, 1),
	PIN_FIELD_BASE(3, 3, 5, 0x60, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 5, 0x60, 0x10, 0, 1),
	PIN_FIELD_BASE(5, 5, 5, 0x60, 0x10, 3, 1),
	PIN_FIELD_BASE(6, 6, 5, 0x60, 0x10, 4, 1),

	PIN_FIELD_BASE(11, 11, 1, 0x80, 0x10, 0, 1),
	PIN_FIELD_BASE(12, 12, 1, 0x80, 0x10, 18, 1),

	PIN_FIELD_BASE(19, 19, 4, 0x70, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 4, 0x70, 0x10, 1, 1),

	PIN_FIELD_BASE(21, 21, 3, 0x90, 0x10, 17, 1),
	PIN_FIELD_BASE(22, 22, 3, 0x90, 0x10, 23, 1),
	PIN_FIELD_BASE(23, 23, 3, 0x90, 0x10, 20, 1),
	PIN_FIELD_BASE(24, 24, 3, 0x90, 0x10, 19, 1),
	PIN_FIELD_BASE(25, 25, 3, 0x90, 0x10, 21, 1),
	PIN_FIELD_BASE(26, 26, 3, 0x90, 0x10, 22, 1),
	PIN_FIELD_BASE(27, 27, 3, 0x90, 0x10, 18, 1),
	PIN_FIELD_BASE(28, 28, 3, 0x90, 0x10, 25, 1),
	PIN_FIELD_BASE(29, 29, 3, 0x90, 0x10, 26, 1),
	PIN_FIELD_BASE(30, 30, 3, 0x90, 0x10, 27, 1),
	PIN_FIELD_BASE(31, 31, 3, 0x90, 0x10, 24, 1),
	PIN_FIELD_BASE(32, 32, 3, 0x90, 0x10, 28, 1),
	PIN_FIELD_BASE(33, 33, 3, 0xa0, 0x10, 0, 1),
	PIN_FIELD_BASE(34, 34, 3, 0x90, 0x10, 31, 1),
	PIN_FIELD_BASE(35, 35, 3, 0x90, 0x10, 29, 1),
	PIN_FIELD_BASE(36, 36, 3, 0x90, 0x10, 30, 1),
	PIN_FIELD_BASE(37, 37, 3, 0xa0, 0x10, 1, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x90, 0x10, 11, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x90, 0x10, 10, 1),
	PIN_FIELD_BASE(40, 40, 3, 0x90, 0x10, 0, 1),
	PIN_FIELD_BASE(41, 41, 3, 0x90, 0x10, 1, 1),
	PIN_FIELD_BASE(42, 42, 3, 0x90, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 3, 0x90, 0x10, 8, 1),
	PIN_FIELD_BASE(44, 44, 3, 0x90, 0x10, 7, 1),
	PIN_FIELD_BASE(45, 45, 3, 0x90, 0x10, 6, 1),
	PIN_FIELD_BASE(46, 46, 3, 0x90, 0x10, 5, 1),
	PIN_FIELD_BASE(47, 47, 3, 0x90, 0x10, 4, 1),
	PIN_FIELD_BASE(48, 48, 3, 0x90, 0x10, 3, 1),
	PIN_FIELD_BASE(49, 49, 3, 0x90, 0x10, 2, 1),
	PIN_FIELD_BASE(50, 50, 3, 0x90, 0x10, 15, 1),
	PIN_FIELD_BASE(51, 51, 3, 0x90, 0x10, 12, 1),
	PIN_FIELD_BASE(52, 52, 3, 0x90, 0x10, 13, 1),
	PIN_FIELD_BASE(53, 53, 3, 0x90, 0x10, 14, 1),
	PIN_FIELD_BASE(54, 54, 3, 0x90, 0x10, 16, 1),

	PIN_FIELD_BASE(55, 55, 1, 0x80, 0x10, 12, 1),
	PIN_FIELD_BASE(56, 56, 1, 0x80, 0x10, 13, 1),
	PIN_FIELD_BASE(57, 57, 1, 0x80, 0x10, 11, 1),
	PIN_FIELD_BASE(58, 58, 1, 0x80, 0x10, 2, 1),
	PIN_FIELD_BASE(59, 59, 1, 0x80, 0x10, 3, 1),
	PIN_FIELD_BASE(60, 60, 1, 0x80, 0x10, 4, 1),
	PIN_FIELD_BASE(61, 61, 1, 0x80, 0x10, 1, 1),
	PIN_FIELD_BASE(62, 62, 1, 0x80, 0x10, 5, 1),
	PIN_FIELD_BASE(64, 64, 1, 0x80, 0x10, 6, 1),
	PIN_FIELD_BASE(65, 65, 1, 0x80, 0x10, 7, 1),
	PIN_FIELD_BASE(66, 66, 1, 0x80, 0x10, 8, 1),
	PIN_FIELD_BASE(67, 67, 1, 0x80, 0x10, 9, 1),
	PIN_FIELD_BASE(68, 68, 1, 0x80, 0x10, 10, 1),

	PIN_FIELD_BASE(69, 69, 5, 0x60, 0x10, 1, 1),
	PIN_FIELD_BASE(70, 70, 5, 0x60, 0x10, 2, 1),

	PIN_FIELD_BASE(73, 73, 4, 0x70, 0x10, 3, 1),
	PIN_FIELD_BASE(74, 74, 4, 0x70, 0x10, 0, 1),

	PIN_FIELD_BASE(80, 80, 1, 0x80, 0x10, 16, 1),
	PIN_FIELD_BASE(81, 81, 1, 0x80, 0x10, 17, 1),
	PIN_FIELD_BASE(82, 82, 1, 0x80, 0x10, 14, 1),
	PIN_FIELD_BASE(83, 83, 1, 0x80, 0x10, 15, 1),
};

static const struct mtk_pin_field_calc mt7988_pin_r1_range[] = {
	PIN_FIELD_BASE(0, 0, 5, 0x70, 0x10, 7, 1),
	PIN_FIELD_BASE(1, 1, 5, 0x70, 0x10, 8, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x70, 0x10, 5, 1),
	PIN_FIELD_BASE(3, 3, 5, 0x70, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 5, 0x70, 0x10, 0, 1),
	PIN_FIELD_BASE(5, 5, 5, 0x70, 0x10, 3, 1),
	PIN_FIELD_BASE(6, 6, 5, 0x70, 0x10, 4, 1),

	PIN_FIELD_BASE(11, 11, 1, 0x90, 0x10, 0, 1),
	PIN_FIELD_BASE(12, 12, 1, 0x90, 0x10, 18, 1),

	PIN_FIELD_BASE(19, 19, 4, 0x80, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 4, 0x80, 0x10, 1, 1),

	PIN_FIELD_BASE(21, 21, 3, 0xb0, 0x10, 17, 1),
	PIN_FIELD_BASE(22, 22, 3, 0xb0, 0x10, 23, 1),
	PIN_FIELD_BASE(23, 23, 3, 0xb0, 0x10, 20, 1),
	PIN_FIELD_BASE(24, 24, 3, 0xb0, 0x10, 19, 1),
	PIN_FIELD_BASE(25, 25, 3, 0xb0, 0x10, 21, 1),
	PIN_FIELD_BASE(26, 26, 3, 0xb0, 0x10, 22, 1),
	PIN_FIELD_BASE(27, 27, 3, 0xb0, 0x10, 18, 1),
	PIN_FIELD_BASE(28, 28, 3, 0xb0, 0x10, 25, 1),
	PIN_FIELD_BASE(29, 29, 3, 0xb0, 0x10, 26, 1),
	PIN_FIELD_BASE(30, 30, 3, 0xb0, 0x10, 27, 1),
	PIN_FIELD_BASE(31, 31, 3, 0xb0, 0x10, 24, 1),
	PIN_FIELD_BASE(32, 32, 3, 0xb0, 0x10, 28, 1),
	PIN_FIELD_BASE(33, 33, 3, 0xc0, 0x10, 0, 1),
	PIN_FIELD_BASE(34, 34, 3, 0xb0, 0x10, 31, 1),
	PIN_FIELD_BASE(35, 35, 3, 0xb0, 0x10, 29, 1),
	PIN_FIELD_BASE(36, 36, 3, 0xb0, 0x10, 30, 1),
	PIN_FIELD_BASE(37, 37, 3, 0xc0, 0x10, 1, 1),
	PIN_FIELD_BASE(38, 38, 3, 0xb0, 0x10, 11, 1),
	PIN_FIELD_BASE(39, 39, 3, 0xb0, 0x10, 10, 1),
	PIN_FIELD_BASE(40, 40, 3, 0xb0, 0x10, 0, 1),
	PIN_FIELD_BASE(41, 41, 3, 0xb0, 0x10, 1, 1),
	PIN_FIELD_BASE(42, 42, 3, 0xb0, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 3, 0xb0, 0x10, 8, 1),
	PIN_FIELD_BASE(44, 44, 3, 0xb0, 0x10, 7, 1),
	PIN_FIELD_BASE(45, 45, 3, 0xb0, 0x10, 6, 1),
	PIN_FIELD_BASE(46, 46, 3, 0xb0, 0x10, 5, 1),
	PIN_FIELD_BASE(47, 47, 3, 0xb0, 0x10, 4, 1),
	PIN_FIELD_BASE(48, 48, 3, 0xb0, 0x10, 3, 1),
	PIN_FIELD_BASE(49, 49, 3, 0xb0, 0x10, 2, 1),
	PIN_FIELD_BASE(50, 50, 3, 0xb0, 0x10, 15, 1),
	PIN_FIELD_BASE(51, 51, 3, 0xb0, 0x10, 12, 1),
	PIN_FIELD_BASE(52, 52, 3, 0xb0, 0x10, 13, 1),
	PIN_FIELD_BASE(53, 53, 3, 0xb0, 0x10, 14, 1),
	PIN_FIELD_BASE(54, 54, 3, 0xb0, 0x10, 16, 1),

	PIN_FIELD_BASE(55, 55, 1, 0x90, 0x10, 12, 1),
	PIN_FIELD_BASE(56, 56, 1, 0x90, 0x10, 13, 1),
	PIN_FIELD_BASE(57, 57, 1, 0x90, 0x10, 11, 1),
	PIN_FIELD_BASE(58, 58, 1, 0x90, 0x10, 2, 1),
	PIN_FIELD_BASE(59, 59, 1, 0x90, 0x10, 3, 1),
	PIN_FIELD_BASE(60, 60, 1, 0x90, 0x10, 4, 1),
	PIN_FIELD_BASE(61, 61, 1, 0x90, 0x10, 1, 1),
	PIN_FIELD_BASE(62, 62, 1, 0x90, 0x10, 5, 1),
	PIN_FIELD_BASE(64, 64, 1, 0x90, 0x10, 6, 1),
	PIN_FIELD_BASE(65, 65, 1, 0x90, 0x10, 7, 1),
	PIN_FIELD_BASE(66, 66, 1, 0x90, 0x10, 8, 1),
	PIN_FIELD_BASE(67, 67, 1, 0x90, 0x10, 9, 1),
	PIN_FIELD_BASE(68, 68, 1, 0x90, 0x10, 10, 1),

	PIN_FIELD_BASE(69, 69, 5, 0x70, 0x10, 1, 1),
	PIN_FIELD_BASE(70, 70, 5, 0x70, 0x10, 2, 1),

	PIN_FIELD_BASE(73, 73, 4, 0x80, 0x10, 3, 1),
	PIN_FIELD_BASE(74, 74, 4, 0x80, 0x10, 0, 1),

	PIN_FIELD_BASE(80, 80, 1, 0x90, 0x10, 16, 1),
	PIN_FIELD_BASE(81, 81, 1, 0x90, 0x10, 17, 1),
	PIN_FIELD_BASE(82, 82, 1, 0x90, 0x10, 14, 1),
	PIN_FIELD_BASE(83, 83, 1, 0x90, 0x10, 15, 1),
};

static const unsigned int mt7988_pull_type[] = {
	MTK_PULL_PUPD_R1R0_TYPE,/*0*/ MTK_PULL_PUPD_R1R0_TYPE,/*1*/
	MTK_PULL_PUPD_R1R0_TYPE,/*2*/ MTK_PULL_PUPD_R1R0_TYPE,/*3*/
	MTK_PULL_PUPD_R1R0_TYPE,/*4*/ MTK_PULL_PUPD_R1R0_TYPE,/*5*/
	MTK_PULL_PUPD_R1R0_TYPE,/*6*/ MTK_PULL_PU_PD_TYPE,    /*7*/
	MTK_PULL_PU_PD_TYPE,    /*8*/ MTK_PULL_PU_PD_TYPE,    /*9*/
	MTK_PULL_PU_PD_TYPE,    /*10*/ MTK_PULL_PUPD_R1R0_TYPE,/*11*/
	MTK_PULL_PUPD_R1R0_TYPE,/*12*/ MTK_PULL_PU_PD_TYPE,    /*13*/
	MTK_PULL_PU_PD_TYPE,    /*14*/ MTK_PULL_PD_TYPE,       /*15*/
	MTK_PULL_PD_TYPE,       /*16*/ MTK_PULL_PD_TYPE,       /*17*/
	MTK_PULL_PD_TYPE,       /*18*/ MTK_PULL_PUPD_R1R0_TYPE,/*19*/
	MTK_PULL_PUPD_R1R0_TYPE,/*20*/ MTK_PULL_PUPD_R1R0_TYPE,/*21*/
	MTK_PULL_PUPD_R1R0_TYPE,/*22*/ MTK_PULL_PUPD_R1R0_TYPE,/*23*/
	MTK_PULL_PUPD_R1R0_TYPE,/*24*/ MTK_PULL_PUPD_R1R0_TYPE,/*25*/
	MTK_PULL_PUPD_R1R0_TYPE,/*26*/ MTK_PULL_PUPD_R1R0_TYPE,/*27*/
	MTK_PULL_PUPD_R1R0_TYPE,/*28*/ MTK_PULL_PUPD_R1R0_TYPE,/*29*/
	MTK_PULL_PUPD_R1R0_TYPE,/*30*/ MTK_PULL_PUPD_R1R0_TYPE,/*31*/
	MTK_PULL_PUPD_R1R0_TYPE,/*32*/ MTK_PULL_PUPD_R1R0_TYPE,/*33*/
	MTK_PULL_PUPD_R1R0_TYPE,/*34*/ MTK_PULL_PUPD_R1R0_TYPE,/*35*/
	MTK_PULL_PUPD_R1R0_TYPE,/*36*/ MTK_PULL_PUPD_R1R0_TYPE,/*37*/
	MTK_PULL_PUPD_R1R0_TYPE,/*38*/ MTK_PULL_PUPD_R1R0_TYPE,/*39*/
	MTK_PULL_PUPD_R1R0_TYPE,/*40*/ MTK_PULL_PUPD_R1R0_TYPE,/*41*/
	MTK_PULL_PUPD_R1R0_TYPE,/*42*/ MTK_PULL_PUPD_R1R0_TYPE,/*43*/
	MTK_PULL_PUPD_R1R0_TYPE,/*44*/ MTK_PULL_PUPD_R1R0_TYPE,/*45*/
	MTK_PULL_PUPD_R1R0_TYPE,/*46*/ MTK_PULL_PUPD_R1R0_TYPE,/*47*/
	MTK_PULL_PUPD_R1R0_TYPE,/*48*/ MTK_PULL_PUPD_R1R0_TYPE,/*49*/
	MTK_PULL_PUPD_R1R0_TYPE,/*50*/ MTK_PULL_PUPD_R1R0_TYPE,/*51*/
	MTK_PULL_PUPD_R1R0_TYPE,/*52*/ MTK_PULL_PUPD_R1R0_TYPE,/*53*/
	MTK_PULL_PUPD_R1R0_TYPE,/*54*/ MTK_PULL_PUPD_R1R0_TYPE,/*55*/
	MTK_PULL_PUPD_R1R0_TYPE,/*56*/ MTK_PULL_PUPD_R1R0_TYPE,/*57*/
	MTK_PULL_PUPD_R1R0_TYPE,/*58*/ MTK_PULL_PUPD_R1R0_TYPE,/*59*/
	MTK_PULL_PUPD_R1R0_TYPE,/*60*/ MTK_PULL_PUPD_R1R0_TYPE,/*61*/
	MTK_PULL_PUPD_R1R0_TYPE,/*62*/ MTK_PULL_PU_PD_TYPE,    /*63*/
	MTK_PULL_PUPD_R1R0_TYPE,/*64*/ MTK_PULL_PUPD_R1R0_TYPE,/*65*/
	MTK_PULL_PUPD_R1R0_TYPE,/*66*/ MTK_PULL_PUPD_R1R0_TYPE,/*67*/
	MTK_PULL_PUPD_R1R0_TYPE,/*68*/ MTK_PULL_PUPD_R1R0_TYPE,/*69*/
	MTK_PULL_PUPD_R1R0_TYPE,/*70*/ MTK_PULL_PD_TYPE,       /*71*/
	MTK_PULL_PD_TYPE,       /*72*/ MTK_PULL_PUPD_R1R0_TYPE,/*73*/
	MTK_PULL_PUPD_R1R0_TYPE,/*74*/ MTK_PULL_PU_PD_TYPE,    /*75*/
	MTK_PULL_PU_PD_TYPE,    /*76*/ MTK_PULL_PU_PD_TYPE,    /*77*/
	MTK_PULL_PU_PD_TYPE,    /*78*/ MTK_PULL_PU_PD_TYPE,    /*79*/
	MTK_PULL_PUPD_R1R0_TYPE,/*80*/ MTK_PULL_PUPD_R1R0_TYPE,/*81*/
	MTK_PULL_PUPD_R1R0_TYPE,/*82*/ MTK_PULL_PUPD_R1R0_TYPE,/*83*/
};

static const struct mtk_pin_reg_calc mt7988_reg_cals[] = {
	[PINCTRL_PIN_REG_MODE] = MTK_RANGE(mt7988_pin_mode_range),
	[PINCTRL_PIN_REG_DIR] = MTK_RANGE(mt7988_pin_dir_range),
	[PINCTRL_PIN_REG_DI] = MTK_RANGE(mt7988_pin_di_range),
	[PINCTRL_PIN_REG_DO] = MTK_RANGE(mt7988_pin_do_range),
	[PINCTRL_PIN_REG_SMT] = MTK_RANGE(mt7988_pin_smt_range),
	[PINCTRL_PIN_REG_IES] = MTK_RANGE(mt7988_pin_ies_range),
	[PINCTRL_PIN_REG_PU] = MTK_RANGE(mt7988_pin_pu_range),
	[PINCTRL_PIN_REG_PD] = MTK_RANGE(mt7988_pin_pd_range),
	[PINCTRL_PIN_REG_DRV] = MTK_RANGE(mt7988_pin_drv_range),
	[PINCTRL_PIN_REG_PUPD] = MTK_RANGE(mt7988_pin_pupd_range),
	[PINCTRL_PIN_REG_R0] = MTK_RANGE(mt7988_pin_r0_range),
	[PINCTRL_PIN_REG_R1] = MTK_RANGE(mt7988_pin_r1_range),
};

static const struct mtk_pin_desc mt7988_pins[] = {
	MT7988_PIN(0, "UART2_RXD"),
	MT7988_PIN(1, "UART2_TXD"),
	MT7988_PIN(2, "UART2_CTS"),
	MT7988_PIN(3, "UART2_RTS"),
	MT7988_PIN(4, "GPIO_A"),
	MT7988_PIN(5, "SMI_0_MDC"),
	MT7988_PIN(6, "SMI_0_MDIO"),
	MT7988_PIN(7, "PCIE30_2L_0_WAKE_N"),
	MT7988_PIN(8, "PCIE30_2L_0_CLKREQ_N"),
	MT7988_PIN(9, "PCIE30_1L_1_WAKE_N"),
	MT7988_PIN(10, "PCIE30_1L_1_CLKREQ_N"),
	MT7988_PIN(11, "GPIO_P"),
	MT7988_PIN(12, "WATCHDOG"),
	MT7988_PIN(13, "GPIO_RESET"),
	MT7988_PIN(14, "GPIO_WPS"),
	MT7988_PIN(15, "PMIC_I2C_SCL"),
	MT7988_PIN(16, "PMIC_I2C_SDA"),
	MT7988_PIN(17, "I2C_1_SCL"),
	MT7988_PIN(18, "I2C_1_SDA"),
	MT7988_PIN(19, "PCIE30_2L_0_PRESET_N"),
	MT7988_PIN(20, "PCIE30_1L_1_PRESET_N"),
	MT7988_PIN(21, "PWMD1"),
	MT7988_PIN(22, "SPI0_WP"),
	MT7988_PIN(23, "SPI0_HOLD"),
	MT7988_PIN(24, "SPI0_CSB"),
	MT7988_PIN(25, "SPI0_MISO"),
	MT7988_PIN(26, "SPI0_MOSI"),
	MT7988_PIN(27, "SPI0_CLK"),
	MT7988_PIN(28, "SPI1_CSB"),
	MT7988_PIN(29, "SPI1_MISO"),
	MT7988_PIN(30, "SPI1_MOSI"),
	MT7988_PIN(31, "SPI1_CLK"),
	MT7988_PIN(32, "SPI2_CLK"),
	MT7988_PIN(33, "SPI2_MOSI"),
	MT7988_PIN(34, "SPI2_MISO"),
	MT7988_PIN(35, "SPI2_CSB"),
	MT7988_PIN(36, "SPI2_HOLD"),
	MT7988_PIN(37, "SPI2_WP"),
	MT7988_PIN(38, "EMMC_RSTB"),
	MT7988_PIN(39, "EMMC_DSL"),
	MT7988_PIN(40, "EMMC_CK"),
	MT7988_PIN(41, "EMMC_CMD"),
	MT7988_PIN(42, "EMMC_DATA_7"),
	MT7988_PIN(43, "EMMC_DATA_6"),
	MT7988_PIN(44, "EMMC_DATA_5"),
	MT7988_PIN(45, "EMMC_DATA_4"),
	MT7988_PIN(46, "EMMC_DATA_3"),
	MT7988_PIN(47, "EMMC_DATA_2"),
	MT7988_PIN(48, "EMMC_DATA_1"),
	MT7988_PIN(49, "EMMC_DATA_0"),
	MT7988_PIN(50, "PCM_FS_I2S_LRCK"),
	MT7988_PIN(51, "PCM_CLK_I2S_BCLK"),
	MT7988_PIN(52, "PCM_DRX_I2S_DIN"),
	MT7988_PIN(53, "PCM_DTX_I2S_DOUT"),
	MT7988_PIN(54, "PCM_MCK_I2S_MCLK"),
	MT7988_PIN(55, "UART0_RXD"),
	MT7988_PIN(56, "UART0_TXD"),
	MT7988_PIN(57, "PWMD0"),
	MT7988_PIN(58, "JTAG_JTDI"),
	MT7988_PIN(59, "JTAG_JTDO"),
	MT7988_PIN(60, "JTAG_JTMS"),
	MT7988_PIN(61, "JTAG_JTCLK"),
	MT7988_PIN(62, "JTAG_JTRST_N"),
	MT7988_PIN(63, "USB_DRV_VBUS_P1"),
	MT7988_PIN(64, "LED_A"),
	MT7988_PIN(65, "LED_B"),
	MT7988_PIN(66, "LED_C"),
	MT7988_PIN(67, "LED_D"),
	MT7988_PIN(68, "LED_E"),
	MT7988_PIN(69, "GPIO_B"),
	MT7988_PIN(70, "GPIO_C"),
	MT7988_PIN(71, "I2C_2_SCL"),
	MT7988_PIN(72, "I2C_2_SDA"),
	MT7988_PIN(73, "PCIE30_2L_1_PRESET_N"),
	MT7988_PIN(74, "PCIE30_1L_0_PRESET_N"),
	MT7988_PIN(75, "PCIE30_2L_1_WAKE_N"),
	MT7988_PIN(76, "PCIE30_2L_1_CLKREQ_N"),
	MT7988_PIN(77, "PCIE30_1L_0_WAKE_N"),
	MT7988_PIN(78, "PCIE30_1L_0_CLKREQ_N"),
	MT7988_PIN(79, "USB_DRV_VBUS_P0"),
	MT7988_PIN(80, "UART1_RXD"),
	MT7988_PIN(81, "UART1_TXD"),
	MT7988_PIN(82, "UART1_CTS"),
	MT7988_PIN(83, "UART1_RTS"),
};

/* jtag */
static const int mt7988_tops_jtag0_0_pins[] = { 0, 1, 2, 3, 4 };
static int mt7988_tops_jtag0_0_funcs[] = { 2, 2, 2, 2, 2 };

static const int mt7988_wo0_jtag_pins[] = { 50, 51, 52, 53, 54 };
static int mt7988_wo0_jtag_funcs[] = { 3, 3, 3, 3, 3 };

static const int mt7988_wo1_jtag_pins[] = { 50, 51, 52, 53, 54 };
static int mt7988_wo1_jtag_funcs[] = { 4, 4, 4, 4, 4 };

static const int mt7988_wo2_jtag_pins[] = { 50, 51, 52, 53, 54 };
static int mt7988_wo2_jtag_funcs[] = { 5, 5, 5, 5, 5 };

static const int mt7988_jtag_pins[] = { 58, 59, 60, 61, 62 };
static int mt7988_jtag_funcs[] = { 1, 1, 1, 1, 1 };

static const int mt7988_tops_jtag0_1_pins[] = { 58, 59, 60, 61, 62 };
static int mt7988_tops_jtag0_1_funcs[] = { 4, 4, 4, 4, 4 };

/* int_usxgmii */
static const int mt7988_int_usxgmii_pins[] = { 2, 3 };
static int mt7988_int_usxgmii_funcs[] = { 3, 3 };

/* pwm */
static const int mt7988_pwm0_pins[] = { 57 };
static int mt7988_pwm0_funcs[] = { 1 };

static const int mt7988_pwm1_pins[] = { 21 };
static int mt7988_pwm1_funcs[] = { 1 };

static const int mt7988_pwm2_pins[] = { 80 };
static int mt7988_pwm2_funcs[] = { 2 };

static const int mt7988_pwm2_0_pins[] = { 58 };
static int mt7988_pwm2_0_funcs[] = { 5 };

static const int mt7988_pwm3_pins[] = { 81 };
static int mt7988_pwm3_funcs[] = { 2 };

static const int mt7988_pwm3_0_pins[] = { 59 };
static int mt7988_pwm3_0_funcs[] = { 5 };

static const int mt7988_pwm4_pins[] = { 82 };
static int mt7988_pwm4_funcs[] = { 2 };

static const int mt7988_pwm4_0_pins[] = { 60 };
static int mt7988_pwm4_0_funcs[] = { 5 };

static const int mt7988_pwm5_pins[] = { 83 };
static int mt7988_pwm5_funcs[] = { 2 };

static const int mt7988_pwm5_0_pins[] = { 61 };
static int mt7988_pwm5_0_funcs[] = { 5 };

static const int mt7988_pwm6_pins[] = { 69 };
static int mt7988_pwm6_funcs[] = { 3 };

static const int mt7988_pwm6_0_pins[] = { 62 };
static int mt7988_pwm6_0_funcs[] = { 5 };

static const int mt7988_pwm7_pins[] = { 70 };
static int mt7988_pwm7_funcs[] = { 3 };

static const int mt7988_pwm7_0_pins[] = { 4 };
static int mt7988_pwm7_0_funcs[] = { 3 };

/* dfd */
static const int mt7988_dfd_pins[] = { 0, 1, 2, 3, 4 };
static int mt7988_dfd_funcs[] = { 4, 4, 4, 4, 4 };

/* i2c */
static const int mt7988_xfi_phy0_i2c0_pins[] = { 0, 1 };
static int mt7988_xfi_phy0_i2c0_funcs[] = { 5, 5 };

static const int mt7988_xfi_phy1_i2c0_pins[] = { 0, 1 };
static int mt7988_xfi_phy1_i2c0_funcs[] = { 6, 6 };

static const int mt7988_xfi_phy_pll_i2c0_pins[] = { 3, 4 };
static int mt7988_xfi_phy_pll_i2c0_funcs[] = { 5, 5 };

static const int mt7988_xfi_phy_pll_i2c1_pins[] = { 3, 4 };
static int mt7988_xfi_phy_pll_i2c1_funcs[] = { 6, 6 };

static const int mt7988_i2c0_0_pins[] = { 5, 6 };
static int mt7988_i2c0_0_funcs[] = { 2, 2 };

static const int mt7988_i2c1_sfp_pins[] = { 5, 6 };
static int mt7988_i2c1_sfp_funcs[] = { 4, 4 };

static const int mt7988_xfi_pextp_phy0_i2c_pins[] = { 5, 6 };
static int mt7988_xfi_pextp_phy0_i2c_funcs[] = { 5, 5 };

static const int mt7988_xfi_pextp_phy1_i2c_pins[] = { 5, 6 };
static int mt7988_xfi_pextp_phy1_i2c_funcs[] = { 6, 6 };

static const int mt7988_i2c0_1_pins[] = { 15, 16 };
static int mt7988_i2c0_1_funcs[] = { 1, 1 };

static const int mt7988_u30_phy_i2c0_pins[] = { 15, 16 };
static int mt7988_u30_phy_i2c0_funcs[] = { 2, 2 };

static const int mt7988_u32_phy_i2c0_pins[] = { 15, 16 };
static int mt7988_u32_phy_i2c0_funcs[] = { 3, 3 };

static const int mt7988_xfi_phy0_i2c1_pins[] = { 15, 16 };
static int mt7988_xfi_phy0_i2c1_funcs[] = { 5, 5 };

static const int mt7988_xfi_phy1_i2c1_pins[] = { 15, 16 };
static int mt7988_xfi_phy1_i2c1_funcs[] = { 6, 6 };

static const int mt7988_xfi_phy_pll_i2c2_pins[] = { 15, 16 };
static int mt7988_xfi_phy_pll_i2c2_funcs[] = { 7, 7 };

static const int mt7988_i2c1_0_pins[] = { 17, 18 };
static int mt7988_i2c1_0_funcs[] = { 1, 1 };

static const int mt7988_u30_phy_i2c1_pins[] = { 17, 18 };
static int mt7988_u30_phy_i2c1_funcs[] = { 2, 2 };

static const int mt7988_u32_phy_i2c1_pins[] = { 17, 18 };
static int mt7988_u32_phy_i2c1_funcs[] = { 3, 3 };

static const int mt7988_xfi_phy_pll_i2c3_pins[] = { 17, 18 };
static int mt7988_xfi_phy_pll_i2c3_funcs[] = { 4, 4 };

static const int mt7988_sgmii0_i2c_pins[] = { 17, 18 };
static int mt7988_sgmii0_i2c_funcs[] = { 5, 5 };

static const int mt7988_sgmii1_i2c_pins[] = { 17, 18 };
static int mt7988_sgmii1_i2c_funcs[] = { 6, 6 };

static const int mt7988_i2c1_2_pins[] = { 69, 70 };
static int mt7988_i2c1_2_funcs[] = { 2, 2 };

static const int mt7988_i2c2_0_pins[] = { 69, 70 };
static int mt7988_i2c2_0_funcs[] = { 4, 4 };

static const int mt7988_i2c2_1_pins[] = { 71, 72 };
static int mt7988_i2c2_1_funcs[] = { 1, 1 };

/* eth */
static const int mt7988_mdc_mdio0_pins[] = { 5, 6 };
static int mt7988_mdc_mdio0_funcs[] = { 1, 1 };

static const int mt7988_2p5g_ext_mdio_pins[] = { 28, 29 };
static int mt7988_2p5g_ext_mdio_funcs[] = { 6, 6 };

static const int mt7988_gbe_ext_mdio_pins[] = { 30, 31 };
static int mt7988_gbe_ext_mdio_funcs[] = { 6, 6 };

static const int mt7988_mdc_mdio1_pins[] = { 69, 70 };
static int mt7988_mdc_mdio1_funcs[] = { 1, 1 };

/* pcie */
static const int mt7988_pcie_wake_n0_0_pins[] = { 7 };
static int mt7988_pcie_wake_n0_0_funcs[] = { 1 };

static const int mt7988_pcie_clk_req_n0_0_pins[] = { 8 };
static int mt7988_pcie_clk_req_n0_0_funcs[] = { 1 };

static const int mt7988_pcie_wake_n3_0_pins[] = { 9 };
static int mt7988_pcie_wake_n3_0_funcs[] = { 1 };

static const int mt7988_pcie_clk_req_n3_pins[] = { 10 };
static int mt7988_pcie_clk_req_n3_funcs[] = { 1 };

static const int mt7988_pcie_clk_req_n0_1_pins[] = { 10 };
static int mt7988_pcie_clk_req_n0_1_funcs[] = { 2 };

static const int mt7988_pcie_p0_phy_i2c_pins[] = { 7, 8 };
static int mt7988_pcie_p0_phy_i2c_funcs[] = { 3, 3 };

static const int mt7988_pcie_p1_phy_i2c_pins[] = { 7, 8 };
static int mt7988_pcie_p1_phy_i2c_funcs[] = { 4, 4 };

static const int mt7988_pcie_p3_phy_i2c_pins[] = { 9, 10 };
static int mt7988_pcie_p3_phy_i2c_funcs[] = { 4, 4 };

static const int mt7988_pcie_p2_phy_i2c_pins[] = { 7, 8 };
static int mt7988_pcie_p2_phy_i2c_funcs[] = { 5, 5 };

static const int mt7988_ckm_phy_i2c_pins[] = { 9, 10 };
static int mt7988_ckm_phy_i2c_funcs[] = { 5, 5 };

static const int mt7988_pcie_wake_n0_1_pins[] = { 13 };
static int mt7988_pcie_wake_n0_1_funcs[] = { 2 };

static const int mt7988_pcie_wake_n3_1_pins[] = { 14 };
static int mt7988_pcie_wake_n3_1_funcs[] = { 2 };

static const int mt7988_pcie_2l_0_pereset_pins[] = { 19 };
static int mt7988_pcie_2l_0_pereset_funcs[] = { 1 };

static const int mt7988_pcie_1l_1_pereset_pins[] = { 20 };
static int mt7988_pcie_1l_1_pereset_funcs[] = { 1 };

static const int mt7988_pcie_clk_req_n2_1_pins[] = { 63 };
static int mt7988_pcie_clk_req_n2_1_funcs[] = { 2 };

static const int mt7988_pcie_2l_1_pereset_pins[] = { 73 };
static int mt7988_pcie_2l_1_pereset_funcs[] = { 1 };

static const int mt7988_pcie_1l_0_pereset_pins[] = { 74 };
static int mt7988_pcie_1l_0_pereset_funcs[] = { 1 };

static const int mt7988_pcie_wake_n1_0_pins[] = { 75 };
static int mt7988_pcie_wake_n1_0_funcs[] = { 1 };

static const int mt7988_pcie_clk_req_n1_pins[] = { 76 };
static int mt7988_pcie_clk_req_n1_funcs[] = { 1 };

static const int mt7988_pcie_wake_n2_0_pins[] = { 77 };
static int mt7988_pcie_wake_n2_0_funcs[] = { 1 };

static const int mt7988_pcie_clk_req_n2_0_pins[] = { 78 };
static int mt7988_pcie_clk_req_n2_0_funcs[] = { 1 };

static const int mt7988_pcie_wake_n2_1_pins[] = { 79 };
static int mt7988_pcie_wake_n2_1_funcs[] = { 2 };

/* pmic */
static const int mt7988_pmic_pins[] = { 11 };
static int mt7988_pmic_funcs[] = { 1 };

/* watchdog */
static const int mt7988_watchdog_pins[] = { 12 };
static int mt7988_watchdog_funcs[] = { 1 };

/* spi */
static const int mt7988_spi0_wp_hold_pins[] = { 22, 23 };
static int mt7988_spi0_wp_hold_funcs[] = { 1, 1 };

static const int mt7988_spi0_pins[] = { 24, 25, 26, 27 };
static int mt7988_spi0_funcs[] = { 1, 1, 1, 1 };

static const int mt7988_spi1_pins[] = { 28, 29, 30, 31 };
static int mt7988_spi1_funcs[] = { 1, 1, 1, 1 };

static const int mt7988_spi2_pins[] = { 32, 33, 34, 35 };
static int mt7988_spi2_funcs[] = { 1, 1, 1, 1 };

static const int mt7988_spi2_wp_hold_pins[] = { 36, 37 };
static int mt7988_spi2_wp_hold_funcs[] = { 1, 1 };

/* flash */
static const int mt7988_snfi_pins[] = { 22, 23, 24, 25, 26, 27 };
static int mt7988_snfi_funcs[] = { 2, 2, 2, 2, 2, 2 };

static const int mt7988_emmc_45_pins[] = {
	21, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37
};
static int mt7988_emmc_45_funcs[] = { 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5 };

static const int mt7988_sdcard_pins[] = { 32, 33, 34, 35, 36, 37 };
static int mt7988_sdcard_funcs[] = { 5, 5, 5, 5, 5, 5 };

static const int mt7988_emmc_51_pins[] = { 38, 39, 40, 41, 42, 43,
				     44, 45, 46, 47, 48, 49 };
static int mt7988_emmc_51_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

/* uart */
static const int mt7988_uart2_pins[] = { 0, 1, 2, 3 };
static int mt7988_uart2_funcs[] = { 1, 1, 1, 1 };

static const int mt7988_tops_uart0_0_pins[] = { 22, 23 };
static int mt7988_tops_uart0_0_funcs[] = { 3, 3 };

static const int mt7988_uart2_0_pins[] = { 28, 29, 30, 31 };
static int mt7988_uart2_0_funcs[] = { 2, 2, 2, 2 };

static const int mt7988_uart1_0_pins[] = { 32, 33, 34, 35 };
static int mt7988_uart1_0_funcs[] = { 2, 2, 2, 2 };

static const int mt7988_uart2_1_pins[] = { 32, 33, 34, 35 };
static int mt7988_uart2_1_funcs[] = { 3, 3, 3, 3 };

static const int mt7988_net_wo0_uart_txd_0_pins[] = { 28 };
static int mt7988_net_wo0_uart_txd_0_funcs[] = { 3 };

static const int mt7988_net_wo1_uart_txd_0_pins[] = { 29 };
static int mt7988_net_wo1_uart_txd_0_funcs[] = { 3 };

static const int mt7988_net_wo2_uart_txd_0_pins[] = { 30 };
static int mt7988_net_wo2_uart_txd_0_funcs[] = { 3 };

static const int mt7988_tops_uart1_0_pins[] = { 28, 29 };
static int mt7988_tops_uart1_0_funcs[] = { 4, 4 };

static const int mt7988_tops_uart0_1_pins[] = { 30, 31 };
static int mt7988_tops_uart0_1_funcs[] = { 4, 4 };

static const int mt7988_tops_uart1_1_pins[] = { 36, 37 };
static int mt7988_tops_uart1_1_funcs[] = { 3, 3 };

static const int mt7988_uart0_pins[] = { 55, 56 };
static int mt7988_uart0_funcs[] = { 1, 1 };

static const int mt7988_tops_uart0_2_pins[] = { 55, 56 };
static int mt7988_tops_uart0_2_funcs[] = { 2, 2 };

static const int mt7988_uart2_2_pins[] = { 50, 51, 52, 53 };
static int mt7988_uart2_2_funcs[] = { 2, 2, 2, 2 };

static const int mt7988_uart1_1_pins[] = { 58, 59, 60, 61 };
static int mt7988_uart1_1_funcs[] = { 2, 2, 2, 2 };

static const int mt7988_uart2_3_pins[] = { 58, 59, 60, 61 };
static int mt7988_uart2_3_funcs[] = { 3, 3, 3, 3 };

static const int mt7988_uart1_2_pins[] = { 80, 81, 82, 83 };
static int mt7988_uart1_2_funcs[] = { 1, 1, 1, 1 };

static const int mt7988_uart1_2_lite_pins[] = { 80, 81 };
static int mt7988_uart1_2_lite_funcs[] = { 1, 1 };

static const int mt7988_tops_uart1_2_pins[] = { 80, 81 };
static int mt7988_tops_uart1_2_funcs[] = { 4, 4, };

static const int mt7988_net_wo0_uart_txd_1_pins[] = { 80 };
static int mt7988_net_wo0_uart_txd_1_funcs[] = { 3 };

static const int mt7988_net_wo1_uart_txd_1_pins[] = { 81 };
static int mt7988_net_wo1_uart_txd_1_funcs[] = { 3 };

static const int mt7988_net_wo2_uart_txd_1_pins[] = { 82 };
static int mt7988_net_wo2_uart_txd_1_funcs[] = { 3 };

/* udi */
static const int mt7988_udi_pins[] = { 32, 33, 34, 35, 36 };
static int mt7988_udi_funcs[] = { 4, 4, 4, 4, 4 };

/* i2s */
static const int mt7988_i2s_pins[] = { 50, 51, 52, 53, 54 };
static int mt7988_i2s_funcs[] = { 1, 1, 1, 1, 1 };

/* pcm */
static const int mt7988_pcm_pins[] = { 50, 51, 52, 53 };
static int mt7988_pcm_funcs[] = { 1, 1, 1, 1 };

/* led */
static const int mt7988_gbe0_led1_pins[] = { 58 };
static int mt7988_gbe0_led1_funcs[] = { 6 };
static const int mt7988_gbe1_led1_pins[] = { 59 };
static int mt7988_gbe1_led1_funcs[] = { 6 };
static const int mt7988_gbe2_led1_pins[] = { 60 };
static int mt7988_gbe2_led1_funcs[] = { 6 };
static const int mt7988_gbe3_led1_pins[] = { 61 };
static int mt7988_gbe3_led1_funcs[] = { 6 };

static const int mt7988_2p5gbe_led1_pins[] = { 62 };
static int mt7988_2p5gbe_led1_funcs[] = { 6 };

static const int mt7988_gbe0_led0_pins[] = { 64 };
static int mt7988_gbe0_led0_funcs[] = { 1 };
static const int mt7988_gbe1_led0_pins[] = { 65 };
static int mt7988_gbe1_led0_funcs[] = { 1 };
static const int mt7988_gbe2_led0_pins[] = { 66 };
static int mt7988_gbe2_led0_funcs[] = { 1 };
static const int mt7988_gbe3_led0_pins[] = { 67 };
static int mt7988_gbe3_led0_funcs[] = { 1 };

static const int mt7988_2p5gbe_led0_pins[] = { 68 };
static int mt7988_2p5gbe_led0_funcs[] = { 1 };

/* usb */
static const int mt7988_drv_vbus_p1_pins[] = { 63 };
static int mt7988_drv_vbus_p1_funcs[] = { 1 };

static const int mt7988_drv_vbus_pins[] = { 79 };
static int mt7988_drv_vbus_funcs[] = { 1 };

static const struct group_desc mt7988_groups[] = {
	/*  @GPIO(0,1,2,3): uart2 */
	PINCTRL_PIN_GROUP("uart2", mt7988_uart2),
	/*  @GPIO(0,1,2,3,4): tops_jtag0_0 */
	PINCTRL_PIN_GROUP("tops_jtag0_0", mt7988_tops_jtag0_0),
	/*  @GPIO(2,3): int_usxgmii */
	PINCTRL_PIN_GROUP("int_usxgmii", mt7988_int_usxgmii),
	/*  @GPIO(0,1,2,3,4): dfd */
	PINCTRL_PIN_GROUP("dfd", mt7988_dfd),
	/*  @GPIO(0,1): xfi_phy0_i2c0 */
	PINCTRL_PIN_GROUP("xfi_phy0_i2c0", mt7988_xfi_phy0_i2c0),
	/*  @GPIO(0,1): xfi_phy1_i2c0 */
	PINCTRL_PIN_GROUP("xfi_phy1_i2c0", mt7988_xfi_phy1_i2c0),
	/*  @GPIO(3,4): xfi_phy_pll_i2c0 */
	PINCTRL_PIN_GROUP("xfi_phy_pll_i2c0", mt7988_xfi_phy_pll_i2c0),
	/*  @GPIO(3,4): xfi_phy_pll_i2c1 */
	PINCTRL_PIN_GROUP("xfi_phy_pll_i2c1", mt7988_xfi_phy_pll_i2c1),
	/*  @GPIO(4): pwm7 */
	PINCTRL_PIN_GROUP("pwm7_0", mt7988_pwm7_0),
	/*  @GPIO(5,6) i2c0_0 */
	PINCTRL_PIN_GROUP("i2c0_0", mt7988_i2c0_0),
	/*  @GPIO(5,6) i2c1_sfp */
	PINCTRL_PIN_GROUP("i2c1_sfp", mt7988_i2c1_sfp),
	/*  @GPIO(5,6) xfi_pextp_phy0_i2c */
	PINCTRL_PIN_GROUP("xfi_pextp_phy0_i2c", mt7988_xfi_pextp_phy0_i2c),
	/*  @GPIO(5,6) xfi_pextp_phy1_i2c */
	PINCTRL_PIN_GROUP("xfi_pextp_phy1_i2c", mt7988_xfi_pextp_phy1_i2c),
	/*  @GPIO(5,6) mdc_mdio0 */
	PINCTRL_PIN_GROUP("mdc_mdio0", mt7988_mdc_mdio0),
	/*  @GPIO(7): pcie_wake_n0_0 */
	PINCTRL_PIN_GROUP("pcie_wake_n0_0", mt7988_pcie_wake_n0_0),
	/*  @GPIO(8): pcie_clk_req_n0_0 */
	PINCTRL_PIN_GROUP("pcie_clk_req_n0_0", mt7988_pcie_clk_req_n0_0),
	/*  @GPIO(9): pcie_wake_n3_0 */
	PINCTRL_PIN_GROUP("pcie_wake_n3_0", mt7988_pcie_wake_n3_0),
	/*  @GPIO(10): pcie_clk_req_n3 */
	PINCTRL_PIN_GROUP("pcie_clk_req_n3", mt7988_pcie_clk_req_n3),
	/*  @GPIO(10): pcie_clk_req_n0_1 */
	PINCTRL_PIN_GROUP("pcie_clk_req_n0_1", mt7988_pcie_clk_req_n0_1),
	/*  @GPIO(7,8) pcie_p0_phy_i2c */
	PINCTRL_PIN_GROUP("pcie_p0_phy_i2c", mt7988_pcie_p0_phy_i2c),
	/*  @GPIO(7,8) pcie_p1_phy_i2c */
	PINCTRL_PIN_GROUP("pcie_p1_phy_i2c", mt7988_pcie_p1_phy_i2c),
	/*  @GPIO(7,8) pcie_p2_phy_i2c */
	PINCTRL_PIN_GROUP("pcie_p2_phy_i2c", mt7988_pcie_p2_phy_i2c),
	/*  @GPIO(9,10) pcie_p3_phy_i2c */
	PINCTRL_PIN_GROUP("pcie_p3_phy_i2c", mt7988_pcie_p3_phy_i2c),
	/*  @GPIO(9,10) ckm_phy_i2c */
	PINCTRL_PIN_GROUP("ckm_phy_i2c", mt7988_ckm_phy_i2c),
	/*  @GPIO(11): pmic */
	PINCTRL_PIN_GROUP("pcie_pmic", mt7988_pmic),
	/*  @GPIO(12): watchdog */
	PINCTRL_PIN_GROUP("watchdog", mt7988_watchdog),
	/*  @GPIO(13): pcie_wake_n0_1 */
	PINCTRL_PIN_GROUP("pcie_wake_n0_1", mt7988_pcie_wake_n0_1),
	/*  @GPIO(14): pcie_wake_n3_1 */
	PINCTRL_PIN_GROUP("pcie_wake_n3_1", mt7988_pcie_wake_n3_1),
	/*  @GPIO(15,16) i2c0_1 */
	PINCTRL_PIN_GROUP("i2c0_1", mt7988_i2c0_1),
	/*  @GPIO(15,16) u30_phy_i2c0 */
	PINCTRL_PIN_GROUP("u30_phy_i2c0", mt7988_u30_phy_i2c0),
	/*  @GPIO(15,16) u32_phy_i2c0 */
	PINCTRL_PIN_GROUP("u32_phy_i2c0", mt7988_u32_phy_i2c0),
	/*  @GPIO(15,16) xfi_phy0_i2c1 */
	PINCTRL_PIN_GROUP("xfi_phy0_i2c1", mt7988_xfi_phy0_i2c1),
	/*  @GPIO(15,16) xfi_phy1_i2c1 */
	PINCTRL_PIN_GROUP("xfi_phy1_i2c1", mt7988_xfi_phy1_i2c1),
	/*  @GPIO(15,16) xfi_phy_pll_i2c2 */
	PINCTRL_PIN_GROUP("xfi_phy_pll_i2c2", mt7988_xfi_phy_pll_i2c2),
	/*  @GPIO(17,18) i2c1_0 */
	PINCTRL_PIN_GROUP("i2c1_0", mt7988_i2c1_0),
	/*  @GPIO(17,18) u30_phy_i2c1 */
	PINCTRL_PIN_GROUP("u30_phy_i2c1", mt7988_u30_phy_i2c1),
	/*  @GPIO(17,18) u32_phy_i2c1 */
	PINCTRL_PIN_GROUP("u32_phy_i2c1", mt7988_u32_phy_i2c1),
	/*  @GPIO(17,18) xfi_phy_pll_i2c3 */
	PINCTRL_PIN_GROUP("xfi_phy_pll_i2c3", mt7988_xfi_phy_pll_i2c3),
	/*  @GPIO(17,18) sgmii0_i2c */
	PINCTRL_PIN_GROUP("sgmii0_i2c", mt7988_sgmii0_i2c),
	/*  @GPIO(17,18) sgmii1_i2c */
	PINCTRL_PIN_GROUP("sgmii1_i2c", mt7988_sgmii1_i2c),
	/*  @GPIO(19): pcie_2l_0_pereset */
	PINCTRL_PIN_GROUP("pcie_2l_0_pereset", mt7988_pcie_2l_0_pereset),
	/*  @GPIO(20): pcie_1l_1_pereset */
	PINCTRL_PIN_GROUP("pcie_1l_1_pereset", mt7988_pcie_1l_1_pereset),
	/*  @GPIO(21): pwm1 */
	PINCTRL_PIN_GROUP("pwm1", mt7988_pwm1),
	/*  @GPIO(22,23) spi0_wp_hold */
	PINCTRL_PIN_GROUP("spi0_wp_hold", mt7988_spi0_wp_hold),
	/*  @GPIO(24,25,26,27) spi0 */
	PINCTRL_PIN_GROUP("spi0", mt7988_spi0),
	/*  @GPIO(28,29,30,31) spi1 */
	PINCTRL_PIN_GROUP("spi1", mt7988_spi1),
	/*  @GPIO(32,33,34,35) spi2 */
	PINCTRL_PIN_GROUP("spi2", mt7988_spi2),
	/*  @GPIO(36,37) spi2_wp_hold */
	PINCTRL_PIN_GROUP("spi2_wp_hold", mt7988_spi2_wp_hold),
	/*  @GPIO(22,23,24,25,26,27) snfi */
	PINCTRL_PIN_GROUP("snfi", mt7988_snfi),
	/*  @GPIO(22,23) tops_uart0_0 */
	PINCTRL_PIN_GROUP("tops_uart0_0", mt7988_tops_uart0_0),
	/*  @GPIO(28,29,30,31) uart2_0 */
	PINCTRL_PIN_GROUP("uart2_0", mt7988_uart2_0),
	/*  @GPIO(32,33,34,35) uart1_0 */
	PINCTRL_PIN_GROUP("uart1_0", mt7988_uart1_0),
	/*  @GPIO(32,33,34,35) uart2_1 */
	PINCTRL_PIN_GROUP("uart2_1", mt7988_uart2_1),
	/*  @GPIO(28) net_wo0_uart_txd_0 */
	PINCTRL_PIN_GROUP("net_wo0_uart_txd_0", mt7988_net_wo0_uart_txd_0),
	/*  @GPIO(29) net_wo1_uart_txd_0 */
	PINCTRL_PIN_GROUP("net_wo1_uart_txd_0", mt7988_net_wo1_uart_txd_0),
	/*  @GPIO(30) net_wo2_uart_txd_0 */
	PINCTRL_PIN_GROUP("net_wo2_uart_txd_0", mt7988_net_wo2_uart_txd_0),
	/*  @GPIO(28,29) tops_uart1_0 */
	PINCTRL_PIN_GROUP("tops_uart0_0", mt7988_tops_uart1_0),
	/*  @GPIO(30,31) tops_uart0_1 */
	PINCTRL_PIN_GROUP("tops_uart0_1", mt7988_tops_uart0_1),
	/*  @GPIO(36,37) tops_uart1_1 */
	PINCTRL_PIN_GROUP("tops_uart1_1", mt7988_tops_uart1_1),
	/*  @GPIO(32,33,34,35,36) udi */
	PINCTRL_PIN_GROUP("udi", mt7988_udi),
	/*  @GPIO(21,28,29,30,31,32,33,34,35,36,37) emmc_45 */
	PINCTRL_PIN_GROUP("emmc_45", mt7988_emmc_45),
	/*  @GPIO(32,33,34,35,36,37) sdcard */
	PINCTRL_PIN_GROUP("sdcard", mt7988_sdcard),
	/*  @GPIO(38,39,40,41,42,43,44,45,46,47,48,49) emmc_51 */
	PINCTRL_PIN_GROUP("emmc_51", mt7988_emmc_51),
	/*  @GPIO(28,29) 2p5g_ext_mdio */
	PINCTRL_PIN_GROUP("2p5g_ext_mdio", mt7988_2p5g_ext_mdio),
	/*  @GPIO(30,31) gbe_ext_mdio */
	PINCTRL_PIN_GROUP("gbe_ext_mdio", mt7988_gbe_ext_mdio),
	/*  @GPIO(50,51,52,53,54) i2s */
	PINCTRL_PIN_GROUP("i2s", mt7988_i2s),
	/*  @GPIO(50,51,52,53) pcm */
	PINCTRL_PIN_GROUP("pcm", mt7988_pcm),
	/*  @GPIO(55,56) uart0 */
	PINCTRL_PIN_GROUP("uart0", mt7988_uart0),
	/*  @GPIO(55,56) tops_uart0_2 */
	PINCTRL_PIN_GROUP("tops_uart0_2", mt7988_tops_uart0_2),
	/*  @GPIO(50,51,52,53) uart2_2 */
	PINCTRL_PIN_GROUP("uart2_2", mt7988_uart2_2),
	/*  @GPIO(50,51,52,53,54) wo0_jtag */
	PINCTRL_PIN_GROUP("wo0_jtag", mt7988_wo0_jtag),
	/*  @GPIO(50,51,52,53,54) wo1-wo1_jtag */
	PINCTRL_PIN_GROUP("wo1_jtag", mt7988_wo1_jtag),
	/*  @GPIO(50,51,52,53,54) wo2_jtag */
	PINCTRL_PIN_GROUP("wo2_jtag", mt7988_wo2_jtag),
	/*  @GPIO(57) pwm0 */
	PINCTRL_PIN_GROUP("pwm0", mt7988_pwm0),
	/*  @GPIO(58) pwm2_0 */
	PINCTRL_PIN_GROUP("pwm2_0", mt7988_pwm2_0),
	/*  @GPIO(59) pwm3_0 */
	PINCTRL_PIN_GROUP("pwm3_0", mt7988_pwm3_0),
	/*  @GPIO(60) pwm4_0 */
	PINCTRL_PIN_GROUP("pwm4_0", mt7988_pwm4_0),
	/*  @GPIO(61) pwm5_0 */
	PINCTRL_PIN_GROUP("pwm5_0", mt7988_pwm5_0),
	/*  @GPIO(58,59,60,61,62) jtag */
	PINCTRL_PIN_GROUP("jtag", mt7988_jtag),
	/*  @GPIO(58,59,60,61,62) tops_jtag0_1 */
	PINCTRL_PIN_GROUP("tops_jtag0_1", mt7988_tops_jtag0_1),
	/*  @GPIO(58,59,60,61) uart2_3 */
	PINCTRL_PIN_GROUP("uart2_3", mt7988_uart2_3),
	/*  @GPIO(58,59,60,61) uart1_1 */
	PINCTRL_PIN_GROUP("uart1_1", mt7988_uart1_1),
	/*  @GPIO(58,59,60,61) gbe_led1 */
	PINCTRL_PIN_GROUP("gbe0_led1", mt7988_gbe0_led1),
	PINCTRL_PIN_GROUP("gbe1_led1", mt7988_gbe1_led1),
	PINCTRL_PIN_GROUP("gbe2_led1", mt7988_gbe2_led1),
	PINCTRL_PIN_GROUP("gbe3_led1", mt7988_gbe3_led1),
	/*  @GPIO(62) pwm6_0 */
	PINCTRL_PIN_GROUP("pwm6_0", mt7988_pwm6_0),
	/*  @GPIO(62) 2p5gbe_led1 */
	PINCTRL_PIN_GROUP("2p5gbe_led1", mt7988_2p5gbe_led1),
	/*  @GPIO(64,65,66,67) gbe_led0 */
	PINCTRL_PIN_GROUP("gbe0_led0", mt7988_gbe0_led0),
	PINCTRL_PIN_GROUP("gbe1_led0", mt7988_gbe1_led0),
	PINCTRL_PIN_GROUP("gbe2_led0", mt7988_gbe2_led0),
	PINCTRL_PIN_GROUP("gbe3_led0", mt7988_gbe3_led0),
	/*  @GPIO(68) 2p5gbe_led0 */
	PINCTRL_PIN_GROUP("2p5gbe_led0", mt7988_2p5gbe_led0),
	/*  @GPIO(63) drv_vbus_p1 */
	PINCTRL_PIN_GROUP("drv_vbus_p1", mt7988_drv_vbus_p1),
	/*  @GPIO(63) pcie_clk_req_n2_1 */
	PINCTRL_PIN_GROUP("pcie_clk_req_n2_1", mt7988_pcie_clk_req_n2_1),
	/*  @GPIO(69, 70) mdc_mdio1 */
	PINCTRL_PIN_GROUP("mdc_mdio1", mt7988_mdc_mdio1),
	/*  @GPIO(69, 70) i2c1_2 */
	PINCTRL_PIN_GROUP("i2c1_2", mt7988_i2c1_2),
	/*  @GPIO(69) pwm6 */
	PINCTRL_PIN_GROUP("pwm6", mt7988_pwm6),
	/*  @GPIO(70) pwm7 */
	PINCTRL_PIN_GROUP("pwm7", mt7988_pwm7),
	/*  @GPIO(69,70) i2c2_0 */
	PINCTRL_PIN_GROUP("i2c2_0", mt7988_i2c2_0),
	/*  @GPIO(71,72) i2c2_1 */
	PINCTRL_PIN_GROUP("i2c2_1", mt7988_i2c2_1),
	/*  @GPIO(73) pcie_2l_1_pereset */
	PINCTRL_PIN_GROUP("pcie_2l_1_pereset", mt7988_pcie_2l_1_pereset),
	/*  @GPIO(74) pcie_1l_0_pereset */
	PINCTRL_PIN_GROUP("pcie_1l_0_pereset", mt7988_pcie_1l_0_pereset),
	/*  @GPIO(75) pcie_wake_n1_0 */
	PINCTRL_PIN_GROUP("pcie_wake_n1_0", mt7988_pcie_wake_n1_0),
	/*  @GPIO(76) pcie_clk_req_n1 */
	PINCTRL_PIN_GROUP("pcie_clk_req_n1", mt7988_pcie_clk_req_n1),
	/*  @GPIO(77) pcie_wake_n2_0 */
	PINCTRL_PIN_GROUP("pcie_wake_n2_0", mt7988_pcie_wake_n2_0),
	/*  @GPIO(78) pcie_clk_req_n2_0 */
	PINCTRL_PIN_GROUP("pcie_clk_req_n2_0", mt7988_pcie_clk_req_n2_0),
	/*  @GPIO(79) drv_vbus */
	PINCTRL_PIN_GROUP("drv_vbus", mt7988_drv_vbus),
	/*  @GPIO(79) pcie_wake_n2_1 */
	PINCTRL_PIN_GROUP("pcie_wake_n2_1", mt7988_pcie_wake_n2_1),
	/*  @GPIO(80,81,82,83) uart1_2 */
	PINCTRL_PIN_GROUP("uart1_2", mt7988_uart1_2),
	/*  @GPIO(80,81) uart1_2_lite */
	PINCTRL_PIN_GROUP("uart1_2_lite", mt7988_uart1_2_lite),
	/*  @GPIO(80) pwm2 */
	PINCTRL_PIN_GROUP("pwm2", mt7988_pwm2),
	/*  @GPIO(81) pwm3 */
	PINCTRL_PIN_GROUP("pwm3", mt7988_pwm3),
	/*  @GPIO(82) pwm4 */
	PINCTRL_PIN_GROUP("pwm4", mt7988_pwm4),
	/*  @GPIO(83) pwm5 */
	PINCTRL_PIN_GROUP("pwm5", mt7988_pwm5),
	/*  @GPIO(80) net_wo0_uart_txd_0 */
	PINCTRL_PIN_GROUP("net_wo0_uart_txd_0", mt7988_net_wo0_uart_txd_0),
	/*  @GPIO(81) net_wo1_uart_txd_0 */
	PINCTRL_PIN_GROUP("net_wo1_uart_txd_0", mt7988_net_wo1_uart_txd_0),
	/*  @GPIO(82) net_wo2_uart_txd_0 */
	PINCTRL_PIN_GROUP("net_wo2_uart_txd_0", mt7988_net_wo2_uart_txd_0),
	/*  @GPIO(80,81) tops_uart1_2 */
	PINCTRL_PIN_GROUP("tops_uart1_2", mt7988_tops_uart1_2),
	/*  @GPIO(80) net_wo0_uart_txd_1 */
	PINCTRL_PIN_GROUP("net_wo0_uart_txd_1", mt7988_net_wo0_uart_txd_1),
	/*  @GPIO(81) net_wo1_uart_txd_1 */
	PINCTRL_PIN_GROUP("net_wo1_uart_txd_1", mt7988_net_wo1_uart_txd_1),
	/*  @GPIO(82) net_wo2_uart_txd_1 */
	PINCTRL_PIN_GROUP("net_wo2_uart_txd_1", mt7988_net_wo2_uart_txd_1),
};

/* Joint those groups owning the same capability in user point of view which
 * allows that people tend to use through the device tree.
 */
static const char * const mt7988_jtag_groups[] = {
	"tops_jtag0_0", "wo0_jtag", "wo1_jtag",
	"wo2_jtag",	"jtag",	    "tops_jtag0_1",
};
static const char * const mt7988_int_usxgmii_groups[] = {
	"int_usxgmii",
};
static const char * const mt7988_pwm_groups[] = {
	"pwm0", "pwm1", "pwm2", "pwm2_0", "pwm3", "pwm3_0", "pwm4", "pwm4_0",
	"pwm5", "pwm5_0", "pwm6", "pwm6_0", "pwm7", "pwm7_0",

};
static const char * const mt7988_dfd_groups[] = {
	"dfd",
};
static const char * const mt7988_i2c_groups[] = {
	"xfi_phy0_i2c0",
	"xfi_phy1_i2c0",
	"xfi_phy_pll_i2c0",
	"xfi_phy_pll_i2c1",
	"i2c0_0",
	"i2c1_sfp",
	"xfi_pextp_phy0_i2c",
	"xfi_pextp_phy1_i2c",
	"i2c0_1",
	"u30_phy_i2c0",
	"u32_phy_i2c0",
	"xfi_phy0_i2c1",
	"xfi_phy1_i2c1",
	"xfi_phy_pll_i2c2",
	"i2c1_0",
	"u30_phy_i2c1",
	"u32_phy_i2c1",
	"xfi_phy_pll_i2c3",
	"sgmii0_i2c",
	"sgmii1_i2c",
	"i2c1_2",
	"i2c2_0",
	"i2c2_1",
};
static const char * const mt7988_ethernet_groups[] = {
	"mdc_mdio0",
	"2p5g_ext_mdio",
	"gbe_ext_mdio",
	"mdc_mdio1",
};
static const char * const mt7988_pcie_groups[] = {
	"pcie_wake_n0_0",    "pcie_clk_req_n0_0", "pcie_wake_n3_0",
	"pcie_clk_req_n3",   "pcie_p0_phy_i2c",	  "pcie_p1_phy_i2c",
	"pcie_p3_phy_i2c",   "pcie_p2_phy_i2c",	  "ckm_phy_i2c",
	"pcie_wake_n0_1",    "pcie_wake_n3_1",	  "pcie_2l_0_pereset",
	"pcie_1l_1_pereset", "pcie_clk_req_n2_1", "pcie_2l_1_pereset",
	"pcie_1l_0_pereset", "pcie_wake_n1_0",	  "pcie_clk_req_n1",
	"pcie_wake_n2_0",    "pcie_clk_req_n2_0", "pcie_wake_n2_1",
	"pcie_clk_req_n0_1"
};
static const char * const mt7988_pmic_groups[] = {
	"pmic",
};
static const char * const mt7988_wdt_groups[] = {
	"watchdog",
};
static const char * const mt7988_spi_groups[] = {
	"spi0", "spi0_wp_hold", "spi1", "spi2", "spi2_wp_hold",
};
static const char * const mt7988_flash_groups[] = { "emmc_45", "sdcard", "snfi",
						    "emmc_51" };
static const char * const mt7988_uart_groups[] = {
	"uart2",
	"tops_uart0_0",
	"uart2_0",
	"uart1_0",
	"uart2_1",
	"net_wo0_uart_txd_0",
	"net_wo1_uart_txd_0",
	"net_wo2_uart_txd_0",
	"tops_uart1_0",
	"ops_uart0_1",
	"ops_uart1_1",
	"uart0",
	"tops_uart0_2",
	"uart1_1",
	"uart2_3",
	"uart1_2",
	"uart1_2_lite",
	"tops_uart1_2",
	"net_wo0_uart_txd_1",
	"net_wo1_uart_txd_1",
	"net_wo2_uart_txd_1",
};
static const char * const mt7988_udi_groups[] = {
	"udi",
};
static const char * const mt7988_audio_groups[] = {
	"i2s", "pcm",
};
static const char * const mt7988_led_groups[] = {
	"gbe0_led1", "gbe1_led1", "gbe2_led1", "gbe3_led1", "2p5gbe_led1",
	"gbe0_led0", "gbe1_led0", "gbe2_led0", "gbe3_led0", "2p5gbe_led0",
	"wf5g_led0",   "wf5g_led1",
};
static const char * const mt7988_usb_groups[] = {
	"drv_vbus",
	"drv_vbus_p1",
};

static const struct pinfunction mt7988_functions[] = {
	PINCTRL_PIN_FUNCTION("audio", mt7988_audio),
	PINCTRL_PIN_FUNCTION("jtag", mt7988_jtag),
	PINCTRL_PIN_FUNCTION("int_usxgmii", mt7988_int_usxgmii),
	PINCTRL_PIN_FUNCTION("pwm", mt7988_pwm),
	PINCTRL_PIN_FUNCTION("dfd", mt7988_dfd),
	PINCTRL_PIN_FUNCTION("i2c", mt7988_i2c),
	PINCTRL_PIN_FUNCTION("eth", mt7988_ethernet),
	PINCTRL_PIN_FUNCTION("pcie", mt7988_pcie),
	PINCTRL_PIN_FUNCTION("pmic", mt7988_pmic),
	PINCTRL_PIN_FUNCTION("watchdog", mt7988_wdt),
	PINCTRL_PIN_FUNCTION("spi", mt7988_spi),
	PINCTRL_PIN_FUNCTION("flash", mt7988_flash),
	PINCTRL_PIN_FUNCTION("uart", mt7988_uart),
	PINCTRL_PIN_FUNCTION("udi", mt7988_udi),
	PINCTRL_PIN_FUNCTION("usb", mt7988_usb),
	PINCTRL_PIN_FUNCTION("led", mt7988_led),
};

static const struct mtk_eint_hw mt7988_eint_hw = {
	.port_mask = 7,
	.ports = 7,
	.ap_num = ARRAY_SIZE(mt7988_pins),
	.db_cnt = 16,
};

static const char * const mt7988_pinctrl_register_base_names[] = {
	"gpio",	 "iocfg_tr", "iocfg_br",
	"iocfg_rb", "iocfg_lb", "iocfg_tl",
};

static const struct mtk_pin_soc mt7988_data = {
	.reg_cal = mt7988_reg_cals,
	.pins = mt7988_pins,
	.npins = ARRAY_SIZE(mt7988_pins),
	.grps = mt7988_groups,
	.ngrps = ARRAY_SIZE(mt7988_groups),
	.funcs = mt7988_functions,
	.nfuncs = ARRAY_SIZE(mt7988_functions),
	.eint_hw = &mt7988_eint_hw,
	.gpio_m = 0,
	.ies_present = false,
	.base_names = mt7988_pinctrl_register_base_names,
	.nbase_names = ARRAY_SIZE(mt7988_pinctrl_register_base_names),
	.bias_disable_set = mtk_pinconf_bias_disable_set,
	.bias_disable_get = mtk_pinconf_bias_disable_get,
	.bias_set = mtk_pinconf_bias_set,
	.bias_get = mtk_pinconf_bias_get,
	.pull_type = mt7988_pull_type,
	.bias_set_combo = mtk_pinconf_bias_set_combo,
	.bias_get_combo = mtk_pinconf_bias_get_combo,
	.drive_set = mtk_pinconf_drive_set_rev1,
	.drive_get = mtk_pinconf_drive_get_rev1,
	.adv_pull_get = mtk_pinconf_adv_pull_get,
	.adv_pull_set = mtk_pinconf_adv_pull_set,
};

static const struct of_device_id mt7988_pinctrl_of_match[] = {
	{ .compatible = "mediatek,mt7988-pinctrl" },
	{}
};

static int mt7988_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_moore_pinctrl_probe(pdev, &mt7988_data);
}

static struct platform_driver mt7988_pinctrl_driver = {
	.driver = {
		.name = "mt7988-pinctrl",
		.of_match_table = mt7988_pinctrl_of_match,
	},
	.probe = mt7988_pinctrl_probe,
};

static int __init mt7988_pinctrl_init(void)
{
	return platform_driver_register(&mt7988_pinctrl_driver);
}
arch_initcall(mt7988_pinctrl_init);
