/* Copyright (C) 2006, Red Hat, Inc. */

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/if_arp.h>
#include <net/lib80211.h>

#include "assoc.h"
#include "decl.h"
#include "host.h"
#include "scan.h"
#include "cmd.h"

static const u8 bssid_any[ETH_ALEN]  __attribute__ ((aligned (2))) =
	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 bssid_off[ETH_ALEN]  __attribute__ ((aligned (2))) =
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

/* The firmware needs the following bits masked out of the beacon-derived
 * capability field when associating/joining to a BSS:
 *  9 (QoS), 11 (APSD), 12 (unused), 14 (unused), 15 (unused)
 */
#define CAPINFO_MASK	(~(0xda00))

/**
 * 802.11b/g supported bitrates (in 500Kb/s units)
 */
u8 lbs_bg_rates[MAX_RATES] =
    { 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c,
0x00, 0x00 };


/**
 *  @brief This function finds common rates between rates and card rates.
 *
 * It will fill common rates in rates as output if found.
 *
 * NOTE: Setting the MSB of the basic rates need to be taken
 *   care, either before or after calling this function
 *
 *  @param priv     A pointer to struct lbs_private structure
 *  @param rates       the buffer which keeps input and output
 *  @param rates_size  the size of rates buffer; new size of buffer on return,
 *                     which will be less than or equal to original rates_size
 *
 *  @return            0 on success, or -1 on error
 */
static int get_common_rates(struct lbs_private *priv,
	u8 *rates,
	u16 *rates_size)
{
	int i, j;
	u8 intersection[MAX_RATES];
	u16 intersection_size;
	u16 num_rates = 0;

	intersection_size = min_t(u16, *rates_size, ARRAY_SIZE(intersection));

	/* Allow each rate from 'rates' that is supported by the hardware */
	for (i = 0; i < ARRAY_SIZE(lbs_bg_rates) && lbs_bg_rates[i]; i++) {
		for (j = 0; j < intersection_size && rates[j]; j++) {
			if (rates[j] == lbs_bg_rates[i])
				intersection[num_rates++] = rates[j];
		}
	}

	lbs_deb_hex(LBS_DEB_JOIN, "AP rates    ", rates, *rates_size);
	lbs_deb_hex(LBS_DEB_JOIN, "card rates  ", lbs_bg_rates,
			ARRAY_SIZE(lbs_bg_rates));
	lbs_deb_hex(LBS_DEB_JOIN, "common rates", intersection, num_rates);
	lbs_deb_join("TX data rate 0x%02x\n", priv->cur_rate);

	if (!priv->enablehwauto) {
		for (i = 0; i < num_rates; i++) {
			if (intersection[i] == priv->cur_rate)
				goto done;
		}
		lbs_pr_alert("Previously set fixed data rate %#x isn't "
		       "compatible with the network.\n", priv->cur_rate);
		return -1;
	}

done:
	memset(rates, 0, *rates_size);
	*rates_size = num_rates;
	memcpy(rates, intersection, num_rates);
	return 0;
}


/**
 *  @brief Sets the MSB on basic rates as the firmware requires
 *
 * Scan through an array and set the MSB for basic data rates.
 *
 *  @param rates     buffer of data rates
 *  @param len       size of buffer
 */
static void lbs_set_basic_rate_flags(u8 *rates, size_t len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (rates[i] == 0x02 || rates[i] == 0x04 ||
		    rates[i] == 0x0b || rates[i] == 0x16)
			rates[i] |= 0x80;
	}
}


static u8 iw_auth_to_ieee_auth(u8 auth)
{
	if (auth == IW_AUTH_ALG_OPEN_SYSTEM)
		return 0x00;
	else if (auth == IW_AUTH_ALG_SHARED_KEY)
		return 0x01;
	else if (auth == IW_AUTH_ALG_LEAP)
		return 0x80;

	lbs_deb_join("%s: invalid auth alg 0x%X\n", __func__, auth);
	return 0;
}

/**
 *  @brief This function prepares the authenticate command.  AUTHENTICATE only
 *  sets the authentication suite for future associations, as the firmware
 *  handles authentication internally during the ASSOCIATE command.
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param bssid     The peer BSSID with which to authenticate
 *  @param auth      The authentication mode to use (from wireless.h)
 *
 *  @return         0 or -1
 */
static int lbs_set_authentication(struct lbs_private *priv, u8 bssid[6], u8 auth)
{
	struct cmd_ds_802_11_authenticate cmd;
	int ret = -1;

	lbs_deb_enter(LBS_DEB_JOIN);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.bssid, bssid, ETH_ALEN);

	cmd.authtype = iw_auth_to_ieee_auth(auth);

	lbs_deb_join("AUTH_CMD: BSSID %pM, auth 0x%x\n", bssid, cmd.authtype);

	ret = lbs_cmd_with_response(priv, CMD_802_11_AUTHENTICATE, &cmd);

	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}


int lbs_cmd_802_11_set_wep(struct lbs_private *priv, uint16_t cmd_action,
			   struct assoc_request *assoc)
{
	struct cmd_ds_802_11_set_wep cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.command = cpu_to_le16(CMD_802_11_SET_WEP);
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	cmd.action = cpu_to_le16(cmd_action);

