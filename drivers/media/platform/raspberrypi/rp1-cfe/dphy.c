// SPDX-License-Identifier: GPL-2.0-only
/*
 * RP1 CSI-2 Driver
 *
 * Copyright (c) 2021-2024 Raspberry Pi Ltd.
 * Copyright (c) 2023-2024 Ideas on Board Oy
 */

#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include "dphy.h"

#define dphy_dbg(dphy, fmt, arg...) dev_dbg((dphy)->dev, fmt, ##arg)
#define dphy_err(dphy, fmt, arg...) dev_err((dphy)->dev, fmt, ##arg)

/* DW dphy Host registers */
#define DPHY_VERSION		0x000
#define DPHY_N_LANES		0x004
#define DPHY_RESETN		0x008
#define DPHY_PHY_SHUTDOWNZ	0x040
#define DPHY_PHY_RSTZ		0x044
#define DPHY_PHY_RX		0x048
#define	DPHY_PHY_STOPSTATE	0x04c
#define DPHY_PHY_TST_CTRL0	0x050
#define DPHY_PHY_TST_CTRL1	0x054
#define DPHY_PHY2_TST_CTRL0	0x058
#define DPHY_PHY2_TST_CTRL1	0x05c

/* DW dphy Host Transactions */
#define DPHY_HS_RX_CTRL_LANE0_OFFSET	0x44
#define DPHY_PLL_INPUT_DIV_OFFSET	0x17
#define DPHY_PLL_LOOP_DIV_OFFSET	0x18
#define DPHY_PLL_DIV_CTRL_OFFSET	0x19

static u32 dw_csi2_host_read(struct dphy_data *dphy, u32 offset)
{
	return readl(dphy->base + offset);
}

static void dw_csi2_host_write(struct dphy_data *dphy, u32 offset, u32 data)
{
	writel(data, dphy->base + offset);
}

static void set_tstclr(struct dphy_data *dphy, u32 val)
{
	u32 ctrl0 = dw_csi2_host_read(dphy, DPHY_PHY_TST_CTRL0);

	dw_csi2_host_write(dphy, DPHY_PHY_TST_CTRL0, (ctrl0 & ~1) | val);
}

static void set_tstclk(struct dphy_data *dphy, u32 val)
{
	u32 ctrl0 = dw_csi2_host_read(dphy, DPHY_PHY_TST_CTRL0);

	dw_csi2_host_write(dphy, DPHY_PHY_TST_CTRL0, (ctrl0 & ~2) | (val << 1));
}

static uint8_t get_tstdout(struct dphy_data *dphy)
{
	u32 ctrl1 = dw_csi2_host_read(dphy, DPHY_PHY_TST_CTRL1);

	return ((ctrl1 >> 8) & 0xff);
}

static void set_testen(struct dphy_data *dphy, u32 val)
{
	u32 ctrl1 = dw_csi2_host_read(dphy, DPHY_PHY_TST_CTRL1);

	dw_csi2_host_write(dphy, DPHY_PHY_TST_CTRL1,
			   (ctrl1 & ~(1 << 16)) | (val << 16));
}

static void set_testdin(struct dphy_data *dphy, u32 val)
{
	u32 ctrl1 = dw_csi2_host_read(dphy, DPHY_PHY_TST_CTRL1);

	dw_csi2_host_write(dphy, DPHY_PHY_TST_CTRL1, (ctrl1 & ~0xff) | val);
}

static uint8_t dphy_transaction(struct dphy_data *dphy, u8 test_code,
				uint8_t test_data)
{
	/* See page 101 of the MIPI DPHY databook. */
	set_tstclk(dphy, 1);
	set_testen(dphy, 0);
	set_testdin(dphy, test_code);
	set_testen(dphy, 1);
	set_tstclk(dphy, 0);
	set_testen(dphy, 0);
	set_testdin(dphy, test_data);
	set_tstclk(dphy, 1);
	return get_tstdout(dphy);
}

