/**
  * This file contains the handling of command.
  * It prepares command and sends it to firmware when it is ready.
  */

#include <net/iw_handler.h>
#include "host.h"
#include "hostcmd.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "join.h"
#include "wext.h"

static void cleanup_cmdnode(struct cmd_ctrl_node *ptempnode);

static u16 commands_allowed_in_ps[] = {
	cmd_802_11_rssi,
};

/**
 *  @brief This function checks if the commans is allowed
 *  in PS mode not.
 *
 *  @param command the command ID
 *  @return 	   TRUE or FALSE
 */
static u8 is_command_allowed_in_ps(__le16 command)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands_allowed_in_ps); i++) {
		if (command == cpu_to_le16(commands_allowed_in_ps[i]))
			return 1;
	}

	return 0;
}

static int wlan_cmd_hw_spec(wlan_private * priv, struct cmd_ds_command *cmd)
{
	struct cmd_ds_get_hw_spec *hwspec = &cmd->params.hwspec;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_get_hw_spec);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_get_hw_spec) + S_DS_GEN);
	memcpy(hwspec->permanentaddr, priv->adapter->current_addr, ETH_ALEN);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_ps_mode(wlan_private * priv,
				   struct cmd_ds_command *cmd,
				   u16 cmd_action)
{
	struct cmd_ds_802_11_ps_mode *psm = &cmd->params.psmode;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_802_11_ps_mode);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_ps_mode) +
				S_DS_GEN);
	psm->action = cpu_to_le16(cmd_action);
	psm->multipledtim = 0;
	switch (cmd_action) {
	case cmd_subcmd_enter_ps:
		lbs_deb_cmd("PS command:" "SubCode- Enter PS\n");
		lbs_deb_cmd("locallisteninterval = %d\n",
		       adapter->locallisteninterval);

		psm->locallisteninterval =
		    cpu_to_le16(adapter->locallisteninterval);
		psm->nullpktinterval =
		    cpu_to_le16(adapter->nullpktinterval);
		psm->multipledtim =
		    cpu_to_le16(priv->adapter->multipledtim);
		break;

	case cmd_subcmd_exit_ps:
		lbs_deb_cmd("PS command:" "SubCode- Exit PS\n");
		break;

	case cmd_subcmd_sleep_confirmed:
		lbs_deb_cmd("PS command: SubCode- sleep confirm\n");
		break;

	default:
		break;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_inactivity_timeout(wlan_private * priv,
					      struct cmd_ds_command *cmd,
					      u16 cmd_action, void *pdata_buf)
{
	u16 *timeout = pdata_buf;

	cmd->command = cpu_to_le16(cmd_802_11_inactivity_timeout);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_inactivity_timeout)
			     + S_DS_GEN);

	cmd->params.inactivity_timeout.action = cpu_to_le16(cmd_action);

	if (cmd_action)
		cmd->params.inactivity_timeout.timeout = cpu_to_le16(*timeout);
	else
		cmd->params.inactivity_timeout.timeout = 0;

	return 0;
}

static int wlan_cmd_802_11_sleep_params(wlan_private * priv,
					struct cmd_ds_command *cmd,
					u16 cmd_action)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_sleep_params *sp = &cmd->params.sleep_params;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size = cpu_to_le16((sizeof(struct cmd_ds_802_11_sleep_params)) +
				S_DS_GEN);
	cmd->command = cpu_to_le16(cmd_802_11_sleep_params);

	if (cmd_action == cmd_act_get) {
		memset(&adapter->sp, 0, sizeof(struct sleep_params));
		memset(sp, 0, sizeof(struct cmd_ds_802_11_sleep_params));
		sp->action = cpu_to_le16(cmd_action);
	} else if (cmd_action == cmd_act_set) {
		sp->action = cpu_to_le16(cmd_action);
		sp->error = cpu_to_le16(adapter->sp.sp_error);
		sp->offset = cpu_to_le16(adapter->sp.sp_offset);
		sp->stabletime = cpu_to_le16(adapter->sp.sp_stabletime);
		sp->calcontrol = (u8) adapter->sp.sp_calcontrol;
		sp->externalsleepclk = (u8) adapter->sp.sp_extsleepclk;
		sp->reserved = cpu_to_le16(adapter->sp.sp_reserved);
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_set_wep(wlan_private * priv,
                                   struct cmd_ds_command *cmd,
                                   u32 cmd_act,
                                   void * pdata_buf)
{
	struct cmd_ds_802_11_set_wep *wep = &cmd->params.wep;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct assoc_request * assoc_req = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_802_11_set_wep);
	cmd->size = cpu_to_le16(sizeof(*wep) + S_DS_GEN);

	if (cmd_act == cmd_act_add) {
		int i;

		if (!assoc_req) {
			lbs_deb_cmd("Invalid association request!");
			ret = -1;
			goto done;
		}

		wep->action = cpu_to_le16(cmd_act_add);

		/* default tx key index */
		wep->keyindex = cpu_to_le16((u16)(assoc_req->wep_tx_keyidx &
						  (u32)cmd_WEP_KEY_INDEX_MASK));

		lbs_deb_cmd("Tx key Index: %u\n", le16_to_cpu(wep->keyindex));

		/* Copy key types and material to host command structure */
		for (i = 0; i < 4; i++) {
			struct WLAN_802_11_KEY * pkey = &assoc_req->wep_keys[i];

			switch (pkey->len) {
			case KEY_LEN_WEP_40:
				wep->keytype[i] =
					cpu_to_le16(cmd_type_wep_40_bit);
				memmove(&wep->keymaterial[i], pkey->key,
				        pkey->len);
				break;
			case KEY_LEN_WEP_104:
				wep->keytype[i] =
					cpu_to_le16(cmd_type_wep_104_bit);
				memmove(&wep->keymaterial[i], pkey->key,
				        pkey->len);
				break;
			case 0:
				break;
			default:
				lbs_deb_cmd("Invalid WEP key %d length of %d\n",
				       i, pkey->len);
				ret = -1;
				goto done;
				break;
			}
		}
	} else if (cmd_act == cmd_act_remove) {
		/* ACT_REMOVE clears _all_ WEP keys */
		wep->action = cpu_to_le16(cmd_act_remove);

		/* default tx key index */
		wep->keyindex = cpu_to_le16((u16)(adapter->wep_tx_keyidx &
						  (u32)cmd_WEP_KEY_INDEX_MASK));
	}

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int wlan_cmd_802_11_enable_rsn(wlan_private * priv,
				      struct cmd_ds_command *cmd,
				      u16 cmd_action,
				      void * pdata_buf)
{
	struct cmd_ds_802_11_enable_rsn *penableRSN = &cmd->params.enbrsn;
	u32 * enable = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_802_11_enable_rsn);
	cmd->size = cpu_to_le16(sizeof(*penableRSN) + S_DS_GEN);
	penableRSN->action = cpu_to_le16(cmd_action);

	if (cmd_action == cmd_act_set) {
		if (*enable)
			penableRSN->enable = cpu_to_le16(cmd_enable_rsn);
		else
			penableRSN->enable = cpu_to_le16(cmd_disable_rsn);
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}


static void set_one_wpa_key(struct MrvlIEtype_keyParamSet * pkeyparamset,
                            struct WLAN_802_11_KEY * pkey)
{
	pkeyparamset->keytypeid = cpu_to_le16(pkey->type);

	if (pkey->flags & KEY_INFO_WPA_ENABLED) {
		pkeyparamset->keyinfo |= cpu_to_le16(KEY_INFO_WPA_ENABLED);
	}
	if (pkey->flags & KEY_INFO_WPA_UNICAST) {
		pkeyparamset->keyinfo |= cpu_to_le16(KEY_INFO_WPA_UNICAST);
	}
	if (pkey->flags & KEY_INFO_WPA_MCAST) {
		pkeyparamset->keyinfo |= cpu_to_le16(KEY_INFO_WPA_MCAST);
	}

	pkeyparamset->type = cpu_to_le16(TLV_TYPE_KEY_MATERIAL);
	pkeyparamset->keylen = cpu_to_le16(pkey->len);
	memcpy(pkeyparamset->key, pkey->key, pkey->len);
	pkeyparamset->length = cpu_to_le16(  sizeof(pkeyparamset->keytypeid)
	                                        + sizeof(pkeyparamset->keyinfo)
	                                        + sizeof(pkeyparamset->keylen)
	                                        + sizeof(pkeyparamset->key));
}

static int wlan_cmd_802_11_key_material(wlan_private * priv,
					struct cmd_ds_command *cmd,
					u16 cmd_action,
					u32 cmd_oid, void *pdata_buf)
{
	struct cmd_ds_802_11_key_material *pkeymaterial =
	    &cmd->params.keymaterial;
	struct assoc_request * assoc_req = pdata_buf;
	int ret = 0;
	int index = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_802_11_key_material);
	pkeymaterial->action = cpu_to_le16(cmd_action);

	if (cmd_action == cmd_act_get) {
		cmd->size = cpu_to_le16(S_DS_GEN + sizeof (pkeymaterial->action));
		ret = 0;
		goto done;
	}

	memset(&pkeymaterial->keyParamSet, 0, sizeof(pkeymaterial->keyParamSet));

	if (test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		set_one_wpa_key(&pkeymaterial->keyParamSet[index],
		                &assoc_req->wpa_unicast_key);
		index++;
	}

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		set_one_wpa_key(&pkeymaterial->keyParamSet[index],
		                &assoc_req->wpa_mcast_key);
		index++;
	}

	cmd->size = cpu_to_le16(  S_DS_GEN
	                        + sizeof (pkeymaterial->action)
	                        + (index * sizeof(struct MrvlIEtype_keyParamSet)));

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int wlan_cmd_802_11_reset(wlan_private * priv,
				 struct cmd_ds_command *cmd, int cmd_action)
{
	struct cmd_ds_802_11_reset *reset = &cmd->params.reset;

