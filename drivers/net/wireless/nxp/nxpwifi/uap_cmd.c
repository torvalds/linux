// SPDX-License-Identifier: GPL-2.0-only
/*
 * NXP Wireless LAN device driver: AP specific command handling
 *
 * Copyright 2011-2024 NXP
 */

#include "main.h"
#include "cmdevt.h"
#include "11n.h"
#include "11ac.h"
#include "11ax.h"

/* Parse BSS params and append WPA/WPA2 TLVs to the command buffer. */
static void
nxpwifi_uap_bss_wpa(u8 **tlv_buf, void *cmd_buf, u16 *param_size)
{
	struct host_cmd_tlv_pwk_cipher *pwk_cipher;
	struct host_cmd_tlv_gwk_cipher *gwk_cipher;
	struct host_cmd_tlv_passphrase *passphrase;
	struct host_cmd_tlv_akmp *tlv_akmp;
	struct nxpwifi_uap_bss_param *bss_cfg = cmd_buf;
	u16 cmd_size = *param_size;
	u8 *tlv = *tlv_buf;

	tlv_akmp = (struct host_cmd_tlv_akmp *)tlv;
	tlv_akmp->header.type = cpu_to_le16(TLV_TYPE_UAP_AKMP);
	tlv_akmp->header.len = cpu_to_le16(sizeof(struct host_cmd_tlv_akmp) -
					sizeof(struct nxpwifi_ie_types_header));
	tlv_akmp->key_mgmt_operation = cpu_to_le16(bss_cfg->key_mgmt_operation);
	tlv_akmp->key_mgmt = cpu_to_le16(bss_cfg->key_mgmt);
	cmd_size += sizeof(struct host_cmd_tlv_akmp);
	tlv += sizeof(struct host_cmd_tlv_akmp);

	if (bss_cfg->wpa_cfg.pairwise_cipher_wpa & VALID_CIPHER_BITMAP) {
		pwk_cipher = (struct host_cmd_tlv_pwk_cipher *)tlv;
		pwk_cipher->header.type = cpu_to_le16(TLV_TYPE_PWK_CIPHER);
		pwk_cipher->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_pwk_cipher) -
				    sizeof(struct nxpwifi_ie_types_header));
		pwk_cipher->proto = cpu_to_le16(PROTOCOL_WPA);
		pwk_cipher->cipher = bss_cfg->wpa_cfg.pairwise_cipher_wpa;
		cmd_size += sizeof(struct host_cmd_tlv_pwk_cipher);
		tlv += sizeof(struct host_cmd_tlv_pwk_cipher);
	}

	if (bss_cfg->wpa_cfg.pairwise_cipher_wpa2 & VALID_CIPHER_BITMAP) {
		pwk_cipher = (struct host_cmd_tlv_pwk_cipher *)tlv;
		pwk_cipher->header.type = cpu_to_le16(TLV_TYPE_PWK_CIPHER);
		pwk_cipher->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_pwk_cipher) -
				    sizeof(struct nxpwifi_ie_types_header));
		pwk_cipher->proto = cpu_to_le16(PROTOCOL_WPA2);
		pwk_cipher->cipher = bss_cfg->wpa_cfg.pairwise_cipher_wpa2;
		cmd_size += sizeof(struct host_cmd_tlv_pwk_cipher);
		tlv += sizeof(struct host_cmd_tlv_pwk_cipher);
	}

	if (bss_cfg->wpa_cfg.group_cipher & VALID_CIPHER_BITMAP) {
		gwk_cipher = (struct host_cmd_tlv_gwk_cipher *)tlv;
		gwk_cipher->header.type = cpu_to_le16(TLV_TYPE_GWK_CIPHER);
		gwk_cipher->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_gwk_cipher) -
				    sizeof(struct nxpwifi_ie_types_header));
		gwk_cipher->cipher = bss_cfg->wpa_cfg.group_cipher;
		cmd_size += sizeof(struct host_cmd_tlv_gwk_cipher);
		tlv += sizeof(struct host_cmd_tlv_gwk_cipher);
	}

	if (bss_cfg->wpa_cfg.length) {
		passphrase = (struct host_cmd_tlv_passphrase *)tlv;
		passphrase->header.type =
				cpu_to_le16(TLV_TYPE_UAP_WPA_PASSPHRASE);
		passphrase->header.len = cpu_to_le16(bss_cfg->wpa_cfg.length);
		memcpy(passphrase->passphrase, bss_cfg->wpa_cfg.passphrase,
		       bss_cfg->wpa_cfg.length);
		cmd_size += sizeof(struct nxpwifi_ie_types_header) +
			    bss_cfg->wpa_cfg.length;
		tlv += sizeof(struct nxpwifi_ie_types_header) +
				bss_cfg->wpa_cfg.length;
	}

	*param_size = cmd_size;
	*tlv_buf = tlv;
}

/* Parse BSS params and append WEP TLVs to the command buffer. */
static void
nxpwifi_uap_bss_wep(u8 **tlv_buf, void *cmd_buf, u16 *param_size)
{
	struct host_cmd_tlv_wep_key *wep_key;
	u16 cmd_size = *param_size;
	int i;
	u8 *tlv = *tlv_buf;
	struct nxpwifi_uap_bss_param *bss_cfg = cmd_buf;

	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if (bss_cfg->wep_cfg[i].length &&
		    (bss_cfg->wep_cfg[i].length == WLAN_KEY_LEN_WEP40 ||
		     bss_cfg->wep_cfg[i].length == WLAN_KEY_LEN_WEP104)) {
			wep_key = (struct host_cmd_tlv_wep_key *)tlv;
			wep_key->header.type =
				cpu_to_le16(TLV_TYPE_UAP_WEP_KEY);
			wep_key->header.len =
				cpu_to_le16(bss_cfg->wep_cfg[i].length + 2);
			wep_key->key_index = bss_cfg->wep_cfg[i].key_index;
			wep_key->is_default = bss_cfg->wep_cfg[i].is_default;
			memcpy(wep_key->key, bss_cfg->wep_cfg[i].key,
			       bss_cfg->wep_cfg[i].length);
			cmd_size += sizeof(struct nxpwifi_ie_types_header) + 2 +
				    bss_cfg->wep_cfg[i].length;
			tlv += sizeof(struct nxpwifi_ie_types_header) + 2 +
				    bss_cfg->wep_cfg[i].length;
		}
	}

	*param_size = cmd_size;
	*tlv_buf = tlv;
}

