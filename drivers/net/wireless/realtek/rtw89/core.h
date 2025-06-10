/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_CORE_H__
#define __RTW89_CORE_H__

#include <linux/average.h>
#include <linux/bitfield.h>
#include <linux/dmi.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

struct rtw89_dev;
struct rtw89_pci_info;
struct rtw89_mac_gen_def;
struct rtw89_phy_gen_def;
struct rtw89_fw_blacklist;
struct rtw89_efuse_block_cfg;
struct rtw89_h2c_rf_tssi;
struct rtw89_fw_txpwr_track_cfg;
struct rtw89_phy_rfk_log_fmt;
struct rtw89_debugfs;
struct rtw89_regd_data;

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
#define BYPASS_CR_DATA 0xbabecafe

#define RTW89_TRACK_WORK_PERIOD	round_jiffies_relative(HZ * 2)
#define RTW89_FORBID_BA_TIMER round_jiffies_relative(HZ * 4)
#define CFO_TRACK_MAX_USER 64
#define MAX_RSSI 110
#define RSSI_FACTOR 1
#define RTW89_RSSI_RAW_TO_DBM(rssi) ((s8)((rssi) >> RSSI_FACTOR) - MAX_RSSI)
#define RTW89_TX_DIV_RSSI_RAW_TH (2 << RSSI_FACTOR)
#define DELTA_SWINGIDX_SIZE 30

#define RTW89_RADIOTAP_ROOM_HE sizeof(struct ieee80211_radiotap_he)
#define RTW89_RADIOTAP_ROOM_EHT \
	(sizeof(struct ieee80211_radiotap_tlv) + \
	 ALIGN(struct_size((struct ieee80211_radiotap_eht *)0, user_info, 1), 4) + \
	 sizeof(struct ieee80211_radiotap_tlv) + \
	 ALIGN(sizeof(struct ieee80211_radiotap_eht_usig), 4))
#define RTW89_RADIOTAP_ROOM \
	ALIGN(max(RTW89_RADIOTAP_ROOM_HE, RTW89_RADIOTAP_ROOM_EHT), 64)

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

#define RTW89_TF_PAD GENMASK(11, 0)
#define RTW89_TF_BASIC_USER_INFO_SZ 6

#define RTW89_GET_TF_USER_INFO_AID12(data)	\
	le32_get_bits(*((const __le32 *)(data)), GENMASK(11, 0))
#define RTW89_GET_TF_USER_INFO_RUA(data)	\
	le32_get_bits(*((const __le32 *)(data)), GENMASK(19, 12))
#define RTW89_GET_TF_USER_INFO_UL_MCS(data)	\
	le32_get_bits(*((const __le32 *)(data)), GENMASK(24, 21))

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
	RTW89_SUBBAND_2GHZ_5GHZ_NR = RTW89_CH_5G_BAND_4 + 1,
};

enum rtw89_gain_offset {
	RTW89_GAIN_OFFSET_2G_CCK,
	RTW89_GAIN_OFFSET_2G_OFDM,
	RTW89_GAIN_OFFSET_5G_LOW,
	RTW89_GAIN_OFFSET_5G_MID,
	RTW89_GAIN_OFFSET_5G_HIGH,
	RTW89_GAIN_OFFSET_6G_L0,
	RTW89_GAIN_OFFSET_6G_L1,
	RTW89_GAIN_OFFSET_6G_M0,
	RTW89_GAIN_OFFSET_6G_M1,
	RTW89_GAIN_OFFSET_6G_H0,
	RTW89_GAIN_OFFSET_6G_H1,
	RTW89_GAIN_OFFSET_6G_UH0,
	RTW89_GAIN_OFFSET_6G_UH1,

	RTW89_GAIN_OFFSET_NR,
};

enum rtw89_hci_type {
	RTW89_HCI_TYPE_PCIE,
	RTW89_HCI_TYPE_USB,
	RTW89_HCI_TYPE_SDIO,
};

enum rtw89_core_chip_id {
	RTL8852A,
	RTL8852B,
	RTL8852BT,
	RTL8852C,
	RTL8851B,
	RTL8922A,
};

enum rtw89_chip_gen {
	RTW89_CHIP_AX,
	RTW89_CHIP_BE,

	RTW89_CHIP_GEN_NUM,
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

enum rtw89_bacam_ver {
	RTW89_BACAM_V0,
	RTW89_BACAM_V1,

	RTW89_BACAM_V0_EXT = 99,
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
	RTW89_CORE_RX_TYPE_H2C		= 13,
	RTW89_CORE_RX_TYPE_FWDL		= 14,
};

enum rtw89_txq_flags {
	RTW89_TXQ_F_AMPDU		= 0,
	RTW89_TXQ_F_BLOCK_BA		= 1,
	RTW89_TXQ_F_FORBID_BA		= 2,
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
	RTW89_ROLE_CON_DISCONN,
	RTW89_ROLE_BAND_SW,
	RTW89_ROLE_FW_RESTORE,
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
	RTW89_BAND_NUM,
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

	RTW89_HW_RATE_V1_MCS0		= 0x100,
	RTW89_HW_RATE_V1_MCS1		= 0x101,
	RTW89_HW_RATE_V1_MCS2		= 0x102,
	RTW89_HW_RATE_V1_MCS3		= 0x103,
	RTW89_HW_RATE_V1_MCS4		= 0x104,
	RTW89_HW_RATE_V1_MCS5		= 0x105,
	RTW89_HW_RATE_V1_MCS6		= 0x106,
	RTW89_HW_RATE_V1_MCS7		= 0x107,
	RTW89_HW_RATE_V1_MCS8		= 0x108,
	RTW89_HW_RATE_V1_MCS9		= 0x109,
	RTW89_HW_RATE_V1_MCS10		= 0x10A,
	RTW89_HW_RATE_V1_MCS11		= 0x10B,
	RTW89_HW_RATE_V1_MCS12		= 0x10C,
	RTW89_HW_RATE_V1_MCS13		= 0x10D,
	RTW89_HW_RATE_V1_MCS14		= 0x10E,
	RTW89_HW_RATE_V1_MCS15		= 0x10F,
	RTW89_HW_RATE_V1_MCS16		= 0x110,
	RTW89_HW_RATE_V1_MCS17		= 0x111,
	RTW89_HW_RATE_V1_MCS18		= 0x112,
	RTW89_HW_RATE_V1_MCS19		= 0x113,
	RTW89_HW_RATE_V1_MCS20		= 0x114,
	RTW89_HW_RATE_V1_MCS21		= 0x115,
	RTW89_HW_RATE_V1_MCS22		= 0x116,
	RTW89_HW_RATE_V1_MCS23		= 0x117,
	RTW89_HW_RATE_V1_MCS24		= 0x118,
	RTW89_HW_RATE_V1_MCS25		= 0x119,
	RTW89_HW_RATE_V1_MCS26		= 0x11A,
	RTW89_HW_RATE_V1_MCS27		= 0x11B,
	RTW89_HW_RATE_V1_MCS28		= 0x11C,
	RTW89_HW_RATE_V1_MCS29		= 0x11D,
	RTW89_HW_RATE_V1_MCS30		= 0x11E,
	RTW89_HW_RATE_V1_MCS31		= 0x11F,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS0	= 0x200,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS1	= 0x201,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS2	= 0x202,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS3	= 0x203,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS4	= 0x204,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS5	= 0x205,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS6	= 0x206,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS7	= 0x207,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS8	= 0x208,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS9	= 0x209,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS10	= 0x20A,
	RTW89_HW_RATE_V1_VHT_NSS1_MCS11	= 0x20B,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS0	= 0x220,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS1	= 0x221,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS2	= 0x222,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS3	= 0x223,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS4	= 0x224,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS5	= 0x225,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS6	= 0x226,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS7	= 0x227,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS8	= 0x228,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS9	= 0x229,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS10	= 0x22A,
	RTW89_HW_RATE_V1_VHT_NSS2_MCS11	= 0x22B,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS0	= 0x240,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS1	= 0x241,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS2	= 0x242,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS3	= 0x243,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS4	= 0x244,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS5	= 0x245,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS6	= 0x246,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS7	= 0x247,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS8	= 0x248,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS9	= 0x249,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS10	= 0x24A,
	RTW89_HW_RATE_V1_VHT_NSS3_MCS11	= 0x24B,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS0	= 0x260,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS1	= 0x261,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS2	= 0x262,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS3	= 0x263,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS4	= 0x264,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS5	= 0x265,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS6	= 0x266,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS7	= 0x267,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS8	= 0x268,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS9	= 0x269,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS10	= 0x26A,
	RTW89_HW_RATE_V1_VHT_NSS4_MCS11	= 0x26B,
	RTW89_HW_RATE_V1_HE_NSS1_MCS0	= 0x300,
	RTW89_HW_RATE_V1_HE_NSS1_MCS1	= 0x301,
	RTW89_HW_RATE_V1_HE_NSS1_MCS2	= 0x302,
	RTW89_HW_RATE_V1_HE_NSS1_MCS3	= 0x303,
	RTW89_HW_RATE_V1_HE_NSS1_MCS4	= 0x304,
	RTW89_HW_RATE_V1_HE_NSS1_MCS5	= 0x305,
	RTW89_HW_RATE_V1_HE_NSS1_MCS6	= 0x306,
	RTW89_HW_RATE_V1_HE_NSS1_MCS7	= 0x307,
	RTW89_HW_RATE_V1_HE_NSS1_MCS8	= 0x308,
	RTW89_HW_RATE_V1_HE_NSS1_MCS9	= 0x309,
	RTW89_HW_RATE_V1_HE_NSS1_MCS10	= 0x30A,
	RTW89_HW_RATE_V1_HE_NSS1_MCS11	= 0x30B,
	RTW89_HW_RATE_V1_HE_NSS2_MCS0	= 0x320,
	RTW89_HW_RATE_V1_HE_NSS2_MCS1	= 0x321,
	RTW89_HW_RATE_V1_HE_NSS2_MCS2	= 0x322,
	RTW89_HW_RATE_V1_HE_NSS2_MCS3	= 0x323,
	RTW89_HW_RATE_V1_HE_NSS2_MCS4	= 0x324,
	RTW89_HW_RATE_V1_HE_NSS2_MCS5	= 0x325,
	RTW89_HW_RATE_V1_HE_NSS2_MCS6	= 0x326,
	RTW89_HW_RATE_V1_HE_NSS2_MCS7	= 0x327,
	RTW89_HW_RATE_V1_HE_NSS2_MCS8	= 0x328,
	RTW89_HW_RATE_V1_HE_NSS2_MCS9	= 0x329,
	RTW89_HW_RATE_V1_HE_NSS2_MCS10	= 0x32A,
	RTW89_HW_RATE_V1_HE_NSS2_MCS11	= 0x32B,
	RTW89_HW_RATE_V1_HE_NSS3_MCS0	= 0x340,
	RTW89_HW_RATE_V1_HE_NSS3_MCS1	= 0x341,
	RTW89_HW_RATE_V1_HE_NSS3_MCS2	= 0x342,
	RTW89_HW_RATE_V1_HE_NSS3_MCS3	= 0x343,
	RTW89_HW_RATE_V1_HE_NSS3_MCS4	= 0x344,
	RTW89_HW_RATE_V1_HE_NSS3_MCS5	= 0x345,
	RTW89_HW_RATE_V1_HE_NSS3_MCS6	= 0x346,
	RTW89_HW_RATE_V1_HE_NSS3_MCS7	= 0x347,
	RTW89_HW_RATE_V1_HE_NSS3_MCS8	= 0x348,
	RTW89_HW_RATE_V1_HE_NSS3_MCS9	= 0x349,
	RTW89_HW_RATE_V1_HE_NSS3_MCS10	= 0x34A,
	RTW89_HW_RATE_V1_HE_NSS3_MCS11	= 0x34B,
	RTW89_HW_RATE_V1_HE_NSS4_MCS0	= 0x360,
	RTW89_HW_RATE_V1_HE_NSS4_MCS1	= 0x361,
	RTW89_HW_RATE_V1_HE_NSS4_MCS2	= 0x362,
	RTW89_HW_RATE_V1_HE_NSS4_MCS3	= 0x363,
	RTW89_HW_RATE_V1_HE_NSS4_MCS4	= 0x364,
	RTW89_HW_RATE_V1_HE_NSS4_MCS5	= 0x365,
	RTW89_HW_RATE_V1_HE_NSS4_MCS6	= 0x366,
	RTW89_HW_RATE_V1_HE_NSS4_MCS7	= 0x367,
	RTW89_HW_RATE_V1_HE_NSS4_MCS8	= 0x368,
	RTW89_HW_RATE_V1_HE_NSS4_MCS9	= 0x369,
	RTW89_HW_RATE_V1_HE_NSS4_MCS10	= 0x36A,
	RTW89_HW_RATE_V1_HE_NSS4_MCS11	= 0x36B,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS0	= 0x400,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS1	= 0x401,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS2	= 0x402,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS3	= 0x403,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS4	= 0x404,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS5	= 0x405,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS6	= 0x406,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS7	= 0x407,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS8	= 0x408,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS9	= 0x409,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS10	= 0x40A,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS11	= 0x40B,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS12	= 0x40C,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS13	= 0x40D,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS14	= 0x40E,
	RTW89_HW_RATE_V1_EHT_NSS1_MCS15	= 0x40F,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS0	= 0x420,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS1	= 0x421,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS2	= 0x422,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS3	= 0x423,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS4	= 0x424,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS5	= 0x425,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS6	= 0x426,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS7	= 0x427,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS8	= 0x428,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS9	= 0x429,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS10	= 0x42A,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS11	= 0x42B,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS12	= 0x42C,
	RTW89_HW_RATE_V1_EHT_NSS2_MCS13	= 0x42D,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS0	= 0x440,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS1	= 0x441,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS2	= 0x442,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS3	= 0x443,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS4	= 0x444,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS5	= 0x445,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS6	= 0x446,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS7	= 0x447,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS8	= 0x448,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS9	= 0x449,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS10	= 0x44A,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS11	= 0x44B,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS12	= 0x44C,
	RTW89_HW_RATE_V1_EHT_NSS3_MCS13	= 0x44D,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS0	= 0x460,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS1	= 0x461,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS2	= 0x462,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS3	= 0x463,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS4	= 0x464,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS5	= 0x465,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS6	= 0x466,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS7	= 0x467,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS8	= 0x468,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS9	= 0x469,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS10	= 0x46A,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS11	= 0x46B,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS12	= 0x46C,
	RTW89_HW_RATE_V1_EHT_NSS4_MCS13	= 0x46D,

	RTW89_HW_RATE_NR,
	RTW89_HW_RATE_INVAL,

	RTW89_HW_RATE_MASK_MOD = GENMASK(8, 7),
	RTW89_HW_RATE_MASK_VAL = GENMASK(6, 0),
	RTW89_HW_RATE_V1_MASK_MOD = GENMASK(10, 8),
	RTW89_HW_RATE_V1_MASK_VAL = GENMASK(7, 0),
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

/* 6G channels,
 * 1, 3, 5, 7, 9, 11, 13, 15,
 * 17, 19, 21, 23, 25, 27, 29, 33,
 * 35, 37, 39, 41, 43, 45, 47, 49,
 * 51, 53, 55, 57, 59, 61, 65, 67,
 * 69, 71, 73, 75, 77, 79, 81, 83,
 * 85, 87, 89, 91, 93, 97, 99, 101,
 * 103, 105, 107, 109, 111, 113, 115, 117,
 * 119, 121, 123, 125, 129, 131, 133, 135,
 * 137, 139, 141, 143, 145, 147, 149, 151,
 * 153, 155, 157, 161, 163, 165, 167, 169,
 * 171, 173, 175, 177, 179, 181, 183, 185,
 * 187, 189, 193, 195, 197, 199, 201, 203,
 * 205, 207, 209, 211, 213, 215, 217, 219,
 * 221, 225, 227, 229, 231, 233, 235, 237,
 * 239, 241, 243, 245, 247, 249, 251, 253,
 */
#define RTW89_6G_CH_NUM 120

enum rtw89_rate_section {
	RTW89_RS_CCK,
	RTW89_RS_OFDM,
	RTW89_RS_MCS, /* for HT/VHT/HE */
	RTW89_RS_HEDCM,
	RTW89_RS_OFFSET,
	RTW89_RS_NUM,
	RTW89_RS_LMT_NUM = RTW89_RS_MCS + 1,
	RTW89_RS_TX_SHAPE_NUM = RTW89_RS_OFDM + 1,
};

enum rtw89_rate_offset_indexes {
	RTW89_RATE_OFFSET_HE,
	RTW89_RATE_OFFSET_VHT,
	RTW89_RATE_OFFSET_HT,
	RTW89_RATE_OFFSET_OFDM,
	RTW89_RATE_OFFSET_CCK,
	RTW89_RATE_OFFSET_DLRU_EHT,
	RTW89_RATE_OFFSET_DLRU_HE,
	RTW89_RATE_OFFSET_EHT,
	__RTW89_RATE_OFFSET_NUM,

	RTW89_RATE_OFFSET_NUM_AX = RTW89_RATE_OFFSET_CCK + 1,
	RTW89_RATE_OFFSET_NUM_BE = RTW89_RATE_OFFSET_EHT + 1,
};

enum rtw89_rate_num {
	RTW89_RATE_CCK_NUM	= 4,
	RTW89_RATE_OFDM_NUM	= 8,
	RTW89_RATE_HEDCM_NUM	= 4, /* for HEDCM MCS0/1/3/4 */

	RTW89_RATE_MCS_NUM_AX	= 12,
	RTW89_RATE_MCS_NUM_BE	= 16,
	__RTW89_RATE_MCS_NUM	= 16,
};

enum rtw89_nss {
	RTW89_NSS_1		= 0,
	RTW89_NSS_2		= 1,
	/* HE DCM only support 1ss and 2ss */
	RTW89_NSS_HEDCM_NUM	= RTW89_NSS_2 + 1,
	RTW89_NSS_3		= 2,
	RTW89_NSS_4		= 3,
	RTW89_NSS_NUM,
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

enum rtw89_ofdma_type {
	RTW89_NON_OFDMA	= 0,
	RTW89_OFDMA	= 1,
	RTW89_OFDMA_NUM,
};

/* neither insert new in the middle, nor change any given definition */
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
	RTW89_UK	= 14,
	RTW89_THAILAND	= 15,
	RTW89_REGD_NUM,
};

enum rtw89_reg_6ghz_power {
	RTW89_REG_6GHZ_POWER_VLP = 0,
	RTW89_REG_6GHZ_POWER_LPI = 1,
	RTW89_REG_6GHZ_POWER_STD = 2,

	NUM_OF_RTW89_REG_6GHZ_POWER,
	RTW89_REG_6GHZ_POWER_DFLT = RTW89_REG_6GHZ_POWER_VLP,
};

#define RTW89_MIN_VALID_POWER_CONSTRAINT (-10) /* unit: dBm */

/* calculate based on ieee80211 Transmit Power Envelope */
struct rtw89_reg_6ghz_tpe {
	bool valid;
	s8 constraint; /* unit: dBm */
};

enum rtw89_fw_pkt_ofld_type {
	RTW89_PKT_OFLD_TYPE_PROBE_RSP = 0,
	RTW89_PKT_OFLD_TYPE_PS_POLL = 1,
	RTW89_PKT_OFLD_TYPE_NULL_DATA = 2,
	RTW89_PKT_OFLD_TYPE_QOS_NULL = 3,
	RTW89_PKT_OFLD_TYPE_CTS2SELF = 4,
	RTW89_PKT_OFLD_TYPE_ARP_RSP = 5,
	RTW89_PKT_OFLD_TYPE_NDP = 6,
	RTW89_PKT_OFLD_TYPE_EAPOL_KEY = 7,
	RTW89_PKT_OFLD_TYPE_SA_QUERY = 8,
	RTW89_PKT_OFLD_TYPE_PROBE_REQ = 12,
	RTW89_PKT_OFLD_TYPE_NUM,
};

struct rtw89_txpwr_byrate {
	s8 cck[RTW89_RATE_CCK_NUM];
	s8 ofdm[RTW89_RATE_OFDM_NUM];
	s8 mcs[RTW89_OFDMA_NUM][RTW89_NSS_NUM][__RTW89_RATE_MCS_NUM];
	s8 hedcm[RTW89_OFDMA_NUM][RTW89_NSS_HEDCM_NUM][RTW89_RATE_HEDCM_NUM];
	s8 offset[__RTW89_RATE_OFFSET_NUM];
	s8 trap;
};

struct rtw89_rate_desc {
	enum rtw89_nss nss;
	enum rtw89_rate_section rs;
	enum rtw89_ofdma_type ofdma;
	u8 idx;
};

#define PHY_STS_HDR_LEN 8
#define RF_PATH_MAX 4
#define RTW89_MAX_PPDU_CNT 8
struct rtw89_rx_phy_ppdu {
	void *buf;
	u32 len;
	u8 rssi_avg;
	u8 rssi[RF_PATH_MAX];
	u8 mac_id;
	u8 chan_idx;
	u8 phy_idx;
	u8 ie;
	u16 rate;
	u8 rpl_avg;
	u8 rpl_path[RF_PATH_MAX];
	u8 rpl_fd[RF_PATH_MAX];
	u8 bw_idx;
	u8 rx_path_en;
	struct {
		bool has;
		u8 avg_snr;
		u8 evm_max;
		u8 evm_min;
	} ofdm;
	bool has_data;
	bool has_bcn;
	bool ldpc;
	bool stbc;
	bool to_self;
	bool valid;
	bool hdr_2_en;
};

enum rtw89_mac_idx {
	RTW89_MAC_0 = 0,
	RTW89_MAC_1 = 1,
	RTW89_MAC_NUM,
};

enum rtw89_phy_idx {
	RTW89_PHY_0 = 0,
	RTW89_PHY_1 = 1,
	RTW89_PHY_NUM,
};

#define __RTW89_MLD_MAX_LINK_NUM 2
#define RTW89_MLD_NON_STA_LINK_NUM 1

enum rtw89_chanctx_idx {
	RTW89_CHANCTX_0 = 0,
	RTW89_CHANCTX_1 = 1,

	NUM_OF_RTW89_CHANCTX,
	RTW89_CHANCTX_IDLE = NUM_OF_RTW89_CHANCTX,
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
	RTW89_CHANNEL_WIDTH_320	= 4,

	/* keep index order above */
	RTW89_CHANNEL_WIDTH_ORDINARY_NUM = 5,

	RTW89_CHANNEL_WIDTH_80_80 = 5,
	RTW89_CHANNEL_WIDTH_5 = 6,
	RTW89_CHANNEL_WIDTH_10 = 7,
};

enum rtw89_ps_mode {
	RTW89_PS_MODE_NONE	= 0,
	RTW89_PS_MODE_RFOFF	= 1,
	RTW89_PS_MODE_CLK_GATED	= 2,
	RTW89_PS_MODE_PWR_GATED	= 3,
};

#define RTW89_2G_BW_NUM (RTW89_CHANNEL_WIDTH_40 + 1)
#define RTW89_5G_BW_NUM (RTW89_CHANNEL_WIDTH_160 + 1)
#define RTW89_6G_BW_NUM (RTW89_CHANNEL_WIDTH_320 + 1)
#define RTW89_BYR_BW_NUM (RTW89_CHANNEL_WIDTH_320 + 1)
#define RTW89_PPE_BW_NUM (RTW89_CHANNEL_WIDTH_320 + 1)

enum rtw89_pe_duration {
	RTW89_PE_DURATION_0 = 0,
	RTW89_PE_DURATION_8 = 1,
	RTW89_PE_DURATION_16 = 2,
	RTW89_PE_DURATION_16_20 = 3,
};

enum rtw89_ru_bandwidth {
	RTW89_RU26 = 0,
	RTW89_RU52 = 1,
	RTW89_RU106 = 2,
	RTW89_RU52_26 = 3,
	RTW89_RU106_26 = 4,
	RTW89_RU_NUM,
};

enum rtw89_sc_offset {
	RTW89_SC_DONT_CARE	= 0,
	RTW89_SC_20_UPPER	= 1,
	RTW89_SC_20_LOWER	= 2,
	RTW89_SC_20_UPMOST	= 3,
	RTW89_SC_20_LOWEST	= 4,
	RTW89_SC_20_UP2X	= 5,
	RTW89_SC_20_LOW2X	= 6,
	RTW89_SC_20_UP3X	= 7,
	RTW89_SC_20_LOW3X	= 8,
	RTW89_SC_40_UPPER	= 9,
	RTW89_SC_40_LOWER	= 10,
};

/* only mgd features can be added to the enum */
enum rtw89_wow_flags {
	RTW89_WOW_FLAG_EN_MAGIC_PKT,
	RTW89_WOW_FLAG_EN_REKEY_PKT,
	RTW89_WOW_FLAG_EN_DISCONNECT,
	RTW89_WOW_FLAG_EN_PATTERN,
	RTW89_WOW_FLAG_NUM,
};

struct rtw89_chan {
	u8 channel;
	u8 primary_channel;
	enum rtw89_band band_type;
	enum rtw89_bandwidth band_width;

	/* The follow-up are derived from the above. We must ensure that it
	 * is assigned correctly in rtw89_chan_create() if new one is added.
	 */
	u32 freq;
	enum rtw89_subband subband_type;
	enum rtw89_sc_offset pri_ch_idx;
	u8 pri_sb_idx;
};

struct rtw89_chan_rcd {
	u8 prev_primary_channel;
	enum rtw89_band prev_band_type;
	bool band_changed;
};

struct rtw89_channel_help_params {
	u32 tx_en;
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
	u32 md_tsft;
	u32 bss_color;
	u32 mbssid;
	u32 mbssid_drop;
	u32 tsf_sync;
	u32 ptcl_dbg;
	u32 ptcl_dbg_info;
	u32 bcn_drop_all;
	u32 hiq_win[RTW89_PORT_NUM];
};

struct rtw89_txwd_body {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
} __packed;

struct rtw89_txwd_body_v1 {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
	__le32 dword6;
	__le32 dword7;
} __packed;

struct rtw89_txwd_body_v2 {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
	__le32 dword6;
	__le32 dword7;
} __packed;

struct rtw89_txwd_info {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
} __packed;

struct rtw89_txwd_info_v2 {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
	__le32 dword6;
	__le32 dword7;
} __packed;

struct rtw89_rx_desc_info {
	u16 pkt_size;
	u8 pkt_type;
	u8 drv_info_size;
	u8 phy_rpt_size;
	u8 hdr_cnv_size;
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
	u16 rxd_len;
	bool ready;
	u16 rssi;
};

struct rtw89_rxdesc_short {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
} __packed;

struct rtw89_rxdesc_short_v2 {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
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

struct rtw89_rxdesc_long_v2 {
	__le32 dword0;
	__le32 dword1;
	__le32 dword2;
	__le32 dword3;
	__le32 dword4;
	__le32 dword5;
	__le32 dword6;
	__le32 dword7;
	__le32 dword8;
	__le32 dword9;
} __packed;

struct rtw89_rxdesc_phy_rpt_v2 {
	__le32 dword0;
	__le32 dword1;
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
	u8 addr_info_nr;
	u8 sec_keyid;
	u8 sec_type;
	u8 sec_cam_idx;
	u8 sec_seq[6];
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
	bool er_cap;
	bool stbc;
	bool ldpc;
	bool upd_wlan_hdr;
	bool mlo;
	bool sw_mld;
};

struct rtw89_core_tx_request {
	enum rtw89_core_tx_type tx_type;

