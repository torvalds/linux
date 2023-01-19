/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_MAC_H__
#define __RTW89_MAC_H__

#include "core.h"
#include "reg.h"

#define MAC_MEM_DUMP_PAGE_SIZE 0x40000
#define ADDR_CAM_ENT_SIZE  0x40
#define BSSID_CAM_ENT_SIZE 0x08
#define HFC_PAGE_UNIT 64
#define RPWM_TRY_CNT 3

enum rtw89_mac_hwmod_sel {
	RTW89_DMAC_SEL = 0,
	RTW89_CMAC_SEL = 1,

	RTW89_MAC_INVALID,
};

enum rtw89_mac_fwd_target {
	RTW89_FWD_DONT_CARE    = 0,
	RTW89_FWD_TO_HOST      = 1,
	RTW89_FWD_TO_WLAN_CPU  = 2
};

enum rtw89_mac_wd_dma_intvl {
	RTW89_MAC_WD_DMA_INTVL_0S,
	RTW89_MAC_WD_DMA_INTVL_256NS,
	RTW89_MAC_WD_DMA_INTVL_512NS,
	RTW89_MAC_WD_DMA_INTVL_768NS,
	RTW89_MAC_WD_DMA_INTVL_1US,
	RTW89_MAC_WD_DMA_INTVL_1_5US,
	RTW89_MAC_WD_DMA_INTVL_2US,
	RTW89_MAC_WD_DMA_INTVL_4US,
	RTW89_MAC_WD_DMA_INTVL_8US,
	RTW89_MAC_WD_DMA_INTVL_16US,
	RTW89_MAC_WD_DMA_INTVL_DEF = 0xFE
};

enum rtw89_mac_multi_tag_num {
	RTW89_MAC_TAG_NUM_1,
	RTW89_MAC_TAG_NUM_2,
	RTW89_MAC_TAG_NUM_3,
	RTW89_MAC_TAG_NUM_4,
	RTW89_MAC_TAG_NUM_5,
	RTW89_MAC_TAG_NUM_6,
	RTW89_MAC_TAG_NUM_7,
	RTW89_MAC_TAG_NUM_8,
	RTW89_MAC_TAG_NUM_DEF = 0xFE
};

enum rtw89_mac_lbc_tmr {
	RTW89_MAC_LBC_TMR_8US = 0,
	RTW89_MAC_LBC_TMR_16US,
	RTW89_MAC_LBC_TMR_32US,
	RTW89_MAC_LBC_TMR_64US,
	RTW89_MAC_LBC_TMR_128US,
	RTW89_MAC_LBC_TMR_256US,
	RTW89_MAC_LBC_TMR_512US,
	RTW89_MAC_LBC_TMR_1MS,
	RTW89_MAC_LBC_TMR_2MS,
	RTW89_MAC_LBC_TMR_4MS,
	RTW89_MAC_LBC_TMR_8MS,
	RTW89_MAC_LBC_TMR_DEF = 0xFE
};

enum rtw89_mac_cpuio_op_cmd_type {
	CPUIO_OP_CMD_GET_1ST_PID = 0,
	CPUIO_OP_CMD_GET_NEXT_PID = 1,
	CPUIO_OP_CMD_ENQ_TO_TAIL = 4,
	CPUIO_OP_CMD_ENQ_TO_HEAD = 5,
	CPUIO_OP_CMD_DEQ = 8,
	CPUIO_OP_CMD_DEQ_ENQ_ALL = 9,
	CPUIO_OP_CMD_DEQ_ENQ_TO_TAIL = 12
};

enum rtw89_mac_wde_dle_port_id {
	WDE_DLE_PORT_ID_DISPATCH = 0,
	WDE_DLE_PORT_ID_PKTIN = 1,
	WDE_DLE_PORT_ID_CMAC0 = 3,
	WDE_DLE_PORT_ID_CMAC1 = 4,
	WDE_DLE_PORT_ID_CPU_IO = 6,
	WDE_DLE_PORT_ID_WDRLS = 7,
	WDE_DLE_PORT_ID_END = 8
};

enum rtw89_mac_wde_dle_queid_wdrls {
	WDE_DLE_QUEID_TXOK = 0,
	WDE_DLE_QUEID_DROP_RETRY_LIMIT = 1,
	WDE_DLE_QUEID_DROP_LIFETIME_TO = 2,
	WDE_DLE_QUEID_DROP_MACID_DROP = 3,
	WDE_DLE_QUEID_NO_REPORT = 4
};

enum rtw89_mac_ple_dle_port_id {
	PLE_DLE_PORT_ID_DISPATCH = 0,
	PLE_DLE_PORT_ID_MPDU = 1,
	PLE_DLE_PORT_ID_SEC = 2,
	PLE_DLE_PORT_ID_CMAC0 = 3,
	PLE_DLE_PORT_ID_CMAC1 = 4,
	PLE_DLE_PORT_ID_WDRLS = 5,
	PLE_DLE_PORT_ID_CPU_IO = 6,
	PLE_DLE_PORT_ID_PLRLS = 7,
	PLE_DLE_PORT_ID_END = 8
};

enum rtw89_mac_ple_dle_queid_plrls {
	PLE_DLE_QUEID_NO_REPORT = 0x0
};

enum rtw89_machdr_frame_type {
	RTW89_MGNT = 0,
	RTW89_CTRL = 1,
	RTW89_DATA = 2,
};

enum rtw89_mac_dle_dfi_type {
	DLE_DFI_TYPE_FREEPG	= 0,
	DLE_DFI_TYPE_QUOTA	= 1,
	DLE_DFI_TYPE_PAGELLT	= 2,
	DLE_DFI_TYPE_PKTINFO	= 3,
	DLE_DFI_TYPE_PREPKTLLT	= 4,
	DLE_DFI_TYPE_NXTPKTLLT	= 5,
	DLE_DFI_TYPE_QLNKTBL	= 6,
	DLE_DFI_TYPE_QEMPTY	= 7,
};

enum rtw89_mac_dle_wde_quota_id {
	WDE_QTAID_HOST_IF = 0,
	WDE_QTAID_WLAN_CPU = 1,
	WDE_QTAID_DATA_CPU = 2,
	WDE_QTAID_PKTIN = 3,
	WDE_QTAID_CPUIO = 4,
};

enum rtw89_mac_dle_ple_quota_id {
	PLE_QTAID_B0_TXPL = 0,
	PLE_QTAID_B1_TXPL = 1,
	PLE_QTAID_C2H = 2,
	PLE_QTAID_H2C = 3,
	PLE_QTAID_WLAN_CPU = 4,
	PLE_QTAID_MPDU = 5,
	PLE_QTAID_CMAC0_RX = 6,
	PLE_QTAID_CMAC1_RX = 7,
	PLE_QTAID_CMAC1_BBRPT = 8,
	PLE_QTAID_WDRLS = 9,
	PLE_QTAID_CPUIO = 10,
};

