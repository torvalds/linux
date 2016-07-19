/* cfg80211 Interface for prism2_usb module */
#include "hfa384x.h"
#include "prism2mgmt.h"

/* Prism2 channel/frequency/bitrate declarations */
static const struct ieee80211_channel prism2_channels[] = {
	{ .center_freq = 2412 },
	{ .center_freq = 2417 },
	{ .center_freq = 2422 },
	{ .center_freq = 2427 },
	{ .center_freq = 2432 },
	{ .center_freq = 2437 },
	{ .center_freq = 2442 },
	{ .center_freq = 2447 },
	{ .center_freq = 2452 },
	{ .center_freq = 2457 },
	{ .center_freq = 2462 },
	{ .center_freq = 2467 },
	{ .center_freq = 2472 },
	{ .center_freq = 2484 },
};

static const struct ieee80211_rate prism2_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20 },
	{ .bitrate = 55 },
	{ .bitrate = 110 }
};

#define PRISM2_NUM_CIPHER_SUITES 2
static const u32 prism2_cipher_suites[PRISM2_NUM_CIPHER_SUITES] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104
};

/* prism2 device private data */
struct prism2_wiphy_private {
	wlandevice_t *wlandev;

	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[ARRAY_SIZE(prism2_channels)];
	struct ieee80211_rate rates[ARRAY_SIZE(prism2_rates)];

	struct cfg80211_scan_request *scan_request;
};

static const void * const prism2_wiphy_privid = &prism2_wiphy_privid;

/* Helper Functions */
static int prism2_result2err(int prism2_result)
{
	int err = 0;

	switch (prism2_result) {
	case P80211ENUM_resultcode_invalid_parameters:
		err = -EINVAL;
		break;
	case P80211ENUM_resultcode_implementation_failure:
		err = -EIO;
		break;
	case P80211ENUM_resultcode_not_supported:
		err = -EOPNOTSUPP;
		break;
	default:
		err = 0;
		break;
	}

	return err;
}

static int prism2_domibset_uint32(wlandevice_t *wlandev, u32 did, u32 data)
{
	struct p80211msg_dot11req_mibset msg;
	p80211item_uint32_t *mibitem =
			(p80211item_uint32_t *)&msg.mibattribute.data;

	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem->did = did;
	mibitem->data = data;

	return p80211req_dorequest(wlandev, (u8 *)&msg);
}

static int prism2_domibset_pstr32(wlandevice_t *wlandev,
				  u32 did, u8 len, const u8 *data)
{
	struct p80211msg_dot11req_mibset msg;
	p80211item_pstr32_t *mibitem =
			(p80211item_pstr32_t *)&msg.mibattribute.data;

	msg.msgcode = DIDmsg_dot11req_mibset;
	mibitem->did = did;
	mibitem->data.len = len;
	memcpy(mibitem->data.data, data, len);

	return p80211req_dorequest(wlandev, (u8 *)&msg);
}

/* The interface functions, called by the cfg80211 layer */
static int prism2_change_virtual_intf(struct wiphy *wiphy,
				      struct net_device *dev,
				      enum nl80211_iftype type, u32 *flags,
				      struct vif_params *params)
{
	wlandevice_t *wlandev = dev->ml_priv;
	u32 data;
	int result;
	int err = 0;

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		if (wlandev->macmode == WLAN_MACMODE_IBSS_STA)
			goto exit;
		wlandev->macmode = WLAN_MACMODE_IBSS_STA;
		data = 0;
		break;
	case NL80211_IFTYPE_STATION:
		if (wlandev->macmode == WLAN_MACMODE_ESS_STA)
			goto exit;
		wlandev->macmode = WLAN_MACMODE_ESS_STA;
		data = 1;
		break;
	default:
		netdev_warn(dev, "Operation mode: %d not support\n", type);
		return -EOPNOTSUPP;
	}

	/* Set Operation mode to the PORT TYPE RID */
	result = prism2_domibset_uint32(wlandev,
					DIDmib_p2_p2Static_p2CnfPortType,
					data);

	if (result)
		err = -EFAULT;

	dev->ieee80211_ptr->iftype = type;

exit:
	return err;
}

static int prism2_add_key(struct wiphy *wiphy, struct net_device *dev,
			  u8 key_index, bool pairwise, const u8 *mac_addr,
			  struct key_params *params)
{
	wlandevice_t *wlandev = dev->ml_priv;
	u32 did;

