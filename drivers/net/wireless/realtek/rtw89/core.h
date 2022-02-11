/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_CORE_H__
#define __RTW89_CORE_H__

#include <linux/average.h>
#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

struct rtw89_dev;

extern const struct ieee80211_ops rtw89_ops;

#define MASKBYTE0 0xff
#define MASKBYTE1 0xff00
#define MASKBYTE2 0xff0000
#define MASKBYTE3 0xff000000
#define MASKBYTE4 0xff00000000ULL
#define MASKHWORD 0xffff0000
#define MASKLWORD 0x0000ffff
#define MASKDWORD 0xffffffff
#define RFREG_MASK 0xfffff
#define INV_RF_DATA 0xffffffff

#define RTW89_TRACK_WORK_PERIOD	round_jiffies_relative(HZ * 2)
#define CFO_TRACK_MAX_USER 64
#define MAX_RSSI 110
#define RSSI_FACTOR 1
#define RTW89_RSSI_RAW_TO_DBM(rssi) ((s8)((rssi) >> RSSI_FACTOR) - MAX_RSSI)

#define RTW89_HTC_MASK_VARIANT GENMASK(1, 0)
#define RTW89_HTC_VARIANT_HE 3
#define RTW89_HTC_MASK_CTL_ID GENMASK(5, 2)
#define RTW89_HTC_VARIANT_HE_CID_OM 1
#define RTW89_HTC_VARIANT_HE_CID_CAS 6
#define RTW89_HTC_MASK_CTL_INFO GENMASK(31, 6)

#define RTW89_HTC_MASK_HTC_OM_RX_NSS GENMASK(8, 6)
enum htc_om_channel_width {
	HTC_OM_CHANNEL_WIDTH_20 = 0,
	HTC_OM_CHANNEL_WIDTH_40 = 1,
	HTC_OM_CHANNEL_WIDTH_80 = 2,
	HTC_OM_CHANNEL_WIDTH_160_OR_80_80 = 3,
};
#define RTW89_HTC_MASK_HTC_OM_CH_WIDTH GENMASK(10, 9)
#define RTW89_HTC_MASK_HTC_OM_UL_MU_DIS BIT(11)
#define RTW89_HTC_MASK_HTC_OM_TX_NSTS GENMASK(14, 12)
#define RTW89_HTC_MASK_HTC_OM_ER_SU_DIS BIT(15)
#define RTW89_HTC_MASK_HTC_OM_DL_MU_MIMO_RR BIT(16)
#define RTW89_HTC_MASK_HTC_OM_UL_MU_DATA_DIS BIT(17)

enum rtw89_subband {
	RTW89_CH_2G = 0,
	RTW89_CH_5G_BAND_1 = 1,
	/* RTW89_CH_5G_BAND_2 = 2, unused */
	RTW89_CH_5G_BAND_3 = 3,
	RTW89_CH_5G_BAND_4 = 4,

	RTW89_CH_6G_BAND_IDX0, /* Low */
	RTW89_CH_6G_BAND_IDX1, /* Low */
	RTW89_CH_6G_BAND_IDX2, /* Mid */
	RTW89_CH_6G_BAND_IDX3, /* Mid */
	RTW89_CH_6G_BAND_IDX4, /* High */
	RTW89_CH_6G_BAND_IDX5, /* High */
	RTW89_CH_6G_BAND_IDX6, /* Ultra-high */
	RTW89_CH_6G_BAND_IDX7, /* Ultra-high */

	RTW89_SUBBAND_NR,
};

enum rtw89_hci_type {
	RTW89_HCI_TYPE_PCIE,
	RTW89_HCI_TYPE_USB,
	RTW89_HCI_TYPE_SDIO,
};

enum rtw89_core_chip_id {
	RTL8852A,
	RTL8852B,
	RTL8852C,
};

enum rtw89_cv {
	CHIP_CAV,
	CHIP_CBV,
	CHIP_CCV,
	CHIP_CDV,
	CHIP_CEV,
	CHIP_CFV,
	CHIP_CV_MAX,
	CHIP_CV_INVALID = CHIP_CV_MAX,
};

enum rtw89_core_tx_type {
	RTW89_CORE_TX_TYPE_DATA,
	RTW89_CORE_TX_TYPE_MGMT,
	RTW89_CORE_TX_TYPE_FWCMD,
};

enum rtw89_core_rx_type {
	RTW89_CORE_RX_TYPE_WIFI		= 0,
	RTW89_CORE_RX_TYPE_PPDU_STAT	= 1,
	RTW89_CORE_RX_TYPE_CHAN_INFO	= 2,
	RTW89_CORE_RX_TYPE_BB_SCOPE	= 3,
	RTW89_CORE_RX_TYPE_F2P_TXCMD	= 4,
	RTW89_CORE_RX_TYPE_SS2FW	= 5,
	RTW89_CORE_RX_TYPE_TX_REPORT	= 6,
	RTW89_CORE_RX_TYPE_TX_REL_HOST	= 7,
	RTW89_CORE_RX_TYPE_DFS_REPORT	= 8,
	RTW89_CORE_RX_TYPE_TX_REL_CPU	= 9,
	RTW89_CORE_RX_TYPE_C2H		= 10,
	RTW89_CORE_RX_TYPE_CSI		= 11,
	RTW89_CORE_RX_TYPE_CQI		= 12,
};

enum rtw89_txq_flags {
	RTW89_TXQ_F_AMPDU		= 0,
	RTW89_TXQ_F_BLOCK_BA		= 1,
};

enum rtw89_net_type {
	RTW89_NET_TYPE_NO_LINK		= 0,
	RTW89_NET_TYPE_AD_HOC		= 1,
	RTW89_NET_TYPE_INFRA		= 2,
	RTW89_NET_TYPE_AP_MODE		= 3,
};

enum rtw89_wifi_role {
	RTW89_WIFI_ROLE_NONE,
	RTW89_WIFI_ROLE_STATION,
	RTW89_WIFI_ROLE_AP,
	RTW89_WIFI_ROLE_AP_VLAN,
	RTW89_WIFI_ROLE_ADHOC,
	RTW89_WIFI_ROLE_ADHOC_MASTER,
	RTW89_WIFI_ROLE_MESH_POINT,
	RTW89_WIFI_ROLE_MONITOR,
	RTW89_WIFI_ROLE_P2P_DEVICE,
	RTW89_WIFI_ROLE_P2P_CLIENT,
	RTW89_WIFI_ROLE_P2P_GO,
	RTW89_WIFI_ROLE_NAN,
	RTW89_WIFI_ROLE_MLME_MAX
};

enum rtw89_upd_mode {
	RTW89_ROLE_CREATE,
	RTW89_ROLE_REMOVE,
	RTW89_ROLE_TYPE_CHANGE,
	RTW89_ROLE_INFO_CHANGE,
	RTW89_ROLE_CON_DISCONN
};

enum rtw89_self_role {
	RTW89_SELF_ROLE_CLIENT,
	RTW89_SELF_ROLE_AP,
	RTW89_SELF_ROLE_AP_CLIENT
};

enum rtw89_msk_sO_el {
	RTW89_NO_MSK,
	RTW89_SMA,
	RTW89_TMA,
	RTW89_BSSID
};

enum rtw89_sch_tx_sel {
	RTW89_SCH_TX_SEL_ALL,
	RTW89_SCH_TX_SEL_HIQ,
	RTW89_SCH_TX_SEL_MG0,
	RTW89_SCH_TX_SEL_MACID,
};

/* RTW89_ADDR_CAM_SEC_NONE	: not enabled
 * RTW89_ADDR_CAM_SEC_ALL_UNI	: 0 - 6 unicast
 * RTW89_ADDR_CAM_SEC_NORMAL	: 0 - 1 unicast, 2 - 4 group, 5 - 6 BIP
 * RTW89_ADDR_CAM_SEC_4GROUP	: 0 - 1 unicast, 2 - 5 group, 6 BIP
 */
enum rtw89_add_cam_sec_mode {
	RTW89_ADDR_CAM_SEC_NONE		= 0,
	RTW89_ADDR_CAM_SEC_ALL_UNI	= 1,
	RTW89_ADDR_CAM_SEC_NORMAL	= 2,
	RTW89_ADDR_CAM_SEC_4GROUP	= 3,
};

enum rtw89_sec_key_type {
	RTW89_SEC_KEY_TYPE_NONE		= 0,
	RTW89_SEC_KEY_TYPE_WEP40	= 1,
	RTW89_SEC_KEY_TYPE_WEP104	= 2,
	RTW89_SEC_KEY_TYPE_TKIP		= 3,
	RTW89_SEC_KEY_TYPE_WAPI		= 4,
	RTW89_SEC_KEY_TYPE_GCMSMS4	= 5,
	RTW89_SEC_KEY_TYPE_CCMP128	= 6,
	RTW89_SEC_KEY_TYPE_CCMP256	= 7,
	RTW89_SEC_KEY_TYPE_GCMP128	= 8,
	RTW89_SEC_KEY_TYPE_GCMP256	= 9,
	RTW89_SEC_KEY_TYPE_BIP_CCMP128	= 10,
};

enum rtw89_port {
	RTW89_PORT_0 = 0,
	RTW89_PORT_1 = 1,
	RTW89_PORT_2 = 2,
	RTW89_PORT_3 = 3,
	RTW89_PORT_4 = 4,
	RTW89_PORT_NUM
};

enum rtw89_band {
	RTW89_BAND_2G = 0,
	RTW89_BAND_5G = 1,
	RTW89_BAND_6G = 2,
	RTW89_BAND_MAX,
};

enum rtw89_hw_rate {
	RTW89_HW_RATE_CCK1	= 0x0,
	RTW89_HW_RATE_CCK2	= 0x1,
	RTW89_HW_RATE_CCK5_5	= 0x2,
	RTW89_HW_RATE_CCK11	= 0x3,
	RTW89_HW_RATE_OFDM6	= 0x4,
	RTW89_HW_RATE_OFDM9	= 0x5,
	RTW89_HW_RATE_OFDM12	= 0x6,
	RTW89_HW_RATE_OFDM18	= 0x7,
	RTW89_HW_RATE_OFDM24	= 0x8,
	RTW89_HW_RATE_OFDM36	= 0x9,
	RTW89_HW_RATE_OFDM48	= 0xA,
	RTW89_HW_RATE_OFDM54	= 0xB,
	RTW89_HW_RATE_MCS0	= 0x80,
	RTW89_HW_RATE_MCS1	= 0x81,
	RTW89_HW_RATE_MCS2	= 0x82,
	RTW89_HW_RATE_MCS3	= 0x83,
	RTW89_HW_RATE_MCS4	= 0x84,
	RTW89_HW_RATE_MCS5	= 0x85,
	RTW89_HW_RATE_MCS6	= 0x86,
	RTW89_HW_RATE_MCS7	= 0x87,
	RTW89_HW_RATE_MCS8	= 0x88,
	RTW89_HW_RATE_MCS9	= 0x89,
	RTW89_HW_RATE_MCS10	= 0x8A,
	RTW89_HW_RATE_MCS11	= 0x8B,
	RTW89_HW_RATE_MCS12	= 0x8C,
	RTW89_HW_RATE_MCS13	= 0x8D,
	RTW89_HW_RATE_MCS14	= 0x8E,
	RTW89_HW_RATE_MCS15	= 0x8F,
	RTW89_HW_RATE_MCS16	= 0x90,
	RTW89_HW_RATE_MCS17	= 0x91,
	RTW89_HW_RATE_MCS18	= 0x92,
	RTW89_HW_RATE_MCS19	= 0x93,
	RTW89_HW_RATE_MCS20	= 0x94,
	RTW89_HW_RATE_MCS21	= 0x95,
	RTW89_HW_RATE_MCS22	= 0x96,
	RTW89_HW_RATE_MCS23	= 0x97,
	RTW89_HW_RATE_MCS24	= 0x98,
	RTW89_HW_RATE_MCS25	= 0x99,
	RTW89_HW_RATE_MCS26	= 0x9A,
	RTW89_HW_RATE_MCS27	= 0x9B,
	RTW89_HW_RATE_MCS28	= 0x9C,
	RTW89_HW_RATE_MCS29	= 0x9D,
	RTW89_HW_RATE_MCS30	= 0x9E,
	RTW89_HW_RATE_MCS31	= 0x9F,
	RTW89_HW_RATE_VHT_NSS1_MCS0	= 0x100,
	RTW89_HW_RATE_VHT_NSS1_MCS1	= 0x101,
	RTW89_HW_RATE_VHT_NSS1_MCS2	= 0x102,
	RTW89_HW_RATE_VHT_NSS1_MCS3	= 0x103,
	RTW89_HW_RATE_VHT_NSS1_MCS4	= 0x104,
	RTW89_HW_RATE_VHT_NSS1_MCS5	= 0x105,
	RTW89_HW_RATE_VHT_NSS1_MCS6	= 0x106,
	RTW89_HW_RATE_VHT_NSS1_MCS7	= 0x107,
	RTW89_HW_RATE_VHT_NSS1_MCS8	= 0x108,
	RTW89_HW_RATE_VHT_NSS1_MCS9	= 0x109,
	RTW89_HW_RATE_VHT_NSS2_MCS0	= 0x110,
	RTW89_HW_RATE_VHT_NSS2_MCS1	= 0x111,
	RTW89_HW_RATE_VHT_NSS2_MCS2	= 0x112,
	RTW89_HW_RATE_VHT_NSS2_MCS3	= 0x113,
	RTW89_HW_RATE_VHT_NSS2_MCS4	= 0x114,
	RTW89_HW_RATE_VHT_NSS2_MCS5	= 0x115,
	RTW89_HW_RATE_VHT_NSS2_MCS6	= 0x116,
	RTW89_HW_RATE_VHT_NSS2_MCS7	= 0x117,
	RTW89_HW_RATE_VHT_NSS2_MCS8	= 0x118,
	RTW89_HW_RATE_VHT_NSS2_MCS9	= 0x119,
	RTW89_HW_RATE_VHT_NSS3_MCS0	= 0x120,
	RTW89_HW_RATE_VHT_NSS3_MCS1	= 0x121,
	RTW89_HW_RATE_VHT_NSS3_MCS2	= 0x122,
	RTW89_HW_RATE_VHT_NSS3_MCS3	= 0x123,
	RTW89_HW_RATE_VHT_NSS3_MCS4	= 0x124,
	RTW89_HW_RATE_VHT_NSS3_MCS5	= 0x125,
	RTW89_HW_RATE_VHT_NSS3_MCS6	= 0x126,
	RTW89_HW_RATE_VHT_NSS3_MCS7	= 0x127,
	RTW89_HW_RATE_VHT_NSS3_MCS8	= 0x128,
	RTW89_HW_RATE_VHT_NSS3_MCS9	= 0x129,
	RTW89_HW_RATE_VHT_NSS4_MCS0	= 0x130,
	RTW89_HW_RATE_VHT_NSS4_MCS1	= 0x131,
	RTW89_HW_RATE_VHT_NSS4_MCS2	= 0x132,
	RTW89_HW_RATE_VHT_NSS4_MCS3	= 0x133,
	RTW89_HW_RATE_VHT_NSS4_MCS4	= 0x134,
	RTW89_HW_RATE_VHT_NSS4_MCS5	= 0x135,
	RTW89_HW_RATE_VHT_NSS4_MCS6	= 0x136,
	RTW89_HW_RATE_VHT_NSS4_MCS7	= 0x137,
	RTW89_HW_RATE_VHT_NSS4_MCS8	= 0x138,
	RTW89_HW_RATE_VHT_NSS4_MCS9	= 0x139,
	RTW89_HW_RATE_HE_NSS1_MCS0	= 0x180,
	RTW89_HW_RATE_HE_NSS1_MCS1	= 0x181,
	RTW89_HW_RATE_HE_NSS1_MCS2	= 0x182,
	RTW89_HW_RATE_HE_NSS1_MCS3	= 0x183,
	RTW89_HW_RATE_HE_NSS1_MCS4	= 0x184,
	RTW89_HW_RATE_HE_NSS1_MCS5	= 0x185,
	RTW89_HW_RATE_HE_NSS1_MCS6	= 0x186,
	RTW89_HW_RATE_HE_NSS1_MCS7	= 0x187,
	RTW89_HW_RATE_HE_NSS1_MCS8	= 0x188,
	RTW89_HW_RATE_HE_NSS1_MCS9	= 0x189,
	RTW89_HW_RATE_HE_NSS1_MCS10	= 0x18A,
	RTW89_HW_RATE_HE_NSS1_MCS11	= 0x18B,
	RTW89_HW_RATE_HE_NSS2_MCS0	= 0x190,
	RTW89_HW_RATE_HE_NSS2_MCS1	= 0x191,
	RTW89_HW_RATE_HE_NSS2_MCS2	= 0x192,
	RTW89_HW_RATE_HE_NSS2_MCS3	= 0x193,
	RTW89_HW_RATE_HE_NSS2_MCS4	= 0x194,
	RTW89_HW_RATE_HE_NSS2_MCS5	= 0x195,
	RTW89_HW_RATE_HE_NSS2_MCS6	= 0x196,
	RTW89_HW_RATE_HE_NSS2_MCS7	= 0x197,
	RTW89_HW_RATE_HE_NSS2_MCS8	= 0x198,
	RTW89_HW_RATE_HE_NSS2_MCS9	= 0x199,
	RTW89_HW_RATE_HE_NSS2_MCS10	= 0x19A,
	RTW89_HW_RATE_HE_NSS2_MCS11	= 0x19B,
	RTW89_HW_RATE_HE_NSS3_MCS0	= 0x1A0,
	RTW89_HW_RATE_HE_NSS3_MCS1	= 0x1A1,
	RTW89_HW_RATE_HE_NSS3_MCS2	= 0x1A2,
	RTW89_HW_RATE_HE_NSS3_MCS3	= 0x1A3,
	RTW89_HW_RATE_HE_NSS3_MCS4	= 0x1A4,
	RTW89_HW_RATE_HE_NSS3_MCS5	= 0x1A5,
	RTW89_HW_RATE_HE_NSS3_MCS6	= 0x1A6,
	RTW89_HW_RATE_HE_NSS3_MCS7	= 0x1A7,
	RTW89_HW_RATE_HE_NSS3_MCS8	= 0x1A8,
	RTW89_HW_RATE_HE_NSS3_MCS9	= 0x1A9,
	RTW89_HW_RATE_HE_NSS3_MCS10	= 0x1AA,
	RTW89_HW_RATE_HE_NSS3_MCS11	= 0x1AB,
	RTW89_HW_RATE_HE_NSS4_MCS0	= 0x1B0,
	RTW89_HW_RATE_HE_NSS4_MCS1	= 0x1B1,
	RTW89_HW_RATE_HE_NSS4_MCS2	= 0x1B2,
	RTW89_HW_RATE_HE_NSS4_MCS3	= 0x1B3,
	RTW89_HW_RATE_HE_NSS4_MCS4	= 0x1B4,
	RTW89_HW_RATE_HE_NSS4_MCS5	= 0x1B5,
	RTW89_HW_RATE_HE_NSS4_MCS6	= 0x1B6,
	RTW89_HW_RATE_HE_NSS4_MCS7	= 0x1B7,
	RTW89_HW_RATE_HE_NSS4_MCS8	= 0x1B8,
	RTW89_HW_RATE_HE_NSS4_MCS9	= 0x1B9,
	RTW89_HW_RATE_HE_NSS4_MCS10	= 0x1BA,
	RTW89_HW_RATE_HE_NSS4_MCS11	= 0x1BB,
	RTW89_HW_RATE_NR,

