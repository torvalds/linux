/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_FW_H__
#define __RTW89_FW_H__

#include "core.h"

enum rtw89_fw_dl_status {
	RTW89_FWDL_INITIAL_STATE = 0,
	RTW89_FWDL_FWDL_ONGOING = 1,
	RTW89_FWDL_CHECKSUM_FAIL = 2,
	RTW89_FWDL_SECURITY_FAIL = 3,
	RTW89_FWDL_CV_NOT_MATCH = 4,
	RTW89_FWDL_RSVD0 = 5,
	RTW89_FWDL_WCPU_FWDL_RDY = 6,
	RTW89_FWDL_WCPU_FW_INIT_RDY = 7
};

struct rtw89_c2hreg_hdr {
	u32 w0;
};

#define RTW89_C2HREG_HDR_FUNC_MASK GENMASK(6, 0)
#define RTW89_C2HREG_HDR_ACK BIT(7)
#define RTW89_C2HREG_HDR_LEN_MASK GENMASK(11, 8)
#define RTW89_C2HREG_HDR_SEQ_MASK GENMASK(15, 12)

struct rtw89_c2hreg_phycap {
	u32 w0;
	u32 w1;
	u32 w2;
	u32 w3;
} __packed;

#define RTW89_C2HREG_PHYCAP_W0_FUNC GENMASK(6, 0)
#define RTW89_C2HREG_PHYCAP_W0_ACK BIT(7)
#define RTW89_C2HREG_PHYCAP_W0_LEN GENMASK(11, 8)
#define RTW89_C2HREG_PHYCAP_W0_SEQ GENMASK(15, 12)
#define RTW89_C2HREG_PHYCAP_W0_RX_NSS GENMASK(23, 16)
#define RTW89_C2HREG_PHYCAP_W0_BW GENMASK(31, 24)
#define RTW89_C2HREG_PHYCAP_W1_TX_NSS GENMASK(7, 0)
#define RTW89_C2HREG_PHYCAP_W1_PROT GENMASK(15, 8)
#define RTW89_C2HREG_PHYCAP_W1_NIC GENMASK(23, 16)
#define RTW89_C2HREG_PHYCAP_W1_WL_FUNC GENMASK(31, 24)
#define RTW89_C2HREG_PHYCAP_W2_HW_TYPE GENMASK(7, 0)
#define RTW89_C2HREG_PHYCAP_W3_ANT_TX_NUM GENMASK(15, 8)
#define RTW89_C2HREG_PHYCAP_W3_ANT_RX_NUM GENMASK(23, 16)

struct rtw89_h2creg_hdr {
	u32 w0;
};

#define RTW89_H2CREG_HDR_FUNC_MASK GENMASK(6, 0)
#define RTW89_H2CREG_HDR_LEN_MASK GENMASK(11, 8)

struct rtw89_h2creg_sch_tx_en {
	u32 w0;
	u32 w1;
} __packed;

#define RTW89_H2CREG_SCH_TX_EN_W0_EN GENMASK(31, 16)
#define RTW89_H2CREG_SCH_TX_EN_W1_MASK GENMASK(15, 0)
#define RTW89_H2CREG_SCH_TX_EN_W1_BAND BIT(16)

#define RTW89_H2CREG_MAX 4
#define RTW89_C2HREG_MAX 4
#define RTW89_C2HREG_HDR_LEN 2
#define RTW89_H2CREG_HDR_LEN 2
#define RTW89_C2H_TIMEOUT 1000000
struct rtw89_mac_c2h_info {
	u8 id;
	u8 content_len;
	union {
		u32 c2hreg[RTW89_C2HREG_MAX];
		struct rtw89_c2hreg_hdr hdr;
		struct rtw89_c2hreg_phycap phycap;
	} u;
};

struct rtw89_mac_h2c_info {
	u8 id;
	u8 content_len;
	union {
		u32 h2creg[RTW89_H2CREG_MAX];
		struct rtw89_h2creg_hdr hdr;
		struct rtw89_h2creg_sch_tx_en sch_tx_en;
	} u;
};

enum rtw89_mac_h2c_type {
	RTW89_FWCMD_H2CREG_FUNC_H2CREG_LB = 0,
	RTW89_FWCMD_H2CREG_FUNC_CNSL_CMD,
	RTW89_FWCMD_H2CREG_FUNC_FWERR,
	RTW89_FWCMD_H2CREG_FUNC_GET_FEATURE,
	RTW89_FWCMD_H2CREG_FUNC_GETPKT_INFORM,
	RTW89_FWCMD_H2CREG_FUNC_SCH_TX_EN
};

enum rtw89_mac_c2h_type {
	RTW89_FWCMD_C2HREG_FUNC_C2HREG_LB = 0,
	RTW89_FWCMD_C2HREG_FUNC_ERR_RPT,
	RTW89_FWCMD_C2HREG_FUNC_ERR_MSG,
	RTW89_FWCMD_C2HREG_FUNC_PHY_CAP,
	RTW89_FWCMD_C2HREG_FUNC_TX_PAUSE_RPT,
	RTW89_FWCMD_C2HREG_FUNC_NULL = 0xFF
};

enum rtw89_fw_c2h_category {
	RTW89_C2H_CAT_TEST,
	RTW89_C2H_CAT_MAC,
	RTW89_C2H_CAT_OUTSRC,
};

enum rtw89_fw_log_level {
	RTW89_FW_LOG_LEVEL_OFF,
	RTW89_FW_LOG_LEVEL_CRT,
	RTW89_FW_LOG_LEVEL_SER,
	RTW89_FW_LOG_LEVEL_WARN,
	RTW89_FW_LOG_LEVEL_LOUD,
	RTW89_FW_LOG_LEVEL_TR,
};

enum rtw89_fw_log_path {
	RTW89_FW_LOG_LEVEL_UART,
	RTW89_FW_LOG_LEVEL_C2H,
	RTW89_FW_LOG_LEVEL_SNI,
};

enum rtw89_fw_log_comp {
	RTW89_FW_LOG_COMP_VER,
	RTW89_FW_LOG_COMP_INIT,
	RTW89_FW_LOG_COMP_TASK,
	RTW89_FW_LOG_COMP_CNS,
	RTW89_FW_LOG_COMP_H2C,
	RTW89_FW_LOG_COMP_C2H,
	RTW89_FW_LOG_COMP_TX,
	RTW89_FW_LOG_COMP_RX,
	RTW89_FW_LOG_COMP_IPSEC,
	RTW89_FW_LOG_COMP_TIMER,
	RTW89_FW_LOG_COMP_DBGPKT,
	RTW89_FW_LOG_COMP_PS,
	RTW89_FW_LOG_COMP_ERROR,
	RTW89_FW_LOG_COMP_WOWLAN,
	RTW89_FW_LOG_COMP_SECURE_BOOT,
	RTW89_FW_LOG_COMP_BTC,
	RTW89_FW_LOG_COMP_BB,
	RTW89_FW_LOG_COMP_TWT,
	RTW89_FW_LOG_COMP_RF,
	RTW89_FW_LOG_COMP_MCC = 20,
};

enum rtw89_pkt_offload_op {
	RTW89_PKT_OFLD_OP_ADD,
	RTW89_PKT_OFLD_OP_DEL,
	RTW89_PKT_OFLD_OP_READ,

	NUM_OF_RTW89_PKT_OFFLOAD_OP,
};

#define RTW89_PKT_OFLD_WAIT_TAG(pkt_id, pkt_op) \
	((pkt_id) * NUM_OF_RTW89_PKT_OFFLOAD_OP + (pkt_op))

enum rtw89_scanofld_notify_reason {
	RTW89_SCAN_DWELL_NOTIFY,
	RTW89_SCAN_PRE_TX_NOTIFY,
	RTW89_SCAN_POST_TX_NOTIFY,
	RTW89_SCAN_ENTER_CH_NOTIFY,
	RTW89_SCAN_LEAVE_CH_NOTIFY,
	RTW89_SCAN_END_SCAN_NOTIFY,
};

enum rtw89_chan_type {
	RTW89_CHAN_OPERATE = 0,
	RTW89_CHAN_ACTIVE,
	RTW89_CHAN_DFS,
};

enum rtw89_p2pps_action {
	RTW89_P2P_ACT_INIT = 0,
	RTW89_P2P_ACT_UPDATE = 1,
	RTW89_P2P_ACT_REMOVE = 2,
	RTW89_P2P_ACT_TERMINATE = 3,
};

enum rtw89_bcn_fltr_offload_mode {
	RTW89_BCN_FLTR_OFFLOAD_MODE_0 = 0,
	RTW89_BCN_FLTR_OFFLOAD_MODE_1,
	RTW89_BCN_FLTR_OFFLOAD_MODE_2,
	RTW89_BCN_FLTR_OFFLOAD_MODE_3,

	RTW89_BCN_FLTR_OFFLOAD_MODE_DEFAULT = RTW89_BCN_FLTR_OFFLOAD_MODE_0,
};

enum rtw89_bcn_fltr_type {
	RTW89_BCN_FLTR_BEACON_LOSS,
	RTW89_BCN_FLTR_RSSI,
	RTW89_BCN_FLTR_NOTIFY,
};

enum rtw89_bcn_fltr_rssi_event {
	RTW89_BCN_FLTR_RSSI_NOT_CHANGED,
	RTW89_BCN_FLTR_RSSI_HIGH,
	RTW89_BCN_FLTR_RSSI_LOW,
};

#define FWDL_SECTION_MAX_NUM 10
#define FWDL_SECTION_CHKSUM_LEN	8
#define FWDL_SECTION_PER_PKT_LEN 2020

struct rtw89_fw_hdr_section_info {
	u8 redl;
	const u8 *addr;
	u32 len;
	u32 dladdr;
	u32 mssc;
	u8 type;
};

struct rtw89_fw_bin_info {
	u8 section_num;
	u32 hdr_len;
	bool dynamic_hdr_en;
	u32 dynamic_hdr_len;
	struct rtw89_fw_hdr_section_info section_info[FWDL_SECTION_MAX_NUM];
};

struct rtw89_fw_macid_pause_grp {
	__le32 pause_grp[4];
	__le32 mask_grp[4];
} __packed;

#define RTW89_H2C_MAX_SIZE 2048
#define RTW89_CHANNEL_TIME 45
#define RTW89_CHANNEL_TIME_6G 20
#define RTW89_DFS_CHAN_TIME 105
#define RTW89_OFF_CHAN_TIME 100
#define RTW89_DWELL_TIME 20
#define RTW89_DWELL_TIME_6G 10
#define RTW89_SCAN_WIDTH 0
#define RTW89_SCANOFLD_MAX_SSID 8
#define RTW89_SCANOFLD_MAX_IE_LEN 512
#define RTW89_SCANOFLD_PKT_NONE 0xFF
#define RTW89_SCANOFLD_DEBUG_MASK 0x1F
#define RTW89_MAC_CHINFO_SIZE 28
#define RTW89_SCAN_LIST_GUARD 4
#define RTW89_SCAN_LIST_LIMIT \
		((RTW89_H2C_MAX_SIZE / RTW89_MAC_CHINFO_SIZE) - RTW89_SCAN_LIST_GUARD)

#define RTW89_BCN_LOSS_CNT 10

struct rtw89_mac_chinfo {
	u8 period;
	u8 dwell_time;
	u8 central_ch;
	u8 pri_ch;
	u8 bw:3;
	u8 notify_action:5;
	u8 num_pkt:4;
	u8 tx_pkt:1;
	u8 pause_data:1;
	u8 ch_band:2;
	u8 probe_id;
	u8 dfs_ch:1;
	u8 tx_null:1;
	u8 rand_seq_num:1;
	u8 cfg_tx_pwr:1;
	u8 rsvd0: 4;
	u8 pkt_id[RTW89_SCANOFLD_MAX_SSID];
	u16 tx_pwr_idx;
	u8 rsvd1;
	struct list_head list;
	bool is_psc;
};

struct rtw89_scan_option {
	bool enable;
	bool target_ch_mode;
};

struct rtw89_pktofld_info {
	struct list_head list;
	u8 id;

	/* Below fields are for 6 GHz RNR use only */
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	u8 bssid[ETH_ALEN];
	u16 channel_6ghz;
	bool cancel;
};

static inline void RTW89_SET_FWCMD_RA_IS_DIS(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(0));
}

static inline void RTW89_SET_FWCMD_RA_MODE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(5, 1));
}

static inline void RTW89_SET_FWCMD_RA_BW_CAP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(7, 6));
}

static inline void RTW89_SET_FWCMD_RA_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_RA_DCM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(16));
}

static inline void RTW89_SET_FWCMD_RA_ER(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(17));
}

static inline void RTW89_SET_FWCMD_RA_INIT_RATE_LV(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(19, 18));
}

static inline void RTW89_SET_FWCMD_RA_UPD_ALL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(20));
}

static inline void RTW89_SET_FWCMD_RA_SGI(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(21));
}

static inline void RTW89_SET_FWCMD_RA_LDPC(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(22));
}

static inline void RTW89_SET_FWCMD_RA_STBC(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(23));
}

static inline void RTW89_SET_FWCMD_RA_SS_NUM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(26, 24));
}

