/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTW_FW_H_
#define __RTW_FW_H_

#define H2C_PKT_SIZE		32
#define H2C_PKT_HDR_SIZE	8

/* FW bin information */
#define FW_HDR_SIZE			64
#define FW_HDR_CHKSUM_SIZE		8

#define FW_NLO_INFO_CHECK_SIZE		4

#define FIFO_PAGE_SIZE_SHIFT		12
#define FIFO_PAGE_SIZE			4096
#define RSVD_PAGE_START_ADDR		0x780
#define FIFO_DUMP_ADDR			0x8000

enum rtw_c2h_cmd_id {
	C2H_BT_INFO = 0x09,
	C2H_BT_MP_INFO = 0x0b,
	C2H_RA_RPT = 0x0c,
	C2H_HW_FEATURE_REPORT = 0x19,
	C2H_WLAN_INFO = 0x27,
	C2H_HW_FEATURE_DUMP = 0xfd,
	C2H_HALMAC = 0xff,
};

enum rtw_c2h_cmd_id_ext {
	C2H_CCX_RPT = 0x0f,
};

struct rtw_c2h_cmd {
	u8 id;
	u8 seq;
	u8 payload[0];
} __packed;

enum rtw_rsvd_packet_type {
	RSVD_BEACON,
	RSVD_PS_POLL,
	RSVD_PROBE_RESP,
	RSVD_NULL,
	RSVD_QOS_NULL,
	RSVD_LPS_PG_DPK,
	RSVD_LPS_PG_INFO,
	RSVD_PROBE_REQ,
	RSVD_NLO_INFO,
	RSVD_CH_INFO,
};

enum rtw_fw_rf_type {
	FW_RF_1T2R = 0,
	FW_RF_2T4R = 1,
	FW_RF_2T2R = 2,
	FW_RF_2T3R = 3,
	FW_RF_1T1R = 4,
	FW_RF_2T2R_GREEN = 5,
	FW_RF_3T3R = 6,
	FW_RF_3T4R = 7,
	FW_RF_4T4R = 8,
	FW_RF_MAX_TYPE = 0xF,
};

struct rtw_coex_info_req {
	u8 seq;
	u8 op_code;
	u8 para1;
	u8 para2;
	u8 para3;
};

struct rtw_iqk_para {
	u8 clear;
	u8 segment_iqk;
};

struct rtw_lps_pg_dpk_hdr {
	u16 dpk_path_ok;
	u8 dpk_txagc[2];
	u16 dpk_gs[2];
	u32 coef[2][20];
	u8 dpk_ch;
} __packed;

struct rtw_lps_pg_info_hdr {
	u8 macid;
	u8 mbssid;
	u8 pattern_count;
	u8 mu_tab_group_id;
	u8 sec_cam_count;
	u8 tx_bu_page_count;
	u16 rsvd;
	u8 sec_cam[MAX_PG_CAM_BACKUP_NUM];
} __packed;

struct rtw_rsvd_page {
	struct list_head list;
	struct sk_buff *skb;
	enum rtw_rsvd_packet_type type;
	u8 page;
	bool add_txdesc;
	struct cfg80211_ssid *ssid;
};

enum rtw_keep_alive_pkt_type {
	KEEP_ALIVE_NULL_PKT = 0,
	KEEP_ALIVE_ARP_RSP = 1,
};

struct rtw_nlo_info_hdr {
	u8 nlo_count;
	u8 hidden_ap_count;
	u8 rsvd1[2];
	u8 pattern_check[FW_NLO_INFO_CHECK_SIZE];
	u8 rsvd2[8];
	u8 ssid_len[16];
	u8 chiper[16];
	u8 rsvd3[16];
	u8 location[8];
} __packed;

enum rtw_packet_type {
	RTW_PACKET_PROBE_REQ = 0x00,

	RTW_PACKET_UNDEFINE = 0x7FFFFFFF,
};

