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
 *
 * released under the GPL
 */


#include "rtllib.h"

#include <linux/random.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include "dot11d.h"

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

	if (ieee->modulation & RTLLIB_CCK_MODULATION)
		rate_len = RTLLIB_CCK_RATE_LEN + 2;

	if (ieee->modulation & RTLLIB_OFDM_MODULATION)

		rate_len += RTLLIB_OFDM_RATE_LEN + 2;

	return rate_len;
}

/* place the MFIE rate, tag to the memory (double) pointed.
 * Then it updates the pointer so that
 * it points after the new MFIE tag added.
 */
static void rtllib_MFIE_Brate(struct rtllib_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	if (ieee->modulation & RTLLIB_CCK_MODULATION) {
		*tag++ = MFIE_TYPE_RATES;
		*tag++ = 4;
		*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_1MB;
		*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_2MB;
		*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_5MB;
		*tag++ = RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_11MB;
	}

	/* We may add an option for custom rates that specific HW
	 * might support
	 */
	*tag_p = tag;
}

static void rtllib_MFIE_Grate(struct rtllib_device *ieee, u8 **tag_p)
{
	u8 *tag = *tag_p;

	if (ieee->modulation & RTLLIB_OFDM_MODULATION) {
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
	}
	/* We may add an option for custom rates that specific HW might
	 * support
	 */
	*tag_p = tag;
}

static void rtllib_WMM_Info(struct rtllib_device *ieee, u8 **tag_p)
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

static void rtllib_TURBO_Info(struct rtllib_device *ieee, u8 **tag_p)
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
	ieee->mgmt_queue_tail = ieee->mgmt_queue_head = 0;
}


u8
MgntQuery_TxRateExcludeCCKRates(struct rtllib_device *ieee)
{
	u16	i;
	u8	QueryRate = 0;
	u8	BasicRate;


	for (i = 0; i < ieee->current_network.rates_len; i++) {
		BasicRate = ieee->current_network.rates[i]&0x7F;
		if (!rtllib_is_cck_rate(BasicRate)) {
			if (QueryRate == 0) {
				QueryRate = BasicRate;
			} else {
				if (BasicRate < QueryRate)
					QueryRate = BasicRate;
			}
		}
	}

	if (QueryRate == 0) {
		QueryRate = 12;
		netdev_info(ieee->dev, "No BasicRate found!!\n");
	}
	return QueryRate;
}

static u8 MgntQuery_MgntFrameTxRate(struct rtllib_device *ieee)
{
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	u8 rate;

	if (pHTInfo->IOTAction & HT_IOT_ACT_MGNT_USE_CCK_6M)
		rate = 0x0c;
	else
		rate = ieee->basic_rate & 0x7f;

	if (rate == 0) {
		if (ieee->mode == IEEE_A ||
		   ieee->mode == IEEE_N_5G ||
		   (ieee->mode == IEEE_N_24G && !pHTInfo->bCurSuppCCK))
			rate = 0x0c;
		else
			rate = 0x02;
	}

	return rate;
}

inline void softmac_mgmt_xmit(struct sk_buff *skb, struct rtllib_device *ieee)
{
	unsigned long flags;
	short single = ieee->softmac_features & IEEE_SOFTMAC_SINGLE_QUEUE;
	struct rtllib_hdr_3addr  *header =
		(struct rtllib_hdr_3addr  *) skb->data;

	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + 8);

	spin_lock_irqsave(&ieee->lock, flags);

	/* called with 2nd param 0, no mgmt lock required */
	rtllib_sta_wakeup(ieee, 0);

	if (le16_to_cpu(header->frame_ctl) == RTLLIB_STYPE_BEACON)
		tcb_desc->queue_index = BEACON_QUEUE;
	else
		tcb_desc->queue_index = MGNT_QUEUE;

	if (ieee->disable_mgnt_queue)
		tcb_desc->queue_index = HIGH_QUEUE;

	tcb_desc->data_rate = MgntQuery_MgntFrameTxRate(ieee);
	tcb_desc->RATRIndex = 7;
	tcb_desc->bTxDisableRateFallBack = 1;
	tcb_desc->bTxUseDriverAssingedRate = 1;
	if (single) {
		if (ieee->queue_stop) {
			enqueue_mgmt(ieee, skb);
		} else {
			header->seq_ctl = cpu_to_le16(ieee->seq_ctrl[0]<<4);

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

		header->seq_ctl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

		if (ieee->seq_ctrl[0] == 0xFFF)
			ieee->seq_ctrl[0] = 0;
		else
			ieee->seq_ctrl[0]++;

		/* check whether the managed packet queued greater than 5 */
		if (!ieee->check_nic_enough_desc(ieee->dev,
						 tcb_desc->queue_index) ||
		    skb_queue_len(&ieee->skb_waitQ[tcb_desc->queue_index]) ||
		    ieee->queue_stop) {
			/* insert the skb packet to the management queue
			 *
			 * as for the completion function, it does not need
			 * to check it any more.
			 */
			netdev_info(ieee->dev,
			       "%s():insert to waitqueue, queue_index:%d!\n",
			       __func__, tcb_desc->queue_index);
			skb_queue_tail(&ieee->skb_waitQ[tcb_desc->queue_index],
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
	struct rtllib_hdr_3addr  *header =
		(struct rtllib_hdr_3addr  *) skb->data;
	u16 fc, type, stype;
	struct cb_desc *tcb_desc = (struct cb_desc *)(skb->cb + 8);

	fc = le16_to_cpu(header->frame_ctl);
	type = WLAN_FC_GET_TYPE(fc);
	stype = WLAN_FC_GET_STYPE(fc);


	if (stype != RTLLIB_STYPE_PSPOLL)
		tcb_desc->queue_index = MGNT_QUEUE;
	else
		tcb_desc->queue_index = HIGH_QUEUE;

	if (ieee->disable_mgnt_queue)
		tcb_desc->queue_index = HIGH_QUEUE;


	tcb_desc->data_rate = MgntQuery_MgntFrameTxRate(ieee);
	tcb_desc->RATRIndex = 7;
	tcb_desc->bTxDisableRateFallBack = 1;
	tcb_desc->bTxUseDriverAssingedRate = 1;
	if (single) {
		if (type != RTLLIB_FTYPE_CTL) {
			header->seq_ctl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

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
			header->seq_ctl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

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
	req->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_PROBE_REQ);
	req->header.duration_id = 0;

	eth_broadcast_addr(req->header.addr1);
	ether_addr_copy(req->header.addr2, ieee->dev->dev_addr);
	eth_broadcast_addr(req->header.addr3);

	tag = skb_put(skb, len + 2 + rate_len);

	*tag++ = MFIE_TYPE_SSID;
	*tag++ = len;
	memcpy(tag, ieee->current_network.ssid, len);
	tag += len;

	rtllib_MFIE_Brate(ieee, &tag);
	rtllib_MFIE_Grate(ieee, &tag);

	return skb;
}

static struct sk_buff *rtllib_get_beacon_(struct rtllib_device *ieee);

static void rtllib_send_beacon(struct rtllib_device *ieee)
{
	struct sk_buff *skb;

	if (!ieee->ieee_up)
		return;
	skb = rtllib_get_beacon_(ieee);

	if (skb) {
		softmac_mgmt_xmit(skb, ieee);
		ieee->softmac_stats.tx_beacons++;
	}

	if (ieee->beacon_txing && ieee->ieee_up)
		mod_timer(&ieee->beacon_timer, jiffies +
			  (msecs_to_jiffies(ieee->current_network.beacon_interval - 5)));
}


static void rtllib_send_beacon_cb(struct timer_list *t)
{
	struct rtllib_device *ieee =
		from_timer(ieee, t, beacon_timer);
	unsigned long flags;

	spin_lock_irqsave(&ieee->beacon_lock, flags);
	rtllib_send_beacon(ieee);
	spin_unlock_irqrestore(&ieee->beacon_lock, flags);
}

/* Enables network monitor mode, all rx packets will be received. */
void rtllib_EnableNetMonitorMode(struct net_device *dev,
		bool bInitState)
{
	struct rtllib_device *ieee = netdev_priv_rsl(dev);

	netdev_info(dev, "========>Enter Monitor Mode\n");

	ieee->AllowAllDestAddrHandler(dev, true, !bInitState);
}


/* Disables network monitor mode. Only packets destinated to
 * us will be received.
 */
void rtllib_DisableNetMonitorMode(struct net_device *dev,
		bool bInitState)
{
	struct rtllib_device *ieee = netdev_priv_rsl(dev);

	netdev_info(dev, "========>Exit Monitor Mode\n");

	ieee->AllowAllDestAddrHandler(dev, false, !bInitState);
}


/* Enables the specialized promiscuous mode required by Intel.
 * In this mode, Intel intends to hear traffics from/to other STAs in the
 * same BSS. Therefore we don't have to disable checking BSSID and we only need
 * to allow all dest. BUT: if we enable checking BSSID then we can't recv
 * packets from other STA.
 */
void rtllib_EnableIntelPromiscuousMode(struct net_device *dev,
		bool bInitState)
{
	bool bFilterOutNonAssociatedBSSID = false;

	struct rtllib_device *ieee = netdev_priv_rsl(dev);

	netdev_info(dev, "========>Enter Intel Promiscuous Mode\n");

	ieee->AllowAllDestAddrHandler(dev, true, !bInitState);
	ieee->SetHwRegHandler(dev, HW_VAR_CECHK_BSSID,
			     (u8 *)&bFilterOutNonAssociatedBSSID);

	ieee->bNetPromiscuousMode = true;
}
EXPORT_SYMBOL(rtllib_EnableIntelPromiscuousMode);


/* Disables the specialized promiscuous mode required by Intel.
 * See MgntEnableIntelPromiscuousMode for detail.
 */
void rtllib_DisableIntelPromiscuousMode(struct net_device *dev,
		bool bInitState)
{
	bool bFilterOutNonAssociatedBSSID = true;

	struct rtllib_device *ieee = netdev_priv_rsl(dev);

	netdev_info(dev, "========>Exit Intel Promiscuous Mode\n");

	ieee->AllowAllDestAddrHandler(dev, false, !bInitState);
	ieee->SetHwRegHandler(dev, HW_VAR_CECHK_BSSID,
			     (u8 *)&bFilterOutNonAssociatedBSSID);

	ieee->bNetPromiscuousMode = false;
}
EXPORT_SYMBOL(rtllib_DisableIntelPromiscuousMode);

static void rtllib_send_probe(struct rtllib_device *ieee, u8 is_mesh)
{
	struct sk_buff *skb;

	skb = rtllib_probe_req(ieee);
	if (skb) {
		softmac_mgmt_xmit(skb, ieee);
		ieee->softmac_stats.tx_probe_rq++;
	}
}


static void rtllib_send_probe_requests(struct rtllib_device *ieee, u8 is_mesh)
{
	if (ieee->active_scan && (ieee->softmac_features &
	    IEEE_SOFTMAC_PROBERQ)) {
		rtllib_send_probe(ieee, 0);
		rtllib_send_probe(ieee, 0);
	}
}

static void rtllib_update_active_chan_map(struct rtllib_device *ieee)
{
	memcpy(ieee->active_channel_map, GET_DOT11D_INFO(ieee)->channel_map,
	       MAX_CHANNEL_NUMBER+1);
}

/* this performs syncro scan blocking the caller until all channels
 * in the allowed channel map has been checked.
 */
static void rtllib_softmac_scan_syncro(struct rtllib_device *ieee, u8 is_mesh)
{
	union iwreq_data wrqu;
	short ch = 0;

	rtllib_update_active_chan_map(ieee);

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
		 *    RTLLIB_NOLINK until we found an interesting cell.
		 *    If so the ieee8021_new_net, called by the RX path
		 *    will set the state to RTLLIB_LINKED, so we stop
		 *    scanning
		 * 2- We are linked and the root uses run iwlist scan.
		 *    So we switch to RTLLIB_LINKED_SCANNING to remember
		 *    that we are still logically linked (not interested in
		 *    new network events, despite for updating the net list,
		 *    but we are temporarly 'unlinked' as the driver shall
		 *    not filter RX frames and the channel is changing.
		 * So the only situation in which are interested is to check
		 * if the state become LINKED because of the #1 situation
		 */

		if (ieee->state == RTLLIB_LINKED)
			goto out;
		if (ieee->sync_scan_hurryup) {
			netdev_info(ieee->dev,
				    "============>sync_scan_hurryup out\n");
			goto out;
		}

		ieee->set_chan(ieee->dev, ch);
		if (ieee->active_channel_map[ch] == 1)
			rtllib_send_probe_requests(ieee, 0);

		/* this prevent excessive time wait when we
		 * need to wait for a syncro scan to end..
		 */
		msleep_interruptible_rsl(RTLLIB_SOFTMAC_SCAN_TIME);
	}
out:
	ieee->actscanning = false;
	ieee->sync_scan_hurryup = 0;

	if (ieee->state >= RTLLIB_LINKED) {
		if (IS_DOT11D_ENABLE(ieee))
			DOT11D_ScanComplete(ieee);
	}
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

	rtllib_update_active_chan_map(ieee);

	if (!ieee->ieee_up)
		return;
	if (rtllib_act_scanning(ieee, true))
		return;

	mutex_lock(&ieee->scan_mutex);

	if (ieee->eRFPowerState == eRfOff) {
		netdev_info(ieee->dev,
			    "======>%s():rf state is eRfOff, return\n",
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
		rtllib_send_probe_requests(ieee, 0);

	schedule_delayed_work(&ieee->softmac_scan_wq,
			      msecs_to_jiffies(RTLLIB_SOFTMAC_SCAN_TIME));

	mutex_unlock(&ieee->scan_mutex);
	return;

out:
	if (IS_DOT11D_ENABLE(ieee))
		DOT11D_ScanComplete(ieee);
	ieee->current_network.channel = last_channel;

out1:
	ieee->actscanning = false;
	ieee->scan_watch_dog = 0;
	ieee->scanning_continue = 0;
	mutex_unlock(&ieee->scan_mutex);
}



static void rtllib_beacons_start(struct rtllib_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->beacon_lock, flags);

	ieee->beacon_txing = 1;
	rtllib_send_beacon(ieee);

	spin_unlock_irqrestore(&ieee->beacon_lock, flags);
}

static void rtllib_beacons_stop(struct rtllib_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->beacon_lock, flags);

	ieee->beacon_txing = 0;
	del_timer_sync(&ieee->beacon_timer);

	spin_unlock_irqrestore(&ieee->beacon_lock, flags);

}


void rtllib_stop_send_beacons(struct rtllib_device *ieee)
{
	if (ieee->stop_send_beacons)
		ieee->stop_send_beacons(ieee->dev);
	if (ieee->softmac_features & IEEE_SOFTMAC_BEACONS)
		rtllib_beacons_stop(ieee);
}
EXPORT_SYMBOL(rtllib_stop_send_beacons);


void rtllib_start_send_beacons(struct rtllib_device *ieee)
{
	if (ieee->start_send_beacons)
		ieee->start_send_beacons(ieee->dev);
	if (ieee->softmac_features & IEEE_SOFTMAC_BEACONS)
		rtllib_beacons_start(ieee);
}
EXPORT_SYMBOL(rtllib_start_send_beacons);


static void rtllib_softmac_stop_scan(struct rtllib_device *ieee)
{
	mutex_lock(&ieee->scan_mutex);
	ieee->scan_watch_dog = 0;
	if (ieee->scanning_continue == 1) {
		ieee->scanning_continue = 0;
		ieee->actscanning = false;

		cancel_delayed_work_sync(&ieee->softmac_scan_wq);
	}

	mutex_unlock(&ieee->scan_mutex);
}

void rtllib_stop_scan(struct rtllib_device *ieee)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN) {
		rtllib_softmac_stop_scan(ieee);
	} else {
		if (ieee->rtllib_stop_hw_scan)
			ieee->rtllib_stop_hw_scan(ieee->dev);
	}
}
EXPORT_SYMBOL(rtllib_stop_scan);

void rtllib_stop_scan_syncro(struct rtllib_device *ieee)
{
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN) {
		ieee->sync_scan_hurryup = 1;
	} else {
		if (ieee->rtllib_stop_hw_scan)
			ieee->rtllib_stop_hw_scan(ieee->dev);
	}
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
	RT_TRACE(COMP_DBG, "===>%s()\n", __func__);
	if (ieee->rtllib_ips_leave_wq != NULL)
		ieee->rtllib_ips_leave_wq(ieee->dev);

	if (IS_DOT11D_ENABLE(ieee)) {
		if (IS_COUNTRY_IE_VALID(ieee))
			RESET_CIE_WATCHDOG(ieee);
	}
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN) {
		if (ieee->scanning_continue == 0) {
			ieee->actscanning = true;
			ieee->scanning_continue = 1;
			schedule_delayed_work(&ieee->softmac_scan_wq, 0);
		}
	} else {
		if (ieee->rtllib_start_hw_scan)
			ieee->rtllib_start_hw_scan(ieee->dev);
	}
}

