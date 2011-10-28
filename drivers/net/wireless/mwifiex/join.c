/*
 * Marvell Wireless LAN device driver: association and ad-hoc start/join
 *
 * Copyright (C) 2011, Marvell International Ltd.
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

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"

#define CAPINFO_MASK    (~(BIT(15) | BIT(14) | BIT(12) | BIT(11) | BIT(9)))

/*
 * Append a generic IE as a pass through TLV to a TLV buffer.
 *
 * This function is called from the network join command preparation routine.
 *
 * If the IE buffer has been setup by the application, this routine appends
 * the buffer as a pass through TLV type to the request.
 */
static int
mwifiex_cmd_append_generic_ie(struct mwifiex_private *priv, u8 **buffer)
{
	int ret_len = 0;
	struct mwifiex_ie_types_header ie_header;

	/* Null Checks */
	if (!buffer)
		return 0;
	if (!(*buffer))
		return 0;

	/*
	 * If there is a generic ie buffer setup, append it to the return
	 *   parameter buffer pointer.
	 */
	if (priv->gen_ie_buf_len) {
		dev_dbg(priv->adapter->dev, "info: %s: append generic %d to %p\n",
				__func__, priv->gen_ie_buf_len, *buffer);

		/* Wrap the generic IE buffer with a pass through TLV type */
		ie_header.type = cpu_to_le16(TLV_TYPE_PASSTHROUGH);
		ie_header.len = cpu_to_le16(priv->gen_ie_buf_len);
		memcpy(*buffer, &ie_header, sizeof(ie_header));

		/* Increment the return size and the return buffer pointer
		   param */
		*buffer += sizeof(ie_header);
		ret_len += sizeof(ie_header);

		/* Copy the generic IE buffer to the output buffer, advance
		   pointer */
		memcpy(*buffer, priv->gen_ie_buf, priv->gen_ie_buf_len);

		/* Increment the return size and the return buffer pointer
		   param */
		*buffer += priv->gen_ie_buf_len;
		ret_len += priv->gen_ie_buf_len;

		/* Reset the generic IE buffer */
		priv->gen_ie_buf_len = 0;
	}

	/* return the length appended to the buffer */
	return ret_len;
}

/*
 * Append TSF tracking info from the scan table for the target AP.
 *
 * This function is called from the network join command preparation routine.
 *
 * The TSF table TSF sent to the firmware contains two TSF values:
 *      - The TSF of the target AP from its previous beacon/probe response
 *      - The TSF timestamp of our local MAC at the time we observed the
 *        beacon/probe response.
 *
 * The firmware uses the timestamp values to set an initial TSF value
 * in the MAC for the new association after a reassociation attempt.
 */
static int
mwifiex_cmd_append_tsf_tlv(struct mwifiex_private *priv, u8 **buffer,
			   struct mwifiex_bssdescriptor *bss_desc)
{
	struct mwifiex_ie_types_tsf_timestamp tsf_tlv;
	__le64 tsf_val;

	/* Null Checks */
	if (buffer == NULL)
		return 0;
	if (*buffer == NULL)
		return 0;

	memset(&tsf_tlv, 0x00, sizeof(struct mwifiex_ie_types_tsf_timestamp));

	tsf_tlv.header.type = cpu_to_le16(TLV_TYPE_TSFTIMESTAMP);
	tsf_tlv.header.len = cpu_to_le16(2 * sizeof(tsf_val));

	memcpy(*buffer, &tsf_tlv, sizeof(tsf_tlv.header));
	*buffer += sizeof(tsf_tlv.header);

	/* TSF at the time when beacon/probe_response was received */
	tsf_val = cpu_to_le64(bss_desc->network_tsf);
	memcpy(*buffer, &tsf_val, sizeof(tsf_val));
	*buffer += sizeof(tsf_val);

	memcpy(&tsf_val, bss_desc->time_stamp, sizeof(tsf_val));

	dev_dbg(priv->adapter->dev, "info: %s: TSF offset calc: %016llx - "
			"%016llx\n", __func__, tsf_val, bss_desc->network_tsf);

	memcpy(*buffer, &tsf_val, sizeof(tsf_val));
	*buffer += sizeof(tsf_val);

	return sizeof(tsf_tlv.header) + (2 * sizeof(tsf_val));
}

/*
 * This function finds out the common rates between rate1 and rate2.
 *
 * It will fill common rates in rate1 as output if found.
 *
 * NOTE: Setting the MSB of the basic rates needs to be taken
 * care of, either before or after calling this function.
 */
static int mwifiex_get_common_rates(struct mwifiex_private *priv, u8 *rate1,
				    u32 rate1_size, u8 *rate2, u32 rate2_size)
{
	int ret;
	u8 *ptr = rate1, *tmp;
	u32 i, j;

	tmp = kmalloc(rate1_size, GFP_KERNEL);
	if (!tmp) {
		dev_err(priv->adapter->dev, "failed to alloc tmp buf\n");
		return -ENOMEM;
	}

	memcpy(tmp, rate1, rate1_size);
	memset(rate1, 0, rate1_size);

	for (i = 0; rate2[i] && i < rate2_size; i++) {
		for (j = 0; tmp[j] && j < rate1_size; j++) {
			/* Check common rate, excluding the bit for
			   basic rate */
			if ((rate2[i] & 0x7F) == (tmp[j] & 0x7F)) {
				*rate1++ = tmp[j];
				break;
			}
		}
	}

	dev_dbg(priv->adapter->dev, "info: Tx data rate set to %#x\n",
						priv->data_rate);

	if (!priv->is_data_rate_auto) {
		while (*ptr) {
			if ((*ptr & 0x7f) == priv->data_rate) {
				ret = 0;
				goto done;
			}
			ptr++;
		}
		dev_err(priv->adapter->dev, "previously set fixed data rate %#x"
			" is not compatible with the network\n",
			priv->data_rate);

		ret = -1;
		goto done;
	}

	ret = 0;
done:
	kfree(tmp);
	return ret;
}

/*
 * This function creates the intersection of the rates supported by a
 * target BSS and our adapter settings for use in an assoc/join command.
 */
static int
mwifiex_setup_rates_from_bssdesc(struct mwifiex_private *priv,
				 struct mwifiex_bssdescriptor *bss_desc,
				 u8 *out_rates, u32 *out_rates_size)
{
	u8 card_rates[MWIFIEX_SUPPORTED_RATES];
	u32 card_rates_size;

	/* Copy AP supported rates */
	memcpy(out_rates, bss_desc->supported_rates, MWIFIEX_SUPPORTED_RATES);
	/* Get the STA supported rates */
	card_rates_size = mwifiex_get_active_data_rates(priv, card_rates);
	/* Get the common rates between AP and STA supported rates */
	if (mwifiex_get_common_rates(priv, out_rates, MWIFIEX_SUPPORTED_RATES,
				     card_rates, card_rates_size)) {
		*out_rates_size = 0;
		dev_err(priv->adapter->dev, "%s: cannot get common rates\n",
						__func__);
		return -1;
	}

	*out_rates_size =
		min_t(size_t, strlen(out_rates), MWIFIEX_SUPPORTED_RATES);

	return 0;
}

