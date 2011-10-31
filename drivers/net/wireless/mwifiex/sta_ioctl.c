/*
 * Marvell Wireless LAN device driver: functions for station ioctl
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
#include "cfg80211.h"

/*
 * Copies the multicast address list from device to driver.
 *
 * This function does not validate the destination memory for
 * size, and the calling function must ensure enough memory is
 * available.
 */
int mwifiex_copy_mcast_addr(struct mwifiex_multicast_list *mlist,
			    struct net_device *dev)
{
	int i = 0;
	struct netdev_hw_addr *ha;

	netdev_for_each_mc_addr(ha, dev)
		memcpy(&mlist->mac_list[i++], ha->addr, ETH_ALEN);

	return i;
}

/*
 * Wait queue completion handler.
 *
 * This function waits on a cmd wait queue. It also cancels the pending
 * request after waking up, in case of errors.
 */
int mwifiex_wait_queue_complete(struct mwifiex_adapter *adapter)
{
	bool cancel_flag = false;
	int status = adapter->cmd_wait_q.status;
	struct cmd_ctrl_node *cmd_queued = adapter->cmd_queued;

	adapter->cmd_queued = NULL;
	dev_dbg(adapter->dev, "cmd pending\n");
	atomic_inc(&adapter->cmd_pending);

	/* Status pending, wake up main process */
	queue_work(adapter->workqueue, &adapter->main_work);

	/* Wait for completion */
	wait_event_interruptible(adapter->cmd_wait_q.wait,
					*(cmd_queued->condition));
	if (!*(cmd_queued->condition))
		cancel_flag = true;

	if (cancel_flag) {
		mwifiex_cancel_pending_ioctl(adapter);
		dev_dbg(adapter->dev, "cmd cancel\n");
	}
	adapter->cmd_wait_q.status = 0;

	return status;
}

/*
 * This function prepares the correct firmware command and
 * issues it to set the multicast list.
 *
 * This function can be used to enable promiscuous mode, or enable all
 * multicast packets, or to enable selective multicast.
 */
int mwifiex_request_set_multicast_list(struct mwifiex_private *priv,
				struct mwifiex_multicast_list *mcast_list)
{
	int ret = 0;
	u16 old_pkt_filter;

	old_pkt_filter = priv->curr_pkt_filter;

	if (mcast_list->mode == MWIFIEX_PROMISC_MODE) {
		dev_dbg(priv->adapter->dev, "info: Enable Promiscuous mode\n");
		priv->curr_pkt_filter |= HostCmd_ACT_MAC_PROMISCUOUS_ENABLE;
		priv->curr_pkt_filter &=
			~HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE;
	} else {
		/* Multicast */
		priv->curr_pkt_filter &= ~HostCmd_ACT_MAC_PROMISCUOUS_ENABLE;
		if (mcast_list->mode == MWIFIEX_MULTICAST_MODE) {
			dev_dbg(priv->adapter->dev,
				"info: Enabling All Multicast!\n");
			priv->curr_pkt_filter |=
				HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE;
		} else {
			priv->curr_pkt_filter &=
				~HostCmd_ACT_MAC_ALL_MULTICAST_ENABLE;
			if (mcast_list->num_multicast_addr) {
				dev_dbg(priv->adapter->dev,
					"info: Set multicast list=%d\n",
				       mcast_list->num_multicast_addr);
				/* Set multicast addresses to firmware */
				if (old_pkt_filter == priv->curr_pkt_filter) {
					/* Send request to firmware */
					ret = mwifiex_send_cmd_async(priv,
						HostCmd_CMD_MAC_MULTICAST_ADR,
						HostCmd_ACT_GEN_SET, 0,
						mcast_list);
				} else {
					/* Send request to firmware */
					ret = mwifiex_send_cmd_async(priv,
						HostCmd_CMD_MAC_MULTICAST_ADR,
						HostCmd_ACT_GEN_SET, 0,
						mcast_list);
				}
			}
		}
	}
	dev_dbg(priv->adapter->dev,
		"info: old_pkt_filter=%#x, curr_pkt_filter=%#x\n",
	       old_pkt_filter, priv->curr_pkt_filter);
	if (old_pkt_filter != priv->curr_pkt_filter) {
		ret = mwifiex_send_cmd_async(priv, HostCmd_CMD_MAC_CONTROL,
					     HostCmd_ACT_GEN_SET,
					     0, &priv->curr_pkt_filter);
	}

	return ret;
}

/*
 * This function fills bss descriptor structure using provided
 * information.
 */
int mwifiex_fill_new_bss_desc(struct mwifiex_private *priv,
			      u8 *bssid, s32 rssi, u8 *ie_buf,
			      size_t ie_len, u16 beacon_period,
			      u16 cap_info_bitmap, u8 band,
			      struct mwifiex_bssdescriptor *bss_desc)
{
	int ret;

	memcpy(bss_desc->mac_address, bssid, ETH_ALEN);
	bss_desc->rssi = rssi;
	bss_desc->beacon_buf = ie_buf;
	bss_desc->beacon_buf_size = ie_len;
	bss_desc->beacon_period = beacon_period;
	bss_desc->cap_info_bitmap = cap_info_bitmap;
	bss_desc->bss_band = band;
	if (bss_desc->cap_info_bitmap & WLAN_CAPABILITY_PRIVACY) {
		dev_dbg(priv->adapter->dev, "info: InterpretIE: AP WEP enabled\n");
		bss_desc->privacy = MWIFIEX_802_11_PRIV_FILTER_8021X_WEP;
	} else {
		bss_desc->privacy = MWIFIEX_802_11_PRIV_FILTER_ACCEPT_ALL;
	}
	if (bss_desc->cap_info_bitmap & WLAN_CAPABILITY_IBSS)
		bss_desc->bss_mode = NL80211_IFTYPE_ADHOC;
	else
		bss_desc->bss_mode = NL80211_IFTYPE_STATION;

	ret = mwifiex_update_bss_desc_with_ie(priv->adapter, bss_desc,
					      ie_buf, ie_len);

	return ret;
}

/*
 * In Ad-Hoc mode, the IBSS is created if not found in scan list.
 * In both Ad-Hoc and infra mode, an deauthentication is performed
 * first.
 */
int mwifiex_bss_start(struct mwifiex_private *priv, struct cfg80211_bss *bss,
		      struct mwifiex_802_11_ssid *req_ssid)
{
	int ret;
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_bssdescriptor *bss_desc = NULL;
	u8 *beacon_ie = NULL;

	priv->scan_block = false;