enum rtw89_mac_dle_ctrl_type {
	DLE_CTRL_TYPE_WDE = 0,
	DLE_CTRL_TYPE_PLE = 1,
	DLE_CTRL_TYPE_NUM = 2,
};

enum rtw89_mac_ax_l0_to_l1_event {
	MAC_AX_L0_TO_L1_CHIF_IDLE = 0,
	MAC_AX_L0_TO_L1_CMAC_DMA_IDLE = 1,
	MAC_AX_L0_TO_L1_RLS_PKID = 2,
	MAC_AX_L0_TO_L1_PTCL_IDLE = 3,
	MAC_AX_L0_TO_L1_RX_QTA_LOST = 4,
	MAC_AX_L0_TO_L1_DLE_STAT_HANG = 5,
	MAC_AX_L0_TO_L1_PCIE_STUCK = 6,
	MAC_AX_L0_TO_L1_EVENT_MAX = 15,
};

#define RTW89_PORT_OFFSET_MS_TO_32US(n, shift_ms) ((n) * (shift_ms) * 1000 / 32)

enum rtw89_mac_dbg_port_sel {
	/* CMAC 0 related */
	RTW89_DBG_PORT_SEL_PTCL_C0 = 0,
	RTW89_DBG_PORT_SEL_SCH_C0,
	RTW89_DBG_PORT_SEL_TMAC_C0,
	RTW89_DBG_PORT_SEL_RMAC_C0,
	RTW89_DBG_PORT_SEL_RMACST_C0,
	RTW89_DBG_PORT_SEL_RMAC_PLCP_C0,
	RTW89_DBG_PORT_SEL_TRXPTCL_C0,
	RTW89_DBG_PORT_SEL_TX_INFOL_C0,
	RTW89_DBG_PORT_SEL_TX_INFOH_C0,
	RTW89_DBG_PORT_SEL_TXTF_INFOL_C0,
	RTW89_DBG_PORT_SEL_TXTF_INFOH_C0,
	/* CMAC 1 related */
	RTW89_DBG_PORT_SEL_PTCL_C1,
	RTW89_DBG_PORT_SEL_SCH_C1,
	RTW89_DBG_PORT_SEL_TMAC_C1,
	RTW89_DBG_PORT_SEL_RMAC_C1,
	RTW89_DBG_PORT_SEL_RMACST_C1,
	RTW89_DBG_PORT_SEL_RMAC_PLCP_C1,
	RTW89_DBG_PORT_SEL_TRXPTCL_C1,
	RTW89_DBG_PORT_SEL_TX_INFOL_C1,
	RTW89_DBG_PORT_SEL_TX_INFOH_C1,
	RTW89_DBG_PORT_SEL_TXTF_INFOL_C1,
	RTW89_DBG_PORT_SEL_TXTF_INFOH_C1,
	/* DLE related */
	RTW89_DBG_PORT_SEL_WDE_BUFMGN_FREEPG,
	RTW89_DBG_PORT_SEL_WDE_BUFMGN_QUOTA,
	RTW89_DBG_PORT_SEL_WDE_BUFMGN_PAGELLT,
	RTW89_DBG_PORT_SEL_WDE_BUFMGN_PKTINFO,
	RTW89_DBG_PORT_SEL_WDE_QUEMGN_PREPKT,
	RTW89_DBG_PORT_SEL_WDE_QUEMGN_NXTPKT,
	RTW89_DBG_PORT_SEL_WDE_QUEMGN_QLNKTBL,
	RTW89_DBG_PORT_SEL_WDE_QUEMGN_QEMPTY,
	RTW89_DBG_PORT_SEL_PLE_BUFMGN_FREEPG,
	RTW89_DBG_PORT_SEL_PLE_BUFMGN_QUOTA,
	RTW89_DBG_PORT_SEL_PLE_BUFMGN_PAGELLT,
	RTW89_DBG_PORT_SEL_PLE_BUFMGN_PKTINFO,
	RTW89_DBG_PORT_SEL_PLE_QUEMGN_PREPKT,
	RTW89_DBG_PORT_SEL_PLE_QUEMGN_NXTPKT,
	RTW89_DBG_PORT_SEL_PLE_QUEMGN_QLNKTBL,
	RTW89_DBG_PORT_SEL_PLE_QUEMGN_QEMPTY,
	RTW89_DBG_PORT_SEL_PKTINFO,
	/* DISPATCHER related */
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX0,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX1,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX2,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX3,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX4,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX5,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX6,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX7,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX8,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TX9,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TXA,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TXB,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TXC,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TXD,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TXE,
	RTW89_DBG_PORT_SEL_DSPT_HDT_TXF,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX0,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX1,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX3,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX4,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX5,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX6,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX7,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX8,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TX9,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TXA,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TXB,
	RTW89_DBG_PORT_SEL_DSPT_CDT_TXC,
	RTW89_DBG_PORT_SEL_DSPT_HDT_RX0,
	RTW89_DBG_PORT_SEL_DSPT_HDT_RX1,
	RTW89_DBG_PORT_SEL_DSPT_HDT_RX2,
	RTW89_DBG_PORT_SEL_DSPT_HDT_RX3,
	RTW89_DBG_PORT_SEL_DSPT_HDT_RX4,
	RTW89_DBG_PORT_SEL_DSPT_HDT_RX5,
	RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0,
	RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0_0,
	RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0_1,
	RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P0_2,
	RTW89_DBG_PORT_SEL_DSPT_CDT_RX_P1,
	RTW89_DBG_PORT_SEL_DSPT_STF_CTRL,
	RTW89_DBG_PORT_SEL_DSPT_ADDR_CTRL,
	RTW89_DBG_PORT_SEL_DSPT_WDE_INTF,
	RTW89_DBG_PORT_SEL_DSPT_PLE_INTF,
	RTW89_DBG_PORT_SEL_DSPT_FLOW_CTRL,
	/* PCIE related */
	RTW89_DBG_PORT_SEL_PCIE_TXDMA,
	RTW89_DBG_PORT_SEL_PCIE_RXDMA,
	RTW89_DBG_PORT_SEL_PCIE_CVT,
	RTW89_DBG_PORT_SEL_PCIE_CXPL,
	RTW89_DBG_PORT_SEL_PCIE_IO,
	RTW89_DBG_PORT_SEL_PCIE_MISC,
	RTW89_DBG_PORT_SEL_PCIE_MISC2,