struct rtw_fw_wow_keep_alive_para {
	bool adopt;
	u8 pkt_type;
	u8 period;		/* unit: sec */
};

struct rtw_fw_wow_disconnect_para {
	bool adopt;
	u8 period;		/* unit: sec */
	u8 retry_count;
};

struct rtw_ch_switch_option {
	u8 periodic_option;
	u32 tsf_high;
	u32 tsf_low;
	u8 dest_ch_en;
	u8 absolute_time_en;
	u8 dest_ch;
	u8 normal_period;
	u8 normal_period_sel;
	u8 normal_cycle;
	u8 slow_period;
	u8 slow_period_sel;
	u8 nlo_en;
};

struct rtw_fw_hdr {
	__le16 signature;
	u8 category;
	u8 function;
	__le16 version;		/* 0x04 */
	u8 subversion;
	u8 subindex;
	__le32 rsvd;		/* 0x08 */
	__le32 rsvd2;		/* 0x0C */
	u8 month;		/* 0x10 */
	u8 day;
	u8 hour;
	u8 min;
	__le16 year;		/* 0x14 */
	__le16 rsvd3;
	u8 mem_usage;		/* 0x18 */
	u8 rsvd4[3];
	__le16 h2c_fmt_ver;	/* 0x1C */
	__le16 rsvd5;
	__le32 dmem_addr;	/* 0x20 */
	__le32 dmem_size;
	__le32 rsvd6;
	__le32 rsvd7;
	__le32 imem_size;	/* 0x30 */
	__le32 emem_size;
	__le32 emem_addr;
	__le32 imem_addr;
} __packed;

/* C2H */
#define GET_CCX_REPORT_SEQNUM(c2h_payload)	(c2h_payload[8] & 0xfc)
#define GET_CCX_REPORT_STATUS(c2h_payload)	(c2h_payload[9] & 0xc0)

#define GET_RA_REPORT_RATE(c2h_payload)		(c2h_payload[0] & 0x7f)
#define GET_RA_REPORT_SGI(c2h_payload)		((c2h_payload[0] & 0x80) >> 7)
#define GET_RA_REPORT_BW(c2h_payload)		(c2h_payload[6])
#define GET_RA_REPORT_MACID(c2h_payload)	(c2h_payload[1])

/* PKT H2C */
#define H2C_PKT_CMD_ID 0xFF
#define H2C_PKT_CATEGORY 0x01

#define H2C_PKT_GENERAL_INFO 0x0D
#define H2C_PKT_PHYDM_INFO 0x11
#define H2C_PKT_IQK 0x0E

#define H2C_PKT_CH_SWITCH 0x02
#define H2C_PKT_UPDATE_PKT 0x0C

#define H2C_PKT_CH_SWITCH_LEN 0x20
#define H2C_PKT_UPDATE_PKT_LEN 0x4

#define SET_PKT_H2C_CATEGORY(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(6, 0))
#define SET_PKT_H2C_CMD_ID(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_PKT_H2C_SUB_CMD_ID(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 16))
#define SET_PKT_H2C_TOTAL_LEN(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 0))

static inline void rtw_h2c_pkt_set_header(u8 *h2c_pkt, u8 sub_id)
{
	SET_PKT_H2C_CATEGORY(h2c_pkt, H2C_PKT_CATEGORY);
	SET_PKT_H2C_CMD_ID(h2c_pkt, H2C_PKT_CMD_ID);
	SET_PKT_H2C_SUB_CMD_ID(h2c_pkt, sub_id);
}

#define FW_OFFLOAD_H2C_SET_SEQ_NUM(h2c_pkt, value)                             \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(31, 16))
#define GENERAL_INFO_SET_FW_TX_BOUNDARY(h2c_pkt, value)                        \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))

