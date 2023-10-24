/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HCLGE_CMD_H
#define __HCLGE_CMD_H
#include <linux/types.h>
#include <linux/io.h>
#include <linux/etherdevice.h>
#include "hnae3.h"
#include "hclge_comm_cmd.h"

struct hclge_dev;

#define HCLGE_CMDQ_RX_INVLD_B		0
#define HCLGE_CMDQ_RX_OUTVLD_B		1

struct hclge_misc_vector {
	u8 __iomem *addr;
	int vector_irq;
	char name[HNAE3_INT_NAME_LEN];
};

#define hclge_cmd_setup_basic_desc(desc, opcode, is_read) \
	hclge_comm_cmd_setup_basic_desc(desc, opcode, is_read)

#define HCLGE_TQP_REG_OFFSET		0x80000
#define HCLGE_TQP_REG_SIZE		0x200

#define HCLGE_TQP_MAX_SIZE_DEV_V2	1024
#define HCLGE_TQP_EXT_REG_OFFSET	0x100

#define HCLGE_RCB_INIT_QUERY_TIMEOUT	10
#define HCLGE_RCB_INIT_FLAG_EN_B	0
#define HCLGE_RCB_INIT_FLAG_FINI_B	8
struct hclge_config_rcb_init_cmd {
	__le16 rcb_init_flag;
	u8 rsv[22];
};

struct hclge_tqp_map_cmd {
	__le16 tqp_id;	/* Absolute tqp id for in this pf */
	u8 tqp_vf;	/* VF id */
#define HCLGE_TQP_MAP_TYPE_PF		0
#define HCLGE_TQP_MAP_TYPE_VF		1
#define HCLGE_TQP_MAP_TYPE_B		0
#define HCLGE_TQP_MAP_EN_B		1
	u8 tqp_flag;	/* Indicate it's pf or vf tqp */
	__le16 tqp_vid; /* Virtual id in this pf/vf */
	u8 rsv[18];
};

#define HCLGE_VECTOR_ELEMENTS_PER_CMD	10

enum hclge_int_type {
	HCLGE_INT_TX,
	HCLGE_INT_RX,
	HCLGE_INT_EVENT,
};

struct hclge_ctrl_vector_chain_cmd {
#define HCLGE_VECTOR_ID_L_S	0
#define HCLGE_VECTOR_ID_L_M	GENMASK(7, 0)
	u8 int_vector_id_l;
	u8 int_cause_num;
#define HCLGE_INT_TYPE_S	0
#define HCLGE_INT_TYPE_M	GENMASK(1, 0)
#define HCLGE_TQP_ID_S		2
#define HCLGE_TQP_ID_M		GENMASK(12, 2)
#define HCLGE_INT_GL_IDX_S	13
#define HCLGE_INT_GL_IDX_M	GENMASK(14, 13)
	__le16 tqp_type_and_id[HCLGE_VECTOR_ELEMENTS_PER_CMD];
	u8 vfid;
#define HCLGE_VECTOR_ID_H_S	8
#define HCLGE_VECTOR_ID_H_M	GENMASK(15, 8)
	u8 int_vector_id_h;
};

#define HCLGE_MAX_TC_NUM		8
#define HCLGE_TC0_PRI_BUF_EN_B	15 /* Bit 15 indicate enable or not */
#define HCLGE_BUF_UNIT_S	7  /* Buf size is united by 128 bytes */
struct hclge_tx_buff_alloc_cmd {
	__le16 tx_pkt_buff[HCLGE_MAX_TC_NUM];
	u8 tx_buff_rsv[8];
};

struct hclge_rx_priv_buff_cmd {
	__le16 buf_num[HCLGE_MAX_TC_NUM];
	__le16 shared_buf;
	u8 rsv[6];
};

#define HCLGE_RX_PRIV_EN_B	15
#define HCLGE_TC_NUM_ONE_DESC	4
struct hclge_priv_wl {
	__le16 high;
	__le16 low;
};

struct hclge_rx_priv_wl_buf {
	struct hclge_priv_wl tc_wl[HCLGE_TC_NUM_ONE_DESC];
};

struct hclge_rx_com_thrd {
	struct hclge_priv_wl com_thrd[HCLGE_TC_NUM_ONE_DESC];
};

struct hclge_rx_com_wl {
	struct hclge_priv_wl com_wl;
};

struct hclge_waterline {
	u32 low;
	u32 high;
};