	struct sk_buff *skb;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_sta_link *rtwsta_link;
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
} __packed;

struct rtw89_mac_ax_wl_act {
	u8 wlan_act_en;
	u8 wlan_act;
};

#define RTW89_MAC_AX_COEX_GNT_NR 2
struct rtw89_mac_ax_coex_gnt {
	struct rtw89_mac_ax_gnt band[RTW89_MAC_AX_COEX_GNT_NR];
	struct rtw89_mac_ax_wl_act bt[RTW89_MAC_AX_COEX_GNT_NR];
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
	BTC_NCNT_WL_STA_LAST,
	BTC_NCNT_FWINFO,
	BTC_NCNT_TIMER,
	BTC_NCNT_SWITCH_CHBW,
	BTC_NCNT_RESUME_DL_FW,
	BTC_NCNT_COUNTRYCODE,
	BTC_NCNT_NUM,
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
	BTC_DCNT_RPT_HANG,
	BTC_DCNT_CYCLE,
	BTC_DCNT_CYCLE_HANG,
	BTC_DCNT_W1,
	BTC_DCNT_W1_HANG,
	BTC_DCNT_B1,
	BTC_DCNT_B1_HANG,
	BTC_DCNT_TDMA_NONSYNC,
	BTC_DCNT_SLOT_NONSYNC,
	BTC_DCNT_BTCNT_HANG,
	BTC_DCNT_BTTX_HANG,
	BTC_DCNT_WL_SLOT_DRIFT,
	BTC_DCNT_WL_STA_LAST,
	BTC_DCNT_BT_SLOT_DRIFT,
	BTC_DCNT_BT_SLOT_FLOOD,
	BTC_DCNT_FDDT_TRIG,
	BTC_DCNT_E2G,
	BTC_DCNT_E2G_HANG,
	BTC_DCNT_WL_FW_VER_MATCH,
	BTC_DCNT_NULL_TX_FAIL,
	BTC_DCNT_WL_STA_NTFY,
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
	BTC_WCNT_DBCC_ALL_2G,
	BTC_WCNT_DBCC_CHG,
	BTC_WCNT_RX_OK_LAST,
	BTC_WCNT_RX_OK_LAST2S,
	BTC_WCNT_RX_ERR_LAST,
	BTC_WCNT_RX_ERR_LAST2S,
	BTC_WCNT_RX_LAST,
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
	BTC_BCNT_POLUT_NOW,
	BTC_BCNT_POLUT_DIFF,
	BTC_BCNT_RATECHG,
	BTC_BCNT_NUM,
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
	u8 btg_pos: 2;
	u8 stream_cnt: 4;
};

struct rtw89_btc_ant_info_v7 {
	u8 type;  /* shared, dedicated(non-shared) */
	u8 num;   /* antenna count  */
	u8 isolation;
	u8 single_pos;/* wifi 1ss-1ant at 0:S0 or 1:S1 */

	u8 diversity; /* only for wifi use 1-antenna */
	u8 btg_pos; /* btg-circuit at 0:S0/1:S1/others:all */
	u8 stream_cnt;  /* spatial_stream count */
	u8 rsvd;
} __packed;

enum rtw89_tfc_dir {
	RTW89_TFC_UL,
	RTW89_TFC_DL,
};

struct rtw89_btc_wl_smap {
	u32 busy: 1;
	u32 scan: 1;
	u32 connecting: 1;
	u32 roaming: 1;
	u32 dbccing: 1;
	u32 _4way: 1;
	u32 rf_off: 1;
	u32 lps: 2;
	u32 ips: 1;
	u32 init_ok: 1;
	u32 traffic_dir : 2;
	u32 rf_off_pre: 1;
	u32 lps_pre: 2;
	u32 lps_exiting: 1;
	u32 emlsr: 1;
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

	u32 rx_tf_acc;
	u32 rx_tf_periodic;

	enum rtw89_tfc_lv tx_tfc_lv;
	enum rtw89_tfc_lv rx_tfc_lv;
	struct ewma_tp tx_ewma_tp;
	struct ewma_tp rx_ewma_tp;

	u16 tx_rate;
	u16 rx_rate;
};

struct rtw89_btc_chdef {
	u8 center_ch;
	u8 band;
	u8 chan;
	enum rtw89_sc_offset offset;
	enum rtw89_bandwidth bw;
};

struct rtw89_btc_statistic {
	u8 rssi; /* 0%~110% (dBm = rssi -110) */
	struct rtw89_traffic_stats traffic;
};

#define BTC_WL_RSSI_THMAX 4

struct rtw89_btc_wl_link_info {
	struct rtw89_btc_chdef chdef;
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
	u8 tx_1ss_limit;

	u8 mac_id;
	u8 tx_retry;

	u32 bcn_period;
	u32 busy_t;
	u32 tx_time;
	u32 client_cnt;
	u32 rx_rate_drop_cnt;
	u32 noa_duration;

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
	u32 handle_update: 1;
	u32 devinfo_query: 1;
	u32 no_empty_streak_2s: 8;
	u32 no_empty_streak_max: 8;
	u32 rsvd: 6;

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
	u8 band[RTW89_PHY_NUM];
	u8 phy_map;
	u8 rsvd;
};

struct rtw89_btc_wl_dbcc_info {
	u8 op_band[RTW89_PHY_NUM]; /* op band in each phy */
	u8 scan_band[RTW89_PHY_NUM]; /* scan band in  each phy */
	u8 real_band[RTW89_PHY_NUM];
	u8 role[RTW89_PHY_NUM]; /* role in each phy */
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

struct rtw89_btc_wl_active_role_v1 {
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

	u32 noa_duration; /* ms */
};

struct rtw89_btc_wl_active_role_v2 {
	u8 connected: 1;
	u8 pid: 3;
	u8 phy: 1;
	u8 noa: 1;
	u8 band: 2;

	u8 client_ps: 1;
	u8 bw: 7;

	u8 role;
	u8 ch;

	u32 noa_duration; /* ms */
};

struct rtw89_btc_wl_active_role_v7 {
	u8 connected;
	u8 pid;
	u8 phy;
	u8 noa;

	u8 band;
	u8 client_ps;
	u8 bw;
	u8 role;

	u8 ch;
	u8 noa_dur;
	u8 client_cnt;
	u8 rsvd2;
} __packed;

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

struct rtw89_btc_wl_scc_ctrl {
	u8 null_role1;
	u8 null_role2;
	u8 ebt_null; /* if tx null at EBT slot */
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

struct rtw89_btc_wl_role_info_v1 { /* struct size must be n*4 bytes */
	u8 connect_cnt;
	u8 link_mode;
	union rtw89_btc_wl_role_info_map role_map;
	struct rtw89_btc_wl_active_role_v1 active_role_v1[RTW89_PORT_NUM];
	u32 mrole_type; /* btc_wl_mrole_type */
	u32 mrole_noa_duration; /* ms */

	u32 dbcc_en: 1;
	u32 dbcc_chg: 1;
	u32 dbcc_2g_phy: 2; /* which phy operate in 2G, HW_PHY_0 or HW_PHY_1 */
	u32 link_mode_chg: 1;
	u32 rsvd: 27;
};

struct rtw89_btc_wl_role_info_v2 { /* struct size must be n*4 bytes */
	u8 connect_cnt;
	u8 link_mode;
	union rtw89_btc_wl_role_info_map role_map;
	struct rtw89_btc_wl_active_role_v2 active_role_v2[RTW89_PORT_NUM];
	u32 mrole_type; /* btc_wl_mrole_type */
	u32 mrole_noa_duration; /* ms */

	u32 dbcc_en: 1;
	u32 dbcc_chg: 1;
	u32 dbcc_2g_phy: 2; /* which phy operate in 2G, HW_PHY_0 or HW_PHY_1 */
	u32 link_mode_chg: 1;
	u32 rsvd: 27;
};

struct rtw89_btc_wl_rlink { /* H2C info, struct size must be n*4 bytes */
	u8 connected;
	u8 pid;
	u8 phy;
	u8 noa;

	u8 rf_band; /* enum band_type RF band: 2.4G/5G/6G */
	u8 active; /* 0:rlink is under doze */
	u8 bw; /* enum channel_width */
	u8 role; /*enum role_type */

	u8 ch;
	u8 noa_dur; /* ms */
	u8 client_cnt; /* for Role = P2P-Go/AP */
	u8 mode; /* wifi protocol */
} __packed;

#define RTW89_BE_BTC_WL_MAX_ROLE_NUMBER 6
struct rtw89_btc_wl_role_info_v7 { /* struct size must be n*4 bytes */
	u8 connect_cnt;
	u8 link_mode;
	u8 link_mode_chg;
	u8 p2p_2g;

	struct rtw89_btc_wl_active_role_v7 active_role[RTW89_BE_BTC_WL_MAX_ROLE_NUMBER];

	u32 role_map;
	u32 mrole_type; /* btc_wl_mrole_type */
	u32 mrole_noa_duration; /* ms */
	u32 dbcc_en;
	u32 dbcc_chg;
	u32 dbcc_2g_phy; /* which phy operate in 2G, HW_PHY_0 or HW_PHY_1 */
} __packed;

struct rtw89_btc_wl_role_info_v8 { /* H2C info, struct size must be n*4 bytes */
	u8 connect_cnt;
	u8 link_mode;
	u8 link_mode_chg;
	u8 p2p_2g;

	u8 pta_req_band;
	u8 dbcc_en; /* 1+1 and 2.4G-included */
	u8 dbcc_chg;
	u8 dbcc_2g_phy; /* which phy operate in 2G, HW_PHY_0 or HW_PHY_1 */

	struct rtw89_btc_wl_rlink rlink[RTW89_BE_BTC_WL_MAX_ROLE_NUMBER][RTW89_MAC_NUM];

	u32 role_map;
	u32 mrole_type; /* btc_wl_mrole_type */
	u32 mrole_noa_duration; /* ms */
} __packed;

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
	u32 con_rfk: 1;
	u32 rsvd: 13;

	u32 start_time;
	u32 proc_time;
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
#define BTC_BT_AFH_LE_GROUP 5

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
	u8 afh_map_le[BTC_BT_AFH_LE_GROUP];

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
	u32 cycle_hang: 1;
	u32 w1_hang: 1;
	u32 b1_hang: 1;
	u32 tdma_no_sync: 1;
	u32 slot_no_sync: 1;
	u32 wl_slot_drift: 1;
	u32 bt_slot_drift: 1;
	u32 role_num_mismatch: 1;
	u32 null1_tx_late: 1;
	u32 bt_afh_conflict: 1;
	u32 bt_leafh_conflict: 1;
	u32 bt_slot_flood: 1;
	u32 wl_e2g_hang: 1;
	u32 wl_ver_mismatch: 1;
	u32 bt_ver_mismatch: 1;
	u32 rfe_type0: 1;
	u32 h2c_buffer_over: 1;
	u32 bt_tx_hang: 1; /* for SNR too low bug, BT has no Tx req*/
	u32 wl_no_sta_ntfy: 1;

	u32 h2c_bmap_mismatch: 1;
	u32 c2h_bmap_mismatch: 1;
	u32 h2c_struct_invalid: 1;
	u32 c2h_struct_invalid: 1;
	u32 h2c_c2h_buffer_mismatch: 1;
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

struct rtw89_btc_wl_nhm {
	u8 instant_wl_nhm_dbm;
	u8 instant_wl_nhm_per_mhz;
	u16 valid_record_times;
	s8 record_pwr[16];
	u8 record_ratio[16];
	s8 pwr; /* dbm_per_MHz  */
	u8 ratio;
	u8 current_status;
	u8 refresh;
	bool start_flag;
	s8 pwr_max;
	s8 pwr_min;
};

struct rtw89_btc_wl_info {
	struct rtw89_btc_wl_link_info link_info[RTW89_PORT_NUM];
	struct rtw89_btc_wl_link_info rlink_info[RTW89_BE_BTC_WL_MAX_ROLE_NUMBER][RTW89_MAC_NUM];
	struct rtw89_btc_wl_rfk_info rfk_info;
	struct rtw89_btc_wl_ver_info  ver_info;
	struct rtw89_btc_wl_afh_info afh_info;
	struct rtw89_btc_wl_role_info role_info;
	struct rtw89_btc_wl_role_info_v1 role_info_v1;
	struct rtw89_btc_wl_role_info_v2 role_info_v2;
	struct rtw89_btc_wl_role_info_v7 role_info_v7;
	struct rtw89_btc_wl_role_info_v8 role_info_v8;
	struct rtw89_btc_wl_scan_info scan_info;
	struct rtw89_btc_wl_dbcc_info dbcc_info;
	struct rtw89_btc_rf_para rf_para;
	struct rtw89_btc_wl_nhm nhm;
	union rtw89_btc_wl_state_map status;

	u8 port_id[RTW89_WIFI_ROLE_MLME_MAX];
	u8 rssi_level;
	u8 cn_report;
	u8 coex_mode;
	u8 pta_req_mac;
	u8 bt_polut_type[RTW89_PHY_NUM]; /* BT polluted WL-Tx type for phy0/1  */

	bool is_5g_hi_channel;
	bool pta_reg_mac_chg;
	bool bg_mode;
	bool he_mode;
	bool scbd_change;
	bool fw_ver_mismatch;
	bool client_cnt_inc_2g;
	u32 scbd;
};

struct rtw89_btc_module {
	struct rtw89_btc_ant_info ant;
	u8 rfe_type;
	u8 cv;

	u8 bt_solo: 1;
	u8 bt_pos: 1;
	u8 switch_type: 1;
	u8 wa_type: 3;

	u8 kt_ver_adie;
};

struct rtw89_btc_module_v7 {
	u8 rfe_type;
	u8 kt_ver;
	u8 bt_solo;
	u8 bt_pos; /* wl-end view: get from efuse, must compare bt.btg_type*/

	u8 switch_type; /* WL/BT switch type: 0: internal, 1: external */
	u8 wa_type; /* WA type: 0:none, 1: 51B 5G_Hi-Ch_Rx */
	u8 kt_ver_adie;
	u8 rsvd;

	struct rtw89_btc_ant_info_v7 ant;
} __packed;

union rtw89_btc_module_info {
	struct rtw89_btc_module md;
	struct rtw89_btc_module_v7 md_v7;
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

struct rtw89_btc_init_info_v7 {
	u8 wl_guard_ch;
	u8 wl_only;
	u8 wl_init_ok;
	u8 rsvd3;

	u8 cx_other;
	u8 bt_only;
	u8 pta_mode;
	u8 pta_direction;

	struct rtw89_btc_module_v7 module;
} __packed;

union rtw89_btc_init_info_u {
	struct rtw89_btc_init_info init;
	struct rtw89_btc_init_info_v7 init_v7;
};

struct rtw89_btc_wl_tx_limit_para {
	u16 enable;
	u32 tx_time;	/* unit: us */
	u16 tx_retry;
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

enum rtw89_btc_ble_scan_type {
	CXSCAN_BG = 0,
	CXSCAN_INIT,
	CXSCAN_LE,
	CXSCAN_MAX
};

#define RTW89_BTC_BTC_SCAN_V1_FLAG_ENABLE BIT(0)
#define RTW89_BTC_BTC_SCAN_V1_FLAG_INTERLACE BIT(1)

struct rtw89_btc_bt_scan_info_v1 {
	__le16 win;
	__le16 intvl;
	__le32 flags;
} __packed;

struct rtw89_btc_bt_scan_info_v2 {
	__le16 win;
	__le16 intvl;
} __packed;

struct rtw89_btc_fbtc_btscan_v1 {
	u8 fver; /* btc_ver::fcxbtscan */
	u8 rsvd;
	__le16 rsvd2;
	struct rtw89_btc_bt_scan_info_v1 scan[BTC_SCAN_MAX1];
} __packed;

struct rtw89_btc_fbtc_btscan_v2 {
	u8 fver; /* btc_ver::fcxbtscan */
	u8 type;
	__le16 rsvd2;
	struct rtw89_btc_bt_scan_info_v2 para[CXSCAN_MAX];
} __packed;

struct rtw89_btc_fbtc_btscan_v7 {
	u8 fver; /* btc_ver::fcxbtscan */
	u8 type;
	u8 rsvd0;
	u8 rsvd1;
	struct rtw89_btc_bt_scan_info_v2 para[CXSCAN_MAX];
} __packed;

union rtw89_btc_fbtc_btscan {
	struct rtw89_btc_fbtc_btscan_v1 v1;
	struct rtw89_btc_fbtc_btscan_v2 v2;
	struct rtw89_btc_fbtc_btscan_v7 v7;
};

struct rtw89_btc_bt_info {
	struct rtw89_btc_bt_link_info link_info;
	struct rtw89_btc_bt_scan_info_v1 scan_info_v1[BTC_SCAN_MAX1];
	struct rtw89_btc_bt_scan_info_v2 scan_info_v2[CXSCAN_MAX];
	struct rtw89_btc_bt_ver_info ver_info;
	struct rtw89_btc_bool_sta_chg enable;
	struct rtw89_btc_bool_sta_chg inq_pag;
	struct rtw89_btc_rf_para rf_para;
	union rtw89_btc_bt_rfk_info_map rfk_info;