/*
 * This function updates the scan entry TSF timestamps to reflect
 * a new association.
 */
static void
mwifiex_update_tsf_timestamps(struct mwifiex_private *priv,
			      struct mwifiex_bssdescriptor *new_bss_desc)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u32 table_idx;
	long long new_tsf_base;
	signed long long tsf_delta;

	memcpy(&new_tsf_base, new_bss_desc->time_stamp, sizeof(new_tsf_base));

	tsf_delta = new_tsf_base - new_bss_desc->network_tsf;

	dev_dbg(adapter->dev, "info: TSF: update TSF timestamps, "
		"0x%016llx -> 0x%016llx\n",
	       new_bss_desc->network_tsf, new_tsf_base);

	for (table_idx = 0; table_idx < adapter->num_in_scan_table;
	     table_idx++)
		adapter->scan_table[table_idx].network_tsf += tsf_delta;
}

/*
 * This function appends a WAPI IE.
 *
 * This function is called from the network join command preparation routine.
 *
 * If the IE buffer has been setup by the application, this routine appends
 * the buffer as a WAPI TLV type to the request.
 */
static int
mwifiex_cmd_append_wapi_ie(struct mwifiex_private *priv, u8 **buffer)
{
	int retLen = 0;
	struct mwifiex_ie_types_header ie_header;

	/* Null Checks */
	if (buffer == NULL)
		return 0;
	if (*buffer == NULL)
		return 0;

	/*
	 * If there is a wapi ie buffer setup, append it to the return
	 *   parameter buffer pointer.
	 */
	if (priv->wapi_ie_len) {
		dev_dbg(priv->adapter->dev, "cmd: append wapi ie %d to %p\n",
				priv->wapi_ie_len, *buffer);

		/* Wrap the generic IE buffer with a pass through TLV type */
		ie_header.type = cpu_to_le16(TLV_TYPE_WAPI_IE);
		ie_header.len = cpu_to_le16(priv->wapi_ie_len);
		memcpy(*buffer, &ie_header, sizeof(ie_header));

		/* Increment the return size and the return buffer pointer
		   param */
		*buffer += sizeof(ie_header);
		retLen += sizeof(ie_header);

		/* Copy the wapi IE buffer to the output buffer, advance
		   pointer */
		memcpy(*buffer, priv->wapi_ie, priv->wapi_ie_len);

		/* Increment the return size and the return buffer pointer
		   param */
		*buffer += priv->wapi_ie_len;
		retLen += priv->wapi_ie_len;

	}
	/* return the length appended to the buffer */
	return retLen;
}

/*
 * This function appends rsn ie tlv for wpa/wpa2 security modes.
 * It is called from the network join command preparation routine.
 */
static int mwifiex_append_rsn_ie_wpa_wpa2(struct mwifiex_private *priv,
					  u8 **buffer)
{
	struct mwifiex_ie_types_rsn_param_set *rsn_ie_tlv;
	int rsn_ie_len;

	if (!buffer || !(*buffer))
		return 0;

	rsn_ie_tlv = (struct mwifiex_ie_types_rsn_param_set *) (*buffer);
	rsn_ie_tlv->header.type = cpu_to_le16((u16) priv->wpa_ie[0]);
	rsn_ie_tlv->header.type = cpu_to_le16(
				 le16_to_cpu(rsn_ie_tlv->header.type) & 0x00FF);
	rsn_ie_tlv->header.len = cpu_to_le16((u16) priv->wpa_ie[1]);
	rsn_ie_tlv->header.len = cpu_to_le16(le16_to_cpu(rsn_ie_tlv->header.len)
							& 0x00FF);
	if (le16_to_cpu(rsn_ie_tlv->header.len) <= (sizeof(priv->wpa_ie) - 2))
		memcpy(rsn_ie_tlv->rsn_ie, &priv->wpa_ie[2],
					le16_to_cpu(rsn_ie_tlv->header.len));
	else
		return -1;

	rsn_ie_len = sizeof(rsn_ie_tlv->header) +
					le16_to_cpu(rsn_ie_tlv->header.len);
	*buffer += rsn_ie_len;

	return rsn_ie_len;
}

/*
 * This function prepares command for association.
 *
 * This sets the following parameters -
 *      - Peer MAC address
 *      - Listen interval
 *      - Beacon interval
 *      - Capability information
 *
 * ...and the following TLVs, as required -
 *      - SSID TLV
 *      - PHY TLV
 *      - SS TLV
 *      - Rates TLV
 *      - Authentication TLV
 *      - Channel TLV
 *      - WPA/WPA2 IE
 *      - 11n TLV
 *      - Vendor specific TLV
 *      - WMM TLV
 *      - WAPI IE
 *      - Generic IE
 *      - TSF TLV
 *
 * Preparation also includes -
 *      - Setting command ID and proper size
 *      - Ensuring correct endian-ness
 */