struct hclge_tc_thrd {
	u32 low;
	u32 high;
};

struct hclge_priv_buf {
	struct hclge_waterline wl;	/* Waterline for low and high */
	u32 buf_size;	/* TC private buffer size */
	u32 tx_buf_size;
	u32 enable;	/* Enable TC private buffer or not */
};

struct hclge_shared_buf {
	struct hclge_waterline self;
	struct hclge_tc_thrd tc_thrd[HCLGE_MAX_TC_NUM];
	u32 buf_size;
};

struct hclge_pkt_buf_alloc {
	struct hclge_priv_buf priv_buf[HCLGE_MAX_TC_NUM];
	struct hclge_shared_buf s_buf;
};

#define HCLGE_RX_COM_WL_EN_B	15
struct hclge_rx_com_wl_buf_cmd {
	__le16 high_wl;
	__le16 low_wl;
	u8 rsv[20];
};

#define HCLGE_RX_PKT_EN_B	15
struct hclge_rx_pkt_buf_cmd {
	__le16 high_pkt;
	__le16 low_pkt;
	u8 rsv[20];
};

#define HCLGE_PF_STATE_DONE_B	0
#define HCLGE_PF_STATE_MAIN_B	1
#define HCLGE_PF_STATE_BOND_B	2
#define HCLGE_PF_STATE_MAC_N_B	6
#define HCLGE_PF_MAC_NUM_MASK	0x3
#define HCLGE_PF_STATE_MAIN	BIT(HCLGE_PF_STATE_MAIN_B)
#define HCLGE_PF_STATE_DONE	BIT(HCLGE_PF_STATE_DONE_B)
#define HCLGE_VF_RST_STATUS_CMD	4

struct hclge_func_status_cmd {
	__le32  vf_rst_state[HCLGE_VF_RST_STATUS_CMD];
	u8 pf_state;
	u8 mac_id;
	u8 rsv1;
	u8 pf_cnt_in_mac;
	u8 pf_num;
	u8 vf_num;
	u8 rsv[2];
};

struct hclge_pf_res_cmd {
	__le16 tqp_num;
	__le16 buf_size;
	__le16 msixcap_localid_ba_nic;
	__le16 msixcap_localid_number_nic;
	__le16 pf_intr_vector_number_roce;
	__le16 pf_own_fun_number;
	__le16 tx_buf_size;
	__le16 dv_buf_size;
	__le16 ext_tqp_num;
	u8 rsv[6];
};

#define HCLGE_CFG_OFFSET_S	0
#define HCLGE_CFG_OFFSET_M	GENMASK(19, 0)
#define HCLGE_CFG_RD_LEN_S	24
#define HCLGE_CFG_RD_LEN_M	GENMASK(27, 24)
#define HCLGE_CFG_RD_LEN_BYTES	16
#define HCLGE_CFG_RD_LEN_UNIT	4

#define HCLGE_CFG_TC_NUM_S	8
#define HCLGE_CFG_TC_NUM_M	GENMASK(15, 8)
#define HCLGE_CFG_TQP_DESC_N_S	16
#define HCLGE_CFG_TQP_DESC_N_M	GENMASK(31, 16)
#define HCLGE_CFG_PHY_ADDR_S	0
#define HCLGE_CFG_PHY_ADDR_M	GENMASK(7, 0)
#define HCLGE_CFG_MEDIA_TP_S	8
#define HCLGE_CFG_MEDIA_TP_M	GENMASK(15, 8)
#define HCLGE_CFG_RX_BUF_LEN_S	16
#define HCLGE_CFG_RX_BUF_LEN_M	GENMASK(31, 16)
#define HCLGE_CFG_MAC_ADDR_H_S	0
#define HCLGE_CFG_MAC_ADDR_H_M	GENMASK(15, 0)
#define HCLGE_CFG_DEFAULT_SPEED_S	16
#define HCLGE_CFG_DEFAULT_SPEED_M	GENMASK(23, 16)
#define HCLGE_CFG_RSS_SIZE_S	24
#define HCLGE_CFG_RSS_SIZE_M	GENMASK(31, 24)
#define HCLGE_CFG_SPEED_ABILITY_S	0
#define HCLGE_CFG_SPEED_ABILITY_M	GENMASK(7, 0)
#define HCLGE_CFG_SPEED_ABILITY_EXT_S	10
#define HCLGE_CFG_SPEED_ABILITY_EXT_M	GENMASK(15, 10)
#define HCLGE_CFG_VLAN_FLTR_CAP_S	8
#define HCLGE_CFG_VLAN_FLTR_CAP_M	GENMASK(9, 8)
#define HCLGE_CFG_UMV_TBL_SPACE_S	16
#define HCLGE_CFG_UMV_TBL_SPACE_M	GENMASK(31, 16)
#define HCLGE_CFG_PF_RSS_SIZE_S		0
#define HCLGE_CFG_PF_RSS_SIZE_M		GENMASK(3, 0)
#define HCLGE_CFG_TX_SPARE_BUF_SIZE_S	4
#define HCLGE_CFG_TX_SPARE_BUF_SIZE_M	GENMASK(15, 4)