	/* keep last */
	RTW89_DBG_PORT_SEL_LAST,
	RTW89_DBG_PORT_SEL_MAX = RTW89_DBG_PORT_SEL_LAST,
	RTW89_DBG_PORT_SEL_INVALID = RTW89_DBG_PORT_SEL_LAST,
};

/* SRAM mem dump */
#define R_AX_INDIR_ACCESS_ENTRY 0x40000

#define	AXIDMA_BASE_ADDR		0x18006000
#define	STA_SCHED_BASE_ADDR		0x18808000
#define	RXPLD_FLTR_CAM_BASE_ADDR	0x18813000
#define	SECURITY_CAM_BASE_ADDR		0x18814000
#define	WOW_CAM_BASE_ADDR		0x18815000
#define	CMAC_TBL_BASE_ADDR		0x18840000
#define	ADDR_CAM_BASE_ADDR		0x18850000
#define	BSSID_CAM_BASE_ADDR		0x18853000
#define	BA_CAM_BASE_ADDR		0x18854000
#define	BCN_IE_CAM0_BASE_ADDR		0x18855000
#define	SHARED_BUF_BASE_ADDR		0x18700000
#define	DMAC_TBL_BASE_ADDR		0x18800000
#define	SHCUT_MACHDR_BASE_ADDR		0x18800800
#define	BCN_IE_CAM1_BASE_ADDR		0x188A0000
#define	TXD_FIFO_0_BASE_ADDR		0x18856200
#define	TXD_FIFO_1_BASE_ADDR		0x188A1080
#define	TXD_FIFO_0_BASE_ADDR_V1		0x18856400 /* for 8852C */
#define	TXD_FIFO_1_BASE_ADDR_V1		0x188A1080 /* for 8852C */
#define	TXDATA_FIFO_0_BASE_ADDR		0x18856000
#define	TXDATA_FIFO_1_BASE_ADDR		0x188A1000
#define	CPU_LOCAL_BASE_ADDR		0x18003000

#define CCTL_INFO_SIZE		32

enum rtw89_mac_mem_sel {
	RTW89_MAC_MEM_AXIDMA,
	RTW89_MAC_MEM_SHARED_BUF,
	RTW89_MAC_MEM_DMAC_TBL,
	RTW89_MAC_MEM_SHCUT_MACHDR,
	RTW89_MAC_MEM_STA_SCHED,
	RTW89_MAC_MEM_RXPLD_FLTR_CAM,
	RTW89_MAC_MEM_SECURITY_CAM,
	RTW89_MAC_MEM_WOW_CAM,
	RTW89_MAC_MEM_CMAC_TBL,
	RTW89_MAC_MEM_ADDR_CAM,
	RTW89_MAC_MEM_BA_CAM,
	RTW89_MAC_MEM_BCN_IE_CAM0,
	RTW89_MAC_MEM_BCN_IE_CAM1,
	RTW89_MAC_MEM_TXD_FIFO_0,
	RTW89_MAC_MEM_TXD_FIFO_1,
	RTW89_MAC_MEM_TXDATA_FIFO_0,
	RTW89_MAC_MEM_TXDATA_FIFO_1,
	RTW89_MAC_MEM_CPU_LOCAL,
	RTW89_MAC_MEM_BSSID_CAM,
	RTW89_MAC_MEM_TXD_FIFO_0_V1,
	RTW89_MAC_MEM_TXD_FIFO_1_V1,

	/* keep last */
	RTW89_MAC_MEM_NUM,
};

extern const u32 rtw89_mac_mem_base_addrs[];

enum rtw89_rpwm_req_pwr_state {
	RTW89_MAC_RPWM_REQ_PWR_STATE_ACTIVE = 0,
	RTW89_MAC_RPWM_REQ_PWR_STATE_BAND0_RFON = 1,
	RTW89_MAC_RPWM_REQ_PWR_STATE_BAND1_RFON = 2,
	RTW89_MAC_RPWM_REQ_PWR_STATE_BAND0_RFOFF = 3,
	RTW89_MAC_RPWM_REQ_PWR_STATE_BAND1_RFOFF = 4,
	RTW89_MAC_RPWM_REQ_PWR_STATE_CLK_GATED = 5,
	RTW89_MAC_RPWM_REQ_PWR_STATE_PWR_GATED = 6,
	RTW89_MAC_RPWM_REQ_PWR_STATE_HIOE_PWR_GATED = 7,
	RTW89_MAC_RPWM_REQ_PWR_STATE_MAX,
};

struct rtw89_pwr_cfg {
	u16 addr;
	u8 cv_msk;
	u8 intf_msk;
	u8 base:4;
	u8 cmd:4;
	u8 msk;
	u8 val;
};

enum rtw89_mac_c2h_ofld_func {
	RTW89_MAC_C2H_FUNC_EFUSE_DUMP,
	RTW89_MAC_C2H_FUNC_READ_RSP,
	RTW89_MAC_C2H_FUNC_PKT_OFLD_RSP,
	RTW89_MAC_C2H_FUNC_BCN_RESEND,
	RTW89_MAC_C2H_FUNC_MACID_PAUSE,
	RTW89_MAC_C2H_FUNC_TSF32_TOGL_RPT = 0x6,
	RTW89_MAC_C2H_FUNC_SCANOFLD_RSP = 0x9,
	RTW89_MAC_C2H_FUNC_OFLD_MAX,
};

enum rtw89_mac_c2h_info_func {
	RTW89_MAC_C2H_FUNC_REC_ACK,
	RTW89_MAC_C2H_FUNC_DONE_ACK,
	RTW89_MAC_C2H_FUNC_C2H_LOG,
	RTW89_MAC_C2H_FUNC_BCN_CNT,
	RTW89_MAC_C2H_FUNC_INFO_MAX,
};

enum rtw89_mac_c2h_mcc_func {
	RTW89_MAC_C2H_FUNC_MCC_RCV_ACK = 0,
	RTW89_MAC_C2H_FUNC_MCC_REQ_ACK = 1,
	RTW89_MAC_C2H_FUNC_MCC_TSF_RPT = 2,
	RTW89_MAC_C2H_FUNC_MCC_STATUS_RPT = 3,

	NUM_OF_RTW89_MAC_C2H_FUNC_MCC,
};

enum rtw89_mac_c2h_class {
	RTW89_MAC_C2H_CLASS_INFO,
	RTW89_MAC_C2H_CLASS_OFLD,
	RTW89_MAC_C2H_CLASS_TWT,
	RTW89_MAC_C2H_CLASS_WOW,
	RTW89_MAC_C2H_CLASS_MCC,
	RTW89_MAC_C2H_CLASS_FWDBG,
	RTW89_MAC_C2H_CLASS_MAX,
};

