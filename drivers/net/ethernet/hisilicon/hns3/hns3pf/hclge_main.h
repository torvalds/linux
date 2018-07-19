/*
 * Copyright (c) 2016~2017 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

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

#define HCLGE_INVALID_VPORT 0xffff

#define HCLGE_ROCE_VECTOR_OFFSET	96

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

#define HCLGE_MTA_TBL_SIZE		4096

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

/* Reset related Registers */
#define HCLGE_MISC_RESET_STS_REG	0x20700
#define HCLGE_MISC_VECTOR_INT_STS	0x20800
#define HCLGE_GLOBAL_RESET_REG		0x20A00
#define HCLGE_GLOBAL_RESET_BIT		0
#define HCLGE_CORE_RESET_BIT		1
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

enum hclge_mta_dmac_sel_type {
	HCLGE_MAC_ADDR_47_36,
	HCLGE_MAC_ADDR_46_35,
	HCLGE_MAC_ADDR_45_34,
	HCLGE_MAC_ADDR_44_33,
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

/* all 64bit stats, opcode id: 0x0030 */
struct hclge_64_bit_stats {
	/* query_igu_stat */
	u64 igu_rx_oversize_pkt;
	u64 igu_rx_undersize_pkt;
	u64 igu_rx_out_all_pkt;
	u64 igu_rx_uni_pkt;
	u64 igu_rx_multi_pkt;
	u64 igu_rx_broad_pkt;
	u64 rsv0;

	/* query_egu_stat */
	u64 egu_tx_out_all_pkt;
	u64 egu_tx_uni_pkt;
	u64 egu_tx_multi_pkt;
	u64 egu_tx_broad_pkt;

	/* ssu_ppp packet stats */
	u64 ssu_ppp_mac_key_num;
	u64 ssu_ppp_host_key_num;
	u64 ppp_ssu_mac_rlt_num;
	u64 ppp_ssu_host_rlt_num;

	/* ssu_tx_in_out_dfx_stats */
	u64 ssu_tx_in_num;
	u64 ssu_tx_out_num;
	/* ssu_rx_in_out_dfx_stats */
	u64 ssu_rx_in_num;
	u64 ssu_rx_out_num;
};

/* all 32bit stats, opcode id: 0x0031 */
struct hclge_32_bit_stats {
	u64 igu_rx_err_pkt;
	u64 igu_rx_no_eof_pkt;
	u64 igu_rx_no_sof_pkt;
	u64 egu_tx_1588_pkt;
	u64 egu_tx_err_pkt;
	u64 ssu_full_drop_num;
	u64 ssu_part_drop_num;
	u64 ppp_key_drop_num;
	u64 ppp_rlt_drop_num;
	u64 ssu_key_drop_num;
	u64 pkt_curr_buf_cnt;
	u64 qcn_fb_rcv_cnt;
	u64 qcn_fb_drop_cnt;
	u64 qcn_fb_invaild_cnt;
	u64 rsv0;
	u64 rx_packet_tc0_in_cnt;
	u64 rx_packet_tc1_in_cnt;
	u64 rx_packet_tc2_in_cnt;
	u64 rx_packet_tc3_in_cnt;
	u64 rx_packet_tc4_in_cnt;
	u64 rx_packet_tc5_in_cnt;
	u64 rx_packet_tc6_in_cnt;
	u64 rx_packet_tc7_in_cnt;
	u64 rx_packet_tc0_out_cnt;
	u64 rx_packet_tc1_out_cnt;
	u64 rx_packet_tc2_out_cnt;
	u64 rx_packet_tc3_out_cnt;
	u64 rx_packet_tc4_out_cnt;
	u64 rx_packet_tc5_out_cnt;
	u64 rx_packet_tc6_out_cnt;
	u64 rx_packet_tc7_out_cnt;

	/* Tx packet level statistics */
	u64 tx_packet_tc0_in_cnt;
	u64 tx_packet_tc1_in_cnt;
	u64 tx_packet_tc2_in_cnt;
	u64 tx_packet_tc3_in_cnt;
	u64 tx_packet_tc4_in_cnt;
	u64 tx_packet_tc5_in_cnt;
	u64 tx_packet_tc6_in_cnt;
	u64 tx_packet_tc7_in_cnt;
	u64 tx_packet_tc0_out_cnt;
	u64 tx_packet_tc1_out_cnt;
	u64 tx_packet_tc2_out_cnt;
	u64 tx_packet_tc3_out_cnt;
	u64 tx_packet_tc4_out_cnt;
	u64 tx_packet_tc5_out_cnt;
	u64 tx_packet_tc6_out_cnt;
	u64 tx_packet_tc7_out_cnt;

