/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019  Realtek Corporation
 */

#ifndef __RTK_MAIN_H_
#define __RTK_MAIN_H_

#include <net/mac80211.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <linux/average.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include "util.h"

#define RTW_NAPI_WEIGHT_NUM		64
#define RTW_MAX_MAC_ID_NUM		32
#define RTW_MAX_SEC_CAM_NUM		32
#define MAX_PG_CAM_BACKUP_NUM		8

#define RTW_SCAN_MAX_SSIDS		4
#define RTW_SCAN_MAX_IE_LEN		128

#define RTW_MAX_PATTERN_NUM		12
#define RTW_MAX_PATTERN_MASK_SIZE	16
#define RTW_MAX_PATTERN_SIZE		128

#define RTW_WATCH_DOG_DELAY_TIME	round_jiffies_relative(HZ * 2)

#define RFREG_MASK			0xfffff
#define INV_RF_DATA			0xffffffff
#define TX_PAGE_SIZE_SHIFT		7

#define RTW_CHANNEL_WIDTH_MAX		3
#define RTW_RF_PATH_MAX			4
#define HW_FEATURE_LEN			13

#define RTW_TP_SHIFT			18 /* bytes/2s --> Mbps */

extern bool rtw_bf_support;
extern bool rtw_disable_lps_deep_mode;
extern unsigned int rtw_debug_mask;
extern bool rtw_edcca_enabled;
extern const struct ieee80211_ops rtw_ops;

#define RTW_MAX_CHANNEL_NUM_2G 14
#define RTW_MAX_CHANNEL_NUM_5G 49

struct rtw_dev;

enum rtw_hci_type {
	RTW_HCI_TYPE_PCIE,
	RTW_HCI_TYPE_USB,
	RTW_HCI_TYPE_SDIO,

	RTW_HCI_TYPE_UNDEFINE,
};

struct rtw_hci {
	struct rtw_hci_ops *ops;
	enum rtw_hci_type type;

	u32 rpwm_addr;
	u32 cpwm_addr;

	u8 bulkout_num;
};

#define IS_CH_5G_BAND_1(channel) ((channel) >= 36 && (channel <= 48))
#define IS_CH_5G_BAND_2(channel) ((channel) >= 52 && (channel <= 64))
#define IS_CH_5G_BAND_3(channel) ((channel) >= 100 && (channel <= 144))
#define IS_CH_5G_BAND_4(channel) ((channel) >= 149 && (channel <= 177))

#define IS_CH_5G_BAND_MID(channel) \
	(IS_CH_5G_BAND_2(channel) || IS_CH_5G_BAND_3(channel))

#define IS_CH_2G_BAND(channel) ((channel) <= 14)
#define IS_CH_5G_BAND(channel) \
	(IS_CH_5G_BAND_1(channel) || IS_CH_5G_BAND_2(channel) || \
	 IS_CH_5G_BAND_3(channel) || IS_CH_5G_BAND_4(channel))

enum rtw_supported_band {
	RTW_BAND_2G = BIT(NL80211_BAND_2GHZ),
	RTW_BAND_5G = BIT(NL80211_BAND_5GHZ),
	RTW_BAND_60G = BIT(NL80211_BAND_60GHZ),
};

/* now, support upto 80M bw */
#define RTW_MAX_CHANNEL_WIDTH RTW_CHANNEL_WIDTH_80

enum rtw_bandwidth {
	RTW_CHANNEL_WIDTH_20	= 0,
	RTW_CHANNEL_WIDTH_40	= 1,
	RTW_CHANNEL_WIDTH_80	= 2,
	RTW_CHANNEL_WIDTH_160	= 3,
	RTW_CHANNEL_WIDTH_80_80	= 4,
	RTW_CHANNEL_WIDTH_5	= 5,
	RTW_CHANNEL_WIDTH_10	= 6,
};

enum rtw_sc_offset {
	RTW_SC_DONT_CARE	= 0,
	RTW_SC_20_UPPER		= 1,
	RTW_SC_20_LOWER		= 2,
	RTW_SC_20_UPMOST	= 3,
	RTW_SC_20_LOWEST	= 4,
	RTW_SC_40_UPPER		= 9,
	RTW_SC_40_LOWER		= 10,
};

enum rtw_net_type {
	RTW_NET_NO_LINK		= 0,
	RTW_NET_AD_HOC		= 1,
	RTW_NET_MGD_LINKED	= 2,
	RTW_NET_AP_MODE		= 3,
};

enum rtw_rf_type {
	RF_1T1R			= 0,
	RF_1T2R			= 1,
	RF_2T2R			= 2,
	RF_2T3R			= 3,
	RF_2T4R			= 4,
	RF_3T3R			= 5,
	RF_3T4R			= 6,
	RF_4T4R			= 7,
	RF_TYPE_MAX,
};

enum rtw_rf_path {
	RF_PATH_A = 0,
	RF_PATH_B = 1,
	RF_PATH_C = 2,
	RF_PATH_D = 3,
};

enum rtw_bb_path {
	BB_PATH_A = BIT(0),
	BB_PATH_B = BIT(1),
	BB_PATH_C = BIT(2),
	BB_PATH_D = BIT(3),

	BB_PATH_AB = (BB_PATH_A | BB_PATH_B),
	BB_PATH_AC = (BB_PATH_A | BB_PATH_C),
	BB_PATH_AD = (BB_PATH_A | BB_PATH_D),
	BB_PATH_BC = (BB_PATH_B | BB_PATH_C),
	BB_PATH_BD = (BB_PATH_B | BB_PATH_D),
	BB_PATH_CD = (BB_PATH_C | BB_PATH_D),

	BB_PATH_ABC = (BB_PATH_A | BB_PATH_B | BB_PATH_C),
	BB_PATH_ABD = (BB_PATH_A | BB_PATH_B | BB_PATH_D),
	BB_PATH_ACD = (BB_PATH_A | BB_PATH_C | BB_PATH_D),
	BB_PATH_BCD = (BB_PATH_B | BB_PATH_C | BB_PATH_D),

	BB_PATH_ABCD = (BB_PATH_A | BB_PATH_B | BB_PATH_C | BB_PATH_D),
};

enum rtw_rate_section {
	RTW_RATE_SECTION_CCK = 0,
	RTW_RATE_SECTION_OFDM,
	RTW_RATE_SECTION_HT_1S,
	RTW_RATE_SECTION_HT_2S,
	RTW_RATE_SECTION_VHT_1S,
	RTW_RATE_SECTION_VHT_2S,

	/* keep last */
	RTW_RATE_SECTION_MAX,
};

enum rtw_wireless_set {
	WIRELESS_CCK	= 0x00000001,
	WIRELESS_OFDM	= 0x00000002,
	WIRELESS_HT	= 0x00000004,
	WIRELESS_VHT	= 0x00000008,
};

#define HT_STBC_EN	BIT(0)
#define VHT_STBC_EN	BIT(1)
#define HT_LDPC_EN	BIT(0)
#define VHT_LDPC_EN	BIT(1)

enum rtw_chip_type {
	RTW_CHIP_TYPE_8822B,
	RTW_CHIP_TYPE_8822C,
	RTW_CHIP_TYPE_8723D,
	RTW_CHIP_TYPE_8821C,
};

enum rtw_tx_queue_type {
	/* the order of AC queues matters */
	RTW_TX_QUEUE_BK = 0x0,
	RTW_TX_QUEUE_BE = 0x1,
	RTW_TX_QUEUE_VI = 0x2,
	RTW_TX_QUEUE_VO = 0x3,

	RTW_TX_QUEUE_BCN = 0x4,
	RTW_TX_QUEUE_MGMT = 0x5,
	RTW_TX_QUEUE_HI0 = 0x6,
	RTW_TX_QUEUE_H2C = 0x7,
	/* keep it last */
	RTK_MAX_TX_QUEUE_NUM
};

enum rtw_rx_queue_type {
	RTW_RX_QUEUE_MPDU = 0x0,
	RTW_RX_QUEUE_C2H = 0x1,
	/* keep it last */
	RTK_MAX_RX_QUEUE_NUM
};

enum rtw_fw_type {
	RTW_NORMAL_FW = 0x0,
	RTW_WOWLAN_FW = 0x1,
};

enum rtw_rate_index {
	RTW_RATEID_BGN_40M_2SS	= 0,
	RTW_RATEID_BGN_40M_1SS	= 1,
	RTW_RATEID_BGN_20M_2SS	= 2,
	RTW_RATEID_BGN_20M_1SS	= 3,
	RTW_RATEID_GN_N2SS	= 4,
	RTW_RATEID_GN_N1SS	= 5,
	RTW_RATEID_BG		= 6,
	RTW_RATEID_G		= 7,
	RTW_RATEID_B_20M	= 8,
	RTW_RATEID_ARFR0_AC_2SS	= 9,
	RTW_RATEID_ARFR1_AC_1SS	= 10,
	RTW_RATEID_ARFR2_AC_2G_1SS = 11,
	RTW_RATEID_ARFR3_AC_2G_2SS = 12,
	RTW_RATEID_ARFR4_AC_3SS	= 13,
	RTW_RATEID_ARFR5_N_3SS	= 14,
	RTW_RATEID_ARFR7_N_4SS	= 15,
	RTW_RATEID_ARFR6_AC_4SS	= 16
};

enum rtw_trx_desc_rate {
	DESC_RATE1M	= 0x00,
	DESC_RATE2M	= 0x01,
	DESC_RATE5_5M	= 0x02,
	DESC_RATE11M	= 0x03,

