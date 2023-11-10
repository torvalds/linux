// SPDX-License-Identifier: GPL-2.0
/* IEEE 802.11 SoftMAC layer
 * Copyright (c) 2005 Andrea Merello <andrea.merello@gmail.com>
 *
 * Mostly extracted from the rtl8180-sa2400 driver for the
 * in-kernel generic ieee802.11 stack.
 *
 * Some pieces of code might be stolen from ipw2100 driver
 * copyright of who own it's copyright ;-)
 *
 * PS wx handler mostly stolen from hostap, copyright who
 * own it's copyright ;-)
 */
#include <linux/etherdevice.h>

#include "rtllib.h"
#include "dot11d.h"

int rtllib_wx_set_freq(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	int ret;
	struct iw_freq *fwrq = &wrqu->freq;

	mutex_lock(&ieee->wx_mutex);

	if (ieee->iw_mode == IW_MODE_INFRA) {
		ret = 0;
		goto out;
	}

	/* if setting by freq convert to channel */
	if (fwrq->e == 1) {
		if ((fwrq->m >= (int)2.412e8 &&
		     fwrq->m <= (int)2.487e8)) {
			fwrq->m = ieee80211_freq_khz_to_channel(fwrq->m / 100);
			fwrq->e = 0;
		}
	}

	if (fwrq->e > 0 || fwrq->m > 14 || fwrq->m < 1) {
		ret = -EOPNOTSUPP;
		goto out;

	} else { /* Set the channel */

		if (ieee->active_channel_map[fwrq->m] != 1) {
			ret = -EINVAL;
			goto out;
		}
		ieee->current_network.channel = fwrq->m;
		ieee->set_chan(ieee->dev, ieee->current_network.channel);
	}

	ret = 0;
out:
	mutex_unlock(&ieee->wx_mutex);
	return ret;
}
EXPORT_SYMBOL(rtllib_wx_set_freq);

int rtllib_wx_get_freq(struct rtllib_device *ieee,
			     struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct iw_freq *fwrq = &wrqu->freq;

	if (ieee->current_network.channel == 0)
		return -1;
	fwrq->m = ieee80211_channel_to_freq_khz(ieee->current_network.channel,
						NL80211_BAND_2GHZ) * 100;
	fwrq->e = 1;
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_freq);

int rtllib_wx_get_wap(struct rtllib_device *ieee,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	unsigned long flags;

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	if (ieee->iw_mode == IW_MODE_MONITOR)
		return -1;

	/* We want avoid to give to the user inconsistent infos*/
	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->link_state != MAC80211_LINKED &&
		ieee->link_state != MAC80211_LINKED_SCANNING &&
		ieee->wap_set == 0)

		eth_zero_addr(wrqu->ap_addr.sa_data);
	else
		memcpy(wrqu->ap_addr.sa_data,
		       ieee->current_network.bssid, ETH_ALEN);

	spin_unlock_irqrestore(&ieee->lock, flags);

	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_wap);

int rtllib_wx_set_wap(struct rtllib_device *ieee,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra)
{
	int ret = 0;
	unsigned long flags;

	short ifup = ieee->proto_started;
	struct sockaddr *temp = (struct sockaddr *)awrq;

	rtllib_stop_scan_syncro(ieee);

	mutex_lock(&ieee->wx_mutex);
	/* use ifconfig hw ether */

	if (temp->sa_family != ARPHRD_ETHER) {
		ret = -EINVAL;
		goto out;
	}

	if (is_zero_ether_addr(temp->sa_data)) {
		spin_lock_irqsave(&ieee->lock, flags);
		ether_addr_copy(ieee->current_network.bssid, temp->sa_data);
		ieee->wap_set = 0;
		spin_unlock_irqrestore(&ieee->lock, flags);
		ret = -1;
		goto out;
	}

	if (ifup)
		rtllib_stop_protocol(ieee);

	/* just to avoid to give inconsistent infos in the
	 * get wx method. not really needed otherwise
	 */
	spin_lock_irqsave(&ieee->lock, flags);

	ieee->cannot_notify = false;
	ether_addr_copy(ieee->current_network.bssid, temp->sa_data);
	ieee->wap_set = !is_zero_ether_addr(temp->sa_data);

	spin_unlock_irqrestore(&ieee->lock, flags);

	if (ifup)
		rtllib_start_protocol(ieee);
out:
	mutex_unlock(&ieee->wx_mutex);
	return ret;
}
EXPORT_SYMBOL(rtllib_wx_set_wap);

