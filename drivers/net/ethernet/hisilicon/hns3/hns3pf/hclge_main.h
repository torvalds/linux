/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HCLGE_MAIN_H
#define __HCLGE_MAIN_H
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>
#include <linux/kfifo.h>

#include "hclge_cmd.h"
#include "hclge_ptp.h"
#include "hnae3.h"

#define HCLGE_MOD_VERSION "1.0"
#define HCLGE_DRIVER_NAME "hclge"

#define HCLGE_MAX_PF_NUM		8

#define HCLGE_VF_VPORT_START_NUM	1

#define HCLGE_RD_FIRST_STATS_NUM        2
#define HCLGE_RD_OTHER_STATS_NUM        4

#define HCLGE_INVALID_VPORT 0xffff

#define HCLGE_PF_CFG_BLOCK_SIZE		32
#define HCLGE_PF_CFG_DESC_NUM \
	(HCLGE_PF_CFG_BLOCK_SIZE / HCLGE_CFG_RD_LEN_BYTES)

#define HCLGE_VECTOR_REG_BASE		0x20000
#define HCLGE_VECTOR_EXT_REG_BASE	0x30000
#define HCLGE_MISC_VECTOR_REG_BASE	0x20400

#define HCLGE_VECTOR_REG_OFFSET		0x4
#define HCLGE_VECTOR_REG_OFFSET_H	0x1000
#define HCLGE_VECTOR_VF_OFFSET		0x100000

#define HCLGE_CMDQ_TX_ADDR_L_REG	0x27000
#define HCLGE_CMDQ_TX_ADDR_H_REG	0x27004
#define HCLGE_CMDQ_TX_DEPTH_REG		0x27008
#define HCLGE_CMDQ_TX_TAIL_REG		0x27010
#define HCLGE_CMDQ_TX_HEAD_REG		0x27014
#define HCLGE_CMDQ_RX_ADDR_L_REG	0x27018
#define HCLGE_CMDQ_RX_ADDR_H_REG	0x2701C
#define HCLGE_CMDQ_RX_DEPTH_REG		0x27020
#define HCLGE_CMDQ_RX_TAIL_REG		0x27024
#define HCLGE_CMDQ_RX_HEAD_REG		0x27028
#define HCLGE_CMDQ_INTR_STS_REG		0x27104
#define HCLGE_CMDQ_INTR_EN_REG		0x27108
#define HCLGE_CMDQ_INTR_GEN_REG		0x2710C

/* bar registers for common func */
#define HCLGE_VECTOR0_OTER_EN_REG	0x20600
#define HCLGE_GRO_EN_REG		0x28000
#define HCLGE_RXD_ADV_LAYOUT_EN_REG	0x28008

/* bar registers for rcb */
#define HCLGE_RING_RX_ADDR_L_REG	0x80000
#define HCLGE_RING_RX_ADDR_H_REG	0x80004
#define HCLGE_RING_RX_BD_NUM_REG	0x80008
#define HCLGE_RING_RX_BD_LENGTH_REG	0x8000C
#define HCLGE_RING_RX_MERGE_EN_REG	0x80014
#define HCLGE_RING_RX_TAIL_REG		0x80018
#define HCLGE_RING_RX_HEAD_REG		0x8001C
#define HCLGE_RING_RX_FBD_NUM_REG	0x80020
#define HCLGE_RING_RX_OFFSET_REG	0x80024
#define HCLGE_RING_RX_FBD_OFFSET_REG	0x80028
#define HCLGE_RING_RX_STASH_REG		0x80030
#define HCLGE_RING_RX_BD_ERR_REG	0x80034
#define HCLGE_RING_TX_ADDR_L_REG	0x80040
#define HCLGE_RING_TX_ADDR_H_REG	0x80044
#define HCLGE_RING_TX_BD_NUM_REG	0x80048
#define HCLGE_RING_TX_PRIORITY_REG	0x8004C
#define HCLGE_RING_TX_TC_REG		0x80050
#define HCLGE_RING_TX_MERGE_EN_REG	0x80054
#define HCLGE_RING_TX_TAIL_REG		0x80058
#define HCLGE_RING_TX_HEAD_REG		0x8005C
#define HCLGE_RING_TX_FBD_NUM_REG	0x80060
#define HCLGE_RING_TX_OFFSET_REG	0x80064
#define HCLGE_RING_TX_EBD_NUM_REG	0x80068
#define HCLGE_RING_TX_EBD_OFFSET_REG	0x80070
#define HCLGE_RING_TX_BD_ERR_REG	0x80074
#define HCLGE_RING_EN_REG		0x80090