	DESC_RATE6M	= 0x04,
	DESC_RATE9M	= 0x05,
	DESC_RATE12M	= 0x06,
	DESC_RATE18M	= 0x07,
	DESC_RATE24M	= 0x08,
	DESC_RATE36M	= 0x09,
	DESC_RATE48M	= 0x0a,
	DESC_RATE54M	= 0x0b,

	DESC_RATEMCS0	= 0x0c,
	DESC_RATEMCS1	= 0x0d,
	DESC_RATEMCS2	= 0x0e,
	DESC_RATEMCS3	= 0x0f,
	DESC_RATEMCS4	= 0x10,
	DESC_RATEMCS5	= 0x11,
	DESC_RATEMCS6	= 0x12,
	DESC_RATEMCS7	= 0x13,
	DESC_RATEMCS8	= 0x14,
	DESC_RATEMCS9	= 0x15,
	DESC_RATEMCS10	= 0x16,
	DESC_RATEMCS11	= 0x17,
	DESC_RATEMCS12	= 0x18,
	DESC_RATEMCS13	= 0x19,
	DESC_RATEMCS14	= 0x1a,
	DESC_RATEMCS15	= 0x1b,
	DESC_RATEMCS16	= 0x1c,
	DESC_RATEMCS17	= 0x1d,
	DESC_RATEMCS18	= 0x1e,
	DESC_RATEMCS19	= 0x1f,
	DESC_RATEMCS20	= 0x20,
	DESC_RATEMCS21	= 0x21,
	DESC_RATEMCS22	= 0x22,
	DESC_RATEMCS23	= 0x23,
	DESC_RATEMCS24	= 0x24,
	DESC_RATEMCS25	= 0x25,
	DESC_RATEMCS26	= 0x26,
	DESC_RATEMCS27	= 0x27,
	DESC_RATEMCS28	= 0x28,
	DESC_RATEMCS29	= 0x29,
	DESC_RATEMCS30	= 0x2a,
	DESC_RATEMCS31	= 0x2b,

	DESC_RATEVHT1SS_MCS0	= 0x2c,
	DESC_RATEVHT1SS_MCS1	= 0x2d,
	DESC_RATEVHT1SS_MCS2	= 0x2e,
	DESC_RATEVHT1SS_MCS3	= 0x2f,
	DESC_RATEVHT1SS_MCS4	= 0x30,
	DESC_RATEVHT1SS_MCS5	= 0x31,
	DESC_RATEVHT1SS_MCS6	= 0x32,
	DESC_RATEVHT1SS_MCS7	= 0x33,
	DESC_RATEVHT1SS_MCS8	= 0x34,
	DESC_RATEVHT1SS_MCS9	= 0x35,

	DESC_RATEVHT2SS_MCS0	= 0x36,
	DESC_RATEVHT2SS_MCS1	= 0x37,
	DESC_RATEVHT2SS_MCS2	= 0x38,
	DESC_RATEVHT2SS_MCS3	= 0x39,
	DESC_RATEVHT2SS_MCS4	= 0x3a,
	DESC_RATEVHT2SS_MCS5	= 0x3b,
	DESC_RATEVHT2SS_MCS6	= 0x3c,
	DESC_RATEVHT2SS_MCS7	= 0x3d,
	DESC_RATEVHT2SS_MCS8	= 0x3e,
	DESC_RATEVHT2SS_MCS9	= 0x3f,

	DESC_RATEVHT3SS_MCS0	= 0x40,
	DESC_RATEVHT3SS_MCS1	= 0x41,
	DESC_RATEVHT3SS_MCS2	= 0x42,
	DESC_RATEVHT3SS_MCS3	= 0x43,
	DESC_RATEVHT3SS_MCS4	= 0x44,
	DESC_RATEVHT3SS_MCS5	= 0x45,
	DESC_RATEVHT3SS_MCS6	= 0x46,
	DESC_RATEVHT3SS_MCS7	= 0x47,
	DESC_RATEVHT3SS_MCS8	= 0x48,
	DESC_RATEVHT3SS_MCS9	= 0x49,

	DESC_RATEVHT4SS_MCS0	= 0x4a,
	DESC_RATEVHT4SS_MCS1	= 0x4b,
	DESC_RATEVHT4SS_MCS2	= 0x4c,
	DESC_RATEVHT4SS_MCS3	= 0x4d,
	DESC_RATEVHT4SS_MCS4	= 0x4e,
	DESC_RATEVHT4SS_MCS5	= 0x4f,
	DESC_RATEVHT4SS_MCS6	= 0x50,
	DESC_RATEVHT4SS_MCS7	= 0x51,
	DESC_RATEVHT4SS_MCS8	= 0x52,
	DESC_RATEVHT4SS_MCS9	= 0x53,

	DESC_RATE_MAX,
};

enum rtw_regulatory_domains {
	RTW_REGD_FCC		= 0,
	RTW_REGD_MKK		= 1,
	RTW_REGD_ETSI		= 2,
	RTW_REGD_IC		= 3,
	RTW_REGD_KCC		= 4,
	RTW_REGD_ACMA		= 5,
	RTW_REGD_CHILE		= 6,
	RTW_REGD_UKRAINE	= 7,
	RTW_REGD_MEXICO		= 8,
	RTW_REGD_CN		= 9,
	RTW_REGD_WW,

	RTW_REGD_MAX
};

enum rtw_txq_flags {
	RTW_TXQ_AMPDU,
	RTW_TXQ_BLOCK_BA,
};

enum rtw_flags {
	RTW_FLAG_RUNNING,
	RTW_FLAG_FW_RUNNING,
	RTW_FLAG_SCANNING,
	RTW_FLAG_INACTIVE_PS,
	RTW_FLAG_LEISURE_PS,
	RTW_FLAG_LEISURE_PS_DEEP,
	RTW_FLAG_DIG_DISABLE,
	RTW_FLAG_BUSY_TRAFFIC,
	RTW_FLAG_WOWLAN,
	RTW_FLAG_RESTARTING,
	RTW_FLAG_RESTART_TRIGGERING,
	RTW_FLAG_FORCE_LOWEST_RATE,

	NUM_OF_RTW_FLAGS,
};

enum rtw_evm {
	RTW_EVM_OFDM = 0,
	RTW_EVM_1SS,
	RTW_EVM_2SS_A,
	RTW_EVM_2SS_B,
	/* keep it last */
	RTW_EVM_NUM
};

enum rtw_snr {
	RTW_SNR_OFDM_A = 0,
	RTW_SNR_OFDM_B,
	RTW_SNR_OFDM_C,
	RTW_SNR_OFDM_D,
	RTW_SNR_1SS_A,
	RTW_SNR_1SS_B,
	RTW_SNR_1SS_C,
	RTW_SNR_1SS_D,
	RTW_SNR_2SS_A,
	RTW_SNR_2SS_B,
	RTW_SNR_2SS_C,
	RTW_SNR_2SS_D,
	/* keep it last */
	RTW_SNR_NUM
};

enum rtw_wow_flags {
	RTW_WOW_FLAG_EN_MAGIC_PKT,
	RTW_WOW_FLAG_EN_REKEY_PKT,
	RTW_WOW_FLAG_EN_DISCONNECT,

	/* keep it last */
	RTW_WOW_FLAG_MAX,
};

/* the power index is represented by differences, which cck-1s & ht40-1s are
 * the base values, so for 1s's differences, there are only ht20 & ofdm
 */
struct rtw_2g_1s_pwr_idx_diff {
#ifdef __LITTLE_ENDIAN
	s8 ofdm:4;
	s8 bw20:4;
#else
	s8 bw20:4;
	s8 ofdm:4;
#endif
} __packed;

struct rtw_2g_ns_pwr_idx_diff {
#ifdef __LITTLE_ENDIAN
	s8 bw20:4;
	s8 bw40:4;
	s8 cck:4;
	s8 ofdm:4;
#else
	s8 ofdm:4;
	s8 cck:4;
	s8 bw40:4;
	s8 bw20:4;
#endif
} __packed;

struct rtw_2g_txpwr_idx {
	u8 cck_base[6];
	u8 bw40_base[5];
	struct rtw_2g_1s_pwr_idx_diff ht_1s_diff;
	struct rtw_2g_ns_pwr_idx_diff ht_2s_diff;
	struct rtw_2g_ns_pwr_idx_diff ht_3s_diff;
	struct rtw_2g_ns_pwr_idx_diff ht_4s_diff;
};

struct rtw_5g_ht_1s_pwr_idx_diff {
#ifdef __LITTLE_ENDIAN
	s8 ofdm:4;
	s8 bw20:4;
#else
	s8 bw20:4;
	s8 ofdm:4;
#endif
} __packed;

struct rtw_5g_ht_ns_pwr_idx_diff {
#ifdef __LITTLE_ENDIAN
	s8 bw20:4;
	s8 bw40:4;
#else
	s8 bw40:4;
	s8 bw20:4;
#endif
} __packed;

struct rtw_5g_ofdm_ns_pwr_idx_diff {
#ifdef __LITTLE_ENDIAN
	s8 ofdm_3s:4;
	s8 ofdm_2s:4;
	s8 ofdm_4s:4;
	s8 res:4;
#else
	s8 res:4;
	s8 ofdm_4s:4;
	s8 ofdm_2s:4;
	s8 ofdm_3s:4;
#endif
} __packed;