#define PHYDM_INFO_SET_REF_TYPE(h2c_pkt, value)                                \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(7, 0))
#define PHYDM_INFO_SET_RF_TYPE(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 8))
#define PHYDM_INFO_SET_CUT_VER(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define PHYDM_INFO_SET_RX_ANT_STATUS(h2c_pkt, value)                           \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(27, 24))
#define PHYDM_INFO_SET_TX_ANT_STATUS(h2c_pkt, value)                           \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(31, 28))
#define IQK_SET_CLEAR(h2c_pkt, value)                                          \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(0))
#define IQK_SET_SEGMENT_IQK(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(1))

#define CHSW_INFO_SET_CH(pkt, value)					       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(7, 0))
#define CHSW_INFO_SET_PRI_CH_IDX(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(11, 8))
#define CHSW_INFO_SET_BW(pkt, value)					       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(15, 12))
#define CHSW_INFO_SET_TIMEOUT(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(23, 16))
#define CHSW_INFO_SET_ACTION_ID(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(30, 24))

#define UPDATE_PKT_SET_SIZE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 0))
#define UPDATE_PKT_SET_PKT_ID(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define UPDATE_PKT_SET_LOCATION(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(31, 24))

#define CH_SWITCH_SET_START(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(0))
#define CH_SWITCH_SET_DEST_CH_EN(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(1))
#define CH_SWITCH_SET_ABSOLUTE_TIME(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(2))
#define CH_SWITCH_SET_PERIODIC_OPT(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(4, 3))
#define CH_SWITCH_SET_INFO_LOC(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 8))
#define CH_SWITCH_SET_CH_NUM(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define CH_SWITCH_SET_PRI_CH_IDX(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(27, 24))
#define CH_SWITCH_SET_DEST_CH(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(7, 0))
#define CH_SWITCH_SET_NORMAL_PERIOD(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(13, 8))
#define CH_SWITCH_SET_NORMAL_PERIOD_SEL(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(15, 14))
#define CH_SWITCH_SET_SLOW_PERIOD(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(21, 16))
#define CH_SWITCH_SET_SLOW_PERIOD_SEL(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(23, 22))
#define CH_SWITCH_SET_NORMAL_CYCLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(31, 24))
#define CH_SWITCH_SET_TSF_HIGH(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x04, value, GENMASK(31, 0))
#define CH_SWITCH_SET_TSF_LOW(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x05, value, GENMASK(31, 0))
#define CH_SWITCH_SET_INFO_SIZE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x06, value, GENMASK(15, 0))

/* Command H2C */
#define H2C_CMD_RSVD_PAGE		0x0
#define H2C_CMD_MEDIA_STATUS_RPT	0x01
#define H2C_CMD_SET_PWR_MODE		0x20
#define H2C_CMD_LPS_PG_INFO		0x2b
#define H2C_CMD_RA_INFO			0x40
#define H2C_CMD_RSSI_MONITOR		0x42

#define H2C_CMD_COEX_TDMA_TYPE		0x60
#define H2C_CMD_QUERY_BT_INFO		0x61
#define H2C_CMD_FORCE_BT_TX_POWER	0x62
#define H2C_CMD_IGNORE_WLAN_ACTION	0x63
#define H2C_CMD_WL_CH_INFO		0x66
#define H2C_CMD_QUERY_BT_MP_INFO	0x67
#define H2C_CMD_BT_WIFI_CONTROL		0x69

#define H2C_CMD_KEEP_ALIVE		0x03
#define H2C_CMD_DISCONNECT_DECISION	0x04
#define H2C_CMD_WOWLAN			0x80
#define H2C_CMD_REMOTE_WAKE_CTRL	0x81
#define H2C_CMD_AOAC_GLOBAL_INFO	0x82
#define H2C_CMD_NLO_INFO		0x8C

#define SET_H2C_CMD_ID_CLASS(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(7, 0))

#define MEDIA_STATUS_RPT_SET_OP_MODE(h2c_pkt, value)                           \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define MEDIA_STATUS_RPT_SET_MACID(h2c_pkt, value)                             \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))

