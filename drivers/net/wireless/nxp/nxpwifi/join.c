// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: association and ad-hoc start/join
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "wmm.h"
#include "11n.h"
#include "11ac.h"
#include "11ax.h"

#define CAPINFO_MASK    (~(BIT(15) | BIT(14) | BIT(12) | BIT(11) | BIT(9)))

/* Append generic IE as pass-through TLV for join */
static int
nxpwifi_cmd_append_generic_ie(struct nxpwifi_private *priv, u8 **buffer)
{
	int ret_len = 0;
	struct nxpwifi_ie_types_header ie_header;

	/* Null Checks */
	if (!buffer)
		return 0;
	if (!(*buffer))
		return 0;

	/*
	 * If there is a generic element buffer setup, append it to the return
	 * parameter buffer pointer.
	 */
	if (priv->gen_ie_buf_len) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "info: %s: append generic element len %d to %p\n",
			    __func__, priv->gen_ie_buf_len, *buffer);

		/* Wrap the generic element buffer with a pass through TLV type */
		ie_header.type = cpu_to_le16(TLV_TYPE_PASSTHROUGH);
		ie_header.len = cpu_to_le16(priv->gen_ie_buf_len);
		memcpy(*buffer, &ie_header, sizeof(ie_header));

		/*
		 * Increment the return size and the return buffer pointer
		 * param
		 */
		*buffer += sizeof(ie_header);
		ret_len += sizeof(ie_header);

		/*
		 * Copy the generic element buffer to the output buffer, advance
		 * pointer
		 */
		memcpy(*buffer, priv->gen_ie_buf, priv->gen_ie_buf_len);

		/*
		 * Increment the return size and the return buffer pointer
		 * param
		 */
		*buffer += priv->gen_ie_buf_len;
		ret_len += priv->gen_ie_buf_len;

		/* Reset the generic element buffer */
		priv->gen_ie_buf_len = 0;
	}

	/* return the length appended to the buffer */
	return ret_len;
}

/* Append TSF timestamp (AP TSF and local RX TSF) for reassoc */
static int
nxpwifi_cmd_append_tsf_tlv(struct nxpwifi_private *priv, u8 **buffer,
			   struct nxpwifi_bssdescriptor *bss_desc)
{
	struct nxpwifi_ie_types_tsf_timestamp tsf_tlv;
	__le64 tsf_val;

	/* Null Checks */
	if (!buffer)
		return 0;
	if (!*buffer)
		return 0;

	memset(&tsf_tlv, 0x00, sizeof(struct nxpwifi_ie_types_tsf_timestamp));

	tsf_tlv.header.type = cpu_to_le16(TLV_TYPE_TSFTIMESTAMP);
	tsf_tlv.header.len = cpu_to_le16(2 * sizeof(tsf_val));

	memcpy(*buffer, &tsf_tlv, sizeof(tsf_tlv.header));
	*buffer += sizeof(tsf_tlv.header);

	/* TSF at the time when beacon/probe_response was received */
	tsf_val = cpu_to_le64(bss_desc->fw_tsf);
	memcpy(*buffer, &tsf_val, sizeof(tsf_val));
	*buffer += sizeof(tsf_val);

	tsf_val = cpu_to_le64(bss_desc->timestamp);

	nxpwifi_dbg(priv->adapter, INFO,
		    "info: %s: TSF offset calc: %016llx - %016llx\n",
		    __func__, bss_desc->timestamp, bss_desc->fw_tsf);

	memcpy(*buffer, &tsf_val, sizeof(tsf_val));
	*buffer += sizeof(tsf_val);

	return sizeof(tsf_tlv.header) + (2 * sizeof(tsf_val));
}

