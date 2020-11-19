/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2020 NXP
 *
 */

#ifndef __FSL_DPSW_CMD_H
#define __FSL_DPSW_CMD_H

/* DPSW Version */
#define DPSW_VER_MAJOR		8
#define DPSW_VER_MINOR		5

#define DPSW_CMD_BASE_VERSION	1
#define DPSW_CMD_VERSION_2	2
#define DPSW_CMD_ID_OFFSET	4

#define DPSW_CMD_ID(id)	(((id) << DPSW_CMD_ID_OFFSET) | DPSW_CMD_BASE_VERSION)
#define DPSW_CMD_V2(id) (((id) << DPSW_CMD_ID_OFFSET) | DPSW_CMD_VERSION_2)

/* Command IDs */
#define DPSW_CMDID_CLOSE                    DPSW_CMD_ID(0x800)
#define DPSW_CMDID_OPEN                     DPSW_CMD_ID(0x802)

#define DPSW_CMDID_GET_API_VERSION          DPSW_CMD_ID(0xa02)

#define DPSW_CMDID_ENABLE                   DPSW_CMD_ID(0x002)
#define DPSW_CMDID_DISABLE                  DPSW_CMD_ID(0x003)
#define DPSW_CMDID_GET_ATTR                 DPSW_CMD_ID(0x004)
#define DPSW_CMDID_RESET                    DPSW_CMD_ID(0x005)

#define DPSW_CMDID_SET_IRQ_ENABLE           DPSW_CMD_ID(0x012)

#define DPSW_CMDID_SET_IRQ_MASK             DPSW_CMD_ID(0x014)

#define DPSW_CMDID_GET_IRQ_STATUS           DPSW_CMD_ID(0x016)
#define DPSW_CMDID_CLEAR_IRQ_STATUS         DPSW_CMD_ID(0x017)

#define DPSW_CMDID_IF_SET_TCI               DPSW_CMD_ID(0x030)
#define DPSW_CMDID_IF_SET_STP               DPSW_CMD_ID(0x031)

#define DPSW_CMDID_IF_GET_COUNTER           DPSW_CMD_V2(0x034)

#define DPSW_CMDID_IF_ENABLE                DPSW_CMD_ID(0x03D)
#define DPSW_CMDID_IF_DISABLE               DPSW_CMD_ID(0x03E)

#define DPSW_CMDID_IF_SET_MAX_FRAME_LENGTH  DPSW_CMD_ID(0x044)

#define DPSW_CMDID_IF_GET_LINK_STATE        DPSW_CMD_ID(0x046)
#define DPSW_CMDID_IF_SET_FLOODING          DPSW_CMD_ID(0x047)
#define DPSW_CMDID_IF_SET_BROADCAST         DPSW_CMD_ID(0x048)

#define DPSW_CMDID_IF_GET_TCI               DPSW_CMD_ID(0x04A)

#define DPSW_CMDID_IF_SET_LINK_CFG          DPSW_CMD_ID(0x04C)

#define DPSW_CMDID_VLAN_ADD                 DPSW_CMD_ID(0x060)
#define DPSW_CMDID_VLAN_ADD_IF              DPSW_CMD_ID(0x061)
#define DPSW_CMDID_VLAN_ADD_IF_UNTAGGED     DPSW_CMD_ID(0x062)

#define DPSW_CMDID_VLAN_REMOVE_IF           DPSW_CMD_ID(0x064)
#define DPSW_CMDID_VLAN_REMOVE_IF_UNTAGGED  DPSW_CMD_ID(0x065)
#define DPSW_CMDID_VLAN_REMOVE_IF_FLOODING  DPSW_CMD_ID(0x066)
#define DPSW_CMDID_VLAN_REMOVE              DPSW_CMD_ID(0x067)

#define DPSW_CMDID_FDB_ADD_UNICAST          DPSW_CMD_ID(0x084)
#define DPSW_CMDID_FDB_REMOVE_UNICAST       DPSW_CMD_ID(0x085)
#define DPSW_CMDID_FDB_ADD_MULTICAST        DPSW_CMD_ID(0x086)
#define DPSW_CMDID_FDB_REMOVE_MULTICAST     DPSW_CMD_ID(0x087)
#define DPSW_CMDID_FDB_SET_LEARNING_MODE    DPSW_CMD_ID(0x088)
#define DPSW_CMDID_FDB_DUMP                 DPSW_CMD_ID(0x08A)

