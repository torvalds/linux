// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: station command handling
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

static bool disable_auto_ds;

static int
nxpwifi_cmd_sta_get_hw_spec(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *cmd,
			    u16 cmd_no, void *data_buf,
			    u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_get_hw_spec *hw_spec = &cmd->params.hw_spec;

	cmd->command = cpu_to_le16(HOST_CMD_GET_HW_SPEC);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_get_hw_spec) +
				S_DS_GEN);
	memcpy(hw_spec->permanent_addr, priv->curr_addr, ETH_ALEN);

	return 0;
}

static int
nxpwifi_ret_sta_get_hw_spec(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *resp,
			    u16 cmdresp_no,
			    void *data_buf)
{
	struct host_cmd_ds_get_hw_spec *hw_spec = &resp->params.hw_spec;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ie_types_header *tlv;
	struct hw_spec_api_rev *api_rev;
	struct hw_spec_max_conn *max_conn;
	struct hw_spec_extension *hw_he_cap;
	struct hw_spec_fw_cap_info *fw_cap;
	struct hw_spec_secure_boot_uuid *sb_uuid;
	u16 resp_size, api_id;
	int i, left_len, parsed_len = 0;

	adapter->fw_cap_info = le32_to_cpu(hw_spec->fw_cap_info);

	if (IS_SUPPORT_MULTI_BANDS(adapter))
		adapter->fw_bands = GET_FW_DEFAULT_BANDS(adapter);
	else
		adapter->fw_bands = BAND_B;

	if ((adapter->fw_bands & BAND_A) && (adapter->fw_bands & BAND_GN))
		adapter->fw_bands |= BAND_AN;
	if (!(adapter->fw_bands & BAND_G) && (adapter->fw_bands & BAND_GN))
		adapter->fw_bands &= ~BAND_GN;

	adapter->fw_release_number = le32_to_cpu(hw_spec->fw_release_number);
	adapter->fw_api_ver = (adapter->fw_release_number >> 16) & 0xff;
	adapter->number_of_antenna =
		le16_to_cpu(hw_spec->number_of_antenna) & 0xf;

	if (le32_to_cpu(hw_spec->dot_11ac_dev_cap)) {
		adapter->is_hw_11ac_capable = true;

		/* Copy 11AC cap */
		adapter->hw_dot_11ac_dev_cap =
			le32_to_cpu(hw_spec->dot_11ac_dev_cap);
		adapter->usr_dot_11ac_dev_cap_bg = adapter->hw_dot_11ac_dev_cap
			& ~NXPWIFI_DEF_11AC_CAP_BF_RESET_MASK;
		adapter->usr_dot_11ac_dev_cap_a = adapter->hw_dot_11ac_dev_cap
			& ~NXPWIFI_DEF_11AC_CAP_BF_RESET_MASK;

		/* Copy 11AC mcs */
		adapter->hw_dot_11ac_mcs_support =
			le32_to_cpu(hw_spec->dot_11ac_mcs_support);
		adapter->usr_dot_11ac_mcs_support =
			adapter->hw_dot_11ac_mcs_support;
	} else {
		adapter->is_hw_11ac_capable = false;
	}

	resp_size = le16_to_cpu(resp->size) - S_DS_GEN;
	if (resp_size > sizeof(struct host_cmd_ds_get_hw_spec)) {
		/* we have variable HW SPEC information */
		left_len = resp_size - sizeof(struct host_cmd_ds_get_hw_spec);
		while (left_len > sizeof(struct nxpwifi_ie_types_header)) {
			tlv = (void *)&hw_spec->tlv + parsed_len;
			switch (le16_to_cpu(tlv->type)) {
			case TLV_TYPE_API_REV:
				api_rev = (struct hw_spec_api_rev *)tlv;
				api_id = le16_to_cpu(api_rev->api_id);
				switch (api_id) {
				case KEY_API_VER_ID:
					adapter->key_api_major_ver =
						api_rev->major_ver;
					adapter->key_api_minor_ver =
						api_rev->minor_ver;
					nxpwifi_dbg(adapter, INFO,
						    "key_api v%d.%d\n",
						    adapter->key_api_major_ver,
						    adapter->key_api_minor_ver);
					break;
				case FW_API_VER_ID:
					adapter->fw_api_ver =
						api_rev->major_ver;
					nxpwifi_dbg(adapter, MSG,
						    "Firmware api version %d.%d\n",
						    adapter->fw_api_ver,
						    api_rev->minor_ver);
					break;
				case UAP_FW_API_VER_ID:
					nxpwifi_dbg(adapter, INFO,
						    "uAP api version %d.%d\n",
						    api_rev->major_ver,
						    api_rev->minor_ver);
					break;
				case CHANRPT_API_VER_ID:
					nxpwifi_dbg(adapter, INFO,
						    "channel report api version %d.%d\n",
						    api_rev->major_ver,
						    api_rev->minor_ver);
					break;
				case FW_HOTFIX_VER_ID:
					adapter->fw_hotfix_ver =
						api_rev->major_ver;
					nxpwifi_dbg(adapter, INFO,
						    "Firmware hotfix version %d\n",
						    api_rev->major_ver);
					break;
				default:
					nxpwifi_dbg(adapter, FATAL,
						    "Unknown api_id: %d\n",
						    api_id);
					break;
				}
				break;
			case TLV_TYPE_MAX_CONN:
				max_conn = (struct hw_spec_max_conn *)tlv;
				adapter->max_sta_conn = max_conn->max_sta_conn;
				nxpwifi_dbg(adapter, INFO,
					    "max sta connections: %u\n",
					    adapter->max_sta_conn);
				break;
			case TLV_TYPE_EXTENSION_ID:
				hw_he_cap = (struct hw_spec_extension *)tlv;
				if (hw_he_cap->ext_id ==
				    WLAN_EID_EXT_HE_CAPABILITY)
					nxpwifi_update_11ax_cap(adapter, hw_he_cap);
				break;
			case TLV_TYPE_FW_CAP_INFO:
				fw_cap = (struct hw_spec_fw_cap_info *)tlv;
				adapter->fw_cap_info =
					le32_to_cpu(fw_cap->fw_cap_info);
				adapter->fw_cap_ext =
					le32_to_cpu(fw_cap->fw_cap_ext);
				nxpwifi_dbg(adapter, INFO,
					    "fw_cap_info:%#x fw_cap_ext:%#x\n",
					    adapter->fw_cap_info,
					    adapter->fw_cap_ext);
				break;
			case TLV_TYPE_SECURE_BOOT_UUID:
				sb_uuid = (struct hw_spec_secure_boot_uuid *)tlv;
				adapter->uuid_lo =
					le64_to_cpu(sb_uuid->uuid_lo);
				adapter->uuid_hi =
					le64_to_cpu(sb_uuid->uuid_hi);
				nxpwifi_dbg(adapter, INFO,
					    "uuid: %#llx%#llx\n",
					    adapter->uuid_lo, adapter->uuid_hi);
				break;
			default:
				nxpwifi_dbg(adapter, FATAL,
					    "Unknown GET_HW_SPEC TLV type: %#x\n",
					    le16_to_cpu(tlv->type));
				break;
			}
			parsed_len += le16_to_cpu(tlv->len) +
				      sizeof(struct nxpwifi_ie_types_header);
			left_len -= le16_to_cpu(tlv->len) +
				      sizeof(struct nxpwifi_ie_types_header);
		}
	}

	if (adapter->key_api_major_ver < KEY_API_VER_MAJOR_V2)
		return -EOPNOTSUPP;

	nxpwifi_dbg(adapter, INFO,
		    "info: GET_HW_SPEC: fw_release_number- %#x\n",
		    adapter->fw_release_number);
	nxpwifi_dbg(adapter, INFO,
		    "info: GET_HW_SPEC: permanent addr: %pM\n",
		    hw_spec->permanent_addr);
	nxpwifi_dbg(adapter, INFO,
		    "info: GET_HW_SPEC: hw_if_version=%#x version=%#x\n",
		    le16_to_cpu(hw_spec->hw_if_version),
		    le16_to_cpu(hw_spec->version));

	ether_addr_copy(priv->adapter->perm_addr, hw_spec->permanent_addr);
	adapter->region_code = le16_to_cpu(hw_spec->region_code);

	/* If it's unidentified region code, use the default (USA/FCC) */
	if (!nxpwifi_is_valid_region_code(adapter->region_code)) {
		nxpwifi_dbg(adapter, WARN,
				    "cmd: unknown region code %#x, use default (USA/%#x)\n",
				    adapter->region_code, NXPWIFI_DEFAULT_REGION_CODE);
		adapter->region_code = NXPWIFI_DEFAULT_REGION_CODE;
	}

	adapter->hw_dot_11n_dev_cap = le32_to_cpu(hw_spec->dot_11n_dev_cap);
	adapter->hw_dev_mcs_support = hw_spec->dev_mcs_support;
	adapter->hw_mpdu_density = GET_MPDU_DENSITY(le32_to_cpu(hw_spec->hw_dev_cap));
	adapter->user_dev_mcs_support = adapter->hw_dev_mcs_support;
	adapter->user_htstream = adapter->hw_dev_mcs_support;
	if (adapter->fw_bands & BAND_A)
		adapter->user_htstream |= (adapter->user_htstream << 8);

	if (adapter->if_ops.update_mp_end_port) {
		u16 mp_end_port;

		mp_end_port = le16_to_cpu(hw_spec->mp_end_port);
		adapter->if_ops.update_mp_end_port(adapter, mp_end_port);
	}

	if (adapter->fw_api_ver == NXPWIFI_FW_V15)
		adapter->scan_chan_gap_enabled = true;

	for (i = 0; i < adapter->priv_num; i++)
		adapter->priv[i]->config_bands = adapter->fw_bands;

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_scan(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *cmd,
			    u16 cmd_no, void *data_buf,
			    u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_802_11_scan(cmd, data_buf);
}

static int
nxpwifi_ret_sta_802_11_scan(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *resp,
			    u16 cmdresp_no,
			    void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;

	ret = nxpwifi_ret_802_11_scan(priv, resp);
	adapter->curr_cmd->wait_q_enabled = false;

	return ret;
}

static int
nxpwifi_cmd_sta_802_11_get_log(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *cmd,
			       u16 cmd_no, void *data_buf,
			       u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(HOST_CMD_802_11_GET_LOG);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_get_log) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_ret_sta_802_11_get_log(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *resp,
			       u16 cmdresp_no,
			       void *data_buf)
{
	struct host_cmd_ds_802_11_get_log *get_log =
		&resp->params.get_log;
	struct nxpwifi_ds_get_stats *stats =
		(struct nxpwifi_ds_get_stats *)data_buf;

	if (stats) {
		stats->mcast_tx_frame = le32_to_cpu(get_log->mcast_tx_frame);
		stats->failed = le32_to_cpu(get_log->failed);
		stats->retry = le32_to_cpu(get_log->retry);
		stats->multi_retry = le32_to_cpu(get_log->multi_retry);
		stats->frame_dup = le32_to_cpu(get_log->frame_dup);
		stats->rts_success = le32_to_cpu(get_log->rts_success);
		stats->rts_failure = le32_to_cpu(get_log->rts_failure);
		stats->ack_failure = le32_to_cpu(get_log->ack_failure);
		stats->rx_frag = le32_to_cpu(get_log->rx_frag);
		stats->mcast_rx_frame = le32_to_cpu(get_log->mcast_rx_frame);
		stats->fcs_error = le32_to_cpu(get_log->fcs_error);
		stats->tx_frame = le32_to_cpu(get_log->tx_frame);
		stats->wep_icv_error[0] =
			le32_to_cpu(get_log->wep_icv_err_cnt[0]);
		stats->wep_icv_error[1] =
			le32_to_cpu(get_log->wep_icv_err_cnt[1]);
		stats->wep_icv_error[2] =
			le32_to_cpu(get_log->wep_icv_err_cnt[2]);
		stats->wep_icv_error[3] =
			le32_to_cpu(get_log->wep_icv_err_cnt[3]);
		stats->bcn_rcv_cnt = le32_to_cpu(get_log->bcn_rcv_cnt);
		stats->bcn_miss_cnt = le32_to_cpu(get_log->bcn_miss_cnt);
	}

	return 0;
}

static int
nxpwifi_cmd_sta_mac_multicast_adr(struct nxpwifi_private *priv,
				  struct host_cmd_ds_command *cmd,
				  u16 cmd_no, void *data_buf,
				  u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_mac_multicast_adr *mcast_addr = &cmd->params.mc_addr;
	struct nxpwifi_multicast_list *mcast_list =
		(struct nxpwifi_multicast_list *)data_buf;

	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_mac_multicast_adr) +
				S_DS_GEN);
	cmd->command = cpu_to_le16(HOST_CMD_MAC_MULTICAST_ADR);

	mcast_addr->action = cpu_to_le16(cmd_action);
	mcast_addr->num_of_adrs =
		cpu_to_le16((u16)mcast_list->num_multicast_addr);
	memcpy(mcast_addr->mac_list, mcast_list->mac_list,
	       mcast_list->num_multicast_addr * ETH_ALEN);

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_associate(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 u16 cmd_no, void *data_buf,
				 u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_802_11_associate(priv, cmd, data_buf);
}

static int
nxpwifi_ret_sta_802_11_associate(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *resp,
				 u16 cmdresp_no,
				 void *data_buf)
{
	return nxpwifi_ret_802_11_associate(priv, resp);
}

static int
nxpwifi_cmd_sta_802_11_snmp_mib(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_no, void *data_buf,
				u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_802_11_snmp_mib *snmp_mib = &cmd->params.smib;
	u16 *ul_temp = (u16 *)data_buf;

	nxpwifi_dbg(priv->adapter, CMD,
		    "cmd: SNMP_CMD: cmd_oid = 0x%x\n", cmd_type);
	cmd->command = cpu_to_le16(HOST_CMD_802_11_SNMP_MIB);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_snmp_mib)
				+ S_DS_GEN);

	snmp_mib->oid = cpu_to_le16((u16)cmd_type);
	if (cmd_action == HOST_ACT_GEN_GET) {
		snmp_mib->query_type = cpu_to_le16(HOST_ACT_GEN_GET);
		snmp_mib->buf_size = cpu_to_le16(MAX_SNMP_BUF_SIZE);
		le16_unaligned_add_cpu(&cmd->size, MAX_SNMP_BUF_SIZE);
	} else if (cmd_action == HOST_ACT_GEN_SET) {
		snmp_mib->query_type = cpu_to_le16(HOST_ACT_GEN_SET);
		snmp_mib->buf_size = cpu_to_le16(sizeof(u16));
		put_unaligned_le16(*ul_temp, snmp_mib->value);
		le16_unaligned_add_cpu(&cmd->size, sizeof(u16));
	}

	nxpwifi_dbg(priv->adapter, CMD,
		    "cmd: SNMP_CMD: Action=0x%x, OID=0x%x,\t"
		    "OIDSize=0x%x, Value=0x%x\n",
		    cmd_action, cmd_type, le16_to_cpu(snmp_mib->buf_size),
		    get_unaligned_le16(snmp_mib->value));

	return 0;
}

