/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2018-2019 Hisilicon Limited. */

#ifndef __HCLGE_DEBUGFS_H
#define __HCLGE_DEBUGFS_H

#include <linux/etherdevice.h>
#include "hclge_cmd.h"

#define HCLGE_DBG_MNG_TBL_MAX	   64

#define HCLGE_DBG_MNG_VLAN_MASK_B  BIT(0)
#define HCLGE_DBG_MNG_MAC_MASK_B   BIT(1)
#define HCLGE_DBG_MNG_ETHER_MASK_B BIT(2)
#define HCLGE_DBG_MNG_E_TYPE_B	   BIT(11)
#define HCLGE_DBG_MNG_DROP_B	   BIT(13)
#define HCLGE_DBG_MNG_VLAN_TAG	   0x0FFF
#define HCLGE_DBG_MNG_PF_ID	   0x0007
#define HCLGE_DBG_MNG_VF_ID	   0x00FF

/* Get DFX BD number offset */
#define HCLGE_DBG_DFX_BIOS_OFFSET  1
#define HCLGE_DBG_DFX_SSU_0_OFFSET 2
#define HCLGE_DBG_DFX_SSU_1_OFFSET 3
#define HCLGE_DBG_DFX_IGU_OFFSET   4
#define HCLGE_DBG_DFX_RPU_0_OFFSET 5

#define HCLGE_DBG_DFX_RPU_1_OFFSET 6
#define HCLGE_DBG_DFX_NCSI_OFFSET  7
#define HCLGE_DBG_DFX_RTC_OFFSET   8
#define HCLGE_DBG_DFX_PPP_OFFSET   9
#define HCLGE_DBG_DFX_RCB_OFFSET   10
#define HCLGE_DBG_DFX_TQP_OFFSET   11

#define HCLGE_DBG_DFX_SSU_2_OFFSET 12

struct hclge_qos_pri_map_cmd {
	u8 pri0_tc  : 4,
	   pri1_tc  : 4;
	u8 pri2_tc  : 4,
	   pri3_tc  : 4;
	u8 pri4_tc  : 4,
	   pri5_tc  : 4;
	u8 pri6_tc  : 4,
	   pri7_tc  : 4;
	u8 vlan_pri : 4,
	   rev	    : 4;
};

struct hclge_dbg_bitmap_cmd {
	union {
		u8 bitmap;
		struct {
			u8 bit0 : 1,
			   bit1 : 1,
			   bit2 : 1,
			   bit3 : 1,
			   bit4 : 1,
			   bit5 : 1,
			   bit6 : 1,
			   bit7 : 1;
		};
	};
};

struct hclge_dbg_reg_common_msg {
	int msg_num;
	int offset;
	enum hclge_opcode_type cmd;
};

struct hclge_dbg_tcam_msg {
	u8 stage;
	u32 loc;
};

#define	HCLGE_DBG_MAX_DFX_MSG_LEN	60
struct hclge_dbg_dfx_message {
	int flag;
	char message[HCLGE_DBG_MAX_DFX_MSG_LEN];
};

#define HCLGE_DBG_MAC_REG_TYPE_LEN	32
struct hclge_dbg_reg_type_info {
	enum hnae3_dbg_cmd cmd;
	const struct hclge_dbg_dfx_message *dfx_msg;
	struct hclge_dbg_reg_common_msg reg_msg;
};

struct hclge_dbg_func {
	enum hnae3_dbg_cmd cmd;
	int (*dbg_dump)(struct hclge_dev *hdev, char *buf, int len);
	int (*dbg_dump_reg)(struct hclge_dev *hdev, enum hnae3_dbg_cmd cmd,
			    char *buf, int len);
};