#define HCLGE_CFG_CMD_CNT		4

struct hclge_cfg_param_cmd {
	__le32 offset;
	__le32 rsv;
	__le32 param[HCLGE_CFG_CMD_CNT];
};

#define HCLGE_MAC_MODE		0x0
#define HCLGE_DESC_NUM		0x40

#define HCLGE_ALLOC_VALID_B	0
struct hclge_vf_num_cmd {
	u8 alloc_valid;
	u8 rsv[23];
};

#define HCLGE_RSS_DEFAULT_OUTPORT_B	4

#define HCLGE_RSS_CFG_TBL_SIZE_H	4
#define HCLGE_RSS_CFG_TBL_BW_L		8U

#define HCLGE_RSS_TC_OFFSET_S		0
#define HCLGE_RSS_TC_OFFSET_M		GENMASK(10, 0)
#define HCLGE_RSS_TC_SIZE_MSB_B		11
#define HCLGE_RSS_TC_SIZE_S		12
#define HCLGE_RSS_TC_SIZE_M		GENMASK(14, 12)
#define HCLGE_RSS_TC_SIZE_MSB_OFFSET	3
#define HCLGE_RSS_TC_VALID_B		15

#define HCLGE_LINK_STATUS_UP_B	0
#define HCLGE_LINK_STATUS_UP_M	BIT(HCLGE_LINK_STATUS_UP_B)
struct hclge_link_status_cmd {
	u8 status;
	u8 rsv[23];
};

/* for DEVICE_VERSION_V1/2, reference to promisc cmd byte8 */
#define HCLGE_PROMISC_EN_UC	1
#define HCLGE_PROMISC_EN_MC	2
#define HCLGE_PROMISC_EN_BC	3
#define HCLGE_PROMISC_TX_EN	4
#define HCLGE_PROMISC_RX_EN	5

/* for DEVICE_VERSION_V3, reference to promisc cmd byte10 */
#define HCLGE_PROMISC_UC_RX_EN	2
#define HCLGE_PROMISC_MC_RX_EN	3
#define HCLGE_PROMISC_BC_RX_EN	4
#define HCLGE_PROMISC_UC_TX_EN	5
#define HCLGE_PROMISC_MC_TX_EN	6
#define HCLGE_PROMISC_BC_TX_EN	7

struct hclge_promisc_cfg_cmd {
	u8 promisc;
	u8 vf_id;
	u8 extend_promisc;
	u8 rsv0[21];
};

enum hclge_promisc_type {
	HCLGE_UNICAST	= 1,
	HCLGE_MULTICAST	= 2,
	HCLGE_BROADCAST	= 3,
};

#define HCLGE_MAC_TX_EN_B	6
#define HCLGE_MAC_RX_EN_B	7
#define HCLGE_MAC_PAD_TX_B	11
#define HCLGE_MAC_PAD_RX_B	12
#define HCLGE_MAC_1588_TX_B	13
#define HCLGE_MAC_1588_RX_B	14
#define HCLGE_MAC_APP_LP_B	15
#define HCLGE_MAC_LINE_LP_B	16
#define HCLGE_MAC_FCS_TX_B	17
#define HCLGE_MAC_RX_OVERSIZE_TRUNCATE_B	18
#define HCLGE_MAC_RX_FCS_STRIP_B	19
#define HCLGE_MAC_RX_FCS_B	20
#define HCLGE_MAC_TX_UNDER_MIN_ERR_B		21
#define HCLGE_MAC_TX_OVERSIZE_TRUNCATE_B	22

struct hclge_config_mac_mode_cmd {
	__le32 txrx_pad_fcs_loop_en;
	u8 rsv[20];
};

