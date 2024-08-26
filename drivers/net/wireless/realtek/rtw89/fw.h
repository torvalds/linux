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

#define RTW89_C2HREG_AOAC_RPT_1_W0_KEY_IDX GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_1_W1_IV_0 GENMASK(7, 0)
#define RTW89_C2HREG_AOAC_RPT_1_W1_IV_1 GENMASK(15, 8)
#define RTW89_C2HREG_AOAC_RPT_1_W1_IV_2 GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_1_W1_IV_3 GENMASK(31, 24)
#define RTW89_C2HREG_AOAC_RPT_1_W2_IV_4 GENMASK(7, 0)
#define RTW89_C2HREG_AOAC_RPT_1_W2_IV_5 GENMASK(15, 8)
#define RTW89_C2HREG_AOAC_RPT_1_W2_IV_6 GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_1_W2_IV_7 GENMASK(31, 24)
#define RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_0 GENMASK(7, 0)
#define RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_1 GENMASK(15, 8)
#define RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_2 GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_1_W3_PTK_IV_3 GENMASK(31, 24)
#define RTW89_C2HREG_AOAC_RPT_2_W0_PTK_IV_4 GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_2_W0_PTK_IV_5 GENMASK(31, 24)
#define RTW89_C2HREG_AOAC_RPT_2_W1_PTK_IV_6 GENMASK(7, 0)
#define RTW89_C2HREG_AOAC_RPT_2_W1_PTK_IV_7 GENMASK(15, 8)
#define RTW89_C2HREG_AOAC_RPT_2_W1_IGTK_IPN_IV_0 GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_2_W1_IGTK_IPN_IV_1 GENMASK(31, 24)
#define RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_2 GENMASK(7, 0)
#define RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_3 GENMASK(15, 8)
#define RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_4 GENMASK(23, 16)
#define RTW89_C2HREG_AOAC_RPT_2_W2_IGTK_IPN_IV_5 GENMASK(31, 24)
#define RTW89_C2HREG_AOAC_RPT_2_W3_IGTK_IPN_IV_6 GENMASK(7, 0)
#define RTW89_C2HREG_AOAC_RPT_2_W3_IGTK_IPN_IV_7 GENMASK(15, 8)

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

#define RTW89_H2CREG_WOW_CPUIO_RX_CTRL_EN GENMASK(23, 16)

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
	RTW89_FWCMD_H2CREG_FUNC_SCH_TX_EN,
	RTW89_FWCMD_H2CREG_FUNC_WOW_TRX_STOP,
	RTW89_FWCMD_H2CREG_FUNC_AOAC_RPT_1,
	RTW89_FWCMD_H2CREG_FUNC_AOAC_RPT_2,
	RTW89_FWCMD_H2CREG_FUNC_AOAC_RPT_3_REQ,
	RTW89_FWCMD_H2CREG_FUNC_WOW_CPUIO_RX_CTRL,
};

enum rtw89_mac_c2h_type {
	RTW89_FWCMD_C2HREG_FUNC_C2HREG_LB = 0,
	RTW89_FWCMD_C2HREG_FUNC_ERR_RPT,
	RTW89_FWCMD_C2HREG_FUNC_ERR_MSG,
	RTW89_FWCMD_C2HREG_FUNC_PHY_CAP,
	RTW89_FWCMD_C2HREG_FUNC_TX_PAUSE_RPT,
	RTW89_FWCMD_C2HREG_FUNC_WOW_CPUIO_RX_ACK = 0xA,
	RTW89_FWCMD_C2HREG_FUNC_NULL = 0xFF,
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
	RTW89_FW_LOG_COMP_SCAN = 28,
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
	RTW89_SCAN_REPORT_NOTIFY,
	RTW89_SCAN_CHKPT_NOTIFY,
	RTW89_SCAN_ENTER_OP_NOTIFY,
	RTW89_SCAN_LEAVE_OP_NOTIFY,
};

enum rtw89_scanofld_status {
	RTW89_SCAN_STATUS_NOTIFY,
	RTW89_SCAN_STATUS_SUCCESS,
	RTW89_SCAN_STATUS_FAIL,
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

#define RTW89_DEFAULT_CQM_HYST 4
#define RTW89_DEFAULT_CQM_THOLD -70

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
	bool ignore;
	const u8 *key_addr;
	u32 key_len;
	u32 key_idx;
};

struct rtw89_fw_bin_info {
	u8 section_num;
	u32 hdr_len;
	bool dynamic_hdr_en;
	u32 dynamic_hdr_len;
	bool dsp_checksum;
	bool secure_section_exist;
	struct rtw89_fw_hdr_section_info section_info[FWDL_SECTION_MAX_NUM];
};

struct rtw89_fw_macid_pause_grp {
	__le32 pause_grp[4];
	__le32 mask_grp[4];
} __packed;

struct rtw89_fw_macid_pause_sleep_grp {
	struct {
		__le32 pause_grp[4];
		__le32 pause_mask_grp[4];
		__le32 sleep_grp[4];
		__le32 sleep_mask_grp[4];
	} __packed n[4];
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
#define RTW89_CHAN_INVALID 0xFF
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

struct rtw89_mac_chinfo_be {
	u8 period;
	u8 dwell_time;
	u8 central_ch;
	u8 pri_ch;
	u8 bw:3;
	u8 ch_band:2;
	u8 dfs_ch:1;
	u8 pause_data:1;
	u8 tx_null:1;
	u8 rand_seq_num:1;
	u8 notify_action:5;
	u8 probe_id;
	u8 leave_crit;
	u8 chkpt_timer;
	u8 leave_time;
	u8 leave_th;
	u16 tx_pkt_ctrl;
	u8 pkt_id[RTW89_SCANOFLD_MAX_SSID];
	u8 sw_def;
	u16 fw_probe0_ssids;
	u16 fw_probe0_shortssids;
	u16 fw_probe0_bssids;

	struct list_head list;
	bool is_psc;
};

struct rtw89_pktofld_info {
	struct list_head list;
	u8 id;
	bool wildcard_6ghz;

