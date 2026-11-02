/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2025-2026 MediaTek Inc. */

#ifndef __MT7925_NAN_H
#define __MT7925_NAN_H

#include <linux/if_ether.h>
#include <linux/types.h>

#include "../mt76_connac_mcu.h"

#define NAN_MAX_SOCIAL_CHANNELS		3
#define NAN_ANCHOR_MASTER_RANK_NUM	8
#define NAN_5G_LOW_DISC_CHANNEL		44
#define NAN_5G_HIGH_DISC_CHANNEL	149
#define NAN_MAX_MASTER_PREFERENCE	255
#define NAN_DEFAULT_DW_INTERVAL		1
#define NAN_DEFAULT_DISC_BCN_INTERVAL	100
#define NAN_TOTAL_DW			16
#define NAN_SUPPORTED_2G_FAW_CH_NUM	4
#define NAN_SUPPORTED_5G_FAW_CH_NUM	4
#define NAN_TIMELINE_MGMT_SIZE		2
#define NAN_TIMELINE_MGMT_CHNL_LIST_NUM				\
	((NAN_SUPPORTED_2G_FAW_CH_NUM +				\
	  NAN_SUPPORTED_5G_FAW_CH_NUM) / NAN_TIMELINE_MGMT_SIZE)
#define NAN_NUM_AVAIL_DB		2
#define NAN_NDC_ATTRIBUTE_ID_LENGTH	6
#define NAN_MAX_CONN_CFG		8
#define NAN_MAX_NDP_CXT			4

#define MT7925_NAN_ENABLE_MAX_SIZE					\
	(sizeof(struct mt7925_nan_common_hdr) +				\
	 sizeof(struct mt7925_nan_enable_req_tlv) +			\
	 sizeof(struct mt7925_nan_dw_interval_tlv) +			\
	 sizeof(struct mt7925_nan_cluster_id_tlv) +			\
	 sizeof(struct mt7925_nan_sync_rssi_tlv))

#define MT7925_NAN_CONF_MAX_SIZE					\
	(sizeof(struct mt7925_nan_common_hdr) +				\
	 sizeof(struct mt7925_nan_master_preference_tlv) +		\
	 sizeof(struct mt7925_nan_dw_interval_tlv) +			\
	 sizeof(struct mt7925_nan_cluster_id_tlv) +			\
	 sizeof(struct mt7925_nan_sync_rssi_tlv))

#define MT7925_NAN_AVAIL_MAX_SIZE					\
	(sizeof(struct mt7925_nan_common_hdr) +				\
	 sizeof(struct mt7925_nan_avail_ctrl_tlv) +			\
	 sizeof(struct mt7925_nan_avail_entry_tlv))

#define MT7925_NAN_PEER_MAX_SIZE					\
	(sizeof(struct mt7925_nan_common_hdr) +				\
	 sizeof(struct mt7925_nan_sched_manage_peer_rec_tlv) +		\
	 sizeof(struct mt7925_nan_sched_update_peer_cap_tlv) +		\
	 sizeof(struct mt7925_nan_sched_update_crb_tlv) +		\
	 sizeof(struct mt7925_nan_update_ulw_tlv))

/* NAN Availability Attribute */
#define NAN_AVAIL_ATTR_ID_OFFSET	0
#define NAN_AVAIL_ATTR_LEN_OFFSET	1
#define NAN_AVAIL_SEQ_ID_OFFSET		3
#define NAN_AVAIL_ATTR_CTRL_OFFSET	4

/* NAN Availability Attribute - Attribute Control Field */
#define NAN_AVAIL_CTRL_MAPID			GENMASK(3, 0)
#define NAN_AVAIL_CTRL_COMMIT_CHANGED		BIT(4)
#define NAN_AVAIL_CTRL_POTN_CHANGED		BIT(5)
#define NAN_AVAIL_CTRL_PUBLIC_AVAIL_CHANGED	BIT(6)
#define NAN_AVAIL_CTRL_NDC_CHANGED		BIT(7)
#define NAN_AVAIL_CTRL_CHECK_FOR_CHANGED	GENMASK(7, 4)

