// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HCLGE_MAIN_H
#define __HCLGE_MAIN_H
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>

#include "hclge_cmd.h"
#include "hnae3.h"

#define HCLGE_MOD_VERSION "1.0"
#define HCLGE_DRIVER_NAME "hclge"

#define HCLGE_MAX_PF_NUM		8

#define HCLGE_INVALID_VPORT 0xffff

#define HCLGE_PF_CFG_BLOCK_SIZE		32
#define HCLGE_PF_CFG_DESC_NUM \
	(HCLGE_PF_CFG_BLOCK_SIZE / HCLGE_CFG_RD_LEN_BYTES)

#define HCLGE_VECTOR_REG_BASE		0x20000
#define HCLGE_MISC_VECTOR_REG_BASE	0x20400

#define HCLGE_VECTOR_REG_OFFSET		0x4
#define HCLGE_VECTOR_VF_OFFSET		0x100000

#define HCLGE_RSS_IND_TBL_SIZE		512
#define HCLGE_RSS_SET_BITMAP_MSK	GENMASK(15, 0)
#define HCLGE_RSS_KEY_SIZE		40
#define HCLGE_RSS_HASH_ALGO_TOEPLITZ	0
#define HCLGE_RSS_HASH_ALGO_SIMPLE	1
#define HCLGE_RSS_HASH_ALGO_SYMMETRIC	2
#define HCLGE_RSS_HASH_ALGO_MASK	GENMASK(3, 0)
#define HCLGE_RSS_CFG_TBL_NUM \
	(HCLGE_RSS_IND_TBL_SIZE / HCLGE_RSS_CFG_TBL_SIZE)

#define HCLGE_RSS_INPUT_TUPLE_OTHER	GENMASK(3, 0)
#define HCLGE_RSS_INPUT_TUPLE_SCTP	GENMASK(4, 0)
#define HCLGE_D_PORT_BIT		BIT(0)
#define HCLGE_S_PORT_BIT		BIT(1)
#define HCLGE_D_IP_BIT			BIT(2)
#define HCLGE_S_IP_BIT			BIT(3)
#define HCLGE_V_TAG_BIT			BIT(4)

#define HCLGE_RSS_TC_SIZE_0		1
#define HCLGE_RSS_TC_SIZE_1		2
#define HCLGE_RSS_TC_SIZE_2		4
#define HCLGE_RSS_TC_SIZE_3		8
#define HCLGE_RSS_TC_SIZE_4		16
#define HCLGE_RSS_TC_SIZE_5		32
#define HCLGE_RSS_TC_SIZE_6		64
#define HCLGE_RSS_TC_SIZE_7		128

#define HCLGE_UMV_TBL_SIZE		3072
#define HCLGE_DEFAULT_UMV_SPACE_PER_PF \
	(HCLGE_UMV_TBL_SIZE / HCLGE_MAX_PF_NUM)

#define HCLGE_TQP_RESET_TRY_TIMES	10

#define HCLGE_PHY_PAGE_MDIX		0
#define HCLGE_PHY_PAGE_COPPER		0

/* Page Selection Reg. */
#define HCLGE_PHY_PAGE_REG		22

/* Copper Specific Control Register */
#define HCLGE_PHY_CSC_REG		16

/* Copper Specific Status Register */
#define HCLGE_PHY_CSS_REG		17

#define HCLGE_PHY_MDIX_CTRL_S		5
#define HCLGE_PHY_MDIX_CTRL_M		GENMASK(6, 5)

#define HCLGE_PHY_MDIX_STATUS_B		6
#define HCLGE_PHY_SPEED_DUP_RESOLVE_B	11

/* Factor used to calculate offset and bitmap of VF num */
#define HCLGE_VF_NUM_PER_CMD           64
#define HCLGE_VF_NUM_PER_BYTE          8

enum HLCGE_PORT_TYPE {
	HOST_PORT,
	NETWORK_PORT
};

#define HCLGE_PF_ID_S			0
#define HCLGE_PF_ID_M			GENMASK(2, 0)
#define HCLGE_VF_ID_S			3
#define HCLGE_VF_ID_M			GENMASK(10, 3)
#define HCLGE_PORT_TYPE_B		11
#define HCLGE_NETWORK_PORT_ID_S		0
#define HCLGE_NETWORK_PORT_ID_M		GENMASK(3, 0)

