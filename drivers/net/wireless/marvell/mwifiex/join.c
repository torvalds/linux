/*
 * Marvell Wireless LAN device driver: association and ad-hoc start/join
 *
 * Copyright (C) 2011-2014, Marvell International Ltd.
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
#include "11ac.h"

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
		mwifiex_dbg(priv->adapter, INFO,
			    "info: %s: append generic ie len %d to %p\n",
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
	tsf_val = cpu_to_le64(bss_desc->fw_tsf);
	memcpy(*buffer, &tsf_val, sizeof(tsf_val));
	*buffer += sizeof(tsf_val);

	tsf_val = cpu_to_le64(bss_desc->timestamp);

	mwifiex_dbg(priv->adapter, INFO,
		    "info: %s: TSF offset calc: %016llx - %016llx\n",
		    __func__, bss_desc->timestamp, bss_desc->fw_tsf);

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

	tmp = kmemdup(rate1, rate1_size, GFP_KERNEL);
	if (!tmp) {
		mwifiex_dbg(priv->adapter, ERROR, "failed to alloc tmp buf\n");
		return -ENOMEM;
	}

	memset(rate1, 0, rate1_size);

	for (i = 0; i < rate2_size && rate2[i]; i++) {
		for (j = 0; j < rate1_size && tmp[j]; j++) {
			/* Check common rate, excluding the bit for
			   basic rate */
			if ((rate2[i] & 0x7F) == (tmp[j] & 0x7F)) {
				*rate1++ = tmp[j];
				break;
			}
		}
	}

	mwifiex_dbg(priv->adapter, INFO, "info: Tx data rate set to %#x\n",
		    priv->data_rate);

	if (!priv->is_data_rate_auto) {
		while (*ptr) {
			if ((*ptr & 0x7f) == priv->data_rate) {
				ret = 0;
				goto done;
			}
			ptr++;
		}
		mwifiex_dbg(priv->adapter, ERROR,
			    "previously set fixed data rate %#x\t"
			    "is not compatible with the network\n",
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
		mwifiex_dbg(priv->adapter, ERROR,
			    "%s: cannot get common rates\n",
			    __func__);
		return -1;
	}

	*out_rates_size =
		min_t(size_t, strlen(out_rates), MWIFIEX_SUPPORTED_RATES);

	return 0;
}

/*
 * This function appends a WPS IE. It is called from the network join command
 * preparation routine.
 *
 * If the IE buffer has been setup by the application, this routine appends
 * the buffer as a WPS TLV type to the request.
 */
