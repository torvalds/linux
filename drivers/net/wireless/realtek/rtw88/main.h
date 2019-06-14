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

#include "util.h"

#define RTW_MAX_MAC_ID_NUM		32
#define RTW_MAX_SEC_CAM_NUM		32

#define RTW_WATCH_DOG_DELAY_TIME	round_jiffies_relative(HZ * 2)

#define RFREG_MASK			0xfffff
#define INV_RF_DATA			0xffffffff
#define TX_PAGE_SIZE_SHIFT		7

#define RTW_CHANNEL_WIDTH_MAX		3
#define RTW_RF_PATH_MAX			4
#define HW_FEATURE_LEN			13

extern unsigned int rtw_debug_mask;
extern const struct ieee80211_ops rtw_ops;
extern struct rtw_chip_info rtw8822b_hw_spec;
extern struct rtw_chip_info rtw8822c_hw_spec;

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

	u8 bulkout_num;
};

enum rtw_supported_band {
	RTW_BAND_2G = 1 << 0,
	RTW_BAND_5G = 1 << 1,
	RTW_BAND_60G = 1 << 2,

	RTW_BAND_MAX,
};

enum rtw_bandwidth {
	RTW_CHANNEL_WIDTH_20	= 0,
	RTW_CHANNEL_WIDTH_40	= 1,
	RTW_CHANNEL_WIDTH_80	= 2,
	RTW_CHANNEL_WIDTH_160	= 3,
	RTW_CHANNEL_WIDTH_80_80	= 4,
	RTW_CHANNEL_WIDTH_5	= 5,
	RTW_CHANNEL_WIDTH_10	= 6,
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
	RTW_REGD_FCC	= 0,
	RTW_REGD_MKK	= 1,
	RTW_REGD_ETSI	= 2,
	RTW_REGD_WW	= 3,

	RTW_REGD_MAX
};

enum rtw_flags {
	RTW_FLAG_RUNNING,
	RTW_FLAG_FW_RUNNING,
	RTW_FLAG_SCANNING,
	RTW_FLAG_INACTIVE_PS,
	RTW_FLAG_LEISURE_PS,
	RTW_FLAG_DIG_DISABLE,

	NUM_OF_RTW_FLAGS,
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
};

struct rtw_hw_reg {
	u32 addr;
	u32 mask;
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
	struct rtw_sta_info *si;
	struct ieee80211_vif *vif;
};

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
};

enum rtw_lps_mode {
	RTW_MODE_ACTIVE	= 0,
	RTW_MODE_LPS	= 1,
	RTW_MODE_WMM_PS	= 2,
};

enum rtw_pwr_state {
	RTW_RF_OFF	= 0x0,
	RTW_RF_ON	= 0x4,
	RTW_ALL_ON	= 0xc,
};

struct rtw_lps_conf {
	/* the interface to enter lps */
	struct rtw_vif *rtwvif;
	enum rtw_lps_mode mode;
	enum rtw_pwr_state state;
	u8 awake_interval;
	u8 rlbm;
	u8 smart_ps;
	u8 port_id;
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
};

struct rtw_vif {
	struct ieee80211_vif *vif;
	enum rtw_net_type net_type;
	u16 aid;
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	u8 port;
	u8 bcn_ctrl;
	const struct rtw_vif_port *conf;

	struct rtw_traffic_stats stats;
	bool in_lps;
};

struct rtw_regulatory {
	char alpha2[2];
	u8 chplan;
	u8 txpwr_regd;
};

struct rtw_chip_ops {
	int (*mac_init)(struct rtw_dev *rtwdev);
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
	void (*set_antenna)(struct rtw_dev *rtwdev, u8 antenna_tx,
			    u8 antenna_rx);
	void (*cfg_ldo25)(struct rtw_dev *rtwdev, bool enable);
	void (*false_alarm_statistics)(struct rtw_dev *rtwdev);
	void (*do_iqk)(struct rtw_dev *rtwdev);
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

struct rtw_intf_phy_para_table {
	struct rtw_intf_phy_para *usb2_para;
	struct rtw_intf_phy_para *usb3_para;
	struct rtw_intf_phy_para *gen1_para;
	struct rtw_intf_phy_para *gen2_para;
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
};

#define RTW_DEF_RFE(chip, bb_pg, pwrlmt) {				  \
	.phy_pg_tbl = &rtw ## chip ## _bb_pg_type ## bb_pg ## _tbl,	  \
	.txpwr_lmt_tbl = &rtw ## chip ## _txpwr_lmt_type ## pwrlmt ## _tbl, \
	}

/* hardware configuration for each IC */
struct rtw_chip_info {
	struct rtw_chip_ops *ops;
	u8 id;

	const char *fw_name;
	u8 tx_pkt_desc_sz;
	u8 tx_buf_desc_sz;
	u8 rx_pkt_desc_sz;
	u8 rx_buf_desc_sz;
	u32 phy_efuse_size;
	u32 log_efuse_size;
	u32 ptct_efuse_size;
	u32 txff_size;
	u32 rxff_size;
	u8 band;
	u8 page_size;
	u8 csi_buf_pg_num;
	u8 dig_max;
	u8 dig_min;
	u8 txgi_factor;
	bool is_pwr_by_rate_dec;
	u8 max_power_index;

	bool ht_supported;
	bool vht_supported;