	/* Below fields are for WiFi 6 chips 6 GHz RNR use only */
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 ssid_len;
	u8 bssid[ETH_ALEN];
	u16 channel_6ghz;
	bool cancel;
};

struct rtw89_h2c_ra {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
} __packed;

#define RTW89_H2C_RA_W0_IS_DIS BIT(0)
#define RTW89_H2C_RA_W0_MODE GENMASK(5, 1)
#define RTW89_H2C_RA_W0_BW_CAP GENMASK(7, 6)
#define RTW89_H2C_RA_W0_MACID GENMASK(15, 8)
#define RTW89_H2C_RA_W0_DCM BIT(16)
#define RTW89_H2C_RA_W0_ER BIT(17)
#define RTW89_H2C_RA_W0_INIT_RATE_LV GENMASK(19, 18)
#define RTW89_H2C_RA_W0_UPD_ALL BIT(20)
#define RTW89_H2C_RA_W0_SGI BIT(21)
#define RTW89_H2C_RA_W0_LDPC BIT(22)
#define RTW89_H2C_RA_W0_STBC BIT(23)
#define RTW89_H2C_RA_W0_SS_NUM GENMASK(26, 24)
#define RTW89_H2C_RA_W0_GILTF GENMASK(29, 27)
#define RTW89_H2C_RA_W0_UPD_BW_NSS_MASK BIT(30)
#define RTW89_H2C_RA_W0_UPD_MASK BIT(31)
#define RTW89_H2C_RA_W1_RAMASK_LO32 GENMASK(31, 0)
#define RTW89_H2C_RA_W2_RAMASK_HI32 GENMASK(30, 0)
#define RTW89_H2C_RA_W2_BFEE_CSI_CTL BIT(31)
#define RTW89_H2C_RA_W3_BAND_NUM GENMASK(7, 0)
#define RTW89_H2C_RA_W3_RA_CSI_RATE_EN BIT(8)
#define RTW89_H2C_RA_W3_FIXED_CSI_RATE_EN BIT(9)
#define RTW89_H2C_RA_W3_CR_TBL_SEL BIT(10)
#define RTW89_H2C_RA_W3_FIX_GILTF_EN BIT(11)
#define RTW89_H2C_RA_W3_FIX_GILTF GENMASK(14, 12)
#define RTW89_H2C_RA_W3_FIXED_CSI_MCS_SS_IDX GENMASK(23, 16)
#define RTW89_H2C_RA_W3_FIXED_CSI_MODE GENMASK(25, 24)
#define RTW89_H2C_RA_W3_FIXED_CSI_GI_LTF GENMASK(28, 26)
#define RTW89_H2C_RA_W3_FIXED_CSI_BW GENMASK(31, 29)

struct rtw89_h2c_ra_v1 {
	struct rtw89_h2c_ra v0;
	__le32 w4;
	__le32 w5;
} __packed;

#define RTW89_H2C_RA_V1_W4_MODE_EHT GENMASK(6, 0)
#define RTW89_H2C_RA_V1_W4_BW_EHT GENMASK(10, 8)
#define RTW89_H2C_RA_V1_W4_RAMASK_UHL16 GENMASK(31, 16)
#define RTW89_H2C_RA_V1_W5_RAMASK_UHH16 GENMASK(15, 0)

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
#define FWDL_SECURITY_CHKSUM_LEN 8

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
#define FW_HDR_W2_COMMITID GENMASK(31, 0)
#define FW_HDR_W3_LEN GENMASK(23, 16)
#define FW_HDR_W3_HDR_VER GENMASK(31, 24)
#define FW_HDR_W4_MONTH GENMASK(7, 0)
#define FW_HDR_W4_DATE GENMASK(15, 8)
#define FW_HDR_W4_HOUR GENMASK(23, 16)
#define FW_HDR_W4_MIN GENMASK(31, 24)
#define FW_HDR_W5_YEAR GENMASK(31, 0)
#define FW_HDR_W6_SEC_NUM GENMASK(15, 8)
#define FW_HDR_W7_PART_SIZE GENMASK(15, 0)
#define FW_HDR_W7_DYN_HDR BIT(16)
#define FW_HDR_W7_CMD_VERSERION GENMASK(31, 24)

struct rtw89_fw_hdr_section_v1 {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
} __packed;

#define FWSECTION_HDR_V1_W0_DL_ADDR GENMASK(31, 0)
#define FWSECTION_HDR_V1_W1_METADATA GENMASK(31, 24)
#define FWSECTION_HDR_V1_W1_SECTIONTYPE GENMASK(27, 24)
#define FWSECTION_HDR_V1_W1_SEC_SIZE GENMASK(23, 0)
#define FWSECTION_HDR_V1_W1_CHECKSUM BIT(28)
#define FWSECTION_HDR_V1_W1_REDL BIT(29)
#define FWSECTION_HDR_V1_W2_MSSC GENMASK(7, 0)
#define FORMATTED_MSSC 0xFF
#define FWSECTION_HDR_V1_W2_BBMCU_IDX GENMASK(27, 24)

struct rtw89_fw_hdr_v1 {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	__le32 w9;
	__le32 w10;
	__le32 w11;
	struct rtw89_fw_hdr_section_v1 sections[];
} __packed;

#define FW_HDR_V1_W1_MAJOR_VERSION GENMASK(7, 0)
#define FW_HDR_V1_W1_MINOR_VERSION GENMASK(15, 8)
#define FW_HDR_V1_W1_SUBVERSION GENMASK(23, 16)
#define FW_HDR_V1_W1_SUBINDEX GENMASK(31, 24)
#define FW_HDR_V1_W2_COMMITID GENMASK(31, 0)
#define FW_HDR_V1_W3_CMD_VERSERION GENMASK(23, 16)
#define FW_HDR_V1_W3_HDR_VER GENMASK(31, 24)
#define FW_HDR_V1_W4_MONTH GENMASK(7, 0)
#define FW_HDR_V1_W4_DATE GENMASK(15, 8)
#define FW_HDR_V1_W4_HOUR GENMASK(23, 16)
#define FW_HDR_V1_W4_MIN GENMASK(31, 24)
#define FW_HDR_V1_W5_YEAR GENMASK(15, 0)
#define FW_HDR_V1_W5_HDR_SIZE GENMASK(31, 16)
#define FW_HDR_V1_W6_SEC_NUM GENMASK(15, 8)
#define FW_HDR_V1_W6_DSP_CHKSUM BIT(24)
#define FW_HDR_V1_W7_PART_SIZE GENMASK(15, 0)
#define FW_HDR_V1_W7_DYN_HDR BIT(16)

enum rtw89_fw_mss_pool_rmp_tbl_type {
	MSS_POOL_RMP_TBL_BITMASK = 0x0,
	MSS_POOL_RMP_TBL_RECORD = 0x1,
};

#define FWDL_MSS_POOL_DEFKEYSETS_SIZE 8

struct rtw89_fw_mss_pool_hdr {
	u8 signature[8]; /* equal to mss_signature[] */
	__le32 rmp_tbl_offset;
	__le32 key_raw_offset;
	u8 defen;
	u8 rsvd[3];
	u8 rmpfmt; /* enum rtw89_fw_mss_pool_rmp_tbl_type */
	u8 mssdev_max;
	__le16 keypair_num;
	__le16 msscust_max;
	__le16 msskey_num_max;
	__le32 rsvd3;
	u8 rmp_tbl[];
} __packed;

union rtw89_fw_section_mssc_content {
	struct {
		u8 pad[58];
		__le32 v;
	} __packed sb_sel_ver;
	struct {
		u8 pad[60];
		__le16 v;
	} __packed key_sign_len;
} __packed;

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

struct rtw89_h2c_cctlinfo_ud_g7 {
	__le32 c0;
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	__le32 w9;
	__le32 w10;
	__le32 w11;
	__le32 w12;
	__le32 w13;
	__le32 w14;
	__le32 w15;
	__le32 m0;
	__le32 m1;
	__le32 m2;
	__le32 m3;
	__le32 m4;
	__le32 m5;
	__le32 m6;
	__le32 m7;
	__le32 m8;
	__le32 m9;
	__le32 m10;
	__le32 m11;
	__le32 m12;
	__le32 m13;
	__le32 m14;
	__le32 m15;
} __packed;

#define CCTLINFO_G7_C0_MACID GENMASK(6, 0)
#define CCTLINFO_G7_C0_OP BIT(7)

#define CCTLINFO_G7_W0_DATARATE GENMASK(11, 0)
#define CCTLINFO_G7_W0_DATA_GI_LTF GENMASK(14, 12)
#define CCTLINFO_G7_W0_TRYRATE BIT(15)
#define CCTLINFO_G7_W0_ARFR_CTRL GENMASK(17, 16)
#define CCTLINFO_G7_W0_DIS_HE1SS_STBC BIT(18)
#define CCTLINFO_G7_W0_ACQ_RPT_EN BIT(20)
#define CCTLINFO_G7_W0_MGQ_RPT_EN BIT(21)
#define CCTLINFO_G7_W0_ULQ_RPT_EN BIT(22)
#define CCTLINFO_G7_W0_TWTQ_RPT_EN BIT(23)
#define CCTLINFO_G7_W0_FORCE_TXOP BIT(24)
#define CCTLINFO_G7_W0_DISRTSFB BIT(25)
#define CCTLINFO_G7_W0_DISDATAFB BIT(26)
#define CCTLINFO_G7_W0_NSTR_EN BIT(27)
#define CCTLINFO_G7_W0_AMPDU_DENSITY GENMASK(31, 28)
#define CCTLINFO_G7_W0_ALL (GENMASK(31, 20) | GENMASK(18, 0))
#define CCTLINFO_G7_W1_DATA_RTY_LOWEST_RATE GENMASK(11, 0)
#define CCTLINFO_G7_W1_RTS_TXCNT_LMT GENMASK(15, 12)
#define CCTLINFO_G7_W1_RTSRATE GENMASK(27, 16)
#define CCTLINFO_G7_W1_RTS_RTY_LOWEST_RATE GENMASK(31, 28)
#define CCTLINFO_G7_W1_ALL GENMASK(31, 0)
#define CCTLINFO_G7_W2_DATA_TX_CNT_LMT GENMASK(5, 0)
#define CCTLINFO_G7_W2_DATA_TXCNT_LMT_SEL BIT(6)
#define CCTLINFO_G7_W2_MAX_AGG_NUM_SEL BIT(7)
#define CCTLINFO_G7_W2_RTS_EN BIT(8)
#define CCTLINFO_G7_W2_CTS2SELF_EN BIT(9)
#define CCTLINFO_G7_W2_CCA_RTS GENMASK(11, 10)
#define CCTLINFO_G7_W2_HW_RTS_EN BIT(12)
#define CCTLINFO_G7_W2_RTS_DROP_DATA_MODE GENMASK(14, 13)
#define CCTLINFO_G7_W2_PRELD_EN BIT(15)
#define CCTLINFO_G7_W2_AMPDU_MAX_LEN GENMASK(26, 16)
#define CCTLINFO_G7_W2_UL_MU_DIS BIT(27)
#define CCTLINFO_G7_W2_AMPDU_MAX_TIME GENMASK(31, 28)
#define CCTLINFO_G7_W2_ALL GENMASK(31, 0)
#define CCTLINFO_G7_W3_MAX_AGG_NUM GENMASK(7, 0)
#define CCTLINFO_G7_W3_DATA_BW GENMASK(10, 8)
#define CCTLINFO_G7_W3_DATA_BW_ER BIT(11)
#define CCTLINFO_G7_W3_BA_BMAP GENMASK(14, 12)
#define CCTLINFO_G7_W3_VCS_STBC BIT(15)
#define CCTLINFO_G7_W3_VO_LFTIME_SEL GENMASK(18, 16)
#define CCTLINFO_G7_W3_VI_LFTIME_SEL GENMASK(21, 19)
#define CCTLINFO_G7_W3_BE_LFTIME_SEL GENMASK(24, 22)
#define CCTLINFO_G7_W3_BK_LFTIME_SEL GENMASK(27, 25)
#define CCTLINFO_G7_W3_AMPDU_TIME_SEL BIT(28)
#define CCTLINFO_G7_W3_AMPDU_LEN_SEL BIT(29)
#define CCTLINFO_G7_W3_RTS_TXCNT_LMT_SEL BIT(30)
#define CCTLINFO_G7_W3_LSIG_TXOP_EN BIT(31)
#define CCTLINFO_G7_W3_ALL GENMASK(31, 0)
#define CCTLINFO_G7_W4_MULTI_PORT_ID GENMASK(2, 0)
#define CCTLINFO_G7_W4_BYPASS_PUNC BIT(3)
#define CCTLINFO_G7_W4_MBSSID GENMASK(7, 4)
#define CCTLINFO_G7_W4_DATA_DCM BIT(8)
#define CCTLINFO_G7_W4_DATA_ER BIT(9)
#define CCTLINFO_G7_W4_DATA_LDPC BIT(10)
#define CCTLINFO_G7_W4_DATA_STBC BIT(11)
#define CCTLINFO_G7_W4_A_CTRL_BQR BIT(12)
#define CCTLINFO_G7_W4_A_CTRL_BSR BIT(14)
#define CCTLINFO_G7_W4_A_CTRL_CAS BIT(15)
#define CCTLINFO_G7_W4_ACT_SUBCH_CBW GENMASK(31, 16)
#define CCTLINFO_G7_W4_ALL (GENMASK(31, 14) | GENMASK(12, 0))
#define CCTLINFO_G7_W5_NOMINAL_PKT_PADDING0 GENMASK(1, 0)
#define CCTLINFO_G7_W5_NOMINAL_PKT_PADDING1 GENMASK(3, 2)
#define CCTLINFO_G7_W5_NOMINAL_PKT_PADDING2 GENMASK(5, 4)
#define CCTLINFO_G7_W5_NOMINAL_PKT_PADDING3 GENMASK(7, 6)
#define CCTLINFO_G7_W5_NOMINAL_PKT_PADDING4 GENMASK(9, 8)
#define CCTLINFO_G7_W5_SR_RATE GENMASK(14, 10)
#define CCTLINFO_G7_W5_TID_DISABLE GENMASK(23, 16)
#define CCTLINFO_G7_W5_ADDR_CAM_INDEX GENMASK(31, 24)
#define CCTLINFO_G7_W5_ALL (GENMASK(31, 16) | GENMASK(14, 0))
#define CCTLINFO_G7_W6_AID12_PAID GENMASK(11, 0)
#define CCTLINFO_G7_W6_RESP_REF_RATE GENMASK(23, 12)
#define CCTLINFO_G7_W6_ULDL BIT(31)
#define CCTLINFO_G7_W6_ALL (BIT(31) | GENMASK(23, 0))
#define CCTLINFO_G7_W7_NC GENMASK(2, 0)
#define CCTLINFO_G7_W7_NR GENMASK(5, 3)
#define CCTLINFO_G7_W7_NG GENMASK(7, 6)
#define CCTLINFO_G7_W7_CB GENMASK(9, 8)
#define CCTLINFO_G7_W7_CS GENMASK(11, 10)
#define CCTLINFO_G7_W7_CSI_STBC_EN BIT(13)
#define CCTLINFO_G7_W7_CSI_LDPC_EN BIT(14)
#define CCTLINFO_G7_W7_CSI_PARA_EN BIT(15)
#define CCTLINFO_G7_W7_CSI_FIX_RATE GENMASK(27, 16)
#define CCTLINFO_G7_W7_CSI_BW GENMASK(31, 29)
#define CCTLINFO_G7_W7_ALL (GENMASK(31, 29) | GENMASK(27, 13) | GENMASK(11, 0))
#define CCTLINFO_G7_W8_ALL_ACK_SUPPORT BIT(0)
#define CCTLINFO_G7_W8_BSR_QUEUE_SIZE_FORMAT BIT(1)
#define CCTLINFO_G7_W8_BSR_OM_UPD_EN BIT(2)
#define CCTLINFO_G7_W8_MACID_FWD_IDC BIT(3)
#define CCTLINFO_G7_W8_AZ_SEC_EN BIT(4)
#define CCTLINFO_G7_W8_CSI_SEC_EN BIT(5)
#define CCTLINFO_G7_W8_FIX_UL_ADDRCAM_IDX BIT(6)
#define CCTLINFO_G7_W8_CTRL_CNT_VLD BIT(7)
#define CCTLINFO_G7_W8_CTRL_CNT GENMASK(11, 8)
#define CCTLINFO_G7_W8_RESP_SEC_TYPE GENMASK(15, 12)
#define CCTLINFO_G7_W8_ALL GENMASK(15, 0)
/* W9~13 are reserved */
#define CCTLINFO_G7_W14_VO_CURR_RATE GENMASK(11, 0)
#define CCTLINFO_G7_W14_VI_CURR_RATE GENMASK(23, 12)
#define CCTLINFO_G7_W14_BE_CURR_RATE_L GENMASK(31, 24)
#define CCTLINFO_G7_W14_ALL GENMASK(31, 0)
#define CCTLINFO_G7_W15_BE_CURR_RATE_H GENMASK(3, 0)
#define CCTLINFO_G7_W15_BK_CURR_RATE GENMASK(15, 4)
#define CCTLINFO_G7_W15_MGNT_CURR_RATE GENMASK(27, 16)
#define CCTLINFO_G7_W15_ALL GENMASK(27, 0)

struct rtw89_h2c_bcn_upd {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_H2C_BCN_UPD_W0_PORT GENMASK(7, 0)
#define RTW89_H2C_BCN_UPD_W0_MBSSID GENMASK(15, 8)
#define RTW89_H2C_BCN_UPD_W0_BAND GENMASK(23, 16)
#define RTW89_H2C_BCN_UPD_W0_GRP_IE_OFST GENMASK(31, 24)
#define RTW89_H2C_BCN_UPD_W1_MACID GENMASK(7, 0)
#define RTW89_H2C_BCN_UPD_W1_SSN_SEL GENMASK(9, 8)
#define RTW89_H2C_BCN_UPD_W1_SSN_MODE GENMASK(11, 10)
#define RTW89_H2C_BCN_UPD_W1_RATE GENMASK(20, 12)
#define RTW89_H2C_BCN_UPD_W1_TXPWR GENMASK(23, 21)
#define RTW89_H2C_BCN_UPD_W2_TXINFO_CTRL_EN BIT(0)
#define RTW89_H2C_BCN_UPD_W2_NTX_PATH_EN GENMASK(4, 1)
#define RTW89_H2C_BCN_UPD_W2_PATH_MAP_A GENMASK(6, 5)
#define RTW89_H2C_BCN_UPD_W2_PATH_MAP_B GENMASK(8, 7)
#define RTW89_H2C_BCN_UPD_W2_PATH_MAP_C GENMASK(10, 9)
#define RTW89_H2C_BCN_UPD_W2_PATH_MAP_D GENMASK(12, 11)
#define RTW89_H2C_BCN_UPD_W2_PATH_ANTSEL_A BIT(13)
#define RTW89_H2C_BCN_UPD_W2_PATH_ANTSEL_B BIT(14)
#define RTW89_H2C_BCN_UPD_W2_PATH_ANTSEL_C BIT(15)
#define RTW89_H2C_BCN_UPD_W2_PATH_ANTSEL_D BIT(16)
#define RTW89_H2C_BCN_UPD_W2_CSA_OFST GENMASK(31, 17)

struct rtw89_h2c_bcn_upd_be {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	__le32 w9;
	__le32 w10;
	__le32 w11;
	__le32 w12;
	__le32 w13;
	__le32 w14;
	__le32 w15;
	__le32 w16;
	__le32 w17;
	__le32 w18;
	__le32 w19;
	__le32 w20;
	__le32 w21;
	__le32 w22;
	__le32 w23;
	__le32 w24;
	__le32 w25;
	__le32 w26;
	__le32 w27;
	__le32 w28;
	__le32 w29;
} __packed;

#define RTW89_H2C_BCN_UPD_BE_W0_PORT GENMASK(7, 0)
#define RTW89_H2C_BCN_UPD_BE_W0_MBSSID GENMASK(15, 8)
#define RTW89_H2C_BCN_UPD_BE_W0_BAND GENMASK(23, 16)
#define RTW89_H2C_BCN_UPD_BE_W0_GRP_IE_OFST GENMASK(31, 24)
#define RTW89_H2C_BCN_UPD_BE_W1_MACID GENMASK(7, 0)
#define RTW89_H2C_BCN_UPD_BE_W1_SSN_SEL GENMASK(9, 8)
#define RTW89_H2C_BCN_UPD_BE_W1_SSN_MODE GENMASK(11, 10)
#define RTW89_H2C_BCN_UPD_BE_W1_RATE GENMASK(20, 12)
#define RTW89_H2C_BCN_UPD_BE_W1_TXPWR GENMASK(23, 21)
#define RTW89_H2C_BCN_UPD_BE_W1_MACID_EXT GENMASK(31, 24)
#define RTW89_H2C_BCN_UPD_BE_W2_TXINFO_CTRL_EN BIT(0)
#define RTW89_H2C_BCN_UPD_BE_W2_NTX_PATH_EN GENMASK(4, 1)
#define RTW89_H2C_BCN_UPD_BE_W2_PATH_MAP_A GENMASK(6, 5)
#define RTW89_H2C_BCN_UPD_BE_W2_PATH_MAP_B GENMASK(8, 7)
#define RTW89_H2C_BCN_UPD_BE_W2_PATH_MAP_C GENMASK(10, 9)
#define RTW89_H2C_BCN_UPD_BE_W2_PATH_MAP_D GENMASK(12, 11)
#define RTW89_H2C_BCN_UPD_BE_W2_ANTSEL_A BIT(13)
#define RTW89_H2C_BCN_UPD_BE_W2_ANTSEL_B BIT(14)
#define RTW89_H2C_BCN_UPD_BE_W2_ANTSEL_C BIT(15)
#define RTW89_H2C_BCN_UPD_BE_W2_ANTSEL_D BIT(16)
#define RTW89_H2C_BCN_UPD_BE_W2_CSA_OFST GENMASK(31, 17)
#define RTW89_H2C_BCN_UPD_BE_W3_MLIE_CSA_OFST GENMASK(15, 0)
#define RTW89_H2C_BCN_UPD_BE_W3_CRITICAL_UPD_FLAG_OFST GENMASK(31, 16)
#define RTW89_H2C_BCN_UPD_BE_W4_VAP1_DTIM_CNT_OFST GENMASK(15, 0)
#define RTW89_H2C_BCN_UPD_BE_W4_VAP2_DTIM_CNT_OFST GENMASK(31, 16)
#define RTW89_H2C_BCN_UPD_BE_W5_VAP3_DTIM_CNT_OFST GENMASK(15, 0)
#define RTW89_H2C_BCN_UPD_BE_W5_VAP4_DTIM_CNT_OFST GENMASK(31, 16)
#define RTW89_H2C_BCN_UPD_BE_W6_VAP5_DTIM_CNT_OFST GENMASK(15, 0)
#define RTW89_H2C_BCN_UPD_BE_W6_VAP6_DTIM_CNT_OFST GENMASK(31, 16)
#define RTW89_H2C_BCN_UPD_BE_W7_VAP7_DTIM_CNT_OFST GENMASK(15, 0)
#define RTW89_H2C_BCN_UPD_BE_W7_ECSA_OFST GENMASK(30, 16)
#define RTW89_H2C_BCN_UPD_BE_W7_PROTECTION_KEY_ID BIT(31)

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

enum rtw89_fw_sta_type { /* value of RTW89_H2C_JOININFO_W1_STA_TYPE */
	RTW89_FW_N_AC_STA = 0,
	RTW89_FW_AX_STA = 1,
	RTW89_FW_BE_STA = 2,
};

struct rtw89_h2c_join {
	__le32 w0;
} __packed;

struct rtw89_h2c_join_v1 {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_H2C_JOININFO_W0_MACID GENMASK(7, 0)
#define RTW89_H2C_JOININFO_W0_OP BIT(8)
#define RTW89_H2C_JOININFO_W0_BAND BIT(9)
#define RTW89_H2C_JOININFO_W0_WMM GENMASK(11, 10)
#define RTW89_H2C_JOININFO_W0_TGR BIT(12)
#define RTW89_H2C_JOININFO_W0_ISHESTA BIT(13)
#define RTW89_H2C_JOININFO_W0_DLBW GENMASK(15, 14)
#define RTW89_H2C_JOININFO_W0_TF_MAC_PAD GENMASK(17, 16)
#define RTW89_H2C_JOININFO_W0_DL_T_PE GENMASK(20, 18)
#define RTW89_H2C_JOININFO_W0_PORT_ID GENMASK(23, 21)
#define RTW89_H2C_JOININFO_W0_NET_TYPE GENMASK(25, 24)
#define RTW89_H2C_JOININFO_W0_WIFI_ROLE GENMASK(29, 26)
#define RTW89_H2C_JOININFO_W0_SELF_ROLE GENMASK(31, 30)
#define RTW89_H2C_JOININFO_W1_STA_TYPE GENMASK(2, 0)
#define RTW89_H2C_JOININFO_W1_IS_MLD BIT(3)
#define RTW89_H2C_JOININFO_W1_MAIN_MACID GENMASK(11, 4)
#define RTW89_H2C_JOININFO_W1_MLO_MODE BIT(12)
#define RTW89_H2C_JOININFO_W1_EMLSR_CAB BIT(13)
#define RTW89_H2C_JOININFO_W1_NSTR_EN BIT(14)
#define RTW89_H2C_JOININFO_W1_INIT_PWR_STATE BIT(15)
#define RTW89_H2C_JOININFO_W1_EMLSR_PADDING GENMASK(18, 16)
#define RTW89_H2C_JOININFO_W1_EMLSR_TRANS_DELAY GENMASK(21, 19)
#define RTW89_H2C_JOININFO_W2_MACID_EXT GENMASK(7, 0)
#define RTW89_H2C_JOININFO_W2_MAIN_MACID_EXT GENMASK(15, 8)

struct rtw89_h2c_notify_dbcc {
	__le32 w0;
} __packed;

#define RTW89_H2C_NOTIFY_DBCC_EN BIT(0)

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

struct rtw89_h2c_ba_cam {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_H2C_BA_CAM_W0_VALID BIT(0)
#define RTW89_H2C_BA_CAM_W0_INIT_REQ BIT(1)
#define RTW89_H2C_BA_CAM_W0_ENTRY_IDX GENMASK(3, 2)
#define RTW89_H2C_BA_CAM_W0_TID GENMASK(7, 4)
#define RTW89_H2C_BA_CAM_W0_MACID GENMASK(15, 8)
#define RTW89_H2C_BA_CAM_W0_BMAP_SIZE GENMASK(19, 16)
#define RTW89_H2C_BA_CAM_W0_SSN GENMASK(31, 20)
#define RTW89_H2C_BA_CAM_W1_UID GENMASK(7, 0)
#define RTW89_H2C_BA_CAM_W1_STD_EN BIT(8)
#define RTW89_H2C_BA_CAM_W1_BAND BIT(9)
#define RTW89_H2C_BA_CAM_W1_ENTRY_IDX_V1 GENMASK(31, 28)

struct rtw89_h2c_ba_cam_v1 {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_H2C_BA_CAM_V1_W0_VALID BIT(0)
#define RTW89_H2C_BA_CAM_V1_W0_INIT_REQ BIT(1)
#define RTW89_H2C_BA_CAM_V1_W0_TID_MASK GENMASK(7, 4)
#define RTW89_H2C_BA_CAM_V1_W0_MACID_MASK GENMASK(15, 8)
#define RTW89_H2C_BA_CAM_V1_W0_BMAP_SIZE_MASK GENMASK(19, 16)
#define RTW89_H2C_BA_CAM_V1_W0_SSN_MASK GENMASK(31, 20)
#define RTW89_H2C_BA_CAM_V1_W1_UID_VALUE_MASK GENMASK(7, 0)
#define RTW89_H2C_BA_CAM_V1_W1_STD_ENTRY_EN BIT(8)
#define RTW89_H2C_BA_CAM_V1_W1_BAND_SEL BIT(9)
#define RTW89_H2C_BA_CAM_V1_W1_MLD_EN BIT(10)
#define RTW89_H2C_BA_CAM_V1_W1_ENTRY_IDX_MASK GENMASK(31, 24)

struct rtw89_h2c_ba_cam_init {
	__le32 w0;
} __packed;

#define RTW89_H2C_BA_CAM_INIT_USERS_MASK GENMASK(7, 0)
#define RTW89_H2C_BA_CAM_INIT_OFFSET_MASK GENMASK(19, 12)
#define RTW89_H2C_BA_CAM_INIT_BAND_SEL BIT(24)

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

struct rtw89_h2c_lps_ch_info {
	struct {
		u8 pri_ch;
		u8 central_ch;
		u8 bw;
		u8 band;
	} __packed info[2];