	if (bss) {
		/* Allocate and fill new bss descriptor */
		bss_desc = kzalloc(sizeof(struct mwifiex_bssdescriptor),
				GFP_KERNEL);
		if (!bss_desc) {
			dev_err(priv->adapter->dev, " failed to alloc bss_desc\n");
			return -ENOMEM;
		}

		beacon_ie = kmemdup(bss->information_elements,
					bss->len_beacon_ies, GFP_KERNEL);
		if (!beacon_ie) {
			kfree(bss_desc);
			dev_err(priv->adapter->dev, " failed to alloc beacon_ie\n");
			return -ENOMEM;
		}

		ret = mwifiex_fill_new_bss_desc(priv, bss->bssid, bss->signal,
						beacon_ie, bss->len_beacon_ies,
						bss->beacon_interval,
						bss->capability,
						*(u8 *)bss->priv, bss_desc);
		if (ret)
			goto done;
	}

	if (priv->bss_mode == NL80211_IFTYPE_STATION) {
		/* Infra mode */
		ret = mwifiex_deauthenticate(priv, NULL);
		if (ret)
			goto done;

		ret = mwifiex_check_network_compatibility(priv, bss_desc);
		if (ret)
			goto done;

		dev_dbg(adapter->dev, "info: SSID found in scan list ... "
				      "associating...\n");

		if (!netif_queue_stopped(priv->netdev))
			netif_stop_queue(priv->netdev);

		/* Clear any past association response stored for
		 * application retrieval */
		priv->assoc_rsp_size = 0;
		ret = mwifiex_associate(priv, bss_desc);
		if (bss)
			cfg80211_put_bss(bss);
	} else {
		/* Adhoc mode */
		/* If the requested SSID matches current SSID, return */
		if (bss_desc && bss_desc->ssid.ssid_len &&
		    (!mwifiex_ssid_cmp
		     (&priv->curr_bss_params.bss_descriptor.ssid,
		      &bss_desc->ssid))) {
			kfree(bss_desc);
			kfree(beacon_ie);
			return 0;
		}

		/* Exit Adhoc mode first */
		dev_dbg(adapter->dev, "info: Sending Adhoc Stop\n");
		ret = mwifiex_deauthenticate(priv, NULL);
		if (ret)
			goto done;

		priv->adhoc_is_link_sensed = false;

		ret = mwifiex_check_network_compatibility(priv, bss_desc);

		if (!netif_queue_stopped(priv->netdev))
			netif_stop_queue(priv->netdev);

		if (!ret) {
			dev_dbg(adapter->dev, "info: network found in scan"
							" list. Joining...\n");
			ret = mwifiex_adhoc_join(priv, bss_desc);
			if (bss)
				cfg80211_put_bss(bss);
		} else {
			dev_dbg(adapter->dev, "info: Network not found in "
				"the list, creating adhoc with ssid = %s\n",
				req_ssid->ssid);
			ret = mwifiex_adhoc_start(priv, req_ssid);
		}
	}

done:
	kfree(bss_desc);
	kfree(beacon_ie);
	return ret;
}

/*
 * IOCTL request handler to set host sleep configuration.
 *
 * This function prepares the correct firmware command and
 * issues it.
 */
static int mwifiex_set_hs_params(struct mwifiex_private *priv, u16 action,
				 int cmd_type, struct mwifiex_ds_hs_cfg *hs_cfg)

{
	struct mwifiex_adapter *adapter = priv->adapter;
	int status = 0;
	u32 prev_cond = 0;

	if (!hs_cfg)
		return -ENOMEM;

	switch (action) {
	case HostCmd_ACT_GEN_SET:
		if (adapter->pps_uapsd_mode) {
			dev_dbg(adapter->dev, "info: Host Sleep IOCTL"
				" is blocked in UAPSD/PPS mode\n");
			status = -1;
			break;
		}
		if (hs_cfg->is_invoke_hostcmd) {
			if (hs_cfg->conditions == HOST_SLEEP_CFG_CANCEL) {
				if (!adapter->is_hs_configured)
					/* Already cancelled */
					break;
				/* Save previous condition */
				prev_cond = le32_to_cpu(adapter->hs_cfg
							.conditions);
				adapter->hs_cfg.conditions =
						cpu_to_le32(hs_cfg->conditions);
			} else if (hs_cfg->conditions) {
				adapter->hs_cfg.conditions =
						cpu_to_le32(hs_cfg->conditions);
				adapter->hs_cfg.gpio = (u8)hs_cfg->gpio;
				if (hs_cfg->gap)
					adapter->hs_cfg.gap = (u8)hs_cfg->gap;
			} else if (adapter->hs_cfg.conditions ==
						cpu_to_le32(
						HOST_SLEEP_CFG_CANCEL)) {
				/* Return failure if no parameters for HS
				   enable */
				status = -1;
				break;
			}
			if (cmd_type == MWIFIEX_SYNC_CMD)
				status = mwifiex_send_cmd_sync(priv,
						HostCmd_CMD_802_11_HS_CFG_ENH,
						HostCmd_ACT_GEN_SET, 0,
						&adapter->hs_cfg);
			else
				status = mwifiex_send_cmd_async(priv,
						HostCmd_CMD_802_11_HS_CFG_ENH,
						HostCmd_ACT_GEN_SET, 0,
						&adapter->hs_cfg);
			if (hs_cfg->conditions == HOST_SLEEP_CFG_CANCEL)
				/* Restore previous condition */
				adapter->hs_cfg.conditions =
						cpu_to_le32(prev_cond);
		} else {
			adapter->hs_cfg.conditions =
				cpu_to_le32(hs_cfg->conditions);
			adapter->hs_cfg.gpio = (u8)hs_cfg->gpio;
			adapter->hs_cfg.gap = (u8)hs_cfg->gap;
		}
		break;
	case HostCmd_ACT_GEN_GET:
		hs_cfg->conditions = le32_to_cpu(adapter->hs_cfg.conditions);
		hs_cfg->gpio = adapter->hs_cfg.gpio;
		hs_cfg->gap = adapter->hs_cfg.gap;
		break;
	default:
		status = -1;
		break;
	}

	return status;
}

/*
 * Sends IOCTL request to cancel the existing Host Sleep configuration.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int mwifiex_cancel_hs(struct mwifiex_private *priv, int cmd_type)
{
	struct mwifiex_ds_hs_cfg hscfg;

	hscfg.conditions = HOST_SLEEP_CFG_CANCEL;
	hscfg.is_invoke_hostcmd = true;

	return mwifiex_set_hs_params(priv, HostCmd_ACT_GEN_SET,
				    cmd_type, &hscfg);
}
EXPORT_SYMBOL_GPL(mwifiex_cancel_hs);

/*
 * Sends IOCTL request to cancel the existing Host Sleep configuration.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int mwifiex_enable_hs(struct mwifiex_adapter *adapter)
{
	struct mwifiex_ds_hs_cfg hscfg;

	if (adapter->hs_activated) {
		dev_dbg(adapter->dev, "cmd: HS Already actived\n");
		return true;
	}

	adapter->hs_activate_wait_q_woken = false;

	memset(&hscfg, 0, sizeof(struct mwifiex_hs_config_param));
	hscfg.is_invoke_hostcmd = true;

	if (mwifiex_set_hs_params(mwifiex_get_priv(adapter,
						       MWIFIEX_BSS_ROLE_STA),
				  HostCmd_ACT_GEN_SET, MWIFIEX_SYNC_CMD,
				  &hscfg)) {
		dev_err(adapter->dev, "IOCTL request HS enable failed\n");
		return false;
	}

	wait_event_interruptible(adapter->hs_activate_wait_q,
			adapter->hs_activate_wait_q_woken);

	return true;
}
EXPORT_SYMBOL_GPL(mwifiex_enable_hs);

/*
 * IOCTL request handler to get BSS information.
 *
 * This function collates the information from different driver structures
 * to send to the user.
 */