int rtllib_wx_get_essid(struct rtllib_device *ieee, struct iw_request_info *a,
			 union iwreq_data *wrqu, char *b)
{
	int len, ret = 0;
	unsigned long flags;

	if (ieee->iw_mode == IW_MODE_MONITOR)
		return -1;

	/* We want avoid to give to the user inconsistent infos*/
	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->current_network.ssid[0] == '\0' ||
		ieee->current_network.ssid_len == 0) {
		ret = -1;
		goto out;
	}

	if (ieee->link_state != MAC80211_LINKED &&
		ieee->link_state != MAC80211_LINKED_SCANNING &&
		ieee->ssid_set == 0) {
		ret = -1;
		goto out;
	}
	len = ieee->current_network.ssid_len;
	wrqu->essid.length = len;
	strncpy(b, ieee->current_network.ssid, len);
	wrqu->essid.flags = 1;

out:
	spin_unlock_irqrestore(&ieee->lock, flags);

	return ret;
}
EXPORT_SYMBOL(rtllib_wx_get_essid);

int rtllib_wx_set_rate(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u32 target_rate = wrqu->bitrate.value;

	ieee->rate = target_rate / 100000;
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_set_rate);

int rtllib_wx_get_rate(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u32 tmp_rate;

	tmp_rate = TxCountToDataRate(ieee,
				     ieee->softmac_stats.CurrentShowTxate);
	wrqu->bitrate.value = tmp_rate * 500000;

	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_rate);

int rtllib_wx_set_rts(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	if (wrqu->rts.disabled || !wrqu->rts.fixed) {
		ieee->rts = DEFAULT_RTS_THRESHOLD;
	} else {
		if (wrqu->rts.value < MIN_RTS_THRESHOLD ||
				wrqu->rts.value > MAX_RTS_THRESHOLD)
			return -EINVAL;
		ieee->rts = wrqu->rts.value;
	}
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_set_rts);

int rtllib_wx_get_rts(struct rtllib_device *ieee,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->rts.value = ieee->rts;
	wrqu->rts.fixed = 0;	/* no auto select */
	wrqu->rts.disabled = (wrqu->rts.value == DEFAULT_RTS_THRESHOLD);
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_rts);

int rtllib_wx_set_mode(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	int set_mode_status = 0;

	rtllib_stop_scan_syncro(ieee);
	mutex_lock(&ieee->wx_mutex);
	switch (wrqu->mode) {
	case IW_MODE_MONITOR:
	case IW_MODE_INFRA:
		break;
	case IW_MODE_AUTO:
		wrqu->mode = IW_MODE_INFRA;
		break;
	default:
		set_mode_status = -EINVAL;
		goto out;
	}

	if (wrqu->mode == ieee->iw_mode)
		goto out;

	if (wrqu->mode == IW_MODE_MONITOR) {
		ieee->dev->type = ARPHRD_IEEE80211;
		rtllib_EnableNetMonitorMode(ieee->dev, false);
	} else {
		ieee->dev->type = ARPHRD_ETHER;
		if (ieee->iw_mode == IW_MODE_MONITOR)
			rtllib_DisableNetMonitorMode(ieee->dev, false);
	}

	if (!ieee->proto_started) {
		ieee->iw_mode = wrqu->mode;
	} else {
		rtllib_stop_protocol(ieee);
		ieee->iw_mode = wrqu->mode;
		rtllib_start_protocol(ieee);
	}

out:
	mutex_unlock(&ieee->wx_mutex);
	return set_mode_status;
}
EXPORT_SYMBOL(rtllib_wx_set_mode);

