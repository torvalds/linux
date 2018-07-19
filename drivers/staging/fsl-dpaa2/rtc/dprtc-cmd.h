// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#ifndef _FSL_DPRTC_CMD_H
#define _FSL_DPRTC_CMD_H

/* DPRTC Version */
#define DPRTC_VER_MAJOR			2
#define DPRTC_VER_MINOR			0

/* Command versioning */
#define DPRTC_CMD_BASE_VERSION		1
#define DPRTC_CMD_ID_OFFSET		4

#define DPRTC_CMD(id)	(((id) << DPRTC_CMD_ID_OFFSET) | DPRTC_CMD_BASE_VERSION)

/* Command IDs */
#define DPRTC_CMDID_CLOSE			DPRTC_CMD(0x800)
#define DPRTC_CMDID_OPEN			DPRTC_CMD(0x810)
#define DPRTC_CMDID_CREATE			DPRTC_CMD(0x910)
#define DPRTC_CMDID_DESTROY			DPRTC_CMD(0x990)
#define DPRTC_CMDID_GET_API_VERSION		DPRTC_CMD(0xa10)

#define DPRTC_CMDID_ENABLE			DPRTC_CMD(0x002)
#define DPRTC_CMDID_DISABLE			DPRTC_CMD(0x003)
#define DPRTC_CMDID_GET_ATTR			DPRTC_CMD(0x004)
#define DPRTC_CMDID_RESET			DPRTC_CMD(0x005)
#define DPRTC_CMDID_IS_ENABLED			DPRTC_CMD(0x006)

#define DPRTC_CMDID_SET_IRQ_ENABLE		DPRTC_CMD(0x012)
#define DPRTC_CMDID_GET_IRQ_ENABLE		DPRTC_CMD(0x013)
#define DPRTC_CMDID_SET_IRQ_MASK		DPRTC_CMD(0x014)
#define DPRTC_CMDID_GET_IRQ_MASK		DPRTC_CMD(0x015)
#define DPRTC_CMDID_GET_IRQ_STATUS		DPRTC_CMD(0x016)
#define DPRTC_CMDID_CLEAR_IRQ_STATUS		DPRTC_CMD(0x017)

#define DPRTC_CMDID_SET_CLOCK_OFFSET		DPRTC_CMD(0x1d0)
#define DPRTC_CMDID_SET_FREQ_COMPENSATION	DPRTC_CMD(0x1d1)
#define DPRTC_CMDID_GET_FREQ_COMPENSATION	DPRTC_CMD(0x1d2)
#define DPRTC_CMDID_GET_TIME			DPRTC_CMD(0x1d3)
#define DPRTC_CMDID_SET_TIME			DPRTC_CMD(0x1d4)
#define DPRTC_CMDID_SET_ALARM			DPRTC_CMD(0x1d5)
#define DPRTC_CMDID_SET_PERIODIC_PULSE		DPRTC_CMD(0x1d6)
#define DPRTC_CMDID_CLEAR_PERIODIC_PULSE	DPRTC_CMD(0x1d7)
#define DPRTC_CMDID_SET_EXT_TRIGGER		DPRTC_CMD(0x1d8)
#define DPRTC_CMDID_CLEAR_EXT_TRIGGER		DPRTC_CMD(0x1d9)
#define DPRTC_CMDID_GET_EXT_TRIGGER_TIMESTAMP	DPRTC_CMD(0x1dA)

/* Macros for accessing command fields smaller than 1byte */
#define DPRTC_MASK(field)        \
	GENMASK(DPRTC_##field##_SHIFT + DPRTC_##field##_SIZE - 1, \
		DPRTC_##field##_SHIFT)
#define dprtc_get_field(var, field)      \
	(((var) & DPRTC_MASK(field)) >> DPRTC_##field##_SHIFT)

#pragma pack(push, 1)
struct dprtc_cmd_open {
	__le32 dprtc_id;
};

struct dprtc_cmd_destroy {
	__le32 object_id;
};

#define DPRTC_ENABLE_SHIFT	0
#define DPRTC_ENABLE_SIZE	1

struct dprtc_rsp_is_enabled {
	u8 en;
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

struct dprtc_rsp_get_attributes {
	__le32 pad;
	__le32 id;
};

struct dprtc_cmd_set_clock_offset {
	__le64 offset;
};

struct dprtc_get_freq_compensation {
	__le32 freq_compensation;
};

struct dprtc_time {
	__le64 time;
};

struct dprtc_rsp_get_api_version {
	__le16 major;
	__le16 minor;
};

#pragma pack(pop)

#endif /* _FSL_DPRTC_CMD_H */
