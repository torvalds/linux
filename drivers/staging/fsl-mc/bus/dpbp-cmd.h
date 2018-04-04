/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 */
#ifndef _FSL_DPBP_CMD_H
#define _FSL_DPBP_CMD_H

/* DPBP Version */
#define DPBP_VER_MAJOR				3
#define DPBP_VER_MINOR				2

/* Command versioning */
#define DPBP_CMD_BASE_VERSION			1
#define DPBP_CMD_ID_OFFSET			4

#define DPBP_CMD(id)	(((id) << DPBP_CMD_ID_OFFSET) | DPBP_CMD_BASE_VERSION)

/* Command IDs */
#define DPBP_CMDID_CLOSE		DPBP_CMD(0x800)
#define DPBP_CMDID_OPEN			DPBP_CMD(0x804)
#define DPBP_CMDID_GET_API_VERSION	DPBP_CMD(0xa04)

#define DPBP_CMDID_ENABLE		DPBP_CMD(0x002)
#define DPBP_CMDID_DISABLE		DPBP_CMD(0x003)
#define DPBP_CMDID_GET_ATTR		DPBP_CMD(0x004)
#define DPBP_CMDID_RESET		DPBP_CMD(0x005)
#define DPBP_CMDID_IS_ENABLED		DPBP_CMD(0x006)

struct dpbp_cmd_open {
	__le32 dpbp_id;
};

struct dpbp_cmd_destroy {
	__le32 object_id;
};

#define DPBP_ENABLE			0x1

struct dpbp_rsp_is_enabled {
	u8 enabled;
};

struct dpbp_rsp_get_attributes {
	/* response word 0 */
	__le16 pad;
	__le16 bpid;
	__le32 id;
	/* response word 1 */
	__le16 version_major;
	__le16 version_minor;
};

#endif /* _FSL_DPBP_CMD_H */