/* called with wx_mutex held */
void rtllib_start_scan_syncro(struct rtllib_device *ieee, u8 is_mesh)
{
	if (IS_DOT11D_ENABLE(ieee)) {
		if (IS_COUNTRY_IE_VALID(ieee))
			RESET_CIE_WATCHDOG(ieee);
	}
	ieee->sync_scan_hurryup = 0;
	if (ieee->softmac_features & IEEE_SOFTMAC_SCAN) {
		rtllib_softmac_scan_syncro(ieee, is_mesh);
	} else {
		if (ieee->rtllib_start_hw_scan)
			ieee->rtllib_start_hw_scan(ieee->dev);
	}
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

	auth->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_AUTH);
	if (challengelen)
		auth->header.frame_ctl |= cpu_to_le16(RTLLIB_FCTL_WEP);

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

static struct sk_buff *rtllib_probe_resp(struct rtllib_device *ieee,
					 const u8 *dest)
{
	u8 *tag;
	int beacon_size;
	struct rtllib_probe_response *beacon_buf;
	struct sk_buff *skb = NULL;
	int encrypt;
	int atim_len, erp_len;
	struct lib80211_crypt_data *crypt;

	char *ssid = ieee->current_network.ssid;
	int ssid_len = ieee->current_network.ssid_len;
	int rate_len = ieee->current_network.rates_len+2;
	int rate_ex_len = ieee->current_network.rates_ex_len;
	int wpa_ie_len = ieee->wpa_ie_len;
	u8 erpinfo_content = 0;

	u8 *tmp_ht_cap_buf = NULL;
	u8 tmp_ht_cap_len = 0;
	u8 *tmp_ht_info_buf = NULL;
	u8 tmp_ht_info_len = 0;
	struct rt_hi_throughput *pHTInfo = ieee->pHTInfo;
	u8 *tmp_generic_ie_buf = NULL;
	u8 tmp_generic_ie_len = 0;

	if (rate_ex_len > 0)
		rate_ex_len += 2;

	if (ieee->current_network.capability & WLAN_CAPABILITY_IBSS)
		atim_len = 4;
	else
		atim_len = 0;

	if ((ieee->current_network.mode == IEEE_G) ||
	   (ieee->current_network.mode == IEEE_N_24G &&
	   ieee->pHTInfo->bCurSuppCCK)) {
		erp_len = 3;
		erpinfo_content = 0;
		if (ieee->current_network.buseprotection)
			erpinfo_content |= ERP_UseProtection;
	} else
		erp_len = 0;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	encrypt = ieee->host_encrypt && crypt && crypt->ops &&
		((strcmp(crypt->ops->name, "R-WEP") == 0 || wpa_ie_len));
	if (ieee->pHTInfo->bCurrentHTSupport) {
		tmp_ht_cap_buf = (u8 *) &(ieee->pHTInfo->SelfHTCap);
		tmp_ht_cap_len = sizeof(ieee->pHTInfo->SelfHTCap);
		tmp_ht_info_buf = (u8 *) &(ieee->pHTInfo->SelfHTInfo);
		tmp_ht_info_len = sizeof(ieee->pHTInfo->SelfHTInfo);
		HTConstructCapabilityElement(ieee, tmp_ht_cap_buf,
					     &tmp_ht_cap_len, encrypt, false);
		HTConstructInfoElement(ieee, tmp_ht_info_buf, &tmp_ht_info_len,
				       encrypt);

		if (pHTInfo->bRegRT2RTAggregation) {
			tmp_generic_ie_buf = ieee->pHTInfo->szRT2RTAggBuffer;
			tmp_generic_ie_len =
				 sizeof(ieee->pHTInfo->szRT2RTAggBuffer);
			HTConstructRT2RTAggElement(ieee, tmp_generic_ie_buf,
						   &tmp_generic_ie_len);
		}
	}

	beacon_size = sizeof(struct rtllib_probe_response)+2+
		ssid_len + 3 + rate_len + rate_ex_len + atim_len + erp_len
		+ wpa_ie_len + ieee->tx_headroom;
	skb = dev_alloc_skb(beacon_size);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	beacon_buf = skb_put(skb, (beacon_size - ieee->tx_headroom));
	ether_addr_copy(beacon_buf->header.addr1, dest);
	ether_addr_copy(beacon_buf->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(beacon_buf->header.addr3, ieee->current_network.bssid);

	beacon_buf->header.duration_id = 0;
	beacon_buf->beacon_interval =
		cpu_to_le16(ieee->current_network.beacon_interval);
	beacon_buf->capability =
		cpu_to_le16(ieee->current_network.capability &
		WLAN_CAPABILITY_IBSS);
	beacon_buf->capability |=
		cpu_to_le16(ieee->current_network.capability &
		WLAN_CAPABILITY_SHORT_PREAMBLE);

	if (ieee->short_slot && (ieee->current_network.capability &
	    WLAN_CAPABILITY_SHORT_SLOT_TIME))
		beacon_buf->capability |=
			cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT_TIME);

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	if (encrypt)
		beacon_buf->capability |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);


	beacon_buf->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_PROBE_RESP);
	beacon_buf->info_element[0].id = MFIE_TYPE_SSID;
	beacon_buf->info_element[0].len = ssid_len;

	tag = (u8 *) beacon_buf->info_element[0].data;

	memcpy(tag, ssid, ssid_len);

	tag += ssid_len;

	*(tag++) = MFIE_TYPE_RATES;
	*(tag++) = rate_len-2;
	memcpy(tag, ieee->current_network.rates, rate_len-2);
	tag += rate_len-2;

	*(tag++) = MFIE_TYPE_DS_SET;
	*(tag++) = 1;
	*(tag++) = ieee->current_network.channel;

	if (atim_len) {
		u16 val16;
		*(tag++) = MFIE_TYPE_IBSS_SET;
		*(tag++) = 2;
		val16 = ieee->current_network.atim_window;
		memcpy((u8 *)tag, (u8 *)&val16, 2);
		tag += 2;
	}

	if (erp_len) {
		*(tag++) = MFIE_TYPE_ERP;
		*(tag++) = 1;
		*(tag++) = erpinfo_content;
	}
	if (rate_ex_len) {
		*(tag++) = MFIE_TYPE_RATES_EX;
		*(tag++) = rate_ex_len-2;
		memcpy(tag, ieee->current_network.rates_ex, rate_ex_len-2);
		tag += rate_ex_len-2;
	}

	if (wpa_ie_len) {
		if (ieee->iw_mode == IW_MODE_ADHOC)
			memcpy(&ieee->wpa_ie[14], &ieee->wpa_ie[8], 4);
		memcpy(tag, ieee->wpa_ie, ieee->wpa_ie_len);
		tag += ieee->wpa_ie_len;
	}
	return skb;
}