int mwifiex_cmd_802_11_associate(struct mwifiex_private *priv,
				 struct host_cmd_ds_command *cmd,
				 void *data_buf)
{
	struct host_cmd_ds_802_11_associate *assoc = &cmd->params.associate;
	struct mwifiex_bssdescriptor *bss_desc;
	struct mwifiex_ie_types_ssid_param_set *ssid_tlv;
	struct mwifiex_ie_types_phy_param_set *phy_tlv;
	struct mwifiex_ie_types_ss_param_set *ss_tlv;
	struct mwifiex_ie_types_rates_param_set *rates_tlv;
	struct mwifiex_ie_types_auth_type *auth_tlv;
	struct mwifiex_ie_types_chan_list_param_set *chan_tlv;
	u8 rates[MWIFIEX_SUPPORTED_RATES];
	u32 rates_size;
	u16 tmp_cap;
	u8 *pos;
	int rsn_ie_len = 0;

	bss_desc = (struct mwifiex_bssdescriptor *) data_buf;
	pos = (u8 *) assoc;

	mwifiex_cfg_tx_buf(priv, bss_desc);

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_ASSOCIATE);

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

	ssid_tlv = (struct mwifiex_ie_types_ssid_param_set *) pos;
	ssid_tlv->header.type = cpu_to_le16(WLAN_EID_SSID);
	ssid_tlv->header.len = cpu_to_le16((u16) bss_desc->ssid.ssid_len);
	memcpy(ssid_tlv->ssid, bss_desc->ssid.ssid,
		le16_to_cpu(ssid_tlv->header.len));
	pos += sizeof(ssid_tlv->header) + le16_to_cpu(ssid_tlv->header.len);

	phy_tlv = (struct mwifiex_ie_types_phy_param_set *) pos;
	phy_tlv->header.type = cpu_to_le16(WLAN_EID_DS_PARAMS);
	phy_tlv->header.len = cpu_to_le16(sizeof(phy_tlv->fh_ds.ds_param_set));
	memcpy(&phy_tlv->fh_ds.ds_param_set,
	       &bss_desc->phy_param_set.ds_param_set.current_chan,
	       sizeof(phy_tlv->fh_ds.ds_param_set));
	pos += sizeof(phy_tlv->header) + le16_to_cpu(phy_tlv->header.len);

	ss_tlv = (struct mwifiex_ie_types_ss_param_set *) pos;
	ss_tlv->header.type = cpu_to_le16(WLAN_EID_CF_PARAMS);
	ss_tlv->header.len = cpu_to_le16(sizeof(ss_tlv->cf_ibss.cf_param_set));
	pos += sizeof(ss_tlv->header) + le16_to_cpu(ss_tlv->header.len);

	/* Get the common rates supported between the driver and the BSS Desc */
	if (mwifiex_setup_rates_from_bssdesc
	    (priv, bss_desc, rates, &rates_size))
		return -1;

	/* Save the data rates into Current BSS state structure */
	priv->curr_bss_params.num_of_rates = rates_size;
	memcpy(&priv->curr_bss_params.data_rates, rates, rates_size);

	/* Setup the Rates TLV in the association command */
	rates_tlv = (struct mwifiex_ie_types_rates_param_set *) pos;
	rates_tlv->header.type = cpu_to_le16(WLAN_EID_SUPP_RATES);
	rates_tlv->header.len = cpu_to_le16((u16) rates_size);
	memcpy(rates_tlv->rates, rates, rates_size);
	pos += sizeof(rates_tlv->header) + rates_size;
	dev_dbg(priv->adapter->dev, "info: ASSOC_CMD: rates size = %d\n",
					rates_size);

	/* Add the Authentication type to be used for Auth frames */
	auth_tlv = (struct mwifiex_ie_types_auth_type *) pos;
	auth_tlv->header.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
	auth_tlv->header.len = cpu_to_le16(sizeof(auth_tlv->auth_type));
	if (priv->sec_info.wep_status == MWIFIEX_802_11_WEP_ENABLED)
		auth_tlv->auth_type = cpu_to_le16(
				(u16) priv->sec_info.authentication_mode);
	else
		auth_tlv->auth_type = cpu_to_le16(NL80211_AUTHTYPE_OPEN_SYSTEM);

	pos += sizeof(auth_tlv->header) + le16_to_cpu(auth_tlv->header.len);

	if (IS_SUPPORT_MULTI_BANDS(priv->adapter)
	    && !(ISSUPP_11NENABLED(priv->adapter->fw_cap_info)
		&& (!bss_desc->disable_11n)
		 && (priv->adapter->config_bands & BAND_GN
		     || priv->adapter->config_bands & BAND_AN)
		 && (bss_desc->bcn_ht_cap)
	    )
		) {
		/* Append a channel TLV for the channel the attempted AP was
		   found on */
		chan_tlv = (struct mwifiex_ie_types_chan_list_param_set *) pos;
		chan_tlv->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
		chan_tlv->header.len =
			cpu_to_le16(sizeof(struct mwifiex_chan_scan_param_set));

		memset(chan_tlv->chan_scan_param, 0x00,
		       sizeof(struct mwifiex_chan_scan_param_set));
		chan_tlv->chan_scan_param[0].chan_number =
			(bss_desc->phy_param_set.ds_param_set.current_chan);
		dev_dbg(priv->adapter->dev, "info: Assoc: TLV Chan = %d\n",
		       chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].radio_type =
			mwifiex_band_to_radio_type((u8) bss_desc->bss_band);

		dev_dbg(priv->adapter->dev, "info: Assoc: TLV Band = %d\n",
		       chan_tlv->chan_scan_param[0].radio_type);
		pos += sizeof(chan_tlv->header) +
			sizeof(struct mwifiex_chan_scan_param_set);
	}

	if (!priv->wps.session_enable) {
		if (priv->sec_info.wpa_enabled || priv->sec_info.wpa2_enabled)
			rsn_ie_len = mwifiex_append_rsn_ie_wpa_wpa2(priv, &pos);

		if (rsn_ie_len == -1)
			return -1;
	}

	if (ISSUPP_11NENABLED(priv->adapter->fw_cap_info)
		&& (!bss_desc->disable_11n)
	    && (priv->adapter->config_bands & BAND_GN
		|| priv->adapter->config_bands & BAND_AN))
		mwifiex_cmd_append_11n_tlv(priv, bss_desc, &pos);

	/* Append vendor specific IE TLV */
	mwifiex_cmd_append_vsie_tlv(priv, MWIFIEX_VSIE_MASK_ASSOC, &pos);

	mwifiex_wmm_process_association_req(priv, &pos, &bss_desc->wmm_ie,
					    bss_desc->bcn_ht_cap);
	if (priv->sec_info.wapi_enabled && priv->wapi_ie_len)
		mwifiex_cmd_append_wapi_ie(priv, &pos);


	mwifiex_cmd_append_generic_ie(priv, &pos);

	mwifiex_cmd_append_tsf_tlv(priv, &pos, bss_desc);

	cmd->size = cpu_to_le16((u16) (pos - (u8 *) assoc) + S_DS_GEN);

	/* Set the Capability info at last */
	tmp_cap = bss_desc->cap_info_bitmap;

	if (priv->adapter->config_bands == BAND_B)
		tmp_cap &= ~WLAN_CAPABILITY_SHORT_SLOT_TIME;

	tmp_cap &= CAPINFO_MASK;
	dev_dbg(priv->adapter->dev, "info: ASSOC_CMD: tmp_cap=%4X CAPINFO_MASK=%4lX\n",
	       tmp_cap, CAPINFO_MASK);
	assoc->cap_info_bitmap = cpu_to_le16(tmp_cap);

	return 0;
}

