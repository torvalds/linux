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
#include "cmd.h"

static void cleanup_cmdnode(struct cmd_ctrl_node *ptempnode);
static struct cmd_ctrl_node *lbs_get_cmd_ctrl_node(struct lbs_private *priv);
static void lbs_set_cmd_ctrl_node(struct lbs_private *priv,
		    struct cmd_ctrl_node *ptempnode,
		    void *pdata_buf);


/**
 *  @brief Checks whether a command is allowed in Power Save mode
 *
 *  @param command the command ID
 *  @return 	   1 if allowed, 0 if not allowed
 */
static u8 is_command_allowed_in_ps(u16 cmd)
{
	switch (cmd) {
	case CMD_802_11_RSSI:
		return 1;
	default:
		break;
	}
	return 0;
}

/**
 *  @brief Updates the hardware details like MAC address and regulatory region
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *
 *  @return 	   	0 on success, error on failure
 */
int lbs_update_hw_spec(struct lbs_private *priv)
{
	struct cmd_ds_get_hw_spec cmd;
	int ret = -1;
	u32 i;
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.permanentaddr, priv->current_addr, ETH_ALEN);
	ret = lbs_cmd_with_response(priv, CMD_GET_HW_SPEC, &cmd);
	if (ret)
		goto out;

	priv->fwcapinfo = le32_to_cpu(cmd.fwcapinfo);
	memcpy(priv->fwreleasenumber, cmd.fwreleasenumber, 4);

	lbs_deb_cmd("GET_HW_SPEC: firmware release %u.%u.%up%u\n",
		    priv->fwreleasenumber[2], priv->fwreleasenumber[1],
		    priv->fwreleasenumber[0], priv->fwreleasenumber[3]);
	lbs_deb_cmd("GET_HW_SPEC: MAC addr %s\n",
		    print_mac(mac, cmd.permanentaddr));
	lbs_deb_cmd("GET_HW_SPEC: hardware interface 0x%x, hardware spec 0x%04x\n",
		    cmd.hwifversion, cmd.version);

	/* Clamp region code to 8-bit since FW spec indicates that it should
	 * only ever be 8-bit, even though the field size is 16-bit.  Some firmware
	 * returns non-zero high 8 bits here.
	 */
	priv->regioncode = le16_to_cpu(cmd.regioncode) & 0xFF;

	for (i = 0; i < MRVDRV_MAX_REGION_CODE; i++) {
		/* use the region code to search for the index */
		if (priv->regioncode == lbs_region_code_to_index[i])
			break;
	}

	/* if it's unidentified region code, use the default (USA) */
	if (i >= MRVDRV_MAX_REGION_CODE) {
		priv->regioncode = 0x10;
		lbs_pr_info("unidentified region code; using the default (USA)\n");
	}

	if (priv->current_addr[0] == 0xff)
		memmove(priv->current_addr, cmd.permanentaddr, ETH_ALEN);

	memcpy(priv->dev->dev_addr, priv->current_addr, ETH_ALEN);
	if (priv->mesh_dev)
		memcpy(priv->mesh_dev->dev_addr, priv->current_addr, ETH_ALEN);

	if (lbs_set_regiontable(priv, priv->regioncode, 0)) {
		ret = -1;
		goto out;
	}

	if (lbs_set_universaltable(priv, 0)) {
		ret = -1;
		goto out;
	}

out:
	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