struct hclge_pf_rst_sync_cmd {
#define HCLGE_PF_RST_ALL_VF_RDY_B	0
	u8 all_vf_ready;
	u8 rsv[23];
};

#define HCLGE_CFG_SPEED_S		0
#define HCLGE_CFG_SPEED_M		GENMASK(5, 0)

#define HCLGE_CFG_DUPLEX_B		7
#define HCLGE_CFG_DUPLEX_M		BIT(HCLGE_CFG_DUPLEX_B)

struct hclge_config_mac_speed_dup_cmd {
	u8 speed_dup;

#define HCLGE_CFG_MAC_SPEED_CHANGE_EN_B	0
	u8 mac_change_fec_en;
	u8 rsv[4];
	u8 lane_num;
	u8 rsv1[17];
};

#define HCLGE_TQP_ENABLE_B		0

#define HCLGE_MAC_CFG_AN_EN_B		0
#define HCLGE_MAC_CFG_AN_INT_EN_B	1
#define HCLGE_MAC_CFG_AN_INT_MSK_B	2
#define HCLGE_MAC_CFG_AN_INT_CLR_B	3
#define HCLGE_MAC_CFG_AN_RST_B		4

#define HCLGE_MAC_CFG_AN_EN	BIT(HCLGE_MAC_CFG_AN_EN_B)

struct hclge_config_auto_neg_cmd {
	__le32  cfg_an_cmd_flag;
	u8      rsv[20];
};

struct hclge_sfp_info_cmd {
	__le32 speed;
	u8 query_type; /* 0: sfp speed, 1: active speed */
	u8 active_fec;
	u8 autoneg; /* autoneg state */
	u8 autoneg_ability; /* whether support autoneg */
	__le32 speed_ability; /* speed ability for current media */
	__le32 module_type;
	u8 fec_ability;
	u8 lane_num;
	u8 rsv[6];
};

#define HCLGE_MAC_CFG_FEC_AUTO_EN_B	0
#define HCLGE_MAC_CFG_FEC_MODE_S	1
#define HCLGE_MAC_CFG_FEC_MODE_M	GENMASK(3, 1)
#define HCLGE_MAC_CFG_FEC_SET_DEF_B	0
#define HCLGE_MAC_CFG_FEC_CLR_DEF_B	1

#define HCLGE_MAC_FEC_OFF		0
#define HCLGE_MAC_FEC_BASER		1
#define HCLGE_MAC_FEC_RS		2
#define HCLGE_MAC_FEC_LLRS		3
struct hclge_config_fec_cmd {
	u8 fec_mode;
	u8 default_config;
	u8 rsv[22];
};

#define HCLGE_FEC_STATS_CMD_NUM 4

struct hclge_query_fec_stats_cmd {
	/* fec rs mode total stats */
	__le32 rs_fec_corr_blocks;
	__le32 rs_fec_uncorr_blocks;
	__le32 rs_fec_error_blocks;
	/* fec base-r mode per lanes stats */
	u8 base_r_lane_num;
	u8 rsv[3];
	__le32 base_r_fec_corr_blocks;
	__le32 base_r_fec_uncorr_blocks;
};

#define HCLGE_MAC_UPLINK_PORT		0x100

struct hclge_config_max_frm_size_cmd {
	__le16  max_frm_size;
	u8      min_frm_size;
	u8      rsv[21];
};

enum hclge_mac_vlan_tbl_opcode {
	HCLGE_MAC_VLAN_ADD,	/* Add new or modify mac_vlan */
	HCLGE_MAC_VLAN_UPDATE,  /* Modify other fields of this table */
	HCLGE_MAC_VLAN_REMOVE,  /* Remove a entry through mac_vlan key */
	HCLGE_MAC_VLAN_LKUP,    /* Lookup a entry through mac_vlan key */
};

enum hclge_mac_vlan_add_resp_code {
	HCLGE_ADD_UC_OVERFLOW = 2,	/* ADD failed for UC overflow */
	HCLGE_ADD_MC_OVERFLOW,		/* ADD failed for MC overflow */
};