/* Parse BSS params and append TLVs to the command buffer. */
static int nxpwifi_uap_bss_param_prepare(struct nxpwifi_private *priv, u8 *tlv,
					 void *cmd_buf, u16 *param_size)
{
	struct host_cmd_tlv_mac_addr *mac_tlv;
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
	struct host_cmd_tlv_power_constraint *pwr_ct;
	struct nxpwifi_ie_types_htcap *htcap;
	struct nxpwifi_uap_bss_param *bss_cfg = cmd_buf;
	int i;
	u16 cmd_size = *param_size;

	mac_tlv = (struct host_cmd_tlv_mac_addr *)tlv;
	mac_tlv->header.type = cpu_to_le16(TLV_TYPE_UAP_MAC_ADDRESS);
	mac_tlv->header.len = cpu_to_le16(ETH_ALEN);
	memcpy(mac_tlv->mac_addr, bss_cfg->mac_addr, ETH_ALEN);
	cmd_size += sizeof(struct host_cmd_tlv_mac_addr);
	tlv += sizeof(struct host_cmd_tlv_mac_addr);

	if (bss_cfg->ssid.ssid_len) {
		ssid = (struct host_cmd_tlv_ssid *)tlv;
		ssid->header.type = cpu_to_le16(TLV_TYPE_UAP_SSID);
		ssid->header.len = cpu_to_le16((u16)bss_cfg->ssid.ssid_len);
		memcpy(ssid->ssid, bss_cfg->ssid.ssid, bss_cfg->ssid.ssid_len);
		cmd_size += sizeof(struct nxpwifi_ie_types_header) +
			    bss_cfg->ssid.ssid_len;
		tlv += sizeof(struct nxpwifi_ie_types_header) +
				bss_cfg->ssid.ssid_len;

		bcast_ssid = (struct host_cmd_tlv_bcast_ssid *)tlv;
		bcast_ssid->header.type = cpu_to_le16(TLV_TYPE_UAP_BCAST_SSID);
		bcast_ssid->header.len =
				cpu_to_le16(sizeof(bcast_ssid->bcast_ctl));
		bcast_ssid->bcast_ctl = bss_cfg->bcast_ssid_ctl;
		cmd_size += sizeof(struct host_cmd_tlv_bcast_ssid);
		tlv += sizeof(struct host_cmd_tlv_bcast_ssid);
	}
	if (bss_cfg->rates[0]) {
		tlv_rates = (struct host_cmd_tlv_rates *)tlv;
		tlv_rates->header.type = cpu_to_le16(TLV_TYPE_UAP_RATES);

		for (i = 0; i < NXPWIFI_SUPPORTED_RATES && bss_cfg->rates[i];
		     i++)
			tlv_rates->rates[i] = bss_cfg->rates[i];

		tlv_rates->header.len = cpu_to_le16(i);
		cmd_size += sizeof(struct host_cmd_tlv_rates) + i;
		tlv += sizeof(struct host_cmd_tlv_rates) + i;
	}
	if (bss_cfg->channel &&
	    (((bss_cfg->band_cfg & BIT(0)) == BAND_CONFIG_BG &&
	      bss_cfg->channel <= MAX_CHANNEL_BAND_BG) ||
	    ((bss_cfg->band_cfg & BIT(0)) == BAND_CONFIG_A &&
	     bss_cfg->channel <= MAX_CHANNEL_BAND_A))) {
		chan_band = (struct host_cmd_tlv_channel_band *)tlv;
		chan_band->header.type = cpu_to_le16(TLV_TYPE_CHANNELBANDLIST);
		chan_band->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_channel_band) -
				    sizeof(struct nxpwifi_ie_types_header));
		chan_band->band_config = bss_cfg->band_cfg;
		chan_band->channel = bss_cfg->channel;
		cmd_size += sizeof(struct host_cmd_tlv_channel_band);
		tlv += sizeof(struct host_cmd_tlv_channel_band);
	}
	if (bss_cfg->beacon_period >= NXPWIFI_BEACON_PERIOD_MIN &&
	    bss_cfg->beacon_period <= NXPWIFI_BEACON_PERIOD_MAX) {
		beacon_period = (struct host_cmd_tlv_beacon_period *)tlv;
		beacon_period->header.type =
					cpu_to_le16(TLV_TYPE_UAP_BEACON_PERIOD);
		beacon_period->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_beacon_period) -
				    sizeof(struct nxpwifi_ie_types_header));
		beacon_period->period = cpu_to_le16(bss_cfg->beacon_period);
		cmd_size += sizeof(struct host_cmd_tlv_beacon_period);
		tlv += sizeof(struct host_cmd_tlv_beacon_period);
	}
	if (bss_cfg->dtim_period >= NXPWIFI_MIN_DTIM_PERIOD &&
	    bss_cfg->dtim_period <= NXPWIFI_MAX_DTIM_PERIOD) {
		dtim_period = (struct host_cmd_tlv_dtim_period *)tlv;
		dtim_period->header.type =
			cpu_to_le16(TLV_TYPE_UAP_DTIM_PERIOD);
		dtim_period->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_dtim_period) -
				    sizeof(struct nxpwifi_ie_types_header));
		dtim_period->period = bss_cfg->dtim_period;
		cmd_size += sizeof(struct host_cmd_tlv_dtim_period);
		tlv += sizeof(struct host_cmd_tlv_dtim_period);
	}
	if (bss_cfg->rts_threshold <= NXPWIFI_RTS_THRESHOLD_MAX) {
		rts_threshold = (struct host_cmd_tlv_rts_threshold *)tlv;
		rts_threshold->header.type =
					cpu_to_le16(TLV_TYPE_UAP_RTS_THRESHOLD);
		rts_threshold->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_rts_threshold) -
				    sizeof(struct nxpwifi_ie_types_header));
		rts_threshold->rts_thr = cpu_to_le16(bss_cfg->rts_threshold);
		cmd_size += sizeof(struct host_cmd_tlv_frag_threshold);
		tlv += sizeof(struct host_cmd_tlv_frag_threshold);
	}
	if (bss_cfg->frag_threshold >= NXPWIFI_FRAG_THRESHOLD_MIN &&
	    bss_cfg->frag_threshold <= NXPWIFI_FRAG_THRESHOLD_MAX) {
		frag_threshold = (struct host_cmd_tlv_frag_threshold *)tlv;
		frag_threshold->header.type =
				cpu_to_le16(TLV_TYPE_UAP_FRAG_THRESHOLD);
		frag_threshold->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_frag_threshold) -
				    sizeof(struct nxpwifi_ie_types_header));
		frag_threshold->frag_thr = cpu_to_le16(bss_cfg->frag_threshold);
		cmd_size += sizeof(struct host_cmd_tlv_frag_threshold);
		tlv += sizeof(struct host_cmd_tlv_frag_threshold);
	}
	if (bss_cfg->retry_limit <= NXPWIFI_RETRY_LIMIT_MAX) {
		retry_limit = (struct host_cmd_tlv_retry_limit *)tlv;
		retry_limit->header.type =
			cpu_to_le16(TLV_TYPE_UAP_RETRY_LIMIT);
		retry_limit->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_retry_limit) -
				    sizeof(struct nxpwifi_ie_types_header));
		retry_limit->limit = (u8)bss_cfg->retry_limit;
		cmd_size += sizeof(struct host_cmd_tlv_retry_limit);
		tlv += sizeof(struct host_cmd_tlv_retry_limit);
	}
	if ((bss_cfg->protocol & PROTOCOL_WPA) ||
	    (bss_cfg->protocol & PROTOCOL_WPA2) ||
	    (bss_cfg->protocol & PROTOCOL_EAP))
		nxpwifi_uap_bss_wpa(&tlv, cmd_buf, &cmd_size);
	else
		nxpwifi_uap_bss_wep(&tlv, cmd_buf, &cmd_size);

	if (bss_cfg->auth_mode <= WLAN_AUTH_SHARED_KEY ||
	    bss_cfg->auth_mode == NXPWIFI_AUTH_MODE_AUTO) {
		auth_type = (struct host_cmd_tlv_auth_type *)tlv;
		auth_type->header.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
		auth_type->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_auth_type) -
			sizeof(struct nxpwifi_ie_types_header));
		auth_type->auth_type = (u8)bss_cfg->auth_mode;
		auth_type->pwe_derivation = 0;
		auth_type->transition_disable = 0;
		cmd_size += sizeof(struct host_cmd_tlv_auth_type);
		tlv += sizeof(struct host_cmd_tlv_auth_type);
	}
	if (bss_cfg->protocol) {
		encrypt_protocol = (struct host_cmd_tlv_encrypt_protocol *)tlv;
		encrypt_protocol->header.type =
			cpu_to_le16(TLV_TYPE_UAP_ENCRY_PROTOCOL);
		encrypt_protocol->header.len =
			cpu_to_le16(sizeof(struct host_cmd_tlv_encrypt_protocol)
			- sizeof(struct nxpwifi_ie_types_header));
		encrypt_protocol->proto = cpu_to_le16(bss_cfg->protocol);
		cmd_size += sizeof(struct host_cmd_tlv_encrypt_protocol);
		tlv += sizeof(struct host_cmd_tlv_encrypt_protocol);
	}

	if (bss_cfg->ht_cap.cap_info) {
		htcap = (struct nxpwifi_ie_types_htcap *)tlv;
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
		cmd_size += sizeof(struct nxpwifi_ie_types_htcap);
		tlv += sizeof(struct nxpwifi_ie_types_htcap);
	}

	if (priv->wmm_enabled) {
		struct nxpwifi_ie_types_wmmcap *wmm_cap;
		struct nxpwifi_types_wmm_info *fw_wmm;
		const struct ieee80211_wmm_param_ie *ie;

		wmm_cap = (struct nxpwifi_ie_types_wmmcap *)tlv;
		fw_wmm = &wmm_cap->wmm_info;
		ie = &bss_cfg->wmm_element;

		wmm_cap->header.type = cpu_to_le16(WLAN_EID_VENDOR_SPECIFIC);
		wmm_cap->header.len =
		    cpu_to_le16(sizeof(struct nxpwifi_types_wmm_info));

		/* Map 802.11 WMM IE fields to FW WMM TLV payload */
		fw_wmm->oui[0] = ie->oui[0];
		fw_wmm->oui[1] = ie->oui[1];
		fw_wmm->oui[2] = ie->oui[2];
		fw_wmm->oui[3] = ie->oui_type;

		fw_wmm->subtype = ie->oui_subtype;
		fw_wmm->version = ie->version;
		fw_wmm->qos_info = ie->qos_info;
		fw_wmm->reserved = ie->reserved;

		memcpy(fw_wmm->ac, ie->ac, sizeof(fw_wmm->ac));

		cmd_size += sizeof(*wmm_cap);
		tlv += sizeof(*wmm_cap);
	}

	if (bss_cfg->sta_ao_timer) {
		ao_timer = (struct host_cmd_tlv_ageout_timer *)tlv;
		ao_timer->header.type = cpu_to_le16(TLV_TYPE_UAP_AO_TIMER);
		ao_timer->header.len = cpu_to_le16(sizeof(*ao_timer) -
					sizeof(struct nxpwifi_ie_types_header));
		ao_timer->sta_ao_timer = cpu_to_le32(bss_cfg->sta_ao_timer);
		cmd_size += sizeof(*ao_timer);
		tlv += sizeof(*ao_timer);
	}

	if (bss_cfg->power_constraint) {
		pwr_ct = (void *)tlv;
		pwr_ct->header.type = cpu_to_le16(TLV_TYPE_PWR_CONSTRAINT);
		pwr_ct->header.len = cpu_to_le16(sizeof(u8));
		pwr_ct->constraint = bss_cfg->power_constraint;
		cmd_size += sizeof(*pwr_ct);
		tlv += sizeof(*pwr_ct);
	}

	if (bss_cfg->ps_sta_ao_timer) {
		ps_ao_timer = (struct host_cmd_tlv_ageout_timer *)tlv;
		ps_ao_timer->header.type =
				cpu_to_le16(TLV_TYPE_UAP_PS_AO_TIMER);
		ps_ao_timer->header.len = cpu_to_le16(sizeof(*ps_ao_timer) -
				sizeof(struct nxpwifi_ie_types_header));
		ps_ao_timer->sta_ao_timer =
					cpu_to_le32(bss_cfg->ps_sta_ao_timer);
		cmd_size += sizeof(*ps_ao_timer);
		tlv += sizeof(*ps_ao_timer);
	}

	*param_size = cmd_size;

	return 0;
}