	RTW89_HW_RATE_MASK_MOD = GENMASK(8, 7),
	RTW89_HW_RATE_MASK_VAL = GENMASK(6, 0),
};

/* 2G channels,
 * 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
 */
#define RTW89_2G_CH_NUM 14

/* 5G channels,
 * 36, 38, 40, 42, 44, 46, 48, 50,
 * 52, 54, 56, 58, 60, 62, 64,
 * 100, 102, 104, 106, 108, 110, 112, 114,
 * 116, 118, 120, 122, 124, 126, 128, 130,
 * 132, 134, 136, 138, 140, 142, 144,
 * 149, 151, 153, 155, 157, 159, 161, 163,
 * 165, 167, 169, 171, 173, 175, 177
 */
#define RTW89_5G_CH_NUM 53

enum rtw89_rate_section {
	RTW89_RS_CCK,
	RTW89_RS_OFDM,
	RTW89_RS_MCS, /* for HT/VHT/HE */
	RTW89_RS_HEDCM,
	RTW89_RS_OFFSET,
	RTW89_RS_MAX,
	RTW89_RS_LMT_NUM = RTW89_RS_MCS + 1,
};

enum rtw89_rate_max {
	RTW89_RATE_CCK_MAX	= 4,
	RTW89_RATE_OFDM_MAX	= 8,
	RTW89_RATE_MCS_MAX	= 12,
	RTW89_RATE_HEDCM_MAX	= 4, /* for HEDCM MCS0/1/3/4 */
	RTW89_RATE_OFFSET_MAX	= 5, /* for HE(HEDCM)/VHT/HT/OFDM/CCK offset */
};

enum rtw89_nss {
	RTW89_NSS_1		= 0,
	RTW89_NSS_2		= 1,
	/* HE DCM only support 1ss and 2ss */
	RTW89_NSS_HEDCM_MAX	= RTW89_NSS_2 + 1,
	RTW89_NSS_3		= 2,
	RTW89_NSS_4		= 3,
	RTW89_NSS_MAX,
};

enum rtw89_ntx {
	RTW89_1TX	= 0,
	RTW89_2TX	= 1,
	RTW89_NTX_NUM,
};

enum rtw89_beamforming_type {
	RTW89_NONBF	= 0,
	RTW89_BF	= 1,
	RTW89_BF_NUM,
};

enum rtw89_regulation_type {
	RTW89_WW	= 0,
	RTW89_ETSI	= 1,
	RTW89_FCC	= 2,
	RTW89_MKK	= 3,
	RTW89_NA	= 4,
	RTW89_IC	= 5,
	RTW89_KCC	= 6,
	RTW89_ACMA	= 7,
	RTW89_NCC	= 8,
	RTW89_MEXICO	= 9,
	RTW89_CHILE	= 10,
	RTW89_UKRAINE	= 11,
	RTW89_CN	= 12,
	RTW89_QATAR	= 13,
	RTW89_REGD_NUM,
};

struct rtw89_txpwr_byrate {
	s8 cck[RTW89_RATE_CCK_MAX];
	s8 ofdm[RTW89_RATE_OFDM_MAX];
	s8 mcs[RTW89_NSS_MAX][RTW89_RATE_MCS_MAX];
	s8 hedcm[RTW89_NSS_HEDCM_MAX][RTW89_RATE_HEDCM_MAX];
	s8 offset[RTW89_RATE_OFFSET_MAX];
};

enum rtw89_bandwidth_section_num {
	RTW89_BW20_SEC_NUM = 8,
	RTW89_BW40_SEC_NUM = 4,
	RTW89_BW80_SEC_NUM = 2,
};

struct rtw89_txpwr_limit {
	s8 cck_20m[RTW89_BF_NUM];
	s8 cck_40m[RTW89_BF_NUM];
	s8 ofdm[RTW89_BF_NUM];
	s8 mcs_20m[RTW89_BW20_SEC_NUM][RTW89_BF_NUM];
	s8 mcs_40m[RTW89_BW40_SEC_NUM][RTW89_BF_NUM];
	s8 mcs_80m[RTW89_BW80_SEC_NUM][RTW89_BF_NUM];
	s8 mcs_160m[RTW89_BF_NUM];
	s8 mcs_40m_0p5[RTW89_BF_NUM];
	s8 mcs_40m_2p5[RTW89_BF_NUM];
};

#define RTW89_RU_SEC_NUM 8

struct rtw89_txpwr_limit_ru {
	s8 ru26[RTW89_RU_SEC_NUM];
	s8 ru52[RTW89_RU_SEC_NUM];
	s8 ru106[RTW89_RU_SEC_NUM];
};

struct rtw89_rate_desc {
	enum rtw89_nss nss;
	enum rtw89_rate_section rs;
	u8 idx;
};

#define PHY_STS_HDR_LEN 8
#define RF_PATH_MAX 4
#define RTW89_MAX_PPDU_CNT 8
struct rtw89_rx_phy_ppdu {
	u8 *buf;
	u32 len;
	u8 rssi_avg;
	s8 rssi[RF_PATH_MAX];
	u8 mac_id;
	u8 chan_idx;
	u8 ie;
	u16 rate;
	bool to_self;
	bool valid;
};

enum rtw89_mac_idx {
	RTW89_MAC_0 = 0,
	RTW89_MAC_1 = 1,
};

enum rtw89_phy_idx {
	RTW89_PHY_0 = 0,
	RTW89_PHY_1 = 1,
	RTW89_PHY_MAX
};

enum rtw89_rf_path {
	RF_PATH_A = 0,
	RF_PATH_B = 1,
	RF_PATH_C = 2,
	RF_PATH_D = 3,
	RF_PATH_AB,
	RF_PATH_AC,
	RF_PATH_AD,
	RF_PATH_BC,
	RF_PATH_BD,
	RF_PATH_CD,
	RF_PATH_ABC,
	RF_PATH_ABD,
	RF_PATH_ACD,
	RF_PATH_BCD,
	RF_PATH_ABCD,
};

enum rtw89_rf_path_bit {
	RF_A	= BIT(0),
	RF_B	= BIT(1),
	RF_C	= BIT(2),
	RF_D	= BIT(3),

	RF_AB	= (RF_A | RF_B),
	RF_AC	= (RF_A | RF_C),
	RF_AD	= (RF_A | RF_D),
	RF_BC	= (RF_B | RF_C),
	RF_BD	= (RF_B | RF_D),
	RF_CD	= (RF_C | RF_D),

	RF_ABC	= (RF_A | RF_B | RF_C),
	RF_ABD	= (RF_A | RF_B | RF_D),
	RF_ACD	= (RF_A | RF_C | RF_D),
	RF_BCD	= (RF_B | RF_C | RF_D),

	RF_ABCD	= (RF_A | RF_B | RF_C | RF_D),
};

enum rtw89_bandwidth {
	RTW89_CHANNEL_WIDTH_20	= 0,
	RTW89_CHANNEL_WIDTH_40	= 1,
	RTW89_CHANNEL_WIDTH_80	= 2,
	RTW89_CHANNEL_WIDTH_160	= 3,
	RTW89_CHANNEL_WIDTH_80_80	= 4,
	RTW89_CHANNEL_WIDTH_5	= 5,
	RTW89_CHANNEL_WIDTH_10	= 6,
};

enum rtw89_ps_mode {
	RTW89_PS_MODE_NONE	= 0,
	RTW89_PS_MODE_RFOFF	= 1,
	RTW89_PS_MODE_CLK_GATED	= 2,
	RTW89_PS_MODE_PWR_GATED	= 3,
};

#define RTW89_2G_BW_NUM (RTW89_CHANNEL_WIDTH_40 + 1)
#define RTW89_5G_BW_NUM (RTW89_CHANNEL_WIDTH_80 + 1)
#define RTW89_PPE_BW_NUM (RTW89_CHANNEL_WIDTH_80 + 1)

enum rtw89_ru_bandwidth {
	RTW89_RU26 = 0,
	RTW89_RU52 = 1,
	RTW89_RU106 = 2,
	RTW89_RU_NUM,
};

enum rtw89_sc_offset {
	RTW89_SC_DONT_CARE	= 0,
	RTW89_SC_20_UPPER	= 1,
	RTW89_SC_20_LOWER	= 2,
	RTW89_SC_20_UPMOST	= 3,
	RTW89_SC_20_LOWEST	= 4,
	RTW89_SC_40_UPPER	= 9,
	RTW89_SC_40_LOWER	= 10,
};

struct rtw89_channel_params {
	u8 center_chan;
	u8 primary_chan;
	u8 bandwidth;
	u8 pri_ch_idx;
	u8 band_type;
	u8 subband_type;
};

struct rtw89_channel_help_params {
	u16 tx_en;
};

struct rtw89_port_reg {
	u32 port_cfg;
	u32 tbtt_prohib;
	u32 bcn_area;
	u32 bcn_early;
	u32 tbtt_early;
	u32 tbtt_agg;
	u32 bcn_space;
	u32 bcn_forcetx;
	u32 bcn_err_cnt;
	u32 bcn_err_flag;
	u32 dtim_ctrl;
	u32 tbtt_shift;
	u32 bcn_cnt_tmr;
	u32 tsftr_l;
	u32 tsftr_h;
};

struct rtw89_txwd_body {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
} __packed;

struct rtw89_txwd_info {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
} __packed;

struct rtw89_rx_desc_info {
	u16 pkt_size;
	u8 pkt_type;
	u8 drv_info_size;
	u8 shift;
	u8 wl_hd_iv_len;
	bool long_rxdesc;
	bool bb_sel;
	bool mac_info_valid;
	u16 data_rate;
	u8 gi_ltf;
	u8 bw;
	u32 free_run_cnt;
	u8 user_id;
	bool sr_en;
	u8 ppdu_cnt;
	u8 ppdu_type;
	bool icv_err;
	bool crc32_err;
	bool hw_dec;
	bool sw_dec;
	bool addr1_match;
	u8 frag;
	u16 seq;
	u8 frame_type;
	u8 rx_pl_id;
	bool addr_cam_valid;
	u8 addr_cam_id;
	u8 sec_cam_id;
	u8 mac_id;
	u16 offset;
	bool ready;
};

struct rtw89_rxdesc_short {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
} __packed;

struct rtw89_rxdesc_long {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
	__le32 dword6;
	__le32 dword7;
} __packed;

struct rtw89_tx_desc_info {
	u16 pkt_size;
	u8 wp_offset;
	u8 mac_id;
	u8 qsel;
	u8 ch_dma;
	u8 hdr_llc_len;
	bool is_bmc;
	bool en_wd_info;
	bool wd_page;
	bool use_rate;
	bool dis_data_fb;
	bool tid_indicate;
	bool agg_en;
	bool bk;
	u8 ampdu_density;
	u8 ampdu_num;
	bool sec_en;
	u8 sec_type;
	u8 sec_cam_idx;
	u16 data_rate;
	u16 data_retry_lowest_rate;
	bool fw_dl;
	u16 seq;
	bool a_ctrl_bsr;
	u8 hw_ssn_sel;
#define RTW89_MGMT_HW_SSN_SEL	1
	u8 hw_seq_mode;
#define RTW89_MGMT_HW_SEQ_MODE	1
	bool hiq;
	u8 port;
};

struct rtw89_core_tx_request {
	enum rtw89_core_tx_type tx_type;

	struct sk_buff *skb;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	struct rtw89_tx_desc_info desc_info;
};

