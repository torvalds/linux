/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2023 MediaTek Inc. */

#ifndef __MT7925_MCU_H
#define __MT7925_MCU_H

#include "../mt76_connac_mcu.h"

/* ext event table */
enum {
	MCU_EXT_EVENT_RATE_REPORT = 0x87,
};

struct mt7925_mcu_eeprom_info {
	__le32 addr;
	__le32 valid;
	u8 data[MT7925_EEPROM_BLOCK_SIZE];
} __packed;

#define MT_RA_RATE_NSS			GENMASK(8, 6)
#define MT_RA_RATE_MCS			GENMASK(3, 0)
#define MT_RA_RATE_TX_MODE		GENMASK(12, 9)
#define MT_RA_RATE_DCM_EN		BIT(4)
#define MT_RA_RATE_BW			GENMASK(14, 13)

struct mt7925_mcu_rxd {
	__le32 rxd[8];

	__le16 len;
	__le16 pkt_type_id;

	u8 eid;
	u8 seq;
	u8 option;
	u8 __rsv;

	u8 ext_eid;
	u8 __rsv1[2];
	u8 s2d_index;

	u8 tlv[];
};

struct mt7925_mcu_uni_event {
	u8 cid;
	u8 pad[3];
	__le32 status; /* 0: success, others: fail */
} __packed;

enum {
	MT_EBF = BIT(0),	/* explicit beamforming */
	MT_IBF = BIT(1)		/* implicit beamforming */
};

struct mt7925_mcu_reg_event {
	__le32 reg;
	__le32 val;
} __packed;

struct mt7925_mcu_ant_id_config {
	u8 ant_id[4];
} __packed;

struct mt7925_txpwr_req {
	u8 _rsv[4];
	__le16 tag;
	__le16 len;

	u8 format_id;
	u8 catg;
	u8 band_idx;
	u8 _rsv1;
} __packed;

struct mt7925_txpwr_event {
	u8 rsv[4];
	__le16 tag;
	__le16 len;

	u8 catg;
	u8 band_idx;
	u8 ch_band;
	u8 format; /* 0:Legacy, 1:HE */

	/* Rate power info */
	struct mt7925_txpwr txpwr;

	s8 pwr_max;
	s8 pwr_min;
	u8 rsv1;
} __packed;

enum {
	TM_SWITCH_MODE,
	TM_SET_AT_CMD,
	TM_QUERY_AT_CMD,
};

enum {
	MT7925_TM_NORMAL,
	MT7925_TM_TESTMODE,
	MT7925_TM_ICAP,
	MT7925_TM_ICAP_OVERLAP,
	MT7925_TM_WIFISPECTRUM,
};

struct mt7925_rftest_cmd {
	u8 action;
	u8 rsv[3];
	__le32 param0;
	__le32 param1;
} __packed;

struct mt7925_rftest_evt {
	__le32 param0;
	__le32 param1;
} __packed;

enum {
	UNI_CHANNEL_SWITCH,
	UNI_CHANNEL_RX_PATH,
};

enum {
	UNI_CHIP_CONFIG_CHIP_CFG = 0x2,
	UNI_CHIP_CONFIG_NIC_CAPA = 0x3,
};

enum {
	UNI_BAND_CONFIG_RADIO_ENABLE,
	UNI_BAND_CONFIG_RTS_THRESHOLD = 0x08,
	UNI_BAND_CONFIG_SET_MAC80211_RX_FILTER = 0x0C,
};

enum {
	UNI_WSYS_CONFIG_FW_LOG_CTRL,
	UNI_WSYS_CONFIG_FW_DBG_CTRL,
};

enum {
	UNI_EFUSE_ACCESS = 1,
	UNI_EFUSE_BUFFER_MODE,
	UNI_EFUSE_FREE_BLOCK,
	UNI_EFUSE_BUFFER_RD,
};

enum {
	UNI_CMD_ACCESS_REG_BASIC = 0x0,
	UNI_CMD_ACCESS_RF_REG_BASIC,
};

enum {
	UNI_MBMC_SETTING,
};

enum {
	UNI_EVENT_SCAN_DONE_BASIC = 0,
	UNI_EVENT_SCAN_DONE_CHNLINFO = 2,
	UNI_EVENT_SCAN_DONE_NLO = 3,
};