	cmd->command = cpu_to_le16(cmd_802_11_reset);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_reset) + S_DS_GEN);
	reset->action = cpu_to_le16(cmd_action);

	return 0;
}

static int wlan_cmd_802_11_get_log(wlan_private * priv,
				   struct cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(cmd_802_11_get_log);
	cmd->size =
		cpu_to_le16(sizeof(struct cmd_ds_802_11_get_log) + S_DS_GEN);

	return 0;
}

static int wlan_cmd_802_11_get_stat(wlan_private * priv,
				    struct cmd_ds_command *cmd)
{
	cmd->command = cpu_to_le16(cmd_802_11_get_stat);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_get_stat) + S_DS_GEN);

	return 0;
}

static int wlan_cmd_802_11_snmp_mib(wlan_private * priv,
				    struct cmd_ds_command *cmd,
				    int cmd_action,
				    int cmd_oid, void *pdata_buf)
{
	struct cmd_ds_802_11_snmp_mib *pSNMPMIB = &cmd->params.smib;
	wlan_adapter *adapter = priv->adapter;
	u8 ucTemp;

	lbs_deb_enter(LBS_DEB_CMD);

	lbs_deb_cmd("SNMP_CMD: cmd_oid = 0x%x\n", cmd_oid);

	cmd->command = cpu_to_le16(cmd_802_11_snmp_mib);
	cmd->size = cpu_to_le16(sizeof(*pSNMPMIB) + S_DS_GEN);

	switch (cmd_oid) {
	case OID_802_11_INFRASTRUCTURE_MODE:
	{
		u8 mode = (u8) (size_t) pdata_buf;
		pSNMPMIB->querytype = cpu_to_le16(cmd_act_set);
		pSNMPMIB->oid = cpu_to_le16((u16) desired_bsstype_i);
		pSNMPMIB->bufsize = sizeof(u8);
		if (mode == IW_MODE_ADHOC) {
			ucTemp = SNMP_MIB_VALUE_ADHOC;
		} else {
			/* Infra and Auto modes */
			ucTemp = SNMP_MIB_VALUE_INFRA;
		}

		memmove(pSNMPMIB->value, &ucTemp, sizeof(u8));

		break;
	}

	case OID_802_11D_ENABLE:
		{
			u32 ulTemp;

			pSNMPMIB->oid = cpu_to_le16((u16) dot11d_i);

			if (cmd_action == cmd_act_set) {
				pSNMPMIB->querytype = cmd_act_set;
				pSNMPMIB->bufsize = sizeof(u16);
				ulTemp = *(u32 *)pdata_buf;
				*((__le16 *)(pSNMPMIB->value)) =
				    cpu_to_le16((u16) ulTemp);
			}
			break;
		}

	case OID_802_11_FRAGMENTATION_THRESHOLD:
		{
			u32 ulTemp;

			pSNMPMIB->oid = cpu_to_le16((u16) fragthresh_i);

			if (cmd_action == cmd_act_get) {
				pSNMPMIB->querytype = cpu_to_le16(cmd_act_get);
			} else if (cmd_action == cmd_act_set) {
				pSNMPMIB->querytype = cpu_to_le16(cmd_act_set);
				pSNMPMIB->bufsize = cpu_to_le16(sizeof(u16));
				ulTemp = *((u32 *) pdata_buf);
				*((__le16 *)(pSNMPMIB->value)) =
				    cpu_to_le16((u16) ulTemp);

			}

			break;
		}

	case OID_802_11_RTS_THRESHOLD:
		{

			u32 ulTemp;
			pSNMPMIB->oid = le16_to_cpu((u16) rtsthresh_i);

			if (cmd_action == cmd_act_get) {
				pSNMPMIB->querytype = cpu_to_le16(cmd_act_get);
			} else if (cmd_action == cmd_act_set) {
				pSNMPMIB->querytype = cpu_to_le16(cmd_act_set);
				pSNMPMIB->bufsize = cpu_to_le16(sizeof(u16));
				ulTemp = *((u32 *)pdata_buf);
				*(__le16 *)(pSNMPMIB->value) =
				    cpu_to_le16((u16) ulTemp);

			}
			break;
		}
	case OID_802_11_TX_RETRYCOUNT:
		pSNMPMIB->oid = cpu_to_le16((u16) short_retrylim_i);

		if (cmd_action == cmd_act_get) {
			pSNMPMIB->querytype = cpu_to_le16(cmd_act_get);
		} else if (cmd_action == cmd_act_set) {
			pSNMPMIB->querytype = cpu_to_le16(cmd_act_set);
			pSNMPMIB->bufsize = cpu_to_le16(sizeof(u16));
			*((__le16 *)(pSNMPMIB->value)) =
			    cpu_to_le16((u16) adapter->txretrycount);
		}

		break;
	default:
		break;
	}

	lbs_deb_cmd(
	       "SNMP_CMD: command=0x%x, size=0x%x, seqnum=0x%x, result=0x%x\n",
	       le16_to_cpu(cmd->command), le16_to_cpu(cmd->size),
	       le16_to_cpu(cmd->seqnum), le16_to_cpu(cmd->result));

	lbs_deb_cmd(
	       "SNMP_CMD: action=0x%x, oid=0x%x, oidsize=0x%x, value=0x%x\n",
	       le16_to_cpu(pSNMPMIB->querytype), le16_to_cpu(pSNMPMIB->oid),
	       le16_to_cpu(pSNMPMIB->bufsize),
	       le16_to_cpu(*(__le16 *) pSNMPMIB->value));

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_radio_control(wlan_private * priv,
					 struct cmd_ds_command *cmd,
					 int cmd_action)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ds_802_11_radio_control *pradiocontrol = &cmd->params.radio;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size =
	    cpu_to_le16((sizeof(struct cmd_ds_802_11_radio_control)) +
			     S_DS_GEN);
	cmd->command = cpu_to_le16(cmd_802_11_radio_control);

	pradiocontrol->action = cpu_to_le16(cmd_action);

	switch (adapter->preamble) {
	case cmd_type_short_preamble:
		pradiocontrol->control = cpu_to_le16(SET_SHORT_PREAMBLE);
		break;

	case cmd_type_long_preamble:
		pradiocontrol->control = cpu_to_le16(SET_LONG_PREAMBLE);
		break;

	case cmd_type_auto_preamble:
	default:
		pradiocontrol->control = cpu_to_le16(SET_AUTO_PREAMBLE);
		break;
	}

	if (adapter->radioon)
		pradiocontrol->control |= cpu_to_le16(TURN_ON_RF);
	else
		pradiocontrol->control &= cpu_to_le16(~TURN_ON_RF);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_rf_tx_power(wlan_private * priv,
				       struct cmd_ds_command *cmd,
				       u16 cmd_action, void *pdata_buf)
{

	struct cmd_ds_802_11_rf_tx_power *prtp = &cmd->params.txp;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size =
	    cpu_to_le16((sizeof(struct cmd_ds_802_11_rf_tx_power)) + S_DS_GEN);
	cmd->command = cpu_to_le16(cmd_802_11_rf_tx_power);
	prtp->action = cpu_to_le16(cmd_action);

	lbs_deb_cmd("RF_TX_POWER_CMD: size:%d cmd:0x%x Act:%d\n",
		    le16_to_cpu(cmd->size), le16_to_cpu(cmd->command),
		    le16_to_cpu(prtp->action));

	switch (cmd_action) {
	case cmd_act_tx_power_opt_get:
		prtp->action = cpu_to_le16(cmd_act_get);
		prtp->currentlevel = 0;
		break;

	case cmd_act_tx_power_opt_set_high:
		prtp->action = cpu_to_le16(cmd_act_set);
		prtp->currentlevel = cpu_to_le16(cmd_act_tx_power_index_high);
		break;

	case cmd_act_tx_power_opt_set_mid:
		prtp->action = cpu_to_le16(cmd_act_set);
		prtp->currentlevel = cpu_to_le16(cmd_act_tx_power_index_mid);
		break;

	case cmd_act_tx_power_opt_set_low:
		prtp->action = cpu_to_le16(cmd_act_set);
		prtp->currentlevel = cpu_to_le16(*((u16 *) pdata_buf));
		break;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_rf_antenna(wlan_private * priv,
				      struct cmd_ds_command *cmd,
				      u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_802_11_rf_antenna *rant = &cmd->params.rant;

	cmd->command = cpu_to_le16(cmd_802_11_rf_antenna);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_rf_antenna) +
				S_DS_GEN);

	rant->action = cpu_to_le16(cmd_action);
	if ((cmd_action == cmd_act_set_rx) || (cmd_action == cmd_act_set_tx)) {
		rant->antennamode = cpu_to_le16((u16) (*(u32 *) pdata_buf));
	}

	return 0;
}

static int wlan_cmd_802_11_rate_adapt_rateset(wlan_private * priv,
					      struct cmd_ds_command *cmd,
					      u16 cmd_action)
{
	struct cmd_ds_802_11_rate_adapt_rateset
	*rateadapt = &cmd->params.rateset;
	wlan_adapter *adapter = priv->adapter;

	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_rate_adapt_rateset)
			     + S_DS_GEN);
	cmd->command = cpu_to_le16(cmd_802_11_rate_adapt_rateset);

	lbs_deb_enter(LBS_DEB_CMD);

	rateadapt->action = cpu_to_le16(cmd_action);
	rateadapt->enablehwauto = cpu_to_le16(adapter->enablehwauto);
	rateadapt->bitmap = cpu_to_le16(adapter->ratebitmap);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_data_rate(wlan_private * priv,
				     struct cmd_ds_command *cmd,
				     u16 cmd_action)
{
	struct cmd_ds_802_11_data_rate *pdatarate = &cmd->params.drate;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_data_rate) +
			     S_DS_GEN);

	cmd->command = cpu_to_le16(cmd_802_11_data_rate);

	memset(pdatarate, 0, sizeof(struct cmd_ds_802_11_data_rate));

	pdatarate->action = cpu_to_le16(cmd_action);

	if (cmd_action == cmd_act_set_tx_fix_rate) {
		pdatarate->datarate[0] = libertas_data_rate_to_index(adapter->datarate);
		lbs_deb_cmd("Setting FW for fixed rate 0x%02X\n",
		       adapter->datarate);
	} else if (cmd_action == cmd_act_set_tx_auto) {
		lbs_deb_cmd("Setting FW for AUTO rate\n");
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_mac_multicast_adr(wlan_private * priv,
				      struct cmd_ds_command *cmd,
				      u16 cmd_action)
{
	struct cmd_ds_mac_multicast_adr *pMCastAdr = &cmd->params.madr;
	wlan_adapter *adapter = priv->adapter;

	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_mac_multicast_adr) +
			     S_DS_GEN);
	cmd->command = cpu_to_le16(cmd_mac_multicast_adr);

	pMCastAdr->action = cpu_to_le16(cmd_action);
	pMCastAdr->nr_of_adrs =
	    cpu_to_le16((u16) adapter->nr_of_multicastmacaddr);
	memcpy(pMCastAdr->maclist, adapter->multicastlist,
	       adapter->nr_of_multicastmacaddr * ETH_ALEN);

	return 0;
}