#define HCLGE_MAC_VLAN_BIT0_EN_B	0
#define HCLGE_MAC_VLAN_BIT1_EN_B	1
#define HCLGE_MAC_EPORT_SW_EN_B		12
#define HCLGE_MAC_EPORT_TYPE_B		11
#define HCLGE_MAC_EPORT_VFID_S		3
#define HCLGE_MAC_EPORT_VFID_M		GENMASK(10, 3)
#define HCLGE_MAC_EPORT_PFID_S		0
#define HCLGE_MAC_EPORT_PFID_M		GENMASK(2, 0)
struct hclge_mac_vlan_tbl_entry_cmd {
	u8	flags;
	u8      resp_code;
	__le16  vlan_tag;
	__le32  mac_addr_hi32;
	__le16  mac_addr_lo16;
	__le16  rsv1;
	u8      entry_type;
	u8      mc_mac_en;
	__le16  egress_port;
	__le16  egress_queue;
	u8      rsv2[6];
};

#define HCLGE_UMV_SPC_ALC_B	0
struct hclge_umv_spc_alc_cmd {
	u8 allocate;
	u8 rsv1[3];
	__le32 space_size;
	u8 rsv2[16];
};

#define HCLGE_MAC_MGR_MASK_VLAN_B		BIT(0)
#define HCLGE_MAC_MGR_MASK_MAC_B		BIT(1)
#define HCLGE_MAC_MGR_MASK_ETHERTYPE_B		BIT(2)

struct hclge_mac_mgr_tbl_entry_cmd {
	u8      flags;
	u8      resp_code;
	__le16  vlan_tag;
	u8      mac_addr[ETH_ALEN];
	__le16  rsv1;
	__le16  ethter_type;
	__le16  egress_port;
	__le16  egress_queue;
	u8      sw_port_id_aware;
	u8      rsv2;
	u8      i_port_bitmap;
	u8      i_port_direction;
	u8      rsv3[2];
};

struct hclge_vlan_filter_ctrl_cmd {
	u8 vlan_type;
	u8 vlan_fe;
	u8 rsv1[2];
	u8 vf_id;
	u8 rsv2[19];
};

#define HCLGE_VLAN_ID_OFFSET_STEP	160
#define HCLGE_VLAN_BYTE_SIZE		8
#define	HCLGE_VLAN_OFFSET_BITMAP \
	(HCLGE_VLAN_ID_OFFSET_STEP / HCLGE_VLAN_BYTE_SIZE)

struct hclge_vlan_filter_pf_cfg_cmd {
	u8 vlan_offset;
	u8 vlan_cfg;
	u8 rsv[2];
	u8 vlan_offset_bitmap[HCLGE_VLAN_OFFSET_BITMAP];
};

#define HCLGE_MAX_VF_BYTES  16

struct hclge_vlan_filter_vf_cfg_cmd {
	__le16 vlan_id;
	u8  resp_code;
	u8  rsv;
	u8  vlan_cfg;
	u8  rsv1[3];
	u8  vf_bitmap[HCLGE_MAX_VF_BYTES];
};

#define HCLGE_INGRESS_BYPASS_B		0
struct hclge_port_vlan_filter_bypass_cmd {
	u8 bypass_state;
	u8 rsv1[3];
	u8 vf_id;
	u8 rsv2[19];
};

#define HCLGE_SWITCH_ANTI_SPOOF_B	0U
#define HCLGE_SWITCH_ALW_LPBK_B		1U
#define HCLGE_SWITCH_ALW_LCL_LPBK_B	2U
#define HCLGE_SWITCH_ALW_DST_OVRD_B	3U
#define HCLGE_SWITCH_NO_MASK		0x0
#define HCLGE_SWITCH_ANTI_SPOOF_MASK	0xFE
#define HCLGE_SWITCH_ALW_LPBK_MASK	0xFD
#define HCLGE_SWITCH_ALW_LCL_LPBK_MASK	0xFB
#define HCLGE_SWITCH_LW_DST_OVRD_MASK	0xF7

struct hclge_mac_vlan_switch_cmd {
	u8 roce_sel;
	u8 rsv1[3];
	__le32 func_id;
	u8 switch_param;
	u8 rsv2[3];
	u8 param_mask;
	u8 rsv3[11];
};

enum hclge_mac_vlan_cfg_sel {
	HCLGE_MAC_VLAN_NIC_SEL = 0,
	HCLGE_MAC_VLAN_ROCE_SEL,
};