#define SET_PWR_MODE_SET_MODE(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(14, 8))
#define SET_PWR_MODE_SET_RLBM(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(19, 16))
#define SET_PWR_MODE_SET_SMART_PS(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 20))
#define SET_PWR_MODE_SET_AWAKE_INTERVAL(h2c_pkt, value)                        \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_PWR_MODE_SET_PORT_ID(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 5))
#define SET_PWR_MODE_SET_PWR_STATE(h2c_pkt, value)                             \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define LPS_PG_INFO_LOC(h2c_pkt, value)                                        \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define LPS_PG_DPK_LOC(h2c_pkt, value)                                         \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define LPS_PG_SEC_CAM_EN(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define LPS_PG_PATTERN_CAM_EN(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_RSSI_INFO_MACID(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_RSSI_INFO_RSSI(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_RSSI_INFO_STBC(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, BIT(1))
#define SET_RA_INFO_MACID(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_RA_INFO_RATE_ID(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(20, 16))
#define SET_RA_INFO_INIT_RA_LVL(h2c_pkt, value)                                \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(22, 21))
#define SET_RA_INFO_SGI_EN(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(23))
#define SET_RA_INFO_BW_MODE(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(25, 24))
#define SET_RA_INFO_LDPC(h2c_pkt, value)                                       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(26))
#define SET_RA_INFO_NO_UPDATE(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(27))
#define SET_RA_INFO_VHT_EN(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(29, 28))
#define SET_RA_INFO_DIS_PT(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(30))
#define SET_RA_INFO_RA_MASK0(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_RA_INFO_RA_MASK1(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_RA_INFO_RA_MASK2(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(23, 16))
#define SET_RA_INFO_RA_MASK3(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(31, 24))
#define SET_QUERY_BT_INFO(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_WL_CH_INFO_LINK(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_WL_CH_INFO_CHNL(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_WL_CH_INFO_BW(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BT_MP_INFO_SEQ(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 12))
#define SET_BT_MP_INFO_OP_CODE(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_BT_MP_INFO_PARA1(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BT_MP_INFO_PARA2(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_BT_MP_INFO_PARA3(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_BT_TX_POWER_INDEX(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_IGNORE_WLAN_ACTION_EN(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_COEX_TDMA_TYPE_PARA1(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_COEX_TDMA_TYPE_PARA2(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_COEX_TDMA_TYPE_PARA3(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_COEX_TDMA_TYPE_PARA4(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_COEX_TDMA_TYPE_PARA5(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_BT_WIFI_CONTROL_OP_CODE(h2c_pkt, value)                            \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_BT_WIFI_CONTROL_DATA1(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_BT_WIFI_CONTROL_DATA2(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BT_WIFI_CONTROL_DATA3(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_BT_WIFI_CONTROL_DATA4(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_BT_WIFI_CONTROL_DATA5(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(23, 16))

#define SET_KEEP_ALIVE_ENABLE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_KEEP_ALIVE_ADOPT(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_KEEP_ALIVE_PKT_TYPE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_KEEP_ALIVE_CHECK_PERIOD(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))

#define SET_DISCONNECT_DECISION_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_DISCONNECT_DECISION_ADOPT(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_DISCONNECT_DECISION_CHECK_PERIOD(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_DISCONNECT_DECISION_TRY_PKT_NUM(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))

#define SET_WOWLAN_FUNC_ENABLE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_WOWLAN_PATTERN_MATCH_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_WOWLAN_MAGIC_PKT_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_WOWLAN_UNICAST_PKT_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(11))
#define SET_WOWLAN_REKEY_WAKEUP_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(14))
#define SET_WOWLAN_DEAUTH_WAKEUP_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(15))

#define SET_REMOTE_WAKECTRL_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_REMOTE_WAKE_CTRL_NLO_OFFLOAD_EN(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(12))

#define SET_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))

#define SET_NLO_FUN_EN(h2c_pkt, value)                                         \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_NLO_PS_32K(h2c_pkt, value)                                         \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_NLO_IGNORE_SECURITY(h2c_pkt, value)                                \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_NLO_LOC_NLO_INFO(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))

