/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
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

#ifndef __BOOT_H__
#define __BOOT_H__

#include "wl1271.h"

int wl1271_boot(struct wl1271 *wl);

#define WL1271_NO_SUBBANDS 8
#define WL1271_NO_POWER_LEVELS 4
#define WL1271_FW_VERSION_MAX_LEN 20

struct wl1271_static_data {
	u8 mac_address[ETH_ALEN];
	u8 padding[2];
	u8 fw_version[WL1271_FW_VERSION_MAX_LEN];
	u32 hw_version;
	u8 tx_power_table[WL1271_NO_SUBBANDS][WL1271_NO_POWER_LEVELS];
};

/* number of times we try to read the INIT interrupt */
#define INIT_LOOP 20000

/* delay between retries */
#define INIT_LOOP_DELAY 50

#define REF_CLOCK            2
#define WU_COUNTER_PAUSE_VAL 0x3FF
#define WELP_ARM_COMMAND_VAL 0x4

#define OCP_CMD_LOOP  32

#define OCP_CMD_WRITE 0x1
#define OCP_CMD_READ  0x2

#define OCP_READY_MASK  BIT(18)
#define OCP_STATUS_MASK (BIT(16) | BIT(17))

#define OCP_STATUS_NO_RESP    0x00000
#define OCP_STATUS_OK         0x10000
#define OCP_STATUS_REQ_FAILED 0x20000
#define OCP_STATUS_RESP_ERROR 0x30000

#define OCP_REG_POLARITY 0x30032

#define CMD_MBOX_ADDRESS 0x407B4

#define POLARITY_LOW BIT(1)

#endif