	u8 raw_info[BTC_BTINFO_MAX]; /* raw bt info from mailbox */
	u8 rssi_level;

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
	u32 scan_rx_low_pri: 1;
	u32 scan_info_update: 1;
	u32 lna_constrain: 3;
	u32 rsvd: 17;
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
	u8 type; /* btc_ver::fcxtdma */
	u8 rxflctrl;
	u8 txpause;
	u8 wtgle_n;
	u8 leak_n;
	u8 ext_ctrl;
	u8 rxflctrl_role;
	u8 option_ctrl;
} __packed;

struct rtw89_btc_fbtc_tdma_v3 {
	u8 fver; /* btc_ver::fcxtdma */
	u8 rsvd;
	__le16 rsvd1;
	struct rtw89_btc_fbtc_tdma tdma;
} __packed;

union rtw89_btc_fbtc_tdma_le32 {
	struct rtw89_btc_fbtc_tdma v1;
	struct rtw89_btc_fbtc_tdma_v3 v3;
};

#define CXMREG_MAX 30
#define CXMREG_MAX_V2 20
#define FCXMAX_STEP 255 /*STEP trace record cnt, Max:65535, default:255*/
#define BTC_CYCLE_SLOT_MAX 48 /* must be even number, non-zero */

enum rtw89_btc_bt_sta_counter {
	BTC_BCNT_RFK_REQ = 0,
	BTC_BCNT_RFK_GO = 1,
	BTC_BCNT_RFK_REJECT = 2,
	BTC_BCNT_RFK_FAIL = 3,
	BTC_BCNT_RFK_TIMEOUT = 4,
	BTC_BCNT_HI_TX = 5,
	BTC_BCNT_HI_RX = 6,
	BTC_BCNT_LO_TX = 7,
	BTC_BCNT_LO_RX = 8,
	BTC_BCNT_POLLUTED = 9,
	BTC_BCNT_STA_MAX
};

enum rtw89_btc_bt_sta_counter_v105 {
	BTC_BCNT_RFK_REQ_V105 = 0,
	BTC_BCNT_HI_TX_V105 = 1,
	BTC_BCNT_HI_RX_V105 = 2,
	BTC_BCNT_LO_TX_V105 = 3,
	BTC_BCNT_LO_RX_V105 = 4,
	BTC_BCNT_POLLUTED_V105 = 5,
	BTC_BCNT_STA_MAX_V105
};

struct rtw89_btc_fbtc_rpt_ctrl_v1 {
	u16 fver; /* btc_ver::fcxbtcrpt */
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
	u32 bt_rfk_cnt[BTC_BCNT_HI_TX];
	u32 c2h_cnt; /* fw send c2h counter  */
	u32 h2c_cnt; /* fw recv h2c counter */
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_info {
	__le32 cnt; /* fw report counter */
	__le32 en; /* report map */
	__le32 para; /* not used */

	__le32 cnt_c2h; /* fw send c2h counter  */
	__le32 cnt_h2c; /* fw recv h2c counter */
	__le32 len_c2h; /* The total length of the last C2H  */

	__le32 cnt_aoac_rf_on;  /* rf-on counter for aoac switch notify */
	__le32 cnt_aoac_rf_off; /* rf-off counter for aoac switch notify */
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_info_v5 {
	__le32 cx_ver; /* match which driver's coex version */
	__le32 fw_ver;
	__le32 en; /* report map */

	__le16 cnt; /* fw report counter */
	__le16 cnt_c2h; /* fw send c2h counter  */
	__le16 cnt_h2c; /* fw recv h2c counter */
	__le16 len_c2h; /* The total length of the last C2H  */

	__le16 cnt_aoac_rf_on;  /* rf-on counter for aoac switch notify */
	__le16 cnt_aoac_rf_off; /* rf-off counter for aoac switch notify */
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_info_v8 {
	__le16 cnt; /* fw report counter */
	__le16 cnt_c2h; /* fw send c2h counter  */
	__le16 cnt_h2c; /* fw recv h2c counter */
	__le16 len_c2h; /* The total length of the last C2H  */

	__le16 cnt_aoac_rf_on;  /* rf-on counter for aoac switch notify */
	__le16 cnt_aoac_rf_off; /* rf-off counter for aoac switch notify */

	__le32 cx_ver; /* match which driver's coex version */
	__le32 fw_ver;
	__le32 en; /* report map */
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_wl_fw_info {
	__le32 cx_ver; /* match which driver's coex version */
	__le32 cx_offload;
	__le32 fw_ver;
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_a2dp_empty {
	__le32 cnt_empty; /* a2dp empty count */
	__le32 cnt_flowctrl; /* a2dp empty flow control counter */
	__le32 cnt_tx;
	__le32 cnt_ack;
	__le32 cnt_nack;
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_bt_mailbox {
	__le32 cnt_send_ok; /* fw send mailbox ok counter */
	__le32 cnt_send_fail; /* fw send mailbox fail counter */
	__le32 cnt_recv; /* fw recv mailbox counter */
	struct rtw89_btc_fbtc_rpt_ctrl_a2dp_empty a2dp;
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_v4 {
	u8 fver;
	u8 rsvd;
	__le16 rsvd1;
	struct rtw89_btc_fbtc_rpt_ctrl_info rpt_info;
	struct rtw89_btc_fbtc_rpt_ctrl_wl_fw_info wl_fw_info;
	struct rtw89_btc_fbtc_rpt_ctrl_bt_mailbox bt_mbx_info;
	__le32 bt_cnt[BTC_BCNT_STA_MAX];
	struct rtw89_mac_ax_gnt gnt_val[RTW89_PHY_NUM];
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_v5 {
	u8 fver;
	u8 rsvd;
	__le16 rsvd1;

	u8 gnt_val[RTW89_PHY_NUM][4];
	__le16 bt_cnt[BTC_BCNT_STA_MAX];

	struct rtw89_btc_fbtc_rpt_ctrl_info_v5 rpt_info;
	struct rtw89_btc_fbtc_rpt_ctrl_bt_mailbox bt_mbx_info;
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_v105 {
	u8 fver;
	u8 rsvd;
	__le16 rsvd1;

	u8 gnt_val[RTW89_PHY_NUM][4];
	__le16 bt_cnt[BTC_BCNT_STA_MAX_V105];

	struct rtw89_btc_fbtc_rpt_ctrl_info_v5 rpt_info;
	struct rtw89_btc_fbtc_rpt_ctrl_bt_mailbox bt_mbx_info;
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_v7 {
	u8 fver;
	u8 rsvd0;
	u8 rsvd1;
	u8 rsvd2;

	u8 gnt_val[RTW89_PHY_NUM][4];
	__le16 bt_cnt[BTC_BCNT_STA_MAX_V105];

	struct rtw89_btc_fbtc_rpt_ctrl_info_v8 rpt_info;
	struct rtw89_btc_fbtc_rpt_ctrl_bt_mailbox bt_mbx_info;
} __packed;

struct rtw89_btc_fbtc_rpt_ctrl_v8 {
	u8 fver;
	u8 rsvd0;
	u8 rpt_len_max_l; /* BTC_RPT_MAX bit0~7 */
	u8 rpt_len_max_h; /* BTC_RPT_MAX bit8~15 */

	u8 gnt_val[RTW89_PHY_NUM][4];
	__le16 bt_cnt[BTC_BCNT_STA_MAX_V105];

	struct rtw89_btc_fbtc_rpt_ctrl_info_v8 rpt_info;
	struct rtw89_btc_fbtc_rpt_ctrl_bt_mailbox bt_mbx_info;
} __packed;

union rtw89_btc_fbtc_rpt_ctrl_ver_info {
	struct rtw89_btc_fbtc_rpt_ctrl_v1 v1;
	struct rtw89_btc_fbtc_rpt_ctrl_v4 v4;
	struct rtw89_btc_fbtc_rpt_ctrl_v5 v5;
	struct rtw89_btc_fbtc_rpt_ctrl_v105 v105;
	struct rtw89_btc_fbtc_rpt_ctrl_v7 v7;
	struct rtw89_btc_fbtc_rpt_ctrl_v8 v8;
};

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

enum rtw89_btc_cxevnt {
	CXEVNT_TDMA_ENTRY = 0x0,
	CXEVNT_WL_TMR,
	CXEVNT_B1_TMR,
	CXEVNT_B2_TMR,
	CXEVNT_B3_TMR,
	CXEVNT_B4_TMR,
	CXEVNT_W2B_TMR,
	CXEVNT_B2W_TMR,
	CXEVNT_BCN_EARLY,
	CXEVNT_A2DP_EMPTY,
	CXEVNT_LK_END,
	CXEVNT_RX_ISR,
	CXEVNT_RX_FC0,
	CXEVNT_RX_FC1,
	CXEVNT_BT_RELINK,
	CXEVNT_BT_RETRY,
	CXEVNT_E2G,
	CXEVNT_E5G,
	CXEVNT_EBT,
	CXEVNT_ENULL,
	CXEVNT_DRV_WLK,
	CXEVNT_BCN_OK,
	CXEVNT_BT_CHANGE,
	CXEVNT_EBT_EXTEND,
	CXEVNT_E2G_NULL1,
	CXEVNT_B1FDD_TMR,
	CXEVNT_MAX
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

enum rtw89_btc_afh_map_type { /*AFH MAP TYPE */
	RPT_BT_AFH_SEQ_LEGACY = 0x10,
	RPT_BT_AFH_SEQ_LE = 0x20
};

#define BTC_DBG_MAX1  32
struct rtw89_btc_fbtc_gpio_dbg_v1 {
	u8 fver; /* btc_ver::fcxgpiodbg */
	u8 rsvd;
	__le16 rsvd2;
	__le32 en_map; /* which debug signal (see btc_wl_gpio_debug) is enable */
	__le32 pre_state; /* the debug signal is 1 or 0  */
	u8 gpio_map[BTC_DBG_MAX1]; /*the debug signals to GPIO-Position */
} __packed;

struct rtw89_btc_fbtc_gpio_dbg_v7 {
	u8 fver;
	u8 rsvd0;
	u8 rsvd1;
	u8 rsvd2;

	u8 gpio_map[BTC_DBG_MAX1];

	__le32 en_map;
	__le32 pre_state;
} __packed;

union rtw89_btc_fbtc_gpio_dbg {
	struct rtw89_btc_fbtc_gpio_dbg_v1 v1;
	struct rtw89_btc_fbtc_gpio_dbg_v7 v7;
};

struct rtw89_btc_fbtc_mreg_val_v1 {
	u8 fver; /* btc_ver::fcxmreg */
	u8 reg_num;
	__le16 rsvd;
	__le32 mreg_val[CXMREG_MAX];
} __packed;

struct rtw89_btc_fbtc_mreg_val_v2 {
	u8 fver; /* btc_ver::fcxmreg */
	u8 reg_num;
	__le16 rsvd;
	__le32 mreg_val[CXMREG_MAX_V2];
} __packed;

struct rtw89_btc_fbtc_mreg_val_v7 {
	u8 fver;
	u8 reg_num;
	u8 rsvd0;
	u8 rsvd1;
	__le32 mreg_val[CXMREG_MAX_V2];
} __packed;

union rtw89_btc_fbtc_mreg_val {
	struct rtw89_btc_fbtc_mreg_val_v1 v1;
	struct rtw89_btc_fbtc_mreg_val_v2 v2;
	struct rtw89_btc_fbtc_mreg_val_v7 v7;
};

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

struct rtw89_btc_fbtc_slots {
	u8 fver; /* btc_ver::fcxslots */
	u8 tbl_num;
	__le16 rsvd;
	__le32 update_map;
	struct rtw89_btc_fbtc_slot slot[CXST_MAX];
} __packed;

struct rtw89_btc_fbtc_slot_v7 {
	__le16 dur; /* slot duration */
	__le16 cxtype;
	__le32 cxtbl;
} __packed;

struct rtw89_btc_fbtc_slot_u16 {
	__le16 dur; /* slot duration */
	__le16 cxtype;
	__le16 cxtbl_l16; /* coex table [15:0] */
	__le16 cxtbl_h16; /* coex table [31:16] */
} __packed;

struct rtw89_btc_fbtc_1slot_v7 {
	u8 fver;
	u8 sid; /* slot id */
	__le16 rsvd;
	struct rtw89_btc_fbtc_slot_v7 slot;
} __packed;

struct rtw89_btc_fbtc_slots_v7 {
	u8 fver;
	u8 slot_cnt;
	u8 rsvd0;
	u8 rsvd1;
	struct rtw89_btc_fbtc_slot_u16 slot[CXST_MAX];
	__le32 update_map;
} __packed;

union rtw89_btc_fbtc_slots_info {
	struct rtw89_btc_fbtc_slots v1;
	struct rtw89_btc_fbtc_slots_v7 v7;
} __packed;

struct rtw89_btc_fbtc_step {
	u8 type;
	u8 val;
	__le16 difft;
} __packed;

struct rtw89_btc_fbtc_steps_v2 {
	u8 fver; /* btc_ver::fcxstep */
	u8 rsvd;
	__le16 cnt;
	__le16 pos_old;
	__le16 pos_new;
	struct rtw89_btc_fbtc_step step[FCXMAX_STEP];
} __packed;

struct rtw89_btc_fbtc_steps_v3 {
	u8 fver;
	u8 en;
	__le16 rsvd;
	__le32 cnt;
	struct rtw89_btc_fbtc_step step[FCXMAX_STEP];
} __packed;

union rtw89_btc_fbtc_steps_info {
	struct rtw89_btc_fbtc_steps_v2 v2;
	struct rtw89_btc_fbtc_steps_v3 v3;
};

struct rtw89_btc_fbtc_cysta_v2 { /* statistics for cycles */
	u8 fver; /* btc_ver::fcxcysta */
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

struct rtw89_btc_fbtc_fdd_try_info {
	__le16 cycles[CXT_FLCTRL_MAX];
	__le16 tavg[CXT_FLCTRL_MAX]; /* avg try BT-Slot-TDD/BT-slot-FDD time */
	__le16 tmax[CXT_FLCTRL_MAX]; /* max try BT-Slot-TDD/BT-slot-FDD time */
} __packed;

struct rtw89_btc_fbtc_cycle_time_info {
	__le16 tavg[CXT_MAX]; /* avg wl/bt cycle time */
	__le16 tmax[CXT_MAX]; /* max wl/bt cycle time */
	__le16 tmaxdiff[CXT_MAX]; /* max wl-wl bt-bt cycle diff time */
} __packed;

struct rtw89_btc_fbtc_cycle_time_info_v5 {
	__le16 tavg[CXT_MAX]; /* avg wl/bt cycle time */
	__le16 tmax[CXT_MAX]; /* max wl/bt cycle time */
} __packed;

struct rtw89_btc_fbtc_a2dp_trx_stat {
	u8 empty_cnt;
	u8 retry_cnt;
	u8 tx_rate;
	u8 tx_cnt;
	u8 ack_cnt;
	u8 nack_cnt;
	u8 rsvd1;
	u8 rsvd2;
} __packed;

struct rtw89_btc_fbtc_a2dp_trx_stat_v4 {
	u8 empty_cnt;
	u8 retry_cnt;
	u8 tx_rate;
	u8 tx_cnt;
	u8 ack_cnt;
	u8 nack_cnt;
	u8 no_empty_cnt;
	u8 rsvd;
} __packed;

struct rtw89_btc_fbtc_cycle_a2dp_empty_info {
	__le16 cnt; /* a2dp empty cnt */
	__le16 cnt_timeout; /* a2dp empty timeout cnt*/
	__le16 tavg; /* avg a2dp empty time */
	__le16 tmax; /* max a2dp empty time */
} __packed;

struct rtw89_btc_fbtc_cycle_leak_info {
	__le32 cnt_rximr; /* the rximr occur at leak slot  */
	__le16 tavg; /* avg leak-slot time */
	__le16 tmax; /* max leak-slot time */
} __packed;

struct rtw89_btc_fbtc_cycle_leak_info_v7 {
	__le16 tavg;
	__le16 tamx;
	__le32 cnt_rximr;
} __packed;

#define RTW89_BTC_FDDT_PHASE_CYCLE GENMASK(9, 0)
#define RTW89_BTC_FDDT_TRAIN_STEP GENMASK(15, 10)

struct rtw89_btc_fbtc_cycle_fddt_info {
	__le16 train_cycle;
	__le16 tp;

	s8 tx_power; /* absolute Tx power (dBm), 0xff-> no BTC control */
	s8 bt_tx_power; /* decrease Tx power (dB) */
	s8 bt_rx_gain;  /* LNA constrain level */
	u8 no_empty_cnt;

	u8 rssi; /* [7:4] -> bt_rssi_level, [3:0]-> wl_rssi_level */
	u8 cn; /* condition_num */
	u8 train_status; /* [7:4]-> train-state, [3:0]-> train-phase */
	u8 train_result; /* refer to enum btc_fddt_check_map */
} __packed;

#define RTW89_BTC_FDDT_CELL_TRAIN_STATE GENMASK(3, 0)
#define RTW89_BTC_FDDT_CELL_TRAIN_PHASE GENMASK(7, 4)

struct rtw89_btc_fbtc_cycle_fddt_info_v5 {
	__le16 train_cycle;
	__le16 tp;

	s8 tx_power; /* absolute Tx power (dBm), 0xff-> no BTC control */
	s8 bt_tx_power; /* decrease Tx power (dB) */
	s8 bt_rx_gain;  /* LNA constrain level */
	u8 no_empty_cnt;

	u8 rssi; /* [7:4] -> bt_rssi_level, [3:0]-> wl_rssi_level */
	u8 cn; /* condition_num */
	u8 train_status; /* [7:4]-> train-state, [3:0]-> train-phase */
	u8 train_result; /* refer to enum btc_fddt_check_map */
} __packed;

struct rtw89_btc_fbtc_fddt_cell_status {
	s8 wl_tx_pwr;
	s8 bt_tx_pwr;
	s8 bt_rx_gain;
	u8 state_phase; /* [0:3] train state, [4:7] train phase */
} __packed;

struct rtw89_btc_fbtc_cysta_v3 { /* statistics for cycles */
	u8 fver;
	u8 rsvd;
	__le16 cycles; /* total cycle number */
	__le16 slot_step_time[BTC_CYCLE_SLOT_MAX];
	struct rtw89_btc_fbtc_cycle_time_info cycle_time;
	struct rtw89_btc_fbtc_fdd_try_info fdd_try;
	struct rtw89_btc_fbtc_cycle_a2dp_empty_info a2dp_ept;
	struct rtw89_btc_fbtc_a2dp_trx_stat a2dp_trx[BTC_CYCLE_SLOT_MAX];
	struct rtw89_btc_fbtc_cycle_leak_info leak_slot;
	__le32 slot_cnt[CXST_MAX]; /* slot count */
	__le32 bcn_cnt[CXBCN_MAX];
	__le32 collision_cnt; /* counter for event/timer occur at the same time */
	__le32 skip_cnt;
	__le32 except_cnt;
	__le32 except_map;
} __packed;

#define FDD_TRAIN_WL_DIRECTION 2
#define FDD_TRAIN_WL_RSSI_LEVEL 5
#define FDD_TRAIN_BT_RSSI_LEVEL 5

struct rtw89_btc_fbtc_cysta_v4 { /* statistics for cycles */
	u8 fver;
	u8 rsvd;
	u8 collision_cnt; /* counter for event/timer occur at the same time */
	u8 except_cnt;

	__le16 skip_cnt;
	__le16 cycles; /* total cycle number */

	__le16 slot_step_time[BTC_CYCLE_SLOT_MAX]; /* record the wl/bt slot time */
	__le16 slot_cnt[CXST_MAX]; /* slot count */
	__le16 bcn_cnt[CXBCN_MAX];
	struct rtw89_btc_fbtc_cycle_time_info cycle_time;
	struct rtw89_btc_fbtc_cycle_leak_info leak_slot;
	struct rtw89_btc_fbtc_cycle_a2dp_empty_info a2dp_ept;
	struct rtw89_btc_fbtc_a2dp_trx_stat_v4 a2dp_trx[BTC_CYCLE_SLOT_MAX];
	struct rtw89_btc_fbtc_cycle_fddt_info fddt_trx[BTC_CYCLE_SLOT_MAX];
	struct rtw89_btc_fbtc_fddt_cell_status fddt_cells[FDD_TRAIN_WL_DIRECTION]
							 [FDD_TRAIN_WL_RSSI_LEVEL]
							 [FDD_TRAIN_BT_RSSI_LEVEL];
	__le32 except_map;
} __packed;

struct rtw89_btc_fbtc_cysta_v5 { /* statistics for cycles */
	u8 fver;
	u8 rsvd;
	u8 collision_cnt; /* counter for event/timer occur at the same time */
	u8 except_cnt;
	u8 wl_rx_err_ratio[BTC_CYCLE_SLOT_MAX];

	__le16 skip_cnt;
	__le16 cycles; /* total cycle number */

	__le16 slot_step_time[BTC_CYCLE_SLOT_MAX]; /* record the wl/bt slot time */
	__le16 slot_cnt[CXST_MAX]; /* slot count */
	__le16 bcn_cnt[CXBCN_MAX];
	struct rtw89_btc_fbtc_cycle_time_info_v5 cycle_time;
	struct rtw89_btc_fbtc_cycle_leak_info leak_slot;
	struct rtw89_btc_fbtc_cycle_a2dp_empty_info a2dp_ept;
	struct rtw89_btc_fbtc_a2dp_trx_stat_v4 a2dp_trx[BTC_CYCLE_SLOT_MAX];
	struct rtw89_btc_fbtc_cycle_fddt_info_v5 fddt_trx[BTC_CYCLE_SLOT_MAX];
	struct rtw89_btc_fbtc_fddt_cell_status fddt_cells[FDD_TRAIN_WL_DIRECTION]
							 [FDD_TRAIN_WL_RSSI_LEVEL]
							 [FDD_TRAIN_BT_RSSI_LEVEL];
	__le32 except_map;
} __packed;

struct rtw89_btc_fbtc_cysta_v7 { /* statistics for cycles */
	u8 fver;
	u8 rsvd;
	u8 collision_cnt; /* counter for event/timer occur at the same time */
	u8 except_cnt;

	u8 wl_rx_err_ratio[BTC_CYCLE_SLOT_MAX];

	struct rtw89_btc_fbtc_a2dp_trx_stat_v4 a2dp_trx[BTC_CYCLE_SLOT_MAX];

	__le16 skip_cnt;
	__le16 cycles; /* total cycle number */

	__le16 slot_step_time[BTC_CYCLE_SLOT_MAX]; /* record the wl/bt slot time */
	__le16 slot_cnt[CXST_MAX]; /* slot count */
	__le16 bcn_cnt[CXBCN_MAX];

	struct rtw89_btc_fbtc_cycle_time_info_v5 cycle_time;
	struct rtw89_btc_fbtc_cycle_a2dp_empty_info a2dp_ept;
	struct rtw89_btc_fbtc_cycle_leak_info_v7 leak_slot;

	__le32 except_map;
} __packed;

union rtw89_btc_fbtc_cysta_info {
	struct rtw89_btc_fbtc_cysta_v2 v2;
	struct rtw89_btc_fbtc_cysta_v3 v3;
	struct rtw89_btc_fbtc_cysta_v4 v4;
	struct rtw89_btc_fbtc_cysta_v5 v5;
	struct rtw89_btc_fbtc_cysta_v7 v7;
};

struct rtw89_btc_fbtc_cynullsta_v1 { /* cycle null statistics */
	u8 fver; /* btc_ver::fcxnullsta */
	u8 rsvd;
	__le16 rsvd2;
	__le32 max_t[2]; /* max_t for 0:null0/1:null1 */
	__le32 avg_t[2]; /* avg_t for 0:null0/1:null1 */
	__le32 result[2][4]; /* 0:fail, 1:ok, 2:on_time, 3:retry */
} __packed;

struct rtw89_btc_fbtc_cynullsta_v2 { /* cycle null statistics */
	u8 fver; /* btc_ver::fcxnullsta */
	u8 rsvd;
	__le16 rsvd2;
	__le32 max_t[2]; /* max_t for 0:null0/1:null1 */
	__le32 avg_t[2]; /* avg_t for 0:null0/1:null1 */
	__le32 result[2][5]; /* 0:fail, 1:ok, 2:on_time, 3:retry, 4:tx */
} __packed;

struct rtw89_btc_fbtc_cynullsta_v7 { /* cycle null statistics */
	u8 fver;
	u8 rsvd0;
	u8 rsvd1;
	u8 rsvd2;

	__le32 tmax[2];
	__le32 tavg[2];
	__le32 result[2][5];
} __packed;

union rtw89_btc_fbtc_cynullsta_info {
	struct rtw89_btc_fbtc_cynullsta_v1 v1; /* info from fw */
	struct rtw89_btc_fbtc_cynullsta_v2 v2;
	struct rtw89_btc_fbtc_cynullsta_v7 v7;
};

struct rtw89_btc_fbtc_btver_v1 {
	u8 fver; /* btc_ver::fcxbtver */
	u8 rsvd;
	__le16 rsvd2;
	__le32 coex_ver; /*bit[15:8]->shared, bit[7:0]->non-shared */
	__le32 fw_ver;
	__le32 feature;
} __packed;

struct rtw89_btc_fbtc_btver_v7 {
	u8 fver;
	u8 rsvd0;
	u8 rsvd1;
	u8 rsvd2;

	__le32 coex_ver; /*bit[15:8]->shared, bit[7:0]->non-shared */
	__le32 fw_ver;
	__le32 feature;
} __packed;

union rtw89_btc_fbtc_btver {
	struct rtw89_btc_fbtc_btver_v1 v1;
	struct rtw89_btc_fbtc_btver_v7 v7;
} __packed;

struct rtw89_btc_fbtc_btafh {
	u8 fver; /* btc_ver::fcxbtafh */
	u8 rsvd;
	__le16 rsvd2;
	u8 afh_l[4]; /*bit0:2402, bit1: 2403.... bit31:2433 */
	u8 afh_m[4]; /*bit0:2434, bit1: 2435.... bit31:2465 */
	u8 afh_h[4]; /*bit0:2466, bit1:2467......bit14:2480 */
} __packed;

struct rtw89_btc_fbtc_btafh_v2 {
	u8 fver; /* btc_ver::fcxbtafh */
	u8 rsvd;
	u8 rsvd2;
	u8 map_type;
	u8 afh_l[4];
	u8 afh_m[4];
	u8 afh_h[4];
	u8 afh_le_a[4];
	u8 afh_le_b[4];
} __packed;

struct rtw89_btc_fbtc_btafh_v7 {
	u8 fver;
	u8 map_type;
	u8 rsvd0;
	u8 rsvd1;
	u8 afh_l[4]; /*bit0:2402, bit1:2403.... bit31:2433 */
	u8 afh_m[4]; /*bit0:2434, bit1:2435.... bit31:2465 */
	u8 afh_h[4]; /*bit0:2466, bit1:2467.....bit14:2480 */
	u8 afh_le_a[4];
	u8 afh_le_b[4];
} __packed;

struct rtw89_btc_fbtc_btdevinfo {
	u8 fver; /* btc_ver::fcxbtdevinfo */
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

struct rtw89_btc_trx_info {
	u8 tx_lvl;
	u8 rx_lvl;
	u8 wl_rssi;
	u8 bt_rssi;

	s8 tx_power; /* absolute Tx power (dBm), 0xff-> no BTC control */
	s8 rx_gain;  /* rx gain table index (TBD.) */
	s8 bt_tx_power; /* decrease Tx power (dB) */
	s8 bt_rx_gain;  /* LNA constrain level */

	u8 cn; /* condition_num */
	s8 nhm;
	u8 bt_profile;
	u8 rsvd2;

	u16 tx_rate;
	u16 rx_rate;

	u32 tx_tp;
	u32 rx_tp;
	u32 rx_err_ratio;
};

union rtw89_btc_fbtc_slot_u {
	struct rtw89_btc_fbtc_slot v1[CXST_MAX];
	struct rtw89_btc_fbtc_slot_v7 v7[CXST_MAX];
};

struct rtw89_btc_dm {
	union rtw89_btc_fbtc_slot_u slot;
	union rtw89_btc_fbtc_slot_u slot_now;
	struct rtw89_btc_fbtc_tdma tdma;
	struct rtw89_btc_fbtc_tdma tdma_now;
	struct rtw89_mac_ax_coex_gnt gnt;
	union rtw89_btc_init_info_u init_info; /* pass to wl_fw if offload */
	struct rtw89_btc_rf_trx_para rf_trx_para;
	struct rtw89_btc_wl_tx_limit_para wl_tx_limit;
	struct rtw89_btc_dm_step dm_step;
	struct rtw89_btc_wl_scc_ctrl wl_scc;
	struct rtw89_btc_trx_info trx_info;
	union rtw89_btc_dm_error_map error;
	u32 cnt_dm[BTC_DCNT_NUM];
	u32 cnt_notify[BTC_NCNT_NUM];

	u32 update_slot_map;
	u32 set_ant_path;
	u32 e2g_slot_limit;
	u32 e2g_slot_nulltx_time;

	u32 wl_only: 1;
	u32 wl_fw_cx_offload: 1;
	u32 freerun: 1;
	u32 fddt_train: 1;
	u32 wl_ps_ctrl: 2;
	u32 wl_mimo_ps: 1;
	u32 leak_ap: 1;
	u32 noisy_level: 3;
	u32 coex_info_map: 8;
	u32 bt_only: 1;
	u32 wl_btg_rx: 2;
	u32 trx_para_level: 8;
	u32 wl_stb_chg: 1;
	u32 pta_owner: 1;

	u32 tdma_instant_excute: 1;
	u32 wl_btg_rx_rb: 2;

	u16 slot_dur[CXST_MAX];
	u16 bt_slot_flood;

	u8 run_reason;
	u8 run_action;

	u8 wl_pre_agc: 2;
	u8 wl_lna2: 1;
	u8 freerun_chk: 1;
	u8 wl_pre_agc_rb: 2;
	u8 bt_select: 2; /* 0:s0, 1:s1, 2:s0 & s1, refer to enum btc_bt_index */
	u8 slot_req_more: 1;
};

struct rtw89_btc_ctrl {
	u32 manual: 1;
	u32 igno_bt: 1;
	u32 always_freerun: 1;
	u32 trace_step: 16;
	u32 rsvd: 12;
};

struct rtw89_btc_ctrl_v7 {
	u8 manual;
	u8 igno_bt;
	u8 always_freerun;
	u8 rsvd;
} __packed;

union rtw89_btc_ctrl_list {
	struct rtw89_btc_ctrl ctrl;
	struct rtw89_btc_ctrl_v7 ctrl_v7;
};

struct rtw89_btc_dbg {
	/* cmd "rb" */
	bool rb_done;
	u32 rb_val;
};

enum rtw89_btc_btf_fw_event {
	BTF_EVNT_RPT = 0,
	BTF_EVNT_BT_INFO = 1,
	BTF_EVNT_BT_SCBD = 2,
	BTF_EVNT_BT_REG = 3,
	BTF_EVNT_CX_RUNINFO = 4,
	BTF_EVNT_BT_PSD = 5,
	BTF_EVNT_BT_DEV_INFO = 6, /* fwc2hfunc > 0 */
	BTF_EVNT_BT_LEAUDIO_INFO = 7, /* fwc2hfunc > 1 */
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
	BTC_RPT_TYPE_FDDT, /* added by ver->fwevntrptl == 1 */
	BTC_RPT_TYPE_MREG,
	BTC_RPT_TYPE_GPIO_DBG,
	BTC_RPT_TYPE_BT_VER,
	BTC_RPT_TYPE_BT_SCAN,
	BTC_RPT_TYPE_BT_AFH,
	BTC_RPT_TYPE_BT_DEVICE,
	BTC_RPT_TYPE_TEST,
	BTC_RPT_TYPE_MAX = 31,

	__BTC_RPT_TYPE_V0_SAME = BTC_RPT_TYPE_NULLSTA,
	__BTC_RPT_TYPE_V0_MAX = 12,
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

union rtw89_btc_fbtc_btafh_info {
	struct rtw89_btc_fbtc_btafh v1;
	struct rtw89_btc_fbtc_btafh_v2 v2;
	struct rtw89_btc_fbtc_btafh_v7 v7;
};

struct rtw89_btc_report_ctrl_state {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_rpt_ctrl_ver_info finfo;
};

struct rtw89_btc_rpt_fbtc_tdma {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_tdma_le32 finfo;
};

struct rtw89_btc_rpt_fbtc_slots {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_slots_info finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_cysta {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_cysta_info finfo;
};

struct rtw89_btc_rpt_fbtc_step {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_steps_info finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_nullsta {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_cynullsta_info finfo;
};

struct rtw89_btc_rpt_fbtc_mreg {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_mreg_val finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_gpio_dbg {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_gpio_dbg finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btver {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_btver finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btscan {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_btscan finfo; /* info from fw */
};

struct rtw89_btc_rpt_fbtc_btafh {
	struct rtw89_btc_rpt_cmn_info cinfo; /* common info, by driver */
	union rtw89_btc_fbtc_btafh_info finfo;
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

struct rtw89_btc_ver {
	enum rtw89_core_chip_id chip_id;
	u32 fw_ver_code;

	u8 fcxbtcrpt;
	u8 fcxtdma;
	u8 fcxslots;
	u8 fcxcysta;
	u8 fcxstep;
	u8 fcxnullsta;
	u8 fcxmreg;
	u8 fcxgpiodbg;
	u8 fcxbtver;
	u8 fcxbtscan;
	u8 fcxbtafh;
	u8 fcxbtdevinfo;
	u8 fwlrole;
	u8 frptmap;
	u8 fcxctrl;
	u8 fcxinit;

	u8 fwevntrptl;
	u8 fwc2hfunc;
	u8 drvinfo_type;
	u16 info_buf;
	u8 max_role_num;
};

#define RTW89_BTC_POLICY_MAXLEN 512

struct rtw89_btc {
	const struct rtw89_btc_ver *ver;

	struct rtw89_btc_cx cx;
	struct rtw89_btc_dm dm;
	union rtw89_btc_ctrl_list ctrl;
	union rtw89_btc_module_info mdinfo;
	struct rtw89_btc_btf_fwinfo fwinfo;
	struct rtw89_btc_dbg dbg;

	struct wiphy_work eapol_notify_work;
	struct wiphy_work arp_notify_work;
	struct wiphy_work dhcp_notify_work;
	struct wiphy_work icmp_notify_work;

	u32 bt_req_len;

	u8 policy[RTW89_BTC_POLICY_MAXLEN];
	u8 ant_type;
	u8 btg_pos;
	u16 policy_len;
	u16 policy_type;
	u32 hubmsg_cnt;
	bool bt_req_en;
	bool update_policy_force;
	bool lps;
	bool manual_ctrl;
};

enum rtw89_btc_hmsg {
	RTW89_BTC_HMSG_TMR_EN = 0x0,
	RTW89_BTC_HMSG_BT_REG_READBACK = 0x1,
	RTW89_BTC_HMSG_SET_BT_REQ_SLOT = 0x2,
	RTW89_BTC_HMSG_FW_EV = 0x3,
	RTW89_BTC_HMSG_BT_LINK_CHG = 0x4,
	RTW89_BTC_HMSG_SET_BT_REQ_STBC = 0x5,

	NUM_OF_RTW89_BTC_HMSG,
};

enum rtw89_ra_mode {
	RTW89_RA_MODE_CCK = BIT(0),
	RTW89_RA_MODE_OFDM = BIT(1),
	RTW89_RA_MODE_HT = BIT(2),
	RTW89_RA_MODE_VHT = BIT(3),
	RTW89_RA_MODE_HE = BIT(4),
	RTW89_RA_MODE_EHT = BIT(5),
};

enum rtw89_ra_report_mode {
	RTW89_RA_RPT_MODE_LEGACY,
	RTW89_RA_RPT_MODE_HT,
	RTW89_RA_RPT_MODE_VHT,
	RTW89_RA_RPT_MODE_HE,
	RTW89_RA_RPT_MODE_EHT,
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

enum rtw89_efuse_block {
	RTW89_EFUSE_BLOCK_SYS = 0,
	RTW89_EFUSE_BLOCK_RF = 1,
	RTW89_EFUSE_BLOCK_HCI_DIG_PCIE_SDIO = 2,
	RTW89_EFUSE_BLOCK_HCI_DIG_USB = 3,
	RTW89_EFUSE_BLOCK_HCI_PHY_PCIE = 4,
	RTW89_EFUSE_BLOCK_HCI_PHY_USB3 = 5,
	RTW89_EFUSE_BLOCK_HCI_PHY_USB2 = 6,
	RTW89_EFUSE_BLOCK_ADIE = 7,

	RTW89_EFUSE_BLOCK_NUM,
	RTW89_EFUSE_BLOCK_IGNORE,
};

struct rtw89_ra_info {
	u8 is_dis_ra:1;
	/* Bit0 : CCK
	 * Bit1 : OFDM
	 * Bit2 : HT
	 * Bit3 : VHT
	 * Bit4 : HE
	 * Bit5 : EHT
	 */
	u8 mode_ctrl:6;
	u8 bw_cap:3; /* enum rtw89_bandwidth */
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
	u8 fix_giltf_en:1;
	u8 fix_giltf:3;
	u8 rsvd2:1;
	u8 csi_mcs_ss_idx;
	u8 csi_mode:2;
	u8 csi_gi_ltf:3;
	u8 csi_bw:3;
};

#define RTW89_PPDU_MAC_INFO_USR_SIZE 4
#define RTW89_PPDU_MAC_INFO_SIZE 8
#define RTW89_PPDU_MAC_RX_CNT_SIZE 96
#define RTW89_PPDU_MAC_RX_CNT_SIZE_V1 128

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
	bool might_fallback_legacy;
};

DECLARE_EWMA(rssi, 10, 16);
DECLARE_EWMA(evm, 10, 16);
DECLARE_EWMA(snr, 10, 16);

struct rtw89_ba_cam_entry {
	struct list_head list;
	u8 tid;
};

#define RTW89_MAX_ADDR_CAM_NUM		128
#define RTW89_MAX_BSSID_CAM_NUM		20
#define RTW89_MAX_SEC_CAM_NUM		128
#define RTW89_MAX_BA_CAM_NUM		24
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

	struct ieee80211_key_conf *key_conf;
};

struct rtw89_sta_link {
	struct rtw89_sta *rtwsta;
	struct list_head dlink_schd;
	unsigned int link_id;

	u8 mac_id;
	bool er_cap;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_ra_info ra;
	struct rtw89_ra_report ra_report;
	int max_agg_wait;
	u8 prev_rssi;
	struct ewma_rssi avg_rssi;
	struct ewma_rssi rssi[RF_PATH_MAX];
	struct ewma_snr avg_snr;
	struct ewma_evm evm_1ss;
	struct ewma_evm evm_min[RF_PATH_MAX];
	struct ewma_evm evm_max[RF_PATH_MAX];
	struct ieee80211_rx_status rx_status;
	u16 rx_hw_rate;
	__le32 htc_template;
	struct rtw89_addr_cam_entry addr_cam; /* AP mode or TDLS peer only */
	struct rtw89_bssid_cam_entry bssid_cam; /* TDLS peer only */
	struct list_head ba_cam_list;

	bool use_cfg_mask;
	struct cfg80211_bitrate_mask mask;

	bool cctl_tx_time;
	u32 ampdu_max_time:4;
	bool cctl_tx_retry_limit;
	u32 data_tx_cnt_lmt:6;
};

struct rtw89_efuse {
	bool valid;
	bool power_k_valid;
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

struct rtw89_tx_wait_info {
	struct rcu_head rcu_head;
	struct completion completion;
	bool tx_done;
};

struct rtw89_tx_skb_data {
	struct rtw89_tx_wait_info __rcu *wait;
	u8 hci_priv[];
};

#define RTW89_ROC_IDLE_TIMEOUT 500
#define RTW89_ROC_TX_TIMEOUT 30
enum rtw89_roc_state {
	RTW89_ROC_IDLE,
	RTW89_ROC_NORMAL,
	RTW89_ROC_MGMT,
};

struct rtw89_roc {
	struct ieee80211_channel chan;
	struct wiphy_delayed_work roc_work;
	enum ieee80211_roc_type type;
	enum rtw89_roc_state state;
	int duration;
	unsigned int link_id;
};

#define RTW89_P2P_MAX_NOA_NUM 2

struct rtw89_p2p_ie_head {
	u8 eid;
	u8 ie_len;
	u8 oui[3];
	u8 oui_type;
} __packed;

struct rtw89_noa_attr_head {
	u8 attr_type;
	__le16 attr_len;
	u8 index;
	u8 oppps_ctwindow;
} __packed;

struct rtw89_p2p_noa_ie {
	struct rtw89_p2p_ie_head p2p_head;
	struct rtw89_noa_attr_head noa_head;
	struct ieee80211_p2p_noa_desc noa_desc[RTW89_P2P_MAX_NOA_NUM];
} __packed;

struct rtw89_p2p_noa_setter {
	struct rtw89_p2p_noa_ie ie;
	u8 noa_count;
	u8 noa_index;
};

struct rtw89_ps_noa_once_handler {
	bool in_duration;
	u64 tsf_begin;
	u64 tsf_end;
	struct wiphy_delayed_work set_work;
	struct wiphy_delayed_work clr_work;
};

struct rtw89_vif_link {
	struct rtw89_vif *rtwvif;
	struct list_head dlink_schd;
	unsigned int link_id;

	bool chanctx_assigned; /* only valid when running with chanctx_ops */
	enum rtw89_chanctx_idx chanctx_idx;
	enum rtw89_reg_6ghz_power reg_6ghz_power;
	struct rtw89_reg_6ghz_tpe reg_6ghz_tpe;

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
	u8 bcn_bw_idx;
	u8 hit_rule;
	u8 last_noa_nr;
	u64 sync_bcn_tsf;
	bool rand_tsf_done;
	bool trigger;
	bool lsig_txop;
	u8 tgt_ind;
	u8 frm_tgt_ind;
	bool wowlan_pattern;
	bool wowlan_uc;
	bool wowlan_magic;
	bool is_hesta;
	bool last_a_ctrl;
	bool dyn_tb_bedge_en;
	bool pre_pwr_diff_en;
	bool pwr_diff_en;
	u8 def_tri_idx;
	struct wiphy_work update_beacon_work;
	struct wiphy_delayed_work csa_beacon_work;
	struct rtw89_addr_cam_entry addr_cam;
	struct rtw89_bssid_cam_entry bssid_cam;
	struct ieee80211_tx_queue_params tx_params[IEEE80211_NUM_ACS];
	struct rtw89_phy_rate_pattern rate_pattern;
	struct list_head general_pkt_list;
	struct rtw89_p2p_noa_setter p2p_noa;
	struct rtw89_ps_noa_once_handler noa_once;
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
	void (*pause)(struct rtw89_dev *rtwdev, bool pause);
	void (*switch_mode)(struct rtw89_dev *rtwdev, bool low_power);
	void (*recalc_int_mit)(struct rtw89_dev *rtwdev);

	u8 (*read8)(struct rtw89_dev *rtwdev, u32 addr);
	u16 (*read16)(struct rtw89_dev *rtwdev, u32 addr);
	u32 (*read32)(struct rtw89_dev *rtwdev, u32 addr);
	void (*write8)(struct rtw89_dev *rtwdev, u32 addr, u8 data);
	void (*write16)(struct rtw89_dev *rtwdev, u32 addr, u16 data);
	void (*write32)(struct rtw89_dev *rtwdev, u32 addr, u32 data);

	int (*mac_pre_init)(struct rtw89_dev *rtwdev);
	int (*mac_pre_deinit)(struct rtw89_dev *rtwdev);
	int (*mac_post_init)(struct rtw89_dev *rtwdev);
	int (*deinit)(struct rtw89_dev *rtwdev);

	u32 (*check_and_reclaim_tx_resource)(struct rtw89_dev *rtwdev, u8 txch);
	int (*mac_lv1_rcvy)(struct rtw89_dev *rtwdev, enum rtw89_lv1_rcvy_step step);
	void (*dump_err_status)(struct rtw89_dev *rtwdev);
	int (*napi_poll)(struct napi_struct *napi, int budget);

	/* Deal with locks inside recovery_start and recovery_complete callbacks
	 * by hci instance, and handle things which need to consider under SER.
	 * e.g. turn on/off interrupts except for the one for halt notification.
	 */
	void (*recovery_start)(struct rtw89_dev *rtwdev);
	void (*recovery_complete)(struct rtw89_dev *rtwdev);

	void (*ctrl_txdma_ch)(struct rtw89_dev *rtwdev, bool enable);
	void (*ctrl_txdma_fw_ch)(struct rtw89_dev *rtwdev, bool enable);
	void (*ctrl_trxhci)(struct rtw89_dev *rtwdev, bool enable);
	int (*poll_txdma_ch_idle)(struct rtw89_dev *rtwdev);
	void (*clr_idx_all)(struct rtw89_dev *rtwdev);
	void (*clear)(struct rtw89_dev *rtwdev, struct pci_dev *pdev);
	void (*disable_intr)(struct rtw89_dev *rtwdev);
	void (*enable_intr)(struct rtw89_dev *rtwdev);
	int (*rst_bdram)(struct rtw89_dev *rtwdev);
};

struct rtw89_hci_info {
	const struct rtw89_hci_ops *ops;
	enum rtw89_hci_type type;
	u32 rpwm_addr;
	u32 cpwm_addr;
	bool paused;
};

struct rtw89_chip_ops {
	int (*enable_bb_rf)(struct rtw89_dev *rtwdev);
	int (*disable_bb_rf)(struct rtw89_dev *rtwdev);
	void (*bb_preinit)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
	void (*bb_postinit)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
	void (*bb_reset)(struct rtw89_dev *rtwdev,
			 enum rtw89_phy_idx phy_idx);
	void (*bb_sethw)(struct rtw89_dev *rtwdev);
	u32 (*read_rf)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
		       u32 addr, u32 mask);
	bool (*write_rf)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path,
			 u32 addr, u32 mask, u32 data);
	void (*set_channel)(struct rtw89_dev *rtwdev,
			    const struct rtw89_chan *chan,
			    enum rtw89_mac_idx mac_idx,
			    enum rtw89_phy_idx phy_idx);
	void (*set_channel_help)(struct rtw89_dev *rtwdev, bool enter,
				 struct rtw89_channel_help_params *p,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx);
	int (*read_efuse)(struct rtw89_dev *rtwdev, u8 *log_map,
			  enum rtw89_efuse_block block);
	int (*read_phycap)(struct rtw89_dev *rtwdev, u8 *phycap_map);
	void (*fem_setup)(struct rtw89_dev *rtwdev);
	void (*rfe_gpio)(struct rtw89_dev *rtwdev);
	void (*rfk_hw_init)(struct rtw89_dev *rtwdev);
	void (*rfk_init)(struct rtw89_dev *rtwdev);
	void (*rfk_init_late)(struct rtw89_dev *rtwdev);
	void (*rfk_channel)(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link);
	void (*rfk_band_changed)(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx,
				 const struct rtw89_chan *chan);
	void (*rfk_scan)(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			 bool start);
	void (*rfk_track)(struct rtw89_dev *rtwdev);
	void (*power_trim)(struct rtw89_dev *rtwdev);
	void (*set_txpwr)(struct rtw89_dev *rtwdev,
			  const struct rtw89_chan *chan,
			  enum rtw89_phy_idx phy_idx);
	void (*set_txpwr_ctrl)(struct rtw89_dev *rtwdev,
			       enum rtw89_phy_idx phy_idx);
	int (*init_txpwr_unit)(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx);
	u8 (*get_thermal)(struct rtw89_dev *rtwdev, enum rtw89_rf_path rf_path);
	u32 (*chan_to_rf18_val)(struct rtw89_dev *rtwdev,
				const struct rtw89_chan *chan);
	void (*ctrl_btg_bt_rx)(struct rtw89_dev *rtwdev, bool en,
			       enum rtw89_phy_idx phy_idx);
	void (*query_ppdu)(struct rtw89_dev *rtwdev,
			   struct rtw89_rx_phy_ppdu *phy_ppdu,
			   struct ieee80211_rx_status *status);
	void (*convert_rpl_to_rssi)(struct rtw89_dev *rtwdev,
				    struct rtw89_rx_phy_ppdu *phy_ppdu);
	void (*phy_rpt_to_rssi)(struct rtw89_dev *rtwdev,
				struct rtw89_rx_desc_info *desc_info,
				struct ieee80211_rx_status *rx_status);
	void (*ctrl_nbtg_bt_tx)(struct rtw89_dev *rtwdev, bool en,
				enum rtw89_phy_idx phy_idx);
	void (*cfg_txrx_path)(struct rtw89_dev *rtwdev);
	void (*set_txpwr_ul_tb_offset)(struct rtw89_dev *rtwdev,
				       s8 pw_ofst, enum rtw89_mac_idx mac_idx);
	void (*digital_pwr_comp)(struct rtw89_dev *rtwdev,
				 enum rtw89_phy_idx phy_idx);
	int (*pwr_on_func)(struct rtw89_dev *rtwdev);
	int (*pwr_off_func)(struct rtw89_dev *rtwdev);
	void (*query_rxdesc)(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset);
	void (*fill_txdesc)(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc);
	void (*fill_txdesc_fwcmd)(struct rtw89_dev *rtwdev,
				  struct rtw89_tx_desc_info *desc_info,
				  void *txdesc);
	int (*cfg_ctrl_path)(struct rtw89_dev *rtwdev, bool wl);
	int (*mac_cfg_gnt)(struct rtw89_dev *rtwdev,
			   const struct rtw89_mac_ax_coex_gnt *gnt_cfg);
	int (*stop_sch_tx)(struct rtw89_dev *rtwdev, u8 mac_idx,
			   u32 *tx_en, enum rtw89_sch_tx_sel sel);
	int (*resume_sch_tx)(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en);
	int (*h2c_dctl_sec_cam)(struct rtw89_dev *rtwdev,
				struct rtw89_vif_link *rtwvif_link,
				struct rtw89_sta_link *rtwsta_link);
	int (*h2c_default_cmac_tbl)(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct rtw89_sta_link *rtwsta_link);
	int (*h2c_assoc_cmac_tbl)(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta_link *rtwsta_link);
	int (*h2c_ampdu_cmac_tbl)(struct rtw89_dev *rtwdev,
				  struct rtw89_vif_link *rtwvif_link,
				  struct rtw89_sta_link *rtwsta_link);
	int (*h2c_txtime_cmac_tbl)(struct rtw89_dev *rtwdev,
				   struct rtw89_sta_link *rtwsta_link);
	int (*h2c_default_dmac_tbl)(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link,
				    struct rtw89_sta_link *rtwsta_link);
	int (*h2c_update_beacon)(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link);
	int (*h2c_ba_cam)(struct rtw89_dev *rtwdev,
			  struct rtw89_vif_link *rtwvif_link,
			  struct rtw89_sta_link *rtwsta_link,
			  bool valid, struct ieee80211_ampdu_params *params);

	void (*btc_set_rfe)(struct rtw89_dev *rtwdev);
	void (*btc_init_cfg)(struct rtw89_dev *rtwdev);
	void (*btc_set_wl_pri)(struct rtw89_dev *rtwdev, u8 map, bool state);
	void (*btc_set_wl_txpwr_ctrl)(struct rtw89_dev *rtwdev, u32 txpwr_val);
	s8 (*btc_get_bt_rssi)(struct rtw89_dev *rtwdev, s8 val);
	void (*btc_update_bt_cnt)(struct rtw89_dev *rtwdev);
	void (*btc_wl_s1_standby)(struct rtw89_dev *rtwdev, bool state);
	void (*btc_set_policy)(struct rtw89_dev *rtwdev, u16 policy_type);
	void (*btc_set_wl_rx_gain)(struct rtw89_dev *rtwdev, u32 level);
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

#define MLO_MODE_FOR_BB0_BB1_RF(bb0, bb1, rf) ((rf) << 12 | (bb1) << 4 | (bb0))

enum rtw89_mlo_dbcc_mode {
	MLO_DBCC_NOT_SUPPORT = 1,
	MLO_0_PLUS_2_1RF = MLO_MODE_FOR_BB0_BB1_RF(0, 2, 1),
	MLO_0_PLUS_2_2RF = MLO_MODE_FOR_BB0_BB1_RF(0, 2, 2),
	MLO_1_PLUS_1_1RF = MLO_MODE_FOR_BB0_BB1_RF(1, 1, 1),
	MLO_1_PLUS_1_2RF = MLO_MODE_FOR_BB0_BB1_RF(1, 1, 2),
	MLO_2_PLUS_0_1RF = MLO_MODE_FOR_BB0_BB1_RF(2, 0, 1),
	MLO_2_PLUS_0_2RF = MLO_MODE_FOR_BB0_BB1_RF(2, 0, 2),
	MLO_2_PLUS_2_2RF = MLO_MODE_FOR_BB0_BB1_RF(2, 2, 2),
	DBCC_LEGACY = 0xffffffff,
};

enum rtw89_scan_be_operation {
	RTW89_SCAN_OP_STOP,
	RTW89_SCAN_OP_START,
	RTW89_SCAN_OP_SETPARM,
	RTW89_SCAN_OP_GETRPT,
	RTW89_SCAN_OP_NUM
};

enum rtw89_scan_be_mode {
	RTW89_SCAN_MODE_SA,
	RTW89_SCAN_MODE_MACC,
	RTW89_SCAN_MODE_NUM
};

enum rtw89_scan_be_opmode {
	RTW89_SCAN_OPMODE_NONE,
	RTW89_SCAN_OPMODE_TBTT,
	RTW89_SCAN_OPMODE_INTV,
	RTW89_SCAN_OPMODE_CNT,
	RTW89_SCAN_OPMODE_NUM,
};

struct rtw89_scan_option {
	bool enable;
	bool target_ch_mode;
	u8 num_macc_role;
	u8 num_opch;
	u8 repeat;
	u16 norm_pd;
	u16 slow_pd;
	u16 norm_cy;
	u8 opch_end;
	u16 delay;
	u64 prohib_chan;
	enum rtw89_phy_idx band;
	enum rtw89_scan_be_operation operation;
	enum rtw89_scan_be_mode scan_mode;
	enum rtw89_mlo_dbcc_mode mlo_mode;
};

enum rtw89_qta_mode {
	RTW89_QTA_SCC,
	RTW89_QTA_DBCC,
	RTW89_QTA_DLFW,
	RTW89_QTA_WOW,

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
	/* for WiFi 7 chips below */
	u32 srt_ofst;
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
	u16 tx_rpt;
	/* for WiFi 7 chips below */
	u16 h2d;
};

struct rtw89_rsvd_quota {
	u16 mpdu_info_tbl;
	u16 b0_csi;
	u16 b1_csi;
	u16 b0_lmr;
	u16 b1_lmr;
	u16 b0_ftm;
	u16 b1_ftm;
	u16 b0_smr;
	u16 b1_smr;
	u16 others;
};

struct rtw89_dle_rsvd_size {
	u32 srt_ofst;
	u32 size;
};

struct rtw89_dle_mem {
	enum rtw89_qta_mode mode;
	const struct rtw89_dle_size *wde_size;
	const struct rtw89_dle_size *ple_size;
	const struct rtw89_wde_quota *wde_min_qt;
	const struct rtw89_wde_quota *wde_max_qt;
	const struct rtw89_ple_quota *ple_min_qt;
	const struct rtw89_ple_quota *ple_max_qt;
	/* for WiFi 7 chips below */
	const struct rtw89_rsvd_quota *rsvd_qt;
	const struct rtw89_dle_rsvd_size *rsvd0_size;
	const struct rtw89_dle_rsvd_size *rsvd1_size;
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

struct rtw89_reg_imr {
	u32 addr;
	u32 clr;
	u32 set;
};

struct rtw89_phy_table {
	const struct rtw89_reg2_def *regs;
	u32 n_regs;
	enum rtw89_rf_path rf_path;
	void (*config)(struct rtw89_dev *rtwdev, const struct rtw89_reg2_def *reg,
		       enum rtw89_rf_path rf_path, void *data);
};

struct rtw89_txpwr_table {
	const void *data;
	u32 size;
	void (*load)(struct rtw89_dev *rtwdev,
		     const struct rtw89_txpwr_table *tbl);
};

struct rtw89_txpwr_rule_2ghz {
	const s8 (*lmt)[RTW89_2G_BW_NUM][RTW89_NTX_NUM]
		       [RTW89_RS_LMT_NUM][RTW89_BF_NUM]
		       [RTW89_REGD_NUM][RTW89_2G_CH_NUM];
	const s8 (*lmt_ru)[RTW89_RU_NUM][RTW89_NTX_NUM]
			  [RTW89_REGD_NUM][RTW89_2G_CH_NUM];
};

struct rtw89_txpwr_rule_5ghz {
	const s8 (*lmt)[RTW89_5G_BW_NUM][RTW89_NTX_NUM]
		       [RTW89_RS_LMT_NUM][RTW89_BF_NUM]
		       [RTW89_REGD_NUM][RTW89_5G_CH_NUM];
	const s8 (*lmt_ru)[RTW89_RU_NUM][RTW89_NTX_NUM]
			  [RTW89_REGD_NUM][RTW89_5G_CH_NUM];
};

struct rtw89_txpwr_rule_6ghz {
	const s8 (*lmt)[RTW89_6G_BW_NUM][RTW89_NTX_NUM]
		       [RTW89_RS_LMT_NUM][RTW89_BF_NUM]
		       [RTW89_REGD_NUM][NUM_OF_RTW89_REG_6GHZ_POWER]
		       [RTW89_6G_CH_NUM];
	const s8 (*lmt_ru)[RTW89_RU_NUM][RTW89_NTX_NUM]
			  [RTW89_REGD_NUM][NUM_OF_RTW89_REG_6GHZ_POWER]
			  [RTW89_6G_CH_NUM];
};

struct rtw89_tx_shape {
	const u8 (*lmt)[RTW89_BAND_NUM][RTW89_RS_TX_SHAPE_NUM][RTW89_REGD_NUM];
	const u8 (*lmt_ru)[RTW89_BAND_NUM][RTW89_REGD_NUM];
};

struct rtw89_rfe_parms {
	const struct rtw89_txpwr_table *byr_tbl;
	struct rtw89_txpwr_rule_2ghz rule_2ghz;
	struct rtw89_txpwr_rule_5ghz rule_5ghz;
	struct rtw89_txpwr_rule_6ghz rule_6ghz;
	struct rtw89_txpwr_rule_2ghz rule_da_2ghz;
	struct rtw89_txpwr_rule_5ghz rule_da_5ghz;
	struct rtw89_txpwr_rule_6ghz rule_da_6ghz;
	struct rtw89_tx_shape tx_shape;
	bool has_da;
};

struct rtw89_rfe_parms_conf {
	const struct rtw89_rfe_parms *rfe_parms;
	u8 rfe_type;
};

#define RTW89_TXPWR_CONF_DFLT_RFE_TYPE 0x0

struct rtw89_txpwr_conf {
	u8 rfe_type;
	u8 ent_sz;
	u32 num_ents;
	const void *data;
};

static inline bool rtw89_txpwr_entcpy(void *entry, const void *cursor, u8 size,
				      const struct rtw89_txpwr_conf *conf)
{
	u8 valid_size = min(size, conf->ent_sz);

	memcpy(entry, cursor, valid_size);
	return true;
}

#define rtw89_txpwr_conf_valid(conf) (!!(conf)->data)

#define rtw89_for_each_in_txpwr_conf(entry, cursor, conf) \
	for (typecheck(const void *, cursor), (cursor) = (conf)->data; \
	     (cursor) < (conf)->data + (conf)->num_ents * (conf)->ent_sz; \
	     (cursor) += (conf)->ent_sz) \
		if (rtw89_txpwr_entcpy(&(entry), cursor, sizeof(entry), conf))

struct rtw89_txpwr_byrate_data {
	struct rtw89_txpwr_conf conf;
	struct rtw89_txpwr_table tbl;
};

struct rtw89_txpwr_lmt_2ghz_data {
	struct rtw89_txpwr_conf conf;
	s8 v[RTW89_2G_BW_NUM][RTW89_NTX_NUM]
	    [RTW89_RS_LMT_NUM][RTW89_BF_NUM]
	    [RTW89_REGD_NUM][RTW89_2G_CH_NUM];
};

struct rtw89_txpwr_lmt_5ghz_data {
	struct rtw89_txpwr_conf conf;
	s8 v[RTW89_5G_BW_NUM][RTW89_NTX_NUM]
	    [RTW89_RS_LMT_NUM][RTW89_BF_NUM]
	    [RTW89_REGD_NUM][RTW89_5G_CH_NUM];
};

struct rtw89_txpwr_lmt_6ghz_data {
	struct rtw89_txpwr_conf conf;
	s8 v[RTW89_6G_BW_NUM][RTW89_NTX_NUM]
	    [RTW89_RS_LMT_NUM][RTW89_BF_NUM]
	    [RTW89_REGD_NUM][NUM_OF_RTW89_REG_6GHZ_POWER]
	    [RTW89_6G_CH_NUM];
};

struct rtw89_txpwr_lmt_ru_2ghz_data {
	struct rtw89_txpwr_conf conf;
	s8 v[RTW89_RU_NUM][RTW89_NTX_NUM]
	    [RTW89_REGD_NUM][RTW89_2G_CH_NUM];
};

struct rtw89_txpwr_lmt_ru_5ghz_data {
	struct rtw89_txpwr_conf conf;
	s8 v[RTW89_RU_NUM][RTW89_NTX_NUM]
	    [RTW89_REGD_NUM][RTW89_5G_CH_NUM];
};

struct rtw89_txpwr_lmt_ru_6ghz_data {
	struct rtw89_txpwr_conf conf;
	s8 v[RTW89_RU_NUM][RTW89_NTX_NUM]
	    [RTW89_REGD_NUM][NUM_OF_RTW89_REG_6GHZ_POWER]
	    [RTW89_6G_CH_NUM];
};

struct rtw89_tx_shape_lmt_data {
	struct rtw89_txpwr_conf conf;
	u8 v[RTW89_BAND_NUM][RTW89_RS_TX_SHAPE_NUM][RTW89_REGD_NUM];
};

struct rtw89_tx_shape_lmt_ru_data {
	struct rtw89_txpwr_conf conf;
	u8 v[RTW89_BAND_NUM][RTW89_REGD_NUM];
};

struct rtw89_rfe_data {
	struct rtw89_txpwr_byrate_data byrate;
	struct rtw89_txpwr_lmt_2ghz_data lmt_2ghz;
	struct rtw89_txpwr_lmt_5ghz_data lmt_5ghz;
	struct rtw89_txpwr_lmt_6ghz_data lmt_6ghz;
	struct rtw89_txpwr_lmt_2ghz_data da_lmt_2ghz;
	struct rtw89_txpwr_lmt_5ghz_data da_lmt_5ghz;
	struct rtw89_txpwr_lmt_6ghz_data da_lmt_6ghz;
	struct rtw89_txpwr_lmt_ru_2ghz_data lmt_ru_2ghz;
	struct rtw89_txpwr_lmt_ru_5ghz_data lmt_ru_5ghz;
	struct rtw89_txpwr_lmt_ru_6ghz_data lmt_ru_6ghz;
	struct rtw89_txpwr_lmt_ru_2ghz_data da_lmt_ru_2ghz;
	struct rtw89_txpwr_lmt_ru_5ghz_data da_lmt_ru_5ghz;
	struct rtw89_txpwr_lmt_ru_6ghz_data da_lmt_ru_6ghz;
	struct rtw89_tx_shape_lmt_data tx_shape_lmt;
	struct rtw89_tx_shape_lmt_ru_data tx_shape_lmt_ru;
	struct rtw89_rfe_parms rfe_parms;
};

struct rtw89_page_regs {
	u32 hci_fc_ctrl;
	u32 ch_page_ctrl;
	u32 ach_page_ctrl;
	u32 ach_page_info;
	u32 pub_page_info3;
	u32 pub_page_ctrl1;
	u32 pub_page_ctrl2;
	u32 pub_page_info1;
	u32 pub_page_info2;
	u32 wp_page_ctrl1;
	u32 wp_page_ctrl2;
	u32 wp_page_info1;
};

struct rtw89_imr_info {
	u32 wdrls_imr_set;
	u32 wsec_imr_reg;
	u32 wsec_imr_set;
	u32 mpdu_tx_imr_set;
	u32 mpdu_rx_imr_set;
	u32 sta_sch_imr_set;
	u32 txpktctl_imr_b0_reg;
	u32 txpktctl_imr_b0_clr;
	u32 txpktctl_imr_b0_set;
	u32 txpktctl_imr_b1_reg;
	u32 txpktctl_imr_b1_clr;
	u32 txpktctl_imr_b1_set;
	u32 wde_imr_clr;
	u32 wde_imr_set;
	u32 ple_imr_clr;
	u32 ple_imr_set;
	u32 host_disp_imr_clr;
	u32 host_disp_imr_set;
	u32 cpu_disp_imr_clr;
	u32 cpu_disp_imr_set;
	u32 other_disp_imr_clr;
	u32 other_disp_imr_set;
	u32 bbrpt_com_err_imr_reg;
	u32 bbrpt_chinfo_err_imr_reg;
	u32 bbrpt_err_imr_set;
	u32 bbrpt_dfs_err_imr_reg;
	u32 ptcl_imr_clr;
	u32 ptcl_imr_set;
	u32 cdma_imr_0_reg;
	u32 cdma_imr_0_clr;
	u32 cdma_imr_0_set;
	u32 cdma_imr_1_reg;
	u32 cdma_imr_1_clr;
	u32 cdma_imr_1_set;
	u32 phy_intf_imr_reg;
	u32 phy_intf_imr_clr;
	u32 phy_intf_imr_set;
	u32 rmac_imr_reg;
	u32 rmac_imr_clr;
	u32 rmac_imr_set;
	u32 tmac_imr_reg;
	u32 tmac_imr_clr;
	u32 tmac_imr_set;
};

struct rtw89_imr_table {
	const struct rtw89_reg_imr *regs;
	u32 n_regs;
};

struct rtw89_xtal_info {
	u32 xcap_reg;
	u32 sc_xo_mask;
	u32 sc_xi_mask;
};

struct rtw89_rrsr_cfgs {
	struct rtw89_reg3_def ref_rate;
	struct rtw89_reg3_def rsc;
};

struct rtw89_rfkill_regs {
	struct rtw89_reg3_def pinmux;
	struct rtw89_reg3_def mode;
};

struct rtw89_dig_regs {
	u32 seg0_pd_reg;
	u32 pd_lower_bound_mask;
	u32 pd_spatial_reuse_en;
	u32 bmode_pd_reg;
	u32 bmode_cca_rssi_limit_en;
	u32 bmode_pd_lower_bound_reg;
	u32 bmode_rssi_nocca_low_th_mask;
	struct rtw89_reg_def p0_lna_init;
	struct rtw89_reg_def p1_lna_init;
	struct rtw89_reg_def p0_tia_init;
	struct rtw89_reg_def p1_tia_init;
	struct rtw89_reg_def p0_rxb_init;
	struct rtw89_reg_def p1_rxb_init;
	struct rtw89_reg_def p0_p20_pagcugc_en;
	struct rtw89_reg_def p0_s20_pagcugc_en;
	struct rtw89_reg_def p1_p20_pagcugc_en;
	struct rtw89_reg_def p1_s20_pagcugc_en;
};

struct rtw89_edcca_regs {
	u32 edcca_level;
	u32 edcca_mask;
	u32 edcca_p_mask;
	u32 ppdu_level;
	u32 ppdu_mask;
	struct rtw89_edcca_p_regs {
		u32 rpt_a;
		u32 rpt_b;
		u32 rpt_sel;
		u32 rpt_sel_mask;
	} p[RTW89_PHY_NUM];
	u32 rpt_sel_be;
	u32 rpt_sel_be_mask;
	u32 tx_collision_t2r_st;
	u32 tx_collision_t2r_st_mask;
};

struct rtw89_phy_ul_tb_info {
	bool dyn_tb_tri_en;
	u8 def_if_bandedge;
};

struct rtw89_antdiv_stats {
	struct ewma_rssi cck_rssi_avg;
	struct ewma_rssi ofdm_rssi_avg;
	struct ewma_rssi non_legacy_rssi_avg;
	u16 pkt_cnt_cck;
	u16 pkt_cnt_ofdm;
	u16 pkt_cnt_non_legacy;
	u32 evm;
};

struct rtw89_antdiv_info {
	struct rtw89_antdiv_stats target_stats;
	struct rtw89_antdiv_stats main_stats;
	struct rtw89_antdiv_stats aux_stats;
	u8 training_count;
	u8 rssi_pre;
	bool get_stats;
};

enum rtw89_chanctx_state {
	RTW89_CHANCTX_STATE_MCC_START,
	RTW89_CHANCTX_STATE_MCC_STOP,
};

enum rtw89_chanctx_callbacks {
	RTW89_CHANCTX_CALLBACK_PLACEHOLDER,
	RTW89_CHANCTX_CALLBACK_RFK,
	RTW89_CHANCTX_CALLBACK_TAS,

	NUM_OF_RTW89_CHANCTX_CALLBACKS,
};

struct rtw89_chanctx_listener {
	void (*callbacks[NUM_OF_RTW89_CHANCTX_CALLBACKS])
		(struct rtw89_dev *rtwdev, enum rtw89_chanctx_state state);
};

struct rtw89_chip_info {
	enum rtw89_core_chip_id chip_id;
	enum rtw89_chip_gen chip_gen;
	const struct rtw89_chip_ops *ops;
	const struct rtw89_mac_gen_def *mac_def;
	const struct rtw89_phy_gen_def *phy_def;
	const char *fw_basename;
	u8 fw_format_max;
	bool try_ce_fw;
	u8 bbmcu_nr;
	u32 needed_fw_elms;
	const struct rtw89_fw_blacklist *fw_blacklist;
	u32 fifo_size;
	bool small_fifo_size;
	u32 dle_scc_rsvd_size;
	u16 max_amsdu_limit;
	bool dis_2g_40m_ul_ofdma;
	u32 rsvd_ple_ofst;
	const struct rtw89_hfc_param_ini *hfc_param_ini;
	const struct rtw89_dle_mem *dle_mem;
	u8 wde_qempty_acq_grpnum;
	u8 wde_qempty_mgq_grpsel;
	u32 rf_base_addr[2];
	u8 thermal_th[2];
	u8 support_macid_num;
	u8 support_link_num;
	u8 support_chanctx_num;
	u8 support_bands;
	u16 support_bandwidths;
	bool support_unii4;
	bool support_rnr;
	bool support_ant_gain;
	bool support_tas;
	bool support_sar_by_ant;
	bool ul_tb_waveform_ctrl;
	bool ul_tb_pwr_diff;
	bool rx_freq_frome_ie;
	bool hw_sec_hdr;
	bool hw_mgmt_tx_encrypt;
	bool hw_tkip_crypto;
	bool hw_mlo_bmc_crypto;
	u8 rf_path_num;
	u8 tx_nss;
	u8 rx_nss;
	u8 acam_num;
	u8 bcam_num;
	u8 scam_num;
	u8 bacam_num;
	u8 bacam_dynamic_num;
	enum rtw89_bacam_ver bacam_ver;
	u8 ppdu_max_usr;

	u8 sec_ctrl_efuse_size;
	u32 physical_efuse_size;
	u32 logical_efuse_size;
	u32 limit_efuse_size;
	u32 dav_phy_efuse_size;
	u32 dav_log_efuse_size;
	u32 phycap_addr;
	u32 phycap_size;
	const struct rtw89_efuse_block_cfg *efuse_blocks;

	const struct rtw89_pwr_cfg * const *pwr_on_seq;
	const struct rtw89_pwr_cfg * const *pwr_off_seq;
	const struct rtw89_phy_table *bb_table;
	const struct rtw89_phy_table *bb_gain_table;
	const struct rtw89_phy_table *rf_table[RF_PATH_MAX];
	const struct rtw89_phy_table *nctl_table;
	const struct rtw89_rfk_tbl *nctl_post_table;
	const struct rtw89_phy_dig_gain_table *dig_table;
	const struct rtw89_dig_regs *dig_regs;
	const struct rtw89_phy_tssi_dbw_table *tssi_dbw_table;

	/* NULL if no rfe-specific, or a null-terminated array by rfe_parms */
	const struct rtw89_rfe_parms_conf *rfe_parms_conf;
	const struct rtw89_rfe_parms *dflt_parms;
	const struct rtw89_chanctx_listener *chanctx_listener;

	u8 txpwr_factor_bb;
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
	u8 low_power_hci_modes;

	u32 h2c_cctl_func_id;
	u32 hci_func_en_addr;
	u32 h2c_desc_size;
	u32 txwd_body_size;
	u32 txwd_info_size;
	u32 h2c_ctrl_reg;
	const u32 *h2c_regs;
	struct rtw89_reg_def h2c_counter_reg;
	u32 c2h_ctrl_reg;
	const u32 *c2h_regs;
	struct rtw89_reg_def c2h_counter_reg;
	const struct rtw89_page_regs *page_regs;
	const u32 *wow_reason_reg;
	bool cfo_src_fd;
	bool cfo_hw_comp;
	const struct rtw89_reg_def *dcfo_comp;
	u8 dcfo_comp_sft;
	const struct rtw89_imr_info *imr_info;
	const struct rtw89_imr_table *imr_dmac_table;
	const struct rtw89_imr_table *imr_cmac_table;
	const struct rtw89_rrsr_cfgs *rrsr_cfgs;
	struct rtw89_reg_def bss_clr_vld;
	u32 bss_clr_map_reg;
	const struct rtw89_rfkill_regs *rfkill_init;
	struct rtw89_reg_def rfkill_get;
	u32 dma_ch_mask;
	const struct rtw89_edcca_regs *edcca_regs;
	const struct wiphy_wowlan_support *wowlan_stub;
	const struct rtw89_xtal_info *xtal_info;
};

struct rtw89_chip_variant {
	bool no_mcs_12_13: 1;
	u32 fw_min_ver_code;
};

union rtw89_bus_info {
	const struct rtw89_pci_info *pci;
};

struct rtw89_driver_info {
	const struct rtw89_chip_info *chip;
	const struct rtw89_chip_variant *variant;
	const struct dmi_system_id *quirks;
	union rtw89_bus_info bus;
};

enum rtw89_hcifc_mode {
	RTW89_HCIFC_POH = 0,
	RTW89_HCIFC_STF = 1,
	RTW89_HCIFC_SDIO = 2,

	/* keep last */
	RTW89_HCIFC_MODE_INVALID,
};

struct rtw89_dle_info {
	const struct rtw89_rsvd_quota *rsvd_qt;
	enum rtw89_qta_mode qta_mode;
	u16 ple_pg_size;
	u16 ple_free_pg;
	u16 c0_rx_qta;
	u16 c1_rx_qta;
};

enum rtw89_host_rpr_mode {
	RTW89_RPR_MODE_POH = 0,
	RTW89_RPR_MODE_STF
};

#define RTW89_COMPLETION_BUF_SIZE 40
#define RTW89_WAIT_COND_IDLE UINT_MAX

struct rtw89_completion_data {
	bool err;
	u8 buf[RTW89_COMPLETION_BUF_SIZE];
};

struct rtw89_wait_info {
	atomic_t cond;
	struct completion completion;
	struct rtw89_completion_data data;
};

#define RTW89_WAIT_FOR_COND_TIMEOUT msecs_to_jiffies(100)

static inline void rtw89_init_wait(struct rtw89_wait_info *wait)
{
	init_completion(&wait->completion);
	atomic_set(&wait->cond, RTW89_WAIT_COND_IDLE);
}

struct rtw89_mac_info {
	struct rtw89_dle_info dle_info;
	struct rtw89_hfc_param hfc_param;
	enum rtw89_qta_mode qta_mode;
	u8 rpwm_seq_num;
	u8 cpwm_seq_num;

	/* see RTW89_FW_OFLD_WAIT_COND series for wait condition */
	struct rtw89_wait_info fw_ofld_wait;
	/* see RTW89_PS_WAIT_COND series for wait condition */
	struct rtw89_wait_info ps_wait;
};

enum rtw89_fwdl_check_type {
	RTW89_FWDL_CHECK_FREERTOS_DONE,
	RTW89_FWDL_CHECK_WCPU_FWDL_DONE,
	RTW89_FWDL_CHECK_DCPU_FWDL_DONE,
	RTW89_FWDL_CHECK_BB0_FWDL_DONE,
	RTW89_FWDL_CHECK_BB1_FWDL_DONE,
};

enum rtw89_fw_type {
	RTW89_FW_NORMAL = 1,
	RTW89_FW_WOWLAN = 3,
	RTW89_FW_NORMAL_CE = 5,
	RTW89_FW_BBMCU0 = 64,
	RTW89_FW_BBMCU1 = 65,
	RTW89_FW_LOGFMT = 255,
};

enum rtw89_fw_feature {
	RTW89_FW_FEATURE_OLD_HT_RA_FORMAT,
	RTW89_FW_FEATURE_SCAN_OFFLOAD,
	RTW89_FW_FEATURE_TX_WAKE,
	RTW89_FW_FEATURE_CRASH_TRIGGER,
	RTW89_FW_FEATURE_NO_PACKET_DROP,
	RTW89_FW_FEATURE_NO_DEEP_PS,
	RTW89_FW_FEATURE_NO_LPS_PG,
	RTW89_FW_FEATURE_BEACON_FILTER,
	RTW89_FW_FEATURE_MACID_PAUSE_SLEEP,
	RTW89_FW_FEATURE_SCAN_OFFLOAD_BE_V0,
	RTW89_FW_FEATURE_WOW_REASON_V1,
	RTW89_FW_FEATURE_RFK_PRE_NOTIFY_V0,
	RTW89_FW_FEATURE_RFK_PRE_NOTIFY_V1,
	RTW89_FW_FEATURE_RFK_RXDCK_V0,
	RTW89_FW_FEATURE_RFK_IQK_V0,
	RTW89_FW_FEATURE_NO_WOW_CPU_IO_RX,
	RTW89_FW_FEATURE_NOTIFY_AP_INFO,
	RTW89_FW_FEATURE_CH_INFO_BE_V0,
	RTW89_FW_FEATURE_LPS_CH_INFO,
	RTW89_FW_FEATURE_NO_PHYCAP_P1,
	RTW89_FW_FEATURE_NO_POWER_DIFFERENCE,
	RTW89_FW_FEATURE_BEACON_LOSS_COUNT_V1,
	RTW89_FW_FEATURE_SCAN_OFFLOAD_EXTRA_OP,
	RTW89_FW_FEATURE_RFK_NTFY_MCC_V0,
};

struct rtw89_fw_suit {
	enum rtw89_fw_type type;
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
	u8 hdr_ver;
	u32 commitid;
};

#define RTW89_FW_VER_CODE(major, minor, sub, idx)	\
	(((major) << 24) | ((minor) << 16) | ((sub) << 8) | (idx))
#define RTW89_FW_SUIT_VER_CODE(s)	\
	RTW89_FW_VER_CODE((s)->major_ver, (s)->minor_ver, (s)->sub_ver, (s)->sub_idex)

#define RTW89_MFW_HDR_VER_CODE(mfw_hdr)		\
	RTW89_FW_VER_CODE((mfw_hdr)->ver.major,	\
			  (mfw_hdr)->ver.minor,	\
			  (mfw_hdr)->ver.sub,	\
			  (mfw_hdr)->ver.idx)

#define RTW89_FW_HDR_VER_CODE(fw_hdr)				\
	RTW89_FW_VER_CODE(le32_get_bits((fw_hdr)->w1, FW_HDR_W1_MAJOR_VERSION),	\
			  le32_get_bits((fw_hdr)->w1, FW_HDR_W1_MINOR_VERSION),	\
			  le32_get_bits((fw_hdr)->w1, FW_HDR_W1_SUBVERSION),	\
			  le32_get_bits((fw_hdr)->w1, FW_HDR_W1_SUBINDEX))

struct rtw89_fw_req_info {
	const struct firmware *firmware;
	struct completion completion;
};

struct rtw89_fw_log {
	struct rtw89_fw_suit suit;
	bool enable;
	u32 last_fmt_id;
	u32 fmt_count;
	const __le32 *fmt_ids;
	const char *(*fmts)[];
};

struct rtw89_fw_elm_info {
	struct rtw89_phy_table *bb_tbl;
	struct rtw89_phy_table *bb_gain;
	struct rtw89_phy_table *rf_radio[RF_PATH_MAX];
	struct rtw89_phy_table *rf_nctl;
	struct rtw89_fw_txpwr_track_cfg *txpwr_trk;
	struct rtw89_phy_rfk_log_fmt *rfk_log_fmt;
	const struct rtw89_regd_data *regd;
};

enum rtw89_fw_mss_dev_type {
	RTW89_FW_MSS_DEV_TYPE_FWSEC_DEF = 0xF,
	RTW89_FW_MSS_DEV_TYPE_FWSEC_INV = 0xFF,
};

struct rtw89_fw_secure {
	bool secure_boot: 1;
	bool can_mss_v1: 1;
	bool can_mss_v0: 1;
	u32 sb_sel_mgn;
	u8 mss_dev_type;
	u8 mss_cust_idx;
	u8 mss_key_num;
	u8 mss_idx; /* v0 */
};

struct rtw89_fw_info {
	struct rtw89_fw_req_info req;
	int fw_format;
	u8 h2c_seq;
	u8 rec_seq;
	u8 h2c_counter;
	u8 c2h_counter;
	struct rtw89_fw_suit normal;
	struct rtw89_fw_suit wowlan;
	struct rtw89_fw_suit bbmcu0;
	struct rtw89_fw_suit bbmcu1;
	struct rtw89_fw_log log;
	u32 feature_map;
	struct rtw89_fw_elm_info elm_info;
	struct rtw89_fw_secure sec;
};

#define RTW89_CHK_FW_FEATURE(_feat, _fw) \
	(!!((_fw)->feature_map & BIT(RTW89_FW_FEATURE_ ## _feat)))

#define RTW89_SET_FW_FEATURE(_fw_feature, _fw) \
	((_fw)->feature_map |= BIT(_fw_feature))

struct rtw89_cam_info {
	DECLARE_BITMAP(addr_cam_map, RTW89_MAX_ADDR_CAM_NUM);
	DECLARE_BITMAP(bssid_cam_map, RTW89_MAX_BSSID_CAM_NUM);
	DECLARE_BITMAP(sec_cam_map, RTW89_MAX_SEC_CAM_NUM);
	DECLARE_BITMAP(ba_cam_map, RTW89_MAX_BA_CAM_NUM);
	struct rtw89_ba_cam_entry ba_cam_entry[RTW89_MAX_BA_CAM_NUM];
	const struct rtw89_sec_cam_entry *sec_entries[RTW89_MAX_SEC_CAM_NUM];
};

enum rtw89_sar_sources {
	RTW89_SAR_SOURCE_NONE,
	RTW89_SAR_SOURCE_COMMON,
	RTW89_SAR_SOURCE_ACPI,

	RTW89_SAR_SOURCE_NR,
};

enum rtw89_sar_subband {
	RTW89_SAR_2GHZ_SUBBAND,
	RTW89_SAR_5GHZ_SUBBAND_1_2, /* U-NII-1 and U-NII-2 */
	RTW89_SAR_5GHZ_SUBBAND_2_E, /* U-NII-2-Extended */
	RTW89_SAR_5GHZ_SUBBAND_3_4, /* U-NII-3 and U-NII-4 */
	RTW89_SAR_6GHZ_SUBBAND_5_L, /* U-NII-5 lower part */
	RTW89_SAR_6GHZ_SUBBAND_5_H, /* U-NII-5 higher part */
	RTW89_SAR_6GHZ_SUBBAND_6,   /* U-NII-6 */
	RTW89_SAR_6GHZ_SUBBAND_7_L, /* U-NII-7 lower part */
	RTW89_SAR_6GHZ_SUBBAND_7_H, /* U-NII-7 higher part */
	RTW89_SAR_6GHZ_SUBBAND_8,   /* U-NII-8 */

	RTW89_SAR_SUBBAND_NR,
};

struct rtw89_sar_cfg_common {
	bool set[RTW89_SAR_SUBBAND_NR];
	s32 cfg[RTW89_SAR_SUBBAND_NR];
};

enum rtw89_acpi_sar_subband {
	RTW89_ACPI_SAR_2GHZ_SUBBAND,
	RTW89_ACPI_SAR_5GHZ_SUBBAND_1,   /* U-NII-1 */
	RTW89_ACPI_SAR_5GHZ_SUBBAND_2,   /* U-NII-2 */
	RTW89_ACPI_SAR_5GHZ_SUBBAND_2E,  /* U-NII-2-Extended */
	RTW89_ACPI_SAR_5GHZ_SUBBAND_3_4, /* U-NII-3 and U-NII-4 */
	RTW89_ACPI_SAR_6GHZ_SUBBAND_5_L, /* U-NII-5 lower part */
	RTW89_ACPI_SAR_6GHZ_SUBBAND_5_H, /* U-NII-5 higher part */
	RTW89_ACPI_SAR_6GHZ_SUBBAND_6,   /* U-NII-6 */
	RTW89_ACPI_SAR_6GHZ_SUBBAND_7_L, /* U-NII-7 lower part */
	RTW89_ACPI_SAR_6GHZ_SUBBAND_7_H, /* U-NII-7 higher part */
	RTW89_ACPI_SAR_6GHZ_SUBBAND_8,   /* U-NII-8 */

	NUM_OF_RTW89_ACPI_SAR_SUBBAND,
	RTW89_ACPI_SAR_SUBBAND_NR_LEGACY = RTW89_ACPI_SAR_5GHZ_SUBBAND_3_4 + 1,
	RTW89_ACPI_SAR_SUBBAND_NR_HAS_6GHZ = RTW89_ACPI_SAR_6GHZ_SUBBAND_8 + 1,
};

#define TXPWR_FACTOR_OF_RTW89_ACPI_SAR 3 /* unit: 0.125 dBm */
#define MAX_VAL_OF_RTW89_ACPI_SAR S16_MAX
#define MIN_VAL_OF_RTW89_ACPI_SAR S16_MIN
#define MAX_NUM_OF_RTW89_ACPI_SAR_TBL 6
#define NUM_OF_RTW89_ACPI_SAR_RF_PATH (RF_PATH_B + 1)

struct rtw89_sar_entry_from_acpi {
	s16 v[NUM_OF_RTW89_ACPI_SAR_SUBBAND][NUM_OF_RTW89_ACPI_SAR_RF_PATH];
};

struct rtw89_sar_table_from_acpi {
	/* If this table is active, must fill all fields according to either
	 * configuration in BIOS or some default values for SAR to work well.
	 */
	struct rtw89_sar_entry_from_acpi entries[RTW89_REGD_NUM];
};

struct rtw89_sar_indicator_from_acpi {
	bool enable_sync;
	unsigned int fields;
	u8 (*rfpath_to_antidx)(enum rtw89_rf_path rfpath);

	/* Select among @tables of container, rtw89_sar_cfg_acpi, by path.
	 * Not design with pointers since addresses will be invalid after
	 * sync content with local container instance.
	 */
	u8 tblsel[NUM_OF_RTW89_ACPI_SAR_RF_PATH];
};

struct rtw89_sar_cfg_acpi {
	u8 downgrade_2tx;
	unsigned int valid_num;
	struct rtw89_sar_table_from_acpi tables[MAX_NUM_OF_RTW89_ACPI_SAR_TBL];
	struct rtw89_sar_indicator_from_acpi indicator;
};

struct rtw89_sar_info {
	/* used to decide how to access SAR cfg union */
	enum rtw89_sar_sources src;

	/* reserved for different knids of SAR cfg struct.
	 * supposed that a single cfg struct cannot handle various SAR sources.
	 */
	union {
		struct rtw89_sar_cfg_common cfg_common;
		struct rtw89_sar_cfg_acpi cfg_acpi;
	};
};

enum rtw89_ant_gain_subband {
	RTW89_ANT_GAIN_2GHZ_SUBBAND,
	RTW89_ANT_GAIN_5GHZ_SUBBAND_1,   /* U-NII-1 */
	RTW89_ANT_GAIN_5GHZ_SUBBAND_2,   /* U-NII-2 */
	RTW89_ANT_GAIN_5GHZ_SUBBAND_2E,  /* U-NII-2-Extended */
	RTW89_ANT_GAIN_5GHZ_SUBBAND_3_4, /* U-NII-3 and U-NII-4 */
	RTW89_ANT_GAIN_6GHZ_SUBBAND_5_L, /* U-NII-5 lower part */
	RTW89_ANT_GAIN_6GHZ_SUBBAND_5_H, /* U-NII-5 higher part */
	RTW89_ANT_GAIN_6GHZ_SUBBAND_6,   /* U-NII-6 */
	RTW89_ANT_GAIN_6GHZ_SUBBAND_7_L, /* U-NII-7 lower part */
	RTW89_ANT_GAIN_6GHZ_SUBBAND_7_H, /* U-NII-7 higher part */
	RTW89_ANT_GAIN_6GHZ_SUBBAND_8,   /* U-NII-8 */

	RTW89_ANT_GAIN_SUBBAND_NR,
};

enum rtw89_ant_gain_domain_type {
	RTW89_ANT_GAIN_ETSI = 0,

	RTW89_ANT_GAIN_DOMAIN_NUM,
};

#define RTW89_ANT_GAIN_CHAIN_NUM 2
struct rtw89_ant_gain_info {
	s8 offset[RTW89_ANT_GAIN_CHAIN_NUM][RTW89_ANT_GAIN_SUBBAND_NR];
	u32 regd_enabled;
	bool block_country;
};

struct rtw89_6ghz_span {
	enum rtw89_sar_subband sar_subband_low;
	enum rtw89_sar_subband sar_subband_high;
	enum rtw89_acpi_sar_subband acpi_sar_subband_low;
	enum rtw89_acpi_sar_subband acpi_sar_subband_high;
	enum rtw89_ant_gain_subband ant_gain_subband_low;
	enum rtw89_ant_gain_subband ant_gain_subband_high;
};

#define RTW89_SAR_SPAN_VALID(span) ((span)->sar_subband_high)
#define RTW89_ACPI_SAR_SPAN_VALID(span) ((span)->acpi_sar_subband_high)
#define RTW89_ANT_GAIN_SPAN_VALID(span) ((span)->ant_gain_subband_high)

enum rtw89_tas_state {
	RTW89_TAS_STATE_DPR_OFF,
	RTW89_TAS_STATE_DPR_ON,
	RTW89_TAS_STATE_STATIC_SAR,
};

#define RTW89_TAS_TX_RATIO_WINDOW 6
#define RTW89_TAS_TXPWR_WINDOW 180
struct rtw89_tas_info {
	u16 tx_ratio_history[RTW89_TAS_TX_RATIO_WINDOW];
	u64 txpwr_history[RTW89_TAS_TXPWR_WINDOW];
	u8 enabled_countries;
	u8 txpwr_head_idx;
	u8 txpwr_tail_idx;
	u8 tx_ratio_idx;
	u16 total_tx_ratio;
	u64 total_txpwr;
	u64 instant_txpwr;
	u32 window_size;
	s8 dpr_on_threshold;
	s8 dpr_off_threshold;
	enum rtw89_tas_state backup_state;
	enum rtw89_tas_state state;
	bool keep_history;
	bool block_regd;
	bool enable;
	bool pause;
};

struct rtw89_chanctx_cfg {
	enum rtw89_chanctx_idx idx;
	int ref_count;
};

enum rtw89_chanctx_changes {
	RTW89_CHANCTX_REMOTE_STA_CHANGE,
	RTW89_CHANCTX_BCN_OFFSET_CHANGE,
	RTW89_CHANCTX_P2P_PS_CHANGE,
	RTW89_CHANCTX_BT_SLOT_CHANGE,
	RTW89_CHANCTX_TSF32_TOGGLE_CHANGE,

	NUM_OF_RTW89_CHANCTX_CHANGES,
	RTW89_CHANCTX_CHANGE_DFLT = NUM_OF_RTW89_CHANCTX_CHANGES,
};

enum rtw89_entity_mode {
	RTW89_ENTITY_MODE_SCC_OR_SMLD,
	RTW89_ENTITY_MODE_MCC_PREPARE,
	RTW89_ENTITY_MODE_MCC,

	NUM_OF_RTW89_ENTITY_MODE,
	RTW89_ENTITY_MODE_INVALID = -EINVAL,
	RTW89_ENTITY_MODE_UNHANDLED = -ESRCH,
};

#define RTW89_MAX_INTERFACE_NUM 2

/* only valid when running with chanctx_ops */
struct rtw89_entity_mgnt {
	struct list_head active_list;
	struct rtw89_vif *active_roles[RTW89_MAX_INTERFACE_NUM];
	enum rtw89_chanctx_idx chanctx_tbl[RTW89_MAX_INTERFACE_NUM]
					  [__RTW89_MLD_MAX_LINK_NUM];
};

struct rtw89_chanctx {
	struct cfg80211_chan_def chandef;
	struct rtw89_chan chan;
	struct rtw89_chan_rcd rcd;

	/* only assigned when running with chanctx_ops */
	struct rtw89_chanctx_cfg *cfg;
};

struct rtw89_edcca_bak {
	u8 a;
	u8 p;
	u8 ppdu;
	u8 th_old;
};

enum rtw89_dm_type {
	RTW89_DM_DYNAMIC_EDCCA,
	RTW89_DM_THERMAL_PROTECT,
	RTW89_DM_TAS,
	RTW89_DM_MLO,
};

#define RTW89_THERMAL_PROT_LV_MAX 5
#define RTW89_THERMAL_PROT_STEP 5 /* -5% for each level */

struct rtw89_hal {
	u32 rx_fltr;
	u8 cv;
	u8 acv;
	u32 antenna_tx;
	u32 antenna_rx;
	u8 tx_nss;
	u8 rx_nss;
	bool tx_path_diversity;
	bool ant_diversity;
	bool ant_diversity_fixed;
	bool support_cckpd;
	bool support_igi;
	bool no_mcs_12_13;

	atomic_t roc_chanctx_idx;
	u8 roc_link_index;

	DECLARE_BITMAP(changes, NUM_OF_RTW89_CHANCTX_CHANGES);
	DECLARE_BITMAP(entity_map, NUM_OF_RTW89_CHANCTX);
	struct rtw89_chanctx chanctx[NUM_OF_RTW89_CHANCTX];
	struct cfg80211_chan_def roc_chandef;

	bool entity_active[RTW89_PHY_NUM];
	bool entity_pause;
	enum rtw89_entity_mode entity_mode;
	struct rtw89_entity_mgnt entity_mgnt;

	u32 disabled_dm_bitmap; /* bitmap of enum rtw89_dm_type */

	u8 thermal_prot_th;
	u8 thermal_prot_lv; /* 0 ~ RTW89_THERMAL_PROT_LV_MAX */
};

#define RTW89_MAX_MAC_ID_NUM 128
#define RTW89_MAX_PKT_OFLD_NUM 255

enum rtw89_flags {
	RTW89_FLAG_POWERON,
	RTW89_FLAG_DMAC_FUNC,
	RTW89_FLAG_CMAC0_FUNC,
	RTW89_FLAG_CMAC1_FUNC,
	RTW89_FLAG_FW_RDY,
	RTW89_FLAG_RUNNING,
	RTW89_FLAG_PROBE_DONE,
	RTW89_FLAG_BFEE_MON,
	RTW89_FLAG_BFEE_EN,
	RTW89_FLAG_BFEE_TIMER_KEEP,
	RTW89_FLAG_NAPI_RUNNING,
	RTW89_FLAG_LEISURE_PS,
	RTW89_FLAG_LOW_POWER_MODE,
	RTW89_FLAG_INACTIVE_PS,
	RTW89_FLAG_CRASH_SIMULATING,
	RTW89_FLAG_SER_HANDLING,
	RTW89_FLAG_WOWLAN,
	RTW89_FLAG_FORBIDDEN_TRACK_WORK,
	RTW89_FLAG_CHANGING_INTERFACE,
	RTW89_FLAG_HW_RFKILL_STATE,

	NUM_OF_RTW89_FLAGS,
};

enum rtw89_quirks {
	RTW89_QUIRK_PCI_BER,
	RTW89_QUIRK_THERMAL_PROT_120C,
	RTW89_QUIRK_THERMAL_PROT_110C,

	NUM_OF_RTW89_QUIRKS,
};

enum rtw89_custid {
	RTW89_CUSTID_NONE,
	RTW89_CUSTID_ACER,
	RTW89_CUSTID_AMD,
	RTW89_CUSTID_ASUS,
	RTW89_CUSTID_DELL,
	RTW89_CUSTID_HP,
	RTW89_CUSTID_LENOVO,
};

enum rtw89_pkt_drop_sel {
	RTW89_PKT_DROP_SEL_MACID_BE_ONCE,
	RTW89_PKT_DROP_SEL_MACID_BK_ONCE,
	RTW89_PKT_DROP_SEL_MACID_VI_ONCE,
	RTW89_PKT_DROP_SEL_MACID_VO_ONCE,
	RTW89_PKT_DROP_SEL_MACID_ALL,
	RTW89_PKT_DROP_SEL_MG0_ONCE,
	RTW89_PKT_DROP_SEL_HIQ_ONCE,
	RTW89_PKT_DROP_SEL_HIQ_PORT,
	RTW89_PKT_DROP_SEL_HIQ_MBSSID,
	RTW89_PKT_DROP_SEL_BAND,
	RTW89_PKT_DROP_SEL_BAND_ONCE,
	RTW89_PKT_DROP_SEL_REL_MACID,
	RTW89_PKT_DROP_SEL_REL_HIQ_PORT,
	RTW89_PKT_DROP_SEL_REL_HIQ_MBSSID,
};

struct rtw89_pkt_drop_params {
	enum rtw89_pkt_drop_sel sel;
	enum rtw89_mac_idx mac_band;
	u8 macid;
	u8 port;
	u8 mbssid;
	bool tf_trs;
	u32 macid_band_sel[4];
};

struct rtw89_pkt_stat {
	u16 beacon_nr;
	u8 beacon_rate;
	u32 rx_rate_cnt[RTW89_HW_RATE_NR];
};

DECLARE_EWMA(thermal, 4, 4);

struct rtw89_phy_stat {
	struct ewma_thermal avg_thermal[RF_PATH_MAX];
	u8 last_thermal_max;
	struct ewma_rssi bcn_rssi;
	struct rtw89_pkt_stat cur_pkt_stat;
	struct rtw89_pkt_stat last_pkt_stat;
};

enum rtw89_rfk_report_state {
	RTW89_RFK_STATE_START = 0x0,
	RTW89_RFK_STATE_OK = 0x1,
	RTW89_RFK_STATE_FAIL = 0x2,
	RTW89_RFK_STATE_TIMEOUT = 0x3,
	RTW89_RFK_STATE_H2C_CMD_ERR = 0x4,
};

struct rtw89_rfk_wait_info {
	struct completion completion;
	ktime_t start_time;
	enum rtw89_rfk_report_state state;
	u8 version;
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

enum rtw89_rfk_chs_nrs {
	__RTW89_RFK_CHS_NR_V0 = 2,
	__RTW89_RFK_CHS_NR_V1 = 3,

	RTW89_RFK_CHS_NR = __RTW89_RFK_CHS_NR_V1,
};

struct rtw89_rfk_mcc_info_data {
	u8 ch[RTW89_RFK_CHS_NR];
	u8 band[RTW89_RFK_CHS_NR];
	u8 bw[RTW89_RFK_CHS_NR];
	u8 table_idx;
};

struct rtw89_rfk_mcc_info {
	struct rtw89_rfk_mcc_info_data data[2];
};

#define RTW89_IQK_CHS_NR 2
#define RTW89_IQK_PATH_NR 4

struct rtw89_lck_info {
	u8 thermal[RF_PATH_MAX];
};

struct rtw89_rx_dck_info {
	u8 thermal[RF_PATH_MAX];
};

struct rtw89_iqk_info {
	bool lok_cor_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool lok_fin_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool lok_fail[RTW89_IQK_PATH_NR];
	bool iqk_tx_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	bool iqk_rx_fail[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u32 iqk_fail_cnt;
	bool is_iqk_init;
	u32 iqk_channel[RTW89_IQK_CHS_NR];
	u8 iqk_band[RTW89_IQK_PATH_NR];
	u8 iqk_ch[RTW89_IQK_PATH_NR];
	u8 iqk_bw[RTW89_IQK_PATH_NR];
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
	u32 syn1to2;
	u8 iqk_mcc_ch[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u8 iqk_table_idx[RTW89_IQK_PATH_NR];
	u32 lok_idac[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
	u32 lok_vbuf[RTW89_IQK_CHS_NR][RTW89_IQK_PATH_NR];
};

#define RTW89_DPK_RF_PATH 2
#define RTW89_DPK_AVG_THERMAL_NUM 8
#define RTW89_DPK_BKUP_NUM 2
struct rtw89_dpk_bkup_para {
	enum rtw89_band band;
	enum rtw89_bandwidth bw;
	u8 ch;
	bool path_ok;
	u8 mdpd_en;
	u8 txagc_dpk;
	u8 ther_dpk;
	u8 gs;
	u16 pwsf;
};

struct rtw89_dpk_info {
	bool is_dpk_enable;
	bool is_dpk_reload_en;
	u8 dpk_gs[RTW89_PHY_NUM];
	u16 dc_i[RTW89_DPK_RF_PATH][RTW89_DPK_BKUP_NUM];
	u16 dc_q[RTW89_DPK_RF_PATH][RTW89_DPK_BKUP_NUM];
	u8 corr_val[RTW89_DPK_RF_PATH][RTW89_DPK_BKUP_NUM];
	u8 corr_idx[RTW89_DPK_RF_PATH][RTW89_DPK_BKUP_NUM];
	u8 cur_idx[RTW89_DPK_RF_PATH];
	u8 cur_k_set;
	struct rtw89_dpk_bkup_para bp[RTW89_DPK_RF_PATH][RTW89_DPK_BKUP_NUM];
	u8 max_dpk_txagc[RTW89_DPK_RF_PATH];
	u32 dpk_order[RTW89_DPK_RF_PATH];
};

struct rtw89_fem_info {
	bool elna_2g;
	bool elna_5g;
	bool epa_2g;
	bool epa_5g;
	bool epa_6g;
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
#define TIA_LNA_OP1DB_NUM 8
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
	RTW89_PHY_DCFO_STATE_HOLD = 2,
	RTW89_PHY_DCFO_STATE_MAX
};

enum rtw89_phy_cfo_ul_ofdma_acc_mode {
	RTW89_CFO_UL_OFDMA_ACC_DISABLE = 0,
	RTW89_CFO_UL_OFDMA_ACC_ENABLE = 1
};

struct rtw89_cfo_tracking_info {
	u16 cfo_timer_ms;
	bool cfo_trig_by_timer_en;
	enum rtw89_phy_cfo_status phy_cfo_status;
	enum rtw89_phy_cfo_ul_ofdma_acc_mode cfo_ul_ofdma_acc_mode;
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
	s32 dcfo_avg;
	s32 dcfo_avg_pre;
	u32 packet_count;
	u32 packet_count_pre;
	s32 residual_cfo_acc;
	u8 phy_cfotrk_state;
	u8 phy_cfotrk_cnt;
	bool divergence_lock_en;
	u8 x_cap_lb;
	u8 x_cap_ub;
	u8 lock_cnt;
};

enum rtw89_tssi_mode {
	RTW89_TSSI_NORMAL = 0,
	RTW89_TSSI_SCAN = 1,
};

enum rtw89_tssi_alimk_band {
	TSSI_ALIMK_2G = 0,
	TSSI_ALIMK_5GL,
	TSSI_ALIMK_5GM,
	TSSI_ALIMK_5GH,
	TSSI_ALIMK_MAX
};

/* 2GL, 2GH, 5GL1, 5GH1, 5GM1, 5GM2, 5GH1, 5GH2 */
#define TSSI_TRIM_CH_GROUP_NUM 8
#define TSSI_TRIM_CH_GROUP_NUM_6G 16

#define TSSI_CCK_CH_GROUP_NUM 6
#define TSSI_MCS_2G_CH_GROUP_NUM 5
#define TSSI_MCS_5G_CH_GROUP_NUM 14
#define TSSI_MCS_6G_CH_GROUP_NUM 32
#define TSSI_MCS_CH_GROUP_NUM \
	(TSSI_MCS_2G_CH_GROUP_NUM + TSSI_MCS_5G_CH_GROUP_NUM)
#define TSSI_MAX_CH_NUM 67
#define TSSI_ALIMK_VALUE_NUM 8

struct rtw89_tssi_info {
	u8 thermal[RF_PATH_MAX];
	s8 tssi_trim[RF_PATH_MAX][TSSI_TRIM_CH_GROUP_NUM];
	s8 tssi_trim_6g[RF_PATH_MAX][TSSI_TRIM_CH_GROUP_NUM_6G];
	s8 tssi_cck[RF_PATH_MAX][TSSI_CCK_CH_GROUP_NUM];
	s8 tssi_mcs[RF_PATH_MAX][TSSI_MCS_CH_GROUP_NUM];
	s8 tssi_6g_mcs[RF_PATH_MAX][TSSI_MCS_6G_CH_GROUP_NUM];
	s8 extra_ofst[RF_PATH_MAX];
	bool tssi_tracking_check[RF_PATH_MAX];
	u8 default_txagc_offset[RF_PATH_MAX];
	u32 base_thermal[RF_PATH_MAX];
	bool check_backup_aligmk[RF_PATH_MAX][TSSI_MAX_CH_NUM];
	u32 alignment_backup_by_ch[RF_PATH_MAX][TSSI_MAX_CH_NUM][TSSI_ALIMK_VALUE_NUM];
	u32 alignment_value[RF_PATH_MAX][TSSI_ALIMK_MAX][TSSI_ALIMK_VALUE_NUM];
	bool alignment_done[RF_PATH_MAX][TSSI_ALIMK_MAX];
	u64 tssi_alimk_time;
};

struct rtw89_power_trim_info {
	bool pg_thermal_trim;
	bool pg_pa_bias_trim;
	u8 thermal_trim[RF_PATH_MAX];
	u8 pa_bias_trim[RF_PATH_MAX];
	u8 pad_bias_trim[RF_PATH_MAX];
};

enum rtw89_regd_func {
	RTW89_REGD_FUNC_TAS = 0, /* TAS (Time Average SAR) */
	RTW89_REGD_FUNC_DAG = 1, /* DAG (Dynamic Antenna Gain) */

	NUM_OF_RTW89_REGD_FUNC,
};

struct rtw89_regd {
	char alpha2[3];
	u8 txpwr_regd[RTW89_BAND_NUM];
	DECLARE_BITMAP(func_bitmap, NUM_OF_RTW89_REGD_FUNC);
};

struct rtw89_regd_data {
	unsigned int nr;
	struct rtw89_regd map[] __counted_by(nr);
};

struct rtw89_regd_ctrl {
	unsigned int nr;
	const struct rtw89_regd *map;
};

#define RTW89_REGD_MAX_COUNTRY_NUM U8_MAX
#define RTW89_5GHZ_UNII4_CHANNEL_NUM 3
#define RTW89_5GHZ_UNII4_START_INDEX 25

struct rtw89_regulatory_info {
	struct rtw89_regd_ctrl ctrl;
	const struct rtw89_regd *regd;
	enum rtw89_reg_6ghz_power reg_6ghz_power;
	struct rtw89_reg_6ghz_tpe reg_6ghz_tpe;
	bool txpwr_uk_follow_etsi;

	DECLARE_BITMAP(block_unii4, RTW89_REGD_MAX_COUNTRY_NUM);
	DECLARE_BITMAP(block_6ghz, RTW89_REGD_MAX_COUNTRY_NUM);
	DECLARE_BITMAP(block_6ghz_sp, RTW89_REGD_MAX_COUNTRY_NUM);
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
	u8 ccx_watchdog_result;
	bool ccx_ongoing;
	u8 ccx_rac_lv;
	bool ccx_manual_ctrl;
	u16 ifs_clm_mntr_time;
	enum rtw89_ifs_clm_application ifs_clm_app;
	u16 ccx_period;
	u8 ccx_unit_idx;
	u16 ifs_clm_th_l[RTW89_IFS_CLM_NUM];
	u16 ifs_clm_th_h[RTW89_IFS_CLM_NUM];
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
};

enum rtw89_ser_rcvy_step {
	RTW89_SER_DRV_STOP_TX,
	RTW89_SER_DRV_STOP_RX,
	RTW89_SER_DRV_STOP_RUN,
	RTW89_SER_HAL_STOP_DMA,
	RTW89_SER_SUPPRESS_LOG,
	RTW89_NUM_OF_SER_FLAGS
};

struct rtw89_ser {
	u8 state;
	u8 alarm_event;
	bool prehandle_l1;

	struct work_struct ser_hdl_work;
	struct delayed_work ser_alarm_work;
	const struct state_ent *st_tbl;
	const struct event_ent *ev_tbl;
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
	struct sk_buff_head rx_queue[RTW89_PHY_NUM];
	u8 curr_rx_ppdu_cnt[RTW89_PHY_NUM];
};

struct rtw89_early_h2c {
	struct list_head list;
	u8 *h2c;
	u16 h2c_len;
};

struct rtw89_hw_scan_extra_op {
	bool set;
	u8 macid;
	struct rtw89_chan chan;
};

struct rtw89_hw_scan_info {
	struct rtw89_vif_link *scanning_vif;
	struct list_head pkt_list[NUM_NL80211_BANDS];
	struct list_head chan_list;
	struct rtw89_chan op_chan;
	struct rtw89_hw_scan_extra_op extra_op;
	bool connected;
	bool abort;
};

enum rtw89_phy_bb_gain_band {
	RTW89_BB_GAIN_BAND_2G = 0,
	RTW89_BB_GAIN_BAND_5G_L = 1,
	RTW89_BB_GAIN_BAND_5G_M = 2,
	RTW89_BB_GAIN_BAND_5G_H = 3,
	RTW89_BB_GAIN_BAND_6G_L = 4,
	RTW89_BB_GAIN_BAND_6G_M = 5,
	RTW89_BB_GAIN_BAND_6G_H = 6,
	RTW89_BB_GAIN_BAND_6G_UH = 7,

	RTW89_BB_GAIN_BAND_NR,
};

enum rtw89_phy_gain_band_be {
	RTW89_BB_GAIN_BAND_2G_BE = 0,
	RTW89_BB_GAIN_BAND_5G_L_BE = 1,
	RTW89_BB_GAIN_BAND_5G_M_BE = 2,
	RTW89_BB_GAIN_BAND_5G_H_BE = 3,
	RTW89_BB_GAIN_BAND_6G_L0_BE = 4,
	RTW89_BB_GAIN_BAND_6G_L1_BE = 5,
	RTW89_BB_GAIN_BAND_6G_M0_BE = 6,
	RTW89_BB_GAIN_BAND_6G_M1_BE = 7,
	RTW89_BB_GAIN_BAND_6G_H0_BE = 8,
	RTW89_BB_GAIN_BAND_6G_H1_BE = 9,
	RTW89_BB_GAIN_BAND_6G_UH0_BE = 10,
	RTW89_BB_GAIN_BAND_6G_UH1_BE = 11,

	RTW89_BB_GAIN_BAND_NR_BE,
};

enum rtw89_phy_bb_bw_be {
	RTW89_BB_BW_20_40 = 0,
	RTW89_BB_BW_80_160_320 = 1,

	RTW89_BB_BW_NR_BE,
};

enum rtw89_bw20_sc {
	RTW89_BW20_SC_20M = 1,
	RTW89_BW20_SC_40M = 2,
	RTW89_BW20_SC_80M = 4,
	RTW89_BW20_SC_160M = 8,
	RTW89_BW20_SC_320M = 16,
};

enum rtw89_cmac_table_bw {
	RTW89_CMAC_BW_20M = 0,
	RTW89_CMAC_BW_40M = 1,
	RTW89_CMAC_BW_80M = 2,
	RTW89_CMAC_BW_160M = 3,
	RTW89_CMAC_BW_320M = 4,

	RTW89_CMAC_BW_NR,
};

enum rtw89_phy_bb_rxsc_num {
	RTW89_BB_RXSC_NUM_40 = 9, /* SC: 0, 1~8 */
	RTW89_BB_RXSC_NUM_80 = 13, /* SC: 0, 1~8, 9~12 */
	RTW89_BB_RXSC_NUM_160 = 15, /* SC: 0, 1~8, 9~12, 13~14 */
};

struct rtw89_phy_bb_gain_info {
	s8 lna_gain[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX][LNA_GAIN_NUM];
	s8 tia_gain[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX][TIA_GAIN_NUM];
	s8 lna_gain_bypass[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX][LNA_GAIN_NUM];
	s8 lna_op1db[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX][LNA_GAIN_NUM];
	s8 tia_lna_op1db[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX]
			[LNA_GAIN_NUM + 1]; /* TIA0_LNA0~6 + TIA1_LNA6 */
	s8 rpl_ofst_20[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX];
	s8 rpl_ofst_40[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX]
		      [RTW89_BB_RXSC_NUM_40];
	s8 rpl_ofst_80[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX]
		      [RTW89_BB_RXSC_NUM_80];
	s8 rpl_ofst_160[RTW89_BB_GAIN_BAND_NR][RF_PATH_MAX]
		       [RTW89_BB_RXSC_NUM_160];
};

struct rtw89_phy_bb_gain_info_be {
	s8 lna_gain[RTW89_BB_GAIN_BAND_NR_BE][RTW89_BB_BW_NR_BE][RF_PATH_MAX]
		   [LNA_GAIN_NUM];
	s8 tia_gain[RTW89_BB_GAIN_BAND_NR_BE][RTW89_BB_BW_NR_BE][RF_PATH_MAX]
		   [TIA_GAIN_NUM];
	s8 lna_gain_bypass[RTW89_BB_GAIN_BAND_NR_BE][RTW89_BB_BW_NR_BE]
			  [RF_PATH_MAX][LNA_GAIN_NUM];
	s8 lna_op1db[RTW89_BB_GAIN_BAND_NR_BE][RTW89_BB_BW_NR_BE]
		    [RF_PATH_MAX][LNA_GAIN_NUM];
	s8 tia_lna_op1db[RTW89_BB_GAIN_BAND_NR_BE][RTW89_BB_BW_NR_BE]
			[RF_PATH_MAX][LNA_GAIN_NUM + 1];
	s8 rpl_ofst_20[RTW89_BB_GAIN_BAND_NR_BE][RF_PATH_MAX]
		      [RTW89_BW20_SC_20M];
	s8 rpl_ofst_40[RTW89_BB_GAIN_BAND_NR_BE][RF_PATH_MAX]
		      [RTW89_BW20_SC_40M];
	s8 rpl_ofst_80[RTW89_BB_GAIN_BAND_NR_BE][RF_PATH_MAX]
		      [RTW89_BW20_SC_80M];
	s8 rpl_ofst_160[RTW89_BB_GAIN_BAND_NR_BE][RF_PATH_MAX]
		       [RTW89_BW20_SC_160M];
};

struct rtw89_phy_efuse_gain {
	bool offset_valid;
	bool comp_valid;
	s8 offset[RF_PATH_MAX][RTW89_GAIN_OFFSET_NR]; /* S(8, 0) */
	s8 offset_base[RTW89_PHY_NUM]; /* S(8, 4) */
	s8 rssi_base[RTW89_PHY_NUM]; /* S(8, 4) */
	s8 comp[RF_PATH_MAX][RTW89_SUBBAND_NR]; /* S(8, 0) */
};

#define RTW89_MAX_PATTERN_NUM             18
#define RTW89_MAX_PATTERN_MASK_SIZE       4
#define RTW89_MAX_PATTERN_SIZE            128

struct rtw89_wow_cam_info {
	bool r_w;
	u8 idx;
	u32 mask[RTW89_MAX_PATTERN_MASK_SIZE];
	u16 crc;
	bool negative_pattern_match;
	bool skip_mac_hdr;
	bool uc;
	bool mc;
	bool bc;
	bool valid;
};

struct rtw89_wow_key_info {
	u8 ptk_tx_iv[8];
	u8 valid_check;
	u8 symbol_check_en;
	u8 gtk_keyidx;
	u8 rsvd[5];
	u8 ptk_rx_iv[8];
	u8 gtk_rx_iv[4][8];
} __packed;

struct rtw89_wow_gtk_info {
	u8 kck[32];
	u8 kek[32];
	u8 tk1[16];
	u8 txmickey[8];
	u8 rxmickey[8];
	__le32 igtk_keyid;
	__le64 ipn;
	u8 igtk[2][32];
	u8 psk[32];
} __packed;

struct rtw89_wow_aoac_report {
	u8 rpt_ver;
	u8 sec_type;
	u8 key_idx;
	u8 pattern_idx;
	u8 rekey_ok;
	u8 ptk_tx_iv[8];
	u8 eapol_key_replay_count[8];
	u8 gtk[32];
	u8 ptk_rx_iv[8];
	u8 gtk_rx_iv[4][8];
	u64 igtk_key_id;
	u64 igtk_ipn;
	u8 igtk[32];
	u8 csa_pri_ch;
	u8 csa_bw;
	u8 csa_ch_offset;
	u8 csa_chsw_failed;
	u8 csa_ch_band;
};

struct rtw89_wow_param {
	struct rtw89_vif_link *rtwvif_link;
	DECLARE_BITMAP(flags, RTW89_WOW_FLAG_NUM);
	struct rtw89_wow_cam_info patterns[RTW89_MAX_PATTERN_NUM];
	struct rtw89_wow_key_info key_info;
	struct rtw89_wow_gtk_info gtk_info;
	struct rtw89_wow_aoac_report aoac_rpt;
	u8 pattern_cnt;
	u8 ptk_alg;
	u8 gtk_alg;
	u8 ptk_keyidx;
	u8 akm;

	/* see RTW89_WOW_WAIT_COND series for wait condition */
	struct rtw89_wait_info wait;

	bool pno_inited;
	struct list_head pno_pkt_list;
	struct cfg80211_sched_scan_request *nd_config;
};

struct rtw89_mcc_limit {
	bool enable;
	u16 max_tob; /* TU; max time offset behind */
	u16 max_toa; /* TU; max time offset ahead */
	u16 max_dur; /* TU */
};

struct rtw89_mcc_policy {
	u8 c2h_rpt;
	u8 tx_null_early;
	u8 dis_tx_null;
	u8 in_curr_ch;
	u8 dis_sw_retry;
	u8 sw_retry_count;
};

struct rtw89_mcc_role {
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_mcc_policy policy;
	struct rtw89_mcc_limit limit;

	const struct rtw89_mcc_courtesy_cfg *crtz;

	/* only valid when running with FW MRC mechanism */
	u8 slot_idx;

	/* byte-array in LE order for FW */
	u8 macid_bitmap[BITS_TO_BYTES(RTW89_MAX_MAC_ID_NUM)];
	u8 probe_count;

	u16 duration; /* TU */
	u16 beacon_interval; /* TU */
	bool is_2ghz;
	bool is_go;
	bool is_gc;
};

struct rtw89_mcc_bt_role {
	u16 duration; /* TU */
};

struct rtw89_mcc_courtesy_cfg {
	u8 slot_num;
	u8 macid_tgt;
};

struct rtw89_mcc_courtesy {
	struct rtw89_mcc_courtesy_cfg ref;
	struct rtw89_mcc_courtesy_cfg aux;
};

enum rtw89_mcc_plan {
	RTW89_MCC_PLAN_TAIL_BT,
	RTW89_MCC_PLAN_MID_BT,
	RTW89_MCC_PLAN_NO_BT,

	NUM_OF_RTW89_MCC_PLAN,
};

struct rtw89_mcc_pattern {
	s16 tob_ref; /* TU; time offset behind of reference role */
	s16 toa_ref; /* TU; time offset ahead of reference role */
	s16 tob_aux; /* TU; time offset behind of auxiliary role */
	s16 toa_aux; /* TU; time offset ahead of auxiliary role */

	enum rtw89_mcc_plan plan;
	struct rtw89_mcc_courtesy courtesy;
};

struct rtw89_mcc_sync {
	bool enable;
	u16 offset; /* TU */
	u8 macid_src;
	u8 band_src;
	u8 port_src;
	u8 macid_tgt;
	u8 band_tgt;
	u8 port_tgt;
};

struct rtw89_mcc_config {
	struct rtw89_mcc_pattern pattern;
	struct rtw89_mcc_sync sync;
	u64 start_tsf;
	u64 start_tsf_in_aux_domain;
	u64 prepare_delay;
	u16 mcc_interval; /* TU */
	u16 beacon_offset; /* TU */
};

enum rtw89_mcc_mode {
	RTW89_MCC_MODE_GO_STA,
	RTW89_MCC_MODE_GC_STA,
};

struct rtw89_mcc_info {
	struct rtw89_wait_info wait;

	u8 group;
	enum rtw89_mcc_mode mode;
	struct rtw89_mcc_role role_ref; /* reference role */
	struct rtw89_mcc_role role_aux; /* auxiliary role */
	struct rtw89_mcc_bt_role bt_role;
	struct rtw89_mcc_config config;
};

enum rtw89_mlo_mode {
	RTW89_MLO_MODE_MLSR = 0,

	NUM_OF_RTW89_MLO_MODE,
};

struct rtw89_mlo_info {
	struct rtw89_wait_info wait;
};

struct rtw89_dev {
	struct ieee80211_hw *hw;
	struct device *dev;
	const struct ieee80211_ops *ops;

	bool dbcc_en;
	bool support_mlo;
	enum rtw89_mlo_dbcc_mode mlo_dbcc_mode;
	struct rtw89_hw_scan_info scan_info;
	const struct rtw89_chip_info *chip;
	const struct rtw89_chip_variant *variant;
	const struct rtw89_pci_info *pci_info;
	const struct rtw89_rfe_parms *rfe_parms;
	struct rtw89_hal hal;
	struct rtw89_mcc_info mcc;
	struct rtw89_mlo_info mlo;
	struct rtw89_mac_info mac;
	struct rtw89_fw_info fw;
	struct rtw89_hci_info hci;
	struct rtw89_efuse efuse;
	struct rtw89_traffic_stats stats;
	struct rtw89_rfe_data *rfe_data;
	enum rtw89_custid custid;

	struct rtw89_sta_link __rcu *assoc_link_on_macid[RTW89_MAX_MAC_ID_NUM];
	refcount_t refcount_ap_info;

	struct list_head rtwvifs_list;
	/* used to protect rf read write */
	struct mutex rf_mutex;
	struct workqueue_struct *txq_wq;
	struct work_struct txq_work;
	struct delayed_work txq_reinvoke_work;
	/* used to protect ba_list and forbid_ba_list */
	spinlock_t ba_lock;
	/* txqs to setup ba session */
	struct list_head ba_list;
	/* txqs to forbid ba session */
	struct list_head forbid_ba_list;
	struct work_struct ba_work;
	/* used to protect rpwm */
	spinlock_t rpwm_lock;

	struct rtw89_cam_info cam_info;

	struct sk_buff_head c2h_queue;
	struct wiphy_work c2h_work;
	struct wiphy_work ips_work;
	struct wiphy_work cancel_6ghz_probe_work;
	struct work_struct load_firmware_work;

	struct list_head early_h2c_list;

	struct rtw89_ser ser;

	DECLARE_BITMAP(hw_port, RTW89_PORT_NUM);
	DECLARE_BITMAP(mac_id_map, RTW89_MAX_MAC_ID_NUM);
	DECLARE_BITMAP(flags, NUM_OF_RTW89_FLAGS);
	DECLARE_BITMAP(pkt_offload, RTW89_MAX_PKT_OFLD_NUM);
	DECLARE_BITMAP(quirks, NUM_OF_RTW89_QUIRKS);

	struct rtw89_phy_stat phystat;
	struct rtw89_rfk_wait_info rfk_wait;
	struct rtw89_dack_info dack;
	struct rtw89_iqk_info iqk;
	struct rtw89_dpk_info dpk;
	struct rtw89_rfk_mcc_info rfk_mcc;
	struct rtw89_lck_info lck;
	struct rtw89_rx_dck_info rx_dck;
	bool is_tssi_mode[RF_PATH_MAX];
	bool is_bt_iqk_timeout;

	struct rtw89_fem_info fem;
	struct rtw89_txpwr_byrate byr[RTW89_BAND_NUM][RTW89_BYR_BW_NUM];
	struct rtw89_tssi_info tssi;
	struct rtw89_power_trim_info pwr_trim;

	struct rtw89_cfo_tracking_info cfo_tracking;
	union {
		struct rtw89_phy_bb_gain_info ax;
		struct rtw89_phy_bb_gain_info_be be;
	} bb_gain;
	struct rtw89_phy_efuse_gain efuse_gain;
	struct rtw89_phy_ul_tb_info ul_tb_info;
	struct rtw89_antdiv_info antdiv;

	struct rtw89_bb_ctx {
		enum rtw89_phy_idx phy_idx;
		struct rtw89_env_monitor_info env_monitor;
		struct rtw89_dig_info dig;
		struct rtw89_phy_ch_info ch_info;
		struct rtw89_edcca_bak edcca_bak;
	} bbs[RTW89_PHY_NUM];

	struct wiphy_delayed_work track_work;
	struct wiphy_delayed_work chanctx_work;
	struct wiphy_delayed_work coex_act1_work;
	struct wiphy_delayed_work coex_bt_devinfo_work;
	struct wiphy_delayed_work coex_rfk_chk_work;
	struct wiphy_delayed_work cfo_track_work;
	struct wiphy_delayed_work mcc_prepare_done_work;
	struct delayed_work forbid_ba_work;
	struct wiphy_delayed_work antdiv_work;
	struct rtw89_ppdu_sts_info ppdu_sts;
	u8 total_sta_assoc;
	bool scanning;

	struct rtw89_regulatory_info regulatory;
	struct rtw89_sar_info sar;
	struct rtw89_tas_info tas;
	struct rtw89_ant_gain_info ant_gain;

	struct rtw89_btc btc;
	enum rtw89_ps_mode ps_mode;
	bool lps_enabled;

	struct rtw89_wow_param wow;

	/* napi structure */
	struct net_device *netdev;
	struct napi_struct napi;
	int napi_budget_countdown;

	struct rtw89_debugfs *debugfs;

	/* HCI related data, keep last */
	u8 priv[] __aligned(sizeof(void *));
};

struct rtw89_link_conf_container {
	struct ieee80211_bss_conf *link_conf[IEEE80211_MLD_MAX_NUM_LINKS];
};

#define RTW89_VIF_IDLE_LINK_ID 0

struct rtw89_vif {
	struct rtw89_dev *rtwdev;
	struct list_head list;
	struct list_head mgnt_entry;
	struct rtw89_link_conf_container __rcu *snap_link_confs;

	u8 mac_addr[ETH_ALEN];
	__be32 ip_addr;

	struct rtw89_traffic_stats stats;
	u32 tdls_peer;

	struct ieee80211_scan_ies *scan_ies;
	struct cfg80211_scan_request *scan_req;

	struct rtw89_roc roc;
	bool offchan;

	enum rtw89_mlo_mode mlo_mode;

	struct list_head dlink_pool;
	u8 links_inst_valid_num;
	DECLARE_BITMAP(links_inst_map, __RTW89_MLD_MAX_LINK_NUM);
	struct rtw89_vif_link *links[IEEE80211_MLD_MAX_NUM_LINKS];
	struct rtw89_vif_link links_inst[] __counted_by(links_inst_valid_num);
};

static inline bool rtw89_vif_assign_link_is_valid(struct rtw89_vif_link **rtwvif_link,
						  const struct rtw89_vif *rtwvif,
						  unsigned int link_id)
{
	*rtwvif_link = rtwvif->links[link_id];
	return !!*rtwvif_link;
}

#define rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) \
	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) \
		if (rtw89_vif_assign_link_is_valid(&(rtwvif_link), rtwvif, link_id))

enum rtw89_sta_flags {
	RTW89_REMOTE_STA_IN_PS,

	NUM_OF_RTW89_STA_FLAGS,
};

struct rtw89_sta {
	struct rtw89_dev *rtwdev;
	struct rtw89_vif *rtwvif;

	DECLARE_BITMAP(flags, NUM_OF_RTW89_STA_FLAGS);

	bool disassoc;

	struct sk_buff_head roc_queue;

	struct rtw89_ampdu_params ampdu_params[IEEE80211_NUM_TIDS];
	DECLARE_BITMAP(ampdu_map, IEEE80211_NUM_TIDS);

	DECLARE_BITMAP(pairwise_sec_cam_map, RTW89_MAX_SEC_CAM_NUM);

	struct list_head dlink_pool;
	u8 links_inst_valid_num;
	DECLARE_BITMAP(links_inst_map, __RTW89_MLD_MAX_LINK_NUM);
	struct rtw89_sta_link *links[IEEE80211_MLD_MAX_NUM_LINKS];
	struct rtw89_sta_link links_inst[] __counted_by(links_inst_valid_num);
};

static inline bool rtw89_sta_assign_link_is_valid(struct rtw89_sta_link **rtwsta_link,
						  const struct rtw89_sta *rtwsta,
						  unsigned int link_id)
{
	*rtwsta_link = rtwsta->links[link_id];
	return !!*rtwsta_link;
}

#define rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id) \
	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++) \
		if (rtw89_sta_assign_link_is_valid(&(rtwsta_link), rtwsta, link_id))

static inline u8 rtw89_vif_get_main_macid(struct rtw89_vif *rtwvif)
{
	/* const after init, so no need to check if active first */
	return rtwvif->links_inst[0].mac_id;
}

static inline u8 rtw89_vif_get_main_port(struct rtw89_vif *rtwvif)
{
	/* const after init, so no need to check if active first */
	return rtwvif->links_inst[0].port;
}

static inline struct rtw89_vif_link *
rtw89_vif_get_link_inst(struct rtw89_vif *rtwvif, u8 index)
{
	if (index >= rtwvif->links_inst_valid_num ||
	    !test_bit(index, rtwvif->links_inst_map))
		return NULL;
	return &rtwvif->links_inst[index];
}

static inline
u8 rtw89_vif_link_inst_get_index(struct rtw89_vif_link *rtwvif_link)
{
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;

	return rtwvif_link - rtwvif->links_inst;
}

static inline u8 rtw89_sta_get_main_macid(struct rtw89_sta *rtwsta)
{
	/* const after init, so no need to check if active first */
	return rtwsta->links_inst[0].mac_id;
}

static inline struct rtw89_sta_link *
rtw89_sta_get_link_inst(struct rtw89_sta *rtwsta, u8 index)
{
	if (index >= rtwsta->links_inst_valid_num ||
	    !test_bit(index, rtwsta->links_inst_map))
		return NULL;
	return &rtwsta->links_inst[index];
}

static inline
u8 rtw89_sta_link_inst_get_index(struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_sta *rtwsta = rtwsta_link->rtwsta;

	return rtwsta_link - rtwsta->links_inst;
}

static inline void rtw89_assoc_link_set(struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_sta *rtwsta = rtwsta_link->rtwsta;
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;

	rcu_assign_pointer(rtwdev->assoc_link_on_macid[rtwsta_link->mac_id],
			   rtwsta_link);
}

static inline void rtw89_assoc_link_clr(struct rtw89_sta_link *rtwsta_link)
{
	struct rtw89_sta *rtwsta = rtwsta_link->rtwsta;
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;

	rcu_assign_pointer(rtwdev->assoc_link_on_macid[rtwsta_link->mac_id],
			   NULL);
	synchronize_rcu();
}

static inline struct rtw89_sta_link *
rtw89_assoc_link_rcu_dereference(struct rtw89_dev *rtwdev, u8 macid)
{
	return rcu_dereference(rtwdev->assoc_link_on_macid[macid]);
}

#define rtw89_get_designated_link(links_holder) \
({ \
	typeof(links_holder) p = links_holder; \
	list_first_entry_or_null(&p->dlink_pool, typeof(*p->links_inst), dlink_schd); \
})

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

static inline void rtw89_hci_pause(struct rtw89_dev *rtwdev, bool pause)
{
	rtwdev->hci.ops->pause(rtwdev, pause);
}

static inline void rtw89_hci_switch_mode(struct rtw89_dev *rtwdev, bool low_power)
{
	rtwdev->hci.ops->switch_mode(rtwdev, low_power);
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

static inline int rtw89_hci_mac_pre_deinit(struct rtw89_dev *rtwdev)
{
	return rtwdev->hci.ops->mac_pre_deinit(rtwdev);
}

static inline void rtw89_hci_flush_queues(struct rtw89_dev *rtwdev, u32 queues,
					  bool drop)
{
	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags))
		return;

	if (rtwdev->hci.ops->flush_queues)
		return rtwdev->hci.ops->flush_queues(rtwdev, queues, drop);
}

static inline void rtw89_hci_recovery_start(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hci.ops->recovery_start)
		rtwdev->hci.ops->recovery_start(rtwdev);
}

static inline void rtw89_hci_recovery_complete(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hci.ops->recovery_complete)
		rtwdev->hci.ops->recovery_complete(rtwdev);
}

static inline void rtw89_hci_enable_intr(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hci.ops->enable_intr)
		rtwdev->hci.ops->enable_intr(rtwdev);
}

static inline void rtw89_hci_disable_intr(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hci.ops->disable_intr)
		rtwdev->hci.ops->disable_intr(rtwdev);
}

static inline void rtw89_hci_ctrl_txdma_ch(struct rtw89_dev *rtwdev, bool enable)
{
	if (rtwdev->hci.ops->ctrl_txdma_ch)
		rtwdev->hci.ops->ctrl_txdma_ch(rtwdev, enable);
}

static inline void rtw89_hci_ctrl_txdma_fw_ch(struct rtw89_dev *rtwdev, bool enable)
{
	if (rtwdev->hci.ops->ctrl_txdma_fw_ch)
		rtwdev->hci.ops->ctrl_txdma_fw_ch(rtwdev, enable);
}

static inline void rtw89_hci_ctrl_trxhci(struct rtw89_dev *rtwdev, bool enable)
{
	if (rtwdev->hci.ops->ctrl_trxhci)
		rtwdev->hci.ops->ctrl_trxhci(rtwdev, enable);
}

static inline int rtw89_hci_poll_txdma_ch_idle(struct rtw89_dev *rtwdev)
{
	int ret = 0;

	if (rtwdev->hci.ops->poll_txdma_ch_idle)
		ret = rtwdev->hci.ops->poll_txdma_ch_idle(rtwdev);
	return ret;
}

static inline void rtw89_hci_clr_idx_all(struct rtw89_dev *rtwdev)
{
	if (rtwdev->hci.ops->clr_idx_all)
		rtwdev->hci.ops->clr_idx_all(rtwdev);
}

static inline int rtw89_hci_rst_bdram(struct rtw89_dev *rtwdev)
{
	int ret = 0;

	if (rtwdev->hci.ops->rst_bdram)
		ret = rtwdev->hci.ops->rst_bdram(rtwdev);
	return ret;
}

static inline void rtw89_hci_clear(struct rtw89_dev *rtwdev, struct pci_dev *pdev)
{
	if (rtwdev->hci.ops->clear)
		rtwdev->hci.ops->clear(rtwdev, pdev);
}

static inline
struct rtw89_tx_skb_data *RTW89_TX_SKB_CB(struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

	return (struct rtw89_tx_skb_data *)info->status.status_driver_data;
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

static inline struct ieee80211_vif *rtwvif_to_vif_safe(struct rtw89_vif *rtwvif)
{
	return rtwvif ? rtwvif_to_vif(rtwvif) : NULL;
}

static inline
struct ieee80211_vif *rtwvif_link_to_vif(struct rtw89_vif_link *rtwvif_link)
{
	return rtwvif_to_vif(rtwvif_link->rtwvif);
}

static inline
struct ieee80211_vif *rtwvif_link_to_vif_safe(struct rtw89_vif_link *rtwvif_link)
{
	return rtwvif_link ? rtwvif_link_to_vif(rtwvif_link) : NULL;
}

static inline struct rtw89_vif *vif_to_rtwvif(struct ieee80211_vif *vif)
{
	return (struct rtw89_vif *)vif->drv_priv;
}

static inline struct rtw89_vif *vif_to_rtwvif_safe(struct ieee80211_vif *vif)
{
	return vif ? vif_to_rtwvif(vif) : NULL;
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

static inline
struct ieee80211_sta *rtwsta_link_to_sta(struct rtw89_sta_link *rtwsta_link)
{
	return rtwsta_to_sta(rtwsta_link->rtwsta);
}

static inline
struct ieee80211_sta *rtwsta_link_to_sta_safe(struct rtw89_sta_link *rtwsta_link)
{
	return rtwsta_link ? rtwsta_link_to_sta(rtwsta_link) : NULL;
}

static inline struct rtw89_sta *sta_to_rtwsta(struct ieee80211_sta *sta)
{
	return (struct rtw89_sta *)sta->drv_priv;
}

static inline struct rtw89_sta *sta_to_rtwsta_safe(struct ieee80211_sta *sta)
{
	return sta ? sta_to_rtwsta(sta) : NULL;
}

static inline struct ieee80211_bss_conf *
__rtw89_vif_rcu_dereference_link(struct rtw89_vif_link *rtwvif_link, bool *nolink)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	struct rtw89_link_conf_container *snap;
	struct ieee80211_bss_conf *bss_conf;

	snap = rcu_dereference(rtwvif->snap_link_confs);
	if (snap) {
		bss_conf = snap->link_conf[rtwvif_link->link_id];
		goto out;
	}

	bss_conf = rcu_dereference(vif->link_conf[rtwvif_link->link_id]);

out:
	if (unlikely(!bss_conf)) {
		*nolink = true;
		return &vif->bss_conf;
	}

	*nolink = false;
	return bss_conf;
}

#define rtw89_vif_rcu_dereference_link(rtwvif_link, assert)		\
({									\
	typeof(rtwvif_link) p = rtwvif_link;				\
	struct ieee80211_bss_conf *bss_conf;				\
	bool nolink;							\
									\
	bss_conf = __rtw89_vif_rcu_dereference_link(p, &nolink);	\
	if (unlikely(nolink) && (assert))				\
		rtw89_err(p->rtwvif->rtwdev,				\
			  "%s: cannot find exact bss_conf for link_id %u\n",\
			  __func__, p->link_id);			\
	bss_conf;							\
})

static inline struct ieee80211_link_sta *
__rtw89_sta_rcu_dereference_link(struct rtw89_sta_link *rtwsta_link, bool *nolink)
{
	struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);
	struct ieee80211_link_sta *link_sta;

	link_sta = rcu_dereference(sta->link[rtwsta_link->link_id]);
	if (unlikely(!link_sta)) {
		*nolink = true;
		return &sta->deflink;
	}

	*nolink = false;
	return link_sta;
}

#define rtw89_sta_rcu_dereference_link(rtwsta_link, assert)		\
({									\
	typeof(rtwsta_link) p = rtwsta_link;				\
	struct ieee80211_link_sta *link_sta;				\
	bool nolink;							\
									\
	link_sta = __rtw89_sta_rcu_dereference_link(p, &nolink);	\
	if (unlikely(nolink) && (assert))				\
		rtw89_err(p->rtwsta->rtwdev,				\
			  "%s: cannot find exact link_sta for link_id %u\n",\
			  __func__, p->link_id);			\
	link_sta;							\
})

static inline u8 rtw89_hw_to_rate_info_bw(enum rtw89_bandwidth hw_bw)
{
	if (hw_bw == RTW89_CHANNEL_WIDTH_160)
		return RATE_INFO_BW_160;
	else if (hw_bw == RTW89_CHANNEL_WIDTH_80)
		return RATE_INFO_BW_80;
	else if (hw_bw == RTW89_CHANNEL_WIDTH_40)
		return RATE_INFO_BW_40;
	else
		return RATE_INFO_BW_20;
}

static inline
enum nl80211_band rtw89_hw_to_nl80211_band(enum rtw89_band hw_band)
{
	switch (hw_band) {
	default:
	case RTW89_BAND_2G:
		return NL80211_BAND_2GHZ;
	case RTW89_BAND_5G:
		return NL80211_BAND_5GHZ;
	case RTW89_BAND_6G:
		return NL80211_BAND_6GHZ;
	}
}

static inline
enum rtw89_band rtw89_nl80211_to_hw_band(enum nl80211_band nl_band)
{
	switch (nl_band) {
	default:
	case NL80211_BAND_2GHZ:
		return RTW89_BAND_2G;
	case NL80211_BAND_5GHZ:
		return RTW89_BAND_5G;
	case NL80211_BAND_6GHZ:
		return RTW89_BAND_6G;
	}
}

static inline
enum rtw89_bandwidth nl_to_rtw89_bandwidth(enum nl80211_chan_width width)
{
	switch (width) {
	default:
		WARN(1, "Not support bandwidth %d\n", width);
		fallthrough;
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return RTW89_CHANNEL_WIDTH_20;
	case NL80211_CHAN_WIDTH_40:
		return RTW89_CHANNEL_WIDTH_40;
	case NL80211_CHAN_WIDTH_80:
		return RTW89_CHANNEL_WIDTH_80;
	case NL80211_CHAN_WIDTH_160:
		return RTW89_CHANNEL_WIDTH_160;
	}
}

static inline
enum nl80211_he_ru_alloc rtw89_he_rua_to_ru_alloc(u16 rua)
{
	switch (rua) {
	default:
		WARN(1, "Invalid RU allocation: %d\n", rua);
		fallthrough;
	case 0 ... 36:
		return NL80211_RATE_INFO_HE_RU_ALLOC_26;
	case 37 ... 52:
		return NL80211_RATE_INFO_HE_RU_ALLOC_52;
	case 53 ... 60:
		return NL80211_RATE_INFO_HE_RU_ALLOC_106;
	case 61 ... 64:
		return NL80211_RATE_INFO_HE_RU_ALLOC_242;
	case 65 ... 66:
		return NL80211_RATE_INFO_HE_RU_ALLOC_484;
	case 67:
		return NL80211_RATE_INFO_HE_RU_ALLOC_996;
	case 68:
		return NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
	}
}

static inline
struct rtw89_addr_cam_entry *rtw89_get_addr_cam_of(struct rtw89_vif_link *rtwvif_link,
						   struct rtw89_sta_link *rtwsta_link)
{
	if (rtwsta_link) {
		struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);

		if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE || sta->tdls)
			return &rtwsta_link->addr_cam;
	}
	return &rtwvif_link->addr_cam;
}

static inline
struct rtw89_bssid_cam_entry *rtw89_get_bssid_cam_of(struct rtw89_vif_link *rtwvif_link,
						     struct rtw89_sta_link *rtwsta_link)
{
	if (rtwsta_link) {
		struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);

		if (sta->tdls)
			return &rtwsta_link->bssid_cam;
	}
	return &rtwvif_link->bssid_cam;
}

static inline
void rtw89_chip_set_channel_prepare(struct rtw89_dev *rtwdev,
				    struct rtw89_channel_help_params *p,
				    const struct rtw89_chan *chan,
				    enum rtw89_mac_idx mac_idx,
				    enum rtw89_phy_idx phy_idx)
{
	rtwdev->chip->ops->set_channel_help(rtwdev, true, p, chan,
					    mac_idx, phy_idx);
}

static inline
void rtw89_chip_set_channel_done(struct rtw89_dev *rtwdev,
				 struct rtw89_channel_help_params *p,
				 const struct rtw89_chan *chan,
				 enum rtw89_mac_idx mac_idx,
				 enum rtw89_phy_idx phy_idx)
{
	rtwdev->chip->ops->set_channel_help(rtwdev, false, p, chan,
					    mac_idx, phy_idx);
}

static inline
const struct cfg80211_chan_def *rtw89_chandef_get(struct rtw89_dev *rtwdev,
						  enum rtw89_chanctx_idx idx)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	enum rtw89_chanctx_idx roc_idx = atomic_read(&hal->roc_chanctx_idx);

	if (roc_idx == idx)
		return &hal->roc_chandef;

	return &hal->chanctx[idx].chandef;
}

static inline
const struct rtw89_chan *rtw89_chan_get(struct rtw89_dev *rtwdev,
					enum rtw89_chanctx_idx idx)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return &hal->chanctx[idx].chan;
}

static inline
const struct rtw89_chan_rcd *rtw89_chan_rcd_get(struct rtw89_dev *rtwdev,
						enum rtw89_chanctx_idx idx)
{
	struct rtw89_hal *hal = &rtwdev->hal;

	return &hal->chanctx[idx].rcd;
}

static inline
const struct rtw89_chan_rcd *rtw89_chan_rcd_get_by_chan(const struct rtw89_chan *chan)
{
	const struct rtw89_chanctx *chanctx =
		container_of_const(chan, struct rtw89_chanctx, chan);

	return &chanctx->rcd;
}

static inline
const struct rtw89_chan *rtw89_scan_chan_get(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link = rtwdev->scan_info.scanning_vif;

	if (rtwvif_link)
		return rtw89_chan_get(rtwdev, rtwvif_link->chanctx_idx);
	else
		return rtw89_chan_get(rtwdev, RTW89_CHANCTX_0);
}

static inline void rtw89_chip_fem_setup(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->fem_setup)
		chip->ops->fem_setup(rtwdev);
}

static inline void rtw89_chip_rfe_gpio(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfe_gpio)
		chip->ops->rfe_gpio(rtwdev);
}

static inline void rtw89_chip_rfk_hw_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_hw_init)
		chip->ops->rfk_hw_init(rtwdev);
}

static inline
void rtw89_chip_bb_preinit(struct rtw89_dev *rtwdev, enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->bb_preinit)
		chip->ops->bb_preinit(rtwdev, phy_idx);
}

static inline
void rtw89_chip_bb_postinit(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!chip->ops->bb_postinit)
		return;

	chip->ops->bb_postinit(rtwdev, RTW89_PHY_0);

	if (rtwdev->dbcc_en)
		chip->ops->bb_postinit(rtwdev, RTW89_PHY_1);
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

static inline void rtw89_chip_rfk_init_late(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_init_late)
		chip->ops->rfk_init_late(rtwdev);
}

static inline void rtw89_chip_rfk_channel(struct rtw89_dev *rtwdev,
					  struct rtw89_vif_link *rtwvif_link)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_channel)
		chip->ops->rfk_channel(rtwdev, rtwvif_link);
}

static inline void rtw89_chip_rfk_band_changed(struct rtw89_dev *rtwdev,
					       enum rtw89_phy_idx phy_idx,
					       const struct rtw89_chan *chan)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_band_changed)
		chip->ops->rfk_band_changed(rtwdev, phy_idx, chan);
}

static inline void rtw89_chip_rfk_scan(struct rtw89_dev *rtwdev,
				       struct rtw89_vif_link *rtwvif_link, bool start)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->rfk_scan)
		chip->ops->rfk_scan(rtwdev, rtwvif_link, start);
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

	if (!chip->ops->set_txpwr_ctrl)
		return;

	chip->ops->set_txpwr_ctrl(rtwdev,  RTW89_PHY_0);
	if (rtwdev->dbcc_en)
		chip->ops->set_txpwr_ctrl(rtwdev,  RTW89_PHY_1);
}

static inline void rtw89_chip_power_trim(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->power_trim)
		chip->ops->power_trim(rtwdev);
}

static inline void __rtw89_chip_init_txpwr_unit(struct rtw89_dev *rtwdev,
						enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->init_txpwr_unit)
		chip->ops->init_txpwr_unit(rtwdev, phy_idx);
}

static inline void rtw89_chip_init_txpwr_unit(struct rtw89_dev *rtwdev)
{
	__rtw89_chip_init_txpwr_unit(rtwdev, RTW89_PHY_0);
	if (rtwdev->dbcc_en)
		__rtw89_chip_init_txpwr_unit(rtwdev, RTW89_PHY_1);
}

static inline u8 rtw89_chip_get_thermal(struct rtw89_dev *rtwdev,
					enum rtw89_rf_path rf_path)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!chip->ops->get_thermal)
		return 0x10;

