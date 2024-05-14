/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#ifndef _FSL_DPRTC_CMD_H
#define _FSL_DPRTC_CMD_H

/* Command versioning */
#define DPRTC_CMD_BASE_VERSION		1
#define DPRTC_CMD_VERSION_2		2
#define DPRTC_CMD_ID_OFFSET		4

#define DPRTC_CMD(id)	(((id) << DPRTC_CMD_ID_OFFSET) | DPRTC_CMD_BASE_VERSION)
#define DPRTC_CMD_V2(id) (((id) << DPRTC_CMD_ID_OFFSET) | DPRTC_CMD_VERSION_2)

/* Command IDs */
#define DPRTC_CMDID_CLOSE			DPRTC_CMD(0x800)
#define DPRTC_CMDID_OPEN			DPRTC_CMD(0x810)

#define DPRTC_CMDID_SET_IRQ_ENABLE		DPRTC_CMD(0x012)
#define DPRTC_CMDID_GET_IRQ_ENABLE		DPRTC_CMD(0x013)
#define DPRTC_CMDID_SET_IRQ_MASK		DPRTC_CMD_V2(0x014)
#define DPRTC_CMDID_GET_IRQ_MASK		DPRTC_CMD(0x015)
#define DPRTC_CMDID_GET_IRQ_STATUS		DPRTC_CMD(0x016)
#define DPRTC_CMDID_CLEAR_IRQ_STATUS		DPRTC_CMD(0x017)

#pragma pack(push, 1)
struct dprtc_cmd_open {
	__le32 dprtc_id;
};

struct dprtc_cmd_get_irq {
	__le32 pad;
	u8 irq_index;
};

struct dprtc_cmd_set_irq_enable {
	u8 en;
	u8 pad[3];
	u8 irq_index;
};

struct dprtc_rsp_get_irq_enable {
	u8 en;
};

struct dprtc_cmd_set_irq_mask {
	__le32 mask;
	u8 irq_index;
};

struct dprtc_rsp_get_irq_mask {
	__le32 mask;
};

struct dprtc_cmd_get_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dprtc_rsp_get_irq_status {
	__le32 status;
};

struct dprtc_cmd_clear_irq_status {
	__le32 status;
	u8 irq_index;
};

#pragma pack(pop)

#endif /* _FSL_DPRTC_CMD_H */