static int
mwifiex_cmd_append_wps_ie(struct mwifiex_private *priv, u8 **buffer)
{
	int retLen = 0;
	struct mwifiex_ie_types_header ie_header;

	if (!buffer || !*buffer)
		return 0;

	/*
	 * If there is a wps ie buffer setup, append it to the return
	 * parameter buffer pointer.
	 */
	if (priv->wps_ie_len) {
		mwifiex_dbg(priv->adapter, CMD,
			    "cmd: append wps ie %d to %p\n",
			    priv->wps_ie_len, *buffer);

		/* Wrap the generic IE buffer with a pass through TLV type */
		ie_header.type = cpu_to_le16(TLV_TYPE_MGMT_IE);
		ie_header.len = cpu_to_le16(priv->wps_ie_len);
		memcpy(*buffer, &ie_header, sizeof(ie_header));
		*buffer += sizeof(ie_header);
		retLen += sizeof(ie_header);

		memcpy(*buffer, priv->wps_ie, priv->wps_ie_len);
		*buffer += priv->wps_ie_len;
		retLen += priv->wps_ie_len;

	}

	kfree(priv->wps_ie);
	priv->wps_ie_len = 0;
	return retLen;
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
		mwifiex_dbg(priv->adapter, CMD,
			    "cmd: append wapi ie %d to %p\n",
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
				 struct mwifiex_bssdescriptor *bss_desc)
{
	struct host_cmd_ds_802_11_associate *assoc = &cmd->params.associate;
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

	pos = (u8 *) assoc;

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
	mwifiex_dbg(priv->adapter, INFO, "info: ASSOC_CMD: rates size = %d\n",
		    rates_size);

	/* Add the Authentication type to be used for Auth frames */
	auth_tlv = (struct mwifiex_ie_types_auth_type *) pos;
	auth_tlv->header.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
	auth_tlv->header.len = cpu_to_le16(sizeof(auth_tlv->auth_type));
	if (priv->sec_info.wep_enabled)
		auth_tlv->auth_type = cpu_to_le16(
				(u16) priv->sec_info.authentication_mode);
	else
		auth_tlv->auth_type = cpu_to_le16(NL80211_AUTHTYPE_OPEN_SYSTEM);

	pos += sizeof(auth_tlv->header) + le16_to_cpu(auth_tlv->header.len);

	if (IS_SUPPORT_MULTI_BANDS(priv->adapter) &&
	    !(ISSUPP_11NENABLED(priv->adapter->fw_cap_info) &&
	    (!bss_desc->disable_11n) &&
	    (priv->adapter->config_bands & BAND_GN ||
	     priv->adapter->config_bands & BAND_AN) &&
	    (bss_desc->bcn_ht_cap)
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
		mwifiex_dbg(priv->adapter, INFO, "info: Assoc: TLV Chan = %d\n",
			    chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].radio_type =
			mwifiex_band_to_radio_type((u8) bss_desc->bss_band);

		mwifiex_dbg(priv->adapter, INFO, "info: Assoc: TLV Band = %d\n",
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

	if (ISSUPP_11NENABLED(priv->adapter->fw_cap_info) &&
	    (!bss_desc->disable_11n) &&
	    (priv->adapter->config_bands & BAND_GN ||
	     priv->adapter->config_bands & BAND_AN))
		mwifiex_cmd_append_11n_tlv(priv, bss_desc, &pos);

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info) &&
	    !bss_desc->disable_11n && !bss_desc->disable_11ac &&
	    priv->adapter->config_bands & BAND_AAC)
		mwifiex_cmd_append_11ac_tlv(priv, bss_desc, &pos);

	/* Append vendor specific IE TLV */
	mwifiex_cmd_append_vsie_tlv(priv, MWIFIEX_VSIE_MASK_ASSOC, &pos);

	mwifiex_wmm_process_association_req(priv, &pos, &bss_desc->wmm_ie,
					    bss_desc->bcn_ht_cap);
	if (priv->sec_info.wapi_enabled && priv->wapi_ie_len)
		mwifiex_cmd_append_wapi_ie(priv, &pos);

	if (priv->wps.session_enable && priv->wps_ie_len)
		mwifiex_cmd_append_wps_ie(priv, &pos);

	mwifiex_cmd_append_generic_ie(priv, &pos);

	mwifiex_cmd_append_tsf_tlv(priv, &pos, bss_desc);

	mwifiex_11h_process_join(priv, &pos, bss_desc);

	cmd->size = cpu_to_le16((u16) (pos - (u8 *) assoc) + S_DS_GEN);

	/* Set the Capability info at last */
	tmp_cap = bss_desc->cap_info_bitmap;

	if (priv->adapter->config_bands == BAND_B)
		tmp_cap &= ~WLAN_CAPABILITY_SHORT_SLOT_TIME;

	tmp_cap &= CAPINFO_MASK;
	mwifiex_dbg(priv->adapter, INFO,
		    "info: ASSOC_CMD: tmp_cap=%4X CAPINFO_MASK=%4lX\n",
		    tmp_cap, CAPINFO_MASK);
	assoc->cap_info_bitmap = cpu_to_le16(tmp_cap);

	return 0;
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
	bool enable_data = true;
	u16 cap_info, status_code, aid;
	const u8 *ie_ptr;
	struct ieee80211_ht_operation *assoc_resp_ht_oper;

	if (!priv->attempted_bss_desc) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "ASSOC_RESP: failed, association terminated by host\n");
		goto done;
	}

	assoc_rsp = (struct ieee_types_assoc_rsp *) &resp->params;

	cap_info = le16_to_cpu(assoc_rsp->cap_info_bitmap);
	status_code = le16_to_cpu(assoc_rsp->status_code);
	aid = le16_to_cpu(assoc_rsp->a_id);

	if ((aid & (BIT(15) | BIT(14))) != (BIT(15) | BIT(14)))
		dev_err(priv->adapter->dev,
			"invalid AID value 0x%x; bits 15:14 not set\n",
			aid);

	aid &= ~(BIT(15) | BIT(14));

	priv->assoc_rsp_size = min(le16_to_cpu(resp->size) - S_DS_GEN,
				   sizeof(priv->assoc_rsp_buf));

	assoc_rsp->a_id = cpu_to_le16(aid);
	memcpy(priv->assoc_rsp_buf, &resp->params, priv->assoc_rsp_size);

	if (status_code) {
		priv->adapter->dbg.num_cmd_assoc_failure++;
		mwifiex_dbg(priv->adapter, ERROR,
			    "ASSOC_RESP: failed,\t"
			    "status code=%d err=%#x a_id=%#x\n",
			    status_code, cap_info,
			    le16_to_cpu(assoc_rsp->a_id));

		mwifiex_dbg(priv->adapter, ERROR, "assoc failure: reason %s\n",
			    assoc_failure_reason_to_str(cap_info));
		if (cap_info == CONNECT_ERR_ASSOC_ERR_TIMEOUT) {
			if (status_code == MWIFIEX_ASSOC_CMD_FAILURE_AUTH) {
				ret = WLAN_STATUS_AUTH_TIMEOUT;
				mwifiex_dbg(priv->adapter, ERROR,
					    "ASSOC_RESP: AUTH timeout\n");
			} else {
				ret = WLAN_STATUS_UNSPECIFIED_FAILURE;
				mwifiex_dbg(priv->adapter, ERROR,
					    "ASSOC_RESP: UNSPECIFIED failure\n");
			}
		} else {
			ret = status_code;
		}

		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->media_connected = true;

	priv->adapter->ps_state = PS_STATE_AWAKE;
	priv->adapter->pps_uapsd_mode = false;
	priv->adapter->tx_lock_flag = false;

	/* Set the attempted BSSID Index to current */
	bss_desc = priv->attempted_bss_desc;

	mwifiex_dbg(priv->adapter, INFO, "info: ASSOC_RESP: %s\n",
		    bss_desc->ssid.ssid);

	/* Make a copy of current BSSID descriptor */
	memcpy(&priv->curr_bss_params.bss_descriptor,
	       bss_desc, sizeof(struct mwifiex_bssdescriptor));

	/* Update curr_bss_params */
	priv->curr_bss_params.bss_descriptor.channel
		= bss_desc->phy_param_set.ds_param_set.current_chan;

	priv->curr_bss_params.band = (u8) bss_desc->bss_band;

	if (bss_desc->wmm_ie.vend_hdr.element_id == WLAN_EID_VENDOR_SPECIFIC)
		priv->curr_bss_params.wmm_enabled = true;
	else
		priv->curr_bss_params.wmm_enabled = false;

	if ((priv->wmm_required || bss_desc->bcn_ht_cap) &&
	    priv->curr_bss_params.wmm_enabled)
		priv->wmm_enabled = true;
	else
		priv->wmm_enabled = false;

	priv->curr_bss_params.wmm_uapsd_enabled = false;

	if (priv->wmm_enabled)
		priv->curr_bss_params.wmm_uapsd_enabled
			= ((bss_desc->wmm_ie.qos_info_bitmap &
				IEEE80211_WMM_IE_AP_QOSINFO_UAPSD) ? 1 : 0);

	/* Store the bandwidth information from assoc response */
	ie_ptr = cfg80211_find_ie(WLAN_EID_HT_OPERATION, assoc_rsp->ie_buffer,
				  priv->assoc_rsp_size
				  - sizeof(struct ieee_types_assoc_rsp));
	if (ie_ptr) {
		assoc_resp_ht_oper = (struct ieee80211_ht_operation *)(ie_ptr
					+ sizeof(struct ieee_types_header));
		priv->assoc_resp_ht_param = assoc_resp_ht_oper->ht_param;
		priv->ht_param_present = true;
	} else {
		priv->ht_param_present = false;
	}

	mwifiex_dbg(priv->adapter, INFO,
		    "info: ASSOC_RESP: curr_pkt_filter is %#x\n",
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
		mwifiex_dbg(priv->adapter, INFO,
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

	mwifiex_dbg(priv->adapter, INFO, "info: ASSOC_RESP: associated\n");

	/* Add the ra_list here for infra mode as there will be only 1 ra
	   always */
	mwifiex_ralist_add(priv,
			   priv->curr_bss_params.bss_descriptor.mac_address);

	if (!netif_carrier_ok(priv->netdev))
		netif_carrier_on(priv->netdev);
	mwifiex_wake_up_net_dev_queue(priv->netdev, adapter);

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
				struct host_cmd_ds_command *cmd,
				struct cfg80211_ssid *req_ssid)
{
	int rsn_ie_len = 0;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ad_hoc_start *adhoc_start =
		&cmd->params.adhoc_start;
	struct mwifiex_bssdescriptor *bss_desc;
	u32 cmd_append_size = 0;
	u32 i;
	u16 tmp_cap;
	struct mwifiex_ie_types_chan_list_param_set *chan_tlv;
	u8 radio_type;

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

	memcpy(adhoc_start->ssid, req_ssid->ssid, req_ssid->ssid_len);

	mwifiex_dbg(adapter, INFO, "info: ADHOC_S_CMD: SSID = %s\n",
		    adhoc_start->ssid);

	memset(bss_desc->ssid.ssid, 0, IEEE80211_MAX_SSID_LEN);
	memcpy(bss_desc->ssid.ssid, req_ssid->ssid, req_ssid->ssid_len);

	bss_desc->ssid.ssid_len = req_ssid->ssid_len;

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

	if (!mwifiex_get_cfp(priv, adapter->adhoc_start_band,
			     (u16) priv->adhoc_channel, 0)) {
		struct mwifiex_chan_freq_power *cfp;
		cfp = mwifiex_get_cfp(priv, adapter->adhoc_start_band,
				      FIRST_VALID_CHANNEL, 0);
		if (cfp)
			priv->adhoc_channel = (u8) cfp->channel;
	}

	if (!priv->adhoc_channel) {
		mwifiex_dbg(adapter, ERROR,
			    "ADHOC_S_CMD: adhoc_channel cannot be 0\n");
		return -1;
	}

	mwifiex_dbg(adapter, INFO,
		    "info: ADHOC_S_CMD: creating ADHOC on channel %d\n",
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
	tmp_cap = WLAN_CAPABILITY_IBSS;

	/* Set up privacy in bss_desc */
	if (priv->sec_info.encryption_mode) {
		/* Ad-Hoc capability privacy on */
		mwifiex_dbg(adapter, INFO,
			    "info: ADHOC_S_CMD: wep_status set privacy to WEP\n");
		bss_desc->privacy = MWIFIEX_802_11_PRIV_FILTER_8021X_WEP;
		tmp_cap |= WLAN_CAPABILITY_PRIVACY;
	} else {
		mwifiex_dbg(adapter, INFO,
			    "info: ADHOC_S_CMD: wep_status NOT set,\t"
			    "setting privacy to ACCEPT ALL\n");
		bss_desc->privacy = MWIFIEX_802_11_PRIV_FILTER_ACCEPT_ALL;
	}

	memset(adhoc_start->data_rate, 0, sizeof(adhoc_start->data_rate));
	mwifiex_get_active_data_rates(priv, adhoc_start->data_rate);
	if ((adapter->adhoc_start_band & BAND_G) &&
	    (priv->curr_pkt_filter & HostCmd_ACT_MAC_ADHOC_G_PROTECTION_ON)) {
		if (mwifiex_send_cmd(priv, HostCmd_CMD_MAC_CONTROL,
				     HostCmd_ACT_GEN_SET, 0,
				     &priv->curr_pkt_filter, false)) {
			mwifiex_dbg(adapter, ERROR,
				    "ADHOC_S_CMD: G Protection config failed\n");
			return -1;
		}
	}
	/* Find the last non zero */
	for (i = 0; i < sizeof(adhoc_start->data_rate); i++)
		if (!adhoc_start->data_rate[i])
			break;

	priv->curr_bss_params.num_of_rates = i;

	/* Copy the ad-hoc creating rates into Current BSS rate structure */
	memcpy(&priv->curr_bss_params.data_rates,
	       &adhoc_start->data_rate, priv->curr_bss_params.num_of_rates);

	mwifiex_dbg(adapter, INFO, "info: ADHOC_S_CMD: rates=%4ph\n",
		    adhoc_start->data_rate);

	mwifiex_dbg(adapter, INFO, "info: ADHOC_S_CMD: AD-HOC Start command is ready\n");

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

		mwifiex_dbg(adapter, INFO, "info: ADHOC_S_CMD: TLV Chan = %d\n",
			    chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].radio_type
		       = mwifiex_band_to_radio_type(priv->curr_bss_params.band);
		if (adapter->adhoc_start_band & BAND_GN ||
		    adapter->adhoc_start_band & BAND_AN) {
			if (adapter->sec_chan_offset ==
					    IEEE80211_HT_PARAM_CHA_SEC_ABOVE)
				chan_tlv->chan_scan_param[0].radio_type |=
					(IEEE80211_HT_PARAM_CHA_SEC_ABOVE << 4);
			else if (adapter->sec_chan_offset ==
					    IEEE80211_HT_PARAM_CHA_SEC_BELOW)
				chan_tlv->chan_scan_param[0].radio_type |=
					(IEEE80211_HT_PARAM_CHA_SEC_BELOW << 4);
		}
		mwifiex_dbg(adapter, INFO, "info: ADHOC_S_CMD: TLV Band = %d\n",
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
		/* Fill HT CAPABILITY */
		ht_cap = (struct mwifiex_ie_types_htcap *) pos;
		memset(ht_cap, 0, sizeof(struct mwifiex_ie_types_htcap));
		ht_cap->header.type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		ht_cap->header.len =
		       cpu_to_le16(sizeof(struct ieee80211_ht_cap));
		radio_type = mwifiex_band_to_radio_type(
					priv->adapter->config_bands);
		mwifiex_fill_cap_info(priv, radio_type, &ht_cap->ht_cap);

		if (adapter->sec_chan_offset ==
					IEEE80211_HT_PARAM_CHA_SEC_NONE) {
			u16 tmp_ht_cap;

			tmp_ht_cap = le16_to_cpu(ht_cap->ht_cap.cap_info);
			tmp_ht_cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;
			tmp_ht_cap &= ~IEEE80211_HT_CAP_SGI_40;
			ht_cap->ht_cap.cap_info = cpu_to_le16(tmp_ht_cap);
		}

		pos += sizeof(struct mwifiex_ie_types_htcap);
		cmd_append_size += sizeof(struct mwifiex_ie_types_htcap);

		/* Fill HT INFORMATION */
		ht_info = (struct mwifiex_ie_types_htinfo *) pos;
		memset(ht_info, 0, sizeof(struct mwifiex_ie_types_htinfo));
		ht_info->header.type = cpu_to_le16(WLAN_EID_HT_OPERATION);
		ht_info->header.len =
			cpu_to_le16(sizeof(struct ieee80211_ht_operation));

		ht_info->ht_oper.primary_chan =
			(u8) priv->curr_bss_params.bss_descriptor.channel;
		if (adapter->sec_chan_offset) {
			ht_info->ht_oper.ht_param = adapter->sec_chan_offset;
			ht_info->ht_oper.ht_param |=
					IEEE80211_HT_PARAM_CHAN_WIDTH_ANY;
		}
		ht_info->ht_oper.operation_mode =
		     cpu_to_le16(IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT);
		ht_info->ht_oper.basic_set[0] = 0xff;
		pos += sizeof(struct mwifiex_ie_types_htinfo);
		cmd_append_size +=
				sizeof(struct mwifiex_ie_types_htinfo);
	}

	cmd->size =
		cpu_to_le16((u16)(sizeof(struct host_cmd_ds_802_11_ad_hoc_start)
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
			       struct host_cmd_ds_command *cmd,
			       struct mwifiex_bssdescriptor *bss_desc)
{
	int rsn_ie_len = 0;
	struct host_cmd_ds_802_11_ad_hoc_join *adhoc_join =
		&cmd->params.adhoc_join;
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

		if (mwifiex_send_cmd(priv, HostCmd_CMD_MAC_CONTROL,
				     HostCmd_ACT_GEN_SET, 0,
				     &curr_pkt_filter, false)) {
			mwifiex_dbg(priv->adapter, ERROR,
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

	mwifiex_dbg(priv->adapter, INFO,
		    "info: ADHOC_J_CMD: tmp_cap=%4X CAPINFO_MASK=%4lX\n",
		    tmp_cap, CAPINFO_MASK);

	/* Information on BSSID descriptor passed to FW */
	mwifiex_dbg(priv->adapter, INFO,
		    "info: ADHOC_J_CMD: BSSID=%pM, SSID='%s'\n",
		    adhoc_join->bss_descriptor.bssid,
		    adhoc_join->bss_descriptor.ssid);

	for (i = 0; i < MWIFIEX_SUPPORTED_RATES &&
		    bss_desc->supported_rates[i]; i++)
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

	if (priv->sec_info.wep_enabled || priv->sec_info.wpa_enabled)
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
		mwifiex_dbg(priv->adapter, INFO, "info: ADHOC_J_CMD: TLV Chan=%d\n",
			    chan_tlv->chan_scan_param[0].chan_number);

		chan_tlv->chan_scan_param[0].radio_type =
			mwifiex_band_to_radio_type((u8) bss_desc->bss_band);

		mwifiex_dbg(priv->adapter, INFO, "info: ADHOC_J_CMD: TLV Band=%d\n",
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

	cmd->size = cpu_to_le16
		((u16) (sizeof(struct host_cmd_ds_802_11_ad_hoc_join)
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
	struct host_cmd_ds_802_11_ad_hoc_start_result *start_result =
				&resp->params.start_result;
	struct host_cmd_ds_802_11_ad_hoc_join_result *join_result =
				&resp->params.join_result;
	struct mwifiex_bssdescriptor *bss_desc;
	u16 cmd = le16_to_cpu(resp->command);
	u8 result;

	if (!priv->attempted_bss_desc) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "ADHOC_RESP: failed, association terminated by host\n");
		goto done;
	}

	if (cmd == HostCmd_CMD_802_11_AD_HOC_START)
		result = start_result->result;
	else
		result = join_result->result;

	bss_desc = priv->attempted_bss_desc;

	/* Join result code 0 --> SUCCESS */
	if (result) {
		mwifiex_dbg(priv->adapter, ERROR, "ADHOC_RESP: failed\n");
		if (priv->media_connected)
			mwifiex_reset_connect_state(priv, result, true);

		memset(&priv->curr_bss_params.bss_descriptor,
		       0x00, sizeof(struct mwifiex_bssdescriptor));

		ret = -1;
		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->media_connected = true;

	if (le16_to_cpu(resp->command) == HostCmd_CMD_802_11_AD_HOC_START) {
		mwifiex_dbg(priv->adapter, INFO, "info: ADHOC_S_RESP %s\n",
			    bss_desc->ssid.ssid);

		/* Update the created network descriptor with the new BSSID */
		memcpy(bss_desc->mac_address,
		       start_result->bssid, ETH_ALEN);

		priv->adhoc_state = ADHOC_STARTED;
	} else {
		/*
		 * Now the join cmd should be successful.
		 * If BSSID has changed use SSID to compare instead of BSSID
		 */
		mwifiex_dbg(priv->adapter, INFO,
			    "info: ADHOC_J_RESP %s\n",
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

	mwifiex_dbg(priv->adapter, INFO, "info: ADHOC_RESP: channel = %d\n",
		    priv->adhoc_channel);
	mwifiex_dbg(priv->adapter, INFO, "info: ADHOC_RESP: BSSID = %pM\n",
		    priv->curr_bss_params.bss_descriptor.mac_address);

	if (!netif_carrier_ok(priv->netdev))
		netif_carrier_on(priv->netdev);
	mwifiex_wake_up_net_dev_queue(priv->netdev, adapter);

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
	/* Return error if the adapter is not STA role or table entry
	 * is not marked as infra.
	 */
	if ((GET_BSS_ROLE(priv) != MWIFIEX_BSS_ROLE_STA) ||
	    (bss_desc->bss_mode != NL80211_IFTYPE_STATION))
		return -1;

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info) &&
	    !bss_desc->disable_11n && !bss_desc->disable_11ac &&
	    priv->adapter->config_bands & BAND_AAC)
		mwifiex_set_11ac_ba_params(priv);
	else
		mwifiex_set_ba_params(priv);

	/* Clear any past association response stored for application
	   retrieval */
	priv->assoc_rsp_size = 0;

	return mwifiex_send_cmd(priv, HostCmd_CMD_802_11_ASSOCIATE,
				HostCmd_ACT_GEN_SET, 0, bss_desc, true);
}

/*
 * This function starts an ad-hoc network.
 *
 * It calls the command preparation routine to send the command to firmware.
 */
int
mwifiex_adhoc_start(struct mwifiex_private *priv,
		    struct cfg80211_ssid *adhoc_ssid)
{
	mwifiex_dbg(priv->adapter, INFO, "info: Adhoc Channel = %d\n",
		    priv->adhoc_channel);
	mwifiex_dbg(priv->adapter, INFO, "info: curr_bss_params.channel = %d\n",
		    priv->curr_bss_params.bss_descriptor.channel);
	mwifiex_dbg(priv->adapter, INFO, "info: curr_bss_params.band = %d\n",
		    priv->curr_bss_params.band);

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info) &&
	    priv->adapter->config_bands & BAND_AAC)
		mwifiex_set_11ac_ba_params(priv);
	else
		mwifiex_set_ba_params(priv);

	return mwifiex_send_cmd(priv, HostCmd_CMD_802_11_AD_HOC_START,
				HostCmd_ACT_GEN_SET, 0, adhoc_ssid, true);
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
	mwifiex_dbg(priv->adapter, INFO,
		    "info: adhoc join: curr_bss ssid =%s\n",
		    priv->curr_bss_params.bss_descriptor.ssid.ssid);
	mwifiex_dbg(priv->adapter, INFO,
		    "info: adhoc join: curr_bss ssid_len =%u\n",
		    priv->curr_bss_params.bss_descriptor.ssid.ssid_len);
	mwifiex_dbg(priv->adapter, INFO, "info: adhoc join: ssid =%s\n",
		    bss_desc->ssid.ssid);
	mwifiex_dbg(priv->adapter, INFO, "info: adhoc join: ssid_len =%u\n",
		    bss_desc->ssid.ssid_len);

	/* Check if the requested SSID is already joined */
	if (priv->curr_bss_params.bss_descriptor.ssid.ssid_len &&
	    !mwifiex_ssid_cmp(&bss_desc->ssid,
			      &priv->curr_bss_params.bss_descriptor.ssid) &&
	    (priv->curr_bss_params.bss_descriptor.bss_mode ==
							NL80211_IFTYPE_ADHOC)) {
		mwifiex_dbg(priv->adapter, INFO,
			    "info: ADHOC_J_CMD: new ad-hoc SSID\t"
			    "is the same as current; not attempting to re-join\n");
		return -1;
	}

	if (ISSUPP_11ACENABLED(priv->adapter->fw_cap_info) &&
	    !bss_desc->disable_11n && !bss_desc->disable_11ac &&
	    priv->adapter->config_bands & BAND_AAC)
		mwifiex_set_11ac_ba_params(priv);
	else
		mwifiex_set_ba_params(priv);

	mwifiex_dbg(priv->adapter, INFO,
		    "info: curr_bss_params.channel = %d\n",
		    priv->curr_bss_params.bss_descriptor.channel);
	mwifiex_dbg(priv->adapter, INFO,
		    "info: curr_bss_params.band = %c\n",
		    priv->curr_bss_params.band);

	return mwifiex_send_cmd(priv, HostCmd_CMD_802_11_AD_HOC_JOIN,
				HostCmd_ACT_GEN_SET, 0, bss_desc, true);
}

/*
 * This function deauthenticates/disconnects from infra network by sending
 * deauthentication request.
 */
static int mwifiex_deauthenticate_infra(struct mwifiex_private *priv, u8 *mac)
{
	u8 mac_address[ETH_ALEN];
	int ret;

	if (!mac || is_zero_ether_addr(mac))
		memcpy(mac_address,
		       priv->curr_bss_params.bss_descriptor.mac_address,
		       ETH_ALEN);
	else
		memcpy(mac_address, mac, ETH_ALEN);

	ret = mwifiex_send_cmd(priv, HostCmd_CMD_802_11_DEAUTHENTICATE,
			       HostCmd_ACT_GEN_SET, 0, mac_address, true);

	return ret;
}

/*
 * This function deauthenticates/disconnects from a BSS.
 *
 * In case of infra made, it sends deauthentication request, and
 * in case of ad-hoc mode, a stop network request is sent to the firmware.
 * In AP mode, a command to stop bss is sent to firmware.
 */
int mwifiex_deauthenticate(struct mwifiex_private *priv, u8 *mac)
{
	int ret = 0;

	if (!priv->media_connected)
		return 0;

	switch (priv->bss_mode) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		ret = mwifiex_deauthenticate_infra(priv, mac);
		if (ret)
			cfg80211_disconnected(priv->netdev, 0, NULL, 0,
					      true, GFP_KERNEL);
		break;
	case NL80211_IFTYPE_ADHOC:
		return mwifiex_send_cmd(priv, HostCmd_CMD_802_11_AD_HOC_STOP,
					HostCmd_ACT_GEN_SET, 0, NULL, true);
	case NL80211_IFTYPE_AP:
		return mwifiex_send_cmd(priv, HostCmd_CMD_UAP_BSS_STOP,
					HostCmd_ACT_GEN_SET, 0, NULL, true);
	default:
		break;
	}

	return ret;
}

/* This function deauthenticates/disconnects from all BSS. */
void mwifiex_deauthenticate_all(struct mwifiex_adapter *adapter)
{
	struct mwifiex_private *priv;
	int i;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		if (priv)
			mwifiex_deauthenticate(priv, NULL);
	}
}
EXPORT_SYMBOL_GPL(mwifiex_deauthenticate_all);

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
	case BAND_A | BAND_AN | BAND_AAC:
		return HostCmd_SCAN_RADIO_TYPE_A;
	case BAND_B:
	case BAND_G:
	case BAND_B | BAND_G:
	default:
		return HostCmd_SCAN_RADIO_TYPE_BG;
	}
}