	return chip->ops->get_thermal(rtwdev, rf_path);
}

static inline u32 rtw89_chip_chan_to_rf18_val(struct rtw89_dev *rtwdev,
					      const struct rtw89_chan *chan)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!chip->ops->chan_to_rf18_val)
		return 0;

	return chip->ops->chan_to_rf18_val(rtwdev, chan);
}

static inline void rtw89_chip_query_ppdu(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_phy_ppdu *phy_ppdu,
					 struct ieee80211_rx_status *status)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->query_ppdu)
		chip->ops->query_ppdu(rtwdev, phy_ppdu, status);
}

static inline void rtw89_chip_convert_rpl_to_rssi(struct rtw89_dev *rtwdev,
						  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->convert_rpl_to_rssi)
		chip->ops->convert_rpl_to_rssi(rtwdev, phy_ppdu);
}

static inline void rtw89_chip_phy_rpt_to_rssi(struct rtw89_dev *rtwdev,
					      struct rtw89_rx_desc_info *desc_info,
					      struct ieee80211_rx_status *rx_status)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->phy_rpt_to_rssi)
		chip->ops->phy_rpt_to_rssi(rtwdev, desc_info, rx_status);
}

static inline void rtw89_ctrl_nbtg_bt_tx(struct rtw89_dev *rtwdev, bool en,
					 enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->ctrl_nbtg_bt_tx)
		chip->ops->ctrl_nbtg_bt_tx(rtwdev, en, phy_idx);
}

