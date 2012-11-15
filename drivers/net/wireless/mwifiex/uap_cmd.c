/*
 * Marvell Wireless LAN device driver: AP specific command handling
 *
 * Copyright (C) 2012, Marvell International Ltd.
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
 */

#include "main.h"

/* This function parses security related parameters from cfg80211_ap_settings
 * and sets into FW understandable bss_config structure.
 */
int mwifiex_set_secure_params(struct mwifiex_private *priv,
			      struct mwifiex_uap_bss_param *bss_config,
			      struct cfg80211_ap_settings *params) {
	int i;
	struct mwifiex_wep_key wep_key;

	if (!params->privacy) {
		bss_config->protocol = PROTOCOL_NO_SECURITY;
		bss_config->key_mgmt = KEY_MGMT_NONE;
		bss_config->wpa_cfg.length = 0;
		priv->sec_info.wep_enabled = 0;
		priv->sec_info.wpa_enabled = 0;
		priv->sec_info.wpa2_enabled = 0;

		return 0;
	}

	switch (params->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		bss_config->auth_mode = WLAN_AUTH_OPEN;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		bss_config->auth_mode = WLAN_AUTH_SHARED_KEY;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		bss_config->auth_mode = WLAN_AUTH_LEAP;
		break;
	default:
		bss_config->auth_mode = MWIFIEX_AUTH_MODE_AUTO;
		break;
	}

	bss_config->key_mgmt_operation |= KEY_MGMT_ON_HOST;

	for (i = 0; i < params->crypto.n_akm_suites; i++) {
		switch (params->crypto.akm_suites[i]) {
		case WLAN_AKM_SUITE_8021X:
			if (params->crypto.wpa_versions &
			    NL80211_WPA_VERSION_1) {
				bss_config->protocol = PROTOCOL_WPA;
				bss_config->key_mgmt = KEY_MGMT_EAP;
			}
			if (params->crypto.wpa_versions &
			    NL80211_WPA_VERSION_2) {
				bss_config->protocol |= PROTOCOL_WPA2;
				bss_config->key_mgmt = KEY_MGMT_EAP;
			}
			break;
		case WLAN_AKM_SUITE_PSK:
			if (params->crypto.wpa_versions &
			    NL80211_WPA_VERSION_1) {
				bss_config->protocol = PROTOCOL_WPA;
				bss_config->key_mgmt = KEY_MGMT_PSK;
			}
			if (params->crypto.wpa_versions &
			    NL80211_WPA_VERSION_2) {
				bss_config->protocol |= PROTOCOL_WPA2;
				bss_config->key_mgmt = KEY_MGMT_PSK;
			}
			break;
		default:
			break;
		}
	}
	for (i = 0; i < params->crypto.n_ciphers_pairwise; i++) {
		switch (params->crypto.ciphers_pairwise[i]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_1)
				bss_config->wpa_cfg.pairwise_cipher_wpa |=
								CIPHER_TKIP;
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_2)
				bss_config->wpa_cfg.pairwise_cipher_wpa2 |=
								CIPHER_TKIP;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_1)
				bss_config->wpa_cfg.pairwise_cipher_wpa |=
								CIPHER_AES_CCMP;
			if (params->crypto.wpa_versions & NL80211_WPA_VERSION_2)
				bss_config->wpa_cfg.pairwise_cipher_wpa2 |=
								CIPHER_AES_CCMP;
		default:
			break;
		}
	}

	switch (params->crypto.cipher_group) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		if (priv->sec_info.wep_enabled) {
			bss_config->protocol = PROTOCOL_STATIC_WEP;
			bss_config->key_mgmt = KEY_MGMT_NONE;
			bss_config->wpa_cfg.length = 0;

			for (i = 0; i < NUM_WEP_KEYS; i++) {
				wep_key = priv->wep_key[i];
				bss_config->wep_cfg[i].key_index = i;

				if (priv->wep_key_curr_index == i)
					bss_config->wep_cfg[i].is_default = 1;
				else
					bss_config->wep_cfg[i].is_default = 0;

				bss_config->wep_cfg[i].length =
							     wep_key.key_length;
				memcpy(&bss_config->wep_cfg[i].key,
				       &wep_key.key_material,
				       wep_key.key_length);
			}
		}
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		bss_config->wpa_cfg.group_cipher = CIPHER_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		bss_config->wpa_cfg.group_cipher = CIPHER_AES_CCMP;
		break;
	default:
		break;
	}

	return 0;
}

