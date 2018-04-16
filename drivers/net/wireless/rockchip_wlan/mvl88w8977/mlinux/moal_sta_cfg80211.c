/** @file moal_sta_cfg80211.c
  *
  * @brief This file contains the functions for STA CFG80211.
  *
  * Copyright (C) 2011-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

#include "moal_cfg80211.h"
#include "moal_cfgvendor.h"
#include "moal_sta_cfg80211.h"
#include "moal_eth_ioctl.h"
#ifdef UAP_SUPPORT
#include    "moal_uap.h"
#endif

extern int cfg80211_wext;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
extern int fw_region;
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
extern int dfs_offload;
#endif

extern int cntry_txpwr;

/* Supported crypto cipher suits to be advertised to cfg80211 */
const u32 cfg80211_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_SMS4,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int woal_cfg80211_set_monitor_channel(struct wiphy *wiphy,
					     struct cfg80211_chan_def *chandef);
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
static void
#else
static int
#endif

woal_cfg80211_reg_notifier(struct wiphy *wiphy,
			   struct regulatory_request *request);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
static int woal_cfg80211_scan(struct wiphy *wiphy,
			      struct cfg80211_scan_request *request);
#else
static int woal_cfg80211_scan(struct wiphy *wiphy, struct net_device *dev,
			      struct cfg80211_scan_request *request);
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4,5,0)
static void woal_cfg80211_abort_scan(struct wiphy *wiphy,
				     struct wireless_dev *wdev);
#endif
static int woal_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
				 struct cfg80211_connect_params *sme);

static int woal_cfg80211_disconnect(struct wiphy *wiphy,
				    struct net_device *dev, t_u16 reason_code);

static int woal_cfg80211_get_station(struct wiphy *wiphy,
				     struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
				     const t_u8 *mac,
#else
				     t_u8 *mac,
#endif
				     struct station_info *sinfo);

static int woal_cfg80211_dump_station(struct wiphy *wiphy,
				      struct net_device *dev, int idx,
				      t_u8 *mac, struct station_info *sinfo);

static int woal_cfg80211_dump_survey(struct wiphy *wiphy,
				     struct net_device *dev, int idx,
				     struct survey_info *survey);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int woal_cfg80211_get_channel(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     struct cfg80211_chan_def *chandef);
#endif
static int woal_cfg80211_set_power_mgmt(struct wiphy *wiphy,
					struct net_device *dev, bool enabled,
					int timeout);
#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
static int woal_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
					     struct net_device *dev,
					     s32 rssi_thold, u32 rssi_hyst);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
static int woal_cfg80211_get_tx_power(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
				      struct wireless_dev *wdev,
#endif
				      int *dbm);

static int woal_cfg80211_set_tx_power(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
				      struct wireless_dev *wdev,
#endif
#if CFG80211_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
				      enum tx_power_setting type,
#else
				      enum nl80211_tx_power_setting type,
#endif
				      int dbm);
#endif

static int woal_cfg80211_join_ibss(struct wiphy *wiphy,
				   struct net_device *dev,
				   struct cfg80211_ibss_params *params);

static int woal_cfg80211_leave_ibss(struct wiphy *wiphy,
				    struct net_device *dev);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
static int woal_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
					     struct wireless_dev *wdev,
#else
					     struct net_device *dev,
#endif
					     u64 cookie);

static int woal_cfg80211_remain_on_channel(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
					   struct wireless_dev *wdev,
#else
					   struct net_device *dev,
#endif
					   struct ieee80211_channel *chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
					   enum nl80211_channel_type
					   channel_type,
#endif
					   unsigned int duration, u64 * cookie);

static int woal_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
						  struct wireless_dev *wdev,
#else
						  struct net_device *dev,
#endif
						  u64 cookie);
#endif /* KERNEL_VERSION */

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
int woal_cfg80211_sched_scan_start(struct wiphy *wiphy,
				   struct net_device *dev,
				   struct cfg80211_sched_scan_request *request);
int woal_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
				  , u64 reqid
#endif
	);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
int woal_cfg80211_resume(struct wiphy *wiphy);
int woal_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow);
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
static void woal_cfg80211_set_wakeup(struct wiphy *wiphy, bool enabled);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,2,0)
void woal_check_auto_tdls(struct wiphy *wiphy, struct net_device *dev);
int woal_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			    const u8 *peer,
#else
			    u8 *peer,
#endif
			    enum nl80211_tdls_operation oper);
int woal_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			    const u8 *peer,
#else
			    u8 *peer,
#endif
			    u8 action_code, u8 dialog_token, u16 status_code,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
			    u32 peer_capability,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
			    bool initiator,
#endif
			    const u8 *extra_ies, size_t extra_ies_len);
static int
 woal_cfg80211_add_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			   const u8 *mac,
#else
			   u8 *mac,
#endif
			   struct station_parameters *params);
static int
 woal_cfg80211_change_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			      const u8 *mac,
#else
			      u8 *mac,
#endif
			      struct station_parameters *params);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
static int

woal_cfg80211_tdls_channel_switch(struct wiphy *wiphy,
				  struct net_device *dev,
				  const u8 *addr, u8 oper_class,
				  struct cfg80211_chan_def *chandef);

void woal_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
					      struct net_device *dev,
					      const u8 *addr);
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,10,0)
int woal_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
				struct cfg80211_update_ft_ies_params *ftie);
#endif
/** cfg80211 operations */
static struct cfg80211_ops woal_cfg80211_ops = {
	.change_virtual_intf = woal_cfg80211_change_virtual_intf,
	.scan = woal_cfg80211_scan,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4,5,0)
	.abort_scan = woal_cfg80211_abort_scan,
#endif
	.connect = woal_cfg80211_connect,
	.disconnect = woal_cfg80211_disconnect,
	.get_station = woal_cfg80211_get_station,
	.dump_station = woal_cfg80211_dump_station,
	.dump_survey = woal_cfg80211_dump_survey,
	.set_wiphy_params = woal_cfg80211_set_wiphy_params,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 5, 0)
	.set_channel = woal_cfg80211_set_channel,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	.get_channel = woal_cfg80211_get_channel,
#endif
	.join_ibss = woal_cfg80211_join_ibss,
	.leave_ibss = woal_cfg80211_leave_ibss,
	.add_key = woal_cfg80211_add_key,
	.del_key = woal_cfg80211_del_key,
	.set_default_key = woal_cfg80211_set_default_key,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
	.set_default_mgmt_key = woal_cfg80211_set_default_mgmt_key,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
	.set_rekey_data = woal_cfg80211_set_rekey_data,
#endif
	.set_pmksa = woal_cfg80211_set_pmksa,
	.del_pmksa = woal_cfg80211_del_pmksa,
	.flush_pmksa = woal_cfg80211_flush_pmksa,
	.set_power_mgmt = woal_cfg80211_set_power_mgmt,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
	.set_tx_power = woal_cfg80211_set_tx_power,
	.get_tx_power = woal_cfg80211_get_tx_power,
#endif
	.set_bitrate_mask = woal_cfg80211_set_bitrate_mask,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	.sched_scan_start = woal_cfg80211_sched_scan_start,
	.sched_scan_stop = woal_cfg80211_sched_scan_stop,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	.suspend = woal_cfg80211_suspend,
	.resume = woal_cfg80211_resume,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
	.set_wakeup = woal_cfg80211_set_wakeup,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	.set_antenna = woal_cfg80211_set_antenna,
	.get_antenna = woal_cfg80211_get_antenna,
#endif
#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
	.set_cqm_rssi_config = woal_cfg80211_set_cqm_rssi_config,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	.tdls_oper = woal_cfg80211_tdls_oper,
	.tdls_mgmt = woal_cfg80211_tdls_mgmt,
	.add_station = woal_cfg80211_add_station,
	.change_station = woal_cfg80211_change_station,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	.tdls_channel_switch =
		woal_cfg80211_tdls_channel_switch,.tdls_cancel_channel_switch =
		woal_cfg80211_tdls_cancel_channel_switch,
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		.update_ft_ies = woal_cfg80211_update_ft_ies,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,14,0)
		.set_qos_map = woal_cfg80211_set_qos_map,
#endif
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		.set_coalesce = woal_cfg80211_set_coalesce,
#endif
		.add_virtual_intf =
		woal_cfg80211_add_virtual_intf,.del_virtual_intf =
		woal_cfg80211_del_virtual_intf,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
		.start_ap = woal_cfg80211_add_beacon,.change_beacon =
		woal_cfg80211_set_beacon,.stop_ap = woal_cfg80211_del_beacon,
#else
		.add_beacon = woal_cfg80211_add_beacon,.set_beacon =
		woal_cfg80211_set_beacon,.del_beacon = woal_cfg80211_del_beacon,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		.change_bss = woal_cfg80211_change_bss,
#endif
		.del_station = woal_cfg80211_del_station,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
		.set_txq_params = woal_cfg80211_set_txq_params,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		.set_mac_acl = woal_cfg80211_set_mac_acl,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		.start_radar_detection =
		woal_cfg80211_start_radar_detection,.channel_switch =
		woal_cfg80211_channel_switch,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
		.mgmt_frame_register =
		woal_cfg80211_mgmt_frame_register,.mgmt_tx =
		woal_cfg80211_mgmt_tx,
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		.mgmt_tx_cancel_wait =
		woal_cfg80211_mgmt_tx_cancel_wait,.remain_on_channel =
		woal_cfg80211_remain_on_channel,.cancel_remain_on_channel =
		woal_cfg80211_cancel_remain_on_channel,
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		.set_monitor_channel = woal_cfg80211_set_monitor_channel,
#endif
};

/** Region code mapping */
typedef struct _region_code_t {
    /** Region */
	t_u8 region[COUNTRY_CODE_LEN];
} region_code_t;

static const struct ieee80211_regdomain mrvl_regdom = {
	.n_reg_rules = 4,
	.alpha2 = "99",
	.reg_rules = {
		      /* IEEE 802.11b/g, channels 1..11 */
		      REG_RULE(2412 - 10, 2472 + 10, 40, 6, 20, 0),
		      /* If any */
		      /* IEEE 802.11 channel 14 - Only JP enables
		       * this and for 802.11b only
		       */
		      REG_RULE(2484 - 10, 2484 + 10, 20, 6, 20, 0),
		      /* IEEE 802.11a, channel 36..64 */
		      REG_RULE(5150 - 10, 5350 + 10, 80, 6, 20, 0),
		      /* IEEE 802.11a, channel 100..165 */
		      REG_RULE(5470 - 10, 5850 + 10, 80, 6, 20, 0),}
};

/********************************************************
				Local Variables
********************************************************/
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
static const struct ieee80211_txrx_stypes
 ieee80211_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
				  .tx = BIT(IEEE80211_STYPE_ACTION >> 4),
				  .rx = BIT(IEEE80211_STYPE_ACTION >> 4),
				  },
	[NL80211_IFTYPE_STATION] = {
				    .tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				    BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
				    .rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				    BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
				    },
	[NL80211_IFTYPE_AP] = {
			       .tx = 0xffff,
			       .rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			       BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			       BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			       BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			       BIT(IEEE80211_STYPE_AUTH >> 4) |
			       BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			       BIT(IEEE80211_STYPE_ACTION >> 4),
			       },
	[NL80211_IFTYPE_AP_VLAN] = {
				    .tx = 0x0000,
				    .rx = 0x0000,
				    },
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	[NL80211_IFTYPE_P2P_CLIENT] = {
				       .tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				       BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
				       .rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				       BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
				       },
	[NL80211_IFTYPE_P2P_GO] = {
				   .tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
				   BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
				   .rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
				   BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
				   BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
				   BIT(IEEE80211_STYPE_DISASSOC >> 4) |
				   BIT(IEEE80211_STYPE_AUTH >> 4) |
				   BIT(IEEE80211_STYPE_DEAUTH >> 4) |
				   BIT(IEEE80211_STYPE_ACTION >> 4),
				   },
#endif
#endif
	[NL80211_IFTYPE_MESH_POINT] = {
				       .tx = 0x0000,
				       .rx = 0x0000,
				       },

};
#endif

#if CFG80211_VERSION_CODE > KERNEL_VERSION(3, 0, 0)
/**
 * NOTE: types in all the sets must be equals to the
 * initial value of wiphy->interface_modes
 */
static const struct ieee80211_iface_limit cfg80211_ap_sta_limits[] = {
	{
	 .max = 4,
	 .types = MBIT(NL80211_IFTYPE_STATION) |
#ifdef UAP_CFG80211
	 MBIT(NL80211_IFTYPE_AP) | MBIT(NL80211_IFTYPE_MONITOR) |
#endif
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	 MBIT(NL80211_IFTYPE_P2P_GO) | MBIT(NL80211_IFTYPE_P2P_CLIENT) |
#endif
#endif
	 MBIT(NL80211_IFTYPE_ADHOC)
	 }
};

static struct ieee80211_iface_combination cfg80211_iface_comb_ap_sta = {
	.limits = cfg80211_ap_sta_limits,
	.num_different_channels = 1,
	.n_limits = ARRAY_SIZE(cfg80211_ap_sta_limits),
	.max_interfaces = 4,
	.beacon_int_infra_match = MTRUE,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	.radar_detect_widths = MBIT(NL80211_CHAN_WIDTH_20_NOHT)
		| MBIT(NL80211_CHAN_WIDTH_20),
#endif
};
#endif

extern moal_handle *m_handle[];
extern int hw_test;
extern int ps_mode;
/** Region alpha2 string */
char *reg_alpha2;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
int country_ie_ignore = 0;
int beacon_hints = 0;
#endif

#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
extern int p2p_enh;
#endif
#endif

int cfg80211_drcs = 0;

#ifdef CONFIG_PM
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
static const struct wiphy_wowlan_support wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_MAGIC_PKT,
	.n_patterns = MAX_NUM_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = WOWLAN_MAX_PATTERN_LEN,
	.max_pkt_offset = WOWLAN_MAX_OFFSET_LEN,
};

static const struct wiphy_wowlan_support wowlan_support_with_gtk = {
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_MAGIC_PKT
		| WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
		WIPHY_WOWLAN_GTK_REKEY_FAILURE,
	.n_patterns = MAX_NUM_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = WOWLAN_MAX_PATTERN_LEN,
	.max_pkt_offset = WOWLAN_MAX_OFFSET_LEN,
};
#endif
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
static const struct wiphy_coalesce_support coalesce_support = {
	.n_rules = COALESCE_MAX_RULES,
	.max_delay = MAX_COALESCING_DELAY,
	.n_patterns = COALESCE_MAX_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = MAX_PATTERN_LEN,
	.max_pkt_offset = MAX_OFFSET_LEN,
};
#endif

/********************************************************
				Global Variables
********************************************************/

/********************************************************
				Local Functions
********************************************************/
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int
woal_cfg80211_set_monitor_channel(struct wiphy *wiphy,
				  struct cfg80211_chan_def *chandef)
{
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	moal_private *priv =
		(moal_private *)woal_get_priv(handle, MLAN_BSS_ROLE_STA);
	netmon_band_chan_cfg band_chan_cfg;
	t_u32 bandwidth = 0;
	int ret = -EFAULT;

	ENTER();

	if (handle->mon_if) {
		if (cfg80211_chandef_identical
		    (&handle->mon_if->chandef, chandef)) {
			ret = 0;
			goto done;
		}

		memset(&band_chan_cfg, 0x00, sizeof(band_chan_cfg));
		/* Set channel */
		band_chan_cfg.channel =
			ieee80211_frequency_to_channel(chandef->chan->
						       center_freq);
		/* Set band */
		if (chandef->chan->band == IEEE80211_BAND_2GHZ)
			band_chan_cfg.band |= (BAND_B | BAND_G);
		if (chandef->chan->band == IEEE80211_BAND_5GHZ)
			band_chan_cfg.band |= BAND_A;
		if (chandef->chan->band == IEEE80211_BAND_2GHZ)
			band_chan_cfg.band |= BAND_GN;
		if (chandef->chan->band == IEEE80211_BAND_5GHZ)
			band_chan_cfg.band |= BAND_AN;
		/* Set bandwidth */
		if (chandef->width == NL80211_CHAN_WIDTH_20)
			bandwidth = CHANNEL_BW_20MHZ;
		else if (chandef->width == NL80211_CHAN_WIDTH_40)
			bandwidth =
				chandef->center_freq1 >
				chandef->chan->
				center_freq ? CHANNEL_BW_40MHZ_ABOVE :
				CHANNEL_BW_40MHZ_BELOW;
		band_chan_cfg.chan_bandwidth = bandwidth;

		if (MLAN_STATUS_SUCCESS !=
		    woal_set_net_monitor(priv, MOAL_IOCTL_WAIT,
					 CHANNEL_SPEC_SNIFFER_MODE, 0x7,
					 &band_chan_cfg)) {
			PRINTM(MERROR, "%s: woal_set_net_monitor fail\n",
			       __func__);
			ret = -EFAULT;
			goto done;
		}

		memcpy(&handle->mon_if->band_chan_cfg, &band_chan_cfg,
		       sizeof(handle->mon_if->band_chan_cfg));
		handle->mon_if->chandef = *chandef;

		if (handle->mon_if->chandef.chan)
			PRINTM(MINFO,
			       "set_monitor_channel+++ chan[band=%d center_freq=%d hw_value=%d] width=%d center_freq1=%d center_freq2=%d\n",
			       handle->mon_if->chandef.chan->band,
			       handle->mon_if->chandef.chan->center_freq,
			       handle->mon_if->chandef.chan->hw_value,
			       handle->mon_if->chandef.width,
			       handle->mon_if->chandef.center_freq1,
			       handle->mon_if->chandef.center_freq2);
		PRINTM(MINFO,
		       "set_monitor_channel+++ band=%x channel=%d bandwidth=%d\n",
		       handle->mon_if->band_chan_cfg.band,
		       handle->mon_if->band_chan_cfg.channel,
		       handle->mon_if->band_chan_cfg.chan_bandwidth);
		ret = 0;
	}

done:
	LEAVE();
	return ret;
}
#endif

/**
 *  @brief This function check cfg80211 special region code.
 *
 *  @param region_string         Region string
 *
 *  @return     MTRUE/MFALSE
 */
t_u8
is_cfg80211_special_region_code(char *region_string)
{
	t_u8 i;
	region_code_t cfg80211_special_region_code[] = {
		{"00 "}, {"99 "}, {"98 "}, {"97 "}
	};

	for (i = 0; i < COUNTRY_CODE_LEN && region_string[i]; i++)
		region_string[i] = toupper(region_string[i]);

	for (i = 0; i < ARRAY_SIZE(cfg80211_special_region_code); i++) {
		if (!memcmp(region_string,
			    cfg80211_special_region_code[i].region,
			    COUNTRY_CODE_LEN)) {
			PRINTM(MIOCTL, "special region code=%s\n",
			       region_string);
			return MTRUE;
		}
	}
	return MFALSE;
}

/**
 * @brief Get the encryption mode from cipher
 *
 * @param cipher        Cipher cuite
 * @param wpa_enabled   WPA enable or disable
 *
 * @return              MLAN_ENCRYPTION_MODE_*
 */
static int
woal_cfg80211_get_encryption_mode(t_u32 cipher, int *wpa_enabled)
{
	int encrypt_mode;

	ENTER();

	*wpa_enabled = 0;
	switch (cipher) {
	case MW_AUTH_CIPHER_NONE:
		encrypt_mode = MLAN_ENCRYPTION_MODE_NONE;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
		encrypt_mode = MLAN_ENCRYPTION_MODE_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		encrypt_mode = MLAN_ENCRYPTION_MODE_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		encrypt_mode = MLAN_ENCRYPTION_MODE_TKIP;
		*wpa_enabled = 1;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		encrypt_mode = MLAN_ENCRYPTION_MODE_CCMP;
		*wpa_enabled = 1;
		break;
	default:
		encrypt_mode = -1;
	}

	LEAVE();
	return encrypt_mode;
}

/**
 *  @brief get associate failure status code
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *
 *  @return         IEEE status code
 */
static int
woal_get_assoc_status(moal_private *priv)
{
	int ret = WLAN_STATUS_UNSPECIFIED_FAILURE;
	t_u16 status = (t_u16)(priv->assoc_status & 0xffff);
	t_u16 cap = (t_u16)(priv->assoc_status >> 16);

	switch (cap) {
	case 0xfffd:
	case 0xfffe:
		ret = status;
		break;
	case 0xfffc:
		ret = WLAN_STATUS_AUTH_TIMEOUT;
		break;
	default:
		break;
	}
	PRINTM(MCMND, "Assoc fail: status=%d, cap=0x%x, IEEE status=%d\n",
	       status, cap, ret);
	return ret;
}

/**
 *  @brief Check the pairwise or group cipher for
 *  WEP enabled or not
 *
 *  @param cipher       MLAN Cipher cuite
 *
 *  @return             1 -- enable or 0 -- disable
 */
static int
woal_cfg80211_is_alg_wep(t_u32 cipher)
{
	int alg = 0;
	ENTER();

	if (cipher == MLAN_ENCRYPTION_MODE_WEP40 ||
	    cipher == MLAN_ENCRYPTION_MODE_WEP104)
		alg = 1;

	LEAVE();
	return alg;
}

/**
 *  @brief Convert NL802.11 channel type into driver channel type
 *
 * The mapping is as follows -
 *      NL80211_CHAN_NO_HT     -> CHANNEL_BW_20MHZ
 *      NL80211_CHAN_HT20      -> CHANNEL_BW_20MHZ
 *      NL80211_CHAN_HT40PLUS  -> CHANNEL_BW_40MHZ_ABOVE
 *      NL80211_CHAN_HT40MINUS -> CHANNEL_BW_40MHZ_BELOW
 *      Others                 -> CHANNEL_BW_20MHZ
 *
 *  @param channel_type     Channel type
 *
 *  @return                 Driver channel type
 */
static int
woal_cfg80211_channel_type_to_channel(enum nl80211_channel_type channel_type)
{
	int channel;

	ENTER();

	switch (channel_type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		channel = CHANNEL_BW_20MHZ;
		break;
	case NL80211_CHAN_HT40PLUS:
		channel = CHANNEL_BW_40MHZ_ABOVE;
		break;
	case NL80211_CHAN_HT40MINUS:
		channel = CHANNEL_BW_40MHZ_BELOW;
		break;
	default:
		channel = CHANNEL_BW_20MHZ;
	}
	LEAVE();
	return channel;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/**
 *  @brief get nl80211_channel_type from cfg80211_chan_def
 *
 *  @param chan_def     struct cfg80211_chan_def
 *
 *  @return                 nl80211_channel_type type
 */
static enum nl80211_channel_type
woal_channel_to_nl80211_channel_type(struct cfg80211_chan_def *chan_def)
{
	enum nl80211_channel_type channel_type = 0;

	ENTER();

	switch (chan_def->width) {
	case NL80211_CHAN_WIDTH_20:
		/** Channel width 20MHz**/
		channel_type = NL80211_CHAN_HT20;
		break;
	case NL80211_CHAN_WIDTH_40:
		/** Channel width 40MHz**/
		if (chan_def->center_freq1 < chan_def->chan->center_freq)
			channel_type = NL80211_CHAN_HT40MINUS;
		else if (chan_def->center_freq1 > chan_def->chan->center_freq)
			channel_type = NL80211_CHAN_HT40PLUS;
		break;
	default:
		channel_type = NL80211_CHAN_HT20;
		break;
	}
	LEAVE();
	return channel_type;
}
#endif

/**
 *  @brief Convert NL80211 interface type to MLAN_BSS_MODE_*
 *
 *  @param iftype   Interface type of NL80211
 *
 *  @return         Driver bss mode
 */
static t_u32
woal_nl80211_iftype_to_mode(enum nl80211_iftype iftype)
{
	switch (iftype) {
	case NL80211_IFTYPE_ADHOC:
		return MLAN_BSS_MODE_IBSS;
	case NL80211_IFTYPE_STATION:
		return MLAN_BSS_MODE_INFRA;
	case NL80211_IFTYPE_UNSPECIFIED:
	default:
		return MLAN_BSS_MODE_AUTO;
	}
}

/**
 *  @brief Control WPS Session Enable/Disable
 *
 *  @param priv     Pointer to the moal_private driver data struct
 *  @param enable   enable/disable flag
 *
 *  @return          0 --success, otherwise fail
 */
static int
woal_wps_cfg(moal_private *priv, int enable)
{
	int ret = 0;
	mlan_ds_wps_cfg *pwps = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	PRINTM(MINFO, "WOAL_WPS_SESSION\n");

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_wps_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	pwps = (mlan_ds_wps_cfg *)req->pbuf;
	req->req_id = MLAN_IOCTL_WPS_CFG;
	req->action = MLAN_ACT_SET;
	pwps->sub_command = MLAN_OID_WPS_CFG_SESSION;
	if (enable)
		pwps->param.wps_session = MLAN_WPS_CFG_SESSION_START;
	else
		pwps->param.wps_session = MLAN_WPS_CFG_SESSION_END;

	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief configure ASSOC IE
 *
 * @param priv				A pointer to moal private structure
 * @param ie				A pointer to ie data
 * @param ie_len			The length of ie data
 * @param wait_option       wait option
 *
 * @return                  0 -- success, otherwise fail
 */
static int
woal_cfg80211_assoc_ies_cfg(moal_private *priv, t_u8 *ie, int ie_len,
			    t_u8 wait_option)
{
	int bytes_left = ie_len;
	t_u8 *pcurrent_ptr = ie;
	int total_ie_len;
	t_u8 element_len;
	int ret = MLAN_STATUS_SUCCESS;
	IEEEtypes_ElementId_e element_id;
	IEEEtypes_VendorSpecific_t *pvendor_ie;
	t_u8 wps_oui[] = { 0x00, 0x50, 0xf2, 0x04 };

	while (bytes_left >= 2) {
		element_id = (IEEEtypes_ElementId_e)(*((t_u8 *)pcurrent_ptr));
		element_len = *((t_u8 *)pcurrent_ptr + 1);
		total_ie_len = element_len + sizeof(IEEEtypes_Header_t);
		if (bytes_left < total_ie_len) {
			PRINTM(MERROR,
			       "InterpretIE: Error in processing IE, bytes left < IE length\n");
			bytes_left = 0;
			continue;
		}
		switch (element_id) {
		case RSN_IE:
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_gen_ie(priv, MLAN_ACT_SET,
						pcurrent_ptr, &total_ie_len,
						wait_option)) {
				PRINTM(MERROR, "Fail to set RSN IE\n");
				ret = -EFAULT;
				goto done;
			}
			PRINTM(MIOCTL, "Set RSN IE\n");
			break;
		case VENDOR_SPECIFIC_221:
			pvendor_ie = (IEEEtypes_VendorSpecific_t *)pcurrent_ptr;
			if (!memcmp
			    (pvendor_ie->vend_hdr.oui, wps_oui,
			     sizeof(pvendor_ie->vend_hdr.oui)) &&
			    (pvendor_ie->vend_hdr.oui_type == wps_oui[3])) {
				PRINTM(MIOCTL, "Enable WPS session\n");
				woal_wps_cfg(priv, MTRUE);
			}
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_gen_ie(priv, MLAN_ACT_SET,
						pcurrent_ptr, &total_ie_len,
						wait_option)) {
				PRINTM(MERROR,
				       "Fail to Set VENDOR SPECIFIC IE\n");
				ret = -EFAULT;
				goto done;
			}
			PRINTM(MIOCTL,
			       "Set VENDOR SPECIFIC IE, OUI: %02x:%02x:%02x:%02x\n",
			       pvendor_ie->vend_hdr.oui[0],
			       pvendor_ie->vend_hdr.oui[1],
			       pvendor_ie->vend_hdr.oui[2],
			       pvendor_ie->vend_hdr.oui_type);
			break;
		case MOBILITY_DOMAIN:
			break;
		case FAST_BSS_TRANSITION:
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_gen_ie(priv, MLAN_ACT_SET,
						pcurrent_ptr, &total_ie_len,
						wait_option)) {
				PRINTM(MERROR,
				       "Fail to set"
				       "FAST_BSS_TRANSITION IE\n");
				ret = -EFAULT;
				goto done;
			}
			PRINTM(MIOCTL, "Set FAST_BSS_TRANSITION IE\n");
			break;
		case RIC:
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_gen_ie(priv, MLAN_ACT_SET,
						pcurrent_ptr, &total_ie_len,
						wait_option)) {
				PRINTM(MERROR,
				       "Fail to set"
				       "RESOURCE INFORMATION CONTAINER IE\n");
				ret = -EFAULT;
				goto done;
			}
			PRINTM(MIOCTL,
			       "Set RESOURCE INFORMATION CONTAINER IE\n");
			break;
		case EXT_CAPABILITY:
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_gen_ie(priv, MLAN_ACT_SET,
						pcurrent_ptr, &total_ie_len,
						wait_option)) {
				PRINTM(MERROR,
				       "Fail to set Extended Capabilites IE\n");
				ret = -EFAULT;
				goto done;
			}
			PRINTM(MIOCTL, "Set Extended Capabilities IE\n");
			break;
		default:
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_gen_ie(priv, MLAN_ACT_SET,
						pcurrent_ptr, &total_ie_len,
						wait_option)) {
				PRINTM(MERROR, "Fail to set GEN IE\n");
				ret = -EFAULT;
				goto done;
			}
			PRINTM(MIOCTL, "Set GEN IE\n");
			break;
		}
		pcurrent_ptr += element_len + 2;
		/* Need to account for IE ID and IE Len */
		bytes_left -= (element_len + 2);
	}