static inline void rtw89_chip_cfg_txrx_path(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->cfg_txrx_path)
		chip->ops->cfg_txrx_path(rtwdev);
}

static inline void rtw89_chip_digital_pwr_comp(struct rtw89_dev *rtwdev,
					       enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->digital_pwr_comp)
		chip->ops->digital_pwr_comp(rtwdev, phy_idx);
}

static inline void rtw89_load_txpwr_table(struct rtw89_dev *rtwdev,
					  const struct rtw89_txpwr_table *tbl)
{
	tbl->load(rtwdev, tbl);
}

static inline u8 rtw89_regd_get(struct rtw89_dev *rtwdev, u8 band)
{
	const struct rtw89_regulatory_info *regulatory = &rtwdev->regulatory;
	const struct rtw89_regd *regd = regulatory->regd;
	u8 txpwr_regd = regd->txpwr_regd[band];

	if (regulatory->txpwr_uk_follow_etsi && txpwr_regd == RTW89_UK)
		return RTW89_ETSI;

	return txpwr_regd;
}

static inline void rtw89_ctrl_btg_bt_rx(struct rtw89_dev *rtwdev, bool en,
					enum rtw89_phy_idx phy_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (chip->ops->ctrl_btg_bt_rx)
		chip->ops->ctrl_btg_bt_rx(rtwdev, en, phy_idx);
}