static struct sk_buff *rtllib_assoc_resp(struct rtllib_device *ieee, u8 *dest)
{
	struct sk_buff *skb;
	u8 *tag;

	struct lib80211_crypt_data *crypt;
	struct rtllib_assoc_response_frame *assoc;
	short encrypt;

	unsigned int rate_len = rtllib_MFIE_rate_len(ieee);
	int len = sizeof(struct rtllib_assoc_response_frame) + rate_len +
		  ieee->tx_headroom;

	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	assoc = skb_put(skb, sizeof(struct rtllib_assoc_response_frame));

	assoc->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_ASSOC_RESP);
	ether_addr_copy(assoc->header.addr1, dest);
	ether_addr_copy(assoc->header.addr3, ieee->dev->dev_addr);
	ether_addr_copy(assoc->header.addr2, ieee->dev->dev_addr);
	assoc->capability = cpu_to_le16(ieee->iw_mode == IW_MODE_MASTER ?
		WLAN_CAPABILITY_ESS : WLAN_CAPABILITY_IBSS);


	if (ieee->short_slot)
		assoc->capability |=
				 cpu_to_le16(WLAN_CAPABILITY_SHORT_SLOT_TIME);

	if (ieee->host_encrypt)
		crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	else
		crypt = NULL;

	encrypt = (crypt && crypt->ops);

	if (encrypt)
		assoc->capability |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);

	assoc->status = 0;
	assoc->aid = cpu_to_le16(ieee->assoc_id);
	if (ieee->assoc_id == 0x2007)
		ieee->assoc_id = 0;
	else
		ieee->assoc_id++;

	tag = skb_put(skb, rate_len);
	rtllib_MFIE_Brate(ieee, &tag);
	rtllib_MFIE_Grate(ieee, &tag);

	return skb;
}

static struct sk_buff *rtllib_auth_resp(struct rtllib_device *ieee, int status,
				 u8 *dest)
{
	struct sk_buff *skb = NULL;
	struct rtllib_authentication *auth;
	int len = ieee->tx_headroom + sizeof(struct rtllib_authentication) + 1;

	skb = dev_alloc_skb(len);
	if (!skb)
		return NULL;

	skb->len = sizeof(struct rtllib_authentication);

	skb_reserve(skb, ieee->tx_headroom);

	auth = skb_put(skb, sizeof(struct rtllib_authentication));

	auth->status = cpu_to_le16(status);
	auth->transaction = cpu_to_le16(2);
	auth->algorithm = cpu_to_le16(WLAN_AUTH_OPEN);

	ether_addr_copy(auth->header.addr3, ieee->dev->dev_addr);
	ether_addr_copy(auth->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(auth->header.addr1, dest);
	auth->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_AUTH);
	return skb;


}

static struct sk_buff *rtllib_null_func(struct rtllib_device *ieee, short pwr)
{
	struct sk_buff *skb;
	struct rtllib_hdr_3addr *hdr;

	skb = dev_alloc_skb(sizeof(struct rtllib_hdr_3addr)+ieee->tx_headroom);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	hdr = skb_put(skb, sizeof(struct rtllib_hdr_3addr));

	ether_addr_copy(hdr->addr1, ieee->current_network.bssid);
	ether_addr_copy(hdr->addr2, ieee->dev->dev_addr);
	ether_addr_copy(hdr->addr3, ieee->current_network.bssid);

	hdr->frame_ctl = cpu_to_le16(RTLLIB_FTYPE_DATA |
		RTLLIB_STYPE_NULLFUNC | RTLLIB_FCTL_TODS |
		(pwr ? RTLLIB_FCTL_PM : 0));

	return skb;


}

static struct sk_buff *rtllib_pspoll_func(struct rtllib_device *ieee)
{
	struct sk_buff *skb;
	struct rtllib_pspoll_hdr *hdr;

	skb = dev_alloc_skb(sizeof(struct rtllib_pspoll_hdr)+ieee->tx_headroom);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	hdr = skb_put(skb, sizeof(struct rtllib_pspoll_hdr));

	ether_addr_copy(hdr->bssid, ieee->current_network.bssid);
	ether_addr_copy(hdr->ta, ieee->dev->dev_addr);

	hdr->aid = cpu_to_le16(ieee->assoc_id | 0xc000);
	hdr->frame_ctl = cpu_to_le16(RTLLIB_FTYPE_CTL | RTLLIB_STYPE_PSPOLL |
			 RTLLIB_FCTL_PM);

	return skb;

}

static void rtllib_resp_to_assoc_rq(struct rtllib_device *ieee, u8 *dest)
{
	struct sk_buff *buf = rtllib_assoc_resp(ieee, dest);

	if (buf)
		softmac_mgmt_xmit(buf, ieee);
}


static void rtllib_resp_to_auth(struct rtllib_device *ieee, int s, u8 *dest)
{
	struct sk_buff *buf = rtllib_auth_resp(ieee, s, dest);

	if (buf)
		softmac_mgmt_xmit(buf, ieee);
}


static void rtllib_resp_to_probe(struct rtllib_device *ieee, u8 *dest)
{
	struct sk_buff *buf = rtllib_probe_resp(ieee, dest);

	if (buf)
		softmac_mgmt_xmit(buf, ieee);
}