	int err = 0;
	int result = 0;

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
						key_index);
		if (result)
			goto exit;

		/* send key to driver */
		switch (key_index) {
		case 0:
			did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0;
			break;

		case 1:
			did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1;
			break;

		case 2:
			did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2;
			break;

		case 3:
			did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3;
			break;

		default:
			err = -EINVAL;
			goto exit;
		}

		result = prism2_domibset_pstr32(wlandev, did,
						params->key_len, params->key);
		if (result)
			goto exit;
		break;

	default:
		pr_debug("Unsupported cipher suite\n");
		result = 1;
	}

exit:
	if (result)
		err = -EFAULT;

	return err;
}

static int prism2_get_key(struct wiphy *wiphy, struct net_device *dev,
			  u8 key_index, bool pairwise,
			  const u8 *mac_addr, void *cookie,
			  void (*callback)(void *cookie, struct key_params*))
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct key_params params;
	int len;

	if (key_index >= NUM_WEPKEYS)
		return -EINVAL;

	len = wlandev->wep_keylens[key_index];
	memset(&params, 0, sizeof(params));

	if (len == 13)
		params.cipher = WLAN_CIPHER_SUITE_WEP104;
	else if (len == 5)
		params.cipher = WLAN_CIPHER_SUITE_WEP104;
	else
		return -ENOENT;
	params.key_len = len;
	params.key = wlandev->wep_keys[key_index];
	params.seq_len = 0;

	callback(cookie, &params);

	return 0;
}

static int prism2_del_key(struct wiphy *wiphy, struct net_device *dev,
			  u8 key_index, bool pairwise, const u8 *mac_addr)
{
	wlandevice_t *wlandev = dev->ml_priv;
	u32 did;
	int err = 0;
	int result = 0;

	/* There is no direct way in the hardware (AFAIK) of removing
	 * a key, so we will cheat by setting the key to a bogus value
	 */

	/* send key to driver */
	switch (key_index) {
	case 0:
		did =
		    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0;
		break;

	case 1:
		did =
		    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1;
		break;

	case 2:
		did =
		    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2;
		break;

	case 3:
		did =
		    DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3;
		break;

	default:
		err = -EINVAL;
		goto exit;
	}

	result = prism2_domibset_pstr32(wlandev, did, 13, "0000000000000");

exit:
	if (result)
		err = -EFAULT;

	return err;
}

static int prism2_set_default_key(struct wiphy *wiphy, struct net_device *dev,
				  u8 key_index, bool unicast, bool multicast)
{
	wlandevice_t *wlandev = dev->ml_priv;

	int err = 0;
	int result = 0;

	result = prism2_domibset_uint32(wlandev,
		DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
		key_index);

	if (result)
		err = -EFAULT;

	return err;
}

static int prism2_get_station(struct wiphy *wiphy, struct net_device *dev,
			      const u8 *mac, struct station_info *sinfo)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct p80211msg_lnxreq_commsquality quality;
	int result;

	memset(sinfo, 0, sizeof(*sinfo));

	if ((wlandev == NULL) || (wlandev->msdstate != WLAN_MSD_RUNNING))
		return -EOPNOTSUPP;

	/* build request message */
	quality.msgcode = DIDmsg_lnxreq_commsquality;
	quality.dbm.data = P80211ENUM_truth_true;
	quality.dbm.status = P80211ENUM_msgitem_status_data_ok;

	/* send message to nsd */
	if (wlandev->mlmerequest == NULL)
		return -EOPNOTSUPP;

	result = wlandev->mlmerequest(wlandev, (struct p80211msg *)&quality);

	if (result == 0) {
		sinfo->txrate.legacy = quality.txrate.data;
		sinfo->filled |= BIT(NL80211_STA_INFO_TX_BITRATE);
		sinfo->signal = quality.level.data;
		sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);
	}

	return result;
}

static int prism2_scan(struct wiphy *wiphy,
		       struct cfg80211_scan_request *request)
{
	struct net_device *dev;
	struct prism2_wiphy_private *priv = wiphy_priv(wiphy);
	wlandevice_t *wlandev;
	struct p80211msg_dot11req_scan msg1;
	struct p80211msg_dot11req_scan_results msg2;
	struct cfg80211_bss *bss;
	int result;
	int err = 0;
	int numbss = 0;
	int i = 0;
	u8 ie_buf[46];
	int ie_len;