static inline
void rtw89_chip_query_rxdesc(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	chip->ops->query_rxdesc(rtwdev, desc_info, data, data_offset);
}

static inline
void rtw89_chip_fill_txdesc(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	chip->ops->fill_txdesc(rtwdev, desc_info, txdesc);
}

static inline
void rtw89_chip_fill_txdesc_fwcmd(struct rtw89_dev *rtwdev,
				  struct rtw89_tx_desc_info *desc_info,
				  void *txdesc)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	chip->ops->fill_txdesc_fwcmd(rtwdev, desc_info, txdesc);
}

static inline
void rtw89_chip_mac_cfg_gnt(struct rtw89_dev *rtwdev,
			    const struct rtw89_mac_ax_coex_gnt *gnt_cfg)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	chip->ops->mac_cfg_gnt(rtwdev, gnt_cfg);
}

static inline void rtw89_chip_cfg_ctrl_path(struct rtw89_dev *rtwdev, bool wl)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	chip->ops->cfg_ctrl_path(rtwdev, wl);
}

static inline
int rtw89_chip_stop_sch_tx(struct rtw89_dev *rtwdev, u8 mac_idx,
			   u32 *tx_en, enum rtw89_sch_tx_sel sel)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->stop_sch_tx(rtwdev, mac_idx, tx_en, sel);
}