static inline int SecIsInPMKIDList(struct rtllib_device *ieee, u8 *bssid)
{
	int i = 0;

	do {
		if ((ieee->PMKIDList[i].bUsed) &&
		   (memcmp(ieee->PMKIDList[i].Bssid, bssid, ETH_ALEN) == 0))
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
	int	PMKCacheIdx;

	unsigned int rate_len = (beacon->rates_len ?
				(beacon->rates_len + 2) : 0) +
				(beacon->rates_ex_len ? (beacon->rates_ex_len) +
				2 : 0);

	unsigned int wmm_info_len = beacon->qos_data.supported ? 9 : 0;
	unsigned int turbo_info_len = beacon->Turbo_Enable ? 9 : 0;

	int len = 0;

	crypt = ieee->crypt_info.crypt[ieee->crypt_info.tx_keyidx];
	if (crypt != NULL)
		encrypt = ieee->host_encrypt && crypt && crypt->ops &&
			  ((strcmp(crypt->ops->name, "R-WEP") == 0 ||
			  wpa_ie_len));
	else
		encrypt = 0;

	if ((ieee->rtllib_ap_sec_type &&
	    (ieee->rtllib_ap_sec_type(ieee) & SEC_ALG_TKIP)) ||
	    ieee->bForcedBgMode) {
		ieee->pHTInfo->bEnableHT = 0;
		ieee->mode = WIRELESS_MODE_G;
	}

	if (ieee->pHTInfo->bCurrentHTSupport && ieee->pHTInfo->bEnableHT) {
		ht_cap_buf = (u8 *)&(ieee->pHTInfo->SelfHTCap);
		ht_cap_len = sizeof(ieee->pHTInfo->SelfHTCap);
		HTConstructCapabilityElement(ieee, ht_cap_buf, &ht_cap_len,
					     encrypt, true);
		if (ieee->pHTInfo->bCurrentRT2RTAggregation) {
			realtek_ie_buf = ieee->pHTInfo->szRT2RTAggBuffer;
			realtek_ie_len =
				 sizeof(ieee->pHTInfo->szRT2RTAggBuffer);
			HTConstructRT2RTAggElement(ieee, realtek_ie_buf,
						   &realtek_ie_len);
		}
	}

	if (beacon->bCkipSupported)
		ckip_ie_len = 30+2;
	if (beacon->bCcxRmEnable)
		ccxrm_ie_len = 6+2;
	if (beacon->BssCcxVerNumber >= 2)
		cxvernum_ie_len = 5+2;

	PMKCacheIdx = SecIsInPMKIDList(ieee, ieee->current_network.bssid);
	if (PMKCacheIdx >= 0) {
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


	hdr->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_ASSOC_REQ);
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

	if (ieee->short_slot &&
	   (beacon->capability&WLAN_CAPABILITY_SHORT_SLOT_TIME))
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

	if (beacon->bCkipSupported) {
		static const u8 AironetIeOui[] = {0x00, 0x01, 0x66};
		u8	CcxAironetBuf[30];
		struct octet_string osCcxAironetIE;

		memset(CcxAironetBuf, 0, 30);
		osCcxAironetIE.Octet = CcxAironetBuf;
		osCcxAironetIE.Length = sizeof(CcxAironetBuf);
		memcpy(osCcxAironetIE.Octet, AironetIeOui,
		       sizeof(AironetIeOui));

		osCcxAironetIE.Octet[IE_CISCO_FLAG_POSITION] |=
					 (SUPPORT_CKIP_PK|SUPPORT_CKIP_MIC);
		tag = skb_put(skb, ckip_ie_len);
		*tag++ = MFIE_TYPE_AIRONET;
		*tag++ = osCcxAironetIE.Length;
		memcpy(tag, osCcxAironetIE.Octet, osCcxAironetIE.Length);
		tag += osCcxAironetIE.Length;
	}

	if (beacon->bCcxRmEnable) {
		static const u8 CcxRmCapBuf[] = {0x00, 0x40, 0x96, 0x01, 0x01,
			0x00};
		struct octet_string osCcxRmCap;

		osCcxRmCap.Octet = (u8 *) CcxRmCapBuf;
		osCcxRmCap.Length = sizeof(CcxRmCapBuf);
		tag = skb_put(skb, ccxrm_ie_len);
		*tag++ = MFIE_TYPE_GENERIC;
		*tag++ = osCcxRmCap.Length;
		memcpy(tag, osCcxRmCap.Octet, osCcxRmCap.Length);
		tag += osCcxRmCap.Length;
	}

	if (beacon->BssCcxVerNumber >= 2) {
		u8 CcxVerNumBuf[] = {0x00, 0x40, 0x96, 0x03, 0x00};
		struct octet_string osCcxVerNum;

		CcxVerNumBuf[4] = beacon->BssCcxVerNumber;
		osCcxVerNum.Octet = CcxVerNumBuf;
		osCcxVerNum.Length = sizeof(CcxVerNumBuf);
		tag = skb_put(skb, cxvernum_ie_len);
		*tag++ = MFIE_TYPE_GENERIC;
		*tag++ = osCcxVerNum.Length;
		memcpy(tag, osCcxVerNum.Octet, osCcxVerNum.Length);
		tag += osCcxVerNum.Length;
	}
	if (ieee->pHTInfo->bCurrentHTSupport && ieee->pHTInfo->bEnableHT) {
		if (ieee->pHTInfo->ePeerHTSpecVer != HT_SPEC_VER_EWC) {
			tag = skb_put(skb, ht_cap_len);
			*tag++ = MFIE_TYPE_HT_CAP;
			*tag++ = ht_cap_len - 2;
			memcpy(tag, ht_cap_buf, ht_cap_len - 2);
			tag += ht_cap_len - 2;
		}
	}

	if (wpa_ie_len) {
		skb_put_data(skb, ieee->wpa_ie, ieee->wpa_ie_len);

		if (PMKCacheIdx >= 0) {
			tag = skb_put(skb, 18);
			*tag = 1;
			*(tag + 1) = 0;
			memcpy((tag + 2), &ieee->PMKIDList[PMKCacheIdx].PMKID,
			       16);
		}
	}
	if (wmm_info_len) {
		tag = skb_put(skb, wmm_info_len);
		rtllib_WMM_Info(ieee, &tag);
	}

	if (wps_ie_len && ieee->wps_ie) {
		skb_put_data(skb, ieee->wps_ie, wps_ie_len);
	}

	if (turbo_info_len) {
		tag = skb_put(skb, turbo_info_len);
		rtllib_TURBO_Info(ieee, &tag);
	}

	if (ieee->pHTInfo->bCurrentHTSupport && ieee->pHTInfo->bEnableHT) {
		if (ieee->pHTInfo->ePeerHTSpecVer == HT_SPEC_VER_EWC) {
			tag = skb_put(skb, ht_cap_len);
			*tag++ = MFIE_TYPE_GENERIC;
			*tag++ = ht_cap_len - 2;
			memcpy(tag, ht_cap_buf, ht_cap_len - 2);
			tag += ht_cap_len - 2;
		}

		if (ieee->pHTInfo->bCurrentRT2RTAggregation) {
			tag = skb_put(skb, realtek_ie_len);
			*tag++ = MFIE_TYPE_GENERIC;
			*tag++ = realtek_ie_len - 2;
			memcpy(tag, realtek_ie_buf, realtek_ie_len - 2);
		}
	}

	kfree(ieee->assocreq_ies);
	ieee->assocreq_ies = NULL;
	ies = &(hdr->info_element[0].id);
	ieee->assocreq_ies_len = (skb->data + skb->len) - ies;
	ieee->assocreq_ies = kmalloc(ieee->assocreq_ies_len, GFP_ATOMIC);
	if (ieee->assocreq_ies)
		memcpy(ieee->assocreq_ies, ies, ieee->assocreq_ies_len);
	else {
		netdev_info(ieee->dev,
			    "%s()Warning: can't alloc memory for assocreq_ies\n",
			    __func__);
		ieee->assocreq_ies_len = 0;
	}
	return skb;
}

static void rtllib_associate_abort(struct rtllib_device *ieee)
{
	unsigned long flags;

	spin_lock_irqsave(&ieee->lock, flags);

	ieee->associate_seq++;

	/* don't scan, and avoid to have the RX path possibily
	 * try again to associate. Even do not react to AUTH or
	 * ASSOC response. Just wait for the retry wq to be scheduled.
	 * Here we will check if there are good nets to associate
	 * with, so we retry or just get back to NO_LINK and scanning
	 */
	if (ieee->state == RTLLIB_ASSOCIATING_AUTHENTICATING) {
		netdev_dbg(ieee->dev, "Authentication failed\n");
		ieee->softmac_stats.no_auth_rs++;
	} else {
		netdev_dbg(ieee->dev, "Association failed\n");
		ieee->softmac_stats.no_ass_rs++;
	}

	ieee->state = RTLLIB_ASSOCIATING_RETRY;

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

	if (!skb)
		rtllib_associate_abort(ieee);
	else {
		ieee->state = RTLLIB_ASSOCIATING_AUTHENTICATING;
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

	if (!skb)
		rtllib_associate_abort(ieee);
	else {
		c = skb_put(skb, chlen+2);
		*(c++) = MFIE_TYPE_CHALLENGE;
		*(c++) = chlen;
		memcpy(c, challenge, chlen);

		netdev_dbg(ieee->dev,
			   "Sending authentication challenge response\n");

		rtllib_encrypt_fragment(ieee, skb,
					sizeof(struct rtllib_hdr_3addr));

		softmac_mgmt_xmit(skb, ieee);
		mod_timer(&ieee->associate_timer, jiffies + (HZ/2));
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
	if (!skb)
		rtllib_associate_abort(ieee);
	else {
		softmac_mgmt_xmit(skb, ieee);
		mod_timer(&ieee->associate_timer, jiffies + (HZ/2));
	}
}

static void rtllib_associate_complete_wq(void *data)
{
	struct rtllib_device *ieee = (struct rtllib_device *)
				     container_of_work_rsl(data,
				     struct rtllib_device,
				     associate_complete_wq);
	struct rt_pwr_save_ctrl *pPSC = &(ieee->PowerSaveControl);

	netdev_info(ieee->dev, "Associated successfully with %pM\n",
		    ieee->current_network.bssid);
	if (!ieee->is_silent_reset) {
		netdev_info(ieee->dev, "normal associate\n");
		notify_wx_assoc_event(ieee);
	}

	netif_carrier_on(ieee->dev);
	ieee->is_roaming = false;
	if (rtllib_is_54g(&ieee->current_network) &&
	   (ieee->modulation & RTLLIB_OFDM_MODULATION)) {
		ieee->rate = 108;
		netdev_info(ieee->dev, "Using G rates:%d\n", ieee->rate);
	} else {
		ieee->rate = 22;
		ieee->SetWirelessMode(ieee->dev, IEEE_B);
		netdev_info(ieee->dev, "Using B rates:%d\n", ieee->rate);
	}
	if (ieee->pHTInfo->bCurrentHTSupport && ieee->pHTInfo->bEnableHT) {
		netdev_info(ieee->dev, "Successfully associated, ht enabled\n");
		HTOnAssocRsp(ieee);
	} else {
		netdev_info(ieee->dev,
			    "Successfully associated, ht not enabled(%d, %d)\n",
			    ieee->pHTInfo->bCurrentHTSupport,
			    ieee->pHTInfo->bEnableHT);
		memset(ieee->dot11HTOperationalRateSet, 0, 16);
	}
	ieee->LinkDetectInfo.SlotNum = 2 * (1 +
				       ieee->current_network.beacon_interval /
				       500);
	if (ieee->LinkDetectInfo.NumRecvBcnInPeriod == 0 ||
	    ieee->LinkDetectInfo.NumRecvDataInPeriod == 0) {
		ieee->LinkDetectInfo.NumRecvBcnInPeriod = 1;
		ieee->LinkDetectInfo.NumRecvDataInPeriod = 1;
	}
	pPSC->LpsIdleCount = 0;
	ieee->link_change(ieee->dev);

	if (ieee->is_silent_reset) {
		netdev_info(ieee->dev, "silent reset associate\n");
		ieee->is_silent_reset = false;
	}

	if (ieee->data_hard_resume)
		ieee->data_hard_resume(ieee->dev);

}

static void rtllib_sta_send_associnfo(struct rtllib_device *ieee)
{
}

static void rtllib_associate_complete(struct rtllib_device *ieee)
{
	del_timer_sync(&ieee->associate_timer);

	ieee->state = RTLLIB_LINKED;
	rtllib_sta_send_associnfo(ieee);

	schedule_work(&ieee->associate_complete_wq);
}

static void rtllib_associate_procedure_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device,
				     associate_procedure_wq);
	rtllib_stop_scan_syncro(ieee);
	if (ieee->rtllib_ips_leave != NULL)
		ieee->rtllib_ips_leave(ieee->dev);
	mutex_lock(&ieee->wx_mutex);

	if (ieee->data_hard_stop)
		ieee->data_hard_stop(ieee->dev);

	rtllib_stop_scan(ieee);
	RT_TRACE(COMP_DBG, "===>%s(), chan:%d\n", __func__,
		 ieee->current_network.channel);
	HTSetConnectBwMode(ieee, HT_CHANNEL_WIDTH_20, HT_EXTCHNL_OFFSET_NO_EXT);
	if (ieee->eRFPowerState == eRfOff) {
		RT_TRACE(COMP_DBG,
			 "=============>%s():Rf state is eRfOff, schedule ipsleave wq again,return\n",
			 __func__);
		if (ieee->rtllib_ips_leave_wq != NULL)
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

	/* we are interested in new new only if we are not associated
	 * and we are not associating / authenticating
	 */
	if (ieee->state != RTLLIB_NOLINK)
		return;

	if ((ieee->iw_mode == IW_MODE_INFRA) && !(net->capability &
	    WLAN_CAPABILITY_ESS))
		return;

	if ((ieee->iw_mode == IW_MODE_ADHOC) && !(net->capability &
	     WLAN_CAPABILITY_IBSS))
		return;

	if ((ieee->iw_mode == IW_MODE_ADHOC) &&
	    (net->channel > ieee->ibss_maxjoin_chal))
		return;
	if (ieee->iw_mode == IW_MODE_INFRA || ieee->iw_mode == IW_MODE_ADHOC) {
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
		} else
			ssidmatch =
			   (ieee->current_network.ssid_len == net->ssid_len) &&
			   (!strncmp(ieee->current_network.ssid, net->ssid,
			   net->ssid_len));

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
				    ieee->pHTInfo->bEnableHT,
				    ieee->current_network.bssht.bdSupportHT,
				    ieee->current_network.mode,
				    ieee->current_network.flags);

			if ((rtllib_act_scanning(ieee, false)) &&
			   !(ieee->softmac_features & IEEE_SOFTMAC_SCAN))
				rtllib_stop_scan_syncro(ieee);

			HTResetIOTSetting(ieee->pHTInfo);
			ieee->wmm_acm = 0;
			if (ieee->iw_mode == IW_MODE_INFRA) {
				/* Join the network for the first time */
				ieee->AsocRetryCount = 0;
				if ((ieee->current_network.qos_data.supported == 1) &&
				    ieee->current_network.bssht.bdSupportHT)
					HTResetSelfAndSavePeerSetting(ieee,
						 &(ieee->current_network));
				else
					ieee->pHTInfo->bCurrentHTSupport =
								 false;

				ieee->state = RTLLIB_ASSOCIATING;
				if (ieee->LedControlHandler != NULL)
					ieee->LedControlHandler(ieee->dev,
							 LED_CTL_START_TO_LINK);
				schedule_delayed_work(
					   &ieee->associate_procedure_wq, 0);
			} else {
				if (rtllib_is_54g(&ieee->current_network) &&
				    (ieee->modulation &
				     RTLLIB_OFDM_MODULATION)) {
					ieee->rate = 108;
					ieee->SetWirelessMode(ieee->dev,
							      IEEE_G);
					netdev_info(ieee->dev,
						    "Using G rates\n");
				} else {
					ieee->rate = 22;
					ieee->SetWirelessMode(ieee->dev,
							      IEEE_B);
					netdev_info(ieee->dev,
						    "Using B rates\n");
				}
				memset(ieee->dot11HTOperationalRateSet, 0, 16);
				ieee->state = RTLLIB_LINKED;
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

		if (ieee->state != RTLLIB_NOLINK)
			break;

		if (ieee->scan_age == 0 || time_after(target->last_scanned +
		    ieee->scan_age, jiffies))
			rtllib_softmac_new_net(ieee, target);
	}
	spin_unlock_irqrestore(&ieee->lock, flags);
}

static inline u16 auth_parse(struct net_device *dev, struct sk_buff *skb,
			     u8 **challenge, int *chlen)
{
	struct rtllib_authentication *a;
	u8 *t;

	if (skb->len <  (sizeof(struct rtllib_authentication) -
	    sizeof(struct rtllib_info_element))) {
		netdev_dbg(dev, "invalid len in auth resp: %d\n", skb->len);
		return 0xcafe;
	}
	*challenge = NULL;
	a = (struct rtllib_authentication *) skb->data;
	if (skb->len > (sizeof(struct rtllib_authentication) + 3)) {
		t = skb->data + sizeof(struct rtllib_authentication);

		if (*(t++) == MFIE_TYPE_CHALLENGE) {
			*chlen = *(t++);
			*challenge = kmemdup(t, *chlen, GFP_ATOMIC);
			if (!*challenge)
				return -ENOMEM;
		}
	}
	return le16_to_cpu(a->status);
}

static int auth_rq_parse(struct net_device *dev, struct sk_buff *skb, u8 *dest)
{
	struct rtllib_authentication *a;

	if (skb->len <  (sizeof(struct rtllib_authentication) -
	    sizeof(struct rtllib_info_element))) {
		netdev_dbg(dev, "invalid len in auth request: %d\n", skb->len);
		return -1;
	}
	a = (struct rtllib_authentication *) skb->data;

	ether_addr_copy(dest, a->header.addr2);

	if (le16_to_cpu(a->algorithm) != WLAN_AUTH_OPEN)
		return  WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG;

	return WLAN_STATUS_SUCCESS;
}

static short probe_rq_parse(struct rtllib_device *ieee, struct sk_buff *skb,
			    u8 *src)
{
	u8 *tag;
	u8 *skbend;
	u8 *ssid = NULL;
	u8 ssidlen = 0;
	struct rtllib_hdr_3addr   *header =
		(struct rtllib_hdr_3addr   *) skb->data;
	bool bssid_match;

	if (skb->len < sizeof(struct rtllib_hdr_3addr))
		return -1; /* corrupted */

	bssid_match =
	  (!ether_addr_equal(header->addr3, ieee->current_network.bssid)) &&
	  (!is_broadcast_ether_addr(header->addr3));
	if (bssid_match)
		return -1;

	ether_addr_copy(src, header->addr2);

	skbend = (u8 *)skb->data + skb->len;

	tag = skb->data + sizeof(struct rtllib_hdr_3addr);

	while (tag + 1 < skbend) {
		if (*tag == 0) {
			ssid = tag + 2;
			ssidlen = *(tag + 1);
			break;
		}
		tag++; /* point to the len field */
		tag = tag + *(tag); /* point to the last data byte of the tag */
		tag++; /* point to the next tag */
	}

	if (ssidlen == 0)
		return 1;

	if (!ssid)
		return 1; /* ssid not found in tagged param */

	return !strncmp(ssid, ieee->current_network.ssid, ssidlen);
}

static int assoc_rq_parse(struct net_device *dev, struct sk_buff *skb, u8 *dest)
{
	struct rtllib_assoc_request_frame *a;

	if (skb->len < (sizeof(struct rtllib_assoc_request_frame) -
		sizeof(struct rtllib_info_element))) {
		netdev_dbg(dev, "invalid len in auth request:%d\n", skb->len);
		return -1;
	}

	a = (struct rtllib_assoc_request_frame *) skb->data;

	ether_addr_copy(dest, a->header.addr2);

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

	response_head = (struct rtllib_assoc_response_frame *) skb->data;
	*aid = le16_to_cpu(response_head->aid) & 0x3fff;

	status_code = le16_to_cpu(response_head->status);
	if ((status_code == WLAN_STATUS_ASSOC_DENIED_RATES ||
	   status_code == WLAN_STATUS_CAPS_UNSUPPORTED) &&
	   ((ieee->mode == IEEE_G) &&
	   (ieee->current_network.mode == IEEE_N_24G) &&
	   (ieee->AsocRetryCount++ < (RT_ASOC_RETRY_LIMIT-1)))) {
		ieee->pHTInfo->IOTAction |= HT_IOT_ACT_PURE_N_MODE;
	} else {
		ieee->AsocRetryCount = 0;
	}

	return le16_to_cpu(response_head->status);
}

void rtllib_rx_probe_rq(struct rtllib_device *ieee, struct sk_buff *skb)
{
	u8 dest[ETH_ALEN];

	ieee->softmac_stats.rx_probe_rq++;
	if (probe_rq_parse(ieee, skb, dest) > 0) {
		ieee->softmac_stats.tx_probe_rs++;
		rtllib_resp_to_probe(ieee, dest);
	}
}

static inline void rtllib_rx_auth_rq(struct rtllib_device *ieee,
				     struct sk_buff *skb)
{
	u8 dest[ETH_ALEN];
	int status;

	ieee->softmac_stats.rx_auth_rq++;

	status = auth_rq_parse(ieee->dev, skb, dest);
	if (status != -1)
		rtllib_resp_to_auth(ieee, status, dest);
}

static inline void rtllib_rx_assoc_rq(struct rtllib_device *ieee,
				      struct sk_buff *skb)
{
	u8 dest[ETH_ALEN];


	ieee->softmac_stats.rx_ass_rq++;
	if (assoc_rq_parse(ieee->dev, skb, dest) != -1)
		rtllib_resp_to_assoc_rq(ieee, dest);

	netdev_info(ieee->dev, "New client associated: %pM\n", dest);
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
	struct rt_pwr_save_ctrl *pPSC = &(ieee->PowerSaveControl);

	if (ieee->LPSDelayCnt) {
		ieee->LPSDelayCnt--;
		return 0;
	}

	dtim = ieee->current_network.dtim_data;
	if (!(dtim & RTLLIB_DTIM_VALID))
		return 0;
	timeout = ieee->current_network.beacon_interval;
	ieee->current_network.dtim_data = RTLLIB_DTIM_INVALID;
	/* there's no need to nofity AP that I find you buffered
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
		if (ieee->bAwakePktSent) {
			pPSC->LPSAwakeIntvl = 1;
		} else {
			u8 MaxPeriod = 1;

			if (pPSC->LPSAwakeIntvl == 0)
				pPSC->LPSAwakeIntvl = 1;
			if (pPSC->RegMaxLPSAwakeIntvl == 0)
				MaxPeriod = 1;
			else if (pPSC->RegMaxLPSAwakeIntvl == 0xFF)
				MaxPeriod = ieee->current_network.dtim_period;
			else
				MaxPeriod = pPSC->RegMaxLPSAwakeIntvl;
			pPSC->LPSAwakeIntvl = (pPSC->LPSAwakeIntvl >=
					       MaxPeriod) ? MaxPeriod :
					       (pPSC->LPSAwakeIntvl + 1);
		}
		{
			u8 LPSAwakeIntvl_tmp = 0;
			u8 period = ieee->current_network.dtim_period;
			u8 count = ieee->current_network.tim.tim_count;

			if (count == 0) {
				if (pPSC->LPSAwakeIntvl > period)
					LPSAwakeIntvl_tmp = period +
						 (pPSC->LPSAwakeIntvl -
						 period) -
						 ((pPSC->LPSAwakeIntvl-period) %
						 period);
				else
					LPSAwakeIntvl_tmp = pPSC->LPSAwakeIntvl;

			} else {
				if (pPSC->LPSAwakeIntvl >
				    ieee->current_network.tim.tim_count)
					LPSAwakeIntvl_tmp = count +
					(pPSC->LPSAwakeIntvl - count) -
					((pPSC->LPSAwakeIntvl-count)%period);
				else
					LPSAwakeIntvl_tmp = pPSC->LPSAwakeIntvl;
			}

		*time = ieee->current_network.last_dtim_sta_time
			+ msecs_to_jiffies(ieee->current_network.beacon_interval *
			LPSAwakeIntvl_tmp);
	}
	}

	return 1;


}

static inline void rtllib_sta_ps(struct rtllib_device *ieee)
{
	u64 time;
	short sleep;
	unsigned long flags, flags2;

	spin_lock_irqsave(&ieee->lock, flags);

	if ((ieee->ps == RTLLIB_PS_DISABLED ||
	     ieee->iw_mode != IW_MODE_INFRA ||
	     ieee->state != RTLLIB_LINKED)) {
		RT_TRACE(COMP_DBG,
			 "=====>%s(): no need to ps,wake up!! ieee->ps is %d, ieee->iw_mode is %d, ieee->state is %d\n",
			 __func__, ieee->ps, ieee->iw_mode, ieee->state);
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

		ieee->bAwakePktSent = false;

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
			if (ieee->pHTInfo->IOTAction &
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
		if (ieee->pHTInfo->IOTAction &
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
			if (ieee->pHTInfo->IOTAction &
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
	struct rtllib_hdr_3addr *header = (struct rtllib_hdr_3addr *) skb->data;
	u8 *act = rtllib_get_payload((struct rtllib_hdr *)header);
	u8 category = 0;

	if (act == NULL) {
		netdev_warn(ieee->dev,
			    "Error getting payload of action frame\n");
		return;
	}

	category = *act;
	act++;
	switch (category) {
	case ACT_CAT_BA:
		switch (*act) {
		case ACT_ADDBAREQ:
			rtllib_rx_ADDBAReq(ieee, skb);
			break;
		case ACT_ADDBARSP:
			rtllib_rx_ADDBARsp(ieee, skb);
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
	struct rtllib_hdr_3addr *header = (struct rtllib_hdr_3addr *) skb->data;
	u16 frame_ctl = le16_to_cpu(header->frame_ctl);

	netdev_dbg(ieee->dev, "received [RE]ASSOCIATION RESPONSE (%d)\n",
		   WLAN_FC_GET_STYPE(frame_ctl));

	if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
	     ieee->state == RTLLIB_ASSOCIATING_AUTHENTICATED &&
	     (ieee->iw_mode == IW_MODE_INFRA)) {
		errcode = assoc_parse(ieee, skb, &aid);
		if (!errcode) {
			struct rtllib_network *network =
				 kzalloc(sizeof(struct rtllib_network),
				 GFP_ATOMIC);

			if (!network)
				return 1;
			ieee->state = RTLLIB_LINKED;
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
				memcpy(ieee->pHTInfo->PeerHTCapBuf,
				       network->bssht.bdHTCapBuf,
				       network->bssht.bdHTCapLen);
				memcpy(ieee->pHTInfo->PeerHTInfoBuf,
				       network->bssht.bdHTInfoBuf,
				       network->bssht.bdHTInfoLen);
				if (ieee->handle_assoc_response != NULL)
					ieee->handle_assoc_response(ieee->dev,
						 (struct rtllib_assoc_response_frame *)header,
						 network);
			}
			kfree(network);

			kfree(ieee->assocresp_ies);
			ieee->assocresp_ies = NULL;
			ies = &(assoc_resp->info_element[0].id);
			ieee->assocresp_ies_len = (skb->data + skb->len) - ies;
			ieee->assocresp_ies = kmalloc(ieee->assocresp_ies_len,
						      GFP_ATOMIC);
			if (ieee->assocresp_ies)
				memcpy(ieee->assocresp_ies, ies,
				       ieee->assocresp_ies_len);
			else {
				netdev_info(ieee->dev,
					    "%s()Warning: can't alloc memory for assocresp_ies\n",
					    __func__);
				ieee->assocresp_ies_len = 0;
			}
			rtllib_associate_complete(ieee);
		} else {
			/* aid could not been allocated */
			ieee->softmac_stats.rx_ass_err++;
			netdev_info(ieee->dev,
				    "Association response status code 0x%x\n",
				    errcode);
			if (ieee->AsocRetryCount < RT_ASOC_RETRY_LIMIT)
				schedule_delayed_work(
					 &ieee->associate_procedure_wq, 0);
			else
				rtllib_associate_abort(ieee);
		}
	}
	return 0;
}