	if (cmd_action == CMD_ACT_ADD) {
		int i;

		/* default tx key index */
		cmd.keyindex = cpu_to_le16(assoc->wep_tx_keyidx &
					   CMD_WEP_KEY_INDEX_MASK);

		/* Copy key types and material to host command structure */
		for (i = 0; i < 4; i++) {
			struct enc_key *pkey = &assoc->wep_keys[i];

			switch (pkey->len) {
			case KEY_LEN_WEP_40:
				cmd.keytype[i] = CMD_TYPE_WEP_40_BIT;
				memmove(cmd.keymaterial[i], pkey->key, pkey->len);
				lbs_deb_cmd("SET_WEP: add key %d (40 bit)\n", i);
				break;
			case KEY_LEN_WEP_104:
				cmd.keytype[i] = CMD_TYPE_WEP_104_BIT;
				memmove(cmd.keymaterial[i], pkey->key, pkey->len);
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
	} else if (cmd_action == CMD_ACT_REMOVE) {
		/* ACT_REMOVE clears _all_ WEP keys */

		/* default tx key index */
		cmd.keyindex = cpu_to_le16(priv->wep_tx_keyidx &
					   CMD_WEP_KEY_INDEX_MASK);
		lbs_deb_cmd("SET_WEP: remove key %d\n", priv->wep_tx_keyidx);
	}

	ret = lbs_cmd_with_response(priv, CMD_802_11_SET_WEP, &cmd);
done:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

int lbs_cmd_802_11_enable_rsn(struct lbs_private *priv, uint16_t cmd_action,
			      uint16_t *enable)
{
	struct cmd_ds_802_11_enable_rsn cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(cmd_action);

	if (cmd_action == CMD_ACT_GET)
		cmd.enable = 0;
	else {
		if (*enable)
			cmd.enable = cpu_to_le16(CMD_ENABLE_RSN);
		else
			cmd.enable = cpu_to_le16(CMD_DISABLE_RSN);
		lbs_deb_cmd("ENABLE_RSN: %d\n", *enable);
	}

	ret = lbs_cmd_with_response(priv, CMD_802_11_ENABLE_RSN, &cmd);
	if (!ret && cmd_action == CMD_ACT_GET)
		*enable = le16_to_cpu(cmd.enable);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static void set_one_wpa_key(struct MrvlIEtype_keyParamSet *keyparam,
		struct enc_key *key)
{
	lbs_deb_enter(LBS_DEB_CMD);

	if (key->flags & KEY_INFO_WPA_ENABLED)
		keyparam->keyinfo |= cpu_to_le16(KEY_INFO_WPA_ENABLED);
	if (key->flags & KEY_INFO_WPA_UNICAST)
		keyparam->keyinfo |= cpu_to_le16(KEY_INFO_WPA_UNICAST);
	if (key->flags & KEY_INFO_WPA_MCAST)
		keyparam->keyinfo |= cpu_to_le16(KEY_INFO_WPA_MCAST);

	keyparam->type = cpu_to_le16(TLV_TYPE_KEY_MATERIAL);
	keyparam->keytypeid = cpu_to_le16(key->type);
	keyparam->keylen = cpu_to_le16(key->len);
	memcpy(keyparam->key, key->key, key->len);

	/* Length field doesn't include the {type,length} header */
	keyparam->length = cpu_to_le16(sizeof(*keyparam) - 4);
	lbs_deb_leave(LBS_DEB_CMD);
}

int lbs_cmd_802_11_key_material(struct lbs_private *priv, uint16_t cmd_action,
				struct assoc_request *assoc)
{
	struct cmd_ds_802_11_key_material cmd;
	int ret = 0;
	int index = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	cmd.action = cpu_to_le16(cmd_action);
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	if (cmd_action == CMD_ACT_GET) {
		cmd.hdr.size = cpu_to_le16(sizeof(struct cmd_header) + 2);
	} else {
		memset(cmd.keyParamSet, 0, sizeof(cmd.keyParamSet));

		if (test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc->flags)) {
			set_one_wpa_key(&cmd.keyParamSet[index],
					&assoc->wpa_unicast_key);
			index++;
		}

		if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc->flags)) {
			set_one_wpa_key(&cmd.keyParamSet[index],
					&assoc->wpa_mcast_key);
			index++;
		}

		/* The common header and as many keys as we included */
		cmd.hdr.size = cpu_to_le16(offsetof(typeof(cmd),
						    keyParamSet[index]));
	}
	ret = lbs_cmd_with_response(priv, CMD_802_11_KEY_MATERIAL, &cmd);
	/* Copy the returned key to driver private data */
	if (!ret && cmd_action == CMD_ACT_GET) {
		void *buf_ptr = cmd.keyParamSet;
		void *resp_end = &(&cmd)[1];

		while (buf_ptr < resp_end) {
			struct MrvlIEtype_keyParamSet *keyparam = buf_ptr;
			struct enc_key *key;
			uint16_t param_set_len = le16_to_cpu(keyparam->length);
			uint16_t key_len = le16_to_cpu(keyparam->keylen);
			uint16_t key_flags = le16_to_cpu(keyparam->keyinfo);
			uint16_t key_type = le16_to_cpu(keyparam->keytypeid);
			void *end;

			end = (void *)keyparam + sizeof(keyparam->type)
				+ sizeof(keyparam->length) + param_set_len;

			/* Make sure we don't access past the end of the IEs */
			if (end > resp_end)
				break;

			if (key_flags & KEY_INFO_WPA_UNICAST)
				key = &priv->wpa_unicast_key;
			else if (key_flags & KEY_INFO_WPA_MCAST)
				key = &priv->wpa_mcast_key;
			else
				break;

			/* Copy returned key into driver */
			memset(key, 0, sizeof(struct enc_key));
			if (key_len > sizeof(key->key))
				break;
			key->type = key_type;
			key->flags = key_flags;
			key->len = key_len;
			memcpy(key->key, keyparam->key, key->len);

			buf_ptr = end + 1;
		}
	}

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

static __le16 lbs_rate_to_fw_bitmap(int rate, int lower_rates_ok)
{
/*		Bit  	Rate
*		15:13 Reserved
*		12    54 Mbps
*		11    48 Mbps
*		10    36 Mbps
*		9     24 Mbps
*		8     18 Mbps
*		7     12 Mbps
*		6     9 Mbps
*		5     6 Mbps
*		4     Reserved
*		3     11 Mbps
*		2     5.5 Mbps
*		1     2 Mbps
*		0     1 Mbps
**/

	uint16_t ratemask;
	int i = lbs_data_rate_to_fw_index(rate);
	if (lower_rates_ok)
		ratemask = (0x1fef >> (12 - i));
	else
		ratemask = (1 << i);
	return cpu_to_le16(ratemask);
}

int lbs_cmd_802_11_rate_adapt_rateset(struct lbs_private *priv,
				      uint16_t cmd_action)
{
	struct cmd_ds_802_11_rate_adapt_rateset cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_CMD);

	if (!priv->cur_rate && !priv->enablehwauto)
		return -EINVAL;

	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	cmd.action = cpu_to_le16(cmd_action);
	cmd.enablehwauto = cpu_to_le16(priv->enablehwauto);
	cmd.bitmap = lbs_rate_to_fw_bitmap(priv->cur_rate, priv->enablehwauto);
	ret = lbs_cmd_with_response(priv, CMD_802_11_RATE_ADAPT_RATESET, &cmd);
	if (!ret && cmd_action == CMD_ACT_GET)
		priv->enablehwauto = le16_to_cpu(cmd.enablehwauto);

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

	lbs_deb_hex(LBS_DEB_CMD, "DATA_RATE_RESP", (u8 *) &cmd, sizeof(cmd));

	/* FIXME: get actual rates FW can do if this command actually returns
	 * all data rates supported.
	 */
	priv->cur_rate = lbs_fw_index_to_data_rate(cmd.rates[0]);
	lbs_deb_cmd("DATA_RATE: current rate is 0x%02x\n", priv->cur_rate);

out:
	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}


int lbs_cmd_802_11_rssi(struct lbs_private *priv,
				struct cmd_ds_command *cmd)
{

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->command = cpu_to_le16(CMD_802_11_RSSI);
	cmd->size = cpu_to_le16(sizeof(struct cmd_ds_802_11_rssi) +
		sizeof(struct cmd_header));
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

int lbs_ret_802_11_rssi(struct lbs_private *priv,
				struct cmd_ds_command *resp)
{
	struct cmd_ds_802_11_rssi_rsp *rssirsp = &resp->params.rssirsp;

	lbs_deb_enter(LBS_DEB_CMD);

	/* store the non average value */
	priv->SNR[TYPE_BEACON][TYPE_NOAVG] = get_unaligned_le16(&rssirsp->SNR);
	priv->NF[TYPE_BEACON][TYPE_NOAVG] =
		get_unaligned_le16(&rssirsp->noisefloor);

	priv->SNR[TYPE_BEACON][TYPE_AVG] = get_unaligned_le16(&rssirsp->avgSNR);
	priv->NF[TYPE_BEACON][TYPE_AVG] =
		get_unaligned_le16(&rssirsp->avgnoisefloor);

	priv->RSSI[TYPE_BEACON][TYPE_NOAVG] =
	    CAL_RSSI(priv->SNR[TYPE_BEACON][TYPE_NOAVG],
		     priv->NF[TYPE_BEACON][TYPE_NOAVG]);

	priv->RSSI[TYPE_BEACON][TYPE_AVG] =
	    CAL_RSSI(priv->SNR[TYPE_BEACON][TYPE_AVG] / AVG_SCALE,
		     priv->NF[TYPE_BEACON][TYPE_AVG] / AVG_SCALE);

	lbs_deb_cmd("RSSI: beacon %d, avg %d\n",
	       priv->RSSI[TYPE_BEACON][TYPE_NOAVG],
	       priv->RSSI[TYPE_BEACON][TYPE_AVG]);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}


int lbs_cmd_bcn_ctrl(struct lbs_private *priv,
				struct cmd_ds_command *cmd,
				u16 cmd_action)
{
	struct cmd_ds_802_11_beacon_control
		*bcn_ctrl = &cmd->params.bcn_ctrl;

	lbs_deb_enter(LBS_DEB_CMD);
	cmd->size =
	    cpu_to_le16(sizeof(struct cmd_ds_802_11_beacon_control)
			     + sizeof(struct cmd_header));
	cmd->command = cpu_to_le16(CMD_802_11_BEACON_CTRL);

	bcn_ctrl->action = cpu_to_le16(cmd_action);
	bcn_ctrl->beacon_enable = cpu_to_le16(priv->beacon_enable);
	bcn_ctrl->beacon_period = cpu_to_le16(priv->beacon_period);

	lbs_deb_leave(LBS_DEB_CMD);
	return 0;
}

int lbs_ret_802_11_bcn_ctrl(struct lbs_private *priv,
					struct cmd_ds_command *resp)
{
	struct cmd_ds_802_11_beacon_control *bcn_ctrl =
	    &resp->params.bcn_ctrl;

	lbs_deb_enter(LBS_DEB_CMD);

	if (bcn_ctrl->action == CMD_ACT_GET) {
		priv->beacon_enable = (u8) le16_to_cpu(bcn_ctrl->beacon_enable);
		priv->beacon_period = le16_to_cpu(bcn_ctrl->beacon_period);
	}

	lbs_deb_enter(LBS_DEB_CMD);
	return 0;
}



static int lbs_assoc_post(struct lbs_private *priv,
			  struct cmd_ds_802_11_associate_response *resp)
{
	int ret = 0;
	union iwreq_data wrqu;
	struct bss_descriptor *bss;
	u16 status_code;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (!priv->in_progress_assoc_req) {
		lbs_deb_assoc("ASSOC_RESP: no in-progress assoc request\n");
		ret = -1;
		goto done;
	}
	bss = &priv->in_progress_assoc_req->bss;

