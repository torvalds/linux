/* SPDX-License-Identifier: GPL-2.0 */
/* Texas Instruments ICSSG Ethernet driver
 *
 * Copyright (C) 2022 Texas Instruments Incorporated - https://www.ti.com/
 *
 */

#ifndef __NET_TI_ICSSG_CONFIG_H
#define __NET_TI_ICSSG_CONFIG_H

struct icssg_buffer_pool_cfg {
	__le32	addr;
	__le32	len;
} __packed;

struct icssg_flow_cfg {
	__le16 rx_base_flow;
	__le16 mgm_base_flow;
} __packed;

#define PRUETH_PKT_TYPE_CMD	0x10
#define PRUETH_NAV_PS_DATA_SIZE	16	/* Protocol specific data size */
#define PRUETH_NAV_SW_DATA_SIZE	16	/* SW related data size */
#define PRUETH_MAX_TX_DESC	512
#define PRUETH_MAX_RX_DESC	512
#define PRUETH_MAX_RX_FLOWS	1	/* excluding default flow */
#define PRUETH_RX_FLOW_DATA	0

#define PRUETH_EMAC_BUF_POOL_SIZE	SZ_8K
#define PRUETH_EMAC_POOLS_PER_SLICE	24
#define PRUETH_EMAC_BUF_POOL_START	8
#define PRUETH_NUM_BUF_POOLS	8
#define PRUETH_EMAC_RX_CTX_BUF_SIZE	SZ_16K	/* per slice */
#define MSMC_RAM_SIZE	\
	(2 * (PRUETH_EMAC_BUF_POOL_SIZE * PRUETH_NUM_BUF_POOLS + \
	 PRUETH_EMAC_RX_CTX_BUF_SIZE * 2))

struct icssg_rxq_ctx {
	__le32 start[3];
	__le32 end;
} __packed;

/* Load time Fiwmware Configuration */

#define ICSSG_FW_MGMT_CMD_HEADER	0x81
#define ICSSG_FW_MGMT_FDB_CMD_TYPE	0x03
#define ICSSG_FW_MGMT_CMD_TYPE		0x04
#define ICSSG_FW_MGMT_PKT		0x80000000

struct icssg_r30_cmd {
	u32 cmd[4];
} __packed;

enum icssg_port_state_cmd {
	ICSSG_EMAC_PORT_DISABLE = 0,
	ICSSG_EMAC_PORT_BLOCK,
	ICSSG_EMAC_PORT_FORWARD,
	ICSSG_EMAC_PORT_FORWARD_WO_LEARNING,
	ICSSG_EMAC_PORT_ACCEPT_ALL,
	ICSSG_EMAC_PORT_ACCEPT_TAGGED,
	ICSSG_EMAC_PORT_ACCEPT_UNTAGGED_N_PRIO,
	ICSSG_EMAC_PORT_TAS_TRIGGER,
	ICSSG_EMAC_PORT_TAS_ENABLE,
	ICSSG_EMAC_PORT_TAS_RESET,
	ICSSG_EMAC_PORT_TAS_DISABLE,
	ICSSG_EMAC_PORT_UC_FLOODING_ENABLE,
	ICSSG_EMAC_PORT_UC_FLOODING_DISABLE,
	ICSSG_EMAC_PORT_MC_FLOODING_ENABLE,
	ICSSG_EMAC_PORT_MC_FLOODING_DISABLE,
	ICSSG_EMAC_PORT_PREMPT_TX_ENABLE,
	ICSSG_EMAC_PORT_PREMPT_TX_DISABLE,
	ICSSG_EMAC_PORT_VLAN_AWARE_ENABLE,
	ICSSG_EMAC_PORT_VLAN_AWARE_DISABLE,
	ICSSG_EMAC_PORT_MAX_COMMANDS
};

#define EMAC_NONE           0xffff0000
#define EMAC_PRU0_P_DI      0xffff0004
#define EMAC_PRU1_P_DI      0xffff0040
#define EMAC_TX_P_DI        0xffff0100