static inline void RTW89_SET_FWCMD_RA_GILTF(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(29, 27));
}

static inline void RTW89_SET_FWCMD_RA_UPD_BW_NSS_MASK(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(30));
}

static inline void RTW89_SET_FWCMD_RA_UPD_MASK(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(31));
}

static inline void RTW89_SET_FWCMD_RA_MASK_0(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_RA_MASK_1(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_RA_MASK_2(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_RA_MASK_3(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_RA_MASK_4(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x02, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_RA_BFEE_CSI_CTL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x02, val, BIT(31));
}

static inline void RTW89_SET_FWCMD_RA_BAND_NUM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_RA_RA_CSI_RATE_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, BIT(8));
}

static inline void RTW89_SET_FWCMD_RA_FIXED_CSI_RATE_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, BIT(9));
}

static inline void RTW89_SET_FWCMD_RA_CR_TBL_SEL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, BIT(10));
}

static inline void RTW89_SET_FWCMD_RA_FIX_GILTF_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, BIT(11));
}

static inline void RTW89_SET_FWCMD_RA_FIX_GILTF(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(14, 12));
}

static inline void RTW89_SET_FWCMD_RA_FIXED_CSI_MCS_SS_IDX(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_RA_FIXED_CSI_MODE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(25, 24));
}

static inline void RTW89_SET_FWCMD_RA_FIXED_CSI_GI_LTF(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(28, 26));
}

static inline void RTW89_SET_FWCMD_RA_FIXED_CSI_BW(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(31, 29));
}

static inline void RTW89_SET_FWCMD_SEC_IDX(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_SEC_OFFSET(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_SEC_LEN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_SEC_TYPE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, GENMASK(3, 0));
}

static inline void RTW89_SET_FWCMD_SEC_EXT_KEY(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, BIT(4));
}

static inline void RTW89_SET_FWCMD_SEC_SPP_MODE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, BIT(5));
}

static inline void RTW89_SET_FWCMD_SEC_KEY0(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x02, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_SEC_KEY1(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x03, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_SEC_KEY2(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x04, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_SEC_KEY3(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x05, val, GENMASK(31, 0));
}

static inline void RTW89_SET_EDCA_SEL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(1, 0));
}

static inline void RTW89_SET_EDCA_BAND(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(3));
}

static inline void RTW89_SET_EDCA_WMM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, BIT(4));
}

static inline void RTW89_SET_EDCA_AC(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x00, val, GENMASK(6, 5));
}

static inline void RTW89_SET_EDCA_PARAM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 0x01, val, GENMASK(31, 0));
}
#define FW_EDCA_PARAM_TXOPLMT_MSK GENMASK(26, 16)
#define FW_EDCA_PARAM_CWMAX_MSK GENMASK(15, 12)
#define FW_EDCA_PARAM_CWMIN_MSK GENMASK(11, 8)
#define FW_EDCA_PARAM_AIFS_MSK GENMASK(7, 0)

#define FWDL_SECURITY_SECTION_TYPE 9
#define FWDL_SECURITY_SIGLEN 512

struct rtw89_fw_dynhdr_sec {
	__le32 w0;
	u8 content[];
} __packed;

struct rtw89_fw_dynhdr_hdr {
	__le32 hdr_len;
	__le32 setcion_count;
	/* struct rtw89_fw_dynhdr_sec (nested flexible structures) */
} __packed;

struct rtw89_fw_hdr_section {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
} __packed;

#define FWSECTION_HDR_W0_DL_ADDR GENMASK(31, 0)
#define FWSECTION_HDR_W1_METADATA GENMASK(31, 24)
#define FWSECTION_HDR_W1_SECTIONTYPE GENMASK(27, 24)
#define FWSECTION_HDR_W1_SEC_SIZE GENMASK(23, 0)
#define FWSECTION_HDR_W1_CHECKSUM BIT(28)
#define FWSECTION_HDR_W1_REDL BIT(29)
#define FWSECTION_HDR_W2_MSSC GENMASK(31, 0)

struct rtw89_fw_hdr {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	struct rtw89_fw_hdr_section sections[];
	/* struct rtw89_fw_dynhdr_hdr (optional) */
} __packed;

#define FW_HDR_W1_MAJOR_VERSION GENMASK(7, 0)
#define FW_HDR_W1_MINOR_VERSION GENMASK(15, 8)
#define FW_HDR_W1_SUBVERSION GENMASK(23, 16)
#define FW_HDR_W1_SUBINDEX GENMASK(31, 24)
#define FW_HDR_W3_LEN GENMASK(23, 16)
#define FW_HDR_W4_MONTH GENMASK(7, 0)
#define FW_HDR_W4_DATE GENMASK(15, 8)
#define FW_HDR_W4_HOUR GENMASK(23, 16)
#define FW_HDR_W4_MIN GENMASK(31, 24)
#define FW_HDR_W5_YEAR GENMASK(31, 0)
#define FW_HDR_W6_SEC_NUM GENMASK(15, 8)
#define FW_HDR_W7_DYN_HDR BIT(16)
#define FW_HDR_W7_CMD_VERSERION GENMASK(31, 24)

static inline void SET_FW_HDR_PART_SIZE(void *fwhdr, u32 val)
{
	le32p_replace_bits((__le32 *)fwhdr + 7, val, GENMASK(15, 0));
}

static inline void SET_CTRL_INFO_MACID(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 0, val, GENMASK(6, 0));
}