static int
nxpwifi_ret_sta_802_11_snmp_mib(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *resp,
				u16 cmdresp_no,
				void *data_buf)
{
	struct host_cmd_ds_802_11_snmp_mib *smib = &resp->params.smib;
	u16 oid = le16_to_cpu(smib->oid);
	u16 query_type = le16_to_cpu(smib->query_type);
	u32 ul_temp;

	nxpwifi_dbg(priv->adapter, INFO,
		    "info: SNMP_RESP: oid value = %#x,\t"
		    "query_type = %#x, buf size = %#x\n",
		    oid, query_type, le16_to_cpu(smib->buf_size));
	if (query_type == HOST_ACT_GEN_GET) {
		ul_temp = get_unaligned_le16(smib->value);
		if (data_buf)
			*(u32 *)data_buf = ul_temp;
		switch (oid) {
		case FRAG_THRESH_I:
			nxpwifi_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: FragThsd =%u\n",
				    ul_temp);
			break;
		case RTS_THRESH_I:
			nxpwifi_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: RTSThsd =%u\n",
				    ul_temp);
			break;
		case SHORT_RETRY_LIM_I:
			nxpwifi_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: TxRetryCount=%u\n",
				    ul_temp);
			break;
		case DTIM_PERIOD_I:
			nxpwifi_dbg(priv->adapter, INFO,
				    "info: SNMP_RESP: DTIM period=%u\n",
				    ul_temp);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int nxpwifi_cmd_sta_reg_access(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *cmd,
				      u16 cmd_no, void *data_buf,
				      u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_ds_reg_rw *reg_rw = data_buf;

	cmd->command = cpu_to_le16(cmd_no);

	switch (cmd_no) {
	case HOST_CMD_MAC_REG_ACCESS:
	{
		struct host_cmd_ds_mac_reg_access *mac_reg;

		cmd->size = cpu_to_le16(sizeof(*mac_reg) + S_DS_GEN);
		mac_reg = &cmd->params.mac_reg;
		mac_reg->action = cpu_to_le16(cmd_action);
		mac_reg->offset = cpu_to_le16((u16)reg_rw->offset);
		mac_reg->value = cpu_to_le32(reg_rw->value);
		break;
	}
	case HOST_CMD_BBP_REG_ACCESS:
	{
		struct host_cmd_ds_bbp_reg_access *bbp_reg;

		cmd->size = cpu_to_le16(sizeof(*bbp_reg) + S_DS_GEN);
		bbp_reg = &cmd->params.bbp_reg;
		bbp_reg->action = cpu_to_le16(cmd_action);
		bbp_reg->offset = cpu_to_le16((u16)reg_rw->offset);
		bbp_reg->value = (u8)reg_rw->value;
		break;
	}
	case HOST_CMD_RF_REG_ACCESS:
	{
		struct host_cmd_ds_rf_reg_access *rf_reg;

		cmd->size = cpu_to_le16(sizeof(*rf_reg) + S_DS_GEN);
		rf_reg = &cmd->params.rf_reg;
		rf_reg->action = cpu_to_le16(cmd_action);
		rf_reg->offset = cpu_to_le16((u16)reg_rw->offset);
		rf_reg->value = (u8)reg_rw->value;
		break;
	}
	case HOST_CMD_PMIC_REG_ACCESS:
	{
		struct host_cmd_ds_pmic_reg_access *pmic_reg;

		cmd->size = cpu_to_le16(sizeof(*pmic_reg) + S_DS_GEN);
		pmic_reg = &cmd->params.pmic_reg;
		pmic_reg->action = cpu_to_le16(cmd_action);
		pmic_reg->offset = cpu_to_le16((u16)reg_rw->offset);
		pmic_reg->value = (u8)reg_rw->value;
		break;
	}
	case HOST_CMD_CAU_REG_ACCESS:
	{
		struct host_cmd_ds_rf_reg_access *cau_reg;

		cmd->size = cpu_to_le16(sizeof(*cau_reg) + S_DS_GEN);
		cau_reg = &cmd->params.rf_reg;
		cau_reg->action = cpu_to_le16(cmd_action);
		cau_reg->offset = cpu_to_le16((u16)reg_rw->offset);
		cau_reg->value = (u8)reg_rw->value;
		break;
	}
	case HOST_CMD_802_11_EEPROM_ACCESS:
	{
		struct nxpwifi_ds_read_eeprom *rd_eeprom = data_buf;
		struct host_cmd_ds_802_11_eeprom_access *cmd_eeprom =
			&cmd->params.eeprom;

		cmd->size = cpu_to_le16(sizeof(*cmd_eeprom) + S_DS_GEN);
		cmd_eeprom->action = cpu_to_le16(cmd_action);
		cmd_eeprom->offset = cpu_to_le16(rd_eeprom->offset);
		cmd_eeprom->byte_count = cpu_to_le16(rd_eeprom->byte_count);
		cmd_eeprom->value = 0;
		break;
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int
nxpwifi_ret_sta_reg_access(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *resp,
			   u16 cmdresp_no,
			   void *data_buf)
{
	struct nxpwifi_ds_reg_rw *reg_rw;
	struct nxpwifi_ds_read_eeprom *eeprom;
	union reg {
		struct host_cmd_ds_mac_reg_access *mac;
		struct host_cmd_ds_bbp_reg_access *bbp;
		struct host_cmd_ds_rf_reg_access *rf;
		struct host_cmd_ds_pmic_reg_access *pmic;
		struct host_cmd_ds_802_11_eeprom_access *eeprom;
	} r;

	if (!data_buf)
		return 0;

	reg_rw = data_buf;
	eeprom = data_buf;
	switch (cmdresp_no) {
	case HOST_CMD_MAC_REG_ACCESS:
		r.mac = &resp->params.mac_reg;
		reg_rw->offset = (u32)le16_to_cpu(r.mac->offset);
		reg_rw->value = le32_to_cpu(r.mac->value);
		break;
	case HOST_CMD_BBP_REG_ACCESS:
		r.bbp = &resp->params.bbp_reg;
		reg_rw->offset = (u32)le16_to_cpu(r.bbp->offset);
		reg_rw->value = (u32)r.bbp->value;
		break;

	case HOST_CMD_RF_REG_ACCESS:
		r.rf = &resp->params.rf_reg;
		reg_rw->offset = (u32)le16_to_cpu(r.rf->offset);
		reg_rw->value = (u32)r.bbp->value;
		break;
	case HOST_CMD_PMIC_REG_ACCESS:
		r.pmic = &resp->params.pmic_reg;
		reg_rw->offset = (u32)le16_to_cpu(r.pmic->offset);
		reg_rw->value = (u32)r.pmic->value;
		break;
	case HOST_CMD_CAU_REG_ACCESS:
		r.rf = &resp->params.rf_reg;
		reg_rw->offset = (u32)le16_to_cpu(r.rf->offset);
		reg_rw->value = (u32)r.rf->value;
		break;
	case HOST_CMD_802_11_EEPROM_ACCESS:
		r.eeprom = &resp->params.eeprom;
		pr_debug("info: EEPROM read len=%x\n",
			 le16_to_cpu(r.eeprom->byte_count));
		if (eeprom->byte_count < le16_to_cpu(r.eeprom->byte_count)) {
			eeprom->byte_count = 0;
			pr_debug("info: EEPROM read length is too big\n");
			return -ENOMEM;
		}
		eeprom->offset = le16_to_cpu(r.eeprom->offset);
		eeprom->byte_count = le16_to_cpu(r.eeprom->byte_count);
		if (eeprom->byte_count > 0)
			memcpy(&eeprom->value, &r.eeprom->value,
			       min((u16)MAX_EEPROM_DATA, eeprom->byte_count));
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int
nxpwifi_cmd_sta_rf_tx_pwr(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_no, void *data_buf,
			  u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_rf_tx_pwr *txp = &cmd->params.txp;

	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_rf_tx_pwr)
				+ S_DS_GEN);
	cmd->command = cpu_to_le16(HOST_CMD_RF_TX_PWR);
	txp->action = cpu_to_le16(cmd_action);

	return 0;
}

static int
nxpwifi_ret_sta_rf_tx_pwr(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *resp,
			  u16 cmdresp_no,
			  void *data_buf)
{
	struct host_cmd_ds_rf_tx_pwr *txp = &resp->params.txp;
	u16 action = le16_to_cpu(txp->action);

	priv->tx_power_level = le16_to_cpu(txp->cur_level);

	if (action == HOST_ACT_GEN_GET) {
		priv->max_tx_power_level = txp->max_power;
		priv->min_tx_power_level = txp->min_power;
	}

	nxpwifi_dbg(priv->adapter, INFO,
		    "Current TxPower Level=%d, Max Power=%d, Min Power=%d\n",
		    priv->tx_power_level, priv->max_tx_power_level,
		    priv->min_tx_power_level);

	return 0;
}

static int
nxpwifi_cmd_sta_rf_antenna(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *cmd,
			   u16 cmd_no, void *data_buf,
			   u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_rf_ant_mimo *ant_mimo = &cmd->params.ant_mimo;
	struct host_cmd_ds_rf_ant_siso *ant_siso = &cmd->params.ant_siso;
	struct nxpwifi_ds_ant_cfg *ant_cfg =
		(struct nxpwifi_ds_ant_cfg *)data_buf;

	cmd->command = cpu_to_le16(HOST_CMD_RF_ANTENNA);

	switch (cmd_action) {
	case HOST_ACT_GEN_SET:
		if (priv->adapter->hw_dev_mcs_support == HT_STREAM_2X2) {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_mimo)
						+ S_DS_GEN);
			ant_mimo->action_tx = cpu_to_le16(HOST_ACT_SET_TX);
			ant_mimo->tx_ant_mode =
				cpu_to_le16((u16)ant_cfg->tx_ant);
			ant_mimo->action_rx = cpu_to_le16(HOST_ACT_SET_RX);
			ant_mimo->rx_ant_mode =
				cpu_to_le16((u16)ant_cfg->rx_ant);
		} else {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_siso) +
						S_DS_GEN);
			ant_siso->action = cpu_to_le16(HOST_ACT_SET_BOTH);
			ant_siso->ant_mode = cpu_to_le16((u16)ant_cfg->tx_ant);
		}
		break;
	case HOST_ACT_GEN_GET:
		if (priv->adapter->hw_dev_mcs_support == HT_STREAM_2X2) {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_mimo) +
						S_DS_GEN);
			ant_mimo->action_tx = cpu_to_le16(HOST_ACT_GET_TX);
			ant_mimo->action_rx = cpu_to_le16(HOST_ACT_GET_RX);
		} else {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_siso) +
						S_DS_GEN);
			ant_siso->action = cpu_to_le16(HOST_ACT_GET_BOTH);
		}
		break;
	}
	return 0;
}

static int
nxpwifi_ret_sta_rf_antenna(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *resp,
			   u16 cmdresp_no,
			   void *data_buf)
{
	struct host_cmd_ds_rf_ant_mimo *ant_mimo = &resp->params.ant_mimo;
	struct host_cmd_ds_rf_ant_siso *ant_siso = &resp->params.ant_siso;
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (adapter->hw_dev_mcs_support == HT_STREAM_2X2) {
		priv->tx_ant = le16_to_cpu(ant_mimo->tx_ant_mode);
		priv->rx_ant = le16_to_cpu(ant_mimo->rx_ant_mode);
		nxpwifi_dbg(adapter, INFO,
			    "RF_ANT_RESP: Tx action = 0x%x, Tx Mode = 0x%04x\t"
			    "Rx action = 0x%x, Rx Mode = 0x%04x\n",
			    le16_to_cpu(ant_mimo->action_tx),
			    le16_to_cpu(ant_mimo->tx_ant_mode),
			    le16_to_cpu(ant_mimo->action_rx),
			    le16_to_cpu(ant_mimo->rx_ant_mode));
	} else {
		priv->tx_ant = le16_to_cpu(ant_siso->ant_mode);
		priv->rx_ant = le16_to_cpu(ant_siso->ant_mode);
		nxpwifi_dbg(adapter, INFO,
			    "RF_ANT_RESP: action = 0x%x, Mode = 0x%04x\n",
			    le16_to_cpu(ant_siso->action),
			    le16_to_cpu(ant_siso->ant_mode));
	}
	return 0;
}

static int
nxpwifi_cmd_sta_802_11_deauthenticate(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *cmd,
				      u16 cmd_no, void *data_buf,
				      u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_802_11_deauthenticate *deauth = &cmd->params.deauth;
	u8 *mac = (u8 *)data_buf;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_DEAUTHENTICATE);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_deauthenticate)
				+ S_DS_GEN);

	/* Set AP MAC address */
	memcpy(deauth->mac_addr, mac, ETH_ALEN);

	nxpwifi_dbg(priv->adapter, CMD, "cmd: Deauth: %pM\n", deauth->mac_addr);

	deauth->reason_code = cpu_to_le16(WLAN_REASON_DEAUTH_LEAVING);

	return 0;
}

static int
nxpwifi_ret_sta_802_11_deauthenticate(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *resp,
				      u16 cmdresp_no,
				      void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->dbg.num_cmd_deauth++;
	if (!memcmp(resp->params.deauth.mac_addr,
		    &priv->curr_bss_params.bss_descriptor.mac_address,
		    sizeof(resp->params.deauth.mac_addr)))
		nxpwifi_reset_connect_state(priv, WLAN_REASON_DEAUTH_LEAVING,
					    false);

	return 0;
}

static int
nxpwifi_cmd_sta_mac_control(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *cmd,
			    u16 cmd_no, void *data_buf,
			    u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_mac_control *mac_ctrl = &cmd->params.mac_ctrl;
	u32 *action = (u32 *)data_buf;

	if (cmd_action != HOST_ACT_GEN_SET) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "mac_control: only support set cmd\n");
		return -EINVAL;
	}

	cmd->command = cpu_to_le16(HOST_CMD_MAC_CONTROL);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_ds_mac_control) + S_DS_GEN);
	mac_ctrl->action = cpu_to_le32(*action);

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_mac_address(struct nxpwifi_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_no, void *data_buf,
				   u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(HOST_CMD_802_11_MAC_ADDRESS);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_mac_address) +
				S_DS_GEN);
	cmd->result = 0;

	cmd->params.mac_addr.action = cpu_to_le16(cmd_action);

	if (cmd_action == HOST_ACT_GEN_SET)
		memcpy(cmd->params.mac_addr.mac_addr, priv->curr_addr,
		       ETH_ALEN);

	return 0;
}

static int
nxpwifi_ret_sta_802_11_mac_address(struct nxpwifi_private *priv,
				   struct host_cmd_ds_command *resp,
				   u16 cmdresp_no,
				   void *data_buf)
{
	struct host_cmd_ds_802_11_mac_address *cmd_mac_addr;

	cmd_mac_addr = &resp->params.mac_addr;

	memcpy(priv->curr_addr, cmd_mac_addr->mac_addr, ETH_ALEN);

	nxpwifi_dbg(priv->adapter, INFO,
		    "info: set mac address: %pM\n", priv->curr_addr);

	return 0;
}

static int
nxpwifi_cmd_sta_802_11d_domain_info(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *cmd,
				    u16 cmd_no, void *data_buf,
				    u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11d_domain_info *domain_info =
		&cmd->params.domain_info;
	struct nxpwifi_ietypes_domain_param_set *domain =
		&domain_info->domain;
	struct nxpwifi_ietypes_domain_code *domain_code;
	u8 no_of_triplet = adapter->domain_reg.no_of_triplet;
	int triplet_size;

	nxpwifi_dbg(adapter, INFO,
		    "info: 11D: no_of_triplet=0x%x\n", no_of_triplet);

	cmd->command = cpu_to_le16(HOST_CMD_802_11D_DOMAIN_INFO);
	cmd->size = cpu_to_le16(S_DS_GEN);
	domain_info->action = cpu_to_le16(cmd_action);
	le16_unaligned_add_cpu(&cmd->size, sizeof(domain_info->action));

	if (cmd_action == HOST_ACT_GEN_GET)
		return 0;

	triplet_size = no_of_triplet *
		sizeof(struct ieee80211_country_ie_triplet);

	domain->header.type = cpu_to_le16(WLAN_EID_COUNTRY);
	domain->header.len =
		cpu_to_le16(sizeof(domain->country_code) + triplet_size);
	memcpy(domain->country_code, adapter->domain_reg.country_code,
	       sizeof(domain->country_code));
	if (no_of_triplet)
		memcpy(domain->triplet, adapter->domain_reg.triplet,
		       triplet_size);
	le16_unaligned_add_cpu(&cmd->size, sizeof(*domain) + triplet_size);

	domain_code = (struct nxpwifi_ietypes_domain_code *)((u8 *)cmd +
		le16_to_cpu(cmd->size));
	domain_code->header.type = cpu_to_le16(TLV_TYPE_REGION_DOMAIN_CODE);
	domain_code->header.len =
		cpu_to_le16(sizeof(*domain_code) -
		sizeof(struct nxpwifi_ie_types_header));
	le16_unaligned_add_cpu(&cmd->size, sizeof(*domain_code));

	return 0;
}

