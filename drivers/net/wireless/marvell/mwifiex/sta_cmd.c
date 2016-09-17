/*
 * Marvell Wireless LAN device driver: station command handling
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

static bool drcs;
module_param(drcs, bool, 0644);
MODULE_PARM_DESC(drcs, "multi-channel operation:1, single-channel operation:0");

static bool disable_auto_ds;
module_param(disable_auto_ds, bool, 0);
MODULE_PARM_DESC(disable_auto_ds,
		 "deepsleep enabled=0(default), deepsleep disabled=1");
/*
 * This function prepares command to set/get RSSI information.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting data/beacon average factors
 *      - Resetting SNR/NF/RSSI values in private structure
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_802_11_rssi_info(struct mwifiex_private *priv,
			     struct host_cmd_ds_command *cmd, u16 cmd_action)
{
	cmd->command = cpu_to_le16(HostCmd_CMD_RSSI_INFO);
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

/*
 * This function prepares command to set MAC control.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_mac_control(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_action, u16 *action)
{
	struct host_cmd_ds_mac_control *mac_ctrl = &cmd->params.mac_ctrl;

	if (cmd_action != HostCmd_ACT_GEN_SET) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "mac_control: only support set cmd\n");
		return -1;
	}

	cmd->command = cpu_to_le16(HostCmd_CMD_MAC_CONTROL);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_ds_mac_control) + S_DS_GEN);
	mac_ctrl->action = cpu_to_le16(*action);

	return 0;
}

/*
 * This function prepares command to set/get SNMP MIB.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting SNMP MIB OID number and value
 *        (as required)
 *      - Ensuring correct endian-ness
 *
 * The following SNMP MIB OIDs are supported -
 *      - FRAG_THRESH_I     : Fragmentation threshold
 *      - RTS_THRESH_I      : RTS threshold
 *      - SHORT_RETRY_LIM_I : Short retry limit
 *      - DOT11D_I          : 11d support
 */
static int mwifiex_cmd_802_11_snmp_mib(struct mwifiex_private *priv,
				       struct host_cmd_ds_command *cmd,
				       u16 cmd_action, u32 cmd_oid,
				       u16 *ul_temp)
{
	struct host_cmd_ds_802_11_snmp_mib *snmp_mib = &cmd->params.smib;

	mwifiex_dbg(priv->adapter, CMD,
		    "cmd: SNMP_CMD: cmd_oid = 0x%x\n", cmd_oid);
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_SNMP_MIB);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_snmp_mib)
				- 1 + S_DS_GEN);

	snmp_mib->oid = cpu_to_le16((u16)cmd_oid);
	if (cmd_action == HostCmd_ACT_GEN_GET) {
		snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_GET);
		snmp_mib->buf_size = cpu_to_le16(MAX_SNMP_BUF_SIZE);
		le16_add_cpu(&cmd->size, MAX_SNMP_BUF_SIZE);
	} else if (cmd_action == HostCmd_ACT_GEN_SET) {
		snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_SET);
		snmp_mib->buf_size = cpu_to_le16(sizeof(u16));
		*((__le16 *) (snmp_mib->value)) = cpu_to_le16(*ul_temp);
		le16_add_cpu(&cmd->size, sizeof(u16));
	}

	mwifiex_dbg(priv->adapter, CMD,
		    "cmd: SNMP_CMD: Action=0x%x, OID=0x%x,\t"
		    "OIDSize=0x%x, Value=0x%x\n",
		    cmd_action, cmd_oid, le16_to_cpu(snmp_mib->buf_size),
		    le16_to_cpu(*(__le16 *)snmp_mib->value));
	return 0;
}

/*
 * This function prepares command to get log.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_802_11_get_log(struct host_cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_GET_LOG);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_get_log) +
				S_DS_GEN);
	return 0;
}

/*
 * This function prepares command to set/get Tx data rate configuration.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting configuration index, rate scope and rate drop pattern
 *        parameters (as required)
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_tx_rate_cfg(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_action, u16 *pbitmap_rates)
{
	struct host_cmd_ds_tx_rate_cfg *rate_cfg = &cmd->params.tx_rate_cfg;
	struct mwifiex_rate_scope *rate_scope;
	struct mwifiex_rate_drop_pattern *rate_drop;
	u32 i;

	cmd->command = cpu_to_le16(HostCmd_CMD_TX_RATE_CFG);

	rate_cfg->action = cpu_to_le16(cmd_action);
	rate_cfg->cfg_index = 0;

	rate_scope = (struct mwifiex_rate_scope *) ((u8 *) rate_cfg +
		      sizeof(struct host_cmd_ds_tx_rate_cfg));
	rate_scope->type = cpu_to_le16(TLV_TYPE_RATE_SCOPE);
	rate_scope->length = cpu_to_le16
		(sizeof(*rate_scope) - sizeof(struct mwifiex_ie_types_header));
	if (pbitmap_rates != NULL) {
		rate_scope->hr_dsss_rate_bitmap = cpu_to_le16(pbitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap = cpu_to_le16(pbitmap_rates[1]);
		for (i = 0;
		     i < sizeof(rate_scope->ht_mcs_rate_bitmap) / sizeof(u16);
		     i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				cpu_to_le16(pbitmap_rates[2 + i]);
		if (priv->adapter->fw_api_ver == MWIFIEX_FW_V15) {
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
		for (i = 0;
		     i < sizeof(rate_scope->ht_mcs_rate_bitmap) / sizeof(u16);
		     i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				cpu_to_le16(priv->bitmap_rates[2 + i]);
		if (priv->adapter->fw_api_ver == MWIFIEX_FW_V15) {
			for (i = 0;
			     i < ARRAY_SIZE(rate_scope->vht_mcs_rate_bitmap);
			     i++)
				rate_scope->vht_mcs_rate_bitmap[i] =
					cpu_to_le16(priv->bitmap_rates[10 + i]);
		}
	}

	rate_drop = (struct mwifiex_rate_drop_pattern *) ((u8 *) rate_scope +
					     sizeof(struct mwifiex_rate_scope));
	rate_drop->type = cpu_to_le16(TLV_TYPE_RATE_DROP_CONTROL);
	rate_drop->length = cpu_to_le16(sizeof(rate_drop->rate_drop_mode));
	rate_drop->rate_drop_mode = 0;

	cmd->size =
		cpu_to_le16(S_DS_GEN + sizeof(struct host_cmd_ds_tx_rate_cfg) +
			    sizeof(struct mwifiex_rate_scope) +
			    sizeof(struct mwifiex_rate_drop_pattern));

	return 0;
}

/*
 * This function prepares command to set/get Tx power configuration.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting Tx power mode, power group TLV
 *        (as required)
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_tx_power_cfg(struct host_cmd_ds_command *cmd,
				    u16 cmd_action,
				    struct host_cmd_ds_txpwr_cfg *txp)
{
	struct mwifiex_types_power_group *pg_tlv;
	struct host_cmd_ds_txpwr_cfg *cmd_txp_cfg = &cmd->params.txp_cfg;

	cmd->command = cpu_to_le16(HostCmd_CMD_TXPWR_CFG);
	cmd->size =
		cpu_to_le16(S_DS_GEN + sizeof(struct host_cmd_ds_txpwr_cfg));
	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		if (txp->mode) {
			pg_tlv = (struct mwifiex_types_power_group
				  *) ((unsigned long) txp +
				     sizeof(struct host_cmd_ds_txpwr_cfg));
			memmove(cmd_txp_cfg, txp,
				sizeof(struct host_cmd_ds_txpwr_cfg) +
				sizeof(struct mwifiex_types_power_group) +
				le16_to_cpu(pg_tlv->length));

			pg_tlv = (struct mwifiex_types_power_group *) ((u8 *)
				  cmd_txp_cfg +
				  sizeof(struct host_cmd_ds_txpwr_cfg));
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size) +
				  sizeof(struct mwifiex_types_power_group) +
				  le16_to_cpu(pg_tlv->length));
		} else {
			memmove(cmd_txp_cfg, txp, sizeof(*txp));
		}
		cmd_txp_cfg->action = cpu_to_le16(cmd_action);
		break;
	case HostCmd_ACT_GEN_GET:
		cmd_txp_cfg->action = cpu_to_le16(cmd_action);
		break;
	}

	return 0;
}

/*
 * This function prepares command to get RF Tx power.
 */
static int mwifiex_cmd_rf_tx_power(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_action, void *data_buf)
{
	struct host_cmd_ds_rf_tx_pwr *txp = &cmd->params.txp;

	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_rf_tx_pwr)
				+ S_DS_GEN);
	cmd->command = cpu_to_le16(HostCmd_CMD_RF_TX_PWR);
	txp->action = cpu_to_le16(cmd_action);

	return 0;
}

/*
 * This function prepares command to set rf antenna.
 */
static int mwifiex_cmd_rf_antenna(struct mwifiex_private *priv,
				  struct host_cmd_ds_command *cmd,
				  u16 cmd_action,
				  struct mwifiex_ds_ant_cfg *ant_cfg)
{
	struct host_cmd_ds_rf_ant_mimo *ant_mimo = &cmd->params.ant_mimo;
	struct host_cmd_ds_rf_ant_siso *ant_siso = &cmd->params.ant_siso;

	cmd->command = cpu_to_le16(HostCmd_CMD_RF_ANTENNA);

	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		if (priv->adapter->hw_dev_mcs_support == HT_STREAM_2X2) {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_mimo)
						+ S_DS_GEN);
			ant_mimo->action_tx = cpu_to_le16(HostCmd_ACT_SET_TX);
			ant_mimo->tx_ant_mode = cpu_to_le16((u16)ant_cfg->
							    tx_ant);
			ant_mimo->action_rx = cpu_to_le16(HostCmd_ACT_SET_RX);
			ant_mimo->rx_ant_mode = cpu_to_le16((u16)ant_cfg->
							    rx_ant);
		} else {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_siso) +
						S_DS_GEN);
			ant_siso->action = cpu_to_le16(HostCmd_ACT_SET_BOTH);
			ant_siso->ant_mode = cpu_to_le16((u16)ant_cfg->tx_ant);
		}
		break;
	case HostCmd_ACT_GEN_GET:
		if (priv->adapter->hw_dev_mcs_support == HT_STREAM_2X2) {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_mimo) +
						S_DS_GEN);
			ant_mimo->action_tx = cpu_to_le16(HostCmd_ACT_GET_TX);
			ant_mimo->action_rx = cpu_to_le16(HostCmd_ACT_GET_RX);
		} else {
			cmd->size = cpu_to_le16(sizeof(struct
						host_cmd_ds_rf_ant_siso) +
						S_DS_GEN);
			ant_siso->action = cpu_to_le16(HostCmd_ACT_GET_BOTH);
		}
		break;
	}
	return 0;
}