/* Parse custom IEs and write them to the command buffer. */
static int nxpwifi_uap_custom_ie_prepare(u8 *tlv, void *cmd_buf, u16 *ie_size)
{
	struct nxpwifi_ie_list *ap_ie = cmd_buf;
	struct nxpwifi_ie_types_header *tlv_ie = (void *)tlv;

	if (!ap_ie || !ap_ie->len)
		return -EINVAL;

	*ie_size += le16_to_cpu(ap_ie->len) +
		    sizeof(struct nxpwifi_ie_types_header);

	tlv_ie->type = cpu_to_le16(TLV_TYPE_MGMT_IE);
	tlv_ie->len = ap_ie->len;
	tlv += sizeof(struct nxpwifi_ie_types_header);

	memcpy(tlv, ap_ie->ie_list, le16_to_cpu(ap_ie->len));

	return 0;
}

static int
nxpwifi_cmd_uap_sys_config(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *cmd,
			   u16 cmd_no, void *data_buf,
			   u16 cmd_action, u32 cmd_type)
{
	u8 *tlv;
	u16 cmd_size, param_size, ie_size;
	struct host_cmd_ds_sys_config *sys_cfg;
	int ret = 0;

	cmd->command = cpu_to_le16(HOST_CMD_UAP_SYS_CONFIG);
	cmd_size = (u16)(sizeof(struct host_cmd_ds_sys_config) + S_DS_GEN);
	sys_cfg = &cmd->params.uap_sys_config;
	sys_cfg->action = cpu_to_le16(cmd_action);
	tlv = sys_cfg->tlv;

	switch (cmd_type) {
	case UAP_BSS_PARAMS_I:
		param_size = cmd_size;
		ret = nxpwifi_uap_bss_param_prepare(priv, tlv, data_buf, &param_size);
		if (ret)
			return ret;
		cmd->size = cpu_to_le16(param_size);
		break;
	case UAP_CUSTOM_IE_I:
		ie_size = cmd_size;
		ret = nxpwifi_uap_custom_ie_prepare(tlv, data_buf, &ie_size);
		if (ret)
			return ret;
		cmd->size = cpu_to_le16(ie_size);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int
nxpwifi_cmd_uap_bss_start(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_no, void *data_buf,
			  u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_ie_types_host_mlme *tlv;
	int size;

	cmd->command = cpu_to_le16(HOST_CMD_UAP_BSS_START);
	size = S_DS_GEN;

	tlv = (struct nxpwifi_ie_types_host_mlme *)((u8 *)cmd + size);
	tlv->header.type = cpu_to_le16(TLV_TYPE_HOST_MLME);
	tlv->header.len = cpu_to_le16(sizeof(tlv->host_mlme));
	tlv->host_mlme = 1;
	size += sizeof(struct nxpwifi_ie_types_host_mlme);

	cmd->size = cpu_to_le16(size);

	return 0;
}

static int
nxpwifi_ret_uap_bss_start(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *resp,
			  u16 cmdresp_no,
			  void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->tx_lock_flag = false;
	adapter->pps_uapsd_mode = false;
	adapter->delay_null_pkt = false;
	priv->bss_started = 1;

	return 0;
}

static int
nxpwifi_ret_uap_bss_stop(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 u16 cmdresp_no,
			 void *data_buf)
{
	priv->bss_started = 0;

	return 0;
}

static int nxpwifi_ret_apcmd_sta_list(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *resp,
				      u16 cmdresp_no, void *data_buf)
{
	struct host_cmd_ds_sta_list *sta_list = &resp->params.sta_list;
	struct nxpwifi_ie_types_sta_info *sta_info;
	struct nxpwifi_sta_node *sta_node;
	u16 sta_count;
	u32 resp_size;
	u32 base;
	u32 required_size;
	int i;

	resp_size = le16_to_cpu(resp->size);
	sta_count = le16_to_cpu(sta_list->sta_count);

	/* End of fixed fields before sta_list.tlv[] */
	base = offsetofend(struct host_cmd_ds_command, params.sta_list.sta_count);

	/* At least fixed fields must be present */
	if (resp_size < base)
		return -EINVAL;

	required_size = base + sta_count * sizeof(*sta_info);

	/* Verify firmware did not claim more entries than the buffer holds */
	if (resp_size < required_size)
		return -EINVAL;

	sta_info = (void *)sta_list->tlv;

	rcu_read_lock();
	for (i = 0; i < sta_count; i++) {
		sta_node = nxpwifi_get_sta_entry(priv, sta_info->mac);
		if (unlikely(!sta_node)) {
			sta_info++;
			continue;
		}

		sta_node->stats.rssi = sta_info->rssi;
		sta_info++;
	}
	rcu_read_unlock();

	return 0;
}

/* Build AP deauth command for the given MAC address. */
static int nxpwifi_cmd_uap_sta_deauth(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *cmd,
				      u16 cmd_no, void *data_buf,
				      u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_sta_deauth *sta_deauth = &cmd->params.sta_deauth;
	u8 *mac = (u8 *)data_buf;

	cmd->command = cpu_to_le16(HOST_CMD_UAP_STA_DEAUTH);
	memcpy(sta_deauth->mac, mac, ETH_ALEN);
	sta_deauth->reason = cpu_to_le16(WLAN_REASON_DEAUTH_LEAVING);

	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_sta_deauth) +
				S_DS_GEN);
	return 0;
}

