// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#ifndef _FSL_DPRTC_CMD_H
#define _FSL_DPRTC_CMD_H

/* Command versioning */
#define DPRTC_CMD_BASE_VERSION		1
#define DPRTC_CMD_ID_OFFSET		4

#define DPRTC_CMD(id)	(((id) << DPRTC_CMD_ID_OFFSET) | DPRTC_CMD_BASE_VERSION)

/* Command IDs */
#define DPRTC_CMDID_CLOSE			DPRTC_CMD(0x800)
#define DPRTC_CMDID_OPEN			DPRTC_CMD(0x810)

#pragma pack(push, 1)
struct dprtc_cmd_open {
	__le32 dprtc_id;
};

#pragma pack(pop)

#endif /* _FSL_DPRTC_CMD_H */