/*
 * This function prepares command to set Host Sleep configuration.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Setting Host Sleep action, conditions, ARP filters
 *        (as required)
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_802_11_hs_cfg(struct mwifiex_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_action,
			  struct mwifiex_hs_config_param *hscfg_param)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_hs_cfg_enh *hs_cfg = &cmd->params.opt_hs_cfg;
	bool hs_activate = false;

	if (!hscfg_param)
		/* New Activate command */
		hs_activate = true;
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_HS_CFG_ENH);

	if (!hs_activate &&
	    (hscfg_param->conditions != cpu_to_le32(HS_CFG_CANCEL)) &&
	    ((adapter->arp_filter_size > 0) &&
	     (adapter->arp_filter_size <= ARP_FILTER_MAX_BUF_SIZE))) {
		mwifiex_dbg(adapter, CMD,
			    "cmd: Attach %d bytes ArpFilter to HSCfg cmd\n",
			    adapter->arp_filter_size);
		memcpy(((u8 *) hs_cfg) +
		       sizeof(struct host_cmd_ds_802_11_hs_cfg_enh),
		       adapter->arp_filter, adapter->arp_filter_size);
		cmd->size = cpu_to_le16
				(adapter->arp_filter_size +
				 sizeof(struct host_cmd_ds_802_11_hs_cfg_enh)
				+ S_DS_GEN);
	} else {
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof(struct
						host_cmd_ds_802_11_hs_cfg_enh));
	}
	if (hs_activate) {
		hs_cfg->action = cpu_to_le16(HS_ACTIVATE);
		hs_cfg->params.hs_activate.resp_ctrl = cpu_to_le16(RESP_NEEDED);
	} else {
		hs_cfg->action = cpu_to_le16(HS_CONFIGURE);
		hs_cfg->params.hs_config.conditions = hscfg_param->conditions;
		hs_cfg->params.hs_config.gpio = hscfg_param->gpio;
		hs_cfg->params.hs_config.gap = hscfg_param->gap;
		mwifiex_dbg(adapter, CMD,
			    "cmd: HS_CFG_CMD: condition:0x%x gpio:0x%x gap:0x%x\n",
			    hs_cfg->params.hs_config.conditions,
			    hs_cfg->params.hs_config.gpio,
			    hs_cfg->params.hs_config.gap);
	}

	return 0;
}

/*
 * This function prepares command to set/get MAC address.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting MAC address (for SET only)
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_802_11_mac_address(struct mwifiex_private *priv,
					  struct host_cmd_ds_command *cmd,
					  u16 cmd_action)
{
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_MAC_ADDRESS);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_mac_address) +
				S_DS_GEN);
	cmd->result = 0;

	cmd->params.mac_addr.action = cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_SET)
		memcpy(cmd->params.mac_addr.mac_addr, priv->curr_addr,
		       ETH_ALEN);
	return 0;
}

/*
 * This function prepares command to set MAC multicast address.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting MAC multicast address
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_mac_multicast_adr(struct host_cmd_ds_command *cmd,
			      u16 cmd_action,
			      struct mwifiex_multicast_list *mcast_list)
{
	struct host_cmd_ds_mac_multicast_adr *mcast_addr = &cmd->params.mc_addr;

	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_mac_multicast_adr) +
				S_DS_GEN);
	cmd->command = cpu_to_le16(HostCmd_CMD_MAC_MULTICAST_ADR);

	mcast_addr->action = cpu_to_le16(cmd_action);
	mcast_addr->num_of_adrs =
		cpu_to_le16((u16) mcast_list->num_multicast_addr);
	memcpy(mcast_addr->mac_list, mcast_list->mac_list,
	       mcast_list->num_multicast_addr * ETH_ALEN);

	return 0;
}

/*
 * This function prepares command to deauthenticate.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Setting AP MAC address and reason code
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_802_11_deauthenticate(struct mwifiex_private *priv,
					     struct host_cmd_ds_command *cmd,
					     u8 *mac)
{
	struct host_cmd_ds_802_11_deauthenticate *deauth = &cmd->params.deauth;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_DEAUTHENTICATE);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_deauthenticate)
				+ S_DS_GEN);

	/* Set AP MAC address */
	memcpy(deauth->mac_addr, mac, ETH_ALEN);

	mwifiex_dbg(priv->adapter, CMD, "cmd: Deauth: %pM\n", deauth->mac_addr);

	deauth->reason_code = cpu_to_le16(WLAN_REASON_DEAUTH_LEAVING);

	return 0;
}

/*
 * This function prepares command to stop Ad-Hoc network.
 *
 * Preparation includes -
 *      - Setting command ID and proper size
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_802_11_ad_hoc_stop(struct host_cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_AD_HOC_STOP);
	cmd->size = cpu_to_le16(S_DS_GEN);
	return 0;
}

/*
 * This function sets WEP key(s) to key parameter TLV(s).
 *
 * Multi-key parameter TLVs are supported, so we can send multiple
 * WEP keys in a single buffer.
 */
static int
mwifiex_set_keyparamset_wep(struct mwifiex_private *priv,
			    struct mwifiex_ie_type_key_param_set *key_param_set,
			    u16 *key_param_len)
{
	int cur_key_param_len;
	u8 i;

	/* Multi-key_param_set TLV is supported */
	for (i = 0; i < NUM_WEP_KEYS; i++) {
		if ((priv->wep_key[i].key_length == WLAN_KEY_LEN_WEP40) ||
		    (priv->wep_key[i].key_length == WLAN_KEY_LEN_WEP104)) {
			key_param_set->type =
				cpu_to_le16(TLV_TYPE_KEY_MATERIAL);
/* Key_param_set WEP fixed length */
#define KEYPARAMSET_WEP_FIXED_LEN 8
			key_param_set->length = cpu_to_le16((u16)
					(priv->wep_key[i].
					 key_length +
					 KEYPARAMSET_WEP_FIXED_LEN));
			key_param_set->key_type_id =
				cpu_to_le16(KEY_TYPE_ID_WEP);
			key_param_set->key_info =
				cpu_to_le16(KEY_ENABLED | KEY_UNICAST |
					    KEY_MCAST);
			key_param_set->key_len =
				cpu_to_le16(priv->wep_key[i].key_length);
			/* Set WEP key index */
			key_param_set->key[0] = i;
			/* Set default Tx key flag */
			if (i ==
			    (priv->
			     wep_key_curr_index & HostCmd_WEP_KEY_INDEX_MASK))
				key_param_set->key[1] = 1;
			else
				key_param_set->key[1] = 0;
			memmove(&key_param_set->key[2],
				priv->wep_key[i].key_material,
				priv->wep_key[i].key_length);

			cur_key_param_len = priv->wep_key[i].key_length +
				KEYPARAMSET_WEP_FIXED_LEN +
				sizeof(struct mwifiex_ie_types_header);
			*key_param_len += (u16) cur_key_param_len;
			key_param_set =
				(struct mwifiex_ie_type_key_param_set *)
						((u8 *)key_param_set +
						 cur_key_param_len);
		} else if (!priv->wep_key[i].key_length) {
			continue;
		} else {
			mwifiex_dbg(priv->adapter, ERROR,
				    "key%d Length = %d is incorrect\n",
				    (i + 1), priv->wep_key[i].key_length);
			return -1;
		}
	}

	return 0;
}

/* This function populates key material v2 command
 * to set network key for AES & CMAC AES.
 */
static int mwifiex_set_aes_key_v2(struct mwifiex_private *priv,
				  struct host_cmd_ds_command *cmd,
				  struct mwifiex_ds_encrypt_key *enc_key,
				  struct host_cmd_ds_802_11_key_material_v2 *km)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u16 size, len = KEY_PARAMS_FIXED_LEN;

	if (enc_key->is_igtk_key) {
		mwifiex_dbg(adapter, INFO,
			    "%s: Set CMAC AES Key\n", __func__);
		if (enc_key->is_rx_seq_valid)
			memcpy(km->key_param_set.key_params.cmac_aes.ipn,
			       enc_key->pn, enc_key->pn_len);
		km->key_param_set.key_info &= cpu_to_le16(~KEY_MCAST);
		km->key_param_set.key_info |= cpu_to_le16(KEY_IGTK);
		km->key_param_set.key_type = KEY_TYPE_ID_AES_CMAC;
		km->key_param_set.key_params.cmac_aes.key_len =
					  cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.cmac_aes.key,
		       enc_key->key_material, enc_key->key_len);
		len += sizeof(struct mwifiex_cmac_aes_param);
	} else {
		mwifiex_dbg(adapter, INFO,
			    "%s: Set AES Key\n", __func__);
		if (enc_key->is_rx_seq_valid)
			memcpy(km->key_param_set.key_params.aes.pn,
			       enc_key->pn, enc_key->pn_len);
		km->key_param_set.key_type = KEY_TYPE_ID_AES;
		km->key_param_set.key_params.aes.key_len =
					  cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.aes.key,
		       enc_key->key_material, enc_key->key_len);
		len += sizeof(struct mwifiex_aes_param);
	}

	km->key_param_set.len = cpu_to_le16(len);
	size = len + sizeof(struct mwifiex_ie_types_header) +
	       sizeof(km->action) + S_DS_GEN;
	cmd->size = cpu_to_le16(size);

	return 0;
}