static inline void SET_CTRL_INFO_OPERATION(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 0, val, BIT(7));
}
#define SET_CMC_TBL_MASK_DATARATE GENMASK(8, 0)
static inline void SET_CMC_TBL_DATARATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(8, 0));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_DATARATE,
			   GENMASK(8, 0));
}
#define SET_CMC_TBL_MASK_FORCE_TXOP BIT(0)
static inline void SET_CMC_TBL_FORCE_TXOP(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(9));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_FORCE_TXOP,
			   BIT(9));
}
#define SET_CMC_TBL_MASK_DATA_BW GENMASK(1, 0)
static inline void SET_CMC_TBL_DATA_BW(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(11, 10));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_DATA_BW,
			   GENMASK(11, 10));
}
#define SET_CMC_TBL_MASK_DATA_GI_LTF GENMASK(2, 0)
static inline void SET_CMC_TBL_DATA_GI_LTF(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(14, 12));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_DATA_GI_LTF,
			   GENMASK(14, 12));
}
#define SET_CMC_TBL_MASK_DARF_TC_INDEX BIT(0)
static inline void SET_CMC_TBL_DARF_TC_INDEX(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(15));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_DARF_TC_INDEX,
			   BIT(15));
}
#define SET_CMC_TBL_MASK_ARFR_CTRL GENMASK(3, 0)
static inline void SET_CMC_TBL_ARFR_CTRL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(19, 16));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_ARFR_CTRL,
			   GENMASK(19, 16));
}
#define SET_CMC_TBL_MASK_ACQ_RPT_EN BIT(0)
static inline void SET_CMC_TBL_ACQ_RPT_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(20));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_ACQ_RPT_EN,
			   BIT(20));
}
#define SET_CMC_TBL_MASK_MGQ_RPT_EN BIT(0)
static inline void SET_CMC_TBL_MGQ_RPT_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(21));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_MGQ_RPT_EN,
			   BIT(21));
}
#define SET_CMC_TBL_MASK_ULQ_RPT_EN BIT(0)
static inline void SET_CMC_TBL_ULQ_RPT_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(22));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_ULQ_RPT_EN,
			   BIT(22));
}
#define SET_CMC_TBL_MASK_TWTQ_RPT_EN BIT(0)
static inline void SET_CMC_TBL_TWTQ_RPT_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(23));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_TWTQ_RPT_EN,
			   BIT(23));
}
#define SET_CMC_TBL_MASK_DISRTSFB BIT(0)
static inline void SET_CMC_TBL_DISRTSFB(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(25));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_DISRTSFB,
			   BIT(25));
}
#define SET_CMC_TBL_MASK_DISDATAFB BIT(0)
static inline void SET_CMC_TBL_DISDATAFB(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(26));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_DISDATAFB,
			   BIT(26));
}
#define SET_CMC_TBL_MASK_TRYRATE BIT(0)
static inline void SET_CMC_TBL_TRYRATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(27));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_TRYRATE,
			   BIT(27));
}
#define SET_CMC_TBL_MASK_AMPDU_DENSITY GENMASK(3, 0)
static inline void SET_CMC_TBL_AMPDU_DENSITY(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(31, 28));
	le32p_replace_bits((__le32 *)(table) + 9, SET_CMC_TBL_MASK_AMPDU_DENSITY,
			   GENMASK(31, 28));
}
#define SET_CMC_TBL_MASK_DATA_RTY_LOWEST_RATE GENMASK(8, 0)
static inline void SET_CMC_TBL_DATA_RTY_LOWEST_RATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, GENMASK(8, 0));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_DATA_RTY_LOWEST_RATE,
			   GENMASK(8, 0));
}
#define SET_CMC_TBL_MASK_AMPDU_TIME_SEL BIT(0)
static inline void SET_CMC_TBL_AMPDU_TIME_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, BIT(9));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_AMPDU_TIME_SEL,
			   BIT(9));
}
#define SET_CMC_TBL_MASK_AMPDU_LEN_SEL BIT(0)
static inline void SET_CMC_TBL_AMPDU_LEN_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, BIT(10));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_AMPDU_LEN_SEL,
			   BIT(10));
}
#define SET_CMC_TBL_MASK_RTS_TXCNT_LMT_SEL BIT(0)
static inline void SET_CMC_TBL_RTS_TXCNT_LMT_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, BIT(11));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_RTS_TXCNT_LMT_SEL,
			   BIT(11));
}
#define SET_CMC_TBL_MASK_RTS_TXCNT_LMT GENMASK(3, 0)
static inline void SET_CMC_TBL_RTS_TXCNT_LMT(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, GENMASK(15, 12));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_RTS_TXCNT_LMT,
			   GENMASK(15, 12));
}
#define SET_CMC_TBL_MASK_RTSRATE GENMASK(8, 0)
static inline void SET_CMC_TBL_RTSRATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, GENMASK(24, 16));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_RTSRATE,
			   GENMASK(24, 16));
}
#define SET_CMC_TBL_MASK_VCS_STBC BIT(0)
static inline void SET_CMC_TBL_VCS_STBC(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, BIT(27));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_VCS_STBC,
			   BIT(27));
}
#define SET_CMC_TBL_MASK_RTS_RTY_LOWEST_RATE GENMASK(3, 0)
static inline void SET_CMC_TBL_RTS_RTY_LOWEST_RATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, GENMASK(31, 28));
	le32p_replace_bits((__le32 *)(table) + 10, SET_CMC_TBL_MASK_RTS_RTY_LOWEST_RATE,
			   GENMASK(31, 28));
}
#define SET_CMC_TBL_MASK_DATA_TX_CNT_LMT GENMASK(5, 0)
static inline void SET_CMC_TBL_DATA_TX_CNT_LMT(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(5, 0));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_DATA_TX_CNT_LMT,
			   GENMASK(5, 0));
}
#define SET_CMC_TBL_MASK_DATA_TXCNT_LMT_SEL BIT(0)
static inline void SET_CMC_TBL_DATA_TXCNT_LMT_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(6));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_DATA_TXCNT_LMT_SEL,
			   BIT(6));
}
#define SET_CMC_TBL_MASK_MAX_AGG_NUM_SEL BIT(0)
static inline void SET_CMC_TBL_MAX_AGG_NUM_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(7));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_MAX_AGG_NUM_SEL,
			   BIT(7));
}
#define SET_CMC_TBL_MASK_RTS_EN BIT(0)
static inline void SET_CMC_TBL_RTS_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(8));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_RTS_EN,
			   BIT(8));
}
#define SET_CMC_TBL_MASK_CTS2SELF_EN BIT(0)
static inline void SET_CMC_TBL_CTS2SELF_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(9));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_CTS2SELF_EN,
			   BIT(9));
}
#define SET_CMC_TBL_MASK_CCA_RTS GENMASK(1, 0)
static inline void SET_CMC_TBL_CCA_RTS(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(11, 10));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_CCA_RTS,
			   GENMASK(11, 10));
}
#define SET_CMC_TBL_MASK_HW_RTS_EN BIT(0)
static inline void SET_CMC_TBL_HW_RTS_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(12));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_HW_RTS_EN,
			   BIT(12));
}
#define SET_CMC_TBL_MASK_RTS_DROP_DATA_MODE GENMASK(1, 0)
static inline void SET_CMC_TBL_RTS_DROP_DATA_MODE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(14, 13));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_RTS_DROP_DATA_MODE,
			   GENMASK(14, 13));
}
#define SET_CMC_TBL_MASK_AMPDU_MAX_LEN GENMASK(10, 0)
static inline void SET_CMC_TBL_AMPDU_MAX_LEN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(26, 16));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_AMPDU_MAX_LEN,
			   GENMASK(26, 16));
}
#define SET_CMC_TBL_MASK_UL_MU_DIS BIT(0)
static inline void SET_CMC_TBL_UL_MU_DIS(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(27));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_UL_MU_DIS,
			   BIT(27));
}
#define SET_CMC_TBL_MASK_AMPDU_MAX_TIME GENMASK(3, 0)
static inline void SET_CMC_TBL_AMPDU_MAX_TIME(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(31, 28));
	le32p_replace_bits((__le32 *)(table) + 11, SET_CMC_TBL_MASK_AMPDU_MAX_TIME,
			   GENMASK(31, 28));
}
#define SET_CMC_TBL_MASK_MAX_AGG_NUM GENMASK(7, 0)
static inline void SET_CMC_TBL_MAX_AGG_NUM(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(7, 0));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_MAX_AGG_NUM,
			   GENMASK(7, 0));
}
#define SET_CMC_TBL_MASK_BA_BMAP GENMASK(1, 0)
static inline void SET_CMC_TBL_BA_BMAP(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(9, 8));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_BA_BMAP,
			   GENMASK(9, 8));
}
#define SET_CMC_TBL_MASK_VO_LFTIME_SEL GENMASK(2, 0)
static inline void SET_CMC_TBL_VO_LFTIME_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(18, 16));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_VO_LFTIME_SEL,
			   GENMASK(18, 16));
}
#define SET_CMC_TBL_MASK_VI_LFTIME_SEL GENMASK(2, 0)
static inline void SET_CMC_TBL_VI_LFTIME_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(21, 19));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_VI_LFTIME_SEL,
			   GENMASK(21, 19));
}
#define SET_CMC_TBL_MASK_BE_LFTIME_SEL GENMASK(2, 0)
static inline void SET_CMC_TBL_BE_LFTIME_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(24, 22));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_BE_LFTIME_SEL,
			   GENMASK(24, 22));
}
#define SET_CMC_TBL_MASK_BK_LFTIME_SEL GENMASK(2, 0)
static inline void SET_CMC_TBL_BK_LFTIME_SEL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(27, 25));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_BK_LFTIME_SEL,
			   GENMASK(27, 25));
}
#define SET_CMC_TBL_MASK_SECTYPE GENMASK(3, 0)
static inline void SET_CMC_TBL_SECTYPE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(31, 28));
	le32p_replace_bits((__le32 *)(table) + 12, SET_CMC_TBL_MASK_SECTYPE,
			   GENMASK(31, 28));
}
#define SET_CMC_TBL_MASK_MULTI_PORT_ID GENMASK(2, 0)
static inline void SET_CMC_TBL_MULTI_PORT_ID(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(2, 0));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_MULTI_PORT_ID,
			   GENMASK(2, 0));
}
#define SET_CMC_TBL_MASK_BMC BIT(0)
static inline void SET_CMC_TBL_BMC(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(3));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_BMC,
			   BIT(3));
}
#define SET_CMC_TBL_MASK_MBSSID GENMASK(3, 0)
static inline void SET_CMC_TBL_MBSSID(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(7, 4));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_MBSSID,
			   GENMASK(7, 4));
}
#define SET_CMC_TBL_MASK_NAVUSEHDR BIT(0)
static inline void SET_CMC_TBL_NAVUSEHDR(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(8));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_NAVUSEHDR,
			   BIT(8));
}
#define SET_CMC_TBL_MASK_TXPWR_MODE GENMASK(2, 0)
static inline void SET_CMC_TBL_TXPWR_MODE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(11, 9));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_TXPWR_MODE,
			   GENMASK(11, 9));
}
#define SET_CMC_TBL_MASK_DATA_DCM BIT(0)
static inline void SET_CMC_TBL_DATA_DCM(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(12));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_DATA_DCM,
			   BIT(12));
}
#define SET_CMC_TBL_MASK_DATA_ER BIT(0)
static inline void SET_CMC_TBL_DATA_ER(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(13));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_DATA_ER,
			   BIT(13));
}
#define SET_CMC_TBL_MASK_DATA_LDPC BIT(0)
static inline void SET_CMC_TBL_DATA_LDPC(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(14));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_DATA_LDPC,
			   BIT(14));
}
#define SET_CMC_TBL_MASK_DATA_STBC BIT(0)
static inline void SET_CMC_TBL_DATA_STBC(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(15));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_DATA_STBC,
			   BIT(15));
}
#define SET_CMC_TBL_MASK_A_CTRL_BQR BIT(0)
static inline void SET_CMC_TBL_A_CTRL_BQR(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(16));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_A_CTRL_BQR,
			   BIT(16));
}
#define SET_CMC_TBL_MASK_A_CTRL_UPH BIT(0)
static inline void SET_CMC_TBL_A_CTRL_UPH(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(17));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_A_CTRL_UPH,
			   BIT(17));
}
#define SET_CMC_TBL_MASK_A_CTRL_BSR BIT(0)
static inline void SET_CMC_TBL_A_CTRL_BSR(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(18));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_A_CTRL_BSR,
			   BIT(18));
}
#define SET_CMC_TBL_MASK_A_CTRL_CAS BIT(0)
static inline void SET_CMC_TBL_A_CTRL_CAS(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(19));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_A_CTRL_CAS,
			   BIT(19));
}
#define SET_CMC_TBL_MASK_DATA_BW_ER BIT(0)
static inline void SET_CMC_TBL_DATA_BW_ER(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(20));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_DATA_BW_ER,
			   BIT(20));
}
#define SET_CMC_TBL_MASK_LSIG_TXOP_EN BIT(0)
static inline void SET_CMC_TBL_LSIG_TXOP_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(21));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_LSIG_TXOP_EN,
			   BIT(21));
}
#define SET_CMC_TBL_MASK_CTRL_CNT_VLD BIT(0)
static inline void SET_CMC_TBL_CTRL_CNT_VLD(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(27));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_CTRL_CNT_VLD,
			   BIT(27));
}
#define SET_CMC_TBL_MASK_CTRL_CNT GENMASK(3, 0)
static inline void SET_CMC_TBL_CTRL_CNT(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(31, 28));
	le32p_replace_bits((__le32 *)(table) + 13, SET_CMC_TBL_MASK_CTRL_CNT,
			   GENMASK(31, 28));
}
#define SET_CMC_TBL_MASK_RESP_REF_RATE GENMASK(8, 0)
static inline void SET_CMC_TBL_RESP_REF_RATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(8, 0));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_RESP_REF_RATE,
			   GENMASK(8, 0));
}
#define SET_CMC_TBL_MASK_ALL_ACK_SUPPORT BIT(0)
static inline void SET_CMC_TBL_ALL_ACK_SUPPORT(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, BIT(12));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_ALL_ACK_SUPPORT,
			   BIT(12));
}
#define SET_CMC_TBL_MASK_BSR_QUEUE_SIZE_FORMAT BIT(0)
static inline void SET_CMC_TBL_BSR_QUEUE_SIZE_FORMAT(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, BIT(13));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_BSR_QUEUE_SIZE_FORMAT,
			   BIT(13));
}
#define SET_CMC_TBL_MASK_NTX_PATH_EN GENMASK(3, 0)
static inline void SET_CMC_TBL_NTX_PATH_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(19, 16));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_NTX_PATH_EN,
			   GENMASK(19, 16));
}
#define SET_CMC_TBL_MASK_PATH_MAP_A GENMASK(1, 0)
static inline void SET_CMC_TBL_PATH_MAP_A(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(21, 20));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_PATH_MAP_A,
			   GENMASK(21, 20));
}
#define SET_CMC_TBL_MASK_PATH_MAP_B GENMASK(1, 0)
static inline void SET_CMC_TBL_PATH_MAP_B(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(23, 22));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_PATH_MAP_B,
			   GENMASK(23, 22));
}
#define SET_CMC_TBL_MASK_PATH_MAP_C GENMASK(1, 0)
static inline void SET_CMC_TBL_PATH_MAP_C(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(25, 24));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_PATH_MAP_C,
			   GENMASK(25, 24));
}
#define SET_CMC_TBL_MASK_PATH_MAP_D GENMASK(1, 0)
static inline void SET_CMC_TBL_PATH_MAP_D(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(27, 26));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_PATH_MAP_D,
			   GENMASK(27, 26));
}
#define SET_CMC_TBL_MASK_ANTSEL_A BIT(0)
static inline void SET_CMC_TBL_ANTSEL_A(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, BIT(28));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_ANTSEL_A,
			   BIT(28));
}
#define SET_CMC_TBL_MASK_ANTSEL_B BIT(0)
static inline void SET_CMC_TBL_ANTSEL_B(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, BIT(29));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_ANTSEL_B,
			   BIT(29));
}
#define SET_CMC_TBL_MASK_ANTSEL_C BIT(0)
static inline void SET_CMC_TBL_ANTSEL_C(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, BIT(30));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_ANTSEL_C,
			   BIT(30));
}
#define SET_CMC_TBL_MASK_ANTSEL_D BIT(0)
static inline void SET_CMC_TBL_ANTSEL_D(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, BIT(31));
	le32p_replace_bits((__le32 *)(table) + 14, SET_CMC_TBL_MASK_ANTSEL_D,
			   BIT(31));
}

#define SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING GENMASK(1, 0)
static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(1, 0));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(1, 0));
}

static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING40_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(3, 2));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(3, 2));
}

static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING80_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(5, 4));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(5, 4));
}

static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING160_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(7, 6));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(7, 6));
}

#define SET_CMC_TBL_MASK_ADDR_CAM_INDEX GENMASK(7, 0)
static inline void SET_CMC_TBL_ADDR_CAM_INDEX(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(7, 0));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_ADDR_CAM_INDEX,
			   GENMASK(7, 0));
}
#define SET_CMC_TBL_MASK_PAID GENMASK(8, 0)
static inline void SET_CMC_TBL_PAID(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(16, 8));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_PAID,
			   GENMASK(16, 8));
}
#define SET_CMC_TBL_MASK_ULDL BIT(0)
static inline void SET_CMC_TBL_ULDL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, BIT(17));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_ULDL,
			   BIT(17));
}
#define SET_CMC_TBL_MASK_DOPPLER_CTRL GENMASK(1, 0)
static inline void SET_CMC_TBL_DOPPLER_CTRL(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(19, 18));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_DOPPLER_CTRL,
			   GENMASK(19, 18));
}
static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(21, 20));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(21, 20));
}

static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING40(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(23, 22));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(23, 22));
}
#define SET_CMC_TBL_MASK_TXPWR_TOLERENCE GENMASK(3, 0)
static inline void SET_CMC_TBL_TXPWR_TOLERENCE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(27, 24));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_TXPWR_TOLERENCE,
			   GENMASK(27, 24));
}

static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING80(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(31, 30));
	le32p_replace_bits((__le32 *)(table) + 15, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(31, 30));
}
#define SET_CMC_TBL_MASK_NC GENMASK(2, 0)
static inline void SET_CMC_TBL_NC(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(2, 0));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_NC,
			   GENMASK(2, 0));
}
#define SET_CMC_TBL_MASK_NR GENMASK(2, 0)
static inline void SET_CMC_TBL_NR(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(5, 3));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_NR,
			   GENMASK(5, 3));
}
#define SET_CMC_TBL_MASK_NG GENMASK(1, 0)
static inline void SET_CMC_TBL_NG(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(7, 6));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_NG,
			   GENMASK(7, 6));
}
#define SET_CMC_TBL_MASK_CB GENMASK(1, 0)
static inline void SET_CMC_TBL_CB(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(9, 8));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CB,
			   GENMASK(9, 8));
}
#define SET_CMC_TBL_MASK_CS GENMASK(1, 0)
static inline void SET_CMC_TBL_CS(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(11, 10));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CS,
			   GENMASK(11, 10));
}
#define SET_CMC_TBL_MASK_CSI_TXBF_EN BIT(0)
static inline void SET_CMC_TBL_CSI_TXBF_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, BIT(12));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_TXBF_EN,
			   BIT(12));
}
#define SET_CMC_TBL_MASK_CSI_STBC_EN BIT(0)
static inline void SET_CMC_TBL_CSI_STBC_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, BIT(13));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_STBC_EN,
			   BIT(13));
}
#define SET_CMC_TBL_MASK_CSI_LDPC_EN BIT(0)
static inline void SET_CMC_TBL_CSI_LDPC_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, BIT(14));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_LDPC_EN,
			   BIT(14));
}
#define SET_CMC_TBL_MASK_CSI_PARA_EN BIT(0)
static inline void SET_CMC_TBL_CSI_PARA_EN(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, BIT(15));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_PARA_EN,
			   BIT(15));
}
#define SET_CMC_TBL_MASK_CSI_FIX_RATE GENMASK(8, 0)
static inline void SET_CMC_TBL_CSI_FIX_RATE(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(24, 16));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_FIX_RATE,
			   GENMASK(24, 16));
}
#define SET_CMC_TBL_MASK_CSI_GI_LTF GENMASK(2, 0)
static inline void SET_CMC_TBL_CSI_GI_LTF(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(27, 25));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_GI_LTF,
			   GENMASK(27, 25));
}

static inline void SET_CMC_TBL_NOMINAL_PKT_PADDING160(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(29, 28));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_NOMINAL_PKT_PADDING,
			   GENMASK(29, 28));
}