static int
nxpwifi_cmd_uap_chan_report_request(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *cmd,
				    u16 cmd_no, void *data_buf,
				    u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_issue_chan_report_request(priv, cmd, data_buf);
}

/* Build AP add-station command. */
static int
nxpwifi_cmd_uap_add_new_station(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_no, void *data_buf,
				u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_add_station *new_sta = &cmd->params.sta_info;
	struct nxpwifi_sta_info *add_sta = (struct nxpwifi_sta_info *)data_buf;
	struct station_parameters *params = add_sta->params;
	struct nxpwifi_sta_node *sta_ptr;
	u16 cmd_size;
	u8 *pos, *cmd_end;
	u16 tlv_len;
	struct nxpwifi_ie_types_sta_flag *sta_flag;
	int i;

	cmd->command = cpu_to_le16(HOST_CMD_ADD_NEW_STATION);
	new_sta->action = cpu_to_le16(cmd_action);
	cmd_size = sizeof(struct host_cmd_ds_add_station) + S_DS_GEN;

	if (cmd_action == HOST_ACT_ADD_STA)
		sta_ptr = nxpwifi_add_sta_entry(priv, add_sta->peer_mac);
	else
		sta_ptr = nxpwifi_get_sta_entry_rcu(priv, add_sta->peer_mac);

	if (!sta_ptr)
		return -EINVAL;

	memcpy(new_sta->peer_mac, add_sta->peer_mac, ETH_ALEN);

	if (cmd_action == HOST_ACT_REMOVE_STA) {
		cmd->size = cpu_to_le16(cmd_size);
		return 0;
	}

	new_sta->aid = cpu_to_le16(params->aid);
	new_sta->listen_interval = cpu_to_le32(params->listen_interval);
	new_sta->cap_info = cpu_to_le16(params->capability);

	pos = new_sta->tlv;
	cmd_end = (u8 *)cmd;
	cmd_end += (NXPWIFI_SIZE_OF_CMD_BUFFER - 1);

	if (params->sta_flags_set & NL80211_STA_FLAG_WME)
		sta_ptr->is_wmm_enabled = 1;
	sta_flag = (struct nxpwifi_ie_types_sta_flag *)pos;
	sta_flag->header.type = cpu_to_le16(TLV_TYPE_UAP_STA_FLAGS);
	sta_flag->header.len = cpu_to_le16(sizeof(__le32));
	sta_flag->sta_flags = cpu_to_le32(params->sta_flags_set);
	pos += sizeof(struct nxpwifi_ie_types_sta_flag);
	cmd_size += sizeof(struct nxpwifi_ie_types_sta_flag);

	if (params->ext_capab_len) {
		u8 *data = (u8 *)params->ext_capab;
		u16 len = params->ext_capab_len;

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_EXT_CAPABILITY,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
	}

	if (params->link_sta_params.supported_rates_len) {
		u8 *data = (u8 *)params->link_sta_params.supported_rates;
		u16 len = params->link_sta_params.supported_rates_len;

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_SUPP_RATES,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
	}

	if (params->uapsd_queues || params->max_sp) {
		u8 qos_capability = params->uapsd_queues | (params->max_sp << 5);
		u8 *data = &qos_capability;
		u16 len = sizeof(u8);

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_QOS_CAPA,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
		sta_ptr->is_wmm_enabled = 1;
	}

	if (params->link_sta_params.ht_capa) {
		u8 *data = (u8 *)params->link_sta_params.ht_capa;
		u16 len = sizeof(struct ieee80211_ht_cap);

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_HT_CAPABILITY,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
		sta_ptr->is_11n_enabled = 1;
		sta_ptr->max_amsdu =
			le16_to_cpu(params->link_sta_params.ht_capa->cap_info) &
			IEEE80211_HT_CAP_MAX_AMSDU ?
			NXPWIFI_TX_DATA_BUF_SIZE_8K :
			NXPWIFI_TX_DATA_BUF_SIZE_4K;
	}

	if (params->link_sta_params.vht_capa) {
		u8 *data = (u8 *)params->link_sta_params.vht_capa;
		u16 len = sizeof(struct ieee80211_vht_cap);

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_VHT_CAPABILITY,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
		sta_ptr->is_11ac_enabled = 1;
	}

	if (params->link_sta_params.opmode_notif_used) {
		u8 *data = &params->link_sta_params.opmode_notif;
		u16 len = sizeof(u8);

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_OPMODE_NOTIF,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
	}

	if (params->link_sta_params.he_capa_len) {
		u8 *data = (u8 *)params->link_sta_params.he_capa;
		u16 len = params->link_sta_params.he_capa_len;

		tlv_len = nxpwifi_append_data_tlv(WLAN_EID_EXT_HE_CAPABILITY,
						  data, len, pos, cmd_end);
		if (!tlv_len)
			return -EINVAL;
		pos += tlv_len;
		cmd_size += tlv_len;
		sta_ptr->is_11ax_enabled = 1;
	}

	for (i = 0; i < MAX_NUM_TID; i++) {
		if (sta_ptr->is_11n_enabled || sta_ptr->is_11ax_enabled)
			sta_ptr->ampdu_sta[i] =
				      priv->aggr_prio_tbl[i].ampdu_user;
		else
			sta_ptr->ampdu_sta[i] = BA_STREAM_NOT_ALLOWED;
	}

	memset(sta_ptr->rx_seq, 0xff, sizeof(sta_ptr->rx_seq));

	cmd->size = cpu_to_le16(cmd_size);

	return 0;
}