int lbs_host_sleep_cfg(struct lbs_private *priv, uint32_t criteria)
{
	struct cmd_ds_host_sleep cmd_config;
	int ret;

	cmd_config.criteria = cpu_to_le32(criteria);
	cmd_config.gpio = priv->wol_gpio;
	cmd_config.gap = priv->wol_gap;

	ret = lbs_cmd_with_response(priv, CMD_802_11_HOST_SLEEP_CFG, &cmd_config);
	if (!ret) {
		lbs_deb_cmd("Set WOL criteria to %x\n", criteria);
		priv->wol_criteria = criteria;
	} else {
		lbs_pr_info("HOST_SLEEP_CFG failed %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(lbs_host_sleep_cfg);

static int lbs_cmd_802_11_ps_mode(struct lbs_private *priv,
				   struct cmd_ds_command *cmd,
				   u16 cmd_action)
{
	struct cmd_ds_802_11_ps_mode *psm = &cmd->params.psmode;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_802_11_PS_MODE);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_ps_mode) +
				S_DS_GEN);
	psm->action = cpu_to_le16(cmd_action);
	psm->multipledtim = 0;
	switch (cmd_action) {
	case CMD_SUBCMD_ENTER_PS:
		lbs_deb_cmd("PS command:" "SubCode- Enter PS\n");

		psm->locallisteninterval = 0;
		psm->nullpktinterval = 0;
		psm->multipledtim =
		    cpu_to_le16(MRVDRV_DEFAULT_MULTIPLE_DTIM);
		break;

	case CMD_SUBCMD_EXIT_PS:
		lbs_deb_cmd("PS command:" "SubCode- Exit PS\n");
		break;

	case CMD_SUBCMD_SLEEP_CONFIRMED:
		lbs_deb_cmd("PS command: SubCode- sleep confirm\n");
		break;

	default:
		break;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_inactivity_timeout(struct lbs_private *priv,
					      struct cmd_ds_command *cmd,
					      u16 cmd_action, void *pdata_buf)
{
	u16 *timeout = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_802_11_INACTIVITY_TIMEOUT);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_inactivity_timeout)
			     + S_DS_GEN);

	cmd->params.inactivity_timeout.action = cpu_to_le16(cmd_action);

	if (cmd_action)
		cmd->params.inactivity_timeout.timeout = cpu_to_le16(*timeout);
	else
		cmd->params.inactivity_timeout.timeout = 0;

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_sleep_params(struct lbs_private *priv,
					struct cmd_ds_command *cmd,
					u16 cmd_action)
{
	struct cmd_ds_802_11_sleep_params *sp = &cmd->params.sleep_params;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size = cpu_to_le16((sizeof(struct cmd_ds_802_11_sleep_params)) +
				S_DS_GEN);
	cmd->command = cpu_to_le16(CMD_802_11_SLEEP_PARAMS);

	if (cmd_action == CMD_ACT_GET) {
		memset(&priv->sp, 0, sizeof(struct sleep_params));
		memset(sp, 0, sizeof(struct cmd_ds_802_11_sleep_params));
		sp->action = cpu_to_le16(cmd_action);
	} else if (cmd_action == CMD_ACT_SET) {
		sp->action = cpu_to_le16(cmd_action);
		sp->error = cpu_to_le16(priv->sp.sp_error);
		sp->offset = cpu_to_le16(priv->sp.sp_offset);
		sp->stabletime = cpu_to_le16(priv->sp.sp_stabletime);
		sp->calcontrol = (u8) priv->sp.sp_calcontrol;
		sp->externalsleepclk = (u8) priv->sp.sp_extsleepclk;
		sp->reserved = cpu_to_le16(priv->sp.sp_reserved);
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_set_wep(struct lbs_private *priv,
                                   struct cmd_ds_command *cmd,
                                   u32 cmd_act,
                                   void * pdata_buf)
{
	struct cmd_ds_802_11_set_wep *wep = &cmd->params.wep;
	int ret = 0;
	struct assoc_request * assoc_req = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_802_11_SET_WEP);
	cmd->size = cpu_to_le16(sizeof(*wep) + S_DS_GEN);

	if (cmd_act == CMD_ACT_ADD) {
		int i;

		if (!assoc_req) {
			lbs_deb_cmd("Invalid association request!\n");
			ret = -1;
			goto done;
		}

		wep->action = cpu_to_le16(CMD_ACT_ADD);

		/* default tx key index */
		wep->keyindex = cpu_to_le16((u16)(assoc_req->wep_tx_keyidx &
						  (u32)CMD_WEP_KEY_INDEX_MASK));

		/* Copy key types and material to host command structure */
		for (i = 0; i < 4; i++) {
			struct enc_key * pkey = &assoc_req->wep_keys[i];

			switch (pkey->len) {
			case KEY_LEN_WEP_40:
				wep->keytype[i] = CMD_TYPE_WEP_40_BIT;
				memmove(&wep->keymaterial[i], pkey->key,
				        pkey->len);
				lbs_deb_cmd("SET_WEP: add key %d (40 bit)\n", i);
				break;
			case KEY_LEN_WEP_104:
				wep->keytype[i] = CMD_TYPE_WEP_104_BIT;
				memmove(&wep->keymaterial[i], pkey->key,
				        pkey->len);
				lbs_deb_cmd("SET_WEP: add key %d (104 bit)\n", i);
				break;
			case 0:
				break;
			default:
				lbs_deb_cmd("SET_WEP: invalid key %d, length %d\n",
				       i, pkey->len);
				ret = -1;
				goto done;
				break;
			}
		}
	} else if (cmd_act == CMD_ACT_REMOVE) {
		/* ACT_REMOVE clears _all_ WEP keys */
		wep->action = cpu_to_le16(CMD_ACT_REMOVE);

		/* default tx key index */
		wep->keyindex = cpu_to_le16((u16)(priv->wep_tx_keyidx &
						  (u32)CMD_WEP_KEY_INDEX_MASK));
		lbs_deb_cmd("SET_WEP: remove key %d\n", priv->wep_tx_keyidx);
	}

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int lbs_cmd_802_11_enable_rsn(struct lbs_private *priv,
				      struct cmd_ds_command *cmd,
				      u16 cmd_action,
				      void * pdata_buf)
{
	struct cmd_ds_802_11_enable_rsn *penableRSN = &cmd->params.enbrsn;
	u32 * enable = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_802_11_ENABLE_RSN);
	cmd->size = cpu_to_le16(sizeof(*penableRSN) + S_DS_GEN);
	penableRSN->action = cpu_to_le16(cmd_action);

	if (cmd_action == CMD_ACT_SET) {
		if (*enable)
			penableRSN->enable = cpu_to_le16(CMD_ENABLE_RSN);
		else
			penableRSN->enable = cpu_to_le16(CMD_DISABLE_RSN);
		lbs_deb_cmd("ENABLE_RSN: %d\n", *enable);
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}


static ssize_t lbs_tlv_size(const u8 *tlv, u16 size)
{
	ssize_t pos = 0;
	struct mrvlietypesheader *tlv_h;
	while (pos < size) {
		u16 length;
		tlv_h = (struct mrvlietypesheader *) tlv;
		if (tlv_h->len == 0)
			return pos;
		length = le16_to_cpu(tlv_h->len) +
			sizeof(struct mrvlietypesheader);
		pos += length;
		tlv += length;
	}
	return pos;
}


static void lbs_cmd_802_11_subscribe_event(struct lbs_private *priv,
	struct cmd_ds_command *cmd, u16 cmd_action,
	void *pdata_buf)
{
	struct cmd_ds_802_11_subscribe_event *events =
		(struct cmd_ds_802_11_subscribe_event *) pdata_buf;

	/* pdata_buf points to a struct cmd_ds_802_11_subscribe_event and room
	 * for various Marvell TLVs */

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size = cpu_to_le16(sizeof(*events)
			- sizeof(events->tlv)
			+ S_DS_GEN);
	cmd->params.subscribe_event.action = cpu_to_le16(cmd_action);
	if (cmd_action == CMD_ACT_GET) {
		cmd->params.subscribe_event.events = 0;
	} else {
		ssize_t sz = lbs_tlv_size(events->tlv, sizeof(events->tlv));
		cmd->size = cpu_to_le16(le16_to_cpu(cmd->size) + sz);
		cmd->params.subscribe_event.events = events->events;
		memcpy(cmd->params.subscribe_event.tlv, events->tlv, sz);
	}

	lbs_deb_leave(LBS_DEB_CMD);
}

static void set_one_wpa_key(struct MrvlIEtype_keyParamSet * pkeyparamset,
                            struct enc_key * pkey)
{
	lbs_deb_enter(LBS_DEB_CMD);

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
	pkeyparamset->keytypeid = cpu_to_le16(pkey->type);
	pkeyparamset->keylen = cpu_to_le16(pkey->len);
	memcpy(pkeyparamset->key, pkey->key, pkey->len);
	pkeyparamset->length = cpu_to_le16(  sizeof(pkeyparamset->keytypeid)
	                                        + sizeof(pkeyparamset->keyinfo)
	                                        + sizeof(pkeyparamset->keylen)
	                                        + sizeof(pkeyparamset->key));
	lbs_deb_leave(LBS_DEB_CMD);
}

static int lbs_cmd_802_11_key_material(struct lbs_private *priv,
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

	cmd->command = cpu_to_le16(CMD_802_11_KEY_MATERIAL);
	pkeymaterial->action = cpu_to_le16(cmd_action);

	if (cmd_action == CMD_ACT_GET) {
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

static int lbs_cmd_802_11_reset(struct lbs_private *priv,
				 struct cmd_ds_command *cmd, int cmd_action)
{
	struct cmd_ds_802_11_reset *reset = &cmd->params.reset;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_802_11_RESET);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_reset) + S_DS_GEN);
	reset->action = cpu_to_le16(cmd_action);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_get_log(struct lbs_private *priv,
				   struct cmd_ds_command *cmd)
{
	lbs_deb_enter(LBS_DEB_CMD);
	cmd->command = cpu_to_le16(CMD_802_11_GET_LOG);
	cmd->size =
		cpu_to_le16(sizeof(struct cmd_ds_802_11_get_log) + S_DS_GEN);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_get_stat(struct lbs_private *priv,
				    struct cmd_ds_command *cmd)
{
	lbs_deb_enter(LBS_DEB_CMD);
	cmd->command = cpu_to_le16(CMD_802_11_GET_STAT);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_get_stat) + S_DS_GEN);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_snmp_mib(struct lbs_private *priv,
				    struct cmd_ds_command *cmd,
				    int cmd_action,
				    int cmd_oid, void *pdata_buf)
{
	struct cmd_ds_802_11_snmp_mib *pSNMPMIB = &cmd->params.smib;
	u8 ucTemp;

	lbs_deb_enter(LBS_DEB_CMD);

	lbs_deb_cmd("SNMP_CMD: cmd_oid = 0x%x\n", cmd_oid);

	cmd->command = cpu_to_le16(CMD_802_11_SNMP_MIB);
	cmd->size = cpu_to_le16(sizeof(*pSNMPMIB) + S_DS_GEN);

	switch (cmd_oid) {
	case OID_802_11_INFRASTRUCTURE_MODE:
	{
		u8 mode = (u8) (size_t) pdata_buf;
		pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_SET);
		pSNMPMIB->oid = cpu_to_le16((u16) DESIRED_BSSTYPE_I);
		pSNMPMIB->bufsize = cpu_to_le16(sizeof(u8));
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

			pSNMPMIB->oid = cpu_to_le16((u16) DOT11D_I);

			if (cmd_action == CMD_ACT_SET) {
				pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_SET);
				pSNMPMIB->bufsize = cpu_to_le16(sizeof(u16));
				ulTemp = *(u32 *)pdata_buf;
				*((__le16 *)(pSNMPMIB->value)) =
				    cpu_to_le16((u16) ulTemp);
			}
			break;
		}

	case OID_802_11_FRAGMENTATION_THRESHOLD:
		{
			u32 ulTemp;

			pSNMPMIB->oid = cpu_to_le16((u16) FRAGTHRESH_I);

			if (cmd_action == CMD_ACT_GET) {
				pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_GET);
			} else if (cmd_action == CMD_ACT_SET) {
				pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_SET);
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
			pSNMPMIB->oid = cpu_to_le16(RTSTHRESH_I);

			if (cmd_action == CMD_ACT_GET) {
				pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_GET);
			} else if (cmd_action == CMD_ACT_SET) {
				pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_SET);
				pSNMPMIB->bufsize = cpu_to_le16(sizeof(u16));
				ulTemp = *((u32 *)pdata_buf);
				*(__le16 *)(pSNMPMIB->value) =
				    cpu_to_le16((u16) ulTemp);

			}
			break;
		}
	case OID_802_11_TX_RETRYCOUNT:
		pSNMPMIB->oid = cpu_to_le16((u16) SHORT_RETRYLIM_I);

		if (cmd_action == CMD_ACT_GET) {
			pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_GET);
		} else if (cmd_action == CMD_ACT_SET) {
			pSNMPMIB->querytype = cpu_to_le16(CMD_ACT_SET);
			pSNMPMIB->bufsize = cpu_to_le16(sizeof(u16));
			*((__le16 *)(pSNMPMIB->value)) =
			    cpu_to_le16((u16) priv->txretrycount);
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
	       "SNMP_CMD: action 0x%x, oid 0x%x, oidsize 0x%x, value 0x%x\n",
	       le16_to_cpu(pSNMPMIB->querytype), le16_to_cpu(pSNMPMIB->oid),
	       le16_to_cpu(pSNMPMIB->bufsize),
	       le16_to_cpu(*(__le16 *) pSNMPMIB->value));

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_radio_control(struct lbs_private *priv,
					 struct cmd_ds_command *cmd,
					 int cmd_action)
{
	struct cmd_ds_802_11_radio_control *pradiocontrol = &cmd->params.radio;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size =
	    cpu_to_le16((sizeof(struct cmd_ds_802_11_radio_control)) +
			     S_DS_GEN);
	cmd->command = cpu_to_le16(CMD_802_11_RADIO_CONTROL);

	pradiocontrol->action = cpu_to_le16(cmd_action);

	switch (priv->preamble) {
	case CMD_TYPE_SHORT_PREAMBLE:
		pradiocontrol->control = cpu_to_le16(SET_SHORT_PREAMBLE);
		break;

	case CMD_TYPE_LONG_PREAMBLE:
		pradiocontrol->control = cpu_to_le16(SET_LONG_PREAMBLE);
		break;

	case CMD_TYPE_AUTO_PREAMBLE:
	default:
		pradiocontrol->control = cpu_to_le16(SET_AUTO_PREAMBLE);
		break;
	}

	if (priv->radioon)
		pradiocontrol->control |= cpu_to_le16(TURN_ON_RF);
	else
		pradiocontrol->control &= cpu_to_le16(~TURN_ON_RF);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_rf_tx_power(struct lbs_private *priv,
				       struct cmd_ds_command *cmd,
				       u16 cmd_action, void *pdata_buf)
{

	struct cmd_ds_802_11_rf_tx_power *prtp = &cmd->params.txp;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->size =
	    cpu_to_le16((sizeof(struct cmd_ds_802_11_rf_tx_power)) + S_DS_GEN);
	cmd->command = cpu_to_le16(CMD_802_11_RF_TX_POWER);
	prtp->action = cpu_to_le16(cmd_action);

	lbs_deb_cmd("RF_TX_POWER_CMD: size:%d cmd:0x%x Act:%d\n",
		    le16_to_cpu(cmd->size), le16_to_cpu(cmd->command),
		    le16_to_cpu(prtp->action));

	switch (cmd_action) {
	case CMD_ACT_TX_POWER_OPT_GET:
		prtp->action = cpu_to_le16(CMD_ACT_GET);
		prtp->currentlevel = 0;
		break;

	case CMD_ACT_TX_POWER_OPT_SET_HIGH:
		prtp->action = cpu_to_le16(CMD_ACT_SET);
		prtp->currentlevel = cpu_to_le16(CMD_ACT_TX_POWER_INDEX_HIGH);
		break;

	case CMD_ACT_TX_POWER_OPT_SET_MID:
		prtp->action = cpu_to_le16(CMD_ACT_SET);
		prtp->currentlevel = cpu_to_le16(CMD_ACT_TX_POWER_INDEX_MID);
		break;

	case CMD_ACT_TX_POWER_OPT_SET_LOW:
		prtp->action = cpu_to_le16(CMD_ACT_SET);
		prtp->currentlevel = cpu_to_le16(*((u16 *) pdata_buf));
		break;
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_monitor_mode(struct lbs_private *priv,
				      struct cmd_ds_command *cmd,
				      u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_802_11_monitor_mode *monitor = &cmd->params.monitor;

	cmd->command = cpu_to_le16(CMD_802_11_MONITOR_MODE);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_monitor_mode) +
			     S_DS_GEN);

	monitor->action = cpu_to_le16(cmd_action);
	if (cmd_action == CMD_ACT_SET) {
		monitor->mode =
		    cpu_to_le16((u16) (*(u32 *) pdata_buf));
	}

	return 0;
}

static int lbs_cmd_802_11_rate_adapt_rateset(struct lbs_private *priv,
					      struct cmd_ds_command *cmd,
					      u16 cmd_action)
{
	struct cmd_ds_802_11_rate_adapt_rateset
	*rateadapt = &cmd->params.rateset;

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_rate_adapt_rateset)
			     + S_DS_GEN);
	cmd->command = cpu_to_le16(CMD_802_11_RATE_ADAPT_RATESET);

	rateadapt->action = cpu_to_le16(cmd_action);
	rateadapt->enablehwauto = cpu_to_le16(priv->enablehwauto);
	rateadapt->bitmap = cpu_to_le16(priv->ratebitmap);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

