/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_MCU_H
#define __MT7915_MCU_H

#include "../mt76_connac_mcu.h"

enum {
	MCU_ATE_SET_TRX = 0x1,
	MCU_ATE_SET_FREQ_OFFSET = 0xa,
	MCU_ATE_SET_SLOT_TIME = 0x13,
	MCU_ATE_CLEAN_TXQUEUE = 0x1c,
};

struct mt7915_mcu_thermal_ctrl {
	u8 ctrl_id;
	u8 band_idx;
	union {
		struct {
			u8 protect_type; /* 1: duty admit, 2: radio off */
			u8 trigger_type; /* 0: low, 1: high */
		} __packed type;
		struct {
			u8 duty_level;	/* level 0~3 */
			u8 duty_cycle;
		} __packed duty;
	};
} __packed;

struct mt7915_mcu_thermal_notify {
	struct mt76_connac2_mcu_rxd rxd;

	struct mt7915_mcu_thermal_ctrl ctrl;
	__le32 temperature;
	u8 rsv[8];
} __packed;

struct mt7915_mcu_csa_notify {
	struct mt76_connac2_mcu_rxd rxd;

	u8 omac_idx;
	u8 csa_count;
	u8 band_idx;
	u8 rsv;
} __packed;

struct mt7915_mcu_bcc_notify {
	struct mt76_connac2_mcu_rxd rxd;

	u8 band_idx;
	u8 omac_idx;
	u8 cca_count;
	u8 rsv;
} __packed;

struct mt7915_mcu_rdd_report {
	struct mt76_connac2_mcu_rxd rxd;

	u8 band_idx;
	u8 long_detected;
	u8 constant_prf_detected;
	u8 staggered_prf_detected;
	u8 radar_type_idx;
	u8 periodic_pulse_num;
	u8 long_pulse_num;
	u8 hw_pulse_num;

	u8 out_lpn;
	u8 out_spn;
	u8 out_crpn;
	u8 out_crpw;
	u8 out_crbn;
	u8 out_stgpn;
	u8 out_stgpw;

	u8 rsv;

	__le32 out_pri_const;
	__le32 out_pri_stg[3];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 mdrdy_flag;
		u8 rsv[3];
	} long_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 mdrdy_flag;
		u8 rsv[3];
	} periodic_pulse[32];

	struct {
		__le32 start;
		__le16 pulse_width;
		__le16 pulse_power;
		u8 sc_pass;
		u8 sw_reset;
		u8 mdrdy_flag;
		u8 tx_active;
	} hw_pulse[32];
} __packed;

struct mt7915_mcu_background_chain_ctrl {
	u8 chan;		/* primary channel */
	u8 central_chan;	/* central channel */
	u8 bw;
	u8 tx_stream;
	u8 rx_stream;

	u8 monitor_chan;	/* monitor channel */
	u8 monitor_central_chan;/* monitor central channel */
	u8 monitor_bw;
	u8 monitor_tx_stream;
	u8 monitor_rx_stream;

	u8 scan_mode;		/* 0: ScanStop
				 * 1: ScanStart
				 * 2: ScanRunning
				 */
	u8 band_idx;		/* DBDC */
	u8 monitor_scan_type;
	u8 band;		/* 0: 2.4GHz, 1: 5GHz */
	u8 rsv[2];
} __packed;

struct mt7915_mcu_eeprom {
	u8 buffer_mode;
	u8 format;
	__le16 len;
} __packed;

struct mt7915_mcu_eeprom_info {
	__le32 addr;
	__le32 valid;
	u8 data[16];
} __packed;

struct mt7915_mcu_phy_rx_info {
	u8 category;
	u8 rate;
	u8 mode;
	u8 nsts;
	u8 gi;
	u8 coding;
	u8 stbc;
	u8 bw;
};

struct mt7915_mcu_mib {
	__le32 band;
	__le32 offs;
	__le64 data;
} __packed;

enum mt7915_chan_mib_offs {
	/* mt7915 */
	MIB_BUSY_TIME = 14,
	MIB_TX_TIME = 81,
	MIB_RX_TIME,
	MIB_OBSS_AIRTIME = 86,
	/* mt7916 */
	MIB_BUSY_TIME_V2 = 0,
	MIB_TX_TIME_V2 = 6,
	MIB_RX_TIME_V2 = 8,
	MIB_OBSS_AIRTIME_V2 = 490
};

struct edca {
	u8 queue;
	u8 set;
	u8 aifs;
	u8 cw_min;
	__le16 cw_max;
	__le16 txop;
};

struct mt7915_mcu_tx {
	u8 total;
	u8 action;
	u8 valid;
	u8 mode;

	struct edca edca[IEEE80211_NUM_ACS];
} __packed;