/* This function updates 11n related parameters from IE and sets them into
 * bss_config structure.
 */
void
mwifiex_set_ht_params(struct mwifiex_private *priv,
		      struct mwifiex_uap_bss_param *bss_cfg,
		      struct cfg80211_ap_settings *params)
{
	const u8 *ht_ie;

	if (!ISSUPP_11NENABLED(priv->adapter->fw_cap_info))
		return;

	ht_ie = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY, params->beacon.tail,
				 params->beacon.tail_len);
	if (ht_ie) {
		memcpy(&bss_cfg->ht_cap, ht_ie + 2,
		       sizeof(struct ieee80211_ht_cap));
		priv->ap_11n_enabled = 1;
	} else {
		memset(&bss_cfg->ht_cap , 0, sizeof(struct ieee80211_ht_cap));
		bss_cfg->ht_cap.cap_info = cpu_to_le16(MWIFIEX_DEF_HT_CAP);
		bss_cfg->ht_cap.ampdu_params_info = MWIFIEX_DEF_AMPDU;
	}

	return;
}

/* This function finds supported rates IE from beacon parameter and sets
 * these rates into bss_config structure.
 */
void
mwifiex_set_uap_rates(struct mwifiex_uap_bss_param *bss_cfg,
		      struct cfg80211_ap_settings *params)
{
	struct ieee_types_header *rate_ie;
	int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const u8 *var_pos = params->beacon.head + var_offset;
	int len = params->beacon.head_len - var_offset;
	u8 rate_len = 0;

	rate_ie = (void *)cfg80211_find_ie(WLAN_EID_SUPP_RATES, var_pos, len);
	if (rate_ie) {
		memcpy(bss_cfg->rates, rate_ie + 1, rate_ie->len);
		rate_len = rate_ie->len;
	}

	rate_ie = (void *)cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES,
					   params->beacon.tail,
					   params->beacon.tail_len);
	if (rate_ie)
		memcpy(bss_cfg->rates + rate_len, rate_ie + 1, rate_ie->len);

	return;
}

/* This function initializes some of mwifiex_uap_bss_param variables.
 * This helps FW in ignoring invalid values. These values may or may not
 * be get updated to valid ones at later stage.
 */
void mwifiex_set_sys_config_invalid_data(struct mwifiex_uap_bss_param *config)
{
	config->bcast_ssid_ctl = 0x7F;
	config->radio_ctl = 0x7F;
	config->dtim_period = 0x7F;
	config->beacon_period = 0x7FFF;
	config->auth_mode = 0x7F;
	config->rts_threshold = 0x7FFF;
	config->frag_threshold = 0x7FFF;
	config->retry_limit = 0x7F;
}

/* This function parses BSS related parameters from structure
 * and prepares TLVs specific to WPA/WPA2 security.
 * These TLVs are appended to command buffer.
 */