done:
	return ret;
}

/**
 * @brief Send domain info command to FW
 *
 * @param priv      A pointer to moal_private structure
 * @param wait_option  wait option
 *
 * @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_send_domain_info_cmd_fw(moal_private *priv, t_u8 wait_option)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband = NULL;
	struct ieee80211_channel *channel = NULL;
	t_u8 no_of_sub_band = 0;
	t_u8 no_of_parsed_chan = 0;
	t_u8 first_chan = 0, next_chan = 0, max_pwr = 0;
	t_u8 i, flag = 0;
	mlan_ds_11d_cfg *cfg_11d = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!priv->wdev || !priv->wdev->wiphy) {
		PRINTM(MERROR, "No wdev or wiphy in priv\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	band = priv->phandle->band;
	if (!priv->wdev->wiphy->bands[band]) {
		PRINTM(MERROR, "11D: setting domain info in FW failed band=%d",
		       band);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (MTRUE ==
	    is_cfg80211_special_region_code(priv->phandle->country_code)) {
		PRINTM(MIOCTL,
		       "skip region code config, cfg80211 special region code: %s\n",
		       priv->phandle->country_code);
		goto done;
	}
	PRINTM(MIOCTL, "Send domain info: country=%c%c band=%d\n",
	       priv->phandle->country_code[0], priv->phandle->country_code[1],
	       band);
	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11d_cfg));
	if (req == NULL) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	cfg_11d = (mlan_ds_11d_cfg *)req->pbuf;
	cfg_11d->sub_command = MLAN_OID_11D_DOMAIN_INFO;
	req->req_id = MLAN_IOCTL_11D_CFG;
	req->action = MLAN_ACT_SET;

	/* Set country code */
	cfg_11d->param.domain_info.country_code[0] =
		priv->phandle->country_code[0];
	cfg_11d->param.domain_info.country_code[1] =
		priv->phandle->country_code[1];
	cfg_11d->param.domain_info.country_code[2] = ' ';
	cfg_11d->param.domain_info.band = band;

	sband = priv->wdev->wiphy->bands[band];
	for (i = 0; (i < sband->n_channels) &&
	     (no_of_sub_band < MRVDRV_MAX_SUBBAND_802_11D); i++) {
		channel = &sband->channels[i];
		if (channel->flags & IEEE80211_CHAN_DISABLED)
			continue;

		if (!flag) {
			flag = 1;
			next_chan = first_chan = (t_u32)channel->hw_value;
			max_pwr = channel->max_power;
			no_of_parsed_chan = 1;
			continue;
		}

		if (channel->hw_value == next_chan + 1 &&
		    channel->max_power == max_pwr) {
			next_chan++;
			no_of_parsed_chan++;
		} else {
			cfg_11d->param.domain_info.sub_band[no_of_sub_band]
				.first_chan = first_chan;
			cfg_11d->param.domain_info.sub_band[no_of_sub_band]
				.no_of_chan = no_of_parsed_chan;
			cfg_11d->param.domain_info.sub_band[no_of_sub_band]
				.max_tx_pwr = max_pwr;
			no_of_sub_band++;
			next_chan = first_chan = (t_u32)channel->hw_value;
			max_pwr = channel->max_power;
			no_of_parsed_chan = 1;
		}
	}

	if (flag && (no_of_sub_band < MRVDRV_MAX_SUBBAND_802_11D)) {
		cfg_11d->param.domain_info.sub_band[no_of_sub_band]
			.first_chan = first_chan;
		cfg_11d->param.domain_info.sub_band[no_of_sub_band]
			.no_of_chan = no_of_parsed_chan;
		cfg_11d->param.domain_info.sub_band[no_of_sub_band]
			.max_tx_pwr = max_pwr;
		no_of_sub_band++;
	}
	cfg_11d->param.domain_info.no_of_sub_band = no_of_sub_band;

	/* Send domain info command to FW */
	status = woal_request_ioctl(priv, req, wait_option);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		PRINTM(MERROR, "11D: Error setting domain info in FW\n");
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to change the channel and
 * change domain info according to that channel
 *
 * @param priv            A pointer to moal_private structure
 * @param chan            A pointer to ieee80211_channel structure
 * @param channel_type    Channel type of nl80211_channel_type
 * @param wait_option     wait option
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_set_rf_channel(moal_private *priv,
		    struct ieee80211_channel *chan,
		    enum nl80211_channel_type channel_type, t_u8 wait_option)
{
	int ret = 0;
	t_u32 mode, config_bands = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_radio_cfg *radio_cfg = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	int chan_width = 0;

	ENTER();

	if (!chan) {
		LEAVE();
		return -EINVAL;
	}
	mode = woal_nl80211_iftype_to_mode(priv->wdev->iftype);
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_radio_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	radio_cfg = (mlan_ds_radio_cfg *)req->pbuf;
	radio_cfg->sub_command = MLAN_OID_BAND_CFG;
	req->req_id = MLAN_IOCTL_RADIO_CFG;
	/* Get config_bands, adhoc_start_band and adhoc_channel values from MLAN */
	req->action = MLAN_ACT_GET;
	status = woal_request_ioctl(priv, req, wait_option);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	req->action = MLAN_ACT_SET;
	priv->phandle->band = chan->band;
	chan_width = woal_cfg80211_channel_type_to_channel(channel_type);
	/* Set appropriate bands */
	if (chan->band == IEEE80211_BAND_2GHZ)
		config_bands = BAND_B | BAND_G | BAND_GN;
	else {
		config_bands = BAND_AN | BAND_A;
	}
	if (mode == MLAN_BSS_MODE_IBSS) {
		radio_cfg->param.band_cfg.adhoc_start_band = config_bands;
		radio_cfg->param.band_cfg.adhoc_channel =
			ieee80211_frequency_to_channel(chan->center_freq);
	}
	/* Set channel offset */
	radio_cfg->param.band_cfg.adhoc_chan_bandwidth = chan_width;

	status = woal_request_ioctl(priv, req, wait_option);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	woal_send_domain_info_cmd_fw(priv, wait_option);

	PRINTM(MINFO,
	       "Setting band %d, channel bandwidth %d and mode = %d channel=%d\n",
	       config_bands, radio_cfg->param.band_cfg.adhoc_chan_bandwidth,
	       mode, ieee80211_frequency_to_channel(chan->center_freq));

	if (MLAN_STATUS_SUCCESS !=
	    woal_change_adhoc_chan(priv,
				   ieee80211_frequency_to_channel(chan->
								  center_freq),
				   wait_option)) {
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief Set ewpa mode
 *
 *  @param priv                 A pointer to moal_private structure
 *  @param wait_option          Wait option
 *  @param ssid_bssid           A pointer to mlan_ssid_bssid structure
 *
 *  @return                     MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING -- success, otherwise fail
 */
mlan_status
woal_set_ewpa_mode(moal_private *priv, t_u8 wait_option,
		   mlan_ssid_bssid *ssid_bssid)
{
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_sec_cfg *sec = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
	if (req == NULL) {
		ret = -ENOMEM;
		goto error;
	}
	/* Fill request buffer */
	sec = (mlan_ds_sec_cfg *)req->pbuf;
	sec->sub_command = MLAN_OID_SEC_CFG_PASSPHRASE;
	req->req_id = MLAN_IOCTL_SEC_CFG;
	req->action = MLAN_ACT_GET;

	/* Try Get All */
	memset(&sec->param.passphrase, 0, sizeof(mlan_ds_passphrase));
	memcpy(&sec->param.passphrase.ssid, &ssid_bssid->ssid,
	       sizeof(sec->param.passphrase.ssid));
	memcpy(&sec->param.passphrase.bssid, &ssid_bssid->bssid,
	       MLAN_MAC_ADDR_LENGTH);
	sec->param.passphrase.psk_type = MLAN_PSK_QUERY;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);
	if (status != MLAN_STATUS_SUCCESS)
		goto error;
	sec->param.ewpa_enabled = MFALSE;
	if (sec->param.passphrase.psk_type == MLAN_PSK_PASSPHRASE) {
		if (sec->param.passphrase.psk.passphrase.passphrase_len > 0)
			sec->param.ewpa_enabled = MTRUE;
	} else if (sec->param.passphrase.psk_type == MLAN_PSK_PMK)
		sec->param.ewpa_enabled = MTRUE;

	sec->sub_command = MLAN_OID_SEC_CFG_EWPA_ENABLED;
	req->action = MLAN_ACT_SET;

	/* Send IOCTL request to MLAN */
	status = woal_request_ioctl(priv, req, wait_option);

error:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return status;
}

/**
 * @brief Set encryption mode and enable WPA
 *
 * @param priv          A pointer to moal_private structure
 * @param encrypt_mode  Encryption mode
 * @param wpa_enabled   WPA enable or not
 * @param wait_option   wait option
 *
 * @return              0 -- success, otherwise fail
 */
static int
woal_cfg80211_set_auth(moal_private *priv, int encrypt_mode,
		       int wpa_enabled, t_u8 wait_option)
{
	int ret = 0;

	ENTER();

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_encrypt_mode(priv, wait_option, encrypt_mode))
		ret = -EFAULT;

	if (wpa_enabled) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_wpa_enable(priv, wait_option, 1))
			ret = -EFAULT;
	}

	LEAVE();
	return ret;
}

/**
 * @brief Informs the CFG802.11 subsystem of a new BSS connection.
 *
 * The following information are sent to the CFG802.11 subsystem
 * to register the new BSS connection. If we do not register the new BSS,
 * a kernel panic will result.
 *      - MAC address
 *      - Capabilities
 *      - Beacon period
 *      - RSSI value
 *      - Channel
 *      - Supported rates IE
 *      - Extended capabilities IE
 *      - DS parameter set IE
 *      - HT Capability IE
 *      - Vendor Specific IE (221)
 *      - WPA IE
 *      - RSN IE
 *
 * @param priv            A pointer to moal_private structure
 * @param ssid_bssid      A pointer to A pointer to mlan_ssid_bssid structure
 * @param wait_option     wait_option
 *
 * @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_inform_bss_from_scan_result(moal_private *priv,
				 mlan_ssid_bssid *ssid_bssid, t_u8 wait_option)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	struct ieee80211_channel *chan;
	mlan_scan_resp scan_resp;
	BSSDescriptor_t *scan_table;
	t_u64 ts = 0;
	u16 cap_info = 0;
	int i = 0;
	struct cfg80211_bss *pub = NULL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	struct timespec tstamp;
#endif
	ENTER();
	if (!priv->wdev || !priv->wdev->wiphy) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	memset(&scan_resp, 0, sizeof(scan_resp));
	if (MLAN_STATUS_SUCCESS != woal_get_scan_table(priv,
						       wait_option,
						       &scan_resp)) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (scan_resp.num_in_scan_table) {
		scan_table = (BSSDescriptor_t *)scan_resp.pscan_table;
		for (i = 0; i < scan_resp.num_in_scan_table; i++) {
			if (ssid_bssid) {
				/* Inform specific BSS only */
				if (memcmp
				    (ssid_bssid->ssid.ssid,
				     scan_table[i].ssid.ssid,
				     ssid_bssid->ssid.ssid_len) ||
				    memcmp(ssid_bssid->bssid,
					   scan_table[i].mac_address, ETH_ALEN))
					continue;
			}
			if (!scan_table[i].freq) {
				scan_table[i].freq =
					ieee80211_channel_to_frequency((int)
								       scan_table
								       [i].
								       channel
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
								       ,
								       woal_band_cfg_to_ieee_band
								       (scan_table
									[i].
									bss_band)
#endif
					);
			}
			chan = ieee80211_get_channel(priv->wdev->wiphy,
						     scan_table[i].freq);
			if (!chan) {
				PRINTM(MCMND,
				       "Fail to get chan with freq: channel=%d freq=%d\n",
				       (int)scan_table[i].channel,
				       (int)scan_table[i].freq);
				continue;
			}
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
			if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT &&
			    !ssid_bssid) {
				if (!strncmp
				    (scan_table[i].ssid.ssid, "DIRECT-",
				     strlen("DIRECT-"))) {
					PRINTM(MCMND,
					       "wlan: P2P device " MACSTR
					       " found, channel=%d\n",
					       MAC2STR(scan_table[i].
						       mac_address),
					       (int)chan->hw_value);
				}
			}
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
			/** Andorid's Location service is expecting timestamp to be
			* local time (in microsecond) since boot;
			* and not the TSF found in the beacon. */
			get_monotonic_boottime(&tstamp);
			ts = (t_u64)tstamp.tv_sec * 1000000 +
				tstamp.tv_nsec / 1000;
#else
			memcpy(&ts, scan_table[i].time_stamp, sizeof(ts));
#endif
			memcpy(&cap_info, &scan_table[i].cap_info,
			       sizeof(cap_info));
			pub = cfg80211_inform_bss(priv->wdev->wiphy, chan,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
						  CFG80211_BSS_FTYPE_UNKNOWN,
#endif
						  scan_table[i].mac_address,
						  ts, cap_info,
						  scan_table[i].beacon_period,
						  scan_table[i].pbeacon_buf +
						  WLAN_802_11_FIXED_IE_SIZE,
						  scan_table[i].
						  beacon_buf_size -
						  WLAN_802_11_FIXED_IE_SIZE,
						  -RSSI_DBM_TO_MDM(scan_table
								   [i].rssi),
						  GFP_KERNEL);
			if (pub) {
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
				pub->len_information_elements =
					pub->len_beacon_ies;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
				cfg80211_put_bss(priv->wdev->wiphy, pub);
#else
				cfg80211_put_bss(pub);
#endif
			}
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 * @brief Informs the CFG802.11 subsystem of a new IBSS connection.
 *
 * The following information are sent to the CFG802.11 subsystem
 * to register the new IBSS connection. If we do not register the
 * new IBSS, a kernel panic will result.
 *      - MAC address
 *      - Capabilities
 *      - Beacon period
 *      - RSSI value
 *      - Channel
 *      - Supported rates IE
 *      - Extended capabilities IE
 *      - DS parameter set IE
 *      - HT Capability IE
 *      - Vendor Specific IE (221)
 *      - WPA IE
 *      - RSN IE
 *
 * @param priv              A pointer to moal_private structure
 * @param cahn              A pointer to ieee80211_channel structure
 * @param beacon_interval   Beacon interval
 *
 * @return                  MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
woal_cfg80211_inform_ibss_bss(moal_private *priv,
			      struct ieee80211_channel *chan,
			      t_u16 beacon_interval)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_bss_info bss_info;
	mlan_ds_get_signal signal;
	t_u8 ie_buf[MLAN_MAX_SSID_LENGTH + sizeof(IEEEtypes_Header_t)];
	int ie_len = 0;
	struct cfg80211_bss *bss = NULL;

	ENTER();

	ret = woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
	if (ret)
		goto done;

	memset(ie_buf, 0, sizeof(ie_buf));
	ie_buf[0] = WLAN_EID_SSID;
	ie_buf[1] = bss_info.ssid.ssid_len;

	memcpy(&ie_buf[sizeof(IEEEtypes_Header_t)],
	       &bss_info.ssid.ssid, bss_info.ssid.ssid_len);
	ie_len = ie_buf[1] + sizeof(IEEEtypes_Header_t);

	/* Get signal information from the firmware */
	memset(&signal, 0, sizeof(mlan_ds_get_signal));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_signal_info(priv, MOAL_IOCTL_WAIT, &signal)) {
		PRINTM(MERROR, "Error getting signal information\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	bss = cfg80211_inform_bss(priv->wdev->wiphy, chan,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 18, 0)
				  CFG80211_BSS_FTYPE_UNKNOWN,
#endif
				  bss_info.bssid, 0, WLAN_CAPABILITY_IBSS,
				  beacon_interval, ie_buf, ie_len,
				  signal.bcn_rssi_avg, GFP_KERNEL);
	if (bss)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
		cfg80211_put_bss(priv->wdev->wiphy, bss);
#else
		cfg80211_put_bss(bss);
#endif
done:
	LEAVE();
	return ret;
}

/**
 * @brief Process country IE before assoicate
 *
 * @param priv            A pointer to moal_private structure
 * @param bss             A pointer to cfg80211_bss structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_process_country_ie(moal_private *priv, struct cfg80211_bss *bss)
{
	u8 *country_ie, country_ie_len;
	int ret = 0;
	mlan_ioctl_req *req = NULL;
	mlan_ds_11d_cfg *cfg_11d = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();
	country_ie = (u8 *)ieee80211_bss_get_ie(bss, WLAN_EID_COUNTRY);
	if (!country_ie) {
		PRINTM(MIOCTL, "No country IE found!\n");
		woal_send_domain_info_cmd_fw(priv, MOAL_IOCTL_WAIT);
		LEAVE();
		return 0;
	}

	country_ie_len = country_ie[1];
	if (country_ie_len < IEEE80211_COUNTRY_IE_MIN_LEN) {
		PRINTM(MIOCTL, "Wrong Country IE length!\n");
		woal_send_domain_info_cmd_fw(priv, MOAL_IOCTL_WAIT);
		LEAVE();
		return 0;
	}
	PRINTM(MIOCTL, "Find bss country IE: %c%c band=%d\n", country_ie[2],
	       country_ie[3], priv->phandle->band);
	priv->phandle->country_code[0] = country_ie[2];
	priv->phandle->country_code[1] = country_ie[3];
	priv->phandle->country_code[2] = ' ';
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_region_code(priv, priv->phandle->country_code))
		PRINTM(MERROR, "Set country code failed!\n");

	/* Allocate an IOCTL request buffer */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11d_cfg));
	if (req == NULL) {
		PRINTM(MERROR, "Fail to allocate mlan_ds_11d_cfg buffer\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	cfg_11d = (mlan_ds_11d_cfg *)req->pbuf;
	cfg_11d->sub_command = MLAN_OID_11D_DOMAIN_INFO;
	req->req_id = MLAN_IOCTL_11D_CFG;
	req->action = MLAN_ACT_SET;

	/* Set country code */
	cfg_11d->param.domain_info.country_code[0] =
		priv->phandle->country_code[0];
	cfg_11d->param.domain_info.country_code[1] =
		priv->phandle->country_code[1];
	cfg_11d->param.domain_info.country_code[2] = ' ';

    /** IEEE80211_BAND_2GHZ or IEEE80211_BAND_5GHZ */
	cfg_11d->param.domain_info.band = priv->phandle->band;

	country_ie_len -= COUNTRY_CODE_LEN;
	cfg_11d->param.domain_info.no_of_sub_band =
		MIN(MRVDRV_MAX_SUBBAND_802_11D,
		    (country_ie_len /
		     sizeof(struct ieee80211_country_ie_triplet)));
	memcpy((u8 *)cfg_11d->param.domain_info.sub_band,
	       &country_ie[2] + COUNTRY_CODE_LEN,
	       cfg_11d->param.domain_info.no_of_sub_band *
	       sizeof(mlan_ds_subband_set_t));

	/* Send domain info command to FW */
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		PRINTM(MERROR, "11D: Error setting domain info in FW\n");
		goto done;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Request scan based on connect parameter
 *
 * @param priv            A pointer to moal_private structure
 * @param conn_param      A pointer to connect parameters
 * @param wait_option     wait option
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_cfg80211_connect_scan(moal_private *priv,
			   struct cfg80211_connect_params *conn_param,
			   t_u8 wait_option)
{
	moal_handle *handle = priv->phandle;
	int ret = 0;
	wlan_user_scan_cfg scan_req;
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	int chan_idx = 0, i;
	ENTER();
	if (handle->scan_pending_on_block == MTRUE) {
		PRINTM(MINFO, "scan already in processing...\n");
		LEAVE();
		return ret;
	}
#ifdef REASSOCIATION
	if (MOAL_ACQ_SEMAPHORE_BLOCK(&handle->reassoc_sem)) {
		PRINTM(MERROR, "Acquire semaphore error, woal_do_combo_scan\n");
		LEAVE();
		return -EBUSY;
	}
#endif /* REASSOCIATION */
	priv->report_scan_result = MTRUE;
	memset(&scan_req, 0x00, sizeof(scan_req));
	memcpy(scan_req.ssid_list[0].ssid, conn_param->ssid,
	       conn_param->ssid_len);
	scan_req.ssid_list[0].max_len = 0;
	if (conn_param->channel) {
		scan_req.chan_list[0].chan_number =
			conn_param->channel->hw_value;
		scan_req.chan_list[0].radio_type = conn_param->channel->band;
		if (conn_param->channel->
		    flags & (IEEE80211_CHAN_PASSIVE_SCAN |
			     IEEE80211_CHAN_RADAR))
			scan_req.chan_list[0].scan_type =
				MLAN_SCAN_TYPE_PASSIVE;
		else
			scan_req.chan_list[0].scan_type = MLAN_SCAN_TYPE_ACTIVE;
		scan_req.chan_list[0].scan_time = 0;
	} else {
		for (band = 0; (band < IEEE80211_NUM_BANDS); band++) {
			if (!priv->wdev->wiphy->bands[band])
				continue;
			sband = priv->wdev->wiphy->bands[band];
			for (i = 0; (i < sband->n_channels); i++) {
				ch = &sband->channels[i];
				if (ch->flags & IEEE80211_CHAN_DISABLED)
					continue;
				scan_req.chan_list[chan_idx].radio_type = band;
				if (ch->
				    flags & (IEEE80211_CHAN_PASSIVE_SCAN |
					     IEEE80211_CHAN_RADAR))
					scan_req.chan_list[chan_idx].scan_type =
						MLAN_SCAN_TYPE_PASSIVE;
				else
					scan_req.chan_list[chan_idx].scan_type =
						MLAN_SCAN_TYPE_ACTIVE;
				scan_req.chan_list[chan_idx].chan_number =
					(u32)ch->hw_value;
				chan_idx++;
			}
		}
	}
	ret = woal_request_userscan(priv, wait_option, &scan_req);
#ifdef REASSOCIATION
	MOAL_REL_SEMAPHORE(&handle->reassoc_sem);
#endif
	LEAVE();
	return ret;

}

/**
 * @brief Request the driver for (re)association
 *
 * @param priv            A pointer to moal_private structure
 * @param sme             A pointer to connect parameters
 * @param wait_option     wait option
 *
 * @return                0 -- success, otherwise fail
 */
int
woal_cfg80211_assoc(moal_private *priv, void *sme, t_u8 wait_option)
{
	struct cfg80211_ibss_params *ibss_param = NULL;
	struct cfg80211_connect_params *conn_param = NULL;
	mlan_802_11_ssid req_ssid;
	mlan_ssid_bssid ssid_bssid;
	mlan_ioctl_req *req = NULL;
	int ret = 0;
	t_u32 auth_type = 0, mode;
	int wpa_enabled = 0;
	int group_enc_mode = 0, pairwise_enc_mode = 0;
	int alg_is_wep = 0;

	t_u8 *ssid, ssid_len = 0, *bssid;
	t_u8 *ie = NULL;
	int ie_len = 0;
	enum nl80211_channel_type chan_type = 0;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	struct cfg80211_chan_def *chan_def = NULL;
#endif
	struct ieee80211_channel *channel = NULL;
	t_u16 beacon_interval = 0;
	bool privacy;
	struct cfg80211_bss *bss = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	if (!sme) {
		LEAVE();
		return -EFAULT;
	}

	mode = woal_nl80211_iftype_to_mode(priv->wdev->iftype);

	if (mode == MLAN_BSS_MODE_IBSS) {
		ibss_param = (struct cfg80211_ibss_params *)sme;
		ssid = (t_u8 *)ibss_param->ssid;
		ssid_len = ibss_param->ssid_len;
		bssid = (t_u8 *)ibss_param->bssid;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
		channel = ibss_param->channel;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
		chan_type = ibss_param->channel_type;
#else
		chan_type = 0;
#endif
#else
		chan_def = &ibss_param->chandef;
		channel = ibss_param->chandef.chan;
#endif
		if (channel)
			priv->phandle->band = channel->band;
		if (ibss_param->ie_len)
			ie = (t_u8 *)ibss_param->ie;
		ie_len = ibss_param->ie_len;
		beacon_interval = ibss_param->beacon_interval;
		privacy = ibss_param->privacy;

	} else {
		conn_param = (struct cfg80211_connect_params *)sme;
		ssid = (t_u8 *)conn_param->ssid;
		ssid_len = conn_param->ssid_len;
		bssid = (t_u8 *)conn_param->bssid;
		channel = conn_param->channel;
		if (channel)
			priv->phandle->band = channel->band;
		if (conn_param->ie_len)
			ie = (t_u8 *)conn_param->ie;
		ie_len = conn_param->ie_len;
		privacy = conn_param->privacy;
		bss = cfg80211_get_bss(priv->wdev->wiphy, channel, bssid, ssid,
				       ssid_len, WLAN_CAPABILITY_ESS,
				       WLAN_CAPABILITY_ESS);
		if (bss) {
			if (!reg_alpha2 ||
			    strncmp(reg_alpha2, "99", strlen("99")))
				woal_process_country_ie(priv, bss);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
			cfg80211_put_bss(priv->wdev->wiphy, bss);
#else
			cfg80211_put_bss(bss);
#endif
		} else
			woal_send_domain_info_cmd_fw(priv, wait_option);
#ifdef STA_WEXT
		if (IS_STA_WEXT(cfg80211_wext)) {
			switch (conn_param->crypto.wpa_versions) {
			case NL80211_WPA_VERSION_2:
				priv->wpa_version = IW_AUTH_WPA_VERSION_WPA2;
				break;
			case NL80211_WPA_VERSION_1:
				priv->wpa_version = IW_AUTH_WPA_VERSION_WPA;
				break;
			default:
				priv->wpa_version = 0;
				break;
			}
			if (conn_param->crypto.n_akm_suites) {
				switch (conn_param->crypto.akm_suites[0]) {
				case WLAN_AKM_SUITE_PSK:
					priv->key_mgmt = IW_AUTH_KEY_MGMT_PSK;
					break;
				case WLAN_AKM_SUITE_8021X:
					priv->key_mgmt =
						IW_AUTH_KEY_MGMT_802_1X;
					break;
				default:
					priv->key_mgmt = 0;
					break;
				}
			}
		}
#endif
	}

	memset(&req_ssid, 0, sizeof(mlan_802_11_ssid));
	memset(&ssid_bssid, 0, sizeof(mlan_ssid_bssid));

	req_ssid.ssid_len = ssid_len;
	if (ssid_len > MW_ESSID_MAX_SIZE) {
		PRINTM(MERROR, "Invalid SSID - aborting\n");
		ret = -EINVAL;
		goto done;
	}

	memcpy(req_ssid.ssid, ssid, ssid_len);
	if (!req_ssid.ssid_len || req_ssid.ssid[0] < 0x20) {
		PRINTM(MERROR, "Invalid SSID - aborting\n");
		ret = -EINVAL;
		goto done;
	}

	if ((mode == MLAN_BSS_MODE_IBSS) && channel) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		chan_type = woal_channel_to_nl80211_channel_type(chan_def);
#endif
		if (MLAN_STATUS_SUCCESS != woal_set_rf_channel(priv,
							       channel,
							       chan_type,
							       wait_option)) {
			ret = -EFAULT;
			goto done;
		}
	}

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_ewpa_mode(priv, wait_option, &ssid_bssid)) {
		ret = -EFAULT;
		goto done;
	}

	if (MLAN_STATUS_SUCCESS !=
	    woal_cfg80211_set_key(priv, 0, 0, NULL, 0, NULL, 0,
				  KEY_INDEX_CLEAR_ALL, NULL, 1, wait_option)) {
		/* Disable keys and clear all previous security settings */
		ret = -EFAULT;
		goto done;
	}
#ifdef STA_CFG80211
	if (IS_STA_CFG80211(cfg80211_wext)) {
		/** Check if current roaming support OKC offload roaming */
		if (conn_param && conn_param->crypto.n_akm_suites &&
		    conn_param->crypto.akm_suites[0] == WLAN_AKM_SUITE_8021X) {
			if (priv->okc_roaming_ie && priv->okc_ie_len) {
				ie = priv->okc_roaming_ie;
				ie_len = priv->okc_ie_len;
			}
		}
	}
#endif

	if ((priv->ft_pre_connect ||
	     (conn_param && conn_param->auth_type == NL80211_AUTHTYPE_FT))
	    && priv->ft_ie_len) {
		ie = priv->ft_ie;
		ie_len = priv->ft_ie_len;
		priv->ft_ie_len = 0;
	}
	if (ie && ie_len) {	/* Set the IE */
		if (MLAN_STATUS_SUCCESS !=
		    woal_cfg80211_assoc_ies_cfg(priv, ie, ie_len,
						wait_option)) {
			ret = -EFAULT;
			goto done;
		}
	}

	if (conn_param && mode != MLAN_BSS_MODE_IBSS) {
		/* These parameters are only for managed mode */
		if (conn_param->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM)
			auth_type = MLAN_AUTH_MODE_OPEN;
		else if (conn_param->auth_type == NL80211_AUTHTYPE_SHARED_KEY)
			auth_type = MLAN_AUTH_MODE_SHARED;
		else if (conn_param->auth_type == NL80211_AUTHTYPE_NETWORK_EAP)
			auth_type = MLAN_AUTH_MODE_NETWORKEAP;
		else if (conn_param->auth_type == NL80211_AUTHTYPE_FT)
			auth_type = MLAN_AUTH_MODE_FT;
		else
			auth_type = MLAN_AUTH_MODE_AUTO;
		if (priv->ft_pre_connect)
			auth_type = MLAN_AUTH_MODE_FT;
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_auth_mode(priv, wait_option, auth_type)) {
			ret = -EFAULT;
			goto done;
		}

		if (conn_param->crypto.n_ciphers_pairwise) {
			pairwise_enc_mode =
				woal_cfg80211_get_encryption_mode(conn_param->
								  crypto.ciphers_pairwise
								  [0],
								  &wpa_enabled);
			ret = woal_cfg80211_set_auth(priv, pairwise_enc_mode,
						     wpa_enabled, wait_option);
			if (ret)
				goto done;
		}

		if (conn_param->crypto.cipher_group) {
			group_enc_mode =
				woal_cfg80211_get_encryption_mode(conn_param->
								  crypto.cipher_group,
								  &wpa_enabled);
			ret = woal_cfg80211_set_auth(priv, group_enc_mode,
						     wpa_enabled, wait_option);
			if (ret)
				goto done;
		}

		if (conn_param->key) {
			alg_is_wep =
				woal_cfg80211_is_alg_wep(pairwise_enc_mode) |
				woal_cfg80211_is_alg_wep(group_enc_mode);
			if (alg_is_wep) {
				PRINTM(MINFO,
				       "Setting wep encryption with key len %d\n",
				       conn_param->key_len);
				/* Set the WEP key */
				if (MLAN_STATUS_SUCCESS !=
				    woal_cfg80211_set_wep_keys(priv,
							       conn_param->key,
							       conn_param->
							       key_len,
							       conn_param->
							       key_idx,
							       wait_option)) {
					ret = -EFAULT;
					goto done;
				}
				/* Enable the WEP key by key index */
				if (MLAN_STATUS_SUCCESS !=
				    woal_cfg80211_set_wep_keys(priv, NULL, 0,
							       conn_param->
							       key_idx,
							       wait_option)) {
					ret = -EFAULT;
					goto done;
				}
			}
		}
	}

	if (mode == MLAN_BSS_MODE_IBSS) {
		mlan_ds_bss *bss = NULL;
		/* Change beacon interval */
		if ((beacon_interval < MLAN_MIN_BEACON_INTERVAL) ||
		    (beacon_interval > MLAN_MAX_BEACON_INTERVAL)) {
			ret = -EINVAL;
			goto done;
		}
		kfree(req);
		req = NULL;

		req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
		if (req == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		bss = (mlan_ds_bss *)req->pbuf;
		req->req_id = MLAN_IOCTL_BSS;
		req->action = MLAN_ACT_SET;
		bss->sub_command = MLAN_OID_IBSS_BCN_INTERVAL;
		bss->param.bcn_interval = beacon_interval;
		status = woal_request_ioctl(priv, req, wait_option);
		if (status != MLAN_STATUS_SUCCESS) {
			ret = -EFAULT;
			goto done;
		}

		/* "privacy" is set only for ad-hoc mode */
		if (privacy) {
			/*
			 * Keep MLAN_ENCRYPTION_MODE_WEP40 for now so that
			 * the firmware can find a matching network from the
			 * scan. cfg80211 does not give us the encryption
			 * mode at this stage so just setting it to wep here
			 */
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_auth_mode(priv, wait_option,
					       MLAN_AUTH_MODE_OPEN)) {
				ret = -EFAULT;
				goto done;
			}

			wpa_enabled = 0;
			ret = woal_cfg80211_set_auth(priv,
						     MLAN_ENCRYPTION_MODE_WEP104,
						     wpa_enabled, wait_option);
			if (ret)
				goto done;
		}
	}
	memcpy(&ssid_bssid.ssid, &req_ssid, sizeof(mlan_802_11_ssid));
	if (bssid)
		memcpy(&ssid_bssid.bssid, bssid, ETH_ALEN);
	if (MLAN_STATUS_SUCCESS !=
	    woal_find_essid(priv, &ssid_bssid, wait_option)) {
		/* Do specific SSID scanning */
		if (mode != MLAN_BSS_MODE_IBSS)
			ret = woal_cfg80211_connect_scan(priv, conn_param,
							 wait_option);
		else
			ret = woal_request_scan(priv, wait_option, &req_ssid);
		if (ret) {
			ret = -EFAULT;
			goto done;
		}
	}

	/* Disconnect before try to associate */
	if (mode == MLAN_BSS_MODE_IBSS)
		woal_disconnect(priv, wait_option, NULL,
				DEF_DEAUTH_REASON_CODE);

	if (mode != MLAN_BSS_MODE_IBSS) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_find_best_network(priv, wait_option, &ssid_bssid)) {
			ret = -EFAULT;
			goto done;
		}
		/* Inform the BSS information to kernel, otherwise
		 * kernel will give a panic after successful assoc */
		if (MLAN_STATUS_SUCCESS !=
		    woal_inform_bss_from_scan_result(priv, &ssid_bssid,
						     wait_option)) {
			ret = -EFAULT;
			goto done;
		}
	} else if (MLAN_STATUS_SUCCESS !=
		   woal_find_best_network(priv, wait_option, &ssid_bssid))
		/* Adhoc start, Check the channel command */
		woal_11h_channel_check_ioctl(priv, wait_option);

	PRINTM(MINFO, "Trying to associate to %s and bssid " MACSTR "\n",
	       (char *)req_ssid.ssid, MAC2STR(ssid_bssid.bssid));

	/* Zero SSID implies use BSSID to connect */
	if (bssid)
		memset(&ssid_bssid.ssid, 0, sizeof(mlan_802_11_ssid));
	else			/* Connect to BSS by ESSID */
		memset(&ssid_bssid.bssid, 0, MLAN_MAC_ADDR_LENGTH);
	if (channel) {
		ssid_bssid.channel_flags = channel->flags;
		PRINTM(MCMND, "channel flags=0x%x\n", channel->flags);
	}
	if (MLAN_STATUS_SUCCESS !=
	    woal_bss_start(priv, MOAL_IOCTL_WAIT_TIMEOUT, &ssid_bssid)) {
		ret = -EFAULT;
		goto done;
	}

	/* Inform the IBSS information to kernel, otherwise
	 * kernel will give a panic after successful assoc */
	if (mode == MLAN_BSS_MODE_IBSS) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_cfg80211_inform_ibss_bss(priv, channel,
						  beacon_interval)) {
			ret = -EFAULT;
			goto done;
		}
	}