void rtllib_wx_sync_scan_wq(void *data)
{
	struct rtllib_device *ieee = container_of(data, struct rtllib_device, wx_sync_scan_wq);
	short chan;
	enum ht_extchnl_offset chan_offset = 0;
	enum ht_channel_width bandwidth = 0;
	int b40M = 0;

	mutex_lock(&ieee->wx_mutex);
	if (!(ieee->softmac_features & IEEE_SOFTMAC_SCAN)) {
		rtllib_start_scan_syncro(ieee);
		goto out;
	}

	chan = ieee->current_network.channel;

	ieee->leisure_ps_leave(ieee->dev);
	/* notify AP to be in PS mode */
	rtllib_sta_ps_send_null_frame(ieee, 1);
	rtllib_sta_ps_send_null_frame(ieee, 1);

	rtllib_stop_all_queues(ieee);
	ieee->link_state = MAC80211_LINKED_SCANNING;
	ieee->link_change(ieee->dev);
	/* wait for ps packet to be kicked out successfully */
	msleep(50);

	ieee->ScanOperationBackupHandler(ieee->dev, SCAN_OPT_BACKUP);

	if (ieee->ht_info->current_ht_support && ieee->ht_info->enable_ht &&
	    ieee->ht_info->bCurBW40MHz) {
		b40M = 1;
		chan_offset = ieee->ht_info->CurSTAExtChnlOffset;
		bandwidth = (enum ht_channel_width)ieee->ht_info->bCurBW40MHz;
		ieee->set_bw_mode_handler(ieee->dev, HT_CHANNEL_WIDTH_20,
				       HT_EXTCHNL_OFFSET_NO_EXT);
	}

	rtllib_start_scan_syncro(ieee);

	if (b40M) {
		if (chan_offset == HT_EXTCHNL_OFFSET_UPPER)
			ieee->set_chan(ieee->dev, chan + 2);
		else if (chan_offset == HT_EXTCHNL_OFFSET_LOWER)
			ieee->set_chan(ieee->dev, chan - 2);
		else
			ieee->set_chan(ieee->dev, chan);
		ieee->set_bw_mode_handler(ieee->dev, bandwidth, chan_offset);
	} else {
		ieee->set_chan(ieee->dev, chan);
	}

	ieee->ScanOperationBackupHandler(ieee->dev, SCAN_OPT_RESTORE);

	ieee->link_state = MAC80211_LINKED;
	ieee->link_change(ieee->dev);

	/* Notify AP that I wake up again */
	rtllib_sta_ps_send_null_frame(ieee, 0);

	if (ieee->link_detect_info.NumRecvBcnInPeriod == 0 ||
	    ieee->link_detect_info.NumRecvDataInPeriod == 0) {
		ieee->link_detect_info.NumRecvBcnInPeriod = 1;
		ieee->link_detect_info.NumRecvDataInPeriod = 1;
	}
	rtllib_wake_all_queues(ieee);

out:
	mutex_unlock(&ieee->wx_mutex);
}

int rtllib_wx_set_scan(struct rtllib_device *ieee, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	int ret = 0;

	if (ieee->iw_mode == IW_MODE_MONITOR || !(ieee->proto_started)) {
		ret = -1;
		goto out;
	}

	if (ieee->link_state == MAC80211_LINKED) {
		schedule_work(&ieee->wx_sync_scan_wq);
		/* intentionally forget to up sem */
		return 0;
	}

out:
	return ret;
}
EXPORT_SYMBOL(rtllib_wx_set_scan);

int rtllib_wx_set_essid(struct rtllib_device *ieee,
			struct iw_request_info *a,
			union iwreq_data *wrqu, char *extra)
{
	int ret = 0, len;
	short proto_started;
	unsigned long flags;

	rtllib_stop_scan_syncro(ieee);
	mutex_lock(&ieee->wx_mutex);

	proto_started = ieee->proto_started;

	len = min_t(__u16, wrqu->essid.length, IW_ESSID_MAX_SIZE);

	if (ieee->iw_mode == IW_MODE_MONITOR) {
		ret = -1;
		goto out;
	}

	if (proto_started)
		rtllib_stop_protocol(ieee);

	/* this is just to be sure that the GET wx callback
	 * has consistent infos. not needed otherwise
	 */
	spin_lock_irqsave(&ieee->lock, flags);