static int
nxpwifi_ret_sta_802_11d_domain_info(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *resp,
				    u16 cmdresp_no,
				    void *data_buf)
{
	struct host_cmd_ds_802_11d_domain_info_rsp *domain_info =
		&resp->params.domain_info_resp;
	struct nxpwifi_ietypes_domain_param_set *domain = &domain_info->domain;
	u16 action = le16_to_cpu(domain_info->action);
	u8 no_of_triplet;

	no_of_triplet = (u8)((le16_to_cpu(domain->header.len)
			     - IEEE80211_COUNTRY_STRING_LEN)
			     / sizeof(struct ieee80211_country_ie_triplet));

	nxpwifi_dbg(priv->adapter, INFO,
		    "info: 11D Domain Info Resp: no_of_triplet=%d\n",
		    no_of_triplet);

	if (no_of_triplet > NXPWIFI_MAX_TRIPLET_802_11D) {
		nxpwifi_dbg(priv->adapter, FATAL,
			    "11D: invalid number of triplets %d returned\n",
			    no_of_triplet);
		return -EINVAL;
	}

	switch (action) {
	case HOST_ACT_GEN_SET:  /* Proc Set Action */
		break;
	case HOST_ACT_GEN_GET:
		break;
	default:
		nxpwifi_dbg(priv->adapter, ERROR,
			    "11D: invalid action:%d\n", domain_info->action);
		return -EINVAL;
	}

	return 0;
}

static int nxpwifi_set_aes_key(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *cmd,
			       struct nxpwifi_ds_encrypt_key *enc_key,
			       struct host_cmd_ds_802_11_key_material *km)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u16 size, len = KEY_PARAMS_FIXED_LEN;
	u8 key_type, key_type_igtk;

	if (enc_key->key_len == WLAN_KEY_LEN_CCMP) {
		key_type = KEY_TYPE_ID_AES;
		key_type_igtk = KEY_TYPE_ID_AES_CMAC;
	} else {
		key_type = KEY_TYPE_ID_GCMP_256;
		key_type_igtk = KEY_TYPE_ID_BIP_GMAC_256;
	}

	if (enc_key->is_igtk_key) {
		km->key_param_set.key_info &= cpu_to_le16(~KEY_MCAST);
		km->key_param_set.key_info |= cpu_to_le16(KEY_IGTK);
		km->key_param_set.key_type = key_type_igtk;
		if (enc_key->key_len == WLAN_KEY_LEN_CCMP) {
			nxpwifi_dbg(adapter, INFO,
				    "%s: Set CMAC AES Key\n", __func__);
			if (enc_key->is_rx_seq_valid)
				memcpy(km->key_param_set.key_params.cmac_aes.ipn,
				       enc_key->pn, enc_key->pn_len);
			km->key_param_set.key_params.cmac_aes.key_len =
				cpu_to_le16(enc_key->key_len);
			memcpy(km->key_param_set.key_params.cmac_aes.key,
			       enc_key->key_material, enc_key->key_len);
			len += sizeof(struct nxpwifi_cmac_aes_param);
		} else {
			nxpwifi_dbg(adapter, INFO,
				    "%s: Set GMAC AES Key\n", __func__);
			if (enc_key->is_rx_seq_valid)
				memcpy(km->key_param_set.key_params.gmac_aes.ipn,
				       enc_key->pn, enc_key->pn_len);
			km->key_param_set.key_params.gmac_aes.key_len =
				cpu_to_le16(enc_key->key_len);
			memcpy(km->key_param_set.key_params.gmac_aes.key,
			       enc_key->key_material, enc_key->key_len);
			len += sizeof(struct nxpwifi_gmac_aes_param);
		}
	} else if (enc_key->is_igtk_def_key) {
		nxpwifi_dbg(adapter, INFO,
			    "%s: Set CMAC default Key index\n", __func__);
		km->key_param_set.key_type = key_type_igtk;
		km->key_param_set.key_idx = enc_key->key_index & KEY_INDEX_MASK;
	} else {
		nxpwifi_dbg(adapter, INFO,
			    "%s: Set AES Key\n", __func__);
		if (enc_key->is_rx_seq_valid)
			memcpy(km->key_param_set.key_params.aes.pn,
			       enc_key->pn, enc_key->pn_len);
		km->key_param_set.key_type = key_type;
		km->key_param_set.key_params.aes.key_len =
					  cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.aes.key,
		       enc_key->key_material, enc_key->key_len);
		len += sizeof(struct nxpwifi_aes_param);
	}

	km->key_param_set.len = cpu_to_le16(len);
	size = len + sizeof(struct nxpwifi_ie_types_header) +
	       sizeof(km->action) + S_DS_GEN;
	cmd->size = cpu_to_le16(size);

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_key_material(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *cmd,
				    u16 cmd_no, void *data_buf,
				    u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ds_encrypt_key *enc_key =
		(struct nxpwifi_ds_encrypt_key *)data_buf;
	u8 *mac = enc_key->mac_addr;
	u16 key_info, len = KEY_PARAMS_FIXED_LEN;
	struct host_cmd_ds_802_11_key_material *km =
		&cmd->params.key_material;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_KEY_MATERIAL);
	km->action = cpu_to_le16(cmd_action);

	if (cmd_action == HOST_ACT_GEN_GET) {
		nxpwifi_dbg(adapter, INFO, "%s: Get key\n", __func__);
		km->key_param_set.key_idx =
			enc_key->key_index & KEY_INDEX_MASK;
		km->key_param_set.type = cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
		km->key_param_set.len = cpu_to_le16(KEY_PARAMS_FIXED_LEN);
		ether_addr_copy(km->key_param_set.mac_addr, mac);

		if (enc_key->key_index & NXPWIFI_KEY_INDEX_UNICAST)
			key_info = KEY_UNICAST;
		else
			key_info = KEY_MCAST;

		if (enc_key->is_igtk_key)
			key_info |= KEY_IGTK;

		km->key_param_set.key_info = cpu_to_le16(key_info);

		cmd->size = cpu_to_le16(sizeof(struct nxpwifi_ie_types_header) +
					S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					sizeof(km->action));
		return 0;
	}

	memset(&km->key_param_set, 0,
	       sizeof(struct nxpwifi_ie_type_key_param_set));

	if (enc_key->key_disable) {
		nxpwifi_dbg(adapter, INFO, "%s: Remove key\n", __func__);
		km->action = cpu_to_le16(HOST_ACT_GEN_REMOVE);
		km->key_param_set.type = cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
		km->key_param_set.len = cpu_to_le16(KEY_PARAMS_FIXED_LEN);
		km->key_param_set.key_idx = enc_key->key_index & KEY_INDEX_MASK;
		key_info = KEY_MCAST | KEY_UNICAST;
		km->key_param_set.key_info = cpu_to_le16(key_info);
		ether_addr_copy(km->key_param_set.mac_addr, mac);
		cmd->size = cpu_to_le16(sizeof(struct nxpwifi_ie_types_header) +
					S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					sizeof(km->action));
		return 0;
	}

	km->action = cpu_to_le16(HOST_ACT_GEN_SET);
	km->key_param_set.key_idx = enc_key->key_index & KEY_INDEX_MASK;
	km->key_param_set.type = cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
	key_info = KEY_ENABLED;
	ether_addr_copy(km->key_param_set.mac_addr, mac);

	if (enc_key->key_len <= WLAN_KEY_LEN_WEP104) {
		nxpwifi_dbg(adapter, INFO, "%s: Set WEP Key\n", __func__);
		len += sizeof(struct nxpwifi_wep_param);
		km->key_param_set.len = cpu_to_le16(len);
		km->key_param_set.key_type = KEY_TYPE_ID_WEP;

		if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
			key_info |= KEY_MCAST | KEY_UNICAST;
		} else {
			if (enc_key->is_current_wep_key) {
				key_info |= KEY_MCAST | KEY_UNICAST;
				if (km->key_param_set.key_idx ==
				    (priv->wep_key_curr_index & KEY_INDEX_MASK))
					key_info |= KEY_DEFAULT;
			} else {
				if (is_broadcast_ether_addr(mac))
					key_info |= KEY_MCAST;
				else
					key_info |= KEY_UNICAST | KEY_DEFAULT;
			}
		}
		km->key_param_set.key_info = cpu_to_le16(key_info);

		km->key_param_set.key_params.wep.key_len =
						  cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.wep.key,
		       enc_key->key_material, enc_key->key_len);

		cmd->size = cpu_to_le16(sizeof(struct nxpwifi_ie_types_header) +
					len + sizeof(km->action) + S_DS_GEN);
		return 0;
	}

	if (is_broadcast_ether_addr(mac))
		key_info |= KEY_MCAST | KEY_RX_KEY;
	else
		key_info |= KEY_UNICAST | KEY_TX_KEY | KEY_RX_KEY;

	/* Enable default key for WPA/WPA2 */
	if (!priv->wpa_is_gtk_set)
		key_info |= KEY_DEFAULT;

	km->key_param_set.key_info = cpu_to_le16(key_info);

	if (enc_key->key_cipher != WLAN_CIPHER_SUITE_TKIP &&
	    enc_key->key_len >= WLAN_KEY_LEN_CCMP)
		return nxpwifi_set_aes_key(priv, cmd, enc_key, km);

	if (enc_key->key_len == WLAN_KEY_LEN_TKIP) {
		nxpwifi_dbg(adapter, INFO,
			    "%s: Set TKIP Key\n", __func__);
		if (enc_key->is_rx_seq_valid)
			memcpy(km->key_param_set.key_params.tkip.pn,
			       enc_key->pn, enc_key->pn_len);
		km->key_param_set.key_type = KEY_TYPE_ID_TKIP;
		km->key_param_set.key_params.tkip.key_len =
						cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.tkip.key,
		       enc_key->key_material, enc_key->key_len);

		len += sizeof(struct nxpwifi_tkip_param);
		km->key_param_set.len = cpu_to_le16(len);
		cmd->size = cpu_to_le16(sizeof(struct nxpwifi_ie_types_header) +
					len + sizeof(km->action) + S_DS_GEN);
	}

	return 0;
}

static int
nxpwifi_ret_sta_802_11_key_material(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *resp,
				    u16 cmdresp_no,
				    void *data_buf)
{
	struct host_cmd_ds_802_11_key_material *key;
	int len;

	key = &resp->params.key_material;

	len = le16_to_cpu(key->key_param_set.key_params.aes.key_len);
	if (len > sizeof(key->key_param_set.key_params.aes.key))
		return -EINVAL;

	if (le16_to_cpu(key->action) == HOST_ACT_GEN_SET) {
		if ((le16_to_cpu(key->key_param_set.key_info) & KEY_MCAST)) {
			nxpwifi_dbg(priv->adapter, INFO,
				    "info: key: GTK is set\n");
			priv->wpa_is_gtk_set = true;
			priv->scan_block = false;
			priv->port_open = true;
		}
	}

	if (key->key_param_set.key_type != KEY_TYPE_ID_AES &&
	    key->key_param_set.key_type != KEY_TYPE_ID_GCMP_256)
		return 0;

	memset(priv->aes_key.key_param_set.key_params.aes.key, 0,
	       sizeof(key->key_param_set.key_params.aes.key));
	priv->aes_key.key_param_set.key_params.aes.key_len = cpu_to_le16(len);
	memcpy(priv->aes_key.key_param_set.key_params.aes.key,
	       key->key_param_set.key_params.aes.key, len);

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_bg_scan_config(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *cmd,
				      u16 cmd_no, void *data_buf,
				      u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_802_11_bg_scan_config(priv, cmd, data_buf);
}

static int
nxpwifi_cmd_sta_802_11_bg_scan_query(struct nxpwifi_private *priv,
				     struct host_cmd_ds_command *cmd,
				     u16 cmd_no, void *data_buf,
				     u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_802_11_bg_scan_query(cmd);
}

static int
nxpwifi_ret_sta_802_11_bg_scan_query(struct nxpwifi_private *priv,
				     struct host_cmd_ds_command *resp,
				     u16 cmdresp_no,
				     void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;

	ret = nxpwifi_ret_802_11_scan(priv, resp);
	cfg80211_sched_scan_results(priv->wdev.wiphy, 0);
	nxpwifi_dbg(adapter, CMD,
		    "info: CMD_RESP: BG_SCAN result is ready!\n");

	return ret;
}

static int
nxpwifi_cmd_sta_wmm_get_status(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *cmd,
			       u16 cmd_no, void *data_buf,
			       u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(HOST_CMD_WMM_GET_STATUS);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_wmm_get_status) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_ret_sta_wmm_get_status(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *resp,
			       u16 cmdresp_no,
			       void *data_buf)
{
	return nxpwifi_ret_wmm_get_status(priv, resp);
}

static int
nxpwifi_cmd_sta_802_11_subsc_evt(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 u16 cmd_no, void *data_buf,
				 u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_802_11_subsc_evt *subsc_evt = &cmd->params.subsc_evt;
	struct nxpwifi_ds_misc_subsc_evt *subsc_evt_cfg =
		(struct nxpwifi_ds_misc_subsc_evt *)data_buf;
	struct nxpwifi_ie_types_rssi_threshold *rssi_tlv;
	u16 event_bitmap;
	u8 *pos;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_SUBSCRIBE_EVENT);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_subsc_evt) +
				S_DS_GEN);

	subsc_evt->action = cpu_to_le16(subsc_evt_cfg->action);
	nxpwifi_dbg(priv->adapter, CMD,
		    "cmd: action: %d\n", subsc_evt_cfg->action);

	/* For query requests, no configuration TLV structures are to be added. */
	if (subsc_evt_cfg->action == HOST_ACT_GEN_GET)
		return 0;

	subsc_evt->events = cpu_to_le16(subsc_evt_cfg->events);

	event_bitmap = subsc_evt_cfg->events;
	nxpwifi_dbg(priv->adapter, CMD, "cmd: event bitmap : %16x\n",
		    event_bitmap);

	if ((subsc_evt_cfg->action == HOST_ACT_BITWISE_CLR ||
	     subsc_evt_cfg->action == HOST_ACT_BITWISE_SET) &&
	    event_bitmap == 0) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Error: No event specified\t"
			    "for bitwise action type\n");
		return -EINVAL;
	}

	/*
	 * Append TLV structures for each of the specified events for
	 * subscribing or re-configuring. This is not required for
	 * bitwise unsubscribing request.
	 */
	if (subsc_evt_cfg->action == HOST_ACT_BITWISE_CLR)
		return 0;

	pos = ((u8 *)subsc_evt) +
			sizeof(struct host_cmd_ds_802_11_subsc_evt);

	if (event_bitmap & BITMASK_BCN_RSSI_LOW) {
		rssi_tlv = (struct nxpwifi_ie_types_rssi_threshold *)pos;

		rssi_tlv->header.type = cpu_to_le16(TLV_TYPE_RSSI_LOW);
		rssi_tlv->header.len =
		    cpu_to_le16(sizeof(struct nxpwifi_ie_types_rssi_threshold) -
				sizeof(struct nxpwifi_ie_types_header));
		rssi_tlv->abs_value = subsc_evt_cfg->bcn_l_rssi_cfg.abs_value;
		rssi_tlv->evt_freq = subsc_evt_cfg->bcn_l_rssi_cfg.evt_freq;

		nxpwifi_dbg(priv->adapter, EVENT,
			    "Cfg Beacon Low Rssi event,\t"
			    "RSSI:-%d dBm, Freq:%d\n",
			    subsc_evt_cfg->bcn_l_rssi_cfg.abs_value,
			    subsc_evt_cfg->bcn_l_rssi_cfg.evt_freq);

		pos += sizeof(struct nxpwifi_ie_types_rssi_threshold);
		le16_unaligned_add_cpu
		(&cmd->size,
		 sizeof(struct nxpwifi_ie_types_rssi_threshold));
	}

	if (event_bitmap & BITMASK_BCN_RSSI_HIGH) {
		rssi_tlv = (struct nxpwifi_ie_types_rssi_threshold *)pos;

		rssi_tlv->header.type = cpu_to_le16(TLV_TYPE_RSSI_HIGH);
		rssi_tlv->header.len =
		    cpu_to_le16(sizeof(struct nxpwifi_ie_types_rssi_threshold) -
				sizeof(struct nxpwifi_ie_types_header));
		rssi_tlv->abs_value = subsc_evt_cfg->bcn_h_rssi_cfg.abs_value;
		rssi_tlv->evt_freq = subsc_evt_cfg->bcn_h_rssi_cfg.evt_freq;

		nxpwifi_dbg(priv->adapter, EVENT,
			    "Cfg Beacon High Rssi event,\t"
			    "RSSI:-%d dBm, Freq:%d\n",
			    subsc_evt_cfg->bcn_h_rssi_cfg.abs_value,
			    subsc_evt_cfg->bcn_h_rssi_cfg.evt_freq);

		pos += sizeof(struct nxpwifi_ie_types_rssi_threshold);
		le16_unaligned_add_cpu
		(&cmd->size,
		 sizeof(struct nxpwifi_ie_types_rssi_threshold));
	}

	return 0;
}