	/* init values */
	u8 sys_func_en;
	struct rtw_pwr_seq_cmd **pwr_on_seq;
	struct rtw_pwr_seq_cmd **pwr_off_seq;
	struct rtw_rqpn *rqpn_table;
	struct rtw_page_table *page_table;
	struct rtw_intf_phy_para_table *intf_table;

	struct rtw_hw_reg *dig;
	u32 rf_base_addr[2];
	u32 rf_sipi_addr[2];

	const struct rtw_table *mac_tbl;
	const struct rtw_table *agc_tbl;
	const struct rtw_table *bb_tbl;
	const struct rtw_table *rf_tbl[RTW_RF_PATH_MAX];
	const struct rtw_table *rfk_init_tbl;

	const struct rtw_rfe_def *rfe_defs;
	u32 rfe_defs_size;
};

struct rtw_dm_info {
	u32 cck_fa_cnt;
	u32 ofdm_fa_cnt;
	u32 total_fa_cnt;
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
};

struct rtw_efuse {
	u32 size;
	u32 physical_size;
	u32 logical_size;
	u32 protect_size;

	u8 addr[ETH_ALEN];
	u8 channel_plan;
	u8 country_code[2];
	u8 rfe_option;
	u8 thermal_meter;
	u8 crystal_cap;
	u8 ant_div_cfg;
	u8 ant_div_type;
	u8 regd;

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
	enum rtw_dma_mapping pq_map[RTW_PQ_MAP_NUM];
};

struct rtw_fw_state {
	const struct firmware *firmware;
	struct completion completion;
	u16 version;
	u8 sub_version;
	u8 sub_index;
	u16 h2c_version;
};

struct rtw_hal {
	u32 rcr;

	u32 chip_version;
	u8 fab_version;
	u8 cut_version;
	u8 mp_chip;
	u8 oem_id;
	struct rtw_phy_cond phy_cond;

	u8 ps_mode;
	u8 current_channel;
	u8 current_band_width;
	u8 current_band_type;
	u8 sec_ch_offset;
	u8 rf_type;
	u8 rf_path_num;
	u8 antenna_tx;
	u8 antenna_rx;

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
};

struct rtw_dev {
	struct ieee80211_hw *hw;
	struct device *dev;

	struct rtw_hci hci;

	struct rtw_chip_info *chip;
	struct rtw_hal hal;
	struct rtw_fifo_conf fifo;
	struct rtw_fw_state fw;
	struct rtw_efuse efuse;
	struct rtw_sec_desc sec;
	struct rtw_traffic_stats stats;
	struct rtw_regulatory regd;

	struct rtw_dm_info dm_info;

	/* ensures exclusive access from mac80211 callbacks */
	struct mutex mutex;

	/* lock for dm to use */
	spinlock_t dm_lock;

	/* read/write rf register */
	spinlock_t rf_lock;

	/* watch dog every 2 sec */
	struct delayed_work watch_dog_work;
	u32 watch_dog_cnt;

	struct list_head rsvd_page_list;

	/* c2h cmd queue & handler work */
	struct sk_buff_head c2h_queue;
	struct work_struct c2h_work;

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
	struct delayed_work lps_work;

	struct dentry *debugfs;

	u8 sta_cnt;

	DECLARE_BITMAP(mac_id_map, RTW_MAX_MAC_ID_NUM);
	DECLARE_BITMAP(flags, NUM_OF_RTW_FLAGS);

	u8 mp_mode;

	/* hci related data, must be last */
	u8 priv[0] __aligned(sizeof(void *));
};

#include "hci.h"

static inline bool rtw_flag_check(struct rtw_dev *rtwdev, enum rtw_flags flag)
{
	return test_bit(flag, rtwdev->flags);
}

static inline void rtw_flag_clear(struct rtw_dev *rtwdev, enum rtw_flags flag)
{
	clear_bit(flag, rtwdev->flags);
}

static inline void rtw_flag_set(struct rtw_dev *rtwdev, enum rtw_flags flag)
{
	set_bit(flag, rtwdev->flags);
}

void rtw_get_channel_params(struct cfg80211_chan_def *chandef,
			    struct rtw_channel_params *ch_param);
bool check_hw_ready(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target);
bool ltecoex_read_reg(struct rtw_dev *rtwdev, u16 offset, u32 *val);
bool ltecoex_reg_write(struct rtw_dev *rtwdev, u16 offset, u32 value);
void rtw_restore_reg(struct rtw_dev *rtwdev,
		     struct rtw_backup_info *bckp, u32 num);
void rtw_set_channel(struct rtw_dev *rtwdev);
void rtw_vif_port_config(struct rtw_dev *rtwdev, struct rtw_vif *rtwvif,
			 u32 config);
void rtw_tx_report_purge_timer(struct timer_list *t);
void rtw_update_sta_info(struct rtw_dev *rtwdev, struct rtw_sta_info *si);
int rtw_core_start(struct rtw_dev *rtwdev);
void rtw_core_stop(struct rtw_dev *rtwdev);
int rtw_chip_info_setup(struct rtw_dev *rtwdev);
int rtw_core_init(struct rtw_dev *rtwdev);
void rtw_core_deinit(struct rtw_dev *rtwdev);
int rtw_register_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw);
void rtw_unregister_hw(struct rtw_dev *rtwdev, struct ieee80211_hw *hw);

#endif