	/*
	 * Older FW versions map the IEEE 802.11 Status Code in the association
	 * response to the following values returned in resp->statuscode:
	 *
	 *    IEEE Status Code                Marvell Status Code
	 *    0                       ->      0x0000 ASSOC_RESULT_SUCCESS
	 *    13                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    14                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    15                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    16                      ->      0x0004 ASSOC_RESULT_AUTH_REFUSED
	 *    others                  ->      0x0003 ASSOC_RESULT_REFUSED
	 *
	 * Other response codes:
	 *    0x0001 -> ASSOC_RESULT_INVALID_PARAMETERS (unused)
	 *    0x0002 -> ASSOC_RESULT_TIMEOUT (internal timer expired waiting for
	 *                                    association response from the AP)
	 */

	status_code = le16_to_cpu(resp->statuscode);
	if (priv->fwrelease < 0x09000000) {
		switch (status_code) {
		case 0x00:
			break;
		case 0x01:
			lbs_deb_assoc("ASSOC_RESP: invalid parameters\n");
			break;
		case 0x02:
			lbs_deb_assoc("ASSOC_RESP: internal timer "
				"expired while waiting for the AP\n");
			break;
		case 0x03:
			lbs_deb_assoc("ASSOC_RESP: association "
				"refused by AP\n");
			break;
		case 0x04:
			lbs_deb_assoc("ASSOC_RESP: authentication "
				"refused by AP\n");
			break;
		default:
			lbs_deb_assoc("ASSOC_RESP: failure reason 0x%02x "
				" unknown\n", status_code);
			break;
		}
	} else {
		/* v9+ returns the AP's association response */
		lbs_deb_assoc("ASSOC_RESP: failure reason 0x%02x\n", status_code);
	}

	if (status_code) {
		lbs_mac_event_disconnected(priv);
		ret = -1;
		goto done;
	}

	lbs_deb_hex(LBS_DEB_ASSOC, "ASSOC_RESP",
		    (void *) (resp + sizeof (resp->hdr)),
		    le16_to_cpu(resp->hdr.size) - sizeof (resp->hdr));

	/* Send a Media Connected event, according to the Spec */
	priv->connect_status = LBS_CONNECTED;

	/* Update current SSID and BSSID */
	memcpy(&priv->curbssparams.ssid, &bss->ssid, IEEE80211_MAX_SSID_LEN);
	priv->curbssparams.ssid_len = bss->ssid_len;
	memcpy(priv->curbssparams.bssid, bss->bssid, ETH_ALEN);

	priv->SNR[TYPE_RXPD][TYPE_AVG] = 0;
	priv->NF[TYPE_RXPD][TYPE_AVG] = 0;

	memset(priv->rawSNR, 0x00, sizeof(priv->rawSNR));
	memset(priv->rawNF, 0x00, sizeof(priv->rawNF));
	priv->nextSNRNF = 0;
	priv->numSNRNF = 0;

	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief This function prepares an association-class command.
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param assoc_req The association request describing the BSS to associate
 *                   or reassociate with
 *  @param command   The actual command, either CMD_802_11_ASSOCIATE or
 *                   CMD_802_11_REASSOCIATE
 *
 *  @return         0 or -1
 */
static int lbs_associate(struct lbs_private *priv,
			 struct assoc_request *assoc_req,
			 u16 command)
{
	struct cmd_ds_802_11_associate cmd;
	int ret = 0;
	struct bss_descriptor *bss = &assoc_req->bss;
	u8 *pos = &(cmd.iebuf[0]);
	u16 tmpcap, tmplen, tmpauth;
	struct mrvl_ie_ssid_param_set *ssid;
	struct mrvl_ie_ds_param_set *ds;
	struct mrvl_ie_cf_param_set *cf;
	struct mrvl_ie_rates_param_set *rates;
	struct mrvl_ie_rsn_param_set *rsn;
	struct mrvl_ie_auth_type *auth;

	lbs_deb_enter(LBS_DEB_ASSOC);

	BUG_ON((command != CMD_802_11_ASSOCIATE) &&
		(command != CMD_802_11_REASSOCIATE));

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.command = cpu_to_le16(command);

	/* Fill in static fields */
	memcpy(cmd.bssid, bss->bssid, ETH_ALEN);
	cmd.listeninterval = cpu_to_le16(MRVDRV_DEFAULT_LISTEN_INTERVAL);

	/* Capability info */
	tmpcap = (bss->capability & CAPINFO_MASK);
	if (bss->mode == IW_MODE_INFRA)
		tmpcap |= WLAN_CAPABILITY_ESS;
	cmd.capability = cpu_to_le16(tmpcap);
	lbs_deb_assoc("ASSOC_CMD: capability 0x%04x\n", tmpcap);

	/* SSID */
	ssid = (struct mrvl_ie_ssid_param_set *) pos;
	ssid->header.type = cpu_to_le16(TLV_TYPE_SSID);
	tmplen = bss->ssid_len;
	ssid->header.len = cpu_to_le16(tmplen);
	memcpy(ssid->ssid, bss->ssid, tmplen);
	pos += sizeof(ssid->header) + tmplen;

	ds = (struct mrvl_ie_ds_param_set *) pos;
	ds->header.type = cpu_to_le16(TLV_TYPE_PHY_DS);
	ds->header.len = cpu_to_le16(1);
	ds->channel = bss->phy.ds.channel;
	pos += sizeof(ds->header) + 1;

	cf = (struct mrvl_ie_cf_param_set *) pos;
	cf->header.type = cpu_to_le16(TLV_TYPE_CF);
	tmplen = sizeof(*cf) - sizeof (cf->header);
	cf->header.len = cpu_to_le16(tmplen);
	/* IE payload should be zeroed, firmware fills it in for us */
	pos += sizeof(*cf);

	rates = (struct mrvl_ie_rates_param_set *) pos;
	rates->header.type = cpu_to_le16(TLV_TYPE_RATES);
	tmplen = min_t(u16, ARRAY_SIZE(bss->rates), MAX_RATES);
	memcpy(&rates->rates, &bss->rates, tmplen);
	if (get_common_rates(priv, rates->rates, &tmplen)) {
		ret = -1;
		goto done;
	}
	pos += sizeof(rates->header) + tmplen;
	rates->header.len = cpu_to_le16(tmplen);
	lbs_deb_assoc("ASSOC_CMD: num rates %u\n", tmplen);

	/* Copy the infra. association rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, &rates->rates, tmplen);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(rates->rates, tmplen);

	/* Firmware v9+ indicate authentication suites as a TLV */
	if (priv->fwrelease >= 0x09000000) {
		auth = (struct mrvl_ie_auth_type *) pos;
		auth->header.type = cpu_to_le16(TLV_TYPE_AUTH_TYPE);
		auth->header.len = cpu_to_le16(2);
		tmpauth = iw_auth_to_ieee_auth(priv->secinfo.auth_mode);
		auth->auth = cpu_to_le16(tmpauth);
		pos += sizeof(auth->header) + 2;

		lbs_deb_join("AUTH_CMD: BSSID %pM, auth 0x%x\n",
			bss->bssid, priv->secinfo.auth_mode);
	}

	/* WPA/WPA2 IEs */
	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		rsn = (struct mrvl_ie_rsn_param_set *) pos;
		/* WPA_IE or WPA2_IE */
		rsn->header.type = cpu_to_le16((u16) assoc_req->wpa_ie[0]);
		tmplen = (u16) assoc_req->wpa_ie[1];
		rsn->header.len = cpu_to_le16(tmplen);
		memcpy(rsn->rsnie, &assoc_req->wpa_ie[2], tmplen);
		lbs_deb_hex(LBS_DEB_JOIN, "ASSOC_CMD: WPA/RSN IE", (u8 *) rsn,
			sizeof(rsn->header) + tmplen);
		pos += sizeof(rsn->header) + tmplen;
	}

	cmd.hdr.size = cpu_to_le16((sizeof(cmd) - sizeof(cmd.iebuf)) +
				   (u16)(pos - (u8 *) &cmd.iebuf));

	/* update curbssparams */
	priv->channel = bss->phy.ds.channel;