done:
	if (ret) {
		/* clear the encryption mode */
		woal_cfg80211_set_auth(priv, MLAN_ENCRYPTION_MODE_NONE, MFALSE,
				       wait_option);
		/* clear IE */
		ie_len = 0;
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_gen_ie(priv, MLAN_ACT_SET, NULL, &ie_len,
					wait_option)) {
			PRINTM(MERROR, "Could not clear RSN IE\n");
			ret = -EFAULT;
		}
	}
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to dump the station information
 *
 * @param priv            A pointer to moal_private structure
 * @param sinfo           A pointer to station_info structure
 *
 * @return                0 -- success, otherwise fail
 */
static mlan_status
woal_cfg80211_dump_station_info(moal_private *priv, struct station_info *sinfo)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_get_signal signal;
	mlan_ds_get_stats stats;
	mlan_ioctl_req *req = NULL;
	mlan_ds_rate *rate = NULL;
	t_u16 Rates[12] = {
		0x02, 0x04, 0x0B, 0x16,
		0x0C, 0x12, 0x18, 0x24,
		0x30, 0x48, 0x60, 0x6c
	};
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	mlan_bss_info bss_info;
	t_u8 dtim_period = 0;
#endif

	ENTER();
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	sinfo->filled =
		BIT(NL80211_STA_INFO_RX_BYTES) | BIT(NL80211_STA_INFO_TX_BYTES)
		| BIT(NL80211_STA_INFO_RX_PACKETS) |
		BIT(NL80211_STA_INFO_TX_PACKETS) | BIT(NL80211_STA_INFO_SIGNAL)
		| BIT(NL80211_STA_INFO_TX_BITRATE);
#else
	sinfo->filled = STATION_INFO_RX_BYTES | STATION_INFO_TX_BYTES |
		STATION_INFO_RX_PACKETS | STATION_INFO_TX_PACKETS |
		STATION_INFO_SIGNAL | STATION_INFO_TX_BITRATE;
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	sinfo->filled |= BIT(NL80211_STA_INFO_TX_FAILED);
#else
	sinfo->filled |= STATION_INFO_TX_FAILED;
#endif
#endif

	/* Get signal information from the firmware */
	memset(&signal, 0, sizeof(mlan_ds_get_signal));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_signal_info(priv, MOAL_IOCTL_WAIT, &signal)) {
		PRINTM(MERROR, "Error getting signal information\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Get stats information from the firmware */
	memset(&stats, 0, sizeof(mlan_ds_get_stats));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_stats_info(priv, MOAL_IOCTL_WAIT, &stats)) {
		PRINTM(MERROR, "Error getting stats information\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_rate));
	if (req == NULL) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	rate = (mlan_ds_rate *)req->pbuf;
	rate->sub_command = MLAN_OID_GET_DATA_RATE;
	req->req_id = MLAN_IOCTL_RATE;
	req->action = MLAN_ACT_GET;
	ret = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;
	if (rate->param.data_rate.tx_rate_format != MLAN_RATE_FORMAT_LG) {
		if (rate->param.data_rate.tx_rate_format == MLAN_RATE_FORMAT_HT) {
			sinfo->txrate.flags = RATE_INFO_FLAGS_MCS;
			if (rate->param.data_rate.tx_ht_bw == MLAN_HT_BW40)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
				sinfo->txrate.bw = RATE_INFO_BW_40;
#else
				sinfo->txrate.flags |=
					RATE_INFO_FLAGS_40_MHZ_WIDTH;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
			else
				sinfo->txrate.bw = RATE_INFO_BW_20;
#endif
		}
		if (rate->param.data_rate.tx_ht_gi == MLAN_HT_SGI)
			sinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		sinfo->txrate.mcs = rate->param.data_rate.tx_mcs_index;
	} else {
		/* Bit rate is in 500 kb/s units. Convert it to 100kb/s units */
		sinfo->txrate.legacy =
			Rates[rate->param.data_rate.tx_data_rate] * 5;
	}
	sinfo->rx_bytes = priv->stats.rx_bytes;
	sinfo->tx_bytes = priv->stats.tx_bytes;
	sinfo->rx_packets = priv->stats.rx_packets;
	sinfo->tx_packets = priv->stats.tx_packets;
	sinfo->signal = signal.bcn_rssi_avg;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
	sinfo->tx_failed = stats.failed;
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	/* Update BSS information */
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	sinfo->filled |= BIT(NL80211_STA_INFO_BSS_PARAM);
#else
	sinfo->filled |= STATION_INFO_BSS_PARAM;
#endif
	sinfo->bss_param.flags = 0;
	ret = woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
	if (ret)
		goto done;
	if (bss_info.capability_info & WLAN_CAPABILITY_SHORT_PREAMBLE)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_PREAMBLE;
	if (bss_info.capability_info & WLAN_CAPABILITY_SHORT_SLOT_TIME)
		sinfo->bss_param.flags |= BSS_PARAM_FLAGS_SHORT_SLOT_TIME;
	sinfo->bss_param.beacon_interval = bss_info.beacon_interval;
	/* Get DTIM period */
	ret = woal_set_get_dtim_period(priv, MLAN_ACT_GET,
				       MOAL_IOCTL_WAIT, &dtim_period);
	if (ret) {
		PRINTM(MERROR, "Get DTIM period failed\n");
		goto done;
	}
	sinfo->bss_param.dtim_period = dtim_period;
#endif

done:
	if (ret != MLAN_STATUS_PENDING)
		kfree(req);

	LEAVE();
	return ret;
}

/********************************************************
				Global Functions
********************************************************/
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
/**
 * @brief Set all radar channel's dfs_state
 *
 * @param wiphy           A pointer to wiphy structure
 *
 * @return                N/A
 */
void
woal_update_radar_chans_dfs_state(struct wiphy *wiphy)
{
	enum ieee80211_band band;
	struct ieee80211_supported_band *sband;
	int i;
	for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
		sband = wiphy->bands[band];
		if (!sband)
			continue;
		for (i = 0; i < sband->n_channels; i++) {
			if (sband->channels[i].flags & IEEE80211_CHAN_RADAR) {
				if (dfs_offload)
					sband->channels[i].dfs_state =
						NL80211_DFS_AVAILABLE;
				else
					sband->channels[i].dfs_state =
						NL80211_DFS_USABLE;
			}
		}
	}
	PRINTM(MCMND, "Set radar dfs_state: dfs_offload=%d\n", dfs_offload);
}
#endif

/**
 * @brief Request the driver to change regulatory domain
 *
 * @param wiphy           A pointer to wiphy structure
 * @param request         A pointer to regulatory_request structure
 *
 * @return                0
 */
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
static void
#else
static int
#endif
woal_cfg80211_reg_notifier(struct wiphy *wiphy,
			   struct regulatory_request *request)
{
	moal_private *priv = NULL;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	t_u8 region[COUNTRY_CODE_LEN];
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
	int ret = 0;
#endif

	ENTER();

	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	if (!priv) {
		PRINTM(MFATAL, "Unable to get priv in %s()\n", __func__);
		LEAVE();
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
		return -EINVAL;
#else
		return;
#endif
	}

	PRINTM(MIOCTL, "cfg80211 regulatory domain callback "
	       "%c%c\n", request->alpha2[0], request->alpha2[1]);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (dfs_offload)
		woal_update_radar_chans_dfs_state(wiphy);
#endif
	memset(region, 0, sizeof(region));
	memcpy(region, request->alpha2, sizeof(request->alpha2));
	region[2] = ' ';
	if (MTRUE == is_cfg80211_special_region_code(region)) {
		PRINTM(MIOCTL, "Skip configure special region code\n");
		LEAVE();
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
		return ret;
#else
		return;
#endif
	}
	if ((handle->country_code[0] != request->alpha2[0]) ||
	    (handle->country_code[1] != request->alpha2[1])) {
		t_u8 country_code[COUNTRY_CODE_LEN];
		memset(country_code, 0, sizeof(country_code));
		country_code[0] = request->alpha2[0];
		country_code[1] = request->alpha2[1];
		if (cntry_txpwr) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_request_country_power_table(priv,
							     country_code)) {
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
				return -EFAULT;
#else
				return;
#endif
			}
		}
	}
	handle->country_code[0] = request->alpha2[0];
	handle->country_code[1] = request->alpha2[1];
	handle->country_code[2] = ' ';
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_region_code(priv, handle->country_code))
		PRINTM(MERROR, "Set country code failed!\n");
	switch (request->initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
		PRINTM(MIOCTL, "Regulatory domain BY_DRIVER\n");
		break;
	case NL80211_REGDOM_SET_BY_CORE:
		PRINTM(MIOCTL, "Regulatory domain BY_CORE\n");
		break;
	case NL80211_REGDOM_SET_BY_USER:
		PRINTM(MIOCTL, "Regulatory domain BY_USER\n");
		break;
		/* TODO: apply driver specific changes in channel flags based
		   on the request initiator if necessory. * */
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		PRINTM(MIOCTL, "Regulatory domain BY_COUNTRY_IE\n");
		break;
	}
	if (priv->wdev && priv->wdev->wiphy &&
	    (request->initiator != NL80211_REGDOM_SET_BY_COUNTRY_IE))
		woal_send_domain_info_cmd_fw(priv, MOAL_IOCTL_WAIT);

	LEAVE();
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 9, 0)
	return ret;
#endif
}

#ifdef UAP_CFG80211
/**
 * @brief Swithces BSS role of interface
 *
 * @param priv          A pointer to moal_private structure
 * @param wait_option   Wait option (MOAL_IOCTL_WAIT or MOAL_NO_WAIT)
 * @param bss_role      bss role
 *
 * @return         0 --success, otherwise fail
 */
mlan_status
woal_role_switch(moal_private *priv, t_u8 wait_option, t_u8 bss_role)
{
	int ret = 0;
	mlan_ds_bss *bss = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
	if (req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	bss = (mlan_ds_bss *)req->pbuf;
	bss->sub_command = MLAN_OID_BSS_ROLE;
	req->req_id = MLAN_IOCTL_BSS;
	req->action = MLAN_ACT_SET;
	bss->param.bss_role = bss_role;

	status = woal_request_ioctl(priv, req, wait_option);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief request scan
 *
 *  @param priv                 A pointer to moal_private structure
 *  @param scan_cfg             A pointer to wlan_user_scan_cfg structure
 *
 *  @return                     MLAN_STATUS_SUCCESS -- success, otherwise fail
 */
mlan_status
woal_uap_scan(moal_private *priv, wlan_user_scan_cfg *scan_cfg)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_handle *handle = priv->phandle;
	moal_private *tmp_priv;
	u8 role;

	ENTER();
	if (priv->bss_index > 0)
		tmp_priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	else
		tmp_priv = priv;
	if (!tmp_priv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	role = GET_BSS_ROLE(tmp_priv);
	if (role == MLAN_BSS_ROLE_UAP)
		woal_role_switch(tmp_priv, MOAL_IOCTL_WAIT, MLAN_BSS_ROLE_STA);
#ifdef REASSOCIATION
	if (MOAL_ACQ_SEMAPHORE_BLOCK(&handle->reassoc_sem)) {
		PRINTM(MERROR, "Acquire semaphore error, woal_do_combo_scan\n");
		goto done;
	}
#endif /* REASSOCIATION */
	tmp_priv->report_scan_result = MTRUE;
	ret = woal_request_userscan(tmp_priv, MOAL_IOCTL_WAIT, scan_cfg);
	woal_sched_timeout(5);
#ifdef REASSOCIATION
	MOAL_REL_SEMAPHORE(&handle->reassoc_sem);
#endif
done:
	if (role == MLAN_BSS_ROLE_UAP)
		woal_role_switch(tmp_priv, MOAL_IOCTL_WAIT, MLAN_BSS_ROLE_UAP);
	LEAVE();
	return ret;
}
#endif

static int
woal_find_wps_ie_in_probereq(const t_u8 *ie, int len)
{
	int left_len = len;
	const t_u8 *pos = ie;
	t_u8 ie_id, ie_len;
	IEEEtypes_VendorSpecific_t *pvendor_ie = NULL;
	const u8 wps_oui[4] = { 0x00, 0x50, 0xf2, 0x04 };

	while (left_len >= 2) {
		ie_id = *pos;
		ie_len = *(pos + 1);
		if ((ie_len + 2) > left_len)
			break;
		if (ie_id == VENDOR_SPECIFIC_221) {
			pvendor_ie = (IEEEtypes_VendorSpecific_t *)pos;
			if (!memcmp
			    (pvendor_ie->vend_hdr.oui, wps_oui,
			     sizeof(pvendor_ie->vend_hdr.oui)) &&
			    pvendor_ie->vend_hdr.oui_type == wps_oui[3])
				return MTRUE;
		}

		pos += (ie_len + 2);
		left_len -= (ie_len + 2);
	}

	return MFALSE;
}

/**
 *  @brief check if the scan result expired
 *
 *  @param priv         A pointer to moal_private
 *
 *
 *  @return             MTRUE/MFALSE;
 */
t_u8
woal_is_scan_result_expired(moal_private *priv)
{
	mlan_scan_resp scan_resp;
	struct timeval t;
	ENTER();
	if (!woal_is_any_interface_active(priv->phandle)) {
		LEAVE();
		return MTRUE;
	}

	if (MLAN_STATUS_SUCCESS !=
	    woal_get_scan_table(priv, MOAL_IOCTL_WAIT, &scan_resp)) {
		LEAVE();
		return MTRUE;
	}
	do_gettimeofday(&t);
/** scan result expired value */
#define SCAN_RESULT_EXPIRTED      1
	if (t.tv_sec > (scan_resp.age_in_secs + SCAN_RESULT_EXPIRTED)) {
		LEAVE();
		return MTRUE;
	}
	LEAVE();
	return MFALSE;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
/**
 * @brief Request the driver to do a scan. Always returning
 * zero meaning that the scan request is given to driver,
 * and will be valid until passed to cfg80211_scan_done().
 * To inform scan results, call cfg80211_inform_bss().
 *
 * @param wiphy           A pointer to wiphy structure
 * @param request         A pointer to cfg80211_scan_request structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
#else
/**
 * @brief Request the driver to do a scan. Always returning
 * zero meaning that the scan request is given to driver,
 * and will be valid until passed to cfg80211_scan_done().
 * To inform scan results, call cfg80211_inform_bss().
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param request         A pointer to cfg80211_scan_request structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_scan(struct wiphy *wiphy, struct net_device *dev,
		   struct cfg80211_scan_request *request)
#endif
{
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct net_device *dev = request->wdev->netdev;
#endif
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	wlan_user_scan_cfg scan_req;
	mlan_bss_info bss_info;
	struct ieee80211_channel *chan;
	int ret = 0, i;
	unsigned long flags;

	ENTER();

	PRINTM(MINFO, "Received scan request on %s\n", dev->name);
	if (priv->phandle->scan_pending_on_block == MTRUE) {
		PRINTM(MCMND, "scan already in processing...\n");
		LEAVE();
		return -EAGAIN;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	if (priv->last_event & EVENT_BG_SCAN_REPORT) {
		PRINTM(MCMND, "block scan while pending BGSCAN result\n");
		priv->last_event = 0;
		LEAVE();
		return -EAGAIN;
	}
#endif
#if defined(STA_CFG80211) || defined(UAP_CFG80211)
#ifdef WIFI_DIRECT_SUPPORT
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (priv->phandle->is_go_timer_set &&
	    priv->wdev->iftype != NL80211_IFTYPE_P2P_GO) {
		PRINTM(MCMND, "block scan in go timer....\n");
		LEAVE();
		return -EAGAIN;
	}
#endif
#endif
#endif
	if (priv->fake_scan_complete || !woal_is_scan_result_expired(priv)) {
		PRINTM(MEVENT, "Reporting fake scan results\n");
		woal_inform_bss_from_scan_result(priv, NULL, MOAL_IOCTL_WAIT);
		woal_cfg80211_scan_done(request, MFALSE);
		return ret;
	}
	memset(&bss_info, 0, sizeof(bss_info));
	if (MLAN_STATUS_SUCCESS ==
	    woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info)) {
		if (bss_info.scan_block) {
			PRINTM(MEVENT, "Block scan in mlan module\n");
			woal_inform_bss_from_scan_result(priv, NULL,
							 MOAL_IOCTL_WAIT);
			woal_cfg80211_scan_done(request, MFALSE);
			return ret;
		}
	}
	if (priv->phandle->scan_request &&
	    priv->phandle->scan_request != request) {
		PRINTM(MCMND,
		       "different scan_request is coming before previous one is finished on %s...\n",
		       dev->name);
		LEAVE();
		return -EBUSY;
	}
	spin_lock_irqsave(&priv->phandle->scan_req_lock, flags);
	priv->phandle->scan_request = request;
	spin_unlock_irqrestore(&priv->phandle->scan_req_lock, flags);
	memset(&scan_req, 0x00, sizeof(scan_req));
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	if (!is_broadcast_ether_addr(request->bssid)) {
		memcpy(scan_req.specific_bssid, request->bssid, ETH_ALEN);
		PRINTM(MIOCTL, "scan: bssid=" MACSTR "\n",
		       MAC2STR(scan_req.specific_bssid));
	}
#endif

	if (priv->phandle->scan_request->n_channels <= 38)
		scan_req.ext_scan_type = EXT_SCAN_ENHANCE;

#ifdef WIFI_DIRECT_SUPPORT
	if (priv->phandle->miracast_mode ||
	    woal_is_any_interface_active(priv->phandle))
		scan_req.scan_chan_gap = priv->phandle->scan_chan_gap;
	else
		scan_req.scan_chan_gap = 0;
#endif
	for (i = 0; i < priv->phandle->scan_request->n_ssids; i++) {
		memcpy(scan_req.ssid_list[i].ssid,
		       priv->phandle->scan_request->ssids[i].ssid,
		       priv->phandle->scan_request->ssids[i].ssid_len);
		if (priv->phandle->scan_request->ssids[i].ssid_len)
			scan_req.ssid_list[i].max_len = 0;
		else
			scan_req.ssid_list[i].max_len = 0xff;
		PRINTM(MIOCTL, "scan: ssid=%s\n", scan_req.ssid_list[i].ssid);
	}
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT &&
	    priv->phandle->scan_request->n_ssids) {
		if (!memcmp(scan_req.ssid_list[0].ssid, "DIRECT-", 7))
			scan_req.ssid_list[0].max_len = 0xfe;
	}
#endif
#endif
	for (i = 0;
	     i < MIN(WLAN_USER_SCAN_CHAN_MAX,
		     priv->phandle->scan_request->n_channels); i++) {
		chan = priv->phandle->scan_request->channels[i];
		scan_req.chan_list[i].chan_number = chan->hw_value;
		scan_req.chan_list[i].radio_type = chan->band;
		if ((chan->
		     flags & (IEEE80211_CHAN_PASSIVE_SCAN |
			      IEEE80211_CHAN_RADAR))
		    || !priv->phandle->scan_request->n_ssids)
			scan_req.chan_list[i].scan_type =
				MLAN_SCAN_TYPE_PASSIVE;
		else
			scan_req.chan_list[i].scan_type = MLAN_SCAN_TYPE_ACTIVE;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
		scan_req.chan_list[i].scan_time =
			priv->phandle->scan_request->duration;
#else
		scan_req.chan_list[i].scan_time = 0;
#endif
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
		if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT &&
		    priv->phandle->scan_request->n_ssids) {
			if (!memcmp(scan_req.ssid_list[0].ssid, "DIRECT-", 7))
				scan_req.chan_list[i].scan_time =
					MIN_SPECIFIC_SCAN_CHAN_TIME;
		}
#endif
#endif
#ifdef WIFI_DIRECT_SUPPORT
		if (priv->phandle->miracast_mode)
			scan_req.chan_list[i].scan_time =
				priv->phandle->miracast_scan_time;
		else if (woal_is_any_interface_active(priv->phandle))
			scan_req.chan_list[i].scan_time =
				MIN_SPECIFIC_SCAN_CHAN_TIME;
#endif
#ifdef UAP_CFG80211
		if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP)
			scan_req.chan_list[i].scan_time =
				MIN_SPECIFIC_SCAN_CHAN_TIME;
#endif
	}
	if (priv->phandle->scan_request->ie &&
	    priv->phandle->scan_request->ie_len) {
		if (woal_find_wps_ie_in_probereq
		    ((t_u8 *)priv->phandle->scan_request->ie,
		     priv->phandle->scan_request->ie_len)) {
			PRINTM(MIOCTL,
			       "Notify firmware only keep probe response\n");
			scan_req.proberesp_only = MTRUE;
		}
		if (MLAN_STATUS_SUCCESS !=
		    woal_cfg80211_mgmt_frame_ie(priv, NULL, 0,
						NULL, 0, NULL, 0,
						(t_u8 *)priv->phandle->
						scan_request->ie,
						priv->phandle->scan_request->
						ie_len, MGMT_MASK_PROBE_REQ,
						MOAL_IOCTL_WAIT)) {
			PRINTM(MERROR, "Fail to set scan request IE\n");
			ret = -EFAULT;
			goto done;
		}
	} else {
		/** Clear SCAN IE in Firmware */
		if (priv->probereq_index != MLAN_CUSTOM_IE_AUTO_IDX_MASK)
			woal_cfg80211_mgmt_frame_ie(priv, NULL, 0, NULL, 0,
						    NULL, 0, NULL, 0,
						    MGMT_MASK_PROBE_REQ,
						    MOAL_IOCTL_WAIT);
	}
#ifdef UAP_CFG80211
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
	/** use sync scan for uap */
		ret = woal_uap_scan(priv, &scan_req);
		if (!ret) {
			LEAVE();
			return ret;
		} else {
			PRINTM(MERROR, "Uap SCAN failure\n");
			goto done;
		}
	}
#endif
	if (MLAN_STATUS_SUCCESS != woal_do_scan(priv, &scan_req)) {
		PRINTM(MERROR, "woal_do_scan fails!\n");
		ret = -EAGAIN;
		goto done;
	}
done:
	if (ret) {
		spin_lock_irqsave(&priv->phandle->scan_req_lock, flags);
		woal_cfg80211_scan_done(request, MTRUE);
		priv->phandle->scan_request = NULL;
		priv->phandle->scan_priv = NULL;
		spin_unlock_irqrestore(&priv->phandle->scan_req_lock, flags);
	} else
		PRINTM(MMSG, "wlan: %s START SCAN\n", dev->name);
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
static void
woal_cfg80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(wdev->netdev);
	ENTER();
	PRINTM(MMSG, "wlan: ABORT SCAN start\n");
	woal_cancel_scan(priv, MOAL_IOCTL_WAIT);
	LEAVE();
	return;
}
#endif
/**
 * @brief construct and send ft action request
 *
*  @param priv     A pointer to moal_private structure
 * @param ie       A pointer to ft ie
 * @param le       Value of ie len
 * @param bssid    A pointer to target ap bssid
 * @
 * @return         0 -- success, otherwise fail
 */