static int wlan_cmd_802_11_rf_channel(wlan_private * priv,
				      struct cmd_ds_command *cmd,
				      int option, void *pdata_buf)
{
	struct cmd_ds_802_11_rf_channel *rfchan = &cmd->params.rfchannel;

	cmd->command = cpu_to_le16(cmd_802_11_rf_channel);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_rf_channel) +
				S_DS_GEN);

	if (option == cmd_opt_802_11_rf_channel_set) {
		rfchan->currentchannel = cpu_to_le16(*((u16 *) pdata_buf));
	}

	rfchan->action = cpu_to_le16(option);

	return 0;
}

static int wlan_cmd_802_11_rssi(wlan_private * priv,
				struct cmd_ds_command *cmd)
{
	wlan_adapter *adapter = priv->adapter;

	cmd->command = cpu_to_le16(cmd_802_11_rssi);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_rssi) + S_DS_GEN);
	cmd->params.rssi.N = cpu_to_le16(priv->adapter->bcn_avg_factor);

	/* reset Beacon SNR/NF/RSSI values */
	adapter->SNR[TYPE_BEACON][TYPE_NOAVG] = 0;
	adapter->SNR[TYPE_BEACON][TYPE_AVG] = 0;
	adapter->NF[TYPE_BEACON][TYPE_NOAVG] = 0;
	adapter->NF[TYPE_BEACON][TYPE_AVG] = 0;
	adapter->RSSI[TYPE_BEACON][TYPE_NOAVG] = 0;
	adapter->RSSI[TYPE_BEACON][TYPE_AVG] = 0;

	return 0;
}

