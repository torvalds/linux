/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2019 NXP
 */
#ifndef _FSL_DPMAC_CMD_H
#define _FSL_DPMAC_CMD_H

/* DPMAC Version */
#define DPMAC_VER_MAJOR				4
#define DPMAC_VER_MINOR				4
#define DPMAC_CMD_BASE_VERSION			1
#define DPMAC_CMD_2ND_VERSION			2
#define DPMAC_CMD_ID_OFFSET			4

#define DPMAC_CMD(id)	(((id) << DPMAC_CMD_ID_OFFSET) | DPMAC_CMD_BASE_VERSION)
#define DPMAC_CMD_V2(id) (((id) << DPMAC_CMD_ID_OFFSET) | DPMAC_CMD_2ND_VERSION)

/* Command IDs */
#define DPMAC_CMDID_CLOSE		DPMAC_CMD(0x800)
#define DPMAC_CMDID_OPEN		DPMAC_CMD(0x80c)

#define DPMAC_CMDID_GET_ATTR		DPMAC_CMD(0x004)
#define DPMAC_CMDID_SET_LINK_STATE	DPMAC_CMD_V2(0x0c3)

#define DPMAC_CMDID_GET_COUNTER		DPMAC_CMD(0x0c4)

/* Macros for accessing command fields smaller than 1byte */
#define DPMAC_MASK(field)        \
	GENMASK(DPMAC_##field##_SHIFT + DPMAC_##field##_SIZE - 1, \
		DPMAC_##field##_SHIFT)

#define dpmac_set_field(var, field, val) \
	((var) |= (((val) << DPMAC_##field##_SHIFT) & DPMAC_MASK(field)))
#define dpmac_get_field(var, field)      \
	(((var) & DPMAC_MASK(field)) >> DPMAC_##field##_SHIFT)

struct dpmac_cmd_open {
	__le32 dpmac_id;
};

struct dpmac_rsp_get_attributes {
	u8 eth_if;
	u8 link_type;
	__le16 id;
	__le32 max_rate;
};

#define DPMAC_STATE_SIZE	1
#define DPMAC_STATE_SHIFT	0
#define DPMAC_STATE_VALID_SIZE	1
#define DPMAC_STATE_VALID_SHIFT	1

struct dpmac_cmd_set_link_state {
	__le64 options;
	__le32 rate;
	__le32 pad0;
	/* from lsb: up:1, state_valid:1 */
	u8 state;
	u8 pad1[7];
	__le64 supported;
	__le64 advertising;
};

struct dpmac_cmd_get_counter {
	u8 id;
};

struct dpmac_rsp_get_counter {
	u64 pad;
	u64 counter;
};

#endif /* _FSL_DPMAC_CMD_H */