static int
woal_send_ft_action_requst(moal_private *priv, t_u8 *ie, t_u8 len, t_u8 *bssid,
			   t_u8 *target_ap)
{
	IEEE80211_MGMT *mgmt = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	pmlan_buffer pmbuf = NULL;
	t_u32 pkt_type;
	t_u32 tx_control;
	t_u16 packet_len = 0;
	t_u8 addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	int ret = 0;

	ENTER();

	/* pkt_type + tx_control */
#define HEADER_SIZE				8
	/* frmctl + durationid + addr1 + addr2 + addr3 + seqctl + addr4 */
#define MGMT_HEADER_LEN		(2 + 2 + 6 + 6 + 6 + 2 +6)
	/* 14   = category + action + sta addr + target ap */
#define FT_REQUEST_LEN 14
	packet_len = (t_u16)len + MGMT_HEADER_LEN + FT_REQUEST_LEN;
	pmbuf = woal_alloc_mlan_buffer(priv->phandle,
				       MLAN_MIN_DATA_HEADER_LEN + HEADER_SIZE +
				       packet_len + sizeof(packet_len));
	if (!pmbuf) {
		PRINTM(MERROR, "Fail to allocate mlan_buffer\n");
		ret = -ENOMEM;
		goto done;
	}

	pmbuf->data_offset = MLAN_MIN_DATA_HEADER_LEN;
	pkt_type = MRVL_PKT_TYPE_MGMT_FRAME;
	tx_control = 0;
	/* Add pkt_type and tx_control */
	memcpy(pmbuf->pbuf + pmbuf->data_offset, &pkt_type, sizeof(pkt_type));
	memcpy(pmbuf->pbuf + pmbuf->data_offset + sizeof(pkt_type), &tx_control,
	       sizeof(tx_control));
	/*Add packet len */
	memcpy(pmbuf->pbuf + pmbuf->data_offset + HEADER_SIZE, &packet_len,
	       sizeof(packet_len));

	mgmt = (IEEE80211_MGMT *)(pmbuf->pbuf + pmbuf->data_offset +
				  HEADER_SIZE + sizeof(packet_len));
	memset(mgmt, 0, MGMT_HEADER_LEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, priv->current_addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	memcpy(mgmt->addr4, addr, ETH_ALEN);

	mgmt->u.ft_req.category = 0x06;	/**ft action code 0x6*/
	mgmt->u.ft_req.action = 0x1; /**ft action request*/
	memcpy(mgmt->u.ft_req.sta_addr, priv->current_addr, ETH_ALEN);
	memcpy(mgmt->u.ft_req.target_ap_addr, target_ap, ETH_ALEN);

	if (ie && len)
		memcpy((t_u8 *)(&mgmt->u.ft_req.variable), ie, len);

	pmbuf->data_len = HEADER_SIZE + packet_len + sizeof(packet_len);
	pmbuf->buf_type = MLAN_BUF_TYPE_RAW_DATA;
	pmbuf->bss_index = priv->bss_index;
	pmbuf->priority = 7;

	status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);

	switch (status) {
	case MLAN_STATUS_PENDING:
		atomic_inc(&priv->phandle->tx_pending);
		queue_work(priv->phandle->workqueue, &priv->phandle->main_work);
		break;
	case MLAN_STATUS_SUCCESS:
		woal_free_mlan_buffer(priv->phandle, pmbuf);
		break;
	case MLAN_STATUS_FAILURE:
	default:
		woal_free_mlan_buffer(priv->phandle, pmbuf);
		ret = -EFAULT;
		break;
	}

done:
	LEAVE();
	return ret;
}

/**
 * @brief construct and send ft auth request
 *
*  @param priv     A pointer to moal_private structure
 * @param ie       A pointer to ft ie
 * @param le       Value of ie len
 * @param bssid    A pointer to target ap bssid
 * @
 * @return         0 -- success, otherwise fail
 */
static int
woal_send_ft_auth_requst(moal_private *priv, t_u8 *ie, t_u8 len, t_u8 *bssid)
{
	IEEE80211_MGMT *mgmt = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	pmlan_buffer pmbuf = NULL;
	t_u32 pkt_type;
	t_u32 tx_control;
	t_u16 packet_len = 0;
	t_u8 addr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	int ret = 0;

	ENTER();
	/* pkt_type + tx_control */
#define HEADER_SIZE				8
	/* frmctl + durationid + addr1 + addr2 + addr3 + seqctl + addr4 */
#define MGMT_HEADER_LEN		(2 + 2 + 6 + 6 + 6 + 2 +6)
	/* 6   = auth_alg + auth_transaction +auth_status */
#define AUTH_BODY_LEN 6
	packet_len = (t_u16)len + MGMT_HEADER_LEN + AUTH_BODY_LEN;
	pmbuf = woal_alloc_mlan_buffer(priv->phandle,
				       MLAN_MIN_DATA_HEADER_LEN + HEADER_SIZE +
				       packet_len + sizeof(packet_len));
	if (!pmbuf) {
		PRINTM(MERROR, "Fail to allocate mlan_buffer\n");
		ret = -ENOMEM;
		goto done;
	}

	pmbuf->data_offset = MLAN_MIN_DATA_HEADER_LEN;
	pkt_type = MRVL_PKT_TYPE_MGMT_FRAME;
	tx_control = 0;
	/* Add pkt_type and tx_control */
	memcpy(pmbuf->pbuf + pmbuf->data_offset, &pkt_type, sizeof(pkt_type));
	memcpy(pmbuf->pbuf + pmbuf->data_offset + sizeof(pkt_type), &tx_control,
	       sizeof(tx_control));
	/*Add packet len */
	memcpy(pmbuf->pbuf + pmbuf->data_offset + HEADER_SIZE, &packet_len,
	       sizeof(packet_len));

	mgmt = (IEEE80211_MGMT *)(pmbuf->pbuf + pmbuf->data_offset +
				  HEADER_SIZE + sizeof(packet_len));
	memset(mgmt, 0, MGMT_HEADER_LEN);
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_AUTH);
	memcpy(mgmt->da, bssid, ETH_ALEN);
	memcpy(mgmt->sa, priv->current_addr, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	memcpy(mgmt->addr4, addr, ETH_ALEN);

	mgmt->u.auth.auth_alg = cpu_to_le16(WLAN_AUTH_FT);
	mgmt->u.auth.auth_transaction = cpu_to_le16(1);
	mgmt->u.auth.status_code = cpu_to_le16(0);
	if (ie && len)
		memcpy((t_u8 *)(&mgmt->u.auth.variable), ie, len);

	pmbuf->data_len = HEADER_SIZE + packet_len + sizeof(packet_len);
	pmbuf->buf_type = MLAN_BUF_TYPE_RAW_DATA;
	pmbuf->bss_index = priv->bss_index;
	pmbuf->priority = 7;

	status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);

	switch (status) {
	case MLAN_STATUS_PENDING:
		atomic_inc(&priv->phandle->tx_pending);
		queue_work(priv->phandle->workqueue, &priv->phandle->main_work);
		break;
	case MLAN_STATUS_SUCCESS:
		woal_free_mlan_buffer(priv->phandle, pmbuf);
		break;
	case MLAN_STATUS_FAILURE:
	default:
		woal_free_mlan_buffer(priv->phandle, pmbuf);
		ret = -EFAULT;
		break;
	}

done:
	LEAVE();
	return ret;
}

/**
 * @brief connect the AP through ft over air.
 *
 * @param priv            A pointer to moal_private structure
 * @param bssid           A pointer to bssid
 * @param chan            struct ieee80211_channel
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_connect_ft_over_air(moal_private *priv, t_u8 *bssid,
			 struct ieee80211_channel *chan)
{
	struct wiphy *wiphy = priv->wdev->wiphy;
	mlan_bss_info bss_info;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	t_u8 status = 0;
#endif
	t_u8 wait_option = MOAL_IOCTL_WAIT;
	int ret = 0;
	long timeout = 0;

	ENTER();

	if (!bssid) {
		PRINTM(MERROR,
		       "Invalid bssid, unable to connect AP to through FT\n");
		LEAVE();
		return -EFAULT;
	}

	if (!priv->ft_roaming_triggered_by_driver) {
		wait_option = MOAL_IOCTL_WAIT;
		memset(&bss_info, 0, sizeof(bss_info));
		woal_get_bss_info(priv, wait_option, &bss_info);
	}

	if (priv->ft_roaming_triggered_by_driver || (priv->media_connected &&
						     bss_info.mdid ==
						     priv->ft_md &&
						     bss_info.ft_cap ==
						     priv->ft_cap)) {
		ret = MTRUE;

		/*enable auth register frame */
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
		woal_cfg80211_mgmt_frame_register(wiphy, priv->netdev,
						  IEEE80211_STYPE_AUTH, MTRUE);
#else
		woal_cfg80211_mgmt_frame_register(wiphy, priv->wdev,
						  IEEE80211_STYPE_AUTH, MTRUE);
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
#define AUTH_TX_DEFAULT_WAIT_TIME  1200
		woal_cfg80211_remain_on_channel_cfg(priv, wait_option, MFALSE,
						    &status, chan, 0,
						    AUTH_TX_DEFAULT_WAIT_TIME);
#endif
		/*construct auth request and send out */
		woal_send_ft_auth_requst(priv, priv->ft_ie, priv->ft_ie_len,
					 bssid);
		PRINTM(MMSG, "wlan: send out FT auth,wait for auth response\n");
		/*wait until received auth response */
		priv->ft_wait_condition = MFALSE;
		timeout =
			wait_event_timeout(priv->ft_wait_q,
					   priv->ft_wait_condition, 1 * HZ);
		if (!timeout) {
			/*connet fail */
			if (!priv->ft_roaming_triggered_by_driver) {
				woal_inform_bss_from_scan_result(priv, NULL,
								 wait_option);
				cfg80211_connect_result(priv->netdev,
							priv->cfg_bssid, NULL,
							0, NULL, 0,
							WLAN_STATUS_SUCCESS,
							GFP_KERNEL);
			}
			priv->ft_roaming_triggered_by_driver = MFALSE;
			PRINTM(MMSG,
			       "wlan: keep connected to bssid " MACSTR "\n",
			       MAC2STR(priv->cfg_bssid));
		} else {
			PRINTM(MMSG, "wlan: FT auth received \n");
			memcpy(priv->target_ap_bssid, bssid, ETH_ALEN);
		}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		woal_cfg80211_remain_on_channel_cfg(priv, wait_option, MTRUE,
						    &status, NULL, 0, 0);
#endif
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
		woal_cfg80211_mgmt_frame_register(wiphy, priv->netdev,
						  IEEE80211_STYPE_AUTH, MFALSE);
#else
		woal_cfg80211_mgmt_frame_register(wiphy, priv->wdev,
						  IEEE80211_STYPE_AUTH, MFALSE);
#endif
	}

	LEAVE();
	return ret;
}

/**
 * @brief connect the AP through ft over DS.
 *
 * @param priv            A pointer to moal_private structure
 * @param bssid           A pointer to bssid
 * @param chan            struct ieee80211_channel
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_connect_ft_over_ds(moal_private *priv, t_u8 *bssid,
			struct ieee80211_channel *pchan)
{
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	t_u8 status = 0;
#endif
	t_u8 wait_option = MOAL_IOCTL_WAIT;
	struct ieee80211_channel chan;
	mlan_bss_info bss_info;
	int ret = 0;
	long timeout = 0;

	ENTER();

	if (!priv->ft_roaming_triggered_by_driver)
		wait_option = MOAL_IOCTL_WAIT;

	memset(&bss_info, 0, sizeof(bss_info));
	woal_get_bss_info(priv, wait_option, &bss_info);
	chan.band = (bss_info.bss_chan < 36) ?
		IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	chan.center_freq = ieee80211_channel_to_frequency(bss_info.bss_chan
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
							  , chan.band
#endif
		);

	if (priv->media_connected) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		woal_cfg80211_remain_on_channel_cfg(priv, wait_option, MFALSE,
						    &status, &chan, 0, 1200);
#endif
		/*construct ft action request and send out */
		woal_send_ft_action_requst(priv, priv->ft_ie, priv->ft_ie_len,
					   (t_u8 *)priv->cfg_bssid, bssid);
		PRINTM(MMSG,
		       "wlan: send out FT request,wait for FT response\n");
		/*wait until received auth response */
		priv->ft_wait_condition = MFALSE;
		timeout =
			wait_event_timeout(priv->ft_wait_q,
					   priv->ft_wait_condition, 1 * HZ);
		if (!timeout) {
			/*go over air, as current AP may be unreachable */
			PRINTM(MMSG, "wlan: go over air\n");
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
			woal_cfg80211_remain_on_channel_cfg(priv, wait_option,
							    MTRUE, &status,
							    NULL, 0, 0);
#endif
			woal_connect_ft_over_air(priv, bssid, pchan);
			LEAVE();
			return ret;
		} else {
			PRINTM(MMSG, "wlan: received FT response\n");
			memcpy(priv->target_ap_bssid, bssid, ETH_ALEN);
		}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
		woal_cfg80211_remain_on_channel_cfg(priv, wait_option, MTRUE,
						    &status, NULL, 0, 0);
#endif

	}

	LEAVE();
	return ret;
}

/**
 * @brief start FT Roaming.
 *
 * @param priv               A pointer to moal_private structure
 * @param ssid_bssid         A pointer to mlan_ssid_bssid structure
 *
 *
 * @return                   0 -- success, otherwise fail
 */
static int
woal_start_ft_roaming(moal_private *priv, mlan_ssid_bssid *ssid_bssid)
{
	struct ieee80211_channel chan;
	int ret = 0;

	ENTER();
	PRINTM(MEVENT, "Try to start FT roaming......\n");
	chan.band = (ssid_bssid->channel < 36) ?
		IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
	chan.center_freq = ieee80211_channel_to_frequency(ssid_bssid->channel
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
							  , chan.band
#endif
		);

	priv->ft_roaming_triggered_by_driver = MTRUE;
	if (!(priv->last_event & EVENT_PRE_BCN_LOST) &&
	    (ssid_bssid->ft_cap & MBIT(0))) {
		woal_connect_ft_over_ds(priv, (t_u8 *)&ssid_bssid->bssid,
					&chan);
	} else {
		/*if pre beacon lost, it need to send auth request instead ft action request when ft over ds */

		woal_connect_ft_over_air(priv, (t_u8 *)&ssid_bssid->bssid,
					 &chan);
	}

	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to connect to the ESS with
 * the specified parameters from kernel
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param sme             A pointer to cfg80211_connect_params structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
		      struct cfg80211_connect_params *sme)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;
	mlan_bss_info bss_info;
	unsigned long flags;
	mlan_ds_misc_assoc_rsp assoc_rsp;
	IEEEtypes_AssocRsp_t *passoc_rsp = NULL;
	mlan_ssid_bssid ssid_bssid;
	moal_handle *handle = priv->phandle;
	int i;

	ENTER();

	PRINTM(MINFO, "Received association request on %s\n", dev->name);
#ifdef UAP_CFG80211
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		LEAVE();
		return 0;
	}
#endif
	if (priv->wdev->iftype != NL80211_IFTYPE_STATION
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
	    && priv->wdev->iftype != NL80211_IFTYPE_P2P_CLIENT
#endif /* KERNEL_VERSION */
#endif /* WIFI_DIRECT_SUPPORT && V14_FEATURE */
		) {
		PRINTM(MERROR,
		       "Received infra assoc request when station not in infra mode\n");
		LEAVE();
		return -EINVAL;
	}

	memset(&ssid_bssid, 0, sizeof(ssid_bssid));
	memcpy(&ssid_bssid.ssid.ssid, sme->ssid, sme->ssid_len);
	ssid_bssid.ssid.ssid_len = sme->ssid_len;
	if (sme->bssid)
		memcpy(&ssid_bssid.bssid, sme->bssid, ETH_ALEN);
	/* Not allowed to connect to the same AP which is already connected
	   with other interface */
	for (i = 0; i < handle->priv_num; i++) {
		if (handle->priv[i] != priv &&
		    MTRUE == woal_is_connected(handle->priv[i], &ssid_bssid)) {
			PRINTM(MMSG,
			       "wlan: already connected with other interface, bssid "
			       MACSTR "\n",
			       MAC2STR(handle->priv[i]->cfg_bssid));
			LEAVE();
			return -EINVAL;
		}
	}

	/** cancel pending scan */
	woal_cancel_scan(priv, MOAL_IOCTL_WAIT);

#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT
	    && (priv->wdev->iftype == NL80211_IFTYPE_STATION
		|| priv->wdev->iftype == NL80211_IFTYPE_P2P_CLIENT)) {
		/* if bsstype == wifi direct, and iftype == station or p2p client,
		 * that means wpa_supplicant wants to enable wifi direct
		 * functionality, so we should init p2p client.
		 *
		 * Note that due to kernel iftype check, ICS wpa_supplicant
		 * could not updaet iftype to init p2p client, so we have to
		 * done it here.
		 * */
		if (MLAN_STATUS_SUCCESS != woal_cfg80211_init_p2p_client(priv)) {
			PRINTM(MERROR,
			       "Init p2p client for wpa_supplicant failed.\n");
			ret = -EFAULT;

			LEAVE();
			return ret;
		}
	}
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
		/* WAR for P2P connection with Samsung TV */
		woal_sched_timeout(200);
	}
#endif
#endif
	/*Fast BSS Transition use ft-over-air */
	if (priv->media_connected && priv->ft_ie_len &&
	    !(priv->ft_cap & MBIT(0))) {
		ret = woal_connect_ft_over_air(priv, (t_u8 *)sme->bssid,
					       sme->channel);
		if (ret == MTRUE) {
			LEAVE();
			return 0;
		}
	}
	priv->cfg_connect = MTRUE;
	if (priv->scan_type == MLAN_SCAN_TYPE_PASSIVE)
		woal_set_scan_type(priv, MLAN_SCAN_TYPE_ACTIVE);
	priv->assoc_status = 0;
	ret = woal_cfg80211_assoc(priv, (void *)sme, MOAL_IOCTL_WAIT);

	if (priv->scan_type == MLAN_SCAN_TYPE_PASSIVE)
		woal_set_scan_type(priv, MLAN_SCAN_TYPE_PASSIVE);
	if (!ret) {
		memset(&assoc_rsp, 0, sizeof(mlan_ds_misc_assoc_rsp));
		woal_get_assoc_rsp(priv, &assoc_rsp, MOAL_IOCTL_WAIT);
		passoc_rsp = (IEEEtypes_AssocRsp_t *)assoc_rsp.assoc_resp_buf;
		priv->rssi_low = DEFAULT_RSSI_LOW_THRESHOLD;
		if (priv->bss_type == MLAN_BSS_TYPE_STA)
			woal_save_conn_params(priv, sme);
		memset(&bss_info, 0, sizeof(bss_info));
		woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
		priv->channel = bss_info.bss_chan;
	}
	spin_lock_irqsave(&priv->connect_lock, flags);
	priv->cfg_connect = MFALSE;
	if (!ret && priv->media_connected) {
		PRINTM(MMSG,
		       "wlan: Connected to bssid " MACSTR " successfully\n",
		       MAC2STR(priv->cfg_bssid));
		spin_unlock_irqrestore(&priv->connect_lock, flags);
		cfg80211_connect_result(priv->netdev, priv->cfg_bssid, NULL, 0,
					passoc_rsp->ie_buffer,
					assoc_rsp.assoc_resp_len -
					ASSOC_RESP_FIXED_SIZE,
					WLAN_STATUS_SUCCESS, GFP_KERNEL);
	} else {
		PRINTM(MINFO, "wlan: Failed to connect to bssid " MACSTR "\n",
		       MAC2STR(priv->cfg_bssid));
		memset(priv->cfg_bssid, 0, ETH_ALEN);
		priv->ft_ie_len = 0;
		priv->ft_pre_connect = MFALSE;
		spin_unlock_irqrestore(&priv->connect_lock, flags);
		cfg80211_connect_result(priv->netdev, priv->cfg_bssid, NULL, 0,
					NULL, 0, woal_get_assoc_status(priv),
					GFP_KERNEL);

	}
	LEAVE();
	return 0;
}

/**
 * @brief Request the driver to disconnect
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param reason_code     Reason code
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
			 t_u16 reason_code)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	long start_time;

	ENTER();
	PRINTM(MMSG,
	       "wlan: Received disassociation request on %s, reason: %u\n",
	       dev->name, reason_code);
#ifdef UAP_CFG80211
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		LEAVE();
		return 0;
	}
#endif
	priv->phandle->driver_state = woal_check_driver_status(priv->phandle);
	if (priv->phandle->driver_state) {
		PRINTM(MERROR,
		       "Block woal_cfg80211_disconnect in abnormal driver state\n");
		LEAVE();
		return -EFAULT;
	}

	if (priv->cfg_disconnect) {
		PRINTM(MERROR, "Disassociation already in progress\n");
		LEAVE();
		return -EBUSY;
	}

	if (priv->media_connected == MFALSE) {
		PRINTM(MMSG, " Already disconnected\n");
		LEAVE();
		return 0;
	}

	priv->cfg_disconnect = MTRUE;
	start_time = jiffies;
	if (woal_disconnect
	    (priv, MOAL_IOCTL_WAIT_TIMEOUT, priv->cfg_bssid,
	     reason_code) != MLAN_STATUS_SUCCESS) {
		priv->cfg_disconnect = MFALSE;
		LEAVE();
		return -EFAULT;
	}
       /**Add delay to avoid auth failure after wps success */
	if ((jiffies - start_time) < 1 * HZ)
		woal_sched_timeout(1500);

	priv->cfg_disconnect = MFALSE;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	if (priv->wdev->iftype == NL80211_IFTYPE_STATION)
		cfg80211_disconnected(priv->netdev, 0, NULL, 0,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
				      true,
#endif
				      GFP_KERNEL);
#endif

	memset(priv->cfg_bssid, 0, ETH_ALEN);
	if (priv->bss_type == MLAN_BSS_TYPE_STA)
		woal_clear_conn_params(priv);
	priv->channel = 0;

	LEAVE();
	return 0;
}

/**
 * @brief Request the driver to get the station information
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param mac             MAC address of the station
 * @param sinfo           A pointer to station_info structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			  const t_u8 *mac,
#else
			  t_u8 *mac,
#endif
			  struct station_info *sinfo)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();

#ifdef UAP_CFG80211
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		LEAVE();
		return woal_uap_cfg80211_get_station(wiphy, dev, mac, sinfo);
	}
#endif
	if (priv->media_connected == MFALSE) {
		PRINTM(MINFO, "cfg80211: Media not connected!\n");
		LEAVE();
		return -ENOENT;
	}

	if (MLAN_STATUS_SUCCESS != woal_cfg80211_dump_station_info(priv, sinfo)) {
		PRINTM(MERROR, "cfg80211: Failed to get station info\n");
		ret = -EFAULT;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	woal_check_auto_tdls(wiphy, dev);
#endif
	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to dump the station information
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param idx             Station index
 * @param mac             MAC address of the station
 * @param sinfo           A pointer to station_info structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_dump_station(struct wiphy *wiphy,
			   struct net_device *dev, int idx,
			   t_u8 *mac, struct station_info *sinfo)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();

#ifdef UAP_CFG80211
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		LEAVE();
		return woal_uap_cfg80211_dump_station(wiphy, dev, idx, mac,
						      sinfo);
	}
#endif

	if (!priv->media_connected || idx != 0) {
		PRINTM(MINFO,
		       "cfg80211: Media not connected or not for this station!\n");
		LEAVE();
		return -ENOENT;
	}

	memcpy(mac, priv->cfg_bssid, ETH_ALEN);

	if (MLAN_STATUS_SUCCESS != woal_cfg80211_dump_station_info(priv, sinfo)) {
		PRINTM(MERROR, "cfg80211: Failed to get station info\n");
		ret = -EFAULT;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Convert driver band configuration to IEEE band type
 *
 *  @param bandcfg  Driver band configuration
 *
 *  @return         IEEE band type
 */
t_u8
woal_bandcfg_to_ieee_band(Band_Config_t bandcfg)
{
	t_u8 ret_radio_type = 0;

	ENTER();

	switch (bandcfg.chanBand) {
	case BAND_5GHZ:
		ret_radio_type = IEEE80211_BAND_5GHZ;
		break;
	case BAND_2GHZ:
	default:
		ret_radio_type = IEEE80211_BAND_2GHZ;
		break;
	}
	LEAVE();
	return ret_radio_type;
}

/**
 * @brief Request the driver to dump survey info
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param idx             Station index
 * @param survey          A pointer to survey_info structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *dev,
			  int idx, struct survey_info *survey)
{
	int ret = -ENOENT;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	enum ieee80211_band band;
	ChanStatistics_t *pchan_stats = NULL;
	mlan_scan_resp scan_resp;

	ENTER();
	PRINTM(MIOCTL, "dump_survey idx=%d\n", idx);

	memset(&scan_resp, 0, sizeof(scan_resp));
	if (MLAN_STATUS_SUCCESS != woal_get_scan_table(priv,
						       MOAL_IOCTL_WAIT,
						       &scan_resp)) {
		ret = -EFAULT;
		goto done;
	}
	pchan_stats = (ChanStatistics_t *)scan_resp.pchan_stats;
	if (idx > scan_resp.num_in_chan_stats || idx < 0) {
		ret = -EFAULT;
		goto done;
	}
	if (idx == scan_resp.num_in_chan_stats ||
	    !pchan_stats[idx].cca_scan_duration)
		goto done;
	ret = 0;
	memset(survey, 0, sizeof(*survey));
	band = woal_bandcfg_to_ieee_band(pchan_stats[idx].bandcfg);
	survey->channel =
		ieee80211_get_channel(wiphy,
				      ieee80211_channel_to_frequency(pchan_stats
								     [idx].
								     chan_num
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
								     , band
#endif
				      ));
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = pchan_stats[idx].noise;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
	survey->filled |= SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY;
	survey->time = pchan_stats[idx].cca_scan_duration;
	survey->time_busy = pchan_stats[idx].cca_busy_duration;
#else
	survey->filled |=
		SURVEY_INFO_CHANNEL_TIME | SURVEY_INFO_CHANNEL_TIME_BUSY;
	survey->channel_time = pchan_stats[idx].cca_scan_duration;
	survey->channel_time_busy = pchan_stats[idx].cca_busy_duration;
#endif
#endif
done:
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
/**
 * @brief Function gets channel info from cfg80211
 *
 * @param wiphy           A pointer to wiphy structure
 * @param wdev            A pointer to wireless_dev structure
 * @param chandef         A pointer to cfg80211_chan_def
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_get_channel(struct wiphy *wiphy,
			  struct wireless_dev *wdev,
			  struct cfg80211_chan_def *chandef)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(wdev->netdev);
	chan_band_info channel;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);

	memset(&channel, 0x00, sizeof(channel));

	if (wdev->iftype == NL80211_IFTYPE_MONITOR) {
		if ((handle->mon_if) &&
		    (handle->mon_if->mon_ndev == wdev->netdev)) {
			*chandef = handle->mon_if->chandef;
			return 0;
		}
		return -EFAULT;
	}
#ifdef UAP_SUPPORT
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		if (priv->bss_started == MTRUE) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_set_get_ap_channel(priv, MLAN_ACT_GET,
						    MOAL_IOCTL_WAIT,
						    &channel)) {
				PRINTM(MERROR, "Fail to get ap channel \n");
				return -EFAULT;
			}
		} else {
			PRINTM(MERROR, "get_channel when AP is not started\n");
			return -EFAULT;
		}
	} else
#endif
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) {
		if (priv->media_connected == MTRUE) {
			if (MLAN_STATUS_SUCCESS !=
			    woal_get_sta_channel(priv, MOAL_IOCTL_WAIT,
						 &channel)) {
				PRINTM(MERROR, "Fail to get sta channel \n");
				return -EFAULT;
			}
		} else {
			PRINTM(MERROR,
			       "get_channel when STA is not connected\n");
			return -EFAULT;
		}
	} else {
		PRINTM(MERROR, "BssRole not support %d.\n", GET_BSS_ROLE(priv));
		return -EFAULT;
	}

	if (MLAN_STATUS_FAILURE == woal_chandef_create(priv, chandef, &channel))
		return -EFAULT;
	else
		return 0;
}
#endif

/**
 * @brief Request the driver to Join the specified
 * IBSS (or create if necessary)
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param params          A pointer to cfg80211_ibss_params structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *dev,
			struct cfg80211_ibss_params *params)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;

	ENTER();

	if (priv->wdev->iftype != NL80211_IFTYPE_ADHOC) {
		PRINTM(MERROR,
		       "Request IBSS join received when station not in ibss mode\n");
		LEAVE();
		return -EINVAL;
	}

	ret = woal_cfg80211_assoc(priv, (void *)params, MOAL_IOCTL_WAIT);

	if (!ret) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
		cfg80211_ibss_joined(priv->netdev, priv->cfg_bssid,
				     params->chandef.chan, GFP_KERNEL);
#else
		cfg80211_ibss_joined(priv->netdev, priv->cfg_bssid, GFP_KERNEL);
#endif
		PRINTM(MINFO, "Joined/created adhoc network with bssid"
		       MACSTR " successfully\n", MAC2STR(priv->cfg_bssid));
	} else {
		PRINTM(MINFO, "Failed creating/joining adhoc network\n");
		memset(priv->cfg_bssid, 0, ETH_ALEN);
	}

	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to leave the IBSS
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();

	if (priv->cfg_disconnect) {
		PRINTM(MERROR, "IBSS leave already in progress\n");
		LEAVE();
		return -EBUSY;
	}

	if (priv->media_connected == MFALSE) {
		LEAVE();
		return -EINVAL;
	}

	priv->cfg_disconnect = 1;

	PRINTM(MINFO, "Leaving from IBSS " MACSTR "\n",
	       MAC2STR(priv->cfg_bssid));
	if (woal_disconnect
	    (priv, MOAL_IOCTL_WAIT, priv->cfg_bssid,
	     DEF_DEAUTH_REASON_CODE) != MLAN_STATUS_SUCCESS) {
		priv->cfg_disconnect = 0;
		LEAVE();
		return -EFAULT;
	}
	priv->cfg_disconnect = 0;
	memset(priv->cfg_bssid, 0, ETH_ALEN);

	LEAVE();
	return 0;
}

/**
 * @brief Request the driver to change the IEEE power save
 * mdoe
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param enabled         Enable or disable
 * @param timeout         Timeout value
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_set_power_mgmt(struct wiphy *wiphy,
			     struct net_device *dev, bool enabled, int timeout)
{
	int ret = 0, disabled;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();
	if (hw_test || (ps_mode == MLAN_INIT_PARA_DISABLED)) {
		PRINTM(MIOCTL, "block set power hw_test=%d ps_mode=%d\n",
		       hw_test, ps_mode);
		LEAVE();
		return -EFAULT;
	}
	priv->phandle->driver_state = woal_check_driver_status(priv->phandle);
	if (priv->phandle->driver_state) {
		PRINTM(MERROR,
		       "Block woal_cfg80211_set_power_mgmt in abnormal driver state\n");
		LEAVE();
		return -EFAULT;
	}
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (priv->bss_type == MLAN_BSS_TYPE_WIFIDIRECT) {
		PRINTM(MIOCTL, "skip set power for p2p interface\n");
		LEAVE();
		return ret;
	}
#endif
#endif
	if (enabled)
		disabled = 0;
	else
		disabled = 1;

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_power_mgmt(priv, MLAN_ACT_SET, &disabled, timeout,
				    MOAL_IOCTL_WAIT)) {
		ret = -EOPNOTSUPP;
	}

	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
/**
 * @brief Request the driver to get the transmit power info
 *
 * @param wiphy           A pointer to wiphy structure
 * @param type            TX power adjustment type
 * @param dbm             TX power in dbm
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_get_tx_power(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
			   struct wireless_dev *wdev,
#endif
			   int *dbm)
{
	int ret = 0;
	moal_private *priv = NULL;
	mlan_power_cfg_t power_cfg;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);

	ENTER();

	if (!handle) {
		PRINTM(MFATAL, "Unable to get handle\n");
		LEAVE();
		return -EFAULT;
	}

	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);

	if (!priv) {
		PRINTM(MFATAL, "Unable to get priv in %s()\n", __func__);
		LEAVE();
		return -EFAULT;
	}

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_tx_power(priv, MLAN_ACT_GET, &power_cfg)) {
		LEAVE();
		return -EFAULT;
	}

	*dbm = power_cfg.power_level;

	LEAVE();
	return ret;
}

/**
 * @brief Request the driver to change the transmit power
 *
 * @param wiphy           A pointer to wiphy structure
 * @param type            TX power adjustment type
 * @param dbm             TX power in dbm
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_set_tx_power(struct wiphy *wiphy,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
			   struct wireless_dev *wdev,
#endif
#if CFG80211_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
			   enum tx_power_setting type,
#else
			   enum nl80211_tx_power_setting type,
#endif
			   int dbm)
{
	int ret = 0;
	moal_private *priv = NULL;
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	mlan_power_cfg_t power_cfg;

	ENTER();

	priv = woal_get_priv(handle, MLAN_BSS_ROLE_ANY);
	if (!priv) {
		PRINTM(MFATAL, "Unable to get priv in %s()\n", __func__);
		LEAVE();
		return -EFAULT;
	}

	if (type) {
		power_cfg.is_power_auto = 0;
		power_cfg.power_level = dbm;
	} else
		power_cfg.is_power_auto = 1;

	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_tx_power(priv, MLAN_ACT_SET, &power_cfg))
		ret = -EFAULT;

	LEAVE();
	return ret;
}
#endif

#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
/**
 * CFG802.11 operation handler for connection quality monitoring.
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param rssi_thold	  rssi threshold
 * @param rssi_hyst		  rssi hysteresis
 */