#define HCLGE_ACCEPT_TAG1_B		0
#define HCLGE_ACCEPT_UNTAG1_B		1
#define HCLGE_PORT_INS_TAG1_EN_B	2
#define HCLGE_PORT_INS_TAG2_EN_B	3
#define HCLGE_CFG_NIC_ROCE_SEL_B	4
#define HCLGE_ACCEPT_TAG2_B		5
#define HCLGE_ACCEPT_UNTAG2_B		6
#define HCLGE_TAG_SHIFT_MODE_EN_B	7
#define HCLGE_VF_NUM_PER_BYTE		8

struct hclge_vport_vtag_tx_cfg_cmd {
	u8 vport_vlan_cfg;
	u8 vf_offset;
	u8 rsv1[2];
	__le16 def_vlan_tag1;
	__le16 def_vlan_tag2;
	u8 vf_bitmap[HCLGE_VF_NUM_PER_BYTE];
	u8 rsv2[8];
};

#define HCLGE_REM_TAG1_EN_B		0
#define HCLGE_REM_TAG2_EN_B		1
#define HCLGE_SHOW_TAG1_EN_B		2
#define HCLGE_SHOW_TAG2_EN_B		3
#define HCLGE_DISCARD_TAG1_EN_B		5
#define HCLGE_DISCARD_TAG2_EN_B		6
struct hclge_vport_vtag_rx_cfg_cmd {
	u8 vport_vlan_cfg;
	u8 vf_offset;
	u8 rsv1[6];
	u8 vf_bitmap[HCLGE_VF_NUM_PER_BYTE];
	u8 rsv2[8];
};

struct hclge_tx_vlan_type_cfg_cmd {
	__le16 ot_vlan_type;
	__le16 in_vlan_type;
	u8 rsv[20];
};

struct hclge_rx_vlan_type_cfg_cmd {
	__le16 ot_fst_vlan_type;
	__le16 ot_sec_vlan_type;
	__le16 in_fst_vlan_type;
	__le16 in_sec_vlan_type;
	u8 rsv[16];
};

struct hclge_cfg_com_tqp_queue_cmd {
	__le16 tqp_id;
	__le16 stream_id;
	u8 enable;
	u8 rsv[19];
};

struct hclge_cfg_tx_queue_pointer_cmd {
	__le16 tqp_id;
	__le16 tx_tail;
	__le16 tx_head;
	__le16 fbd_num;
	__le16 ring_offset;
	u8 rsv[14];
};

#pragma pack(1)
struct hclge_mac_ethertype_idx_rd_cmd {
	u8	flags;
	u8	resp_code;
	__le16  vlan_tag;
	u8      mac_addr[ETH_ALEN];
	__le16  index;
	__le16	ethter_type;
	__le16  egress_port;
	__le16  egress_queue;
	__le16  rev0;
	u8	i_port_bitmap;
	u8	i_port_direction;
	u8	rev1[2];
};

#pragma pack()

#define HCLGE_TSO_MSS_MIN_S	0
#define HCLGE_TSO_MSS_MIN_M	GENMASK(13, 0)

#define HCLGE_TSO_MSS_MAX_S	16
#define HCLGE_TSO_MSS_MAX_M	GENMASK(29, 16)

struct hclge_cfg_tso_status_cmd {
	__le16 tso_mss_min;
	__le16 tso_mss_max;
	u8 rsv[20];
};

#define HCLGE_GRO_EN_B		0
struct hclge_cfg_gro_status_cmd {
	u8 gro_en;
	u8 rsv[23];
};

#define HCLGE_TSO_MSS_MIN	256
#define HCLGE_TSO_MSS_MAX	9668

#define HCLGE_TQP_RESET_B	0
struct hclge_reset_tqp_queue_cmd {
	__le16 tqp_id;
	u8 reset_req;
	u8 ready_to_reset;
	u8 rsv[20];
};

#define HCLGE_CFG_RESET_MAC_B		3
#define HCLGE_CFG_RESET_FUNC_B		7
#define HCLGE_CFG_RESET_RCB_B		1
struct hclge_reset_cmd {
	u8 mac_func_reset;
	u8 fun_reset_vfid;
	u8 fun_reset_rcb;
	u8 rsv;
	__le16 fun_reset_rcb_vqid_start;
	__le16 fun_reset_rcb_vqid_num;
	u8 fun_reset_rcb_return_status;
	u8 rsv1[15];
};

#define HCLGE_PF_RESET_DONE_BIT		BIT(0)

struct hclge_pf_rst_done_cmd {
	u8 pf_rst_done;
	u8 rsv[23];
};

