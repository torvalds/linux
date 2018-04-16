/** @file mlan_uap_cmdevent.c
 *
 *  @brief This file contains the handling of AP mode command and event
 *
 *  Copyright (C) 2009-2017, Marvell International Ltd.
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

/********************************************************
Change log:
    02/05/2009: initial version
********************************************************/

#include "mlan.h"
#include "mlan_util.h"
#include "mlan_fw.h"
#ifdef STA_SUPPORT
#include "mlan_join.h"
#endif
#include "mlan_main.h"
#include "mlan_uap.h"
#include "mlan_sdio.h"
#include "mlan_11n.h"
#include "mlan_11h.h"
#ifdef DRV_EMBEDDED_AUTHENTICATOR
#include "authenticator_api.h"
#endif

/********************************************************
			Local Functions
********************************************************/
/**
 *  @brief This function handles the command response error
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to command buffer
 *
 *  @return             N/A
 */
static mlan_status
uap_process_cmdresp_error(mlan_private *pmpriv, HostCmd_DS_COMMAND *resp,
			  mlan_ioctl_req *pioctl_buf)
{
	mlan_adapter *pmadapter = pmpriv->adapter;
	mlan_status ret = MLAN_STATUS_FAILURE;

	ENTER();
	if (resp->command != HostCmd_CMD_WMM_PARAM_CONFIG
	    || resp->command != HostCmd_CMD_CHAN_REGION_CFG)
		PRINTM(MERROR, "CMD_RESP: cmd %#x error, result=%#x\n",
		       resp->command, resp->result);
	if (pioctl_buf)
		pioctl_buf->status_code = resp->result;
	/*
	 * Handling errors here
	 */
	switch (resp->command) {
	case HostCmd_CMD_SDIO_SP_RX_AGGR_CFG:
		pmadapter->sdio_rx_aggr_enable = MFALSE;
		PRINTM(MMSG, "FW don't support SDIO single port rx aggr\n");
		break;

	case HOST_CMD_APCMD_SYS_CONFIGURE:
		{
			HostCmd_DS_SYS_CONFIG *sys_config =
				(HostCmd_DS_SYS_CONFIG *)&resp->params.
				sys_config;
			t_u16 resp_len = 0, travel_len = 0, index;
			mlan_ds_misc_custom_ie *cust_ie = MNULL;
			mlan_ds_misc_cfg *misc = MNULL;
			custom_ie *cptr;

			if (!pioctl_buf ||
			    (pioctl_buf->req_id != MLAN_IOCTL_MISC_CFG))
				break;
			misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
			if ((pioctl_buf->action == MLAN_ACT_SET) &&
			    (misc->sub_command == MLAN_OID_MISC_CUSTOM_IE)) {
				cust_ie =
					(mlan_ds_misc_custom_ie *)sys_config->
					tlv_buffer;
				if (cust_ie) {
					cust_ie->type =
						wlan_le16_to_cpu(cust_ie->type);
					resp_len = cust_ie->len =
						wlan_le16_to_cpu(cust_ie->len);
					travel_len = 0;
					/* conversion for index, mask, len */
					if (resp_len == sizeof(t_u16))
						cust_ie->ie_data_list[0].
							ie_index =
							wlan_cpu_to_le16
							(cust_ie->
							 ie_data_list[0].
							 ie_index);

					while (resp_len > sizeof(t_u16)) {
						cptr = (custom_ie
							*)(((t_u8 *)cust_ie->
							    ie_data_list) +
							   travel_len);
						index = cptr->ie_index =
							wlan_le16_to_cpu(cptr->
									 ie_index);
						cptr->mgmt_subtype_mask =
							wlan_le16_to_cpu(cptr->
									 mgmt_subtype_mask);
						cptr->ie_length =
							wlan_le16_to_cpu(cptr->
									 ie_length);
						travel_len +=
							cptr->ie_length +
							sizeof(custom_ie) -
							MAX_IE_SIZE;
						resp_len -=
							cptr->ie_length +
							sizeof(custom_ie) -
							MAX_IE_SIZE;
						if ((pmpriv->mgmt_ie[index].
						     mgmt_subtype_mask ==
						     cptr->mgmt_subtype_mask) &&
						    (pmpriv->mgmt_ie[index].
						     ie_length ==
						     cptr->ie_length) &&
						    !memcmp(pmpriv->adapter,
							    pmpriv->
							    mgmt_ie[index].
							    ie_buffer,
							    cptr->ie_buffer,
							    cptr->ie_length)) {
							PRINTM(MERROR,
							       "set custom ie fail, remove ie index :%d\n",
							       index);
							memset(pmadapter,
							       &pmpriv->
							       mgmt_ie[index],
							       0,
							       sizeof
							       (custom_ie));
						}
					}
				}
			}
		}
		break;
	case HostCmd_CMD_CHAN_REGION_CFG:
		ret = MLAN_STATUS_SUCCESS;
		PRINTM(MCMND, "FW don't support chan region cfg command!\n");
		break;

	default:
		break;
	}

	wlan_request_cmd_lock(pmadapter);
	wlan_insert_cmd_to_free_q(pmadapter, pmadapter->curr_cmd);
	pmadapter->curr_cmd = MNULL;
	wlan_release_cmd_lock(pmadapter);

	LEAVE();
	return ret;
}

/**
 *  @brief This function will return the pointer to station entry in station list
 *          table which matches the give mac address
 *
 *  @param priv    A pointer to mlan_private
 *
 *  @return	   A pointer to structure sta_node
 */
void
wlan_notify_station_deauth(mlan_private *priv)
{
	sta_node *sta_ptr;
	t_u8 event_buf[100];
	mlan_event *pevent = (mlan_event *)event_buf;
	t_u8 *pbuf;

	ENTER();
	sta_ptr = (sta_node *)util_peek_list(priv->adapter->pmoal_handle,
					     &priv->sta_list,
					     priv->adapter->callbacks.
					     moal_spin_lock,
					     priv->adapter->callbacks.
					     moal_spin_unlock);
	if (!sta_ptr) {
		LEAVE();
		return;
	}
	while (sta_ptr != (sta_node *)&priv->sta_list) {
		memset(priv->adapter, event_buf, 0, sizeof(event_buf));
		pevent->bss_index = priv->bss_index;
		pevent->event_id = MLAN_EVENT_ID_UAP_FW_STA_DISCONNECT;
		pevent->event_len = MLAN_MAC_ADDR_LENGTH + 2;
		pbuf = (t_u8 *)pevent->event_buf;
		/* reason field set to 0, Unspecified */
		memcpy(priv->adapter, pbuf + 2, sta_ptr->mac_addr,
		       MLAN_MAC_ADDR_LENGTH);
		wlan_recv_event(priv, pevent->event_id, pevent);
		sta_ptr = sta_ptr->pnext;
	}
	LEAVE();
	return;
}

/**
 * @brief This function prepares command of hs_cfg.
 *
 * @param pmpriv       A pointer to mlan_private structure
 * @param cmd          A pointer to HostCmd_DS_COMMAND structure
 * @param cmd_action   The action: GET or SET
 * @param pdata_buf    A pointer to data buffer
 *
 * @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_cmd_802_11_hs_cfg(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *cmd,
			   IN t_u16 cmd_action, IN hs_config_param *pdata_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_802_11_HS_CFG_ENH *phs_cfg =
		(HostCmd_DS_802_11_HS_CFG_ENH *)&(cmd->params.opt_hs_cfg);
	t_u8 *tlv = (t_u8 *)phs_cfg + sizeof(HostCmd_DS_802_11_HS_CFG_ENH);
	MrvlIEtypes_HsWakeHoldoff_t *holdoff_tlv = MNULL;
	MrvlIEtypes_WakeupSourceGPIO_t *gpio_tlv = MNULL;
	MrvlIEtypes_MgmtFrameFilter_t *mgmt_filter_tlv = MNULL;
	MrvlIEtypes_WakeupExtend_t *ext_tlv = MNULL;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_HS_CFG_ENH);
	cmd->size =
		wlan_cpu_to_le16(S_DS_GEN +
				 sizeof(HostCmd_DS_802_11_HS_CFG_ENH));

	if (pdata_buf == MNULL) {
		phs_cfg->action = wlan_cpu_to_le16(HS_ACTIVATE);
		phs_cfg->params.hs_activate.resp_ctrl =
			wlan_cpu_to_le16(RESP_NEEDED);
	} else {
		phs_cfg->action = wlan_cpu_to_le16(HS_CONFIGURE);
		phs_cfg->params.hs_config.conditions =
			wlan_cpu_to_le32(pdata_buf->conditions);
		phs_cfg->params.hs_config.gpio = pdata_buf->gpio;
		phs_cfg->params.hs_config.gap = pdata_buf->gap;
		if (pmpriv->adapter->min_wake_holdoff) {
			cmd->size =
				wlan_cpu_to_le16(S_DS_GEN +
						 sizeof
						 (HostCmd_DS_802_11_HS_CFG_ENH)
						 +
						 sizeof
						 (MrvlIEtypes_HsWakeHoldoff_t));
			holdoff_tlv = (MrvlIEtypes_HsWakeHoldoff_t *)tlv;
			holdoff_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_HS_WAKE_HOLDOFF);
			holdoff_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_HsWakeHoldoff_t) -
						 sizeof(MrvlIEtypesHeader_t));
			holdoff_tlv->min_wake_holdoff =
				wlan_cpu_to_le16(pmpriv->adapter->
						 min_wake_holdoff);
			tlv += sizeof(MrvlIEtypes_HsWakeHoldoff_t);
		}
		PRINTM(MCMND,
		       "HS_CFG_CMD: condition:0x%x gpio:0x%x gap:0x%x holdoff=%d\n",
		       phs_cfg->params.hs_config.conditions,
		       phs_cfg->params.hs_config.gpio,
		       phs_cfg->params.hs_config.gap,
		       pmpriv->adapter->min_wake_holdoff);

		if (pmadapter->param_type_ind == 1) {
			cmd->size += sizeof(MrvlIEtypes_WakeupSourceGPIO_t);
			gpio_tlv = (MrvlIEtypes_WakeupSourceGPIO_t *) tlv;
			gpio_tlv->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_HS_WAKEUP_SOURCE_GPIO);
			gpio_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_WakeupSourceGPIO_t)
						 - sizeof(MrvlIEtypesHeader_t));
			gpio_tlv->ind_gpio = (t_u8)pmadapter->ind_gpio;
			gpio_tlv->level = (t_u8)pmadapter->level;
			tlv += sizeof(MrvlIEtypes_WakeupSourceGPIO_t);
		}
		if (pmadapter->param_type_ext == 2) {
			cmd->size += sizeof(MrvlIEtypes_WakeupExtend_t);
			ext_tlv = (MrvlIEtypes_WakeupExtend_t *) tlv;
			ext_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_WAKEUP_EXTEND);
			ext_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_WakeupExtend_t) -
						 sizeof(MrvlIEtypesHeader_t));
			ext_tlv->event_force_ignore =
				wlan_cpu_to_le32(pmadapter->event_force_ignore);
			ext_tlv->event_use_ext_gap =
				wlan_cpu_to_le32(pmadapter->event_use_ext_gap);
			ext_tlv->ext_gap = pmadapter->ext_gap;
			ext_tlv->gpio_wave = pmadapter->gpio_wave;
			tlv += sizeof(MrvlIEtypes_WakeupExtend_t);
		}
		if (pmadapter->mgmt_filter[0].type) {
			int i = 0;
			mgmt_frame_filter mgmt_filter[MAX_MGMT_FRAME_FILTER];
			memset(pmadapter, mgmt_filter, 0,
			       MAX_MGMT_FRAME_FILTER *
			       sizeof(mgmt_frame_filter));
			mgmt_filter_tlv = (MrvlIEtypes_MgmtFrameFilter_t *) tlv;
			mgmt_filter_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_MGMT_FRAME_WAKEUP);
			tlv += sizeof(MrvlIEtypesHeader_t);
			while (i < MAX_MGMT_FRAME_FILTER &&
			       pmadapter->mgmt_filter[i].type) {
				mgmt_filter[i].action =
					(t_u8)pmadapter->mgmt_filter[i].action;
				mgmt_filter[i].type =
					(t_u8)pmadapter->mgmt_filter[i].type;
				mgmt_filter[i].frame_mask =
					wlan_cpu_to_le32(pmadapter->
							 mgmt_filter[i].
							 frame_mask);
				i++;
			}
			memcpy(pmadapter, (t_u8 *)mgmt_filter_tlv->filter,
			       (t_u8 *)mgmt_filter,
			       i * sizeof(mgmt_frame_filter));
			tlv += i * sizeof(mgmt_frame_filter);
			mgmt_filter_tlv->header.len =
				wlan_cpu_to_le16(i * sizeof(mgmt_frame_filter));
			cmd->size +=
				i * sizeof(mgmt_frame_filter) +
				sizeof(MrvlIEtypesHeader_t);
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of Tx data pause
 *
 *  @param pmpriv		A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_cmd_txdatapause(pmlan_private pmpriv,
			 IN HostCmd_DS_COMMAND *cmd,
			 IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_CMD_TX_DATA_PAUSE *pause_cmd =
		(HostCmd_DS_CMD_TX_DATA_PAUSE *)&cmd->params.tx_data_pause;
	mlan_ds_misc_tx_datapause *data_pause =
		(mlan_ds_misc_tx_datapause *)pdata_buf;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_CFG_TX_DATA_PAUSE);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_CMD_TX_DATA_PAUSE) +
				 S_DS_GEN);
	pause_cmd->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		pause_cmd->enable_tx_pause = (t_u8)data_pause->tx_pause;
		pause_cmd->pause_tx_count = (t_u8)data_pause->tx_buf_cnt;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of Tx data pause
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_txdatapause(IN pmlan_private pmpriv,
			 IN HostCmd_DS_COMMAND *resp,
			 IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_CMD_TX_DATA_PAUSE *pause_cmd =
		(HostCmd_DS_CMD_TX_DATA_PAUSE *)&resp->params.tx_data_pause;
	mlan_ds_misc_cfg *misc_cfg = MNULL;

	ENTER();

	if (pioctl_buf) {
		misc_cfg = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		misc_cfg->param.tx_datapause.tx_pause =
			pause_cmd->enable_tx_pause;
		misc_cfg->param.tx_datapause.tx_buf_cnt =
			pause_cmd->pause_tx_count;
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function will process tx pause event
 *
 *  @param priv    A pointer to mlan_private
 *  @param pevent  A pointer to event buf
 *
 *  @return	       N/A
 */