struct rtw89_txq {
	struct list_head list;
	unsigned long flags;
	int wait_cnt;
};

struct rtw89_mac_ax_gnt {
	u8 gnt_bt_sw_en;
	u8 gnt_bt;
	u8 gnt_wl_sw_en;
	u8 gnt_wl;
};

#define RTW89_MAC_AX_COEX_GNT_NR 2
struct rtw89_mac_ax_coex_gnt {
	struct rtw89_mac_ax_gnt band[RTW89_MAC_AX_COEX_GNT_NR];
};

enum rtw89_btc_ncnt {
	BTC_NCNT_POWER_ON = 0x0,
	BTC_NCNT_POWER_OFF,
	BTC_NCNT_INIT_COEX,
	BTC_NCNT_SCAN_START,
	BTC_NCNT_SCAN_FINISH,
	BTC_NCNT_SPECIAL_PACKET,
	BTC_NCNT_SWITCH_BAND,
	BTC_NCNT_RFK_TIMEOUT,
	BTC_NCNT_SHOW_COEX_INFO,
	BTC_NCNT_ROLE_INFO,
	BTC_NCNT_CONTROL,
	BTC_NCNT_RADIO_STATE,
	BTC_NCNT_CUSTOMERIZE,
	BTC_NCNT_WL_RFK,
	BTC_NCNT_WL_STA,
	BTC_NCNT_FWINFO,
	BTC_NCNT_TIMER,
	BTC_NCNT_NUM
};

enum rtw89_btc_btinfo {
	BTC_BTINFO_L0 = 0,
	BTC_BTINFO_L1,
	BTC_BTINFO_L2,
	BTC_BTINFO_L3,
	BTC_BTINFO_H0,
	BTC_BTINFO_H1,
	BTC_BTINFO_H2,
	BTC_BTINFO_H3,
	BTC_BTINFO_MAX
};

enum rtw89_btc_dcnt {
	BTC_DCNT_RUN = 0x0,
	BTC_DCNT_CX_RUNINFO,
	BTC_DCNT_RPT,
	BTC_DCNT_RPT_FREEZE,
	BTC_DCNT_CYCLE,
	BTC_DCNT_CYCLE_FREEZE,
	BTC_DCNT_W1,
	BTC_DCNT_W1_FREEZE,
	BTC_DCNT_B1,
	BTC_DCNT_B1_FREEZE,
	BTC_DCNT_TDMA_NONSYNC,
	BTC_DCNT_SLOT_NONSYNC,
	BTC_DCNT_BTCNT_FREEZE,
	BTC_DCNT_WL_SLOT_DRIFT,
	BTC_DCNT_WL_STA_LAST,
	BTC_DCNT_NUM,
};

enum rtw89_btc_wl_state_cnt {
	BTC_WCNT_SCANAP = 0x0,
	BTC_WCNT_DHCP,
	BTC_WCNT_EAPOL,
	BTC_WCNT_ARP,
	BTC_WCNT_SCBDUPDATE,
	BTC_WCNT_RFK_REQ,
	BTC_WCNT_RFK_GO,
	BTC_WCNT_RFK_REJECT,
	BTC_WCNT_RFK_TIMEOUT,
	BTC_WCNT_CH_UPDATE,
	BTC_WCNT_NUM
};

enum rtw89_btc_bt_state_cnt {
	BTC_BCNT_RETRY = 0x0,
	BTC_BCNT_REINIT,
	BTC_BCNT_REENABLE,
	BTC_BCNT_SCBDREAD,
	BTC_BCNT_RELINK,
	BTC_BCNT_IGNOWL,
	BTC_BCNT_INQPAG,
	BTC_BCNT_INQ,
	BTC_BCNT_PAGE,
	BTC_BCNT_ROLESW,
	BTC_BCNT_AFH,
	BTC_BCNT_INFOUPDATE,
	BTC_BCNT_INFOSAME,
	BTC_BCNT_SCBDUPDATE,
	BTC_BCNT_HIPRI_TX,
	BTC_BCNT_HIPRI_RX,
	BTC_BCNT_LOPRI_TX,
	BTC_BCNT_LOPRI_RX,
	BTC_BCNT_POLUT,
	BTC_BCNT_RATECHG,
	BTC_BCNT_NUM
};

enum rtw89_btc_bt_profile {
	BTC_BT_NOPROFILE = 0,
	BTC_BT_HFP = BIT(0),
	BTC_BT_HID = BIT(1),
	BTC_BT_A2DP = BIT(2),
	BTC_BT_PAN = BIT(3),
	BTC_PROFILE_MAX = 4,
};

struct rtw89_btc_ant_info {
	u8 type;  /* shared, dedicated */
	u8 num;
	u8 isolation;

	u8 single_pos: 1;/* Single antenna at S0 or S1 */
	u8 diversity: 1;
};

enum rtw89_tfc_dir {
	RTW89_TFC_UL,
	RTW89_TFC_DL,
};

struct rtw89_btc_wl_smap {
	u32 busy: 1;
	u32 scan: 1;
	u32 connecting: 1;
	u32 roaming: 1;
	u32 _4way: 1;
	u32 rf_off: 1;
	u32 lps: 1;
	u32 ips: 1;
	u32 init_ok: 1;
	u32 traffic_dir : 2;
	u32 rf_off_pre: 1;
	u32 lps_pre: 1;
};

enum rtw89_tfc_lv {
	RTW89_TFC_IDLE,
	RTW89_TFC_ULTRA_LOW,
	RTW89_TFC_LOW,
	RTW89_TFC_MID,
	RTW89_TFC_HIGH,
};

#define RTW89_TP_SHIFT 18 /* bytes/2s --> Mbps */
DECLARE_EWMA(tp, 10, 2);

struct rtw89_traffic_stats {
	/* units in bytes */
	u64 tx_unicast;
	u64 rx_unicast;
	u32 tx_avg_len;
	u32 rx_avg_len;

	/* count for packets */
	u64 tx_cnt;
	u64 rx_cnt;

	/* units in Mbps */
	u32 tx_throughput;
	u32 rx_throughput;
	u32 tx_throughput_raw;
	u32 rx_throughput_raw;
	enum rtw89_tfc_lv tx_tfc_lv;
	enum rtw89_tfc_lv rx_tfc_lv;
	struct ewma_tp tx_ewma_tp;
	struct ewma_tp rx_ewma_tp;

	u16 tx_rate;
	u16 rx_rate;
};

struct rtw89_btc_statistic {
	u8 rssi; /* 0%~110% (dBm = rssi -110) */
	struct rtw89_traffic_stats traffic;
};

#define BTC_WL_RSSI_THMAX 4

struct rtw89_btc_wl_link_info {
	struct rtw89_btc_statistic stat;
	enum rtw89_tfc_dir dir;
	u8 rssi_state[BTC_WL_RSSI_THMAX];
	u8 mac_addr[ETH_ALEN];
	u8 busy;
	u8 ch;
	u8 bw;
	u8 band;
	u8 role;
	u8 pid;
	u8 phy;
	u8 dtim_period;
	u8 mode;

	u8 mac_id;
	u8 tx_retry;

	u32 bcn_period;
	u32 busy_t;
	u32 tx_time;
	u32 client_cnt;
	u32 rx_rate_drop_cnt;

	u32 active: 1;
	u32 noa: 1;
	u32 client_ps: 1;
	u32 connected: 2;
};

union rtw89_btc_wl_state_map {
	u32 val;
	struct rtw89_btc_wl_smap map;
};

struct rtw89_btc_bt_hfp_desc {
	u32 exist: 1;
	u32 type: 2;
	u32 rsvd: 29;
};

struct rtw89_btc_bt_hid_desc {
	u32 exist: 1;
	u32 slot_info: 2;
	u32 pair_cnt: 2;
	u32 type: 8;
	u32 rsvd: 19;
};

struct rtw89_btc_bt_a2dp_desc {
	u8 exist: 1;
	u8 exist_last: 1;
	u8 play_latency: 1;
	u8 type: 3;
	u8 active: 1;
	u8 sink: 1;

	u8 bitpool;
	u16 vendor_id;
	u32 device_name;
	u32 flush_time;
};

struct rtw89_btc_bt_pan_desc {
	u32 exist: 1;
	u32 type: 1;
	u32 active: 1;
	u32 rsvd: 29;
};

struct rtw89_btc_bt_rfk_info {
	u32 run: 1;
	u32 req: 1;
	u32 timeout: 1;
	u32 rsvd: 29;
};

union rtw89_btc_bt_rfk_info_map {
	u32 val;
	struct rtw89_btc_bt_rfk_info map;
};

struct rtw89_btc_bt_ver_info {
	u32 fw_coex; /* match with which coex_ver */
	u32 fw;
};

struct rtw89_btc_bool_sta_chg {
	u32 now: 1;
	u32 last: 1;
	u32 remain: 1;
	u32 srvd: 29;
};

struct rtw89_btc_u8_sta_chg {
	u8 now;
	u8 last;
	u8 remain;
	u8 rsvd;
};

struct rtw89_btc_wl_scan_info {
	u8 band[RTW89_PHY_MAX];
	u8 phy_map;
	u8 rsvd;
};

struct rtw89_btc_wl_dbcc_info {
	u8 op_band[RTW89_PHY_MAX]; /* op band in each phy */
	u8 scan_band[RTW89_PHY_MAX]; /* scan band in  each phy */
	u8 real_band[RTW89_PHY_MAX];
	u8 role[RTW89_PHY_MAX]; /* role in each phy */
};

struct rtw89_btc_wl_active_role {
	u8 connected: 1;
	u8 pid: 3;
	u8 phy: 1;
	u8 noa: 1;
	u8 band: 2;

	u8 client_ps: 1;
	u8 bw: 7;

	u8 role;
	u8 ch;

	u16 tx_lvl;
	u16 rx_lvl;
	u16 tx_rate;
	u16 rx_rate;
};

struct rtw89_btc_wl_role_info_bpos {
	u16 none: 1;
	u16 station: 1;
	u16 ap: 1;
	u16 vap: 1;
	u16 adhoc: 1;
	u16 adhoc_master: 1;
	u16 mesh: 1;
	u16 moniter: 1;
	u16 p2p_device: 1;
	u16 p2p_gc: 1;
	u16 p2p_go: 1;
	u16 nan: 1;
};

union rtw89_btc_wl_role_info_map {
	u16 val;
	struct rtw89_btc_wl_role_info_bpos role;
};

struct rtw89_btc_wl_role_info { /* struct size must be n*4 bytes */
	u8 connect_cnt;
	u8 link_mode;
	union rtw89_btc_wl_role_info_map role_map;
	struct rtw89_btc_wl_active_role active_role[RTW89_PORT_NUM];
};

struct rtw89_btc_wl_ver_info {
	u32 fw_coex; /* match with which coex_ver */
	u32 fw;
	u32 mac;
	u32 bb;
	u32 rf;
};

struct rtw89_btc_wl_afh_info {
	u8 en;
	u8 ch;
	u8 bw;
	u8 rsvd;
} __packed;

struct rtw89_btc_wl_rfk_info {
	u32 state: 2;
	u32 path_map: 4;
	u32 phy_map: 2;
	u32 band: 2;
	u32 type: 8;
	u32 rsvd: 14;
};

struct rtw89_btc_bt_smap {
	u32 connect: 1;
	u32 ble_connect: 1;
	u32 acl_busy: 1;
	u32 sco_busy: 1;
	u32 mesh_busy: 1;
	u32 inq_pag: 1;
};

union rtw89_btc_bt_state_map {
	u32 val;
	struct rtw89_btc_bt_smap map;
};

#define BTC_BT_RSSI_THMAX 4
#define BTC_BT_AFH_GROUP 12

struct rtw89_btc_bt_link_info {
	struct rtw89_btc_u8_sta_chg profile_cnt;
	struct rtw89_btc_bool_sta_chg multi_link;
	struct rtw89_btc_bool_sta_chg relink;
	struct rtw89_btc_bt_hfp_desc hfp_desc;
	struct rtw89_btc_bt_hid_desc hid_desc;
	struct rtw89_btc_bt_a2dp_desc a2dp_desc;
	struct rtw89_btc_bt_pan_desc pan_desc;
	union rtw89_btc_bt_state_map status;

	u8 sut_pwr_level[BTC_PROFILE_MAX];
	u8 golden_rx_shift[BTC_PROFILE_MAX];
	u8 rssi_state[BTC_BT_RSSI_THMAX];
	u8 afh_map[BTC_BT_AFH_GROUP];

	u32 role_sw: 1;
	u32 slave_role: 1;
	u32 afh_update: 1;
	u32 cqddr: 1;
	u32 rssi: 8;
	u32 tx_3m: 1;
	u32 rsvd: 19;
};

struct rtw89_btc_3rdcx_info {
	u8 type;   /* 0: none, 1:zigbee, 2:LTE  */
	u8 hw_coex;
	u16 rsvd;
};

struct rtw89_btc_dm_emap {
	u32 init: 1;
	u32 pta_owner: 1;
	u32 wl_rfk_timeout: 1;
	u32 bt_rfk_timeout: 1;

	u32 wl_fw_hang: 1;
	u32 offload_mismatch: 1;
	u32 cycle_hang: 1;
	u32 w1_hang: 1;

	u32 b1_hang: 1;
	u32 tdma_no_sync: 1;
	u32 wl_slot_drift: 1;
};

union rtw89_btc_dm_error_map {
	u32 val;
	struct rtw89_btc_dm_emap map;
};

struct rtw89_btc_rf_para {
	u32 tx_pwr_freerun;
	u32 rx_gain_freerun;
	u32 tx_pwr_perpkt;
	u32 rx_gain_perpkt;
};

struct rtw89_btc_wl_info {
	struct rtw89_btc_wl_link_info link_info[RTW89_PORT_NUM];
	struct rtw89_btc_wl_rfk_info rfk_info;
	struct rtw89_btc_wl_ver_info  ver_info;
	struct rtw89_btc_wl_afh_info afh_info;
	struct rtw89_btc_wl_role_info role_info;
	struct rtw89_btc_wl_scan_info scan_info;
	struct rtw89_btc_wl_dbcc_info dbcc_info;
	struct rtw89_btc_rf_para rf_para;
	union rtw89_btc_wl_state_map status;

	u8 port_id[RTW89_WIFI_ROLE_MLME_MAX];
	u8 rssi_level;

	u32 scbd;
};

struct rtw89_btc_module {
	struct rtw89_btc_ant_info ant;
	u8 rfe_type;
	u8 cv;

	u8 bt_solo: 1;
	u8 bt_pos: 1;
	u8 switch_type: 1;

	u8 rsvd;
};

#define RTW89_BTC_DM_MAXSTEP 30
#define RTW89_BTC_DM_CNT_MAX (RTW89_BTC_DM_MAXSTEP * 8)

struct rtw89_btc_dm_step {
	u16 step[RTW89_BTC_DM_MAXSTEP];
	u8 step_pos;
	bool step_ov;
};

struct rtw89_btc_init_info {
	struct rtw89_btc_module module;
	u8 wl_guard_ch;

	u8 wl_only: 1;
	u8 wl_init_ok: 1;
	u8 dbcc_en: 1;
	u8 cx_other: 1;
	u8 bt_only: 1;

	u16 rsvd;
};

struct rtw89_btc_wl_tx_limit_para {
	u16 enable;
	u32 tx_time;	/* unit: us */
	u16 tx_retry;
};

struct rtw89_btc_bt_scan_info {
	u16 win;
	u16 intvl;
	u32 enable: 1;
	u32 interlace: 1;
	u32 rsvd: 30;
};