static void rtllib_rx_auth_resp(struct rtllib_device *ieee, struct sk_buff *skb)
{
	u16 errcode;
	u8 *challenge;
	int chlen = 0;
	bool bSupportNmode = true, bHalfSupportNmode = false;

	errcode = auth_parse(ieee->dev, skb, &challenge, &chlen);

	if (errcode) {
		ieee->softmac_stats.rx_auth_rs_err++;
		netdev_info(ieee->dev,
			    "Authentication response status code 0x%x",
			    errcode);
		rtllib_associate_abort(ieee);
		return;
	}

	if (ieee->open_wep || !challenge) {
		ieee->state = RTLLIB_ASSOCIATING_AUTHENTICATED;
		ieee->softmac_stats.rx_auth_rs_ok++;
		if (!(ieee->pHTInfo->IOTAction & HT_IOT_ACT_PURE_N_MODE)) {
			if (!ieee->GetNmodeSupportBySecCfg(ieee->dev)) {
				if (IsHTHalfNmodeAPs(ieee)) {
					bSupportNmode = true;
					bHalfSupportNmode = true;
				} else {
					bSupportNmode = false;
					bHalfSupportNmode = false;
				}
			}
		}
		/* Dummy wirless mode setting to avoid encryption issue */
		if (bSupportNmode) {
			ieee->SetWirelessMode(ieee->dev,
					      ieee->current_network.mode);
		} else {
			/*TODO*/
			ieee->SetWirelessMode(ieee->dev, IEEE_G);
		}

		if ((ieee->current_network.mode == IEEE_N_24G) &&
		    bHalfSupportNmode) {
			netdev_info(ieee->dev, "======>enter half N mode\n");
			ieee->bHalfWirelessN24GMode = true;
		} else {
			ieee->bHalfWirelessN24GMode = false;
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
		if (ieee->state == RTLLIB_ASSOCIATING_AUTHENTICATING &&
		    (ieee->iw_mode == IW_MODE_INFRA)) {
			netdev_dbg(ieee->dev,
				   "Received authentication response");
			rtllib_rx_auth_resp(ieee, skb);
		} else if (ieee->iw_mode == IW_MODE_MASTER) {
			rtllib_rx_auth_rq(ieee, skb);
		}
	}
	return 0;
}

static inline int
rtllib_rx_deauth(struct rtllib_device *ieee, struct sk_buff *skb)
{
	struct rtllib_hdr_3addr *header = (struct rtllib_hdr_3addr *) skb->data;
	u16 frame_ctl;

	if (memcmp(header->addr3, ieee->current_network.bssid, ETH_ALEN) != 0)
		return 0;

	/* FIXME for now repeat all the association procedure
	 * both for disassociation and deauthentication
	 */
	if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
	    ieee->state == RTLLIB_LINKED &&
	    (ieee->iw_mode == IW_MODE_INFRA)) {
		frame_ctl = le16_to_cpu(header->frame_ctl);
		netdev_info(ieee->dev,
			    "==========>received disassoc/deauth(%x) frame, reason code:%x\n",
			    WLAN_FC_GET_STYPE(frame_ctl),
			    ((struct rtllib_disassoc *)skb->data)->reason);
		ieee->state = RTLLIB_ASSOCIATING;
		ieee->softmac_stats.reassoc++;
		ieee->is_roaming = true;
		ieee->LinkDetectInfo.bBusyTraffic = false;
		rtllib_disassociate(ieee);
		RemovePeerTS(ieee, header->addr2);
		if (ieee->LedControlHandler != NULL)
			ieee->LedControlHandler(ieee->dev,
						LED_CTL_START_TO_LINK);

		if (!(ieee->rtllib_ap_sec_type(ieee) &
		    (SEC_ALG_CCMP|SEC_ALG_TKIP)))
			schedule_delayed_work(
				       &ieee->associate_procedure_wq, 5);
	}
	return 0;
}