	/* packet buffer statistics */
	u64 pkt_curr_buf_tc0_cnt;
	u64 pkt_curr_buf_tc1_cnt;
	u64 pkt_curr_buf_tc2_cnt;
	u64 pkt_curr_buf_tc3_cnt;
	u64 pkt_curr_buf_tc4_cnt;
	u64 pkt_curr_buf_tc5_cnt;
	u64 pkt_curr_buf_tc6_cnt;
	u64 pkt_curr_buf_tc7_cnt;

	u64 mb_uncopy_num;
	u64 lo_pri_unicast_rlt_drop_num;
	u64 hi_pri_multicast_rlt_drop_num;
	u64 lo_pri_multicast_rlt_drop_num;
	u64 rx_oq_drop_pkt_cnt;
	u64 tx_oq_drop_pkt_cnt;
	u64 nic_l2_err_drop_pkt_cnt;
	u64 roc_l2_err_drop_pkt_cnt;
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
	struct hclge_64_bit_stats   all_64_bit_stats;
	struct hclge_32_bit_stats   all_32_bit_stats;
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

#define HCLGE_VPORT_NUM 256
struct hclge_dev {
	struct pci_dev *pdev;
	struct hnae3_ae_dev *ae_dev;
	struct hclge_hw hw;
	struct hclge_misc_vector misc_vector;
	struct hclge_hw_stats hw_stats;
	unsigned long state;

	enum hnae3_reset_type reset_type;
	unsigned long reset_request;	/* reset has been requested */
	unsigned long reset_pending;	/* client rst is pending to be served */
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

	enum hclge_mta_dmac_sel_type mta_mac_sel_type;
	bool enable_mta; /* Multicast filter enable */

	struct hclge_vlan_type_cfg vlan_type_cfg;

	unsigned long vlan_table[VLAN_N_VID][BITS_TO_LONGS(HCLGE_VPORT_NUM)];
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

	int vport_id;
	struct hclge_dev *back;  /* Back reference to associated dev */
	struct hnae3_handle nic;
	struct hnae3_handle roce;

	bool accept_mta_mc; /* whether to accept mta filter multicast */
	unsigned long mta_shadow[BITS_TO_LONGS(HCLGE_MTA_TBL_SIZE)];
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

int hclge_cfg_func_mta_filter(struct hclge_dev *hdev,
			      u8 func_id,
			      bool enable);
int hclge_update_mta_status_common(struct hclge_vport *vport,
				   unsigned long *status,
				   u16 idx,
				   u16 count,
				   bool update_filter);

struct hclge_vport *hclge_get_vport(struct hnae3_handle *handle);
int hclge_bind_ring_with_vector(struct hclge_vport *vport,
				int vector_id, bool en,
				struct hnae3_ring_chain_node *ring_chain);

static inline int hclge_get_queue_id(struct hnae3_queue *queue)
{
	struct hclge_tqp *tqp = container_of(queue, struct hclge_tqp, q);

	return tqp->index;
}

int hclge_cfg_mac_speed_dup(struct hclge_dev *hdev, int speed, u8 duplex);
int hclge_set_vlan_filter(struct hnae3_handle *handle, __be16 proto,
			  u16 vlan_id, bool is_kill);
int hclge_en_hw_strip_rxvtag(struct hnae3_handle *handle, bool enable);

int hclge_buffer_alloc(struct hclge_dev *hdev);
int hclge_rss_init_hw(struct hclge_dev *hdev);
void hclge_rss_indir_init_cfg(struct hclge_dev *hdev);

void hclge_mbx_handler(struct hclge_dev *hdev);
void hclge_reset_tqp(struct hnae3_handle *handle, u16 queue_id);
void hclge_reset_vf_queue(struct hclge_vport *vport, u16 queue_id);
int hclge_cfg_flowctrl(struct hclge_dev *hdev);
int hclge_func_reset_cmd(struct hclge_dev *hdev, int func_id);
#endif