enum rtw89_mac_mcc_status {
	RTW89_MAC_MCC_ADD_ROLE_OK = 0,
	RTW89_MAC_MCC_START_GROUP_OK = 1,
	RTW89_MAC_MCC_STOP_GROUP_OK = 2,
	RTW89_MAC_MCC_DEL_GROUP_OK = 3,
	RTW89_MAC_MCC_RESET_GROUP_OK = 4,
	RTW89_MAC_MCC_SWITCH_CH_OK = 5,
	RTW89_MAC_MCC_TXNULL0_OK = 6,
	RTW89_MAC_MCC_TXNULL1_OK = 7,

	RTW89_MAC_MCC_SWITCH_EARLY = 10,
	RTW89_MAC_MCC_TBTT = 11,
	RTW89_MAC_MCC_DURATION_START = 12,
	RTW89_MAC_MCC_DURATION_END = 13,

	RTW89_MAC_MCC_ADD_ROLE_FAIL = 20,
	RTW89_MAC_MCC_START_GROUP_FAIL = 21,
	RTW89_MAC_MCC_STOP_GROUP_FAIL = 22,
	RTW89_MAC_MCC_DEL_GROUP_FAIL = 23,
	RTW89_MAC_MCC_RESET_GROUP_FAIL = 24,
	RTW89_MAC_MCC_SWITCH_CH_FAIL = 25,
	RTW89_MAC_MCC_TXNULL0_FAIL = 26,
	RTW89_MAC_MCC_TXNULL1_FAIL = 27,
};

struct rtw89_mac_ax_coex {
#define RTW89_MAC_AX_COEX_RTK_MODE 0
#define RTW89_MAC_AX_COEX_CSR_MODE 1
	u8 pta_mode;
#define RTW89_MAC_AX_COEX_INNER 0
#define RTW89_MAC_AX_COEX_OUTPUT 1
#define RTW89_MAC_AX_COEX_INPUT 2
	u8 direction;
};

struct rtw89_mac_ax_plt {
#define RTW89_MAC_AX_PLT_LTE_RX BIT(0)
#define RTW89_MAC_AX_PLT_GNT_BT_TX BIT(1)
#define RTW89_MAC_AX_PLT_GNT_BT_RX BIT(2)
#define RTW89_MAC_AX_PLT_GNT_WL BIT(3)
	u8 band;
	u8 tx;
	u8 rx;
};

enum rtw89_mac_bf_rrsc_rate {
	RTW89_MAC_BF_RRSC_6M = 0,
	RTW89_MAC_BF_RRSC_9M = 1,
	RTW89_MAC_BF_RRSC_12M,
	RTW89_MAC_BF_RRSC_18M,
	RTW89_MAC_BF_RRSC_24M,
	RTW89_MAC_BF_RRSC_36M,
	RTW89_MAC_BF_RRSC_48M,
	RTW89_MAC_BF_RRSC_54M,
	RTW89_MAC_BF_RRSC_HT_MSC0,
	RTW89_MAC_BF_RRSC_HT_MSC1,
	RTW89_MAC_BF_RRSC_HT_MSC2,
	RTW89_MAC_BF_RRSC_HT_MSC3,
	RTW89_MAC_BF_RRSC_HT_MSC4,
	RTW89_MAC_BF_RRSC_HT_MSC5,
	RTW89_MAC_BF_RRSC_HT_MSC6,
	RTW89_MAC_BF_RRSC_HT_MSC7,
	RTW89_MAC_BF_RRSC_VHT_MSC0,
	RTW89_MAC_BF_RRSC_VHT_MSC1,
	RTW89_MAC_BF_RRSC_VHT_MSC2,
	RTW89_MAC_BF_RRSC_VHT_MSC3,
	RTW89_MAC_BF_RRSC_VHT_MSC4,
	RTW89_MAC_BF_RRSC_VHT_MSC5,
	RTW89_MAC_BF_RRSC_VHT_MSC6,
	RTW89_MAC_BF_RRSC_VHT_MSC7,
	RTW89_MAC_BF_RRSC_HE_MSC0,
	RTW89_MAC_BF_RRSC_HE_MSC1,
	RTW89_MAC_BF_RRSC_HE_MSC2,
	RTW89_MAC_BF_RRSC_HE_MSC3,
	RTW89_MAC_BF_RRSC_HE_MSC4,
	RTW89_MAC_BF_RRSC_HE_MSC5,
	RTW89_MAC_BF_RRSC_HE_MSC6,
	RTW89_MAC_BF_RRSC_HE_MSC7 = 31,
	RTW89_MAC_BF_RRSC_MAX = 32
};

#define RTW89_R32_EA		0xEAEAEAEA
#define RTW89_R32_DEAD		0xDEADBEEF
#define MAC_REG_POOL_COUNT	10
#define ACCESS_CMAC(_addr) \
	({typeof(_addr) __addr = (_addr); \
	  __addr >= R_AX_CMAC_REG_START && __addr <= R_AX_CMAC_REG_END; })
#define RTW89_MAC_AX_BAND_REG_OFFSET 0x2000

#define PTCL_IDLE_POLL_CNT	10000
#define SW_CVR_DUR_US	8
#define SW_CVR_CNT	8

#define DLE_BOUND_UNIT (8 * 1024)
#define DLE_WAIT_CNT 2000
#define TRXCFG_WAIT_CNT	2000

#define RTW89_WDE_PG_64		64
#define RTW89_WDE_PG_128	128
#define RTW89_WDE_PG_256	256

#define S_AX_WDE_PAGE_SEL_64	0
#define S_AX_WDE_PAGE_SEL_128	1
#define S_AX_WDE_PAGE_SEL_256	2

#define RTW89_PLE_PG_64		64
#define RTW89_PLE_PG_128	128
#define RTW89_PLE_PG_256	256

#define S_AX_PLE_PAGE_SEL_64	0
#define S_AX_PLE_PAGE_SEL_128	1
#define S_AX_PLE_PAGE_SEL_256	2

#define B_CMAC0_MGQ_NORMAL	BIT(2)
#define B_CMAC0_MGQ_NO_PWRSAV	BIT(3)
#define B_CMAC0_CPUMGQ		BIT(4)
#define B_CMAC1_MGQ_NORMAL	BIT(10)
#define B_CMAC1_MGQ_NO_PWRSAV	BIT(11)
#define B_CMAC1_CPUMGQ		BIT(12)

#define QEMP_ACQ_GRP_MACID_NUM	8
#define QEMP_ACQ_GRP_QSEL_SH	4
#define QEMP_ACQ_GRP_QSEL_MASK	0xF

#define SDIO_LOCAL_BASE_ADDR    0x80000000

#define	PWR_CMD_WRITE		0
#define	PWR_CMD_POLL		1
#define	PWR_CMD_DELAY		2
#define	PWR_CMD_END		3

