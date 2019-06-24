/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Maxim MAX2165 silicon tuner
 *
 *  Copyright (c) 2009 David T. L. Wong <davidtlwong@gmail.com>
 */

#ifndef __MAX2165_PRIV_H__
#define __MAX2165_PRIV_H__

#define REG_NDIV_INT 0x00
#define REG_NDIV_FRAC2 0x01
#define REG_NDIV_FRAC1 0x02
#define REG_NDIV_FRAC0 0x03
#define REG_TRACK_FILTER 0x04
#define REG_LNA 0x05
#define REG_PLL_CFG 0x06
#define REG_TEST 0x07
#define REG_SHUTDOWN 0x08
#define REG_VCO_CTRL 0x09
#define REG_BASEBAND_CTRL 0x0A
#define REG_DC_OFFSET_CTRL 0x0B
#define REG_DC_OFFSET_DAC 0x0C
#define REG_ROM_TABLE_ADDR 0x0D

/* Read Only Registers */
#define REG_ROM_TABLE_DATA 0x10
#define REG_STATUS 0x11
#define REG_AUTOTUNE 0x12

struct max2165_priv {
	struct max2165_config *config;
	struct i2c_adapter *i2c;

	u32 frequency;
	u32 bandwidth;

	u8 tf_ntch_low_cfg;
	u8 tf_ntch_hi_cfg;
	u8 tf_balun_low_ref;
	u8 tf_balun_hi_ref;
	u8 bb_filter_7mhz_cfg;
	u8 bb_filter_8mhz_cfg;
};

#endif
