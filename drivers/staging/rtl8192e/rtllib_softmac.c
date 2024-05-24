// SPDX-License-Identifier: GPL-2.0
/* IEEE 802.11 SoftMAC layer
 * Copyright (c) 2005 Andrea Merello <andrea.merello@gmail.com>
 *
 * Mostly extracted from the rtl8180-sa2400 driver for the
 * in-kernel generic ieee802.11 stack.
 *
 * Few lines might be stolen from other part of the rtllib
 * stack. Copyright who own it's copyright
 *
 * WPA code stolen from the ipw2200 driver.
 * Copyright who own it's copyright.
 */
#include "rtllib.h"

#include <linux/random.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>

static void rtllib_sta_wakeup(struct rtllib_device *ieee, short nl);

static short rtllib_is_54g(struct rtllib_network *net)
{
	return (net->rates_ex_len > 0) || (net->rates_len > 4);
}

/* returns the total length needed for placing the RATE MFIE
 * tag and the EXTENDED RATE MFIE tag if needed.
 * It encludes two bytes per tag for the tag itself and its len
 */
static unsigned int rtllib_MFIE_rate_len(struct rtllib_device *ieee)
{
	unsigned int rate_len = 0;

	rate_len = RTLLIB_CCK_RATE_LEN + 2;
	rate_len += RTLLIB_OFDM_RATE_LEN + 2;

	return rate_len;
}

/* place the MFIE rate, tag to the memory (double) pointed.
 * Then it updates the pointer so that
 * it points after the new MFIE tag added.
 */
static void rtllib_mfie_brate(struct rtllib_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	*tag++ = MFIE_TYPE_RATES;
	*tag++ = 4;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_1MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_2MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_5MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_11MB;

	/* We may add an option for custom rates that specific HW
	 * might support
	 */
	*tag_p = tag;
}

static void rtllib_mfie_grate(struct rtllib_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	*tag++ = MFIE_TYPE_RATES_EX;
	*tag++ = 8;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_6MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_9MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_12MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_18MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_24MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_36MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_48MB;
	*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_OFDM_RATE_54MB;

	/* We may add an option for custom rates that specific HW might
	 * support
	 */
	*tag_p = tag;
}

static void rtllib_wmm_info(struct rtllib_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	*tag++ = MFIE_TYPE_GENERIC;
	*tag++ = 7;
	*tag++ = 0x00;
	*tag++ = 0x50;
	*tag++ = 0xf2;
	*tag++ = 0x02;
	*tag++ = 0x00;
	*tag++ = 0x01;
	*tag++ = MAX_SP_Len;
	*tag_p = tag;
}

static void rtllib_turbo_info(struct rtllib_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	*tag++ = MFIE_TYPE_GENERIC;
	*tag++ = 7;
	*tag++ = 0x00;
	*tag++ = 0xe0;
	*tag++ = 0x4c;
	*tag++ = 0x01;
	*tag++ = 0x02;
	*tag++ = 0x11;
	*tag++ = 0x00;

	*tag_p = tag;
	netdev_alert(ieee->dev, "This is enable turbo mode IE process\n");
}

static void enqueue_mgmt(struct rtllib_device *ieee, struct sk_buff *skb)
{
	int nh;

	nh = (ieee->mgmt_queue_head + 1) % MGMT_QUEUE_NUM;

/* if the queue is full but we have newer frames then
 * just overwrites the oldest.
 *
 * if (nh == ieee->mgmt_queue_tail)
 *		return -1;
 */
	ieee->mgmt_queue_head = nh;
	ieee->mgmt_queue_ring[nh] = skb;
}

static void init_mgmt_queue(struct rtllib_device *ieee)
{
	ieee->mgmt_queue_tail = 0;
	ieee->mgmt_queue_head = 0;
}

u8 mgnt_query_tx_rate_exclude_cck_rates(struct rtllib_device *ieee)
{
	u16	i;
	u8	query_rate = 0;
	u8	basic_rate;

	for (i = 0; i < ieee->current_network.rates_len; i++) {
		basic_rate = ieee->current_network.rates[i] & 0x7F;
		if (!rtllib_is_cck_rate(basic_rate)) {
			if (query_rate == 0) {
				query_rate = basic_rate;
			} else {
				if (basic_rate < query_rate)
					query_rate = basic_rate;
			}
		}
	}

	if (query_rate == 0) {
		query_rate = 12;
		netdev_info(ieee->dev, "No basic_rate found!!\n");
	}
	return query_rate;
}

static u8 mgnt_query_mgnt_frame_tx_rate(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *ht_info = ieee->ht_info;
	u8 rate;

	if (ht_info->iot_action & HT_IOT_ACT_MGNT_USE_CCK_6M)
		rate = 0x0c;
	else
		rate = ieee->basic_rate & 0x7f;

	if (rate == 0)
		rate = 0x02;

	return rate;
}

inline void softmac_mgmt_xmit(struct sk_buff *skb, struct rtllib_device *ieee)
{
	unsigned long flags;
	short single = ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE;
	struct ieee80211_hdr_3addr  *header =
		(struct ieee80211_hdr_3addr  *)skb->data;

	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + 8);

	spin_lock_irqsave(&ieee->lock, flags);

	/* called with 2nd param 0, no mgmt lock required */
	rtllib_sta_wakeup(ieee, 0);

	if (ieee80211_is_beacon(header->frame_control))
		tcb_desc->queue_index = BEACON_QUEUE;
	else
		tcb_desc->queue_index = MGNT_QUEUE;

	if (ieee->disable_mgnt_queue)
		tcb_desc->queue_index = HIGH_QUEUE;

	tcb_desc->data_rate = mgnt_query_mgnt_frame_tx_rate(ieee);
	tcb_desc->ratr_index = 7;
	tcb_desc->tx_dis_rate_fallback = 1;
	tcb_desc->tx_use_drv_assinged_rate = 1;
	if (single) {
		if (ieee->queue_stop) {
			enqueue_mgmt(ieee, skb);
		} else {
			header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

			if (ieee->seq_ctrl[0] == 0xFFF)
				ieee->seq_ctrl[0] = 0;
			else
				ieee->seq_ctrl[0]++;

			/* avoid watchdog triggers */
			ieee->softmac_data_hard_start_xmit(skb, ieee->dev,
							   ieee->basic_rate);
		}

		spin_unlock_irqrestore(&ieee->lock, flags);
	} else {
		spin_unlock_irqrestore(&ieee->lock, flags);
		spin_lock_irqsave(&ieee->mgmt_tx_lock, flags);

		header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

		if (ieee->seq_ctrl[0] == 0xFFF)
			ieee->seq_ctrl[0] = 0;
		else
			ieee->seq_ctrl[0]++;

		/* check whether the managed packet queued greater than 5 */
		if (!ieee->check_nic_enough_desc(ieee->dev,
						 tcb_desc->queue_index) ||
		    skb_queue_len(&ieee->skb_waitq[tcb_desc->queue_index]) ||
		    ieee->queue_stop) {
			/* insert the skb packet to the management queue
			 *
			 * as for the completion function, it does not need
			 * to check it any more.
			 */
			netdev_info(ieee->dev,
			       "%s():insert to waitqueue, queue_index:%d!\n",
			       __func__, tcb_desc->queue_index);
			skb_queue_tail(&ieee->skb_waitq[tcb_desc->queue_index],
				       skb);
		} else {
			ieee->softmac_hard_start_xmit(skb, ieee->dev);
		}
		spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags);
	}
}

static inline void
softmac_ps_mgmt_xmit(struct sk_buff *skb,
		     struct rtllib_device *ieee)
{
	short single = ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE;
	struct ieee80211_hdr_3addr  *header =
		(struct ieee80211_hdr_3addr  *)skb->data;
	u16 fc, type, stype;
	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + 8);

	fc = le16_to_cpu(header->frame_control);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);

	if (stype != IEEE80211_STYPE_PSPOLL)
		tcb_desc->queue_index = MGNT_QUEUE;
	else
		tcb_desc->queue_index = HIGH_QUEUE;

	if (ieee->disable_mgnt_queue)
		tcb_desc->queue_index = HIGH_QUEUE;

	tcb_desc->data_rate = mgnt_query_mgnt_frame_tx_rate(ieee);
	tcb_desc->ratr_index = 7;
	tcb_desc->tx_dis_rate_fallback = 1;
	tcb_desc->tx_use_drv_assinged_rate = 1;
	if (single) {
		if (type != RTLLIB_FTYPE_CTL) {
			header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

			if (ieee->seq_ctrl[0] == 0xFFF)
				ieee->seq_ctrl[0] = 0;
			else
				ieee->seq_ctrl[0]++;
		}
		/* avoid watchdog triggers */
		ieee->softmac_data_hard_start_xmit(skb, ieee->dev,
						   ieee->basic_rate);

	} else {
		if (type != RTLLIB_FTYPE_CTL) {
			header->seq_ctrl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

			if (ieee->seq_ctrl[0] == 0xFFF)
				ieee->seq_ctrl[0] = 0;
			else
				ieee->seq_ctrl[0]++;
		}
		ieee->softmac_hard_start_xmit(skb, ieee->dev);
	}
}

static inline struct sk_buff *rtllib_probe_req(struct rtllib_device *ieee)
{
	unsigned int len, rate_len;
	u8 *tag;
	struct sk_buff *skb;
	struct rtllib_probe_request *req;

	len = ieee->current_network.ssid_len;

	rate_len = rtllib_MFIE_rate_len(ieee);

	skb = dev_alloc_skb(sizeof(struct rtllib_probe_request) +
			    2 + len + rate_len + ieee->tx_headroom);

	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	req = skb_put(skb, sizeof(struct rtllib_probe_request));
	req->header.frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	req->header.duration_id = 0;

	eth_broadcast_addr(req->header.addr1);
	ether_addr_copy(req->header.addr2, ieee->dev->dev_addr);
	eth_broadcast_addr(req->header.addr3);

	tag = skb_put(skb, len + 2 + rate_len);

	*tag++ = MFIE_TYPE_SSID;
	*tag++ = len;
	memcpy(tag, ieee->current_network.ssid, len);
	tag += len;

	rtllib_mfie_brate(ieee, &tag);
	rtllib_mfie_grate(ieee, &tag);

	return skb;
}