static int wlan_cmd_reg_access(wlan_private * priv,
			       struct cmd_ds_command *cmdptr,
			       u8 cmd_action, void *pdata_buf)
{
	struct wlan_offset_value *offval;

	lbs_deb_enter(LBS_DEB_CMD);

	offval = (struct wlan_offset_value *)pdata_buf;

	switch (cmdptr->command) {
	case cmd_mac_reg_access:
		{
			struct cmd_ds_mac_reg_access *macreg;

			cmdptr->size =
			    cpu_to_le16(sizeof (struct cmd_ds_mac_reg_access)
					+ S_DS_GEN);
			macreg =
			    (struct cmd_ds_mac_reg_access *)&cmdptr->params.
			    macreg;

			macreg->action = cpu_to_le16(cmd_action);
			macreg->offset = cpu_to_le16((u16) offval->offset);
			macreg->value = cpu_to_le32(offval->value);

			break;
		}

	case cmd_bbp_reg_access:
		{
			struct cmd_ds_bbp_reg_access *bbpreg;

			cmdptr->size =
			    cpu_to_le16(sizeof
					     (struct cmd_ds_bbp_reg_access)
					     + S_DS_GEN);
			bbpreg =
			    (struct cmd_ds_bbp_reg_access *)&cmdptr->params.
			    bbpreg;

			bbpreg->action = cpu_to_le16(cmd_action);
			bbpreg->offset = cpu_to_le16((u16) offval->offset);
			bbpreg->value = (u8) offval->value;

			break;
		}

	case cmd_rf_reg_access:
		{
			struct cmd_ds_rf_reg_access *rfreg;

			cmdptr->size =
			    cpu_to_le16(sizeof
					     (struct cmd_ds_rf_reg_access) +
					     S_DS_GEN);
			rfreg =
			    (struct cmd_ds_rf_reg_access *)&cmdptr->params.
			    rfreg;

			rfreg->action = cpu_to_le16(cmd_action);
			rfreg->offset = cpu_to_le16((u16) offval->offset);
			rfreg->value = (u8) offval->value;

			break;
		}

	default:
		break;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int wlan_cmd_802_11_mac_address(wlan_private * priv,
				       struct cmd_ds_command *cmd,
				       u16 cmd_action)
{
	wlan_adapter *adapter = priv->adapter;

	cmd->command = cpu_to_le16(cmd_802_11_mac_address);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_mac_address) +
			     S_DS_GEN);
	cmd->result = 0;

	cmd->params.macadd.action = cpu_to_le16(cmd_action);

	if (cmd_action == cmd_act_set) {
		memcpy(cmd->params.macadd.macadd,
		       adapter->current_addr, ETH_ALEN);
		lbs_dbg_hex("SET_CMD: MAC ADDRESS-", adapter->current_addr, 6);
	}

	return 0;
}

static int wlan_cmd_802_11_eeprom_access(wlan_private * priv,
					 struct cmd_ds_command *cmd,
					 int cmd_action, void *pdata_buf)
{
	struct wlan_ioctl_regrdwr *ea = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_802_11_eeprom_access);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_eeprom_access) +
				S_DS_GEN);
	cmd->result = 0;

	cmd->params.rdeeprom.action = cpu_to_le16(ea->action);
	cmd->params.rdeeprom.offset = cpu_to_le16(ea->offset);
	cmd->params.rdeeprom.bytecount = cpu_to_le16(ea->NOB);
	cmd->params.rdeeprom.value = 0;

	return 0;
}

static int wlan_cmd_bt_access(wlan_private * priv,
			       struct cmd_ds_command *cmd,
			       u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_bt_access *bt_access = &cmd->params.bt;
	lbs_deb_cmd("BT CMD(%d)\n", cmd_action);

	cmd->command = cpu_to_le16(cmd_bt_access);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_bt_access) + S_DS_GEN);
	cmd->result = 0;
	bt_access->action = cpu_to_le16(cmd_action);

	switch (cmd_action) {
	case cmd_act_bt_access_add:
		memcpy(bt_access->addr1, pdata_buf, 2 * ETH_ALEN);
		lbs_dbg_hex("BT_ADD: blinded mac address-", bt_access->addr1, 6);
		break;
	case cmd_act_bt_access_del:
		memcpy(bt_access->addr1, pdata_buf, 1 * ETH_ALEN);
		lbs_dbg_hex("BT_DEL: blinded mac address-", bt_access->addr1, 6);
		break;
	case cmd_act_bt_access_list:
		bt_access->id = cpu_to_le32(*(u32 *) pdata_buf);
		break;
	case cmd_act_bt_access_reset:
		break;
	case cmd_act_bt_access_set_invert:
		bt_access->id = cpu_to_le32(*(u32 *) pdata_buf);
		break;
	case cmd_act_bt_access_get_invert:
		break;
	default:
		break;
	}
	return 0;
}

static int wlan_cmd_fwt_access(wlan_private * priv,
			       struct cmd_ds_command *cmd,
			       u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_fwt_access *fwt_access = &cmd->params.fwt;
	lbs_deb_cmd("FWT CMD(%d)\n", cmd_action);

	cmd->command = cpu_to_le16(cmd_fwt_access);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_fwt_access) + S_DS_GEN);
	cmd->result = 0;

	if (pdata_buf)
		memcpy(fwt_access, pdata_buf, sizeof(*fwt_access));
	else
		memset(fwt_access, 0, sizeof(*fwt_access));

	fwt_access->action = cpu_to_le16(cmd_action);

	return 0;
}

static int wlan_cmd_mesh_access(wlan_private * priv,
				struct cmd_ds_command *cmd,
				u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_mesh_access *mesh_access = &cmd->params.mesh;
	lbs_deb_cmd("FWT CMD(%d)\n", cmd_action);

	cmd->command = cpu_to_le16(cmd_mesh_access);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_mesh_access) + S_DS_GEN);
	cmd->result = 0;

	if (pdata_buf)
		memcpy(mesh_access, pdata_buf, sizeof(*mesh_access));
	else
		memset(mesh_access, 0, sizeof(*mesh_access));

	mesh_access->action = cpu_to_le16(cmd_action);

	return 0;
}

void libertas_queue_cmd(wlan_adapter * adapter, struct cmd_ctrl_node *cmdnode, u8 addtail)
{
	unsigned long flags;
	struct cmd_ds_command *cmdptr;

	lbs_deb_enter(LBS_DEB_CMD);

	if (!cmdnode) {
		lbs_deb_cmd("QUEUE_CMD: cmdnode is NULL\n");
		goto done;
	}

	cmdptr = (struct cmd_ds_command *)cmdnode->bufvirtualaddr;
	if (!cmdptr) {
		lbs_deb_cmd("QUEUE_CMD: cmdptr is NULL\n");
		goto done;
	}

	/* Exit_PS command needs to be queued in the header always. */
	if (cmdptr->command == cmd_802_11_ps_mode) {
		struct cmd_ds_802_11_ps_mode *psm = &cmdptr->params.psmode;
		if (psm->action == cpu_to_le16(cmd_subcmd_exit_ps)) {
			if (adapter->psstate != PS_STATE_FULL_POWER)
				addtail = 0;
		}
	}

	spin_lock_irqsave(&adapter->driver_lock, flags);

	if (addtail)
		list_add_tail((struct list_head *)cmdnode,
			      &adapter->cmdpendingq);
	else
		list_add((struct list_head *)cmdnode, &adapter->cmdpendingq);

	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	lbs_deb_cmd("QUEUE_CMD: Inserted node=%p, cmd=0x%x in cmdpendingq\n",
	       cmdnode,
	       le16_to_cpu(((struct cmd_ds_gen*)cmdnode->bufvirtualaddr)->command));

done:
	lbs_deb_leave(LBS_DEB_CMD);
}

/*
 * TODO: Fix the issue when DownloadcommandToStation is being called the
 * second time when the command timesout. All the cmdptr->xxx are in little
 * endian and therefore all the comparissions will fail.
 * For now - we are not performing the endian conversion the second time - but
 * for PS and DEEP_SLEEP we need to worry
 */
static int DownloadcommandToStation(wlan_private * priv,
				    struct cmd_ctrl_node *cmdnode)
{
	unsigned long flags;
	struct cmd_ds_command *cmdptr;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	u16 cmdsize;
	u16 command;

	lbs_deb_enter(LBS_DEB_CMD);

	if (!adapter || !cmdnode) {
		lbs_deb_cmd("DNLD_CMD: adapter = %p, cmdnode = %p\n",
		       adapter, cmdnode);
		if (cmdnode) {
			spin_lock_irqsave(&adapter->driver_lock, flags);
			__libertas_cleanup_and_insert_cmd(priv, cmdnode);
			spin_unlock_irqrestore(&adapter->driver_lock, flags);
		}
		ret = -1;
		goto done;
	}

	cmdptr = (struct cmd_ds_command *)cmdnode->bufvirtualaddr;