#define SET_CMC_TBL_MASK_CSI_BW GENMASK(1, 0)
static inline void SET_CMC_TBL_CSI_BW(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 8, val, GENMASK(31, 30));
	le32p_replace_bits((__le32 *)(table) + 16, SET_CMC_TBL_MASK_CSI_BW,
			   GENMASK(31, 30));
}

static inline void SET_DCTL_MACID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 0, val, GENMASK(6, 0));
}

static inline void SET_DCTL_OPERATION_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 0, val, BIT(7));
}

#define SET_DCTL_MASK_QOS_FIELD_V1 GENMASK(7, 0)
static inline void SET_DCTL_QOS_FIELD_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(7, 0));
	le32p_replace_bits((__le32 *)(table) + 9, SET_DCTL_MASK_QOS_FIELD_V1,
			   GENMASK(7, 0));
}

#define SET_DCTL_MASK_SET_DCTL_HW_EXSEQ_MACID GENMASK(6, 0)
static inline void SET_DCTL_HW_EXSEQ_MACID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(14, 8));
	le32p_replace_bits((__le32 *)(table) + 9, SET_DCTL_MASK_SET_DCTL_HW_EXSEQ_MACID,
			   GENMASK(14, 8));
}

#define SET_DCTL_MASK_QOS_DATA BIT(0)
static inline void SET_DCTL_QOS_DATA_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, BIT(15));
	le32p_replace_bits((__le32 *)(table) + 9, SET_DCTL_MASK_QOS_DATA,
			   BIT(15));
}

#define SET_DCTL_MASK_AES_IV_L GENMASK(15, 0)
static inline void SET_DCTL_AES_IV_L_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 1, val, GENMASK(31, 16));
	le32p_replace_bits((__le32 *)(table) + 9, SET_DCTL_MASK_AES_IV_L,
			   GENMASK(31, 16));
}

#define SET_DCTL_MASK_AES_IV_H GENMASK(31, 0)
static inline void SET_DCTL_AES_IV_H_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 2, val, GENMASK(31, 0));
	le32p_replace_bits((__le32 *)(table) + 10, SET_DCTL_MASK_AES_IV_H,
			   GENMASK(31, 0));
}

#define SET_DCTL_MASK_SEQ0 GENMASK(11, 0)
static inline void SET_DCTL_SEQ0_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(11, 0));
	le32p_replace_bits((__le32 *)(table) + 11, SET_DCTL_MASK_SEQ0,
			   GENMASK(11, 0));
}

#define SET_DCTL_MASK_SEQ1 GENMASK(11, 0)
static inline void SET_DCTL_SEQ1_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(23, 12));
	le32p_replace_bits((__le32 *)(table) + 11, SET_DCTL_MASK_SEQ1,
			   GENMASK(23, 12));
}

#define SET_DCTL_MASK_AMSDU_MAX_LEN GENMASK(2, 0)
static inline void SET_DCTL_AMSDU_MAX_LEN_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, GENMASK(26, 24));
	le32p_replace_bits((__le32 *)(table) + 11, SET_DCTL_MASK_AMSDU_MAX_LEN,
			   GENMASK(26, 24));
}

#define SET_DCTL_MASK_STA_AMSDU_EN BIT(0)
static inline void SET_DCTL_STA_AMSDU_EN_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(27));
	le32p_replace_bits((__le32 *)(table) + 11, SET_DCTL_MASK_STA_AMSDU_EN,
			   BIT(27));
}

#define SET_DCTL_MASK_CHKSUM_OFLD_EN BIT(0)
static inline void SET_DCTL_CHKSUM_OFLD_EN_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(28));
	le32p_replace_bits((__le32 *)(table) + 11, SET_DCTL_MASK_CHKSUM_OFLD_EN,
			   BIT(28));
}

#define SET_DCTL_MASK_WITH_LLC BIT(0)
static inline void SET_DCTL_WITH_LLC_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 3, val, BIT(29));
	le32p_replace_bits((__le32 *)(table) + 11, SET_DCTL_MASK_WITH_LLC,
			   BIT(29));
}

#define SET_DCTL_MASK_SEQ2 GENMASK(11, 0)
static inline void SET_DCTL_SEQ2_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(11, 0));
	le32p_replace_bits((__le32 *)(table) + 12, SET_DCTL_MASK_SEQ2,
			   GENMASK(11, 0));
}

#define SET_DCTL_MASK_SEQ3 GENMASK(11, 0)
static inline void SET_DCTL_SEQ3_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(23, 12));
	le32p_replace_bits((__le32 *)(table) + 12, SET_DCTL_MASK_SEQ3,
			   GENMASK(23, 12));
}

#define SET_DCTL_MASK_TGT_IND GENMASK(3, 0)
static inline void SET_DCTL_TGT_IND_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(27, 24));
	le32p_replace_bits((__le32 *)(table) + 12, SET_DCTL_MASK_TGT_IND,
			   GENMASK(27, 24));
}

#define SET_DCTL_MASK_TGT_IND_EN BIT(0)
static inline void SET_DCTL_TGT_IND_EN_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, BIT(28));
	le32p_replace_bits((__le32 *)(table) + 12, SET_DCTL_MASK_TGT_IND_EN,
			   BIT(28));
}

#define SET_DCTL_MASK_HTC_LB GENMASK(2, 0)
static inline void SET_DCTL_HTC_LB_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 4, val, GENMASK(31, 29));
	le32p_replace_bits((__le32 *)(table) + 12, SET_DCTL_MASK_HTC_LB,
			   GENMASK(31, 29));
}

#define SET_DCTL_MASK_MHDR_LEN GENMASK(4, 0)
static inline void SET_DCTL_MHDR_LEN_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(4, 0));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_MHDR_LEN,
			   GENMASK(4, 0));
}

#define SET_DCTL_MASK_VLAN_TAG_VALID BIT(0)
static inline void SET_DCTL_VLAN_TAG_VALID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(5));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_VLAN_TAG_VALID,
			   BIT(5));
}

#define SET_DCTL_MASK_VLAN_TAG_SEL GENMASK(1, 0)
static inline void SET_DCTL_VLAN_TAG_SEL_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(7, 6));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_VLAN_TAG_SEL,
			   GENMASK(7, 6));
}

#define SET_DCTL_MASK_HTC_ORDER BIT(0)
static inline void SET_DCTL_HTC_ORDER_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(8));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_HTC_ORDER,
			   BIT(8));
}

#define SET_DCTL_MASK_SEC_KEY_ID GENMASK(1, 0)
static inline void SET_DCTL_SEC_KEY_ID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(10, 9));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_KEY_ID,
			   GENMASK(10, 9));
}

#define SET_DCTL_MASK_WAPI BIT(0)
static inline void SET_DCTL_WAPI_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, BIT(15));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_WAPI,
			   BIT(15));
}

#define SET_DCTL_MASK_SEC_ENT_MODE GENMASK(1, 0)
static inline void SET_DCTL_SEC_ENT_MODE_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(17, 16));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENT_MODE,
			   GENMASK(17, 16));
}

#define SET_DCTL_MASK_SEC_ENTX_KEYID GENMASK(1, 0)
static inline void SET_DCTL_SEC_ENT0_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(19, 18));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(19, 18));
}

static inline void SET_DCTL_SEC_ENT1_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(21, 20));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(21, 20));
}

static inline void SET_DCTL_SEC_ENT2_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(23, 22));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(23, 22));
}

static inline void SET_DCTL_SEC_ENT3_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(25, 24));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(25, 24));
}

static inline void SET_DCTL_SEC_ENT4_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(27, 26));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(27, 26));
}

static inline void SET_DCTL_SEC_ENT5_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(29, 28));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(29, 28));
}

static inline void SET_DCTL_SEC_ENT6_KEYID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 5, val, GENMASK(31, 30));
	le32p_replace_bits((__le32 *)(table) + 13, SET_DCTL_MASK_SEC_ENTX_KEYID,
			   GENMASK(31, 30));
}

#define SET_DCTL_MASK_SEC_ENT_VALID GENMASK(7, 0)
static inline void SET_DCTL_SEC_ENT_VALID_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(7, 0));
	le32p_replace_bits((__le32 *)(table) + 14, SET_DCTL_MASK_SEC_ENT_VALID,
			   GENMASK(7, 0));
}

#define SET_DCTL_MASK_SEC_ENTX GENMASK(7, 0)
static inline void SET_DCTL_SEC_ENT0_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(15, 8));
	le32p_replace_bits((__le32 *)(table) + 14, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(15, 8));
}

static inline void SET_DCTL_SEC_ENT1_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(23, 16));
	le32p_replace_bits((__le32 *)(table) + 14, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(23, 16));
}

static inline void SET_DCTL_SEC_ENT2_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 6, val, GENMASK(31, 24));
	le32p_replace_bits((__le32 *)(table) + 14, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(31, 24));
}

static inline void SET_DCTL_SEC_ENT3_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(7, 0));
	le32p_replace_bits((__le32 *)(table) + 15, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(7, 0));
}

static inline void SET_DCTL_SEC_ENT4_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(15, 8));
	le32p_replace_bits((__le32 *)(table) + 15, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(15, 8));
}

static inline void SET_DCTL_SEC_ENT5_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(23, 16));
	le32p_replace_bits((__le32 *)(table) + 15, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(23, 16));
}

static inline void SET_DCTL_SEC_ENT6_V1(void *table, u32 val)
{
	le32p_replace_bits((__le32 *)(table) + 7, val, GENMASK(31, 24));
	le32p_replace_bits((__le32 *)(table) + 15, SET_DCTL_MASK_SEC_ENTX,
			   GENMASK(31, 24));
}

static inline void SET_BCN_UPD_PORT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 0));
}

static inline void SET_BCN_UPD_MBSSID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void SET_BCN_UPD_BAND(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(23, 16));
}

static inline void SET_BCN_UPD_GRP_IE_OFST(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, (val - 24) | BIT(7), GENMASK(31, 24));
}

static inline void SET_BCN_UPD_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(7, 0));
}

static inline void SET_BCN_UPD_SSN_SEL(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(9, 8));
}

static inline void SET_BCN_UPD_SSN_MODE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(11, 10));
}

static inline void SET_BCN_UPD_RATE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(20, 12));
}

static inline void SET_BCN_UPD_TXPWR(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(23, 21));
}

static inline void SET_BCN_UPD_TXINFO_CTRL_EN(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val, BIT(0));
}

static inline void SET_BCN_UPD_NTX_PATH_EN(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  GENMASK(4, 1));
}

static inline void SET_BCN_UPD_PATH_MAP_A(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  GENMASK(6, 5));
}

static inline void SET_BCN_UPD_PATH_MAP_B(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  GENMASK(8, 7));
}

static inline void SET_BCN_UPD_PATH_MAP_C(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  GENMASK(10, 9));
}

static inline void SET_BCN_UPD_PATH_MAP_D(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  GENMASK(12, 11));
}

static inline void SET_BCN_UPD_PATH_ANTSEL_A(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  BIT(13));
}

static inline void SET_BCN_UPD_PATH_ANTSEL_B(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  BIT(14));
}

static inline void SET_BCN_UPD_PATH_ANTSEL_C(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  BIT(15));
}

static inline void SET_BCN_UPD_PATH_ANTSEL_D(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  BIT(16));
}

static inline void SET_BCN_UPD_CSA_OFST(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val,  GENMASK(31, 17));
}

static inline void SET_FWROLE_MAINTAIN_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 0));
}

static inline void SET_FWROLE_MAINTAIN_SELF_ROLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(9, 8));
}

static inline void SET_FWROLE_MAINTAIN_UPD_MODE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(12, 10));
}

static inline void SET_FWROLE_MAINTAIN_WIFI_ROLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(16, 13));
}

static inline void SET_JOININFO_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 0));
}

static inline void SET_JOININFO_OP(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(8));
}

static inline void SET_JOININFO_BAND(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(9));
}

static inline void SET_JOININFO_WMM(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(11, 10));
}

static inline void SET_JOININFO_TGR(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(12));
}

static inline void SET_JOININFO_ISHESTA(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(13));
}

static inline void SET_JOININFO_DLBW(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 14));
}

static inline void SET_JOININFO_TF_MAC_PAD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(17, 16));
}

static inline void SET_JOININFO_DL_T_PE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(20, 18));
}

static inline void SET_JOININFO_PORT_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(23, 21));
}

static inline void SET_JOININFO_NET_TYPE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(25, 24));
}

static inline void SET_JOININFO_WIFI_ROLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(29, 26));
}

static inline void SET_JOININFO_SELF_ROLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 30));
}

static inline void SET_GENERAL_PKT_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 0));
}

static inline void SET_GENERAL_PKT_PROBRSP_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void SET_GENERAL_PKT_PSPOLL_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(23, 16));
}

static inline void SET_GENERAL_PKT_NULL_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 24));
}

static inline void SET_GENERAL_PKT_QOS_NULL_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(7, 0));
}

static inline void SET_GENERAL_PKT_CTS2SELF_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(15, 8));
}

static inline void SET_LOG_CFG_LEVEL(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 0));
}

static inline void SET_LOG_CFG_PATH(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void SET_LOG_CFG_COMP(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(31, 0));
}