struct rtw_5g_vht_ns_pwr_idx_diff {
#ifdef __LITTLE_ENDIAN
	s8 bw160:4;
	s8 bw80:4;
#else
	s8 bw80:4;
	s8 bw160:4;
#endif
} __packed;

struct rtw_5g_txpwr_idx {
	u8 bw40_base[14];
	struct rtw_5g_ht_1s_pwr_idx_diff ht_1s_diff;
	struct rtw_5g_ht_ns_pwr_idx_diff ht_2s_diff;
	struct rtw_5g_ht_ns_pwr_idx_diff ht_3s_diff;
	struct rtw_5g_ht_ns_pwr_idx_diff ht_4s_diff;
	struct rtw_5g_ofdm_ns_pwr_idx_diff ofdm_diff;
	struct rtw_5g_vht_ns_pwr_idx_diff vht_1s_diff;
	struct rtw_5g_vht_ns_pwr_idx_diff vht_2s_diff;
	struct rtw_5g_vht_ns_pwr_idx_diff vht_3s_diff;
	struct rtw_5g_vht_ns_pwr_idx_diff vht_4s_diff;
};

struct rtw_txpwr_idx {
	struct rtw_2g_txpwr_idx pwr_idx_2g;
	struct rtw_5g_txpwr_idx pwr_idx_5g;
};

struct rtw_timer_list {
	struct timer_list timer;
	void (*function)(void *data);
	void *args;
};

struct rtw_channel_params {
	u8 center_chan;
	u8 bandwidth;
	u8 primary_chan_idx;
	/* center channel by different available bandwidth,
	 * val of (bw > current bandwidth) is invalid
	 */
	u8 cch_by_bw[RTW_MAX_CHANNEL_WIDTH + 1];
};

struct rtw_hw_reg {
	u32 addr;
	u32 mask;
};

struct rtw_ltecoex_addr {
	u32 ctrl;
	u32 wdata;
	u32 rdata;
};

struct rtw_reg_domain {
	u32 addr;
	u32 mask;
#define RTW_REG_DOMAIN_MAC32	0
#define RTW_REG_DOMAIN_MAC16	1
#define RTW_REG_DOMAIN_MAC8	2
#define RTW_REG_DOMAIN_RF_A	3
#define RTW_REG_DOMAIN_RF_B	4
#define RTW_REG_DOMAIN_NL	0xFF
	u8 domain;
};

struct rtw_rf_sipi_addr {
	u32 hssi_1;
	u32 hssi_2;
	u32 lssi_read;
	u32 lssi_read_pi;
};

struct rtw_hw_reg_offset {
	struct rtw_hw_reg hw_reg;
	u8 offset;
};

struct rtw_backup_info {
	u8 len;
	u32 reg;
	u32 val;
};

enum rtw_vif_port_set {
	PORT_SET_MAC_ADDR	= BIT(0),
	PORT_SET_BSSID		= BIT(1),
	PORT_SET_NET_TYPE	= BIT(2),
	PORT_SET_AID		= BIT(3),
	PORT_SET_BCN_CTRL	= BIT(4),
};

struct rtw_vif_port {
	struct rtw_hw_reg mac_addr;
	struct rtw_hw_reg bssid;
	struct rtw_hw_reg net_type;
	struct rtw_hw_reg aid;
	struct rtw_hw_reg bcn_ctrl;
};

struct rtw_tx_pkt_info {
	u32 tx_pkt_size;
	u8 offset;
	u8 pkt_offset;
	u8 mac_id;
	u8 rate_id;
	u8 rate;
	u8 qsel;
	u8 bw;
	u8 sec_type;
	u8 sn;
	bool ampdu_en;
	u8 ampdu_factor;
	u8 ampdu_density;
	u16 seq;
	bool stbc;
	bool ldpc;
	bool dis_rate_fallback;
	bool bmc;
	bool use_rate;
	bool ls;
	bool fs;
	bool short_gi;
	bool report;
	bool rts;
	bool dis_qselseq;
	bool en_hwseq;
	u8 hw_ssn_sel;
	bool nav_use_hdr;
	bool bt_null;
};

struct rtw_rx_pkt_stat {
	bool phy_status;
	bool icv_err;
	bool crc_err;
	bool decrypted;
	bool is_c2h;

	s32 signal_power;
	u16 pkt_len;
	u8 bw;
	u8 drv_info_sz;
	u8 shift;
	u8 rate;
	u8 mac_id;
	u8 cam_id;
	u8 ppdu_cnt;
	u32 tsf_low;
	s8 rx_power[RTW_RF_PATH_MAX];
	u8 rssi;
	u8 rxsc;
	s8 rx_snr[RTW_RF_PATH_MAX];
	u8 rx_evm[RTW_RF_PATH_MAX];
	s8 cfo_tail[RTW_RF_PATH_MAX];
	u16 freq;
	u8 band;

	struct rtw_sta_info *si;
	struct ieee80211_vif *vif;
	struct ieee80211_hdr *hdr;
};

DECLARE_EWMA(tp, 10, 2);

struct rtw_traffic_stats {
	/* units in bytes */
	u64 tx_unicast;
	u64 rx_unicast;

	/* count for packets */
	u64 tx_cnt;
	u64 rx_cnt;

	/* units in Mbps */
	u32 tx_throughput;
	u32 rx_throughput;
	struct ewma_tp tx_ewma_tp;
	struct ewma_tp rx_ewma_tp;
};

enum rtw_lps_mode {
	RTW_MODE_ACTIVE	= 0,
	RTW_MODE_LPS	= 1,
	RTW_MODE_WMM_PS	= 2,
};

enum rtw_lps_deep_mode {
	LPS_DEEP_MODE_NONE	= 0,
	LPS_DEEP_MODE_LCLK	= 1,
	LPS_DEEP_MODE_PG	= 2,
};

enum rtw_pwr_state {
	RTW_RF_OFF	= 0x0,
	RTW_RF_ON	= 0x4,
	RTW_ALL_ON	= 0xc,
};

struct rtw_lps_conf {
	enum rtw_lps_mode mode;
	enum rtw_lps_deep_mode deep_mode;
	enum rtw_lps_deep_mode wow_deep_mode;
	enum rtw_pwr_state state;
	u8 awake_interval;
	u8 rlbm;
	u8 smart_ps;
	u8 port_id;
	bool sec_cam_backup;
	bool pattern_cam_backup;
};

enum rtw_hw_key_type {
	RTW_CAM_NONE	= 0,
	RTW_CAM_WEP40	= 1,
	RTW_CAM_TKIP	= 2,
	RTW_CAM_AES	= 4,
	RTW_CAM_WEP104	= 5,
};

struct rtw_cam_entry {
	bool valid;
	bool group;
	u8 addr[ETH_ALEN];
	u8 hw_key_type;
	struct ieee80211_key_conf *key;
};

struct rtw_sec_desc {
	/* search strategy */
	bool default_key_search;

	u32 total_cam_num;
	struct rtw_cam_entry cam_table[RTW_MAX_SEC_CAM_NUM];
	DECLARE_BITMAP(cam_map, RTW_MAX_SEC_CAM_NUM);
};

struct rtw_tx_report {
	/* protect the tx report queue */
	spinlock_t q_lock;
	struct sk_buff_head queue;
	atomic_t sn;
	struct timer_list purge_timer;
};

struct rtw_ra_report {
	struct rate_info txrate;
	u32 bit_rate;
	u8 desc_rate;
};

struct rtw_txq {
	struct list_head list;

	unsigned long flags;
	unsigned long last_push;
};

#define RTW_BC_MC_MACID 1
DECLARE_EWMA(rssi, 10, 16);

struct rtw_sta_info {
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;

	struct ewma_rssi avg_rssi;
	u8 rssi_level;

	u8 mac_id;
	u8 rate_id;
	enum rtw_bandwidth bw_mode;
	enum rtw_rf_type rf_type;
	enum rtw_wireless_set wireless_set;
	u8 stbc_en:2;
	u8 ldpc_en:2;
	bool sgi_enable;
	bool vht_enable;
	bool updated;
	u8 init_ra_lv;
	u64 ra_mask;

	DECLARE_BITMAP(tid_ba, IEEE80211_NUM_TIDS);

	struct rtw_ra_report ra_report;

	bool use_cfg_mask;
	struct cfg80211_bitrate_mask *mask;
};

enum rtw_bfee_role {
	RTW_BFEE_NONE,
	RTW_BFEE_SU,
	RTW_BFEE_MU
};

struct rtw_bfee {
	enum rtw_bfee_role role;

	u16 p_aid;
	u8 g_id;
	u8 mac_addr[ETH_ALEN];
	u8 sound_dim;

	/* SU-MIMO */
	u8 su_reg_index;

	/* MU-MIMO */
	u16 aid;
};

struct rtw_bf_info {
	u8 bfer_mu_cnt;
	u8 bfer_su_cnt;
	DECLARE_BITMAP(bfer_su_reg_maping, 2);
	u8 cur_csi_rpt_rate;
};

struct rtw_vif {
	enum rtw_net_type net_type;
	u16 aid;
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 port;
	u8 bcn_ctrl;
	struct list_head rsvd_page_list;
	struct ieee80211_tx_queue_params tx_params[IEEE80211_NUM_ACS];
	const struct rtw_vif_port *conf;
	struct cfg80211_scan_request *scan_req;
	struct ieee80211_scan_ies *scan_ies;

	struct rtw_traffic_stats stats;

	struct rtw_bfee bfee;
};