/**
 *  @brief Get the current data rate
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *
 *  @return 	   	The data rate on success, error on failure
 */
int lbs_get_data_rate(struct lbs_private *priv)
{
	struct cmd_ds_802_11_data_rate cmd;
	int ret = -1;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_GET_TX_RATE);

	ret = lbs_cmd_with_response(priv, CMD_802_11_DATA_RATE, &cmd);
	if (ret)
		goto out;

	lbs_deb_hex(LBS_DEB_CMD, "DATA_RATE_RESP", (u8 *) &cmd, sizeof (cmd));

	ret = (int) lbs_fw_index_to_data_rate(cmd.rates[0]);
	lbs_deb_cmd("DATA_RATE: current rate 0x%02x\n", ret);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  @brief Set the data rate
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *  @param rate  	The desired data rate, or 0 to clear a locked rate
 *
 *  @return 	   	0 on success, error on failure
 */
int lbs_set_data_rate(struct lbs_private *priv, u8 rate)
{
	struct cmd_ds_802_11_data_rate cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	if (rate > 0) {
		cmd.action = cpu_to_le16(CMD_ACT_SET_TX_FIX_RATE);
		cmd.rates[0] = lbs_data_rate_to_fw_index(rate);
		if (cmd.rates[0] == 0) {
			lbs_deb_cmd("DATA_RATE: invalid requested rate of"
			            " 0x%02X\n", rate);
			ret = 0;
			goto out;
		}
		lbs_deb_cmd("DATA_RATE: set fixed 0x%02X\n", cmd.rates[0]);
	} else {
		cmd.action = cpu_to_le16(CMD_ACT_SET_TX_AUTO);
		lbs_deb_cmd("DATA_RATE: setting auto\n");
	}

	ret = lbs_cmd_with_response(priv, CMD_802_11_DATA_RATE, &cmd);
	if (ret)
		goto out;

	lbs_deb_hex(LBS_DEB_CMD, "DATA_RATE_RESP", (u8 *) &cmd, sizeof (cmd));

	/* FIXME: get actual rates FW can do if this command actually returns
	 * all data rates supported.
	 */
	priv->cur_rate = lbs_fw_index_to_data_rate(cmd.rates[0]);
	lbs_deb_cmd("DATA_RATE: current rate is 0x%02x\n", priv->cur_rate);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int lbs_cmd_mac_multicast_adr(struct lbs_private *priv,
				      struct cmd_ds_command *cmd,
				      u16 cmd_action)
{
	struct cmd_ds_mac_multicast_adr *pMCastAdr = &cmd->params.madr;

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_mac_multicast_adr) +
			     S_DS_GEN);
	cmd->command = cpu_to_le16(CMD_MAC_MULTICAST_ADR);

	lbs_deb_cmd("MULTICAST_ADR: setting %d addresses\n", pMCastAdr->nr_of_adrs);
	pMCastAdr->action = cpu_to_le16(cmd_action);
	pMCastAdr->nr_of_adrs =
	    cpu_to_le16((u16) priv->nr_of_multicastmacaddr);
	memcpy(pMCastAdr->maclist, priv->multicastlist,
	       priv->nr_of_multicastmacaddr * ETH_ALEN);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

/**
 *  @brief Get the radio channel
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *
 *  @return 	   	The channel on success, error on failure
 */
int lbs_get_channel(struct lbs_private *priv)
{
	struct cmd_ds_802_11_rf_channel cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_OPT_802_11_RF_CHANNEL_GET);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RF_CHANNEL, &cmd);
	if (ret)
		goto out;

	ret = le16_to_cpu(cmd.channel);
	lbs_deb_cmd("current radio channel is %d\n", ret);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  @brief Set the radio channel
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *  @param channel  	The desired channel, or 0 to clear a locked channel
 *
 *  @return 	   	0 on success, error on failure
 */