#define DPSW_CMDID_IF_GET_PORT_MAC_ADDR     DPSW_CMD_ID(0x0A7)
#define DPSW_CMDID_IF_GET_PRIMARY_MAC_ADDR  DPSW_CMD_ID(0x0A8)
#define DPSW_CMDID_IF_SET_PRIMARY_MAC_ADDR  DPSW_CMD_ID(0x0A9)

/* Macros for accessing command fields smaller than 1byte */
#define DPSW_MASK(field)        \
	GENMASK(DPSW_##field##_SHIFT + DPSW_##field##_SIZE - 1, \
		DPSW_##field##_SHIFT)
#define dpsw_set_field(var, field, val) \
	((var) |= (((val) << DPSW_##field##_SHIFT) & DPSW_MASK(field)))
#define dpsw_get_field(var, field)      \
	(((var) & DPSW_MASK(field)) >> DPSW_##field##_SHIFT)
#define dpsw_get_bit(var, bit) \
	(((var)  >> (bit)) & GENMASK(0, 0))

#pragma pack(push, 1)
struct dpsw_cmd_open {
	__le32 dpsw_id;
};

#define DPSW_COMPONENT_TYPE_SHIFT	0
#define DPSW_COMPONENT_TYPE_SIZE	4

struct dpsw_cmd_create {
	/* cmd word 0 */
	__le16 num_ifs;
	u8 max_fdbs;
	u8 max_meters_per_if;
	/* from LSB: only the first 4 bits */
	u8 component_type;
	u8 pad[3];
	/* cmd word 1 */
	__le16 max_vlans;
	__le16 max_fdb_entries;
	__le16 fdb_aging_time;
	__le16 max_fdb_mc_groups;
	/* cmd word 2 */
	__le64 options;
};

struct dpsw_cmd_destroy {
	__le32 dpsw_id;
};

#define DPSW_ENABLE_SHIFT 0
#define DPSW_ENABLE_SIZE  1

struct dpsw_rsp_is_enabled {
	/* from LSB: enable:1 */
	u8 enabled;
};

struct dpsw_cmd_set_irq_enable {
	u8 enable_state;
	u8 pad[3];
	u8 irq_index;
};

struct dpsw_cmd_get_irq_enable {
	__le32 pad;
	u8 irq_index;
};

struct dpsw_rsp_get_irq_enable {
	u8 enable_state;
};

struct dpsw_cmd_set_irq_mask {
	__le32 mask;
	u8 irq_index;
};

struct dpsw_cmd_get_irq_mask {
	__le32 pad;
	u8 irq_index;
};

struct dpsw_rsp_get_irq_mask {
	__le32 mask;
};

struct dpsw_cmd_get_irq_status {
	__le32 status;
	u8 irq_index;
};

struct dpsw_rsp_get_irq_status {
	__le32 status;
};

struct dpsw_cmd_clear_irq_status {
	__le32 status;
	u8 irq_index;
};

#define DPSW_COMPONENT_TYPE_SHIFT	0
#define DPSW_COMPONENT_TYPE_SIZE	4

struct dpsw_rsp_get_attr {
	/* cmd word 0 */
	__le16 num_ifs;
	u8 max_fdbs;
	u8 num_fdbs;
	__le16 max_vlans;
	__le16 num_vlans;
	/* cmd word 1 */
	__le16 max_fdb_entries;
	__le16 fdb_aging_time;
	__le32 dpsw_id;
	/* cmd word 2 */
	__le16 mem_size;
	__le16 max_fdb_mc_groups;
	u8 max_meters_per_if;
	/* from LSB only the first 4 bits */
	u8 component_type;
	__le16 pad;
	/* cmd word 3 */
	__le64 options;
};

struct dpsw_cmd_if_set_flooding {
	__le16 if_id;
	/* from LSB: enable:1 */
	u8 enable;
};

struct dpsw_cmd_if_set_broadcast {
	__le16 if_id;
	/* from LSB: enable:1 */
	u8 enable;
};

#define DPSW_VLAN_ID_SHIFT	0
#define DPSW_VLAN_ID_SIZE	12
#define DPSW_DEI_SHIFT		12
#define DPSW_DEI_SIZE		1
#define DPSW_PCP_SHIFT		13
#define DPSW_PCP_SIZE		3

struct dpsw_cmd_if_set_tci {
	__le16 if_id;
	/* from LSB: VLAN_ID:12 DEI:1 PCP:3 */
	__le16 conf;
};

struct dpsw_cmd_if_get_tci {
	__le16 if_id;
};

struct dpsw_rsp_if_get_tci {
	__le16 pad;
	__le16 vlan_id;
	u8 dei;
	u8 pcp;
};

#define DPSW_STATE_SHIFT	0
#define DPSW_STATE_SIZE		4

struct dpsw_cmd_if_set_stp {
	__le16 if_id;
	__le16 vlan_id;
	/* only the first LSB 4 bits */
	u8 state;
};

#define DPSW_COUNTER_TYPE_SHIFT		0
#define DPSW_COUNTER_TYPE_SIZE		5

struct dpsw_cmd_if_get_counter {
	__le16 if_id;
	/* from LSB: type:5 */
	u8 type;
};

struct dpsw_rsp_if_get_counter {
	__le64 pad;
	__le64 counter;
};

struct dpsw_cmd_if {
	__le16 if_id;
};

struct dpsw_cmd_if_set_max_frame_length {
	__le16 if_id;
	__le16 frame_length;
};

struct dpsw_cmd_if_set_link_cfg {
	/* cmd word 0 */
	__le16 if_id;
	u8 pad[6];
	/* cmd word 1 */
	__le32 rate;
	__le32 pad1;
	/* cmd word 2 */
	__le64 options;
};

struct dpsw_cmd_if_get_link_state {
	__le16 if_id;
};

#define DPSW_UP_SHIFT	0
#define DPSW_UP_SIZE	1

struct dpsw_rsp_if_get_link_state {
	/* cmd word 0 */
	__le32 pad0;
	u8 up;
	u8 pad1[3];
	/* cmd word 1 */
	__le32 rate;
	__le32 pad2;
	/* cmd word 2 */
	__le64 options;
};

struct dpsw_vlan_add {
	__le16 fdb_id;
	__le16 vlan_id;
};

struct dpsw_cmd_vlan_manage_if {
	/* cmd word 0 */
	__le16 pad0;
	__le16 vlan_id;
	__le32 pad1;
	/* cmd word 1-4 */
	__le64 if_id[4];
};

struct dpsw_cmd_vlan_remove {
	__le16 pad;
	__le16 vlan_id;
};

struct dpsw_cmd_fdb_add {
	__le32 pad;
	__le16 fdb_aging_time;
	__le16 num_fdb_entries;
};

struct dpsw_rsp_fdb_add {
	__le16 fdb_id;
};

struct dpsw_cmd_fdb_remove {
	__le16 fdb_id;
};

#define DPSW_ENTRY_TYPE_SHIFT	0
#define DPSW_ENTRY_TYPE_SIZE	4

struct dpsw_cmd_fdb_unicast_op {
	/* cmd word 0 */
	__le16 fdb_id;
	u8 mac_addr[6];
	/* cmd word 1 */
	__le16 if_egress;
	/* only the first 4 bits from LSB */
	u8 type;
};

struct dpsw_cmd_fdb_multicast_op {
	/* cmd word 0 */
	__le16 fdb_id;
	__le16 num_ifs;
	/* only the first 4 bits from LSB */
	u8 type;
	u8 pad[3];
	/* cmd word 1 */
	u8 mac_addr[6];
	__le16 pad2;
	/* cmd word 2-5 */
	__le64 if_id[4];
};

#define DPSW_LEARNING_MODE_SHIFT	0
#define DPSW_LEARNING_MODE_SIZE		4

struct dpsw_cmd_fdb_set_learning_mode {
	__le16 fdb_id;
	/* only the first 4 bits from LSB */
	u8 mode;
};

struct dpsw_cmd_fdb_dump {
	__le16 fdb_id;
	__le16 pad0;
	__le32 pad1;
	__le64 iova_addr;
	__le32 iova_size;
};

struct dpsw_rsp_fdb_dump {
	__le16 num_entries;
};

struct dpsw_rsp_get_api_version {
	__le16 version_major;
	__le16 version_minor;
};

struct dpsw_rsp_if_get_mac_addr {
	__le16 pad;
	u8 mac_addr[6];
};

struct dpsw_cmd_if_set_mac_addr {
	__le16 if_id;
	u8 mac_addr[6];
};

#pragma pack(pop)
#endif /* __FSL_DPSW_CMD_H */