/* Reset related Registers */
#define HCLGE_PF_OTHER_INT_REG		0x20600
#define HCLGE_MISC_RESET_STS_REG	0x20700
#define HCLGE_MISC_VECTOR_INT_STS	0x20800
#define HCLGE_GLOBAL_RESET_REG		0x20A00
#define HCLGE_GLOBAL_RESET_BIT		0
#define HCLGE_CORE_RESET_BIT		1
#define HCLGE_IMP_RESET_BIT		2
#define HCLGE_FUN_RST_ING		0x20C00
#define HCLGE_FUN_RST_ING_B		0

/* Vector0 register bits define */
#define HCLGE_VECTOR0_GLOBALRESET_INT_B	5
#define HCLGE_VECTOR0_CORERESET_INT_B	6
#define HCLGE_VECTOR0_IMPRESET_INT_B	7

/* Vector0 interrupt CMDQ event source register(RW) */
#define HCLGE_VECTOR0_CMDQ_SRC_REG	0x27100
/* CMDQ register bits for RX event(=MBX event) */
#define HCLGE_VECTOR0_RX_CMDQ_INT_B	1

#define HCLGE_VECTOR0_IMP_RESET_INT_B	1

#define HCLGE_MAC_DEFAULT_FRAME \
	(ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN + ETH_DATA_LEN)
#define HCLGE_MAC_MIN_FRAME		64
#define HCLGE_MAC_MAX_FRAME		9728

#define HCLGE_SUPPORT_1G_BIT		BIT(0)
#define HCLGE_SUPPORT_10G_BIT		BIT(1)
#define HCLGE_SUPPORT_25G_BIT		BIT(2)
#define HCLGE_SUPPORT_50G_BIT		BIT(3)
#define HCLGE_SUPPORT_100G_BIT		BIT(4)

enum HCLGE_DEV_STATE {
	HCLGE_STATE_REINITING,
	HCLGE_STATE_DOWN,
	HCLGE_STATE_DISABLED,
	HCLGE_STATE_REMOVING,
	HCLGE_STATE_SERVICE_INITED,
	HCLGE_STATE_SERVICE_SCHED,
	HCLGE_STATE_RST_SERVICE_SCHED,
	HCLGE_STATE_RST_HANDLING,
	HCLGE_STATE_MBX_SERVICE_SCHED,
	HCLGE_STATE_MBX_HANDLING,
	HCLGE_STATE_STATISTICS_UPDATING,
	HCLGE_STATE_CMD_DISABLE,
	HCLGE_STATE_MAX
};

enum hclge_evt_cause {
	HCLGE_VECTOR0_EVENT_RST,
	HCLGE_VECTOR0_EVENT_MBX,
	HCLGE_VECTOR0_EVENT_OTHER,
};

#define HCLGE_MPF_ENBALE 1

enum HCLGE_MAC_SPEED {
	HCLGE_MAC_SPEED_10M	= 10,		/* 10 Mbps */
	HCLGE_MAC_SPEED_100M	= 100,		/* 100 Mbps */
	HCLGE_MAC_SPEED_1G	= 1000,		/* 1000 Mbps   = 1 Gbps */
	HCLGE_MAC_SPEED_10G	= 10000,	/* 10000 Mbps  = 10 Gbps */
	HCLGE_MAC_SPEED_25G	= 25000,	/* 25000 Mbps  = 25 Gbps */
	HCLGE_MAC_SPEED_40G	= 40000,	/* 40000 Mbps  = 40 Gbps */
	HCLGE_MAC_SPEED_50G	= 50000,	/* 50000 Mbps  = 50 Gbps */
	HCLGE_MAC_SPEED_100G	= 100000	/* 100000 Mbps = 100 Gbps */
};

enum HCLGE_MAC_DUPLEX {
	HCLGE_MAC_HALF,
	HCLGE_MAC_FULL
};