int mwifiex_get_bss_info(struct mwifiex_private *priv,
			 struct mwifiex_bss_info *info)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_bssdescriptor *bss_desc;

	if (!info)
		return -1;

	bss_desc = &priv->curr_bss_params.bss_descriptor;

	info->bss_mode = priv->bss_mode;

	memcpy(&info->ssid, &bss_desc->ssid,
	       sizeof(struct mwifiex_802_11_ssid));

	memcpy(&info->bssid, &bss_desc->mac_address, ETH_ALEN);

	info->bss_chan = bss_desc->channel;

	info->region_code = adapter->region_code;

	info->media_connected = priv->media_connected;

	info->max_power_level = priv->max_tx_power_level;
	info->min_power_level = priv->min_tx_power_level;

	info->adhoc_state = priv->adhoc_state;

	info->bcn_nf_last = priv->bcn_nf_last;

	if (priv->sec_info.wep_status == MWIFIEX_802_11_WEP_ENABLED)
		info->wep_status = true;
	else
		info->wep_status = false;

	info->is_hs_configured = adapter->is_hs_configured;
	info->is_deep_sleep = adapter->is_deep_sleep;

	return 0;
}

/*
 * The function sets band configurations.
 *
 * it performs extra checks to make sure the Ad-Hoc
 * band and channel are compatible. Otherwise it returns an error.
 *
 */
int mwifiex_set_radio_band_cfg(struct mwifiex_private *priv,
			       struct mwifiex_ds_band_cfg *radio_cfg)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	u8 infra_band, adhoc_band;
	u32 adhoc_channel;

	infra_band = (u8) radio_cfg->config_bands;
	adhoc_band = (u8) radio_cfg->adhoc_start_band;
	adhoc_channel = radio_cfg->adhoc_channel;

	/* SET Infra band */
	if ((infra_band | adapter->fw_bands) & ~adapter->fw_bands)
		return -1;

	adapter->config_bands = infra_band;

	/* SET Ad-hoc Band */
	if ((adhoc_band | adapter->fw_bands) & ~adapter->fw_bands)
		return -1;

	if (adhoc_band)
		adapter->adhoc_start_band = adhoc_band;
	adapter->chan_offset = (u8) radio_cfg->sec_chan_offset;
	/*
	 * If no adhoc_channel is supplied verify if the existing adhoc
	 * channel compiles with new adhoc_band
	 */
	if (!adhoc_channel) {
		if (!mwifiex_get_cfp_by_band_and_channel_from_cfg80211
		     (priv, adapter->adhoc_start_band,
		     priv->adhoc_channel)) {
			/* Pass back the default channel */
			radio_cfg->adhoc_channel = DEFAULT_AD_HOC_CHANNEL;
			if ((adapter->adhoc_start_band & BAND_A)
			    || (adapter->adhoc_start_band & BAND_AN))
				radio_cfg->adhoc_channel =
					DEFAULT_AD_HOC_CHANNEL_A;
		}
	} else {	/* Retrurn error if adhoc_band and
			   adhoc_channel combination is invalid */
		if (!mwifiex_get_cfp_by_band_and_channel_from_cfg80211
		    (priv, adapter->adhoc_start_band, (u16) adhoc_channel))
			return -1;
		priv->adhoc_channel = (u8) adhoc_channel;
	}
	if ((adhoc_band & BAND_GN) || (adhoc_band & BAND_AN))
		adapter->adhoc_11n_enabled = true;
	else
		adapter->adhoc_11n_enabled = false;

	return 0;
}

/*
 * The function disables auto deep sleep mode.
 */
int mwifiex_disable_auto_ds(struct mwifiex_private *priv)
{
	struct mwifiex_ds_auto_ds auto_ds;

	auto_ds.auto_ds = DEEP_SLEEP_OFF;

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_PS_MODE_ENH,
				     DIS_AUTO_PS, BITMAP_AUTO_DS, &auto_ds);
}
EXPORT_SYMBOL_GPL(mwifiex_disable_auto_ds);

/*
 * IOCTL request handler to set/get active channel.
 *
 * This function performs validity checking on channel/frequency
 * compatibility and returns failure if not valid.
 */
int mwifiex_bss_set_channel(struct mwifiex_private *priv,
			    struct mwifiex_chan_freq_power *chan)
{
	struct mwifiex_adapter *adapter = priv->adapter;
	struct mwifiex_chan_freq_power *cfp = NULL;

	if (!chan)
		return -1;

	if (!chan->channel && !chan->freq)
		return -1;
	if (adapter->adhoc_start_band & BAND_AN)
		adapter->adhoc_start_band = BAND_G | BAND_B | BAND_GN;
	else if (adapter->adhoc_start_band & BAND_A)
		adapter->adhoc_start_band = BAND_G | BAND_B;
	if (chan->channel) {
		if (chan->channel <= MAX_CHANNEL_BAND_BG)
			cfp = mwifiex_get_cfp_by_band_and_channel_from_cfg80211
					(priv, 0, (u16) chan->channel);
		if (!cfp) {
			cfp = mwifiex_get_cfp_by_band_and_channel_from_cfg80211
					(priv, BAND_A, (u16) chan->channel);
			if (cfp) {
				if (adapter->adhoc_11n_enabled)
					adapter->adhoc_start_band = BAND_A
						| BAND_AN;
				else
					adapter->adhoc_start_band = BAND_A;
			}
		}
	} else {
		if (chan->freq <= MAX_FREQUENCY_BAND_BG)
			cfp = mwifiex_get_cfp_by_band_and_freq_from_cfg80211(
							priv, 0, chan->freq);
		if (!cfp) {
			cfp = mwifiex_get_cfp_by_band_and_freq_from_cfg80211
						  (priv, BAND_A, chan->freq);
			if (cfp) {
				if (adapter->adhoc_11n_enabled)
					adapter->adhoc_start_band = BAND_A
						| BAND_AN;
				else
					adapter->adhoc_start_band = BAND_A;
			}
		}
	}
	if (!cfp || !cfp->channel) {
		dev_err(adapter->dev, "invalid channel/freq\n");
		return -1;
	}
	priv->adhoc_channel = (u8) cfp->channel;
	chan->channel = cfp->channel;
	chan->freq = cfp->freq;