#define HCLGE_CMD_SERDES_SERIAL_INNER_LOOP_B	BIT(0)
#define HCLGE_CMD_SERDES_PARALLEL_INNER_LOOP_B	BIT(2)
#define HCLGE_CMD_GE_PHY_INNER_LOOP_B		BIT(3)
#define HCLGE_CMD_COMMON_LB_DONE_B		BIT(0)
#define HCLGE_CMD_COMMON_LB_SUCCESS_B		BIT(1)
struct hclge_common_lb_cmd {
	u8 mask;
	u8 enable;
	u8 result;
	u8 rsv[21];
};

#define HCLGE_DEFAULT_TX_BUF		0x4000	 /* 16k  bytes */
#define HCLGE_TOTAL_PKT_BUF		0x108000 /* 1.03125M bytes */
#define HCLGE_DEFAULT_DV		0xA000	 /* 40k byte */
#define HCLGE_DEFAULT_NON_DCB_DV	0x7800	/* 30K byte */
#define HCLGE_NON_DCB_ADDITIONAL_BUF	0x1400	/* 5120 byte */

#define HCLGE_LED_LOCATE_STATE_S	0
#define HCLGE_LED_LOCATE_STATE_M	GENMASK(1, 0)

struct hclge_set_led_state_cmd {
	u8 rsv1[3];
	u8 locate_led_config;
	u8 rsv2[20];
};

struct hclge_get_fd_mode_cmd {
	u8 mode;
	u8 enable;
	u8 rsv[22];
};

struct hclge_get_fd_allocation_cmd {
	__le32 stage1_entry_num;
	__le32 stage2_entry_num;
	__le16 stage1_counter_num;
	__le16 stage2_counter_num;
	u8 rsv[12];
};

struct hclge_set_fd_key_config_cmd {
	u8 stage;
	u8 key_select;
	u8 inner_sipv6_word_en;
	u8 inner_dipv6_word_en;
	u8 outer_sipv6_word_en;
	u8 outer_dipv6_word_en;
	u8 rsv1[2];
	__le32 tuple_mask;
	__le32 meta_data_mask;
	u8 rsv2[8];
};

#define HCLGE_FD_EPORT_SW_EN_B		0
struct hclge_fd_tcam_config_1_cmd {
	u8 stage;
	u8 xy_sel;
	u8 port_info;
	u8 rsv1[1];
	__le32 index;
	u8 entry_vld;
	u8 rsv2[7];
	u8 tcam_data[8];
};

struct hclge_fd_tcam_config_2_cmd {
	u8 tcam_data[24];
};

struct hclge_fd_tcam_config_3_cmd {
	u8 tcam_data[20];
	u8 rsv[4];
};

#define HCLGE_FD_AD_DROP_B		0
#define HCLGE_FD_AD_DIRECT_QID_B	1
#define HCLGE_FD_AD_QID_S		2
#define HCLGE_FD_AD_QID_M		GENMASK(11, 2)
#define HCLGE_FD_AD_USE_COUNTER_B	12
#define HCLGE_FD_AD_COUNTER_NUM_S	13
#define HCLGE_FD_AD_COUNTER_NUM_M	GENMASK(20, 13)
#define HCLGE_FD_AD_NXT_STEP_B		20
#define HCLGE_FD_AD_NXT_KEY_S		21
#define HCLGE_FD_AD_NXT_KEY_M		GENMASK(25, 21)
#define HCLGE_FD_AD_WR_RULE_ID_B	0
#define HCLGE_FD_AD_RULE_ID_S		1
#define HCLGE_FD_AD_RULE_ID_M		GENMASK(12, 1)
#define HCLGE_FD_AD_TC_OVRD_B		16
#define HCLGE_FD_AD_TC_SIZE_S		17
#define HCLGE_FD_AD_TC_SIZE_M		GENMASK(20, 17)

struct hclge_fd_ad_config_cmd {
	u8 stage;
	u8 rsv1[3];
	__le32 index;
	__le64 ad_data;
	u8 rsv2[8];
};

struct hclge_fd_ad_cnt_read_cmd {
	u8 rsv0[4];
	__le16 index;
	u8 rsv1[2];
	__le64 cnt;
	u8 rsv2[8];
};