	__le32 mlo_dbcc_mode_lps;
} __packed;

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

struct rtw89_h2c_wow_global {
	__le32 w0;
	struct rtw89_wow_key_info key_info;
} __packed;

#define RTW89_H2C_WOW_GLOBAL_W0_ENABLE BIT(0)
#define RTW89_H2C_WOW_GLOBAL_W0_DROP_ALL_PKT BIT(1)
#define RTW89_H2C_WOW_GLOBAL_W0_RX_PARSE_AFTER_WAKE BIT(2)
#define RTW89_H2C_WOW_GLOBAL_W0_WAKE_BAR_PULLED BIT(3)
#define RTW89_H2C_WOW_GLOBAL_W0_MAC_ID GENMASK(15, 8)
#define RTW89_H2C_WOW_GLOBAL_W0_PAIRWISE_SEC_ALGO GENMASK(23, 16)
#define RTW89_H2C_WOW_GLOBAL_W0_GROUP_SEC_ALGO GENMASK(31, 24)

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

struct rtw89_h2c_wow_gtk_ofld {
	__le32 w0;
	__le32 w1;
	struct rtw89_wow_gtk_info gtk_info;
} __packed;

#define RTW89_H2C_WOW_GTK_OFLD_W0_EN BIT(0)
#define RTW89_H2C_WOW_GTK_OFLD_W0_TKIP_EN BIT(1)
#define RTW89_H2C_WOW_GTK_OFLD_W0_IEEE80211W_EN BIT(2)
#define RTW89_H2C_WOW_GTK_OFLD_W0_PAIRWISE_WAKEUP BIT(3)
#define RTW89_H2C_WOW_GTK_OFLD_W0_NOREKEY_WAKEUP BIT(4)
#define RTW89_H2C_WOW_GTK_OFLD_W0_MAC_ID GENMASK(23, 16)
#define RTW89_H2C_WOW_GTK_OFLD_W0_GTK_RSP_ID GENMASK(31, 24)
#define RTW89_H2C_WOW_GTK_OFLD_W1_PMF_SA_QUERY_ID GENMASK(7, 0)
#define RTW89_H2C_WOW_GTK_OFLD_W1_PMF_BIP_SEC_ALGO GENMASK(9, 8)
#define RTW89_H2C_WOW_GTK_OFLD_W1_ALGO_AKM_SUIT GENMASK(17, 10)

struct rtw89_h2c_arp_offload {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_H2C_ARP_OFFLOAD_W0_ENABLE BIT(0)
#define RTW89_H2C_ARP_OFFLOAD_W0_ACTION BIT(1)
#define RTW89_H2C_ARP_OFFLOAD_W0_MACID GENMASK(23, 16)
#define RTW89_H2C_ARP_OFFLOAD_W0_PKT_ID GENMASK(31, 24)
#define RTW89_H2C_ARP_OFFLOAD_W1_CONTENT GENMASK(31, 0)

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
	SET_BT_QUERY_DEV_LIST,
	SET_BT_QUERY_DEV_INFO,
	SET_BT_PSD_REPORT,
	SET_H2C_TEST,
	SET_IOFLD_RF,
	SET_IOFLD_BB,
	SET_IOFLD_MAC,
	SET_IOFLD_SCBD,
	SET_H2C_MACRO,
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
	CXDRVINFO_TXPWR,
	CXDRVINFO_FDDT,
	CXDRVINFO_MLO,
	CXDRVINFO_OSI,
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

struct rtw89_h2c_cxhdr_v7 {
	u8 type;
	u8 ver;
	u8 len;
} __packed;

struct rtw89_h2c_cxctrl_v7 {
	struct rtw89_h2c_cxhdr_v7 hdr;
	struct rtw89_btc_ctrl_v7 ctrl;
} __packed;

#define H2C_LEN_CXDRVHDR sizeof(struct rtw89_h2c_cxhdr)
#define H2C_LEN_CXDRVHDR_V7 sizeof(struct rtw89_h2c_cxhdr_v7)

struct rtw89_btc_wl_role_info_v8_u8 {
	u8 connect_cnt;
	u8 link_mode;
	u8 link_mode_chg;
	u8 p2p_2g;