/* Enables network monitor mode, all rx packets will be received. */
void rtllib_enable_net_monitor_mode(struct net_device *dev,
		bool init_state)
{
	struct rtllib_device *ieee = netdev_priv_rsl(dev);

	netdev_info(dev, "========>Enter Monitor Mode\n");

	ieee->allow_all_dest_addr_handler(dev, true, !init_state);
}

/* Disables network monitor mode. Only packets destinated to
 * us will be received.
 */
void rtllib_disable_net_monitor_mode(struct net_device *dev, bool init_state)
{
	struct rtllib_device *ieee = netdev_priv_rsl(dev);

	netdev_info(dev, "========>Exit Monitor Mode\n");

	ieee->allow_all_dest_addr_handler(dev, false, !init_state);
}

static void rtllib_send_probe(struct rtllib_device *ieee)
{
	struct sk_buff *skb;

	skb = rtllib_probe_req(ieee);
	if (skb) {
		softmac_mgmt_xmit(skb, ieee);
		ieee->softmac_stats.tx_probe_rq++;
	}
}

static void rtllib_send_probe_requests(struct rtllib_device *ieee)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_PROBERQ) {
		rtllib_send_probe(ieee);
		rtllib_send_probe(ieee);
	}
}

/* this performs syncro scan blocking the caller until all channels
 * in the allowed channel map has been checked.
 */
static void rtllib_softmac_scan_syncro(struct rtllib_device *ieee)
{
	union iwreq_data wrqu;
	short ch = 0;

	ieee->be_scan_inprogress = true;

	mutex_lock(&ieee->scan_mutex);

	while (1) {
		do {
			ch++;
			if (ch > MAX_CHANNEL_NUMBER)
				goto out; /* scan completed */
		} while (!ieee->active_channel_map[ch]);

		/* this function can be called in two situations
		 * 1- We have switched to ad-hoc mode and we are
		 *    performing a complete syncro scan before conclude
		 *    there are no interesting cell and to create a
		 *    new one. In this case the link state is
		 *    MAC80211_NOLINK until we found an interesting cell.
		 *    If so the ieee8021_new_net, called by the RX path
		 *    will set the state to MAC80211_LINKED, so we stop
		 *    scanning
		 * 2- We are linked and the root uses run iwlist scan.
		 *    So we switch to MAC80211_LINKED_SCANNING to remember
		 *    that we are still logically linked (not interested in
		 *    new network events, despite for updating the net list,
		 *    but we are temporarily 'unlinked' as the driver shall
		 *    not filter RX frames and the channel is changing.
		 * So the only situation in which are interested is to check
		 * if the state become LINKED because of the #1 situation
		 */

		if (ieee->link_state == MAC80211_LINKED)
			goto out;
		if (ieee->sync_scan_hurryup) {
			netdev_info(ieee->dev,
				    "============>sync_scan_hurryup out\n");
			goto out;
		}

		ieee->set_chan(ieee->dev, ch);
		if (ieee->active_channel_map[ch] == 1)
			rtllib_send_probe_requests(ieee);

		/* this prevent excessive time wait when we
		 * need to wait for a syncro scan to end..
		 */
		msleep_interruptible_rsl(RTLLIB_SOFTMAC_SCAN_TIME);
	}
out:
	ieee->actscanning = false;
	ieee->sync_scan_hurryup = 0;

	mutex_unlock(&ieee->scan_mutex);

	ieee->be_scan_inprogress = false;

	memset(&wrqu, 0, sizeof(wrqu));
	wireless_send_event(ieee->dev, SIOCGIWSCAN, &wrqu, NULL);
}

static void rtllib_softmac_scan_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, softmac_scan_wq);
	u8 last_channel = ieee->current_network.channel;

	if (!ieee->ieee_up)
		return;
	if (rtllib_act_scanning(ieee, true))
		return;

	mutex_lock(&ieee->scan_mutex);

	if (ieee->rf_power_state == rf_off) {
		netdev_info(ieee->dev,
			    "======>%s():rf state is rf_off, return\n",
			    __func__);
		goto out1;
	}

	do {
		ieee->current_network.channel =
			(ieee->current_network.channel + 1) %
			MAX_CHANNEL_NUMBER;
		if (ieee->scan_watch_dog++ > MAX_CHANNEL_NUMBER) {
			if (!ieee->active_channel_map[ieee->current_network.channel])
				ieee->current_network.channel = 6;
			goto out; /* no good chans */
		}
	} while (!ieee->active_channel_map[ieee->current_network.channel]);

	if (ieee->scanning_continue == 0)
		goto out;

	ieee->set_chan(ieee->dev, ieee->current_network.channel);

	if (ieee->active_channel_map[ieee->current_network.channel] == 1)
		rtllib_send_probe_requests(ieee);

	schedule_delayed_work(&ieee->softmac_scan_wq,
			      msecs_to_jiffies(RTLLIB_SOFTMAC_SCAN_TIME));

	mutex_unlock(&ieee->scan_mutex);
	return;

out:
	ieee->current_network.channel = last_channel;

out1:
	ieee->actscanning = false;
	ieee->scan_watch_dog = 0;
	ieee->scanning_continue = 0;
	mutex_unlock(&ieee->scan_mutex);
}

static void rtllib_softmac_stop_scan(struct rtllib_device *ieee)
{
	mutex_lock(&ieee->scan_mutex);
	ieee->scan_watch_dog = 0;
	if (ieee->scanning_continue == 1) {
		ieee->scanning_continue = 0;
		ieee->actscanning = false;
		mutex_unlock(&ieee->scan_mutex);
		cancel_delayed_work_sync(&ieee->softmac_scan_wq);
	} else {
		mutex_unlock(&ieee->scan_mutex);
	}
}

void rtllib_stop_scan(struct rtllib_device *ieee)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN)
		rtllib_softmac_stop_scan(ieee);
}
EXPORT_SYMBOL(rtllib_stop_scan);

void rtllib_stop_scan_syncro(struct rtllib_device *ieee)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN)
		ieee->sync_scan_hurryup = 1;
}
EXPORT_SYMBOL(rtllib_stop_scan_syncro);

bool rtllib_act_scanning(struct rtllib_device *ieee, bool sync_scan)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN) {
		if (sync_scan)
			return ieee->be_scan_inprogress;
		else
			return ieee->actscanning || ieee->be_scan_inprogress;
	} else {
		return test_bit(STATUS_SCANNING, &ieee->status);
	}
}
EXPORT_SYMBOL(rtllib_act_scanning);

/* called with ieee->lock held */
static void rtllib_start_scan(struct rtllib_device *ieee)
{
	ieee->rtllib_ips_leave_wq(ieee->dev);

	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN) {
		if (ieee->scanning_continue == 0) {
			ieee->actscanning = true;
			ieee->scanning_continue = 1;
			schedule_delayed_work(&ieee->softmac_scan_wq, 0);
		}
	}
}

/* called with wx_mutex held */
void rtllib_start_scan_syncro(struct rtllib_device *ieee)
{
	ieee->sync_scan_hurryup = 0;
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN)
		rtllib_softmac_scan_syncro(ieee);
}
EXPORT_SYMBOL(rtllib_start_scan_syncro);

