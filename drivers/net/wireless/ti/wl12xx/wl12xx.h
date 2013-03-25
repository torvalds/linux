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
#define WL127X_MINOR_SR_VER	115
/* minimum multi-role FW version for wl127x */
#define WL127X_IFTYPE_MR_VER	5
#define WL127X_MAJOR_MR_VER	7
#define WL127X_SUBTYPE_MR_VER	WLCORE_FW_VER_IGNORE
#define WL127X_MINOR_MR_VER	115

/* FW chip version for wl128x */
#define WL128X_CHIP_VER		7
/* minimum single-role FW version for wl128x */
#define WL128X_IFTYPE_SR_VER	3
#define WL128X_MAJOR_SR_VER	10
#define WL128X_SUBTYPE_SR_VER	WLCORE_FW_VER_IGNORE
#define WL128X_MINOR_SR_VER	115
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

#endif /* __WL12XX_PRIV_H__ */