static inline void SET_LOG_CFG_COMP_EXT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 2, val, GENMASK(31, 0));
}

static inline void SET_BA_CAM_VALID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(0));
}

static inline void SET_BA_CAM_INIT_REQ(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(1));
}

static inline void SET_BA_CAM_ENTRY_IDX(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(3, 2));
}

static inline void SET_BA_CAM_TID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 4));
}

static inline void SET_BA_CAM_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void SET_BA_CAM_BMAP_SIZE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(19, 16));
}

static inline void SET_BA_CAM_SSN(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 20));
}

static inline void SET_BA_CAM_UID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 1, val, GENMASK(7, 0));
}

static inline void SET_BA_CAM_STD_EN(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 1, val, BIT(8));
}

static inline void SET_BA_CAM_BAND(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 1, val, BIT(9));
}

static inline void SET_BA_CAM_ENTRY_IDX_V1(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 1, val, GENMASK(31, 28));
}

static inline void SET_LPS_PARM_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 0));
}

static inline void SET_LPS_PARM_PSMODE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void SET_LPS_PARM_RLBM(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(19, 16));
}

static inline void SET_LPS_PARM_SMARTPS(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(23, 20));
}

static inline void SET_LPS_PARM_AWAKEINTERVAL(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 24));
}

static inline void SET_LPS_PARM_VOUAPSD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, BIT(0));
}

static inline void SET_LPS_PARM_VIUAPSD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, BIT(1));
}

static inline void SET_LPS_PARM_BEUAPSD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, BIT(2));
}

static inline void SET_LPS_PARM_BKUAPSD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, BIT(3));
}

static inline void SET_LPS_PARM_LASTRPWM(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_CPU_EXCEPTION_TYPE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_SEL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_BAND(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_PORT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_MBSSID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_ROLE_A_INFO_TF_TRS(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_0(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 2, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_1(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 3, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_2(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 4, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_PKT_DROP_MACID_BAND_SEL_3(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 5, val, GENMASK(31, 0));
}

static inline void RTW89_SET_KEEP_ALIVE_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(1, 0));
}

static inline void RTW89_SET_KEEP_ALIVE_PKT_NULL_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void RTW89_SET_KEEP_ALIVE_PERIOD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(24, 16));
}

static inline void RTW89_SET_KEEP_ALIVE_MACID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 24));
}

static inline void RTW89_SET_DISCONNECT_DETECT_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(0));
}

static inline void RTW89_SET_DISCONNECT_DETECT_TRYOK_BCNFAIL_COUNT_EN(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(1));
}

static inline void RTW89_SET_DISCONNECT_DETECT_DISCONNECT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(2));
}

static inline void RTW89_SET_DISCONNECT_DETECT_MAC_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void RTW89_SET_DISCONNECT_DETECT_CHECK_PERIOD(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(23, 16));
}

static inline void RTW89_SET_DISCONNECT_DETECT_TRY_PKT_COUNT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 24));
}

static inline void RTW89_SET_DISCONNECT_DETECT_TRYOK_BCNFAIL_COUNT_LIMIT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(7, 0));
}

static inline void RTW89_SET_WOW_GLOBAL_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(0));
}

static inline void RTW89_SET_WOW_GLOBAL_DROP_ALL_PKT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(1));
}

static inline void RTW89_SET_WOW_GLOBAL_RX_PARSE_AFTER_WAKE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(2));
}

static inline void RTW89_SET_WOW_GLOBAL_WAKE_BAR_PULLED(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(3));
}

static inline void RTW89_SET_WOW_GLOBAL_MAC_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(15, 8));
}

static inline void RTW89_SET_WOW_GLOBAL_PAIRWISE_SEC_ALGO(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(23, 16));
}

static inline void RTW89_SET_WOW_GLOBAL_GROUP_SEC_ALGO(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 24));
}

static inline void RTW89_SET_WOW_GLOBAL_REMOTECTRL_INFO_CONTENT(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)(h2c) + 1, val, GENMASK(31, 0));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_PATTERN_MATCH_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(0));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_MAGIC_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(1));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_HW_UNICAST_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(2));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_FW_UNICAST_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(3));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_DEAUTH_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(4));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_REKEYP_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(5));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_EAP_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(6));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_ALL_DATA_ENABLE(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(7));
}

static inline void RTW89_SET_WOW_WAKEUP_CTRL_MAC_ID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(31, 24));
}

static inline void RTW89_SET_WOW_CAM_UPD_R_W(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, BIT(0));
}

static inline void RTW89_SET_WOW_CAM_UPD_IDX(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c, val, GENMASK(7, 1));
}

static inline void RTW89_SET_WOW_CAM_UPD_WKFM1(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 1, val, GENMASK(31, 0));
}

static inline void RTW89_SET_WOW_CAM_UPD_WKFM2(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 2, val, GENMASK(31, 0));
}

static inline void RTW89_SET_WOW_CAM_UPD_WKFM3(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 3, val, GENMASK(31, 0));
}

static inline void RTW89_SET_WOW_CAM_UPD_WKFM4(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 4, val, GENMASK(31, 0));
}

static inline void RTW89_SET_WOW_CAM_UPD_CRC(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, GENMASK(15, 0));
}

static inline void RTW89_SET_WOW_CAM_UPD_NEGATIVE_PATTERN_MATCH(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, BIT(22));
}

static inline void RTW89_SET_WOW_CAM_UPD_SKIP_MAC_HDR(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, BIT(23));
}

static inline void RTW89_SET_WOW_CAM_UPD_UC(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, BIT(24));
}

static inline void RTW89_SET_WOW_CAM_UPD_MC(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, BIT(25));
}

static inline void RTW89_SET_WOW_CAM_UPD_BC(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, BIT(26));
}

static inline void RTW89_SET_WOW_CAM_UPD_VALID(void *h2c, u32 val)
{
	le32p_replace_bits((__le32 *)h2c + 5, val, BIT(31));
}

enum rtw89_btc_btf_h2c_class {
	BTFC_SET = 0x10,
	BTFC_GET = 0x11,
	BTFC_FW_EVENT = 0x12,
};

enum rtw89_btc_btf_set {
	SET_REPORT_EN = 0x0,
	SET_SLOT_TABLE,
	SET_MREG_TABLE,
	SET_CX_POLICY,
	SET_GPIO_DBG,
	SET_DRV_INFO,
	SET_DRV_EVENT,
	SET_BT_WREG_ADDR,
	SET_BT_WREG_VAL,
	SET_BT_RREG_ADDR,
	SET_BT_WL_CH_INFO,
	SET_BT_INFO_REPORT,
	SET_BT_IGNORE_WLAN_ACT,
	SET_BT_TX_PWR,
	SET_BT_LNA_CONSTRAIN,
	SET_BT_GOLDEN_RX_RANGE,
	SET_BT_PSD_REPORT,
	SET_H2C_TEST,
	SET_MAX1,
};

enum rtw89_btc_cxdrvinfo {
	CXDRVINFO_INIT = 0,
	CXDRVINFO_ROLE,
	CXDRVINFO_DBCC,
	CXDRVINFO_SMAP,
	CXDRVINFO_RFK,
	CXDRVINFO_RUN,
	CXDRVINFO_CTRL,
	CXDRVINFO_SCAN,
	CXDRVINFO_TRX,  /* WL traffic to WL fw */
	CXDRVINFO_MAX,
};

enum rtw89_scan_mode {
	RTW89_SCAN_IMMEDIATE,
};

enum rtw89_scan_type {
	RTW89_SCAN_ONCE,
};

static inline void RTW89_SET_FWCMD_CXHDR_TYPE(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)(cmd) + 0, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXHDR_LEN(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)(cmd) + 1, val, GENMASK(7, 0));
}

struct rtw89_h2c_cxhdr {
	u8 type;
	u8 len;
} __packed;

#define H2C_LEN_CXDRVHDR sizeof(struct rtw89_h2c_cxhdr)

struct rtw89_h2c_cxinit {
	struct rtw89_h2c_cxhdr hdr;
	u8 ant_type;
	u8 ant_num;
	u8 ant_iso;
	u8 ant_info;
	u8 mod_rfe;
	u8 mod_cv;
	u8 mod_info;
	u8 mod_adie_kt;
	u8 wl_gch;
	u8 info;
	u8 rsvd;
	u8 rsvd1;
} __packed;

#define RTW89_H2C_CXINIT_ANT_INFO_POS BIT(0)
#define RTW89_H2C_CXINIT_ANT_INFO_DIVERSITY BIT(1)
#define RTW89_H2C_CXINIT_ANT_INFO_BTG_POS GENMASK(3, 2)
#define RTW89_H2C_CXINIT_ANT_INFO_STREAM_CNT GENMASK(7, 4)

#define RTW89_H2C_CXINIT_MOD_INFO_BT_SOLO BIT(0)
#define RTW89_H2C_CXINIT_MOD_INFO_BT_POS BIT(1)
#define RTW89_H2C_CXINIT_MOD_INFO_SW_TYPE BIT(2)
#define RTW89_H2C_CXINIT_MOD_INFO_WA_TYPE GENMASK(5, 3)

#define RTW89_H2C_CXINIT_INFO_WL_ONLY BIT(0)
#define RTW89_H2C_CXINIT_INFO_WL_INITOK BIT(1)
#define RTW89_H2C_CXINIT_INFO_DBCC_EN BIT(2)
#define RTW89_H2C_CXINIT_INFO_CX_OTHER BIT(3)
#define RTW89_H2C_CXINIT_INFO_BT_ONLY BIT(4)

static inline void RTW89_SET_FWCMD_CXROLE_CONNECT_CNT(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)(cmd) + 2, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_LINK_MODE(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)(cmd) + 3, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_NONE(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_STA(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(1));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_AP(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(2));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_VAP(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(3));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(4));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_ADHOC_MASTER(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(5));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_MESH(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(6));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_MONITOR(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(7));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_P2P_DEV(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(8));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GC(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(9));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_P2P_GO(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(10));
}

static inline void RTW89_SET_FWCMD_CXROLE_ROLE_NAN(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)(cmd) + 4), val, BIT(11));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_CONNECTED(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_PID(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, GENMASK(3, 1));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_PHY(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, BIT(4));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_NOA(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, BIT(5));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_BAND(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, GENMASK(7, 6));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_CLIENT_PS(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (7 + (12 + offset) * n), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_BW(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (7 + (12 + offset) * n), val, GENMASK(7, 1));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_ROLE(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (8 + (12 + offset) * n), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_CH(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (9 + (12 + offset) * n), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_TX_LVL(void *cmd, u16 val, int n, u8 offset)
{
	le16p_replace_bits((__le16 *)((u8 *)cmd + (10 + (12 + offset) * n)), val, GENMASK(15, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_RX_LVL(void *cmd, u16 val, int n, u8 offset)
{
	le16p_replace_bits((__le16 *)((u8 *)cmd + (12 + (12 + offset) * n)), val, GENMASK(15, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_TX_RATE(void *cmd, u16 val, int n, u8 offset)
{
	le16p_replace_bits((__le16 *)((u8 *)cmd + (14 + (12 + offset) * n)), val, GENMASK(15, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_RX_RATE(void *cmd, u16 val, int n, u8 offset)
{
	le16p_replace_bits((__le16 *)((u8 *)cmd + (16 + (12 + offset) * n)), val, GENMASK(15, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_NOA_DUR(void *cmd, u32 val, int n, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + (20 + (12 + offset) * n)), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_CONNECTED_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_PID_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, GENMASK(3, 1));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_PHY_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, BIT(4));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_NOA_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, BIT(5));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_BAND_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (6 + (12 + offset) * n), val, GENMASK(7, 6));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_CLIENT_PS_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (7 + (12 + offset) * n), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_BW_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (7 + (12 + offset) * n), val, GENMASK(7, 1));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_ROLE_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (8 + (12 + offset) * n), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_CH_V2(void *cmd, u8 val, int n, u8 offset)
{
	u8p_replace_bits((u8 *)cmd + (9 + (12 + offset) * n), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_ACT_NOA_DUR_V2(void *cmd, u32 val, int n, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + (10 + (12 + offset) * n)), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_MROLE_TYPE(void *cmd, u32 val, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + offset), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_MROLE_NOA(void *cmd, u32 val, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + offset + 4), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXROLE_DBCC_EN(void *cmd, u32 val, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + offset + 8), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXROLE_DBCC_CHG(void *cmd, u32 val, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + offset + 8), val, BIT(1));
}

static inline void RTW89_SET_FWCMD_CXROLE_DBCC_2G_PHY(void *cmd, u32 val, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + offset + 8), val, GENMASK(3, 2));
}

static inline void RTW89_SET_FWCMD_CXROLE_LINK_MODE_CHG(void *cmd, u32 val, u8 offset)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + offset + 8), val, BIT(4));
}

static inline void RTW89_SET_FWCMD_CXCTRL_MANUAL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, BIT(0));
}

static inline void RTW89_SET_FWCMD_CXCTRL_IGNORE_BT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, BIT(1));
}

static inline void RTW89_SET_FWCMD_CXCTRL_ALWAYS_FREERUN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, BIT(2));
}

