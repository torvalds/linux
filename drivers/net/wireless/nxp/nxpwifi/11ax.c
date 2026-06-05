// SPDX-License-Identifier: GPL-2.0-only
/* nxpwifi: 802.11ax (HE) support
 * Copyright (C) 2011-2024 NXP
 */

#include "cfg.h"
#include "fw.h"
#include "main.h"
#include "11ax.h"

void nxpwifi_update_11ax_cap(struct nxpwifi_adapter *adapter,
			     struct hw_spec_extension *hw_he_cap)
{
	struct nxpwifi_private *priv;
	struct nxpwifi_ie_types_he_cap *he_cap = NULL;
	struct nxpwifi_ie_types_he_cap *user_he_cap = NULL;
	u8 header_len = sizeof(struct nxpwifi_ie_types_header);
	u16 data_len = le16_to_cpu(hw_he_cap->header.len);
	bool he_cap_2g = false;
	int i;

	if ((data_len + header_len) > sizeof(adapter->hw_he_cap)) {
		nxpwifi_dbg(adapter, ERROR,
			    "hw_he_cap too big, len=%d\n",
			    data_len);
		return;
	}

	he_cap = (struct nxpwifi_ie_types_he_cap *)hw_he_cap;

	if (he_cap->he_phy_cap[0] &
	    (AX_2G_40MHZ_SUPPORT | AX_2G_20MHZ_SUPPORT)) {
		adapter->hw_2g_he_cap_len = data_len + header_len;
		memcpy(adapter->hw_2g_he_cap, (u8 *)hw_he_cap,
		       adapter->hw_2g_he_cap_len);
		adapter->fw_bands |= BAND_GAX;
		he_cap_2g = true;
		nxpwifi_dbg_dump(adapter, CMD_D, "2.4G HE capability element ",
				 adapter->hw_2g_he_cap,
				 adapter->hw_2g_he_cap_len);
	} else {
		adapter->hw_he_cap_len = data_len + header_len;
		memcpy(adapter->hw_he_cap, (u8 *)hw_he_cap,
		       adapter->hw_he_cap_len);
		adapter->fw_bands |= BAND_AAX;
		nxpwifi_dbg_dump(adapter, CMD_D, "5G HE capability element ",
				 adapter->hw_he_cap,
				 adapter->hw_he_cap_len);
	}

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];

		if (he_cap_2g) {
			priv->user_2g_he_cap_len = adapter->hw_2g_he_cap_len;
			memcpy(priv->user_2g_he_cap, adapter->hw_2g_he_cap,
			       sizeof(adapter->hw_2g_he_cap));
			user_he_cap = (struct nxpwifi_ie_types_he_cap *)
				priv->user_2g_he_cap;
		} else {
			priv->user_he_cap_len = adapter->hw_he_cap_len;
			memcpy(priv->user_he_cap, adapter->hw_he_cap,
			       sizeof(adapter->hw_he_cap));
			user_he_cap = (struct nxpwifi_ie_types_he_cap *)
				priv->user_he_cap;
		}

		if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA)
			user_he_cap->he_mac_cap[0] &=
				~HE_MAC_CAP_TWT_RESP_SUPPORT;
		else
			user_he_cap->he_mac_cap[0] &=
				~HE_MAC_CAP_TWT_REQ_SUPPORT;
	}

	adapter->is_hw_11ax_capable = true;
}

bool nxpwifi_11ax_bandconfig_allowed(struct nxpwifi_private *priv,
				     struct nxpwifi_bssdescriptor *bss_desc)
{
	u16 bss_band = bss_desc->bss_band;

	if (bss_desc->disable_11n)
		return false;

	if (bss_band & BAND_G)
		return (priv->config_bands & BAND_GAX);
	else if (bss_band & BAND_A)
		return (priv->config_bands & BAND_AAX);

	return false;
}