/* Compute intersection of two rate sets; rate1 updated in-place */
static int nxpwifi_get_common_rates(struct nxpwifi_private *priv, u8 *rate1,
				    u32 rate1_size, u8 *rate2, u32 rate2_size)
{
	int ret;
	u8 *ptr = rate1, *tmp;
	u32 i, j;

	tmp = kmemdup(rate1, rate1_size, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	memset(rate1, 0, rate1_size);

	for (i = 0; i < rate2_size && rate2[i]; i++) {
		for (j = 0; j < rate1_size && tmp[j]; j++) {
			/*
			 * Check common rate, excluding the bit for
			 * basic rate
			 */
			if ((rate2[i] & 0x7F) == (tmp[j] & 0x7F)) {
				*rate1++ = tmp[j];
				break;
			}
		}
	}

	nxpwifi_dbg(priv->adapter, INFO, "info: Tx data rate set to %#x\n",
		    priv->data_rate);

	if (!priv->is_data_rate_auto) {
		while (*ptr) {
			if ((*ptr & 0x7f) == priv->data_rate) {
				ret = 0;
				goto done;
			}
			ptr++;
		}
		nxpwifi_dbg(priv->adapter, ERROR,
			    "previously set fixed data rate %#x\t"
			    "is not compatible with the network\n",
			    priv->data_rate);

		ret = -EPERM;
		goto done;
	}

	ret = 0;
done:
	kfree(tmp);
	return ret;
}

/* Build common rates from BSS descriptor into out_rates */
static int
nxpwifi_setup_rates_from_bssdesc(struct nxpwifi_private *priv,
				 struct nxpwifi_bssdescriptor *bss_desc,
				 u8 *out_rates, u32 *out_rates_size)
{
	u8 card_rates[NXPWIFI_SUPPORTED_RATES];
	u32 card_rates_size;
	int ret;

	/* Copy AP supported rates */
	memcpy(out_rates, bss_desc->supported_rates, NXPWIFI_SUPPORTED_RATES);
	/* Get the STA supported rates */
	card_rates_size = nxpwifi_get_active_data_rates(priv, card_rates);
	/* Get the common rates between AP and STA supported rates */
	ret = nxpwifi_get_common_rates(priv, out_rates, NXPWIFI_SUPPORTED_RATES,
				       card_rates, card_rates_size);
	if (ret) {
		*out_rates_size = 0;
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: cannot get common rates\n",
			    __func__);
	} else {
		*out_rates_size =
			min_t(size_t, strlen(out_rates), NXPWIFI_SUPPORTED_RATES);
	}

	return ret;
}

/* Append WPS IE as pass-through TLV for join */
static int
nxpwifi_cmd_append_wps_ie(struct nxpwifi_private *priv, u8 **buffer)
{
	int ret_len = 0;
	struct nxpwifi_ie_types_header ie_header;

	if (!buffer || !*buffer)
		return 0;

	/*
	 * If there is a wps element buffer setup, append it to the return
	 * parameter buffer pointer.
	 */
	if (priv->wps_ie_len) {
		nxpwifi_dbg(priv->adapter, CMD,
			    "cmd: append wps element %d to %p\n",
			    priv->wps_ie_len, *buffer);

		/* Wrap the generic element buffer with a pass through TLV type */
		ie_header.type = cpu_to_le16(TLV_TYPE_PASSTHROUGH);
		ie_header.len = cpu_to_le16(priv->wps_ie_len);
		memcpy(*buffer, &ie_header, sizeof(ie_header));
		*buffer += sizeof(ie_header);
		ret_len += sizeof(ie_header);

		memcpy(*buffer, priv->wps_ie, priv->wps_ie_len);
		*buffer += priv->wps_ie_len;
		ret_len += priv->wps_ie_len;
	}

	kfree(priv->wps_ie);
	priv->wps_ie_len = 0;
	return ret_len;
}