static int
nxpwifi_ret_sta_subsc_evt(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *resp,
			  u16 cmdresp_no,
			  void *data_buf)
{
	struct host_cmd_ds_802_11_subsc_evt *cmd_sub_event =
		&resp->params.subsc_evt;

	/*
	 * For every subscribe event command (Get/Set/Clear), FW reports the current
	 * set of subscribed events
	 */
	nxpwifi_dbg(priv->adapter, EVENT,
		    "Bitmap of currently subscribed events: %16x\n",
		    le16_to_cpu(cmd_sub_event->events));

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_tx_rate_query(struct nxpwifi_private *priv,
				     struct host_cmd_ds_command *cmd,
				     u16 cmd_no, void *data_buf,
				     u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(HOST_CMD_802_11_TX_RATE_QUERY);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_tx_rate_query) +
				S_DS_GEN);
	priv->tx_rate = 0;

	return 0;
}

static int
nxpwifi_ret_sta_802_11_tx_rate_query(struct nxpwifi_private *priv,
				     struct host_cmd_ds_command *resp,
				     u16 cmdresp_no,
				     void *data_buf)
{
	priv->tx_rate = resp->params.tx_rate.tx_rate;
	priv->tx_htinfo = resp->params.tx_rate.ht_info;
	if (!priv->is_data_rate_auto)
		priv->data_rate =
			nxpwifi_index_to_data_rate(priv, priv->tx_rate,
						   priv->tx_htinfo);

	return 0;
}

static int
nxpwifi_cmd_sta_mem_access(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *cmd,
			   u16 cmd_no, void *data_buf,
			   u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_ds_mem_rw *mem_rw =
		(struct nxpwifi_ds_mem_rw *)data_buf;
	struct host_cmd_ds_mem_access *mem_access = (void *)&cmd->params.mem;

	cmd->command = cpu_to_le16(HOST_CMD_MEM_ACCESS);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_mem_access) +
				S_DS_GEN);

	mem_access->action = cpu_to_le16(cmd_action);
	mem_access->addr = cpu_to_le32(mem_rw->addr);
	mem_access->value = cpu_to_le32(mem_rw->value);

	return 0;
}

static int
nxpwifi_ret_sta_mem_access(struct nxpwifi_private *priv,
			   struct host_cmd_ds_command *resp,
			   u16 cmdresp_no,
			   void *data_buf)
{
	struct host_cmd_ds_mem_access *mem = (void *)&resp->params.mem;

	priv->mem_rw.addr = le32_to_cpu(mem->addr);
	priv->mem_rw.value = le32_to_cpu(mem->value);

	return 0;
}

static u32 nxpwifi_parse_cal_cfg(u8 *src, size_t len, u8 *dst)
{
	u8 *s = src, *d = dst;

	while (s - src < len) {
		if (*s && (isspace(*s) || *s == '\t')) {
			s++;
			continue;
		}
		if (isxdigit(*s)) {
			if (kstrtou8(s, 16, d))
				return 0;
			d++;
			s += 2;
		} else {
			s++;
		}
	}

	return d - dst;
}

static int
nxpwifi_cmd_sta_cfg_data(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd,
			 u16 cmd_no, void *data_buf,
			 u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u32 len;
	u8 *data = (u8 *)cmd + S_DS_GEN;

	if (adapter->cal_data->data && adapter->cal_data->size > 0) {
		len = nxpwifi_parse_cal_cfg((u8 *)adapter->cal_data->data,
					    adapter->cal_data->size, data);
		nxpwifi_dbg(adapter, INFO,
			    "download cfg_data from config file\n");
	} else {
		return -EINVAL;
	}

	cmd->command = cpu_to_le16(HOST_CMD_CFG_DATA);
	cmd->size = cpu_to_le16(S_DS_GEN + len);

	return 0;
}

static int
nxpwifi_ret_sta_cfg_data(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 u16 cmdresp_no,
			 void *data_buf)
{
	if (resp->result != HOST_RESULT_OK) {
		nxpwifi_dbg(priv->adapter, ERROR, "Cal data cmd resp failed\n");
		return -EINVAL;
	}

	return 0;
}

static int
nxpwifi_cmd_sta_ver_ext(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd,
			u16 cmd_no, void *data_buf,
			u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(cmd_no);
	cmd->params.verext.version_str_sel =
		(u8)(get_unaligned((u32 *)data_buf));
	memcpy(&cmd->params, data_buf, sizeof(struct host_cmd_ds_version_ext));
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_version_ext) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_ret_sta_ver_ext(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *resp,
			u16 cmdresp_no,
			void *data_buf)
{
	struct host_cmd_ds_version_ext *ver_ext = &resp->params.verext;
	struct host_cmd_ds_version_ext *version_ext =
		(struct host_cmd_ds_version_ext *)data_buf;

	if (test_and_clear_bit(NXPWIFI_IS_REQUESTING_FW_VEREXT, &priv->adapter->work_flags)) {
		if (strncmp(ver_ext->version_str, "ChipRev:20, BB:9b(10.00), RF:40(21)",
			    NXPWIFI_VERSION_STR_LENGTH) == 0) {
			struct nxpwifi_ds_auto_ds auto_ds = {
				.auto_ds = DEEP_SLEEP_OFF,
			};

			nxpwifi_dbg(priv->adapter, MSG,
				    "Bad HW revision detected, disabling deep sleep\n");

			if (nxpwifi_send_cmd(priv, HOST_CMD_802_11_PS_MODE_ENH,
					     DIS_AUTO_PS, BITMAP_AUTO_DS, &auto_ds, false)) {
				nxpwifi_dbg(priv->adapter, MSG,
					    "Disabling deep sleep failed.\n");
			}
		}

		return 0;
	}

	if (version_ext) {
		version_ext->version_str_sel = ver_ext->version_str_sel;
		memcpy(version_ext->version_str, ver_ext->version_str,
		       NXPWIFI_VERSION_STR_LENGTH);
		memcpy(priv->version_str, ver_ext->version_str,
		       NXPWIFI_VERSION_STR_LENGTH);

		/* Ensure the version string from the firmware is 0-terminated */
		priv->version_str[NXPWIFI_VERSION_STR_LENGTH - 1] = '\0';
	}
	return 0;
}

static int
nxpwifi_cmd_append_rpn_expression(struct nxpwifi_private *priv,
				  struct nxpwifi_mef_entry *mef_entry,
				  u8 **buffer)
{
	struct nxpwifi_mef_filter *filter = mef_entry->filter;
	int i, byte_len;
	u8 *stack_ptr = *buffer;

	for (i = 0; i < NXPWIFI_MEF_MAX_FILTERS; i++) {
		filter = &mef_entry->filter[i];
		if (!filter->filt_type)
			break;
		put_unaligned_le32((u32)filter->repeat, stack_ptr);
		stack_ptr += 4;
		*stack_ptr = TYPE_DNUM;
		stack_ptr += 1;

		byte_len = filter->byte_seq[NXPWIFI_MEF_MAX_BYTESEQ];
		memcpy(stack_ptr, filter->byte_seq, byte_len);
		stack_ptr += byte_len;
		*stack_ptr = byte_len;
		stack_ptr += 1;
		*stack_ptr = TYPE_BYTESEQ;
		stack_ptr += 1;
		put_unaligned_le32((u32)filter->offset, stack_ptr);
		stack_ptr += 4;
		*stack_ptr = TYPE_DNUM;
		stack_ptr += 1;

		*stack_ptr = filter->filt_type;
		stack_ptr += 1;

		if (filter->filt_action) {
			*stack_ptr = filter->filt_action;
			stack_ptr += 1;
		}

		if (stack_ptr - *buffer > STACK_NBYTES)
			return -ENOMEM;
	}

	*buffer = stack_ptr;
	return 0;
}

static int
nxpwifi_cmd_sta_mef_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd,
			u16 cmd_no, void *data_buf,
			u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_mef_cfg *mef_cfg = &cmd->params.mef_cfg;
	struct nxpwifi_ds_mef_cfg *mef =
		(struct nxpwifi_ds_mef_cfg *)data_buf;
	struct nxpwifi_fw_mef_entry *mef_entry = NULL;
	u8 *pos = (u8 *)mef_cfg;
	u16 i;
	int ret = 0;

	cmd->command = cpu_to_le16(HOST_CMD_MEF_CFG);

	mef_cfg->criteria = cpu_to_le32(mef->criteria);
	mef_cfg->num_entries = cpu_to_le16(mef->num_entries);
	pos += sizeof(*mef_cfg);

	for (i = 0; i < mef->num_entries; i++) {
		mef_entry = (struct nxpwifi_fw_mef_entry *)pos;
		mef_entry->mode = mef->mef_entry[i].mode;
		mef_entry->action = mef->mef_entry[i].action;
		pos += sizeof(*mef_entry);

		ret = nxpwifi_cmd_append_rpn_expression(priv,
							&mef->mef_entry[i],
							&pos);
		if (ret)
			return ret;

		mef_entry->exprsize =
			cpu_to_le16(pos - mef_entry->expr);
	}
	cmd->size = cpu_to_le16((u16)(pos - (u8 *)mef_cfg) + S_DS_GEN);

	return ret;
}

static int
nxpwifi_cmd_sta_802_11_rssi_info(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 u16 cmd_no, void *data_buf,
				 u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(HOST_CMD_RSSI_INFO);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_rssi_info) +
				S_DS_GEN);
	cmd->params.rssi_info.action = cpu_to_le16(cmd_action);
	cmd->params.rssi_info.ndata = cpu_to_le16(priv->data_avg_factor);
	cmd->params.rssi_info.nbcn = cpu_to_le16(priv->bcn_avg_factor);

	/* Reset SNR/NF/RSSI values in private structure */
	priv->data_rssi_last = 0;
	priv->data_nf_last = 0;
	priv->data_rssi_avg = 0;
	priv->data_nf_avg = 0;
	priv->bcn_rssi_last = 0;
	priv->bcn_nf_last = 0;
	priv->bcn_rssi_avg = 0;
	priv->bcn_nf_avg = 0;

	return 0;
}

static int
nxpwifi_ret_sta_802_11_rssi_info(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *resp,
				 u16 cmdresp_no,
				 void *data_buf)
{
	struct host_cmd_ds_802_11_rssi_info_rsp *rssi_info_rsp =
		&resp->params.rssi_info_rsp;
	struct nxpwifi_ds_misc_subsc_evt *subsc_evt =
		&priv->async_subsc_evt_storage;

	priv->data_rssi_last = le16_to_cpu(rssi_info_rsp->data_rssi_last);
	priv->data_nf_last = le16_to_cpu(rssi_info_rsp->data_nf_last);

	priv->data_rssi_avg = le16_to_cpu(rssi_info_rsp->data_rssi_avg);
	priv->data_nf_avg = le16_to_cpu(rssi_info_rsp->data_nf_avg);

	priv->bcn_rssi_last = le16_to_cpu(rssi_info_rsp->bcn_rssi_last);
	priv->bcn_nf_last = le16_to_cpu(rssi_info_rsp->bcn_nf_last);

	priv->bcn_rssi_avg = le16_to_cpu(rssi_info_rsp->bcn_rssi_avg);
	priv->bcn_nf_avg = le16_to_cpu(rssi_info_rsp->bcn_nf_avg);

	if (priv->subsc_evt_rssi_state == EVENT_HANDLED)
		return 0;

	memset(subsc_evt, 0x00, sizeof(struct nxpwifi_ds_misc_subsc_evt));

	/* Resubscribe low and high rssi events with new thresholds */
	subsc_evt->events = BITMASK_BCN_RSSI_LOW | BITMASK_BCN_RSSI_HIGH;
	subsc_evt->action = HOST_ACT_BITWISE_SET;
	if (priv->subsc_evt_rssi_state == RSSI_LOW_RECVD) {
		subsc_evt->bcn_l_rssi_cfg.abs_value = abs(priv->bcn_rssi_avg -
				priv->cqm_rssi_hyst);
		subsc_evt->bcn_h_rssi_cfg.abs_value = abs(priv->cqm_rssi_thold);
	} else if (priv->subsc_evt_rssi_state == RSSI_HIGH_RECVD) {
		subsc_evt->bcn_l_rssi_cfg.abs_value = abs(priv->cqm_rssi_thold);
		subsc_evt->bcn_h_rssi_cfg.abs_value = abs(priv->bcn_rssi_avg +
				priv->cqm_rssi_hyst);
	}
	subsc_evt->bcn_l_rssi_cfg.evt_freq = 1;
	subsc_evt->bcn_h_rssi_cfg.evt_freq = 1;

	priv->subsc_evt_rssi_state = EVENT_HANDLED;

	nxpwifi_send_cmd(priv, HOST_CMD_802_11_SUBSCRIBE_EVENT,
			 0, 0, subsc_evt, false);

	return 0;
}

static int
nxpwifi_cmd_sta_func_init(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_no, void *data_buf,
			  u16 cmd_action, u32 cmd_type)
{
	if (priv->adapter->hw_status == NXPWIFI_HW_STATUS_RESET)
		priv->adapter->hw_status = NXPWIFI_HW_STATUS_READY;
	cmd->command = cpu_to_le16(cmd_no);
	cmd->size = cpu_to_le16(S_DS_GEN);

	return 0;
}

static int
nxpwifi_cmd_sta_func_shutdown(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *cmd,
			      u16 cmd_no, void *data_buf,
			      u16 cmd_action, u32 cmd_type)
{
	priv->adapter->hw_status = NXPWIFI_HW_STATUS_RESET;
	cmd->command = cpu_to_le16(cmd_no);
	cmd->size = cpu_to_le16(S_DS_GEN);

	return 0;
}

static int
nxpwifi_cmd_sta_11n_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd,
			u16 cmd_no, void *data_buf,
			u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11n_cfg(priv, cmd, cmd_action, data_buf);
}

static int
nxpwifi_cmd_sta_11n_addba_req(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *cmd,
			      u16 cmd_no, void *data_buf,
			      u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11n_addba_req(cmd, data_buf);
}

static int
nxpwifi_ret_sta_11n_addba_req(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp,
			      u16 cmdresp_no,
			      void *data_buf)
{
	return nxpwifi_ret_11n_addba_req(priv, resp);
}

static int
nxpwifi_cmd_sta_11n_addba_rsp(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *cmd,
			      u16 cmd_no, void *data_buf,
			      u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11n_addba_rsp_gen(priv, cmd, data_buf);
}

static int
nxpwifi_ret_sta_11n_addba_rsp(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp,
			      u16 cmdresp_no,
			      void *data_buf)
{
	return nxpwifi_ret_11n_addba_resp(priv, resp);
}

static int
nxpwifi_cmd_sta_11n_delba(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_no, void *data_buf,
			  u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11n_delba(cmd, data_buf);
}

static int
nxpwifi_ret_sta_11n_delba(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *resp,
			  u16 cmdresp_no,
			  void *data_buf)
{
	return nxpwifi_ret_11n_delba(priv, resp);
}

