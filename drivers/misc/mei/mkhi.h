/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2003-2022, Intel Corporation. All rights reserved.
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#ifndef _MEI_MKHI_H_
#define _MEI_MKHI_H_

#include <linux/types.h>

#define MKHI_FEATURE_PTT 0x10

#define MKHI_FWCAPS_GROUP_ID 0x3
#define MKHI_FWCAPS_SET_OS_VER_APP_RULE_CMD 6
#define MKHI_GEN_GROUP_ID 0xFF
#define MKHI_GEN_GET_FW_VERSION_CMD 0x2

#define MKHI_GROUP_ID_GFX              0x30
#define MKHI_GFX_RESET_WARN_CMD_REQ    0x0
#define MKHI_GFX_MEMORY_READY_CMD_REQ  0x1

/* Allow transition to PXP mode without approval */
#define MKHI_GFX_MEM_READY_PXP_ALLOWED  0x1

struct mkhi_rule_id {
	__le16 rule_type;
	u8 feature_id;
	u8 reserved;
} __packed;

struct mkhi_fwcaps {
	struct mkhi_rule_id id;
	u8 len;
	u8 data[];
} __packed;

struct mkhi_msg_hdr {
	u8  group_id;
	u8  command;
	u8  reserved;
	u8  result;
} __packed;

struct mkhi_msg {
	struct mkhi_msg_hdr hdr;
	u8 data[];
} __packed;

struct mkhi_gfx_mem_ready {
	struct mkhi_msg_hdr hdr;
	u32    flags;
} __packed;

#endif /* _MEI_MKHI_H_ */