	return 0;
}

/*
 * IOCTL request handler to set/get Ad-Hoc channel.
 *
 * This function prepares the correct firmware command and
 * issues it to set or get the ad-hoc channel.
 */
static int mwifiex_bss_ioctl_ibss_channel(struct mwifiex_private *priv,
					  u16 action, u16 *channel)
{
	if (action == HostCmd_ACT_GEN_GET) {
		if (!priv->media_connected) {
			*channel = priv->adhoc_channel;
			return 0;
		}
	} else {
		priv->adhoc_channel = (u8) *channel;
	}

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_RF_CHANNEL,
				    action, 0, channel);
}

/*
 * IOCTL request handler to change Ad-Hoc channel.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 *
 * The function follows the following steps to perform the change -
 *      - Get current IBSS information
 *      - Get current channel
 *      - If no change is required, return
 *      - If not connected, change channel and return
 *      - If connected,
 *          - Disconnect
 *          - Change channel
 *          - Perform specific SSID scan with same SSID
 *          - Start/Join the IBSS
 */
int
mwifiex_drv_change_adhoc_chan(struct mwifiex_private *priv, int channel)
{
	int ret;
	struct mwifiex_bss_info bss_info;
	struct mwifiex_ssid_bssid ssid_bssid;
	u16 curr_chan = 0;
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *chan;
	enum ieee80211_band band;

	memset(&bss_info, 0, sizeof(bss_info));

	/* Get BSS information */
	if (mwifiex_get_bss_info(priv, &bss_info))
		return -1;

	/* Get current channel */
	ret = mwifiex_bss_ioctl_ibss_channel(priv, HostCmd_ACT_GEN_GET,
					     &curr_chan);

	if (curr_chan == channel) {
		ret = 0;
		goto done;
	}
	dev_dbg(priv->adapter->dev, "cmd: updating channel from %d to %d\n",
			curr_chan, channel);

	if (!bss_info.media_connected) {
		ret = 0;
		goto done;
	}

	/* Do disonnect */
	memset(&ssid_bssid, 0, ETH_ALEN);
	ret = mwifiex_deauthenticate(priv, ssid_bssid.bssid);

	ret = mwifiex_bss_ioctl_ibss_channel(priv, HostCmd_ACT_GEN_SET,
					     (u16 *) &channel);

	/* Do specific SSID scanning */
	if (mwifiex_request_scan(priv, &bss_info.ssid)) {
		ret = -1;
		goto done;
	}

	band = mwifiex_band_to_radio_type(priv->curr_bss_params.band);
	chan = __ieee80211_get_channel(priv->wdev->wiphy,
			ieee80211_channel_to_frequency(channel, band));

	/* Find the BSS we want using available scan results */
	bss = cfg80211_get_bss(priv->wdev->wiphy, chan, bss_info.bssid,
			       bss_info.ssid.ssid, bss_info.ssid.ssid_len,
			       WLAN_CAPABILITY_ESS, WLAN_CAPABILITY_ESS);
	if (!bss)
		wiphy_warn(priv->wdev->wiphy, "assoc: bss %pM not in scan results\n",
			  bss_info.bssid);

	ret = mwifiex_bss_start(priv, bss, &bss_info.ssid);
done:
	return ret;
}

/*
 * IOCTL request handler to get rate.
 *
 * This function prepares the correct firmware command and
 * issues it to get the current rate if it is connected,
 * otherwise, the function returns the lowest supported rate
 * for the band.
 */
static int mwifiex_rate_ioctl_get_rate_value(struct mwifiex_private *priv,
					     struct mwifiex_rate_cfg *rate_cfg)
{
	rate_cfg->is_rate_auto = priv->is_data_rate_auto;
	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_TX_RATE_QUERY,
				     HostCmd_ACT_GEN_GET, 0, NULL);
}

/*
 * IOCTL request handler to set rate.
 *
 * This function prepares the correct firmware command and
 * issues it to set the current rate.
 *
 * The function also performs validation checking on the supplied value.
 */
static int mwifiex_rate_ioctl_set_rate_value(struct mwifiex_private *priv,
					     struct mwifiex_rate_cfg *rate_cfg)
{
	u8 rates[MWIFIEX_SUPPORTED_RATES];
	u8 *rate;
	int rate_index, ret;
	u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	u32 i;
	struct mwifiex_adapter *adapter = priv->adapter;

	if (rate_cfg->is_rate_auto) {
		memset(bitmap_rates, 0, sizeof(bitmap_rates));
		/* Support all HR/DSSS rates */
		bitmap_rates[0] = 0x000F;
		/* Support all OFDM rates */
		bitmap_rates[1] = 0x00FF;
		/* Support all HT-MCSs rate */
		for (i = 0; i < ARRAY_SIZE(priv->bitmap_rates) - 3; i++)
			bitmap_rates[i + 2] = 0xFFFF;
		bitmap_rates[9] = 0x3FFF;
	} else {
		memset(rates, 0, sizeof(rates));
		mwifiex_get_active_data_rates(priv, rates);
		rate = rates;
		for (i = 0; (rate[i] && i < MWIFIEX_SUPPORTED_RATES); i++) {
			dev_dbg(adapter->dev, "info: rate=%#x wanted=%#x\n",
				rate[i], rate_cfg->rate);
			if ((rate[i] & 0x7f) == (rate_cfg->rate & 0x7f))
				break;
		}
		if ((i == MWIFIEX_SUPPORTED_RATES) || !rate[i]) {
			dev_err(adapter->dev, "fixed data rate %#x is out "
			       "of range\n", rate_cfg->rate);
			return -1;
		}
		memset(bitmap_rates, 0, sizeof(bitmap_rates));

		rate_index = mwifiex_data_rate_to_index(rate_cfg->rate);

		/* Only allow b/g rates to be set */
		if (rate_index >= MWIFIEX_RATE_INDEX_HRDSSS0 &&
		    rate_index <= MWIFIEX_RATE_INDEX_HRDSSS3) {
			bitmap_rates[0] = 1 << rate_index;
		} else {
			rate_index -= 1; /* There is a 0x00 in the table */
			if (rate_index >= MWIFIEX_RATE_INDEX_OFDM0 &&
			    rate_index <= MWIFIEX_RATE_INDEX_OFDM7)
				bitmap_rates[1] = 1 << (rate_index -
						   MWIFIEX_RATE_INDEX_OFDM0);
		}
	}

