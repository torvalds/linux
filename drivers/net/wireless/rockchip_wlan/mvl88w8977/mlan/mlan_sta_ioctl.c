/** @file mlan_sta_ioctl.c
 *
 *  @brief This file contains the functions for station ioctl.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/******************************************************
Change log:
    10/21/2008: initial version
******************************************************/

#include "mlan.h"
#include "mlan_join.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#include "mlan_main.h"
#include "mlan_wmm.h"
#include "mlan_11n.h"
#include "mlan_sdio.h"
#include "mlan_11h.h"
#ifdef DRV_EMBEDDED_SUPPLICANT
#include "authenticator_api.h"
#endif

/********************************************************
			Local Variables
********************************************************/

/********************************************************
			Global Variables
********************************************************/

/********************************************************
			Local Functions
********************************************************/
/**
 *  @brief enable adhoc aes key
 *
 *  @param pmpriv   A pointer to mlan_private structure
 *
 *  @return         N/A
 */
static void
wlan_enable_aes_key(pmlan_private pmpriv)
{

	ENTER();

	if (pmpriv->aes_key.key_len != WPA_AES_KEY_LEN) {
		LEAVE();
		return;
	}
	wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_KEY_MATERIAL,
			 HostCmd_ACT_GEN_SET, 0, MNULL, &pmpriv->aes_key);

	LEAVE();
	return;
}

