/*
 * Marvell Wireless LAN device driver: station command handling
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
		dev_err(priv->adapter->dev,
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
				       u32 *ul_temp)
{
	struct host_cmd_ds_802_11_snmp_mib *snmp_mib = &cmd->params.smib;

	dev_dbg(priv->adapter->dev, "cmd: SNMP_CMD: cmd_oid = 0x%x\n", cmd_oid);
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_SNMP_MIB);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_snmp_mib)
		- 1 + S_DS_GEN);

	if (cmd_action == HostCmd_ACT_GEN_GET) {
		snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_GET);
		snmp_mib->buf_size = cpu_to_le16(MAX_SNMP_BUF_SIZE);
		cmd->size = cpu_to_le16(le16_to_cpu(cmd->size)
			+ MAX_SNMP_BUF_SIZE);
	}

	switch (cmd_oid) {
	case FRAG_THRESH_I:
		snmp_mib->oid = cpu_to_le16((u16) FRAG_THRESH_I);
		if (cmd_action == HostCmd_ACT_GEN_SET) {
			snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_SET);
			snmp_mib->buf_size = cpu_to_le16(sizeof(u16));
			*((__le16 *) (snmp_mib->value)) =
				cpu_to_le16((u16) *ul_temp);
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size)
				+ sizeof(u16));
		}
		break;
	case RTS_THRESH_I:
		snmp_mib->oid = cpu_to_le16((u16) RTS_THRESH_I);
		if (cmd_action == HostCmd_ACT_GEN_SET) {
			snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_SET);
			snmp_mib->buf_size = cpu_to_le16(sizeof(u16));
			*(__le16 *) (snmp_mib->value) =
				cpu_to_le16((u16) *ul_temp);
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size)
				+ sizeof(u16));
		}
		break;

	case SHORT_RETRY_LIM_I:
		snmp_mib->oid = cpu_to_le16((u16) SHORT_RETRY_LIM_I);
		if (cmd_action == HostCmd_ACT_GEN_SET) {
			snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_SET);
			snmp_mib->buf_size = cpu_to_le16(sizeof(u16));
			*((__le16 *) (snmp_mib->value)) =
				cpu_to_le16((u16) *ul_temp);
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size)
				+ sizeof(u16));
		}
		break;
	case DOT11D_I:
		snmp_mib->oid = cpu_to_le16((u16) DOT11D_I);
		if (cmd_action == HostCmd_ACT_GEN_SET) {
			snmp_mib->query_type = cpu_to_le16(HostCmd_ACT_GEN_SET);
			snmp_mib->buf_size = cpu_to_le16(sizeof(u16));
			*((__le16 *) (snmp_mib->value)) =
				cpu_to_le16((u16) *ul_temp);
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size)
				+ sizeof(u16));
		}
		break;
	default:
		break;
	}
	dev_dbg(priv->adapter->dev,
		"cmd: SNMP_CMD: Action=0x%x, OID=0x%x, OIDSize=0x%x,"
		" Value=0x%x\n",
	       cmd_action, cmd_oid, le16_to_cpu(snmp_mib->buf_size),
	       le16_to_cpu(*(__le16 *) snmp_mib->value));
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
	rate_scope->length = cpu_to_le16(sizeof(struct mwifiex_rate_scope) -
			sizeof(struct mwifiex_ie_types_header));
	if (pbitmap_rates != NULL) {
		rate_scope->hr_dsss_rate_bitmap = cpu_to_le16(pbitmap_rates[0]);
		rate_scope->ofdm_rate_bitmap = cpu_to_le16(pbitmap_rates[1]);
		for (i = 0;
		     i < sizeof(rate_scope->ht_mcs_rate_bitmap) / sizeof(u16);
		     i++)
			rate_scope->ht_mcs_rate_bitmap[i] =
				cpu_to_le16(pbitmap_rates[2 + i]);
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
				pg_tlv->length);

			pg_tlv = (struct mwifiex_types_power_group *) ((u8 *)
				  cmd_txp_cfg +
				  sizeof(struct host_cmd_ds_txpwr_cfg));
			cmd->size = cpu_to_le16(le16_to_cpu(cmd->size) +
				  sizeof(struct mwifiex_types_power_group) +
				  pg_tlv->length);
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
	u16 hs_activate = false;

	if (!hscfg_param)
		/* New Activate command */
		hs_activate = true;
	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_HS_CFG_ENH);

	if (!hs_activate &&
	    (hscfg_param->conditions
	    != cpu_to_le32(HOST_SLEEP_CFG_CANCEL))
	    && ((adapter->arp_filter_size > 0)
		&& (adapter->arp_filter_size <= ARP_FILTER_MAX_BUF_SIZE))) {
		dev_dbg(adapter->dev,
			"cmd: Attach %d bytes ArpFilter to HSCfg cmd\n",
		       adapter->arp_filter_size);
		memcpy(((u8 *) hs_cfg) +
		       sizeof(struct host_cmd_ds_802_11_hs_cfg_enh),
		       adapter->arp_filter, adapter->arp_filter_size);
		cmd->size = cpu_to_le16(adapter->arp_filter_size +
				    sizeof(struct host_cmd_ds_802_11_hs_cfg_enh)
				    + S_DS_GEN);
	} else {
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof(struct
					   host_cmd_ds_802_11_hs_cfg_enh));
	}
	if (hs_activate) {
		hs_cfg->action = cpu_to_le16(HS_ACTIVATE);
		hs_cfg->params.hs_activate.resp_ctrl = RESP_NEEDED;
	} else {
		hs_cfg->action = cpu_to_le16(HS_CONFIGURE);
		hs_cfg->params.hs_config.conditions = hscfg_param->conditions;
		hs_cfg->params.hs_config.gpio = hscfg_param->gpio;
		hs_cfg->params.hs_config.gap = hscfg_param->gap;
		dev_dbg(adapter->dev,
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

	dev_dbg(priv->adapter->dev, "cmd: Deauth: %pM\n", deauth->mac_addr);

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
			dev_err(priv->adapter->dev,
				"key%d Length = %d is incorrect\n",
			       (i + 1), priv->wep_key[i].key_length);
			return -1;
		}
	}

	return 0;
}