int lbs_set_channel(struct lbs_private *priv, u8 channel)
{
	struct cmd_ds_802_11_rf_channel cmd;
	u8 old_channel = priv->curbssparams.channel;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_OPT_802_11_RF_CHANNEL_SET);
	cmd.channel = cpu_to_le16(channel);

	ret = lbs_cmd_with_response(priv, CMD_802_11_RF_CHANNEL, &cmd);
	if (ret)
		goto out;

	priv->curbssparams.channel = (uint8_t) le16_to_cpu(cmd.channel);
	lbs_deb_cmd("channel switch from %d to %d\n", old_channel,
		priv->curbssparams.channel);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static int lbs_cmd_802_11_rssi(struct lbs_private *priv,
				struct cmd_ds_command *cmd)
{

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->command = cpu_to_le16(CMD_802_11_RSSI);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_rssi) + S_DS_GEN);
	cmd->params.rssi.N = cpu_to_le16(DEFAULT_BCN_AVG_FACTOR);

	/* reset Beacon SNR/NF/RSSI values */
	priv->SNR[TYPE_BEACON][TYPE_NOAVG] = 0;
	priv->SNR[TYPE_BEACON][TYPE_AVG] = 0;
	priv->NF[TYPE_BEACON][TYPE_NOAVG] = 0;
	priv->NF[TYPE_BEACON][TYPE_AVG] = 0;
	priv->RSSI[TYPE_BEACON][TYPE_NOAVG] = 0;
	priv->RSSI[TYPE_BEACON][TYPE_AVG] = 0;

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_reg_access(struct lbs_private *priv,
			       struct cmd_ds_command *cmdptr,
			       u8 cmd_action, void *pdata_buf)
{
	struct lbs_offset_value *offval;

	lbs_deb_enter(LBS_DEB_CMD);

	offval = (struct lbs_offset_value *)pdata_buf;

	switch (le16_to_cpu(cmdptr->command)) {
	case CMD_MAC_REG_ACCESS:
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

	case CMD_BBP_REG_ACCESS:
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

	case CMD_RF_REG_ACCESS:
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

static int lbs_cmd_802_11_mac_address(struct lbs_private *priv,
				       struct cmd_ds_command *cmd,
				       u16 cmd_action)
{

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->command = cpu_to_le16(CMD_802_11_MAC_ADDRESS);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_mac_address) +
			     S_DS_GEN);
	cmd->result = 0;

	cmd->params.macadd.action = cpu_to_le16(cmd_action);

	if (cmd_action == CMD_ACT_SET) {
		memcpy(cmd->params.macadd.macadd,
		       priv->current_addr, ETH_ALEN);
		lbs_deb_hex(LBS_DEB_CMD, "SET_CMD: MAC addr", priv->current_addr, 6);
	}

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_802_11_eeprom_access(struct lbs_private *priv,
					 struct cmd_ds_command *cmd,
					 int cmd_action, void *pdata_buf)
{
	struct lbs_ioctl_regrdwr *ea = pdata_buf;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_802_11_EEPROM_ACCESS);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_eeprom_access) +
				S_DS_GEN);
	cmd->result = 0;

	cmd->params.rdeeprom.action = cpu_to_le16(ea->action);
	cmd->params.rdeeprom.offset = cpu_to_le16(ea->offset);
	cmd->params.rdeeprom.bytecount = cpu_to_le16(ea->NOB);
	cmd->params.rdeeprom.value = 0;

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_bt_access(struct lbs_private *priv,
			       struct cmd_ds_command *cmd,
			       u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_bt_access *bt_access = &cmd->params.bt;
	lbs_deb_enter_args(LBS_DEB_CMD, "action %d", cmd_action);

	cmd->command = cpu_to_le16(CMD_BT_ACCESS);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_bt_access) + S_DS_GEN);
	cmd->result = 0;
	bt_access->action = cpu_to_le16(cmd_action);

	switch (cmd_action) {
	case CMD_ACT_BT_ACCESS_ADD:
		memcpy(bt_access->addr1, pdata_buf, 2 * ETH_ALEN);
		lbs_deb_hex(LBS_DEB_MESH, "BT_ADD: blinded MAC addr", bt_access->addr1, 6);
		break;
	case CMD_ACT_BT_ACCESS_DEL:
		memcpy(bt_access->addr1, pdata_buf, 1 * ETH_ALEN);
		lbs_deb_hex(LBS_DEB_MESH, "BT_DEL: blinded MAC addr", bt_access->addr1, 6);
		break;
	case CMD_ACT_BT_ACCESS_LIST:
		bt_access->id = cpu_to_le32(*(u32 *) pdata_buf);
		break;
	case CMD_ACT_BT_ACCESS_RESET:
		break;
	case CMD_ACT_BT_ACCESS_SET_INVERT:
		bt_access->id = cpu_to_le32(*(u32 *) pdata_buf);
		break;
	case CMD_ACT_BT_ACCESS_GET_INVERT:
		break;
	default:
		break;
	}
	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

static int lbs_cmd_fwt_access(struct lbs_private *priv,
			       struct cmd_ds_command *cmd,
			       u16 cmd_action, void *pdata_buf)
{
	struct cmd_ds_fwt_access *fwt_access = &cmd->params.fwt;
	lbs_deb_enter_args(LBS_DEB_CMD, "action %d", cmd_action);

	cmd->command = cpu_to_le16(CMD_FWT_ACCESS);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_fwt_access) + S_DS_GEN);
	cmd->result = 0;

	if (pdata_buf)
		memcpy(fwt_access, pdata_buf, sizeof(*fwt_access));
	else
		memset(fwt_access, 0, sizeof(*fwt_access));

	fwt_access->action = cpu_to_le16(cmd_action);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

int lbs_mesh_access(struct lbs_private *priv, uint16_t cmd_action,
		    struct cmd_ds_mesh_access *cmd)
{
	int ret;

	lbs_deb_enter_args(LBS_DEB_CMD, "action %d", cmd_action);

	cmd->hdr.command = cpu_to_le16(CMD_MESH_ACCESS);
	cmd->hdr.size = cpu_to_le16(sizeof(struct cmd_ds_mesh_access) + S_DS_GEN);
	cmd->hdr.result = 0;

	cmd->action = cpu_to_le16(cmd_action);

	ret = lbs_cmd_with_response(priv, CMD_MESH_ACCESS, cmd);

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_mesh_access);

int lbs_mesh_config(struct lbs_private *priv, uint16_t enable, uint16_t chan)
{
	struct cmd_ds_mesh_config cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.action = cpu_to_le16(enable);
	cmd.channel = cpu_to_le16(chan);
	cmd.type = cpu_to_le16(priv->mesh_tlv);

	if (enable) {
		cmd.length = cpu_to_le16(priv->mesh_ssid_len);
		memcpy(cmd.data, priv->mesh_ssid, priv->mesh_ssid_len);
	}
	lbs_deb_cmd("mesh config enable %d TLV %x channel %d SSID %s\n",
		    enable, priv->mesh_tlv, chan,
		    escape_essid(priv->mesh_ssid, priv->mesh_ssid_len));
	return lbs_cmd_with_response(priv, CMD_MESH_CONFIG, &cmd);
}

static int lbs_cmd_bcn_ctrl(struct lbs_private * priv,
				struct cmd_ds_command *cmd,
				u16 cmd_action)
{
	struct cmd_ds_802_11_beacon_control
		*bcn_ctrl = &cmd->params.bcn_ctrl;

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_beacon_control)
			     + S_DS_GEN);
	cmd->command = cpu_to_le16(CMD_802_11_BEACON_CTRL);

	bcn_ctrl->action = cpu_to_le16(cmd_action);
	bcn_ctrl->beacon_enable = cpu_to_le16(priv->beacon_enable);
	bcn_ctrl->beacon_period = cpu_to_le16(priv->beacon_period);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

/*
 * Note: NEVER use lbs_queue_cmd() with addtail==0 other than for
 * the command timer, because it does not account for queued commands.
 */
void lbs_queue_cmd(struct lbs_private *priv,
	struct cmd_ctrl_node *cmdnode,
	u8 addtail)
{
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_HOST);

	if (!cmdnode) {
		lbs_deb_host("QUEUE_CMD: cmdnode is NULL\n");
		goto done;
	}
	if (!cmdnode->cmdbuf->size) {
		lbs_deb_host("DNLD_CMD: cmd size is zero\n");
		goto done;
	}

	/* Exit_PS command needs to be queued in the header always. */
	if (le16_to_cpu(cmdnode->cmdbuf->command) == CMD_802_11_PS_MODE) {
		struct cmd_ds_802_11_ps_mode *psm = (void *) cmdnode->cmdbuf;

		if (psm->action == cpu_to_le16(CMD_SUBCMD_EXIT_PS)) {
			if (priv->psstate != PS_STATE_FULL_POWER)
				addtail = 0;
		}
	}

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (addtail)
		list_add_tail(&cmdnode->list, &priv->cmdpendingq);
	else
		list_add(&cmdnode->list, &priv->cmdpendingq);

	spin_unlock_irqrestore(&priv->driver_lock, flags);

	lbs_deb_host("QUEUE_CMD: inserted command 0x%04x into cmdpendingq\n",
		     le16_to_cpu(cmdnode->cmdbuf->command));

done:
	lbs_deb_leave(LBS_DEB_HOST);
}

static int lbs_submit_command(struct lbs_private *priv,
			      struct cmd_ctrl_node *cmdnode)
{
	unsigned long flags;
	struct cmd_header *cmd;
	int ret = -1;
	u16 cmdsize;
	u16 command;

	lbs_deb_enter(LBS_DEB_HOST);