	ret = lbs_cmd_with_response(priv, command, &cmd);
	if (ret == 0) {
		ret = lbs_assoc_post(priv,
			(struct cmd_ds_802_11_associate_response *) &cmd);
	}

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief Associate to a specific BSS discovered in a scan
 *
 *  @param priv      A pointer to struct lbs_private structure
 *  @param assoc_req The association request describing the BSS to associate with
 *
 *  @return          0-success, otherwise fail
 */
static int lbs_try_associate(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	int ret;
	u8 preamble = RADIO_PREAMBLE_LONG;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* FW v9 and higher indicate authentication suites as a TLV in the
	 * association command, not as a separate authentication command.
	 */
	if (priv->fwrelease < 0x09000000) {
		ret = lbs_set_authentication(priv, assoc_req->bss.bssid,
					     priv->secinfo.auth_mode);
		if (ret)
			goto out;
	}

	/* Use short preamble only when both the BSS and firmware support it */
	if (assoc_req->bss.capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		preamble = RADIO_PREAMBLE_SHORT;

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	ret = lbs_associate(priv, assoc_req, CMD_802_11_ASSOCIATE);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static int lbs_adhoc_post(struct lbs_private *priv,
			  struct cmd_ds_802_11_ad_hoc_result *resp)
{
	int ret = 0;
	u16 command = le16_to_cpu(resp->hdr.command);
	u16 result = le16_to_cpu(resp->hdr.result);
	union iwreq_data wrqu;
	struct bss_descriptor *bss;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_JOIN);

	if (!priv->in_progress_assoc_req) {
		lbs_deb_join("ADHOC_RESP: no in-progress association "
			"request\n");
		ret = -1;
		goto done;
	}
	bss = &priv->in_progress_assoc_req->bss;

	/*
	 * Join result code 0 --> SUCCESS
	 */
	if (result) {
		lbs_deb_join("ADHOC_RESP: failed (result 0x%X)\n", result);
		if (priv->connect_status == LBS_CONNECTED)
			lbs_mac_event_disconnected(priv);
		ret = -1;
		goto done;
	}

	/* Send a Media Connected event, according to the Spec */
	priv->connect_status = LBS_CONNECTED;

	if (command == CMD_RET(CMD_802_11_AD_HOC_START)) {
		/* Update the created network descriptor with the new BSSID */
		memcpy(bss->bssid, resp->bssid, ETH_ALEN);
	}

	/* Set the BSSID from the joined/started descriptor */
	memcpy(&priv->curbssparams.bssid, bss->bssid, ETH_ALEN);

	/* Set the new SSID to current SSID */
	memcpy(&priv->curbssparams.ssid, &bss->ssid, IEEE80211_MAX_SSID_LEN);
	priv->curbssparams.ssid_len = bss->ssid_len;

	netif_carrier_on(priv->dev);
	if (!priv->tx_pending_len)
		netif_wake_queue(priv->dev);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid, ETH_ALEN);
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);

	lbs_deb_join("ADHOC_RESP: Joined/started '%s', BSSID %pM, channel %d\n",
		     print_ssid(ssid, bss->ssid, bss->ssid_len),
		     priv->curbssparams.bssid,
		     priv->channel);

done:
	lbs_deb_leave_args(LBS_DEB_JOIN, "ret %d", ret);
	return ret;
}

/**
 *  @brief Join an adhoc network found in a previous scan
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param assoc_req    The association request describing the BSS to join
 *
 *  @return             0 on success, error on failure
 */
static int lbs_adhoc_join(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	struct cmd_ds_802_11_ad_hoc_join cmd;
	struct bss_descriptor *bss = &assoc_req->bss;
	u8 preamble = RADIO_PREAMBLE_LONG;
	DECLARE_SSID_BUF(ssid);
	u16 ratesize = 0;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	lbs_deb_join("current SSID '%s', ssid length %u\n",
		print_ssid(ssid, priv->curbssparams.ssid,
		priv->curbssparams.ssid_len),
		priv->curbssparams.ssid_len);
	lbs_deb_join("requested ssid '%s', ssid length %u\n",
		print_ssid(ssid, bss->ssid, bss->ssid_len),
		bss->ssid_len);

	/* check if the requested SSID is already joined */
	if (priv->curbssparams.ssid_len &&
	    !lbs_ssid_cmp(priv->curbssparams.ssid,
			priv->curbssparams.ssid_len,
			bss->ssid, bss->ssid_len) &&
	    (priv->mode == IW_MODE_ADHOC) &&
	    (priv->connect_status == LBS_CONNECTED)) {
		union iwreq_data wrqu;

		lbs_deb_join("ADHOC_J_CMD: New ad-hoc SSID is the same as "
			"current, not attempting to re-join");

		/* Send the re-association event though, because the association
		 * request really was successful, even if just a null-op.
		 */
		memset(&wrqu, 0, sizeof(wrqu));
		memcpy(wrqu.ap_addr.sa_data, priv->curbssparams.bssid,
		       ETH_ALEN);
		wrqu.ap_addr.sa_family = ARPHRD_ETHER;
		wireless_send_event(priv->dev, SIOCGIWAP, &wrqu, NULL);
		goto out;
	}

	/* Use short preamble only when both the BSS and firmware support it */
	if (bss->capability & WLAN_CAPABILITY_SHORT_PREAMBLE) {
		lbs_deb_join("AdhocJoin: Short preamble\n");
		preamble = RADIO_PREAMBLE_SHORT;
	}

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	lbs_deb_join("AdhocJoin: channel = %d\n", assoc_req->channel);
	lbs_deb_join("AdhocJoin: band = %c\n", assoc_req->band);

	priv->adhoccreate = 0;
	priv->channel = bss->channel;

	/* Build the join command */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	cmd.bss.type = CMD_BSS_TYPE_IBSS;
	cmd.bss.beaconperiod = cpu_to_le16(bss->beaconperiod);

	memcpy(&cmd.bss.bssid, &bss->bssid, ETH_ALEN);
	memcpy(&cmd.bss.ssid, &bss->ssid, bss->ssid_len);

	memcpy(&cmd.bss.ds, &bss->phy.ds, sizeof(struct ieee_ie_ds_param_set));

	memcpy(&cmd.bss.ibss, &bss->ss.ibss,
	       sizeof(struct ieee_ie_ibss_param_set));

	cmd.bss.capability = cpu_to_le16(bss->capability & CAPINFO_MASK);
	lbs_deb_join("ADHOC_J_CMD: tmpcap=%4X CAPINFO_MASK=%4X\n",
	       bss->capability, CAPINFO_MASK);

	/* information on BSSID descriptor passed to FW */
	lbs_deb_join("ADHOC_J_CMD: BSSID = %pM, SSID = '%s'\n",
			cmd.bss.bssid, cmd.bss.ssid);

	/* Only v8 and below support setting these */
	if (priv->fwrelease < 0x09000000) {
		/* failtimeout */
		cmd.failtimeout = cpu_to_le16(MRVDRV_ASSOCIATION_TIME_OUT);
		/* probedelay */
		cmd.probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);
	}

	/* Copy Data rates from the rates recorded in scan response */
	memset(cmd.bss.rates, 0, sizeof(cmd.bss.rates));
	ratesize = min_t(u16, ARRAY_SIZE(cmd.bss.rates), ARRAY_SIZE (bss->rates));
	memcpy(cmd.bss.rates, bss->rates, ratesize);
	if (get_common_rates(priv, cmd.bss.rates, &ratesize)) {
		lbs_deb_join("ADHOC_JOIN: get_common_rates returned error.\n");
		ret = -1;
		goto out;
	}

	/* Copy the ad-hoc creation rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, cmd.bss.rates, ratesize);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(cmd.bss.rates, ratesize);

	cmd.bss.ibss.atimwindow = bss->atimwindow;

	if (assoc_req->secinfo.wep_enabled) {
		u16 tmp = le16_to_cpu(cmd.bss.capability);
		tmp |= WLAN_CAPABILITY_PRIVACY;
		cmd.bss.capability = cpu_to_le16(tmp);
	}

	if (priv->psmode == LBS802_11POWERMODEMAX_PSP) {
		__le32 local_ps_mode = cpu_to_le32(LBS802_11POWERMODECAM);

		/* wake up first */
		ret = lbs_prepare_and_send_command(priv, CMD_802_11_PS_MODE,
						   CMD_ACT_SET, 0, 0,
						   &local_ps_mode);
		if (ret) {
			ret = -1;
			goto out;
		}
	}

	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_JOIN, &cmd);
	if (ret == 0) {
		ret = lbs_adhoc_post(priv,
				     (struct cmd_ds_802_11_ad_hoc_result *)&cmd);
	}

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief Start an Adhoc Network
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param assoc_req    The association request describing the BSS to start
 *
 *  @return             0 on success, error on failure
 */