enum connac3_mcu_cipher_type {
	CONNAC3_CIPHER_NONE = 0,
	CONNAC3_CIPHER_WEP40 = 1,
	CONNAC3_CIPHER_TKIP = 2,
	CONNAC3_CIPHER_AES_CCMP = 4,
	CONNAC3_CIPHER_WEP104 = 5,
	CONNAC3_CIPHER_BIP_CMAC_128 = 6,
	CONNAC3_CIPHER_WEP128 = 7,
	CONNAC3_CIPHER_WAPI = 8,
	CONNAC3_CIPHER_CCMP_256 = 10,
	CONNAC3_CIPHER_GCMP = 11,
	CONNAC3_CIPHER_GCMP_256 = 12,
};

struct mt7925_mcu_scan_chinfo_event {
	u8 nr_chan;
	u8 alpha2[3];
} __packed;

enum {
	UNI_SCAN_REQ = 1,
	UNI_SCAN_CANCEL = 2,
	UNI_SCAN_SCHED_REQ = 3,
	UNI_SCAN_SCHED_ENABLE = 4,
	UNI_SCAN_SSID = 10,
	UNI_SCAN_BSSID,
	UNI_SCAN_CHANNEL,
	UNI_SCAN_IE,
	UNI_SCAN_MISC,
	UNI_SCAN_SSID_MATCH_SETS,
};

enum {
	UNI_SNIFFER_ENABLE,
	UNI_SNIFFER_CONFIG,
};

struct scan_hdr_tlv {
	/* fixed field */
	u8 seq_num;
	u8 bss_idx;
	u8 pad[2];
	/* tlv */
	u8 data[];
} __packed;

struct scan_req_tlv {
	__le16 tag;
	__le16 len;

	u8 scan_type; /* 0: PASSIVE SCAN
		       * 1: ACTIVE SCAN
		       */
	u8 probe_req_num; /* Number of probe request for each SSID */
	u8 scan_func; /* BIT(0) Enable random MAC scan
		       * BIT(1) Disable DBDC scan type 1~3.
		       * BIT(2) Use DBDC scan type 3 (dedicated one RF to scan).
		       */
	u8 src_mask;
	__le16 channel_min_dwell_time;
	__le16 channel_dwell_time; /* channel Dwell interval */
	__le16 timeout_value;
	__le16 probe_delay_time;
	u8 func_mask_ext;
};

struct scan_ssid_tlv {
	__le16 tag;
	__le16 len;

	u8 ssid_type; /* BIT(0) wildcard SSID
		       * BIT(1) P2P wildcard SSID
		       * BIT(2) specified SSID + wildcard SSID
		       * BIT(2) + ssid_type_ext BIT(0) specified SSID only
		       */
	u8 ssids_num;
	u8 pad[2];
	struct mt76_connac_mcu_scan_ssid ssids[4];
};

struct scan_bssid_tlv {
	__le16 tag;
	__le16 len;

	u8 bssid[ETH_ALEN];
	u8 match_ch;
	u8 match_ssid_ind;
	u8 rcpi;
	u8 pad[3];
};

struct scan_chan_info_tlv {
	__le16 tag;
	__le16 len;

	u8 channel_type; /* 0: Full channels
			  * 1: Only 2.4GHz channels
			  * 2: Only 5GHz channels
			  * 3: P2P social channel only (channel #1, #6 and #11)
			  * 4: Specified channels
			  * Others: Reserved
			  */
	u8 channels_num; /* valid when channel_type is 4 */
	u8 pad[2];
	struct mt76_connac_mcu_scan_channel channels[64];
};

struct scan_ie_tlv {
	__le16 tag;
	__le16 len;

	__le16 ies_len;
	u8 band;
	u8 pad;
	u8 ies[MT76_CONNAC_SCAN_IE_LEN];
};

struct scan_misc_tlv {
	__le16 tag;
	__le16 len;

	u8 random_mac[ETH_ALEN];
	u8 rsv[2];
};

struct scan_sched_req {
	__le16 tag;
	__le16 len;

	u8 version;
	u8 stop_on_match;
	u8 intervals_num;
	u8 scan_func;
	__le16 intervals[MT76_CONNAC_MAX_NUM_SCHED_SCAN_INTERVAL];
};

struct scan_sched_ssid_match_sets {
	__le16 tag;
	__le16 len;

	u8 match_num;
	u8 rsv[3];

	struct mt76_connac_mcu_scan_match match[MT76_CONNAC_MAX_SCAN_MATCH];
};

struct scan_sched_enable {
	__le16 tag;
	__le16 len;

	u8 active;
	u8 rsv[3];
};

struct mbmc_set_req {
	u8 pad[4];
	u8 data[];
} __packed;

