/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 */

#ifndef __MT7601U_PHY_INITVALS_H
#define __MT7601U_PHY_INITVALS_H

#define RF_REG_PAIR(bank, reg, value)				\
	{ MT_MCU_MEMMAP_RF | (bank) << 16 | (reg), value }

static const struct mt76_reg_pair rf_central[] = {
	/* Bank 0 - for central blocks: BG, PLL, XTAL, LO, ADC/DAC */
	RF_REG_PAIR(0,	 0, 0x02),
	RF_REG_PAIR(0,	 1, 0x01),
	RF_REG_PAIR(0,	 2, 0x11),
	RF_REG_PAIR(0,	 3, 0xff),
	RF_REG_PAIR(0,	 4, 0x0a),
	RF_REG_PAIR(0,	 5, 0x20),
	RF_REG_PAIR(0,	 6, 0x00),
	/* B/G */
	RF_REG_PAIR(0,	 7, 0x00),
	RF_REG_PAIR(0,	 8, 0x00),
	RF_REG_PAIR(0,	 9, 0x00),
	RF_REG_PAIR(0,	10, 0x00),
	RF_REG_PAIR(0,	11, 0x21),
	/* XO */
	RF_REG_PAIR(0,	13, 0x00),		/* 40mhz xtal */
	/* RF_REG_PAIR(0,	13, 0x13), */	/* 20mhz xtal */
	RF_REG_PAIR(0,	14, 0x7c),
	RF_REG_PAIR(0,	15, 0x22),
	RF_REG_PAIR(0,	16, 0x80),
	/* PLL */
	RF_REG_PAIR(0,	17, 0x99),
	RF_REG_PAIR(0,	18, 0x99),
	RF_REG_PAIR(0,	19, 0x09),
	RF_REG_PAIR(0,	20, 0x50),
	RF_REG_PAIR(0,	21, 0xb0),
	RF_REG_PAIR(0,	22, 0x00),
	RF_REG_PAIR(0,	23, 0xc5),
	RF_REG_PAIR(0,	24, 0xfc),
	RF_REG_PAIR(0,	25, 0x40),
	RF_REG_PAIR(0,	26, 0x4d),
	RF_REG_PAIR(0,	27, 0x02),
	RF_REG_PAIR(0,	28, 0x72),
	RF_REG_PAIR(0,	29, 0x01),
	RF_REG_PAIR(0,	30, 0x00),
	RF_REG_PAIR(0,	31, 0x00),
	/* test ports */
	RF_REG_PAIR(0,	32, 0x00),
	RF_REG_PAIR(0,	33, 0x00),
	RF_REG_PAIR(0,	34, 0x23),
	RF_REG_PAIR(0,	35, 0x01), /* change setting to reduce spurs */
	RF_REG_PAIR(0,	36, 0x00),
	RF_REG_PAIR(0,	37, 0x00),
	/* ADC/DAC */
	RF_REG_PAIR(0,	38, 0x00),
	RF_REG_PAIR(0,	39, 0x20),
	RF_REG_PAIR(0,	40, 0x00),
	RF_REG_PAIR(0,	41, 0xd0),
	RF_REG_PAIR(0,	42, 0x1b),
	RF_REG_PAIR(0,	43, 0x02),
	RF_REG_PAIR(0,	44, 0x00),
};