static inline struct sk_buff *
rtllib_authentication_req(struct rtllib_network *beacon,
			  struct rtllib_device *ieee,
			  int challengelen, u8 *daddr)
{
	struct sk_buff *skb;
	struct rtllib_authentication *auth;
	int  len;

	len = sizeof(struct rtllib_authentication) + challengelen +
		     ieee->tx_headroom + 4;
	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	auth = skb_put(skb, sizeof(struct rtllib_authentication));

	auth->header.frame_control = cpu_to_le16(IEEE80211_STYPE_AUTH);
	if (challengelen)
		auth->header.frame_control |= cpu_to_le16(IEEE80211_FCTL_PROTECTED);

	auth->header.duration_id = cpu_to_le16(0x013a);
	ether_addr_copy(auth->header.addr1, beacon->bssid);
	ether_addr_copy(auth->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(auth->header.addr3, beacon->bssid);
	if (ieee->auth_mode == 0)
		auth->algorithm = WLAN_AUTH_OPEN;
	else if (ieee->auth_mode == 1)
		auth->algorithm = cpu_to_le16(WLAN_AUTH_SHARED_KEY);
	else if (ieee->auth_mode == 2)
		auth->algorithm = WLAN_AUTH_OPEN;
	auth->transaction = cpu_to_le16(ieee->associate_seq);
	ieee->associate_seq++;

	auth->status = cpu_to_le16(WLAN_STATUS_SUCCESS);

	return skb;
}

static struct sk_buff *rtllib_null_func(struct rtllib_device *ieee, short pwr)
{
	struct sk_buff *skb;
	struct ieee80211_hdr_3addr *hdr;

	skb = dev_alloc_skb(sizeof(struct ieee80211_hdr_3addr) + ieee->tx_headroom);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	hdr = skb_put(skb, sizeof(struct ieee80211_hdr_3addr));

	ether_addr_copy(hdr->addr1, ieee->current_network.bssid);
	ether_addr_copy(hdr->addr2, ieee->dev->dev_addr);
	ether_addr_copy(hdr->addr3, ieee->current_network.bssid);

	hdr->frame_control = cpu_to_le16(RTLLIB_FTYPE_DATA |
		IEEE80211_STYPE_NULLFUNC | IEEE80211_FCTL_TODS |
		(pwr ? IEEE80211_FCTL_PM : 0));

	return skb;
}

static struct sk_buff *rtllib_pspoll_func(struct rtllib_device *ieee)
{
	struct sk_buff *skb;
	struct ieee80211_pspoll *hdr;

	skb = dev_alloc_skb(sizeof(struct ieee80211_pspoll) + ieee->tx_headroom);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	hdr = skb_put(skb, sizeof(struct ieee80211_pspoll));

	ether_addr_copy(hdr->bssid, ieee->current_network.bssid);
	ether_addr_copy(hdr->ta, ieee->dev->dev_addr);

	hdr->aid = cpu_to_le16(ieee->assoc_id | 0xc000);
	hdr->frame_control = cpu_to_le16(RTLLIB_FTYPE_CTL | IEEE80211_STYPE_PSPOLL |
			 IEEE80211_FCTL_PM);

	return skb;
}

static inline int sec_is_in_pmkid_list(struct rtllib_device *ieee, u8 *bssid)
{
	int i = 0;

	do {
		if ((ieee->pmkid_list[i].used) &&
		    (memcmp(ieee->pmkid_list[i].bssid, bssid, ETH_ALEN) == 0))
			break;
		i++;
	} while (i < NUM_PMKID_CACHE);

	if (i == NUM_PMKID_CACHE)
		i = -1;
	return i;
}

static inline struct sk_buff *
rtllib_association_req(struct rtllib_network *beacon,
		       struct rtllib_device *ieee)
{
	struct sk_buff *skb;
	struct rtllib_assoc_request_frame *hdr;
	u8 *tag, *ies;
	int i;
	u8 *ht_cap_buf = NULL;
	u8 ht_cap_len = 0;
	u8 *realtek_ie_buf = NULL;
	u8 realtek_ie_len = 0;
	int wpa_ie_len = ieee->wpa_ie_len;
	int wps_ie_len = ieee->wps_ie_len;
	unsigned int ckip_ie_len = 0;
	unsigned int ccxrm_ie_len = 0;
	unsigned int cxvernum_ie_len = 0;
	struct lib80211_crypt_data *crypt;
	int encrypt;
	int	pmk_cache_idx;

	unsigned int rate_len = (beacon->rates_len ?
				(beacon->rates_len + 2) : 0) +
				(beacon->rates_ex_len ? (beacon->rates_ex_len) +
				2 : 0);

	unsigned int wmm_info_len = beacon->qos_data.supported ? 9 : 0;
	unsigned int turbo_info_len = beacon->turbo_enable ? 9 : 0;

	int len = 0;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	if (crypt)
		encrypt = crypt && crypt->ops &&
			  ((strcmp(crypt->ops->name, "R-WEP") == 0 ||
			  wpa_ie_len));
	else
		encrypt = 0;

	if ((ieee->rtllib_ap_sec_type &&
	    (ieee->rtllib_ap_sec_type(ieee) & SEC_ALG_TKIP)) ||
	    ieee->forced_bg_mode) {
		ieee->ht_info->enable_ht = 0;
		ieee->mode = WIRELESS_MODE_G;
	}

	if (ieee->ht_info->current_ht_support && ieee->ht_info->enable_ht) {
		ht_cap_buf = (u8 *)&ieee->ht_info->self_ht_cap;
		ht_cap_len = sizeof(ieee->ht_info->self_ht_cap);
		ht_construct_capability_element(ieee, ht_cap_buf, &ht_cap_len,
					     encrypt, true);
		if (ieee->ht_info->current_rt2rt_aggregation) {
			realtek_ie_buf = ieee->ht_info->sz_rt2rt_agg_buf;
			realtek_ie_len =
				 sizeof(ieee->ht_info->sz_rt2rt_agg_buf);
			ht_construct_rt2rt_agg_element(ieee, realtek_ie_buf,
						   &realtek_ie_len);
		}
	}

	if (beacon->ckip_supported)
		ckip_ie_len = 30 + 2;
	if (beacon->ccx_rm_enable)
		ccxrm_ie_len = 6 + 2;
	if (beacon->bss_ccx_ver_number >= 2)
		cxvernum_ie_len = 5 + 2;

	pmk_cache_idx = sec_is_in_pmkid_list(ieee, ieee->current_network.bssid);
	if (pmk_cache_idx >= 0) {
		wpa_ie_len += 18;
		netdev_info(ieee->dev, "[PMK cache]: WPA2 IE length: %x\n",
			    wpa_ie_len);
	}
	len = sizeof(struct rtllib_assoc_request_frame) + 2
		+ beacon->ssid_len
		+ rate_len
		+ wpa_ie_len
		+ wps_ie_len
		+ wmm_info_len
		+ turbo_info_len
		+ ht_cap_len
		+ realtek_ie_len
		+ ckip_ie_len
		+ ccxrm_ie_len
		+ cxvernum_ie_len
		+ ieee->tx_headroom;

	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	hdr = skb_put(skb, sizeof(struct rtllib_assoc_request_frame) + 2);

	hdr->header.frame_control = cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ);
	hdr->header.duration_id = cpu_to_le16(37);
	ether_addr_copy(hdr->header.addr1, beacon->bssid);
	ether_addr_copy(hdr->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(hdr->header.addr3, beacon->bssid);

	ether_addr_copy(ieee->ap_mac_addr, beacon->bssid);

	hdr->capability = cpu_to_le16(WLAN_CAPABILITY_ESS);
	if (beacon->capability & WLAN_CAPABILITY_PRIVACY)
		hdr->capability |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);

	if (beacon->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		hdr->capability |= cpu_to_le16(WLAN_CAPABILITY_SHORT_PREAMBLE);

	if (beacon->capability & WLAN_CAPABILITY_SHORT_SLOT_TIME)
		hdr->capability |= cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT_TIME);

	hdr->listen_interval = cpu_to_le16(beacon->listen_interval);

	hdr->info_element[0].id = MFIE_TYPE_SSID;

	hdr->info_element[0].len = beacon->ssid_len;
	skb_put_data(skb, beacon->ssid, beacon->ssid_len);

	tag = skb_put(skb, rate_len);

	if (beacon->rates_len) {
		*tag++ = MFIE_TYPE_RATES;
		*tag++ = beacon->rates_len;
		for (i = 0; i < beacon->rates_len; i++)
			*tag++ = beacon->rates[i];
	}

	if (beacon->rates_ex_len) {
		*tag++ = MFIE_TYPE_RATES_EX;
		*tag++ = beacon->rates_ex_len;
		for (i = 0; i < beacon->rates_ex_len; i++)
			*tag++ = beacon->rates_ex[i];
	}

	if (beacon->ckip_supported) {
		static const u8 aironet_ie_oui[] = {0x00, 0x01, 0x66};
		u8	ccx_aironet_buf[30];
		struct octet_string os_ccx_aironet_ie;

		memset(ccx_aironet_buf, 0, 30);
		os_ccx_aironet_ie.octet = ccx_aironet_buf;
		os_ccx_aironet_ie.Length = sizeof(ccx_aironet_buf);
		memcpy(os_ccx_aironet_ie.octet, aironet_ie_oui,
		       sizeof(aironet_ie_oui));

		os_ccx_aironet_ie.octet[IE_CISCO_FLAG_POSITION] |=
					 (SUPPORT_CKIP_PK | SUPPORT_CKIP_MIC);
		tag = skb_put(skb, ckip_ie_len);
		*tag++ = MFIE_TYPE_AIRONET;
		*tag++ = os_ccx_aironet_ie.Length;
		memcpy(tag, os_ccx_aironet_ie.octet, os_ccx_aironet_ie.Length);
		tag += os_ccx_aironet_ie.Length;
	}

	if (beacon->ccx_rm_enable) {
		static const u8 ccx_rm_cap_buf[] = {0x00, 0x40, 0x96, 0x01, 0x01,
			0x00};
		struct octet_string os_ccx_rm_cap;

		os_ccx_rm_cap.octet = (u8 *)ccx_rm_cap_buf;
		os_ccx_rm_cap.Length = sizeof(ccx_rm_cap_buf);
		tag = skb_put(skb, ccxrm_ie_len);
		*tag++ = MFIE_TYPE_GENERIC;
		*tag++ = os_ccx_rm_cap.Length;
		memcpy(tag, os_ccx_rm_cap.octet, os_ccx_rm_cap.Length);
		tag += os_ccx_rm_cap.Length;
	}

	if (beacon->bss_ccx_ver_number >= 2) {
		u8 ccx_ver_num_buf[] = {0x00, 0x40, 0x96, 0x03, 0x00};
		struct octet_string os_ccx_ver_num;

		ccx_ver_num_buf[4] = beacon->bss_ccx_ver_number;
		os_ccx_ver_num.octet = ccx_ver_num_buf;
		os_ccx_ver_num.Length = sizeof(ccx_ver_num_buf);
		tag = skb_put(skb, cxvernum_ie_len);
		*tag++ = MFIE_TYPE_GENERIC;
		*tag++ = os_ccx_ver_num.Length;
		memcpy(tag, os_ccx_ver_num.octet, os_ccx_ver_num.Length);
		tag += os_ccx_ver_num.Length;
	}
	if (ieee->ht_info->current_ht_support && ieee->ht_info->enable_ht) {
		if (ieee->ht_info->peer_ht_spec_ver != HT_SPEC_VER_EWC) {
			tag = skb_put(skb, ht_cap_len);
			*tag++ = MFIE_TYPE_HT_CAP;
			*tag++ = ht_cap_len - 2;
			memcpy(tag, ht_cap_buf, ht_cap_len - 2);
			tag += ht_cap_len - 2;
		}
	}

	if (wpa_ie_len) {
		skb_put_data(skb, ieee->wpa_ie, ieee->wpa_ie_len);

		if (pmk_cache_idx >= 0) {
			tag = skb_put(skb, 18);
			*tag = 1;
			*(tag + 1) = 0;
			memcpy((tag + 2), &ieee->pmkid_list[pmk_cache_idx].PMKID,
			       16);
		}
	}
	if (wmm_info_len) {
		tag = skb_put(skb, wmm_info_len);
		rtllib_wmm_info(ieee, &tag);
	}

	if (wps_ie_len && ieee->wps_ie)
		skb_put_data(skb, ieee->wps_ie, wps_ie_len);

	if (turbo_info_len) {
		tag = skb_put(skb, turbo_info_len);
		rtllib_turbo_info(ieee, &tag);
	}

	if (ieee->ht_info->current_ht_support && ieee->ht_info->enable_ht) {
		if (ieee->ht_info->peer_ht_spec_ver == HT_SPEC_VER_EWC) {
			tag = skb_put(skb, ht_cap_len);
			*tag++ = MFIE_TYPE_GENERIC;
			*tag++ = ht_cap_len - 2;
			memcpy(tag, ht_cap_buf, ht_cap_len - 2);
			tag += ht_cap_len - 2;
		}

		if (ieee->ht_info->current_rt2rt_aggregation) {
			tag = skb_put(skb, realtek_ie_len);
			*tag++ = MFIE_TYPE_GENERIC;
			*tag++ = realtek_ie_len - 2;
			memcpy(tag, realtek_ie_buf, realtek_ie_len - 2);
		}
	}

	kfree(ieee->assocreq_ies);
	ieee->assocreq_ies = NULL;
	ies = &hdr->info_element[0].id;
	ieee->assocreq_ies_len = (skb->data + skb->len) - ies;
	ieee->assocreq_ies = kmemdup(ies, ieee->assocreq_ies_len, GFP_ATOMIC);
	if (!ieee->assocreq_ies)
		ieee->assocreq_ies_len = 0;

	return skb;
}