struct hclge_mac {
	u8 phy_addr;
	u8 flag;
	u8 media_type;
	u8 mac_addr[ETH_ALEN];
	u8 autoneg;
	u8 duplex;
	u32 speed;
	int link;	/* store the link status of mac & phy (if phy exit)*/
	struct phy_device *phydev;
	struct mii_bus *mdio_bus;
	phy_interface_t phy_if;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
};

struct hclge_hw {
	void __iomem *io_base;
	struct hclge_mac mac;
	int num_vec;
	struct hclge_cmq cmq;
};

/* TQP stats */
struct hlcge_tqp_stats {
	/* query_tqp_tx_queue_statistics ,opcode id:  0x0B03 */
	u64 rcb_tx_ring_pktnum_rcd; /* 32bit */
	/* query_tqp_rx_queue_statistics ,opcode id:  0x0B13 */
	u64 rcb_rx_ring_pktnum_rcd; /* 32bit */
};

struct hclge_tqp {
	/* copy of device pointer from pci_dev,
	 * used when perform DMA mapping
	 */
	struct device *dev;
	struct hnae3_queue q;
	struct hlcge_tqp_stats tqp_stats;
	u16 index;	/* Global index in a NIC controller */

	bool alloced;
};

enum hclge_fc_mode {
	HCLGE_FC_NONE,
	HCLGE_FC_RX_PAUSE,
	HCLGE_FC_TX_PAUSE,
	HCLGE_FC_FULL,
	HCLGE_FC_PFC,
	HCLGE_FC_DEFAULT
};

#define HCLGE_PG_NUM		4
#define HCLGE_SCH_MODE_SP	0
#define HCLGE_SCH_MODE_DWRR	1
struct hclge_pg_info {
	u8 pg_id;
	u8 pg_sch_mode;		/* 0: sp; 1: dwrr */
	u8 tc_bit_map;
	u32 bw_limit;
	u8 tc_dwrr[HNAE3_MAX_TC];
};

struct hclge_tc_info {
	u8 tc_id;
	u8 tc_sch_mode;		/* 0: sp; 1: dwrr */
	u8 pgid;
	u32 bw_limit;
};

struct hclge_cfg {
	u8 vmdq_vport_num;
	u8 tc_num;
	u16 tqp_desc_num;
	u16 rx_buf_len;
	u16 rss_size_max;
	u8 phy_addr;
	u8 media_type;
	u8 mac_addr[ETH_ALEN];
	u8 default_speed;
	u32 numa_node_map;
	u8 speed_ability;
	u16 umv_space;
};

struct hclge_tm_info {
	u8 num_tc;
	u8 num_pg;      /* It must be 1 if vNET-Base schd */
	u8 pg_dwrr[HCLGE_PG_NUM];
	u8 prio_tc[HNAE3_MAX_USER_PRIO];
	struct hclge_pg_info pg_info[HCLGE_PG_NUM];
	struct hclge_tc_info tc_info[HNAE3_MAX_TC];
	enum hclge_fc_mode fc_mode;
	u8 hw_pfc_map; /* Allow for packet drop or not on this TC */
};

struct hclge_comm_stats_str {
	char desc[ETH_GSTRING_LEN];
	unsigned long offset;
};