/* bar registers for tqp interrupt */
#define HCLGE_TQP_INTR_CTRL_REG		0x20000
#define HCLGE_TQP_INTR_GL0_REG		0x20100
#define HCLGE_TQP_INTR_GL1_REG		0x20200
#define HCLGE_TQP_INTR_GL2_REG		0x20300
#define HCLGE_TQP_INTR_RL_REG		0x20900

#define HCLGE_RSS_IND_TBL_SIZE		512
#define HCLGE_RSS_SET_BITMAP_MSK	GENMASK(15, 0)
#define HCLGE_RSS_KEY_SIZE		40
#define HCLGE_RSS_HASH_ALGO_TOEPLITZ	0
#define HCLGE_RSS_HASH_ALGO_SIMPLE	1
#define HCLGE_RSS_HASH_ALGO_SYMMETRIC	2
#define HCLGE_RSS_HASH_ALGO_MASK	GENMASK(3, 0)

#define HCLGE_RSS_INPUT_TUPLE_OTHER	GENMASK(3, 0)
#define HCLGE_RSS_INPUT_TUPLE_SCTP	GENMASK(4, 0)
#define HCLGE_D_PORT_BIT		BIT(0)
#define HCLGE_S_PORT_BIT		BIT(1)
#define HCLGE_D_IP_BIT			BIT(2)
#define HCLGE_S_IP_BIT			BIT(3)
#define HCLGE_V_TAG_BIT			BIT(4)
#define HCLGE_RSS_INPUT_TUPLE_SCTP_NO_PORT	\
		(HCLGE_D_IP_BIT | HCLGE_S_IP_BIT | HCLGE_V_TAG_BIT)

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

#define HCLGE_TQP_RESET_TRY_TIMES	200

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

#define HCLGE_GET_DFX_REG_TYPE_CNT	4

/* Factor used to calculate offset and bitmap of VF num */
#define HCLGE_VF_NUM_PER_CMD           64

#define HCLGE_MAX_QSET_NUM		1024

#define HCLGE_DBG_RESET_INFO_LEN	1024

enum HLCGE_PORT_TYPE {
	HOST_PORT,
	NETWORK_PORT
};

#define PF_VPORT_ID			0

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
#define HCLGE_RESET_INT_M		GENMASK(7, 5)
#define HCLGE_FUN_RST_ING		0x20C00
#define HCLGE_FUN_RST_ING_B		0

/* Vector0 register bits define */
#define HCLGE_VECTOR0_REG_PTP_INT_B	0
#define HCLGE_VECTOR0_GLOBALRESET_INT_B	5
#define HCLGE_VECTOR0_CORERESET_INT_B	6
#define HCLGE_VECTOR0_IMPRESET_INT_B	7

/* Vector0 interrupt CMDQ event source register(RW) */
#define HCLGE_VECTOR0_CMDQ_SRC_REG	0x27100
/* CMDQ register bits for RX event(=MBX event) */
#define HCLGE_VECTOR0_RX_CMDQ_INT_B	1

#define HCLGE_VECTOR0_IMP_RESET_INT_B	1
#define HCLGE_VECTOR0_IMP_CMDQ_ERR_B	4U
#define HCLGE_VECTOR0_IMP_RD_POISON_B	5U
#define HCLGE_VECTOR0_ALL_MSIX_ERR_B	6U

#define HCLGE_MAC_DEFAULT_FRAME \
	(ETH_HLEN + ETH_FCS_LEN + 2 * VLAN_HLEN + ETH_DATA_LEN)
#define HCLGE_MAC_MIN_FRAME		64
#define HCLGE_MAC_MAX_FRAME		9728