enum rtw89_btc_bt_scan_type {
	BTC_SCAN_INQ	= 0,
	BTC_SCAN_PAGE,
	BTC_SCAN_BLE,
	BTC_SCAN_INIT,
	BTC_SCAN_TV,
	BTC_SCAN_ADV,
	BTC_SCAN_MAX1,
};

struct rtw89_btc_bt_info {
	struct rtw89_btc_bt_link_info link_info;
	struct rtw89_btc_bt_scan_info scan_info[BTC_SCAN_MAX1];
	struct rtw89_btc_bt_ver_info ver_info;
	struct rtw89_btc_bool_sta_chg enable;
	struct rtw89_btc_bool_sta_chg inq_pag;
	struct rtw89_btc_rf_para rf_para;
	union rtw89_btc_bt_rfk_info_map rfk_info;

	u8 raw_info[BTC_BTINFO_MAX]; /* raw bt info from mailbox */

	u32 scbd;
	u32 feature;

	u32 mbx_avl: 1;
	u32 whql_test: 1;
	u32 igno_wl: 1;
	u32 reinit: 1;
	u32 ble_scan_en: 1;
	u32 btg_type: 1;
	u32 inq: 1;
	u32 pag: 1;
	u32 run_patch_code: 1;
	u32 hi_lna_rx: 1;
	u32 rsvd: 22;
};

struct rtw89_btc_cx {
	struct rtw89_btc_wl_info wl;
	struct rtw89_btc_bt_info bt;
	struct rtw89_btc_3rdcx_info other;
	u32 state_map;
	u32 cnt_bt[BTC_BCNT_NUM];
	u32 cnt_wl[BTC_WCNT_NUM];
};

struct rtw89_btc_fbtc_tdma {
	u8 type;
	u8 rxflctrl;
	u8 txpause;
	u8 wtgle_n;
	u8 leak_n;
	u8 ext_ctrl;
	u8 rsvd0;
	u8 rsvd1;
} __packed;

#define CXMREG_MAX 30
#define FCXMAX_STEP 255 /*STEP trace record cnt, Max:65535, default:255*/
#define BTCRPT_VER 1
#define BTC_CYCLE_SLOT_MAX 48 /* must be even number, non-zero */

enum rtw89_btc_bt_rfk_counter {
	BTC_BCNT_RFK_REQ = 0,
	BTC_BCNT_RFK_GO = 1,
	BTC_BCNT_RFK_REJECT = 2,
	BTC_BCNT_RFK_FAIL = 3,
	BTC_BCNT_RFK_TIMEOUT = 4,
	BTC_BCNT_RFK_MAX
};

struct rtw89_btc_fbtc_rpt_ctrl {
	u16 fver;
	u16 rpt_cnt; /* tmr counters */
	u32 wl_fw_coex_ver; /* match which driver's coex version */
	u32 wl_fw_cx_offload;
	u32 wl_fw_ver;
	u32 rpt_enable;
	u32 rpt_para; /* ms */
	u32 mb_send_fail_cnt; /* fw send mailbox fail counter */
	u32 mb_send_ok_cnt; /* fw send mailbox ok counter */
	u32 mb_recv_cnt; /* fw recv mailbox counter */
	u32 mb_a2dp_empty_cnt; /* a2dp empty count */
	u32 mb_a2dp_flct_cnt; /* a2dp empty flow control counter */
	u32 mb_a2dp_full_cnt; /* a2dp empty full counter */
	u32 bt_rfk_cnt[BTC_BCNT_RFK_MAX];
	u32 c2h_cnt; /* fw send c2h counter  */
	u32 h2c_cnt; /* fw recv h2c counter */
} __packed;

enum rtw89_fbtc_ext_ctrl_type {
	CXECTL_OFF = 0x0, /* tdma off */
	CXECTL_B2 = 0x1, /* allow B2 (beacon-early) */
	CXECTL_EXT = 0x2,
	CXECTL_MAX
};

union rtw89_btc_fbtc_rxflct {
	u8 val;
	u8 type: 3;
	u8 tgln_n: 5;
};

enum rtw89_btc_cxst_state {
	CXST_OFF = 0x0,
	CXST_B2W = 0x1,
	CXST_W1 = 0x2,
	CXST_W2 = 0x3,
	CXST_W2B = 0x4,
	CXST_B1 = 0x5,
	CXST_B2 = 0x6,
	CXST_B3 = 0x7,
	CXST_B4 = 0x8,
	CXST_LK = 0x9,
	CXST_BLK = 0xa,
	CXST_E2G = 0xb,
	CXST_E5G = 0xc,
	CXST_EBT = 0xd,
	CXST_ENULL = 0xe,
	CXST_WLK = 0xf,
	CXST_W1FDD = 0x10,
	CXST_B1FDD = 0x11,
	CXST_MAX = 0x12,
};

enum {
	CXBCN_ALL = 0x0,
	CXBCN_ALL_OK,
	CXBCN_BT_SLOT,
	CXBCN_BT_OK,
	CXBCN_MAX
};

enum btc_slot_type {
	SLOT_MIX = 0x0, /* accept BT Lower-Pri Tx/Rx request 0x778 = 1 */
	SLOT_ISO = 0x1, /* no accept BT Lower-Pri Tx/Rx request 0x778 = d*/
	CXSTYPE_NUM,
};

enum { /* TIME */
	CXT_BT = 0x0,
	CXT_WL = 0x1,
	CXT_MAX
};

enum { /* TIME-A2DP */
	CXT_FLCTRL_OFF = 0x0,
	CXT_FLCTRL_ON = 0x1,
	CXT_FLCTRL_MAX
};

enum { /* STEP TYPE */
	CXSTEP_NONE = 0x0,
	CXSTEP_EVNT = 0x1,
	CXSTEP_SLOT = 0x2,
	CXSTEP_MAX,
};

#define FCXGPIODBG_VER 1
#define BTC_DBG_MAX1  32
struct rtw89_btc_fbtc_gpio_dbg {
	u8 fver;
	u8 rsvd;
	u16 rsvd2;
	u32 en_map; /* which debug signal (see btc_wl_gpio_debug) is enable */
	u32 pre_state; /* the debug signal is 1 or 0  */
	u8 gpio_map[BTC_DBG_MAX1]; /*the debug signals to GPIO-Position */
} __packed;

#define FCXMREG_VER 1
struct rtw89_btc_fbtc_mreg_val {
	u8 fver;
	u8 reg_num;
	__le16 rsvd;
	__le32 mreg_val[CXMREG_MAX];
} __packed;

#define RTW89_DEF_FBTC_MREG(__type, __bytes, __offset) \
	{ .type = cpu_to_le16(__type), .bytes = cpu_to_le16(__bytes), \
	  .offset = cpu_to_le32(__offset), }

struct rtw89_btc_fbtc_mreg {
	__le16 type;
	__le16 bytes;
	__le32 offset;
} __packed;

struct rtw89_btc_fbtc_slot {
	__le16 dur;
	__le32 cxtbl;
	__le16 cxtype;
} __packed;

#define FCXSLOTS_VER 1
struct rtw89_btc_fbtc_slots {
	u8 fver;
	u8 tbl_num;
	__le16 rsvd;
	__le32 update_map;
	struct rtw89_btc_fbtc_slot slot[CXST_MAX];
} __packed;

#define FCXSTEP_VER 2
struct rtw89_btc_fbtc_step {
	u8 type;
	u8 val;
	__le16 difft;
} __packed;

struct rtw89_btc_fbtc_steps {
	u8 fver;
	u8 rsvd;
	__le16 cnt;
	__le16 pos_old;
	__le16 pos_new;
	struct rtw89_btc_fbtc_step step[FCXMAX_STEP];
} __packed;

#define FCXCYSTA_VER 2
struct rtw89_btc_fbtc_cysta { /* statistics for cycles */
	u8 fver;
	u8 rsvd;
	__le16 cycles; /* total cycle number */
	__le16 cycles_a2dp[CXT_FLCTRL_MAX];
	__le16 a2dpept; /* a2dp empty cnt */
	__le16 a2dpeptto; /* a2dp empty timeout cnt*/
	__le16 tavg_cycle[CXT_MAX]; /* avg wl/bt cycle time */
	__le16 tmax_cycle[CXT_MAX]; /* max wl/bt cycle time */
	__le16 tmaxdiff_cycle[CXT_MAX]; /* max wl-wl bt-bt cycle diff time */
	__le16 tavg_a2dp[CXT_FLCTRL_MAX]; /* avg a2dp PSTDMA/TDMA time */
	__le16 tmax_a2dp[CXT_FLCTRL_MAX]; /* max a2dp PSTDMA/TDMA time */
	__le16 tavg_a2dpept; /* avg a2dp empty time */
	__le16 tmax_a2dpept; /* max a2dp empty time */
	__le16 tavg_lk; /* avg leak-slot time */
	__le16 tmax_lk; /* max leak-slot time */
	__le32 slot_cnt[CXST_MAX]; /* slot count */
	__le32 bcn_cnt[CXBCN_MAX];
	__le32 leakrx_cnt; /* the rximr occur at leak slot  */
	__le32 collision_cnt; /* counter for event/timer occur at same time */
	__le32 skip_cnt;
	__le32 exception;
	__le32 except_cnt;
	__le16 tslot_cycle[BTC_CYCLE_SLOT_MAX];
} __packed;

#define FCXNULLSTA_VER 1
struct rtw89_btc_fbtc_cynullsta { /* cycle null statistics */
	u8 fver;
	u8 rsvd;
	__le16 rsvd2;
	__le32 max_t[2]; /* max_t for 0:null0/1:null1 */
	__le32 avg_t[2]; /* avg_t for 0:null0/1:null1 */
	__le32 result[2][4]; /* 0:fail, 1:ok, 2:on_time, 3:retry */
} __packed;

#define FCX_BTVER_VER 1
struct rtw89_btc_fbtc_btver {
	u8 fver;
	u8 rsvd;
	__le16 rsvd2;
	__le32 coex_ver; /*bit[15:8]->shared, bit[7:0]->non-shared */
	__le32 fw_ver;
	__le32 feature;
} __packed;

#define FCX_BTSCAN_VER 1
struct rtw89_btc_fbtc_btscan {
	u8 fver;
	u8 rsvd;
	__le16 rsvd2;
	u8 scan[6];
} __packed;

#define FCX_BTAFH_VER 1
struct rtw89_btc_fbtc_btafh {
	u8 fver;
	u8 rsvd;
	__le16 rsvd2;
	u8 afh_l[4]; /*bit0:2402, bit1: 2403.... bit31:2433 */
	u8 afh_m[4]; /*bit0:2434, bit1: 2435.... bit31:2465 */
	u8 afh_h[4]; /*bit0:2466, bit1:2467......bit14:2480 */
} __packed;

#define FCX_BTDEVINFO_VER 1
struct rtw89_btc_fbtc_btdevinfo {
	u8 fver;
	u8 rsvd;
	__le16 vendor_id;
	__le32 dev_name; /* only 24 bits valid */
	__le32 flush_time;
} __packed;

#define RTW89_BTC_WL_DEF_TX_PWR GENMASK(7, 0)
struct rtw89_btc_rf_trx_para {
	u32 wl_tx_power; /* absolute Tx power (dBm), 0xff-> no BTC control */
	u32 wl_rx_gain;  /* rx gain table index (TBD.) */
	u8 bt_tx_power; /* decrease Tx power (dB) */
	u8 bt_rx_gain;  /* LNA constrain level */
};

struct rtw89_btc_dm {
	struct rtw89_btc_fbtc_slot slot[CXST_MAX];
	struct rtw89_btc_fbtc_slot slot_now[CXST_MAX];
	struct rtw89_btc_fbtc_tdma tdma;
	struct rtw89_btc_fbtc_tdma tdma_now;
	struct rtw89_mac_ax_coex_gnt gnt;
	struct rtw89_btc_init_info init_info; /* pass to wl_fw if offload */
	struct rtw89_btc_rf_trx_para rf_trx_para;
	struct rtw89_btc_wl_tx_limit_para wl_tx_limit;
	struct rtw89_btc_dm_step dm_step;
	union rtw89_btc_dm_error_map error;
	u32 cnt_dm[BTC_DCNT_NUM];
	u32 cnt_notify[BTC_NCNT_NUM];

	u32 update_slot_map;
	u32 set_ant_path;

	u32 wl_only: 1;
	u32 wl_fw_cx_offload: 1;
	u32 freerun: 1;
	u32 wl_ps_ctrl: 2;
	u32 wl_mimo_ps: 1;
	u32 leak_ap: 1;
	u32 noisy_level: 3;
	u32 coex_info_map: 8;
	u32 bt_only: 1;
	u32 wl_btg_rx: 1;
	u32 trx_para_level: 8;
	u32 wl_stb_chg: 1;
	u32 rsvd: 3;

	u16 slot_dur[CXST_MAX];

	u8 run_reason;
	u8 run_action;
};

struct rtw89_btc_ctrl {
	u32 manual: 1;
	u32 igno_bt: 1;
	u32 always_freerun: 1;
	u32 trace_step: 16;
	u32 rsvd: 12;
};

struct rtw89_btc_dbg {
	/* cmd "rb" */
	bool rb_done;
	u32 rb_val;
};

#define FCXTDMA_VER 1

enum rtw89_btc_btf_fw_event {
	BTF_EVNT_RPT = 0,
	BTF_EVNT_BT_INFO = 1,
	BTF_EVNT_BT_SCBD = 2,
	BTF_EVNT_BT_REG = 3,
	BTF_EVNT_CX_RUNINFO = 4,
	BTF_EVNT_BT_PSD = 5,
	BTF_EVNT_BUF_OVERFLOW,
	BTF_EVNT_C2H_LOOPBACK,
	BTF_EVNT_MAX,
};

enum btf_fw_event_report {
	BTC_RPT_TYPE_CTRL = 0x0,
	BTC_RPT_TYPE_TDMA,
	BTC_RPT_TYPE_SLOT,
	BTC_RPT_TYPE_CYSTA,
	BTC_RPT_TYPE_STEP,
	BTC_RPT_TYPE_NULLSTA,
	BTC_RPT_TYPE_MREG,
	BTC_RPT_TYPE_GPIO_DBG,
	BTC_RPT_TYPE_BT_VER,
	BTC_RPT_TYPE_BT_SCAN,
	BTC_RPT_TYPE_BT_AFH,
	BTC_RPT_TYPE_BT_DEVICE,
	BTC_RPT_TYPE_TEST,
	BTC_RPT_TYPE_MAX = 31
};

enum rtw_btc_btf_reg_type {
	REG_MAC = 0x0,
	REG_BB = 0x1,
	REG_RF = 0x2,
	REG_BT_RF = 0x3,
	REG_BT_MODEM = 0x4,
	REG_BT_BLUEWIZE = 0x5,
	REG_BT_VENDOR = 0x6,
	REG_BT_LE = 0x7,
	REG_MAX_TYPE,
};

struct rtw89_btc_rpt_cmn_info {
	u32 rx_cnt;
	u32 rx_len;
	u32 req_len; /* expected rsp len */
	u8 req_fver; /* expected rsp fver */
	u8 rsp_fver; /* fver from fw */
	u8 valid;
} __packed;

