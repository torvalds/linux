/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL18XX_PRIV_H__
#define __WL18XX_PRIV_H__

#include "conf.h"

/* minimum FW required for driver */
#define WL18XX_CHIP_VER		8
#define WL18XX_IFTYPE_VER	2
#define WL18XX_MAJOR_VER	0
#define WL18XX_SUBTYPE_VER	0
#define WL18XX_MINOR_VER	100

#define WL18XX_CMD_MAX_SIZE          740

struct wl18xx_priv {
	/* buffer for sending commands to FW */
	u8 cmd_buf[WL18XX_CMD_MAX_SIZE];

	struct wl18xx_priv_conf conf;

	/* Index of last released Tx desc in FW */
	u8 last_fw_rls_idx;

	/* number of VIFs requiring extra spare mem-blocks */
	int extra_spare_vif_count;
};

#define WL18XX_FW_MAX_TX_STATUS_DESC 33

struct wl18xx_fw_status_priv {
	/*
	 * Index in released_tx_desc for first byte that holds
	 * released tx host desc
	 */
	u8 fw_release_idx;

	/*
	 * Array of host Tx descriptors, where fw_release_idx
	 * indicated the first released idx.
	 */
	u8 released_tx_desc[WL18XX_FW_MAX_TX_STATUS_DESC];

	u8 padding[2];
};

#define WL18XX_PHY_VERSION_MAX_LEN 20

struct wl18xx_static_data_priv {
	char phy_version[WL18XX_PHY_VERSION_MAX_LEN];
};

struct wl18xx_clk_cfg {
	u32 n;
	u32 m;
	u32 p;
	u32 q;
	bool swallow;
};

enum {
	CLOCK_CONFIG_16_2_M	= 1,
	CLOCK_CONFIG_16_368_M,
	CLOCK_CONFIG_16_8_M,
	CLOCK_CONFIG_19_2_M,
	CLOCK_CONFIG_26_M,
	CLOCK_CONFIG_32_736_M,
	CLOCK_CONFIG_33_6_M,
	CLOCK_CONFIG_38_468_M,
	CLOCK_CONFIG_52_M,

	NUM_CLOCK_CONFIGS,
};

#endif /* __WL18XX_PRIV_H__ */