static inline void RTW89_SET_FWCMD_CXCTRL_TRACE_STEP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, GENMASK(18, 3));
}

static inline void RTW89_SET_FWCMD_CXTRX_TXLV(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 2, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_RXLV(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 3, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_WLRSSI(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 4, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_BTRSSI(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 5, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_TXPWR(void *cmd, s8 val)
{
	u8p_replace_bits((u8 *)cmd + 6, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_RXGAIN(void *cmd, s8 val)
{
	u8p_replace_bits((u8 *)cmd + 7, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_BTTXPWR(void *cmd, s8 val)
{
	u8p_replace_bits((u8 *)cmd + 8, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_BTRXGAIN(void *cmd, s8 val)
{
	u8p_replace_bits((u8 *)cmd + 9, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_CN(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 10, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_NHM(void *cmd, s8 val)
{
	u8p_replace_bits((u8 *)cmd + 11, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_BTPROFILE(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 12, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_RSVD2(void *cmd, u8 val)
{
	u8p_replace_bits((u8 *)cmd + 13, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_TXRATE(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)cmd + 14), val, GENMASK(15, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_RXRATE(void *cmd, u16 val)
{
	le16p_replace_bits((__le16 *)((u8 *)cmd + 16), val, GENMASK(15, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_TXTP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + 18), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_RXTP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + 22), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXTRX_RXERRRA(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)cmd + 26), val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_CXRFK_STATE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, GENMASK(1, 0));
}

static inline void RTW89_SET_FWCMD_CXRFK_PATH_MAP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, GENMASK(5, 2));
}

static inline void RTW89_SET_FWCMD_CXRFK_PHY_MAP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, GENMASK(7, 6));
}

static inline void RTW89_SET_FWCMD_CXRFK_BAND(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, GENMASK(9, 8));
}

static inline void RTW89_SET_FWCMD_CXRFK_TYPE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 2), val, GENMASK(17, 10));
}

static inline void RTW89_SET_FWCMD_PACKET_OFLD_PKT_IDX(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_PACKET_OFLD_PKT_OP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(10, 8));
}

static inline void RTW89_SET_FWCMD_PACKET_OFLD_PKT_LENGTH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(31, 16));
}

static inline void RTW89_SET_FWCMD_SCANOFLD_CH_NUM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_SCANOFLD_CH_SIZE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_CHINFO_PERIOD(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CHINFO_DWELL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_CHINFO_CENTER_CH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_CHINFO_PRI_CH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd)), val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_CHINFO_BW(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, GENMASK(2, 0));
}

static inline void RTW89_SET_FWCMD_CHINFO_ACTION(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, GENMASK(7, 3));
}

static inline void RTW89_SET_FWCMD_CHINFO_NUM_PKT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, GENMASK(11, 8));
}

static inline void RTW89_SET_FWCMD_CHINFO_TX(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, BIT(12));
}

static inline void RTW89_SET_FWCMD_CHINFO_PAUSE_DATA(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, BIT(13));
}

static inline void RTW89_SET_FWCMD_CHINFO_BAND(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, GENMASK(15, 14));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT_ID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_CHINFO_DFS(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, BIT(24));
}

static inline void RTW89_SET_FWCMD_CHINFO_TX_NULL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, BIT(25));
}

static inline void RTW89_SET_FWCMD_CHINFO_RANDOM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, BIT(26));
}

static inline void RTW89_SET_FWCMD_CHINFO_CFG_TX(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 4), val, BIT(27));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT0(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 8), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT1(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 8), val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT2(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 8), val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT3(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 8), val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT4(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 12), val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT5(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 12), val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT6(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 12), val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_CHINFO_PKT7(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 12), val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_CHINFO_POWER_IDX(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)((u8 *)(cmd) + 16), val, GENMASK(15, 0));
}

struct rtw89_h2c_scanofld {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 tsf_high;
	__le32 tsf_low;
	__le32 w5;
	__le32 w6;
} __packed;

#define RTW89_H2C_SCANOFLD_W0_MACID GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_W0_NORM_CY GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_W0_PORT_ID GENMASK(18, 16)
#define RTW89_H2C_SCANOFLD_W0_BAND BIT(19)
#define RTW89_H2C_SCANOFLD_W0_OPERATION GENMASK(21, 20)
#define RTW89_H2C_SCANOFLD_W0_TARGET_CH_BAND GENMASK(23, 22)
#define RTW89_H2C_SCANOFLD_W1_NOTIFY_END BIT(0)
#define RTW89_H2C_SCANOFLD_W1_TARGET_CH_MODE BIT(1)
#define RTW89_H2C_SCANOFLD_W1_START_MODE BIT(2)
#define RTW89_H2C_SCANOFLD_W1_SCAN_TYPE GENMASK(4, 3)
#define RTW89_H2C_SCANOFLD_W1_TARGET_CH_BW GENMASK(7, 5)
#define RTW89_H2C_SCANOFLD_W1_TARGET_PRI_CH GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_W1_TARGET_CENTRAL_CH GENMASK(23, 16)
#define RTW89_H2C_SCANOFLD_W1_PROBE_REQ_PKT_ID GENMASK(31, 24)
#define RTW89_H2C_SCANOFLD_W2_NORM_PD GENMASK(15, 0)
#define RTW89_H2C_SCANOFLD_W2_SLOW_PD GENMASK(23, 16)

static inline void RTW89_SET_FWCMD_P2P_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_P2P_P2PID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(11, 8));
}

static inline void RTW89_SET_FWCMD_P2P_NOAID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 12));
}

static inline void RTW89_SET_FWCMD_P2P_ACT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(19, 16));
}

static inline void RTW89_SET_FWCMD_P2P_TYPE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(20));
}

static inline void RTW89_SET_FWCMD_P2P_ALL_SLEP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(21));
}

static inline void RTW89_SET_FWCMD_NOA_START_TIME(void *cmd, __le32 val)
{
	*((__le32 *)cmd + 1) = val;
}

static inline void RTW89_SET_FWCMD_NOA_INTERVAL(void *cmd, __le32 val)
{
	*((__le32 *)cmd + 2) = val;
}

static inline void RTW89_SET_FWCMD_NOA_DURATION(void *cmd, __le32 val)
{
	*((__le32 *)cmd + 3) = val;
}

static inline void RTW89_SET_FWCMD_NOA_COUNT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)(cmd) + 4, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_NOA_CTWINDOW(void *cmd, u32 val)
{
	u8 ctwnd;

	if (!(val & IEEE80211_P2P_OPPPS_ENABLE_BIT))
		return;
	ctwnd = FIELD_GET(IEEE80211_P2P_OPPPS_CTWINDOW_MASK, val);
	le32p_replace_bits((__le32 *)(cmd) + 4, ctwnd, GENMASK(23, 8));
}

static inline void RTW89_SET_FWCMD_TSF32_TOGL_BAND(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(0));
}

static inline void RTW89_SET_FWCMD_TSF32_TOGL_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(1));
}

static inline void RTW89_SET_FWCMD_TSF32_TOGL_PORT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(4, 2));
}

static inline void RTW89_SET_FWCMD_TSF32_TOGL_EARLY(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 16));
}

enum rtw89_fw_mcc_c2h_rpt_cfg {
	RTW89_FW_MCC_C2H_RPT_OFF	= 0,
	RTW89_FW_MCC_C2H_RPT_FAIL_ONLY	= 1,
	RTW89_FW_MCC_C2H_RPT_ALL	= 2,
};

struct rtw89_fw_mcc_add_req {
	u8 macid;
	u8 central_ch_seg0;
	u8 central_ch_seg1;
	u8 primary_ch;
	enum rtw89_bandwidth bandwidth: 4;
	u32 group: 2;
	u32 c2h_rpt: 2;
	u32 dis_tx_null: 1;
	u32 dis_sw_retry: 1;
	u32 in_curr_ch: 1;
	u32 sw_retry_count: 3;
	u32 tx_null_early: 4;
	u32 btc_in_2g: 1;
	u32 pta_en: 1;
	u32 rfk_by_pass: 1;
	u32 ch_band_type: 2;
	u32 rsvd0: 9;
	u32 duration;
	u8 courtesy_en;
	u8 courtesy_num;
	u8 courtesy_target;
	u8 rsvd1;
};

static inline void RTW89_SET_FWCMD_ADD_MCC_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_CENTRAL_CH_SEG0(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_CENTRAL_CH_SEG1(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_PRIMARY_CH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_BANDWIDTH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(3, 0));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(5, 4));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_C2H_RPT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(7, 6));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_DIS_TX_NULL(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, BIT(8));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_DIS_SW_RETRY(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, BIT(9));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_IN_CURR_CH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, BIT(10));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_SW_RETRY_COUNT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(13, 11));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_TX_NULL_EARLY(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(17, 14));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_BTC_IN_2G(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, BIT(18));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_PTA_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, BIT(19));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_RFK_BY_PASS(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, BIT(20));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_CH_BAND_TYPE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(22, 21));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_DURATION(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 2, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_COURTESY_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 3, val, BIT(0));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_COURTESY_NUM(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 3, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_ADD_MCC_COURTESY_TARGET(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 3, val, GENMASK(23, 16));
}

struct rtw89_fw_mcc_start_req {
	u32 group: 2;
	u32 btc_in_group: 1;
	u32 old_group_action: 2;
	u32 old_group: 2;
	u32 rsvd0: 9;
	u32 notify_cnt: 3;
	u32 rsvd1: 2;
	u32 notify_rxdbg_en: 1;
	u32 rsvd2: 2;
	u32 macid: 8;
	u32 tsf_low;
	u32 tsf_high;
};

static inline void RTW89_SET_FWCMD_START_MCC_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

static inline void RTW89_SET_FWCMD_START_MCC_BTC_IN_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(2));
}

static inline void RTW89_SET_FWCMD_START_MCC_OLD_GROUP_ACTION(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(4, 3));
}

static inline void RTW89_SET_FWCMD_START_MCC_OLD_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(6, 5));
}

static inline void RTW89_SET_FWCMD_START_MCC_NOTIFY_CNT(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(18, 16));
}

static inline void RTW89_SET_FWCMD_START_MCC_NOTIFY_RXDBG_EN(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(21));
}

static inline void RTW89_SET_FWCMD_START_MCC_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 24));
}

static inline void RTW89_SET_FWCMD_START_MCC_TSF_LOW(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_START_MCC_TSF_HIGH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 2, val, GENMASK(31, 0));
}

static inline void RTW89_SET_FWCMD_STOP_MCC_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(7, 0));
}

static inline void RTW89_SET_FWCMD_STOP_MCC_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(9, 8));
}

static inline void RTW89_SET_FWCMD_STOP_MCC_PREV_GROUPS(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(10));
}

static inline void RTW89_SET_FWCMD_DEL_MCC_GROUP_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

static inline void RTW89_SET_FWCMD_DEL_MCC_GROUP_PREV_GROUPS(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(2));
}

static inline void RTW89_SET_FWCMD_RESET_MCC_GROUP_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

struct rtw89_fw_mcc_tsf_req {
	u8 group: 2;
	u8 rsvd0: 6;
	u8 macid_x;
	u8 macid_y;
	u8 rsvd1;
};

static inline void RTW89_SET_FWCMD_MCC_REQ_TSF_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

static inline void RTW89_SET_FWCMD_MCC_REQ_TSF_MACID_X(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_MCC_REQ_TSF_MACID_Y(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_MCC_MACID_BITMAP_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

static inline void RTW89_SET_FWCMD_MCC_MACID_BITMAP_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_MCC_MACID_BITMAP_BITMAP_LENGTH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_MCC_MACID_BITMAP_BITMAP(void *cmd,
							   u8 *bitmap, u8 len)
{
	memcpy((__le32 *)cmd + 1, bitmap, len);
}

static inline void RTW89_SET_FWCMD_MCC_SYNC_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

static inline void RTW89_SET_FWCMD_MCC_SYNC_MACID_SOURCE(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_MCC_SYNC_MACID_TARGET(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_MCC_SYNC_SYNC_OFFSET(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 24));
}

struct rtw89_fw_mcc_duration {
	u32 group: 2;
	u32 btc_in_group: 1;
	u32 rsvd0: 5;
	u32 start_macid: 8;
	u32 macid_x: 8;
	u32 macid_y: 8;
	u32 start_tsf_low;
	u32 start_tsf_high;
	u32 duration_x;
	u32 duration_y;
};

static inline void RTW89_SET_FWCMD_MCC_SET_DURATION_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(1, 0));
}

static
inline void RTW89_SET_FWCMD_MCC_SET_DURATION_BTC_IN_GROUP(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, BIT(2));
}

static
inline void RTW89_SET_FWCMD_MCC_SET_DURATION_START_MACID(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(15, 8));
}

static inline void RTW89_SET_FWCMD_MCC_SET_DURATION_MACID_X(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(23, 16));
}

static inline void RTW89_SET_FWCMD_MCC_SET_DURATION_MACID_Y(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd, val, GENMASK(31, 24));
}

static
inline void RTW89_SET_FWCMD_MCC_SET_DURATION_START_TSF_LOW(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 1, val, GENMASK(31, 0));
}