#define UNII1_LOWER_BOUND	36
#define UNII1_UPPER_BOUND	50
#define UNII3_LOWER_BOUND	149
#define UNII3_UPPER_BOUND	165

#define NAN_BSS_INDEX_BAND0 0

enum nan_uni_cmd_tag {
	NAN_UNI_CMD_SET_MASTER_PREFERENCE	= 0,
	NAN_UNI_CMD_ENABLE_REQUEST		= 7,
	NAN_UNI_CMD_DISABLE_REQUEST		= 8,
	NAN_UNI_CMD_UPDATE_AVAILABILITY		= 9,
	NAN_UNI_CMD_UPDATE_CRB			= 10,
	NAN_UNI_CMD_MANAGE_PEER_SCH_RECORD	= 12,
	NAN_UNI_CMD_MAP_STA_RECORD		= 13,
	NAN_UNI_CMD_UPDATE_ULW			= 16,
	NAN_UNI_CMD_UPDATE_PHY_SETTING		= 18,
	NAN_UNI_CMD_UPDATE_AVAILABILITY_CTRL	= 20,
	NAN_UNI_CMD_UPDATE_PEER_CAPABILITY	= 21,
	NAN_UNI_CMD_CHANGE_NMI_ADDRESS		= 24,
	NAN_UNI_CMD_SET_DW_INTERVAL		= 26,
	NAN_UNI_CMD_SET_SYNC_RSSI		= 39,
	NAN_UNI_CMD_SET_CLUSTER_ID		= 40,
	NAN_UNI_CMD_KEY_MANAGEMENT		= 53,
};

enum nan_uni_event_tag {
	NAN_UNI_EVENT_ID_DE_EVENT_IND		= 19,
	NAN_UNI_EVENT_ID_ULW_UPDATE		= 39,
	NAN_UNI_EVENT_ID_SCHED_UPDATE_DONE	= 43,
	NAN_UNI_EVENT_REPORT_DW_START		= 59,
	NAN_UNI_EVENT_REPORT_DW_END		= 60,
};

enum nan_disc_event_type {
	NAN_EVENT_ID_DISC_MAC_ADDR		= 0,
	NAN_EVENT_ID_STARTED_CLUSTER		= 1,
	NAN_EVENT_ID_JOINED_CLUSTER		= 2,
};

/* bit indices into mt792x_dev->nan_deferred_pending, set from the atomic
 * MCU-event RX path and consumed by mt7925_nan_deferred_work()
 */
enum mt7925_nan_deferred_event {
	MT7925_NAN_DEFERRED_STARTED_CLUSTER,
	MT7925_NAN_DEFERRED_SCHED_UPDATE_DONE,
};

/* NAN 4.0 Table 79. Device Capability attribute format, Supported Bands */
enum nan_supported_bands {
	NAN_SUPPORTED_BAND_ID_2P4G = 2,
	NAN_SUPPORTED_BAND_ID_5G = 4,
	NAN_PROPRIETARY_BAND_ID_6G = 6,
	NAN_SUPPORTED_BAND_ID_6G = 7,
};

enum nan_peer_supported_bands {
	NAN_SUPPORTED_BN_2G = 0,
	NAN_SUPPORTED_BN_5G_LOW,
	NAN_SUPPORTED_BN_5G_HIGH,
	NAN_SUPPORTED_BN_6G,
	NAN_SUPPORTED_BN_NUM
};

#define NAN_CH_CTRL_CH_TYPE		BIT(0)
#define NAN_CH_CTRL_OP_CLASS		GENMASK(15, 8)
#define NAN_CH_CTRL_PRIMARY_CH		GENMASK(23, 16)

#define NAN_CRB_USE_DATA_PATH		BIT(0)
#define NAN_CRB_AVAIL_6G_FORMAT		GENMASK(2, 1)