static inline
int rtw89_chip_resume_sch_tx(struct rtw89_dev *rtwdev, u8 mac_idx, u32 tx_en)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	return chip->ops->resume_sch_tx(rtwdev, mac_idx, tx_en);
}

static inline
int rtw89_chip_h2c_dctl_sec_cam(struct rtw89_dev *rtwdev,
				struct rtw89_vif_link *rtwvif_link,
				struct rtw89_sta_link *rtwsta_link)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!chip->ops->h2c_dctl_sec_cam)
		return 0;
	return chip->ops->h2c_dctl_sec_cam(rtwdev, rtwvif_link, rtwsta_link);
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

static inline
bool rtw89_sta_has_beamformer_cap(struct ieee80211_link_sta *link_sta)
{
	if ((link_sta->vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMER_CAPABLE) ||
	    (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE) ||
	    (link_sta->he_cap.he_cap_elem.phy_cap_info[3] &
			IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER) ||
	    (link_sta->he_cap.he_cap_elem.phy_cap_info[4] &
			IEEE80211_HE_PHY_CAP4_MU_BEAMFORMER))
		return true;
	return false;
}

static inline
bool rtw89_sta_link_has_su_mu_4xhe08(struct ieee80211_link_sta *link_sta)
{
	if (link_sta->he_cap.he_cap_elem.phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI)
		return true;

	return false;
}