static const struct nxpwifi_cmd_entry cmd_table_uap[] = {
	{.cmd_no = HOST_CMD_APCMD_SYS_RESET,
	.prepare_cmd = nxpwifi_cmd_fill_head_only,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_UAP_SYS_CONFIG,
	.prepare_cmd = nxpwifi_cmd_uap_sys_config,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_UAP_BSS_START,
	.prepare_cmd = nxpwifi_cmd_uap_bss_start,
	.cmd_resp = nxpwifi_ret_uap_bss_start},
	{.cmd_no = HOST_CMD_UAP_BSS_STOP,
	.prepare_cmd = nxpwifi_cmd_fill_head_only,
	.cmd_resp = nxpwifi_ret_uap_bss_stop},
	{.cmd_no = HOST_CMD_APCMD_STA_LIST,
	.prepare_cmd = nxpwifi_cmd_fill_head_only,
	.cmd_resp = nxpwifi_ret_apcmd_sta_list},
	{.cmd_no = HOST_CMD_UAP_STA_DEAUTH,
	.prepare_cmd = nxpwifi_cmd_uap_sta_deauth,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_CHAN_REPORT_REQUEST,
	.prepare_cmd = nxpwifi_cmd_uap_chan_report_request,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_ADD_NEW_STATION,
	.prepare_cmd = nxpwifi_cmd_uap_add_new_station,
	.cmd_resp = NULL},
};