/* This function prepares command to set/get/reset network key(s).
 * This function prepares key material command for V2 format.
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting WEP keys, WAPI keys or WPA keys along with required
 *        encryption (TKIP, AES) (as required)
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_802_11_key_material_v2(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_action, u32 cmd_oid,
				   struct mwifiex_ds_encrypt_key *enc_key)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u8 *mac = enc_key->mac_addr;
	u16 key_info, len = KEY_PARAMS_FIXED_LEN;
	struct host_cmd_ds_802_11_key_material_v2 *km =
						&cmd->params.key_material_v2;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_KEY_MATERIAL);
	km->action = cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_GET) {
		mwifiex_dbg(adapter, INFO, "%s: Get key\n", __func__);
		km->key_param_set.key_idx =
					enc_key->key_index & KEY_INDEX_MASK;
		km->key_param_set.type = cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
		km->key_param_set.len = cpu_to_le16(KEY_PARAMS_FIXED_LEN);
		memcpy(km->key_param_set.mac_addr, mac, ETH_ALEN);

		if (enc_key->key_index & MWIFIEX_KEY_INDEX_UNICAST)
			key_info = KEY_UNICAST;
		else
			key_info = KEY_MCAST;

		if (enc_key->is_igtk_key)
			key_info |= KEY_IGTK;

		km->key_param_set.key_info = cpu_to_le16(key_info);

		cmd->size = cpu_to_le16(sizeof(struct mwifiex_ie_types_header) +
					S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					sizeof(km->action));
		return 0;
	}

	memset(&km->key_param_set, 0,
	       sizeof(struct mwifiex_ie_type_key_param_set_v2));

	if (enc_key->key_disable) {
		mwifiex_dbg(adapter, INFO, "%s: Remove key\n", __func__);
		km->action = cpu_to_le16(HostCmd_ACT_GEN_REMOVE);
		km->key_param_set.type = cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
		km->key_param_set.len = cpu_to_le16(KEY_PARAMS_FIXED_LEN);
		km->key_param_set.key_idx = enc_key->key_index & KEY_INDEX_MASK;
		key_info = KEY_MCAST | KEY_UNICAST;
		km->key_param_set.key_info = cpu_to_le16(key_info);
		memcpy(km->key_param_set.mac_addr, mac, ETH_ALEN);
		cmd->size = cpu_to_le16(sizeof(struct mwifiex_ie_types_header) +
					S_DS_GEN + KEY_PARAMS_FIXED_LEN +
					sizeof(km->action));
		return 0;
	}

	km->action = cpu_to_le16(HostCmd_ACT_GEN_SET);
	km->key_param_set.key_idx = enc_key->key_index & KEY_INDEX_MASK;
	km->key_param_set.type = cpu_to_le16(TLV_TYPE_KEY_PARAM_V2);
	key_info = KEY_ENABLED;
	memcpy(km->key_param_set.mac_addr, mac, ETH_ALEN);

	if (enc_key->key_len <= WLAN_KEY_LEN_WEP104) {
		mwifiex_dbg(adapter, INFO, "%s: Set WEP Key\n", __func__);
		len += sizeof(struct mwifiex_wep_param);
		km->key_param_set.len = cpu_to_le16(len);
		km->key_param_set.key_type = KEY_TYPE_ID_WEP;

		if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
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

		cmd->size = cpu_to_le16(sizeof(struct mwifiex_ie_types_header) +
					len + sizeof(km->action) + S_DS_GEN);
		return 0;
	}

	if (is_broadcast_ether_addr(mac))
		key_info |= KEY_MCAST | KEY_RX_KEY;
	else
		key_info |= KEY_UNICAST | KEY_TX_KEY | KEY_RX_KEY;

	if (enc_key->is_wapi_key) {
		mwifiex_dbg(adapter, INFO, "%s: Set WAPI Key\n", __func__);
		km->key_param_set.key_type = KEY_TYPE_ID_WAPI;
		memcpy(km->key_param_set.key_params.wapi.pn, enc_key->pn,
		       PN_LEN);
		km->key_param_set.key_params.wapi.key_len =
						cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.wapi.key,
		       enc_key->key_material, enc_key->key_len);
		if (is_broadcast_ether_addr(mac))
			priv->sec_info.wapi_key_on = true;

		if (!priv->sec_info.wapi_key_on)
			key_info |= KEY_DEFAULT;
		km->key_param_set.key_info = cpu_to_le16(key_info);

		len += sizeof(struct mwifiex_wapi_param);
		km->key_param_set.len = cpu_to_le16(len);
		cmd->size = cpu_to_le16(sizeof(struct mwifiex_ie_types_header) +
					len + sizeof(km->action) + S_DS_GEN);
		return 0;
	}

	if (priv->bss_mode == NL80211_IFTYPE_ADHOC) {
		key_info |= KEY_DEFAULT;
		/* Enable unicast bit for WPA-NONE/ADHOC_AES */
		if (!priv->sec_info.wpa2_enabled &&
		    !is_broadcast_ether_addr(mac))
			key_info |= KEY_UNICAST;
	} else {
		/* Enable default key for WPA/WPA2 */
		if (!priv->wpa_is_gtk_set)
			key_info |= KEY_DEFAULT;
	}

	km->key_param_set.key_info = cpu_to_le16(key_info);

	if (enc_key->key_len == WLAN_KEY_LEN_CCMP)
		return mwifiex_set_aes_key_v2(priv, cmd, enc_key, km);

	if (enc_key->key_len == WLAN_KEY_LEN_TKIP) {
		mwifiex_dbg(adapter, INFO,
			    "%s: Set TKIP Key\n", __func__);
		if (enc_key->is_rx_seq_valid)
			memcpy(km->key_param_set.key_params.tkip.pn,
			       enc_key->pn, enc_key->pn_len);
		km->key_param_set.key_type = KEY_TYPE_ID_TKIP;
		km->key_param_set.key_params.tkip.key_len =
						cpu_to_le16(enc_key->key_len);
		memcpy(km->key_param_set.key_params.tkip.key,
		       enc_key->key_material, enc_key->key_len);

		len += sizeof(struct mwifiex_tkip_param);
		km->key_param_set.len = cpu_to_le16(len);
		cmd->size = cpu_to_le16(sizeof(struct mwifiex_ie_types_header) +
					len + sizeof(km->action) + S_DS_GEN);
	}

	return 0;
}

/*
 * This function prepares command to set/get/reset network key(s).
 * This function prepares key material command for V1 format.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting WEP keys, WAPI keys or WPA keys along with required
 *        encryption (TKIP, AES) (as required)
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_802_11_key_material_v1(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_action, u32 cmd_oid,
				   struct mwifiex_ds_encrypt_key *enc_key)
{
	struct host_cmd_ds_802_11_key_material *key_material =
		&cmd->params.key_material;
	struct host_cmd_tlv_mac_addr *tlv_mac;
	u16 key_param_len = 0, cmd_size;
	int ret = 0;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_KEY_MATERIAL);
	key_material->action = cpu_to_le16(cmd_action);

	if (cmd_action == HostCmd_ACT_GEN_GET) {
		cmd->size =
			cpu_to_le16(sizeof(key_material->action) + S_DS_GEN);
		return ret;
	}

	if (!enc_key) {
		memset(&key_material->key_param_set, 0,
		       (NUM_WEP_KEYS *
			sizeof(struct mwifiex_ie_type_key_param_set)));
		ret = mwifiex_set_keyparamset_wep(priv,
						  &key_material->key_param_set,
						  &key_param_len);
		cmd->size = cpu_to_le16(key_param_len +
				    sizeof(key_material->action) + S_DS_GEN);
		return ret;
	} else
		memset(&key_material->key_param_set, 0,
		       sizeof(struct mwifiex_ie_type_key_param_set));
	if (enc_key->is_wapi_key) {
		mwifiex_dbg(priv->adapter, INFO, "info: Set WAPI Key\n");
		key_material->key_param_set.key_type_id =
						cpu_to_le16(KEY_TYPE_ID_WAPI);
		if (cmd_oid == KEY_INFO_ENABLED)
			key_material->key_param_set.key_info =
						cpu_to_le16(KEY_ENABLED);
		else
			key_material->key_param_set.key_info =
						cpu_to_le16(!KEY_ENABLED);

		key_material->key_param_set.key[0] = enc_key->key_index;
		if (!priv->sec_info.wapi_key_on)
			key_material->key_param_set.key[1] = 1;
		else
			/* set 0 when re-key */
			key_material->key_param_set.key[1] = 0;

		if (!is_broadcast_ether_addr(enc_key->mac_addr)) {
			/* WAPI pairwise key: unicast */
			key_material->key_param_set.key_info |=
				cpu_to_le16(KEY_UNICAST);
		} else {	/* WAPI group key: multicast */
			key_material->key_param_set.key_info |=
				cpu_to_le16(KEY_MCAST);
			priv->sec_info.wapi_key_on = true;
		}

		key_material->key_param_set.type =
					cpu_to_le16(TLV_TYPE_KEY_MATERIAL);
		key_material->key_param_set.key_len =
						cpu_to_le16(WAPI_KEY_LEN);
		memcpy(&key_material->key_param_set.key[2],
		       enc_key->key_material, enc_key->key_len);
		memcpy(&key_material->key_param_set.key[2 + enc_key->key_len],
		       enc_key->pn, PN_LEN);
		key_material->key_param_set.length =
			cpu_to_le16(WAPI_KEY_LEN + KEYPARAMSET_FIXED_LEN);

		key_param_len = (WAPI_KEY_LEN + KEYPARAMSET_FIXED_LEN) +
				 sizeof(struct mwifiex_ie_types_header);
		cmd->size = cpu_to_le16(sizeof(key_material->action)
					+ S_DS_GEN +  key_param_len);
		return ret;
	}
	if (enc_key->key_len == WLAN_KEY_LEN_CCMP) {
		if (enc_key->is_igtk_key) {
			mwifiex_dbg(priv->adapter, CMD, "cmd: CMAC_AES\n");
			key_material->key_param_set.key_type_id =
					cpu_to_le16(KEY_TYPE_ID_AES_CMAC);
			if (cmd_oid == KEY_INFO_ENABLED)
				key_material->key_param_set.key_info =
						cpu_to_le16(KEY_ENABLED);
			else
				key_material->key_param_set.key_info =
						cpu_to_le16(!KEY_ENABLED);

			key_material->key_param_set.key_info |=
							cpu_to_le16(KEY_IGTK);
		} else {
			mwifiex_dbg(priv->adapter, CMD, "cmd: WPA_AES\n");
			key_material->key_param_set.key_type_id =
						cpu_to_le16(KEY_TYPE_ID_AES);
			if (cmd_oid == KEY_INFO_ENABLED)
				key_material->key_param_set.key_info =
						cpu_to_le16(KEY_ENABLED);
			else
				key_material->key_param_set.key_info =
						cpu_to_le16(!KEY_ENABLED);

			if (enc_key->key_index & MWIFIEX_KEY_INDEX_UNICAST)
				/* AES pairwise key: unicast */
				key_material->key_param_set.key_info |=
						cpu_to_le16(KEY_UNICAST);
			else	/* AES group key: multicast */
				key_material->key_param_set.key_info |=
							cpu_to_le16(KEY_MCAST);
		}
	} else if (enc_key->key_len == WLAN_KEY_LEN_TKIP) {
		mwifiex_dbg(priv->adapter, CMD, "cmd: WPA_TKIP\n");
		key_material->key_param_set.key_type_id =
						cpu_to_le16(KEY_TYPE_ID_TKIP);
		key_material->key_param_set.key_info =
						cpu_to_le16(KEY_ENABLED);

		if (enc_key->key_index & MWIFIEX_KEY_INDEX_UNICAST)
				/* TKIP pairwise key: unicast */
			key_material->key_param_set.key_info |=
						cpu_to_le16(KEY_UNICAST);
		else		/* TKIP group key: multicast */
			key_material->key_param_set.key_info |=
							cpu_to_le16(KEY_MCAST);
	}

	if (key_material->key_param_set.key_type_id) {
		key_material->key_param_set.type =
					cpu_to_le16(TLV_TYPE_KEY_MATERIAL);
		key_material->key_param_set.key_len =
					cpu_to_le16((u16) enc_key->key_len);
		memcpy(key_material->key_param_set.key, enc_key->key_material,
		       enc_key->key_len);
		key_material->key_param_set.length =
			cpu_to_le16((u16) enc_key->key_len +
				    KEYPARAMSET_FIXED_LEN);

		key_param_len = (u16)(enc_key->key_len + KEYPARAMSET_FIXED_LEN)
				+ sizeof(struct mwifiex_ie_types_header);

		if (le16_to_cpu(key_material->key_param_set.key_type_id) ==
							KEY_TYPE_ID_AES_CMAC) {
			struct mwifiex_cmac_param *param =
					(void *)key_material->key_param_set.key;

			memcpy(param->ipn, enc_key->pn, IGTK_PN_LEN);
			memcpy(param->key, enc_key->key_material,
			       WLAN_KEY_LEN_AES_CMAC);

			key_param_len = sizeof(struct mwifiex_cmac_param);
			key_material->key_param_set.key_len =
						cpu_to_le16(key_param_len);
			key_param_len += KEYPARAMSET_FIXED_LEN;
			key_material->key_param_set.length =
						cpu_to_le16(key_param_len);
			key_param_len += sizeof(struct mwifiex_ie_types_header);
		}

		cmd->size = cpu_to_le16(sizeof(key_material->action) + S_DS_GEN
					+ key_param_len);

		if (GET_BSS_ROLE(priv) == MWIFIEX_BSS_ROLE_UAP) {
			tlv_mac = (void *)((u8 *)&key_material->key_param_set +
					   key_param_len);
			tlv_mac->header.type =
					cpu_to_le16(TLV_TYPE_STA_MAC_ADDR);
			tlv_mac->header.len = cpu_to_le16(ETH_ALEN);
			memcpy(tlv_mac->mac_addr, enc_key->mac_addr, ETH_ALEN);
			cmd_size = key_param_len + S_DS_GEN +
				   sizeof(key_material->action) +
				   sizeof(struct host_cmd_tlv_mac_addr);
		} else {
			cmd_size = key_param_len + S_DS_GEN +
				   sizeof(key_material->action);
		}
		cmd->size = cpu_to_le16(cmd_size);
	}

	return ret;
}