struct mbmc_conf_tlv {
	__le16 tag;
	__le16 len;

	u8 mbmc_en;
	u8 band;
	u8 pad[2];
} __packed;

struct edca {
	__le16 tag;
	__le16 len;

	u8 queue;
	u8 set;
	u8 cw_min;
	u8 cw_max;
	__le16 txop;
	u8 aifs;
	u8 __rsv;
};

struct bss_req_hdr {
	u8 bss_idx;
	u8 __rsv[3];
} __packed;

struct bss_rate_tlv {
	__le16 tag;
	__le16 len;
	u8 __rsv1[2];
	__le16 basic_rate;
	__le16 bc_trans;
	__le16 mc_trans;
	u8 short_preamble;
	u8 bc_fixed_rate;
	u8 mc_fixed_rate;
	u8 __rsv2;
} __packed;

struct bss_mld_tlv {
	__le16 tag;
	__le16 len;
	u8 group_mld_id;
	u8 own_mld_id;
	u8 mac_addr[ETH_ALEN];
	u8 remap_idx;
	u8 link_id;
	u8 __rsv[2];
} __packed;

struct sta_rec_ba_uni {
	__le16 tag;
	__le16 len;
	u8 tid;
	u8 ba_type;
	u8 amsdu;
	u8 ba_en;
	__le16 ssn;
	__le16 winsize;
	u8 ba_rdd_rro;
	u8 __rsv[3];
} __packed;

struct sta_rec_eht {
	__le16 tag;
	__le16 len;
	u8 tid_bitmap;
	u8 _rsv;
	__le16 mac_cap;
	__le64 phy_cap;
	__le64 phy_cap_ext;
	u8 mcs_map_bw20[4];
	u8 mcs_map_bw80[3];
	u8 mcs_map_bw160[3];
	u8 mcs_map_bw320[3];
	u8 _rsv2[3];
} __packed;

struct sta_rec_sec_uni {
	__le16 tag;
	__le16 len;
	u8 add;
	u8 tx_key;
	u8 key_type;
	u8 is_authenticator;
	u8 peer_addr[6];
	u8 bss_idx;
	u8 cipher_id;
	u8 key_id;
	u8 key_len;
	u8 wlan_idx;
	u8 mgmt_prot;
	u8 key[32];
	u8 key_rsc[16];
} __packed;

struct sta_rec_hdr_trans {
	__le16 tag;
	__le16 len;
	u8 from_ds;
	u8 to_ds;
	u8 dis_rx_hdr_tran;
	u8 rsv;
} __packed;

struct sta_rec_mld {
	__le16 tag;
	__le16 len;
	u8 mac_addr[ETH_ALEN];
	__le16 primary_id;
	__le16 secondary_id;
	__le16 wlan_id;
	u8 link_num;
	u8 rsv[3];
	struct {
		__le16 wlan_id;
		u8 bss_idx;
		u8 rsv;
	} __packed link[2];
} __packed;

struct bss_ifs_time_tlv {
	__le16 tag;
	__le16 len;
	u8 slot_valid;
	u8 sifs_valid;
	u8 rifs_valid;
	u8 eifs_valid;
	__le16 slot_time;
	__le16 sifs_time;
	__le16 rifs_time;
	__le16 eifs_time;
	u8 eifs_cck_valid;
	u8 rsv;
	__le16 eifs_cck_time;
} __packed;

#define MT7925_STA_UPDATE_MAX_SIZE	(sizeof(struct sta_req_hdr) +		\
					 sizeof(struct sta_rec_basic) +		\
					 sizeof(struct sta_rec_bf) +		\
					 sizeof(struct sta_rec_ht) +		\
					 sizeof(struct sta_rec_he_v2) +		\
					 sizeof(struct sta_rec_ba_uni) +	\
					 sizeof(struct sta_rec_vht) +		\
					 sizeof(struct sta_rec_uapsd) +		\
					 sizeof(struct sta_rec_amsdu) +		\
					 sizeof(struct sta_rec_bfee) +		\
					 sizeof(struct sta_rec_phy) +		\
					 sizeof(struct sta_rec_ra) +		\
					 sizeof(struct sta_rec_sec_uni) +   \
					 sizeof(struct sta_rec_ra_fixed) +	\
					 sizeof(struct sta_rec_he_6g_capa) +	\
					 sizeof(struct sta_rec_eht) +		\
					 sizeof(struct sta_rec_hdr_trans) +	\
					 sizeof(struct sta_rec_mld) +		\
					 sizeof(struct tlv))