struct rtw89_btc_report_ctrl_state {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_rpt_ctrl finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_tdma {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_tdma finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_slots {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_slots finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_cysta {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_cysta finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_step {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_steps finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_nullsta {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_cynullsta finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_mreg {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_mreg_val finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_gpio_dbg {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_gpio_dbg finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btver {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_btver finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btscan {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_btscan finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btafh {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_btafh finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btdev {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	struct rtw89_btc_fbtc_btdevinfo finfo; /* info from fw */
};

enum rtw89_btc_btfre_type {
	BTFRE_INVALID_INPUT = 0x0, /* invalid input parameters */
	BTFRE_UNDEF_TYPE,
	BTFRE_EXCEPTION,
	BTFRE_MAX,
};

struct rtw89_btc_btf_fwinfo {
	u32 cnt_c2h;
	u32 cnt_h2c;
	u32 cnt_h2c_fail;
	u32 event[BTF_EVNT_MAX];

	u32 err[BTFRE_MAX];
	u32 len_mismch;
	u32 fver_mismch;
	u32 rpt_en_map;

	struct rtw89_btc_report_ctrl_state rpt_ctrl;
	struct rtw89_btc_rpt_fbtc_tdma rpt_fbtc_tdma;
	struct rtw89_btc_rpt_fbtc_slots rpt_fbtc_slots;
	struct rtw89_btc_rpt_fbtc_cysta rpt_fbtc_cysta;
	struct rtw89_btc_rpt_fbtc_step rpt_fbtc_step;
	struct rtw89_btc_rpt_fbtc_nullsta rpt_fbtc_nullsta;
	struct rtw89_btc_rpt_fbtc_mreg rpt_fbtc_mregval;
	struct rtw89_btc_rpt_fbtc_gpio_dbg rpt_fbtc_gpio_dbg;
	struct rtw89_btc_rpt_fbtc_btver rpt_fbtc_btver;
	struct rtw89_btc_rpt_fbtc_btscan rpt_fbtc_btscan;
	struct rtw89_btc_rpt_fbtc_btafh rpt_fbtc_btafh;
	struct rtw89_btc_rpt_fbtc_btdev rpt_fbtc_btdev;
};

#define RTW89_BTC_POLICY_MAXLEN 512

struct rtw89_btc {
	struct rtw89_btc_cx cx;
	struct rtw89_btc_dm dm;
	struct rtw89_btc_ctrl ctrl;
	struct rtw89_btc_module mdinfo;
	struct rtw89_btc_btf_fwinfo fwinfo;
	struct rtw89_btc_dbg dbg;

	struct work_struct eapol_notify_work;
	struct work_struct arp_notify_work;
	struct work_struct dhcp_notify_work;
	struct work_struct icmp_notify_work;

	u32 bt_req_len;

	u8 policy[RTW89_BTC_POLICY_MAXLEN];
	u16 policy_len;
	u16 policy_type;
	bool bt_req_en;
	bool update_policy_force;
	bool lps;
};

enum rtw89_ra_mode {
	RTW89_RA_MODE_CCK = BIT(0),
	RTW89_RA_MODE_OFDM = BIT(1),
	RTW89_RA_MODE_HT = BIT(2),
	RTW89_RA_MODE_VHT = BIT(3),
	RTW89_RA_MODE_HE = BIT(4),
};

enum rtw89_ra_report_mode {
	RTW89_RA_RPT_MODE_LEGACY,
	RTW89_RA_RPT_MODE_HT,
	RTW89_RA_RPT_MODE_VHT,
	RTW89_RA_RPT_MODE_HE,
};

enum rtw89_dig_noisy_level {
	RTW89_DIG_NOISY_LEVEL0 = -1,
	RTW89_DIG_NOISY_LEVEL1 = 0,
	RTW89_DIG_NOISY_LEVEL2 = 1,
	RTW89_DIG_NOISY_LEVEL3 = 2,
	RTW89_DIG_NOISY_LEVEL_MAX = 3,
};

enum rtw89_gi_ltf {
	RTW89_GILTF_LGI_4XHE32 = 0,
	RTW89_GILTF_SGI_4XHE08 = 1,
	RTW89_GILTF_2XHE16 = 2,
	RTW89_GILTF_2XHE08 = 3,
	RTW89_GILTF_1XHE16 = 4,
	RTW89_GILTF_1XHE08 = 5,
	RTW89_GILTF_MAX
};

enum rtw89_rx_frame_type {
	RTW89_RX_TYPE_MGNT = 0,
	RTW89_RX_TYPE_CTRL = 1,
	RTW89_RX_TYPE_DATA = 2,
	RTW89_RX_TYPE_RSVD = 3,
};

struct rtw89_ra_info {
	u8 is_dis_ra:1;
	/* Bit0 : CCK
	 * Bit1 : OFDM
	 * Bit2 : HT
	 * Bit3 : VHT
	 * Bit4 : HE
	 */
	u8 mode_ctrl:5;
	u8 bw_cap:2;
	u8 macid;
	u8 dcm_cap:1;
	u8 er_cap:1;
	u8 init_rate_lv:2;
	u8 upd_all:1;
	u8 en_sgi:1;
	u8 ldpc_cap:1;
	u8 stbc_cap:1;
	u8 ss_num:3;
	u8 giltf:3;
	u8 upd_bw_nss_mask:1;
	u8 upd_mask:1;
	u64 ra_mask; /* 63 bits ra_mask + 1 bit CSI ctrl */
	/* BFee CSI */
	u8 band_num;
	u8 ra_csi_rate_en:1;
	u8 fixed_csi_rate_en:1;
	u8 cr_tbl_sel:1;
	u8 rsvd2:5;
	u8 csi_mcs_ss_idx;
	u8 csi_mode:2;
	u8 csi_gi_ltf:3;
	u8 csi_bw:3;
};

#define RTW89_PPDU_MAX_USR 4
#define RTW89_PPDU_MAC_INFO_USR_SIZE 4
#define RTW89_PPDU_MAC_INFO_SIZE 8
#define RTW89_PPDU_MAC_RX_CNT_SIZE 96

#define RTW89_MAX_RX_AGG_NUM 64
#define RTW89_MAX_TX_AGG_NUM 128

struct rtw89_ampdu_params {
	u16 agg_num;
	bool amsdu;
};

struct rtw89_ra_report {
	struct rate_info txrate;
	u32 bit_rate;
	u16 hw_rate;
};

DECLARE_EWMA(rssi, 10, 16);

#define RTW89_BA_CAM_NUM 2

struct rtw89_ba_cam_entry {
	u8 tid;
};

#define RTW89_MAX_ADDR_CAM_NUM		128
#define RTW89_MAX_BSSID_CAM_NUM		20
#define RTW89_MAX_SEC_CAM_NUM		128
#define RTW89_SEC_CAM_IN_ADDR_CAM	7

struct rtw89_addr_cam_entry {
	u8 addr_cam_idx;
	u8 offset;
	u8 len;
	u8 valid	: 1;
	u8 addr_mask	: 6;
	u8 wapi		: 1;
	u8 mask_sel	: 2;
	u8 bssid_cam_idx: 6;

	u8 sec_ent_mode;
	DECLARE_BITMAP(sec_cam_map, RTW89_SEC_CAM_IN_ADDR_CAM);
	u8 sec_ent_keyid[RTW89_SEC_CAM_IN_ADDR_CAM];
	u8 sec_ent[RTW89_SEC_CAM_IN_ADDR_CAM];
	struct rtw89_sec_cam_entry *sec_entries[RTW89_SEC_CAM_IN_ADDR_CAM];
};

struct rtw89_bssid_cam_entry {
	u8 bssid[ETH_ALEN];
	u8 phy_idx;
	u8 bssid_cam_idx;
	u8 offset;
	u8 len;
	u8 valid : 1;
	u8 num;
};

struct rtw89_sec_cam_entry {
	u8 sec_cam_idx;
	u8 offset;
	u8 len;
	u8 type : 4;
	u8 ext_key : 1;
	u8 spp_mode : 1;
	/* 256 bits */
	u8 key[32];
};

struct rtw89_sta {
	u8 mac_id;
	bool disassoc;
	struct rtw89_vif *rtwvif;
	struct rtw89_ra_info ra;
	struct rtw89_ra_report ra_report;
	int max_agg_wait;
	u8 prev_rssi;
	struct ewma_rssi avg_rssi;
	struct rtw89_ampdu_params ampdu_params[IEEE80211_NUM_TIDS];
	struct ieee80211_rx_status rx_status;
	u16 rx_hw_rate;
	__le32 htc_template;
	struct rtw89_addr_cam_entry addr_cam; /* AP mode only */

	bool use_cfg_mask;
	struct cfg80211_bitrate_mask mask;

	bool cctl_tx_time;
	u32 ampdu_max_time:4;
	bool cctl_tx_retry_limit;
	u32 data_tx_cnt_lmt:6;

	DECLARE_BITMAP(ba_cam_map, RTW89_BA_CAM_NUM);
	struct rtw89_ba_cam_entry ba_cam_entry[RTW89_BA_CAM_NUM];
};

struct rtw89_efuse {
	bool valid;
	u8 xtal_cap;
	u8 addr[ETH_ALEN];
	u8 rfe_type;
	char country_code[2];
};

struct rtw89_phy_rate_pattern {
	u64 ra_mask;
	u16 rate;
	u8 ra_mode;
	bool enable;
};

struct rtw89_vif {
	struct list_head list;
	struct rtw89_dev *rtwdev;
	u8 mac_id;
	u8 port;
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 phy_idx;
	u8 mac_idx;
	u8 net_type;
	u8 wifi_role;
	u8 self_role;
	u8 wmm;
	u8 bcn_hit_cond;
	u8 hit_rule;
	bool trigger;
	bool lsig_txop;
	u8 tgt_ind;
	u8 frm_tgt_ind;
	bool wowlan_pattern;
	bool wowlan_uc;
	bool wowlan_magic;
	bool is_hesta;
	bool last_a_ctrl;
	struct work_struct update_beacon_work;
	struct rtw89_addr_cam_entry addr_cam;
	struct rtw89_bssid_cam_entry bssid_cam;
	struct ieee80211_tx_queue_params tx_params[IEEE80211_NUM_ACS];
	struct rtw89_traffic_stats stats;
	struct rtw89_phy_rate_pattern rate_pattern;
};

enum rtw89_lv1_rcvy_step {
	RTW89_LV1_RCVY_STEP_1,
	RTW89_LV1_RCVY_STEP_2,
};

struct rtw89_hci_ops {
	int (*tx_write)(struct rtw89_dev *rtwdev, struct rtw89_core_tx_request *tx_req);
	void (*tx_kick_off)(struct rtw89_dev *rtwdev, u8 txch);
	void (*flush_queues)(struct rtw89_dev *rtwdev, u32 queues, bool drop);
	void (*reset)(struct rtw89_dev *rtwdev);
	int (*start)(struct rtw89_dev *rtwdev);
	void (*stop)(struct rtw89_dev *rtwdev);
	void (*recalc_int_mit)(struct rtw89_dev *rtwdev);

	u8 (*read8)(struct rtw89_dev *rtwdev, u32 addr);
	u16 (*read16)(struct rtw89_dev *rtwdev, u32 addr);
	u32 (*read32)(struct rtw89_dev *rtwdev, u32 addr);
	void (*write8)(struct rtw89_dev *rtwdev, u32 addr, u8 data);
	void (*write16)(struct rtw89_dev *rtwdev, u32 addr, u16 data);
	void (*write32)(struct rtw89_dev *rtwdev, u32 addr, u32 data);

	int (*mac_pre_init)(struct rtw89_dev *rtwdev);
	int (*mac_post_init)(struct rtw89_dev *rtwdev);
	int (*deinit)(struct rtw89_dev *rtwdev);

	u32 (*check_and_reclaim_tx_resource)(struct rtw89_dev *rtwdev, u8 txch);
	int (*mac_lv1_rcvy)(struct rtw89_dev *rtwdev, enum rtw89_lv1_rcvy_step step);
	void (*dump_err_status)(struct rtw89_dev *rtwdev);
	int (*napi_poll)(struct napi_struct *napi, int budget);
};

struct rtw89_hci_info {
	const struct rtw89_hci_ops *ops;
	enum rtw89_hci_type type;
	u32 rpwm_addr;
	u32 cpwm_addr;
};

struct rtw89_chip_ops {
	void (*bb_reset)(struct rtw89_dev *rtwdev,
			 enum rtw89_phy_idx phy_idx);
	void (*bb_sethw)(struct rtw89_dev *rtwdev);
	u32 (*read_rf)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
		       u32 addr, u32 mask);
	bool (*write_rf)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask, u32 data);
	void (*set_channel)(struct rtw89_dev *rtwdev,
			    struct rtw89_channel_params *param);
	void (*set_channel_help)(struct rtw89_dev *rtwdev, bool enter,
				 struct rtw89_channel_help_params *p);
	int (*read_efuse)(struct rtw89_dev *rtwdev, u8 *log_map);
	int (*read_phycap)(struct rtw89_dev *rtwdev, u8 *phycap_map);
	void (*fem_setup)(struct rtw89_dev *rtwdev);
	void (*rfk_init)(struct rtw89_dev *rtwdev);
	void (*rfk_channel)(struct rtw89_dev *rtwdev);
	void (*rfk_band_changed)(struct rtw89_dev *rtwdev);
	void (*rfk_scan)(struct rtw89_dev *rtwdev, bool start);
	void (*rfk_track)(struct rtw89_dev *rtwdev);
	void (*power_trim)(struct rtw89_dev *rtwdev);
	void (*set_txpwr)(struct rtw89_dev *rtwdev);
	void (*set_txpwr_ctrl)(struct rtw89_dev *rtwdev);
	int (*init_txpwr_unit)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
	u8 (*get_thermal)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path);
	void (*ctrl_btg)(struct rtw89_dev *rtwdev, bool btg);
	void (*query_ppdu)(struct rtw89_dev *rtwdev,
			   struct rtw89_rx_phy_ppdu *phy_ppdu,
			   struct ieee80211_rx_status *status);
	void (*bb_ctrl_btc_preagc)(struct rtw89_dev *rtwdev, bool bt_en);
	void (*set_txpwr_ul_tb_offset)(struct rtw89_dev *rtwdev,
				       s16 pw_ofst, enum rtw89_mac_idx mac_idx);

	void (*btc_set_rfe)(struct rtw89_dev *rtwdev);
	void (*btc_init_cfg)(struct rtw89_dev *rtwdev);
	void (*btc_set_wl_pri)(struct rtw89_dev *rtwdev, u8 map, bool state);
	void (*btc_set_wl_txpwr_ctrl)(struct rtw89_dev *rtwdev, u32 txpwr_val);
	s8 (*btc_get_bt_rssi)(struct rtw89_dev *rtwdev, s8 val);
	void (*btc_bt_aci_imp)(struct rtw89_dev *rtwdev);
	void (*btc_update_bt_cnt)(struct rtw89_dev *rtwdev);
	void (*btc_wl_s1_standby)(struct rtw89_dev *rtwdev, bool state);
};

enum rtw89_dma_ch {
	RTW89_DMA_ACH0 = 0,
	RTW89_DMA_ACH1 = 1,
	RTW89_DMA_ACH2 = 2,
	RTW89_DMA_ACH3 = 3,
	RTW89_DMA_ACH4 = 4,
	RTW89_DMA_ACH5 = 5,
	RTW89_DMA_ACH6 = 6,
	RTW89_DMA_ACH7 = 7,
	RTW89_DMA_B0MG = 8,
	RTW89_DMA_B0HI = 9,
	RTW89_DMA_B1MG = 10,
	RTW89_DMA_B1HI = 11,
	RTW89_DMA_H2C = 12,
	RTW89_DMA_CH_NUM = 13
};

enum rtw89_qta_mode {
	RTW89_QTA_SCC,
	RTW89_QTA_DLFW,