struct rtw_regulatory {
	char alpha2[2];
	u8 txpwr_regd_2g;
	u8 txpwr_regd_5g;
};

enum rtw_regd_state {
	RTW_REGD_STATE_WORLDWIDE,
	RTW_REGD_STATE_PROGRAMMED,
	RTW_REGD_STATE_SETTING,

	RTW_REGD_STATE_NR,
};

struct rtw_regd {
	enum rtw_regd_state state;
	const struct rtw_regulatory *regulatory;
	enum nl80211_dfs_regions dfs_region;
};

struct rtw_chip_ops {
	int (*mac_init)(struct rtw_dev *rtwdev);
	int (*dump_fw_crash)(struct rtw_dev *rtwdev);
	void (*shutdown)(struct rtw_dev *rtwdev);
	int (*read_efuse)(struct rtw_dev *rtwdev, u8 *map);
	void (*phy_set_param)(struct rtw_dev *rtwdev);
	void (*set_channel)(struct rtw_dev *rtwdev, u8 channel,
			    u8 bandwidth, u8 primary_chan_idx);
	void (*query_rx_desc)(struct rtw_dev *rtwdev, u8 *rx_desc,
			      struct rtw_rx_pkt_stat *pkt_stat,
			      struct ieee80211_rx_status *rx_status);
	u32 (*read_rf)(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
		       u32 addr, u32 mask);
	bool (*write_rf)(struct rtw_dev *rtwdev, enum rtw_rf_path rf_path,
			 u32 addr, u32 mask, u32 data);
	void (*set_tx_power_index)(struct rtw_dev *rtwdev);
	int (*rsvd_page_dump)(struct rtw_dev *rtwdev, u8 *buf, u32 offset,
			      u32 size);
	int (*set_antenna)(struct rtw_dev *rtwdev,
			   u32 antenna_tx,
			   u32 antenna_rx);
	void (*cfg_ldo25)(struct rtw_dev *rtwdev, bool enable);
	void (*efuse_grant)(struct rtw_dev *rtwdev, bool enable);
	void (*false_alarm_statistics)(struct rtw_dev *rtwdev);
	void (*phy_calibration)(struct rtw_dev *rtwdev);
	void (*dpk_track)(struct rtw_dev *rtwdev);
	void (*cck_pd_set)(struct rtw_dev *rtwdev, u8 level);
	void (*pwr_track)(struct rtw_dev *rtwdev);
	void (*config_bfee)(struct rtw_dev *rtwdev, struct rtw_vif *vif,
			    struct rtw_bfee *bfee, bool enable);
	void (*set_gid_table)(struct rtw_dev *rtwdev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *conf);
	void (*cfg_csi_rate)(struct rtw_dev *rtwdev, u8 rssi, u8 cur_rate,
			     u8 fixrate_en, u8 *new_rate);
	void (*adaptivity_init)(struct rtw_dev *rtwdev);
	void (*adaptivity)(struct rtw_dev *rtwdev);
	void (*cfo_init)(struct rtw_dev *rtwdev);
	void (*cfo_track)(struct rtw_dev *rtwdev);
	void (*config_tx_path)(struct rtw_dev *rtwdev, u8 tx_path,
			       enum rtw_bb_path tx_path_1ss,
			       enum rtw_bb_path tx_path_cck,
			       bool is_tx2_path);

	/* for coex */
	void (*coex_set_init)(struct rtw_dev *rtwdev);
	void (*coex_set_ant_switch)(struct rtw_dev *rtwdev,
				    u8 ctrl_type, u8 pos_type);
	void (*coex_set_gnt_fix)(struct rtw_dev *rtwdev);
	void (*coex_set_gnt_debug)(struct rtw_dev *rtwdev);
	void (*coex_set_rfe_type)(struct rtw_dev *rtwdev);
	void (*coex_set_wl_tx_power)(struct rtw_dev *rtwdev, u8 wl_pwr);
	void (*coex_set_wl_rx_gain)(struct rtw_dev *rtwdev, bool low_gain);
};

#define RTW_PWR_POLLING_CNT	20000

#define RTW_PWR_CMD_READ	0x00
#define RTW_PWR_CMD_WRITE	0x01
#define RTW_PWR_CMD_POLLING	0x02
#define RTW_PWR_CMD_DELAY	0x03
#define RTW_PWR_CMD_END		0x04

/* define the base address of each block */
#define RTW_PWR_ADDR_MAC	0x00
#define RTW_PWR_ADDR_USB	0x01
#define RTW_PWR_ADDR_PCIE	0x02
#define RTW_PWR_ADDR_SDIO	0x03

#define RTW_PWR_INTF_SDIO_MSK	BIT(0)
#define RTW_PWR_INTF_USB_MSK	BIT(1)
#define RTW_PWR_INTF_PCI_MSK	BIT(2)
#define RTW_PWR_INTF_ALL_MSK	(BIT(0) | BIT(1) | BIT(2) | BIT(3))

#define RTW_PWR_CUT_TEST_MSK	BIT(0)
#define RTW_PWR_CUT_A_MSK	BIT(1)
#define RTW_PWR_CUT_B_MSK	BIT(2)
#define RTW_PWR_CUT_C_MSK	BIT(3)
#define RTW_PWR_CUT_D_MSK	BIT(4)
#define RTW_PWR_CUT_E_MSK	BIT(5)
#define RTW_PWR_CUT_F_MSK	BIT(6)
#define RTW_PWR_CUT_G_MSK	BIT(7)
#define RTW_PWR_CUT_ALL_MSK	0xFF

enum rtw_pwr_seq_cmd_delay_unit {
	RTW_PWR_DELAY_US,
	RTW_PWR_DELAY_MS,
};

struct rtw_pwr_seq_cmd {
	u16 offset;
	u8 cut_mask;
	u8 intf_mask;
	u8 base:4;
	u8 cmd:4;
	u8 mask;
	u8 value;
};

enum rtw_chip_ver {
	RTW_CHIP_VER_CUT_A = 0x00,
	RTW_CHIP_VER_CUT_B = 0x01,
	RTW_CHIP_VER_CUT_C = 0x02,
	RTW_CHIP_VER_CUT_D = 0x03,
	RTW_CHIP_VER_CUT_E = 0x04,
	RTW_CHIP_VER_CUT_F = 0x05,
	RTW_CHIP_VER_CUT_G = 0x06,
};

#define RTW_INTF_PHY_PLATFORM_ALL 0

enum rtw_intf_phy_cut {
	RTW_INTF_PHY_CUT_A = BIT(0),
	RTW_INTF_PHY_CUT_B = BIT(1),
	RTW_INTF_PHY_CUT_C = BIT(2),
	RTW_INTF_PHY_CUT_D = BIT(3),
	RTW_INTF_PHY_CUT_E = BIT(4),
	RTW_INTF_PHY_CUT_F = BIT(5),
	RTW_INTF_PHY_CUT_G = BIT(6),
	RTW_INTF_PHY_CUT_ALL = 0xFFFF,
};

enum rtw_ip_sel {
	RTW_IP_SEL_PHY = 0,
	RTW_IP_SEL_MAC = 1,
	RTW_IP_SEL_DBI = 2,

	RTW_IP_SEL_UNDEF = 0xFFFF
};

enum rtw_pq_map_id {
	RTW_PQ_MAP_VO = 0x0,
	RTW_PQ_MAP_VI = 0x1,
	RTW_PQ_MAP_BE = 0x2,
	RTW_PQ_MAP_BK = 0x3,
	RTW_PQ_MAP_MG = 0x4,
	RTW_PQ_MAP_HI = 0x5,
	RTW_PQ_MAP_NUM = 0x6,

	RTW_PQ_MAP_UNDEF,
};

enum rtw_dma_mapping {
	RTW_DMA_MAPPING_EXTRA	= 0,
	RTW_DMA_MAPPING_LOW	= 1,
	RTW_DMA_MAPPING_NORMAL	= 2,
	RTW_DMA_MAPPING_HIGH	= 3,

	RTW_DMA_MAPPING_MAX,
	RTW_DMA_MAPPING_UNDEF,
};

struct rtw_rqpn {
	enum rtw_dma_mapping dma_map_vo;
	enum rtw_dma_mapping dma_map_vi;
	enum rtw_dma_mapping dma_map_be;
	enum rtw_dma_mapping dma_map_bk;
	enum rtw_dma_mapping dma_map_mg;
	enum rtw_dma_mapping dma_map_hi;
};

struct rtw_prioq_addr {
	u32 rsvd;
	u32 avail;
};

struct rtw_prioq_addrs {
	struct rtw_prioq_addr prio[RTW_DMA_MAPPING_MAX];
	bool wsize;
};

struct rtw_page_table {
	u16 hq_num;
	u16 nq_num;
	u16 lq_num;
	u16 exq_num;
	u16 gapq_num;
};

struct rtw_intf_phy_para {
	u16 offset;
	u16 value;
	u16 ip_sel;
	u16 cut_mask;
	u16 platform;
};

struct rtw_wow_pattern {
	u16 crc;
	u8 type;
	u8 valid;
	u8 mask[RTW_MAX_PATTERN_MASK_SIZE];
};

struct rtw_pno_request {
	bool inited;
	u32 match_set_cnt;
	struct cfg80211_match_set *match_sets;
	u8 channel_cnt;
	struct ieee80211_channel *channels;
	struct cfg80211_sched_scan_plan scan_plan;
};