/**
 *  @brief Get signal information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_get_info_signal(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pioctl_req != MNULL) {
		pmpriv = pmadapter->priv[pioctl_req->bss_index];
	} else {
		PRINTM(MERROR, "MLAN IOCTL information is not present\n");
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Check information buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < sizeof(mlan_ds_get_signal)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_get_signal);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	/* Signal info can be obtained only if connected */
	if (pmpriv->media_connected == MFALSE) {
		PRINTM(MINFO, "Can not get signal in disconnected state\n");
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_RSSI_INFO,
			       HostCmd_ACT_GEN_GET,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get signal information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_get_info_signal_ext(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_get_info *info = MNULL;

	ENTER();

	if (pioctl_req != MNULL) {
		pmpriv = pmadapter->priv[pioctl_req->bss_index];
	} else {
		PRINTM(MERROR, "MLAN IOCTL information is not present\n");
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}
	info = (mlan_ds_get_info *)pioctl_req->pbuf;

	/* Check information buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < sizeof(mlan_ds_get_info)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_get_info);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_RSSI_INFO_EXT,
			       HostCmd_ACT_GEN_GET,
			       0, (t_void *)pioctl_req, (t_void *)info);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get statistics information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_get_info_stats(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pioctl_req != MNULL) {
		pmpriv = pmadapter->priv[pioctl_req->bss_index];
	} else {
		PRINTM(MERROR, "MLAN IOCTL information is not present\n");
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Check information buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < sizeof(mlan_ds_get_stats)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_get_stats);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_GET_LOG,
			       HostCmd_ACT_GEN_GET,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get sta channel information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_get_chan_info(IN pmlan_adapter pmadapter,
			     IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	if (pioctl_req != MNULL) {
		pmpriv = pmadapter->priv[pioctl_req->bss_index];
	} else {
		PRINTM(MERROR, "MLAN IOCTL information is not present\n");
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	/* Check information buffer length of MLAN IOCTL */
	if (pioctl_req->buf_len < sizeof(chan_band_info)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(chan_band_info);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_STA_CONFIGURE,
			       HostCmd_ACT_GEN_GET,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get BSS information
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_get_info_bss_info(IN pmlan_adapter pmadapter,
		       IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_get_info *info;
	BSSDescriptor_t *pbss_desc;
	t_s32 tbl_idx = 0;

	ENTER();

	/* Get current BSS info */
	pbss_desc = &pmpriv->curr_bss_params.bss_descriptor;
	info = (mlan_ds_get_info *)pioctl_req->pbuf;

	/* BSS mode */
	info->param.bss_info.bss_mode = pmpriv->bss_mode;

	/* SSID */
	memcpy(pmadapter, &info->param.bss_info.ssid, &pbss_desc->ssid,
	       sizeof(mlan_802_11_ssid));

	/* BSSID */
	memcpy(pmadapter, &info->param.bss_info.bssid, &pbss_desc->mac_address,
	       MLAN_MAC_ADDR_LENGTH);

	/* Channel */
	info->param.bss_info.bss_chan = pbss_desc->channel;

	/* Beacon interval */
	info->param.bss_info.beacon_interval = pbss_desc->beacon_period;

	/* Band */
	info->param.bss_info.bss_band = (t_u8)pbss_desc->bss_band;

	/* Region code */
	info->param.bss_info.region_code = pmadapter->region_code;

	/* Scan table index if connected */
	info->param.bss_info.scan_table_idx = 0;
	info->param.bss_info.scan_block = pmadapter->scan_block;
	if (pmpriv->media_connected == MTRUE) {
		tbl_idx =
			wlan_find_ssid_in_list(pmpriv, &pbss_desc->ssid,
					       pbss_desc->mac_address,
					       pmpriv->bss_mode);
		if (tbl_idx >= 0)
			info->param.bss_info.scan_table_idx = tbl_idx;
	}

	/* Connection status */
	info->param.bss_info.media_connected = pmpriv->media_connected;

	/* Radio status */
	info->param.bss_info.radio_on = pmadapter->radio_on;

	/* Tx power information */
	info->param.bss_info.max_power_level = pmpriv->max_tx_power_level;
	info->param.bss_info.min_power_level = pmpriv->min_tx_power_level;

	/* AdHoc state */
	info->param.bss_info.adhoc_state = pmpriv->adhoc_state;

	/* Last beacon NF */
	info->param.bss_info.bcn_nf_last = pmpriv->bcn_nf_last;

	/* wep status */
	if (pmpriv->sec_info.wep_status == Wlan802_11WEPEnabled)
		info->param.bss_info.wep_status = MTRUE;
	else
		info->param.bss_info.wep_status = MFALSE;

	info->param.bss_info.is_hs_configured = pmadapter->is_hs_configured;
	info->param.bss_info.is_deep_sleep = pmadapter->is_deep_sleep;

	/* Capability Info */
	info->param.bss_info.capability_info = 0;
	memcpy(pmadapter, &info->param.bss_info.capability_info,
	       &pbss_desc->cap_info,
	       sizeof(info->param.bss_info.capability_info));

	memset(pmadapter, &info->param.bss_info.ext_cap, 0, sizeof(ExtCap_t));
	if (pbss_desc->pext_cap) {
		memcpy(pmadapter, &info->param.bss_info.ext_cap,
		       (t_u8 *)pbss_desc->pext_cap + sizeof(IEEEtypes_Header_t),
		       pbss_desc->pext_cap->ieee_hdr.len);
	}

	/* Listen Interval */
	info->param.bss_info.listen_interval = pmpriv->listen_interval;

	/* Association ID */
	if (pmpriv->assoc_rsp_buf)
		info->param.bss_info.assoc_id =
			(t_u16)((IEEEtypes_AssocRsp_t *)pmpriv->assoc_rsp_buf)->
			a_id;
	else
		info->param.bss_info.assoc_id = 0;

	/* AP/Peer supported rates */
	memset(pmadapter, info->param.bss_info.peer_supp_rates, 0,
	       sizeof(info->param.bss_info.peer_supp_rates));
	memcpy(pmadapter, info->param.bss_info.peer_supp_rates,
	       pbss_desc->supported_rates,
	       MIN(sizeof(info->param.bss_info.peer_supp_rates),
		   sizeof(pbss_desc->supported_rates)));
	if (pbss_desc->pmd_ie) {
		info->param.bss_info.mdid = pbss_desc->pmd_ie->mdid;
		info->param.bss_info.ft_cap = pbss_desc->pmd_ie->ft_cap;
	}
	pioctl_req->data_read_written =
		sizeof(mlan_bss_info) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return ret;
}

/**
 *  @brief Get information handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_get_info_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_get_info *pget_info = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	pget_info = (mlan_ds_get_info *)pioctl_req->pbuf;

	switch (pget_info->sub_command) {
	case MLAN_OID_GET_STATS:
		status = wlan_get_info_stats(pmadapter, pioctl_req);
		break;
	case MLAN_OID_GET_SIGNAL:
		status = wlan_get_info_signal(pmadapter, pioctl_req);
		break;
	case MLAN_OID_GET_SIGNAL_EXT:
		status = wlan_get_info_signal_ext(pmadapter, pioctl_req);
		break;
	case MLAN_OID_GET_FW_INFO:
		pioctl_req->data_read_written =
			sizeof(mlan_fw_info) + MLAN_SUB_COMMAND_SIZE;
		pget_info->param.fw_info.fw_ver = pmadapter->fw_release_number;
		memcpy(pmadapter, &pget_info->param.fw_info.mac_addr,
		       pmpriv->curr_addr, MLAN_MAC_ADDR_LENGTH);
		pget_info->param.fw_info.fw_bands = pmadapter->fw_bands;
		pget_info->param.fw_info.region_code = pmadapter->region_code;
		pget_info->param.fw_info.ecsa_enable = pmadapter->ecsa_enable;
		pget_info->param.fw_info.getlog_enable =
			pmadapter->getlog_enable;
		pget_info->param.fw_info.hw_dev_mcs_support =
			pmadapter->hw_dev_mcs_support;
		pget_info->param.fw_info.hw_dot_11n_dev_cap =
			pmadapter->hw_dot_11n_dev_cap;
		pget_info->param.fw_info.usr_dev_mcs_support =
			pmpriv->usr_dev_mcs_support;
		pget_info->param.fw_info.fw_supplicant_support =
			IS_FW_SUPPORT_SUPPLICANT(pmadapter) ? 0x01 : 0x00;
		pget_info->param.fw_info.antinfo = pmadapter->antinfo;
		pget_info->param.fw_info.max_ap_assoc_sta =
			pmadapter->max_sta_conn;
		pget_info->param.fw_info.fw_roaming_support =
			(pmadapter->
			 fw_cap_info & FW_ROAMING_SUPPORT) ? 0x01 : 0x00;
		break;
	case MLAN_OID_GET_BSS_INFO:
		status = wlan_get_info_bss_info(pmadapter, pioctl_req);
		break;
	case MLAN_OID_GET_DEBUG_INFO:
		status = wlan_get_info_debug_info(pmadapter, pioctl_req);
		break;
	case MLAN_OID_GET_VER_EXT:
		status = wlan_get_info_ver_ext(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief Set/Get SNMP MIB handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_snmp_mib_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;
	t_u16 cmd_oid = 0;
	mlan_ds_snmp_mib *mib = MNULL;
	t_u32 value = 0;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_snmp_mib)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_snmp_mib);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	mib = (mlan_ds_snmp_mib *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	switch (mib->sub_command) {
	case MLAN_OID_SNMP_MIB_RTS_THRESHOLD:
		value = mib->param.rts_threshold;
		cmd_oid = RtsThresh_i;
		break;
	case MLAN_OID_SNMP_MIB_FRAG_THRESHOLD:
		value = mib->param.frag_threshold;
		cmd_oid = FragThresh_i;
		break;
	case MLAN_OID_SNMP_MIB_RETRY_COUNT:
		value = mib->param.retry_count;
		cmd_oid = ShortRetryLim_i;
		break;
	case MLAN_OID_SNMP_MIB_DTIM_PERIOD:
		value = mib->param.dtim_period;
		cmd_oid = DtimPeriod_i;
		break;
	case MLAN_OID_SNMP_MIB_SIGNALEXT_ENABLE:
		value = mib->param.signalext_enable;
		cmd_oid = SignalextEnable_i;
		break;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       cmd_action,
			       cmd_oid, (t_void *)pioctl_req, &value);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Infra/Ad-hoc band configuration
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_radio_ioctl_band_cfg(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	t_u8 i, global_band = 0;
	t_u8 chan_offset;
	t_u8 infra_band = 0;
	t_u8 adhoc_band = 0;
	t_u32 adhoc_channel = 0;
	mlan_ds_radio_cfg *radio_cfg = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		infra_band = (t_u8)radio_cfg->param.band_cfg.config_bands;
		adhoc_band = (t_u8)radio_cfg->param.band_cfg.adhoc_start_band;
		adhoc_channel = radio_cfg->param.band_cfg.adhoc_channel;

		/* SET Infra band */
		if ((infra_band | pmadapter->fw_bands) & ~pmadapter->fw_bands) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		/* SET Ad-hoc Band */
		if ((adhoc_band | pmadapter->fw_bands) & ~pmadapter->fw_bands) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		if (!adhoc_band)
			adhoc_band = pmadapter->adhoc_start_band;

		for (i = 0; i < pmadapter->priv_num; i++) {
			if (pmadapter->priv[i] && pmadapter->priv[i] != pmpriv
			    && GET_BSS_ROLE(pmadapter->priv[i]) ==
			    MLAN_BSS_ROLE_STA)
				global_band |= pmadapter->priv[i]->config_bands;
		}
		global_band |= infra_band;

		if (wlan_set_regiontable
		    (pmpriv, (t_u8)pmadapter->region_code,
		     global_band | adhoc_band)) {
			pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}

		if (wlan_11d_set_universaltable
		    (pmpriv, global_band | adhoc_band)) {
			pioctl_req->status_code = MLAN_ERROR_IOCTL_FAIL;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pmpriv->config_bands = infra_band;
		pmadapter->config_bands = global_band;

		pmadapter->adhoc_start_band = adhoc_band;
		pmpriv->intf_state_11h.adhoc_auto_sel_chan = MFALSE;
		pmadapter->chan_bandwidth =
			(t_u8)radio_cfg->param.band_cfg.adhoc_chan_bandwidth;
		/*
		 * If no adhoc_channel is supplied verify if the existing
		 * adhoc channel compiles with new adhoc_band
		 */
		if (!adhoc_channel) {
			if (!wlan_find_cfp_by_band_and_channel
			    (pmadapter, pmadapter->adhoc_start_band,
			     pmpriv->adhoc_channel)) {
				/* Pass back the default channel */
				radio_cfg->param.band_cfg.adhoc_channel =
					DEFAULT_AD_HOC_CHANNEL;
				if ((pmadapter->adhoc_start_band & BAND_A)
				    || (pmadapter->adhoc_start_band & BAND_AN)
					) {
					radio_cfg->param.band_cfg.
						adhoc_channel =
						DEFAULT_AD_HOC_CHANNEL_A;
				}
			}
		} else {
			/* Return error if adhoc_band and adhoc_channel
			 * combination is invalid
			 */
			if (!wlan_find_cfp_by_band_and_channel
			    (pmadapter, pmadapter->adhoc_start_band,
			     (t_u16)adhoc_channel)) {
				pioctl_req->status_code =
					MLAN_ERROR_INVALID_PARAMETER;
				LEAVE();
				return MLAN_STATUS_FAILURE;
			}
			pmpriv->adhoc_channel = (t_u8)adhoc_channel;
		}

		/* validate the channel offset */
		chan_offset = wlan_validate_chan_offset(pmpriv,
							pmadapter->
							adhoc_start_band,
							pmpriv->adhoc_channel,
							pmadapter->
							chan_bandwidth);

		if (chan_offset != SEC_CHAN_NONE) {
			if (chan_offset == SEC_CHAN_ABOVE)
				pmadapter->chan_bandwidth =
					CHANNEL_BW_40MHZ_ABOVE;
			else
				pmadapter->chan_bandwidth =
					CHANNEL_BW_40MHZ_BELOW;
		}
		if ((adhoc_band & BAND_GN)
		    || (adhoc_band & BAND_AN)
			) {
			pmadapter->adhoc_11n_enabled = MTRUE;
		} else {
			pmadapter->adhoc_11n_enabled = MFALSE;
		}
	} else {
		/* Infra Bands   */
		radio_cfg->param.band_cfg.config_bands = pmpriv->config_bands;
		/* Adhoc Band    */
		radio_cfg->param.band_cfg.adhoc_start_band =
			pmadapter->adhoc_start_band;
		/* Adhoc Channel */
		radio_cfg->param.band_cfg.adhoc_channel = pmpriv->adhoc_channel;
		/* FW support Bands */
		radio_cfg->param.band_cfg.fw_bands = pmadapter->fw_bands;
		PRINTM(MINFO, "Global config band = %d\n",
		       pmadapter->config_bands);
		/* adhoc channel bandwidth */
		radio_cfg->param.band_cfg.adhoc_chan_bandwidth =
			pmadapter->chan_bandwidth;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Radio command handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_radio_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_radio_cfg *radio_cfg = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_radio_cfg)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_radio_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	radio_cfg = (mlan_ds_radio_cfg *)pioctl_req->pbuf;
	switch (radio_cfg->sub_command) {
	case MLAN_OID_RADIO_CTRL:
		status = wlan_radio_ioctl_radio_ctl(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BAND_CFG:
		status = wlan_radio_ioctl_band_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_ANT_CFG:
		status = wlan_radio_ioctl_ant_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_REMAIN_CHAN_CFG:
		status = wlan_radio_ioctl_remain_chan_cfg(pmadapter,
							  pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief Set/Get MAC address
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_mac_address(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	t_u16 cmd_action;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		cmd_action = HostCmd_ACT_GEN_GET;
	} else {
		cmd_action = HostCmd_ACT_GEN_SET;
		memcpy(pmadapter, pmpriv->curr_addr, &bss->param.mac_addr,
		       MLAN_MAC_ADDR_LENGTH);
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_MAC_ADDRESS,
			       cmd_action, 0, (t_void *)pioctl_req, MNULL);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set multicast list
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_set_multicast_list(IN pmlan_adapter pmadapter,
				  IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 old_pkt_filter;

	ENTER();

	old_pkt_filter = pmpriv->curr_pkt_filter;
	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}
	pioctl_req->data_read_written =
		sizeof(mlan_multicast_list) + MLAN_SUB_COMMAND_SIZE;
	if (bss->param.multicast_list.mode == MLAN_PROMISC_MODE) {
		PRINTM(MINFO, "Enable Promiscuous mode\n");
		pmpriv->curr_pkt_filter |= HostCmd_ACT_MAC_PROMISCUOUS_ENABLE;
		pmpriv->curr_pkt_filter &=
			~HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE;
	} else {
		/* Multicast */
		pmpriv->curr_pkt_filter &= ~HostCmd_ACT_MAC_PROMISCUOUS_ENABLE;
		if (bss->param.multicast_list.mode == MLAN_ALL_MULTI_MODE) {
			PRINTM(MINFO, "Enabling All Multicast!\n");
			pmpriv->curr_pkt_filter |=
				HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE;
		} else {
			pmpriv->curr_pkt_filter &=
				~HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE;
			if (bss->param.multicast_list.num_multicast_addr) {
				PRINTM(MINFO, "Set multicast list=%d\n",
				       bss->param.multicast_list.
				       num_multicast_addr);
				/* Set multicast addresses to firmware */
				if (old_pkt_filter == pmpriv->curr_pkt_filter) {
					/* Send request to firmware */
					ret = wlan_prepare_cmd(pmpriv,
							       HostCmd_CMD_MAC_MULTICAST_ADR,
							       HostCmd_ACT_GEN_SET,
							       0,
							       (t_void *)
							       pioctl_req,
							       &bss->param.
							       multicast_list);
					if (ret == MLAN_STATUS_SUCCESS)
						ret = MLAN_STATUS_PENDING;
				} else {
					/* Send request to firmware */
					ret = wlan_prepare_cmd(pmpriv,
							       HostCmd_CMD_MAC_MULTICAST_ADR,
							       HostCmd_ACT_GEN_SET,
							       0, MNULL,
							       &bss->param.
							       multicast_list);
				}
				if (ret)
					goto exit;
			}
		}
	}
	PRINTM(MINFO, "old_pkt_filter=0x%x, curr_pkt_filter=0x%x\n",
	       old_pkt_filter, pmpriv->curr_pkt_filter);
	if (old_pkt_filter != pmpriv->curr_pkt_filter) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_MAC_CONTROL,
				       HostCmd_ACT_GEN_SET,
				       0,
				       (t_void *)pioctl_req,
				       &pmpriv->curr_pkt_filter);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get channel list
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_get_channel_list(IN pmlan_adapter pmadapter,
				IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	chan_freq_power_t *cfp;
	t_u32 i, j;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action != MLAN_ACT_GET) {
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	if ((wlan_11d_is_enabled(pmpriv) &&
	     pmpriv->media_connected == MTRUE) &&
	    ((pmpriv->bss_mode == MLAN_BSS_MODE_INFRA) ||
	     (pmpriv->bss_mode == MLAN_BSS_MODE_IBSS &&
	      pmpriv->adhoc_state != ADHOC_STARTED))
		) {
		t_u8 chan_no;
		t_u8 band;

		parsed_region_chan_11d_t *parsed_region_chan = MNULL;
		parsed_region_chan_11d_t region_chan;

		BSSDescriptor_t *pbss_desc =
			&pmpriv->curr_bss_params.bss_descriptor;

		memset(pmadapter, &region_chan, 0,
		       sizeof(parsed_region_chan_11d_t));

		/*If country IE is present in the associated AP then return the channel list from country IE
		   else return it from the learning table */

		if (wlan_11d_parse_domain_info
		    (pmadapter, &pbss_desc->country_info,
		     (t_u8)pbss_desc->bss_band,
		     &region_chan) == MLAN_STATUS_SUCCESS) {

			parsed_region_chan = &region_chan;
		} else {
			parsed_region_chan = &pmadapter->parsed_region_chan;
		}

		PRINTM(MINFO, "no_of_chan=%d\n",
		       parsed_region_chan->no_of_chan);

		for (i = 0;
		     (bss->param.chanlist.num_of_chan < MLAN_MAX_CHANNEL_NUM)
		     && (i < parsed_region_chan->no_of_chan); i++) {
			chan_no = parsed_region_chan->chan_pwr[i].chan;
			band = parsed_region_chan->chan_pwr[i].band;
			PRINTM(MINFO, "band=%d, chan_no=%d\n", band, chan_no);
			bss->param.chanlist.cf[bss->param.chanlist.num_of_chan].
				channel = (t_u32)chan_no;
			bss->param.chanlist.cf[bss->param.chanlist.num_of_chan].
				freq =
				(t_u32)wlan_11d_chan_2_freq(pmadapter, chan_no,
							    band);
			bss->param.chanlist.num_of_chan++;
		}
	} else {
		for (j = 0;
		     (bss->param.chanlist.num_of_chan < MLAN_MAX_CHANNEL_NUM)
		     && (j < MAX_REGION_CHANNEL_NUM); j++) {
			cfp = pmadapter->region_channel[j].pcfp;
			for (i = 0;
			     (bss->param.chanlist.num_of_chan <
			      MLAN_MAX_CHANNEL_NUM)
			     && pmadapter->region_channel[j].valid && cfp &&
			     (i < pmadapter->region_channel[j].num_cfp); i++) {
				bss->param.chanlist.cf[bss->param.chanlist.
						       num_of_chan].channel =
					(t_u32)cfp->channel;
				bss->param.chanlist.cf[bss->param.chanlist.
						       num_of_chan].freq =
					(t_u32)cfp->freq;
				bss->param.chanlist.num_of_chan++;
				cfp++;
			}
		}
	}

	PRINTM(MINFO, "num of channel=%d\n", bss->param.chanlist.num_of_chan);

	LEAVE();
	return ret;
}

/** Highest channel used in 2.4GHz band */
#define MAX_CHANNEL_BAND_B    (14)

/** Highest frequency used in 2.4GHz band */
#define MAX_FREQUENCY_BAND_B  (2484)

/**
 *  @brief Set/Get BSS channel
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_channel(IN pmlan_adapter pmadapter,
		       IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = MNULL;
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	chan_freq_power_t *cfp = MNULL;
	ENTER();

	if ((pioctl_req == MNULL) || (pioctl_req->pbuf == MNULL)) {
		PRINTM(MERROR, "Request buffer not found!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	pmpriv = pmadapter->priv[pioctl_req->bss_index];
	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		cfp = wlan_find_cfp_by_band_and_channel(pmadapter,
							pmpriv->curr_bss_params.
							band,
							(t_u16)pmpriv->
							curr_bss_params.
							bss_descriptor.channel);
		if (cfp) {
			bss->param.bss_chan.channel = cfp->channel;
			bss->param.bss_chan.freq = cfp->freq;
		} else {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
		}
		pioctl_req->data_read_written =
			sizeof(chan_freq) + MLAN_SUB_COMMAND_SIZE;
		LEAVE();
		return ret;
	}
	if (!bss->param.bss_chan.channel && !bss->param.bss_chan.freq) {
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	if (pmadapter->adhoc_start_band & BAND_AN)
		pmadapter->adhoc_start_band = BAND_G | BAND_B | BAND_GN;
	else if (pmadapter->adhoc_start_band & BAND_A)
		pmadapter->adhoc_start_band = BAND_G | BAND_B;
	if (bss->param.bss_chan.channel) {
		if (bss->param.bss_chan.channel <= MAX_CHANNEL_BAND_B)
			cfp = wlan_find_cfp_by_band_and_channel(pmadapter,
								BAND_B,
								(t_u16)bss->
								param.bss_chan.
								channel);
		if (!cfp) {
			cfp = wlan_find_cfp_by_band_and_channel(pmadapter,
								BAND_A,
								(t_u16)bss->
								param.bss_chan.
								channel);
			if (cfp) {
				if (pmadapter->adhoc_11n_enabled)
					pmadapter->adhoc_start_band =
						BAND_A | BAND_AN;
				else
					pmadapter->adhoc_start_band = BAND_A;
			}
		}
	} else {
		if (bss->param.bss_chan.freq <= MAX_FREQUENCY_BAND_B)
			cfp = wlan_find_cfp_by_band_and_freq(pmadapter, BAND_B,
							     bss->param.
							     bss_chan.freq);
		if (!cfp) {
			cfp = wlan_find_cfp_by_band_and_freq(pmadapter, BAND_A,
							     bss->param.
							     bss_chan.freq);
			if (cfp) {
				if (pmadapter->adhoc_11n_enabled)
					pmadapter->adhoc_start_band =
						BAND_A | BAND_AN;
				else
					pmadapter->adhoc_start_band = BAND_A;
			}
		}
	}
	if (!cfp || !cfp->channel) {
		PRINTM(MERROR, "Invalid channel/freq\n");
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_FAILURE;

	}
	pmpriv->adhoc_channel = (t_u8)cfp->channel;
	pmpriv->intf_state_11h.adhoc_auto_sel_chan = MFALSE;
	bss->param.bss_chan.channel = cfp->channel;
	bss->param.bss_chan.freq = cfp->freq;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get BSS mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_mode(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	bss = (mlan_ds_bss *)pioctl_req->pbuf;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_GET) {
		bss->param.bss_mode = pmpriv->bss_mode;
		pioctl_req->data_read_written =
			sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
		goto exit;
	}

	if ((pmpriv->bss_mode == bss->param.bss_mode) ||
	    (bss->param.bss_mode == MLAN_BSS_MODE_AUTO)) {
		PRINTM(MINFO, "Already set to required mode! No change!\n");
		pmpriv->bss_mode = bss->param.bss_mode;
		goto exit;
	}

	if (pmpriv->bss_mode != MLAN_BSS_MODE_AUTO)
		ret = wlan_disconnect(pmpriv, MNULL, MNULL);
	else
		ret = wlan_disconnect(pmpriv, pioctl_req, MNULL);

	if (pmpriv->sec_info.authentication_mode != MLAN_AUTH_MODE_AUTO)
		pmpriv->sec_info.authentication_mode = MLAN_AUTH_MODE_OPEN;
	pmpriv->bss_mode = bss->param.bss_mode;

	if (pmpriv->bss_mode == MLAN_BSS_MODE_INFRA)
		pmpriv->port_ctrl_mode = MTRUE;
	else
		pmpriv->port_ctrl_mode = MFALSE;

	if ((pmpriv->bss_mode != MLAN_BSS_MODE_AUTO)
		) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_SET_BSS_MODE,
				       HostCmd_ACT_GEN_SET,
				       0, (t_void *)pioctl_req, MNULL);
		if (ret == MLAN_STATUS_SUCCESS)
			ret = MLAN_STATUS_PENDING;
	}
exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Start BSS
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_start(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = (mlan_ds_bss *)pioctl_req->pbuf;
	t_s32 i = -1;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };

	ENTER();

	if (pmadapter->enable_net_mon == CHANNEL_SPEC_SNIFFER_MODE) {
		PRINTM(MINFO,
		       "Association is blocked in Channel Specified Network Monitor mode...\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Before ASSOC REQ, If "port ctrl" mode is enabled,
	 * move the port to CLOSED state */
	if (pmpriv->port_ctrl_mode == MTRUE) {
		PRINTM(MINFO, "bss_ioctl_start(): port_state=CLOSED\n");
		pmpriv->prior_port_status = pmpriv->port_open;
		pmpriv->port_open = MFALSE;
	}
	pmadapter->scan_block = MFALSE;

	if (pmpriv->bss_mode == MLAN_BSS_MODE_INFRA) {
		if (!bss->param.ssid_bssid.idx ||
		    bss->param.ssid_bssid.idx > pmadapter->num_in_scan_table) {
			/* Search for the requested SSID in the scan table */
			if (bss->param.ssid_bssid.ssid.ssid_len) {
				if (memcmp
				    (pmadapter, &bss->param.ssid_bssid.bssid,
				     zero_mac, sizeof(zero_mac)))
					i = wlan_find_ssid_in_list(pmpriv,
								   &bss->param.
								   ssid_bssid.
								   ssid,
								   (t_u8 *)
								   &bss->param.
								   ssid_bssid.
								   bssid,
								   MLAN_BSS_MODE_INFRA);
				else
					i = wlan_find_ssid_in_list(pmpriv,
								   &bss->param.
								   ssid_bssid.
								   ssid, MNULL,
								   MLAN_BSS_MODE_INFRA);
			} else {
				i = wlan_find_bssid_in_list(pmpriv,
							    (t_u8 *)&bss->param.
							    ssid_bssid.bssid,
							    MLAN_BSS_MODE_INFRA);
			}
		} else {
			/* use bsslist index number to assoicate */
			i = wlan_is_network_compatible(pmpriv,
						       bss->param.ssid_bssid.
						       idx - 1,
						       pmpriv->bss_mode);
		}
		if (i >= 0) {
			/* block if upper-layer tries to reconnect before new scan */
			if (wlan_11h_get_csa_closed_channel(pmpriv) ==
			    (t_u8)pmadapter->pscan_table[i].channel) {
				PRINTM(MINFO,
				       "Attempt to reconnect on csa_closed_chan(%d)\n",
				       pmadapter->pscan_table[i].channel);
				pioctl_req->status_code =
					MLAN_ERROR_INVALID_PARAMETER;
				ret = MLAN_STATUS_FAILURE;
				goto start_ssid_done;
			}
			PRINTM(MINFO,
			       "SSID found in scan list ... associating...\n");

			/* Clear any past association response stored for application retrieval */
			pmpriv->assoc_rsp_size = 0;
			pmpriv->curr_chan_flags =
				bss->param.ssid_bssid.channel_flags;
			ret = wlan_associate(pmpriv, pioctl_req,
					     &pmadapter->pscan_table[i]);
			if (ret)
				goto start_ssid_done;
		} else {	/* i >= 0 */
			PRINTM(MERROR,
			       "SSID not found in scan list: ssid=%s, " MACSTR
			       ", idx=%d\n", bss->param.ssid_bssid.ssid.ssid,
			       MAC2STR(bss->param.ssid_bssid.bssid),
			       (int)bss->param.ssid_bssid.idx);
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto start_ssid_done;
		}
	} else {
		/* Adhoc mode */
		/* If the requested SSID matches current SSID, return */
		if (bss->param.ssid_bssid.ssid.ssid_len &&
		    (!wlan_ssid_cmp
		     (pmadapter, &pmpriv->curr_bss_params.bss_descriptor.ssid,
		      &bss->param.ssid_bssid.ssid))) {
			ret = MLAN_STATUS_SUCCESS;
			goto start_ssid_done;
		}

		/* Exit Adhoc mode first */
		PRINTM(MINFO, "Sending Adhoc Stop\n");
		ret = wlan_disconnect(pmpriv, MNULL, MNULL);
		if (ret)
			goto start_ssid_done;

		pmpriv->adhoc_is_link_sensed = MFALSE;

		if (!bss->param.ssid_bssid.idx ||
		    bss->param.ssid_bssid.idx > pmadapter->num_in_scan_table) {
			/* Search for the requested network in the scan table */
			if (bss->param.ssid_bssid.ssid.ssid_len) {
				i = wlan_find_ssid_in_list(pmpriv,
							   &bss->param.
							   ssid_bssid.ssid,
							   MNULL,
							   MLAN_BSS_MODE_IBSS);
			} else {
				i = wlan_find_bssid_in_list(pmpriv,
							    (t_u8 *)&bss->param.
							    ssid_bssid.bssid,
							    MLAN_BSS_MODE_IBSS);
			}
		} else {
			/* use bsslist index number to assoicate */
			i = wlan_is_network_compatible(pmpriv,
						       bss->param.ssid_bssid.
						       idx - 1,
						       pmpriv->bss_mode);
		}

		if (i >= 0) {
			PRINTM(MINFO,
			       "Network found in scan list ... joining ...\n");
			pmpriv->curr_chan_flags =
				bss->param.ssid_bssid.channel_flags;
			ret = wlan_adhoc_join(pmpriv, pioctl_req,
					      &pmadapter->pscan_table[i]);
			if (ret)
				goto start_ssid_done;
			if (pmpriv->adhoc_aes_enabled)
				wlan_enable_aes_key(pmpriv);
		} else {	/* i >= 0 */
			PRINTM(MINFO, "Network not found in the list, "
			       "creating adhoc with ssid = %s\n",
			       bss->param.ssid_bssid.ssid.ssid);
			pmpriv->curr_chan_flags =
				bss->param.ssid_bssid.channel_flags;
			ret = wlan_adhoc_start(pmpriv, pioctl_req,
					       &bss->param.ssid_bssid.ssid);
			if (ret)
				goto start_ssid_done;
			if (pmpriv->adhoc_aes_enabled)
				wlan_enable_aes_key(pmpriv);
		}
	}

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

start_ssid_done:
	LEAVE();
	return ret;
}

/**
 *  @brief Stop BSS
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_stop(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = (mlan_ds_bss *)pioctl_req->pbuf;

	ENTER();

	ret = wlan_disconnect(pmpriv, pioctl_req, &bss->param.deauth_param);

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get IBSS channel
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_ibss_channel(IN pmlan_adapter pmadapter,
			    IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		if (pmpriv->media_connected == MFALSE) {
			bss->param.bss_chan.channel = pmpriv->adhoc_channel;
			goto exit;
		}
		cmd_action = HostCmd_ACT_GEN_GET;
	} else {
		cmd_action = HostCmd_ACT_GEN_SET;
		pmpriv->adhoc_channel = (t_u8)bss->param.bss_chan.channel;
		pmpriv->intf_state_11h.adhoc_auto_sel_chan = MFALSE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_RF_CHANNEL,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req,
			       &bss->param.bss_chan.channel);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Listen Interval
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl_listen_interval(IN pmlan_adapter pmadapter,
			       IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		bss->param.listen_interval = pmpriv->listen_interval;
	else
		pmpriv->listen_interval = bss->param.listen_interval;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/*
 *  @brief Set/Get beacon interval
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_bss_ioctl_beacon_interval(IN pmlan_adapter pmadapter,
			       IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	ENTER();
	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		bss->param.bcn_interval = pmpriv->beacon_period;
		if (pmpriv->media_connected == MTRUE)
			bss->param.bcn_interval =
				pmpriv->curr_bss_params.bss_descriptor.
				beacon_period;
	} else
		pmpriv->beacon_period = (t_u16)bss->param.bcn_interval;
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get ATIM window
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_bss_ioctl_atim_window(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	ENTER();
	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		bss->param.atim_window = pmpriv->atim_window;
		if (pmpriv->media_connected == MTRUE)
			bss->param.atim_window =
				pmpriv->curr_bss_params.bss_descriptor.
				atim_window;
	} else
		pmpriv->atim_window = (t_u16)bss->param.atim_window;

	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Query passphrase
 *
 *  @param priv                 A pointer to mlan_private structure
 *  @param pioctl_req           A pointer to ioctl request buffer
 *
 *  @return                     MLAN_STATUS_SUCCESS -- success, otherwise fail
 */
static mlan_status
wlan_query_passphrase(mlan_private *priv, pmlan_ioctl_req pioctl_req)
{
	mlan_adapter *pmadapter = priv->adapter;
	mlan_ds_bss *bss = MNULL;
	mlan_ssid_bssid *ssid_bssid = MNULL;
	mlan_ds_sec_cfg sec;
	mlan_ds_passphrase *sec_pp;
	int i = 0;
	BSSDescriptor_t *pbss_desc;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	ssid_bssid = &bss->param.ssid_bssid;

	memset(pmadapter, &sec, 0, sizeof(mlan_ds_sec_cfg));
	sec_pp = (mlan_ds_passphrase *)&sec.param.passphrase;
	sec_pp->psk_type = MLAN_PSK_QUERY;
	if (ssid_bssid->ssid.ssid_len == 0) {
		i = wlan_find_bssid_in_list(priv, (t_u8 *)&ssid_bssid->bssid,
					    MLAN_BSS_MODE_AUTO);
		if (i >= 0) {
			pbss_desc = &pmadapter->pscan_table[i];
			memcpy(pmadapter, (t_u8 *)&sec_pp->ssid,
			       &pbss_desc->ssid, sizeof(mlan_802_11_ssid));
		} else
			memcpy(pmadapter, (t_u8 *)&sec_pp->bssid,
			       &ssid_bssid->bssid, MLAN_MAC_ADDR_LENGTH);
	} else {
		memcpy(pmadapter, (t_u8 *)&sec_pp->ssid, &ssid_bssid->ssid,
		       sizeof(mlan_802_11_ssid));
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(priv,
			       HostCmd_CMD_SUPPLICANT_PMK,
			       HostCmd_ACT_GEN_GET,
			       0, (t_void *)pioctl_req, (t_void *)&sec);

	LEAVE();
	return ret;
}

/**
 *  @brief Search for a BSS
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_bss_ioctl_find_bss(IN pmlan_adapter pmadapter,
			IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
#ifdef DRV_EMBEDDED_SUPPLICANT
	mlan_ds_bss *bss = MNULL;
	mlan_ssid_bssid *ssid_bssid = MNULL;
#endif

	ENTER();

	if (pmpriv->ewpa_query) {
		if (wlan_query_passphrase(pmpriv, pioctl_req) ==
		    MLAN_STATUS_SUCCESS) {
			PRINTM(MINFO, "Find BSS ioctl: query passphrase\n");
			LEAVE();
			return MLAN_STATUS_PENDING;
		}
	}
#ifdef DRV_EMBEDDED_SUPPLICANT
	if (!IS_FW_SUPPORT_SUPPLICANT(pmpriv->adapter)) {
		bss = (mlan_ds_bss *)pioctl_req->pbuf;
		ssid_bssid = &bss->param.ssid_bssid;
		supplicantQueryPassphraseAndEnable(pmpriv->psapriv,
						   (t_u8 *)ssid_bssid);
	}
#endif

	ret = wlan_find_bss(pmpriv, pioctl_req);

	LEAVE();
	return ret;
}

/**
 *  @brief Check if BSS channel is valid for Station's region
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_bss_ioctl_bss_11d_check_channel(IN pmlan_adapter pmadapter,
				     IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_bss *bss = MNULL;
	mlan_ssid_bssid *ssid_bssid = MNULL;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;
	ssid_bssid = &bss->param.ssid_bssid;

	PRINTM(MINFO, "ssid: %s idx:%d\n", ssid_bssid->ssid.ssid,
	       ssid_bssid->idx);
	PRINTM(MINFO, "band:%d channel:%d\n", (t_u8)ssid_bssid->bss_band,
	       (t_u32)ssid_bssid->channel);

	/* check if this channel is supported in the region */
	if (!wlan_find_cfp_by_band_and_channel(pmadapter,
					       (t_u8)ssid_bssid->bss_band,
					       (t_u32)ssid_bssid->channel)) {
		PRINTM(MERROR, "Unsupported Channel for region 0x%x\n",
		       pmadapter->region_code);
		ret = MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief BSS command handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_bss_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_bss *bss = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_bss)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_bss);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	bss = (mlan_ds_bss *)pioctl_req->pbuf;

	switch (bss->sub_command) {
	case MLAN_OID_BSS_START:
		status = wlan_bss_ioctl_start(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_STOP:
		status = wlan_bss_ioctl_stop(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_MODE:
		status = wlan_bss_ioctl_mode(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_CHANNEL:
		status = wlan_bss_ioctl_channel(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_CHANNEL_LIST:
		status = wlan_bss_ioctl_get_channel_list(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_MAC_ADDR:
		status = wlan_bss_ioctl_mac_address(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_MULTICAST_LIST:
		status = wlan_bss_ioctl_set_multicast_list(pmadapter,
							   pioctl_req);
		break;
	case MLAN_OID_BSS_FIND_BSS:
		status = wlan_bss_ioctl_find_bss(pmadapter, pioctl_req);
		break;
	case MLAN_OID_IBSS_BCN_INTERVAL:
		status = wlan_bss_ioctl_beacon_interval(pmadapter, pioctl_req);
		break;
	case MLAN_OID_IBSS_ATIM_WINDOW:
		status = wlan_bss_ioctl_atim_window(pmadapter, pioctl_req);
		break;
	case MLAN_OID_IBSS_CHANNEL:
		status = wlan_bss_ioctl_ibss_channel(pmadapter, pioctl_req);
		break;
#if defined(STA_SUPPORT) && defined(UAP_SUPPORT)
	case MLAN_OID_BSS_ROLE:
		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &pmadapter->ioctl_pending_q,
				       (pmlan_linked_list)pioctl_req,
				       pmadapter->callbacks.moal_spin_lock,
				       pmadapter->callbacks.moal_spin_unlock);
		pmadapter->pending_ioctl = MTRUE;
		status = MLAN_STATUS_PENDING;
		break;
#endif
#ifdef WIFI_DIRECT_SUPPORT
	case MLAN_OID_WIFI_DIRECT_MODE:
		status = wlan_bss_ioctl_wifi_direct_mode(pmadapter, pioctl_req);
		break;
#endif
	case MLAN_OID_BSS_LISTEN_INTERVAL:
		status = wlan_bss_ioctl_listen_interval(pmadapter, pioctl_req);
		break;
	case MLAN_OID_BSS_REMOVE:
		status = wlan_bss_ioctl_bss_remove(pmadapter, pioctl_req);
		break;

	case MLAN_OID_BSS_11D_CHECK_CHANNEL:
		status = wlan_bss_ioctl_bss_11d_check_channel(pmadapter,
							      pioctl_req);
		break;
	case MLAN_OID_BSS_CHAN_INFO:
		status = wlan_bss_ioctl_get_chan_info(pmadapter, pioctl_req);
		break;
	default:
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief Get supported rates
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl_get_supported_rate(IN pmlan_adapter pmadapter,
				   IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_rate *rate = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	if (pioctl_req->action != MLAN_ACT_GET) {
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	rate = (mlan_ds_rate *)pioctl_req->pbuf;
	if (rate->param.rate_band_cfg.config_bands &&
	    rate->param.rate_band_cfg.bss_mode)
		wlan_get_active_data_rates(pmpriv,
					   rate->param.rate_band_cfg.bss_mode,
					   rate->param.rate_band_cfg.
					   config_bands, rate->param.rates);
	else
		wlan_get_active_data_rates(pmpriv, pmpriv->bss_mode,
					   (pmpriv->bss_mode ==
					    MLAN_BSS_MODE_INFRA) ? pmpriv->
					   config_bands : pmadapter->
					   adhoc_start_band, rate->param.rates);
	pioctl_req->data_read_written =
		MLAN_SUPPORTED_RATES + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Rate command handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_rate_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_rate *rate = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_rate)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_rate);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	rate = (mlan_ds_rate *)pioctl_req->pbuf;
	switch (rate->sub_command) {
	case MLAN_OID_RATE_CFG:
		status = wlan_rate_ioctl_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_GET_DATA_RATE:
		status = wlan_rate_ioctl_get_data_rate(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SUPPORTED_RATES:
		status = wlan_rate_ioctl_get_supported_rate(pmadapter,
							    pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief Get Tx power configuration
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param cmd_no	Firmware command number used to retrieve power values
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_power_ioctl_get_power(IN pmlan_adapter pmadapter,
			   IN t_u16 cmd_no, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       cmd_no,
			       HostCmd_ACT_GEN_GET,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set Tx power configuration
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_power_ioctl_set_power(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_ds_power_cfg *power = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	HostCmd_DS_TXPWR_CFG *txp_cfg = MNULL;
	MrvlTypes_Power_Group_t *pg_tlv = MNULL;
	Power_Group_t *pg = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 *buf = MNULL;
	t_s8 dbm = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	power = (mlan_ds_power_cfg *)pioctl_req->pbuf;
	if (!power->param.power_cfg.is_power_auto) {
		dbm = (t_s8)power->param.power_cfg.power_level;
		if ((dbm < pmpriv->min_tx_power_level) ||
		    (dbm > pmpriv->max_tx_power_level)) {
			PRINTM(MERROR,
			       "The set txpower value %d dBm is out of range (%d dBm-%d dBm)!\n",
			       dbm, pmpriv->min_tx_power_level,
			       pmpriv->max_tx_power_level);
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
	}

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       MRVDRV_SIZE_OF_CMD_BUFFER, MLAN_MEM_DEF, &buf);
	if (ret != MLAN_STATUS_SUCCESS || buf == MNULL) {
		PRINTM(MERROR,
		       "ALLOC_CMD_BUF: Failed to allocate command buffer\n");
		pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}
	memset(pmadapter, buf, 0, MRVDRV_SIZE_OF_CMD_BUFFER);
	txp_cfg = (HostCmd_DS_TXPWR_CFG *)buf;
	txp_cfg->action = HostCmd_ACT_GEN_SET;
	if (!power->param.power_cfg.is_power_auto) {
		txp_cfg->mode = 1;
		pg_tlv = (MrvlTypes_Power_Group_t *)(buf +
						     sizeof
						     (HostCmd_DS_TXPWR_CFG));
		pg_tlv->type = TLV_TYPE_POWER_GROUP;
		pg_tlv->length = 4 * sizeof(Power_Group_t);
		pg = (Power_Group_t *)(buf + sizeof(HostCmd_DS_TXPWR_CFG) +
				       sizeof(MrvlTypes_Power_Group_t));
		/* Power group for modulation class HR/DSSS */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x03;
		pg->modulation_class = MOD_CLASS_HR_DSSS;
		pg->power_step = 0;
		pg->power_min = (t_s8)dbm;
		pg->power_max = (t_s8)dbm;
		pg++;
		/* Power group for modulation class OFDM */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x07;
		pg->modulation_class = MOD_CLASS_OFDM;
		pg->power_step = 0;
		pg->power_min = (t_s8)dbm;
		pg->power_max = (t_s8)dbm;
		pg++;
		/* Power group for modulation class HTBW20 */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x20;
		pg->modulation_class = MOD_CLASS_HT;
		pg->power_step = 0;
		pg->power_min = (t_s8)dbm;
		pg->power_max = (t_s8)dbm;
		pg->ht_bandwidth = HT_BW_20;
		pg++;
		/* Power group for modulation class HTBW40 */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x20;
		pg->modulation_class = MOD_CLASS_HT;
		pg->power_step = 0;
		pg->power_min = (t_s8)dbm;
		pg->power_max = (t_s8)dbm;
		pg->ht_bandwidth = HT_BW_40;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_TXPWR_CFG,
			       HostCmd_ACT_GEN_SET,
			       0, (t_void *)pioctl_req, buf);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	if (buf)
		pcb->moal_mfree(pmadapter->pmoal_handle, buf);

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set extended power configuration
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_power_ioctl_set_power_ext(IN pmlan_adapter pmadapter,
			       IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_power_cfg *power = MNULL;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 *buf = MNULL;
	HostCmd_DS_TXPWR_CFG *txp_cfg = MNULL;
	MrvlTypes_Power_Group_t *pg_tlv = MNULL;
	Power_Group_t *pg = MNULL;
	mlan_power_group *pwr_grp = MNULL;

	ENTER();

	power = (mlan_ds_power_cfg *)pioctl_req->pbuf;
	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       MRVDRV_SIZE_OF_CMD_BUFFER, MLAN_MEM_DEF, &buf);
	if (ret != MLAN_STATUS_SUCCESS || buf == MNULL) {
		PRINTM(MERROR,
		       "ALLOC_CMD_BUF: Failed to allocate command buffer\n");
		pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}
	memset(pmadapter, buf, 0, MRVDRV_SIZE_OF_CMD_BUFFER);
	txp_cfg = (HostCmd_DS_TXPWR_CFG *)buf;
	txp_cfg->action = HostCmd_ACT_GEN_SET;
	pwr_grp = &power->param.power_ext.power_group[0];
	if (pwr_grp->rate_format == TX_PWR_CFG_AUTO_CTRL_OFF)
		txp_cfg->mode = 0;
	else {
		txp_cfg->mode = 1;

		pg_tlv = (MrvlTypes_Power_Group_t *)(buf +
						     sizeof
						     (HostCmd_DS_TXPWR_CFG));
		pg_tlv->type = TLV_TYPE_POWER_GROUP;
		pg_tlv->length = sizeof(Power_Group_t);
		pg = (Power_Group_t *)(buf + sizeof(HostCmd_DS_TXPWR_CFG) +
				       sizeof(MrvlTypes_Power_Group_t));
		pg->ht_bandwidth = pwr_grp->bandwidth;
		pg->power_min = (t_s8)pwr_grp->power_min;
		pg->power_max = (t_s8)pwr_grp->power_max;
		pg->power_step = (t_s8)pwr_grp->power_step;

		if (pwr_grp->rate_format == MLAN_RATE_FORMAT_LG) {
			if (pwr_grp->first_rate_ind <= MLAN_RATE_INDEX_HRDSSS3) {
				pg->modulation_class = MOD_CLASS_HR_DSSS;
			} else {
				pg->modulation_class = MOD_CLASS_OFDM;
				pwr_grp->first_rate_ind -=
					MLAN_RATE_INDEX_OFDM0;
				pwr_grp->last_rate_ind -= MLAN_RATE_INDEX_OFDM0;
			}
			pg->first_rate_code = (t_u8)pwr_grp->first_rate_ind;
			pg->last_rate_code = (t_u8)pwr_grp->last_rate_ind;
		} else if (pwr_grp->rate_format == MLAN_RATE_FORMAT_HT) {
			pg->modulation_class = MOD_CLASS_HT;
			pg->first_rate_code = (t_u8)pwr_grp->first_rate_ind;
			pg->last_rate_code = (t_u8)pwr_grp->last_rate_ind;
		} else {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
		}
	}
	if (ret == MLAN_STATUS_FAILURE) {
		if (buf)
			pcb->moal_mfree(pmadapter->pmoal_handle, buf);
		goto exit;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_TXPWR_CFG,
			       HostCmd_ACT_GEN_SET,
			       0, (t_void *)pioctl_req, buf);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	if (buf)
		pcb->moal_mfree(pmadapter->pmoal_handle, buf);

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Power configuration command handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_power_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_power_cfg *power = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_power_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_power_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	power = (mlan_ds_power_cfg *)pioctl_req->pbuf;
	switch (power->sub_command) {
	case MLAN_OID_POWER_CFG:
		if (pioctl_req->action == MLAN_ACT_GET)
			status = wlan_power_ioctl_get_power(pmadapter,
							    HostCmd_CMD_TXPWR_CFG,
							    pioctl_req);
		else
			status = wlan_power_ioctl_set_power(pmadapter,
							    pioctl_req);
		break;

	case MLAN_OID_POWER_CFG_EXT:
		if (pioctl_req->action == MLAN_ACT_GET)
			status = wlan_power_ioctl_get_power(pmadapter,
							    HostCmd_CMD_TXPWR_CFG,
							    pioctl_req);
		else
			status = wlan_power_ioctl_set_power_ext(pmadapter,
								pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief Set power save configurations
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *  @param ps_mode	Power save mode
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_pm_ioctl_ps_mode(IN pmlan_adapter pmadapter,
		      IN pmlan_ioctl_req pioctl_req, IN t_u16 ps_mode)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 sub_cmd;

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET) {
		sub_cmd = (pmadapter->ps_mode == Wlan802_11PowerModePSP) ?
			EN_AUTO_PS : DIS_AUTO_PS;
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_PS_MODE_ENH, sub_cmd,
				       BITMAP_STA_PS, (t_void *)pioctl_req,
				       MNULL);
		if ((ret == MLAN_STATUS_SUCCESS) && (sub_cmd == DIS_AUTO_PS)) {
			ret = wlan_prepare_cmd(pmpriv,
					       HostCmd_CMD_802_11_PS_MODE_ENH,
					       GET_PS, 0, MNULL, MNULL);
		}
	} else {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_PS_MODE_ENH, GET_PS,
				       0, (t_void *)pioctl_req, MNULL);
	}

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Inactivity timeout extend
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_pm_ioctl_inactivity_timeout(IN pmlan_adapter pmadapter,
				 IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pmcfg = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	pmcfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	cmd_action = HostCmd_ACT_GEN_GET;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_INACTIVITY_TIMEOUT_EXT,
			       cmd_action, 0, (t_void *)pioctl_req,
			       (t_void *)&pmcfg->param.inactivity_to);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Enable/Disable Auto Deep Sleep
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_set_auto_deep_sleep(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv =
		(pmlan_private)pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_auto_ds auto_ds;
	t_u32 mode;

	ENTER();

	if (((mlan_ds_pm_cfg *)pioctl_req->pbuf)->param.auto_deep_sleep.
	    auto_ds == DEEP_SLEEP_ON) {
		auto_ds.auto_ds = DEEP_SLEEP_ON;
		PRINTM(MINFO, "Auto Deep Sleep: on\n");
		mode = EN_AUTO_PS;
	} else {
		auto_ds.auto_ds = DEEP_SLEEP_OFF;
		PRINTM(MINFO, "Auto Deep Sleep: off\n");
		mode = DIS_AUTO_PS;
	}
	if (((mlan_ds_pm_cfg *)pioctl_req->pbuf)->param.auto_deep_sleep.
	    idletime)
		auto_ds.idletime =
			((mlan_ds_pm_cfg *)pioctl_req->pbuf)->param.
			auto_deep_sleep.idletime;
	else
		auto_ds.idletime = pmadapter->idle_time;
	/* note: the command could be queued and executed
	   later if there is command in progress. */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_PS_MODE_ENH,
			       (t_u16)mode,
			       BITMAP_AUTO_DS, (t_void *)pioctl_req, &auto_ds);
	if (ret) {
		LEAVE();
		return ret;
	}
	ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get sleep period
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_set_get_sleep_pd(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pm_cfg = MNULL;
	t_u16 cmd_action = 0, sleep_pd = 0;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	cmd_action = HostCmd_ACT_GEN_GET;
	if (pioctl_req->action == MLAN_ACT_SET) {
		cmd_action = HostCmd_ACT_GEN_SET;
		sleep_pd = (t_u16)pm_cfg->param.sleep_period;
	}

	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_SLEEP_PERIOD,
			       cmd_action, 0, (t_void *)pioctl_req, &sleep_pd);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get PS configuration parameter
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_set_get_ps_cfg(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_ds_pm_cfg *pm_cfg = MNULL;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		pm_cfg->param.ps_cfg.ps_null_interval =
			(t_u32)pmadapter->null_pkt_interval;
		pm_cfg->param.ps_cfg.multiple_dtim_interval =
			(t_u32)pmadapter->multiple_dtim;
		pm_cfg->param.ps_cfg.listen_interval =
			(t_u32)pmadapter->local_listen_interval;
		pm_cfg->param.ps_cfg.adhoc_awake_period =
			(t_u32)pmadapter->adhoc_awake_period;
		pm_cfg->param.ps_cfg.bcn_miss_timeout =
			(t_u32)pmadapter->bcn_miss_time_out;
		pm_cfg->param.ps_cfg.delay_to_ps =
			(t_u32)pmadapter->delay_to_ps;
		pm_cfg->param.ps_cfg.ps_mode =
			(t_u32)pmadapter->enhanced_ps_mode;
	} else {
		if (pm_cfg->param.ps_cfg.ps_null_interval)
			pmadapter->null_pkt_interval =
				(t_u16)pm_cfg->param.ps_cfg.ps_null_interval;
		else
			pm_cfg->param.ps_cfg.ps_null_interval =
				(t_u32)pmadapter->null_pkt_interval;
		if (pm_cfg->param.ps_cfg.multiple_dtim_interval)
			pmadapter->multiple_dtim =
				(t_u16)pm_cfg->param.ps_cfg.
				multiple_dtim_interval;
		else
			pm_cfg->param.ps_cfg.multiple_dtim_interval =
				(t_u32)pmadapter->multiple_dtim;
		if (((t_s32)pm_cfg->param.ps_cfg.listen_interval) ==
		    MRVDRV_LISTEN_INTERVAL_DISABLE)
			pmadapter->local_listen_interval = 0;
		else if (pm_cfg->param.ps_cfg.listen_interval)
			pmadapter->local_listen_interval =
				(t_u16)pm_cfg->param.ps_cfg.listen_interval;
		else
			pm_cfg->param.ps_cfg.listen_interval =
				(t_u32)pmadapter->local_listen_interval;
		if (pm_cfg->param.ps_cfg.adhoc_awake_period)
			pmadapter->adhoc_awake_period =
				(t_u16)pm_cfg->param.ps_cfg.adhoc_awake_period;
		else
			pm_cfg->param.ps_cfg.adhoc_awake_period =
				(t_u32)pmadapter->adhoc_awake_period;
		if (pm_cfg->param.ps_cfg.bcn_miss_timeout)
			pmadapter->bcn_miss_time_out =
				(t_u16)pm_cfg->param.ps_cfg.bcn_miss_timeout;
		else
			pm_cfg->param.ps_cfg.bcn_miss_timeout =
				(t_u32)pmadapter->bcn_miss_time_out;
		if (pm_cfg->param.ps_cfg.delay_to_ps != DELAY_TO_PS_UNCHANGED)
			pmadapter->delay_to_ps =
				(t_u16)pm_cfg->param.ps_cfg.delay_to_ps;
		else
			pm_cfg->param.ps_cfg.delay_to_ps =
				(t_u32)pmadapter->delay_to_ps;
		if (pm_cfg->param.ps_cfg.ps_mode)
			pmadapter->enhanced_ps_mode =
				(t_u16)pm_cfg->param.ps_cfg.ps_mode;
		else
			pm_cfg->param.ps_cfg.ps_mode =
				(t_u32)pmadapter->enhanced_ps_mode;
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set/Get PS configuration parameter
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_set_get_bcn_timeout(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pm_cfg = MNULL;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_PS_MODE_ENH,
			       EN_AUTO_PS, BITMAP_BCN_TMO, (t_void *)pioctl_req,
			       &pm_cfg->param.bcn_timeout);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set the sleep parameters
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		        MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_set_get_sleep_params(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_pm_cfg *pm_cfg = MNULL;
	t_u16 cmd_action = 0;

	ENTER();

	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	cmd_action = HostCmd_ACT_GEN_GET;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_SLEEP_PARAMS,
			       cmd_action, 0, (t_void *)pioctl_req,
			       &pm_cfg->param.sleep_params);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief config management frame wakeup filter
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		        MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_config_mgmt_filter(IN pmlan_adapter pmadapter,
			IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_pm_cfg *pm_cfg = MNULL;
	int i = 0;

	ENTER();

	memset(pmadapter, pmadapter->mgmt_filter, 0,
	       sizeof(mlan_mgmt_frame_wakeup) * MAX_MGMT_FRAME_FILTER);
	pm_cfg = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		for (i = 0; i < MAX_MGMT_FRAME_FILTER; i++)
			if (!pm_cfg->param.mgmt_filter[i].type)
				break;
		memcpy(pmadapter, (t_u8 *)pmadapter->mgmt_filter,
		       (t_u8 *)pm_cfg->param.mgmt_filter,
		       (i + 1) * sizeof(mlan_mgmt_frame_wakeup));
	} else if (pioctl_req->action == MLAN_ACT_GET)
		PRINTM(MERROR, "Get not support\n");
	LEAVE();
	return ret;
}

/**
 *  @brief Power save command handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_pm_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_pm_cfg *pm = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_pm_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_pm_cfg);
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	pm = (mlan_ds_pm_cfg *)pioctl_req->pbuf;
	switch (pm->sub_command) {
	case MLAN_OID_PM_CFG_IEEE_PS:
		switch (pioctl_req->action) {
		case MLAN_ACT_SET:
		/**Block ieee power save disable command when bt coex enable*/
			if (pmadapter->coex_scan && !pm->param.ps_mode)
				break;
			if (pm->param.ps_mode)
				pmadapter->ps_mode = Wlan802_11PowerModePSP;
			else
				pmadapter->ps_mode = Wlan802_11PowerModeCAM;
			status = wlan_pm_ioctl_ps_mode(pmadapter, pioctl_req,
						       pmadapter->ps_mode);
			break;
		case MLAN_ACT_GET:
			status = wlan_pm_ioctl_ps_mode(pmadapter, pioctl_req,
						       pmadapter->ps_mode);
			break;
		default:
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			status = MLAN_STATUS_FAILURE;
			break;
		}
		break;
	case MLAN_OID_PM_CFG_HS_CFG:
		status = wlan_pm_ioctl_hscfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_CFG_INACTIVITY_TO:
		status = wlan_pm_ioctl_inactivity_timeout(pmadapter,
							  pioctl_req);
		break;
	case MLAN_OID_PM_CFG_DEEP_SLEEP:
		switch (pioctl_req->action) {
		case MLAN_ACT_SET:
			if (pmadapter->is_deep_sleep &&
			    pm->param.auto_deep_sleep.auto_ds ==
			    DEEP_SLEEP_ON) {
				PRINTM(MMSG,
				       "Station already in enhanced deep sleep mode\n");
				status = MLAN_STATUS_FAILURE;
				break;
			} else if (!pmadapter->is_deep_sleep &&
				   pm->param.auto_deep_sleep.auto_ds ==
				   DEEP_SLEEP_OFF) {
				PRINTM(MMSG,
				       "Station already not in enhanced deep sleep mode\n");
				status = MLAN_STATUS_FAILURE;
				break;
			}
			status = wlan_set_auto_deep_sleep(pmadapter,
							  pioctl_req);
			break;
		case MLAN_ACT_GET:
			if (pmadapter->is_deep_sleep) {
				pm->param.auto_deep_sleep.auto_ds =
					DEEP_SLEEP_ON;
				pm->param.auto_deep_sleep.idletime =
					pmadapter->idle_time;
			} else
				pm->param.auto_deep_sleep.auto_ds =
					DEEP_SLEEP_OFF;
			break;
		default:
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			status = MLAN_STATUS_FAILURE;
			break;
		}
		break;
	case MLAN_OID_PM_CFG_PS_CFG:
		status = wlan_set_get_ps_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_CFG_SLEEP_PD:
		status = wlan_set_get_sleep_pd(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_CFG_FW_WAKEUP_METHOD:
		status = wlan_fw_wakeup_method(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_CFG_SLEEP_PARAMS:
		status = wlan_set_get_sleep_params(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_INFO:
		status = wlan_get_pm_info(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_HS_WAKEUP_REASON:
		status = wlan_get_hs_wakeup_reason(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_MGMT_FILTER:
		status = wlan_config_mgmt_filter(pmadapter, pioctl_req);
		break;
	case MLAN_OID_PM_CFG_BCN_TIMEOUT:
		status = wlan_set_get_bcn_timeout(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief Set/Get WPA IE
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param ie_data_ptr  A pointer to IE
 *  @param ie_len       Length of the IE
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_set_wpa_ie_helper(mlan_private *priv, t_u8 *ie_data_ptr, t_u16 ie_len)
{
	ENTER();

	if (ie_len) {
		if (ie_len > sizeof(priv->wpa_ie)) {
			PRINTM(MERROR, "failed to copy, WPA IE is too big\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		memcpy(priv->adapter, priv->wpa_ie, ie_data_ptr, ie_len);
		priv->wpa_ie_len = (t_u8)ie_len;
		PRINTM(MIOCTL, "Set Wpa_ie_len=%d IE=%#x\n", priv->wpa_ie_len,
		       priv->wpa_ie[0]);
		DBG_HEXDUMP(MCMD_D, "Wpa_ie", priv->wpa_ie, priv->wpa_ie_len);
		if (priv->wpa_ie[0] == WPA_IE) {
			priv->sec_info.wpa_enabled = MTRUE;
		} else if (priv->wpa_ie[0] == RSN_IE) {
			priv->sec_info.wpa2_enabled = MTRUE;
		} else {
			priv->sec_info.wpa_enabled = MFALSE;
			priv->sec_info.wpa2_enabled = MFALSE;
		}
	} else {
		memset(priv->adapter, priv->wpa_ie, 0, sizeof(priv->wpa_ie));
		priv->wpa_ie_len = 0;
		PRINTM(MINFO, "Reset Wpa_ie_len=%d IE=%#x\n", priv->wpa_ie_len,
		       priv->wpa_ie[0]);
		priv->sec_info.wpa_enabled = MFALSE;
		priv->sec_info.wpa2_enabled = MFALSE;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set WAPI IE
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param ie_data_ptr  A pointer to IE
 *  @param ie_len       Length of the IE
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_set_wapi_ie(mlan_private *priv, t_u8 *ie_data_ptr, t_u16 ie_len)
{
	ENTER();
	if (ie_len) {
		if (ie_len > sizeof(priv->wapi_ie)) {
			PRINTM(MWARN, "failed to copy, WAPI IE is too big\n");
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		memcpy(priv->adapter, priv->wapi_ie, ie_data_ptr, ie_len);
		priv->wapi_ie_len = (t_u8)ie_len;
		PRINTM(MIOCTL, "Set wapi_ie_len=%d IE=%#x\n", priv->wapi_ie_len,
		       priv->wapi_ie[0]);
		DBG_HEXDUMP(MCMD_D, "wapi_ie", priv->wapi_ie,
			    priv->wapi_ie_len);
		if (priv->wapi_ie[0] == WAPI_IE)
			priv->sec_info.wapi_enabled = MTRUE;
	} else {
		memset(priv->adapter, priv->wapi_ie, 0, sizeof(priv->wapi_ie));
		priv->wapi_ie_len = (t_u8)ie_len;
		PRINTM(MINFO, "Reset wapi_ie_len=%d IE=%#x\n",
		       priv->wapi_ie_len, priv->wapi_ie[0]);
		priv->sec_info.wapi_enabled = MFALSE;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set/Get WAPI status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_sec_ioctl_wapi_enable(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		if (pmpriv->wapi_ie_len)
			sec->param.wapi_enabled = MTRUE;
		else
			sec->param.wapi_enabled = MFALSE;
	} else {
		if (sec->param.wapi_enabled == MFALSE)
			wlan_set_wapi_ie(pmpriv, MNULL, 0);
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Set WAPI key
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_set_wapi_key(IN pmlan_adapter pmadapter,
			    IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_KEY_MATERIAL,
			       HostCmd_ACT_GEN_SET,
			       KEY_INFO_ENABLED,
			       (t_void *)pioctl_req, &sec->param.encrypt_key);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get Port Control status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_port_ctrl_enable(IN pmlan_adapter pmadapter,
				IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_sec_cfg *sec = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		if (pmpriv->port_ctrl_mode)
			sec->param.port_ctrl_enabled = MTRUE;
		else
			sec->param.port_ctrl_enabled = MFALSE;
	} else {
		if (sec->param.port_ctrl_enabled) {
			pmpriv->port_ctrl_mode = MTRUE;
			pmpriv->port_open = MFALSE;
		} else {
			if (pmpriv->port_ctrl_mode == MTRUE) {
				pmpriv->port_ctrl_mode = MFALSE;
				/* Cleanup the bypass TX queue */
				wlan_cleanup_bypass_txq(pmpriv);
			}
		}
	}
	PRINTM(MINFO, "port_ctrl: port_ctrl_mode=%d port_open=%d\n",
	       pmpriv->port_ctrl_mode, pmpriv->port_open);

	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get authentication mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_sec_ioctl_auth_mode(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		sec->param.auth_mode = pmpriv->sec_info.authentication_mode;
	else
		pmpriv->sec_info.authentication_mode = sec->param.auth_mode;

	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get encryption mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_sec_ioctl_encrypt_mode(IN pmlan_adapter pmadapter,
			    IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		sec->param.encrypt_mode = pmpriv->sec_info.encryption_mode;
	else
		pmpriv->sec_info.encryption_mode = sec->param.encrypt_mode;

	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Get Random charactor
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *
 *  @return		random charactor
 */
t_u8
wlan_get_random_charactor(pmlan_adapter pmadapter)
{
	t_u32 sec, usec;
	t_u8 ch = 0;

	ENTER();

	pmadapter->callbacks.moal_get_system_time(pmadapter->pmoal_handle, &sec,
						  &usec);
	sec = (sec & 0xFFFF) + (sec >> 16);
	usec = (usec & 0xFFFF) + (usec >> 16);
	ch = (((sec << 16) + usec) % 26) + 'a';
	LEAVE();
	return ch;
}

/**
 *  @brief Set/Get WPA status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_sec_ioctl_wpa_enable(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		if (pmpriv->wpa_ie_len)
			sec->param.wpa_enabled = MTRUE;
		else
			sec->param.wpa_enabled = MFALSE;
	} else {
		if (sec->param.wpa_enabled == MFALSE)
			wlan_set_wpa_ie_helper(pmpriv, MNULL, 0);
		/** clear adhoc aes flag, when WPA enabled */
		pmpriv->adhoc_aes_enabled = MFALSE;
		pmpriv->aes_key.key_len = 0;
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Set WEP keys
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_set_wep_key(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	mrvl_wep_key_t *pwep_key = MNULL;
	int index;
	int i = 0;

	ENTER();

	if (pmpriv->wep_key_curr_index >= MRVL_NUM_WEP_KEY)
		pmpriv->wep_key_curr_index = 0;
	pwep_key = &pmpriv->wep_key[pmpriv->wep_key_curr_index];
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (sec->param.encrypt_key.key_index == MLAN_KEY_INDEX_DEFAULT) {
		index = pmpriv->wep_key_curr_index;
		sec->param.encrypt_key.key_index = index;
	} else {
		if (sec->param.encrypt_key.key_index >= MRVL_NUM_WEP_KEY) {
			if ((sec->param.encrypt_key.key_remove == MTRUE)&&
			    (sec->param.encrypt_key.key_index <= 5)) {
				/* call firmware remove key */
				ret = wlan_prepare_cmd(pmpriv,
						       HostCmd_CMD_802_11_KEY_MATERIAL,
						       HostCmd_ACT_GEN_SET,
						       0,
						       MNULL,
						       &sec->param.encrypt_key);
				goto exit;
			}
			PRINTM(MERROR, "Key_index is invalid\n");
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
		index = sec->param.encrypt_key.key_index;
	}

	if ((sec->param.encrypt_key.key_disable == MTRUE)||
	    (sec->param.encrypt_key.key_remove == MTRUE)) {
		pmpriv->sec_info.wep_status = Wlan802_11WEPDisabled;
		/* remove key */
		if (sec->param.encrypt_key.key_remove == MTRUE) {
			memset(pmadapter, &pmpriv->wep_key[index], 0,
			       sizeof(mrvl_wep_key_t));
			/* call firmware remove key */
			ret = wlan_prepare_cmd(pmpriv,
					       HostCmd_CMD_802_11_KEY_MATERIAL,
					       HostCmd_ACT_GEN_SET,
					       0,
					       MNULL, &sec->param.encrypt_key);
			if (ret)
				goto exit;
		}
	} else {
		if (sec->param.encrypt_key.key_len) {
			if ((sec->param.encrypt_key.key_len !=
			     WEP_104_BIT_LEN)&&(sec->param.encrypt_key.
						key_len != WEP_40_BIT_LEN)) {
				PRINTM(MERROR, "Invalid wep key len=%d\n",
				       sec->param.encrypt_key.key_len);
				/* We will use random key to clear the key buffer in FW */
				if (sec->param.encrypt_key.key_len <
				    WEP_40_BIT_LEN)
					sec->param.encrypt_key.key_len =
						WEP_40_BIT_LEN;
				else
					sec->param.encrypt_key.key_len =
						WEP_104_BIT_LEN;
				for (i = 0; i < sec->param.encrypt_key.key_len;
				     i++)
					sec->param.encrypt_key.key_material[i] =
						wlan_get_random_charactor
						(pmadapter);
			}
			pwep_key = &pmpriv->wep_key[index];
			/* Cleanup */
			memset(pmadapter, pwep_key, 0, sizeof(mrvl_wep_key_t));
			/* Copy the key in the driver */

			memcpy(pmadapter, pwep_key->key_material,
			       sec->param.encrypt_key.key_material,
			       sec->param.encrypt_key.key_len);
			pwep_key->key_index = index;
			pwep_key->key_length = sec->param.encrypt_key.key_len;
			if (pmpriv->sec_info.wep_status != Wlan802_11WEPEnabled) {
				/*
				 * The status is set as Key Absent
				 * so as to make sure we display the
				 * keys when iwlist mlanX key is used
				 */
				pmpriv->sec_info.wep_status =
					Wlan802_11WEPKeyAbsent;
			}
		}
		if (sec->param.encrypt_key.is_current_wep_key == MTRUE) {
			/* Copy the required key as the current key */
			pwep_key = &pmpriv->wep_key[index];
			if (!pwep_key->key_length) {
				if (0
				    || &pmpriv->sec_info.wpa_enabled
				    || &pmpriv->sec_info.wpa2_enabled
				    || &pmpriv->sec_info.wapi_enabled) {
					ret = MLAN_STATUS_SUCCESS;
					goto exit;
				}
				PRINTM(MERROR,
				       "Key %d not set,so cannot enable it\n",
				       index);
				pioctl_req->status_code =
					MLAN_ERROR_CMD_RESP_FAIL;
				ret = MLAN_STATUS_FAILURE;
				goto exit;
			}
			pmpriv->wep_key_curr_index = (t_u16)index;
			pmpriv->sec_info.wep_status = Wlan802_11WEPEnabled;
		}
		if (sec->param.encrypt_key.key_flags && pwep_key->key_length) {
			pmpriv->wep_key_curr_index = (t_u16)index;
			// Only do this if the key is an xmit key.  If the key is a group key,
			// we might be in wpa/wep mixed mode in which case we don't want to
			// set wep_status = Wlan802_11WEPEnabled because that enables WEP
			// at the MAC controller level and WPA stops working properly.
			if (sec->param.encrypt_key.
			    key_flags & KEY_FLAG_SET_TX_KEY) {
				pmpriv->sec_info.wep_status =
					Wlan802_11WEPEnabled;
			}
		}
	}
	if (pmpriv->sec_info.wep_status == Wlan802_11WEPEnabled)
		pmpriv->curr_pkt_filter |= HostCmd_ACT_MAC_WEP_ENABLE;
	else
		pmpriv->curr_pkt_filter &= ~HostCmd_ACT_MAC_WEP_ENABLE;

	/* Send request to firmware */
	if (pmpriv->sec_info.wep_status == Wlan802_11WEPEnabled
	    && pwep_key->key_length) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_MAC_CONTROL,
				       HostCmd_ACT_GEN_SET,
				       0, MNULL, &pmpriv->curr_pkt_filter);
		if (ret)
			goto exit;
		if (!sec->param.encrypt_key.key_len) {
			sec->param.encrypt_key.key_index = pwep_key->key_index;
			sec->param.encrypt_key.key_len = pwep_key->key_length;
			memcpy(pmadapter, sec->param.encrypt_key.key_material,
			       pwep_key->key_material,
			       sec->param.encrypt_key.key_len);
		}
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11_KEY_MATERIAL,
				       HostCmd_ACT_GEN_SET,
				       0,
				       (t_void *)pioctl_req,
				       &sec->param.encrypt_key);
	} else {
		if (pwep_key->key_length) {
			if (!sec->param.encrypt_key.key_len) {
				sec->param.encrypt_key.key_index =
					pwep_key->key_index;
				sec->param.encrypt_key.key_len =
					pwep_key->key_length;
				memcpy(pmadapter,
				       sec->param.encrypt_key.key_material,
				       pwep_key->key_material,
				       sec->param.encrypt_key.key_len);
			}
			ret = wlan_prepare_cmd(pmpriv,
					       HostCmd_CMD_802_11_KEY_MATERIAL,
					       HostCmd_ACT_GEN_SET,
					       0,
					       MNULL, &sec->param.encrypt_key);
			if (ret)
				goto exit;
		}
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_MAC_CONTROL,
				       HostCmd_ACT_GEN_SET,
				       0,
				       (t_void *)pioctl_req,
				       &pmpriv->curr_pkt_filter);
	}
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Set WPA key
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_set_wpa_key(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;

	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	/* Current driver only supports key length of up to 32 bytes */
	if (sec->param.encrypt_key.key_len > MLAN_MAX_KEY_LENGTH) {
		PRINTM(MERROR, "Key length is incorrect\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_FAILURE;
		goto exit;
	}

	if (sec->param.encrypt_key.key_len == WPA_AES_KEY_LEN) {
	/** back up adhoc AES key */
		memset(pmpriv->adapter, &pmpriv->aes_key, 0,
		       sizeof(pmpriv->aes_key));
		memcpy(pmpriv->adapter, (t_u8 *)&pmpriv->aes_key,
		       (t_u8 *)&sec->param.encrypt_key,
		       sizeof(pmpriv->aes_key));
	}

    /** only adhoc aes key_index = MLAN_KEY_INDEX_UNICAST */
	if (pmpriv->bss_mode == MLAN_BSS_MODE_IBSS &&
	    sec->param.encrypt_key.key_len == WPA_AES_KEY_LEN
	    && sec->param.encrypt_key.key_index & MLAN_KEY_INDEX_UNICAST) {
		t_u8 zero_key_material[WPA_AES_KEY_LEN];
		memset(pmadapter, zero_key_material, 0,
		       sizeof(zero_key_material));
		if (memcmp
		    (pmadapter, sec->param.encrypt_key.key_material,
		     zero_key_material, WPA_AES_KEY_LEN)) {
			PRINTM(MINFO, "Adhoc AES Enabled.\n");
			pmpriv->adhoc_aes_enabled = MTRUE;
		} else {
			PRINTM(MINFO, "Adhoc AES Disabled.\n");
			pmpriv->adhoc_aes_enabled = MFALSE;
	    /** clear adhoc AES key */
			pmpriv->aes_key.key_len = 0;
		}
	}

	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_KEY_MATERIAL,
			       HostCmd_ACT_GEN_SET,
			       0,
			       (t_void *)pioctl_req, &sec->param.encrypt_key);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Get security keys
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_get_key(IN pmlan_adapter pmadapter,
		       IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	int index;
	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;

	if ((sec->param.encrypt_key.key_index == MLAN_KEY_INDEX_UNICAST)&&
	    (sec->param.encrypt_key.key_len == WPA_AES_KEY_LEN)) {
		if (pmpriv->adhoc_aes_enabled == MTRUE &&
		    (pmpriv->aes_key.key_len == WPA_AES_KEY_LEN)) {
			HEXDUMP("Get ADHOCAES Key",
				pmpriv->aes_key.key_material, WPA_AES_KEY_LEN);
			memcpy(pmadapter, sec->param.encrypt_key.key_material,
			       pmpriv->aes_key.key_material, WPA_AES_KEY_LEN);
			LEAVE();
			return ret;
		} else {
			PRINTM(MERROR, " ADHOCAES key is not set yet!\n");
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			LEAVE();
			return ret;
		}
	}
	if (pmpriv->wep_key_curr_index >= MRVL_NUM_WEP_KEY)
		pmpriv->wep_key_curr_index = 0;

	if ((pmpriv->sec_info.wep_status == Wlan802_11WEPEnabled)
	    || (pmpriv->sec_info.wep_status == Wlan802_11WEPKeyAbsent)
	    || pmpriv->sec_info.ewpa_enabled
	    || pmpriv->sec_info.wpa_enabled
	    || pmpriv->sec_info.wpa2_enabled || pmpriv->adhoc_aes_enabled) {
		sec->param.encrypt_key.key_disable = MFALSE;
	} else {
		sec->param.encrypt_key.key_disable = MTRUE;
	}
	if (sec->param.encrypt_key.key_index == MLAN_KEY_INDEX_DEFAULT) {
		if ((pmpriv->wep_key[pmpriv->wep_key_curr_index].key_length)&&
		    (pmpriv->sec_info.wep_status == Wlan802_11WEPEnabled)) {
			index = pmpriv->wep_key_curr_index;
			sec->param.encrypt_key.key_index =
				pmpriv->wep_key[index].key_index;
			memcpy(pmadapter, sec->param.encrypt_key.key_material,
			       pmpriv->wep_key[index].key_material,
			       MIN(MLAN_MAX_KEY_LENGTH,
				   pmpriv->wep_key[index].key_length));
			sec->param.encrypt_key.key_len =
				MIN(MLAN_MAX_KEY_LENGTH,
				    pmpriv->wep_key[index].key_length);
		} else if ((pmpriv->sec_info.wpa_enabled)
			   || (pmpriv->sec_info.ewpa_enabled)
			   || (pmpriv->sec_info.wpa2_enabled)
			   || (pmpriv->sec_info.wapi_enabled)
			   || (pmpriv->adhoc_aes_enabled)
			) {
			/* Return WPA enabled */
			sec->param.encrypt_key.key_disable = MFALSE;

			memcpy(pmadapter, sec->param.encrypt_key.key_material,
			       pmpriv->aes_key.key_material,
			       MIN(MLAN_MAX_KEY_LENGTH,
				   pmpriv->aes_key.key_len));
			sec->param.encrypt_key.key_len =
				MIN(MLAN_MAX_KEY_LENGTH,
				    pmpriv->aes_key.key_len);
		} else {
			sec->param.encrypt_key.key_disable = MTRUE;
		}
	} else {
		index = sec->param.encrypt_key.key_index;
		if (pmpriv->wep_key[index].key_length) {
			sec->param.encrypt_key.key_index =
				pmpriv->wep_key[index].key_index;
			memcpy(pmadapter, sec->param.encrypt_key.key_material,
			       pmpriv->wep_key[index].key_material,
			       MIN(MLAN_MAX_KEY_LENGTH,
				   pmpriv->wep_key[index].key_length));
			sec->param.encrypt_key.key_len =
				MIN(MLAN_MAX_KEY_LENGTH,
				    pmpriv->wep_key[index].key_length);
		} else if ((pmpriv->sec_info.wpa_enabled)
			   || (pmpriv->sec_info.ewpa_enabled)
			   || (pmpriv->sec_info.wpa2_enabled)
			   || (pmpriv->sec_info.wapi_enabled)
			   || (pmpriv->adhoc_aes_enabled)
			) {
			/* Return WPA enabled */
			sec->param.encrypt_key.key_disable = MFALSE;
		} else {
			sec->param.encrypt_key.key_disable = MTRUE;
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Set security key(s)
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_encrypt_key(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		if (sec->param.encrypt_key.is_wapi_key)
			status = wlan_sec_ioctl_set_wapi_key(pmadapter,
							     pioctl_req);
		else if (sec->param.encrypt_key.key_len > MAX_WEP_KEY_SIZE)
			status = wlan_sec_ioctl_set_wpa_key(pmadapter,
							    pioctl_req);
		else
			status = wlan_sec_ioctl_set_wep_key(pmadapter,
							    pioctl_req);
	} else {
		status = wlan_sec_ioctl_get_key(pmadapter, pioctl_req);
	}
	LEAVE();
	return status;
}

/**
 *  @brief Set/Get esupplicant status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_sec_ioctl_ewpa_enable(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_sec_cfg *sec = MNULL;
	ENTER();
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET) {
		sec->param.ewpa_enabled = pmpriv->sec_info.ewpa_enabled;
	} else {
		pmpriv->sec_info.ewpa_enabled = (t_u8)sec->param.ewpa_enabled;
		PRINTM(MINFO, "Set: ewpa_enabled = %d\n",
		       (int)pmpriv->sec_info.ewpa_enabled);
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Get esupplicant mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_sec_ioctl_esupp_mode(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	t_u16 cmd_action = 0;
	mlan_ds_sec_cfg *sec = MNULL;

	ENTER();

	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		cmd_action = HostCmd_ACT_GEN_SET;
		if (pmpriv->media_connected == MTRUE) {
			PRINTM(MERROR,
			       "Cannot set esupplicant mode configuration while connected.\n");
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
		if (!sec->param.esupp_mode.rsn_mode ||
		    (sec->param.esupp_mode.rsn_mode & RSN_TYPE_VALID_BITS)
		    != sec->param.esupp_mode.rsn_mode) {
			PRINTM(MERROR, "Invalid RSN mode\n");
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
		if (!sec->param.esupp_mode.act_paircipher ||
		    (sec->param.esupp_mode.
		     act_paircipher & EMBED_CIPHER_VALID_BITS)
		    != sec->param.esupp_mode.act_paircipher) {
			PRINTM(MERROR, "Invalid pairwise cipher\n");
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
		if (!sec->param.esupp_mode.act_groupcipher ||
		    (sec->param.esupp_mode.
		     act_groupcipher & EMBED_CIPHER_VALID_BITS)
		    != sec->param.esupp_mode.act_groupcipher) {
			PRINTM(MERROR, "Invalid group cipher\n");
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}
	} else {
		cmd_action = HostCmd_ACT_GEN_GET_CURRENT;
	}

	/* Send request to firmware */
	if (sec) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_SUPPLICANT_PROFILE,
				       cmd_action,
				       0,
				       (t_void *)pioctl_req,
				       &sec->param.esupp_mode);
	} else {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_SUPPLICANT_PROFILE,
				       cmd_action,
				       0, (t_void *)pioctl_req, MNULL);
	}
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;
}

/**
 *  @brief Security configuration handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_sec_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_sec_cfg *sec = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_sec_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_sec_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	sec = (mlan_ds_sec_cfg *)pioctl_req->pbuf;
	switch (sec->sub_command) {
	case MLAN_OID_SEC_CFG_AUTH_MODE:
		status = wlan_sec_ioctl_auth_mode(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_ENCRYPT_MODE:
		status = wlan_sec_ioctl_encrypt_mode(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_WPA_ENABLED:
		status = wlan_sec_ioctl_wpa_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_WAPI_ENABLED:
		status = wlan_sec_ioctl_wapi_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_PORT_CTRL_ENABLED:
		status = wlan_sec_ioctl_port_ctrl_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_ENCRYPT_KEY:
		status = wlan_sec_ioctl_encrypt_key(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_PASSPHRASE:
		status = wlan_sec_ioctl_passphrase(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_EWPA_ENABLED:
		status = wlan_sec_ioctl_ewpa_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_SEC_CFG_ESUPP_MODE:
		status = wlan_sec_ioctl_esupp_mode(pmadapter, pioctl_req);
		break;

	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief  Append/Reset IE buffer.
 *
 *  Pass an opaque block of data, expected to be IEEE IEs, to the driver
 *    for eventual passthrough to the firmware in an associate/join
 *    (and potentially start) command.  This function is the main body
 *    for both wlan_set_gen_ie_ioctl and wlan_set_gen_ie
 *
 *  Data is appended to an existing buffer and then wrapped in a passthrough
 *    TLV in the command API to the firmware.  The firmware treats the data
 *    as a transparent passthrough to the transmitted management frame.
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param ie_data_ptr  A pointer to iwreq structure
 *  @param ie_len       Length of the IE or IE block passed in ie_data_ptr
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static int
wlan_set_gen_ie_helper(mlan_private *priv, t_u8 *ie_data_ptr, t_u16 ie_len)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	IEEEtypes_VendorHeader_t *pvendor_ie;
	const t_u8 wpa_oui[] = { 0x00, 0x50, 0xf2, 0x01 };
	const t_u8 wps_oui[] = { 0x00, 0x50, 0xf2, 0x04 };

	ENTER();

	/* If the passed length is zero, reset the buffer */
	if (!ie_len) {
		priv->gen_ie_buf_len = 0;
		priv->wps.session_enable = MFALSE;
		wlan_set_wpa_ie_helper(priv, MNULL, 0);
		wlan_set_wapi_ie(priv, MNULL, 0);
	} else if (!ie_data_ptr) {
		/* MNULL check */
		ret = MLAN_STATUS_FAILURE;
	} else {

		pvendor_ie = (IEEEtypes_VendorHeader_t *)ie_data_ptr;
		if (pvendor_ie->element_id == EXT_CAPABILITY) {
			t_u8 len = sizeof(priv->ext_cap);
			if (len > pvendor_ie->len)
				len = pvendor_ie->len;
			memcpy(priv->adapter, &priv->ext_cap, &ie_data_ptr[2],
			       len);
		} else
			/* Test to see if it is a WPA IE, if not, then it is a gen IE */
			if (((pvendor_ie->element_id == WPA_IE)
			     &&
			     (!memcmp
			      (priv->adapter, pvendor_ie->oui, wpa_oui,
			       sizeof(wpa_oui))))
			    || (pvendor_ie->element_id == RSN_IE)
			) {

			/* IE is a WPA/WPA2 IE so call set_wpa function */
			ret = wlan_set_wpa_ie_helper(priv, ie_data_ptr, ie_len);
			priv->wps.session_enable = MFALSE;
		} else if (pvendor_ie->element_id == WAPI_IE) {
			/* IE is a WAPI IE so call set_wapi function */
			ret = wlan_set_wapi_ie(priv, ie_data_ptr, ie_len);
		} else if ((pvendor_ie->element_id == WPS_IE) &&
			   (priv->wps.session_enable == MFALSE) &&
			   (!memcmp
			    (priv->adapter, pvendor_ie->oui, wps_oui,
			     sizeof(wps_oui)))) {
			/*
			 * Discard first two byte (Element ID and Length)
			 * because they are not needed in the case of setting
			 * WPS_IE
			 */
			if (pvendor_ie->len > 4) {
				memcpy(priv->adapter, (t_u8 *)&priv->wps.wps_ie,
				       ie_data_ptr, ie_len);
				HEXDUMP("wps_ie", (t_u8 *)&priv->wps.wps_ie,
					priv->wps.wps_ie.vend_hdr.len + 2);
			} else {
				/* Only wps oui exist, reset driver wps buffer */
				memset(priv->adapter, (t_u8 *)&priv->wps.wps_ie,
				       0x00, sizeof(priv->wps.wps_ie));
				PRINTM(MINFO, "wps_ie cleared\n");
			}
		} else {
			/*
			 * Verify that the passed length is not larger than
			 * the available space remaining in the buffer
			 */
			if (ie_len <
			    (sizeof(priv->gen_ie_buf) - priv->gen_ie_buf_len)) {

				/* Test to see if it is a WPS IE, if so, enable wps session flag */
				pvendor_ie =
					(IEEEtypes_VendorHeader_t *)ie_data_ptr;
				if ((pvendor_ie->element_id == WPS_IE)
				    &&
				    (!memcmp
				     (priv->adapter, pvendor_ie->oui, wps_oui,
				      sizeof(wps_oui)))) {
					priv->wps.session_enable = MTRUE;
					PRINTM(MINFO, "WPS Session Enabled.\n");
				}

				/* Append the passed data to the end of
				 * the genIeBuffer */
				memcpy(priv->adapter,
				       priv->gen_ie_buf + priv->gen_ie_buf_len,
				       ie_data_ptr, ie_len);
				/* Increment the stored buffer length by
				 * the size passed */
				priv->gen_ie_buf_len += ie_len;
			} else {
				/* Passed data does not fit in the
				 * remaining buffer space */
				ret = MLAN_STATUS_FAILURE;
			}
		}
	}

	/* Return MLAN_STATUS_SUCCESS, or MLAN_STATUS_FAILURE for error case */
	LEAVE();
	return ret;
}

/**
 *  @brief Set/Get WWS mode
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_misc_ioctl_wws_cfg(IN pmlan_adapter pmadapter,
			IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd_action = 0;
	t_u32 enable = 0;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_misc_cfg)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_misc_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	enable = misc_cfg->param.wws_cfg;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       cmd_action,
			       WwsMode_i, (t_void *)pioctl_req, &enable);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

exit:
	LEAVE();
	return ret;

}

/**
 *  @brief Set/Get 11D status
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_11d_cfg_ioctl_enable(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11d_cfg *pcfg_11d = MNULL;

	ENTER();

	pcfg_11d = (mlan_ds_11d_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET) {
		if (pmpriv->media_connected == MTRUE) {
			PRINTM(MIOCTL,
			       "11D setting cannot be changed while interface is active.\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		PRINTM(MINFO, "11D: 11dcfg SET=%d\n",
		       pcfg_11d->param.enable_11d);

		/* Compare with current settings */
		if (pmpriv->state_11d.user_enable_11d !=
		    pcfg_11d->param.enable_11d) {
			ret = wlan_11d_enable(pmpriv, pioctl_req,
					      (state_11d_t)pcfg_11d->param.
					      enable_11d);
			if (ret == MLAN_STATUS_SUCCESS)
				ret = MLAN_STATUS_PENDING;
		} else {
			PRINTM(MINFO,
			       "11D: same as current setting, do nothing\n");
		}
	} else {
		pcfg_11d->param.enable_11d =
			(t_u32)pmpriv->state_11d.user_enable_11d;
		pioctl_req->data_read_written =
			sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
		PRINTM(MINFO, "11D: 11dcfg GET=%d\n",
		       pcfg_11d->param.enable_11d);
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Clear 11D chan table
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_11d_clr_chan_table(IN pmlan_adapter pmadapter,
			IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	if (pioctl_req->action == MLAN_ACT_SET) {
		PRINTM(MINFO, "11D: 11dclrtbl SET\n");

		if (wlan_11d_clear_parsedtable(pmpriv) == MLAN_STATUS_SUCCESS)
			PRINTM(MINFO,
			       "11D: cleared parsed_region_chan (now no_of_chan=%d)\n",
			       pmadapter->parsed_region_chan.no_of_chan);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief 11D configuration handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_11d_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_11d_cfg *pcfg_11d = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11d_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11d_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		status = MLAN_STATUS_RESOURCE;
		goto exit;
	}

	pcfg_11d = (mlan_ds_11d_cfg *)pioctl_req->pbuf;
	switch (pcfg_11d->sub_command) {
	case MLAN_OID_11D_CFG_ENABLE:
		status = wlan_11d_cfg_ioctl_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11D_CLR_CHAN_TABLE:
		status = wlan_11d_clr_chan_table(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11D_DOMAIN_INFO:
		status = wlan_11d_cfg_domain_info(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

exit:
	LEAVE();
	return status;
}

/**
 *  @brief WPS configuration handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_wps_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_wps_cfg *pwps = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_wps_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_wps_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	pwps = (mlan_ds_wps_cfg *)pioctl_req->pbuf;
	switch (pwps->sub_command) {
	case MLAN_OID_WPS_CFG_SESSION:
		if (pioctl_req->action == MLAN_ACT_SET) {
			if (pwps->param.wps_session ==
			    MLAN_WPS_CFG_SESSION_START)
				pmpriv->wps.session_enable = MTRUE;
			else
				pmpriv->wps.session_enable = MFALSE;
		} else {
			pwps->param.wps_session =
				(t_u32)pmpriv->wps.session_enable;
			pioctl_req->data_read_written = sizeof(t_u32);
			PRINTM(MINFO, "wpscfg GET=%d\n",
			       pwps->param.wps_session);
		}
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief register memory access handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_reg_mem_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_reg_mem *reg_mem = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_reg_mem)) {
		PRINTM(MWARN, "MLAN REG_MEM IOCTL length is too short\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_reg_mem);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	reg_mem = (mlan_ds_reg_mem *)pioctl_req->pbuf;
	switch (reg_mem->sub_command) {
	case MLAN_OID_REG_RW:
		status = wlan_reg_mem_ioctl_reg_rw(pmadapter, pioctl_req);
		break;
	case MLAN_OID_EEPROM_RD:
		status = wlan_reg_mem_ioctl_read_eeprom(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MEM_RW:
		status = wlan_reg_mem_ioctl_mem_rw(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief 802.11h ad-hoc start channel check
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_11h_channel_check_req(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = MNULL;
	mlan_status ret = MLAN_STATUS_FAILURE;
	t_u8 chan_width = CHAN_BW_20MHZ;
	Band_Config_t bandcfg;

	ENTER();

	if (pioctl_req != MNULL) {
		pmpriv = pmadapter->priv[pioctl_req->bss_index];
	} else {
		PRINTM(MERROR, "MLAN IOCTL information is not present\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	memset(pmadapter, &bandcfg, 0, sizeof(Band_Config_t));
	pmpriv->adhoc_state = ADHOC_STARTING;

	if ((pmadapter->adhoc_start_band & BAND_A)
	    || (pmadapter->adhoc_start_band & BAND_AN)
		) {
		if (pmpriv->intf_state_11h.adhoc_auto_sel_chan)
			pmpriv->adhoc_channel =
				wlan_11h_get_adhoc_start_channel(pmpriv);

		/*
		 * Check if the region and channel requires a channel availability
		 * check.
		 */
		if (wlan_11h_radar_detect_required
		    (pmpriv, pmpriv->adhoc_channel)
		    && !wlan_11h_is_channel_under_nop(pmadapter,
						      pmpriv->adhoc_channel)
			) {
			/*
			 * Radar detection is required for this channel, make sure
			 * 11h is activated in the firmware
			 */
			ret = wlan_11h_activate(pmpriv, MNULL, MTRUE);
			ret = wlan_11h_config_master_radar_det(pmpriv, MTRUE);
			ret = wlan_11h_check_update_radar_det_state(pmpriv);

			/* Check for radar on the channel */
			if ((pmadapter->chan_bandwidth ==
			     CHANNEL_BW_40MHZ_ABOVE) ||
			    (pmadapter->chan_bandwidth ==
			     CHANNEL_BW_40MHZ_BELOW)) {
				chan_width = CHAN_BW_40MHZ;
				if (pmadapter->chanrpt_param_bandcfg) {
					bandcfg.chan2Offset =
						pmadapter->chan_bandwidth;
				}
			}
			if (pmadapter->chanrpt_param_bandcfg) {
				bandcfg.chanWidth = chan_width;
				bandcfg.chanBand = BAND_5GHZ;
			} else {
				*((t_u8 *)&bandcfg) = chan_width;
			}

			ret = wlan_11h_issue_radar_detect(pmpriv, pioctl_req,
							  pmpriv->adhoc_channel,
							  bandcfg);
			if (ret == MLAN_STATUS_SUCCESS)
				ret = MLAN_STATUS_PENDING;
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief 802.11h set/get local power constraint
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_11h_ioctl_local_power_constraint(IN pmlan_adapter pmadapter,
				      IN pmlan_ioctl_req pioctl_req)
{
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;
	t_s8 *plocalpower = &pmadapter->state_11h.usr_def_power_constraint;

	ENTER();

	ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_GET)
		ds_11hcfg->param.usr_local_power_constraint = *plocalpower;
	else
		*plocalpower = ds_11hcfg->param.usr_local_power_constraint;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief 11h configuration handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_11h_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_11h_cfg *ds_11hcfg = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11h_cfg)) {
		PRINTM(MWARN, "MLAN 11H IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11h_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	ds_11hcfg = (mlan_ds_11h_cfg *)pioctl_req->pbuf;

	switch (ds_11hcfg->sub_command) {
	case MLAN_OID_11H_CHANNEL_CHECK:
		status = wlan_11h_channel_check_req(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11H_LOCAL_POWER_CONSTRAINT:
		status = wlan_11h_ioctl_local_power_constraint(pmadapter,
							       pioctl_req);
		break;
#if defined(DFS_TESTING_SUPPORT)
	case MLAN_OID_11H_DFS_TESTING:
		status = wlan_11h_ioctl_dfs_testing(pmadapter, pioctl_req);
		break;
#endif
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief 11k enable
 *
 *  @param pmpriv           A pointer to mlan_private structure
 *  @param pioctl_buf 	A pointer to ioctl request buffer
 *  @param enable_11k   11k enable flag.
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_11k_enable(mlan_private *pmpriv, t_void *pioctl_buf, t_u8 enable_11k)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_OFFLOAD_FEATURE_CTRL fctrl;

	ENTER();

	/* Send cmd HostCmd_CMD_OFFLOAD_FEATURE_CONTROL to FW to enable/disable 11K function */
	/* need to active 11h first  when enable 11k */
	if (!wlan_11h_is_active(pmpriv)) {
		ret = wlan_11h_activate(pmpriv, MNULL, MTRUE);
		if (ret != MLAN_STATUS_SUCCESS) {
			PRINTM(MERROR, "11h enable failed \n");
			goto done;
		}
	}
	memset(pmpriv->adapter, &fctrl, 0, sizeof(fctrl));
	fctrl.featureSelect = 0;	/* Std feature */

	/* enable or disable Neighbor AP list Report */
	fctrl.control.std.dot11k_nbor_support = enable_11k;
	/* enable or disable Traffic Stream Measurement */
	fctrl.control.std.dot11k_tsm = enable_11k;
	/* enable or disable Link Measurement */
	fctrl.control.std.dot11k_lm = enable_11k;
	/* enable or disable Radio Measurement (Beacon Report) */
	fctrl.control.std.dot11k_rm = enable_11k;

	/* enable 11v BSS Transition with 11k */
	fctrl.control.std.dot11v_bss_trans = enable_11k;

	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_OFFLOAD_FEATURE_CONTROL,
			       HostCmd_ACT_GEN_SET,
			       0, (t_void *)pioctl_buf, &fctrl);

	if (ret) {
		PRINTM(MERROR, "11K: Failed to %s 11K\n",
		       (enable_11k) ? "enable" : "disable");
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief 11k enable ioctl
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_11k_cfg_ioctl_enable(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_11k_cfg *pcfg_11k = MNULL;

	ENTER();

	pcfg_11k = (mlan_ds_11k_cfg *) pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET) {
		if (pmpriv->media_connected == MTRUE
		    && pcfg_11k->param.enable_11k != pmpriv->enable_11k) {
			PRINTM(MERROR,
			       "11K setting cannot be changed while connecting with AP.\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}

		PRINTM(MINFO, "11K: 11kcfg SET=%d\n",
		       pcfg_11k->param.enable_11k);

		/* Compare with current settings */
		if (pmpriv->enable_11k != pcfg_11k->param.enable_11k) {
			ret = wlan_11k_enable(pmpriv, pioctl_req,
					      pcfg_11k->param.enable_11k);
			if (ret == MLAN_STATUS_SUCCESS)
				ret = MLAN_STATUS_PENDING;
		} else {
			PRINTM(MINFO,
			       "11K: same as current setting, do nothing\n");
		}
	} else {
		pcfg_11k->param.enable_11k = pmpriv->enable_11k;
		pioctl_req->data_read_written =
			sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;
		PRINTM(MINFO, "11K: 11kcfg GET=%d\n",
		       pcfg_11k->param.enable_11k);
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief 11k get nlist
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_11k_get_nlist(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	if (pioctl_req->action == MLAN_ACT_GET && pmpriv->media_connected) {
		ret = wlan_prepare_cmd(pmpriv,
				       HostCmd_CMD_802_11K_GET_NLIST,
				       HostCmd_ACT_GEN_GET,
				       0, pioctl_req, MNULL);
		if (ret) {
			PRINTM(MERROR, "11K: prepare get_nlist failed\n");
			ret = MLAN_STATUS_FAILURE;
		} else {
			/* start excute command */
			if (ret == MLAN_STATUS_SUCCESS)
				ret = MLAN_STATUS_PENDING;
		}
	} else {
		PRINTM(MERROR, " 11k get_nlist only support GET oper\n");
		ret = MLAN_STATUS_FAILURE;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief 11K cfg ioctl
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_11k_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_11k_cfg *ds_11kcfg = MNULL;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_11k_cfg)) {
		PRINTM(MWARN, "MLAN 11K IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_11k_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	ds_11kcfg = (mlan_ds_11k_cfg *) pioctl_req->pbuf;
	switch (ds_11kcfg->sub_command) {
	case MLAN_OID_11K_CFG_ENABLE:
		status = wlan_11k_cfg_ioctl_enable(pmadapter, pioctl_req);
		break;
	case MLAN_OID_11K_GET_NLIST:
		status = wlan_11k_get_nlist(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}

	LEAVE();
	return status;
}

/**
 *  @brief Set/Get generic IE
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_misc_ioctl_gen_ie(IN pmlan_adapter pmadapter,
		       IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	switch (misc->param.gen_ie.type) {
	case MLAN_IE_TYPE_GEN_IE:
		if (pioctl_req->action == MLAN_ACT_GET) {
			misc->param.gen_ie.len = pmpriv->wpa_ie_len;
			memcpy(pmadapter, misc->param.gen_ie.ie_data,
			       pmpriv->wpa_ie, misc->param.gen_ie.len);
		} else {
			wlan_set_gen_ie_helper(pmpriv,
					       misc->param.gen_ie.ie_data,
					       (t_u16)misc->param.gen_ie.len);
		}
		break;
	case MLAN_IE_TYPE_ARP_FILTER:
		memset(pmadapter, pmadapter->arp_filter, 0,
		       sizeof(pmadapter->arp_filter));
		if (misc->param.gen_ie.len > ARP_FILTER_MAX_BUF_SIZE) {
			pmadapter->arp_filter_size = 0;
			PRINTM(MERROR, "Invalid ARP Filter Size\n");
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
		} else if (misc->param.gen_ie.len <=
			   sizeof(MrvlIEtypesHeader_t)) {
			pmadapter->arp_filter_size = 0;
			PRINTM(MINFO, "Clear ARP filter\n");
		} else {
			memcpy(pmadapter, pmadapter->arp_filter,
			       misc->param.gen_ie.ie_data,
			       misc->param.gen_ie.len);
			pmadapter->arp_filter_size = misc->param.gen_ie.len;
			HEXDUMP("ArpFilter", pmadapter->arp_filter,
				pmadapter->arp_filter_size);
		}
		break;
	default:
		PRINTM(MERROR, "Invalid IE type\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_FAILURE;
	}
	pioctl_req->data_read_written =
		sizeof(mlan_ds_misc_gen_ie) + MLAN_SUB_COMMAND_SIZE;
	LEAVE();
	return ret;
}

/**
 *  @brief Perform warm reset
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_ioctl_warm_reset(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	pmlan_buffer pmbuf;
	t_s32 i = 0;
	t_u16 mc_policy = pmadapter->mc_policy;
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	ENTER();
	mlan_block_rx_process(pmadapter, MTRUE);
	/* Cancel all pending commands and complete ioctls */
	if (misc->param.fw_reload)
		wlan_cancel_all_pending_cmd(pmadapter);
    /** Init all the head nodes and free all the locks here */
	for (i = 0; i < pmadapter->priv_num; i++)
		wlan_free_priv(pmadapter->priv[i]);

	while ((pmbuf = (pmlan_buffer)util_dequeue_list(pmadapter->pmoal_handle,
							&pmadapter->
							rx_data_queue,
							pcb->moal_spin_lock,
							pcb->
							moal_spin_unlock))) {
		wlan_free_mlan_buffer(pmadapter, pmbuf);
	}
	pmadapter->rx_pkts_queued = 0;

	/* Initialize adapter structure */
	wlan_init_adapter(pmadapter);
	pmadapter->hw_status = WlanHardwareStatusInitializing;

	/* Initialize private structures */
	for (i = 0; i < pmadapter->priv_num; i++) {
		if (pmadapter->priv[i])
			wlan_init_priv(pmadapter->priv[i]);
	}
	mlan_block_rx_process(pmadapter, MFALSE);
	if (misc->param.fw_reload != MTRUE) {
		/* Restart the firmware */
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_FUNC_SHUTDOWN,
				       HostCmd_ACT_GEN_SET, 0, MNULL, MNULL);
		if (ret)
			goto done;
	}

	/* Issue firmware initialize commands for first BSS,
	 * for other interfaces it will be called after getting
	 * the last init command response of previous interface
	 */
	pmpriv = wlan_get_priv(pmadapter, MLAN_BSS_ROLE_ANY);
	if (!pmpriv) {
		ret = MLAN_STATUS_FAILURE;
		LEAVE();
		return ret;
	}
	ret = wlan_adapter_get_hw_spec(pmpriv->adapter);
	if (ret == MLAN_STATUS_FAILURE) {
		LEAVE();
		return ret;
	}
	ret = pmpriv->ops.init_cmd(pmpriv, MTRUE);
	if (ret == MLAN_STATUS_FAILURE) {
		LEAVE();
		return ret;
	}
	ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_MULTI_CHAN_POLICY,
			       HostCmd_ACT_GEN_SET, 0, MNULL, &mc_policy);
	if (ret == MLAN_STATUS_FAILURE) {
		LEAVE();
		return ret;
	}
	if (ret == MLAN_STATUS_PENDING)
		pmadapter->pwarm_reset_ioctl_req = pioctl_req;

done:
	LEAVE();
	return ret;
}

#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
/**
 *  @brief Reconfigure SDIO multiport aggregation parameters
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
static mlan_status
wlan_misc_ioctl_sdio_mpa_ctrl(IN pmlan_adapter pmadapter,
			      IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_ds_misc_sdio_mpa_ctrl *mpa_ctrl = MNULL;

	ENTER();

	mpa_ctrl = &misc->param.mpa_ctrl;

	if (pioctl_req->action == MLAN_ACT_SET) {

		if (pmpriv->media_connected == MTRUE) {
			PRINTM(MMSG,
			       "SDIO MPA CTRL: not allowed in connected state\n");
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}

		if (mpa_ctrl->tx_enable > 1) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}

		if (mpa_ctrl->rx_enable > 1) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}

		if (mpa_ctrl->tx_max_ports > SDIO_MP_AGGR_DEF_PKT_LIMIT) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}

		if (mpa_ctrl->rx_max_ports > SDIO_MP_AGGR_DEF_PKT_LIMIT) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto exit;
		}

		if (mpa_ctrl->tx_buf_size || mpa_ctrl->rx_buf_size) {

			wlan_free_sdio_mpa_buffers(pmadapter);

			if (mpa_ctrl->tx_buf_size > 0)
				pmadapter->mpa_tx.buf_size =
					mpa_ctrl->tx_buf_size;

			if (mpa_ctrl->rx_buf_size > 0)
				pmadapter->mpa_rx.buf_size =
					mpa_ctrl->rx_buf_size;

			if (wlan_alloc_sdio_mpa_buffers(pmadapter,
							pmadapter->mpa_tx.
							buf_size,
							pmadapter->mpa_rx.
							buf_size) !=
			    MLAN_STATUS_SUCCESS) {
				PRINTM(MERROR,
				       "Failed to allocate sdio mp-a buffers\n");
				pioctl_req->status_code = MLAN_ERROR_NO_MEM;
				ret = MLAN_STATUS_FAILURE;
				goto exit;
			}
		}

		if (mpa_ctrl->tx_max_ports > 0)
			pmadapter->mpa_tx.pkt_aggr_limit =
				mpa_ctrl->tx_max_ports;
		if (mpa_ctrl->rx_max_ports > 0)
			pmadapter->mpa_rx.pkt_aggr_limit =
				mpa_ctrl->rx_max_ports;

		pmadapter->mpa_tx.enabled = (t_u8)mpa_ctrl->tx_enable;
		pmadapter->mpa_rx.enabled = (t_u8)mpa_ctrl->rx_enable;

	} else {
		mpa_ctrl->tx_enable = (t_u16)pmadapter->mpa_tx.enabled;
		mpa_ctrl->rx_enable = (t_u16)pmadapter->mpa_rx.enabled;
		mpa_ctrl->tx_buf_size = (t_u16)pmadapter->mpa_tx.buf_size;
		mpa_ctrl->rx_buf_size = (t_u16)pmadapter->mpa_rx.buf_size;
		mpa_ctrl->tx_max_ports =
			(t_u16)pmadapter->mpa_tx.pkt_aggr_limit;
		mpa_ctrl->rx_max_ports =
			(t_u16)pmadapter->mpa_rx.pkt_aggr_limit;
	}

exit:
	LEAVE();
	return ret;
}
#endif /* SDIO_MULTI_PORT_TX_AGGR || SDIO_MULTI_PORT_RX_AGGR */

/**
 *  @brief Set/Get system clock configuration
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_misc_ioctl_sysclock(IN pmlan_adapter pmadapter,
			 IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u16 cmd_action = 0;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else
		cmd_action = HostCmd_ACT_GEN_SET;

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_ECL_SYSTEM_CLOCK_CONFIG,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req,
			       (t_void *)&misc->param.sys_clock);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get the associate response
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_misc_ioctl_get_assoc_rsp(IN pmlan_adapter pmadapter,
			      IN pmlan_ioctl_req pioctl_req)
{
	mlan_ds_misc_cfg *misc = MNULL;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if ((pioctl_req->action == MLAN_ACT_GET) && pmpriv->assoc_rsp_size) {
		memcpy(pmadapter, misc->param.assoc_resp.assoc_resp_buf,
		       pmpriv->assoc_rsp_buf, MIN(ASSOC_RSP_BUF_SIZE,
						  pmpriv->assoc_rsp_size));
		misc->param.assoc_resp.assoc_resp_len =
			MIN(ASSOC_RSP_BUF_SIZE, pmpriv->assoc_rsp_size);

	}

	LEAVE();
	return ret;
}

/**
 *  @brief Send function softreset command to firmware
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_misc_ioctl_soft_reset(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();
	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_SOFT_RESET,
			       HostCmd_ACT_GEN_SET,
			       0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;
	LEAVE();
	return ret;
}

/**
 *  @brief Get the thermal reading
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING -- success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_thermal(IN pmlan_adapter pmadapter,
			IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;

	ENTER();

	if (pioctl_req->buf_len < sizeof(mlan_ds_misc_cfg)) {
		PRINTM(MWARN,
		       "MLAN IOCTL information buffer length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_misc_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET) {
		PRINTM(MERROR, "Thermal reading setting is not allowed!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	} else {
		cmd_action = HostCmd_ACT_GEN_GET;
	}

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_SNMP_MIB,
			       cmd_action,
			       Thermal_i, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Get/Set subscribe event
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING -- success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_subscribe_evt(IN pmlan_adapter pmadapter,
			      IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_802_11_SUBSCRIBE_EVENT,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req,
			       &misc->param.subscribe_event);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Set ARP filter based on IP address
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *  @param ipv4_addr    ipv4 Address
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_ipaddr_arp_filter(IN pmlan_adapter pmadapter,
		       IN pmlan_ioctl_req pioctl_req, IN t_u32 ipv4_addr)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	t_u8 *buf;
	arpfilter_header *arpfilter = MNULL;
	filter_entry *entry = MNULL;
	t_u32 len;

	ENTER();

	pcb->moal_malloc(pmadapter->pmoal_handle, MRVDRV_SIZE_OF_CMD_BUFFER,
			 MLAN_MEM_DEF, &buf);
	if (!buf) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	/* Construct the ARP filter TLV */
	arpfilter = (arpfilter_header *)buf;
	arpfilter->type = wlan_cpu_to_le16(TLV_TYPE_ARP_FILTER);

	if (ipv4_addr) {
		arpfilter->len = wlan_cpu_to_le16(sizeof(filter_entry) * 3);
		entry = (filter_entry *)(buf + sizeof(arpfilter_header));
		entry->addr_type = wlan_cpu_to_le16(ADDR_TYPE_BROADCAST);
		entry->eth_type = wlan_cpu_to_le16(ETHER_TYPE_ARP);
		entry->ipv4_addr = wlan_cpu_to_le32(ipv4_addr);
		entry++;
		entry->addr_type = wlan_cpu_to_le16(ADDR_TYPE_UNICAST);
		entry->eth_type = wlan_cpu_to_le16(ETHER_TYPE_ANY);
		entry->ipv4_addr = wlan_cpu_to_le32(IPV4_ADDR_ANY);
		entry++;
		entry->addr_type = wlan_cpu_to_le16(ADDR_TYPE_MULTICAST);
		entry->eth_type = wlan_cpu_to_le16(ETHER_TYPE_ANY);
		entry->ipv4_addr = wlan_cpu_to_le32(IPV4_ADDR_ANY);
	} else
		arpfilter->len = 0;

	/* Update the total length */
	len = sizeof(arpfilter_header) + wlan_le16_to_cpu(arpfilter->len);

	memset(pmadapter, pmadapter->arp_filter, 0,
	       sizeof(pmadapter->arp_filter));
	if (len > ARP_FILTER_MAX_BUF_SIZE) {
		pmadapter->arp_filter_size = 0;
		PRINTM(MERROR, "Invalid ARP Filter Size\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_FAILURE;
	} else if (len <= sizeof(MrvlIEtypesHeader_t)) {
		pmadapter->arp_filter_size = 0;
		PRINTM(MINFO, "Clear ARP filter\n");
	} else {
		memcpy(pmadapter, pmadapter->arp_filter, buf, len);
		pmadapter->arp_filter_size = len;
		HEXDUMP("ArpFilter", pmadapter->arp_filter,
			pmadapter->arp_filter_size);
	}

done:
	if (buf)
		pcb->moal_mfree(pmadapter->pmoal_handle, buf);
	LEAVE();
	return ret;
}

#define FLTR_BUF_IP_OFFSET              24
#define FLTR_BUF_IP_OFFSET_2_IP_1       9
#define FLTR_BUF_IP_OFFSET_2_IP_2       26

/**
 *  @brief Enable/Disable Auto ARP resonse
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *  @param ipv4_addr    ipv4 Address
 *  @param num_ipv4     Number of ipv4 Addresses
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_ipaddr_auto_arp_resp(IN pmlan_adapter pmadapter,
			  IN pmlan_ioctl_req pioctl_req,
			  IN t_u32 *ipv4_addr, IN t_u8 num_ipv4)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	HostCmd_DS_GEN *hostcmd_hdr;
	HostCmd_DS_MEF_CFG *mefcmd;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_ds_misc_cmd *hostcmd;
	t_u32 buf_len = 0;
	t_u8 *buf, *filter;

	t_u8 fltr_buf[] = { 0x01, 0x10, 0x21, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x01, 0x08, 0x06, 0x02, 0x02, 0x14, 0x00, 0x00,
		0x00, 0x01, 0x41, 0x01, 0x00, 0x00, 0x00, 0x01,
		0xc0, 0xa8, 0x01, 0x6d, 0x04, 0x02, 0x2e, 0x00,
		0x00, 0x00, 0x01, 0x41, 0x44
	};
	t_u8 fltr_buf_2_ip[] = { 0x01, 0x10, 0x33, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x01, 0xc0, 0xa8, 0x01, 0x6d, 0x04, 0x02, 0x2e,
		0x00, 0x00, 0x00, 0x01, 0x41, 0x01, 0x00, 0x00,
		0x00, 0x01, 0xc0, 0xa8, 0x02, 0x6d, 0x04, 0x02,
		0x2e, 0x00, 0x00, 0x00, 0x01, 0x41, 0x45, 0x01,
		0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x02, 0x02,
		0x14, 0x00, 0x00, 0x00, 0x01, 0x41, 0x44
	};

	ENTER();

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_cmd),
			       MLAN_MEM_DEF, (t_u8 **)&hostcmd);

	if (ret != MLAN_STATUS_SUCCESS || hostcmd == MNULL) {
		PRINTM(MERROR, "Failed to allocate hostcmd  buffer\n");
		pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	memset(pmpriv->adapter, hostcmd, 0, sizeof(mlan_ds_misc_cmd));
	buf = hostcmd->cmd;

	/* Prepare hostcmd buffer */
	hostcmd_hdr = (HostCmd_DS_GEN *)(buf);
	hostcmd_hdr->command = wlan_cpu_to_le16(HostCmd_CMD_MEF_CFG);
	mefcmd = (HostCmd_DS_MEF_CFG *)(buf + S_DS_GEN);
	buf_len = S_DS_GEN;

	if (!ipv4_addr) {
		PRINTM(MINFO, "Disable Auto ARP Response\n");
		mefcmd->criteria = wlan_cpu_to_le32(0);
		mefcmd->nentries = wlan_cpu_to_le16(0);
		buf_len += sizeof(HostCmd_DS_MEF_CFG);
	} else {
		/* Event bit (bit2) of HS conditions should be masked out */
		mefcmd->criteria =
			wlan_cpu_to_le32(pmpriv->adapter->hs_cfg.
					 conditions & ~MBIT(2));
		mefcmd->nentries = wlan_cpu_to_le16(1);
		buf_len += sizeof(HostCmd_DS_MEF_CFG);
		filter = buf + buf_len;
		if (num_ipv4 == 1) {
			memcpy(pmpriv->adapter, filter, fltr_buf,
			       sizeof(fltr_buf));
			memcpy(pmpriv->adapter, &filter[FLTR_BUF_IP_OFFSET],
			       &ipv4_addr[0], sizeof(t_u32));
			buf_len += sizeof(fltr_buf);
		} else if (num_ipv4 >= 2) {
			memcpy(pmpriv->adapter, filter, fltr_buf_2_ip,
			       sizeof(fltr_buf_2_ip));
			memcpy(pmpriv->adapter,
			       &filter[FLTR_BUF_IP_OFFSET_2_IP_1],
			       &ipv4_addr[0], sizeof(t_u32));
			memcpy(pmpriv->adapter,
			       &filter[FLTR_BUF_IP_OFFSET_2_IP_2],
			       &ipv4_addr[1], sizeof(t_u32));
			buf_len += sizeof(fltr_buf_2_ip);
		}
	}
	hostcmd_hdr->size = wlan_cpu_to_le16(buf_len);
	hostcmd->len = buf_len;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       0,
			       0, 0, (t_void *)pioctl_req, (t_void *)hostcmd);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)hostcmd);

	LEAVE();
	return ret;
}

/**
 *  @brief MEF configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_mef_cfg(IN pmlan_adapter pmadapter,
			IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_mef_cfg *mef_cfg =
		&((mlan_ds_misc_cfg *)pioctl_req->pbuf)->param.mef_cfg;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	HostCmd_DS_GEN *hostcmd_hdr;
	HostCmd_DS_MEF_CFG *mefcmd;
	mlan_ds_misc_cmd *hostcmd = MNULL;
	t_u32 buf_len = 0;
	t_u8 *buf, *filter;
	t_u8 fltr_buf[] = { 0x02, 0x00, 0x2f, 0x00, 0x01, 0x00, 0x00, 0x00,
		0x01, 0x01, 0x00, 0x5e, 0x03, 0x02, 0x00, 0x00,
		0x00, 0x00, 0x01, 0x41, 0x06, 0x00, 0x00, 0x00,
		0x01, 0xff, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x41, 0x45, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x33, 0x33, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x01, 0x41, 0x45
	};

	ENTER();

	/* GET operation */
	if (pioctl_req->action == MLAN_ACT_GET) {
		/* TODO: need to store for get operation */
		goto done;
	}

	ret = pcb->moal_malloc(pmadapter->pmoal_handle,
			       sizeof(mlan_ds_misc_cmd),
			       MLAN_MEM_DEF, (t_u8 **)&hostcmd);

	if (ret != MLAN_STATUS_SUCCESS || hostcmd == MNULL) {
		PRINTM(MERROR, "Failed to allocate hostcmd  buffer\n");
		pioctl_req->status_code = MLAN_ERROR_NO_MEM;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	memset(pmpriv->adapter, hostcmd, 0, sizeof(mlan_ds_misc_cmd));
	buf = hostcmd->cmd;

	/* Prepare hostcmd buffer */
	hostcmd_hdr = (HostCmd_DS_GEN *)(buf);
	hostcmd_hdr->command = wlan_cpu_to_le16(HostCmd_CMD_MEF_CFG);
	mefcmd = (HostCmd_DS_MEF_CFG *)(buf + S_DS_GEN);
	buf_len = S_DS_GEN;

	switch (mef_cfg->sub_id) {
	case MEF_CFG_DISABLE:
		PRINTM(MINFO, "Disable MEF\n");
		mefcmd->criteria = wlan_cpu_to_le32(0);
		mefcmd->nentries = wlan_cpu_to_le16(0);
		buf_len += sizeof(HostCmd_DS_MEF_CFG);
		break;
	case MEF_CFG_RX_FILTER_ENABLE:
		PRINTM(MINFO, "Enable Rx filter\n");
		mefcmd->criteria = wlan_cpu_to_le32((MBIT(3) | MBIT(0)));
		mefcmd->nentries = wlan_cpu_to_le16(1);
		buf_len += sizeof(HostCmd_DS_MEF_CFG);
		filter = buf + buf_len;
		memcpy(pmpriv->adapter, filter, fltr_buf, sizeof(fltr_buf));
		buf_len += sizeof(fltr_buf);
		break;
	case MEF_CFG_AUTO_ARP_RESP:
		PRINTM(MINFO, "Enable auto ARP response\n");
		/* TODO */
		break;
	case MEF_CFG_HOSTCMD:
		PRINTM(MINFO, "MEF hostcmd from MOAL\n");
		filter = buf + buf_len;
		memcpy(pmpriv->adapter, filter, mef_cfg->param.cmd_buf.cmd,
		       mef_cfg->param.cmd_buf.len);
		buf_len += mef_cfg->param.cmd_buf.len;
		break;
	default:
		PRINTM(MERROR, "Invalid sub ID parameter\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_FAILURE;
		goto done;
		break;
	}
	hostcmd_hdr->size = wlan_cpu_to_le16(buf_len);
	hostcmd->len = buf_len;

	/* Send command to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       0,
			       0, 0, (t_void *)pioctl_req, (t_void *)hostcmd);
	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

done:
	if (hostcmd)
		pcb->moal_mfree(pmadapter->pmoal_handle, (t_u8 *)hostcmd);

	LEAVE();
	return ret;
}

/**
 *  @brief ipaddr configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_ipaddr_cfg(IN pmlan_adapter pmadapter,
			   IN pmlan_ioctl_req pioctl_req)
{
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 ipv4_addr[MAX_IPADDR];
	int i = 0;

	ENTER();

	/* GET operation */
	if (pioctl_req->action == MLAN_ACT_GET) {
		memcpy(pmadapter, misc->param.ipaddr_cfg.ip_addr,
		       pmpriv->ip_addr, IPADDR_LEN);
		misc->param.ipaddr_cfg.op_code = pmpriv->op_code;
		goto done;
	}
	/* only one IP is supported in current firmware */
	for (i = 0; i < misc->param.ipaddr_cfg.ip_addr_num; i++) {
		memcpy(pmadapter, &ipv4_addr[i],
		       misc->param.ipaddr_cfg.ip_addr[i], sizeof(t_u32));
	}

	if (misc->param.ipaddr_cfg.op_code != MLAN_IPADDR_OP_IP_REMOVE &&
	    !misc->param.ipaddr_cfg.ip_addr_num) {
		PRINTM(MERROR, "Invalid IPv4 address\n");
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	if (misc->param.ipaddr_cfg.op_code & MLAN_IPADDR_OP_ARP_FILTER)
		ret = wlan_ipaddr_arp_filter(pmadapter, pioctl_req,
					     ipv4_addr[0]);
	else if (pmpriv->op_code & MLAN_IPADDR_OP_ARP_FILTER)
		ret = wlan_ipaddr_arp_filter(pmadapter, pioctl_req, 0);
	if (ret == MLAN_STATUS_FAILURE)
		goto done;
	if (misc->param.ipaddr_cfg.op_code & MLAN_IPADDR_OP_AUTO_ARP_RESP)
		ret = wlan_ipaddr_auto_arp_resp(pmadapter, pioctl_req,
						ipv4_addr,
						misc->param.ipaddr_cfg.
						ip_addr_num);
	else if (pmpriv->op_code & MLAN_IPADDR_OP_AUTO_ARP_RESP)
		ret = wlan_ipaddr_auto_arp_resp(pmadapter, pioctl_req, MNULL,
						0);
	if (ret == MLAN_STATUS_FAILURE)
		goto done;

	/* Save the values in MLAN */
	if (pioctl_req->action == MLAN_ACT_SET) {
		pmpriv->op_code = misc->param.ipaddr_cfg.op_code;
		memcpy(pmadapter, pmpriv->ip_addr,
		       misc->param.ipaddr_cfg.ip_addr, IPADDR_LEN);
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief CFP code configuration
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_cfp_code_cfg(IN pmlan_adapter pmadapter,
			     IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	mlan_ds_misc_cfp_code *cfp_code = MNULL;
	t_u32 region_bg = 0;
	t_u32 region_a = 0;
	int i;

	ENTER();

	cfp_code = &misc->param.cfp_code;
	if (pioctl_req->action == MLAN_ACT_SET) {
		if (pmadapter->otp_region && pmadapter->otp_region->force_reg) {
			PRINTM(MERROR,
			       "ForceRegionRule is set in the on-chip OTP"
			       "memory\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		/* Save the values in MLAN */
		if (!cfp_code->cfp_code_bg)
			cfp_code->cfp_code_bg = pmadapter->cfp_code_bg;
		for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
			/* Use the region code to search for the index */
			if (cfp_code->cfp_code_bg == region_code_index[i]) {
				region_bg = cfp_code->cfp_code_bg;
				break;
			}
		}
		if (!cfp_code->cfp_code_a)
			cfp_code->cfp_code_a = pmadapter->cfp_code_a;
		for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
			/* Use the region code to search for the index */
			if (cfp_code->cfp_code_a == region_code_index[i]) {
				region_a = cfp_code->cfp_code_a;
				break;
			}
		}
		if (!region_a) {
			for (i = 0; i < MRVDRV_MAX_CFP_CODE_A; i++) {
				/* Use the CFP code to search for the index */
				if (cfp_code->cfp_code_a == cfp_code_index_a[i])
					break;
			}
			if (i >= MRVDRV_MAX_CFP_CODE_A) {
				PRINTM(MERROR,
				       "CFP Code not identified for A\n");
				pioctl_req->status_code =
					MLAN_ERROR_INVALID_PARAMETER;
				ret = MLAN_STATUS_FAILURE;
				goto done;
			}
		}
		pmadapter->cfp_code_bg = (t_u8)cfp_code->cfp_code_bg;
		pmadapter->cfp_code_a = (t_u8)cfp_code->cfp_code_a;
		if (region_bg && region_a && (region_bg == region_a))
			pmadapter->region_code = pmadapter->cfp_code_a;
		else
			pmadapter->region_code = 0;
		if (wlan_set_regiontable(pmpriv, (t_u8)pmadapter->region_code,
					 pmadapter->config_bands | pmadapter->
					 adhoc_start_band)) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	} else {
		/* GET operation */
		cfp_code->cfp_code_bg = pmadapter->cfp_code_bg;
		cfp_code->cfp_code_a = pmadapter->cfp_code_a;
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function sets up country code and downloads CMD to FW
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_ioctl_country_code(IN pmlan_adapter pmadapter,
			     IN mlan_ioctl_req *pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_country_code *country_code = MNULL;
	mlan_ds_misc_cfg *cfg_misc = MNULL;
	t_u8 cfp_bg = 0, cfp_a = 0;

	ENTER();

	cfg_misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	country_code = &cfg_misc->param.country_code;

	if (pioctl_req->action == MLAN_ACT_SET) {
		if (pmadapter->otp_region && pmadapter->otp_region->force_reg) {
			PRINTM(MERROR,
			       "ForceRegionRule is set in the on-chip OTP"
			       "memory\n");
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		/* Update region code and table based on country code */
		if (wlan_misc_country_2_cfp_table_code(pmadapter,
						       country_code->
						       country_code, &cfp_bg,
						       &cfp_a)) {
			PRINTM(MERROR, "Country code not found!\n");
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		pmadapter->cfp_code_bg = cfp_bg;
		pmadapter->cfp_code_a = cfp_a;
		if (cfp_bg && cfp_a && (cfp_bg == cfp_a))
			pmadapter->region_code = cfp_a;
		else
			pmadapter->region_code = 0;
		if (wlan_set_regiontable(pmpriv, pmadapter->region_code,
					 pmadapter->config_bands | pmadapter->
					 adhoc_start_band)) {
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
		memcpy(pmadapter, pmadapter->country_code,
		       country_code->country_code, COUNTRY_CODE_LEN);
	} else {
		/* GET operation */
		memcpy(pmadapter, country_code->country_code,
		       pmadapter->country_code, COUNTRY_CODE_LEN);
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Configure MFPC and MFPR for management frame protection
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_pmfcfg(IN pmlan_adapter pmadapter, IN mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *cfg_misc = MNULL;
	mlan_ds_misc_pmfcfg *pmfcfg;

	cfg_misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	pmfcfg = &cfg_misc->param.pmfcfg;

	if (pioctl_req->action == MLAN_ACT_SET) {
		pmpriv->pmfcfg.mfpc = pmfcfg->mfpc;
		pmpriv->pmfcfg.mfpr = pmfcfg->mfpr;
	} else {
		/* GET operation */
		pmfcfg->mfpc = pmpriv->pmfcfg.mfpc;
		pmfcfg->mfpr = pmpriv->pmfcfg.mfpr;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief get sensor temperature
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   A pointer to ioctl request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_get_sensor_temp(IN pmlan_adapter pmadapter,
				IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_GET)
		cmd_action = HostCmd_ACT_GEN_GET;
	else {
		PRINTM(MERROR, " sensor temp only support get operation \n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_DS_GET_SENSOR_TEMP,
			       cmd_action, 0, (t_void *)pioctl_req, MNULL);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Gtk Rekey Offload
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_misc_ioctl_gtk_rekey_offload(IN pmlan_adapter pmadapter,
				  IN pmlan_ioctl_req pioctl_req)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc_cfg = MNULL;
	t_u16 cmd_action = 0;
	mlan_private *pmpriv = pmadapter->priv[pioctl_req->bss_index];

	ENTER();
	misc_cfg = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else if (pioctl_req->action == MLAN_ACT_CLEAR)
		cmd_action = HostCmd_ACT_GEN_REMOVE;
	else
		cmd_action = HostCmd_ACT_GEN_GET;

	if (!pmpriv->wpa_is_gtk_set) {
		/* Store the gtk rekey data if it has already set gtk */
		memcpy(pmadapter, &pmpriv->gtk_rekey,
		       &misc_cfg->param.gtk_rekey,
		       sizeof(mlan_ds_misc_gtk_rekey_data));
		LEAVE();
		return ret;
	}
	/* Send request to firmware if it hasn't set gtk yet */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_GTK_REKEY_OFFLOAD_CFG,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req,
			       &misc_cfg->param.gtk_rekey);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief enable/disable roam offload in firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_roam_offload(IN pmlan_adapter pmadapter,
		       IN mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (!(pmadapter->fw_cap_info & FW_ROAMING_SUPPORT)) {
		PRINTM(MERROR, "Firmware roaming not support\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (!IS_FW_SUPPORT_SUPPLICANT(pmadapter)) {
		PRINTM(MERROR, "Embedded supplicant do not enable\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if ((misc->param.roam_offload.config_mode == ROAM_OFFLOAD_ENABLE) &&
	    misc->param.roam_offload.userset_passphrase) {
		pmpriv->adapter->userset_passphrase =
			misc->param.roam_offload.userset_passphrase;
		if (!misc->param.roam_offload.enable) {
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
	}

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else {
		PRINTM(MERROR, "Unsupported cmd_action\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_ROAM_OFFLOAD,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req, &misc->param.roam_offload);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief set roam offload aplist to firmware
 *
 *  @param pmadapter    A pointer to mlan_adapter structure
 *  @param pioctl_req   Pointer to the IOCTL request buffer
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_misc_roam_offload_aplist(IN pmlan_adapter pmadapter,
			      IN mlan_ioctl_req *pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_ds_misc_cfg *misc = MNULL;
	t_u16 cmd_action = 0;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;

	if (pioctl_req->action == MLAN_ACT_SET)
		cmd_action = HostCmd_ACT_GEN_SET;
	else {
		PRINTM(MERROR, "Unsupported cmd_action\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Send request to firmware */
	ret = wlan_prepare_cmd(pmpriv,
			       HostCmd_CMD_ROAM_OFFLOAD,
			       cmd_action,
			       0,
			       (t_void *)pioctl_req, &misc->param.roam_offload);

	if (ret == MLAN_STATUS_SUCCESS)
		ret = MLAN_STATUS_PENDING;

	LEAVE();
	return ret;
}

/**
 *  @brief Miscellaneous configuration handler
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_misc_cfg_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_misc_cfg *misc = MNULL;

	ENTER();

	if ((pioctl_req == MNULL) || (pioctl_req->pbuf == MNULL)) {
		PRINTM(MERROR, "Request buffer not found!\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
	if (pioctl_req->buf_len < sizeof(mlan_ds_misc_cfg)) {
		PRINTM(MWARN, "MLAN bss IOCTL length is too short.\n");
		pioctl_req->data_read_written = 0;
		pioctl_req->buf_len_needed = sizeof(mlan_ds_misc_cfg);
		pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
		LEAVE();
		return MLAN_STATUS_RESOURCE;
	}
	misc = (mlan_ds_misc_cfg *)pioctl_req->pbuf;
	switch (misc->sub_command) {
	case MLAN_OID_MISC_GEN_IE:
		status = wlan_misc_ioctl_gen_ie(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_REGION:
		status = wlan_misc_ioctl_region(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_WARM_RESET:
		util_enqueue_list_tail(pmadapter->pmoal_handle,
				       &pmadapter->ioctl_pending_q,
				       (pmlan_linked_list)pioctl_req,
				       pmadapter->callbacks.moal_spin_lock,
				       pmadapter->callbacks.moal_spin_unlock);
		pmadapter->pending_ioctl = MTRUE;
		status = MLAN_STATUS_PENDING;
		break;
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
	case MLAN_OID_MISC_SDIO_MPA_CTRL:
		status = wlan_misc_ioctl_sdio_mpa_ctrl(pmadapter, pioctl_req);
		break;
#endif
	case MLAN_OID_MISC_HOST_CMD:
		status = wlan_misc_ioctl_host_cmd(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_SYS_CLOCK:
		status = wlan_misc_ioctl_sysclock(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_WWS:
		status = wlan_misc_ioctl_wws_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_ASSOC_RSP:
		status = wlan_misc_ioctl_get_assoc_rsp(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_INIT_SHUTDOWN:
		status = wlan_misc_ioctl_init_shutdown(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_SOFT_RESET:
		status = wlan_misc_ioctl_soft_reset(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_COALESCING_STATUS:
		status = wlan_misc_ioctl_coalescing_status(pmadapter,
							   pioctl_req);
		break;
	case MLAN_OID_MISC_CUSTOM_IE:
		status = wlan_misc_ioctl_custom_ie_list(pmadapter, pioctl_req,
							MTRUE);
		break;
	case MLAN_OID_MISC_TDLS_CONFIG:
		status = wlan_misc_ioctl_tdls_config(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_TDLS_OPER:
		status = wlan_misc_ioctl_tdls_oper(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_GET_TDLS_IES:
		status = wlan_misc_ioctl_tdls_get_ies(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_TDLS_CS_CHANNEL:
		status = wlan_misc_ioctl_tdls_cs_channel(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_TDLS_IDLE_TIME:
		status = wlan_misc_ioctl_tdls_idle_time(pmadapter, pioctl_req);
		break;

	case MLAN_OID_MISC_NET_MONITOR:
		status = wlan_misc_ioctl_net_monitor(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_MAC_CONTROL:
		status = wlan_misc_ioctl_mac_control(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_MEF_CFG:
		status = wlan_misc_ioctl_mef_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_RX_MGMT_IND:
		status = wlan_reg_rx_mgmt_ind(pmadapter, pioctl_req);
		break;
#ifdef DEBUG_LEVEL1
	case MLAN_OID_MISC_DRVDBG:
		status = wlan_set_drvdbg(pmadapter, pioctl_req);
		break;
#endif
	case MLAN_OID_MISC_IP_ADDR:
		status = wlan_misc_ioctl_ipaddr_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_CFP_CODE:
		status = wlan_misc_ioctl_cfp_code_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_COUNTRY_CODE:
		status = wlan_misc_ioctl_country_code(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_THERMAL:
		status = wlan_misc_ioctl_thermal(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_SUBSCRIBE_EVENT:
		status = wlan_misc_ioctl_subscribe_evt(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_OTP_USER_DATA:
		status = wlan_misc_otp_user_data(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_TXCONTROL:
		status = wlan_misc_ioctl_txcontrol(pmadapter, pioctl_req);
		break;
#ifdef STA_SUPPORT
	case MLAN_OID_MISC_EXT_CAP_CFG:
		status = wlan_misc_ext_capa_cfg(pmadapter, pioctl_req);
		break;
#endif
	case MLAN_OID_MISC_PMFCFG:
		status = wlan_misc_pmfcfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_MULTI_CHAN_CFG:
		status = wlan_misc_ioctl_multi_chan_config(pmadapter,
							   pioctl_req);
		break;
	case MLAN_OID_MISC_MULTI_CHAN_POLICY:
		status = wlan_misc_ioctl_multi_chan_policy(pmadapter,
							   pioctl_req);
		break;
	case MLAN_OID_MISC_DRCS_CFG:
		status = wlan_misc_ioctl_drcs_config(pmadapter, pioctl_req);
		break;
#ifdef RX_PACKET_COALESCE
	case MLAN_OID_MISC_RX_PACKET_COALESCE:
		status = wlan_misc_ioctl_rx_pkt_coalesce_config(pmadapter,
								pioctl_req);
		break;
#endif
	case MLAN_OID_MISC_PMIC_CFG:
		status = wlan_misc_ioctl_pmic_configure(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_CWMODE_CTRL:
		status = wlan_misc_ioctl_cwmode_ctrl(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_DFS_REAPTER_MODE:
		status = wlan_misc_ioctl_dfs_repeater_cfg(pmadapter,
							  pioctl_req);
		break;
	case MLAN_OID_MISC_COALESCE_CFG:
		status = wlan_misc_ioctl_coalesce_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_GET_SENSOR_TEMP:
		status = wlan_misc_ioctl_get_sensor_temp(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_OPER_CLASS:
		status = wlan_misc_ioctl_oper_class(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_OPER_CLASS_CHECK:
		status = wlan_misc_ioctl_operclass_validation(pmadapter,
							      pioctl_req);
		break;
	case MLAN_OID_MISC_GTK_REKEY_OFFLOAD:
		status = wlan_misc_ioctl_gtk_rekey_offload(pmadapter,
							   pioctl_req);
		break;
	case MLAN_OID_MISC_IND_RST_CFG:
		status = wlan_misc_ioctl_ind_rst_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_GET_TSF:
		status = wlan_misc_ioctl_get_tsf(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_ROAM_OFFLOAD:
		status = wlan_misc_roam_offload(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_ROAM_OFFLOAD_APLIST:
		status = wlan_misc_roam_offload_aplist(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_GET_CHAN_REGION_CFG:
		status = wlan_misc_chan_reg_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_FW_DUMP_EVENT:
		status = wlan_misc_ioctl_fw_dump_event(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_PER_PKT_CFG:
		status = wlan_misc_per_pkt_cfg(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_ROBUSTCOEX:
		status = wlan_misc_robustcoex(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_GET_CORRELATED_TIME:
		status = wlan_misc_get_correlated_time(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_BOOT_SLEEP:
		status = wlan_misc_bootsleep(pmadapter, pioctl_req);
		break;
	case MLAN_OID_MISC_CFP_INFO:
		status = wlan_get_cfpinfo(pmadapter, pioctl_req);
		break;
	default:
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}

/**
 *  @brief Set/Get scan configuration parameter
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *  @param action	    Set/Get
 *
 *  @return		MLAN_STATUS_SUCCESS --success
 */
static mlan_status
wlan_set_get_scan_cfg(IN pmlan_adapter pmadapter,
		      IN pmlan_ioctl_req pioctl_req, IN t_u32 action)
{
	mlan_ds_scan *scan = MNULL;

	ENTER();

	scan = (mlan_ds_scan *)pioctl_req->pbuf;
	if (action == MLAN_ACT_GET) {
		scan->param.scan_cfg.scan_type = (t_u32)pmadapter->scan_type;
		scan->param.scan_cfg.scan_mode = pmadapter->scan_mode;
		scan->param.scan_cfg.scan_probe = (t_u32)pmadapter->scan_probes;
		scan->param.scan_cfg.scan_time.specific_scan_time =
			(t_u32)pmadapter->specific_scan_time;
		scan->param.scan_cfg.scan_time.active_scan_time =
			(t_u32)pmadapter->active_scan_time;
		scan->param.scan_cfg.scan_time.passive_scan_time =
			(t_u32)pmadapter->passive_scan_time;
		scan->param.scan_cfg.ext_scan = pmadapter->ext_scan;
	} else {
		if (scan->param.scan_cfg.scan_type)
			pmadapter->scan_type =
				(t_u8)scan->param.scan_cfg.scan_type;
		if (scan->param.scan_cfg.scan_mode)
			pmadapter->scan_mode = scan->param.scan_cfg.scan_mode;
		if (scan->param.scan_cfg.scan_probe)
			pmadapter->scan_probes =
				(t_u16)scan->param.scan_cfg.scan_probe;
		if (scan->param.scan_cfg.scan_time.specific_scan_time)
			pmadapter->specific_scan_time =
				(t_u16)scan->param.scan_cfg.scan_time.
				specific_scan_time;
		if (scan->param.scan_cfg.scan_time.active_scan_time)
			pmadapter->active_scan_time =
				(t_u16)scan->param.scan_cfg.scan_time.
				active_scan_time;
		if (scan->param.scan_cfg.scan_time.passive_scan_time)
			pmadapter->passive_scan_time =
				(t_u16)scan->param.scan_cfg.scan_time.
				passive_scan_time;
		pmadapter->ext_scan = scan->param.scan_cfg.ext_scan;
	}
	pioctl_req->data_read_written = sizeof(t_u32) + MLAN_SUB_COMMAND_SIZE;

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Set/Get scan
 *
 *  @param pmadapter	A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
static mlan_status
wlan_scan_ioctl(IN pmlan_adapter pmadapter, IN pmlan_ioctl_req pioctl_req)
{
	pmlan_private pmpriv = pmadapter->priv[pioctl_req->bss_index];
	mlan_status status = MLAN_STATUS_SUCCESS;
	mlan_ds_scan *pscan;

	ENTER();

	pscan = (mlan_ds_scan *)pioctl_req->pbuf;
	if (pscan->sub_command == MLAN_OID_SCAN_CONFIG
	    || pscan->sub_command == MLAN_OID_SCAN_BGSCAN_CONFIG)
		goto start_config;
	if (pmadapter->scan_processing && pioctl_req->action == MLAN_ACT_SET &&
	    pscan->sub_command != MLAN_OID_SCAN_CANCEL) {
		PRINTM(MINFO, "Scan already in process...\n");
		LEAVE();
		return status;
	}

	if (pmadapter->enable_net_mon == CHANNEL_SPEC_SNIFFER_MODE) {
		PRINTM(MINFO,
		       "Scan is blocked in Channel Specified Network Monitor mode...\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (pmadapter->scan_block && pioctl_req->action == MLAN_ACT_SET) {
		PRINTM(MERROR, "Scan is blocked during association...\n");
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}
start_config:
	/* Set scan */
	if (pioctl_req->action == MLAN_ACT_SET) {

		switch (pscan->sub_command) {
		case MLAN_OID_SCAN_NORMAL:
			status = wlan_scan_networks(pmpriv, pioctl_req, MNULL);
			break;
		case MLAN_OID_SCAN_SPECIFIC_SSID:
			status = wlan_scan_specific_ssid(pmpriv, pioctl_req,
							 &pscan->param.scan_req.
							 scan_ssid);
			break;
		case MLAN_OID_SCAN_USER_CONFIG:
			status = wlan_scan_networks(pmpriv, pioctl_req,
						    (wlan_user_scan_cfg *)
						    pscan->param.user_scan.
						    scan_cfg_buf);
			break;
		case MLAN_OID_SCAN_CONFIG:
			status = wlan_set_get_scan_cfg(pmadapter,
						       pioctl_req,
						       MLAN_ACT_SET);
			break;
		case MLAN_OID_SCAN_CANCEL:
			status = wlan_cancel_pending_scan_cmd(pmadapter,
							      pioctl_req);
			break;
		case MLAN_OID_SCAN_TABLE_FLUSH:
			status = wlan_flush_scan_table(pmadapter);
			break;
		case MLAN_OID_SCAN_BGSCAN_CONFIG:
			/* Send request to firmware */
			status = wlan_prepare_cmd(pmpriv,
						  HostCmd_CMD_802_11_BG_SCAN_CONFIG,
						  HostCmd_ACT_GEN_SET,
						  0,
						  (t_void *)pioctl_req,
						  pscan->param.user_scan.
						  scan_cfg_buf);
			break;
		default:
			pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
			status = MLAN_STATUS_FAILURE;
			break;
		}

		if ((status == MLAN_STATUS_SUCCESS) &&
		    (pscan->sub_command != MLAN_OID_SCAN_TABLE_FLUSH) &&
		    (pscan->sub_command != MLAN_OID_SCAN_CANCEL) &&
		    (pscan->sub_command != MLAN_OID_SCAN_CONFIG)) {
			PRINTM(MINFO,
			       "wlan_scan_ioctl: return MLAN_STATUS_PENDING\n");
			status = MLAN_STATUS_PENDING;
		}
	}
	/* Get scan */
	else {
		if (pscan->sub_command == MLAN_OID_SCAN_CONFIG) {
			status = wlan_set_get_scan_cfg(pmadapter, pioctl_req,
						       MLAN_ACT_GET);
		} else if (pscan->sub_command == MLAN_OID_SCAN_GET_CURRENT_BSS) {
			pscan->param.scan_resp.num_in_scan_table =
				pmadapter->num_in_scan_table;
			pscan->param.scan_resp.pscan_table =
				(t_u8 *)&pmpriv->curr_bss_params.bss_descriptor;
			pioctl_req->data_read_written =
				sizeof(mlan_scan_resp) + MLAN_SUB_COMMAND_SIZE;
		} else {
			if (pmadapter->bgscan_reported) {
				pmadapter->bgscan_reported = MFALSE;
				/* Clear the previous scan result */
				memset(pmadapter, pmadapter->pscan_table, 0x00,
				       sizeof(BSSDescriptor_t) *
				       MRVDRV_MAX_BSSID_LIST);
				pmadapter->num_in_scan_table = 0;
				pmadapter->pbcn_buf_end = pmadapter->bcn_buf;
				status = wlan_prepare_cmd(pmpriv,
							  HostCmd_CMD_802_11_BG_SCAN_QUERY,
							  HostCmd_ACT_GEN_GET,
							  0,
							  (t_void *)pioctl_req,
							  MNULL);
				if (status == MLAN_STATUS_SUCCESS) {
					PRINTM(MINFO,
					       "wlan_scan_ioctl: return MLAN_STATUS_PENDING\n");
					status = MLAN_STATUS_PENDING;
				}
			} else {
				pscan->param.scan_resp.pscan_table =
					(t_u8 *)pmadapter->pscan_table;
				pscan->param.scan_resp.num_in_scan_table =
					pmadapter->num_in_scan_table;
				pscan->param.scan_resp.age_in_secs =
					pmadapter->age_in_secs;
				pioctl_req->data_read_written =
					sizeof(mlan_scan_resp) +
					MLAN_SUB_COMMAND_SIZE;
				pscan->param.scan_resp.pchan_stats =
					(t_u8 *)pmadapter->pchan_stats;
				pscan->param.scan_resp.num_in_chan_stats =
					pmadapter->num_in_chan_stats;
			}
		}
	}

	LEAVE();
	return status;
}

/********************************************************
			Global Functions
********************************************************/

/**
 *  @brief Set ewpa mode
 *
 *  @param priv                 A pointer to mlan_private structure
 *  @param psec_pp              A pointer to mlan_ds_passphrase structure
 *
 *  @return                     MLAN_STATUS_SUCCESS
 */
mlan_status
wlan_set_ewpa_mode(mlan_private *priv, mlan_ds_passphrase *psec_pp)
{
	ENTER();

	if ((psec_pp->psk_type == MLAN_PSK_PASSPHRASE &&
	     psec_pp->psk.passphrase.passphrase_len > 0) ||
	    (psec_pp->psk_type == MLAN_PSK_PMK))
		priv->sec_info.ewpa_enabled = MTRUE;
	else
		priv->sec_info.ewpa_enabled = MFALSE;

	PRINTM(MINFO, "Set ewpa mode = %d\n", priv->sec_info.ewpa_enabled);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief Search for a BSS
 *
 *  @param pmpriv           A pointer to mlan_private structure
 *  @param pioctl_req       A pointer to ioctl request buffer
 *
 *  @return		            MLAN_STATUS_SUCCESS --success, otherwise fail
 */
mlan_status
wlan_find_bss(mlan_private *pmpriv, pmlan_ioctl_req pioctl_req)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_ds_bss *bss = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	t_u8 mac[MLAN_MAC_ADDR_LENGTH];
	int i = 0;
	BSSDescriptor_t *pbss_desc = MNULL;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_req->pbuf;

	if (memcmp
	    (pmadapter, &bss->param.ssid_bssid.bssid, zero_mac,
	     sizeof(zero_mac))) {
		if (bss->param.ssid_bssid.ssid.ssid_len)	/* ssid & bssid */
			i = wlan_find_ssid_in_list(pmpriv,
						   &bss->param.ssid_bssid.ssid,
						   (t_u8 *)&bss->param.
						   ssid_bssid.bssid,
						   pmpriv->bss_mode);
		else
			i = wlan_find_bssid_in_list(pmpriv,
						    (t_u8 *)&bss->param.
						    ssid_bssid.bssid,
						    pmpriv->bss_mode);
		if (i < 0) {
			memcpy(pmadapter, mac, &bss->param.ssid_bssid.bssid,
			       sizeof(mac));
			PRINTM(MIOCTL, "Can not find bssid " MACSTR "\n",
			       MAC2STR(mac));
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pbss_desc = &pmadapter->pscan_table[i];
		memcpy(pmadapter, &bss->param.ssid_bssid.ssid, &pbss_desc->ssid,
		       sizeof(mlan_802_11_ssid));
		bss->param.ssid_bssid.rssi = pbss_desc->rssi;
		bss->param.ssid_bssid.channel = (t_u16)pbss_desc->channel;

		bss->param.ssid_bssid.bss_band = pbss_desc->bss_band;
		/* index in bss list,start from 1 */
		bss->param.ssid_bssid.idx = i + 1;
	} else if (bss->param.ssid_bssid.ssid.ssid_len) {
		i = wlan_find_ssid_in_list(pmpriv, &bss->param.ssid_bssid.ssid,
					   MNULL, pmpriv->bss_mode);
		if (i < 0) {
			PRINTM(MIOCTL, "Can not find ssid %s\n",
			       bss->param.ssid_bssid.ssid.ssid);
			pioctl_req->status_code = MLAN_ERROR_INVALID_PARAMETER;
			LEAVE();
			return MLAN_STATUS_FAILURE;
		}
		pbss_desc = &pmadapter->pscan_table[i];
		memcpy(pmadapter, (t_u8 *)&bss->param.ssid_bssid.bssid,
		       (t_u8 *)&pbss_desc->mac_address, MLAN_MAC_ADDR_LENGTH);
		bss->param.ssid_bssid.rssi = pbss_desc->rssi;
		bss->param.ssid_bssid.channel = (t_u16)pbss_desc->channel;

		bss->param.ssid_bssid.bss_band = pbss_desc->bss_band;
		/* index in bss list, start from 1 */
		bss->param.ssid_bssid.idx = i + 1;
	} else {
		ret = wlan_find_best_network(pmpriv, &bss->param.ssid_bssid);
	}

	if (pbss_desc) {
		/**if rsn do not have ft akm, don't set ft cap and ft md*/
		if (pbss_desc->pmd_ie
		    && wlan_ft_akm_is_used(pmpriv, (t_u8 *)pbss_desc->prsn_ie)
			) {
			bss->param.ssid_bssid.ft_md = pbss_desc->pmd_ie->mdid;
			bss->param.ssid_bssid.ft_cap =
				pbss_desc->pmd_ie->ft_cap;
		}
	}

	LEAVE();
	return ret;
}

/**
 *  @brief MLAN station ioctl handler
 *
 *  @param adapter  A pointer to mlan_adapter structure
 *  @param pioctl_req	A pointer to ioctl request buffer
 *
 *  @return		MLAN_STATUS_SUCCESS/MLAN_STATUS_PENDING --success, otherwise fail
 */
mlan_status
wlan_ops_sta_ioctl(t_void *adapter, pmlan_ioctl_req pioctl_req)
{
	pmlan_adapter pmadapter = (pmlan_adapter)adapter;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	switch (pioctl_req->req_id) {
	case MLAN_IOCTL_SCAN:
		status = wlan_scan_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_BSS:
		status = wlan_bss_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_RADIO_CFG:
		status = wlan_radio_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_SNMP_MIB:
		status = wlan_snmp_mib_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_GET_INFO:
		status = wlan_get_info_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_SEC_CFG:
		status = wlan_sec_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_RATE:
		status = wlan_rate_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_POWER_CFG:
		status = wlan_power_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_PM_CFG:
		status = wlan_pm_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_WMM_CFG:
		status = wlan_wmm_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_WPS_CFG:
		status = wlan_wps_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_11N_CFG:
		status = wlan_11n_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_11D_CFG:
		status = wlan_11d_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_REG_MEM:
		status = wlan_reg_mem_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_MISC_CFG:
		status = wlan_misc_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_11H_CFG:
		status = wlan_11h_cfg_ioctl(pmadapter, pioctl_req);
		break;
	case MLAN_IOCTL_11K_CFG:
		status = wlan_11k_cfg_ioctl(pmadapter, pioctl_req);
		break;
	default:
		pioctl_req->status_code = MLAN_ERROR_IOCTL_INVALID;
		status = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return status;
}