	ret = mwifiex_send_cmd_sync(priv, HostCmd_CMD_TX_RATE_CFG,
				    HostCmd_ACT_GEN_SET, 0, bitmap_rates);

	return ret;
}

/*
 * IOCTL request handler to set/get rate.
 *
 * This function can be used to set/get either the rate value or the
 * rate index.
 */
static int mwifiex_rate_ioctl_cfg(struct mwifiex_private *priv,
				  struct mwifiex_rate_cfg *rate_cfg)
{
	int status;

	if (!rate_cfg)
		return -1;

	if (rate_cfg->action == HostCmd_ACT_GEN_GET)
		status = mwifiex_rate_ioctl_get_rate_value(priv, rate_cfg);
	else
		status = mwifiex_rate_ioctl_set_rate_value(priv, rate_cfg);

	return status;
}

/*
 * Sends IOCTL request to get the data rate.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int mwifiex_drv_get_data_rate(struct mwifiex_private *priv,
			      struct mwifiex_rate_cfg *rate)
{
	int ret;

	memset(rate, 0, sizeof(struct mwifiex_rate_cfg));
	rate->action = HostCmd_ACT_GEN_GET;
	ret = mwifiex_rate_ioctl_cfg(priv, rate);

	if (!ret) {
		if (rate->is_rate_auto)
			rate->rate = mwifiex_index_to_data_rate(priv->tx_rate,
							priv->tx_htinfo);
		else
			rate->rate = priv->data_rate;
	} else {
		ret = -1;
	}

	return ret;
}

/*
 * IOCTL request handler to set tx power configuration.
 *
 * This function prepares the correct firmware command and
 * issues it.
 *
 * For non-auto power mode, all the following power groups are set -
 *      - Modulation class HR/DSSS
 *      - Modulation class OFDM
 *      - Modulation class HTBW20
 *      - Modulation class HTBW40
 */
int mwifiex_set_tx_power(struct mwifiex_private *priv,
			 struct mwifiex_power_cfg *power_cfg)
{
	int ret;
	struct host_cmd_ds_txpwr_cfg *txp_cfg;
	struct mwifiex_types_power_group *pg_tlv;
	struct mwifiex_power_group *pg;
	u8 *buf;
	u16 dbm = 0;

	if (!power_cfg->is_power_auto) {
		dbm = (u16) power_cfg->power_level;
		if ((dbm < priv->min_tx_power_level) ||
		    (dbm > priv->max_tx_power_level)) {
			dev_err(priv->adapter->dev, "txpower value %d dBm"
					" is out of range (%d dBm-%d dBm)\n",
					dbm, priv->min_tx_power_level,
					priv->max_tx_power_level);
			return -1;
		}
	}
	buf = kzalloc(MWIFIEX_SIZE_OF_CMD_BUFFER, GFP_KERNEL);
	if (!buf) {
		dev_err(priv->adapter->dev, "%s: failed to alloc cmd buffer\n",
				__func__);
		return -ENOMEM;
	}

	txp_cfg = (struct host_cmd_ds_txpwr_cfg *) buf;
	txp_cfg->action = cpu_to_le16(HostCmd_ACT_GEN_SET);
	if (!power_cfg->is_power_auto) {
		txp_cfg->mode = cpu_to_le32(1);
		pg_tlv = (struct mwifiex_types_power_group *) (buf +
				sizeof(struct host_cmd_ds_txpwr_cfg));
		pg_tlv->type = TLV_TYPE_POWER_GROUP;
		pg_tlv->length = 4 * sizeof(struct mwifiex_power_group);
		pg = (struct mwifiex_power_group *) (buf +
				sizeof(struct host_cmd_ds_txpwr_cfg) +
				sizeof(struct mwifiex_types_power_group));
		/* Power group for modulation class HR/DSSS */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x03;
		pg->modulation_class = MOD_CLASS_HR_DSSS;
		pg->power_step = 0;
		pg->power_min = (s8) dbm;
		pg->power_max = (s8) dbm;
		pg++;
		/* Power group for modulation class OFDM */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x07;
		pg->modulation_class = MOD_CLASS_OFDM;
		pg->power_step = 0;
		pg->power_min = (s8) dbm;
		pg->power_max = (s8) dbm;
		pg++;
		/* Power group for modulation class HTBW20 */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x20;
		pg->modulation_class = MOD_CLASS_HT;
		pg->power_step = 0;
		pg->power_min = (s8) dbm;
		pg->power_max = (s8) dbm;
		pg->ht_bandwidth = HT_BW_20;
		pg++;
		/* Power group for modulation class HTBW40 */
		pg->first_rate_code = 0x00;
		pg->last_rate_code = 0x20;
		pg->modulation_class = MOD_CLASS_HT;
		pg->power_step = 0;
		pg->power_min = (s8) dbm;
		pg->power_max = (s8) dbm;
		pg->ht_bandwidth = HT_BW_40;
	}
	ret = mwifiex_send_cmd_sync(priv, HostCmd_CMD_TXPWR_CFG,
				    HostCmd_ACT_GEN_SET, 0, buf);

	kfree(buf);
	return ret;
}

/*
 * IOCTL request handler to get power save mode.
 *
 * This function prepares the correct firmware command and
 * issues it.
 */
int mwifiex_drv_set_power(struct mwifiex_private *priv, u32 *ps_mode)
{
	int ret;
	struct mwifiex_adapter *adapter = priv->adapter;
	u16 sub_cmd;

	if (*ps_mode)
		adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_PSP;
	else
		adapter->ps_mode = MWIFIEX_802_11_POWER_MODE_CAM;
	sub_cmd = (*ps_mode) ? EN_AUTO_PS : DIS_AUTO_PS;
	ret = mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_PS_MODE_ENH,
				    sub_cmd, BITMAP_STA_PS, NULL);
	if ((!ret) && (sub_cmd == DIS_AUTO_PS))
		ret = mwifiex_send_cmd_async(priv,
				HostCmd_CMD_802_11_PS_MODE_ENH, GET_PS,
				0, NULL);

	return ret;
}

/*
 * IOCTL request handler to set/reset WPA IE.
 *
 * The supplied WPA IE is treated as a opaque buffer. Only the first field
 * is checked to determine WPA version. If buffer length is zero, the existing
 * WPA IE is reset.
 */