	/* keep last */
	RTW89_QTA_INVALID,
};

struct rtw89_hfc_ch_cfg {
	u16 min;
	u16 max;
#define grp_0 0
#define grp_1 1
#define grp_num 2
	u8 grp;
};

struct rtw89_hfc_ch_info {
	u16 aval;
	u16 used;
};

struct rtw89_hfc_pub_cfg {
	u16 grp0;
	u16 grp1;
	u16 pub_max;
	u16 wp_thrd;
};

struct rtw89_hfc_pub_info {
	u16 g0_used;
	u16 g1_used;
	u16 g0_aval;
	u16 g1_aval;
	u16 pub_aval;
	u16 wp_aval;
};

struct rtw89_hfc_prec_cfg {
	u16 ch011_prec;
	u16 h2c_prec;
	u16 wp_ch07_prec;
	u16 wp_ch811_prec;
	u8 ch011_full_cond;
	u8 h2c_full_cond;
	u8 wp_ch07_full_cond;
	u8 wp_ch811_full_cond;
};

struct rtw89_hfc_param {
	bool en;
	bool h2c_en;
	u8 mode;
	const struct rtw89_hfc_ch_cfg *ch_cfg;
	struct rtw89_hfc_ch_info ch_info[RTW89_DMA_CH_NUM];
	struct rtw89_hfc_pub_cfg pub_cfg;
	struct rtw89_hfc_pub_info pub_info;
	struct rtw89_hfc_prec_cfg prec_cfg;
};

struct rtw89_hfc_param_ini {
	const struct rtw89_hfc_ch_cfg *ch_cfg;
	const struct rtw89_hfc_pub_cfg *pub_cfg;
	const struct rtw89_hfc_prec_cfg *prec_cfg;
	u8 mode;
};

struct rtw89_dle_size {
	u16 pge_size;
	u16 lnk_pge_num;
	u16 unlnk_pge_num;
};

struct rtw89_wde_quota {
	u16 hif;
	u16 wcpu;
	u16 pkt_in;
	u16 cpu_io;
};

struct rtw89_ple_quota {
	u16 cma0_tx;
	u16 cma1_tx;
	u16 c2h;
	u16 h2c;
	u16 wcpu;
	u16 mpdu_proc;
	u16 cma0_dma;
	u16 cma1_dma;
	u16 bb_rpt;
	u16 wd_rel;
	u16 cpu_io;
};

struct rtw89_dle_mem {
	enum rtw89_qta_mode mode;
	const struct rtw89_dle_size *wde_size;
	const struct rtw89_dle_size *ple_size;
	const struct rtw89_wde_quota *wde_min_qt;
	const struct rtw89_wde_quota *wde_max_qt;
	const struct rtw89_ple_quota *ple_min_qt;
	const struct rtw89_ple_quota *ple_max_qt;
};

struct rtw89_reg_def {
	u32 addr;
	u32 mask;
};

struct rtw89_reg2_def {
	u32 addr;
	u32 data;
};

struct rtw89_reg3_def {
	u32 addr;
	u32 mask;
	u32 data;
};

struct rtw89_reg5_def {
	u8 flag; /* recognized by parsers */
	u8 path;
	u32 addr;
	u32 mask;
	u32 data;
};

struct rtw89_phy_table {
	const struct rtw89_reg2_def *regs;
	u32 n_regs;
	enum rtw89_rf_path rf_path;
};

struct rtw89_txpwr_table {
	const void *data;
	u32 size;
	void (*load)(struct rtw89_dev *rtwdev,
		     const struct rtw89_txpwr_table *tbl);
};

struct rtw89_chip_info {
	enum rtw89_core_chip_id chip_id;
	const struct rtw89_chip_ops *ops;
	const char *fw_name;
	u32 fifo_size;
	u16 max_amsdu_limit;
	bool dis_2g_40m_ul_ofdma;
	const struct rtw89_hfc_param_ini *hfc_param_ini;
	const struct rtw89_dle_mem *dle_mem;
	u32 rf_base_addr[2];
	u8 support_bands;
	u8 rf_path_num;
	u8 tx_nss;
	u8 rx_nss;
	u8 acam_num;
	u8 bcam_num;
	u8 scam_num;

	u8 sec_ctrl_efuse_size;
	u32 physical_efuse_size;
	u32 logical_efuse_size;
	u32 limit_efuse_size;
	u32 phycap_addr;
	u32 phycap_size;

	const struct rtw89_pwr_cfg * const *pwr_on_seq;
	const struct rtw89_pwr_cfg * const *pwr_off_seq;
	const struct rtw89_phy_table *bb_table;
	const struct rtw89_phy_table *rf_table[RF_PATH_MAX];
	const struct rtw89_phy_table *nctl_table;
	const struct rtw89_txpwr_table *byr_table;
	const struct rtw89_phy_dig_gain_table *dig_table;
	const s8 (*txpwr_lmt_2g)[RTW89_2G_BW_NUM][RTW89_NTX_NUM]
				[RTW89_RS_LMT_NUM][RTW89_BF_NUM]
				[RTW89_REGD_NUM][RTW89_2G_CH_NUM];
	const s8 (*txpwr_lmt_5g)[RTW89_5G_BW_NUM][RTW89_NTX_NUM]
				[RTW89_RS_LMT_NUM][RTW89_BF_NUM]
				[RTW89_REGD_NUM][RTW89_5G_CH_NUM];
	const s8 (*txpwr_lmt_ru_2g)[RTW89_RU_NUM][RTW89_NTX_NUM]
				   [RTW89_REGD_NUM][RTW89_2G_CH_NUM];
	const s8 (*txpwr_lmt_ru_5g)[RTW89_RU_NUM][RTW89_NTX_NUM]
				   [RTW89_REGD_NUM][RTW89_5G_CH_NUM];

	u8 txpwr_factor_rf;
	u8 txpwr_factor_mac;

	u32 para_ver;
	u32 wlcx_desired;
	u8 btcx_desired;
	u8 scbd;
	u8 mailbox;

	u8 afh_guard_ch;
	const u8 *wl_rssi_thres;
	const u8 *bt_rssi_thres;
	u8 rssi_tol;

	u8 mon_reg_num;
	const struct rtw89_btc_fbtc_mreg *mon_reg;
	u8 rf_para_ulink_num;
	const struct rtw89_btc_rf_trx_para *rf_para_ulink;
	u8 rf_para_dlink_num;
	const struct rtw89_btc_rf_trx_para *rf_para_dlink;
	u8 ps_mode_supported;
};

struct rtw89_driver_info {
	const struct rtw89_chip_info *chip;
};

enum rtw89_hcifc_mode {
	RTW89_HCIFC_POH = 0,
	RTW89_HCIFC_STF = 1,
	RTW89_HCIFC_SDIO = 2,

	/* keep last */
	RTW89_HCIFC_MODE_INVALID,
};

struct rtw89_dle_info {
	enum rtw89_qta_mode qta_mode;
	u16 wde_pg_size;
	u16 ple_pg_size;
	u16 c0_rx_qta;
	u16 c1_rx_qta;
};

enum rtw89_host_rpr_mode {
	RTW89_RPR_MODE_POH = 0,
	RTW89_RPR_MODE_STF
};

struct rtw89_mac_info {
	struct rtw89_dle_info dle_info;
	struct rtw89_hfc_param hfc_param;
	enum rtw89_qta_mode qta_mode;
	u8 rpwm_seq_num;
	u8 cpwm_seq_num;
};

enum rtw89_fw_type {
	RTW89_FW_NORMAL = 1,
	RTW89_FW_WOWLAN = 3,
};

struct rtw89_fw_suit {
	const u8 *data;
	u32 size;
	u8 major_ver;
	u8 minor_ver;
	u8 sub_ver;
	u8 sub_idex;
	u16 build_year;
	u16 build_mon;
	u16 build_date;
	u16 build_hour;
	u16 build_min;
	u8 cmd_ver;
};

#define RTW89_FW_VER_CODE(major, minor, sub, idx)	\
	(((major) << 24) | ((minor) << 16) | ((sub) << 8) | (idx))
#define RTW89_FW_SUIT_VER_CODE(s)	\
	RTW89_FW_VER_CODE((s)->major_ver, (s)->minor_ver, (s)->sub_ver, (s)->sub_idex)

struct rtw89_fw_info {
	const struct firmware *firmware;
	struct rtw89_dev *rtwdev;
	struct completion completion;
	u8 h2c_seq;
	u8 rec_seq;
	struct rtw89_fw_suit normal;
	struct rtw89_fw_suit wowlan;
	bool fw_log_enable;
	bool old_ht_ra_format;
};

struct rtw89_cam_info {
	DECLARE_BITMAP(addr_cam_map, RTW89_MAX_ADDR_CAM_NUM);
	DECLARE_BITMAP(bssid_cam_map, RTW89_MAX_BSSID_CAM_NUM);
	DECLARE_BITMAP(sec_cam_map, RTW89_MAX_SEC_CAM_NUM);
};

enum rtw89_sar_sources {
	RTW89_SAR_SOURCE_NONE,
	RTW89_SAR_SOURCE_COMMON,

	RTW89_SAR_SOURCE_NR,
};

struct rtw89_sar_cfg_common {
	bool set[RTW89_SUBBAND_NR];
	s32 cfg[RTW89_SUBBAND_NR];
};

struct rtw89_sar_info {
	/* used to decide how to acces SAR cfg union */
	enum rtw89_sar_sources src;

	/* reserved for different knids of SAR cfg struct.
	 * supposed that a single cfg struct cannot handle various SAR sources.
	 */
	union {
		struct rtw89_sar_cfg_common cfg_common;
	};
};

struct rtw89_hal {
	u32 rx_fltr;
	u8 cv;
	u8 current_channel;
	u8 prev_primary_channel;
	u8 current_primary_channel;
	enum rtw89_subband current_subband;
	u8 current_band_width;
	u8 current_band_type;
	u32 sw_amsdu_max_size;
	u32 antenna_tx;
	u32 antenna_rx;
	u8 tx_nss;
	u8 rx_nss;
	bool support_cckpd;
};

#define RTW89_MAX_MAC_ID_NUM 128

enum rtw89_flags {
	RTW89_FLAG_POWERON,
	RTW89_FLAG_FW_RDY,
	RTW89_FLAG_RUNNING,
	RTW89_FLAG_BFEE_MON,
	RTW89_FLAG_BFEE_EN,
	RTW89_FLAG_NAPI_RUNNING,
	RTW89_FLAG_LEISURE_PS,
	RTW89_FLAG_LOW_POWER_MODE,
	RTW89_FLAG_INACTIVE_PS,