/* Wrapper function for setting network key depending upon FW KEY API version */
static int
mwifiex_cmd_802_11_key_material(struct mwifiex_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_action, u32 cmd_oid,
				struct mwifiex_ds_encrypt_key *enc_key)
{
	if (priv->adapter->key_api_major_ver == KEY_API_VER_MAJOR_V2)
		return mwifiex_cmd_802_11_key_material_v2(priv, cmd,
							  cmd_action, cmd_oid,
							  enc_key);

	else
		return mwifiex_cmd_802_11_key_material_v1(priv, cmd,
							  cmd_action, cmd_oid,
							  enc_key);
}

/*
 * This function prepares command to set/get 11d domain information.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting domain information fields (for SET only)
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_802_11d_domain_info(struct mwifiex_private *priv,
					   struct host_cmd_ds_command *cmd,
					   u16 cmd_action)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11d_domain_info *domain_info =
		&cmd->params.domain_info;
	struct mwifiex_ietypes_domain_param_set *domain =
		&domain_info->domain;
	u8 no_of_triplet = adapter->domain_reg.no_of_triplet;

	mwifiex_dbg(adapter, INFO,
		    "info: 11D: no_of_triplet=0x%x\n", no_of_triplet);

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11D_DOMAIN_INFO);
	domain_info->action = cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_GET) {
		cmd->size = cpu_to_le16(sizeof(domain_info->action) + S_DS_GEN);
		return 0;
	}

	/* Set domain info fields */
	domain->header.type = cpu_to_le16(WLAN_EID_COUNTRY);
	memcpy(domain->country_code, adapter->domain_reg.country_code,
	       sizeof(domain->country_code));

	domain->header.len =
		cpu_to_le16((no_of_triplet *
			     sizeof(struct ieee80211_country_ie_triplet))
			    + sizeof(domain->country_code));

	if (no_of_triplet) {
		memcpy(domain->triplet, adapter->domain_reg.triplet,
		       no_of_triplet * sizeof(struct
					      ieee80211_country_ie_triplet));

		cmd->size = cpu_to_le16(sizeof(domain_info->action) +
					le16_to_cpu(domain->header.len) +
					sizeof(struct mwifiex_ie_types_header)
					+ S_DS_GEN);
	} else {
		cmd->size = cpu_to_le16(sizeof(domain_info->action) + S_DS_GEN);
	}

	return 0;
}

/*
 * This function prepares command to set/get IBSS coalescing status.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting status to enable or disable (for SET only)
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_ibss_coalescing_status(struct host_cmd_ds_command *cmd,
					      u16 cmd_action, u16 *enable)
{
	struct host_cmd_ds_802_11_ibss_status *ibss_coal =
		&(cmd->params.ibss_coalescing);

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_IBSS_COALESCING_STATUS);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_ibss_status) +
				S_DS_GEN);
	cmd->result = 0;
	ibss_coal->action = cpu_to_le16(cmd_action);

	switch (cmd_action) {
	case HostCmd_ACT_GEN_SET:
		if (enable)
			ibss_coal->enable = cpu_to_le16(*enable);
		else
			ibss_coal->enable = 0;
		break;

		/* In other case.. Nothing to do */
	case HostCmd_ACT_GEN_GET:
	default:
		break;
	}

	return 0;
}

/* This function prepares command buffer to get/set memory location value.
 */
static int
mwifiex_cmd_mem_access(struct host_cmd_ds_command *cmd, u16 cmd_action,
		       void *pdata_buf)
{
	struct mwifiex_ds_mem_rw *mem_rw = (void *)pdata_buf;
	struct host_cmd_ds_mem_access *mem_access = (void *)&cmd->params.mem;

	cmd->command = cpu_to_le16(HostCmd_CMD_MEM_ACCESS);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_mem_access) +
				S_DS_GEN);

	mem_access->action = cpu_to_le16(cmd_action);
	mem_access->addr = cpu_to_le32(mem_rw->addr);
	mem_access->value = cpu_to_le32(mem_rw->value);

	return 0;
}

/*
 * This function prepares command to set/get register value.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting register offset (for both GET and SET) and
 *        register value (for SET only)
 *      - Ensuring correct endian-ness
 *
 * The following type of registers can be accessed with this function -
 *      - MAC register
 *      - BBP register
 *      - RF register
 *      - PMIC register
 *      - CAU register
 *      - EEPROM
 */
static int mwifiex_cmd_reg_access(struct host_cmd_ds_command *cmd,
				  u16 cmd_action, void *data_buf)
{
	struct mwifiex_ds_reg_rw *reg_rw = data_buf;

	switch (le16_to_cpu(cmd->command)) {
	case HostCmd_CMD_MAC_REG_ACCESS:
	{
		struct host_cmd_ds_mac_reg_access *mac_reg;

		cmd->size = cpu_to_le16(sizeof(*mac_reg) + S_DS_GEN);
		mac_reg = &cmd->params.mac_reg;
		mac_reg->action = cpu_to_le16(cmd_action);
		mac_reg->offset = cpu_to_le16((u16) reg_rw->offset);
		mac_reg->value = cpu_to_le32(reg_rw->value);
		break;
	}
	case HostCmd_CMD_BBP_REG_ACCESS:
	{
		struct host_cmd_ds_bbp_reg_access *bbp_reg;

		cmd->size = cpu_to_le16(sizeof(*bbp_reg) + S_DS_GEN);
		bbp_reg = &cmd->params.bbp_reg;
		bbp_reg->action = cpu_to_le16(cmd_action);
		bbp_reg->offset = cpu_to_le16((u16) reg_rw->offset);
		bbp_reg->value = (u8) reg_rw->value;
		break;
	}
	case HostCmd_CMD_RF_REG_ACCESS:
	{
		struct host_cmd_ds_rf_reg_access *rf_reg;

		cmd->size = cpu_to_le16(sizeof(*rf_reg) + S_DS_GEN);
		rf_reg = &cmd->params.rf_reg;
		rf_reg->action = cpu_to_le16(cmd_action);
		rf_reg->offset = cpu_to_le16((u16) reg_rw->offset);
		rf_reg->value = (u8) reg_rw->value;
		break;
	}
	case HostCmd_CMD_PMIC_REG_ACCESS:
	{
		struct host_cmd_ds_pmic_reg_access *pmic_reg;

		cmd->size = cpu_to_le16(sizeof(*pmic_reg) + S_DS_GEN);
		pmic_reg = &cmd->params.pmic_reg;
		pmic_reg->action = cpu_to_le16(cmd_action);
		pmic_reg->offset = cpu_to_le16((u16) reg_rw->offset);
		pmic_reg->value = (u8) reg_rw->value;
		break;
	}
	case HostCmd_CMD_CAU_REG_ACCESS:
	{
		struct host_cmd_ds_rf_reg_access *cau_reg;

		cmd->size = cpu_to_le16(sizeof(*cau_reg) + S_DS_GEN);
		cau_reg = &cmd->params.rf_reg;
		cau_reg->action = cpu_to_le16(cmd_action);
		cau_reg->offset = cpu_to_le16((u16) reg_rw->offset);
		cau_reg->value = (u8) reg_rw->value;
		break;
	}
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
	{
		struct mwifiex_ds_read_eeprom *rd_eeprom = data_buf;
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
		return -1;
	}

	return 0;
}

/*
 * This function prepares command to set PCI-Express
 * host buffer configuration
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting host buffer configuration
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_pcie_host_spec(struct mwifiex_private *priv,
			   struct host_cmd_ds_command *cmd, u16 action)
{
	struct host_cmd_ds_pcie_details *host_spec =
					&cmd->params.pcie_host_spec;
	struct pcie_service_card *card = priv->adapter->card;

	cmd->command = cpu_to_le16(HostCmd_CMD_PCIE_DESC_DETAILS);
	cmd->size = cpu_to_le16(sizeof(struct
					host_cmd_ds_pcie_details) + S_DS_GEN);
	cmd->result = 0;

	memset(host_spec, 0, sizeof(struct host_cmd_ds_pcie_details));

	if (action != HostCmd_ACT_GEN_SET)
		return 0;

	/* Send the ring base addresses and count to firmware */
	host_spec->txbd_addr_lo = cpu_to_le32((u32)(card->txbd_ring_pbase));
	host_spec->txbd_addr_hi =
			cpu_to_le32((u32)(((u64)card->txbd_ring_pbase) >> 32));
	host_spec->txbd_count = cpu_to_le32(MWIFIEX_MAX_TXRX_BD);
	host_spec->rxbd_addr_lo = cpu_to_le32((u32)(card->rxbd_ring_pbase));
	host_spec->rxbd_addr_hi =
			cpu_to_le32((u32)(((u64)card->rxbd_ring_pbase) >> 32));
	host_spec->rxbd_count = cpu_to_le32(MWIFIEX_MAX_TXRX_BD);
	host_spec->evtbd_addr_lo = cpu_to_le32((u32)(card->evtbd_ring_pbase));
	host_spec->evtbd_addr_hi =
			cpu_to_le32((u32)(((u64)card->evtbd_ring_pbase) >> 32));
	host_spec->evtbd_count = cpu_to_le32(MWIFIEX_MAX_EVT_BD);
	if (card->sleep_cookie_vbase) {
		host_spec->sleep_cookie_addr_lo =
				cpu_to_le32((u32)(card->sleep_cookie_pbase));
		host_spec->sleep_cookie_addr_hi = cpu_to_le32((u32)(((u64)
					(card->sleep_cookie_pbase)) >> 32));
		mwifiex_dbg(priv->adapter, INFO,
			    "sleep_cook_lo phy addr: 0x%x\n",
			    host_spec->sleep_cookie_addr_lo);
	}

	return 0;
}

/*
 * This function prepares command for event subscription, configuration
 * and query. Events can be subscribed or unsubscribed. Current subscribed
 * events can be queried. Also, current subscribed events are reported in
 * every FW response.
 */
