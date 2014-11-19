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
#define WL18XX_IFTYPE_VER	8
#define WL18XX_MAJOR_VER	WLCORE_FW_VER_IGNORE
#define WL18XX_SUBTYPE_VER	WLCORE_FW_VER_IGNORE
#define WL18XX_MINOR_VER	13

#define WL18XX_CMD_MAX_SIZE          740

#define WL18XX_AGGR_BUFFER_SIZE		(13 * PAGE_SIZE)

#define WL18XX_NUM_TX_DESCRIPTORS 32
#define WL18XX_NUM_RX_DESCRIPTORS 32

#define WL18XX_NUM_MAC_ADDRESSES 2

#define WL18XX_RX_BA_MAX_SESSIONS 13

#define WL18XX_MAX_AP_STATIONS 10
#define WL18XX_MAX_LINKS 16

struct wl18xx_priv {
	/* buffer for sending commands to FW */
	u8 cmd_buf[WL18XX_CMD_MAX_SIZE];

	struct wl18xx_priv_conf conf;

	/* Index of last released Tx desc in FW */
	u8 last_fw_rls_idx;

	/* number of keys requiring extra spare mem-blocks */
	int extra_spare_key_count;
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

	/* A bitmap representing the currently suspended links. The suspend
	 * is short lived, for multi-channel Tx requirements.
	 */
	__le32 link_suspend_bitmap;

	/* packet threshold for an "almost empty" AC,
	 * for Tx schedulng purposes
	 */
	u8 tx_ac_threshold;

	/* number of packets to queue up for a link in PS */
	u8 tx_ps_threshold;

	/* number of packet to queue up for a suspended link */
	u8 tx_suspend_threshold;

	/* Should have less than this number of packets in queue of a slow
	 * link to qualify as high priority link
	 */
	u8 tx_slow_link_prio_threshold;

	/* Should have less than this number of packets in queue of a fast
	 * link to qualify as high priority link
	 */
	u8 tx_fast_link_prio_threshold;

	/* Should have less than this number of packets in queue of a slow
	 * link before we stop queuing up packets for it.
	 */
	u8 tx_slow_stop_threshold;

	/* Should have less than this number of packets in queue of a fast
	 * link before we stop queuing up packets for it.
	 */
	u8 tx_fast_stop_threshold;

	u8 padding[3];
};

struct wl18xx_fw_packet_counters {
	/* Cumulative counter of released packets per AC */
	u8 tx_released_pkts[NUM_TX_QUEUES];

	/* Cumulative counter of freed packets per HLID */
	u8 tx_lnk_free_pkts[WL18XX_MAX_LINKS];

	/* Cumulative counter of released Voice memory blocks */
	u8 tx_voice_released_blks;

	/* Tx rate of the last transmitted packet */
	u8 tx_last_rate;

	u8 padding[2];
} __packed;

/* FW status registers */
struct wl18xx_fw_status {
	__le32 intr;
	u8  fw_rx_counter;
	u8  drv_rx_counter;
	u8  reserved;
	u8  tx_results_counter;
	__le32 rx_pkt_descs[WL18XX_NUM_RX_DESCRIPTORS];

	__le32 fw_localtime;

	/*
	 * A bitmap (where each bit represents a single HLID)
	 * to indicate if the station is in PS mode.
	 */
	__le32 link_ps_bitmap;

	/*
	 * A bitmap (where each bit represents a single HLID) to indicate
	 * if the station is in Fast mode
	 */
	__le32 link_fast_bitmap;

	/* Cumulative counter of total released mem blocks since FW-reset */
	__le32 total_released_blks;

	/* Size (in Memory Blocks) of TX pool */
	__le32 tx_total;

	struct wl18xx_fw_packet_counters counters;

	__le32 log_start_addr;

	/* Private status to be used by the lower drivers */
	struct wl18xx_fw_status_priv priv;
} __packed;

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