static int mwifiex_set_wpa_ie_helper(struct mwifiex_private *priv,
				     u8 *ie_data_ptr, u16 ie_len)
{
	if (ie_len) {
		if (ie_len > sizeof(priv->wpa_ie)) {
			dev_err(priv->adapter->dev,
				"failed to copy WPA IE, too big\n");
			return -1;
		}
		memcpy(priv->wpa_ie, ie_data_ptr, ie_len);
		priv->wpa_ie_len = (u8) ie_len;
		dev_dbg(priv->adapter->dev, "cmd: Set Wpa_ie_len=%d IE=%#x\n",
				priv->wpa_ie_len, priv->wpa_ie[0]);

		if (priv->wpa_ie[0] == WLAN_EID_WPA) {
			priv->sec_info.wpa_enabled = true;
		} else if (priv->wpa_ie[0] == WLAN_EID_RSN) {
			priv->sec_info.wpa2_enabled = true;
		} else {
			priv->sec_info.wpa_enabled = false;
			priv->sec_info.wpa2_enabled = false;
		}
	} else {
		memset(priv->wpa_ie, 0, sizeof(priv->wpa_ie));
		priv->wpa_ie_len = 0;
		dev_dbg(priv->adapter->dev, "info: reset wpa_ie_len=%d IE=%#x\n",
			priv->wpa_ie_len, priv->wpa_ie[0]);
		priv->sec_info.wpa_enabled = false;
		priv->sec_info.wpa2_enabled = false;
	}

	return 0;
}

/*
 * IOCTL request handler to set/reset WAPI IE.
 *
 * The supplied WAPI IE is treated as a opaque buffer. Only the first field
 * is checked to internally enable WAPI. If buffer length is zero, the existing
 * WAPI IE is reset.
 */
static int mwifiex_set_wapi_ie(struct mwifiex_private *priv,
			       u8 *ie_data_ptr, u16 ie_len)
{
	if (ie_len) {
		if (ie_len > sizeof(priv->wapi_ie)) {
			dev_dbg(priv->adapter->dev,
				"info: failed to copy WAPI IE, too big\n");
			return -1;
		}
		memcpy(priv->wapi_ie, ie_data_ptr, ie_len);
		priv->wapi_ie_len = ie_len;
		dev_dbg(priv->adapter->dev, "cmd: Set wapi_ie_len=%d IE=%#x\n",
				priv->wapi_ie_len, priv->wapi_ie[0]);

		if (priv->wapi_ie[0] == WLAN_EID_BSS_AC_ACCESS_DELAY)
			priv->sec_info.wapi_enabled = true;
	} else {
		memset(priv->wapi_ie, 0, sizeof(priv->wapi_ie));
		priv->wapi_ie_len = ie_len;
		dev_dbg(priv->adapter->dev,
			"info: Reset wapi_ie_len=%d IE=%#x\n",
		       priv->wapi_ie_len, priv->wapi_ie[0]);
		priv->sec_info.wapi_enabled = false;
	}
	return 0;
}

/*
 * IOCTL request handler to set WAPI key.
 *
 * This function prepares the correct firmware command and
 * issues it.
 */
static int mwifiex_sec_ioctl_set_wapi_key(struct mwifiex_private *priv,
			       struct mwifiex_ds_encrypt_key *encrypt_key)
{

	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_KEY_MATERIAL,
				    HostCmd_ACT_GEN_SET, KEY_INFO_ENABLED,
				    encrypt_key);
}

/*
 * IOCTL request handler to set WEP network key.
 *
 * This function prepares the correct firmware command and
 * issues it, after validation checks.
 */
static int mwifiex_sec_ioctl_set_wep_key(struct mwifiex_private *priv,
			      struct mwifiex_ds_encrypt_key *encrypt_key)
{
	int ret;
	struct mwifiex_wep_key *wep_key;
	int index;

	if (priv->wep_key_curr_index >= NUM_WEP_KEYS)
		priv->wep_key_curr_index = 0;
	wep_key = &priv->wep_key[priv->wep_key_curr_index];
	index = encrypt_key->key_index;
	if (encrypt_key->key_disable) {
		priv->sec_info.wep_status = MWIFIEX_802_11_WEP_DISABLED;
	} else if (!encrypt_key->key_len) {
		/* Copy the required key as the current key */
		wep_key = &priv->wep_key[index];
		if (!wep_key->key_length) {
			dev_err(priv->adapter->dev,
				"key not set, so cannot enable it\n");
			return -1;
		}
		priv->wep_key_curr_index = (u16) index;
		priv->sec_info.wep_status = MWIFIEX_802_11_WEP_ENABLED;
	} else {
		wep_key = &priv->wep_key[index];
		memset(wep_key, 0, sizeof(struct mwifiex_wep_key));
		/* Copy the key in the driver */
		memcpy(wep_key->key_material,
		       encrypt_key->key_material,
		       encrypt_key->key_len);
		wep_key->key_index = index;
		wep_key->key_length = encrypt_key->key_len;
		priv->sec_info.wep_status = MWIFIEX_802_11_WEP_ENABLED;
	}
	if (wep_key->key_length) {
		/* Send request to firmware */
		ret = mwifiex_send_cmd_async(priv,
					     HostCmd_CMD_802_11_KEY_MATERIAL,
					     HostCmd_ACT_GEN_SET, 0, NULL);
		if (ret)
			return ret;
	}
	if (priv->sec_info.wep_status == MWIFIEX_802_11_WEP_ENABLED)
		priv->curr_pkt_filter |= HostCmd_ACT_MAC_WEP_ENABLE;
	else
		priv->curr_pkt_filter &= ~HostCmd_ACT_MAC_WEP_ENABLE;

	ret = mwifiex_send_cmd_sync(priv, HostCmd_CMD_MAC_CONTROL,
				    HostCmd_ACT_GEN_SET, 0,
				    &priv->curr_pkt_filter);

	return ret;
}

/*
 * IOCTL request handler to set WPA key.
 *
 * This function prepares the correct firmware command and
 * issues it, after validation checks.
 *
 * Current driver only supports key length of up to 32 bytes.
 *
 * This function can also be used to disable a currently set key.
 */
static int mwifiex_sec_ioctl_set_wpa_key(struct mwifiex_private *priv,
			      struct mwifiex_ds_encrypt_key *encrypt_key)
{
	int ret;
	u8 remove_key = false;
	struct host_cmd_ds_802_11_key_material *ibss_key;

	/* Current driver only supports key length of up to 32 bytes */
	if (encrypt_key->key_len > WLAN_MAX_KEY_LEN) {
		dev_err(priv->adapter->dev, "key length too long\n");
		return -1;
	}

	if (priv->bss_mode == NL80211_IFTYPE_ADHOC) {
		/*
		 * IBSS/WPA-None uses only one key (Group) for both receiving
		 * and sending unicast and multicast packets.
		 */
		/* Send the key as PTK to firmware */
		encrypt_key->key_index = MWIFIEX_KEY_INDEX_UNICAST;
		ret = mwifiex_send_cmd_async(priv,
					HostCmd_CMD_802_11_KEY_MATERIAL,
					HostCmd_ACT_GEN_SET, KEY_INFO_ENABLED,
					encrypt_key);
		if (ret)
			return ret;

		ibss_key = &priv->aes_key;
		memset(ibss_key, 0,
		       sizeof(struct host_cmd_ds_802_11_key_material));
		/* Copy the key in the driver */
		memcpy(ibss_key->key_param_set.key, encrypt_key->key_material,
		       encrypt_key->key_len);
		memcpy(&ibss_key->key_param_set.key_len, &encrypt_key->key_len,
		       sizeof(ibss_key->key_param_set.key_len));
		ibss_key->key_param_set.key_type_id
			= cpu_to_le16(KEY_TYPE_ID_TKIP);
		ibss_key->key_param_set.key_info = cpu_to_le16(KEY_ENABLED);

		/* Send the key as GTK to firmware */
		encrypt_key->key_index = ~MWIFIEX_KEY_INDEX_UNICAST;
	}