static void rtllib_associate_abort(struct rtllib_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);

	ieee->associate_seq++;

	/* don't scan, and avoid to have the RX path possibly
	 * try again to associate. Even do not react to AUTH or
	 * ASSOC response. Just wait for the retry wq to be scheduled.
	 * Here we will check if there are good nets to associate
	 * with, so we retry or just get back to NO_LINK and scanning
	 */
	if (ieee->link_state == RTLLIB_ASSOCIATING_AUTHENTICATING) {
		netdev_dbg(ieee->dev, "Authentication failed\n");
		ieee->softmac_stats.no_auth_rs++;
	} else {
		netdev_dbg(ieee->dev, "Association failed\n");
		ieee->softmac_stats.no_ass_rs++;
	}

	ieee->link_state = RTLLIB_ASSOCIATING_RETRY;

	schedule_delayed_work(&ieee->associate_retry_wq,
			      RTLLIB_SOFTMAC_ASSOC_RETRY_TIME);

	spin_unlock_irqrestore(&ieee->lock, flags);
}

static void rtllib_associate_abort_cb(struct timer_list *t)
{
	struct rtllib_device *dev = from_timer(dev, t, associate_timer);

	rtllib_associate_abort(dev);
}

static void rtllib_associate_step1(struct rtllib_device *ieee, u8 *daddr)
{
	struct rtllib_network *beacon = &ieee->current_network;
	struct sk_buff *skb;

	netdev_dbg(ieee->dev, "Stopping scan\n");

	ieee->softmac_stats.tx_auth_rq++;

	skb = rtllib_authentication_req(beacon, ieee, 0, daddr);

	if (!skb) {
		rtllib_associate_abort(ieee);
	} else {
		ieee->link_state = RTLLIB_ASSOCIATING_AUTHENTICATING;
		netdev_dbg(ieee->dev, "Sending authentication request\n");
		softmac_mgmt_xmit(skb, ieee);
		if (!timer_pending(&ieee->associate_timer)) {
			ieee->associate_timer.expires = jiffies + (HZ / 2);
			add_timer(&ieee->associate_timer);
		}
	}
}

static void rtllib_auth_challenge(struct rtllib_device *ieee, u8 *challenge,
				  int chlen)
{
	u8 *c;
	struct sk_buff *skb;
	struct rtllib_network *beacon = &ieee->current_network;

	ieee->associate_seq++;
	ieee->softmac_stats.tx_auth_rq++;

	skb = rtllib_authentication_req(beacon, ieee, chlen + 2, beacon->bssid);

	if (!skb) {
		rtllib_associate_abort(ieee);
	} else {
		c = skb_put(skb, chlen + 2);
		*(c++) = MFIE_TYPE_CHALLENGE;
		*(c++) = chlen;
		memcpy(c, challenge, chlen);

		netdev_dbg(ieee->dev,
			   "Sending authentication challenge response\n");

		rtllib_encrypt_fragment(ieee, skb,
					sizeof(struct ieee80211_hdr_3addr));

		softmac_mgmt_xmit(skb, ieee);
		mod_timer(&ieee->associate_timer, jiffies + (HZ / 2));
	}
	kfree(challenge);
}

static void rtllib_associate_step2(struct rtllib_device *ieee)
{
	struct sk_buff *skb;
	struct rtllib_network *beacon = &ieee->current_network;

	del_timer_sync(&ieee->associate_timer);

	netdev_dbg(ieee->dev, "Sending association request\n");

	ieee->softmac_stats.tx_ass_rq++;
	skb = rtllib_association_req(beacon, ieee);
	if (!skb) {
		rtllib_associate_abort(ieee);
	} else {
		softmac_mgmt_xmit(skb, ieee);
		mod_timer(&ieee->associate_timer, jiffies + (HZ / 2));
	}
}

static void rtllib_associate_complete_wq(void *data)
{
	struct rtllib_device *ieee = (struct rtllib_device *)
				     container_of(data,
				     struct rtllib_device,
				     associate_complete_wq);
	struct rt_pwr_save_ctrl *psc = &ieee->pwr_save_ctrl;

	netdev_info(ieee->dev, "Associated successfully with %pM\n",
		    ieee->current_network.bssid);
	netdev_info(ieee->dev, "normal associate\n");
	notify_wx_assoc_event(ieee);

	netif_carrier_on(ieee->dev);
	ieee->is_roaming = false;
	if (rtllib_is_54g(&ieee->current_network)) {
		ieee->rate = 108;
		netdev_info(ieee->dev, "Using G rates:%d\n", ieee->rate);
	} else {
		ieee->rate = 22;
		ieee->set_wireless_mode(ieee->dev, WIRELESS_MODE_B);
		netdev_info(ieee->dev, "Using B rates:%d\n", ieee->rate);
	}
	if (ieee->ht_info->current_ht_support && ieee->ht_info->enable_ht) {
		netdev_info(ieee->dev, "Successfully associated, ht enabled\n");
		ht_on_assoc_rsp(ieee);
	} else {
		netdev_info(ieee->dev,
			    "Successfully associated, ht not enabled(%d, %d)\n",
			    ieee->ht_info->current_ht_support,
			    ieee->ht_info->enable_ht);
		memset(ieee->dot11ht_oper_rate_set, 0, 16);
	}
	ieee->link_detect_info.slot_num = 2 * (1 +
				       ieee->current_network.beacon_interval /
				       500);
	if (ieee->link_detect_info.num_recv_bcn_in_period == 0 ||
	    ieee->link_detect_info.num_recv_data_in_period == 0) {
		ieee->link_detect_info.num_recv_bcn_in_period = 1;
		ieee->link_detect_info.num_recv_data_in_period = 1;
	}
	psc->lps_idle_count = 0;
	ieee->link_change(ieee->dev);
}

static void rtllib_sta_send_associnfo(struct rtllib_device *ieee)
{
}

static void rtllib_associate_complete(struct rtllib_device *ieee)
{
	del_timer_sync(&ieee->associate_timer);

	ieee->link_state = MAC80211_LINKED;
	rtllib_sta_send_associnfo(ieee);

	schedule_work(&ieee->associate_complete_wq);
}

static void rtllib_associate_procedure_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device,
				     associate_procedure_wq);
	rtllib_stop_scan_syncro(ieee);
	ieee->rtllib_ips_leave(ieee->dev);
	mutex_lock(&ieee->wx_mutex);

	rtllib_stop_scan(ieee);
	ht_set_connect_bw_mode(ieee, HT_CHANNEL_WIDTH_20, HT_EXTCHNL_OFFSET_NO_EXT);
	if (ieee->rf_power_state == rf_off) {
		ieee->rtllib_ips_leave_wq(ieee->dev);
		mutex_unlock(&ieee->wx_mutex);
		return;
	}
	ieee->associate_seq = 1;

	rtllib_associate_step1(ieee, ieee->current_network.bssid);

	mutex_unlock(&ieee->wx_mutex);
}

inline void rtllib_softmac_new_net(struct rtllib_device *ieee,
				   struct rtllib_network *net)
{
	u8 tmp_ssid[IW_ESSID_MAX_SIZE + 1];
	int tmp_ssid_len = 0;

	short apset, ssidset, ssidbroad, apmatch, ssidmatch;

	/* we are interested in new only if we are not associated
	 * and we are not associating / authenticating
	 */
	if (ieee->link_state != MAC80211_NOLINK)
		return;

	if ((ieee->iw_mode == IW_MODE_INFRA) && !(net->capability &
	    WLAN_CAPABILITY_ESS))
		return;