struct rtw_wow_param {
	struct ieee80211_vif *wow_vif;
	DECLARE_BITMAP(flags, RTW_WOW_FLAG_MAX);
	u8 txpause;
	u8 pattern_cnt;
	struct rtw_wow_pattern patterns[RTW_MAX_PATTERN_NUM];

	bool ips_enabled;
	struct rtw_pno_request pno_req;
};

struct rtw_intf_phy_para_table {
	const struct rtw_intf_phy_para *usb2_para;
	const struct rtw_intf_phy_para *usb3_para;
	const struct rtw_intf_phy_para *gen1_para;
	const struct rtw_intf_phy_para *gen2_para;
	u8 n_usb2_para;
	u8 n_usb3_para;
	u8 n_gen1_para;
	u8 n_gen2_para;
};

struct rtw_table {
	const void *data;
	const u32 size;
	void (*parse)(struct rtw_dev *rtwdev, const struct rtw_table *tbl);
	void (*do_cfg)(struct rtw_dev *rtwdev, const struct rtw_table *tbl,
		       u32 addr, u32 data);
	enum rtw_rf_path rf_path;
};

static inline void rtw_load_table(struct rtw_dev *rtwdev,
				  const struct rtw_table *tbl)
{
	(*tbl->parse)(rtwdev, tbl);
}

enum rtw_rfe_fem {
	RTW_RFE_IFEM,
	RTW_RFE_EFEM,
	RTW_RFE_IFEM2G_EFEM5G,
	RTW_RFE_NUM,
};

struct rtw_rfe_def {
	const struct rtw_table *phy_pg_tbl;
	const struct rtw_table *txpwr_lmt_tbl;
	const struct rtw_table *agc_btg_tbl;
};

#define RTW_DEF_RFE(chip, bb_pg, pwrlmt) {				  \
	.phy_pg_tbl = &rtw ## chip ## _bb_pg_type ## bb_pg ## _tbl,	  \
	.txpwr_lmt_tbl = &rtw ## chip ## _txpwr_lmt_type ## pwrlmt ## _tbl, \
	}

#define RTW_DEF_RFE_EXT(chip, bb_pg, pwrlmt, btg) {			  \
	.phy_pg_tbl = &rtw ## chip ## _bb_pg_type ## bb_pg ## _tbl,	  \
	.txpwr_lmt_tbl = &rtw ## chip ## _txpwr_lmt_type ## pwrlmt ## _tbl, \
	.agc_btg_tbl = &rtw ## chip ## _agc_btg_type ## btg ## _tbl, \
	}

#define RTW_PWR_TRK_5G_1		0
#define RTW_PWR_TRK_5G_2		1
#define RTW_PWR_TRK_5G_3		2
#define RTW_PWR_TRK_5G_NUM		3

#define RTW_PWR_TRK_TBL_SZ		30

/* This table stores the values of TX power that will be adjusted by power
 * tracking.
 *
 * For 5G bands, there are 3 different settings.
 * For 2G there are cck rate and ofdm rate with different settings.
 */
struct rtw_pwr_track_tbl {
	const u8 *pwrtrk_5gb_n[RTW_PWR_TRK_5G_NUM];
	const u8 *pwrtrk_5gb_p[RTW_PWR_TRK_5G_NUM];
	const u8 *pwrtrk_5ga_n[RTW_PWR_TRK_5G_NUM];
	const u8 *pwrtrk_5ga_p[RTW_PWR_TRK_5G_NUM];
	const u8 *pwrtrk_2gb_n;
	const u8 *pwrtrk_2gb_p;
	const u8 *pwrtrk_2ga_n;
	const u8 *pwrtrk_2ga_p;
	const u8 *pwrtrk_2g_cckb_n;
	const u8 *pwrtrk_2g_cckb_p;
	const u8 *pwrtrk_2g_ccka_n;
	const u8 *pwrtrk_2g_ccka_p;
	const s8 *pwrtrk_xtal_n;
	const s8 *pwrtrk_xtal_p;
};

enum rtw_wlan_cpu {
	RTW_WCPU_11AC,
	RTW_WCPU_11N,
};

enum rtw_fw_fifo_sel {
	RTW_FW_FIFO_SEL_TX,
	RTW_FW_FIFO_SEL_RX,
	RTW_FW_FIFO_SEL_RSVD_PAGE,
	RTW_FW_FIFO_SEL_REPORT,
	RTW_FW_FIFO_SEL_LLT,
	RTW_FW_FIFO_SEL_RXBUF_FW,

	RTW_FW_FIFO_MAX,
};

enum rtw_fwcd_item {
	RTW_FWCD_TLV,
	RTW_FWCD_REG,
	RTW_FWCD_ROM,
	RTW_FWCD_IMEM,
	RTW_FWCD_DMEM,
	RTW_FWCD_EMEM,
};

/* hardware configuration for each IC */
struct rtw_chip_info {
	struct rtw_chip_ops *ops;
	u8 id;

	const char *fw_name;
	enum rtw_wlan_cpu wlan_cpu;
	u8 tx_pkt_desc_sz;
	u8 tx_buf_desc_sz;
	u8 rx_pkt_desc_sz;
	u8 rx_buf_desc_sz;
	u32 phy_efuse_size;
	u32 log_efuse_size;
	u32 ptct_efuse_size;
	u32 txff_size;
	u32 rxff_size;
	u32 fw_rxff_size;
	u8 band;
	u8 page_size;
	u8 csi_buf_pg_num;
	u8 dig_max;
	u8 dig_min;
	u8 txgi_factor;
	bool is_pwr_by_rate_dec;
	bool rx_ldpc;
	bool tx_stbc;
	u8 max_power_index;

	u16 fw_fifo_addr[RTW_FW_FIFO_MAX];
	const struct rtw_fwcd_segs *fwcd_segs;

	u8 default_1ss_tx_path;

	bool path_div_supported;
	bool ht_supported;
	bool vht_supported;
	u8 lps_deep_mode_supported;

	/* init values */
	u8 sys_func_en;
	const struct rtw_pwr_seq_cmd **pwr_on_seq;
	const struct rtw_pwr_seq_cmd **pwr_off_seq;
	const struct rtw_rqpn *rqpn_table;
	const struct rtw_prioq_addrs *prioq_addrs;
	const struct rtw_page_table *page_table;
	const struct rtw_intf_phy_para_table *intf_table;

	const struct rtw_hw_reg *dig;
	const struct rtw_hw_reg *dig_cck;
	u32 rf_base_addr[2];
	u32 rf_sipi_addr[2];
	const struct rtw_rf_sipi_addr *rf_sipi_read_addr;
	u8 fix_rf_phy_num;
	const struct rtw_ltecoex_addr *ltecoex_addr;

	const struct rtw_table *mac_tbl;
	const struct rtw_table *agc_tbl;
	const struct rtw_table *bb_tbl;
	const struct rtw_table *rf_tbl[RTW_RF_PATH_MAX];
	const struct rtw_table *rfk_init_tbl;

	const struct rtw_rfe_def *rfe_defs;
	u32 rfe_defs_size;

	bool en_dis_dpd;
	u16 dpd_ratemask;
	u8 iqk_threshold;
	u8 lck_threshold;
	const struct rtw_pwr_track_tbl *pwr_track_tbl;

	u8 bfer_su_max_num;
	u8 bfer_mu_max_num;

	struct rtw_hw_reg_offset *edcca_th;
	s8 l2h_th_ini_cs;
	s8 l2h_th_ini_ad;

	const char *wow_fw_name;
	const struct wiphy_wowlan_support *wowlan_stub;
	const u8 max_sched_scan_ssids;

	/* for 8821c set channel */
	u32 ch_param[3];

	/* coex paras */
	u32 coex_para_ver;
	u8 bt_desired_ver;
	bool scbd_support;
	bool new_scbd10_def; /* true: fix 2M(8822c) */
	bool ble_hid_profile_support;
	u8 pstdma_type; /* 0: LPSoff, 1:LPSon */
	u8 bt_rssi_type;
	u8 ant_isolation;
	u8 rssi_tolerance;
	u8 table_sant_num;
	u8 table_nsant_num;
	u8 tdma_sant_num;
	u8 tdma_nsant_num;
	u8 bt_afh_span_bw20;
	u8 bt_afh_span_bw40;
	u8 afh_5g_num;
	u8 wl_rf_para_num;
	u8 coex_info_hw_regs_num;
	const u8 *bt_rssi_step;
	const u8 *wl_rssi_step;
	const struct coex_table_para *table_nsant;
	const struct coex_table_para *table_sant;
	const struct coex_tdma_para *tdma_sant;
	const struct coex_tdma_para *tdma_nsant;
	const struct coex_rf_para *wl_rf_para_tx;
	const struct coex_rf_para *wl_rf_para_rx;
	const struct coex_5g_afh_map *afh_5g;
	const struct rtw_hw_reg *btg_reg;
	const struct rtw_reg_domain *coex_info_hw_regs;
	u32 wl_fw_desired_ver;
};

enum rtw_coex_bt_state_cnt {
	COEX_CNT_BT_RETRY,
	COEX_CNT_BT_REINIT,
	COEX_CNT_BT_REENABLE,
	COEX_CNT_BT_POPEVENT,
	COEX_CNT_BT_SETUPLINK,
	COEX_CNT_BT_IGNWLANACT,
	COEX_CNT_BT_INQ,
	COEX_CNT_BT_PAGE,
	COEX_CNT_BT_ROLESWITCH,
	COEX_CNT_BT_AFHUPDATE,
	COEX_CNT_BT_INFOUPDATE,
	COEX_CNT_BT_IQK,
	COEX_CNT_BT_IQKFAIL,

