/*
 * IEEE 802.11 Common routines
 * Copyright (c) 2002-2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_11_COMMON_H
#define IEEE802_11_COMMON_H

#include "defs.h"

struct hostapd_hw_modes;

#define MAX_NOF_MB_IES_SUPPORTED 5

struct mb_ies_info {
	struct {
		const u8 *ie;
		u8 ie_len;
	} ies[MAX_NOF_MB_IES_SUPPORTED];
	u8 nof_ies;
};

/* Parsed Information Elements */
struct ieee802_11_elems {
	const u8 *ssid;
	const u8 *supp_rates;
	const u8 *ds_params;
	const u8 *challenge;
	const u8 *erp_info;
	const u8 *ext_supp_rates;
	const u8 *wpa_ie;
	const u8 *rsn_ie;
	const u8 *wmm; /* WMM Information or Parameter Element */
	const u8 *wmm_tspec;
	const u8 *wps_ie;
	const u8 *supp_channels;
	const u8 *mdie;
	const u8 *ftie;
	const u8 *timeout_int;
	const u8 *ht_capabilities;
	const u8 *ht_operation;
	const u8 *mesh_config;
	const u8 *mesh_id;
	const u8 *peer_mgmt;
	const u8 *vht_capabilities;
	const u8 *vht_operation;
	const u8 *vht_opmode_notif;
	const u8 *vendor_ht_cap;
	const u8 *vendor_vht;
	const u8 *p2p;
	const u8 *wfd;
	const u8 *link_id;
	const u8 *interworking;
	const u8 *qos_map_set;
	const u8 *hs20;
	const u8 *ext_capab;
	const u8 *bss_max_idle_period;
	const u8 *ssid_list;
	const u8 *osen;
	const u8 *mbo;
	const u8 *ampe;
	const u8 *mic;
	const u8 *pref_freq_list;
	const u8 *supp_op_classes;
	const u8 *rrm_enabled;
	const u8 *cag_number;
	const u8 *ap_csn;
	const u8 *fils_indic;
	const u8 *dils;
	const u8 *assoc_delay_info;
	const u8 *fils_req_params;
	const u8 *fils_key_confirm;
	const u8 *fils_session;
	const u8 *fils_hlp;
	const u8 *fils_ip_addr_assign;
	const u8 *key_delivery;
	const u8 *fils_wrapped_data;
	const u8 *fils_pk;
	const u8 *fils_nonce;
	const u8 *owe_dh;
	const u8 *power_capab;
	const u8 *roaming_cons_sel;
	const u8 *password_id;

	u8 ssid_len;
	u8 supp_rates_len;
	u8 challenge_len;
	u8 ext_supp_rates_len;
	u8 wpa_ie_len;
	u8 rsn_ie_len;
	u8 wmm_len; /* 7 = WMM Information; 24 = WMM Parameter */
	u8 wmm_tspec_len;
	u8 wps_ie_len;
	u8 supp_channels_len;
	u8 mdie_len;
	u8 ftie_len;
	u8 mesh_config_len;
	u8 mesh_id_len;
	u8 peer_mgmt_len;
	u8 vendor_ht_cap_len;
	u8 vendor_vht_len;
	u8 p2p_len;
	u8 wfd_len;
	u8 interworking_len;
	u8 qos_map_set_len;
	u8 hs20_len;
	u8 ext_capab_len;
	u8 ssid_list_len;
	u8 osen_len;
	u8 mbo_len;
	u8 ampe_len;
	u8 mic_len;
	u8 pref_freq_list_len;
	u8 supp_op_classes_len;
	u8 rrm_enabled_len;
	u8 cag_number_len;
	u8 fils_indic_len;
	u8 dils_len;
	u8 fils_req_params_len;
	u8 fils_key_confirm_len;
	u8 fils_hlp_len;
	u8 fils_ip_addr_assign_len;
	u8 key_delivery_len;
	u8 fils_wrapped_data_len;
	u8 fils_pk_len;
	u8 owe_dh_len;
	u8 power_capab_len;
	u8 roaming_cons_sel_len;
	u8 password_id_len;

	struct mb_ies_info mb_ies;
};

typedef enum { ParseOK = 0, ParseUnknown = 1, ParseFailed = -1 } ParseRes;

ParseRes ieee802_11_parse_elems(const u8 *start, size_t len,
				struct ieee802_11_elems *elems,
				int show_errors);
int ieee802_11_ie_count(const u8 *ies, size_t ies_len);
struct wpabuf * ieee802_11_vendor_ie_concat(const u8 *ies, size_t ies_len,
					    u32 oui_type);
struct ieee80211_hdr;
const u8 * get_hdr_bssid(const struct ieee80211_hdr *hdr, size_t len);

struct hostapd_wmm_ac_params {
	int cwmin;
	int cwmax;
	int aifs;
	int txop_limit; /* in units of 32us */
	int admission_control_mandatory;
};

int hostapd_config_wmm_ac(struct hostapd_wmm_ac_params wmm_ac_params[],
			  const char *name, const char *val);
enum hostapd_hw_mode ieee80211_freq_to_chan(int freq, u8 *channel);
int ieee80211_chan_to_freq(const char *country, u8 op_class, u8 chan);
enum hostapd_hw_mode ieee80211_freq_to_channel_ext(unsigned int freq,
						   int sec_channel, int vht,
						   u8 *op_class, u8 *channel);
int ieee80211_is_dfs(int freq, const struct hostapd_hw_modes *modes,
		     u16 num_modes);
enum phy_type ieee80211_get_phy_type(int freq, int ht, int vht);

int supp_rates_11b_only(struct ieee802_11_elems *elems);
int mb_ies_info_by_ies(struct mb_ies_info *info, const u8 *ies_buf,
		       size_t ies_len);
struct wpabuf * mb_ies_by_info(struct mb_ies_info *info);

const char * fc2str(u16 fc);

struct oper_class_map {
	enum hostapd_hw_mode mode;
	u8 op_class;
	u8 min_chan;
	u8 max_chan;
	u8 inc;
	enum { BW20, BW40PLUS, BW40MINUS, BW80, BW2160, BW160, BW80P80 } bw;
	enum { P2P_SUPP, NO_P2P_SUPP } p2p;
};

extern const struct oper_class_map global_op_class[];
extern size_t global_op_class_size;

const u8 * get_ie(const u8 *ies, size_t len, u8 eid);
const u8 * get_ie_ext(const u8 *ies, size_t len, u8 ext);

size_t mbo_add_ie(u8 *buf, size_t len, const u8 *attr, size_t attr_len);

struct country_op_class {
	u8 country_op_class;
	u8 global_op_class;
};

u8 country_to_global_op_class(const char *country, u8 op_class);

const struct oper_class_map * get_oper_class(const char *country, u8 op_class);

int ieee802_11_parse_candidate_list(const char *pos, u8 *nei_rep,
				    size_t nei_rep_len);

#endif /* IEEE802_11_COMMON_H */
