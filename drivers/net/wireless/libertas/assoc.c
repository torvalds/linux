/* Copyright (C) 2006, Red Hat, Inc. */

#include <linux/bitops.h>
#include <net/ieee80211.h>

#include "assoc.h"
#include "join.h"
#include "decl.h"
#include "hostcmd.h"
#include "host.h"


static const u8 bssid_any[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 bssid_off[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static int assoc_helper_essid(wlan_private *priv,
                              struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct bss_descriptor * bss;

	lbs_deb_enter(LBS_DEB_ASSOC);

	lbs_deb_assoc("New SSID requested: %s\n", assoc_req->ssid.ssid);
	if (assoc_req->mode == IW_MODE_INFRA) {
		if (adapter->prescan) {
			libertas_send_specific_SSID_scan(priv, &assoc_req->ssid, 0);
		}

		bss = libertas_find_SSID_in_list(adapter, &assoc_req->ssid,
				NULL, IW_MODE_INFRA);
		if (bss != NULL) {
			lbs_deb_assoc("SSID found in scan list, associating\n");
			ret = wlan_associate(priv, bss);
			if (ret == 0) {
				memcpy(&assoc_req->bssid, bss->bssid, ETH_ALEN);
			}
		} else {
			lbs_deb_assoc("SSID '%s' not found; cannot associate\n",
				assoc_req->ssid.ssid);
		}
	} else if (assoc_req->mode == IW_MODE_ADHOC) {
		/* Scan for the network, do not save previous results.  Stale
		 *   scan data will cause us to join a non-existant adhoc network
		 */
		libertas_send_specific_SSID_scan(priv, &assoc_req->ssid, 1);

		/* Search for the requested SSID in the scan table */
		bss = libertas_find_SSID_in_list(adapter, &assoc_req->ssid, NULL,
				IW_MODE_ADHOC);
		if (bss != NULL) {
			lbs_deb_assoc("SSID found joining\n");
			libertas_join_adhoc_network(priv, bss);
		} else {
			/* else send START command */
			lbs_deb_assoc("SSID not found in list, so creating adhoc"
				" with SSID '%s'\n", assoc_req->ssid.ssid);
			libertas_start_adhoc_network(priv, &assoc_req->ssid);
		}
		memcpy(&assoc_req->bssid, &adapter->current_addr, ETH_ALEN);
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_bssid(wlan_private *priv,
                              struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct bss_descriptor * bss;

	lbs_deb_enter_args(LBS_DEB_ASSOC, "BSSID" MAC_FMT "\n",
		MAC_ARG(assoc_req->bssid));

	/* Search for index position in list for requested MAC */
	bss = libertas_find_BSSID_in_list(adapter, assoc_req->bssid,
			    assoc_req->mode);
	if (bss == NULL) {
		lbs_deb_assoc("ASSOC: WAP: BSSID " MAC_FMT " not found, "
			"cannot associate.\n", MAC_ARG(assoc_req->bssid));
		goto out;
	}

	if (assoc_req->mode == IW_MODE_INFRA) {
		ret = wlan_associate(priv, bss);
		lbs_deb_assoc("ASSOC: wlan_associate(bssid) returned %d\n", ret);
	} else if (assoc_req->mode == IW_MODE_ADHOC) {
		libertas_join_adhoc_network(priv, bss);
	}
	memcpy(&assoc_req->ssid, &bss->ssid, sizeof(struct WLAN_802_11_SSID));

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_associate(wlan_private *priv,
                                  struct assoc_request * assoc_req)
{
	int ret = 0, done = 0;

	/* If we're given and 'any' BSSID, try associating based on SSID */

	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		if (memcmp(bssid_any, assoc_req->bssid, ETH_ALEN)
		    && memcmp(bssid_off, assoc_req->bssid, ETH_ALEN)) {
			ret = assoc_helper_bssid(priv, assoc_req);
			done = 1;
			if (ret) {
				lbs_deb_assoc("ASSOC: bssid: ret = %d\n", ret);
			}
		}
	}

	if (!done && test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		ret = assoc_helper_essid(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC: bssid: ret = %d\n", ret);
		}
	}

	return ret;
}


static int assoc_helper_mode(wlan_private *priv,
                             struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (assoc_req->mode == adapter->mode)
		goto done;

	if (assoc_req->mode == IW_MODE_INFRA) {
		if (adapter->psstate != PS_STATE_FULL_POWER)
			libertas_ps_wakeup(priv, cmd_option_waitforrsp);
		adapter->psmode = wlan802_11powermodecam;
	}

	adapter->mode = assoc_req->mode;
	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_snmp_mib,
				    0, cmd_option_waitforrsp,
				    OID_802_11_INFRASTRUCTURE_MODE,
				    (void *) (size_t) assoc_req->mode);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wep_keys(wlan_private *priv,
                                 struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int i;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* Set or remove WEP keys */
	if (   assoc_req->wep_keys[0].len
	    || assoc_req->wep_keys[1].len
	    || assoc_req->wep_keys[2].len
	    || assoc_req->wep_keys[3].len) {
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_set_wep,
					    cmd_act_add,
					    cmd_option_waitforrsp,
					    0, assoc_req);
	} else {
		ret = libertas_prepare_and_send_command(priv,
					    cmd_802_11_set_wep,
					    cmd_act_remove,
					    cmd_option_waitforrsp,
					    0, NULL);
	}

	if (ret)
		goto out;

	/* enable/disable the MAC's WEP packet filter */
	if (assoc_req->secinfo.wep_enabled)
		adapter->currentpacketfilter |= cmd_act_mac_wep_enable;
	else
		adapter->currentpacketfilter &= ~cmd_act_mac_wep_enable;
	ret = libertas_set_mac_packet_filter(priv);
	if (ret)
		goto out;

	mutex_lock(&adapter->lock);

	/* Copy WEP keys into adapter wep key fields */
	for (i = 0; i < 4; i++) {
		memcpy(&adapter->wep_keys[i], &assoc_req->wep_keys[i],
			sizeof(struct WLAN_802_11_KEY));
	}
	adapter->wep_tx_keyidx = assoc_req->wep_tx_keyidx;

	mutex_unlock(&adapter->lock);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}

static int assoc_helper_secinfo(wlan_private *priv,
                                struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	memcpy(&adapter->secinfo, &assoc_req->secinfo,
		sizeof(struct wlan_802_11_security));

	ret = libertas_set_mac_packet_filter(priv);

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_keys(wlan_private *priv,
                                 struct assoc_request * assoc_req)
{
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* enable/Disable RSN */
	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_enable_rsn,
				    cmd_act_set,
				    cmd_option_waitforrsp,
				    0, assoc_req);
	if (ret)
		goto out;

	ret = libertas_prepare_and_send_command(priv,
				    cmd_802_11_key_material,
				    cmd_act_set,
				    cmd_option_waitforrsp,
				    0, assoc_req);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_ie(wlan_private *priv,
                               struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	if (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled) {
		memcpy(&adapter->wpa_ie, &assoc_req->wpa_ie, assoc_req->wpa_ie_len);
		adapter->wpa_ie_len = assoc_req->wpa_ie_len;
	} else {
		memset(&adapter->wpa_ie, 0, MAX_WPA_IE_LEN);
		adapter->wpa_ie_len = 0;
	}

	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int should_deauth_infrastructure(wlan_adapter *adapter,
                                        struct assoc_request * assoc_req)
{
	if (adapter->connect_status != libertas_connected)
		return 0;

	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		lbs_deb_assoc("Deauthenticating due to new SSID in "
			" configuration request.\n");
		return 1;
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		if (adapter->secinfo.auth_mode != assoc_req->secinfo.auth_mode) {
			lbs_deb_assoc("Deauthenticating due to updated security "
				"info in configuration request.\n");
			return 1;
		}
	}

	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		lbs_deb_assoc("Deauthenticating due to new BSSID in "
			" configuration request.\n");
		return 1;
	}

	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_INFRA)
			return 1;
	}

	return 0;
}


static int should_stop_adhoc(wlan_adapter *adapter,
                             struct assoc_request * assoc_req)
{
	if (adapter->connect_status != libertas_connected)
		return 0;

	if (adapter->curbssparams.ssid.ssidlength != assoc_req->ssid.ssidlength)
		return 1;
	if (memcmp(adapter->curbssparams.ssid.ssid, assoc_req->ssid.ssid,
			adapter->curbssparams.ssid.ssidlength))
		return 1;

	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_ADHOC)
			return 1;
	}

	return 0;
}