#define NAN_BAND_CHANNEL_ENTRY_LIST_TYPE_CHANNEL	1

struct mt7925_nan_social_ch_scan_params {
	u8 dwell_time[NAN_MAX_SOCIAL_CHANNELS];
	__le16 scan_period[NAN_MAX_SOCIAL_CHANNELS];
} __packed;

/* Firmware-reported NAN device information */
struct nan_dev_info_evt {
	u8 is_enabled;
	u8 my_addr[ETH_ALEN];
	u8 en_fw_election;
	__le32 nan_dev_role;
	__le32 nan_dev_state;
	u8 mst_preference;
	u8 random_factor;
	u8 cnt_hop;
	u8 cluster_id[ETH_ALEN];
	u8 anchor_mst_addr[ETH_ALEN];
	u8 am_preference;
	u8 am_random_factor;
	u8 parent_mac[ETH_ALEN];
	u8 parent_am_preference;
	u8 parent_am_factor;
	__le32 ambtt;
	__le32 tsf[2];
	u8 pn_igtk[6];
	u8 pn_bigtk[6];
};

/* Firmware NAN discovery window event */
struct nan_rpt_dw_evt {
	struct nan_dev_info_evt device_info;
	__le32 expected_tsf_h;
	__le32 expected_tsf_l;
	__le32 actual_tsf_h;
	__le32 actual_tsf_l;
	__le16 channel;
	__le16 dw_num;
};

#define NAN_ULW_ATTR_ID		0x17
#define NAN_ULW_FIXED_PAYLOAD	16	/* Table 109 fixed fields */

struct mt7925_nan_ulw_event {
	u8 sched_id;
	u8 seq_id;
	u8 count_down;
	u8 ulw_overwrite;
	__le32 start_time;
	__le32 duration;
	__le32 period;
} __packed;

/* ULW attribute laid out in NAN spec Table 109 field order */
struct mt7925_nan_ulw_attr {
	u8 attr_id;
	__le16 length;
	u8 sched_id;
	u8 seq_id;
	__le32 start_time;
	__le32 duration;
	__le32 period;
	u8 count_down;
	u8 ulw_overwrite;
} __packed;

struct mt7925_nan_conf_dw {
	u8 config_2dot4g_dw_band;
	__le32 dw_2dot4g_interval_val;

	u8 config_5g_dw_band;
	__le32 dw_5g_interval_val;
} __packed;

struct mt7925_nan_enable_req_tlv {
	__le16 tag;
	__le16 len;

	u8 master_pref;
	__le16 cluster_low;
	__le16 cluster_high;

	u8 config_support_5g;
	u8 support_5g_val;

	u8 config_sid_beacon;
	u8 sid_beacon_val;

	u8 config_2dot4g_rssi_close;
	u8 rssi_close_2dot4g_val;
	u8 config_2dot4g_rssi_middle;
	u8 rssi_middle_2dot4g_val;

	u8 config_2dot4g_rssi_proximity;
	u8 rssi_proximity_2dot4g_val;
	u8 config_hop_count_limit;
	u8 hop_count_limit_val;

	u8 config_2dot4g_support;
	u8 support_2dot4g_val;

	u8 config_2dot4g_beacons;
	u8 beacon_2dot4g_val;

	u8 config_2dot4g_sdf;
	u8 sdf_2dot4g_val;

	u8 config_5g_beacons;
	u8 beacon_5g_val;

	u8 config_5g_sdf;
	u8 sdf_5g_val;

	u8 config_5g_rssi_close;
	u8 rssi_close_5g_val;

	u8 config_5g_rssi_middle;
	u8 rssi_middle_5g_val;

	u8 config_5g_rssi_close_proximity;
	u8 rssi_close_proximity_5g_val;

	u8 config_rssi_window_size;
	u8 rssi_window_size_val;

	u8 config_oui;
	__le32 oui_val;

	u8 config_intf_addr;
	u8 intf_addr_val[ETH_ALEN];

	u8 config_cluster_attribute_val;

