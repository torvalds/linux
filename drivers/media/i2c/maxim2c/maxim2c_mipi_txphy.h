/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM2C_MIPI_TXPHY_H__
#define __MAXIM2C_MIPI_TXPHY_H__

/* MIPI TXPHY ID: 0 ~ 3 */
enum {
	MAXIM2C_TXPHY_ID_A = 0,
	MAXIM2C_TXPHY_ID_B,
	MAXIM2C_TXPHY_ID_C,
	MAXIM2C_TXPHY_ID_D,
	MAXIM2C_TXPHY_ID_MAX,
};

/* MIPI TXPHY Bit Mask: bit0 ~ bit3 */
#define MAXIM2C_TXPHY_MASK_A		BIT(MAXIM2C_TXPHY_ID_A)
#define MAXIM2C_TXPHY_MASK_B		BIT(MAXIM2C_TXPHY_ID_B)
#define MAXIM2C_TXPHY_MASK_C		BIT(MAXIM2C_TXPHY_ID_C)
#define MAXIM2C_TXPHY_MASK_D		BIT(MAXIM2C_TXPHY_ID_D)

#define MAXIM2C_TXPHY_MASK_ALL		GENMASK(MAXIM2C_TXPHY_ID_D, MAXIM2C_TXPHY_ID_A)

/* MIPI TXPHY Type */
enum {
	MAXIM2C_TXPHY_TYPE_DPHY = 0,
	MAXIM2C_TXPHY_TYPE_CPHY,
};

/* MIPI TXPHY Mode */
enum {
	MAXIM2C_TXPHY_MODE_2X4LANES = 0, /* PortA: 1x4Lanes, PortB: 1x4Lanes */
	MAXIM2C_TXPHY_MODE_2X2LANES, /* PortA: 2Lanes, PortB: 2Lanes */
};

/* MIPI TXPHY DPLL */
enum {
	MAXIM2C_TXPHY_DPLL_PREDEF = 0,
	MAXIM2C_TXPHY_DPLL_FINE_TUNING,
};

struct maxim2c_txphy_cfg {
	u8 phy_enable;
	u8 phy_type;
	u8 auto_deskew;
	u8 data_lane_num;
	u8 data_lane_map;
	u8 vc_ext_en;
	u8 tunnel_enable;
	u8 tunnel_vs_wait;
	u8 tunnel_dest;
	u8 clock_master;
	u8 clock_mode;
};

typedef struct maxim2c_mipi_txphy {
	u8 phy_mode; /* mipi txphy mode */
	u8 force_clock_out_en; /* Force all MIPI clocks running */

	struct maxim2c_txphy_cfg phy_cfg[MAXIM2C_TXPHY_ID_MAX];
} maxim2c_mipi_txphy_t;

#endif /* __MAXIM2C_MIPI_TXPHY_H__ */