	u8 pta_req_band;
	u8 dbcc_en;
	u8 dbcc_chg;
	u8 dbcc_2g_phy;

	struct rtw89_btc_wl_rlink rlink[RTW89_BE_BTC_WL_MAX_ROLE_NUMBER][RTW89_MAC_NUM];
} __packed;

struct rtw89_btc_wl_role_info_v8_u32 {
	__le32 role_map;
	__le32 mrole_type;
	__le32 mrole_noa_duration;
} __packed;

struct rtw89_h2c_cxrole_v8 {
	struct rtw89_h2c_cxhdr hdr;
	struct rtw89_btc_wl_role_info_v8_u8 _u8;
	struct rtw89_btc_wl_role_info_v8_u32 _u32;
} __packed;

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

struct rtw89_h2c_cxinit_v7 {
	struct rtw89_h2c_cxhdr_v7 hdr;
	struct rtw89_btc_init_info_v7 init;
} __packed;

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

struct rtw89_h2c_chinfo_elem {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
} __packed;

#define RTW89_H2C_CHINFO_W0_PERIOD GENMASK(7, 0)
#define RTW89_H2C_CHINFO_W0_DWELL GENMASK(15, 8)
#define RTW89_H2C_CHINFO_W0_CENTER_CH GENMASK(23, 16)
#define RTW89_H2C_CHINFO_W0_PRI_CH GENMASK(31, 24)
#define RTW89_H2C_CHINFO_W1_BW GENMASK(2, 0)
#define RTW89_H2C_CHINFO_W1_ACTION GENMASK(7, 3)
#define RTW89_H2C_CHINFO_W1_NUM_PKT GENMASK(11, 8)
#define RTW89_H2C_CHINFO_W1_TX BIT(12)
#define RTW89_H2C_CHINFO_W1_PAUSE_DATA BIT(13)
#define RTW89_H2C_CHINFO_W1_BAND GENMASK(15, 14)
#define RTW89_H2C_CHINFO_W1_PKT_ID GENMASK(23, 16)
#define RTW89_H2C_CHINFO_W1_DFS BIT(24)
#define RTW89_H2C_CHINFO_W1_TX_NULL BIT(25)
#define RTW89_H2C_CHINFO_W1_RANDOM BIT(26)
#define RTW89_H2C_CHINFO_W1_CFG_TX BIT(27)
#define RTW89_H2C_CHINFO_W2_PKT0 GENMASK(7, 0)
#define RTW89_H2C_CHINFO_W2_PKT1 GENMASK(15, 8)
#define RTW89_H2C_CHINFO_W2_PKT2 GENMASK(23, 16)
#define RTW89_H2C_CHINFO_W2_PKT3 GENMASK(31, 24)
#define RTW89_H2C_CHINFO_W3_PKT4 GENMASK(7, 0)
#define RTW89_H2C_CHINFO_W3_PKT5 GENMASK(15, 8)
#define RTW89_H2C_CHINFO_W3_PKT6 GENMASK(23, 16)
#define RTW89_H2C_CHINFO_W3_PKT7 GENMASK(31, 24)
#define RTW89_H2C_CHINFO_W4_POWER_IDX GENMASK(15, 0)

struct rtw89_h2c_chinfo_elem_be {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
} __packed;

#define RTW89_H2C_CHINFO_BE_W0_PERIOD GENMASK(7, 0)
#define RTW89_H2C_CHINFO_BE_W0_DWELL GENMASK(15, 8)
#define RTW89_H2C_CHINFO_BE_W0_CENTER_CH GENMASK(23, 16)
#define RTW89_H2C_CHINFO_BE_W0_PRI_CH GENMASK(31, 24)
#define RTW89_H2C_CHINFO_BE_W1_BW GENMASK(2, 0)
#define RTW89_H2C_CHINFO_BE_W1_CH_BAND GENMASK(4, 3)
#define RTW89_H2C_CHINFO_BE_W1_DFS BIT(5)
#define RTW89_H2C_CHINFO_BE_W1_PAUSE_DATA BIT(6)
#define RTW89_H2C_CHINFO_BE_W1_TX_NULL BIT(7)
#define RTW89_H2C_CHINFO_BE_W1_RANDOM BIT(8)
#define RTW89_H2C_CHINFO_BE_W1_NOTIFY GENMASK(13, 9)
#define RTW89_H2C_CHINFO_BE_W1_PROBE BIT(14)
#define RTW89_H2C_CHINFO_BE_W1_EARLY_LEAVE_CRIT GENMASK(17, 15)
#define RTW89_H2C_CHINFO_BE_W1_CHKPT_TIMER GENMASK(31, 24)
#define RTW89_H2C_CHINFO_BE_W2_EARLY_LEAVE_TIME GENMASK(7, 0)
#define RTW89_H2C_CHINFO_BE_W2_EARLY_LEAVE_TH GENMASK(15, 8)
#define RTW89_H2C_CHINFO_BE_W2_TX_PKT_CTRL GENMASK(31, 16)
#define RTW89_H2C_CHINFO_BE_W3_PKT0 GENMASK(7, 0)
#define RTW89_H2C_CHINFO_BE_W3_PKT1 GENMASK(15, 8)
#define RTW89_H2C_CHINFO_BE_W3_PKT2 GENMASK(23, 16)
#define RTW89_H2C_CHINFO_BE_W3_PKT3 GENMASK(31, 24)
#define RTW89_H2C_CHINFO_BE_W4_PKT4 GENMASK(7, 0)
#define RTW89_H2C_CHINFO_BE_W4_PKT5 GENMASK(15, 8)
#define RTW89_H2C_CHINFO_BE_W4_PKT6 GENMASK(23, 16)
#define RTW89_H2C_CHINFO_BE_W4_PKT7 GENMASK(31, 24)
#define RTW89_H2C_CHINFO_BE_W5_SW_DEF GENMASK(7, 0)
#define RTW89_H2C_CHINFO_BE_W5_FW_PROBE0_SSIDS GENMASK(31, 16)
#define RTW89_H2C_CHINFO_BE_W6_FW_PROBE0_SHORTSSIDS GENMASK(15, 0)
#define RTW89_H2C_CHINFO_BE_W6_FW_PROBE0_BSSIDS GENMASK(31, 16)

struct rtw89_h2c_chinfo {
	u8 ch_num;
	u8 elem_size;
	u8 arg;
	u8 rsvd0;
	struct rtw89_h2c_chinfo_elem elem[] __counted_by(ch_num);
} __packed;

#define RTW89_H2C_CHINFO_ARG_MAC_IDX_MASK BIT(0)
#define RTW89_H2C_CHINFO_ARG_APPEND_MASK BIT(1)

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

struct rtw89_h2c_scanofld_be_macc_role {
	__le32 w0;
} __packed;

#define RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_BAND GENMASK(1, 0)
#define RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_PORT GENMASK(4, 2)
#define RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_MACID GENMASK(23, 8)
#define RTW89_H2C_SCANOFLD_BE_MACC_ROLE_W0_OPCH_END GENMASK(31, 24)

struct rtw89_h2c_scanofld_be_opch {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
} __packed;

#define RTW89_H2C_SCANOFLD_BE_OPCH_W0_MACID GENMASK(15, 0)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W0_BAND GENMASK(17, 16)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W0_PORT GENMASK(20, 18)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W0_POLICY GENMASK(22, 21)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W0_TXNULL BIT(23)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W0_POLICY_VAL GENMASK(31, 24)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W1_DURATION GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W1_CH_BAND GENMASK(9, 8)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W1_BW GENMASK(12, 10)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W1_NOTIFY GENMASK(14, 13)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W1_PRI_CH GENMASK(23, 16)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W1_CENTRAL_CH GENMASK(31, 24)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W2_PKTS_CTRL GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W2_SW_DEF GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W2_SS GENMASK(18, 16)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT0 GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT1 GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT2 GENMASK(23, 16)
#define RTW89_H2C_SCANOFLD_BE_OPCH_W3_PKT3 GENMASK(31, 24)

struct rtw89_h2c_scanofld_be {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
	__le32 w8;
	struct rtw89_h2c_scanofld_be_macc_role role[];
} __packed;

#define RTW89_H2C_SCANOFLD_BE_W0_OP GENMASK(1, 0)
#define RTW89_H2C_SCANOFLD_BE_W0_SCAN_MODE GENMASK(3, 2)
#define RTW89_H2C_SCANOFLD_BE_W0_REPEAT GENMASK(5, 4)
#define RTW89_H2C_SCANOFLD_BE_W0_NOTIFY_END BIT(6)
#define RTW89_H2C_SCANOFLD_BE_W0_LEARN_CH BIT(7)
#define RTW89_H2C_SCANOFLD_BE_W0_MACID GENMASK(23, 8)
#define RTW89_H2C_SCANOFLD_BE_W0_PORT GENMASK(26, 24)
#define RTW89_H2C_SCANOFLD_BE_W0_BAND GENMASK(28, 27)
#define RTW89_H2C_SCANOFLD_BE_W0_PROBE_WITH_RATE BIT(29)
#define RTW89_H2C_SCANOFLD_BE_W1_NUM_MACC_ROLE GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_W1_NUM_OP GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_BE_W1_NORM_PD GENMASK(31, 16)
#define RTW89_H2C_SCANOFLD_BE_W2_SLOW_PD GENMASK(15, 0)
#define RTW89_H2C_SCANOFLD_BE_W2_NORM_CY GENMASK(23, 16)
#define RTW89_H2C_SCANOFLD_BE_W2_OPCH_END GENMASK(31, 24)
#define RTW89_H2C_SCANOFLD_BE_W3_NUM_SSID GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_W3_NUM_SHORT_SSID GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_BE_W3_NUM_BSSID GENMASK(23, 16)
#define RTW89_H2C_SCANOFLD_BE_W3_PROBEID GENMASK(31, 24)
#define RTW89_H2C_SCANOFLD_BE_W4_PROBE_5G GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_W4_PROBE_6G GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_BE_W4_DELAY_START GENMASK(31, 16)
#define RTW89_H2C_SCANOFLD_BE_W5_MLO_MODE GENMASK(31, 0)
#define RTW89_H2C_SCANOFLD_BE_W6_CHAN_PROHIB_LOW GENMASK(31, 0)
#define RTW89_H2C_SCANOFLD_BE_W7_CHAN_PROHIB_HIGH GENMASK(31, 0)
#define RTW89_H2C_SCANOFLD_BE_W8_PROBE_RATE_2GHZ GENMASK(7, 0)
#define RTW89_H2C_SCANOFLD_BE_W8_PROBE_RATE_5GHZ GENMASK(15, 8)
#define RTW89_H2C_SCANOFLD_BE_W8_PROBE_RATE_6GHZ GENMASK(23, 16)

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

enum rtw89_fw_mcc_old_group_actions {
	RTW89_FW_MCC_OLD_GROUP_ACT_NONE = 0,
	RTW89_FW_MCC_OLD_GROUP_ACT_REPLACE = 1,
};

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

enum rtw89_h2c_mrc_sch_types {
	RTW89_H2C_MRC_SCH_BAND0_ONLY = 0,
	RTW89_H2C_MRC_SCH_BAND1_ONLY = 1,
	RTW89_H2C_MRC_SCH_DUAL_BAND = 2,
};

enum rtw89_h2c_mrc_role_types {
	RTW89_H2C_MRC_ROLE_WIFI = 0,
	RTW89_H2C_MRC_ROLE_BT = 1,
	RTW89_H2C_MRC_ROLE_EMPTY = 2,
};

#define RTW89_MAC_MRC_MAX_ADD_SLOT_NUM 3
#define RTW89_MAC_MRC_MAX_ADD_ROLE_NUM_PER_SLOT 1 /* before MLO */

struct rtw89_fw_mrc_add_slot_arg {
	u16 duration; /* unit: TU */
	bool courtesy_en;
	u8 courtesy_period;
	u8 courtesy_target; /* slot idx */