static const struct mt76_reg_pair rf_channel[] = {
	RF_REG_PAIR(4,	 0, 0x01),
	RF_REG_PAIR(4,	 1, 0x00),
	RF_REG_PAIR(4,	 2, 0x00),
	RF_REG_PAIR(4,	 3, 0x00),
	/* LDO */
	RF_REG_PAIR(4,	 4, 0x00),
	RF_REG_PAIR(4,	 5, 0x08),
	RF_REG_PAIR(4,	 6, 0x00),
	/* RX */
	RF_REG_PAIR(4,	 7, 0x5b),
	RF_REG_PAIR(4,	 8, 0x52),
	RF_REG_PAIR(4,	 9, 0xb6),
	RF_REG_PAIR(4,	10, 0x57),
	RF_REG_PAIR(4,	11, 0x33),
	RF_REG_PAIR(4,	12, 0x22),
	RF_REG_PAIR(4,	13, 0x3d),
	RF_REG_PAIR(4,	14, 0x3e),
	RF_REG_PAIR(4,	15, 0x13),
	RF_REG_PAIR(4,	16, 0x22),
	RF_REG_PAIR(4,	17, 0x23),
	RF_REG_PAIR(4,	18, 0x02),
	RF_REG_PAIR(4,	19, 0xa4),
	RF_REG_PAIR(4,	20, 0x01),
	RF_REG_PAIR(4,	21, 0x12),
	RF_REG_PAIR(4,	22, 0x80),
	RF_REG_PAIR(4,	23, 0xb3),
	RF_REG_PAIR(4,	24, 0x00), /* reserved */
	RF_REG_PAIR(4,	25, 0x00), /* reserved */
	RF_REG_PAIR(4,	26, 0x00), /* reserved */
	RF_REG_PAIR(4,	27, 0x00), /* reserved */
	/* LOGEN */
	RF_REG_PAIR(4,	28, 0x18),
	RF_REG_PAIR(4,	29, 0xee),
	RF_REG_PAIR(4,	30, 0x6b),
	RF_REG_PAIR(4,	31, 0x31),
	RF_REG_PAIR(4,	32, 0x5d),
	RF_REG_PAIR(4,	33, 0x00), /* reserved */
	/* TX */
	RF_REG_PAIR(4,	34, 0x96),
	RF_REG_PAIR(4,	35, 0x55),
	RF_REG_PAIR(4,	36, 0x08),
	RF_REG_PAIR(4,	37, 0xbb),
	RF_REG_PAIR(4,	38, 0xb3),
	RF_REG_PAIR(4,	39, 0xb3),
	RF_REG_PAIR(4,	40, 0x03),
	RF_REG_PAIR(4,	41, 0x00), /* reserved */
	RF_REG_PAIR(4,	42, 0x00), /* reserved */
	RF_REG_PAIR(4,	43, 0xc5),
	RF_REG_PAIR(4,	44, 0xc5),
	RF_REG_PAIR(4,	45, 0xc5),
	RF_REG_PAIR(4,	46, 0x07),
	RF_REG_PAIR(4,	47, 0xa8),
	RF_REG_PAIR(4,	48, 0xef),
	RF_REG_PAIR(4,	49, 0x1a),
	/* PA */
	RF_REG_PAIR(4,	54, 0x07),
	RF_REG_PAIR(4,	55, 0xa7),
	RF_REG_PAIR(4,	56, 0xcc),
	RF_REG_PAIR(4,	57, 0x14),
	RF_REG_PAIR(4,	58, 0x07),
	RF_REG_PAIR(4,	59, 0xa8),
	RF_REG_PAIR(4,	60, 0xd7),
	RF_REG_PAIR(4,	61, 0x10),
	RF_REG_PAIR(4,	62, 0x1c),
	RF_REG_PAIR(4,	63, 0x00), /* reserved */
};

static const struct mt76_reg_pair rf_vga[] = {
	RF_REG_PAIR(5,	 0, 0x47),
	RF_REG_PAIR(5,	 1, 0x00),
	RF_REG_PAIR(5,	 2, 0x00),
	RF_REG_PAIR(5,	 3, 0x08),
	RF_REG_PAIR(5,	 4, 0x04),
	RF_REG_PAIR(5,	 5, 0x20),
	RF_REG_PAIR(5,	 6, 0x3a),
	RF_REG_PAIR(5,	 7, 0x3a),
	RF_REG_PAIR(5,	 8, 0x00),
	RF_REG_PAIR(5,	 9, 0x00),
	RF_REG_PAIR(5,	10, 0x10),
	RF_REG_PAIR(5,	11, 0x10),
	RF_REG_PAIR(5,	12, 0x10),
	RF_REG_PAIR(5,	13, 0x10),
	RF_REG_PAIR(5,	14, 0x10),
	RF_REG_PAIR(5,	15, 0x20),
	RF_REG_PAIR(5,	16, 0x22),
	RF_REG_PAIR(5,	17, 0x7c),
	RF_REG_PAIR(5,	18, 0x00),
	RF_REG_PAIR(5,	19, 0x00),
	RF_REG_PAIR(5,	20, 0x00),
	RF_REG_PAIR(5,	21, 0xf1),
	RF_REG_PAIR(5,	22, 0x11),
	RF_REG_PAIR(5,	23, 0x02),
	RF_REG_PAIR(5,	24, 0x41),
	RF_REG_PAIR(5,	25, 0x20),
	RF_REG_PAIR(5,	26, 0x00),
	RF_REG_PAIR(5,	27, 0xd7),
	RF_REG_PAIR(5,	28, 0xa2),
	RF_REG_PAIR(5,	29, 0x20),
	RF_REG_PAIR(5,	30, 0x49),
	RF_REG_PAIR(5,	31, 0x20),
	RF_REG_PAIR(5,	32, 0x04),
	RF_REG_PAIR(5,	33, 0xf1),
	RF_REG_PAIR(5,	34, 0xa1),
	RF_REG_PAIR(5,	35, 0x01),
	RF_REG_PAIR(5,	41, 0x00),
	RF_REG_PAIR(5,	42, 0x00),
	RF_REG_PAIR(5,	43, 0x00),
	RF_REG_PAIR(5,	44, 0x00),
	RF_REG_PAIR(5,	45, 0x00),
	RF_REG_PAIR(5,	46, 0x00),
	RF_REG_PAIR(5,	47, 0x00),
	RF_REG_PAIR(5,	48, 0x00),
	RF_REG_PAIR(5,	49, 0x00),
	RF_REG_PAIR(5,	50, 0x00),
	RF_REG_PAIR(5,	51, 0x00),
	RF_REG_PAIR(5,	52, 0x00),
	RF_REG_PAIR(5,	53, 0x00),
	RF_REG_PAIR(5,	54, 0x00),
	RF_REG_PAIR(5,	55, 0x00),
	RF_REG_PAIR(5,	56, 0x00),
	RF_REG_PAIR(5,	57, 0x00),
	RF_REG_PAIR(5,	58, 0x31),
	RF_REG_PAIR(5,	59, 0x31),
	RF_REG_PAIR(5,	60, 0x0a),
	RF_REG_PAIR(5,	61, 0x02),
	RF_REG_PAIR(5,	62, 0x00),
	RF_REG_PAIR(5,	63, 0x00),
};