#define HCLGE_SUPPORT_1G_BIT		BIT(0)
#define HCLGE_SUPPORT_10G_BIT		BIT(1)
#define HCLGE_SUPPORT_25G_BIT		BIT(2)
#define HCLGE_SUPPORT_50G_BIT		BIT(3)
#define HCLGE_SUPPORT_100G_BIT		BIT(4)
/* to be compatible with exsit board */
#define HCLGE_SUPPORT_40G_BIT		BIT(5)
#define HCLGE_SUPPORT_100M_BIT		BIT(6)
#define HCLGE_SUPPORT_10M_BIT		BIT(7)
#define HCLGE_SUPPORT_200G_BIT		BIT(8)
#define HCLGE_SUPPORT_GE \
	(HCLGE_SUPPORT_1G_BIT | HCLGE_SUPPORT_100M_BIT | HCLGE_SUPPORT_10M_BIT)

enum HCLGE_DEV_STATE {
	HCLGE_STATE_REINITING,
	HCLGE_STATE_DOWN,
	HCLGE_STATE_DISABLED,
	HCLGE_STATE_REMOVING,
	HCLGE_STATE_NIC_REGISTERED,
	HCLGE_STATE_ROCE_REGISTERED,
	HCLGE_STATE_SERVICE_INITED,
	HCLGE_STATE_RST_SERVICE_SCHED,
	HCLGE_STATE_RST_HANDLING,
	HCLGE_STATE_MBX_SERVICE_SCHED,
	HCLGE_STATE_MBX_HANDLING,
	HCLGE_STATE_ERR_SERVICE_SCHED,
	HCLGE_STATE_STATISTICS_UPDATING,
	HCLGE_STATE_CMD_DISABLE,
	HCLGE_STATE_LINK_UPDATING,
	HCLGE_STATE_RST_FAIL,
	HCLGE_STATE_FD_TBL_CHANGED,
	HCLGE_STATE_FD_CLEAR_ALL,
	HCLGE_STATE_FD_USER_DEF_CHANGED,
	HCLGE_STATE_PTP_EN,
	HCLGE_STATE_PTP_TX_HANDLING,
	HCLGE_STATE_MAX
};

enum hclge_evt_cause {
	HCLGE_VECTOR0_EVENT_RST,
	HCLGE_VECTOR0_EVENT_MBX,
	HCLGE_VECTOR0_EVENT_ERR,
	HCLGE_VECTOR0_EVENT_PTP,
	HCLGE_VECTOR0_EVENT_OTHER,
};

enum HCLGE_MAC_SPEED {
	HCLGE_MAC_SPEED_UNKNOWN = 0,		/* unknown */
	HCLGE_MAC_SPEED_10M	= 10,		/* 10 Mbps */
	HCLGE_MAC_SPEED_100M	= 100,		/* 100 Mbps */
	HCLGE_MAC_SPEED_1G	= 1000,		/* 1000 Mbps   = 1 Gbps */
	HCLGE_MAC_SPEED_10G	= 10000,	/* 10000 Mbps  = 10 Gbps */
	HCLGE_MAC_SPEED_25G	= 25000,	/* 25000 Mbps  = 25 Gbps */
	HCLGE_MAC_SPEED_40G	= 40000,	/* 40000 Mbps  = 40 Gbps */
	HCLGE_MAC_SPEED_50G	= 50000,	/* 50000 Mbps  = 50 Gbps */
	HCLGE_MAC_SPEED_100G	= 100000,	/* 100000 Mbps = 100 Gbps */
	HCLGE_MAC_SPEED_200G	= 200000	/* 200000 Mbps = 200 Gbps */
};

enum HCLGE_MAC_DUPLEX {
	HCLGE_MAC_HALF,
	HCLGE_MAC_FULL
};

#define QUERY_SFP_SPEED		0
#define QUERY_ACTIVE_SPEED	1