/* Append WPA/WPA2 RSN IE TLV */
static int nxpwifi_append_rsn_ie_wpa_wpa2(struct nxpwifi_private *priv,
					  u8 **buffer)
{
	struct nxpwifi_ie_types_rsn_param_set *rsn_ie_tlv;
	int rsn_ie_len;

	if (!buffer || !(*buffer))
		return 0;

	rsn_ie_tlv = (struct nxpwifi_ie_types_rsn_param_set *)(*buffer);
	rsn_ie_tlv->header.type = cpu_to_le16((u16)priv->wpa_ie[0]);
	rsn_ie_tlv->header.type =
		cpu_to_le16(le16_to_cpu(rsn_ie_tlv->header.type) & 0x00FF);
	rsn_ie_tlv->header.len = cpu_to_le16((u16)priv->wpa_ie[1]);
	rsn_ie_tlv->header.len = cpu_to_le16(le16_to_cpu(rsn_ie_tlv->header.len)
							 & 0x00FF);
	if (le16_to_cpu(rsn_ie_tlv->header.len) <= (sizeof(priv->wpa_ie) - 2))
		memcpy(rsn_ie_tlv->rsn_ie, &priv->wpa_ie[2],
		       le16_to_cpu(rsn_ie_tlv->header.len));
	else
		return -ENOMEM;

	rsn_ie_len = sizeof(rsn_ie_tlv->header) +
					le16_to_cpu(rsn_ie_tlv->header.len);
	*buffer += rsn_ie_len;

	return rsn_ie_len;
}