	COEX_CNT_BT_MAX
};

enum rtw_coex_wl_state_cnt {
	COEX_CNT_WL_SCANAP,
	COEX_CNT_WL_CONNPKT,
	COEX_CNT_WL_COEXRUN,
	COEX_CNT_WL_NOISY0,
	COEX_CNT_WL_NOISY1,
	COEX_CNT_WL_NOISY2,
	COEX_CNT_WL_5MS_NOEXTEND,
	COEX_CNT_WL_FW_NOTIFY,

	COEX_CNT_WL_MAX
};

struct rtw_coex_rfe {
	bool ant_switch_exist;
	bool ant_switch_diversity;
	bool ant_switch_with_bt;
	u8 rfe_module_type;
	u8 ant_switch_polarity;

	/* true if WLG at BTG, else at WLAG */
	bool wlg_at_btg;
};

#define COEX_WL_TDMA_PARA_LENGTH	5

struct rtw_coex_dm {
	bool cur_ps_tdma_on;
	bool cur_wl_rx_low_gain_en;
	bool ignore_wl_act;

	u8 reason;
	u8 bt_rssi_state[4];
	u8 wl_rssi_state[4];
	u8 wl_ch_info[3];
	u8 cur_ps_tdma;
	u8 cur_table;
	u8 ps_tdma_para[5];
	u8 cur_bt_pwr_lvl;
	u8 cur_bt_lna_lvl;
	u8 cur_wl_pwr_lvl;
	u8 bt_status;
	u32 cur_ant_pos_type;
	u32 cur_switch_status;
	u32 setting_tdma;
	u8 fw_tdma_para[COEX_WL_TDMA_PARA_LENGTH];
};

#define COEX_BTINFO_SRC_WL_FW	0x0
#define COEX_BTINFO_SRC_BT_RSP	0x1
#define COEX_BTINFO_SRC_BT_ACT	0x2
#define COEX_BTINFO_SRC_BT_IQK	0x3
#define COEX_BTINFO_SRC_BT_SCBD	0x4
#define COEX_BTINFO_SRC_H2C60	0x5
#define COEX_BTINFO_SRC_MAX	0x6

#define COEX_INFO_FTP		BIT(7)
#define COEX_INFO_A2DP		BIT(6)
#define COEX_INFO_HID		BIT(5)
#define COEX_INFO_SCO_BUSY	BIT(4)
#define COEX_INFO_ACL_BUSY	BIT(3)
#define COEX_INFO_INQ_PAGE	BIT(2)
#define COEX_INFO_SCO_ESCO	BIT(1)
#define COEX_INFO_CONNECTION	BIT(0)
#define COEX_BTINFO_LENGTH_MAX	10
#define COEX_BTINFO_LENGTH	7

struct rtw_coex_stat {
	bool bt_disabled;
	bool bt_disabled_pre;
	bool bt_link_exist;
	bool bt_whck_test;
	bool bt_inq_page;
	bool bt_inq_remain;
	bool bt_inq;
	bool bt_page;
	bool bt_ble_voice;
	bool bt_ble_exist;
	bool bt_hfp_exist;
	bool bt_a2dp_exist;
	bool bt_hid_exist;
	bool bt_pan_exist; /* PAN or OPP */
	bool bt_opp_exist; /* OPP only */
	bool bt_acl_busy;
	bool bt_fix_2M;
	bool bt_setup_link;
	bool bt_multi_link;
	bool bt_multi_link_pre;
	bool bt_multi_link_remain;
	bool bt_a2dp_sink;
	bool bt_a2dp_active;
	bool bt_reenable;
	bool bt_ble_scan_en;
	bool bt_init_scan;
	bool bt_slave;
	bool bt_418_hid_exist;
	bool bt_ble_hid_exist;
	bool bt_mailbox_reply;

	bool wl_under_lps;
	bool wl_under_ips;
	bool wl_hi_pri_task1;
	bool wl_hi_pri_task2;
	bool wl_force_lps_ctrl;
	bool wl_gl_busy;
	bool wl_linkscan_proc;
	bool wl_ps_state_fail;
	bool wl_tx_limit_en;
	bool wl_ampdu_limit_en;
	bool wl_connected;
	bool wl_slot_extend;
	bool wl_cck_lock;
	bool wl_cck_lock_pre;
	bool wl_cck_lock_ever;
	bool wl_connecting;
	bool wl_slot_toggle;
	bool wl_slot_toggle_change; /* if toggle to no-toggle */

	u32 bt_supported_version;
	u32 bt_supported_feature;
	u32 hi_pri_tx;
	u32 hi_pri_rx;
	u32 lo_pri_tx;
	u32 lo_pri_rx;
	u32 patch_ver;
	u16 bt_reg_vendor_ae;
	u16 bt_reg_vendor_ac;
	s8 bt_rssi;
	u8 kt_ver;
	u8 gnt_workaround_state;
	u8 tdma_timer_base;
	u8 bt_profile_num;
	u8 bt_info_c2h[COEX_BTINFO_SRC_MAX][COEX_BTINFO_LENGTH_MAX];
	u8 bt_info_lb2;
	u8 bt_info_lb3;
	u8 bt_info_hb0;
	u8 bt_info_hb1;
	u8 bt_info_hb2;
	u8 bt_info_hb3;
	u8 bt_ble_scan_type;
	u8 bt_hid_pair_num;
	u8 bt_hid_slot;
	u8 bt_a2dp_bitpool;
	u8 bt_iqk_state;

	u16 wl_beacon_interval;
	u8 wl_noisy_level;
	u8 wl_fw_dbg_info[10];
	u8 wl_fw_dbg_info_pre[10];
	u8 wl_rx_rate;
	u8 wl_tx_rate;
	u8 wl_rts_rx_rate;
	u8 wl_coex_mode;
	u8 wl_iot_peer;
	u8 ampdu_max_time;
	u8 wl_tput_dir;

	u8 wl_toggle_para[6];
	u8 wl_toggle_interval;

	u16 score_board;
	u16 retry_limit;

	/* counters to record bt states */
	u32 cnt_bt[COEX_CNT_BT_MAX];

	/* counters to record wifi states */
	u32 cnt_wl[COEX_CNT_WL_MAX];

	/* counters to record bt c2h data */
	u32 cnt_bt_info_c2h[COEX_BTINFO_SRC_MAX];

	u32 darfrc;
	u32 darfrch;
};

struct rtw_coex {
	/* protects coex info request section */
	struct mutex mutex;
	struct sk_buff_head queue;
	wait_queue_head_t wait;

	bool under_5g;
	bool stop_dm;
	bool freeze;
	bool freerun;
	bool wl_rf_off;
	bool manual_control;

	struct rtw_coex_stat stat;
	struct rtw_coex_dm dm;
	struct rtw_coex_rfe rfe;

	struct delayed_work bt_relink_work;
	struct delayed_work bt_reenable_work;
	struct delayed_work defreeze_work;
	struct delayed_work wl_remain_work;
	struct delayed_work bt_remain_work;
	struct delayed_work wl_connecting_work;
	struct delayed_work bt_multi_link_remain_work;
	struct delayed_work wl_ccklock_work;

};

#define DPK_RF_REG_NUM 7
#define DPK_RF_PATH_NUM 2
#define DPK_BB_REG_NUM 18
#define DPK_CHANNEL_WIDTH_80 1

DECLARE_EWMA(thermal, 10, 4);

struct rtw_dpk_info {
	bool is_dpk_pwr_on;
	bool is_reload;

	DECLARE_BITMAP(dpk_path_ok, DPK_RF_PATH_NUM);

	u8 thermal_dpk[DPK_RF_PATH_NUM];
	struct ewma_thermal avg_thermal[DPK_RF_PATH_NUM];

	u32 gnt_control;
	u32 gnt_value;

	u8 result[RTW_RF_PATH_MAX];
	u8 dpk_txagc[RTW_RF_PATH_MAX];
	u32 coef[RTW_RF_PATH_MAX][20];
	u16 dpk_gs[RTW_RF_PATH_MAX];
	u8 thermal_dpk_delta[RTW_RF_PATH_MAX];
	u8 pre_pwsf[RTW_RF_PATH_MAX];

	u8 dpk_band;
	u8 dpk_ch;
	u8 dpk_bw;
};

struct rtw_phy_cck_pd_reg {
	u32 reg_pd;
	u32 mask_pd;
	u32 reg_cs;
	u32 mask_cs;
};

#define DACK_MSBK_BACKUP_NUM	0xf
#define DACK_DCK_BACKUP_NUM	0x2

struct rtw_swing_table {
	const u8 *p[RTW_RF_PATH_MAX];
	const u8 *n[RTW_RF_PATH_MAX];
};

struct rtw_pkt_count {
	u16 num_bcn_pkt;
	u16 num_qry_pkt[DESC_RATE_MAX];
};

DECLARE_EWMA(evm, 10, 4);
DECLARE_EWMA(snr, 10, 4);

struct rtw_iqk_info {
	bool done;
	struct {
		u32 s1_x;
		u32 s1_y;
		u32 s0_x;
		u32 s0_y;
	} result;
};

enum rtw_rf_band {
	RF_BAND_2G_CCK,
	RF_BAND_2G_OFDM,
	RF_BAND_5G_L,
	RF_BAND_5G_M,
	RF_BAND_5G_H,
	RF_BAND_MAX
};