/*
 * Association firmware command response handler
 *
 * The response buffer for the association command has the following
 * memory layout.
 *
 * For cases where an association response was not received (indicated
 * by the CapInfo and AId field):
 *
 *     .------------------------------------------------------------.
 *     |  Header(4 * sizeof(t_u16)):  Standard command response hdr |
 *     .------------------------------------------------------------.
 *     |  cap_info/Error Return(t_u16):                             |
 *     |           0xFFFF(-1): Internal error                       |
 *     |           0xFFFE(-2): Authentication unhandled message     |
 *     |           0xFFFD(-3): Authentication refused               |
 *     |           0xFFFC(-4): Timeout waiting for AP response      |
 *     .------------------------------------------------------------.
 *     |  status_code(t_u16):                                       |
 *     |        If cap_info is -1:                                  |
 *     |           An internal firmware failure prevented the       |
 *     |           command from being processed.  The status_code   |
 *     |           will be set to 1.                                |
 *     |                                                            |
 *     |        If cap_info is -2:                                  |
 *     |           An authentication frame was received but was     |
 *     |           not handled by the firmware.  IEEE Status        |
 *     |           code for the failure is returned.                |
 *     |                                                            |
 *     |        If cap_info is -3:                                  |
 *     |           An authentication frame was received and the     |
 *     |           status_code is the IEEE Status reported in the   |
 *     |           response.                                        |
 *     |                                                            |
 *     |        If cap_info is -4:                                  |
 *     |           (1) Association response timeout                 |
 *     |           (2) Authentication response timeout              |
 *     .------------------------------------------------------------.
 *     |  a_id(t_u16): 0xFFFF                                       |
 *     .------------------------------------------------------------.
 *
 *
 * For cases where an association response was received, the IEEE
 * standard association response frame is returned:
 *
 *     .------------------------------------------------------------.
 *     |  Header(4 * sizeof(t_u16)):  Standard command response hdr |
 *     .------------------------------------------------------------.
 *     |  cap_info(t_u16): IEEE Capability                          |
 *     .------------------------------------------------------------.
 *     |  status_code(t_u16): IEEE Status Code                      |
 *     .------------------------------------------------------------.
 *     |  a_id(t_u16): IEEE Association ID                          |
 *     .------------------------------------------------------------.
 *     |  IEEE IEs(variable): Any received IEs comprising the       |
 *     |                      remaining portion of a received       |
 *     |                      association response frame.           |
 *     .------------------------------------------------------------.
 *
 * For simplistic handling, the status_code field can be used to determine
 * an association success (0) or failure (non-zero).
 */