static int
woal_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
				  struct net_device *dev,
				  s32 rssi_thold, u32 rssi_hyst)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	ENTER();
	priv->cqm_rssi_thold = rssi_thold;
	priv->cqm_rssi_high_thold = rssi_thold;
	priv->cqm_rssi_hyst = rssi_hyst;

	PRINTM(MIOCTL, "rssi_thold=%d rssi_hyst=%d\n",
	       (int)rssi_thold, (int)rssi_hyst);
	woal_set_rssi_threshold(priv, 0, MOAL_IOCTL_WAIT);
	LEAVE();
	return 0;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
/**
 * @brief remain on channel config
 *
 * @param priv              A pointer to moal_private structure
 * @param wait_option       Wait option
 * @param cancel			cancel remain on channel flag
 * @param status            A pointer to status, success, in process or reject
 * @param chan              A pointer to ieee80211_channel structure
 * @param channel_type      channel_type,
 * @param duration          Duration wait to receive frame
 *
 * @return                  0 -- success, otherwise fail
 */
int
woal_cfg80211_remain_on_channel_cfg(moal_private *priv,
				    t_u8 wait_option, t_u8 remove, t_u8 *status,
				    struct ieee80211_channel *chan,
				    enum nl80211_channel_type channel_type,
				    t_u32 duration)
{
	mlan_ds_remain_chan chan_cfg;
	int ret = 0;

	ENTER();

	if (!status || (!chan && !remove)) {
		LEAVE();
		return -EFAULT;
	}
	memset(&chan_cfg, 0, sizeof(mlan_ds_remain_chan));
	if (remove) {
		chan_cfg.remove = MTRUE;
	} else {
#ifdef WIFI_DIRECT_SUPPORT
		if (priv->phandle->is_go_timer_set) {
			PRINTM(MINFO,
			       "block remain on channel while go timer is on\n");
			LEAVE();
			return -EBUSY;
		}
#endif
		if (chan->band == IEEE80211_BAND_2GHZ)
			chan_cfg.bandcfg.chanBand = BAND_2GHZ;
		else if (chan->band == IEEE80211_BAND_5GHZ)
			chan_cfg.bandcfg.chanBand = BAND_5GHZ;
		switch (channel_type) {
		case NL80211_CHAN_HT40MINUS:
			chan_cfg.bandcfg.chan2Offset = SEC_CHAN_BELOW;
			break;
		case NL80211_CHAN_HT40PLUS:
			chan_cfg.bandcfg.chan2Offset = SEC_CHAN_ABOVE;
			break;

		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
		default:
			break;
		}
		chan_cfg.channel =
			ieee80211_frequency_to_channel(chan->center_freq);
		chan_cfg.remain_period = duration;
	}
	if (MLAN_STATUS_SUCCESS ==
	    woal_set_remain_channel_ioctl(priv, wait_option, &chan_cfg))
		*status = chan_cfg.status;
	else
		ret = -EFAULT;
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
/**
 * @brief tx mgmt frame
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param wdev                  A pointer to wireless_dev structure
 * @param cookie                A pointer to frame cookie
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
				  struct wireless_dev *wdev, u64 cookie)
#else
/**
 * @brief tx mgmt frame
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param cookie                A pointer to frame cookie
 *
 * @return                0 -- success, otherwise fail
 */
static int
woal_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy,
				  struct net_device *dev, u64 cookie)
#endif
{
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct net_device *dev = wdev->netdev;
#endif
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;
	t_u8 status = 1;
	moal_private *remain_priv = NULL;

	ENTER();

	if (priv->phandle->remain_on_channel) {
		remain_priv =
			priv->phandle->priv[priv->phandle->remain_bss_index];
		if (!remain_priv) {
			PRINTM(MERROR,
			       "mgmt_tx_cancel_wait: Wrong remain_bss_index=%d\n",
			       priv->phandle->remain_bss_index);
			ret = -EFAULT;
			goto done;
		}
		if (woal_cfg80211_remain_on_channel_cfg
		    (remain_priv, MOAL_IOCTL_WAIT, MTRUE, &status, NULL, 0,
		     0)) {
			PRINTM(MERROR,
			       "mgmt_tx_cancel_wait: Fail to cancel remain on channel\n");
			ret = -EFAULT;
			goto done;
		}
		if (priv->phandle->cookie) {
			cfg80211_remain_on_channel_expired(
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
								  remain_priv->
								  netdev,
#else
								  remain_priv->
								  wdev,
#endif
								  priv->
								  phandle->
								  cookie,
								  &priv->
								  phandle->chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
								  priv->
								  phandle->
								  channel_type,
#endif
								  GFP_ATOMIC);
			priv->phandle->cookie = 0;
		}
		priv->phandle->remain_on_channel = MFALSE;
	}

done:
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
/**
 * @brief Make chip remain on channel
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param wdev                  A pointer to wireless_dev structure
 * @param chan                  A pointer to ieee80211_channel structure
 * @param channel_type          Channel type
 * @param duration              Duration for timer
 * @param cookie                A pointer to timer cookie
 *
 * @return                  0 -- success, otherwise fail
 */
static int
woal_cfg80211_remain_on_channel(struct wiphy *wiphy,
				struct wireless_dev *wdev,
				struct ieee80211_channel *chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
				enum nl80211_channel_type channel_type,
#endif
				unsigned int duration, u64 * cookie)
#else
/**
 * @brief Make chip remain on channel
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param chan                  A pointer to ieee80211_channel structure
 * @param channel_type          Channel type
 * @param duration              Duration for timer
 * @param cookie                A pointer to timer cookie
 *
 * @return                  0 -- success, otherwise fail
 */
static int
woal_cfg80211_remain_on_channel(struct wiphy *wiphy,
				struct net_device *dev,
				struct ieee80211_channel *chan,
				enum nl80211_channel_type channel_type,
				unsigned int duration, u64 * cookie)
#endif
{
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct net_device *dev = wdev->netdev;
#endif
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;
	t_u8 status = 1;
	moal_private *remain_priv = NULL;

	ENTER();

	if (!chan || !cookie) {
		PRINTM(MERROR, "Invalid parameter for remain on channel\n");
		ret = -EFAULT;
		goto done;
	}
	/** cancel previous remain on channel */
	if (priv->phandle->remain_on_channel &&
	    ((priv->phandle->chan.center_freq != chan->center_freq)
	    )) {
		remain_priv =
			priv->phandle->priv[priv->phandle->remain_bss_index];
		if (!remain_priv) {
			PRINTM(MERROR,
			       "remain_on_channel: Wrong remain_bss_index=%d\n",
			       priv->phandle->remain_bss_index);
			ret = -EFAULT;
			goto done;
		}
		if (woal_cfg80211_remain_on_channel_cfg
		    (remain_priv, MOAL_IOCTL_WAIT, MTRUE, &status, NULL, 0,
		     0)) {
			PRINTM(MERROR,
			       "remain_on_channel: Fail to cancel remain on channel\n");
			ret = -EFAULT;
			goto done;
		}
		priv->phandle->cookie = 0;
		priv->phandle->remain_on_channel = MFALSE;
	}
	if (MLAN_STATUS_SUCCESS !=
	    woal_cfg80211_remain_on_channel_cfg(priv, MOAL_IOCTL_WAIT,
						MFALSE, &status, chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
						channel_type,
#else
						0,
#endif
						(t_u32)duration)) {
		ret = -EFAULT;
		goto done;
	}

	if (status) {
		PRINTM(MMSG,
		       "%s: Set remain on Channel: channel=%d with status=%d\n",
		       dev->name,
		       ieee80211_frequency_to_channel(chan->center_freq),
		       status);
		if (!priv->phandle->remain_on_channel) {
			priv->phandle->is_remain_timer_set = MTRUE;
			woal_mod_timer(&priv->phandle->remain_timer, duration);
		}
	}

	/* remain on channel operation success */
	/* we need update the value cookie */
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	*cookie = (u64) random32() | 1;
#else
	*cookie = (u64) prandom_u32() | 1;
#endif
	priv->phandle->remain_on_channel = MTRUE;
	priv->phandle->remain_bss_index = priv->bss_index;
	priv->phandle->cookie = *cookie;
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
	priv->phandle->channel_type = channel_type;
#endif
	memcpy(&priv->phandle->chan, chan, sizeof(struct ieee80211_channel));

	if (status == 0)
		PRINTM(MIOCTL,
		       "%s: Set remain on Channel: channel=%d cookie = %#llx\n",
		       dev->name,
		       ieee80211_frequency_to_channel(chan->center_freq),
		       priv->phandle->cookie);

	cfg80211_ready_on_channel(
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
					 dev,
#else
					 priv->wdev,
#endif
					 *cookie, chan,
#if CFG80211_VERSION_CODE < KERNEL_VERSION(3, 8, 0)
					 channel_type,
#endif
					 duration, GFP_KERNEL);

done:
	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
/**
 * @brief Cancel remain on channel
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param wdev                  A pointer to wireless_dev structure
 * @param cookie                A pointer to timer cookie
 *
 * @return                  0 -- success, otherwise fail
 */
static int
woal_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
				       struct wireless_dev *wdev, u64 cookie)
#else
/**
 * @brief Cancel remain on channel
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param cookie                A pointer to timer cookie
 *
 * @return                  0 -- success, otherwise fail
 */
static int
woal_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
				       struct net_device *dev, u64 cookie)
#endif
{
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
	struct net_device *dev = wdev->netdev;
#endif
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	moal_private *remain_priv = NULL;
	int ret = 0;
	t_u8 status = 1;

	ENTER();
	PRINTM(MIOCTL, "Cancel remain on Channel: cookie = %#llx\n", cookie);
	remain_priv = priv->phandle->priv[priv->phandle->remain_bss_index];
	if (!remain_priv) {
		PRINTM(MERROR,
		       "cancel_remain_on_channel: Wrong remain_bss_index=%d\n",
		       priv->phandle->remain_bss_index);
		ret = -EFAULT;
		goto done;
	}
	if (woal_cfg80211_remain_on_channel_cfg
	    (remain_priv, MOAL_IOCTL_WAIT, MTRUE, &status, NULL, 0, 0)) {
		PRINTM(MERROR,
		       "cancel_remain_on_channel: Fail to cancel remain on channel\n");
		ret = -EFAULT;
		goto done;
	}

	priv->phandle->remain_on_channel = MFALSE;
	if (priv->phandle->cookie)
		priv->phandle->cookie = 0;
done:
	LEAVE();
	return ret;
}
#endif /* KERNEL_VERSION */

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
/**
 * @brief start sched scan
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param request               A pointer to struct cfg80211_sched_scan_request
 *
 * @return                  0 -- success, otherwise fail
 */
int
woal_cfg80211_sched_scan_start(struct wiphy *wiphy,
			       struct net_device *dev,
			       struct cfg80211_sched_scan_request *request)
{
	struct ieee80211_channel *chan = NULL;
	int i = 0;
	int ret = 0;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	struct cfg80211_ssid *ssid = NULL;
	ENTER();
#ifdef UAP_CFG80211
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_UAP) {
		LEAVE();
		return -EFAULT;
	}
#endif

	memset(&priv->scan_cfg, 0, sizeof(priv->scan_cfg));
	if (!request) {
		PRINTM(MERROR, "Invalid sched_scan req parameter\n");
		LEAVE();
		return -EINVAL;
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	PRINTM(MIOCTL,
	       "%s sched scan: n_ssids=%d n_match_sets=%d n_channels=%d interval=%d ie_len=%d\n",
	       priv->netdev->name, request->n_ssids, request->n_match_sets,
	       request->n_channels, request->scan_plans[0].interval,
	       (int)request->ie_len);
#else
	PRINTM(MIOCTL,
	       "%s sched scan: n_ssids=%d n_match_sets=%d n_channels=%d interval=%d ie_len=%d\n",
	       priv->netdev->name, request->n_ssids, request->n_match_sets,
	       request->n_channels, request->interval, (int)request->ie_len);
#endif
    /** We have pending scan, start bgscan later */
	if (priv->phandle->scan_pending_on_block)
		priv->scan_cfg.start_later = MTRUE;
	for (i = 0; i < request->n_match_sets; i++) {
		ssid = &request->match_sets[i].ssid;
		strncpy(priv->scan_cfg.ssid_list[i].ssid, ssid->ssid,
			ssid->ssid_len);
		priv->scan_cfg.ssid_list[i].max_len = 0;
		PRINTM(MIOCTL, "sched scan: ssid=%s\n", ssid->ssid);
	}
	/** Add broadcast scan, when n_match_sets = 0 */
	if (!request->n_match_sets)
		priv->scan_cfg.ssid_list[0].max_len = 0xff;
	for (i = 0; i < MIN(WLAN_BG_SCAN_CHAN_MAX, request->n_channels); i++) {
		chan = request->channels[i];
		priv->scan_cfg.chan_list[i].chan_number = chan->hw_value;
		priv->scan_cfg.chan_list[i].radio_type = chan->band;
		if (chan->
		    flags & (IEEE80211_CHAN_PASSIVE_SCAN |
			     IEEE80211_CHAN_RADAR))
			priv->scan_cfg.chan_list[i].scan_type =
				MLAN_SCAN_TYPE_PASSIVE;
		else
			priv->scan_cfg.chan_list[i].scan_type =
				MLAN_SCAN_TYPE_ACTIVE;
		priv->scan_cfg.chan_list[i].scan_time = 0;
#ifdef WIFI_DIRECT_SUPPORT
		if (priv->phandle->miracast_mode)
			priv->scan_cfg.chan_list[i].scan_time =
				priv->phandle->miracast_scan_time;
#endif
	}
	priv->scan_cfg.chan_per_scan =
		MIN(WLAN_BG_SCAN_CHAN_MAX, request->n_channels);

	/** set scan request IES */
	if (request->ie && request->ie_len) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_cfg80211_mgmt_frame_ie(priv, NULL, 0,
						NULL, 0, NULL, 0,
						(t_u8 *)request->ie,
						request->ie_len,
						MGMT_MASK_PROBE_REQ,
						MOAL_IOCTL_WAIT)) {
			PRINTM(MERROR, "Fail to set sched scan IE\n");
			ret = -EFAULT;
			goto done;
		}
	} else {
		/** Clear SCAN IE in Firmware */
		if (priv->probereq_index != MLAN_CUSTOM_IE_AUTO_IDX_MASK)
			woal_cfg80211_mgmt_frame_ie(priv, NULL, 0, NULL, 0,
						    NULL, 0, NULL, 0,
						    MGMT_MASK_PROBE_REQ,
						    MOAL_IOCTL_WAIT);
	}

	/* Interval between scan cycles in milliseconds,supplicant set to 10 second */
	/* We want to use 30 second for per scan cycle */
	priv->scan_cfg.scan_interval = MIN_BGSCAN_INTERVAL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	/* only support 1 scan plan now */
	if (request->scan_plans[0].interval > MIN_BGSCAN_INTERVAL)
		priv->scan_cfg.scan_interval = request->scan_plans[0].interval;
#else
	if (request->interval > MIN_BGSCAN_INTERVAL)
		priv->scan_cfg.scan_interval = request->interval;
#endif
	priv->scan_cfg.repeat_count = DEF_REPEAT_COUNT;
	priv->scan_cfg.report_condition =
		BG_SCAN_SSID_MATCH | BG_SCAN_WAIT_ALL_CHAN_DONE;
	priv->scan_cfg.bss_type = MLAN_BSS_MODE_INFRA;
	priv->scan_cfg.action = BG_SCAN_ACT_SET;
	priv->scan_cfg.enable = MTRUE;
#ifdef WIFI_DIRECT_SUPPORT
	if (priv->phandle->miracast_mode)
		priv->scan_cfg.scan_chan_gap = priv->phandle->scan_chan_gap;
	else
		priv->scan_cfg.scan_chan_gap = 0;
#endif

	if (MLAN_STATUS_SUCCESS ==
	    woal_request_bgscan(priv, MOAL_IOCTL_WAIT, &priv->scan_cfg)) {
		priv->sched_scanning = MTRUE;
		priv->bg_scan_start = MTRUE;
		priv->bg_scan_reported = MFALSE;
	} else
		ret = -EFAULT;
done:
	LEAVE();
	return ret;
}

/**
 * @brief stop sched scan
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 *
 * @return                      0 -- success, otherwise fail
 */
int
woal_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
			      , u64 reqid
#endif
	)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	ENTER();
	PRINTM(MIOCTL, "sched scan stop\n");
	priv->sched_scanning = MFALSE;
	woal_stop_bg_scan(priv, MOAL_NO_WAIT);
	priv->bg_scan_start = MFALSE;
	priv->bg_scan_reported = MFALSE;
	LEAVE();
	return 0;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
/**
 * @brief cfg80211_resume handler
 *
 * @param wiphy                 A pointer to wiphy structure
 *
 * @return                      0 -- success, otherwise fail
 */
int
woal_cfg80211_resume(struct wiphy *wiphy)
{
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	moal_private *priv = woal_get_priv(handle, MLAN_BSS_ROLE_STA);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) && defined(CONFIG_PM)
	struct cfg80211_wowlan_wakeup wakeup_report;
#endif
	mlan_ds_hs_wakeup_reason wakeup_reason;
	int i;

	PRINTM(MCMND, "<--- Enter woal_cfg80211_resume --->\n");
	for (i = 0; i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM); i++) {
		if (handle->priv[i] &&
		    (GET_BSS_ROLE(handle->priv[i]) == MLAN_BSS_ROLE_STA)) {
			if (handle->priv[i]->last_event & EVENT_BG_SCAN_REPORT) {
				if (handle->priv[i]->sched_scanning) {
					woal_inform_bss_from_scan_result
						(handle->priv[i], NULL,
						 MOAL_IOCTL_WAIT);
					cfg80211_sched_scan_results(handle->
								    priv[i]->
								    wdev->wiphy
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
								    , 0
#endif
						);
					handle->priv[i]->last_event = 0;
					PRINTM(MIOCTL,
					       "Report sched scan result in cfg80211 resume\n");
				}
				if (!hw_test &&
				    handle->priv[i]->roaming_enabled) {
					handle->priv[i]->roaming_required =
						MTRUE;
#ifdef ANDROID_KERNEL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
					__pm_wakeup_event(&handle->ws,
							  ROAMING_WAKE_LOCK_TIMEOUT);
#else
					wake_lock_timeout(&handle->wake_lock,
							  ROAMING_WAKE_LOCK_TIMEOUT);
#endif
#endif
					wake_up_interruptible(&handle->
							      reassoc_thread.
							      wait_q);
				}
			}
		}
	}

	woal_get_wakeup_reason(priv, &wakeup_reason);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 11, 0) && defined(CONFIG_PM)
	memset(&wakeup_report, 0, sizeof(struct cfg80211_wowlan_wakeup));
	wakeup_report.pattern_idx = -1;

	switch (wakeup_reason.hs_wakeup_reason) {
	case NO_HSWAKEUP_REASON:
		break;
	case BCAST_DATA_MATCHED:
		break;
	case MCAST_DATA_MATCHED:
		break;
	case UCAST_DATA_MATCHED:
		break;
	case MASKTABLE_EVENT_MATCHED:
		break;
	case NON_MASKABLE_EVENT_MATCHED:
		break;
	case NON_MASKABLE_CONDITION_MATCHED:
		if (wiphy->wowlan_config->disconnect)
			wakeup_report.disconnect = true;
		break;
	case MAGIC_PATTERN_MATCHED:
		if (wiphy->wowlan_config->magic_pkt)
			wakeup_report.magic_pkt = true;
		if (wiphy->wowlan_config->n_patterns)
			wakeup_report.pattern_idx = 1;
		break;
	case CONTROL_FRAME_MATCHED:
		break;
	case MANAGEMENT_FRAME_MATCHED:
		break;
	case GTK_REKEY_FAILURE:
		if (wiphy->wowlan_config->gtk_rekey_failure)
			wakeup_report.gtk_rekey_failure = true;
		break;
	default:
		break;
	}

	if ((wakeup_reason.hs_wakeup_reason > 0) &&
	    (wakeup_reason.hs_wakeup_reason <= 10)) {
		cfg80211_report_wowlan_wakeup(priv->wdev, &wakeup_report,
					      GFP_KERNEL);
	}
#endif

	handle->cfg80211_suspend = MFALSE;
	PRINTM(MCMND, "<--- Leave woal_cfg80211_resume --->\n");
	return 0;
}

/**
 * @brief cfg80211_suspend handler
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param wow                   A pointer to cfg80211_wowlan
 *
 * @return                      0 -- success, otherwise fail
 */
int
woal_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
{
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);
	int i;
	int ret = 0;

	PRINTM(MCMND, "<--- Enter woal_cfg80211_suspend --->\n");
	for (i = 0; i < MIN(handle->priv_num, MLAN_MAX_BSS_NUM); i++) {
		if (handle->priv[i] &&
		    (GET_BSS_ROLE(handle->priv[i]) == MLAN_BSS_ROLE_STA)) {
			if (handle->scan_request) {
				PRINTM(MIOCTL,
				       "Cancel pending scan in woal_cfg80211_suspend\n");
				woal_cancel_scan(handle->priv[i],
						 MOAL_IOCTL_WAIT);
			}
			handle->priv[i]->last_event = 0;
		}
	}

	handle->cfg80211_suspend = MTRUE;

	PRINTM(MCMND, "<--- Leave woal_cfg80211_suspend --->\n");
	return ret;
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
static void
woal_cfg80211_set_wakeup(struct wiphy *wiphy, bool enabled)
{
	moal_handle *handle = (moal_handle *)woal_get_wiphy_priv(wiphy);

	device_set_wakeup_enable(handle->hotplug_device, enabled);
}
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,2,0)
/**
 *  @brief TDLS operation ioctl handler
 *
 *  @param priv     A pointer to moal_private structure
 *  @param peer     A pointer to peer mac
 *  @apram action   action for TDLS
 *  @return         0 --success, otherwise fail
 */
static int
woal_tdls_oper(moal_private *priv, u8 *peer, t_u8 action)
{
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)ioctl_req->pbuf;
	misc->sub_command = MLAN_OID_MISC_TDLS_OPER;
	ioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	ioctl_req->action = MLAN_ACT_SET;
	misc->param.tdls_oper.tdls_action = action;
	memcpy(misc->param.tdls_oper.peer_mac, peer, ETH_ALEN);
	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

/**
 *  @brief TDLS operation ioctl handler
 *
 *  @param priv         A pointer to moal_private structure
 *  @param peer         A pointer to peer mac
 *  @param tdls_ies     A pointer to mlan_ds_misc_tdls_ies structure
 *  @param flags        TDLS ie flags
 *
 *  @return         0 --success, otherwise fail
 */
static int
woal_tdls_get_ies(moal_private *priv, u8 *peer, mlan_ds_misc_tdls_ies *tdls_ies,
		  t_u16 flags)
{
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)ioctl_req->pbuf;
	misc->sub_command = MLAN_OID_MISC_GET_TDLS_IES;
	ioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	ioctl_req->action = MLAN_ACT_GET;
	misc->param.tdls_ies.flags = flags;
	memcpy(misc->param.tdls_ies.peer_mac, peer, ETH_ALEN);
	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}
	if (tdls_ies)
		memcpy(tdls_ies, &misc->param.tdls_ies,
		       sizeof(mlan_ds_misc_tdls_ies));
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

/**
 * @brief append tdls ext_capability
 *
 * @param skb                   A pointer to sk_buff structure
 *
 * @return                      N/A
 */
static void
woal_tdls_add_ext_capab(struct sk_buff *skb, mlan_ds_misc_tdls_ies *tdls_ies)
{
	u8 *pos = NULL;
	if (tdls_ies->ext_cap[0] == WLAN_EID_EXT_CAPABILITY) {
		pos = (void *)skb_put(skb, sizeof(IEEEtypes_ExtCap_t));
		memcpy(pos, tdls_ies->ext_cap, sizeof(IEEEtypes_ExtCap_t));
	} else {
		PRINTM(MERROR, "Fail to append tdls ext_capability\n");
	}
}

/**
 * @brief append supported rates
 *
 * @param priv                  A pointer to moal_private structure
 * @param skb                   A pointer to sk_buff structure
 * @param band                  AP's band
 *
 * @return                      N/A
 */
static void
woal_add_supported_rates_ie(moal_private *priv, struct sk_buff *skb,
			    enum ieee80211_band band)
{
	t_u8 basic_rates[] = {
		0x82, 0x84, 0x8b, 0x96,
		0x0c, 0x12, 0x18, 0x24
	};
	t_u8 basic_rates_5G[] = {
		0x0c, 0x12, 0x18, 0x24,
		0x30, 0x48, 0x60, 0x6c
	};
	t_u8 *pos;
	t_u8 rate_num = 0;
	if (band == IEEE80211_BAND_2GHZ)
		rate_num = sizeof(basic_rates);
	else
		rate_num = sizeof(basic_rates_5G);