static int lbs_adhoc_start(struct lbs_private *priv,
	struct assoc_request *assoc_req)
{
	struct cmd_ds_802_11_ad_hoc_start cmd;
	u8 preamble = RADIO_PREAMBLE_SHORT;
	size_t ratesize = 0;
	u16 tmpcap = 0;
	int ret = 0;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_ASSOC);

	ret = lbs_set_radio(priv, preamble, 1);
	if (ret)
		goto out;

	/* Build the start command */
	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));

	memcpy(cmd.ssid, assoc_req->ssid, assoc_req->ssid_len);

	lbs_deb_join("ADHOC_START: SSID '%s', ssid length %u\n",
		print_ssid(ssid, assoc_req->ssid, assoc_req->ssid_len),
		assoc_req->ssid_len);

	cmd.bsstype = CMD_BSS_TYPE_IBSS;

	if (priv->beacon_period == 0)
		priv->beacon_period = MRVDRV_BEACON_INTERVAL;
	cmd.beaconperiod = cpu_to_le16(priv->beacon_period);

	WARN_ON(!assoc_req->channel);

	/* set Physical parameter set */
	cmd.ds.header.id = WLAN_EID_DS_PARAMS;
	cmd.ds.header.len = 1;
	cmd.ds.channel = assoc_req->channel;

	/* set IBSS parameter set */
	cmd.ibss.header.id = WLAN_EID_IBSS_PARAMS;
	cmd.ibss.header.len = 2;
	cmd.ibss.atimwindow = cpu_to_le16(0);

	/* set capability info */
	tmpcap = WLAN_CAPABILITY_IBSS;
	if (assoc_req->secinfo.wep_enabled ||
	    assoc_req->secinfo.WPAenabled ||
	    assoc_req->secinfo.WPA2enabled) {
		lbs_deb_join("ADHOC_START: WEP/WPA enabled, privacy on\n");
		tmpcap |= WLAN_CAPABILITY_PRIVACY;
	} else
		lbs_deb_join("ADHOC_START: WEP disabled, privacy off\n");

	cmd.capability = cpu_to_le16(tmpcap);

	/* Only v8 and below support setting probe delay */
	if (priv->fwrelease < 0x09000000)
		cmd.probedelay = cpu_to_le16(CMD_SCAN_PROBE_DELAY_TIME);

	ratesize = min(sizeof(cmd.rates), sizeof(lbs_bg_rates));
	memcpy(cmd.rates, lbs_bg_rates, ratesize);

	/* Copy the ad-hoc creating rates into Current BSS state structure */
	memset(&priv->curbssparams.rates, 0, sizeof(priv->curbssparams.rates));
	memcpy(&priv->curbssparams.rates, &cmd.rates, ratesize);

	/* Set MSB on basic rates as the firmware requires, but _after_
	 * copying to current bss rates.
	 */
	lbs_set_basic_rate_flags(cmd.rates, ratesize);

	lbs_deb_join("ADHOC_START: rates=%02x %02x %02x %02x\n",
	       cmd.rates[0], cmd.rates[1], cmd.rates[2], cmd.rates[3]);

	lbs_deb_join("ADHOC_START: Starting Ad-Hoc BSS on channel %d, band %d\n",
		     assoc_req->channel, assoc_req->band);

	priv->adhoccreate = 1;
	priv->mode = IW_MODE_ADHOC;

	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_START, &cmd);
	if (ret == 0)
		ret = lbs_adhoc_post(priv,
				     (struct cmd_ds_802_11_ad_hoc_result *)&cmd);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

/**
 *  @brief Stop and Ad-Hoc network and exit Ad-Hoc mode
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @return             0 on success, or an error
 */