	spin_lock_irqsave(&adapter->driver_lock, flags);
	if (!cmdptr || !cmdptr->size) {
		lbs_deb_cmd("DNLD_CMD: cmdptr is Null or cmd size is Zero, "
		       "Not sending\n");
		__libertas_cleanup_and_insert_cmd(priv, cmdnode);
		spin_unlock_irqrestore(&adapter->driver_lock, flags);
		ret = -1;
		goto done;
	}

	adapter->cur_cmd = cmdnode;
	adapter->cur_cmd_retcode = 0;
	spin_unlock_irqrestore(&adapter->driver_lock, flags);
	lbs_deb_cmd("DNLD_CMD:: Before download, size of cmd = %d\n",
		    le16_to_cpu(cmdptr->size));

	cmdsize = cmdptr->size;

	command = cpu_to_le16(cmdptr->command);

	cmdnode->cmdwaitqwoken = 0;
	cmdsize = cpu_to_le16(cmdsize);

	ret = priv->hw_host_to_card(priv, MVMS_CMD, (u8 *) cmdptr, cmdsize);

	if (ret != 0) {
		lbs_deb_cmd("DNLD_CMD: Host to Card failed\n");
		spin_lock_irqsave(&adapter->driver_lock, flags);
		__libertas_cleanup_and_insert_cmd(priv, adapter->cur_cmd);
		adapter->cur_cmd = NULL;
		spin_unlock_irqrestore(&adapter->driver_lock, flags);
		ret = -1;
		goto done;
	}

	lbs_deb_cmd("DNLD_CMD: Sent command 0x%x @ %lu\n", command, jiffies);
	lbs_dbg_hex("DNLD_CMD: command", cmdnode->bufvirtualaddr, cmdsize);

	/* Setup the timer after transmit command */
	if (command == cmd_802_11_scan || command == cmd_802_11_authenticate
	    || command == cmd_802_11_associate)
		mod_timer(&adapter->command_timer, jiffies + (10*HZ));
	else
		mod_timer(&adapter->command_timer, jiffies + (5*HZ));

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int wlan_cmd_mac_control(wlan_private * priv,
				struct cmd_ds_command *cmd)
{
	struct cmd_ds_mac_control *mac = &cmd->params.macctrl;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(cmd_mac_control);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_mac_control) + S_DS_GEN);
	mac->action = cpu_to_le16(priv->adapter->currentpacketfilter);

	lbs_deb_cmd("wlan_cmd_mac_control(): action=0x%X size=%d\n",
		    le16_to_cpu(mac->action), le16_to_cpu(cmd->size));

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

/**
 *  This function inserts command node to cmdfreeq
 *  after cleans it. Requires adapter->driver_lock held.
 */
void __libertas_cleanup_and_insert_cmd(wlan_private * priv, struct cmd_ctrl_node *ptempcmd)
{
	wlan_adapter *adapter = priv->adapter;

	if (!ptempcmd)
		goto done;

	cleanup_cmdnode(ptempcmd);
	list_add_tail((struct list_head *)ptempcmd, &adapter->cmdfreeq);
done:
	return;
}

void libertas_cleanup_and_insert_cmd(wlan_private * priv, struct cmd_ctrl_node *ptempcmd)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->adapter->driver_lock, flags);
	__libertas_cleanup_and_insert_cmd(priv, ptempcmd);
	spin_unlock_irqrestore(&priv->adapter->driver_lock, flags);
}

int libertas_set_radio_control(wlan_private * priv)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_radio_control,
				    cmd_act_set,
				    cmd_option_waitforrsp, 0, NULL);

	lbs_deb_cmd("RADIO_SET: on or off: 0x%X, preamble = 0x%X\n",
	       priv->adapter->radioon, priv->adapter->preamble);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

int libertas_set_mac_packet_filter(wlan_private * priv)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	lbs_deb_cmd("libertas_set_mac_packet_filter value = %x\n",
	       priv->adapter->currentpacketfilter);

	/* Send MAC control command to station */
	ret = libertas_prepare_and_send_command(priv,
				    cmd_mac_control, 0, 0, 0, NULL);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function prepare the command before send to firmware.
 *
 *  @param priv		A pointer to wlan_private structure
 *  @param cmd_no	command number
 *  @param cmd_action	command action: GET or SET
 *  @param wait_option	wait option: wait response or not
 *  @param cmd_oid	cmd oid: treated as sub command
 *  @param pdata_buf	A pointer to informaion buffer
 *  @return 		0 or -1
 */