/* Build 802.11 association command and required TLVs */
int nxpwifi_cmd_802_11_associate(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 struct nxpwifi_bssdescriptor *bss_desc)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_associate *assoc = &cmd->params.associate;
	struct nxpwifi_ie_types_host_mlme *host_mlme_tlv;
	struct nxpwifi_ie_types_ssid_param_set *ssid_tlv;
	struct nxpwifi_ie_types_phy_param_set *phy_tlv;
	struct nxpwifi_ie_types_ss_param_set *ss_tlv;
	struct nxpwifi_ie_types_rates_param_set *rates_tlv;
	struct nxpwifi_ie_types_auth_type *auth_tlv;
	struct nxpwifi_ie_types_sae_pwe_mode *sae_pwe_tlv;
	struct nxpwifi_ie_types_chan_list_param_set *chan_tlv;
	u8 rates[NXPWIFI_SUPPORTED_RATES];
	u32 rates_size;
	u16 tmp_cap;
	u8 *pos;
	int rsn_ie_len = 0;
	int ret;

	pos = (u8 *)assoc;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_ASSOCIATE);

	/* Save so we know which BSS Desc to use in the response handler */
	priv->attempted_bss_desc = bss_desc;

	memcpy(assoc->peer_sta_addr,
	       bss_desc->mac_address, sizeof(assoc->peer_sta_addr));
	pos += sizeof(assoc->peer_sta_addr);

	/* Set the listen interval */
	assoc->listen_interval = cpu_to_le16(priv->listen_interval);
	/* Set the beacon period */
	assoc->beacon_period = cpu_to_le16(bss_desc->beacon_period);

	pos += sizeof(assoc->cap_info_bitmap);
	pos += sizeof(assoc->listen_interval);
	pos += sizeof(assoc->beacon_period);
	pos += sizeof(assoc->dtim_period);

	host_mlme_tlv = (struct nxpwifi_ie_types_host_mlme *)pos;
	host_mlme_tlv->header.type = cpu_to_le16(TLV_TYPE_HOST_MLME);
	host_mlme_tlv->header.len = cpu_to_le16(sizeof(host_mlme_tlv->host_mlme));
	host_mlme_tlv->host_mlme = 1;
	pos += sizeof(host_mlme_tlv->header) + sizeof(host_mlme_tlv->host_mlme);

	ssid_tlv = (struct nxpwifi_ie_types_ssid_param_set *)pos;
	ssid_tlv->header.type = cpu_to_le16(WLAN_EID_SSID);
	ssid_tlv->header.len = cpu_to_le16((u16)bss_desc->ssid.ssid_len);
	memcpy(ssid_tlv->ssid, bss_desc->ssid.ssid,
	       le16_to_cpu(ssid_tlv->header.len));
	pos += sizeof(ssid_tlv->header) + le16_to_cpu(ssid_tlv->header.len);

	phy_tlv = (struct nxpwifi_ie_types_phy_param_set *)pos;
	phy_tlv->header.type = cpu_to_le16(WLAN_EID_DS_PARAMS);
	phy_tlv->header.len = cpu_to_le16(sizeof(phy_tlv->fh_ds.ds_param_set));
	memcpy(&phy_tlv->fh_ds.ds_param_set,
	       &bss_desc->phy_param_set.ds_param_set.current_chan,
	       sizeof(phy_tlv->fh_ds.ds_param_set));
	pos += sizeof(phy_tlv->header) + le16_to_cpu(phy_tlv->header.len);

	ss_tlv = (struct nxpwifi_ie_types_ss_param_set *)pos;
	ss_tlv->header.type = cpu_to_le16(WLAN_EID_CF_PARAMS);
	ss_tlv->header.len = cpu_to_le16(sizeof(ss_tlv->cf_ibss.cf_param_set));
	pos += sizeof(ss_tlv->header) + le16_to_cpu(ss_tlv->header.len);

	/* Get the common rates supported between the driver and the BSS Desc */
	ret = nxpwifi_setup_rates_from_bssdesc(priv, bss_desc,
					       rates, &rates_size);
	if (ret)
		return ret;

	/* Save the data rates into Current BSS state structure */
	priv->curr_bss_params.num_of_rates = rates_size;
	memcpy(&priv->curr_bss_params.data_rates, rates, rates_size);

	/* Setup the Rates TLV in the association command */
	rates_tlv = (struct nxpwifi_ie_types_rates_param_set *)pos;
	rates_tlv->header.type = cpu_to_le16(WLAN_EID_SUPP_RATES);
	rates_tlv->header.len = cpu_to_le16((u16)rates_size);
	memcpy(rates_tlv->rates, rates, rates_size);
	pos += sizeof(rates_tlv->header) + rates_size;
	nxpwifi_dbg(adapter, INFO, "info: ASSOC_CMD: rates size = %d\n",
		    rates_size);

	/* Add the Authentication type */
	auth_tlv = (struct nxpwifi_ie_types_auth_type *)pos;
	auth_tlv->header.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
	auth_tlv->header.len = cpu_to_le16(sizeof(auth_tlv->auth_type));
	if (priv->sec_info.wep_enabled)
		auth_tlv->auth_type =
			cpu_to_le16((u16)priv->sec_info.authentication_mode);
	else
		auth_tlv->auth_type = cpu_to_le16(NL80211_AUTHTYPE_OPEN_SYSTEM);

	pos += sizeof(auth_tlv->header) + le16_to_cpu(auth_tlv->header.len);

	if (priv->sec_info.authentication_mode == WLAN_AUTH_SAE) {
		auth_tlv->auth_type = cpu_to_le16(NXPWIFI_AUTHTYPE_SAE);
		if (bss_desc->bcn_rsnx_ie &&
		    bss_desc->bcn_rsnx_ie->datalen &&
		    (bss_desc->bcn_rsnx_ie->data[0] &
		     WLAN_RSNX_CAPA_SAE_H2E)) {
			sae_pwe_tlv =
				(struct nxpwifi_ie_types_sae_pwe_mode *)pos;
			sae_pwe_tlv->header.type =
				cpu_to_le16(TLV_TYPE_SAE_PWE_MODE);
			sae_pwe_tlv->header.len =
				cpu_to_le16(sizeof(sae_pwe_tlv->pwe[0]));
			sae_pwe_tlv->pwe[0] = bss_desc->bcn_rsnx_ie->data[0];
			pos += sizeof(sae_pwe_tlv->header) +
				sizeof(sae_pwe_tlv->pwe[0]);
		}
	}

	if (IS_SUPPORT_MULTI_BANDS(adapter) &&
	    !(ISSUPP_11NENABLED(adapter->fw_cap_info) &&
	    !bss_desc->disable_11n &&
	    (priv->config_bands & BAND_GN ||
	     priv->config_bands & BAND_AN) &&
	    bss_desc->bcn_ht_cap)) {
		/*
		 * Append a channel TLV for the channel the attempted AP was
		 * found on
		 */
		chan_tlv = (struct nxpwifi_ie_types_chan_list_param_set *)pos;
		chan_tlv->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
		chan_tlv->header.len =
			cpu_to_le16(sizeof(struct nxpwifi_chan_scan_param_set));

		memset(chan_tlv->chan_scan_param, 0x00,
		       sizeof(struct nxpwifi_chan_scan_param_set));
		chan_tlv->chan_scan_param[0].chan_number =
			(bss_desc->phy_param_set.ds_param_set.current_chan);
		nxpwifi_dbg(adapter, INFO, "info: Assoc: TLV Chan = %d\n",
			    chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].band_cfg =
			nxpwifi_band_to_radio_type((u8)bss_desc->bss_band);

		nxpwifi_dbg(adapter, INFO, "info: Assoc: TLV Band = %d\n",
			    chan_tlv->chan_scan_param[0].band_cfg);
		pos += sizeof(chan_tlv->header) +
			sizeof(struct nxpwifi_chan_scan_param_set);
	}

	if (!priv->wps.session_enable) {
		if (priv->sec_info.wpa_enabled || priv->sec_info.wpa2_enabled)
			rsn_ie_len = nxpwifi_append_rsn_ie_wpa_wpa2(priv, &pos);

		if (rsn_ie_len == -ENOMEM)
			return -ENOMEM;
	}

	if (ISSUPP_11NENABLED(adapter->fw_cap_info) &&
	    !bss_desc->disable_11n &&
	    (priv->config_bands & BAND_GN ||
	     priv->config_bands & BAND_AN))
		nxpwifi_cmd_append_11n_tlv(priv, bss_desc, &pos);

	if (ISSUPP_11ACENABLED(adapter->fw_cap_info) &&
	    !bss_desc->disable_11n && !bss_desc->disable_11ac &&
	    (priv->config_bands & BAND_GAC ||
	     priv->config_bands & BAND_AAC))
		nxpwifi_cmd_append_11ac_tlv(priv, bss_desc, &pos);

	if (ISSUPP_11AXENABLED(adapter->fw_cap_ext) &&
	    nxpwifi_11ax_bandconfig_allowed(priv, bss_desc))
		nxpwifi_cmd_append_11ax_tlv(priv, bss_desc, &pos);

	/* Append vendor specific element TLV */
	nxpwifi_cmd_append_vsie_tlv(priv, NXPWIFI_VSIE_MASK_ASSOC, &pos);

	nxpwifi_wmm_process_association_req(priv, &pos, &bss_desc->wmm_ie,
					    bss_desc->bcn_ht_cap);

	if (priv->wps.session_enable && priv->wps_ie_len)
		nxpwifi_cmd_append_wps_ie(priv, &pos);

	nxpwifi_cmd_append_generic_ie(priv, &pos);

	nxpwifi_cmd_append_tsf_tlv(priv, &pos, bss_desc);

	nxpwifi_11h_process_join(priv, &pos, bss_desc);

	cmd->size = cpu_to_le16((u16)(pos - (u8 *)assoc) + S_DS_GEN);

	/* Set the Capability info at last */
	tmp_cap = bss_desc->cap_info_bitmap;

	if (priv->config_bands == BAND_B)
		tmp_cap &= ~WLAN_CAPABILITY_SHORT_SLOT_TIME;

	tmp_cap &= CAPINFO_MASK;
	nxpwifi_dbg(adapter, INFO,
		    "info: ASSOC_CMD: tmp_cap=%4X CAPINFO_MASK=%4lX\n",
		    tmp_cap, CAPINFO_MASK);
	assoc->cap_info_bitmap = cpu_to_le16(tmp_cap);

	return ret;
}