struct hclge_mac {
	u8 mac_id;
	u8 phy_addr;
	u8 flag;
	u8 media_type;	/* port media type, e.g. fibre/copper/backplane */
	u8 mac_addr[ETH_ALEN];
	u8 autoneg;
	u8 duplex;
	u8 support_autoneg;
	u8 speed_type;	/* 0: sfp speed, 1: active speed */
	u32 speed;
	u32 max_speed;
	u32 speed_ability; /* speed ability supported by current media */
	u32 module_type; /* sub media type, e.g. kr/cr/sr/lr */
	u32 fec_mode; /* active fec mode */
	u32 user_fec_mode;
	u32 fec_ability;
	int link;	/* store the link status of mac & phy (if phy exists) */
	struct phy_device *phydev;
	struct mii_bus *mdio_bus;
	phy_interface_t phy_if;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	__ETHTOOL_DECLARE_LINK_MODE_MASK(advertising);
};

struct hclge_hw {
	void __iomem *io_base;
	void __iomem *mem_base;
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

#define HCLGE_FILTER_TYPE_VF		0
#define HCLGE_FILTER_TYPE_PORT		1
#define HCLGE_FILTER_FE_EGRESS_V1_B	BIT(0)
#define HCLGE_FILTER_FE_NIC_INGRESS_B	BIT(0)
#define HCLGE_FILTER_FE_NIC_EGRESS_B	BIT(1)
#define HCLGE_FILTER_FE_ROCE_INGRESS_B	BIT(2)
#define HCLGE_FILTER_FE_ROCE_EGRESS_B	BIT(3)
#define HCLGE_FILTER_FE_EGRESS		(HCLGE_FILTER_FE_NIC_EGRESS_B \
					| HCLGE_FILTER_FE_ROCE_EGRESS_B)
#define HCLGE_FILTER_FE_INGRESS		(HCLGE_FILTER_FE_NIC_INGRESS_B \
					| HCLGE_FILTER_FE_ROCE_INGRESS_B)

enum hclge_vlan_fltr_cap {
	HCLGE_VLAN_FLTR_DEF,
	HCLGE_VLAN_FLTR_CAN_MDF,
};
enum hclge_link_fail_code {
	HCLGE_LF_NORMAL,
	HCLGE_LF_REF_CLOCK_LOST,
	HCLGE_LF_XSFP_TX_DISABLE,
	HCLGE_LF_XSFP_ABSENT,
};

#define HCLGE_LINK_STATUS_DOWN 0
#define HCLGE_LINK_STATUS_UP   1

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
	u8 tc_num;
	u8 vlan_fliter_cap;
	u16 tqp_desc_num;
	u16 rx_buf_len;
	u16 vf_rss_size_max;
	u16 pf_rss_size_max;
	u8 phy_addr;
	u8 media_type;
	u8 mac_addr[ETH_ALEN];
	u8 default_speed;
	u32 numa_node_map;
	u32 tx_spare_buf_size;
	u16 speed_ability;
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
	u8 pfc_en;	/* PFC enabled or not for user priority */
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
	u64 mac_tx_pfc_pause_pkt_num;
	u64 mac_rx_pfc_pause_pkt_num;
	u64 mac_tx_ctrl_pkt_num;
	u64 mac_rx_ctrl_pkt_num;
};

#define HCLGE_STATS_TIMER_INTERVAL	300UL

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
	MAX_STAGE_NUM,
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

#define HCLGE_FD_TUPLE_USER_DEF_TUPLES \
	(BIT(INNER_L2_RSV) | BIT(INNER_L3_RSV) | BIT(INNER_L4_RSV))

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

enum HCLGE_FD_KEY_OPT {
	KEY_OPT_U8,
	KEY_OPT_LE16,
	KEY_OPT_LE32,
	KEY_OPT_MAC,
	KEY_OPT_IP,
	KEY_OPT_VNI,
};

struct key_info {
	u8 key_type;
	u8 key_length; /* use bit as unit */
	enum HCLGE_FD_KEY_OPT key_opt;
	int offset;
	int moffset;
};

#define MAX_KEY_LENGTH	400
#define MAX_KEY_DWORDS	DIV_ROUND_UP(MAX_KEY_LENGTH / 8, 4)
#define MAX_KEY_BYTES	(MAX_KEY_DWORDS * 4)
#define MAX_META_DATA_LENGTH	32