struct mt7915_mcu_muru_stats {
	__le32 event_id;
	struct {
		__le32 cck_cnt;
		__le32 ofdm_cnt;
		__le32 htmix_cnt;
		__le32 htgf_cnt;
		__le32 vht_su_cnt;
		__le32 vht_2mu_cnt;
		__le32 vht_3mu_cnt;
		__le32 vht_4mu_cnt;
		__le32 he_su_cnt;
		__le32 he_ext_su_cnt;
		__le32 he_2ru_cnt;
		__le32 he_2mu_cnt;
		__le32 he_3ru_cnt;
		__le32 he_3mu_cnt;
		__le32 he_4ru_cnt;
		__le32 he_4mu_cnt;
		__le32 he_5to8ru_cnt;
		__le32 he_9to16ru_cnt;
		__le32 he_gtr16ru_cnt;
	} dl;

	struct {
		__le32 hetrig_su_cnt;
		__le32 hetrig_2ru_cnt;
		__le32 hetrig_3ru_cnt;
		__le32 hetrig_4ru_cnt;
		__le32 hetrig_5to8ru_cnt;
		__le32 hetrig_9to16ru_cnt;
		__le32 hetrig_gtr16ru_cnt;
		__le32 hetrig_2mu_cnt;
		__le32 hetrig_3mu_cnt;
		__le32 hetrig_4mu_cnt;
	} ul;
};

#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		GENMASK(3, 0)

enum {
	MCU_FW_LOG_WM,
	MCU_FW_LOG_WA,
	MCU_FW_LOG_TO_HOST,
};

enum {
	MCU_TWT_AGRT_ADD,
	MCU_TWT_AGRT_MODIFY,
	MCU_TWT_AGRT_DELETE,
	MCU_TWT_AGRT_TEARDOWN,
	MCU_TWT_AGRT_GET_TSF,
};

enum {
	MCU_WA_PARAM_CMD_QUERY,
	MCU_WA_PARAM_CMD_SET,
	MCU_WA_PARAM_CMD_CAPABILITY,
	MCU_WA_PARAM_CMD_DEBUG,
};

enum {
	MCU_WA_PARAM_PDMA_RX = 0x04,
	MCU_WA_PARAM_CPU_UTIL = 0x0b,
	MCU_WA_PARAM_RED = 0x0e,
};

enum mcu_mmps_mode {
	MCU_MMPS_STATIC,
	MCU_MMPS_DYNAMIC,
	MCU_MMPS_RSV,
	MCU_MMPS_DISABLE,
};

struct bss_info_bmc_rate {
	__le16 tag;
	__le16 len;
	__le16 bc_trans;
	__le16 mc_trans;
	u8 short_preamble;
	u8 rsv[7];
} __packed;

struct bss_info_ra {
	__le16 tag;
	__le16 len;
	u8 op_mode;
	u8 adhoc_en;
	u8 short_preamble;
	u8 tx_streams;
	u8 rx_streams;
	u8 algo;
	u8 force_sgi;
	u8 force_gf;
	u8 ht_mode;
	u8 has_20_sta;		/* Check if any sta support GF. */
	u8 bss_width_trigger_events;
	u8 vht_nss_cap;
	u8 vht_bw_signal;	/* not use */
	u8 vht_force_sgi;	/* not use */
	u8 se_off;
	u8 antenna_idx;
	u8 train_up_rule;
	u8 rsv[3];
	unsigned short train_up_high_thres;
	short train_up_rule_rssi;
	unsigned short low_traffic_thres;
	__le16 max_phyrate;
	__le32 phy_cap;
	__le32 interval;
	__le32 fast_interval;
} __packed;

struct bss_info_hw_amsdu {
	__le16 tag;
	__le16 len;
	__le32 cmp_bitmap_0;
	__le32 cmp_bitmap_1;
	__le16 trig_thres;
	u8 enable;
	u8 rsv;
} __packed;

struct bss_info_color {
	__le16 tag;
	__le16 len;
	u8 disable;
	u8 color;
	u8 rsv[2];
} __packed;

struct bss_info_he {
	__le16 tag;
	__le16 len;
	u8 he_pe_duration;
	u8 vht_op_info_present;
	__le16 he_rts_thres;
	__le16 max_nss_mcs[CMD_HE_MCS_BW_NUM];
	u8 rsv[6];
} __packed;

struct bss_info_bcn {
	__le16 tag;
	__le16 len;
	u8 ver;
	u8 enable;
	__le16 sub_ntlv;
} __packed __aligned(4);

struct bss_info_bcn_cntdwn {
	__le16 tag;
	__le16 len;
	u8 cnt;
	u8 rsv[3];
} __packed __aligned(4);