int libertas_prepare_and_send_command(wlan_private * priv,
			  u16 cmd_no,
			  u16 cmd_action,
			  u16 wait_option, u32 cmd_oid, void *pdata_buf)
{
	int ret = 0;
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *cmdnode;
	struct cmd_ds_command *cmdptr;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_CMD);

	if (!adapter) {
		lbs_deb_cmd("PREP_CMD: adapter is Null\n");
		ret = -1;
		goto done;
	}

	if (adapter->surpriseremoved) {
		lbs_deb_cmd("PREP_CMD: Card is Removed\n");
		ret = -1;
		goto done;
	}

	cmdnode = libertas_get_free_cmd_ctrl_node(priv);

	if (cmdnode == NULL) {
		lbs_deb_cmd("PREP_CMD: No free cmdnode\n");

		/* Wake up main thread to execute next command */
		wake_up_interruptible(&priv->mainthread.waitq);
		ret = -1;
		goto done;
	}

	libertas_set_cmd_ctrl_node(priv, cmdnode, cmd_oid, wait_option, pdata_buf);

	cmdptr = (struct cmd_ds_command *)cmdnode->bufvirtualaddr;

	lbs_deb_cmd("PREP_CMD: Val of cmd ptr=%p, command=0x%X\n",
	       cmdptr, cmd_no);

	if (!cmdptr) {
		lbs_deb_cmd("PREP_CMD: bufvirtualaddr of cmdnode is NULL\n");
		libertas_cleanup_and_insert_cmd(priv, cmdnode);
		ret = -1;
		goto done;
	}

	/* Set sequence number, command and INT option */
	adapter->seqnum++;
	cmdptr->seqnum = cpu_to_le16(adapter->seqnum);

	cmdptr->command = cpu_to_le16(cmd_no);
	cmdptr->result = 0;

	switch (cmd_no) {
	case cmd_get_hw_spec:
		ret = wlan_cmd_hw_spec(priv, cmdptr);
		break;
	case cmd_802_11_ps_mode:
		ret = wlan_cmd_802_11_ps_mode(priv, cmdptr, cmd_action);
		break;

	case cmd_802_11_scan:
		ret = libertas_cmd_80211_scan(priv, cmdptr, pdata_buf);
		break;

	case cmd_mac_control:
		ret = wlan_cmd_mac_control(priv, cmdptr);
		break;

	case cmd_802_11_associate:
	case cmd_802_11_reassociate:
		ret = libertas_cmd_80211_associate(priv, cmdptr, pdata_buf);
		break;

	case cmd_802_11_deauthenticate:
		ret = libertas_cmd_80211_deauthenticate(priv, cmdptr);
		break;

	case cmd_802_11_set_wep:
		ret = wlan_cmd_802_11_set_wep(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case cmd_802_11_ad_hoc_start:
		ret = libertas_cmd_80211_ad_hoc_start(priv, cmdptr, pdata_buf);
		break;
	case cmd_code_dnld:
		break;

	case cmd_802_11_reset:
		ret = wlan_cmd_802_11_reset(priv, cmdptr, cmd_action);
		break;

	case cmd_802_11_get_log:
		ret = wlan_cmd_802_11_get_log(priv, cmdptr);
		break;

	case cmd_802_11_authenticate:
		ret = libertas_cmd_80211_authenticate(priv, cmdptr, pdata_buf);
		break;

	case cmd_802_11_get_stat:
		ret = wlan_cmd_802_11_get_stat(priv, cmdptr);
		break;

	case cmd_802_11_snmp_mib:
		ret = wlan_cmd_802_11_snmp_mib(priv, cmdptr,
					       cmd_action, cmd_oid, pdata_buf);
		break;

	case cmd_mac_reg_access:
	case cmd_bbp_reg_access:
	case cmd_rf_reg_access:
		ret = wlan_cmd_reg_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case cmd_802_11_rf_channel:
		ret = wlan_cmd_802_11_rf_channel(priv, cmdptr,
						 cmd_action, pdata_buf);
		break;

	case cmd_802_11_rf_tx_power:
		ret = wlan_cmd_802_11_rf_tx_power(priv, cmdptr,
						  cmd_action, pdata_buf);
		break;

	case cmd_802_11_radio_control:
		ret = wlan_cmd_802_11_radio_control(priv, cmdptr, cmd_action);
		break;

	case cmd_802_11_rf_antenna:
		ret = wlan_cmd_802_11_rf_antenna(priv, cmdptr,
						 cmd_action, pdata_buf);
		break;

	case cmd_802_11_data_rate:
		ret = wlan_cmd_802_11_data_rate(priv, cmdptr, cmd_action);
		break;
	case cmd_802_11_rate_adapt_rateset:
		ret = wlan_cmd_802_11_rate_adapt_rateset(priv,
							 cmdptr, cmd_action);
		break;

	case cmd_mac_multicast_adr:
		ret = wlan_cmd_mac_multicast_adr(priv, cmdptr, cmd_action);
		break;

	case cmd_802_11_ad_hoc_join:
		ret = libertas_cmd_80211_ad_hoc_join(priv, cmdptr, pdata_buf);
		break;

	case cmd_802_11_rssi:
		ret = wlan_cmd_802_11_rssi(priv, cmdptr);
		break;

	case cmd_802_11_ad_hoc_stop:
		ret = libertas_cmd_80211_ad_hoc_stop(priv, cmdptr);
		break;

	case cmd_802_11_enable_rsn:
		ret = wlan_cmd_802_11_enable_rsn(priv, cmdptr, cmd_action,
				pdata_buf);
		break;

	case cmd_802_11_key_material:
		ret = wlan_cmd_802_11_key_material(priv, cmdptr, cmd_action,
				cmd_oid, pdata_buf);
		break;

	case cmd_802_11_pairwise_tsc:
		break;
	case cmd_802_11_group_tsc:
		break;

	case cmd_802_11_mac_address:
		ret = wlan_cmd_802_11_mac_address(priv, cmdptr, cmd_action);
		break;

	case cmd_802_11_eeprom_access:
		ret = wlan_cmd_802_11_eeprom_access(priv, cmdptr,
						    cmd_action, pdata_buf);
		break;

	case cmd_802_11_set_afc:
	case cmd_802_11_get_afc:

		cmdptr->command = cpu_to_le16(cmd_no);
		cmdptr->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_afc) +
					   S_DS_GEN);

		memmove(&cmdptr->params.afc,
			pdata_buf, sizeof(struct cmd_ds_802_11_afc));

		ret = 0;
		goto done;

	case cmd_802_11d_domain_info:
		ret = libertas_cmd_802_11d_domain_info(priv, cmdptr,
						   cmd_no, cmd_action);
		break;

	case cmd_802_11_sleep_params:
		ret = wlan_cmd_802_11_sleep_params(priv, cmdptr, cmd_action);
		break;
	case cmd_802_11_inactivity_timeout:
		ret = wlan_cmd_802_11_inactivity_timeout(priv, cmdptr,
							 cmd_action, pdata_buf);
		libertas_set_cmd_ctrl_node(priv, cmdnode, 0, 0, pdata_buf);
		break;

	case cmd_802_11_tpc_cfg:
		cmdptr->command = cpu_to_le16(cmd_802_11_tpc_cfg);
		cmdptr->size =
		    cpu_to_le16(sizeof(struct cmd_ds_802_11_tpc_cfg) +
				     S_DS_GEN);

		memmove(&cmdptr->params.tpccfg,
			pdata_buf, sizeof(struct cmd_ds_802_11_tpc_cfg));

		ret = 0;
		break;
	case cmd_802_11_led_gpio_ctrl:
		{
			struct mrvlietypes_ledgpio *gpio =
			    (struct mrvlietypes_ledgpio*)
			    cmdptr->params.ledgpio.data;

			memmove(&cmdptr->params.ledgpio,
				pdata_buf,
				sizeof(struct cmd_ds_802_11_led_ctrl));

			cmdptr->command =
			    cpu_to_le16(cmd_802_11_led_gpio_ctrl);

#define ACTION_NUMLED_TLVTYPE_LEN_FIELDS_LEN 8
			cmdptr->size =
			    cpu_to_le16(gpio->header.len + S_DS_GEN +
					     ACTION_NUMLED_TLVTYPE_LEN_FIELDS_LEN);
			gpio->header.len = cpu_to_le16(gpio->header.len);

			ret = 0;
			break;
		}
	case cmd_802_11_pwr_cfg:
		cmdptr->command = cpu_to_le16(cmd_802_11_pwr_cfg);
		cmdptr->size =
		    cpu_to_le16(sizeof(struct cmd_ds_802_11_pwr_cfg) +
				     S_DS_GEN);
		memmove(&cmdptr->params.pwrcfg, pdata_buf,
			sizeof(struct cmd_ds_802_11_pwr_cfg));

		ret = 0;
		break;
	case cmd_bt_access:
		ret = wlan_cmd_bt_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case cmd_fwt_access:
		ret = wlan_cmd_fwt_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case cmd_mesh_access:
		ret = wlan_cmd_mesh_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case cmd_get_tsf:
		cmdptr->command = cpu_to_le16(cmd_get_tsf);
		cmdptr->size = cpu_to_le16(sizeof(struct cmd_ds_get_tsf) +
					   S_DS_GEN);
		ret = 0;
		break;
	case cmd_802_11_tx_rate_query:
		cmdptr->command = cpu_to_le16(cmd_802_11_tx_rate_query);
		cmdptr->size = cpu_to_le16(sizeof(struct cmd_tx_rate_query) +
					   S_DS_GEN);
		adapter->txrate = 0;
		ret = 0;
		break;
	default:
		lbs_deb_cmd("PREP_CMD: unknown command- %#x\n", cmd_no);
		ret = -1;
		break;
	}

	/* return error, since the command preparation failed */
	if (ret != 0) {
		lbs_deb_cmd("PREP_CMD: command preparation failed\n");
		libertas_cleanup_and_insert_cmd(priv, cmdnode);
		ret = -1;
		goto done;
	}

	cmdnode->cmdwaitqwoken = 0;

	libertas_queue_cmd(adapter, cmdnode, 1);
	adapter->nr_cmd_pending++;
	wake_up_interruptible(&priv->mainthread.waitq);

	if (wait_option & cmd_option_waitforrsp) {
		lbs_deb_cmd("PREP_CMD: Wait for CMD response\n");
		might_sleep();
		wait_event_interruptible(cmdnode->cmdwait_q,
					 cmdnode->cmdwaitqwoken);
	}

	spin_lock_irqsave(&adapter->driver_lock, flags);
	if (adapter->cur_cmd_retcode) {
		lbs_deb_cmd("PREP_CMD: command failed with return code=%d\n",
		       adapter->cur_cmd_retcode);
		adapter->cur_cmd_retcode = 0;
		ret = -1;
	}
	spin_unlock_irqrestore(&adapter->driver_lock, flags);

done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(libertas_prepare_and_send_command);

/**
 *  @brief This function allocates the command buffer and link
 *  it to command free queue.
 *
 *  @param priv		A pointer to wlan_private structure
 *  @return 		0 or -1
 */