static void
mwifiex_uap_bss_wpa(u8 **tlv_buf, void *cmd_buf, u16 *param_size)
{
	struct host_cmd_tlv_pwk_cipher *pwk_cipher;
	struct host_cmd_tlv_gwk_cipher *gwk_cipher;
	struct host_cmd_tlv_passphrase *passphrase;
	struct host_cmd_tlv_akmp *tlv_akmp;
	struct mwifiex_uap_bss_param *bss_cfg = cmd_buf;
	u16 cmd_size = *param_size;
	u8 *tlv = *tlv_buf;

	tlv_akmp = (struct host_cmd_tlv_akmp *)tlv;
	tlv_akmp->tlv.type = cpu_to_le16(TLV_TYPE_UAP_AKMP);
	tlv_akmp->tlv.len = cpu_to_le16(sizeof(struct host_cmd_tlv_akmp) -
					sizeof(struct host_cmd_tlv));
	tlv_akmp->key_mgmt_operation = cpu_to_le16(bss_cfg->key_mgmt_operation);
	tlv_akmp->key_mgmt = cpu_to_le16(bss_cfg->key_mgmt);
	cmd_size += sizeof(struct host_cmd_tlv_akmp);
	tlv += sizeof(struct host_cmd_tlv_akmp);

	if (bss_cfg->wpa_cfg.pairwise_cipher_wpa & VALID_CIPHER_BITMAP) {
		pwk_cipher = (struct host_cmd_tlv_pwk_cipher *)tlv;
		pwk_cipher->tlv.type = cpu_to_le16(TLV_TYPE_PWK_CIPHER);
		pwk_cipher->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_pwk_cipher) -
				    sizeof(struct host_cmd_tlv));
		pwk_cipher->proto = cpu_to_le16(PROTOCOL_WPA);
		pwk_cipher->cipher = bss_cfg->wpa_cfg.pairwise_cipher_wpa;
		cmd_size += sizeof(struct host_cmd_tlv_pwk_cipher);
		tlv += sizeof(struct host_cmd_tlv_pwk_cipher);
	}

	if (bss_cfg->wpa_cfg.pairwise_cipher_wpa2 & VALID_CIPHER_BITMAP) {
		pwk_cipher = (struct host_cmd_tlv_pwk_cipher *)tlv;
		pwk_cipher->tlv.type = cpu_to_le16(TLV_TYPE_PWK_CIPHER);
		pwk_cipher->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_pwk_cipher) -
				    sizeof(struct host_cmd_tlv));
		pwk_cipher->proto = cpu_to_le16(PROTOCOL_WPA2);
		pwk_cipher->cipher = bss_cfg->wpa_cfg.pairwise_cipher_wpa2;
		cmd_size += sizeof(struct host_cmd_tlv_pwk_cipher);
		tlv += sizeof(struct host_cmd_tlv_pwk_cipher);
	}

	if (bss_cfg->wpa_cfg.group_cipher & VALID_CIPHER_BITMAP) {
		gwk_cipher = (struct host_cmd_tlv_gwk_cipher *)tlv;
		gwk_cipher->tlv.type = cpu_to_le16(TLV_TYPE_GWK_CIPHER);
		gwk_cipher->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_gwk_cipher) -
				    sizeof(struct host_cmd_tlv));
		gwk_cipher->cipher = bss_cfg->wpa_cfg.group_cipher;
		cmd_size += sizeof(struct host_cmd_tlv_gwk_cipher);
		tlv += sizeof(struct host_cmd_tlv_gwk_cipher);
	}

	if (bss_cfg->wpa_cfg.length) {
		passphrase = (struct host_cmd_tlv_passphrase *)tlv;
		passphrase->tlv.type = cpu_to_le16(TLV_TYPE_UAP_WPA_PASSPHRASE);
		passphrase->tlv.len = cpu_to_le16(bss_cfg->wpa_cfg.length);
		memcpy(passphrase->passphrase, bss_cfg->wpa_cfg.passphrase,
		       bss_cfg->wpa_cfg.length);
		cmd_size += sizeof(struct host_cmd_tlv) +
			    bss_cfg->wpa_cfg.length;
		tlv += sizeof(struct host_cmd_tlv) + bss_cfg->wpa_cfg.length;
	}

	*param_size = cmd_size;
	*tlv_buf = tlv;

	return;
}

/* This function parses BSS related parameters from structure
 * and prepares TLVs specific to WEP encryption.
 * These TLVs are appended to command buffer.
 */