void libertas_association_worker(struct work_struct *work)
{
	wlan_private *priv = container_of(work, wlan_private, assoc_work.work);
	wlan_adapter *adapter = priv->adapter;
	struct assoc_request * assoc_req = NULL;
	int ret = 0;
	int find_any_ssid = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	mutex_lock(&adapter->lock);
	assoc_req = adapter->assoc_req;
	adapter->assoc_req = NULL;
	mutex_unlock(&adapter->lock);

	if (!assoc_req)
		goto done;

	lbs_deb_assoc("ASSOC: starting new association request: flags = 0x%lX\n",
		assoc_req->flags);

	/* If 'any' SSID was specified, find an SSID to associate with */
	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)
	    && !assoc_req->ssid.ssidlength)
		find_any_ssid = 1;

	/* But don't use 'any' SSID if there's a valid locked BSSID to use */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		if (memcmp(&assoc_req->bssid, bssid_any, ETH_ALEN)
		    && memcmp(&assoc_req->bssid, bssid_off, ETH_ALEN))
			find_any_ssid = 0;
	}

	if (find_any_ssid) {
		u8 new_mode;

		ret = libertas_find_best_network_SSID(priv, &assoc_req->ssid,
				assoc_req->mode, &new_mode);
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
	if (adapter->mode == IW_MODE_INFRA) {
		if (should_deauth_infrastructure(adapter, assoc_req)) {
			ret = libertas_send_deauthentication(priv);
			if (ret) {
				lbs_deb_assoc("Deauthentication due to new "
					"configuration request failed: %d\n",
					ret);
			}
		}
	} else if (adapter->mode == IW_MODE_ADHOC) {
		if (should_stop_adhoc(adapter, assoc_req)) {
			ret = libertas_stop_adhoc_network(priv);
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
		if (ret) {
lbs_deb_assoc("ASSOC(:%d) mode: ret = %d\n", __LINE__, ret);
			goto out;
		}
	}

	if (   test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags)) {
		ret = assoc_helper_wep_keys(priv, assoc_req);
		if (ret) {
lbs_deb_assoc("ASSOC(:%d) wep_keys: ret = %d\n", __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		ret = assoc_helper_secinfo(priv, assoc_req);
		if (ret) {
lbs_deb_assoc("ASSOC(:%d) secinfo: ret = %d\n", __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		ret = assoc_helper_wpa_ie(priv, assoc_req);
		if (ret) {
lbs_deb_assoc("ASSOC(:%d) wpa_ie: ret = %d\n", __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		ret = assoc_helper_wpa_keys(priv, assoc_req);
		if (ret) {
lbs_deb_assoc("ASSOC(:%d) wpa_keys: ret = %d\n", __LINE__, ret);
			goto out;
		}
	}

	/* SSID/BSSID should be the _last_ config option set, because they
	 * trigger the association attempt.
	 */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		int success = 1;

		ret = assoc_helper_associate(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC: association attempt unsuccessful: %d\n",
				ret);
			success = 0;
		}

		if (adapter->connect_status != libertas_connected) {
			lbs_deb_assoc("ASSOC: assoication attempt unsuccessful, "
				"not connected.\n");
			success = 0;
		}

		if (success) {
			lbs_deb_assoc("ASSOC: association attempt successful. "
				"Associated to '%s' (" MAC_FMT ")\n",
				assoc_req->ssid.ssid, MAC_ARG(assoc_req->bssid));
			libertas_prepare_and_send_command(priv,
				cmd_802_11_rssi,
				0, cmd_option_waitforrsp, 0, NULL);

			libertas_prepare_and_send_command(priv,
				cmd_802_11_get_log,
				0, cmd_option_waitforrsp, 0, NULL);
		} else {

			ret = -1;
		}
	}

out:
	if (ret) {
		lbs_deb_assoc("ASSOC: reconfiguration attempt unsuccessful: %d\n",
			ret);
	}
	kfree(assoc_req);

done:
	lbs_deb_leave(LBS_DEB_ASSOC);
}


/*
 * Caller MUST hold any necessary locks
 */
struct assoc_request * wlan_get_association_request(wlan_adapter *adapter)
{
	struct assoc_request * assoc_req;

	if (!adapter->assoc_req) {
		adapter->assoc_req = kzalloc(sizeof(struct assoc_request), GFP_KERNEL);
		if (!adapter->assoc_req) {
			lbs_pr_info("Not enough memory to allocate association"
				" request!\n");
			return NULL;
		}
	}

	/* Copy current configuration attributes to the association request,
	 * but don't overwrite any that are already set.
	 */
	assoc_req = adapter->assoc_req;
	if (!test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		memcpy(&assoc_req->ssid, adapter->curbssparams.ssid.ssid,
			adapter->curbssparams.ssid.ssidlength);
	}

	if (!test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		assoc_req->channel = adapter->curbssparams.channel;

	if (!test_bit(ASSOC_FLAG_MODE, &assoc_req->flags))
		assoc_req->mode = adapter->mode;

	if (!test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		memcpy(&assoc_req->bssid, adapter->curbssparams.bssid,
			ETH_ALEN);
	}

	if (!test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags)) {
		int i;
		for (i = 0; i < 4; i++) {
			memcpy(&assoc_req->wep_keys[i], &adapter->wep_keys[i],
				sizeof(struct WLAN_802_11_KEY));
		}
	}

	if (!test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags))
		assoc_req->wep_tx_keyidx = adapter->wep_tx_keyidx;

	if (!test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_mcast_key, &adapter->wpa_mcast_key,
			sizeof(struct WLAN_802_11_KEY));
	}

	if (!test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_unicast_key, &adapter->wpa_unicast_key,
			sizeof(struct WLAN_802_11_KEY));
	}

	if (!test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		memcpy(&assoc_req->secinfo, &adapter->secinfo,
			sizeof(struct wlan_802_11_security));
	}

	if (!test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_ie, &adapter->wpa_ie,
			MAX_WPA_IE_LEN);
		assoc_req->wpa_ie_len = adapter->wpa_ie_len;
	}

	return assoc_req;
}


