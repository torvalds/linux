/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl1271
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#ifndef __BOOT_H__
#define __BOOT_H__

#include "wlcore.h"

int wlcore_boot_upload_firmware(struct wl1271 *wl);
int wlcore_boot_upload_nvs(struct wl1271 *wl);
int wlcore_boot_run_firmware(struct wl1271 *wl);

#define WL1271_NO_SUBBANDS 8
#define WL1271_NO_POWER_LEVELS 4
#define WL1271_FW_VERSION_MAX_LEN 20

struct wl1271_static_data {
	u8 mac_address[ETH_ALEN];
	u8 padding[2];
	u8 fw_version[WL1271_FW_VERSION_MAX_LEN];
	u32 hw_version;
	u8 tx_power_table[WL1271_NO_SUBBANDS][WL1271_NO_POWER_LEVELS];
	u8 priv[];
};

/* number of times we try to read the INIT interrupt */
#define INIT_LOOP 20000

/* delay between retries */
#define INIT_LOOP_DELAY 50

#define WU_COUNTER_PAUSE_VAL 0x3FF
#define WELP_ARM_COMMAND_VAL 0x4

#endif