static void
mwifiex_uap_bss_wep(u8 **tlv_buf, void *cmd_buf, u16 *param_size)
{
	struct host_cmd_tlv_wep_key *wep_key;
	u16 cmd_size = *param_size;
	int i;
	u8 *tlv = *tlv_buf;
	struct mwifiex_uap_bss_param *bss_cfg = cmd_buf;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (bss_cfg->wep_cfg[i].length &&
		    (bss_cfg->wep_cfg[i].length == WLAN_KEY_LEN_WEP40 ||
		     bss_cfg->wep_cfg[i].length == WLAN_KEY_LEN_WEP104)) {
			wep_key = (struct host_cmd_tlv_wep_key *)tlv;
			wep_key->tlv.type = cpu_to_le16(TLV_TYPE_UAP_WEP_KEY);
			wep_key->tlv.len =
				cpu_to_le16(bss_cfg->wep_cfg[i].length + 2);
			wep_key->key_index = bss_cfg->wep_cfg[i].key_index;
			wep_key->is_default = bss_cfg->wep_cfg[i].is_default;
			memcpy(wep_key->key, bss_cfg->wep_cfg[i].key,
			       bss_cfg->wep_cfg[i].length);
			cmd_size += sizeof(struct host_cmd_tlv) + 2 +
				    bss_cfg->wep_cfg[i].length;
			tlv += sizeof(struct host_cmd_tlv) + 2 +
				    bss_cfg->wep_cfg[i].length;
		}
	}

	*param_size = cmd_size;
	*tlv_buf = tlv;

	return;
}