static inline
bool rtw89_sta_link_has_er_su_4xhe08(struct ieee80211_link_sta *link_sta)
{
	if (link_sta->he_cap.he_cap_elem.phy_cap_info[8] &
	    IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI)
		return true;

	return false;
}

static inline struct rtw89_fw_suit *rtw89_fw_suit_get(struct rtw89_dev *rtwdev,
						      enum rtw89_fw_type type)
{
	struct rtw89_fw_info *fw_info = &rtwdev->fw;

	switch (type) {
	case RTW89_FW_WOWLAN:
		return &fw_info->wowlan;
	case RTW89_FW_LOGFMT:
		return &fw_info->log.suit;
	case RTW89_FW_BBMCU0:
		return &fw_info->bbmcu0;
	case RTW89_FW_BBMCU1:
		return &fw_info->bbmcu1;
	default:
		break;
	}

	return &fw_info->normal;
}

static inline struct sk_buff *rtw89_alloc_skb_for_rx(struct rtw89_dev *rtwdev,
						     unsigned int length)
{
	struct sk_buff *skb;

	if (rtwdev->hw->conf.flags & IEEE80211_CONF_MONITOR) {
		skb = dev_alloc_skb(length + RTW89_RADIOTAP_ROOM);
		if (!skb)
			return NULL;

		skb_reserve(skb, RTW89_RADIOTAP_ROOM);
		return skb;
	}

	return dev_alloc_skb(length);
}

static inline void rtw89_core_tx_wait_complete(struct rtw89_dev *rtwdev,
					       struct rtw89_tx_skb_data *skb_data,
					       bool tx_done)
{
	struct rtw89_tx_wait_info *wait;

	rcu_read_lock();

	wait = rcu_dereference(skb_data->wait);
	if (!wait)
		goto out;

	wait->tx_done = tx_done;
	complete(&wait->completion);

out:
	rcu_read_unlock();
}

static inline bool rtw89_is_mlo_1_1(struct rtw89_dev *rtwdev)
{
	switch (rtwdev->mlo_dbcc_mode) {
	case MLO_1_PLUS_1_1RF:
	case MLO_1_PLUS_1_2RF:
	case DBCC_LEGACY:
		return true;
	default:
		return false;
	}
}

static inline u8 rtw89_get_active_phy_bitmap(struct rtw89_dev *rtwdev)
{
	if (!rtwdev->dbcc_en)
		return BIT(RTW89_PHY_0);

	switch (rtwdev->mlo_dbcc_mode) {
	case MLO_0_PLUS_2_1RF:
	case MLO_0_PLUS_2_2RF:
		return BIT(RTW89_PHY_1);
	case MLO_1_PLUS_1_1RF:
	case MLO_1_PLUS_1_2RF:
	case MLO_2_PLUS_2_2RF:
	case DBCC_LEGACY:
		return BIT(RTW89_PHY_0) | BIT(RTW89_PHY_1);
	case MLO_2_PLUS_0_1RF:
	case MLO_2_PLUS_0_2RF:
	default:
		return BIT(RTW89_PHY_0);
	}
}

#define rtw89_for_each_active_bb(rtwdev, bb) \
	for (u8 __active_bb_bitmap = rtw89_get_active_phy_bitmap(rtwdev), \
	     __phy_idx = 0; __phy_idx < RTW89_PHY_NUM; __phy_idx++) \
		if (__active_bb_bitmap & BIT(__phy_idx) && \
		    (bb = &rtwdev->bbs[__phy_idx]))

#define rtw89_for_each_capab_bb(rtwdev, bb) \
	for (u8 __phy_idx_max = rtwdev->dbcc_en ? RTW89_PHY_1 : RTW89_PHY_0, \
	     __phy_idx = 0; __phy_idx <= __phy_idx_max; __phy_idx++) \
		if ((bb = &rtwdev->bbs[__phy_idx]))

static inline
struct rtw89_bb_ctx *rtw89_get_bb_ctx(struct rtw89_dev *rtwdev,
				      enum rtw89_phy_idx phy_idx)
{
	if (phy_idx >= RTW89_PHY_NUM)
		return &rtwdev->bbs[RTW89_PHY_0];

	return &rtwdev->bbs[phy_idx];
}

static inline bool rtw89_is_rtl885xb(struct rtw89_dev *rtwdev)
{
	enum rtw89_core_chip_id chip_id = rtwdev->chip->chip_id;

	if (chip_id == RTL8852B || chip_id == RTL8851B || chip_id == RTL8852BT)
		return true;

	return false;
}

int rtw89_core_tx_write(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, struct sk_buff *skb, int *qsel);
int rtw89_h2c_tx(struct rtw89_dev *rtwdev,
		 struct sk_buff *skb, bool fwdl);
void rtw89_core_tx_kick_off(struct rtw89_dev *rtwdev, u8 qsel);
int rtw89_core_tx_kick_off_and_wait(struct rtw89_dev *rtwdev, struct sk_buff *skb,
				    int qsel, unsigned int timeout);
void rtw89_core_fill_txdesc(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc);
void rtw89_core_fill_txdesc_v1(struct rtw89_dev *rtwdev,
			       struct rtw89_tx_desc_info *desc_info,
			       void *txdesc);
void rtw89_core_fill_txdesc_v2(struct rtw89_dev *rtwdev,
			       struct rtw89_tx_desc_info *desc_info,
			       void *txdesc);
void rtw89_core_fill_txdesc_fwcmd_v1(struct rtw89_dev *rtwdev,
				     struct rtw89_tx_desc_info *desc_info,
				     void *txdesc);
void rtw89_core_fill_txdesc_fwcmd_v2(struct rtw89_dev *rtwdev,
				     struct rtw89_tx_desc_info *desc_info,
				     void *txdesc);
void rtw89_core_rx(struct rtw89_dev *rtwdev,
		   struct rtw89_rx_desc_info *desc_info,
		   struct sk_buff *skb);
void rtw89_core_query_rxdesc(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset);
void rtw89_core_query_rxdesc_v2(struct rtw89_dev *rtwdev,
				struct rtw89_rx_desc_info *desc_info,
				u8 *data, u32 data_offset);
void rtw89_core_napi_start(struct rtw89_dev *rtwdev);
void rtw89_core_napi_stop(struct rtw89_dev *rtwdev);
int rtw89_core_napi_init(struct rtw89_dev *rtwdev);
void rtw89_core_napi_deinit(struct rtw89_dev *rtwdev);
int rtw89_core_sta_link_add(struct rtw89_dev *rtwdev,
			    struct rtw89_vif_link *rtwvif_link,
			    struct rtw89_sta_link *rtwsta_link);
int rtw89_core_sta_link_assoc(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      struct rtw89_sta_link *rtwsta_link);
int rtw89_core_sta_link_disassoc(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct rtw89_sta_link *rtwsta_link);
int rtw89_core_sta_link_disconnect(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link,
				   struct rtw89_sta_link *rtwsta_link);
int rtw89_core_sta_link_remove(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link,
			       struct rtw89_sta_link *rtwsta_link);
void rtw89_core_set_tid_config(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta *sta,
			       struct cfg80211_tid_config *tid_config);
void rtw89_core_rfkill_poll(struct rtw89_dev *rtwdev, bool force);
void rtw89_check_quirks(struct rtw89_dev *rtwdev, const struct dmi_system_id *quirks);
int rtw89_core_init(struct rtw89_dev *rtwdev);
void rtw89_core_deinit(struct rtw89_dev *rtwdev);
int rtw89_core_register(struct rtw89_dev *rtwdev);
void rtw89_core_unregister(struct rtw89_dev *rtwdev);
struct rtw89_dev *rtw89_alloc_ieee80211_hw(struct device *device,
					   u32 bus_data_size,
					   const struct rtw89_chip_info *chip,
					   const struct rtw89_chip_variant *variant);
void rtw89_free_ieee80211_hw(struct rtw89_dev *rtwdev);
u8 rtw89_acquire_mac_id(struct rtw89_dev *rtwdev);
void rtw89_release_mac_id(struct rtw89_dev *rtwdev, u8 mac_id);
void rtw89_init_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		    u8 mac_id, u8 port);
void rtw89_init_sta(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		    struct rtw89_sta *rtwsta, u8 mac_id);
struct rtw89_vif_link *rtw89_vif_set_link(struct rtw89_vif *rtwvif,
					  unsigned int link_id);
void rtw89_vif_unset_link(struct rtw89_vif *rtwvif, unsigned int link_id);
struct rtw89_sta_link *rtw89_sta_set_link(struct rtw89_sta *rtwsta,
					  unsigned int link_id);
void rtw89_sta_unset_link(struct rtw89_sta *rtwsta, unsigned int link_id);
void rtw89_core_set_chip_txpwr(struct rtw89_dev *rtwdev);
const struct rtw89_6ghz_span *
rtw89_get_6ghz_span(struct rtw89_dev *rtwdev, u32 center_freq);
void rtw89_get_default_chandef(struct cfg80211_chan_def *chandef);
void rtw89_get_channel_params(const struct cfg80211_chan_def *chandef,
			      struct rtw89_chan *chan);
int rtw89_set_channel(struct rtw89_dev *rtwdev);
u8 rtw89_core_acquire_bit_map(unsigned long *addr, unsigned long size);
void rtw89_core_release_bit_map(unsigned long *addr, u8 bit);
void rtw89_core_release_all_bits_map(unsigned long *addr, unsigned int nbits);
int rtw89_core_acquire_sta_ba_entry(struct rtw89_dev *rtwdev,
				    struct rtw89_sta_link *rtwsta_link, u8 tid,
				    u8 *cam_idx);
int rtw89_core_release_sta_ba_entry(struct rtw89_dev *rtwdev,
				    struct rtw89_sta_link *rtwsta_link, u8 tid,
				    u8 *cam_idx);
void rtw89_core_free_sta_pending_ba(struct rtw89_dev *rtwdev,
				    struct ieee80211_sta *sta);
void rtw89_core_free_sta_pending_forbid_ba(struct rtw89_dev *rtwdev,
					   struct ieee80211_sta *sta);
void rtw89_core_free_sta_pending_roc_tx(struct rtw89_dev *rtwdev,
					struct ieee80211_sta *sta);
void rtw89_vif_type_mapping(struct rtw89_vif_link *rtwvif_link, bool assoc);
int rtw89_chip_info_setup(struct rtw89_dev *rtwdev);
void rtw89_chip_cfg_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				       struct rtw89_vif_link *rtwvif_link);
bool rtw89_ra_report_to_bitrate(struct rtw89_dev *rtwdev, u8 rpt_rate, u16 *bitrate);
int rtw89_regd_setup(struct rtw89_dev *rtwdev);
int rtw89_regd_init_hint(struct rtw89_dev *rtwdev);
const char *rtw89_regd_get_string(enum rtw89_regulation_type regd);
void rtw89_traffic_stats_init(struct rtw89_dev *rtwdev,
			      struct rtw89_traffic_stats *stats);
int rtw89_wait_for_cond(struct rtw89_wait_info *wait, unsigned int cond);
void rtw89_complete_cond(struct rtw89_wait_info *wait, unsigned int cond,
			 const struct rtw89_completion_data *data);
int rtw89_core_start(struct rtw89_dev *rtwdev);
void rtw89_core_stop(struct rtw89_dev *rtwdev);
void rtw89_core_update_beacon_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_core_csa_beacon_work(struct wiphy *wiphy, struct wiphy_work *work);
int rtw89_core_send_nullfunc(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			     bool qos, bool ps, int timeout);
void rtw89_roc_work(struct wiphy *wiphy, struct wiphy_work *work);
void rtw89_roc_start(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
void rtw89_roc_end(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif);
void rtw89_core_scan_start(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			   const u8 *mac_addr, bool hw_scan);
void rtw89_core_scan_complete(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link, bool hw_scan);
int rtw89_reg_6ghz_recalc(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			  bool active);
void rtw89_core_update_p2p_ps(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      struct ieee80211_bss_conf *bss_conf);
void rtw89_core_ntfy_btc_event(struct rtw89_dev *rtwdev, enum rtw89_btc_hmsg event);
int rtw89_core_mlsr_switch(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
			   unsigned int link_id);

#endif