int nxpwifi_fill_he_cap_tlv(struct nxpwifi_private *priv,
			    struct nxpwifi_ie_types_he_cap *he_cap,
			    u16 bands)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ie_types_he_cap *hw_he_cap = NULL;
	u16 rx_nss, tx_nss;
	u8 nss;
	u16 cfg_value;
	u16 hw_value;
	int ret_len;

	if (bands & BAND_A) {
		memcpy(he_cap, priv->user_he_cap, priv->user_he_cap_len);
		hw_he_cap = (struct nxpwifi_ie_types_he_cap *)adapter->hw_he_cap;
		ret_len = priv->user_he_cap_len;
	} else {
		memcpy(he_cap, priv->user_2g_he_cap, priv->user_2g_he_cap_len);
		hw_he_cap = (struct nxpwifi_ie_types_he_cap *)adapter->hw_2g_he_cap;
		ret_len = priv->user_2g_he_cap_len;
	}

	if (bands & BAND_A) {
		rx_nss = GET_RXMCSSUPP(adapter->user_htstream >> 8);
		tx_nss = GET_TXMCSSUPP(adapter->user_htstream >> 8) & 0x0f;
	} else {
		rx_nss = GET_RXMCSSUPP(adapter->user_htstream);
		tx_nss = GET_TXMCSSUPP(adapter->user_htstream) & 0x0f;
	}

	for (nss = 1; nss <= 8; nss++) {
		cfg_value = nxpwifi_get_he_nss_mcs(he_cap->rx_mcs_80, nss);
		hw_value = nxpwifi_get_he_nss_mcs(hw_he_cap->rx_mcs_80, nss);
		if (rx_nss != 0 && nss > rx_nss)
			cfg_value = NO_NSS_SUPPORT;
		if (hw_value == NO_NSS_SUPPORT || cfg_value == NO_NSS_SUPPORT)
			nxpwifi_set_he_nss_mcs(&he_cap->rx_mcs_80, nss,
					       NO_NSS_SUPPORT);
		else
			nxpwifi_set_he_nss_mcs(&he_cap->rx_mcs_80, nss,
					       min(cfg_value, hw_value));
	}

	for (nss = 1; nss <= 8; nss++) {
		cfg_value = nxpwifi_get_he_nss_mcs(he_cap->tx_mcs_80, nss);
		hw_value = nxpwifi_get_he_nss_mcs(hw_he_cap->tx_mcs_80, nss);
		if (tx_nss != 0 && nss > tx_nss)
			cfg_value = NO_NSS_SUPPORT;
		if (hw_value == NO_NSS_SUPPORT || cfg_value == NO_NSS_SUPPORT)
			nxpwifi_set_he_nss_mcs(&he_cap->tx_mcs_80, nss,
					       NO_NSS_SUPPORT);
		else
			nxpwifi_set_he_nss_mcs(&he_cap->tx_mcs_80, nss,
					       min(cfg_value, hw_value));
	}

	return ret_len;
}

int nxpwifi_cmd_append_11ax_tlv(struct nxpwifi_private *priv,
				struct nxpwifi_bssdescriptor *bss_desc,
				u8 **buffer)
{
	struct nxpwifi_ie_types_he_cap *he_cap = NULL;
	int ret_len;

	if (!bss_desc->bcn_he_cap)
		return -EOPNOTSUPP;

	he_cap = (struct nxpwifi_ie_types_he_cap *)*buffer;
	ret_len = nxpwifi_fill_he_cap_tlv(priv, he_cap, bss_desc->bss_band);
	*buffer += ret_len;

	return ret_len;
}