int mwifiex_ret_802_11_associate(struct mwifiex_private *priv,
			     struct host_cmd_ds_command *resp)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	int ret = 0;
	struct ieee_types_assoc_rsp *assoc_rsp;
	struct mwifiex_bssdescriptor *bss_desc;
	u8 enable_data = true;

	assoc_rsp = (struct ieee_types_assoc_rsp *) &resp->params;

	priv->assoc_rsp_size = min(le16_to_cpu(resp->size) - S_DS_GEN,
				     sizeof(priv->assoc_rsp_buf));

	memcpy(priv->assoc_rsp_buf, &resp->params, priv->assoc_rsp_size);

	if (le16_to_cpu(assoc_rsp->status_code)) {
		priv->adapter->dbg.num_cmd_assoc_failure++;
		dev_err(priv->adapter->dev, "ASSOC_RESP: association failed, "
		       "status code = %d, error = 0x%x, a_id = 0x%x\n",
		       le16_to_cpu(assoc_rsp->status_code),
		       le16_to_cpu(assoc_rsp->cap_info_bitmap),
		       le16_to_cpu(assoc_rsp->a_id));

		ret = -1;
		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->media_connected = true;

	priv->adapter->ps_state = PS_STATE_AWAKE;
	priv->adapter->pps_uapsd_mode = false;
	priv->adapter->tx_lock_flag = false;

	/* Set the attempted BSSID Index to current */
	bss_desc = priv->attempted_bss_desc;

	dev_dbg(priv->adapter->dev, "info: ASSOC_RESP: %s\n",
						bss_desc->ssid.ssid);

	/* Make a copy of current BSSID descriptor */
	memcpy(&priv->curr_bss_params.bss_descriptor,
	       bss_desc, sizeof(struct mwifiex_bssdescriptor));

	/* Update curr_bss_params */
	priv->curr_bss_params.bss_descriptor.channel
		= bss_desc->phy_param_set.ds_param_set.current_chan;

	priv->curr_bss_params.band = (u8) bss_desc->bss_band;

	/*
	 * Adjust the timestamps in the scan table to be relative to the newly
	 * associated AP's TSF
	 */
	mwifiex_update_tsf_timestamps(priv, bss_desc);

	if (bss_desc->wmm_ie.vend_hdr.element_id == WLAN_EID_VENDOR_SPECIFIC)
		priv->curr_bss_params.wmm_enabled = true;
	else
		priv->curr_bss_params.wmm_enabled = false;

	if ((priv->wmm_required || bss_desc->bcn_ht_cap)
			&& priv->curr_bss_params.wmm_enabled)
		priv->wmm_enabled = true;
	else
		priv->wmm_enabled = false;

	priv->curr_bss_params.wmm_uapsd_enabled = false;

	if (priv->wmm_enabled)
		priv->curr_bss_params.wmm_uapsd_enabled
			= ((bss_desc->wmm_ie.qos_info_bitmap &
				IEEE80211_WMM_IE_AP_QOSINFO_UAPSD) ? 1 : 0);

	dev_dbg(priv->adapter->dev, "info: ASSOC_RESP: curr_pkt_filter is %#x\n",
	       priv->curr_pkt_filter);
	if (priv->sec_info.wpa_enabled || priv->sec_info.wpa2_enabled)
		priv->wpa_is_gtk_set = false;

	if (priv->wmm_enabled) {
		/* Don't re-enable carrier until we get the WMM_GET_STATUS
		   event */
		enable_data = false;
	} else {
		/* Since WMM is not enabled, setup the queues with the
		   defaults */
		mwifiex_wmm_setup_queue_priorities(priv, NULL);
		mwifiex_wmm_setup_ac_downgrade(priv);
	}

	if (enable_data)
		dev_dbg(priv->adapter->dev,
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

	mwifiex_save_curr_bcn(priv);

	priv->adapter->dbg.num_cmd_assoc_success++;

	dev_dbg(priv->adapter->dev, "info: ASSOC_RESP: associated\n");

	/* Add the ra_list here for infra mode as there will be only 1 ra
	   always */
	mwifiex_ralist_add(priv,
			   priv->curr_bss_params.bss_descriptor.mac_address);

	if (!netif_carrier_ok(priv->netdev))
		netif_carrier_on(priv->netdev);
	if (netif_queue_stopped(priv->netdev))
		netif_wake_queue(priv->netdev);

	if (priv->sec_info.wpa_enabled || priv->sec_info.wpa2_enabled)
		priv->scan_block = true;

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

/*
 * This function prepares command for ad-hoc start.
 *
 * Driver will fill up SSID, BSS mode, IBSS parameters, physical
 * parameters, probe delay, and capability information. Firmware
 * will fill up beacon period, basic rates and operational rates.
 *
 * In addition, the following TLVs are added -
 *      - Channel TLV
 *      - Vendor specific IE
 *      - WPA/WPA2 IE
 *      - HT Capabilities IE
 *      - HT Information IE
 *
 * Preparation also includes -
 *      - Setting command ID and proper size
 *      - Ensuring correct endian-ness
 */
int
mwifiex_cmd_802_11_ad_hoc_start(struct mwifiex_private *priv,
				struct host_cmd_ds_command *cmd, void *data_buf)
{
	int rsn_ie_len = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ad_hoc_start *adhoc_start =
		&cmd->params.adhoc_start;
	struct mwifiex_bssdescriptor *bss_desc;
	u32 cmd_append_size = 0;
	u32 i;
	u16 tmp_cap;
	uint16_t ht_cap_info;
	struct mwifiex_ie_types_chan_list_param_set *chan_tlv;

	struct mwifiex_ie_types_htcap *ht_cap;
	struct mwifiex_ie_types_htinfo *ht_info;
	u8 *pos = (u8 *) adhoc_start +
			sizeof(struct host_cmd_ds_802_11_ad_hoc_start);

	if (!adapter)
		return -1;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_AD_HOC_START);

	bss_desc = &priv->curr_bss_params.bss_descriptor;
	priv->attempted_bss_desc = bss_desc;

	/*
	 * Fill in the parameters for 2 data structures:
	 *   1. struct host_cmd_ds_802_11_ad_hoc_start command
	 *   2. bss_desc
	 * Driver will fill up SSID, bss_mode,IBSS param, Physical Param,
	 * probe delay, and Cap info.
	 * Firmware will fill up beacon period, Basic rates
	 * and operational rates.
	 */

	memset(adhoc_start->ssid, 0, IEEE80211_MAX_SSID_LEN);

	memcpy(adhoc_start->ssid,
	       ((struct mwifiex_802_11_ssid *) data_buf)->ssid,
	       ((struct mwifiex_802_11_ssid *) data_buf)->ssid_len);

	dev_dbg(adapter->dev, "info: ADHOC_S_CMD: SSID = %s\n",
				adhoc_start->ssid);

	memset(bss_desc->ssid.ssid, 0, IEEE80211_MAX_SSID_LEN);
	memcpy(bss_desc->ssid.ssid,
	       ((struct mwifiex_802_11_ssid *) data_buf)->ssid,
	       ((struct mwifiex_802_11_ssid *) data_buf)->ssid_len);

	bss_desc->ssid.ssid_len =
		((struct mwifiex_802_11_ssid *) data_buf)->ssid_len;

	/* Set the BSS mode */
	adhoc_start->bss_mode = HostCmd_BSS_MODE_IBSS;
	bss_desc->bss_mode = NL80211_IFTYPE_ADHOC;
	adhoc_start->beacon_period = cpu_to_le16(priv->beacon_period);
	bss_desc->beacon_period = priv->beacon_period;

	/* Set Physical param set */
/* Parameter IE Id */
#define DS_PARA_IE_ID   3
/* Parameter IE length */
#define DS_PARA_IE_LEN  1

	adhoc_start->phy_param_set.ds_param_set.element_id = DS_PARA_IE_ID;
	adhoc_start->phy_param_set.ds_param_set.len = DS_PARA_IE_LEN;

	if (!mwifiex_get_cfp_by_band_and_channel_from_cfg80211
			(priv, adapter->adhoc_start_band, (u16)
				priv->adhoc_channel)) {
		struct mwifiex_chan_freq_power *cfp;
		cfp = mwifiex_get_cfp_by_band_and_channel_from_cfg80211(priv,
				adapter->adhoc_start_band, FIRST_VALID_CHANNEL);
		if (cfp)
			priv->adhoc_channel = (u8) cfp->channel;
	}

	if (!priv->adhoc_channel) {
		dev_err(adapter->dev, "ADHOC_S_CMD: adhoc_channel cannot be 0\n");
		return -1;
	}

	dev_dbg(adapter->dev, "info: ADHOC_S_CMD: creating ADHOC on channel %d\n",
				priv->adhoc_channel);

	priv->curr_bss_params.bss_descriptor.channel = priv->adhoc_channel;
	priv->curr_bss_params.band = adapter->adhoc_start_band;

	bss_desc->channel = priv->adhoc_channel;
	adhoc_start->phy_param_set.ds_param_set.current_chan =
		priv->adhoc_channel;

	memcpy(&bss_desc->phy_param_set, &adhoc_start->phy_param_set,
	       sizeof(union ieee_types_phy_param_set));

	/* Set IBSS param set */
/* IBSS parameter IE Id */
#define IBSS_PARA_IE_ID   6
/* IBSS parameter IE length */
#define IBSS_PARA_IE_LEN  2

	adhoc_start->ss_param_set.ibss_param_set.element_id = IBSS_PARA_IE_ID;
	adhoc_start->ss_param_set.ibss_param_set.len = IBSS_PARA_IE_LEN;
	adhoc_start->ss_param_set.ibss_param_set.atim_window
		= cpu_to_le16(priv->atim_window);
	memcpy(&bss_desc->ss_param_set, &adhoc_start->ss_param_set,
	       sizeof(union ieee_types_ss_param_set));

	/* Set Capability info */
	bss_desc->cap_info_bitmap |= WLAN_CAPABILITY_IBSS;
	tmp_cap = le16_to_cpu(adhoc_start->cap_info_bitmap);
	tmp_cap &= ~WLAN_CAPABILITY_ESS;
	tmp_cap |= WLAN_CAPABILITY_IBSS;

	/* Set up privacy in bss_desc */
	if (priv->sec_info.encryption_mode) {
		/* Ad-Hoc capability privacy on */
		dev_dbg(adapter->dev,
			"info: ADHOC_S_CMD: wep_status set privacy to WEP\n");
		bss_desc->privacy = MWIFIEX_802_11_PRIV_FILTER_8021X_WEP;
		tmp_cap |= WLAN_CAPABILITY_PRIVACY;
	} else {
		dev_dbg(adapter->dev, "info: ADHOC_S_CMD: wep_status NOT set,"
				" setting privacy to ACCEPT ALL\n");
		bss_desc->privacy = MWIFIEX_802_11_PRIV_FILTER_ACCEPT_ALL;
	}

	memset(adhoc_start->DataRate, 0, sizeof(adhoc_start->DataRate));
	mwifiex_get_active_data_rates(priv, adhoc_start->DataRate);
	if ((adapter->adhoc_start_band & BAND_G) &&
	    (priv->curr_pkt_filter & HostCmd_ACT_MAC_ADHOC_G_PROTECTION_ON)) {
		if (mwifiex_send_cmd_async(priv, HostCmd_CMD_MAC_CONTROL,
					     HostCmd_ACT_GEN_SET, 0,
					     &priv->curr_pkt_filter)) {
			dev_err(adapter->dev,
			       "ADHOC_S_CMD: G Protection config failed\n");
			return -1;
		}
	}
	/* Find the last non zero */
	for (i = 0; i < sizeof(adhoc_start->DataRate) &&
			adhoc_start->DataRate[i];
			i++)
			;

	priv->curr_bss_params.num_of_rates = i;

	/* Copy the ad-hoc creating rates into Current BSS rate structure */
	memcpy(&priv->curr_bss_params.data_rates,
	       &adhoc_start->DataRate, priv->curr_bss_params.num_of_rates);

	dev_dbg(adapter->dev, "info: ADHOC_S_CMD: rates=%02x %02x %02x %02x\n",
	       adhoc_start->DataRate[0], adhoc_start->DataRate[1],
	       adhoc_start->DataRate[2], adhoc_start->DataRate[3]);

	dev_dbg(adapter->dev, "info: ADHOC_S_CMD: AD-HOC Start command is ready\n");

	if (IS_SUPPORT_MULTI_BANDS(adapter)) {
		/* Append a channel TLV */
		chan_tlv = (struct mwifiex_ie_types_chan_list_param_set *) pos;
		chan_tlv->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
		chan_tlv->header.len =
			cpu_to_le16(sizeof(struct mwifiex_chan_scan_param_set));

		memset(chan_tlv->chan_scan_param, 0x00,
		       sizeof(struct mwifiex_chan_scan_param_set));
		chan_tlv->chan_scan_param[0].chan_number =
			(u8) priv->curr_bss_params.bss_descriptor.channel;

		dev_dbg(adapter->dev, "info: ADHOC_S_CMD: TLV Chan = %d\n",
		       chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].radio_type
		       = mwifiex_band_to_radio_type(priv->curr_bss_params.band);
		if (adapter->adhoc_start_band & BAND_GN
		    || adapter->adhoc_start_band & BAND_AN) {
			if (adapter->chan_offset == SEC_CHANNEL_ABOVE)
				chan_tlv->chan_scan_param[0].radio_type |=
					SECOND_CHANNEL_ABOVE;
			else if (adapter->chan_offset == SEC_CHANNEL_BELOW)
				chan_tlv->chan_scan_param[0].radio_type |=
					SECOND_CHANNEL_BELOW;
		}
		dev_dbg(adapter->dev, "info: ADHOC_S_CMD: TLV Band = %d\n",
		       chan_tlv->chan_scan_param[0].radio_type);
		pos += sizeof(chan_tlv->header) +
			sizeof(struct mwifiex_chan_scan_param_set);
		cmd_append_size +=
			sizeof(chan_tlv->header) +
			sizeof(struct mwifiex_chan_scan_param_set);
	}

	/* Append vendor specific IE TLV */
	cmd_append_size += mwifiex_cmd_append_vsie_tlv(priv,
				MWIFIEX_VSIE_MASK_ADHOC, &pos);

	if (priv->sec_info.wpa_enabled) {
		rsn_ie_len = mwifiex_append_rsn_ie_wpa_wpa2(priv, &pos);
		if (rsn_ie_len == -1)
			return -1;
		cmd_append_size += rsn_ie_len;
	}

	if (adapter->adhoc_11n_enabled) {
		{
			ht_cap = (struct mwifiex_ie_types_htcap *) pos;
			memset(ht_cap, 0,
			       sizeof(struct mwifiex_ie_types_htcap));
			ht_cap->header.type =
				cpu_to_le16(WLAN_EID_HT_CAPABILITY);
			ht_cap->header.len =
			       cpu_to_le16(sizeof(struct ieee80211_ht_cap));
			ht_cap_info = le16_to_cpu(ht_cap->ht_cap.cap_info);

			ht_cap_info |= IEEE80211_HT_CAP_SGI_20;
			if (adapter->chan_offset) {
				ht_cap_info |= IEEE80211_HT_CAP_SGI_40;
				ht_cap_info |= IEEE80211_HT_CAP_DSSSCCK40;
				ht_cap_info |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
				SETHT_MCS32(ht_cap->ht_cap.mcs.rx_mask);
			}

			ht_cap->ht_cap.ampdu_params_info
					= IEEE80211_HT_MAX_AMPDU_64K;
			ht_cap->ht_cap.mcs.rx_mask[0] = 0xff;
			pos += sizeof(struct mwifiex_ie_types_htcap);
			cmd_append_size +=
				sizeof(struct mwifiex_ie_types_htcap);
		}
		{
			ht_info = (struct mwifiex_ie_types_htinfo *) pos;
			memset(ht_info, 0,
			       sizeof(struct mwifiex_ie_types_htinfo));
			ht_info->header.type =
				cpu_to_le16(WLAN_EID_HT_INFORMATION);
			ht_info->header.len =
				cpu_to_le16(sizeof(struct ieee80211_ht_info));
			ht_info->ht_info.control_chan =
				(u8) priv->curr_bss_params.bss_descriptor.
				channel;
			if (adapter->chan_offset) {
				ht_info->ht_info.ht_param =
					adapter->chan_offset;
				ht_info->ht_info.ht_param |=
					IEEE80211_HT_PARAM_CHAN_WIDTH_ANY;
			}
			ht_info->ht_info.operation_mode =
			     cpu_to_le16(IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
			ht_info->ht_info.basic_set[0] = 0xff;
			pos += sizeof(struct mwifiex_ie_types_htinfo);
			cmd_append_size +=
				sizeof(struct mwifiex_ie_types_htinfo);
		}
	}

	cmd->size = cpu_to_le16((u16)
			    (sizeof(struct host_cmd_ds_802_11_ad_hoc_start)
			     + S_DS_GEN + cmd_append_size));

	if (adapter->adhoc_start_band == BAND_B)
		tmp_cap &= ~WLAN_CAPABILITY_SHORT_SLOT_TIME;
	else
		tmp_cap |= WLAN_CAPABILITY_SHORT_SLOT_TIME;

	adhoc_start->cap_info_bitmap = cpu_to_le16(tmp_cap);

	return 0;
}

/*
 * This function prepares command for ad-hoc join.
 *
 * Most of the parameters are set up by copying from the target BSS descriptor
 * from the scan response.
 *
 * In addition, the following TLVs are added -
 *      - Channel TLV
 *      - Vendor specific IE
 *      - WPA/WPA2 IE
 *      - 11n IE
 *
 * Preparation also includes -
 *      - Setting command ID and proper size
 *      - Ensuring correct endian-ness
 */
int
mwifiex_cmd_802_11_ad_hoc_join(struct mwifiex_private *priv,
			       struct host_cmd_ds_command *cmd, void *data_buf)
{
	int rsn_ie_len = 0;
	struct host_cmd_ds_802_11_ad_hoc_join *adhoc_join =
		&cmd->params.adhoc_join;
	struct mwifiex_bssdescriptor *bss_desc =
		(struct mwifiex_bssdescriptor *) data_buf;
	struct mwifiex_ie_types_chan_list_param_set *chan_tlv;
	u32 cmd_append_size = 0;
	u16 tmp_cap;
	u32 i, rates_size = 0;
	u16 curr_pkt_filter;
	u8 *pos =
		(u8 *) adhoc_join +
		sizeof(struct host_cmd_ds_802_11_ad_hoc_join);

/* Use G protection */
#define USE_G_PROTECTION        0x02
	if (bss_desc->erp_flags & USE_G_PROTECTION) {
		curr_pkt_filter =
			priv->
			curr_pkt_filter | HostCmd_ACT_MAC_ADHOC_G_PROTECTION_ON;

		if (mwifiex_send_cmd_async(priv, HostCmd_CMD_MAC_CONTROL,
					     HostCmd_ACT_GEN_SET, 0,
					     &curr_pkt_filter)) {
			dev_err(priv->adapter->dev,
			       "ADHOC_J_CMD: G Protection config failed\n");
			return -1;
		}
	}

	priv->attempted_bss_desc = bss_desc;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_AD_HOC_JOIN);

	adhoc_join->bss_descriptor.bss_mode = HostCmd_BSS_MODE_IBSS;

	adhoc_join->bss_descriptor.beacon_period
		= cpu_to_le16(bss_desc->beacon_period);

	memcpy(&adhoc_join->bss_descriptor.bssid,
	       &bss_desc->mac_address, ETH_ALEN);

	memcpy(&adhoc_join->bss_descriptor.ssid,
	       &bss_desc->ssid.ssid, bss_desc->ssid.ssid_len);

	memcpy(&adhoc_join->bss_descriptor.phy_param_set,
	       &bss_desc->phy_param_set,
	       sizeof(union ieee_types_phy_param_set));

	memcpy(&adhoc_join->bss_descriptor.ss_param_set,
	       &bss_desc->ss_param_set, sizeof(union ieee_types_ss_param_set));

	tmp_cap = bss_desc->cap_info_bitmap;

	tmp_cap &= CAPINFO_MASK;

	dev_dbg(priv->adapter->dev, "info: ADHOC_J_CMD: tmp_cap=%4X"
			" CAPINFO_MASK=%4lX\n", tmp_cap, CAPINFO_MASK);

	/* Information on BSSID descriptor passed to FW */
	dev_dbg(priv->adapter->dev, "info: ADHOC_J_CMD: BSSID = %pM, SSID = %s\n",
				adhoc_join->bss_descriptor.bssid,
				adhoc_join->bss_descriptor.ssid);

	for (i = 0; bss_desc->supported_rates[i] &&
			i < MWIFIEX_SUPPORTED_RATES;
			i++)
			;
	rates_size = i;

	/* Copy Data Rates from the Rates recorded in scan response */
	memset(adhoc_join->bss_descriptor.data_rates, 0,
	       sizeof(adhoc_join->bss_descriptor.data_rates));
	memcpy(adhoc_join->bss_descriptor.data_rates,
	       bss_desc->supported_rates, rates_size);

	/* Copy the adhoc join rates into Current BSS state structure */
	priv->curr_bss_params.num_of_rates = rates_size;
	memcpy(&priv->curr_bss_params.data_rates, bss_desc->supported_rates,
	       rates_size);

	/* Copy the channel information */
	priv->curr_bss_params.bss_descriptor.channel = bss_desc->channel;
	priv->curr_bss_params.band = (u8) bss_desc->bss_band;

	if (priv->sec_info.wep_status == MWIFIEX_802_11_WEP_ENABLED
	    || priv->sec_info.wpa_enabled)
		tmp_cap |= WLAN_CAPABILITY_PRIVACY;

	if (IS_SUPPORT_MULTI_BANDS(priv->adapter)) {
		/* Append a channel TLV */
		chan_tlv = (struct mwifiex_ie_types_chan_list_param_set *) pos;
		chan_tlv->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);
		chan_tlv->header.len =
			cpu_to_le16(sizeof(struct mwifiex_chan_scan_param_set));

		memset(chan_tlv->chan_scan_param, 0x00,
		       sizeof(struct mwifiex_chan_scan_param_set));
		chan_tlv->chan_scan_param[0].chan_number =
			(bss_desc->phy_param_set.ds_param_set.current_chan);
		dev_dbg(priv->adapter->dev, "info: ADHOC_J_CMD: TLV Chan = %d\n",
		       chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].radio_type =
			mwifiex_band_to_radio_type((u8) bss_desc->bss_band);

		dev_dbg(priv->adapter->dev, "info: ADHOC_J_CMD: TLV Band = %d\n",
		       chan_tlv->chan_scan_param[0].radio_type);
		pos += sizeof(chan_tlv->header) +
			sizeof(struct mwifiex_chan_scan_param_set);
		cmd_append_size += sizeof(chan_tlv->header) +
			sizeof(struct mwifiex_chan_scan_param_set);
	}

	if (priv->sec_info.wpa_enabled)
		rsn_ie_len = mwifiex_append_rsn_ie_wpa_wpa2(priv, &pos);
	if (rsn_ie_len == -1)
		return -1;
	cmd_append_size += rsn_ie_len;

	if (ISSUPP_11NENABLED(priv->adapter->fw_cap_info))
		cmd_append_size += mwifiex_cmd_append_11n_tlv(priv,
			bss_desc, &pos);

	/* Append vendor specific IE TLV */
	cmd_append_size += mwifiex_cmd_append_vsie_tlv(priv,
			MWIFIEX_VSIE_MASK_ADHOC, &pos);

	cmd->size = cpu_to_le16((u16)
			    (sizeof(struct host_cmd_ds_802_11_ad_hoc_join)
			     + S_DS_GEN + cmd_append_size));

	adhoc_join->bss_descriptor.cap_info_bitmap = cpu_to_le16(tmp_cap);

	return 0;
}