	NUM_OF_RTW89_FLAGS,
};

struct rtw89_pkt_stat {
	u16 beacon_nr;
	u32 rx_rate_cnt[RTW89_HW_RATE_NR];
};

DECLARE_EWMA(thermal, 4, 4);

struct rtw89_phy_stat {
	struct ewma_thermal avg_thermal[RF_PATH_MAX];
	struct rtw89_pkt_stat cur_pkt_stat;
	struct rtw89_pkt_stat last_pkt_stat;
};

#define RTW89_DACK_PATH_NR 2
#define RTW89_DACK_IDX_NR 2
#define RTW89_DACK_MSBK_NR 16
struct rtw89_dack_info {
	bool dack_done;
	u8 msbk_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR][RTW89_DACK_MSBK_NR];
	u8 dadck_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR];
	u16 addck_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR];
	u16 biask_d[RTW89_DACK_PATH_NR][RTW89_DACK_IDX_NR];
	u32 dack_cnt;
	bool addck_timeout[RTW89_DACK_PATH_NR];
	bool dadck_timeout[RTW89_DACK_PATH_NR];
	bool msbk_timeout[RTW89_DACK_PATH_NR];
};

#define RTW89_IQK_CHS_NR 2
#define RTW89_IQK_PATH_NR 4
struct rtw89_iqk_info {
	bool lok_cor_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool lok_fin_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool iqk_tx_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool iqk_rx_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u32 iqk_fail_cnt;
	bool is_iqk_init;
	u32 iqk_channel[RTW89_IQK_CHS_NR];
	u8 iqk_band[RTW89_IQK_PATH_NR];
	u8 iqk_ch[RTW89_IQK_PATH_NR];
	u8 iqk_bw[RTW89_IQK_PATH_NR];
	u8 kcount;
	u8 iqk_times;
	u8 version;
	u32 nb_txcfir[RTW89_IQK_PATH_NR];
	u32 nb_rxcfir[RTW89_IQK_PATH_NR];
	u32 bp_txkresult[RTW89_IQK_PATH_NR];
	u32 bp_rxkresult[RTW89_IQK_PATH_NR];
	u32 bp_iqkenable[RTW89_IQK_PATH_NR];
	bool is_wb_txiqk[RTW89_IQK_PATH_NR];
	bool is_wb_rxiqk[RTW89_IQK_PATH_NR];
	bool is_nbiqk;
	bool iqk_fft_en;
	bool iqk_xym_en;
	bool iqk_sram_en;
	bool iqk_cfir_en;
	u8 thermal[RTW89_IQK_PATH_NR];
	bool thermal_rek_en;
	u32 syn1to2;
	u8 iqk_mcc_ch[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u8 iqk_table_idx[RTW89_IQK_PATH_NR];
};

#define RTW89_DPK_RF_PATH 2
#define RTW89_DPK_AVG_THERMAL_NUM 8
#define RTW89_DPK_BKUP_NUM 2
struct rtw89_dpk_bkup_para {
	enum rtw89_band band;
	enum rtw89_bandwidth bw;
	u8 ch;
	bool path_ok;
	u8 txagc_dpk;
	u8 ther_dpk;
	u8 gs;
	u16 pwsf;
};

struct rtw89_dpk_info {
	bool is_dpk_enable;
	bool is_dpk_reload_en;
	u16 dc_i[RTW89_DPK_RF_PATH];
	u16 dc_q[RTW89_DPK_RF_PATH];
	u8 corr_val[RTW89_DPK_RF_PATH];
	u8 corr_idx[RTW89_DPK_RF_PATH];
	u8 cur_idx[RTW89_DPK_RF_PATH];
	struct rtw89_dpk_bkup_para bp[RTW89_DPK_RF_PATH][RTW89_DPK_BKUP_NUM];
};

struct rtw89_fem_info {
	bool elna_2g;
	bool elna_5g;
	bool epa_2g;
	bool epa_5g;
};

struct rtw89_phy_ch_info {
	u8 rssi_min;
	u16 rssi_min_macid;
	u8 pre_rssi_min;
	u8 rssi_max;
	u16 rssi_max_macid;
	u8 rxsc_160;
	u8 rxsc_80;
	u8 rxsc_40;
	u8 rxsc_20;
	u8 rxsc_l;
	u8 is_noisy;
};

struct rtw89_agc_gaincode_set {
	u8 lna_idx;
	u8 tia_idx;
	u8 rxb_idx;
};

#define IGI_RSSI_TH_NUM 5
#define FA_TH_NUM 4
#define LNA_GAIN_NUM 7
#define TIA_GAIN_NUM 2
struct rtw89_dig_info {
	struct rtw89_agc_gaincode_set cur_gaincode;
	bool force_gaincode_idx_en;
	struct rtw89_agc_gaincode_set force_gaincode;
	u8 igi_rssi_th[IGI_RSSI_TH_NUM];
	u16 fa_th[FA_TH_NUM];
	u8 igi_rssi;
	u8 igi_fa_rssi;
	u8 fa_rssi_ofst;
	u8 dyn_igi_max;
	u8 dyn_igi_min;
	bool dyn_pd_th_en;
	u8 dyn_pd_th_max;
	u8 pd_low_th_ofst;
	u8 ib_pbk;
	s8 ib_pkpwr;
	s8 lna_gain_a[LNA_GAIN_NUM];
	s8 lna_gain_g[LNA_GAIN_NUM];
	s8 *lna_gain;
	s8 tia_gain_a[TIA_GAIN_NUM];
	s8 tia_gain_g[TIA_GAIN_NUM];
	s8 *tia_gain;
	bool is_linked_pre;
	bool bypass_dig;
};

enum rtw89_multi_cfo_mode {
	RTW89_PKT_BASED_AVG_MODE = 0,
	RTW89_ENTRY_BASED_AVG_MODE = 1,
	RTW89_TP_BASED_AVG_MODE = 2,
};

enum rtw89_phy_cfo_status {
	RTW89_PHY_DCFO_STATE_NORMAL = 0,
	RTW89_PHY_DCFO_STATE_ENHANCE = 1,
	RTW89_PHY_DCFO_STATE_MAX
};

struct rtw89_cfo_tracking_info {
	u16 cfo_timer_ms;
	bool cfo_trig_by_timer_en;
	enum rtw89_phy_cfo_status phy_cfo_status;
	u8 phy_cfo_trk_cnt;
	bool is_adjust;
	enum rtw89_multi_cfo_mode rtw89_multi_cfo_mode;
	bool apply_compensation;
	u8 crystal_cap;
	u8 crystal_cap_default;
	u8 def_x_cap;
	s8 x_cap_ofst;
	u32 sta_cfo_tolerance;
	s32 cfo_tail[CFO_TRACK_MAX_USER];
	u16 cfo_cnt[CFO_TRACK_MAX_USER];
	s32 cfo_avg_pre;
	s32 cfo_avg[CFO_TRACK_MAX_USER];
	s32 pre_cfo_avg[CFO_TRACK_MAX_USER];
	u32 packet_count;
	u32 packet_count_pre;
	s32 residual_cfo_acc;
	u8 phy_cfotrk_state;
	u8 phy_cfotrk_cnt;
};

/* 2GL, 2GH, 5GL1, 5GH1, 5GM1, 5GM2, 5GH1, 5GH2 */
#define TSSI_TRIM_CH_GROUP_NUM 8

#define TSSI_CCK_CH_GROUP_NUM 6
#define TSSI_MCS_2G_CH_GROUP_NUM 5
#define TSSI_MCS_5G_CH_GROUP_NUM 14
#define TSSI_MCS_CH_GROUP_NUM \
	(TSSI_MCS_2G_CH_GROUP_NUM + TSSI_MCS_5G_CH_GROUP_NUM)

struct rtw89_tssi_info {
	u8 thermal[RF_PATH_MAX];
	s8 tssi_trim[RF_PATH_MAX][TSSI_TRIM_CH_GROUP_NUM];
	s8 tssi_cck[RF_PATH_MAX][TSSI_CCK_CH_GROUP_NUM];
	s8 tssi_mcs[RF_PATH_MAX][TSSI_MCS_CH_GROUP_NUM];
	s8 extra_ofst[RF_PATH_MAX];
	bool tssi_tracking_check[RF_PATH_MAX];
	u8 default_txagc_offset[RF_PATH_MAX];
	u32 base_thermal[RF_PATH_MAX];
};

struct rtw89_power_trim_info {
	bool pg_thermal_trim;
	bool pg_pa_bias_trim;
	u8 thermal_trim[RF_PATH_MAX];
	u8 pa_bias_trim[RF_PATH_MAX];
};

struct rtw89_regulatory {
	char alpha2[3];
	u8 txpwr_regd[RTW89_BAND_MAX];
};

enum rtw89_ifs_clm_application {
	RTW89_IFS_CLM_INIT = 0,
	RTW89_IFS_CLM_BACKGROUND = 1,
	RTW89_IFS_CLM_ACS = 2,
	RTW89_IFS_CLM_DIG = 3,
	RTW89_IFS_CLM_TDMA_DIG = 4,
	RTW89_IFS_CLM_DBG = 5,
	RTW89_IFS_CLM_DBG_MANUAL = 6
};

enum rtw89_env_racing_lv {
	RTW89_RAC_RELEASE = 0,
	RTW89_RAC_LV_1 = 1,
	RTW89_RAC_LV_2 = 2,
	RTW89_RAC_LV_3 = 3,
	RTW89_RAC_LV_4 = 4,
	RTW89_RAC_MAX_NUM = 5
};

struct rtw89_ccx_para_info {
	enum rtw89_env_racing_lv rac_lv;
	u16 mntr_time;
	u8 nhm_manual_th_ofst;
	u8 nhm_manual_th0;
	enum rtw89_ifs_clm_application ifs_clm_app;
	u32 ifs_clm_manual_th_times;
	u32 ifs_clm_manual_th0;
	u8 fahm_manual_th_ofst;
	u8 fahm_manual_th0;
	u8 fahm_numer_opt;
	u8 fahm_denom_opt;
};

enum rtw89_ccx_edcca_opt_sc_idx {
	RTW89_CCX_EDCCA_SEG0_P0 = 0,
	RTW89_CCX_EDCCA_SEG0_S1 = 1,
	RTW89_CCX_EDCCA_SEG0_S2 = 2,
	RTW89_CCX_EDCCA_SEG0_S3 = 3,
	RTW89_CCX_EDCCA_SEG1_P0 = 4,
	RTW89_CCX_EDCCA_SEG1_S1 = 5,
	RTW89_CCX_EDCCA_SEG1_S2 = 6,
	RTW89_CCX_EDCCA_SEG1_S3 = 7
};

enum rtw89_ccx_edcca_opt_bw_idx {
	RTW89_CCX_EDCCA_BW20_0 = 0,
	RTW89_CCX_EDCCA_BW20_1 = 1,
	RTW89_CCX_EDCCA_BW20_2 = 2,
	RTW89_CCX_EDCCA_BW20_3 = 3,
	RTW89_CCX_EDCCA_BW20_4 = 4,
	RTW89_CCX_EDCCA_BW20_5 = 5,
	RTW89_CCX_EDCCA_BW20_6 = 6,
	RTW89_CCX_EDCCA_BW20_7 = 7
};

#define RTW89_NHM_TH_NUM 11
#define RTW89_FAHM_TH_NUM 11
#define RTW89_NHM_RPT_NUM 12
#define RTW89_FAHM_RPT_NUM 12
#define RTW89_IFS_CLM_NUM 4
struct rtw89_env_monitor_info {
	u32 ccx_trigger_time;
	u64 start_time;
	u8 ccx_rpt_stamp;
	u8 ccx_watchdog_result;
	bool ccx_ongoing;
	u8 ccx_rac_lv;
	bool ccx_manual_ctrl;
	u8 ccx_pre_rssi;
	u16 clm_mntr_time;
	u16 nhm_mntr_time;
	u16 ifs_clm_mntr_time;
	enum rtw89_ifs_clm_application ifs_clm_app;
	u16 fahm_mntr_time;
	u16 edcca_clm_mntr_time;
	u16 ccx_period;
	u8 ccx_unit_idx;
	enum rtw89_ccx_edcca_opt_bw_idx ccx_edcca_opt_bw_idx;
	u8 nhm_th[RTW89_NHM_TH_NUM];
	u16 ifs_clm_th_l[RTW89_IFS_CLM_NUM];
	u16 ifs_clm_th_h[RTW89_IFS_CLM_NUM];
	u8 fahm_numer_opt;
	u8 fahm_denom_opt;
	u8 fahm_th[RTW89_FAHM_TH_NUM];
	u16 clm_result;
	u16 nhm_result[RTW89_NHM_RPT_NUM];
	u8 nhm_wgt[RTW89_NHM_RPT_NUM];
	u16 nhm_tx_cnt;
	u16 nhm_cca_cnt;
	u16 nhm_idle_cnt;
	u16 ifs_clm_tx;
	u16 ifs_clm_edcca_excl_cca;
	u16 ifs_clm_ofdmfa;
	u16 ifs_clm_ofdmcca_excl_fa;
	u16 ifs_clm_cckfa;
	u16 ifs_clm_cckcca_excl_fa;
	u16 ifs_clm_total_ifs;
	u8 ifs_clm_his[RTW89_IFS_CLM_NUM];
	u16 ifs_clm_avg[RTW89_IFS_CLM_NUM];
	u16 ifs_clm_cca[RTW89_IFS_CLM_NUM];
	u16 fahm_result[RTW89_FAHM_RPT_NUM];
	u16 fahm_denom_result;
	u16 edcca_clm_result;
	u8 clm_ratio;
	u8 nhm_rpt[RTW89_NHM_RPT_NUM];
	u8 nhm_tx_ratio;
	u8 nhm_cca_ratio;
	u8 nhm_idle_ratio;
	u8 nhm_ratio;
	u16 nhm_result_sum;
	u8 nhm_pwr;
	u8 ifs_clm_tx_ratio;
	u8 ifs_clm_edcca_excl_cca_ratio;
	u8 ifs_clm_cck_fa_ratio;
	u8 ifs_clm_ofdm_fa_ratio;
	u8 ifs_clm_cck_cca_excl_fa_ratio;
	u8 ifs_clm_ofdm_cca_excl_fa_ratio;
	u16 ifs_clm_cck_fa_permil;
	u16 ifs_clm_ofdm_fa_permil;
	u32 ifs_clm_ifs_avg[RTW89_IFS_CLM_NUM];
	u32 ifs_clm_cca_avg[RTW89_IFS_CLM_NUM];
	u8 fahm_rpt[RTW89_FAHM_RPT_NUM];
	u16 fahm_result_sum;
	u8 fahm_ratio;
	u8 fahm_denom_ratio;
	u8 fahm_pwr;
	u8 edcca_clm_ratio;
};

enum rtw89_ser_rcvy_step {
	RTW89_SER_DRV_STOP_TX,
	RTW89_SER_DRV_STOP_RX,
	RTW89_SER_DRV_STOP_RUN,
	RTW89_SER_HAL_STOP_DMA,
	RTW89_NUM_OF_SER_FLAGS
};

struct rtw89_ser {
	u8 state;
	u8 alarm_event;

	struct work_struct ser_hdl_work;
	struct delayed_work ser_alarm_work;
	struct state_ent *st_tbl;
	struct event_ent *ev_tbl;
	struct list_head msg_q;
	spinlock_t msg_q_lock; /* lock when read/write ser msg */
	DECLARE_BITMAP(flags, RTW89_NUM_OF_SER_FLAGS);
};

enum rtw89_mac_ax_ps_mode {
	RTW89_MAC_AX_PS_MODE_ACTIVE = 0,
	RTW89_MAC_AX_PS_MODE_LEGACY = 1,
	RTW89_MAC_AX_PS_MODE_WMMPS  = 2,
	RTW89_MAC_AX_PS_MODE_MAX    = 3,
};

enum rtw89_last_rpwm_mode {
	RTW89_LAST_RPWM_PS        = 0x0,
	RTW89_LAST_RPWM_ACTIVE    = 0x6,
};

struct rtw89_lps_parm {
	u8 macid;
	u8 psmode; /* enum rtw89_mac_ax_ps_mode */
	u8 lastrpwm; /* enum rtw89_last_rpwm_mode */
};

struct rtw89_ppdu_sts_info {
	struct sk_buff_head rx_queue[RTW89_PHY_MAX];
	u8 curr_rx_ppdu_cnt[RTW89_PHY_MAX];
};

struct rtw89_early_h2c {
	struct list_head list;
	u8 *h2c;
	u16 h2c_len;
};

struct rtw89_dev {
	struct ieee80211_hw *hw;
	struct device *dev;

	bool dbcc_en;
	const struct rtw89_chip_info *chip;
	struct rtw89_hal hal;
	struct rtw89_mac_info mac;
	struct rtw89_fw_info fw;
	struct rtw89_hci_info hci;
	struct rtw89_efuse efuse;
	struct rtw89_traffic_stats stats;

	/* ensures exclusive access from mac80211 callbacks */
	struct mutex mutex;
	struct list_head rtwvifs_list;
	/* used to protect rf read write */
	struct mutex rf_mutex;
	struct workqueue_struct *txq_wq;
	struct work_struct txq_work;
	struct delayed_work txq_reinvoke_work;
	/* used to protect ba_list */
	spinlock_t ba_lock;
	/* txqs to setup ba session */
	struct list_head ba_list;
	struct work_struct ba_work;

	struct rtw89_cam_info cam_info;

	struct sk_buff_head c2h_queue;
	struct work_struct c2h_work;

	struct list_head early_h2c_list;

	struct rtw89_ser ser;

	DECLARE_BITMAP(hw_port, RTW89_PORT_NUM);
	DECLARE_BITMAP(mac_id_map, RTW89_MAX_MAC_ID_NUM);
	DECLARE_BITMAP(flags, NUM_OF_RTW89_FLAGS);

	struct rtw89_phy_stat phystat;
	struct rtw89_dack_info dack;
	struct rtw89_iqk_info iqk;
	struct rtw89_dpk_info dpk;
	bool is_tssi_mode[RF_PATH_MAX];
	bool is_bt_iqk_timeout;

	struct rtw89_fem_info fem;
	struct rtw89_txpwr_byrate byr[RTW89_BAND_MAX];
	struct rtw89_tssi_info tssi;
	struct rtw89_power_trim_info pwr_trim;

	struct rtw89_cfo_tracking_info cfo_tracking;
	struct rtw89_env_monitor_info env_monitor;
	struct rtw89_dig_info dig;
	struct rtw89_phy_ch_info ch_info;
	struct delayed_work track_work;
	struct delayed_work coex_act1_work;
	struct delayed_work coex_bt_devinfo_work;
	struct delayed_work coex_rfk_chk_work;
	struct delayed_work cfo_track_work;
	struct rtw89_ppdu_sts_info ppdu_sts;
	u8 total_sta_assoc;
	bool scanning;

	const struct rtw89_regulatory *regd;
	struct rtw89_sar_info sar;

	struct rtw89_btc btc;
	enum rtw89_ps_mode ps_mode;
	bool lps_enabled;

	/* napi structure */
	struct net_device netdev;
	struct napi_struct napi;
	int napi_budget_countdown;