static void
wlan_process_tx_pause_event(pmlan_private priv, pmlan_buffer pevent)
{
	t_u16 tlv_type, tlv_len;
	int tlv_buf_left = pevent->data_len - sizeof(t_u32);
	MrvlIEtypesHeader_t *tlv =
		(MrvlIEtypesHeader_t *)(pevent->pbuf + pevent->data_offset +
					sizeof(t_u32));
	MrvlIEtypes_tx_pause_t *tx_pause_tlv;
	sta_node *sta_ptr = MNULL;
	t_u8 bc_mac[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	ENTER();

	while (tlv_buf_left >= (int)sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if ((sizeof(MrvlIEtypesHeader_t) + tlv_len) >
		    (unsigned int)tlv_buf_left) {
			PRINTM(MERROR, "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
			       tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == TLV_TYPE_TX_PAUSE) {
			tx_pause_tlv = (MrvlIEtypes_tx_pause_t *)tlv;
			PRINTM(MCMND, "TxPause: " MACSTR " pause=%d, pkts=%d\n",
			       MAC2STR(tx_pause_tlv->peermac),
			       tx_pause_tlv->tx_pause, tx_pause_tlv->pkt_cnt);
			if (!memcmp
			    (priv->adapter, bc_mac, tx_pause_tlv->peermac,
			     MLAN_MAC_ADDR_LENGTH))
				wlan_update_ralist_tx_pause(priv,
							    tx_pause_tlv->
							    peermac,
							    tx_pause_tlv->
							    tx_pause);
			else if (!memcmp
				 (priv->adapter, priv->curr_addr,
				  tx_pause_tlv->peermac,
				  MLAN_MAC_ADDR_LENGTH)) {
				if (tx_pause_tlv->tx_pause)
					priv->tx_pause = MTRUE;
				else
					priv->tx_pause = MFALSE;
			} else {
				sta_ptr =
					wlan_get_station_entry(priv,
							       tx_pause_tlv->
							       peermac);
				if (sta_ptr) {
					if (sta_ptr->tx_pause !=
					    tx_pause_tlv->tx_pause) {
						sta_ptr->tx_pause =
							tx_pause_tlv->tx_pause;
						wlan_update_ralist_tx_pause
							(priv,
							 tx_pause_tlv->peermac,
							 tx_pause_tlv->
							 tx_pause);
					}
				}
			}
		}
		tlv_buf_left -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}

	LEAVE();
	return;
}

/**
 *  @brief This function prepares command for config uap settings
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_uap_cmd_ap_config(pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *cmd,
		       IN t_u16 cmd_action, IN pmlan_ioctl_req pioctl_buf)
{
	mlan_ds_bss *bss = MNULL;
	HostCmd_DS_SYS_CONFIG *sys_config =
		(HostCmd_DS_SYS_CONFIG *)&cmd->params.sys_config;
	t_u8 *tlv = MNULL;
	MrvlIEtypes_MacAddr_t *tlv_mac = MNULL;
	MrvlIEtypes_SsIdParamSet_t *tlv_ssid = MNULL;
	MrvlIEtypes_beacon_period_t *tlv_beacon_period = MNULL;
	MrvlIEtypes_dtim_period_t *tlv_dtim_period = MNULL;
	MrvlIEtypes_RatesParamSet_t *tlv_rates = MNULL;
	MrvlIEtypes_tx_rate_t *tlv_txrate = MNULL;
	MrvlIEtypes_mcbc_rate_t *tlv_mcbc_rate = MNULL;
	MrvlIEtypes_tx_power_t *tlv_tx_power = MNULL;
	MrvlIEtypes_bcast_ssid_t *tlv_bcast_ssid = MNULL;
	MrvlIEtypes_antenna_mode_t *tlv_antenna = MNULL;
	MrvlIEtypes_pkt_forward_t *tlv_pkt_forward = MNULL;
	MrvlIEtypes_max_sta_count_t *tlv_sta_count = MNULL;
	MrvlIEtypes_sta_ageout_t *tlv_sta_ageout = MNULL;
	MrvlIEtypes_ps_sta_ageout_t *tlv_ps_sta_ageout = MNULL;
	MrvlIEtypes_rts_threshold_t *tlv_rts_threshold = MNULL;
	MrvlIEtypes_frag_threshold_t *tlv_frag_threshold = MNULL;
	MrvlIEtypes_retry_limit_t *tlv_retry_limit = MNULL;
	MrvlIEtypes_eapol_pwk_hsk_timeout_t *tlv_pairwise_timeout = MNULL;
	MrvlIEtypes_eapol_pwk_hsk_retries_t *tlv_pairwise_retries = MNULL;
	MrvlIEtypes_eapol_gwk_hsk_timeout_t *tlv_groupwise_timeout = MNULL;
	MrvlIEtypes_eapol_gwk_hsk_retries_t *tlv_groupwise_retries = MNULL;
	MrvlIEtypes_mgmt_ie_passthru_t *tlv_mgmt_ie_passthru = MNULL;
	MrvlIEtypes_2040_coex_enable_t *tlv_2040_coex_enable = MNULL;
	MrvlIEtypes_mac_filter_t *tlv_mac_filter = MNULL;
	MrvlIEtypes_channel_band_t *tlv_chan_band = MNULL;
	MrvlIEtypes_ChanListParamSet_t *tlv_chan_list = MNULL;
	ChanScanParamSet_t *pscan_chan = MNULL;
	MrvlIEtypes_auth_type_t *tlv_auth_type = MNULL;
	MrvlIEtypes_encrypt_protocol_t *tlv_encrypt_protocol = MNULL;
	MrvlIEtypes_akmp_t *tlv_akmp = MNULL;
	MrvlIEtypes_pwk_cipher_t *tlv_pwk_cipher = MNULL;
	MrvlIEtypes_gwk_cipher_t *tlv_gwk_cipher = MNULL;
	MrvlIEtypes_rsn_replay_prot_t *tlv_rsn_prot = MNULL;
	MrvlIEtypes_passphrase_t *tlv_passphrase = MNULL;
	MrvlIEtypes_group_rekey_time_t *tlv_rekey_time = MNULL;
	MrvlIEtypes_wep_key_t *tlv_wep_key = MNULL;
	MrvlIETypes_HTCap_t *tlv_htcap = MNULL;
	MrvlIEtypes_wmm_parameter_t *tlv_wmm_parameter = MNULL;
	t_u32 cmd_size = 0;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };
	t_u16 i;
	t_u16 ac;

	ENTER();
	if (pioctl_buf == MNULL) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	bss = (mlan_ds_bss *)pioctl_buf->pbuf;

	cmd->command = wlan_cpu_to_le16(HOST_CMD_APCMD_SYS_CONFIGURE);
	sys_config->action = wlan_cpu_to_le16(cmd_action);
	cmd_size = sizeof(HostCmd_DS_SYS_CONFIG) - 1 + S_DS_GEN;

	tlv = (t_u8 *)sys_config->tlv_buffer;
	if (memcmp
	    (pmpriv->adapter, zero_mac, &bss->param.bss_config.mac_addr,
	     MLAN_MAC_ADDR_LENGTH)) {
		tlv_mac = (MrvlIEtypes_MacAddr_t *)tlv;
		tlv_mac->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_MAC_ADDRESS);
		tlv_mac->header.len = wlan_cpu_to_le16(MLAN_MAC_ADDR_LENGTH);
		memcpy(pmpriv->adapter, tlv_mac->mac,
		       &bss->param.bss_config.mac_addr, MLAN_MAC_ADDR_LENGTH);
		cmd_size += sizeof(MrvlIEtypes_MacAddr_t);
		tlv += sizeof(MrvlIEtypes_MacAddr_t);
	}

	if (bss->param.bss_config.bandcfg.scanMode == SCAN_MODE_ACS) {
		/* ACS is not allowed when DFS repeater mode is on */
		if (pmpriv->adapter->dfs_repeater) {
			PRINTM(MERROR, "ACS is not allowed when"
			       "DFS repeater mode is on.\n");
			return MLAN_STATUS_FAILURE;
		}
	}

	if (bss->param.bss_config.ssid.ssid_len) {
		tlv_ssid = (MrvlIEtypes_SsIdParamSet_t *)tlv;
		tlv_ssid->header.type = wlan_cpu_to_le16(TLV_TYPE_SSID);
		tlv_ssid->header.len =
			wlan_cpu_to_le16((t_u16)bss->param.bss_config.ssid.
					 ssid_len);
		memcpy(pmpriv->adapter, tlv_ssid->ssid,
		       bss->param.bss_config.ssid.ssid,
		       bss->param.bss_config.ssid.ssid_len);
		cmd_size +=
			sizeof(MrvlIEtypesHeader_t) +
			bss->param.bss_config.ssid.ssid_len;
		tlv += sizeof(MrvlIEtypesHeader_t) +
			bss->param.bss_config.ssid.ssid_len;
	}

	if ((bss->param.bss_config.beacon_period >= MIN_BEACON_PERIOD) &&
	    (bss->param.bss_config.beacon_period <= MAX_BEACON_PERIOD)) {
		tlv_beacon_period = (MrvlIEtypes_beacon_period_t *)tlv;
		tlv_beacon_period->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_BEACON_PERIOD);
		tlv_beacon_period->header.len = wlan_cpu_to_le16(sizeof(t_u16));
		tlv_beacon_period->beacon_period =
			wlan_cpu_to_le16(bss->param.bss_config.beacon_period);
		cmd_size += sizeof(MrvlIEtypes_beacon_period_t);
		tlv += sizeof(MrvlIEtypes_beacon_period_t);
	}

	if ((bss->param.bss_config.dtim_period >= MIN_DTIM_PERIOD) &&
	    (bss->param.bss_config.dtim_period <= MAX_DTIM_PERIOD)) {
		tlv_dtim_period = (MrvlIEtypes_dtim_period_t *)tlv;
		tlv_dtim_period->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_DTIM_PERIOD);
		tlv_dtim_period->header.len = wlan_cpu_to_le16(sizeof(t_u8));
		tlv_dtim_period->dtim_period =
			bss->param.bss_config.dtim_period;
		cmd_size += sizeof(MrvlIEtypes_dtim_period_t);
		tlv += sizeof(MrvlIEtypes_dtim_period_t);
	}

	if (bss->param.bss_config.rates[0]) {
		tlv_rates = (MrvlIEtypes_RatesParamSet_t *)tlv;
		tlv_rates->header.type = wlan_cpu_to_le16(TLV_TYPE_RATES);
		for (i = 0;
		     i < MAX_DATA_RATES && bss->param.bss_config.rates[i];
		     i++) {
			tlv_rates->rates[i] = bss->param.bss_config.rates[i];
		}
		tlv_rates->header.len = wlan_cpu_to_le16(i);
		cmd_size += sizeof(MrvlIEtypesHeader_t) + i;
		tlv += sizeof(MrvlIEtypesHeader_t) + i;
	}

	if (bss->param.bss_config.tx_data_rate <= DATA_RATE_54M) {
		tlv_txrate = (MrvlIEtypes_tx_rate_t *)tlv;
		tlv_txrate->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_TX_DATA_RATE);
		tlv_txrate->header.len = wlan_cpu_to_le16(sizeof(t_u16));
		tlv_txrate->tx_data_rate =
			wlan_cpu_to_le16(bss->param.bss_config.tx_data_rate);
		cmd_size += sizeof(MrvlIEtypes_tx_rate_t);
		tlv += sizeof(MrvlIEtypes_tx_rate_t);
	}

	if (bss->param.bss_config.tx_beacon_rate <= DATA_RATE_54M) {
		tlv_txrate = (MrvlIEtypes_tx_rate_t *)tlv;
		tlv_txrate->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_TX_BEACON_RATE);
		tlv_txrate->header.len = wlan_cpu_to_le16(sizeof(t_u16));
		tlv_txrate->tx_data_rate =
			wlan_cpu_to_le16(bss->param.bss_config.tx_beacon_rate);
		cmd_size += sizeof(MrvlIEtypes_tx_rate_t);
		tlv += sizeof(MrvlIEtypes_tx_rate_t);
	}

	if (bss->param.bss_config.mcbc_data_rate <= DATA_RATE_54M) {
		tlv_mcbc_rate = (MrvlIEtypes_mcbc_rate_t *)tlv;
		tlv_mcbc_rate->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_MCBC_DATA_RATE);
		tlv_mcbc_rate->header.len = wlan_cpu_to_le16(sizeof(t_u16));
		tlv_mcbc_rate->mcbc_data_rate =
			wlan_cpu_to_le16(bss->param.bss_config.mcbc_data_rate);
		cmd_size += sizeof(MrvlIEtypes_mcbc_rate_t);
		tlv += sizeof(MrvlIEtypes_mcbc_rate_t);
	}

	if (bss->param.bss_config.tx_power_level <= MAX_TX_POWER) {
		tlv_tx_power = (MrvlIEtypes_tx_power_t *)tlv;
		tlv_tx_power->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_TX_POWER);
		tlv_tx_power->header.len = wlan_cpu_to_le16(sizeof(t_u8));
		tlv_tx_power->tx_power = bss->param.bss_config.tx_power_level;
		cmd_size += sizeof(MrvlIEtypes_tx_power_t);
		tlv += sizeof(MrvlIEtypes_tx_power_t);
	}

	if (bss->param.bss_config.bcast_ssid_ctl <= MAX_BCAST_SSID_CTL) {
		tlv_bcast_ssid = (MrvlIEtypes_bcast_ssid_t *)tlv;
		tlv_bcast_ssid->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_BCAST_SSID_CTL);
		tlv_bcast_ssid->header.len = wlan_cpu_to_le16(sizeof(t_u8));
		tlv_bcast_ssid->bcast_ssid_ctl =
			bss->param.bss_config.bcast_ssid_ctl;
		cmd_size += sizeof(MrvlIEtypes_bcast_ssid_t);
		tlv += sizeof(MrvlIEtypes_bcast_ssid_t);
	}

	if ((bss->param.bss_config.tx_antenna == ANTENNA_MODE_A) ||
	    (bss->param.bss_config.tx_antenna == ANTENNA_MODE_B)) {
		tlv_antenna = (MrvlIEtypes_antenna_mode_t *)tlv;
		tlv_antenna->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_ANTENNA_CTL);
		tlv_antenna->header.len =
			wlan_cpu_to_le16(sizeof(t_u8) + sizeof(t_u8));
		tlv_antenna->which_antenna = TX_ANTENNA;
		tlv_antenna->antenna_mode = bss->param.bss_config.tx_antenna;
		cmd_size += sizeof(MrvlIEtypes_antenna_mode_t);
		tlv += sizeof(MrvlIEtypes_antenna_mode_t);
	}

	if ((bss->param.bss_config.rx_antenna == ANTENNA_MODE_A) ||
	    (bss->param.bss_config.rx_antenna == ANTENNA_MODE_B)) {
		tlv_antenna = (MrvlIEtypes_antenna_mode_t *)tlv;
		tlv_antenna->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_ANTENNA_CTL);
		tlv_antenna->header.len =
			wlan_cpu_to_le16(sizeof(t_u8) + sizeof(t_u8));
		tlv_antenna->which_antenna = RX_ANTENNA;
		tlv_antenna->antenna_mode = bss->param.bss_config.rx_antenna;
		cmd_size += sizeof(MrvlIEtypes_antenna_mode_t);
		tlv += sizeof(MrvlIEtypes_antenna_mode_t);
	}

	if (bss->param.bss_config.pkt_forward_ctl <= MAX_PKT_FWD_CTRL) {
		tlv_pkt_forward = (MrvlIEtypes_pkt_forward_t *)tlv;
		tlv_pkt_forward->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_PKT_FWD_CTL);
		tlv_pkt_forward->header.len = wlan_cpu_to_le16(sizeof(t_u8));
		tlv_pkt_forward->pkt_forward_ctl =
			bss->param.bss_config.pkt_forward_ctl;
		cmd_size += sizeof(MrvlIEtypes_pkt_forward_t);
		tlv += sizeof(MrvlIEtypes_pkt_forward_t);
	}

	if (bss->param.bss_config.max_sta_count <= MAX_STA_COUNT) {
		tlv_sta_count = (MrvlIEtypes_max_sta_count_t *)tlv;
		tlv_sta_count->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_MAX_STA_CNT);
		tlv_sta_count->header.len = wlan_cpu_to_le16(sizeof(t_u16));
		tlv_sta_count->max_sta_count =
			wlan_cpu_to_le16(bss->param.bss_config.max_sta_count);
		cmd_size += sizeof(MrvlIEtypes_max_sta_count_t);
		tlv += sizeof(MrvlIEtypes_max_sta_count_t);
	}

	if (((bss->param.bss_config.sta_ageout_timer >= MIN_STAGE_OUT_TIME) &&
	     (bss->param.bss_config.sta_ageout_timer <= MAX_STAGE_OUT_TIME)) ||
	    (bss->param.bss_config.sta_ageout_timer == 0)) {
		tlv_sta_ageout = (MrvlIEtypes_sta_ageout_t *)tlv;
		tlv_sta_ageout->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_STA_AGEOUT_TIMER);
		tlv_sta_ageout->header.len = wlan_cpu_to_le16(sizeof(t_u32));
		tlv_sta_ageout->sta_ageout_timer =
			wlan_cpu_to_le32(bss->param.bss_config.
					 sta_ageout_timer);
		cmd_size += sizeof(MrvlIEtypes_sta_ageout_t);
		tlv += sizeof(MrvlIEtypes_sta_ageout_t);
	}

	if (((bss->param.bss_config.ps_sta_ageout_timer >= MIN_STAGE_OUT_TIME)
	     && (bss->param.bss_config.ps_sta_ageout_timer <=
		 MAX_STAGE_OUT_TIME)) ||
	    (bss->param.bss_config.ps_sta_ageout_timer == 0)) {
		tlv_ps_sta_ageout = (MrvlIEtypes_ps_sta_ageout_t *)tlv;
		tlv_ps_sta_ageout->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_PS_STA_AGEOUT_TIMER);
		tlv_ps_sta_ageout->header.len = wlan_cpu_to_le16(sizeof(t_u32));
		tlv_ps_sta_ageout->ps_sta_ageout_timer =
			wlan_cpu_to_le32(bss->param.bss_config.
					 ps_sta_ageout_timer);
		cmd_size += sizeof(MrvlIEtypes_ps_sta_ageout_t);
		tlv += sizeof(MrvlIEtypes_ps_sta_ageout_t);
	}
	if (bss->param.bss_config.rts_threshold <= MAX_RTS_THRESHOLD) {
		tlv_rts_threshold = (MrvlIEtypes_rts_threshold_t *)tlv;
		tlv_rts_threshold->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_RTS_THRESHOLD);
		tlv_rts_threshold->header.len = wlan_cpu_to_le16(sizeof(t_u16));
		tlv_rts_threshold->rts_threshold =
			wlan_cpu_to_le16(bss->param.bss_config.rts_threshold);
		cmd_size += sizeof(MrvlIEtypes_rts_threshold_t);
		tlv += sizeof(MrvlIEtypes_rts_threshold_t);
	}

	if ((bss->param.bss_config.frag_threshold >= MIN_FRAG_THRESHOLD) &&
	    (bss->param.bss_config.frag_threshold <= MAX_FRAG_THRESHOLD)) {
		tlv_frag_threshold = (MrvlIEtypes_frag_threshold_t *)tlv;
		tlv_frag_threshold->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_FRAG_THRESHOLD);
		tlv_frag_threshold->header.len =
			wlan_cpu_to_le16(sizeof(t_u16));
		tlv_frag_threshold->frag_threshold =
			wlan_cpu_to_le16(bss->param.bss_config.frag_threshold);
		cmd_size += sizeof(MrvlIEtypes_frag_threshold_t);
		tlv += sizeof(MrvlIEtypes_frag_threshold_t);
	}

	if (bss->param.bss_config.retry_limit <= MAX_RETRY_LIMIT) {
		tlv_retry_limit = (MrvlIEtypes_retry_limit_t *)tlv;
		tlv_retry_limit->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_RETRY_LIMIT);
		tlv_retry_limit->header.len = wlan_cpu_to_le16(sizeof(t_u8));
		tlv_retry_limit->retry_limit =
			(t_u8)bss->param.bss_config.retry_limit;
		cmd_size += sizeof(MrvlIEtypes_retry_limit_t);
		tlv += sizeof(MrvlIEtypes_retry_limit_t);
	}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	if (IS_FW_SUPPORT_AUTHENTICATOR(pmpriv->adapter)) {
#endif
		if (bss->param.bss_config.pairwise_update_timeout <
		    (MAX_VALID_DWORD)) {
			tlv_pairwise_timeout =
				(MrvlIEtypes_eapol_pwk_hsk_timeout_t *)tlv;
			tlv_pairwise_timeout->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_UAP_EAPOL_PWK_HSK_TIMEOUT);
			tlv_pairwise_timeout->header.len =
				wlan_cpu_to_le16(sizeof(t_u32));
			tlv_pairwise_timeout->pairwise_update_timeout =
				wlan_cpu_to_le32(bss->param.bss_config.
						 pairwise_update_timeout);
			cmd_size += sizeof(MrvlIEtypes_eapol_pwk_hsk_timeout_t);
			tlv += sizeof(MrvlIEtypes_eapol_pwk_hsk_timeout_t);
		}

		if (bss->param.bss_config.pwk_retries < (MAX_VALID_DWORD)) {
			tlv_pairwise_retries =
				(MrvlIEtypes_eapol_pwk_hsk_retries_t *)tlv;
			tlv_pairwise_retries->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_UAP_EAPOL_PWK_HSK_RETRIES);
			tlv_pairwise_retries->header.len =
				wlan_cpu_to_le16(sizeof(t_u32));
			tlv_pairwise_retries->pwk_retries =
				wlan_cpu_to_le32(bss->param.bss_config.
						 pwk_retries);
			cmd_size += sizeof(MrvlIEtypes_eapol_pwk_hsk_retries_t);
			tlv += sizeof(MrvlIEtypes_eapol_pwk_hsk_retries_t);
		}

		if (bss->param.bss_config.groupwise_update_timeout <
		    (MAX_VALID_DWORD)) {
			tlv_groupwise_timeout =
				(MrvlIEtypes_eapol_gwk_hsk_timeout_t *)tlv;
			tlv_groupwise_timeout->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_UAP_EAPOL_GWK_HSK_TIMEOUT);
			tlv_groupwise_timeout->header.len =
				wlan_cpu_to_le16(sizeof(t_u32));
			tlv_groupwise_timeout->groupwise_update_timeout =
				wlan_cpu_to_le32(bss->param.bss_config.
						 groupwise_update_timeout);
			cmd_size += sizeof(MrvlIEtypes_eapol_gwk_hsk_timeout_t);
			tlv += sizeof(MrvlIEtypes_eapol_gwk_hsk_timeout_t);
		}

		if (bss->param.bss_config.gwk_retries < (MAX_VALID_DWORD)) {
			tlv_groupwise_retries =
				(MrvlIEtypes_eapol_gwk_hsk_retries_t *)tlv;
			tlv_groupwise_retries->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_UAP_EAPOL_GWK_HSK_RETRIES);
			tlv_groupwise_retries->header.len =
				wlan_cpu_to_le16(sizeof(t_u32));
			tlv_groupwise_retries->gwk_retries =
				wlan_cpu_to_le32(bss->param.bss_config.
						 gwk_retries);
			cmd_size += sizeof(MrvlIEtypes_eapol_gwk_hsk_retries_t);
			tlv += sizeof(MrvlIEtypes_eapol_gwk_hsk_retries_t);
		}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	}
#endif
	if ((bss->param.bss_config.filter.filter_mode <=
	     MAC_FILTER_MODE_BLOCK_MAC)
	    && (bss->param.bss_config.filter.mac_count <= MAX_MAC_FILTER_NUM)) {
		tlv_mac_filter = (MrvlIEtypes_mac_filter_t *)tlv;
		tlv_mac_filter->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_STA_MAC_ADDR_FILTER);
		tlv_mac_filter->header.len =
			wlan_cpu_to_le16(2 +
					 MLAN_MAC_ADDR_LENGTH *
					 bss->param.bss_config.filter.
					 mac_count);
		tlv_mac_filter->count =
			(t_u8)bss->param.bss_config.filter.mac_count;
		tlv_mac_filter->filter_mode =
			(t_u8)bss->param.bss_config.filter.filter_mode;
		memcpy(pmpriv->adapter, tlv_mac_filter->mac_address,
		       (t_u8 *)bss->param.bss_config.filter.mac_list,
		       MLAN_MAC_ADDR_LENGTH *
		       bss->param.bss_config.filter.mac_count);
		cmd_size +=
			sizeof(MrvlIEtypesHeader_t) + 2 +
			MLAN_MAC_ADDR_LENGTH *
			bss->param.bss_config.filter.mac_count;
		tlv += sizeof(MrvlIEtypesHeader_t) + 2 +
			MLAN_MAC_ADDR_LENGTH *
			bss->param.bss_config.filter.mac_count;
	}

	if (((bss->param.bss_config.bandcfg.scanMode == SCAN_MODE_MANUAL) &&
	     (bss->param.bss_config.channel > 0) &&
	     (bss->param.bss_config.channel <= MLAN_MAX_CHANNEL)) ||
	    (bss->param.bss_config.bandcfg.scanMode == SCAN_MODE_ACS)) {
		tlv_chan_band = (MrvlIEtypes_channel_band_t *)tlv;
		tlv_chan_band->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_CHAN_BAND_CONFIG);
		tlv_chan_band->header.len =
			wlan_cpu_to_le16(sizeof(t_u8) + sizeof(t_u8));
		tlv_chan_band->bandcfg = bss->param.bss_config.bandcfg;
		tlv_chan_band->channel = bss->param.bss_config.channel;
		cmd_size += sizeof(MrvlIEtypes_channel_band_t);
		tlv += sizeof(MrvlIEtypes_channel_band_t);
	}

	if ((bss->param.bss_config.num_of_chan) &&
	    (bss->param.bss_config.num_of_chan <= MLAN_MAX_CHANNEL)) {
		tlv_chan_list = (MrvlIEtypes_ChanListParamSet_t *)tlv;
		tlv_chan_list->header.type =
			wlan_cpu_to_le16(TLV_TYPE_CHANLIST);
		tlv_chan_list->header.len =
			wlan_cpu_to_le16((t_u16)
					 (sizeof(ChanScanParamSet_t) *
					  bss->param.bss_config.num_of_chan));
		pscan_chan = tlv_chan_list->chan_scan_param;
		for (i = 0; i < bss->param.bss_config.num_of_chan; i++) {
			pscan_chan->chan_number =
				bss->param.bss_config.chan_list[i].chan_number;
			pscan_chan->bandcfg =
				bss->param.bss_config.chan_list[i].bandcfg;
			pscan_chan++;
		}
		cmd_size += sizeof(tlv_chan_list->header) +
			(sizeof(ChanScanParamSet_t) *
			 bss->param.bss_config.num_of_chan);
		tlv += sizeof(tlv_chan_list->header) +
			(sizeof(ChanScanParamSet_t) *
			 bss->param.bss_config.num_of_chan);
	}

	if ((bss->param.bss_config.auth_mode <= MLAN_AUTH_MODE_SHARED) ||
	    (bss->param.bss_config.auth_mode == MLAN_AUTH_MODE_AUTO)) {
		tlv_auth_type = (MrvlIEtypes_auth_type_t *)tlv;
		tlv_auth_type->header.type =
			wlan_cpu_to_le16(TLV_TYPE_AUTH_TYPE);
		tlv_auth_type->header.len = wlan_cpu_to_le16(sizeof(t_u8));
		tlv_auth_type->auth_type =
			(t_u8)bss->param.bss_config.auth_mode;
		cmd_size += sizeof(MrvlIEtypes_auth_type_t);
		tlv += sizeof(MrvlIEtypes_auth_type_t);
	}

	if (bss->param.bss_config.protocol) {
		tlv_encrypt_protocol = (MrvlIEtypes_encrypt_protocol_t *)tlv;
		tlv_encrypt_protocol->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_ENCRYPT_PROTOCOL);
		tlv_encrypt_protocol->header.len =
			wlan_cpu_to_le16(sizeof(t_u16));
		tlv_encrypt_protocol->protocol =
			wlan_cpu_to_le16(bss->param.bss_config.protocol);
		cmd_size += sizeof(MrvlIEtypes_encrypt_protocol_t);
		tlv += sizeof(MrvlIEtypes_encrypt_protocol_t);
	}

	if ((bss->param.bss_config.protocol & PROTOCOL_WPA) ||
	    (bss->param.bss_config.protocol & PROTOCOL_WPA2) ||
	    (bss->param.bss_config.protocol & PROTOCOL_EAP)) {
		tlv_akmp = (MrvlIEtypes_akmp_t *)tlv;
		tlv_akmp->header.type = wlan_cpu_to_le16(TLV_TYPE_UAP_AKMP);
		tlv_akmp->key_mgmt =
			wlan_cpu_to_le16(bss->param.bss_config.key_mgmt);
		tlv_akmp->header.len = sizeof(t_u16);
		tlv_akmp->key_mgmt_operation =
			wlan_cpu_to_le16(bss->param.bss_config.
					 key_mgmt_operation);
		tlv_akmp->header.len += sizeof(t_u16);
		tlv_akmp->header.len = wlan_cpu_to_le16(tlv_akmp->header.len);
		cmd_size += sizeof(MrvlIEtypes_akmp_t);
		tlv += sizeof(MrvlIEtypes_akmp_t);

		if (bss->param.bss_config.wpa_cfg.
		    pairwise_cipher_wpa & VALID_CIPHER_BITMAP) {
			tlv_pwk_cipher = (MrvlIEtypes_pwk_cipher_t *)tlv;
			tlv_pwk_cipher->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PWK_CIPHER);
			tlv_pwk_cipher->header.len =
				wlan_cpu_to_le16(sizeof(t_u16) + sizeof(t_u8) +
						 sizeof(t_u8));
			tlv_pwk_cipher->protocol =
				wlan_cpu_to_le16(PROTOCOL_WPA);
			tlv_pwk_cipher->pairwise_cipher =
				bss->param.bss_config.wpa_cfg.
				pairwise_cipher_wpa;
			cmd_size += sizeof(MrvlIEtypes_pwk_cipher_t);
			tlv += sizeof(MrvlIEtypes_pwk_cipher_t);
		}

		if (bss->param.bss_config.wpa_cfg.
		    pairwise_cipher_wpa2 & VALID_CIPHER_BITMAP) {
			tlv_pwk_cipher = (MrvlIEtypes_pwk_cipher_t *)tlv;
			tlv_pwk_cipher->header.type =
				wlan_cpu_to_le16(TLV_TYPE_PWK_CIPHER);
			tlv_pwk_cipher->header.len =
				wlan_cpu_to_le16(sizeof(t_u16) + sizeof(t_u8) +
						 sizeof(t_u8));
			tlv_pwk_cipher->protocol =
				wlan_cpu_to_le16(PROTOCOL_WPA2);
			tlv_pwk_cipher->pairwise_cipher =
				bss->param.bss_config.wpa_cfg.
				pairwise_cipher_wpa2;
			cmd_size += sizeof(MrvlIEtypes_pwk_cipher_t);
			tlv += sizeof(MrvlIEtypes_pwk_cipher_t);
		}

		if (bss->param.bss_config.wpa_cfg.
		    group_cipher & VALID_CIPHER_BITMAP) {
			tlv_gwk_cipher = (MrvlIEtypes_gwk_cipher_t *)tlv;
			tlv_gwk_cipher->header.type =
				wlan_cpu_to_le16(TLV_TYPE_GWK_CIPHER);
			tlv_gwk_cipher->header.len =
				wlan_cpu_to_le16(sizeof(t_u8) + sizeof(t_u8));
			tlv_gwk_cipher->group_cipher =
				bss->param.bss_config.wpa_cfg.group_cipher;
			cmd_size += sizeof(MrvlIEtypes_gwk_cipher_t);
			tlv += sizeof(MrvlIEtypes_gwk_cipher_t);
		}

		if (bss->param.bss_config.wpa_cfg.rsn_protection <= MTRUE) {
			tlv_rsn_prot = (MrvlIEtypes_rsn_replay_prot_t *)tlv;
			tlv_rsn_prot->header.type =
				wlan_cpu_to_le16
				(TLV_TYPE_UAP_RSN_REPLAY_PROTECT);
			tlv_rsn_prot->header.len =
				wlan_cpu_to_le16(sizeof(t_u8));
			tlv_rsn_prot->rsn_replay_prot =
				bss->param.bss_config.wpa_cfg.rsn_protection;
			cmd_size += sizeof(MrvlIEtypes_rsn_replay_prot_t);
			tlv += sizeof(MrvlIEtypes_rsn_replay_prot_t);
		}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		if (IS_FW_SUPPORT_AUTHENTICATOR(pmpriv->adapter)) {
#endif
			if (bss->param.bss_config.wpa_cfg.length) {
				tlv_passphrase =
					(MrvlIEtypes_passphrase_t *)tlv;
				tlv_passphrase->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_UAP_WPA_PASSPHRASE);
				tlv_passphrase->header.len =
					(t_u16)wlan_cpu_to_le16(bss->param.
								bss_config.
								wpa_cfg.length);
				memcpy(pmpriv->adapter,
				       tlv_passphrase->passphrase,
				       bss->param.bss_config.wpa_cfg.passphrase,
				       bss->param.bss_config.wpa_cfg.length);
				cmd_size +=
					sizeof(MrvlIEtypesHeader_t) +
					bss->param.bss_config.wpa_cfg.length;
				tlv += sizeof(MrvlIEtypesHeader_t) +
					bss->param.bss_config.wpa_cfg.length;
			}

			if (bss->param.bss_config.wpa_cfg.gk_rekey_time <
			    MAX_GRP_TIMER) {
				tlv_rekey_time =
					(MrvlIEtypes_group_rekey_time_t *)tlv;
				tlv_rekey_time->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_UAP_GRP_REKEY_TIME);
				tlv_rekey_time->header.len =
					wlan_cpu_to_le16(sizeof(t_u32));
				tlv_rekey_time->gk_rekey_time =
					wlan_cpu_to_le32(bss->param.bss_config.
							 wpa_cfg.gk_rekey_time);
				cmd_size +=
					sizeof(MrvlIEtypes_group_rekey_time_t);
				tlv += sizeof(MrvlIEtypes_group_rekey_time_t);
			}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		}