	u8 config_scan_params;
	struct mt7925_nan_social_ch_scan_params scan_params_val;

	u8 config_random_factor_force;
	u8 random_factor_force_val;

	u8 config_hop_count_force;
	u8 hop_count_force_val;

	u8 config_24g_channel;
	__le32 channel_24g_val;

	u8 config_5g_channel;
	__le32 channel_5g_val;

	struct mt7925_nan_conf_dw config_dw;

	u8 config_disc_mac_addr_randomization;
	__le32 disc_mac_addr_rand_interval_sec;

	u8 discovery_indication_cfg;

	u8 config_subscribe_sid_beacon;
	__le32 subscribe_sid_beacon_val;

	u8 enable_log_slot_statistics;
} __packed __aligned(4);

struct mt7925_nan_common_hdr {
	u8 reserved[4];
};

struct mt7925_nan_master_preference_tlv {
	__le16 tag;
	__le16 len;
	u8 master_preference;
	u8 reserved[3];
} __packed __aligned(4);

struct mt7925_nan_dw_interval_tlv {
	__le16 tag;
	__le16 len;
	u8 dw_interval;
	u8 vendor_ioctl;
	__le16 disc_bcn_interval;
} __packed __aligned(4);

struct mt7925_nan_cluster_id_tlv {
	__le16 tag;
	__le16 len;
	u8 cluster_id[ETH_ALEN];
	u8 reserved[2];
} __packed __aligned(4);

struct mt7925_nan_sync_rssi_tlv {
	__le16 tag;
	__le16 len;
	s8 rssi_close_2g;
	s8 rssi_middle_2g;
	s8 rssi_close_5g;
	s8 rssi_middle_5g;
} __packed __aligned(4);

struct mt7925_nan_de_event {
	u8 event_type;
	u8 cluster_id[ETH_ALEN];
	u8 anchor_master_rank[NAN_ANCHOR_MASTER_RANK_NUM];
	u8 own_nmi[ETH_ALEN];
	u8 master_nmi[ETH_ALEN];
};

struct mt7925_nan_nmi_addr_tlv {
	__le16 tag;
	__le16 len;
	u8 nmi_addr[ETH_ALEN];
} __packed __aligned(4);

struct mt7925_nan_avail_ctrl_tlv {
	__le16 tag;
	__le16 len;
	__le16 avail_ctrl;
	u8 seq_id;
	u8 is_deferred;
} __packed __aligned(4);

struct mt7925_nan_ch_timeline {
	u8 is_valid;
	u8 reserved[3];

	__le32 ch_info;

	__le32 num;
	__le32 avail_map[NAN_TOTAL_DW];
};

struct mt7925_nan_avail_entry_tlv {
	__le16 tag;
	__le16 len;
	u8 map_id;
	u8 is_cond_avail;
	u8 timeline_idx;
	u8 is_multi_map;

	struct mt7925_nan_ch_timeline ch_list[NAN_TIMELINE_MGMT_CHNL_LIST_NUM];
} __packed __aligned(4);

struct mt7925_nan_sched_manage_peer_rec_tlv {
	__le16 tag;
	__le16 len;
	__le32 sch_idx;
	u8 is_activate;
	u8 nmi_addr[ETH_ALEN];
	u8 reserved[1];
} __packed __aligned(4);

struct mt7925_nan_sched_update_peer_cap_tlv {
	__le16 tag;
	__le16 len;
	__le32 sch_idx;
	u8 supported_bands;
	__le16 max_chnl_switch_time;
	u8 peer_supported_bands;
} __packed __aligned(4);

struct mt7925_nan_sched_timeline {
	u8 map_id;
	u8 reserved[3];
	union {
		__le32 avail_map[NAN_TOTAL_DW];
		u8 avail_block[NAN_TOTAL_DW * 4];
	};
};

struct mt7925_nan_sched_faw_ndc_timeline {
	__le32 avail_map[NAN_TOTAL_DW];
};