	unsigned int role_num;
	struct {
		enum rtw89_h2c_mrc_role_types role_type;
		bool is_master;
		bool en_tx_null;
		enum rtw89_band band;
		enum rtw89_bandwidth bw;
		u8 macid;
		u8 central_ch;
		u8 primary_ch;
		u8 null_early; /* unit: TU */

		/* if MLD, for macid: [0, chip::support_mld_num)
		 * otherwise, for macid: [0, 32)
		 */
		u32 macid_main_bitmap;
		/* for MLD, bit X maps to macid: X + chip::support_mld_num */
		u32 macid_paired_bitmap;
	} roles[RTW89_MAC_MRC_MAX_ADD_ROLE_NUM_PER_SLOT];
};

struct rtw89_fw_mrc_add_arg {
	u8 sch_idx;
	enum rtw89_h2c_mrc_sch_types sch_type;
	bool btc_in_sch;

	unsigned int slot_num;
	struct rtw89_fw_mrc_add_slot_arg slots[RTW89_MAC_MRC_MAX_ADD_SLOT_NUM];
};

struct rtw89_h2c_mrc_add_role {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 macid_main_bitmap;
	__le32 macid_paired_bitmap;
} __packed;

#define RTW89_H2C_MRC_ADD_ROLE_W0_MACID GENMASK(15, 0)
#define RTW89_H2C_MRC_ADD_ROLE_W0_ROLE_TYPE GENMASK(23, 16)
#define RTW89_H2C_MRC_ADD_ROLE_W0_IS_MASTER BIT(24)
#define RTW89_H2C_MRC_ADD_ROLE_W0_IS_ALT_ROLE BIT(25)
#define RTW89_H2C_MRC_ADD_ROLE_W0_TX_NULL_EN BIT(26)
#define RTW89_H2C_MRC_ADD_ROLE_W0_ROLE_ALT_EN BIT(27)
#define RTW89_H2C_MRC_ADD_ROLE_W1_CENTRAL_CH_SEG GENMASK(7, 0)
#define RTW89_H2C_MRC_ADD_ROLE_W1_PRI_CH GENMASK(15, 8)
#define RTW89_H2C_MRC_ADD_ROLE_W1_BW GENMASK(19, 16)
#define RTW89_H2C_MRC_ADD_ROLE_W1_CH_BAND_TYPE GENMASK(21, 20)
#define RTW89_H2C_MRC_ADD_ROLE_W1_RFK_BY_PASS BIT(22)
#define RTW89_H2C_MRC_ADD_ROLE_W1_CAN_BTC BIT(23)
#define RTW89_H2C_MRC_ADD_ROLE_W1_NULL_EARLY GENMASK(31, 24)
#define RTW89_H2C_MRC_ADD_ROLE_W2_ALT_PERIOD GENMASK(7, 0)
#define RTW89_H2C_MRC_ADD_ROLE_W2_ALT_ROLE_TYPE GENMASK(15, 8)
#define RTW89_H2C_MRC_ADD_ROLE_W2_ALT_ROLE_MACID GENMASK(23, 16)

struct rtw89_h2c_mrc_add_slot {
	__le32 w0;
	__le32 w1;
	struct rtw89_h2c_mrc_add_role roles[];
} __packed;

#define RTW89_H2C_MRC_ADD_SLOT_W0_DURATION GENMASK(15, 0)
#define RTW89_H2C_MRC_ADD_SLOT_W0_COURTESY_EN BIT(17)
#define RTW89_H2C_MRC_ADD_SLOT_W0_ROLE_NUM GENMASK(31, 24)
#define RTW89_H2C_MRC_ADD_SLOT_W1_COURTESY_PERIOD GENMASK(7, 0)
#define RTW89_H2C_MRC_ADD_SLOT_W1_COURTESY_TARGET GENMASK(15, 8)

struct rtw89_h2c_mrc_add {
	__le32 w0;
	/* Logically append flexible struct rtw89_h2c_mrc_add_slot, but there
	 * are other flexible array inside it. We cannot access them correctly
	 * through this struct. So, in case misusing, we don't really declare
	 * it here.
	 */
} __packed;

#define RTW89_H2C_MRC_ADD_W0_SCH_IDX GENMASK(3, 0)
#define RTW89_H2C_MRC_ADD_W0_SCH_TYPE GENMASK(7, 4)
#define RTW89_H2C_MRC_ADD_W0_SLOT_NUM GENMASK(15, 8)
#define RTW89_H2C_MRC_ADD_W0_BTC_IN_SCH BIT(16)

enum rtw89_h2c_mrc_start_actions {
	RTW89_H2C_MRC_START_ACTION_START_NEW = 0,
	RTW89_H2C_MRC_START_ACTION_REPLACE_OLD = 1,
};

struct rtw89_fw_mrc_start_arg {
	u8 sch_idx;
	u8 old_sch_idx;
	u64 start_tsf;
	enum rtw89_h2c_mrc_start_actions action;
};

struct rtw89_h2c_mrc_start {
	__le32 w0;
	__le32 start_tsf_low;
	__le32 start_tsf_high;
} __packed;

#define RTW89_H2C_MRC_START_W0_SCH_IDX GENMASK(3, 0)
#define RTW89_H2C_MRC_START_W0_OLD_SCH_IDX GENMASK(7, 4)
#define RTW89_H2C_MRC_START_W0_ACTION GENMASK(15, 8)

struct rtw89_h2c_mrc_del {
	__le32 w0;
} __packed;

#define RTW89_H2C_MRC_DEL_W0_SCH_IDX GENMASK(3, 0)
#define RTW89_H2C_MRC_DEL_W0_DEL_ALL BIT(4)
#define RTW89_H2C_MRC_DEL_W0_STOP_ONLY BIT(5)
#define RTW89_H2C_MRC_DEL_W0_SPECIFIC_ROLE_EN BIT(6)
#define RTW89_H2C_MRC_DEL_W0_STOP_SLOT_IDX GENMASK(15, 8)
#define RTW89_H2C_MRC_DEL_W0_SPECIFIC_ROLE_MACID GENMASK(31, 16)

#define RTW89_MAC_MRC_MAX_REQ_TSF_NUM 2

struct rtw89_fw_mrc_req_tsf_arg {
	unsigned int num;
	struct {
		u8 band;
		u8 port;
	} infos[RTW89_MAC_MRC_MAX_REQ_TSF_NUM];
};

struct rtw89_h2c_mrc_req_tsf {
	u8 req_tsf_num;
	u8 infos[] __counted_by(req_tsf_num);
} __packed;

#define RTW89_H2C_MRC_REQ_TSF_INFO_BAND GENMASK(3, 0)
#define RTW89_H2C_MRC_REQ_TSF_INFO_PORT GENMASK(7, 4)

enum rtw89_h2c_mrc_upd_bitmap_actions {
	RTW89_H2C_MRC_UPD_BITMAP_ACTION_DEL = 0,
	RTW89_H2C_MRC_UPD_BITMAP_ACTION_ADD = 1,
};

struct rtw89_fw_mrc_upd_bitmap_arg {
	u8 sch_idx;
	u8 macid;
	u8 client_macid;
	enum rtw89_h2c_mrc_upd_bitmap_actions action;
};

struct rtw89_h2c_mrc_upd_bitmap {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_H2C_MRC_UPD_BITMAP_W0_SCH_IDX GENMASK(3, 0)
#define RTW89_H2C_MRC_UPD_BITMAP_W0_ACTION BIT(4)
#define RTW89_H2C_MRC_UPD_BITMAP_W0_MACID GENMASK(31, 16)
#define RTW89_H2C_MRC_UPD_BITMAP_W1_CLIENT_MACID GENMASK(15, 0)

struct rtw89_fw_mrc_sync_arg {
	u8 offset; /* unit: TU */
	struct {
		u8 band;
		u8 port;
	} src, dest;
};

struct rtw89_h2c_mrc_sync {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_H2C_MRC_SYNC_W0_SYNC_EN BIT(0)
#define RTW89_H2C_MRC_SYNC_W0_SRC_PORT GENMASK(11, 8)
#define RTW89_H2C_MRC_SYNC_W0_SRC_BAND GENMASK(15, 12)
#define RTW89_H2C_MRC_SYNC_W0_DEST_PORT GENMASK(19, 16)
#define RTW89_H2C_MRC_SYNC_W0_DEST_BAND GENMASK(23, 20)
#define RTW89_H2C_MRC_SYNC_W1_OFFSET GENMASK(15, 0)

struct rtw89_fw_mrc_upd_duration_arg {
	u8 sch_idx;
	u64 start_tsf;