static int
mwifiex_cmd_802_11_subsc_evt(struct mwifiex_private *priv,
			     struct host_cmd_ds_command *cmd,
			     struct mwifiex_ds_misc_subsc_evt *subsc_evt_cfg)
{
	struct host_cmd_ds_802_11_subsc_evt *subsc_evt = &cmd->params.subsc_evt;
	struct mwifiex_ie_types_rssi_threshold *rssi_tlv;
	u16 event_bitmap;
	u8 *pos;

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_SUBSCRIBE_EVENT);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_subsc_evt) +
				S_DS_GEN);

	subsc_evt->action = cpu_to_le16(subsc_evt_cfg->action);
	mwifiex_dbg(priv->adapter, CMD,
		    "cmd: action: %d\n", subsc_evt_cfg->action);

	/*For query requests, no configuration TLV structures are to be added.*/
	if (subsc_evt_cfg->action == HostCmd_ACT_GEN_GET)
		return 0;

	subsc_evt->events = cpu_to_le16(subsc_evt_cfg->events);

	event_bitmap = subsc_evt_cfg->events;
	mwifiex_dbg(priv->adapter, CMD, "cmd: event bitmap : %16x\n",
		    event_bitmap);

	if (((subsc_evt_cfg->action == HostCmd_ACT_BITWISE_CLR) ||
	     (subsc_evt_cfg->action == HostCmd_ACT_BITWISE_SET)) &&
	    (event_bitmap == 0)) {
		mwifiex_dbg(priv->adapter, ERROR,
			    "Error: No event specified\t"
			    "for bitwise action type\n");
		return -EINVAL;
	}

	/*
	 * Append TLV structures for each of the specified events for
	 * subscribing or re-configuring. This is not required for
	 * bitwise unsubscribing request.
	 */
	if (subsc_evt_cfg->action == HostCmd_ACT_BITWISE_CLR)
		return 0;

	pos = ((u8 *)subsc_evt) +
			sizeof(struct host_cmd_ds_802_11_subsc_evt);

	if (event_bitmap & BITMASK_BCN_RSSI_LOW) {
		rssi_tlv = (struct mwifiex_ie_types_rssi_threshold *) pos;

		rssi_tlv->header.type = cpu_to_le16(TLV_TYPE_RSSI_LOW);
		rssi_tlv->header.len =
		    cpu_to_le16(sizeof(struct mwifiex_ie_types_rssi_threshold) -
				sizeof(struct mwifiex_ie_types_header));
		rssi_tlv->abs_value = subsc_evt_cfg->bcn_l_rssi_cfg.abs_value;
		rssi_tlv->evt_freq = subsc_evt_cfg->bcn_l_rssi_cfg.evt_freq;

		mwifiex_dbg(priv->adapter, EVENT,
			    "Cfg Beacon Low Rssi event,\t"
			    "RSSI:-%d dBm, Freq:%d\n",
			    subsc_evt_cfg->bcn_l_rssi_cfg.abs_value,
			    subsc_evt_cfg->bcn_l_rssi_cfg.evt_freq);

		pos += sizeof(struct mwifiex_ie_types_rssi_threshold);
		le16_add_cpu(&cmd->size,
			     sizeof(struct mwifiex_ie_types_rssi_threshold));
	}

	if (event_bitmap & BITMASK_BCN_RSSI_HIGH) {
		rssi_tlv = (struct mwifiex_ie_types_rssi_threshold *) pos;

		rssi_tlv->header.type = cpu_to_le16(TLV_TYPE_RSSI_HIGH);
		rssi_tlv->header.len =
		    cpu_to_le16(sizeof(struct mwifiex_ie_types_rssi_threshold) -
				sizeof(struct mwifiex_ie_types_header));
		rssi_tlv->abs_value = subsc_evt_cfg->bcn_h_rssi_cfg.abs_value;
		rssi_tlv->evt_freq = subsc_evt_cfg->bcn_h_rssi_cfg.evt_freq;

		mwifiex_dbg(priv->adapter, EVENT,
			    "Cfg Beacon High Rssi event,\t"
			    "RSSI:-%d dBm, Freq:%d\n",
			    subsc_evt_cfg->bcn_h_rssi_cfg.abs_value,
			    subsc_evt_cfg->bcn_h_rssi_cfg.evt_freq);

		pos += sizeof(struct mwifiex_ie_types_rssi_threshold);
		le16_add_cpu(&cmd->size,
			     sizeof(struct mwifiex_ie_types_rssi_threshold));
	}

	return 0;
}

static int
mwifiex_cmd_append_rpn_expression(struct mwifiex_private *priv,
				  struct mwifiex_mef_entry *mef_entry,
				  u8 **buffer)
{
	struct mwifiex_mef_filter *filter = mef_entry->filter;
	int i, byte_len;
	u8 *stack_ptr = *buffer;

	for (i = 0; i < MWIFIEX_MEF_MAX_FILTERS; i++) {
		filter = &mef_entry->filter[i];
		if (!filter->filt_type)
			break;
		*(__le32 *)stack_ptr = cpu_to_le32((u32)filter->repeat);
		stack_ptr += 4;
		*stack_ptr = TYPE_DNUM;
		stack_ptr += 1;

		byte_len = filter->byte_seq[MWIFIEX_MEF_MAX_BYTESEQ];
		memcpy(stack_ptr, filter->byte_seq, byte_len);
		stack_ptr += byte_len;
		*stack_ptr = byte_len;
		stack_ptr += 1;
		*stack_ptr = TYPE_BYTESEQ;
		stack_ptr += 1;

		*(__le32 *)stack_ptr = cpu_to_le32((u32)filter->offset);
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
			return -1;
	}

	*buffer = stack_ptr;
	return 0;
}

static int
mwifiex_cmd_mef_cfg(struct mwifiex_private *priv,
		    struct host_cmd_ds_command *cmd,
		    struct mwifiex_ds_mef_cfg *mef)
{
	struct host_cmd_ds_mef_cfg *mef_cfg = &cmd->params.mef_cfg;
	struct mwifiex_fw_mef_entry *mef_entry = NULL;
	u8 *pos = (u8 *)mef_cfg;
	u16 i;

	cmd->command = cpu_to_le16(HostCmd_CMD_MEF_CFG);

	mef_cfg->criteria = cpu_to_le32(mef->criteria);
	mef_cfg->num_entries = cpu_to_le16(mef->num_entries);
	pos += sizeof(*mef_cfg);

	for (i = 0; i < mef->num_entries; i++) {
		mef_entry = (struct mwifiex_fw_mef_entry *)pos;
		mef_entry->mode = mef->mef_entry[i].mode;
		mef_entry->action = mef->mef_entry[i].action;
		pos += sizeof(*mef_cfg->mef_entry);

		if (mwifiex_cmd_append_rpn_expression(priv,
						      &mef->mef_entry[i], &pos))
			return -1;

		mef_entry->exprsize =
			cpu_to_le16(pos - mef_entry->expr);
	}
	cmd->size = cpu_to_le16((u16) (pos - (u8 *)mef_cfg) + S_DS_GEN);

	return 0;
}

/* This function parse cal data from ASCII to hex */
static u32 mwifiex_parse_cal_cfg(u8 *src, size_t len, u8 *dst)
{
	u8 *s = src, *d = dst;

	while (s - src < len) {
		if (*s && (isspace(*s) || *s == '\t')) {
			s++;
			continue;
		}
		if (isxdigit(*s)) {
			*d++ = simple_strtol(s, NULL, 16);
			s += 2;
		} else {
			s++;
		}
	}

	return d - dst;
}

int mwifiex_dnld_dt_cfgdata(struct mwifiex_private *priv,
			    struct device_node *node, const char *prefix)
{
#ifdef CONFIG_OF
	struct property *prop;
	size_t len = strlen(prefix);
	int ret;

	/* look for all matching property names */
	for_each_property_of_node(node, prop) {
		if (len > strlen(prop->name) ||
		    strncmp(prop->name, prefix, len))
			continue;

		/* property header is 6 bytes, data must fit in cmd buffer */
		if (prop->value && prop->length > 6 &&
		    prop->length <= MWIFIEX_SIZE_OF_CMD_BUFFER - S_DS_GEN) {
			ret = mwifiex_send_cmd(priv, HostCmd_CMD_CFG_DATA,
					       HostCmd_ACT_GEN_SET, 0,
					       prop, true);
			if (ret)
				return ret;
		}
	}
#endif
	return 0;
}

/* This function prepares command of set_cfg_data. */
static int mwifiex_cmd_cfg_data(struct mwifiex_private *priv,
				struct host_cmd_ds_command *cmd, void *data_buf)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct property *prop = data_buf;
	u32 len;
	u8 *data = (u8 *)cmd + S_DS_GEN;
	int ret;

	if (prop) {
		len = prop->length;
		ret = of_property_read_u8_array(adapter->dt_node, prop->name,
						data, len);
		if (ret)
			return ret;
		mwifiex_dbg(adapter, INFO,
			    "download cfg_data from device tree: %s\n",
			    prop->name);
	} else if (adapter->cal_data->data && adapter->cal_data->size > 0) {
		len = mwifiex_parse_cal_cfg((u8 *)adapter->cal_data->data,
					    adapter->cal_data->size, data);
		mwifiex_dbg(adapter, INFO,
			    "download cfg_data from config file\n");
	} else {
		return -1;
	}

	cmd->command = cpu_to_le16(HostCmd_CMD_CFG_DATA);
	cmd->size = cpu_to_le16(S_DS_GEN + len);

	return 0;
}

static int
mwifiex_cmd_set_mc_policy(struct mwifiex_private *priv,
			  struct host_cmd_ds_command *cmd,
			  u16 cmd_action, void *data_buf)
{
	struct host_cmd_ds_multi_chan_policy *mc_pol = &cmd->params.mc_policy;
	const u16 *drcs_info = data_buf;

	mc_pol->action = cpu_to_le16(cmd_action);
	mc_pol->policy = cpu_to_le16(*drcs_info);
	cmd->command = cpu_to_le16(HostCmd_CMD_MC_POLICY);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_multi_chan_policy) +
				S_DS_GEN);
	return 0;
}

static int mwifiex_cmd_robust_coex(struct mwifiex_private *priv,
				   struct host_cmd_ds_command *cmd,
				   u16 cmd_action, bool *is_timeshare)
{
	struct host_cmd_ds_robust_coex *coex = &cmd->params.coex;
	struct mwifiex_ie_types_robust_coex *coex_tlv;

	cmd->command = cpu_to_le16(HostCmd_CMD_ROBUST_COEX);
	cmd->size = cpu_to_le16(sizeof(*coex) + sizeof(*coex_tlv) + S_DS_GEN);

	coex->action = cpu_to_le16(cmd_action);
	coex_tlv = (struct mwifiex_ie_types_robust_coex *)
				((u8 *)coex + sizeof(*coex));
	coex_tlv->header.type = cpu_to_le16(TLV_TYPE_ROBUST_COEX);
	coex_tlv->header.len = cpu_to_le16(sizeof(coex_tlv->mode));

	if (coex->action == HostCmd_ACT_GEN_GET)
		return 0;

	if (*is_timeshare)
		coex_tlv->mode = cpu_to_le32(MWIFIEX_COEX_MODE_TIMESHARE);
	else
		coex_tlv->mode = cpu_to_le32(MWIFIEX_COEX_MODE_SPATIAL);

	return 0;
}