	if (!request)
		return -EINVAL;

	dev = request->wdev->netdev;
	wlandev = dev->ml_priv;

	if (priv->scan_request && priv->scan_request != request)
		return -EBUSY;

	if (wlandev->macmode == WLAN_MACMODE_ESS_AP) {
		netdev_err(dev, "Can't scan in AP mode\n");
		return -EOPNOTSUPP;
	}

	priv->scan_request = request;

	memset(&msg1, 0x00, sizeof(struct p80211msg_dot11req_scan));
	msg1.msgcode = DIDmsg_dot11req_scan;
	msg1.bsstype.data = P80211ENUM_bsstype_any;

	memset(&msg1.bssid.data.data, 0xFF, sizeof(msg1.bssid.data.data));
	msg1.bssid.data.len = 6;

	if (request->n_ssids > 0) {
		msg1.scantype.data = P80211ENUM_scantype_active;
		msg1.ssid.data.len = request->ssids->ssid_len;
		memcpy(msg1.ssid.data.data,
			request->ssids->ssid, request->ssids->ssid_len);
	} else {
		msg1.scantype.data = 0;
	}
	msg1.probedelay.data = 0;

	for (i = 0;
		(i < request->n_channels) && i < ARRAY_SIZE(prism2_channels);
		i++)
		msg1.channellist.data.data[i] =
			ieee80211_frequency_to_channel(
				request->channels[i]->center_freq);
	msg1.channellist.data.len = request->n_channels;

	msg1.maxchanneltime.data = 250;
	msg1.minchanneltime.data = 200;

	result = p80211req_dorequest(wlandev, (u8 *)&msg1);
	if (result) {
		err = prism2_result2err(msg1.resultcode.data);
		goto exit;
	}
	/* Now retrieve scan results */
	numbss = msg1.numbss.data;

	for (i = 0; i < numbss; i++) {
		int freq;

		memset(&msg2, 0, sizeof(msg2));
		msg2.msgcode = DIDmsg_dot11req_scan_results;
		msg2.bssindex.data = i;

		result = p80211req_dorequest(wlandev, (u8 *)&msg2);
		if ((result != 0) ||
		    (msg2.resultcode.data != P80211ENUM_resultcode_success)) {
			break;
		}

		ie_buf[0] = WLAN_EID_SSID;
		ie_buf[1] = msg2.ssid.data.len;
		ie_len = ie_buf[1] + 2;
		memcpy(&ie_buf[2], &(msg2.ssid.data.data), msg2.ssid.data.len);
		freq = ieee80211_channel_to_frequency(msg2.dschannel.data,
						      IEEE80211_BAND_2GHZ);
		bss = cfg80211_inform_bss(wiphy,
			ieee80211_get_channel(wiphy, freq),
			CFG80211_BSS_FTYPE_UNKNOWN,
			(const u8 *)&(msg2.bssid.data.data),
			msg2.timestamp.data, msg2.capinfo.data,
			msg2.beaconperiod.data,
			ie_buf,
			ie_len,
			(msg2.signal.data - 65536) * 100, /* Conversion to signed type */
			GFP_KERNEL
		);

		if (!bss) {
			err = -ENOMEM;
			goto exit;
		}

		cfg80211_put_bss(wiphy, bss);
	}

	if (result)
		err = prism2_result2err(msg2.resultcode.data);

exit:
	cfg80211_scan_done(request, err ? 1 : 0);
	priv->scan_request = NULL;
	return err;
}

static int prism2_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct prism2_wiphy_private *priv = wiphy_priv(wiphy);
	wlandevice_t *wlandev = priv->wlandev;
	u32 data;
	int result;
	int err = 0;

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		if (wiphy->rts_threshold == -1)
			data = 2347;
		else
			data = wiphy->rts_threshold;

		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11mac_dot11OperationTable_dot11RTSThreshold,
						data);
		if (result) {
			err = -EFAULT;
			goto exit;
		}
	}

	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		if (wiphy->frag_threshold == -1)
			data = 2346;
		else
			data = wiphy->frag_threshold;

		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11mac_dot11OperationTable_dot11FragmentationThreshold,
						data);
		if (result) {
			err = -EFAULT;
			goto exit;
		}
	}

exit:
	return err;
}