/* mac stats ,opcode id: 0x0032 */
struct hclge_mac_stats {
	u64 mac_tx_mac_pause_num;
	u64 mac_rx_mac_pause_num;
	u64 mac_tx_pfc_pri0_pkt_num;
	u64 mac_tx_pfc_pri1_pkt_num;
	u64 mac_tx_pfc_pri2_pkt_num;
	u64 mac_tx_pfc_pri3_pkt_num;
	u64 mac_tx_pfc_pri4_pkt_num;
	u64 mac_tx_pfc_pri5_pkt_num;
	u64 mac_tx_pfc_pri6_pkt_num;
	u64 mac_tx_pfc_pri7_pkt_num;
	u64 mac_rx_pfc_pri0_pkt_num;
	u64 mac_rx_pfc_pri1_pkt_num;
	u64 mac_rx_pfc_pri2_pkt_num;
	u64 mac_rx_pfc_pri3_pkt_num;
	u64 mac_rx_pfc_pri4_pkt_num;
	u64 mac_rx_pfc_pri5_pkt_num;
	u64 mac_rx_pfc_pri6_pkt_num;
	u64 mac_rx_pfc_pri7_pkt_num;
	u64 mac_tx_total_pkt_num;
	u64 mac_tx_total_oct_num;
	u64 mac_tx_good_pkt_num;
	u64 mac_tx_bad_pkt_num;
	u64 mac_tx_good_oct_num;
	u64 mac_tx_bad_oct_num;
	u64 mac_tx_uni_pkt_num;
	u64 mac_tx_multi_pkt_num;
	u64 mac_tx_broad_pkt_num;
	u64 mac_tx_undersize_pkt_num;
	u64 mac_tx_oversize_pkt_num;
	u64 mac_tx_64_oct_pkt_num;
	u64 mac_tx_65_127_oct_pkt_num;
	u64 mac_tx_128_255_oct_pkt_num;
	u64 mac_tx_256_511_oct_pkt_num;
	u64 mac_tx_512_1023_oct_pkt_num;
	u64 mac_tx_1024_1518_oct_pkt_num;
	u64 mac_tx_1519_2047_oct_pkt_num;
	u64 mac_tx_2048_4095_oct_pkt_num;
	u64 mac_tx_4096_8191_oct_pkt_num;
	u64 rsv0;
	u64 mac_tx_8192_9216_oct_pkt_num;
	u64 mac_tx_9217_12287_oct_pkt_num;
	u64 mac_tx_12288_16383_oct_pkt_num;
	u64 mac_tx_1519_max_good_oct_pkt_num;
	u64 mac_tx_1519_max_bad_oct_pkt_num;

	u64 mac_rx_total_pkt_num;
	u64 mac_rx_total_oct_num;
	u64 mac_rx_good_pkt_num;
	u64 mac_rx_bad_pkt_num;
	u64 mac_rx_good_oct_num;
	u64 mac_rx_bad_oct_num;
	u64 mac_rx_uni_pkt_num;
	u64 mac_rx_multi_pkt_num;
	u64 mac_rx_broad_pkt_num;
	u64 mac_rx_undersize_pkt_num;
	u64 mac_rx_oversize_pkt_num;
	u64 mac_rx_64_oct_pkt_num;
	u64 mac_rx_65_127_oct_pkt_num;
	u64 mac_rx_128_255_oct_pkt_num;
	u64 mac_rx_256_511_oct_pkt_num;
	u64 mac_rx_512_1023_oct_pkt_num;
	u64 mac_rx_1024_1518_oct_pkt_num;
	u64 mac_rx_1519_2047_oct_pkt_num;
	u64 mac_rx_2048_4095_oct_pkt_num;
	u64 mac_rx_4096_8191_oct_pkt_num;
	u64 rsv1;
	u64 mac_rx_8192_9216_oct_pkt_num;
	u64 mac_rx_9217_12287_oct_pkt_num;
	u64 mac_rx_12288_16383_oct_pkt_num;
	u64 mac_rx_1519_max_good_oct_pkt_num;
	u64 mac_rx_1519_max_bad_oct_pkt_num;

	u64 mac_tx_fragment_pkt_num;
	u64 mac_tx_undermin_pkt_num;
	u64 mac_tx_jabber_pkt_num;
	u64 mac_tx_err_all_pkt_num;
	u64 mac_tx_from_app_good_pkt_num;
	u64 mac_tx_from_app_bad_pkt_num;
	u64 mac_rx_fragment_pkt_num;
	u64 mac_rx_undermin_pkt_num;
	u64 mac_rx_jabber_pkt_num;
	u64 mac_rx_fcs_err_pkt_num;
	u64 mac_rx_send_app_good_pkt_num;
	u64 mac_rx_send_app_bad_pkt_num;
};

#define HCLGE_STATS_TIMER_INTERVAL	(60 * 5)
struct hclge_hw_stats {
	struct hclge_mac_stats      mac_stats;
	u32 stats_timer;
};

struct hclge_vlan_type_cfg {
	u16 rx_ot_fst_vlan_type;
	u16 rx_ot_sec_vlan_type;
	u16 rx_in_fst_vlan_type;
	u16 rx_in_sec_vlan_type;
	u16 tx_ot_vlan_type;
	u16 tx_in_vlan_type;
};