	if (!encrypt_key->key_index)
		encrypt_key->key_index = MWIFIEX_KEY_INDEX_UNICAST;

	if (remove_key)
		ret = mwifiex_send_cmd_sync(priv,
				       HostCmd_CMD_802_11_KEY_MATERIAL,
				       HostCmd_ACT_GEN_SET, !(KEY_INFO_ENABLED),
				       encrypt_key);
	else
		ret = mwifiex_send_cmd_sync(priv,
					HostCmd_CMD_802_11_KEY_MATERIAL,
					HostCmd_ACT_GEN_SET, KEY_INFO_ENABLED,
					encrypt_key);

	return ret;
}

/*
 * IOCTL request handler to set/get network keys.
 *
 * This is a generic key handling function which supports WEP, WPA
 * and WAPI.
 */
static int
mwifiex_sec_ioctl_encrypt_key(struct mwifiex_private *priv,
			      struct mwifiex_ds_encrypt_key *encrypt_key)
{
	int status;

	if (encrypt_key->is_wapi_key)
		status = mwifiex_sec_ioctl_set_wapi_key(priv, encrypt_key);
	else if (encrypt_key->key_len > WLAN_KEY_LEN_WEP104)
		status = mwifiex_sec_ioctl_set_wpa_key(priv, encrypt_key);
	else
		status = mwifiex_sec_ioctl_set_wep_key(priv, encrypt_key);
	return status;
}

/*
 * This function returns the driver version.
 */
int
mwifiex_drv_get_driver_version(struct mwifiex_adapter *adapter, char *version,
			       int max_len)
{
	union {
		u32 l;
		u8 c[4];
	} ver;
	char fw_ver[32];

	ver.l = adapter->fw_release_number;
	sprintf(fw_ver, "%u.%u.%u.p%u", ver.c[2], ver.c[1], ver.c[0], ver.c[3]);

	snprintf(version, max_len, driver_version, fw_ver);

	dev_dbg(adapter->dev, "info: MWIFIEX VERSION: %s\n", version);

	return 0;
}

/*
 * Sends IOCTL request to get signal information.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int mwifiex_get_signal_info(struct mwifiex_private *priv,
			    struct mwifiex_ds_get_signal *signal)
{
	int status;

	signal->selector = ALL_RSSI_INFO_MASK;

	/* Signal info can be obtained only if connected */
	if (!priv->media_connected) {
		dev_dbg(priv->adapter->dev,
			"info: Can not get signal in disconnected state\n");
		return -1;
	}

	status = mwifiex_send_cmd_sync(priv, HostCmd_CMD_RSSI_INFO,
				       HostCmd_ACT_GEN_GET, 0, signal);

	if (!status) {
		if (signal->selector & BCN_RSSI_AVG_MASK)
			priv->qual_level = signal->bcn_rssi_avg;
		if (signal->selector & BCN_NF_AVG_MASK)
			priv->qual_noise = signal->bcn_nf_avg;
	}

	return status;
}

/*
 * Sends IOCTL request to set encoding parameters.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int mwifiex_set_encode(struct mwifiex_private *priv, const u8 *key,
			int key_len, u8 key_index, int disable)
{
	struct mwifiex_ds_encrypt_key encrypt_key;

	memset(&encrypt_key, 0, sizeof(struct mwifiex_ds_encrypt_key));
	encrypt_key.key_len = key_len;
	if (!disable) {
		encrypt_key.key_index = key_index;
		if (key_len)
			memcpy(encrypt_key.key_material, key, key_len);
	} else {
		encrypt_key.key_disable = true;
	}

	return mwifiex_sec_ioctl_encrypt_key(priv, &encrypt_key);
}

/*
 * Sends IOCTL request to get extended version.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int
mwifiex_get_ver_ext(struct mwifiex_private *priv)
{
	struct mwifiex_ver_ext ver_ext;

	memset(&ver_ext, 0, sizeof(struct host_cmd_ds_version_ext));
	if (mwifiex_send_cmd_sync(priv, HostCmd_CMD_VERSION_EXT,
				    HostCmd_ACT_GEN_GET, 0, &ver_ext))
		return -1;

	return 0;
}

/*
 * Sends IOCTL request to get statistics information.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int
mwifiex_get_stats_info(struct mwifiex_private *priv,
		       struct mwifiex_ds_get_stats *log)
{
	return mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_GET_LOG,
				    HostCmd_ACT_GEN_GET, 0, log);
}

/*
 * IOCTL request handler to read/write register.
 *
 * This function prepares the correct firmware command and
 * issues it.
 *
 * Access to the following registers are supported -
 *      - MAC
 *      - BBP
 *      - RF
 *      - PMIC
 *      - CAU
 */
static int mwifiex_reg_mem_ioctl_reg_rw(struct mwifiex_private *priv,
					struct mwifiex_ds_reg_rw *reg_rw,
					u16 action)
{
	u16 cmd_no;

	switch (le32_to_cpu(reg_rw->type)) {
	case MWIFIEX_REG_MAC:
		cmd_no = HostCmd_CMD_MAC_REG_ACCESS;
		break;
	case MWIFIEX_REG_BBP:
		cmd_no = HostCmd_CMD_BBP_REG_ACCESS;
		break;
	case MWIFIEX_REG_RF:
		cmd_no = HostCmd_CMD_RF_REG_ACCESS;
		break;
	case MWIFIEX_REG_PMIC:
		cmd_no = HostCmd_CMD_PMIC_REG_ACCESS;
		break;
	case MWIFIEX_REG_CAU:
		cmd_no = HostCmd_CMD_CAU_REG_ACCESS;
		break;
	default:
		return -1;
	}

	return mwifiex_send_cmd_sync(priv, cmd_no, action, 0, reg_rw);

}