#define HCLGE_FD_MAX_USER_DEF_OFFSET	9000
#define HCLGE_FD_USER_DEF_DATA		GENMASK(15, 0)
#define HCLGE_FD_USER_DEF_OFFSET	GENMASK(15, 0)
#define HCLGE_FD_USER_DEF_OFFSET_UNMASK	GENMASK(15, 0)

/* assigned by firmware, the real filter number for each pf may be less */
#define MAX_FD_FILTER_NUM	4096
#define HCLGE_ARFS_EXPIRE_INTERVAL	5UL

enum HCLGE_FD_ACTIVE_RULE_TYPE {
	HCLGE_FD_RULE_NONE,
	HCLGE_FD_ARFS_ACTIVE,
	HCLGE_FD_EP_ACTIVE,
	HCLGE_FD_TC_FLOWER_ACTIVE,
};

enum HCLGE_FD_PACKET_TYPE {
	NIC_PACKET,
	ROCE_PACKET,
};

enum HCLGE_FD_ACTION {
	HCLGE_FD_ACTION_SELECT_QUEUE,
	HCLGE_FD_ACTION_DROP_PACKET,
	HCLGE_FD_ACTION_SELECT_TC,
};

enum HCLGE_FD_NODE_STATE {
	HCLGE_FD_TO_ADD,
	HCLGE_FD_TO_DEL,
	HCLGE_FD_ACTIVE,
	HCLGE_FD_DELETED,
};

enum HCLGE_FD_USER_DEF_LAYER {
	HCLGE_FD_USER_DEF_NONE,
	HCLGE_FD_USER_DEF_L2,
	HCLGE_FD_USER_DEF_L3,
	HCLGE_FD_USER_DEF_L4,
};

#define HCLGE_FD_USER_DEF_LAYER_NUM 3
struct hclge_fd_user_def_cfg {
	u16 ref_cnt;
	u16 offset;
};

struct hclge_fd_user_def_info {
	enum HCLGE_FD_USER_DEF_LAYER layer;
	u16 data;
	u16 data_mask;
	u16 offset;
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
	u16 max_key_length; /* use bit as unit */
	u32 rule_num[MAX_STAGE_NUM]; /* rule entry number */
	u16 cnt_num[MAX_STAGE_NUM]; /* rule hit counter number */
	struct hclge_fd_key_cfg key_cfg[MAX_STAGE_NUM];
	struct hclge_fd_user_def_cfg user_def_cfg[HCLGE_FD_USER_DEF_LAYER_NUM];
};

#define IPV4_INDEX	3
#define IPV6_SIZE	4
struct hclge_fd_rule_tuples {
	u8 src_mac[ETH_ALEN];
	u8 dst_mac[ETH_ALEN];
	/* Be compatible for ip address of both ipv4 and ipv6.
	 * For ipv4 address, we store it in src/dst_ip[3].
	 */
	u32 src_ip[IPV6_SIZE];
	u32 dst_ip[IPV6_SIZE];
	u16 src_port;
	u16 dst_port;
	u16 vlan_tag1;
	u16 ether_proto;
	u16 l2_user_def;
	u16 l3_user_def;
	u32 l4_user_def;
	u8 ip_tos;
	u8 ip_proto;
};

struct hclge_fd_rule {
	struct hlist_node rule_node;
	struct hclge_fd_rule_tuples tuples;
	struct hclge_fd_rule_tuples tuples_mask;
	u32 unused_tuple;
	u32 flow_type;
	union {
		struct {
			unsigned long cookie;
			u8 tc;
		} cls_flower;
		struct {
			u16 flow_id; /* only used for arfs */
		} arfs;
		struct {
			struct hclge_fd_user_def_info user_def;
		} ep;
	};
	u16 queue_id;
	u16 vf_id;
	u16 location;
	enum HCLGE_FD_ACTIVE_RULE_TYPE rule_type;
	enum HCLGE_FD_NODE_STATE state;
	u8 action;
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
	u16 tc_size;
	u8 override_tc;
};

