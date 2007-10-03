/* Copyright (C) 2006, Red Hat, Inc. */

#include <linux/bitops.h>
#include <net/ieee80211.h>
#include <linux/etherdevice.h>

#include "assoc.h"
#include "join.h"
#include "decl.h"
#include "hostcmd.h"
#include "host.h"


static const u8 bssid_any[ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 bssid_off[ETH_ALEN] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static void print_assoc_req(const char * extra, struct assoc_request * assoc_req)
{
	DECLARE_MAC_BUF(mac);
	lbs_deb_assoc(
	       "#### Association Request: %s\n"
	       "       flags:      0x%08lX\n"
	       "       SSID:       '%s'\n"
	       "       channel:    %d\n"
	       "       band:       %d\n"
	       "       mode:       %d\n"
	       "       BSSID:      %s\n"
	       "       Encryption:%s%s%s\n"
	       "       auth:       %d\n",
	       extra, assoc_req->flags,
	       escape_essid(assoc_req->ssid, assoc_req->ssid_len),
	       assoc_req->channel, assoc_req->band, assoc_req->mode,
	       print_mac(mac, assoc_req->bssid),
	       assoc_req->secinfo.WPAenabled ? " WPA" : "",
	       assoc_req->secinfo.WPA2enabled ? " WPA2" : "",
	       assoc_req->secinfo.wep_enabled ? " WEP" : "",
	       assoc_req->secinfo.auth_mode);
}


static int assoc_helper_essid(wlan_private *priv,
                              struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;
	struct bss_descriptor * bss;
	int channel = -1;

	lbs_deb_enter(LBS_DEB_ASSOC);

	/* FIXME: take channel into account when picking SSIDs if a channel
	 * is set.
	 */

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		channel = assoc_req->channel;

	lbs_deb_assoc("New SSID requested: '%s'\n",
	              escape_essid(assoc_req->ssid, assoc_req->ssid_len));
	if (assoc_req->mode == IW_MODE_INFRA) {
		libertas_send_specific_ssid_scan(priv, assoc_req->ssid,
			assoc_req->ssid_len, 0);

		bss = libertas_find_ssid_in_list(adapter, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_INFRA, channel);
		if (bss != NULL) {
			lbs_deb_assoc("SSID found in scan list, associating\n");
			memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
			ret = wlan_associate(priv, assoc_req);
		} else {
			lbs_deb_assoc("SSID not found; cannot associate\n");
		}
	} else if (assoc_req->mode == IW_MODE_ADHOC) {
		/* Scan for the network, do not save previous results.  Stale
		 *   scan data will cause us to join a non-existant adhoc network
		 */
		libertas_send_specific_ssid_scan(priv, assoc_req->ssid,
			assoc_req->ssid_len, 1);

		/* Search for the requested SSID in the scan table */
		bss = libertas_find_ssid_in_list(adapter, assoc_req->ssid,
				assoc_req->ssid_len, NULL, IW_MODE_ADHOC, channel);
		if (bss != NULL) {
			lbs_deb_assoc("SSID found, will join\n");
			memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
			libertas_join_adhoc_network(priv, assoc_req);
		} else {
			/* else send START command */
			lbs_deb_assoc("SSID not found, creating adhoc network\n");
			memcpy(&assoc_req->bss.ssid, &assoc_req->ssid,
				IW_ESSID_MAX_SIZE);
			assoc_req->bss.ssid_len = assoc_req->ssid_len;
			libertas_start_adhoc_network(priv, assoc_req);
		}
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
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter_args(LBS_DEB_ASSOC, "BSSID %s",
		print_mac(mac, assoc_req->bssid));

	/* Search for index position in list for requested MAC */
	bss = libertas_find_bssid_in_list(adapter, assoc_req->bssid,
			    assoc_req->mode);
	if (bss == NULL) {
		lbs_deb_assoc("ASSOC: WAP: BSSID %s not found, "
			"cannot associate.\n", print_mac(mac, assoc_req->bssid));
		goto out;
	}

	memcpy(&assoc_req->bss, bss, sizeof(struct bss_descriptor));
	if (assoc_req->mode == IW_MODE_INFRA) {
		ret = wlan_associate(priv, assoc_req);
		lbs_deb_assoc("ASSOC: wlan_associate(bssid) returned %d\n", ret);
	} else if (assoc_req->mode == IW_MODE_ADHOC) {
		libertas_join_adhoc_network(priv, assoc_req);
	}

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
		if (compare_ether_addr(bssid_any, assoc_req->bssid)
		    && compare_ether_addr(bssid_off, assoc_req->bssid)) {
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
			libertas_ps_wakeup(priv, CMD_OPTION_WAITFORRSP);
		adapter->psmode = WLAN802_11POWERMODECAM;
	}

	adapter->mode = assoc_req->mode;
	ret = libertas_prepare_and_send_command(priv,
				    CMD_802_11_SNMP_MIB,
				    0, CMD_OPTION_WAITFORRSP,
				    OID_802_11_INFRASTRUCTURE_MODE,
		/* Shoot me now */  (void *) (size_t) assoc_req->mode);

done:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int update_channel(wlan_private * priv)
{
	/* the channel in f/w could be out of sync, get the current channel */
	return libertas_prepare_and_send_command(priv, CMD_802_11_RF_CHANNEL,
				    CMD_OPT_802_11_RF_CHANNEL_GET,
				    CMD_OPTION_WAITFORRSP, 0, NULL);
}

void libertas_sync_channel(struct work_struct *work)
{
	wlan_private *priv = container_of(work, wlan_private, sync_channel);

	if (update_channel(priv) != 0)
		lbs_pr_info("Channel synchronization failed.");
}

static int assoc_helper_channel(wlan_private *priv,
                                struct assoc_request * assoc_req)
{
	wlan_adapter *adapter = priv->adapter;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	ret = update_channel(priv);
	if (ret < 0) {
		lbs_deb_assoc("ASSOC: channel: error getting channel.");
	}

	if (assoc_req->channel == adapter->curbssparams.channel)
		goto done;

	lbs_deb_assoc("ASSOC: channel: %d -> %d\n",
	       adapter->curbssparams.channel, assoc_req->channel);

	ret = libertas_prepare_and_send_command(priv, CMD_802_11_RF_CHANNEL,
				CMD_OPT_802_11_RF_CHANNEL_SET,
				CMD_OPTION_WAITFORRSP, 0, &assoc_req->channel);
	if (ret < 0) {
		lbs_deb_assoc("ASSOC: channel: error setting channel.");
	}

	ret = update_channel(priv);
	if (ret < 0) {
		lbs_deb_assoc("ASSOC: channel: error getting channel.");
	}

	if (assoc_req->channel != adapter->curbssparams.channel) {
		lbs_deb_assoc("ASSOC: channel: failed to update channel to %d",
		              assoc_req->channel);
		goto done;
	}

	if (   assoc_req->secinfo.wep_enabled
	    &&   (assoc_req->wep_keys[0].len
	       || assoc_req->wep_keys[1].len
	       || assoc_req->wep_keys[2].len
	       || assoc_req->wep_keys[3].len)) {
		/* Make sure WEP keys are re-sent to firmware */
		set_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags);
	}

	/* Must restart/rejoin adhoc networks after channel change */
	set_bit(ASSOC_FLAG_SSID, &assoc_req->flags);

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
					    CMD_802_11_SET_WEP,
					    CMD_ACT_ADD,
					    CMD_OPTION_WAITFORRSP,
					    0, assoc_req);
	} else {
		ret = libertas_prepare_and_send_command(priv,
					    CMD_802_11_SET_WEP,
					    CMD_ACT_REMOVE,
					    CMD_OPTION_WAITFORRSP,
					    0, NULL);
	}

	if (ret)
		goto out;

	/* enable/disable the MAC's WEP packet filter */
	if (assoc_req->secinfo.wep_enabled)
		adapter->currentpacketfilter |= CMD_ACT_MAC_WEP_ENABLE;
	else
		adapter->currentpacketfilter &= ~CMD_ACT_MAC_WEP_ENABLE;
	ret = libertas_set_mac_packet_filter(priv);
	if (ret)
		goto out;

	mutex_lock(&adapter->lock);

	/* Copy WEP keys into adapter wep key fields */
	for (i = 0; i < 4; i++) {
		memcpy(&adapter->wep_keys[i], &assoc_req->wep_keys[i],
			sizeof(struct enc_key));
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
	u32 do_wpa;
	u32 rsn = 0;

	lbs_deb_enter(LBS_DEB_ASSOC);

	memcpy(&adapter->secinfo, &assoc_req->secinfo,
		sizeof(struct wlan_802_11_security));

	ret = libertas_set_mac_packet_filter(priv);
	if (ret)
		goto out;

	/* If RSN is already enabled, don't try to enable it again, since
	 * ENABLE_RSN resets internal state machines and will clobber the
	 * 4-way WPA handshake.
	 */

	/* Get RSN enabled/disabled */
	ret = libertas_prepare_and_send_command(priv,
				    CMD_802_11_ENABLE_RSN,
				    CMD_ACT_GET,
				    CMD_OPTION_WAITFORRSP,
				    0, &rsn);
	if (ret) {
		lbs_deb_assoc("Failed to get RSN status: %d", ret);
		goto out;
	}

	/* Don't re-enable RSN if it's already enabled */
	do_wpa = (assoc_req->secinfo.WPAenabled || assoc_req->secinfo.WPA2enabled);
	if (do_wpa == rsn)
		goto out;

	/* Set RSN enabled/disabled */
	rsn = do_wpa;
	ret = libertas_prepare_and_send_command(priv,
				    CMD_802_11_ENABLE_RSN,
				    CMD_ACT_SET,
				    CMD_OPTION_WAITFORRSP,
				    0, &rsn);

out:
	lbs_deb_leave_args(LBS_DEB_ASSOC, "ret %d", ret);
	return ret;
}


static int assoc_helper_wpa_keys(wlan_private *priv,
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
		ret = libertas_prepare_and_send_command(priv,
					CMD_802_11_KEY_MATERIAL,
					CMD_ACT_SET,
					CMD_OPTION_WAITFORRSP,
					0, assoc_req);
		assoc_req->flags = flags;
	}

	if (ret)
		goto out;

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		clear_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags);

		ret = libertas_prepare_and_send_command(priv,
					CMD_802_11_KEY_MATERIAL,
					CMD_ACT_SET,
					CMD_OPTION_WAITFORRSP,
					0, assoc_req);
		assoc_req->flags = flags;
	}

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
	if (adapter->connect_status != LIBERTAS_CONNECTED)
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

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		lbs_deb_assoc("Deauthenticating due to channel switch.\n");
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
	if (adapter->connect_status != LIBERTAS_CONNECTED)
		return 0;

	if (libertas_ssid_cmp(adapter->curbssparams.ssid,
	                      adapter->curbssparams.ssid_len,
	                      assoc_req->ssid, assoc_req->ssid_len) != 0)
		return 1;

	/* FIXME: deal with 'auto' mode somehow */
	if (test_bit(ASSOC_FLAG_MODE, &assoc_req->flags)) {
		if (assoc_req->mode != IW_MODE_ADHOC)
			return 1;
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		if (assoc_req->channel != adapter->curbssparams.channel)
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
	DECLARE_MAC_BUF(mac);

	lbs_deb_enter(LBS_DEB_ASSOC);

	mutex_lock(&adapter->lock);
	assoc_req = adapter->pending_assoc_req;
	adapter->pending_assoc_req = NULL;
	adapter->in_progress_assoc_req = assoc_req;
	mutex_unlock(&adapter->lock);

	if (!assoc_req)
		goto done;

	print_assoc_req(__func__, assoc_req);

	/* If 'any' SSID was specified, find an SSID to associate with */
	if (test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)
	    && !assoc_req->ssid_len)
		find_any_ssid = 1;

	/* But don't use 'any' SSID if there's a valid locked BSSID to use */
	if (test_bit(ASSOC_FLAG_BSSID, &assoc_req->flags)) {
		if (compare_ether_addr(assoc_req->bssid, bssid_any)
		    && compare_ether_addr(assoc_req->bssid, bssid_off))
			find_any_ssid = 0;
	}

	if (find_any_ssid) {
		u8 new_mode;

		ret = libertas_find_best_network_ssid(priv, assoc_req->ssid,
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
			lbs_deb_assoc("ASSOC(:%d) mode: ret = %d\n",
			              __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags)) {
		ret = assoc_helper_channel(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC(:%d) channel: ret = %d\n",
			              __LINE__, ret);
			goto out;
		}
	}

	if (   test_bit(ASSOC_FLAG_WEP_KEYS, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags)) {
		ret = assoc_helper_wep_keys(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC(:%d) wep_keys: ret = %d\n",
			              __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_SECINFO, &assoc_req->flags)) {
		ret = assoc_helper_secinfo(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC(:%d) secinfo: ret = %d\n",
			              __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_WPA_IE, &assoc_req->flags)) {
		ret = assoc_helper_wpa_ie(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC(:%d) wpa_ie: ret = %d\n",
			              __LINE__, ret);
			goto out;
		}
	}

	if (test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)
	    || test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		ret = assoc_helper_wpa_keys(priv, assoc_req);
		if (ret) {
			lbs_deb_assoc("ASSOC(:%d) wpa_keys: ret = %d\n",
			              __LINE__, ret);
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

		if (adapter->connect_status != LIBERTAS_CONNECTED) {
			lbs_deb_assoc("ASSOC: association attempt unsuccessful, "
				"not connected.\n");
			success = 0;
		}

		if (success) {
			lbs_deb_assoc("ASSOC: association attempt successful. "
				"Associated to '%s' (%s)\n",
				escape_essid(adapter->curbssparams.ssid,
				             adapter->curbssparams.ssid_len),
				print_mac(mac, adapter->curbssparams.bssid));
			libertas_prepare_and_send_command(priv,
				CMD_802_11_RSSI,
				0, CMD_OPTION_WAITFORRSP, 0, NULL);

			libertas_prepare_and_send_command(priv,
				CMD_802_11_GET_LOG,
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

	mutex_lock(&adapter->lock);
	adapter->in_progress_assoc_req = NULL;
	mutex_unlock(&adapter->lock);
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

	if (!adapter->pending_assoc_req) {
		adapter->pending_assoc_req = kzalloc(sizeof(struct assoc_request),
		                                     GFP_KERNEL);
		if (!adapter->pending_assoc_req) {
			lbs_pr_info("Not enough memory to allocate association"
				" request!\n");
			return NULL;
		}
	}

	/* Copy current configuration attributes to the association request,
	 * but don't overwrite any that are already set.
	 */
	assoc_req = adapter->pending_assoc_req;
	if (!test_bit(ASSOC_FLAG_SSID, &assoc_req->flags)) {
		memcpy(&assoc_req->ssid, &adapter->curbssparams.ssid,
		       IW_ESSID_MAX_SIZE);
		assoc_req->ssid_len = adapter->curbssparams.ssid_len;
	}

	if (!test_bit(ASSOC_FLAG_CHANNEL, &assoc_req->flags))
		assoc_req->channel = adapter->curbssparams.channel;

	if (!test_bit(ASSOC_FLAG_BAND, &assoc_req->flags))
		assoc_req->band = adapter->curbssparams.band;

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
				sizeof(struct enc_key));
		}
	}

	if (!test_bit(ASSOC_FLAG_WEP_TX_KEYIDX, &assoc_req->flags))
		assoc_req->wep_tx_keyidx = adapter->wep_tx_keyidx;

	if (!test_bit(ASSOC_FLAG_WPA_MCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_mcast_key, &adapter->wpa_mcast_key,
			sizeof(struct enc_key));
	}

	if (!test_bit(ASSOC_FLAG_WPA_UCAST_KEY, &assoc_req->flags)) {
		memcpy(&assoc_req->wpa_unicast_key, &adapter->wpa_unicast_key,
			sizeof(struct enc_key));
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

	print_assoc_req(__func__, assoc_req);

	return assoc_req;
}
