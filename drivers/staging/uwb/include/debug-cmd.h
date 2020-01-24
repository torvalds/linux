/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Ultra Wide Band
 * Debug interface commands
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 */
#ifndef __LINUX__UWB__DEBUG_CMD_H__
#define __LINUX__UWB__DEBUG_CMD_H__

#include <linux/types.h>

/*
 * Debug interface commands
 *
 * UWB_DBG_CMD_RSV_ESTABLISH: Establish a new unicast reservation.
 *
 * UWB_DBG_CMD_RSV_TERMINATE: Terminate the Nth reservation.
 */

enum uwb_dbg_cmd_type {
	UWB_DBG_CMD_RSV_ESTABLISH = 1,
	UWB_DBG_CMD_RSV_TERMINATE = 2,
	UWB_DBG_CMD_IE_ADD = 3,
	UWB_DBG_CMD_IE_RM = 4,
	UWB_DBG_CMD_RADIO_START = 5,
	UWB_DBG_CMD_RADIO_STOP = 6,
};

struct uwb_dbg_cmd_rsv_establish {
	__u8  target[6];
	__u8  type;
	__u16 max_mas;
	__u16 min_mas;
	__u8  max_interval;
};

struct uwb_dbg_cmd_rsv_terminate {
	int index;
};

struct uwb_dbg_cmd_ie {
	__u8 data[128];
	int len;
};

struct uwb_dbg_cmd {
	__u32 type;
	union {
		struct uwb_dbg_cmd_rsv_establish rsv_establish;
		struct uwb_dbg_cmd_rsv_terminate rsv_terminate;
		struct uwb_dbg_cmd_ie ie_add;
		struct uwb_dbg_cmd_ie ie_rm;
	};
};

#endif /* #ifndef __LINUX__UWB__DEBUG_CMD_H__ */