enum HCLGE_FD_MODE {
	HCLGE_FD_MODE_DEPTH_2K_WIDTH_400B_STAGE_1,
	HCLGE_FD_MODE_DEPTH_1K_WIDTH_400B_STAGE_2,
	HCLGE_FD_MODE_DEPTH_4K_WIDTH_200B_STAGE_1,
	HCLGE_FD_MODE_DEPTH_2K_WIDTH_200B_STAGE_2,
};

enum HCLGE_FD_KEY_TYPE {
	HCLGE_FD_KEY_BASE_ON_PTYPE,
	HCLGE_FD_KEY_BASE_ON_TUPLE,
};

enum HCLGE_FD_STAGE {
	HCLGE_FD_STAGE_1,
	HCLGE_FD_STAGE_2,
};

/* OUTER_XXX indicates tuples in tunnel header of tunnel packet
 * INNER_XXX indicate tuples in tunneled header of tunnel packet or
 *           tuples of non-tunnel packet
 */
enum HCLGE_FD_TUPLE {
	OUTER_DST_MAC,
	OUTER_SRC_MAC,
	OUTER_VLAN_TAG_FST,
	OUTER_VLAN_TAG_SEC,
	OUTER_ETH_TYPE,
	OUTER_L2_RSV,
	OUTER_IP_TOS,
	OUTER_IP_PROTO,
	OUTER_SRC_IP,
	OUTER_DST_IP,
	OUTER_L3_RSV,
	OUTER_SRC_PORT,
	OUTER_DST_PORT,
	OUTER_L4_RSV,
	OUTER_TUN_VNI,
	OUTER_TUN_FLOW_ID,
	INNER_DST_MAC,
	INNER_SRC_MAC,
	INNER_VLAN_TAG_FST,
	INNER_VLAN_TAG_SEC,
	INNER_ETH_TYPE,
	INNER_L2_RSV,
	INNER_IP_TOS,
	INNER_IP_PROTO,
	INNER_SRC_IP,
	INNER_DST_IP,
	INNER_L3_RSV,
	INNER_SRC_PORT,
	INNER_DST_PORT,
	INNER_L4_RSV,
	MAX_TUPLE,
};

enum HCLGE_FD_META_DATA {
	PACKET_TYPE_ID,
	IP_FRAGEMENT,
	ROCE_TYPE,
	NEXT_KEY,
	VLAN_NUMBER,
	SRC_VPORT,
	DST_VPORT,
	TUNNEL_PACKET,
	MAX_META_DATA,
};

struct key_info {
	u8 key_type;
	u8 key_length;
};

static const struct key_info meta_data_key_info[] = {
	{ PACKET_TYPE_ID, 6},
	{ IP_FRAGEMENT, 1},
	{ ROCE_TYPE, 1},
	{ NEXT_KEY, 5},
	{ VLAN_NUMBER, 2},
	{ SRC_VPORT, 12},
	{ DST_VPORT, 12},
	{ TUNNEL_PACKET, 1},
};

static const struct key_info tuple_key_info[] = {
	{ OUTER_DST_MAC, 48},
	{ OUTER_SRC_MAC, 48},
	{ OUTER_VLAN_TAG_FST, 16},
	{ OUTER_VLAN_TAG_SEC, 16},
	{ OUTER_ETH_TYPE, 16},
	{ OUTER_L2_RSV, 16},
	{ OUTER_IP_TOS, 8},
	{ OUTER_IP_PROTO, 8},
	{ OUTER_SRC_IP, 32},
	{ OUTER_DST_IP, 32},
	{ OUTER_L3_RSV, 16},
	{ OUTER_SRC_PORT, 16},
	{ OUTER_DST_PORT, 16},
	{ OUTER_L4_RSV, 32},
	{ OUTER_TUN_VNI, 24},
	{ OUTER_TUN_FLOW_ID, 8},
	{ INNER_DST_MAC, 48},
	{ INNER_SRC_MAC, 48},
	{ INNER_VLAN_TAG_FST, 16},
	{ INNER_VLAN_TAG_SEC, 16},
	{ INNER_ETH_TYPE, 16},
	{ INNER_L2_RSV, 16},
	{ INNER_IP_TOS, 8},
	{ INNER_IP_PROTO, 8},
	{ INNER_SRC_IP, 32},
	{ INNER_DST_IP, 32},
	{ INNER_L3_RSV, 16},
	{ INNER_SRC_PORT, 16},
	{ INNER_DST_PORT, 16},
	{ INNER_L4_RSV, 32},
};

