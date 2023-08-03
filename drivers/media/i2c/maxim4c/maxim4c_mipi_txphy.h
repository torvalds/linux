/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM4C_MIPI_TXPHY_H__
#define __MAXIM4C_MIPI_TXPHY_H__

/* MIPI TXPHY ID: 0 ~ 3 */
enum {
	MAXIM4C_TXPHY_ID_A = 0,
	MAXIM4C_TXPHY_ID_B,
	MAXIM4C_TXPHY_ID_C,
	MAXIM4C_TXPHY_ID_D,
	MAXIM4C_TXPHY_ID_MAX,
};

/* MIPI TXPHY Bit Mask: bit0 ~ bit3 */
#define MAXIM4C_TXPHY_MASK_A		BIT(MAXIM4C_TXPHY_ID_A)
#define MAXIM4C_TXPHY_MASK_B		BIT(MAXIM4C_TXPHY_ID_B)
#define MAXIM4C_TXPHY_MASK_C		BIT(MAXIM4C_TXPHY_ID_C)
#define MAXIM4C_TXPHY_MASK_D		BIT(MAXIM4C_TXPHY_ID_D)

#define MAXIM4C_TXPHY_MASK_ALL		GENMASK(MAXIM4C_TXPHY_ID_D, MAXIM4C_TXPHY_ID_A)

/* MIPI TXPHY Type */
enum {
	MAXIM4C_TXPHY_TYPE_DPHY = 0,
	MAXIM4C_TXPHY_TYPE_CPHY,
};

/* MIPI TXPHY Mode */
enum {
	MAXIM4C_TXPHY_MODE_2X4LANES = 0, /* PortA: 1x4Lanes, PortB: 1x4Lanes */
	MAXIM4C_TXPHY_MODE_4X2LANES, /* PortA: 2x2Lanes, PortB: 2x2Lanes */
	MAXIM4C_TXPHY_MODE_1X4LANES_2X2LANES, /* PortA: 1x4Lanes, PortB: 2x2Lanes */
	MAXIM4C_TXPHY_MODE_2X2LANES_1X4LANES, /* PortA: 2x2Lanes, PortB: 1x4Lanes */
};

/* MIPI TXPHY DPLL */
enum {
	MAXIM4C_TXPHY_DPLL_PREDEF = 0,
	MAXIM4C_TXPHY_DPLL_FINE_TUNING,
};

struct maxim4c_txphy_cfg {
	u8 phy_enable;
	u8 phy_type;
	u8 auto_deskew;
	u8 data_lane_num;
	u8 data_lane_map;
	u8 vc_ext_en;
	u8 clock_master;
	u8 clock_mode;
};

typedef struct maxim4c_mipi_txphy {
	u8 phy_mode; /* mipi txphy mode */
	u8 force_clock_out_en; /* Force all MIPI clocks running */
	u8 force_clk0_en; /* DPHY0 enabled as clock */
	u8 force_clk3_en; /* DPHY3 enabled as clock */

	struct maxim4c_txphy_cfg phy_cfg[MAXIM4C_TXPHY_ID_MAX];
} maxim4c_mipi_txphy_t;

#endif /* __MAXIM4C_MIPI_TXPHY_H__ */