static int prism2_connect(struct wiphy *wiphy, struct net_device *dev,
			  struct cfg80211_connect_params *sme)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct ieee80211_channel *channel = sme->channel;
	struct p80211msg_lnxreq_autojoin msg_join;
	u32 did;
	int length = sme->ssid_len;
	int chan = -1;
	int is_wep = (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP40) ||
	    (sme->crypto.cipher_group == WLAN_CIPHER_SUITE_WEP104);
	int result;
	int err = 0;

	/* Set the channel */
	if (channel) {
		chan = ieee80211_frequency_to_channel(channel->center_freq);
		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11phy_dot11PhyDSSSTable_dot11CurrentChannel,
						chan);
		if (result)
			goto exit;
	}

	/* Set the authorization */
	if ((sme->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM) ||
		((sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC) && !is_wep))
			msg_join.authtype.data = P80211ENUM_authalg_opensystem;
	else if ((sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY) ||
		((sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC) && is_wep))
			msg_join.authtype.data = P80211ENUM_authalg_sharedkey;
	else
		netdev_warn(dev,
			"Unhandled authorisation type for connect (%d)\n",
			sme->auth_type);

	/* Set the encryption - we only support wep */
	if (is_wep) {
		if (sme->key) {
			result = prism2_domibset_uint32(wlandev,
				DIDmib_dot11smt_dot11PrivacyTable_dot11WEPDefaultKeyID,
				sme->key_idx);
			if (result)
				goto exit;

			/* send key to driver */
			switch (sme->key_idx) {
			case 0:
				did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey0;
				break;

			case 1:
				did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey1;
				break;

			case 2:
				did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey2;
				break;

			case 3:
				did = DIDmib_dot11smt_dot11WEPDefaultKeysTable_dot11WEPDefaultKey3;
				break;

			default:
				err = -EINVAL;
				goto exit;
			}

			result = prism2_domibset_pstr32(wlandev,
							did, sme->key_len,
							(u8 *)sme->key);
			if (result)
				goto exit;
		}

		/* Assume we should set privacy invoked and exclude unencrypted
		 * We could possible use sme->privacy here, but the assumption
		 * seems reasonable anyways
		 */
		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
						P80211ENUM_truth_true);
		if (result)
			goto exit;

		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
						P80211ENUM_truth_true);
		if (result)
			goto exit;

	} else {
		/* Assume we should unset privacy invoked
		 * and exclude unencrypted
		 */
		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11smt_dot11PrivacyTable_dot11PrivacyInvoked,
						P80211ENUM_truth_false);
		if (result)
			goto exit;

		result = prism2_domibset_uint32(wlandev,
						DIDmib_dot11smt_dot11PrivacyTable_dot11ExcludeUnencrypted,
						P80211ENUM_truth_false);
		if (result)
			goto exit;
	}

	/* Now do the actual join. Note there is no way that I can
	 * see to request a specific bssid
	 */
	msg_join.msgcode = DIDmsg_lnxreq_autojoin;

	memcpy(msg_join.ssid.data.data, sme->ssid, length);
	msg_join.ssid.data.len = length;

	result = p80211req_dorequest(wlandev, (u8 *)&msg_join);

exit:
	if (result)
		err = -EFAULT;

	return err;
}

static int prism2_disconnect(struct wiphy *wiphy, struct net_device *dev,
			     u16 reason_code)
{
	wlandevice_t *wlandev = dev->ml_priv;
	struct p80211msg_lnxreq_autojoin msg_join;
	int result;
	int err = 0;

	/* Do a join, with a bogus ssid. Thats the only way I can think of */
	msg_join.msgcode = DIDmsg_lnxreq_autojoin;

	memcpy(msg_join.ssid.data.data, "---", 3);
	msg_join.ssid.data.len = 3;

	result = p80211req_dorequest(wlandev, (u8 *)&msg_join);

	if (result)
		err = -EFAULT;

	return err;
}

static int prism2_join_ibss(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_ibss_params *params)
{
	return -EOPNOTSUPP;
}

static int prism2_leave_ibss(struct wiphy *wiphy, struct net_device *dev)
{
	return -EOPNOTSUPP;
}