struct bss_info_bcn_mbss {
#define MAX_BEACON_NUM	32
	__le16 tag;
	__le16 len;
	__le32 bitmap;
	__le16 offset[MAX_BEACON_NUM];
	u8 rsv[8];
} __packed __aligned(4);

struct bss_info_bcn_cont {
	__le16 tag;
	__le16 len;
	__le16 tim_ofs;
	__le16 csa_ofs;
	__le16 bcc_ofs;
	__le16 pkt_len;
} __packed __aligned(4);

struct bss_info_inband_discovery {
	__le16 tag;
	__le16 len;
	u8 tx_type;
	u8 tx_mode;
	u8 tx_interval;
	u8 enable;
	__le16 rsv;
	__le16 prob_rsp_len;
} __packed __aligned(4);

enum {
	BSS_INFO_BCN_CSA,
	BSS_INFO_BCN_BCC,
	BSS_INFO_BCN_MBSSID,
	BSS_INFO_BCN_CONTENT,
	BSS_INFO_BCN_DISCOV,
	BSS_INFO_BCN_MAX
};

enum {
	RATE_PARAM_FIXED = 3,
	RATE_PARAM_MMPS_UPDATE = 5,
	RATE_PARAM_FIXED_HE_LTF = 7,
	RATE_PARAM_FIXED_MCS,
	RATE_PARAM_FIXED_GI = 11,
	RATE_PARAM_AUTO = 20,
};

#define RATE_CFG_MCS			GENMASK(3, 0)
#define RATE_CFG_NSS			GENMASK(7, 4)
#define RATE_CFG_GI			GENMASK(11, 8)
#define RATE_CFG_BW			GENMASK(15, 12)
#define RATE_CFG_STBC			GENMASK(19, 16)
#define RATE_CFG_LDPC			GENMASK(23, 20)
#define RATE_CFG_PHY_TYPE		GENMASK(27, 24)
#define RATE_CFG_HE_LTF			GENMASK(31, 28)

enum {
	THERMAL_PROTECT_PARAMETER_CTRL,
	THERMAL_PROTECT_BASIC_INFO,
	THERMAL_PROTECT_ENABLE,
	THERMAL_PROTECT_DISABLE,
	THERMAL_PROTECT_DUTY_CONFIG,
	THERMAL_PROTECT_MECH_INFO,
	THERMAL_PROTECT_DUTY_INFO,
	THERMAL_PROTECT_STATE_ACT,
};

enum {
	MT_BF_SOUNDING_ON = 1,
	MT_BF_TYPE_UPDATE = 20,
	MT_BF_MODULE_UPDATE = 25
};

enum {
	MURU_SET_ARB_OP_MODE = 14,
	MURU_SET_PLATFORM_TYPE = 25,
};

enum {
	MURU_PLATFORM_TYPE_PERF_LEVEL_1 = 1,
	MURU_PLATFORM_TYPE_PERF_LEVEL_2,
};

/* tx cmd tx statistics */
enum {
	MURU_SET_TXC_TX_STATS_EN = 150,
	MURU_GET_TXC_TX_STATS = 151,
};

enum {
	SER_QUERY,
	/* recovery */
	SER_SET_RECOVER_L1,
	SER_SET_RECOVER_L2,
	SER_SET_RECOVER_L3_RX_ABORT,
	SER_SET_RECOVER_L3_TX_ABORT,
	SER_SET_RECOVER_L3_TX_DISABLE,
	SER_SET_RECOVER_L3_BF,
	/* action */
	SER_ENABLE = 2,
	SER_RECOVER
};

#define MT7915_MAX_BEACON_SIZE		512
#define MT7915_MAX_INBAND_FRAME_SIZE	256
#define MT7915_MAX_BSS_OFFLOAD_SIZE	(MT7915_MAX_BEACON_SIZE +	  \
					 MT7915_MAX_INBAND_FRAME_SIZE +	  \
					 MT7915_BEACON_UPDATE_SIZE)

#define MT7915_BSS_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct bss_info_omac) +	\
					 sizeof(struct bss_info_basic) +\
					 sizeof(struct bss_info_rf_ch) +\
					 sizeof(struct bss_info_ra) +	\
					 sizeof(struct bss_info_hw_amsdu) +\
					 sizeof(struct bss_info_he) +	\
					 sizeof(struct bss_info_bmc_rate) +\
					 sizeof(struct bss_info_ext_bss))

#define MT7915_BEACON_UPDATE_SIZE	(sizeof(struct sta_req_hdr) +	\
					 sizeof(struct bss_info_bcn_cntdwn) + \
					 sizeof(struct bss_info_bcn_mbss) + \
					 sizeof(struct bss_info_bcn_cont) + \
					 sizeof(struct bss_info_inband_discovery))

#endif