	cmd = cmdnode->cmdbuf;

	spin_lock_irqsave(&priv->driver_lock, flags);
	priv->cur_cmd = cmdnode;
	priv->cur_cmd_retcode = 0;
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	cmdsize = le16_to_cpu(cmd->size);
	command = le16_to_cpu(cmd->command);

	lbs_deb_host("DNLD_CMD: command 0x%04x, seq %d, size %d, jiffies %lu\n",
		     command, le16_to_cpu(cmd->seqnum), cmdsize, jiffies);
	lbs_deb_hex(LBS_DEB_HOST, "DNLD_CMD", (void *) cmdnode->cmdbuf, cmdsize);

	ret = priv->hw_host_to_card(priv, MVMS_CMD, (u8 *) cmd, cmdsize);
	if (ret) {
		lbs_pr_info("DNLD_CMD: hw_host_to_card failed: %d\n", ret);
		spin_lock_irqsave(&priv->driver_lock, flags);
		lbs_complete_command(priv, priv->cur_cmd, ret);
		spin_unlock_irqrestore(&priv->driver_lock, flags);
		goto done;
	}

	lbs_deb_cmd("DNLD_CMD: sent command 0x%04x, jiffies %lu\n", command, jiffies);

	/* Setup the timer after transmit command */
	if (command == CMD_802_11_SCAN || command == CMD_802_11_AUTHENTICATE
	    || command == CMD_802_11_ASSOCIATE)
		mod_timer(&priv->command_timer, jiffies + (10*HZ));
	else
		mod_timer(&priv->command_timer, jiffies + (5*HZ));

	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}

static int lbs_cmd_mac_control(struct lbs_private *priv,
				struct cmd_ds_command *cmd)
{
	struct cmd_ds_mac_control *mac = &cmd->params.macctrl;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd->command = cpu_to_le16(CMD_MAC_CONTROL);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_mac_control) + S_DS_GEN);
	mac->action = cpu_to_le16(priv->currentpacketfilter);

	lbs_deb_cmd("MAC_CONTROL: action 0x%x, size %d\n",
		    le16_to_cpu(mac->action), le16_to_cpu(cmd->size));

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

/**
 *  This function inserts command node to cmdfreeq
 *  after cleans it. Requires priv->driver_lock held.
 */
static void __lbs_cleanup_and_insert_cmd(struct lbs_private *priv,
					 struct cmd_ctrl_node *ptempcmd)
{

	if (!ptempcmd)
		return;

	cleanup_cmdnode(ptempcmd);
	list_add_tail(&ptempcmd->list, &priv->cmdfreeq);
}

static void lbs_cleanup_and_insert_cmd(struct lbs_private *priv,
	struct cmd_ctrl_node *ptempcmd)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->driver_lock, flags);
	__lbs_cleanup_and_insert_cmd(priv, ptempcmd);
	spin_unlock_irqrestore(&priv->driver_lock, flags);
}

void lbs_complete_command(struct lbs_private *priv, struct cmd_ctrl_node *cmd,
			  int result)
{
	if (cmd == priv->cur_cmd)
		priv->cur_cmd_retcode = result;
	__lbs_cleanup_and_insert_cmd(priv, cmd);
	priv->cur_cmd = NULL;
}

int lbs_set_radio_control(struct lbs_private *priv)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	ret = lbs_prepare_and_send_command(priv,
				    CMD_802_11_RADIO_CONTROL,
				    CMD_ACT_SET,
				    CMD_OPTION_WAITFORRSP, 0, NULL);

	lbs_deb_cmd("RADIO_SET: radio %d, preamble %d\n",
	       priv->radioon, priv->preamble);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

int lbs_set_mac_packet_filter(struct lbs_private *priv)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	/* Send MAC control command to station */
	ret = lbs_prepare_and_send_command(priv,
				    CMD_MAC_CONTROL, 0, 0, 0, NULL);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function prepare the command before send to firmware.
 *
 *  @param priv		A pointer to struct lbs_private structure
 *  @param cmd_no	command number
 *  @param cmd_action	command action: GET or SET
 *  @param wait_option	wait option: wait response or not
 *  @param cmd_oid	cmd oid: treated as sub command
 *  @param pdata_buf	A pointer to informaion buffer
 *  @return 		0 or -1
 */