#endif
	} else {
		if ((bss->param.bss_config.wep_cfg.key0.length) &&
		    ((bss->param.bss_config.wep_cfg.key0.length == 5) ||
		     (bss->param.bss_config.wep_cfg.key0.length == 10) ||
		     (bss->param.bss_config.wep_cfg.key0.length == 13) ||
		     (bss->param.bss_config.wep_cfg.key0.length == 26))) {
			tlv_wep_key = (MrvlIEtypes_wep_key_t *)tlv;
			tlv_wep_key->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_WEP_KEY);
			tlv_wep_key->header.len =
				wlan_cpu_to_le16(2 +
						 bss->param.bss_config.wep_cfg.
						 key0.length);
			tlv_wep_key->key_index =
				bss->param.bss_config.wep_cfg.key0.key_index;
			tlv_wep_key->is_default =
				bss->param.bss_config.wep_cfg.key0.is_default;
			memcpy(pmpriv->adapter, tlv_wep_key->key,
			       bss->param.bss_config.wep_cfg.key0.key,
			       bss->param.bss_config.wep_cfg.key0.length);
			cmd_size +=
				sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key0.length;
			tlv += sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key0.length;
		}

		if ((bss->param.bss_config.wep_cfg.key1.length) &&
		    ((bss->param.bss_config.wep_cfg.key1.length == 5) ||
		     (bss->param.bss_config.wep_cfg.key1.length == 10) ||
		     (bss->param.bss_config.wep_cfg.key1.length == 13) ||
		     (bss->param.bss_config.wep_cfg.key1.length == 26))) {
			tlv_wep_key = (MrvlIEtypes_wep_key_t *)tlv;
			tlv_wep_key->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_WEP_KEY);
			tlv_wep_key->header.len =
				wlan_cpu_to_le16(2 +
						 bss->param.bss_config.wep_cfg.
						 key1.length);
			tlv_wep_key->key_index =
				bss->param.bss_config.wep_cfg.key1.key_index;
			tlv_wep_key->is_default =
				bss->param.bss_config.wep_cfg.key1.is_default;
			memcpy(pmpriv->adapter, tlv_wep_key->key,
			       bss->param.bss_config.wep_cfg.key1.key,
			       bss->param.bss_config.wep_cfg.key1.length);
			cmd_size +=
				sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key1.length;
			tlv += sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key1.length;
		}

		if ((bss->param.bss_config.wep_cfg.key2.length) &&
		    ((bss->param.bss_config.wep_cfg.key2.length == 5) ||
		     (bss->param.bss_config.wep_cfg.key2.length == 10) ||
		     (bss->param.bss_config.wep_cfg.key2.length == 13) ||
		     (bss->param.bss_config.wep_cfg.key2.length == 26))) {
			tlv_wep_key = (MrvlIEtypes_wep_key_t *)tlv;
			tlv_wep_key->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_WEP_KEY);
			tlv_wep_key->header.len =
				wlan_cpu_to_le16(2 +
						 bss->param.bss_config.wep_cfg.
						 key2.length);
			tlv_wep_key->key_index =
				bss->param.bss_config.wep_cfg.key2.key_index;
			tlv_wep_key->is_default =
				bss->param.bss_config.wep_cfg.key2.is_default;
			memcpy(pmpriv->adapter, tlv_wep_key->key,
			       bss->param.bss_config.wep_cfg.key2.key,
			       bss->param.bss_config.wep_cfg.key2.length);
			cmd_size +=
				sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key2.length;
			tlv += sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key2.length;
		}

		if ((bss->param.bss_config.wep_cfg.key3.length) &&
		    ((bss->param.bss_config.wep_cfg.key3.length == 5) ||
		     (bss->param.bss_config.wep_cfg.key3.length == 10) ||
		     (bss->param.bss_config.wep_cfg.key3.length == 13) ||
		     (bss->param.bss_config.wep_cfg.key3.length == 26))) {
			tlv_wep_key = (MrvlIEtypes_wep_key_t *)tlv;
			tlv_wep_key->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_WEP_KEY);
			tlv_wep_key->header.len =
				wlan_cpu_to_le16(2 +
						 bss->param.bss_config.wep_cfg.
						 key3.length);
			tlv_wep_key->key_index =
				bss->param.bss_config.wep_cfg.key3.key_index;
			tlv_wep_key->is_default =
				bss->param.bss_config.wep_cfg.key3.is_default;
			memcpy(pmpriv->adapter, tlv_wep_key->key,
			       bss->param.bss_config.wep_cfg.key3.key,
			       bss->param.bss_config.wep_cfg.key3.length);
			cmd_size +=
				sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key3.length;
			tlv += sizeof(MrvlIEtypesHeader_t) + 2 +
				bss->param.bss_config.wep_cfg.key3.length;
		}
	}
	if ((bss->param.bss_config.ht_cap_info)
		) {
		tlv_htcap = (MrvlIETypes_HTCap_t *)tlv;
		tlv_htcap->header.type = wlan_cpu_to_le16(HT_CAPABILITY);
		tlv_htcap->header.len = wlan_cpu_to_le16(sizeof(HTCap_t));
		tlv_htcap->ht_cap.ht_cap_info =
			wlan_cpu_to_le16(bss->param.bss_config.ht_cap_info);
		tlv_htcap->ht_cap.ampdu_param =
			bss->param.bss_config.ampdu_param;
		memcpy(pmpriv->adapter, tlv_htcap->ht_cap.supported_mcs_set,
		       bss->param.bss_config.supported_mcs_set, 16);
		tlv_htcap->ht_cap.ht_ext_cap =
			wlan_cpu_to_le16(bss->param.bss_config.ht_ext_cap);
		tlv_htcap->ht_cap.tx_bf_cap =
			wlan_cpu_to_le32(bss->param.bss_config.tx_bf_cap);
		tlv_htcap->ht_cap.asel = bss->param.bss_config.asel;
		cmd_size += sizeof(MrvlIETypes_HTCap_t);
		tlv += sizeof(MrvlIETypes_HTCap_t);
	}
	if (bss->param.bss_config.mgmt_ie_passthru_mask < (MAX_VALID_DWORD)) {
		tlv_mgmt_ie_passthru = (MrvlIEtypes_mgmt_ie_passthru_t *)tlv;
		tlv_mgmt_ie_passthru->header.type =
			wlan_cpu_to_le16(TLV_TYPE_UAP_MGMT_IE_PASSTHRU_MASK);
		tlv_mgmt_ie_passthru->header.len =
			wlan_cpu_to_le16(sizeof(t_u32));
		/* keep copy in private data */
		pmpriv->mgmt_frame_passthru_mask =
			bss->param.bss_config.mgmt_ie_passthru_mask;
		tlv_mgmt_ie_passthru->mgmt_ie_mask =
			wlan_cpu_to_le32(bss->param.bss_config.
					 mgmt_ie_passthru_mask);
		cmd_size += sizeof(MrvlIEtypes_mgmt_ie_passthru_t);
		tlv += sizeof(MrvlIEtypes_mgmt_ie_passthru_t);
	}
	if (((bss->param.bss_config.enable_2040coex == 0) ||
	     (bss->param.bss_config.enable_2040coex == 1))
		) {
		tlv_2040_coex_enable = (MrvlIEtypes_2040_coex_enable_t *)tlv;
		tlv_2040_coex_enable->header.type =
			wlan_cpu_to_le16(TLV_TYPE_2040_BSS_COEX_CONTROL);
		tlv_2040_coex_enable->header.len =
			wlan_cpu_to_le16(sizeof(t_u8));
		tlv_2040_coex_enable->enable_2040coex =
			bss->param.bss_config.enable_2040coex;
		cmd_size += sizeof(MrvlIEtypes_2040_coex_enable_t);
		tlv += sizeof(MrvlIEtypes_2040_coex_enable_t);
	}
	if (bss->param.bss_config.wmm_para.qos_info == 0x80 ||
	    bss->param.bss_config.wmm_para.qos_info == 0x00) {
		tlv_wmm_parameter = (MrvlIEtypes_wmm_parameter_t *)tlv;
		tlv_wmm_parameter->header.type =
			wlan_cpu_to_le16(TLV_TYPE_VENDOR_SPECIFIC_IE);
		tlv_wmm_parameter->header.len =
			wlan_cpu_to_le16(sizeof
					 (bss->param.bss_config.wmm_para));
		memcpy(pmpriv->adapter, tlv_wmm_parameter->wmm_para.ouitype,
		       bss->param.bss_config.wmm_para.ouitype,
		       sizeof(tlv_wmm_parameter->wmm_para.ouitype));
		tlv_wmm_parameter->wmm_para.ouisubtype =
			bss->param.bss_config.wmm_para.ouisubtype;
		tlv_wmm_parameter->wmm_para.version =
			bss->param.bss_config.wmm_para.version;
		tlv_wmm_parameter->wmm_para.qos_info =
			bss->param.bss_config.wmm_para.qos_info;
		for (ac = 0; ac < 4; ac++) {
			tlv_wmm_parameter->wmm_para.ac_params[ac].aci_aifsn.
				aifsn =
				bss->param.bss_config.wmm_para.ac_params[ac].
				aci_aifsn.aifsn;
			tlv_wmm_parameter->wmm_para.ac_params[ac].aci_aifsn.
				aci =
				bss->param.bss_config.wmm_para.ac_params[ac].
				aci_aifsn.aci;
			tlv_wmm_parameter->wmm_para.ac_params[ac].ecw.ecw_max =
				bss->param.bss_config.wmm_para.ac_params[ac].
				ecw.ecw_max;
			tlv_wmm_parameter->wmm_para.ac_params[ac].ecw.ecw_min =
				bss->param.bss_config.wmm_para.ac_params[ac].
				ecw.ecw_min;
			tlv_wmm_parameter->wmm_para.ac_params[ac].tx_op_limit =
				wlan_cpu_to_le16(bss->param.bss_config.wmm_para.
						 ac_params[ac].tx_op_limit);
		}
		cmd_size += sizeof(MrvlIEtypes_wmm_parameter_t);
		tlv += sizeof(MrvlIEtypes_wmm_parameter_t);
	}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	if (!IS_FW_SUPPORT_AUTHENTICATOR(pmpriv->adapter))
		AuthenticatorBssConfig(pmpriv->psapriv,
				       (t_u8 *)&bss->param.bss_config, 0, 0, 0);