enum HCLGE_MAC_NODE_STATE {
	HCLGE_MAC_TO_ADD,
	HCLGE_MAC_TO_DEL,
	HCLGE_MAC_ACTIVE
};

struct hclge_mac_node {
	struct list_head node;
	enum HCLGE_MAC_NODE_STATE state;
	u8 mac_addr[ETH_ALEN];
};

enum HCLGE_MAC_ADDR_TYPE {
	HCLGE_MAC_ADDR_UC,
	HCLGE_MAC_ADDR_MC
};

struct hclge_vport_vlan_cfg {
	struct list_head node;
	int hd_tbl_status;
	u16 vlan_id;
};

struct hclge_rst_stats {
	u32 reset_done_cnt;	/* the number of reset has completed */
	u32 hw_reset_done_cnt;	/* the number of HW reset has completed */
	u32 pf_rst_cnt;		/* the number of PF reset */
	u32 flr_rst_cnt;	/* the number of FLR */
	u32 global_rst_cnt;	/* the number of GLOBAL */
	u32 imp_rst_cnt;	/* the number of IMP reset */
	u32 reset_cnt;		/* the number of reset */
	u32 reset_fail_cnt;	/* the number of reset fail */
};

/* time and register status when mac tunnel interruption occur */
struct hclge_mac_tnl_stats {
	u64 time;
	u32 status;
};

#define HCLGE_RESET_INTERVAL	(10 * HZ)
#define HCLGE_WAIT_RESET_DONE	100

#pragma pack(1)
struct hclge_vf_vlan_cfg {
	u8 mbx_cmd;
	u8 subcode;
	union {
		struct {
			u8 is_kill;
			u16 vlan;
			u16 proto;
		};
		u8 enable;
	};
};

#pragma pack()

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
#define calc_x(x, k, v) (x = ~(k) & (v))
#define calc_y(y, k, v) \
	do { \
		const typeof(k) _k_ = (k); \
		const typeof(v) _v_ = (v); \
		(y) = (_k_ ^ ~_v_) & (_k_); \
	} while (0)

#define HCLGE_MAC_TNL_LOG_SIZE	8
#define HCLGE_VPORT_NUM 256
struct hclge_dev {
	struct pci_dev *pdev;
	struct hnae3_ae_dev *ae_dev;
	struct hclge_hw hw;
	struct hclge_misc_vector misc_vector;
	struct hclge_mac_stats mac_stats;
	unsigned long state;
	unsigned long flr_state;
	unsigned long last_reset_time;

	enum hnae3_reset_type reset_type;
	enum hnae3_reset_type reset_level;
	unsigned long default_reset_request;
	unsigned long reset_request;	/* reset has been requested */
	unsigned long reset_pending;	/* client rst is pending to be served */
	struct hclge_rst_stats rst_stats;
	struct semaphore reset_sem;	/* protect reset process */
	u32 fw_version;
	u16 num_tqps;			/* Num task queue pairs of this PF */
	u16 num_req_vfs;		/* Num VFs requested for this PF */

	u16 base_tqp_pid;	/* Base task tqp physical id of this PF */
	u16 alloc_rss_size;		/* Allocated RSS task queue */
	u16 vf_rss_size_max;		/* HW defined VF max RSS task queue */
	u16 pf_rss_size_max;		/* HW defined PF max RSS task queue */
	u32 tx_spare_buf_size;		/* HW defined TX spare buffer size */

	u16 fdir_pf_filter_count; /* Num of guaranteed filters for this PF */
	u16 num_alloc_vport;		/* Num vports this driver supports */
	u32 numa_node_mask;
	u16 rx_buf_len;
	u16 num_tx_desc;		/* desc num of per tx queue */
	u16 num_rx_desc;		/* desc num of per rx queue */
	u8 hw_tc_map;
	enum hclge_fc_mode fc_mode_last_time;
	u8 support_sfp_query;

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
	u32 base_msi_vector;
	u16 *vector_status;
	int *vector_irq;
	u16 num_nic_msi;	/* Num of nic vectors for this PF */
	u16 num_roce_msi;	/* Num of roce vectors for this PF */
	int roce_base_vector;