#define	PWR_INTF_MSK_SDIO	BIT(0)
#define	PWR_INTF_MSK_USB	BIT(1)
#define	PWR_INTF_MSK_PCIE	BIT(2)
#define	PWR_INTF_MSK_ALL	0x7

#define PWR_BASE_MAC		0
#define PWR_BASE_USB		1
#define PWR_BASE_PCIE		2
#define PWR_BASE_SDIO		3

#define	PWR_CV_MSK_A		BIT(0)
#define	PWR_CV_MSK_B		BIT(1)
#define	PWR_CV_MSK_C		BIT(2)
#define	PWR_CV_MSK_D		BIT(3)
#define	PWR_CV_MSK_E		BIT(4)
#define	PWR_CV_MSK_F		BIT(5)
#define	PWR_CV_MSK_G		BIT(6)
#define	PWR_CV_MSK_TEST		BIT(7)
#define	PWR_CV_MSK_ALL		0xFF

#define	PWR_DELAY_US		0
#define	PWR_DELAY_MS		1

/* STA scheduler */
#define SS_MACID_SH		8
#define SS_TX_LEN_MSK		0x1FFFFF
#define SS_CTRL1_R_TX_LEN	5
#define SS_CTRL1_R_NEXT_LINK	20
#define SS_LINK_SIZE		256

/* MAC debug port */
#define TMAC_DBG_SEL_C0 0xA5
#define RMAC_DBG_SEL_C0 0xA6
#define TRXPTCL_DBG_SEL_C0 0xA7
#define TMAC_DBG_SEL_C1 0xB5
#define RMAC_DBG_SEL_C1 0xB6
#define TRXPTCL_DBG_SEL_C1 0xB7
#define FW_PROG_CNTR_DBG_SEL 0xF2
#define PCIE_TXDMA_DBG_SEL 0x30
#define PCIE_RXDMA_DBG_SEL 0x31
#define PCIE_CVT_DBG_SEL 0x32
#define PCIE_CXPL_DBG_SEL 0x33
#define PCIE_IO_DBG_SEL 0x37
#define PCIE_MISC_DBG_SEL 0x38
#define PCIE_MISC2_DBG_SEL 0x00
#define MAC_DBG_SEL 1
#define RMAC_CMAC_DBG_SEL 1

/* TRXPTCL dbg port sel */
#define TRXPTRL_DBG_SEL_TMAC 0
#define TRXPTRL_DBG_SEL_RMAC 1

struct rtw89_cpuio_ctrl {
	u16 pkt_num;
	u16 start_pktid;
	u16 end_pktid;
	u8 cmd_type;
	u8 macid;
	u8 src_pid;
	u8 src_qid;
	u8 dst_pid;
	u8 dst_qid;
	u16 pktid;
};

struct rtw89_mac_dbg_port_info {
	u32 sel_addr;
	u8 sel_byte;
	u32 sel_msk;
	u32 srt;
	u32 end;
	u32 rd_addr;
	u8 rd_byte;
	u32 rd_msk;
};

#define QLNKTBL_ADDR_INFO_SEL BIT(0)
#define QLNKTBL_ADDR_INFO_SEL_0 0
#define QLNKTBL_ADDR_INFO_SEL_1 1
#define QLNKTBL_ADDR_TBL_IDX_MASK GENMASK(10, 1)
#define QLNKTBL_DATA_SEL1_PKT_CNT_MASK GENMASK(11, 0)

struct rtw89_mac_dle_dfi_ctrl {
	enum rtw89_mac_dle_ctrl_type type;
	u32 target;
	u32 addr;
	u32 out_data;
};

struct rtw89_mac_dle_dfi_quota {
	enum rtw89_mac_dle_ctrl_type dle_type;
	u32 qtaid;
	u16 rsv_pgnum;
	u16 use_pgnum;
};

struct rtw89_mac_dle_dfi_qempty {
	enum rtw89_mac_dle_ctrl_type dle_type;
	u32 grpsel;
	u32 qempty;
};

enum rtw89_mac_error_scenario {
	RTW89_WCPU_CPU_EXCEPTION	= 2,
	RTW89_WCPU_ASSERTION		= 3,
};

#define RTW89_ERROR_SCENARIO(__err) ((__err) >> 28)

/* Define DBG and recovery enum */
enum mac_ax_err_info {
	/* Get error info */

	/* L0 */
	MAC_AX_ERR_L0_ERR_CMAC0 = 0x0001,
	MAC_AX_ERR_L0_ERR_CMAC1 = 0x0002,
	MAC_AX_ERR_L0_RESET_DONE = 0x0003,
	MAC_AX_ERR_L0_PROMOTE_TO_L1 = 0x0010,

	/* L1 */
	MAC_AX_ERR_L1_ERR_DMAC = 0x1000,
	MAC_AX_ERR_L1_RESET_DISABLE_DMAC_DONE = 0x1001,
	MAC_AX_ERR_L1_RESET_RECOVERY_DONE = 0x1002,
	MAC_AX_ERR_L1_PROMOTE_TO_L2 = 0x1010,
	MAC_AX_ERR_L1_RCVY_STOP_DONE = 0x1011,

	/* L2 */
	/* address hole (master) */
	MAC_AX_ERR_L2_ERR_AH_DMA = 0x2000,
	MAC_AX_ERR_L2_ERR_AH_HCI = 0x2010,
	MAC_AX_ERR_L2_ERR_AH_RLX4081 = 0x2020,
	MAC_AX_ERR_L2_ERR_AH_IDDMA = 0x2030,
	MAC_AX_ERR_L2_ERR_AH_HIOE = 0x2040,
	MAC_AX_ERR_L2_ERR_AH_IPSEC = 0x2050,
	MAC_AX_ERR_L2_ERR_AH_RX4281 = 0x2060,
	MAC_AX_ERR_L2_ERR_AH_OTHERS = 0x2070,

	/* AHB bridge timeout (master) */
	MAC_AX_ERR_L2_ERR_AHB_TO_DMA = 0x2100,
	MAC_AX_ERR_L2_ERR_AHB_TO_HCI = 0x2110,
	MAC_AX_ERR_L2_ERR_AHB_TO_RLX4081 = 0x2120,
	MAC_AX_ERR_L2_ERR_AHB_TO_IDDMA = 0x2130,
	MAC_AX_ERR_L2_ERR_AHB_TO_HIOE = 0x2140,
	MAC_AX_ERR_L2_ERR_AHB_TO_IPSEC = 0x2150,
	MAC_AX_ERR_L2_ERR_AHB_TO_RX4281 = 0x2160,
	MAC_AX_ERR_L2_ERR_AHB_TO_OTHERS = 0x2170,