/* Prepare AP commands and dispatch to per-cmd builders before sending to firmware. */
int nxpwifi_uap_prepare_cmd(struct nxpwifi_private *priv,
			    struct cmd_ctrl_node *cmd_node,
			    u16 cmd_action, u32 type)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u16 cmd_no = cmd_node->cmd_no;
	struct host_cmd_ds_command *cmd =
		(struct host_cmd_ds_command *)cmd_node->skb->data;
	void *data_buf = cmd_node->data_buf;
	int i, ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cmd_table_uap); i++) {
		if (cmd_no == cmd_table_uap[i].cmd_no) {
			if (cmd_table_uap[i].prepare_cmd)
				ret = cmd_table_uap[i].prepare_cmd(priv, cmd,
								   cmd_no,
								   data_buf,
								   cmd_action,
								   type);
			cmd_node->cmd_resp = cmd_table_uap[i].cmd_resp;
			break;
		}
	}

	if (i == ARRAY_SIZE(cmd_table_uap))
		nxpwifi_dbg(adapter, ERROR,
			    "%s: unknown command: %#x\n",
			    __func__, cmd_no);
	else
		nxpwifi_dbg(adapter, CMD,
			    "%s: command: %#x\n",
			    __func__, cmd_no);

	return ret;
}

/* Translate cfg80211_ap_settings security into bss_config for firmware. */
int nxpwifi_set_secure_params(struct nxpwifi_private *priv,
			      struct nxpwifi_uap_bss_param *bss_config,
			      struct cfg80211_ap_settings *params)
{
	int i;
	struct nxpwifi_wep_key wep_key;

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
		bss_config->auth_mode = NXPWIFI_AUTH_MODE_AUTO;
		break;
	}

	bss_config->key_mgmt_operation |= KEY_MGMT_ON_HOST;

	bss_config->protocol = 0;
	if (params->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		bss_config->protocol |= PROTOCOL_WPA;
	if (params->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		bss_config->protocol |= PROTOCOL_WPA2;

	bss_config->key_mgmt = 0;
	for (i = 0; i < params->crypto.n_akm_suites; i++) {
		switch (params->crypto.akm_suites[i]) {
		case WLAN_AKM_SUITE_8021X:
			bss_config->key_mgmt |= KEY_MGMT_EAP;
			break;
		case WLAN_AKM_SUITE_PSK:
			bss_config->key_mgmt |= KEY_MGMT_PSK;
			break;
		case WLAN_AKM_SUITE_PSK_SHA256:
			bss_config->key_mgmt |= KEY_MGMT_PSK_SHA256;
			break;
		case WLAN_AKM_SUITE_OWE:
			bss_config->key_mgmt |= KEY_MGMT_OWE;
			break;
		case WLAN_AKM_SUITE_SAE:
			bss_config->key_mgmt |= KEY_MGMT_SAE;
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
			break;
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

/* Update 11n HT params from beacon and fill bss_config. */
void
nxpwifi_set_ht_params(struct nxpwifi_private *priv,
		      struct nxpwifi_uap_bss_param *bss_cfg,
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
		if (ISSUPP_BEAMFORMING(priv->adapter->hw_dot_11n_dev_cap))
			bss_cfg->ht_cap.tx_BF_cap_info =
				cpu_to_le32(NXPWIFI_DEF_11N_TX_BF_CAP);
		priv->ap_11n_enabled = 1;
	} else {
		memset(&bss_cfg->ht_cap, 0, sizeof(struct ieee80211_ht_cap));
		bss_cfg->ht_cap.cap_info = cpu_to_le16(NXPWIFI_DEF_HT_CAP);
		bss_cfg->ht_cap.ampdu_params_info = NXPWIFI_DEF_AMPDU;
	}
}

/* Update 11ac VHT params from beacon and fill bss_config. */
void nxpwifi_set_vht_params(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params)
{
	const u8 *vht_ie;

	vht_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, params->beacon.tail,
				  params->beacon.tail_len);
	if (vht_ie) {
		memcpy(&bss_cfg->vht_cap, vht_ie + 2,
		       sizeof(struct ieee80211_vht_cap));
		priv->ap_11ac_enabled = 1;
	} else {
		priv->ap_11ac_enabled = 0;
	}
}

/* Extract TPC request from beacon and set power_constraint. */
void nxpwifi_set_tpc_params(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params)
{
	const u8 *tpc_ie;

	tpc_ie = cfg80211_find_ie(WLAN_EID_TPC_REQUEST, params->beacon.tail,
				  params->beacon.tail_len);
	if (tpc_ie)
		bss_cfg->power_constraint = *(tpc_ie + 2);
	else
		bss_cfg->power_constraint = 0;
}

/* Enable VHT only when VHT IE is present; otherwise disable VHT. */
void nxpwifi_set_vht_width(struct nxpwifi_private *priv,
			   enum nl80211_chan_width width,
			   bool ap_11ac_enable)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_11ac_vht_cfg vht_cfg;

	vht_cfg.band_config = VHT_CFG_5GHZ;
	vht_cfg.cap_info = adapter->hw_dot_11ac_dev_cap;

	if (!ap_11ac_enable) {
		vht_cfg.mcs_tx_set = DISABLE_VHT_MCS_SET;
		vht_cfg.mcs_rx_set = DISABLE_VHT_MCS_SET;
	} else {
		vht_cfg.mcs_tx_set = DEFAULT_VHT_MCS_SET;
		vht_cfg.mcs_rx_set = DEFAULT_VHT_MCS_SET;
	}

	vht_cfg.misc_config  = VHT_CAP_UAP_ONLY;

	if (ap_11ac_enable && width >= NL80211_CHAN_WIDTH_80)
		vht_cfg.misc_config |= VHT_BW_80_160_80P80;

	nxpwifi_send_cmd(priv, HOST_CMD_11AC_CFG,
			 HOST_ACT_GEN_SET, 0, &vht_cfg, true);
}

bool nxpwifi_check_11ax_capability(struct nxpwifi_private *priv,
				   struct nxpwifi_uap_bss_param *bss_cfg,
				   struct cfg80211_ap_settings *params)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 band = bss_cfg->band_cfg & BAND_CFG_CHAN_BAND_MASK;

	if (band == BAND_2GHZ &&
	    !(adapter->fw_bands & BAND_GAX))
		return false;

	if (band == BAND_5GHZ &&
	    !(adapter->fw_bands & BAND_AAX))
		return false;

	if (params->he_cap)
		return true;
	else
		return false;
}