	unsigned long service_timer_period;
	unsigned long service_timer_previous;
	struct timer_list reset_timer;
	struct delayed_work service_task;

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
	u32 tx_buf_size; /* Tx buffer size for each TC */
	u32 dv_buf_size; /* Dv buffer size for each TC */

	u32 mps; /* Max packet size */
	/* vport_lock protect resource shared by vports */
	struct mutex vport_lock;

	struct hclge_vlan_type_cfg vlan_type_cfg;

	unsigned long vlan_table[VLAN_N_VID][BITS_TO_LONGS(HCLGE_VPORT_NUM)];
	unsigned long vf_vlan_full[BITS_TO_LONGS(HCLGE_VPORT_NUM)];

	unsigned long vport_config_block[BITS_TO_LONGS(HCLGE_VPORT_NUM)];

	struct hclge_fd_cfg fd_cfg;
	struct hlist_head fd_rule_list;
	spinlock_t fd_rule_lock; /* protect fd_rule_list and fd_bmap */
	u16 hclge_fd_rule_num;
	unsigned long serv_processed_cnt;
	unsigned long last_serv_processed;
	unsigned long fd_bmap[BITS_TO_LONGS(MAX_FD_FILTER_NUM)];
	enum HCLGE_FD_ACTIVE_RULE_TYPE fd_active_type;
	u8 fd_en;

	u16 wanted_umv_size;
	/* max available unicast mac vlan space */
	u16 max_umv_size;
	/* private unicast mac vlan space, it's same for PF and its VFs */
	u16 priv_umv_size;
	/* unicast mac vlan space shared by PF and its VFs */
	u16 share_umv_size;

	DECLARE_KFIFO(mac_tnl_log, struct hclge_mac_tnl_stats,
		      HCLGE_MAC_TNL_LOG_SIZE);

	/* affinity mask and notify for misc interrupt */
	cpumask_t affinity_mask;
	struct irq_affinity_notify affinity_notify;
	struct hclge_ptp *ptp;
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
	bool tag_shift_mode_en;
};

/* VPort level vlan tag configuration for RX direction */
struct hclge_rx_vtag_cfg {
	bool rx_vlan_offload_en; /* Whether enable rx vlan offload */
	bool strip_tag1_en;	 /* Whether strip inner vlan tag */
	bool strip_tag2_en;	 /* Whether strip outer vlan tag */
	bool vlan1_vlan_prionly; /* Inner vlan tag up to descriptor enable */
	bool vlan2_vlan_prionly; /* Outer vlan tag up to descriptor enable */
	bool strip_tag1_discard_en; /* Inner vlan tag discard for BD enable */
	bool strip_tag2_discard_en; /* Outer vlan tag discard for BD enable */
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

enum HCLGE_VPORT_STATE {
	HCLGE_VPORT_STATE_ALIVE,
	HCLGE_VPORT_STATE_MAC_TBL_CHANGE,
	HCLGE_VPORT_STATE_PROMISC_CHANGE,
	HCLGE_VPORT_STATE_VLAN_FLTR_CHANGE,
	HCLGE_VPORT_STATE_MAX
};

struct hclge_vlan_info {
	u16 vlan_proto; /* so far support 802.1Q only */
	u16 qos;
	u16 vlan_tag;
};

struct hclge_port_base_vlan_config {
	u16 state;
	struct hclge_vlan_info vlan_info;
};

struct hclge_vf_info {
	int link_state;
	u8 mac[ETH_ALEN];
	u32 spoofchk;
	u32 max_tx_rate;
	u32 trusted;
	u8 request_uc_en;
	u8 request_mc_en;
	u8 request_bc_en;
};

struct hclge_vport {
	u16 alloc_tqps;	/* Allocated Tx/Rx queues */

	u8  rss_hash_key[HCLGE_RSS_KEY_SIZE]; /* User configured hash keys */
	/* User configured lookup table entries */
	u16 *rss_indirection_tbl;
	int rss_algo;		/* User configured hash algorithm */
	/* User configured rss tuple sets */
	struct hclge_rss_tuple_cfg rss_tuple_sets;

	u16 alloc_rss_size;