int lbs_prepare_and_send_command(struct lbs_private *priv,
			  u16 cmd_no,
			  u16 cmd_action,
			  u16 wait_option, u32 cmd_oid, void *pdata_buf)
{
	int ret = 0;
	struct cmd_ctrl_node *cmdnode;
	struct cmd_ds_command *cmdptr;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_HOST);

	if (!priv) {
		lbs_deb_host("PREP_CMD: priv is NULL\n");
		ret = -1;
		goto done;
	}

	if (priv->surpriseremoved) {
		lbs_deb_host("PREP_CMD: card removed\n");
		ret = -1;
		goto done;
	}

	cmdnode = lbs_get_cmd_ctrl_node(priv);

	if (cmdnode == NULL) {
		lbs_deb_host("PREP_CMD: cmdnode is NULL\n");

		/* Wake up main thread to execute next command */
		wake_up_interruptible(&priv->waitq);
		ret = -1;
		goto done;
	}

	lbs_set_cmd_ctrl_node(priv, cmdnode, pdata_buf);

	cmdptr = (struct cmd_ds_command *)cmdnode->cmdbuf;

	lbs_deb_host("PREP_CMD: command 0x%04x\n", cmd_no);

	/* Set sequence number, command and INT option */
	priv->seqnum++;
	cmdptr->seqnum = cpu_to_le16(priv->seqnum);

	cmdptr->command = cpu_to_le16(cmd_no);
	cmdptr->result = 0;

	switch (cmd_no) {
	case CMD_802_11_PS_MODE:
		ret = lbs_cmd_802_11_ps_mode(priv, cmdptr, cmd_action);
		break;

	case CMD_802_11_SCAN:
		ret = lbs_cmd_80211_scan(priv, cmdptr, pdata_buf);
		break;

	case CMD_MAC_CONTROL:
		ret = lbs_cmd_mac_control(priv, cmdptr);
		break;

	case CMD_802_11_ASSOCIATE:
	case CMD_802_11_REASSOCIATE:
		ret = lbs_cmd_80211_associate(priv, cmdptr, pdata_buf);
		break;

	case CMD_802_11_DEAUTHENTICATE:
		ret = lbs_cmd_80211_deauthenticate(priv, cmdptr);
		break;

	case CMD_802_11_SET_WEP:
		ret = lbs_cmd_802_11_set_wep(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case CMD_802_11_AD_HOC_START:
		ret = lbs_cmd_80211_ad_hoc_start(priv, cmdptr, pdata_buf);
		break;
	case CMD_CODE_DNLD:
		break;

	case CMD_802_11_RESET:
		ret = lbs_cmd_802_11_reset(priv, cmdptr, cmd_action);
		break;

	case CMD_802_11_GET_LOG:
		ret = lbs_cmd_802_11_get_log(priv, cmdptr);
		break;

	case CMD_802_11_AUTHENTICATE:
		ret = lbs_cmd_80211_authenticate(priv, cmdptr, pdata_buf);
		break;

	case CMD_802_11_GET_STAT:
		ret = lbs_cmd_802_11_get_stat(priv, cmdptr);
		break;

	case CMD_802_11_SNMP_MIB:
		ret = lbs_cmd_802_11_snmp_mib(priv, cmdptr,
					       cmd_action, cmd_oid, pdata_buf);
		break;

	case CMD_MAC_REG_ACCESS:
	case CMD_BBP_REG_ACCESS:
	case CMD_RF_REG_ACCESS:
		ret = lbs_cmd_reg_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case CMD_802_11_RF_TX_POWER:
		ret = lbs_cmd_802_11_rf_tx_power(priv, cmdptr,
						  cmd_action, pdata_buf);
		break;

	case CMD_802_11_RADIO_CONTROL:
		ret = lbs_cmd_802_11_radio_control(priv, cmdptr, cmd_action);
		break;

	case CMD_802_11_RATE_ADAPT_RATESET:
		ret = lbs_cmd_802_11_rate_adapt_rateset(priv,
							 cmdptr, cmd_action);
		break;

	case CMD_MAC_MULTICAST_ADR:
		ret = lbs_cmd_mac_multicast_adr(priv, cmdptr, cmd_action);
		break;

	case CMD_802_11_MONITOR_MODE:
		ret = lbs_cmd_802_11_monitor_mode(priv, cmdptr,
				          cmd_action, pdata_buf);
		break;

	case CMD_802_11_AD_HOC_JOIN:
		ret = lbs_cmd_80211_ad_hoc_join(priv, cmdptr, pdata_buf);
		break;

	case CMD_802_11_RSSI:
		ret = lbs_cmd_802_11_rssi(priv, cmdptr);
		break;

	case CMD_802_11_AD_HOC_STOP:
		ret = lbs_cmd_80211_ad_hoc_stop(priv, cmdptr);
		break;

	case CMD_802_11_ENABLE_RSN:
		ret = lbs_cmd_802_11_enable_rsn(priv, cmdptr, cmd_action,
				pdata_buf);
		break;

	case CMD_802_11_KEY_MATERIAL:
		ret = lbs_cmd_802_11_key_material(priv, cmdptr, cmd_action,
				cmd_oid, pdata_buf);
		break;

	case CMD_802_11_PAIRWISE_TSC:
		break;
	case CMD_802_11_GROUP_TSC:
		break;

	case CMD_802_11_MAC_ADDRESS:
		ret = lbs_cmd_802_11_mac_address(priv, cmdptr, cmd_action);
		break;

	case CMD_802_11_EEPROM_ACCESS:
		ret = lbs_cmd_802_11_eeprom_access(priv, cmdptr,
						    cmd_action, pdata_buf);
		break;

	case CMD_802_11_SET_AFC:
	case CMD_802_11_GET_AFC:

		cmdptr->command = cpu_to_le16(cmd_no);
		cmdptr->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_afc) +
					   S_DS_GEN);

		memmove(&cmdptr->params.afc,
			pdata_buf, sizeof(struct cmd_ds_802_11_afc));

		ret = 0;
		goto done;

	case CMD_802_11D_DOMAIN_INFO:
		ret = lbs_cmd_802_11d_domain_info(priv, cmdptr,
						   cmd_no, cmd_action);
		break;

	case CMD_802_11_SLEEP_PARAMS:
		ret = lbs_cmd_802_11_sleep_params(priv, cmdptr, cmd_action);
		break;
	case CMD_802_11_INACTIVITY_TIMEOUT:
		ret = lbs_cmd_802_11_inactivity_timeout(priv, cmdptr,
							 cmd_action, pdata_buf);
		lbs_set_cmd_ctrl_node(priv, cmdnode, pdata_buf);
		break;

	case CMD_802_11_TPC_CFG:
		cmdptr->command = cpu_to_le16(CMD_802_11_TPC_CFG);
		cmdptr->size =
		    cpu_to_le16(sizeof(struct cmd_ds_802_11_tpc_cfg) +
				     S_DS_GEN);

		memmove(&cmdptr->params.tpccfg,
			pdata_buf, sizeof(struct cmd_ds_802_11_tpc_cfg));

		ret = 0;
		break;
	case CMD_802_11_LED_GPIO_CTRL:
		{
			struct mrvlietypes_ledgpio *gpio =
			    (struct mrvlietypes_ledgpio*)
			    cmdptr->params.ledgpio.data;

			memmove(&cmdptr->params.ledgpio,
				pdata_buf,
				sizeof(struct cmd_ds_802_11_led_ctrl));

			cmdptr->command =
			    cpu_to_le16(CMD_802_11_LED_GPIO_CTRL);

#define ACTION_NUMLED_TLVTYPE_LEN_FIELDS_LEN 8
			cmdptr->size =
			    cpu_to_le16(le16_to_cpu(gpio->header.len)
				+ S_DS_GEN
				+ ACTION_NUMLED_TLVTYPE_LEN_FIELDS_LEN);
			gpio->header.len = gpio->header.len;

			ret = 0;
			break;
		}
	case CMD_802_11_SUBSCRIBE_EVENT:
		lbs_cmd_802_11_subscribe_event(priv, cmdptr,
			cmd_action, pdata_buf);
		break;
	case CMD_802_11_PWR_CFG:
		cmdptr->command = cpu_to_le16(CMD_802_11_PWR_CFG);
		cmdptr->size =
		    cpu_to_le16(sizeof(struct cmd_ds_802_11_pwr_cfg) +
				     S_DS_GEN);
		memmove(&cmdptr->params.pwrcfg, pdata_buf,
			sizeof(struct cmd_ds_802_11_pwr_cfg));

		ret = 0;
		break;
	case CMD_BT_ACCESS:
		ret = lbs_cmd_bt_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case CMD_FWT_ACCESS:
		ret = lbs_cmd_fwt_access(priv, cmdptr, cmd_action, pdata_buf);
		break;

	case CMD_GET_TSF:
		cmdptr->command = cpu_to_le16(CMD_GET_TSF);
		cmdptr->size = cpu_to_le16(sizeof(struct cmd_ds_get_tsf) +
					   S_DS_GEN);
		ret = 0;
		break;
	case CMD_802_11_BEACON_CTRL:
		ret = lbs_cmd_bcn_ctrl(priv, cmdptr, cmd_action);
		break;
	default:
		lbs_deb_host("PREP_CMD: unknown command 0x%04x\n", cmd_no);
		ret = -1;
		break;
	}

	/* return error, since the command preparation failed */
	if (ret != 0) {
		lbs_deb_host("PREP_CMD: command preparation failed\n");
		lbs_cleanup_and_insert_cmd(priv, cmdnode);
		ret = -1;
		goto done;
	}

	cmdnode->cmdwaitqwoken = 0;

	lbs_queue_cmd(priv, cmdnode, 1);
	wake_up_interruptible(&priv->waitq);

	if (wait_option & CMD_OPTION_WAITFORRSP) {
		lbs_deb_host("PREP_CMD: wait for response\n");
		might_sleep();
		wait_event_interruptible(cmdnode->cmdwait_q,
					 cmdnode->cmdwaitqwoken);
	}

	spin_lock_irqsave(&priv->driver_lock, flags);
	if (priv->cur_cmd_retcode) {
		lbs_deb_host("PREP_CMD: command failed with return code %d\n",
		       priv->cur_cmd_retcode);
		priv->cur_cmd_retcode = 0;
		ret = -1;
	}
	spin_unlock_irqrestore(&priv->driver_lock, flags);

done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(lbs_prepare_and_send_command);

/**
 *  @brief This function allocates the command buffer and link
 *  it to command free queue.
 *
 *  @param priv		A pointer to struct lbs_private structure
 *  @return 		0 or -1
 */