/*
 * This function handles the command response of ad-hoc start and
 * ad-hoc join.
 *
 * The function generates a device-connected event to notify
 * the applications, in case of successful ad-hoc start/join, and
 * saves the beacon buffer.
 */
int mwifiex_ret_802_11_ad_hoc(struct mwifiex_private *priv,
			      struct host_cmd_ds_command *resp)
{
	int ret = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ad_hoc_result *adhoc_result;
	struct mwifiex_bssdescriptor *bss_desc;

	adhoc_result = &resp->params.adhoc_result;

	bss_desc = priv->attempted_bss_desc;

	/* Join result code 0 --> SUCCESS */
	if (le16_to_cpu(resp->result)) {
		dev_err(priv->adapter->dev, "ADHOC_RESP: failed\n");
		if (priv->media_connected)
			mwifiex_reset_connect_state(priv);

		memset(&priv->curr_bss_params.bss_descriptor,
		       0x00, sizeof(struct mwifiex_bssdescriptor));

		ret = -1;
		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->media_connected = true;

	if (le16_to_cpu(resp->command) == HostCmd_CMD_802_11_AD_HOC_START) {
		dev_dbg(priv->adapter->dev, "info: ADHOC_S_RESP %s\n",
				bss_desc->ssid.ssid);

		/* Update the created network descriptor with the new BSSID */
		memcpy(bss_desc->mac_address,
		       adhoc_result->bssid, ETH_ALEN);

		priv->adhoc_state = ADHOC_STARTED;
	} else {
		/*
		 * Now the join cmd should be successful.
		 * If BSSID has changed use SSID to compare instead of BSSID
		 */
		dev_dbg(priv->adapter->dev, "info: ADHOC_J_RESP %s\n",
				bss_desc->ssid.ssid);

		/*
		 * Make a copy of current BSSID descriptor, only needed for
		 * join since the current descriptor is already being used
		 * for adhoc start
		 */
		memcpy(&priv->curr_bss_params.bss_descriptor,
		       bss_desc, sizeof(struct mwifiex_bssdescriptor));

		priv->adhoc_state = ADHOC_JOINED;
	}

	dev_dbg(priv->adapter->dev, "info: ADHOC_RESP: channel = %d\n",
				priv->adhoc_channel);
	dev_dbg(priv->adapter->dev, "info: ADHOC_RESP: BSSID = %pM\n",
	       priv->curr_bss_params.bss_descriptor.mac_address);

	if (!netif_carrier_ok(priv->netdev))
		netif_carrier_on(priv->netdev);
	if (netif_queue_stopped(priv->netdev))
		netif_wake_queue(priv->netdev);

	mwifiex_save_curr_bcn(priv);

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

/*
 * This function associates to a specific BSS discovered in a scan.
 *
 * It clears any past association response stored for application
 * retrieval and calls the command preparation routine to send the
 * command to firmware.
 */
int mwifiex_associate(struct mwifiex_private *priv,
		      struct mwifiex_bssdescriptor *bss_desc)
{
	u8 current_bssid[ETH_ALEN];

	/* Return error if the adapter or table entry is not marked as infra */
	if ((priv->bss_mode != NL80211_IFTYPE_STATION) ||
	    (bss_desc->bss_mode != NL80211_IFTYPE_STATION))
		return -1;

	memcpy(&current_bssid,
	       &priv->curr_bss_params.bss_descriptor.mac_address,
	       sizeof(current_bssid));

	/* Clear any past association response stored for application
	   retrieval */
	priv->assoc_rsp_size = 0;

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_ASSOCIATE,
				    HostCmd_ACT_GEN_SET, 0, bss_desc);
}

/*
 * This function starts an ad-hoc network.
 *
 * It calls the command preparation routine to send the command to firmware.
 */
int
mwifiex_adhoc_start(struct mwifiex_private *priv,
		    struct mwifiex_802_11_ssid *adhoc_ssid)
{
	dev_dbg(priv->adapter->dev, "info: Adhoc Channel = %d\n",
		priv->adhoc_channel);
	dev_dbg(priv->adapter->dev, "info: curr_bss_params.channel = %d\n",
	       priv->curr_bss_params.bss_descriptor.channel);
	dev_dbg(priv->adapter->dev, "info: curr_bss_params.band = %d\n",
	       priv->curr_bss_params.band);

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_AD_HOC_START,
				    HostCmd_ACT_GEN_SET, 0, adhoc_ssid);
}