/* This function parses BSS related parameters from structure
 * and prepares TLVs. These TLVs are appended to command buffer.
*/
static int
mwifiex_uap_bss_param_prepare(u8 *tlv, void *cmd_buf, u16 *param_size)
{
	struct host_cmd_tlv_dtim_period *dtim_period;
	struct host_cmd_tlv_beacon_period *beacon_period;
	struct host_cmd_tlv_ssid *ssid;
	struct host_cmd_tlv_bcast_ssid *bcast_ssid;
	struct host_cmd_tlv_channel_band *chan_band;
	struct host_cmd_tlv_frag_threshold *frag_threshold;
	struct host_cmd_tlv_rts_threshold *rts_threshold;
	struct host_cmd_tlv_retry_limit *retry_limit;
	struct host_cmd_tlv_encrypt_protocol *encrypt_protocol;
	struct host_cmd_tlv_auth_type *auth_type;
	struct host_cmd_tlv_rates *tlv_rates;
	struct host_cmd_tlv_ageout_timer *ao_timer, *ps_ao_timer;
	struct mwifiex_ie_types_htcap *htcap;
	struct mwifiex_uap_bss_param *bss_cfg = cmd_buf;
	int i;
	u16 cmd_size = *param_size;

	if (bss_cfg->ssid.ssid_len) {
		ssid = (struct host_cmd_tlv_ssid *)tlv;
		ssid->tlv.type = cpu_to_le16(TLV_TYPE_UAP_SSID);
		ssid->tlv.len = cpu_to_le16((u16)bss_cfg->ssid.ssid_len);
		memcpy(ssid->ssid, bss_cfg->ssid.ssid, bss_cfg->ssid.ssid_len);
		cmd_size += sizeof(struct host_cmd_tlv) +
			    bss_cfg->ssid.ssid_len;
		tlv += sizeof(struct host_cmd_tlv) + bss_cfg->ssid.ssid_len;

		bcast_ssid = (struct host_cmd_tlv_bcast_ssid *)tlv;
		bcast_ssid->tlv.type = cpu_to_le16(TLV_TYPE_UAP_BCAST_SSID);
		bcast_ssid->tlv.len =
				cpu_to_le16(sizeof(bcast_ssid->bcast_ctl));
		bcast_ssid->bcast_ctl = bss_cfg->bcast_ssid_ctl;
		cmd_size += sizeof(struct host_cmd_tlv_bcast_ssid);
		tlv += sizeof(struct host_cmd_tlv_bcast_ssid);
	}
	if (bss_cfg->rates[0]) {
		tlv_rates = (struct host_cmd_tlv_rates *)tlv;
		tlv_rates->tlv.type = cpu_to_le16(TLV_TYPE_UAP_RATES);

		for (i = 0; i < MWIFIEX_SUPPORTED_RATES && bss_cfg->rates[i];
		     i++)
			tlv_rates->rates[i] = bss_cfg->rates[i];

		tlv_rates->tlv.len = cpu_to_le16(i);
		cmd_size += sizeof(struct host_cmd_tlv_rates) + i;
		tlv += sizeof(struct host_cmd_tlv_rates) + i;
	}
	if (bss_cfg->channel &&
	    ((bss_cfg->band_cfg == BAND_CONFIG_BG &&
	      bss_cfg->channel <= MAX_CHANNEL_BAND_BG) ||
	    (bss_cfg->band_cfg == BAND_CONFIG_A &&
	     bss_cfg->channel <= MAX_CHANNEL_BAND_A))) {
		chan_band = (struct host_cmd_tlv_channel_band *)tlv;
		chan_band->tlv.type = cpu_to_le16(TLV_TYPE_CHANNELBANDLIST);
		chan_band->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_channel_band) -
				    sizeof(struct host_cmd_tlv));
		chan_band->band_config = bss_cfg->band_cfg;
		chan_band->channel = bss_cfg->channel;
		cmd_size += sizeof(struct host_cmd_tlv_channel_band);
		tlv += sizeof(struct host_cmd_tlv_channel_band);
	}
	if (bss_cfg->beacon_period >= MIN_BEACON_PERIOD &&
	    bss_cfg->beacon_period <= MAX_BEACON_PERIOD) {
		beacon_period = (struct host_cmd_tlv_beacon_period *)tlv;
		beacon_period->tlv.type =
					cpu_to_le16(TLV_TYPE_UAP_BEACON_PERIOD);
		beacon_period->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_beacon_period) -
				    sizeof(struct host_cmd_tlv));
		beacon_period->period = cpu_to_le16(bss_cfg->beacon_period);
		cmd_size += sizeof(struct host_cmd_tlv_beacon_period);
		tlv += sizeof(struct host_cmd_tlv_beacon_period);
	}
	if (bss_cfg->dtim_period >= MIN_DTIM_PERIOD &&
	    bss_cfg->dtim_period <= MAX_DTIM_PERIOD) {
		dtim_period = (struct host_cmd_tlv_dtim_period *)tlv;
		dtim_period->tlv.type = cpu_to_le16(TLV_TYPE_UAP_DTIM_PERIOD);
		dtim_period->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_dtim_period) -
				    sizeof(struct host_cmd_tlv));
		dtim_period->period = bss_cfg->dtim_period;
		cmd_size += sizeof(struct host_cmd_tlv_dtim_period);
		tlv += sizeof(struct host_cmd_tlv_dtim_period);
	}
	if (bss_cfg->rts_threshold <= MWIFIEX_RTS_MAX_VALUE) {
		rts_threshold = (struct host_cmd_tlv_rts_threshold *)tlv;
		rts_threshold->tlv.type =
					cpu_to_le16(TLV_TYPE_UAP_RTS_THRESHOLD);
		rts_threshold->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_rts_threshold) -
				    sizeof(struct host_cmd_tlv));
		rts_threshold->rts_thr = cpu_to_le16(bss_cfg->rts_threshold);
		cmd_size += sizeof(struct host_cmd_tlv_frag_threshold);
		tlv += sizeof(struct host_cmd_tlv_frag_threshold);
	}
	if ((bss_cfg->frag_threshold >= MWIFIEX_FRAG_MIN_VALUE) &&
	    (bss_cfg->frag_threshold <= MWIFIEX_FRAG_MAX_VALUE)) {
		frag_threshold = (struct host_cmd_tlv_frag_threshold *)tlv;
		frag_threshold->tlv.type =
				cpu_to_le16(TLV_TYPE_UAP_FRAG_THRESHOLD);
		frag_threshold->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_frag_threshold) -
				    sizeof(struct host_cmd_tlv));
		frag_threshold->frag_thr = cpu_to_le16(bss_cfg->frag_threshold);
		cmd_size += sizeof(struct host_cmd_tlv_frag_threshold);
		tlv += sizeof(struct host_cmd_tlv_frag_threshold);
	}
	if (bss_cfg->retry_limit <= MWIFIEX_RETRY_LIMIT) {
		retry_limit = (struct host_cmd_tlv_retry_limit *)tlv;
		retry_limit->tlv.type = cpu_to_le16(TLV_TYPE_UAP_RETRY_LIMIT);
		retry_limit->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_retry_limit) -
				    sizeof(struct host_cmd_tlv));
		retry_limit->limit = (u8)bss_cfg->retry_limit;
		cmd_size += sizeof(struct host_cmd_tlv_retry_limit);
		tlv += sizeof(struct host_cmd_tlv_retry_limit);
	}
	if ((bss_cfg->protocol & PROTOCOL_WPA) ||
	    (bss_cfg->protocol & PROTOCOL_WPA2) ||
	    (bss_cfg->protocol & PROTOCOL_EAP))
		mwifiex_uap_bss_wpa(&tlv, cmd_buf, &cmd_size);
	else
		mwifiex_uap_bss_wep(&tlv, cmd_buf, &cmd_size);

	if ((bss_cfg->auth_mode <= WLAN_AUTH_SHARED_KEY) ||
	    (bss_cfg->auth_mode == MWIFIEX_AUTH_MODE_AUTO)) {
		auth_type = (struct host_cmd_tlv_auth_type *)tlv;
		auth_type->tlv.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
		auth_type->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_auth_type) -
			sizeof(struct host_cmd_tlv));
		auth_type->auth_type = (u8)bss_cfg->auth_mode;
		cmd_size += sizeof(struct host_cmd_tlv_auth_type);
		tlv += sizeof(struct host_cmd_tlv_auth_type);
	}
	if (bss_cfg->protocol) {
		encrypt_protocol = (struct host_cmd_tlv_encrypt_protocol *)tlv;
		encrypt_protocol->tlv.type =
			cpu_to_le16(TLV_TYPE_UAP_ENCRY_PROTOCOL);
		encrypt_protocol->tlv.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_encrypt_protocol)
			- sizeof(struct host_cmd_tlv));
		encrypt_protocol->proto = cpu_to_le16(bss_cfg->protocol);
		cmd_size += sizeof(struct host_cmd_tlv_encrypt_protocol);
		tlv += sizeof(struct host_cmd_tlv_encrypt_protocol);
	}

	if (bss_cfg->ht_cap.cap_info) {
		htcap = (struct mwifiex_ie_types_htcap *)tlv;
		htcap->header.type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		htcap->header.len =
				cpu_to_le16(sizeof(struct ieee80211_ht_cap));
		htcap->ht_cap.cap_info = bss_cfg->ht_cap.cap_info;
		htcap->ht_cap.ampdu_params_info =
					     bss_cfg->ht_cap.ampdu_params_info;
		memcpy(&htcap->ht_cap.mcs, &bss_cfg->ht_cap.mcs,
		       sizeof(struct ieee80211_mcs_info));
		htcap->ht_cap.extended_ht_cap_info =
					bss_cfg->ht_cap.extended_ht_cap_info;
		htcap->ht_cap.tx_BF_cap_info = bss_cfg->ht_cap.tx_BF_cap_info;
		htcap->ht_cap.antenna_selection_info =
					bss_cfg->ht_cap.antenna_selection_info;
		cmd_size += sizeof(struct mwifiex_ie_types_htcap);
		tlv += sizeof(struct mwifiex_ie_types_htcap);
	}

	if (bss_cfg->sta_ao_timer) {
		ao_timer = (struct host_cmd_tlv_ageout_timer *)tlv;
		ao_timer->tlv.type = cpu_to_le16(TLV_TYPE_UAP_AO_TIMER);
		ao_timer->tlv.len = cpu_to_le16(sizeof(*ao_timer) -
						sizeof(struct host_cmd_tlv));
		ao_timer->sta_ao_timer = cpu_to_le32(bss_cfg->sta_ao_timer);
		cmd_size += sizeof(*ao_timer);
		tlv += sizeof(*ao_timer);
	}

	if (bss_cfg->ps_sta_ao_timer) {
		ps_ao_timer = (struct host_cmd_tlv_ageout_timer *)tlv;
		ps_ao_timer->tlv.type = cpu_to_le16(TLV_TYPE_UAP_PS_AO_TIMER);
		ps_ao_timer->tlv.len = cpu_to_le16(sizeof(*ps_ao_timer) -
						   sizeof(struct host_cmd_tlv));
		ps_ao_timer->sta_ao_timer =
					cpu_to_le32(bss_cfg->ps_sta_ao_timer);
		cmd_size += sizeof(*ps_ao_timer);
		tlv += sizeof(*ps_ao_timer);
	}

	*param_size = cmd_size;

	return 0;
}