static int
nxpwifi_cmd_sta_tx_power_cfg(struct nxpwifi_private *priv,
			     struct host_cmd_ds_command *cmd,
			     u16 cmd_no, void *data_buf,
			     u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_types_power_group *pg_tlv;
	struct host_cmd_ds_txpwr_cfg *cmd_txp_cfg = &cmd->params.txp_cfg;
	struct host_cmd_ds_txpwr_cfg *txp =
		(struct host_cmd_ds_txpwr_cfg *)data_buf;

	cmd->command = cpu_to_le16(HOST_CMD_TXPWR_CFG);
	cmd->size =
		cpu_to_le16(S_DS_GEN + sizeof(struct host_cmd_ds_txpwr_cfg));
	switch (cmd_action) {
	case HOST_ACT_GEN_SET:
		if (txp->mode) {
			pg_tlv = (struct nxpwifi_types_power_group
				  *)((unsigned long)txp +
				     sizeof(struct host_cmd_ds_txpwr_cfg));
			memmove(cmd_txp_cfg, txp,
				sizeof(struct host_cmd_ds_txpwr_cfg) +
				sizeof(struct nxpwifi_types_power_group) +
				le16_to_cpu(pg_tlv->length));

			pg_tlv = (struct nxpwifi_types_power_group *)((u8 *)
				  cmd_txp_cfg +
				  sizeof(struct host_cmd_ds_txpwr_cfg));
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size) +
				  sizeof(struct nxpwifi_types_power_group) +
				  le16_to_cpu(pg_tlv->length));
		} else {
			memmove(cmd_txp_cfg, txp, sizeof(*txp));
		}
		cmd_txp_cfg->action = cpu_to_le16(cmd_action);
		break;
	case HOST_ACT_GEN_GET:
		cmd_txp_cfg->action = cpu_to_le16(cmd_action);
		break;
	}

	return 0;
}

static int nxpwifi_get_power_level(struct nxpwifi_private *priv, void *data_buf)
{
	int length, max_power = -1, min_power = -1;
	struct nxpwifi_types_power_group *pg_tlv_hdr;
	struct nxpwifi_power_group *pg;

	if (!data_buf)
		return -ENOMEM;

	pg_tlv_hdr = (struct nxpwifi_types_power_group *)((u8 *)data_buf);
	pg = (struct nxpwifi_power_group *)
		((u8 *)pg_tlv_hdr + sizeof(struct nxpwifi_types_power_group));
	length = le16_to_cpu(pg_tlv_hdr->length);

	/* At least one structure required to update power */
	if (length < sizeof(struct nxpwifi_power_group))
		return 0;

	max_power = pg->power_max;
	min_power = pg->power_min;
	length -= sizeof(struct nxpwifi_power_group);

	while (length >= sizeof(struct nxpwifi_power_group)) {
		pg++;
		if (max_power < pg->power_max)
			max_power = pg->power_max;

		if (min_power > pg->power_min)
			min_power = pg->power_min;

		length -= sizeof(struct nxpwifi_power_group);
	}
	priv->min_tx_power_level = (u8)min_power;
	priv->max_tx_power_level = (u8)max_power;

	return 0;
}

static int
nxpwifi_ret_sta_tx_power_cfg(struct nxpwifi_private *priv,
			     struct host_cmd_ds_command *resp,
			     u16 cmdresp_no,
			     void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_txpwr_cfg *txp_cfg = &resp->params.txp_cfg;
	struct nxpwifi_types_power_group *pg_tlv_hdr;
	struct nxpwifi_power_group *pg;
	u16 action = le16_to_cpu(txp_cfg->action);
	u16 tlv_buf_left;

	pg_tlv_hdr = (struct nxpwifi_types_power_group *)
		((u8 *)txp_cfg +
		 sizeof(struct host_cmd_ds_txpwr_cfg));

	pg = (struct nxpwifi_power_group *)
		((u8 *)pg_tlv_hdr +
		 sizeof(struct nxpwifi_types_power_group));

	tlv_buf_left = le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*txp_cfg);
	if (tlv_buf_left <
			le16_to_cpu(pg_tlv_hdr->length) + sizeof(*pg_tlv_hdr))
		return 0;

	switch (action) {
	case HOST_ACT_GEN_GET:
		if (adapter->hw_status == NXPWIFI_HW_STATUS_INITIALIZING)
			nxpwifi_get_power_level(priv, pg_tlv_hdr);

		priv->tx_power_level = (u16)pg->power_min;
		break;

	case HOST_ACT_GEN_SET:
		if (!le32_to_cpu(txp_cfg->mode))
			break;

		if (pg->power_max == pg->power_min)
			priv->tx_power_level = (u16)pg->power_min;
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "CMD_RESP: unknown cmd action %d\n",
			    action);
		return 0;
	}
	nxpwifi_dbg(adapter, INFO,
		    "info: Current TxPower Level = %d, Max Power=%d, Min Power=%d\n",
		    priv->tx_power_level, priv->max_tx_power_level,
		    priv->min_tx_power_level);

	return 0;
}

static int
nxpwifi_cmd_sta_tx_rate_cfg(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *cmd,
			    u16 cmd_no, void *data_buf,
			    u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_tx_rate_cfg *rate_cfg = &cmd->params.tx_rate_cfg;
	u16 *pbitmap_rates = (u16 *)data_buf;
	struct nxpwifi_rate_scope *rate_scope;
	struct nxpwifi_rate_drop_pattern *rate_drop;
	u32 i;

	cmd->command = cpu_to_le16(HOST_CMD_TX_RATE_CFG);

	rate_cfg->action = cpu_to_le16(cmd_action);
	rate_cfg->cfg_index = 0;

	rate_scope = (struct nxpwifi_rate_scope *)((u8 *)rate_cfg +
		      sizeof(struct host_cmd_ds_tx_rate_cfg));
	rate_scope->type = cpu_to_le16(TLV_TYPE_RATE_SCOPE);
	rate_scope->length = cpu_to_le16
		(sizeof(*rate_scope) - sizeof(struct nxpwifi_ie_types_header));
	if (pbitmap_rates) {
		rate_scope->hr_dsss_rate_bitmap = cpu_to_le16(pbitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap = cpu_to_le16(pbitmap_rates[1]);
		for (i = 0; i < ARRAY_SIZE(rate_scope->ht_mcs_rate_bitmap); i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				cpu_to_le16(pbitmap_rates[2 + i]);
		if (priv->adapter->fw_api_ver == NXPWIFI_FW_V15) {
			for (i = 0;
			     i < ARRAY_SIZE(rate_scope->vht_mcs_rate_bitmap);
			     i++)
				rate_scope->vht_mcs_rate_bitmap[i] =
					cpu_to_le16(pbitmap_rates[10 + i]);
		}
	} else {
		rate_scope->hr_dsss_rate_bitmap =
			cpu_to_le16(priv->bitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap =
			cpu_to_le16(priv->bitmap_rates[1]);
		for (i = 0; i < ARRAY_SIZE(rate_scope->ht_mcs_rate_bitmap); i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				cpu_to_le16(priv->bitmap_rates[2 + i]);
		if (priv->adapter->fw_api_ver == NXPWIFI_FW_V15) {
			for (i = 0;
			     i < ARRAY_SIZE(rate_scope->vht_mcs_rate_bitmap);
			     i++)
				rate_scope->vht_mcs_rate_bitmap[i] =
					cpu_to_le16(priv->bitmap_rates[10 + i]);
		}
	}

	rate_drop = (struct nxpwifi_rate_drop_pattern *)((u8 *)rate_scope +
					     sizeof(struct nxpwifi_rate_scope));
	rate_drop->type = cpu_to_le16(TLV_TYPE_RATE_DROP_CONTROL);
	rate_drop->length = cpu_to_le16(sizeof(rate_drop->rate_drop_mode));
	rate_drop->rate_drop_mode = 0;

	cmd->size =
		cpu_to_le16(S_DS_GEN + sizeof(struct host_cmd_ds_tx_rate_cfg) +
			    sizeof(struct nxpwifi_rate_scope) +
			    sizeof(struct nxpwifi_rate_drop_pattern));

	return 0;
}

static void nxpwifi_ret_rate_scope(struct nxpwifi_private *priv, u8 *tlv_buf)
{
	struct nxpwifi_rate_scope *rate_scope;
	int i;

	rate_scope = (struct nxpwifi_rate_scope *)tlv_buf;
	priv->bitmap_rates[0] =
		le16_to_cpu(rate_scope->hr_dsss_rate_bitmap);
	priv->bitmap_rates[1] =
		le16_to_cpu(rate_scope->ofdm_rate_bitmap);
	for (i = 0; i < ARRAY_SIZE(rate_scope->ht_mcs_rate_bitmap); i++)
		priv->bitmap_rates[2 + i] =
			le16_to_cpu(rate_scope->ht_mcs_rate_bitmap[i]);

	if (priv->adapter->fw_api_ver == NXPWIFI_FW_V15) {
		for (i = 0; i < ARRAY_SIZE(rate_scope->vht_mcs_rate_bitmap);
		     i++)
			priv->bitmap_rates[10 + i] =
				le16_to_cpu(rate_scope->vht_mcs_rate_bitmap[i]);
	}
}

static int
nxpwifi_ret_sta_tx_rate_cfg(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *resp,
			    u16 cmdresp_no,
			    void *data_buf)
{
	struct host_cmd_ds_tx_rate_cfg *rate_cfg = &resp->params.tx_rate_cfg;
	struct nxpwifi_ie_types_header *head;
	u16 tlv, tlv_buf_len, tlv_buf_left;
	u8 *tlv_buf;

	tlv_buf = ((u8 *)rate_cfg) + sizeof(struct host_cmd_ds_tx_rate_cfg);
	tlv_buf_left = le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*rate_cfg);

	while (tlv_buf_left >= sizeof(*head)) {
		head = (struct nxpwifi_ie_types_header *)tlv_buf;
		tlv = le16_to_cpu(head->type);
		tlv_buf_len = le16_to_cpu(head->len);

		if (tlv_buf_left < (sizeof(*head) + tlv_buf_len))
			break;

		switch (tlv) {
		case TLV_TYPE_RATE_SCOPE:
			nxpwifi_ret_rate_scope(priv, tlv_buf);
			break;
			/* Add RATE_DROP tlv here */
		}

		tlv_buf += (sizeof(*head) + tlv_buf_len);
		tlv_buf_left -= (sizeof(*head) + tlv_buf_len);
	}

	priv->is_data_rate_auto = nxpwifi_is_rate_auto(priv);

	if (priv->is_data_rate_auto)
		priv->data_rate = 0;
	else
		return nxpwifi_send_cmd(priv, HOST_CMD_802_11_TX_RATE_QUERY,
					HOST_ACT_GEN_GET, 0, NULL, false);

	return 0;
}

static int
nxpwifi_cmd_sta_reconfigure_rx_buff(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *cmd,
				    u16 cmd_no, void *data_buf,
				    u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_recfg_tx_buf(priv, cmd, cmd_action, data_buf);
}

static int
nxpwifi_ret_sta_reconfigure_rx_buff(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *resp,
				    u16 cmdresp_no,
				    void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (0xffff != (u16)le16_to_cpu(resp->params.tx_buf.buff_size)) {
		adapter->tx_buf_size =
			(u16)le16_to_cpu(resp->params.tx_buf.buff_size);
		adapter->tx_buf_size =
			(adapter->tx_buf_size / NXPWIFI_SDIO_BLOCK_SIZE) *
			NXPWIFI_SDIO_BLOCK_SIZE;
		adapter->curr_tx_buf_size = adapter->tx_buf_size;
		nxpwifi_dbg(adapter, CMD, "cmd: curr_tx_buf_size=%d\n",
			    adapter->curr_tx_buf_size);

		if (adapter->if_ops.update_mp_end_port) {
			u16 mp_end_port;

			mp_end_port =
				le16_to_cpu(resp->params.tx_buf.mp_end_port);
			adapter->if_ops.update_mp_end_port(adapter,
							   mp_end_port);
		}
	}

	return 0;
}

static int
nxpwifi_cmd_sta_chan_report_request(struct nxpwifi_private *priv,
				    struct host_cmd_ds_command *cmd,
				    u16 cmd_no, void *data_buf,
				    u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_issue_chan_report_request(priv, cmd, data_buf);
}

static int
nxpwifi_cmd_sta_amsdu_aggr_ctrl(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_no, void *data_buf,
				u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_amsdu_aggr_ctrl(cmd, cmd_action, data_buf);
}

static int
nxpwifi_cmd_sta_robust_coex(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *cmd,
			    u16 cmd_no, void *data_buf,
			    u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_robust_coex *coex = &cmd->params.coex;
	bool *is_timeshare = (bool *)data_buf;
	struct nxpwifi_ie_types_robust_coex *coex_tlv;

	cmd->command = cpu_to_le16(HOST_CMD_ROBUST_COEX);
	cmd->size = cpu_to_le16(sizeof(*coex) + sizeof(*coex_tlv) + S_DS_GEN);

	coex->action = cpu_to_le16(cmd_action);
	coex_tlv = (struct nxpwifi_ie_types_robust_coex *)
				((u8 *)coex + sizeof(*coex));
	coex_tlv->header.type = cpu_to_le16(TLV_TYPE_ROBUST_COEX);
	coex_tlv->header.len = cpu_to_le16(sizeof(coex_tlv->mode));

	if (coex->action == HOST_ACT_GEN_GET)
		return 0;

	if (*is_timeshare)
		coex_tlv->mode = cpu_to_le32(NXPWIFI_COEX_MODE_TIMESHARE);
	else
		coex_tlv->mode = cpu_to_le32(NXPWIFI_COEX_MODE_SPATIAL);

	return 0;
}

static int
nxpwifi_ret_sta_robust_coex(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *resp,
			    u16 cmdresp_no,
			    void *data_buf)
{
	struct host_cmd_ds_robust_coex *coex = &resp->params.coex;
	bool *is_timeshare = (bool *)data_buf;
	struct nxpwifi_ie_types_robust_coex *coex_tlv;
	u16 action = le16_to_cpu(coex->action);
	u32 mode;

	coex_tlv = (struct nxpwifi_ie_types_robust_coex
		    *)((u8 *)coex + sizeof(struct host_cmd_ds_robust_coex));
	if (action == HOST_ACT_GEN_GET) {
		mode = le32_to_cpu(coex_tlv->mode);
		if (mode == NXPWIFI_COEX_MODE_TIMESHARE)
			*is_timeshare = true;
		else
			*is_timeshare = false;
	}

	return 0;
}

static int
nxpwifi_cmd_sta_enh_power_mode(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *cmd,
			       u16 cmd_no, void *data_buf,
			       u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_802_11_ps_mode_enh *psmode_enh =
		&cmd->params.psmode_enh;
	u16 ps_bitmap = (u16)cmd_type;
	struct nxpwifi_ds_auto_ds *auto_ds =
		(struct nxpwifi_ds_auto_ds *)data_buf;
	u8 *tlv;
	u16 cmd_size = 0;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_PS_MODE_ENH);
	if (cmd_action == DIS_AUTO_PS) {
		psmode_enh->action = cpu_to_le16(DIS_AUTO_PS);
		psmode_enh->params.ps_bitmap = cpu_to_le16(ps_bitmap);
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof(psmode_enh->action) +
					sizeof(psmode_enh->params.ps_bitmap));
	} else if (cmd_action == GET_PS) {
		psmode_enh->action = cpu_to_le16(GET_PS);
		psmode_enh->params.ps_bitmap = cpu_to_le16(ps_bitmap);
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof(psmode_enh->action) +
					sizeof(psmode_enh->params.ps_bitmap));
	} else if (cmd_action == EN_AUTO_PS) {
		psmode_enh->action = cpu_to_le16(EN_AUTO_PS);
		psmode_enh->params.ps_bitmap = cpu_to_le16(ps_bitmap);
		cmd_size = S_DS_GEN + sizeof(psmode_enh->action) +
					sizeof(psmode_enh->params.ps_bitmap);
		tlv = (u8 *)cmd + cmd_size;
		if (ps_bitmap & BITMAP_STA_PS) {
			struct nxpwifi_adapter *adapter = priv->adapter;
			struct nxpwifi_ie_types_ps_param *ps_tlv =
				(struct nxpwifi_ie_types_ps_param *)tlv;
			struct nxpwifi_ps_param *ps_mode = &ps_tlv->param;

			ps_tlv->header.type = cpu_to_le16(TLV_TYPE_PS_PARAM);
			ps_tlv->header.len = cpu_to_le16(sizeof(*ps_tlv) -
					sizeof(struct nxpwifi_ie_types_header));
			cmd_size += sizeof(*ps_tlv);
			tlv += sizeof(*ps_tlv);
			nxpwifi_dbg(priv->adapter, CMD,
				    "cmd: PS Command: Enter PS\n");
			ps_mode->null_pkt_interval =
					cpu_to_le16(adapter->null_pkt_interval);
			ps_mode->multiple_dtims =
					cpu_to_le16(adapter->multiple_dtim);
			ps_mode->bcn_miss_timeout =
					cpu_to_le16(adapter->bcn_miss_time_out);
			ps_mode->local_listen_interval =
				cpu_to_le16(adapter->local_listen_interval);
			ps_mode->delay_to_ps =
					cpu_to_le16(adapter->delay_to_ps);
			ps_mode->mode = cpu_to_le16(adapter->enhanced_ps_mode);
		}
		if (ps_bitmap & BITMAP_AUTO_DS) {
			struct nxpwifi_ie_types_auto_ds_param *auto_ds_tlv =
				(struct nxpwifi_ie_types_auto_ds_param *)tlv;
			u16 idletime = 0;

			auto_ds_tlv->header.type =
				cpu_to_le16(TLV_TYPE_AUTO_DS_PARAM);
			auto_ds_tlv->header.len =
				cpu_to_le16(sizeof(*auto_ds_tlv) -
					sizeof(struct nxpwifi_ie_types_header));
			cmd_size += sizeof(*auto_ds_tlv);
			tlv += sizeof(*auto_ds_tlv);
			if (auto_ds)
				idletime = auto_ds->idle_time;
			nxpwifi_dbg(priv->adapter, CMD,
				    "cmd: PS Command: Enter Auto Deep Sleep\n");
			auto_ds_tlv->deep_sleep_timeout = cpu_to_le16(idletime);
		}
		cmd->size = cpu_to_le16(cmd_size);
	}
	return 0;
}