	/* APB_SA bridge timeout (master + slave) */
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_WVA = 0x2200,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_UART = 0x2201,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_CPULOCAL = 0x2202,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_AXIDMA = 0x2203,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_HIOE = 0x2204,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_IDDMA = 0x2205,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_IPSEC = 0x2206,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_WON = 0x2207,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_WDMAC = 0x2208,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_WCMAC = 0x2209,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_DMA_OTHERS = 0x220A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_WVA = 0x2210,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_UART = 0x2211,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_CPULOCAL = 0x2212,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_AXIDMA = 0x2213,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_HIOE = 0x2214,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_IDDMA = 0x2215,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_IPSEC = 0x2216,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_WDMAC = 0x2218,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_WCMAC = 0x2219,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HCI_OTHERS = 0x221A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_WVA = 0x2220,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_UART = 0x2221,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_CPULOCAL = 0x2222,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_AXIDMA = 0x2223,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_HIOE = 0x2224,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_IDDMA = 0x2225,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_IPSEC = 0x2226,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_WON = 0x2227,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_WDMAC = 0x2228,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_WCMAC = 0x2229,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RLX4081_OTHERS = 0x222A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_WVA = 0x2230,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_UART = 0x2231,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_CPULOCAL = 0x2232,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_AXIDMA = 0x2233,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_HIOE = 0x2234,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_IDDMA = 0x2235,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_IPSEC = 0x2236,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_WON = 0x2237,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_WDMAC = 0x2238,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_WCMAC = 0x2239,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IDDMA_OTHERS = 0x223A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_WVA = 0x2240,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_UART = 0x2241,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_CPULOCAL = 0x2242,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_AXIDMA = 0x2243,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_HIOE = 0x2244,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_IDDMA = 0x2245,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_IPSEC = 0x2246,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_WON = 0x2247,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_WDMAC = 0x2248,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_WCMAC = 0x2249,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_HIOE_OTHERS = 0x224A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_WVA = 0x2250,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_UART = 0x2251,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_CPULOCAL = 0x2252,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_AXIDMA = 0x2253,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_HIOE = 0x2254,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_IDDMA = 0x2255,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_IPSEC = 0x2256,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_WON = 0x2257,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_WDMAC = 0x2258,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_WCMAC = 0x2259,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_IPSEC_OTHERS = 0x225A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_WVA = 0x2260,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_UART = 0x2261,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_CPULOCAL = 0x2262,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_AXIDMA = 0x2263,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_HIOE = 0x2264,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_IDDMA = 0x2265,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_IPSEC = 0x2266,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_WON = 0x2267,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_WDMAC = 0x2268,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_WCMAC = 0x2269,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_RX4281_OTHERS = 0x226A,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_WVA = 0x2270,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_UART = 0x2271,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_CPULOCAL = 0x2272,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_AXIDMA = 0x2273,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_HIOE = 0x2274,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_IDDMA = 0x2275,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_IPSEC = 0x2276,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_WON = 0x2277,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_WDMAC = 0x2278,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_WCMAC = 0x2279,
	MAC_AX_ERR_L2_ERR_APB_SA_TO_OTHERS_OTHERS = 0x227A,

	/* APB_BBRF bridge timeout (master) */
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_DMA = 0x2300,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_HCI = 0x2310,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_RLX4081 = 0x2320,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_IDDMA = 0x2330,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_HIOE = 0x2340,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_IPSEC = 0x2350,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_RX4281 = 0x2360,
	MAC_AX_ERR_L2_ERR_APB_BBRF_TO_OTHERS = 0x2370,
	MAC_AX_ERR_L2_RESET_DONE = 0x2400,
	MAC_AX_ERR_L2_ERR_WDT_TIMEOUT_INT = 0x2599,
	MAC_AX_ERR_CPU_EXCEPTION = 0x3000,
	MAC_AX_ERR_ASSERTION = 0x4000,
	MAC_AX_GET_ERR_MAX,
	MAC_AX_DUMP_SHAREBUFF_INDICATOR = 0x80000000,

	/* set error info */
	MAC_AX_ERR_L1_DISABLE_EN = 0x0001,
	MAC_AX_ERR_L1_RCVY_EN = 0x0002,
	MAC_AX_ERR_L1_RCVY_STOP_REQ = 0x0003,
	MAC_AX_ERR_L1_RCVY_START_REQ = 0x0004,
	MAC_AX_ERR_L0_CFG_NOTIFY = 0x0010,
	MAC_AX_ERR_L0_CFG_DIS_NOTIFY = 0x0011,
	MAC_AX_ERR_L0_CFG_HANDSHAKE = 0x0012,
	MAC_AX_ERR_L0_RCVY_EN = 0x0013,
	MAC_AX_SET_ERR_MAX,
};

struct rtw89_mac_size_set {
	const struct rtw89_hfc_prec_cfg hfc_preccfg_pcie;
	const struct rtw89_dle_size wde_size0;
	const struct rtw89_dle_size wde_size4;
	const struct rtw89_dle_size wde_size6;
	const struct rtw89_dle_size wde_size9;
	const struct rtw89_dle_size wde_size18;
	const struct rtw89_dle_size wde_size19;
	const struct rtw89_dle_size ple_size0;
	const struct rtw89_dle_size ple_size4;
	const struct rtw89_dle_size ple_size6;
	const struct rtw89_dle_size ple_size8;
	const struct rtw89_dle_size ple_size18;
	const struct rtw89_dle_size ple_size19;
	const struct rtw89_wde_quota wde_qt0;
	const struct rtw89_wde_quota wde_qt4;
	const struct rtw89_wde_quota wde_qt6;
	const struct rtw89_wde_quota wde_qt17;
	const struct rtw89_wde_quota wde_qt18;
	const struct rtw89_ple_quota ple_qt4;
	const struct rtw89_ple_quota ple_qt5;
	const struct rtw89_ple_quota ple_qt13;
	const struct rtw89_ple_quota ple_qt18;
	const struct rtw89_ple_quota ple_qt44;
	const struct rtw89_ple_quota ple_qt45;
	const struct rtw89_ple_quota ple_qt46;
	const struct rtw89_ple_quota ple_qt47;
	const struct rtw89_ple_quota ple_qt58;
	const struct rtw89_ple_quota ple_qt_52a_wow;
};

extern const struct rtw89_mac_size_set rtw89_mac_size;

static inline u32 rtw89_mac_reg_by_idx(u32 reg_base, u8 band)
{
	return band == 0 ? reg_base : (reg_base + 0x2000);
}

static inline u32 rtw89_mac_reg_by_port(u32 base, u8 port, u8 mac_idx)
{
	return rtw89_mac_reg_by_idx(base + port * 0x40, mac_idx);
}

static inline u32
rtw89_read32_port_mask(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		       u32 base, u32 mask)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	return rtw89_read32_mask(rtwdev, reg, mask);
}