static const char *assoc_failure_reason_to_str(u16 cap_info)
{
	switch (cap_info) {
	case CONNECT_ERR_AUTH_ERR_STA_FAILURE:
		return "CONNECT_ERR_AUTH_ERR_STA_FAILURE";
	case CONNECT_ERR_AUTH_MSG_UNHANDLED:
		return "CONNECT_ERR_AUTH_MSG_UNHANDLED";
	case CONNECT_ERR_ASSOC_ERR_TIMEOUT:
		return "CONNECT_ERR_ASSOC_ERR_TIMEOUT";
	case CONNECT_ERR_ASSOC_ERR_AUTH_REFUSED:
		return "CONNECT_ERR_ASSOC_ERR_AUTH_REFUSED";
	case CONNECT_ERR_STA_FAILURE:
		return "CONNECT_ERR_STA_FAILURE";
	}

	return "Unknown connect failure";
}

/*
 * Handle association command response.
 * Parse cap_info/status_code/AID and copy IEs, update connection state,
 * WMM and HT/HE parameters, queues and filters. On failure, return the
 * IEEE status code or a mapped timeout error.
 */
int nxpwifi_ret_802_11_associate(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *resp)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret = 0;
	struct ieee_types_assoc_rsp *assoc_rsp;
	struct nxpwifi_bssdescriptor *bss_desc;
	bool enable_data = true;
	u16 cap_info, status_code, aid;
	const u8 *ie_ptr;
	struct ieee80211_ht_operation *assoc_resp_ht_oper;
	struct ieee80211_mgmt *hdr;

	if (!priv->attempted_bss_desc) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: failed, association terminated by host\n",
			    __func__);
		goto done;
	}

	hdr = (struct ieee80211_mgmt *)&resp->params;
	if (!memcmp(hdr->bssid, priv->attempted_bss_desc->mac_address,
		    ETH_ALEN))
		assoc_rsp = (struct ieee_types_assoc_rsp *)&hdr->u.assoc_resp;
	else
		assoc_rsp = (struct ieee_types_assoc_rsp *)&resp->params;

	cap_info = le16_to_cpu(assoc_rsp->cap_info_bitmap);
	status_code = le16_to_cpu(assoc_rsp->status_code);
	aid = le16_to_cpu(assoc_rsp->a_id);

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		nxpwifi_dbg(adapter, ERROR,
			    "invalid AID value 0x%x; bits 15:14 not set\n", aid);

	aid &= ~(BIT(15) | BIT(14));

	priv->assoc_rsp_size = min(le16_to_cpu(resp->size) - S_DS_GEN,
				   sizeof(priv->assoc_rsp_buf));

	assoc_rsp->a_id = cpu_to_le16(aid);
	memcpy(priv->assoc_rsp_buf, &resp->params, priv->assoc_rsp_size);

	if (status_code) {
		adapter->dbg.num_cmd_assoc_failure++;
		nxpwifi_dbg(adapter, ERROR,
			    "ASSOC_RESP: failed,\t"
			    "status code=%d err=%#x a_id=%#x\n",
			    status_code, cap_info,
			    le16_to_cpu(assoc_rsp->a_id));

		nxpwifi_dbg(adapter, ERROR, "assoc failure: reason %s\n",
			    assoc_failure_reason_to_str(cap_info));
		if (cap_info == CONNECT_ERR_ASSOC_ERR_TIMEOUT) {
			if (status_code == NXPWIFI_ASSOC_CMD_FAILURE_AUTH) {
				ret = WLAN_STATUS_AUTH_TIMEOUT;
				nxpwifi_dbg(adapter, ERROR,
					    "ASSOC_RESP: AUTH timeout\n");
			} else {
				ret = WLAN_STATUS_UNSPECIFIED_FAILURE;
				nxpwifi_dbg(adapter, ERROR,
					    "ASSOC_RESP: UNSPECIFIED failure\n");
			}

			priv->assoc_rsp_size = 0;
		} else {
			ret = status_code;
		}

		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->media_connected = true;

	adapter->ps_state = PS_STATE_AWAKE;
	adapter->pps_uapsd_mode = false;
	adapter->tx_lock_flag = false;

	/* Set the attempted BSSID Index to current */
	bss_desc = priv->attempted_bss_desc;

	nxpwifi_dbg(adapter, INFO, "info: ASSOC_RESP: %s\n",
		    bss_desc->ssid.ssid);

	/* Make a copy of current BSSID descriptor */
	memcpy(&priv->curr_bss_params.bss_descriptor,
	       bss_desc, sizeof(struct nxpwifi_bssdescriptor));

	/* Update curr_bss_params */
	priv->curr_bss_params.bss_descriptor.channel =
		bss_desc->phy_param_set.ds_param_set.current_chan;

	priv->curr_bss_params.band = (u8)bss_desc->bss_band;

	if (bss_desc->wmm_ie.element_id == WLAN_EID_VENDOR_SPECIFIC)
		priv->curr_bss_params.wmm_enabled = true;
	else
		priv->curr_bss_params.wmm_enabled = false;

	if ((priv->wmm_required || bss_desc->bcn_ht_cap) &&
	    priv->curr_bss_params.wmm_enabled)
		priv->wmm_enabled = true;
	else
		priv->wmm_enabled = false;

	priv->curr_bss_params.wmm_uapsd_enabled = false;

	priv->curr_bss_params.wmm_uapsd_enabled = priv->wmm_enabled &&
		(bss_desc->wmm_ie.qos_info & IEEE80211_WMM_IE_AP_QOSINFO_UAPSD);

	/* Store the bandwidth information from assoc response */
	ie_ptr = cfg80211_find_ie(WLAN_EID_HT_OPERATION, assoc_rsp->ie_buffer,
				  priv->assoc_rsp_size
				  - sizeof(struct ieee_types_assoc_rsp));
	if (ie_ptr) {
		assoc_resp_ht_oper = (struct ieee80211_ht_operation *)(ie_ptr
					+ sizeof(struct element));
		priv->assoc_resp_ht_param = assoc_resp_ht_oper->ht_param;
		priv->ht_param_present = true;
	} else {
		priv->ht_param_present = false;
	}

	nxpwifi_dbg(adapter, INFO,
		    "info: ASSOC_RESP: curr_pkt_filter is %#x\n",
		    priv->curr_pkt_filter);
	if (priv->sec_info.wpa_enabled || priv->sec_info.wpa2_enabled)
		priv->wpa_is_gtk_set = false;

	if (priv->wmm_enabled) {
		/* Don't re-enable carrier until we get the WMM_GET_STATUS event */
		enable_data = false;
	} else {
		/* Since WMM is not enabled, setup the queues with the defaults */
		nxpwifi_wmm_setup_queue_priorities(priv, NULL);
		nxpwifi_wmm_setup_ac_downgrade(priv);
	}

	if (enable_data)
		nxpwifi_dbg(adapter, INFO,
			    "info: post association, re-enabling data flow\n");

	/* Reset SNR/NF/RSSI values */
	priv->data_rssi_last = 0;
	priv->data_nf_last = 0;
	priv->data_rssi_avg = 0;
	priv->data_nf_avg = 0;
	priv->bcn_rssi_last = 0;
	priv->bcn_nf_last = 0;
	priv->bcn_rssi_avg = 0;
	priv->bcn_nf_avg = 0;
	priv->rxpd_rate = 0;
	priv->rxpd_htinfo = 0;

	nxpwifi_save_curr_bcn(priv);

	adapter->dbg.num_cmd_assoc_success++;

	nxpwifi_dbg(adapter, MSG, "assoc: associated with %pM\n",
		    priv->attempted_bss_desc->mac_address);

	/* Add the ra_list here for infra mode as there will be only 1 ra always */
	nxpwifi_ralist_add(priv,
			   priv->curr_bss_params.bss_descriptor.mac_address);

	netif_carrier_on(priv->netdev);
	nxpwifi_wake_up_net_dev_queue(priv->netdev, adapter);

	if (priv->sec_info.wpa_enabled || priv->sec_info.wpa2_enabled)
		priv->scan_block = true;
	else
		priv->port_open = true;

done:
	/* Need to indicate IOCTL complete */
	if (adapter->curr_cmd->wait_q_enabled) {
		if (ret)
			adapter->cmd_wait_q.status = -1;
		else
			adapter->cmd_wait_q.status = 0;
	}

	return ret;
}