static int
nxpwifi_ret_sta_enh_power_mode(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *resp,
			       u16 cmdresp_no,
			       void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_ps_mode_enh *ps_mode =
		&resp->params.psmode_enh;
	struct nxpwifi_ds_pm_cfg *pm_cfg =
		(struct nxpwifi_ds_pm_cfg *)data_buf;
	u16 action = le16_to_cpu(ps_mode->action);
	u16 ps_bitmap = le16_to_cpu(ps_mode->params.ps_bitmap);
	u16 auto_ps_bitmap =
		le16_to_cpu(ps_mode->params.ps_bitmap);

	nxpwifi_dbg(adapter, INFO,
		    "info: %s: PS_MODE cmd reply result=%#x action=%#X\n",
		    __func__, resp->result, action);
	if (action == EN_AUTO_PS) {
		if (auto_ps_bitmap & BITMAP_AUTO_DS) {
			nxpwifi_dbg(adapter, CMD,
				    "cmd: Enabled auto deep sleep\n");
			priv->adapter->is_deep_sleep = true;
		}
		if (auto_ps_bitmap & BITMAP_STA_PS) {
			nxpwifi_dbg(adapter, CMD,
				    "cmd: Enabled STA power save\n");
			if (adapter->sleep_period.period)
				nxpwifi_dbg(adapter, CMD,
					    "cmd: set to uapsd/pps mode\n");
		}
	} else if (action == DIS_AUTO_PS) {
		if (ps_bitmap & BITMAP_AUTO_DS) {
			priv->adapter->is_deep_sleep = false;
			nxpwifi_dbg(adapter, CMD,
				    "cmd: Disabled auto deep sleep\n");
		}
		if (ps_bitmap & BITMAP_STA_PS) {
			nxpwifi_dbg(adapter, CMD,
				    "cmd: Disabled STA power save\n");
			if (adapter->sleep_period.period) {
				adapter->delay_null_pkt = false;
				adapter->tx_lock_flag = false;
				adapter->pps_uapsd_mode = false;
			}
		}
	} else if (action == GET_PS) {
		if (ps_bitmap & BITMAP_STA_PS)
			adapter->ps_mode = NXPWIFI_802_11_POWER_MODE_PSP;
		else
			adapter->ps_mode = NXPWIFI_802_11_POWER_MODE_CAM;

		nxpwifi_dbg(adapter, CMD,
			    "cmd: ps_bitmap=%#x\n", ps_bitmap);

		if (pm_cfg) {
			/* This section is for get power save mode */
			if (ps_bitmap & BITMAP_STA_PS)
				pm_cfg->param.ps_mode = 1;
			else
				pm_cfg->param.ps_mode = 0;
		}
	}
	return 0;
}

static int
nxpwifi_cmd_sta_802_11_hs_cfg(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *cmd,
			      u16 cmd_no, void *data_buf,
			      u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_hs_cfg_enh *hs_cfg = &cmd->params.opt_hs_cfg;
	struct nxpwifi_hs_config_param *hscfg_param =
		(struct nxpwifi_hs_config_param *)data_buf;
	u8 *tlv = (u8 *)hs_cfg + sizeof(struct host_cmd_ds_802_11_hs_cfg_enh);
	struct nxpwifi_ps_param_in_hs *psparam_tlv = NULL;
	bool hs_activate = false;
	u16 size;

	if (!hscfg_param)
		/* New Activate command */
		hs_activate = true;
	cmd->command = cpu_to_le16(HOST_CMD_802_11_HS_CFG_ENH);

	if (!hs_activate &&
	    hscfg_param->conditions != cpu_to_le32(HS_CFG_CANCEL) &&
	    (adapter->arp_filter_size > 0 &&
	     adapter->arp_filter_size <= ARP_FILTER_MAX_BUF_SIZE)) {
		nxpwifi_dbg(adapter, CMD,
			    "cmd: Attach %d bytes ArpFilter to HSCfg cmd\n",
			    adapter->arp_filter_size);
		memcpy(((u8 *)hs_cfg) +
		       sizeof(struct host_cmd_ds_802_11_hs_cfg_enh),
		       adapter->arp_filter, adapter->arp_filter_size);
		size = adapter->arp_filter_size +
			sizeof(struct host_cmd_ds_802_11_hs_cfg_enh)
			+ S_DS_GEN;
		tlv = (u8 *)hs_cfg
			+ sizeof(struct host_cmd_ds_802_11_hs_cfg_enh)
			+ adapter->arp_filter_size;
	} else {
		size = S_DS_GEN + sizeof(struct host_cmd_ds_802_11_hs_cfg_enh);
	}
	if (hs_activate) {
		hs_cfg->action = cpu_to_le16(HS_ACTIVATE);
		hs_cfg->params.hs_activate.resp_ctrl = cpu_to_le16(RESP_NEEDED);

		adapter->hs_activated_manually = true;
		nxpwifi_dbg(priv->adapter, CMD,
			    "cmd: Activating host sleep manually\n");
	} else {
		hs_cfg->action = cpu_to_le16(HS_CONFIGURE);
		hs_cfg->params.hs_config.conditions = hscfg_param->conditions;
		hs_cfg->params.hs_config.gpio = hscfg_param->gpio;
		hs_cfg->params.hs_config.gap = hscfg_param->gap;

		size += sizeof(struct nxpwifi_ps_param_in_hs);
		psparam_tlv = (struct nxpwifi_ps_param_in_hs *)tlv;
		psparam_tlv->header.type =
			cpu_to_le16(TLV_TYPE_PS_PARAMS_IN_HS);
		psparam_tlv->header.len =
			cpu_to_le16(sizeof(struct nxpwifi_ps_param_in_hs)
				- sizeof(struct nxpwifi_ie_types_header));
		psparam_tlv->hs_wake_int = cpu_to_le32(HS_DEF_WAKE_INTERVAL);
		psparam_tlv->hs_inact_timeout =
			cpu_to_le32(HS_DEF_INACTIVITY_TIMEOUT);

		nxpwifi_dbg(adapter, CMD,
			    "cmd: HS_CFG_CMD: condition:0x%x gpio:0x%x gap:0x%x\n",
			    hs_cfg->params.hs_config.conditions,
			    hs_cfg->params.hs_config.gpio,
			    hs_cfg->params.hs_config.gap);
	}
	cmd->size = cpu_to_le16(size);

	return 0;
}

static int
nxpwifi_ret_sta_802_11_hs_cfg(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp,
			      u16 cmdresp_no,
			      void *data_buf)
{
	return nxpwifi_ret_802_11_hs_cfg(priv, resp);
}

static int
nxpwifi_cmd_sta_set_bss_mode(struct nxpwifi_private *priv,
			     struct host_cmd_ds_command *cmd,
			     u16 cmd_no, void *data_buf,
			     u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(cmd_no);
	if (priv->bss_mode == NL80211_IFTYPE_STATION)
		cmd->params.bss_mode.con_type = CONNECTION_TYPE_INFRA;
	else if (priv->bss_mode == NL80211_IFTYPE_AP)
		cmd->params.bss_mode.con_type = CONNECTION_TYPE_AP;
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_set_bss_mode) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_cmd_sta_802_11_net_monitor(struct nxpwifi_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_no, void *data_buf,
				   u16 cmd_action, u32 cmd_type)
{
	struct nxpwifi_802_11_net_monitor *net_mon;
	struct host_cmd_ds_802_11_net_monitor *cmd_net_mon =
		&cmd->params.net_mon;
	struct chan_band_param *chan_band = NULL;
	u8 sec_chan_offset = 0;
	u32 bw_offset = 0;

	net_mon = (struct nxpwifi_802_11_net_monitor *)data_buf;

	cmd->size = cpu_to_le16(S_DS_GEN +
				sizeof(struct host_cmd_ds_802_11_net_monitor) +
				sizeof(struct chan_band_param));
	cmd->command = cpu_to_le16(cmd_no);
	cmd_net_mon->action = cpu_to_le16(cmd_action);

	if (cmd_action == HOST_ACT_GEN_SET) {
		if (net_mon->enable_net_mon) {
			cmd_net_mon->enable_net_mon = cpu_to_le16(0x1);
			cmd_net_mon->filter_flag = cpu_to_le16((u16)
			net_mon->filter_flag);
		}

		if (net_mon->enable_net_mon && net_mon->channel) {
			chan_band = &cmd_net_mon->monitor_chan.chan_band_param[0];
			cmd_net_mon->monitor_chan.header.type =
				cpu_to_le16(TLV_TYPE_CHANNELBANDLIST);
			cmd_net_mon->monitor_chan.header.len =
				cpu_to_le16(sizeof(struct chan_band_param));
			chan_band->chan_number = (u8)net_mon->channel;
			chan_band->band_cfg.chan_band =
				nxpwifi_band_to_radio_type((u16)net_mon->band);

			if (net_mon->band & BAND_GN ||
			    net_mon->band & BAND_AN ||
			    net_mon->band & BAND_GAC ||
			    net_mon->band & BAND_AAC) {
				bw_offset = net_mon->chan_bandwidth;
				if (bw_offset == CHANNEL_BW_40MHZ_ABOVE) {
					chan_band->band_cfg.chan_2O_ffset =
						NXPWIFI_SEC_CHAN_ABOVE;
					chan_band->band_cfg.chan_width =
						CHAN_BW_40MHZ;
				} else if (bw_offset == CHANNEL_BW_40MHZ_BELOW) {
					chan_band->band_cfg.chan_2O_ffset =
						NXPWIFI_SEC_CHAN_BELOW;
					chan_band->band_cfg.chan_width =
						CHAN_BW_40MHZ;
				} else if (bw_offset == CHANNEL_BW_80MHZ) {
					sec_chan_offset =
						nxpwifi_get_sec_chan_offset(net_mon->channel);
					if (sec_chan_offset == NXPWIFI_SEC_CHAN_ABOVE)
						chan_band->band_cfg.chan_2O_ffset =
							NXPWIFI_SEC_CHAN_ABOVE;
					else if (sec_chan_offset == NXPWIFI_SEC_CHAN_BELOW)
						chan_band->band_cfg.chan_2O_ffset =
							NXPWIFI_SEC_CHAN_BELOW;
					chan_band->band_cfg.chan_width = CHAN_BW_80MHZ;
				}
			}
		}
	}
	return 0;
}

static int
nxpwifi_ret_sta_802_11_net_monitor(struct nxpwifi_private *priv,
				   struct host_cmd_ds_command *resp,
				   u16 cmdresp_no,
				   void *data_buf)
{
	struct host_cmd_ds_802_11_net_monitor *cmd_net_mon = &resp->params.net_mon;

	nxpwifi_dbg(priv->adapter, CMD,
		    "cmd: NET_MONITOR_CMD: action: %d, enable: %d, flag: %d ch: %d band: %d bw: %d offset: %d\n",
		    le16_to_cpu(cmd_net_mon->action),
		    le16_to_cpu(cmd_net_mon->enable_net_mon),
		    le16_to_cpu(cmd_net_mon->filter_flag),
		    cmd_net_mon->monitor_chan.chan_band_param[0].chan_number,
		    cmd_net_mon->monitor_chan.chan_band_param[0].band_cfg.chan_band,
		    cmd_net_mon->monitor_chan.chan_band_param[0].band_cfg.chan_width,
		    cmd_net_mon->monitor_chan.chan_band_param[0].band_cfg.chan_2O_ffset);
	priv->adapter->enable_net_mon = le16_to_cpu(cmd_net_mon->enable_net_mon);
	return 0;
}

static int
nxpwifi_cmd_sta_802_11_scan_ext(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_no, void *data_buf,
				u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_802_11_scan_ext(priv, cmd, data_buf);
}

static int
nxpwifi_ret_sta_802_11_scan_ext(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *resp,
				u16 cmdresp_no,
				void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;

	ret = nxpwifi_ret_802_11_scan_ext(priv, resp);
	adapter->curr_cmd->wait_q_enabled = false;

	return ret;
}

static int
nxpwifi_cmd_sta_coalesce_cfg(struct nxpwifi_private *priv,
			     struct host_cmd_ds_command *cmd,
			     u16 cmd_no, void *data_buf,
			     u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_coalesce_cfg *coalesce_cfg =
		&cmd->params.coalesce_cfg;
	struct nxpwifi_ds_coalesce_cfg *cfg =
		(struct nxpwifi_ds_coalesce_cfg *)data_buf;
	struct coalesce_filt_field_param *param;
	u16 cnt, idx, length;
	struct coalesce_receive_filt_rule *rule;

	cmd->command = cpu_to_le16(HOST_CMD_COALESCE_CFG);
	cmd->size = cpu_to_le16(S_DS_GEN);

	coalesce_cfg->action = cpu_to_le16(cmd_action);
	coalesce_cfg->num_of_rules = cpu_to_le16(cfg->num_of_rules);
	rule = (void *)coalesce_cfg->rule_data;

	for (cnt = 0; cnt < cfg->num_of_rules; cnt++) {
		rule->header.type = cpu_to_le16(TLV_TYPE_COALESCE_RULE);
		rule->max_coalescing_delay =
			cpu_to_le16(cfg->rule[cnt].max_coalescing_delay);
		rule->pkt_type = cfg->rule[cnt].pkt_type;
		rule->num_of_fields = cfg->rule[cnt].num_of_fields;

		length = 0;

		param = rule->params;
		for (idx = 0; idx < cfg->rule[cnt].num_of_fields; idx++) {
			param->operation = cfg->rule[cnt].params[idx].operation;
			param->operand_len =
					cfg->rule[cnt].params[idx].operand_len;
			param->offset =
				cpu_to_le16(cfg->rule[cnt].params[idx].offset);
			memcpy(param->operand_byte_stream,
			       cfg->rule[cnt].params[idx].operand_byte_stream,
			       param->operand_len);

			length += sizeof(struct coalesce_filt_field_param);

			param++;
		}

		/*
		 * Total rule length is sizeof max_coalescing_delay(u16),
		 * num_of_fields(u8), pkt_type(u8) and total length of the all
		 * params
		 */
		rule->header.len = cpu_to_le16(length + sizeof(u16) +
					       sizeof(u8) + sizeof(u8));

		/* Add the rule length to the command size */
		le16_unaligned_add_cpu(&cmd->size,
				       le16_to_cpu(rule->header.len) +
				       sizeof(struct nxpwifi_ie_types_header));

		rule = (void *)((u8 *)rule->params + length);
	}

	/* Add sizeof action, num_of_rules to total command length */
	le16_unaligned_add_cpu(&cmd->size, sizeof(u16) + sizeof(u16));

	return 0;
}

