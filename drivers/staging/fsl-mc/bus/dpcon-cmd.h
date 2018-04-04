/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 *
 */
#ifndef _FSL_DPCON_CMD_H
#define _FSL_DPCON_CMD_H

/* DPCON Version */
#define DPCON_VER_MAJOR				3
#define DPCON_VER_MINOR				2

/* Command versioning */
#define DPCON_CMD_BASE_VERSION			1
#define DPCON_CMD_ID_OFFSET			4

#define DPCON_CMD(id)	(((id) << DPCON_CMD_ID_OFFSET) | DPCON_CMD_BASE_VERSION)

/* Command IDs */
#define DPCON_CMDID_CLOSE			DPCON_CMD(0x800)
#define DPCON_CMDID_OPEN			DPCON_CMD(0x808)

#define DPCON_CMDID_ENABLE			DPCON_CMD(0x002)
#define DPCON_CMDID_DISABLE			DPCON_CMD(0x003)
#define DPCON_CMDID_GET_ATTR			DPCON_CMD(0x004)
#define DPCON_CMDID_RESET			DPCON_CMD(0x005)

#define DPCON_CMDID_SET_NOTIFICATION		DPCON_CMD(0x100)

struct dpcon_cmd_open {
	__le32 dpcon_id;
};

#define DPCON_ENABLE			1

struct dpcon_rsp_get_attr {
	/* response word 0 */
	__le32 id;
	__le16 qbman_ch_id;
	u8 num_priorities;
	u8 pad;
};

struct dpcon_cmd_set_notification {
	/* cmd word 0 */
	__le32 dpio_id;
	u8 priority;
	u8 pad[3];
	/* cmd word 1 */
	__le64 user_ctx;
};

#endif /* _FSL_DPCON_CMD_H */