#define MAX_KEY_LENGTH	400
#define MAX_KEY_DWORDS	DIV_ROUND_UP(MAX_KEY_LENGTH / 8, 4)
#define MAX_KEY_BYTES	(MAX_KEY_DWORDS * 4)
#define MAX_META_DATA_LENGTH	32

enum HCLGE_FD_PACKET_TYPE {
	NIC_PACKET,
	ROCE_PACKET,
};

enum HCLGE_FD_ACTION {
	HCLGE_FD_ACTION_ACCEPT_PACKET,
	HCLGE_FD_ACTION_DROP_PACKET,
};

struct hclge_fd_key_cfg {
	u8 key_sel;
	u8 inner_sipv6_word_en;
	u8 inner_dipv6_word_en;
	u8 outer_sipv6_word_en;
	u8 outer_dipv6_word_en;
	u32 tuple_active;
	u32 meta_data_active;
};

struct hclge_fd_cfg {
	u8 fd_mode;
	u8 fd_en;
	u16 max_key_length;
	u32 proto_support;
	u32 rule_num[2]; /* rule entry number */
	u16 cnt_num[2]; /* rule hit counter number */
	struct hclge_fd_key_cfg key_cfg[2];
};

struct hclge_fd_rule_tuples {
	u8 src_mac[6];
	u8 dst_mac[6];
	u32 src_ip[4];
	u32 dst_ip[4];
	u16 src_port;
	u16 dst_port;
	u16 vlan_tag1;
	u16 ether_proto;
	u8 ip_tos;
	u8 ip_proto;
};

struct hclge_fd_rule {
	struct hlist_node rule_node;
	struct hclge_fd_rule_tuples tuples;
	struct hclge_fd_rule_tuples tuples_mask;
	u32 unused_tuple;
	u32 flow_type;
	u8 action;
	u16 vf_id;
	u16 queue_id;
	u16 location;
};

struct hclge_fd_ad_data {
	u16 ad_id;
	u8 drop_packet;
	u8 forward_to_direct_queue;
	u16 queue_id;
	u8 use_counter;
	u8 counter_id;
	u8 use_next_stage;
	u8 write_rule_id_to_bd;
	u8 next_input_key;
	u16 rule_id;
};

/* For each bit of TCAM entry, it uses a pair of 'x' and
 * 'y' to indicate which value to match, like below:
 * ----------------------------------
 * | bit x | bit y |  search value  |
 * ----------------------------------
 * |   0   |   0   |   always hit   |
 * ----------------------------------
 * |   1   |   0   |   match '0'    |
 * ----------------------------------
 * |   0   |   1   |   match '1'    |
 * ----------------------------------
 * |   1   |   1   |   invalid      |
 * ----------------------------------
 * Then for input key(k) and mask(v), we can calculate the value by
 * the formulae:
 *	x = (~k) & v
 *	y = (k ^ ~v) & k
 */
#define calc_x(x, k, v) ((x) = (~(k) & (v)))
#define calc_y(y, k, v) \
	do { \
		const typeof(k) _k_ = (k); \
		const typeof(v) _v_ = (v); \
		(y) = (_k_ ^ ~_v_) & (_k_); \
	} while (0)

#define HCLGE_VPORT_NUM 256
struct hclge_dev {
	struct pci_dev *pdev;
	struct hnae3_ae_dev *ae_dev;
	struct hclge_hw hw;
	struct hclge_misc_vector misc_vector;
	struct hclge_hw_stats hw_stats;
	unsigned long state;
	unsigned long flr_state;
	unsigned long last_reset_time;