/*
 * This function joins an ad-hoc network found in a previous scan.
 *
 * It calls the command preparation routine to send the command to firmware,
 * if already not connected to the requested SSID.
 */
int mwifiex_adhoc_join(struct mwifiex_private *priv,
		       struct mwifiex_bssdescriptor *bss_desc)
{
	dev_dbg(priv->adapter->dev, "info: adhoc join: curr_bss ssid =%s\n",
	       priv->curr_bss_params.bss_descriptor.ssid.ssid);
	dev_dbg(priv->adapter->dev, "info: adhoc join: curr_bss ssid_len =%u\n",
	       priv->curr_bss_params.bss_descriptor.ssid.ssid_len);
	dev_dbg(priv->adapter->dev, "info: adhoc join: ssid =%s\n",
		bss_desc->ssid.ssid);
	dev_dbg(priv->adapter->dev, "info: adhoc join: ssid_len =%u\n",
	       bss_desc->ssid.ssid_len);

	/* Check if the requested SSID is already joined */
	if (priv->curr_bss_params.bss_descriptor.ssid.ssid_len &&
	    !mwifiex_ssid_cmp(&bss_desc->ssid,
			      &priv->curr_bss_params.bss_descriptor.ssid) &&
	    (priv->curr_bss_params.bss_descriptor.bss_mode ==
							NL80211_IFTYPE_ADHOC)) {
		dev_dbg(priv->adapter->dev, "info: ADHOC_J_CMD: new ad-hoc SSID"
			" is the same as current; not attempting to re-join\n");
		return -1;
	}