int libertas_allocate_cmd_buffer(wlan_private * priv)
{
	int ret = 0;
	u32 ulbufsize;
	u32 i;
	struct cmd_ctrl_node *tempcmd_array;
	u8 *ptempvirtualaddr;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_CMD);

	/* Allocate and initialize cmdCtrlNode */
	ulbufsize = sizeof(struct cmd_ctrl_node) * MRVDRV_NUM_OF_CMD_BUFFER;

	if (!(tempcmd_array = kzalloc(ulbufsize, GFP_KERNEL))) {
		lbs_deb_cmd(
		       "ALLOC_CMD_BUF: failed to allocate tempcmd_array\n");
		ret = -1;
		goto done;
	}
	adapter->cmd_array = tempcmd_array;

	/* Allocate and initialize command buffers */
	ulbufsize = MRVDRV_SIZE_OF_CMD_BUFFER;
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
		if (!(ptempvirtualaddr = kzalloc(ulbufsize, GFP_KERNEL))) {
			lbs_deb_cmd(
			       "ALLOC_CMD_BUF: ptempvirtualaddr: out of memory\n");
			ret = -1;
			goto done;
		}

		/* Update command buffer virtual */
		tempcmd_array[i].bufvirtualaddr = ptempvirtualaddr;
	}

	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
		init_waitqueue_head(&tempcmd_array[i].cmdwait_q);
		libertas_cleanup_and_insert_cmd(priv, &tempcmd_array[i]);
	}

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function frees the command buffer.
 *
 *  @param priv		A pointer to wlan_private structure
 *  @return 		0 or -1
 */