	if (ieee->iw_mode == IW_MODE_INFRA) {
		/* if the user specified the AP MAC, we need also the essid
		 * This could be obtained by beacons or, if the network does not
		 * broadcast it, it can be put manually.
		 */
		apset = ieee->wap_set;
		ssidset = ieee->ssid_set;
		ssidbroad =  !(net->ssid_len == 0 || net->ssid[0] == '\0');
		apmatch = (memcmp(ieee->current_network.bssid, net->bssid,
				  ETH_ALEN) == 0);
		if (!ssidbroad) {
			ssidmatch = (ieee->current_network.ssid_len ==
				    net->hidden_ssid_len) &&
				    (!strncmp(ieee->current_network.ssid,
				    net->hidden_ssid, net->hidden_ssid_len));
			if (net->hidden_ssid_len > 0) {
				strncpy(net->ssid, net->hidden_ssid,
					net->hidden_ssid_len);
				net->ssid_len = net->hidden_ssid_len;
				ssidbroad = 1;
			}
		} else {
			ssidmatch =
			   (ieee->current_network.ssid_len == net->ssid_len) &&
			   (!strncmp(ieee->current_network.ssid, net->ssid,
			   net->ssid_len));
		}

		/* if the user set the AP check if match.
		 * if the network does not broadcast essid we check the
		 *	 user supplied ANY essid
		 * if the network does broadcast and the user does not set
		 *	 essid it is OK
		 * if the network does broadcast and the user did set essid
		 * check if essid match
		 * if the ap is not set, check that the user set the bssid
		 * and the network does broadcast and that those two bssid match
		 */
		if ((apset && apmatch &&
		   ((ssidset && ssidbroad && ssidmatch) ||
		   (ssidbroad && !ssidset) || (!ssidbroad && ssidset))) ||
		   (!apset && ssidset && ssidbroad && ssidmatch) ||
		   (ieee->is_roaming && ssidset && ssidbroad && ssidmatch)) {
			/* Save the essid so that if it is hidden, it is
			 * replaced with the essid provided by the user.
			 */
			if (!ssidbroad) {
				memcpy(tmp_ssid, ieee->current_network.ssid,
				       ieee->current_network.ssid_len);
				tmp_ssid_len = ieee->current_network.ssid_len;
			}
			memcpy(&ieee->current_network, net,
				sizeof(ieee->current_network));
			if (!ssidbroad) {
				memcpy(ieee->current_network.ssid, tmp_ssid,
				       tmp_ssid_len);
				ieee->current_network.ssid_len = tmp_ssid_len;
			}
			netdev_info(ieee->dev,
				    "Linking with %s,channel:%d, qos:%d, myHT:%d, networkHT:%d, mode:%x cur_net.flags:0x%x\n",
				    ieee->current_network.ssid,
				    ieee->current_network.channel,
				    ieee->current_network.qos_data.supported,
				    ieee->ht_info->enable_ht,
				    ieee->current_network.bssht.bd_support_ht,
				    ieee->current_network.mode,
				    ieee->current_network.flags);

			if ((rtllib_act_scanning(ieee, false)) &&
			    !(ieee->softmac_features & IEEE_SOFTMAC_SCAN))
				rtllib_stop_scan_syncro(ieee);

			ht_reset_iot_setting(ieee->ht_info);
			ieee->wmm_acm = 0;
			if (ieee->iw_mode == IW_MODE_INFRA) {
				/* Join the network for the first time */
				ieee->asoc_retry_count = 0;
				if ((ieee->current_network.qos_data.supported == 1) &&
				    ieee->current_network.bssht.bd_support_ht)
					ht_reset_self_and_save_peer_setting(ieee,
						 &ieee->current_network);
				else
					ieee->ht_info->current_ht_support = false;

				ieee->link_state = RTLLIB_ASSOCIATING;
				schedule_delayed_work(&ieee->associate_procedure_wq, 0);
			} else {
				if (rtllib_is_54g(&ieee->current_network)) {
					ieee->rate = 108;
					ieee->set_wireless_mode(ieee->dev, WIRELESS_MODE_G);
					netdev_info(ieee->dev,
						    "Using G rates\n");
				} else {
					ieee->rate = 22;
					ieee->set_wireless_mode(ieee->dev, WIRELESS_MODE_B);
					netdev_info(ieee->dev,
						    "Using B rates\n");
				}
				memset(ieee->dot11ht_oper_rate_set, 0, 16);
				ieee->link_state = MAC80211_LINKED;
			}
		}
	}
}

static void rtllib_softmac_check_all_nets(struct rtllib_device *ieee)
{
	unsigned long flags;
	struct rtllib_network *target;

	spin_lock_irqsave(&ieee->lock, flags);

	list_for_each_entry(target, &ieee->network_list, list) {
		/* if the state become different that NOLINK means
		 * we had found what we are searching for
		 */

		if (ieee->link_state != MAC80211_NOLINK)
			break;

		if (ieee->scan_age == 0 || time_after(target->last_scanned +
		    ieee->scan_age, jiffies))
			rtllib_softmac_new_net(ieee, target);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

static inline int auth_parse(struct net_device *dev, struct sk_buff *skb,
			     u8 **challenge, int *chlen)
{
	struct rtllib_authentication *a;
	u8 *t;

	if (skb->len <  (sizeof(struct rtllib_authentication) -
	    sizeof(struct rtllib_info_element))) {
		netdev_dbg(dev, "invalid len in auth resp: %d\n", skb->len);
		return -EINVAL;
	}
	*challenge = NULL;
	a = (struct rtllib_authentication *)skb->data;
	if (skb->len > (sizeof(struct rtllib_authentication) + 3)) {
		t = skb->data + sizeof(struct rtllib_authentication);

		if (*(t++) == MFIE_TYPE_CHALLENGE) {
			*chlen = *(t++);
			*challenge = kmemdup(t, *chlen, GFP_ATOMIC);
			if (!*challenge)
				return -ENOMEM;
		}
	}

	if (a->status) {
		netdev_dbg(dev, "auth_parse() failed\n");
		return -EINVAL;
	}

	return 0;
}

static inline u16 assoc_parse(struct rtllib_device *ieee, struct sk_buff *skb,
			      int *aid)
{
	struct rtllib_assoc_response_frame *response_head;
	u16 status_code;

	if (skb->len <  sizeof(struct rtllib_assoc_response_frame)) {
		netdev_dbg(ieee->dev, "Invalid len in auth resp: %d\n",
			   skb->len);
		return 0xcafe;
	}

	response_head = (struct rtllib_assoc_response_frame *)skb->data;
	*aid = le16_to_cpu(response_head->aid) & 0x3fff;

	status_code = le16_to_cpu(response_head->status);
	if ((status_code == WLAN_STATUS_ASSOC_DENIED_RATES ||
	   status_code == WLAN_STATUS_CAPS_UNSUPPORTED) &&
	   ((ieee->mode == WIRELESS_MODE_G) &&
	   (ieee->current_network.mode == WIRELESS_MODE_N_24G) &&
	   (ieee->asoc_retry_count++ < (RT_ASOC_RETRY_LIMIT - 1)))) {
		ieee->ht_info->iot_action |= HT_IOT_ACT_PURE_N_MODE;
	} else {
		ieee->asoc_retry_count = 0;
	}

	return le16_to_cpu(response_head->status);
}

void rtllib_sta_ps_send_null_frame(struct rtllib_device *ieee, short pwr)
{
	struct sk_buff *buf = rtllib_null_func(ieee, pwr);

	if (buf)
		softmac_ps_mgmt_xmit(buf, ieee);
}
EXPORT_SYMBOL(rtllib_sta_ps_send_null_frame);

void rtllib_sta_ps_send_pspoll_frame(struct rtllib_device *ieee)
{
	struct sk_buff *buf = rtllib_pspoll_func(ieee);

	if (buf)
		softmac_ps_mgmt_xmit(buf, ieee);
}

static short rtllib_sta_ps_sleep(struct rtllib_device *ieee, u64 *time)
{
	int timeout;
	u8 dtim;
	struct rt_pwr_save_ctrl *psc = &ieee->pwr_save_ctrl;

	if (ieee->lps_delay_cnt) {
		ieee->lps_delay_cnt--;
		return 0;
	}

	dtim = ieee->current_network.dtim_data;
	if (!(dtim & RTLLIB_DTIM_VALID))
		return 0;
	timeout = ieee->current_network.beacon_interval;
	ieee->current_network.dtim_data = RTLLIB_DTIM_INVALID;
	/* there's no need to notify AP that I find you buffered
	 * with broadcast packet
	 */
	if (dtim & (RTLLIB_DTIM_UCAST & ieee->ps))
		return 2;

	if (!time_after(jiffies,
			dev_trans_start(ieee->dev) + msecs_to_jiffies(timeout)))
		return 0;
	if (!time_after(jiffies,
			ieee->last_rx_ps_time + msecs_to_jiffies(timeout)))
		return 0;
	if ((ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE) &&
	    (ieee->mgmt_queue_tail != ieee->mgmt_queue_head))
		return 0;

	if (time) {
		if (ieee->awake_pkt_sent) {
			psc->lps_awake_intvl = 1;
		} else {
			u8 max_period = 5;

			if (psc->lps_awake_intvl == 0)
				psc->lps_awake_intvl = 1;
			psc->lps_awake_intvl = (psc->lps_awake_intvl >=
					       max_period) ? max_period :
					       (psc->lps_awake_intvl + 1);
		}
		{
			u8 lps_awake_intvl_tmp = 0;
			u8 period = ieee->current_network.dtim_period;
			u8 count = ieee->current_network.tim.tim_count;

			if (count == 0) {
				if (psc->lps_awake_intvl > period)
					lps_awake_intvl_tmp = period +
						 (psc->lps_awake_intvl -
						 period) -
						 ((psc->lps_awake_intvl - period) %
						 period);
				else
					lps_awake_intvl_tmp = psc->lps_awake_intvl;

			} else {
				if (psc->lps_awake_intvl >
				    ieee->current_network.tim.tim_count)
					lps_awake_intvl_tmp = count +
					(psc->lps_awake_intvl - count) -
					((psc->lps_awake_intvl - count) % period);
				else
					lps_awake_intvl_tmp = psc->lps_awake_intvl;
			}

		*time = ieee->current_network.last_dtim_sta_time
			+ msecs_to_jiffies(ieee->current_network.beacon_interval *
			lps_awake_intvl_tmp);
	}
	}

	return 1;
}

static inline void rtllib_sta_ps(struct work_struct *work)
{
	struct rtllib_device *ieee;
	u64 time;
	short sleep;
	unsigned long flags, flags2;

	ieee = container_of(work, struct rtllib_device, ps_task);

	spin_lock_irqsave(&ieee->lock, flags);

	if ((ieee->ps == RTLLIB_PS_DISABLED ||
	     ieee->iw_mode != IW_MODE_INFRA ||
	     ieee->link_state != MAC80211_LINKED)) {
		spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);
		rtllib_sta_wakeup(ieee, 1);

		spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
	}
	sleep = rtllib_sta_ps_sleep(ieee, &time);
	/* 2 wake, 1 sleep, 0 do nothing */
	if (sleep == 0)
		goto out;
	if (sleep == 1) {
		if (ieee->sta_sleep == LPS_IS_SLEEP) {
			ieee->enter_sleep_state(ieee->dev, time);
		} else if (ieee->sta_sleep == LPS_IS_WAKE) {
			spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);

			if (ieee->ps_is_queue_empty(ieee->dev)) {
				ieee->sta_sleep = LPS_WAIT_NULL_DATA_SEND;
				ieee->ack_tx_to_ieee = 1;
				rtllib_sta_ps_send_null_frame(ieee, 1);
				ieee->ps_time = time;
			}
			spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
		}

		ieee->awake_pkt_sent = false;

	} else if (sleep == 2) {
		spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);

		rtllib_sta_wakeup(ieee, 1);

		spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
	}