	/* HCI related data, keep last */
	u8 priv[0] __aligned(sizeof(void *));
};

static inline int rtw89_hci_tx_write(struct rtw89_dev *rtwdev,
				     struct rtw89_core_tx_request *tx_req)
{
	return rtwdev->hci.ops->tx_write(rtwdev, tx_req);
}

static inline void rtw89_hci_reset(struct rtw89_dev *rtwdev)
{
	rtwdev->hci.ops->reset(rtwdev);
}

static inline int rtw89_hci_start(struct rtw89_dev *rtwdev)
{
	return rtwdev->hci.ops->start(rtwdev);
}

static inline void rtw89_hci_stop(struct rtw89_dev *rtwdev)
{
	rtwdev->hci.ops->stop(rtwdev);
}

static inline int rtw89_hci_deinit(struct rtw89_dev *rtwdev)
{
	return rtwdev->hci.ops->deinit(rtwdev);
}

static inline void rtw89_hci_recalc_int_mit(struct rtw89_dev *rtwdev)
{
	rtwdev->hci.ops->recalc_int_mit(rtwdev);
}

static inline u32 rtw89_hci_check_and_reclaim_tx_resource(struct rtw89_dev *rtwdev, u8 txch)
{
	return rtwdev->hci.ops->check_and_reclaim_tx_resource(rtwdev, txch);
}

static inline void rtw89_hci_tx_kick_off(struct rtw89_dev *rtwdev, u8 txch)
{
	return rtwdev->hci.ops->tx_kick_off(rtwdev, txch);
}

static inline void rtw89_hci_flush_queues(struct rtw89_dev *rtwdev, u32 queues,
					  bool drop)
{
	if (rtwdev->hci.ops->flush_queues)
		return rtwdev->hci.ops->flush_queues(rtwdev, queues, drop);
}

static inline u8 rtw89_read8(struct rtw89_dev *rtwdev, u32 addr)
{
	return rtwdev->hci.ops->read8(rtwdev, addr);
}

static inline u16 rtw89_read16(struct rtw89_dev *rtwdev, u32 addr)
{
	return rtwdev->hci.ops->read16(rtwdev, addr);
}

static inline u32 rtw89_read32(struct rtw89_dev *rtwdev, u32 addr)
{
	return rtwdev->hci.ops->read32(rtwdev, addr);
}

static inline void rtw89_write8(struct rtw89_dev *rtwdev, u32 addr, u8 data)
{
	rtwdev->hci.ops->write8(rtwdev, addr, data);
}

static inline void rtw89_write16(struct rtw89_dev *rtwdev, u32 addr, u16 data)
{
	rtwdev->hci.ops->write16(rtwdev, addr, data);
}

static inline void rtw89_write32(struct rtw89_dev *rtwdev, u32 addr, u32 data)
{
	rtwdev->hci.ops->write32(rtwdev, addr, data);
}

static inline void
rtw89_write8_set(struct rtw89_dev *rtwdev, u32 addr, u8 bit)
{
	u8 val;

	val = rtw89_read8(rtwdev, addr);
	rtw89_write8(rtwdev, addr, val | bit);
}

static inline void
rtw89_write16_set(struct rtw89_dev *rtwdev, u32 addr, u16 bit)
{
	u16 val;

	val = rtw89_read16(rtwdev, addr);
	rtw89_write16(rtwdev, addr, val | bit);
}

static inline void
rtw89_write32_set(struct rtw89_dev *rtwdev, u32 addr, u32 bit)
{
	u32 val;

	val = rtw89_read32(rtwdev, addr);
	rtw89_write32(rtwdev, addr, val | bit);
}

static inline void
rtw89_write8_clr(struct rtw89_dev *rtwdev, u32 addr, u8 bit)
{
	u8 val;

	val = rtw89_read8(rtwdev, addr);
	rtw89_write8(rtwdev, addr, val & ~bit);
}

static inline void
rtw89_write16_clr(struct rtw89_dev *rtwdev, u32 addr, u16 bit)
{
	u16 val;

	val = rtw89_read16(rtwdev, addr);
	rtw89_write16(rtwdev, addr, val & ~bit);
}

static inline void
rtw89_write32_clr(struct rtw89_dev *rtwdev, u32 addr, u32 bit)
{
	u32 val;

	val = rtw89_read32(rtwdev, addr);
	rtw89_write32(rtwdev, addr, val & ~bit);
}

static inline u32
rtw89_read32_mask(struct rtw89_dev *rtwdev, u32 addr, u32 mask)
{
	u32 shift = __ffs(mask);
	u32 orig;
	u32 ret;

	orig = rtw89_read32(rtwdev, addr);
	ret = (orig & mask) >> shift;

	return ret;
}

static inline u16
rtw89_read16_mask(struct rtw89_dev *rtwdev, u32 addr, u32 mask)
{
	u32 shift = __ffs(mask);
	u32 orig;
	u32 ret;

	orig = rtw89_read16(rtwdev, addr);
	ret = (orig & mask) >> shift;

	return ret;
}

static inline u8
rtw89_read8_mask(struct rtw89_dev *rtwdev, u32 addr, u32 mask)
{
	u32 shift = __ffs(mask);
	u32 orig;
	u32 ret;

	orig = rtw89_read8(rtwdev, addr);
	ret = (orig & mask) >> shift;

	return ret;
}

static inline void
rtw89_write32_mask(struct rtw89_dev *rtwdev, u32 addr, u32 mask, u32 data)
{
	u32 shift = __ffs(mask);
	u32 orig;
	u32 set;

	WARN(addr & 0x3, "should be 4-byte aligned, addr = 0x%08x\n", addr);

	orig = rtw89_read32(rtwdev, addr);
	set = (orig & ~mask) | ((data << shift) & mask);
	rtw89_write32(rtwdev, addr, set);
}

static inline void
rtw89_write16_mask(struct rtw89_dev *rtwdev, u32 addr, u32 mask, u16 data)
{
	u32 shift;
	u16 orig, set;

	mask &= 0xffff;
	shift = __ffs(mask);

	orig = rtw89_read16(rtwdev, addr);
	set = (orig & ~mask) | ((data << shift) & mask);
	rtw89_write16(rtwdev, addr, set);
}

static inline void
rtw89_write8_mask(struct rtw89_dev *rtwdev, u32 addr, u32 mask, u8 data)
{
	u32 shift;
	u8 orig, set;

	mask &= 0xff;
	shift = __ffs(mask);

	orig = rtw89_read8(rtwdev, addr);
	set = (orig & ~mask) | ((data << shift) & mask);
	rtw89_write8(rtwdev, addr, set);
}

static inline u32
rtw89_read_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
	      u32 addr, u32 mask)
{
	u32 val;

	mutex_lock(&rtwdev->rf_mutex);
	val = rtwdev->chip->ops->read_rf(rtwdev, rf_path, addr, mask);
	mutex_unlock(&rtwdev->rf_mutex);

	return val;
}

static inline void
rtw89_write_rf(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
	       u32 addr, u32 mask, u32 data)
{
	mutex_lock(&rtwdev->rf_mutex);
	rtwdev->chip->ops->write_rf(rtwdev, rf_path, addr, mask, data);
	mutex_unlock(&rtwdev->rf_mutex);
}

static inline struct ieee80211_txq *rtw89_txq_to_txq(struct rtw89_txq *rtwtxq)
{
	void *p = rtwtxq;

	return container_of(p, struct ieee80211_txq, drv_priv);
}

static inline void rtw89_core_txq_init(struct rtw89_dev *rtwdev,
				       struct ieee80211_txq *txq)
{
	struct rtw89_txq *rtwtxq;

	if (!txq)
		return;

	rtwtxq = (struct rtw89_txq *)txq->drv_priv;
	INIT_LIST_HEAD(&rtwtxq->list);
}

static inline struct ieee80211_vif *rtwvif_to_vif(struct rtw89_vif *rtwvif)
{
	void *p = rtwvif;

	return container_of(p, struct ieee80211_vif, drv_priv);
}

static inline struct ieee80211_sta *rtwsta_to_sta(struct rtw89_sta *rtwsta)
{
	void *p = rtwsta;

	return container_of(p, struct ieee80211_sta, drv_priv);
}

static inline struct ieee80211_sta *rtwsta_to_sta_safe(struct rtw89_sta *rtwsta)
{
	return rtwsta ? rtwsta_to_sta(rtwsta) : NULL;
}

static inline struct rtw89_sta *sta_to_rtwsta_safe(struct ieee80211_sta *sta)
{
	return sta ? (struct rtw89_sta *)sta->drv_priv : NULL;
}

static inline
struct rtw89_addr_cam_entry *rtw89_get_addr_cam_of(struct rtw89_vif *rtwvif,
						   struct rtw89_sta *rtwsta)
{
	if (rtwvif->net_type == RTW89_NET_TYPE_AP_MODE && rtwsta)
		return &rtwsta->addr_cam;
	return &rtwvif->addr_cam;
}

static inline
void rtw89_chip_set_channel_prepare(struct rtw89_dev *rtwdev,
				    struct rtw89_channel_help_params *p)
{
	rtwdev->chip->ops->set_channel_help(rtwdev, true, p);
}

static inline
void rtw89_chip_set_channel_done(struct rtw89_dev *rtwdev,
				 struct rtw89_channel_help_params *p)
{
	rtwdev->chip->ops->set_channel_help(rtwdev, false, p);
}

static inline void rtw89_chip_fem_setup(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->fem_setup)
		chip->ops->fem_setup(rtwdev);
}

static inline void rtw89_chip_bb_sethw(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->bb_sethw)
		chip->ops->bb_sethw(rtwdev);
}

static inline void rtw89_chip_rfk_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_init)
		chip->ops->rfk_init(rtwdev);
}

static inline void rtw89_chip_rfk_channel(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_channel)
		chip->ops->rfk_channel(rtwdev);
}

static inline void rtw89_chip_rfk_band_changed(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_band_changed)
		chip->ops->rfk_band_changed(rtwdev);
}

static inline void rtw89_chip_rfk_scan(struct rtw89_dev *rtwdev, bool start)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_scan)
		chip->ops->rfk_scan(rtwdev, start);
}

static inline void rtw89_chip_rfk_track(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_track)
		chip->ops->rfk_track(rtwdev);
}

static inline void rtw89_chip_set_txpwr_ctrl(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->set_txpwr_ctrl)
		chip->ops->set_txpwr_ctrl(rtwdev);
}

static inline void rtw89_chip_set_txpwr(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 ch = rtwdev->hal.current_channel;

	if (!ch)
		return;

	if (chip->ops->set_txpwr)
		chip->ops->set_txpwr(rtwdev);
}

static inline void rtw89_chip_power_trim(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->power_trim)
		chip->ops->power_trim(rtwdev);
}

static inline void rtw89_chip_init_txpwr_unit(struct rtw89_dev *rtwdev,
					      enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->init_txpwr_unit)
		chip->ops->init_txpwr_unit(rtwdev, phy_idx);
}

static inline u8 rtw89_chip_get_thermal(struct rtw89_dev *rtwdev,
					enum rtw89_rf_path rf_path)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!chip->ops->get_thermal)
		return 0x10;

	return chip->ops->get_thermal(rtwdev, rf_path);
}

static inline void rtw89_chip_query_ppdu(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_phy_ppdu *phy_ppdu,
					 struct ieee80211_rx_status *status)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->query_ppdu)
		chip->ops->query_ppdu(rtwdev, phy_ppdu, status);
}

static inline void rtw89_chip_bb_ctrl_btc_preagc(struct rtw89_dev *rtwdev,
						 bool bt_en)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->bb_ctrl_btc_preagc)
		chip->ops->bb_ctrl_btc_preagc(rtwdev, bt_en);
}

static inline
void rtw89_chip_cfg_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				       struct ieee80211_vif *vif)
{
	struct rtw89_vif *rtwvif = (struct rtw89_vif *)vif->drv_priv;
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!vif->bss_conf.he_support || !vif->bss_conf.assoc)
		return;

	if (chip->ops->set_txpwr_ul_tb_offset)
		chip->ops->set_txpwr_ul_tb_offset(rtwdev, 0, rtwvif->mac_idx);
}

static inline void rtw89_load_txpwr_table(struct rtw89_dev *rtwdev,
					  const struct rtw89_txpwr_table *tbl)
{
	tbl->load(rtwdev, tbl);
}

static inline u8 rtw89_regd_get(struct rtw89_dev *rtwdev, u8 band)
{
	return rtwdev->regd->txpwr_regd[band];
}

static inline void rtw89_ctrl_btg(struct rtw89_dev *rtwdev, bool btg)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->ctrl_btg)
		chip->ops->ctrl_btg(rtwdev, btg);
}

static inline u8 *get_hdr_bssid(struct ieee80211_hdr *hdr)
{
	__le16 fc = hdr->frame_control;

	if (ieee80211_has_tods(fc))
		return hdr->addr1;
	else if (ieee80211_has_fromds(fc))
		return hdr->addr2;
	else
		return hdr->addr3;
}

static inline bool rtw89_sta_has_beamformer_cap(struct ieee80211_sta *sta)
{
	if ((sta->vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) ||
	    (sta->vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE) ||
	    (sta->he_cap.he_cap_elem.phy_cap_info[3] & IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER) ||
	    (sta->he_cap.he_cap_elem.phy_cap_info[4] & IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER))
		return true;
	return false;
}

static inline struct rtw89_fw_suit *rtw89_fw_suit_get(struct rtw89_dev *rtwdev,
						      enum rtw89_fw_type type)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;

	if (type == RTW89_FW_WOWLAN)
		return &fw_info->wowlan;
	return &fw_info->normal;
}

int rtw89_core_tx_write(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, struct sk_buff *skb, int *qsel);
int rtw89_h2c_tx(struct rtw89_dev *rtwdev,
		 struct sk_buff *skb, bool fwdl);
void rtw89_core_tx_kick_off(struct rtw89_dev *rtwdev, u8 qsel);
void rtw89_core_fill_txdesc(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc);
void rtw89_core_rx(struct rtw89_dev *rtwdev,
		   struct rtw89_rx_desc_info *desc_info,
		   struct sk_buff *skb);
void rtw89_core_query_rxdesc(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset);
void rtw89_core_napi_start(struct rtw89_dev *rtwdev);
void rtw89_core_napi_stop(struct rtw89_dev *rtwdev);
void rtw89_core_napi_init(struct rtw89_dev *rtwdev);
void rtw89_core_napi_deinit(struct rtw89_dev *rtwdev);
int rtw89_core_sta_add(struct rtw89_dev *rtwdev,
		       struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
int rtw89_core_sta_assoc(struct rtw89_dev *rtwdev,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta);
int rtw89_core_sta_disassoc(struct rtw89_dev *rtwdev,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta);
int rtw89_core_sta_disconnect(struct rtw89_dev *rtwdev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta);
int rtw89_core_sta_remove(struct rtw89_dev *rtwdev,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);
int rtw89_core_init(struct rtw89_dev *rtwdev);
void rtw89_core_deinit(struct rtw89_dev *rtwdev);
int rtw89_core_register(struct rtw89_dev *rtwdev);
void rtw89_core_unregister(struct rtw89_dev *rtwdev);
void rtw89_set_channel(struct rtw89_dev *rtwdev);
u8 rtw89_core_acquire_bit_map(unsigned long *addr, unsigned long size);
void rtw89_core_release_bit_map(unsigned long *addr, u8 bit);
void rtw89_core_release_all_bits_map(unsigned long *addr, unsigned int nbits);
int rtw89_core_acquire_sta_ba_entry(struct rtw89_sta *rtwsta, u8 tid, u8 *cam_idx);
int rtw89_core_release_sta_ba_entry(struct rtw89_sta *rtwsta, u8 tid, u8 *cam_idx);
void rtw89_vif_type_mapping(struct ieee80211_vif *vif, bool assoc);
int rtw89_chip_info_setup(struct rtw89_dev *rtwdev);
u16 rtw89_ra_report_to_bitrate(struct rtw89_dev *rtwdev, u8 rpt_rate);
int rtw89_regd_init(struct rtw89_dev *rtwdev,
		    void (*reg_notifier)(struct wiphy *wiphy, struct regulatory_request *request));
void rtw89_regd_notifier(struct wiphy *wiphy, struct regulatory_request *request);
void rtw89_traffic_stats_init(struct rtw89_dev *rtwdev,
			      struct rtw89_traffic_stats *stats);
int rtw89_core_start(struct rtw89_dev *rtwdev);
void rtw89_core_stop(struct rtw89_dev *rtwdev);
void rtw89_core_update_beacon_work(struct work_struct *work);

#endif