#define HCLGE_FD_USER_DEF_OFT_S		0
#define HCLGE_FD_USER_DEF_OFT_M		GENMASK(14, 0)
#define HCLGE_FD_USER_DEF_EN_B		15
struct hclge_fd_user_def_cfg_cmd {
	__le16 ol2_cfg;
	__le16 l2_cfg;
	__le16 ol3_cfg;
	__le16 l3_cfg;
	__le16 ol4_cfg;
	__le16 l4_cfg;
	u8 rsv[12];
};

struct hclge_get_imp_bd_cmd {
	__le32 bd_num;
	u8 rsv[20];
};

struct hclge_query_ppu_pf_other_int_dfx_cmd {
	__le16 over_8bd_no_fe_qid;
	__le16 over_8bd_no_fe_vf_id;
	__le16 tso_mss_cmp_min_err_qid;
	__le16 tso_mss_cmp_min_err_vf_id;
	__le16 tso_mss_cmp_max_err_qid;
	__le16 tso_mss_cmp_max_err_vf_id;
	__le16 tx_rd_fbd_poison_qid;
	__le16 tx_rd_fbd_poison_vf_id;
	__le16 rx_rd_fbd_poison_qid;
	__le16 rx_rd_fbd_poison_vf_id;
	u8 rsv[4];
};

#define HCLGE_SFP_INFO_CMD_NUM	6
#define HCLGE_SFP_INFO_BD0_LEN	20
#define HCLGE_SFP_INFO_BDX_LEN	24
#define HCLGE_SFP_INFO_MAX_LEN \
	(HCLGE_SFP_INFO_BD0_LEN + \
	(HCLGE_SFP_INFO_CMD_NUM - 1) * HCLGE_SFP_INFO_BDX_LEN)

struct hclge_sfp_info_bd0_cmd {
	__le16 offset;
	__le16 read_len;
	u8 data[HCLGE_SFP_INFO_BD0_LEN];
};

#define HCLGE_QUERY_DEV_SPECS_BD_NUM		4

struct hclge_dev_specs_0_cmd {
	__le32 rsv0;
	__le32 mac_entry_num;
	__le32 mng_entry_num;
	__le16 rss_ind_tbl_size;
	__le16 rss_key_size;
	__le16 int_ql_max;
	u8 max_non_tso_bd_num;
	u8 rsv1;
	__le32 max_tm_rate;
};

#define HCLGE_DEF_MAX_INT_GL		0x1FE0U

struct hclge_dev_specs_1_cmd {
	__le16 max_frm_size;
	__le16 max_qset_num;
	__le16 max_int_gl;
	u8 rsv0[2];
	__le16 umv_size;
	__le16 mc_mac_size;
	u8 rsv1[6];
	u8 tnl_num;
	u8 rsv2[5];
};

/* mac speed type defined in firmware command */
enum HCLGE_FIRMWARE_MAC_SPEED {
	HCLGE_FW_MAC_SPEED_1G,
	HCLGE_FW_MAC_SPEED_10G,
	HCLGE_FW_MAC_SPEED_25G,
	HCLGE_FW_MAC_SPEED_40G,
	HCLGE_FW_MAC_SPEED_50G,
	HCLGE_FW_MAC_SPEED_100G,
	HCLGE_FW_MAC_SPEED_10M,
	HCLGE_FW_MAC_SPEED_100M,
	HCLGE_FW_MAC_SPEED_200G,
};

#define HCLGE_PHY_LINK_SETTING_BD_NUM		2

struct hclge_phy_link_ksetting_0_cmd {
	__le32 speed;
	u8 duplex;
	u8 autoneg;
	u8 eth_tp_mdix;
	u8 eth_tp_mdix_ctrl;
	u8 port;
	u8 transceiver;
	u8 phy_address;
	u8 rsv;
	__le32 supported;
	__le32 advertising;
	__le32 lp_advertising;
};

struct hclge_phy_link_ksetting_1_cmd {
	u8 master_slave_cfg;
	u8 master_slave_state;
	u8 rsv[22];
};

struct hclge_phy_reg_cmd {
	__le16 reg_addr;
	u8 rsv0[2];
	__le16 reg_val;
	u8 rsv1[18];
};

struct hclge_wol_cfg_cmd {
	__le32 wake_on_lan_mode;
	u8 sopass[SOPASS_MAX];
	u8 sopass_size;
	u8 rsv[13];
};

struct hclge_query_wol_supported_cmd {
	__le32 supported_wake_mode;
	u8 rsv[20];
};

struct hclge_hw;
int hclge_cmd_send(struct hclge_hw *hw, struct hclge_desc *desc, int num);
#endif