	unsigned int slot_num;
	struct {
		u8 slot_idx;
		u16 duration; /* unit: TU */
	} slots[RTW89_MAC_MRC_MAX_ADD_SLOT_NUM];
};

struct rtw89_h2c_mrc_upd_duration {
	__le32 w0;
	__le32 start_tsf_low;
	__le32 start_tsf_high;
	__le32 slots[];
} __packed;

#define RTW89_H2C_MRC_UPD_DURATION_W0_SCH_IDX GENMASK(3, 0)
#define RTW89_H2C_MRC_UPD_DURATION_W0_SLOT_NUM GENMASK(15, 8)
#define RTW89_H2C_MRC_UPD_DURATION_W0_BTC_IN_SCH BIT(16)
#define RTW89_H2C_MRC_UPD_DURATION_SLOT_SLOT_IDX GENMASK(7, 0)
#define RTW89_H2C_MRC_UPD_DURATION_SLOT_DURATION GENMASK(31, 16)

struct rtw89_h2c_wow_aoac {
	__le32 w0;
} __packed;

#define RTW89_C2H_HEADER_LEN 8

struct rtw89_c2h_hdr {
	__le32 w0;
	__le32 w1;
} __packed;

#define RTW89_C2H_HDR_W0_CATEGORY GENMASK(1, 0)
#define RTW89_C2H_HDR_W0_CLASS GENMASK(7, 2)
#define RTW89_C2H_HDR_W0_FUNC GENMASK(15, 8)
#define RTW89_C2H_HDR_W1_LEN GENMASK(13, 0)

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

struct rtw89_fw_c2h_log_fmt {
	__le16 signature;
	u8 feature;
	u8 syntax;
	__le32 fmt_id;
	u8 file_num;
	__le16 line_num;
	u8 argc;
	union {
		DECLARE_FLEX_ARRAY(u8, raw);
		DECLARE_FLEX_ARRAY(__le32, argv);
	} __packed u;
} __packed;

#define RTW89_C2H_FW_FORMATTED_LOG_MIN_LEN 11
#define RTW89_C2H_FW_LOG_FEATURE_PARA_INT BIT(2)
#define RTW89_C2H_FW_LOG_MAX_PARA_NUM 16
#define RTW89_C2H_FW_LOG_SIGNATURE 0xA5A5
#define RTW89_C2H_FW_LOG_STR_BUF_SIZE 512

struct rtw89_c2h_mac_bcnfltr_rpt {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_MACID GENMASK(7, 0)
#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_TYPE GENMASK(9, 8)
#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_EVENT GENMASK(11, 10)
#define RTW89_C2H_MAC_BCNFLTR_RPT_W2_MA GENMASK(23, 16)

struct rtw89_c2h_ra_rpt {
	struct rtw89_c2h_hdr hdr;
	__le32 w2;
	__le32 w3;
} __packed;

#define RTW89_C2H_RA_RPT_W2_MACID GENMASK(15, 0)
#define RTW89_C2H_RA_RPT_W2_RETRY_RATIO GENMASK(23, 16)
#define RTW89_C2H_RA_RPT_W2_MCSNSS_B7 BIT(31)
#define RTW89_C2H_RA_RPT_W3_MCSNSS GENMASK(6, 0)
#define RTW89_C2H_RA_RPT_W3_MD_SEL GENMASK(9, 8)
#define RTW89_C2H_RA_RPT_W3_GILTF GENMASK(12, 10)
#define RTW89_C2H_RA_RPT_W3_BW GENMASK(14, 13)
#define RTW89_C2H_RA_RPT_W3_MD_SEL_B2 BIT(15)
#define RTW89_C2H_RA_RPT_W3_BW_B2 BIT(16)

/* For WiFi 6 chips:
 *   VHT, HE, HT-old: [6:4]: NSS, [3:0]: MCS
 *   HT-new: [6:5]: NA, [4:0]: MCS
 * For WiFi 7 chips (V1):
 *   HT, VHT, HE, EHT: [7:5]: NSS, [4:0]: MCS
 */
#define RTW89_RA_RATE_MASK_NSS GENMASK(6, 4)
#define RTW89_RA_RATE_MASK_MCS GENMASK(3, 0)
#define RTW89_RA_RATE_MASK_NSS_V1 GENMASK(7, 5)
#define RTW89_RA_RATE_MASK_MCS_V1 GENMASK(4, 0)
#define RTW89_RA_RATE_MASK_HT_MCS GENMASK(4, 0)
#define RTW89_MK_HT_RATE(nss, mcs) (FIELD_PREP(GENMASK(4, 3), nss) | \
				    FIELD_PREP(GENMASK(2, 0), mcs))

#define RTW89_GET_MAC_C2H_PKTOFLD_ID(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(7, 0))
#define RTW89_GET_MAC_C2H_PKTOFLD_OP(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(10, 8))
#define RTW89_GET_MAC_C2H_PKTOFLD_LEN(c2h) \
	le32_get_bits(*((const __le32 *)(c2h) + 2), GENMASK(31, 16))

struct rtw89_c2h_scanofld {
	__le32 w0;
	__le32 w1;
	__le32 w2;
	__le32 w3;
	__le32 w4;
	__le32 w5;
	__le32 w6;
	__le32 w7;
} __packed;

#define RTW89_C2H_SCANOFLD_W2_PRI_CH GENMASK(7, 0)
#define RTW89_C2H_SCANOFLD_W2_RSN GENMASK(19, 16)
#define RTW89_C2H_SCANOFLD_W2_STATUS GENMASK(23, 20)
#define RTW89_C2H_SCANOFLD_W2_PERIOD GENMASK(31, 24)
#define RTW89_C2H_SCANOFLD_W5_TX_FAIL GENMASK(3, 0)
#define RTW89_C2H_SCANOFLD_W5_AIR_DENSITY GENMASK(7, 4)
#define RTW89_C2H_SCANOFLD_W5_BAND GENMASK(25, 24)
#define RTW89_C2H_SCANOFLD_W5_MAC_IDX BIT(26)
#define RTW89_C2H_SCANOFLD_W6_SW_DEF GENMASK(7, 0)
#define RTW89_C2H_SCANOFLD_W6_EXPECT_PERIOD GENMASK(15, 8)
#define RTW89_C2H_SCANOFLD_W6_FW_DEF GENMASK(23, 16)
#define RTW89_C2H_SCANOFLD_W7_REPORT_TSF GENMASK(31, 0)

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

struct rtw89_mac_mrc_tsf_rpt {
	unsigned int num;
	u64 tsfs[RTW89_MAC_MRC_MAX_REQ_TSF_NUM];
};

static_assert(sizeof(struct rtw89_mac_mrc_tsf_rpt) <= RTW89_COMPLETION_BUF_SIZE);

struct rtw89_c2h_mrc_tsf_rpt_info {
	__le32 tsf_low;
	__le32 tsf_high;
} __packed;

struct rtw89_c2h_mrc_tsf_rpt {
	struct rtw89_c2h_hdr hdr;
	__le32 w2;
	struct rtw89_c2h_mrc_tsf_rpt_info infos[];
} __packed;

#define RTW89_C2H_MRC_TSF_RPT_W2_REQ_TSF_NUM GENMASK(7, 0)

struct rtw89_c2h_mrc_status_rpt {
	struct rtw89_c2h_hdr hdr;
	__le32 w2;
	__le32 tsf_low;
	__le32 tsf_high;
} __packed;

#define RTW89_C2H_MRC_STATUS_RPT_W2_STATUS GENMASK(5, 0)
#define RTW89_C2H_MRC_STATUS_RPT_W2_SCH_IDX GENMASK(7, 6)

struct rtw89_c2h_pkt_ofld_rsp {
	__le32 w0;
	__le32 w1;
	__le32 w2;
} __packed;

#define RTW89_C2H_PKT_OFLD_RSP_W2_PTK_ID GENMASK(7, 0)
#define RTW89_C2H_PKT_OFLD_RSP_W2_PTK_OP GENMASK(10, 8)
#define RTW89_C2H_PKT_OFLD_RSP_W2_PTK_LEN GENMASK(31, 16)

struct rtw89_c2h_wow_aoac_report {
	struct rtw89_c2h_hdr c2h_hdr;
	u8 rpt_ver;
	u8 sec_type;
	u8 key_idx;
	u8 pattern_idx;
	u8 rekey_ok;
	u8 rsvd1[3];
	u8 ptk_tx_iv[8];
	u8 eapol_key_replay_count[8];
	u8 gtk[32];
	u8 ptk_rx_iv[8];
	u8 gtk_rx_iv[4][8];
	__le64 igtk_key_id;
	__le64 igtk_ipn;
	u8 igtk[32];
	u8 csa_pri_ch;
	u8 csa_bw_ch_offset;
	u8 csa_ch_band_chsw_failed;
	u8 csa_rsvd1;
} __packed;

#define RTW89_C2H_WOW_AOAC_RPT_REKEY_IDX BIT(0)

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

struct rtw89_fw_logsuit_hdr {
	__le32 rsvd;
	__le32 count;
	__le32 ids[];
} __packed;

#define RTW89_FW_ELEMENT_ALIGN 16

enum rtw89_fw_element_id {
	RTW89_FW_ELEMENT_ID_BBMCU0 = 0,
	RTW89_FW_ELEMENT_ID_BBMCU1 = 1,
	RTW89_FW_ELEMENT_ID_BB_REG = 2,
	RTW89_FW_ELEMENT_ID_BB_GAIN = 3,
	RTW89_FW_ELEMENT_ID_RADIO_A = 4,
	RTW89_FW_ELEMENT_ID_RADIO_B = 5,
	RTW89_FW_ELEMENT_ID_RADIO_C = 6,
	RTW89_FW_ELEMENT_ID_RADIO_D = 7,
	RTW89_FW_ELEMENT_ID_RF_NCTL = 8,
	RTW89_FW_ELEMENT_ID_TXPWR_BYRATE = 9,
	RTW89_FW_ELEMENT_ID_TXPWR_LMT_2GHZ = 10,
	RTW89_FW_ELEMENT_ID_TXPWR_LMT_5GHZ = 11,
	RTW89_FW_ELEMENT_ID_TXPWR_LMT_6GHZ = 12,
	RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_2GHZ = 13,
	RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_5GHZ = 14,
	RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_6GHZ = 15,
	RTW89_FW_ELEMENT_ID_TX_SHAPE_LMT = 16,
	RTW89_FW_ELEMENT_ID_TX_SHAPE_LMT_RU = 17,
	RTW89_FW_ELEMENT_ID_TXPWR_TRK = 18,
	RTW89_FW_ELEMENT_ID_RFKLOG_FMT = 19,

	RTW89_FW_ELEMENT_ID_NUM,
};

#define BITS_OF_RTW89_TXPWR_FW_ELEMENTS \
	(BIT(RTW89_FW_ELEMENT_ID_TXPWR_BYRATE) | \
	 BIT(RTW89_FW_ELEMENT_ID_TXPWR_LMT_2GHZ) | \
	 BIT(RTW89_FW_ELEMENT_ID_TXPWR_LMT_5GHZ) | \
	 BIT(RTW89_FW_ELEMENT_ID_TXPWR_LMT_6GHZ) | \
	 BIT(RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_2GHZ) | \
	 BIT(RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_5GHZ) | \
	 BIT(RTW89_FW_ELEMENT_ID_TXPWR_LMT_RU_6GHZ) | \
	 BIT(RTW89_FW_ELEMENT_ID_TX_SHAPE_LMT) | \
	 BIT(RTW89_FW_ELEMENT_ID_TX_SHAPE_LMT_RU))

#define RTW89_BE_GEN_DEF_NEEDED_FW_ELEMENTS (BIT(RTW89_FW_ELEMENT_ID_BBMCU0) | \
					     BIT(RTW89_FW_ELEMENT_ID_BB_REG) | \
					     BIT(RTW89_FW_ELEMENT_ID_RADIO_A) | \
					     BIT(RTW89_FW_ELEMENT_ID_RADIO_B) | \
					     BIT(RTW89_FW_ELEMENT_ID_RF_NCTL) | \
					     BIT(RTW89_FW_ELEMENT_ID_TXPWR_TRK) | \
					     BITS_OF_RTW89_TXPWR_FW_ELEMENTS)

struct __rtw89_fw_txpwr_element {
	u8 rsvd0;
	u8 rsvd1;
	u8 rfe_type;
	u8 ent_sz;
	__le32 num_ents;
	u8 content[];
} __packed;

enum rtw89_fw_txpwr_trk_type {
	__RTW89_FW_TXPWR_TRK_TYPE_6GHZ_START = 0,
	RTW89_FW_TXPWR_TRK_TYPE_6GB_N = 0,
	RTW89_FW_TXPWR_TRK_TYPE_6GB_P = 1,
	RTW89_FW_TXPWR_TRK_TYPE_6GA_N = 2,
	RTW89_FW_TXPWR_TRK_TYPE_6GA_P = 3,
	__RTW89_FW_TXPWR_TRK_TYPE_6GHZ_MAX = 3,

	__RTW89_FW_TXPWR_TRK_TYPE_5GHZ_START = 4,
	RTW89_FW_TXPWR_TRK_TYPE_5GB_N = 4,
	RTW89_FW_TXPWR_TRK_TYPE_5GB_P = 5,
	RTW89_FW_TXPWR_TRK_TYPE_5GA_N = 6,
	RTW89_FW_TXPWR_TRK_TYPE_5GA_P = 7,
	__RTW89_FW_TXPWR_TRK_TYPE_5GHZ_MAX = 7,

	__RTW89_FW_TXPWR_TRK_TYPE_2GHZ_START = 8,
	RTW89_FW_TXPWR_TRK_TYPE_2GB_N = 8,
	RTW89_FW_TXPWR_TRK_TYPE_2GB_P = 9,
	RTW89_FW_TXPWR_TRK_TYPE_2GA_N = 10,
	RTW89_FW_TXPWR_TRK_TYPE_2GA_P = 11,
	RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_B_N = 12,
	RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_B_P = 13,
	RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_A_N = 14,
	RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_A_P = 15,
	__RTW89_FW_TXPWR_TRK_TYPE_2GHZ_MAX = 15,

	RTW89_FW_TXPWR_TRK_TYPE_NR,
};

struct rtw89_fw_txpwr_track_cfg {
	const s8 (*delta[RTW89_FW_TXPWR_TRK_TYPE_NR])[DELTA_SWINGIDX_SIZE];
};

#define RTW89_DEFAULT_NEEDED_FW_TXPWR_TRK_6GHZ \
	(BIT(RTW89_FW_TXPWR_TRK_TYPE_6GB_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_6GB_P) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_6GA_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_6GA_P))
#define RTW89_DEFAULT_NEEDED_FW_TXPWR_TRK_5GHZ \
	(BIT(RTW89_FW_TXPWR_TRK_TYPE_5GB_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_5GB_P) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_5GA_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_5GA_P))
#define RTW89_DEFAULT_NEEDED_FW_TXPWR_TRK_2GHZ \
	(BIT(RTW89_FW_TXPWR_TRK_TYPE_2GB_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2GB_P) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2GA_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2GA_P) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_B_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_B_P) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_A_N) | \
	 BIT(RTW89_FW_TXPWR_TRK_TYPE_2G_CCK_A_P))

struct rtw89_fw_element_hdr {
	__le32 id; /* enum rtw89_fw_element_id */
	__le32 size; /* exclude header size */
	u8 ver[4];
	__le32 rsvd0;
	__le32 rsvd1;
	__le32 rsvd2;
	union {
		struct {
			u8 priv[8];
			u8 contents[];
		} __packed common;
		struct {
			u8 idx;
			u8 rsvd[7];
			struct {
				__le32 addr;
				__le32 data;
			} __packed regs[];
		} __packed reg2;
		struct {
			u8 cv;
			u8 priv[7];
			u8 contents[];
		} __packed bbmcu;
		struct {
			__le32 bitmap; /* bitmap of enum rtw89_fw_txpwr_trk_type */
			__le32 rsvd;
			s8 contents[][DELTA_SWINGIDX_SIZE];
		} __packed txpwr_trk;
		struct {
			u8 nr;
			u8 rsvd[3];
			u8 rfk_id; /* enum rtw89_phy_c2h_rfk_log_func */
			u8 rsvd1[3];
			__le16 offset[];
		} __packed rfk_log_fmt;
		struct __rtw89_fw_txpwr_element txpwr;
	} __packed u;
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
enum rtw89_wow_h2c_func {
	H2C_FUNC_KEEP_ALIVE		= 0x0,
	H2C_FUNC_DISCONNECT_DETECT	= 0x1,
	H2C_FUNC_WOW_GLOBAL		= 0x2,
	H2C_FUNC_GTK_OFLD		= 0x3,
	H2C_FUNC_ARP_OFLD		= 0x4,
	H2C_FUNC_WAKEUP_CTRL		= 0x8,
	H2C_FUNC_WOW_CAM_UPD		= 0xC,
	H2C_FUNC_AOAC_REPORT_REQ	= 0xD,