#define RF_GAIN_NUM 11
#define RF_HW_OFFSET_NUM 10

struct rtw_gapk_info {
	u32 rf3f_bp[RF_BAND_MAX][RF_GAIN_NUM][RTW_RF_PATH_MAX];
	u32 rf3f_fs[RTW_RF_PATH_MAX][RF_GAIN_NUM];
	bool txgapk_bp_done;
	s8 offset[RF_GAIN_NUM][RTW_RF_PATH_MAX];
	s8 fianl_offset[RF_GAIN_NUM][RTW_RF_PATH_MAX];
	u8 read_txgain;
	u8 channel;
};

#define EDCCA_TH_L2H_IDX 0
#define EDCCA_TH_H2L_IDX 1
#define EDCCA_TH_L2H_LB 48
#define EDCCA_ADC_BACKOFF 12
#define EDCCA_IGI_BASE 50
#define EDCCA_IGI_L2H_DIFF 8
#define EDCCA_L2H_H2L_DIFF 7
#define EDCCA_L2H_H2L_DIFF_NORMAL 8

enum rtw_edcca_mode {
	RTW_EDCCA_NORMAL	= 0,
	RTW_EDCCA_ADAPTIVITY	= 1,
};

struct rtw_cfo_track {
	bool is_adjust;
	u8 crystal_cap;
	s32 cfo_tail[RTW_RF_PATH_MAX];
	s32 cfo_cnt[RTW_RF_PATH_MAX];
	u32 packet_count;
	u32 packet_count_pre;
};

#define RRSR_INIT_2G 0x15f
#define RRSR_INIT_5G 0x150

enum rtw_dm_cap {
	RTW_DM_CAP_NA,
	RTW_DM_CAP_TXGAPK,
	RTW_DM_CAP_NUM
};

struct rtw_dm_info {
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 total_fa_cnt;
	u32 cck_cca_cnt;
	u32 ofdm_cca_cnt;
	u32 total_cca_cnt;

	u32 cck_ok_cnt;
	u32 cck_err_cnt;
	u32 ofdm_ok_cnt;
	u32 ofdm_err_cnt;
	u32 ht_ok_cnt;
	u32 ht_err_cnt;
	u32 vht_ok_cnt;
	u32 vht_err_cnt;

	u8 min_rssi;
	u8 pre_min_rssi;
	u16 fa_history[4];
	u8 igi_history[4];
	u8 igi_bitmap;
	bool damping;
	u8 damping_cnt;
	u8 damping_rssi;

	u8 cck_gi_u_bnd;
	u8 cck_gi_l_bnd;

	u8 fix_rate;
	u8 tx_rate;
	u32 rrsr_val_init;
	u32 rrsr_mask_min;
	u8 thermal_avg[RTW_RF_PATH_MAX];
	u8 thermal_meter_k;
	u8 thermal_meter_lck;
	s8 delta_power_index[RTW_RF_PATH_MAX];
	s8 delta_power_index_last[RTW_RF_PATH_MAX];
	u8 default_ofdm_index;
	bool pwr_trk_triggered;
	bool pwr_trk_init_trigger;
	struct ewma_thermal avg_thermal[RTW_RF_PATH_MAX];
	s8 txagc_remnant_cck;
	s8 txagc_remnant_ofdm;

	/* backup dack results for each path and I/Q */
	u32 dack_adck[RTW_RF_PATH_MAX];
	u16 dack_msbk[RTW_RF_PATH_MAX][2][DACK_MSBK_BACKUP_NUM];
	u8 dack_dck[RTW_RF_PATH_MAX][2][DACK_DCK_BACKUP_NUM];

	struct rtw_dpk_info dpk_info;
	struct rtw_cfo_track cfo_track;

	/* [bandwidth 0:20M/1:40M][number of path] */
	u8 cck_pd_lv[2][RTW_RF_PATH_MAX];
	u32 cck_fa_avg;
	u8 cck_pd_default;

	/* save the last rx phy status for debug */
	s8 rx_snr[RTW_RF_PATH_MAX];
	u8 rx_evm_dbm[RTW_RF_PATH_MAX];
	s16 cfo_tail[RTW_RF_PATH_MAX];
	u8 rssi[RTW_RF_PATH_MAX];
	u8 curr_rx_rate;
	struct rtw_pkt_count cur_pkt_count;
	struct rtw_pkt_count last_pkt_count;
	struct ewma_evm ewma_evm[RTW_EVM_NUM];
	struct ewma_snr ewma_snr[RTW_SNR_NUM];

	u32 dm_flags; /* enum rtw_dm_cap */
	struct rtw_iqk_info iqk;
	struct rtw_gapk_info gapk;
	bool is_bt_iqk_timeout;

	s8 l2h_th_ini;
	enum rtw_edcca_mode edcca_mode;
	u8 scan_density;
};

struct rtw_efuse {
	u32 size;
	u32 physical_size;
	u32 logical_size;
	u32 protect_size;

	u8 addr[ETH_ALEN];
	u8 channel_plan;
	u8 country_code[2];
	u8 rf_board_option;
	u8 rfe_option;
	u8 power_track_type;
	u8 thermal_meter[RTW_RF_PATH_MAX];
	u8 thermal_meter_k;
	u8 crystal_cap;
	u8 ant_div_cfg;
	u8 ant_div_type;
	u8 regd;
	u8 afe;

	u8 lna_type_2g;
	u8 lna_type_5g;
	u8 glna_type;
	u8 alna_type;
	bool ext_lna_2g;
	bool ext_lna_5g;
	u8 pa_type_2g;
	u8 pa_type_5g;
	u8 gpa_type;
	u8 apa_type;
	bool ext_pa_2g;
	bool ext_pa_5g;
	u8 tx_bb_swing_setting_2g;
	u8 tx_bb_swing_setting_5g;

	bool btcoex;
	/* bt share antenna with wifi */
	bool share_ant;
	u8 bt_setting;

	struct {
		u8 hci;
		u8 bw;
		u8 ptcl;
		u8 nss;
		u8 ant_num;
	} hw_cap;

	struct rtw_txpwr_idx txpwr_idx_table[4];
};

struct rtw_phy_cond {
#ifdef __LITTLE_ENDIAN
	u32 rfe:8;
	u32 intf:4;
	u32 pkg:4;
	u32 plat:4;
	u32 intf_rsvd:4;
	u32 cut:4;
	u32 branch:2;
	u32 neg:1;
	u32 pos:1;
#else
	u32 pos:1;
	u32 neg:1;
	u32 branch:2;
	u32 cut:4;
	u32 intf_rsvd:4;
	u32 plat:4;
	u32 pkg:4;
	u32 intf:4;
	u32 rfe:8;
#endif
	/* for intf:4 */
	#define INTF_PCIE	BIT(0)
	#define INTF_USB	BIT(1)
	#define INTF_SDIO	BIT(2)
	/* for branch:2 */
	#define BRANCH_IF	0
	#define BRANCH_ELIF	1
	#define BRANCH_ELSE	2
	#define BRANCH_ENDIF	3
};

struct rtw_fifo_conf {
	/* tx fifo information */
	u16 rsvd_boundary;
	u16 rsvd_pg_num;
	u16 rsvd_drv_pg_num;
	u16 txff_pg_num;
	u16 acq_pg_num;
	u16 rsvd_drv_addr;
	u16 rsvd_h2c_info_addr;
	u16 rsvd_h2c_sta_info_addr;
	u16 rsvd_h2cq_addr;
	u16 rsvd_cpu_instr_addr;
	u16 rsvd_fw_txbuf_addr;
	u16 rsvd_csibuf_addr;
	const struct rtw_rqpn *rqpn;
};

struct rtw_fwcd_desc {
	u32 size;
	u8 *next;
	u8 *data;
};

struct rtw_fwcd_segs {
	const u32 *segs;
	u8 num;
};

#define FW_CD_TYPE 0xffff
#define FW_CD_LEN 4
#define FW_CD_VAL 0xaabbccdd
struct rtw_fw_state {
	const struct firmware *firmware;
	struct rtw_dev *rtwdev;
	struct completion completion;
	struct rtw_fwcd_desc fwcd_desc;
	u16 version;
	u8 sub_version;
	u8 sub_index;
	u16 h2c_version;
	u32 feature;
};

enum rtw_sar_sources {
	RTW_SAR_SOURCE_NONE,
	RTW_SAR_SOURCE_COMMON,
};

enum rtw_sar_bands {
	RTW_SAR_BAND_0,
	RTW_SAR_BAND_1,
	/* RTW_SAR_BAND_2, not used now */
	RTW_SAR_BAND_3,
	RTW_SAR_BAND_4,

	RTW_SAR_BAND_NR,
};

/* the union is reserved for other knids of SAR sources
 * which might not re-use same format with array common.
 */
union rtw_sar_cfg {
	s8 common[RTW_SAR_BAND_NR];
};

struct rtw_sar {
	enum rtw_sar_sources src;
	union rtw_sar_cfg cfg[RTW_RF_PATH_MAX][RTW_RATE_SECTION_MAX];
};

struct rtw_hal {
	u32 rcr;

	u32 chip_version;
	u8 cut_version;
	u8 mp_chip;
	u8 oem_id;
	struct rtw_phy_cond phy_cond;

	u8 ps_mode;
	u8 current_channel;
	u8 current_primary_channel_index;
	u8 current_band_width;
	u8 current_band_type;

	/* center channel for different available bandwidth,
	 * val of (bw > current_band_width) is invalid
	 */
	u8 cch_by_bw[RTW_MAX_CHANNEL_WIDTH + 1];