int nxpwifi_set_11ax_status(struct nxpwifi_private *priv,
			    struct nxpwifi_uap_bss_param *bss_cfg,
			    struct cfg80211_ap_settings *params)
{
	struct nxpwifi_11ax_he_cfg ax_cfg;
	u8 band = bss_cfg->band_cfg & BAND_CFG_CHAN_BAND_MASK;
	const struct element *he_cap;
	int ret;

	if (band == BAND_2GHZ)
		ax_cfg.band = BIT(0);
	else if (band == BAND_5GHZ)
		ax_cfg.band = BIT(1);
	else
		return -EINVAL;

	ret = nxpwifi_send_cmd(priv, HOST_CMD_11AX_CFG,
			       HOST_ACT_GEN_GET, 0, &ax_cfg, true);
	if (ret)
		return ret;

	he_cap = cfg80211_find_ext_elem(WLAN_EID_EXT_HE_CAPABILITY,
					params->beacon.tail,
					params->beacon.tail_len);

	if (he_cap) {
		ax_cfg.he_cap_cfg.id = he_cap->id;
		ax_cfg.he_cap_cfg.len = he_cap->datalen;
		if (params->twt_responder == 0) {
			struct nxpwifi_11ax_he_cap_cfg *he_cap_cfg =
			(struct nxpwifi_11ax_he_cap_cfg *)he_cap;

			he_cap_cfg->cap_elem.mac_cap_info[0] &=
				~HE_MAC_CAP_TWT_RESP_SUPPORT;
		}
		memcpy(ax_cfg.data + 4,
		       he_cap->data,
		       he_cap->datalen);
	} else {
		/* disable */
		if (ax_cfg.he_cap_cfg.len &&
		    ax_cfg.he_cap_cfg.ext_id == WLAN_EID_EXT_HE_CAPABILITY) {
			memset(ax_cfg.he_cap_cfg.he_txrx_mcs_support, 0xff,
			       sizeof(ax_cfg.he_cap_cfg.he_txrx_mcs_support));
		}
	}

	return nxpwifi_send_cmd(priv, HOST_CMD_11AX_CFG,
				HOST_ACT_GEN_SET, 0, &ax_cfg, true);
}

/* Copy supported rates from beacon into bss_config. */
void
nxpwifi_set_uap_rates(struct nxpwifi_uap_bss_param *bss_cfg,
		      struct cfg80211_ap_settings *params)
{
	struct element *rate_ie;
	int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const u8 *var_pos = params->beacon.head + var_offset;
	int len = params->beacon.head_len - var_offset;
	u8 rate_len = 0;

	rate_ie = (void *)cfg80211_find_ie(WLAN_EID_SUPP_RATES, var_pos, len);
	if (rate_ie) {
		if (rate_ie->datalen > NXPWIFI_SUPPORTED_RATES)
			return;
		memcpy(bss_cfg->rates, rate_ie + 1, rate_ie->datalen);
		rate_len = rate_ie->datalen;
	}

	rate_ie = (void *)cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES,
					   params->beacon.tail,
					   params->beacon.tail_len);
	if (rate_ie) {
		if (rate_ie->datalen > NXPWIFI_SUPPORTED_RATES - rate_len)
			return;
		memcpy(bss_cfg->rates + rate_len,
		       rate_ie + 1, rate_ie->datalen);
	}
}