	dev_dbg(priv->adapter->dev, "info: curr_bss_params.channel = %d\n",
	       priv->curr_bss_params.bss_descriptor.channel);
	dev_dbg(priv->adapter->dev, "info: curr_bss_params.band = %c\n",
	       priv->curr_bss_params.band);

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_AD_HOC_JOIN,
				    HostCmd_ACT_GEN_SET, 0, bss_desc);
}

/*
 * This function deauthenticates/disconnects from infra network by sending
 * deauthentication request.
 */
static int mwifiex_deauthenticate_infra(struct mwifiex_private *priv, u8 *mac)
{
	u8 mac_address[ETH_ALEN];
	int ret;
	u8 zero_mac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };

	if (mac) {
		if (!memcmp(mac, zero_mac, sizeof(zero_mac)))
			memcpy((u8 *) &mac_address,
			       (u8 *) &priv->curr_bss_params.bss_descriptor.
			       mac_address, ETH_ALEN);
		else
			memcpy((u8 *) &mac_address, (u8 *) mac, ETH_ALEN);
	} else {
		memcpy((u8 *) &mac_address, (u8 *) &priv->curr_bss_params.
		       bss_descriptor.mac_address, ETH_ALEN);
	}

	ret = mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_DEAUTHENTICATE,
				    HostCmd_ACT_GEN_SET, 0, &mac_address);

	return ret;
}

/*
 * This function deauthenticates/disconnects from a BSS.
 *
 * In case of infra made, it sends deauthentication request, and
 * in case of ad-hoc mode, a stop network request is sent to the firmware.
 */
int mwifiex_deauthenticate(struct mwifiex_private *priv, u8 *mac)
{
	int ret = 0;

	if (priv->media_connected) {
		if (priv->bss_mode == NL80211_IFTYPE_STATION) {
			ret = mwifiex_deauthenticate_infra(priv, mac);
		} else if (priv->bss_mode == NL80211_IFTYPE_ADHOC) {
			ret = mwifiex_send_cmd_sync(priv,
						HostCmd_CMD_802_11_AD_HOC_STOP,
						HostCmd_ACT_GEN_SET, 0, NULL);
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(mwifiex_deauthenticate);

/*
 * This function converts band to radio type used in channel TLV.
 */
u8
mwifiex_band_to_radio_type(u8 band)
{
	switch (band) {
	case BAND_A:
	case BAND_AN:
	case BAND_A | BAND_AN:
		return HostCmd_SCAN_RADIO_TYPE_A;
	case BAND_B:
	case BAND_G:
	case BAND_B | BAND_G:
	default:
		return HostCmd_SCAN_RADIO_TYPE_BG;
	}
}