	enum hnae3_reset_type reset_type;
	enum hnae3_reset_type reset_level;
	unsigned long default_reset_request;
	unsigned long reset_request;	/* reset has been requested */
	unsigned long reset_pending;	/* client rst is pending to be served */
	unsigned long reset_count;	/* the number of reset has been done */
	u32 reset_fail_cnt;
	u32 fw_version;
	u16 num_vmdq_vport;		/* Num vmdq vport this PF has set up */
	u16 num_tqps;			/* Num task queue pairs of this PF */
	u16 num_req_vfs;		/* Num VFs requested for this PF */

	u16 base_tqp_pid;	/* Base task tqp physical id of this PF */
	u16 alloc_rss_size;		/* Allocated RSS task queue */
	u16 rss_size_max;		/* HW defined max RSS task queue */

	u16 fdir_pf_filter_count; /* Num of guaranteed filters for this PF */
	u16 num_alloc_vport;		/* Num vports this driver supports */
	u32 numa_node_mask;
	u16 rx_buf_len;
	u16 num_desc;
	u8 hw_tc_map;
	u8 tc_num_last_time;
	enum hclge_fc_mode fc_mode_last_time;

#define HCLGE_FLAG_TC_BASE_SCH_MODE		1
#define HCLGE_FLAG_VNET_BASE_SCH_MODE		2
	u8 tx_sch_mode;
	u8 tc_max;
	u8 pfc_max;

	u8 default_up;
	u8 dcbx_cap;
	struct hclge_tm_info tm_info;

	u16 num_msi;
	u16 num_msi_left;
	u16 num_msi_used;
	u16 roce_base_msix_offset;
	u32 base_msi_vector;
	u16 *vector_status;
	int *vector_irq;
	u16 num_roce_msi;	/* Num of roce vectors for this PF */
	int roce_base_vector;

	u16 pending_udp_bitmap;

	u16 rx_itr_default;
	u16 tx_itr_default;

	u16 adminq_work_limit; /* Num of admin receive queue desc to process */
	unsigned long service_timer_period;
	unsigned long service_timer_previous;
	struct timer_list service_timer;
	struct timer_list reset_timer;
	struct work_struct service_task;
	struct work_struct rst_service_task;
	struct work_struct mbx_service_task;

	bool cur_promisc;
	int num_alloc_vfs;	/* Actual number of VFs allocated */

	struct hclge_tqp *htqp;
	struct hclge_vport *vport;

	struct dentry *hclge_dbgfs;

	struct hnae3_client *nic_client;
	struct hnae3_client *roce_client;

#define HCLGE_FLAG_MAIN			BIT(0)
#define HCLGE_FLAG_DCB_CAPABLE		BIT(1)
#define HCLGE_FLAG_DCB_ENABLE		BIT(2)
#define HCLGE_FLAG_MQPRIO_ENABLE	BIT(3)
	u32 flag;

	u32 pkt_buf_size; /* Total pf buf size for tx/rx */
	u32 mps; /* Max packet size */

	struct hclge_vlan_type_cfg vlan_type_cfg;

	unsigned long vlan_table[VLAN_N_VID][BITS_TO_LONGS(HCLGE_VPORT_NUM)];

	struct hclge_fd_cfg fd_cfg;
	struct hlist_head fd_rule_list;
	u16 hclge_fd_rule_num;

	u16 wanted_umv_size;
	/* max available unicast mac vlan space */
	u16 max_umv_size;
	/* private unicast mac vlan space, it's same for PF and its VFs */
	u16 priv_umv_size;
	/* unicast mac vlan space shared by PF and its VFs */
	u16 share_umv_size;
	struct mutex umv_mutex; /* protect share_umv_size */
};

/* VPort level vlan tag configuration for TX direction */
struct hclge_tx_vtag_cfg {
	bool accept_tag1;	/* Whether accept tag1 packet from host */
	bool accept_untag1;	/* Whether accept untag1 packet from host */
	bool accept_tag2;
	bool accept_untag2;
	bool insert_tag1_en;	/* Whether insert inner vlan tag */
	bool insert_tag2_en;	/* Whether insert outer vlan tag */
	u16  default_tag1;	/* The default inner vlan tag to insert */
	u16  default_tag2;	/* The default outer vlan tag to insert */
};