static inline void
rtw89_write32_port(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif, u32 base,
		   u32 data)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	rtw89_write32(rtwdev, reg, data);
}

static inline void
rtw89_write32_port_mask(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			u32 base, u32 mask, u32 data)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	rtw89_write32_mask(rtwdev, reg, mask, data);
}

static inline void
rtw89_write16_port_mask(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			u32 base, u32 mask, u16 data)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	rtw89_write16_mask(rtwdev, reg, mask, data);
}

static inline void
rtw89_write32_port_clr(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		       u32 base, u32 bit)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	rtw89_write32_clr(rtwdev, reg, bit);
}

static inline void
rtw89_write16_port_clr(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		       u32 base, u16 bit)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	rtw89_write16_clr(rtwdev, reg, bit);
}

static inline void
rtw89_write32_port_set(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		       u32 base, u32 bit)
{
	u32 reg;

	reg = rtw89_mac_reg_by_port(base, rtwvif->port, rtwvif->mac_idx);
	rtw89_write32_set(rtwdev, reg, bit);
}

void rtw89_mac_pwr_off(struct rtw89_dev *rtwdev);
int rtw89_mac_partial_init(struct rtw89_dev *rtwdev);
int rtw89_mac_init(struct rtw89_dev *rtwdev);
int rtw89_mac_check_mac_en(struct rtw89_dev *rtwdev, u8 band,
			   enum rtw89_mac_hwmod_sel sel);