inline int rtllib_rx_frame_softmac(struct rtllib_device *ieee,
				   struct sk_buff *skb,
				   struct rtllib_rx_stats *rx_stats, u16 type,
				   u16 stype)
{
	struct rtllib_hdr_3addr *header = (struct rtllib_hdr_3addr *) skb->data;
	u16 frame_ctl;

	if (!ieee->proto_started)
		return 0;

	frame_ctl = le16_to_cpu(header->frame_ctl);
	switch (WLAN_FC_GET_STYPE(frame_ctl)) {
	case RTLLIB_STYPE_ASSOC_RESP:
	case RTLLIB_STYPE_REASSOC_RESP:
		if (rtllib_rx_assoc_resp(ieee, skb, rx_stats) == 1)
			return 1;
		break;
	case RTLLIB_STYPE_ASSOC_REQ:
	case RTLLIB_STYPE_REASSOC_REQ:
		if ((ieee->softmac_features & IEEE_SOFTMAC_ASSOCIATE) &&
		     ieee->iw_mode == IW_MODE_MASTER)
			rtllib_rx_assoc_rq(ieee, skb);
		break;
	case RTLLIB_STYPE_AUTH:
		rtllib_rx_auth(ieee, skb, rx_stats);
		break;
	case RTLLIB_STYPE_DISASSOC:
	case RTLLIB_STYPE_DEAUTH:
		rtllib_rx_deauth(ieee, skb);
		break;
	case RTLLIB_STYPE_MANAGE_ACT:
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
 * to the driver later, when it wakes the queue.
 */
void rtllib_softmac_xmit(struct rtllib_txb *txb, struct rtllib_device *ieee)
{

	unsigned int queue_index = txb->queue_index;
	unsigned long flags;
	int  i;
	struct cb_desc *tcb_desc = NULL;
	unsigned long queue_len = 0;

	spin_lock_irqsave(&ieee->lock, flags);

	/* called with 2nd parm 0, no tx mgmt lock required */
	rtllib_sta_wakeup(ieee, 0);

	/* update the tx status */
	tcb_desc = (struct cb_desc *)(txb->fragments[0]->cb +
		   MAX_DEV_ADDR_SIZE);
	if (tcb_desc->bMulticast)
		ieee->stats.multicast++;

	/* if xmit available, just xmit it immediately, else just insert it to
	 * the wait queue
	 */
	for (i = 0; i < txb->nr_frags; i++) {
		queue_len = skb_queue_len(&ieee->skb_waitQ[queue_index]);
		if ((queue_len  != 0) ||
		    (!ieee->check_nic_enough_desc(ieee->dev, queue_index)) ||
		    (ieee->queue_stop)) {
			/* insert the skb packet to the wait queue
			 * as for the completion function, it does not need
			 * to check it any more.
			 */
			if (queue_len < 200)
				skb_queue_tail(&ieee->skb_waitQ[queue_index],
					       txb->fragments[i]);
			else
				kfree_skb(txb->fragments[i]);
		} else {
			ieee->softmac_data_hard_start_xmit(
					txb->fragments[i],
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
		netdev_get_tx_queue(ieee->dev, i)->trans_start = jiffies;

	netif_tx_stop_all_queues(ieee->dev);
}

void rtllib_wake_all_queues(struct rtllib_device *ieee)
{
	netif_tx_wake_all_queues(ieee->dev);
}

/* called in user context only */
static void rtllib_start_master_bss(struct rtllib_device *ieee)
{
	ieee->assoc_id = 1;

	if (ieee->current_network.ssid_len == 0) {
		strncpy(ieee->current_network.ssid,
			RTLLIB_DEFAULT_TX_ESSID,
			IW_ESSID_MAX_SIZE);

		ieee->current_network.ssid_len =
				 strlen(RTLLIB_DEFAULT_TX_ESSID);
		ieee->ssid_set = 1;
	}

	ether_addr_copy(ieee->current_network.bssid, ieee->dev->dev_addr);

	ieee->set_chan(ieee->dev, ieee->current_network.channel);
	ieee->state = RTLLIB_LINKED;
	ieee->link_change(ieee->dev);
	notify_wx_assoc_event(ieee);

	if (ieee->data_hard_resume)
		ieee->data_hard_resume(ieee->dev);

	netif_carrier_on(ieee->dev);
}

static void rtllib_start_monitor_mode(struct rtllib_device *ieee)
{
	/* reset hardware status */
	if (ieee->raw_tx) {
		if (ieee->data_hard_resume)
			ieee->data_hard_resume(ieee->dev);

		netif_carrier_on(ieee->dev);
	}
}

static void rtllib_start_ibss_wq(void *data)
{
	struct rtllib_device *ieee = container_of_dwork_rsl(data,
				     struct rtllib_device, start_ibss_wq);
	/* iwconfig mode ad-hoc will schedule this and return
	 * on the other hand this will block further iwconfig SET
	 * operations because of the wx_mutex hold.
	 * Anyway some most set operations set a flag to speed-up
	 * (abort) this wq (when syncro scanning) before sleeping
	 * on the mutex
	 */
	if (!ieee->proto_started) {
		netdev_info(ieee->dev, "==========oh driver down return\n");
		return;
	}
	mutex_lock(&ieee->wx_mutex);

	if (ieee->current_network.ssid_len == 0) {
		strcpy(ieee->current_network.ssid, RTLLIB_DEFAULT_TX_ESSID);
		ieee->current_network.ssid_len = strlen(RTLLIB_DEFAULT_TX_ESSID);
		ieee->ssid_set = 1;
	}

	ieee->state = RTLLIB_NOLINK;
	ieee->mode = IEEE_G;
	/* check if we have this cell in our network list */
	rtllib_softmac_check_all_nets(ieee);


	/* if not then the state is not linked. Maybe the user switched to
	 * ad-hoc mode just after being in monitor mode, or just after
	 * being very few time in managed mode (so the card have had no
	 * time to scan all the chans..) or we have just run up the iface
	 * after setting ad-hoc mode. So we have to give another try..
	 * Here, in ibss mode, should be safe to do this without extra care
	 * (in bss mode we had to make sure no-one tried to associate when
	 * we had just checked the ieee->state and we was going to start the
	 * scan) because in ibss mode the rtllib_new_net function, when
	 * finds a good net, just set the ieee->state to RTLLIB_LINKED,
	 * so, at worst, we waste a bit of time to initiate an unneeded syncro
	 * scan, that will stop at the first round because it sees the state
	 * associated.
	 */
	if (ieee->state == RTLLIB_NOLINK)
		rtllib_start_scan_syncro(ieee, 0);

	/* the network definitively is not here.. create a new cell */
	if (ieee->state == RTLLIB_NOLINK) {
		netdev_info(ieee->dev, "creating new IBSS cell\n");
		ieee->current_network.channel = ieee->IbssStartChnl;
		if (!ieee->wap_set)
			eth_random_addr(ieee->current_network.bssid);

		if (ieee->modulation & RTLLIB_CCK_MODULATION) {

			ieee->current_network.rates_len = 4;

			ieee->current_network.rates[0] =
				 RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_1MB;
			ieee->current_network.rates[1] =
				 RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_2MB;
			ieee->current_network.rates[2] =
				 RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_5MB;
			ieee->current_network.rates[3] =
				 RTLLIB_BASIC_RATE_MASK | RTLLIB_CCK_RATE_11MB;

		} else
			ieee->current_network.rates_len = 0;

		if (ieee->modulation & RTLLIB_OFDM_MODULATION) {
			ieee->current_network.rates_ex_len = 8;

			ieee->current_network.rates_ex[0] =
						 RTLLIB_OFDM_RATE_6MB;
			ieee->current_network.rates_ex[1] =
						 RTLLIB_OFDM_RATE_9MB;
			ieee->current_network.rates_ex[2] =
						 RTLLIB_OFDM_RATE_12MB;
			ieee->current_network.rates_ex[3] =
						 RTLLIB_OFDM_RATE_18MB;
			ieee->current_network.rates_ex[4] =
						 RTLLIB_OFDM_RATE_24MB;
			ieee->current_network.rates_ex[5] =
						 RTLLIB_OFDM_RATE_36MB;
			ieee->current_network.rates_ex[6] =
						 RTLLIB_OFDM_RATE_48MB;
			ieee->current_network.rates_ex[7] =
						 RTLLIB_OFDM_RATE_54MB;

			ieee->rate = 108;
		} else {
			ieee->current_network.rates_ex_len = 0;
			ieee->rate = 22;
		}

		ieee->current_network.qos_data.supported = 0;
		ieee->SetWirelessMode(ieee->dev, IEEE_G);
		ieee->current_network.mode = ieee->mode;
		ieee->current_network.atim_window = 0;
		ieee->current_network.capability = WLAN_CAPABILITY_IBSS;
	}

	netdev_info(ieee->dev, "%s(): ieee->mode = %d\n", __func__, ieee->mode);
	if ((ieee->mode == IEEE_N_24G) || (ieee->mode == IEEE_N_5G))
		HTUseDefaultSetting(ieee);
	else
		ieee->pHTInfo->bCurrentHTSupport = false;

	ieee->SetHwRegHandler(ieee->dev, HW_VAR_MEDIA_STATUS,
			      (u8 *)(&ieee->state));

	ieee->state = RTLLIB_LINKED;
	ieee->link_change(ieee->dev);

	HTSetConnectBwMode(ieee, HT_CHANNEL_WIDTH_20, HT_EXTCHNL_OFFSET_NO_EXT);
	if (ieee->LedControlHandler != NULL)
		ieee->LedControlHandler(ieee->dev, LED_CTL_LINK);

	rtllib_start_send_beacons(ieee);

	notify_wx_assoc_event(ieee);

	if (ieee->data_hard_resume)
		ieee->data_hard_resume(ieee->dev);

	netif_carrier_on(ieee->dev);

	mutex_unlock(&ieee->wx_mutex);
}

inline void rtllib_start_ibss(struct rtllib_device *ieee)
{
	schedule_delayed_work(&ieee->start_ibss_wq, msecs_to_jiffies(150));
}

/* this is called only in user context, with wx_mutex held */
static void rtllib_start_bss(struct rtllib_device *ieee)
{
	unsigned long flags;

	if (IS_DOT11D_ENABLE(ieee) && !IS_COUNTRY_IE_VALID(ieee)) {
		if (!ieee->bGlobalDomain)
			return;
	}
	/* check if we have already found the net we
	 * are interested in (if any).
	 * if not (we are disassociated and we are not
	 * in associating / authenticating phase) start the background scanning.
	 */
	rtllib_softmac_check_all_nets(ieee);

	/* ensure no-one start an associating process (thus setting
	 * the ieee->state to rtllib_ASSOCIATING) while we
	 * have just checked it and we are going to enable scan.
	 * The rtllib_new_net function is always called with
	 * lock held (from both rtllib_softmac_check_all_nets and
	 * the rx path), so we cannot be in the middle of such function
	 */
	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->state == RTLLIB_NOLINK)
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

	if (ieee->data_hard_stop)
		ieee->data_hard_stop(ieee->dev);
	if (IS_DOT11D_ENABLE(ieee))
		Dot11d_Reset(ieee);
	ieee->state = RTLLIB_NOLINK;
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

	if (ieee->state != RTLLIB_ASSOCIATING_RETRY)
		goto exit;

	/* until we do not set the state to RTLLIB_NOLINK
	 * there are no possibility to have someone else trying
	 * to start an association procedure (we get here with
	 * ieee->state = RTLLIB_ASSOCIATING).
	 * When we set the state to RTLLIB_NOLINK it is possible
	 * that the RX path run an attempt to associate, but
	 * both rtllib_softmac_check_all_nets and the
	 * RX path works with ieee->lock held so there are no
	 * problems. If we are still disassociated then start a scan.
	 * the lock here is necessary to ensure no one try to start
	 * an association procedure when we have just checked the
	 * state and we are going to start the scan.
	 */
	ieee->beinretry = true;
	ieee->state = RTLLIB_NOLINK;

	rtllib_softmac_check_all_nets(ieee);

	spin_lock_irqsave(&ieee->lock, flags);

	if (ieee->state == RTLLIB_NOLINK)
		rtllib_start_scan(ieee);
	spin_unlock_irqrestore(&ieee->lock, flags);

	ieee->beinretry = false;
exit:
	mutex_unlock(&ieee->wx_mutex);
}

static struct sk_buff *rtllib_get_beacon_(struct rtllib_device *ieee)
{
	static const u8 broadcast_addr[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	struct sk_buff *skb;
	struct rtllib_probe_response *b;

	skb = rtllib_probe_resp(ieee, broadcast_addr);

	if (!skb)
		return NULL;

	b = (struct rtllib_probe_response *) skb->data;
	b->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_BEACON);

	return skb;

}

struct sk_buff *rtllib_get_beacon(struct rtllib_device *ieee)
{
	struct sk_buff *skb;
	struct rtllib_probe_response *b;

	skb = rtllib_get_beacon_(ieee);
	if (!skb)
		return NULL;

	b = (struct rtllib_probe_response *) skb->data;
	b->header.seq_ctl = cpu_to_le16(ieee->seq_ctrl[0] << 4);

	if (ieee->seq_ctrl[0] == 0xFFF)
		ieee->seq_ctrl[0] = 0;
	else
		ieee->seq_ctrl[0]++;

	return skb;
}
EXPORT_SYMBOL(rtllib_get_beacon);

void rtllib_softmac_stop_protocol(struct rtllib_device *ieee, u8 mesh_flag,
				  u8 shutdown)
{
	rtllib_stop_scan_syncro(ieee);
	mutex_lock(&ieee->wx_mutex);
	rtllib_stop_protocol(ieee, shutdown);
	mutex_unlock(&ieee->wx_mutex);
}
EXPORT_SYMBOL(rtllib_softmac_stop_protocol);


void rtllib_stop_protocol(struct rtllib_device *ieee, u8 shutdown)
{
	if (!ieee->proto_started)
		return;

	if (shutdown) {
		ieee->proto_started = 0;
		ieee->proto_stoppping = 1;
		if (ieee->rtllib_ips_leave != NULL)
			ieee->rtllib_ips_leave(ieee->dev);
	}

	rtllib_stop_send_beacons(ieee);
	del_timer_sync(&ieee->associate_timer);
	cancel_delayed_work_sync(&ieee->associate_retry_wq);
	cancel_delayed_work_sync(&ieee->start_ibss_wq);
	cancel_delayed_work_sync(&ieee->link_change_wq);
	rtllib_stop_scan(ieee);

	if (ieee->state <= RTLLIB_ASSOCIATING_AUTHENTICATED)
		ieee->state = RTLLIB_NOLINK;

	if (ieee->state == RTLLIB_LINKED) {
		if (ieee->iw_mode == IW_MODE_INFRA)
			SendDisassociation(ieee, 1, WLAN_REASON_DEAUTH_LEAVING);
		rtllib_disassociate(ieee);
	}

	if (shutdown) {
		RemoveAllTS(ieee);
		ieee->proto_stoppping = 0;
	}
	kfree(ieee->assocreq_ies);
	ieee->assocreq_ies = NULL;
	ieee->assocreq_ies_len = 0;
	kfree(ieee->assocresp_ies);
	ieee->assocresp_ies = NULL;
	ieee->assocresp_ies_len = 0;
}

void rtllib_softmac_start_protocol(struct rtllib_device *ieee, u8 mesh_flag)
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

	rtllib_update_active_chan_map(ieee);

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

	if (ieee->UpdateBeaconInterruptHandler)
		ieee->UpdateBeaconInterruptHandler(ieee->dev, false);

	ieee->wmm_acm = 0;
	/* if the user set the MAC of the ad-hoc cell and then
	 * switch to managed mode, shall we  make sure that association
	 * attempts does not fail just because the user provide the essid
	 * and the nic is still checking for the AP MAC ??
	 */
	if (ieee->iw_mode == IW_MODE_INFRA) {
		rtllib_start_bss(ieee);
	} else if (ieee->iw_mode == IW_MODE_ADHOC) {
		if (ieee->UpdateBeaconInterruptHandler)
			ieee->UpdateBeaconInterruptHandler(ieee->dev, true);

		rtllib_start_ibss(ieee);

	} else if (ieee->iw_mode == IW_MODE_MASTER) {
		rtllib_start_master_bss(ieee);
	} else if (ieee->iw_mode == IW_MODE_MONITOR) {
		rtllib_start_monitor_mode(ieee);
	}
}

void rtllib_softmac_init(struct rtllib_device *ieee)
{
	int i;

	memset(&ieee->current_network, 0, sizeof(struct rtllib_network));

	ieee->state = RTLLIB_NOLINK;
	for (i = 0; i < 5; i++)
		ieee->seq_ctrl[i] = 0;
	ieee->pDot11dInfo = kzalloc(sizeof(struct rt_dot11d_info), GFP_ATOMIC);
	if (!ieee->pDot11dInfo)
		netdev_err(ieee->dev, "Can't alloc memory for DOT11D\n");
	ieee->LinkDetectInfo.SlotIndex = 0;
	ieee->LinkDetectInfo.SlotNum = 2;
	ieee->LinkDetectInfo.NumRecvBcnInPeriod = 0;
	ieee->LinkDetectInfo.NumRecvDataInPeriod = 0;
	ieee->LinkDetectInfo.NumTxOkInPeriod = 0;
	ieee->LinkDetectInfo.NumRxOkInPeriod = 0;
	ieee->LinkDetectInfo.NumRxUnicastOkInPeriod = 0;
	ieee->bIsAggregateFrame = false;
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

	ieee->Regdot11HTOperationalRateSet[0] = 0xff;
	ieee->Regdot11HTOperationalRateSet[1] = 0xff;
	ieee->Regdot11HTOperationalRateSet[4] = 0x01;

	ieee->Regdot11TxHTOperationalRateSet[0] = 0xff;
	ieee->Regdot11TxHTOperationalRateSet[1] = 0xff;
	ieee->Regdot11TxHTOperationalRateSet[4] = 0x01;

	ieee->FirstIe_InScan = false;
	ieee->actscanning = false;
	ieee->beinretry = false;
	ieee->is_set_key = false;
	init_mgmt_queue(ieee);

	ieee->tx_pending.txb = NULL;

	timer_setup(&ieee->associate_timer, rtllib_associate_abort_cb, 0);

	timer_setup(&ieee->beacon_timer, rtllib_send_beacon_cb, 0);

	INIT_DELAYED_WORK_RSL(&ieee->link_change_wq,
			      (void *)rtllib_link_change_wq, ieee);
	INIT_DELAYED_WORK_RSL(&ieee->start_ibss_wq,
			      (void *)rtllib_start_ibss_wq, ieee);
	INIT_WORK_RSL(&ieee->associate_complete_wq,
		      (void *)rtllib_associate_complete_wq, ieee);
	INIT_DELAYED_WORK_RSL(&ieee->associate_procedure_wq,
			      (void *)rtllib_associate_procedure_wq, ieee);
	INIT_DELAYED_WORK_RSL(&ieee->softmac_scan_wq,
			      (void *)rtllib_softmac_scan_wq, ieee);
	INIT_DELAYED_WORK_RSL(&ieee->associate_retry_wq,
			      (void *)rtllib_associate_retry_wq, ieee);
	INIT_WORK_RSL(&ieee->wx_sync_scan_wq, (void *)rtllib_wx_sync_scan_wq,
		      ieee);

	mutex_init(&ieee->wx_mutex);
	mutex_init(&ieee->scan_mutex);
	mutex_init(&ieee->ips_mutex);

	spin_lock_init(&ieee->mgmt_tx_lock);
	spin_lock_init(&ieee->beacon_lock);

	tasklet_init(&ieee->ps_task,
	     (void(*)(unsigned long)) rtllib_sta_ps,
	     (unsigned long)ieee);

}

void rtllib_softmac_free(struct rtllib_device *ieee)
{
	mutex_lock(&ieee->wx_mutex);
	kfree(ieee->pDot11dInfo);
	ieee->pDot11dInfo = NULL;
	del_timer_sync(&ieee->associate_timer);

	cancel_delayed_work_sync(&ieee->associate_retry_wq);
	cancel_delayed_work_sync(&ieee->associate_procedure_wq);
	cancel_delayed_work_sync(&ieee->softmac_scan_wq);
	cancel_delayed_work_sync(&ieee->start_ibss_wq);
	cancel_delayed_work_sync(&ieee->hw_wakeup_wq);
	cancel_delayed_work_sync(&ieee->hw_sleep_wq);
	cancel_delayed_work_sync(&ieee->link_change_wq);
	cancel_work_sync(&ieee->associate_complete_wq);
	cancel_work_sync(&ieee->ips_leave_wq);
	cancel_work_sync(&ieee->wx_sync_scan_wq);
	mutex_unlock(&ieee->wx_mutex);
	tasklet_kill(&ieee->ps_task);
}

static inline struct sk_buff *
rtllib_disauth_skb(struct rtllib_network *beacon,
		   struct rtllib_device *ieee, u16 asRsn)
{
	struct sk_buff *skb;
	struct rtllib_disauth *disauth;
	int len = sizeof(struct rtllib_disauth) + ieee->tx_headroom;

	skb = dev_alloc_skb(len);
	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	disauth = skb_put(skb, sizeof(struct rtllib_disauth));
	disauth->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_DEAUTH);
	disauth->header.duration_id = 0;

	ether_addr_copy(disauth->header.addr1, beacon->bssid);
	ether_addr_copy(disauth->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(disauth->header.addr3, beacon->bssid);

	disauth->reason = cpu_to_le16(asRsn);
	return skb;
}

static inline struct sk_buff *
rtllib_disassociate_skb(struct rtllib_network *beacon,
			struct rtllib_device *ieee, u16 asRsn)
{
	struct sk_buff *skb;
	struct rtllib_disassoc *disass;
	int len = sizeof(struct rtllib_disassoc) + ieee->tx_headroom;

	skb = dev_alloc_skb(len);

	if (!skb)
		return NULL;

	skb_reserve(skb, ieee->tx_headroom);

	disass = skb_put(skb, sizeof(struct rtllib_disassoc));
	disass->header.frame_ctl = cpu_to_le16(RTLLIB_STYPE_DISASSOC);
	disass->header.duration_id = 0;

	ether_addr_copy(disass->header.addr1, beacon->bssid);
	ether_addr_copy(disass->header.addr2, ieee->dev->dev_addr);
	ether_addr_copy(disass->header.addr3, beacon->bssid);

	disass->reason = cpu_to_le16(asRsn);
	return skb;
}

void SendDisassociation(struct rtllib_device *ieee, bool deauth, u16 asRsn)
{
	struct rtllib_network *beacon = &ieee->current_network;
	struct sk_buff *skb;

	if (deauth)
		skb = rtllib_disauth_skb(beacon, ieee, asRsn);
	else
		skb = rtllib_disassociate_skb(beacon, ieee, asRsn);

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
		  || (ieee->host_encrypt && crypt && crypt->ops &&
		  (strcmp(crypt->ops->name, "R-WEP") == 0));

	/* simply judge  */
	if (encrypt && (wpa_ie_len == 0)) {
		return SEC_ALG_WEP;
	} else if ((wpa_ie_len != 0)) {
		if (((ieee->wpa_ie[0] == 0xdd) &&
		    (!memcmp(&(ieee->wpa_ie[14]), ccmp_ie, 4))) ||
		    ((ieee->wpa_ie[0] == 0x30) &&
		    (!memcmp(&ieee->wpa_ie[10], ccmp_rsn_ie, 4))))
			return SEC_ALG_CCMP;
		else
			return SEC_ALG_TKIP;
	} else {
		return SEC_ALG_NONE;
	}
}

static void rtllib_MgntDisconnectIBSS(struct rtllib_device *rtllib)
{
	u8	OpMode;
	u8	i;
	bool	bFilterOutNonAssociatedBSSID = false;

	rtllib->state = RTLLIB_NOLINK;

	for (i = 0; i < 6; i++)
		rtllib->current_network.bssid[i] = 0x55;

	rtllib->OpMode = RT_OP_MODE_NO_LINK;
	rtllib->SetHwRegHandler(rtllib->dev, HW_VAR_BSSID,
				rtllib->current_network.bssid);
	OpMode = RT_OP_MODE_NO_LINK;
	rtllib->SetHwRegHandler(rtllib->dev, HW_VAR_MEDIA_STATUS, &OpMode);
	rtllib_stop_send_beacons(rtllib);

	bFilterOutNonAssociatedBSSID = false;
	rtllib->SetHwRegHandler(rtllib->dev, HW_VAR_CECHK_BSSID,
				(u8 *)(&bFilterOutNonAssociatedBSSID));
	notify_wx_assoc_event(rtllib);

}

static void rtllib_MlmeDisassociateRequest(struct rtllib_device *rtllib,
					   u8 *asSta, u8 asRsn)
{
	u8 i;
	u8	OpMode;

	RemovePeerTS(rtllib, asSta);

	if (memcmp(rtllib->current_network.bssid, asSta, 6) == 0) {
		rtllib->state = RTLLIB_NOLINK;

		for (i = 0; i < 6; i++)
			rtllib->current_network.bssid[i] = 0x22;
		OpMode = RT_OP_MODE_NO_LINK;
		rtllib->OpMode = RT_OP_MODE_NO_LINK;
		rtllib->SetHwRegHandler(rtllib->dev, HW_VAR_MEDIA_STATUS,
					(u8 *)(&OpMode));
		rtllib_disassociate(rtllib);

		rtllib->SetHwRegHandler(rtllib->dev, HW_VAR_BSSID,
					rtllib->current_network.bssid);

	}

}

static void
rtllib_MgntDisconnectAP(
	struct rtllib_device *rtllib,
	u8 asRsn
)
{
	bool bFilterOutNonAssociatedBSSID = false;

	bFilterOutNonAssociatedBSSID = false;
	rtllib->SetHwRegHandler(rtllib->dev, HW_VAR_CECHK_BSSID,
				(u8 *)(&bFilterOutNonAssociatedBSSID));
	rtllib_MlmeDisassociateRequest(rtllib, rtllib->current_network.bssid,
				       asRsn);

	rtllib->state = RTLLIB_NOLINK;
}

bool rtllib_MgntDisconnect(struct rtllib_device *rtllib, u8 asRsn)
{
	if (rtllib->ps != RTLLIB_PS_DISABLED)
		rtllib->sta_wake_up(rtllib->dev);

	if (rtllib->state == RTLLIB_LINKED) {
		if (rtllib->iw_mode == IW_MODE_ADHOC)
			rtllib_MgntDisconnectIBSS(rtllib);
		if (rtllib->iw_mode == IW_MODE_INFRA)
			rtllib_MgntDisconnectAP(rtllib, asRsn);

	}

	return true;
}
EXPORT_SYMBOL(rtllib_MgntDisconnect);

void notify_wx_assoc_event(struct rtllib_device *ieee)
{
	union iwreq_data wrqu;

	if (ieee->cannot_notify)
		return;

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (ieee->state == RTLLIB_LINKED)
		memcpy(wrqu.ap_addr.sa_data, ieee->current_network.bssid,
		       ETH_ALEN);
	else {

		netdev_info(ieee->dev, "%s(): Tell user space disconnected\n",
			    __func__);
		eth_zero_addr(wrqu.ap_addr.sa_data);
	}
	wireless_send_event(ieee->dev, SIOCGIWAP, &wrqu, NULL);
}
EXPORT_SYMBOL(notify_wx_assoc_event);