int lbs_allocate_cmd_buffer(struct lbs_private *priv)
{
	int ret = 0;
	u32 bufsize;
	u32 i;
	struct cmd_ctrl_node *cmdarray;

	lbs_deb_enter(LBS_DEB_HOST);

	/* Allocate and initialize the command array */
	bufsize = sizeof(struct cmd_ctrl_node) * LBS_NUM_CMD_BUFFERS;
	if (!(cmdarray = kzalloc(bufsize, GFP_KERNEL))) {
		lbs_deb_host("ALLOC_CMD_BUF: tempcmd_array is NULL\n");
		ret = -1;
		goto done;
	}
	priv->cmd_array = cmdarray;

	/* Allocate and initialize each command buffer in the command array */
	for (i = 0; i < LBS_NUM_CMD_BUFFERS; i++) {
		cmdarray[i].cmdbuf = kzalloc(LBS_CMD_BUFFER_SIZE, GFP_KERNEL);
		if (!cmdarray[i].cmdbuf) {
			lbs_deb_host("ALLOC_CMD_BUF: ptempvirtualaddr is NULL\n");
			ret = -1;
			goto done;
		}
	}

	for (i = 0; i < LBS_NUM_CMD_BUFFERS; i++) {
		init_waitqueue_head(&cmdarray[i].cmdwait_q);
		lbs_cleanup_and_insert_cmd(priv, &cmdarray[i]);
	}
	ret = 0;

done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function frees the command buffer.
 *
 *  @param priv		A pointer to struct lbs_private structure
 *  @return 		0 or -1
 */
int lbs_free_cmd_buffer(struct lbs_private *priv)
{
	struct cmd_ctrl_node *cmdarray;
	unsigned int i;

	lbs_deb_enter(LBS_DEB_HOST);

	/* need to check if cmd array is allocated or not */
	if (priv->cmd_array == NULL) {
		lbs_deb_host("FREE_CMD_BUF: cmd_array is NULL\n");
		goto done;
	}

	cmdarray = priv->cmd_array;

	/* Release shared memory buffers */
	for (i = 0; i < LBS_NUM_CMD_BUFFERS; i++) {
		if (cmdarray[i].cmdbuf) {
			kfree(cmdarray[i].cmdbuf);
			cmdarray[i].cmdbuf = NULL;
		}
	}

	/* Release cmd_ctrl_node */
	if (priv->cmd_array) {
		kfree(priv->cmd_array);
		priv->cmd_array = NULL;
	}

done:
	lbs_deb_leave(LBS_DEB_HOST);
	return 0;
}

/**
 *  @brief This function gets a free command node if available in
 *  command free queue.
 *
 *  @param priv		A pointer to struct lbs_private structure
 *  @return cmd_ctrl_node A pointer to cmd_ctrl_node structure or NULL
 */
static struct cmd_ctrl_node *lbs_get_cmd_ctrl_node(struct lbs_private *priv)
{
	struct cmd_ctrl_node *tempnode;
	unsigned long flags;

	lbs_deb_enter(LBS_DEB_HOST);

	if (!priv)
		return NULL;

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (!list_empty(&priv->cmdfreeq)) {
		tempnode = list_first_entry(&priv->cmdfreeq,
					    struct cmd_ctrl_node, list);
		list_del(&tempnode->list);
	} else {
		lbs_deb_host("GET_CMD_NODE: cmd_ctrl_node is not available\n");
		tempnode = NULL;
	}

	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (tempnode)
		cleanup_cmdnode(tempnode);

	lbs_deb_leave(LBS_DEB_HOST);
	return tempnode;
}

/**
 *  @brief This function cleans command node.
 *
 *  @param ptempnode	A pointer to cmdCtrlNode structure
 *  @return 		n/a
 */
static void cleanup_cmdnode(struct cmd_ctrl_node *cmdnode)
{
	lbs_deb_enter(LBS_DEB_HOST);

	if (!cmdnode)
		return;
	cmdnode->cmdwaitqwoken = 1;
	wake_up_interruptible(&cmdnode->cmdwait_q);
	cmdnode->pdata_buf = NULL;
	cmdnode->callback = NULL;
	cmdnode->callback_arg = 0;

	memset(cmdnode->cmdbuf, 0, LBS_CMD_BUFFER_SIZE);

	lbs_deb_leave(LBS_DEB_HOST);
}

/**
 *  @brief This function initializes the command node.
 *
 *  @param priv		A pointer to struct lbs_private structure
 *  @param ptempnode	A pointer to cmd_ctrl_node structure
 *  @param pdata_buf	A pointer to informaion buffer
 *  @return 		0 or -1
 */
static void lbs_set_cmd_ctrl_node(struct lbs_private *priv,
				  struct cmd_ctrl_node *ptempnode,
				  void *pdata_buf)
{
	lbs_deb_enter(LBS_DEB_HOST);

	if (!ptempnode)
		return;

	ptempnode->pdata_buf = pdata_buf;
	ptempnode->callback = NULL;
	ptempnode->callback_arg = 0;

	lbs_deb_leave(LBS_DEB_HOST);
}

/**
 *  @brief This function executes next command in command
 *  pending queue. It will put fimware back to PS mode
 *  if applicable.
 *
 *  @param priv     A pointer to struct lbs_private structure
 *  @return 	   0 or -1
 */
int lbs_execute_next_command(struct lbs_private *priv)
{
	struct cmd_ctrl_node *cmdnode = NULL;
	struct cmd_header *cmd;
	unsigned long flags;
	int ret = 0;

	// Debug group is LBS_DEB_THREAD and not LBS_DEB_HOST, because the
	// only caller to us is lbs_thread() and we get even when a
	// data packet is received
	lbs_deb_enter(LBS_DEB_THREAD);

	spin_lock_irqsave(&priv->driver_lock, flags);

	if (priv->cur_cmd) {
		lbs_pr_alert( "EXEC_NEXT_CMD: already processing command!\n");
		spin_unlock_irqrestore(&priv->driver_lock, flags);
		ret = -1;
		goto done;
	}

	if (!list_empty(&priv->cmdpendingq)) {
		cmdnode = list_first_entry(&priv->cmdpendingq,
					   struct cmd_ctrl_node, list);
	}

	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (cmdnode) {
		cmd = cmdnode->cmdbuf;

		if (is_command_allowed_in_ps(le16_to_cpu(cmd->command))) {
			if ((priv->psstate == PS_STATE_SLEEP) ||
			    (priv->psstate == PS_STATE_PRE_SLEEP)) {
				lbs_deb_host(
				       "EXEC_NEXT_CMD: cannot send cmd 0x%04x in psstate %d\n",
				       le16_to_cpu(cmd->command),
				       priv->psstate);
				ret = -1;
				goto done;
			}
			lbs_deb_host("EXEC_NEXT_CMD: OK to send command "
				     "0x%04x in psstate %d\n",
				     le16_to_cpu(cmd->command), priv->psstate);
		} else if (priv->psstate != PS_STATE_FULL_POWER) {
			/*
			 * 1. Non-PS command:
			 * Queue it. set needtowakeup to TRUE if current state
			 * is SLEEP, otherwise call lbs_ps_wakeup to send Exit_PS.
			 * 2. PS command but not Exit_PS:
			 * Ignore it.
			 * 3. PS command Exit_PS:
			 * Set needtowakeup to TRUE if current state is SLEEP,
			 * otherwise send this command down to firmware
			 * immediately.
			 */
			if (cmd->command != cpu_to_le16(CMD_802_11_PS_MODE)) {
				/*  Prepare to send Exit PS,
				 *  this non PS command will be sent later */
				if ((priv->psstate == PS_STATE_SLEEP)
				    || (priv->psstate == PS_STATE_PRE_SLEEP)
				    ) {
					/* w/ new scheme, it will not reach here.
					   since it is blocked in main_thread. */
					priv->needtowakeup = 1;
				} else
					lbs_ps_wakeup(priv, 0);

				ret = 0;
				goto done;
			} else {
				/*
				 * PS command. Ignore it if it is not Exit_PS.
				 * otherwise send it down immediately.
				 */
				struct cmd_ds_802_11_ps_mode *psm = (void *)cmd;

				lbs_deb_host(
				       "EXEC_NEXT_CMD: PS cmd, action 0x%02x\n",
				       psm->action);
				if (psm->action !=
				    cpu_to_le16(CMD_SUBCMD_EXIT_PS)) {
					lbs_deb_host(
					       "EXEC_NEXT_CMD: ignore ENTER_PS cmd\n");
					list_del(&cmdnode->list);
					spin_lock_irqsave(&priv->driver_lock, flags);
					lbs_complete_command(priv, cmdnode, 0);
					spin_unlock_irqrestore(&priv->driver_lock, flags);

					ret = 0;
					goto done;
				}

				if ((priv->psstate == PS_STATE_SLEEP) ||
				    (priv->psstate == PS_STATE_PRE_SLEEP)) {
					lbs_deb_host(
					       "EXEC_NEXT_CMD: ignore EXIT_PS cmd in sleep\n");
					list_del(&cmdnode->list);
					spin_lock_irqsave(&priv->driver_lock, flags);
					lbs_complete_command(priv, cmdnode, 0);
					spin_unlock_irqrestore(&priv->driver_lock, flags);
					priv->needtowakeup = 1;

					ret = 0;
					goto done;
				}

				lbs_deb_host(
				       "EXEC_NEXT_CMD: sending EXIT_PS\n");
			}
		}
		list_del(&cmdnode->list);
		lbs_deb_host("EXEC_NEXT_CMD: sending command 0x%04x\n",
			    le16_to_cpu(cmd->command));
		lbs_submit_command(priv, cmdnode);
	} else {
		/*
		 * check if in power save mode, if yes, put the device back
		 * to PS mode
		 */
		if ((priv->psmode != LBS802_11POWERMODECAM) &&
		    (priv->psstate == PS_STATE_FULL_POWER) &&
		    ((priv->connect_status == LBS_CONNECTED) ||
		    (priv->mesh_connect_status == LBS_CONNECTED))) {
			if (priv->secinfo.WPAenabled ||
			    priv->secinfo.WPA2enabled) {
				/* check for valid WPA group keys */
				if (priv->wpa_mcast_key.len ||
				    priv->wpa_unicast_key.len) {
					lbs_deb_host(
					       "EXEC_NEXT_CMD: WPA enabled and GTK_SET"
					       " go back to PS_SLEEP");
					lbs_ps_sleep(priv, 0);
				}
			} else {
				lbs_deb_host(
				       "EXEC_NEXT_CMD: cmdpendingq empty, "
				       "go back to PS_SLEEP");
				lbs_ps_sleep(priv, 0);
			}
		}
	}

	ret = 0;
done:
	lbs_deb_leave(LBS_DEB_THREAD);
	return ret;
}

void lbs_send_iwevcustom_event(struct lbs_private *priv, s8 *str)
{
	union iwreq_data iwrq;
	u8 buf[50];

	lbs_deb_enter(LBS_DEB_WEXT);

	memset(&iwrq, 0, sizeof(union iwreq_data));
	memset(buf, 0, sizeof(buf));

	snprintf(buf, sizeof(buf) - 1, "%s", str);

	iwrq.data.length = strlen(buf) + 1 + IW_EV_LCP_LEN;

	/* Send Event to upper layer */
	lbs_deb_wext("event indication string %s\n", (char *)buf);
	lbs_deb_wext("event indication length %d\n", iwrq.data.length);
	lbs_deb_wext("sending wireless event IWEVCUSTOM for %s\n", str);

	wireless_send_event(priv->dev, IWEVCUSTOM, &iwrq, buf);

	lbs_deb_leave(LBS_DEB_WEXT);
}

static int sendconfirmsleep(struct lbs_private *priv, u8 *cmdptr, u16 size)
{
	unsigned long flags;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_HOST);

	lbs_deb_host("SEND_SLEEPC_CMD: before download, cmd size %d\n",
	       size);

	lbs_deb_hex(LBS_DEB_HOST, "sleep confirm command", cmdptr, size);

	ret = priv->hw_host_to_card(priv, MVMS_CMD, cmdptr, size);
	priv->dnld_sent = DNLD_RES_RECEIVED;

	spin_lock_irqsave(&priv->driver_lock, flags);
	if (priv->intcounter || priv->currenttxskb)
		lbs_deb_host("SEND_SLEEPC_CMD: intcounter %d, currenttxskb %p\n",
		       priv->intcounter, priv->currenttxskb);
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (ret) {
		lbs_pr_alert(
		       "SEND_SLEEPC_CMD: Host to Card failed for Confirm Sleep\n");
	} else {
		spin_lock_irqsave(&priv->driver_lock, flags);
		if (!priv->intcounter) {
			priv->psstate = PS_STATE_SLEEP;
		} else {
			lbs_deb_host("SEND_SLEEPC_CMD: after sent, intcounter %d\n",
			       priv->intcounter);
		}
		spin_unlock_irqrestore(&priv->driver_lock, flags);

		lbs_deb_host("SEND_SLEEPC_CMD: sent confirm sleep\n");
	}

	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}