	u8 sec_ch_offset;
	u8 rf_type;
	u8 rf_path_num;
	u8 rf_phy_num;
	u32 antenna_tx;
	u32 antenna_rx;
	u8 bfee_sts_cap;

	/* protect tx power section */
	struct mutex tx_power_mutex;
	s8 tx_pwr_by_rate_offset_2g[RTW_RF_PATH_MAX]
				   [DESC_RATE_MAX];
	s8 tx_pwr_by_rate_offset_5g[RTW_RF_PATH_MAX]
				   [DESC_RATE_MAX];
	s8 tx_pwr_by_rate_base_2g[RTW_RF_PATH_MAX]
				 [RTW_RATE_SECTION_MAX];
	s8 tx_pwr_by_rate_base_5g[RTW_RF_PATH_MAX]
				 [RTW_RATE_SECTION_MAX];
	s8 tx_pwr_limit_2g[RTW_REGD_MAX]
			  [RTW_CHANNEL_WIDTH_MAX]
			  [RTW_RATE_SECTION_MAX]
			  [RTW_MAX_CHANNEL_NUM_2G];
	s8 tx_pwr_limit_5g[RTW_REGD_MAX]
			  [RTW_CHANNEL_WIDTH_MAX]
			  [RTW_RATE_SECTION_MAX]
			  [RTW_MAX_CHANNEL_NUM_5G];
	s8 tx_pwr_tbl[RTW_RF_PATH_MAX]
		     [DESC_RATE_MAX];

	enum rtw_sar_bands sar_band;
	struct rtw_sar sar;
};

struct rtw_path_div {
	enum rtw_bb_path current_tx_path;
	u32 path_a_sum;
	u32 path_b_sum;
	u16 path_a_cnt;
	u16 path_b_cnt;
};

struct rtw_chan_info {
	int pri_ch_idx;
	int action_id;
	int bw;
	u8 extra_info;
	u8 channel;
	u16 timeout;
};

struct rtw_chan_list {
	u32 buf_size;
	u32 ch_num;
	u32 size;
	u16 addr;
};

struct rtw_hw_scan_info {
	struct ieee80211_vif *scanning_vif;
	u8 probe_pg_size;
	u8 op_pri_ch_idx;
	u8 op_chan;
	u8 op_bw;
};

struct rtw_dev {
	struct ieee80211_hw *hw;
	struct device *dev;

	struct rtw_hci hci;

	struct rtw_hw_scan_info scan_info;
	struct rtw_chip_info *chip;
	struct rtw_hal hal;
	struct rtw_fifo_conf fifo;
	struct rtw_fw_state fw;
	struct rtw_efuse efuse;
	struct rtw_sec_desc sec;
	struct rtw_traffic_stats stats;
	struct rtw_regd regd;
	struct rtw_bf_info bf_info;

	struct rtw_dm_info dm_info;
	struct rtw_coex coex;

	/* ensures exclusive access from mac80211 callbacks */
	struct mutex mutex;

	/* read/write rf register */
	spinlock_t rf_lock;

	/* watch dog every 2 sec */
	struct delayed_work watch_dog_work;
	u32 watch_dog_cnt;

	struct list_head rsvd_page_list;

	/* c2h cmd queue & handler work */
	struct sk_buff_head c2h_queue;
	struct work_struct c2h_work;
	struct work_struct ips_work;
	struct work_struct fw_recovery_work;

	/* used to protect txqs list */
	spinlock_t txq_lock;
	struct list_head txqs;
	struct workqueue_struct *tx_wq;
	struct work_struct tx_work;
	struct work_struct ba_work;

	struct rtw_tx_report tx_report;

	struct {
		/* incicate the mail box to use with fw */
		u8 last_box_num;
		/* protect to send h2c to fw */
		spinlock_t lock;
		u32 seq;
	} h2c;

	/* lps power state & handler work */
	struct rtw_lps_conf lps_conf;
	bool ps_enabled;
	bool beacon_loss;
	struct completion lps_leave_check;

	struct dentry *debugfs;

	u8 sta_cnt;
	u32 rts_threshold;

	DECLARE_BITMAP(mac_id_map, RTW_MAX_MAC_ID_NUM);
	DECLARE_BITMAP(flags, NUM_OF_RTW_FLAGS);

	u8 mp_mode;
	struct rtw_path_div dm_path_div;

	struct rtw_fw_state wow_fw;
	struct rtw_wow_param wow;

	bool need_rfk;
	struct completion fw_scan_density;

	/* hci related data, must be last */
	u8 priv[] __aligned(sizeof(void *));
};

#include "hci.h"

static inline bool rtw_is_assoc(struct rtw_dev *rtwdev)
{
	return !!rtwdev->sta_cnt;
}

static inline struct ieee80211_txq *rtwtxq_to_txq(struct rtw_txq *rtwtxq)
{
	void *p = rtwtxq;

	return container_of(p, struct ieee80211_txq, drv_priv);
}

static inline struct ieee80211_vif *rtwvif_to_vif(struct rtw_vif *rtwvif)
{
	void *p = rtwvif;

	return container_of(p, struct ieee80211_vif, drv_priv);
}

static inline bool rtw_ssid_equal(struct cfg80211_ssid *a,
				  struct cfg80211_ssid *b)
{
	if (!a || !b || a->ssid_len != b->ssid_len)
		return false;

	if (memcmp(a->ssid, b->ssid, a->ssid_len))
		return false;

	return true;
}

static inline void rtw_chip_efuse_grant_on(struct rtw_dev *rtwdev)
{
	if (rtwdev->chip->ops->efuse_grant)
		rtwdev->chip->ops->efuse_grant(rtwdev, true);
}

static inline void rtw_chip_efuse_grant_off(struct rtw_dev *rtwdev)
{
	if (rtwdev->chip->ops->efuse_grant)
		rtwdev->chip->ops->efuse_grant(rtwdev, false);
}

static inline bool rtw_chip_wcpu_11n(struct rtw_dev *rtwdev)
{
	return rtwdev->chip->wlan_cpu == RTW_WCPU_11N;
}

static inline bool rtw_chip_wcpu_11ac(struct rtw_dev *rtwdev)
{
	return rtwdev->chip->wlan_cpu == RTW_WCPU_11AC;
}

static inline bool rtw_chip_has_rx_ldpc(struct rtw_dev *rtwdev)
{
	return rtwdev->chip->rx_ldpc;
}

static inline bool rtw_chip_has_tx_stbc(struct rtw_dev *rtwdev)
{
	return rtwdev->chip->tx_stbc;
}

static inline void rtw_release_macid(struct rtw_dev *rtwdev, u8 mac_id)
{
	clear_bit(mac_id, rtwdev->mac_id_map);
}

static inline int rtw_chip_dump_fw_crash(struct rtw_dev *rtwdev)
{
	if (rtwdev->chip->ops->dump_fw_crash)
		return rtwdev->chip->ops->dump_fw_crash(rtwdev);

	return 0;
}

void rtw_set_rx_freq_band(struct rtw_rx_pkt_stat *pkt_stat, u8 channel);
void rtw_get_channel_params(struct cfg80211_chan_def *chandef,
			    struct rtw_channel_params *ch_param);
bool check_hw_ready(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target);
bool ltecoex_read_reg(struct rtw_dev *rtwdev, u16 offset, u32 *val);
bool ltecoex_reg_write(struct rtw_dev *rtwdev, u16 offset, u32 value);
void rtw_restore_reg(struct rtw_dev *rtwdev,
		     struct rtw_backup_info *bckp, u32 num);
void rtw_desc_to_mcsrate(u16 rate, u8 *mcs, u8 *nss);
void rtw_set_channel(struct rtw_dev *rtwdev);
void rtw_chip_prepare_tx(struct rtw_dev *rtwdev);
void rtw_vif_port_config(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif,
			 u32 config);
void rtw_tx_report_purge_timer(struct timer_list *t);
void rtw_update_sta_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si);
void rtw_core_scan_start(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif,
			 const u8 *mac_addr, bool hw_scan);
void rtw_core_scan_complete(struct rtw_dev *rtwdev, struct ieee80211_vif *vif,
			    bool hw_scan);
int rtw_core_start(struct rtw_dev *rtwdev);
void rtw_core_stop(struct rtw_dev *rtwdev);
int rtw_chip_info_setup(struct rtw_dev *rtwdev);
int rtw_core_init(struct rtw_dev *rtwdev);
void rtw_core_deinit(struct rtw_dev *rtwdev);
int rtw_register_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw);
void rtw_unregister_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw);
u16 rtw_desc_to_bitrate(u8 desc_rate);
void rtw_vif_assoc_changed(struct rtw_vif *rtwvif,
			   struct ieee80211_bss_conf *conf);
int rtw_sta_add(struct rtw_dev *rtwdev, struct ieee80211_sta *sta,
		struct ieee80211_vif *vif);
void rtw_sta_remove(struct rtw_dev *rtwdev, struct ieee80211_sta *sta,
		    bool fw_exist);
void rtw_fw_recovery(struct rtw_dev *rtwdev);
void rtw_core_fw_scan_notify(struct rtw_dev *rtwdev, bool start);
int rtw_dump_fw(struct rtw_dev *rtwdev, const u32 ocp_src, u32 size,
		u32 fwcd_item);
int rtw_dump_reg(struct rtw_dev *rtwdev, const u32 addr, const u32 size);

#endif
