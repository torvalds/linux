/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#ifndef __WL1251_H__
#define __WL1251_H__

#include <linux/bitops.h>

#include "wl12xx.h"
#include "acx.h"

#define WL1251_FW_NAME "wl1251-fw.bin"
#define WL1251_NVS_NAME "wl1251-nvs.bin"

#define WL1251_POWER_ON_SLEEP 10 /* in miliseconds */

void wl1251_setup(struct wl12xx *wl);


struct wl1251_acx_memory {
	__le16 num_stations; /* number of STAs to be supported. */
	u16 reserved_1;

	/*
	 * Nmber of memory buffers for the RX mem pool.
	 * The actual number may be less if there are
	 * not enough blocks left for the minimum num
	 * of TX ones.
	 */
	u8 rx_mem_block_num;
	u8 reserved_2;
	u8 num_tx_queues; /* From 1 to 16 */
	u8 host_if_options; /* HOST_IF* */
	u8 tx_min_mem_block_num;
	u8 num_ssid_profiles;
	__le16 debug_buffer_size;
} __attribute__ ((packed));


#define ACX_RX_DESC_MIN                1
#define ACX_RX_DESC_MAX                127
#define ACX_RX_DESC_DEF                32
struct wl1251_acx_rx_queue_config {
	u8 num_descs;
	u8 pad;
	u8 type;
	u8 priority;
	__le32 dma_address;
} __attribute__ ((packed));

#define ACX_TX_DESC_MIN                1
#define ACX_TX_DESC_MAX                127
#define ACX_TX_DESC_DEF                16
struct wl1251_acx_tx_queue_config {
    u8 num_descs;
    u8 pad[2];
    u8 attributes;
} __attribute__ ((packed));

#define MAX_TX_QUEUE_CONFIGS 5
#define MAX_TX_QUEUES 4
struct wl1251_acx_config_memory {
	struct acx_header header;

	struct wl1251_acx_memory mem_config;
	struct wl1251_acx_rx_queue_config rx_queue_config;
	struct wl1251_acx_tx_queue_config tx_queue_config[MAX_TX_QUEUE_CONFIGS];
} __attribute__ ((packed));

struct wl1251_acx_mem_map {
	struct acx_header header;

	void *code_start;
	void *code_end;

	void *wep_defkey_start;
	void *wep_defkey_end;

	void *sta_table_start;
	void *sta_table_end;

	void *packet_template_start;
	void *packet_template_end;

	void *queue_memory_start;
	void *queue_memory_end;

	void *packet_memory_pool_start;
	void *packet_memory_pool_end;

	void *debug_buffer1_start;
	void *debug_buffer1_end;

	void *debug_buffer2_start;
	void *debug_buffer2_end;

	/* Number of blocks FW allocated for TX packets */
	u32 num_tx_mem_blocks;

	/* Number of blocks FW allocated for RX packets */
	u32 num_rx_mem_blocks;
} __attribute__ ((packed));

/*************************************************************************

    Host Interrupt Register (WiLink -> Host)

**************************************************************************/

/* RX packet is ready in Xfer buffer #0 */
#define WL1251_ACX_INTR_RX0_DATA      BIT(0)

/* TX result(s) are in the TX complete buffer */
#define WL1251_ACX_INTR_TX_RESULT	BIT(1)

/* OBSOLETE */
#define WL1251_ACX_INTR_TX_XFR		BIT(2)

/* RX packet is ready in Xfer buffer #1 */
#define WL1251_ACX_INTR_RX1_DATA	BIT(3)

/* Event was entered to Event MBOX #A */
#define WL1251_ACX_INTR_EVENT_A		BIT(4)

/* Event was entered to Event MBOX #B */
#define WL1251_ACX_INTR_EVENT_B		BIT(5)

/* OBSOLETE */
#define WL1251_ACX_INTR_WAKE_ON_HOST	BIT(6)

/* Trace meassge on MBOX #A */
#define WL1251_ACX_INTR_TRACE_A		BIT(7)

/* Trace meassge on MBOX #B */
#define WL1251_ACX_INTR_TRACE_B		BIT(8)

/* Command processing completion */
#define WL1251_ACX_INTR_CMD_COMPLETE	BIT(9)

/* Init sequence is done */
#define WL1251_ACX_INTR_INIT_COMPLETE	BIT(14)

#define WL1251_ACX_INTR_ALL           0xFFFFFFFF

#endif
