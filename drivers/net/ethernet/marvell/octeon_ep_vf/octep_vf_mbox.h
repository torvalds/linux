/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#ifndef _OCTEP_VF_MBOX_H_
#define _OCTEP_VF_MBOX_H_

/* When a new command is implemented, VF Mbox version should be bumped.
 */
enum octep_pfvf_mbox_version {
	OCTEP_PFVF_MBOX_VERSION_V0,
	OCTEP_PFVF_MBOX_VERSION_V1,
	OCTEP_PFVF_MBOX_VERSION_V2
};

#define OCTEP_PFVF_MBOX_VERSION_CURRENT OCTEP_PFVF_MBOX_VERSION_V2

enum octep_pfvf_mbox_opcode {
	OCTEP_PFVF_MBOX_CMD_VERSION,
	OCTEP_PFVF_MBOX_CMD_SET_MTU,
	OCTEP_PFVF_MBOX_CMD_SET_MAC_ADDR,
	OCTEP_PFVF_MBOX_CMD_GET_MAC_ADDR,
	OCTEP_PFVF_MBOX_CMD_GET_LINK_INFO,
	OCTEP_PFVF_MBOX_CMD_GET_STATS,
	OCTEP_PFVF_MBOX_CMD_SET_RX_STATE,
	OCTEP_PFVF_MBOX_CMD_SET_LINK_STATUS,
	OCTEP_PFVF_MBOX_CMD_GET_LINK_STATUS,
	OCTEP_PFVF_MBOX_CMD_GET_MTU,
	OCTEP_PFVF_MBOX_CMD_DEV_REMOVE,
	OCTEP_PFVF_MBOX_CMD_GET_FW_INFO,
	OCTEP_PFVF_MBOX_CMD_SET_OFFLOADS,
	OCTEP_PFVF_MBOX_NOTIF_LINK_STATUS,
	OCTEP_PFVF_MBOX_CMD_MAX,
};

enum octep_pfvf_mbox_word_type {
	OCTEP_PFVF_MBOX_TYPE_CMD,
	OCTEP_PFVF_MBOX_TYPE_RSP_ACK,
	OCTEP_PFVF_MBOX_TYPE_RSP_NACK,
};

enum octep_pfvf_mbox_cmd_status {
	OCTEP_PFVF_MBOX_CMD_STATUS_NOT_SETUP = 1,
	OCTEP_PFVF_MBOX_CMD_STATUS_TIMEDOUT = 2,
	OCTEP_PFVF_MBOX_CMD_STATUS_NACK = 3,
	OCTEP_PFVF_MBOX_CMD_STATUS_BUSY = 4,
	OCTEP_PFVF_MBOX_CMD_STATUS_ERR = 5
};

enum octep_pfvf_link_status {
	OCTEP_PFVF_LINK_STATUS_DOWN,
	OCTEP_PFVF_LINK_STATUS_UP,
};

enum octep_pfvf_link_speed {
	OCTEP_PFVF_LINK_SPEED_NONE,
	OCTEP_PFVF_LINK_SPEED_1000,
	OCTEP_PFVF_LINK_SPEED_10000,
	OCTEP_PFVF_LINK_SPEED_25000,
	OCTEP_PFVF_LINK_SPEED_40000,
	OCTEP_PFVF_LINK_SPEED_50000,
	OCTEP_PFVF_LINK_SPEED_100000,
	OCTEP_PFVF_LINK_SPEED_LAST,
};

enum octep_pfvf_link_duplex {
	OCTEP_PFVF_LINK_HALF_DUPLEX,
	OCTEP_PFVF_LINK_FULL_DUPLEX,
};

enum octep_pfvf_link_autoneg {
	OCTEP_PFVF_LINK_AUTONEG,
	OCTEP_PFVF_LINK_FIXED,
};

#define OCTEP_PFVF_MBOX_TIMEOUT_WAIT_COUNT  8000
#define OCTEP_PFVF_MBOX_TIMEOUT_WAIT_UDELAY 1000
#define OCTEP_PFVF_MBOX_MAX_RETRIES    2
#define OCTEP_PFVF_MBOX_VERSION        0
#define OCTEP_PFVF_MBOX_MAX_DATA_SIZE  6
#define OCTEP_PFVF_MBOX_MAX_DATA_BUF_SIZE 320
#define OCTEP_PFVF_MBOX_MORE_FRAG_FLAG 1

union octep_pfvf_mbox_word {
	u64 u64;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 rsvd:6;
		u64 data:48;
	} s;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 frag:1;
		u64 rsvd:5;
		u8 data[6];
	} s_data;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 rsvd:6;
		u64 version:48;
	} s_version;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 rsvd:6;
		u8 mac_addr[6];
	} s_set_mac;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 rsvd:6;
		u64 mtu:48;
	} s_set_mtu;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 state:1;
		u64 rsvd:53;
	} s_link_state;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 status:1;
		u64 rsvd:53;
	} s_link_status;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 pkind:8;
		u64 fsz:8;
		u64 rx_ol_flags:16;
		u64 tx_ol_flags:16;
		u64 rsvd:6;
	} s_fw_info;
	struct {
		u64 opcode:8;
		u64 type:2;
		u64 rsvd:22;
		u64 rx_ol_flags:16;
		u64 tx_ol_flags:16;
	} s_offloads;
} __packed;

int octep_vf_setup_mbox(struct octep_vf_device *oct);
void octep_vf_delete_mbox(struct octep_vf_device *oct);
int octep_vf_mbox_send_cmd(struct octep_vf_device *oct, union octep_pfvf_mbox_word cmd,
			   union octep_pfvf_mbox_word *rsp);
int octep_vf_mbox_bulk_read(struct octep_vf_device *oct, enum octep_pfvf_mbox_opcode opcode,
			    u8 *data, int *size);
int octep_vf_mbox_set_mtu(struct octep_vf_device *oct, int mtu);
int octep_vf_mbox_set_mac_addr(struct octep_vf_device *oct, char *mac_addr);
int octep_vf_mbox_get_mac_addr(struct octep_vf_device *oct, char *mac_addr);
int octep_vf_mbox_version_check(struct octep_vf_device *oct);
int octep_vf_mbox_set_rx_state(struct octep_vf_device *oct, bool state);
int octep_vf_mbox_set_link_status(struct octep_vf_device *oct, bool status);
int octep_vf_mbox_get_link_status(struct octep_vf_device *oct, u8 *oper_up);
int octep_vf_mbox_dev_remove(struct octep_vf_device *oct);
int octep_vf_mbox_get_fw_info(struct octep_vf_device *oct);
int octep_vf_mbox_set_offloads(struct octep_vf_device *oct, u16 tx_offloads, u16 rx_offloads);

#endif
