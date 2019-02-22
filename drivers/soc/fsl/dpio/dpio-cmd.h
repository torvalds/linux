/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016 NXP
 *
 */
#ifndef _FSL_DPIO_CMD_H
#define _FSL_DPIO_CMD_H

/* DPIO Version */
#define DPIO_VER_MAJOR			4
#define DPIO_VER_MINOR			2

/* Command Versioning */

#define DPIO_CMD_ID_OFFSET		4
#define DPIO_CMD_BASE_VERSION		1

#define DPIO_CMD(id)	(((id) << DPIO_CMD_ID_OFFSET) | DPIO_CMD_BASE_VERSION)

/* Command IDs */
#define DPIO_CMDID_CLOSE				DPIO_CMD(0x800)
#define DPIO_CMDID_OPEN					DPIO_CMD(0x803)
#define DPIO_CMDID_GET_API_VERSION			DPIO_CMD(0xa03)
#define DPIO_CMDID_ENABLE				DPIO_CMD(0x002)
#define DPIO_CMDID_DISABLE				DPIO_CMD(0x003)
#define DPIO_CMDID_GET_ATTR				DPIO_CMD(0x004)
#define DPIO_CMDID_RESET				DPIO_CMD(0x005)

struct dpio_cmd_open {
	__le32 dpio_id;
};

#define DPIO_CHANNEL_MODE_MASK		0x3

struct dpio_rsp_get_attr {
	/* cmd word 0 */
	__le32 id;
	__le16 qbman_portal_id;
	u8 num_priorities;
	u8 channel_mode;
	/* cmd word 1 */
	__le64 qbman_portal_ce_addr;
	/* cmd word 2 */
	__le64 qbman_portal_ci_addr;
	/* cmd word 3 */
	__le32 qbman_version;
};

#endif /* _FSL_DPIO_CMD_H */