int lbs_adhoc_stop(struct lbs_private *priv)
{
	struct cmd_ds_802_11_ad_hoc_stop cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_JOIN);

	memset(&cmd, 0, sizeof (cmd));
	cmd.hdr.size = cpu_to_le16 (sizeof (cmd));

	ret = lbs_cmd_with_response(priv, CMD_802_11_AD_HOC_STOP, &cmd);

	/* Clean up everything even if there was an error */
	lbs_mac_event_disconnected(priv);

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static inline int match_bss_no_security(struct lbs_802_11_security *secinfo,
					struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled &&
	    !secinfo->WPAenabled && !secinfo->WPA2enabled &&
	    match_bss->wpa_ie[0] != WLAN_EID_GENERIC &&
	    match_bss->rsn_ie[0] != WLAN_EID_RSN &&
	    !(match_bss->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	else
		return 0;
}

static inline int match_bss_static_wep(struct lbs_802_11_security *secinfo,
				       struct bss_descriptor *match_bss)
{
	if (secinfo->wep_enabled &&
	    !secinfo->WPAenabled && !secinfo->WPA2enabled &&
	    (match_bss->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	else
		return 0;
}

static inline int match_bss_wpa(struct lbs_802_11_security *secinfo,
				struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled && secinfo->WPAenabled &&
	    (match_bss->wpa_ie[0] == WLAN_EID_GENERIC)
	    /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	    && (match_bss->capability & WLAN_CAPABILITY_PRIVACY) */
	   )
		return 1;
	else
		return 0;
}

static inline int match_bss_wpa2(struct lbs_802_11_security *secinfo,
				 struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled && secinfo->WPA2enabled &&
	    (match_bss->rsn_ie[0] == WLAN_EID_RSN)
	    /* privacy bit may NOT be set in some APs like LinkSys WRT54G
	    (match_bss->capability & WLAN_CAPABILITY_PRIVACY) */
	   )
		return 1;
	else
		return 0;
}

static inline int match_bss_dynamic_wep(struct lbs_802_11_security *secinfo,
					struct bss_descriptor *match_bss)
{
	if (!secinfo->wep_enabled &&
	    !secinfo->WPAenabled && !secinfo->WPA2enabled &&
	    (match_bss->wpa_ie[0] != WLAN_EID_GENERIC) &&
	    (match_bss->rsn_ie[0] != WLAN_EID_RSN) &&
	    (match_bss->capability & WLAN_CAPABILITY_PRIVACY))
		return 1;
	else
		return 0;
}

/**
 *  @brief Check if a scanned network compatible with the driver settings
 *
 *   WEP     WPA     WPA2    ad-hoc  encrypt                      Network
 * enabled enabled  enabled   AES     mode   privacy  WPA  WPA2  Compatible
 *    0       0        0       0      NONE      0      0    0   yes No security
 *    1       0        0       0      NONE      1      0    0   yes Static WEP
 *    0       1        0       0       x        1x     1    x   yes WPA
 *    0       0        1       0       x        1x     x    1   yes WPA2
 *    0       0        0       1      NONE      1      0    0   yes Ad-hoc AES
 *    0       0        0       0     !=NONE     1      0    0   yes Dynamic WEP
 *
 *
 *  @param priv A pointer to struct lbs_private
 *  @param index   Index in scantable to check against current driver settings
 *  @param mode    Network mode: Infrastructure or IBSS
 *
 *  @return        Index in scantable, or error code if negative
 */
static int is_network_compatible(struct lbs_private *priv,
				 struct bss_descriptor *bss, uint8_t mode)
{
	int matched = 0;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (bss->mode != mode)
		goto done;

	matched = match_bss_no_security(&priv->secinfo, bss);
	if (matched)
		goto done;
	matched = match_bss_static_wep(&priv->secinfo, bss);
	if (matched)
		goto done;
	matched = match_bss_wpa(&priv->secinfo, bss);
	if (matched) {
		lbs_deb_scan("is_network_compatible() WPA: wpa_ie 0x%x "
			     "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s "
			     "privacy 0x%x\n", bss->wpa_ie[0], bss->rsn_ie[0],
			     priv->secinfo.wep_enabled ? "e" : "d",
			     priv->secinfo.WPAenabled ? "e" : "d",
			     priv->secinfo.WPA2enabled ? "e" : "d",
			     (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}
	matched = match_bss_wpa2(&priv->secinfo, bss);
	if (matched) {
		lbs_deb_scan("is_network_compatible() WPA2: wpa_ie 0x%x "
			     "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s "
			     "privacy 0x%x\n", bss->wpa_ie[0], bss->rsn_ie[0],
			     priv->secinfo.wep_enabled ? "e" : "d",
			     priv->secinfo.WPAenabled ? "e" : "d",
			     priv->secinfo.WPA2enabled ? "e" : "d",
			     (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}
	matched = match_bss_dynamic_wep(&priv->secinfo, bss);
	if (matched) {
		lbs_deb_scan("is_network_compatible() dynamic WEP: "
			     "wpa_ie 0x%x wpa2_ie 0x%x privacy 0x%x\n",
			     bss->wpa_ie[0], bss->rsn_ie[0],
			     (bss->capability & WLAN_CAPABILITY_PRIVACY));
		goto done;
	}

	/* bss security settings don't match those configured on card */
	lbs_deb_scan("is_network_compatible() FAILED: wpa_ie 0x%x "
		     "wpa2_ie 0x%x WEP %s WPA %s WPA2 %s privacy 0x%x\n",
		     bss->wpa_ie[0], bss->rsn_ie[0],
		     priv->secinfo.wep_enabled ? "e" : "d",
		     priv->secinfo.WPAenabled ? "e" : "d",
		     priv->secinfo.WPA2enabled ? "e" : "d",
		     (bss->capability & WLAN_CAPABILITY_PRIVACY));

done:
	lbs_deb_leave_args(LBS_DEB_SCAN, "matched: %d", matched);
	return matched;
}

/**
 *  @brief This function finds a specific compatible BSSID in the scan list
 *
 *  Used in association code
 *
 *  @param priv  A pointer to struct lbs_private
 *  @param bssid    BSSID to find in the scan list
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list, or error return code (< 0)
 */
static struct bss_descriptor *lbs_find_bssid_in_list(struct lbs_private *priv,
					      uint8_t *bssid, uint8_t mode)
{
	struct bss_descriptor *iter_bss;
	struct bss_descriptor *found_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	if (!bssid)
		goto out;

	lbs_deb_hex(LBS_DEB_SCAN, "looking for", bssid, ETH_ALEN);

	/* Look through the scan table for a compatible match.  The loop will
	 *   continue past a matched bssid that is not compatible in case there
	 *   is an AP with multiple SSIDs assigned to the same BSSID
	 */
	mutex_lock(&priv->lock);
	list_for_each_entry(iter_bss, &priv->network_list, list) {
		if (compare_ether_addr(iter_bss->bssid, bssid))
			continue; /* bssid doesn't match */
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;
			found_bss = iter_bss;
			break;
		default:
			found_bss = iter_bss;
			break;
		}
	}
	mutex_unlock(&priv->lock);

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

/**
 *  @brief This function finds ssid in ssid list.
 *
 *  Used in association code
 *
 *  @param priv  A pointer to struct lbs_private
 *  @param ssid     SSID to find in the list
 *  @param bssid    BSSID to qualify the SSID selection (if provided)
 *  @param mode     Network mode: Infrastructure or IBSS
 *
 *  @return         index in BSSID list
 */
static struct bss_descriptor *lbs_find_ssid_in_list(struct lbs_private *priv,
					     uint8_t *ssid, uint8_t ssid_len,
					     uint8_t *bssid, uint8_t mode,
					     int channel)
{
	u32 bestrssi = 0;
	struct bss_descriptor *iter_bss = NULL;
	struct bss_descriptor *found_bss = NULL;
	struct bss_descriptor *tmp_oldest = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&priv->lock);

	list_for_each_entry(iter_bss, &priv->network_list, list) {
		if (!tmp_oldest ||
		    (iter_bss->last_scanned < tmp_oldest->last_scanned))
			tmp_oldest = iter_bss;

		if (lbs_ssid_cmp(iter_bss->ssid, iter_bss->ssid_len,
				 ssid, ssid_len) != 0)
			continue; /* ssid doesn't match */
		if (bssid && compare_ether_addr(iter_bss->bssid, bssid) != 0)
			continue; /* bssid doesn't match */
		if ((channel > 0) && (iter_bss->channel != channel))
			continue; /* channel doesn't match */

		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;

			if (bssid) {
				/* Found requested BSSID */
				found_bss = iter_bss;
				goto out;
			}

			if (SCAN_RSSI(iter_bss->rssi) > bestrssi) {
				bestrssi = SCAN_RSSI(iter_bss->rssi);
				found_bss = iter_bss;
			}
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(iter_bss->rssi) > bestrssi) {
				bestrssi = SCAN_RSSI(iter_bss->rssi);
				found_bss = iter_bss;
			}
			break;
		}
	}

out:
	mutex_unlock(&priv->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "found_bss %p", found_bss);
	return found_bss;
}

static int assoc_helper_essid(struct lbs_private *priv,
                              struct assoc_request * assoc_req)
{
	int ret = 0;
	struct bss_descriptor * bss;
	int channel = -1;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* FIXME: take channel into account when picking SSIDs if a channel
	 * is set.
	 */

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		channel = assoc_req->channel;

	lbs_deb_assoc("SSID '%s' requested\n",
	              print_ssid(ssid, assoc_req->ssid, assoc_req->ssid_len));
	if (assoc_req->mode == IW_MODE_INFRA) {
		lbs_send_specific_ssid_scan(priv, assoc_req->ssid,
			assoc_req->ssid_len);

		bss = lbs_find_ssid_in_list(priv, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_INFRA, channel);
		if (bss != NULL) {
			memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
			ret = lbs_try_associate(priv, assoc_req);
		} else {
			lbs_deb_assoc("SSID not found; cannot associate\n");
		}
	} else if (assoc_req->mode == IW_MODE_ADHOC) {
		/* Scan for the network, do not save previous results.  Stale
		 *   scan data will cause us to join a non-existant adhoc network
		 */
		lbs_send_specific_ssid_scan(priv, assoc_req->ssid,
			assoc_req->ssid_len);

		/* Search for the requested SSID in the scan table */
		bss = lbs_find_ssid_in_list(priv, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_ADHOC, channel);
		if (bss != NULL) {
			lbs_deb_assoc("SSID found, will join\n");
			memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
			lbs_adhoc_join(priv, assoc_req);
		} else {
			/* else send START command */
			lbs_deb_assoc("SSID not found, creating adhoc network\n");
			memcpy(&assoc_req->bss.ssid, &assoc_req->ssid,
				IEEE80211_MAX_SSID_LEN);
			assoc_req->bss.ssid_len = assoc_req->ssid_len;
			lbs_adhoc_start(priv, assoc_req);
		}
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_bssid(struct lbs_private *priv,
                              struct assoc_request * assoc_req)
{
	int ret = 0;
	struct bss_descriptor * bss;

	lbs_deb_enter_args(LBS_DEB_ASSOC, "BSSID %pM", assoc_req->bssid);

	/* Search for index position in list for requested MAC */
	bss = lbs_find_bssid_in_list(priv, assoc_req->bssid,
			    assoc_req->mode);
	if (bss == NULL) {
		lbs_deb_assoc("ASSOC: WAP: BSSID %pM not found, "
			"cannot associate.\n", assoc_req->bssid);
		goto out;
	}

	memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
	if (assoc_req->mode == IW_MODE_INFRA) {
		ret = lbs_try_associate(priv, assoc_req);
		lbs_deb_assoc("ASSOC: lbs_try_associate(bssid) returned %d\n",
			      ret);
	} else if (assoc_req->mode == IW_MODE_ADHOC) {
		lbs_adhoc_join(priv, assoc_req);
	}

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_associate(struct lbs_private *priv,
                                  struct assoc_request * assoc_req)
{
	int ret = 0, done = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* If we're given and 'any' BSSID, try associating based on SSID */

	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		if (compare_ether_addr(bssid_any, assoc_req->bssid) &&
		    compare_ether_addr(bssid_off, assoc_req->bssid)) {
			ret = assoc_helper_bssid(priv, assoc_req);
			done = 1;
		}
	}

	if (!done && test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		ret = assoc_helper_essid(priv, assoc_req);
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_mode(struct lbs_private *priv,
                             struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (assoc_req->mode == priv->mode)
		goto done;

	if (assoc_req->mode == IW_MODE_INFRA) {
		if (priv->psstate != PS_STATE_FULL_POWER)
			lbs_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
		priv->psmode = LBS802_11POWERMODECAM;
	}

	priv->mode = assoc_req->mode;
	ret = lbs_set_snmp_mib(priv, SNMP_MIB_OID_BSS_TYPE,
		assoc_req->mode == IW_MODE_ADHOC ? 2 : 1);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static int assoc_helper_channel(struct lbs_private *priv,
                                struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	ret = lbs_update_channel(priv);
	if (ret) {
		lbs_deb_assoc("ASSOC: channel: error getting channel.\n");
		goto done;
	}

	if (assoc_req->channel == priv->channel)
		goto done;

	if (priv->mesh_dev) {
		/* Change mesh channel first; 21.p21 firmware won't let
		   you change channel otherwise (even though it'll return
		   an error to this */
		lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_STOP,
				assoc_req->channel);
	}

	lbs_deb_assoc("ASSOC: channel: %d -> %d\n",
		      priv->channel, assoc_req->channel);

	ret = lbs_set_channel(priv, assoc_req->channel);
	if (ret < 0)
		lbs_deb_assoc("ASSOC: channel: error setting channel.\n");

	/* FIXME: shouldn't need to grab the channel _again_ after setting
	 * it since the firmware is supposed to return the new channel, but
	 * whatever... */
	ret = lbs_update_channel(priv);
	if (ret) {
		lbs_deb_assoc("ASSOC: channel: error getting channel.\n");
		goto done;
	}

	if (assoc_req->channel != priv->channel) {
		lbs_deb_assoc("ASSOC: channel: failed to update channel to %d\n",
		              assoc_req->channel);
		goto restore_mesh;
	}

	if (assoc_req->secinfo.wep_enabled &&
	    (assoc_req->wep_keys[0].len || assoc_req->wep_keys[1].len ||
	     assoc_req->wep_keys[2].len || assoc_req->wep_keys[3].len)) {
		/* Make sure WEP keys are re-sent to firmware */
		set_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags);
	}

	/* Must restart/rejoin adhoc networks after channel change */
 	set_bit(ASSOC_FLAG_SSID, &assoc_req->flags);

 restore_mesh:
	if (priv->mesh_dev)
		lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
				priv->channel);

 done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wep_keys(struct lbs_private *priv,
				 struct assoc_request *assoc_req)
{
	int i;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* Set or remove WEP keys */
	if (assoc_req->wep_keys[0].len || assoc_req->wep_keys[1].len ||
	    assoc_req->wep_keys[2].len || assoc_req->wep_keys[3].len)
		ret = lbs_cmd_802_11_set_wep(priv, CMD_ACT_ADD, assoc_req);
	else
		ret = lbs_cmd_802_11_set_wep(priv, CMD_ACT_REMOVE, assoc_req);

	if (ret)
		goto out;

	/* enable/disable the MAC's WEP packet filter */
	if (assoc_req->secinfo.wep_enabled)
		priv->mac_control |= CMD_ACT_MAC_WEP_ENABLE;
	else
		priv->mac_control &= ~CMD_ACT_MAC_WEP_ENABLE;

	lbs_set_mac_control(priv);

	mutex_lock(&priv->lock);

	/* Copy WEP keys into priv wep key fields */
	for (i = 0; i < 4; i++) {
		memcpy(&priv->wep_keys[i], &assoc_req->wep_keys[i],
		       sizeof(struct enc_key));
	}
	priv->wep_tx_keyidx = assoc_req->wep_tx_keyidx;

	mutex_unlock(&priv->lock);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static int assoc_helper_secinfo(struct lbs_private *priv,
                                struct assoc_request * assoc_req)
{
	int ret = 0;
	uint16_t do_wpa;
	uint16_t rsn = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	memcpy(&priv->secinfo, &assoc_req->secinfo,
		sizeof(struct lbs_802_11_security));

	lbs_set_mac_control(priv);

	/* If RSN is already enabled, don't try to enable it again, since
	 * ENABLE_RSN resets internal state machines and will clobber the
	 * 4-way WPA handshake.
	 */

	/* Get RSN enabled/disabled */
	ret = lbs_cmd_802_11_enable_rsn(priv, CMD_ACT_GET, &rsn);
	if (ret) {
		lbs_deb_assoc("Failed to get RSN status: %d\n", ret);
		goto out;
	}

	/* Don't re-enable RSN if it's already enabled */
	do_wpa = assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled;
	if (do_wpa == rsn)
		goto out;

	/* Set RSN enabled/disabled */
	ret = lbs_cmd_802_11_enable_rsn(priv, CMD_ACT_SET, &do_wpa);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_keys(struct lbs_private *priv,
                                 struct assoc_request * assoc_req)
{
	int ret = 0;
	unsigned int flags = assoc_req->flags;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* Work around older firmware bug where WPA unicast and multicast
	 * keys must be set independently.  Seen in SDIO parts with firmware
	 * version 5.0.11p0.
	 */

	if (test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		clear_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags);
		ret = lbs_cmd_802_11_key_material(priv, CMD_ACT_SET, assoc_req);
		assoc_req->flags = flags;
	}

	if (ret)
		goto out;

	memcpy(&priv->wpa_unicast_key, &assoc_req->wpa_unicast_key,
			sizeof(struct enc_key));

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		clear_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags);

		ret = lbs_cmd_802_11_key_material(priv, CMD_ACT_SET, assoc_req);
		assoc_req->flags = flags;

		memcpy(&priv->wpa_mcast_key, &assoc_req->wpa_mcast_key,
				sizeof(struct enc_key));
	}

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_ie(struct lbs_private *priv,
                               struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		memcpy(&priv->wpa_ie, &assoc_req->wpa_ie, assoc_req->wpa_ie_len);
		priv->wpa_ie_len = assoc_req->wpa_ie_len;
	} else {
		memset(&priv->wpa_ie, 0, MAX_WPA_IE_LEN);
		priv->wpa_ie_len = 0;
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int should_deauth_infrastructure(struct lbs_private *priv,
                                        struct assoc_request * assoc_req)
{
	int ret = 0;

	if (priv->connect_status != LBS_CONNECTED)
		return 0;

	lbs_deb_enter(LBS_DEB_ASSOC);
	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		lbs_deb_assoc("Deauthenticating due to new SSID\n");
		ret = 1;
		goto out;
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		if (priv->secinfo.auth_mode != assoc_req->secinfo.auth_mode) {
			lbs_deb_assoc("Deauthenticating due to new security\n");
			ret = 1;
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		lbs_deb_assoc("Deauthenticating due to new BSSID\n");
		ret = 1;
		goto out;
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		lbs_deb_assoc("Deauthenticating due to channel switch\n");
		ret = 1;
		goto out;
	}

	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_INFRA) {
			lbs_deb_assoc("Deauthenticating due to leaving "
				"infra mode\n");
			ret = 1;
			goto out;
		}
	}

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int should_stop_adhoc(struct lbs_private *priv,
                             struct assoc_request * assoc_req)
{
	lbs_deb_enter(LBS_DEB_ASSOC);

	if (priv->connect_status != LBS_CONNECTED)
		return 0;

	if (lbs_ssid_cmp(priv->curbssparams.ssid,
	                      priv->curbssparams.ssid_len,
	                      assoc_req->ssid, assoc_req->ssid_len) != 0)
		return 1;

	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_ADHOC)
			return 1;
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		if (assoc_req->channel != priv->channel)
			return 1;
	}

	lbs_deb_leave(LBS_DEB_ASSOC);
	return 0;
}


/**
 *  @brief This function finds the best SSID in the Scan List
 *
 *  Search the scan table for the best SSID that also matches the current
 *   adapter network preference (infrastructure or adhoc)
 *
 *  @param priv  A pointer to struct lbs_private
 *
 *  @return         index in BSSID list
 */
static struct bss_descriptor *lbs_find_best_ssid_in_list(
	struct lbs_private *priv, uint8_t mode)
{
	uint8_t bestrssi = 0;
	struct bss_descriptor *iter_bss;
	struct bss_descriptor *best_bss = NULL;

	lbs_deb_enter(LBS_DEB_SCAN);

	mutex_lock(&priv->lock);

	list_for_each_entry(iter_bss, &priv->network_list, list) {
		switch (mode) {
		case IW_MODE_INFRA:
		case IW_MODE_ADHOC:
			if (!is_network_compatible(priv, iter_bss, mode))
				break;
			if (SCAN_RSSI(iter_bss->rssi) <= bestrssi)
				break;
			bestrssi = SCAN_RSSI(iter_bss->rssi);
			best_bss = iter_bss;
			break;
		case IW_MODE_AUTO:
		default:
			if (SCAN_RSSI(iter_bss->rssi) <= bestrssi)
				break;
			bestrssi = SCAN_RSSI(iter_bss->rssi);
			best_bss = iter_bss;
			break;
		}
	}

	mutex_unlock(&priv->lock);
	lbs_deb_leave_args(LBS_DEB_SCAN, "best_bss %p", best_bss);
	return best_bss;
}

/**
 *  @brief Find the best AP
 *
 *  Used from association worker.
 *
 *  @param priv         A pointer to struct lbs_private structure
 *  @param pSSID        A pointer to AP's ssid
 *
 *  @return             0--success, otherwise--fail
 */
static int lbs_find_best_network_ssid(struct lbs_private *priv,
	uint8_t *out_ssid, uint8_t *out_ssid_len, uint8_t preferred_mode,
	uint8_t *out_mode)
{
	int ret = -1;
	struct bss_descriptor *found;

	lbs_deb_enter(LBS_DEB_SCAN);

	priv->scan_ssid_len = 0;
	lbs_scan_networks(priv, 1);
	if (priv->surpriseremoved)
		goto out;

	found = lbs_find_best_ssid_in_list(priv, preferred_mode);
	if (found && (found->ssid_len > 0)) {
		memcpy(out_ssid, &found->ssid, IEEE80211_MAX_SSID_LEN);
		*out_ssid_len = found->ssid_len;
		*out_mode = found->mode;
		ret = 0;
	}

out:
	lbs_deb_leave_args(LBS_DEB_SCAN, "ret %d", ret);
	return ret;
}


void lbs_association_worker(struct work_struct *work)
{
	struct lbs_private *priv = container_of(work, struct lbs_private,
		assoc_work.work);
	struct assoc_request * assoc_req = NULL;
	int ret = 0;
	int find_any_ssid = 0;
	DECLARE_SSID_BUF(ssid);

	lbs_deb_enter(LBS_DEB_ASSOC);

	mutex_lock(&priv->lock);
	assoc_req = priv->pending_assoc_req;
	priv->pending_assoc_req = NULL;
	priv->in_progress_assoc_req = assoc_req;
	mutex_unlock(&priv->lock);

	if (!assoc_req)
		goto done;

	lbs_deb_assoc(
		"Association Request:\n"
		"    flags:     0x%08lx\n"
		"    SSID:      '%s'\n"
		"    chann:     %d\n"
		"    band:      %d\n"
		"    mode:      %d\n"
		"    BSSID:     %pM\n"
		"    secinfo:  %s%s%s\n"
		"    auth_mode: %d\n",
		assoc_req->flags,
		print_ssid(ssid, assoc_req->ssid, assoc_req->ssid_len),
		assoc_req->channel, assoc_req->band, assoc_req->mode,
		assoc_req->bssid,
		assoc_req->secinfo.WPAenabled ? " WPA" : "",
		assoc_req->secinfo.WPA2enabled ? " WPA2" : "",
		assoc_req->secinfo.wep_enabled ? " WEP" : "",
		assoc_req->secinfo.auth_mode);

	/* If 'any' SSID was specified, find an SSID to associate with */
	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags) &&
	    !assoc_req->ssid_len)
		find_any_ssid = 1;

	/* But don't use 'any' SSID if there's a valid locked BSSID to use */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		if (compare_ether_addr(assoc_req->bssid, bssid_any) &&
		    compare_ether_addr(assoc_req->bssid, bssid_off))
			find_any_ssid = 0;
	}

	if (find_any_ssid) {
		u8 new_mode = assoc_req->mode;

		ret = lbs_find_best_network_ssid(priv, assoc_req->ssid,
				&assoc_req->ssid_len, assoc_req->mode, &new_mode);
		if (ret) {
			lbs_deb_assoc("Could not find best network\n");
			ret = -ENETUNREACH;
			goto out;
		}

		/* Ensure we switch to the mode of the AP */
		if (assoc_req->mode == IW_MODE_AUTO) {
			set_bit(ASSOC_FLAG_MODE, &assoc_req->flags);
			assoc_req->mode = new_mode;
		}
	}

	/*
	 * Check if the attributes being changing require deauthentication
	 * from the currently associated infrastructure access point.
	 */
	if (priv->mode == IW_MODE_INFRA) {
		if (should_deauth_infrastructure(priv, assoc_req)) {
			ret = lbs_cmd_80211_deauthenticate(priv,
							   priv->curbssparams.bssid,
							   WLAN_REASON_DEAUTH_LEAVING);
			if (ret) {
				lbs_deb_assoc("Deauthentication due to new "
					"configuration request failed: %d\n",
					ret);
			}
		}
	} else if (priv->mode == IW_MODE_ADHOC) {
		if (should_stop_adhoc(priv, assoc_req)) {
			ret = lbs_adhoc_stop(priv);
			if (ret) {
				lbs_deb_assoc("Teardown of AdHoc network due to "
					"new configuration request failed: %d\n",
					ret);
			}

		}
	}

	/* Send the various configuration bits to the firmware */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		ret = assoc_helper_mode(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		ret = assoc_helper_channel(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		ret = assoc_helper_secinfo(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		ret = assoc_helper_wpa_ie(priv, assoc_req);
		if (ret)
			goto out;
	}

	/*
	 * v10 FW wants WPA keys to be set/cleared before WEP key operations,
	 * otherwise it will fail to correctly associate to WEP networks.
	 * Other firmware versions don't appear to care.
	 */
	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags) ||
	    test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		ret = assoc_helper_wpa_keys(priv, assoc_req);
		if (ret)
			goto out;
	}

	if (test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags) ||
	    test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags)) {
		ret = assoc_helper_wep_keys(priv, assoc_req);
		if (ret)
			goto out;
	}


	/* SSID/BSSID should be the _last_ config option set, because they
	 * trigger the association attempt.
	 */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags) ||
	    test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		int success = 1;

		ret = assoc_helper_associate(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC: association unsuccessful: %d\n",
				ret);
			success = 0;
		}

		if (priv->connect_status != LBS_CONNECTED) {
			lbs_deb_assoc("ASSOC: association unsuccessful, "
				"not connected\n");
			success = 0;
		}

		if (success) {
			lbs_deb_assoc("associated to %pM\n",
				priv->curbssparams.bssid);
			lbs_prepare_and_send_command(priv,
				CMD_802_11_RSSI,
				0, CMD_OPTION_WAITFORRSP, 0, NULL);
		} else {
			ret = -1;
		}
	}