int nxpwifi_cmd_11ax_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct nxpwifi_11ax_he_cfg *ax_cfg)
{
	struct host_cmd_11ax_cfg *he_cfg = &cmd->params.ax_cfg;
	u16 cmd_size;
	struct nxpwifi_ie_types_header *header;

	cmd->command = cpu_to_le16(HOST_CMD_11AX_CFG);
	cmd_size = sizeof(struct host_cmd_11ax_cfg) + S_DS_GEN;

	he_cfg->action = cpu_to_le16(cmd_action);
	he_cfg->band_config = ax_cfg->band;

	if (ax_cfg->he_cap_cfg.len &&
	    ax_cfg->he_cap_cfg.ext_id == WLAN_EID_EXT_HE_CAPABILITY) {
		header = (struct nxpwifi_ie_types_header *)he_cfg->tlv;
		header->type = cpu_to_le16(ax_cfg->he_cap_cfg.id);
		header->len = cpu_to_le16(ax_cfg->he_cap_cfg.len);
		memcpy(he_cfg->tlv + sizeof(*header),
		       &ax_cfg->he_cap_cfg.ext_id,
		       ax_cfg->he_cap_cfg.len);
		cmd_size += (sizeof(*header) + ax_cfg->he_cap_cfg.len);
	}

	cmd->size = cpu_to_le16(cmd_size);

	return 0;
}

int nxpwifi_ret_11ax_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 struct nxpwifi_11ax_he_cfg *ax_cfg)
{
	struct host_cmd_11ax_cfg *he_cfg = &resp->params.ax_cfg;
	struct nxpwifi_ie_types_header *header;
	u16 left_len, tlv_type, tlv_len;
	u8 ext_id;
	struct nxpwifi_11ax_he_cap_cfg *he_cap = &ax_cfg->he_cap_cfg;

	left_len = le16_to_cpu(resp->size) - sizeof(*he_cfg) - S_DS_GEN;
	header = (struct nxpwifi_ie_types_header *)he_cfg->tlv;

	while (left_len > sizeof(*header)) {
		tlv_type = le16_to_cpu(header->type);
		tlv_len = le16_to_cpu(header->len);

		if (tlv_type == TLV_TYPE_EXTENSION_ID) {
			ext_id = *((u8 *)header + sizeof(*header) + 1);
			if (ext_id == WLAN_EID_EXT_HE_CAPABILITY) {
				he_cap->id = tlv_type;
				he_cap->len = tlv_len;
				memcpy((u8 *)&he_cap->ext_id,
				       (u8 *)header + sizeof(*header) + 1,
				       tlv_len);
				if (he_cfg->band_config & BIT(1)) {
					memcpy(priv->user_he_cap,
					       (u8 *)header,
					       sizeof(*header) + tlv_len);
					priv->user_he_cap_len =
						sizeof(*header) + tlv_len;
				} else {
					memcpy(priv->user_2g_he_cap,
					       (u8 *)header,
					       sizeof(*header) + tlv_len);
					priv->user_2g_he_cap_len =
						sizeof(*header) + tlv_len;
				}
			}
		}

		left_len -= (sizeof(*header) + tlv_len);
		header = (struct nxpwifi_ie_types_header *)((u8 *)header +
							    sizeof(*header) +
							    tlv_len);
	}

	return 0;
}