	NUM_OF_RTW89_WOW_H2C_FUNC,
};

#define RTW89_WOW_WAIT_COND(tag, func) \
	((tag) * NUM_OF_RTW89_WOW_H2C_FUNC + (func))

#define RTW89_WOW_WAIT_COND_AOAC \
	RTW89_WOW_WAIT_COND(0 /* don't care */, H2C_FUNC_AOAC_REPORT_REQ)

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
#define H2C_FUNC_MAC_DCTLINFO_UD_V2	0xc
#define H2C_FUNC_MAC_BCN_UPD_BE		0xd
#define H2C_FUNC_MAC_CCTLINFO_UD_G7	0x11

/* CLASS 6 - Address CAM */
#define H2C_CL_MAC_ADDR_CAM_UPDATE	0x6
#define H2C_FUNC_MAC_ADDR_CAM_UPD	0x0

/* CLASS 8 - Media Status Report */
#define H2C_CL_MAC_MEDIA_RPT		0x8
#define H2C_FUNC_MAC_JOININFO		0x0
#define H2C_FUNC_MAC_FWROLE_MAINTAIN	0x4
#define H2C_FUNC_NOTIFY_DBCC		0x5

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
	H2C_FUNC_MAC_MACID_PAUSE_SLEEP	= 0x28,
	H2C_FUNC_SCANOFLD_BE		= 0x2c,

	NUM_OF_RTW89_FW_OFLD_H2C_FUNC,
};

#define RTW89_FW_OFLD_WAIT_COND(tag, func) \
	((tag) * NUM_OF_RTW89_FW_OFLD_H2C_FUNC + (func))

#define RTW89_FW_OFLD_WAIT_COND_PKT_OFLD(pkt_id, pkt_op) \
	RTW89_FW_OFLD_WAIT_COND(RTW89_PKT_OFLD_WAIT_TAG(pkt_id, pkt_op), \
				H2C_FUNC_PACKET_OFLD)

#define RTW89_SCANOFLD_WAIT_COND_ADD_CH RTW89_FW_OFLD_WAIT_COND(0, H2C_FUNC_ADD_SCANOFLD_CH)

#define RTW89_SCANOFLD_WAIT_COND_START RTW89_FW_OFLD_WAIT_COND(0, H2C_FUNC_SCANOFLD)
#define RTW89_SCANOFLD_WAIT_COND_STOP RTW89_FW_OFLD_WAIT_COND(1, H2C_FUNC_SCANOFLD)
#define RTW89_SCANOFLD_BE_WAIT_COND_START RTW89_FW_OFLD_WAIT_COND(0, H2C_FUNC_SCANOFLD_BE)
#define RTW89_SCANOFLD_BE_WAIT_COND_STOP RTW89_FW_OFLD_WAIT_COND(1, H2C_FUNC_SCANOFLD_BE)


/* CLASS 10 - Security CAM */
#define H2C_CL_MAC_SEC_CAM		0xa
#define H2C_FUNC_MAC_SEC_UPD		0x1

/* CLASS 12 - BA CAM */
#define H2C_CL_BA_CAM			0xc
#define H2C_FUNC_MAC_BA_CAM		0x0
#define H2C_FUNC_MAC_BA_CAM_V1		0x1
#define H2C_FUNC_MAC_BA_CAM_INIT	0x2

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

/* CLASS 24 - MRC */
#define H2C_CL_MRC			0x18
enum rtw89_mrc_h2c_func {
	H2C_FUNC_MRC_REQ_TSF		= 0x0,
	H2C_FUNC_ADD_MRC		= 0x1,
	H2C_FUNC_START_MRC		= 0x2,
	H2C_FUNC_DEL_MRC		= 0x3,
	H2C_FUNC_MRC_SYNC		= 0x4,
	H2C_FUNC_MRC_UPD_DURATION	= 0x5,
	H2C_FUNC_MRC_UPD_BITMAP		= 0x6,

	NUM_OF_RTW89_MRC_H2C_FUNC,
};

/* can consider MRC's sch_idx as MCC's group */
#define RTW89_MRC_WAIT_COND(sch_idx, func) \
	((sch_idx) * NUM_OF_RTW89_MRC_H2C_FUNC + (func))

#define RTW89_MRC_WAIT_COND_REQ_TSF \
	RTW89_MRC_WAIT_COND(0 /* don't care */, H2C_FUNC_MRC_REQ_TSF)

#define H2C_CAT_OUTSRC			0x2

#define H2C_CL_OUTSRC_RA		0x1
#define H2C_FUNC_OUTSRC_RA_MACIDCFG	0x0

#define H2C_CL_OUTSRC_DM		0x2
#define H2C_FUNC_FW_LPS_CH_INFO		0xb

#define H2C_CL_OUTSRC_RF_REG_A		0x8
#define H2C_CL_OUTSRC_RF_REG_B		0x9
#define H2C_CL_OUTSRC_RF_FW_NOTIFY	0xa
#define H2C_FUNC_OUTSRC_RF_GET_MCCCH	0x2
#define H2C_CL_OUTSRC_RF_FW_RFK		0xb

enum rtw89_rfk_offload_h2c_func {
	H2C_FUNC_RFK_TSSI_OFFLOAD = 0x0,
	H2C_FUNC_RFK_IQK_OFFLOAD = 0x1,
	H2C_FUNC_RFK_DPK_OFFLOAD = 0x3,
	H2C_FUNC_RFK_TXGAPK_OFFLOAD = 0x4,
	H2C_FUNC_RFK_DACK_OFFLOAD = 0x5,
	H2C_FUNC_RFK_RXDCK_OFFLOAD = 0x6,
	H2C_FUNC_RFK_PRE_NOTIFY = 0x8,
};

struct rtw89_fw_h2c_rf_get_mccch {
	__le32 ch_0;
	__le32 ch_1;
	__le32 band_0;
	__le32 band_1;
	__le32 current_channel;
	__le32 current_band_type;
} __packed;

#define NUM_OF_RTW89_FW_RFK_PATH 2
#define NUM_OF_RTW89_FW_RFK_TBL 3

struct rtw89_fw_h2c_rfk_pre_info {
	struct {
		__le32 ch[NUM_OF_RTW89_FW_RFK_PATH][NUM_OF_RTW89_FW_RFK_TBL];
		__le32 band[NUM_OF_RTW89_FW_RFK_PATH][NUM_OF_RTW89_FW_RFK_TBL];
	} __packed dbcc;

	__le32 mlo_mode;
	struct {
		__le32 cur_ch[NUM_OF_RTW89_FW_RFK_PATH];
		__le32 cur_band[NUM_OF_RTW89_FW_RFK_PATH];
	} __packed tbl;

	__le32 phy_idx;
	__le32 cur_band;
	__le32 cur_bw;
	__le32 cur_center_ch;

	__le32 ktbl_sel0;
	__le32 ktbl_sel1;
	__le32 rfmod0;
	__le32 rfmod1;

	__le32 mlo_1_1;
	__le32 rfe_type;
	__le32 drv_mode;