out:
	spin_unlock_irqrestore(&ieee->lock, flags);
}

static void rtllib_sta_wakeup(struct rtllib_device *ieee, short nl)
{
	if (ieee->sta_sleep == LPS_IS_WAKE) {
		if (nl) {
			if (ieee->ht_info->iot_action &
			    HT_IOT_ACT_NULL_DATA_POWER_SAVING) {
				ieee->ack_tx_to_ieee = 1;
				rtllib_sta_ps_send_null_frame(ieee, 0);
			} else {
				ieee->ack_tx_to_ieee = 1;
				rtllib_sta_ps_send_pspoll_frame(ieee);
			}
		}
		return;
	}

	if (ieee->sta_sleep == LPS_IS_SLEEP)
		ieee->sta_wake_up(ieee->dev);
	if (nl) {
		if (ieee->ht_info->iot_action &
		    HT_IOT_ACT_NULL_DATA_POWER_SAVING) {
			ieee->ack_tx_to_ieee = 1;
			rtllib_sta_ps_send_null_frame(ieee, 0);
		} else {
			ieee->ack_tx_to_ieee = 1;
			ieee->polling = true;
			rtllib_sta_ps_send_pspoll_frame(ieee);
		}

	} else {
		ieee->sta_sleep = LPS_IS_WAKE;
		ieee->polling = false;
	}
}

void rtllib_ps_tx_ack(struct rtllib_device *ieee, short success)
{
	unsigned long flags, flags2;

	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->sta_sleep == LPS_WAIT_NULL_DATA_SEND) {
		/* Null frame with PS bit set */
		if (success) {
			ieee->sta_sleep = LPS_IS_SLEEP;
			ieee->enter_sleep_state(ieee->dev, ieee->ps_time);
		}
		/* if the card report not success we can't be sure the AP
		 * has not RXed so we can't assume the AP believe us awake
		 */
	} else {/* 21112005 - tx again null without PS bit if lost */

		if ((ieee->sta_sleep == LPS_IS_WAKE) && !success) {
			spin_lock_irqsave(&ieee->mgmt_tx_lock, flags2);
			if (ieee->ht_info->iot_action &
			    HT_IOT_ACT_NULL_DATA_POWER_SAVING)
				rtllib_sta_ps_send_null_frame(ieee, 0);
			else
				rtllib_sta_ps_send_pspoll_frame(ieee);
			spin_unlock_irqrestore(&ieee->mgmt_tx_lock, flags2);
		}
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}
EXPORT_SYMBOL(rtllib_ps_tx_ack);

static void rtllib_process_action(struct rtllib_device *ieee,
				  struct sk_buff *skb)
{
	u8 *act = skb->data + RTLLIB_3ADDR_LEN;
	u8 category = 0;

	category = *act;
	act++;
	switch (category) {
	case ACT_CAT_BA:
		switch (*act) {
		case ACT_ADDBAREQ:
			rtllib_rx_add_ba_req(ieee, skb);
			break;
		case ACT_ADDBARSP:
			rtllib_rx_add_ba_rsp(ieee, skb);
			break;
		case ACT_DELBA:
			rtllib_rx_DELBA(ieee, skb);
			break;
		}
		break;
	default:
		break;
	}
}

static inline int
rtllib_rx_assoc_resp(struct rtllib_device *ieee, struct sk_buff *skb,
		     struct rtllib_rx_stats *rx_stats)
{
	u16 errcode;
	int aid;
	u8 *ies;
	struct rtllib_assoc_response_frame *assoc_resp;
	struct ieee80211_hdr_3addr *header = (struct ieee80211_hdr_3addr *)skb->data;
	u16 frame_ctl = le16_to_cpu(header->frame_control);

	netdev_dbg(ieee->dev, "received [RE]ASSOCIATION RESPONSE (%d)\n",
		   WLAN_FC_GET_STYPE(frame_ctl));

	if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
	     ieee->link_state == RTLLIB_ASSOCIATING_AUTHENTICATED &&
	     (ieee->iw_mode == IW_MODE_INFRA)) {
		errcode = assoc_parse(ieee, skb, &aid);
		if (!errcode) {
			struct rtllib_network *network =
				 kzalloc(sizeof(struct rtllib_network),
				 GFP_ATOMIC);

			if (!network)
				return 1;
			ieee->link_state = MAC80211_LINKED;
			ieee->assoc_id = aid;
			ieee->softmac_stats.rx_ass_ok++;
			/* station support qos */
			/* Let the register setting default with Legacy station */
			assoc_resp = (struct rtllib_assoc_response_frame *)skb->data;
			if (ieee->current_network.qos_data.supported == 1) {
				if (rtllib_parse_info_param(ieee, assoc_resp->info_element,
							rx_stats->len - sizeof(*assoc_resp),
							network, rx_stats)) {
					kfree(network);
					return 1;
				}
				memcpy(ieee->ht_info->peer_ht_cap_buf,
				       network->bssht.bd_ht_cap_buf,
				       network->bssht.bd_ht_cap_len);
				memcpy(ieee->ht_info->peer_ht_info_buf,
				       network->bssht.bd_ht_info_buf,
				       network->bssht.bd_ht_info_len);
				ieee->handle_assoc_response(ieee->dev,
					(struct rtllib_assoc_response_frame *)header, network);
			}
			kfree(network);

			kfree(ieee->assocresp_ies);
			ieee->assocresp_ies = NULL;
			ies = &assoc_resp->info_element[0].id;
			ieee->assocresp_ies_len = (skb->data + skb->len) - ies;
			ieee->assocresp_ies = kmemdup(ies,
						      ieee->assocresp_ies_len,
						      GFP_ATOMIC);
			if (!ieee->assocresp_ies)
				ieee->assocresp_ies_len = 0;

			rtllib_associate_complete(ieee);
		} else {
			/* aid could not been allocated */
			ieee->softmac_stats.rx_ass_err++;
			netdev_info(ieee->dev,
				    "Association response status code 0x%x\n",
				    errcode);
			if (ieee->asoc_retry_count < RT_ASOC_RETRY_LIMIT)
				schedule_delayed_work(&ieee->associate_procedure_wq, 0);
			else
				rtllib_associate_abort(ieee);
		}
	}
	return 0;
}

static void rtllib_rx_auth_resp(struct rtllib_device *ieee, struct sk_buff *skb)
{
	int errcode;
	u8 *challenge;
	int chlen = 0;
	bool support_nmode = true, half_support_nmode = false;

	errcode = auth_parse(ieee->dev, skb, &challenge, &chlen);

	if (errcode) {
		ieee->softmac_stats.rx_auth_rs_err++;
		netdev_info(ieee->dev,
			    "Authentication response status code %d", errcode);
		rtllib_associate_abort(ieee);
		return;
	}

	if (ieee->open_wep || !challenge) {
		ieee->link_state = RTLLIB_ASSOCIATING_AUTHENTICATED;
		ieee->softmac_stats.rx_auth_rs_ok++;
		if (!(ieee->ht_info->iot_action & HT_IOT_ACT_PURE_N_MODE)) {
			if (!ieee->get_nmode_support_by_sec_cfg(ieee->dev)) {
				if (is_ht_half_nmode_aps(ieee)) {
					support_nmode = true;
					half_support_nmode = true;
				} else {
					support_nmode = false;
					half_support_nmode = false;
				}
			}
		}
		/* Dummy wirless mode setting to avoid encryption issue */
		if (support_nmode) {
			ieee->set_wireless_mode(ieee->dev,
					      ieee->current_network.mode);
		} else {
			/*TODO*/
			ieee->set_wireless_mode(ieee->dev, WIRELESS_MODE_G);
		}

		if ((ieee->current_network.mode == WIRELESS_MODE_N_24G) &&
		    half_support_nmode) {
			netdev_info(ieee->dev, "======>enter half N mode\n");
			ieee->half_wireless_n24g_mode = true;
		} else {
			ieee->half_wireless_n24g_mode = false;
		}
		rtllib_associate_step2(ieee);
	} else {
		rtllib_auth_challenge(ieee, challenge,  chlen);
	}
}

static inline int
rtllib_rx_auth(struct rtllib_device *ieee, struct sk_buff *skb,
	       struct rtllib_rx_stats *rx_stats)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) {
		if (ieee->link_state == RTLLIB_ASSOCIATING_AUTHENTICATING &&
		    (ieee->iw_mode == IW_MODE_INFRA)) {
			netdev_dbg(ieee->dev,
				   "Received authentication response");
			rtllib_rx_auth_resp(ieee, skb);
		}
	}
	return 0;
}

static inline int
rtllib_rx_deauth(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct ieee80211_hdr_3addr *header = (struct ieee80211_hdr_3addr *)skb->data;
	u16 frame_ctl;

	if (memcmp(header->addr3, ieee->current_network.bssid, ETH_ALEN) != 0)
		return 0;

	/* FIXME for now repeat all the association procedure
	 * both for disassociation and deauthentication
	 */
	if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
	    ieee->link_state == MAC80211_LINKED &&
	    (ieee->iw_mode == IW_MODE_INFRA)) {
		frame_ctl = le16_to_cpu(header->frame_control);
		netdev_info(ieee->dev,
			    "==========>received disassoc/deauth(%x) frame, reason code:%x\n",
			    WLAN_FC_GET_STYPE(frame_ctl),
			    ((struct rtllib_disassoc *)skb->data)->reason);
		ieee->link_state = RTLLIB_ASSOCIATING;
		ieee->softmac_stats.reassoc++;
		ieee->is_roaming = true;
		ieee->link_detect_info.busy_traffic = false;
		rtllib_disassociate(ieee);
		remove_peer_ts(ieee, header->addr2);
		if (!(ieee->rtllib_ap_sec_type(ieee) & (SEC_ALG_CCMP | SEC_ALG_TKIP)))
			schedule_delayed_work(&ieee->associate_procedure_wq, 5);
	}
	return 0;
}