int nxpwifi_cmd_11ax_cmd(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd, u16 cmd_action,
			 struct nxpwifi_11ax_cmd_cfg *ax_cmd)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_11ax_cmd *he_cmd = &cmd->params.ax_cmd;
	u16 cmd_size;
	struct nxpwifi_11ax_sr_cmd *sr_cmd;
	struct nxpwifi_ie_types_data *tlv;
	struct nxpwifi_11ax_beam_cmd *beam_cmd;
	struct nxpwifi_11ax_htc_cmd *htc_cmd;
	struct nxpwifi_11ax_txomi_cmd *txmoi_cmd;
	struct nxpwifi_11ax_toltime_cmd *toltime_cmd;
	struct nxpwifi_11ax_txop_cmd *txop_cmd;
	struct nxpwifi_11ax_set_bsrp_cmd *set_bsrp_cmd;
	struct nxpwifi_11ax_llde_cmd *llde_cmd;

	cmd->command = cpu_to_le16(HOST_CMD_11AX_CMD);
	cmd_size = sizeof(struct host_cmd_11ax_cmd) + S_DS_GEN;

	he_cmd->action = cpu_to_le16(cmd_action);
	he_cmd->sub_id = cpu_to_le16(ax_cmd->sub_id);

	switch (ax_cmd->sub_command) {
	case NXPWIFI_11AXCMD_SR_SUBID:
		sr_cmd = (struct nxpwifi_11ax_sr_cmd *)&ax_cmd->param;

		tlv = (struct nxpwifi_ie_types_data *)he_cmd->val;
		tlv->header.type = cpu_to_le16(sr_cmd->type);
		tlv->header.len = cpu_to_le16(sr_cmd->len);
		memcpy(tlv->data, sr_cmd->param.obss_pd_offset.offset,
		       sr_cmd->len);
		cmd_size += (sizeof(tlv->header) + sr_cmd->len);
		break;
	case NXPWIFI_11AXCMD_BEAM_SUBID:
		beam_cmd = (struct nxpwifi_11ax_beam_cmd *)&ax_cmd->param;

		he_cmd->val[0] = beam_cmd->value;
		cmd_size += sizeof(*beam_cmd);
		break;
	case NXPWIFI_11AXCMD_HTC_SUBID:
		htc_cmd = (struct nxpwifi_11ax_htc_cmd *)&ax_cmd->param;

		he_cmd->val[0] = htc_cmd->value;
		cmd_size += sizeof(*htc_cmd);
		break;
	case NXPWIFI_11AXCMD_TXOMI_SUBID:
		txmoi_cmd =	(struct nxpwifi_11ax_txomi_cmd *)&ax_cmd->param;

		memcpy((void *)he_cmd->val, txmoi_cmd, sizeof(*txmoi_cmd));
		cmd_size += sizeof(*txmoi_cmd);
		break;
	case NXPWIFI_11AXCMD_OBSS_TOLTIME_SUBID:
		toltime_cmd = (struct nxpwifi_11ax_toltime_cmd *)&ax_cmd->param;

		memcpy(he_cmd->val, &toltime_cmd->tol_time,
		       sizeof(toltime_cmd->tol_time));
		cmd_size += sizeof(*toltime_cmd);
		break;
	case NXPWIFI_11AXCMD_TXOPRTS_SUBID:
		txop_cmd = (struct nxpwifi_11ax_txop_cmd *)&ax_cmd->param;

		memcpy(he_cmd->val, &txop_cmd->rts_thres,
		       sizeof(txop_cmd->rts_thres));
		cmd_size += sizeof(*txop_cmd);
		break;
	case NXPWIFI_11AXCMD_SET_BSRP_SUBID:
		set_bsrp_cmd = (struct nxpwifi_11ax_set_bsrp_cmd *)&ax_cmd->param;

		he_cmd->val[0] = set_bsrp_cmd->value;
		cmd_size += sizeof(*set_bsrp_cmd);
		break;
	case NXPWIFI_11AXCMD_LLDE_SUBID:
		llde_cmd = (struct nxpwifi_11ax_llde_cmd *)&ax_cmd->param;

		memcpy((void *)he_cmd->val, llde_cmd, sizeof(*llde_cmd));
		cmd_size += sizeof(*llde_cmd);
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "%s: Unknown sub command: %d\n",
			    __func__, ax_cmd->sub_command);
		return -EINVAL;
	}

	cmd->size = cpu_to_le16(cmd_size);

	return 0;
}