/*
 * Sends IOCTL request to write to a register.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int
mwifiex_reg_write(struct mwifiex_private *priv, u32 reg_type,
		  u32 reg_offset, u32 reg_value)
{
	struct mwifiex_ds_reg_rw reg_rw;

	reg_rw.type = cpu_to_le32(reg_type);
	reg_rw.offset = cpu_to_le32(reg_offset);
	reg_rw.value = cpu_to_le32(reg_value);

	return mwifiex_reg_mem_ioctl_reg_rw(priv, &reg_rw, HostCmd_ACT_GEN_SET);
}

/*
 * Sends IOCTL request to read from a register.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int
mwifiex_reg_read(struct mwifiex_private *priv, u32 reg_type,
		 u32 reg_offset, u32 *value)
{
	int ret;
	struct mwifiex_ds_reg_rw reg_rw;

	reg_rw.type = cpu_to_le32(reg_type);
	reg_rw.offset = cpu_to_le32(reg_offset);
	ret = mwifiex_reg_mem_ioctl_reg_rw(priv, &reg_rw, HostCmd_ACT_GEN_GET);

	if (ret)
		goto done;

	*value = le32_to_cpu(reg_rw.value);

done:
	return ret;
}

/*
 * Sends IOCTL request to read from EEPROM.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int
mwifiex_eeprom_read(struct mwifiex_private *priv, u16 offset, u16 bytes,
		    u8 *value)
{
	int ret;
	struct mwifiex_ds_read_eeprom rd_eeprom;

	rd_eeprom.offset = cpu_to_le16((u16) offset);
	rd_eeprom.byte_count = cpu_to_le16((u16) bytes);

	/* Send request to firmware */
	ret = mwifiex_send_cmd_sync(priv, HostCmd_CMD_802_11_EEPROM_ACCESS,
				    HostCmd_ACT_GEN_GET, 0, &rd_eeprom);

	if (!ret)
		memcpy(value, rd_eeprom.value, MAX_EEPROM_DATA);
	return ret;
}

/*
 * This function sets a generic IE. In addition to generic IE, it can
 * also handle WPA, WPA2 and WAPI IEs.
 */
static int
mwifiex_set_gen_ie_helper(struct mwifiex_private *priv, u8 *ie_data_ptr,
			  u16 ie_len)
{
	int ret = 0;
	struct ieee_types_vendor_header *pvendor_ie;
	const u8 wpa_oui[] = { 0x00, 0x50, 0xf2, 0x01 };
	const u8 wps_oui[] = { 0x00, 0x50, 0xf2, 0x04 };

	/* If the passed length is zero, reset the buffer */
	if (!ie_len) {
		priv->gen_ie_buf_len = 0;
		priv->wps.session_enable = false;

		return 0;
	} else if (!ie_data_ptr) {
		return -1;
	}
	pvendor_ie = (struct ieee_types_vendor_header *) ie_data_ptr;
	/* Test to see if it is a WPA IE, if not, then it is a gen IE */
	if (((pvendor_ie->element_id == WLAN_EID_WPA)
	     && (!memcmp(pvendor_ie->oui, wpa_oui, sizeof(wpa_oui))))
			|| (pvendor_ie->element_id == WLAN_EID_RSN)) {

		/* IE is a WPA/WPA2 IE so call set_wpa function */
		ret = mwifiex_set_wpa_ie_helper(priv, ie_data_ptr, ie_len);
		priv->wps.session_enable = false;

		return ret;
	} else if (pvendor_ie->element_id == WLAN_EID_BSS_AC_ACCESS_DELAY) {
		/* IE is a WAPI IE so call set_wapi function */
		ret = mwifiex_set_wapi_ie(priv, ie_data_ptr, ie_len);

		return ret;
	}
	/*
	 * Verify that the passed length is not larger than the
	 * available space remaining in the buffer
	 */
	if (ie_len < (sizeof(priv->gen_ie_buf) - priv->gen_ie_buf_len)) {

		/* Test to see if it is a WPS IE, if so, enable
		 * wps session flag
		 */
		pvendor_ie = (struct ieee_types_vendor_header *) ie_data_ptr;
		if ((pvendor_ie->element_id == WLAN_EID_VENDOR_SPECIFIC)
				&& (!memcmp(pvendor_ie->oui, wps_oui,
						sizeof(wps_oui)))) {
			priv->wps.session_enable = true;
			dev_dbg(priv->adapter->dev,
				"info: WPS Session Enabled.\n");
		}

		/* Append the passed data to the end of the
		   genIeBuffer */
		memcpy(priv->gen_ie_buf + priv->gen_ie_buf_len, ie_data_ptr,
									ie_len);
		/* Increment the stored buffer length by the
		   size passed */
		priv->gen_ie_buf_len += ie_len;
	} else {
		/* Passed data does not fit in the remaining
		   buffer space */
		ret = -1;
	}

	/* Return 0, or -1 for error case */
	return ret;
}

/*
 * IOCTL request handler to set/get generic IE.
 *
 * In addition to various generic IEs, this function can also be
 * used to set the ARP filter.
 */
static int mwifiex_misc_ioctl_gen_ie(struct mwifiex_private *priv,
				     struct mwifiex_ds_misc_gen_ie *gen_ie,
				     u16 action)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	switch (gen_ie->type) {
	case MWIFIEX_IE_TYPE_GEN_IE:
		if (action == HostCmd_ACT_GEN_GET) {
			gen_ie->len = priv->wpa_ie_len;
			memcpy(gen_ie->ie_data, priv->wpa_ie, gen_ie->len);
		} else {
			mwifiex_set_gen_ie_helper(priv, gen_ie->ie_data,
						  (u16) gen_ie->len);
		}
		break;
	case MWIFIEX_IE_TYPE_ARP_FILTER:
		memset(adapter->arp_filter, 0, sizeof(adapter->arp_filter));
		if (gen_ie->len > ARP_FILTER_MAX_BUF_SIZE) {
			adapter->arp_filter_size = 0;
			dev_err(adapter->dev, "invalid ARP filter size\n");
			return -1;
		} else {
			memcpy(adapter->arp_filter, gen_ie->ie_data,
								gen_ie->len);
			adapter->arp_filter_size = gen_ie->len;
		}
		break;
	default:
		dev_err(adapter->dev, "invalid IE type\n");
		return -1;
	}
	return 0;
}

/*
 * Sends IOCTL request to set a generic IE.
 *
 * This function allocates the IOCTL request buffer, fills it
 * with requisite parameters and calls the IOCTL handler.
 */
int
mwifiex_set_gen_ie(struct mwifiex_private *priv, u8 *ie, int ie_len)
{
	struct mwifiex_ds_misc_gen_ie gen_ie;

	if (ie_len > IEEE_MAX_IE_SIZE)
		return -EFAULT;

	gen_ie.type = MWIFIEX_IE_TYPE_GEN_IE;
	gen_ie.len = ie_len;
	memcpy(gen_ie.ie_data, ie, ie_len);
	if (mwifiex_misc_ioctl_gen_ie(priv, &gen_ie, HostCmd_ACT_GEN_SET))
		return -EFAULT;

	return 0;
}