	u16 qs_offset;
	u32 bw_limit;		/* VSI BW Limit (0 = disabled) */
	u8  dwrr;

	bool req_vlan_fltr_en;
	bool cur_vlan_fltr_en;
	unsigned long vlan_del_fail_bmap[BITS_TO_LONGS(VLAN_N_VID)];
	struct hclge_port_base_vlan_config port_base_vlan_cfg;
	struct hclge_tx_vtag_cfg  txvlan_cfg;
	struct hclge_rx_vtag_cfg  rxvlan_cfg;

	u16 used_umv_num;

	u16 vport_id;
	struct hclge_dev *back;  /* Back reference to associated dev */
	struct hnae3_handle nic;
	struct hnae3_handle roce;

	unsigned long state;
	unsigned long last_active_jiffies;
	u32 mps; /* Max packet size */
	struct hclge_vf_info vf_info;

	u8 overflow_promisc_flags;
	u8 last_promisc_flags;

	spinlock_t mac_list_lock; /* protect mac address need to add/detele */
	struct list_head uc_mac_list;   /* Store VF unicast table */
	struct list_head mc_mac_list;   /* Store VF multicast table */
	struct list_head vlan_list;     /* Store VF vlan table */
};

int hclge_set_vport_promisc_mode(struct hclge_vport *vport, bool en_uc_pmc,
				 bool en_mc_pmc, bool en_bc_pmc);
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

void hclge_mbx_handler(struct hclge_dev *hdev);
int hclge_reset_tqp(struct hnae3_handle *handle);
int hclge_cfg_flowctrl(struct hclge_dev *hdev);
int hclge_func_reset_cmd(struct hclge_dev *hdev, int func_id);
int hclge_vport_start(struct hclge_vport *vport);
void hclge_vport_stop(struct hclge_vport *vport);
int hclge_set_vport_mtu(struct hclge_vport *vport, int new_mtu);
int hclge_dbg_read_cmd(struct hnae3_handle *handle, enum hnae3_dbg_cmd cmd,
		       char *buf, int len);
u16 hclge_covert_handle_qid_global(struct hnae3_handle *handle, u16 queue_id);
int hclge_notify_client(struct hclge_dev *hdev,
			enum hnae3_reset_notify_type type);
int hclge_update_mac_list(struct hclge_vport *vport,
			  enum HCLGE_MAC_NODE_STATE state,
			  enum HCLGE_MAC_ADDR_TYPE mac_type,
			  const unsigned char *addr);
int hclge_update_mac_node_for_dev_addr(struct hclge_vport *vport,
				       const u8 *old_addr, const u8 *new_addr);
void hclge_rm_vport_all_mac_table(struct hclge_vport *vport, bool is_del_list,
				  enum HCLGE_MAC_ADDR_TYPE mac_type);
void hclge_rm_vport_all_vlan_table(struct hclge_vport *vport, bool is_del_list);
void hclge_uninit_vport_vlan_table(struct hclge_dev *hdev);
void hclge_restore_mac_table_common(struct hclge_vport *vport);
void hclge_restore_vport_vlan_table(struct hclge_vport *vport);
int hclge_update_port_base_vlan_cfg(struct hclge_vport *vport, u16 state,
				    struct hclge_vlan_info *vlan_info);
int hclge_push_vf_port_base_vlan_info(struct hclge_vport *vport, u8 vfid,
				      u16 state,
				      struct hclge_vlan_info *vlan_info);
void hclge_task_schedule(struct hclge_dev *hdev, unsigned long delay_time);
int hclge_query_bd_num_cmd_send(struct hclge_dev *hdev,
				struct hclge_desc *desc);
void hclge_report_hw_error(struct hclge_dev *hdev,
			   enum hnae3_hw_error_type type);
void hclge_inform_vf_promisc_info(struct hclge_vport *vport);
int hclge_dbg_dump_rst_info(struct hclge_dev *hdev, char *buf, int len);
int hclge_push_vf_link_status(struct hclge_vport *vport);
int hclge_enable_vport_vlan_filter(struct hclge_vport *vport, bool request_en);
#endif