	if (wrqu->essid.flags && wrqu->essid.length) {
		strncpy(ieee->current_network.ssid, extra, len);
		ieee->current_network.ssid_len = len;
		ieee->cannot_notify = false;
		ieee->ssid_set = 1;
	} else {
		ieee->ssid_set = 0;
		ieee->current_network.ssid[0] = '\0';
		ieee->current_network.ssid_len = 0;
	}
	spin_unlock_irqrestore(&ieee->lock, flags);

	if (proto_started)
		rtllib_start_protocol(ieee);
out:
	mutex_unlock(&ieee->wx_mutex);
	return ret;
}
EXPORT_SYMBOL(rtllib_wx_set_essid);

int rtllib_wx_get_mode(struct rtllib_device *ieee, struct iw_request_info *a,
		       union iwreq_data *wrqu, char *b)
{
	wrqu->mode = ieee->iw_mode;
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_mode);

int rtllib_wx_get_name(struct rtllib_device *ieee, struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	const char *n = ieee->mode & (WIRELESS_MODE_N_24G) ? "n" : "";

	scnprintf(wrqu->name, sizeof(wrqu->name), "802.11bg%s", n);
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_name);

/* this is mostly stolen from hostap */
int rtllib_wx_set_power(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	if ((!ieee->sta_wake_up) ||
	    (!ieee->enter_sleep_state) ||
	    (!ieee->ps_is_queue_empty)) {
		netdev_warn(ieee->dev,
			    "%s(): PS mode is tried to be use but driver missed a callback\n",
			    __func__);
		return -1;
	}

	mutex_lock(&ieee->wx_mutex);

	if (wrqu->power.disabled) {
		ieee->ps = RTLLIB_PS_DISABLED;
		goto exit;
	}
	if (wrqu->power.flags & IW_POWER_TIMEOUT)
		ieee->ps_timeout = wrqu->power.value / 1000;

	if (wrqu->power.flags & IW_POWER_PERIOD)
		ieee->ps_period = wrqu->power.value / 1000;

	switch (wrqu->power.flags & IW_POWER_MODE) {
	case IW_POWER_UNICAST_R:
		ieee->ps = RTLLIB_PS_UNICAST;
		break;
	case IW_POWER_MULTICAST_R:
		ieee->ps = RTLLIB_PS_MBCAST;
		break;
	case IW_POWER_ALL_R:
		ieee->ps = RTLLIB_PS_UNICAST | RTLLIB_PS_MBCAST;
		break;

	case IW_POWER_ON:
		break;

	default:
		ret = -EINVAL;
		goto exit;
	}
exit:
	mutex_unlock(&ieee->wx_mutex);
	return ret;
}
EXPORT_SYMBOL(rtllib_wx_set_power);

/* this is stolen from hostap */
int rtllib_wx_get_power(struct rtllib_device *ieee,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *extra)
{
	mutex_lock(&ieee->wx_mutex);

	if (ieee->ps == RTLLIB_PS_DISABLED) {
		wrqu->power.disabled = 1;
		goto exit;
	}

	wrqu->power.disabled = 0;

	if ((wrqu->power.flags & IW_POWER_TYPE) == IW_POWER_TIMEOUT) {
		wrqu->power.flags = IW_POWER_TIMEOUT;
		wrqu->power.value = ieee->ps_timeout * 1000;
	} else {
		wrqu->power.flags = IW_POWER_PERIOD;
		wrqu->power.value = ieee->ps_period * 1000;
	}

	if ((ieee->ps & (RTLLIB_PS_MBCAST | RTLLIB_PS_UNICAST)) ==
	    (RTLLIB_PS_MBCAST | RTLLIB_PS_UNICAST))
		wrqu->power.flags |= IW_POWER_ALL_R;
	else if (ieee->ps & RTLLIB_PS_MBCAST)
		wrqu->power.flags |= IW_POWER_MULTICAST_R;
	else
		wrqu->power.flags |= IW_POWER_UNICAST_R;

exit:
	mutex_unlock(&ieee->wx_mutex);
	return 0;
}
EXPORT_SYMBOL(rtllib_wx_get_power);