	if (skb_tailroom(skb) < rate_num + 2)
		return;

	pos = skb_put(skb, rate_num + 2);
	*pos++ = WLAN_EID_SUPP_RATES;
	*pos++ = rate_num;
	if (band == IEEE80211_BAND_2GHZ)
		memcpy(pos, basic_rates, rate_num);
	else
		memcpy(pos, basic_rates_5G, rate_num);
	return;
}

/**
 * @brief append ext_supported rates
 *
 * @param priv                  A pointer to moal_private structure
 * @param skb                   A pointer to sk_buff structure
 * @param band                  AP's band
 *
 * @return                      N/A
 */
static void
woal_add_ext_supported_rates_ie(moal_private *priv, struct sk_buff *skb,
				enum ieee80211_band band)
{
	t_u8 ext_rates[] = { 0x0c, 0x12, 0x18, 0x60 };
	t_u8 *pos;
	t_u8 rate_num = sizeof(ext_rates);

	if (band != IEEE80211_BAND_2GHZ)
		return;

	if (skb_tailroom(skb) < rate_num + 2)
		return;

	pos = skb_put(skb, rate_num + 2);
	*pos++ = WLAN_EID_EXT_SUPP_RATES;
	*pos++ = rate_num;
	memcpy(pos, ext_rates, rate_num);
	return;
}

/**
 * @brief append wmm ie
 *
 * @param priv                  A pointer to moal_private structure
 * @param skb                   A pointer to sk_buff structure
 * @param wmm_type         WMM_TYPE_INFO/WMM_TYPE_PARAMETER
  * @param pQosInfo           A pointer to qos info
 *
 * @return                      N/A
 */
static void
woal_add_wmm_ie(moal_private *priv, struct sk_buff *skb, t_u8 wmm_type,
		t_u8 *pQosInfo)
{
	t_u8 wmmInfoElement[] = { 0x00, 0x50, 0xf2,
		0x02, 0x00, 0x01
	};
	t_u8 wmmParamElement[] = { 0x00, 0x50, 0xf2,
		0x02, 0x01, 0x01
	};
	t_u8 ac_vi[] = { 0x42, 0x43, 0x5e, 0x00 };
	t_u8 ac_vo[] = { 0x62, 0x32, 0x2f, 0x00 };
	t_u8 ac_be[] = { 0x03, 0xa4, 0x00, 0x00 };
	t_u8 ac_bk[] = { 0x27, 0xa4, 0x00, 0x00 };
	t_u8 qosInfo = 0x0;
	t_u8 reserved = 0;
	t_u8 wmm_id = 221;
	t_u8 wmmParamIe_len = 24;
	t_u8 wmmInfoIe_len = 7;
	t_u8 len = 0;
	t_u8 *pos;

	if (skb_tailroom(skb) < wmmParamIe_len + 2)
		return;

	qosInfo = (pQosInfo == NULL) ? 0xf : (*pQosInfo);
	/*wmm parameter */
	if (wmm_type == WMM_TYPE_PARAMETER) {
		pos = skb_put(skb, wmmParamIe_len + 2);
		len = wmmParamIe_len;
	} else {
		pos = skb_put(skb, wmmInfoIe_len + 2);
		len = wmmInfoIe_len;
	}

	*pos++ = wmm_id;
	*pos++ = len;
	/*wmm parameter */
	if (wmm_type == WMM_TYPE_PARAMETER) {
		memcpy(pos, wmmParamElement, sizeof(wmmParamElement));
		pos += sizeof(wmmParamElement);
	} else {
		memcpy(pos, wmmInfoElement, sizeof(wmmInfoElement));
		pos += sizeof(wmmInfoElement);
	}
	*pos++ = qosInfo;
	/*wmm parameter */
	if (wmm_type == WMM_TYPE_PARAMETER) {
		*pos++ = reserved;
		memcpy(pos, ac_be, sizeof(ac_be));
		pos += sizeof(ac_be);
		memcpy(pos, ac_bk, sizeof(ac_bk));
		pos += sizeof(ac_bk);
		memcpy(pos, ac_vi, sizeof(ac_vi));
		pos += sizeof(ac_vi);
		memcpy(pos, ac_vo, sizeof(ac_vo));
	}
	return;
}

/**
 * @brief update tdls peer status
 *
 * @param priv                  A pointer to moal_private structure
 * @param peer_addr             A point to peer mac address
 * @param link_status           link status
 *
 * @return                      N/A
*/
t_void
woal_updata_peer_status(moal_private *priv, t_u8 *peer_addr,
			tdlsStatus_e link_status)
{
	struct tdls_peer *peer = NULL;
	unsigned long flags;
	if (priv && priv->enable_auto_tdls) {
		spin_lock_irqsave(&priv->tdls_lock, flags);
		list_for_each_entry(peer, &priv->tdls_list, link) {
			if (!memcmp(peer->peer_addr, peer_addr, ETH_ALEN)) {
				if ((link_status == TDLS_NOT_SETUP) &&
				    (peer->link_status ==
				     TDLS_SETUP_INPROGRESS))
					peer->num_failure++;
				else if (link_status == TDLS_SETUP_COMPLETE)
					peer->num_failure = 0;
				peer->link_status = link_status;
				break;
			}
		}
		spin_unlock_irqrestore(&priv->tdls_lock, flags);
	}
}

/**
 * @brief add tdls peer
 *
 * @param priv                  A pointer to moal_private structure
 * @param peer                  A point to peer address
 *
 * @return                      N/A
*/
t_void
woal_add_tdls_peer(moal_private *priv, t_u8 *peer)
{
	struct tdls_peer *tdls_peer = NULL;
	unsigned long flags;
	t_u8 find_peer = MFALSE;
	if (priv && priv->enable_auto_tdls) {
		spin_lock_irqsave(&priv->tdls_lock, flags);
		list_for_each_entry(tdls_peer, &priv->tdls_list, link) {
			if (!memcmp(tdls_peer->peer_addr, peer, ETH_ALEN)) {
				tdls_peer->link_status = TDLS_SETUP_INPROGRESS;
				tdls_peer->rssi_jiffies = jiffies;
				find_peer = MTRUE;
				break;
			}
		}
		if (!find_peer) {
			/* create new TDLS peer */
			tdls_peer =
				kzalloc(sizeof(struct tdls_peer), GFP_ATOMIC);
			if (tdls_peer) {
				memcpy(tdls_peer->peer_addr, peer, ETH_ALEN);
				tdls_peer->link_status = TDLS_SETUP_INPROGRESS;
				tdls_peer->rssi_jiffies = jiffies;
				INIT_LIST_HEAD(&tdls_peer->link);
				list_add_tail(&tdls_peer->link,
					      &priv->tdls_list);
				PRINTM(MCMND,
				       "Add to TDLS list: peer=" MACSTR "\n",
				       MAC2STR(peer));
			}
		}
		spin_unlock_irqrestore(&priv->tdls_lock, flags);
	}
}

/**
 * @brief check auto tdls
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 *
 * @return                      N/A
*/
void
woal_check_auto_tdls(struct wiphy *wiphy, struct net_device *dev)
{
	t_u8 bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct tdls_peer *tdls_peer = NULL;
	unsigned long flags;
	t_u8 tdls_discovery = MFALSE;
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);

	ENTER();
	if (priv && priv->enable_auto_tdls) {
		priv->tdls_check_tx = MFALSE;
		spin_lock_irqsave(&priv->tdls_lock, flags);
		list_for_each_entry(tdls_peer, &priv->tdls_list, link) {
			if ((jiffies - tdls_peer->rssi_jiffies) >
			    TDLS_IDLE_TIME) {
				tdls_peer->rssi = 0;
				if (tdls_peer->num_failure <
				    TDLS_MAX_FAILURE_COUNT)
					tdls_discovery = MTRUE;
			}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
			if (tdls_peer->rssi &&
			    (tdls_peer->rssi >= TDLS_RSSI_LOW_THRESHOLD)) {
				if (tdls_peer->link_status ==
				    TDLS_SETUP_COMPLETE) {
					tdls_peer->link_status = TDLS_TEAR_DOWN;
					PRINTM(MMSG,
					       "Wlan: Tear down TDLS link, peer="
					       MACSTR " rssi=%d\n",
					       MAC2STR(tdls_peer->peer_addr),
					       -tdls_peer->rssi);
					cfg80211_tdls_oper_request(dev,
								   tdls_peer->
								   peer_addr,
								   NL80211_TDLS_TEARDOWN,
								   TDLS_TEARN_DOWN_REASON_UNSPECIFIC,
								   GFP_ATOMIC);
				}
			} else if (tdls_peer->rssi &&
				   (tdls_peer->rssi <=
				    TDLS_RSSI_HIGH_THRESHOLD)) {
				if ((tdls_peer->link_status == TDLS_NOT_SETUP)
				    && (tdls_peer->num_failure <
					TDLS_MAX_FAILURE_COUNT)) {
					priv->tdls_check_tx = MTRUE;
					PRINTM(MCMND,
					       "Wlan: Find TDLS peer=" MACSTR
					       " rssi=%d\n",
					       MAC2STR(tdls_peer->peer_addr),
					       -tdls_peer->rssi);

				}
			}
#endif
		}
		spin_unlock_irqrestore(&priv->tdls_lock, flags);
	}
	if (tdls_discovery)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
		woal_cfg80211_tdls_mgmt(wiphy, dev, bcast_addr,
					TDLS_DISCOVERY_REQUEST, 1, 0, 0, 0,
					NULL, 0);
#else
		woal_cfg80211_tdls_mgmt(wiphy, dev, bcast_addr,
					TDLS_DISCOVERY_REQUEST, 1, 0, 0, NULL,
					0);
#endif
#else
		woal_cfg80211_tdls_mgmt(wiphy, dev, bcast_addr,
					TDLS_DISCOVERY_REQUEST, 1, 0, NULL, 0);
#endif
	LEAVE();
}

/**
 * @brief woal construct tdls data frame
 *
 * @param priv                  A pointer to moal_private structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param skb                   skb buffer
 *
 * @return                      0 -- success, otherwise fail
 */
static int
woal_construct_tdls_data_frame(moal_private *priv,
			       t_u8 *peer, t_u8 action_code, t_u8 dialog_token,
			       t_u16 status_code, struct sk_buff *skb)
{

	struct ieee80211_tdls_data *tf;
	t_u16 capability;
	IEEEtypes_HTCap_t *HTcap;
	IEEEtypes_HTInfo_t *HTInfo;
	IEEEtypes_2040BSSCo_t *BSSCo;
	IEEEtypes_Generic_t *pSupp_chan = NULL, *pRegulatory_class = NULL;
	mlan_ds_misc_tdls_ies *tdls_ies = NULL;
	int ret = 0;
	mlan_bss_info bss_info;
	enum ieee80211_band band;
	mlan_fw_info fw_info;

	ENTER();

	memset(&bss_info, 0, sizeof(bss_info));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info)) {
		PRINTM(MERROR, "Fail to get bss info\n");
		LEAVE();
		return -EFAULT;
	}
	band = woal_band_cfg_to_ieee_band(bss_info.bss_band);
	tdls_ies = kzalloc(sizeof(mlan_ds_misc_tdls_ies), GFP_KERNEL);
	if (!tdls_ies) {
		PRINTM(MERROR, "Fail to alloc memory for tdls_ies\n");
		LEAVE();
		return -ENOMEM;
	}

	capability = 0x2421;

	tf = (void *)skb_put(skb, offsetof(struct ieee80211_tdls_data, u));
	memcpy(tf->da, peer, ETH_ALEN);
	memcpy(tf->sa, priv->current_addr, ETH_ALEN);
	tf->ether_type = cpu_to_be16(MLAN_ETHER_PKT_TYPE_TDLS_ACTION);
	tf->payload_type = WLAN_TDLS_SNAP_RFTYPE;
	woal_request_get_fw_info(priv, MOAL_IOCTL_WAIT, &fw_info);

	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
		woal_tdls_get_ies(priv, peer, tdls_ies,
				  TDLS_IE_FLAGS_SETUP | TDLS_IE_FLAGS_EXTCAP |
				  TDLS_IE_FLAGS_HTCAP |
				  TDLS_IE_FLAGS_SUPP_CS_IE);

		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_REQUEST;
		skb_put(skb, sizeof(tf->u.setup_req));
		tf->u.setup_req.dialog_token = dialog_token;
		tf->u.setup_req.capability = cpu_to_le16(capability);
		woal_add_supported_rates_ie(priv, skb, band);
		woal_add_ext_supported_rates_ie(priv, skb, band);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
		woal_tdls_get_ies(priv, peer, tdls_ies,
				  TDLS_IE_FLAGS_EXTCAP | TDLS_IE_FLAGS_HTCAP |
				  TDLS_IE_FLAGS_SUPP_CS_IE);

		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_RESPONSE;

		skb_put(skb, sizeof(tf->u.setup_resp));
		tf->u.setup_resp.status_code = cpu_to_le16(status_code);
		tf->u.setup_resp.dialog_token = dialog_token;
		tf->u.setup_resp.capability = cpu_to_le16(capability);

		woal_add_supported_rates_ie(priv, skb, band);
		woal_add_ext_supported_rates_ie(priv, skb, band);
		break;
	case WLAN_TDLS_SETUP_CONFIRM:
		woal_tdls_get_ies(priv, peer, tdls_ies,
				  TDLS_IE_FLAGS_EXTCAP | TDLS_IE_FLAGS_HTINFO |
				  TDLS_IE_FLAGS_QOS_INFO);

		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_SETUP_CONFIRM;

		skb_put(skb, sizeof(tf->u.setup_cfm));
		tf->u.setup_cfm.status_code = cpu_to_le16(status_code);
		tf->u.setup_cfm.dialog_token = dialog_token;

		break;
	case WLAN_TDLS_TEARDOWN:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_TEARDOWN;

		skb_put(skb, sizeof(tf->u.teardown));
		tf->u.teardown.reason_code = cpu_to_le16(status_code);
		break;
	case WLAN_TDLS_DISCOVERY_REQUEST:
		tf->category = WLAN_CATEGORY_TDLS;
		tf->action_code = WLAN_TDLS_DISCOVERY_REQUEST;

		skb_put(skb, sizeof(tf->u.discover_req));
		tf->u.discover_req.dialog_token = dialog_token;
		break;
	default:
		ret = -EINVAL;
		goto done;
	}

	if (action_code == WLAN_TDLS_SETUP_REQUEST ||
	    action_code == WLAN_TDLS_SETUP_RESPONSE) {
		/* supported chanel ie */
		if (tdls_ies->supp_chan[0] == SUPPORTED_CHANNELS) {
			pSupp_chan =
				(void *)skb_put(skb,
						sizeof(IEEEtypes_Header_t) +
						tdls_ies->supp_chan[1]);
			memset(pSupp_chan, 0,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->supp_chan[1]);
			memcpy(pSupp_chan, tdls_ies->supp_chan,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->supp_chan[1]);
		}
		/* supported regulatory class ie */
		if (tdls_ies->regulatory_class[0] == REGULATORY_CLASS) {
			pRegulatory_class =
				(void *)skb_put(skb,
						sizeof(IEEEtypes_Header_t) +
						tdls_ies->regulatory_class[1]);
			memset(pRegulatory_class, 0,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->regulatory_class[1]);
			memcpy(pRegulatory_class, tdls_ies->regulatory_class,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->regulatory_class[1]);
		}
		woal_tdls_add_ext_capab(skb, tdls_ies);
	}

	/* TODO we should fill in ht_cap and htinfo with correct value */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
		/*HT capability */
		if (tdls_ies->ht_cap[0] == HT_CAPABILITY) {
			HTcap = (void *)skb_put(skb, sizeof(IEEEtypes_HTCap_t));
			memset(HTcap, 0, sizeof(IEEEtypes_HTCap_t));
			memcpy(HTcap, tdls_ies->ht_cap,
			       sizeof(IEEEtypes_HTCap_t));
		} else {
			PRINTM(MIOCTL, "No TDLS HT capability\n");
		}

		/*20_40_bss_coexist */
		BSSCo = (void *)skb_put(skb, sizeof(IEEEtypes_2040BSSCo_t));
		memset(BSSCo, 0, sizeof(IEEEtypes_2040BSSCo_t));
		BSSCo->ieee_hdr.element_id = BSSCO_2040;
		BSSCo->ieee_hdr.len =
			sizeof(IEEEtypes_2040BSSCo_t) -
			sizeof(IEEEtypes_Header_t);
		BSSCo->bss_co_2040.bss_co_2040_value = 0x01;

		break;
	case WLAN_TDLS_SETUP_CONFIRM:
		/*HT information */
		if (tdls_ies->ht_info[0] == HT_OPERATION) {
			HTInfo = (void *)skb_put(skb,
						 sizeof(IEEEtypes_HTInfo_t));
			memset(HTInfo, 0, sizeof(IEEEtypes_HTInfo_t));
			memcpy(HTInfo, tdls_ies->ht_info,
			       sizeof(IEEEtypes_HTInfo_t));
		} else
			PRINTM(MIOCTL, "No TDLS HT information\n");
		break;
	default:
		break;
	}

	if (action_code == WLAN_TDLS_SETUP_REQUEST ||
	    action_code == WLAN_TDLS_SETUP_RESPONSE) {
		/*wmm info */
		woal_add_wmm_ie(priv, skb, WMM_TYPE_INFO, NULL);
	} else if (action_code == WLAN_TDLS_SETUP_CONFIRM) {
		/*wmm parameter */
		woal_add_wmm_ie(priv, skb, WMM_TYPE_PARAMETER,
				&tdls_ies->QosInfo);
	}

done:
	kfree(tdls_ies);
	return ret;
}

/**
 * @brief woal construct tdls action frame
 *
 * @param priv                  A pointer to moal_private structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param skb                   skb buffer
 *
 * @return                      0 -- success, otherwise fail
 */
static int
woal_construct_tdls_action_frame(moal_private *priv,
				 t_u8 *peer, t_u8 action_code,
				 t_u8 dialog_token, t_u16 status_code,
				 struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt;
	t_u8 addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	t_u16 capability;
	t_u8 *pos = NULL;
	mlan_ds_misc_tdls_ies *tdls_ies = NULL;
	mlan_bss_info bss_info;
	enum ieee80211_band band;
	IEEEtypes_Generic_t *pSupp_chan = NULL, *pRegulatory_class = NULL;

	int ret = 0;

	ENTER();

	memset(&bss_info, 0, sizeof(bss_info));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info)) {
		PRINTM(MERROR, "Fail to get bss info\n");
		LEAVE();
		return -EFAULT;
	}
	band = woal_band_cfg_to_ieee_band(bss_info.bss_band);

	tdls_ies = kzalloc(sizeof(mlan_ds_misc_tdls_ies), GFP_KERNEL);
	if (!tdls_ies) {
		PRINTM(MERROR, "Fail to alloc memory for tdls_ies\n");
		LEAVE();
		return -ENOMEM;
	}

	mgmt = (void *)skb_put(skb, 24);
	memset(mgmt, 0, 24);
	memcpy(mgmt->da, peer, ETH_ALEN);
	memcpy(mgmt->sa, priv->current_addr, ETH_ALEN);
	memcpy(mgmt->bssid, priv->cfg_bssid, ETH_ALEN);

	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_ACTION);
	/* add address 4 */
	pos = skb_put(skb, ETH_ALEN);

	capability = 0x2421;

	switch (action_code) {
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		woal_tdls_get_ies(priv, peer, tdls_ies,
				  TDLS_IE_FLAGS_EXTCAP |
				  TDLS_IE_FLAGS_SUPP_CS_IE);
		skb_put(skb, 1 + sizeof(mgmt->u.action.u.tdls_discover_resp));
		mgmt->u.action.category = WLAN_CATEGORY_PUBLIC;
		mgmt->u.action.u.tdls_discover_resp.action_code =
			WLAN_PUB_ACTION_TDLS_DISCOVER_RES;
		mgmt->u.action.u.tdls_discover_resp.dialog_token = dialog_token;
		mgmt->u.action.u.tdls_discover_resp.capability =
			cpu_to_le16(capability);
		/* move back for addr4 */
		memmove(pos + ETH_ALEN, &mgmt->u.action.category,
			1 + sizeof(mgmt->u.action.u.tdls_discover_resp));
		/** init address 4 */
		memcpy(pos, addr, ETH_ALEN);

		woal_add_supported_rates_ie(priv, skb, band);
		woal_add_ext_supported_rates_ie(priv, skb, band);
		woal_tdls_add_ext_capab(skb, tdls_ies);
		/* supported chanel ie */
		if (tdls_ies->supp_chan[0] == SUPPORTED_CHANNELS) {
			pSupp_chan =
				(void *)skb_put(skb,
						sizeof(IEEEtypes_Header_t) +
						tdls_ies->supp_chan[1]);
			memset(pSupp_chan, 0,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->supp_chan[1]);
			memcpy(pSupp_chan, tdls_ies->supp_chan,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->supp_chan[1]);
		}
		/* supported regulatory class ie */
		if (tdls_ies->regulatory_class[0] == REGULATORY_CLASS) {
			pRegulatory_class =
				(void *)skb_put(skb,
						sizeof(IEEEtypes_Header_t) +
						tdls_ies->regulatory_class[1]);
			memset(pRegulatory_class, 0,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->regulatory_class[1]);
			memcpy(pRegulatory_class, tdls_ies->regulatory_class,
			       sizeof(IEEEtypes_Header_t) +
			       tdls_ies->regulatory_class[1]);
		}

		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (tdls_ies)
		kfree(tdls_ies);
	return ret;
}

/**
 * @brief woal add tdls link identifier ie
 *
 * @param skb                   skb buffer
 * @param src_addr              source address
 * @param peer                  peer address
 * @param bssid                 AP's bssid
 *
 * @return                      NA
 */
static void
woal_tdls_add_link_ie(struct sk_buff *skb, u8 *src_addr, u8 *peer, u8 *bssid)
{
	struct ieee80211_tdls_lnkie *lnkid;

	lnkid = (void *)skb_put(skb, sizeof(struct ieee80211_tdls_lnkie));

	lnkid->ie_type = WLAN_EID_LINK_ID;
	lnkid->ie_len = sizeof(struct ieee80211_tdls_lnkie) - 2;

	memcpy(lnkid->bssid, bssid, ETH_ALEN);
	memcpy(lnkid->init_sta, src_addr, ETH_ALEN);
	memcpy(lnkid->resp_sta, peer, ETH_ALEN);
}

/**
 * @brief woal send tdls action frame
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param extra_ies              A pointer to extra ie buffer
 * @param extra_ies_len          etra ie len
 * @param skb                   skb buffer
 *
 * @return                      0 -- success, otherwise fail
 */
static int
woal_send_tdls_action_frame(struct wiphy *wiphy, struct net_device *dev,
			    t_u8 *peer, u8 action_code, t_u8 dialog_token,
			    t_u16 status_code, const t_u8 *extra_ies,
			    size_t extra_ies_len)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	pmlan_buffer pmbuf = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	struct sk_buff *skb = NULL;
	t_u8 *pos;
	t_u32 pkt_type;
	t_u32 tx_control;
	t_u16 pkt_len;
	int ret = 0;

	ENTER();

#define HEADER_SIZE				8	/* pkt_type + tx_control */

	pmbuf = woal_alloc_mlan_buffer(priv->phandle, MLAN_MIN_DATA_HEADER_LEN + HEADER_SIZE + sizeof(pkt_len) + max(sizeof(struct ieee80211_mgmt), sizeof(struct ieee80211_tdls_data)) + 50 +	/* supported rates */
				       sizeof(IEEEtypes_ExtCap_t) +	/* ext capab */
				       extra_ies_len +
				       sizeof(IEEEtypes_tdls_linkie));
	if (!pmbuf) {
		PRINTM(MERROR, "Fail to allocate mlan_buffer\n");
		ret = -ENOMEM;
		goto done;
	}

	skb = (struct sk_buff *)pmbuf->pdesc;

	skb_put(skb, MLAN_MIN_DATA_HEADER_LEN);

	pos = skb_put(skb, HEADER_SIZE + sizeof(pkt_len));
	pkt_type = MRVL_PKT_TYPE_MGMT_FRAME;
	tx_control = 0;
	memset(pos, 0, HEADER_SIZE + sizeof(pkt_len));
	memcpy(pos, &pkt_type, sizeof(pkt_type));
	memcpy(pos + sizeof(pkt_type), &tx_control, sizeof(tx_control));

	woal_construct_tdls_action_frame(priv, peer, action_code,
					 dialog_token, status_code, skb);

	if (extra_ies_len)
		memcpy(skb_put(skb, extra_ies_len), extra_ies, extra_ies_len);

	/* the TDLS link IE is always added last */
	/* we are the responder */
	woal_tdls_add_link_ie(skb, peer, priv->current_addr, priv->cfg_bssid);

	/*
	 * According to 802.11z: Setup req/resp are sent in AC_BK, otherwise
	 * we should default to AC_VI.
	 */
	skb_set_queue_mapping(skb, WMM_AC_VI);
	skb->priority = 5;

	pmbuf->data_offset = MLAN_MIN_DATA_HEADER_LEN;
	pmbuf->data_len = skb->len - pmbuf->data_offset;
	pmbuf->priority = skb->priority;
	pmbuf->buf_type = MLAN_BUF_TYPE_RAW_DATA;
	pmbuf->bss_index = priv->bss_index;

	pkt_len = pmbuf->data_len - HEADER_SIZE - sizeof(pkt_len);
	memcpy(pmbuf->pbuf + pmbuf->data_offset + HEADER_SIZE, &pkt_len,
	       sizeof(pkt_len));

	DBG_HEXDUMP(MDAT_D, "TDLS action:", pmbuf->pbuf + pmbuf->data_offset,
		    pmbuf->data_len);

	status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);

	switch (status) {
	case MLAN_STATUS_PENDING:
		atomic_inc(&priv->phandle->tx_pending);
		queue_work(priv->phandle->workqueue, &priv->phandle->main_work);
		break;
	case MLAN_STATUS_SUCCESS:
		woal_free_mlan_buffer(priv->phandle, pmbuf);
		break;
	case MLAN_STATUS_FAILURE:
	default:
		woal_free_mlan_buffer(priv->phandle, pmbuf);
		ret = -EFAULT;
		break;
	}

done:
	LEAVE();
	return ret;
}

/**
 * @brief woal send tdls data frame
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param extra_ies              A pointer to extra ie buffer
 * @param extra_ies_len          etra ie len
 * @param skb                   skb buffer
 *
 * @return                      0 -- success, otherwise fail
 */
static int
woal_send_tdls_data_frame(struct wiphy *wiphy, struct net_device *dev,
			  t_u8 *peer, u8 action_code, t_u8 dialog_token,
			  t_u16 status_code, const t_u8 *extra_ies,
			  size_t extra_ies_len)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	pmlan_buffer pmbuf = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	struct sk_buff *skb = NULL;
	int ret = 0;
#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	t_u32 index = 0;
#endif

	ENTER();

	skb = dev_alloc_skb(priv->extra_tx_head_len + MLAN_MIN_DATA_HEADER_LEN + sizeof(mlan_buffer) + max(sizeof(struct ieee80211_mgmt), sizeof(struct ieee80211_tdls_data)) + 50 +	/* supported rates */
			    sizeof(IEEEtypes_ExtCap_t) +	/* ext capab */
			    3 +	/* Qos Info */
			    sizeof(IEEEtypes_WmmParameter_t) +	/*wmm ie */
			    sizeof(IEEEtypes_HTCap_t) +
			    sizeof(IEEEtypes_2040BSSCo_t) +
			    sizeof(IEEEtypes_HTInfo_t) + extra_ies_len +
			    sizeof(IEEEtypes_tdls_linkie));
	if (!skb)
		return -ENOMEM;

	skb_reserve(skb,
		    MLAN_MIN_DATA_HEADER_LEN + sizeof(mlan_buffer) +
		    priv->extra_tx_head_len);

	woal_construct_tdls_data_frame(priv, peer,
				       action_code, dialog_token,
				       status_code, skb);

	if (extra_ies_len)
		memcpy(skb_put(skb, extra_ies_len), extra_ies, extra_ies_len);

	/* the TDLS link IE is always added last */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
		/* we are the initiator */
		woal_tdls_add_link_ie(skb, priv->current_addr, peer,
				      priv->cfg_bssid);
		break;
	case WLAN_TDLS_SETUP_RESPONSE:
		/* we are the responder */
		woal_tdls_add_link_ie(skb, peer, priv->current_addr,
				      priv->cfg_bssid);
		break;
	default:
		ret = -ENOTSUPP;
		goto fail;
	}

	/*
	 * According to 802.11z: Setup req/resp are sent in AC_BK, otherwise
	 * we should default to AC_VI.
	 */
	switch (action_code) {
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_SETUP_RESPONSE:
		skb_set_queue_mapping(skb, WMM_AC_BK);
		skb->priority = 2;
		break;
	default:
		skb_set_queue_mapping(skb, WMM_AC_VI);
		skb->priority = 5;
		break;
	}

	pmbuf = (mlan_buffer *)skb->head;
	memset((t_u8 *)pmbuf, 0, sizeof(mlan_buffer));
	pmbuf->bss_index = priv->bss_index;
	pmbuf->pdesc = skb;
	pmbuf->pbuf = skb->head + sizeof(mlan_buffer);

	pmbuf->data_offset = skb->data - (skb->head + sizeof(mlan_buffer));
	pmbuf->data_len = skb->len;
	pmbuf->priority = skb->priority;
	pmbuf->buf_type = MLAN_BUF_TYPE_DATA;

	DBG_HEXDUMP(MDAT_D, "TDLS data:", pmbuf->pbuf + pmbuf->data_offset,
		    pmbuf->data_len);

	status = mlan_send_packet(priv->phandle->pmlan_adapter, pmbuf);

	switch (status) {
	case MLAN_STATUS_PENDING:
		atomic_inc(&priv->phandle->tx_pending);
#if CFG80211_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
		index = skb_get_queue_mapping(skb);
		atomic_inc(&priv->wmm_tx_pending[index]);
#endif
		queue_work(priv->phandle->workqueue, &priv->phandle->main_work);
		/*delay 10 ms to guarantee the teardown/confirm frame can be sent out before disalbe/enable tdls link
		 * if we don't delay and return immediately, wpa_supplicant will call disalbe/enable tdls link
		 * this may cause tdls link disabled/enabled before teardown/confirm frame sent out */
		if (action_code == WLAN_TDLS_TEARDOWN ||
		    action_code == WLAN_TDLS_SETUP_CONFIRM)
			woal_sched_timeout(10);
		break;
	case MLAN_STATUS_SUCCESS:
		dev_kfree_skb(skb);
		break;
	case MLAN_STATUS_FAILURE:
	default:
		dev_kfree_skb(skb);
		ret = -ENOTSUPP;
		break;
	}

	LEAVE();
	return ret;