#define EMAC_PRU0_P_EN      0xfffb0000
#define EMAC_PRU1_P_EN      0xffbf0000
#define EMAC_TX_P_EN        0xfeff0000

#define EMAC_P_BLOCK        0xffff0040
#define EMAC_TX_P_BLOCK     0xffff0200
#define EMAC_P_UNBLOCK      0xffbf0000
#define EMAC_TX_P_UNBLOCK   0xfdff0000
#define EMAC_LEAN_EN        0xfff70000
#define EMAC_LEAN_DI        0xffff0008

#define EMAC_ACCEPT_ALL     0xffff0001
#define EMAC_ACCEPT_TAG     0xfffe0002
#define EMAC_ACCEPT_PRIOR   0xfffc0000

/* Config area lies in DRAM */
#define ICSSG_CONFIG_OFFSET	0x0

/* Config area lies in shared RAM */
#define ICSSG_CONFIG_OFFSET_SLICE0   0
#define ICSSG_CONFIG_OFFSET_SLICE1   0x8000

#define ICSSG_NUM_NORMAL_PDS	64
#define ICSSG_NUM_SPECIAL_PDS	16

#define ICSSG_NORMAL_PD_SIZE	8
#define ICSSG_SPECIAL_PD_SIZE	20

#define ICSSG_FLAG_MASK		0xff00ffff

/* SR1.0-specific bits */
#define PRUETH_MAX_RX_FLOWS_SR1			4	/* excluding default flow */
#define PRUETH_RX_FLOW_DATA_SR1			3       /* highest priority flow */
#define PRUETH_MAX_RX_MGM_DESC_SR1		8
#define PRUETH_MAX_RX_MGM_FLOWS_SR1		2	/* excluding default flow */
#define PRUETH_RX_MGM_FLOW_RESPONSE_SR1		0
#define PRUETH_RX_MGM_FLOW_TIMESTAMP_SR1	1

#define PRUETH_NUM_BUF_POOLS_SR1		16
#define PRUETH_EMAC_BUF_POOL_START_SR1		8
#define PRUETH_EMAC_BUF_POOL_MIN_SIZE_SR1	128
#define PRUETH_EMAC_BUF_SIZE_SR1		1536
#define PRUETH_EMAC_NUM_BUF_SR1			4
#define PRUETH_EMAC_BUF_POOL_SIZE_SR1	(PRUETH_EMAC_NUM_BUF_SR1 * \
					 PRUETH_EMAC_BUF_SIZE_SR1)
#define MSMC_RAM_SIZE_SR1	(SZ_64K + SZ_32K + SZ_2K) /* 0x1880 x 8 x 2 */

struct icssg_sr1_config {
	__le32 status;		/* Firmware status */
	__le32 addr_lo;		/* MSMC Buffer pool base address low. */
	__le32 addr_hi;		/* MSMC Buffer pool base address high. Must be 0 */
	__le32 tx_buf_sz[16];	/* Array of buffer pool sizes */
	__le32 num_tx_threads;	/* Number of active egress threads, 1 to 4 */
	__le32 tx_rate_lim_en;	/* Bitmask: Egress rate limit en per thread */
	__le32 rx_flow_id;	/* RX flow id for first rx ring */
	__le32 rx_mgr_flow_id;	/* RX flow id for the first management ring */
	__le32 flags;		/* TBD */
	__le32 n_burst;		/* for debug */
	__le32 rtu_status;	/* RTU status */
	__le32 info;		/* reserved */
	__le32 reserve;
	__le32 rand_seed;	/* Used for the random number generation at fw */
} __packed;

/* SR1.0 shutdown command to stop processing at firmware.
 * Command format: 0x8101ss00, where
 *	- ss: sequence number. Currently not used by driver.
 */
#define ICSSG_SHUTDOWN_CMD_SR1		0x81010000

/* SR1.0 pstate speed/duplex command to set speed and duplex settings
 * in firmware.
 * Command format: 0x8102ssPN, where
 *	- ss: sequence number. Currently not used by driver.
 *	- P: port number (for switch mode).
 *	- N: Speed/Duplex state:
 *		0x0 - 10Mbps/Half duplex;
 *		0x8 - 10Mbps/Full duplex;
 *		0x2 - 100Mbps/Half duplex;
 *		0xa - 100Mbps/Full duplex;
 *		0xc - 1Gbps/Full duplex;
 *		NOTE: The above are the same value as bits [3..1](slice 0)
 *		      or bits [7..5](slice 1) of RGMII CFG register.
 */