static int mwifiex_cmd_gtk_rekey_offload(struct mwifiex_private *priv,
					 struct host_cmd_ds_command *cmd,
					 u16 cmd_action,
					 struct cfg80211_gtk_rekey_data *data)
{
	struct host_cmd_ds_gtk_rekey_params *rekey = &cmd->params.rekey;
	u64 rekey_ctr;

	cmd->command = cpu_to_le16(HostCmd_CMD_GTK_REKEY_OFFLOAD_CFG);
	cmd->size = cpu_to_le16(sizeof(*rekey) + S_DS_GEN);

	rekey->action = cpu_to_le16(cmd_action);
	if (cmd_action == HostCmd_ACT_GEN_SET) {
		memcpy(rekey->kek, data->kek, NL80211_KEK_LEN);
		memcpy(rekey->kck, data->kck, NL80211_KCK_LEN);
		rekey_ctr = be64_to_cpup((__be64 *)data->replay_ctr);
		rekey->replay_ctr_low = cpu_to_le32((u32)rekey_ctr);
		rekey->replay_ctr_high =
			cpu_to_le32((u32)((u64)rekey_ctr >> 32));
	}

	return 0;
}

static int mwifiex_cmd_chan_region_cfg(struct mwifiex_private *priv,
				       struct host_cmd_ds_command *cmd,
				       u16 cmd_action)
{
	struct host_cmd_ds_chan_region_cfg *reg = &cmd->params.reg_cfg;

	cmd->command = cpu_to_le16(HostCmd_CMD_CHAN_REGION_CFG);
	cmd->size = cpu_to_le16(sizeof(*reg) + S_DS_GEN);

	if (cmd_action == HostCmd_ACT_GEN_GET)
		reg->action = cpu_to_le16(cmd_action);

	return 0;
}

static int
mwifiex_cmd_coalesce_cfg(struct mwifiex_private *priv,
			 struct host_cmd_ds_command *cmd,
			 u16 cmd_action, void *data_buf)
{
	struct host_cmd_ds_coalesce_cfg *coalesce_cfg =
						&cmd->params.coalesce_cfg;
	struct mwifiex_ds_coalesce_cfg *cfg = data_buf;
	struct coalesce_filt_field_param *param;
	u16 cnt, idx, length;
	struct coalesce_receive_filt_rule *rule;

	cmd->command = cpu_to_le16(HostCmd_CMD_COALESCE_CFG);
	cmd->size = cpu_to_le16(S_DS_GEN);

	coalesce_cfg->action = cpu_to_le16(cmd_action);
	coalesce_cfg->num_of_rules = cpu_to_le16(cfg->num_of_rules);
	rule = coalesce_cfg->rule;

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

		/* Total rule length is sizeof max_coalescing_delay(u16),
		 * num_of_fields(u8), pkt_type(u8) and total length of the all
		 * params
		 */
		rule->header.len = cpu_to_le16(length + sizeof(u16) +
					       sizeof(u8) + sizeof(u8));

		/* Add the rule length to the command size*/
		le16_add_cpu(&cmd->size, le16_to_cpu(rule->header.len) +
			     sizeof(struct mwifiex_ie_types_header));

		rule = (void *)((u8 *)rule->params + length);
	}

	/* Add sizeof action, num_of_rules to total command length */
	le16_add_cpu(&cmd->size, sizeof(u16) + sizeof(u16));

	return 0;
}

static int
mwifiex_cmd_tdls_config(struct mwifiex_private *priv,
			struct host_cmd_ds_command *cmd,
			u16 cmd_action, void *data_buf)
{
	struct host_cmd_ds_tdls_config *tdls_config = &cmd->params.tdls_config;
	struct mwifiex_tdls_init_cs_params *config;
	struct mwifiex_tdls_config *init_config;
	u16 len;

	cmd->command = cpu_to_le16(HostCmd_CMD_TDLS_CONFIG);
	cmd->size = cpu_to_le16(S_DS_GEN);
	tdls_config->tdls_action = cpu_to_le16(cmd_action);
	le16_add_cpu(&cmd->size, sizeof(tdls_config->tdls_action));

	switch (cmd_action) {
	case ACT_TDLS_CS_ENABLE_CONFIG:
		init_config = data_buf;
		len = sizeof(*init_config);
		memcpy(tdls_config->tdls_data, init_config, len);
		break;
	case ACT_TDLS_CS_INIT:
		config = data_buf;
		len = sizeof(*config);
		memcpy(tdls_config->tdls_data, config, len);
		break;
	case ACT_TDLS_CS_STOP:
		len = sizeof(struct mwifiex_tdls_stop_cs_params);
		memcpy(tdls_config->tdls_data, data_buf, len);
		break;
	case ACT_TDLS_CS_PARAMS:
		len = sizeof(struct mwifiex_tdls_config_cs_params);
		memcpy(tdls_config->tdls_data, data_buf, len);
		break;
	default:
		mwifiex_dbg(priv->adapter, ERROR,
			    "Unknown TDLS configuration\n");
		return -ENOTSUPP;
	}

	le16_add_cpu(&cmd->size, len);
	return 0;
}

static int
mwifiex_cmd_tdls_oper(struct mwifiex_private *priv,
		      struct host_cmd_ds_command *cmd,
		      void *data_buf)
{
	struct host_cmd_ds_tdls_oper *tdls_oper = &cmd->params.tdls_oper;
	struct mwifiex_ds_tdls_oper *oper = data_buf;
	struct mwifiex_sta_node *sta_ptr;
	struct host_cmd_tlv_rates *tlv_rates;
	struct mwifiex_ie_types_htcap *ht_capab;
	struct mwifiex_ie_types_qos_info *wmm_qos_info;
	struct mwifiex_ie_types_extcap *extcap;
	struct mwifiex_ie_types_vhtcap *vht_capab;
	struct mwifiex_ie_types_aid *aid;
	struct mwifiex_ie_types_tdls_idle_timeout *timeout;
	u8 *pos, qos_info;
	u16 config_len = 0;
	struct station_parameters *params = priv->sta_params;

	cmd->command = cpu_to_le16(HostCmd_CMD_TDLS_OPER);
	cmd->size = cpu_to_le16(S_DS_GEN);
	le16_add_cpu(&cmd->size, sizeof(struct host_cmd_ds_tdls_oper));

	tdls_oper->reason = 0;
	memcpy(tdls_oper->peer_mac, oper->peer_mac, ETH_ALEN);
	sta_ptr = mwifiex_get_sta_entry(priv, oper->peer_mac);

	pos = (u8 *)tdls_oper + sizeof(struct host_cmd_ds_tdls_oper);

	switch (oper->tdls_action) {
	case MWIFIEX_TDLS_DISABLE_LINK:
		tdls_oper->tdls_action = cpu_to_le16(ACT_TDLS_DELETE);
		break;
	case MWIFIEX_TDLS_CREATE_LINK:
		tdls_oper->tdls_action = cpu_to_le16(ACT_TDLS_CREATE);
		break;
	case MWIFIEX_TDLS_CONFIG_LINK:
		tdls_oper->tdls_action = cpu_to_le16(ACT_TDLS_CONFIG);

		if (!params) {
			mwifiex_dbg(priv->adapter, ERROR,
				    "TDLS config params not available for %pM\n",
				    oper->peer_mac);
			return -ENODATA;
		}

		*(__le16 *)pos = cpu_to_le16(params->capability);
		config_len += sizeof(params->capability);

		qos_info = params->uapsd_queues | (params->max_sp << 5);
		wmm_qos_info = (struct mwifiex_ie_types_qos_info *)(pos +
								    config_len);
		wmm_qos_info->header.type = cpu_to_le16(WLAN_EID_QOS_CAPA);
		wmm_qos_info->header.len = cpu_to_le16(sizeof(qos_info));
		wmm_qos_info->qos_info = qos_info;
		config_len += sizeof(struct mwifiex_ie_types_qos_info);

		if (params->ht_capa) {
			ht_capab = (struct mwifiex_ie_types_htcap *)(pos +
								    config_len);
			ht_capab->header.type =
					    cpu_to_le16(WLAN_EID_HT_CAPABILITY);
			ht_capab->header.len =
				   cpu_to_le16(sizeof(struct ieee80211_ht_cap));
			memcpy(&ht_capab->ht_cap, params->ht_capa,
			       sizeof(struct ieee80211_ht_cap));
			config_len += sizeof(struct mwifiex_ie_types_htcap);
		}

		if (params->supported_rates && params->supported_rates_len) {
			tlv_rates = (struct host_cmd_tlv_rates *)(pos +
								  config_len);
			tlv_rates->header.type =
					       cpu_to_le16(WLAN_EID_SUPP_RATES);
			tlv_rates->header.len =
				       cpu_to_le16(params->supported_rates_len);
			memcpy(tlv_rates->rates, params->supported_rates,
			       params->supported_rates_len);
			config_len += sizeof(struct host_cmd_tlv_rates) +
				      params->supported_rates_len;
		}

		if (params->ext_capab && params->ext_capab_len) {
			extcap = (struct mwifiex_ie_types_extcap *)(pos +
								    config_len);
			extcap->header.type =
					   cpu_to_le16(WLAN_EID_EXT_CAPABILITY);
			extcap->header.len = cpu_to_le16(params->ext_capab_len);
			memcpy(extcap->ext_capab, params->ext_capab,
			       params->ext_capab_len);
			config_len += sizeof(struct mwifiex_ie_types_extcap) +
				      params->ext_capab_len;
		}
		if (params->vht_capa) {
			vht_capab = (struct mwifiex_ie_types_vhtcap *)(pos +
								    config_len);
			vht_capab->header.type =
					   cpu_to_le16(WLAN_EID_VHT_CAPABILITY);
			vht_capab->header.len =
				  cpu_to_le16(sizeof(struct ieee80211_vht_cap));
			memcpy(&vht_capab->vht_cap, params->vht_capa,
			       sizeof(struct ieee80211_vht_cap));
			config_len += sizeof(struct mwifiex_ie_types_vhtcap);
		}
		if (params->aid) {
			aid = (struct mwifiex_ie_types_aid *)(pos + config_len);
			aid->header.type = cpu_to_le16(WLAN_EID_AID);
			aid->header.len = cpu_to_le16(sizeof(params->aid));
			aid->aid = cpu_to_le16(params->aid);
			config_len += sizeof(struct mwifiex_ie_types_aid);
		}

		timeout = (void *)(pos + config_len);
		timeout->header.type = cpu_to_le16(TLV_TYPE_TDLS_IDLE_TIMEOUT);
		timeout->header.len = cpu_to_le16(sizeof(timeout->value));
		timeout->value = cpu_to_le16(MWIFIEX_TDLS_IDLE_TIMEOUT_IN_SEC);
		config_len += sizeof(struct mwifiex_ie_types_tdls_idle_timeout);

		break;
	default:
		mwifiex_dbg(priv->adapter, ERROR, "Unknown TDLS operation\n");
		return -ENOTSUPP;
	}

	le16_add_cpu(&cmd->size, config_len);

	return 0;
}