/* VPort level vlan tag configuration for RX direction */
struct hclge_rx_vtag_cfg {
	bool strip_tag1_en;	/* Whether strip inner vlan tag */
	bool strip_tag2_en;	/* Whether strip outer vlan tag */
	bool vlan1_vlan_prionly;/* Inner VLAN Tag up to descriptor Enable */
	bool vlan2_vlan_prionly;/* Outer VLAN Tag up to descriptor Enable */
};

struct hclge_rss_tuple_cfg {
	u8 ipv4_tcp_en;
	u8 ipv4_udp_en;
	u8 ipv4_sctp_en;
	u8 ipv4_fragment_en;
	u8 ipv6_tcp_en;
	u8 ipv6_udp_en;
	u8 ipv6_sctp_en;
	u8 ipv6_fragment_en;
};

struct hclge_vport {
	u16 alloc_tqps;	/* Allocated Tx/Rx queues */

	u8  rss_hash_key[HCLGE_RSS_KEY_SIZE]; /* User configured hash keys */
	/* User configured lookup table entries */
	u8  rss_indirection_tbl[HCLGE_RSS_IND_TBL_SIZE];
	int rss_algo;		/* User configured hash algorithm */
	/* User configured rss tuple sets */
	struct hclge_rss_tuple_cfg rss_tuple_sets;

	u16 alloc_rss_size;

	u16 qs_offset;
	u16 bw_limit;		/* VSI BW Limit (0 = disabled) */
	u8  dwrr;

	struct hclge_tx_vtag_cfg  txvlan_cfg;
	struct hclge_rx_vtag_cfg  rxvlan_cfg;

	u16 used_umv_num;

	int vport_id;
	struct hclge_dev *back;  /* Back reference to associated dev */
	struct hnae3_handle nic;
	struct hnae3_handle roce;
};

void hclge_promisc_param_init(struct hclge_promisc_param *param, bool en_uc,
			      bool en_mc, bool en_bc, int vport_id);

int hclge_add_uc_addr_common(struct hclge_vport *vport,
			     const unsigned char *addr);
int hclge_rm_uc_addr_common(struct hclge_vport *vport,
			    const unsigned char *addr);
int hclge_add_mc_addr_common(struct hclge_vport *vport,
			     const unsigned char *addr);
int hclge_rm_mc_addr_common(struct hclge_vport *vport,
			    const unsigned char *addr);

struct hclge_vport *hclge_get_vport(struct hnae3_handle *handle);
int hclge_bind_ring_with_vector(struct hclge_vport *vport,
				int vector_id, bool en,
				struct hnae3_ring_chain_node *ring_chain);

static inline int hclge_get_queue_id(struct hnae3_queue *queue)
{
	struct hclge_tqp *tqp = container_of(queue, struct hclge_tqp, q);

	return tqp->index;
}

static inline bool hclge_is_reset_pending(struct hclge_dev *hdev)
{
	return !!hdev->reset_pending;
}

int hclge_inform_reset_assert_to_vf(struct hclge_vport *vport);
int hclge_cfg_mac_speed_dup(struct hclge_dev *hdev, int speed, u8 duplex);
int hclge_set_vlan_filter(struct hnae3_handle *handle, __be16 proto,
			  u16 vlan_id, bool is_kill);
int hclge_en_hw_strip_rxvtag(struct hnae3_handle *handle, bool enable);

int hclge_buffer_alloc(struct hclge_dev *hdev);
int hclge_rss_init_hw(struct hclge_dev *hdev);
void hclge_rss_indir_init_cfg(struct hclge_dev *hdev);

int hclge_inform_reset_assert_to_vf(struct hclge_vport *vport);
void hclge_mbx_handler(struct hclge_dev *hdev);
int hclge_reset_tqp(struct hnae3_handle *handle, u16 queue_id);
void hclge_reset_vf_queue(struct hclge_vport *vport, u16 queue_id);
int hclge_cfg_flowctrl(struct hclge_dev *hdev);
int hclge_func_reset_cmd(struct hclge_dev *hdev, int func_id);
#endif