#define ICSSG_PSTATE_SPEED_DUPLEX_CMD_SR1	0x81020000

struct icssg_setclock_desc {
	u8 request;
	u8 restore;
	u8 acknowledgment;
	u8 cmp_status;
	u32 margin;
	u32 cyclecounter0_set;
	u32 cyclecounter1_set;
	u32 iepcount_set;
	u32 rsvd1;
	u32 rsvd2;
	u32 CMP0_current;
	u32 iepcount_current;
	u32 difference;
	u32 cyclecounter0_new;
	u32 cyclecounter1_new;
	u32 CMP0_new;
} __packed;

#define ICSSG_CMD_POP_SLICE0	56
#define ICSSG_CMD_POP_SLICE1	60

#define ICSSG_CMD_PUSH_SLICE0	57
#define ICSSG_CMD_PUSH_SLICE1	61

#define ICSSG_RSP_POP_SLICE0	58
#define ICSSG_RSP_POP_SLICE1	62

#define ICSSG_RSP_PUSH_SLICE0	56
#define ICSSG_RSP_PUSH_SLICE1	60

#define ICSSG_TS_POP_SLICE0	59
#define ICSSG_TS_POP_SLICE1	63

#define ICSSG_TS_PUSH_SLICE0	40
#define ICSSG_TS_PUSH_SLICE1	41

/* FDB FID_C2 flag definitions */
/* Indicates host port membership.*/
#define ICSSG_FDB_ENTRY_P0_MEMBERSHIP         BIT(0)
/* Indicates that MAC ID is connected to physical port 1 */
#define ICSSG_FDB_ENTRY_P1_MEMBERSHIP         BIT(1)
/* Indicates that MAC ID is connected to physical port 2 */
#define ICSSG_FDB_ENTRY_P2_MEMBERSHIP         BIT(2)
/* Ageable bit is set for learned entries and cleared for static entries */
#define ICSSG_FDB_ENTRY_AGEABLE               BIT(3)
/* If set for DA then packet is determined to be a special packet */
#define ICSSG_FDB_ENTRY_BLOCK                 BIT(4)
/* If set for DA then the SA from the packet is not learned */
#define ICSSG_FDB_ENTRY_SECURE                BIT(5)
/* If set, it means packet has been seen recently with source address + FID
 * matching MAC address/FID of entry
 */
#define ICSSG_FDB_ENTRY_TOUCHED               BIT(6)
/* Set if entry is valid */
#define ICSSG_FDB_ENTRY_VALID                 BIT(7)

/**
 * struct prueth_vlan_tbl - VLAN table entries struct in ICSSG SMEM
 * @fid_c1: membership and forwarding rules flag to this table. See
 *          above to defines for bit definitions
 * @fid: FDB index for this VID (there is 1-1 mapping b/w VID and FID)
 */
struct prueth_vlan_tbl {
	u8 fid_c1;
	u8 fid;
} __packed;

/**
 * struct prueth_fdb_slot - Result of FDB slot lookup
 * @mac: MAC address
 * @fid: fid to be associated with MAC
 * @fid_c2: FID_C2 entry for this MAC
 */
struct prueth_fdb_slot {
	u8 mac[ETH_ALEN];
	u8 fid;
	u8 fid_c2;
} __packed;

enum icssg_ietfpe_verify_states {
	ICSSG_IETFPE_STATE_UNKNOWN = 0,
	ICSSG_IETFPE_STATE_INITIAL,
	ICSSG_IETFPE_STATE_VERIFYING,
	ICSSG_IETFPE_STATE_SUCCEEDED,
	ICSSG_IETFPE_STATE_FAILED,
	ICSSG_IETFPE_STATE_DISABLED
};
#endif /* __NET_TI_ICSSG_CONFIG_H */