out:
	if (ret) {
		lbs_deb_assoc("ASSOC: reconfiguration attempt unsuccessful: %d\n",
			ret);
	}

	mutex_lock(&priv->lock);
	priv->in_progress_assoc_req = NULL;
	mutex_unlock(&priv->lock);
	kfree(assoc_req);

done:
	lbs_deb_leave(LBS_DEB_ASSOC);
}


/*
 * Caller MUST hold any necessary locks
 */
struct assoc_request *lbs_get_association_request(struct lbs_private *priv)
{
	struct assoc_request * assoc_req;

	lbs_deb_enter(LBS_DEB_ASSOC);
	if (!priv->pending_assoc_req) {
		priv->pending_assoc_req = kzalloc(sizeof(struct assoc_request),
		                                     GFP_KERNEL);
		if (!priv->pending_assoc_req) {
			lbs_pr_info("Not enough memory to allocate association"
				" request!\n");
			return NULL;
		}
	}

	/* Copy current configuration attributes to the association request,
	 * but don't overwrite any that are already set.
	 */
	assoc_req = priv->pending_assoc_req;
	if (!test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		memcpy(&assoc_req->ssid, &priv->curbssparams.ssid,
		       IEEE80211_MAX_SSID_LEN);
		assoc_req->ssid_len = priv->curbssparams.ssid_len;
	}