/*
 * This function prepares command to set/get/reset network key(s).
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting WEP keys, WAPI keys or WPA keys along with required
 *        encryption (TKIP, AES) (as required)
 *      - Ensuring correct endian-ness
 */
static int
mwifiex_cmd_802_11_key_material(struct mwifiex_private *priv,
				struct host_cmd_ds_command *cmd,
				u16 cmd_action, u32 cmd_oid,
				struct mwifiex_ds_encrypt_key *enc_key)
{
	struct host_cmd_ds_802_11_key_material *key_material =
		&cmd->params.key_material;
	u16 key_param_len = 0;
	int ret = 0;
	const u8 bc_mac[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

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
		dev_dbg(priv->adapter->dev, "info: Set WAPI Key\n");
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

		if (0 != memcmp(enc_key->mac_addr, bc_mac, sizeof(bc_mac))) {
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
		       enc_key->wapi_rxpn, WAPI_RXPN_LEN);
		key_material->key_param_set.length =
			cpu_to_le16(WAPI_KEY_LEN + KEYPARAMSET_FIXED_LEN);

		key_param_len = (WAPI_KEY_LEN + KEYPARAMSET_FIXED_LEN) +
				 sizeof(struct mwifiex_ie_types_header);
		cmd->size = cpu_to_le16(key_param_len +
				sizeof(key_material->action) + S_DS_GEN);
		return ret;
	}
	if (enc_key->key_len == WLAN_KEY_LEN_CCMP) {
		dev_dbg(priv->adapter->dev, "cmd: WPA_AES\n");
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
		else		/* AES group key: multicast */
			key_material->key_param_set.key_info |=
				cpu_to_le16(KEY_MCAST);
	} else if (enc_key->key_len == WLAN_KEY_LEN_TKIP) {
		dev_dbg(priv->adapter->dev, "cmd: WPA_TKIP\n");
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

		key_param_len = (u16) (enc_key->key_len + KEYPARAMSET_FIXED_LEN)
				      + sizeof(struct mwifiex_ie_types_header);

		cmd->size = cpu_to_le16(key_param_len +
				    sizeof(key_material->action) + S_DS_GEN);
	}