static int
nxpwifi_cmd_sta_mgmt_frame_reg(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *cmd,
			       u16 cmd_no, void *data_buf,
			       u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(cmd_no);
	cmd->params.reg_mask.action = cpu_to_le16(cmd_action);
	cmd->params.reg_mask.mask =
		cpu_to_le32(get_unaligned((u32 *)data_buf));
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_mgmt_frame_reg) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_cmd_sta_remain_on_chan(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *cmd,
			       u16 cmd_no, void *data_buf,
			       u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(cmd_no);
	memcpy(&cmd->params, data_buf,
	       sizeof(struct host_cmd_ds_remain_on_chan));
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_remain_on_chan) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_ret_sta_remain_on_chan(struct nxpwifi_private *priv,
			       struct host_cmd_ds_command *resp,
			       u16 cmdresp_no,
			       void *data_buf)
{
	struct host_cmd_ds_remain_on_chan *resp_cfg = &resp->params.roc_cfg;
	struct host_cmd_ds_remain_on_chan *roc_cfg =
		(struct host_cmd_ds_remain_on_chan *)data_buf;

	if (roc_cfg)
		memcpy(roc_cfg, resp_cfg, sizeof(*roc_cfg));

	return 0;
}

static int
nxpwifi_cmd_sta_gtk_rekey_offload(struct nxpwifi_private *priv,
				  struct host_cmd_ds_command *cmd,
				  u16 cmd_no, void *data_buf,
				  u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_gtk_rekey_params *rekey = &cmd->params.rekey;
	struct cfg80211_gtk_rekey_data *data =
		(struct cfg80211_gtk_rekey_data *)data_buf;
	u64 rekey_ctr;

	cmd->command = cpu_to_le16(HOST_CMD_GTK_REKEY_OFFLOAD_CFG);
	cmd->size = cpu_to_le16(sizeof(*rekey) + S_DS_GEN);

	rekey->action = cpu_to_le16(cmd_action);
	if (cmd_action == HOST_ACT_GEN_SET) {
		memcpy(rekey->kek, data->kek, NL80211_KEK_LEN);
		memcpy(rekey->kck, data->kck, NL80211_KCK_LEN);
		rekey_ctr = be64_to_cpup((__be64 *)data->replay_ctr);
		rekey->replay_ctr_low = cpu_to_le32((u32)rekey_ctr);
		rekey->replay_ctr_high =
			cpu_to_le32((u32)((u64)rekey_ctr >> 32));
	}

	return 0;
}

static int
nxpwifi_cmd_sta_11ac_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd,
			 u16 cmd_no, void *data_buf,
			 u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11ac_cfg(priv, cmd, cmd_action, data_buf);
}

static int
nxpwifi_cmd_sta_hs_wakeup_reason(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 u16 cmd_no, void *data_buf,
				 u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(HOST_CMD_HS_WAKEUP_REASON);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_wakeup_reason) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_ret_sta_hs_wakeup_reason(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *resp,
				 u16 cmdresp_no,
				 void *data_buf)
{
	struct host_cmd_ds_wakeup_reason *wakeup_reason =
		(struct host_cmd_ds_wakeup_reason *)data_buf;
	wakeup_reason->wakeup_reason =
		resp->params.hs_wakeup_reason.wakeup_reason;

	return 0;
}

static int
nxpwifi_cmd_sta_mc_policy(struct nxpwifi_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_no, void *data_buf,
			  u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_multi_chan_policy *mc_pol = &cmd->params.mc_policy;
	const u16 *drcs_info = data_buf;

	mc_pol->action = cpu_to_le16(cmd_action);
	mc_pol->policy = cpu_to_le16(*drcs_info);
	cmd->command = cpu_to_le16(HOST_CMD_MC_POLICY);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_multi_chan_policy) +
				S_DS_GEN);
	return 0;
}

static int
nxpwifi_cmd_sta_sdio_rx_aggr_cfg(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *cmd,
				 u16 cmd_no, void *data_buf,
				 u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_sdio_sp_rx_aggr_cfg *cfg =
					&cmd->params.sdio_rx_aggr_cfg;

	cmd->command = cpu_to_le16(HOST_CMD_SDIO_SP_RX_AGGR_CFG);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_sdio_sp_rx_aggr_cfg) +
			    S_DS_GEN);
	cfg->action = cmd_action;
	if (cmd_action == HOST_ACT_GEN_SET)
		cfg->enable = *(u8 *)data_buf;

	return 0;
}

static int
nxpwifi_ret_sta_sdio_rx_aggr_cfg(struct nxpwifi_private *priv,
				 struct host_cmd_ds_command *resp,
				 u16 cmdresp_no,
				 void *data_buf)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_sdio_sp_rx_aggr_cfg *cfg =
				&resp->params.sdio_rx_aggr_cfg;

	adapter->sdio_rx_aggr_enable = cfg->enable;
	adapter->sdio_rx_block_size = le16_to_cpu(cfg->block_size);

	return 0;
}

static int
nxpwifi_cmd_sta_get_chan_info(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *cmd,
			      u16 cmd_no, void *data_buf,
			      u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_sta_configure *sta_cfg_cmd = &cmd->params.sta_cfg;
	struct host_cmd_tlv_channel_band *tlv_band_channel =
	(struct host_cmd_tlv_channel_band *)sta_cfg_cmd->tlv_buffer;

	cmd->command = cpu_to_le16(HOST_CMD_STA_CONFIGURE);
	cmd->size = cpu_to_le16(sizeof(*sta_cfg_cmd) +
				sizeof(*tlv_band_channel) + S_DS_GEN);
	sta_cfg_cmd->action = cpu_to_le16(cmd_action);
	memset(tlv_band_channel, 0, sizeof(*tlv_band_channel));
	tlv_band_channel->header.type = cpu_to_le16(TLV_TYPE_CHANNELBANDLIST);
	tlv_band_channel->header.len  = cpu_to_le16(sizeof(*tlv_band_channel) -
					sizeof(struct nxpwifi_ie_types_header));

	return 0;
}

static int
nxpwifi_ret_sta_get_chan_info(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp,
			      u16 cmdresp_no,
			      void *data_buf)
{
	struct host_cmd_ds_sta_configure *sta_cfg_cmd = &resp->params.sta_cfg;
	struct nxpwifi_channel_band *channel_band =
		(struct nxpwifi_channel_band *)data_buf;
	struct host_cmd_tlv_channel_band *tlv_band_channel;

	tlv_band_channel =
	(struct host_cmd_tlv_channel_band *)sta_cfg_cmd->tlv_buffer;
	memcpy(&channel_band->band_config, &tlv_band_channel->band_config,
	       sizeof(struct nxpwifi_band_config));
	channel_band->channel = tlv_band_channel->channel;

	return 0;
}

static int
nxpwifi_cmd_sta_chan_region_cfg(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_no, void *data_buf,
				u16 cmd_action, u32 cmd_type)
{
	struct host_cmd_ds_chan_region_cfg *reg = &cmd->params.reg_cfg;

	cmd->command = cpu_to_le16(HOST_CMD_CHAN_REGION_CFG);
	cmd->size = cpu_to_le16(sizeof(*reg) + S_DS_GEN);

	if (cmd_action == HOST_ACT_GEN_GET)
		reg->action = cpu_to_le16(cmd_action);

	return 0;
}

static struct ieee80211_regdomain *
nxpwifi_create_custom_regdomain(struct nxpwifi_private *priv,
				u8 *buf, u16 buf_len)
{
	u16 num_chan = buf_len / 2;
	struct ieee80211_regdomain *regd;
	struct ieee80211_reg_rule *rule;
	bool new_rule;
	int idx, freq, prev_freq = 0;
	u32 bw, prev_bw = 0;
	u8 chflags, prev_chflags = 0, valid_rules = 0;

	if (WARN_ON_ONCE(num_chan > NL80211_MAX_SUPP_REG_RULES))
		return ERR_PTR(-EINVAL);

	regd = kzalloc_flex(*regd, reg_rules, num_chan, GFP_KERNEL);
	if (!regd)
		return ERR_PTR(-ENOMEM);

	for (idx = 0; idx < num_chan; idx++) {
		u8 chan;
		enum nl80211_band band;

		chan = *buf++;
		if (!chan) {
			kfree(regd);
			return NULL;
		}
		chflags = *buf++;
		band = (chan <= 14) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
		freq = ieee80211_channel_to_frequency(chan, band);
		new_rule = false;

		if (chflags & NXPWIFI_CHANNEL_DISABLED)
			continue;

		if (band == NL80211_BAND_5GHZ) {
			if (!(chflags & NXPWIFI_CHANNEL_NOHT80))
				bw = MHZ_TO_KHZ(80);
			else if (!(chflags & NXPWIFI_CHANNEL_NOHT40))
				bw = MHZ_TO_KHZ(40);
			else
				bw = MHZ_TO_KHZ(20);
		} else {
			if (!(chflags & NXPWIFI_CHANNEL_NOHT40))
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

		if (chflags & NXPWIFI_CHANNEL_PASSIVE)
			rule->flags = NL80211_RRF_NO_IR;

		if (chflags & NXPWIFI_CHANNEL_DFS)
			rule->flags = NL80211_RRF_DFS;

		rule->freq_range.max_bandwidth_khz = bw;
	}

	regd->n_reg_rules = valid_rules;
	regd->alpha2[0] = '9';
	regd->alpha2[1] = '9';

	return regd;
}

static int
nxpwifi_ret_sta_chan_region_cfg(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *resp,
				u16 cmdresp_no,
				void *data_buf)
{
	struct host_cmd_ds_chan_region_cfg *reg = &resp->params.reg_cfg;
	u16 action = le16_to_cpu(reg->action);
	u16 tlv, tlv_buf_len, tlv_buf_left;
	struct nxpwifi_ie_types_header *head;
	struct ieee80211_regdomain *regd;
	u8 *tlv_buf;

	if (action != HOST_ACT_GEN_GET)
		return 0;

	tlv_buf = (u8 *)reg + sizeof(*reg);
	tlv_buf_left = le16_to_cpu(resp->size) - S_DS_GEN - sizeof(*reg);

	while (tlv_buf_left >= sizeof(*head)) {
		head = (struct nxpwifi_ie_types_header *)tlv_buf;
		tlv = le16_to_cpu(head->type);
		tlv_buf_len = le16_to_cpu(head->len);

		if (tlv_buf_left < (sizeof(*head) + tlv_buf_len))
			break;

		switch (tlv) {
		case TLV_TYPE_CHAN_ATTR_CFG:
			nxpwifi_dbg_dump(priv->adapter, CMD_D, "CHAN:",
					 (u8 *)head + sizeof(*head),
					 tlv_buf_len);
			regd = nxpwifi_create_custom_regdomain(priv, (u8 *)head
							       + sizeof(*head),
							       tlv_buf_len);
			if (!IS_ERR(regd))
				priv->adapter->regd = regd;
			break;
		}

		tlv_buf += (sizeof(*head) + tlv_buf_len);
		tlv_buf_left -= (sizeof(*head) + tlv_buf_len);
	}

	return 0;
}

static int
nxpwifi_cmd_sta_pkt_aggr_ctrl(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *cmd,
			      u16 cmd_no, void *data_buf,
			      u16 cmd_action, u32 cmd_type)
{
	cmd->command = cpu_to_le16(cmd_no);
	cmd->params.pkt_aggr_ctrl.action = cpu_to_le16(cmd_action);
	cmd->params.pkt_aggr_ctrl.enable = cpu_to_le16(*(u16 *)data_buf);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_pkt_aggr_ctrl) +
				S_DS_GEN);

	return 0;
}

static int
nxpwifi_ret_sta_pkt_aggr_ctrl(struct nxpwifi_private *priv,
			      struct host_cmd_ds_command *resp,
			      u16 cmdresp_no,
			      void *data_buf)
{
	struct host_cmd_ds_pkt_aggr_ctrl *pkt_aggr_ctrl =
					&resp->params.pkt_aggr_ctrl;
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->bus_aggr.enable = le16_to_cpu(pkt_aggr_ctrl->enable);
	if (adapter->bus_aggr.enable)
		adapter->intf_hdr_len = INTF_HEADER_LEN;
	adapter->bus_aggr.mode = NXPWIFI_BUS_AGGR_MODE_LEN_V2;
	adapter->bus_aggr.tx_aggr_max_size =
		le16_to_cpu(pkt_aggr_ctrl->tx_aggr_max_size);
	adapter->bus_aggr.tx_aggr_max_num =
		le16_to_cpu(pkt_aggr_ctrl->tx_aggr_max_num);
	adapter->bus_aggr.tx_aggr_align =
		le16_to_cpu(pkt_aggr_ctrl->tx_aggr_align);

	return 0;
}

static int
nxpwifi_cmd_sta_11ax_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd,
			 u16 cmd_no, void *data_buf,
			 u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11ax_cfg(priv, cmd, cmd_action, data_buf);
}

static int
nxpwifi_ret_sta_11ax_cfg(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 u16 cmdresp_no,
			 void *data_buf)
{
	return nxpwifi_ret_11ax_cfg(priv, resp, data_buf);
}

static int
nxpwifi_cmd_sta_11ax_cmd(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *cmd,
			 u16 cmd_no, void *data_buf,
			 u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_11ax_cmd(priv, cmd, cmd_action, data_buf);
}

static int
nxpwifi_ret_sta_11ax_cmd(struct nxpwifi_private *priv,
			 struct host_cmd_ds_command *resp,
			 u16 cmdresp_no,
			 void *data_buf)
{
	return nxpwifi_ret_11ax_cmd(priv, resp, data_buf);
}

static int
nxpwifi_cmd_sta_twt_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *cmd,
			u16 cmd_no, void *data_buf,
			u16 cmd_action, u32 cmd_type)
{
	return nxpwifi_cmd_twt_cfg(priv, cmd, cmd_action, data_buf);
}

static int
nxpwifi_ret_sta_twt_cfg(struct nxpwifi_private *priv,
			struct host_cmd_ds_command *resp,
			u16 cmdresp_no,
			void *data_buf)
{
	return nxpwifi_ret_twt_cfg(priv, resp, data_buf);
}