static
inline void RTW89_SET_FWCMD_MCC_SET_DURATION_START_TSF_HIGH(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 2, val, GENMASK(31, 0));
}

static
inline void RTW89_SET_FWCMD_MCC_SET_DURATION_DURATION_X(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 3, val, GENMASK(31, 0));
}

static
inline void RTW89_SET_FWCMD_MCC_SET_DURATION_DURATION_Y(void *cmd, u32 val)
{
	le32p_replace_bits((__le32 *)cmd + 4, val, GENMASK(31, 0));
}

#define RTW89_C2H_HEADER_LEN 8

#define RTW89_GET_C2H_CATEGORY(c2h) \
	le32_get_bits(*((const __le32 *)c2h), GENMASK(1, 0))
#define RTW89_GET_C2H_CLASS(c2h) \
	le32_get_bits(*((const __le32 *)c2h), GENMASK(7, 2))
#define RTW89_GET_C2H_FUNC(c2h) \
	le32_get_bits(*((const __le32 *)c2h), GENMASK(15, 8))
#define RTW89_GET_C2H_LEN(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 1), GENMASK(13, 0))

struct rtw89_fw_c2h_attr {
	u8 category;
	u8 class;
	u8 func;
	u16 len;
};

static inline struct rtw89_fw_c2h_attr *RTW89_SKB_C2H_CB(struct sk_buff *skb)
{
	static_assert(sizeof(skb->cb) >= sizeof(struct rtw89_fw_c2h_attr));

	return (struct rtw89_fw_c2h_attr *)skb->cb;
}

#define RTW89_GET_C2H_LOG_SRT_PRT(c2h) (char *)((__le32 *)(c2h) + 2)
#define RTW89_GET_C2H_LOG_LEN(len) ((len) - RTW89_C2H_HEADER_LEN)

struct rtw89_c2h_done_ack {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_C2H_DONE_ACK_W2_CAT GENMASK(1, 0)
#define RTW89_C2H_DONE_ACK_W2_CLASS GENMASK(7, 2)
#define RTW89_C2H_DONE_ACK_W2_FUNC GENMASK(15, 8)
#define RTW89_C2H_DONE_ACK_W2_H2C_RETURN GENMASK(23, 16)
#define RTW89_C2H_DONE_ACK_W2_H2C_SEQ GENMASK(31, 24)

#define RTW89_GET_MAC_C2H_REV_ACK_CAT(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(1, 0))
#define RTW89_GET_MAC_C2H_REV_ACK_CLASS(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 2))
#define RTW89_GET_MAC_C2H_REV_ACK_FUNC(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(15, 8))
#define RTW89_GET_MAC_C2H_REV_ACK_H2C_SEQ(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(23, 16))

struct rtw89_c2h_mac_bcnfltr_rpt {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_MACID GENMASK(7, 0)
#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_TYPE GENMASK(9, 8)
#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_EVENT GENMASK(11, 10)
#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_MA GENMASK(23, 16)

#define RTW89_GET_PHY_C2H_RA_RPT_MACID(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(15, 0))
#define RTW89_GET_PHY_C2H_RA_RPT_RETRY_RATIO(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(23, 16))
#define RTW89_GET_PHY_C2H_RA_RPT_MCSNSS(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 3), GENMASK(6, 0))
#define RTW89_GET_PHY_C2H_RA_RPT_MD_SEL(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 3), GENMASK(9, 8))
#define RTW89_GET_PHY_C2H_RA_RPT_GILTF(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 3), GENMASK(12, 10))
#define RTW89_GET_PHY_C2H_RA_RPT_BW(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 3), GENMASK(14, 13))

/* VHT, HE, HT-old: [6:4]: NSS, [3:0]: MCS
 * HT-new: [6:5]: NA, [4:0]: MCS
 */
#define RTW89_RA_RATE_MASK_NSS GENMASK(6, 4)
#define RTW89_RA_RATE_MASK_MCS GENMASK(3, 0)
#define RTW89_RA_RATE_MASK_HT_MCS GENMASK(4, 0)
#define RTW89_MK_HT_RATE(nss, mcs) (FIELD_PREP(GENMASK(4, 3), nss) | \
				    FIELD_PREP(GENMASK(2, 0), mcs))

#define RTW89_GET_MAC_C2H_PKTOFLD_ID(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 0))
#define RTW89_GET_MAC_C2H_PKTOFLD_OP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(10, 8))
#define RTW89_GET_MAC_C2H_PKTOFLD_LEN(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(31, 16))

#define RTW89_GET_MAC_C2H_SCANOFLD_PRI_CH(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 0))
#define RTW89_GET_MAC_C2H_SCANOFLD_RSP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(19, 16))
#define RTW89_GET_MAC_C2H_SCANOFLD_STATUS(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(23, 20))
#define RTW89_GET_MAC_C2H_ACTUAL_PERIOD(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(31, 24))
#define RTW89_GET_MAC_C2H_SCANOFLD_TX_FAIL(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 5), GENMASK(3, 0))
#define RTW89_GET_MAC_C2H_SCANOFLD_AIR_DENSITY(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 5), GENMASK(7, 4))
#define RTW89_GET_MAC_C2H_SCANOFLD_BAND(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 5), GENMASK(25, 24))

#define RTW89_GET_MAC_C2H_MCC_RCV_ACK_GROUP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(1, 0))
#define RTW89_GET_MAC_C2H_MCC_RCV_ACK_H2C_FUNC(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(15, 8))

#define RTW89_GET_MAC_C2H_MCC_REQ_ACK_GROUP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(1, 0))
#define RTW89_GET_MAC_C2H_MCC_REQ_ACK_H2C_RETURN(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 2))
#define RTW89_GET_MAC_C2H_MCC_REQ_ACK_H2C_FUNC(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(15, 8))

struct rtw89_mac_mcc_tsf_rpt {
	u32 macid_x;
	u32 macid_y;
	u32 tsf_x_low;
	u32 tsf_x_high;
	u32 tsf_y_low;
	u32 tsf_y_high;
};

static_assert(sizeof(struct rtw89_mac_mcc_tsf_rpt) <= RTW89_COMPLETION_BUF_SIZE);

#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_MACID_X(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 0))
#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_MACID_Y(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(15, 8))
#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_GROUP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(17, 16))
#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_LOW_X(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 3), GENMASK(31, 0))
#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_HIGH_X(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 4), GENMASK(31, 0))
#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_LOW_Y(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 5), GENMASK(31, 0))
#define RTW89_GET_MAC_C2H_MCC_TSF_RPT_TSF_HIGH_Y(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 6), GENMASK(31, 0))

#define RTW89_GET_MAC_C2H_MCC_STATUS_RPT_STATUS(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(5, 0))
#define RTW89_GET_MAC_C2H_MCC_STATUS_RPT_GROUP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 6))
#define RTW89_GET_MAC_C2H_MCC_STATUS_RPT_MACID(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(15, 8))
#define RTW89_GET_MAC_C2H_MCC_STATUS_RPT_TSF_LOW(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 3), GENMASK(31, 0))
#define RTW89_GET_MAC_C2H_MCC_STATUS_RPT_TSF_HIGH(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 4), GENMASK(31, 0))

struct rtw89_c2h_pkt_ofld_rsp {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_C2H_PKT_OFLD_RSP_W2_PTK_ID GENMASK(7, 0)
#define RTW89_C2H_PKT_OFLD_RSP_W2_PTK_OP GENMASK(10, 8)
#define RTW89_C2H_PKT_OFLD_RSP_W2_PTK_LEN GENMASK(31, 16)

struct rtw89_h2c_bcnfltr {
	__le32 w0;
} __packed;

#define RTW89_H2C_BCNFLTR_W0_MON_RSSI BIT(0)
#define RTW89_H2C_BCNFLTR_W0_MON_BCN BIT(1)
#define RTW89_H2C_BCNFLTR_W0_MON_EN BIT(2)
#define RTW89_H2C_BCNFLTR_W0_MODE GENMASK(4, 3)
#define RTW89_H2C_BCNFLTR_W0_BCN_LOSS_CNT GENMASK(11, 8)
#define RTW89_H2C_BCNFLTR_W0_RSSI_HYST GENMASK(15, 12)
#define RTW89_H2C_BCNFLTR_W0_RSSI_THRESHOLD GENMASK(23, 16)
#define RTW89_H2C_BCNFLTR_W0_MAC_ID GENMASK(31, 24)

struct rtw89_h2c_ofld_rssi {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_H2C_OFLD_RSSI_W0_MACID GENMASK(7, 0)
#define RTW89_H2C_OFLD_RSSI_W0_NUM GENMASK(15, 8)
#define RTW89_H2C_OFLD_RSSI_W1_VAL GENMASK(7, 0)

struct rtw89_h2c_ofld {
	__le32 w0;
} __packed;

#define RTW89_H2C_OFLD_W0_MAC_ID GENMASK(7, 0)
#define RTW89_H2C_OFLD_W0_TX_TP GENMASK(17, 8)
#define RTW89_H2C_OFLD_W0_RX_TP GENMASK(27, 18)

#define RTW89_MFW_SIG	0xFF

struct rtw89_mfw_info {
	u8 cv;
	u8 type; /* enum rtw89_fw_type */
	u8 mp;
	u8 rsvd;
	__le32 shift;
	__le32 size;
	u8 rsvd2[4];
} __packed;

struct rtw89_mfw_hdr {
	u8 sig;	/* RTW89_MFW_SIG */
	u8 fw_nr;
	u8 rsvd0[2];
	struct {
		u8 major;
		u8 minor;
		u8 sub;
		u8 idx;
	} ver;
	u8 rsvd1[8];
	struct rtw89_mfw_info info[];
} __packed;

struct fwcmd_hdr {
	__le32 hdr0;
	__le32 hdr1;
};

union rtw89_compat_fw_hdr {
	struct rtw89_mfw_hdr mfw_hdr;
	struct rtw89_fw_hdr fw_hdr;
};

static inline u32 rtw89_compat_fw_hdr_ver_code(const void *fw_buf)
{
	const union rtw89_compat_fw_hdr *compat = (typeof(compat))fw_buf;

	if (compat->mfw_hdr.sig == RTW89_MFW_SIG)
		return RTW89_MFW_HDR_VER_CODE(&compat->mfw_hdr);
	else
		return RTW89_FW_HDR_VER_CODE(&compat->fw_hdr);
}

static inline void rtw89_fw_get_filename(char *buf, size_t size,
					 const char *fw_basename, int fw_format)
{
	if (fw_format <= 0)
		snprintf(buf, size, "%s.bin", fw_basename);
	else
		snprintf(buf, size, "%s-%d.bin", fw_basename, fw_format);
}

#define RTW89_H2C_RF_PAGE_SIZE 500
#define RTW89_H2C_RF_PAGE_NUM 3
struct rtw89_fw_h2c_rf_reg_info {
	enum rtw89_rf_path rf_path;
	__le32 rtw89_phy_config_rf_h2c[RTW89_H2C_RF_PAGE_NUM][RTW89_H2C_RF_PAGE_SIZE];
	u16 curr_idx;
};

#define H2C_SEC_CAM_LEN			24

#define H2C_HEADER_LEN			8
#define H2C_HDR_CAT			GENMASK(1, 0)
#define H2C_HDR_CLASS			GENMASK(7, 2)
#define H2C_HDR_FUNC			GENMASK(15, 8)
#define H2C_HDR_DEL_TYPE		GENMASK(19, 16)
#define H2C_HDR_H2C_SEQ			GENMASK(31, 24)
#define H2C_HDR_TOTAL_LEN		GENMASK(13, 0)
#define H2C_HDR_REC_ACK			BIT(14)
#define H2C_HDR_DONE_ACK		BIT(15)

#define FWCMD_TYPE_H2C			0

#define H2C_CAT_TEST		0x0

/* CLASS 5 - FW STATUS TEST */
#define H2C_CL_FW_STATUS_TEST		0x5
#define H2C_FUNC_CPU_EXCEPTION		0x1

#define H2C_CAT_MAC		0x1

/* CLASS 0 - FW INFO */
#define H2C_CL_FW_INFO			0x0
#define H2C_FUNC_LOG_CFG		0x0
#define H2C_FUNC_MAC_GENERAL_PKT	0x1

/* CLASS 1 - WOW */
#define H2C_CL_MAC_WOW			0x1
#define H2C_FUNC_KEEP_ALIVE		0x0
#define H2C_FUNC_DISCONNECT_DETECT	0x1
#define H2C_FUNC_WOW_GLOBAL		0x2
#define H2C_FUNC_WAKEUP_CTRL		0x8
#define H2C_FUNC_WOW_CAM_UPD		0xC

/* CLASS 2 - PS */
#define H2C_CL_MAC_PS			0x2
#define H2C_FUNC_MAC_LPS_PARM		0x0
#define H2C_FUNC_P2P_ACT		0x1

/* CLASS 3 - FW download */
#define H2C_CL_MAC_FWDL		0x3
#define H2C_FUNC_MAC_FWHDR_DL		0x0

/* CLASS 5 - Frame Exchange */
#define H2C_CL_MAC_FR_EXCHG		0x5
#define H2C_FUNC_MAC_CCTLINFO_UD	0x2
#define H2C_FUNC_MAC_BCN_UPD		0x5
#define H2C_FUNC_MAC_DCTLINFO_UD_V1	0x9
#define H2C_FUNC_MAC_CCTLINFO_UD_V1	0xa

/* CLASS 6 - Address CAM */
#define H2C_CL_MAC_ADDR_CAM_UPDATE	0x6
#define H2C_FUNC_MAC_ADDR_CAM_UPD	0x0

/* CLASS 8 - Media Status Report */
#define H2C_CL_MAC_MEDIA_RPT		0x8
#define H2C_FUNC_MAC_JOININFO		0x0
#define H2C_FUNC_MAC_FWROLE_MAINTAIN	0x4

/* CLASS 9 - FW offload */
#define H2C_CL_MAC_FW_OFLD		0x9
enum rtw89_fw_ofld_h2c_func {
	H2C_FUNC_PACKET_OFLD		= 0x1,
	H2C_FUNC_MAC_MACID_PAUSE	= 0x8,
	H2C_FUNC_USR_EDCA		= 0xF,
	H2C_FUNC_TSF32_TOGL		= 0x10,
	H2C_FUNC_OFLD_CFG		= 0x14,
	H2C_FUNC_ADD_SCANOFLD_CH	= 0x16,
	H2C_FUNC_SCANOFLD		= 0x17,
	H2C_FUNC_PKT_DROP		= 0x1b,
	H2C_FUNC_CFG_BCNFLTR		= 0x1e,
	H2C_FUNC_OFLD_RSSI		= 0x1f,
	H2C_FUNC_OFLD_TP		= 0x20,