/* TODO: BBP178 is set to 0xff for "CCK CH14 OBW" which overrides the settings
 *	 from channel switching. Seems stupid at best.
 */
static const struct mt76_reg_pair bbp_high_temp[] = {
	{  75, 0x60 },
	{  92, 0x02 },
	{ 178, 0xff }, /* For CCK CH14 OBW */
	{ 195, 0x88 }, { 196, 0x60 },
}, bbp_high_temp_bw20[] = {
	{  69, 0x12 },
	{  91, 0x07 },
	{ 195, 0x23 }, { 196, 0x17 },
	{ 195, 0x24 }, { 196, 0x06 },
	{ 195, 0x81 }, { 196, 0x12 },
	{ 195, 0x83 }, { 196, 0x17 },
}, bbp_high_temp_bw40[] = {
	{  69, 0x15 },
	{  91, 0x04 },
	{ 195, 0x23 }, { 196, 0x12 },
	{ 195, 0x24 }, { 196, 0x08 },
	{ 195, 0x81 }, { 196, 0x15 },
	{ 195, 0x83 }, { 196, 0x16 },
}, bbp_low_temp[] = {
	{ 178, 0xff }, /* For CCK CH14 OBW */
}, bbp_low_temp_bw20[] = {
	{  69, 0x12 },
	{  75, 0x5e },
	{  91, 0x07 },
	{  92, 0x02 },
	{ 195, 0x23 }, { 196, 0x17 },
	{ 195, 0x24 }, { 196, 0x06 },
	{ 195, 0x81 }, { 196, 0x12 },
	{ 195, 0x83 }, { 196, 0x17 },
	{ 195, 0x88 }, { 196, 0x5e },
}, bbp_low_temp_bw40[] = {
	{  69, 0x15 },
	{  75, 0x5c },
	{  91, 0x04 },
	{  92, 0x03 },
	{ 195, 0x23 }, { 196, 0x10 },
	{ 195, 0x24 }, { 196, 0x08 },
	{ 195, 0x81 }, { 196, 0x15 },
	{ 195, 0x83 }, { 196, 0x16 },
	{ 195, 0x88 }, { 196, 0x5b },
}, bbp_normal_temp[] = {
	{  75, 0x60 },
	{  92, 0x02 },
	{ 178, 0xff }, /* For CCK CH14 OBW */
	{ 195, 0x88 }, { 196, 0x60 },
}, bbp_normal_temp_bw20[] = {
	{  69, 0x12 },
	{  91, 0x07 },
	{ 195, 0x23 }, { 196, 0x17 },
	{ 195, 0x24 }, { 196, 0x06 },
	{ 195, 0x81 }, { 196, 0x12 },
	{ 195, 0x83 }, { 196, 0x17 },
}, bbp_normal_temp_bw40[] = {
	{  69, 0x15 },
	{  91, 0x04 },
	{ 195, 0x23 }, { 196, 0x12 },
	{ 195, 0x24 }, { 196, 0x08 },
	{ 195, 0x81 }, { 196, 0x15 },
	{ 195, 0x83 }, { 196, 0x16 },
};

#define BBP_TABLE(arr) { arr, ARRAY_SIZE(arr), }

static const struct reg_table {
	const struct mt76_reg_pair *regs;
	size_t n;
} bbp_mode_table[3][3] = {
	{
		BBP_TABLE(bbp_normal_temp_bw20),
		BBP_TABLE(bbp_normal_temp_bw40),
		BBP_TABLE(bbp_normal_temp),
	}, {
		BBP_TABLE(bbp_high_temp_bw20),
		BBP_TABLE(bbp_high_temp_bw40),
		BBP_TABLE(bbp_high_temp),
	}, {
		BBP_TABLE(bbp_low_temp_bw20),
		BBP_TABLE(bbp_low_temp_bw40),
		BBP_TABLE(bbp_low_temp),
	}
};

#endif