static const struct nxpwifi_cmd_entry cmd_table_sta[] = {
	{.cmd_no = HOST_CMD_GET_HW_SPEC,
	.prepare_cmd = nxpwifi_cmd_sta_get_hw_spec,
	.cmd_resp = nxpwifi_ret_sta_get_hw_spec},
	{.cmd_no = HOST_CMD_802_11_SCAN,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_scan,
	.cmd_resp = nxpwifi_ret_sta_802_11_scan},
	{.cmd_no = HOST_CMD_802_11_GET_LOG,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_get_log,
	.cmd_resp = nxpwifi_ret_sta_802_11_get_log},
	{.cmd_no = HOST_CMD_MAC_MULTICAST_ADR,
	.prepare_cmd = nxpwifi_cmd_sta_mac_multicast_adr,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_802_11_ASSOCIATE,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_associate,
	.cmd_resp = nxpwifi_ret_sta_802_11_associate},
	{.cmd_no = HOST_CMD_802_11_SNMP_MIB,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_snmp_mib,
	.cmd_resp = nxpwifi_ret_sta_802_11_snmp_mib},
	{.cmd_no = HOST_CMD_MAC_REG_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_reg_access,
	.cmd_resp = nxpwifi_ret_sta_reg_access},
	{.cmd_no = HOST_CMD_BBP_REG_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_reg_access,
	.cmd_resp = nxpwifi_ret_sta_reg_access},
	{.cmd_no = HOST_CMD_RF_REG_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_reg_access,
	.cmd_resp = nxpwifi_ret_sta_reg_access},
	{.cmd_no = HOST_CMD_RF_TX_PWR,
	.prepare_cmd = nxpwifi_cmd_sta_rf_tx_pwr,
	.cmd_resp = nxpwifi_ret_sta_rf_tx_pwr},
	{.cmd_no = HOST_CMD_RF_ANTENNA,
	.prepare_cmd = nxpwifi_cmd_sta_rf_antenna,
	.cmd_resp = nxpwifi_ret_sta_rf_antenna},
	{.cmd_no = HOST_CMD_802_11_DEAUTHENTICATE,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_deauthenticate,
	.cmd_resp = nxpwifi_ret_sta_802_11_deauthenticate},
	{.cmd_no = HOST_CMD_MAC_CONTROL,
	.prepare_cmd = nxpwifi_cmd_sta_mac_control,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_802_11_MAC_ADDRESS,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_mac_address,
	.cmd_resp = nxpwifi_ret_sta_802_11_mac_address},
	{.cmd_no = HOST_CMD_802_11_EEPROM_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_reg_access,
	.cmd_resp = nxpwifi_ret_sta_reg_access},
	{.cmd_no = HOST_CMD_802_11D_DOMAIN_INFO,
	.prepare_cmd = nxpwifi_cmd_sta_802_11d_domain_info,
	.cmd_resp = nxpwifi_ret_sta_802_11d_domain_info},
	{.cmd_no = HOST_CMD_802_11_KEY_MATERIAL,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_key_material,
	.cmd_resp = nxpwifi_ret_sta_802_11_key_material},
	{.cmd_no = HOST_CMD_802_11_BG_SCAN_CONFIG,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_bg_scan_config,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_802_11_BG_SCAN_QUERY,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_bg_scan_query,
	.cmd_resp = nxpwifi_ret_sta_802_11_bg_scan_query},
	{.cmd_no = HOST_CMD_WMM_GET_STATUS,
	.prepare_cmd = nxpwifi_cmd_sta_wmm_get_status,
	.cmd_resp = nxpwifi_ret_sta_wmm_get_status},
	{.cmd_no = HOST_CMD_802_11_SUBSCRIBE_EVENT,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_subsc_evt,
	.cmd_resp = nxpwifi_ret_sta_subsc_evt},
	{.cmd_no = HOST_CMD_802_11_TX_RATE_QUERY,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_tx_rate_query,
	.cmd_resp = nxpwifi_ret_sta_802_11_tx_rate_query},
	{.cmd_no = HOST_CMD_MEM_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_mem_access,
	.cmd_resp = nxpwifi_ret_sta_mem_access},
	{.cmd_no = HOST_CMD_CFG_DATA,
	.prepare_cmd = nxpwifi_cmd_sta_cfg_data,
	.cmd_resp = nxpwifi_ret_sta_cfg_data},
	{.cmd_no = HOST_CMD_VERSION_EXT,
	.prepare_cmd = nxpwifi_cmd_sta_ver_ext,
	.cmd_resp = nxpwifi_ret_sta_ver_ext},
	{.cmd_no = HOST_CMD_MEF_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_mef_cfg,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_RSSI_INFO,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_rssi_info,
	.cmd_resp = nxpwifi_ret_sta_802_11_rssi_info},
	{.cmd_no = HOST_CMD_FUNC_INIT,
	.prepare_cmd = nxpwifi_cmd_sta_func_init,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_FUNC_SHUTDOWN,
	.prepare_cmd = nxpwifi_cmd_sta_func_shutdown,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_PMIC_REG_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_reg_access,
	.cmd_resp = nxpwifi_ret_sta_reg_access},
	{.cmd_no = HOST_CMD_11N_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_11n_cfg,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_11N_ADDBA_REQ,
	.prepare_cmd = nxpwifi_cmd_sta_11n_addba_req,
	.cmd_resp = nxpwifi_ret_sta_11n_addba_req},
	{.cmd_no = HOST_CMD_11N_ADDBA_RSP,
	.prepare_cmd = nxpwifi_cmd_sta_11n_addba_rsp,
	.cmd_resp = nxpwifi_ret_sta_11n_addba_rsp},
	{.cmd_no = HOST_CMD_11N_DELBA,
	.prepare_cmd = nxpwifi_cmd_sta_11n_delba,
	.cmd_resp = nxpwifi_ret_sta_11n_delba},
	{.cmd_no = HOST_CMD_TXPWR_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_tx_power_cfg,
	.cmd_resp = nxpwifi_ret_sta_tx_power_cfg},
	{.cmd_no = HOST_CMD_TX_RATE_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_tx_rate_cfg,
	.cmd_resp = nxpwifi_ret_sta_tx_rate_cfg},
	{.cmd_no = HOST_CMD_RECONFIGURE_TX_BUFF,
	.prepare_cmd = nxpwifi_cmd_sta_reconfigure_rx_buff,
	.cmd_resp = nxpwifi_ret_sta_reconfigure_rx_buff},
	{.cmd_no = HOST_CMD_CHAN_REPORT_REQUEST,
	.prepare_cmd = nxpwifi_cmd_sta_chan_report_request,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_AMSDU_AGGR_CTRL,
	.prepare_cmd = nxpwifi_cmd_sta_amsdu_aggr_ctrl,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_ROBUST_COEX,
	.prepare_cmd = nxpwifi_cmd_sta_robust_coex,
	.cmd_resp = nxpwifi_ret_sta_robust_coex},
	{.cmd_no = HOST_CMD_802_11_PS_MODE_ENH,
	.prepare_cmd = nxpwifi_cmd_sta_enh_power_mode,
	.cmd_resp = nxpwifi_ret_sta_enh_power_mode},
	{.cmd_no = HOST_CMD_802_11_HS_CFG_ENH,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_hs_cfg,
	.cmd_resp = nxpwifi_ret_sta_802_11_hs_cfg},
	{.cmd_no = HOST_CMD_CAU_REG_ACCESS,
	.prepare_cmd = nxpwifi_cmd_sta_reg_access,
	.cmd_resp = nxpwifi_ret_sta_reg_access},
	{.cmd_no = HOST_CMD_SET_BSS_MODE,
	.prepare_cmd = nxpwifi_cmd_sta_set_bss_mode,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_802_11_NET_MONITOR,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_net_monitor,
	.cmd_resp = nxpwifi_ret_sta_802_11_net_monitor},
	{.cmd_no = HOST_CMD_802_11_SCAN_EXT,
	.prepare_cmd = nxpwifi_cmd_sta_802_11_scan_ext,
	.cmd_resp = nxpwifi_ret_sta_802_11_scan_ext},
	{.cmd_no = HOST_CMD_COALESCE_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_coalesce_cfg,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_MGMT_FRAME_REG,
	.prepare_cmd = nxpwifi_cmd_sta_mgmt_frame_reg,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_REMAIN_ON_CHAN,
	.prepare_cmd = nxpwifi_cmd_sta_remain_on_chan,
	.cmd_resp = nxpwifi_ret_sta_remain_on_chan},
	{.cmd_no = HOST_CMD_GTK_REKEY_OFFLOAD_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_gtk_rekey_offload,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_11AC_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_11ac_cfg,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_HS_WAKEUP_REASON,
	.prepare_cmd = nxpwifi_cmd_sta_hs_wakeup_reason,
	.cmd_resp = nxpwifi_ret_sta_hs_wakeup_reason},
	{.cmd_no = HOST_CMD_MC_POLICY,
	.prepare_cmd = nxpwifi_cmd_sta_mc_policy,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_FW_DUMP_EVENT,
	.prepare_cmd = nxpwifi_cmd_fill_head_only,
	.cmd_resp = NULL},
	{.cmd_no = HOST_CMD_SDIO_SP_RX_AGGR_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_sdio_rx_aggr_cfg,
	.cmd_resp = nxpwifi_ret_sta_sdio_rx_aggr_cfg},
	{.cmd_no = HOST_CMD_STA_CONFIGURE,
	.prepare_cmd = nxpwifi_cmd_sta_get_chan_info,
	.cmd_resp = nxpwifi_ret_sta_get_chan_info},
	{.cmd_no = HOST_CMD_CHAN_REGION_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_chan_region_cfg,
	.cmd_resp = nxpwifi_ret_sta_chan_region_cfg},
	{.cmd_no = HOST_CMD_PACKET_AGGR_CTRL,
	.prepare_cmd = nxpwifi_cmd_sta_pkt_aggr_ctrl,
	.cmd_resp = nxpwifi_ret_sta_pkt_aggr_ctrl},
	{.cmd_no = HOST_CMD_11AX_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_11ax_cfg,
	.cmd_resp = nxpwifi_ret_sta_11ax_cfg},
	{.cmd_no = HOST_CMD_11AX_CMD,
	.prepare_cmd = nxpwifi_cmd_sta_11ax_cmd,
	.cmd_resp = nxpwifi_ret_sta_11ax_cmd},
	{.cmd_no = HOST_CMD_TWT_CFG,
	.prepare_cmd = nxpwifi_cmd_sta_twt_cfg,
	.cmd_resp = nxpwifi_ret_sta_twt_cfg},
};

/*
 * Prepare a command before sending it to firmware by invoking the
 * appropriate handler based on the command ID.
 */
int nxpwifi_sta_prepare_cmd(struct nxpwifi_private *priv,
			    struct cmd_ctrl_node *cmd_node,
			    u16 cmd_action, u32 cmd_oid)

{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u16 cmd_no = cmd_node->cmd_no;
	struct host_cmd_ds_command *cmd =
		(struct host_cmd_ds_command *)cmd_node->skb->data;
	void *data_buf = cmd_node->data_buf;
	int i, ret = -EINVAL;

	for (i = 0; i < ARRAY_SIZE(cmd_table_sta); i++) {
		if (cmd_no == cmd_table_sta[i].cmd_no) {
			if (cmd_table_sta[i].prepare_cmd)
				ret = cmd_table_sta[i].prepare_cmd(priv, cmd,
								   cmd_no,
								   data_buf,
								   cmd_action,
								   cmd_oid);
			cmd_node->cmd_resp = cmd_table_sta[i].cmd_resp;
			break;
		}
	}

	if (i == ARRAY_SIZE(cmd_table_sta))
		nxpwifi_dbg(adapter, ERROR,
			    "%s: unknown command: %#x\n",
			    __func__, cmd_no);
	else
		nxpwifi_dbg(adapter, CMD,
			    "%s: command: %#x\n",
			    __func__, cmd_no);

	return ret;
}

/*
 * Initialize firmware after download or during virtual interface
 * reinitialization to bring the device to a working state.
 */
int nxpwifi_sta_init_cmd(struct nxpwifi_private *priv, u8 first_sta, bool init)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;
	struct nxpwifi_ds_11n_amsdu_aggr_ctrl amsdu_aggr_ctrl;
	struct nxpwifi_ds_auto_ds auto_ds;
	enum state_11d_t state_11d;
	struct nxpwifi_ds_11n_tx_cfg tx_cfg;
	u8 sdio_sp_rx_aggr_enable;

	if (first_sta) {
		ret = nxpwifi_send_cmd(priv, HOST_CMD_FUNC_INIT,
				       HOST_ACT_GEN_SET, 0, NULL, true);
		if (ret)
			return ret;

		if (adapter->cal_data)
			nxpwifi_send_cmd(priv, HOST_CMD_CFG_DATA,
					 HOST_ACT_GEN_SET, 0, NULL, true);

		/* Read MAC address from HW */
		ret = nxpwifi_send_cmd(priv, HOST_CMD_GET_HW_SPEC,
				       HOST_ACT_GEN_GET, 0, NULL, true);
		if (ret)
			return ret;

		/* * Set SDIO Single Port RX Aggr Info */
		if (priv->adapter->iface_type == NXPWIFI_SDIO &&
		    ISSUPP_SDIO_SPA_ENABLED(priv->adapter->fw_cap_info) &&
		    !priv->adapter->host_disable_sdio_rx_aggr) {
			sdio_sp_rx_aggr_enable = true;
			ret = nxpwifi_send_cmd(priv,
					       HOST_CMD_SDIO_SP_RX_AGGR_CFG,
					       HOST_ACT_GEN_SET, 0,
					       &sdio_sp_rx_aggr_enable,
					       true);
			if (ret) {
				nxpwifi_dbg(priv->adapter, ERROR,
					    "error while enabling SP aggregation..disable it");
				adapter->sdio_rx_aggr_enable = false;
			}
		}

		/* Reconfigure tx buf size */
		ret = nxpwifi_send_cmd(priv, HOST_CMD_RECONFIGURE_TX_BUFF,
				       HOST_ACT_GEN_SET, 0,
				       &priv->adapter->tx_buf_size, true);
		if (ret)
			return ret;

		if (priv->bss_type != NXPWIFI_BSS_TYPE_UAP) {
			/* Enable IEEE PS by default */
			priv->adapter->ps_mode = NXPWIFI_802_11_POWER_MODE_PSP;
			ret = nxpwifi_send_cmd(priv,
					       HOST_CMD_802_11_PS_MODE_ENH,
					       EN_AUTO_PS, BITMAP_STA_PS, NULL,
					       true);
			if (ret)
				return ret;
		}

		nxpwifi_send_cmd(priv, HOST_CMD_CHAN_REGION_CFG,
				 HOST_ACT_GEN_GET, 0, NULL, true);
	}

	/* get tx rate */
	ret = nxpwifi_send_cmd(priv, HOST_CMD_TX_RATE_CFG,
			       HOST_ACT_GEN_GET, 0, NULL, true);
	if (ret)
		return ret;
	priv->data_rate = 0;

	/* get tx power */
	ret = nxpwifi_send_cmd(priv, HOST_CMD_RF_TX_PWR,
			       HOST_ACT_GEN_GET, 0, NULL, true);
	if (ret)
		return ret;

	memset(&amsdu_aggr_ctrl, 0, sizeof(amsdu_aggr_ctrl));
	amsdu_aggr_ctrl.enable = true;
	/* Send request to firmware */
	ret = nxpwifi_send_cmd(priv, HOST_CMD_AMSDU_AGGR_CTRL,
			       HOST_ACT_GEN_SET, 0,
			       &amsdu_aggr_ctrl, true);
	if (ret)
		return ret;
	/* MAC Control must be the last command in init_fw */
	/* set MAC Control */
	ret = nxpwifi_send_cmd(priv, HOST_CMD_MAC_CONTROL,
			       HOST_ACT_GEN_SET, 0,
			       &priv->curr_pkt_filter, true);
	if (ret)
		return ret;

	if (!disable_auto_ds && first_sta &&
	    priv->bss_type != NXPWIFI_BSS_TYPE_UAP) {
		/* Enable auto deep sleep */
		auto_ds.auto_ds = DEEP_SLEEP_ON;
		auto_ds.idle_time = DEEP_SLEEP_IDLE_TIME;
		ret = nxpwifi_send_cmd(priv, HOST_CMD_802_11_PS_MODE_ENH,
				       EN_AUTO_PS, BITMAP_AUTO_DS,
				       &auto_ds, true);
		if (ret)
			return ret;
	}

	if (priv->bss_type != NXPWIFI_BSS_TYPE_UAP) {
		/* Send cmd to FW to enable/disable 11D function */
		state_11d = ENABLE_11D;
		ret = nxpwifi_send_cmd(priv, HOST_CMD_802_11_SNMP_MIB,
				       HOST_ACT_GEN_SET, DOT11D_I,
				       &state_11d, true);
		if (ret)
			nxpwifi_dbg(priv->adapter, ERROR,
				    "11D: failed to enable 11D\n");
	}

	/*
	 * Send cmd to FW to configure 11n specific configuration
	 * (Short GI, Channel BW, Green field support etc.) for transmit
	 */
	tx_cfg.tx_htcap = NXPWIFI_FW_DEF_HTTXCFG;
	ret = nxpwifi_send_cmd(priv, HOST_CMD_11N_CFG,
			       HOST_ACT_GEN_SET, 0, &tx_cfg, true);

	return ret;
}