static const struct hclge_dbg_dfx_message hclge_dbg_bios_common_reg[] = {
	{false, "Reserved"},
	{true,	"BP_CPU_STATE"},
	{true,	"DFX_MSIX_INFO_NIC_0"},
	{true,	"DFX_MSIX_INFO_NIC_1"},
	{true,	"DFX_MSIX_INFO_NIC_2"},
	{true,	"DFX_MSIX_INFO_NIC_3"},

	{true,	"DFX_MSIX_INFO_ROC_0"},
	{true,	"DFX_MSIX_INFO_ROC_1"},
	{true,	"DFX_MSIX_INFO_ROC_2"},
	{true,	"DFX_MSIX_INFO_ROC_3"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_ssu_reg_0[] = {
	{false, "Reserved"},
	{true,	"SSU_ETS_PORT_STATUS"},
	{true,	"SSU_ETS_TCG_STATUS"},
	{false, "Reserved"},
	{false, "Reserved"},
	{true,	"SSU_BP_STATUS_0"},

	{true,	"SSU_BP_STATUS_1"},
	{true,	"SSU_BP_STATUS_2"},
	{true,	"SSU_BP_STATUS_3"},
	{true,	"SSU_BP_STATUS_4"},
	{true,	"SSU_BP_STATUS_5"},
	{true,	"SSU_MAC_TX_PFC_IND"},

	{true,	"MAC_SSU_RX_PFC_IND"},
	{true,	"BTMP_AGEING_ST_B0"},
	{true,	"BTMP_AGEING_ST_B1"},
	{true,	"BTMP_AGEING_ST_B2"},
	{false, "Reserved"},
	{false, "Reserved"},

	{true,	"FULL_DROP_NUM"},
	{true,	"PART_DROP_NUM"},
	{true,	"PPP_KEY_DROP_NUM"},
	{true,	"PPP_RLT_DROP_NUM"},
	{true,	"LO_PRI_UNICAST_RLT_DROP_NUM"},
	{true,	"HI_PRI_MULTICAST_RLT_DROP_NUM"},

	{true,	"LO_PRI_MULTICAST_RLT_DROP_NUM"},
	{true,	"NCSI_PACKET_CURR_BUFFER_CNT"},
	{true,	"BTMP_AGEING_RLS_CNT_BANK0"},
	{true,	"BTMP_AGEING_RLS_CNT_BANK1"},
	{true,	"BTMP_AGEING_RLS_CNT_BANK2"},
	{true,	"SSU_MB_RD_RLT_DROP_CNT"},

	{true,	"SSU_PPP_MAC_KEY_NUM_L"},
	{true,	"SSU_PPP_MAC_KEY_NUM_H"},
	{true,	"SSU_PPP_HOST_KEY_NUM_L"},
	{true,	"SSU_PPP_HOST_KEY_NUM_H"},
	{true,	"PPP_SSU_MAC_RLT_NUM_L"},
	{true,	"PPP_SSU_MAC_RLT_NUM_H"},

	{true,	"PPP_SSU_HOST_RLT_NUM_L"},
	{true,	"PPP_SSU_HOST_RLT_NUM_H"},
	{true,	"NCSI_RX_PACKET_IN_CNT_L"},
	{true,	"NCSI_RX_PACKET_IN_CNT_H"},
	{true,	"NCSI_TX_PACKET_OUT_CNT_L"},
	{true,	"NCSI_TX_PACKET_OUT_CNT_H"},

	{true,	"SSU_KEY_DROP_NUM"},
	{true,	"MB_UNCOPY_NUM"},
	{true,	"RX_OQ_DROP_PKT_CNT"},
	{true,	"TX_OQ_DROP_PKT_CNT"},
	{true,	"BANK_UNBALANCE_DROP_CNT"},
	{true,	"BANK_UNBALANCE_RX_DROP_CNT"},

	{true,	"NIC_L2_ERR_DROP_PKT_CNT"},
	{true,	"ROC_L2_ERR_DROP_PKT_CNT"},
	{true,	"NIC_L2_ERR_DROP_PKT_CNT_RX"},
	{true,	"ROC_L2_ERR_DROP_PKT_CNT_RX"},
	{true,	"RX_OQ_GLB_DROP_PKT_CNT"},
	{false, "Reserved"},

	{true,	"LO_PRI_UNICAST_CUR_CNT"},
	{true,	"HI_PRI_MULTICAST_CUR_CNT"},
	{true,	"LO_PRI_MULTICAST_CUR_CNT"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_ssu_reg_1[] = {
	{true,	"prt_id"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_0"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_1"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_2"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_3"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_4"},

	{true,	"PACKET_TC_CURR_BUFFER_CNT_5"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_6"},
	{true,	"PACKET_TC_CURR_BUFFER_CNT_7"},
	{true,	"PACKET_CURR_BUFFER_CNT"},
	{false, "Reserved"},
	{false, "Reserved"},

	{true,	"RX_PACKET_IN_CNT_L"},
	{true,	"RX_PACKET_IN_CNT_H"},
	{true,	"RX_PACKET_OUT_CNT_L"},
	{true,	"RX_PACKET_OUT_CNT_H"},
	{true,	"TX_PACKET_IN_CNT_L"},
	{true,	"TX_PACKET_IN_CNT_H"},

	{true,	"TX_PACKET_OUT_CNT_L"},
	{true,	"TX_PACKET_OUT_CNT_H"},
	{true,	"ROC_RX_PACKET_IN_CNT_L"},
	{true,	"ROC_RX_PACKET_IN_CNT_H"},
	{true,	"ROC_TX_PACKET_OUT_CNT_L"},
	{true,	"ROC_TX_PACKET_OUT_CNT_H"},

	{true,	"RX_PACKET_TC_IN_CNT_0_L"},
	{true,	"RX_PACKET_TC_IN_CNT_0_H"},
	{true,	"RX_PACKET_TC_IN_CNT_1_L"},
	{true,	"RX_PACKET_TC_IN_CNT_1_H"},
	{true,	"RX_PACKET_TC_IN_CNT_2_L"},
	{true,	"RX_PACKET_TC_IN_CNT_2_H"},

	{true,	"RX_PACKET_TC_IN_CNT_3_L"},
	{true,	"RX_PACKET_TC_IN_CNT_3_H"},
	{true,	"RX_PACKET_TC_IN_CNT_4_L"},
	{true,	"RX_PACKET_TC_IN_CNT_4_H"},
	{true,	"RX_PACKET_TC_IN_CNT_5_L"},
	{true,	"RX_PACKET_TC_IN_CNT_5_H"},

	{true,	"RX_PACKET_TC_IN_CNT_6_L"},
	{true,	"RX_PACKET_TC_IN_CNT_6_H"},
	{true,	"RX_PACKET_TC_IN_CNT_7_L"},
	{true,	"RX_PACKET_TC_IN_CNT_7_H"},
	{true,	"RX_PACKET_TC_OUT_CNT_0_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_0_H"},

	{true,	"RX_PACKET_TC_OUT_CNT_1_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_1_H"},
	{true,	"RX_PACKET_TC_OUT_CNT_2_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_2_H"},
	{true,	"RX_PACKET_TC_OUT_CNT_3_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_3_H"},

	{true,	"RX_PACKET_TC_OUT_CNT_4_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_4_H"},
	{true,	"RX_PACKET_TC_OUT_CNT_5_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_5_H"},
	{true,	"RX_PACKET_TC_OUT_CNT_6_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_6_H"},

	{true,	"RX_PACKET_TC_OUT_CNT_7_L"},
	{true,	"RX_PACKET_TC_OUT_CNT_7_H"},
	{true,	"TX_PACKET_TC_IN_CNT_0_L"},
	{true,	"TX_PACKET_TC_IN_CNT_0_H"},
	{true,	"TX_PACKET_TC_IN_CNT_1_L"},
	{true,	"TX_PACKET_TC_IN_CNT_1_H"},

	{true,	"TX_PACKET_TC_IN_CNT_2_L"},
	{true,	"TX_PACKET_TC_IN_CNT_2_H"},
	{true,	"TX_PACKET_TC_IN_CNT_3_L"},
	{true,	"TX_PACKET_TC_IN_CNT_3_H"},
	{true,	"TX_PACKET_TC_IN_CNT_4_L"},
	{true,	"TX_PACKET_TC_IN_CNT_4_H"},

	{true,	"TX_PACKET_TC_IN_CNT_5_L"},
	{true,	"TX_PACKET_TC_IN_CNT_5_H"},
	{true,	"TX_PACKET_TC_IN_CNT_6_L"},
	{true,	"TX_PACKET_TC_IN_CNT_6_H"},
	{true,	"TX_PACKET_TC_IN_CNT_7_L"},
	{true,	"TX_PACKET_TC_IN_CNT_7_H"},

	{true,	"TX_PACKET_TC_OUT_CNT_0_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_0_H"},
	{true,	"TX_PACKET_TC_OUT_CNT_1_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_1_H"},
	{true,	"TX_PACKET_TC_OUT_CNT_2_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_2_H"},

	{true,	"TX_PACKET_TC_OUT_CNT_3_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_3_H"},
	{true,	"TX_PACKET_TC_OUT_CNT_4_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_4_H"},
	{true,	"TX_PACKET_TC_OUT_CNT_5_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_5_H"},

	{true,	"TX_PACKET_TC_OUT_CNT_6_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_6_H"},
	{true,	"TX_PACKET_TC_OUT_CNT_7_L"},
	{true,	"TX_PACKET_TC_OUT_CNT_7_H"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_ssu_reg_2[] = {
	{true,	"OQ_INDEX"},
	{true,	"QUEUE_CNT"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_igu_egu_reg[] = {
	{true,	"prt_id"},
	{true,	"IGU_RX_ERR_PKT"},
	{true,	"IGU_RX_NO_SOF_PKT"},
	{true,	"EGU_TX_1588_SHORT_PKT"},
	{true,	"EGU_TX_1588_PKT"},
	{true,	"EGU_TX_ERR_PKT"},

	{true,	"IGU_RX_OUT_L2_PKT"},
	{true,	"IGU_RX_OUT_L3_PKT"},
	{true,	"IGU_RX_OUT_L4_PKT"},
	{true,	"IGU_RX_IN_L2_PKT"},
	{true,	"IGU_RX_IN_L3_PKT"},
	{true,	"IGU_RX_IN_L4_PKT"},

	{true,	"IGU_RX_EL3E_PKT"},
	{true,	"IGU_RX_EL4E_PKT"},
	{true,	"IGU_RX_L3E_PKT"},
	{true,	"IGU_RX_L4E_PKT"},
	{true,	"IGU_RX_ROCEE_PKT"},
	{true,	"IGU_RX_OUT_UDP0_PKT"},

	{true,	"IGU_RX_IN_UDP0_PKT"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},

	{true,	"IGU_RX_OVERSIZE_PKT_L"},
	{true,	"IGU_RX_OVERSIZE_PKT_H"},
	{true,	"IGU_RX_UNDERSIZE_PKT_L"},
	{true,	"IGU_RX_UNDERSIZE_PKT_H"},
	{true,	"IGU_RX_OUT_ALL_PKT_L"},
	{true,	"IGU_RX_OUT_ALL_PKT_H"},

	{true,	"IGU_TX_OUT_ALL_PKT_L"},
	{true,	"IGU_TX_OUT_ALL_PKT_H"},
	{true,	"IGU_RX_UNI_PKT_L"},
	{true,	"IGU_RX_UNI_PKT_H"},
	{true,	"IGU_RX_MULTI_PKT_L"},
	{true,	"IGU_RX_MULTI_PKT_H"},

	{true,	"IGU_RX_BROAD_PKT_L"},
	{true,	"IGU_RX_BROAD_PKT_H"},
	{true,	"EGU_TX_OUT_ALL_PKT_L"},
	{true,	"EGU_TX_OUT_ALL_PKT_H"},
	{true,	"EGU_TX_UNI_PKT_L"},
	{true,	"EGU_TX_UNI_PKT_H"},

	{true,	"EGU_TX_MULTI_PKT_L"},
	{true,	"EGU_TX_MULTI_PKT_H"},
	{true,	"EGU_TX_BROAD_PKT_L"},
	{true,	"EGU_TX_BROAD_PKT_H"},
	{true,	"IGU_TX_KEY_NUM_L"},
	{true,	"IGU_TX_KEY_NUM_H"},

	{true,	"IGU_RX_NON_TUN_PKT_L"},
	{true,	"IGU_RX_NON_TUN_PKT_H"},
	{true,	"IGU_RX_TUN_PKT_L"},
	{true,	"IGU_RX_TUN_PKT_H"},
	{false,	"Reserved"},
	{false,	"Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_rpu_reg_0[] = {
	{true, "tc_queue_num"},
	{true, "FSM_DFX_ST0"},
	{true, "FSM_DFX_ST1"},
	{true, "RPU_RX_PKT_DROP_CNT"},
	{true, "BUF_WAIT_TIMEOUT"},
	{true, "BUF_WAIT_TIMEOUT_QID"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_rpu_reg_1[] = {
	{false, "Reserved"},
	{true,	"FIFO_DFX_ST0"},
	{true,	"FIFO_DFX_ST1"},
	{true,	"FIFO_DFX_ST2"},
	{true,	"FIFO_DFX_ST3"},
	{true,	"FIFO_DFX_ST4"},

	{true,	"FIFO_DFX_ST5"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_ncsi_reg[] = {
	{false, "Reserved"},
	{true,	"NCSI_EGU_TX_FIFO_STS"},
	{true,	"NCSI_PAUSE_STATUS"},
	{true,	"NCSI_RX_CTRL_DMAC_ERR_CNT"},
	{true,	"NCSI_RX_CTRL_SMAC_ERR_CNT"},
	{true,	"NCSI_RX_CTRL_CKS_ERR_CNT"},

	{true,	"NCSI_RX_CTRL_PKT_CNT"},
	{true,	"NCSI_RX_PT_DMAC_ERR_CNT"},
	{true,	"NCSI_RX_PT_SMAC_ERR_CNT"},
	{true,	"NCSI_RX_PT_PKT_CNT"},
	{true,	"NCSI_RX_FCS_ERR_CNT"},
	{true,	"NCSI_TX_CTRL_DMAC_ERR_CNT"},

	{true,	"NCSI_TX_CTRL_SMAC_ERR_CNT"},
	{true,	"NCSI_TX_CTRL_PKT_CNT"},
	{true,	"NCSI_TX_PT_DMAC_ERR_CNT"},
	{true,	"NCSI_TX_PT_SMAC_ERR_CNT"},
	{true,	"NCSI_TX_PT_PKT_CNT"},
	{true,	"NCSI_TX_PT_PKT_TRUNC_CNT"},

	{true,	"NCSI_TX_PT_PKT_ERR_CNT"},
	{true,	"NCSI_TX_CTRL_PKT_ERR_CNT"},
	{true,	"NCSI_RX_CTRL_PKT_TRUNC_CNT"},
	{true,	"NCSI_RX_CTRL_PKT_CFLIT_CNT"},
	{false, "Reserved"},
	{false, "Reserved"},

	{true,	"NCSI_MAC_RX_OCTETS_OK"},
	{true,	"NCSI_MAC_RX_OCTETS_BAD"},
	{true,	"NCSI_MAC_RX_UC_PKTS"},
	{true,	"NCSI_MAC_RX_MC_PKTS"},
	{true,	"NCSI_MAC_RX_BC_PKTS"},
	{true,	"NCSI_MAC_RX_PKTS_64OCTETS"},

	{true,	"NCSI_MAC_RX_PKTS_65TO127OCTETS"},
	{true,	"NCSI_MAC_RX_PKTS_128TO255OCTETS"},
	{true,	"NCSI_MAC_RX_PKTS_255TO511OCTETS"},
	{true,	"NCSI_MAC_RX_PKTS_512TO1023OCTETS"},
	{true,	"NCSI_MAC_RX_PKTS_1024TO1518OCTETS"},
	{true,	"NCSI_MAC_RX_PKTS_1519TOMAXOCTETS"},

	{true,	"NCSI_MAC_RX_FCS_ERRORS"},
	{true,	"NCSI_MAC_RX_LONG_ERRORS"},
	{true,	"NCSI_MAC_RX_JABBER_ERRORS"},
	{true,	"NCSI_MAC_RX_RUNT_ERR_CNT"},
	{true,	"NCSI_MAC_RX_SHORT_ERR_CNT"},
	{true,	"NCSI_MAC_RX_FILT_PKT_CNT"},

	{true,	"NCSI_MAC_RX_OCTETS_TOTAL_FILT"},
	{true,	"NCSI_MAC_TX_OCTETS_OK"},
	{true,	"NCSI_MAC_TX_OCTETS_BAD"},
	{true,	"NCSI_MAC_TX_UC_PKTS"},
	{true,	"NCSI_MAC_TX_MC_PKTS"},
	{true,	"NCSI_MAC_TX_BC_PKTS"},

	{true,	"NCSI_MAC_TX_PKTS_64OCTETS"},
	{true,	"NCSI_MAC_TX_PKTS_65TO127OCTETS"},
	{true,	"NCSI_MAC_TX_PKTS_128TO255OCTETS"},
	{true,	"NCSI_MAC_TX_PKTS_256TO511OCTETS"},
	{true,	"NCSI_MAC_TX_PKTS_512TO1023OCTETS"},
	{true,	"NCSI_MAC_TX_PKTS_1024TO1518OCTETS"},

	{true,	"NCSI_MAC_TX_PKTS_1519TOMAXOCTETS"},
	{true,	"NCSI_MAC_TX_UNDERRUN"},
	{true,	"NCSI_MAC_TX_CRC_ERROR"},
	{true,	"NCSI_MAC_TX_PAUSE_FRAMES"},
	{true,	"NCSI_MAC_RX_PAD_PKTS"},
	{true,	"NCSI_MAC_RX_PAUSE_FRAMES"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_rtc_reg[] = {
	{false, "Reserved"},
	{true,	"LGE_IGU_AFIFO_DFX_0"},
	{true,	"LGE_IGU_AFIFO_DFX_1"},
	{true,	"LGE_IGU_AFIFO_DFX_2"},
	{true,	"LGE_IGU_AFIFO_DFX_3"},
	{true,	"LGE_IGU_AFIFO_DFX_4"},

	{true,	"LGE_IGU_AFIFO_DFX_5"},
	{true,	"LGE_IGU_AFIFO_DFX_6"},
	{true,	"LGE_IGU_AFIFO_DFX_7"},
	{true,	"LGE_EGU_AFIFO_DFX_0"},
	{true,	"LGE_EGU_AFIFO_DFX_1"},
	{true,	"LGE_EGU_AFIFO_DFX_2"},

	{true,	"LGE_EGU_AFIFO_DFX_3"},
	{true,	"LGE_EGU_AFIFO_DFX_4"},
	{true,	"LGE_EGU_AFIFO_DFX_5"},
	{true,	"LGE_EGU_AFIFO_DFX_6"},
	{true,	"LGE_EGU_AFIFO_DFX_7"},
	{true,	"CGE_IGU_AFIFO_DFX_0"},

	{true,	"CGE_IGU_AFIFO_DFX_1"},
	{true,	"CGE_EGU_AFIFO_DFX_0"},
	{true,	"CGE_EGU_AFIFO_DFX_1"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_ppp_reg[] = {
	{false, "Reserved"},
	{true,	"DROP_FROM_PRT_PKT_CNT"},
	{true,	"DROP_FROM_HOST_PKT_CNT"},
	{true,	"DROP_TX_VLAN_PROC_CNT"},
	{true,	"DROP_MNG_CNT"},
	{true,	"DROP_FD_CNT"},

	{true,	"DROP_NO_DST_CNT"},
	{true,	"DROP_MC_MBID_FULL_CNT"},
	{true,	"DROP_SC_FILTERED"},
	{true,	"PPP_MC_DROP_PKT_CNT"},
	{true,	"DROP_PT_CNT"},
	{true,	"DROP_MAC_ANTI_SPOOF_CNT"},

	{true,	"DROP_IG_VFV_CNT"},
	{true,	"DROP_IG_PRTV_CNT"},
	{true,	"DROP_CNM_PFC_PAUSE_CNT"},
	{true,	"DROP_TORUS_TC_CNT"},
	{true,	"DROP_TORUS_LPBK_CNT"},
	{true,	"PPP_HFS_STS"},

	{true,	"PPP_MC_RSLT_STS"},
	{true,	"PPP_P3U_STS"},
	{true,	"PPP_RSLT_DESCR_STS"},
	{true,	"PPP_UMV_STS_0"},
	{true,	"PPP_UMV_STS_1"},
	{true,	"PPP_VFV_STS"},

	{true,	"PPP_GRO_KEY_CNT"},
	{true,	"PPP_GRO_INFO_CNT"},
	{true,	"PPP_GRO_DROP_CNT"},
	{true,	"PPP_GRO_OUT_CNT"},
	{true,	"PPP_GRO_KEY_MATCH_DATA_CNT"},
	{true,	"PPP_GRO_KEY_MATCH_TCAM_CNT"},

	{true,	"PPP_GRO_INFO_MATCH_CNT"},
	{true,	"PPP_GRO_FREE_ENTRY_CNT"},
	{true,	"PPP_GRO_INNER_DFX_SIGNAL"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},

	{true,	"GET_RX_PKT_CNT_L"},
	{true,	"GET_RX_PKT_CNT_H"},
	{true,	"GET_TX_PKT_CNT_L"},
	{true,	"GET_TX_PKT_CNT_H"},
	{true,	"SEND_UC_PRT2HOST_PKT_CNT_L"},
	{true,	"SEND_UC_PRT2HOST_PKT_CNT_H"},

	{true,	"SEND_UC_PRT2PRT_PKT_CNT_L"},
	{true,	"SEND_UC_PRT2PRT_PKT_CNT_H"},
	{true,	"SEND_UC_HOST2HOST_PKT_CNT_L"},
	{true,	"SEND_UC_HOST2HOST_PKT_CNT_H"},
	{true,	"SEND_UC_HOST2PRT_PKT_CNT_L"},
	{true,	"SEND_UC_HOST2PRT_PKT_CNT_H"},

	{true,	"SEND_MC_FROM_PRT_CNT_L"},
	{true,	"SEND_MC_FROM_PRT_CNT_H"},
	{true,	"SEND_MC_FROM_HOST_CNT_L"},
	{true,	"SEND_MC_FROM_HOST_CNT_H"},
	{true,	"SSU_MC_RD_CNT_L"},
	{true,	"SSU_MC_RD_CNT_H"},

	{true,	"SSU_MC_DROP_CNT_L"},
	{true,	"SSU_MC_DROP_CNT_H"},
	{true,	"SSU_MC_RD_PKT_CNT_L"},
	{true,	"SSU_MC_RD_PKT_CNT_H"},
	{true,	"PPP_MC_2HOST_PKT_CNT_L"},
	{true,	"PPP_MC_2HOST_PKT_CNT_H"},

	{true,	"PPP_MC_2PRT_PKT_CNT_L"},
	{true,	"PPP_MC_2PRT_PKT_CNT_H"},
	{true,	"NTSNOS_PKT_CNT_L"},
	{true,	"NTSNOS_PKT_CNT_H"},
	{true,	"NTUP_PKT_CNT_L"},
	{true,	"NTUP_PKT_CNT_H"},

	{true,	"NTLCL_PKT_CNT_L"},
	{true,	"NTLCL_PKT_CNT_H"},
	{true,	"NTTGT_PKT_CNT_L"},
	{true,	"NTTGT_PKT_CNT_H"},
	{true,	"RTNS_PKT_CNT_L"},
	{true,	"RTNS_PKT_CNT_H"},

	{true,	"RTLPBK_PKT_CNT_L"},
	{true,	"RTLPBK_PKT_CNT_H"},
	{true,	"NR_PKT_CNT_L"},
	{true,	"NR_PKT_CNT_H"},
	{true,	"RR_PKT_CNT_L"},
	{true,	"RR_PKT_CNT_H"},

	{true,	"MNG_TBL_HIT_CNT_L"},
	{true,	"MNG_TBL_HIT_CNT_H"},
	{true,	"FD_TBL_HIT_CNT_L"},
	{true,	"FD_TBL_HIT_CNT_H"},
	{true,	"FD_LKUP_CNT_L"},
	{true,	"FD_LKUP_CNT_H"},

	{true,	"BC_HIT_CNT_L"},
	{true,	"BC_HIT_CNT_H"},
	{true,	"UM_TBL_UC_HIT_CNT_L"},
	{true,	"UM_TBL_UC_HIT_CNT_H"},
	{true,	"UM_TBL_MC_HIT_CNT_L"},
	{true,	"UM_TBL_MC_HIT_CNT_H"},

	{true,	"UM_TBL_VMDQ1_HIT_CNT_L"},
	{true,	"UM_TBL_VMDQ1_HIT_CNT_H"},
	{true,	"MTA_TBL_HIT_CNT_L"},
	{true,	"MTA_TBL_HIT_CNT_H"},
	{true,	"FWD_BONDING_HIT_CNT_L"},
	{true,	"FWD_BONDING_HIT_CNT_H"},

	{true,	"PROMIS_TBL_HIT_CNT_L"},
	{true,	"PROMIS_TBL_HIT_CNT_H"},
	{true,	"GET_TUNL_PKT_CNT_L"},
	{true,	"GET_TUNL_PKT_CNT_H"},
	{true,	"GET_BMC_PKT_CNT_L"},
	{true,	"GET_BMC_PKT_CNT_H"},

	{true,	"SEND_UC_PRT2BMC_PKT_CNT_L"},
	{true,	"SEND_UC_PRT2BMC_PKT_CNT_H"},
	{true,	"SEND_UC_HOST2BMC_PKT_CNT_L"},
	{true,	"SEND_UC_HOST2BMC_PKT_CNT_H"},
	{true,	"SEND_UC_BMC2HOST_PKT_CNT_L"},
	{true,	"SEND_UC_BMC2HOST_PKT_CNT_H"},

	{true,	"SEND_UC_BMC2PRT_PKT_CNT_L"},
	{true,	"SEND_UC_BMC2PRT_PKT_CNT_H"},
	{true,	"PPP_MC_2BMC_PKT_CNT_L"},
	{true,	"PPP_MC_2BMC_PKT_CNT_H"},
	{true,	"VLAN_MIRR_CNT_L"},
	{true,	"VLAN_MIRR_CNT_H"},

	{true,	"IG_MIRR_CNT_L"},
	{true,	"IG_MIRR_CNT_H"},
	{true,	"EG_MIRR_CNT_L"},
	{true,	"EG_MIRR_CNT_H"},
	{true,	"RX_DEFAULT_HOST_HIT_CNT_L"},
	{true,	"RX_DEFAULT_HOST_HIT_CNT_H"},

	{true,	"LAN_PAIR_CNT_L"},
	{true,	"LAN_PAIR_CNT_H"},
	{true,	"UM_TBL_MC_HIT_PKT_CNT_L"},
	{true,	"UM_TBL_MC_HIT_PKT_CNT_H"},
	{true,	"MTA_TBL_HIT_PKT_CNT_L"},
	{true,	"MTA_TBL_HIT_PKT_CNT_H"},

	{true,	"PROMIS_TBL_HIT_PKT_CNT_L"},
	{true,	"PROMIS_TBL_HIT_PKT_CNT_H"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_rcb_reg[] = {
	{false, "Reserved"},
	{true,	"FSM_DFX_ST0"},
	{true,	"FSM_DFX_ST1"},
	{true,	"FSM_DFX_ST2"},
	{true,	"FIFO_DFX_ST0"},
	{true,	"FIFO_DFX_ST1"},

	{true,	"FIFO_DFX_ST2"},
	{true,	"FIFO_DFX_ST3"},
	{true,	"FIFO_DFX_ST4"},
	{true,	"FIFO_DFX_ST5"},
	{true,	"FIFO_DFX_ST6"},
	{true,	"FIFO_DFX_ST7"},

	{true,	"FIFO_DFX_ST8"},
	{true,	"FIFO_DFX_ST9"},
	{true,	"FIFO_DFX_ST10"},
	{true,	"FIFO_DFX_ST11"},
	{true,	"Q_CREDIT_VLD_0"},
	{true,	"Q_CREDIT_VLD_1"},

	{true,	"Q_CREDIT_VLD_2"},
	{true,	"Q_CREDIT_VLD_3"},
	{true,	"Q_CREDIT_VLD_4"},
	{true,	"Q_CREDIT_VLD_5"},
	{true,	"Q_CREDIT_VLD_6"},
	{true,	"Q_CREDIT_VLD_7"},

	{true,	"Q_CREDIT_VLD_8"},
	{true,	"Q_CREDIT_VLD_9"},
	{true,	"Q_CREDIT_VLD_10"},
	{true,	"Q_CREDIT_VLD_11"},
	{true,	"Q_CREDIT_VLD_12"},
	{true,	"Q_CREDIT_VLD_13"},

	{true,	"Q_CREDIT_VLD_14"},
	{true,	"Q_CREDIT_VLD_15"},
	{true,	"Q_CREDIT_VLD_16"},
	{true,	"Q_CREDIT_VLD_17"},
	{true,	"Q_CREDIT_VLD_18"},
	{true,	"Q_CREDIT_VLD_19"},

	{true,	"Q_CREDIT_VLD_20"},
	{true,	"Q_CREDIT_VLD_21"},
	{true,	"Q_CREDIT_VLD_22"},
	{true,	"Q_CREDIT_VLD_23"},
	{true,	"Q_CREDIT_VLD_24"},
	{true,	"Q_CREDIT_VLD_25"},

	{true,	"Q_CREDIT_VLD_26"},
	{true,	"Q_CREDIT_VLD_27"},
	{true,	"Q_CREDIT_VLD_28"},
	{true,	"Q_CREDIT_VLD_29"},
	{true,	"Q_CREDIT_VLD_30"},
	{true,	"Q_CREDIT_VLD_31"},

	{true,	"GRO_BD_SERR_CNT"},
	{true,	"GRO_CONTEXT_SERR_CNT"},
	{true,	"RX_STASH_CFG_SERR_CNT"},
	{true,	"AXI_RD_FBD_SERR_CNT"},
	{true,	"GRO_BD_MERR_CNT"},
	{true,	"GRO_CONTEXT_MERR_CNT"},

	{true,	"RX_STASH_CFG_MERR_CNT"},
	{true,	"AXI_RD_FBD_MERR_CNT"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
	{false, "Reserved"},
};

static const struct hclge_dbg_dfx_message hclge_dbg_tqp_reg[] = {
	{true, "q_num"},
	{true, "RCB_CFG_RX_RING_TAIL"},
	{true, "RCB_CFG_RX_RING_HEAD"},
	{true, "RCB_CFG_RX_RING_FBDNUM"},
	{true, "RCB_CFG_RX_RING_OFFSET"},
	{true, "RCB_CFG_RX_RING_FBDOFFSET"},

	{true, "RCB_CFG_RX_RING_PKTNUM_RECORD"},
	{true, "RCB_CFG_TX_RING_TAIL"},
	{true, "RCB_CFG_TX_RING_HEAD"},
	{true, "RCB_CFG_TX_RING_FBDNUM"},
	{true, "RCB_CFG_TX_RING_OFFSET"},
	{true, "RCB_CFG_TX_RING_EBDNUM"},
};

#define HCLGE_DBG_INFO_LEN			256
#define HCLGE_DBG_ID_LEN			16
#define HCLGE_DBG_ITEM_NAME_LEN			32
#define HCLGE_DBG_DATA_STR_LEN			32
#define HCLGE_DBG_TM_INFO_LEN			256

#define HCLGE_BILLION_NANO_SECONDS	1000000000

struct hclge_dbg_item {
	char name[HCLGE_DBG_ITEM_NAME_LEN];
	u16 interval; /* blank numbers after the item */
};

#endif