inline int rtllib_rx_frame_softmac(struct rtllib_device *ieee,
				   struct sk_buff *skb,
				   struct rtllib_rx_stats *rx_stats, u16 type,
				   u16 stype)
{
	struct ieee80211_hdr_3addr *header = (struct ieee80211_hdr_3addr *)skb->data;
	u16 frame_ctl;

	if (!ieee->proto_started)
		return 0;

	frame_ctl = le16_to_cpu(header->frame_control);
	switch (WLAN_FC_GET_STYPE(frame_ctl)) {
	case IEEE80211_STYPE_ASSOC_RESP:
	case IEEE80211_STYPE_REASSOC_RESP:
		if (rtllib_rx_assoc_resp(ieee, skb, rx_stats) == 1)
			return 1;
		break;
	case IEEE80211_STYPE_ASSOC_REQ:
	case IEEE80211_STYPE_REASSOC_REQ:
		break;
	case IEEE80211_STYPE_AUTH:
		rtllib_rx_auth(ieee, skb, rx_stats);
		break;
	case IEEE80211_STYPE_DISASSOC:
	case IEEE80211_STYPE_DEAUTH:
		rtllib_rx_deauth(ieee, skb);
		break;
	case IEEE80211_STYPE_ACTION:
		rtllib_process_action(ieee, skb);
		break;
	default:
		return -1;
	}
	return 0;
}

/* following are for a simpler TX queue management.
 * Instead of using netif_[stop/wake]_queue the driver
 * will use these two functions (plus a reset one), that
 * will internally use the kernel netif_* and takes
 * care of the ieee802.11 fragmentation.
 * So the driver receives a fragment per time and might
 * call the stop function when it wants to not
 * have enough room to TX an entire packet.
 * This might be useful if each fragment needs it's own
 * descriptor, thus just keep a total free memory > than
 * the max fragmentation threshold is not enough.. If the
 * ieee802.11 stack passed a TXB struct then you need
 * to keep N free descriptors where
 * N = MAX_PACKET_SIZE / MIN_FRAG_TRESHOLD
 * In this way you need just one and the 802.11 stack
 * will take care of buffering fragments and pass them to
 * the driver later, when it wakes the queue.
 */
void rtllib_softmac_xmit(struct rtllib_txb *txb, struct rtllib_device *ieee)
{
	unsigned int queue_index = txb->queue_index;
	unsigned long flags;
	int  i;
	struct cb_desc *tcb_desc = NULL;
	unsigned long queue_len = 0;

	spin_lock_irqsave(&ieee->lock, flags);

	/* called with 2nd param 0, no tx mgmt lock required */
	rtllib_sta_wakeup(ieee, 0);

	/* update the tx status */
	tcb_desc = (struct cb_desc *)(txb->fragments[0]->cb +
		   MAX_DEV_ADDR_SIZE);
	if (tcb_desc->multicast)
		ieee->stats.multicast++;

	/* if xmit available, just xmit it immediately, else just insert it to
	 * the wait queue
	 */
	for (i = 0; i < txb->nr_frags; i++) {
		queue_len = skb_queue_len(&ieee->skb_waitq[queue_index]);
		if ((queue_len  != 0) ||
		    (!ieee->check_nic_enough_desc(ieee->dev, queue_index)) ||
		    (ieee->queue_stop)) {
			/* insert the skb packet to the wait queue
			 * as for the completion function, it does not need
			 * to check it any more.
			 */
			if (queue_len < 200)
				skb_queue_tail(&ieee->skb_waitq[queue_index],
					       txb->fragments[i]);
			else
				kfree_skb(txb->fragments[i]);
		} else {
			ieee->softmac_data_hard_start_xmit(txb->fragments[i],
					ieee->dev, ieee->rate);
		}
	}

	rtllib_txb_free(txb);

	spin_unlock_irqrestore(&ieee->lock, flags);
}

void rtllib_reset_queue(struct rtllib_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);
	init_mgmt_queue(ieee);
	if (ieee->tx_pending.txb) {
		rtllib_txb_free(ieee->tx_pending.txb);
		ieee->tx_pending.txb = NULL;
	}
	ieee->queue_stop = 0;
	spin_unlock_irqrestore(&ieee->lock, flags);
}
EXPORT_SYMBOL(rtllib_reset_queue);

void rtllib_stop_all_queues(struct rtllib_device *ieee)
{
	unsigned int i;

	for (i = 0; i < ieee->dev->num_tx_queues; i++)
		txq_trans_cond_update(netdev_get_tx_queue(ieee->dev, i));

	netif_tx_stop_all_queues(ieee->dev);
}

void rtllib_wake_all_queues(struct rtllib_device *ieee)
{
	netif_tx_wake_all_queues(ieee->dev);
}

/* this is called only in user context, with wx_mutex held */
static void rtllib_start_bss(struct rtllib_device *ieee)
{
	unsigned long flags;

	/* check if we have already found the net we
	 * are interested in (if any).
	 * if not (we are disassociated and we are not
	 * in associating / authenticating phase) start the background scanning.
	 */
	rtllib_softmac_check_all_nets(ieee);

	/* ensure no-one start an associating process (thus setting
	 * the ieee->link_state to rtllib_ASSOCIATING) while we
	 * have just checked it and we are going to enable scan.
	 * The rtllib_new_net function is always called with
	 * lock held (from both rtllib_softmac_check_all_nets and
	 * the rx path), so we cannot be in the middle of such function
	 */
	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->link_state == MAC80211_NOLINK)
		rtllib_start_scan(ieee);
	spin_unlock_irqrestore(&ieee->lock, flags);
}

static void rtllib_link_change_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, link_change_wq);
	ieee->link_change(ieee->dev);
}

/* called only in userspace context */
void rtllib_disassociate(struct rtllib_device *ieee)
{
	netif_carrier_off(ieee->dev);
	if (ieee->softmac_features & IEEE_SOFTMAC_TX_QUEUE)
		rtllib_reset_queue(ieee);

	ieee->link_state = MAC80211_NOLINK;
	ieee->is_set_key = false;
	ieee->wap_set = 0;

	schedule_delayed_work(&ieee->link_change_wq, 0);

	notify_wx_assoc_event(ieee);
}

static void rtllib_associate_retry_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, associate_retry_wq);
	unsigned long flags;

	mutex_lock(&ieee->wx_mutex);
	if (!ieee->proto_started)
		goto exit;

	if (ieee->link_state != RTLLIB_ASSOCIATING_RETRY)
		goto exit;

	/* until we do not set the state to MAC80211_NOLINK
	 * there are no possibility to have someone else trying
	 * to start an association procedure (we get here with
	 * ieee->link_state = RTLLIB_ASSOCIATING).
	 * When we set the state to MAC80211_NOLINK it is possible
	 * that the RX path run an attempt to associate, but
	 * both rtllib_softmac_check_all_nets and the
	 * RX path works with ieee->lock held so there are no
	 * problems. If we are still disassociated then start a scan.
	 * the lock here is necessary to ensure no one try to start
	 * an association procedure when we have just checked the
	 * state and we are going to start the scan.
	 */
	ieee->beinretry = true;
	ieee->link_state = MAC80211_NOLINK;

	rtllib_softmac_check_all_nets(ieee);

	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->link_state == MAC80211_NOLINK)
		rtllib_start_scan(ieee);
	spin_unlock_irqrestore(&ieee->lock, flags);

	ieee->beinretry = false;
exit:
	mutex_unlock(&ieee->wx_mutex);
}

void rtllib_softmac_stop_protocol(struct rtllib_device *ieee)
{
	rtllib_stop_scan_syncro(ieee);
	mutex_lock(&ieee->wx_mutex);
	rtllib_stop_protocol(ieee);
	mutex_unlock(&ieee->wx_mutex);
}
EXPORT_SYMBOL(rtllib_softmac_stop_protocol);

void rtllib_stop_protocol(struct rtllib_device *ieee)
{
	if (!ieee->proto_started)
		return;

	ieee->proto_started = 0;
	ieee->proto_stoppping = 1;
	ieee->rtllib_ips_leave(ieee->dev);

	del_timer_sync(&ieee->associate_timer);
	mutex_unlock(&ieee->wx_mutex);
	cancel_delayed_work_sync(&ieee->associate_retry_wq);
	mutex_lock(&ieee->wx_mutex);
	cancel_delayed_work_sync(&ieee->link_change_wq);
	rtllib_stop_scan(ieee);

	if (ieee->link_state <= RTLLIB_ASSOCIATING_AUTHENTICATED)
		ieee->link_state = MAC80211_NOLINK;

	if (ieee->link_state == MAC80211_LINKED) {
		if (ieee->iw_mode == IW_MODE_INFRA)
			send_disassociation(ieee, 1, WLAN_REASON_DEAUTH_LEAVING);
		rtllib_disassociate(ieee);
	}

	remove_all_ts(ieee);
	ieee->proto_stoppping = 0;

	kfree(ieee->assocreq_ies);
	ieee->assocreq_ies = NULL;
	ieee->assocreq_ies_len = 0;
	kfree(ieee->assocresp_ies);
	ieee->assocresp_ies = NULL;
	ieee->assocresp_ies_len = 0;
}

void rtllib_softmac_start_protocol(struct rtllib_device *ieee)
{
	mutex_lock(&ieee->wx_mutex);
	rtllib_start_protocol(ieee);
	mutex_unlock(&ieee->wx_mutex);
}
EXPORT_SYMBOL(rtllib_softmac_start_protocol);

void rtllib_start_protocol(struct rtllib_device *ieee)
{
	short ch = 0;
	int i = 0;

	if (ieee->proto_started)
		return;

	ieee->proto_started = 1;

	if (ieee->current_network.channel == 0) {
		do {
			ch++;
			if (ch > MAX_CHANNEL_NUMBER)
				return; /* no channel found */
		} while (!ieee->active_channel_map[ch]);
		ieee->current_network.channel = ch;
	}

	if (ieee->current_network.beacon_interval == 0)
		ieee->current_network.beacon_interval = 100;

	for (i = 0; i < 17; i++) {
		ieee->last_rxseq_num[i] = -1;
		ieee->last_rxfrag_num[i] = -1;
		ieee->last_packet_time[i] = 0;
	}

	ieee->wmm_acm = 0;
	/* if the user set the MAC of the ad-hoc cell and then
	 * switch to managed mode, shall we  make sure that association
	 * attempts does not fail just because the user provide the essid
	 * and the nic is still checking for the AP MAC ??
	 */
	switch (ieee->iw_mode) {
	case IW_MODE_INFRA:
		rtllib_start_bss(ieee);
		break;
	}
}

