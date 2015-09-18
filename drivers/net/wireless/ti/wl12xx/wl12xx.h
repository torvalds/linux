/*
 * This file is part of wl12xx
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

#ifndef __WL12XX_PRIV_H__
#define __WL12XX_PRIV_H__

#include "conf.h"

/* WiLink 6/7 chip IDs */
#define CHIP_ID_127X_PG10              (0x04030101)
#define CHIP_ID_127X_PG20              (0x04030111)
#define CHIP_ID_128X_PG10              (0x05030101)
#define CHIP_ID_128X_PG20              (0x05030111)

/* FW chip version for wl127x */
#define WL127X_CHIP_VER		6
/* minimum single-role FW version for wl127x */
#define WL127X_IFTYPE_SR_VER	3
#define WL127X_MAJOR_SR_VER	10
#define WL127X_SUBTYPE_SR_VER	WLCORE_FW_VER_IGNORE
#define WL127X_MINOR_SR_VER	133
/* minimum multi-role FW version for wl127x */
#define WL127X_IFTYPE_MR_VER	5
#define WL127X_MAJOR_MR_VER	7
#define WL127X_SUBTYPE_MR_VER	WLCORE_FW_VER_IGNORE
#define WL127X_MINOR_MR_VER	42

/* FW chip version for wl128x */
#define WL128X_CHIP_VER		7
/* minimum single-role FW version for wl128x */
#define WL128X_IFTYPE_SR_VER	3
#define WL128X_MAJOR_SR_VER	10
#define WL128X_SUBTYPE_SR_VER	WLCORE_FW_VER_IGNORE
#define WL128X_MINOR_SR_VER	133
/* minimum multi-role FW version for wl128x */
#define WL128X_IFTYPE_MR_VER	5
#define WL128X_MAJOR_MR_VER	7
#define WL128X_SUBTYPE_MR_VER	WLCORE_FW_VER_IGNORE
#define WL128X_MINOR_MR_VER	42

#define WL12XX_AGGR_BUFFER_SIZE	(4 * PAGE_SIZE)

#define WL12XX_NUM_TX_DESCRIPTORS 16
#define WL12XX_NUM_RX_DESCRIPTORS 8

#define WL12XX_NUM_MAC_ADDRESSES 2

#define WL12XX_RX_BA_MAX_SESSIONS 3

#define WL12XX_MAX_AP_STATIONS 8
#define WL12XX_MAX_LINKS 12

struct wl127x_rx_mem_pool_addr {
	u32 addr;
	u32 addr_extra;
};

struct wl12xx_priv {
	struct wl12xx_priv_conf conf;

	int ref_clock;
	int tcxo_clock;

	struct wl127x_rx_mem_pool_addr *rx_mem_addr;
};

/* Reference clock values */
enum {
	WL12XX_REFCLOCK_19	= 0, /* 19.2 MHz */
	WL12XX_REFCLOCK_26	= 1, /* 26 MHz */
	WL12XX_REFCLOCK_38	= 2, /* 38.4 MHz */
	WL12XX_REFCLOCK_52	= 3, /* 52 MHz */
	WL12XX_REFCLOCK_38_XTAL = 4, /* 38.4 MHz, XTAL */
	WL12XX_REFCLOCK_26_XTAL = 5, /* 26 MHz, XTAL */
};

/* TCXO clock values */
enum {
	WL12XX_TCXOCLOCK_19_2	= 0, /* 19.2MHz */
	WL12XX_TCXOCLOCK_26	= 1, /* 26 MHz */
	WL12XX_TCXOCLOCK_38_4	= 2, /* 38.4MHz */
	WL12XX_TCXOCLOCK_52	= 3, /* 52 MHz */
	WL12XX_TCXOCLOCK_16_368	= 4, /* 16.368 MHz */
	WL12XX_TCXOCLOCK_32_736	= 5, /* 32.736 MHz */
	WL12XX_TCXOCLOCK_16_8	= 6, /* 16.8 MHz */
	WL12XX_TCXOCLOCK_33_6	= 7, /* 33.6 MHz */
};

struct wl12xx_clock {
	u32	freq;
	bool	xtal;
	u8	hw_idx;
};

struct wl12xx_fw_packet_counters {
	/* Cumulative counter of released packets per AC */
	u8 tx_released_pkts[NUM_TX_QUEUES];

	/* Cumulative counter of freed packets per HLID */
	u8 tx_lnk_free_pkts[WL12XX_MAX_LINKS];

	/* Cumulative counter of released Voice memory blocks */
	u8 tx_voice_released_blks;

	/* Tx rate of the last transmitted packet */
	u8 tx_last_rate;

	u8 padding[2];
} __packed;

/* FW status registers */
struct wl12xx_fw_status {
	__le32 intr;
	u8  fw_rx_counter;
	u8  drv_rx_counter;
	u8  reserved;
	u8  tx_results_counter;
	__le32 rx_pkt_descs[WL12XX_NUM_RX_DESCRIPTORS];

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

	struct wl12xx_fw_packet_counters counters;

	__le32 log_start_addr;
} __packed;

#endif /* __WL12XX_PRIV_H__ */