/* This function prepares command of sdio rx aggr info. */
static int mwifiex_cmd_sdio_rx_aggr_cfg(struct host_cmd_ds_command *cmd,
					u16 cmd_action, void *data_buf)
{
	struct host_cmd_sdio_sp_rx_aggr_cfg *cfg =
					&cmd->params.sdio_rx_aggr_cfg;

	cmd->command = cpu_to_le16(HostCmd_CMD_SDIO_SP_RX_AGGR_CFG);
	cmd->size =
		cpu_to_le16(sizeof(struct host_cmd_sdio_sp_rx_aggr_cfg) +
			    S_DS_GEN);
	cfg->action = cmd_action;
	if (cmd_action == HostCmd_ACT_GEN_SET)
		cfg->enable = *(u8 *)data_buf;

	return 0;
}

/* This function prepares command to get HS wakeup reason.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_get_wakeup_reason(struct mwifiex_private *priv,
					 struct host_cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(HostCmd_CMD_HS_WAKEUP_REASON);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_wakeup_reason) +
				S_DS_GEN);

	return 0;
}

/*
 * This function prepares the commands before sending them to the firmware.
 *
 * This is a generic function which calls specific command preparation
 * routines based upon the command number.
 */
int mwifiex_sta_prepare_cmd(struct mwifiex_private *priv, uint16_t cmd_no,
			    u16 cmd_action, u32 cmd_oid,
			    void *data_buf, void *cmd_buf)
{
	struct host_cmd_ds_command *cmd_ptr = cmd_buf;
	int ret = 0;

	/* Prepare command */
	switch (cmd_no) {
	case HostCmd_CMD_GET_HW_SPEC:
		ret = mwifiex_cmd_get_hw_spec(priv, cmd_ptr);
		break;
	case HostCmd_CMD_CFG_DATA:
		ret = mwifiex_cmd_cfg_data(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_MAC_CONTROL:
		ret = mwifiex_cmd_mac_control(priv, cmd_ptr, cmd_action,
					      data_buf);
		break;
	case HostCmd_CMD_802_11_MAC_ADDRESS:
		ret = mwifiex_cmd_802_11_mac_address(priv, cmd_ptr,
						     cmd_action);
		break;
	case HostCmd_CMD_MAC_MULTICAST_ADR:
		ret = mwifiex_cmd_mac_multicast_adr(cmd_ptr, cmd_action,
						    data_buf);
		break;
	case HostCmd_CMD_TX_RATE_CFG:
		ret = mwifiex_cmd_tx_rate_cfg(priv, cmd_ptr, cmd_action,
					      data_buf);
		break;
	case HostCmd_CMD_TXPWR_CFG:
		ret = mwifiex_cmd_tx_power_cfg(cmd_ptr, cmd_action,
					       data_buf);
		break;
	case HostCmd_CMD_RF_TX_PWR:
		ret = mwifiex_cmd_rf_tx_power(priv, cmd_ptr, cmd_action,
					      data_buf);
		break;
	case HostCmd_CMD_RF_ANTENNA:
		ret = mwifiex_cmd_rf_antenna(priv, cmd_ptr, cmd_action,
					     data_buf);
		break;
	case HostCmd_CMD_802_11_PS_MODE_ENH:
		ret = mwifiex_cmd_enh_power_mode(priv, cmd_ptr, cmd_action,
						 (uint16_t)cmd_oid, data_buf);
		break;
	case HostCmd_CMD_802_11_HS_CFG_ENH:
		ret = mwifiex_cmd_802_11_hs_cfg(priv, cmd_ptr, cmd_action,
				(struct mwifiex_hs_config_param *) data_buf);
		break;
	case HostCmd_CMD_802_11_SCAN:
		ret = mwifiex_cmd_802_11_scan(cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_802_11_BG_SCAN_CONFIG:
		ret = mwifiex_cmd_802_11_bg_scan_config(priv, cmd_ptr,
							data_buf);
		break;
	case HostCmd_CMD_802_11_BG_SCAN_QUERY:
		ret = mwifiex_cmd_802_11_bg_scan_query(cmd_ptr);
		break;
	case HostCmd_CMD_802_11_ASSOCIATE:
		ret = mwifiex_cmd_802_11_associate(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_802_11_DEAUTHENTICATE:
		ret = mwifiex_cmd_802_11_deauthenticate(priv, cmd_ptr,
							data_buf);
		break;
	case HostCmd_CMD_802_11_AD_HOC_START:
		ret = mwifiex_cmd_802_11_ad_hoc_start(priv, cmd_ptr,
						      data_buf);
		break;
	case HostCmd_CMD_802_11_GET_LOG:
		ret = mwifiex_cmd_802_11_get_log(cmd_ptr);
		break;
	case HostCmd_CMD_802_11_AD_HOC_JOIN:
		ret = mwifiex_cmd_802_11_ad_hoc_join(priv, cmd_ptr,
						     data_buf);
		break;
	case HostCmd_CMD_802_11_AD_HOC_STOP:
		ret = mwifiex_cmd_802_11_ad_hoc_stop(cmd_ptr);
		break;
	case HostCmd_CMD_RSSI_INFO:
		ret = mwifiex_cmd_802_11_rssi_info(priv, cmd_ptr, cmd_action);
		break;
	case HostCmd_CMD_802_11_SNMP_MIB:
		ret = mwifiex_cmd_802_11_snmp_mib(priv, cmd_ptr, cmd_action,
						  cmd_oid, data_buf);
		break;
	case HostCmd_CMD_802_11_TX_RATE_QUERY:
		cmd_ptr->command =
			cpu_to_le16(HostCmd_CMD_802_11_TX_RATE_QUERY);
		cmd_ptr->size =
			cpu_to_le16(sizeof(struct host_cmd_ds_tx_rate_query) +
				    S_DS_GEN);
		priv->tx_rate = 0;
		ret = 0;
		break;
	case HostCmd_CMD_VERSION_EXT:
		cmd_ptr->command = cpu_to_le16(cmd_no);
		cmd_ptr->params.verext.version_str_sel =
			(u8) (*((u32 *) data_buf));
		memcpy(&cmd_ptr->params, data_buf,
		       sizeof(struct host_cmd_ds_version_ext));
		cmd_ptr->size =
			cpu_to_le16(sizeof(struct host_cmd_ds_version_ext) +
				    S_DS_GEN);
		ret = 0;
		break;
	case HostCmd_CMD_MGMT_FRAME_REG:
		cmd_ptr->command = cpu_to_le16(cmd_no);
		cmd_ptr->params.reg_mask.action = cpu_to_le16(cmd_action);
		cmd_ptr->params.reg_mask.mask = cpu_to_le32(*(u32 *)data_buf);
		cmd_ptr->size =
			cpu_to_le16(sizeof(struct host_cmd_ds_mgmt_frame_reg) +
				    S_DS_GEN);
		ret = 0;
		break;
	case HostCmd_CMD_REMAIN_ON_CHAN:
		cmd_ptr->command = cpu_to_le16(cmd_no);
		memcpy(&cmd_ptr->params, data_buf,
		       sizeof(struct host_cmd_ds_remain_on_chan));
		cmd_ptr->size =
		      cpu_to_le16(sizeof(struct host_cmd_ds_remain_on_chan) +
				  S_DS_GEN);
		break;
	case HostCmd_CMD_11AC_CFG:
		ret = mwifiex_cmd_11ac_cfg(priv, cmd_ptr, cmd_action, data_buf);
		break;
	case HostCmd_CMD_P2P_MODE_CFG:
		cmd_ptr->command = cpu_to_le16(cmd_no);
		cmd_ptr->params.mode_cfg.action = cpu_to_le16(cmd_action);
		cmd_ptr->params.mode_cfg.mode = cpu_to_le16(*(u16 *)data_buf);
		cmd_ptr->size =
			cpu_to_le16(sizeof(struct host_cmd_ds_p2p_mode_cfg) +
				    S_DS_GEN);
		break;
	case HostCmd_CMD_FUNC_INIT:
		if (priv->adapter->hw_status == MWIFIEX_HW_STATUS_RESET)
			priv->adapter->hw_status = MWIFIEX_HW_STATUS_READY;
		cmd_ptr->command = cpu_to_le16(cmd_no);
		cmd_ptr->size = cpu_to_le16(S_DS_GEN);
		break;
	case HostCmd_CMD_FUNC_SHUTDOWN:
		priv->adapter->hw_status = MWIFIEX_HW_STATUS_RESET;
		cmd_ptr->command = cpu_to_le16(cmd_no);
		cmd_ptr->size = cpu_to_le16(S_DS_GEN);
		break;
	case HostCmd_CMD_11N_ADDBA_REQ:
		ret = mwifiex_cmd_11n_addba_req(cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_11N_DELBA:
		ret = mwifiex_cmd_11n_delba(cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_11N_ADDBA_RSP:
		ret = mwifiex_cmd_11n_addba_rsp_gen(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_802_11_KEY_MATERIAL:
		ret = mwifiex_cmd_802_11_key_material(priv, cmd_ptr,
						      cmd_action, cmd_oid,
						      data_buf);
		break;
	case HostCmd_CMD_802_11D_DOMAIN_INFO:
		ret = mwifiex_cmd_802_11d_domain_info(priv, cmd_ptr,
						      cmd_action);
		break;
	case HostCmd_CMD_RECONFIGURE_TX_BUFF:
		ret = mwifiex_cmd_recfg_tx_buf(priv, cmd_ptr, cmd_action,
					       data_buf);
		break;
	case HostCmd_CMD_AMSDU_AGGR_CTRL:
		ret = mwifiex_cmd_amsdu_aggr_ctrl(cmd_ptr, cmd_action,
						  data_buf);
		break;
	case HostCmd_CMD_11N_CFG:
		ret = mwifiex_cmd_11n_cfg(priv, cmd_ptr, cmd_action, data_buf);
		break;
	case HostCmd_CMD_WMM_GET_STATUS:
		mwifiex_dbg(priv->adapter, CMD,
			    "cmd: WMM: WMM_GET_STATUS cmd sent\n");
		cmd_ptr->command = cpu_to_le16(HostCmd_CMD_WMM_GET_STATUS);
		cmd_ptr->size =
			cpu_to_le16(sizeof(struct host_cmd_ds_wmm_get_status) +
				    S_DS_GEN);
		ret = 0;
		break;
	case HostCmd_CMD_802_11_IBSS_COALESCING_STATUS:
		ret = mwifiex_cmd_ibss_coalescing_status(cmd_ptr, cmd_action,
							 data_buf);
		break;
	case HostCmd_CMD_802_11_SCAN_EXT:
		ret = mwifiex_cmd_802_11_scan_ext(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_MEM_ACCESS:
		ret = mwifiex_cmd_mem_access(cmd_ptr, cmd_action, data_buf);
		break;
	case HostCmd_CMD_MAC_REG_ACCESS:
	case HostCmd_CMD_BBP_REG_ACCESS:
	case HostCmd_CMD_RF_REG_ACCESS:
	case HostCmd_CMD_PMIC_REG_ACCESS:
	case HostCmd_CMD_CAU_REG_ACCESS:
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
		ret = mwifiex_cmd_reg_access(cmd_ptr, cmd_action, data_buf);
		break;
	case HostCmd_CMD_SET_BSS_MODE:
		cmd_ptr->command = cpu_to_le16(cmd_no);
		if (priv->bss_mode == NL80211_IFTYPE_ADHOC)
			cmd_ptr->params.bss_mode.con_type =
				CONNECTION_TYPE_ADHOC;
		else if (priv->bss_mode == NL80211_IFTYPE_STATION ||
			 priv->bss_mode == NL80211_IFTYPE_P2P_CLIENT)
			cmd_ptr->params.bss_mode.con_type =
				CONNECTION_TYPE_INFRA;
		else if (priv->bss_mode == NL80211_IFTYPE_AP ||
			 priv->bss_mode == NL80211_IFTYPE_P2P_GO)
			cmd_ptr->params.bss_mode.con_type = CONNECTION_TYPE_AP;
		cmd_ptr->size = cpu_to_le16(sizeof(struct
				host_cmd_ds_set_bss_mode) + S_DS_GEN);
		ret = 0;
		break;
	case HostCmd_CMD_PCIE_DESC_DETAILS:
		ret = mwifiex_cmd_pcie_host_spec(priv, cmd_ptr, cmd_action);
		break;
	case HostCmd_CMD_802_11_SUBSCRIBE_EVENT:
		ret = mwifiex_cmd_802_11_subsc_evt(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_MEF_CFG:
		ret = mwifiex_cmd_mef_cfg(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_COALESCE_CFG:
		ret = mwifiex_cmd_coalesce_cfg(priv, cmd_ptr, cmd_action,
					       data_buf);
		break;
	case HostCmd_CMD_TDLS_OPER:
		ret = mwifiex_cmd_tdls_oper(priv, cmd_ptr, data_buf);
		break;
	case HostCmd_CMD_TDLS_CONFIG:
		ret = mwifiex_cmd_tdls_config(priv, cmd_ptr, cmd_action,
					      data_buf);
		break;
	case HostCmd_CMD_CHAN_REPORT_REQUEST:
		ret = mwifiex_cmd_issue_chan_report_request(priv, cmd_ptr,
							    data_buf);
		break;
	case HostCmd_CMD_SDIO_SP_RX_AGGR_CFG:
		ret = mwifiex_cmd_sdio_rx_aggr_cfg(cmd_ptr, cmd_action,
						   data_buf);
		break;
	case HostCmd_CMD_HS_WAKEUP_REASON:
		ret = mwifiex_cmd_get_wakeup_reason(priv, cmd_ptr);
		break;
	case HostCmd_CMD_MC_POLICY:
		ret = mwifiex_cmd_set_mc_policy(priv, cmd_ptr, cmd_action,
						data_buf);
		break;
	case HostCmd_CMD_ROBUST_COEX:
		ret = mwifiex_cmd_robust_coex(priv, cmd_ptr, cmd_action,
					      data_buf);
		break;
	case HostCmd_CMD_GTK_REKEY_OFFLOAD_CFG:
		ret = mwifiex_cmd_gtk_rekey_offload(priv, cmd_ptr, cmd_action,
						    data_buf);
		break;
	case HostCmd_CMD_CHAN_REGION_CFG:
		ret = mwifiex_cmd_chan_region_cfg(priv, cmd_ptr, cmd_action);
		break;
	default:
		mwifiex_dbg(priv->adapter, ERROR,
			    "PREP_CMD: unknown cmd- %#x\n", cmd_no);
		ret = -1;
		break;
	}
	return ret;
}

/*
 * This function issues commands to initialize firmware.
 *
 * This is called after firmware download to bring the card to
 * working state.
 * Function is also called during reinitialization of virtual
 * interfaces.
 *
 * The following commands are issued sequentially -
 *      - Set PCI-Express host buffer configuration (PCIE only)
 *      - Function init (for first interface only)
 *      - Read MAC address (for first interface only)
 *      - Reconfigure Tx buffer size (for first interface only)
 *      - Enable auto deep sleep (for first interface only)
 *      - Get Tx rate
 *      - Get Tx power
 *      - Set IBSS coalescing status
 *      - Set AMSDU aggregation control
 *      - Set 11d control
 *      - Set MAC control (this must be the last command to initialize firmware)
 */
int mwifiex_sta_init_cmd(struct mwifiex_private *priv, u8 first_sta, bool init)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	int ret;
	u16 enable = true;
	struct mwifiex_ds_11n_amsdu_aggr_ctrl amsdu_aggr_ctrl;
	struct mwifiex_ds_auto_ds auto_ds;
	enum state_11d_t state_11d;
	struct mwifiex_ds_11n_tx_cfg tx_cfg;
	u8 sdio_sp_rx_aggr_enable;
	int data;

	if (first_sta) {
		if (priv->adapter->iface_type == MWIFIEX_PCIE) {
			ret = mwifiex_send_cmd(priv,
					       HostCmd_CMD_PCIE_DESC_DETAILS,
					       HostCmd_ACT_GEN_SET, 0, NULL,
					       true);
			if (ret)
				return -1;
		}

		ret = mwifiex_send_cmd(priv, HostCmd_CMD_FUNC_INIT,
				       HostCmd_ACT_GEN_SET, 0, NULL, true);
		if (ret)
			return -1;

		/* Download calibration data to firmware.
		 * The cal-data can be read from device tree and/or
		 * a configuration file and downloaded to firmware.
		 */
		if (priv->adapter->iface_type == MWIFIEX_SDIO &&
		    adapter->dev->of_node) {
			adapter->dt_node = adapter->dev->of_node;
			if (of_property_read_u32(adapter->dt_node,
						 "marvell,wakeup-pin",
						 &data) == 0) {
				pr_debug("Wakeup pin = 0x%x\n", data);
				adapter->hs_cfg.gpio = data;
			}

			ret = mwifiex_dnld_dt_cfgdata(priv, adapter->dt_node,
						      "marvell,caldata");
			if (ret)
				return -1;
		}

		if (adapter->cal_data) {
			ret = mwifiex_send_cmd(priv, HostCmd_CMD_CFG_DATA,
					       HostCmd_ACT_GEN_SET, 0, NULL,
					       true);
			if (ret)
				return -1;
		}

		/* Read MAC address from HW */
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_GET_HW_SPEC,
				       HostCmd_ACT_GEN_GET, 0, NULL, true);
		if (ret)
			return -1;

		/** Set SDIO Single Port RX Aggr Info */
		if (priv->adapter->iface_type == MWIFIEX_SDIO &&
		    ISSUPP_SDIO_SPA_ENABLED(priv->adapter->fw_cap_info) &&
		    !priv->adapter->host_disable_sdio_rx_aggr) {
			sdio_sp_rx_aggr_enable = true;
			ret = mwifiex_send_cmd(priv,
					       HostCmd_CMD_SDIO_SP_RX_AGGR_CFG,
					       HostCmd_ACT_GEN_SET, 0,
					       &sdio_sp_rx_aggr_enable,
					       true);
			if (ret) {
				mwifiex_dbg(priv->adapter, ERROR,
					    "error while enabling SP aggregation..disable it");
				adapter->sdio_rx_aggr_enable = false;
			}
		}

		/* Reconfigure tx buf size */
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_RECONFIGURE_TX_BUFF,
				       HostCmd_ACT_GEN_SET, 0,
				       &priv->adapter->tx_buf_size, true);
		if (ret)
			return -1;

		if (priv->bss_type != MWIFIEX_BSS_TYPE_UAP) {
			/* Enable IEEE PS by default */
			priv->adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_PSP;
			ret = mwifiex_send_cmd(priv,
					       HostCmd_CMD_802_11_PS_MODE_ENH,
					       EN_AUTO_PS, BITMAP_STA_PS, NULL,
					       true);
			if (ret)
				return -1;
		}

		if (drcs) {
			adapter->drcs_enabled = true;
			if (ISSUPP_DRCS_ENABLED(adapter->fw_cap_info))
				ret = mwifiex_send_cmd(priv,
						       HostCmd_CMD_MC_POLICY,
						       HostCmd_ACT_GEN_SET, 0,
						       &adapter->drcs_enabled,
						       true);
			if (ret)
				return -1;
		}

		mwifiex_send_cmd(priv, HostCmd_CMD_CHAN_REGION_CFG,
				 HostCmd_ACT_GEN_GET, 0, NULL, true);
	}

	/* get tx rate */
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_TX_RATE_CFG,
			       HostCmd_ACT_GEN_GET, 0, NULL, true);
	if (ret)
		return -1;
	priv->data_rate = 0;

	/* get tx power */
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_RF_TX_PWR,
			       HostCmd_ACT_GEN_GET, 0, NULL, true);
	if (ret)
		return -1;

	if (priv->bss_type == MWIFIEX_BSS_TYPE_STA) {
		/* set ibss coalescing_status */
		ret = mwifiex_send_cmd(
				priv,
				HostCmd_CMD_802_11_IBSS_COALESCING_STATUS,
				HostCmd_ACT_GEN_SET, 0, &enable, true);
		if (ret)
			return -1;
	}

	memset(&amsdu_aggr_ctrl, 0, sizeof(amsdu_aggr_ctrl));
	amsdu_aggr_ctrl.enable = true;
	/* Send request to firmware */
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_AMSDU_AGGR_CTRL,
			       HostCmd_ACT_GEN_SET, 0,
			       &amsdu_aggr_ctrl, true);
	if (ret)
		return -1;
	/* MAC Control must be the last command in init_fw */
	/* set MAC Control */
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_MAC_CONTROL,
			       HostCmd_ACT_GEN_SET, 0,
			       &priv->curr_pkt_filter, true);
	if (ret)
		return -1;

	if (!disable_auto_ds &&
	    first_sta && priv->adapter->iface_type != MWIFIEX_USB &&
	    priv->bss_type != MWIFIEX_BSS_TYPE_UAP) {
		/* Enable auto deep sleep */
		auto_ds.auto_ds = DEEP_SLEEP_ON;
		auto_ds.idle_time = DEEP_SLEEP_IDLE_TIME;
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_802_11_PS_MODE_ENH,
				       EN_AUTO_PS, BITMAP_AUTO_DS,
				       &auto_ds, true);
		if (ret)
			return -1;
	}

	if (priv->bss_type != MWIFIEX_BSS_TYPE_UAP) {
		/* Send cmd to FW to enable/disable 11D function */
		state_11d = ENABLE_11D;
		ret = mwifiex_send_cmd(priv, HostCmd_CMD_802_11_SNMP_MIB,
				       HostCmd_ACT_GEN_SET, DOT11D_I,
				       &state_11d, true);
		if (ret)
			mwifiex_dbg(priv->adapter, ERROR,
				    "11D: failed to enable 11D\n");
	}

	/* Send cmd to FW to configure 11n specific configuration
	 * (Short GI, Channel BW, Green field support etc.) for transmit
	 */
	tx_cfg.tx_htcap = MWIFIEX_FW_DEF_HTTXCFG;
	ret = mwifiex_send_cmd(priv, HostCmd_CMD_11N_CFG,
			       HostCmd_ACT_GEN_SET, 0, &tx_cfg, true);

	if (init) {
		/* set last_init_cmd before sending the command */
		priv->adapter->last_init_cmd = HostCmd_CMD_11N_CFG;
		ret = -EINPROGRESS;
	}

	return ret;
}