int libertas_free_cmd_buffer(wlan_private * priv)
{
	u32 ulbufsize; /* Someone needs to die for this. Slowly and painfully */
	unsigned int i;
	struct cmd_ctrl_node *tempcmd_array;
	wlan_adapter *adapter = priv->adapter;

	lbs_deb_enter(LBS_DEB_CMD);

	/* need to check if cmd array is allocated or not */
	if (adapter->cmd_array == NULL) {
		lbs_deb_cmd("FREE_CMD_BUF: cmd_array is Null\n");
		goto done;
	}

	tempcmd_array = adapter->cmd_array;

	/* Release shared memory buffers */
	ulbufsize = MRVDRV_SIZE_OF_CMD_BUFFER;
	for (i = 0; i < MRVDRV_NUM_OF_CMD_BUFFER; i++) {
		if (tempcmd_array[i].bufvirtualaddr) {
			lbs_deb_cmd("Free all the array\n");
			kfree(tempcmd_array[i].bufvirtualaddr);
			tempcmd_array[i].bufvirtualaddr = NULL;
		}
	}

	/* Release cmd_ctrl_node */
	if (adapter->cmd_array) {
		lbs_deb_cmd("Free cmd_array\n");
		kfree(adapter->cmd_array);
		adapter->cmd_array = NULL;
	}

done:
	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

/**
 *  @brief This function gets a free command node if available in
 *  command free queue.
 *
 *  @param priv		A pointer to wlan_private structure
 *  @return cmd_ctrl_node A pointer to cmd_ctrl_node structure or NULL
 */
struct cmd_ctrl_node *libertas_get_free_cmd_ctrl_node(wlan_private * priv)
{
	struct cmd_ctrl_node *tempnode;
	wlan_adapter *adapter = priv->adapter;
	unsigned long flags;

	if (!adapter)
		return NULL;

	spin_lock_irqsave(&adapter->driver_lock, flags);

	if (!list_empty(&adapter->cmdfreeq)) {
		tempnode = (struct cmd_ctrl_node *)adapter->cmdfreeq.next;
		list_del((struct list_head *)tempnode);
	} else {
		lbs_deb_cmd("GET_CMD_NODE: cmd_ctrl_node is not available\n");
		tempnode = NULL;
	}

	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	if (tempnode) {
		/*
		lbs_pr_debug(3, "GET_CMD_NODE: cmdCtrlNode available\n");
		lbs_pr_debug(3, "GET_CMD_NODE: cmdCtrlNode Address = %p\n",
		       tempnode);
		*/
		cleanup_cmdnode(tempnode);
	}

	return tempnode;
}

/**
 *  @brief This function cleans command node.
 *
 *  @param ptempnode	A pointer to cmdCtrlNode structure
 *  @return 		n/a
 */
static void cleanup_cmdnode(struct cmd_ctrl_node *ptempnode)
{
	if (!ptempnode)
		return;
	ptempnode->cmdwaitqwoken = 1;
	wake_up_interruptible(&ptempnode->cmdwait_q);
	ptempnode->status = 0;
	ptempnode->cmd_oid = (u32) 0;
	ptempnode->wait_option = 0;
	ptempnode->pdata_buf = NULL;

	if (ptempnode->bufvirtualaddr != NULL)
		memset(ptempnode->bufvirtualaddr, 0, MRVDRV_SIZE_OF_CMD_BUFFER);
	return;
}

/**
 *  @brief This function initializes the command node.
 *
 *  @param priv		A pointer to wlan_private structure
 *  @param ptempnode	A pointer to cmd_ctrl_node structure
 *  @param cmd_oid	cmd oid: treated as sub command
 *  @param wait_option	wait option: wait response or not
 *  @param pdata_buf	A pointer to informaion buffer
 *  @return 		0 or -1
 */
void libertas_set_cmd_ctrl_node(wlan_private * priv,
		    struct cmd_ctrl_node *ptempnode,
		    u32 cmd_oid, u16 wait_option, void *pdata_buf)
{
	lbs_deb_enter(LBS_DEB_CMD);

	if (!ptempnode)
		return;

	ptempnode->cmd_oid = cmd_oid;
	ptempnode->wait_option = wait_option;
	ptempnode->pdata_buf = pdata_buf;

	lbs_deb_leave(LBS_DEB_CMD);
}

/**
 *  @brief This function executes next command in command
 *  pending queue. It will put fimware back to PS mode
 *  if applicable.
 *
 *  @param priv     A pointer to wlan_private structure
 *  @return 	   0 or -1
 */
int libertas_execute_next_command(wlan_private * priv)
{
	wlan_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *cmdnode = NULL;
	struct cmd_ds_command *cmdptr;
	unsigned long flags;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	spin_lock_irqsave(&adapter->driver_lock, flags);

	if (adapter->cur_cmd) {
		lbs_pr_alert( "EXEC_NEXT_CMD: there is command in processing!\n");
		spin_unlock_irqrestore(&adapter->driver_lock, flags);
		ret = -1;
		goto done;
	}

	if (!list_empty(&adapter->cmdpendingq)) {
		cmdnode = (struct cmd_ctrl_node *)
		    adapter->cmdpendingq.next;
	}

	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	if (cmdnode) {
		lbs_deb_cmd(
		       "EXEC_NEXT_CMD: Got next command from cmdpendingq\n");
		cmdptr = (struct cmd_ds_command *)cmdnode->bufvirtualaddr;

		if (is_command_allowed_in_ps(cmdptr->command)) {
			if ((adapter->psstate == PS_STATE_SLEEP) ||
			    (adapter->psstate == PS_STATE_PRE_SLEEP)) {
				lbs_deb_cmd(
				       "EXEC_NEXT_CMD: Cannot send cmd 0x%x in psstate %d\n",
				       le16_to_cpu(cmdptr->command),
				       adapter->psstate);
				ret = -1;
				goto done;
			}
			lbs_deb_cmd("EXEC_NEXT_CMD: OK to send command "
			       "0x%x in psstate %d\n",
				    le16_to_cpu(cmdptr->command),
				    adapter->psstate);
		} else if (adapter->psstate != PS_STATE_FULL_POWER) {
			/*
			 * 1. Non-PS command:
			 * Queue it. set needtowakeup to TRUE if current state
			 * is SLEEP, otherwise call libertas_ps_wakeup to send Exit_PS.
			 * 2. PS command but not Exit_PS:
			 * Ignore it.
			 * 3. PS command Exit_PS:
			 * Set needtowakeup to TRUE if current state is SLEEP,
			 * otherwise send this command down to firmware
			 * immediately.
			 */
			if (cmdptr->command !=
			    cpu_to_le16(cmd_802_11_ps_mode)) {
				/*  Prepare to send Exit PS,
				 *  this non PS command will be sent later */
				if ((adapter->psstate == PS_STATE_SLEEP)
				    || (adapter->psstate == PS_STATE_PRE_SLEEP)
				    ) {
					/* w/ new scheme, it will not reach here.
					   since it is blocked in main_thread. */
					adapter->needtowakeup = 1;
				} else
					libertas_ps_wakeup(priv, 0);

				ret = 0;
				goto done;
			} else {
				/*
				 * PS command. Ignore it if it is not Exit_PS.
				 * otherwise send it down immediately.
				 */
				struct cmd_ds_802_11_ps_mode *psm =
				    &cmdptr->params.psmode;

				lbs_deb_cmd(
				       "EXEC_NEXT_CMD: PS cmd- action=0x%x\n",
				       psm->action);
				if (psm->action !=
				    cpu_to_le16(cmd_subcmd_exit_ps)) {
					lbs_deb_cmd(
					       "EXEC_NEXT_CMD: Ignore Enter PS cmd\n");
					list_del((struct list_head *)cmdnode);
					libertas_cleanup_and_insert_cmd(priv, cmdnode);

					ret = 0;
					goto done;
				}

				if ((adapter->psstate == PS_STATE_SLEEP) ||
				    (adapter->psstate == PS_STATE_PRE_SLEEP)) {
					lbs_deb_cmd(
					       "EXEC_NEXT_CMD: Ignore ExitPS cmd in sleep\n");
					list_del((struct list_head *)cmdnode);
					libertas_cleanup_and_insert_cmd(priv, cmdnode);
					adapter->needtowakeup = 1;

					ret = 0;
					goto done;
				}

				lbs_deb_cmd(
				       "EXEC_NEXT_CMD: Sending Exit_PS down...\n");
			}
		}
		list_del((struct list_head *)cmdnode);
		lbs_deb_cmd("EXEC_NEXT_CMD: Sending 0x%04X command\n",
			    le16_to_cpu(cmdptr->command));
		DownloadcommandToStation(priv, cmdnode);
	} else {
		/*
		 * check if in power save mode, if yes, put the device back
		 * to PS mode
		 */
		if ((adapter->psmode != wlan802_11powermodecam) &&
		    (adapter->psstate == PS_STATE_FULL_POWER) &&
		    (adapter->connect_status == libertas_connected)) {
			if (adapter->secinfo.WPAenabled ||
			    adapter->secinfo.WPA2enabled) {
				/* check for valid WPA group keys */
				if (adapter->wpa_mcast_key.len ||
				    adapter->wpa_unicast_key.len) {
					lbs_deb_cmd(
					       "EXEC_NEXT_CMD: WPA enabled and GTK_SET"
					       " go back to PS_SLEEP");
					libertas_ps_sleep(priv, 0);
				}
			} else {
				lbs_deb_cmd(
				       "EXEC_NEXT_CMD: command PendQ is empty,"
				       " go back to PS_SLEEP");
				libertas_ps_sleep(priv, 0);
			}
		}
	}

	ret = 0;
done:
	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

void libertas_send_iwevcustom_event(wlan_private * priv, s8 * str)
{
	union iwreq_data iwrq;
	u8 buf[50];

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&iwrq, 0, sizeof(union iwreq_data));
	memset(buf, 0, sizeof(buf));

	snprintf(buf, sizeof(buf) - 1, "%s", str);

	iwrq.data.length = strlen(buf) + 1 + IW_EV_LCP_LEN;

	/* Send Event to upper layer */
	lbs_deb_cmd("Event Indication string = %s\n", (char *)buf);
	lbs_deb_cmd("Event Indication String length = %d\n", iwrq.data.length);

	lbs_deb_cmd("Sending wireless event IWEVCUSTOM for %s\n", str);
	wireless_send_event(priv->dev, IWEVCUSTOM, &iwrq, buf);

	lbs_deb_leave(LBS_DEB_CMD);
}

static int sendconfirmsleep(wlan_private * priv, u8 * cmdptr, u16 size)
{
	unsigned long flags;
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	lbs_deb_cmd("SEND_SLEEPC_CMD: Before download, size of cmd = %d\n",
	       size);

	lbs_dbg_hex("SEND_SLEEPC_CMD: Sleep confirm command", cmdptr, size);

	ret = priv->hw_host_to_card(priv, MVMS_CMD, cmdptr, size);
	priv->dnld_sent = DNLD_RES_RECEIVED;

	spin_lock_irqsave(&adapter->driver_lock, flags);
	if (adapter->intcounter || adapter->currenttxskb)
		lbs_deb_cmd("SEND_SLEEPC_CMD: intcounter=%d currenttxskb=%p\n",
		       adapter->intcounter, adapter->currenttxskb);
	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	if (ret) {
		lbs_pr_alert(
		       "SEND_SLEEPC_CMD: Host to Card failed for Confirm Sleep\n");
	} else {
		spin_lock_irqsave(&adapter->driver_lock, flags);
		if (!adapter->intcounter) {
			adapter->psstate = PS_STATE_SLEEP;
		} else {
			lbs_deb_cmd("SEND_SLEEPC_CMD: After sent,IntC=%d\n",
			       adapter->intcounter);
		}
		spin_unlock_irqrestore(&adapter->driver_lock, flags);

		lbs_deb_cmd("SEND_SLEEPC_CMD: Sent Confirm Sleep command\n");
		lbs_deb_cmd("+");
	}

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

void libertas_ps_sleep(wlan_private * priv, int wait_option)
{
	lbs_deb_enter(LBS_DEB_CMD);

	/*
	 * PS is currently supported only in Infrastructure mode
	 * Remove this check if it is to be supported in IBSS mode also
	 */

	libertas_prepare_and_send_command(priv, cmd_802_11_ps_mode,
			      cmd_subcmd_enter_ps, wait_option, 0, NULL);

	lbs_deb_leave(LBS_DEB_CMD);
}

/**
 *  @brief This function sends Eixt_PS command to firmware.
 *
 *  @param priv    	A pointer to wlan_private structure
 *  @param wait_option	wait response or not
 *  @return 	   	n/a
 */
void libertas_ps_wakeup(wlan_private * priv, int wait_option)
{
	__le32 Localpsmode;

	lbs_deb_enter(LBS_DEB_CMD);

	Localpsmode = cpu_to_le32(wlan802_11powermodecam);

	lbs_deb_cmd("Exit_PS: Localpsmode = %d\n", wlan802_11powermodecam);

	libertas_prepare_and_send_command(priv, cmd_802_11_ps_mode,
			      cmd_subcmd_exit_ps,
			      wait_option, 0, &Localpsmode);

	lbs_deb_leave(LBS_DEB_CMD);
}

/**
 *  @brief This function checks condition and prepares to
 *  send sleep confirm command to firmware if ok.
 *
 *  @param priv    	A pointer to wlan_private structure
 *  @param psmode  	Power Saving mode
 *  @return 	   	n/a
 */
void libertas_ps_confirm_sleep(wlan_private * priv, u16 psmode)
{
	unsigned long flags =0;
	wlan_adapter *adapter = priv->adapter;
	u8 allowed = 1;

	lbs_deb_enter(LBS_DEB_CMD);

	if (priv->dnld_sent) {
		allowed = 0;
		lbs_deb_cmd("D");
	}

	spin_lock_irqsave(&adapter->driver_lock, flags);
	if (adapter->cur_cmd) {
		allowed = 0;
		lbs_deb_cmd("C");
	}
	if (adapter->intcounter > 0) {
		allowed = 0;
		lbs_deb_cmd("I%d", adapter->intcounter);
	}
	spin_unlock_irqrestore(&adapter->driver_lock, flags);

	if (allowed) {
		lbs_deb_cmd("Sending libertas_ps_confirm_sleep\n");
		sendconfirmsleep(priv, (u8 *) & adapter->libertas_ps_confirm_sleep,
				 sizeof(struct PS_CMD_ConfirmSleep));
	} else {
		lbs_deb_cmd("Sleep Confirm has been delayed\n");
	}

	lbs_deb_leave(LBS_DEB_CMD);
}