static void dphy_set_hsfreqrange(struct dphy_data *dphy, uint32_t mbps)
{
	/* See Table 5-1 on page 65 of dphy databook */
	static const u16 hsfreqrange_table[][2] = {
		{ 89, 0b000000 },   { 99, 0b010000 },	{ 109, 0b100000 },
		{ 129, 0b000001 },  { 139, 0b010001 },	{ 149, 0b100001 },
		{ 169, 0b000010 },  { 179, 0b010010 },	{ 199, 0b100010 },
		{ 219, 0b000011 },  { 239, 0b010011 },	{ 249, 0b100011 },
		{ 269, 0b000100 },  { 299, 0b010100 },	{ 329, 0b000101 },
		{ 359, 0b010101 },  { 399, 0b100101 },	{ 449, 0b000110 },
		{ 499, 0b010110 },  { 549, 0b000111 },	{ 599, 0b010111 },
		{ 649, 0b001000 },  { 699, 0b011000 },	{ 749, 0b001001 },
		{ 799, 0b011001 },  { 849, 0b101001 },	{ 899, 0b111001 },
		{ 949, 0b001010 },  { 999, 0b011010 },	{ 1049, 0b101010 },
		{ 1099, 0b111010 }, { 1149, 0b001011 }, { 1199, 0b011011 },
		{ 1249, 0b101011 }, { 1299, 0b111011 }, { 1349, 0b001100 },
		{ 1399, 0b011100 }, { 1449, 0b101100 }, { 1500, 0b111100 },
	};
	unsigned int i;

	if (mbps < 80 || mbps > 1500)
		dphy_err(dphy, "DPHY: Datarate %u Mbps out of range\n", mbps);

	for (i = 0; i < ARRAY_SIZE(hsfreqrange_table) - 1; i++) {
		if (mbps <= hsfreqrange_table[i][0])
			break;
	}

	dphy_transaction(dphy, DPHY_HS_RX_CTRL_LANE0_OFFSET,
			 hsfreqrange_table[i][1] << 1);
}

static void dphy_init(struct dphy_data *dphy)
{
	dw_csi2_host_write(dphy, DPHY_PHY_RSTZ, 0);
	dw_csi2_host_write(dphy, DPHY_PHY_SHUTDOWNZ, 0);
	set_tstclk(dphy, 1);
	set_testen(dphy, 0);
	set_tstclr(dphy, 1);
	usleep_range(15, 20);
	set_tstclr(dphy, 0);
	usleep_range(15, 20);

	dphy_set_hsfreqrange(dphy, dphy->dphy_rate);

	usleep_range(5, 10);
	dw_csi2_host_write(dphy, DPHY_PHY_SHUTDOWNZ, 1);
	usleep_range(5, 10);
	dw_csi2_host_write(dphy, DPHY_PHY_RSTZ, 1);
}

void dphy_start(struct dphy_data *dphy)
{
	dphy_dbg(dphy, "%s: Link rate %u Mbps, %u data lanes\n", __func__,
		 dphy->dphy_rate, dphy->active_lanes);

	dw_csi2_host_write(dphy, DPHY_N_LANES, (dphy->active_lanes - 1));
	dphy_init(dphy);
	dw_csi2_host_write(dphy, DPHY_RESETN, 0xffffffff);
	usleep_range(10, 50);
}

void dphy_stop(struct dphy_data *dphy)
{
	dphy_dbg(dphy, "%s\n", __func__);

	/* Set only one lane (lane 0) as active (ON) */
	dw_csi2_host_write(dphy, DPHY_N_LANES, 0);
	dw_csi2_host_write(dphy, DPHY_RESETN, 0);
}

void dphy_probe(struct dphy_data *dphy)
{
	u32 host_ver;
	u8 host_ver_major, host_ver_minor;

	host_ver = dw_csi2_host_read(dphy, DPHY_VERSION);
	host_ver_major = (u8)((host_ver >> 24) - '0');
	host_ver_minor = (u8)((host_ver >> 16) - '0');
	host_ver_minor = host_ver_minor * 10;
	host_ver_minor += (u8)((host_ver >> 8) - '0');

	dphy_dbg(dphy, "DW dphy Host HW v%u.%u\n", host_ver_major,
		 host_ver_minor);
}