#endif
	cmd->size = (t_u16)wlan_cpu_to_le16(cmd_size);
	PRINTM(MCMND, "AP config: cmd_size=%d\n", cmd_size);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of sys_config
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_uap_cmd_sys_configure(pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *cmd,
			   IN t_u16 cmd_action,
			   IN pmlan_ioctl_req pioctl_buf, IN t_void *pdata_buf)
{
	mlan_ds_bss *bss = MNULL;
	HostCmd_DS_SYS_CONFIG *sys_config =
		(HostCmd_DS_SYS_CONFIG *)&cmd->params.sys_config;
	MrvlIEtypes_MacAddr_t *mac_tlv = MNULL;
	MrvlIEtypes_channel_band_t *pdat_tlv_cb = MNULL;
	MrvlIEtypes_beacon_period_t *bcn_pd_tlv = MNULL, *pdat_tlv_bcnpd =
		MNULL;
	MrvlIEtypes_dtim_period_t *dtim_pd_tlv = MNULL, *pdat_tlv_dtimpd =
		MNULL;
	MrvlIEtypes_wmm_parameter_t *tlv_wmm_parameter = MNULL;
	MrvlIEtypes_ChanListParamSet_t *tlv_chan_list = MNULL;
	ChanScanParamSet_t *pscan_chan = MNULL;
	MrvlIEtypes_channel_band_t *chan_band_tlv = MNULL;
	MrvlIEtypes_chan_bw_oper_t *poper_class_tlv = MNULL;
	t_u8 length = 0;
	t_u8 curr_oper_class = 1;
	t_u8 *oper_class_ie = (t_u8 *)sys_config->tlv_buffer;
	t_u16 i = 0;
	t_u8 ac = 0;
	mlan_ds_misc_custom_ie *cust_ie = MNULL;
	mlan_ds_misc_cfg *misc = MNULL;
	MrvlIEtypesHeader_t *ie_header =
		(MrvlIEtypesHeader_t *)sys_config->tlv_buffer;
	MrvlIEtypesHeader_t *pdata_header = (MrvlIEtypesHeader_t *)pdata_buf;
	t_u8 *ie = (t_u8 *)sys_config->tlv_buffer + sizeof(MrvlIEtypesHeader_t);
	t_u16 req_len = 0, travel_len = 0;
	custom_ie *cptr = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HOST_CMD_APCMD_SYS_CONFIGURE);
	sys_config->action = wlan_cpu_to_le16(cmd_action);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) - 1 + S_DS_GEN);
	if (pioctl_buf == MNULL) {

		if (pdata_buf) {
			switch (pdata_header->type) {
			case TLV_TYPE_UAP_CHAN_BAND_CONFIG:
				pdat_tlv_cb =
					(MrvlIEtypes_channel_band_t *)pdata_buf;
				chan_band_tlv =
					(MrvlIEtypes_channel_band_t *)
					sys_config->tlv_buffer;
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_SYS_CONFIG)
							 - 1 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypes_channel_band_t));
				chan_band_tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_UAP_CHAN_BAND_CONFIG);
				chan_band_tlv->header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_channel_band_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				if (cmd_action) {
					chan_band_tlv->bandcfg =
						pdat_tlv_cb->bandcfg;
					chan_band_tlv->channel =
						pdat_tlv_cb->channel;
				}
				ret = MLAN_STATUS_SUCCESS;
				break;
			case TLV_TYPE_UAP_BEACON_PERIOD:
				pdat_tlv_bcnpd =
					(MrvlIEtypes_beacon_period_t *)
					pdata_buf;
				bcn_pd_tlv =
					(MrvlIEtypes_beacon_period_t *)
					sys_config->tlv_buffer;
				cmd->size =
					sizeof(HostCmd_DS_SYS_CONFIG) - 1 +
					S_DS_GEN +
					sizeof(MrvlIEtypes_beacon_period_t);
				bcn_pd_tlv->header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_UAP_BEACON_PERIOD);
				bcn_pd_tlv->header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_beacon_period_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				if (cmd_action) {
					bcn_pd_tlv->beacon_period =
						wlan_cpu_to_le16
						(pdat_tlv_bcnpd->beacon_period);
				}
				/* Add TLV_UAP_DTIM_PERIOD if it follws in pdata_buf */
				pdat_tlv_dtimpd =
					(MrvlIEtypes_dtim_period_t
					 *)(((t_u8 *)pdata_buf)
					    +
					    sizeof
					    (MrvlIEtypes_beacon_period_t));
				if (TLV_TYPE_UAP_DTIM_PERIOD ==
				    pdat_tlv_dtimpd->header.type) {
					dtim_pd_tlv =
						(MrvlIEtypes_dtim_period_t
						 *)(sys_config->tlv_buffer +
						    sizeof
						    (MrvlIEtypes_beacon_period_t));
					cmd->size +=
						sizeof
						(MrvlIEtypes_dtim_period_t);
					dtim_pd_tlv->header.type =
						wlan_cpu_to_le16
						(TLV_TYPE_UAP_DTIM_PERIOD);
					dtim_pd_tlv->header.len =
						wlan_cpu_to_le16(sizeof
								 (MrvlIEtypes_dtim_period_t)
								 -
								 sizeof
								 (MrvlIEtypesHeader_t));
					if (cmd_action) {
						dtim_pd_tlv->dtim_period =
							pdat_tlv_dtimpd->
							dtim_period;
					}
				}
				/* Finalize cmd size */
				cmd->size = wlan_cpu_to_le16(cmd->size);
				ret = MLAN_STATUS_SUCCESS;
				break;
			case TLV_TYPE_MGMT_IE:
				cust_ie = (mlan_ds_misc_custom_ie *)pdata_buf;
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_SYS_CONFIG)
							 - 1 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypesHeader_t) +
							 cust_ie->len);
				ie_header->type =
					wlan_cpu_to_le16(TLV_TYPE_MGMT_IE);
				ie_header->len = wlan_cpu_to_le16(cust_ie->len);

				if (ie && cust_ie->ie_data_list) {
					req_len = cust_ie->len;
					travel_len = 0;
					/* conversion for index, mask, len */
					if (req_len == sizeof(t_u16))
						cust_ie->ie_data_list[0].
							ie_index =
							wlan_cpu_to_le16
							(cust_ie->
							 ie_data_list[0].
							 ie_index);
					while (req_len > sizeof(t_u16)) {
						cptr = (custom_ie
							*)(((t_u8 *)&cust_ie->
							    ie_data_list) +
							   travel_len);
						travel_len +=
							cptr->ie_length +
							sizeof(custom_ie) -
							MAX_IE_SIZE;
						req_len -=
							cptr->ie_length +
							sizeof(custom_ie) -
							MAX_IE_SIZE;
						cptr->ie_index =
							wlan_cpu_to_le16(cptr->
									 ie_index);
						cptr->mgmt_subtype_mask =
							wlan_cpu_to_le16(cptr->
									 mgmt_subtype_mask);
						cptr->ie_length =
							wlan_cpu_to_le16(cptr->
									 ie_length);
					}
					memcpy(pmpriv->adapter, ie,
					       cust_ie->ie_data_list,
					       cust_ie->len);
				}
				break;
			case REGULATORY_CLASS:
				poper_class_tlv =
					(MrvlIEtypes_chan_bw_oper_t *)
					pdata_buf;
				ret = wlan_get_curr_oper_class(pmpriv,
							       poper_class_tlv->
							       ds_chan_bw_oper.
							       channel,
							       poper_class_tlv->
							       ds_chan_bw_oper.
							       bandwidth,
							       &curr_oper_class);
				if (ret != MLAN_STATUS_SUCCESS) {
					PRINTM(MERROR,
					       "Can not get current oper class! bandwidth = %d, channel = %d\n",
					       poper_class_tlv->ds_chan_bw_oper.
					       bandwidth,
					       poper_class_tlv->ds_chan_bw_oper.
					       channel);
				}

				if (cmd_action == HostCmd_ACT_GEN_SET)
					length = wlan_add_supported_oper_class_ie(pmpriv, &oper_class_ie, curr_oper_class);
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_SYS_CONFIG)
							 - 1 + S_DS_GEN +
							 length);
				break;

			default:
				PRINTM(MERROR,
				       "Wrong data, or missing TLV_TYPE 0x%04x handler.\n",
				       *(t_u16 *)pdata_buf);
				break;
			}
			goto done;
		} else {
			mac_tlv =
				(MrvlIEtypes_MacAddr_t *)sys_config->tlv_buffer;
			cmd->size =
				wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) -
						 1 + S_DS_GEN +
						 sizeof(MrvlIEtypes_MacAddr_t));
			mac_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_MAC_ADDRESS);
			mac_tlv->header.len =
				wlan_cpu_to_le16(MLAN_MAC_ADDR_LENGTH);
			ret = MLAN_STATUS_SUCCESS;
			goto done;
		}
	}
	if (pioctl_buf->req_id == MLAN_IOCTL_BSS) {
		bss = (mlan_ds_bss *)pioctl_buf->pbuf;
		if (bss->sub_command == MLAN_OID_BSS_MAC_ADDR) {
			mac_tlv =
				(MrvlIEtypes_MacAddr_t *)sys_config->tlv_buffer;
			cmd->size =
				wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) -
						 1 + S_DS_GEN +
						 sizeof(MrvlIEtypes_MacAddr_t));
			mac_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_MAC_ADDRESS);
			mac_tlv->header.len =
				wlan_cpu_to_le16(MLAN_MAC_ADDR_LENGTH);
			if (cmd_action == HostCmd_ACT_GEN_SET)
				memcpy(pmpriv->adapter, mac_tlv->mac,
				       &bss->param.mac_addr,
				       MLAN_MAC_ADDR_LENGTH);
		} else if (bss->sub_command == MLAN_OID_UAP_CFG_WMM_PARAM) {
			tlv_wmm_parameter =
				(MrvlIEtypes_wmm_parameter_t *)sys_config->
				tlv_buffer;
			cmd->size =
				wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) -
						 1 + S_DS_GEN +
						 sizeof
						 (MrvlIEtypes_wmm_parameter_t));
			tlv_wmm_parameter->header.type =
				wlan_cpu_to_le16(TLV_TYPE_AP_WMM_PARAM);
			tlv_wmm_parameter->header.len =
				wlan_cpu_to_le16(sizeof
						 (bss->param.ap_wmm_para));
			if (cmd_action == HostCmd_ACT_GEN_SET) {
				for (ac = 0; ac < 4; ac++) {
					tlv_wmm_parameter->wmm_para.
						ac_params[ac].aci_aifsn.aifsn =
						bss->param.ap_wmm_para.
						ac_params[ac].aci_aifsn.aifsn;
					tlv_wmm_parameter->wmm_para.
						ac_params[ac].aci_aifsn.aci =
						bss->param.ap_wmm_para.
						ac_params[ac].aci_aifsn.aci;
					tlv_wmm_parameter->wmm_para.
						ac_params[ac].ecw.ecw_max =
						bss->param.ap_wmm_para.
						ac_params[ac].ecw.ecw_max;
					tlv_wmm_parameter->wmm_para.
						ac_params[ac].ecw.ecw_min =
						bss->param.ap_wmm_para.
						ac_params[ac].ecw.ecw_min;
					tlv_wmm_parameter->wmm_para.
						ac_params[ac].tx_op_limit =
						wlan_cpu_to_le16(bss->param.
								 ap_wmm_para.
								 ac_params[ac].
								 tx_op_limit);
				}
			}
		} else if (bss->sub_command == MLAN_OID_UAP_SCAN_CHANNELS) {
			tlv_chan_list =
				(MrvlIEtypes_ChanListParamSet_t *)sys_config->
				tlv_buffer;
			tlv_chan_list->header.type =
				wlan_cpu_to_le16(TLV_TYPE_CHANLIST);
			if (bss->param.ap_scan_channels.num_of_chan &&
			    bss->param.ap_scan_channels.num_of_chan <=
			    MLAN_MAX_CHANNEL) {
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_SYS_CONFIG)
							 - 1 + S_DS_GEN +
							 sizeof(tlv_chan_list->
								header) +
							 sizeof
							 (ChanScanParamSet_t) *
							 bss->param.
							 ap_scan_channels.
							 num_of_chan);
				tlv_chan_list->header.len =
					wlan_cpu_to_le16((t_u16)
							 (sizeof
							  (ChanScanParamSet_t) *
							  bss->param.
							  ap_scan_channels.
							  num_of_chan));
				pscan_chan = tlv_chan_list->chan_scan_param;
				for (i = 0;
				     i <
				     bss->param.ap_scan_channels.num_of_chan;
				     i++) {
					pscan_chan->chan_number =
						bss->param.ap_scan_channels.
						chan_list[i].chan_number;
					pscan_chan->bandcfg =
						bss->param.ap_scan_channels.
						chan_list[i].bandcfg;
					pscan_chan++;
				}
				PRINTM(MCMND,
				       "Set AP scan channel list =  %d\n",
				       bss->param.ap_scan_channels.num_of_chan);
			} else {
				tlv_chan_list->header.len =
					wlan_cpu_to_le16((t_u16)
							 (sizeof
							  (ChanScanParamSet_t) *
							  MLAN_MAX_CHANNEL));
				cmd->size =
					wlan_cpu_to_le16(sizeof
							 (HostCmd_DS_SYS_CONFIG)
							 - 1 + S_DS_GEN +
							 sizeof
							 (MrvlIEtypes_ChanListParamSet_t)
							 +
							 sizeof
							 (ChanScanParamSet_t) *
							 MLAN_MAX_CHANNEL);
			}
		} else if (bss->sub_command == MLAN_OID_UAP_CHANNEL) {
			chan_band_tlv =
				(MrvlIEtypes_channel_band_t *)sys_config->
				tlv_buffer;
			cmd->size =
				wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) -
						 1 + S_DS_GEN +
						 sizeof
						 (MrvlIEtypes_channel_band_t));
			chan_band_tlv->header.type =
				wlan_cpu_to_le16(TLV_TYPE_UAP_CHAN_BAND_CONFIG);
			chan_band_tlv->header.len =
				wlan_cpu_to_le16(sizeof
						 (MrvlIEtypes_channel_band_t)
						 - sizeof(MrvlIEtypesHeader_t));
			if (cmd_action == HostCmd_ACT_GEN_SET) {
				chan_band_tlv->bandcfg =
					bss->param.ap_channel.bandcfg;
				chan_band_tlv->channel =
					bss->param.ap_channel.channel;
				PRINTM(MCMND,
				       "Set AP channel, band=%d, channel=%d\n",
				       bss->param.ap_channel.bandcfg.chanBand,
				       bss->param.ap_channel.channel);
			}
		} else if ((bss->sub_command == MLAN_OID_UAP_BSS_CONFIG) &&
			   (cmd_action == HostCmd_ACT_GEN_SET)) {
			ret = wlan_uap_cmd_ap_config(pmpriv, cmd, cmd_action,
						     pioctl_buf);
			goto done;
		}
	} else if (pioctl_buf->req_id == MLAN_IOCTL_MISC_CFG) {
		misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
		if ((misc->sub_command == MLAN_OID_MISC_GEN_IE) &&
		    (misc->param.gen_ie.type == MLAN_IE_TYPE_GEN_IE)
			) {
			cmd->size =
				wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) -
						 1 + S_DS_GEN +
						 sizeof(MrvlIEtypesHeader_t) +
						 misc->param.gen_ie.len);
			ie_header->type = wlan_cpu_to_le16(TLV_TYPE_WAPI_IE);
			ie_header->len =
				wlan_cpu_to_le16(misc->param.gen_ie.len);
			if (cmd_action == HostCmd_ACT_GEN_SET)
				memcpy(pmpriv->adapter, ie,
				       misc->param.gen_ie.ie_data,
				       misc->param.gen_ie.len);
		}
		if ((misc->sub_command == MLAN_OID_MISC_CUSTOM_IE) &&
		    (misc->param.cust_ie.type == TLV_TYPE_MGMT_IE)) {
			cmd->size =
				wlan_cpu_to_le16(sizeof(HostCmd_DS_SYS_CONFIG) -
						 1 + S_DS_GEN +
						 sizeof(MrvlIEtypesHeader_t) +
						 misc->param.cust_ie.len);
			ie_header->type = wlan_cpu_to_le16(TLV_TYPE_MGMT_IE);
			ie_header->len =
				wlan_cpu_to_le16(misc->param.cust_ie.len);

			if (ie && misc->param.cust_ie.ie_data_list) {
				req_len = misc->param.cust_ie.len;
				travel_len = 0;
				/* conversion for index, mask, len */
				if (req_len == sizeof(t_u16))
					misc->param.cust_ie.ie_data_list[0].
						ie_index =
						wlan_cpu_to_le16(misc->param.
								 cust_ie.
								 ie_data_list
								 [0].ie_index);
				while (req_len > sizeof(t_u16)) {
					cptr = (custom_ie
						*)(((t_u8 *)&misc->param.
						    cust_ie.ie_data_list) +
						   travel_len);
					travel_len +=
						cptr->ie_length +
						sizeof(custom_ie) - MAX_IE_SIZE;
					req_len -=
						cptr->ie_length +
						sizeof(custom_ie) - MAX_IE_SIZE;
					cptr->ie_index =
						wlan_cpu_to_le16(cptr->
								 ie_index);
					cptr->mgmt_subtype_mask =
						wlan_cpu_to_le16(cptr->
								 mgmt_subtype_mask);
					cptr->ie_length =
						wlan_cpu_to_le16(cptr->
								 ie_length);
				}
				if (misc->param.cust_ie.len)
					memcpy(pmpriv->adapter, ie,
					       misc->param.cust_ie.ie_data_list,
					       misc->param.cust_ie.len);
			}
		}
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles command resp for get uap settings
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_cmd_ap_config(IN pmlan_private pmpriv,
			   IN HostCmd_DS_COMMAND *resp,
			   IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_SYS_CONFIG *sys_config =
		(HostCmd_DS_SYS_CONFIG *)&resp->params.sys_config;
	mlan_ds_bss *bss = MNULL;
	MrvlIEtypesHeader_t *tlv = MNULL;
	t_u16 tlv_buf_left = 0;
	t_u16 tlv_type = 0;
	t_u16 tlv_len = 0;
	MrvlIEtypes_MacAddr_t *tlv_mac = MNULL;
	MrvlIEtypes_SsIdParamSet_t *tlv_ssid = MNULL;
	MrvlIEtypes_beacon_period_t *tlv_beacon_period = MNULL;
	MrvlIEtypes_dtim_period_t *tlv_dtim_period = MNULL;
	MrvlIEtypes_RatesParamSet_t *tlv_rates = MNULL;
	MrvlIEtypes_tx_rate_t *tlv_txrate = MNULL;
	MrvlIEtypes_mcbc_rate_t *tlv_mcbc_rate = MNULL;
	MrvlIEtypes_tx_power_t *tlv_tx_power = MNULL;
	MrvlIEtypes_bcast_ssid_t *tlv_bcast_ssid = MNULL;
	MrvlIEtypes_antenna_mode_t *tlv_antenna = MNULL;
	MrvlIEtypes_pkt_forward_t *tlv_pkt_forward = MNULL;
	MrvlIEtypes_max_sta_count_t *tlv_sta_count = MNULL;
	MrvlIEtypes_sta_ageout_t *tlv_sta_ageout = MNULL;
	MrvlIEtypes_ps_sta_ageout_t *tlv_ps_sta_ageout = MNULL;
	MrvlIEtypes_rts_threshold_t *tlv_rts_threshold = MNULL;
	MrvlIEtypes_frag_threshold_t *tlv_frag_threshold = MNULL;
	MrvlIEtypes_retry_limit_t *tlv_retry_limit = MNULL;
	MrvlIEtypes_eapol_pwk_hsk_timeout_t *tlv_pairwise_timeout = MNULL;
	MrvlIEtypes_eapol_pwk_hsk_retries_t *tlv_pairwise_retries = MNULL;
	MrvlIEtypes_eapol_gwk_hsk_timeout_t *tlv_groupwise_timeout = MNULL;
	MrvlIEtypes_eapol_gwk_hsk_retries_t *tlv_groupwise_retries = MNULL;
	MrvlIEtypes_mgmt_ie_passthru_t *tlv_mgmt_ie_passthru = MNULL;
	MrvlIEtypes_2040_coex_enable_t *tlv_2040_coex_enable = MNULL;
	MrvlIEtypes_mac_filter_t *tlv_mac_filter = MNULL;
	MrvlIEtypes_channel_band_t *tlv_chan_band = MNULL;
	MrvlIEtypes_ChanListParamSet_t *tlv_chan_list = MNULL;
	ChanScanParamSet_t *pscan_chan = MNULL;
	MrvlIEtypes_auth_type_t *tlv_auth_type = MNULL;
	MrvlIEtypes_encrypt_protocol_t *tlv_encrypt_protocol = MNULL;
	MrvlIEtypes_akmp_t *tlv_akmp = MNULL;
	MrvlIEtypes_pwk_cipher_t *tlv_pwk_cipher = MNULL;
	MrvlIEtypes_gwk_cipher_t *tlv_gwk_cipher = MNULL;
	MrvlIEtypes_rsn_replay_prot_t *tlv_rsn_prot = MNULL;
	MrvlIEtypes_passphrase_t *tlv_passphrase = MNULL;
#ifdef WIFI_DIRECT_SUPPORT
	MrvlIEtypes_psk_t *tlv_psk = MNULL;
#endif /* WIFI_DIRECT_SUPPORT */
	MrvlIEtypes_group_rekey_time_t *tlv_rekey_time = MNULL;
	MrvlIEtypes_wep_key_t *tlv_wep_key = MNULL;
	MrvlIEtypes_preamble_t *tlv_preamble = MNULL;
	MrvlIEtypes_bss_status_t *tlv_bss_status = MNULL;
	MrvlIETypes_HTCap_t *tlv_htcap = MNULL;
	MrvlIEtypes_wmm_parameter_t *tlv_wmm_parameter = MNULL;

	wep_key *pkey = MNULL;
	t_u16 i;
	t_u16 ac;

	ENTER();

	bss = (mlan_ds_bss *)pioctl_buf->pbuf;
	tlv = (MrvlIEtypesHeader_t *)sys_config->tlv_buffer;
	tlv_buf_left =
		resp->size - (sizeof(HostCmd_DS_SYS_CONFIG) - 1 + S_DS_GEN);

	while (tlv_buf_left >= sizeof(MrvlIEtypesHeader_t)) {

		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);

		if (tlv_buf_left < (tlv_len + sizeof(MrvlIEtypesHeader_t))) {
			PRINTM(MERROR,
			       "Error processing uAP sys config TLVs, bytes left < TLV length\n");
			break;
		}

		switch (tlv_type) {
		case TLV_TYPE_UAP_MAC_ADDRESS:
			tlv_mac = (MrvlIEtypes_MacAddr_t *)tlv;
			memcpy(pmpriv->adapter, &bss->param.bss_config.mac_addr,
			       tlv_mac->mac, MLAN_MAC_ADDR_LENGTH);
			break;
		case TLV_TYPE_SSID:
			tlv_ssid = (MrvlIEtypes_SsIdParamSet_t *)tlv;
			bss->param.bss_config.ssid.ssid_len =
				MIN(MLAN_MAX_SSID_LENGTH, tlv_len);
			memcpy(pmpriv->adapter, bss->param.bss_config.ssid.ssid,
			       tlv_ssid->ssid, MIN(MLAN_MAX_SSID_LENGTH,
						   tlv_len));
			break;
		case TLV_TYPE_UAP_BEACON_PERIOD:
			tlv_beacon_period = (MrvlIEtypes_beacon_period_t *)tlv;
			bss->param.bss_config.beacon_period =
				wlan_le16_to_cpu(tlv_beacon_period->
						 beacon_period);
			pmpriv->uap_state_chan_cb.beacon_period =
				wlan_le16_to_cpu(tlv_beacon_period->
						 beacon_period);
			break;
		case TLV_TYPE_UAP_DTIM_PERIOD:
			tlv_dtim_period = (MrvlIEtypes_dtim_period_t *)tlv;
			bss->param.bss_config.dtim_period =
				tlv_dtim_period->dtim_period;
			pmpriv->uap_state_chan_cb.dtim_period =
				tlv_dtim_period->dtim_period;
			break;
		case TLV_TYPE_RATES:
			tlv_rates = (MrvlIEtypes_RatesParamSet_t *)tlv;
			memcpy(pmpriv->adapter, bss->param.bss_config.rates,
			       tlv_rates->rates, MIN(MAX_DATA_RATES, tlv_len));
			break;
		case TLV_TYPE_UAP_TX_DATA_RATE:
			tlv_txrate = (MrvlIEtypes_tx_rate_t *)tlv;
			bss->param.bss_config.tx_data_rate =
				wlan_le16_to_cpu(tlv_txrate->tx_data_rate);
			break;
		case TLV_TYPE_UAP_TX_BEACON_RATE:
			tlv_txrate = (MrvlIEtypes_tx_rate_t *)tlv;
			bss->param.bss_config.tx_beacon_rate =
				wlan_le16_to_cpu(tlv_txrate->tx_data_rate);
			break;
		case TLV_TYPE_UAP_MCBC_DATA_RATE:
			tlv_mcbc_rate = (MrvlIEtypes_mcbc_rate_t *)tlv;
			bss->param.bss_config.mcbc_data_rate =
				wlan_le16_to_cpu(tlv_mcbc_rate->mcbc_data_rate);
			break;
		case TLV_TYPE_UAP_TX_POWER:
			tlv_tx_power = (MrvlIEtypes_tx_power_t *)tlv;
			bss->param.bss_config.tx_power_level =
				tlv_tx_power->tx_power;
			break;
		case TLV_TYPE_UAP_BCAST_SSID_CTL:
			tlv_bcast_ssid = (MrvlIEtypes_bcast_ssid_t *)tlv;
			bss->param.bss_config.bcast_ssid_ctl =
				tlv_bcast_ssid->bcast_ssid_ctl;
			break;
		case TLV_TYPE_UAP_ANTENNA_CTL:
			tlv_antenna = (MrvlIEtypes_antenna_mode_t *)tlv;
			if (tlv_antenna->which_antenna == TX_ANTENNA)
				bss->param.bss_config.tx_antenna =
					tlv_antenna->antenna_mode;
			else if (tlv_antenna->which_antenna == RX_ANTENNA)
				bss->param.bss_config.rx_antenna =
					tlv_antenna->antenna_mode;
			break;
		case TLV_TYPE_UAP_PKT_FWD_CTL:
			tlv_pkt_forward = (MrvlIEtypes_pkt_forward_t *)tlv;
			bss->param.bss_config.pkt_forward_ctl =
				tlv_pkt_forward->pkt_forward_ctl;
			break;
		case TLV_TYPE_UAP_MAX_STA_CNT:
			tlv_sta_count = (MrvlIEtypes_max_sta_count_t *)tlv;
			bss->param.bss_config.max_sta_count =
				wlan_le16_to_cpu(tlv_sta_count->max_sta_count);
			break;
		case TLV_TYPE_UAP_STA_AGEOUT_TIMER:
			tlv_sta_ageout = (MrvlIEtypes_sta_ageout_t *)tlv;
			bss->param.bss_config.sta_ageout_timer =
				wlan_le32_to_cpu(tlv_sta_ageout->
						 sta_ageout_timer);
			break;
		case TLV_TYPE_UAP_PS_STA_AGEOUT_TIMER:
			tlv_ps_sta_ageout = (MrvlIEtypes_ps_sta_ageout_t *)tlv;
			bss->param.bss_config.ps_sta_ageout_timer =
				wlan_le32_to_cpu(tlv_ps_sta_ageout->
						 ps_sta_ageout_timer);
			break;
		case TLV_TYPE_UAP_RTS_THRESHOLD:
			tlv_rts_threshold = (MrvlIEtypes_rts_threshold_t *)tlv;
			bss->param.bss_config.rts_threshold =
				wlan_le16_to_cpu(tlv_rts_threshold->
						 rts_threshold);
			break;
		case TLV_TYPE_UAP_FRAG_THRESHOLD:
			tlv_frag_threshold =
				(MrvlIEtypes_frag_threshold_t *)tlv;
			bss->param.bss_config.frag_threshold =
				wlan_le16_to_cpu(tlv_frag_threshold->
						 frag_threshold);
			break;
		case TLV_TYPE_UAP_RETRY_LIMIT:
			tlv_retry_limit = (MrvlIEtypes_retry_limit_t *)tlv;
			bss->param.bss_config.retry_limit =
				tlv_retry_limit->retry_limit;
			break;
		case TLV_TYPE_UAP_EAPOL_PWK_HSK_TIMEOUT:
			tlv_pairwise_timeout =
				(MrvlIEtypes_eapol_pwk_hsk_timeout_t *)tlv;
			bss->param.bss_config.pairwise_update_timeout =
				wlan_le32_to_cpu(tlv_pairwise_timeout->
						 pairwise_update_timeout);
			break;
		case TLV_TYPE_UAP_EAPOL_PWK_HSK_RETRIES:
			tlv_pairwise_retries =
				(MrvlIEtypes_eapol_pwk_hsk_retries_t *)tlv;
			bss->param.bss_config.pwk_retries =
				wlan_le32_to_cpu(tlv_pairwise_retries->
						 pwk_retries);
			break;
		case TLV_TYPE_UAP_EAPOL_GWK_HSK_TIMEOUT:
			tlv_groupwise_timeout =
				(MrvlIEtypes_eapol_gwk_hsk_timeout_t *)tlv;
			bss->param.bss_config.groupwise_update_timeout =
				wlan_le32_to_cpu(tlv_groupwise_timeout->
						 groupwise_update_timeout);
			break;
		case TLV_TYPE_UAP_EAPOL_GWK_HSK_RETRIES:
			tlv_groupwise_retries =
				(MrvlIEtypes_eapol_gwk_hsk_retries_t *)tlv;
			bss->param.bss_config.gwk_retries =
				wlan_le32_to_cpu(tlv_groupwise_retries->
						 gwk_retries);
			break;
		case TLV_TYPE_UAP_MGMT_IE_PASSTHRU_MASK:
			tlv_mgmt_ie_passthru =
				(MrvlIEtypes_mgmt_ie_passthru_t *)tlv;
			bss->param.bss_config.mgmt_ie_passthru_mask =
				wlan_le32_to_cpu(tlv_mgmt_ie_passthru->
						 mgmt_ie_mask);
			break;
		case TLV_TYPE_2040_BSS_COEX_CONTROL:
			tlv_2040_coex_enable =
				(MrvlIEtypes_2040_coex_enable_t *)tlv;
			bss->param.bss_config.enable_2040coex =
				tlv_2040_coex_enable->enable_2040coex;
			break;
		case TLV_TYPE_UAP_STA_MAC_ADDR_FILTER:
			tlv_mac_filter = (MrvlIEtypes_mac_filter_t *)tlv;
			bss->param.bss_config.filter.mac_count =
				MIN(MAX_MAC_FILTER_NUM, tlv_mac_filter->count);
			bss->param.bss_config.filter.filter_mode =
				tlv_mac_filter->filter_mode;
			memcpy(pmpriv->adapter,
			       (t_u8 *)bss->param.bss_config.filter.mac_list,
			       tlv_mac_filter->mac_address,
			       MLAN_MAC_ADDR_LENGTH *
			       bss->param.bss_config.filter.mac_count);
			break;
		case TLV_TYPE_UAP_CHAN_BAND_CONFIG:
			tlv_chan_band = (MrvlIEtypes_channel_band_t *)tlv;
			bss->param.bss_config.bandcfg = tlv_chan_band->bandcfg;
			bss->param.bss_config.channel = tlv_chan_band->channel;
			pmpriv->uap_state_chan_cb.bandcfg =
				tlv_chan_band->bandcfg;
			pmpriv->uap_state_chan_cb.channel =
				tlv_chan_band->channel;
			break;
		case TLV_TYPE_CHANLIST:
			tlv_chan_list = (MrvlIEtypes_ChanListParamSet_t *)tlv;
			bss->param.bss_config.num_of_chan =
				tlv_len / sizeof(ChanScanParamSet_t);
			pscan_chan = tlv_chan_list->chan_scan_param;
			for (i = 0; i < bss->param.bss_config.num_of_chan; i++) {
				bss->param.bss_config.chan_list[i].chan_number =
					pscan_chan->chan_number;
				bss->param.bss_config.chan_list[i].bandcfg =
					pscan_chan->bandcfg;
				pscan_chan++;
			}
			break;
		case TLV_TYPE_AUTH_TYPE:
			tlv_auth_type = (MrvlIEtypes_auth_type_t *)tlv;
			bss->param.bss_config.auth_mode =
				tlv_auth_type->auth_type;
			break;
		case TLV_TYPE_UAP_ENCRYPT_PROTOCOL:
			tlv_encrypt_protocol =
				(MrvlIEtypes_encrypt_protocol_t *)tlv;
			bss->param.bss_config.protocol =
				wlan_le16_to_cpu(tlv_encrypt_protocol->
						 protocol);
			break;
		case TLV_TYPE_UAP_AKMP:
			tlv_akmp = (MrvlIEtypes_akmp_t *)tlv;
			bss->param.bss_config.key_mgmt =
				wlan_le16_to_cpu(tlv_akmp->key_mgmt);
			if (tlv_len > sizeof(t_u16))
				bss->param.bss_config.key_mgmt_operation =
					wlan_le16_to_cpu(tlv_akmp->
							 key_mgmt_operation);
			break;
		case TLV_TYPE_PWK_CIPHER:
			tlv_pwk_cipher = (MrvlIEtypes_pwk_cipher_t *)tlv;
			if (wlan_le16_to_cpu(tlv_pwk_cipher->protocol) &
			    PROTOCOL_WPA)
				bss->param.bss_config.wpa_cfg.
					pairwise_cipher_wpa =
					tlv_pwk_cipher->pairwise_cipher;
			if (wlan_le16_to_cpu(tlv_pwk_cipher->protocol) &
			    PROTOCOL_WPA2)
				bss->param.bss_config.wpa_cfg.
					pairwise_cipher_wpa2 =
					tlv_pwk_cipher->pairwise_cipher;
			break;
		case TLV_TYPE_GWK_CIPHER:
			tlv_gwk_cipher = (MrvlIEtypes_gwk_cipher_t *)tlv;
			bss->param.bss_config.wpa_cfg.group_cipher =
				tlv_gwk_cipher->group_cipher;
			break;
		case TLV_TYPE_UAP_RSN_REPLAY_PROTECT:
			tlv_rsn_prot = (MrvlIEtypes_rsn_replay_prot_t *)tlv;
			bss->param.bss_config.wpa_cfg.rsn_protection =
				tlv_rsn_prot->rsn_replay_prot;
			break;
		case TLV_TYPE_UAP_WPA_PASSPHRASE:
			tlv_passphrase = (MrvlIEtypes_passphrase_t *)tlv;
			bss->param.bss_config.wpa_cfg.length =
				MIN(MLAN_PMK_HEXSTR_LENGTH, tlv_len);
			memcpy(pmpriv->adapter,
			       bss->param.bss_config.wpa_cfg.passphrase,
			       tlv_passphrase->passphrase,
			       bss->param.bss_config.wpa_cfg.length);
			break;
#ifdef WIFI_DIRECT_SUPPORT
		case TLV_TYPE_UAP_PSK:
			tlv_psk = (MrvlIEtypes_psk_t *)tlv;
			memcpy(pmpriv->adapter, bss->param.bss_config.psk,
			       tlv_psk->psk, MIN(MLAN_MAX_KEY_LENGTH, tlv_len));
			break;
#endif /* WIFI_DIRECT_SUPPORT */
		case TLV_TYPE_UAP_GRP_REKEY_TIME:
			tlv_rekey_time = (MrvlIEtypes_group_rekey_time_t *)tlv;
			bss->param.bss_config.wpa_cfg.gk_rekey_time =
				wlan_le32_to_cpu(tlv_rekey_time->gk_rekey_time);
			break;
		case TLV_TYPE_UAP_WEP_KEY:
			tlv_wep_key = (MrvlIEtypes_wep_key_t *)tlv;
			pkey = MNULL;
			if (tlv_wep_key->key_index == 0)
				pkey = &bss->param.bss_config.wep_cfg.key0;
			else if (tlv_wep_key->key_index == 1)
				pkey = &bss->param.bss_config.wep_cfg.key1;
			else if (tlv_wep_key->key_index == 2)
				pkey = &bss->param.bss_config.wep_cfg.key2;
			else if (tlv_wep_key->key_index == 3)
				pkey = &bss->param.bss_config.wep_cfg.key3;
			if (pkey) {
				pkey->key_index = tlv_wep_key->key_index;
				pkey->is_default = tlv_wep_key->is_default;
				pkey->length =
					MIN(MAX_WEP_KEY_SIZE, (tlv_len - 2));
				memcpy(pmpriv->adapter, pkey->key,
				       tlv_wep_key->key, pkey->length);
			}
			break;
		case TLV_TYPE_UAP_PREAMBLE_CTL:
			tlv_preamble = (MrvlIEtypes_preamble_t *)tlv;
			bss->param.bss_config.preamble_type =
				tlv_preamble->preamble_type;
			break;
		case TLV_TYPE_BSS_STATUS:
			tlv_bss_status = (MrvlIEtypes_bss_status_t *)tlv;
			bss->param.bss_config.bss_status =
				wlan_le16_to_cpu(tlv_bss_status->bss_status);
			pmpriv->uap_bss_started =
				(bss->param.bss_config.
				 bss_status) ? MTRUE : MFALSE;
			break;
		case TLV_TYPE_HT_CAPABILITY:
			tlv_htcap = (MrvlIETypes_HTCap_t *)tlv;
			bss->param.bss_config.ht_cap_info =
				wlan_le16_to_cpu(tlv_htcap->ht_cap.ht_cap_info);
			bss->param.bss_config.ampdu_param =
				tlv_htcap->ht_cap.ampdu_param;
			memcpy(pmpriv->adapter,
			       bss->param.bss_config.supported_mcs_set,
			       tlv_htcap->ht_cap.supported_mcs_set, 16);
			bss->param.bss_config.ht_ext_cap =
				wlan_le16_to_cpu(tlv_htcap->ht_cap.ht_ext_cap);
			bss->param.bss_config.tx_bf_cap =
				wlan_le32_to_cpu(tlv_htcap->ht_cap.tx_bf_cap);
			bss->param.bss_config.asel = tlv_htcap->ht_cap.asel;
			break;
		case TLV_TYPE_VENDOR_SPECIFIC_IE:
			tlv_wmm_parameter = (MrvlIEtypes_wmm_parameter_t *)tlv;
			bss->param.bss_config.wmm_para.qos_info =
				tlv_wmm_parameter->wmm_para.qos_info;
			for (ac = 0; ac < 4; ac++) {
				bss->param.bss_config.wmm_para.ac_params[ac].
					aci_aifsn.aifsn =
					tlv_wmm_parameter->wmm_para.
					ac_params[ac].aci_aifsn.aifsn;
				bss->param.bss_config.wmm_para.ac_params[ac].
					aci_aifsn.aci =
					tlv_wmm_parameter->wmm_para.
					ac_params[ac].aci_aifsn.aci;
				bss->param.bss_config.wmm_para.ac_params[ac].
					ecw.ecw_max =
					tlv_wmm_parameter->wmm_para.
					ac_params[ac].ecw.ecw_max;
				bss->param.bss_config.wmm_para.ac_params[ac].
					ecw.ecw_min =
					tlv_wmm_parameter->wmm_para.
					ac_params[ac].ecw.ecw_min;
				bss->param.bss_config.wmm_para.ac_params[ac].
					tx_op_limit =
					wlan_le16_to_cpu(tlv_wmm_parameter->
							 wmm_para.ac_params[ac].
							 tx_op_limit);
			}
			break;
		}

		tlv_buf_left -= tlv_len + sizeof(MrvlIEtypesHeader_t);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	if (!IS_FW_SUPPORT_AUTHENTICATOR(pmpriv->adapter))
		AuthenticatorBssConfig(pmpriv->psapriv,
				       (t_u8 *)&bss->param.bss_config, 0, 0, 1);
#endif
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of sys_reset
 *         Clear various private state variables used by DFS.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_sys_reset(IN pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *resp,
		       IN mlan_ioctl_req *pioctl_buf)
{
	ENTER();

	memset(pmpriv->adapter, &(pmpriv->uap_state_chan_cb.bandcfg), 0,
	       sizeof(pmpriv->uap_state_chan_cb.bandcfg));
	pmpriv->uap_state_chan_cb.channel = 0;
	pmpriv->uap_state_chan_cb.beacon_period = 0;
	pmpriv->uap_state_chan_cb.dtim_period = 0;

	/* assume default 11d/11h states are off, should check with FW */
	/* currently don't clear domain_info... global, could be from STA */
	wlan_11d_priv_init(pmpriv);
	wlan_11h_priv_init(pmpriv);

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of sys_config
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_sys_config(IN pmlan_private pmpriv,
			IN HostCmd_DS_COMMAND *resp,
			IN mlan_ioctl_req *pioctl_buf)
{
	int resp_len = 0, travel_len = 0;
	int i = 0;
	custom_ie *cptr;
	HostCmd_DS_SYS_CONFIG *sys_config =
		(HostCmd_DS_SYS_CONFIG *)&resp->params.sys_config;
	mlan_ds_bss *bss = MNULL;
	mlan_ds_misc_cfg *misc = MNULL;
	MrvlIEtypes_MacAddr_t *tlv =
		(MrvlIEtypes_MacAddr_t *)sys_config->tlv_buffer;
	mlan_ds_misc_custom_ie *cust_ie = MNULL;
	tlvbuf_max_mgmt_ie *max_mgmt_ie = MNULL;
	MrvlIEtypes_wmm_parameter_t *tlv_wmm_parameter =
		(MrvlIEtypes_wmm_parameter_t *)sys_config->tlv_buffer;
	MrvlIEtypes_ChanListParamSet_t *tlv_chan_list =
		(MrvlIEtypes_ChanListParamSet_t *)sys_config->tlv_buffer;
	MrvlIEtypes_channel_band_t *chan_band_tlv =
		(MrvlIEtypes_channel_band_t *)sys_config->tlv_buffer;
	ChanScanParamSet_t *pscan_chan = MNULL;
	t_u8 ac = 0;
	MrvlIEtypes_channel_band_t *tlv_cb = MNULL;
	MrvlIEtypes_beacon_period_t *tlv_bcnpd = MNULL;
	MrvlIEtypes_dtim_period_t *tlv_dtimpd = MNULL;

	ENTER();
	if (pioctl_buf) {
		if (pioctl_buf->req_id == MLAN_IOCTL_BSS) {
			bss = (mlan_ds_bss *)pioctl_buf->pbuf;
			if (bss->sub_command == MLAN_OID_BSS_MAC_ADDR) {
				if (TLV_TYPE_UAP_MAC_ADDRESS ==
				    wlan_le16_to_cpu(tlv->header.type)) {
					memcpy(pmpriv->adapter,
					       &bss->param.mac_addr, tlv->mac,
					       MLAN_MAC_ADDR_LENGTH);
				}
			} else if (bss->sub_command ==
				   MLAN_OID_UAP_CFG_WMM_PARAM) {
				if (TLV_TYPE_AP_WMM_PARAM ==
				    wlan_le16_to_cpu(tlv_wmm_parameter->header.
						     type)) {
					if (wlan_le16_to_cpu
					    (tlv_wmm_parameter->header.len) <
					    sizeof(bss->param.ap_wmm_para)) {
						PRINTM(MCMND,
						       "FW don't support AP WMM PARAM\n");
					} else {
						bss->param.ap_wmm_para.
							reserved =
							MLAN_STATUS_COMPLETE;
						for (ac = 0; ac < 4; ac++) {
							bss->param.ap_wmm_para.
								ac_params[ac].
								aci_aifsn.
								aifsn =
								tlv_wmm_parameter->
								wmm_para.
								ac_params[ac].
								aci_aifsn.aifsn;
							bss->param.ap_wmm_para.
								ac_params[ac].
								aci_aifsn.aci =
								tlv_wmm_parameter->
								wmm_para.
								ac_params[ac].
								aci_aifsn.aci;
							bss->param.ap_wmm_para.
								ac_params[ac].
								ecw.ecw_max =
								tlv_wmm_parameter->
								wmm_para.
								ac_params[ac].
								ecw.ecw_max;
							bss->param.ap_wmm_para.
								ac_params[ac].
								ecw.ecw_min =
								tlv_wmm_parameter->
								wmm_para.
								ac_params[ac].
								ecw.ecw_min;
							bss->param.ap_wmm_para.
								ac_params[ac].
								tx_op_limit =
								wlan_le16_to_cpu
								(tlv_wmm_parameter->
								 wmm_para.
								 ac_params[ac].
								 tx_op_limit);
							PRINTM(MCMND,
							       "ac=%d, aifsn=%d, aci=%d, ecw_max=%d, ecw_min=%d, tx_op=%d\n",
							       ac,
							       bss->param.
							       ap_wmm_para.
							       ac_params[ac].
							       aci_aifsn.aifsn,
							       bss->param.
							       ap_wmm_para.
							       ac_params[ac].
							       aci_aifsn.aci,
							       bss->param.
							       ap_wmm_para.
							       ac_params[ac].
							       ecw.ecw_max,
							       bss->param.
							       ap_wmm_para.
							       ac_params[ac].
							       ecw.ecw_min,
							       bss->param.
							       ap_wmm_para.
							       ac_params[ac].
							       tx_op_limit);
						}
					}
				}
			} else if (bss->sub_command ==
				   MLAN_OID_UAP_SCAN_CHANNELS) {
				if (TLV_TYPE_CHANLIST ==
				    wlan_le16_to_cpu(tlv_chan_list->header.
						     type)) {
					pscan_chan =
						tlv_chan_list->chan_scan_param;
					bss->param.ap_scan_channels.
						num_of_chan = 0;
					for (i = 0;
					     i <
					     wlan_le16_to_cpu(tlv_chan_list->
							      header.len) /
					     sizeof(ChanScanParamSet_t); i++) {
						if (bss->param.ap_scan_channels.
						    remove_nop_channel &&
						    wlan_11h_is_channel_under_nop
						    (pmpriv->adapter,
						     pscan_chan->chan_number)) {
							bss->param.
								ap_scan_channels.
								num_remvoed_channel++;
							PRINTM(MCMND,
							       "Remove nop channel=%d\n",
							       pscan_chan->
							       chan_number);
							pscan_chan++;
							continue;
						}
						bss->param.ap_scan_channels.
							chan_list[bss->param.
								  ap_scan_channels.
								  num_of_chan].
							chan_number =
							pscan_chan->chan_number;
						bss->param.ap_scan_channels.
							chan_list[bss->param.
								  ap_scan_channels.
								  num_of_chan].
							bandcfg =
							pscan_chan->bandcfg;
						bss->param.ap_scan_channels.
							num_of_chan++;
						pscan_chan++;
					}
					PRINTM(MCMND,
					       "AP scan channel list=%d\n",
					       bss->param.ap_scan_channels.
					       num_of_chan);
				}
			} else if (bss->sub_command == MLAN_OID_UAP_CHANNEL) {
				if (TLV_TYPE_UAP_CHAN_BAND_CONFIG ==
				    wlan_le16_to_cpu(chan_band_tlv->header.
						     type)) {
					bss->param.ap_channel.bandcfg =
						chan_band_tlv->bandcfg;
					bss->param.ap_channel.channel =
						chan_band_tlv->channel;
					bss->param.ap_channel.is_11n_enabled =
						pmpriv->is_11n_enabled;
					PRINTM(MCMND,
					       "AP channel, band=0x%x, channel=%d, is_11n_enabled=%d center_chan=%d\n",
					       bss->param.ap_channel.bandcfg.
					       chanBand,
					       bss->param.ap_channel.channel,
					       bss->param.ap_channel.
					       is_11n_enabled,
					       bss->param.ap_channel.
					       center_chan);
				}
			} else if ((bss->sub_command == MLAN_OID_UAP_BSS_CONFIG)
				   && (pioctl_buf->action == MLAN_ACT_GET)) {
				wlan_uap_ret_cmd_ap_config(pmpriv, resp,
							   pioctl_buf);
			}
		}
		if (pioctl_buf->req_id == MLAN_IOCTL_MISC_CFG) {
			misc = (mlan_ds_misc_cfg *)pioctl_buf->pbuf;
			cust_ie =
				(mlan_ds_misc_custom_ie *)sys_config->
				tlv_buffer;
			if ((pioctl_buf->action == MLAN_ACT_GET ||
			     pioctl_buf->action == MLAN_ACT_SET) &&
			    (misc->sub_command == MLAN_OID_MISC_CUSTOM_IE)) {

				cust_ie->type = wlan_le16_to_cpu(cust_ie->type);
				resp_len = cust_ie->len =
					wlan_le16_to_cpu(cust_ie->len);
				travel_len = 0;
				/* conversion for index, mask, len */
				if (resp_len == sizeof(t_u16))
					cust_ie->ie_data_list[0].ie_index =
						wlan_cpu_to_le16(cust_ie->
								 ie_data_list
								 [0].ie_index);

				while (resp_len > sizeof(t_u16)) {
					cptr = (custom_ie
						*)(((t_u8 *)cust_ie->
						    ie_data_list) + travel_len);
					cptr->ie_index =
						wlan_le16_to_cpu(cptr->
								 ie_index);
					cptr->mgmt_subtype_mask =
						wlan_le16_to_cpu(cptr->
								 mgmt_subtype_mask);
					cptr->ie_length =
						wlan_le16_to_cpu(cptr->
								 ie_length);
					travel_len +=
						cptr->ie_length +
						sizeof(custom_ie) - MAX_IE_SIZE;
					resp_len -=
						cptr->ie_length +
						sizeof(custom_ie) - MAX_IE_SIZE;
				}
				memcpy(pmpriv->adapter, &misc->param.cust_ie,
				       cust_ie,
				       MIN(sizeof(mlan_ds_misc_custom_ie) -
					   sizeof(tlvbuf_max_mgmt_ie),
					   (cust_ie->len +
					    sizeof(MrvlIEtypesHeader_t))));
				max_mgmt_ie =
					(tlvbuf_max_mgmt_ie *)(sys_config->
							       tlv_buffer +
							       cust_ie->len +
							       sizeof
							       (MrvlIEtypesHeader_t));
				if (max_mgmt_ie) {
					max_mgmt_ie->type =
						wlan_le16_to_cpu(max_mgmt_ie->
								 type);
					if (max_mgmt_ie->type ==
					    TLV_TYPE_MAX_MGMT_IE) {
						max_mgmt_ie->len =
							wlan_le16_to_cpu
							(max_mgmt_ie->len);
						max_mgmt_ie->count =
							wlan_le16_to_cpu
							(max_mgmt_ie->count);
						for (i = 0;
						     i < max_mgmt_ie->count;
						     i++) {
							max_mgmt_ie->info[i].
								buf_size =
								wlan_le16_to_cpu
								(max_mgmt_ie->
								 info[i].
								 buf_size);
							max_mgmt_ie->info[i].
								buf_count =
								wlan_le16_to_cpu
								(max_mgmt_ie->
								 info[i].
								 buf_count);
						}
						/* Append max_mgmt_ie TLV after custom_ie */
						memcpy(pmpriv->adapter,
						       (t_u8 *)&misc->param.
						       cust_ie + (cust_ie->len +
								  sizeof
								  (MrvlIEtypesHeader_t)),
						       max_mgmt_ie,
						       MIN(sizeof
							   (tlvbuf_max_mgmt_ie),
							   max_mgmt_ie->len +
							   sizeof
							   (MrvlIEtypesHeader_t)));
					}
				}
			}
		}
	} else {		/* no ioctl: driver generated get/set */
		switch (wlan_le16_to_cpu(tlv->header.type)) {
		case TLV_TYPE_UAP_MAC_ADDRESS:
			memcpy(pmpriv->adapter, pmpriv->curr_addr, tlv->mac,
			       MLAN_MAC_ADDR_LENGTH);
			break;
		case TLV_TYPE_UAP_CHAN_BAND_CONFIG:
			tlv_cb = (MrvlIEtypes_channel_band_t *)tlv;
			pmpriv->uap_state_chan_cb.bandcfg = tlv_cb->bandcfg;
			pmpriv->uap_state_chan_cb.channel = tlv_cb->channel;
			/* call callback waiting for channel info */
			if (pmpriv->uap_state_chan_cb.get_chan_callback)
				pmpriv->uap_state_chan_cb.
					get_chan_callback(pmpriv);
			break;
		case TLV_TYPE_UAP_BEACON_PERIOD:
			tlv_bcnpd = (MrvlIEtypes_beacon_period_t *)tlv;
			pmpriv->uap_state_chan_cb.beacon_period =
				wlan_le16_to_cpu(tlv_bcnpd->beacon_period);
			/* copy dtim_period as well if it follows */
			tlv_dtimpd =
				(MrvlIEtypes_dtim_period_t *)(((t_u8 *)tlv) +
							      sizeof
							      (MrvlIEtypes_beacon_period_t));
			if (TLV_TYPE_UAP_DTIM_PERIOD ==
			    wlan_le16_to_cpu(tlv_dtimpd->header.type))
				pmpriv->uap_state_chan_cb.dtim_period =
					tlv_dtimpd->dtim_period;
			/* call callback waiting for beacon/dtim info */
			if (pmpriv->uap_state_chan_cb.get_chan_callback)
				pmpriv->uap_state_chan_cb.
					get_chan_callback(pmpriv);
			break;
		case TLV_TYPE_MGMT_IE:
			if ((pmpriv->adapter->state_rdh.stage ==
			     RDH_SET_CUSTOM_IE) ||
			    (pmpriv->adapter->state_rdh.stage ==
			     RDH_REM_CUSTOM_IE)) {
				if (!pmpriv->adapter->ecsa_enable)
					wlan_11h_radar_detected_callback((t_void
									  *)
									 pmpriv);
			}
			break;
		}
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of snmp_mib
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param cmd_oid      Cmd oid: treated as sub command
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *  @param pdata_buf    A pointer to information buffer
 *  @return         MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
static mlan_status
wlan_uap_cmd_snmp_mib(pmlan_private pmpriv,
		      IN HostCmd_DS_COMMAND *cmd,
		      IN t_u16 cmd_action,
		      IN t_u32 cmd_oid,
		      IN pmlan_ioctl_req pioctl_buf, IN t_void *pdata_buf)
{
	HostCmd_DS_802_11_SNMP_MIB *psnmp_mib = &cmd->params.smib;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u8 *psnmp_oid = MNULL;
	t_u32 ul_temp;
	t_u8 i;

	t_u8 snmp_oids[] = {
		tkip_mic_failures,
		ccmp_decrypt_errors,
		wep_undecryptable_count,
		wep_icv_error_count,
		decrypt_failure_count,
		dot11_mcast_tx_count,
		dot11_failed_count,
		dot11_retry_count,
		dot11_multi_retry_count,
		dot11_frame_dup_count,
		dot11_rts_success_count,
		dot11_rts_failure_count,
		dot11_ack_failure_count,
		dot11_rx_fragment_count,
		dot11_mcast_rx_frame_count,
		dot11_fcs_error_count,
		dot11_tx_frame_count,
		dot11_rsna_tkip_cm_invoked,
		dot11_rsna_4way_hshk_failures,
	};

	ENTER();

	if (cmd_action == HostCmd_ACT_GEN_GET) {
		cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_SNMP_MIB);
		psnmp_mib->query_type = wlan_cpu_to_le16(HostCmd_ACT_GEN_GET);
		if (cmd_oid == StopDeauth_i) {
			psnmp_mib->oid = wlan_cpu_to_le16((t_u16)StopDeauth_i);
			psnmp_mib->buf_size = wlan_cpu_to_le16(sizeof(t_u8));
			cmd->size =
				wlan_cpu_to_le16(sizeof
						 (HostCmd_DS_802_11_SNMP_MIB) +
						 S_DS_GEN);
		} else {
			cmd->size =
				wlan_cpu_to_le16(sizeof(t_u16) + S_DS_GEN +
						 sizeof(snmp_oids) *
						 sizeof
						 (MrvlIEtypes_snmp_oid_t));
			psnmp_oid = (t_u8 *)&psnmp_mib->oid;
			for (i = 0; i < sizeof(snmp_oids); i++) {
				/* SNMP OID header type */
				*(t_u16 *)psnmp_oid =
					wlan_cpu_to_le16(snmp_oids[i]);
				psnmp_oid += sizeof(t_u16);
				/* SNMP OID header length */
				*(t_u16 *)psnmp_oid =
					wlan_cpu_to_le16(sizeof(t_u32));
				psnmp_oid += sizeof(t_u16) + sizeof(t_u32);
			}
		}
	} else {		/* cmd_action == ACT_SET */
		cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_SNMP_MIB);
		cmd->size = sizeof(HostCmd_DS_802_11_SNMP_MIB) - 1 + S_DS_GEN;
		psnmp_mib->query_type = wlan_cpu_to_le16(HostCmd_ACT_GEN_SET);

		switch (cmd_oid) {
		case Dot11D_i:
		case Dot11H_i:
			psnmp_mib->oid = wlan_cpu_to_le16((t_u16)cmd_oid);
			psnmp_mib->buf_size = wlan_cpu_to_le16(sizeof(t_u16));
			ul_temp = *(t_u32 *)pdata_buf;
			*((t_u16 *)(psnmp_mib->value)) =
				wlan_cpu_to_le16((t_u16)ul_temp);
			cmd->size += sizeof(t_u16);
			break;
		case ECSAEnable_i:
			psnmp_mib->oid = wlan_cpu_to_le16((t_u16)cmd_oid);
			psnmp_mib->buf_size = wlan_cpu_to_le16(sizeof(t_u8));
			psnmp_mib->value[0] = *((t_u8 *)pdata_buf);
			cmd->size += sizeof(t_u8);
			break;
		case StopDeauth_i:
			psnmp_mib->oid = wlan_cpu_to_le16((t_u16)cmd_oid);
			psnmp_mib->buf_size = wlan_cpu_to_le16(sizeof(t_u8));
			psnmp_mib->value[0] = *((t_u8 *)pdata_buf);
			cmd->size += sizeof(t_u8);
			break;
		default:
			PRINTM(MERROR, "Unsupported OID.\n");
			ret = MLAN_STATUS_FAILURE;
			break;
		}
		cmd->size = wlan_cpu_to_le16(cmd->size);
	}

	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of get_log.
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_cmd_802_11_get_log(IN pmlan_private pmpriv, IN HostCmd_DS_COMMAND *cmd)
{
	ENTER();
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_GET_LOG);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_802_11_GET_LOG) + S_DS_GEN);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of snmp_mib
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_snmp_mib(IN pmlan_private pmpriv,
		      IN HostCmd_DS_COMMAND *resp,
		      IN mlan_ioctl_req *pioctl_buf)
{
	pmlan_adapter pmadapter = pmpriv->adapter;
	HostCmd_DS_802_11_SNMP_MIB *psnmp_mib =
		(HostCmd_DS_802_11_SNMP_MIB *)&resp->params.smib;
	mlan_ds_get_info *info;
	mlan_ds_snmp_mib *mib = MNULL;
	t_u16 oid = wlan_le16_to_cpu(psnmp_mib->oid);
	t_u8 *psnmp_oid = MNULL;
	t_u32 data;
	t_u16 tlv_buf_left = 0;
	t_u16 tlv_type = 0;
	t_u16 query_type = wlan_le16_to_cpu(psnmp_mib->query_type);

	ENTER();
	if (query_type == HostCmd_ACT_GEN_GET) {
		if (!pioctl_buf) {
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
		if (oid == StopDeauth_i) {
			mib = (mlan_ds_snmp_mib *)pioctl_buf->pbuf;
			if (mib)
				mib->param.deauthctrl = psnmp_mib->value[0];
			LEAVE();
			return MLAN_STATUS_SUCCESS;
		}
		info = (mlan_ds_get_info *)pioctl_buf->pbuf;
		tlv_buf_left = resp->size - (sizeof(t_u16) + S_DS_GEN);
		psnmp_oid = (t_u8 *)&psnmp_mib->oid;
		while (tlv_buf_left >= sizeof(MrvlIEtypes_snmp_oid_t)) {
			tlv_type = wlan_le16_to_cpu(*(t_u16 *)psnmp_oid);
			psnmp_oid += sizeof(t_u16) + sizeof(t_u16);
			memcpy(pmadapter, &data, psnmp_oid, sizeof(t_u32));
			switch (tlv_type) {
			case tkip_mic_failures:
				info->param.ustats.tkip_mic_failures =
					wlan_le32_to_cpu(data);
				break;
			case ccmp_decrypt_errors:
				info->param.ustats.ccmp_decrypt_errors =
					wlan_le32_to_cpu(data);
				break;
			case wep_undecryptable_count:
				info->param.ustats.wep_undecryptable_count =
					wlan_le32_to_cpu(data);
				break;
			case wep_icv_error_count:
				info->param.ustats.wep_icv_error_count =
					wlan_le32_to_cpu(data);
				break;
			case decrypt_failure_count:
				info->param.ustats.decrypt_failure_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_mcast_tx_count:
				info->param.ustats.mcast_tx_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_failed_count:
				info->param.ustats.failed_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_retry_count:
				info->param.ustats.retry_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_multi_retry_count:
				info->param.ustats.multi_retry_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_frame_dup_count:
				info->param.ustats.frame_dup_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_rts_success_count:
				info->param.ustats.rts_success_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_rts_failure_count:
				info->param.ustats.rts_failure_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_ack_failure_count:
				info->param.ustats.ack_failure_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_rx_fragment_count:
				info->param.ustats.rx_fragment_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_mcast_rx_frame_count:
				info->param.ustats.mcast_rx_frame_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_fcs_error_count:
				info->param.ustats.fcs_error_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_tx_frame_count:
				info->param.ustats.tx_frame_count =
					wlan_le32_to_cpu(data);
				break;
			case dot11_rsna_tkip_cm_invoked:
				info->param.ustats.rsna_tkip_cm_invoked =
					wlan_le32_to_cpu(data);
				break;
			case dot11_rsna_4way_hshk_failures:
				info->param.ustats.rsna_4way_hshk_failures =
					wlan_le32_to_cpu(data);
				break;
			}
			tlv_buf_left -= sizeof(MrvlIEtypes_snmp_oid_t);
			psnmp_oid += sizeof(t_u32);
		}
	} else {		/* ACT_SET */
		switch (wlan_le16_to_cpu(psnmp_mib->oid)) {
		case Dot11D_i:
			data = wlan_le16_to_cpu(*((t_u16 *)(psnmp_mib->value)));
			/* Set 11d state to private */
			pmpriv->state_11d.enable_11d = data;
			/* Set user enable flag if called from ioctl */
			if (pioctl_buf)
				pmpriv->state_11d.user_enable_11d = data;
			break;
		case Dot11H_i:
			data = wlan_le16_to_cpu(*((t_u16 *)(psnmp_mib->value)));
			/* Set 11h state to priv */
			pmpriv->intf_state_11h.is_11h_active =
				(data & ENABLE_11H_MASK);
			/* Set radar_det state to adapter */
			pmpriv->adapter->state_11h.is_master_radar_det_active
				=
				(data & MASTER_RADAR_DET_MASK) ? MTRUE : MFALSE;
			pmpriv->adapter->state_11h.is_slave_radar_det_active =
				(data & SLAVE_RADAR_DET_MASK) ? MTRUE : MFALSE;
			break;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of get_log
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_get_log(IN pmlan_private pmpriv,
		     IN HostCmd_DS_COMMAND *resp, IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_802_11_GET_LOG *pget_log =
		(HostCmd_DS_802_11_GET_LOG *)&resp->params.get_log;
	mlan_ds_get_info *pget_info = MNULL;
	int i = 0;

	ENTER();

	if (pioctl_buf) {
		pget_info = (mlan_ds_get_info *)pioctl_buf->pbuf;
		pget_info->param.stats.mcast_tx_frame =
			wlan_le32_to_cpu(pget_log->mcast_tx_frame);
		pget_info->param.stats.failed =
			wlan_le32_to_cpu(pget_log->failed);
		pget_info->param.stats.retry =
			wlan_le32_to_cpu(pget_log->retry);
		pget_info->param.stats.multi_retry =
			wlan_le32_to_cpu(pget_log->multiretry);
		pget_info->param.stats.frame_dup =
			wlan_le32_to_cpu(pget_log->frame_dup);
		pget_info->param.stats.rts_success =
			wlan_le32_to_cpu(pget_log->rts_success);
		pget_info->param.stats.rts_failure =
			wlan_le32_to_cpu(pget_log->rts_failure);
		pget_info->param.stats.ack_failure =
			wlan_le32_to_cpu(pget_log->ack_failure);
		pget_info->param.stats.rx_frag =
			wlan_le32_to_cpu(pget_log->rx_frag);
		pget_info->param.stats.mcast_rx_frame =
			wlan_le32_to_cpu(pget_log->mcast_rx_frame);
		pget_info->param.stats.fcs_error =
			wlan_le32_to_cpu(pget_log->fcs_error);
		pget_info->param.stats.tx_frame =
			wlan_le32_to_cpu(pget_log->tx_frame);
		pget_info->param.stats.wep_icv_error[0] =
			wlan_le32_to_cpu(pget_log->wep_icv_err_cnt[0]);
		pget_info->param.stats.wep_icv_error[1] =
			wlan_le32_to_cpu(pget_log->wep_icv_err_cnt[1]);
		pget_info->param.stats.wep_icv_error[2] =
			wlan_le32_to_cpu(pget_log->wep_icv_err_cnt[2]);
		pget_info->param.stats.wep_icv_error[3] =
			wlan_le32_to_cpu(pget_log->wep_icv_err_cnt[3]);
		pget_info->param.stats.bcn_rcv_cnt =
			wlan_le32_to_cpu(pget_log->bcn_rcv_cnt);
		pget_info->param.stats.bcn_miss_cnt =
			wlan_le32_to_cpu(pget_log->bcn_miss_cnt);
		pget_info->param.stats.amsdu_rx_cnt = pmpriv->amsdu_rx_cnt;
		pget_info->param.stats.msdu_in_rx_amsdu_cnt =
			pmpriv->msdu_in_rx_amsdu_cnt;
		pget_info->param.stats.amsdu_tx_cnt = pmpriv->amsdu_tx_cnt;
		pget_info->param.stats.msdu_in_tx_amsdu_cnt =
			pmpriv->msdu_in_tx_amsdu_cnt;
		if (pmpriv->adapter->getlog_enable) {
			pget_info->param.stats.tx_frag_cnt =
				wlan_le32_to_cpu(pget_log->tx_frag_cnt);
			for (i = 0; i < 8; i++) {
				pget_info->param.stats.qos_tx_frag_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_tx_frag_cnt[i]);
				pget_info->param.stats.qos_failed_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_failed_cnt[i]);
				pget_info->param.stats.qos_retry_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_retry_cnt[i]);
				pget_info->param.stats.qos_multi_retry_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_multi_retry_cnt
							 [i]);
				pget_info->param.stats.qos_frm_dup_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_frm_dup_cnt[i]);
				pget_info->param.stats.qos_rts_suc_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_rts_suc_cnt[i]);
				pget_info->param.stats.qos_rts_failure_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_rts_failure_cnt
							 [i]);
				pget_info->param.stats.qos_ack_failure_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_ack_failure_cnt
							 [i]);
				pget_info->param.stats.qos_rx_frag_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_rx_frag_cnt[i]);
				pget_info->param.stats.qos_tx_frm_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_tx_frm_cnt[i]);
				pget_info->param.stats.
					qos_discarded_frm_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_discarded_frm_cnt
							 [i]);
				pget_info->param.stats.qos_mpdus_rx_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_mpdus_rx_cnt[i]);
				pget_info->param.stats.qos_retries_rx_cnt[i] =
					wlan_le32_to_cpu(pget_log->
							 qos_retries_rx_cnt[i]);
			}
			pget_info->param.stats.mgmt_ccmp_replays =
				wlan_le32_to_cpu(pget_log->mgmt_ccmp_replays);
			pget_info->param.stats.tx_amsdu_cnt =
				wlan_le32_to_cpu(pget_log->tx_amsdu_cnt);
			pget_info->param.stats.failed_amsdu_cnt =
				wlan_le32_to_cpu(pget_log->failed_amsdu_cnt);
			pget_info->param.stats.retry_amsdu_cnt =
				wlan_le32_to_cpu(pget_log->retry_amsdu_cnt);
			pget_info->param.stats.multi_retry_amsdu_cnt =
				wlan_le32_to_cpu(pget_log->
						 multi_retry_amsdu_cnt);
			pget_info->param.stats.tx_octets_in_amsdu_cnt =
				wlan_le64_to_cpu(pget_log->
						 tx_octets_in_amsdu_cnt);
			pget_info->param.stats.amsdu_ack_failure_cnt =
				wlan_le32_to_cpu(pget_log->
						 amsdu_ack_failure_cnt);
			pget_info->param.stats.rx_amsdu_cnt =
				wlan_le32_to_cpu(pget_log->rx_amsdu_cnt);
			pget_info->param.stats.rx_octets_in_amsdu_cnt =
				wlan_le64_to_cpu(pget_log->
						 rx_octets_in_amsdu_cnt);
			pget_info->param.stats.tx_ampdu_cnt =
				wlan_le32_to_cpu(pget_log->tx_ampdu_cnt);
			pget_info->param.stats.tx_mpdus_in_ampdu_cnt =
				wlan_le32_to_cpu(pget_log->
						 tx_mpdus_in_ampdu_cnt);
			pget_info->param.stats.tx_octets_in_ampdu_cnt =
				wlan_le64_to_cpu(pget_log->
						 tx_octets_in_ampdu_cnt);
			pget_info->param.stats.ampdu_rx_cnt =
				wlan_le32_to_cpu(pget_log->ampdu_rx_cnt);
			pget_info->param.stats.mpdu_in_rx_ampdu_cnt =
				wlan_le32_to_cpu(pget_log->
						 mpdu_in_rx_ampdu_cnt);
			pget_info->param.stats.rx_octets_in_ampdu_cnt =
				wlan_le64_to_cpu(pget_log->
						 rx_octets_in_ampdu_cnt);
			pget_info->param.stats.ampdu_delimiter_crc_error_cnt =
				wlan_le32_to_cpu(pget_log->
						 ampdu_delimiter_crc_error_cnt);

			/* Indicate ioctl complete */
			pioctl_buf->data_read_written =
				sizeof(mlan_ds_get_info);
		} else
			pioctl_buf->data_read_written =
				sizeof(mlan_ds_get_stats_org) +
				sizeof(pget_info->sub_command);
	}
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of deauth station
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_cmd_sta_deauth(pmlan_private pmpriv,
			IN HostCmd_DS_COMMAND *cmd, IN t_void *pdata_buf)
{
	HostCmd_DS_STA_DEAUTH *pcmd_sta_deauth =
		(HostCmd_DS_STA_DEAUTH *)&cmd->params.sta_deauth;
	mlan_deauth_param *deauth = (mlan_deauth_param *)pdata_buf;

	ENTER();
	cmd->command = wlan_cpu_to_le16(HOST_CMD_APCMD_STA_DEAUTH);
	cmd->size = wlan_cpu_to_le16(S_DS_GEN + sizeof(HostCmd_DS_STA_DEAUTH));
	memcpy(pmpriv->adapter, pcmd_sta_deauth->mac, deauth->mac_addr,
	       MLAN_MAC_ADDR_LENGTH);
	pcmd_sta_deauth->reason = wlan_cpu_to_le16(deauth->reason_code);
	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function prepares command of key material
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   The action: GET or SET
 *  @param cmd_oid      OID: ENABLE or DISABLE
 *  @param pdata_buf    A pointer to data buffer
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_cmd_key_material(IN pmlan_private pmpriv,
			  IN HostCmd_DS_COMMAND *cmd,
			  IN t_u16 cmd_action,
			  IN t_u16 cmd_oid, IN t_void *pdata_buf)
{
	HostCmd_DS_802_11_KEY_MATERIAL *pkey_material =
		&cmd->params.key_material;
	mlan_ds_encrypt_key *pkey = (mlan_ds_encrypt_key *)pdata_buf;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	sta_node *sta_ptr = MNULL;

	ENTER();
	if (!pkey) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	cmd->command = wlan_cpu_to_le16(HostCmd_CMD_802_11_KEY_MATERIAL);
	pkey_material->action = wlan_cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_GET) {
		cmd->size =
			wlan_cpu_to_le16(sizeof(pkey_material->action) +
					 S_DS_GEN);
		goto done;
	}
	memset(pmpriv->adapter, &pkey_material->key_param_set, 0,
	       sizeof(MrvlIEtype_KeyParamSetV2_t));
	if (pkey->key_flags & KEY_FLAG_REMOVE_KEY) {
		pkey_material->action =
			wlan_cpu_to_le16(HostCmd_ACT_GEN_REMOVE);
		pkey_material->key_param_set.type =
			wlan_cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
		pkey_material->key_param_set.length =
			wlan_cpu_to_le16(KEY_PARAMS_FIXED_LEN);
		pkey_material->key_param_set.key_idx =
			pkey->key_index & KEY_INDEX_MASK;
		pkey_material->key_param_set.key_info =
			wlan_cpu_to_le16(KEY_INFO_MCAST_KEY |
					 KEY_INFO_UCAST_KEY);
		memcpy(pmpriv->adapter, pkey_material->key_param_set.mac_addr,
		       pkey->mac_addr, MLAN_MAC_ADDR_LENGTH);
		cmd->size =
			wlan_cpu_to_le16(sizeof(MrvlIEtypesHeader_t) +
					 S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					 sizeof(pkey_material->action));
		PRINTM(MCMND, "Remove Key\n");
		goto done;
	}
	pkey_material->action = wlan_cpu_to_le16(HostCmd_ACT_GEN_SET);
	pkey_material->key_param_set.key_idx = pkey->key_index & KEY_INDEX_MASK;
	pkey_material->key_param_set.type =
		wlan_cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
	pkey_material->key_param_set.key_info = KEY_INFO_ENABLE_KEY;
	memcpy(pmpriv->adapter, pkey_material->key_param_set.mac_addr,
	       pkey->mac_addr, MLAN_MAC_ADDR_LENGTH);
	if (pkey->key_len <= MAX_WEP_KEY_SIZE) {
		pkey_material->key_param_set.length =
			wlan_cpu_to_le16(KEY_PARAMS_FIXED_LEN +
					 sizeof(wep_param_t));
		pkey_material->key_param_set.key_type = KEY_TYPE_ID_WEP;
		pkey_material->key_param_set.key_info |=
			KEY_INFO_MCAST_KEY | KEY_INFO_UCAST_KEY;
		if (pkey_material->key_param_set.key_idx ==
		    (pmpriv->wep_key_curr_index & KEY_INDEX_MASK))
			pkey_material->key_param_set.key_info |=
				KEY_INFO_DEFAULT_KEY;
		pkey_material->key_param_set.key_info =
			wlan_cpu_to_le16(pkey_material->key_param_set.key_info);
		pkey_material->key_param_set.key_params.wep.key_len =
			wlan_cpu_to_le16(pkey->key_len);
		memcpy(pmpriv->adapter,
		       pkey_material->key_param_set.key_params.wep.key,
		       pkey->key_material, pkey->key_len);
		cmd->size =
			wlan_cpu_to_le16(sizeof(MrvlIEtypesHeader_t) +
					 S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					 sizeof(wep_param_t) +
					 sizeof(pkey_material->action));
		PRINTM(MCMND, "Set WEP Key\n");
		goto done;
	}
	if (pkey->key_flags & KEY_FLAG_GROUP_KEY)
		pkey_material->key_param_set.key_info |= KEY_INFO_MCAST_KEY;
	else
		pkey_material->key_param_set.key_info |= KEY_INFO_UCAST_KEY;
	if (pkey->key_flags & KEY_FLAG_AES_MCAST_IGTK)
		pkey_material->key_param_set.key_info = KEY_INFO_CMAC_AES_KEY;
	if (pkey->key_flags & KEY_FLAG_SET_TX_KEY)
		pkey_material->key_param_set.key_info |=
			KEY_INFO_TX_KEY | KEY_INFO_RX_KEY;
	else
		pkey_material->key_param_set.key_info |= KEY_INFO_TX_KEY;
	if (pkey->is_wapi_key) {
		pkey_material->key_param_set.key_type = KEY_TYPE_ID_WAPI;
		memcpy(pmpriv->adapter,
		       pkey_material->key_param_set.key_params.wapi.pn,
		       pkey->pn, PN_SIZE);
		pkey_material->key_param_set.key_params.wapi.key_len =
			wlan_cpu_to_le16(MIN(WAPI_KEY_SIZE, pkey->key_len));
		memcpy(pmpriv->adapter,
		       pkey_material->key_param_set.key_params.wapi.key,
		       pkey->key_material, MIN(WAPI_KEY_SIZE, pkey->key_len));
		if (!pmpriv->sec_info.wapi_key_on)
			pkey_material->key_param_set.key_info |=
				KEY_INFO_DEFAULT_KEY;
		if (pkey->key_flags & KEY_FLAG_GROUP_KEY) {
			pmpriv->sec_info.wapi_key_on = MTRUE;
		} else {
			/* WAPI pairwise key: unicast */
			sta_ptr =
				wlan_add_station_entry(pmpriv, pkey->mac_addr);
			if (sta_ptr) {
				PRINTM(MCMND, "station: wapi_key_on\n");
				sta_ptr->wapi_key_on = MTRUE;
			}
		}
		pkey_material->key_param_set.key_info =
			wlan_cpu_to_le16(pkey_material->key_param_set.key_info);
		pkey_material->key_param_set.length =
			wlan_cpu_to_le16(KEY_PARAMS_FIXED_LEN +
					 sizeof(wapi_param));
		cmd->size =
			wlan_cpu_to_le16(sizeof(MrvlIEtypesHeader_t) +
					 S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					 sizeof(wapi_param) +
					 sizeof(pkey_material->action));
		PRINTM(MCMND, "Set WAPI Key\n");
		goto done;
	}
	pkey_material->key_param_set.key_info |= KEY_INFO_DEFAULT_KEY;
	pkey_material->key_param_set.key_info =
		wlan_cpu_to_le16(pkey_material->key_param_set.key_info);
	if (pkey->key_len == WPA_AES_KEY_LEN &&
	    !(pkey->key_flags & KEY_FLAG_AES_MCAST_IGTK)) {
		if (pkey->
		    key_flags & (KEY_FLAG_RX_SEQ_VALID | KEY_FLAG_TX_SEQ_VALID))
			memcpy(pmpriv->adapter,
			       pkey_material->key_param_set.key_params.aes.pn,
			       pkey->pn, SEQ_MAX_SIZE);
		pkey_material->key_param_set.key_type = KEY_TYPE_ID_AES;
		pkey_material->key_param_set.key_params.aes.key_len =
			wlan_cpu_to_le16(pkey->key_len);
		memcpy(pmpriv->adapter,
		       pkey_material->key_param_set.key_params.aes.key,
		       pkey->key_material, pkey->key_len);
		pkey_material->key_param_set.length =
			wlan_cpu_to_le16(KEY_PARAMS_FIXED_LEN +
					 sizeof(aes_param));
		cmd->size =
			wlan_cpu_to_le16(sizeof(MrvlIEtypesHeader_t) +
					 S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					 sizeof(aes_param) +
					 sizeof(pkey_material->action));
		PRINTM(MCMND, "Set AES Key\n");
		goto done;
	}
	if (pkey->key_len == WPA_IGTK_KEY_LEN &&
	    (pkey->key_flags & KEY_FLAG_AES_MCAST_IGTK)) {
		if (pkey->
		    key_flags & (KEY_FLAG_RX_SEQ_VALID | KEY_FLAG_TX_SEQ_VALID))
			memcpy(pmpriv->adapter,
			       pkey_material->key_param_set.key_params.cmac_aes.
			       ipn, pkey->pn, SEQ_MAX_SIZE);
		pkey_material->key_param_set.key_info &=
			~(wlan_cpu_to_le16(KEY_INFO_MCAST_KEY));
		pkey_material->key_param_set.key_info |=
			wlan_cpu_to_le16(KEY_INFO_AES_MCAST_IGTK);
		pkey_material->key_param_set.key_type = KEY_TYPE_ID_AES_CMAC;
		pkey_material->key_param_set.key_params.cmac_aes.key_len =
			wlan_cpu_to_le16(pkey->key_len);
		memcpy(pmpriv->adapter,
		       pkey_material->key_param_set.key_params.cmac_aes.key,
		       pkey->key_material, pkey->key_len);
		pkey_material->key_param_set.length =
			wlan_cpu_to_le16(KEY_PARAMS_FIXED_LEN +
					 sizeof(cmac_aes_param));
		cmd->size =
			wlan_cpu_to_le16(sizeof(MrvlIEtypesHeader_t) +
					 S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					 sizeof(cmac_aes_param) +
					 sizeof(pkey_material->action));
		PRINTM(MCMND, "Set CMAC AES Key\n");
		goto done;
	}
	if (pkey->key_len == WPA_TKIP_KEY_LEN) {
		if (pkey->
		    key_flags & (KEY_FLAG_RX_SEQ_VALID | KEY_FLAG_TX_SEQ_VALID))
			memcpy(pmpriv->adapter,
			       pkey_material->key_param_set.key_params.tkip.pn,
			       pkey->pn, SEQ_MAX_SIZE);
		pkey_material->key_param_set.key_type = KEY_TYPE_ID_TKIP;
		pkey_material->key_param_set.key_params.tkip.key_len =
			wlan_cpu_to_le16(pkey->key_len);
		memcpy(pmpriv->adapter,
		       pkey_material->key_param_set.key_params.tkip.key,
		       pkey->key_material, pkey->key_len);
		pkey_material->key_param_set.length =
			wlan_cpu_to_le16(KEY_PARAMS_FIXED_LEN +
					 sizeof(tkip_param));
		cmd->size =
			wlan_cpu_to_le16(sizeof(MrvlIEtypesHeader_t) +
					 S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					 sizeof(tkip_param) +
					 sizeof(pkey_material->action));
		PRINTM(MCMND, "Set TKIP Key\n");
	}
done:
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the command response of sta_list
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_sta_list(IN pmlan_private pmpriv,
		      IN HostCmd_DS_COMMAND *resp,
		      IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_STA_LIST *sta_list =
		(HostCmd_DS_STA_LIST *)&resp->params.sta_list;
	mlan_ds_get_info *info;
	MrvlIEtypes_sta_info_t *tlv = MNULL;
	t_u8 i = 0;
	sta_node *sta_ptr;

	ENTER();
	if (pioctl_buf) {
		info = (mlan_ds_get_info *)pioctl_buf->pbuf;
		info->param.sta_list.sta_count =
			wlan_le16_to_cpu(sta_list->sta_count);
		tlv = (MrvlIEtypes_sta_info_t *)((t_u8 *)sta_list +
						 sizeof(HostCmd_DS_STA_LIST));
		info->param.sta_list.sta_count =
			MIN(info->param.sta_list.sta_count, MAX_NUM_CLIENTS);
		for (i = 0; i < info->param.sta_list.sta_count; i++) {
			memcpy(pmpriv->adapter,
			       info->param.sta_list.info[i].mac_address,
			       tlv->mac_address, MLAN_MAC_ADDR_LENGTH);
			info->param.sta_list.info[i].power_mfg_status =
				tlv->power_mfg_status;
			info->param.sta_list.info[i].rssi = tlv->rssi;
			sta_ptr =
				wlan_get_station_entry(pmpriv,
						       tlv->mac_address);
			if (sta_ptr)
				info->param.sta_list.info[i].bandmode =
					sta_ptr->bandmode;
			else
				info->param.sta_list.info[i].bandmode = 0xFF;
			tlv++;
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/** Fixed size of bss start event */
#define BSS_START_EVENT_FIX_SIZE    12

/**
 *  @brief This function will search for the specific ie
 *
 *  @param priv    A pointer to mlan_private
 *  @param pevent  A pointer to event buf
 *
 *  @return	       N/A
 */
static void
wlan_check_uap_capability(pmlan_private priv, pmlan_buffer pevent)
{
	t_u16 tlv_type, tlv_len;
	int tlv_buf_left = pevent->data_len - BSS_START_EVENT_FIX_SIZE;
	MrvlIEtypesHeader_t *tlv =
		(MrvlIEtypesHeader_t *)(pevent->pbuf + pevent->data_offset +
					BSS_START_EVENT_FIX_SIZE);
	const t_u8 wmm_oui[4] = { 0x00, 0x50, 0xf2, 0x02 };
	IEEEtypes_WmmParameter_t wmm_param_ie;
	MrvlIEtypes_channel_band_t *pchan_info;
	t_u8 event_buf[100];
	mlan_event *event = (mlan_event *)event_buf;
	chan_band_info *pchan_band_info = (chan_band_info *) event->event_buf;
	priv->wmm_enabled = MFALSE;
	priv->pkt_fwd = MFALSE;
	priv->is_11n_enabled = MFALSE;

	ENTER();

	while (tlv_buf_left >= (int)sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if ((sizeof(MrvlIEtypesHeader_t) + tlv_len) >
		    (unsigned int)tlv_buf_left) {
			PRINTM(MERROR, "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
			       tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == HT_CAPABILITY) {
			DBG_HEXDUMP(MCMD_D, "HT_CAP tlv", tlv,
				    tlv_len + sizeof(MrvlIEtypesHeader_t));
			priv->is_11n_enabled = MTRUE;
		}
		if (tlv_type == VENDOR_SPECIFIC_221) {
			if (!memcmp
			    (priv->adapter,
			     (t_u8 *)tlv + sizeof(MrvlIEtypesHeader_t), wmm_oui,
			     sizeof(wmm_oui))) {
				DBG_HEXDUMP(MCMD_D, "wmm ie tlv", tlv,
					    tlv_len +
					    sizeof(MrvlIEtypesHeader_t));
				priv->wmm_enabled = MFALSE;
				wlan_wmm_setup_ac_downgrade(priv);
				priv->wmm_enabled = MTRUE;
				memcpy(priv->adapter, &wmm_param_ie,
				       ((t_u8 *)tlv + 2),
				       sizeof(IEEEtypes_WmmParameter_t));
				wmm_param_ie.vend_hdr.len = (t_u8)tlv_len;
				wmm_param_ie.vend_hdr.element_id = WMM_IE;
				wlan_wmm_setup_queue_priorities(priv,
								&wmm_param_ie);
			}
		}
		if (tlv_type == TLV_TYPE_UAP_PKT_FWD_CTL) {
			DBG_HEXDUMP(MCMD_D, "pkt_fwd tlv", tlv,
				    tlv_len + sizeof(MrvlIEtypesHeader_t));
			priv->pkt_fwd =
				*((t_u8 *)tlv + sizeof(MrvlIEtypesHeader_t));
			PRINTM(MCMND, "pkt_fwd FW: 0x%x\n", priv->pkt_fwd);
			if (priv->pkt_fwd & PKT_FWD_FW_BIT)
				priv->pkt_fwd = MFALSE;
			else
				priv->pkt_fwd |= PKT_FWD_ENABLE_BIT;
			PRINTM(MCMND, "pkt_fwd DRV: 0x%x\n", priv->pkt_fwd);
		}
		if (tlv_type == TLV_TYPE_UAP_CHAN_BAND_CONFIG) {
			DBG_HEXDUMP(MCMD_D, "chan_band_config tlv", tlv,
				    tlv_len + sizeof(MrvlIEtypesHeader_t));
			pchan_info = (MrvlIEtypes_channel_band_t *)tlv;
			priv->uap_channel = pchan_info->channel;
			PRINTM(MCMND, "uap_channel FW: 0x%x\n",
			       priv->uap_channel);
			event->bss_index = priv->bss_index;
			event->event_id = MLAN_EVENT_ID_DRV_UAP_CHAN_INFO;
			event->event_len = sizeof(chan_band_info);
			memcpy(priv->adapter, (t_u8 *)&pchan_band_info->bandcfg,
			       (t_u8 *)&pchan_info->bandcfg, tlv_len);
			if (priv->adapter->ecsa_enable) {
				int ret;
				t_u8 bandwidth = BW_20MHZ;

				MrvlIEtypes_chan_bw_oper_t chan_bw_oper;
				chan_bw_oper.header.type = REGULATORY_CLASS;
				chan_bw_oper.header.len =
					sizeof(MrvlIEtypes_chan_bw_oper_t);
				chan_bw_oper.ds_chan_bw_oper.channel =
					pchan_info->channel;

				if (pchan_band_info->bandcfg.chanWidth ==
				    CHAN_BW_40MHZ)
					bandwidth = BW_40MHZ;
				chan_bw_oper.ds_chan_bw_oper.bandwidth =
					bandwidth;

				ret = wlan_prepare_cmd(priv,
						       HOST_CMD_APCMD_SYS_CONFIGURE,
						       HostCmd_ACT_GEN_SET, 0,
						       MNULL, &chan_bw_oper);
				if (ret != MLAN_STATUS_SUCCESS &&
				    ret != MLAN_STATUS_PENDING) {
					PRINTM(MERROR,
					       "%s(): Could not set supported operating class IE for priv=%p [priv_bss_idx=%d]!\n",
					       __func__, priv, priv->bss_index);
				}
			}
		}

		tlv_buf_left -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
	if (priv->wmm_enabled == MFALSE) {
		/* Since WMM is not enabled, setup the queues with the defaults */
		wlan_wmm_setup_queues(priv);
	}
	if (event->event_id == MLAN_EVENT_ID_DRV_UAP_CHAN_INFO) {
		pchan_band_info->is_11n_enabled = priv->is_11n_enabled;
		wlan_recv_event(priv, MLAN_EVENT_ID_DRV_UAP_CHAN_INFO, event);
	}

	LEAVE();
}

/**
 *  @brief This function will update WAPI PN in statation assoc event
 *
 *  @param priv    A pointer to mlan_private
 *  @param pevent  A pointer to event buf
 *
 *  @return	       MFALSE
 */
static t_u32
wlan_update_wapi_info_tlv(pmlan_private priv, pmlan_buffer pevent)
{
	t_u32 ret = MFALSE;
	t_u16 tlv_type, tlv_len;
	t_u32 tx_pn[4];
	t_u32 i = 0;
	int tlv_buf_left = pevent->data_len - ASSOC_EVENT_FIX_SIZE;
	MrvlIEtypesHeader_t *tlv = (MrvlIEtypesHeader_t *)
		(pevent->pbuf + pevent->data_offset + ASSOC_EVENT_FIX_SIZE);
	MrvlIEtypes_wapi_info_t *wapi_tlv = MNULL;

	ENTER();
	while (tlv_buf_left >= (int)sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if ((sizeof(MrvlIEtypesHeader_t) + tlv_len) >
		    (unsigned int)tlv_buf_left) {
			PRINTM(MERROR, "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
			       tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == TLV_TYPE_AP_WAPI_INFO) {
			wapi_tlv = (MrvlIEtypes_wapi_info_t *)tlv;
			DBG_HEXDUMP(MCMD_D, "Fw:multicast_PN",
				    wapi_tlv->multicast_PN, PN_SIZE);
			memcpy(priv->adapter, (t_u8 *)tx_pn,
			       wapi_tlv->multicast_PN, PN_SIZE);
			for (i = 0; i < 4; i++)
				tx_pn[i] = mlan_ntohl(tx_pn[i]);
			memcpy(priv->adapter, wapi_tlv->multicast_PN,
			       (t_u8 *)tx_pn, PN_SIZE);
			DBG_HEXDUMP(MCMD_D, "Host:multicast_PN",
				    wapi_tlv->multicast_PN, PN_SIZE);
			break;
		}
		tlv_buf_left -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
	LEAVE();

	return ret;
}

/**
 *  @brief This function send sta_assoc_event to moal
 *          payload with sta mac address and assoc ie.
 *
 *  @param priv    A pointer to mlan_private
 *  @param pevent  A pointer to mlan_event buffer
 *  @param pbuf    A pointer to mlan_buffer which has event content.
 *
 *  @return	       MFALSE
 */
static t_u32
wlan_process_sta_assoc_event(pmlan_private priv, mlan_event *pevent,
			     pmlan_buffer pmbuf)
{
	t_u32 ret = MFALSE;
	t_u16 tlv_type, tlv_len;
	t_u16 frame_control, frame_sub_type = 0;
	t_u8 *assoc_req_ie = MNULL;
	t_u8 ie_len = 0, assoc_ie_len = 0;
	int tlv_buf_left = pmbuf->data_len - ASSOC_EVENT_FIX_SIZE;
	MrvlIEtypesHeader_t *tlv = (MrvlIEtypesHeader_t *)
		(pmbuf->pbuf + pmbuf->data_offset + ASSOC_EVENT_FIX_SIZE);
	MrvlIETypes_MgmtFrameSet_t *mgmt_tlv = MNULL;

	ENTER();
	pevent->event_id = MLAN_EVENT_ID_UAP_FW_STA_CONNECT;
	pevent->bss_index = priv->bss_index;
	pevent->event_len = MLAN_MAC_ADDR_LENGTH;
	memcpy(priv->adapter, pevent->event_buf,
	       pmbuf->pbuf + pmbuf->data_offset + 6, pevent->event_len);
	while (tlv_buf_left >= (int)sizeof(MrvlIEtypesHeader_t)) {
		tlv_type = wlan_le16_to_cpu(tlv->type);
		tlv_len = wlan_le16_to_cpu(tlv->len);
		if ((sizeof(MrvlIEtypesHeader_t) + tlv_len) >
		    (unsigned int)tlv_buf_left) {
			PRINTM(MERROR, "wrong tlv: tlvLen=%d, tlvBufLeft=%d\n",
			       tlv_len, tlv_buf_left);
			break;
		}
		if (tlv_type == TLV_TYPE_MGMT_FRAME) {
			mgmt_tlv = (MrvlIETypes_MgmtFrameSet_t *)tlv;
			memcpy(priv->adapter, &frame_control,
			       (t_u8 *)&(mgmt_tlv->frame_control),
			       sizeof(frame_control));
			frame_sub_type =
				IEEE80211_GET_FC_MGMT_FRAME_SUBTYPE
				(frame_control);
			if ((mgmt_tlv->frame_control.type == 0) &&
			    ((frame_sub_type == SUBTYPE_ASSOC_REQUEST) ||
			     (frame_sub_type == SUBTYPE_REASSOC_REQUEST))) {

				if (frame_sub_type == SUBTYPE_ASSOC_REQUEST)
					assoc_ie_len =
						sizeof(IEEEtypes_AssocRqst_t);
				else if (frame_sub_type ==
					 SUBTYPE_REASSOC_REQUEST)
					assoc_ie_len =
						sizeof(IEEEtypes_ReAssocRqst_t);

				ie_len = tlv_len -
					sizeof(IEEEtypes_FrameCtl_t) -
					assoc_ie_len;
				assoc_req_ie =
					(t_u8 *)tlv +
					sizeof(MrvlIETypes_MgmtFrameSet_t) +
					assoc_ie_len;
				memcpy(priv->adapter,
				       pevent->event_buf + pevent->event_len,
				       assoc_req_ie, ie_len);
				pevent->event_len += ie_len;
				break;
			}
		}
		tlv_buf_left -= (sizeof(MrvlIEtypesHeader_t) + tlv_len);
		tlv = (MrvlIEtypesHeader_t *)((t_u8 *)tlv + tlv_len +
					      sizeof(MrvlIEtypesHeader_t));
	}
	PRINTM(MEVENT, "STA assoc event len=%d\n", pevent->event_len);
	DBG_HEXDUMP(MCMD_D, "STA assoc event", pevent->event_buf,
		    pevent->event_len);
	wlan_recv_event(priv, pevent->event_id, pevent);
	LEAVE();
	return ret;
}

/**
 *  @brief This function prepares command of uap operation control
 *
 *  @param pmpriv		A pointer to mlan_private structure
 *  @param cmd          A pointer to HostCmd_DS_COMMAND structure
 *  @param cmd_action   the action: GET or SET
 *  @param pdata_buf    A pointer to data buffer
 *  @return         MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_cmd_oper_ctrl(pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *cmd,
		       IN t_u16 cmd_action, IN t_void *pdata_buf)
{
	HostCmd_DS_UAP_OPER_CTRL *poper_ctl =
		(HostCmd_DS_UAP_OPER_CTRL *) & cmd->params.uap_oper_ctrl;
	mlan_ds_bss *bss = (mlan_ds_bss *)pdata_buf;
	mlan_uap_oper_ctrl *uap_oper_ctrl = &bss->param.ap_oper_ctrl;
	Band_Config_t *bandcfg = MNULL;

	ENTER();

	cmd->command = wlan_cpu_to_le16(HOST_CMD_APCMD_OPER_CTRL);
	cmd->size =
		wlan_cpu_to_le16(sizeof(HostCmd_DS_UAP_OPER_CTRL) + S_DS_GEN);
	poper_ctl->action = wlan_cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		poper_ctl->ctrl = wlan_cpu_to_le16(uap_oper_ctrl->ctrl_value);
		if (uap_oper_ctrl->ctrl_value == 2) {
			poper_ctl->chan_opt =
				wlan_cpu_to_le16(uap_oper_ctrl->chan_opt);
			if (uap_oper_ctrl->chan_opt == 3) {
				poper_ctl->channel_band.header.type =
					wlan_cpu_to_le16
					(TLV_TYPE_UAP_CHAN_BAND_CONFIG);
				poper_ctl->channel_band.header.len =
					wlan_cpu_to_le16(sizeof
							 (MrvlIEtypes_channel_band_t)
							 -
							 sizeof
							 (MrvlIEtypesHeader_t));
				bandcfg = &poper_ctl->channel_band.bandcfg;
				if (uap_oper_ctrl->channel > 14)
					bandcfg->chanBand = BAND_5GHZ;
				bandcfg->chanWidth = uap_oper_ctrl->band_cfg;
				if (bandcfg->chanWidth)
					bandcfg->chan2Offset =
						wlan_get_second_channel_offset
						(uap_oper_ctrl->channel);
				bandcfg->scanMode = SCAN_MODE_MANUAL;
				poper_ctl->channel_band.channel =
					uap_oper_ctrl->channel;
			}
		}
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/**
 *  @brief This function handles the command response of uap operation control
 *
 *  @param pmpriv       A pointer to mlan_private structure
 *  @param resp         A pointer to HostCmd_DS_COMMAND
 *  @param pioctl_buf   A pointer to mlan_ioctl_req structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
static mlan_status
wlan_uap_ret_oper_ctrl(IN pmlan_private pmpriv,
		       IN HostCmd_DS_COMMAND *resp,
		       IN mlan_ioctl_req *pioctl_buf)
{
	HostCmd_DS_UAP_OPER_CTRL *poper_ctl =
		(HostCmd_DS_UAP_OPER_CTRL *) & resp->params.uap_oper_ctrl;
	mlan_ds_bss *bss = MNULL;
	mlan_uap_oper_ctrl *uap_oper_ctrl = MNULL;
	Band_Config_t *bandcfg = MNULL;

	ENTER();

	if (pioctl_buf && pioctl_buf->action == MLAN_ACT_GET) {
		bss = (mlan_ds_bss *)pioctl_buf->pbuf;
		uap_oper_ctrl =
			(mlan_uap_oper_ctrl *) & bss->param.ap_oper_ctrl;
		uap_oper_ctrl->ctrl_value = wlan_le16_to_cpu(poper_ctl->ctrl);
		uap_oper_ctrl->chan_opt = wlan_le16_to_cpu(poper_ctl->chan_opt);
		uap_oper_ctrl->channel = poper_ctl->channel_band.channel;
		bandcfg = &poper_ctl->channel_band.bandcfg;
		uap_oper_ctrl->band_cfg = bandcfg->chanWidth;
	}

	LEAVE();
	return MLAN_STATUS_SUCCESS;
}

/********************************************************
			Global Functions
********************************************************/
/**
 *  @brief This function prepare the command before sending to firmware.
 *
 *  @param priv       A pointer to mlan_private structure
 *  @param cmd_no       Command number
 *  @param cmd_action   Command action: GET or SET
 *  @param cmd_oid      Cmd oid: treated as sub command
 *  @param pioctl_buf   A pointer to MLAN IOCTL Request buffer
 *  @param pdata_buf    A pointer to information buffer
 *  @param pcmd_buf      A pointer to cmd buf
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_uap_prepare_cmd(IN t_void *priv,
			 IN t_u16 cmd_no,
			 IN t_u16 cmd_action,
			 IN t_u32 cmd_oid,
			 IN t_void *pioctl_buf,
			 IN t_void *pdata_buf, IN t_void *pcmd_buf)
{
	HostCmd_DS_COMMAND *cmd_ptr = (HostCmd_DS_COMMAND *)pcmd_buf;
	mlan_private *pmpriv = (mlan_private *)priv;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_ioctl_req pioctl_req = (mlan_ioctl_req *)pioctl_buf;

	ENTER();

	/* Prepare command */
	switch (cmd_no) {
	case HostCmd_CMD_SOFT_RESET:
	case HOST_CMD_APCMD_BSS_STOP:
	case HOST_CMD_APCMD_BSS_START:
	case HOST_CMD_APCMD_SYS_INFO:
	case HOST_CMD_APCMD_SYS_RESET:
	case HOST_CMD_APCMD_STA_LIST:
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		cmd_ptr->size = wlan_cpu_to_le16(S_DS_GEN);
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		if ((HOST_CMD_APCMD_BSS_START == cmd_no) &&
		    IsAuthenticatorEnabled(pmpriv->psapriv))
			AuthenticatorBssConfig(pmpriv->psapriv, MNULL, 1, 0, 0);
#endif
		break;
	case HOST_CMD_APCMD_SYS_CONFIGURE:
		ret = wlan_uap_cmd_sys_configure(pmpriv, cmd_ptr, cmd_action,
						 (pmlan_ioctl_req)pioctl_buf,
						 pdata_buf);
		break;
	case HostCmd_CMD_802_11_PS_MODE_ENH:
		ret = wlan_cmd_enh_power_mode(pmpriv, cmd_ptr, cmd_action,
					      (t_u16)cmd_oid, pdata_buf);
		break;
	case HostCmd_CMD_SDIO_GPIO_INT_CONFIG:
		ret = wlan_cmd_sdio_gpio_int(pmpriv, cmd_ptr, cmd_action,
					     pdata_buf);
		break;
	case HostCmd_CMD_FUNC_INIT:
		if (pmpriv->adapter->hw_status == WlanHardwareStatusReset)
			pmpriv->adapter->hw_status =
				WlanHardwareStatusInitializing;
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		cmd_ptr->size = wlan_cpu_to_le16(S_DS_GEN);
		break;
	case HostCmd_CMD_FUNC_SHUTDOWN:
		pmpriv->adapter->hw_status = WlanHardwareStatusReset;
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		cmd_ptr->size = wlan_cpu_to_le16(S_DS_GEN);
		break;
	case HostCmd_CMD_CFG_DATA:
		ret = wlan_cmd_cfg_data(pmpriv, cmd_ptr, cmd_action, cmd_oid,
					pdata_buf);
		break;
	case HostCmd_CMD_MAC_CONTROL:
		ret = wlan_cmd_mac_control(pmpriv, cmd_ptr, cmd_action,
					   pdata_buf);
		break;
	case HostCmd_CMD_802_11_SNMP_MIB:
		ret = wlan_uap_cmd_snmp_mib(pmpriv, cmd_ptr, cmd_action,
					    cmd_oid,
					    (pmlan_ioctl_req)pioctl_buf,
					    pdata_buf);
		break;
	case HostCmd_CMD_802_11_GET_LOG:
		ret = wlan_uap_cmd_802_11_get_log(pmpriv, cmd_ptr);
		break;
	case HostCmd_CMD_802_11D_DOMAIN_INFO:
		ret = wlan_cmd_802_11d_domain_info(pmpriv, cmd_ptr, cmd_action);
		break;
	case HostCmd_CMD_CHAN_REPORT_REQUEST:
		ret = wlan_11h_cmd_process(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HOST_CMD_APCMD_STA_DEAUTH:
		ret = wlan_uap_cmd_sta_deauth(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HostCmd_CMD_802_11_KEY_MATERIAL:
		ret = wlan_uap_cmd_key_material(pmpriv, cmd_ptr, cmd_action,
						cmd_oid, pdata_buf);
		break;
	case HostCmd_CMD_GET_HW_SPEC:
		ret = wlan_cmd_get_hw_spec(pmpriv, cmd_ptr);
		break;
	case HostCmd_CMD_SDIO_SP_RX_AGGR_CFG:
		ret = wlan_cmd_sdio_rx_aggr_cfg(cmd_ptr, cmd_action, pdata_buf);
		break;
	case HostCmd_CMD_802_11_HS_CFG_ENH:
		ret = wlan_uap_cmd_802_11_hs_cfg(pmpriv, cmd_ptr, cmd_action,
						 (hs_config_param *)pdata_buf);
		break;
	case HostCmd_CMD_HS_WAKEUP_REASON:
		ret = wlan_cmd_hs_wakeup_reason(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HostCmd_CMD_802_11_FW_WAKE_METHOD:
		ret = wlan_cmd_802_11_fw_wakeup_method(pmpriv, cmd_ptr,
						       cmd_action,
						       (t_u16 *)pdata_buf);
		break;
	case HostCmd_CMD_802_11_ROBUSTCOEX:
		ret = wlan_cmd_robustcoex(pmpriv, cmd_ptr, cmd_action,
					  (t_u16 *)pdata_buf);
		break;
	case HostCmd_CMD_RECONFIGURE_TX_BUFF:
		ret = wlan_cmd_recfg_tx_buf(pmpriv, cmd_ptr, cmd_action,
					    pdata_buf);
		break;
	case HostCmd_CMD_AMSDU_AGGR_CTRL:
		ret = wlan_cmd_amsdu_aggr_ctrl(pmpriv, cmd_ptr, cmd_action,
					       pdata_buf);
		break;
	case HostCmd_CMD_11N_CFG:
		ret = wlan_cmd_11n_cfg(pmpriv, cmd_ptr, cmd_action, pdata_buf);
		break;
	case HostCmd_CMD_11N_ADDBA_REQ:
		ret = wlan_cmd_11n_addba_req(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HostCmd_CMD_11N_DELBA:
		ret = wlan_cmd_11n_delba(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HostCmd_CMD_11N_ADDBA_RSP:
		ret = wlan_cmd_11n_addba_rspgen(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HostCmd_CMD_REJECT_ADDBA_REQ:
		ret = wlan_cmd_reject_addba_req(pmpriv, cmd_ptr, cmd_action,
						pdata_buf);
		break;
	case HostCmd_CMD_TX_BF_CFG:
		ret = wlan_cmd_tx_bf_cfg(pmpriv, cmd_ptr, cmd_action,
					 pdata_buf);
		break;
#if defined(WIFI_DIRECT_SUPPORT)
	case HostCmd_CMD_SET_BSS_MODE:
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		if (pdata_buf)
			cmd_ptr->params.bss_mode.con_type = *(t_u8 *)pdata_buf;
		else
			cmd_ptr->params.bss_mode.con_type =
				BSS_MODE_WIFIDIRECT_GO;
		cmd_ptr->size =
			wlan_cpu_to_le16(sizeof(HostCmd_DS_SET_BSS_MODE) +
					 S_DS_GEN);
		ret = MLAN_STATUS_SUCCESS;
		break;
#endif
	case HostCmd_CMD_VERSION_EXT:
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		cmd_ptr->params.verext.version_str_sel =
			(t_u8)(*((t_u32 *)pdata_buf));
		cmd_ptr->size =
			wlan_cpu_to_le16(sizeof(HostCmd_DS_VERSION_EXT) +
					 S_DS_GEN);
		ret = MLAN_STATUS_SUCCESS;
		break;
	case HostCmd_CMD_RX_MGMT_IND:
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		cmd_ptr->params.rx_mgmt_ind.action =
			wlan_cpu_to_le16(cmd_action);
		cmd_ptr->params.rx_mgmt_ind.mgmt_subtype_mask =
			(t_u32)(*((t_u32 *)pdata_buf));
		cmd_ptr->size =
			wlan_cpu_to_le16(sizeof(HostCmd_DS_RX_MGMT_IND) +
					 S_DS_GEN);
		break;
	case HostCmd_CMD_CFG_TX_DATA_PAUSE:
		ret = wlan_uap_cmd_txdatapause(pmpriv, cmd_ptr, cmd_action,
					       pdata_buf);
		break;
	case HostCmd_CMD_802_11_RADIO_CONTROL:
		ret = wlan_cmd_802_11_radio_control(pmpriv, cmd_ptr, cmd_action,
						    pdata_buf);
		break;
	case HostCmd_CMD_TX_RATE_CFG:
		ret = wlan_cmd_tx_rate_cfg(pmpriv, cmd_ptr, cmd_action,
					   pdata_buf);
		break;
	case HostCmd_CMD_802_11_TX_RATE_QUERY:
		cmd_ptr->command =
			wlan_cpu_to_le16(HostCmd_CMD_802_11_TX_RATE_QUERY);
		cmd_ptr->size =
			wlan_cpu_to_le16(sizeof(HostCmd_TX_RATE_QUERY) +
					 S_DS_GEN);
		pmpriv->tx_rate = 0;
		ret = MLAN_STATUS_SUCCESS;
		break;
	case HostCmd_CMD_802_11_REMAIN_ON_CHANNEL:
		ret = wlan_cmd_remain_on_channel(pmpriv, cmd_ptr, cmd_action,
						 pdata_buf);
		break;

#ifdef WIFI_DIRECT_SUPPORT
	case HOST_CMD_WIFI_DIRECT_MODE_CONFIG:
		ret = wlan_cmd_wifi_direct_mode(pmpriv, cmd_ptr, cmd_action,
						pdata_buf);
		break;
	case HOST_CMD_P2P_PARAMS_CONFIG:
		ret = wlan_cmd_p2p_params_config(pmpriv, cmd_ptr, cmd_action,
						 pdata_buf);
		break;
#endif
	case HostCmd_CMD_MAC_REG_ACCESS:
	case HostCmd_CMD_BBP_REG_ACCESS:
	case HostCmd_CMD_RF_REG_ACCESS:
	case HostCmd_CMD_CAU_REG_ACCESS:
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
		ret = wlan_cmd_reg_access(cmd_ptr, cmd_action, pdata_buf);
		break;
	case HostCmd_CMD_MEM_ACCESS:
		ret = wlan_cmd_mem_access(cmd_ptr, cmd_action, pdata_buf);
		break;
	case HostCmd_CMD_WMM_QUEUE_CONFIG:
		ret = wlan_cmd_wmm_queue_config(pmpriv, cmd_ptr, pdata_buf);
		break;
	case HostCmd_CMD_MULTI_CHAN_CONFIG:
		ret = wlan_cmd_multi_chan_cfg(pmpriv, cmd_ptr, cmd_action,
					      pdata_buf);
		break;
	case HostCmd_CMD_MULTI_CHAN_POLICY:
		ret = wlan_cmd_multi_chan_policy(pmpriv, cmd_ptr, cmd_action,
						 pdata_buf);
		break;
	case HostCmd_CMD_DRCS_CONFIG:
		ret = wlan_cmd_drcs_cfg(pmpriv, cmd_ptr, cmd_action, pdata_buf);
		break;
#ifdef RX_PACKET_COALESCE
	case HostCmd_CMD_RX_PKT_COALESCE_CFG:
		ret = wlan_cmd_rx_pkt_coalesce_cfg(pmpriv, cmd_ptr, cmd_action,
						   pdata_buf);
		break;
#endif
	case HOST_CMD_APCMD_OPER_CTRL:
		ret = wlan_uap_cmd_oper_ctrl(pmpriv, cmd_ptr, cmd_action,
					     pdata_buf);
		break;

	case HostCmd_CMD_INDEPENDENT_RESET_CFG:
		ret = wlan_cmd_ind_rst_cfg(cmd_ptr, cmd_action, pdata_buf);
		break;
	case HostCmd_CMD_GET_TSF:
		ret = wlan_cmd_get_tsf(pmpriv, cmd_ptr, cmd_action);
		break;

	case HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT:
		ret = wlan_cmd_ps_inactivity_timeout(pmpriv, cmd_ptr,
						     cmd_action, pdata_buf);
		break;

	case HostCmd_CMD_HOST_CLOCK_CFG:
		ret = wlan_cmd_host_clock_cfg(cmd_ptr, cmd_action, pdata_buf);
		break;
	case HostCmd_CMD_CHAN_REGION_CFG:
		cmd_ptr->command = wlan_cpu_to_le16(cmd_no);
		cmd_ptr->size =
			wlan_cpu_to_le16(sizeof(HostCmd_DS_CHAN_REGION_CFG) +
					 S_DS_GEN);
		cmd_ptr->params.reg_cfg.action = wlan_cpu_to_le16(cmd_action);
		break;
	case HostCmd_CMD_802_11_NET_MONITOR:
		ret = wlan_cmd_net_monitor(cmd_ptr, cmd_action, pdata_buf);
		break;
#if defined(SYSKT_MULTI) && defined(OOB_WAKEUP) || defined(SUSPEND_SDIO_PULL_DOWN)
	case HostCmd_CMD_SDIO_PULL_CTRL:
		ret = wlan_cmd_sdio_pull_ctl(pmpriv, cmd_ptr, cmd_action);
		break;
#endif
	case HostCmd_CMD_FW_DUMP_EVENT:
		ret = wlan_cmd_fw_dump_event(pmpriv, cmd_ptr, cmd_action,
					     pdata_buf);
		break;
	case HostCmd_CMD_BOOT_SLEEP:
		ret = wlan_cmd_boot_sleep(pmpriv, cmd_ptr, cmd_action,
					  pdata_buf);
		break;
	default:
		PRINTM(MERROR, "PREP_CMD: unknown command- %#x\n", cmd_no);
		if (pioctl_req)
			pioctl_req->status_code = MLAN_ERROR_CMD_INVALID;
		ret = MLAN_STATUS_FAILURE;
		break;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles the AP mode command response
 *
 *  @param priv             A pointer to mlan_private structure
 *  @param cmdresp_no       cmd no
 *  @param pcmd_buf         cmdresp buf
 *  @param pioctl           A pointer to ioctl buf
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_uap_process_cmdresp(IN t_void *priv,
			     IN t_u16 cmdresp_no,
			     IN t_void *pcmd_buf, IN t_void *pioctl)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	mlan_private *pmpriv = (mlan_private *)priv;
	HostCmd_DS_COMMAND *resp = (HostCmd_DS_COMMAND *)pcmd_buf;
	mlan_ioctl_req *pioctl_buf = (mlan_ioctl_req *)pioctl;
	mlan_adapter *pmadapter = pmpriv->adapter;
	int ctr;
	wlan_dfs_device_state_t *pstate_dfs = (wlan_dfs_device_state_t *)
		&pmpriv->adapter->state_dfs;
	t_u32 sec, usec;
	ENTER();

	/* If the command is not successful, cleanup and return failure */
	if (resp->result != HostCmd_RESULT_OK) {
		ret = uap_process_cmdresp_error(pmpriv, resp, pioctl_buf);
		LEAVE();
		return ret;
	}

	/* Command successful, handle response */
	switch (cmdresp_no) {
	case HOST_CMD_APCMD_BSS_STOP:
		pmpriv->uap_bss_started = MFALSE;
		/* Timestamp update is required because bss_start after skip_cac
		 * enabled should not select non-current channel just because
		 * timestamp got expired
		 */
		if (!pmpriv->intf_state_11h.is_11h_host &&
		    !pstate_dfs->dfs_check_pending &&
		    pstate_dfs->dfs_check_channel) {
			pmpriv->adapter->callbacks.moal_get_system_time(pmpriv->
									adapter->
									pmoal_handle,
									&sec,
									&usec);
			pstate_dfs->dfs_report_time_sec = sec;
		}
		if (pmpriv->intf_state_11h.is_11h_host)
			pmpriv->intf_state_11h.tx_disabled = MFALSE;
		else
			wlan_11h_check_update_radar_det_state(pmpriv);

		if (pmpriv->adapter->state_rdh.stage == RDH_STOP_INTFS)
			wlan_11h_radar_detected_callback((t_void *)pmpriv);
		wlan_coex_ampdu_rxwinsize(pmadapter);
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		if (IsAuthenticatorEnabled(pmpriv->psapriv)) {
			AuthenticatorBssConfig(pmpriv->psapriv, MNULL, 0, 1, 0);
			AuthenticatorkeyClear(pmpriv->psapriv);
		}
#endif
		pmpriv->uap_host_based = MFALSE;
		break;
	case HOST_CMD_APCMD_BSS_START:
		if (!pmpriv->intf_state_11h.is_11h_host &&
		    pmpriv->adapter->state_rdh.stage == RDH_RESTART_INTFS)
			wlan_11h_radar_detected_callback((t_void *)pmpriv);
		/* Stop pps_uapsd_mode once bss_start */
		pmpriv->adapter->tx_lock_flag = MFALSE;
		pmpriv->adapter->pps_uapsd_mode = MFALSE;
		pmpriv->adapter->delay_null_pkt = MFALSE;
		/* Clear AMSDU statistics */
		pmpriv->amsdu_rx_cnt = 0;
		pmpriv->amsdu_tx_cnt = 0;
		pmpriv->msdu_in_rx_amsdu_cnt = 0;
		pmpriv->msdu_in_tx_amsdu_cnt = 0;
		break;
	case HOST_CMD_APCMD_SYS_RESET:
		pmpriv->uap_bss_started = MFALSE;
		pmpriv->uap_host_based = MFALSE;
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		AuthenitcatorInitBssConfig(pmpriv->psapriv);
#endif
		ret = wlan_uap_ret_sys_reset(pmpriv, resp, pioctl_buf);
		wlan_11h_check_update_radar_det_state(pmpriv);
		wlan_coex_ampdu_rxwinsize(pmadapter);
		break;
	case HOST_CMD_APCMD_SYS_INFO:
		break;
	case HOST_CMD_APCMD_SYS_CONFIGURE:
		ret = wlan_uap_ret_sys_config(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_PS_MODE_ENH:
		ret = wlan_ret_enh_power_mode(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_SDIO_GPIO_INT_CONFIG:
		break;
	case HostCmd_CMD_FUNC_INIT:
	case HostCmd_CMD_FUNC_SHUTDOWN:
		break;
	case HostCmd_CMD_802_11_SNMP_MIB:
		ret = wlan_uap_ret_snmp_mib(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_GET_LOG:
		ret = wlan_uap_ret_get_log(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11D_DOMAIN_INFO:
		ret = wlan_ret_802_11d_domain_info(pmpriv, resp);
		break;
	case HostCmd_CMD_CHAN_REPORT_REQUEST:
		ret = wlan_11h_cmdresp_process(pmpriv, resp);
		break;
	case HOST_CMD_APCMD_STA_DEAUTH:
		break;
	case HostCmd_CMD_802_11_KEY_MATERIAL:
		break;
	case HOST_CMD_APCMD_STA_LIST:
		ret = wlan_uap_ret_sta_list(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_GET_HW_SPEC:
		ret = wlan_ret_get_hw_spec(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_SDIO_SP_RX_AGGR_CFG:
		ret = wlan_ret_sdio_rx_aggr_cfg(pmpriv, resp);
		break;
	case HostCmd_CMD_CFG_DATA:
		ret = wlan_ret_cfg_data(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_MAC_CONTROL:
		ret = wlan_ret_mac_control(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_HS_CFG_ENH:
		ret = wlan_ret_802_11_hs_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_HS_WAKEUP_REASON:
		ret = wlan_ret_hs_wakeup_reason(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_FW_WAKE_METHOD:
		ret = wlan_ret_fw_wakeup_method(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_ROBUSTCOEX:
		break;
	case HostCmd_CMD_11N_ADDBA_REQ:
		ret = wlan_ret_11n_addba_req(pmpriv, resp);
		break;
	case HostCmd_CMD_11N_DELBA:
		ret = wlan_ret_11n_delba(pmpriv, resp);
		break;
	case HostCmd_CMD_11N_ADDBA_RSP:
		ret = wlan_ret_11n_addba_resp(pmpriv, resp);
		break;
	case HostCmd_CMD_SET_BSS_MODE:
		break;
	case HostCmd_CMD_802_11_NET_MONITOR:
		ret = wlan_ret_net_monitor(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_RECONFIGURE_TX_BUFF:
		wlan_set_tx_pause_flag(pmpriv, MFALSE);

		pmadapter->tx_buf_size =
			(t_u16)wlan_le16_to_cpu(resp->params.tx_buf.buff_size);
		pmadapter->tx_buf_size =
			(pmadapter->tx_buf_size / MLAN_SDIO_BLOCK_SIZE) *
			MLAN_SDIO_BLOCK_SIZE;
		pmadapter->curr_tx_buf_size = pmadapter->tx_buf_size;
		pmadapter->mp_end_port =
			wlan_le16_to_cpu(resp->params.tx_buf.mp_end_port);
		pmadapter->mp_data_port_mask = DATA_PORT_MASK;

		for (ctr = 1; ctr <= MAX_PORT - pmadapter->mp_end_port; ctr++) {
			pmadapter->mp_data_port_mask &=
				~(1 << (MAX_PORT - ctr));
		}
		pmadapter->curr_wr_port = 0;
		pmadapter->mpa_tx.pkt_aggr_limit =
			MIN(SDIO_MP_AGGR_DEF_PKT_LIMIT,
			    (pmadapter->mp_end_port >> 1));
		PRINTM(MCMND, "end port %d, data port mask %x\n",
		       wlan_le16_to_cpu(resp->params.tx_buf.mp_end_port),
		       pmadapter->mp_data_port_mask);
		PRINTM(MCMND, "max_tx_buf_size=%d, tx_buf_size=%d\n",
		       pmadapter->max_tx_buf_size, pmadapter->tx_buf_size);
		break;
	case HostCmd_CMD_AMSDU_AGGR_CTRL:
		ret = wlan_ret_amsdu_aggr_ctrl(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_11N_CFG:
		ret = wlan_ret_11n_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_REJECT_ADDBA_REQ:
		ret = wlan_ret_reject_addba_req(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_TX_BF_CFG:
		ret = wlan_ret_tx_bf_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_VERSION_EXT:
		ret = wlan_ret_ver_ext(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_RX_MGMT_IND:
		ret = wlan_ret_rx_mgmt_ind(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_CFG_TX_DATA_PAUSE:
		ret = wlan_uap_ret_txdatapause(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_RADIO_CONTROL:
		ret = wlan_ret_802_11_radio_control(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_TX_RATE_CFG:
		ret = wlan_ret_tx_rate_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_TX_RATE_QUERY:
		ret = wlan_ret_802_11_tx_rate_query(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_REMAIN_ON_CHANNEL:
		ret = wlan_ret_remain_on_channel(pmpriv, resp, pioctl_buf);
		break;
#ifdef WIFI_DIRECT_SUPPORT
	case HOST_CMD_WIFI_DIRECT_MODE_CONFIG:
		ret = wlan_ret_wifi_direct_mode(pmpriv, resp, pioctl_buf);
		break;
	case HOST_CMD_P2P_PARAMS_CONFIG:
		ret = wlan_ret_p2p_params_config(pmpriv, resp, pioctl_buf);
		break;
#endif
	case HostCmd_CMD_MAC_REG_ACCESS:
	case HostCmd_CMD_BBP_REG_ACCESS:
	case HostCmd_CMD_RF_REG_ACCESS:
	case HostCmd_CMD_CAU_REG_ACCESS:
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
		ret = wlan_ret_reg_access(pmpriv->adapter, cmdresp_no, resp,
					  pioctl_buf);
		break;
	case HostCmd_CMD_MEM_ACCESS:
		ret = wlan_ret_mem_access(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_WMM_QUEUE_CONFIG:
		ret = wlan_ret_wmm_queue_config(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_MULTI_CHAN_CONFIG:
		ret = wlan_ret_multi_chan_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_MULTI_CHAN_POLICY:
		ret = wlan_ret_multi_chan_policy(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_DRCS_CONFIG:
		ret = wlan_ret_drcs_cfg(pmpriv, resp, pioctl_buf);
		break;
#ifdef RX_PACKET_COALESCE
	case HostCmd_CMD_RX_PKT_COALESCE_CFG:
		ret = wlan_ret_rx_pkt_coalesce_cfg(pmpriv, resp, pioctl_buf);
		break;
#endif
	case HOST_CMD_APCMD_OPER_CTRL:
		ret = wlan_uap_ret_oper_ctrl(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_INDEPENDENT_RESET_CFG:
		ret = wlan_ret_ind_rst_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_802_11_PS_INACTIVITY_TIMEOUT:
		break;
	case HostCmd_CMD_GET_TSF:
		ret = wlan_ret_get_tsf(pmpriv, resp, pioctl_buf);
		break;

	case HostCmd_CMD_HOST_CLOCK_CFG:
		ret = wlan_ret_host_clock_cfg(pmpriv, resp, pioctl_buf);
		break;
	case HostCmd_CMD_CHAN_REGION_CFG:
		ret = wlan_ret_chan_region_cfg(pmpriv, resp, pioctl_buf);
		break;
#if defined(SYSKT_MULTI) && defined(OOB_WAKEUP) || defined(SUSPEND_SDIO_PULL_DOWN)
	case HostCmd_CMD_SDIO_PULL_CTRL:
		break;
#endif
	case HostCmd_CMD_BOOT_SLEEP:
		ret = wlan_ret_boot_sleep(pmpriv, resp, pioctl_buf);
		break;
	default:
		PRINTM(MERROR, "CMD_RESP: Unknown command response %#x\n",
		       resp->command);
		if (pioctl_buf)
			pioctl_buf->status_code = MLAN_ERROR_CMD_RESP_FAIL;
		break;
	}
	LEAVE();
	return ret;
}

/**
 *  @brief This function handles events generated by firmware
 *
 *  @param priv		A pointer to mlan_private structure
 *
 *  @return		MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_uap_process_event(IN t_void *priv)
{
	pmlan_private pmpriv = (pmlan_private)priv;
	pmlan_adapter pmadapter = pmpriv->adapter;
	pmlan_callbacks pcb = &pmadapter->callbacks;
	mlan_status ret = MLAN_STATUS_SUCCESS;
	t_u32 eventcause = pmadapter->event_cause;
	pmlan_buffer pmbuf = pmadapter->pmlan_buffer_event;
	t_u8 *event_buf = MNULL;
	mlan_event *pevent = MNULL;
	t_u8 sta_addr[MLAN_MAC_ADDR_LENGTH];
	sta_node *sta_ptr = MNULL;
	t_u8 i = 0;
	t_u8 channel = 0;
	MrvlIEtypes_channel_band_t *pchan_info = MNULL;
	chan_band_info *pchan_band_info = MNULL;
	event_exceed_max_p2p_conn *event_excd_p2p = MNULL;

	ENTER();

	/* Event length check */
	if (pmbuf && (pmbuf->data_len - sizeof(eventcause)) > MAX_EVENT_SIZE) {
		pmbuf->status_code = MLAN_ERROR_PKT_SIZE_INVALID;
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	/* Allocate memory for event buffer */
	ret = pcb->moal_malloc(pmadapter->pmoal_handle, MAX_EVENT_SIZE,
			       MLAN_MEM_DEF, &event_buf);
	if ((ret != MLAN_STATUS_SUCCESS) || !event_buf) {
		PRINTM(MERROR, "Could not allocate buffer for event buf\n");
		if (pmbuf)
			pmbuf->status_code = MLAN_ERROR_NO_MEM;
		goto done;
	}
	pevent = (pmlan_event)event_buf;
	memset(pmadapter, &pevent->event_id, 0, sizeof(pevent->event_id));

	if (eventcause != EVENT_PS_SLEEP && eventcause != EVENT_PS_AWAKE &&
	    pmbuf->data_len > sizeof(eventcause))
		DBG_HEXDUMP(MEVT_D, "EVENT", pmbuf->pbuf + pmbuf->data_offset,
			    pmbuf->data_len);

	switch (eventcause) {
	case EVENT_MICRO_AP_BSS_START:
		PRINTM(MEVENT, "EVENT: MICRO_AP_BSS_START\n");
		pmpriv->uap_bss_started = MTRUE;
		memcpy(pmadapter, pmpriv->curr_addr, pmadapter->event_body + 2,
		       MLAN_MAC_ADDR_LENGTH);
		pevent->event_id = MLAN_EVENT_ID_UAP_FW_BSS_START;
		wlan_check_uap_capability(pmpriv, pmbuf);
		wlan_coex_ampdu_rxwinsize(pmadapter);
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		if (IsAuthenticatorEnabled(pmpriv->psapriv))
			AuthenticatorKeyMgmtInit(pmpriv->psapriv,
						 pmpriv->curr_addr);
#endif
		break;
	case EVENT_MICRO_AP_BSS_ACTIVE:
		PRINTM(MEVENT, "EVENT: MICRO_AP_BSS_ACTIVE\n");
		pmpriv->media_connected = MTRUE;
		pmpriv->port_open = MTRUE;
		pevent->event_id = MLAN_EVENT_ID_UAP_FW_BSS_ACTIVE;
		break;
	case EVENT_MICRO_AP_BSS_IDLE:
		PRINTM(MEVENT, "EVENT: MICRO_AP_BSS_IDLE\n");
		pevent->event_id = MLAN_EVENT_ID_UAP_FW_BSS_IDLE;
		pmpriv->media_connected = MFALSE;
		wlan_clean_txrx(pmpriv);
		wlan_notify_station_deauth(pmpriv);
		wlan_delete_station_list(pmpriv);
		pmpriv->port_open = MFALSE;
		pmpriv->tx_pause = MFALSE;
		break;
	case EVENT_PS_AWAKE:
		PRINTM(MINFO, "EVENT: AWAKE\n");
		PRINTM(MEVENT, "||");
		/* Handle unexpected PS AWAKE event */
		if (pmadapter->ps_state == PS_STATE_SLEEP_CFM)
			break;
		pmadapter->pm_wakeup_card_req = MFALSE;
		pmadapter->pm_wakeup_fw_try = MFALSE;
		pmadapter->ps_state = PS_STATE_AWAKE;

		break;
	case EVENT_PS_SLEEP:
		PRINTM(MINFO, "EVENT: SLEEP\n");
		PRINTM(MEVENT, "__");
		/* Handle unexpected PS SLEEP event */
		if (pmadapter->ps_state == PS_STATE_SLEEP_CFM)
			break;
		pmadapter->ps_state = PS_STATE_PRE_SLEEP;
		wlan_check_ps_cond(pmadapter);
		break;
	case EVENT_MICRO_AP_STA_ASSOC:
		wlan_process_sta_assoc_event(pmpriv, pevent, pmbuf);
		memcpy(pmadapter, sta_addr, pmadapter->event_body + 2,
		       MLAN_MAC_ADDR_LENGTH);
		sta_ptr = wlan_add_station_entry(pmpriv, sta_addr);
		PRINTM(MMSG, "wlan: EVENT: MICRO_AP_STA_ASSOC " MACSTR "\n",
		       MAC2STR(sta_addr));
		if (!sta_ptr)
			break;
		if (pmpriv->is_11n_enabled
#ifdef DRV_EMBEDDED_AUTHENTICATOR
		    || IsAuthenticatorEnabled(pmpriv->psapriv)
#endif
			) {
			wlan_check_sta_capability(pmpriv, pmbuf, sta_ptr);
			for (i = 0; i < MAX_NUM_TID; i++) {
				if (sta_ptr->is_11n_enabled)
					sta_ptr->ampdu_sta[i] =
						pmpriv->aggr_prio_tbl[i].
						ampdu_user;
				else
					sta_ptr->ampdu_sta[i] =
						BA_STREAM_NOT_ALLOWED;
			}
			memset(pmadapter, sta_ptr->rx_seq, 0xff,
			       sizeof(sta_ptr->rx_seq));
		}
		if (pmpriv->sec_info.wapi_enabled)
			wlan_update_wapi_info_tlv(pmpriv, pmbuf);
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	   /**enter authenticator*/
		if (IsAuthenticatorEnabled(pmpriv->psapriv))
			AuthenticatorSendEapolPacket(pmpriv->psapriv,
						     sta_ptr->
						     cm_connectioninfo);
#endif
		pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
		break;
	case EVENT_MICRO_AP_STA_DEAUTH:
		pevent->event_id = MLAN_EVENT_ID_UAP_FW_STA_DISCONNECT;
		pevent->bss_index = pmpriv->bss_index;
		pevent->event_len = pmbuf->data_len - 4;
		/* skip event length field */
		memcpy(pmadapter, (t_u8 *)pevent->event_buf,
		       pmbuf->pbuf + pmbuf->data_offset + 4, pevent->event_len);
		wlan_recv_event(pmpriv, pevent->event_id, pevent);
		memcpy(pmadapter, sta_addr, pmadapter->event_body + 2,
		       MLAN_MAC_ADDR_LENGTH);
		PRINTM(MMSG, "wlan: EVENT: MICRO_AP_STA_DEAUTH " MACSTR "\n",
		       MAC2STR(sta_addr));
		if (pmpriv->is_11n_enabled) {
			wlan_cleanup_reorder_tbl(pmpriv, sta_addr);
			wlan_11n_cleanup_txbastream_tbl(pmpriv, sta_addr);
		}
		wlan_wmm_delete_peer_ralist(pmpriv, sta_addr);
		wlan_delete_station_entry(pmpriv, sta_addr);
		pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
		break;
	case EVENT_HS_ACT_REQ:
		PRINTM(MEVENT, "EVENT: HS_ACT_REQ\n");
		ret = wlan_prepare_cmd(pmpriv, HostCmd_CMD_802_11_HS_CFG_ENH, 0,
				       0, MNULL, MNULL);
		break;
	case EVENT_ADDBA:
		PRINTM(MEVENT, "EVENT: ADDBA Request\n");
		if (pmpriv->media_connected == MTRUE)
			ret = wlan_prepare_cmd(pmpriv,
					       HostCmd_CMD_11N_ADDBA_RSP,
					       HostCmd_ACT_GEN_SET, 0, MNULL,
					       pmadapter->event_body);
		else
			PRINTM(MERROR,
			       "Ignore ADDBA Request event in BSS idle state\n");
		break;
	case EVENT_DELBA:
		PRINTM(MEVENT, "EVENT: DELBA Request\n");
		if (pmpriv->media_connected == MTRUE)
			wlan_11n_delete_bastream(pmpriv, pmadapter->event_body);
		else
			PRINTM(MERROR,
			       "Ignore DELBA Request event in BSS idle state\n");
		break;
	case EVENT_BA_STREAM_TIMEOUT:
		PRINTM(MEVENT, "EVENT:  BA Stream timeout\n");
		if (pmpriv->media_connected == MTRUE)
			wlan_11n_ba_stream_timeout(pmpriv,
						   (HostCmd_DS_11N_BATIMEOUT *)
						   pmadapter->event_body);
		else
			PRINTM(MERROR,
			       "Ignore BA Stream timeout event in BSS idle state\n");
		break;
	case EVENT_RXBA_SYNC:
		PRINTM(MEVENT, "EVENT:  RXBA_SYNC\n");
		wlan_11n_rxba_sync_event(pmpriv, pmadapter->event_body,
					 pmbuf->data_len - sizeof(eventcause));
		break;
	case EVENT_AMSDU_AGGR_CTRL:
		PRINTM(MEVENT, "EVENT:  AMSDU_AGGR_CTRL %d\n",
		       *(t_u16 *)pmadapter->event_body);
		pmadapter->tx_buf_size =
			MIN(pmadapter->curr_tx_buf_size,
			    wlan_le16_to_cpu(*(t_u16 *)pmadapter->event_body));
		PRINTM(MEVENT, "tx_buf_size %d\n", pmadapter->tx_buf_size);
		break;
	case EVENT_TX_DATA_PAUSE:
		PRINTM(MEVENT, "EVENT: TX_DATA_PAUSE\n");
		wlan_process_tx_pause_event(priv, pmbuf);
		break;
	case EVENT_RADAR_DETECTED:
		PRINTM(MEVENT, "EVENT: Radar Detected\n");

		/* Send as passthru first, this event can cause other events */
		memset(pmadapter, event_buf, 0x00, MAX_EVENT_SIZE);
		pevent->bss_index = pmpriv->bss_index;
		pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
		pevent->event_len = pmbuf->data_len;
		memcpy(pmadapter, (t_u8 *)pevent->event_buf,
		       pmbuf->pbuf + pmbuf->data_offset, pevent->event_len);
		wlan_recv_event(pmpriv, pevent->event_id, pevent);
		pevent->event_id = 0;	/* clear to avoid resending at end of fcn */

		if (!pmpriv->intf_state_11h.is_11h_host) {
			if (pmadapter->state_rdh.stage == RDH_OFF) {
				pmadapter->state_rdh.stage = RDH_CHK_INTFS;
				wlan_11h_radar_detected_handling(pmadapter,
								 pmpriv);
				if (pmpriv->uap_host_based)
					wlan_recv_event(priv,
							MLAN_EVENT_ID_FW_RADAR_DETECTED,
							MNULL);
			} else {
				PRINTM(MEVENT,
				       "Ignore Event Radar Detected - handling"
				       " already in progress.\n");
			}
		} else {

#ifdef DFS_TESTING_SUPPORT
			if (pmpriv->adapter->dfs_test_params.
			    no_channel_change_on_radar ||
			    pmpriv->adapter->dfs_test_params.
			    fixed_new_channel_on_radar) {
				if (pmadapter->state_rdh.stage == RDH_OFF ||
				    pmadapter->state_rdh.stage ==
				    RDH_SET_CUSTOM_IE) {
					pmadapter->state_rdh.stage =
						RDH_CHK_INTFS;
					wlan_11h_radar_detected_handling
						(pmadapter, pmpriv);
				} else
					PRINTM(MEVENT,
					       "Ignore Event Radar Detected - handling already in progress.\n");
			} else {
#endif
				pmpriv->intf_state_11h.tx_disabled = MTRUE;
				wlan_recv_event(priv,
						MLAN_EVENT_ID_FW_RADAR_DETECTED,
						MNULL);

#ifdef DFS_TESTING_SUPPORT
			}
#endif
		}

		break;
	case EVENT_CHANNEL_REPORT_RDY:
		PRINTM(MEVENT, "EVENT: Channel Report Ready\n");
		memset(pmadapter, event_buf, 0x00, MAX_EVENT_SIZE);
		/* Setup event buffer */
		pevent->bss_index = pmpriv->bss_index;
		pevent->event_id = MLAN_EVENT_ID_FW_CHANNEL_REPORT_RDY;
		pevent->event_len = pmbuf->data_len - sizeof(eventcause);
		/* Copy event data */
		memcpy(pmadapter, (t_u8 *)pevent->event_buf,
		       pmbuf->pbuf + pmbuf->data_offset + sizeof(eventcause),
		       pevent->event_len);
		/* Handle / pass event data, and free buffer */
		ret = wlan_11h_handle_event_chanrpt_ready(pmpriv, pevent);

		if (pmpriv->intf_state_11h.is_11h_host) {
			*((t_u8 *)pevent->event_buf) =
				pmpriv->adapter->state_dfs.dfs_radar_found;
			wlan_recv_event(pmpriv,
					MLAN_EVENT_ID_FW_CHANNEL_REPORT_RDY,
					pevent);
		} else {
			/* Send up this Event to unblock MOAL waitqueue */
			wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_MEAS_REPORT,
					MNULL);
		}
		pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
		break;
	case EVENT_CHANNEL_SWITCH:
		pchan_info =
			(MrvlIEtypes_channel_band_t *)(pmadapter->event_body);
		channel = pchan_info->channel;
		PRINTM(MEVENT, "EVENT: CHANNEL_SWITCH new channel %d\n",
		       channel);
		pmpriv->uap_channel = channel;

		if ((pmpriv->adapter->state_rdh.stage != RDH_OFF &&
		     !pmpriv->intf_state_11h.is_11h_host)
#ifdef DFS_TESTING_SUPPORT
		    || pmpriv->adapter->dfs_test_params.
		    no_channel_change_on_radar ||
		    pmpriv->adapter->dfs_test_params.fixed_new_channel_on_radar
#endif
			) {
			/* Handle embedded DFS */
			if (pmpriv->adapter->state_rdh.stage ==
			    RDH_SET_CUSTOM_IE) {
				pmadapter->state_rdh.stage =
					RDH_RESTART_TRAFFIC;
				wlan_11h_radar_detected_handling(pmadapter,
								 pmpriv);
			}
		} else {
			/* Handle Host-based DFS and non-DFS(normal uap) case */
			pmpriv->intf_state_11h.tx_disabled = MFALSE;
			memset(pmadapter, event_buf, 0x00, MAX_EVENT_SIZE);
			/* Setup event buffer */
			pevent->bss_index = pmpriv->bss_index;
			pevent->event_id =
				MLAN_EVENT_ID_FW_CHAN_SWITCH_COMPLETE;
			pevent->event_len = sizeof(chan_band_info);
			pchan_band_info = (chan_band_info *) pevent->event_buf;
			/* Copy event data */
			memcpy(pmadapter, (t_u8 *)&pchan_band_info->bandcfg,
			       (t_u8 *)&pchan_info->bandcfg,
			       sizeof(pchan_info->bandcfg));
			pchan_band_info->channel = pchan_info->channel;
			pchan_band_info->is_11n_enabled =
				pmpriv->is_11n_enabled;
			wlan_recv_event(pmpriv,
					MLAN_EVENT_ID_FW_CHAN_SWITCH_COMPLETE,
					pevent);
			pevent->event_id = 0;
		}
		break;
#ifdef WIFI_DIRECT_SUPPORT
	case EVENT_REMAIN_ON_CHANNEL_EXPIRED:
		PRINTM(MEVENT, "EVENT: REMAIN_ON_CHANNEL_EXPIRED reason=%d\n",
		       *(t_u16 *)pmadapter->event_body);
		wlan_recv_event(pmpriv, MLAN_EVENT_ID_DRV_FLUSH_RX_WORK, MNULL);
		pevent->event_id = MLAN_EVENT_ID_FW_REMAIN_ON_CHAN_EXPIRED;
		break;
#endif
	case EVENT_MULTI_CHAN_INFO:
		PRINTM(MEVENT, "EVENT: MULTI_CHAN_INFO\n");
		wlan_handle_event_multi_chan_info(pmpriv, pmbuf);
		break;

	case EVENT_FW_DEBUG_INFO:
		memset(pmadapter, event_buf, 0x00, MAX_EVENT_SIZE);
		pevent->bss_index = pmpriv->bss_index;
		pevent->event_id = MLAN_EVENT_ID_FW_DEBUG_INFO;
		pevent->event_len = pmbuf->data_len - sizeof(eventcause);
		memcpy(pmadapter,
		       (t_u8 *)pevent->event_buf,
		       pmbuf->pbuf + pmbuf->data_offset + sizeof(eventcause),
		       pevent->event_len);
		PRINTM(MEVENT, "EVENT: FW Debug Info %s\n",
		       (t_u8 *)pevent->event_buf);
		wlan_recv_event(pmpriv, pevent->event_id, pevent);
		pevent->event_id = 0;	/* clear to avoid resending at end of fcn */
		break;
	case EVENT_TX_STATUS_REPORT:
		PRINTM(MINFO, "EVENT: TX_STATUS\n");
		pevent->event_id = MLAN_EVENT_ID_FW_TX_STATUS;
		break;
	case EVENT_BT_COEX_WLAN_PARA_CHANGE:
		PRINTM(MEVENT, "EVENT: BT coex wlan param update\n");
		wlan_bt_coex_wlan_param_update_event(pmpriv, pmbuf);
		break;
	case EVENT_EXCEED_MAX_P2P_CONN:
		event_excd_p2p =
			(event_exceed_max_p2p_conn *) (pmbuf->pbuf +
						       pmbuf->data_offset);
		PRINTM(MEVENT, "EVENT: EXCEED MAX P2P CONNECTION\n");
		PRINTM(MEVENT, "REQUEST P2P MAC: " MACSTR "\n",
		       MAC2STR(event_excd_p2p->peer_mac_addr));
		pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
		break;
	default:
		pevent->event_id = MLAN_EVENT_ID_DRV_PASSTHRU;
		break;
	}

	if (pevent->event_id) {
		pevent->bss_index = pmpriv->bss_index;
		pevent->event_len = pmbuf->data_len;
		memcpy(pmadapter, (t_u8 *)pevent->event_buf,
		       pmbuf->pbuf + pmbuf->data_offset, pevent->event_len);
		wlan_recv_event(pmpriv, pevent->event_id, pevent);
	}
done:
	if (event_buf)
		pcb->moal_mfree(pmadapter->pmoal_handle, event_buf);
	LEAVE();
	return ret;
}

/**
 *  @brief  This function issues commands to initialize firmware
 *
 *  @param priv         A pointer to mlan_private structure
 *  @param first_bss    flag for first BSS
 *
 *  @return   MLAN_STATUS_SUCCESS or MLAN_STATUS_PENDING or MLAN_STATUS_FAILURE
 */
mlan_status
wlan_ops_uap_init_cmd(IN t_void *priv, IN t_u8 first_bss)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	pmlan_private pmpriv = (pmlan_private)priv;
	t_u16 last_cmd = 0;

	ENTER();
	if (!pmpriv) {
		LEAVE();
		return MLAN_STATUS_FAILURE;
	}

	if (first_bss) {
		if (wlan_adapter_init_cmd(pmpriv->adapter) ==
		    MLAN_STATUS_FAILURE) {
			ret = MLAN_STATUS_FAILURE;
			goto done;
		}
	}

	ret = wlan_prepare_cmd(pmpriv, HOST_CMD_APCMD_SYS_CONFIGURE,
			       HostCmd_ACT_GEN_GET, 0, MNULL, MNULL);
	if (ret) {
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}
	last_cmd = HOST_CMD_APCMD_SYS_CONFIGURE;
    /** set last_init_cmd */
	if (last_cmd) {
		pmpriv->adapter->last_init_cmd = last_cmd;
		ret = MLAN_STATUS_PENDING;
	}
done:
	LEAVE();
	return ret;
}