	if (!test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		assoc_req->channel = priv->channel;

	if (!test_bit(ASSOC_FLAG_BAND, &assoc_req->flags))
		assoc_req->band = priv->curbssparams.band;

	if (!test_bit(ASSOC_FLAG_MODE, &assoc_req->flags))
		assoc_req->mode = priv->mode;

	if (!test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		memcpy(&assoc_req->bssid, priv->curbssparams.bssid,
			ETH_ALEN);
	}

	if (!test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags)) {
		int i;
		for (i = 0; i < 4; i++) {
			memcpy(&assoc_req->wep_keys[i], &priv->wep_keys[i],
				sizeof(struct enc_key));
		}
	}

	if (!test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags))
		assoc_req->wep_tx_keyidx = priv->wep_tx_keyidx;

	if (!test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_mcast_key, &priv->wpa_mcast_key,
			sizeof(struct enc_key));
	}

	if (!test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_unicast_key, &priv->wpa_unicast_key,
			sizeof(struct enc_key));
	}

	if (!test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		memcpy(&assoc_req->secinfo, &priv->secinfo,
			sizeof(struct lbs_802_11_security));
	}

	if (!test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_ie, &priv->wpa_ie,
			MAX_WPA_IE_LEN);
		assoc_req->wpa_ie_len = priv->wpa_ie_len;
	}

	lbs_deb_leave(LBS_DEB_ASSOC);
	return assoc_req;
}


/**
 *  @brief Deauthenticate from a specific BSS
 *
 *  @param priv        A pointer to struct lbs_private structure
 *  @param bssid       The specific BSS to deauthenticate from
 *  @param reason      The 802.11 sec. 7.3.1.7 Reason Code for deauthenticating
 *
 *  @return            0 on success, error on failure
 */
int lbs_cmd_80211_deauthenticate(struct lbs_private *priv, u8 bssid[ETH_ALEN],
				 u16 reason)
{
	struct cmd_ds_802_11_deauthenticate cmd;
	int ret;

	lbs_deb_enter(LBS_DEB_JOIN);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.macaddr, &bssid[0], ETH_ALEN);
	cmd.reasoncode = cpu_to_le16(reason);

	ret = lbs_cmd_with_response(priv, CMD_802_11_DEAUTHENTICATE, &cmd);

	/* Clean up everything even if there was an error; can't assume that
	 * we're still authenticated to the AP after trying to deauth.
	 */
	lbs_mac_event_disconnected(priv);

	lbs_deb_leave(LBS_DEB_JOIN);
	return ret;
}