	NUM_OF_RTW89_FW_OFLD_H2C_FUNC,
};

#define RTW89_FW_OFLD_WAIT_COND(tag, func) \
	((tag) * NUM_OF_RTW89_FW_OFLD_H2C_FUNC + (func))

#define RTW89_FW_OFLD_WAIT_COND_PKT_OFLD(pkt_id, pkt_op) \
	RTW89_FW_OFLD_WAIT_COND(RTW89_PKT_OFLD_WAIT_TAG(pkt_id, pkt_op), \
				H2C_FUNC_PACKET_OFLD)

/* CLASS 10 - Security CAM */
#define H2C_CL_MAC_SEC_CAM		0xa
#define H2C_FUNC_MAC_SEC_UPD		0x1

/* CLASS 12 - BA CAM */
#define H2C_CL_BA_CAM			0xc
#define H2C_FUNC_MAC_BA_CAM		0x0

/* CLASS 14 - MCC */
#define H2C_CL_MCC			0xe
enum rtw89_mcc_h2c_func {
	H2C_FUNC_ADD_MCC		= 0x0,
	H2C_FUNC_START_MCC		= 0x1,
	H2C_FUNC_STOP_MCC		= 0x2,
	H2C_FUNC_DEL_MCC_GROUP		= 0x3,
	H2C_FUNC_RESET_MCC_GROUP	= 0x4,
	H2C_FUNC_MCC_REQ_TSF		= 0x5,
	H2C_FUNC_MCC_MACID_BITMAP	= 0x6,
	H2C_FUNC_MCC_SYNC		= 0x7,
	H2C_FUNC_MCC_SET_DURATION	= 0x8,

	NUM_OF_RTW89_MCC_H2C_FUNC,
};

#define RTW89_MCC_WAIT_COND(group, func) \
	((group) * NUM_OF_RTW89_MCC_H2C_FUNC + (func))

#define H2C_CAT_OUTSRC			0x2

#define H2C_CL_OUTSRC_RA		0x1
#define H2C_FUNC_OUTSRC_RA_MACIDCFG	0x0

#define H2C_CL_OUTSRC_RF_REG_A		0x8
#define H2C_CL_OUTSRC_RF_REG_B		0x9
#define H2C_CL_OUTSRC_RF_FW_NOTIFY	0xa
#define H2C_FUNC_OUTSRC_RF_GET_MCCCH	0x2

struct rtw89_fw_h2c_rf_get_mccch {
	__le32 ch_0;
	__le32 ch_1;
	__le32 band_0;
	__le32 band_1;
	__le32 current_channel;
	__le32 current_band_type;
} __packed;

#define RTW89_FW_RSVD_PLE_SIZE 0x800

#define RTW89_WCPU_BASE_MASK GENMASK(27, 0)

#define RTW89_FW_BACKTRACE_INFO_SIZE 8
#define RTW89_VALID_FW_BACKTRACE_SIZE(_size) \
	((_size) % RTW89_FW_BACKTRACE_INFO_SIZE == 0)

#define RTW89_FW_BACKTRACE_MAX_SIZE 512 /* 8 * 64 (entries) */
#define RTW89_FW_BACKTRACE_KEY 0xBACEBACE

int rtw89_fw_check_rdy(struct rtw89_dev *rtwdev);
int rtw89_fw_recognize(struct rtw89_dev *rtwdev);
const struct firmware *
rtw89_early_fw_feature_recognize(struct device *device,
				 const struct rtw89_chip_info *chip,
				 struct rtw89_fw_info *early_fw,
				 int *used_fw_format);
int rtw89_fw_download(struct rtw89_dev *rtwdev, enum rtw89_fw_type type);
void rtw89_load_firmware_work(struct work_struct *work);
void rtw89_unload_firmware(struct rtw89_dev *rtwdev);
int rtw89_wait_firmware_completion(struct rtw89_dev *rtwdev);
void rtw89_h2c_pkt_set_hdr(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			   u8 type, u8 cat, u8 class, u8 func,
			   bool rack, bool dack, u32 len);
int rtw89_fw_h2c_default_cmac_tbl(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *rtwvif);
int rtw89_fw_h2c_assoc_cmac_tbl(struct rtw89_dev *rtwdev,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta);
int rtw89_fw_h2c_txtime_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_txpath_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_update_beacon(struct rtw89_dev *rtwdev,
			       struct rtw89_vif *rtwvif);
int rtw89_fw_h2c_cam(struct rtw89_dev *rtwdev, struct rtw89_vif *vif,
		     struct rtw89_sta *rtwsta, const u8 *scan_mac_addr);
int rtw89_fw_h2c_dctl_sec_cam_v1(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif,
				 struct rtw89_sta *rtwsta);
void rtw89_fw_c2h_irqsafe(struct rtw89_dev *rtwdev, struct sk_buff *c2h);
void rtw89_fw_c2h_work(struct work_struct *work);
int rtw89_fw_h2c_role_maintain(struct rtw89_dev *rtwdev,
			       struct rtw89_vif *rtwvif,
			       struct rtw89_sta *rtwsta,
			       enum rtw89_upd_mode upd_mode);
int rtw89_fw_h2c_join_info(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			   struct rtw89_sta *rtwsta, bool dis_conn);
int rtw89_fw_h2c_macid_pause(struct rtw89_dev *rtwdev, u8 sh, u8 grp,
			     bool pause);
int rtw89_fw_h2c_set_edca(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			  u8 ac, u32 val);
int rtw89_fw_h2c_set_ofld_cfg(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_set_bcn_fltr_cfg(struct rtw89_dev *rtwdev,
				  struct ieee80211_vif *vif,
				  bool connect);
int rtw89_fw_h2c_rssi_offload(struct rtw89_dev *rtwdev,
			      struct rtw89_rx_phy_ppdu *phy_ppdu);
int rtw89_fw_h2c_tp_offload(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
int rtw89_fw_h2c_ra(struct rtw89_dev *rtwdev, struct rtw89_ra_info *ra, bool csi);
int rtw89_fw_h2c_cxdrv_init(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_cxdrv_role(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_cxdrv_role_v1(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_cxdrv_role_v2(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_cxdrv_ctrl(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_cxdrv_trx(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_cxdrv_rfk(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_del_pkt_offload(struct rtw89_dev *rtwdev, u8 id);
int rtw89_fw_h2c_add_pkt_offload(struct rtw89_dev *rtwdev, u8 *id,
				 struct sk_buff *skb_ofld);
int rtw89_fw_h2c_scan_list_offload(struct rtw89_dev *rtwdev, int len,
				   struct list_head *chan_list);
int rtw89_fw_h2c_scan_offload(struct rtw89_dev *rtwdev,
			      struct rtw89_scan_option *opt,
			      struct rtw89_vif *vif);
int rtw89_fw_h2c_rf_reg(struct rtw89_dev *rtwdev,
			struct rtw89_fw_h2c_rf_reg_info *info,
			u16 len, u8 page);
int rtw89_fw_h2c_rf_ntfy_mcc(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_raw_with_hdr(struct rtw89_dev *rtwdev,
			      u8 h2c_class, u8 h2c_func, u8 *buf, u16 len,
			      bool rack, bool dack);
int rtw89_fw_h2c_raw(struct rtw89_dev *rtwdev, const u8 *buf, u16 len);
void rtw89_fw_send_all_early_h2c(struct rtw89_dev *rtwdev);
void rtw89_fw_free_all_early_h2c(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_general_pkt(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			     u8 macid);
void rtw89_fw_release_general_pkt_list_vif(struct rtw89_dev *rtwdev,
					   struct rtw89_vif *rtwvif, bool notify_fw);
void rtw89_fw_release_general_pkt_list(struct rtw89_dev *rtwdev, bool notify_fw);
int rtw89_fw_h2c_ba_cam(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			bool valid, struct ieee80211_ampdu_params *params);
void rtw89_fw_h2c_init_dynamic_ba_cam_v0_ext(struct rtw89_dev *rtwdev);

int rtw89_fw_h2c_lps_parm(struct rtw89_dev *rtwdev,
			  struct rtw89_lps_parm *lps_param);
struct sk_buff *rtw89_fw_h2c_alloc_skb_with_hdr(struct rtw89_dev *rtwdev, u32 len);
struct sk_buff *rtw89_fw_h2c_alloc_skb_no_hdr(struct rtw89_dev *rtwdev, u32 len);
int rtw89_fw_msg_reg(struct rtw89_dev *rtwdev,
		     struct rtw89_mac_h2c_info *h2c_info,
		     struct rtw89_mac_c2h_info *c2h_info);
int rtw89_fw_h2c_fw_log(struct rtw89_dev *rtwdev, bool enable);
void rtw89_fw_st_dbg_dump(struct rtw89_dev *rtwdev);
void rtw89_hw_scan_start(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			 struct ieee80211_scan_request *req);
void rtw89_hw_scan_complete(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			    bool aborted);
int rtw89_hw_scan_offload(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			  bool enable);
void rtw89_hw_scan_abort(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif);
int rtw89_fw_h2c_trigger_cpu_exception(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_pkt_drop(struct rtw89_dev *rtwdev,
			  const struct rtw89_pkt_drop_params *params);
int rtw89_fw_h2c_p2p_act(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			 struct ieee80211_p2p_noa_desc *desc,
			 u8 act, u8 noa_id);
int rtw89_fw_h2c_tsf32_toggle(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			      bool en);
int rtw89_fw_h2c_wow_global(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			    bool enable);
int rtw89_fw_h2c_wow_wakeup_ctrl(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif, bool enable);
int rtw89_fw_h2c_keep_alive(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			    bool enable);
int rtw89_fw_h2c_disconnect_detect(struct rtw89_dev *rtwdev,
				   struct rtw89_vif *rtwvif, bool enable);
int rtw89_fw_h2c_wow_global(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			    bool enable);
int rtw89_fw_h2c_wow_wakeup_ctrl(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif, bool enable);
int rtw89_fw_wow_cam_update(struct rtw89_dev *rtwdev,
			    struct rtw89_wow_cam_info *cam_info);
int rtw89_fw_h2c_add_mcc(struct rtw89_dev *rtwdev,
			 const struct rtw89_fw_mcc_add_req *p);
int rtw89_fw_h2c_start_mcc(struct rtw89_dev *rtwdev,
			   const struct rtw89_fw_mcc_start_req *p);
int rtw89_fw_h2c_stop_mcc(struct rtw89_dev *rtwdev, u8 group, u8 macid,
			  bool prev_groups);
int rtw89_fw_h2c_del_mcc_group(struct rtw89_dev *rtwdev, u8 group,
			       bool prev_groups);
int rtw89_fw_h2c_reset_mcc_group(struct rtw89_dev *rtwdev, u8 group);
int rtw89_fw_h2c_mcc_req_tsf(struct rtw89_dev *rtwdev,
			     const struct rtw89_fw_mcc_tsf_req *req,
			     struct rtw89_mac_mcc_tsf_rpt *rpt);
int rtw89_fw_h2c_mcc_macid_bitamp(struct rtw89_dev *rtwdev, u8 group, u8 macid,
				  u8 *bitmap);
int rtw89_fw_h2c_mcc_sync(struct rtw89_dev *rtwdev, u8 group, u8 source,
			  u8 target, u8 offset);
int rtw89_fw_h2c_mcc_set_duration(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_mcc_duration *p);

static inline void rtw89_fw_h2c_init_ba_cam(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->bacam_ver == RTW89_BACAM_V0_EXT)
		rtw89_fw_h2c_init_dynamic_ba_cam_v0_ext(rtwdev);
}

#endif