/* Associate to the specified BSS (STA only) */
int nxpwifi_associate(struct nxpwifi_private *priv,
		      struct nxpwifi_bssdescriptor *bss_desc)
{
	/*
	 * Return error if the adapter is not STA role or table entry
	 * is not marked as infra.
	 */
	if ((GET_BSS_ROLE(priv) != NXPWIFI_BSS_ROLE_STA) ||
	    bss_desc->bss_mode != NL80211_IFTYPE_STATION)
		return -EINVAL;

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info) &&
	    !bss_desc->disable_11n && !bss_desc->disable_11ac &&
	    priv->config_bands & BAND_AAC)
		nxpwifi_set_11ac_ba_params(priv);
	else
		nxpwifi_set_ba_params(priv);

	/*
	 * Clear any past association response stored for application
	 * retrieval
	 */
	priv->assoc_rsp_size = 0;

	return nxpwifi_send_cmd(priv, HOST_CMD_802_11_ASSOCIATE,
				HOST_ACT_GEN_SET, 0, bss_desc, true);
}

/* Send deauth to disconnect from an infrastructure BSS */
static int nxpwifi_deauthenticate_infra(struct nxpwifi_private *priv, u8 *mac)
{
	u8 mac_address[ETH_ALEN];
	int ret;

	if (!mac || is_zero_ether_addr(mac))
		memcpy(mac_address,
		       priv->curr_bss_params.bss_descriptor.mac_address,
		       ETH_ALEN);
	else
		memcpy(mac_address, mac, ETH_ALEN);

	ret = nxpwifi_send_cmd(priv, HOST_CMD_802_11_DEAUTHENTICATE,
			       HOST_ACT_GEN_SET, 0, mac_address, true);

	return ret;
}