/* This function parses custom IEs from IE list and prepares command buffer */
static int mwifiex_uap_custom_ie_prepare(u8 *tlv, void *cmd_buf, u16 *ie_size)
{
	struct mwifiex_ie_list *ap_ie = cmd_buf;
	struct host_cmd_tlv *tlv_ie = (struct host_cmd_tlv *)tlv;

	if (!ap_ie || !ap_ie->len || !ap_ie->ie_list)
		return -1;

	*ie_size += le16_to_cpu(ap_ie->len) + sizeof(struct host_cmd_tlv);

	tlv_ie->type = cpu_to_le16(TLV_TYPE_MGMT_IE);
	tlv_ie->len = ap_ie->len;
	tlv += sizeof(struct host_cmd_tlv);

	memcpy(tlv, ap_ie->ie_list, le16_to_cpu(ap_ie->len));

	return 0;
}

/* Parse AP config structure and prepare TLV based command structure
 * to be sent to FW for uAP configuration
 */
static int
mwifiex_cmd_uap_sys_config(struct host_cmd_ds_command *cmd, u16 cmd_action,
			   u32 type, void *cmd_buf)
{
	u8 *tlv;
	u16 cmd_size, param_size, ie_size;
	struct host_cmd_ds_sys_config *sys_cfg;

	cmd->command = cpu_to_le16(HostCmd_CMD_UAP_SYS_CONFIG);
	cmd_size = (u16)(sizeof(struct host_cmd_ds_sys_config) + S_DS_GEN);
	sys_cfg = (struct host_cmd_ds_sys_config *)&cmd->params.uap_sys_config;
	sys_cfg->action = cpu_to_le16(cmd_action);
	tlv = sys_cfg->tlv;

	switch (type) {
	case UAP_BSS_PARAMS_I:
		param_size = cmd_size;
		if (mwifiex_uap_bss_param_prepare(tlv, cmd_buf, &param_size))
			return -1;
		cmd->size = cpu_to_le16(param_size);
		break;
	case UAP_CUSTOM_IE_I:
		ie_size = cmd_size;
		if (mwifiex_uap_custom_ie_prepare(tlv, cmd_buf, &ie_size))
			return -1;
		cmd->size = cpu_to_le16(ie_size);
		break;
	default:
		return -1;
	}

	return 0;
}

/* This function prepares the AP specific commands before sending them
 * to the firmware.
 * This is a generic function which calls specific command preparation
 * routines based upon the command number.
 */
int mwifiex_uap_prepare_cmd(struct mwifiex_private *priv, u16 cmd_no,
			    u16 cmd_action, u32 type,
			    void *data_buf, void *cmd_buf)
{
	struct host_cmd_ds_command *cmd = cmd_buf;

	switch (cmd_no) {
	case HostCmd_CMD_UAP_SYS_CONFIG:
		if (mwifiex_cmd_uap_sys_config(cmd, cmd_action, type, data_buf))
			return -1;
		break;
	case HostCmd_CMD_UAP_BSS_START:
	case HostCmd_CMD_UAP_BSS_STOP:
		cmd->command = cpu_to_le16(cmd_no);
		cmd->size = cpu_to_le16(S_DS_GEN);
		break;
	default:
		dev_err(priv->adapter->dev,
			"PREP_CMD: unknown cmd %#x\n", cmd_no);
		return -1;
	}

	return 0;
}