fail:
	dev_kfree_skb(skb);
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
/**
 * @brief Tx TDLS packet
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param peer_capability       peer capability
 * @param initiator             initiator
 * @param extra_ies              A pointer to extra ie buffer
 * @param extra_ies_len          etra ie len
 *
 * @return                      0 -- success, otherwise fail
 */
int
woal_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			const t_u8 *peer,
			u8 action_code, t_u8 dialog_token,
			t_u16 status_code, t_u32 peer_capability,
			bool initiator,
			const t_u8 *extra_ies, size_t extra_ies_len)
#else
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
/**
 * @brief Tx TDLS packet
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param peer_capability       peer capability
 * @param extra_ies              A pointer to extra ie buffer
 * @param extra_ies_len          etra ie len
 *
 * @return                      0 -- success, otherwise fail
 */
int
woal_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			const t_u8 *peer,
#else
			t_u8 *peer,
#endif
			u8 action_code, t_u8 dialog_token,
			t_u16 status_code, t_u32 peer_capability,
			const t_u8 *extra_ies, size_t extra_ies_len)
#else
/**
 * @brief Tx TDLS packet
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param peer                  A pointer to peer mac
 * @param action_code           tdls action code
 * @param dialog_token          dialog_token
 * @param status_code           status_code
 * @param extra_ies              A pointer to extra ie buffer
 * @param extra_ies_len          etra ie len
 *
 * @return                      0 -- success, otherwise fail
 */
int
woal_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev,
			t_u8 *peer, u8 action_code, t_u8 dialog_token,
			t_u16 status_code, const t_u8 *extra_ies,
			size_t extra_ies_len)
#endif
#endif
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;
	mlan_bss_info bss_info;

	ENTER();

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS)) {
		LEAVE();
		return -ENOTSUPP;
	}
	/* make sure we are not in uAP mode and Go mode */
	if (priv->bss_type != MLAN_BSS_TYPE_STA) {
		LEAVE();
		return -ENOTSUPP;
	}

	/* check if AP prohited TDLS */
	memset(&bss_info, 0, sizeof(bss_info));
	woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
	if (IS_EXTCAP_TDLS_PROHIBITED(bss_info.ext_cap)) {
		PRINTM(MMSG, "TDLS is prohibited by AP\n");
		LEAVE();
		return -ENOTSUPP;
	}

	switch (action_code) {
	case TDLS_SETUP_REQUEST:
		woal_add_tdls_peer(priv, (t_u8 *)peer);
		PRINTM(MMSG,
		       "wlan: Send TDLS Setup Request to " MACSTR
		       " status_code=%d\n", MAC2STR(peer), status_code);
		ret = woal_send_tdls_data_frame(wiphy, dev, (t_u8 *)peer,
						action_code, dialog_token,
						status_code, extra_ies,
						extra_ies_len);
		break;
	case TDLS_SETUP_RESPONSE:
		PRINTM(MMSG,
		       "wlan: Send TDLS Setup Response to " MACSTR
		       " status_code=%d\n", MAC2STR(peer), status_code);
		ret = woal_send_tdls_data_frame(wiphy, dev, (t_u8 *)peer,
						action_code, dialog_token,
						status_code, extra_ies,
						extra_ies_len);
		break;
	case TDLS_SETUP_CONFIRM:
		PRINTM(MMSG,
		       "wlan: Send TDLS Confirm to " MACSTR " status_code=%d\n",
		       MAC2STR(peer), status_code);
		ret = woal_send_tdls_data_frame(wiphy, dev, (t_u8 *)peer,
						action_code, dialog_token,
						status_code, extra_ies,
						extra_ies_len);
		break;
	case TDLS_TEARDOWN:
		PRINTM(MMSG, "wlan: Send TDLS Tear down to " MACSTR "\n",
		       MAC2STR(peer));
		ret = woal_send_tdls_data_frame(wiphy, dev, (t_u8 *)peer,
						action_code, dialog_token,
						status_code, extra_ies,
						extra_ies_len);
		break;
	case TDLS_DISCOVERY_REQUEST:
		PRINTM(MMSG,
		       "wlan: Send TDLS Discovery Request to " MACSTR "\n",
		       MAC2STR(peer));
		ret = woal_send_tdls_data_frame(wiphy, dev, (t_u8 *)peer,
						action_code, dialog_token,
						status_code, extra_ies,
						extra_ies_len);
		break;
	case TDLS_DISCOVERY_RESPONSE:
		PRINTM(MMSG,
		       "wlan: Send TDLS Discovery Response to " MACSTR "\n",
		       MAC2STR(peer));
		ret = woal_send_tdls_action_frame(wiphy, dev, (t_u8 *)peer,
						  action_code, dialog_token,
						  status_code, extra_ies,
						  extra_ies_len);
		break;
	default:
		break;
	}

	LEAVE();
	return ret;

}

/**
 * @brief cfg80211_tdls_oper handler
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param peer                  tdls peer mac
 * @param oper                  tdls operation code
 *
 * @return                  	0 -- success, otherwise fail
 */
int
woal_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			const u8 *peer,
#else
			u8 *peer,
#endif
			enum nl80211_tdls_operation oper)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	t_u8 action;
	int ret = 0;
	t_u8 event_buf[32];
	int custom_len = 0;

	ENTER();

	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	if (!(wiphy->flags & WIPHY_FLAG_TDLS_EXTERNAL_SETUP))
		return -ENOTSUPP;
	/* make sure we are in managed mode, and associated */
	if (priv->bss_type != MLAN_BSS_TYPE_STA)
		return -ENOTSUPP;

	PRINTM(MIOCTL, "wlan: TDLS peer=" MACSTR ", oper=%d\n", MAC2STR(peer),
	       oper);
	switch (oper) {
	case NL80211_TDLS_ENABLE_LINK:
		/*Configure TDLS link first */
		woal_tdls_oper(priv, (u8 *)peer, WLAN_TDLS_CONFIG_LINK);
		woal_updata_peer_status(priv, (t_u8 *)peer,
					TDLS_SETUP_COMPLETE);
		PRINTM(MMSG, "wlan: TDLS_ENABLE_LINK: peer=" MACSTR "\n",
		       MAC2STR(peer));
		action = WLAN_TDLS_ENABLE_LINK;
		memset(event_buf, 0, sizeof(event_buf));
		custom_len = strlen(CUS_EVT_TDLS_CONNECTED);
		memcpy(event_buf, CUS_EVT_TDLS_CONNECTED, custom_len);
		memcpy(event_buf + custom_len, peer, ETH_ALEN);
		woal_broadcast_event(priv, event_buf, custom_len + ETH_ALEN);
		break;
	case NL80211_TDLS_DISABLE_LINK:
		woal_updata_peer_status(priv, (t_u8 *)peer, TDLS_NOT_SETUP);
		PRINTM(MMSG, "wlan: TDLS_DISABLE_LINK: peer=" MACSTR "\n",
		       MAC2STR(peer));
		action = WLAN_TDLS_DISABLE_LINK;
		memset(event_buf, 0, sizeof(event_buf));
		custom_len = strlen(CUS_EVT_TDLS_TEARDOWN);
		memcpy(event_buf, CUS_EVT_TDLS_TEARDOWN, custom_len);
		memcpy(event_buf + custom_len, peer, ETH_ALEN);
		woal_broadcast_event(priv, event_buf, custom_len + ETH_ALEN);
		break;
	case NL80211_TDLS_TEARDOWN:
	case NL80211_TDLS_SETUP:
	case NL80211_TDLS_DISCOVERY_REQ:
		return 0;

	default:
		return -ENOTSUPP;
	}
	ret = woal_tdls_oper(priv, (u8 *)peer, action);

	LEAVE();
	return ret;
}

/**
 * @brief add station
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param mac                  A pointer to peer mac
 * @param params           	station parameters
 *
 * @return                  	0 -- success, otherwise fail
 */
static int
woal_cfg80211_add_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			  const u8 *mac,
#else
			  u8 *mac,
#endif
			  struct station_parameters *params)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	int ret = 0;
	ENTER();
	if (!(params->sta_flags_set & MBIT(NL80211_STA_FLAG_TDLS_PEER)))
		goto done;
	/* make sure we are in connected mode */
	if ((priv->bss_type != MLAN_BSS_TYPE_STA) ||
	    (priv->media_connected == MFALSE)) {
		ret = -ENOTSUPP;
		goto done;
	}
	PRINTM(MMSG, "wlan: TDLS add peer station, address =" MACSTR "\n",
	       MAC2STR(mac));
	ret = woal_tdls_oper(priv, (u8 *)mac, WLAN_TDLS_CREATE_LINK);
done:
	return ret;
}

/**
 * @brief change station info
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param mac                   A pointer to peer mac
 * @param params                station parameters
 *
 * @return                      0 -- success, otherwise fail
 */
static int
woal_cfg80211_change_station(struct wiphy *wiphy, struct net_device *dev,
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
			     const u8 *mac,
#else
			     u8 *mac,
#endif
			     struct station_parameters *params)
{
	int ret = 0;

	ENTER();

    /**do nothing*/

	LEAVE();
	return ret;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/**
 * @brief tdls channel switch
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param addr                  A pointer to peer addr
 * @param oper_class            The operating class
 * @param chandef               A pointer to cfg80211_chan_def structure
 *
 * @return                      0 -- success, otherwise fail
 */
static int
woal_cfg80211_tdls_channel_switch(struct wiphy *wiphy,
				  struct net_device *dev,
				  const u8 *addr, u8 oper_class,
				  struct cfg80211_chan_def *chandef)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ds_misc_tdls_config *tdls_data = NULL;
	tdls_all_config *tdls_all_cfg = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_bss_info bss_info;

	ENTER();

	/* check if AP prohited TDLS channel switch */
	memset(&bss_info, 0, sizeof(bss_info));
	woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
	if (IS_EXTCAP_TDLS_CHLSWITCHPROHIB(bss_info.ext_cap)) {
		PRINTM(MMSG, "TDLS Channel Switching is prohibited by AP\n");
		LEAVE();
		return -ENOTSUPP;
	}

	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	misc = (mlan_ds_misc_cfg *)ioctl_req->pbuf;
	misc->sub_command = MLAN_OID_MISC_TDLS_OPER;
	ioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	ioctl_req->action = MLAN_ACT_SET;

	tdls_data = &misc->param.tdls_config;
	tdls_data->tdls_action = WLAN_TDLS_INIT_CHAN_SWITCH;

	tdls_all_cfg = (tdls_all_config *)tdls_data->tdls_data;
	memcpy(tdls_all_cfg->u.tdls_chan_switch.peer_mac_addr, addr, ETH_ALEN);
	tdls_all_cfg->u.tdls_chan_switch.primary_channel =
		chandef->chan->hw_value;
	tdls_all_cfg->u.tdls_chan_switch.band = chandef->chan->band;
	tdls_all_cfg->u.tdls_chan_switch.regulatory_class = oper_class;

	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		PRINTM(MERROR, "TDLS channel switch request failed.\n");
		ret = -EFAULT;
		goto done;
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);
	LEAVE();
	return ret;
}

/**
 * @brief tdls cancel channel switch
 *
 * @param wiphy                 A pointer to wiphy structure
 * @param dev                   A pointer to net_device structure
 * @param addr                  A pointer to peer addr
 *
 */
void
woal_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy,
					 struct net_device *dev, const u8 *addr)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	mlan_ioctl_req *ioctl_req = NULL;
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ds_misc_tdls_config *tdls_data = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	int ret = 0;

	ENTER();

	if (!priv || !priv->phandle) {
		PRINTM(MERROR, "priv or handle is null\n");
		ret = -EFAULT;
		goto done;
	}

	ioctl_req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg));
	if (ioctl_req == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	misc = (mlan_ds_misc_cfg *)ioctl_req->pbuf;
	misc->sub_command = MLAN_OID_MISC_TDLS_CONFIG;
	ioctl_req->req_id = MLAN_IOCTL_MISC_CFG;
	ioctl_req->action = MLAN_ACT_SET;

	tdls_data = &misc->param.tdls_config;
	tdls_data->tdls_action = WLAN_TDLS_STOP_CHAN_SWITCH;
	memcpy(tdls_data->tdls_data, addr, ETH_ALEN);

	status = woal_request_ioctl(priv, ioctl_req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto done;
	}

	PRINTM(MIOCTL, "Tdls channel switch stop!\n");
done:
	if (status != MLAN_STATUS_PENDING)
		kfree(ioctl_req);

	LEAVE();
}
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,10,0)
/**
 * @brief Update ft ie for Fast BSS Transition
 *
 * @param wiphy           A pointer to wiphy structure
 * @param dev             A pointer to net_device structure
 * @param ftie           A pointer to cfg80211_update_ft_ies_params structure
 *
 * @return                0 success , other failure
 */
int
woal_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_update_ft_ies_params *ftie)
{
	moal_private *priv = (moal_private *)woal_get_netdev_priv(dev);
	IEEEtypes_MobilityDomain_t *md_ie = NULL;
	int ret = 0;
	mlan_ds_misc_assoc_rsp assoc_rsp;
	IEEEtypes_AssocRsp_t *passoc_rsp = NULL;
	mlan_bss_info bss_info;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
	struct cfg80211_roam_info roam_info;
#endif

	ENTER();

	if (!ftie) {
		LEAVE();
		return ret;
	}
#ifdef MLAN_64BIT
	PRINTM(MINFO, "==>woal_cfg80211_update_ft_ies %lx \n", ftie->ie_len);
#else
	PRINTM(MINFO, "==>woal_cfg80211_update_ft_ies %x \n", ftie->ie_len);
#endif
	md_ie = (IEEEtypes_MobilityDomain_t *)woal_parse_ie_tlv(ftie->ie,
								ftie->ie_len,
								MOBILITY_DOMAIN);
	if (!md_ie) {
		PRINTM(MERROR, "No Mobility domain IE\n");
		LEAVE();
		return ret;
	}
	priv->ft_cap = md_ie->ft_cap;
	memset(priv->ft_ie, 0, MAX_IE_SIZE);
	memcpy(priv->ft_ie, ftie->ie, MIN(ftie->ie_len, MAX_IE_SIZE));
	priv->ft_ie_len = ftie->ie_len;
	priv->ft_md = ftie->md;

	if (!priv->ft_pre_connect) {
		LEAVE();
		return ret;
	}
	/* check if is different AP */
	if (!memcmp
	    (&priv->target_ap_bssid, priv->cfg_bssid, MLAN_MAC_ADDR_LENGTH)) {
		PRINTM(MMSG, "This is the same AP, no Fast bss transition\n");
		priv->ft_pre_connect = MFALSE;
		priv->ft_ie_len = 0;
		LEAVE();
		return 0;
	}

	/* start fast BSS transition to target AP */
	priv->assoc_status = 0;
	priv->sme_current.bssid = priv->conn_bssid;
	memcpy((void *)priv->sme_current.bssid, &priv->target_ap_bssid,
	       MLAN_MAC_ADDR_LENGTH);
	ret = woal_cfg80211_assoc(priv, (void *)&priv->sme_current,
				  MOAL_IOCTL_WAIT);

	if ((priv->ft_cap & MBIT(0)) || priv->ft_roaming_triggered_by_driver) {
		if (!ret) {
			woal_inform_bss_from_scan_result(priv, NULL,
							 MOAL_IOCTL_WAIT);
			memset(&assoc_rsp, 0, sizeof(mlan_ds_misc_assoc_rsp));
			woal_get_assoc_rsp(priv, &assoc_rsp, MOAL_IOCTL_WAIT);
			passoc_rsp =
				(IEEEtypes_AssocRsp_t *)assoc_rsp.
				assoc_resp_buf;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
			memset(&roam_info, 0,
			       sizeof(struct cfg80211_roam_info));
			roam_info.bssid = priv->cfg_bssid;
			roam_info.req_ie = priv->sme_current.ie;
			roam_info.req_ie_len = priv->sme_current.ie_len;
			roam_info.resp_ie = passoc_rsp->ie_buffer;
			roam_info.resp_ie_len =
				assoc_rsp.assoc_resp_len -
				ASSOC_RESP_FIXED_SIZE;
			cfg80211_roamed(priv->netdev, &roam_info, GFP_KERNEL);
#else
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
			cfg80211_roamed(priv->netdev, NULL, priv->cfg_bssid,
					priv->sme_current.ie,
					priv->sme_current.ie_len,
					passoc_rsp->ie_buffer,
					assoc_rsp.assoc_resp_len -
					ASSOC_RESP_FIXED_SIZE, GFP_KERNEL);
#else
			cfg80211_roamed(priv->netdev, priv->cfg_bssid,
					priv->sme_current.ie,
					priv->sme_current.ie_len,
					passoc_rsp->ie_buffer,
					assoc_rsp.assoc_resp_len -
					ASSOC_RESP_FIXED_SIZE, GFP_KERNEL);
#endif
#endif
			PRINTM(MMSG,
			       "Fast BSS transition to bssid " MACSTR
			       " successfully\n", MAC2STR(priv->cfg_bssid));
		} else {
			PRINTM(MMSG,
			       "Fast BSS transition failed, keep connect to "
			       MACSTR " \n", MAC2STR(priv->cfg_bssid));
			priv->ft_ie_len = 0;
		}
		priv->ft_roaming_triggered_by_driver = MFALSE;

	} else {
		PRINTM(MMSG, "Fast BSS Transition use ft-over-air\n");
		if (!ret) {
			memset(&assoc_rsp, 0, sizeof(mlan_ds_misc_assoc_rsp));
			woal_get_assoc_rsp(priv, &assoc_rsp, MOAL_IOCTL_WAIT);
			passoc_rsp =
				(IEEEtypes_AssocRsp_t *)assoc_rsp.
				assoc_resp_buf;
			cfg80211_connect_result(priv->netdev, priv->cfg_bssid,
						NULL, 0, passoc_rsp->ie_buffer,
						assoc_rsp.assoc_resp_len -
						ASSOC_RESP_FIXED_SIZE,
						WLAN_STATUS_SUCCESS,
						GFP_KERNEL);
			PRINTM(MMSG,
			       "wlan: Fast Bss transition to bssid " MACSTR
			       " successfully\n", MAC2STR(priv->cfg_bssid));

			memset(&bss_info, 0, sizeof(bss_info));
			woal_get_bss_info(priv, MOAL_IOCTL_WAIT, &bss_info);
			priv->channel = bss_info.bss_chan;
		} else {
			PRINTM(MMSG,
			       "wlan: Failed to connect to bssid " MACSTR "\n",
			       MAC2STR(priv->cfg_bssid));
			cfg80211_connect_result(priv->netdev, priv->cfg_bssid,
						NULL, 0, NULL, 0,
						woal_get_assoc_status(priv),
						GFP_KERNEL);
			memset(priv->cfg_bssid, 0, ETH_ALEN);
			priv->ft_ie_len = 0;
		}
	}

	priv->ft_pre_connect = MFALSE;
	LEAVE();
	return 0;
}
#endif

/**
 * @brief Save connect parameters for roaming
 *
 * @param priv            A pointer to moal_private
 * @param sme             A pointer to cfg80211_connect_params structure
 */
void
woal_save_conn_params(moal_private *priv, struct cfg80211_connect_params *sme)
{
	ENTER();
	woal_clear_conn_params(priv);
	memcpy(&priv->sme_current, sme, sizeof(struct cfg80211_connect_params));
	if (sme->channel) {
		priv->sme_current.channel = &priv->conn_chan;
		memcpy(priv->sme_current.channel, sme->channel,
		       sizeof(struct ieee80211_channel));
	}
	if (sme->bssid) {
		priv->sme_current.bssid = priv->conn_bssid;
		memcpy((void *)priv->sme_current.bssid, sme->bssid,
		       MLAN_MAC_ADDR_LENGTH);
	}
	if (sme->ssid && sme->ssid_len) {
		priv->sme_current.ssid = priv->conn_ssid;
		memset(priv->conn_ssid, 0, MLAN_MAX_SSID_LENGTH);
		memcpy((void *)priv->sme_current.ssid, sme->ssid,
		       sme->ssid_len);
	}
	if (sme->ie && sme->ie_len) {
		priv->sme_current.ie = kzalloc(sme->ie_len, GFP_KERNEL);
		memcpy((void *)priv->sme_current.ie, sme->ie, sme->ie_len);
	}
	if (sme->key && sme->key_len && (sme->key_len <= MAX_WEP_KEY_SIZE)) {
		priv->sme_current.key = priv->conn_wep_key;
		memcpy((t_u8 *)priv->sme_current.key, sme->key, sme->key_len);
	}
}

/**
 * @brief clear connect parameters for ing
 *
 * @param priv            A pointer to moal_private
 */
void
woal_clear_conn_params(moal_private *priv)
{
	ENTER();
	if (priv->sme_current.ie_len)
		kfree(priv->sme_current.ie);
	memset(&priv->sme_current, 0, sizeof(struct cfg80211_connect_params));
	priv->roaming_required = MFALSE;
	LEAVE();
}

/**
 * @brief Build new roaming connect ie for okc
 *
 * @param priv            A pointer to moal_private
 * @param entry           A pointer to pmksa_entry
 **/