/* Disconnect from the current BSS (STA/P2P/AP) */
int nxpwifi_deauthenticate(struct nxpwifi_private *priv, u8 *mac)
{
	int ret = 0;

	if (!priv->media_connected)
		return 0;

	priv->auth_flag = 0;
	priv->auth_alg = WLAN_AUTH_NONE;
	priv->host_mlme_reg = false;
	priv->mgmt_frame_mask = 0;

	ret = nxpwifi_send_cmd(priv, HOST_CMD_MGMT_FRAME_REG,
			       HOST_ACT_GEN_SET, 0,
			       &priv->mgmt_frame_mask, false);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "could not unregister mgmt frame rx\n");
		return ret;
	}

	switch (priv->bss_mode) {
	case NL80211_IFTYPE_STATION:
		ret = nxpwifi_deauthenticate_infra(priv, mac);
		if (ret)
			cfg80211_disconnected(priv->netdev, 0, NULL, 0,
					      true, GFP_KERNEL);
		break;
	case NL80211_IFTYPE_AP:
		ret = nxpwifi_send_cmd(priv, HOST_CMD_UAP_BSS_STOP,
				       HOST_ACT_GEN_SET, 0, NULL, true);
		break;
	default:
		break;
	}

	return ret;
}

/* Deauthenticate/disconnect from all BSS. */
void nxpwifi_deauthenticate_all(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_private *priv;
	int i;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		nxpwifi_deauthenticate(priv, NULL);
	}
}
EXPORT_SYMBOL_GPL(nxpwifi_deauthenticate_all);

/* Convert band to radio type used in channel TLV. */
u8 nxpwifi_band_to_radio_type(u16 config_bands)
{
	if (config_bands & BAND_A || config_bands & BAND_AN ||
	    config_bands & BAND_AAC || config_bands & BAND_AAX)
		return HOST_SCAN_RADIO_TYPE_A;

	return HOST_SCAN_RADIO_TYPE_BG;
}