struct mt7925_nan_sched_ndc_ctrl {
	u8 is_valid;
	u8 ndc_id[NAN_NDC_ATTRIBUTE_ID_LENGTH];
	u8 ndc_idx;
	struct mt7925_nan_sched_timeline timeline[NAN_NUM_AVAIL_DB];
};

struct mt7925_nan_sched_update_crb_tlv {
	__le16 tag;
	__le16 len;
	__le32 sch_idx;
	u8 flags;
	u8 is_use_ranging;
	u8 reserved[2];
	struct mt7925_nan_sched_timeline comm_ranging_timeline[NAN_TIMELINE_MGMT_SIZE];
	struct mt7925_nan_sched_timeline comm_faw_timeline[NAN_TIMELINE_MGMT_SIZE];
	struct mt7925_nan_sched_ndc_ctrl comm_ndc_ctrl;
	struct mt7925_nan_sched_faw_ndc_timeline faw_ndc_timeline[NAN_TIMELINE_MGMT_SIZE];
} __packed __aligned(4);

#define NAN_ULW_MAX_SIZE	256

struct mt7925_nan_update_ulw_tlv {
	__le16 tag;
	__le16 len;
	u8 nmi_addr[ETH_ALEN];
	u8 ulw_attr[NAN_ULW_MAX_SIZE];
} __packed __aligned(4);

struct mt7925_nan_sched_map_sta_rec_tlv {
	__le16 tag;
	__le16 len;
	u8 nmi_addr[ETH_ALEN];
	u8 sta_rec_idx;
	u8 ndp_ctx_id;

	__le32 role_idx;
	u8 ndi_addr[ETH_ALEN];
	u8 reserved[2];
} __packed __aligned(4);

/* Matches FW NAN_PHY_SETTING_T (enum fields are 4 bytes on ARM) */
struct mt7925_nan_phy_setting {
	u8 phy_type_set;
	u8 non_ht_basic_phy_type;
	u8 use_short_preamble;
	u8 use_short_slot_time;
	__le16 operational_rate_set;
	__le16 bss_basic_rate_set;
	__le16 vht_basic_mcs_set;
	u8 erp_protect_mode;
	u8 ht_op_info1;
	__le16 ht_op_info2;
	__le16 ht_op_info3;
	__le32 ht_protect_mode;
	__le32 gf_operation_mode;
	__le32 rifs_operation_mode;
} __packed;

/* Matches FW NAN_SCHED_CMD_UPDATE_PHY_PARAM_T */
struct mt7925_nan_update_phy_setting_tlv {
	__le16 tag;
	__le16 len;
	struct mt7925_nan_phy_setting phy_2g;
	struct mt7925_nan_phy_setting phy_5g;
} __packed __aligned(4);

int mt7925_nan_update_phy_setting(struct mt792x_dev *dev);

struct ieee80211_chanctx_conf *
mt7925_nan_seed_link_sta(struct mt792x_dev *dev,
			 struct ieee80211_link_sta *link_sta);

int mt7925_nan_enable(struct ieee80211_vif *vif,
		      struct mt792x_dev *dev,
		      struct cfg80211_nan_conf *conf);

int mt7925_nan_disable(struct ieee80211_vif *vif,
		       struct mt792x_dev *dev);

int mt7925_nan_change_configure(struct ieee80211_vif *vif,
				struct mt792x_dev *dev,
				struct cfg80211_nan_conf *conf);

void mt7925_nan_mcu_event(struct mt792x_dev *dev, struct sk_buff *skb);

void mt7925_nan_local_sched_changed(struct mt792x_dev *dev,
				    struct ieee80211_vif *vif);

int mt792x_nan_set_peer_schedule(struct mt792x_dev *dev,
				 struct ieee80211_sta *sta);

int mt792x_nan_set_peer_rec(struct mt76_dev *mdev,
			    struct ieee80211_sta *sta);

int mt792x_nan_map_sta_rec(struct mt76_dev *mdev,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);

#endif