int
woal_update_okc_roaming_ie(moal_private *priv, struct pmksa_entry *entry)
{
	struct cfg80211_connect_params *sme = &priv->sme_current;
	int ret = MLAN_STATUS_SUCCESS;
	const t_u8 *sme_pos, *sme_ptr;
	t_u8 *okc_ie_pos;
	t_u8 id, ie_len;
	int left_len;

	ENTER();

	if (!sme->ie || !sme->ie_len) {
		PRINTM(MERROR, "No connect ie saved in driver\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (!entry) {
		PRINTM(MERROR, "No roaming ap pmkid\n");
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	if (!priv->okc_roaming_ie) {
		int okc_ie_len = sme->ie_len + sizeof(t_u16) + PMKID_LEN;

	/** Alloc new buffer for okc roaming ie */
		priv->okc_roaming_ie = kzalloc(okc_ie_len, GFP_KERNEL);
		if (!priv->okc_roaming_ie) {
			PRINTM(MERROR, "Fail to allocate assoc req ie\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	/* Build OKC RSN IE with PMKID list
	 * Format of RSN IE: length(bytes) and container
	 * | 1| 1 |   2   |          4            |           2               |
	 * |id|len|version|group data cipher suite|pairwise cipher suite count|
	 * |          4 * m           |       2       |    4 * n     |   2    |
	 * |pairwise cipher suite list|AKM suite count|AKM suite list|RSN Cap |
	 * |    2     |  16 * s  |              4              |
	 * |PMKIDCount|PMKID List|Group Management Cipher Suite|
	 */
#define PAIRWISE_CIPHER_COUNT_OFFSET 8
#define AKM_SUITE_COUNT_OFFSET(n) (10 + (n) * 4)
#define PMKID_COUNT_OFFSET(n) (14 + (n) * 4)

	sme_pos = sme->ie;
	left_len = sme->ie_len;
	okc_ie_pos = priv->okc_roaming_ie;
	priv->okc_ie_len = 0;

	while (left_len >= 2) {
		id = *sme_pos;
		ie_len = *(sme_pos + 1);
		if ((ie_len + 2) > left_len) {
			PRINTM(MERROR, "Invalid ie len %d\n", ie_len);
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		if (id == RSN_IE) {
			t_u16 pairwise_count, akm_count;
			t_u8 *rsn_ie_len;
			int rsn_offset;

			pairwise_count =
				*(t_u16 *)(sme_pos +
					   PAIRWISE_CIPHER_COUNT_OFFSET);
			akm_count =
				*(t_u16 *)(sme_pos +
					   AKM_SUITE_COUNT_OFFSET
					   (pairwise_count));
			rsn_offset =
				PMKID_COUNT_OFFSET(pairwise_count + akm_count);
			sme_ptr = (t_u8 *)(sme_pos + rsn_offset);

			memcpy(okc_ie_pos, sme_pos, rsn_offset);
			rsn_ie_len = okc_ie_pos + 1;
			okc_ie_pos += rsn_offset;
			*(t_u16 *)okc_ie_pos = 1;
			okc_ie_pos += sizeof(t_u16);
			memcpy(okc_ie_pos, entry->pmkid, PMKID_LEN);
			okc_ie_pos += PMKID_LEN;
			priv->okc_ie_len +=
				rsn_offset + sizeof(t_u16) + PMKID_LEN;
			*rsn_ie_len =
				rsn_offset - 2 + sizeof(t_u16) + PMKID_LEN;

			if ((ie_len + 2) > rsn_offset) {
		/** Previous conn ie include pmkid list */
				u16 pmkid_count = *(t_u16 *)sme_ptr;
				rsn_offset +=
					(sizeof(t_u16) +
					 PMKID_LEN * pmkid_count);
				if ((ie_len + 2) > rsn_offset) {
					sme_ptr +=
						(sizeof(t_u16) +
						 PMKID_LEN * pmkid_count);
					memcpy(okc_ie_pos, sme_ptr,
					       (ie_len + 2 - rsn_offset));
					okc_ie_pos += (ie_len + 2 - rsn_offset);
					priv->okc_ie_len +=
						(ie_len + 2 - rsn_offset);
					*rsn_ie_len +=
						(ie_len + 2 - rsn_offset);
				}
			}
		} else {
			memcpy(okc_ie_pos, sme_pos, ie_len + 2);
			okc_ie_pos += ie_len + 2;
			priv->okc_ie_len += ie_len + 2;
		}

		sme_pos += (ie_len + 2);
		left_len -= (ie_len + 2);
	}

done:
	if (ret != MLAN_STATUS_SUCCESS) {
		if (priv->okc_roaming_ie) {
			kfree(priv->okc_roaming_ie);
			priv->okc_roaming_ie = NULL;
			priv->okc_ie_len = 0;
		}
	}

	LEAVE();
	return ret;
}

/**
 * @brief Start roaming: driver handle roaming
 *
 * @param priv      A pointer to moal_private structure
 *
 * @return          N/A
 */
void
woal_start_roaming(moal_private *priv)
{
	mlan_ds_get_signal signal;
	mlan_ssid_bssid ssid_bssid;
	char rssi_low[10];
	int ret = 0;
	mlan_ds_misc_assoc_rsp assoc_rsp;
	IEEEtypes_AssocRsp_t *passoc_rsp = NULL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
	struct cfg80211_roam_info roam_info;
#endif

	ENTER();
	if (priv->ft_roaming_triggered_by_driver) {
		PRINTM(MIOCTL, "FT roaming is in processing ...... \n");
		LEAVE();
		return;
	}

	if (priv->last_event & EVENT_BG_SCAN_REPORT) {
		woal_inform_bss_from_scan_result(priv, NULL, MOAL_IOCTL_WAIT);
		PRINTM(MIOCTL, "Report bgscan result\n");
	}
	if (priv->media_connected == MFALSE || !priv->sme_current.ssid_len) {
		PRINTM(MIOCTL, "Not connected, ignore roaming\n");
		LEAVE();
		return;
	}

	/* Get signal information from the firmware */
	memset(&signal, 0, sizeof(mlan_ds_get_signal));
	if (MLAN_STATUS_SUCCESS !=
	    woal_get_signal_info(priv, MOAL_IOCTL_WAIT, &signal)) {
		PRINTM(MERROR, "Error getting signal information\n");
		ret = -EFAULT;
		goto done;
	}
	memset(&ssid_bssid, 0, sizeof(mlan_ssid_bssid));
	ssid_bssid.ssid.ssid_len = priv->sme_current.ssid_len;
	memcpy(ssid_bssid.ssid.ssid, priv->sme_current.ssid,
	       priv->sme_current.ssid_len);
	if (MLAN_STATUS_SUCCESS !=
	    woal_find_best_network(priv, MOAL_IOCTL_WAIT, &ssid_bssid)) {
		PRINTM(MIOCTL, "Can not find better network\n");
		ret = -EFAULT;
		goto done;
	}
	/* check if we found different AP */
	if (!memcmp(&ssid_bssid.bssid, priv->cfg_bssid, MLAN_MAC_ADDR_LENGTH)) {
		PRINTM(MIOCTL, "This is the same AP, no roaming\n");
		ret = -EFAULT;
		goto done;
	}
	PRINTM(MIOCTL, "Find AP: bssid=" MACSTR ", signal=%d\n",
	       MAC2STR(ssid_bssid.bssid), ssid_bssid.rssi);
	/* check signal */
	if (!(priv->last_event & EVENT_PRE_BCN_LOST)) {
		if ((abs(signal.bcn_rssi_avg) - abs(ssid_bssid.rssi)) <
		    DELTA_RSSI) {
			PRINTM(MERROR, "New AP's signal is not good too.\n");
			ret = -EFAULT;
			goto done;
		}
	}
/**check if need start FT Roaming*/
	if (priv->ft_ie_len && (priv->ft_md == ssid_bssid.ft_md) &&
	    (priv->ft_cap == ssid_bssid.ft_cap)) {
		woal_start_ft_roaming(priv, &ssid_bssid);
		goto done;
	}
	/* start roaming to new AP */
	priv->sme_current.bssid = priv->conn_bssid;
	memcpy((void *)priv->sme_current.bssid, &ssid_bssid.bssid,
	       MLAN_MAC_ADDR_LENGTH);

#ifdef STA_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
	if (IS_STA_CFG80211(cfg80211_wext)) {
	/** Check if current roaming support OKC offload roaming */
		if (priv->sme_current.crypto.n_akm_suites &&
		    priv->sme_current.crypto.akm_suites[0] ==
		    WLAN_AKM_SUITE_8021X) {
			struct pmksa_entry *entry = NULL;

	    /** Get OKC PMK Cache entry
             *  Firstly try to get pmksa from cfg80211
             */
			priv->wait_target_ap_pmkid = MTRUE;
			cfg80211_pmksa_candidate_notify(priv->netdev, 0,
							priv->sme_current.bssid,
							MTRUE, GFP_ATOMIC);
			if (wait_event_interruptible_timeout(priv->okc_wait_q,
							     !priv->
							     wait_target_ap_pmkid,
							     OKC_WAIT_TARGET_PMKSA_TIMEOUT))
			{
				PRINTM(MIOCTL, "OKC Roaming is ready\n");
				entry = priv->target_ap_pmksa;
			} else {
		/** Try to get pmksa from pmksa list */
				priv->wait_target_ap_pmkid = MFALSE;
				entry = woal_get_pmksa_entry(priv,
							     priv->sme_current.
							     bssid);
			}
	    /** Build okc roaming ie */
			woal_update_okc_roaming_ie(priv, entry);
			priv->target_ap_pmksa = NULL;
		}
	}
#endif
#endif

	ret = woal_cfg80211_assoc(priv, (void *)&priv->sme_current,
				  MOAL_IOCTL_WAIT);
	if (!ret) {
		const t_u8 *ie;
		int ie_len;

		woal_inform_bss_from_scan_result(priv, NULL, MOAL_IOCTL_WAIT);
		memset(&assoc_rsp, 0, sizeof(mlan_ds_misc_assoc_rsp));
		woal_get_assoc_rsp(priv, &assoc_rsp, MOAL_IOCTL_WAIT);
		passoc_rsp = (IEEEtypes_AssocRsp_t *)assoc_rsp.assoc_resp_buf;

	/** Update connect ie in roam event */
		ie = priv->sme_current.ie;
		ie_len = priv->sme_current.ie_len;
#ifdef STA_CFG80211
		if (IS_STA_CFG80211(cfg80211_wext)) {
	/** Check if current roaming support OKC offload roaming */
			if (priv->sme_current.crypto.n_akm_suites &&
			    priv->sme_current.crypto.akm_suites[0] ==
			    WLAN_AKM_SUITE_8021X) {
				if (priv->okc_roaming_ie && priv->okc_ie_len) {
					ie = priv->okc_roaming_ie;
					ie_len = priv->okc_ie_len;
				}
			}
		}
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
		memset(&roam_info, 0, sizeof(struct cfg80211_roam_info));
		roam_info.bssid = priv->cfg_bssid;
		roam_info.req_ie = ie;
		roam_info.req_ie_len = ie_len;
		roam_info.resp_ie = passoc_rsp->ie_buffer;
		roam_info.resp_ie_len =
			assoc_rsp.assoc_resp_len - ASSOC_RESP_FIXED_SIZE;
		cfg80211_roamed(priv->netdev, &roam_info, GFP_KERNEL);
#else
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
		cfg80211_roamed(priv->netdev, NULL, priv->cfg_bssid, ie, ie_len,
				passoc_rsp->ie_buffer,
				assoc_rsp.assoc_resp_len -
				ASSOC_RESP_FIXED_SIZE, GFP_KERNEL);
#else
		cfg80211_roamed(priv->netdev, priv->cfg_bssid, ie, ie_len,
				passoc_rsp->ie_buffer,
				assoc_rsp.assoc_resp_len -
				ASSOC_RESP_FIXED_SIZE, GFP_KERNEL);
#endif
#endif
		PRINTM(MMSG, "Roamed to bssid " MACSTR " successfully\n",
		       MAC2STR(priv->cfg_bssid));
	} else {
		PRINTM(MIOCTL, "Roaming to bssid " MACSTR " failed\n",
		       MAC2STR(ssid_bssid.bssid));
	}
done:
	/* config rssi low threshold again */
	priv->last_event = 0;
	priv->rssi_low = DEFAULT_RSSI_LOW_THRESHOLD;
	sprintf(rssi_low, "%d", priv->rssi_low);
	woal_set_rssi_low_threshold(priv, rssi_low, MOAL_IOCTL_WAIT);
	LEAVE();
	return;
}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
/**
 *  @brief This function update channel region config
 *
 *  @param buf              Buffer containing channel region config
 *  @param num_chan         Length of buffer
 *  @param regd             ieee80211_regdomain to be updated
 *
 *  @return                 N/A
 */
static struct ieee80211_regdomain *
create_custom_regdomain(t_u8 *buf, t_u16 num_chan)
{
	struct ieee80211_reg_rule *rule;
	bool new_rule;
	int idx, freq, prev_freq = 0;
	t_u32 bw, prev_bw = 0;
	t_u8 chflags, prev_chflags = 0, valid_rules = 0;
	struct ieee80211_regdomain *regd = NULL;
	int regd_size;

	regd_size = sizeof(struct ieee80211_regdomain) +
		num_chan * sizeof(struct ieee80211_reg_rule);

	regd = kzalloc(regd_size, GFP_KERNEL);
	if (!regd) {
		return NULL;
	}

	for (idx = 0; idx < num_chan; idx++) {
		t_u8 chan;
		enum ieee80211_band band;

		chan = *buf++;
		if (!chan) {
			return NULL;
		}
		chflags = *buf++;
		band = (chan <= 14) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(chan, band);
		new_rule = false;

		if (chflags & MARVELL_CHANNEL_DISABLED)
			continue;

		if (band == IEEE80211_BAND_5GHZ) {
			if (!(chflags & MARVELL_CHANNEL_NOHT80))
				bw = MHZ_TO_KHZ(80);
			else if (!(chflags & MARVELL_CHANNEL_NOHT40))
				bw = MHZ_TO_KHZ(40);
			else
				bw = MHZ_TO_KHZ(20);
		} else {
			if (!(chflags & MARVELL_CHANNEL_NOHT40))
				bw = MHZ_TO_KHZ(40);
			else
				bw = MHZ_TO_KHZ(20);
		}

		if (idx == 0 || prev_chflags != chflags || prev_bw != bw ||
		    freq - prev_freq > 20) {
			valid_rules++;
			new_rule = true;
		}

		rule = &regd->reg_rules[valid_rules - 1];

		rule->freq_range.end_freq_khz = MHZ_TO_KHZ(freq + 10);

		prev_chflags = chflags;
		prev_freq = freq;
		prev_bw = bw;

		if (!new_rule)
			continue;

		rule->freq_range.start_freq_khz = MHZ_TO_KHZ(freq - 10);
		rule->power_rule.max_eirp = DBM_TO_MBM(19);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		if (chflags & MARVELL_CHANNEL_PASSIVE)
			rule->flags = NL80211_RRF_NO_IR;
#endif
		if (chflags & MARVELL_CHANNEL_DFS)
			rule->flags = NL80211_RRF_DFS;

		rule->freq_range.max_bandwidth_khz = bw;
	}

	regd->n_reg_rules = valid_rules;

	/* set alpha2 from FW. */
	regd->alpha2[0] = '9';
	regd->alpha2[1] = '9';

	return regd;
}

/**
 *  @brief create custom channel regulatory config
 *
 *  @param priv         A pointer to moal_private structure
 *
 *  @return		        if success pointer to ieee80211_regdomain, else NULL
 */
static struct ieee80211_regdomain *
woal_create_custom_regdomain(moal_private *priv)
{
	mlan_ds_misc_cfg *misc = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_status status = MLAN_STATUS_SUCCESS;
	struct ieee80211_regdomain *regd = NULL;
	t_u16 num_chan;

	ENTER();

	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_misc_cfg) +
					REGULATORY_CFG_LEN);
	if (req == NULL) {
		goto done;
	}
	misc = (mlan_ds_misc_cfg *)req->pbuf;
	misc->sub_command = MLAN_OID_MISC_GET_CHAN_REGION_CFG;
	req->req_id = MLAN_IOCTL_MISC_CFG;
	req->action = MLAN_ACT_GET;
	memset(&misc->param.custom_reg_domain, 0,
	       sizeof(misc->param.custom_reg_domain));
	/* Passing maximum buffer length to mlan */
	misc->param.custom_reg_domain.cfg_len = REGULATORY_CFG_LEN;
	status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
	if (status != MLAN_STATUS_SUCCESS) {
		goto done;
	}

	if (misc->param.custom_reg_domain.cfg_len) {
		num_chan = misc->param.custom_reg_domain.cfg_len / 2;
		if (num_chan > NL80211_MAX_SUPP_REG_RULES) {
			goto done;
		}

		regd = create_custom_regdomain(misc->param.custom_reg_domain.
					       cfg_buf, num_chan);
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return regd;
}
#endif

/**
 * @brief Register the device with cfg80211
 *
 * @param dev       A pointer to net_device structure
 * @param bss_type  BSS type
 *
 * @return          MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_register_sta_cfg80211(struct net_device *dev, t_u8 bss_type)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	struct wireless_dev *wdev = NULL;
	int psmode = 0;

	ENTER();

	wdev = (struct wireless_dev *)&priv->w_dev;
	memset(wdev, 0, sizeof(struct wireless_dev));
	wdev->wiphy = priv->phandle->wiphy;
	if (!wdev->wiphy) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	if (bss_type == MLAN_BSS_TYPE_STA) {
		wdev->iftype = NL80211_IFTYPE_STATION;
		priv->roaming_enabled = MFALSE;
		priv->roaming_required = MFALSE;
	}
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (bss_type == MLAN_BSS_TYPE_WIFIDIRECT)
		wdev->iftype = NL80211_IFTYPE_STATION;
#endif
#endif
	if (bss_type == MLAN_BSS_TYPE_NAN)
		wdev->iftype = NL80211_IFTYPE_STATION;
	dev_net_set(dev, wiphy_net(wdev->wiphy));
	dev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(dev, wiphy_dev(wdev->wiphy));
	priv->wdev = wdev;
	/* Get IEEE power save mode */
	if (MLAN_STATUS_SUCCESS ==
	    woal_set_get_power_mgmt(priv, MLAN_ACT_GET, &psmode, 0,
				    MOAL_IOCTL_WAIT)) {
		/* Save the IEEE power save mode to wiphy, because after
		 * warmreset wiphy power save should be updated instead
		 * of using the last saved configuration */
		if (psmode)
			priv->wdev->ps = MTRUE;
		else
			priv->wdev->ps = MFALSE;
	}
	woal_send_domain_info_cmd_fw(priv, MOAL_IOCTL_WAIT);
	LEAVE();
	return ret;
}

/**
 * @brief Initialize the wiphy
 *
 * @param priv            A pointer to moal_private structure
 * @param wait_option     Wait option
 *
 * @return                MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
woal_cfg80211_init_wiphy(moal_private *priv, t_u8 wait_option)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	int retry_count, rts_thr, frag_thr;
	struct wiphy *wiphy = NULL;
	mlan_ioctl_req *req = NULL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	mlan_ds_radio_cfg *radio = NULL;
#endif
	mlan_ds_11n_cfg *cfg_11n = NULL;
	t_u32 hw_dev_cap;
#ifdef UAP_SUPPORT
	mlan_uap_bss_param *sys_cfg = NULL;
#endif
#if CFG80211_VERSION_CODE > KERNEL_VERSION(3, 0, 0)
	t_u16 enable = 0;
#endif

	ENTER();

	wiphy = priv->phandle->wiphy;
	/* Get 11n tx parameters from MLAN */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_11n_cfg));
	if (req == NULL) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	cfg_11n = (mlan_ds_11n_cfg *)req->pbuf;
	cfg_11n->sub_command = MLAN_OID_11N_HTCAP_CFG;
	req->req_id = MLAN_IOCTL_11N_CFG;
	req->action = MLAN_ACT_GET;
	cfg_11n->param.htcap_cfg.hw_cap_req = MTRUE;

	ret = woal_request_ioctl(priv, req, wait_option);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;
	hw_dev_cap = cfg_11n->param.htcap_cfg.htcap;

	/* Get supported MCS sets */
	memset(req->pbuf, 0, sizeof(mlan_ds_11n_cfg));
	cfg_11n->sub_command = MLAN_OID_11N_CFG_SUPPORTED_MCS_SET;
	req->req_id = MLAN_IOCTL_11N_CFG;
	req->action = MLAN_ACT_GET;

	ret = woal_request_ioctl(priv, req, wait_option);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* Initialize parameters for 2GHz and 5GHz bands */
	if (wiphy->bands[IEEE80211_BAND_2GHZ])
		woal_cfg80211_setup_ht_cap(&wiphy->bands[IEEE80211_BAND_2GHZ]->
					   ht_cap, hw_dev_cap,
					   cfg_11n->param.supported_mcs_set);
	/* For 2.4G band only card, this shouldn't be set */
	if (wiphy->bands[IEEE80211_BAND_5GHZ]) {
		woal_cfg80211_setup_ht_cap(&wiphy->bands[IEEE80211_BAND_5GHZ]->
					   ht_cap, hw_dev_cap,
					   cfg_11n->param.supported_mcs_set);
	}
	kfree(req);

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
	/* Get antenna modes */
	req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_radio_cfg));
	if (req == NULL) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	radio = (mlan_ds_radio_cfg *)req->pbuf;
	radio->sub_command = MLAN_OID_ANT_CFG;
	req->req_id = MLAN_IOCTL_RADIO_CFG;
	req->action = MLAN_ACT_GET;

	ret = woal_request_ioctl(priv, req, wait_option);
	if (ret != MLAN_STATUS_SUCCESS)
		goto done;

	/* Set available antennas to wiphy */
	wiphy->available_antennas_tx = radio->param.ant_cfg_1x1.antenna;
	wiphy->available_antennas_rx = radio->param.ant_cfg_1x1.antenna;
#endif /* CFG80211_VERSION_CODE */

	/* Set retry limit count to wiphy */
	if (GET_BSS_ROLE(priv) == MLAN_BSS_ROLE_STA) {
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_retry(priv, MLAN_ACT_GET, wait_option,
				       &retry_count)) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}
#ifdef UAP_SUPPORT
	else {
		sys_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
		if (!sys_cfg) {
			PRINTM(MERROR,
			       "Fail to alloc memory for mlan_uap_bss_param\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_sys_config(priv, MLAN_ACT_GET, wait_option,
					    sys_cfg)) {
			ret = MLAN_STATUS_FAILURE;
			kfree(sys_cfg);
			goto done;
		}
		retry_count = sys_cfg->retry_limit;
		kfree(sys_cfg);
	}
#endif
	wiphy->retry_long = (t_u8)retry_count;
	wiphy->retry_short = (t_u8)retry_count;
	wiphy->max_scan_ie_len = MAX_IE_SIZE;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
	wiphy->mgmt_stypes = ieee80211_mgmt_stypes;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	wiphy->max_remain_on_channel_duration = MAX_REMAIN_ON_CHANNEL_DURATION;
#endif /* KERNEL_VERSION */

	/* Set RTS threshold to wiphy */
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_rts(priv, MLAN_ACT_GET, wait_option, &rts_thr)) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (rts_thr < MLAN_RTS_MIN_VALUE || rts_thr > MLAN_RTS_MAX_VALUE)
		rts_thr = MLAN_FRAG_RTS_DISABLED;
	wiphy->rts_threshold = (t_u32)rts_thr;

	/* Set fragment threshold to wiphy */
	if (MLAN_STATUS_SUCCESS !=
	    woal_set_get_frag(priv, MLAN_ACT_GET, wait_option, &frag_thr)) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (frag_thr < MLAN_RTS_MIN_VALUE || frag_thr > MLAN_RTS_MAX_VALUE)
		frag_thr = MLAN_FRAG_RTS_DISABLED;
	wiphy->frag_threshold = (t_u32)frag_thr;
#if CFG80211_VERSION_CODE > KERNEL_VERSION(3, 0, 0)
	/* Enable multi-channel by default if multi-channel is supported */
	if (cfg80211_iface_comb_ap_sta.num_different_channels > 1)
		enable = 1;
	ret = woal_mc_policy_cfg(priv, &enable, wait_option, MLAN_ACT_SET);
#endif

done:
	LEAVE();
	if (ret != MLAN_STATUS_PENDING)
		kfree(req);
	return ret;
}

/*
 * This function registers the device with CFG802.11 subsystem.
 *
 * @param priv       A pointer to moal_private
 *
 */
mlan_status
woal_register_cfg80211(moal_private *priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	struct wiphy *wiphy;
	void *wdev_priv = NULL;
	mlan_fw_info fw_info;
	char *country = NULL;
	int index = 0;

	ENTER();

	woal_request_get_fw_info(priv, MOAL_IOCTL_WAIT, &fw_info);

	wiphy = wiphy_new(&woal_cfg80211_ops, sizeof(moal_handle *));
	if (!wiphy) {
		PRINTM(MERROR, "Could not allocate wiphy device\n");
		ret = MLAN_STATUS_FAILURE;
		goto err_wiphy;
	}
#ifdef CONFIG_PM
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	if (fw_info.fw_supplicant_support)
		wiphy->wowlan = &wowlan_support_with_gtk;
	else
		wiphy->wowlan = &wowlan_support;
#else
	wiphy->wowlan.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_MAGIC_PKT;
	if (fw_info.fw_supplicant_support) {
		wiphy->wowlan.flags |=
			WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
			WIPHY_WOWLAN_GTK_REKEY_FAILURE;
	}
	wiphy->wowlan.n_patterns = MAX_NUM_FILTERS;
	wiphy->wowlan.pattern_min_len = 1;
	wiphy->wowlan.pattern_max_len = WOWLAN_MAX_PATTERN_LEN;
	wiphy->wowlan.max_pkt_offset = WOWLAN_MAX_OFFSET_LEN;
#endif
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	wiphy->coalesce = &coalesce_support;
#endif
	wiphy->max_scan_ssids = MRVDRV_MAX_SSID_LIST_LENGTH;
	wiphy->max_scan_ie_len = MAX_IE_SIZE;
	wiphy->interface_modes = 0;
	wiphy->interface_modes =
		MBIT(NL80211_IFTYPE_STATION) | MBIT(NL80211_IFTYPE_ADHOC) |
		MBIT(NL80211_IFTYPE_AP);
	wiphy->interface_modes |= MBIT(NL80211_IFTYPE_MONITOR);

#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	wiphy->interface_modes |= MBIT(NL80211_IFTYPE_P2P_GO) |
		MBIT(NL80211_IFTYPE_P2P_CLIENT);
#endif
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	woal_register_cfg80211_vendor_command(wiphy);
#endif
	/* Make this wiphy known to this driver only */
	wiphy->privid = mrvl_wiphy_privid;

	if (!fw_info.fw_bands)
		fw_info.fw_bands = BAND_B | BAND_G;
	if (fw_info.fw_bands & BAND_A) {
		wiphy->bands[IEEE80211_BAND_5GHZ] = &cfg80211_band_5ghz;
		priv->phandle->band = IEEE80211_BAND_5GHZ;
	}
	/* Supported bands */
	if (fw_info.fw_bands & (BAND_B | BAND_G | BAND_GN)) {
		wiphy->bands[IEEE80211_BAND_2GHZ] = &cfg80211_band_2ghz;
		/* If 2.4G enable, it will overwrite default to 2.4G */
		priv->phandle->band = IEEE80211_BAND_2GHZ;
	}

	if (fw_info.fw_bands & BAND_A) {
	/** reduce scan time from 110ms to 80ms */
		woal_set_scan_time(priv, INIT_ACTIVE_SCAN_CHAN_TIME,
				   INIT_PASSIVE_SCAN_CHAN_TIME,
				   INIT_SPECIFIC_SCAN_CHAN_TIME);
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		cfg80211_iface_comb_ap_sta.radar_detect_widths |=
			MBIT(NL80211_CHAN_WIDTH_40);
#endif
	} else
		woal_set_scan_time(priv, ACTIVE_SCAN_CHAN_TIME,
				   PASSIVE_SCAN_CHAN_TIME,
				   SPECIFIC_SCAN_CHAN_TIME);

	/* Initialize cipher suits */
	wiphy->cipher_suites = cfg80211_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(cfg80211_cipher_suites);
#ifdef UAP_CFG80211
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	wiphy->max_acl_mac_addrs = MAX_MAC_FILTER_NUM;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
	if (fw_info.max_ap_assoc_sta)
		wiphy->max_ap_assoc_sta = fw_info.max_ap_assoc_sta;
#endif
#endif

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	if (cfg80211_drcs) {
		cfg80211_iface_comb_ap_sta.num_different_channels = 2;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
		cfg80211_iface_comb_ap_sta.radar_detect_widths = 0;
#endif
	}
	/* Initialize interface combinations */
	wiphy->iface_combinations = &cfg80211_iface_comb_ap_sta;
	wiphy->n_iface_combinations = 1;
#endif

	memcpy(wiphy->perm_addr, priv->current_addr, ETH_ALEN);
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->flags = 0;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
	wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	wiphy->flags |=
		WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL | WIPHY_FLAG_OFFCHAN_TX;
	wiphy->flags |=
		WIPHY_FLAG_HAVE_AP_SME | WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
	wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
#endif
#ifdef ANDROID_KERNEL
	wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 2, 0)
#if CFG80211_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
	wiphy->flags |= WIPHY_FLAG_SUPPORTS_SCHED_SCAN;
#else
	wiphy->max_sched_scan_reqs = 1;
#endif
	wiphy->max_sched_scan_ssids = MRVDRV_MAX_SSID_LIST_LENGTH;
	wiphy->max_sched_scan_ie_len = MAX_IE_SIZE;
	wiphy->max_match_sets = MRVDRV_MAX_SSID_LIST_LENGTH;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	wiphy->max_sched_scan_plans = 1;
#endif
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,2,0)
	wiphy->flags |=
		WIPHY_FLAG_SUPPORTS_TDLS | WIPHY_FLAG_TDLS_EXTERNAL_SETUP;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,19,0)
	wiphy->features |= NL80211_FEATURE_TDLS_CHANNEL_SWITCH;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3,4,0)
	wiphy->features |= NL80211_FEATURE_HT_IBSS;
#endif
	wiphy->reg_notifier = woal_cfg80211_reg_notifier;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH;
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	/* Indicate to cfg80211 that the driver can support
	 * CSA and ESCA,i.e., both types of channel switch
	 * Applications like hostapd 2.6 will append CSA IE
	 * and ECSA IE and expect the driver to advertise 2
	 * in max_num_csa_counters to successfully issue a
	 * channel switch
	 */
	wiphy->max_num_csa_counters = MAX_CSA_COUNTERS_NUM;
#endif
	wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);
#endif
	/* Set struct moal_handle pointer in wiphy_priv */
	wdev_priv = wiphy_priv(wiphy);
	*(unsigned long *)wdev_priv = (unsigned long)priv->phandle;

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(2, 6, 39)
	set_wiphy_dev(wiphy, (struct device *)priv->phandle->hotplug_device);
#endif
	/* Set phy name */
	for (index = 0; index < MAX_MLAN_ADAPTER; index++) {
		if (m_handle[index] == priv->phandle) {
			dev_set_name(&wiphy->dev, mwiphy_name, index);
			break;
		}
	}

#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (beacon_hints) {
		/* REGULATORY_DISABLE_BEACON_HINTS: NO-IR flag won't be removed on chn where an AP is visible! */
		wiphy->regulatory_flags |= REGULATORY_DISABLE_BEACON_HINTS;
	}
	if (country_ie_ignore) {
		PRINTM(MIOCTL, "Don't follow countryIE provided by AP.\n");
		wiphy->regulatory_flags |= REGULATORY_COUNTRY_IE_IGNORE;
	} else {
		PRINTM(MIOCTL, "Follow countryIE provided by AP.\n");
	}
#endif
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	if (fw_region) {
		priv->phandle->regd = woal_create_custom_regdomain(priv);
		if (priv->phandle->regd) {
			wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
				REGULATORY_DISABLE_BEACON_HINTS |
				REGULATORY_COUNTRY_IE_IGNORE;
			wiphy_apply_custom_regulatory(wiphy,
						      priv->phandle->regd);
		} else {
			PRINTM(MERROR,
			       "creating custom regulatory domain failed\n");
		}
	}
#endif
	if (reg_alpha2 && !strncmp(reg_alpha2, "99", strlen("99"))) {
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
		wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
			REGULATORY_DISABLE_BEACON_HINTS |
			REGULATORY_COUNTRY_IE_IGNORE;
#else
		wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
#endif
		wiphy_apply_custom_regulatory(wiphy, &mrvl_regdom);
	}
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	if (woal_request_extcap(priv,
				(t_u8 *)&priv->extended_capabilities,
				sizeof(priv->extended_capabilities)) < 0)
		PRINTM(MERROR,
		       "Failed to get driver extended capability, use default\n");
	DBG_HEXDUMP(MCMD_D, "wiphy ext cap",
		    (t_u8 *)&priv->extended_capabilities,
		    sizeof(priv->extended_capabilities));
	wiphy->extended_capabilities = (t_u8 *)&priv->extended_capabilities;
	wiphy->extended_capabilities_mask =
		(t_u8 *)&priv->extended_capabilities;
	wiphy->extended_capabilities_len = sizeof(priv->extended_capabilities);
#endif
	if (wiphy_register(wiphy) < 0) {
		PRINTM(MERROR, "Wiphy device registration failed!\n");
		ret = MLAN_STATUS_FAILURE;
		goto err_wiphy;
	}
#if defined(WIFI_DIRECT_SUPPORT)
#if CFG80211_VERSION_CODE >= WIFI_DIRECT_KERNEL_VERSION
	if (!p2p_enh)
		wiphy->interface_modes &= ~(MBIT(NL80211_IFTYPE_P2P_GO) |
					    MBIT(NL80211_IFTYPE_P2P_CLIENT));
#endif
#endif
	wiphy->interface_modes &= ~(MBIT(NL80211_IFTYPE_MONITOR));

	if ((!reg_alpha2 || strncmp(reg_alpha2, "99", strlen("99")))
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
	    && !priv->phandle->regd
#endif
		) {
	/** we will try driver parameter first */
		if (reg_alpha2 && woal_is_valid_alpha2(reg_alpha2)) {
			PRINTM(MIOCTL, "Notify reg_alpha2 %c%c\n",
			       reg_alpha2[0], reg_alpha2[1]);
			regulatory_hint(wiphy, reg_alpha2);
		} else {
			country = region_code_2_string(fw_info.region_code);
			if (country) {
				if (fw_info.region_code != 0) {
					PRINTM(MIOCTL,
					       "Notify hw region code=%d %c%c\n",
					       fw_info.region_code, country[0],
					       country[1]);
					regulatory_hint(wiphy, country);
				}
			} else
				PRINTM(MERROR,
				       "hw region code=%d not supported\n",
				       fw_info.region_code);
		}
	}
	priv->phandle->wiphy = wiphy;
	woal_cfg80211_init_wiphy(priv, MOAL_IOCTL_WAIT);

	return ret;
err_wiphy:
	if (wiphy)
		wiphy_free(wiphy);
	LEAVE();
	return ret;
}

module_param(cfg80211_drcs, int, 0);
MODULE_PARM_DESC(cfg80211_drcs,
		 "1: Enable DRCS support; 0: Disable DRCS support");
module_param(reg_alpha2, charp, 0660);
MODULE_PARM_DESC(reg_alpha2, "Regulatory alpha2");
#if CFG80211_VERSION_CODE >= KERNEL_VERSION(3, 14, 0)
module_param(country_ie_ignore, int, 0);
MODULE_PARM_DESC(country_ie_ignore,
		 "0: Follow countryIE from AP and beacon hint enable; 1: Ignore countryIE from AP and beacon hint disable");
module_param(beacon_hints, int, 0);
MODULE_PARM_DESC(beacon_hints,
		 "0: enable beacon hints(default); 1: disable beacon hints");
#endif