static int prism2_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			       enum nl80211_tx_power_setting type, int mbm)
{
	struct prism2_wiphy_private *priv = wiphy_priv(wiphy);
	wlandevice_t *wlandev = priv->wlandev;
	u32 data;
	int result;
	int err = 0;

	if (type == NL80211_TX_POWER_AUTOMATIC)
		data = 30;
	else
		data = MBM_TO_DBM(mbm);

	result = prism2_domibset_uint32(wlandev,
		DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel,
		data);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

exit:
	return err;
}

static int prism2_get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			       int *dbm)
{
	struct prism2_wiphy_private *priv = wiphy_priv(wiphy);
	wlandevice_t *wlandev = priv->wlandev;
	struct p80211msg_dot11req_mibget msg;
	p80211item_uint32_t *mibitem;
	int result;
	int err = 0;

	mibitem = (p80211item_uint32_t *)&msg.mibattribute.data;
	msg.msgcode = DIDmsg_dot11req_mibget;
	mibitem->did =
	    DIDmib_dot11phy_dot11PhyTxPowerTable_dot11CurrentTxPowerLevel;

	result = p80211req_dorequest(wlandev, (u8 *)&msg);

	if (result) {
		err = -EFAULT;
		goto exit;
	}

	*dbm = mibitem->data;

exit:
	return err;
}

/* Interface callback functions, passing data back up to the cfg80211 layer */
void prism2_connect_result(wlandevice_t *wlandev, u8 failed)
{
	u16 status = failed ?
		     WLAN_STATUS_UNSPECIFIED_FAILURE : WLAN_STATUS_SUCCESS;

	cfg80211_connect_result(wlandev->netdev, wlandev->bssid,
				NULL, 0, NULL, 0, status, GFP_KERNEL);
}

void prism2_disconnected(wlandevice_t *wlandev)
{
	cfg80211_disconnected(wlandev->netdev, 0, NULL,
		0, false, GFP_KERNEL);
}

void prism2_roamed(wlandevice_t *wlandev)
{
	cfg80211_roamed(wlandev->netdev, NULL, wlandev->bssid,
		NULL, 0, NULL, 0, GFP_KERNEL);
}

/* Structures for declaring wiphy interface */
static const struct cfg80211_ops prism2_usb_cfg_ops = {
	.change_virtual_intf = prism2_change_virtual_intf,
	.add_key = prism2_add_key,
	.get_key = prism2_get_key,
	.del_key = prism2_del_key,
	.set_default_key = prism2_set_default_key,
	.get_station = prism2_get_station,
	.scan = prism2_scan,
	.set_wiphy_params = prism2_set_wiphy_params,
	.connect = prism2_connect,
	.disconnect = prism2_disconnect,
	.join_ibss = prism2_join_ibss,
	.leave_ibss = prism2_leave_ibss,
	.set_tx_power = prism2_set_tx_power,
	.get_tx_power = prism2_get_tx_power,
};

/* Functions to create/free wiphy interface */
static struct wiphy *wlan_create_wiphy(struct device *dev, wlandevice_t *wlandev)
{
	struct wiphy *wiphy;
	struct prism2_wiphy_private *priv;

	wiphy = wiphy_new(&prism2_usb_cfg_ops, sizeof(*priv));
	if (!wiphy)
		return NULL;

	priv = wiphy_priv(wiphy);
	priv->wlandev = wlandev;
	memcpy(priv->channels, prism2_channels, sizeof(prism2_channels));
	memcpy(priv->rates, prism2_rates, sizeof(prism2_rates));
	priv->band.channels = priv->channels;
	priv->band.n_channels = ARRAY_SIZE(prism2_channels);
	priv->band.bitrates = priv->rates;
	priv->band.n_bitrates = ARRAY_SIZE(prism2_rates);
	priv->band.band = IEEE80211_BAND_2GHZ;
	priv->band.ht_cap.ht_supported = false;
	wiphy->bands[IEEE80211_BAND_2GHZ] = &priv->band;

	set_wiphy_dev(wiphy, dev);
	wiphy->privid = prism2_wiphy_privid;
	wiphy->max_scan_ssids = 1;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION)
				 | BIT(NL80211_IFTYPE_ADHOC);
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->n_cipher_suites = PRISM2_NUM_CIPHER_SUITES;
	wiphy->cipher_suites = prism2_cipher_suites;

	if (wiphy_register(wiphy) < 0)
		return NULL;

	return wiphy;
}

static void wlan_free_wiphy(struct wiphy *wiphy)
{
	wiphy_unregister(wiphy);
	wiphy_free(wiphy);
}
