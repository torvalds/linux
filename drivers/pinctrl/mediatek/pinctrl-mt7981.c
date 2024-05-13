// SPDX-License-Identifier: GPL-2.0
/*
 * The MT7981 driver based on Linux generic pinctrl binding.
 *
 * Copyright (C) 2020 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 */

#include "pinctrl-moore.h"

#define MT7981_PIN(_number, _name)				\
	MTK_PIN(_number, _name, 0, _number, DRV_GRP4)

#define PIN_FIELD_BASE(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit, _x_bits)	\
	PIN_FIELD_CALC(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit,	\
		       _x_bits, 32, 0)

#define PINS_FIELD_BASE(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit, _x_bits)	\
	PIN_FIELD_CALC(_s_pin, _e_pin, _i_base, _s_addr, _x_addrs, _s_bit,	\
		      _x_bits, 32, 1)

static const struct mtk_pin_field_calc mt7981_pin_mode_range[] = {
	PIN_FIELD(0, 56, 0x300, 0x10, 0, 4),
};

static const struct mtk_pin_field_calc mt7981_pin_dir_range[] = {
	PIN_FIELD(0, 56, 0x0, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_di_range[] = {
	PIN_FIELD(0, 56, 0x200, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_do_range[] = {
	PIN_FIELD(0, 56, 0x100, 0x10, 0, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_ies_range[] = {
	PIN_FIELD_BASE(0, 0, 1, 0x10, 0x10, 1, 1),
	PIN_FIELD_BASE(1, 1, 1, 0x10, 0x10, 0, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x20, 0x10, 6, 1),
	PIN_FIELD_BASE(3, 3, 4, 0x20, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 4, 0x20, 0x10, 2, 1),
	PIN_FIELD_BASE(5, 5, 4, 0x20, 0x10, 1, 1),
	PIN_FIELD_BASE(6, 6, 4, 0x20, 0x10, 3, 1),
	PIN_FIELD_BASE(7, 7, 4, 0x20, 0x10, 0, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x20, 0x10, 4, 1),

	PIN_FIELD_BASE(9, 9, 5, 0x20, 0x10, 9, 1),
	PIN_FIELD_BASE(10, 10, 5, 0x20, 0x10, 8, 1),
	PIN_FIELD_BASE(11, 11, 5, 0x40, 0x10, 10, 1),
	PIN_FIELD_BASE(12, 12, 5, 0x20, 0x10, 7, 1),
	PIN_FIELD_BASE(13, 13, 5, 0x20, 0x10, 11, 1),

	PIN_FIELD_BASE(14, 14, 4, 0x20, 0x10, 8, 1),

	PIN_FIELD_BASE(15, 15, 2, 0x20, 0x10, 0, 1),
	PIN_FIELD_BASE(16, 16, 2, 0x20, 0x10, 1, 1),
	PIN_FIELD_BASE(17, 17, 2, 0x20, 0x10, 5, 1),
	PIN_FIELD_BASE(18, 18, 2, 0x20, 0x10, 4, 1),
	PIN_FIELD_BASE(19, 19, 2, 0x20, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 2, 0x20, 0x10, 3, 1),
	PIN_FIELD_BASE(21, 21, 2, 0x20, 0x10, 6, 1),
	PIN_FIELD_BASE(22, 22, 2, 0x20, 0x10, 7, 1),
	PIN_FIELD_BASE(23, 23, 2, 0x20, 0x10, 10, 1),
	PIN_FIELD_BASE(24, 24, 2, 0x20, 0x10, 9, 1),
	PIN_FIELD_BASE(25, 25, 2, 0x20, 0x10, 8, 1),

	PIN_FIELD_BASE(26, 26, 5, 0x20, 0x10, 0, 1),
	PIN_FIELD_BASE(27, 27, 5, 0x20, 0x10, 4, 1),
	PIN_FIELD_BASE(28, 28, 5, 0x20, 0x10, 3, 1),
	PIN_FIELD_BASE(29, 29, 5, 0x20, 0x10, 1, 1),
	PIN_FIELD_BASE(30, 30, 5, 0x20, 0x10, 2, 1),
	PIN_FIELD_BASE(31, 31, 5, 0x20, 0x10, 5, 1),

	PIN_FIELD_BASE(32, 32, 1, 0x10, 0x10, 2, 1),
	PIN_FIELD_BASE(33, 33, 1, 0x10, 0x10, 3, 1),

	PIN_FIELD_BASE(34, 34, 4, 0x20, 0x10, 5, 1),
	PIN_FIELD_BASE(35, 35, 4, 0x20, 0x10, 7, 1),

	PIN_FIELD_BASE(36, 36, 3, 0x10, 0x10, 2, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x10, 0x10, 3, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x10, 0x10, 0, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x10, 0x10, 1, 1),

	PIN_FIELD_BASE(40, 40, 7, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(41, 41, 7, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(42, 42, 7, 0x30, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 7, 0x30, 0x10, 7, 1),
	PIN_FIELD_BASE(44, 44, 7, 0x30, 0x10, 8, 1),
	PIN_FIELD_BASE(45, 45, 7, 0x30, 0x10, 3, 1),
	PIN_FIELD_BASE(46, 46, 7, 0x30, 0x10, 4, 1),
	PIN_FIELD_BASE(47, 47, 7, 0x30, 0x10, 5, 1),
	PIN_FIELD_BASE(48, 48, 7, 0x30, 0x10, 6, 1),
	PIN_FIELD_BASE(49, 49, 7, 0x30, 0x10, 2, 1),

	PIN_FIELD_BASE(50, 50, 6, 0x10, 0x10, 0, 1),
	PIN_FIELD_BASE(51, 51, 6, 0x10, 0x10, 2, 1),
	PIN_FIELD_BASE(52, 52, 6, 0x10, 0x10, 3, 1),
	PIN_FIELD_BASE(53, 53, 6, 0x10, 0x10, 4, 1),
	PIN_FIELD_BASE(54, 54, 6, 0x10, 0x10, 5, 1),
	PIN_FIELD_BASE(55, 55, 6, 0x10, 0x10, 6, 1),
	PIN_FIELD_BASE(56, 56, 6, 0x10, 0x10, 1, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_smt_range[] = {
	PIN_FIELD_BASE(0, 0, 1, 0x60, 0x10, 1, 1),
	PIN_FIELD_BASE(1, 1, 1, 0x60, 0x10, 0, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x90, 0x10, 6, 1),
	PIN_FIELD_BASE(3, 3, 4, 0x80, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 4, 0x80, 0x10, 2, 1),
	PIN_FIELD_BASE(5, 5, 4, 0x80, 0x10, 1, 1),
	PIN_FIELD_BASE(6, 6, 4, 0x80, 0x10, 3, 1),
	PIN_FIELD_BASE(7, 7, 4, 0x80, 0x10, 0, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x80, 0x10, 4, 1),

	PIN_FIELD_BASE(9, 9, 5, 0x90, 0x10, 9, 1),
	PIN_FIELD_BASE(10, 10, 5, 0x90, 0x10, 8, 1),
	PIN_FIELD_BASE(11, 11, 5, 0x90, 0x10, 10, 1),
	PIN_FIELD_BASE(12, 12, 5, 0x90, 0x10, 7, 1),
	PIN_FIELD_BASE(13, 13, 5, 0x90, 0x10, 11, 1),

	PIN_FIELD_BASE(14, 14, 4, 0x80, 0x10, 8, 1),

	PIN_FIELD_BASE(15, 15, 2, 0x90, 0x10, 0, 1),
	PIN_FIELD_BASE(16, 16, 2, 0x90, 0x10, 1, 1),
	PIN_FIELD_BASE(17, 17, 2, 0x90, 0x10, 5, 1),
	PIN_FIELD_BASE(18, 18, 2, 0x90, 0x10, 4, 1),
	PIN_FIELD_BASE(19, 19, 2, 0x90, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 2, 0x90, 0x10, 3, 1),
	PIN_FIELD_BASE(21, 21, 2, 0x90, 0x10, 6, 1),
	PIN_FIELD_BASE(22, 22, 2, 0x90, 0x10, 7, 1),
	PIN_FIELD_BASE(23, 23, 2, 0x90, 0x10, 10, 1),
	PIN_FIELD_BASE(24, 24, 2, 0x90, 0x10, 9, 1),
	PIN_FIELD_BASE(25, 25, 2, 0x90, 0x10, 8, 1),

	PIN_FIELD_BASE(26, 26, 5, 0x90, 0x10, 0, 1),
	PIN_FIELD_BASE(27, 27, 5, 0x90, 0x10, 4, 1),
	PIN_FIELD_BASE(28, 28, 5, 0x90, 0x10, 3, 1),
	PIN_FIELD_BASE(29, 29, 5, 0x90, 0x10, 1, 1),
	PIN_FIELD_BASE(30, 30, 5, 0x90, 0x10, 2, 1),
	PIN_FIELD_BASE(31, 31, 5, 0x90, 0x10, 5, 1),

	PIN_FIELD_BASE(32, 32, 1, 0x60, 0x10, 2, 1),
	PIN_FIELD_BASE(33, 33, 1, 0x60, 0x10, 3, 1),

	PIN_FIELD_BASE(34, 34, 4, 0x80, 0x10, 5, 1),
	PIN_FIELD_BASE(35, 35, 4, 0x80, 0x10, 7, 1),

	PIN_FIELD_BASE(36, 36, 3, 0x60, 0x10, 2, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x60, 0x10, 3, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x60, 0x10, 0, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x60, 0x10, 1, 1),

	PIN_FIELD_BASE(40, 40, 7, 0x70, 0x10, 1, 1),
	PIN_FIELD_BASE(41, 41, 7, 0x70, 0x10, 0, 1),
	PIN_FIELD_BASE(42, 42, 7, 0x70, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 7, 0x70, 0x10, 7, 1),
	PIN_FIELD_BASE(44, 44, 7, 0x30, 0x10, 8, 1),
	PIN_FIELD_BASE(45, 45, 7, 0x70, 0x10, 3, 1),
	PIN_FIELD_BASE(46, 46, 7, 0x70, 0x10, 4, 1),
	PIN_FIELD_BASE(47, 47, 7, 0x70, 0x10, 5, 1),
	PIN_FIELD_BASE(48, 48, 7, 0x70, 0x10, 6, 1),
	PIN_FIELD_BASE(49, 49, 7, 0x70, 0x10, 2, 1),

	PIN_FIELD_BASE(50, 50, 6, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(51, 51, 6, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(52, 52, 6, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(53, 53, 6, 0x50, 0x10, 4, 1),
	PIN_FIELD_BASE(54, 54, 6, 0x50, 0x10, 5, 1),
	PIN_FIELD_BASE(55, 55, 6, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(56, 56, 6, 0x50, 0x10, 1, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_pu_range[] = {
	PIN_FIELD_BASE(40, 40, 7, 0x50, 0x10, 1, 1),
	PIN_FIELD_BASE(41, 41, 7, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(42, 42, 7, 0x50, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 7, 0x50, 0x10, 7, 1),
	PIN_FIELD_BASE(44, 44, 7, 0x50, 0x10, 8, 1),
	PIN_FIELD_BASE(45, 45, 7, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(46, 46, 7, 0x50, 0x10, 4, 1),
	PIN_FIELD_BASE(47, 47, 7, 0x50, 0x10, 5, 1),
	PIN_FIELD_BASE(48, 48, 7, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(49, 49, 7, 0x50, 0x10, 2, 1),

	PIN_FIELD_BASE(50, 50, 6, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(51, 51, 6, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(52, 52, 6, 0x30, 0x10, 3, 1),
	PIN_FIELD_BASE(53, 53, 6, 0x30, 0x10, 4, 1),
	PIN_FIELD_BASE(54, 54, 6, 0x30, 0x10, 5, 1),
	PIN_FIELD_BASE(55, 55, 6, 0x30, 0x10, 6, 1),
	PIN_FIELD_BASE(56, 56, 6, 0x30, 0x10, 1, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_pd_range[] = {
	PIN_FIELD_BASE(40, 40, 7, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(41, 41, 7, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(42, 42, 7, 0x40, 0x10, 9, 1),
	PIN_FIELD_BASE(43, 43, 7, 0x40, 0x10, 7, 1),
	PIN_FIELD_BASE(44, 44, 7, 0x40, 0x10, 8, 1),
	PIN_FIELD_BASE(45, 45, 7, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(46, 46, 7, 0x40, 0x10, 4, 1),
	PIN_FIELD_BASE(47, 47, 7, 0x40, 0x10, 5, 1),
	PIN_FIELD_BASE(48, 48, 7, 0x40, 0x10, 6, 1),
	PIN_FIELD_BASE(49, 49, 7, 0x40, 0x10, 2, 1),

	PIN_FIELD_BASE(50, 50, 6, 0x20, 0x10, 0, 1),
	PIN_FIELD_BASE(51, 51, 6, 0x20, 0x10, 2, 1),
	PIN_FIELD_BASE(52, 52, 6, 0x20, 0x10, 3, 1),
	PIN_FIELD_BASE(53, 53, 6, 0x20, 0x10, 4, 1),
	PIN_FIELD_BASE(54, 54, 6, 0x20, 0x10, 5, 1),
	PIN_FIELD_BASE(55, 55, 6, 0x20, 0x10, 6, 1),
	PIN_FIELD_BASE(56, 56, 6, 0x20, 0x10, 1, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_drv_range[] = {
	PIN_FIELD_BASE(0, 0, 1, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(1, 1, 1, 0x00, 0x10, 0, 3),

	PIN_FIELD_BASE(2, 2, 5, 0x00, 0x10, 18, 3),

	PIN_FIELD_BASE(3, 3, 4, 0x00, 0x10, 18, 1),
	PIN_FIELD_BASE(4, 4, 4, 0x00, 0x10, 6, 1),
	PIN_FIELD_BASE(5, 5, 4, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(6, 6, 4, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(7, 7, 4, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(8, 8, 4, 0x00, 0x10, 12, 3),

	PIN_FIELD_BASE(9, 9, 5, 0x00, 0x10, 27, 3),
	PIN_FIELD_BASE(10, 10, 5, 0x00, 0x10, 24, 3),
	PIN_FIELD_BASE(11, 11, 5, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(12, 12, 5, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(13, 13, 5, 0x00, 0x10, 3, 3),

	PIN_FIELD_BASE(14, 14, 4, 0x00, 0x10, 27, 3),

	PIN_FIELD_BASE(15, 15, 2, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(16, 16, 2, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(17, 17, 2, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(18, 18, 2, 0x00, 0x10, 12, 3),
	PIN_FIELD_BASE(19, 19, 2, 0x00, 0x10, 6, 3),
	PIN_FIELD_BASE(20, 20, 2, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(21, 21, 2, 0x00, 0x10, 18, 3),
	PIN_FIELD_BASE(22, 22, 2, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(23, 23, 2, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(24, 24, 2, 0x00, 0x10, 27, 3),
	PIN_FIELD_BASE(25, 25, 2, 0x00, 0x10, 24, 3),

	PIN_FIELD_BASE(26, 26, 5, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(27, 27, 5, 0x00, 0x10, 12, 3),
	PIN_FIELD_BASE(28, 28, 5, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(29, 29, 5, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(30, 30, 5, 0x00, 0x10, 6, 3),
	PIN_FIELD_BASE(31, 31, 5, 0x00, 0x10, 15, 3),

	PIN_FIELD_BASE(32, 32, 1, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(33, 33, 1, 0x00, 0x10, 12, 3),

	PIN_FIELD_BASE(34, 34, 4, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(35, 35, 4, 0x00, 0x10, 21, 3),

	PIN_FIELD_BASE(36, 36, 3, 0x00, 0x10, 6, 3),
	PIN_FIELD_BASE(37, 37, 3, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(38, 38, 3, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(39, 39, 3, 0x00, 0x10, 3, 3),

	PIN_FIELD_BASE(40, 40, 7, 0x00, 0x10, 3, 3),
	PIN_FIELD_BASE(41, 41, 7, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(42, 42, 7, 0x00, 0x10, 27, 3),
	PIN_FIELD_BASE(43, 43, 7, 0x00, 0x10, 21, 3),
	PIN_FIELD_BASE(44, 44, 7, 0x00, 0x10, 24, 3),
	PIN_FIELD_BASE(45, 45, 7, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(46, 46, 7, 0x00, 0x10, 12, 3),
	PIN_FIELD_BASE(47, 47, 7, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(48, 48, 7, 0x00, 0x10, 18, 3),
	PIN_FIELD_BASE(49, 49, 7, 0x00, 0x10, 6, 3),

	PIN_FIELD_BASE(50, 50, 6, 0x00, 0x10, 0, 3),
	PIN_FIELD_BASE(51, 51, 6, 0x00, 0x10, 6, 3),
	PIN_FIELD_BASE(52, 52, 6, 0x00, 0x10, 9, 3),
	PIN_FIELD_BASE(53, 53, 6, 0x00, 0x10, 12, 3),
	PIN_FIELD_BASE(54, 54, 6, 0x00, 0x10, 15, 3),
	PIN_FIELD_BASE(55, 55, 6, 0x00, 0x10, 18, 3),
	PIN_FIELD_BASE(56, 56, 6, 0x00, 0x10, 3, 3),
};

static const struct mtk_pin_field_calc mt7981_pin_pupd_range[] = {
	PIN_FIELD_BASE(0, 0, 1, 0x20, 0x10, 1, 1),
	PIN_FIELD_BASE(1, 1, 1, 0x20, 0x10, 0, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x30, 0x10, 6, 1),
	PIN_FIELD_BASE(3, 3, 4, 0x30, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 4, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(5, 5, 4, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(6, 6, 4, 0x30, 0x10, 3, 1),
	PIN_FIELD_BASE(7, 7, 4, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x30, 0x10, 4, 1),

	PIN_FIELD_BASE(9, 9, 5, 0x30, 0x10, 9, 1),
	PIN_FIELD_BASE(10, 10, 5, 0x30, 0x10, 8, 1),
	PIN_FIELD_BASE(11, 11, 5, 0x30, 0x10, 10, 1),
	PIN_FIELD_BASE(12, 12, 5, 0x30, 0x10, 7, 1),
	PIN_FIELD_BASE(13, 13, 5, 0x30, 0x10, 11, 1),

	PIN_FIELD_BASE(14, 14, 4, 0x30, 0x10, 8, 1),

	PIN_FIELD_BASE(15, 15, 2, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(16, 16, 2, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(17, 17, 2, 0x30, 0x10, 5, 1),
	PIN_FIELD_BASE(18, 18, 2, 0x30, 0x10, 4, 1),
	PIN_FIELD_BASE(19, 19, 2, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 2, 0x90, 0x10, 3, 1),
	PIN_FIELD_BASE(21, 21, 2, 0x30, 0x10, 6, 1),
	PIN_FIELD_BASE(22, 22, 2, 0x30, 0x10, 7, 1),
	PIN_FIELD_BASE(23, 23, 2, 0x30, 0x10, 10, 1),
	PIN_FIELD_BASE(24, 24, 2, 0x30, 0x10, 9, 1),
	PIN_FIELD_BASE(25, 25, 2, 0x30, 0x10, 8, 1),

	PIN_FIELD_BASE(26, 26, 5, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(27, 27, 5, 0x30, 0x10, 4, 1),
	PIN_FIELD_BASE(28, 28, 5, 0x30, 0x10, 3, 1),
	PIN_FIELD_BASE(29, 29, 5, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(30, 30, 5, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(31, 31, 5, 0x30, 0x10, 5, 1),

	PIN_FIELD_BASE(32, 32, 1, 0x20, 0x10, 2, 1),
	PIN_FIELD_BASE(33, 33, 1, 0x20, 0x10, 3, 1),

	PIN_FIELD_BASE(34, 34, 4, 0x30, 0x10, 5, 1),
	PIN_FIELD_BASE(35, 35, 4, 0x30, 0x10, 7, 1),

	PIN_FIELD_BASE(36, 36, 3, 0x20, 0x10, 2, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x20, 0x10, 3, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x20, 0x10, 0, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x20, 0x10, 1, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_r0_range[] = {
	PIN_FIELD_BASE(0, 0, 1, 0x30, 0x10, 1, 1),
	PIN_FIELD_BASE(1, 1, 1, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x40, 0x10, 6, 1),
	PIN_FIELD_BASE(3, 3, 4, 0x40, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 4, 0x40, 0x10, 2, 1),
	PIN_FIELD_BASE(5, 5, 4, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(6, 6, 4, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(7, 7, 4, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x40, 0x10, 4, 1),

	PIN_FIELD_BASE(9, 9, 5, 0x40, 0x10, 9, 1),
	PIN_FIELD_BASE(10, 10, 5, 0x40, 0x10, 8, 1),
	PIN_FIELD_BASE(11, 11, 5, 0x40, 0x10, 10, 1),
	PIN_FIELD_BASE(12, 12, 5, 0x40, 0x10, 7, 1),
	PIN_FIELD_BASE(13, 13, 5, 0x40, 0x10, 11, 1),

	PIN_FIELD_BASE(14, 14, 4, 0x40, 0x10, 8, 1),

	PIN_FIELD_BASE(15, 15, 2, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(16, 16, 2, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(17, 17, 2, 0x40, 0x10, 5, 1),
	PIN_FIELD_BASE(18, 18, 2, 0x40, 0x10, 4, 1),
	PIN_FIELD_BASE(19, 19, 2, 0x40, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 2, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(21, 21, 2, 0x40, 0x10, 6, 1),
	PIN_FIELD_BASE(22, 22, 2, 0x40, 0x10, 7, 1),
	PIN_FIELD_BASE(23, 23, 2, 0x40, 0x10, 10, 1),
	PIN_FIELD_BASE(24, 24, 2, 0x40, 0x10, 9, 1),
	PIN_FIELD_BASE(25, 25, 2, 0x40, 0x10, 8, 1),

	PIN_FIELD_BASE(26, 26, 5, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(27, 27, 5, 0x40, 0x10, 4, 1),
	PIN_FIELD_BASE(28, 28, 5, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(29, 29, 5, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(30, 30, 5, 0x40, 0x10, 2, 1),
	PIN_FIELD_BASE(31, 31, 5, 0x40, 0x10, 5, 1),

	PIN_FIELD_BASE(32, 32, 1, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(33, 33, 1, 0x30, 0x10, 3, 1),

	PIN_FIELD_BASE(34, 34, 4, 0x40, 0x10, 5, 1),
	PIN_FIELD_BASE(35, 35, 4, 0x40, 0x10, 7, 1),

	PIN_FIELD_BASE(36, 36, 3, 0x30, 0x10, 2, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x30, 0x10, 3, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x30, 0x10, 0, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x30, 0x10, 1, 1),
};

static const struct mtk_pin_field_calc mt7981_pin_r1_range[] = {
	PIN_FIELD_BASE(0, 0, 1, 0x40, 0x10, 1, 1),
	PIN_FIELD_BASE(1, 1, 1, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(2, 2, 5, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(3, 3, 4, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(4, 4, 4, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(5, 5, 4, 0x50, 0x10, 1, 1),
	PIN_FIELD_BASE(6, 6, 4, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(7, 7, 4, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(8, 8, 4, 0x50, 0x10, 4, 1),

	PIN_FIELD_BASE(9, 9, 5, 0x50, 0x10, 9, 1),
	PIN_FIELD_BASE(10, 10, 5, 0x50, 0x10, 8, 1),
	PIN_FIELD_BASE(11, 11, 5, 0x50, 0x10, 10, 1),
	PIN_FIELD_BASE(12, 12, 5, 0x50, 0x10, 7, 1),
	PIN_FIELD_BASE(13, 13, 5, 0x50, 0x10, 11, 1),

	PIN_FIELD_BASE(14, 14, 4, 0x50, 0x10, 8, 1),

	PIN_FIELD_BASE(15, 15, 2, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(16, 16, 2, 0x50, 0x10, 1, 1),
	PIN_FIELD_BASE(17, 17, 2, 0x50, 0x10, 5, 1),
	PIN_FIELD_BASE(18, 18, 2, 0x50, 0x10, 4, 1),
	PIN_FIELD_BASE(19, 19, 2, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(20, 20, 2, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(21, 21, 2, 0x50, 0x10, 6, 1),
	PIN_FIELD_BASE(22, 22, 2, 0x50, 0x10, 7, 1),
	PIN_FIELD_BASE(23, 23, 2, 0x50, 0x10, 10, 1),
	PIN_FIELD_BASE(24, 24, 2, 0x50, 0x10, 9, 1),
	PIN_FIELD_BASE(25, 25, 2, 0x50, 0x10, 8, 1),

	PIN_FIELD_BASE(26, 26, 5, 0x50, 0x10, 0, 1),
	PIN_FIELD_BASE(27, 27, 5, 0x50, 0x10, 4, 1),
	PIN_FIELD_BASE(28, 28, 5, 0x50, 0x10, 3, 1),
	PIN_FIELD_BASE(29, 29, 5, 0x50, 0x10, 1, 1),
	PIN_FIELD_BASE(30, 30, 5, 0x50, 0x10, 2, 1),
	PIN_FIELD_BASE(31, 31, 5, 0x50, 0x10, 5, 1),

	PIN_FIELD_BASE(32, 32, 1, 0x40, 0x10, 2, 1),
	PIN_FIELD_BASE(33, 33, 1, 0x40, 0x10, 3, 1),

	PIN_FIELD_BASE(34, 34, 4, 0x50, 0x10, 5, 1),
	PIN_FIELD_BASE(35, 35, 4, 0x50, 0x10, 7, 1),

	PIN_FIELD_BASE(36, 36, 3, 0x40, 0x10, 2, 1),
	PIN_FIELD_BASE(37, 37, 3, 0x40, 0x10, 3, 1),
	PIN_FIELD_BASE(38, 38, 3, 0x40, 0x10, 0, 1),
	PIN_FIELD_BASE(39, 39, 3, 0x40, 0x10, 1, 1),
};

static const unsigned int mt7981_pull_type[] = {
	MTK_PULL_PUPD_R1R0_TYPE,/*0*/ MTK_PULL_PUPD_R1R0_TYPE,/*1*/
	MTK_PULL_PUPD_R1R0_TYPE,/*2*/ MTK_PULL_PUPD_R1R0_TYPE,/*3*/
	MTK_PULL_PUPD_R1R0_TYPE,/*4*/ MTK_PULL_PUPD_R1R0_TYPE,/*5*/
	MTK_PULL_PUPD_R1R0_TYPE,/*6*/ MTK_PULL_PUPD_R1R0_TYPE,/*7*/
	MTK_PULL_PUPD_R1R0_TYPE,/*8*/ MTK_PULL_PUPD_R1R0_TYPE,/*9*/
	MTK_PULL_PUPD_R1R0_TYPE,/*10*/ MTK_PULL_PUPD_R1R0_TYPE,/*11*/
	MTK_PULL_PUPD_R1R0_TYPE,/*12*/ MTK_PULL_PUPD_R1R0_TYPE,/*13*/
	MTK_PULL_PUPD_R1R0_TYPE,/*14*/ MTK_PULL_PUPD_R1R0_TYPE,/*15*/
	MTK_PULL_PUPD_R1R0_TYPE,/*16*/ MTK_PULL_PUPD_R1R0_TYPE,/*17*/
	MTK_PULL_PUPD_R1R0_TYPE,/*18*/ MTK_PULL_PUPD_R1R0_TYPE,/*19*/
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
	MTK_PULL_PU_PD_TYPE,/*40*/ MTK_PULL_PU_PD_TYPE,/*41*/
	MTK_PULL_PU_PD_TYPE,/*42*/ MTK_PULL_PU_PD_TYPE,/*43*/
	MTK_PULL_PU_PD_TYPE,/*44*/ MTK_PULL_PU_PD_TYPE,/*45*/
	MTK_PULL_PU_PD_TYPE,/*46*/ MTK_PULL_PU_PD_TYPE,/*47*/
	MTK_PULL_PU_PD_TYPE,/*48*/ MTK_PULL_PU_PD_TYPE,/*49*/
	MTK_PULL_PU_PD_TYPE,/*50*/ MTK_PULL_PU_PD_TYPE,/*51*/
	MTK_PULL_PU_PD_TYPE,/*52*/ MTK_PULL_PU_PD_TYPE,/*53*/
	MTK_PULL_PU_PD_TYPE,/*54*/ MTK_PULL_PU_PD_TYPE,/*55*/
	MTK_PULL_PU_PD_TYPE,/*56*/
};

static const struct mtk_pin_reg_calc mt7981_reg_cals[] = {
	[PINCTRL_PIN_REG_MODE] = MTK_RANGE(mt7981_pin_mode_range),
	[PINCTRL_PIN_REG_DIR] = MTK_RANGE(mt7981_pin_dir_range),
	[PINCTRL_PIN_REG_DI] = MTK_RANGE(mt7981_pin_di_range),
	[PINCTRL_PIN_REG_DO] = MTK_RANGE(mt7981_pin_do_range),
	[PINCTRL_PIN_REG_SMT] = MTK_RANGE(mt7981_pin_smt_range),
	[PINCTRL_PIN_REG_IES] = MTK_RANGE(mt7981_pin_ies_range),
	[PINCTRL_PIN_REG_PU] = MTK_RANGE(mt7981_pin_pu_range),
	[PINCTRL_PIN_REG_PD] = MTK_RANGE(mt7981_pin_pd_range),
	[PINCTRL_PIN_REG_DRV] = MTK_RANGE(mt7981_pin_drv_range),
	[PINCTRL_PIN_REG_PUPD] = MTK_RANGE(mt7981_pin_pupd_range),
	[PINCTRL_PIN_REG_R0] = MTK_RANGE(mt7981_pin_r0_range),
	[PINCTRL_PIN_REG_R1] = MTK_RANGE(mt7981_pin_r1_range),
};

static const struct mtk_pin_desc mt7981_pins[] = {
	MT7981_PIN(0, "GPIO_WPS"),
	MT7981_PIN(1, "GPIO_RESET"),
	MT7981_PIN(2, "SYS_WATCHDOG"),
	MT7981_PIN(3, "PCIE_PERESET_N"),
	MT7981_PIN(4, "JTAG_JTDO"),
	MT7981_PIN(5, "JTAG_JTDI"),
	MT7981_PIN(6, "JTAG_JTMS"),
	MT7981_PIN(7, "JTAG_JTCLK"),
	MT7981_PIN(8, "JTAG_JTRST_N"),
	MT7981_PIN(9, "WO_JTAG_JTDO"),
	MT7981_PIN(10, "WO_JTAG_JTDI"),
	MT7981_PIN(11, "WO_JTAG_JTMS"),
	MT7981_PIN(12, "WO_JTAG_JTCLK"),
	MT7981_PIN(13, "WO_JTAG_JTRST_N"),
	MT7981_PIN(14, "USB_VBUS"),
	MT7981_PIN(15, "PWM0"),
	MT7981_PIN(16, "SPI0_CLK"),
	MT7981_PIN(17, "SPI0_MOSI"),
	MT7981_PIN(18, "SPI0_MISO"),
	MT7981_PIN(19, "SPI0_CS"),
	MT7981_PIN(20, "SPI0_HOLD"),
	MT7981_PIN(21, "SPI0_WP"),
	MT7981_PIN(22, "SPI1_CLK"),
	MT7981_PIN(23, "SPI1_MOSI"),
	MT7981_PIN(24, "SPI1_MISO"),
	MT7981_PIN(25, "SPI1_CS"),
	MT7981_PIN(26, "SPI2_CLK"),
	MT7981_PIN(27, "SPI2_MOSI"),
	MT7981_PIN(28, "SPI2_MISO"),
	MT7981_PIN(29, "SPI2_CS"),
	MT7981_PIN(30, "SPI2_HOLD"),
	MT7981_PIN(31, "SPI2_WP"),
	MT7981_PIN(32, "UART0_RXD"),
	MT7981_PIN(33, "UART0_TXD"),
	MT7981_PIN(34, "PCIE_CLK_REQ"),
	MT7981_PIN(35, "PCIE_WAKE_N"),
	MT7981_PIN(36, "SMI_MDC"),
	MT7981_PIN(37, "SMI_MDIO"),
	MT7981_PIN(38, "GBE_INT"),
	MT7981_PIN(39, "GBE_RESET"),
	MT7981_PIN(40, "WF_DIG_RESETB"),
	MT7981_PIN(41, "WF_CBA_RESETB"),
	MT7981_PIN(42, "WF_XO_REQ"),
	MT7981_PIN(43, "WF_TOP_CLK"),
	MT7981_PIN(44, "WF_TOP_DATA"),
	MT7981_PIN(45, "WF_HB1"),
	MT7981_PIN(46, "WF_HB2"),
	MT7981_PIN(47, "WF_HB3"),
	MT7981_PIN(48, "WF_HB4"),
	MT7981_PIN(49, "WF_HB0"),
	MT7981_PIN(50, "WF_HB0_B"),
	MT7981_PIN(51, "WF_HB5"),
	MT7981_PIN(52, "WF_HB6"),
	MT7981_PIN(53, "WF_HB7"),
	MT7981_PIN(54, "WF_HB8"),
	MT7981_PIN(55, "WF_HB9"),
	MT7981_PIN(56, "WF_HB10"),
};

/* List all groups consisting of these pins dedicated to the enablement of
 * certain hardware block and the corresponding mode for all of the pins.
 * The hardware probably has multiple combinations of these pinouts.
 */

/* WA_AICE */
static int mt7981_wa_aice1_pins[] = { 0, 1, };
static int mt7981_wa_aice1_funcs[] = { 2, 2, };

static int mt7981_wa_aice2_pins[] = { 0, 1, };
static int mt7981_wa_aice2_funcs[] = { 3, 3, };

static int mt7981_wa_aice3_pins[] = { 28, 29, };
static int mt7981_wa_aice3_funcs[] = { 3, 3, };

static int mt7981_wm_aice1_pins[] = { 9, 10, };
static int mt7981_wm_aice1_funcs[] = { 2, 2, };

static int mt7981_wm_aice2_pins[] = { 30, 31, };
static int mt7981_wm_aice2_funcs[] = { 5, 5, };

/* WM_UART */
static int mt7981_wm_uart_0_pins[] = { 0, 1, };
static int mt7981_wm_uart_0_funcs[] = { 5, 5, };

static int mt7981_wm_uart_1_pins[] = { 20, 21, };
static int mt7981_wm_uart_1_funcs[] = { 4, 4, };

static int mt7981_wm_uart_2_pins[] = { 30, 31, };
static int mt7981_wm_uart_2_funcs[] = { 3, 3, };

/* DFD */
static int mt7981_dfd_pins[] = { 0, 1, 4, 5, };
static int mt7981_dfd_funcs[] = { 5, 5, 6, 6, };

/* SYS_WATCHDOG */
static int mt7981_watchdog_pins[] = { 2, };
static int mt7981_watchdog_funcs[] = { 1, };

static int mt7981_watchdog1_pins[] = { 13, };
static int mt7981_watchdog1_funcs[] = { 5, };

/* PCIE_PERESET_N */
static int mt7981_pcie_pereset_pins[] = { 3, };
static int mt7981_pcie_pereset_funcs[] = { 1, };

/* JTAG */
static int mt7981_jtag_pins[] = { 4, 5, 6, 7, 8, };
static int mt7981_jtag_funcs[] = { 1, 1, 1, 1, 1, };

/* WM_JTAG */
static int mt7981_wm_jtag_0_pins[] = { 4, 5, 6, 7, 8, };
static int mt7981_wm_jtag_0_funcs[] = { 2, 2, 2, 2, 2, };

static int mt7981_wm_jtag_1_pins[] = { 20, 21, 22, 23, 24, };
static int mt7981_wm_jtag_1_funcs[] = { 5, 5, 5, 5, 5, };

/* WO0_JTAG */
static int mt7981_wo0_jtag_0_pins[] = { 9, 10, 11, 12, 13, };
static int mt7981_wo0_jtag_0_funcs[] = { 1, 1, 1, 1, 1, };

static int mt7981_wo0_jtag_1_pins[] = { 25, 26, 27, 28, 29, };
static int mt7981_wo0_jtag_1_funcs[] = { 5, 5, 5, 5, 5, };

/* UART2 */
static int mt7981_uart2_0_pins[] = { 4, 5, 6, 7, };
static int mt7981_uart2_0_funcs[] = { 3, 3, 3, 3, };

/* GBE_LED0 */
static int mt7981_gbe_led0_pins[] = { 8, };
static int mt7981_gbe_led0_funcs[] = { 3, };

/* PTA_EXT */
static int mt7981_pta_ext_0_pins[] = { 4, 5, 6, };
static int mt7981_pta_ext_0_funcs[] = { 4, 4, 4, };

static int mt7981_pta_ext_1_pins[] = { 22, 23, 24, };
static int mt7981_pta_ext_1_funcs[] = { 4, 4, 4, };

/* PWM2 */
static int mt7981_pwm2_pins[] = { 7, };
static int mt7981_pwm2_funcs[] = { 4, };

/* NET_WO0_UART_TXD */
static int mt7981_net_wo0_uart_txd_0_pins[] = { 8, };
static int mt7981_net_wo0_uart_txd_0_funcs[] = { 4, };

static int mt7981_net_wo0_uart_txd_1_pins[] = { 14, };
static int mt7981_net_wo0_uart_txd_1_funcs[] = { 3, };

static int mt7981_net_wo0_uart_txd_2_pins[] = { 15, };
static int mt7981_net_wo0_uart_txd_2_funcs[] = { 4, };

/* SPI1 */
static int mt7981_spi1_0_pins[] = { 4, 5, 6, 7, };
static int mt7981_spi1_0_funcs[] = { 5, 5, 5, 5, };

/* I2C */
static int mt7981_i2c0_0_pins[] = { 6, 7, };
static int mt7981_i2c0_0_funcs[] = { 6, 6, };

static int mt7981_i2c0_1_pins[] = { 30, 31, };
static int mt7981_i2c0_1_funcs[] = { 4, 4, };

static int mt7981_i2c0_2_pins[] = { 36, 37, };
static int mt7981_i2c0_2_funcs[] = { 2, 2, };

static int mt7981_u2_phy_i2c_pins[] = { 30, 31, };
static int mt7981_u2_phy_i2c_funcs[] = { 6, 6, };

static int mt7981_u3_phy_i2c_pins[] = { 32, 33, };
static int mt7981_u3_phy_i2c_funcs[] = { 3, 3, };

static int mt7981_sgmii1_phy_i2c_pins[] = { 32, 33, };
static int mt7981_sgmii1_phy_i2c_funcs[] = { 2, 2, };

static int mt7981_sgmii0_phy_i2c_pins[] = { 32, 33, };
static int mt7981_sgmii0_phy_i2c_funcs[] = { 5, 5, };

/* DFD_NTRST */
static int mt7981_dfd_ntrst_pins[] = { 8, };
static int mt7981_dfd_ntrst_funcs[] = { 6, };

/* PWM0 */
static int mt7981_pwm0_0_pins[] = { 13, };
static int mt7981_pwm0_0_funcs[] = { 2, };

static int mt7981_pwm0_1_pins[] = { 15, };
static int mt7981_pwm0_1_funcs[] = { 1, };

/* PWM1 */
static int mt7981_pwm1_0_pins[] = { 14, };
static int mt7981_pwm1_0_funcs[] = { 2, };

static int mt7981_pwm1_1_pins[] = { 15, };
static int mt7981_pwm1_1_funcs[] = { 3, };

/* GBE_LED1 */
static int mt7981_gbe_led1_pins[] = { 13, };
static int mt7981_gbe_led1_funcs[] = { 3, };

/* PCM */
static int mt7981_pcm_pins[] = { 9, 10, 11, 12, 13, 25 };
static int mt7981_pcm_funcs[] = { 4, 4, 4, 4, 4, 4, };

/* UDI */
static int mt7981_udi_pins[] = { 9, 10, 11, 12, 13, };
static int mt7981_udi_funcs[] = { 6, 6, 6, 6, 6, };

/* DRV_VBUS */
static int mt7981_drv_vbus_pins[] = { 14, };
static int mt7981_drv_vbus_funcs[] = { 1, };

/* EMMC */
static int mt7981_emmc_45_pins[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, };
static int mt7981_emmc_45_funcs[] = { 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, };

/* SNFI */
static int mt7981_snfi_pins[] = { 16, 17, 18, 19, 20, 21, };
static int mt7981_snfi_funcs[] = { 3, 3, 3, 3, 3, 3, };

/* SPI0 */
static int mt7981_spi0_pins[] = { 16, 17, 18, 19, };
static int mt7981_spi0_funcs[] = { 1, 1, 1, 1, };

/* SPI0 */
static int mt7981_spi0_wp_hold_pins[] = { 20, 21, };
static int mt7981_spi0_wp_hold_funcs[] = { 1, 1, };

/* SPI1 */
static int mt7981_spi1_1_pins[] = { 22, 23, 24, 25, };
static int mt7981_spi1_1_funcs[] = { 1, 1, 1, 1, };

/* SPI2 */
static int mt7981_spi2_pins[] = { 26, 27, 28, 29, };
static int mt7981_spi2_funcs[] = { 1, 1, 1, 1, };

/* SPI2 */
static int mt7981_spi2_wp_hold_pins[] = { 30, 31, };
static int mt7981_spi2_wp_hold_funcs[] = { 1, 1, };

/* UART1 */
static int mt7981_uart1_0_pins[] = { 16, 17, 18, 19, };
static int mt7981_uart1_0_funcs[] = { 4, 4, 4, 4, };

static int mt7981_uart1_1_pins[] = { 26, 27, 28, 29, };
static int mt7981_uart1_1_funcs[] = { 2, 2, 2, 2, };

/* UART2 */
static int mt7981_uart2_1_pins[] = { 22, 23, 24, 25, };
static int mt7981_uart2_1_funcs[] = { 3, 3, 3, 3, };

/* UART0 */
static int mt7981_uart0_pins[] = { 32, 33, };
static int mt7981_uart0_funcs[] = { 1, 1, };

/* PCIE_CLK_REQ */
static int mt7981_pcie_clk_pins[] = { 34, };
static int mt7981_pcie_clk_funcs[] = { 2, };

/* PCIE_WAKE_N */
static int mt7981_pcie_wake_pins[] = { 35, };
static int mt7981_pcie_wake_funcs[] = { 2, };

/* MDC_MDIO */
static int mt7981_smi_mdc_mdio_pins[] = { 36, 37, };
static int mt7981_smi_mdc_mdio_funcs[] = { 1, 1, };

static int mt7981_gbe_ext_mdc_mdio_pins[] = { 36, 37, };
static int mt7981_gbe_ext_mdc_mdio_funcs[] = { 3, 3, };

/* WF0_MODE1 */
static int mt7981_wf0_mode1_pins[] = { 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56 };
static int mt7981_wf0_mode1_funcs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

/* WF0_MODE3 */
static int mt7981_wf0_mode3_pins[] = { 45, 46, 47, 48, 49, 51 };
static int mt7981_wf0_mode3_funcs[] = { 2, 2, 2, 2, 2, 2 };

/* WF2G_LED */
static int mt7981_wf2g_led0_pins[] = { 30, };
static int mt7981_wf2g_led0_funcs[] = { 2, };

static int mt7981_wf2g_led1_pins[] = { 34, };
static int mt7981_wf2g_led1_funcs[] = { 1, };

/* WF5G_LED */
static int mt7981_wf5g_led0_pins[] = { 31, };
static int mt7981_wf5g_led0_funcs[] = { 2, };

static int mt7981_wf5g_led1_pins[] = { 35, };
static int mt7981_wf5g_led1_funcs[] = { 1, };

/* MT7531_INT */
static int mt7981_mt7531_int_pins[] = { 38, };
static int mt7981_mt7531_int_funcs[] = { 1, };

/* ANT_SEL */
static int mt7981_ant_sel_pins[] = { 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 34, 35 };
static int mt7981_ant_sel_funcs[] = { 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 };

static const struct group_desc mt7981_groups[] = {
	/* @GPIO(0,1): WA_AICE(2) */
	PINCTRL_PIN_GROUP("wa_aice1", mt7981_wa_aice1),
	/* @GPIO(0,1): WA_AICE(3) */
	PINCTRL_PIN_GROUP("wa_aice2", mt7981_wa_aice2),
	/* @GPIO(0,1): WM_UART(5) */
	PINCTRL_PIN_GROUP("wm_uart_0", mt7981_wm_uart_0),
	/* @GPIO(0,1,4,5): DFD(6) */
	PINCTRL_PIN_GROUP("dfd", mt7981_dfd),
	/* @GPIO(2): SYS_WATCHDOG(1) */
	PINCTRL_PIN_GROUP("watchdog", mt7981_watchdog),
	/* @GPIO(3): PCIE_PERESET_N(1) */
	PINCTRL_PIN_GROUP("pcie_pereset", mt7981_pcie_pereset),
	/* @GPIO(4,8) JTAG(1) */
	PINCTRL_PIN_GROUP("jtag", mt7981_jtag),
	/* @GPIO(4,8) WM_JTAG(2) */
	PINCTRL_PIN_GROUP("wm_jtag_0", mt7981_wm_jtag_0),
	/* @GPIO(9,13) WO0_JTAG(1) */
	PINCTRL_PIN_GROUP("wo0_jtag_0", mt7981_wo0_jtag_0),
	/* @GPIO(4,7) WM_JTAG(3) */
	PINCTRL_PIN_GROUP("uart2_0", mt7981_uart2_0),
	/* @GPIO(8) GBE_LED0(3) */
	PINCTRL_PIN_GROUP("gbe_led0", mt7981_gbe_led0),
	/* @GPIO(4,6) PTA_EXT(4) */
	PINCTRL_PIN_GROUP("pta_ext_0", mt7981_pta_ext_0),
	/* @GPIO(7) PWM2(4) */
	PINCTRL_PIN_GROUP("pwm2", mt7981_pwm2),
	/* @GPIO(8) NET_WO0_UART_TXD(4) */
	PINCTRL_PIN_GROUP("net_wo0_uart_txd_0", mt7981_net_wo0_uart_txd_0),
	/* @GPIO(4,7) SPI1(5) */
	PINCTRL_PIN_GROUP("spi1_0", mt7981_spi1_0),
	/* @GPIO(6,7) I2C(5) */
	PINCTRL_PIN_GROUP("i2c0_0", mt7981_i2c0_0),
	/* @GPIO(0,1,4,5): DFD_NTRST(6) */
	PINCTRL_PIN_GROUP("dfd_ntrst", mt7981_dfd_ntrst),
	/* @GPIO(9,10): WM_AICE(2) */
	PINCTRL_PIN_GROUP("wm_aice1", mt7981_wm_aice1),
	/* @GPIO(13): PWM0(2) */
	PINCTRL_PIN_GROUP("pwm0_0", mt7981_pwm0_0),
	/* @GPIO(15): PWM0(1) */
	PINCTRL_PIN_GROUP("pwm0_1", mt7981_pwm0_1),
	/* @GPIO(14): PWM1(2) */
	PINCTRL_PIN_GROUP("pwm1_0", mt7981_pwm1_0),
	/* @GPIO(15): PWM1(3) */
	PINCTRL_PIN_GROUP("pwm1_1", mt7981_pwm1_1),
	/* @GPIO(14) NET_WO0_UART_TXD(3) */
	PINCTRL_PIN_GROUP("net_wo0_uart_txd_1", mt7981_net_wo0_uart_txd_1),
	/* @GPIO(15) NET_WO0_UART_TXD(4) */
	PINCTRL_PIN_GROUP("net_wo0_uart_txd_2", mt7981_net_wo0_uart_txd_2),
	/* @GPIO(13) GBE_LED0(3) */
	PINCTRL_PIN_GROUP("gbe_led1", mt7981_gbe_led1),
	/* @GPIO(9,13) PCM(4) */
	PINCTRL_PIN_GROUP("pcm", mt7981_pcm),
	/* @GPIO(13): SYS_WATCHDOG1(5) */
	PINCTRL_PIN_GROUP("watchdog1", mt7981_watchdog1),
	/* @GPIO(9,13) UDI(4) */
	PINCTRL_PIN_GROUP("udi", mt7981_udi),
	/* @GPIO(14) DRV_VBUS(1) */
	PINCTRL_PIN_GROUP("drv_vbus", mt7981_drv_vbus),
	/* @GPIO(15,25): EMMC(2) */
	PINCTRL_PIN_GROUP("emmc_45", mt7981_emmc_45),
	/* @GPIO(16,21): SNFI(3) */
	PINCTRL_PIN_GROUP("snfi", mt7981_snfi),
	/* @GPIO(16,19): SPI0(1) */
	PINCTRL_PIN_GROUP("spi0", mt7981_spi0),
	/* @GPIO(20,21): SPI0(1) */
	PINCTRL_PIN_GROUP("spi0_wp_hold", mt7981_spi0_wp_hold),
	/* @GPIO(22,25) SPI1(1) */
	PINCTRL_PIN_GROUP("spi1_1", mt7981_spi1_1),
	/* @GPIO(26,29): SPI2(1) */
	PINCTRL_PIN_GROUP("spi2", mt7981_spi2),
	/* @GPIO(30,31): SPI0(1) */
	PINCTRL_PIN_GROUP("spi2_wp_hold", mt7981_spi2_wp_hold),
	/* @GPIO(16,19): UART1(4) */
	PINCTRL_PIN_GROUP("uart1_0", mt7981_uart1_0),
	/* @GPIO(26,29): UART1(2) */
	PINCTRL_PIN_GROUP("uart1_1", mt7981_uart1_1),
	/* @GPIO(22,25): UART1(3) */
	PINCTRL_PIN_GROUP("uart2_1", mt7981_uart2_1),
	/* @GPIO(22,24) PTA_EXT(4) */
	PINCTRL_PIN_GROUP("pta_ext_1", mt7981_pta_ext_1),
	/* @GPIO(20,21): WM_UART(4) */
	PINCTRL_PIN_GROUP("wm_aurt_1", mt7981_wm_uart_1),
	/* @GPIO(30,31): WM_UART(3) */
	PINCTRL_PIN_GROUP("wm_aurt_2", mt7981_wm_uart_2),
	/* @GPIO(20,24) WM_JTAG(5) */
	PINCTRL_PIN_GROUP("wm_jtag_1", mt7981_wm_jtag_1),
	/* @GPIO(25,29) WO0_JTAG(5) */
	PINCTRL_PIN_GROUP("wo0_jtag_1", mt7981_wo0_jtag_1),
	/* @GPIO(28,29): WA_AICE(3) */
	PINCTRL_PIN_GROUP("wa_aice3", mt7981_wa_aice3),
	/* @GPIO(30,31): WM_AICE(5) */
	PINCTRL_PIN_GROUP("wm_aice2", mt7981_wm_aice2),
	/* @GPIO(30,31): I2C(4) */
	PINCTRL_PIN_GROUP("i2c0_1", mt7981_i2c0_1),
	/* @GPIO(30,31): I2C(6) */
	PINCTRL_PIN_GROUP("u2_phy_i2c", mt7981_u2_phy_i2c),
	/* @GPIO(32,33): I2C(1) */
	PINCTRL_PIN_GROUP("uart0", mt7981_uart0),
	/* @GPIO(32,33): I2C(2) */
	PINCTRL_PIN_GROUP("sgmii1_phy_i2c", mt7981_sgmii1_phy_i2c),
	/* @GPIO(32,33): I2C(3) */
	PINCTRL_PIN_GROUP("u3_phy_i2c", mt7981_u3_phy_i2c),
	/* @GPIO(32,33): I2C(5) */
	PINCTRL_PIN_GROUP("sgmii0_phy_i2c", mt7981_sgmii0_phy_i2c),
	/* @GPIO(34): PCIE_CLK_REQ(2) */
	PINCTRL_PIN_GROUP("pcie_clk", mt7981_pcie_clk),
	/* @GPIO(35): PCIE_WAKE_N(2) */
	PINCTRL_PIN_GROUP("pcie_wake", mt7981_pcie_wake),
	/* @GPIO(36,37): I2C(2) */
	PINCTRL_PIN_GROUP("i2c0_2", mt7981_i2c0_2),
	/* @GPIO(36,37): MDC_MDIO(1) */
	PINCTRL_PIN_GROUP("smi_mdc_mdio", mt7981_smi_mdc_mdio),
	/* @GPIO(36,37): MDC_MDIO(3) */
	PINCTRL_PIN_GROUP("gbe_ext_mdc_mdio", mt7981_gbe_ext_mdc_mdio),
	/* @GPIO(69,85): WF0_MODE1(1) */
	PINCTRL_PIN_GROUP("wf0_mode1", mt7981_wf0_mode1),
	/* @GPIO(74,80): WF0_MODE3(3) */
	PINCTRL_PIN_GROUP("wf0_mode3", mt7981_wf0_mode3),
	/* @GPIO(30): WF2G_LED(2) */
	PINCTRL_PIN_GROUP("wf2g_led0", mt7981_wf2g_led0),
	/* @GPIO(34): WF2G_LED(1) */
	PINCTRL_PIN_GROUP("wf2g_led1", mt7981_wf2g_led1),
	/* @GPIO(31): WF5G_LED(2) */
	PINCTRL_PIN_GROUP("wf5g_led0", mt7981_wf5g_led0),
	/* @GPIO(35): WF5G_LED(1) */
	PINCTRL_PIN_GROUP("wf5g_led1", mt7981_wf5g_led1),
	/* @GPIO(38): MT7531_INT(1) */
	PINCTRL_PIN_GROUP("mt7531_int", mt7981_mt7531_int),
	/* @GPIO(14,15,26,17,18,19,20,21,22,23,24,25,34,35): ANT_SEL(1) */
	PINCTRL_PIN_GROUP("ant_sel", mt7981_ant_sel),
};

/* Joint those groups owning the same capability in user point of view which
 * allows that people tend to use through the device tree.
 */
static const char *mt7981_wa_aice_groups[] = { "wa_aice1", "wa_aice2", "wm_aice1_1",
	"wa_aice3", "wm_aice1_2", };
static const char *mt7981_uart_groups[] = { "wm_uart_0", "uart2_0",
	"net_wo0_uart_txd_0", "net_wo0_uart_txd_1", "net_wo0_uart_txd_2",
	"uart1_0", "uart1_1", "uart2_1", "wm_aurt_1", "wm_aurt_2", "uart0", };
static const char *mt7981_dfd_groups[] = { "dfd", "dfd_ntrst", };
static const char *mt7981_wdt_groups[] = { "watchdog", "watchdog1", };
static const char *mt7981_pcie_groups[] = { "pcie_pereset", "pcie_clk", "pcie_wake", };
static const char *mt7981_jtag_groups[] = { "jtag", "wm_jtag_0", "wo0_jtag_0",
	"wo0_jtag_1", "wm_jtag_1", };
static const char *mt7981_led_groups[] = { "gbe_led0", "gbe_led1", "wf2g_led0",
	"wf2g_led1", "wf5g_led0", "wf5g_led1", };
static const char *mt7981_pta_groups[] = { "pta_ext_0", "pta_ext_1", };
static const char *mt7981_pwm_groups[] = { "pwm2", "pwm0_0", "pwm0_1",
	"pwm1_0", "pwm1_1", };
static const char *mt7981_spi_groups[] = { "spi1_0", "spi0", "spi0_wp_hold", "spi1_1", "spi2",
	"spi2_wp_hold", };
static const char *mt7981_i2c_groups[] = { "i2c0_0", "i2c0_1", "u2_phy_i2c",
	"sgmii1_phy_i2c", "u3_phy_i2c", "sgmii0_phy_i2c", "i2c0_2", };
static const char *mt7981_pcm_groups[] = { "pcm", };
static const char *mt7981_udi_groups[] = { "udi", };
static const char *mt7981_usb_groups[] = { "drv_vbus", };
static const char *mt7981_flash_groups[] = { "emmc_45", "snfi", };
static const char *mt7981_ethernet_groups[] = { "smi_mdc_mdio", "gbe_ext_mdc_mdio",
	"wf0_mode1", "wf0_mode3", "mt7531_int", };
static const char *mt7981_ant_groups[] = { "ant_sel", };

static const struct function_desc mt7981_functions[] = {
	{"wa_aice",	mt7981_wa_aice_groups, ARRAY_SIZE(mt7981_wa_aice_groups)},
	{"dfd",	mt7981_dfd_groups, ARRAY_SIZE(mt7981_dfd_groups)},
	{"jtag", mt7981_jtag_groups, ARRAY_SIZE(mt7981_jtag_groups)},
	{"pta", mt7981_pta_groups, ARRAY_SIZE(mt7981_pta_groups)},
	{"pcm", mt7981_pcm_groups, ARRAY_SIZE(mt7981_pcm_groups)},
	{"udi", mt7981_udi_groups, ARRAY_SIZE(mt7981_udi_groups)},
	{"usb", mt7981_usb_groups, ARRAY_SIZE(mt7981_usb_groups)},
	{"ant", mt7981_ant_groups, ARRAY_SIZE(mt7981_ant_groups)},
	{"eth",	mt7981_ethernet_groups, ARRAY_SIZE(mt7981_ethernet_groups)},
	{"i2c", mt7981_i2c_groups, ARRAY_SIZE(mt7981_i2c_groups)},
	{"led",	mt7981_led_groups, ARRAY_SIZE(mt7981_led_groups)},
	{"pwm",	mt7981_pwm_groups, ARRAY_SIZE(mt7981_pwm_groups)},
	{"spi",	mt7981_spi_groups, ARRAY_SIZE(mt7981_spi_groups)},
	{"uart", mt7981_uart_groups, ARRAY_SIZE(mt7981_uart_groups)},
	{"watchdog", mt7981_wdt_groups, ARRAY_SIZE(mt7981_wdt_groups)},
	{"flash", mt7981_flash_groups, ARRAY_SIZE(mt7981_flash_groups)},
	{"pcie", mt7981_pcie_groups, ARRAY_SIZE(mt7981_pcie_groups)},
};

static const struct mtk_eint_hw mt7981_eint_hw = {
	.port_mask = 7,
	.ports     = 7,
	.ap_num    = ARRAY_SIZE(mt7981_pins),
	.db_cnt    = 16,
};

static const char * const mt7981_pinctrl_register_base_names[] = {
	"gpio", "iocfg_rt", "iocfg_rm", "iocfg_rb",
	"iocfg_lb", "iocfg_bl", "iocfg_tm", "iocfg_tl",
};

static struct mtk_pin_soc mt7981_data = {
	.reg_cal = mt7981_reg_cals,
	.pins = mt7981_pins,
	.npins = ARRAY_SIZE(mt7981_pins),
	.grps = mt7981_groups,
	.ngrps = ARRAY_SIZE(mt7981_groups),
	.funcs = mt7981_functions,
	.nfuncs = ARRAY_SIZE(mt7981_functions),
	.eint_hw = &mt7981_eint_hw,
	.gpio_m = 0,
	.ies_present = false,
	.base_names = mt7981_pinctrl_register_base_names,
	.nbase_names = ARRAY_SIZE(mt7981_pinctrl_register_base_names),
	.bias_disable_set = mtk_pinconf_bias_disable_set,
	.bias_disable_get = mtk_pinconf_bias_disable_get,
	.bias_set = mtk_pinconf_bias_set,
	.bias_get = mtk_pinconf_bias_get,
	.pull_type = mt7981_pull_type,
	.bias_set_combo = mtk_pinconf_bias_set_combo,
	.bias_get_combo = mtk_pinconf_bias_get_combo,
	.drive_set = mtk_pinconf_drive_set_rev1,
	.drive_get = mtk_pinconf_drive_get_rev1,
	.adv_pull_get = mtk_pinconf_adv_pull_get,
	.adv_pull_set = mtk_pinconf_adv_pull_set,
};

static const struct of_device_id mt7981_pinctrl_of_match[] = {
	{ .compatible = "mediatek,mt7981-pinctrl", },
	{}
};

static int mt7981_pinctrl_probe(struct platform_device *pdev)
{
	return mtk_moore_pinctrl_probe(pdev, &mt7981_data);
}

static struct platform_driver mt7981_pinctrl_driver = {
	.driver = {
		.name = "mt7981-pinctrl",
		.of_match_table = mt7981_pinctrl_of_match,
	},
	.probe = mt7981_pinctrl_probe,
};

static int __init mt7981_pinctrl_init(void)
{
	return platform_driver_register(&mt7981_pinctrl_driver);
}
arch_initcall(mt7981_pinctrl_init);