int rtllib_softmac_init(struct rtllib_device *ieee)
{
	int i;

	memset(&ieee->current_network, 0, sizeof(struct rtllib_network));

	ieee->link_state = MAC80211_NOLINK;
	for (i = 0; i < 5; i++)
		ieee->seq_ctrl[i] = 0;

	ieee->link_detect_info.slot_index = 0;
	ieee->link_detect_info.slot_num = 2;
	ieee->link_detect_info.num_recv_bcn_in_period = 0;
	ieee->link_detect_info.num_recv_data_in_period = 0;
	ieee->link_detect_info.num_tx_ok_in_period = 0;
	ieee->link_detect_info.num_rx_ok_in_period = 0;
	ieee->link_detect_info.num_rx_unicast_ok_in_period = 0;
	ieee->is_aggregate_frame = false;
	ieee->assoc_id = 0;
	ieee->queue_stop = 0;
	ieee->scanning_continue = 0;
	ieee->softmac_features = 0;
	ieee->wap_set = 0;
	ieee->ssid_set = 0;
	ieee->proto_started = 0;
	ieee->proto_stoppping = 0;
	ieee->basic_rate = RTLLIB_DEFAULT_BASIC_RATE;
	ieee->rate = 22;
	ieee->ps = RTLLIB_PS_DISABLED;
	ieee->sta_sleep = LPS_IS_WAKE;

	ieee->reg_dot11ht_oper_rate_set[0] = 0xff;
	ieee->reg_dot11ht_oper_rate_set[1] = 0xff;
	ieee->reg_dot11ht_oper_rate_set[4] = 0x01;

	ieee->reg_dot11tx_ht_oper_rate_set[0] = 0xff;
	ieee->reg_dot11tx_ht_oper_rate_set[1] = 0xff;
	ieee->reg_dot11tx_ht_oper_rate_set[4] = 0x01;

	ieee->first_ie_in_scan = false;
	ieee->actscanning = false;
	ieee->beinretry = false;
	ieee->is_set_key = false;
	init_mgmt_queue(ieee);

	ieee->tx_pending.txb = NULL;

	timer_setup(&ieee->associate_timer, rtllib_associate_abort_cb, 0);

	INIT_DELAYED_WORK(&ieee->link_change_wq, (void *)rtllib_link_change_wq);
	INIT_WORK(&ieee->associate_complete_wq, (void *)rtllib_associate_complete_wq);
	INIT_DELAYED_WORK(&ieee->associate_procedure_wq, (void *)rtllib_associate_procedure_wq);
	INIT_DELAYED_WORK(&ieee->softmac_scan_wq, (void *)rtllib_softmac_scan_wq);
	INIT_DELAYED_WORK(&ieee->associate_retry_wq, (void *)rtllib_associate_retry_wq);
	INIT_WORK(&ieee->wx_sync_scan_wq, (void *)rtllib_wx_sync_scan_wq);

	mutex_init(&ieee->wx_mutex);
	mutex_init(&ieee->scan_mutex);
	mutex_init(&ieee->ips_mutex);

	spin_lock_init(&ieee->mgmt_tx_lock);
	spin_lock_init(&ieee->beacon_lock);

	INIT_WORK(&ieee->ps_task, rtllib_sta_ps);

	return 0;
}

void rtllib_softmac_free(struct rtllib_device *ieee)
{
	del_timer_sync(&ieee->associate_timer);

	cancel_delayed_work_sync(&ieee->associate_retry_wq);
	cancel_delayed_work_sync(&ieee->associate_procedure_wq);
	cancel_delayed_work_sync(&ieee->softmac_scan_wq);
	cancel_delayed_work_sync(&ieee->hw_wakeup_wq);
	cancel_delayed_work_sync(&ieee->hw_sleep_wq);
	cancel_delayed_work_sync(&ieee->link_change_wq);
	cancel_work_sync(&ieee->associate_complete_wq);
	cancel_work_sync(&ieee->ips_leave_wq);
	cancel_work_sync(&ieee->wx_sync_scan_wq);
	cancel_work_sync(&ieee->ps_task);
}

static inline struct sk_buff *
rtllib_disauth_skb(struct rtllib_network *beacon,
		   struct rtllib_device *ieee, u16 rsn)
{
	struct sk_buff *skb;
	struct rtllib_disauth *disauth;
	int len = sizeof(struct rtllib_disauth) + ieee->tx_headroom;

	skb = dev_alloc_skb(len);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	disauth = skb_put(skb, sizeof(struct rtllib_disauth));
	disauth->header.frame_control = cpu_to_le16(IEEE80211_STYPE_DEAUTH);
	disauth->header.duration_id = 0;

	ether_addr_copy(disauth->header.addr1, beacon->bssid);
	ether_addr_copy(disauth->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(disauth->header.addr3, beacon->bssid);

	disauth->reason = cpu_to_le16(rsn);
	return skb;
}

static inline struct sk_buff *
rtllib_disassociate_skb(struct rtllib_network *beacon,
			struct rtllib_device *ieee, u16 rsn)
{
	struct sk_buff *skb;
	struct rtllib_disassoc *disass;
	int len = sizeof(struct rtllib_disassoc) + ieee->tx_headroom;

	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	disass = skb_put(skb, sizeof(struct rtllib_disassoc));
	disass->header.frame_control = cpu_to_le16(IEEE80211_STYPE_DISASSOC);
	disass->header.duration_id = 0;

	ether_addr_copy(disass->header.addr1, beacon->bssid);
	ether_addr_copy(disass->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(disass->header.addr3, beacon->bssid);

	disass->reason = cpu_to_le16(rsn);
	return skb;
}

void send_disassociation(struct rtllib_device *ieee, bool deauth, u16 rsn)
{
	struct rtllib_network *beacon = &ieee->current_network;
	struct sk_buff *skb;

	if (deauth)
		skb = rtllib_disauth_skb(beacon, ieee, rsn);
	else
		skb = rtllib_disassociate_skb(beacon, ieee, rsn);

	if (skb)
		softmac_mgmt_xmit(skb, ieee);
}

u8 rtllib_ap_sec_type(struct rtllib_device *ieee)
{
	static u8 ccmp_ie[4] = {0x00, 0x50, 0xf2, 0x04};
	static u8 ccmp_rsn_ie[4] = {0x00, 0x0f, 0xac, 0x04};
	int wpa_ie_len = ieee->wpa_ie_len;
	struct lib80211_crypt_data *crypt;
	int encrypt;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	encrypt = (ieee->current_network.capability & WLAN_CAPABILITY_PRIVACY)
		  || (crypt && crypt->ops && (strcmp(crypt->ops->name, "R-WEP") == 0));

	/* simply judge  */
	if (encrypt && (wpa_ie_len == 0)) {
		return SEC_ALG_WEP;
	} else if ((wpa_ie_len != 0)) {
		if (((ieee->wpa_ie[0] == 0xdd) &&
		    (!memcmp(&ieee->wpa_ie[14], ccmp_ie, 4))) ||
		    ((ieee->wpa_ie[0] == 0x30) &&
		    (!memcmp(&ieee->wpa_ie[10], ccmp_rsn_ie, 4))))
			return SEC_ALG_CCMP;
		else
			return SEC_ALG_TKIP;
	} else {
		return SEC_ALG_NONE;
	}
}

static void rtllib_mlme_disassociate_request(struct rtllib_device *rtllib,
					     u8 *addr, u8 rsn)
{
	u8 i;
	u8	op_mode;

	remove_peer_ts(rtllib, addr);

	if (memcmp(rtllib->current_network.bssid, addr, 6) == 0) {
		rtllib->link_state = MAC80211_NOLINK;

		for (i = 0; i < 6; i++)
			rtllib->current_network.bssid[i] = 0x22;
		op_mode = RT_OP_MODE_NO_LINK;
		rtllib->op_mode = RT_OP_MODE_NO_LINK;
		rtllib->set_hw_reg_handler(rtllib->dev, HW_VAR_MEDIA_STATUS,
					(u8 *)(&op_mode));
		rtllib_disassociate(rtllib);

		rtllib->set_hw_reg_handler(rtllib->dev, HW_VAR_BSSID,
					rtllib->current_network.bssid);
	}
}

static void rtllib_mgnt_disconnect_ap(struct rtllib_device *rtllib, u8 rsn)
{
	bool filter_out_nonassociated_bssid = false;

	filter_out_nonassociated_bssid = false;
	rtllib->set_hw_reg_handler(rtllib->dev, HW_VAR_CECHK_BSSID,
				(u8 *)(&filter_out_nonassociated_bssid));
	rtllib_mlme_disassociate_request(rtllib, rtllib->current_network.bssid,
					 rsn);

	rtllib->link_state = MAC80211_NOLINK;
}

bool rtllib_mgnt_disconnect(struct rtllib_device *rtllib, u8 rsn)
{
	if (rtllib->ps != RTLLIB_PS_DISABLED)
		rtllib->sta_wake_up(rtllib->dev);

	if (rtllib->link_state == MAC80211_LINKED) {
		if (rtllib->iw_mode == IW_MODE_INFRA)
			rtllib_mgnt_disconnect_ap(rtllib, rsn);
	}

	return true;
}
EXPORT_SYMBOL(rtllib_mgnt_disconnect);

void notify_wx_assoc_event(struct rtllib_device *ieee)
{
	union iwreq_data wrqu;

	if (ieee->cannot_notify)
		return;

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (ieee->link_state == MAC80211_LINKED) {
		memcpy(wrqu.ap_addr.sa_data, ieee->current_network.bssid,
		       ETH_ALEN);
	} else {
		netdev_info(ieee->dev, "%s(): Tell user space disconnected\n",
			    __func__);
		eth_zero_addr(wrqu.ap_addr.sa_data);
	}
	wireless_send_event(ieee->dev, SIOCGIWAP, &wrqu, NULL);
}
EXPORT_SYMBOL(notify_wx_assoc_event);