static inline struct rtw_c2h_cmd *get_c2h_from_skb(struct sk_buff *skb)
{
	u32 pkt_offset;

	pkt_offset = *((u32 *)skb->cb);
	return (struct rtw_c2h_cmd *)(skb->data + pkt_offset);
}

void rtw_fw_c2h_cmd_rx_irqsafe(struct rtw_dev *rtwdev, u32 pkt_offset,
			       struct sk_buff *skb);
void rtw_fw_c2h_cmd_handle(struct rtw_dev *rtwdev, struct sk_buff *skb);
void rtw_fw_send_general_info(struct rtw_dev *rtwdev);
void rtw_fw_send_phydm_info(struct rtw_dev *rtwdev);

void rtw_fw_do_iqk(struct rtw_dev *rtwdev, struct rtw_iqk_para *para);
void rtw_fw_set_pwr_mode(struct rtw_dev *rtwdev);
void rtw_fw_set_pg_info(struct rtw_dev *rtwdev);
void rtw_fw_query_bt_info(struct rtw_dev *rtwdev);
void rtw_fw_wl_ch_info(struct rtw_dev *rtwdev, u8 link, u8 ch, u8 bw);
void rtw_fw_query_bt_mp_info(struct rtw_dev *rtwdev,
			     struct rtw_coex_info_req *req);
void rtw_fw_force_bt_tx_power(struct rtw_dev *rtwdev, u8 bt_pwr_dec_lvl);
void rtw_fw_bt_ignore_wlan_action(struct rtw_dev *rtwdev, bool enable);
void rtw_fw_coex_tdma_type(struct rtw_dev *rtwdev,
			   u8 para1, u8 para2, u8 para3, u8 para4, u8 para5);
void rtw_fw_bt_wifi_control(struct rtw_dev *rtwdev, u8 op_code, u8 *data);
void rtw_fw_send_rssi_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si);
void rtw_fw_send_ra_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si);
void rtw_fw_media_status_report(struct rtw_dev *rtwdev, u8 mac_id, bool conn);
void rtw_add_rsvd_page(struct rtw_dev *rtwdev, enum rtw_rsvd_packet_type type,
		       bool txdesc);
void rtw_add_rsvd_page_probe_req(struct rtw_dev *rtwdev,
				 struct cfg80211_ssid *ssid);
int rtw_fw_write_data_rsvd_page(struct rtw_dev *rtwdev, u16 pg_addr,
				u8 *buf, u32 size);
void rtw_reset_rsvd_page(struct rtw_dev *rtwdev);
int rtw_fw_download_rsvd_page(struct rtw_dev *rtwdev,
			      struct ieee80211_vif *vif);
void rtw_send_rsvd_page_h2c(struct rtw_dev *rtwdev);
int rtw_dump_drv_rsvd_page(struct rtw_dev *rtwdev,
			   u32 offset, u32 size, u32 *buf);
void rtw_fw_set_remote_wake_ctrl_cmd(struct rtw_dev *rtwdev, bool enable);
void rtw_fw_set_wowlan_ctrl_cmd(struct rtw_dev *rtwdev, bool enable);
void rtw_fw_set_keep_alive_cmd(struct rtw_dev *rtwdev, bool enable);
void rtw_fw_set_disconnect_decision_cmd(struct rtw_dev *rtwdev, bool enable);
void rtw_fw_set_aoac_global_info_cmd(struct rtw_dev *rtwdev,
				     u8 pairwise_key_enc,
				     u8 group_key_enc);

void rtw_fw_set_nlo_info(struct rtw_dev *rtwdev, bool enable);
void rtw_fw_update_pkt_probe_req(struct rtw_dev *rtwdev,
				 struct cfg80211_ssid *ssid);
void rtw_fw_channel_switch(struct rtw_dev *rtwdev, bool enable);
#endif