void lbs_ps_sleep(struct lbs_private *priv, int wait_option)
{
	lbs_deb_enter(LBS_DEB_HOST);

	/*
	 * PS is currently supported only in Infrastructure mode
	 * Remove this check if it is to be supported in IBSS mode also
	 */

	lbs_prepare_and_send_command(priv, CMD_802_11_PS_MODE,
			      CMD_SUBCMD_ENTER_PS, wait_option, 0, NULL);

	lbs_deb_leave(LBS_DEB_HOST);
}

/**
 *  @brief This function sends Exit_PS command to firmware.
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *  @param wait_option	wait response or not
 *  @return 	   	n/a
 */
void lbs_ps_wakeup(struct lbs_private *priv, int wait_option)
{
	__le32 Localpsmode;

	lbs_deb_enter(LBS_DEB_HOST);

	Localpsmode = cpu_to_le32(LBS802_11POWERMODECAM);

	lbs_prepare_and_send_command(priv, CMD_802_11_PS_MODE,
			      CMD_SUBCMD_EXIT_PS,
			      wait_option, 0, &Localpsmode);

	lbs_deb_leave(LBS_DEB_HOST);
}

/**
 *  @brief This function checks condition and prepares to
 *  send sleep confirm command to firmware if ok.
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *  @param psmode  	Power Saving mode
 *  @return 	   	n/a
 */
void lbs_ps_confirm_sleep(struct lbs_private *priv, u16 psmode)
{
	unsigned long flags =0;
	u8 allowed = 1;

	lbs_deb_enter(LBS_DEB_HOST);

	if (priv->dnld_sent) {
		allowed = 0;
		lbs_deb_host("dnld_sent was set\n");
	}

	spin_lock_irqsave(&priv->driver_lock, flags);
	if (priv->cur_cmd) {
		allowed = 0;
		lbs_deb_host("cur_cmd was set\n");
	}
	if (priv->intcounter > 0) {
		allowed = 0;
		lbs_deb_host("intcounter %d\n", priv->intcounter);
	}
	spin_unlock_irqrestore(&priv->driver_lock, flags);

	if (allowed) {
		lbs_deb_host("sending lbs_ps_confirm_sleep\n");
		sendconfirmsleep(priv, (u8 *) & priv->lbs_ps_confirm_sleep,
				 sizeof(struct PS_CMD_ConfirmSleep));
	} else {
		lbs_deb_host("sleep confirm has been delayed\n");
	}

	lbs_deb_leave(LBS_DEB_HOST);
}


/**
 *  @brief Simple callback that copies response back into command
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *  @param extra  	A pointer to the original command structure for which
 *                      'resp' is a response
 *  @param resp         A pointer to the command response
 *
 *  @return 	   	0 on success, error on failure
 */
int lbs_cmd_copyback(struct lbs_private *priv, unsigned long extra,
		     struct cmd_header *resp)
{
	struct cmd_header *buf = (void *)extra;
	uint16_t copy_len;

	lbs_deb_enter(LBS_DEB_CMD);

	copy_len = min(le16_to_cpu(buf->size), le16_to_cpu(resp->size));
	lbs_deb_cmd("Copying back %u bytes; command response was %u bytes, "
		    "copy back buffer was %u bytes\n", copy_len,
		    le16_to_cpu(resp->size), le16_to_cpu(buf->size));
	memcpy(buf, resp, copy_len);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}
EXPORT_SYMBOL_GPL(lbs_cmd_copyback);

/**
 *  @brief Simple way to call firmware functions
 *
 *  @param priv    	A pointer to struct lbs_private structure
 *  @param psmode  	one of the many CMD_802_11_xxxx
 *  @param cmd          pointer to the parameters structure for above command
 *                      (this should not include the command, size, sequence
 *                      and result fields from struct cmd_ds_gen)
 *  @param cmd_size     size structure pointed to by cmd
 *  @param rsp          pointer to an area where the result should be placed
 *  @param rsp_size     pointer to the size of the rsp area. If the firmware
 *                      returns fewer bytes, then this *rsp_size will be
 *                      changed to the actual size.
 *  @return 	   	-1 in case of a higher level error, otherwise
 *                      the result code from the firmware
 */
int __lbs_cmd(struct lbs_private *priv, uint16_t command,
	      struct cmd_header *in_cmd, int in_cmd_size,
	      int (*callback)(struct lbs_private *, unsigned long, struct cmd_header *),
	      unsigned long callback_arg)
{
	struct cmd_ctrl_node *cmdnode;
	unsigned long flags;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_HOST);

	if (!priv) {
		lbs_deb_host("PREP_CMD: priv is NULL\n");
		ret = -1;
		goto done;
	}

	if (priv->surpriseremoved) {
		lbs_deb_host("PREP_CMD: card removed\n");
		ret = -1;
		goto done;
	}

	cmdnode = lbs_get_cmd_ctrl_node(priv);
	if (cmdnode == NULL) {
		lbs_deb_host("PREP_CMD: cmdnode is NULL\n");

		/* Wake up main thread to execute next command */
		wake_up_interruptible(&priv->waitq);
		ret = -1;
		goto done;
	}

	cmdnode->callback = callback;
	cmdnode->callback_arg = callback_arg;

	/* Copy the incoming command to the buffer */
	memcpy(cmdnode->cmdbuf, in_cmd, in_cmd_size);

	/* Set sequence number, clean result, move to buffer */
	priv->seqnum++;
	cmdnode->cmdbuf->command = cpu_to_le16(command);
	cmdnode->cmdbuf->size    = cpu_to_le16(in_cmd_size);
	cmdnode->cmdbuf->seqnum  = cpu_to_le16(priv->seqnum);
	cmdnode->cmdbuf->result  = 0;

	lbs_deb_host("PREP_CMD: command 0x%04x\n", command);

	/* here was the big old switch() statement, which is now obsolete,
	 * because the caller of lbs_cmd() sets up all of *cmd for us. */

	cmdnode->cmdwaitqwoken = 0;
	lbs_queue_cmd(priv, cmdnode, 1);
	wake_up_interruptible(&priv->waitq);

	might_sleep();
	wait_event_interruptible(cmdnode->cmdwait_q, cmdnode->cmdwaitqwoken);

	spin_lock_irqsave(&priv->driver_lock, flags);
	if (priv->cur_cmd_retcode) {
		lbs_deb_host("PREP_CMD: command failed with return code %d\n",
		       priv->cur_cmd_retcode);
		priv->cur_cmd_retcode = 0;
		ret = -1;
	}
	spin_unlock_irqrestore(&priv->driver_lock, flags);

done:
	lbs_deb_leave_args(LBS_DEB_HOST, "ret %d", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(__lbs_cmd);