	return ret;
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

	dev_dbg(adapter->dev, "info: 11D: no_of_triplet=0x%x\n", no_of_triplet);

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

	domain->header.len = cpu_to_le16((no_of_triplet *
				sizeof(struct ieee80211_country_ie_triplet)) +
				sizeof(domain->country_code));

	if (no_of_triplet) {
		memcpy(domain->triplet, adapter->domain_reg.triplet,
				no_of_triplet *
				sizeof(struct ieee80211_country_ie_triplet));

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
 * This function prepares command to set/get RF channel.
 *
 * Preparation includes -
 *      - Setting command ID, action and proper size
 *      - Setting RF type and current RF channel (for SET only)
 *      - Ensuring correct endian-ness
 */
static int mwifiex_cmd_802_11_rf_channel(struct mwifiex_private *priv,
					 struct host_cmd_ds_command *cmd,
					 u16 cmd_action, u16 *channel)
{
	struct host_cmd_ds_802_11_rf_channel *rf_chan =
		&cmd->params.rf_channel;
	uint16_t rf_type = le16_to_cpu(rf_chan->rf_type);

	cmd->command = cpu_to_le16(HostCmd_CMD_802_11_RF_CHANNEL);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_rf_channel)
				+ S_DS_GEN);

	if (cmd_action == HostCmd_ACT_GEN_SET) {
		if ((priv->adapter->adhoc_start_band & BAND_A)
		    || (priv->adapter->adhoc_start_band & BAND_AN))
			rf_chan->rf_type =
				cpu_to_le16(HostCmd_SCAN_RADIO_TYPE_A);

		rf_type = le16_to_cpu(rf_chan->rf_type);
		SET_SECONDARYCHAN(rf_type, priv->adapter->chan_offset);
		rf_chan->current_channel = cpu_to_le16(*channel);
	}
	rf_chan->action = cpu_to_le16(cmd_action);
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
		mac_reg = (struct host_cmd_ds_mac_reg_access *) &cmd->
			params.mac_reg;
		mac_reg->action = cpu_to_le16(cmd_action);
		mac_reg->offset =
			cpu_to_le16((u16) le32_to_cpu(reg_rw->offset));
		mac_reg->value = reg_rw->value;
		break;
	}
	case HostCmd_CMD_BBP_REG_ACCESS:
	{
		struct host_cmd_ds_bbp_reg_access *bbp_reg;

		cmd->size = cpu_to_le16(sizeof(*bbp_reg) + S_DS_GEN);
		bbp_reg = (struct host_cmd_ds_bbp_reg_access *) &cmd->
			params.bbp_reg;
		bbp_reg->action = cpu_to_le16(cmd_action);
		bbp_reg->offset =
			cpu_to_le16((u16) le32_to_cpu(reg_rw->offset));
		bbp_reg->value = (u8) le32_to_cpu(reg_rw->value);
		break;
	}
	case HostCmd_CMD_RF_REG_ACCESS:
	{
		struct host_cmd_ds_rf_reg_access *rf_reg;

		cmd->size = cpu_to_le16(sizeof(*rf_reg) + S_DS_GEN);
		rf_reg = (struct host_cmd_ds_rf_reg_access *) &cmd->
			params.rf_reg;
		rf_reg->action = cpu_to_le16(cmd_action);
		rf_reg->offset =
			cpu_to_le16((u16) le32_to_cpu(reg_rw->offset));
		rf_reg->value = (u8) le32_to_cpu(reg_rw->value);
		break;
	}
	case HostCmd_CMD_PMIC_REG_ACCESS:
	{
		struct host_cmd_ds_pmic_reg_access *pmic_reg;

		cmd->size = cpu_to_le16(sizeof(*pmic_reg) + S_DS_GEN);
		pmic_reg = (struct host_cmd_ds_pmic_reg_access *) &cmd->
				params.pmic_reg;
		pmic_reg->action = cpu_to_le16(cmd_action);
		pmic_reg->offset =
			cpu_to_le16((u16) le32_to_cpu(reg_rw->offset));
		pmic_reg->value = (u8) le32_to_cpu(reg_rw->value);
		break;
	}
	case HostCmd_CMD_CAU_REG_ACCESS:
	{
		struct host_cmd_ds_rf_reg_access *cau_reg;

		cmd->size = cpu_to_le16(sizeof(*cau_reg) + S_DS_GEN);
		cau_reg = (struct host_cmd_ds_rf_reg_access *) &cmd->
				params.rf_reg;
		cau_reg->action = cpu_to_le16(cmd_action);
		cau_reg->offset =
			cpu_to_le16((u16) le32_to_cpu(reg_rw->offset));
		cau_reg->value = (u8) le32_to_cpu(reg_rw->value);
		break;
	}
	case HostCmd_CMD_802_11_EEPROM_ACCESS:
	{
		struct mwifiex_ds_read_eeprom *rd_eeprom = data_buf;
		struct host_cmd_ds_802_11_eeprom_access *cmd_eeprom =
			(struct host_cmd_ds_802_11_eeprom_access *)
			&cmd->params.eeprom;

		cmd->size = cpu_to_le16(sizeof(*cmd_eeprom) + S_DS_GEN);
		cmd_eeprom->action = cpu_to_le16(cmd_action);
		cmd_eeprom->offset = rd_eeprom->offset;
		cmd_eeprom->byte_count = rd_eeprom->byte_count;
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
	phys_addr_t *buf_pa;

	cmd->command = cpu_to_le16(HostCmd_CMD_PCIE_DESC_DETAILS);
	cmd->size = cpu_to_le16(sizeof(struct
					host_cmd_ds_pcie_details) + S_DS_GEN);
	cmd->result = 0;

	memset(host_spec, 0, sizeof(struct host_cmd_ds_pcie_details));

	if (action == HostCmd_ACT_GEN_SET) {
		/* Send the ring base addresses and count to firmware */
		host_spec->txbd_addr_lo = (u32)(card->txbd_ring_pbase);
		host_spec->txbd_addr_hi =
				(u32)(((u64)card->txbd_ring_pbase)>>32);
		host_spec->txbd_count = MWIFIEX_MAX_TXRX_BD;
		host_spec->rxbd_addr_lo = (u32)(card->rxbd_ring_pbase);
		host_spec->rxbd_addr_hi =
				(u32)(((u64)card->rxbd_ring_pbase)>>32);
		host_spec->rxbd_count = MWIFIEX_MAX_TXRX_BD;
		host_spec->evtbd_addr_lo =
				(u32)(card->evtbd_ring_pbase);
		host_spec->evtbd_addr_hi =
				(u32)(((u64)card->evtbd_ring_pbase)>>32);
		host_spec->evtbd_count = MWIFIEX_MAX_EVT_BD;
		if (card->sleep_cookie) {
			buf_pa = MWIFIEX_SKB_PACB(card->sleep_cookie);
			host_spec->sleep_cookie_addr_lo = (u32) *buf_pa;
			host_spec->sleep_cookie_addr_hi =
						(u32) (((u64)*buf_pa) >> 32);
			dev_dbg(priv->adapter->dev, "sleep_cook_lo phy addr: "
				 "0x%x\n", host_spec->sleep_cookie_addr_lo);
		}
	}

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
	case HostCmd_CMD_802_11_RF_CHANNEL:
		ret = mwifiex_cmd_802_11_rf_channel(priv, cmd_ptr, cmd_action,
						    data_buf);
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
		ret = mwifiex_cmd_11n_cfg(cmd_ptr, cmd_action,
					  data_buf);
		break;
	case HostCmd_CMD_WMM_GET_STATUS:
		dev_dbg(priv->adapter->dev,
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
		else if (priv->bss_mode == NL80211_IFTYPE_STATION)
			cmd_ptr->params.bss_mode.con_type =
				CONNECTION_TYPE_INFRA;
		cmd_ptr->size = cpu_to_le16(sizeof(struct
				host_cmd_ds_set_bss_mode) + S_DS_GEN);
		ret = 0;
		break;
	case HostCmd_CMD_PCIE_DESC_DETAILS:
		ret = mwifiex_cmd_pcie_host_spec(priv, cmd_ptr, cmd_action);
		break;
	default:
		dev_err(priv->adapter->dev,
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
int mwifiex_sta_init_cmd(struct mwifiex_private *priv, u8 first_sta)
{
	int ret;
	u16 enable = true;
	struct mwifiex_ds_11n_amsdu_aggr_ctrl amsdu_aggr_ctrl;
	struct mwifiex_ds_auto_ds auto_ds;
	enum state_11d_t state_11d;
	struct mwifiex_ds_11n_tx_cfg tx_cfg;

	if (first_sta) {
		if (priv->adapter->iface_type == MWIFIEX_PCIE) {
			ret = mwifiex_send_cmd_async(priv,
					HostCmd_CMD_PCIE_DESC_DETAILS,
					HostCmd_ACT_GEN_SET, 0, NULL);
			if (ret)
				return -1;
		}

		ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_FUNC_INIT,
					     HostCmd_ACT_GEN_SET, 0, NULL);
		if (ret)
			return -1;
		/* Read MAC address from HW */
		ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_GET_HW_SPEC,
					     HostCmd_ACT_GEN_GET, 0, NULL);
		if (ret)
			return -1;

		/* Reconfigure tx buf size */
		ret = mwifiex_send_cmd_async(priv,
					     HostCmd_CMD_RECONFIGURE_TX_BUFF,
					     HostCmd_ACT_GEN_SET, 0,
					     &priv->adapter->tx_buf_size);
		if (ret)
			return -1;

		/* Enable IEEE PS by default */
		priv->adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_PSP;
		ret = mwifiex_send_cmd_async(priv,
					     HostCmd_CMD_802_11_PS_MODE_ENH,
					     EN_AUTO_PS, BITMAP_STA_PS, NULL);
		if (ret)
			return -1;
	}

	/* get tx rate */
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_TX_RATE_CFG,
				     HostCmd_ACT_GEN_GET, 0, NULL);
	if (ret)
		return -1;
	priv->data_rate = 0;

	/* get tx power */
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_TXPWR_CFG,
				     HostCmd_ACT_GEN_GET, 0, NULL);
	if (ret)
		return -1;

	/* set ibss coalescing_status */
	ret = mwifiex_send_cmd_async(priv,
				     HostCmd_CMD_802_11_IBSS_COALESCING_STATUS,
				     HostCmd_ACT_GEN_SET, 0, &enable);
	if (ret)
		return -1;

	memset(&amsdu_aggr_ctrl, 0, sizeof(amsdu_aggr_ctrl));
	amsdu_aggr_ctrl.enable = true;
	/* Send request to firmware */
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_AMSDU_AGGR_CTRL,
				     HostCmd_ACT_GEN_SET, 0,
				     &amsdu_aggr_ctrl);
	if (ret)
		return -1;
	/* MAC Control must be the last command in init_fw */
	/* set MAC Control */
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_MAC_CONTROL,
				     HostCmd_ACT_GEN_SET, 0,
				     &priv->curr_pkt_filter);
	if (ret)
		return -1;

	if (first_sta) {
		/* Enable auto deep sleep */
		auto_ds.auto_ds = DEEP_SLEEP_ON;
		auto_ds.idle_time = DEEP_SLEEP_IDLE_TIME;
		ret = mwifiex_send_cmd_async(priv,
					     HostCmd_CMD_802_11_PS_MODE_ENH,
					     EN_AUTO_PS, BITMAP_AUTO_DS,
					     &auto_ds);
		if (ret)
			return -1;
	}

	/* Send cmd to FW to enable/disable 11D function */
	state_11d = ENABLE_11D;
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_802_11_SNMP_MIB,
				     HostCmd_ACT_GEN_SET, DOT11D_I, &state_11d);
	if (ret)
		dev_err(priv->adapter->dev, "11D: failed to enable 11D\n");

	/* Send cmd to FW to configure 11n specific configuration
	 * (Short GI, Channel BW, Green field support etc.) for transmit
	 */
	tx_cfg.tx_htcap = MWIFIEX_FW_DEF_HTTXCFG;
	ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_11N_CFG,
				     HostCmd_ACT_GEN_SET, 0, &tx_cfg);

	/* set last_init_cmd */
	priv->adapter->last_init_cmd = HostCmd_CMD_11N_CFG;
	ret = -EINPROGRESS;

	return ret;
}