int nxpwifi_ret_11ax_cmd(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 struct nxpwifi_11ax_cmd_cfg *ax_cmd)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_11ax_cmd *he_cmd = &resp->params.ax_cmd;
	struct nxpwifi_ie_types_data *tlv;

	ax_cmd->sub_id = le16_to_cpu(he_cmd->sub_id);

	switch (ax_cmd->sub_command) {
	case NXPWIFI_11AXCMD_SR_SUBID:
		tlv = (struct nxpwifi_ie_types_data *)he_cmd->val;
		memcpy(ax_cmd->param.sr_cfg.param.obss_pd_offset.offset,
		       tlv->data,
		       ax_cmd->param.sr_cfg.len);
		break;
	case NXPWIFI_11AXCMD_BEAM_SUBID:
		ax_cmd->param.beam_cfg.value = *he_cmd->val;
		break;
	case NXPWIFI_11AXCMD_HTC_SUBID:
		ax_cmd->param.htc_cfg.value = *he_cmd->val;
		break;
	case NXPWIFI_11AXCMD_TXOMI_SUBID:
		memcpy(&ax_cmd->param.txomi_cfg,
		       he_cmd->val, sizeof(ax_cmd->param.txomi_cfg));
		break;
	case NXPWIFI_11AXCMD_OBSS_TOLTIME_SUBID:
		memcpy(&ax_cmd->param.toltime_cfg.tol_time,
		       he_cmd->val, sizeof(ax_cmd->param.toltime_cfg));
		break;
	case NXPWIFI_11AXCMD_TXOPRTS_SUBID:
		memcpy(&ax_cmd->param.txop_cfg.rts_thres,
		       he_cmd->val, sizeof(ax_cmd->param.txop_cfg));
		break;
	case NXPWIFI_11AXCMD_SET_BSRP_SUBID:
		ax_cmd->param.setbsrp_cfg.value = *he_cmd->val;
		break;
	case NXPWIFI_11AXCMD_LLDE_SUBID:
		memcpy(&ax_cmd->param.llde_cfg,
		       he_cmd->val, sizeof(ax_cmd->param.llde_cfg));
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "%s: Unknown sub command: %d\n",
			    __func__, ax_cmd->sub_command);
		return -EINVAL;
	}

	return 0;
}

static u8 nxpwifi_is_ap_11ax_twt_supported(struct nxpwifi_bssdescriptor *bss_desc)
{
	struct element *ext_cap;

	if (!bss_desc->bcn_he_cap)
		return false;
	if (!(bss_desc->bcn_he_cap->mac_cap_info[0] & HE_MAC_CAP_TWT_RESP_SUPPORT))
		return false;
	if (!bss_desc->bcn_ext_cap)
		return false;
	ext_cap = (struct element *)bss_desc->bcn_ext_cap;

	if (!(ext_cap->data[9] & WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT))
		return false;
	return true;
}

bool nxpwifi_is_11ax_twt_supported(struct nxpwifi_private *priv,
				   struct nxpwifi_bssdescriptor *bss_desc)
{
	struct nxpwifi_ie_types_he_cap *user_he_cap;
	struct nxpwifi_ie_types_he_cap *hw_he_cap;

	if (bss_desc && (!nxpwifi_is_ap_11ax_twt_supported(bss_desc))) {
		nxpwifi_dbg(priv->adapter, MSG,
			    "AP don't support twt feature\n");
		return false;
	}

	if (bss_desc->bss_band & BAND_A) {
		hw_he_cap = (struct nxpwifi_ie_types_he_cap *)
			priv->adapter->hw_he_cap;
		user_he_cap = (struct nxpwifi_ie_types_he_cap *)
			priv->user_he_cap;
	} else {
		hw_he_cap = (struct nxpwifi_ie_types_he_cap *)
			priv->adapter->hw_2g_he_cap;
		user_he_cap = (struct nxpwifi_ie_types_he_cap *)
			priv->user_2g_he_cap;
	}

	if (!(hw_he_cap->he_mac_cap[0] & HE_MAC_CAP_TWT_REQ_SUPPORT)) {
		nxpwifi_dbg(priv->adapter, MSG,
			    "FW don't support TWT\n");
		return false;
	}