	struct {
		__le32 ch[NUM_OF_RTW89_FW_RFK_PATH];
		__le32 band[NUM_OF_RTW89_FW_RFK_PATH];
	} __packed mlo;
} __packed;

struct rtw89_h2c_rf_tssi {
	__le16 len;
	u8 phy;
	u8 ch;
	u8 bw;
	u8 band;
	u8 hwtx_en;
	u8 cv;
	s8 curr_tssi_cck_de[2];
	s8 curr_tssi_cck_de_20m[2];
	s8 curr_tssi_cck_de_40m[2];
	s8 curr_tssi_efuse_cck_de[2];
	s8 curr_tssi_ofdm_de[2];
	s8 curr_tssi_ofdm_de_20m[2];
	s8 curr_tssi_ofdm_de_40m[2];
	s8 curr_tssi_ofdm_de_80m[2];
	s8 curr_tssi_ofdm_de_160m[2];
	s8 curr_tssi_ofdm_de_320m[2];
	s8 curr_tssi_efuse_ofdm_de[2];
	s8 curr_tssi_ofdm_de_diff_20m[2];
	s8 curr_tssi_ofdm_de_diff_80m[2];
	s8 curr_tssi_ofdm_de_diff_160m[2];
	s8 curr_tssi_ofdm_de_diff_320m[2];
	s8 curr_tssi_trim_de[2];
	u8 pg_thermal[2];
	u8 ftable[2][128];
	u8 tssi_mode;
} __packed;

struct rtw89_h2c_rf_iqk {
	__le32 phy_idx;
	__le32 dbcc;
} __packed;

struct rtw89_h2c_rf_dpk {
	u8 len;
	u8 phy;
	u8 dpk_enable;
	u8 kpath;
	u8 cur_band;
	u8 cur_bw;
	u8 cur_ch;
	u8 dpk_dbg_en;
} __packed;

struct rtw89_h2c_rf_txgapk {
	u8 len;
	u8 ktype;
	u8 phy;
	u8 kpath;
	u8 band;
	u8 bw;
	u8 ch;
	u8 cv;
} __packed;

struct rtw89_h2c_rf_dack {
	__le32 len;
	__le32 phy;
	__le32 type;
} __packed;

struct rtw89_h2c_rf_rxdck {
	u8 len;
	u8 phy;
	u8 is_afe;
	u8 kpath;
	u8 cur_band;
	u8 cur_bw;
	u8 cur_ch;
	u8 rxdck_dbg_en;
} __packed;

enum rtw89_rf_log_type {
	RTW89_RF_RUN_LOG = 0,
	RTW89_RF_RPT_LOG = 1,
};

struct rtw89_c2h_rf_log_hdr {
	u8 type; /* enum rtw89_rf_log_type */
	__le16 len;
	u8 content[];
} __packed;

struct rtw89_c2h_rf_run_log {
	__le32 fmt_idx;
	__le32 arg[4];
} __packed;

struct rtw89_c2h_rf_dpk_rpt_log {
	u8 ver;
	u8 idx[2];
	u8 band[2];
	u8 bw[2];
	u8 ch[2];
	u8 path_ok[2];
	u8 txagc[2];
	u8 ther[2];
	u8 gs[2];
	u8 dc_i[4];
	u8 dc_q[4];
	u8 corr_val[2];
	u8 corr_idx[2];
	u8 is_timeout[2];
	u8 rxbb_ov[2];
	u8 rsvd;
} __packed;

struct rtw89_c2h_rf_dack_rpt_log {
	u8 fwdack_ver;
	u8 fwdack_rpt_ver;
	u8 msbk_d[2][2][16];
	u8 dadck_d[2][2];
	u8 cdack_d[2][2][2];
	__le16 addck2_d[2][2][2];
	u8 adgaink_d[2][2];
	__le16 biask_d[2][2];
	u8 addck_timeout;
	u8 cdack_timeout;
	u8 dadck_timeout;
	u8 msbk_timeout;
	u8 adgaink_timeout;
	u8 dack_fail;
} __packed;

struct rtw89_c2h_rf_rxdck_rpt_log {
	u8 ver;
	u8 band[2];
	u8 bw[2];
	u8 ch[2];
	u8 timeout[2];
} __packed;

struct rtw89_c2h_rf_txgapk_rpt_log {
	__le32 r0x8010[2];
	__le32 chk_cnt;
	u8 track_d[2][17];
	u8 power_d[2][17];
	u8 is_txgapk_ok;
	u8 chk_id;
	u8 ver;
	u8 rsv1;
} __packed;

struct rtw89_c2h_rfk_report {
	struct rtw89_c2h_hdr hdr;
	u8 state; /* enum rtw89_rfk_report_state */
	u8 version;
} __packed;

#define RTW89_FW_RSVD_PLE_SIZE 0x800

#define RTW89_FW_BACKTRACE_INFO_SIZE 8
#define RTW89_VALID_FW_BACKTRACE_SIZE(_size) \
	((_size) % RTW89_FW_BACKTRACE_INFO_SIZE == 0)

#define RTW89_FW_BACKTRACE_MAX_SIZE 512 /* 8 * 64 (entries) */
#define RTW89_FW_BACKTRACE_KEY 0xBACEBACE

#define FWDL_WAIT_CNT 400000

int rtw89_fw_check_rdy(struct rtw89_dev *rtwdev, enum rtw89_fwdl_check_type type);
int rtw89_fw_recognize(struct rtw89_dev *rtwdev);
int rtw89_fw_recognize_elements(struct rtw89_dev *rtwdev);
const struct firmware *
rtw89_early_fw_feature_recognize(struct device *device,
				 const struct rtw89_chip_info *chip,
				 struct rtw89_fw_info *early_fw,
				 int *used_fw_format);
int rtw89_fw_download(struct rtw89_dev *rtwdev, enum rtw89_fw_type type,
		      bool include_bb);
void rtw89_load_firmware_work(struct work_struct *work);
void rtw89_unload_firmware(struct rtw89_dev *rtwdev);
int rtw89_wait_firmware_completion(struct rtw89_dev *rtwdev);
int rtw89_fw_log_prepare(struct rtw89_dev *rtwdev);
void rtw89_fw_log_dump(struct rtw89_dev *rtwdev, u8 *buf, u32 len);
void rtw89_h2c_pkt_set_hdr(struct rtw89_dev *rtwdev, struct sk_buff *skb,
			   u8 type, u8 cat, u8 class, u8 func,
			   bool rack, bool dack, u32 len);
int rtw89_fw_h2c_default_cmac_tbl(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *rtwvif,
				  struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_default_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				     struct rtw89_vif *rtwvif,
				     struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_default_dmac_tbl_v2(struct rtw89_dev *rtwdev,
				     struct rtw89_vif *rtwvif,
				     struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_assoc_cmac_tbl(struct rtw89_dev *rtwdev,
				struct ieee80211_vif *vif,
				struct ieee80211_sta *sta);
int rtw89_fw_h2c_assoc_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta);
int rtw89_fw_h2c_ampdu_cmac_tbl_g7(struct rtw89_dev *rtwdev,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta);
int rtw89_fw_h2c_txtime_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_txpath_cmac_tbl(struct rtw89_dev *rtwdev,
				 struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_update_beacon(struct rtw89_dev *rtwdev,
			       struct rtw89_vif *rtwvif);
int rtw89_fw_h2c_update_beacon_be(struct rtw89_dev *rtwdev,
				  struct rtw89_vif *rtwvif);
int rtw89_fw_h2c_cam(struct rtw89_dev *rtwdev, struct rtw89_vif *vif,
		     struct rtw89_sta *rtwsta, const u8 *scan_mac_addr);
int rtw89_fw_h2c_dctl_sec_cam_v1(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif,
				 struct rtw89_sta *rtwsta);
int rtw89_fw_h2c_dctl_sec_cam_v2(struct rtw89_dev *rtwdev,
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
int rtw89_fw_h2c_notify_dbcc(struct rtw89_dev *rtwdev, bool en);
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
int rtw89_fw_h2c_cxdrv_init(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_init_v7(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_role(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_role_v1(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_role_v2(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_role_v8(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_ctrl(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_ctrl_v7(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_trx(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_cxdrv_rfk(struct rtw89_dev *rtwdev, u8 type);
int rtw89_fw_h2c_del_pkt_offload(struct rtw89_dev *rtwdev, u8 id);
int rtw89_fw_h2c_add_pkt_offload(struct rtw89_dev *rtwdev, u8 *id,
				 struct sk_buff *skb_ofld);
int rtw89_fw_h2c_scan_list_offload(struct rtw89_dev *rtwdev, int ch_num,
				   struct list_head *chan_list);
int rtw89_fw_h2c_scan_list_offload_be(struct rtw89_dev *rtwdev, int ch_num,
				      struct list_head *chan_list);
int rtw89_fw_h2c_scan_offload(struct rtw89_dev *rtwdev,
			      struct rtw89_scan_option *opt,
			      struct rtw89_vif *vif);
int rtw89_fw_h2c_scan_offload_be(struct rtw89_dev *rtwdev,
				 struct rtw89_scan_option *opt,
				 struct rtw89_vif *vif);
int rtw89_fw_h2c_rf_reg(struct rtw89_dev *rtwdev,
			struct rtw89_fw_h2c_rf_reg_info *info,
			u16 len, u8 page);
int rtw89_fw_h2c_rf_ntfy_mcc(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_rf_pre_ntfy(struct rtw89_dev *rtwdev,
			     enum rtw89_phy_idx phy_idx);
int rtw89_fw_h2c_rf_tssi(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx,
			 enum rtw89_tssi_mode tssi_mode);
int rtw89_fw_h2c_rf_iqk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
int rtw89_fw_h2c_rf_dpk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
int rtw89_fw_h2c_rf_txgapk(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
int rtw89_fw_h2c_rf_dack(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
int rtw89_fw_h2c_rf_rxdck(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
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
int rtw89_fw_h2c_ba_cam_v1(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			   bool valid, struct ieee80211_ampdu_params *params);
void rtw89_fw_h2c_init_dynamic_ba_cam_v0_ext(struct rtw89_dev *rtwdev);
int rtw89_fw_h2c_init_ba_cam_users(struct rtw89_dev *rtwdev, u8 users,
				   u8 offset, u8 mac_idx);

int rtw89_fw_h2c_lps_parm(struct rtw89_dev *rtwdev,
			  struct rtw89_lps_parm *lps_param);
int rtw89_fw_h2c_lps_ch_info(struct rtw89_dev *rtwdev,
			     struct rtw89_vif *rtwvif);
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
int rtw89_hw_scan_add_chan_list(struct rtw89_dev *rtwdev,
				struct rtw89_vif *rtwvif, bool connected);
int rtw89_hw_scan_add_chan_list_be(struct rtw89_dev *rtwdev,
				   struct rtw89_vif *rtwvif, bool connected);
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
int rtw89_fw_h2c_arp_offload(struct rtw89_dev *rtwdev,
			     struct rtw89_vif *rtwvif, bool enable);
int rtw89_fw_h2c_disconnect_detect(struct rtw89_dev *rtwdev,
				   struct rtw89_vif *rtwvif, bool enable);
int rtw89_fw_h2c_wow_global(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			    bool enable);
int rtw89_fw_h2c_wow_wakeup_ctrl(struct rtw89_dev *rtwdev,
				 struct rtw89_vif *rtwvif, bool enable);
int rtw89_fw_wow_cam_update(struct rtw89_dev *rtwdev,
			    struct rtw89_wow_cam_info *cam_info);
int rtw89_fw_h2c_wow_gtk_ofld(struct rtw89_dev *rtwdev,
			      struct rtw89_vif *rtwvif,
			      bool enable);
int rtw89_fw_h2c_wow_request_aoac(struct rtw89_dev *rtwdev);
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
int rtw89_fw_h2c_mcc_macid_bitmap(struct rtw89_dev *rtwdev, u8 group, u8 macid,
				  u8 *bitmap);
int rtw89_fw_h2c_mcc_sync(struct rtw89_dev *rtwdev, u8 group, u8 source,
			  u8 target, u8 offset);
int rtw89_fw_h2c_mcc_set_duration(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_mcc_duration *p);
int rtw89_fw_h2c_mrc_add(struct rtw89_dev *rtwdev,
			 const struct rtw89_fw_mrc_add_arg *arg);
int rtw89_fw_h2c_mrc_start(struct rtw89_dev *rtwdev,
			   const struct rtw89_fw_mrc_start_arg *arg);
int rtw89_fw_h2c_mrc_del(struct rtw89_dev *rtwdev, u8 sch_idx);
int rtw89_fw_h2c_mrc_req_tsf(struct rtw89_dev *rtwdev,
			     const struct rtw89_fw_mrc_req_tsf_arg *arg,
			     struct rtw89_mac_mrc_tsf_rpt *rpt);
int rtw89_fw_h2c_mrc_upd_bitmap(struct rtw89_dev *rtwdev,
				const struct rtw89_fw_mrc_upd_bitmap_arg *arg);
int rtw89_fw_h2c_mrc_sync(struct rtw89_dev *rtwdev,
			  const struct rtw89_fw_mrc_sync_arg *arg);
int rtw89_fw_h2c_mrc_upd_duration(struct rtw89_dev *rtwdev,
				  const struct rtw89_fw_mrc_upd_duration_arg *arg);

static inline void rtw89_fw_h2c_init_ba_cam(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->bacam_ver == RTW89_BACAM_V0_EXT)
		rtw89_fw_h2c_init_dynamic_ba_cam_v0_ext(rtwdev);
}

static inline int rtw89_chip_h2c_default_cmac_tbl(struct rtw89_dev *rtwdev,
						  struct rtw89_vif *rtwvif,
						  struct rtw89_sta *rtwsta)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->h2c_default_cmac_tbl(rtwdev, rtwvif, rtwsta);
}

static inline int rtw89_chip_h2c_default_dmac_tbl(struct rtw89_dev *rtwdev,
						  struct rtw89_vif *rtwvif,
						  struct rtw89_sta *rtwsta)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->h2c_default_dmac_tbl)
		return chip->ops->h2c_default_dmac_tbl(rtwdev, rtwvif, rtwsta);

	return 0;
}

static inline int rtw89_chip_h2c_update_beacon(struct rtw89_dev *rtwdev,
					       struct rtw89_vif *rtwvif)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->h2c_update_beacon(rtwdev, rtwvif);
}

static inline int rtw89_chip_h2c_assoc_cmac_tbl(struct rtw89_dev *rtwdev,
						struct ieee80211_vif *vif,
						struct ieee80211_sta *sta)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->h2c_assoc_cmac_tbl(rtwdev, vif, sta);
}

static inline int rtw89_chip_h2c_ampdu_cmac_tbl(struct rtw89_dev *rtwdev,
						struct ieee80211_vif *vif,
						struct ieee80211_sta *sta)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->h2c_ampdu_cmac_tbl)
		return chip->ops->h2c_ampdu_cmac_tbl(rtwdev, vif, sta);

	return 0;
}

static inline
int rtw89_chip_h2c_ba_cam(struct rtw89_dev *rtwdev, struct rtw89_sta *rtwsta,
			  bool valid, struct ieee80211_ampdu_params *params)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->h2c_ba_cam(rtwdev, rtwsta, valid, params);
}

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_byrate_entry {
	u8 band;
	u8 nss;
	u8 rs;
	u8 shf;
	u8 len;
	__le32 data;
	u8 bw;
	u8 ofdma;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_lmt_2ghz_entry {
	u8 bw;
	u8 nt;
	u8 rs;
	u8 bf;
	u8 regd;
	u8 ch_idx;
	s8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_lmt_5ghz_entry {
	u8 bw;
	u8 nt;
	u8 rs;
	u8 bf;
	u8 regd;
	u8 ch_idx;
	s8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_lmt_6ghz_entry {
	u8 bw;
	u8 nt;
	u8 rs;
	u8 bf;
	u8 regd;
	u8 reg_6ghz_power;
	u8 ch_idx;
	s8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_lmt_ru_2ghz_entry {
	u8 ru;
	u8 nt;
	u8 regd;
	u8 ch_idx;
	s8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_lmt_ru_5ghz_entry {
	u8 ru;
	u8 nt;
	u8 regd;
	u8 ch_idx;
	s8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_txpwr_lmt_ru_6ghz_entry {
	u8 ru;
	u8 nt;
	u8 regd;
	u8 reg_6ghz_power;
	u8 ch_idx;
	s8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_tx_shape_lmt_entry {
	u8 band;
	u8 tx_shape_rs;
	u8 regd;
	u8 v;
} __packed;

/* must consider compatibility; don't insert new in the mid */
struct rtw89_fw_tx_shape_lmt_ru_entry {
	u8 band;
	u8 regd;
	u8 v;
} __packed;

const struct rtw89_rfe_parms *
rtw89_load_rfe_data_from_fw(struct rtw89_dev *rtwdev,
			    const struct rtw89_rfe_parms *init);

enum rtw89_wow_wakeup_ver {
	RTW89_WOW_REASON_V0,
	RTW89_WOW_REASON_V1,
	RTW89_WOW_REASON_NUM,
};

#endif