#define MT7925_BSS_UPDATE_MAX_SIZE	(sizeof(struct bss_req_hdr) +		\
					 sizeof(struct mt76_connac_bss_basic_tlv) +	\
					 sizeof(struct mt76_connac_bss_qos_tlv) +	\
					 sizeof(struct bss_rate_tlv) +			\
					 sizeof(struct bss_mld_tlv) +			\
					 sizeof(struct bss_info_uni_he) +		\
					 sizeof(struct bss_info_uni_bss_color) +	\
					 sizeof(struct bss_ifs_time_tlv) +		\
					 sizeof(struct tlv))

#define MT_CONNAC3_SKU_POWER_LIMIT      449
struct mt7925_sku_tlv {
	u8 channel;
	s8 pwr_limit[MT_CONNAC3_SKU_POWER_LIMIT];
} __packed;

struct mt7925_tx_power_limit_tlv {
	u8 rsv[4];

	__le16 tag;
	__le16 len;

	/* DW0 - common info*/
	u8 ver;
	u8 pad0;
	__le16 rsv1;
	/* DW1 - cmd hint */
	u8 n_chan; /* # channel */
	u8 band; /* 2.4GHz - 5GHz - 6GHz */
	u8 last_msg;
	u8 limit_type;
	/* DW3 */
	u8 alpha2[4]; /* regulatory_request.alpha2 */
	u8 pad2[32];

	u8 data[];
} __packed;

struct mt7925_arpns_tlv {
	__le16 tag;
	__le16 len;

	u8 enable;
	u8 ips_num;
	u8 rsv[2];
} __packed;

struct mt7925_wow_pattern_tlv {
	__le16 tag;
	__le16 len;
	u8 bss_idx;
	u8 index; /* pattern index */
	u8 enable; /* 0: disable
		    * 1: enable
		    */
	u8 data_len; /* pattern length */
	u8 offset;
	u8 mask[MT76_CONNAC_WOW_MASK_MAX_LEN];
	u8 pattern[MT76_CONNAC_WOW_PATTEN_MAX_LEN];
	u8 rsv[4];
} __packed;

static inline enum connac3_mcu_cipher_type
mt7925_mcu_get_cipher(int cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return CONNAC3_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return CONNAC3_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return CONNAC3_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return CONNAC3_CIPHER_BIP_CMAC_128;
	case WLAN_CIPHER_SUITE_CCMP:
		return CONNAC3_CIPHER_AES_CCMP;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return CONNAC3_CIPHER_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return CONNAC3_CIPHER_GCMP;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return CONNAC3_CIPHER_GCMP_256;
	case WLAN_CIPHER_SUITE_SMS4:
		return CONNAC3_CIPHER_WAPI;
	default:
		return CONNAC3_CIPHER_NONE;
	}
}

int mt7925_mcu_set_dbdc(struct mt76_phy *phy);
int mt7925_mcu_hw_scan(struct mt76_phy *phy, struct ieee80211_vif *vif,
		       struct ieee80211_scan_request *scan_req);
int mt7925_mcu_cancel_hw_scan(struct mt76_phy *phy,
			      struct ieee80211_vif *vif);
int mt7925_mcu_sched_scan_req(struct mt76_phy *phy,
			      struct ieee80211_vif *vif,
			      struct cfg80211_sched_scan_request *sreq);
int mt7925_mcu_sched_scan_enable(struct mt76_phy *phy,
				 struct ieee80211_vif *vif,
				 bool enable);
int mt7925_mcu_add_bss_info(struct mt792x_phy *phy,
			    struct ieee80211_chanctx_conf *ctx,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    int enable);
int mt7925_mcu_set_timing(struct mt792x_phy *phy,
			  struct ieee80211_vif *vif);
int mt7925_mcu_set_deep_sleep(struct mt792x_dev *dev, bool enable);
int mt7925_mcu_set_channel_domain(struct mt76_phy *phy);
int mt7925_mcu_set_radio_en(struct mt792x_phy *phy, bool enable);
int mt7925_mcu_set_chctx(struct mt76_phy *phy, struct mt76_vif *mvif,
			 struct ieee80211_chanctx_conf *ctx);
int mt7925_mcu_set_rate_txpower(struct mt76_phy *phy);
int mt7925_mcu_update_arp_filter(struct mt76_dev *dev,
				 struct mt76_vif *vif,
				 struct ieee80211_bss_conf *info);
#endif