/*
 * Initialize bss_config fields to sentinel values.
 * Fields left with sentinel values are treated as unset and will not be
 * included in the corresponding firmware command.
 */
void nxpwifi_set_sys_config_invalid_data(struct nxpwifi_uap_bss_param *config)
{
	config->radio_ctl = __NXPWIFI_RADIO_CTL_MAX;
	config->dtim_period = NXPWIFI_INVALID_DTIM_PERIOD;
	config->beacon_period = NXPWIFI_INVALID_BEACON_PERIOD;
	config->auth_mode = NXPWIFI_AUTH_MODE_AUTO;
	config->rts_threshold = NXPWIFI_INVALID_RTS;
	config->frag_threshold = NXPWIFI_INVALID_FRAG;
	config->retry_limit = NXPWIFI_INVALID_RETRY_LIMI;
}

/* Parse WMM params from cfg80211_ap_settings and update bss_config. */
void
nxpwifi_set_wmm_params(struct nxpwifi_private *priv,
		       struct nxpwifi_uap_bss_param *bss_cfg,
		       struct cfg80211_ap_settings *params)
{
	const u8 *vendor_ie;
	const struct ieee80211_wmm_param_ie *wmm;
	u8 ie_len;

	vendor_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					    WLAN_OUI_TYPE_MICROSOFT_WMM,
					    params->beacon.tail,
					    params->beacon.tail_len);
	if (!vendor_ie)
		goto no_wmm;

	ie_len = vendor_ie[1];

	if (ie_len < sizeof(struct ieee80211_wmm_param_ie) - 2)
		goto no_wmm;

	wmm = (const struct ieee80211_wmm_param_ie *)vendor_ie;

	if (memcmp(wmm->oui, "\x00\x50\xf2", 3))
		goto no_wmm;

	if (wmm->oui_type != WLAN_OUI_TYPE_MICROSOFT_WMM)
		goto no_wmm;

	if (wmm->oui_subtype != 1)
		goto no_wmm;

	/* Only WMM version 1 is supported */
	if (wmm->version != 1)
		goto no_wmm;

	memcpy(&bss_cfg->wmm_element, wmm,
	       sizeof(struct ieee80211_wmm_param_ie));

	priv->wmm_enabled = true;
	return;

no_wmm:
	memset(&bss_cfg->wmm_element, 0, sizeof(bss_cfg->wmm_element));
	priv->wmm_enabled = false;
}

/* Enable 11d when country IE is present. */
void nxpwifi_config_uap_11d(struct nxpwifi_private *priv,
			    struct cfg80211_beacon_data *beacon_data)
{
	enum state_11d_t state_11d;
	const u8 *country_ie;

	country_ie = cfg80211_find_ie(WLAN_EID_COUNTRY, beacon_data->tail,
				      beacon_data->tail_len);
	if (country_ie) {
		/* Send cmd to FW to enable 11D function */
		state_11d = ENABLE_11D;
		if (nxpwifi_send_cmd(priv, HOST_CMD_802_11_SNMP_MIB,
				     HOST_ACT_GEN_SET, DOT11D_I,
				     &state_11d, true)) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "11D: failed to enable 11D\n");
		}
	}
}

void nxpwifi_uap_set_channel(struct nxpwifi_private *priv,
			     struct nxpwifi_uap_bss_param *bss_cfg,
			     struct cfg80211_chan_def chandef)
{
	u8 config_bands = 0, old_bands = priv->config_bands;

	priv->bss_chandef = chandef;

	bss_cfg->channel =
		ieee80211_frequency_to_channel(chandef.chan->center_freq);

	nxpwifi_convert_chan_to_band_cfg(priv, &bss_cfg->band_cfg, &chandef);

	/* Set appropriate bands */
	if (chandef.chan->band == NL80211_BAND_2GHZ) {
		config_bands = BAND_B | BAND_G;
		if (chandef.width > NL80211_CHAN_WIDTH_20_NOHT)
			config_bands |= BAND_GN | BAND_GAX;
	} else {
		config_bands = BAND_A;
		if (chandef.width > NL80211_CHAN_WIDTH_20_NOHT)
			config_bands |= BAND_AN;
		if (chandef.width > NL80211_CHAN_WIDTH_40)
			config_bands |= BAND_AAC | BAND_AAX;
	}

	priv->config_bands = config_bands;

	if (old_bands != config_bands) {
		if (nxpwifi_band_to_radio_type(priv->config_bands) ==
		    HOST_SCAN_RADIO_TYPE_BG)
			nxpwifi_send_domain_info_cmd_fw(priv->adapter->wiphy,
							NL80211_BAND_2GHZ);
		else
			nxpwifi_send_domain_info_cmd_fw(priv->adapter->wiphy,
							NL80211_BAND_5GHZ);
	}
}

int nxpwifi_config_start_uap(struct nxpwifi_private *priv,
			     struct nxpwifi_uap_bss_param *bss_cfg)
{
	int ret;

	ret = nxpwifi_send_cmd(priv, HOST_CMD_UAP_SYS_CONFIG,
			       HOST_ACT_GEN_SET,
			       UAP_BSS_PARAMS_I, bss_cfg, true);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to set AP configuration\n");
		return ret;
	}

	ret = nxpwifi_send_cmd(priv, HOST_CMD_UAP_BSS_START,
			       HOST_ACT_GEN_SET, 0, NULL, true);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to start the BSS\n");
		return ret;
	}

	if (priv->sec_info.wep_enabled)
		priv->curr_pkt_filter |= HOST_ACT_MAC_WEP_ENABLE;
	else
		priv->curr_pkt_filter &= ~HOST_ACT_MAC_WEP_ENABLE;

	ret = nxpwifi_send_cmd(priv, HOST_CMD_MAC_CONTROL,
			       HOST_ACT_GEN_SET, 0,
			       &priv->curr_pkt_filter, true);

	return ret;
}