	if (!(user_he_cap->he_mac_cap[0] & HE_MAC_CAP_TWT_REQ_SUPPORT)) {
		nxpwifi_dbg(priv->adapter, MSG,
			    "USER HE_MAC_CAP don't support TWT\n");
		return false;
	}

	return true;
}

u8 nxpwifi_is_sta_11ax_twt_req_supported(struct nxpwifi_private *priv)
{
	struct nxpwifi_ie_types_he_cap *user_he_cap;
	u8 ret = 0;

	if (ISSUPP_11AXENABLED(priv->adapter->fw_cap_ext) &&
	    (priv->config_bands & BAND_GAX || priv->config_bands & BAND_AAX)) {
		if (priv->config_bands & BAND_AAX)
			user_he_cap = (struct nxpwifi_ie_types_he_cap *)priv->user_he_cap;
		else
			user_he_cap = (struct nxpwifi_ie_types_he_cap *)priv->user_2g_he_cap;
		ret = user_he_cap->he_mac_cap[0] & HE_MAC_CAP_TWT_REQ_SUPPORT;
	}

	return ret;
}

int nxpwifi_cmd_twt_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd, u16 cmd_action,
			struct nxpwifi_twt_cfg *twt_cfg)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_twt_cfg *twt_cfg_cmd = &cmd->params.twt_cfg;
	struct nxpwifi_twt_setup *twt_setup;
	struct nxpwifi_twt_teardown *twt_teardown;
	struct nxpwifi_twt_report *twt_report;
	struct nxpwifi_twt_information *twt_information;
	struct nxpwifi_btwt_ap_config *btwt_ap_config;
	u8 i;
	u16 cmd_size;

	cmd->command = cpu_to_le16(HOST_CMD_TWT_CFG);
	cmd_size = sizeof(struct host_cmd_twt_cfg) + S_DS_GEN;

	twt_cfg_cmd->action = cpu_to_le16(cmd_action);
	twt_cfg_cmd->sub_id = cpu_to_le16(twt_cfg->sub_id);

	switch (twt_cfg->sub_id) {
	case NXPWIFI_11AX_TWT_SETUP_SUBID:
		twt_setup = (struct nxpwifi_twt_setup *)
			twt_cfg_cmd->val;

		memset(twt_setup, 0x00, sizeof(struct nxpwifi_twt_setup));
		twt_setup->implicit = twt_cfg->param.twt_setup.implicit;
		twt_setup->announced = twt_cfg->param.twt_setup.announced;
		twt_setup->trigger_enabled = twt_cfg->param.twt_setup.trigger_enabled;
		twt_setup->twt_info_disabled = twt_cfg->param.twt_setup.twt_info_disabled;
		twt_setup->negotiation_type = twt_cfg->param.twt_setup.negotiation_type;
		twt_setup->twt_wakeup_duration =
			twt_cfg->param.twt_setup.twt_wakeup_duration;
		twt_setup->flow_identifier = twt_cfg->param.twt_setup.flow_identifier;
		twt_setup->hard_constraint = twt_cfg->param.twt_setup.hard_constraint;
		twt_setup->twt_exponent = twt_cfg->param.twt_setup.twt_exponent;
		twt_setup->twt_mantissa = twt_cfg->param.twt_setup.twt_mantissa;
		twt_setup->twt_request = twt_cfg->param.twt_setup.twt_request;
		twt_setup->bcn_miss_threshold = twt_cfg->param.twt_setup.bcn_miss_threshold;
		cmd_size += sizeof(struct nxpwifi_twt_setup);
		break;
	case NXPWIFI_11AX_TWT_TEARDOWN_SUBID:
		twt_teardown = (struct nxpwifi_twt_teardown *)
			twt_cfg_cmd->val;
		memset(twt_teardown, 0x00,
		       sizeof(struct nxpwifi_twt_teardown));
		twt_teardown->flow_identifier =
			twt_cfg->param.twt_teardown.flow_identifier;
		twt_teardown->negotiation_type =
			twt_cfg->param.twt_teardown.negotiation_type;
		twt_teardown->teardown_all_twt =
			twt_cfg->param.twt_teardown.teardown_all_twt;
		cmd_size += sizeof(struct nxpwifi_twt_teardown);
		break;
	case NXPWIFI_11AX_TWT_REPORT_SUBID:
		twt_report = (struct nxpwifi_twt_report *)
			twt_cfg_cmd->val;
		memset(twt_report, 0x00, sizeof(struct nxpwifi_twt_report));
		twt_report->type = twt_cfg->param.twt_report.type;
		cmd_size += sizeof(struct nxpwifi_twt_report);
		break;
	case NXPWIFI_11AX_TWT_INFORMATION_SUBID:
		twt_information = (struct nxpwifi_twt_information *)
			twt_cfg_cmd->val;
		memset(twt_information, 0x00,
		       sizeof(struct nxpwifi_twt_information));
		twt_information->flow_identifier =
			twt_cfg->param.twt_information.flow_identifier;
		twt_information->suspend_duration =
			twt_cfg->param.twt_information.suspend_duration;
		cmd_size += sizeof(struct nxpwifi_twt_information);
		break;
	case NXPWIFI_11AX_BTWT_AP_CONFIG_SUBID:
		btwt_ap_config = (struct nxpwifi_btwt_ap_config *)
				 twt_cfg_cmd->val;
		memset(btwt_ap_config, 0x00,
		       sizeof(struct nxpwifi_btwt_ap_config));
		btwt_ap_config->ap_bcast_bet_sta_wait =
			twt_cfg->param.btwt_ap_config.ap_bcast_bet_sta_wait;
		btwt_ap_config->ap_bcast_offset =
			twt_cfg->param.btwt_ap_config.ap_bcast_offset;
		btwt_ap_config->bcast_twtli =
			twt_cfg->param.btwt_ap_config.bcast_twtli;
		btwt_ap_config->count =
			twt_cfg->param.btwt_ap_config.count;
		for (i = 0; i < BTWT_AGREEMENT_MAX; i++) {
			btwt_ap_config->btwt_sets[i].btwt_id =
				twt_cfg->param.btwt_ap_config.btwt_sets[i].btwt_id;
			btwt_ap_config->btwt_sets[i].ap_bcast_mantissa =
				twt_cfg->param.btwt_ap_config.btwt_sets[i].ap_bcast_mantissa;
			btwt_ap_config->btwt_sets[i].ap_bcast_exponent =
				twt_cfg->param.btwt_ap_config.btwt_sets[i].ap_bcast_exponent;
			btwt_ap_config->btwt_sets[i].nominalwake =
				twt_cfg->param.btwt_ap_config.btwt_sets[i].nominalwake;
		}

		cmd_size += sizeof(struct nxpwifi_btwt_ap_config);
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "Unknown sub id: %d\n", twt_cfg->sub_id);
		return -EINVAL;
	}

	cmd->size = cpu_to_le16(cmd_size);

	return 0;
}

int nxpwifi_ret_twt_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *resp,
			struct nxpwifi_twt_cfg *twt_cfg)
{
	struct host_cmd_twt_cfg *twt_cfg_cmd = &resp->params.twt_cfg;
	u16 action;

	action = le16_to_cpu(twt_cfg_cmd->action);
	twt_cfg->sub_id = le16_to_cpu(twt_cfg_cmd->sub_id);

	if (action == HOST_ACT_GEN_GET &&
	    twt_cfg->sub_id == NXPWIFI_11AX_TWT_REPORT_SUBID) {
		struct nxpwifi_twt_report *twt_report =
			(struct nxpwifi_twt_report *)twt_cfg_cmd->val;

		memcpy(&twt_cfg->param.twt_report, twt_report, sizeof(struct nxpwifi_twt_report));
	}

	return 0;
}
