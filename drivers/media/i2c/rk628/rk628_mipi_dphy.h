/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#ifndef _RK628_MIPI_DPHY_H
#define _RK628_MIPI_DPHY_H

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

#include "rk628_csi.h"
#include "rk628_dsi.h"
#include "rk628.h"

/* Test Code: 0x44 (HS RX Control of Lane 0) */
#define HSFREQRANGE(x)			UPDATE(x, 6, 1)

static inline void testif_testclk_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, PHY_TESTCLK);
	udelay(1);
}

static inline void testif_testclk_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLK, 0);
	udelay(1);
}

static inline void testif_testclr_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, PHY_TESTCLR);
	udelay(1);
}

static inline void testif_testclr_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTCLR, 0);
	udelay(1);
}

static inline void testif_testen_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTEN, PHY_TESTEN);
	udelay(1);
}

static inline void testif_testen_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTEN, 0);
	udelay(1);
}

static inline void testif_set_data(struct rk628 *rk628, u8 data)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
			   PHY_TESTDIN_MASK, PHY_TESTDIN(data));
	udelay(1);
}

static inline u8 testif_get_data(struct rk628 *rk628)
{
	u32 data = 0;

	rk628_i2c_read(rk628, GRF_DPHY0_STATUS, &data);

	return data >> PHY_TESTDOUT_SHIFT;
}

static void testif_test_code_write(struct rk628 *rk628, u8 test_code)
{
	testif_testclk_assert(rk628);
	testif_set_data(rk628, test_code);
	testif_testen_assert(rk628);
	testif_testclk_deassert(rk628);
	testif_testen_deassert(rk628);
}

static void testif_test_data_write(struct rk628 *rk628, u8 test_data)
{
	testif_testclk_deassert(rk628);
	testif_set_data(rk628, test_data);
	testif_testclk_assert(rk628);
}

static u8 testif_write(struct rk628 *rk628, u8 test_code, u8 test_data)
{
	u8 monitor_data;

	testif_test_code_write(rk628, test_code);
	testif_test_data_write(rk628, test_data);
	monitor_data = testif_get_data(rk628);

	dev_dbg(rk628->dev, "test_code=0x%02x, ", test_code);
	dev_dbg(rk628->dev, "test_data=0x%02x, ", test_data);
	dev_dbg(rk628->dev, "monitor_data=0x%02x\n", monitor_data);

	return monitor_data;
}

static inline u8 testif_read(struct rk628 *rk628, u8 test_code)
{
	u8 test_data;

	testif_test_code_write(rk628, test_code);
	test_data = testif_get_data(rk628);
	testif_test_data_write(rk628, test_data);

	return test_data;
}

static inline void mipi_dphy_enableclk_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, CSITX_DPHY_CTRL, DPHY_ENABLECLK,
			DPHY_ENABLECLK);
	udelay(1);
}

static inline void mipi_dphy_enableclk_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, CSITX_DPHY_CTRL, DPHY_ENABLECLK, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON, CSI_PHYSHUTDOWNZ, 0);
	udelay(1);
}

static inline void mipi_dphy_shutdownz_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON, CSI_PHYSHUTDOWNZ,
			CSI_PHYSHUTDOWNZ);
	udelay(1);
}

static inline void mipi_dphy_rstz_assert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON, CSI_PHYRSTZ, 0);
	udelay(1);
}

static inline void mipi_dphy_rstz_deassert(struct rk628 *rk628)
{
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON, CSI_PHYRSTZ,
			CSI_PHYRSTZ);
	udelay(1);
}

static inline void mipi_dphy_init_hsfreqrange(struct rk628 *rk628, int lane_mbps)
{
	const struct {
		unsigned long max_lane_mbps;
		u8 hsfreqrange;
	} hsfreqrange_table[] = {
		{  90, 0x00}, { 100, 0x10}, { 110, 0x20}, { 130, 0x01},
		{ 140, 0x11}, { 150, 0x21}, { 170, 0x02}, { 180, 0x12},
		{ 200, 0x22}, { 220, 0x03}, { 240, 0x13}, { 250, 0x23},
		{ 270, 0x04}, { 300, 0x14}, { 330, 0x05}, { 360, 0x15},
		{ 400, 0x25}, { 450, 0x06}, { 500, 0x16}, { 550, 0x07},
		{ 600, 0x17}, { 650, 0x08}, { 700, 0x18}, { 750, 0x09},
		{ 800, 0x19}, { 850, 0x29}, { 900, 0x39}, { 950, 0x0a},
		{1000, 0x1a}, {1050, 0x2a}, {1100, 0x3a}, {1150, 0x0b},
		{1200, 0x1b}, {1250, 0x2b}, {1300, 0x3b}, {1350, 0x0c},
		{1400, 0x1c}, {1450, 0x2c}, {1500, 0x3c}
	};
	u8 hsfreqrange;
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(hsfreqrange_table); index++)
		if (lane_mbps <= hsfreqrange_table[index].max_lane_mbps)
			break;

	if (index == ARRAY_SIZE(hsfreqrange_table))
		--index;

	hsfreqrange = hsfreqrange_table[index].hsfreqrange;
	testif_write(rk628, 0x44, HSFREQRANGE(hsfreqrange));
}

static inline int mipi_dphy_reset(struct rk628 *rk628)
{
	u32 val, mask;

	mipi_dphy_enableclk_deassert(rk628);
	mipi_dphy_shutdownz_assert(rk628);
	mipi_dphy_rstz_assert(rk628);
	testif_testclr_assert(rk628);

	/* Set all REQUEST inputs to zero */
	rk628_i2c_update_bits(rk628, GRF_MIPI_TX0_CON,
		     FORCETXSTOPMODE_MASK | FORCERXMODE_MASK,
		     FORCETXSTOPMODE(0) | FORCERXMODE(0));
	udelay(1);
	testif_testclr_deassert(rk628);
	mipi_dphy_enableclk_assert(rk628);
	mipi_dphy_shutdownz_deassert(rk628);
	mipi_dphy_rstz_deassert(rk628);
	usleep_range(1500, 2000);

	mask = STOPSTATE_CLK | STOPSTATE_LANE0;
	rk628_i2c_read(rk628, CSITX_CSITX_STATUS1, &val);
	if ((val & mask) != mask) {
		dev_err(rk628->dev, "lane module is not in stop state\n");
		return -1;
	}

	return 0;
}

#endif