int rtw89_mac_write_lte(struct rtw89_dev *rtwdev, const u32 offset, u32 val);
int rtw89_mac_read_lte(struct rtw89_dev *rtwdev, const u32 offset, u32 *val);
int rtw89_mac_add_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *vif);
int rtw89_mac_port_update(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
void rtw89_mac_set_he_obss_narrow_bw_ru(struct rtw89_dev *rtwdev,
					struct ieee80211_vif *vif);
void rtw89_mac_stop_ap(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
int rtw89_mac_remove_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *vif);
void rtw89_mac_disable_cpu(struct rtw89_dev *rtwdev);
int rtw89_mac_enable_cpu(struct rtw89_dev *rtwdev, u8 boot_reason, bool dlfw);
int rtw89_mac_enable_bb_rf(struct rtw89_dev *rtwdev);
int rtw89_mac_disable_bb_rf(struct rtw89_dev *rtwdev);

static inline int rtw89_chip_enable_bb_rf(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->enable_bb_rf(rtwdev);
}

static inline int rtw89_chip_disable_bb_rf(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->disable_bb_rf(rtwdev);
}

u32 rtw89_mac_get_err_status(struct rtw89_dev *rtwdev);
int rtw89_mac_set_err_status(struct rtw89_dev *rtwdev, u32 err);
bool rtw89_mac_c2h_chk_atomic(struct rtw89_dev *rtwdev, u8 class, u8 func);
void rtw89_mac_c2h_handle(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			  u32 len, u8 class, u8 func);
int rtw89_mac_setup_phycap(struct rtw89_dev *rtwdev);
int rtw89_mac_stop_sch_tx(struct rtw89_dev *rtwdev, u8 mac_idx,
			  u32 *tx_en, enum rtw89_sch_tx_sel sel);
int rtw89_mac_stop_sch_tx_v1(struct rtw89_dev *rtwdev, u8 mac_idx,
			     u32 *tx_en, enum rtw89_sch_tx_sel sel);
int rtw89_mac_resume_sch_tx(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en);
int rtw89_mac_resume_sch_tx_v1(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en);
int rtw89_mac_cfg_ppdu_status(struct rtw89_dev *rtwdev, u8 mac_ids, bool enable);
void rtw89_mac_update_rts_threshold(struct rtw89_dev *rtwdev, u8 mac_idx);
void rtw89_mac_flush_txq(struct rtw89_dev *rtwdev, u32 queues, bool drop);
int rtw89_mac_coex_init(struct rtw89_dev *rtwdev, const struct rtw89_mac_ax_coex *coex);
int rtw89_mac_coex_init_v1(struct rtw89_dev *rtwdev,
			   const struct rtw89_mac_ax_coex *coex);
int rtw89_mac_cfg_gnt(struct rtw89_dev *rtwdev,
		      const struct rtw89_mac_ax_coex_gnt *gnt_cfg);
int rtw89_mac_cfg_gnt_v1(struct rtw89_dev *rtwdev,
			 const struct rtw89_mac_ax_coex_gnt *gnt_cfg);
int rtw89_mac_cfg_plt(struct rtw89_dev *rtwdev, struct rtw89_mac_ax_plt *plt);
u16 rtw89_mac_get_plt_cnt(struct rtw89_dev *rtwdev, u8 band);
void rtw89_mac_cfg_sb(struct rtw89_dev *rtwdev, u32 val);
u32 rtw89_mac_get_sb(struct rtw89_dev *rtwdev);
bool rtw89_mac_get_ctrl_path(struct rtw89_dev *rtwdev);
int rtw89_mac_cfg_ctrl_path(struct rtw89_dev *rtwdev, bool wl);
int rtw89_mac_cfg_ctrl_path_v1(struct rtw89_dev *rtwdev, bool wl);
bool rtw89_mac_get_txpwr_cr(struct rtw89_dev *rtwdev,
			    enum rtw89_phy_idx phy_idx,
			    u32 reg_base, u32 *cr);
void rtw89_mac_power_mode_change(struct rtw89_dev *rtwdev, bool enter);
void rtw89_mac_notify_wake(struct rtw89_dev *rtwdev);
void rtw89_mac_bf_assoc(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta);
void rtw89_mac_bf_disassoc(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
void rtw89_mac_bf_set_gid_table(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *conf);
void rtw89_mac_bf_monitor_calc(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta *sta, bool disconnect);
void _rtw89_mac_bf_monitor_track(struct rtw89_dev *rtwdev);
int rtw89_mac_vif_init(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
int rtw89_mac_vif_deinit(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
int rtw89_mac_set_hw_muedca_ctrl(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif, bool en);
int rtw89_mac_set_macid_pause(struct rtw89_dev *rtwdev, u8 macid, bool pause);

static inline void rtw89_mac_bf_monitor_track(struct rtw89_dev *rtwdev)
{
	if (!test_bit(RTW89_FLAG_BFEE_MON, rtwdev->flags))
		return;

	_rtw89_mac_bf_monitor_track(rtwdev);
}

static inline int rtw89_mac_txpwr_read32(struct rtw89_dev *rtwdev,
					 enum rtw89_phy_idx phy_idx,
					 u32 reg_base, u32 *val)
{
	u32 cr;

	if (!rtw89_mac_get_txpwr_cr(rtwdev, phy_idx, reg_base, &cr))
		return -EINVAL;

	*val = rtw89_read32(rtwdev, cr);
	return 0;
}

static inline int rtw89_mac_txpwr_write32(struct rtw89_dev *rtwdev,
					  enum rtw89_phy_idx phy_idx,
					  u32 reg_base, u32 val)
{
	u32 cr;

	if (!rtw89_mac_get_txpwr_cr(rtwdev, phy_idx, reg_base, &cr))
		return -EINVAL;

	rtw89_write32(rtwdev, cr, val);
	return 0;
}

static inline int rtw89_mac_txpwr_write32_mask(struct rtw89_dev *rtwdev,
					       enum rtw89_phy_idx phy_idx,
					       u32 reg_base, u32 mask, u32 val)
{
	u32 cr;

	if (!rtw89_mac_get_txpwr_cr(rtwdev, phy_idx, reg_base, &cr))
		return -EINVAL;

	rtw89_write32_mask(rtwdev, cr, mask, val);
	return 0;
}

static inline void rtw89_mac_ctrl_hci_dma_tx(struct rtw89_dev *rtwdev,
					     bool enable)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (enable)
		rtw89_write32_set(rtwdev, chip->hci_func_en_addr,
				  B_AX_HCI_TXDMA_EN);
	else
		rtw89_write32_clr(rtwdev, chip->hci_func_en_addr,
				  B_AX_HCI_TXDMA_EN);
}

static inline void rtw89_mac_ctrl_hci_dma_rx(struct rtw89_dev *rtwdev,
					     bool enable)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (enable)
		rtw89_write32_set(rtwdev, chip->hci_func_en_addr,
				  B_AX_HCI_RXDMA_EN);
	else
		rtw89_write32_clr(rtwdev, chip->hci_func_en_addr,
				  B_AX_HCI_RXDMA_EN);
}

static inline void rtw89_mac_ctrl_hci_dma_trx(struct rtw89_dev *rtwdev,
					      bool enable)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (enable)
		rtw89_write32_set(rtwdev, chip->hci_func_en_addr,
				  B_AX_HCI_TXDMA_EN | B_AX_HCI_RXDMA_EN);
	else
		rtw89_write32_clr(rtwdev, chip->hci_func_en_addr,
				  B_AX_HCI_TXDMA_EN | B_AX_HCI_RXDMA_EN);
}

static inline bool rtw89_mac_get_power_state(struct rtw89_dev *rtwdev)
{
	u32 val;

	val = rtw89_read32_mask(rtwdev, R_AX_IC_PWR_STATE,
				B_AX_WLMAC_PWR_STE_MASK);

	return !!val;
}

int rtw89_mac_set_tx_time(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			  bool resume, u32 tx_time);
int rtw89_mac_get_tx_time(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			  u32 *tx_time);
int rtw89_mac_set_tx_retry_limit(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta,
				 bool resume, u8 tx_retry);
int rtw89_mac_get_tx_retry_limit(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta, u8 *tx_retry);

enum rtw89_mac_xtal_si_offset {
	XTAL0 = 0x0,
	XTAL3 = 0x3,
	XTAL_SI_XTAL_SC_XI = 0x04,
#define XTAL_SC_XI_MASK		GENMASK(7, 0)
	XTAL_SI_XTAL_SC_XO = 0x05,
#define XTAL_SC_XO_MASK		GENMASK(7, 0)
	XTAL_SI_PWR_CUT = 0x10,
#define XTAL_SI_SMALL_PWR_CUT	BIT(0)
#define XTAL_SI_BIG_PWR_CUT	BIT(1)
	XTAL_SI_XTAL_XMD_2 = 0x24,
#define XTAL_SI_LDO_LPS		GENMASK(6, 4)
	XTAL_SI_XTAL_XMD_4 = 0x26,
#define XTAL_SI_LPS_CAP		GENMASK(3, 0)
	XTAL_SI_CV = 0x41,
	XTAL_SI_LOW_ADDR = 0x62,
#define XTAL_SI_LOW_ADDR_MASK	GENMASK(7, 0)
	XTAL_SI_CTRL = 0x63,
#define XTAL_SI_MODE_SEL_MASK	GENMASK(7, 6)
#define XTAL_SI_RDY		BIT(5)
#define XTAL_SI_HIGH_ADDR_MASK	GENMASK(2, 0)
	XTAL_SI_READ_VAL = 0x7A,
	XTAL_SI_WL_RFC_S0 = 0x80,
#define XTAL_SI_RF00S_EN	GENMASK(2, 0)
#define XTAL_SI_RF00		BIT(0)
	XTAL_SI_WL_RFC_S1 = 0x81,
#define XTAL_SI_RF10S_EN	GENMASK(2, 0)
#define XTAL_SI_RF10		BIT(0)
	XTAL_SI_ANAPAR_WL = 0x90,
#define XTAL_SI_SRAM2RFC	BIT(7)
#define XTAL_SI_GND_SHDN_WL	BIT(6)
#define XTAL_SI_SHDN_WL		BIT(5)
#define XTAL_SI_RFC2RF		BIT(4)
#define XTAL_SI_OFF_EI		BIT(3)
#define XTAL_SI_OFF_WEI		BIT(2)
#define XTAL_SI_PON_EI		BIT(1)
#define XTAL_SI_PON_WEI		BIT(0)
	XTAL_SI_SRAM_CTRL = 0xA1,
#define XTAL_SI_SRAM_DIS	BIT(1)
#define FULL_BIT_MASK		GENMASK(7, 0)
};

int rtw89_mac_write_xtal_si(struct rtw89_dev *rtwdev, u8 offset, u8 val, u8 mask);
int rtw89_mac_read_xtal_si(struct rtw89_dev *rtwdev, u8 offset, u8 *val);
void rtw89_mac_pkt_drop_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
u16 rtw89_mac_dle_buf_req(struct rtw89_dev *rtwdev, u16 buf_len, bool wd);
int rtw89_mac_set_cpuio(struct rtw89_dev *rtwdev,
			struct rtw89_cpuio_ctrl *ctrl_para, bool wd);
int rtw89_mac_typ_fltr_opt(struct rtw89_dev *rtwdev,
			   enum rtw89_machdr_frame_type type,
			   enum rtw89_mac_fwd_target fwd_target, u8 mac_idx);
int rtw89_mac_resize_ple_rx_quota(struct rtw89_dev *rtwdev, bool wow);
int rtw89_mac_ptk_drop_by_band_and_wait(struct rtw89_dev *rtwdev,
					enum rtw89_mac_idx band);
void rtw89_mac_hw_mgnt_sec(struct rtw89_dev *rtwdev, bool wow);

#endif
